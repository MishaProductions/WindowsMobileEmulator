/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/
#include "emulator.h"
#pragma warning (disable:4995) // name was marked as #pragma deprecated - triggered by strsafe.h
#include <ShellAPI.h> // for CommandLineToArgvW()
#include <shlobj.h>   // for SHGetFolderPath()
#include <atlbase.h>  // for self-registration
#pragma warning(default:4995)
#include "cmerror.h"  // for common error codes
#include "Config.h"
#include "MappedIO.h"
#include "COMInterface.h"
#include "Devices.h"
#include "resource.h"
#include "wincontroller.h"
#include "decfg.h"
#include "Board.h"
#include "cpu.h"

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "cominterface.tmh"
#include "vsd_logging_inc.h"

#if !FEATURE_COM_INTERFACE
// This file (along with vcecominterfaces.idl and vcvcecominterfaces_i.c) should be
// built only for emulators that support COM interop back to a host via
// IDeviceEmulatorVirtualMachineManager and IDeviceEmulatorVirtualMachineTransport.
#error FEATURE_COM_INTERFACE should be specified if this file is built
#endif //!FEATURE_COM_INTERFACE

extern bool LoadImageOrRestoreState(void);
extern HRESULT SetSaveStateFileNameFromVMID(bool Global);

class CEmulatorItem g_EmulatorItem;
HANDLE g_hVMIDObjectEvent = NULL;
DWORD  g_dwRegister = 0;

DMAChannelRecordSet ChannelRecordSet;
DMAAddressService AddressService;

bool RegisterUnregisterServer(BOOL bRegister)
{
    HRESULT hr;
    WCHAR szModPath[_MAX_PATH + 1];

    const DWORD dwModPathLen = GetModuleFileNameW(NULL, szModPath, ARRAY_SIZE(szModPath));

    if (dwModPathLen == 0 || dwModPathLen >= ARRAY_SIZE(szModPath)) {
            return false;
    }

    ATL::CRegObject reg;
    hr = reg.FinalConstruct();
    if (FAILED(hr))
    {
        return false;
    }

    hr = reg.AddReplacement(L"MODULE", szModPath);
    if (hr != S_OK)
        return false;

    if (bRegister) {
        hr = reg.ResourceRegister(szModPath, IDR_REGISTRY, L"REGISTRY");
    } else {
        hr = reg.ResourceUnregister(szModPath, IDR_REGISTRY, L"REGISTRY");
    }

        if (FAILED(hr)) {
            LOG_ERROR(DMA, "RegisterUnregisterServer failed.  bRegister=%d, hr=%x", bRegister, hr);
            return false;
        }

        LOG_INFO(DMA, "RegisterUnregisterServer succeeded.  bRegister=%d", bRegister);
        return true;
}

bool CreateVMIDObjectName(__out_ecount(BufferLength) WCHAR *Buffer, int BufferLength)
{
    WCHAR VMIDString[40]; // large enough for a GUID - "{c200e360-38c5-11ce-ae62-08002b2b79ef}"

    if (StringFromGUID2(Configuration.VMID, VMIDString, ARRAY_SIZE(VMIDString)) == 0) {
        ASSERT(FALSE);  // string ought to fit
        return false;
    }

    if (FAILED(StringCchPrintfW(Buffer, BufferLength, L"Local\\DeviceEmulator_VMID_%s", VMIDString))) {
        ASSERT(FALSE); // string ought to fit
        return false;
    }
    return true;
}

// Note: "Name" must have space for at least MAX_PATH+wcslen("\\Microsoft Device Emulator")+1 characters
HRESULT GetSavedStateDirectory(__out_ecount(cchName) wchar_t *Name, size_t cchName, bool Global )
{
    HRESULT hr;

    // Retrive the "%userprofile%\Application Data" directory path
    if (cchName < _MAX_PATH) {
        // Protect against SHGetFolderPath, which requires _MAX_PATH-sized buffer
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }
    if (Global)
        hr = SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, SHGFP_TYPE_CURRENT, Name);
    else
        hr = SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, Name);

    if (FAILED(hr)) {
        return hr;
    }

    // Append "Microsoft\Device Emulator"
    hr = StringCchCatW(Name, _MAX_PATH, L"\\Microsoft\\" EMULATOR_NAME_W);
    if (FAILED(hr)) {
        return hr;
    }
    return S_OK;
}

// Note: much of the logic in this routine is duplicated in CEmulatorVirtualMachineManager::DeleteVirtualMachine()
HRESULT SetSaveStateFileNameFromVMID(bool Global)
{
    wchar_t Name[2*_MAX_PATH];
    wchar_t VMIDName[40];
    HRESULT hr;

    hr = GetSavedStateDirectory(Name, ARRAY_SIZE(Name), Global);
    if (FAILED(hr)) {
        return hr;
    }

    // Create the directory, silently ignoring failures (such as "directory already exists")
    CreateDirectory(Name, NULL);

    // Append "\{vmid_guid}.dess" to turn Name into the save-state filename
    hr = StringCchCatW(Name, ARRAY_SIZE(Name), L"\\");
    if (FAILED(hr)) {
        return hr;
    }
    hr = StringFromGUID2(Configuration.VMID, VMIDName, ARRAY_SIZE(VMIDName));
    if (FAILED(hr)) {
        return hr;
    }
    hr = StringCchCatW(Name, ARRAY_SIZE(Name), VMIDName);
    if (FAILED(hr)) {
        return hr;
    }
    hr = StringCchCatW(Name, ARRAY_SIZE(Name), L".dess");  // "Device Emulator Save State" file
    if (FAILED(hr)) {
        return hr;
    }
    Configuration.setSaveStateFileName(Name);
    return S_OK;
}

CEmulatorItem::CEmulatorItem()
{
    m_RefCount=1;
}

CEmulatorItem::~CEmulatorItem()
{
}

HRESULT __stdcall CEmulatorItem::QueryInterface(REFIID  inInterfaceID,void** outInterface)
{
    if (inInterfaceID == IID_IDeviceEmulatorItem) {
        *outInterface = static_cast<IDeviceEmulatorItem*>(this);
        AddRef();
        return S_OK;
    } else if (inInterfaceID == IID_IOleItemContainer) {
        *outInterface = static_cast<IOleItemContainer*>(this);
        AddRef();
        return S_OK;
    } else if (inInterfaceID == IID_IOleContainer) {
        *outInterface = static_cast<IOleContainer*>(this);
        AddRef();
        return S_OK;
    } else if (inInterfaceID == IID_IParseDisplayName) {
        *outInterface = static_cast<IParseDisplayName*>(this);
        AddRef();
        return S_OK;
    } else if (inInterfaceID == IID_IUnknown) {
        *outInterface = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    } else {
        *outInterface = NULL;
        return E_NOINTERFACE;
    }
}
ULONG __stdcall CEmulatorItem::AddRef()
{
    return InterlockedIncrement((LONG*)&m_RefCount);
}

ULONG __stdcall CEmulatorItem::Release()
{
    if (InterlockedDecrement((LONG*)&m_RefCount) == 0)
    {
        // the CEmulatorItem is created as a local variable so do not "delete" it.
        //delete this;
        return 0;
    }
    return m_RefCount;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::ParseDisplayName(
    IBindCtx * pbc,
    LPOLESTR pszDisplayName,
    ULONG * pchEaten,
    IMoniker ** ppmkOut
    )
{
    HRESULT hr;
    GUID VMID;

    hr = IIDFromString(pszDisplayName, &VMID);
    if (FAILED(hr)) {
        return MK_E_SYNTAX;
    }
    hr = CreateItemMoniker(L"!", pszDisplayName, ppmkOut);
    if (FAILED(hr)) {
        return hr;
    }
    *pchEaten += 38;  // length of "{guid}"
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::EnumObjects(
    DWORD grfFlags,
    IEnumUnknown **ppenum
    )
{
    return E_NOTIMPL; // Enumeration is not required.
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::LockContainer(
    BOOL fLock  //Value indicating lock or unlock
    )
{
    if (fLock) {
        AddRef();
    } else {
        Release();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::GetObject(
    LPOLESTR pszItem,
    DWORD dwSpeedNeeded,
    IBindCtx * pbc,
    REFIID riid,
    void ** ppvObject
    )
{
    HRESULT hr;
    GUID VMID;

    hr = IIDFromString(pszItem, &VMID);
    if (FAILED(hr)) {
        return hr;
    }
    if (VMID != Configuration.VMID) {
        return MK_E_NOOBJECT;
    }
    if (riid != IID_IDeviceEmulatorItem) {
        return E_NOINTERFACE;
    }
    AddRef();
    *ppvObject = static_cast<IDeviceEmulatorItem*>(this);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::GetObjectStorage(
    LPOLESTR pszItem,
    IBindCtx * pbc,
    REFIID riid,
    void ** ppvStorage
    )
{
    return MK_E_NOSTORAGE; // The object does not have its own independent storage.
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::IsRunning(
    LPOLESTR pszItem
    )
{
    HRESULT hr;
    GUID VMID;

    if (wcsncmp(pszItem, EMULATOR_NAME_W, wcslen(EMULATOR_NAME_W)) != 0) {
        return MK_E_NOOBJECT;
    }
    pszItem += wcslen(EMULATOR_NAME_W);
    if (*pszItem != L' ') {
        return MK_E_NOOBJECT;
    }
    pszItem++;

    hr = IIDFromString(pszItem, &VMID);
    if (FAILED(hr)) {
        return hr;
    }
    if (VMID != Configuration.VMID) {
        return MK_E_NOOBJECT;
    }

    // This is part of IOleItemContainer, and is enquiring about
    // the item moniker, so the running vs. not-running should
    // specifically check whether *this* process is running the
    // moniker.  It should not check if *any* process is running
    // it.
    //
    // Return S_OK if it is running, or S_FALSE if it isn't.
    if (fIsRunning) {
        return S_OK;
    } else {
        return S_FALSE;
    }
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::BringVirtualMachineToFront(void)
{
    LOG_VERBOSE(DMA, "CEmulatorItem::BringVirtualMachineToFront");
    HWND hWnd = WinCtrlInstance->GetWincontrollerHWND();

    if (hWnd) 
    {
        ShowWindowAsync(hWnd, SW_RESTORE );
        SetWindowPos(hWnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE);
        SetWindowPos(hWnd, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(hWnd);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::ResetVirtualMachine(
    /* [in] */ boolean hardReset)
{
    LOG_VERBOSE(DMA, "CEmulatorItem::ResetVirtualMachine hardReset=%d", hardReset);
    BoardReset((hardReset) ? true : false);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::ShutdownVirtualMachine(
    /* [in] */ boolean	saveMachine )
{
    LOG_VERBOSE(DMA, "CEmulatorItem::ShutdownVirtualMachine saveMachine=%d", saveMachine);
    if (saveMachine) {
        BoardSuspend();
    } else {
        exit(0);
    }
    return S_OK;
}


HRESULT STDMETHODCALLTYPE CEmulatorItem::BindToDMAChannel(
    /* [in] */ ULONG dmaChannel,
    /* [out][retval] */ IDeviceEmulatorDMAChannel** ppDMAChannel)
{
    HRESULT hr;

    hr = DMATransport.BindToDMAChannel(dmaChannel, ppDMAChannel);
    LOG_VERBOSE(DMA, "CEmulatorItem::BindToDMAChannel dmaChannel=%x hr=%x", dmaChannel, hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::GetVirtualMachineName(
    /* [out] */ LPOLESTR *virtualMachineName)
{
    IMalloc *pMalloc;
    HRESULT hr;
    size_t VMIDNameLength;
    wchar_t *VMIDName;
    wchar_t Buffer[40];  // large enough for a GUID

    LOG_VERBOSE(DMA, "CEmulatorItem::GetVirtualMachineName"); // the caller will log the return code at LOG_INFO level so don't log it here

    if (!virtualMachineName) {
        return E_INVALIDARG;
    }

    hr = CoGetMalloc(1, &pMalloc);
    if (FAILED(hr)) {
        return hr;
    }
    VMIDName = Configuration.getVMIDName();
    if (VMIDName == NULL) {
        if (StringFromGUID2(Configuration.VMID, Buffer, ARRAY_SIZE(Buffer)) == 0) {
            ASSERT(FALSE); // string ought to fit
            pMalloc->Release();
            return E_FAIL;
        }
        VMIDName = Buffer;
    }

    VMIDNameLength = (wcslen(VMIDName)+1)*sizeof(WCHAR);
    *virtualMachineName = (LPOLESTR)pMalloc->Alloc(VMIDNameLength);
    pMalloc->Release();
    if (!*virtualMachineName) {
        return E_OUTOFMEMORY;
    }
    memcpy(*virtualMachineName, VMIDName, VMIDNameLength);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::SetVirtualMachineName(
    /* [in] */ LPOLESTR virtualMachineName)
{
    LOG_VERBOSE(DMA, "CEmulatorItem::SetVirtualMachineName");
    if (!Configuration.setVMIDName(virtualMachineName)) {
        return E_OUTOFMEMORY;
    }

    HWND hWnd = WinCtrlInstance->GetWincontrollerHWND();

    if (hWnd) {
        SetWindowText(hWnd, virtualMachineName);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::GetMACAddressCount(
        /* [out, retval] */ ULONG* numberOfMACs )
{
    LOG_VERBOSE(DMA, "CEmulatorItem::GetMACAddressCount");
    *numberOfMACs = (Configuration.NetworkingEnabled ? 1 : 0) +
                    (Configuration.PCMCIACardInserted ? 1 : 0);
    LOG_INFO(DMA, "CEmulatorItem::GetMACAddressCount returns %d", *numberOfMACs);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::EnumerateMACAddresses(
        /* [in, out] */  ULONG* numberOfMacs,
        /* [out, size_is(*numberOfMacs*6)]*/ BYTE arrayOfMACAddresses[])
{
    LOG_VERBOSE(DMA, "CEmulatorItem::EnumerateMACAddresses");
    if ( !arrayOfMACAddresses || !numberOfMacs )
        return E_INVALIDARG;

    if ( *numberOfMacs == 0 )
        return GetMACAddressCount(numberOfMacs);

    ULONG currEntry = 0;

    // Make sure we are g
    EnterCriticalSection(&IOLock);

    if ( Configuration.NetworkingEnabled )
    {
        memcpy(arrayOfMACAddresses,CS8900IO.MacAddress, 6);
        currEntry++;
    }

    if (currEntry >= *numberOfMacs && Configuration.PCMCIACardInserted)
    {
        LeaveCriticalSection(&IOLock);
        return E_OUTOFMEMORY;
    }

    if ( Configuration.PCMCIACardInserted )
    {
        memcpy(&arrayOfMACAddresses[6*currEntry],Configuration.NE2000MACAddress, 6);
        currEntry++;
    }
    LeaveCriticalSection(&IOLock);

    *numberOfMacs = currEntry;

    LOG_INFO(DMA, "CEmulatorItem::EnumerateMACAddresses returns %d items", *numberOfMacs);
    return S_OK;
}

struct ShowDialogArgStruct {
    HWND hwndParent;
    BSTR *bstrConfig;
    BSTR* pbstrConfig;
};

DWORD WINAPI ShowDialogStatic(LPVOID lpvThreadParam)
{
    ShowDialogArgStruct * ShowDialogArgs = (ShowDialogArgStruct *)lpvThreadParam;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        goto Exit;
    }

    hr = ShowConfigDialog(ShowDialogArgs->hwndParent,
                        *(ShowDialogArgs->bstrConfig), ShowDialogArgs->pbstrConfig);

    CoUninitialize();

Exit:
    return (DWORD)hr;
}


HRESULT STDMETHODCALLTYPE CEmulatorItem::ConfigureDevice(
    /* [in] */ HWND hwndParent,
    /* [in] */ LCID lcidParent,
    /* [in] */ BSTR bstrConfig,
    /* [out] */ BSTR* pbstrConfig)
{
    HRESULT hr = S_OK;

    if ( !Configuration.DialogOnly )
    {
        // For now enforce that hwndParent is NULL as we don't use it
        if ( hwndParent != NULL)
            return E_INVALIDARG;

        HWND hWnd = (WinCtrlInstance ? (WinCtrlInstance->GetWincontrollerHWND()) : NULL);

        if (hWnd == NULL)
            return E_FAIL;

        // Post a message to the main dialog to get the configuration dialog to open
        // Note that the WndProc is running in an STA
        PostMessage(hWnd, WM_COMMAND, (WPARAM)ID_FILE_CONFIGURE, (LPARAM)0);
    }
    else
    {
        Configuration.Resources.setLanguage(LANGIDFROMLCID(lcidParent));
        // Some of the OLE functions called from the configuration dialog
        // must run from an STA. This function will be launched in an MTA by
        // RPC so another thread must be started to run configuration dialog correctly
        HANDLE hSTAThread;
        DWORD STAThreadId;
        ShowDialogArgStruct ShowDialogArgs;
        ShowDialogArgs.hwndParent = hwndParent;
        ShowDialogArgs.bstrConfig = &bstrConfig;
        ShowDialogArgs.pbstrConfig = pbstrConfig;
        hSTAThread = CreateThread(NULL, 0, ShowDialogStatic, &ShowDialogArgs, 0, &STAThreadId);
        if (hSTAThread == INVALID_HANDLE_VALUE) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        WaitForSingleObject(hSTAThread, INFINITE);
        DWORD retCode;
        GetExitCodeThread(hSTAThread, &retCode);
        hr = (HRESULT)retCode;
        CloseHandle(hSTAThread);
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE CEmulatorItem::GetDebuggerInterface(
        /* [retval][out] */ IDeviceEmulatorDebugger **ppDebugger)
{
    LOG_VERBOSE(DMA, "CEmulatorItem::GetDebuggerInterface");
    return CpuGetDebuggerInterface(ppDebugger);
}


CEmulatorDMAChannel::CEmulatorDMAChannel()
{
    m_RefCount = 0;
    m_Record   = NULL;
}

CEmulatorDMAChannel::~CEmulatorDMAChannel()
{
}

HRESULT __stdcall CEmulatorDMAChannel::QueryInterface(REFIID inInterfaceID,void** outInterface)
{
    if (inInterfaceID == IID_IDeviceEmulatorDMAChannel) {
        *outInterface = static_cast<IDeviceEmulatorDMAChannel*>(this);
        AddRef();
        return S_OK;
    } else if (inInterfaceID == IID_IUnknown) {
        *outInterface = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    } else {
        *outInterface = NULL;
        return E_NOINTERFACE;
    }
}
ULONG __stdcall CEmulatorDMAChannel::AddRef()
{
    return InterlockedIncrement((LONG*)&m_RefCount);
}

ULONG __stdcall CEmulatorDMAChannel::Release()
{
    if (InterlockedDecrement((LONG*)&m_RefCount) == 0)
    {
        m_Record->DetachChannel( DESKTOP_CONNECTED);
        return 0;
    }
    return m_RefCount;
}

void  CEmulatorDMAChannel::AttachToChannelIndex( DMAChannelRecord * Record )
{
   m_Record = Record;
   AddRef();
}

HRESULT STDMETHODCALLTYPE CEmulatorDMAChannel::Send(
    /* [in, size_is(byteCount)] */ const BYTE* dataBuffer,
    /* [in] */ USHORT byteCount)
{
    if ( m_Record->isAddrConnected() )
        return AddressService.writeAddressChannel(m_Record);
    else
        return DMATransport.Send(m_Record, dataBuffer, byteCount);
}

HRESULT STDMETHODCALLTYPE CEmulatorDMAChannel::Receive(
    /* [out, size_is(*byteCount), length_is(*byteCount)] */ BYTE* dataBuffer,
    /* [in, out] */ USHORT* byteCount,
    /* [in] */ ULONG Timeout)
{
    if ( m_Record->isAddrConnected() )
        return AddressService.readAddressChannel(m_Record, dataBuffer, byteCount);
    else
        return DMATransport.Receive(m_Record, dataBuffer, byteCount, Timeout);
}

bool __fastcall IODMATransport::PowerOn(void)
{
    return true;
}

bool __fastcall IODMATransport::Reset(void)
{
    ChannelRecordSet.disconnectAll(DEVICE_CONNECTED);
    return true;
}

void __fastcall IODMATransport::SaveState(StateFiler& filer) const
{
    filer.Write('DMAT');
    filer.LZWrite((unsigned __int8*)InputBuffer,sizeof(InputBuffer));
    filer.LZWrite((unsigned __int8*)OutputBuffer,sizeof(OutputBuffer));
    filer.Write(reinterpret_cast<const unsigned __int8*>(Flags),sizeof(Flags));
    filer.Write(InputDataLength);
    filer.Write(InputDataPtr);
    filer.Write(InputStatus);
    filer.Write(OutputDataLength);
    filer.Write(OutputDataPtr);
    filer.Write(OutputStatus);
    for (size_t i=0; i<NumberOfChannels; ++i) {
        filer.Write(DMAChannels[i].GlobalRegister);
        filer.Write(DMAChannels[i].FlagsRegister);
        filer.Write(DMAChannels[i].IOInputRegister);
        filer.Write(DMAChannels[i].IOOutputRegister);
        filer.Write(DMAChannels[i].IRQRegister);
        filer.Write(DMAChannels[i].IRQAcknowledgeRegister);
    }
    ChannelRecordSet.SaveState(filer);
    AddressService.SaveState(filer);
}

void __fastcall IODMATransport::RestoreState(StateFiler& filer)
{
    filer.Verify('DMAT');
    filer.LZRead(reinterpret_cast<unsigned __int8*>(InputBuffer),sizeof(InputBuffer));
    filer.LZRead(reinterpret_cast<unsigned __int8*>(OutputBuffer),sizeof(OutputBuffer));
    filer.Read(reinterpret_cast<unsigned __int8*>(Flags),sizeof(Flags));
    filer.Read(InputDataLength);
    filer.Read(InputDataPtr);
    filer.Read(InputStatus);
    filer.Read(OutputDataLength);
    filer.Read(OutputDataPtr);
    filer.Read(OutputStatus);
    for (size_t i=0; i<NumberOfChannels; ++i) {
        filer.Read(DMAChannels[i].GlobalRegister);
        filer.Read(DMAChannels[i].FlagsRegister);
        filer.Read(DMAChannels[i].IOInputRegister);
        filer.Read(DMAChannels[i].IOOutputRegister);
        filer.Read(DMAChannels[i].IRQRegister);
        filer.Read(DMAChannels[i].IRQAcknowledgeRegister);
    }
    ChannelRecordSet.RestoreState(filer);
    AddressService.RestoreState(filer);
}

unsigned __int8 __fastcall IODMATransport::ReadByte(unsigned __int32 IOAddress)
{
    if (IOAddress < BufferSizeDMA) {
        return InputBuffer[IOAddress];
    } else if (IOAddress < 2*BufferSizeDMA) {
        return OutputBuffer[IOAddress-BufferSizeDMA];
    } else {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

unsigned __int16 __fastcall IODMATransport::ReadHalf(unsigned __int32 IOAddress)
{
    if (IOAddress < BufferSizeDMA) {
        return *(unsigned __int16*)&InputBuffer[IOAddress];
    } else if (IOAddress < 2*BufferSizeDMA) {
        return *(unsigned __int16*)&OutputBuffer[IOAddress-BufferSizeDMA];
    } else {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

unsigned __int32 __fastcall IODMATransport::ReadWord(unsigned __int32 IOAddress)
{
    if (IOAddress < BufferSizeDMA) {
        return *(unsigned __int32*)&InputBuffer[IOAddress];
    } else if (IOAddress < 2*BufferSizeDMA) {
        return *(unsigned __int32*)&OutputBuffer[IOAddress-BufferSizeDMA];
    } else if (IOAddress < 2*BufferSizeDMA+NumberOfChannels*sizeof(unsigned __int32)) {
        unsigned __int32 ChannelNumber = (IOAddress-2*BufferSizeDMA)/sizeof(unsigned __int32);
        DMAChannelRecord * Channel = ChannelRecordSet.getChannel(ChannelNumber);
        if (Channel)
            return (( Flags[ChannelNumber] & ~1 ) | Channel->getFlags());
        else
            return Flags[ChannelNumber];
    } else if (IOAddress == InputDataOffset) {
        return InputDataLength;
    } else if (IOAddress == InputDataOffset+4) {
        return InputDataPtr;
    } else if (IOAddress == InputDataOffset+8) {
        return InputStatus;
    } else if (IOAddress == InputDataOffset+12) {
        return OutputDataLength;
    } else if (IOAddress == InputDataOffset+16) {
        return OutputDataPtr;
    } else if (IOAddress == InputDataOffset+20) {
        return OutputStatus;
    } else if (IOAddress >= 2*BufferSizeDMA+0x80 && IOAddress < 2*BufferSizeDMA+0x80+(NumberOfChannels*sizeof(DMAChannel))) {
        IOAddress -= 2*BufferSizeDMA+0x80;
        const unsigned __int32 ChannelNumber = IOAddress  / sizeof(DMAChannel);
        const unsigned __int32 ChannelOffset = IOAddress - (ChannelNumber*sizeof(DMAChannel));
        ASSERT(ChannelNumber < NumberOfChannels);
        switch (ChannelOffset) {
        case 0:
            return DMAChannels[ChannelNumber].GlobalRegister;
        case 4:
            return DMAChannels[ChannelNumber].FlagsRegister;
        case 8:
            return DMAChannels[ChannelNumber].IOInputRegister;
        case 0xc:
            return DMAChannels[ChannelNumber].IOOutputRegister;
        case 0x10:
            return DMAChannels[ChannelNumber].IRQRegister;
        case 0x14:
            return DMAChannels[ChannelNumber].IRQAcknowledgeRegister;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }
    } else if (IOAddress >= 2*BufferSizeDMA+0x80+(NumberOfChannels*sizeof(DMAChannel))&& 
               IOAddress < 2*BufferSizeDMA+0xA0+(NumberOfChannels*sizeof(DMAChannel))) {
        IOAddress -= 2*BufferSizeDMA+0x80 + (NumberOfChannels*sizeof(DMAChannel));
        ASSERT(CurrentVirtualChannel < NumberOfChannels || 
               CurrentVirtualChannel >= VIRT_CHANNEL_BASE);
        DMAChannelRecord * Channel = NULL;
        switch (IOAddress) {
        case 0:
            // This is set to on which the operation is to be performed
            return CurrentVirtualChannel;
            break;
        case 4:
            return VirtualOperation;
            break;
        case 8:
            return VirtualStatus;
            break;
        case 0xC:
            Channel = ChannelRecordSet.getChannel(VirtualChannelIndexF);
            if (Channel)
                return (Channel->getFlags() || Channel->isAddrConnected());
            else
                return 0;
            break;
        case 0x10:
            Channel = ChannelRecordSet.getChannel(VirtualChannelIndexR);
            if (Channel)
                return (Channel->getFlags() || Channel->isAddrConnected());
            else
                return 0;
            break;
        case 0x14:
            Channel = ChannelRecordSet.getChannel(VirtualChannelIndexW);
            if (Channel)
                return (Channel->getFlags() || Channel->isAddrConnected());
            else
                return 0;
            break;
            break;


        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
    } else {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void __fastcall IODMATransport::WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value)
{
    if (IOAddress < BufferSizeDMA) {
        InputBuffer[IOAddress] = Value;
    } else if (IOAddress < 2*BufferSizeDMA) {
        OutputBuffer[IOAddress-BufferSizeDMA] = Value;
    } else {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

void __fastcall IODMATransport::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value)
{
    if (IOAddress < BufferSizeDMA) {
        *(unsigned __int16*)&InputBuffer[IOAddress] = Value;
    } else if (IOAddress < 2*BufferSizeDMA) {
        *(unsigned __int16*)&OutputBuffer[IOAddress-BufferSizeDMA] = Value;
    } else {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

void __fastcall IODMATransport::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    if (IOAddress < BufferSizeDMA) {
        *(unsigned __int32*)&InputBuffer[IOAddress] = Value;
    } else if (IOAddress < 2*BufferSizeDMA) {
        *(unsigned __int32*)&OutputBuffer[IOAddress-BufferSizeDMA] = Value;
    } else if (IOAddress < 2*BufferSizeDMA+NumberOfChannels*sizeof(unsigned __int32)) {
         Flags[(IOAddress-2*BufferSizeDMA)/sizeof(unsigned __int32)] = Value;
    } else if (IOAddress == InputDataOffset) {
        InputDataLength = Value;
    } else if (IOAddress == InputDataOffset+4) {
        InputDataPtr = Value;
    } else if (IOAddress == InputDataOffset+8) {
        InputStatus = Value;
    } else if (IOAddress == InputDataOffset+12) {
        OutputDataLength = Value;
    } else if (IOAddress == InputDataOffset+16) {
        OutputDataPtr = Value;
    } else if (IOAddress == InputDataOffset+20) {
        OutputStatus = Value;
    } else if (IOAddress >= 2*BufferSizeDMA+0x80 && IOAddress < 2*BufferSizeDMA+0x80+(NumberOfChannels*sizeof(DMAChannel))) {
        IOAddress -= 2*BufferSizeDMA+0x80;
        const unsigned __int32 ChannelNumber = IOAddress  / sizeof(DMAChannel);
        const unsigned __int32 ChannelOffset = IOAddress - (ChannelNumber*sizeof(DMAChannel));
        DMAChannelRecord * Channel = NULL;
        ASSERT(ChannelNumber < NumberOfChannels);
        ASSERT(VirtualChannelIndexR == 0 || 
               VirtualChannelIndexR >= VIRT_CHANNEL_BASE );
        ASSERT(VirtualChannelIndexW == 0 || 
               VirtualChannelIndexW >= VIRT_CHANNEL_BASE );
                
        switch (ChannelOffset) {
        case 0:
            // This is set to 1 to check if the DMATransport device exists or not, then
            // is set to 0x101 to enable interrupts.
            DMAChannels[ChannelNumber].GlobalRegister = Value;
            break;
        case 4:
            DMAChannels[ChannelNumber].FlagsRegister = Value;
            break;
        case 8:
            DMAChannels[ChannelNumber].IOInputRegister = Value;
            // This initiates DMA to copy the data packet into WinCE
            // - InputDataLength is the size of the buffer to write into
            // - InputDataPtr is the guest physical address of the buffer (always==InputBuffer)
            // - InputStatus is -1
            Channel = 
                ChannelRecordSet.getChannel(VirtualChannelIndexR ? 
                                            VirtualChannelIndexR : ChannelNumber );
            if (Channel && Channel->getSendQueue() && 
                           Channel->getSendQueue()->Dequeue(InputBuffer, &InputDataLength)) {
                // Dequeued a packet successfully into the IOBuffer
                InputStatus=0;
                if (Channel->getSendQueue()->IsEmpty()) {
                    Channel->setFlags(0);
                }
                LOG_VVERBOSE(DMA, "IODMATransport::WriteWord dequeued a packet into the guest DMA buffer. ChannelV %x ChannelP %x", VirtualChannelIndexR, ChannelNumber);
            } else {
                // Else there was an error - the DMA device tried to copy in a packet
                // when none is waiting.
                InputStatus=0xffffffff;
                InputDataLength=0;
                if (Channel) 
                    Channel->setFlags(0);
                LOG_VERBOSE(DMA, "IODMATransport::WriteWord failed to dequeue a packet into the guest DMA buffer:  no packet was waitingChannelV %x ChannelP %x", VirtualChannelIndexR, ChannelNumber);
            }
            // OK, one packet has been DMA'd in.  If more packets are waiting, re-raise
            // the interrupt.
            RaiseInterrupt(ChannelNumber);
            break;
        case 0xc:
            DMAChannels[ChannelNumber].IOOutputRegister = Value;
            // This initiates DMA to copy the data packet from WinCE
            // - OutputDataLength is the size of the packet
            // - OutputDataPtr is the guest physical address of the buffer (always==OutputBuffer)
            // - OutputStatus is 0
            Channel = 
                  ChannelRecordSet.getChannel(VirtualChannelIndexW ? 
                                              VirtualChannelIndexW : ChannelNumber );
            if (!Channel || !Channel->getReceiveQueue()) {
                // If the receive queue hasn't been created yet, do so now.  The Win32 receiver may
                // not have run far enough to have create the channel itself, but we want to enequeue
                // the packet in case it does begin listening later.
                ASSERT( ChannelNumber < VIRT_CHANNEL_BASE);
                Channel = ChannelRecordSet.createChannel(ChannelNumber, DEVICE_CONNECTED);
                if (Channel == NULL) {
                    TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
                }
            }
            if (Channel->isVirtual() && !Channel->isDesktopConnected() &&
                                        !Channel->isAddrConnected())
            {
                // The channel is disconnected
                OutputStatus=0xfffffffd;
                LOG_ERROR(DMA, "IODMATransport::WriteWord failed to enqueue a packet from the guest: the channel is disconnected");
            }
            else if (Channel->getReceiveQueue()) {
                if (Channel->getReceiveQueue()->Enqueue(OutputBuffer, (USHORT)OutputDataLength, (Channel->getChannelIndex() >= VIRT_CHANNEL_BASE)))
                {
                    OutputStatus=0; // indicate success
                    SetEvent(Channel->getReceiveEvent()); // wake the thread blocked in Recieve(), if any
                    LOG_VVERBOSE(DMA, "IODMATransport::WriteWord enqueued a packet from the guest");
                    if ( Channel->getChannelState() & ADDR_CONNECTED )
                    {
                        LeaveCriticalSection(&IOLock);
                        AddressService.writeAddressChannel(Channel);
                        EnterCriticalSection(&IOLock);
                    }
                }
                else
                {
                    // The queue is full so return an error
                    OutputStatus=0xfffffffe;
                    LOG_ERROR(DMA, "IODMATransport::WriteWord failed to enqueue a packet from the guest: the queue is full");
                }
            } else {
                OutputStatus=0xffffffff; // indicate an error occurred:  the channel has been disconnected
                LOG_ERROR(DMA, "IODMATransport::WriteWord failed to enqueue a packet from the guest:  the channel has been disconnected");
            }
            break;
        case 0x10:
            DMAChannels[ChannelNumber].IRQRegister = Value;
            break;
        case 0x14:
            DMAChannels[ChannelNumber].IRQAcknowledgeRegister = Value;
            ClearInterrupt();
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
    } else if (IOAddress >= 2*BufferSizeDMA+0x80+(NumberOfChannels*sizeof(DMAChannel))&& 
               IOAddress < 2*BufferSizeDMA+0xA0+(NumberOfChannels*sizeof(DMAChannel))) {
        IOAddress -= 2*BufferSizeDMA+0x80 + (NumberOfChannels*sizeof(DMAChannel));
        DMAChannelRecord * Channel = NULL;
        ASSERT(CurrentVirtualChannel < NumberOfChannels || 
               CurrentVirtualChannel >= VIRT_CHANNEL_BASE);
        switch (IOAddress) {
        case 0:
            // This is set to on which the operation is to be performed
            CurrentVirtualChannel = Value;
            break;
        case 4:
            VirtualOperation = Value;
            switch(Value) {
            case DeviceAttachOperation:
                Channel = ChannelRecordSet.getChannel(CurrentVirtualChannel);
                if (Channel != NULL )
                {
                    if ( !Channel->isDeviceConnected() )
                    {
                        Channel->AttachChannel(CurrentVirtualChannel,DEVICE_CONNECTED);
                        VirtualStatus = 0;
                    }
                    else
                    {
                        ASSERT(false); // Device tried to connect again on a connected port
                        VirtualStatus = 1;
                    }
                }
                else
                {
                    Channel = ChannelRecordSet.createChannel(CurrentVirtualChannel, DEVICE_CONNECTED);
                    ASSERT(Channel != NULL);
                    if (!Channel)
                        TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
                    VirtualStatus = Channel != NULL ? 0 : 1;
                }
                break;
            case DeviceDetachOperation:
                Channel = ChannelRecordSet.getChannel(CurrentVirtualChannel);
                if (Channel != NULL)
                {
                    Channel->DetachChannel(DEVICE_CONNECTED);
                    VirtualStatus = 0;
                }
                else
                {
                    // ASSERT(false); // Device-side tried to disconnect a non-existant channel
                    // The above should not happen for all cases except after a soft reset. In that case the
                    // guest code will try to close channels which were already closed by the emulator 
                    // during the Reset call.
                    VirtualStatus = 1;
                }
                break;
            case IsChannelInUseOperation:
                VirtualStatus = ChannelRecordSet.isChannelInUse(CurrentVirtualChannel, DESKTOP_CONNECTED);
                break;
            case NewVirtChannelOperation:
                VirtualStatus = ChannelRecordSet.getUnusedChannelIndex(DEVICE_CONNECTED);
                break;
            case NewAddrChannelOperation:
                VirtualStatus = ChannelRecordSet.getUnusedChannelIndex(DEVICE_CONNECTED);
                // Connect the address service
                Channel = ChannelRecordSet.createChannel(VirtualStatus, ADDR_CONNECTED);
                if (!Channel)
                    TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
                break;
            default:
                TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
                break;
            }
            break;
        case 8:
            // VirtualStatus is read only
            break;
        case 0xc:
            VirtualChannelIndexF = Value;
            break;
        case 0x10:
            VirtualChannelIndexR = Value;
            break;
        case 0x14:
            VirtualChannelIndexW = Value;
            break;

        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
    } else {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

HRESULT __fastcall IODMATransport::Send(DMAChannelRecord * channel, const BYTE *dataBuffer, USHORT byteCount)
{
    HRESULT hr = E_FAIL;
    ASSERT(channel != NULL && !channel->isAddrConnected());

    if (byteCount > BufferSizeDMA || channel == NULL ) {
        return E_INVALIDARG;
    }

    // For virtual channel disallow sends on disconnected channels
    if ( channel->isVirtual() && (!channel->isDeviceConnected() || !channel->isDesktopConnected() ) )
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);

    // Notify WinCE that it needs to pick up the packet.  The Send() returns
    // without blocking for WinCE to actually pick it up.
    EnterCriticalSection(&IOLock);
    if (channel->getSendQueue()->Enqueue(dataBuffer, byteCount, channel->isVirtual()))
    {
        channel->setFlags(1);
        RaiseInterrupt(channel->getChannelIndex());
        hr = S_OK;
    }
    else
    {
        hr = HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
    }
    LeaveCriticalSection(&IOLock);

    return hr;
}

HRESULT __fastcall IODMATransport::Receive(DMAChannelRecord * channel, BYTE *dataBuffer, USHORT *byteCount, ULONG Timeout)
{
    bool Ret;
    DWORD dw;

    ASSERT(channel != NULL && !channel->isAddrConnected());
    if ( channel == NULL ) {
        return E_INVALIDARG;
    }

    if ( !channel->isDeviceConnected() && channel->isVirtual())
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);

    // Wait for a packet to arrive, or the timeout to expire
    dw = WaitForSingleObject(channel->getReceiveEvent(), Timeout);
    if (dw == WAIT_OBJECT_0) {
        // A packet has arrived - dequeue and return it
        EnterCriticalSection(&IOLock);
        unsigned __int32 Count = *byteCount;
        Ret = channel->getReceiveQueue()->Dequeue(dataBuffer, &Count);
        if (Ret) {
            *byteCount = (USHORT)Count;
        }
        if (channel->getReceiveQueue()->IsEmpty()) {
            ResetEvent(channel->getReceiveEvent());
        }
        LeaveCriticalSection(&IOLock);
        return (Ret) ? S_OK : E_FAIL;
    } else if (dw == WAIT_TIMEOUT) {
        *byteCount = 0;
        return S_OK;
    }
    return HRESULT_FROM_WIN32(GetLastError());
}

// the IOLock must be held, and the channel is 0...NumberOfChannels
void IODMATransport::RaiseInterrupt(DWORD channelIndex)
{
    ASSERT(channelIndex < NumberOfChannels || channelIndex > VIRT_CHANNEL_BASE );

    if (DMAChannels[channelIndex > VIRT_CHANNEL_BASE ? 0 : channelIndex].GlobalRegister & 0x100) {
        // Raise the interrupt, notifying WinCE that the packet is ready.
        GPIO.RaiseInterrupt(10); // EINT10 is reserved as SYSINTR_DMATRANS
    }
}

// the IOLock must be held
void IODMATransport::ClearInterrupt(void)
{
    GPIO.ClearInterrupt(10); // EINT10 is reserved as SYSINTR_DMATRANS
}


DMATransportQueue::DMATransportQueue()
{
    Head = Tail = 0;
}

void DMATransportQueue::Reset()
{
    Head = Tail = 0;
}

// The caller must hold the IOLock
bool DMATransportQueue::Enqueue(const BYTE *dataBuffer, USHORT byteCount, bool FailOnFull)
{
#if DEVICEEMULATOR_DMA_LOSS

    // This is code to stress the DMA Bootstrap and stub by forcing DMA packet loss

    static bool bInitialized;
    static int DMALossRate;
    static unsigned int DMAPacketCount;
    if (!bInitialized) {
        char *pSeed = getenv("DEVICEEMULATOR_DMA_LOSS_SEED");
        if (pSeed) {
            int i = atoi(pSeed);
            srand((unsigned int)i);
            printf("DEVICEEMULATOR_DMA_LOSS_SEED was set to %d\n", i);
        }
        char *pLossRate = getenv("DEVICEEMULATOR_DMA_LOSS_RATE");
        if (pLossRate) {
            DMALossRate = min(max(atoi(pLossRate), 0), 100); // get a value between 0 and 100
            if (DMALossRate) {
                printf("DEVICEEMULATOR_LOSSY_DMA set to %d%% - DMA will be lossy\n", DMALossRate);
            } else {
                printf("DEVICEEMULATOR_LOSSY_DMA set to 0%% - DMA will be lossless\n");
            }
        }
        bInitialized = true;
    }

    if (DMALossRate) {
        DMAPacketCount++;

        int i = rand();
        if (i < (RAND_MAX*DMALossRate)/100) {
            printf("DEVICEEMULATOR_DMA_LOSS - dropping packet number %u\n", DMAPacketCount);
            return;
        }
    }
#endif //DEVICEEMULATOR_DMA_LOSS

    if ( FailOnFull && ((Head+1) % BufferCount) == Tail )
    {
        // Queue is full so return false
        return false;
    }

    BufferSizes[Head] = min(byteCount, BufferSizeDMA);
    memcpy(&Buffers[Head*BufferSizeDMA], dataBuffer, byteCount);
    Head = (Head+1) % BufferCount;
    if (Head == Tail) {
        // The queue is full.  Discard the packet at the tail.
        Tail = (Tail+1) & BufferCount;
        LOG_INFO(DMA, "IODMATransport::DMATransportQueue::Enqueue will drop a DMA packet if none are dequeued before the next enqueue");
        ASSERT(false && "Packet dropped");
    }
    return true;
}

// the caller must hold the IOLock
bool DMATransportQueue::Dequeue(BYTE *dataBuffer, unsigned __int32 *byteCount)
{
    if (Head == Tail) {
        // The queue is empty.
        return false;
    }
    *byteCount = BufferSizes[Tail];
    memcpy(dataBuffer, &Buffers[Tail*BufferSizeDMA], min(BufferSizes[Tail], BufferSizeDMA));
    Tail = (Tail+1) % BufferCount;
    return true;
}

// the caller must hold the IOLock
bool DMATransportQueue::IsEmpty(void)
{
    return (Head == Tail);
}


HRESULT  IODMATransport::BindToDMAChannel(
    /* [in] */ ULONG dmaChannel,
    /* [out][retval] */ IDeviceEmulatorDMAChannel** ppDMAChannel)
{
    HRESULT hr;

    if (!ppDMAChannel) {
        return E_INVALIDARG;
    }
    *ppDMAChannel = NULL;

    if ((dmaChannel < FirstChannel || dmaChannel > FirstChannel+NumberOfChannels) && dmaChannel < VIRT_CHANNEL_BASE) {
        return E_INVALIDARG;
    }

    if (dmaChannel < VIRT_CHANNEL_BASE )
        dmaChannel -= FirstChannel;

    // Handle special channels
    bool AttachToAddressService = (dmaChannel == NEW_ADDR_CHANNEL);
    if (dmaChannel == NEW_VIRTUAL_CHANNEL || dmaChannel == NEW_ADDR_CHANNEL )
        dmaChannel = ChannelRecordSet.getUnusedChannelIndex(DESKTOP_CONNECTED);

    // Disallow creation of channels in the reserved range
    if (dmaChannel > VIRT_CHANNEL_BASE && dmaChannel < (VIRT_CHANNEL_BASE + VIRT_CHANNEL_RESR) ) {
        return E_INVALIDARG;
    }

    if ( ChannelRecordSet.isChannelInUse(dmaChannel, DESKTOP_CONNECTED) )
    {
        return BOOT_E_PORT_IN_USE; // the channel is already in use
    }

    DMAChannelRecord * Channel = ChannelRecordSet.createChannel(dmaChannel, DESKTOP_CONNECTED);

    if (Channel != NULL) 
    {
        // Connect to the address service if necessary
        if (AttachToAddressService)
            AddressService.connectChannel(Channel);

        *ppDMAChannel = Channel->getCEmulatorDMAChannel();
        hr = S_OK;
    }
    else 
    {
        // If channel creation failed the state of that channel is changed to UNINITIALIZED
        hr = E_OUTOFMEMORY;
    }

    return hr;
}

DMAChannelRecord::DMAChannelRecord()
{
    // Set everything to NULL
    SendQueue            = NULL;
    ReceiveQueue         = NULL;
    m_EmulatorDMAChannel = NULL;
    ReceiveEvent         = NULL;
    Flags                = 0;
    ChannelIndex         = 0;
    Record               = NULL;
    ChannelState         = UNINITIALIZED_STATE;
}

DMAChannelRecord::~DMAChannelRecord()
{
    ReleaseResources(true);
}

bool DMAChannelRecord::InitializeChannel()
{
    // Verify that we hold exclusive access to the collection 
    // Only an uninitialized channel can be initialized
    ASSERT( ChannelState == UNINITIALIZED_STATE );

    SendQueue = new DMATransportQueue;
    ReceiveQueue = new DMATransportQueue;
    m_EmulatorDMAChannel = new CEmulatorDMAChannel();	
    ReceiveEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset event, initially not signalled

    // Check for allocation failures
    if (!SendQueue || !ReceiveQueue || !m_EmulatorDMAChannel || ReceiveEvent == NULL) {
        ReleaseResources(false);
        return false;
    }

    ChannelState = BLANK_STATE; // channel is now in blank state

    return true;
}

void DMAChannelRecord::ReleaseResources(bool ShutDown)
{
    // Verify that we hold exclusive access to the collection 
    // Verify that the channel can be deleted 
    ASSERT( ChannelState == UNINITIALIZED_STATE || ChannelState == BLANK_STATE );
    // Verify that the ref counts are correct
    ASSERT( m_EmulatorDMAChannel == NULL || m_EmulatorDMAChannel->getRefCount() == 0 );

    if ( SendQueue ) 
        delete SendQueue;
    if ( ReceiveQueue )
        delete ReceiveQueue;
    if (m_EmulatorDMAChannel)
        delete m_EmulatorDMAChannel;
    if ( ReceiveEvent )
        CloseHandle(ReceiveEvent);

    // Set everything to NULL
    SendQueue            = NULL;
    ReceiveQueue         = NULL;
    m_EmulatorDMAChannel = NULL;
    ReceiveEvent         = NULL;
    Flags                = 0;
    ChannelIndex         = 0;
    Record               = NULL;
    ChannelState         = UNINITIALIZED_STATE;
}

void DMAChannelRecord::ResetChannel()
{
    // Only channels in BLANK_STATE need to be reset before use
    ASSERT( ChannelState == BLANK_STATE && 
            (m_EmulatorDMAChannel == NULL || m_EmulatorDMAChannel->getRefCount() == 0));
    if ( m_EmulatorDMAChannel == NULL )
        m_EmulatorDMAChannel = new CEmulatorDMAChannel();
    Flags                = 0;
    ChannelIndex         = 0;
    Record               = NULL;
    ResetEvent(ReceiveEvent);
    SendQueue->Reset();
    ReceiveQueue->Reset();
}

void DMAChannelRecord::AttachChannel(DWORD channelIndex, DWORD channelStatus )
{
    // Verify that the channel is in valid state 
    ASSERT( getChannelState() == BLANK_STATE || 
            (getChannelState() & DEVICE_CONNECTED)  && getChannelIndex() == channelIndex || 
            (getChannelState() & DESKTOP_CONNECTED) && getChannelIndex() == channelIndex ||
            (getChannelState() & ADDR_CONNECTED) && getChannelIndex() == channelIndex );
    ASSERT( channelStatus == DEVICE_CONNECTED || channelStatus == DESKTOP_CONNECTED ||
            channelStatus == ADDR_CONNECTED);
    
    if ( channelStatus == DESKTOP_CONNECTED )
        getCEmulatorDMAChannel()->AttachToChannelIndex(this);
    setChannelIndex(channelIndex);
    setChannelState((getChannelState() | channelStatus) );
}

void DMAChannelRecord::DetachChannel( DWORD channelStatus )
{
    // Verify that the channel is in valid state for the requested disconnect 
    // ASSERT( isDesktopConnected() && channelStatus == DESKTOP_CONNECTED ||  
    //         isDeviceConnected()  && channelStatus == DEVICE_CONNECTED );
    // The above is true for all cases except after a soft reset. In that case the
    // guest code will try to close channels which were already closed by the emulator 
    // during the Reset call.

    // Change the state
    setChannelState((getChannelState() & ~channelStatus) );

    // Disconnect address service if necessary
    if ( getChannelState() == (ADDR_CONNECTED | BLANK_STATE) )
        AddressService.disconnectChannel(this);

    if ( getChannelState() ==  BLANK_STATE )
    {
        // Delete the COM pointer if the desktop side has disconnected
        if ( channelStatus == DESKTOP_CONNECTED )
        {
            ASSERT( m_EmulatorDMAChannel && m_EmulatorDMAChannel->getRefCount() == 0 )
            delete m_EmulatorDMAChannel;
            m_EmulatorDMAChannel = NULL;
        }
        // Remove the channel from the look up table
        ChannelRecordSet.deleteChannel(getChannelIndex());
    }
}

unsigned __int32 DMAChannelRecord::getChannelIndex() 
{
    // Verify that the CEmulatorChannel exists or that we are currently in the process of deleting it
    ASSERT( getChannelState() != UNINITIALIZED_STATE && m_EmulatorDMAChannel || getChannelState() ==  BLANK_STATE);
 
    return ChannelIndex;
} 

void DMAChannelRecord::setChannelIndex(DWORD channelIndexIn) 
{
    // Verify that the CEmulatorChannel exists
    ASSERT( getChannelState() != UNINITIALIZED_STATE && m_EmulatorDMAChannel );
 
    ChannelIndex = channelIndexIn;
} 

bool DMAChannelRecord::isVirtual()
{
    // Verify that the CEmulatorChannel exists
    ASSERT( getChannelState() != UNINITIALIZED_STATE && m_EmulatorDMAChannel );

    return (ChannelIndex >= VIRT_CHANNEL_BASE );
}

DMAChannelRecordSet::DMAChannelRecordSet()
{
    ArrayList = NULL;
    UnusedDeviceChannel = VIRT_CHANNEL_BASE + VIRT_CHANNEL_RESR;
    memset(LookupTable, 0, sizeof(LookupTable));
}

DMAChannelRecordSet::~DMAChannelRecordSet()
{
}

void DMAChannelRecordSet::SaveState(StateFiler& filer) const
{
    filer.Write(UnusedDeviceChannel);
    ChannelArray * current_entry = ArrayList;

    unsigned __int32 ActiveChannelCount = 0;
    // Count the currently connected channels
    while (current_entry != NULL)
    {
        for ( int i = 0; i < CHANNEL_ARRAY_SIZE; i++ )
        {
            DMAChannelRecord * channel = &(current_entry->Channels[i]);
            if ( channel->isDeviceConnected() )
            {
                ActiveChannelCount++;
            }
        }
        current_entry = current_entry->Next;
    }
    filer.Write(ActiveChannelCount);
    // Output channel numbers 
    current_entry = ArrayList;
    while (current_entry != NULL)
    {
        for ( int i = 0; i < CHANNEL_ARRAY_SIZE; i++ )
        {
            DMAChannelRecord * channel = &(current_entry->Channels[i]);
            if ( channel->isDeviceConnected() )
            {
                filer.Write(channel->getChannelIndex());
            }
        }
        current_entry = current_entry->Next;
    }
}

void DMAChannelRecordSet::RestoreState(StateFiler& filer)
{
    // Check if the saved state contain information about the DMA channels
    if (filer.getVersion() >= MinimumStateFileVersionForDMATransport)
    {
        filer.Read(UnusedDeviceChannel);
        if ( UnusedDeviceChannel < (VIRT_CHANNEL_BASE + VIRT_CHANNEL_RESR))
        {
            filer.setStatus(false);
            return;
        }
        unsigned __int32 ActiveChannelCount = 0;
        filer.Read(ActiveChannelCount);
        for (unsigned i = 0; i < ActiveChannelCount && filer.getStatus(); i++)
        {
            unsigned __int32 ChannelIndex = 0;
            filer.Read(ChannelIndex);
            #pragma prefast(suppress:29, "Prefix gets confused here")
            if ( createChannel(ChannelIndex, DEVICE_CONNECTED ) == NULL )
            {
                filer.setStatus(false);
                return;
            }
        }
    }
}

DMAChannelRecordSet::ChannelArray * DMAChannelRecordSet::growChannelArray()
{
    ChannelArray * last_entry = ArrayList;
    ChannelArray * new_entry  = new ChannelArray(); 
    // Check if memory allocation was done
    if ( new_entry == NULL )
      return NULL;
    new_entry->Next = NULL;
    // Find the last entry
    while (last_entry != NULL && last_entry->Next != NULL )
       last_entry = last_entry->Next;

    if ( last_entry )
      last_entry->Next = new_entry;
    else
      ArrayList = new_entry;
    return new_entry;
}

// The caller must own m_BindLock before calling this routine
DMAChannelRecord *  DMAChannelRecordSet::createChannel(DWORD channelIndex, DWORD channelStatus)
{
    ASSERT(channelIndex >= 0 && channelIndex < NumberOfChannels || channelIndex >= 0x80000000 );

    DMAChannelRecord * Channel = getChannel(channelIndex);

    // Check if we have already created this channel in response to traffic
    if ( Channel )
    {
        ASSERT( channelStatus == DEVICE_CONNECTED  &&  Channel->isDesktopConnected() && !Channel->isDeviceConnected() ||
                channelStatus == DESKTOP_CONNECTED && !Channel->isDesktopConnected() &&  Channel->isDeviceConnected() );
        Channel->AttachChannel(channelIndex, channelStatus );
        return Channel;
    }

    EnterCriticalSection(&IOLock);
    Channel = getBlankChannel();
    if (!Channel )
    {
         LeaveCriticalSection(&IOLock);
         return NULL;
    }
    ASSERT( Channel->getChannelState() == BLANK_STATE && Channel->getCEmulatorDMAChannel()->getRefCount() == 0 );
    Channel->AttachChannel(channelIndex, channelStatus );
    addToLookupTable(Channel);
    LeaveCriticalSection(&IOLock);

    return Channel;
}

DMAChannelRecord * DMAChannelRecordSet::getBlankChannel()
{
    ChannelArray * current_entry = ArrayList;

    while (true)
    {
        if ( current_entry == NULL && ((current_entry = growChannelArray()) == NULL) )
            return NULL;
        for ( int i = 0; i < CHANNEL_ARRAY_SIZE; i++ )
        {
            DMAChannelRecord * channel = &(current_entry->Channels[i]);
                if ( channel->getChannelState() == UNINITIALIZED_STATE )
                {
                    if (!channel->InitializeChannel())
                        return NULL;
                    return channel;
                }
                else if ( channel->getChannelState() == BLANK_STATE )
                {
                    channel->ResetChannel();
                    return channel;
                }
       }
        current_entry = current_entry->Next;
    }
}

DWORD DMAChannelRecordSet::getUnusedChannelIndex(DWORD ChannelStatus)
{
    while ( ChannelRecordSet.isChannelInUse(UnusedDeviceChannel, ChannelStatus) )
    {
        UnusedDeviceChannel++;
        if (UnusedDeviceChannel < (VIRT_CHANNEL_BASE + VIRT_CHANNEL_RESR))
            UnusedDeviceChannel = (VIRT_CHANNEL_BASE + VIRT_CHANNEL_RESR);
    }
    DWORD retValue = UnusedDeviceChannel;
    UnusedDeviceChannel++;
    if (UnusedDeviceChannel < (VIRT_CHANNEL_BASE + VIRT_CHANNEL_RESR))
        UnusedDeviceChannel = (VIRT_CHANNEL_BASE + VIRT_CHANNEL_RESR);

    return retValue;
}

DMAChannelRecord * DMAChannelRecordSet::getChannel(DWORD channelIndex)
{
    return lookupFromTable(channelIndex);
}

void  DMAChannelRecordSet::deleteChannel(DWORD channelIndex)
{
    removeFromLookupTable(channelIndex);
}

void DMAChannelRecordSet::disconnectAll(DWORD channelStatus)
{
    ChannelArray * current_entry = ArrayList;

    // Count the currently connected channels
    while (current_entry != NULL)
    {
        for ( int i = 0; i < CHANNEL_ARRAY_SIZE; i++ )
        {
            DMAChannelRecord * channel = &(current_entry->Channels[i]);
            if ( channel->isDeviceConnected()  && channelStatus == DEVICE_CONNECTED ||
                 channel->isDesktopConnected() && channelStatus == DESKTOP_CONNECTED )
            {
                channel->DetachChannel(channelStatus);
            }
        }
        current_entry = current_entry->Next;
    }
}

bool DMAChannelRecordSet::isChannelInUse( DWORD ChannelNumber, DWORD ChannelStatus )
{
    ASSERT(ChannelNumber >= 0 && ChannelNumber < NumberOfChannels || ChannelNumber >= 0x80000000 );

    DMAChannelRecord * Channel = getChannel(ChannelNumber);

    if ( Channel == NULL )
       return false;

    return ( Channel->isDesktopConnected() && ChannelStatus == DESKTOP_CONNECTED ||
             Channel->isDeviceConnected()  && ChannelStatus == DEVICE_CONNECTED );
}

unsigned __int32 DMAChannelRecordSet::calcTableIndex(DWORD ChannelNumber)
{
    if (ChannelNumber < (VIRT_CHANNEL_BASE + VIRT_CHANNEL_RESR))
    {
        if ( ChannelNumber > CHANNEL_LOOKUP_TABLE_SIZE )
        {
            ASSERT(false && "Looking up invalid channel number in the table");
            return 0;
        }
        else 
            return ChannelNumber;
    }
    return (ChannelNumber - (VIRT_CHANNEL_BASE + VIRT_CHANNEL_RESR)) % 
           CHANNEL_LOOKUP_TABLE_SIZE;
}

bool DMAChannelRecordSet::addToLookupTable( DMAChannelRecord * channel )
{
    if (channel == NULL)
        return false;

    // Obtain the hash table index for the channel
    unsigned __int32 index = calcTableIndex(channel->getChannelIndex());
    if ( index >= CHANNEL_LOOKUP_TABLE_SIZE )
    {
        ASSERT(false && "Invalid hash table index calculated");
        return false;
    }

    TableEntry * current  = LookupTable[index];
    TableEntry * newEntry = new TableEntry();

    if ( newEntry == NULL )
        return false;

    while (current != NULL && current->next != NULL)
    {
        current = current->next;
    }

    if ( current != NULL )
        current->next = newEntry;
    else
        LookupTable[index] = newEntry;

    newEntry->channel = channel;
    return true;
}

bool DMAChannelRecordSet::removeFromLookupTable( DWORD ChannelNumber )
{
    // Obtain the hash table index for the channel
    unsigned __int32 index = calcTableIndex(ChannelNumber);
    if ( index >= CHANNEL_LOOKUP_TABLE_SIZE )
    {
        ASSERT(false && "Invalid hash table index calculated");
        return false;
    }

    TableEntry * current = LookupTable[index];

    // Check with the head of the list
    if (current && ChannelNumber == current->channel->getChannelIndex())
    {
        LookupTable[index] = current->next;
        delete current;
        return true;
    }

    // Check the body of the list
    while (current != NULL && current->next != NULL &&
           current->next->channel->getChannelIndex() != ChannelNumber)
    {
        current = current->next;
    }

    // If found in the body - relink around it.
    if (current != NULL && current->next != NULL)
    {
        TableEntry * temp = current->next;
        current->next = current->next->next;
        delete temp;
        return true;
    }

    // Did not find the entry
    return false;
}

DMAChannelRecord * DMAChannelRecordSet::lookupFromTable(DWORD ChannelNumber)
{
    unsigned __int32 index = calcTableIndex(ChannelNumber);

    if ( index >= CHANNEL_LOOKUP_TABLE_SIZE )
    {
        ASSERT(false && "Invalid hash table index calculated");
        return NULL;
    }

    TableEntry * current = LookupTable[index];

    while ( current != NULL )
    {
        if ( current->channel->getChannelIndex() == ChannelNumber )
            return current->channel;
        current = current->next;
    }
    // Did not find the entry
    return NULL;
}

unsigned __int16 AddressRecord::ReadRecord(unsigned __int8 * dataBuffer, unsigned int BufferSize, bool multipleEntries)
{
    idLength = *(unsigned __int32 *)dataBuffer;
    ASSERT( idLength <= (BufferSize - 2*sizeof(unsigned __int32)));
    if (!multipleEntries && (idLength + 2*sizeof(unsigned __int32)) != BufferSize ||
        (idLength + 2*sizeof(unsigned __int32)) > BufferSize )
    {
        ASSERT(false && "Device side send packet with invalid size to register channel");
        return 0;
    }
    channel = *(unsigned __int32 *)&dataBuffer[sizeof(unsigned __int32)+idLength];
    id = new unsigned __int8 [idLength];
    if ( id == NULL ) {
        return 0;
    }
    memcpy(id, &dataBuffer[sizeof(unsigned __int32)], idLength);
    return (unsigned __int16)(idLength + 2*sizeof(unsigned __int32));
}

unsigned __int16 AddressRecord::WriteRecord(unsigned __int8 * buffer, unsigned int BufferSize)
{
    // Check for sufficient space in the buffer
    if ( BufferSize < (2*sizeof(unsigned __int32) + idLength) )
        return (unsigned __int16)0;

    // Copy the record into the buffer
    unsigned __int16 byteCount = 0;
    *(unsigned __int32 *)&buffer[byteCount] = idLength;
    byteCount = byteCount + sizeof(unsigned __int32);
    memcpy( &buffer[byteCount], id, idLength );
    byteCount = byteCount + (unsigned __int16)idLength;
    *(unsigned __int32 *)&buffer[byteCount] = channel;
    byteCount = byteCount + sizeof(unsigned __int32);
    return byteCount;
}

DMAAddressService::DMAAddressService()
:Records(NULL)
{
    InitializeCriticalSection(&AddressListAccess);
}

DMAAddressService::~DMAAddressService()
{
    EnterCriticalSection(&AddressListAccess);
    AddressRecord * currentRecord = Records;
    Records = NULL;

    while ( currentRecord )
    {
        AddressRecord * temp = currentRecord;
        currentRecord = currentRecord->next;
        delete temp;
    }
    LeaveCriticalSection(&AddressListAccess);
    DeleteCriticalSection(&AddressListAccess);
}

void DMAAddressService::SaveState(StateFiler& filer)
{
    BYTE localBuffer[BufferSizeDMA];
    USHORT bufLength;

    do {
        bufLength = BufferSizeDMA;
        if ( FAILED(readAddressChannel(NULL, localBuffer, &bufLength)) ||
            (bufLength > BufferSizeDMA ) )
        {
            ASSERT(false && "error while saving publishing information");
            filer.setStatus(false);
            return;
        }
        // If there are published addresses write them out
        if ( bufLength > 4 )
        {
            filer.Write((unsigned __int16)(bufLength-4));
            filer.Write(localBuffer, bufLength-4);
        }
    } while (bufLength > 4 && *(DWORD *)&localBuffer[bufLength-4] != (DWORD)-1 );
    // Terminate the address stream
    filer.Write((unsigned __int16)-1);
}

void DMAAddressService::RestoreState(StateFiler& filer)
{
    // Check if the saved state contain information about the DMA channels
    if (filer.getVersion() < MinimumStateFileVersionForDMATransport) {
        return;
    }

    BYTE localBuffer[BufferSizeDMA];
    USHORT bufLength;

    do {
        filer.Read(bufLength);
        if ( bufLength != (USHORT)-1)
        {
            USHORT offset = 0;
            filer.Read(localBuffer, bufLength);
            while ( offset != bufLength )
            {
                // Allocate a record to hold the address we are about to restore
                AddressRecord * newRecord = new AddressRecord();
                if (newRecord == NULL )
                {
                    filer.setStatus(false);
                    return;
                }
                // Initalize record information by reading from the buffer
                USHORT entry_size = 
                    newRecord->ReadRecord(&localBuffer[offset], bufLength - offset, true);
                if ( entry_size == 0 )
                {
                    ASSERT(false && "Error while restoring publishing data");
                    delete newRecord;
                    filer.setStatus(false);
                    return;
                }
                // Figure link the address record to the channel record
                newRecord->channelRecord = ChannelRecordSet.getChannel(newRecord->channel);
                // Add the record to the address list
                EnterCriticalSection(&AddressListAccess);
                newRecord->next = Records;
                Records = newRecord;
                LeaveCriticalSection(&AddressListAccess);
                offset = offset + entry_size;
            }
        }
    } while (bufLength != (USHORT)-1 && filer.getStatus() );
}


void DMAAddressService::connectChannel(DMAChannelRecord * channel)
{
    // Verify that the channel is in the appropriate state
    ASSERT( !channel->isDeviceConnected() && !channel->isDesktopConnected() ||
            !channel->isDeviceConnected() &&  channel->isDesktopConnected() );
    // Update the state to indicate connection to the address service
    channel->setChannelState(channel->getChannelState() | ADDR_CONNECTED);
}

void DMAAddressService::disconnectChannel(DMAChannelRecord * channel)
{
    // The address service never disconnects first
    ASSERT( (channel->getChannelState() & ADDR_CONNECTED) &&
            !channel->isDeviceConnected() && !channel->isDesktopConnected() );
    // Update the state to indicate disconnect from the address service
    channel->setChannelState(channel->getChannelState() & ~ADDR_CONNECTED);
}

HRESULT DMAAddressService::readAddressChannel( 
    DMAChannelRecord * channel, BYTE* inDataBuffer, USHORT* inByteCount )
{
    // Verify that the channel is in the appropriate state
    ASSERT(   channel == NULL || /* used during SaveState() */
             (channel->getChannelState() & ADDR_CONNECTED) &&
             (channel->isDeviceConnected()  && !channel->isDesktopConnected() ||
             !channel->isDeviceConnected() &&  channel->isDesktopConnected()) );

    if ( channel != NULL && channel->isDesktopConnected() && 
         ( inDataBuffer == NULL || inByteCount == NULL) )
        return E_INVALIDARG;

    EnterCriticalSection(&AddressListAccess);
    AddressRecord * currRecord = channel != NULL ? channel->getCurrentRecord() : NULL;
    AddressRecord * prevRecord = NULL;
    if (currRecord == NULL )
        currRecord = Records;
    unsigned __int8 dataBuffer[BufferSizeDMA];
    unsigned __int16 byteCount = 0;
    bool PacketFull = false;
    // Fill out the DMA packet with address records
    while ( currRecord != NULL && !PacketFull )
    {
        // Test if the current record refers to disconnected address service channel 
        if ( currRecord->channelRecord == NULL ||
             (!currRecord->channelRecord->isDeviceConnected() && 
              !currRecord->channelRecord->isDesktopConnected() ) ||
              currRecord->channelRecord->getChannelIndex() != currRecord->channel ||
              currRecord->channelRecord->isAddrConnected() )
        {
            // Update the list of address records
            if (prevRecord != NULL)
                prevRecord->next = currRecord->next;
            else if ( currRecord == Records )
                Records = currRecord->next;
            else 
            {
                // We'll have to find the previos packet
                prevRecord = Records;
                while ( prevRecord != NULL && prevRecord->next != currRecord )
                    prevRecord = prevRecord->next;
                ASSERT( prevRecord != NULL );
                prevRecord->next = currRecord->next;
            }
            // Delete the record
            delete currRecord;
            // Select next record
            if ( prevRecord != NULL )
                currRecord = prevRecord->next;
            else
                currRecord = Records;
            // Check if there are no more records
            if ( currRecord == NULL )
                break;
        }

        unsigned __int16 recordSize = (unsigned __int16)(2*sizeof(unsigned __int32) + currRecord->idLength );
        if ( (unsigned __int16)(BufferSizeDMA - byteCount) > (sizeof(unsigned __int32) + recordSize))
        {
            // Copy the record into the DMA packet
            *(unsigned __int32 *)&dataBuffer[byteCount] = currRecord->idLength;
            byteCount = byteCount + sizeof(unsigned __int32);
            memcpy( &dataBuffer[byteCount], currRecord->id, currRecord->idLength );
            byteCount = byteCount + (unsigned __int16)currRecord->idLength;
            *(unsigned __int32 *)&dataBuffer[byteCount] = currRecord->channel;
            byteCount = byteCount + sizeof(unsigned __int32);
            // Move to the next record
            prevRecord = currRecord;
            currRecord = currRecord->next;
        }
        else
            PacketFull = true;
    }

    if (currRecord == NULL)
    {
        // Terminate the packet stream
        *(unsigned __int32 *)&dataBuffer[byteCount] = 0xffffffff;
    }
    else
    {
        // Terminate the packet only - the caller will expect more data
        *(unsigned __int32 *)&dataBuffer[byteCount] = 0xfffffffe;
    }
    byteCount = byteCount + sizeof(unsigned __int32);
    if ( channel != NULL )
        channel->setCurrentRecord(currRecord);
    LeaveCriticalSection(&AddressListAccess);

    if ( channel != NULL && channel->isDeviceConnected() )
    {
        return DMATransport.Send(channel, dataBuffer, byteCount );
    }
    else 
    {
        memcpy( inDataBuffer, dataBuffer, byteCount );
        *inByteCount = byteCount;
        LOG_VERBOSE(DMA, "IODMATransport::writeAddressChannel sent a packet to desktop on channel %x", (channel ? channel->getChannelIndex() : 0 ));
    }
    return S_OK;
}

HRESULT DMAAddressService::writeAddressChannel( DMAChannelRecord * channel )
{
    // Verify that the channel is in the appropriate state
    ASSERT(  (channel->getChannelState() & ADDR_CONNECTED) &&
             (channel->isDeviceConnected()  && !channel->isDesktopConnected() ||
             !channel->isDeviceConnected() &&  channel->isDesktopConnected()) );
    unsigned __int16 byteCount = 0;
    unsigned __int8 dataBuffer[BufferSizeDMA];
    // Read the packet
    EnterCriticalSection(&IOLock);
    unsigned __int32 Count = 0;
    bool Ret = channel->getReceiveQueue()->Dequeue(dataBuffer, &Count);
    byteCount = (unsigned __int16)Count;
    if (channel->getReceiveQueue()->IsEmpty()) {
        ResetEvent(channel->getReceiveEvent());
    }
    LeaveCriticalSection(&IOLock);
    // Verify that we read some reasonably sized data
    if (!Ret || byteCount < 9 )
        return E_FAIL;
    // Read the address record from the DMA packet
    unsigned __int32 inLength = *(unsigned __int32 *)dataBuffer;
    ASSERT( inLength == (byteCount - 2*sizeof(unsigned __int32)));
    if (inLength != (byteCount - 2*sizeof(unsigned __int32)))
    {
        ASSERT(false && "Device side send packet with invalid size to register channel");
        return E_INVALIDARG;
    }
    unsigned __int8 * inID = (unsigned __int8 *)&dataBuffer[sizeof(unsigned __int32)];
    unsigned __int32 inChannel = *(unsigned __int32 *)&dataBuffer[sizeof(unsigned __int32)+inLength];
    // Get the channel that is being registered
    DMAChannelRecord * channelRecord = ChannelRecordSet.getChannel( inChannel );
    if ( channelRecord == NULL)
    {
        ASSERT(false && "Device side tried to register non-existant channel");
        return E_INVALIDARG;
    }
    // Create a new address record
    AddressRecord * newRecord = new AddressRecord();
    if ( newRecord == NULL )
    {
        return E_OUTOFMEMORY;
    }
    newRecord->id = new unsigned __int8 [inLength];
    if ( newRecord == NULL )
    {
        delete newRecord;
        return E_OUTOFMEMORY;
    }
    // Fill out the record with the input info
    newRecord->idLength = inLength;
    memcpy(newRecord->id, inID, inLength);
    newRecord->channel = inChannel;
    newRecord->channelRecord = channelRecord;
    // Add the channel to the address list
    EnterCriticalSection(&AddressListAccess);
    newRecord->next = Records;
    Records = newRecord;
    // Signal an update on all address channels
    LeaveCriticalSection(&AddressListAccess);
    return S_OK;
}

void __cdecl ReleaseCOM(void)
{
    CoUninitialize();
}

HRESULT StartCOM(void)
{
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED|COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        return hr;
    }

    atexit(ReleaseCOM);
    return 0;
}

const GUID g_VMIDTable[] = {
    // A list of 64 GUIDs generated via uuidgen
{ /* 20816eb4-ea7d-4d3a-a6da-3e04ccb023d8 */
    0x20816eb4,
    0xea7d,
    0x4d3a,
    {0xa6, 0xda, 0x3e, 0x04, 0xcc, 0xb0, 0x23, 0xd8}
  },
{ /* 4ff1d471-3981-4ced-882b-92158201c9ad */
    0x4ff1d471,
    0x3981,
    0x4ced,
    {0x88, 0x2b, 0x92, 0x15, 0x82, 0x01, 0xc9, 0xad}
  },
{ /* ba5b7214-3044-4034-ad48-48c8fa5e6b8a */
    0xba5b7214,
    0x3044,
    0x4034,
    {0xad, 0x48, 0x48, 0xc8, 0xfa, 0x5e, 0x6b, 0x8a}
  },
{ /* 77efa78c-8da9-4a2d-b237-34d2f204a4a8 */
    0x77efa78c,
    0x8da9,
    0x4a2d,
    {0xb2, 0x37, 0x34, 0xd2, 0xf2, 0x04, 0xa4, 0xa8}
  },
{ /* 190ec3a0-ee55-4506-8a81-0e42fb98fa68 */
    0x190ec3a0,
    0xee55,
    0x4506,
    {0x8a, 0x81, 0x0e, 0x42, 0xfb, 0x98, 0xfa, 0x68}
  },
{ /* 5dae8ab4-f1f0-44c7-ae6a-4ec1a78eebb8 */
    0x5dae8ab4,
    0xf1f0,
    0x44c7,
    {0xae, 0x6a, 0x4e, 0xc1, 0xa7, 0x8e, 0xeb, 0xb8}
  },
{ /* 260f303b-b847-412e-9d0f-4e3db311d88b */
    0x260f303b,
    0xb847,
    0x412e,
    {0x9d, 0x0f, 0x4e, 0x3d, 0xb3, 0x11, 0xd8, 0x8b}
  },
{ /* 75830889-5c29-4070-9a08-a1cbd364d438 */
    0x75830889,
    0x5c29,
    0x4070,
    {0x9a, 0x08, 0xa1, 0xcb, 0xd3, 0x64, 0xd4, 0x38}
  },
{ /* 127dce42-1928-4a06-9d94-968241796598 */
    0x127dce42,
    0x1928,
    0x4a06,
    {0x9d, 0x94, 0x96, 0x82, 0x41, 0x79, 0x65, 0x98}
  },
{ /* 31c8298f-d8fa-47c1-af4d-3fb0648faf37 */
    0x31c8298f,
    0xd8fa,
    0x47c1,
    {0xaf, 0x4d, 0x3f, 0xb0, 0x64, 0x8f, 0xaf, 0x37}
  },
{ /* b7de5f18-a03f-4f66-982d-18fd39f4e7bd */
    0xb7de5f18,
    0xa03f,
    0x4f66,
    {0x98, 0x2d, 0x18, 0xfd, 0x39, 0xf4, 0xe7, 0xbd}
  },
{ /* d476be21-625c-4bbc-aeca-adeb35c3833b */
    0xd476be21,
    0x625c,
    0x4bbc,
    {0xae, 0xca, 0xad, 0xeb, 0x35, 0xc3, 0x83, 0x3b}
  },
{ /* 0debe925-a7bb-4920-ba83-d1d62d8a28f9 */
    0x0debe925,
    0xa7bb,
    0x4920,
    {0xba, 0x83, 0xd1, 0xd6, 0x2d, 0x8a, 0x28, 0xf9}
  },
{ /* 4fba0992-396d-4f2e-bc0d-818c4f8024e4 */
    0x4fba0992,
    0x396d,
    0x4f2e,
    {0xbc, 0x0d, 0x81, 0x8c, 0x4f, 0x80, 0x24, 0xe4}
  },
{ /* a2757f70-2a7f-403a-aeb5-7012edf29e47 */
    0xa2757f70,
    0x2a7f,
    0x403a,
    {0xae, 0xb5, 0x70, 0x12, 0xed, 0xf2, 0x9e, 0x47}
  },
{ /* 9a60e478-ccc9-4131-b39b-a022f6c26910 */
    0x9a60e478,
    0xccc9,
    0x4131,
    {0xb3, 0x9b, 0xa0, 0x22, 0xf6, 0xc2, 0x69, 0x10}
  },
{ /* 80ff0a82-b110-436c-9532-97e51451f50c */
    0x80ff0a82,
    0xb110,
    0x436c,
    {0x95, 0x32, 0x97, 0xe5, 0x14, 0x51, 0xf5, 0x0c}
  },
{ /* 3709d258-5f14-4c5c-a09e-8d28fb790d47 */
    0x3709d258,
    0x5f14,
    0x4c5c,
    {0xa0, 0x9e, 0x8d, 0x28, 0xfb, 0x79, 0x0d, 0x47}
  },
{ /* d5593486-82b8-403d-84c0-120faab383d5 */
    0xd5593486,
    0x82b8,
    0x403d,
    {0x84, 0xc0, 0x12, 0x0f, 0xaa, 0xb3, 0x83, 0xd5}
  },
{ /* d81c1b34-c1d8-4c73-955a-1300f09256fe */
    0xd81c1b34,
    0xc1d8,
    0x4c73,
    {0x95, 0x5a, 0x13, 0x00, 0xf0, 0x92, 0x56, 0xfe}
  },
{ /* e890c7b0-267e-410a-b3b2-7bd9840286e8 */
    0xe890c7b0,
    0x267e,
    0x410a,
    {0xb3, 0xb2, 0x7b, 0xd9, 0x84, 0x02, 0x86, 0xe8}
  },
{ /* a655b129-61d7-4bc1-b11e-bb21ca0cb45d */
    0xa655b129,
    0x61d7,
    0x4bc1,
    {0xb1, 0x1e, 0xbb, 0x21, 0xca, 0x0c, 0xb4, 0x5d}
  },
{ /* ef4439e6-775b-4ce9-bd96-c2fb92fa2a3b */
    0xef4439e6,
    0x775b,
    0x4ce9,
    {0xbd, 0x96, 0xc2, 0xfb, 0x92, 0xfa, 0x2a, 0x3b}
  },
{ /* ce113169-6327-4867-88f8-066b4a6015fb */
    0xce113169,
    0x6327,
    0x4867,
    {0x88, 0xf8, 0x06, 0x6b, 0x4a, 0x60, 0x15, 0xfb}
  },
{ /* 3dd19565-dc6b-403e-b6ff-c6061c85ff19 */
    0x3dd19565,
    0xdc6b,
    0x403e,
    {0xb6, 0xff, 0xc6, 0x06, 0x1c, 0x85, 0xff, 0x19}
  },
{ /* 0f040e3a-97dd-4f5a-abc3-dfc294db6f1b */
    0x0f040e3a,
    0x97dd,
    0x4f5a,
    {0xab, 0xc3, 0xdf, 0xc2, 0x94, 0xdb, 0x6f, 0x1b}
  },
{ /* 8aa0d926-d616-4a8d-bc71-2a0b17210d30 */
    0x8aa0d926,
    0xd616,
    0x4a8d,
    {0xbc, 0x71, 0x2a, 0x0b, 0x17, 0x21, 0x0d, 0x30}
  },
{ /* 5e86123d-8d94-4a5b-b917-2f93ccbb77f1 */
    0x5e86123d,
    0x8d94,
    0x4a5b,
    {0xb9, 0x17, 0x2f, 0x93, 0xcc, 0xbb, 0x77, 0xf1}
  },
{ /* 3ddfe614-549f-4bc5-8deb-e9e67cd0b50b */
    0x3ddfe614,
    0x549f,
    0x4bc5,
    {0x8d, 0xeb, 0xe9, 0xe6, 0x7c, 0xd0, 0xb5, 0x0b}
  },
{ /* 5a74608a-1e2a-435d-9191-0007aa4cfdc5 */
    0x5a74608a,
    0x1e2a,
    0x435d,
    {0x91, 0x91, 0x00, 0x07, 0xaa, 0x4c, 0xfd, 0xc5}
  },
{ /* 8d9053db-d1af-4682-ab0b-c19002d6258e */
    0x8d9053db,
    0xd1af,
    0x4682,
    {0xab, 0x0b, 0xc1, 0x90, 0x02, 0xd6, 0x25, 0x8e}
  },
{ /* 2d29efef-bb48-4da9-9862-d4400f958ec0 */
    0x2d29efef,
    0xbb48,
    0x4da9,
    {0x98, 0x62, 0xd4, 0x40, 0x0f, 0x95, 0x8e, 0xc0}
  },
{ /* 6022fe92-6b5c-432e-98f4-51b29901a292 */
    0x6022fe92,
    0x6b5c,
    0x432e,
    {0x98, 0xf4, 0x51, 0xb2, 0x99, 0x01, 0xa2, 0x92}
  },
{ /* a4448dea-acfb-479a-b96f-8d3b353156f4 */
    0xa4448dea,
    0xacfb,
    0x479a,
    {0xb9, 0x6f, 0x8d, 0x3b, 0x35, 0x31, 0x56, 0xf4}
  },
{ /* 620ea4bb-a174-4244-920a-724ebdc38dc0 */
    0x620ea4bb,
    0xa174,
    0x4244,
    {0x92, 0x0a, 0x72, 0x4e, 0xbd, 0xc3, 0x8d, 0xc0}
  },
{ /* d97a6746-847f-4256-9eae-4e8cf645f96d */
    0xd97a6746,
    0x847f,
    0x4256,
    {0x9e, 0xae, 0x4e, 0x8c, 0xf6, 0x45, 0xf9, 0x6d}
  },
{ /* 7a221395-ba43-4d92-96a8-fc95c1c944e4 */
    0x7a221395,
    0xba43,
    0x4d92,
    {0x96, 0xa8, 0xfc, 0x95, 0xc1, 0xc9, 0x44, 0xe4}
  },
{ /* e6041f64-5598-41e0-ab67-3ef42fe5b4c1 */
    0xe6041f64,
    0x5598,
    0x41e0,
    {0xab, 0x67, 0x3e, 0xf4, 0x2f, 0xe5, 0xb4, 0xc1}
  },
{ /* ee4704b9-b516-40da-b8f0-ec55b524f482 */
    0xee4704b9,
    0xb516,
    0x40da,
    {0xb8, 0xf0, 0xec, 0x55, 0xb5, 0x24, 0xf4, 0x82}
  },
{ /* 8fbb9f66-8652-41b3-b994-593071955784 */
    0x8fbb9f66,
    0x8652,
    0x41b3,
    {0xb9, 0x94, 0x59, 0x30, 0x71, 0x95, 0x57, 0x84}
  },
{ /* 2e21d9b9-b6db-4e2e-a3e3-784dbf1b35cd */
    0x2e21d9b9,
    0xb6db,
    0x4e2e,
    {0xa3, 0xe3, 0x78, 0x4d, 0xbf, 0x1b, 0x35, 0xcd}
  },
{ /* 064483f3-a5b5-440e-8247-dadb3ecdc0ac */
    0x064483f3,
    0xa5b5,
    0x440e,
    {0x82, 0x47, 0xda, 0xdb, 0x3e, 0xcd, 0xc0, 0xac}
  },
{ /* 18b535ae-ed33-4f40-847c-5ce4447e9d98 */
    0x18b535ae,
    0xed33,
    0x4f40,
    {0x84, 0x7c, 0x5c, 0xe4, 0x44, 0x7e, 0x9d, 0x98}
  },
{ /* 255d2db2-69ab-436e-85ce-cba079cffe2e */
    0x255d2db2,
    0x69ab,
    0x436e,
    {0x85, 0xce, 0xcb, 0xa0, 0x79, 0xcf, 0xfe, 0x2e}
  },
{ /* 1866b92b-29cd-4944-a23e-9a6436e35337 */
    0x1866b92b,
    0x29cd,
    0x4944,
    {0xa2, 0x3e, 0x9a, 0x64, 0x36, 0xe3, 0x53, 0x37}
  },
{ /* 7fe58db3-f5e5-4fb9-a8d5-25912835b5fa */
    0x7fe58db3,
    0xf5e5,
    0x4fb9,
    {0xa8, 0xd5, 0x25, 0x91, 0x28, 0x35, 0xb5, 0xfa}
  },
{ /* fb32ae62-a66e-49b0-af5a-111e60bf4881 */
    0xfb32ae62,
    0xa66e,
    0x49b0,
    {0xaf, 0x5a, 0x11, 0x1e, 0x60, 0xbf, 0x48, 0x81}
  },
{ /* 223c4f22-f0cc-4dfe-b0a8-4e37d3f306df */
    0x223c4f22,
    0xf0cc,
    0x4dfe,
    {0xb0, 0xa8, 0x4e, 0x37, 0xd3, 0xf3, 0x06, 0xdf}
  },
{ /* 88455ff2-f95a-4eb4-b78e-3bc50ca899f4 */
    0x88455ff2,
    0xf95a,
    0x4eb4,
    {0xb7, 0x8e, 0x3b, 0xc5, 0x0c, 0xa8, 0x99, 0xf4}
  },
{ /* b6e99a04-d633-440f-93d3-a5f3a0693be0 */
    0xb6e99a04,
    0xd633,
    0x440f,
    {0x93, 0xd3, 0xa5, 0xf3, 0xa0, 0x69, 0x3b, 0xe0}
  },
{ /* 6e705f00-b39b-4688-baea-b9b58d5770b9 */
    0x6e705f00,
    0xb39b,
    0x4688,
    {0xba, 0xea, 0xb9, 0xb5, 0x8d, 0x57, 0x70, 0xb9}
  },
{ /* 8fe6aa3a-f654-41d9-bde0-29cb193fb339 */
    0x8fe6aa3a,
    0xf654,
    0x41d9,
    {0xbd, 0xe0, 0x29, 0xcb, 0x19, 0x3f, 0xb3, 0x39}
  },
{ /* 67202d9c-c95a-4219-a661-1676f2dff079 */
    0x67202d9c,
    0xc95a,
    0x4219,
    {0xa6, 0x61, 0x16, 0x76, 0xf2, 0xdf, 0xf0, 0x79}
  },
{ /* a9863164-2fc5-470c-bb07-d57f63d216bd */
    0xa9863164,
    0x2fc5,
    0x470c,
    {0xbb, 0x07, 0xd5, 0x7f, 0x63, 0xd2, 0x16, 0xbd}
  },
{ /* 7a7b002f-ffc8-4946-a6d2-8cff128efd37 */
    0x7a7b002f,
    0xffc8,
    0x4946,
    {0xa6, 0xd2, 0x8c, 0xff, 0x12, 0x8e, 0xfd, 0x37}
  },
{ /* 737cfbcb-a693-49f4-98c3-a92c895b5c37 */
    0x737cfbcb,
    0xa693,
    0x49f4,
    {0x98, 0xc3, 0xa9, 0x2c, 0x89, 0x5b, 0x5c, 0x37}
  },
{ /* fe06a5d9-9e82-434a-a855-39f5de694cef */
    0xfe06a5d9,
    0x9e82,
    0x434a,
    {0xa8, 0x55, 0x39, 0xf5, 0xde, 0x69, 0x4c, 0xef}
  },
{ /* 925ee5eb-9640-4749-afbf-f23cd30cdffc */
    0x925ee5eb,
    0x9640,
    0x4749,
    {0xaf, 0xbf, 0xf2, 0x3c, 0xd3, 0x0c, 0xdf, 0xfc}
  },
{ /* 780f6469-f2da-4c47-bae4-dc92b940cf5c */
    0x780f6469,
    0xf2da,
    0x4c47,
    {0xba, 0xe4, 0xdc, 0x92, 0xb9, 0x40, 0xcf, 0x5c}
  },
{ /* 3a18f0b6-123a-4422-8c38-c36dcd2dc328 */
    0x3a18f0b6,
    0x123a,
    0x4422,
    {0x8c, 0x38, 0xc3, 0x6d, 0xcd, 0x2d, 0xc3, 0x28}
  },
{ /* 17fb1a08-46e5-4223-9783-050c0d617aca */
    0x17fb1a08,
    0x46e5,
    0x4223,
    {0x97, 0x83, 0x05, 0x0c, 0x0d, 0x61, 0x7a, 0xca}
  },
{ /* d8a7fe0e-c415-4a78-9855-d4e2ef3055e7 */
    0xd8a7fe0e,
    0xc415,
    0x4a78,
    {0x98, 0x55, 0xd4, 0xe2, 0xef, 0x30, 0x55, 0xe7}
  },
{ /* 33002cc3-2845-4312-8e9e-49d7e2b441a5 */
    0x33002cc3,
    0x2845,
    0x4312,
    {0x8e, 0x9e, 0x49, 0xd7, 0xe2, 0xb4, 0x41, 0xa5}
  },
{ /* e1babff5-8ed5-4379-addd-fb36b6afbb2c */
    0xe1babff5,
    0x8ed5,
    0x4379,
    {0xad, 0xdd, 0xfb, 0x36, 0xb6, 0xaf, 0xbb, 0x2c}
  }
};


// This is at atexit callback, called whenever the emulator calls exit(),
// which is most likely in CpuSimulate() if/when the user closes the
// LCD window and save-state has completed.
void __cdecl UnregisterVM(void)
{
    HRESULT hr;
    IRunningObjectTable *pROT;

    // Check if we have already been unregistered from ROT
    if ( Configuration.RegisteredInROT == false )
        return;

    hr = GetRunningObjectTable(0, &pROT);
    if (FAILED(hr)) {
        return;
    }
    /* hr = */ pROT->Revoke(g_dwRegister);
    pROT->Release();
}

HRESULT RegisterVMinROT(bool RegisterInROT)
{
    HRESULT hr;
    IRunningObjectTable *pROT;
    IMoniker *pMoniker;
    WCHAR VMIDString[40]; // large enough for a GUID
    WCHAR ItemName[_MAX_PATH];

    // We should not be currently registered
    ASSERT(Configuration.RegisteredInROT == false );

    hr = GetRunningObjectTable(0, &pROT);
    if (FAILED(hr)) {
        return hr;
    }
    if (StringFromGUID2(Configuration.VMID, VMIDString, ARRAY_SIZE(VMIDString)) == 0) {
        ASSERT(FALSE);  // string ought to fit
        pROT->Release();
        return E_FAIL;
    }

    // Note: if you change this string format, you must also change
    //       the implementation of EnumerateLaunchedVMIDs() to match.
    hr = StringCchPrintfW(ItemName, ARRAY_SIZE(ItemName), L"%s %s", EMULATOR_NAME_W, VMIDString);
    if (FAILED(hr)) {
        pROT->Release();
        return hr;
    }
    hr = CreateItemMoniker(L"!", ItemName, &pMoniker);
    if (FAILED(hr)) {
        pROT->Release();
        return hr;
    }
    if ( RegisterInROT )
    {
        // Add the item with VMID to the ROT
        hr = pROT->Register(ROTFLAGS_REGISTRATIONKEEPSALIVE, &g_EmulatorItem, pMoniker, &g_dwRegister);
        if (FAILED(hr)) {
            pMoniker->Release();
            pROT->Release();
            return hr;
        }
        Configuration.RegisteredInROT = true;
        pROT->Release();
        atexit(UnregisterVM);
    }
    else
    {
        // Check that the VMID is not already in the ROT
        IUnknown *pUnk;
        hr = pROT->GetObject(pMoniker, &pUnk);
        pROT->Release();
        pMoniker->Release();
        if (FAILED(hr)) {
            return S_OK;
        }
        pUnk->Release();
        return E_FAIL;
    }

    return S_OK;
};

HRESULT RegisterVMIDAsRunningHelper(bool RegisterInROT)
{
    WCHAR Buffer[256];

    if (!CreateVMIDObjectName(Buffer, ARRAY_SIZE(Buffer))) {
        return E_FAIL;
    }

    g_hVMIDObjectEvent = CreateEvent(NULL, FALSE, TRUE, Buffer);
    if (g_hVMIDObjectEvent == NULL) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Whoops!  The VMIDObjectName already exists.  That
        // means that another DeviceEmulator instance within
        // this TS session is already emulating it.
        CloseHandle(g_hVMIDObjectEvent);
        LOG_WARN(DMA, "RegisterVMIDAsRunningHelper failed - another VMID owns the named event");
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }

    if (!Configuration.RegisteredInROT) {
        HRESULT hr;

        hr = RegisterVMinROT(RegisterInROT);

        if (FAILED(hr)) {
            CloseHandle(g_hVMIDObjectEvent);
            LOG_ERROR(DMA, "RegisterVMIDAsRunningHelper failed RegisterVM hr=%x", hr);
            return hr;
        }
    }

    return S_OK;
}

bool RegisterVMIDAsRunning(bool RegisterInROT)
{
    if (Configuration.VMID != GUID_NULL) {
        // A VMID has been passed on the command-line.  Register it as running, or fail if
        // another DeviceEmulator instance is already running it.
        LOG_INFO(DMA, "RegisterVMIDAsRunning using configured VMID %!GUID!", &Configuration.VMID);
        return !FAILED(RegisterVMIDAsRunningHelper(RegisterInROT));
    }

    // Else no VMID was passed on the command line.  Normally we could just
    // call COM and generate a new VMID GUID.  However, in order to reduce
    // the load on corpnet DHCP servers, we cache MAC address and VMID pairs
    // in the registry.  If each new DeviceEmulator instance allocated a new
    // VMID GUID, then each new instance would allocate a new MAC address.
    // By using a table of VMID GUIDs, the DeviceEmulator is constrained a
    // little, to use the least number of MAC addresses needed to support
    // the number of *running* DeviceEmulator instances within the current
    // TS session.
    int i;

    for (i=0; i<ARRAY_SIZE(g_VMIDTable); ++i) {
        Configuration.VMID = g_VMIDTable[i];
        LOG_INFO(DMA, "RegisterVMIDAsRunning using table-based VMID %!GUID!", &Configuration.VMID);
        if (!FAILED(RegisterVMIDAsRunningHelper(RegisterInROT))) {
            return true;
        }
    }
    // Else more than ARRAY_SIZE(g_VMIDTable) DeviceEmulator instances
    // running concurrently.  Allocate a new random GUID and don't
    // worry about MAC address caching.
    if (FAILED(CoCreateGuid(&Configuration.VMID))) {
        return false;
    }
    LOG_INFO(DMA, "RegisterVMIDAsRunning using generated VMID %!GUID!", &Configuration.VMID);
    return !FAILED(RegisterVMIDAsRunningHelper(RegisterInROT));
}
