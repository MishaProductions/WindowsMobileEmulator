/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef _EMULATOR_H_INCLUDED
#define _EMULATOR_H_INCLUDED

#include "heap.h"

#define _WIN32_WINNT 0x0400 // Require NT4 or later
//#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#if (defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_))
    #error require winsock2.h -- include <winsock2.h> before you include <windows.h>
#endif

// This prevents GetLastError from being called twice in a construct like this:
// HRESULT hr = HRESULT_FROM_WIN32(::GetLastError());
#define INLINE_HRESULT_FROM_WIN32

#include <windows.h>
#include <msxml2.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <tchar.h>
// StrSafe uses depreciated APIs
#pragma warning(push)
#pragma warning(disable:4996)
#include <strsafe.h>
#pragma warning(pop)

#ifndef DISABLE_CODEMARKERS
// Code markers are only applicable to VS performance testing
#include "codemarkers.h"
#else
// Define the codemarker call to nothing to remove the dependency
#define InitPerformanceDLL(x, y)
#define UninitializePerformanceDLL(x)
#define CodeMarker(x)
#endif

// Call exit() instead of ExitProcess() within the DeviceEmulator:  it
// depends on atexit callbacks being executed.
#define ExitProcess(x) __Do_Not_Call_ExitProcess__Call_exit_instead=(x)

#pragma warning (disable:4127) // conditional expression is constant (hit in ASSERT macro)
#pragma warning (disable:4100) // unreferenced formal paramter (argc/argv in main)
#pragma warning (disable:4310) // cast truncates constant value (Emit_AND_Reg_Imm32 and Emit_CMP_Reg_Imm32 in place.h)
#pragma warning (disable:4702) // unreachable code

#ifndef _PREFAST_                //Enables use of pragma prefast, since the pragma is an extension
                                //of Prefast's AST compilier
#pragma warning(disable:4068)

#endif
#pragma warning(disable:4800)

#ifdef USE_WRAPPERS_FOR_SAFECRT
#include "safecrtwrappers.h"
#pragma warning(disable:4189)
#include "specstrings.h"
#endif

#define EMULATOR_NAME_W  L"Device Emulator"
#define EMULATOR_NAME_A  "Device Emulator"

#define EMULATOR_REGPATH L"Software\\Microsoft\\" ## EMULATOR_NAME_W

#define HELP_FILE_W      L"DeviceEmulator.chm"

// Set this to 1, and the JIT will increment a count stored within each ENTRYPOINT when the basic block
// is entered.  It can be used to identify the most-executed basic blocks.  At each cache flush,
// code in entrypt.cpp dumps the top 10 most-executed blocks.
#define ENTRYPOINT_HITCOUNTS 0

#ifdef DEBUG
extern void __fastcall AssertionFailed(const char *Expression, const char *FileName, const int LineNumber);
#define ASSERT(expr) \
    if (!(expr)) { \
        AssertionFailed(#expr, __FILE__, __LINE__); \
    }

#else
#define ASSERT(expr)
#endif

__declspec(noreturn) void __fastcall TerminateWithMessage(unsigned __int32 MsgID);

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

extern CRITICAL_SECTION IOLock;
extern HANDLE hIdleEvent;

extern bool fIsRunning;
extern bool bSaveState;
extern bool bIsRestoringState;

class EmulatorConfig;
extern EmulatorConfig Configuration;



//#define LOGGING_ENABLED 1

#if LOGGING_ENABLED
extern unsigned __int32 InstructionCount;
extern unsigned __int32 LogInstructionStart;
extern bool LogToFile;
extern FILE *output;

//#define LogIf if (Cpu.CPSR.Bits.Mode == UserModeValue && InstructionCount >= LogInstructionStart)
#define LogIf if(InstructionCount >= LogInstructionStart && LogToFile)
#define LogPrint(x) LogIf { fprintf x;}
#define LogPlace(x) { CodeLocation = PlaceLoggingHelper x; }
#else
#define LogIf if (false)
#define LogPrint(x)
#define LogPlace(x)
#endif

extern "C" void __cdecl ShowDialog(unsigned int Expression, ...);
bool __cdecl PromptToAllow(unsigned int Expression, ...);
int __cdecl PromptToAllowWithCancel(unsigned int Expression, ...);
void __fastcall DecodeLastError( unsigned int ErrorCode, int buffSize, __out_ecount(buffSize) wchar_t *stringBuffer);
int SafeWcsicmp(const wchar_t *s1, const wchar_t *s2);
wchar_t * RemoveEscapes( __inout_z wchar_t * input );


#if FEATURE_COM_INTERFACE
bool RegisterUnregisterServer(BOOL bRegister);
#endif

// Asserts that the lock is held by the current thread
#define ASSERT_CRITSEC_OWNED(LockValue)    ASSERT(HandleToUlong((LockValue).OwningThread) == GetCurrentThreadId());


#endif //!_EMULATOR_H_INCLUDED
