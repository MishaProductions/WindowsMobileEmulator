/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef EMULATORCONFIG__H_
#define EMULATORCONFIG__H_

#include "emulator.h"
#include "ResourceSatellite.h"
#include "State.h"

enum CfgVideoRetType
{
    CFG_VIDEO_SUCCESS                     = 0,
    CFG_VIDEO_MALFORMED_VIDEO,
    CFG_VIDEO_INVALID_WIDTH,
    CFG_VIDEO_INVALID_HEIGHT,
    CFG_VIDEO_INVALID_DEPTH,
    CFG_VIDEO_EXCEEDS_BUFFER,
};

class XMLSkin;

enum ToolTipStateEnum { ToolTipOn, ToolTipOff, ToolTipDelay };

class EmulatorConfig
{
public:
    // Setup and initialization
    EmulatorConfig();
    ~EmulatorConfig();
    bool init();
    bool equal(EmulatorConfig * other, bool update, bool runtime_comp = true);
    void __fastcall SaveState(StateFiler& filer) const;
    void __fastcall RestoreState(StateFiler& filer);
    friend bool BoardUpdateConfiguration(class EmulatorConfig *pNewConfig);

    // Configuration query methods
    inline unsigned __int8 getKeyboardSelector() { return KeyboardSelector; }
    inline bool getKeyboardToWindows() { return KeyboardToWindows; }
    inline bool getHostOnlyRouting() { return HostOnlyRouting; }
    inline bool getFlashEnabled() { return FlashEnabled; }
    inline wchar_t * getSkinXMLFile() { return SkinXMLFile; }
    inline wchar_t * getFlashStateFile() { return FlashStateFile; }
    inline LANGID getLangID() { return LangID; }
    inline wchar_t *getUART(unsigned int PortNum) { ASSERT(PortNum < ARRAY_SIZE(UARTNames)); return UARTNames[PortNum]; }
    inline wchar_t * getROMImageName() { return ROMImageName; }
    inline wchar_t *getVMIDName(void) { return VMIDName; }
    inline wchar_t  *getSaveStateFileName(void) { return SaveStateFileName; }
    void getHostMacAddressNE2000(unsigned __int8 * m);
    inline wchar_t *getFolderShareName(void) { return FolderShareName; }
    inline bool getLoadImage(void) { return LoadImage; }

    // Methods for setting the configuration
    inline void setKeyboardToWindows(bool toWindows){ KeyboardToWindows = toWindows; }
    inline void setKeyboardState(unsigned __int8 selector, bool toWindows)    
           { KeyboardSelector = selector; KeyboardToWindows = toWindows; };
    inline void setHostOnlyRouting(bool hostOnly){ HostOnlyRouting = hostOnly; }
    inline void setFlashEnabled(bool flash){ FlashEnabled = flash; }
    inline void setDontSaveState(bool val){ DontSaveState = val; }
    bool setSkinXMLFile(__in_z const wchar_t * filename);
    inline void clearSkinXMLFile(void) { free(SkinXMLFile); SkinXMLFile = NULL; }
    bool setFlashStateFile(__in_z const wchar_t * filename);
    inline void clearFlashStateFile(void) { free(FlashStateFile); FlashStateFile = NULL; }
    inline bool setLanguage(__in_z const wchar_t * langStr) { LangID = (LANGID)_wtoi(langStr); return (LangID != 0 ); }
    inline void setLoadImage(bool val) { LoadImage = val; }
    CfgVideoRetType setVideo(__inout_z wchar_t * video);
    bool setVMID(__in_z wchar_t * VMIDString);
    bool setUART(unsigned int PortNum, __in_z_opt const wchar_t * PortName);
    inline void clearUART(unsigned int PortNum) { ASSERT(PortNum < ARRAY_SIZE(UARTNames)); free(UARTNames[PortNum]); UARTNames[PortNum] = NULL; }
    bool setROMImageName(__in_z const wchar_t * ImageName);
    inline void clearROMImageName(void) { free(ROMImageName); ROMImageName = NULL; }
    bool setVMIDName(__in_z const wchar_t * NewName);
    bool setSaveStateFileName(__in_z const wchar_t * FileName);
    void setHostMacAddressNE2000(unsigned __int8 * m);
    void setHostMacAddressCS8900(unsigned __int8 * m);
    bool setFolderShareName(__in_z const wchar_t * NewName);

    // Configuration logic
    inline bool isSkinFileSpecified() { return ( SkinXMLFile != NULL ); }
    inline bool isFlashStateFileSpecified() { return ( FlashStateFile != NULL ); }
    inline bool isSetLanguage() { return ( LangID != 0 ); }
    inline bool isVideoSpecified() { return (ScreenHeight != 0); }
    inline bool isToolTipEnabled() { return (ToolTipState != ToolTipOff); }
    inline bool isSaveStateEnabled() { return (SaveStateFileName != NULL) && !DontSaveState; }
    inline bool MatchHostMacAddressNE2000(unsigned __int8 * m) 
        { return *(__int32 *)m == *(__int32 *)SuggestedAdapterMacAddressNE2000 &&
                 *(__int16*)&m[4] == *(__int16*)&SuggestedAdapterMacAddressNE2000[4]; }

private:
    bool EmulatorConfig::updateFileEntry(__in_z const wchar_t * NewValue, __deref_inout wchar_t ** Entry, bool AllowNULL);
    // Data containing the current settings
    unsigned __int8 KeyboardSelector;
    bool KeyboardToWindows;
    bool HostOnlyRouting;
    bool FlashEnabled;
    bool DontSaveState;    // this field is not included in save-state
    wchar_t * SkinXMLFile;
    wchar_t * FlashStateFile;
    LANGID LangID;
    wchar_t *UARTNames[3];
    wchar_t *SaveStateFileName;
    wchar_t *ROMImageName; // this field is not included in save-state
    wchar_t *VMIDName;
    wchar_t *FolderShareName;
    bool LoadImage;

public:
    unsigned __int32 InitialInstructionPointer;
    ResourceSatellite Resources;
    int ScreenHeight;
    int ScreenWidth;
    int ScreenBitsPerPixel;
    unsigned __int32 ROMBaseAddress; // this field is not included in save-state - used only for .nb0 file loads
    unsigned __int32 ProcessorFeatures;
    bool NetworkingEnabled;
    bool PCMCIACardInserted;
    bool fCreateConsoleWindow;
    GUID VMID;
    unsigned int RotationMode;  // View is rotated by RotationMode * 90 degress clockwise
    unsigned int DefaultRotationMode;  // The rotation mode to which we reset on hard reset
    bool IsZoomed;              // View is currently zoomed
    bool IsAlwaysOnTop;         // True if the emulator window is always on top
    bool fShowSkin;             // skin, if present, is showing or not
    ToolTipStateEnum ToolTipState; // state of the tooltip
    unsigned __int16 NE2000MACAddress[3]; // MAC address for the NE2000 card
    unsigned __int8 SuggestedAdapterMacAddressNE2000[6];
    bool SpecifiedNE2000Mac;
    unsigned __int8 SuggestedAdapterMacAddressCS8900[6];
    bool SpecifiedCS8900Mac;
    unsigned __int32 PhysicalMemoryExtensionSize;
    unsigned __int32 FuncKeyCode;
    bool PassiveKITL;           // fill out the BOOTArgs structure to cause KITL not to wait for connection
    bool VSSetup;               // The emulator is booted to generate a saved state image
    bool RegisteredInROT;       // The emulator is registered in ROT
    bool UseUpdatedSettings;    // Used to override the settings from the saved state image
    bool UseDefaultSaveState;   // Use default save state name (derived from VMID)
    bool FolderSettingsChanged; // Folder sharing settings have changed during restore
    bool SurpressMessages;      // Do not show error dialogs
    bool NoSecurityPrompt;      // Do not promt when powering on potentially unsafe peripherals
    bool Board64RamRegion;      // There is a single 64MB RAM region so assume CE5.0 layout
    bool DialogOnly;            // This instance of DE was started to display configuration dialog
};

#endif //EMULATORCONFIG__H_
