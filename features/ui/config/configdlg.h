/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef CONFIGDLG_H
#define CONFIGDLG_H

#include "General.h"
#include "Display.h"
#include "Network.h"
#include "Peripherals.h"

extern bool ShowHelp( __inout_z wchar_t * buffer );

class CConfigDialog :
    public CCheckedDialog<CConfigDialog>
{
public:
    CConfigDialog(PropertyMap& properties, bool dialog_only)
      : m_iSel(0), result(0), m_dialog_only(dialog_only), m_properties(properties)
    {
        try {
            m_pages.push_back(new CTabPageImpl<CGeneral>(properties));
            m_pages.push_back(new CTabPageImpl<CDisplay>(properties));
            m_pages.push_back(new CTabPageImpl<CNetwork>(properties));
            m_pages.push_back(new CTabPageImpl<CPeripherals>(properties));
        }
        catch(...) {
            DeletePages();
            throw;
        }
    }
    ~CConfigDialog()
    {
        DeletePages();
    }

    int SimulateModal(HWND hWnd);

    enum { IDD = IDD_EMULCONFIG };

private:

BEGIN_MSG_MAP(CConfigDialog)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    MESSAGE_HANDLER(WM_SYSCOMMAND, OnSysCommand)
    MESSAGE_HANDLER(WM_HELP, OnHelp)
    COMMAND_HANDLER(IDOK, BN_CLICKED, OnClickedOK)
    COMMAND_HANDLER(IDCANCEL, BN_CLICKED, OnClickedCancel)
    COMMAND_ID_HANDLER(ID_CTRLTAB, OnCtrlTab)
    COMMAND_ID_HANDLER(ID_CTRLSHIFTTAB, OnCtrlShiftTab)
    NOTIFY_HANDLER(IDC_TAB, TCN_SELCHANGE, OnTcnSelchangeTab)
END_MSG_MAP()

    void DeletePages()
    {
        // Delete any pages that were successfully created
        for (std::vector<ITabPage*>::const_iterator i=m_pages.begin();i!=m_pages.end();++i)
            delete *i;
    }

    LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClickedOK(WORD, WORD, HWND, BOOL&);
    LRESULT OnClickedCancel(WORD, WORD, HWND, BOOL&);
    LRESULT OnClickedHelp(WORD, WORD, HWND, BOOL&)
    {
        if(!m_strF1Keyword.IsEmpty())
        {
            // Try to use a keyword to get a more precise display
            unsigned int length = m_strF1Keyword.GetLength();
            wchar_t * Buffer = new wchar_t[length + 1];
            if ( Buffer != NULL )
            {
                if (FAILED(StringCchPrintfW(Buffer, length + 1, L"%s", m_strF1Keyword.GetBuffer()))) 
                {
                    ASSERT(FALSE);
                    delete [] Buffer;
                }
                else
                {
                    if ( m_dialog_only ) {
                        ShowHelp( Buffer );
                    } else {
                        // post specific request to the main emulator window (this will delete Buffer)
                        ::PostMessage(::GetParent(m_hWnd), WM_COMMAND, MAKEWPARAM(ID_HELP_CONTENTS, 0), (LPARAM)Buffer);
                    }
                    return 0;
                }
            }
        }
        if ( m_dialog_only ) {
            ShowHelp( NULL );
        } else {
            // post generic request to the main emulator window
            ::PostMessage(::GetParent(m_hWnd), WM_COMMAND, MAKEWPARAM(ID_HELP_CONTENTS, 0), (LPARAM)NULL);
        }
        return 0;
    }
    LRESULT OnCtrlTab(WORD, WORD, HWND, BOOL&);
    LRESULT OnCtrlShiftTab(WORD, WORD, HWND, BOOL&);
    LRESULT OnTcnSelchangeTab(int, LPNMHDR, BOOL&);
    LRESULT OnSysCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        if ((wParam&0xfff0) == SC_CONTEXTHELP)
            OnClickedHelp(0,0,0,bHandled);
        else
            bHandled=FALSE;
        return 0;
    }
    LRESULT OnHelp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
    {
        BOOL fRet;

        return OnClickedHelp((WORD)0, WORD(0), HWND(0), fRet);
    }
    void ChangeTab(int iTo);
    void DestroyTabs();
    bool CommitChanges();

    CWinTab m_ctrlTab;
    CString m_strF1Keyword;

    std::vector<ITabPage*> m_pages;
    int m_iSel,m_tabs,result;
    bool m_dialog_only;

    HACCEL m_hAccel; // accelerator table

    PropertyMap& m_properties;
};

#endif // CONFIGDLG_H
