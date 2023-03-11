/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

// DSStrings.h - various strings used by the datastore

#ifndef DSSTRINGS_H
#define DSSTRINGS_H

// Generic values
static const WCHAR * g_strDSValueYes                (L"yes");
static const WCHAR * g_strDSValueNo                 (L"no");
static const WCHAR * g_strDSValueTrue               (L"true");
static const WCHAR * g_strDSValueFalse              (L"false");
static const WCHAR * g_strDSValueOn                 (L"on");
static const WCHAR * g_strDSValueOff                (L"off");
static const WCHAR * g_strDSValue0                  (L"0");
static const WCHAR * g_strDSValue1                  (L"1");

// Property IDs
static const WCHAR * g_strDSPropertySerialPort0     (L"SerialPort0");
static const WCHAR * g_strDSPropertySerialPort1     (L"SerialPort1");
static const WCHAR * g_strDSPropertySerialPort2     (L"SerialPort2");
static const WCHAR * g_strDSPropertyCreateConsole   (L"CreateConsole");
static const WCHAR * g_strDSPropertyScreenWidth     (L"ScreenWidth");
static const WCHAR * g_strDSPropertyScreenHeight    (L"ScreenHeight");
static const WCHAR * g_strDSPropertyColorDepth      (L"ColorDepth");
static const WCHAR * g_strDSPropertySkin            (L"Skin");
static const WCHAR * g_strDSPropertyShowSkin        (L"ShowSkin");
static const WCHAR * g_strDSPropertyOrientation     (L"Orientation");
static const WCHAR * g_strDSPropertyZoom2x          (L"Zoom2x");
static const WCHAR * g_strDSPropertyAlwaysOnTop     (L"AlwaysOnTop");
static const WCHAR * g_strDSPropertyEnableToolTips  (L"EnableToolTips");
static const WCHAR * g_strDSPropertyOSBinImage      (L"OSBinImage");
static const WCHAR * g_strDSPropertySpecifyAddress  (L"SpecifyAddress");
static const WCHAR * g_strDSPropertyImageAddress    (L"ImageAddress");
static const WCHAR * g_strDSPropertySpecifyRAMSize  (L"SpecifyRAMSize");
static const WCHAR * g_strDSPropertyRAMSize         (L"RAMSize");
static const WCHAR * g_strDSPropertyFlashFile       (L"FlashFile");
static const WCHAR * g_strDSPropertyHostKey         (L"HostKey");
static const WCHAR * g_strDSPropertyEthernetEnabled (L"EthernetEnabled");
static const WCHAR * g_strDSPropertyCS8900EthernetEnabled (L"CS8900EthernetEnabled");
static const WCHAR * g_strDSPropertyHostOnlyEthernetEnabled (L"HostOnlyEthernetEnabled");
static const WCHAR * g_strDSPropertyNE2000Adapter   (L"NE2000Adapter");
static const WCHAR * g_strDSPropertyCS8900Adapter   (L"CS8900Adapter");
static const WCHAR * g_strDSPropertyFolderShare     (L"SharedFolder");

// Default property values
static const WCHAR * g_strDSDefaultColorDepth       (L"16");
static const WCHAR * g_strDSDefaultOrientation      (L"0");
static const WCHAR * g_strDSOrientation_90          (L"90");
static const WCHAR * g_strDSOrientation_180         (L"180");
static const WCHAR * g_strDSOrientation_270         (L"270");

// These are invariants. Do not localize them.
static const WCHAR * gc_strNoneInvariant            (L"None");
static struct {
    int ids;                  // Resource ID for localized name
    wchar_t *Name;            // Command line name
    unsigned __int8 Value;    // VK value
} HostKeys[3] = { 
    {IDS_NONE, L"none", 0},
    {IDS_ALT, L"Left-Alt", VK_LMENU},
    {IDS_RIGHTALT, L"Right-Alt", VK_RMENU}
};

#endif // DSSTRINGS_H
