/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

Declares the public interface to the CPU and MMU emulators.

--*/

#ifndef _CPU_H_INCLUDED
#define _CPU_H_INCLUDED

#include "DEComInterfaces.h"
#include "syscall.h"

// Simulates asserting the RESET pin on the CPU, resetting it to its initial state
bool CpuReset(void);

void __fastcall CpuSetInstructionPointer(unsigned __int32 InstructionPointer);
void __fastcall CpuSetStackPointer(unsigned __int32 StackPointer);
void __fastcall CpuSetResetPending(void);
void __fastcall CpuSetInterruptPending(void);
void __fastcall CpuClearInterruptPending(void);
CSystemCallbacks * __fastcall CpuGetSystemCallbacks(void);
void __fastcall CpuFlushCachedTranslations(void);
bool __fastcall CpuAreInterruptsEnabled();

// Begins CPU simulation.  This never returns, as simulation ends only when the
// process exits.
__declspec(noreturn) void CpuSimulate(void);

// CPU configuration specified by the board
typedef union {
	struct {
		unsigned __int32 PCStoreOffset:8;		// value to offset the PC by (generally 12 or 8)
		unsigned __int32 BaseRestoredAbortModel:1;	// 1 if "base restored" abort model, 0 for "base updated"
		unsigned __int32 MemoryBeforeWritebackModel:1;	// 1 if "str r0, [r0], r1" writes r0 to memory before updating
								//   r0, or 0 if the update happens before the memory write
		unsigned __int32 GenerateSyscalls:1;		// 1 if certain SWIs are treated as special syscalls
	} ARM;
} PROCESSOR_CONFIG;

extern PROCESSOR_CONFIG ProcessorConfig;


// ICE-like debugging interface to the CPU
HRESULT CpuGetDebuggerInterface(IDeviceEmulatorDebugger **ppDebugger);

#endif //!_CPU_H_INCLUDED
