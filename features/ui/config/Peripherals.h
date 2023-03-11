/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef PERIPHERALS_H
#define PERIPHERALS_H

// EmConfigHwPage.h : Declaration of CPeripherals

#define NYI_SERIAL_VIA_FILE  // undefine this once the feature is implemented

class CPeripherals :
    public CCheckedDialog<CPeripherals>, public ITabPage
{
public:
    CPeripherals(PropertyMap& properties) : ITabPage(properties)
    {
        IfZeroThrow(m_strNone.LoadString(IDS_PORTNONE),E_FAIL);
    }
    int GetNameResourceID() { return IDS_EMULPERIPHERALSTAB; }
    CString GetHelpKeyword() { return HELPKEYWORD_VSD_CONFIGEMUPERIPHERALS; }

    enum { IDD = IDD_EMCONFIGPERIPHERALSPAGE };

BEGIN_MSG_MAP(CPeripherals)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    COMMAND_HANDLER(IDC_BROWSESERIALPORTFILE0, BN_CLICKED, OnBrowseSerialPort0File)
    COMMAND_HANDLER(IDC_BROWSESERIALPORTFILE1, BN_CLICKED, OnBrowseSerialPort1File)
    COMMAND_HANDLER(IDC_BROWSESERIALPORTFILE2, BN_CLICKED, OnBrowseSerialPort2File)
END_MSG_MAP()

    LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnBrowseSerialPort0File(WORD, WORD, HWND, BOOL&) { GetFileFromBrowse(L"",IDS_SERIALFILEFILTER,m_comboSerial0,m_hWnd); return 0; }
    LRESULT OnBrowseSerialPort1File(WORD, WORD, HWND, BOOL&) { GetFileFromBrowse(L"",IDS_SERIALFILEFILTER,m_comboSerial1,m_hWnd); return 0; }
    LRESULT OnBrowseSerialPort2File(WORD, WORD, HWND, BOOL&) { GetFileFromBrowse(L"",IDS_SERIALFILEFILTER,m_comboSerial2,m_hWnd); return 0; }

    void CommitChanges();
    bool BValidating();
private:
    CString GetPortFromInvariant(const CString& strPortInvariant);
    CString GetInvariantFromPort(const CString& strPort);
    void EnumerateSerialPorts();
    void InitSerialPortCombo(CWinComboBox& combo,CString strPort);

    CString m_strNone;

    CWinComboBox m_comboSerial0;
    CWinButton m_btnBrowseSerial0File;
    CWinComboBox m_comboSerial1;
    CWinButton m_btnBrowseSerial1File;
    CWinComboBox m_comboSerial2;
    CWinButton m_btnBrowseSerial2File;
    CWinButton m_checkCreateConsole;
};

#endif // PERIPHERALS_H
