/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "stdafx.h"

void GetFileFromBrowse(const CString& strStartPath,int idsFilter,CWindow& winPath,HWND hwndOwner)
{
    CString strFilter;
    IfFalseThrow(strFilter.LoadString(idsFilter)!=0,E_FAIL);
    strFilter+=CString(L"|");

    CComBSTR bstrFilter(strFilter);
    for (int i=0;i<strFilter.GetLength();++i)
        if (bstrFilter[i]==L'|')
            bstrFilter[i]=L'\0';

    // Browse for a file
    OPENFILENAME openFileName;
    wchar_t szBuf[MAX_PATH];

    memset((void *)&openFileName, 0, sizeof(OPENFILENAME));
    openFileName.lStructSize = sizeof(OPENFILENAME);
    openFileName.hwndOwner = hwndOwner;
//    openFileName.hInstance = ;
    openFileName.lpstrFilter = bstrFilter.m_str;
    openFileName.lpstrCustomFilter = NULL;
//    openFileName.nMaxCustFilter = ;
    openFileName.nFilterIndex = 0;
    szBuf[0] = NULL;
    openFileName.lpstrFile = szBuf;
    openFileName.nMaxFile = MAX_PATH;
    openFileName.lpstrFileTitle = NULL;
//    openFileName.nMaxFileTitle = ;
    openFileName.lpstrInitialDir = strStartPath.IsEmpty() ? NULL : (LPCTSTR)strStartPath;
    openFileName.lpstrTitle = NULL;
    openFileName.Flags = OFN_NONETWORKBUTTON;
//    openFileName.nFileOffset = ;
//    openFileName.nFileExtension = ;
    openFileName.lpstrDefExt = NULL;
    openFileName.lpfnHook = NULL;
//    openFileName.lpTemplateName = ;
//    openFileName.pvReserved = NULL;
//    openFileName.dwReserved = 0;
//    openFileName.FlagsEx = 0

    // GetOpenFileName only succeeds from an STA on Win2k,
    // so we need to verify that the current model is STA
    HRESULT hr_ComInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr_ComInit)) {
        goto Exit;
    }

    if (GetOpenFileName(&openFileName)!=0)
        winPath.SetWindowText(szBuf);

Exit:
    if (SUCCEEDED(hr_ComInit)) {
        CoUninitialize();
    }
}

#include <shlobj.h>

// Retrieves the UIObject interface for the specified full PIDL
STDAPI SHGetUIObjectFromFullPIDL(LPCITEMIDLIST pidl, HWND hwnd, REFIID riid, void **ppv)
{
    LPCITEMIDLIST pidlChild;
    IShellFolder* psf;

    *ppv = NULL;

    HRESULT hr = SHBindToParent(pidl, IID_IShellFolder, (void**)&psf, &pidlChild);
    if (SUCCEEDED(hr))
    {
        hr = psf->GetUIObjectOf(hwnd, 1, &pidlChild, riid, NULL, ppv);
        psf->Release();
    }
    return hr;
}

#define ILSkip(pidl, cb)       ((LPITEMIDLIST)(((BYTE*)(pidl))+cb))
#define ILNext(pidl)           ILSkip(pidl, (pidl)->mkid.cb)

HRESULT SHILClone(LPCITEMIDLIST pidl, LPITEMIDLIST *ppidl)
{
    DWORD cbTotal = 0;

    if (pidl)
    {
        LPCITEMIDLIST pidl_temp = pidl;
        cbTotal += sizeof (pidl_temp->mkid.cb);

        while (pidl_temp->mkid.cb)
        {
            cbTotal += pidl_temp->mkid.cb;
            pidl_temp = ILNext(pidl_temp);
        }
    }

    *ppidl = (LPITEMIDLIST)CoTaskMemAlloc(cbTotal);

    if (*ppidl)
        CopyMemory(*ppidl, pidl, cbTotal);

    return  *ppidl ? S_OK: E_OUTOFMEMORY;
}

// Get the target PIDL for a folder PIDL. This deals with cases where a folder
// is an alias to a real folder, folder shortcuts, etc.
STDAPI SHGetTargetFolderIDList(LPCITEMIDLIST pidlFolder, LPITEMIDLIST *ppidl)
{
    IShellLink *psl;

    *ppidl = NULL;

    HRESULT hr = SHGetUIObjectFromFullPIDL(pidlFolder, NULL, IID_IShellLink, (void **)&psl);

    if (SUCCEEDED(hr))
    {
        hr = psl->GetIDList(ppidl);
        psl->Release();
    }

    // It's not a folder shortcut so get the PIDL normally.
    if (FAILED(hr))
        hr = SHILClone(pidlFolder, ppidl);

    return hr;
}

// Get the target folder for a folder PIDL. This deals with cases where a folder
// is an alias to a real folder, folder shortcuts, the My Documents folder, etc.
STDAPI SHGetTargetFolderPath(LPCITEMIDLIST pidlFolder, __out_ecount(cchpath) LPWSTR pszPath, UINT cchPath)
{
    ASSERT( cchPath >= MAX_PATH && pszPath != NULL );

    LPITEMIDLIST pidlTarget;
    *pszPath = 0;

    HRESULT hr = SHGetTargetFolderIDList(pidlFolder, &pidlTarget);

    if (SUCCEEDED(hr) && pidlTarget != NULL)
    {
        SHGetPathFromIDListW(pidlTarget, pszPath);   // Make sure it is a path
        CoTaskMemFree(pidlTarget);
    }

    return *pszPath ? S_OK : E_FAIL;
}

void GetDirectoryFromBrowse( const CString& strStartPath,  CWindow& winPath, HWND hwndOwner)
{
    BROWSEINFO browse_info;
    LPITEMIDLIST sel_root = NULL, selection = NULL;
    HRESULT hr_ComInit = E_FAIL;
    wchar_t szBuf[MAX_PATH];

    // Get the caption for the dialog
    wchar_t dialogCaption[MAX_LOADSTRING];
    if (!Configuration.Resources.getString(IDS_BROWSE_DIRECTORY_CAPTION, dialogCaption))
    {
        ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
        goto Exit;
    }

    // Convert the start path to a PILD if one was provided
    if (!strStartPath.IsEmpty())
        sel_root = ILCreateFromPathW(strStartPath);

    // Initialize the browse info structure
    memset( (void *)&browse_info, 0, sizeof(BROWSEINFO));
    browse_info.hwndOwner = hwndOwner;
    browse_info.pszDisplayName = szBuf;
    browse_info.pidlRoot = sel_root;
    browse_info.lpszTitle = dialogCaption;

    // SHBrowseForFolder only succeeds from an STA on Win2k,
    // so we need to verify that the current model is STA
    hr_ComInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr_ComInit)) {
        goto Exit;
    }

    // Call the shell API and convert returned PIDL into a path
    selection = SHBrowseForFolder(&browse_info);
    if ( selection != NULL && SUCCEEDED(SHGetTargetFolderPath(selection, szBuf, MAX_PATH)) )
        winPath.SetWindowText(szBuf);

Exit:
    if (selection != NULL)
        CoTaskMemFree(selection);
    if (sel_root != NULL)
        ILFree(sel_root);
    if (SUCCEEDED(hr_ComInit))
        CoUninitialize();
}

void setAANameOnControl(DWORD NameID, HWND hwnd)
{
    CComPtr<IAccPropServices> pAccPropSvc;
    HRESULT hr = CoCreateInstance( CLSID_AccPropServices, NULL,
          CLSCTX_INPROC_SERVER, IID_IAccPropServices, (void **)&pAccPropSvc );

    if (SUCCEEDED(hr)) {
        CComBSTR bstrAAName;

        IfZeroThrow(bstrAAName.LoadString(NameID),E_FAIL);
        IfErrorThrow(pAccPropSvc->SetHwndPropStr(
            hwnd,
            (DWORD)OBJID_CLIENT,
            CHILDID_SELF,
            PROPID_ACC_NAME,
            bstrAAName));
    }
}

