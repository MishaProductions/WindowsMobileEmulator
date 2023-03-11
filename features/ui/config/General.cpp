/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

// General.cpp : Implementation of CGeneral
#include "stdafx.h"

LRESULT CGeneral::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&)
{
    CheckedAttach(IDC_IMAGENAME,m_editImage);
    CheckedAttach(IDC_SPECIFYADDRESS,m_checkSpecifyAddress);
    CheckedAttach(IDC_IMAGEADDRESS,m_editImageAddress);
    CheckedAttach(IDC_SPECIFYRAMSIZE,m_checkSpecifyRAMSize);
    CheckedAttach(IDC_RAMSIZE,m_editRAMSize);
    CheckedAttach(IDC_FLASHFILE,m_editFlashFile);
    CheckedAttach(IDC_HOSTKEY,m_comboHostKey);
    CheckedAttach(IDC_CAPTIONOSIMAGE,m_captionOSImage);
    CheckedAttach(IDC_BROWSEOSIMAGE,m_btnBrowseOSImage);
    CheckedAttach(IDC_CAPTIONFLASHFILE,m_captionFlashFile);
    CheckedAttach(IDC_BROWSEFLASHFILE,m_btnBrowseFlashFile);
    CheckedAttach(IDC_SHAREDFOLDERNAME,m_editFolderShare);
    CheckedAttach(IDC_BROWSEFOLDERSHARE,m_btnBrowseFolderShare);

    m_editRAMSize.LimitText(4);

    m_editImage.SetWindowText(m_properties[g_strDSPropertyOSBinImage].Value());
    m_editImage.ShowWindow(m_properties[g_strDSPropertyOSBinImage].Visible());
    m_editImage.EnableWindow(m_properties[g_strDSPropertyOSBinImage].IsEnabled());
    m_captionOSImage.EnableWindow(m_properties[g_strDSPropertyOSBinImage].IsEnabled());
    m_btnBrowseOSImage.EnableWindow(m_properties[g_strDSPropertyOSBinImage].IsEnabled());
    m_checkSpecifyAddress.SetCheck(m_properties[g_strDSPropertySpecifyAddress].CheckedIfTrue());
    m_checkSpecifyAddress.ShowWindow(m_properties[g_strDSPropertySpecifyAddress].Visible());
    m_checkSpecifyAddress.EnableWindow(m_properties[g_strDSPropertySpecifyAddress].IsEnabled());
    m_editImageAddress.SetWindowText(m_properties[g_strDSPropertyImageAddress].Value());
    m_editImageAddress.ShowWindow(m_properties[g_strDSPropertyImageAddress].Visible());
    m_editImageAddress.EnableWindow(m_properties[g_strDSPropertyImageAddress].IsEnabled());
    m_checkSpecifyRAMSize.SetCheck(m_properties[g_strDSPropertySpecifyRAMSize].CheckedIfTrue());
    m_checkSpecifyRAMSize.ShowWindow(m_properties[g_strDSPropertySpecifyRAMSize].Visible());
    m_checkSpecifyRAMSize.EnableWindow(m_properties[g_strDSPropertySpecifyRAMSize].IsEnabled());
    m_editRAMSize.SetWindowText(m_properties[g_strDSPropertyRAMSize].Value());
    m_editRAMSize.ShowWindow(m_properties[g_strDSPropertyRAMSize].Visible());
    m_editRAMSize.EnableWindow(m_properties[g_strDSPropertyRAMSize].IsEnabled());
    m_editFlashFile.SetWindowText(m_properties[g_strDSPropertyFlashFile].Value());
    m_editFlashFile.ShowWindow(m_properties[g_strDSPropertyFlashFile].Visible());
    m_editFlashFile.EnableWindow(m_properties[g_strDSPropertyFlashFile].IsEnabled());
    m_captionFlashFile.EnableWindow(m_properties[g_strDSPropertyFlashFile].IsEnabled());
    m_btnBrowseFlashFile.EnableWindow(m_properties[g_strDSPropertyFlashFile].IsEnabled());
    m_comboHostKey.ShowWindow(m_properties[g_strDSPropertyHostKey].Visible());
    m_editFolderShare.SetWindowText(m_properties[g_strDSPropertyFolderShare].Value());
    m_editFolderShare.ShowWindow(m_properties[g_strDSPropertyFolderShare].Visible());
    m_editFolderShare.EnableWindow(m_properties[g_strDSPropertyFolderShare].IsEnabled());

    setAANameOnControl( IDS_BROWSE_FOR_FLASH, m_btnBrowseFlashFile.m_hWnd);
    setAANameOnControl( IDS_BROWSE_FOR_OSIMAGE, m_btnBrowseOSImage.m_hWnd);
    setAANameOnControl( IDS_BROWSE_FOR_FOLDERSHARE, m_btnBrowseFolderShare.m_hWnd);

    int nval = _wtoi(m_properties[g_strDSPropertyHostKey].Value());

    // load hardcoded choices for host keys from resource

    for (int i=0;i<ARRAY_SIZE(HostKeys);++i) {
        CString str;
        str.LoadString(HostKeys[i].ids);
        int iCurrent=m_comboHostKey.AddString(str);
        m_comboHostKey.SetItemData(iCurrent,HostKeys[i].Value);
        if (HostKeys[i].Value == nval)
            m_comboHostKey.SetCurSel(iCurrent);
    }
    if (m_comboHostKey.GetCurSel()==LB_ERR)
        m_comboHostKey.SetCurSel(0);

    SetImageAddressEnable();
    SetRAMSizeEnable();

    return 1;  // Let the system set the focus
}

bool CGeneral::BValidating()
{
    if ( m_checkSpecifyAddress.GetCheck())
    {
        CString address;
        m_editImageAddress.GetWindowText(address);
        // Must specify an address if
        if ( address == L"" )
        {
            CDCPException msg(IDS_MISSING_IMAGEADDRESS);
            msg.Display();
            m_editRAMSize.SetFocus();
            return false;
        }

        wchar_t * end_scan = NULL;
        unsigned long value = wcstoul(address, &end_scan, 0x10);
        long neg_value = wcstol(address, NULL, 0x10);
        if ( ( end_scan == NULL || *end_scan != 0)   || // Non-numeric value
               value == ULONG_MAX && errno == ERANGE || // Overflow
               neg_value < 0 )                          // Negative input
        {
            CDCPException msg(IDS_INVALID_IMAGEADDRESS);
            msg.Display();
            m_editImageAddress.SetFocus();
            return false;
        }
    }

    if ( m_checkSpecifyRAMSize.GetCheck())
    {
        CString RAMSize;
        m_editRAMSize.GetWindowText(RAMSize);
        // Must specify an address if
        if ( RAMSize == L"" )
        {
            CDCPException msg(IDS_MISSING_MEMSIZE);
            msg.Display();
            m_editRAMSize.SetFocus();
            return false;
        }

        wchar_t * end_scan = NULL;
        unsigned long value = wcstoul(RAMSize, &end_scan, 10);
        long neg_value = wcstol(RAMSize, NULL, 10);
        if ( ( end_scan == NULL || *end_scan != 0)   || // Non-numeric value
               value == ULONG_MAX && errno == ERANGE || // Overflow
               neg_value < 0 ||                         // Negative input
               value < 64 || value > 256 )              // Size that are not supported
        {
            CDCPException msg(IDS_INVALID_MEMSIZE);
            msg.Display();
            m_editRAMSize.SetFocus();
            return false;
        }
    }

    if ( m_properties[g_strDSPropertyOSBinImage].IsEnabled() )
    {
        CString strPath;
        m_editImage.GetWindowText(strPath);
        if (strPath==L"")
        {
            CDCPException msg(IDS_MISSING_IMAGEFILE);
            msg.Display();
            m_editImage.SetFocus();
            return false;
        }

        // Check if that file exists and pop up warning dialog otherwise
        FILE * imageFile = NULL;
        if ( _wfopen_s( &imageFile, strPath, L"r" ) )
        {
            wchar_t Message[MAX_LOADSTRING];

            if (Configuration.Resources.getString(IDS_NONEXISTANT_IMAGEFILE, Message))
            {
                int Result = ::MessageBox((WinCtrlInstance ? (WinCtrlInstance->GetWincontrollerHWND()) : NULL),
                                       Message, EMULATOR_NAME_W, MB_YESNO);
                if ( Result == IDYES)
                {
                    m_editImage.SetFocus();
                    return false;
                }
            }
        }
        else
            fclose(imageFile);
    }

    return true;
}

void CGeneral::CommitChanges()
{
    CString str;
    int i = m_comboHostKey.GetItemData(m_comboHostKey.GetCurSel());
    IfNegativeThrow(i,E_FAIL);

    str.Format(L"%i",i);
    m_properties[g_strDSPropertyHostKey].SetValue(str);

    m_editImage.GetWindowText(str);
    m_properties[g_strDSPropertyOSBinImage].SetValue(str);
    m_properties[g_strDSPropertySpecifyAddress].SetValue(m_checkSpecifyAddress.GetCheck());
    m_editImageAddress.GetWindowText(str);
    m_properties[g_strDSPropertyImageAddress].SetValue(str);
    bool specifyRAMSize = m_checkSpecifyRAMSize.GetCheck();
    m_properties[g_strDSPropertySpecifyRAMSize].SetValue(specifyRAMSize);
    if (specifyRAMSize) {
        m_editRAMSize.GetWindowText(str);
        m_properties[g_strDSPropertyRAMSize].SetValue(str);
    }
    m_editFlashFile.GetWindowText(str);
    m_properties[g_strDSPropertyFlashFile].SetValue(str);
    m_editFolderShare.GetWindowText(str);
    m_properties[g_strDSPropertyFolderShare].SetValue(str);
}
