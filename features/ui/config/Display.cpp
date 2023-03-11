/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

// Display.cpp : Implementation of CDisplay

#include "stdafx.h"

LRESULT CDisplay::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&)
{
    CheckedAttach(IDC_RADIOSKIN,m_radioSkin);
    CheckedAttach(IDC_SKINNAME,m_editSkin);
    CheckedAttach(IDC_BROWSESKIN,m_btnBrowseSkin);
    CheckedAttach(IDC_RADIOVIDEO,m_radioVideo);
    CheckedAttach(IDC_CAPTIONWIDTH,m_captionWidth);
    CheckedAttach(IDC_SCREENWIDTH,m_editWidth);
    CheckedAttach(IDC_CAPTIONWIDTH2,m_captionWidth2);
    CheckedAttach(IDC_CAPTIONHEIGHT,m_captionHeight);
    CheckedAttach(IDC_SCREENHEIGHT,m_editHeight);
    CheckedAttach(IDC_CAPTIONHEIGHT2,m_captionHeight2);
    CheckedAttach(IDC_CAPTIONCOLORDEPTH,m_captionColorDepth);
    CheckedAttach(IDC_COLORDEPTH,m_comboColorDepth);
    CheckedAttach(IDC_CAPTIONCOLORDEPTH2,m_captionColorDepth2);
    CheckedAttach(IDC_ORIENTATION,m_comboOrientation);
    CheckedAttach(IDC_ZOOM2X,m_checkZoom2x);
    CheckedAttach(IDC_ALWAYSONTOP,m_checkAlwaysOnTop);
    CheckedAttach(IDC_ENABLETOOLTIPS,m_checkEnableTooltips);

    m_editWidth.LimitText(5);
    m_editHeight.LimitText(5);
    m_editSkin.LimitText(256);

    bool UsingSkin = m_properties[g_strDSPropertyShowSkin].IsTrue();
    m_editSkin.SetWindowText(m_properties[g_strDSPropertySkin].Value());
    m_editSkin.ShowWindow(m_properties[g_strDSPropertySkin].Visible());
    m_btnBrowseSkin.ShowWindow(m_properties[g_strDSPropertySkin].Visible());
    setAANameOnControl( IDS_BROWSE_FOR_SKIN, m_btnBrowseSkin.m_hWnd);
    m_radioSkin.SetCheck(UsingSkin);
    m_radioVideo.SetCheck(!UsingSkin);
    m_radioSkin.ShowWindow(m_properties[g_strDSPropertyShowSkin].Visible());
    m_radioVideo.ShowWindow(m_properties[g_strDSPropertyShowSkin].Visible());
    m_editWidth.SetWindowText(m_properties[g_strDSPropertyScreenWidth].Value());
    m_editWidth.EnableWindow(m_properties[g_strDSPropertyScreenWidth].IsEnabled());
    m_captionWidth.ShowWindow(m_properties[g_strDSPropertyScreenWidth].Visible());
    m_captionWidth2.ShowWindow(m_properties[g_strDSPropertyScreenWidth].Visible());
    m_editHeight.SetWindowText(m_properties[g_strDSPropertyScreenHeight].Value());
    m_editHeight.EnableWindow(m_properties[g_strDSPropertyScreenHeight].IsEnabled());
    m_captionHeight.ShowWindow(m_properties[g_strDSPropertyScreenHeight].Visible());
    m_captionHeight2.ShowWindow(m_properties[g_strDSPropertyScreenHeight].Visible());
    CString strColorDepth = m_properties[g_strDSPropertyColorDepth].Value();
    if (strColorDepth==L"")
        strColorDepth=g_strDSDefaultColorDepth;
    m_comboColorDepth.AddString(L"16");
    m_comboColorDepth.AddString(L"24");
    m_comboColorDepth.AddString(L"32");
    m_comboColorDepth.EnableWindow(m_properties[g_strDSPropertyColorDepth].IsEnabled());
    IfFalseThrow(m_comboColorDepth.SelectString(-1,strColorDepth)!=LB_ERR,E_FAIL);
    m_captionColorDepth.ShowWindow(m_properties[g_strDSPropertyColorDepth].Visible());
    m_captionColorDepth2.ShowWindow(m_properties[g_strDSPropertyColorDepth].Visible());
    CString strOrientation = m_properties[g_strDSPropertyOrientation].Value();
    if (strOrientation==L"")
        strOrientation=g_strDSDefaultOrientation;
    m_comboOrientation.AddString(L"0");
    m_comboOrientation.AddString(L"90");
    m_comboOrientation.AddString(L"180");
    m_comboOrientation.AddString(L"270");
    IfFalseThrow(m_comboOrientation.SelectString(-1,strOrientation)!=LB_ERR,E_FAIL);
    m_comboOrientation.ShowWindow(m_properties[g_strDSPropertyOrientation].Visible());
    m_checkZoom2x.SetCheck(m_properties[g_strDSPropertyZoom2x].CheckedIfTrue());
    m_checkZoom2x.ShowWindow(m_properties[g_strDSPropertyZoom2x].Visible());
    m_checkAlwaysOnTop.SetCheck(m_properties[g_strDSPropertyAlwaysOnTop].CheckedIfTrue());
    m_checkAlwaysOnTop.ShowWindow(m_properties[g_strDSPropertyAlwaysOnTop].Visible());
    int enableToolTips;
    if (m_properties.find(g_strDSPropertyEnableToolTips)==m_properties.end())
        enableToolTips = BST_CHECKED;
    else
        enableToolTips = m_properties[g_strDSPropertyEnableToolTips].CheckedIfTrue();
    m_checkEnableTooltips.SetCheck(enableToolTips);
    m_checkEnableTooltips.ShowWindow(m_properties[g_strDSPropertyEnableToolTips].Visible());

    EnableVideo(!UsingSkin);

    return 0;  // Let the system set the focus
}

void CDisplay::CommitChanges()
{
    CString str;

    // Save values in DataStore

    m_editSkin.GetWindowText(str);
    m_properties[g_strDSPropertySkin].SetValue(str);
    m_properties[g_strDSPropertyShowSkin].SetValue(m_radioSkin.GetCheck());
    m_editWidth.GetWindowText(str);
    m_properties[g_strDSPropertyScreenWidth].SetValue(str);
    m_editHeight.GetWindowText(str);
    m_properties[g_strDSPropertyScreenHeight].SetValue(str);
    m_comboColorDepth.GetWindowText(str);
    m_properties[g_strDSPropertyColorDepth].SetValue(str);
    m_comboOrientation.GetWindowText(str);
    m_properties[g_strDSPropertyOrientation].SetValue(str);
    m_properties[g_strDSPropertyZoom2x].SetValue(m_checkZoom2x.GetCheck());
    m_properties[g_strDSPropertyAlwaysOnTop].SetValue(m_checkAlwaysOnTop.GetCheck());
    m_properties[g_strDSPropertyEnableToolTips].SetValue(m_checkEnableTooltips.GetCheck());
}

bool CDisplay::BValidating()
{
    if (m_radioVideo.GetCheck()) {
        CString str;

        m_editWidth.GetWindowText(str);
        int i = _wtoi(str);
        if (i<64 || i>800 || i%2 != 0) {
            CDCPException msg(IDS_INVALID_SCREENWIDTH);
            msg.Display();
            m_editWidth.SetFocus();
            return false;
        }

        m_editHeight.GetWindowText(str);
        i = _wtoi(str);
        if (i<64 || i>800 || i%2 != 0) {
            CDCPException msg(IDS_INVALID_SCREENHEIGHT);
            msg.Display();
            m_editHeight.SetFocus();
            return false;
        }
    }
    else {
        CString strPath;
        m_editSkin.GetWindowText(strPath);
        if (strPath==L"")
        {
            CDCPException msg(IDS_MISSING_SKINFILE);
            msg.Display();
            m_editSkin.SetFocus();
            return false;
        }
        // Check if that file exists and pop up warning dialog otherwise
        FILE * skinFile = NULL; 
        if ( _wfopen_s( &skinFile, strPath, L"r" ) )
        {
            if (!fIsRunning)
            {
                // In the not running scenario ask the user if they want to keep the name
                wchar_t Message[MAX_LOADSTRING];

                if (Configuration.Resources.getString(IDS_NONEXISTANT_SKINFILE, Message))
                {
                    int Result = ::MessageBox((WinCtrlInstance ? (
                                            WinCtrlInstance->GetWincontrollerHWND()) : NULL),
                                            Message, EMULATOR_NAME_W, MB_YESNO);
                    if ( Result == IDYES)
                    {
                        m_editSkin.SetFocus();
                        return false;
                    }
                }
            }
            else
            {
                CDCPException msg(IDS_MISSING_SKINFILE);
                msg.Display();
                m_editSkin.SetFocus();
                return false;
            }
        }
        else
            fclose(skinFile);
    }

    return true;
}
