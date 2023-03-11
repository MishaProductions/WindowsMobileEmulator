/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

// ConfigDlg.cpp : Implementation of CConfigDialog

#include "stdafx.h"

LRESULT CConfigDialog::OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    CheckedAttach(IDC_TAB,m_ctrlTab);
    m_hAccel = ::LoadAccelerators(Configuration.Resources.getSatelliteHModule(), MAKEINTRESOURCE(ID_TABACCEL));

    m_tabs=0;
    for (std::vector<ITabPage*>::const_iterator i=m_pages.begin();i!=m_pages.end();++i) {
        // Add each page to the tab control
        TCITEM tci;
        tci.mask = TCIF_TEXT | TCIF_PARAM;
        CString strCaption;
        IfZeroThrow(strCaption.LoadString((*i)->GetNameResourceID()),E_FAIL);
        tci.pszText = const_cast<LPTSTR>(static_cast<LPCTSTR>(strCaption));
        tci.iImage = -1;  // No image

        // -1 is returned by CWinTab::InsertItem (TCM_INSERTITEM message) on
        // failure.
        IfFalseThrow(m_ctrlTab.InsertItem(m_tabs++,&tci) != -1,E_FAIL);

        // Create the page
        IfFalseThrow((*i)->Init(m_hWnd),E_FAIL);
    }

    m_iSel=-1;
    // Activate the General page
    ChangeTab(0);
    return 1;  // Let the system set the focus
}

void CConfigDialog::DestroyTabs()
{
    for (int i=0;i<m_tabs;++i) {
        m_pages[i]->Reset();
        m_pages[i]->Destroy();
    }
    m_iSel=-1;
}

LRESULT CConfigDialog::OnClickedOK(WORD, WORD, HWND, BOOL&)
{
    if (CommitChanges())
        DestroyTabs();
    result=IDOK;
    return 0;
}

LRESULT CConfigDialog::OnClickedCancel(WORD, WORD, HWND, BOOL&)
{
    DestroyTabs();
    result=IDCANCEL;
    return 0;
}

LRESULT CConfigDialog::OnCtrlTab(WORD, WORD, HWND, BOOL&)
{
    ChangeTab((m_ctrlTab.GetCurSel()+1)%m_tabs);
    return 0;
}

LRESULT CConfigDialog::OnCtrlShiftTab(WORD, WORD, HWND, BOOL&)
{
    ChangeTab((m_ctrlTab.GetCurSel()+m_tabs-1)%m_tabs);
    return 0;
}

LRESULT CConfigDialog::OnTcnSelchangeTab(int, LPNMHDR, BOOL&)
{
    ChangeTab(m_ctrlTab.GetCurSel());
    return 0;
}

void CConfigDialog::ChangeTab(int iTo)
{
    ASSERT(iTo >= 0);

    TCITEM tci;
    memset(&tci, 0, sizeof(TCITEM));
    tci.mask = TCIF_PARAM;

    if (m_iSel!=-1) {
        // deactivate and hide the old page.
        m_pages[m_iSel]->Hide();
    }
    m_iSel=iTo;
    m_strF1Keyword=m_pages[m_iSel]->GetHelpKeyword();
    m_ctrlTab.SetCurSel(m_iSel);

    RECT rc;
    m_ctrlTab.GetWindowRect(&rc);
    ::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rc, 2);
    m_ctrlTab.AdjustRect(FALSE, &rc);
    m_pages[iTo]->Show(rc);
}

bool CConfigDialog::CommitChanges()
{
    int n=0;
    for (std::vector<ITabPage*>::const_iterator i=m_pages.begin();i!=m_pages.end();++i,++n) {
        if ((*i)->BValidating()) {
            // Contents of tab are OK - commit them
            (*i)->CommitChanges();
        }
        else {
            // Contents of tab are invalid - change to the tab so the user can fix them.
            ChangeTab(n);
            return false;
        }
    }
    for (std::vector<ITabPage*>::const_iterator i=m_pages.begin();i!=m_pages.end();++i,++n)
        (*i)->Reset();
    return true;
}

// We want to be able to translate accelerator keys, so rather than using the
// Win32 DialogBox message pump, we implement our own, simulating a modal
// dialog by using a modeless one.
int CConfigDialog::SimulateModal(HWND hWnd)
{
    // Save the window with current focus so that we can restore focus later
    HWND hwndOriginalFocus = ::GetForegroundWindow();

    // Disable the Tools->Options dialog. If the user cancels it we're in trouble.

    // Search up until we find a window that isn't a child. This is the dialog to disable.
    while (::GetWindowLong(hWnd, GWL_STYLE) & WS_CHILD)
        hWnd = ::GetParent(hWnd);
    // If the window was the desktop window, then don't disable it now and don't reenable it later.
    // Also, if the window was already disabled, then don't enable it later.
    if (hWnd == ::GetDesktopWindow() || ::EnableWindow(hWnd,FALSE))
        hWnd = NULL;

    // Create and display the Emulator Config dialog
    Create(hWnd);
    if ( hWnd == NULL)
        CenterWindow();

    ShowWindow(SW_SHOW);

    // Standard ATL message pump with a TranslateAccelerator for our Ctrl-Tab
    MSG msg;
    while (::GetMessage(&msg, 0, 0, 0) && m_iSel!=-1)
    {
        if (!::TranslateAccelerator(m_hWnd,m_hAccel,&msg))
            if (!IsDialogMessage(&msg))
                ::DispatchMessage(&msg);
    }

    // Clean up
    DestroyWindow();

    // Restore the window if we disabled it earlier
    if (hWnd)
        ::EnableWindow(hWnd, TRUE);

    // Restore focus to what it was before we started
    if (::IsWindow(hwndOriginalFocus))
        ::SetForegroundWindow(hwndOriginalFocus);
    return result;
}

bool ShowConfigDialog(HWND hwndParent,PropertyMap& properties)
{
    PropertyMap propertiesCopy(properties);
    _AtlBaseModule.SetResourceInstance(Configuration.Resources.getSatelliteHModule());
    CConfigDialog dlgConnectixCfg(propertiesCopy, Configuration.DialogOnly);
    if (dlgConnectixCfg.SimulateModal(hwndParent)==IDOK) {
        // If the user OKed the dialog, update the device data
        properties = propertiesCopy;
        return true;
    }
    return false;
}

void __fastcall ShowConfigDialog(HWND hwndParent)
{
    DCPTRY {
        PropertyMap properties = ConfigurationToPropertyString(&Configuration);
        if (ShowConfigDialog(hwndParent,properties))
            (void)PropertyMapToEmulatorConfig(properties, &Configuration);
    }
    DCPCATCH(CDCPException& err)
    {
        err.Display();
    }
}


// Find the next token in the string.
int StrFind(const CString& str,int i)
{
    while (i<str.GetLength() && str[i]!=L'|')
        ++i;
    return i;
}

PropertyMap ParsePropertyString(CString str)
{
    PropertyMap properties;

    // Pre-initialize the RAMSize to 64mb.  If there is a RAMSize specified in the
    // option string, it will override the default.  Without this line, the
    // RAMSize field is blank if there is no RAMSize value in the datastore.
    properties[g_strDSPropertyRAMSize] = CMarshalledProperty(L"64", "1", !fIsRunning);

    int iName=0;
    do {
        int iValue=1+StrFind(str,iName);
        int iVisible=1+StrFind(str,iValue);
        int iNextName=1+StrFind(str,iVisible);
        properties[CMarshalledProperty::Unquote(str.Mid(iName,iValue-(iName+1)))] =
            CMarshalledProperty(
                str.Mid(iValue,iVisible-(iValue+1)),
                str.Mid(iVisible,iNextName-(iVisible+1)));
        iName = iNextName;
    } while (iName<str.GetLength());
    return properties;
}

wchar_t * MACToString( __out_ecount(13) wchar_t * buffer, unsigned __int8 * mac_address )
{
    // Convert the integer value one byte at a time
    for (int i = 0; i < 6; i++ )
    {
        buffer[2*i] = (mac_address[i] >> 4 ) > 9 ? ((mac_address[i] >> 4) - 10) + 'A':
                                                   (mac_address[i] >> 4 ) + '0';
        buffer[2*i+1] = (mac_address[i] & 0x0F) > 9 ? ((mac_address[i] & 0x0F) - 10)+'A':
                                                      ( mac_address[i] & 0x0F) + '0';
    }
    buffer[12] = 0;
    return buffer;
}


wchar_t * _itow_wrapper( int exp, __out_ecount(size) wchar_t * buffer, size_t size, int base )
{
  if ( _itow_s((exp), buffer, size, base) )
    buffer[0] = L'\0';
  return buffer;
}

#define EmptyStringIfNull(exp) ( (exp) != NULL ? (exp) : L"")
#define EmptyStringIfZero(exp, base) ( (exp) != 0 ? _itow_wrapper((exp), Buffer, MAX_LOADSTRING, base) : L"")
#define ConvertToTrueFalse(val) ( (val) ? g_strDSValueTrue : g_strDSValueFalse)
#define ConvertToOrientation(val) ( (val) >= 2? ((val) == 2 ? g_strDSOrientation_180 : g_strDSOrientation_270) : \
                                              ((val) == 0 ? g_strDSDefaultOrientation : g_strDSOrientation_90) )

PropertyMap ConfigurationToPropertyString(EmulatorConfig * config)
{
    PropertyMap properties;
    wchar_t Buffer[MAX_LOADSTRING];

    // Peripheral Tab
    properties[g_strDSPropertySerialPort0]     =
        CMarshalledProperty(EmptyStringIfNull(config->getUART(0)),L"1",true);//"SerialPort0");
    properties[g_strDSPropertySerialPort1]     =
        CMarshalledProperty(EmptyStringIfNull(config->getUART(1)),L"1",true);//"SerialPort1");
    properties[g_strDSPropertySerialPort2]     =
        CMarshalledProperty(EmptyStringIfNull(config->getUART(2)),L"1",true);//"SerialPort2");
    properties[g_strDSPropertyCreateConsole]   =
        CMarshalledProperty(ConvertToTrueFalse(config->fCreateConsoleWindow),L"1",!fIsRunning);//"CreateConsole");

    // Display Tab
    unsigned int Width  = config->ScreenWidth;
    unsigned int Height = config->ScreenHeight;
    unsigned int ColorDepth = config->ScreenBitsPerPixel;

    properties[g_strDSPropertyScreenWidth]     =
        CMarshalledProperty(EmptyStringIfZero(Width, 10),L"1",!fIsRunning);//"ScreenWidth");
    properties[g_strDSPropertyScreenHeight]    =
        CMarshalledProperty(EmptyStringIfZero(Height, 10),L"1",!fIsRunning);//"ScreenHeight");
    if ( (ColorDepth == 16 || ColorDepth == 24 || ColorDepth == 32) &&
         !_itow_s(ColorDepth, Buffer, MAX_LOADSTRING, 10) )
        properties[g_strDSPropertyColorDepth]      =
            CMarshalledProperty(Buffer, L"1",!fIsRunning);//"ColorDepth");
    else
    {
        ASSERT(config->getSkinXMLFile() != NULL );
        properties[g_strDSPropertyColorDepth]      =
            CMarshalledProperty(g_strDSDefaultColorDepth,L"1",!fIsRunning);//"ColorDepth");
    }
    properties[g_strDSPropertySkin]            =
        CMarshalledProperty(EmptyStringIfNull(config->getSkinXMLFile()),L"1",true);//"Skin");
    properties[g_strDSPropertyShowSkin]        =
        CMarshalledProperty(ConvertToTrueFalse(config->fShowSkin && config->isSkinFileSpecified()),L"1",true);//"ShowSkin");
    properties[g_strDSPropertyOrientation]     =
        CMarshalledProperty(ConvertToOrientation(config->RotationMode),L"1",true);//"Orientation");
    properties[g_strDSPropertyZoom2x]          =
        CMarshalledProperty(ConvertToTrueFalse(config->IsZoomed),L"1",true);//"Zoom2x");
    properties[g_strDSPropertyAlwaysOnTop]     =
        CMarshalledProperty(ConvertToTrueFalse(config->IsAlwaysOnTop),L"1",true);//"AlwaysOnTop");
    properties[g_strDSPropertyEnableToolTips]  =
        CMarshalledProperty(ConvertToTrueFalse(config->isToolTipEnabled()),"1",true);//"EnableToolTips");

    // General Tab
    properties[g_strDSPropertyOSBinImage]      =
        CMarshalledProperty(EmptyStringIfNull(config->getROMImageName()),L"1",!fIsRunning);//"OSBinImage");
    properties[g_strDSPropertySpecifyAddress]  =
        CMarshalledProperty(ConvertToTrueFalse(config->ROMBaseAddress),L"1",!fIsRunning);//"SpecifyAddress");
    properties[g_strDSPropertyImageAddress]    =
        CMarshalledProperty(EmptyStringIfZero(config->ROMBaseAddress, 16),L"1",!fIsRunning);//"ImageAddress");
    properties[g_strDSPropertySpecifyRAMSize]  =
        CMarshalledProperty(ConvertToTrueFalse(config->PhysicalMemoryExtensionSize),L"1",!fIsRunning);//"SpecifyRAMSize");
    properties[g_strDSPropertyRAMSize]    =
        CMarshalledProperty(EmptyStringIfZero(config->PhysicalMemoryExtensionSize+64, 10),L"1",!fIsRunning);//"RAMSize");
    properties[g_strDSPropertyFlashFile]       =
        CMarshalledProperty(EmptyStringIfNull(config->getFlashStateFile()),L"1",!fIsRunning);//"FlashFile");
    if ( _itow_s((config->getKeyboardSelector()), Buffer, MAX_LOADSTRING, 10) )
        properties[g_strDSPropertyHostKey]         =
              CMarshalledProperty(L"0",L"1",true);//"HostKey");
    else
        properties[g_strDSPropertyHostKey]         =
              CMarshalledProperty(Buffer,L"1",true);//"HostKey");
    properties[g_strDSPropertyFolderShare]     =
        CMarshalledProperty(EmptyStringIfNull(config->getFolderShareName()),L"1",true);//"SharedFolder");

    // Network Tab
    properties[g_strDSPropertyEthernetEnabled] =
        CMarshalledProperty(ConvertToTrueFalse(config->PCMCIACardInserted),L"1",true);//"EthernetEnabled");
    properties[g_strDSPropertyCS8900EthernetEnabled] =
        CMarshalledProperty(ConvertToTrueFalse(config->NetworkingEnabled),L"1",!fIsRunning);//"CS8900EthernetEnabled");
    properties[g_strDSPropertyHostOnlyEthernetEnabled] =
        CMarshalledProperty(ConvertToTrueFalse(config->getHostOnlyRouting()),L"1",true);//"HostOnlyEthernetEnabled");
    properties[g_strDSPropertyNE2000Adapter]   =
        CMarshalledProperty(MACToString(Buffer, config->SuggestedAdapterMacAddressNE2000),L"1",!fIsRunning);//"NE2000Adapter");
    properties[g_strDSPropertyCS8900Adapter]   =
        CMarshalledProperty(MACToString(Buffer, config->SuggestedAdapterMacAddressCS8900),L"1",!fIsRunning);//"CS8900Adapter");

    return properties;
}
wchar_t * CopyPropValue( CString str )
{
    unsigned int length = str.GetLength();
    wchar_t * Buffer = new wchar_t[length + 1];
    if ( Buffer != NULL )
    {
      if (FAILED(StringCchPrintfW(Buffer, length + 1, L"%s", str.GetBuffer()))) {
        ASSERT(FALSE);
      }
    }
    return Buffer;
}

bool PropertyMapToEmulatorConfig(PropertyMap properties, EmulatorConfig * config)
{
    bool fRet = false;
    wchar_t* argv[35];
    int argc;
    EmulatorConfig NewConfig;
    bool fShowSkin;
    bool fIsArgvMalloc[ARRAY_SIZE(argv)];

    NewConfig.init();

    // Reset the flags and the count
    argc = 0;
    memset(fIsArgvMalloc, 0, sizeof(fIsArgvMalloc));

    // General tab
    fIsArgvMalloc[argc] = true;
    argv[argc++] = CopyPropValue(properties[g_strDSPropertyOSBinImage].Value());

    if (properties[g_strDSPropertySpecifyAddress].IsTrue()) {
        argv[argc++] = L"/r";
        fIsArgvMalloc[argc] = true;
        argv[argc++] = CopyPropValue(properties[g_strDSPropertyImageAddress].Value());
    }

    if (properties[g_strDSPropertySpecifyRAMSize].IsTrue()) {
        argv[argc++] = L"/memsize";
        fIsArgvMalloc[argc] = true;
        argv[argc++] = CopyPropValue(properties[g_strDSPropertyRAMSize].Value());
    }

    if (properties[g_strDSPropertyFlashFile].Value().GetLength() != 0) {
        argv[argc++] = L"/s";
        fIsArgvMalloc[argc] = true;
        argv[argc++] = CopyPropValue(properties[g_strDSPropertyFlashFile].Value());
    }
    // The property contains for the host key a VK keycode
    unsigned int keycode = _wtoi(properties[g_strDSPropertyHostKey].Value().GetBuffer());
    // Search through the array looking for a matching keycode
    int item;
    for(item = 0; item < ARRAY_SIZE(HostKeys); item++) {
        if ( keycode == HostKeys[item].Value )
        {
            argv[argc++]= L"/hostkey";
            argv[argc++] = HostKeys[item].Name;
            break;
        }
    }
    // If we didn't find a match we have a mismatch somewhere
    if (item == ARRAY_SIZE(HostKeys) )
    {
        ASSERT(FALSE);
    }
    if (properties[g_strDSPropertyFolderShare].Value().GetLength() != 0) {
        argv[argc++] = L"/sharedfolder";
        fIsArgvMalloc[argc] = true;
        argv[argc++] = CopyPropValue(properties[g_strDSPropertyFolderShare].Value());
    }

    // Display tab
    CString SkinPath = properties[g_strDSPropertySkin].Value();
    if (properties[g_strDSPropertyShowSkin].IsTrue() ||
        config->isSkinFileSpecified() &&
        _wcsicmp(SkinPath.GetBuffer(), config->getSkinXMLFile()) == 0 ) {
        argv[argc++] = L"/skin";
        fIsArgvMalloc[argc] = true;
        argv[argc++] = CopyPropValue(SkinPath);
    }
    else {
        UINT VideoWidth;
        UINT VideoHeight;
        UINT VideoColorDepth;

        // Allocate the buffer for the video parameter
        wchar_t * VideoArgs = new wchar_t[64]; // enough for intXintXint
        if ( VideoArgs == NULL )
            goto Exit;

        // Fill out the argument array early so that the cleanup happens correctly
        // in the error case
        argv[argc++] = L"/video";
        fIsArgvMalloc[argc] = true;
        argv[argc++] = VideoArgs;

        VideoWidth = _wtoi(properties[g_strDSPropertyScreenWidth].Value().GetBuffer());
        VideoHeight = _wtoi(properties[g_strDSPropertyScreenHeight].Value().GetBuffer());
        VideoColorDepth = _wtoi(properties[g_strDSPropertyColorDepth].Value().GetBuffer());
        if (VideoWidth == 0 || VideoHeight == 0 || VideoColorDepth == 0 ||
            FAILED(StringCchPrintfW(VideoArgs,
                                    64,
                                    L"%ux%ux%u",
                                    VideoWidth, VideoHeight, VideoColorDepth)))
        {
                ASSERT(FALSE); // VideoArgs buffer is too small
                goto Exit;
        }
    }

    fShowSkin = properties[g_strDSPropertyShowSkin].IsTrue();
    if (properties[g_strDSPropertyZoom2x].IsTrue()) {
        argv[argc++] = L"/z";
    }
    if (properties[g_strDSPropertyAlwaysOnTop].IsTrue()) {
        argv[argc++] = L"/a";
    }
    argv[argc++] = L"/rotate";
    fIsArgvMalloc[argc] = true;
    argv[argc++] = CopyPropValue(properties[g_strDSPropertyOrientation].Value());

    argv[argc++] = L"/tooltips";
    if (properties[g_strDSPropertyEnableToolTips].IsTrue())
        argv[argc++] = L"on";
    else
        argv[argc++] = L"off";

    // Network tab
    wchar_t defaultValue[MAX_LOADSTRING];
    Configuration.Resources.getString(ID_DEFAULT_VALUE, defaultValue);
    if (properties[g_strDSPropertyEthernetEnabled].IsTrue()) {
        argv[argc++] = L"/p";
        CString NE2000Mac = properties[g_strDSPropertyNE2000Adapter].Value();
        if ( _wcsicmp(NE2000Mac.GetBuffer(), L"000000000000") != 0 )
        {
            fIsArgvMalloc[argc] = true;
            argv[argc++] = CopyPropValue(NE2000Mac.GetBuffer());
        }
    }
    if (properties[g_strDSPropertyCS8900EthernetEnabled].IsTrue()) {
        argv[argc++] = L"/n";
        CString CS8900Mac = properties[g_strDSPropertyCS8900Adapter].Value();
        if ( _wcsicmp(CS8900Mac.GetBuffer(), L"000000000000") != 0 )
        {
            fIsArgvMalloc[argc] = true;
            argv[argc++] = CopyPropValue(CS8900Mac.GetBuffer());
        }
    }
    if (properties[g_strDSPropertyHostOnlyEthernetEnabled].IsTrue()) {
        argv[argc++] = L"/h";
    }

    // Peripherals tab
    if (properties[g_strDSPropertySerialPort0].Value() != gc_strNoneInvariant) {
        argv[argc++] = L"/u0";
        fIsArgvMalloc[argc] = true;
        argv[argc++] = CopyPropValue(properties[g_strDSPropertySerialPort0].Value());
    }
    if (properties[g_strDSPropertySerialPort1].Value() != gc_strNoneInvariant) {
        argv[argc++] = L"/u1";
        fIsArgvMalloc[argc] = true;
        argv[argc++] = CopyPropValue(properties[g_strDSPropertySerialPort1].Value());
    }
    if (properties[g_strDSPropertySerialPort2].Value() != gc_strNoneInvariant) {
        argv[argc++] = L"/u2";
        fIsArgvMalloc[argc] = true;
        argv[argc++] = CopyPropValue(properties[g_strDSPropertySerialPort2].Value());
    }

    if (properties[g_strDSPropertyCreateConsole].IsTrue()) {
        argv[argc++] = L"/c";
    }
    ASSERT(argc < ARRAY_SIZE(argv));

    // Check for any NULL pointers in argv, caused by out-of-memory in CopyPropValue
    for (int i=0; i<argc; ++i) {
        if (argv[i] == NULL) {
            goto Exit;
        }
    }

    // Parse the new command-line to constrct the new configuration object, NewConfig
    NewConfig.fShowSkin = fShowSkin;
    ParseErrorStruct ParseError;
    if (!BoardParseCommandLine(argc, argv, &NewConfig, &ParseError)) {
        if ( ParseError.ParseError == 0 )
            ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
        else
        {
            ShowDialog(ParseError.ParseError, &ParseError.falting_arg, L"");
        }
        goto Exit;
    }

    // Command line parsed OK.  Now update the "live" configuration, including
    // notifying peripheral devices of the configuration changes.
    if (!BoardUpdateConfiguration(&NewConfig)) {
        ShowDialog(ID_MESSAGE_FAILED_CONFIG_UPDATE);
        goto Exit;
    }
    fRet = true;
Exit:
    // Free the argument memory allocated by CopyPropValue and new()
    for (int i=0; i<ARRAY_SIZE(fIsArgvMalloc); ++i) {
        if (fIsArgvMalloc[i]) {
            delete [] argv[i];
        }
    }
    return fRet;
}
HRESULT __fastcall ShowConfigDialog(HWND hwndParent,BSTR bstrConfig,BSTR* pbstrConfig)
{
    HRESULT hr = S_OK;

    DCPTRY {
        IfFalseThrow(pbstrConfig!=NULL,E_INVALIDARG);
        PropertyMap properties = ParsePropertyString(CString(bstrConfig));
        ShowConfigDialog(hwndParent,properties);
        CComBSTR bstrConfigOut = std::for_each(properties.begin(),properties.end(),CAccumulateProperties());
        *pbstrConfig = bstrConfigOut.Detach();
    }
    DCPCATCH(CDCPException& err)
    {
        err.Display();
        hr=err.GetHR();
    }

    return hr;
}
