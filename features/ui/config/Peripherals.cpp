
/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

// Peripherals.cpp : Implementation of CPeripherals

#include "stdafx.h"
#include "setupapi.h" // SDK .h file !!!

CString CPeripherals::GetPortFromInvariant(const CString& strPortInvariant)
{
    if (strPortInvariant==gc_strNoneInvariant)
        return m_strNone;
    return strPortInvariant;
}

CString CPeripherals::GetInvariantFromPort(const CString& strPort)
{
    if (strPort==m_strNone)
        return gc_strNoneInvariant;
    return strPort;
}

void CPeripherals::EnumerateSerialPorts()
{
    // First build a list of all the device that support serial port interface
    HDEVINFO hdi = SetupDiGetClassDevs(
        (GUID *)&GUID_CLASS_COMPORT, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    if (INVALID_HANDLE_VALUE == hdi)
        return;

    SP_DEVICE_INTERFACE_DATA devInterfaceData;
    devInterfaceData.cbSize = sizeof (SP_DEVICE_INTERFACE_DATA);
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof (SP_DEVINFO_DATA);

    // Enumerate the interface devices
    for (
        DWORD dwIndex = 0;
        SetupDiEnumInterfaceDevice(hdi,NULL,(GUID *)&GUID_CLASS_COMPORT,dwIndex,&devInterfaceData);
        dwIndex++
    )
    {
        // For each interface device, get the parent device node
        SetupDiGetDeviceInterfaceDetail(hdi,&devInterfaceData,NULL,0,NULL,&devInfoData);

        HKEY hKeyDev = SetupDiOpenDevRegKey (hdi, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (INVALID_HANDLE_VALUE == hKeyDev)
            continue;

        // Get the value for PortName
        WCHAR szPort[64];
        DWORD cbData = sizeof(szPort);
        DWORD dwRet = RegQueryValueEx(hKeyDev, L"PortName", NULL, NULL, reinterpret_cast<LPBYTE>(szPort), &cbData);
        RegCloseKey(hKeyDev);
        if (ERROR_SUCCESS == dwRet) {
            m_comboSerial0.AddString(szPort);
            m_comboSerial1.AddString(szPort);
            m_comboSerial2.AddString(szPort);
        }
    }

    SetupDiDestroyDeviceInfoList (hdi);
}

void CPeripherals::InitSerialPortCombo(CWinComboBox& combo,CString strPort)
{
    CString strValue = CString(m_properties[strPort].Value());
    int i = combo.FindStringExact(0, GetPortFromInvariant(strValue));
    if (i == CB_ERR)
        if (strValue.GetLength()==0)
            combo.SetCurSel(0);
        else
           combo.SetWindowText(strValue);
    else
        combo.SetCurSel(i);
}

LRESULT CPeripherals::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&)
{
    CheckedAttach(IDC_SERIAL0,m_comboSerial0);
    CheckedAttach(IDC_BROWSESERIALPORTFILE0,m_btnBrowseSerial0File);
    CheckedAttach(IDC_SERIAL1,m_comboSerial1);
    CheckedAttach(IDC_BROWSESERIALPORTFILE1,m_btnBrowseSerial1File);
    CheckedAttach(IDC_SERIAL2,m_comboSerial2);
    CheckedAttach(IDC_BROWSESERIALPORTFILE2,m_btnBrowseSerial2File);
    CheckedAttach(IDC_CREATECONSOLE,m_checkCreateConsole);

    m_comboSerial0.AddString(m_strNone);
    m_comboSerial1.AddString(m_strNone);
    m_comboSerial2.AddString(m_strNone);

    EnumerateSerialPorts();

    InitSerialPortCombo(m_comboSerial0,g_strDSPropertySerialPort0);
    InitSerialPortCombo(m_comboSerial1,g_strDSPropertySerialPort1);
    InitSerialPortCombo(m_comboSerial2,g_strDSPropertySerialPort2);

    m_checkCreateConsole.SetCheck(m_properties[g_strDSPropertyCreateConsole].CheckedIfTrue());

    m_comboSerial0.ShowWindow(m_properties[g_strDSPropertySerialPort0].Visible());
    m_comboSerial1.ShowWindow(m_properties[g_strDSPropertySerialPort1].Visible());
    m_comboSerial2.ShowWindow(m_properties[g_strDSPropertySerialPort2].Visible());
    m_checkCreateConsole.ShowWindow(m_properties[g_strDSPropertyCreateConsole].Visible());
    m_checkCreateConsole.EnableWindow(m_properties[g_strDSPropertyCreateConsole].IsEnabled());

#ifdef NYI_SERIAL_VIA_FILE
    m_btnBrowseSerial0File.ShowWindow(false);
    m_btnBrowseSerial1File.ShowWindow(false);
    m_btnBrowseSerial2File.ShowWindow(false);
#else
    setAANameOnControl( IDS_BROWSE_FOR_SERIAL0, m_btnBrowseSerial0File.m_hWnd);
    setAANameOnControl( IDS_BROWSE_FOR_SERIAL1, m_btnBrowseSerial1File.m_hWnd);
    setAANameOnControl( IDS_BROWSE_FOR_SERIAL2, m_btnBrowseSerial2File.m_hWnd);
#endif

    return 1;  // Let the system set the focus
}

bool CPeripherals::BValidating()
{
    CString str0,str1,str2;

    m_comboSerial0.GetWindowText(str0);
    m_comboSerial1.GetWindowText(str1);
    m_comboSerial2.GetWindowText(str2);

    if ((str0==str1 || str0==str2) && str0!=m_strNone) {
        CDCPException msg(str0==str1 ? IDS_INVALID_SERIALPORT0_1 : IDS_INVALID_SERIALPORT0_2);
        msg.Display();
        m_comboSerial0.SetFocus();
        return false;
    }

    if (str1==str2 && str1!=m_strNone) {
        CDCPException msg(IDS_INVALID_SERIALPORT1_2);
        msg.Display();
        m_comboSerial1.SetFocus();
        return false;
    }
    return true;
}

void CPeripherals::CommitChanges()
{
    CString str;

    m_comboSerial0.GetWindowText(str);
    m_properties[g_strDSPropertySerialPort0].SetValue(GetInvariantFromPort(str));

    m_comboSerial1.GetWindowText(str);
    m_properties[g_strDSPropertySerialPort1].SetValue(GetInvariantFromPort(str));

    m_comboSerial2.GetWindowText(str);
    m_properties[g_strDSPropertySerialPort2].SetValue(GetInvariantFromPort(str));

    m_properties[g_strDSPropertyCreateConsole].SetValue(m_checkCreateConsole.GetCheck());
}
