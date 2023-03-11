/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef RESOURCE_SATELLITE_H__
#define RESOURCE_SATELLITE_H__

#define MAX_LOADSTRING   2000
#define MAX_LANGNAME     255  // Maximum native language name length
#define DEFAULT_LANGUAGE 1033

#ifndef EXTERNAL_RESOURCE_DEFS
#include "emulator.h"

#define SATELLITE_NAME   L"\\DeviceEmulatorUI.dll"
#define FLASH_TYPE       300  // Custom resource type for the flash bin file
#endif

class ResourceSatellite
{
public:
    ResourceSatellite();
    ~ResourceSatellite();

    bool getString(unsigned int strindID, __out_ecount(MAX_LOADSTRING) wchar_t * stringBuffer);  // retrieves a localized string
    HMENU getMenu(unsigned int menuID);                // retrieves a localized menu
    bool startDialog(unsigned int dialogID, HWND hWndParent, DLGPROC lpDialogFunc );
                                            // starts a localized dialog
    HWND createDialog(unsigned int dialogID, HWND hWndParent, DLGPROC lpDialogFunc );
                                            // create a localized modeless dialog
    void setLanguage(LANGID langID);        // load a resource DLL corresponding to langID
    // getLanguageList()[Optional] - returns a list of languages for which satellite files could be located
    __inline LANGID getLanguage() { return m_UILanguage; }

    HMODULE getSatelliteHModule() const { return m_hSatDLL; }

    bool getLocalizedDirectory(LANGID DesiredLanguage, wchar_t szBaseDirectory[], unsigned int nMaxBaseDir, wchar_t szLangDirectory[], unsigned int nMaxLangDir);
private:
    HMODULE LoadSatelliteDLL(LANGID DesiredLanguage);

    LANGID  m_UILanguage;							// The UI language chosen by the user
    HMODULE m_hSatDLL;								// Handle for the satellite DLL module
    CRITICAL_SECTION ResourceLock;                  // Allow thread safe access to resources
};

#endif // RESOURCE_SATELLITE_H__
