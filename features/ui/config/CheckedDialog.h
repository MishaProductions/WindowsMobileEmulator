/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef CHECKEDDIALOG_H
#define CHECKEDDIALOG_H

// CCheckedDialog
//
// Dialogs should inherit from this template instead of from CVsDialog as this
// will provide some extra member functions.

#include "dcpexception.h"

template<class TBase=CWindow> class CCheckedDialog : public CDialogImpl<TBase>
{
public:
    // CheckedAttach : attaches a dialog item to a CWindow
    // Parameters:
    //   id     - the id of the item we want to attach
    //   child  - the window that will be responsible for the item
    //
    // AttachDlgItem throws an exception if the operation cannot be completed
    // successfully. It therefore does not require a return value.
    // Callers should be careful not to let exceptions cross COM boundaries.
    void CheckedAttach(int id,CWindow& child)
    {
        CWindow item=GetDlgItem(id);
        IfNullThrow(static_cast<HWND>(item),E_POINTER);
        child.Attach(item);
    }

    // Override the DialogProc so we catch exceptions before they hit Windows code.
    DLGPROC GetDialogProc() { return CatcherDialogProc; }

    static INT_PTR CALLBACK CatcherDialogProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        INT_PTR result;
        DCPTRY {
            result=DialogProc(hWnd,uMsg,wParam,lParam);
        }
        DCPCATCH(CDCPException& err) {
            err.Display();
            result=FALSE;
        }
        return result;
    }
};

#endif // CHECKEDDIALOG_H
