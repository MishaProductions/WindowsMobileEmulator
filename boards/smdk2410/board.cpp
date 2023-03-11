/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "emulator.h"
#include "Config.h"
#include "resource.h"
#include "state.h"
#include "cpu.h"
#include "board.h"
#include "mappedio.h"
#include "LoadBIN_NB0.h"
#if FEATURE_SKIN
#include "XMLSkin.h"
#endif
#include "devices.h"
#include "decfg.h"
#include "CompletionPort.h"

#define SUPPORT_42_BSP 1
#define SUPPORT_50_BSP 1

#define MAPPEDIODEVICE(baseclass, devicename, iobase, iolength) \
    extern class baseclass devicename;
#include "mappediodevices.h"
#undef MAPPEDIODEVICE

unsigned __int32 BoardIOAddress;
unsigned __int32 BoardIOAddressAdj;

const __int32 StatePlatform='SAMS'; // SAMSung

struct {
    wchar_t *Name;
    unsigned __int8 Value;
} HostKeys[3] = { // do not localize:  these are used for command-line processing
    {L"none", 0},
    {L"Left-Alt", VK_LMENU},
    {L"Right-Alt", VK_RMENU}
};


//ce.bib contains memory address info
#define PHYSICAL_MEMORY_BASE    0x30000000
#define PHYSICAL_MEMORY_SIZE    (64*1024*1024)

#define PHYSICAL_MEMORY_EXTENSION_BASE (PHYSICAL_MEMORY_BASE+PHYSICAL_MEMORY_SIZE)
#define PHYSICAL_MEMORY_EXTENSION_MAX_SIZE (192*1024*1024) // max size is 192mb

#define FLASH_BANK0_BASE    0x00000000
#define FLASH_BANK0_SIZE    (32*1024*1024)

#define INITIAL_STACK_POINTER              (PHYSICAL_MEMORY_BASE+0x10000)  // an arbitrary address within physical RAM

#pragma bss_seg(push, stack1, ".EmulatedMemory")    // Define a new EXE section, ".EmulatedMemory", so Physical/FlashMemory is at least 4k-aligned
unsigned __int32 PhysicalMemory[PHYSICAL_MEMORY_SIZE/sizeof(unsigned __int32)];
unsigned __int32 FlashBank0[FLASH_BANK0_SIZE/sizeof(unsigned __int32)];
#pragma bss_seg(pop, stack1)
LPVOID PhysicalMemoryExtension;
unsigned __int32 PhysicalMemoryExtensionSize; // size in bytes

#define EBOOT_64DRAM_START 0x30021000

#if SUPPORT_50_BSP
//---Copied from CE's platform\common\src\inc\oal_args.h----------------------------------------------

#define OAL_ARGS_SIGNATURE      'SGRA'
#define OAL_ARGS_VERSION        1

typedef struct {
    UINT32  signature;
    UINT16  oalVersion;
    UINT16  bspVersion;
} OAL_ARGS_HEADER;

//------------------------------------------------------------------------------

//---Copied from CE's public\common\oak\inc\pkfuncs.h ----------------------------------------------
typedef struct _DEVICE_LOCATION {
    DWORD IfcType;                  // One of the INTERFACE_TYPE enum, typically PCIBus
    DWORD BusNumber;                // Bus number, typically the PCI bus number the device resides on
    DWORD LogicalLoc;               // Defines logical location of device.  See above for PCIbus example. 
    PVOID PhysicalLoc;              // Reserved for future use
    DWORD Pin;                      // PCIbus IRQ pin
} DEVICE_LOCATION, *PDEVICE_LOCATION;
//------------------------------------------------------------------------------

//---Copied from CE's platform\common\src\inc\oal_kitl.h----------------------------------------------
#pragma warning (push)
#pragma warning (disable:4201) //nonstandard extension used : nameless struct/union 

typedef struct {
    UINT32 flags;
    DEVICE_LOCATION devLoc;                     // KITL device location
    union {
        struct {                                // Serial class parameters
            UINT32 baudRate;
            UINT32 dataBits;
            UINT32 stopBits;
            UINT32 parity;
        };         
        struct {                                // Ether class parameters
            UINT16 mac[3];
            UINT32 ipAddress;
            UINT32 ipMask;
            UINT32 ipRoute;
        };
    };
} OAL_KITL_ARGS;
#pragma warning(pop)

//------------------------------------------------------------------------------
//
//  Enum:  OAL_KITL_FLAGS
//
//  This enumeration specifies optional flags passed as part of OAL_KITL_ARGS
//  structure.
//  
typedef enum {
    OAL_KITL_FLAGS_ENABLED  = 0x0001,   // Kitl enable
    OAL_KITL_FLAGS_PASSIVE  = 0x0002,   // Kitl passive mode
    OAL_KITL_FLAGS_DHCP     = 0x0004,   // DHCP enable
    OAL_KITL_FLAGS_VMINI    = 0x0008,   // VMINI enable
    OAL_KITL_FLAGS_POLL     = 0x0010,   // Use polling (no interrupt)
    OAL_KITL_FLAGS_EXTNAME  = 0x0020    // Extend device name
} OAL_KITL_FLAGS;

//------------------------------------------------------------------------------

//---Copied from CE's platform\DeviceEmulator\src\inc\args.h----------------------------------------------

#define BSP_ARGS_VERSION    256 // Different than SMDK2410 to prevent the SMDK2410 eboot from
                                // passing its BSP_ARGS to a DeviceEmulator OAL

#define BSP_SCREEN_SIGNATURE 0xde12de34

typedef struct {
    OAL_ARGS_HEADER header;

    UINT8 deviceId[16];                 // Device identification
    OAL_KITL_ARGS kitl;

    // CAUTION:  The DeviceEmulator contains hard-coded knowledge of the addresses and contents of these
    //           three fields.
    UINT32 ScreenSignature;        // Set to BSP_SCREEN_SIGNATURE if the DeviceEmulator specifies screen size
    UINT16 ScreenWidth;                 // May be set by the DeviceEmulator, or zero
    UINT16 ScreenHeight;                // May be set by the DeviceEmulator, or zero
    UINT16 ScreenBitsPerPixel;          // May be set by the DeviceEmulator, or zero
    UINT16 EmulatorFlags;               // May be set by the DeviceEmulator, or zero
    UINT8  ScreenOrientation;           // May be set by the DeviceEmulator, or zero
    UINT8  Pad[15];                     // May be set by the DeviceEmulator, or zero

} BSP_ARGS;

//------------------------------------------------------------------------------

//---Copied from CE's platform\DeviceEmulator\src\inc\image_cfg.h----------------------------------------------

#define IMAGE_SHARE_ARGS_UA_START       0xAC020000 /* ie. 0x30020800 */

//------------------------------------------------------------------------------
#endif // SUPPORT_50_BSP

#if SUPPORT_42_BSP
//---Begin WinCE 4.2 support------------------------------------------------------------------------------
typedef struct _CEEthernetAddr
{
    unsigned __int32        fIPAddress;
    unsigned __int8        fMACAddress[6];
    unsigned __int16        fPort;
} EDBG_ADDR;

// This structure has been copied from platform\smdk2410\inc\bootarg.h:  keep the two copies in sync.
typedef struct _BOOT_ARGS {
    UCHAR   ucVideoMode;
    UCHAR   ucComPort;
    UCHAR   ucBaudDivisor;
    UCHAR   ucPCIConfigType;

    // The following args are not set by older versions of loadcepc,
    // so include a sig to verify that the remaining params are valid.
    // Also, include a length to allow expansion in the future.
    DWORD   dwSig;
    #define BOOTARG_SIG  0x544F4F42 // "BOOT"
    DWORD   dwLen;              // Total length of boot args struct
    UCHAR   ucLoaderFlags;      // Flags set by loader
    UCHAR   ucEshellFlags;      // Flags from eshell
    UCHAR   ucEdbgAdapterType;  // Type of debug Ether adapter
    UCHAR   ucEdbgIRQ;          // IRQ line to use for debug Ether adapter
    DWORD   dwEdbgBaseAddr;     // Base I/O address for debug Ether adapter
    DWORD   dwEdbgDebugZone;    // Allow EDBG debug zones to be turned on from loadcepc
    // The following is only valid if LDRFL_ADDR_VALID is set
    EDBG_ADDR EdbgAddr;         // IP/ether addr to use for debug Ethernet
    // The following addresses are only valid if LDRFL_JUMPIMG is set, and corresponding bit in
    // ucEshellFlags is set (configured by eshell, bit definitions in ethdbg.h).
    EDBG_ADDR EshellHostAddr;   // IP/ether addr and UDP port of host running eshell
    EDBG_ADDR DbgHostAddr;      // IP/ether addr and UDP port of host receiving dbg msgs
    EDBG_ADDR CeshHostAddr;     // IP/ether addr and UDP port of host running ether text shell
    EDBG_ADDR KdbgHostAddr;     // IP/ether addr and UDP port of host running kernel debugger
    DWORD DHCPLeaseTime;        // Length of DHCP IP lease in seconds
    DWORD EdbgFlags;            // Information about the ethernet system

    DWORD dwEBootFlag;            // Eboot flags indicating whether EBoot supports warm reset (older version may not)
    DWORD dwEBootAddr;            // Eboot entry point set by eboot and used during warm reset
    DWORD dwLaunchAddr;            // Old image launch address saved by EBoot when it receives jmpimage

    // The following args added to support passing info to flat framebuffer display driver
    DWORD    pvFlatFrameBuffer;    // pointer to flat frame buffer
    WORD    vesaMode;            // VESA mode being used
    WORD    cxDisplayScreen;    // displayable X size
    WORD    cyDisplayScreen;    // displayable Y size
    WORD    cxPhysicalScreen;    // physical X size
    WORD    cyPhysicalScreen;    // physical Y size
    WORD    cbScanLineLength;    // scan line byte count
    WORD    bppScreen;            // color depth
    UCHAR    RedMaskSize;        // size of red color mask
    UCHAR    RedMaskPosition;    // position for red color mask
    UCHAR    GreenMaskSize;        // size of green color mask
    UCHAR    GreenMaskPosition;    // position for green color mask
    UCHAR    BlueMaskSize;        // size of blue color mask
    UCHAR    BlueMaskPosition;    // position for blue color mask
    UCHAR    softReset;           // if one do not clear persistent storage

} BOOT_ARGS, *PBOOT_ARGS;
//------------------------------------------------------------------------------
#endif //SUPPORT_42_BSP

extern HRESULT SetSaveStateFileNameFromVMID(bool Global);

size_t __fastcall BoardMapGuestPhysicalToHostRAM(unsigned __int32 EffectiveAddress)
{
    if (EffectiveAddress >= PHYSICAL_MEMORY_BASE && EffectiveAddress < PHYSICAL_MEMORY_BASE+sizeof(PhysicalMemory)) {
        return (size_t)EffectiveAddress+(size_t)PhysicalMemory-PHYSICAL_MEMORY_BASE;
    } else if (PhysicalMemoryExtension && 
               EffectiveAddress >= PHYSICAL_MEMORY_EXTENSION_BASE && EffectiveAddress < PHYSICAL_MEMORY_EXTENSION_BASE+PhysicalMemoryExtensionSize) {
        return (size_t)EffectiveAddress + (size_t)PhysicalMemoryExtension-PHYSICAL_MEMORY_EXTENSION_BASE;
    }
    return 0;
}

size_t __fastcall BoardGetPhysicalRAMBase()
{
    return PHYSICAL_MEMORY_BASE;
}

size_t __fastcall BoardMapGuestPhysicalToHost(unsigned __int32 EffectiveAddress, size_t *pHostAdjust)
{
    size_t HostAddress;

    if (EffectiveAddress >= PHYSICAL_MEMORY_BASE && EffectiveAddress < PHYSICAL_MEMORY_BASE+sizeof(PhysicalMemory)) {
        *pHostAdjust = (size_t)PhysicalMemory-PHYSICAL_MEMORY_BASE;
        HostAddress = (size_t)EffectiveAddress+(size_t)PhysicalMemory-PHYSICAL_MEMORY_BASE;
    } else if (EffectiveAddress >= FLASH_BANK0_BASE && EffectiveAddress < FLASH_BANK0_BASE+sizeof(FlashBank0)) {
        // Attempts to read from physical 0...32mb are treated as reads from FlashBank0.
        // Attempts to write to that region are treated as memory-mapped IO.
        *pHostAdjust = 0; // Use 0 instead of (size_t)FlashBank0 otherwise TLB established
                          // for reads will cause write to go directly to flash instead of
                          // through the flash controller
        HostAddress = (size_t)EffectiveAddress+(size_t)FlashBank0;
    } else if (PhysicalMemoryExtension && EffectiveAddress >= PHYSICAL_MEMORY_EXTENSION_BASE && EffectiveAddress < PHYSICAL_MEMORY_EXTENSION_BASE+PhysicalMemoryExtensionSize) {
        *pHostAdjust = (size_t)PhysicalMemoryExtension-PHYSICAL_MEMORY_EXTENSION_BASE;
        HostAddress = (size_t)EffectiveAddress + (size_t)PhysicalMemoryExtension-PHYSICAL_MEMORY_EXTENSION_BASE;
    } else {
        // Addresses outside of RAM are assumed to be in I/O space.
        *pHostAdjust = 0;
        BoardIOAddress = EffectiveAddress;
        return 0;
    }
    return HostAddress;
}

size_t __fastcall BoardMapGuestPhysicalToHostWrite(unsigned __int32 EffectiveAddress, size_t *pHostAdjust)
{
    size_t HostAddress;

    if (EffectiveAddress >= PHYSICAL_MEMORY_BASE && EffectiveAddress < PHYSICAL_MEMORY_BASE+sizeof(PhysicalMemory)) {
        *pHostAdjust = (size_t)PhysicalMemory-PHYSICAL_MEMORY_BASE;
        HostAddress = (size_t)EffectiveAddress+(size_t)PhysicalMemory-PHYSICAL_MEMORY_BASE;
    } else if (PhysicalMemoryExtension && 
               EffectiveAddress >= PHYSICAL_MEMORY_EXTENSION_BASE && EffectiveAddress < PHYSICAL_MEMORY_EXTENSION_BASE+PhysicalMemoryExtensionSize) {
        *pHostAdjust = (size_t)PhysicalMemoryExtension-PHYSICAL_MEMORY_EXTENSION_BASE;
        HostAddress = (size_t)EffectiveAddress+(size_t)PhysicalMemoryExtension-PHYSICAL_MEMORY_EXTENSION_BASE;
    } else {
        // Addresses outside of RAM are assumed to be in I/O space.
        *pHostAdjust = 0;
        BoardIOAddress = EffectiveAddress;
        return 0;
    }
    return HostAddress;
}


bool __fastcall BoardIsHostAddressInRAM(size_t HostAddress)
{
    if (HostAddress >= (size_t)PhysicalMemory && HostAddress < (size_t)PhysicalMemory+sizeof(PhysicalMemory)) {
        return true;
    } else if (PhysicalMemoryExtension && HostAddress >= (size_t)PhysicalMemoryExtension && HostAddress < (size_t)PhysicalMemoryExtension+PhysicalMemoryExtensionSize) {
        return true;
    }
    return false;
}

size_t __fastcall BoardMapGuestPhysicalToFlash(unsigned __int32 EffectiveAddress)
{
    if (EffectiveAddress >= FLASH_BANK0_BASE && EffectiveAddress < FLASH_BANK0_BASE+sizeof(FlashBank0)) {
        return (size_t)FlashBank0 + EffectiveAddress;
    }
    return 0;
}


void __fastcall BoardSaveState(StateFiler& filer)
{
    filer.LZWrite(reinterpret_cast<unsigned __int8*>(FlashBank0),sizeof(FlashBank0));
    filer.LZWrite(reinterpret_cast<unsigned __int8*>(PhysicalMemory),sizeof(PhysicalMemory));
    if (Configuration.PhysicalMemoryExtensionSize) {
        filer.LZWrite(reinterpret_cast<unsigned __int8*>(PhysicalMemoryExtension), PhysicalMemoryExtensionSize);
    }
}

bool BoardAllocatePhysicalMemoryExtension(void)
{
    if (!Configuration.PhysicalMemoryExtensionSize) {
        // Extra RAM is not configured - nothing to do
        return true;
    }
    if (Configuration.PhysicalMemoryExtensionSize > PHYSICAL_MEMORY_EXTENSION_MAX_SIZE/(1024*1024)) {
        // Invalid size - fail the allocation
        return false;
    }

    // Convert from megs to bytes
    PhysicalMemoryExtensionSize = Configuration.PhysicalMemoryExtensionSize*(1024*1024);

    // Range-check the value
    if (PhysicalMemoryExtensionSize > PHYSICAL_MEMORY_EXTENSION_MAX_SIZE) {
        return false;
    }

    // Make the allocation
    PhysicalMemoryExtension = VirtualAlloc(NULL, PhysicalMemoryExtensionSize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!PhysicalMemoryExtension) {
        return false;
    }
    return true;
}

void __fastcall BoardRestoreState(StateFiler& filer)
{
    filer.LZRead(reinterpret_cast<unsigned __int8*>(FlashBank0),sizeof(FlashBank0));
    filer.LZRead(reinterpret_cast<unsigned __int8*>(PhysicalMemory),sizeof(PhysicalMemory));
    if (Configuration.PhysicalMemoryExtensionSize) {
        if (!BoardAllocatePhysicalMemoryExtension()) {
            filer.setStatus(false);
            return;
        }
        filer.LZRead(reinterpret_cast<unsigned __int8*>(PhysicalMemoryExtension),PhysicalMemoryExtensionSize);
    }

    CpuSetInstructionPointer(Configuration.InitialInstructionPointer);
    CpuSetStackPointer(INITIAL_STACK_POINTER);  // set sp to an arbitrary address within physical RAM
}

// Pass the display size/depth data over to the BSP.
void __fastcall BoardWriteGuestArguments(WORD Width, WORD Height, WORD BitsPerPixel, bool SoftReset)
{
#if SUPPORT_42_BSP
    if ( !Configuration.Board64RamRegion && Configuration.InitialInstructionPointer != EBOOT_64DRAM_START)
    {
    // BOOT_ARGS are stored at FRAMEBUF_DMA_BASE+1mb-4k.  That way, eboot shouldn't
    // overwrite them when it writes its 320x240 splash screen, but they're still
    // stored within the frame buffer.
    const unsigned __int32 BOOT_ARGS_ADDRESS = (0x30000000 + 0x00100000 + 1024*1024-4096);
    PBOOT_ARGS pBootArgs = (PBOOT_ARGS)BoardMapGuestPhysicalToHostRAM(BOOT_ARGS_ADDRESS);
    ASSERT(pBootArgs);

    pBootArgs->dwSig = BOOTARG_SIG;
    pBootArgs->cxPhysicalScreen = Width;
    pBootArgs->cyPhysicalScreen = Height;
    pBootArgs->bppScreen = BitsPerPixel;
    pBootArgs->softReset = SoftReset;
    }
    else
    {
        Configuration.Board64RamRegion = true;
    }
#endif

#if SUPPORT_50_BSP
    // 0x30020800 is the physical address for IMAGE_SHARE_ARGS_UA_START (0xAC020000)
    BSP_ARGS *pBSPArgs = (BSP_ARGS*)BoardMapGuestPhysicalToHostRAM(0x30020000);

    ASSERT(pBSPArgs);
    pBSPArgs->ScreenSignature = BSP_SCREEN_SIGNATURE;
    pBSPArgs->ScreenWidth = Width;
    pBSPArgs->ScreenHeight = Height;
    pBSPArgs->ScreenBitsPerPixel = BitsPerPixel;
    pBSPArgs->EmulatorFlags = SoftReset;
    ASSERT( Configuration.DefaultRotationMode < 4 );
    pBSPArgs->ScreenOrientation = (unsigned __int8)Configuration.DefaultRotationMode;

    if (Configuration.PassiveKITL)
    {
        pBSPArgs->kitl.flags |= OAL_KITL_FLAGS_PASSIVE;
    }
#endif
}


bool __fastcall BoardPowerOn(void)
{
    unsigned __int32 *Addr;

    // Initialize the contents of virtual address 0x92001004 with the sequence of
    // instructions used to power down the CPU.
    Addr = &FlashBank0[1+0x1000/4];
    *Addr++ = 0xE5801000;
    *Addr++ = 0xE5823000;
    *Addr++ = 0xE5845000;
    *Addr++ = 0xEAFFFFFE;

    // Set the adjustment to 0 ( we want to avoid doing this in the main code path)
    BoardIOAddressAdj = 0;

    if (!CompletionPort.Initialize()) {
        return false;
    }

    // Allocate the extension RAM if it is required.  It may have been allocated already, during restorestate,
    // or may not be enabled.
    if (!PhysicalMemoryExtension) { // if it wasn't restored by restorestate...
        if (!BoardAllocatePhysicalMemoryExtension()) {
            return false;
        }
    }

    // Set up the CPU's configuration to match our expectations
    ProcessorConfig.ARM.PCStoreOffset = 12;
    ProcessorConfig.ARM.BaseRestoredAbortModel = 1;
    ProcessorConfig.ARM.MemoryBeforeWritebackModel = 1;

    // Power on the peripheral devices
    if (!PowerOnDevices()) {
        return false;
    }

    // Set up guest arguments now that the periperhals are powered on.  Before that point,
    // Configuration.Skin isn't set (the WinController sets it at poweron-time),
    // so we can't make the final decision about display size until now.
    WORD Width;
    WORD Height;
    WORD Depth;

    if (Configuration.isSkinFileSpecified() || Configuration.isVideoSpecified() ) {
        Width  = (WORD)Configuration.ScreenWidth;
        Height = (WORD)Configuration.ScreenHeight;
        Depth  = (WORD)Configuration.ScreenBitsPerPixel;
    } else {
        // Report the default SMDK2410 video dimensions to Configuration
        Configuration.ScreenWidth = Width = 240;
        Configuration.ScreenHeight = Height = 320;
        Configuration.ScreenBitsPerPixel = Depth = 16;
    }

    // Verify that the requested display size fits the within the video RAM
    if (Width < 64 || Width > 800 || Width%2 != 0) {
        ShowDialog( IDS_INVALID_SCREENWIDTH );
        return false;
    }
    if (Height < 64 || Height > 800 || Height%2 != 0) {
        ShowDialog( IDS_INVALID_SCREENHEIGHT );
        return false;
    }
    if (Depth != 16 && Depth != 24 && Depth != 32) {
        WCHAR DepthString[20];
        if (FAILED(StringCchPrintfW(DepthString, ARRAY_SIZE(DepthString), L"%d", Depth)))
        {
            DepthString[0] = '/0';
        }
        ShowDialog( ID_MESSAGE_INVALID_SCREENDEPTH, DepthString, L"");
        return false;
    }
    if (Height*Width*(Depth>>3) > 1024*1024) {
        // The SMDK2410 frame buffer is limited to 1mb by its config.bib
        ShowDialog( ID_MESSAGE_EXCEEDED_SCREENBUFFER, L"", L"");
        return false;
    }

    BoardWriteGuestArguments(Width, Height, Depth, false);

    return true;
}

bool __fastcall BoardLoadImage(const wchar_t *ImageFile)
{
    if (!Load_BIN_NB0_File(ImageFile)) {
        return false;
    }
    CpuSetInstructionPointer(Configuration.InitialInstructionPointer);
    CpuSetStackPointer(INITIAL_STACK_POINTER);  // set sp to an arbitrary address within physical RAM

    return true;
}

bool __fastcall BoardLoadSavedState(bool default_state)
{
    // The loading rules are as follows:
    // if /defaultsave is specified we check in the following sequence
    // 1) local user saved state
    // 2) global saved state
    // 3) cold boot bin file
    // if /s is specified we check in the following sequence
    // 1) cold boot bin file
    // 2) saved state specified on the command line

    bool result = false;

    if ( Configuration.VSSetup ) {
        SetSaveStateFileNameFromVMID(true);
        goto Exit;
    }

    if ( default_state && !Configuration.UseDefaultSaveState ) {
        goto Exit;
    }

    // If the local saved state exits do not attempt to restore from global saved state
    FILE * localStateFile = NULL;
    if ( !_wfopen_s( &localStateFile, Configuration.getSaveStateFileName(), L"r" ) ||
         !default_state )
    {
        if ( localStateFile != NULL )
            fclose(localStateFile);
        // Try to restore from the current saved state filename
        StateFiler filer;
        filer.Restore();
        result = filer.getStatus();
    }

    // If successful or if the global saved states are not enabled return
    if ( result || !Configuration.UseDefaultSaveState ) {
        goto Exit;
    }

    // Try to restore from a global state
    bool UsingGlobalState = false;

    HRESULT hr = SetSaveStateFileNameFromVMID(true);
    if (SUCCEEDED(hr))
    {
        FILE * stateFile = NULL;
        if (  !_wfopen_s( &stateFile, Configuration.getSaveStateFileName(), L"r" ) )
        {
            fclose(stateFile);
            // Read the configuration from the global file and compare it to
            // the current configuration if the two match use the global state
            Configuration.SurpressMessages = true;
            EmulatorConfig GlobalConfig;
            GlobalConfig.init();
            StateFiler filer;
            filer.Restore(&GlobalConfig);
            if (filer.getStatus() && GlobalConfig.equal(&Configuration, true, false) )
            {
                // We need to verify that the skin file will fit the LCD
                if ( LCDController.VerifyUpdate( &GlobalConfig, &Configuration ) )
                {
                    Configuration.setLoadImage(false);
                    Configuration.UseUpdatedSettings = true;
                    UsingGlobalState = true;
                    // Disable prompting on power on for unsafe devices - trust
                    // global saved states
                    Configuration.NoSecurityPrompt = true;
                }
            }
            Configuration.SurpressMessages = false;
        }
    } else {
        goto Exit;
    }

    if ( UsingGlobalState )
    {
        StateFiler filer;
        filer.Restore();
        result = filer.getStatus();
    }

    // Reset the file path for the saved file to the user path rather then global path
    Configuration.UseUpdatedSettings = false;
    hr = SetSaveStateFileNameFromVMID(false);
    if (FAILED(hr)) {
        result = false;
        goto Exit;
    }

Exit:
    return result;
}

void PerformSyscall(void)
{
    ASSERT(FALSE);
}

bool BoardParseCommandLine(int argc, __in_ecount(argc) wchar_t *argv[], class EmulatorConfig *pConfiguration,
                           ParseErrorStruct * ParseError)
{
    int i;
    unsigned __int8 * MacAddressBuffer;
    bool *SpecifiedAddress;

    // Reset the error flag
    ParseError->ParseError = 0;

    for (i=0; i<argc; ++i) {
        if (argv[i][0] != '/' && argv[i][0] != '-') {
        if (pConfiguration->getROMImageName()) {
            // Cannot have more than one ROM image name on the command line
            ParseError->setError( ID_MESSAGE_UNRECOGNIZED_ARGUMENT, &argv[i][0] );
            return false;
        }
        pConfiguration->setLoadImage(true);
        if (!pConfiguration->setROMImageName(argv[i])) {
            ParseError->setError( ID_MESSAGE_RESOURCE_EXHAUSTED, NULL );
            return false;
        }
        continue;
    }

    switch (tolower(argv[i][1])) {
#if FEATURE_GUI
        case 'a':
            if (_wcsicmp(&argv[i][1], L"AllowPassiveKitl") == 0) {
                pConfiguration->PassiveKITL = true;
            } else if (argv[i][2] == '\0') {
                pConfiguration->IsAlwaysOnTop = true;
            } else {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            break;
#endif // FEATURE_GUI

        case 'c':
            if (argv[i][2] != '\0') {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            pConfiguration->fCreateConsoleWindow = true;
            break;

#if FEATURE_SAVESTATE
        case 'd':
            if (_wcsicmp(&argv[i][1], L"defaultsave") == 0) {
                // Check if the saved state name is already set
                if (pConfiguration->getSaveStateFileName() != NULL )
                {
                    ParseError->setError( ID_MESSAGE_BOTH_SAVESTATE_DEFAULT, NULL );
                    return false;
                }
                pConfiguration->UseDefaultSaveState = true;
                HRESULT hr = SetSaveStateFileNameFromVMID(false);
                if (FAILED(hr)) {
                    return false;
                }
            } else if (_wcsicmp(&argv[i][1], L"dialogonly") == 0) {
                pConfiguration->DialogOnly = true;
            } else {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            break;
#endif //#if FEATURE_SAVESTATE

        case 'l':
            if (_wcsicmp(&argv[i][1], L"language") == 0) {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    i++; // skip to the file name
                    if (!pConfiguration->setLanguage(&argv[i][0])) {
                        ParseError->setError( ID_MESSAGE_UNRECOGNIZED_LANGUAGE, &argv[i][0] );
                        return false;
                    } else {
                        pConfiguration->Resources.setLanguage(pConfiguration->getLangID());
                    }
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_LANGUAGE, NULL);
                    return false;
                }
            } else if (argv[i][2] == '\0') {
#if LOGGING_ENABLED
                LogToFile = true;
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    i++; // skip to the instruction count
                    wchar_t *EndCharacter;
                    LogInstructionStart= wcstoul(&argv[i][0], &EndCharacter, 16);
                    if (*EndCharacter != '\0') {
                        // Found some garbage characters along with the count
                        return false;
                    }
                }
#endif
            } else {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            break;

        case 'm':
            if (_wcsicmp(&argv[i][1], L"memsize") == 0 ) {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    wchar_t *EndCharacter;
                    unsigned __int32 MemSize;

                    i++; // skip to the next argument
                    MemSize= wcstoul(&argv[i][0], &EndCharacter, 10);
                    if (*EndCharacter != '\0') {
                        // Found some garbage characters along with the address
                        ParseError->setError( ID_MESSAGE_MALFORMED_MEMORY_SIZE, &argv[i][0] );
                        return false;
                    }
                    // The value is the desired RAM size in megabytes (64...256)
                    if (MemSize < PHYSICAL_MEMORY_SIZE/(1024*1024) || MemSize > (PHYSICAL_MEMORY_SIZE+PHYSICAL_MEMORY_EXTENSION_MAX_SIZE)/(1024*1024)) {
                        ParseError->setError( ID_MESSAGE_MALFORMED_MEMORY_SIZE, &argv[i][0] );
                        return false;
                    }
                    // Convert from requested total RAM size (64...256) to the extension RAM size (0...192mb)
                    pConfiguration->PhysicalMemoryExtensionSize = MemSize-(PHYSICAL_MEMORY_SIZE/(1024*1024)); 
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_MEMORY_SIZE, NULL );
                    return false;
                }
            } else {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            break;

        case 'n':
            if (_wcsicmp(&argv[i][1], L"nosecurityprompt") == 0 ) {
                // Disable the security prompt
                pConfiguration->NoSecurityPrompt = true;
            } else if (argv[i][2] == '\0') {
                pConfiguration->NetworkingEnabled = true;
                MacAddressBuffer = (unsigned __int8 *)&pConfiguration->SuggestedAdapterMacAddressCS8900;
                SpecifiedAddress= &(Configuration.SpecifiedCS8900Mac);
                goto ParseMACAddress;
            } else {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            break;

        case 'p':
            if (argv[i][2] != '\0') {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            pConfiguration->PCMCIACardInserted = true;
            MacAddressBuffer = (unsigned __int8 *)&pConfiguration->SuggestedAdapterMacAddressNE2000;
            SpecifiedAddress= &(Configuration.SpecifiedNE2000Mac);
ParseMACAddress:
            if ( (i+1) < argc && argv[i+1][0] != '/' && argv[i+1][0] != '-') {
                i++; // skip to the MAC address argument
                // Make sure that the string has the right length for a MAC address
                if (wcslen(&argv[i][0]) != 12 )
                {
                    ParseError->setError( ID_MESSAGE_MALFORMED_MAC_ADDRESS, &argv[i][0] );
                    return false;
                }

                for (int b_i = 0; b_i < 6; b_i++ )
                {
                    wchar_t temp;
                    temp = argv[i][(b_i+1)*2];
                    argv[i][(b_i+1)*2] = '\0';
                    MacAddressBuffer[b_i] = (unsigned __int8)wcstoul(&argv[i][b_i*2], NULL, 16);
                    argv[i][(b_i+1)*2] = temp;

                    if ( MacAddressBuffer[b_i] != 0 )
                        *SpecifiedAddress = true;
                }
            }
            break;

        case 'h':
            if (_wcsicmp(&argv[i][1], L"hostkey") == 0 ) {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    int j;

                    i++; // advance to the next argument
                    for (j=ARRAY_SIZE(HostKeys)-1; j >= 0; --j) {
                        if (_wcsicmp(&argv[i][0], HostKeys[j].Name) == 0) {
                            pConfiguration->setKeyboardState(HostKeys[j].Value, 0);
                            break;
                        }
                    }
                    if (j < 0) {
                        ParseError->setError( ID_MESSAGE_UNRECOGNIZED_HOSTKEY, &argv[i][0] );
                        return false; // unknown "/hostkey keyname"
                    }
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_HOSTKEY, NULL);
                    return false;
                }
            } else if (argv[i][2] == '\0') {
                pConfiguration->setHostOnlyRouting(true);
            } else {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            break;

        case 'r':
            if (argv[i][2] == '\0') {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    wchar_t *EndCharacter;

                    i++; // skip to the next argument
                    pConfiguration->ROMBaseAddress = wcstoul(&argv[i][0], &EndCharacter, 16);
                    if (*EndCharacter != '\0') {
                        // Found some garbage characters along with the address
                        ParseError->setError( ID_MESSAGE_MALFORMED_ROM_ADDRESS, &argv[i][0] );
                        return false;
                    }
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_ROM_ADDRESS, NULL );
                    return false;
                }
#if FEATURE_COM_INTERFACE
            } else if (_wcsicmp(&argv[i][1], L"register") == 0 || _wcsicmp(&argv[i][1], L"regserver") == 0) {
                // "/Register" - register our COM interfaces
                if (!RegisterUnregisterServer(TRUE)) {
                    ParseError->setError( ID_MESSAGE_INTERNAL_ERROR, NULL );
                    return false;
                }
                exit(0);
                break;
#endif //!FEATURE_COM_INTERFACE
#if FEATURE_GUI
            } else if (_wcsicmp(&argv[i][1], L"rotate") == 0 ) {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    wchar_t *EndCharacter;

                    i++; // skip to the next argument
                    pConfiguration->RotationMode = (wcstoul(&argv[i][0], &EndCharacter, 10) / 90) % 4;
                    pConfiguration->DefaultRotationMode = pConfiguration->RotationMode;
                    if (*EndCharacter != '\0') {
                        // Found some garbage characters along with the rotation value
                        ParseError->setError( ID_MESSAGE_MALFORMED_ROTATION, &argv[i][0] );
                        return false;
                    }
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_ROTATE, NULL );
                    return false;
                }
#endif
            } else {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            break;

        case 'f':
            if (_wcsicmp(&argv[i][1], L"flash") == 0 ) {
                pConfiguration->setFlashEnabled(true);
                if ( (i+1) < argc && argv[i+1][0] != '/' && argv[i+1][0] != '-') {
                    i++; // skip to the next argument - the flash file name
                    wchar_t FullPath[MAX_PATH];
                    if (!_wfullpath(FullPath, &argv[i][0], MAX_PATH)) {
                        // Either the path is too long, or the drive letter wasn't valid, etc.
                        ParseError->setError( ID_MESSAGE_RESOURCE_EXHAUSTED, NULL );
                        return false;
                    }
                    FullPath[MAX_PATH - 1] = L'\0';
                    if (!pConfiguration->setFlashStateFile(FullPath)) {
                        ParseError->setError( ID_MESSAGE_RESOURCE_EXHAUSTED, NULL );
                        return false;
                    }
                }
            } else if (_wcsicmp(&argv[i][1], L"funckey") == 0) {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    wchar_t *EndCharacter;
                    unsigned __int32 KeyCode;

                    i++; // skip to the next argument
                    KeyCode = wcstoul(&argv[i][0], &EndCharacter, 10);
                    if (*EndCharacter != '\0') {
                        // Found some garbage characters along with the address
                        ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                        return false;
                    }
                    // Change the virtual key mapping
                    pConfiguration->FuncKeyCode = KeyCode;
                } else {
                    ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, NULL);
                    return false;
                }
            } else if (argv[i][2] == L'\0') {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    wchar_t *EndCharacter;

                    i++; // skip to the next argument - the processor feature value
                    pConfiguration->ProcessorFeatures = wcstoul(&argv[i][0], &EndCharacter, 10);
                    if (*EndCharacter != '\0' || pConfiguration->ProcessorFeatures > 3) {
                        // Found some garbage characters along with the address
                        ParseError->setError( ID_MESSAGE_MALFORMED_PROCESSOR_FEATURES, &argv[i][0] );
                        return false;
                    }
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_PROCESSOR_FEATURES, NULL );
                    return false;
                }
            } else {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            break;
#if FEATURE_SKIN
        case 't':
            if (_wcsicmp(&argv[i][1], L"tooltips") == 0 ) {
                // There must be at least one more argument
                if ( (i+1) < argc )
                    i++;
                else
                {
                    ParseError->setError( ID_MESSAGE_MISSING_TOOLTIPS_STATE, NULL );
                    return false; // Missing required tooltips state
                }
                // Read the requested state
                if (_wcsicmp(&argv[i][0], L"off") == 0)
                    pConfiguration->ToolTipState = ToolTipOff;
                else if (_wcsicmp(&argv[i][0], L"on") == 0)
                    pConfiguration->ToolTipState = ToolTipOn;
                else if (_wcsicmp(&argv[i][0], L"delay") == 0)
                    pConfiguration->ToolTipState = ToolTipDelay;
                else
                {
                    ParseError->setError( ID_MESSAGE_UNRECOGNIZED_TOOLTIPS_STATE, &argv[i][0] );
                    return false; // Unrecognized tooltips state
                }
            }
            else
            {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false; // Unrecognized argument
            }
            break;
#endif // FEATURE_SKIN
        case 's':
#if FEATURE_SAVESTATE
            if (argv[i][2] == '\0') {
                // Check if the default saved state name is already set
                if (pConfiguration->UseDefaultSaveState)
                {
                    ParseError->setError( ID_MESSAGE_BOTH_SAVESTATE_DEFAULT, NULL );
                    return false;
                }
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    i++; // skip ahead to the save-state filename
                    wchar_t FullPath[MAX_PATH];
                    if (!_wfullpath(FullPath, &argv[i][0], MAX_PATH)) {
                        // Either the path is too long, or the drive letter wasn't valid, etc.
                        ParseError->setError( ID_MESSAGE_RESOURCE_EXHAUSTED, NULL );
                        return false;
                    }
                    FullPath[MAX_PATH - 1] = L'\0';
                    if (!pConfiguration->setSaveStateFileName(FullPath)) {
                        ParseError->setError( ID_MESSAGE_RESOURCE_EXHAUSTED, NULL );
                        return false;
                    }
                    break;
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_SAVESTATE_NAME, NULL );
                    return false;
                }
            }
#endif //FEATURE_SAVESTATE
#if FEATURE_SKIN
            if (_wcsicmp(&argv[i][1], L"skin") == 0) {
                if (pConfiguration->ScreenWidth != 0)
                {
                    ParseError->setError( ID_MESSAGE_BOTH_SKIN_AND_VIDEO, NULL);
                    return false;
                }
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    i++; // skip to the file name
                    if (!pConfiguration->setSkinXMLFile(&argv[i][0])) {
                        ParseError->setError( ID_MESSAGE_RESOURCE_EXHAUSTED, NULL );
                        return false;
                    }
                    break;
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_SKIN_NAME, NULL);
                    return false;
                }
            }
#endif //FEATURE_SKIN
            if (_wcsicmp(&argv[i][1], L"sharedfolder") == 0) {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    i++; // advance to the next argument, the directory name
                    if (!pConfiguration->setFolderShareName(&argv[i][0])) {
                        ParseError->setError( ID_MESSAGE_RESOURCE_EXHAUSTED, NULL );
                        return false;
                    }
                    break;
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_FOLDERSHARE_NAME, NULL );
                    return false;
                }
            }
            ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
            return false;
            break;

        case 'u':
            if (argv[i][3] != '\0') {
#if FEATURE_COM_INTERFACE
               if (_wcsicmp(&argv[i][1], L"unregister") == 0 || _wcsicmp(&argv[i][1], L"unregserver") == 0) {
                   // "/Unregister" - unregister our COM interfaces
                   if (!RegisterUnregisterServer(FALSE)) {
                       ParseError->setError( ID_MESSAGE_INTERNAL_ERROR, NULL );
                       return false;
                   }
                   exit(0);
               }
#endif //!FEATURE_COM_INTERFACE
               ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
               return false;
            }
            if ((i+1) >= argc) {
                ParseError->setError( ID_MESSAGE_MISSING_UART_PORT, NULL );
                return false;
            }

            switch (argv[i][2]) {
            case '0':
            case '1':
            case '2':
                if (!pConfiguration->setUART(argv[i][2]-'0', &argv[i+1][0])) {

                    return false;
                }
                i++; // advance past the devicename argument
                break;
            default:
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            break;

        case 'v':
#if FEATURE_GUI
            if (_wcsicmp(&argv[i][1], L"video") == 0) {
                if (pConfiguration->isSkinFileSpecified())
                {
                    ParseError->setError( ID_MESSAGE_BOTH_SKIN_AND_VIDEO, NULL);
                    return false;
                }
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    i++; // skip the "/video" arg
                    CfgVideoRetType setVideoRetCode= pConfiguration->setVideo(&argv[i][0]);
                    if (setVideoRetCode == CFG_VIDEO_SUCCESS) {
                        pConfiguration->fShowSkin = false;
                    }
                    else
                    {
                        switch( setVideoRetCode )
                        {
                            case CFG_VIDEO_MALFORMED_VIDEO:
                                ParseError->setError( ID_MESSAGE_MALFORMED_VIDEO_SPEC, &argv[i][0] );
                                break;
                            case CFG_VIDEO_INVALID_WIDTH:
                                ParseError->setError( IDS_INVALID_SCREENWIDTH, &argv[i][0] );
                                break;
                            case CFG_VIDEO_INVALID_HEIGHT:
                                ParseError->setError( IDS_INVALID_SCREENHEIGHT, &argv[i][0] );
                                break;
                            case CFG_VIDEO_INVALID_DEPTH:
                                ParseError->setError( ID_MESSAGE_INVALID_SCREENDEPTH, &argv[i][0] );
                                break;
                            case CFG_VIDEO_EXCEEDS_BUFFER:
                                ParseError->setError( ID_MESSAGE_EXCEEDED_SCREENBUFFER, &argv[i][0] );
                                break;
                        }
                        return false;
                    }
                    break;
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_VIDEO, NULL );
                    return false;
                }
            }
#endif //FEATURE_GUI
            if (_wcsicmp(&argv[i][1], L"vmid") == 0) {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    i++; // skip the "/VMID" arg
                    if (!pConfiguration->setVMID(&argv[i][0])) {
                        ParseError->setError( ID_MESSAGE_MALFORMED_VMID, &argv[i][0]);
                        return false;
                    }
                    break;
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_VMID, NULL );
                    return false;
                }
            } else if (_wcsicmp(&argv[i][1], L"vmname") == 0) {
                if ((i+1) < argc && argv[i+1][0] != '-' && *argv[i+1] != '/') {
                    i++; // skip the "/VMName" arg
                    if (!pConfiguration->setVMIDName(&argv[i][0])) {
                        ParseError->setError( ID_MESSAGE_RESOURCE_EXHAUSTED, NULL );
                        return false;
                    }
                } else {
                    ParseError->setError( ID_MESSAGE_MISSING_VMNAME, NULL);
                    return false;
                }
                break;
            } else if (_wcsicmp(&argv[i][1], L"vssetup") == 0) {
                pConfiguration->VSSetup = true;
                break;
            } 
           
            ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
            return false;

#if FEATURE_GUI
        case 'z':
            if (argv[i][2] != '\0') {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            pConfiguration->IsZoomed = true;
            break;
#endif //FEATURE_GUI
        case '?':
            if (argv[i][2] != '\0') {
                ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
                return false;
            }
            return false;
            break;
        default:
            ParseError->setError( ID_MESSAGE_UNRECOGNIZED_OPTION, &argv[i][0] );
            return false;
        }
    }
    return true;
}

void BoardPrintErrorAndUsage(ParseErrorStruct * ParseError )
{
    // There is no reason to print usage if we failed due to out of memory
    if ( ParseError->ParseError == ID_MESSAGE_RESOURCE_EXHAUSTED ||
         ParseError->ParseError == ID_MESSAGE_INTERNAL_ERROR)
        ShowDialog(ParseError->ParseError, &ParseError->falting_arg );
    else
    {
        // Load the usage string
        wchar_t stringBuffer[MAX_LOADSTRING];
        if (Configuration.Resources.getString(ID_MESSAGE_USAGE, stringBuffer ))
        {
            ShowDialog(ParseError->ParseError, &ParseError->falting_arg, stringBuffer);
        }
    }
}


void BoardPrintUsage(void)
{
    ShowDialog(ID_MESSAGE_USAGE);
}

void BoardShowConfigDialog(HWND hwndParent)
{
    ShowConfigDialog(hwndParent);
}

// Note: this destroys pNewConfig as it runs.  It will update the global
// Configuration for all valid new settings.  Any new settings that aren't
// valid (ie. a device fails to accept the new setting), are discarded.
bool BoardUpdateConfiguration(EmulatorConfig *pNewConfig)
{
    bool bRet = true; // assume success

    // Copy in settings that don't require peripherals to be notified.
    // Do them first, so that HostOnlyRouting is set before any changes
    // to the network cards.
    Configuration.KeyboardToWindows = pNewConfig->KeyboardToWindows;
    Configuration.KeyboardSelector = pNewConfig->KeyboardSelector;
    Configuration.HostOnlyRouting = pNewConfig->HostOnlyRouting;
    // LangID not copied
    // InitialInstructionPointer not copied
    // Resources not copied
    Configuration.fCreateConsoleWindow = pNewConfig->fCreateConsoleWindow;
    free(Configuration.VMIDName);
    Configuration.VMIDName = pNewConfig->VMIDName;
    pNewConfig->VMIDName = NULL;
    // VMID not copied

    // For any options that have changed value, notify the affected peripheral
    // so that it can adjust to its new settings.  Notifications are made only
    // if fIsRunning == true.  Otherwise, the config values are updated without
    // any notification, since no peripheral is running.
    EnterCriticalSection(&IOLock);

    // If the user simply removes the skin the reconfigure is guaranteed to succeed because
    // the video sizes are not editable at runtime
    if ( pNewConfig->ScreenHeight && pNewConfig->ScreenWidth && pNewConfig->ScreenBitsPerPixel &&
         pNewConfig->SkinXMLFile == NULL )
    {
        Configuration.ScreenHeight = pNewConfig->ScreenHeight;
        Configuration.ScreenWidth  = pNewConfig->ScreenWidth;
        Configuration.ScreenBitsPerPixel = pNewConfig->ScreenBitsPerPixel;
    }

    if (SafeWcsicmp(Configuration.SkinXMLFile, pNewConfig->SkinXMLFile)) {
        if ( fIsRunning && !LCDController.Reconfigure(pNewConfig->SkinXMLFile)) {
            bRet = false;
        }
    }

    if (Configuration.fShowSkin != pNewConfig->fShowSkin) {
        LCDController.ShowSkin(pNewConfig->fShowSkin);
    }
    if (Configuration.RotationMode != pNewConfig->RotationMode) {
        LCDController.SetRotation(pNewConfig->RotationMode);
    }
    if (Configuration.IsZoomed != pNewConfig->IsZoomed) {
        LCDController.SetZoom(pNewConfig->IsZoomed);
    }
    if (Configuration.IsAlwaysOnTop != pNewConfig->IsAlwaysOnTop) {
        LCDController.SetTopmost(pNewConfig->IsAlwaysOnTop);
    }

    if (Configuration.ToolTipState != pNewConfig->ToolTipState) {
        LCDController.ToggleToolTip(pNewConfig->ToolTipState);
    }

    if (SafeWcsicmp(Configuration.UARTNames[0], pNewConfig->UARTNames[0])) {
        if (!fIsRunning || UART0.Reconfigure(pNewConfig->UARTNames[0])) {
            free(Configuration.UARTNames[0]);
            Configuration.UARTNames[0] = pNewConfig->UARTNames[0];
            pNewConfig->UARTNames[0] = NULL;
        } else {
            bRet = false;
        }
    }
    if (SafeWcsicmp(Configuration.UARTNames[1], pNewConfig->UARTNames[1])) {
        if (!fIsRunning || UART1.Reconfigure(pNewConfig->UARTNames[1])) {
            free(Configuration.UARTNames[1]);
            Configuration.UARTNames[1] = pNewConfig->UARTNames[1];
            pNewConfig->UARTNames[1] = NULL;
        } else {
            bRet = false;
        }
    }
    if (SafeWcsicmp(Configuration.UARTNames[2], pNewConfig->UARTNames[2])) {
        if (!fIsRunning || UART2.Reconfigure(pNewConfig->UARTNames[2])) {
            free(Configuration.UARTNames[2]);
            Configuration.UARTNames[2] = pNewConfig->UARTNames[2];
            pNewConfig->UARTNames[2] = NULL;
        } else {
            bRet = false;
        }
    }
    if (SafeWcsicmp(Configuration.FolderShareName, pNewConfig->FolderShareName)) {
        if (!fIsRunning || FolderSharing.Reconfigure(pNewConfig->FolderShareName)) {
            free(Configuration.FolderShareName);
            Configuration.FolderShareName = pNewConfig->FolderShareName;
            pNewConfig->FolderShareName = NULL;
        } else {
            bRet = false;
        }
    }

    if (Configuration.PCMCIACardInserted != pNewConfig->PCMCIACardInserted ) {
        if ( fIsRunning && !CONTROLPCMCIA.Reconfigure((pNewConfig->PCMCIACardInserted) ? L"1" : L"0")) {
            bRet = false;
        }
    }
    LeaveCriticalSection(&IOLock);

    if (!fIsRunning) {
        // These can't change while the emulator is running
        free(Configuration.FlashStateFile);
        Configuration.FlashStateFile = pNewConfig->FlashStateFile;
        pNewConfig->FlashStateFile = NULL;
        Configuration.FlashEnabled = pNewConfig->FlashEnabled;
        Configuration.ScreenHeight = pNewConfig->ScreenHeight;
        Configuration.ScreenWidth = pNewConfig->ScreenWidth;
        Configuration.ScreenBitsPerPixel = pNewConfig->ScreenBitsPerPixel;

        free(Configuration.ROMImageName);
        Configuration.ROMImageName = pNewConfig->ROMImageName;
        pNewConfig->ROMImageName = NULL;

        Configuration.ROMBaseAddress = pNewConfig->ROMBaseAddress;
        Configuration.ProcessorFeatures = pNewConfig->ProcessorFeatures;
        Configuration.NetworkingEnabled = pNewConfig->NetworkingEnabled;
        Configuration.setHostMacAddressCS8900(pNewConfig->SuggestedAdapterMacAddressCS8900);
        Configuration.setHostMacAddressNE2000(pNewConfig->SuggestedAdapterMacAddressNE2000);
        Configuration.PhysicalMemoryExtensionSize = pNewConfig->PhysicalMemoryExtensionSize;
    }

    // Free resources owned by pNewConfig
    free(pNewConfig->SkinXMLFile);
    free(pNewConfig->FlashStateFile);
    for (unsigned int i=0; i<ARRAY_SIZE(pNewConfig->UARTNames); ++i) {
        free(pNewConfig->UARTNames[i]);
    }
    free(pNewConfig->ROMImageName);
    free(pNewConfig->VMIDName);
    free(pNewConfig->FolderShareName);

    return bRet;
}

// Called by Load_BIN_NB0_File() to map VAs back to PAs during .bin image load
unsigned __int32 __fastcall BoardMapVAToPA(ROMHDR *pROMHDR, unsigned __int32 VA)
{
    // If we didn't find a valid ROM header assume the old layout
    if (pROMHDR && pROMHDR->ulRAMStart >= 0x92000000) {

        // WinCE 5.0 UI NOR flash image 
        // VA         PA         Length
        // ---------- ---------- ------
        // 0x92000000 0x00000000 32mb
        Configuration.Board64RamRegion = true;
        return VA - (0x92000000);
    } else if (pROMHDR && pROMHDR->ulRAMStart >= 0x8c000000) {

        // WinCE 4.2 style memory layout for SMDK2410
        // VA         PA         Length
        // ---------- ---------- ------
        // 0x80000000 0x32000000 32mb
        // 0x8c000000 0x30000000 32mb

        return VA - (0x80000000-0x32000000);
    } else {

        // WinCE Bowmore style memory layout for SMDK2410
        // VA         PA         Length
        // ---------- ---------- ------
        // 0x80000000 0x30000000 64mb
        Configuration.Board64RamRegion = true;
        return VA - (0x80000000-0x30000000);
    }
}
