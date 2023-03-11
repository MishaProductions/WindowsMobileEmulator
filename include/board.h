/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef BOARD__H_
#define BOARD__H_

#ifdef USE_WRAPPERS_FOR_SAFECRT
#include "specstrings.h"
#endif

struct ParseErrorStruct {
    int ParseError;
    wchar_t falting_arg[MAX_PATH];

    void setError( int Error, __inout_z __maybenull wchar_t * arg )
    {
        ParseError = Error;
        // Copy the argument into the buffer
        if ( arg == NULL || FAILED(StringCchPrintfW(falting_arg, ARRAY_SIZE(falting_arg), L"%s",arg)) )
            falting_arg[0] = 0; // empty string
    }
};

extern unsigned __int32 BoardIOAddress;
extern unsigned __int32 BoardIOAddressAdj;

extern const __int32 StatePlatform;

bool __fastcall BoardPowerOn(void);
size_t __fastcall BoardMapGuestPhysicalToHostRAM(unsigned __int32 EffectiveAddress);
size_t __fastcall BoardGetPhysicalRAMBase();
size_t __fastcall BoardMapGuestPhysicalToHost(unsigned __int32 EffectiveAddress, size_t *pHostAdjust);
size_t __fastcall BoardMapGuestPhysicalToHostWrite(unsigned __int32 EffectiveAddress, size_t *pHostAdjust);
bool __fastcall BoardIsHostAddressInRAM(size_t HostAddress);
size_t __fastcall BoardMapGuestPhysicalToFlash(unsigned __int32 EffectiveAddress);
bool __fastcall BoardLoadImage(const wchar_t *ImageFile);
bool __fastcall BoardLoadSavedState(bool default_state);
void __fastcall BoardSaveState(StateFiler& filer);
void __fastcall BoardRestoreState(StateFiler& filer);
void PerformSyscall(void);
bool BoardParseCommandLine(int argc, __in_ecount(argc) wchar_t *argv[], class EmulatorConfig *pConfiguration, ParseErrorStruct * ErrorStruct);
void BoardPrintUsage(void);
void BoardPrintErrorAndUsage(ParseErrorStruct * ParseError );
void BoardShowConfigDialog(HWND hwndParent);
bool BoardUpdateConfiguration(class EmulatorConfig *pNewConfig);
void BoardReset(bool hardReset);
void BoardSuspend(void); // this is async... the actual suspend may happen after this routine returns

unsigned __int32 __fastcall BoardMapVAToPA(struct ROMHDR *pROMHDR, unsigned __int32 VA);  // called from Load_BIN_NB0_File() while loading the .bin file into emulated RAM

unsigned __int8  __fastcall IOReadByte(__int8 *pIOIndexHint);
unsigned __int16 __fastcall IOReadHalf(__int8 *pIOIndexHint);
unsigned __int32 __fastcall IOReadWord(__int8 *pIOIndexHint);

void __fastcall IOWriteByte(unsigned __int8 Value, __int8 *pIOIndexHint);
void __fastcall IOWriteHalf(unsigned __int16 Value, __int8 *pIOIndexHint);
void __fastcall IOWriteWord(unsigned __int32 Value, __int8 *pIOIndexHint);

#endif //BOARD__H_
