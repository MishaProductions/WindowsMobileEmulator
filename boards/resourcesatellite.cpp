/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "ResourceSatellite.h"
#include "heap.h"

ResourceSatellite::ResourceSatellite()
{
    InitializeCriticalSection(&ResourceLock);
    m_hSatDLL = GetModuleHandle(NULL);
    m_UILanguage = DEFAULT_LANGUAGE;
    setLanguage(GetUserDefaultUILanguage());
}

ResourceSatellite::~ResourceSatellite()
{
    DeleteCriticalSection(&ResourceLock);
}

bool ResourceSatellite::getString(unsigned int stringID, __out_ecount(MAX_LOADSTRING) wchar_t * stringBuffer)
{
    // The buffer should not be NULL
    ASSERT(stringBuffer);
    bool retval = false;

    EnterCriticalSection(&ResourceLock);

    if ( stringBuffer && LoadString(m_hSatDLL, stringID, stringBuffer, MAX_LOADSTRING) )
        retval = true;

    LeaveCriticalSection(&ResourceLock);

    return retval;
}

HMENU ResourceSatellite::getMenu(unsigned int menuID)
{
    HMENU hNewMenu;

    EnterCriticalSection(&ResourceLock);

    hNewMenu = LoadMenu(m_hSatDLL,(LPCWSTR)LongToPtr(menuID));

    LeaveCriticalSection(&ResourceLock);

    return hNewMenu;
}

bool ResourceSatellite::startDialog(unsigned int dialogID, HWND hWndParent, DLGPROC lpDialogFunc )
{
    EnterCriticalSection(&ResourceLock);

    INT_PTR ret_val =
        DialogBoxParam( m_hSatDLL, (LPCWSTR)LongToPtr(dialogID), hWndParent,lpDialogFunc, (LPARAM)dialogID);

    LeaveCriticalSection(&ResourceLock);

    return ( ret_val != -1 && ret_val );
}

HWND ResourceSatellite::createDialog(unsigned int dialogID, HWND hWndParent, DLGPROC lpDialogFunc )
{
	HWND hNewDlg;
    EnterCriticalSection(&ResourceLock);

    hNewDlg =
        CreateDialogParam( m_hSatDLL, (LPCWSTR)LongToPtr(dialogID), hWndParent,lpDialogFunc, (LPARAM)dialogID);

    LeaveCriticalSection(&ResourceLock);

    return hNewDlg;
}

void ResourceSatellite::setLanguage(LANGID langID)
{
    EnterCriticalSection(&ResourceLock);

    // We should always have a valid resource handle
    ASSERT(m_hSatDLL != NULL);

    // Attempt to load the satellite DLL
    HMODULE new_hSatDLL = LoadSatelliteDLL(langID);

    if ( new_hSatDLL != NULL )
    {
        m_UILanguage = langID;
        // Free the old satellite DLL
        if ( m_hSatDLL != GetModuleHandle(NULL) )
            FreeLibrary(m_hSatDLL);
        m_hSatDLL = new_hSatDLL;
    }

    // We should always have a valid resource handle
    ASSERT(m_hSatDLL != NULL);

    LeaveCriticalSection(&ResourceLock);
}

// Loads the satellite DLL specified for the language DesiredLanguage
HMODULE ResourceSatellite::LoadSatelliteDLL(LANGID DesiredLanguage)
{
    wchar_t     BaseDirectory[MAX_PATH];
    wchar_t     SatellitePath[MAX_PATH];
    wchar_t     buffer[100];
    HMODULE     hDLL = NULL;

    // Get the base directory for the satellite DLL search
    if (getLocalizedDirectory(DesiredLanguage, BaseDirectory, MAX_PATH, buffer, 100) == false)
        return NULL;
    
    // First try to load the library with the fully specified language
    if (!FAILED(StringCchPrintfW(SatellitePath, MAX_PATH, L"%s\\%s\\%s", BaseDirectory, buffer, SATELLITE_NAME) )) {
        hDLL = LoadLibrary(SatellitePath);
    }

    // Return if the satellite was loaded
    if (hDLL)
        return hDLL;

    // Get the primary language with default sublanguage convert it to a string
    DesiredLanguage = MAKELANGID(PRIMARYLANGID(DesiredLanguage), SUBLANG_DEFAULT);
    _itow_s(DesiredLanguage, buffer, sizeof(buffer)/sizeof(buffer[0]), 10);

    // Try to load the library with the primary language
    if (!FAILED(StringCchPrintfW(SatellitePath, MAX_PATH, L"%s\\%s\\%s", BaseDirectory, buffer, SATELLITE_NAME) )) {
        hDLL = LoadLibrary(SatellitePath);
    }
    return hDLL;
}


bool ResourceSatellite::getLocalizedDirectory(LANGID DesiredLanguage, wchar_t szBaseDirectory[], unsigned int nMaxBaseDir, wchar_t szLangDirectory[], unsigned int nMaxLangDir)
{
    wchar_t     szBuffer[100];
    bool        bRet = false;
        
    // Get the base directory for the satellite DLL search
    if (GetModuleFileName(NULL, szBaseDirectory, nMaxBaseDir) == 0)
        return bRet;

    szBaseDirectory[nMaxBaseDir - 1] = L'\0';

    // Remove the filename to leave a base path
    wchar_t* p;
    for (p = szBaseDirectory + wcslen(szBaseDirectory); p != szBaseDirectory && *p != L'\\'; --p);
        *p = 0;

    // Convert DesiredLanguage to a string
    _itow_s(DesiredLanguage, szBuffer, sizeof(szBuffer)/sizeof(szBuffer[0]), 10);

    wcsncpy_s(szLangDirectory, nMaxLangDir, szBuffer, nMaxLangDir);
    szLangDirectory[nMaxLangDir - 1] = L'\0';

    bRet = true;

    return bRet;

}
