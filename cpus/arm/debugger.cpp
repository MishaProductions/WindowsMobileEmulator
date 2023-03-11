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
#include <memory>
#include <new>
#pragma warning(default:4995)
#include "State.h"
#include "Board.h"
#include "syscall.h"
#include "emdebug.h"
#include "CPU.h"
#include "entrypt.h"
#include "ARMCpu.h"
#include "tc.h"
#include "mmu.h"


#ifndef InitializeListHead
//
//  VOID
//  InitializeListHead(
//      PLIST_ENTRY ListHead
//      );
//

#define InitializeListHead(ListHead) (\
    (ListHead)->Flink = (ListHead)->Blink = (ListHead))

//
//  BOOLEAN
//  IsListEmpty(
//      PLIST_ENTRY ListHead
//      );
//

#define IsListEmpty(ListHead) \
    ((ListHead)->Flink == (ListHead))

//
//  PLIST_ENTRY
//  RemoveHeadList(
//      PLIST_ENTRY ListHead
//      );
//

#define RemoveHeadList(ListHead) \
    (ListHead)->Flink;\
    {RemoveEntryList((ListHead)->Flink)}

//
//  PLIST_ENTRY
//  RemoveTailList(
//      PLIST_ENTRY ListHead
//      );
//

#define RemoveTailList(ListHead) \
    (ListHead)->Blink;\
    {RemoveEntryList((ListHead)->Blink)}

//
//  VOID
//  RemoveEntryList(
//      PLIST_ENTRY Entry
//      );
//

#define RemoveEntryList(Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_Flink;\
    _EX_Flink = (Entry)->Flink;\
    _EX_Blink = (Entry)->Blink;\
    _EX_Blink->Flink = _EX_Flink;\
    _EX_Flink->Blink = _EX_Blink;\
    }

//
//  VOID
//  InsertTailList(
//      PLIST_ENTRY ListHead,
//      PLIST_ENTRY Entry
//      );
//

#define InsertTailList(ListHead,Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_ListHead;\
    _EX_ListHead = (ListHead);\
    _EX_Blink = _EX_ListHead->Blink;\
    (Entry)->Flink = _EX_ListHead;\
    (Entry)->Blink = _EX_Blink;\
    _EX_Blink->Flink = (Entry);\
    _EX_ListHead->Blink = (Entry);\
    }

//
//  VOID
//  InsertHeadList(
//      PLIST_ENTRY ListHead,
//      PLIST_ENTRY Entry
//      );
//

#define InsertHeadList(ListHead,Entry) {\
    PLIST_ENTRY _EX_Flink;\
    PLIST_ENTRY _EX_ListHead;\
    _EX_ListHead = (ListHead);\
    _EX_Flink = _EX_ListHead->Flink;\
    (Entry)->Flink = _EX_Flink;\
    (Entry)->Blink = _EX_ListHead;\
    _EX_Flink->Blink = (Entry);\
    _EX_ListHead->Flink = (Entry);\
    }



BOOL IsNodeOnList(PLIST_ENTRY ListHead, PLIST_ENTRY Entry);


#endif //InitializeListHead

// This is directly pasted from exdi.idl
typedef
    struct _CONTEXT_ARM3
    {
        struct 
        {
            BOOL fControlRegs;
            BOOL fIntegerRegs;
            BOOL fFloatingPointRegs;
            BOOL fConcanRegs;
        }       RegGroupSelection;  // These flags are used to select groups of registers only 
                                    // (instead of the totality) for reading or writing.
        // Control registers (used if RegGroupSelection.fControlRegs is TRUE).
        DWORD   Sp;
        DWORD   Lr;
        DWORD   Pc;
        DWORD   Psr;
        // Integer registers (used if RegGroupSelection.fIntegerRegs is TRUE).
        DWORD   R0;
        DWORD   R1;
        DWORD   R2;
        DWORD   R3;
        DWORD   R4;
        DWORD   R5;
        DWORD   R6;
        DWORD   R7;
        DWORD   R8;
        DWORD   R9;
        DWORD   R10;
        DWORD   R11;
        DWORD   R12;
        // Floating point registers (used if RegGroupSelection.fFloatingPointRegs is TRUE).
        DWORD   Fpscr;        // floating point status register
        DWORD   FpExc;        // floating point exception register
        DWORD   S0;
        DWORD   S1;
        DWORD   S2;
        DWORD   S3;
        DWORD   S4;
        DWORD   S5;
        DWORD   S6;
        DWORD   S7;
        DWORD   S8;
        DWORD   S9;
        DWORD   S10;
        DWORD   S11;
        DWORD   S12;
        DWORD   S13;
        DWORD   S14;
        DWORD   S15;
        DWORD   S16;
        DWORD   S17;
        DWORD   S18;
        DWORD   S19;
        DWORD   S20;
        DWORD   S21;
        DWORD   S22;
        DWORD   S23;
        DWORD   S24;
        DWORD   S25;
        DWORD   S26;
        DWORD   S27;
        DWORD   S28;
        DWORD   S29;
        DWORD   S30;
        DWORD   S31;
        DWORD   S32;          // Additional ULONG required for fstmx/fldmx instructions (VFP5TE spec)
        DWORD   FpExtra0;     // FP Extra control registers
        DWORD   FpExtra1;
        DWORD   FpExtra2;
        DWORD   FpExtra3;
        DWORD   FpExtra4;
        DWORD   FpExtra5;
        DWORD   FpExtra6;
        DWORD   FpExtra7;

        // 64-bit registers
        DWORD64 Wr0;
        DWORD64 Wr1;
        DWORD64 Wr2;
        DWORD64 Wr3;
        DWORD64 Wr4;
        DWORD64 Wr5;
        DWORD64 Wr6;
        DWORD64 Wr7;
        DWORD64 Wr8;
        DWORD64 Wr9;
        DWORD64 Wr10;
        DWORD64 Wr11;
        DWORD64 Wr12;
        DWORD64 Wr13;
        DWORD64 Wr14;
        DWORD64 Wr15;

        // Control registers
        ULONG WCID;
        ULONG WCon;
        ULONG WCSSF;
        ULONG WCASF;
        ULONG Reserved4;
        ULONG Reserved5;
        ULONG Reserved6;
        ULONG Reserved7;
        ULONG WCGR0;
        ULONG WCGR1;
        ULONG WCGR2;
        ULONG WCGR3;
        ULONG Reserved12;
        ULONG Reserved13;
        ULONG Reserved14;
        ULONG Reserved15;
        
    } CONTEXT_ARM3, *PCONTEXT_ARM3;

typedef struct {
    LIST_ENTRY Entry;               // This must be the first field in the structure
    unsigned __int32 GuestAddress;  // Breakpoint address
    bool fIsVirtual;                // True if virtual, false if physical
    DWORD dwBypassCount;            // Count that the BP should hit before actually breaking (-1 = no count)
    DWORD dwBypassOccurrences;      // Count of times that the BP has actually been hit
} DEBUGGER_BREAKPOINT;

class CEmulatorDebugger : public IDeviceEmulatorDebugger
{
public:
    // IUnknown methods:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID	inInterfaceID,void** outInterface);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();

    // IDeviceEmulatorDebugger
    virtual HRESULT STDMETHODCALLTYPE GetProcessorFamily( 
        /* [out] */ DWORD *pdwProcessorFamily);
    
    virtual HRESULT STDMETHODCALLTYPE ContinueExecution( void);
    
    virtual HRESULT STDMETHODCALLTYPE ContinueWithSingleStep( 
        DWORD dwNumberOfSteps, /* [in] */ DWORD dwCpuNum);
    
    virtual HRESULT STDMETHODCALLTYPE Halt( void);
    
    virtual HRESULT STDMETHODCALLTYPE RegisterHaltNotification( 
        /* [in] */ IDeviceEmulatorDebuggerHaltNotificationSink *pSink,
        /* [out] */ DWORD *pdwNotificationCookie);
    
    virtual HRESULT STDMETHODCALLTYPE UnregisterHaltNotification( 
        /* [in] */ DWORD dwNotificationCookie);
    
    virtual HRESULT STDMETHODCALLTYPE ReadVirtualMemory( 
        /* [in] */ DWORD64 Address,
        /* [in] */ DWORD NumBytesToRead,
        /* [in] */ DWORD dwCpuNum,
        /* [size_is][out] */ BYTE *pbReadBuffer,
        /* [out] */ DWORD *pNumBytesActuallyRead);
    
    virtual HRESULT STDMETHODCALLTYPE WriteVirtualMemory( 
        /* [in] */ DWORD64 Address,
        /* [in] */ DWORD NumBytesToWrite,
        /* [in] */ DWORD dwCpuNum,
        /* [size_is][in] */ const BYTE *pbWriteBuffer,
        /* [out] */ DWORD *pNumBytesActuallyWritten);
    
    virtual HRESULT STDMETHODCALLTYPE ReadPhysicalMemory( 
        /* [in] */ DWORD64 Address,
        /* [in] */ boolean fUseIOSpace,
        /* [in] */ DWORD NumBytesToRead,
        /* [in] */ DWORD dwCpuNum,
        /* [size_is][out] */ BYTE *pbReadBuffer,
        /* [out] */ DWORD *pNumBytesActuallyRead);
    
    virtual HRESULT STDMETHODCALLTYPE WritePhysicalMemory( 
        /* [in] */ DWORD64 Address,
        /* [in] */ boolean fUseIOSpace,
        /* [in] */ DWORD NumBytesToWrite,
        /* [in] */ DWORD dwCpuNum,
        /* [size_is][in] */ const BYTE *pbWriteBuffer,
        /* [out] */ DWORD *pNumBytesActuallyWritten);
    
    virtual HRESULT STDMETHODCALLTYPE AddCodeBreakpoint( 
        /* [in] */ DWORD64 Address,
        /* [in] */ boolean fIsVirtual,
        /* [in] */ DWORD dwBypassCount,
        /* [out] */ DWORD *pdwBreakpointCookie);
    
    virtual HRESULT STDMETHODCALLTYPE SetBreakpointState( 
        /* [in] */ DWORD dwBreakpointCookie,
        /* [in] */ boolean fResetBypassCount);

    virtual HRESULT STDMETHODCALLTYPE GetBreakpointState(
        /* [in] */ DWORD dwBreakpointCookie,
        /* [out] */ DWORD *pdwBypassedOccurrences);
    
    virtual HRESULT STDMETHODCALLTYPE DeleteBreakpoint( 
        /* [in] */ DWORD dwBreakpointCookie);
    
    virtual HRESULT STDMETHODCALLTYPE GetContext( 
        /* [size_is][out][in] */ BYTE *pbContext,
        /* [in] */ DWORD dwContextSize,
        /* [in] */ DWORD dwCpuNum);
    
    virtual HRESULT STDMETHODCALLTYPE SetContext( 
        /* [size_is][out][in] */ BYTE *pbContext,
        /* [in] */ DWORD dwContextSize,
        /* [in] */ DWORD dwCpuNum);

    // Other methods
    CEmulatorDebugger();
    ~CEmulatorDebugger();
    PVOID DebugEntry(unsigned __int32 InstructionPointer, EARMExceptionType etReason);
    PVOID SingleStepEntry(unsigned __int32 InstructionPointer);
    bool DebugHardwareBreakpointPresent(unsigned __int32 InstructionPointer);

private:
    PVOID NotifyDebugger(unsigned __int32 InstructionPointer, DEVICEEMULATOR_HALT_REASON_TYPE HaltReason, DWORD dwCode);

    ULONG m_RefCount;
    IDeviceEmulatorDebuggerHaltNotificationSink *m_pHaltNotification;
    CRITICAL_SECTION m_Lock;
    unsigned __int32 m_SingleStepCount;
    LIST_ENTRY m_BreakpointList;       // List of DEBUGGER_BREAKPOINT structures
    bool m_fTranslationCacheFlushed;
};


CEmulatorDebugger *g_pDebugger;

CEmulatorDebugger::CEmulatorDebugger() :
    m_RefCount(2), // initial refcount is 2 - once this object is created, it is never destroyed, as
                   // the CPU assumes that if the g_pDebugger is non-NULL, that it always remains that way.
    m_pHaltNotification(NULL),
    m_SingleStepCount(0),
    m_fTranslationCacheFlushed(false)
{
    InitializeCriticalSection(&m_Lock);
    InitializeListHead(&m_BreakpointList);
}

CEmulatorDebugger::~CEmulatorDebugger()
{
    DeleteCriticalSection(&m_Lock);
    g_pDebugger = NULL;
    if (g_fOptimizeCode == false) {
        g_fOptimizeCode = true; // re-enable optimizations
        FlushTranslationCache(0, 0xffffffff); // flush any unoptimized code
        m_fTranslationCacheFlushed = true;
    }
    SetEvent(g_hDebuggerEvent);
}

HRESULT STDMETHODCALLTYPE CEmulatorDebugger::QueryInterface(REFIID inInterfaceID,void** outInterface)
{
    if (inInterfaceID == IID_IUnknown || inInterfaceID == IID_IDeviceEmulatorDebugger) {
        AddRef();
        *outInterface = (void*)this;
        return S_OK;
    }

    *outInterface = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CEmulatorDebugger::AddRef()
{
    return InterlockedIncrement((LONG*)&m_RefCount);
}

ULONG STDMETHODCALLTYPE CEmulatorDebugger::Release()
{
    ULONG Ret = InterlockedDecrement((LONG*)&m_RefCount);
    if (Ret == 0) {
        ASSERT(FALSE); // see the m_RefCount initialization in the class constructor for an explanation
    } else if (Ret == 1) {
        // The last ref from PB has gone away.  Resume full-speed execution.
        if (g_fOptimizeCode == false) {
            g_fOptimizeCode = true; // re-enable optimizations
            FlushTranslationCache(0, 0xffffffff); // flush any unoptimized code
            m_fTranslationCacheFlushed = true;
        }
        SetEvent(g_hDebuggerEvent);
    }
    return Ret;
}


HRESULT STDMETHODCALLTYPE CEmulatorDebugger::GetProcessorFamily( 
        /* [out] */ DWORD *pdwProcessorFamily)
{
    *pdwProcessorFamily = 4;
    return S_OK;
}
    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::ContinueExecution(void)
{
    if (g_fOptimizeCode == false) {
        g_fOptimizeCode = true; // re-enable optimizations
        FlushTranslationCache(0, 0xffffffff); // flush any unoptimized code
        m_fTranslationCacheFlushed = true;
    }
    SetEvent(g_hDebuggerEvent);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorDebugger::ContinueWithSingleStep( 
        DWORD dwNumberOfSteps, DWORD dwCpuNum)
{
    if ( dwCpuNum != 0) {
        return E_INVALIDARG;
    }
    
    EnterCriticalSection(&m_Lock);
    m_SingleStepCount = dwNumberOfSteps;
    LeaveCriticalSection(&m_Lock);

    EnterCriticalSection(&InterruptLock);
    UpdateInterruptOnPoll(); // cause jitted code to poll the Cpu.DebuggerInterruptPending value and call DebugEntry
    LeaveCriticalSection(&InterruptLock);

    if (g_fOptimizeCode) {
        g_fOptimizeCode = false; // tell the CPU to generate unoptimized code
        FlushTranslationCache(0, 0xffffffff);   // flush any existing optimized code
        m_fTranslationCacheFlushed = true;
    }

    SetEvent(g_hDebuggerEvent);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorDebugger::Halt(void)
{
    EnterCriticalSection(&InterruptLock);
    Cpu.DebuggerInterruptPending |= DEBUGGER_INTERRUPT_BREAKIN;
    UpdateInterruptOnPoll(); // cause jitted code to poll the Cpu.DebuggerInterruptPending value and call DebugEntry
    LeaveCriticalSection(&InterruptLock);
    return S_OK;
}
    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::RegisterHaltNotification( 
        /* [in] */ IDeviceEmulatorDebuggerHaltNotificationSink *pSink,
        /* [out] */ DWORD *pdwNotificationCookie)
{
    EnterCriticalSection(&m_Lock);
    if (m_pHaltNotification) {
        // Last writer wins.  This allows PB to reconnect after a PB-side failure.
        m_pHaltNotification->Release();
    }
    m_pHaltNotification = pSink;
    m_pHaltNotification->AddRef();
    *pdwNotificationCookie = 1;
    LeaveCriticalSection(&m_Lock);
    return S_OK;
}

    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::UnregisterHaltNotification( 
        /* [in] */ DWORD dwNotificationCookie)
{
    HRESULT hr;

    EnterCriticalSection(&m_Lock);
    if (dwNotificationCookie != 1 || m_pHaltNotification == NULL) {
        hr = E_INVALIDARG;
    } else {
        m_pHaltNotification->Release();
        m_pHaltNotification = NULL;
        hr = S_OK;
    }
    LeaveCriticalSection(&m_Lock);
    return hr;
}

    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::ReadVirtualMemory( 
        /* [in] */ DWORD64 Address,
        /* [in] */ DWORD NumBytesToRead,
        /* [in] */ DWORD dwCpuNum,
        /* [size_is][out] */ BYTE *pbReadBuffer,
        /* [out] */ DWORD *pNumBytesActuallyRead)
{
    if (Address > 0xffffffff || dwCpuNum != 0) {
        return E_INVALIDARG;
    } else if (Address+(DWORD64)NumBytesToRead > 0xffffffff) {
        return E_INVALIDARG;
    }

    DWORD GuestAddress = (DWORD)Address;
    __int8 TLBCache = 0;
    DWORD BytesRead;
    HRESULT hr = S_OK;

    EnterCriticalSection(&m_Lock);
    for (BytesRead=0; BytesRead < NumBytesToRead; ++BytesRead) {
        size_t HostAddress = Mmu.MmuMapRead.MapGuestVirtualToHost(GuestAddress+BytesRead, &TLBCache);
        if (HostAddress == 0) {
            // Don't allow access to I/O space.  If anything goes wrong, the peripheral device will TerminateWithMessage()
            //if (BoardIOAddress) {
            //    pbReadBuffer[BytesRead] = IOReadByte(&IOIndexHint);
            //} else {
                hr = HRESULT_FROM_WIN32(ERROR_NOACCESS);
                break; // invalid address hit
            //}
        } else {
            pbReadBuffer[BytesRead] = *(unsigned __int8*)HostAddress;
        }
    }
    LeaveCriticalSection(&m_Lock);

    *pNumBytesActuallyRead = BytesRead;

    return hr;
}
    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::WriteVirtualMemory( 
        /* [in] */ DWORD64 Address,
        /* [in] */ DWORD NumBytesToWrite,
        /* [in] */ DWORD dwCpuNum,
        /* [size_is][in] */ const BYTE *pbWriteBuffer,
        /* [out] */ DWORD *pNumBytesActuallyWritten)
{
    if (Address > 0xffffffff || dwCpuNum != 0) {
        return E_INVALIDARG;
    } else if (Address+(DWORD64)NumBytesToWrite > 0xffffffff) {
        return E_INVALIDARG;
    }

    DWORD GuestAddress = (DWORD)Address;
    __int8 TLBCache = 0;
    DWORD BytesWritten;
    HRESULT hr = S_OK;

    EnterCriticalSection(&m_Lock);
    for (BytesWritten=0; BytesWritten < NumBytesToWrite; ++BytesWritten) {
        size_t HostAddress = Mmu.MmuMapWrite.MapGuestVirtualToHost(GuestAddress+BytesWritten, &TLBCache);
        if (HostAddress == 0) {
            // Don't allow access to I/O space.  If anything goes wrong, the peripheral device will TerminateWithMessage()
            //if (BoardIOAddress) {
            //    IOWriteByte(pbWriteBuffer[BytesWritten], &IOIndexHint);
            //} else {
                hr = HRESULT_FROM_WIN32(ERROR_NOACCESS);
                break; // invalid address hit
            //}
        } else {
            *(unsigned __int8*)HostAddress = pbWriteBuffer[BytesWritten];
        }
    }
    LeaveCriticalSection(&m_Lock);

    *pNumBytesActuallyWritten = BytesWritten;

    return hr;
}

    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::ReadPhysicalMemory( 
        /* [in] */ DWORD64 Address,
        /* [in] */ boolean fUseIOSpace,
        /* [in] */ DWORD NumBytesToRead,
        /* [in] */ DWORD dwCpuNum,
        /* [size_is][out] */ BYTE *pbReadBuffer,
        /* [out] */ DWORD *pNumBytesActuallyRead)
{
    // fUseIOSpace is ignored

    if (Address > 0xffffffff || dwCpuNum != 0) {
        return E_INVALIDARG;
    } else if (Address+(DWORD64)NumBytesToRead > 0xffffffff) {
        return E_INVALIDARG;
    }

    DWORD GuestAddress = (DWORD)Address;
    DWORD BytesRead;
    HRESULT hr = S_OK;

    EnterCriticalSection(&m_Lock);
    for (BytesRead=0; BytesRead < NumBytesToRead; ++BytesRead) {
        size_t HostAdjust;
        size_t HostAddress = BoardMapGuestPhysicalToHost(GuestAddress+BytesRead, &HostAdjust);
        if (HostAddress == 0) {
            // Don't allow access to I/O space.  If anything goes wrong, the peripheral device will TerminateWithMessage()
            //if (BoardIOAddress) {
            //    pbReadBuffer[BytesRead] = IOReadByte(&IOIndexHint);
            //} else {
                hr = HRESULT_FROM_WIN32(ERROR_NOACCESS);
                break; // invalid address hit
            //}
        } else {
            pbReadBuffer[BytesRead] = *(unsigned __int8*)HostAddress;
        }
    }
    LeaveCriticalSection(&m_Lock);

    *pNumBytesActuallyRead = BytesRead;

    return hr;
}

    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::WritePhysicalMemory( 
        /* [in] */ DWORD64 Address,
        /* [in] */ boolean fUseIOSpace,
        /* [in] */ DWORD NumBytesToWrite,
        /* [in] */ DWORD dwCpuNum,
        /* [size_is][in] */ const BYTE *pbWriteBuffer,
        /* [out] */ DWORD *pNumBytesActuallyWritten)
{
    // fUseIOSpace is ignored

    if (Address > 0xffffffff || dwCpuNum != 0) {
        return E_INVALIDARG;
    } else if (Address+(DWORD64)NumBytesToWrite > 0xffffffff) {
        return E_INVALIDARG;
    }

    DWORD GuestAddress = (DWORD)Address;
    DWORD BytesWritten;
    HRESULT hr = S_OK;

    EnterCriticalSection(&m_Lock);
    for (BytesWritten=0; BytesWritten < NumBytesToWrite; ++BytesWritten) {
        size_t HostAdjust;
        size_t HostAddress = BoardMapGuestPhysicalToHostWrite(GuestAddress+BytesWritten, &HostAdjust);
        if (HostAddress == 0) {
            // Don't allow access to I/O space.  If anything goes wrong, the peripheral device will TerminateWithMessage()
            //if (BoardIOAddress) {
            //    IOWriteByte(pbWriteBuffer[BytesWritten], &IOIndexHint);
            //} else {
                hr = HRESULT_FROM_WIN32(ERROR_NOACCESS);
                break; // invalid address hit
            //}
        } else {
            *(unsigned __int8*)HostAddress = pbWriteBuffer[BytesWritten];
        }
    }
    LeaveCriticalSection(&m_Lock);

    *pNumBytesActuallyWritten = BytesWritten;

    return hr;
}

    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::AddCodeBreakpoint( 
        /* [in] */ DWORD64 Address,
        /* [in] */ boolean fIsVirtual,
        /* [in] */ DWORD dwBypassCount,
        /* [out] */ DWORD *pdwBreakpointCookie)
{
    if (Address > 0xfffffffe) {
        return E_INVALIDARG;
    }

    DEBUGGER_BREAKPOINT *p = (DEBUGGER_BREAKPOINT*)malloc(sizeof(DEBUGGER_BREAKPOINT));
    if (!p) {
        return E_OUTOFMEMORY;
    }
    EnterCriticalSection(&m_Lock);
    p->GuestAddress = (unsigned __int32)Address;
    p->fIsVirtual = (fIsVirtual) ? true : false;
    p->dwBypassCount = dwBypassCount;
    p->dwBypassOccurrences = 0;
    InsertHeadList(&m_BreakpointList, (LIST_ENTRY*)p);

    // Flush the TC for this instruction.  Size needs to be only long enough to
    // represent a Thumb breakpoint.
    FlushTranslationCache(p->GuestAddress, 2);
    m_fTranslationCacheFlushed = true;

    LeaveCriticalSection(&m_Lock);

    *pdwBreakpointCookie = (DWORD)PtrToLong(p);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorDebugger::SetBreakpointState( 
        /* [in] */ DWORD dwBreakpointCookie,
        /* [in] */ boolean fResetBypassCount)
{
    EnterCriticalSection(&m_Lock);
    DEBUGGER_BREAKPOINT *p = (DEBUGGER_BREAKPOINT*)LongToPtr(dwBreakpointCookie);
    p->dwBypassOccurrences = 0;
    LeaveCriticalSection(&m_Lock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorDebugger::GetBreakpointState(/*[in] */ DWORD dwBreakpointCookie,
                            /* [out] */ DWORD *pdwBypassedOccurrences)
{
    EnterCriticalSection(&m_Lock);
    DEBUGGER_BREAKPOINT *p = (DEBUGGER_BREAKPOINT*)LongToPtr(dwBreakpointCookie);
    *pdwBypassedOccurrences = p->dwBypassOccurrences;
    LeaveCriticalSection(&m_Lock);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CEmulatorDebugger::DeleteBreakpoint( 
        /* [in] */ DWORD dwBreakpointCookie)
{
    EnterCriticalSection(&m_Lock);
    DEBUGGER_BREAKPOINT *p = (DEBUGGER_BREAKPOINT*)LongToPtr(dwBreakpointCookie);
    FlushTranslationCache(p->GuestAddress, 2);
    m_fTranslationCacheFlushed = true;
    RemoveEntryList(&p->Entry);
    LeaveCriticalSection(&m_Lock);
    return S_OK;
}

    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::SetContext( 
        /* [size_is][out][in] */ BYTE *pbContext,
        /* [in] */ DWORD dwContextSize,
        /* [in] */ DWORD dwCpuNum )
{
    if (dwContextSize != sizeof(CONTEXT_ARM3) || dwCpuNum != 0) {
        return E_INVALIDARG;
    }

    PCONTEXT_ARM3 pContextARM3 = (PCONTEXT_ARM3)pbContext;
    if (pContextARM3->RegGroupSelection.fControlRegs) {
        Cpu.GPRs[R14] = pContextARM3->Sp;
        Cpu.GPRs[R13] = pContextARM3->Lr;
        Cpu.GPRs[R15] = pContextARM3->Pc;
        PSR_FULL NewPSR;
        NewPSR.Word = (unsigned __int16)pContextARM3->Psr;
        UpdateCPSRWithFlags(NewPSR);
    }
    if (pContextARM3->RegGroupSelection.fIntegerRegs) {
        Cpu.GPRs[R0] = pContextARM3->R0;
        Cpu.GPRs[R1] = pContextARM3->R1;
        Cpu.GPRs[R2] = pContextARM3->R2;
        Cpu.GPRs[R3] = pContextARM3->R3;
        Cpu.GPRs[R4] = pContextARM3->R4;
        Cpu.GPRs[R5] = pContextARM3->R5;
        Cpu.GPRs[R6] = pContextARM3->R6;
        Cpu.GPRs[R7] = pContextARM3->R7;
        Cpu.GPRs[R8] = pContextARM3->R8;
        Cpu.GPRs[R9] = pContextARM3->R9;
        Cpu.GPRs[R10] = pContextARM3->R10;
        Cpu.GPRs[R11] = pContextARM3->R11;
        Cpu.GPRs[R12] = pContextARM3->R12;
    }
    // Ignore fFloatingPointRegs and fConcanRegs

    return S_OK;
}

    
HRESULT STDMETHODCALLTYPE CEmulatorDebugger::GetContext( 
        /* [size_is][out][in] */ BYTE *pbContext,
        /* [in] */ DWORD dwContextSize,
        /* [in] */ DWORD dwCpuNum)
{
    if (dwContextSize != sizeof(CONTEXT_ARM3) || dwCpuNum != 0 ) {
        return E_INVALIDARG;
    }

    PCONTEXT_ARM3 pContextARM3 = (PCONTEXT_ARM3)pbContext;
    memset(&pContextARM3->Sp, 0, sizeof(CONTEXT_ARM3)-4); // zero out everything past RegGroupSelection

    if (pContextARM3->RegGroupSelection.fControlRegs) {
        pContextARM3->Sp = Cpu.GPRs[R14];
        pContextARM3->Lr = Cpu.GPRs[R13];
        pContextARM3->Pc = Cpu.GPRs[R15];
        pContextARM3->Psr = GetCPSRWithFlags();
    }
    if (pContextARM3->RegGroupSelection.fIntegerRegs) {
        pContextARM3->R0 = Cpu.GPRs[R0];
        pContextARM3->R1 = Cpu.GPRs[R1];
        pContextARM3->R2 = Cpu.GPRs[R2];
        pContextARM3->R3 = Cpu.GPRs[R3];
        pContextARM3->R4 = Cpu.GPRs[R4];
        pContextARM3->R5 = Cpu.GPRs[R5];
        pContextARM3->R6 = Cpu.GPRs[R6];
        pContextARM3->R7 = Cpu.GPRs[R7];
        pContextARM3->R8 = Cpu.GPRs[R8];
        pContextARM3->R9 = Cpu.GPRs[R9];
        pContextARM3->R10 = Cpu.GPRs[R10];
        pContextARM3->R11 = Cpu.GPRs[R11];
        pContextARM3->R12 = Cpu.GPRs[R12];
    }
    // Ignore fFloatingPointRegs and fConcanRegs

    // Pass back the MMU control register in Reserved15.  The DeviceEmulator eXdi driver needs this value.
    pContextARM3->Reserved15 = Mmu.ControlRegister.Word;

    return S_OK;
}

PVOID CEmulatorDebugger::NotifyDebugger(unsigned __int32 InstructionPointer, DEVICEEMULATOR_HALT_REASON_TYPE HaltReason, DWORD dwCode)
{
    Cpu.GPRs[15] = InstructionPointer;
    m_fTranslationCacheFlushed = false;

    ResetEvent(g_hDebuggerEvent);
    if (m_pHaltNotification) {
        m_pHaltNotification->HaltCallback(HaltReason, dwCode, (DWORD64)InstructionPointer, 0);

        // Block waiting for the callback to update our state
        WaitForSingleObject(g_hDebuggerEvent, INFINITE);
    }

    if (Cpu.GPRs[R15] == InstructionPointer && m_fTranslationCacheFlushed == false) {
        return (PVOID)1; // Instruction pointer hasn't changed and TC wasn't flushed - continue execution
    } else {
        return NULL; // Return to CpuSimulate to begin at Cpu.GPRs[R15].
    }
}

/*++

Description:

    Notify the debugger of a target exception and process debugger commands
    until told to continue.

    This is always called from the CPU thread, so CPU execution is blocked
    while within this routine.  The call happens at the end of the
    instruction.

Returns:

    NULL - return to CpuSimulate and begin emulation at Cpu.GPRs[R15]
    1    - return to the caller and continue execution from the Translation Cache
    else - jump to the native address to continue execution

--*/
PVOID CEmulatorDebugger::DebugEntry(unsigned __int32 InstructionPointer, EARMExceptionType etReason)
{
    DWORD dwCode = 0;
    DEVICEEMULATOR_HALT_REASON_TYPE HaltReason = haltreasonNone;

    switch (etReason) {
    case etDebuggerInterrupt:
        EnterCriticalSection(&InterruptLock);

        // The order of checks is important.  Breakin should be highest priority,
        // followed by breakpoint, singlestep, and IRQ interrupt.  Breakin should
        // be reported even if the same instruction is the target of a breakpoint
        // or single-step.  All debugger events should pre-empt an IRQ, so that
        // single-steps do not step into interrupts.
        if (Cpu.DebuggerInterruptPending & DEBUGGER_INTERRUPT_BREAKIN) {
            Cpu.DebuggerInterruptPending = 0;
            dwCode = STATUS_BREAKPOINT;
            HaltReason = haltreasonUser;
        } else if (Cpu.DebuggerInterruptPending & DEBUGGER_INTERRUPT_BREAKPOINT) {
            Cpu.DebuggerInterruptPending = 0;
            dwCode = STATUS_BREAKPOINT;
            HaltReason = haltreasonBp;
        } else if (Cpu.IRQInterruptPending && Cpu.CPSR.Bits.IRQDisable == 0) {
            LeaveCriticalSection(&InterruptLock);
            return CpuRaiseIRQException(InstructionPointer);
        }

        LeaveCriticalSection(&InterruptLock);
        break;

    case etDataAbort:
        return (PVOID)1;
#if 0 // Don't report etDataAbort to PB as they are not actually true access violations
        dwCode = STATUS_ACCESS_VIOLATION;
        HaltReason = haltreasonException;
        break;
#endif

    case etPrefetchAbort:
        return (PVOID)1;
#if 0 // Don't report etPrefetchAbort to PB as they are not actually true access violations
        if (InstructionPointer > 0xf0000000) { // don't report prefetch aborts related to InterlockedXXX APIs
            return (PVOID)1;
        }
        dwCode = STATUS_ACCESS_VIOLATION;
        HaltReason = haltreasonException;
        break;
#endif

    case etUndefinedInstruction:
        // fall through into the default case
        dwCode = STATUS_ILLEGAL_INSTRUCTION;
        HaltReason = haltreasonException;
        break;

    case etReset:
        // fall through into the default case
    default:
        dwCode = 0;
        HaltReason = haltreasonUnknown;
        break;
    }

    if (HaltReason == haltreasonNone) {
        // This was most likely an interrupt poll with no interrupt pending
        return (PVOID)1;
    }

    return NotifyDebugger(InstructionPointer, HaltReason, dwCode);
}

// This is called at the beginning of each instruction
PVOID CEmulatorDebugger::SingleStepEntry(unsigned __int32 InstructionPointer)
{
    if (m_SingleStepCount == 0) {
        return (PVOID)1; // keep going - not single stepping
    }

    --m_SingleStepCount;
    if (m_SingleStepCount != 0) {
        return (PVOID)1; // keep going - step count hasn't gone to zero yet
    }
    return NotifyDebugger(InstructionPointer, haltreasonStep, STATUS_SINGLE_STEP);
}

// TODO: support breakpoints on physical addresses when the MMU is on (by mapping the guest virtual
//       address to guest physical and making the comparison based on guest physical.
bool CEmulatorDebugger::DebugHardwareBreakpointPresent(unsigned __int32 InstructionPointer)
{
    EnterCriticalSection(&m_Lock);
    LIST_ENTRY *pList = m_BreakpointList.Flink;
    while (pList != &m_BreakpointList) {
        DEBUGGER_BREAKPOINT *p = (DEBUGGER_BREAKPOINT*)pList;
        if ( (   (p->fIsVirtual && Mmu.ControlRegister.Bits.M)          // virtual address and MMU is enabled
              || (!p->fIsVirtual && Mmu.ControlRegister.Bits.M == 0))   // physical address and MMU is disabled
              && p->GuestAddress == InstructionPointer) {
            LeaveCriticalSection(&m_Lock);
            return true;
        }
        pList = pList->Flink;
    }
    LeaveCriticalSection(&m_Lock);
    return false;
}

HRESULT CpuGetDebuggerInterface(IDeviceEmulatorDebugger **ppDebugger)
{
    if (NULL == g_pDebugger) {
        g_pDebugger = new CEmulatorDebugger;
        if (NULL == g_pDebugger) {
            return E_OUTOFMEMORY;
        }
    }
    *ppDebugger = g_pDebugger;
    return S_OK;
}

CSystemCallbacks * __fastcall CpuGetSystemCallbacks(void)
{
    return NULL;
}

PVOID
DebugEntry
(
    unsigned __int32 InstructionPointer,
    EARMExceptionType etReason
)
/*++

Description:

    Notify the debugger of a target exception and process debugger commands
    until told to continue.

    This is always called from the CPU thread, so CPU execution is blocked
    while within this routine.

Returns:

    NULL - return to CpuSimulate and begin emulation at Cpu.GPRs[R15]
    1    - return to the caller and continue execution from the Translation Cache
    else - jump to the native address to continue execution

--*/
{
    if (g_pDebugger) {
        return g_pDebugger->DebugEntry(InstructionPointer, etReason);
    } else if (etReason == etDebuggerInterrupt && Cpu.IRQInterruptPending && Cpu.CPSR.Bits.IRQDisable == 0) {
        return CpuRaiseIRQException(InstructionPointer);
    } else {
        return (PVOID)1;
    }
}

PVOID
SingleStepEntry(unsigned __int32 InstructionPointer)
{
    if (g_pDebugger) {
        return g_pDebugger->SingleStepEntry(InstructionPointer);
    } else {
        return (PVOID)1;
    }
}

bool DebugHardwareBreakpointPresent(unsigned __int32 InstructionPointer)
{
    if (g_pDebugger) {
        return g_pDebugger->DebugHardwareBreakpointPresent(InstructionPointer);
    } else {
        return false;
    }
}
