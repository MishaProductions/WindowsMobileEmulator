/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "Config.h"

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "config.tmh"
#include "vsd_logging_inc.h"

EmulatorConfig::EmulatorConfig()
{
}

EmulatorConfig::~EmulatorConfig()
{
}

bool EmulatorConfig::init()
{
    KeyboardSelector  = 0;
    KeyboardToWindows = false;
    HostOnlyRouting = false;
    FlashEnabled = false;
    DontSaveState = false;
    SkinXMLFile = NULL;
    FlashStateFile = NULL;
    LangID = 0;
    UARTNames[0] = UARTNames[1] = UARTNames[2] = NULL;
    ScreenHeight = 0;
    SaveStateFileName = NULL;
    InitialInstructionPointer = 0;
    ScreenHeight = 0;
    ScreenWidth = 0;
    ScreenBitsPerPixel = 0;
    ROMImageName = NULL;
    ROMBaseAddress = 0;
    ProcessorFeatures = 0;
    NetworkingEnabled = false;
    PCMCIACardInserted = false;
    fCreateConsoleWindow = FALSE;
    VMIDName = NULL;
    memset(&VMID, 0, sizeof(VMID));
    RotationMode = 0;
    DefaultRotationMode = 0;
    IsZoomed = false;
    IsAlwaysOnTop = false;
    fShowSkin = true;
    ToolTipState = ToolTipOn;
    memset(NE2000MACAddress, 0, sizeof(NE2000MACAddress));
    memset(SuggestedAdapterMacAddressNE2000, 0, sizeof(SuggestedAdapterMacAddressNE2000));
    memset(SuggestedAdapterMacAddressCS8900, 0, sizeof(SuggestedAdapterMacAddressCS8900));
    FolderShareName = NULL;
    PhysicalMemoryExtensionSize = 0;
    VSSetup = false;
    LoadImage = false;
    SpecifiedNE2000Mac = false;
    SpecifiedCS8900Mac = false;
    RegisteredInROT = false;
    UseUpdatedSettings = false;
    PassiveKITL = false;
    UseDefaultSaveState = false;
    FolderSettingsChanged = false;
    SurpressMessages = false;
    NoSecurityPrompt = false;
    Board64RamRegion = false;
    DialogOnly = false;
    FuncKeyCode = 0;
    return true;
}

void __fastcall EmulatorConfig::SaveState(StateFiler& filer) const
{
  	filer.Write('CONF');
// private:
    filer.Write(KeyboardSelector);
    filer.Write(KeyboardToWindows);
    filer.Write(HostOnlyRouting);
    filer.Write(FlashEnabled);
    filer.WriteString(SkinXMLFile);
    filer.WriteString(FlashStateFile);
    filer.Write(LangID);
    filer.WriteString(UARTNames[0]);
    filer.WriteString(UARTNames[1]);
    filer.WriteString(UARTNames[2]);
    //Filer.WriteString(SaveStateFileName);  - not persisted:  the filename is known before RestoreState begins.

//public:
    filer.Write(InitialInstructionPointer);
    //XMLSkin *Skin;
    //ResourceSatellite Resources;
    filer.Write(ScreenHeight);
    filer.Write(ScreenWidth);
    filer.Write(ScreenBitsPerPixel);
    filer.WriteString(ROMImageName);  // it isn't needed after a restore, but we include it
                                      // to detect when the user has pointed us at a new image
	//Filer.Write(ROMBaseAddress);      // not included in save-state - it isn't needed after a restore
	filer.Write(ProcessorFeatures);
	filer.Write(NetworkingEnabled);
	filer.Write(PCMCIACardInserted);
	filer.Write(fCreateConsoleWindow);
    filer.WriteString(VMIDName);
    filer.Write(VMID);
    filer.Write(RotationMode);
    filer.Write(IsZoomed);
    filer.Write(fShowSkin);
    filer.Write(ToolTipState);
    filer.Write(NE2000MACAddress);
    filer.Write(IsAlwaysOnTop);
    filer.WriteString(FolderShareName);
    filer.Write(SuggestedAdapterMacAddressNE2000);
    filer.Write(SuggestedAdapterMacAddressCS8900);
    filer.Write(PhysicalMemoryExtensionSize);
    // filer.Write(VSSetup ) // not included in save-state - it isn't needed after a restore
    // filer.Write(LoadImage ) // not included in save-state - it isn't needed after a restore
    // filer.Write(UseUpdatedSettings) // not included in save-state
    // filer.Write(PassiveKITL) // only makes sense on cold boot
    // filer.Write(DialogOnly) // only makes sense on start
    filer.Write(SpecifiedNE2000Mac);
    filer.Write(SpecifiedCS8900Mac);
    filer.Write(UseDefaultSaveState);
    filer.Write(Board64RamRegion);
    filer.Write(DefaultRotationMode);
    filer.Write(FuncKeyCode);

    // Write some zeros at the end of the Config section.  If the config
    // object needs to save additional fields in the future, add the extra
    // filer.Write() calls above this comment, and subtract the field size from
    // the size of the Temp array below.
    //
    // This saves us from having to bump the save-state file format each time
    // the config object's list of fields changes.
#define CONFIG_SAVESTATE_PADDING 55

    char Temp[CONFIG_SAVESTATE_PADDING];
    memset(Temp, 0, sizeof(Temp));
    filer.Write(Temp, sizeof(Temp));
}

void __fastcall EmulatorConfig::RestoreState(StateFiler& filer)
{
    int BufferDWORD = 0;
    LANGID BufferLANGID;
    unsigned __int8 BufferBYTE = 0;
    bool BufferBOOL = false;
    wchar_t * BufferWCHAR = NULL;
    unsigned __int8 BufferMAC[6];

    filer.Verify('CONF');
// private:
    (UseUpdatedSettings ? filer.Read(BufferBYTE) : filer.Read(KeyboardSelector));
    (UseUpdatedSettings ? filer.Read(BufferBOOL) : filer.Read(KeyboardToWindows));
    (UseUpdatedSettings ? filer.Read(BufferBOOL) : filer.Read(HostOnlyRouting));
    filer.Read(FlashEnabled);
    filer.ReadString((UseUpdatedSettings ? BufferWCHAR : SkinXMLFile));
    if ( BufferWCHAR != NULL ) { delete [] BufferWCHAR; BufferWCHAR = NULL; }
    filer.ReadString(FlashStateFile);
    (UseUpdatedSettings ? filer.Read(BufferLANGID) : filer.Read(LangID));
    filer.ReadString(UARTNames[0]);
    filer.ReadString(UARTNames[1]);
    filer.ReadString(UARTNames[2]);
    //filer.ReadString(SaveStateFileName);  - not persisted:  the filename is known before RestoreState begins.

//public:
    filer.Read(InitialInstructionPointer);
    //XMLSkin *Skin;
    //ResourceSatellite Resources;
    filer.Read(ScreenHeight);
    filer.Read(ScreenWidth);
    filer.Read(ScreenBitsPerPixel);
    filer.ReadString(ROMImageName);  // it isn't needed after a restore but we include it to detect
                                     // that the user has pointed us at a new image
    //filer.Read(ROMBaseAddress);      // not included in save-state - it isn't needed after a restore
    filer.Read(ProcessorFeatures);
    filer.Read(NetworkingEnabled);
    (UseUpdatedSettings ? filer.Read(BufferBOOL) : filer.Read(PCMCIACardInserted));
    filer.Read(fCreateConsoleWindow);
    filer.ReadString(VMIDName);
    filer.Read(VMID);
    (UseUpdatedSettings ? filer.Read(BufferDWORD) : filer.Read(RotationMode));
    (UseUpdatedSettings ? filer.Read(BufferBOOL) : filer.Read(IsZoomed));
    (UseUpdatedSettings ? filer.Read(BufferBOOL) : filer.Read(fShowSkin));
    (UseUpdatedSettings ? filer.Read(BufferDWORD) : filer.Read(ToolTipState));
    (UseUpdatedSettings ? filer.Read(BufferMAC) : filer.Read(NE2000MACAddress));
    (UseUpdatedSettings ? filer.Read(BufferBOOL) : filer.Read(IsAlwaysOnTop));
    (UseUpdatedSettings ? filer.ReadString(BufferWCHAR) : filer.ReadString(FolderShareName));
    if ( UseUpdatedSettings && (( FolderShareName == NULL ) ^ ( BufferWCHAR == NULL )) )
    {
        FolderSettingsChanged = true;
        setFolderShareName(FolderShareName);
    }
    if ( BufferWCHAR != NULL ) { delete [] BufferWCHAR; BufferWCHAR = NULL; }
    (UseUpdatedSettings ? filer.Read(BufferMAC):filer.Read(SuggestedAdapterMacAddressNE2000));
    filer.Read(SuggestedAdapterMacAddressCS8900);
    filer.Read(PhysicalMemoryExtensionSize);
    // filer.Read(VSSetup ) // not included in save-state - it isn't needed after a restore
    // filer.Read(LoadImage ) // not included in save-state - it isn't needed after a restore
    // filer.Read(UseUpdatedSettings)  // not included in save-state
    // filer.Read(PassiveKITL) // only makes sense on cold boot
    // filer.Read(DialogOnly) // only makes sense on start
    (UseUpdatedSettings ? filer.Read(BufferBOOL) : filer.Read(SpecifiedNE2000Mac));
    filer.Read(SpecifiedCS8900Mac);

    // If the MAC addresses were not specified clear the entries
    if (!SpecifiedNE2000Mac)
        memset(SuggestedAdapterMacAddressNE2000, 0, sizeof(SuggestedAdapterMacAddressNE2000));
    if (!SpecifiedCS8900Mac)
       memset(SuggestedAdapterMacAddressCS8900, 0, sizeof(SuggestedAdapterMacAddressCS8900));
    filer.Read(UseDefaultSaveState);
    filer.Read(Board64RamRegion);

    // Allow the default rotation mode to change when restoring from a global state
    if (UseUpdatedSettings)
    {
        filer.Read(BufferDWORD);
        DefaultRotationMode = RotationMode;
    }
    else
        filer.Read(DefaultRotationMode);
    (UseUpdatedSettings ? filer.Read(BufferDWORD):filer.Read(FuncKeyCode));

    // See EmulatorConfig::SaveState for an explanation of this read.
    char Temp[CONFIG_SAVESTATE_PADDING];
    filer.Read(Temp, sizeof(Temp));
}

CfgVideoRetType EmulatorConfig::setVideo(__inout_z wchar_t * video)
{
#if FEATURE_GUI
    int Height;
    int Width;
    int BitsPerPixel;
    wchar_t *x;

    x = wcschr(video, L'x');
    if (!x) {
        return CFG_VIDEO_MALFORMED_VIDEO;
    }
    *x = L'\0';
    Width = _wtoi(video);
    *x = L'x';
    video = x+1;
    x = wcschr(video, L'x');
    if (!x) {
        return CFG_VIDEO_MALFORMED_VIDEO;
    }
    *x = L'\0';
    Height = _wtoi(video);
    *x = L'x';
    BitsPerPixel = _wtoi(x+1);

    // At this point, Width, Height and BitsPerPixel are initialized.
    // Validate them now.
    if (Width < 64 || Width > 800 || Width%2 != 0) {
        return CFG_VIDEO_INVALID_WIDTH;
    }
    if (Height < 64 || Height > 800 || Height%2 != 0) {
        return CFG_VIDEO_INVALID_HEIGHT;
    }
    if (BitsPerPixel != 16 && BitsPerPixel != 24 && BitsPerPixel != 32) {
        return CFG_VIDEO_INVALID_DEPTH;
    }
    if (Height*Width*(BitsPerPixel>>3) > 1024*1024) {
        // The SMDK2410 frame buffer is limited to 1mb by its config.bib
        return CFG_VIDEO_EXCEEDS_BUFFER;
    }

    // Validation is complete.  Use these values.
    ScreenWidth = Width;
    ScreenHeight = Height;
    ScreenBitsPerPixel = BitsPerPixel;
    return CFG_VIDEO_SUCCESS;
#else
    return CFG_VIDEO_SUCCESS;
#endif
}

bool EmulatorConfig::setVMID(__in_z wchar_t * VMIDString)
{
    if (FAILED(IIDFromString(VMIDString, &VMID))) {
        return false;
    }
    return true;
}

bool EmulatorConfig::setUART(unsigned int PortNum, __in_z_opt const wchar_t * PortName)
{
    wchar_t *temp;

    ASSERT(PortNum < ARRAY_SIZE(UARTNames));
    temp = PortName != NULL ? _wcsdup(PortName) : NULL;
    if ( PortName != NULL && temp == NULL ) {
        return false;
    }
    free(UARTNames[PortNum]);
    UARTNames[PortNum] = temp;
    return true;
}

bool EmulatorConfig::setSaveStateFileName(__in_z const wchar_t * FileName)
{
    return updateFileEntry(FileName, &SaveStateFileName, false);
}

bool EmulatorConfig::setSkinXMLFile(__in_z const wchar_t * filename)
{
    return updateFileEntry(filename, &SkinXMLFile, true);
}


bool EmulatorConfig::setFlashStateFile(__in_z const wchar_t * filename)
{
    return updateFileEntry(filename, &FlashStateFile, false);
}

bool EmulatorConfig::setROMImageName(__in_z const wchar_t * ImageName)
{
    return updateFileEntry(ImageName, &ROMImageName, false);
}

bool EmulatorConfig::setVMIDName(__in_z const wchar_t * NewName)
{
    wchar_t *temp = _wcsdup(NewName);

    if (temp == NULL) {
        return false;
    }
    free(VMIDName);
    VMIDName = temp;
    return true;
}

void EmulatorConfig::setHostMacAddressNE2000(unsigned __int8 * m)
{
    ASSERT( m != NULL );
    for(int i = 0; i < 6; i++ )
        SuggestedAdapterMacAddressNE2000[i] = m[i];
}

void EmulatorConfig::setHostMacAddressCS8900(unsigned __int8 * m)
{
    ASSERT( m != NULL );
    for(int i = 0; i < 6; i++ )
        SuggestedAdapterMacAddressCS8900[i] = m[i];
}

void EmulatorConfig::getHostMacAddressNE2000(unsigned __int8 * m)
{
    ASSERT( m != NULL );
    for(int i = 0; i < 6; i++ )
        m[i] = SuggestedAdapterMacAddressNE2000[i];
}

void StripEndQuote(__inout_z wchar_t * temp)
{
    // When CommandLineToArgvW parses the command line it expands escapes so
    // "d:\" ends up being d:". However there is a difference as d: is the current
    // directory on D: and D:\ is the root of D:. To fix that we replace the
    // trailing " with \.
    if (temp != NULL)
    {
        unsigned __int64 length = wcslen( temp );
        bool Stripped = false;
        while (length > 1 && temp[length - 1] == L'"')
        {
            length--;
            Stripped = true;
            temp[length] = 0;
        }
        if ( Stripped )
            temp[length] = L'\\';
    }
}

wchar_t * StripStartQuote(__in_z_opt const wchar_t * temp)
{
    wchar_t * stripped_str = (wchar_t*)temp;
    if (temp != NULL)
    {
        while (*stripped_str == L'"')
        {
            stripped_str++;
        }
    }
    return stripped_str;
}

bool EmulatorConfig::updateFileEntry(__in_z const wchar_t * NewValue, __deref_inout wchar_t ** Entry, bool AllowNULL)
{
    // Check if the entry that needs to be updated is valid
    if (Entry == NULL)
    {
        ASSERT(false);
        return false;
    }

    // Make a copy of the new value for the entry (stripping " from the front)
    wchar_t *temp = StripStartQuote(NewValue) != NULL ? 
                    _wcsdup(StripStartQuote(NewValue)) : NULL;

    // Check for out of memory and null input
    if (temp == NULL && (!AllowNULL || NewValue != NULL)) {
        return false;
    }
    // Strip " from the end of the string
    StripEndQuote(temp);
    // Free the old value of the entry and update it with the new value
    free(*Entry);
    *Entry = temp;
    return true;
}

bool EmulatorConfig::setFolderShareName(__in_z const wchar_t * NewName)
{
    return updateFileEntry(NewName, &FolderShareName, true);
}

bool CompareFileNames( __in_z const wchar_t * path1, __in_z const wchar_t * path2 )
{
    if (path1 == NULL || path2 == NULL)
        return false;

    unsigned __int64 length1 = wcslen( path1 );
    unsigned __int64 length2 = wcslen( path2 );

    while (length1 > 1 && length2 > 1 && path1[length1-1] != L'/' && path1[length1-1] != L'\\')
    {
        if (path1[length1-1] != path2[length2-1])
            return false;
        length1--; length2--;
    }

    if ( length1 <= 1 || length2 <= 1)
        return false;

    return (path2[length2-1] == L'/' || path2[length2-1] == L'\\');
}

bool LogCompFailure( LPCSTR name)
{
    LOG_WARN(GENERAL, "Failed configuration comparison on '%s'", name );
    return true;
}

#define NOT_EQUAL_VAL(x) (x != other->x && LogCompFailure( #x ))
#define NOT_EQUAL_STR(x) (SafeWcsicmp(x, other->x) && LogCompFailure( #x ))
#define NOT_EQUAL_MEM(x) (memcmp(x, other->x, sizeof(x)) != 0 && LogCompFailure( #x ))

bool EmulatorConfig::equal(EmulatorConfig * other, bool update, bool runtime_comp)
{
    // First compare all the values that are recorded in the save state file
    if (
        //NOT_EQUAL_VAL(KeyboardSelector) ||    // can be updated
        //NOT_EQUAL_VAL(KeyboardToWindows) ||   // can be updated
        //NOT_EQUAL_VAL(HostOnlyRouting) ||     // can be updated
        NOT_EQUAL_VAL(FlashEnabled) ||
        // NOT_EQUAL_STR(SkinXMLFile) ||        // can be updated
        NOT_EQUAL_STR(FlashStateFile) ||
        //NOT_EQUAL_VAL(LangID) ||              // can be updated
        NOT_EQUAL_STR(UARTNames[0]) ||
        NOT_EQUAL_STR(UARTNames[1]) ||
        NOT_EQUAL_STR(UARTNames[2]) ||
        //NOT_EQUAL_STR(SaveStateFileName) ||   // not included in save-state
        //NOT_EQUAL_VAL(InitialInstructionPointer) ||
        // NOT_EQUAL_VAL(ScreenHeight) ||       // can be updated
        // NOT_EQUAL_VAL(ScreenWidth) ||        // can be updated
        // NOT_EQUAL_VAL(ScreenBitsPerPixel) || // can be updated
        !CompareFileNames(ROMImageName, other->ROMImageName) ||        // is included in save-state for reference only
        //NOT_EQUAL_VAL(ROMBaseAddress) ||      // not included in save-state 
        NOT_EQUAL_VAL(ProcessorFeatures) ||
        NOT_EQUAL_VAL(NetworkingEnabled) ||
        //NOT_EQUAL_VAL(PCMCIACardInserted) ||
        NOT_EQUAL_VAL(fCreateConsoleWindow) ||
        NOT_EQUAL_STR(VMIDName) ||
        NOT_EQUAL_VAL(VMID) ||
        // NOT_EQUAL_VAL(RotationMode) ||       // can be updated
        // NOT_EQUAL_VAL(DefaultRotationMode) ||       // can be updated
        // NOT_EQUAL_VAL(IsZoomed) ||           // can be updated
        // NOT_EQUAL_VAL(fShowSkin) ||          // can be updated
        // NOT_EQUAL_VAL(NE2000MACAddress) ||
        // NOT_EQUAL_VAL(IsAlwaysOnTop) ||      // can be updated
        // NOT_EQUAL_STR(FolderShareName) ||    // can be updated
        // NOT_EQUAL_VAL(FuncKeyCode) ||        // can be updated
        //NOT_EQUAL_VAL(SpecifiedNE2000Mac) ||
        NOT_EQUAL_VAL(SpecifiedCS8900Mac) ||
        //SpecifiedNE2000Mac && NOT_EQUAL_MEM(SuggestedAdapterMacAddressNE2000) ||
        SpecifiedCS8900Mac && NOT_EQUAL_MEM(SuggestedAdapterMacAddressCS8900) ||
        NOT_EQUAL_VAL(PhysicalMemoryExtensionSize) ||
        NOT_EQUAL_VAL(UseDefaultSaveState) 
     // NOT_EQUAL_VAL(Board64RamRegion)     // is not calculatable without loading the image
    )// NOT_EQUAL_VAL(VSSetup )             // not included in save-state 
     // NOT_EQUAL_VAL(DialogOnly)           // not included in save-state
     // NOT_EQUAL_VAL(LoadImage)............// not included in save-state
        return false; // Some value was different

    // Check the settings that can be changed to allow the configuration 
    // to match
    if ( !update && (
        NOT_EQUAL_VAL(LangID) ||
        NOT_EQUAL_STR(FolderShareName) ||
        // Keyboard controls
        NOT_EQUAL_VAL(KeyboardSelector) ||
        NOT_EQUAL_VAL(KeyboardToWindows) ||
        // Display related settings
        NOT_EQUAL_STR(SkinXMLFile) ||
        NOT_EQUAL_VAL(ScreenHeight) ||
        NOT_EQUAL_VAL(ScreenWidth) ||
        NOT_EQUAL_VAL(ScreenBitsPerPixel) ||
        NOT_EQUAL_VAL(IsAlwaysOnTop) ||
        NOT_EQUAL_VAL(RotationMode) ||
        NOT_EQUAL_VAL(IsZoomed) ||
        NOT_EQUAL_VAL(fShowSkin) ||
        NOT_EQUAL_VAL(ToolTipState) ||
        NOT_EQUAL_VAL(FuncKeyCode) ||
        // Network related settings
        NOT_EQUAL_VAL(SpecifiedNE2000Mac) ||
        SpecifiedNE2000Mac && NOT_EQUAL_MEM(SuggestedAdapterMacAddressNE2000) ||
        NOT_EQUAL_VAL(HostOnlyRouting) ||
        NOT_EQUAL_VAL(PCMCIACardInserted) )
    )
        return false; // Some value was different

    // Check the settings that are determined at emulator runtime and are not
    // preserved in the saved state
    if ( runtime_comp && (
        NOT_EQUAL_STR(SaveStateFileName) ||
        NOT_EQUAL_VAL(ROMBaseAddress) ||      // not included in save-state 
        NOT_EQUAL_VAL(VSSetup) ||             // not included in save-state 
        NOT_EQUAL_VAL(LoadImage) ||           // not included in save-state
        NOT_EQUAL_VAL(UseUpdatedSettings) ||  // not included in save-state
        NOT_EQUAL_MEM(NE2000MACAddress) ||
        NOT_EQUAL_VAL(PassiveKITL) ||  // not included in save-state
        NOT_EQUAL_VAL(InitialInstructionPointer) || // not included in save-state
        NOT_EQUAL_VAL(SurpressMessages))
    )
        return false; // Some value was different

    return true;
}

