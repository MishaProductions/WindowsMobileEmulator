/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef DISPLAY_H
#define DISPLAY_H

// Display.h : Declaration of CDisplay


#include "tabpage.h"

class CDisplay :
    public CCheckedDialog<CDisplay>, public ITabPage
{
public:
    CDisplay(PropertyMap& properties) : ITabPage(properties) { }
    int GetNameResourceID() { return IDS_EMULDISPLAYTAB; }
    CString GetHelpKeyword() { return HELPKEYWORD_VSD_CONFIGEMUDISPLAY; }

    enum { IDD = IDD_EMCONFIGDISPLAYPAGE };

BEGIN_MSG_MAP(CDisplay)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    COMMAND_HANDLER(IDC_BROWSESKIN, BN_CLICKED, OnClickedBrowseSkin)
    COMMAND_HANDLER(IDC_RADIOSKIN, BN_CLICKED, OnClickedRadioSkin)
    COMMAND_HANDLER(IDC_RADIOVIDEO, BN_CLICKED, OnClickedRadioVideo)
//    CHAIN_MSG_MAP_ALT(CDialogImpl<CDisplay>,0)
END_MSG_MAP()

    LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClickedBrowseSkin(WORD, WORD, HWND, BOOL&) { GetFileFromBrowse(L"",IDS_SKINFILEFILTER,m_editSkin,m_hWnd); return 0; }
    LRESULT OnClickedRadioSkin(WORD, WORD, HWND, BOOL&) { EnableVideo(false); return 0; }
    LRESULT OnClickedRadioVideo(WORD, WORD, HWND, BOOL&) { EnableVideo(true); return 0; }

    void EnableVideo(bool enabled)
    {
        m_editSkin.EnableWindow(!enabled);
        m_btnBrowseSkin.EnableWindow(!enabled);

        enabled=(enabled && m_properties[g_strDSPropertyScreenWidth].IsEnabled());

        m_captionWidth.EnableWindow(enabled);
        m_editWidth.EnableWindow(enabled);
        m_captionWidth2.EnableWindow(enabled);
        m_captionHeight.EnableWindow(enabled);
        m_editHeight.EnableWindow(enabled);
        m_captionHeight2.EnableWindow(enabled);
        m_captionColorDepth.EnableWindow(enabled);
        m_comboColorDepth.EnableWindow(enabled);
        m_captionColorDepth2.EnableWindow(enabled);
    }

    void CommitChanges();
    bool BValidating();

private:
    CWinButton m_radioSkin;
    CWinEdit m_editSkin;
    CWinButton m_btnBrowseSkin;
    CWinButton m_radioVideo;
    CWindow m_captionWidth;
    CWinEdit m_editWidth;
    CWindow m_captionWidth2;
    CWindow m_captionHeight;
    CWinEdit m_editHeight;
    CWindow m_captionHeight2;
    CWindow m_captionColorDepth;
    CWinComboBox m_comboColorDepth;
    CWindow m_captionColorDepth2;
    CWinComboBox m_comboOrientation;
    CWinButton m_checkZoom2x;
    CWinButton m_checkAlwaysOnTop;
    CWinButton m_checkEnableTooltips;
    CWinEdit m_editFolderShare;
};

#endif // DISPLAY_H
