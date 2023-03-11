/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#pragma once

enum EARMExceptionType {
	etReset,
	etUndefinedInstruction,
	etDataAbort,
	etPrefetchAbort,
    etDebuggerInterrupt
};

bool DebugHardwareBreakpointPresent(unsigned __int32 InstructionPointer);
PVOID DebugEntry(unsigned __int32 InstructionPointer, EARMExceptionType etReason);
PVOID SingleStepEntry(unsigned __int32 InstructionPointer);
