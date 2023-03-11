/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef GENERAL_H
#define GENERAL_H

// General.h : Declaration of CGeneral

class CGeneral :
    public CCheckedDialog<CGeneral>, public ITabPage
{
public:
    CGeneral(PropertyMap& properties) : ITabPage(properties) { }
    int GetNameResourceID() { return IDS_EMULGENERALTAB; }
    CString GetHelpKeyword() { return HELPKEYWORD_VSD_CONFIGEMUGENERAL; }

    enum { IDD = IDD_EMCONFIGGENERALPAGE };

private:
BEGIN_MSG_MAP(CGeneral)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    COMMAND_HANDLER(IDC_BROWSEOSIMAGE, BN_CLICKED, OnClickedBrowseOSImage)
    COMMAND_HANDLER(IDC_SPECIFYADDRESS, BN_CLICKED, OnClickedSpecifyAddress)
    COMMAND_HANDLER(IDC_SPECIFYRAMSIZE, BN_CLICKED, OnClickedSpecifyRAMSize)
    COMMAND_HANDLER(IDC_BROWSEFLASHFILE, BN_CLICKED, OnClickedBrowseFlashFile)
    COMMAND_HANDLER(IDC_BROWSEFOLDERSHARE, BN_CLICKED, OnClickedBrowseFolderShare)

//    CHAIN_MSG_MAP_ALT(CDialogImpl<CGeneral>,0)
END_MSG_MAP()

    LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClickedBrowseOSImage(WORD, WORD, HWND, BOOL&)
        { if(m_editImage.IsWindowEnabled()) GetFileFromBrowse(L"",IDS_IMAGEFILEFILTER,m_editImage,m_hWnd); return 0; }
    LRESULT OnClickedSpecifyAddress(WORD, WORD, HWND, BOOL&) { SetImageAddressEnable(); return 0; }
    LRESULT OnClickedSpecifyRAMSize(WORD, WORD, HWND, BOOL&) { SetRAMSizeEnable(); return 0; }
    LRESULT OnClickedBrowseFlashFile(WORD, WORD, HWND, BOOL&)
        { if (m_editFlashFile.IsWindowEnabled()) GetFileFromBrowse(L"",IDS_FLASHFILEFILTER,m_editFlashFile,m_hWnd); return 0; }
    LRESULT OnClickedBrowseFolderShare(WORD, WORD, HWND, BOOL&)
        { if (m_editFolderShare.IsWindowEnabled()) { 
          GetDirectoryFromBrowse( L"", m_editFolderShare, m_hWnd);} return 0; }

    void SetImageAddressEnable() 
      { m_editImageAddress.EnableWindow(m_checkSpecifyAddress.GetCheck() && 
        m_properties[g_strDSPropertyImageAddress].IsEnabled()); }

    void SetRAMSizeEnable() 
      { m_editRAMSize.EnableWindow(m_checkSpecifyRAMSize.GetCheck() && 
        m_properties[g_strDSPropertyRAMSize].IsEnabled()); }

    void CommitChanges();
    bool BValidating();

    CWinEdit m_editImage;
    CWinButton m_checkSpecifyAddress;
    CWinEdit m_editImageAddress;
    CWinButton m_checkSpecifyRAMSize;
    CWinEdit m_editRAMSize;
    CWinEdit m_editFlashFile;
    CWinEdit m_editFolderShare;
    CWinComboBox m_comboHostKey;
    CWindow m_captionOSImage;
    CWinButton m_btnBrowseOSImage;
    CWindow m_captionFlashFile;
    CWinButton m_btnBrowseFlashFile;
    CWinButton m_btnBrowseFolderShare;
};

#endif // GENERAL_H
