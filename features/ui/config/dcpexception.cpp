/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

// DCPException.cpp : implementation of CDCPException

#include "stdafx.h"

// CDCPException implementation
//
// This class basically converts between various error formats:
// Inputs:
//     HRESULT
//     C++ exception
//     string
//     localized string (resouce ID)
//     nothing (i.e. we have no information about the error)
// Ouputs:
//     Localised (if possible) dialog presented to user
//     HRESULT which can be passed back through COM
//
// It is designed to be thrown as an exception.
//
// Note that the constructors for HRESULT and resource ID do not currently
// clash because HRESULT works out to be "long" and resource ID is an int.
// However, if a future version of the Windows SDK implements HRESULT as an
// int, this class will fail to compile and will need to be changed.

// This helper function attempts to get a string from a resource.
void CDCPException::InitMessage(int i)
{
    if (!m_strMsg.LoadString(i)) {
        // Can't localize this message because we can't load localized
        // strings.
        m_strMsg=L"An internal error has occurred. Localized string resources "
                 L"could not be loaded.\n";
    }
}

// Construct a default exception
CDCPException::CDCPException()
  : m_hr(E_FAIL)
{
    InitMessage(IDS_EXCEPTION);
}

// Construct an exception from an HRESULT
CDCPException::CDCPException(HRESULT hr)
  : m_hr(hr)
{
    InitMessage(IDS_EXCEPTION);
    CString strErr;
    if (strErr.LoadString(IDS_ERRORCODE))
        m_strMsg+=strErr;
    strErr.Format(L"0x%08x",hr);
    m_strMsg+=strErr;
}

// Construct an exception from a C++ standard library exception
CDCPException::CDCPException(const std::exception& serr)
{
    try {
        throw serr;
    }
    catch (std::bad_alloc&) {
        InitMessage(IDS_OUTOFMEMORY);
        m_hr=E_OUTOFMEMORY;
    }
    catch (std::exception& e) {
        m_strMsg=e.what();
        m_hr=E_FAIL;
    }
}

// Alert the user that something went wrong
void CDCPException::Display() const
{
    // This function could fail, but what could we possibly do about it?
    CString strTitle;
    strTitle.LoadString(IDS_ERRORBOXTITLE);
    MessageBox(GetActiveWindow(), m_strMsg, strTitle,
               MB_ICONEXCLAMATION | MB_OK);
}
