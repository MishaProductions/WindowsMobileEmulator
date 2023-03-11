/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef TABPAGE_H
#define TABPAGE_H

// TabPage.h : Declaration of ITabPage

// Virtual base class so we can treat the tab pages polymorphically
class ITabPage
{
public:
    ITabPage(PropertyMap& properties) : m_properties(properties) { }
    virtual ~ITabPage() { }
    virtual int GetNameResourceID() = 0;
    virtual CString GetHelpKeyword() = 0;
    virtual bool BValidating() = 0;
    virtual void CommitChanges() = 0;
    virtual void Reset() { }
    virtual void Hide() = 0;
    virtual void Show(const RECT& rc) = 0;
    virtual HWND Init(HWND hWndParent) = 0;
    virtual void Destroy() = 0;

protected:
    PropertyMap& m_properties;
};

// Helper class for each type of page - these implement Hide(), Show() and
// Init(), which need to know the type of dialog they operate on. Note that
// the template parameter must inherit from ITabPage, but should not implement
// these three functions.
template<class T> class CTabPageImpl : public T
{
public:
    CTabPageImpl(PropertyMap& properties)
      : T(properties) { }
    void Hide() { ShowWindow(SW_HIDE); }
    void Show(const RECT& rc)
    {
        ShowWindow(SW_SHOWNA);
        MoveWindow(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, FALSE);
        SetWindowPos(HWND_TOPMOST, 0,0,0,0, SWP_NOSIZE | SWP_NOMOVE);
    }
    HWND Init(HWND hWndParent)
    {
        return Create(hWndParent);
    }
    void Destroy()
    {
        DestroyWindow();
    }
};

#endif // TABPAGE_H
