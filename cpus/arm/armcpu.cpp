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
#include "Cpu.h"
#include "entrypt.h"
#include "ARMCpu.h"
#include "mmu.h"
#include "State.h"
#include "Board.h"
#include "vfp.h"
#include "entrypt.h"
#include "tc.h"
#include "place.h"
#include "state.h"
#include <io.h>
#include <fcntl.h>
#include <ShellAPI.h> // for CommandLineToArgvW()
#include "emdebug.h"
#include "resource.h"

/*
    These two defines allow two emulator processes to run side-by-side, with one confirming that its behavior is
    the same as the others.  Here is how to use the feature:
    1.  Uncomment GOOD_EMULATOR, and build an emulator.exe with LOGGING_ENABLED.  This is the known-good emulator.  Copy
        it to a temporary directory.
    2.  Comment out GOOD_EMULATOR and uncomment BAD_EMULATOR, and build an emulator.exe with LOGGING_ENABLED>  This is
        the known-bad emulator.
    3.  Run the known-good emulator - it will load the .bin file then pause until it hears from the "bad" emulator.
    4.  Run the known-bad emulator under a debugger.  After each emulated instruction, it allows the "good" emulator to
        advance one instruction, then the "bad" diffs the CPU structure and InstructionPointer value with the "bad".
        Any differences trigger a debug message and a DebugBreak() call.

    Note that this side-by-side scheme works only up to the first interrupt delivery, then the two emulators
    quickly diverge and never converge again.  You'll know when this happens if one of the InstructionPointer
    value is 0xffff????.
*/
//#define GOOD_EMULATOR 1
//#define BAD_EMULATOR 1

unsigned __int8* PlaceR15ModifiedHelper(unsigned __int8* CodeLocation, Decoded* d);

#if LOGGING_ENABLED && (GOOD_EMULATOR || BAD_EMULATOR)
HANDLE hEmulatorGoodDataReady;
HANDLE hEmulatorResumeGood;
void* pEmulatorGoodData;
#endif //LOGGING_ENABLED && (GOOD_EMULATOR || BAD_EMULATOR)

CRITICAL_SECTION InterruptLock; // See UpdateInterruptOnPoll() for documentation

HANDLE g_hDebuggerEvent; // Debugger interface support
bool g_fOptimizeCode = true;

static const __int32 Feature_LoadStoreDouble=1;
static const __int32 Feature_DSP=2;

typedef struct STACKPAIR{
    unsigned __int32 Guest;
    unsigned __int32 Native;
} STACKPAIR;

unsigned __int8 StackCount = 0;
STACKPAIR ShadowStack[256];        // {GuestReturnAddress, NativeReturnAddress }

#define MMU_FAULTADDRESS_OFFSET 0x14
#define MMU_PROCESSID_OFFSET 0x18
C_ASSERT(MMU_FAULTADDRESS_OFFSET == FIELD_OFFSET(MMU, FaultAddress));
C_ASSERT(MMU_PROCESSID_OFFSET == FIELD_OFFSET(MMU, ProcessId));

const unsigned __int32 FlagList[] = {
    FLAG_ZF,
    FLAG_CF,
    FLAG_NF,
    FLAG_VF,
    (FLAG_ZF|FLAG_CF),
    (FLAG_NF|FLAG_VF),
    (FLAG_ZF|FLAG_NF|FLAG_VF),
    NO_FLAGS
};

// Verify that the order of flags matches the order of bits in CPSR
C_ASSERT(FLAG_VF == 1 && FLAG_CF == 2 && FLAG_ZF == 4 && FLAG_NF == 8);

#if LOGGING_ENABLED
const char * CondStrings[] ={
    "(EQ) ",
    "(NE) ",
    "(CS) ",
    "(CC) ",
    "(MI) ",
    "(PL) ",
    "(VS) ",
    "(VC) ",
    "(HI) ",
    "(LS) ",
    "(GC) ",
    "(LT) ",
    "(GT) ",
    "(LE) ",
    "     ",
    "     "
};

const char *FlagStrings[] = {
    "      ",
    "(   V)",
    "(  C )",
    "(  CV)",
    "( Z  )",
    "( Z V)",
    "( ZC )",
    "( ZCV)",
    "(N   )",
    "(N  V)",
    "(N C )",
    "(N CV)",
    "(NZ  )",
    "(NZ V)",
    "(NZC )",
    "(NZCV)"
};
#endif

unsigned __int8* __fastcall PlaceIllegalCoproc(unsigned __int8* CodeLocation, Decoded *d);

typedef unsigned __int8* (__fastcall *PPlaceCoproc)(unsigned __int8* CodeLocation, Decoded* d);

const PPlaceCoproc PlaceCoprocDataTransfers[16] = {
    PlaceIllegalCoproc,                 // 0 - multiply/accumulate
    PlaceIllegalCoproc,                 // 1
    PlaceIllegalCoproc,                 // 2
    PlaceIllegalCoproc,              // 3
    PlaceIllegalCoproc,              // 4
    PlaceIllegalCoproc,              // 5
    PlaceIllegalCoproc,              // 6
    PlaceIllegalCoproc,              // 7
    PlaceIllegalCoproc,              // 8
    PlaceIllegalCoproc,              // 9
    PlaceIllegalCoproc, // PlaceVFPCoprocDataTransfer10,    // 10
    PlaceIllegalCoproc, // PlaceVFPCoprocDataTransfer11,    // 11
    PlaceIllegalCoproc,              // 12
    PlaceIllegalCoproc,              // 13
    PlaceIllegalCoproc,              // 14 - identify/control operations of the microarchitecture
    MMU::PlaceCoprocDataTransfer     // 15 - MMU
};

const PPlaceCoproc PlaceCoprocDataOperations[16] = {
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc, // PlaceVFPCoprocDataOperation10,
    PlaceIllegalCoproc, // PlaceVFPCoprocDataOperation11,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    MMU::PlaceCoprocDataOperation
};

const PPlaceCoproc PlaceCoprocRegisterTransfers[16] = {
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc, // PlaceVFPCoprocRegisterTransfer10,
    PlaceIllegalCoproc, // PlaceVFPCoprocRegisterTransfer11,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    PlaceIllegalCoproc,
    MMU::PlaceCoprocRegisterTransfer
};

typedef union _OPCODE {
    unsigned __int32 Word;

    struct {
        unsigned __int32 Reserved:25;
        unsigned __int32 InstructionClass:3;
        unsigned __int32 Cond:4;
    } Generic;

    struct {
        unsigned __int32 Any1:4;
        unsigned __int32 Reserved1:1; // must be 1
        unsigned __int32 Any2:20;
        unsigned __int32 Cond:4;
    } UndefinedExtension;  // See A3-28

    struct {
        unsigned __int32 Rm:4;
        unsigned __int32 Reserved1:4; // must be 9
        unsigned __int32 Rs:4;
        unsigned __int32 Rn:4; // note that the ARM docs show Rd here and Rn following, but
                               // MUL and MLA expect Rn at 12-15 and Rd at 16-19.
        unsigned __int32 Rd:4;
        unsigned __int32 S:1;
        unsigned __int32 op1:3;
        unsigned __int32 Reserved2:4; // must be 0
        unsigned __int32 Cond:4;
    } ArithmeticExtension; // See A3-29

    struct {
        unsigned __int32 Operand2:12;
        unsigned __int32 Rd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 Reserved2:1; // Must be 0
        unsigned __int32 Op1:2;
        unsigned __int32 Reserved3:5; // Must be 2 or 6
        unsigned __int32 Cond:4;
    } ControlExtension;

    struct {
        unsigned __int32 Rm:4;
        unsigned __int32 Reserved1:1; // must be 1
        unsigned __int32 op1:2;
        unsigned __int32 Reserved2:1; // must be 1
        unsigned __int32 Rs:4;
        unsigned __int32 Rd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 L:1;
        unsigned __int32 W:1;
        unsigned __int32 B:1;
        unsigned __int32 U:1;
        unsigned __int32 P:1;
        unsigned __int32 Reserved3:3; // must be 0
        unsigned __int32 Cond:4;
    } LoadStoreExtension;

    struct {
        unsigned __int32 Rm:4;
        unsigned __int32 Reserved1:1; // must be 1
        unsigned __int32 L:1;
        unsigned __int32 Reserved2:2; // must be 3
        unsigned __int32 Rs:4;
        unsigned __int32 Rd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 Reserved3:1; // must be 0
        unsigned __int32 W:1;
        unsigned __int32 I:1;
        unsigned __int32 U:1;
        unsigned __int32 P:1;
        unsigned __int32 Reserved4:3; // must be 0
        unsigned __int32 Cond:4;
    } DoubleLoadStoreExtension;

    struct {
        unsigned __int32 Offset:8;
        unsigned __int32 cp_num:4;
        unsigned __int32 CRd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 x1:1;
        unsigned __int32 Reserved1:1; // must be 0
        unsigned __int32 x2:1;
        unsigned __int32 Reserved2:5; // must be 24
        unsigned __int32 Cond:4;
    } CoprocessorExtension;

    struct {
        unsigned __int32 x1:4;
        unsigned __int32 opcode2:4;
        unsigned __int32 x2:12;
        unsigned __int32 opcode1:8;
        unsigned __int32 Cond:4; // must be 15
    } UnconditionalExtension;

    struct {
        unsigned __int32 Operand2:12;
        unsigned __int32 Rd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 S:1;
        unsigned __int32 Opcode:4;
        unsigned __int32 I:1;
        unsigned __int32 Reserved1:2;    // must be 0
        unsigned __int32 Cond:4;
    } DataProcessing;

    struct {
        unsigned __int32 Rd:4;
        unsigned __int32 Reserved1:24;
        unsigned __int32 Cond:4;
    } BranchExchange;

    struct {
        unsigned __int32 Rm:4;
        unsigned __int32 Reserved1:1;   // must be 1
        unsigned __int32 H:1;
        unsigned __int32 S:1;
        unsigned __int32 Reserved2:5;   // must be 1
        unsigned __int32 Rd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 L:1;
        unsigned __int32 W:1;
        unsigned __int32 Reserved3:1;
        unsigned __int32 U:1;
        unsigned __int32 P:1;
        unsigned __int32 Reserved4:3;   // must be 0
        unsigned __int32 Cond:4;
    } HalfWordSignedTransferRegister;

    struct {
        unsigned __int32 OffsetLow:4;
        unsigned __int32 Reserved1:1;   // must be 1
        unsigned __int32 H:1;
        unsigned __int32 S:1;
        unsigned __int32 Reserved2:1;   // must be 1
        unsigned __int32 OffsetHigh:4;
        unsigned __int32 Rd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 L:1;
        unsigned __int32 W:1;
        unsigned __int32 Reserved3:1;   // must be 1
        unsigned __int32 U:1;
        unsigned __int32 P:1;
        unsigned __int32 Reserved4:3;   // must be 0
        unsigned __int32 Cond:4;
    } HalfWordSignedTransferImmediate;

    struct {
        unsigned __int32 Offset:12;
        unsigned __int32 Rd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 L:1;
        unsigned __int32 W:1;
        unsigned __int32 B:1;
        unsigned __int32 U:1;
        unsigned __int32 P:1;
        unsigned __int32 I:1;
        unsigned __int32 Reserved1:2;    // must be 1
        unsigned __int32 Cond:4;
    } SingleDataTransfer;

    struct {
        unsigned __int32 Reserved1:4;
        unsigned __int32 Reserved2:1;    // must be 1
        unsigned __int32 Reserved3:20;
        unsigned __int32 Reserved4:3;    // must be 3;
        unsigned __int32 Cond:4;
    } Undefined;

    struct {
        unsigned __int32 RegisterList:16;
        unsigned __int32 Rn:4;
        unsigned __int32 L:1;
        unsigned __int32 W:1;
        unsigned __int32 S:1;
        unsigned __int32 U:1;
        unsigned __int32 P:1;
        unsigned __int32 Reserved1:3;    // must be 4
        unsigned __int32 Cond:4;
    } BlockDataTransfer;

    struct {
            signed __int32 Offset:24;
        unsigned __int32 L:1;
        unsigned __int32 Reserved1:3;    // must be 5
        unsigned __int32 Cond:4;
    } Branch;

    typedef struct _CoprocDataTransfer_tag{
        unsigned __int32 Offset:8;
        unsigned __int32 CPNum:4;
        unsigned __int32 CRd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 L:1;
        unsigned __int32 W:1;
        unsigned __int32 N:1;
        unsigned __int32 U:1;
        unsigned __int32 P:1;
        unsigned __int32 Reserved1:3;    // must be 6
        unsigned __int32 Cond:4;
    } CoprocDataTransferOpcode;

    typedef    struct _CoprocDataOperation_tag {
        unsigned __int32 CRm:4;
        unsigned __int32 Reserved1:1;    // must be 0
        unsigned __int32 CP:3;
        unsigned __int32 CPNum:4;
        unsigned __int32 CRd:4;
        unsigned __int32 CRn:4;
        unsigned __int32 CPOpc:4;
        unsigned __int32 Reserved2:4;    // must be 14
        unsigned __int32 Cond:4;
    } CoprocDataOperationOpcode;

    typedef struct _CoprocRegisterTransfer_tag {
        unsigned __int32 CRm:4;
        unsigned __int32 Reserved1:1;    // must be 1
        unsigned __int32 CP:3;
        unsigned __int32 CPNum:4;
        unsigned __int32 Rd:4;
        unsigned __int32 CRn:4;
        unsigned __int32 L:1;
        unsigned __int32 CPOpc:3;
        unsigned __int32 Reserved2:4;    // must be 14
        unsigned __int32 Cond:4;
    } CoprocRegisterTransferOpcode;

    CoprocDataTransferOpcode CoprocDataTransfer;

    CoprocDataOperationOpcode CoprocDataOperation;

    CoprocRegisterTransferOpcode CoprocRegisterTransfer;

    struct {
        unsigned __int32 Ignored:24;
        unsigned __int32 Reserved1:4;    // must be 15
        unsigned __int32 Cond:4;
    } SoftwareInterrupt;

    struct {
        unsigned __int32 Rm:4;
        unsigned __int32 Reserved1:1;
        unsigned __int32 X:1;
        unsigned __int32 Y:1;
        unsigned __int32 Reserved2:1;
        unsigned __int32 Rs:4;
        unsigned __int32 Rd:4;
        unsigned __int32 Rn:4;
        unsigned __int32 Reserved3:1; // must be 0
        unsigned __int32 Op1:2;
        unsigned __int32 Reserved4:5; // must be 2
        unsigned __int32 Cond:4;
    } DSPExtension;

} OPCODE, *POPCODE;

C_ASSERT(sizeof(OPCODE)==4);

typedef union _THUMB_OPCODE {
    unsigned __int16 HalfWord;

    struct {
        unsigned __int16 Reserved:13;
        unsigned __int16 Opcode:3;
    } Generic;

    struct {
        unsigned __int16 Rd:3;
        unsigned __int16 Rs:3;
        unsigned __int16 Offset5:5;
        unsigned __int16 Op2:2;
        unsigned __int16 Opcode:3;
    } MoveShiftedRegister;

    struct {
        unsigned __int16 Rd:3;
        unsigned __int16 Rs:3;
        unsigned __int16 RnOffset:3;
        unsigned __int16 Op:1;
        unsigned __int16 I:1;
        unsigned __int16 Reserved:2; // must be 3
        unsigned __int16 Opcode:3;
    } AddSubtract;

    struct {
        unsigned __int16 Offset8:8;
        unsigned __int16 Rd:3;
        unsigned __int16 Op:2;
        unsigned __int16 Opcode:3;
    } MathImmediate;

    struct {
        unsigned __int16 Rd:3;
        unsigned __int16 Rs:3;
        unsigned __int16 Op:4;
        unsigned __int16 Reserved:3; // must be 0
        unsigned __int16 Opcode:3;
    } ALUOperation;

    struct {
        unsigned __int16 RdHd:3;
        unsigned __int16 RsHs:3;
        unsigned __int16 H2:1;
        unsigned __int16 H1:1;
        unsigned __int16 Op:2;
        unsigned __int16 Reserved:3; // must be 1
        unsigned __int16 Opcode:3;
    } HiOps;

    struct {
        unsigned __int16 Word8:8;
        unsigned __int16 Rd:3;
        unsigned __int16 Reserved:2; // must be 2
        unsigned __int16 Opcode:3;
    } PCRelativeLoad;

    struct {
        unsigned __int16 Rd:3;
        unsigned __int16 Rb:3;
        unsigned __int16 Ro:3;
        unsigned __int16 Reserved1:1; // must be 0
        unsigned __int16 B:1;
        unsigned __int16 L:1;
        unsigned __int16 Reserved2:1; // must be 1
        unsigned __int16 Opcode:3;
    } LoadStoreRegisterOffset;

    struct {
        unsigned __int16 Rd:3;
        unsigned __int16 Rb:3;
        unsigned __int16 Ro:3;
        unsigned __int16 Reserved1:1; // must be 1
        unsigned __int16 S:1;
        unsigned __int16 H:1;
        unsigned __int16 Reserved2:1; // must be 1
        unsigned __int16 Opcode:3;
    } LoadStoreByteHalfWord;

    struct {
        unsigned __int16 Rd:3;
        unsigned __int16 Rb:3;
        unsigned __int16 Offset5:5;
        unsigned __int16 L:1;
        unsigned __int16 B:1;
        unsigned __int16 Opcode:3;
    } LoadStoreImmediateOffset;

    struct {
        unsigned __int16 Rd:3;
        unsigned __int16 Rb:3;
        unsigned __int16 Offset5:5;
        unsigned __int16 L:1;
        unsigned __int16 Reserved1:1; // must be 0
        unsigned __int16 Opcode:3;
    } LoadStoreHalfWord;

    struct {
        unsigned __int16 Word8:8;
        unsigned __int16 Rd:3;
        unsigned __int16 L:1;
        unsigned __int16 Reserved1:1; // must be 1
        unsigned __int16 Opcode:3;
    } LoadStoreSPRelative;

    struct {
        unsigned __int16 Word8:8;
        unsigned __int16 Rd:3;
        unsigned __int16 SP:1;
        unsigned __int16 Reserved1:1; // must be 0;
        unsigned __int16 Opcode:3;
    } LoadAddress;

    struct {
        unsigned __int16 SWord7:7;
        unsigned __int16 S:1;
        unsigned __int16 Reserved1:5; // must be 16
        unsigned __int16 Opcode:3;
    } AddToStackPointer;

    struct {
        unsigned __int16 Rlist:8;
        unsigned __int16 R:1;
        unsigned __int16 Reserved1:2; // must be 2
        unsigned __int16 L:1;
        unsigned __int16 Reserved2:1; // must be 1
        unsigned __int16 Opcode:3;
    } PushPopRegisters;

    struct {
        unsigned __int16 Rlist:8;
        unsigned __int16 Rb:3;
        unsigned __int16 L:1;
        unsigned __int16 Reserved1:1; // must be 0
        unsigned __int16 Opcode:3;
    } MultipleLoadStore;

    struct {
          signed __int16 Soffset8:8;
        unsigned __int16 Cond:4;
        unsigned __int16 Reserved1:1; // must be 1
        unsigned __int16 Opcode:3;
    } ConditionalBranch;

    struct {
        unsigned __int16 Value8:8;
        unsigned __int16 Reserved1:5; // must be 31
        unsigned __int16 Opcode:3;
    } SoftwareInterrupt;

    struct {
          signed __int16 Offset11:11;
        unsigned __int16 Reserved1:2; // must be 0
        unsigned __int16 Opcode:3;
    } UnconditionalBranch;

    struct {
        unsigned __int16 Offset:11;
        unsigned __int16 H:2;
        unsigned __int16 Opcode:3;
    } LongBranch;
} THUMB_OPCODE, *PTHUMB_OPCODE;

C_ASSERT(sizeof(THUMB_OPCODE)==2);



// This array is located at address zero, and each value is an instruction
// opcode.  They are usually branches to the interrupt handler, though the
// FIQ routine can be simply stored at address 0x1c in memory to save the
// branch overhead.
typedef struct _InterruptVectors {
    unsigned __int32 ResetException;                // Supervisor mode on entry
    unsigned __int32 UndefinedInstructionException; // Undefined mode
    unsigned __int32 SoftwareInterruptException;    // Supervisor mode
    unsigned __int32 AbortPrefetchException;        // Abort mode
    unsigned __int32 AbortDataException;            // Abort mode
    unsigned __int32 Reserved;
    unsigned __int32 IRQException;                    // IRQ mode
} INTERRUPT_VECTORS, *PINTERRUPT_VECTORS;

// These variables cache the jitted destination address of the various interrupt
// vectors.  If the destination hasn't been jitted (or the cache was flushed
// since the last jit), then they are NULL.
PVOID NativeUndefinedInterruptAddress;
PVOID NativeSoftwareInterruptAddress;
PVOID NativeAbortPrefetchInterruptAddress;
PVOID NativeAbortDataInterruptAddress;
PVOID NativeIRQInterruptAddress;


C_ASSERT(sizeof(PSR)==4);
CPU Cpu;

#define MAX_INSTRUCTION_COUNT 100
unsigned __int32 NumberOfInstructions;
Decoded Instructions[MAX_INSTRUCTION_COUNT];

unsigned __int8* BigSkips1[MAX_INSTRUCTION_COUNT];
unsigned __int8* BigSkips2[MAX_INSTRUCTION_COUNT];
unsigned __int32 BigSkipCount;

// These are used within SingleDataTransfers, if they are of the form "LDR X, [R15+/-offset]".
// Within a basic block, the first "LDR" of this form stashes the result of the MMU::MapGuestToHost
// call in EDI, and subsequent ones can avoid calling MMU::MapGuestToHost by retrieving the
// value from EDI.  These two variables are used at jit-time to determine whether the
// cache mechanism can be used or not.
bool PCCachedAddressIsValid;
unsigned __int32 PCGuestEffectiveAddress;

unsigned __int8 *InterruptCheck;
void UpdateInterruptOnPoll();

//return a 32-bit representation of the ARM CPSR with updated flags
unsigned __int32 GetCPSRWithFlags() {

    unsigned __int32 NewPSR = Cpu.CPSR.Partial_Word;
    unsigned __int32 PSRMask = 0;
    
    PSRMask = (    (Cpu.x86_Overflow.Byte << PSR_OVERFLOW_FLAG)                //add Overflow to  mask
                | (Cpu.x86_Flags.Bits.CarryFlag << PSR_CARRY_FLAG)        //add Carry to mask
                | (Cpu.x86_Flags.Bits.ZeroFlag << PSR_ZERO_FLAG)            //add Zero to mask 
                | (Cpu.x86_Flags.Bits.SignFlag << PSR_NEGATIVE_FLAG));    //add Negative to mask

    //Keep all the original CPSR bits except for the last byte.
    //Combine saved bits with flag mask
    NewPSR = ((NewPSR & 0x0fffffff) | PSRMask);        
                                                    
    return NewPSR;
}

// Simulates setting the nRESET signal low
void CpuRaiseResetException(void)
{
    Cpu.ResetPending=0;
    Cpu.GPRs_svc[R14_svc] = Cpu.GPRs[R15];
    Cpu.SPSR_svc = GetCPSRWithFlags();
    Cpu.CPSR.Bits.Mode = SupervisorModeValue;
    Cpu.CPSR.Bits.ThumbMode = 0;
    Cpu.CPSR.Bits.FIQDisable = 1;
    EnterCriticalSection(&InterruptLock);
    Cpu.CPSR.Bits.IRQDisable = 1;
    UpdateInterruptOnPoll();
    LeaveCriticalSection(&InterruptLock);

    // Jump to the Reset vector.  Ideally we'd jump to address 0,
    // the Reset exception vector.  Unfortunately on SMDK2410, there
    // is no ARM instruction there (the value is DWORD 0) so it
    // causes an ARM crash.  Instead, jump back to the initial
    // instruction pointer from the kernel binary image.
    //Cpu.GPRs[R15] = offsetof(INTERRUPT_VECTORS, ResetException);
    Cpu.GPRs[R15] = Configuration.InitialInstructionPointer;

    Mmu.ControlRegister.Word=0; // reset the MMU too
    Mmu.ProcessId = 0;
}

bool CpuReset(void)
{
#if GOOD_EMULATOR
    // Run the good emulator first:  it creates the shared variables
    hEmulatorGoodDataReady = CreateEvent(NULL, TRUE, FALSE, L"Local\\EmulatorGoodDataReady");    // manual reset, initially clear
    hEmulatorResumeGood = CreateEvent(NULL, TRUE, FALSE, L"Local\\EmulatorResumeGood");            // manual reset, initially set
    if (hEmulatorGoodDataReady == NULL || hEmulatorResumeGood == NULL) {
        ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
        return false;
    }
    HANDLE hMapping = CreateFileMapping(NULL, NULL, PAGE_READWRITE, 0, 4096, L"Local\\EmulatorGoodData");
    if (hMapping == NULL) {
        ShowDialog(ID_MESSAGE_UNABLE_TO_OPEN, L"Local\\EmulatorGoodData");
        return false;
    }
    pEmulatorGoodData = MapViewOfFile(hMapping, FILE_MAP_WRITE, 0, 0, 4096);
    if (!pEmulatorGoodData) {
        ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
        return false;
    }
#elif BAD_EMULATOR
    hEmulatorGoodDataReady = OpenEvent(EVENT_ALL_ACCESS, FALSE, L"Local\\EmulatorGoodDataReady");
    hEmulatorResumeGood = OpenEvent(EVENT_ALL_ACCESS, FALSE, L"Local\\EmulatorResumeGood");
    if (hEmulatorGoodDataReady == INVALID_HANDLE_VALUE || hEmulatorResumeGood == INVALID_HANDLE_VALUE) {
        ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
        return false;
    }
    HANDLE hMapping = OpenFileMapping(FILE_MAP_READ, FALSE, L"Local\\EmulatorGoodData");
    if (hMapping == NULL) {
        ShowDialog(ID_MESSAGE_UNABLE_TO_OPEN,L"Local\\EmulatorGoodData");
        return false;
    }
    pEmulatorGoodData = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 4096);
    if (!pEmulatorGoodData) {
        ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
        return false;
    }
#endif

    g_hDebuggerEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset, inintially not set
    if (g_hDebuggerEvent == NULL) {
        return false;
    }

    EntrypointInitialize();
    Mmu.Initialize();

    if (!InitializeTranslationCache()) {
        return false;
    }

    InitializeCriticalSection(&InterruptLock);
    InterruptCheck = (unsigned __int8 *)VirtualAlloc(NULL, 4096, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!InterruptCheck) {
        return false;
    }
    unsigned __int8 *CodeLocation = InterruptCheck;
    unsigned __int8 *ReturnToCpuSimulate;
    unsigned __int8 *ReturnToTranslatedCode;
    // This code is CALLed.  If no interrupt is pending then the first byte is a RETN.
    // When an interrupt is pending, this code is patched, replacing the RETN by a NOP
    // causing a fall into the CpuRaiseIRQException codepath.
    Emit_RETN(0);                                       // RETN (1 byte)
    Emit_MOV_Reg_Imm32(EDX_Reg, etDebuggerInterrupt);   // MOV EDX, etDebuggerInterrupt
    Emit_CALL(DebugEntry);                              // CALL DebugEntry(ECX=GuestAddress, EDX=etDebuggerInterrupt)
    // If EAX == 0:  jump to CpuSimulate() and begin execution at Cpu.GPRS[R15]
    // If EAX == 1:  return back to the translation cache and continue execution
    // Else:         jump to code at the address in EAX
    Emit_CMP_Reg_Imm32(EAX_Reg, 1);                     // CMP EAX, 1
    Emit_JBLabel(ReturnToCpuSimulate);                  // JB ReturnToCpuSimulate - value is 0
    Emit_JZLabel(ReturnToTranslatedCode);               // JZ ReturnToTranslatedCode - value is 1
    // else value is the address to jump to.
    Emit_POP_Reg(ECX_Reg);                              // POP ECX - Pop the old return address from the stack
    Emit_JMP_Reg(EAX_Reg);                              // JMP EAX - Jump to the already-jitted native code
    FixupLabel(ReturnToCpuSimulate);                    // ReturnToCpuSimulate:
    Emit_POP_Reg(EAX_Reg);                              // POP EAX  - Pop the old return address from the stack then RET to CpuSimulate
    FixupLabel(ReturnToTranslatedCode);                 // ReturnToTranslatedCode:
    Emit_RETN(0);                                       // RETN
    FlushInstructionCache(GetCurrentProcess(), InterruptCheck, CodeLocation-InterruptCheck);

    Cpu.DebuggerInterruptPending=0;
    CpuRaiseResetException();    // set up the registers as if nRESET had been asserted
    return true;
}

void EnableInterruptOnPoll(void)
{
    ASSERT_CRITSEC_OWNED(InterruptLock);
    ASSERT((Cpu.CPSR.Bits.IRQDisable == 0 && Cpu.IRQInterruptPending) || Cpu.DebuggerInterruptPending);

    if (*InterruptCheck == 0xc3) { // currently RETN, indicating no interrupt is pending
        unsigned __int8 *CodeLocation = InterruptCheck;
        Emit8(0x90); // NOP
        ASSERT(CodeLocation - InterruptCheck == 1);
        FlushInstructionCache(GetCurrentProcess(), InterruptCheck, 1);
    }
}


void DisableInterruptOnPoll(void)
{
    ASSERT_CRITSEC_OWNED(InterruptLock);

    if (*InterruptCheck != 0xc3) { // currently POP EAX, indicating an interrupt is pending
        unsigned __int8 *CodeLocation = InterruptCheck;
        Emit_RETN(0);
        ASSERT(CodeLocation - InterruptCheck == 1);
        FlushInstructionCache(GetCurrentProcess(), InterruptCheck, 1);
    }
}

void UpdateInterruptOnPoll(void)
{
    // The update must be done while holding the InterruptLock to guarantee two
    // things:
    //
    // 1. That a peripheral device doesn't attempt to raise an interrupt 
    //    while we're sorting out what interrupt polling code to use
    // 2. That the CPU doesn't modify Cpu.CPSR.Bits.IRQDisable while
    //    a peripheral device is raising an interrupt
    ASSERT_CRITSEC_OWNED(InterruptLock);

    if (Cpu.DebuggerInterruptPending || (!Cpu.CPSR.Bits.IRQDisable && Cpu.IRQInterruptPending)) {
        EnableInterruptOnPoll();
    } else {
        DisableInterruptOnPoll();
    }
}


// Bank-switch the registers for a specific privileged mode
#define BankSwitchTo(ModeName) { \
const size_t RegCount = sizeof(Cpu.GPRs_##ModeName) / sizeof(unsigned __int32); \
unsigned __int32 Scratch[RegCount]; \
unsigned __int32 ScratchSPSR; \
memcpy(Scratch, &Cpu.GPRs[15-RegCount], sizeof(Scratch)); \
memcpy(&Cpu.GPRs[15-RegCount], &Cpu.GPRs_##ModeName[0], sizeof(Scratch)); \
memcpy(&Cpu.GPRs_##ModeName[0], Scratch, sizeof(Scratch)); \
ScratchSPSR = Cpu.SPSR.Word; \
Cpu.SPSR.Word = Cpu.SPSR_##ModeName; \
Cpu.SPSR_##ModeName = ScratchSPSR; \
}

void BankSwitch(void)
{
    // Bank-switch the GPRs according to the mode
    switch (Cpu.CPSR.Bits.Mode) {
    case UserModeValue:
    case SystemModeValue:
        break;

    case FIQModeValue:
        // do nothing. FIQ not supported.
        break;

    case SupervisorModeValue:
        BankSwitchTo(svc);
        break;

    case AbortModeValue:
        BankSwitchTo(abt);
        break;

    case IRQModeValue:
        BankSwitchTo(irq);
        break;

    case UndefinedModeValue:
        BankSwitchTo(und);
        break;

    default:
        ASSERT(FALSE); // unknown mode
        break;
    }
}

//Update the x86 Flags as well as the saturate flag bit.  Used in the MSR instruction
//where the control bits of the PSR are not altered.
void __fastcall UpdateFlags(unsigned __int32 NewFlagValue)
{
    //recreate the x86 Flags
    Cpu.x86_Flags.Bits.CarryFlag  = ((NewFlagValue >> PSR_CARRY_FLAG) & 0x1);
    Cpu.x86_Flags.Bits.SignFlag  = ((NewFlagValue >> PSR_NEGATIVE_FLAG) & 0x1);
    Cpu.x86_Flags.Bits.ZeroFlag = ((NewFlagValue >> PSR_ZERO_FLAG) & 0x1);
    Cpu.x86_Overflow.Word = ((NewFlagValue >> PSR_OVERFLOW_FLAG) & 0x1);

    //Set the SaturateFlag
    Cpu.CPSR.Bits.SaturateFlag = ((NewFlagValue >> PSR_SATURATE_FLAG) & 0x1 );
    
}
// Updates the CPSR along with an x86 flags reconstruction. 
//If the CPSR.Flags.Mode changes, then bank-switch registers into place.
void UpdateCPSRWithFlags(PSR_FULL NewPSR) {
    
    bool NeedsBankSwitch;
        
    if (Cpu.CPSR.Bits.Mode != NewPSR.Bits.Mode) {
        // Need to perform a mode switch - restore the usermode registers into the GPRs
        BankSwitch();
        NeedsBankSwitch = true;
    } else {
        NeedsBankSwitch = false;
    }

    if (NewPSR.Bits.ThumbMode && !Cpu.CPSR.Bits.ThumbMode) {
        // If we're switching into Thumb mode, clear the low bit in the
        // instruction pointer.
        Cpu.GPRs[R15] &= 0xfffffffe;
    }

    if (Cpu.CPSR.Bits.IRQDisable != NewPSR.Bits.IRQDisable) {
        // If were're changing the IRQDisable value, then the interrupt poll
        // code may require an update.
        EnterCriticalSection(&InterruptLock);
        Cpu.CPSR.Partial_Word = (NewPSR.Word & 0x0fffffff);
        UpdateInterruptOnPoll();
        LeaveCriticalSection(&InterruptLock);
    } else {
        Cpu.CPSR.Partial_Word = (NewPSR.Word & 0x0fffffff);
    }

    //recreate the x86 Flags
    Cpu.x86_Flags.Bits.CarryFlag  = NewPSR.Bits.CarryFlag;
    Cpu.x86_Flags.Bits.SignFlag  = NewPSR.Bits.NegativeFlag;
    Cpu.x86_Flags.Bits.ZeroFlag = NewPSR.Bits.ZeroFlag;
    Cpu.x86_Overflow.Word = NewPSR.Bits.OverflowFlag;
    
    if (NeedsBankSwitch) {
        // Need to perform a mode switch - swap in the new mode registers into the GPRs
        BankSwitch();
    }
}

// Updates the CPSR without a x86 flag reconstruction. 
// If the CPSR.Bits.Mode changes, then bank-switch registers into place.
void UpdateCPSR(PSR NewPSR) {
    bool NeedsBankSwitch;

    if (Cpu.CPSR.Bits.Mode != NewPSR.Bits.Mode) {
        // Need to perform a mode switch - restore the usermode registers into the GPRs
        BankSwitch();
        NeedsBankSwitch = true;
    } else {
        NeedsBankSwitch = false;
    }

    if (NewPSR.Bits.ThumbMode && !Cpu.CPSR.Bits.ThumbMode) {
        // If we're switching into Thumb mode, clear the low bit in the
        // instruction pointer.
        Cpu.GPRs[R15] &= 0xfffffffe;
    }

    if (Cpu.CPSR.Bits.IRQDisable != NewPSR.Bits.IRQDisable) {
        // If were're changing the IRQDisable value, then the interrupt poll
        // code may require an update.
        EnterCriticalSection(&InterruptLock);
        Cpu.CPSR.Partial_Word = NewPSR.Partial_Word;
        UpdateInterruptOnPoll();
        LeaveCriticalSection(&InterruptLock);
    } else {
        Cpu.CPSR.Partial_Word = NewPSR.Partial_Word;
    }

    if (NeedsBankSwitch) {
        // Need to perform a mode switch - swap in the new mode registers into the GPRs
        BankSwitch();
    }
}

// Converts the FieldMask encoded in Rn of an MSR instruction into a bitmask
unsigned __int32 ComputePSRMaskValue(int FieldMask)
{
    unsigned __int32 Mask;

    Mask = 0;
    if (FieldMask & 1) {
        Mask |= 0xff;
    }
    if (FieldMask & 2) {
        Mask |= 0xff00;
    }
    if (FieldMask & 4) {
        Mask |= 0xff0000;
    }
    if (FieldMask & 8) {
        Mask |= 0xff000000;
    }

    return Mask;
}

// This is used by the MSR opcode, to update a PSR based on an update mask and current CPU mode
// On entry,
//    EDX == FieldMask
//  ECX == NewPSRValue
//  stack == CurrentPSRValue
unsigned __int32 UpdatePSRMask(PSR_FULL CurrentPSRValue, unsigned __int32 NewPSRValue, unsigned __int32 Mask)
{
    if (Cpu.CPSR.Bits.Mode == UserModeValue) {
        // Only the flags can be updated from usermode:  all other bits are readonly.
        Mask &= 0xff000000;
    }
    
    CurrentPSRValue.Word = (CurrentPSRValue.Word & (~Mask)) |
                           (NewPSRValue & Mask);
    return CurrentPSRValue.Word;
}

// Handle an undefined opcode by entering the UNDEFINED mode
PVOID CpuRaiseUndefinedException(unsigned __int32 InstructionPointer)
{
    PSR NewPSR;
    PSR_FULL OldPSR;

    if (DebugEntry(InstructionPointer, etUndefinedInstruction) != (PVOID)1) {
        return 0;
    }

    LogPrint((output,"Raising Undefined Exception\n"));
    
    OldPSR.Word = GetCPSRWithFlags();        //Get the CPSR Value with updated flags
    NewPSR.Partial_Word = OldPSR.Word;
    NewPSR.Bits.Mode = UndefinedModeValue;  // Switch to undefined mode
    NewPSR.Bits.ThumbMode=0;                // Switch to ARM opcodes
    NewPSR.Bits.IRQDisable=1;               // Disable normal interrupts
    UpdateCPSR(NewPSR);
    Cpu.SPSR.Word = OldPSR.Word;
    Cpu.GPRs[R14] = InstructionPointer + ((OldPSR.Bits.ThumbMode) ? 2 : 4);

    // Jump to the UndefinedInstruction vector.
    Cpu.GPRs[R15] = offsetof(INTERRUPT_VECTORS, UndefinedInstructionException);
    if (Mmu.ControlRegister.Bits.M && Mmu.ControlRegister.Bits.V) {
        Cpu.GPRs[R15] |= 0xffff0000;
    }
    if (NativeUndefinedInterruptAddress == 0) {
        ENTRYPOINT *ep;

        ep = EPFromGuestAddrExact(Cpu.GPRs[R15]);
        if (ep) {
            NativeUndefinedInterruptAddress = ep->nativeStart;
        }
    }
    return NativeUndefinedInterruptAddress;
}

unsigned __int8* PlaceRaiseUndefinedException(unsigned __int8* CodeLocation, Decoded* d)
{
    unsigned __int8* NotInCache;

    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);    // MOV ECX, GuestAddress
    Emit_CALL(CpuRaiseUndefinedException);            // CALL CpuRaiseUndefinedException
    Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);            // TEST EAX, EAX
    Emit_JZLabel(NotInCache);                        // JZ NotInCache
    Emit8(0xff); EmitModRmReg(3,EAX_Reg,4);            // JMP EAX
    FixupLabel(NotInCache);                            // NotInCache:                    
    Emit_RETN(0);                                    // RETN

    return CodeLocation;
}

unsigned __int8* PlaceRaiseAbortPrefetchException(unsigned __int8* CodeLocation, Decoded* d)
{
    unsigned __int8* NotInCache;

    // Replay the MMU state that goes along with the exception
    Emit_MOV_Reg_Imm32(EAX_Reg, d->Immediate);
    Emit_MOV_DWORDPTR_Reg(&Mmu.FaultAddress, EAX_Reg);    // Mmu.FaultAddress = d->Immediate

    Emit_MOV_Reg_Imm32(EAX_Reg, d->Reserved3);
    Emit_MOV_DWORDPTR_Reg(&Mmu.FaultStatus, EAX_Reg);    // Mmu.FaultStatus = d->Reserved3

    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);
    Emit_CALL(CpuRaiseAbortPrefetchException);            // CALL CpuRaiseAbortPrefetchException
    Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                // TEST EAX, EAX
    Emit_JZLabel(NotInCache);                            // JZ NotInCache
    Emit8(0xff); EmitModRmReg(3,EAX_Reg,4);                // JMP EAX
    FixupLabel(NotInCache);                                // NotInCache:                    
    Emit_RETN(0);                                        // RETN

    return CodeLocation;
}

PVOID CpuRaiseAbortDataException(unsigned __int32 InstructionPointer)
{
    PSR NewPSR;
    PSR_FULL OldPSR;

    if (DebugEntry(InstructionPointer, etDataAbort) != (PVOID)1) {
        return 0;
    }

    LogPrint((output,"Raising Abort Data Exception\n"));
    OldPSR.Word = GetCPSRWithFlags();        //Get the CPSR Value with updated flags
    NewPSR.Partial_Word = OldPSR.Word;
    NewPSR.Bits.Mode = AbortModeValue;      // Switch to abort data mode
    NewPSR.Bits.ThumbMode=0;                // Switch to ARM opcodes
    NewPSR.Bits.IRQDisable=1;               // Disable normal interrupts
    UpdateCPSR(NewPSR);
    Cpu.SPSR.Word = OldPSR.Word;            // Set the SPSR
    Cpu.GPRs[R14] = InstructionPointer+8;    // R14=faulting address + 8 (not the usual +4 for thumb mode)

    // Jump to the AbortData vector.
    Cpu.GPRs[R15] = offsetof(INTERRUPT_VECTORS, AbortDataException);
    if (Mmu.ControlRegister.Bits.M && Mmu.ControlRegister.Bits.V) {
        Cpu.GPRs[R15] |= 0xffff0000;
    }

    if (NativeAbortDataInterruptAddress == 0) {
        ENTRYPOINT *ep;

        ep = EPFromGuestAddrExact(Cpu.GPRs[R15]);
        if (ep) {
            NativeAbortDataInterruptAddress = ep->nativeStart;
        }
    }
    return NativeAbortDataInterruptAddress;
}

// on entry, ECX == GuestAddress.  This routine never returns:  callers must JMP to it.
__declspec(naked) void __fastcall RaiseAbortDataExceptionHelper(void)
{
    __asm {
        call    CpuRaiseAbortDataException
        test    eax, eax
        jz        NotInCache
        jmp        eax
NotInCache:
        retn
    }
}

PVOID CpuRaiseAbortPrefetchException(unsigned __int32 InstructionPointer)
{
    PSR NewPSR;
    PSR_FULL OldPSR;
    if (DebugEntry(InstructionPointer, etPrefetchAbort) != (PVOID)1) {
        return 0;
    }

    LogPrint((output,"Raising Abort Prefetch Exception\n"));

    OldPSR.Word = GetCPSRWithFlags();        //Get CPSR with Flags
    NewPSR.Partial_Word = OldPSR.Word;
    NewPSR.Bits.Mode = AbortModeValue;      // Switch to abort data mode
    NewPSR.Bits.ThumbMode=0;                // Switch to ARM opcodes
    NewPSR.Bits.IRQDisable=1;               // Disable normal interrupts
    UpdateCPSR(NewPSR);
    Cpu.SPSR.Word = OldPSR.Word;            //Set the SPSR
    Cpu.GPRs[R14] = InstructionPointer+4; // R14=faulting address + 4 (not the usual 8)

    // Jump to the AbortPrefetch vector.
    Cpu.GPRs[R15] = offsetof(INTERRUPT_VECTORS, AbortPrefetchException);
    if (Mmu.ControlRegister.Bits.M && Mmu.ControlRegister.Bits.V) {
        Cpu.GPRs[R15] |= 0xffff0000;
    }

    if (NativeAbortPrefetchInterruptAddress == 0) {
        ENTRYPOINT *ep;

        ep = EPFromGuestAddrExact(Cpu.GPRs[R15]);
        if (ep) {
            NativeAbortPrefetchInterruptAddress = ep->nativeStart;
        }
    }
    return NativeAbortPrefetchInterruptAddress;
}

PVOID CpuRaiseIRQException(unsigned __int32 InstructionPointer)
{
    PSR NewPSR;
    PSR_FULL OldPSR;

    LogPrint((output,"Raising IRQ Exception\n"));

    ASSERT(Cpu.CPSR.Bits.IRQDisable == 0);

    if (Cpu.ResetPending) {
        CpuRaiseResetException();
        return NULL; // return back to CpuSimulate()
    }
    
    OldPSR.Word = GetCPSRWithFlags();        //Get the CPSR with Flags bits
    NewPSR.Partial_Word = OldPSR.Word;
    NewPSR.Bits.Mode = IRQModeValue;        // Switch to IRQ mode
    NewPSR.Bits.ThumbMode=0;                // Switch to ARM opcodes
    NewPSR.Bits.IRQDisable=1;               // Disable normal interrupts
    UpdateCPSR(NewPSR);
    Cpu.SPSR.Word = OldPSR.Word;
    Cpu.GPRs[R14] = InstructionPointer+4; // R14=instruction to restart+4

    // Jump to the IRQ vector.
    Cpu.GPRs[R15] = offsetof(INTERRUPT_VECTORS, IRQException);
    if (Mmu.ControlRegister.Bits.M && Mmu.ControlRegister.Bits.V) {
        Cpu.GPRs[R15] |= 0xffff0000;
    }

    if (NativeIRQInterruptAddress == 0) {
        ENTRYPOINT *ep;

        ep = EPFromGuestAddrExact(Cpu.GPRs[R15]);
        if (ep) {
            NativeIRQInterruptAddress = ep->nativeStart;
        }
    }
    return NativeIRQInterruptAddress;
}

#if LOGGING_ENABLED
void __fastcall JitLoggingHelper(const char* Message)
{
    LogIf {
        fputs(Message, output);
    }
}

unsigned __int8* PlaceLoggingHelper(unsigned __int8* CodeLocation, const char* Message, ...)
{
    unsigned __int8* SkipString;
    unsigned __int8 *StringAddress;
    HRESULT hr;
    va_list args;
    va_start(args, Message);

    Emit_JMPLabel(SkipString);                                // JMP forward over string data
    StringAddress = CodeLocation;
    hr = StringCchVPrintfA((char*)CodeLocation, 120, Message, args);        // format the message into the Translation Cache
    if (FAILED(hr)) {
        ASSERT(FALSE);
        FixupLabel(SkipString);
        return CodeLocation; // return without generating any logging
    }
    CodeLocation+=strlen((char*)CodeLocation)+1;            // advance over the string and terminating NULL
    FixupLabel(SkipString);
    Emit_MOV_Reg_Imm32(ECX_Reg, PtrToLong(StringAddress));    // MOV ECX, string pointer
    Emit_CALL(JitLoggingHelper);                            // CALL JitLoggingHelper
    return CodeLocation;
}
#endif //LOGGING_ENABLED

unsigned __int8* PlaceInterruptPoll(unsigned __int8* CodeLocation, Decoded* d)
{
    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);
    Emit_CALL(InterruptCheck);
    return CodeLocation;
}

// This is called by R15ModifiedHelper if the GuestAddress is 0
// and the cached guest/host address pair is unitialized (which
// is determined by GuestAddress being zero).
__declspec(naked) void JumpToZeroHelper(void)
{
    __asm {
        // Return back to CpuSimulate:  R15 is 0.
        retn
    }
}

__declspec(naked) void R15ModifiedHelper(void)
{
    // On entry, ESI points to the GuestLast/nativeStart pair
    //             EDX is GuestAddress
    // This routine is JMPed to, not CALLed
    __asm {
        mov        ecx, dword ptr Cpu.GPRs[15*4]            // ECX = Cpu.GPRs[R15]
        call    dword ptr [InterruptCheck]              // check for interrupt, doesn't return if one is pending

        test    byte ptr Mmu.ControlRegister.Word, 1    // is Mmu.ControlRegister.Bits.M == 1?
        jz      ProcessIDNotNeeded                        // brif not
        test    ecx, 0xfe000000                            // does the address already specify a process id?
        jnz     ProcessIDNotNeeded                        // brif so - nothing needs to be done
        lea     eax, Mmu                                // eax = &Mmu
        or      ecx, dword ptr [eax+MMU_PROCESSID_OFFSET]     // else include the process ID in the address
ProcessIDNotNeeded:        
        cmp        ecx, [esi]                                // is the R15 value cached in GuestLast?
        jz      JumpToNativeTarget                        // brif so:  we can jump directly to the native destination
        // else see if the target is in the Translation Cache
        call    EPFromGuestAddrExact                    // Call EPFromGuestAddrExact(R15)
        test    eax, eax
        jz        NotCompiled                                // brif not compiled
        // else the destination has been jitted
        mov        ecx, dword ptr [eax]                    // ecx = ep->GuestStart
        mov        dword ptr [esi], ecx                    // set GuestLast = ep->GuestStart
        mov     ecx, dword ptr [eax+8]                    // ecx = ep->nativeStart
        mov     dword ptr [esi+4], ecx                    // set NativeLast = ep->nativeStart

        jmp        ecx

JumpToNativeTarget:
        jmp        dword ptr [esi+4]

NotCompiled:
        retn

    }
}

unsigned __int8* PlaceR15ModifiedHelper(unsigned __int8* CodeLocation, Decoded* d)
{
    unsigned __int32* DataPointer;

    Emit_MOV_Reg_Imm32(EDX_Reg, d->GuestAddress);        // MOV EDX, GuestAddress
    Emit_MOV_Reg_Imm32(ESI_Reg, 0);                        // MOV ESI, xxxxxxxx (updated, below)
    DataPointer = (unsigned __int32*)(CodeLocation-4);  // get a pointer to the '0' in the above MOV instruction
    Emit_JMP32(R15ModifiedHelper);                        // JMP R15ModifiedHelper
    
    // Emit the data dwords here, after the JMP
    CodeLocation = (unsigned __int8*)( (size_t)(CodeLocation+3) & ~3);  // dword-align CodeLocation
    *DataPointer = PtrToLong(CodeLocation);
    Emit32(0);            // GuestLast value
    EmitPtr(JumpToZeroHelper);    // NativeStart value

    return CodeLocation;
}


// Generates code to decode the shift field (Decoded.Operand2) into ResultReg.
// If fNeedsShifterCarryOut is set, then CL=ShifterCarryOut on return
// EAX and ECX may be trashed in the process, so ResultReg must not be one of
// those two registers.
unsigned __int8* PlaceDecodedShift(unsigned __int8* CodeLocation, const Decoded *d, const IntelReg ResultReg, bool fNeedsShifterCarryOut)
{
    // Operand2 is a 4-bit register (Rm) and an 8-bit shift specifier
    int Rm = d->Operand2 & 0xf;
    bool bShiftByRegister;
    unsigned __int32 Shift = d->Operand2 >> 4;
    unsigned __int32 ShiftAmount=0;
    unsigned __int32 ShiftType = (d->Operand2 >> 5) & 3;

    ASSERT(ResultReg != ECX_Reg);
    ASSERT(fNeedsShifterCarryOut == false || ResultReg != EAX_Reg); // this routine consumes EAX if fNeedsShifterCarryOut is set

    if (Shift & 1) {
        // Shift Register is encoded in bits 4-7 of "Shift"
        unsigned __int32 Rs = Shift >> 4;

        if (Rs == R15) {
            unsigned __int32 InstructionPointer = d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8);
            Emit8(0xb0+CL_Reg); Emit8((unsigned __int8)InstructionPointer); // MOV CL, (byte)R15
        } else {
            Emit8(0x8a); EmitModRmReg(0,5,CL_Reg); EmitPtr(&Cpu.GPRs[Rs]); // MOV CL, BYTE PTR [Cpu.GPRs[Rs]]
        }
        bShiftByRegister=true;
    } else {
        ShiftAmount = Shift >> 3;
        bShiftByRegister=false; // shift by immediate
    }

    if (Rm == R15) {
        unsigned __int32 InstructionPointer = d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8);
        if (Shift&1) {
            // R15 is an operand and a register was specified for the shift.  In this case,
            // R15 is 12 bytes ahead of the current instruction, not the usual 8.
            // See ARM7TDMI Data Sheet, 4-16, section 4.5.5.
            InstructionPointer += 4;
        }
        Emit_MOV_Reg_Imm32(ResultReg, InstructionPointer);            // MOV ResultReg, R15
    } else {
        Emit_MOV_Reg_DWORDPTR(ResultReg, &Cpu.GPRs[Rm]);            // MOV ResultReg, Cpu.GPRs[Rm]
    }

    switch (ShiftType) {
    case 0: // logical left
        if (bShiftByRegister) {
            unsigned __int8 *ShiftByLessThan32;
            unsigned __int8 *ShiftBy32;
            unsigned __int8 *ShiftDone1;
            unsigned __int8 *ShiftDone2;

            Emit8(0x80); EmitModRmReg(3,CL_Reg,7); Emit8(0x20u); // CMP CL, 0x20
            Emit_JBLabel(ShiftByLessThan32);    // JB ShiftByLessThan32
            Emit_JZLabel(ShiftBy32);            // JE ShiftBy32

            // else Shift by more than 32:  Set CF==0 and Rd==0
            Emit_XOR_Reg_Reg(ResultReg, ResultReg); // XOR ResultReg, ResultReg (sets Rd==0 and clears x86 CF)
            Emit_JMPLabel(ShiftDone1);          // JMP ShiftDone

            FixupLabel(ShiftBy32);              // ShiftBy32: Rd==0, CF==low bit of original Rd
            Emit8(0xc1); EmitModRmReg(3,ResultReg,4); Emit8(31u);    // SHL ResultReg, 31
            Emit8(0xd1); EmitModRmReg(3,ResultReg,4);                // SHL ResultReg, 1
            Emit_JMPLabel(ShiftDone2);          // JMP ShiftDone

            FixupLabel(ShiftByLessThan32);      // ShiftByLessThan32:
            if (fNeedsShifterCarryOut) {
                // The shift is an unknown amount in CL.  If the shift amount is truly zero, 
                // then ShifterCarryOut is set to carry flag value stored in Cpu.x86_Flags
                // Simulate this by preloading the x86 CarryFlag with our x86 Flags value.

                Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);        // BT Cpu.x86_Flags.Word, 0
            }
            Emit8(0xd3); EmitModRmReg(3,ResultReg,4);                // SHL ResultReg, CL
            FixupLabel(ShiftDone1);
            FixupLabel(ShiftDone2);
            if (fNeedsShifterCarryOut) {
                Emit_SETC_Reg8(CL_Reg);                                // SETC CL
            }
        } else {
            // Shift by immediate value.  In all cases, shifter_carry_out is the ARM CF: see ARM ARM 5.1.6
            if (ShiftAmount == 1) {
                Emit8(0xd1); EmitModRmReg(3,ResultReg,4);   // SHL ResultReg, 1
            } else if (ShiftAmount == 0) {
                // Do not generate code fot ShiftAmount==0
            } else if ((ShiftAmount % 32) == 0) {
                // x86 docs indicate "If a shift count greater than 31 is attempted, only the
                // bottom five bits of the shift count are used."  If the ShiftAmount
                // is an even multiple of 32, then x86 will treat it as no shift, rather than
                // by shifting by 32 to clear out the register.  So break the shift up into
                // to 16-bit shifts, ensuring that the result and flags match ARM's.
                Emit8(0xc1); EmitModRmReg(3,ResultReg,4); Emit8((unsigned __int8)16);                // SHL ResultReg,Imm8
                Emit8(0xc1); EmitModRmReg(3,ResultReg,4); Emit8((unsigned __int8)ShiftAmount-16);    // SHL ResultReg,Imm8
            } else {
                Emit8(0xc1); EmitModRmReg(3,ResultReg,4); Emit8((unsigned __int8)ShiftAmount);        // SHL ResultReg,Imm8
            }

            if (fNeedsShifterCarryOut) { // See the ARM ARM 5.1.6
                if (ShiftAmount == 0) { 
                    Emit_MOV_Reg_BYTEPTR(CL_Reg, &Cpu.x86_Flags.Byte);    // MOV CL, Cpu.x86_Flags.Byte
                    Emit8(0x80); EmitModRmReg(3,CL_Reg,4); Emit8(1);                // AND CL, 1
                } else {
                    Emit_SETC_Reg8(CL_Reg);                                        // SETC CL
                }
            }
        }
        break;

    case 1: // logical right
        if (bShiftByRegister) {
            unsigned __int8 *ShiftByLessThan32;
            unsigned __int8 *ShiftBy32;
            unsigned __int8 *ShiftDone1;
            unsigned __int8 *ShiftDone2;

            Emit8(0x80); EmitModRmReg(3,CL_Reg,7); Emit8(0x20u); // CMP CL, 0x20
            Emit_JBLabel(ShiftByLessThan32);    // JB ShiftByLessThan32
            Emit_JZLabel(ShiftBy32);            // JE ShiftBy32

            // else Shift by more than 32:  Set CF==0 and Rd==0
            Emit_XOR_Reg_Reg(ResultReg, ResultReg); // XOR ResultReg, ResultReg (sets Rd==0 and clears x86 CF)
            Emit_JMPLabel(ShiftDone1);          // JMP ShiftDone

            FixupLabel(ShiftBy32);              // ShiftBy32: Rd==0, CF==low bit of original Rd
            Emit8(0xc1); EmitModRmReg(3,ResultReg,5); Emit8(31u);    // SHR ResultReg, 31
            Emit8(0xd1); EmitModRmReg(3,ResultReg,5);                // SHR ResultReg, 1
            Emit_JMPLabel(ShiftDone2);          // JMP ShiftDone

            FixupLabel(ShiftByLessThan32);      // ShiftByLessThan32:
            if (fNeedsShifterCarryOut) {
                // The shift is an unknown amount in CL.  If the shift amount is truly zero, 
                // then ShifterCarryOut is set to carry flag value stored in Cpu.x86_Flags
                // Simulate this by preloading the x86 CarryFlag with our x86 Flags value.
                
                Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);        // BT Cpu.x86_Flags.Word, 0
            }
            Emit8(0xd3); EmitModRmReg(3,ResultReg,5);                // SHR ResultReg, CL
            FixupLabel(ShiftDone1);
            FixupLabel(ShiftDone2);
        } else { // shift by immediate
            if (ShiftAmount == 0) {
                ShiftAmount = 32;
            }
            if (ShiftAmount == 1) {
                Emit8(0xd1); EmitModRmReg(3,ResultReg,5); // SHR ResultReg,1
            } else if ((ShiftAmount % 32) == 0) {
                // x86 docs indicate "If a shift count greater than 31 is attempted, only the
                // bottom five bits of the shift count are used."  If the ShiftAmount
                // is an even multiple of 32, then x86 will treat it as no shift, rather than
                // by shifting by 32 to clear out the register.  So break the shift up into
                // to 16-bit shifts, ensuring that the result and flags match ARM's.
                Emit8(0xc1); EmitModRmReg(3,ResultReg,5); Emit8((unsigned __int8)16);                // SHR ResultReg,Imm8
                Emit8(0xc1); EmitModRmReg(3,ResultReg,5); Emit8((unsigned __int8)ShiftAmount-16);    // SHR ResultReg,Imm8
            } else {
                Emit8(0xc1); EmitModRmReg(3,ResultReg,5); Emit8((unsigned __int8)ShiftAmount);        // SHR ResultReg,Imm8
            }
        }
        if (fNeedsShifterCarryOut) {
            Emit_SETC_Reg8(CL_Reg);                        // SETC CL - capture ShifterCarryOut into CL
        }
        break;

    case 2:    // arithmetic right
        if (bShiftByRegister) {
            unsigned __int8 *ShiftByLessThan32;
            unsigned __int8 *ShiftDone;

            Emit8(0x80); EmitModRmReg(3,CL_Reg,7); Emit8(0x20u); // CMP CL, 0x20
            Emit_JBLabel(ShiftByLessThan32);    // JB ShiftByLessThan32

            // else Shift by 32 or more - just shift by 32 and stop
            Emit8(0xc1); EmitModRmReg(3,ResultReg,7); Emit8(31u);    // SAR ResultReg, 31
            Emit8(0xd1); EmitModRmReg(3,ResultReg,7);                // SAR ResultReg, 1
            Emit_JMPLabel(ShiftDone);           // JMP ShiftDone

            FixupLabel(ShiftByLessThan32);      // ShiftByLessThan32:
            if (fNeedsShifterCarryOut) {
                // The shift is an unknown amount in CL.  If the shift amount is truly zero, 
                // then ShifterCarryOut is set to carry flag value stored in Cpu.x86_Flags
                // Simulate this by preloading the x86 CarryFlag with our x86 Flags value.
                
                Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);        // BT Cpu.x86_Flags.Word, 0
            }
            Emit8(0xd3); EmitModRmReg(3,ResultReg,7);                // SAR ResultReg, CL
            FixupLabel(ShiftDone);
        } else {
            if (ShiftAmount == 0) { // special case - "ASR 0" really means "ASR 32")
                ShiftAmount = 32;
            }
            if (ShiftAmount == 1) {
                Emit8(0xd1); EmitModRmReg(3,ResultReg,7); // SAR ResultReg,1
            } else if ((ShiftAmount % 32) == 0) {
                // x86 docs indicate "If a shift count greater than 31 is attempted, only the
                // bottom five bits of the shift count are used."  If the ShiftAmount
                // is an even multiple of 32, then x86 will treat it as no shift, rather than
                // by shifting by 32 to clear out the register.  So break the shift up into
                // to 16-bit shifts, ensuring that the result and flags match ARM's.
                Emit8(0xc1); EmitModRmReg(3,ResultReg,7); Emit8((unsigned __int8)16);                // SAR ResultReg,Imm8
                Emit8(0xc1); EmitModRmReg(3,ResultReg,7); Emit8((unsigned __int8)ShiftAmount-16);    // SAR ResultReg,Imm8
            } else {
                Emit8(0xc1); EmitModRmReg(3,ResultReg,7); Emit8((unsigned __int8)ShiftAmount);        // SAR ResultReg,Imm8
            }
        }
        if (fNeedsShifterCarryOut) {
            Emit_SETC_Reg8(CL_Reg);                        // SETC CL - capture ShifterCarryOut into CL
        }
        break;

    case 3: // rotate right
        if (bShiftByRegister) {
            unsigned __int8* NoShift;
            unsigned __int8* ShiftByZero;
            unsigned __int8* ShiftDone1;
            unsigned __int8* ShiftDone2;

            Emit8(0x84); EmitModRmReg(3,CL_Reg,CL_Reg); // TEST CL, CL
            Emit_JZLabel(NoShift);                      // JZ NoShift
            Emit8(0x80); EmitModRmReg(3,CL_Reg,4); Emit8(0x1fu); // AND CL, 0x1f
            Emit_JZLabel(ShiftByZero);                  // JZ ShiftByZero

            // else rotate by a nonzero amount
            Emit8(0xd3); EmitModRmReg(3,ResultReg,1);    // ROR ResultReg, CL
            Emit_JMPLabel(ShiftDone1);

            FixupLabel(NoShift);                        // NoShift:
            // CF and Rd are unaffected
            if (fNeedsShifterCarryOut) {
                // Load the current CF value into CL
                Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);    // BT Cpu.x86_Flags.Word, 0
            }
            Emit_JMPLabel(ShiftDone2);

            FixupLabel(ShiftByZero);                    // ShiftByZero:  (set CF==top bit of Rd, Rd unaffected)
            if (fNeedsShifterCarryOut) {
                Emit_MOV_Reg_Reg(ECX_Reg, ResultReg);   // MOV ECX, ResultReg
                Emit8(0xd1); EmitModRmReg(3,ECX_Reg,4);    // SHL ECX, 1
            }
            FixupLabel(ShiftDone1);
            FixupLabel(ShiftDone2);
        } else {
            if (ShiftAmount == 0) {
                // "ROR 0" is special cased:  it does a one-bit ROR and includes the carry bit
                Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);        // BT Cpu.x86_Flags.Word, 0
                Emit8(0xd1); EmitModRmReg(3,ResultReg,3);            // RCR ResultReg,1
            } else if (ShiftAmount == 1) {
                Emit8(0xd1); EmitModRmReg(3,ResultReg,1); // ROR ResultReg,1
            } else if ((ShiftAmount % 32) == 0) {
                // x86 docs indicate "If a shift count greater than 31 is attempted, only the
                // bottom five bits of the shift count are used."  If the ShiftAmount
                // is an even multiple of 32, then x86 will treat it as no shift, rather than
                // by shifting by 32 to clear out the register.  So break the shift up into
                // to 16-bit shifts, ensuring that the result and flags match ARM's.
                Emit8(0xc1); EmitModRmReg(3,ResultReg,1); Emit8((unsigned __int8)16);                // ROR ResultReg,Imm8
                Emit8(0xc1); EmitModRmReg(3,ResultReg,1); Emit8((unsigned __int8)ShiftAmount-16);    // ROR ResultReg,Imm8
            } else {
                Emit8(0xc1); EmitModRmReg(3,ResultReg,1); Emit8((unsigned __int8)ShiftAmount);        // ROR ResultReg,Imm8
            }
        }
        if (fNeedsShifterCarryOut) {
            Emit_SETC_Reg8(CL_Reg);                        // SETC CL - capture ShifterCarryOut into CL
        }
        break;

    default:
        ASSERT(FALSE);
        break;
    }

    return CodeLocation;
}

// On return, x86 flags contains the flags to store into the ARM PSR flags
unsigned __int8*
PlaceBasicTwoAddrWithResult(
    unsigned __int8* CodeLocation,
    const unsigned __int8 ArithRegOpcode,        // OP r/m32, r32
    const unsigned __int8 ArithImm32Opcode,     // OP r/m32, imm32
    const unsigned __int8 ArithImm32Reg,
    Decoded *d,
    const IntelReg ImmediateReg
    )
{
    if (d->Rn == d->Rd && d->Rn != R15) {
        if (d->I) {
            Emit8(ArithImm32Opcode); EmitModRmReg(0,5,ArithImm32Reg); EmitPtr(&Cpu.GPRs[d->Rd]); Emit32(d->Reserved3); // OP Cpu.GPRs[Rd], Imm32
        } else {
            Emit8(ArithRegOpcode); EmitModRmReg(0,5,ImmediateReg); EmitPtr(&Cpu.GPRs[d->Rd]); // OP Cpu.GPRs[Rd], Immediate
        }
    } else {
        if (d->Rn == R15) {
            Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8)); // MOV EAX, R15
        } else {
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rn]);    // MOV EAX, Cpu.GPRs[Rn]
        }
        if (d->I) {
            if (d->S == 0 && d->Reserved3 == 0 && (ArithImm32Opcode == 0x81 && (ArithImm32Reg == 0) || ArithImm32Reg == 5)) {
                // This would generate "ADD EAX, 0", or "SUB EAX, 0" which are no-ops.
            } else {
                Emit8(ArithImm32Opcode); EmitModRmReg(3,EAX_Reg,ArithImm32Reg); Emit32(d->Reserved3); // op EAX, Imm32
            }
        } else {
            Emit8(ArithRegOpcode); EmitModRmReg(3,EAX_Reg,ImmediateReg);    // OP EAX,reg == OP Rn, Immediate
        }
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);    // MOV Cpu.GPRs[Rd], EAX
    }

    return CodeLocation;
}

// On return, x86 flags contains the flags to store into the ARM PSR flags
unsigned __int8*
PlaceBasicTwoAddrNoResult(
    unsigned __int8* CodeLocation,
    const unsigned __int8 ArithRegOpcode,        // OP r/m32, r32
    const unsigned __int8 ArithImm32Opcode,     // OP r/m32, imm32
    const unsigned __int8 ArithImm32Reg,
    Decoded *d,
    const IntelReg ImmediateReg,
    bool fOpcodeHasSideEffect
    )
{
    if (d->Rn == R15 || fOpcodeHasSideEffect) {
        if (d->Rn == R15) {
            Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8)); // MOV EAX, R15
        } else {
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rn]);    // MOV EAX, Cpu.GPRs[Rn]
        }
        if (d->I) {
            Emit8(ArithImm32Opcode); EmitModRmReg(3,EAX_Reg,ArithImm32Reg); Emit32(d->Reserved3); // op EAX, Imm32
        } else {
            Emit8(ArithRegOpcode); EmitModRmReg(3,EAX_Reg,ImmediateReg);    // OP EAX,reg == OP Rn, Immediate
        }
    } else {
        if (d->I) {
            Emit8(ArithImm32Opcode); EmitModRmReg(0,5,ArithImm32Reg); EmitPtr(&Cpu.GPRs[d->Rn]); Emit32(d->Reserved3); // OP Cpu.GPRs[Rd], Imm32
        } else {
            Emit8(ArithRegOpcode); EmitModRmReg(0,5,ImmediateReg); EmitPtr(&Cpu.GPRs[d->Rn]); // OP Cpu.GPRs[Rd], Immediate
        }
    }
    return CodeLocation;
}

// Sets x86 CF based on:
// if (d->I) the immediate value in d->Operand2/d->Reserved3.  
// else      the value in CL
//
// ECX may be trashed, but all other x86 flags are preserved.
unsigned __int8* PlaceShifterCarryOut(unsigned __int8* CodeLocation, Decoded *d)
{
    if (d->I) {
        unsigned __int32 rotate_imm = (d->Operand2 >> 8) & 0xf;

        if(rotate_imm == 0) {
            // The instruction sets ARM CF to be shifter_carry_out, which is ARM CF.  In other words,
            // the instruction leaves the ARM CF unmodified.  Do this by loading the current
            // x86 CF value into the host x86 CF.
            Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);            // BT Cpu.x86_Flags.Word, 0

        } else {
            if ((d->Reserved3 >> 31) & 1) {
                Emit8(0xf9); // STC - set carry flag
            } else {
                Emit8(0xf8); // CLC - clear carry flag
            }
        }
    } else {
        
        Emit8(0xc0); EmitModRmReg(3,CL_Reg,0); Emit8(8);            // ROL CL, 8 - shift the ARM CF into the x86 CF without
                                                                    //             disturbing any other x86 flags
    }
    return CodeLocation;
}

//Using FlagsSet, we create a mask to update only the needed flags in x86_Flags 
unsigned char GetX86FlagsMask (Decoded *d)
{

    unsigned char x86FlagMask = 0x00;

    switch (d->FlagsSet){    // cases are listed in order of frequency

            case ALL_FLAGS:                        // 15    1111
                x86FlagMask = 0xFF;
                break;
            
            case (FLAG_NF | FLAG_ZF | FLAG_CF):    // 14    1110
                x86FlagMask = X86_FLAG_ZF | X86_FLAG_CF | X86_FLAG_NF;
                break;

            case (FLAG_ZF):                        // 4    0100
            case (FLAG_ZF | FLAG_VF):            // 5    0101
            
                x86FlagMask = X86_FLAG_ZF;                //01000000
                break;
            
            case (FLAG_NF):                        // 8    1000
            case (FLAG_NF | FLAG_VF):            // 9    1001
            
                x86FlagMask = X86_FLAG_NF;                //10000000
                break;

            case (FLAG_CF):                        // 2    0010
            case (FLAG_CF | FLAG_VF):            // 3    0011
                
                x86FlagMask = X86_FLAG_CF;                //00000001
                break;

            case (FLAG_ZF | FLAG_CF):            // 6    0110
            case (FLAG_ZF | FLAG_CF | FLAG_VF): // 7    0111
                x86FlagMask = X86_FLAG_ZF | X86_FLAG_CF;                //01000001            
                break;
            
            case (FLAG_NF | FLAG_ZF):            // 12    1100
            case (FLAG_NF | FLAG_ZF | FLAG_VF):    // 13    1101
                x86FlagMask = X86_FLAG_ZF | X86_FLAG_NF;                //11000000
                break;
            
            //since overflow is set independent, no mask is generated
            case (FLAG_VF):                        // 1    0001
            case NO_FLAGS:
                break;                            

            
            case (FLAG_NF | FLAG_CF):            // 10    1010    
            case (FLAG_NF | FLAG_CF | FLAG_VF): // 11    1011
            
                x86FlagMask = X86_FLAG_NF | X86_FLAG_CF;                //10000001
                break;

            default:
                ASSERT(FALSE);
                break;
        }
        
    return x86FlagMask;
    
}

// Update flags after a 64-bit multiply or multiply/add
// The overflow flag and carry flags are preserved and not updated
unsigned __int8* PlaceUpdateLLX86Flags(unsigned __int8* CodeLocation)
{
    
    Emit8(0x9F);        //LAHF
    //we only want to set Negative (sign) and Zero Flag
    unsigned char flagMask = X86_FLAG_NF|X86_FLAG_ZF;                        //11000000
    
    Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Flags.Byte);                        //MOV Byte Ptr AL, cpu.x86_Flags.Byte
    Emit8(0x80);    EmitModRmReg(3,AH_Reg,4); Emit8(flagMask);                //AND AH, flagMask
    Emit8(0x80);    EmitModRmReg(3,AL_Reg,4); Emit8(~flagMask);                //AND AL, ~flagMask
    Emit8(0x0a); EmitModRmReg(3,AL_Reg,AH_Reg);                                //OR AH, AL - merge orginal flags
                
    Emit8(0x88);  EmitModRmReg(0,5,AH_Reg); EmitPtr(&Cpu.x86_Flags.Byte);        //MOV BYTE PTR &cpu.x86Flags,AH
            
    return CodeLocation;
}

// On entry x86 EFLAGS contains the flag registers.  Copy the flags into our x86 flags fields.
unsigned __int8* PlaceUpdateX86Flags(unsigned __int8* CodeLocation, Decoded *d, bool fAdd)
{
    if (!d->S) {
        return CodeLocation;
    }

    if (d->Rd == R15) {
        Emit_PUSH_DWORDPTR(&Cpu.SPSR.Word);                        // PUSH Cpu.SPSR
        Emit_CALL(UpdateCPSRWithFlags);                            // CALL UpdateCPSRWithFlags (the argument is on the stack, despite this being a __fastcall function)
        Emit_RETN(0);                                            // RET - return to CpuSimulate as the processor mode may have changed
    
    } else if (d->FlagsSet) {
        if (d->FlagsSet == FLAG_VF)                                //We only need to set Carry Flag
        {
            Emit16(0x900f); EmitModRmReg(3,AH_Reg,0);                                    //SETO AH    
            Emit8(0x88);  EmitModRmReg(0,5,AH_Reg); EmitPtr(&Cpu.x86_Overflow.Byte);    //MOV BYTE PTR &cpu.x86_Overflow,AH
                
        }
        else 
        {
            //Set x86_Overflow only if we need to update the Overflow flag
            if (d->FlagsSet & FLAG_VF){ 
                Emit16(0x900f); EmitModRmReg(0,5,0); EmitPtr(&Cpu.x86_Overflow.Byte); //SETO BYTE PTR &cpu.x86_Overflow
            }
            
            if ((d->FlagsSet & FLAG_CF) && !fAdd)
                Emit_CMC();        //invert Carry flag                
            
            Emit8(0x9F);        //LAHF                            
            
            //If all flags are set, we do not need to preserve any of the previous flag bits
            //and hence no mask needs to be calculated
            if (d->FlagsSet != ALL_FLAGS)
            {
                unsigned char flagMask = GetX86FlagsMask(d);

                if (flagMask != (X86_FLAG_ZF | X86_FLAG_CF | X86_FLAG_NF)) {
                    // Need to update ZF, CF, or NF (but not all three) - merge the new flags into the existing
                    // value.  If all three must be updated, just replace the existing value.
                    //
                    // d->FlagsSet == 0 and d->FlagsSet == FLAG_VF have been handled elsewhere, so flagMask should 
                    // always be nonzero.  If it isn't, we'll generate slow but correct code.
                    ASSERT(flagMask != 0); 

                    Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Flags.Byte);                        //MOV Byte Ptr AL, cpu.x86_Flags.Byte
                    Emit8(0x80);    EmitModRmReg(3,AH_Reg,4); Emit8(flagMask);                //AND AH, flagMask
                    Emit8(0x80);    EmitModRmReg(3,AL_Reg,4); Emit8(~flagMask);                //AND AL, ~flagMask
                    Emit8(0x0a); EmitModRmReg(3,AL_Reg,AH_Reg);                                //OR AH, AL - merge orginal flags
                }
            }
                        
            Emit8(0x88);  EmitModRmReg(0,5,AH_Reg); EmitPtr(&Cpu.x86_Flags.Byte);        //MOV BYTE PTR &cpu.x86Flags,AH
            
        }
    }
    return CodeLocation;
}

__declspec(naked) void PopShadowStackHelper(void)
{
    __asm{
    mov al, StackCount                                // al = StackCount
    test al, al                                        // is it zero?
    jz StackEmpty                                    // brif so - stack is empty
    sub al, 1                                        // Decrement StackCount
    mov StackCount, al                                // Update StackCount
    movzx eax, al                                    // Zero-extend AL into EAX
    lea eax, [eax*8+ShadowStack]                    // EAX = &ShadowStack[StackCount]
    mov ecx, Cpu.GPRs[15*4]                            // ECX = Cpu.GPRs[R15]
    test    byte ptr Mmu.ControlRegister.Word, 1    // is Mmu.ControlRegister.Bits.M == 1?
    jz      ProcessIDNotNeeded                        // brif not
    test    ecx, 0xfe000000                            // does the address already specify a process id?
    jnz     ProcessIDNotNeeded                        // brif so - nothing needs to be done
    lea     edx, Mmu                                // edx = &Mmu
    or      ecx, dword ptr [edx+MMU_PROCESSID_OFFSET]     // else include the process ID in the address
ProcessIDNotNeeded:        
    mov edx, dword ptr [eax]                        // edx = ShadowStack[StackCount].GuestReturnAddress
    and edx, 0xfffffffe                                // mask off thumb bit
    cmp ecx, edx                                    // Is R15 == ShadowStack[StackCount].GuestReturnAddress?
    jnz StackEmpty                                    // brif not - we have a cache miss
    jmp dword ptr [eax+4]                            // jump directly to the ShadowStack[StackCount].NativeDestination
    
StackEmpty:
    retn                                            // return to C/C++ code
    }
}

// On entry, esi contains a pointer to the NativeCache, R14 contains the guest return address value.
__declspec(naked) void PushShadowStackHelper(){
    __asm{
        cmp dword ptr[esi], 0    // cmp NativeCache,0
        jz PerformLookup        // brif so - need to call EPFromGuestAddrExact
StartPush:
        mov ecx, Cpu.GPRs[14*4]
        test    byte ptr Mmu.ControlRegister.Word, 1    // is Mmu.ControlRegister.Bits.M == 1?
        jz      ProcessIDNotNeeded                        // brif not
        test    ecx, 0xfe000000                            // does the address already specify a process id?
        jnz     ProcessIDNotNeeded                        // brif so - nothing needs to be done
        lea     eax, Mmu                                // eax = &Mmu
        or      ecx, dword ptr [eax+MMU_PROCESSID_OFFSET]     // else include the process ID in the address
ProcessIDNotNeeded:        
        movzx eax, StackCount
        shl eax, 3
        mov dword ptr ShadowStack[eax],ecx
        mov edx, dword ptr [esi]
        mov dword ptr ShadowStack+4[eax], edx
        inc StackCount
        retn

PerformLookup:
        mov ecx, Cpu.GPRs[14*4]
        and ecx, 0xfffffffe     // mask off the Thumb bit
        call EPFromGuestAddrExact
        test eax, eax
        jz NotJitted
        mov eax, dword ptr [eax+8]        // if entrypoint found, eax=ep->NativeAddress.  Else, eax==0.
        mov dword ptr [esi], eax
        jmp StartPush

NotJitted:
        // The return address hasn't been jitted.  Push a pointer to a 'ret' on the
        // shadow stack.  The 'ret', when executed, will return to C/C++ code
        // and cause the return address code to be jitted.
        mov eax, NotJittedHelper
        mov dword ptr [esi], eax
        jmp StartPush

NotJittedHelper:
        retn
    }
}


// This trashes all scratch registers as it runs.
unsigned __int8* PlacePushShadowStack(unsigned __int8* CodeLocation)
{
    unsigned __int8* SkipCache;
    unsigned __int32* CacheLocationPointer;

    // push return address onto the stack
    Emit_MOV_Reg_Imm32(ESI_Reg,0); CacheLocationPointer = (unsigned __int32*)(CodeLocation-4);
    Emit_CALL(PushShadowStackHelper);
    Emit_JMPLabel(SkipCache);
    CodeLocation = (unsigned __int8*)( (size_t)(CodeLocation+3) & ~3);  // dword-align CodeLocation
    *CacheLocationPointer=PtrToLong(CodeLocation);
    Emit32(0);
    FixupLabel(SkipCache);

    return CodeLocation;
}


unsigned __int8* PlaceDataProcessing(unsigned __int8* CodeLocation, Decoded *d)
{
    const IntelReg ImmediateReg = EDX_Reg;
    bool fNeedsShifterCarryOut=false;

    switch (d->Opcode) {
    case 0: // 0000 = AND - Rd = Rn AND Immediate
        LogPlace((CodeLocation,"AND%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->S && (d->FlagsSet & FLAG_CF)) {
            fNeedsShifterCarryOut = true;
        }
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, fNeedsShifterCarryOut);
            // On return, ImmediateReg contains the immediate value, and if fNeedsShifterCarryOut, CL contains shifter_carry_out
        }
        CodeLocation = PlaceBasicTwoAddrWithResult(CodeLocation, 0x21, 0x81, 4, d, ImmediateReg); // AND
        if (fNeedsShifterCarryOut) {
            CodeLocation = PlaceShifterCarryOut(CodeLocation, d);
        }
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 1: // 0001 = EOR - Rd = Rn EOR Immediate
        if (d->S && (d->FlagsSet & FLAG_CF)) {
            fNeedsShifterCarryOut = true;
        }
        LogPlace((CodeLocation,"EOR%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, fNeedsShifterCarryOut);
            // On return, ImmediateReg contains the immediate value, and if fNeedsShifterCarryOut, CL contains shifter_carry_out
        }
        CodeLocation = PlaceBasicTwoAddrWithResult(CodeLocation, 0x31, 0x81, 6, d, ImmediateReg); // XOR
        if (fNeedsShifterCarryOut) {
            CodeLocation = PlaceShifterCarryOut(CodeLocation, d);
        }
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 2: // 0010 = SUB - Rd = Rn - Immediate
        LogPlace((CodeLocation,"SUB%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, false);
        }
        CodeLocation = PlaceBasicTwoAddrWithResult(CodeLocation, 0x29, 0x81, 5, d, ImmediateReg);    // SUB
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, false);
        break;

    case 3: // 0011 = RSB - Rd = Immediate - Rn
        LogPlace((CodeLocation,"RSB%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->I) {
            unsigned __int32 Immediate;

            Immediate = d->Reserved3;
            if (Immediate) {
                Emit_MOV_Reg_Imm32(ImmediateReg, Immediate);    // MOV ImmediateReg, Immedate
            } else {
                Emit_XOR_Reg_Reg(ImmediateReg, ImmediateReg);    // XOR ImmediateReg, ImmediateReg
            }
        } else {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, false);
        }
        if (d->Rn == R15) {
            Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8)); // MOV EAX, R15
        } else {
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rn]);        // MOV EAX, Cpu.GPRs[Rn]
        }
        Emit8(0x29); EmitModRmReg(3,ImmediateReg, EAX_Reg);        // SUB reg,EAX == SUB Immediate, Rn
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], ImmediateReg);    // MOV Cpu.GPRs[Rd], Immediate
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, false);
        break;

    case 4: // 0100 = ADD - Rd = Rn + Immediate
        LogPlace((CodeLocation,"ADD%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, false);
        }
        CodeLocation = PlaceBasicTwoAddrWithResult(CodeLocation, 0x01, 0x81, 0, d, ImmediateReg); // ADD
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 5: // 0101 = ADC - Rd = Rn + Immediate + C
        LogPlace((CodeLocation,"ADC%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, false);
        }
        //Set the Carry flag
        Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);        // BT Cpu.x86_Flags.Word, 0

        CodeLocation = PlaceBasicTwoAddrWithResult(CodeLocation, 0x11, 0x81, 2, d, ImmediateReg);    // ADC
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 6: // 0110 = SBC - Rd = Rn - shifter_operand - NOT(C Flag)
        // Intel's    SBB - Rd = Rd - (Immediate + C)  which can be rewritten Rd = Rd - Immediate - C.
        LogPlace((CodeLocation,"SBC%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->I) {
            unsigned __int32 Immediate;

            Immediate = d->Reserved3;
            if (Immediate) {
                Emit_MOV_Reg_Imm32(ImmediateReg, Immediate);    // MOV ImmediateReg, Immedate
            } else {
                Emit_XOR_Reg_Reg(ImmediateReg, ImmediateReg);    // XOR ImmediateReg, ImmediateReg
            }
        } else {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, false);
        }
        //Set the Carry flag on the host computer
        Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);            // BT Cpu.x86_Flags.Word, 0

        Emit_CMC();                                                // CMC - invert the carry flag, creating "NOT(C Flag)" in x86 CFlag
        if (d->Rn == R15) {
            Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8)); // MOV EAX, R15
        } else {
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rn]);    // MOV EAX, Cpu.GPRs[Rn]
        }
        Emit8(0x1b); EmitModRmReg(3,ImmediateReg,EAX_Reg);        // SBB EAX,reg == SBB Rn, Immediate
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);        // MOV Cpu.GPRs[Rd], EAX
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, false);
        break;

    case 7: // 0111 = RSC - Rd = shifter_operand - Rn - NOT(C Flag)
        // Intel's    SBB - Rd = Rd - (Immediate + C)  which can be rewritten Rd = Rd - Immediate - C.
        LogPlace((CodeLocation,"RSC%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->I) {
            unsigned __int32 Immediate;

            Immediate = d->Reserved3;
            if (Immediate) {
                Emit_MOV_Reg_Imm32(ImmediateReg, Immediate);    // MOV ImmediateReg, Immedate
            } else {
                Emit_XOR_Reg_Reg(ImmediateReg, ImmediateReg);    // XOR ImmediateReg, ImmediateReg
            }
            // ShifterCarryOut is not needed, so long as d->S==0
        } else {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, false);
        }
        //Set the Carry flag on the host computer
        Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);            // BT Cpu.x86_Flags.Word, 0

        Emit_CMC();                                                // CMC - invert the carry flag, creating "NOT(C Flag)" in x86 CFlag
        if (d->Rn == R15) {
            Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8)); // MOV EAX, R15
        } else {
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rn]);    // MOV EAX, Cpu.GPRs[Rn]
        }
        Emit8(0x1b); EmitModRmReg(3,EAX_Reg,ImmediateReg);        // SBB reg, EAX == SBB Immediate, Rn
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], ImmediateReg);    // MOV Cpu.GPRs[Rd], Immediate
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, false);
        break;

    case 8: // 1000 = TST - set condition codes on Rn AND Immediate
        LogPlace((CodeLocation,"TST Rn=%d, %s=%x\n", d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        ASSERT(d->S);
        if (d->FlagsSet & FLAG_CF) {
            fNeedsShifterCarryOut = true;
        }
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, fNeedsShifterCarryOut);
            // On return, ImmediateReg contains the immediate value, and if fNeedsShifterCarryOut, CL contains shifter_carry_out
        }
        CodeLocation = PlaceBasicTwoAddrNoResult(CodeLocation, 0x85, 0xf7, 0, d, ImmediateReg, false); // TEST
        if (fNeedsShifterCarryOut) {
            CodeLocation = PlaceShifterCarryOut(CodeLocation, d);
        }
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 9: // 1001 = TEQ - set condition codes on Rn EOR Immediate
        LogPlace((CodeLocation,"TEQ Rn=%d, %s=%x\n", d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        ASSERT(d->S);
        if (d->FlagsSet & FLAG_CF) {
            fNeedsShifterCarryOut = true;
        }
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, fNeedsShifterCarryOut);
            // On return, ImmediateReg contains the immediate value, and if fNeedsShifterCarryOut, CL contains shifter_carry_out
        }
        CodeLocation = PlaceBasicTwoAddrNoResult(CodeLocation, 0x31, 0x81, 6, d, ImmediateReg, true); // XOR
        if (fNeedsShifterCarryOut) {
            CodeLocation = PlaceShifterCarryOut(CodeLocation, d);
        }
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 10: // 1010 = CMP - set condition codes on Rn - Immediate
        LogPlace((CodeLocation,"CMP Rn=%d, %s=%x\n", d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, false);
        }
        CodeLocation = PlaceBasicTwoAddrNoResult(CodeLocation, 0x39, 0x81, 7, d, ImmediateReg, false); // CMP
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, false);
        break;

    case 11: // 1011 = CMN - set condition codes on Rn + Immediate
        LogPlace((CodeLocation,"CMN Rn=%d, %s=%x\n", d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, false);
        }
        CodeLocation = PlaceBasicTwoAddrNoResult(CodeLocation, 0x01, 0x81, 0, d, ImmediateReg, true); // ADD
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 12: // 1100 = ORR - Rd = Rn OR Immediate
        LogPlace((CodeLocation,"ORR%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->S && (d->FlagsSet & FLAG_CF)) {
            fNeedsShifterCarryOut = true;
        }
        if (d->I == 0) {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, fNeedsShifterCarryOut);
            // On return, ImmediateReg contains the immediate value, and if fNeedsShifterCarryOut, CL contains shifter_carry_out
        }
        CodeLocation = PlaceBasicTwoAddrWithResult(CodeLocation, 0x09, 0x81, 1, d, ImmediateReg); // OR
        if (fNeedsShifterCarryOut) {
            CodeLocation = PlaceShifterCarryOut(CodeLocation, d);
        }
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 13: // 1101 = MOV = Rd = Immediate
        LogPlace((CodeLocation,"MOV%s Rd=%d, %s=%x\n", (d->S)?"S":"", d->Rd, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->S && (d->FlagsSet & FLAG_CF)) {
            fNeedsShifterCarryOut = true;
        }

        if (d->I) {
            unsigned __int32 Immediate;

            Immediate = d->Reserved3;
            if (d->S) {

                // ARM MOVS instruction.  The x86 code-gen uses MOV to store the result,
                // does not update the x86 flags like Zero.  So here, we need to compute
                // the Zero and Negative flags (Carry is handled by ShifterCarryOut and Overflow
                // is unchanged).    If the destination is R15, then we don't need to compute
                // the flags as it is going to move SPSR into CPSR and overwrite the flags.
                
                Emit_MOV_Reg_Imm32(EAX_Reg, Immediate);    // MOV EAX, Immediate
                Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg); // MOV Cpu.GPRs[Rd], EAX
                Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);    // TEST Reg, Reg - zeroes x86 OF/CF and sets SF/ZF/PF according to the result
            } else {
                Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[d->Rd]); Emit32(Immediate);    // MOV Cpu.GPRs[Rd], Immediate
            }
        } else {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, fNeedsShifterCarryOut);
            // On return, ImmediateReg contains the immediate value, and if fNeedsShifterCarryOut, CL contains shifter_carry_out
            if (d->S && d->Rd == d->Operand2) {
		// Optimize out the store - it is a "MOVES Rd,Rd".  NetCF uses this as a quick check if
		// a pointer is NULL or not.
	    } else {
	            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], ImmediateReg);
	    }
            
            if (d->S && d->Rd != R15) {
                // ARM MOVS instruction.  The x86 code-gen uses MOV to store the result, which
                // does not update the x86 flags like Zero.  So here, we need to compute
                // the Zero and Negative flags (Carry is handled by ShifterCarryOut and Overflow
                // is unchanged).  If the destination is R15, then we don't need to compute
                // the flags as it is going to move SPSR into CPSR and overwrite the flags.
                Emit_TEST_Reg_Reg(ImmediateReg, ImmediateReg); // TEST Reg, Reg - zeroes x86 OF/CF and sets SF/ZF/PF according to the result
            }
        }
        if (fNeedsShifterCarryOut) {
            CodeLocation = PlaceShifterCarryOut(CodeLocation, d);
        }
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 14: // 1110 = BIC = Rd = Rn AND NOT Immediate
        LogPlace((CodeLocation,"BIC%s Rd=%d, Rn=%d, %s=%x\n", (d->S)?"S":"", d->Rd, d->Rn, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : d->Operand2));
        if (d->S && (d->FlagsSet & FLAG_CF)) {
            fNeedsShifterCarryOut = true;
        }
        if (d->I) {
            d->Reserved3 = ~d->Reserved3;    // NOT the immediate value
        } else {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, fNeedsShifterCarryOut);
            // On return, ImmediateReg contains the immediate value, and if fNeedsShifterCarryOut, CL contains shifter_carry_out
            Emit8(0xf7); EmitModRmReg(3,ImmediateReg,2);        // NOT Immediate
        }
        CodeLocation = PlaceBasicTwoAddrWithResult(CodeLocation, 0x21, 0x81, 4, d, ImmediateReg);    // AND
        if (d->I) {
            d->Reserved3 = ~d->Reserved3;    // Undo NOT the immediate value, so that shifter_carry_out contains the
                                            // carry without the NOT.
        }
        if (fNeedsShifterCarryOut) {
            CodeLocation = PlaceShifterCarryOut(CodeLocation, d);
        }
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    case 15: // 1111 = MVN = Rd = NOT Immediate
        LogPlace((CodeLocation,"MVN%s Rd=%d, %s=%x\n", (d->S)?"S":"", d->Rd, (d->I) ? "#" : "Operand2", (d->I) ? d->Reserved3 : (d->I) ? d->Reserved3 : d->Operand2));
        if (d->S && (d->FlagsSet & FLAG_CF)) {
            fNeedsShifterCarryOut = true;
        }
        if (d->I) {
            unsigned __int32 Immediate;

            Immediate = ~d->Reserved3;

            if (d->S && d->Rd != R15) {
                // ARM MOVS instruction.  The x86 code-gen uses MOV to store the result,
                // does not update the x86 flags like Zero.  So here, we need to compute
                // the Zero and Negative flags (Carry is handled by ShifterCarryOut and Overflow
                // is unchanged).  If the destination is R15, then we don't need to compute
                // the flags as it is going to move SPSR into CPSR and overwrite the flags.
                Emit_MOV_Reg_Imm32(EAX_Reg, Immediate);    // MOV EAX, Immediate
                Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg); // MOV Cpu.GPRs[Rd], EAX
                Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);    // TEST Reg, Reg - zeroes x86 OF/CF and sets SF/ZF/PF according to the result
            } else {
                Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[d->Rd]); Emit32(Immediate);    // MOV Cpu.GPRs[Rd], Immediate
            }
        } else {
            CodeLocation = PlaceDecodedShift(CodeLocation, d, ImmediateReg, fNeedsShifterCarryOut);
            // On return, ImmediateReg contains the immediate value, and if fNeedsShifterCarryOut, CL contains shifter_carry_out
            Emit8(0xf7); EmitModRmReg(3,ImmediateReg,2);        // NOT Immediate
            if (d->S && d->Rd != R15) {
                // NOT does not update the x86 flags, so explicitly compute them now.
                // NEG updates the x86 flags (and hence is slower)
                Emit_TEST_Reg_Reg(ImmediateReg, ImmediateReg);    // TEST Reg, Reg - zeroes x86 OF/CF and sets SF/ZF/PF according to the result
            }
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], ImmediateReg);
        }
        if (fNeedsShifterCarryOut) {
            CodeLocation = PlaceShifterCarryOut(CodeLocation, d);                
        }
        CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        break;

    default:
        ASSERT(FALSE);
        break;
    }

    if (d->R15Modified) {
        if (d->Opcode == 13 && d->Rm == R14 && !d->I) {    // ARM "MOV[S] R15, R14"
            Emit_JMP32(PopShadowStackHelper);   // JMP PopShadowStackHelper
        } else {
            CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
        }
    }
    return CodeLocation;
}

unsigned __int8* PlaceDataProcessingCALL(unsigned __int8* CodeLocation, Decoded *d){
    CodeLocation = PlacePushShadowStack(CodeLocation);
    return PlaceDataProcessing(CodeLocation, d);
}

unsigned __int8* PlaceSingleDataTransferOffset(unsigned __int8* CodeLocation, const Decoded *d, bool *pfNeedsAlignmentCheck)
{
    if (d->B) {
        *pfNeedsAlignmentCheck = false; // LDRB/STRB - no alignment checks needed
    } else {
        *pfNeedsAlignmentCheck = true;    // LDR/STR - assume that the effective address will need an alignment check
    }

    if (d->I) {
        CodeLocation = PlaceDecodedShift(CodeLocation, d, EDX_Reg, false);
        if (d->Rn == R15) {
            Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8)); // MOV ECX, R15
        } else {
            Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rn]);        // MOV ECX, Cpu.GPRs[Rn]
        }
        if (d->P) {
	    // Pre-Indexed
  	    if (d->U) {
	        Emit8(0x03); EmitModRmReg(3,EDX_Reg,ECX_Reg);            // ADD ECX, EDX
            } else {
                Emit8(0x2b); EmitModRmReg(3,EDX_Reg,ECX_Reg);            // SUB ECX, EDX
            }
            if (d->W) { // writeback is enabled
	        Emit_MOV_Reg_Reg(ESI_Reg, ECX_Reg);                      // MOV ESI, ECX
            }
        } else {
            // Post-Indexed
            if (d->U == 0) { // Down direction - do the substraction by negating and adding
		Emit8(0xf7); EmitModRmReg(3,EDX_Reg,3);                         // NEG EDX
	    }
	    Emit8(0x8d); EmitModRmReg(0,4,ESI_Reg); EmitSIB(0,EDX_Reg,ECX_Reg); // LEA ESI, [EDX+ECX]
        }
    } else {
        if (d->Rn == R15) {
            Emit_MOV_Reg_Imm32(ECX_Reg, d->Reserved3); // MOV ECX, R15+/-Offset
            if ((d->Reserved3 & 3) == 0) {
                *pfNeedsAlignmentCheck = false;        // the effective address is aligned - no need for a runtime check
            }
        } else {
            Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rn]);        // MOV ECX, Cpu.GPRs[Rn]
	    if (d->P) {
		// Pre-Indexed
                if (d->U) {
                    Emit_ADD_Reg_Imm32(ECX_Reg, d->Offset); // ADD ECX, Offset
                } else {
                    Emit_SUB_Reg_Imm32(ECX_Reg, d->Offset); // SUB ECX, Offset
                }
                if (d->W) { // writeback is enabled
	            Emit_MOV_Reg_Reg(ESI_Reg, ECX_Reg);                      // MOV ESI, ECX
                }
            } else {
                // Post-indexed
                unsigned __int32 OffsetToAdd = (d->U) ? d->Offset : -d->Offset;
                if ((unsigned __int32)(signed __int8)OffsetToAdd == OffsetToAdd) { // if the offset can fit in a signed __int8...
		    Emit8(0x8d); EmitModRmReg(1,ECX_Reg,ESI_Reg); Emit8((unsigned __int8)OffsetToAdd); // LEA ESI, [ECX+Offset]
	        } else {
	            Emit8(0x8d); EmitModRmReg(2,ECX_Reg,ESI_Reg); Emit32(OffsetToAdd); // LEA ESI, [ECX+Offset]
                }
            }
        }
    }
    return CodeLocation;
}

unsigned __int32 LdrUnalignedGuestAddress;

unsigned __int8* PlaceSingleDataTransfer(unsigned __int8* CodeLocation, Decoded *d)
{
    unsigned __int32* TLBIndexCachePointer=NULL;
    unsigned __int32* IOIndexCachePointer=NULL;
    unsigned __int8* AbortExceptionOrIO=NULL;
    unsigned __int8* AbortException;
    unsigned __int8* RaiseAlignmentException=NULL;
    unsigned __int8* RaiseAlignmentException2=NULL;
    bool fNeedsAlignmentCheck=false;
    bool fCacheHit=false;

    LogPlace((CodeLocation, "%s%c Rd=%d, Rn=%d Operand2=%x, %s-indexed, %s%s\n",
        (d->L) ? "LDR" : "STR",
        (d->B) ? 'B' : ' ',
        d->Rd,
        d->Rn,
        d->Operand2,
        (d->P) ? "pre" : "post",
        (d->U) ? "up" : "down",
        (d->W) ? ", writeback" : ""));

    if (d->Rn == R15 && PCCachedAddressIsValid && d->Cond == 14 && d->P && d->B == 0 && d->W == 0 && d->L && d->I == 0) {
        // Unconditional "LDR Rd, [PC+immediate]".  Note that JitOptimizeIR() tries to evaluate "[pc+immediate]" at jit-time, but
        // it is possible that the evaluation failed (say, because the page containing [pc] is in the page table, but
        // the page containing [pc+immediate] isn't).
        unsigned __int32 InstructionPointer = d->Reserved3;
        if ((InstructionPointer & ~0x3ff) == (PCGuestEffectiveAddress & ~0x3ff)) {
            // This LDR can re-use a previous "LDR Rd, [PC+immediate]"'s HostEffectiveAddress:  both
            // "PC+immediate" values are within the same 1k, so they're safely within one page.  This
            // is the second LDR, and it can bypass the MMU.
            fCacheHit=true;
            Emit_MOV_Reg_Reg(EAX_Reg, EDI_Reg); // MOV EAX, EDI (CachedPCHostEffectiveAddress)
            Emit_ADD_Reg_Imm32(EAX_Reg, InstructionPointer - PCGuestEffectiveAddress); // ADD EAX, InstructionPointer - PCGuestEffectiveAddress
            if (InstructionPointer & 3) {
                Emit_MOV_Reg_Imm32(ECX_Reg, InstructionPointer);
            }
        }
    }

    if (!fCacheHit) {
	CodeLocation = PlaceSingleDataTransferOffset(CodeLocation, d, &fNeedsAlignmentCheck);
	// On return:
	//  ECX = address to do the load or store at
	//  If postindex is specified, ESI = new value to store into Rn after the read/write completes

        if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A == 0) {
            if (d->L) {
                Emit_MOV_DWORDPTR_Reg(&LdrUnalignedGuestAddress, ECX_Reg);
            }
            Emit_AND_Reg_Imm32(ECX_Reg, ~3u); // AND ECX, ~3, DWORD-aligning the address
        }

        // At this point, ECX contains the EffectiveAddress, ready for the call to Mmu
        if (Mmu.ControlRegister.Bits.M) {
            // MMU is enabled
            Emit8(0xb8+EDX_Reg); TLBIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &TLBIndexCache
            if (d->L) {
                Emit_CALL(Mmu.MmuMapRead.MapGuestVirtualToHost);
            } else {
                Emit_CALL(Mmu.MmuMapWrite.MapGuestVirtualToHost);
            }
        } else {
            // MMU is disabled - use the faster code-path
            Emit_CALL(Mmu.MapGuestPhysicalToHost);                // CALL Mmu.MapGuestPhysicalToHost
        }

        if (!ProcessorConfig.ARM.BaseRestoredAbortModel) {
            if (d->W) {
                // Base Updated abort model:  perform the write-back before the test for aborts
                ASSERT(d->Rn != R15);
                Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
            }
        }
        Emit_TEST_Reg_Reg(EAX_Reg,EAX_Reg);                        // TEST EAX, EAX            is HostEffectiveAddress == 0 ?
        Emit_JZLabel(AbortExceptionOrIO);                        // JZ AbortExceptionOrIO    brif yes (forward, so predicted not taken)
        // else HostEffectiveAddress is non-NULL so it is a memory address
        if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {    // Needs an alignment check and the MMU has alignment faults enabled
            Emit_TEST_Reg_Imm32(EAX_Reg, 3);                // TEST EAX, 3                is the address aligned properly?
            Emit_JNZLabel(RaiseAlignmentException);            // JNZ RaiseAlignmentException    brif not (fwd, so predicted not taken)
        }

        if (d->W == 0 && d->Cond == 14 && d->B == 0 && d->L && d->I == 0 && d->Rn == R15) {
            // "LDR Rd, [PC+immediate]" without writeback
            Emit_MOV_Reg_Reg(EDI_Reg, EAX_Reg);                    // MOV EDI (CachedPCHostEffectiveAddress), EAX
            PCGuestEffectiveAddress = d->Reserved3;                // Cache the GuestEffectiveAddress
            PCCachedAddressIsValid = true;
        }
    } // !fCacheHit

    // At this point:
    //    EAX contains HostEffectiveAddress, which is non-NULL and is properly aligned.
    //  if (d->W) then ESI contains the write-back value
    if (ProcessorConfig.ARM.BaseRestoredAbortModel && !ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
        if (d->W) {
            // Base Restored abort model:  perform the write-back before completing the load or store
            ASSERT(d->Rn != R15);
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
        }
    }
    if (d->L) {

        // Load
        if (d->B) {
            unsigned __int8* LoadByteDone1;
            unsigned __int8* LoadByteDone2;

            // Transfer byte
            Emit8(0x0f); Emit8(0xb6); EmitModRmReg(0,EAX_Reg,EAX_Reg);// MOVZX EAX, BYTE PTR [EAX]    else load from memory and zero-extend
            Emit_JMPLabel(LoadByteDone1);                            // JMP LoadByteDone

            // At this point, EAX is 0.  BoardIOAddress contains the IOAddress, which may be zero.
            FixupLabel(AbortExceptionOrIO);                            // AbortExceptionOrIO:
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, PtrToLong(&BoardIOAddress)); // MOV EAX, BoardIOAddress
            Emit_TEST_Reg_Reg(EAX_Reg,EAX_Reg);                        // TEST EAX, EAX
            Emit_JZLabel(AbortException);                            // JZ AbortException - BoardIOAddress is also 0

            if (ProcessorConfig.ARM.BaseRestoredAbortModel && !ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
                if (d->W) {
                    // Base Restored abort model:  perform the write-back before completing the load or store
                    ASSERT(d->Rn != R15);
                    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
                }
            }
            Emit8(0xb8+ECX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV ECX, &IODeviceIndex
            Emit_CALL(IOReadByte);                                    // CALL IOReadByte(unsigned __int8* pIOIndexCache)
            Emit8(0x0f); Emit8(0xb6); EmitModRmReg(3,EAX_Reg,EAX_Reg);// MOVZX EAX, AL                zero-extend result to 32-bit
            Emit_JMPLabel(LoadByteDone2);                            // JMP LoadByteDone
            // Store the IOIndexCache value here - it is purely data
            *IOIndexCachePointer=PtrToLong(CodeLocation);
            Emit8(0);

            FixupLabel(AbortException);                                // AbortException:
            Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress)            // MOV ECX, InstructionPointer
            Emit_JMP32(RaiseAbortDataExceptionHelper);                // JMP RaiseAbortDataExceptionHelper

            if (Mmu.ControlRegister.Bits.M) {
                // Store the TLBIndexCache value here - it is purely data
                *TLBIndexCachePointer=PtrToLong(CodeLocation);
                Emit8(0);
            }

            FixupLabel(LoadByteDone1);                                // LoadByteDone:
            FixupLabel(LoadByteDone2);
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd],EAX_Reg);        // MOV DWORD PTR Cpu.GPRs[Rd], EAX  store result in Rd
        } else {
            // Transfer word, accounting for alignment.  Transfers are always word-
            // aligned on the address bus.  The processor then rotates the result
            // around until the byte at the requested effective address is placed in
            // the low byte of Rd.
            unsigned __int8* LoadWordDone1;
            unsigned __int8* LoadWordDone2;

            Emit8(0x8b); EmitModRmReg(0,EAX_Reg,EAX_Reg);            // MOV EAX, DWORD PTR [EAX]        else load from memory
            if (!fCacheHit) {
                Emit_JMPLabel(LoadWordDone1);                        // JMP LoadWordDone

                FixupLabel(AbortExceptionOrIO);                        // AbortExceptionOrIO:
                Emit_MOV_Reg_DWORDPTR(EAX_Reg, PtrToLong(&BoardIOAddress)); // MOV EAX, BoardIOAddress
                Emit_TEST_Reg_Reg(EAX_Reg,EAX_Reg);                    // TEST EAX, EAX
                Emit_JZLabel(AbortException);                        // JZ AbortException - BoardIOAddress is also 0

                if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {
                    Emit_TEST_Reg_Imm32(EAX_Reg, 3);                // TEST EAX, 3                is the address aligned properly?
                    Emit_JNZLabel(RaiseAlignmentException2);        // JNZ RaiseAlignmentException2
                }
                if (ProcessorConfig.ARM.BaseRestoredAbortModel && !ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
                    if (d->W) {
                        // Base Restored abort model:  perform the write-back before completing the load or store
                        ASSERT(d->Rn != R15);
                        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
                    }
                }
                Emit8(0xb8+ECX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV ECX, &IOIndexCache
                Emit_CALL(IOReadWord);                                // CALL IOReadWord(void)
                Emit_JMPLabel(LoadWordDone2);                        // JMP LoadWordDone
                // Store the IOIndexCache value here - it is purely data
                *IOIndexCachePointer=PtrToLong(CodeLocation);
                Emit8(0);

                FixupLabel(AbortException);                            // AbortException:
                Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress)        // MOV ECX, InstructionPointer
                Emit_JMP32(RaiseAbortDataExceptionHelper);            // JMP RaiseAbortDataExceptionHelper

                if (Mmu.ControlRegister.Bits.M) {
                    // Store the TLBIndexCache value here - it is purely data
                    *TLBIndexCachePointer=PtrToLong(CodeLocation);
                    Emit8(0);
                }

                if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {
                    FixupLabel(RaiseAlignmentException);            // RaiseAlignmentException:
                    FixupLabel(RaiseAlignmentException2);
                    // Raise the alignment fault
                    Emit_MOV_Reg_Imm32(ESI_Reg, d->GuestAddress);    // MOV ESI, Instruction Guest Address
                    Emit_CALL(Mmu.RaiseAlignmentException);            // CALL Mmu.RaiseAlignmentException(EffectiveAddress)
                    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);
                    Emit_JMP32(RaiseAbortDataExceptionHelper);        // JMP RaiseAbortDataExceptionHelper
                }
                FixupLabel(LoadWordDone1);                            // LoadWordDone:
                FixupLabel(LoadWordDone2);
            }
            if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A == 0) {
                // LDR, potentially unaligned, and alignment exceptions are masked.  Rotate
                // the value into position.
                Emit_MOV_Reg_DWORDPTR(ECX_Reg, &LdrUnalignedGuestAddress); // MOV ECX, LdrUnalignedGuestAddress
                Emit_AND_Reg_Imm32(ECX_Reg, 3);                        // AND ECX, 3
                Emit_SHL_Reg32_Imm(ECX_Reg, 3);                        // SHL ECX, 3
                // At this point, CL is 0, 8, 16, or 24, depending on the low bits in EAX
                Emit8(0xd3); EmitModRmReg(3,EAX_Reg,1);                // ROR EAX, CL
            }
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);        // MOV DWORD PTR Cpu.GPRs[Rd], EAX  store result in Rd
        }
    } else {
        // Store
        if (d->B) {
            // Transfer byte
            unsigned __int8* StoreByteDone1;
            unsigned __int8* StoreByteDone2;

            if (d->Rd == R15) {
                unsigned __int32 InstructionPointer = d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : ProcessorConfig.ARM.PCStoreOffset);
                Emit8(0xc6); EmitModRmReg(0,EAX_Reg,0); Emit8((unsigned __int8)InstructionPointer); // MOV BYTE PTR [EAX], (byte)R15
            } else {
                Emit8(0x8a); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]); // MOV CL, BYTE PTR Cpu.GPRs[Rd]
                Emit8(0x88); EmitModRmReg(0,EAX_Reg,ECX_Reg);        // MOV BYTE PTR [EAX], CL
            }
            Emit_JMPLabel(StoreByteDone1);                            // JMP StoreByteDone

            if (Mmu.ControlRegister.Bits.M) {
                // Store the TLBIndexCache value here - it is purely data
                *TLBIndexCachePointer=PtrToLong(CodeLocation);
                Emit8(0);
            }

            FixupLabel(AbortExceptionOrIO);                            // AbortExceptionOrIO:
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, PtrToLong(&BoardIOAddress)); // MOV EAX, BoardIOAddress
            Emit_TEST_Reg_Reg(EAX_Reg,EAX_Reg);                        // TEST EAX, EAX
            Emit_JZLabel(AbortException);                            // JZ AbortException - BoardIOAddress is also 0

            if (ProcessorConfig.ARM.BaseRestoredAbortModel && !ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
                if (d->W) {
                    // Base Restored abort model:  perform the write-back before completing the load or store
                    ASSERT(d->Rn != R15);
                    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
                }
            }
            // else it is an IO access
            if (d->Rd == R15) {
                unsigned __int32 InstructionPointer = d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : ProcessorConfig.ARM.PCStoreOffset);
                Emit8(0xb0+CL_Reg); Emit8((unsigned __int8)InstructionPointer); // MOV CL, (byte)R15
            } else {
                Emit8(0x8a); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]); // MOV CL, BYTE PTR Cpu.GPRs[Rd]
            }

            Emit8(0xb8+EDX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &IOIndexCache
            Emit_CALL(IOWriteByte);                                    // CALL IOWriteByte                call IOWriteByte((unsigned __int8)Cpu.GPRs[Rd])
            Emit_JMPLabel(StoreByteDone2);                            // JMP StoreByteDone
            // Store the IOIndexCache value here - it is purely data
            *IOIndexCachePointer=PtrToLong(CodeLocation);
            Emit8(0);
            
            FixupLabel(AbortException);                                // AbortException:
            Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress)            // MOV ECX, InstructionPointer
            Emit_JMP32(RaiseAbortDataExceptionHelper);                // JMP RaiseAbortDataExceptionHelper

            FixupLabel(StoreByteDone1);                                // StoreByteDone:
            FixupLabel(StoreByteDone2);
        } else {
            // Transfer word, accounting for alignment.  Transfers are always word-
            // aligned on the address bus.  The processor then rotates the result
            // around until the byte at the requested effective address is placed in
            // the low byte of Rd.
            unsigned __int8* StoreWordDone1;
            unsigned __int8* StoreWordDone2;

            if (d->Rd == R15) {
                Emit8(0xc7); EmitModRmReg(0,EAX_Reg,0); Emit32(d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : ProcessorConfig.ARM.PCStoreOffset)); // MOV DWORD PTR [EAX], R15
            } else {
                Emit8(0x8b); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]); // MOV ECX, Cpu.GPRs[Rd]        else store DWORD to memory
                Emit8(0x89); EmitModRmReg(0,EAX_Reg,ECX_Reg);        // MOV DWORD PTR [EAX], ECX
            }
            Emit_JMPLabel(StoreWordDone1);                            // JMP StoreWordDone

            FixupLabel(AbortExceptionOrIO);                            // AbortExceptionOrIO:
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, PtrToLong(&BoardIOAddress)); // MOV EAX, BoardIOAddress
            Emit_TEST_Reg_Reg(EAX_Reg,EAX_Reg);                        // TEST EAX, EAX
            Emit_JZLabel(AbortException);                            // JZ AbortException - BoardIOAddress is also 0

            // else it is an IO access
            if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {
                Emit_TEST_Reg_Imm32(EAX_Reg, 3);                    // TEST EAX, 3                is the address aligned properly?
                Emit_JNZLabel(RaiseAlignmentException2);            // JNZ RaiseAlignmentException
            }

            if (ProcessorConfig.ARM.BaseRestoredAbortModel && !ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
                if (d->W) {
                    // Base Restored abort model:  perform the write-back before completing the load or store
                    ASSERT(d->Rn != R15);
                    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
                }
            }
            if (d->Rd == R15) {
                Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : ProcessorConfig.ARM.PCStoreOffset)); // MOV ECX, R15
            } else {
                Emit8(0x8b); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]); // MOV ECX, Cpu.GPRs[Rd]
            }
            Emit8(0xb8+EDX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &IOIndexCache
            Emit_CALL(IOWriteWord);                                    // CALL IOWriteWord                call IOWriteWord(Cpu.GPRs[Rd])
            Emit_JMPLabel(StoreWordDone2);                            // JMP StoreWordDone
            //Store the IOIndexCache value here.  It is purely data
            *IOIndexCachePointer=PtrToLong(CodeLocation);
            Emit8(0)

            FixupLabel(AbortException);                                // AbortException:
            Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress)            // MOV ECX, InstructionPointer
            Emit_JMP32(RaiseAbortDataExceptionHelper);                // JMP RaiseAbortDataExceptionHelper

            if (Mmu.ControlRegister.Bits.M) {
                // Store the TLBIndexCache value here - it is purely data
                *TLBIndexCachePointer=PtrToLong(CodeLocation);
                Emit8(0);
            }

            if (Mmu.ControlRegister.Bits.A && fNeedsAlignmentCheck) {
                FixupLabel(RaiseAlignmentException);            // RaiseAlignmentException:
                FixupLabel(RaiseAlignmentException2);
                // Raise the alignment fault
                Emit_MOV_Reg_Imm32(ESI_Reg, d->GuestAddress);    // MOV ESI, Instruction Guest Address
                Emit_CALL(Mmu.RaiseAlignmentException);            // CALL Mmu.RaiseAlignmentException(EffectiveAddress)
                Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);
                Emit_JMP32(RaiseAbortDataExceptionHelper);        // JMP RaiseAbortDataExceptionHelper
            }
            FixupLabel(StoreWordDone1);                            // StoreWordDone:
            FixupLabel(StoreWordDone2);
        }
    }
    if (ProcessorConfig.ARM.BaseRestoredAbortModel && ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
        if (d->W) {
            // Base Restored abort model:  perform the write-back after completing the load or store
            ASSERT(d->Rn != R15);
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
        }
    }
    if (d->R15Modified) {
        CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
    }
    return CodeLocation;
}

// Helper for this code sequence:
//   MOV R14, PC
//   LDR PC, [ea]
// This is a single data transfer that acts like a CALL instruction, so push the return
// address on the shadow stack then execute the data transfer as normal.
unsigned __int8* PlaceSingleDataTransferCALL(unsigned __int8* CodeLocation, Decoded *d)
{
    CodeLocation = PlacePushShadowStack(CodeLocation);
    return PlaceSingleDataTransfer(CodeLocation, d);
}

// Helper for this code sequence:
//   STR Rx, [ea]
//   LDR R15, [Rx+ea]
// This is the managed method epilog in NetCF v2.
unsigned __int8* PlaceSingleDataTransferRET(unsigned __int8* CodeLocation, Decoded *d)
{
    ASSERT(d->R15Modified);
    d->R15Modified = false; // suppress the R15ModifiedHelper code-gen
    CodeLocation = PlaceSingleDataTransfer(CodeLocation, d);
    d->R15Modified = true;
    Emit_JMP32(PopShadowStackHelper);    // JMP PopShadowStackHelper

    return CodeLocation;
}



unsigned __int8 bitcount(unsigned __int16 bits)
{
    unsigned __int8 count;

    for (count=0; bits; bits >>= 1) {
        count += (unsigned __int8)bits & 1;
    }
    return count;
}

// On entry:  ECX == register number (8-14)
// On exit,   ECX = pointer to "register" image that contains the usermode value of the register
//  EAX is trashed
__declspec(naked) void BlockDataTransferUsermodeHelper(void)
{
    __asm {
        mov eax, Cpu.CPSR
        and eax, 0x1f
        cmp eax, SystemModeValue
        jz  InSystemMode
        cmp eax, IRQModeValue
        jz  InIRQMode
        cmp eax, SupervisorModeValue
        jz  InSupervisorMode
        cmp eax, AbortModeValue
        jz  InAbortMode
        cmp eax, UndefinedModeValue
        jz  InUndefinedMode
        cmp eax, UserModeValue
        jz  InUserMode

        // else in FIQ mode
        // never gets here, FIQ not supported.

InUserMode: // in User or System Mode already - nothing needs to be done
InSystemMode:
        lea    ecx, [Cpu.GPRs+ecx*4]
        retn

InIRQMode:
        cmp ecx, 13
        jb InUserMode
        sub ecx, 13
        lea ecx, [Cpu.GPRs_irq+ecx*4]
        retn

InSupervisorMode:
        cmp ecx, 13
        jb InUserMode
        sub ecx, 13
        lea ecx, [Cpu.GPRs_svc+ecx*4]
        retn

InAbortMode:
        cmp ecx, 13
        jb InUserMode
        sub ecx, 13
        lea ecx, [Cpu.GPRs_abt+ecx*4]
        retn

InUndefinedMode:
        cmp ecx, 13
        jb InUserMode
        sub ecx, 13
        lea ecx, [Cpu.GPRs_und+ecx*4]
        retn
    }
}

unsigned __int32 EndEffectiveAddress;
unsigned __int32 BaseAbortValue;
size_t StartPageHostAddressEnd;
unsigned __int32 StartIOAddress;
unsigned __int32 StartPageIOAddressEnd;
size_t NextPageHostAddress;
unsigned __int32 NextPageIOAddress;

// This routine is called to perform LDM to IO addresses, where the address range is contained
// entirely within a single page.  d->S is 0, and R15 is not in the RegisterList.
//
// On entry, StartIOAddress contains the first IO address to access.  It is still
// also stored in BoardIOAddress, so there is no need to reload.
void __fastcall BlockDataTransferIOLoadHelper(unsigned __int32 RegisterList)
{
    unsigned __int32 RegNum;
    __int8 IOIndexHint = 0;

    ASSERT(BoardIOAddress == StartIOAddress);

    for (RegNum = R0; RegNum <= R14; ++RegNum) {
        if (RegisterList & (1 << RegNum)) {
            // The register is included in the block transfer list
            Cpu.GPRs[RegNum] = IOReadWord(&IOIndexHint);
            BoardIOAddress += 4;
        }
    }
}

// This routine is called to perform STM to IO addresses, where the address range is contained
// entirely within a single page.  d->S is 0, and R15 is not in the RegisterList.
//
// On entry, StartIOAddress contains the first IO address to access.  It is still
// also stored in BoardIOAddress, so there is no need to reload.
void __fastcall BlockDataTransferIOStoreHelper(unsigned __int32 RegisterList)
{
    unsigned __int32 RegNum;
    __int8 IOIndexHint= 0;

    ASSERT(BoardIOAddress == StartIOAddress);

    for (RegNum = R0; RegNum <= R14; ++RegNum) {
        if (RegisterList & (1 << RegNum)) {
            // The register is included in the block transfer list
            IOWriteWord(Cpu.GPRs[RegNum],&IOIndexHint);
            BoardIOAddress += 4;
        }
    }
}

unsigned __int32* GetUserModeRegisterAddress( int RegNum)
{
    unsigned __int32* pReg;

    // Load the usermode registers, regardless of the current mode.  This is done
    // by setting pReg to point to the current address of the register, even
    // if it has been bank-switched away.
    switch (Cpu.CPSR.Bits.Mode) {
    case UserModeValue:
        pReg = &Cpu.GPRs[RegNum];
        break;

    case FIQModeValue:
        ASSERT(FALSE);
        pReg=NULL;
        break;

    case SupervisorModeValue:
        pReg = (RegNum < 13 || RegNum > 14) ? &Cpu.GPRs[RegNum] : &Cpu.GPRs_svc[RegNum-13];
        break;

    case IRQModeValue:
        pReg = (RegNum < 13 || RegNum > 14) ? &Cpu.GPRs[RegNum] : &Cpu.GPRs_irq[RegNum-13];
        break;

    case AbortModeValue:
        pReg = (RegNum < 13 || RegNum > 14) ? &Cpu.GPRs[RegNum] : &Cpu.GPRs_abt[RegNum-13];
        break;

    case UndefinedModeValue:
        pReg = (RegNum < 13 || RegNum > 14) ? &Cpu.GPRs[RegNum] : &Cpu.GPRs_und[RegNum-13];
        break;

    default:
        pReg=NULL;
        ASSERT(FALSE);
        break;
    }

    return pReg;
}


// This routine is called to perform LDM/STM to IO addresses, where one or more
// of the following are true:
//  - d->S == 1
//  - the LDM/STM may span more than one page
//  - R15 is included in the register list
//  - d->W == 1
//
// On entry, the BlockDataTransfer global variables have been set (StartIOAddress, etc.).
// and InstructionAddress is passed only if R15 is included in the register list.
//
// The following flags are ORed into RegisterListAndFlags
#define LDM_STM_S        (1 << 16)
#define LDM_STM_SPANS   (2 << 16)
#define LDM_STM_LOAD    (4 << 16)
#define LDM_STM_W       (8 << 16)
// bits 31-24 are the writeback register number, if LDM_STM_W is set
void __fastcall BlockDataTransferIOHelperSlow(unsigned __int32 RegisterListAndFlags, unsigned __int32 InstructionAddress)
{
    unsigned __int32 RegisterList = RegisterListAndFlags & 0xffff;
    bool fSBit = (RegisterListAndFlags & LDM_STM_S) ? true : false;            // is d->S set?
    bool fSpans = (RegisterListAndFlags & LDM_STM_SPANS) ? true : false;    // does the IOAddress range possibly span a page boundary?
    bool fLoad = (RegisterListAndFlags & LDM_STM_LOAD) ? true : false;
    bool fWriteBack = (RegisterListAndFlags & LDM_STM_W) ? true : false;
    bool fFirst = true;
    unsigned __int32 RegNum;
    __int8 IOIndexHint= 0;

    BoardIOAddress = StartIOAddress;

    for (RegNum = R0; RegNum <= R15; ++RegNum) {
        if (RegisterList & (1 << RegNum)) {
            // The register is included in the block transfer list
            if (fLoad) {
                if (fSBit && (RegisterList & 0x8000) == 0 && RegNum >= R8 && RegNum < R15) {
                    *GetUserModeRegisterAddress(RegNum) = IOReadWord(&IOIndexHint);
                } else if (RegNum == R15) {
                    unsigned __int32 Value;
                    UpdateCPSRWithFlags(Cpu.SPSR);     //recreate x86 flags with our reloaded SPSR register                
                    Value = IOReadWord(&IOIndexHint);
                    if (Cpu.CPSR.Bits.ThumbMode) {
                        Value &= ~1u;
                    } else {
                        Value &= ~3u;
                    }
                    Cpu.GPRs[R15] = Value;
                } else {
                    Cpu.GPRs[RegNum] = IOReadWord(&IOIndexHint);
                }
            } else { // Store
                if (fSBit && RegNum >= R8 && RegNum < R15) {
                    IOWriteWord(*GetUserModeRegisterAddress(RegNum),&IOIndexHint);
                } else if (RegNum == R15) {
                    IOWriteWord(InstructionAddress+ProcessorConfig.ARM.PCStoreOffset,&IOIndexHint);
                } else {
                    IOWriteWord(Cpu.GPRs[RegNum],&IOIndexHint);
                }
            }
            BoardIOAddress += 4;
            if (fFirst) {
                fFirst = false;
                if (fWriteBack) {
                    Cpu.GPRs[RegisterListAndFlags >> 24] = EndEffectiveAddress;
                }

            }
            if (fSpans) {
                if (BoardIOAddress == StartPageIOAddressEnd) {
                    BoardIOAddress = NextPageIOAddress;
                }
            }
        }
    }
}

unsigned __int8* PlaceBlockDataTransfer(unsigned __int8* CodeLocation, Decoded *d)
{
    unsigned __int8 BlockSize;
    unsigned __int32* TLBIndexCachePointer1=NULL;
    unsigned __int8* TLBIndexCachePointer=NULL;
    unsigned __int8* NoAbort1;
    unsigned __int8* NoAbort2;
    unsigned __int8* PossiblyTwoPages=NULL;
    unsigned __int8* NoAbortTwoPage;
    unsigned __int8* AbortDestination;
    unsigned __int8* RaiseUnaligned;
    unsigned __int8* PerformIOBasedTransfer;
    unsigned __int8* PerformIOBasedTransferMultiple=NULL;
    unsigned __int8* DoneInstruction1;
    unsigned __int8* DoneInstruction2=NULL;
    unsigned __int8* DoneInstruction3;
    bool bFirstStore;
    bool bFirstRegister;
    int RegNum;
    unsigned __int8 RegisterOffset;

    LogPlace((CodeLocation,"%s%c%c Rn=%d%s RegisterList=0x%x%s\n",
        (d->L) ? "LDM" : "STM",
        (d->U) ? 'I' : 'D',
        (d->P) ? 'B' : 'A',
        d->Rn,
        (d->W) ? ", writeback" : "",
        d->RegisterList,
        (d->S) ? "^":""));
    ASSERT(d->Rn != R15);

    BlockSize = bitcount(d->RegisterList)*sizeof(unsigned __int32);

    // Compute StartAddress into ESI and value to update into Rn if writeback is enabled into EAX.
    if (d->U) {
        Emit_MOV_Reg_DWORDPTR(ESI_Reg, &Cpu.GPRs[d->Rn]);        // MOV ESI, Cpu.GPRs[Rn]
        if (d->P) { // IB
            // StartAddress = Rn+4
            // EndAddress = Rn + (NumberOfRegisters*4)
            // Writeback = Rn + (NumberOfRegisters*4)
            if (d->W) {
                Emit8(0x8d); EmitModRmReg(1,ESI_Reg,EAX_Reg); Emit8(BlockSize); // LEA EAX, [ESI+BlockSize]
            }
            Emit_ADD_Reg_Imm32(ESI_Reg, 4);                     // ADD ESI, 4
        } else { // IA
            // StartAddress = Rn
            // EndAddress = Rn + (NumberOfRegisters*4)-4
            // Writeback = Rn + (NumberOfRegisters*4)
            if (d->W) {
                Emit8(0x8d); EmitModRmReg(1,ESI_Reg,EAX_Reg); Emit8(BlockSize); // LEA EAX, [ESI+BlockSize]
            }
        }
    } else {
        Emit_MOV_Reg_DWORDPTR(ESI_Reg, &Cpu.GPRs[d->Rn]);        // MOV ESI, Cpu.GPRs[Rn]
        if (d->P) {        // DB
            // StartAddress = Rn - (NumberOfRegisters*4)
            // EndAddress = Rn-4
            // Writeback = Rn-(NumberOfRegisters*4)
            Emit_ADD_Reg_Imm32(ESI_Reg, -BlockSize);            // SUB ESI, BlockSize
            if (d->W) {
                Emit_MOV_Reg_Reg(EAX_Reg, ESI_Reg);                // MOV EAX, ESI
            }
        } else {        // DA
            // StartAddress = Rn-(NumberOfRegisters*4)+4
            // EndAddress = Rn
            // Writeback = Rn - NumberOfRegisters*4
            Emit_ADD_Reg_Imm32(ESI_Reg, -BlockSize+4);            // ADD ESI, -BlockSize+4
            if (d->W) {
                Emit8(0x8d); EmitModRmReg(1,ESI_Reg,EAX_Reg); Emit8((unsigned __int8)-4);    // LEA EAX, [ESI+4]
            }
        }
    }

    if (d->W) {
        if (ProcessorConfig.ARM.BaseRestoredAbortModel) {
            Emit_MOV_Reg_DWORDPTR(EDX_Reg, &Cpu.GPRs[d->Rn]);        // MOV EDX, Cpu.GPRs[Rn]
            Emit_MOV_DWORDPTR_Reg(&BaseAbortValue, EDX_Reg);        // MOV BaseAbortValue, EDX            - BaseAbortValue = Cpu.GPRs[Rn]
        } else {
            Emit_MOV_DWORDPTR_Reg(&BaseAbortValue, EAX_Reg);        // MOV BaseAbortValue, EAX            - BaseAbortValue = EndEffectiveAddress
        }
        if (d->L) {
            // For LDM, update the base immediately.  If the base is included in the register list,
            // that value overwrites the writeback value.
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);    // MOV Cpu.GPRs[Rn], EndEffectiveAddress
        }
        Emit_MOV_DWORDPTR_Reg(&EndEffectiveAddress, EAX_Reg);    // MOV EndEffectiveAddress, EAX
    } else {
        // Otherwise, on data abort, restore the base register to its original
        Emit_MOV_Reg_DWORDPTR(EDX_Reg, &Cpu.GPRs[d->Rn]);        // MOV EAX, Cpu.GPRs[Rn]
        Emit_MOV_DWORDPTR_Reg(&BaseAbortValue, EDX_Reg);        // MOV BaseAbortValue, EAX            - BaseAbortValue = Cpu.GPRs[Rn]
    }

    // At this point:
    // ESI = EffectiveAddress
    Emit_MOV_Reg_Reg(ECX_Reg, ESI_Reg);                            // MOV ECX, ESI                    - copy EffectiveAddress to the argument register
    if (!Mmu.ControlRegister.Bits.A) {
        // Alignment faults are disabled:  4-byte align the effective address before calling the MMU
        Emit_AND_Reg_Imm32(ECX_Reg, ~3u);
    }
    if (Mmu.ControlRegister.Bits.M) {
        Emit8(0xb8+EDX_Reg); TLBIndexCachePointer1=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &TLBIndexCache
        if (d->L) {
            Emit_CALL(Mmu.MmuMapRead.MapGuestVirtualToHost);
        } else {
            Emit_CALL(Mmu.MmuMapWrite.MapGuestVirtualToHost);
        }
    } else {
        Emit_CALL(Mmu.MapGuestPhysicalToHost);                    // CALL Mmu.MapGuestPhysicalToHost
    }
    Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                        // TEST EAX, EAX
    Emit_JNZLabel(NoAbort1);                                    // JNZ NoAbort
    Emit_MOV_Reg_DWORDPTR(ECX_Reg, &BoardIOAddress);            // MOV ECX, BoardIOAddress
    Emit_MOV_DWORDPTR_Reg(&StartIOAddress, ECX_Reg);            // MOV StartIOAddress, ECX
    Emit_TEST_Reg_Reg(ECX_Reg, ECX_Reg);                        // TEST ECX, ECX
    Emit_JNZLabel(NoAbort2);                                    // JNZ NoAbort
    // else raise the exception
    AbortDestination = CodeLocation;                            // AbortDestination:
    Emit_MOV_Reg_DWORDPTR(EAX_Reg, &BaseAbortValue);            // MOV EAX, BaseAbortValue
    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);            // MOV Cpu.GPRs[Rn], restoring Rn back to BaseAbortValue
    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);                // MOV ECX, GuestAddress
    Emit_JMP32(RaiseAbortDataExceptionHelper);                    // JMP RaiseAbortDataExceptionHelper

    if (Mmu.ControlRegister.Bits.M) {
        // Store the TLBIndexCache value here - it is purely data.  Both MMU calls share one index:
        // the assumption is that although the two MMU calls may touch adjacent 1k pages, odds are
        // that they'll really touch the same 4k/64k/1mb page so the TLBIndexCache for the second
        // MMU call ought to get a hit using the cache from the first call.
        TLBIndexCachePointer=CodeLocation;
        *TLBIndexCachePointer1=PtrToLong(CodeLocation);
        Emit8(0);
    }

    if (Mmu.ControlRegister.Bits.A) {
        RaiseUnaligned = CodeLocation;                            // RaiseUnaligned: - EDX = EffectiveAddres on entry
        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &BaseAbortValue);        // MOV EAX, BaseAbortValue
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);        // MOV Cpu.GPRs[Rn], restoring Rn back to BaseAbortValue
        Emit_MOV_Reg_Reg(ECX_Reg, EDX_Reg);                        // MOV ECX, EDX
        Emit_CALL(Mmu.RaiseAlignmentException);                    // CALL Mmu.RaiseAlignmentException
        Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);            // MOV ECX, GuestAddress
        Emit_JMP32(RaiseAbortDataExceptionHelper);                // JMP RaiseAbortDataExceptionHelper
    } else {
        RaiseUnaligned = NULL;
    }

    FixupLabel(NoAbort1);                                        // NoAbort:
    FixupLabel(NoAbort2);
    // At this point, ESI == EffectiveAddress, EAX == StartHostAddress (which may be zero if the address is in IO space),
    // and the stack still has one value on it.
    if (Mmu.ControlRegister.Bits.M && BlockSize > 4) { // MMU enabled and more than one register to load/store
        Emit_MOV_Reg_Reg(EDX_Reg, ESI_Reg);                            // EDX = EffectiveAddress
        Emit_MOV_Reg_Reg(ESI_Reg, EAX_Reg);                            // ESI = StartHostAddress
        Emit_MOV_Reg_Reg(EAX_Reg, EDX_Reg);                            // EAX = EffectiveAddress
        Emit_AND_Reg_Imm32(EAX_Reg, 1023);                            // AND EAX, 1023
        Emit8(0xf7); EmitModRmReg(3,EAX_Reg,3);                        // NEG EAX
        Emit_ADD_Reg_Imm32(EAX_Reg, 1024);                            // ADD EAX, 1024        - EAX = BytesOnStartPage = 1024 - (EffectiveAddress & 1023)
        Emit_CMP_Reg_Imm32(EAX_Reg, BlockSize);                        // CMP EAX, BlockSize    - Is (BytesOnStartPage < BlockSize)?
        Emit_JBLabel32(PossiblyTwoPages);                            // Brif BytesOnStartPage below BlockSize - possibly spanning two pages
    } else {
        Emit_MOV_Reg_Reg(ESI_Reg, EAX_Reg);                            // ESI = StartHostAddress
    }
    // At this point, EDX == EffectiveAddress, ESI = StartHostAddress.  EffectiveAddress is not used again, so really
    // only ESI is live.
    bFirstStore = true;
    bFirstRegister = true;

    Emit_TEST_Reg_Reg(ESI_Reg, ESI_Reg)                            // TEST ESI, ESI    - Is StartHostAddress == 0?
    Emit_JZLabel32(PerformIOBasedTransfer);                        // JZ (32-bit) PerformIOBasedTransfer

    // else Memory-based LDM/STM
    // At this point, ESI = StartHostAddress
    RegisterOffset=0;
    for (RegNum = R0; RegNum <= R15; ++RegNum) {
        if (d->RegisterList & (1 << RegNum)) {
            // The register is included in the block transfer list
            if (d->L) { // Load
                if (d->S && (d->RegisterList & 0x8000) == 0 && RegNum >= R8 && RegNum < R15) {
                    // LDM, R15 not in list and S bit set (User bank transfer)
                    //
                    // At JIT-time, we don't know which mode the CPU is in, but we do know which register is to
                    // be accessed.
                    Emit_MOV_Reg_Imm32(ECX_Reg, RegNum);            // MOV ECX, RegNum
                    Emit_CALL(BlockDataTransferUsermodeHelper);        // CALL BlockDataTransferUsermodeHelper - returns with ECX == ptr to register to store at, EAX is trashed
                    if (RegisterOffset) {
                        Emit8(0x8b); EmitModRmReg(1,ESI_Reg,EAX_Reg); Emit8(RegisterOffset)    // MOV EAX, DWORD PTR [ESI+RegisterOffset]        - load from StartHostAddress
                    } else {
                        Emit8(0x8b); EmitModRmReg(0,ESI_Reg,EAX_Reg);    // MOV EAX, DWORD PTR [ESI]        - load from StartHostAddress
                    }
                    Emit8(0x89); EmitModRmReg(0,ECX_Reg,EAX_Reg);    // MOV DWORD PTR [ECX], EAX        - store to Cpu somewhere
                } else if (RegNum == R15) {
                    if (d->S) {
                        // LDM with R15 in the transfer list and S bit set (Mode changes)
                        Emit_PUSH_DWORDPTR(&Cpu.SPSR.Word);            // PUSH Cpu.SPSR
                        Emit_CALL(UpdateCPSRWithFlags);                // CALL UpdateCPSRWithFlags  (the argument is on the stack, despite this being a __fastcall function)
                        
                    }
                    if (RegisterOffset) {
                        Emit8(0x8b); EmitModRmReg(1,ESI_Reg,EAX_Reg); Emit8(RegisterOffset)    // MOV EAX, DWORD PTR [ESI+RegisterOffset]        - load from StartHostAddress
                    } else {
                        Emit8(0x8b); EmitModRmReg(0,ESI_Reg,EAX_Reg);    // MOV EAX, DWORD PTR [ESI]        - load from StartHostAddress
                    }
                    Emit_MOV_Reg_Imm32(EDX_Reg, ~1u);                                        // MOV EDX, ~1 - Thumb mask
                    Emit_MOV_Reg_Imm32(ECX_Reg, ~3u);                                        // MOV ECX, ~3 - ARM mask masking off both low bits
                    Emit8(0xf7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.CPSR); Emit32(0x20u);    // TEST Cpu.CPSR, ThumbMode
                    Emit16(0x450f); EmitModRmReg(3,EDX_Reg,ECX_Reg);                        // CMOVNE ECX, EDX - if in Thumb mode, set ECX to the Thumb Mask
                    Emit8(0x23); EmitModRmReg(3,ECX_Reg, EAX_Reg);                            // AND EAX, ECX - mask off either 1 or 2 low bits depending on mode
                    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[RegNum], EAX_Reg); // MOV Cpu.GPRs[RegNum], EAX - store to Cpu.GPRs[RegNum]
                    if (d->Rn == R13 && d->P == 0 && d->W && d->U) {
                        // LDM R13, {R15...} with post writeback, and up direction.  Likely a "return" instruction.
                        Emit_JMP32(PopShadowStackHelper);
                    } else {
                        CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
                    }
                } else {
                    if (RegisterOffset) {
                        Emit8(0x8b); EmitModRmReg(1,ESI_Reg,EAX_Reg); Emit8(RegisterOffset);    // MOV EAX, DWORD PTR [ESI+RegisterOffset]        - load from StartHostAddress
                    } else {
                        Emit8(0x8b); EmitModRmReg(0,ESI_Reg,EAX_Reg);    // MOV EAX, DWORD PTR [ESI]        - load from StartHostAddress
                    }
                    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[RegNum], EAX_Reg); // MOV Cpu.GPRs[RegNum], EAX - store to Cpu.GPRs[RegNum]
                }
            } else { // Store
                if (d->S && RegNum >= R8 && RegNum < R15) {
                    // STM with S bit set (User bank transfer)
                    //
                    // At JIT-time, we don't know which mode the CPU is in, but we do know which register is to
                    // be accessed.
                    Emit_MOV_Reg_Imm32(ECX_Reg, RegNum);            // MOV ECX, RegNum
                    Emit_CALL(BlockDataTransferUsermodeHelper);        // CALL BlockDataTransferUsermodeHelper - returns with ECX == ptr to register to store at, EAX is trashed
                    Emit8(0x8b); EmitModRmReg(0,ECX_Reg,EAX_Reg);    // MOV EAX, DWORD PTR [ECX]        - load from the CPU somewhere
                } else if (RegNum == R15) {
                    Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress+ProcessorConfig.ARM.PCStoreOffset); // MOV EAX, R15
                } else {
                    Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[RegNum]);    // MOV EAX, Cpu.GPRs[RegNum]
                }
                if (RegisterOffset) {
                    Emit8(0x89); EmitModRmReg(1,ESI_Reg,EAX_Reg); Emit8(RegisterOffset);        // MOV DWORD PTR [ESI+RegisterOffset], EAX        - store to StartHostAddress
                } else {
                    Emit8(0x89); EmitModRmReg(0,ESI_Reg,EAX_Reg);        // MOV DWORD PTR [ESI], EAX        - store to StartHostAddress
                }
                if (RegisterOffset==0 && d->W) {
                    Emit_MOV_Reg_DWORDPTR(EAX_Reg, &EndEffectiveAddress);    // MOV EAX, EndEffectiveAddress
                    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);        // MOV Cpu.GPRs[Rn], EAX
                }
            }
            RegisterOffset+=4;
        }
    }
    Emit_JMPLabel32(DoneInstruction1);                                    // JMP DoneInstruction;
    if (Mmu.ControlRegister.Bits.M && BlockSize > 4) { // MMU enabled and more than one register to load/store

        FixupLabel32(PossiblyTwoPages);                                // PossiblyTwoPages:
        // At this point, EAX == BytesOnStartPage, ESI = HostStartAddress, EDX = EffectiveAddress
        Emit8(0x8d); EmitModRmReg(0,4,ECX_Reg); EmitSIB(0,ESI_Reg,EAX_Reg);    // LEA ECX, [ESI+EAX]    - ECX = StartHostAddress + BytesOnStartPage
        Emit_MOV_DWORDPTR_Reg(&StartPageHostAddressEnd, ECX_Reg);    // MOV StartPageHostAddressEnd, ECX
        Emit_MOV_Reg_DWORDPTR(ECX_Reg, &StartIOAddress);            // MOV ECX, StartIOAddress
        Emit8(0x03); EmitModRmReg(3,EAX_Reg,ECX_Reg);                // ADD ECX, EAX                    - ECX = StartIOAddress + BytesOnStartPage
        Emit_MOV_DWORDPTR_Reg(&StartPageIOAddressEnd, ECX_Reg);        // MOV StartPageIOAddressEnd, ECX
        Emit8(0x8d); EmitModRmReg(0,4,ECX_Reg); EmitSIB(0,EDX_Reg,EAX_Reg); // LEA ECX, [EDX+EAX]    - ECX = EffectiveAddress + BytesOnStartPage == NextPageGuestAddress
        Emit_PUSH_Reg(EDX_Reg);                                        // PUSH EDX, preserving it across the call to C/C++ code
        if (!Mmu.ControlRegister.Bits.A) {
            // Alignment faults are disabled:  4-byte align the effective address before calling the MMU
            Emit_AND_Reg_Imm32(ECX_Reg, ~3u);
        }
        if (Mmu.ControlRegister.Bits.M) {
            Emit8(0xb8+EDX_Reg); EmitPtr(TLBIndexCachePointer);        // MOV EDX, &TLBIndexCache
            if (d->L) {
                Emit_CALL(Mmu.MmuMapRead.MapGuestVirtualToHost);
            } else {
                Emit_CALL(Mmu.MmuMapWrite.MapGuestVirtualToHost);
            }
        } else {
            Emit_CALL(Mmu.MapGuestPhysicalToHost);                    // CALL Mmu.MapGuestPhysicalToHost
        }
        Emit_POP_Reg(EDX_Reg);                                        // POP EDX, restoring EDX = EffectiveAddress
        Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                        // TEST EAX, EAX
        Emit_MOV_DWORDPTR_Reg(&NextPageHostAddress, EAX_Reg);        // MOV NextPageHostAddress, EAX
        Emit_JNZLabel(NoAbortTwoPage);                                // JNZ NoAbortTwoPage
        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &BoardIOAddress);                // MOV EAX, BoardIOAddress
        Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                        // TEST EAX, EAX
        if (((size_t)AbortDestination-(size_t)CodeLocation-4) < -127) {
            Emit16(0x840f); EmitPtr(AbortDestination-CodeLocation-4);       // JZ (32-bit) AbortDestination  - both NextPageHostAddress and BoardIOAddress are 0
        } else {
            Emit_JZ((unsigned __int8)(AbortDestination-CodeLocation+1));    // JZ AbortDestination  - both NextPageHostAddress and BoardIOAddress are 0
        }
        // else this is an IO address, and the address is in EAX
        Emit_MOV_DWORDPTR_Reg(&NextPageIOAddress, EAX_Reg);            // MOV NextPageIOAddress, EAX
        FixupLabel(NoAbortTwoPage);                                    // NoAbortTwoPage:

        // At this point, EDX == EffectiveAddress, ESI = StartHostAddress, and the 5 global variables are set up
        if (Mmu.ControlRegister.Bits.A) {
            Emit_TEST_Reg_Imm32(EDX_Reg, 3);                            // TEST EDX, 3
            Emit_JNZ((unsigned __int8)(RaiseUnaligned-CodeLocation+1));    // JNZ RaiseUnaligned
        }
        // At this point, EDX == EffectiveAddress, ESI = StartHostAddress.  EffectiveAddress is not used again, so really
        // only ESI is live.
        bFirstStore = true;
        bFirstRegister = true;

        Emit_TEST_Reg_Reg(ESI_Reg, ESI_Reg)                            // TEST ESI, ESI    - Is StartHostAddress == 0?
        Emit_MOV_Reg_DWORDPTR(EDX_Reg, &StartPageHostAddressEnd);   // MOV EDX, StartPageHostAddressEnd
        Emit_JZLabel32(PerformIOBasedTransferMultiple);                // JZ (32-bit) PerformIOBasedTransferMultiple
        // else Memory-based LDM/STM
        for (RegNum = R0; RegNum <= R15; ++RegNum) {
            if (d->RegisterList & (1 << RegNum)) {
                // The register is included in the block transfer list

                if (bFirstRegister) {
                    bFirstRegister = false;
                } else {
                    Emit_ADD_Reg_Imm32(ESI_Reg,4);                    // ADD ESI, 4
                }
                Emit_CMP_Reg_Reg(ESI_Reg, EDX_Reg);                    // CMP ESI, EDX  - is StartHostAddress == StartPageHostAddressEnd?
                Emit16(0x440f); EmitModRmReg(0,5,ESI_Reg); EmitPtr(&NextPageHostAddress); // CMOVE ESI, NextPageHostAddress - begin the next store at the next 1k's native address

                if (d->L) { // Load
                    if (d->S && (d->RegisterList & 0x8000) == 0 && RegNum >= R8 && RegNum < R15) {
                        // At JIT-time, we don't know which mode the CPU is in, but we do know which register is to
                        // be accessed.
                        Emit_MOV_Reg_Imm32(ECX_Reg, RegNum);            // MOV ECX, RegNum
                        Emit_CALL(BlockDataTransferUsermodeHelper);        // CALL BlockDataTransferUsermodeHelper - returns with ECX == ptr to register to store at, EAX is trashed
                        Emit8(0x8b); EmitModRmReg(0,ESI_Reg,EAX_Reg);    // MOV EAX, DWORD PTR [ESI]        - load from StartHostAddress
                        Emit8(0x89); EmitModRmReg(0,ECX_Reg,EAX_Reg);    // MOV DWORD PTR [ECX], EAX        - store to Cpu somewhere
                    } else if (RegNum == R15) {
                        if (d->S) {
                            Emit_PUSH_DWORDPTR(&Cpu.SPSR.Word);            // PUSH Cpu.SPSR
                            Emit_CALL(UpdateCPSRWithFlags);                // CALL UpdateCPSRWithFlags  (the argument is on the stack, despite this being a __fastcall function)
                        }
                        Emit8(0x8b); EmitModRmReg(0,ESI_Reg,EAX_Reg);    // MOV EAX, DWORD PTR [ESI]        - load from StartHostAddress
                        Emit_MOV_Reg_Imm32(EDX_Reg, ~1u);                                        // MOV EDX, ~1 - Thumb mask
                        Emit_MOV_Reg_Imm32(ECX_Reg, ~3u);                                        // MOV ECX, ~3 - ARM mask masking off both low bits
                        Emit8(0xf7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.CPSR); Emit32(0x20u);    // TEST Cpu.CPSR, ThumbMode
                        Emit16(0x450f); EmitModRmReg(3,EDX_Reg,ECX_Reg);                        // CMOVNE ECX, EDX - if in Thumb mode, set ECX to the Thumb Mask
                        Emit8(0x23); EmitModRmReg(3,ECX_Reg, EAX_Reg);                            // AND EAX, ECX - mask off either 1 or 2 low bits depending on mode
                        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[RegNum], EAX_Reg); // MOV Cpu.GPRs[RegNum], EAX - store to Cpu.GPRs[RegNum]
                        if (d->Rn == R13 && d->P == 0 && d->W && d->U) {
                            // LDM R13, {R15...} with post writeback, and up direction.  Likely a "return" instruction.
                            Emit_JMP32(PopShadowStackHelper);
                        } else {
                            CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
                        }
                    } else {
                        Emit8(0x8b); EmitModRmReg(0,ESI_Reg,EAX_Reg);    // MOV EAX, DWORD PTR [ESI]        - load from StartHostAddress
                        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[RegNum], EAX_Reg); // MOV Cpu.GPRs[RegNum], EAX - store to Cpu.GPRs[RegNum]
                    }
                } else { // Store
                    if (d->S && RegNum >= R8 && RegNum < R15) {
                        // At JIT-time, we don't know which mode the CPU is in, but we do know which register is to
                        // be accessed.
                        Emit_MOV_Reg_Imm32(ECX_Reg, RegNum);            // MOV ECX, RegNum
                        Emit_CALL(BlockDataTransferUsermodeHelper);        // CALL BlockDataTransferUsermodeHelper - returns with ECX == ptr to register to store at, EAX is trashed
                        Emit8(0x8b); EmitModRmReg(0,ECX_Reg,EAX_Reg);    // MOV EAX, DWORD PTR [ECX]        - load from the CPU somewhere
                    } else if (RegNum == R15) {
                        Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress+ProcessorConfig.ARM.PCStoreOffset); // MOV EAX, R15
                    } else {
                        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[RegNum]);    // MOV EAX, Cpu.GPRs[RegNum]
                    }
                    Emit8(0x89); EmitModRmReg(0,ESI_Reg,EAX_Reg);        // MOV DWORD PTR [ESI], EAX        - store to StartHostAddress
                    if (bFirstStore && d->W) {
                        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &EndEffectiveAddress);    // MOV EAX, EndEffectiveAddress
                        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);        // MOV Cpu.GPRs[Rn], EAX
                        bFirstStore = false;
                    }
                }
            }
        }
        Emit_JMPLabel32(DoneInstruction2);
    } // if Mmu.ControlRegister.Bits.M && BlockSize > 4

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // IO-based LDM/STM
    FixupLabel32(PerformIOBasedTransfer);                // PerformIOBasedTransfer:
    if (d->W || d->S || (d->RegisterList & (1 << 15))) {        // If we must use the slow IO helper
        unsigned __int32 RegisterListAndFlags;

        RegisterListAndFlags = d->RegisterList;
        if (d->S) {
            RegisterListAndFlags |= LDM_STM_S;
        }
        if (d->L) {
            RegisterListAndFlags |= LDM_STM_LOAD;
        }
        if (d->W) {
            RegisterListAndFlags |= LDM_STM_W | (d->Rn << 24);
        }
        Emit_MOV_Reg_Imm32(ECX_Reg, RegisterListAndFlags); // MOV ECX, RegisterListAndFlags
        if (d->RegisterList & (1 << 15)) {
            Emit_MOV_Reg_Imm32(EDX_Reg, d->GuestAddress); // MOV EDX, d->GuestAddress
        }
        Emit_CALL(BlockDataTransferIOHelperSlow);        // CALL BlockDataTransferIOHelperSlow(RegisterListAndFlags, InstructionAddress);
        if (d->L && (d->RegisterList & (1 << 15))) {        // If this is an LDM and R15 was loaded, do a control transfer
            if (d->Rn == R13 && d->P == 0 && d->W && d->U) {
                // LDM R13, {R15...} with post writeback, and up direction.  Likely a "return" instruction.
                Emit_JMP32(PopShadowStackHelper);
            } else {
                CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
            }
        }
    } else if (d->L) { // d->S is clear and d->L is set, OK to use the fast load helper
        Emit_MOV_Reg_Imm32(ECX_Reg, d->RegisterList);
        Emit_CALL(BlockDataTransferIOLoadHelper);
    } else { // d->S is clear and d->L is clear, OK to use the fast load helper
        Emit_MOV_Reg_Imm32(ECX_Reg, d->RegisterList);
        Emit_CALL(BlockDataTransferIOStoreHelper);
    }
    Emit_JMPLabel32(DoneInstruction3);                                    // JMP DoneInstruction;

    if (Mmu.ControlRegister.Bits.M && BlockSize > 4) { // MMU enabled and more than one register to load/store
        FixupLabel32(PerformIOBasedTransferMultiple);    // PerformIOBasedTransferMultiple:
        // We arrive here only for LDM/STM to an IO address, where the address range may span a pair of pages
        // At this point, BoardIOAddress contains the IO address of the beginning of the range
        Emit_MOV_Reg_Imm32(ECX_Reg, d->RegisterList | LDM_STM_SPANS | ((d->S) ? LDM_STM_S : 0) | ((d->L) ? LDM_STM_LOAD : 0)); // MOV ECX, RegisterList|Flags
        if (d->RegisterList & (1 << 15)) {
            Emit_MOV_Reg_Imm32(EDX_Reg, d->GuestAddress); // MOV EDX, d->GuestAddress
        }
        Emit_CALL(BlockDataTransferIOHelperSlow);        // CALL BlockDataTransferIOHelperSlow(RegisterListAndFlags, InstructionAddress);
        if (d->L && (d->RegisterList & (1 << 15))) {        // If this is an LDM and R15 was loaded, do a control transfer
            if (d->Rn == R13 && d->P == 0 && d->W && d->U) {
                // LDM R13, {R15...} with post writeback, and up direction.  Likely a "return" instruction.
                Emit_JMP32(PopShadowStackHelper);
            } else {
                CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
            }
        }
    }

    FixupLabel32(DoneInstruction1);                        // DoneInstruction:
    FixupLabel32(DoneInstruction3);
    if (Mmu.ControlRegister.Bits.M && BlockSize > 4) { // MMU enabled and more than one register to load/store
        FixupLabel32(DoneInstruction2);
    }
    return CodeLocation;
}

// On entry, ECX = branch destination address, return address points into the
// translation cache.
__declspec(naked) void BranchHelper(void)
{
    __asm {
        mov Cpu.GPRs[15*4], ecx    // set R15 to the branch destination
        call EPFromGuestAddrExact
        test eax, eax
        pop ecx                // ecx = return address within Translation Cache
        jz MustCompile
        // Else there is an ENTRYPOINT descriving the branch destination address.  Update
        // the branch in the Translation Cache so it jumps directly to the native
        // destination in the future.
        mov esi, [eax+8]    // esi = ep->nativeStart
        lea eax, [esi-5]    // eax will become a pc-relative offset to ep->nativeStart
        sub ecx, 10         // back up ecx to point to the start of the "MOV ECX" instruction
        sub eax, ecx        // change ep->nativeStart to a Rel32 offset
        mov byte ptr [ecx], 0xe9    // place a JMP32 opcode
        mov dword ptr [ecx+1], eax  // place the rel32 offset to ep->nativeStart
        push 5
        push ecx
        push -1
        call dword ptr [FlushInstructionCache] // call FlushInstructionCache(GetCurrentProcess(), ecx, 5) to flush the modified code

        jmp esi                // jmp directly to the destination

MustCompile:
        retn                // return back to C code
    }
}

unsigned __int8* PlaceBranch(unsigned __int8* CodeLocation, Decoded *d)
{
    ENTRYPOINT *ep;

    if (d->L) { // BL - Branch with link
        LogPlace((CodeLocation,"BL %8.8x\n", d->Offset));
        Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[R14]); Emit32(d->GuestAddress+4); // MOV Cpu.GPRs[R14], address of next ARM instruction
        CodeLocation = PlacePushShadowStack(CodeLocation);
    } else {
        LogPlace((CodeLocation,"B 0x%8.8x\n", d->Offset));
    }
    if ((unsigned __int32)d->Reserved3 <= d->GuestAddress) {
        // Backward branches (or branches to self) must poll for interrupts
        CodeLocation = PlaceInterruptPoll(CodeLocation, d);
    }
    if (d->Reserved3 >= Instructions[0].GuestAddress && d->Reserved3 <= Instructions[NumberOfInstructions-1].GuestAddress) {
        int Offset = (d->Reserved3 - Instructions[0].GuestAddress) / ((Cpu.CPSR.Bits.ThumbMode) ? 2 : 4);
      
        if (Instructions[Offset].Entrypoint->GuestStart == MmuActualGuestAddress(d->Reserved3)){
            // The destination is within the window of instructions being jitted, and it has
            // been identified as the start of a basic block.  Leave space to generate a JMP
            // directly to it, to be fixed up later by JitApplyFixups() once the destination
            // code has been generated.
            d->JmpFixupLocation = CodeLocation;
            CodeLocation+=5;  // Leave space for an Emit_JMP32 to be placed here.
            return CodeLocation;
        }
    }
    ep = EPFromGuestAddrExact(d->Offset);
    if (ep) {
        // the branch destination has already been jitted
        Emit_JMP32(ep->nativeStart);            // JMP ep->nativeStart
    } else {
        Emit_MOV_Reg_Imm32(ECX_Reg, d->Reserved3);    // MOV ECX, destination  (6 bytes)
        Emit_CALL(BranchHelper);                // CALL BranchHelper     (5 bytes)
    }
    return CodeLocation;
}


unsigned __int8* PlaceIdleLoop(unsigned __int8* CodeLocation, Decoded *d)
{
    Emit_PUSH32(INFINITE);                    // PUSH INFINITE
    Emit_PUSHPtr(hIdleEvent);                // PUSH hIdleEvent
    Emit_CALL(WaitForSingleObject);            // CALL WaitForSingleObject(hIdleEvent, INFINITE);
    return PlaceBranch(CodeLocation, d);
}

unsigned __int8* __fastcall PlaceIllegalCoproc(unsigned __int8* CodeLocation, Decoded *d)
{
    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);            // MOV ECX, GuestAddress
    Emit_CALL(CpuRaiseUndefinedException);                    // CALL CpuRaiseUndefinedException
    Emit_RETN(0);                                            // RETN

    return CodeLocation;
}

unsigned __int8* PlaceCoprocessorPermissionCheck(unsigned __int8* CodeLocation, Decoded* d)
{
    if (d->ActualGuestAddress < 0x80000000) {
        unsigned __int8* PermissionGranted1;
        unsigned __int8* PermissionGranted2;

        Emit8(0xa0); EmitPtr(&Cpu.CPSR.Partial_Word);        // MOV AL Cpu.CPSR.Partial_Word
        Emit8(0x24); Emit8(0x1f);                            // AND AL, ModeMask
        Emit8(0x3c); Emit8(UserModeValue);                    // CMP AL, UserModevalue
        Emit_JNZLabel(PermissionGranted1);                    // JNZ PermissionGranted
        Emit8(0xf7); EmitModRmReg(0,5,0); EmitPtr(&Mmu.CoprocessorAccess); Emit32(1<<d->CPNum); // TEST Mmu.CoprocessorAccess, (1<<d->CPNum)
        Emit_JNZLabel(PermissionGranted2);                    // JNZ PermissionGranted
        Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);        // MOV ECX, GuestAddress
        Emit_CALL(CpuRaiseUndefinedException);                // CALL CpuRaiseUndefinedException
        Emit_RETN(0);                                        // RETN
        FixupLabel(PermissionGranted1);                        // PermissionGranted:
        FixupLabel(PermissionGranted2);
    }
    return CodeLocation;
}

unsigned __int8* PlaceCoprocDataTransfer(unsigned __int8* CodeLocation, Decoded *d)
{
    LogPlace((CodeLocation,"%cDC Rn=%d, CR=%d, CP=%d, Offset=%8.8x\n",
        (d->L) ? 'L' : 'S',
        d->Rn,
        d->CRd,
        d->CPNum,
        d->Offset));

    CodeLocation = PlaceCoprocessorPermissionCheck(CodeLocation, d);
    CodeLocation = (PlaceCoprocDataTransfers[d->CPNum])(CodeLocation, d);
    return CodeLocation;
}

unsigned __int8* PlaceCoprocDataOperation(unsigned __int8* CodeLocation, Decoded* d)
{
    LogPlace((CodeLocation,"CDP p%d, CRn=%d, CRd=%d, CP=%d, CRm=%d\n",
        d->CPNum,
        d->CRn,
        d->CRd,
        d->CP,
        d->CRm));

    CodeLocation = PlaceCoprocessorPermissionCheck(CodeLocation, d);
    CodeLocation = (*PlaceCoprocDataOperations[d->CPNum])(CodeLocation, d);
    return CodeLocation;
}

unsigned __int8* PlaceCoprocRegisterTransfer(unsigned __int8* CodeLocation, Decoded* d)
{
    LogPlace((CodeLocation,"%s p%d CRn=%d CP=%d, Rd=%d\n",
        (d->L) ? "MRC" : "MCR",
        d->CPNum,
        d->CRn,
        d->CP,
        d->Rd));

    CodeLocation = PlaceCoprocessorPermissionCheck(CodeLocation, d);
    CodeLocation = (*PlaceCoprocRegisterTransfers[d->CPNum])(CodeLocation, d);
    return CodeLocation;
}


unsigned __int8* PlaceSyscall(unsigned __int8* CodeLocation, Decoded* d)
{
    LogPlace((CodeLocation, "Syscall\n"));

    Emit_CALL(PerformSyscall);    // CALL PerformSyscall

    return CodeLocation;
}

PVOID CpuRaiseSoftwareInterruptException(unsigned __int32 GuestAddress)
{
    PSR NewPSR;
    PSR_FULL OldPSR;
    // Switch to Supervisor Mode, and bank-switch in that mode's registers
    OldPSR.Word = GetCPSRWithFlags();        //Get the CPSR with Flag bits
    NewPSR.Partial_Word = OldPSR.Word;
    NewPSR.Bits.Mode = SupervisorModeValue;
    NewPSR.Bits.ThumbMode = 0;
    NewPSR.Bits.IRQDisable = 1; // Disable normal interrupts
    UpdateCPSR(NewPSR);
    Cpu.SPSR.Word = OldPSR.Word;
    Cpu.GPRs[R14] = GuestAddress+((OldPSR.Bits.ThumbMode) ? 2 : 4);

    // Now jump to address 8
    Cpu.GPRs[R15] = offsetof(INTERRUPT_VECTORS, SoftwareInterruptException);
    if (Mmu.ControlRegister.Bits.M && Mmu.ControlRegister.Bits.V) {
        Cpu.GPRs[R15] |= 0xffff0000;
    }
    if (NativeSoftwareInterruptAddress == 0) {
        ENTRYPOINT *ep;

        ep = EPFromGuestAddrExact(Cpu.GPRs[R15]);
        if (ep) {
            NativeSoftwareInterruptAddress = ep->nativeStart;
        }
    }
    return NativeSoftwareInterruptAddress;
}

unsigned __int8* PlaceSoftwareInterrupt(unsigned __int8* CodeLocation, Decoded* d)
{
    unsigned __int8* NotInCache;

    LogPlace((CodeLocation, "SWI\n"));

    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);    // MOV ECX, GuestAddress
    Emit_CALL(CpuRaiseSoftwareInterruptException);    // CALL CpuRaiseSoftwareInterruptException
    Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);            // TEST EAX, EAX
    Emit_JZLabel(NotInCache);                        // JZ NotInCache
    Emit8(0xff); EmitModRmReg(3,EAX_Reg,4);            // JMP EAX
    FixupLabel(NotInCache);                            // NotInCache:                    
    Emit_RETN(0);                                    // RETN

    return CodeLocation;
}

unsigned __int8* PlaceNop(unsigned __int8* CodeLocation, Decoded* d)
{
    return CodeLocation;
}

void PowerDown()
{
    StateFiler filer;
    filer.Save();
}

unsigned __int8* PlacePowerDown(unsigned __int8* CodeLocation, Decoded* d){

    Emit_CALL(PowerDown);

    // file was saved. exit gracefully
    Emit_PUSH32(0);
    Emit_CALL(exit);

    return CodeLocation;
}

unsigned __int8* PlaceEntrypointMiddle(unsigned __int8* CodeLocation, Decoded *d)
{
    // Deliver pending interrupts
    CodeLocation = PlaceInterruptPoll(CodeLocation, d);

    // Then fall through into the next basic block
    return CodeLocation;
}

// on entry, ESI is the update location, and EAX is the ENTRYPOINT pointer
__declspec(naked) void EntrypointEndHelper(void)
{
    __asm {
        mov eax, [eax+8]            // eax = EP->nativeAddress
        mov byte ptr [esi], 0xe9    // begin updating in a "JMP Rel32" opcode
        sub eax, esi
        sub eax, 5                    // eax is now a Rel32 value
        mov dword ptr [esi+1], eax
        push 5
        push esi
        push 0xffffffff
        call dword ptr [FlushInstructionCache]    // call FlushInstructionCache(GetCurrentProcess(), updatelocation, 5);
        jmp  esi                    // jump back and execute the updated instruction
    }
}

unsigned __int8* PlaceEntrypointEnd(unsigned __int8* CodeLocation, Decoded* d)
{
    ENTRYPOINT *ep;
    unsigned __int8* NotInCache;
    unsigned __int8* UpdateLocation;

    // Deliver pending interrupts
    CodeLocation = PlaceInterruptPoll(CodeLocation, d);
    UpdateLocation = CodeLocation;

    ep = EPFromGuestAddrExact(d->GuestAddress);
    if (ep) {
        // The next GuestAddress has already been jitted, so jump directly to it
        Emit_JMP32(ep->nativeStart);                        // JMP ep->nativeStart
    } else {
        // Call EPFromGuestAddrExact, and if it returns an ENTRYPOINT
        Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);        // MOV ECX, GuestAddress
        Emit_CALL(EPFromGuestAddrExact);                    // CALL EPFromGuestAddrExact
        Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                // TEST EAX, EAX
        Emit_JZLabel(NotInCache);                            // JZ NotInCache

        // else replace the code at UpdateLocation by a jmp to EP->nativeStart
        Emit_MOV_Reg_Imm32(ESI_Reg, PtrToLong(UpdateLocation));    // MOV ESI, UpdateLocation
        Emit_JMP32(EntrypointEndHelper);                    // JMP EntrypointEndHelper

        FixupLabel(NotInCache);                                // NotInCache:
        Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[R15]); Emit32(d->GuestAddress); // MOV Cpu.GPRs[R15], GuestAddress
        Emit_RETN(0);                                        // RETN
    }

    return CodeLocation;
}

unsigned __int8* PlaceArithmeticExtension(unsigned __int8* CodeLocation, Decoded* d)
{
    switch (d->Op1) {
    case 0: // MUL, MULS
        LogPlace((CodeLocation,"MUL Rd=%d, Rm=%d, Rs=%d\n", d->Rd, d->Rm, d->Rs));
        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rm]);            // MOV EAX, Cpu.GPRs[Rm]
        Emit8(0xf7); EmitModRmReg(0,5,4); EmitPtr(&Cpu.GPRs[d->Rs]);// MUL EAX, Cpu.GPRs[Rs], producing result in EDX:EAX
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);            // MOV Cpu.GPRs[Rd], EAX
        if (d->S) {
            // Carry flag is unaffected
            // OverflowFlag is unaffected
            // Zero and Sign flags are set according to the 32-bit result
            Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);
            CodeLocation = PlaceUpdateX86Flags(CodeLocation,d, true);
        }
        break;

    case 1: // MLA, MLAS
        LogPlace((CodeLocation,"MLA%s Rd=%d, Rm=%d, Rs=%d, Rn=%d\n",
               (d->S) ? "S" : "",
               d->Rd, d->Rm, d->Rs, d->Rn));

        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rm]);            // MOV EAX, Cpu.GPRs[Rm]
        Emit8(0xf7); EmitModRmReg(0,5,4); EmitPtr(&Cpu.GPRs[d->Rs]);// MUL EAX, Cpu.GPRs[Rs], producing result in EDX:EAX
        Emit8(0x03); EmitModRmReg(0,5,EAX_Reg); EmitPtr(&Cpu.GPRs[d->Rn]); // ADD EAX, Cpu.GPRs[Rn]
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);            // MOV Cpu.GPRs[Rd], EAX
        if (d->S) {
            // Carry flag is unaffected
            // OverflowFlag is unaffected
            // Zero and Sign flags are set according to the 32-bit result
            Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);
            CodeLocation = PlaceUpdateX86Flags(CodeLocation, d, true);
        }
        break;

    case 4: // UMULL, UMULLS
        LogPlace((CodeLocation,"UMULL%s Rn=%d Rd=%d, Rs=%d, Rm=%d\n",
            (d->S) ? "S" : "",
            d->Rn, d->Rd,
            d->Rs, d->Rm));

        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rm]);        // MOV EAX, Cpu.GPRs[Rm]
        Emit8(0xf7); EmitModRmReg(0,5,4); EmitPtr(&Cpu.GPRs[d->Rs]);// MUL EAX, Cpu.GPRs[Rs], producing result in EDX:EAX
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EDX_Reg);        // MOV Cpu.GPRs[Rd], EDX
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);        // MOV CPu.GPRs[Rn], EAX
        if (d->S) {
            CodeLocation = PlaceUpdateLLX86Flags(CodeLocation);
        }
        break;

    case 5: // UMLAL, UMLALS
        LogPlace((CodeLocation,"UMLAL%s Rn=%d Rd=%d, Rs=%d, Rm=%d\n",
            (d->S) ? "S" : "",
            d->Rn, d->Rd,
            d->Rs, d->Rm));

        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rm]);                    // MOV EAX, Cpu.GPRs[Rm]
        Emit8(0xf7); EmitModRmReg(0,5,4); EmitPtr(&Cpu.GPRs[d->Rs]);        // MUL EAX, Cpu.GPRs[Rs], producing result in EDX:EAX
        Emit8(0x03); EmitModRmReg(0,5,EAX_Reg); EmitPtr(&Cpu.GPRs[d->Rn]);    // ADD EAX, Cpu.GPRs[Rn]
        Emit8(0x13); EmitModRmReg(0,5,EDX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]);    // ADC EDX, Cpu.GPRs[Rd]
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EDX_Reg);                    // MOV Cpu.GPRs[Rd], EDX
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);                    // MOV CPu.GPRs[Rn], EAX
        if (d->S) {
            CodeLocation = PlaceUpdateLLX86Flags(CodeLocation);
        }
        break;

    case 6: // SMULL, SMULLS
        LogPlace((CodeLocation,"SMULL%s Rn=%d Rd=%d, Rs=%d, Rm=%d\n",
            (d->S) ? "S" : "",
            d->Rn, d->Rd,
            d->Rs, d->Rm));

        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rm]);                    // MOV EAX, Cpu.GPRs[Rm]
        Emit8(0xf7); EmitModRmReg(0,5,5); EmitPtr(&Cpu.GPRs[d->Rs]);        // IMUL EAX, Cpu.GPRs[Rs], producing result in EDX:EAX
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EDX_Reg);                    // MOV Cpu.GPRs[Rd], EDX
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);                    // MOV CPu.GPRs[Rn], EAX
        if (d->S) {
            CodeLocation = PlaceUpdateLLX86Flags(CodeLocation);
        }
        break;

    case 7: // SMLAL, SMLALS
        LogPlace((CodeLocation,"SMLAL Rn=%d Rd=%d, Rs=%d, Rm=%d\n",
            d->Rn, d->Rd,
            d->Rs, d->Rm));

        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rm]);                    // MOV EAX, Cpu.GPRs[Rm]
        Emit8(0xf7); EmitModRmReg(0,5,5); EmitPtr(&Cpu.GPRs[d->Rs]);        // IMUL EAX, Cpu.GPRs[Rs], producing result in EDX:EAX
        Emit8(0x03); EmitModRmReg(0,5,EAX_Reg); EmitPtr(&Cpu.GPRs[d->Rn]);    // ADD EAX, Cpu.GPRs[Rn]
        Emit8(0x13); EmitModRmReg(0,5,EDX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]);    // ADC EDX, Cpu.GPRs[Rd]
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EDX_Reg);                    // MOV Cpu.GPRs[Rd], EDX
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);                    // MOV CPu.GPRs[Rn], EAX
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EDX_Reg);                    // MOV Cpu.GPRs[Rd], EDX
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], EAX_Reg);                    // MOV CPu.GPRs[Rn], EAX
        if (d->S) {
            CodeLocation = PlaceUpdateLLX86Flags(CodeLocation);
        }
        break;

    default:
        Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);
        Emit_CALL(CpuRaiseUndefinedException);
        break;
    }
    return CodeLocation;
}

unsigned __int8* PlaceMSRImmediate(unsigned __int8* CodeLocation, Decoded* d)
{
    unsigned __int32 FieldMask;
    
    FieldMask = ComputePSRMaskValue(d->Rn); // d->Rn is the FieldMask, not a register number.
    if (FieldMask == 0) {
        // Some MSR opcodes don't specify a field mask.  They are likely data, since lack of
        // a field mask makes the MSR a no-op.
        return CodeLocation;
    }

    // MSR (immediate form)
    if (d->Op1 & 2) {
        // MSR SPSR, imm
        LogPlace((CodeLocation,"MSR SPSR, Imm=%x Mask=%x\n", d->Immediate, d->Rn));
        Emit_MOV_Reg_Imm32(EDX_Reg, FieldMask);            // MOV EDX, FieldMask
        Emit_MOV_Reg_Imm32(ECX_Reg, d->Immediate);        // MOV ECX, d->Immediate, the NewPSRValue
        Emit_PUSH_M32(&Cpu.SPSR.Word);                    // PUSH Cpu.SPSR
        Emit_CALL(UpdatePSRMask);                        // CALL UpdatePSRMask(Cpu.SPSR, d->Immediate, Mask);
        Emit_MOV_DWORDPTR_Reg(&Cpu.SPSR.Word, EAX_Reg);        // MOV Cpu.SPSR, EAX
    } else {
        // MSR CPSR, imm
        LogPlace((CodeLocation,"MSR CPSR, Imm=%x Mask=%x\n", d->Immediate, d->Rn));
        if (d->Rn == 8) {                // if not updating the control bits then use a faster code-path
            Emit_MOV_Reg_Imm32(ECX_Reg, d->Immediate);  // MOV ECX, d->Immediate
            Emit_CALL(UpdateFlags)
        } else {                                        // otherwise must call UpdateCPSRWithFlags to bank-switch, etc
            Emit_CALL(GetCPSRWithFlags);                // Get the Full CPSR representation with Arm Flags
            Emit_MOV_Reg_Imm32(EDX_Reg, FieldMask);        // MOV EDX, FieldMask
            Emit_MOV_Reg_Imm32(ECX_Reg, d->Immediate);    // MOV ECX, d->Immediate, the NewPSRValue
            Emit_PUSH_Reg(EAX_Reg);                        // PUSH EAX
            Emit_CALL(UpdatePSRMask);                    // CALL UpdatePSRMask(Cpu.CPSR_Full, d->Immediate, Mask);
            Emit_PUSH_Reg(EAX_Reg);                        // PUSH EAX
            Emit_CALL(UpdateCPSRWithFlags);                // CALL UpdateCPSRWithFlags, passing the return value from UpdatePSRMask
        }
    }
    return CodeLocation;
}

unsigned __int8* PlaceMRSorMSR(unsigned __int8* CodeLocation, Decoded* d)
{
    unsigned __int32 FieldMask;

    ASSERT(Cpu.CPSR.Bits.ThumbMode == 0);
    
    
switch (d->Op1) {
    case 0:
        LogPlace((CodeLocation,"MRS Rd=%d, CPSR\n", d->Rd));
        Emit_CALL(GetCPSRWithFlags);
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);
        break;

    case 1:
        // MSR CPSR, Rm
        LogPlace((CodeLocation,"MSR CPSR, Rm=%d, Mask=%x\n", d->Rm, d->Rn));
        FieldMask = ComputePSRMaskValue(d->Rn);
        
        if (FieldMask) { // Some MSR opcodes don't specify a field mask.  They are likely data, since lack of
                         // a field mask makes the MSR a no-op.
            
                                                // CALL UpdatePSRMask(Cpu.CPSR, Cpu.GPRs[d->Rm], Mask);
            if (d->Rn == 8) {                            // if not updating the control bits then use a faster code-path
                Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rm]);            // MOV ECX, Cpu.GPRs[d->Rm]
                Emit_CALL(UpdateFlags)
            } else {                                                        // otherwise must call UpdateCPSR to bank-switch, etc
                Emit_CALL(GetCPSRWithFlags);                                // Get the Full CPSR representation with Arm Flags
                Emit_MOV_Reg_Imm32(EDX_Reg, FieldMask);                        // MOV EDX, FieldMask
                Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rm]);            // MOV ECX, Cpu.GPRs[d->Rm]
                Emit_PUSH_Reg(EAX_Reg);                                        // PUSH EAX
                Emit_CALL(UpdatePSRMask);                                    // CALL UpdatePSRMask(Cpu.CPSR_Full, Cpu.GPRs[d->Rm], FieldMask);
                Emit_PUSH_Reg(EAX_Reg);                                        // PUSH EAX
                Emit_CALL(UpdateCPSRWithFlags);                                // CALL UpdateCPSR, passing the return value from UpdatePSRMask
            }
        }
        break;

    case 2:
        LogPlace((CodeLocation,"MRS Rd=%d, SPSR\n", d->Rd));
        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.CPSR);                    // MOV EAX, Cpu.CPSR
        Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.SPSR);                    // MOV ECX, Cpu.SPSR
        Emit_AND_Reg_Imm32(EAX_Reg, 0x1f);                            // AND EAX, 0x1f        - EAX = Cpu.CPSR.Bits.Mode
        Emit_CMP_Reg_Imm32(EAX_Reg, UserModeValue);                    // CMP EAX, UserModeValue
        Emit16(0x440f); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]); // CMOVE ECX, Cpu.GPRs[d->Rd] - if usermode, set ecx to GPRs[Rd]
        Emit_CMP_Reg_Imm32(EAX_Reg, SystemModeValue)                // CMP EAX, SystemModeValue
        Emit16(0x440f); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]); // CMOVE ECX, Cpu.GPRs[d->Rd] - if systemmode, set ecx to GPRs[Rd]
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], ECX_Reg);            // MOV Cpu.GPRs[d->Rd], ECX
        break;

    case 3:
        // MSR SPSR, Rm
        LogPlace((CodeLocation,"MSR SPSR, Rm=%d, Mask=%x\n", d->Rm, d->Rn));
        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.CPSR);                    // MOV EAX, Cpu.CPSR
        Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rm]);            // MOV ECX, Cpu.GPRs[d->Rm]
        Emit_AND_Reg_Imm32(EAX_Reg, 0x1f);                            // AND EAX, 0x1f        - EAX = Cpu.CPSR.Bits.Mode
        Emit_CMP_Reg_Imm32(EAX_Reg, UserModeValue);                    // CMP EAX, UserModeValue

        Emit16(0x440f); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.SPSR); // CMOVE ECX, Cpu.SPSR - if usermode, set ecx to SPSR
        Emit_CMP_Reg_Imm32(EAX_Reg, SystemModeValue)                // CMP EAX, SystemModeValue
        Emit16(0x440f); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.SPSR); // CMOVE ECX, Cpu.SPSR - if systemmode, set ecx to SPSR
        Emit_MOV_DWORDPTR_Reg(&Cpu.SPSR, ECX_Reg);                    // MOV Cpu.SPSR, ECX
        break;

    default:
        ASSERT(FALSE);
    }

    return CodeLocation;
}

unsigned __int8* PlaceBxHelper(unsigned __int8* CodeLocation, Decoded* d, bool fCall)
{
    unsigned __int8* SwitchToThumb;

    LogPlace((CodeLocation,"BX Rd=%d\n", d->Rd));

    // Note: this routine calls PlaceR15ModifiedHelper twice, deliberately.
    // This allows one BX instruction to cache both a Thumb and an ARM
    // destination independently.

    Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rd]);        // MOV EAX, Cpu.GPRs[Rd]
    Emit_TEST_Reg_Imm32(EAX_Reg, 1);                        // TEST EAX, 1
    Emit_JNZLabel(SwitchToThumb);                            // JNZ SwitchToThumb
    // else remain in ARM mode
    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R15], EAX_Reg);            // MOV Cpu.GPRs[R15], EAX
    if (fCall || d->Rd == R15 || d->Rd == R12) {            // BX R15 or BX R12 or a BX that is part of a CALL (because the previous opcode was a "MOV LR, PC")
        CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d); // and jump to the branch destination
    } else {                                                // it is a return address of some sort
        Emit_JMP32(PopShadowStackHelper);                    // JMP PopShadowStackHelper
    }

    FixupLabel(SwitchToThumb);                                // SwitchToThumb:
    Emit8(0x81); EmitModRmReg(0,5,1); EmitPtr(&Cpu.CPSR); Emit32(0x20); // OR Cpu.CPSR, 0x20    CPU.CPSR.Bits.ThumbMode=1
    Emit_AND_Reg_Imm32(EAX_Reg, 0xfffffffe);                // AND EAX, 0xfffffffe
    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R15], EAX_Reg);            // MOV Cpu.GPRs[R15], EAX
    if (fCall || d->Rd == R15 || d->Rd == R12) {            // BX R15 or BX R12 or a BX that is part of a CALL (because the previous opcode was a "MOV LR, PC")
        CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d); // and jump to the branch destination
    } else {                                                // it is a return address of some sort
        Emit_JMP32(PopShadowStackHelper);                    // JMP PopShadowStackHelper
    }

    return CodeLocation;
}

unsigned __int8* PlaceBx(unsigned __int8* CodeLocation, Decoded *d)
{
    return PlaceBxHelper(CodeLocation, d, false);
}

unsigned __int8* PlaceBxCALL(unsigned __int8* CodeLocation, Decoded* d)
{
    CodeLocation = PlacePushShadowStack(CodeLocation);
    return PlaceBxHelper(CodeLocation, d, true);
}

unsigned __int8* PlaceBKPT(unsigned __int8* CodeLocation, Decoded* d)
{
    LogPlace((CodeLocation, "BKPT\n"));
    CodeLocation = PlaceRaiseAbortPrefetchException(CodeLocation, d);
    return CodeLocation;
}

unsigned __int8* PlaceLoadStoreExtension(unsigned __int8* CodeLocation, Decoded *d)
{
    if (d->Op1 == 0) {
        unsigned __int32* TLBIndexCachePointer=NULL;
        unsigned __int32* IOIndexCachePointer=NULL;
        unsigned __int32* IOIndexCachePointer2=NULL;
        unsigned __int8* AbortExceptionOrIO;
        unsigned __int8* AbortException;
        unsigned __int8* SwapDone1;
        unsigned __int8* SwapDone2;

        ASSERT(d->Rd != R15 && d->Rn != R15 && d->Rm != R15); // the instruction decoder checks for R15 for us

        if (d->B) {    // SWPB
            LogPlace((CodeLocation,"SWPB Rd=%d Rm=%d [Rn=%d]\n", d->Rd, d->Rm, d->Rn));

            Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rn]);        // MOV ECX, Cpu.GPRs[Rn]
            if (Mmu.ControlRegister.Bits.M) {
                // MMU is enabled
                Emit8(0xb8+EDX_Reg); TLBIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &TLBIndexCache
                Emit_CALL(Mmu.MmuMapReadWrite.MapGuestVirtualToHost);
            } else {
                // MMU is disabled - use the faster code-path
                Emit_CALL(Mmu.MapGuestPhysicalToHost);                // CALL Mmu.MapGuestPhysicalToHost
            }
            Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                    // TEST EAX                    is HostEffectiveAddress == 0?
            Emit_JZLabel(AbortExceptionOrIO);                        // JZ AbortExceptionOrIO    brif yet (forward, so predicted not taken)

            Emit8(0x8a); EmitModRmReg(0,5,DL_Reg); EmitPtr(&Cpu.GPRs[d->Rm]); // MOV DL, BYTE PTR Cpu.GPRs[Rm]
            Emit8(0x0f); Emit8(0xb6); EmitModRmReg(0,EAX_Reg,ESI_Reg);// MOVZX ESI, BYTE PTR [EAX]   load and zero-extend
            Emit8(0x88); EmitModRmReg(0,EAX_Reg,DL_Reg);            // MOV BYTE PTR [EAX], DL
            Emit_JMPLabel(SwapDone1);                                // JMP SwapDone

            FixupLabel(AbortExceptionOrIO);                            // AbortExceptionOrIO:
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, PtrToLong(&BoardIOAddress)); // MOV EAX, BoardIOAddress
            Emit_TEST_Reg_Reg(EAX_Reg,EAX_Reg);                        // TEST EAX, EAX
            Emit_JZLabel(AbortException);                            // JZ AbortException - BoardIOAddress is also 0

            // else it is an IO access
            Emit8(0xb8+ECX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV ECX, &IODeviceIndex
            Emit_CALL(IOReadByte);                                    // AL = byte read from IO
            Emit8(0x0f); Emit8(0xb6); EmitModRmReg(3,EAX_Reg,ESI_Reg); // MOVZX ESI, AL                zero-extend into callee-preserved register
            Emit8(0x8a); EmitModRmReg(0,5,CL_Reg); EmitPtr(&Cpu.GPRs[d->Rm]); // MOV CL, BYTE PTR Cpu.GPRs[Rm]
            Emit8(0xb8+EDX_Reg); IOIndexCachePointer2=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &IODeviceIndex
            Emit_CALL(IOWriteByte);                                    // CALL IOWriteByte((unsigned __int8)Cpu.GPRs[Rm])
            Emit_JMPLabel(SwapDone2);                                // JMP SwapDone
            // Store the IOIndexCache value here - it is purely data
            #pragma prefast(suppress:11, "Performance reasons")
            *IOIndexCachePointer=PtrToLong(CodeLocation);
            *IOIndexCachePointer2=PtrToLong(CodeLocation);
            Emit8(0);


            FixupLabel(AbortException);                                // AbortException:
            Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress)            // MOV ECX, InstructionPointer
            Emit_JMP32(RaiseAbortDataExceptionHelper);            // JMP RaiseAbortDataExceptionHelper

            if (Mmu.ControlRegister.Bits.M) {
                // Store the TLBIndexCache value here - it is purely data
                #pragma prefast(suppress:11, "Performance reasons")
                *TLBIndexCachePointer=PtrToLong(CodeLocation);
                Emit8(0);
            }

            FixupLabel(SwapDone1);                                    // SwapDone:
            FixupLabel(SwapDone2);
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], ESI_Reg);        // MOV Cpu.GPRs[Rd], ESI
        } else {  // SWP
            LogPlace((CodeLocation,"SWPB Rd=%d Rm=%d [Rn=%d]\n", d->Rd, d->Rm, d->Rn));

            Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rn]);        // MOV ECX, Cpu.GPRs[Rn]
            if (Mmu.ControlRegister.Bits.M) {
                // MMU is enabled
                Emit8(0xb8+EDX_Reg); TLBIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &TLBIndexCache
                Emit_CALL(Mmu.MmuMapReadWrite.MapGuestVirtualToHost);
            } else {
                // MMU is disabled - use the faster code-path
                Emit_CALL(Mmu.MapGuestPhysicalToHost);                // CALL Mmu.MapGuestPhysicalToHost
            }
            Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                    // TEST EAX                    is HostEffectiveAddress == 0?
            Emit_JZLabel(AbortExceptionOrIO);                        // JZ AbortExceptionOrIO    brif yet (forward, so predicted not taken)

            Emit_MOV_Reg_DWORDPTR(ESI_Reg, &Cpu.GPRs[d->Rm]);        // MOV ESI, Cpu.GPRs[Rm]
            Emit8(0x87); EmitModRmReg(0,EAX_Reg,ESI_Reg);            // XCHG ESI, DWORD PTR [EAX]    swap Rm and memory
            Emit_JMPLabel(SwapDone1);                                // JMP SwapDone

            FixupLabel(AbortExceptionOrIO);                            // AbortExceptionOrIO:
            Emit_MOV_Reg_DWORDPTR(EAX_Reg, PtrToLong(&BoardIOAddress)); // MOV EAX, BoardIOAddress
            Emit_TEST_Reg_Reg(EAX_Reg,EAX_Reg);                        // TEST EAX, EAX
            Emit_JZLabel(AbortException);                            // JZ AbortException - BoardIOAddress is also 0

            // else it is an IO access
            Emit8(0xb8+ECX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV ECX, &IODeviceIndex
            Emit_CALL(IOReadWord);                                    // EAX = word read from IO
            Emit_MOV_Reg_Reg(ESI_Reg, EAX_Reg);                        // MOV ESI, EAX            preserve ESI across the next CALL
            Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rm]);        // MOV ECX, Cpu.GPRs[Rm]
            Emit8(0xb8+EDX_Reg); IOIndexCachePointer2=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &IODeviceIndex
            Emit_CALL(IOWriteWord);                                    // CALL IOWriteWord(Cpu.GPRs[Rm])
            Emit_JMPLabel(SwapDone2);                                // JMP SwapDone
            // Store the IOIndexCache value here - it is purely data
            *IOIndexCachePointer=PtrToLong(CodeLocation);
            *IOIndexCachePointer2=PtrToLong(CodeLocation);
            Emit8(0);

            FixupLabel(AbortException);                                // AbortException:
            Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress)            // MOV ECX, InstructionPointer
            Emit_JMP32(RaiseAbortDataExceptionHelper);                // JMP RaiseAbortDataExceptionHelper

            if (Mmu.ControlRegister.Bits.M) {
                // Store the TLBIndexCache value here - it is purely data
                *TLBIndexCachePointer=PtrToLong(CodeLocation);
                Emit8(0);
            }
            FixupLabel(SwapDone1);                                    // SwapDone:
            FixupLabel(SwapDone2);
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], ESI_Reg);        // MOV Cpu.GPRs[Rd], ESI
        }
    } else {
        unsigned __int32* TLBIndexCachePointer=NULL;
        unsigned __int32* IOIndexCachePointer=NULL;
        unsigned __int8* LoadStoreDone1=NULL;
        unsigned __int8* LoadStoreDone2=NULL;
        unsigned __int8* UnalignedAccess1=NULL;
        unsigned __int8* UnalignedAccess2=NULL;
        unsigned __int8* AbortExceptionOrIO;
        unsigned __int8* AbortException;
        bool fNeedsAlignmentCheck = (d->H) ? true : false; // assume LDRH/STRH need an alignment check, and LDRB/STRB do not

        // HalfWordSignedTransferRegister or Immediate
        if (d->Reserved3 == 0) {
            // register offset

            LogPlace((CodeLocation,"%sR%s%c Rd=%d Rm=%d [Rn=%d]\n",
                (d->L) ? "LD" : "ST",
                (d->S) ? "S" : "",
                (d->H) ? 'H' : 'B',
                d->Rd,
                d->Rm,
                d->Rn));

            if (d->Rn == R15) {
                Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8)); // MOV ECX, R15
            } else {
                Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rn]);        // MOV ECX, Cpu.GPRs[Rn]
            }

            if (d->P) {    // Pre-Index
                if (d->U) { // Up
                    if (d->Rm == R15) {
                        Emit_ADD_Reg_Imm32(ECX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8));// ADD ECX, R15
                    } else {
                        Emit_ADD_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rm]); // ADD ECX, Cpu.GPRs[Rm]
                    }
                } else {
                    if (d->Rm == R15) {
                        Emit_SUB_Reg_Imm32(ECX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8));// SUB ECX, R15
                    } else {
                        Emit_SUB_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rm]); // SUB ECX, Cpu.GPRs[Rm]
                    }
                }
            }
        } else {
            // immediate offset

            LogPlace((CodeLocation,"%sR%s%c Rd=%d Offset=%4.4x [Rn=%d]\n",
                (d->L) ? "LD" : "ST",
                (d->S) ? "S" : "",
                (d->H) ? 'H' : 'B',
                d->Rd,
                d->Offset,
                d->Rn));

            if (d->Rn == R15) {
                unsigned __int32 InstructionPointer;

                InstructionPointer = d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8);
                if (d->P) {
                    if (d->U) {
                        InstructionPointer += d->Offset;
                    } else {
                        InstructionPointer -= d->Offset;
                    }
                }
                Emit_MOV_Reg_Imm32(ECX_Reg, InstructionPointer); // MOV ECX, R15 +/- offset if pre-indexed
                if ((InstructionPointer & 1) == 0) {
                    fNeedsAlignmentCheck = false;
                }
            } else {
                Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rn]);        // MOV ECX, Cpu.GPRs[Rn]
            }

            if (d->P && d->Rn != R15) {
                // Pre-index
                if (d->U) {
                    Emit_ADD_Reg_Imm32(ECX_Reg, d->Offset);        // ADD ECX, Offset
                } else {
                    Emit_SUB_Reg_Imm32(ECX_Reg, d->Offset);        // SUB ECX, Offset (6 bytes)
                }
            }
        }

        if (d->W) {
            Emit_MOV_Reg_Reg(ESI_Reg, ECX_Reg);                    // MOV ESI, ECX            preserve the EffectiveAddress across the MMU call
        }

        if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A == 0) { // possibly unaligned LDRH/STRH
            // Unaligned LDRH/STRH has UNPREDICTABLE behavior according to the ARM ARM.  The
            // emulator chooses to round the effective address down to halfword alignment.  This makes
            // STRH compatible with STR, but LDRH and LDR behave differently, since LDR rotates the
            // value according to the degree of misalignment.
            Emit_AND_Reg_Imm32(ECX_Reg, ~1u);                    // AND ECX, ~1            mask off the low bit to force alignment
        }
        if (Mmu.ControlRegister.Bits.M) {
            Emit8(0xb8+EDX_Reg); TLBIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &TLBIndexCache
            if (d->L) {
                Emit_CALL(Mmu.MmuMapRead.MapGuestVirtualToHost);
            } else {
                Emit_CALL(Mmu.MmuMapWrite.MapGuestVirtualToHost);
            }
        } else {
            Emit_CALL(Mmu.MapGuestPhysicalToHost);
        }

        if (d->W) {
            // Writeback
            if (d->P == 0) { // Post-index
                if (d->Reserved3 == 0) { // Register offset
                    if (d->U) { // up
                        if (d->Rm == R15) {
                            Emit_ADD_Reg_Imm32(ESI_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8));// ADD ESI, R15
                        } else {
                            Emit_ADD_Reg_DWORDPTR(ESI_Reg, &Cpu.GPRs[d->Rm]);                        // ADD ESI, Cpu.GPRs[Rm]
                        }
                    } else { // down
                        if (d->Rm == R15) {
                            Emit_SUB_Reg_Imm32(ESI_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8));// SUB ESI, R15
                        } else {
                            Emit_SUB_Reg_DWORDPTR(ESI_Reg, &Cpu.GPRs[d->Rm]);                        // SUB ESI, Cpu.GPRs[Rm]
                        }
                    }
                } else { // immediate offset
                    if (d->U) {
                        Emit_ADD_Reg_Imm32(ESI_Reg, d->Offset);                                        // ADD ESI, Offset
                    } else {
                        Emit_SUB_Reg_Imm32(ESI_Reg, d->Offset)                    ;                    // SUB ESI, Offset
                    }
                }
            }
        }

        if (!ProcessorConfig.ARM.BaseRestoredAbortModel) {
            // Base Updated abort model: perform the write-back before the test for aborts
            if (d->W) { // writeback
                Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);
            }
        }
        Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                    // TEST EAX, EAX
        Emit_JZLabel(AbortExceptionOrIO);                        // JZ AbortExceptionOrIO
        // else memory access
        if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {
            // check for unaligned address
            Emit_TEST_Reg_Imm32(EAX_Reg, 1);                    // TEST EAX, 1
            Emit_JNZLabel(UnalignedAccess1);                    // JNZ UnalignedAccess
        }

        if (ProcessorConfig.ARM.BaseRestoredAbortModel && !ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
            if (d->W) { // writeback
                // Base Restored abort model:  perform the write-back before completing the load or store
                Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);
            }
        }
        if (d->L) { // Load
            if (d->H) { // halfword
                if (d->S) {
                    Emit16(0xbf0f); EmitModRmReg(0,EAX_Reg,EAX_Reg); // MOVSX EAX, WORD PTR [EAX]
                } else {
                    Emit16(0xb70f); EmitModRmReg(0,EAX_Reg,EAX_Reg); // MOVZX EAX, WORD PTR [EAX]
                }
            } else { // byte (signed)
                ASSERT(d->S == 1);
                Emit16(0xbe0f); EmitModRmReg(0,EAX_Reg,EAX_Reg); // MOVSX EAX, BYTE PTR [EAX]
            }
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);
        } else { // store
            if (d->H) { // halfword
                if (d->Rd == R15) {
                    Emit_SIZE16(); Emit8(0xc7); EmitModRmReg(0,EAX_Reg,0); Emit16((unsigned __int16)(d->GuestAddress+ProcessorConfig.ARM.PCStoreOffset)); // MOV WORD PTR [EAX], R15

                } else {
                    Emit_SIZE16(); Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rd]); // MOV ECX, Cpu.GPRs[d->Rd]
                    Emit_SIZE16(); Emit8(0x89); EmitModRmReg(0,EAX_Reg, ECX_Reg);    // MOV WORD PTR [EAX], CX
                }
            } else { // byte
                if (d->Rd == R15) {
                    Emit8(0xc6); EmitModRmReg(0,EAX_Reg,0); Emit8((unsigned __int8)(d->GuestAddress+ProcessorConfig.ARM.PCStoreOffset)); // MOV BYTE PTR [EAX], R15
                } else {
                    Emit8(0x8a); EmitModRmReg(0,5,CL_Reg) EmitPtr(&Cpu.GPRs[d->Rd]);    // MOV CL, BYTE PTR Cpu.GPRs[d->Rd]
                    Emit8(0x88); EmitModRmReg(0,EAX_Reg, CL_Reg);                        // MOV BYTE PTR [EAX], CL
                }
            }
        }
        if (d->R15Modified) {
                    if (ProcessorConfig.ARM.BaseRestoredAbortModel && ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
                        if (d->W) { // writeback
                            // Base Restored abort model:  perform the write-back after completing the load or store
                            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);
                        }
                     }
            CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
        } else {
            Emit_JMPLabel(LoadStoreDone1);
        }
        FixupLabel(AbortExceptionOrIO);
        Emit_MOV_Reg_DWORDPTR(EAX_Reg, PtrToLong(&BoardIOAddress)); // MOV EAX, BoardIOAddress
        Emit_TEST_Reg_Reg(EAX_Reg,EAX_Reg);                        // TEST EAX, EAX
        Emit_JZLabel(AbortException);                            // JZ AbortException - BoardIOAddress is also 0
        // else IO
        if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {
            // check for unaligned address
            Emit_TEST_Reg_Imm32(EAX_Reg, 1);                    // TEST EAX, 1
            Emit_JNZLabel(UnalignedAccess2);                    // JNZ UnalignedAccess
        }
        if (ProcessorConfig.ARM.BaseRestoredAbortModel && ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
            if (d->W) { // writeback
                // Base Restored abort model:  perform the write-back before completing the load or store
                Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);
            }
        }
        if (d->L) { // load
            if (d->H) { // halfword
                Emit8(0xb8+ECX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV ECX, &IODeviceIndex
                Emit_CALL(IOReadHalf);                        // CALL IOReadHalf
                if (d->S) {
                    Emit16(0xbf0f); EmitModRmReg(3,EAX_Reg,ECX_Reg); // MOVSX ECX, AX
                } else {
                    Emit16(0xb70f); EmitModRmReg(3,EAX_Reg, ECX_Reg); // MOVZX ECX, AX
                }
                Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], ECX_Reg);
            } else { // byte
                Emit8(0xb8+ECX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV ECX, &IODeviceIndex
                Emit_CALL(IOReadByte);                        // CALL IOReadByte

                ASSERT(d->S == 1);
                Emit16(0xbe0f); EmitModRmReg(3,AL_Reg,ECX_Reg);// MOVSZ ECX, AL
                Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], ECX_Reg); // MOV Cpu.GPRs[d->Rd], ECX
            }
        } else { // store
            if (d->H) { // halfword
                if (d->Rd == R15) {
                    Emit_MOV_Reg_Imm32(ECX_Reg, (unsigned __int16)(d->GuestAddress+ProcessorConfig.ARM.PCStoreOffset)); // MOV ECX, (uint16)R15
                } else {
                    Emit16(0xb70f); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]); // MOVZX ECX, WORD PTR Cpu.GPRs[d->Rd]
                }
                Emit8(0xb8+EDX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &IODeviceIndex
                Emit_CALL(IOWriteHalf);                    // CALL IOWriteHalf
            } else { // byte
                if (d->Rd == R15) {
                    Emit_MOV_Reg_Imm32(ECX_Reg, (unsigned __int8)(d->GuestAddress+ProcessorConfig.ARM.PCStoreOffset)); // MOV ECX, (uint8)R15
                } else {
                    Emit16(0xb60f); EmitModRmReg(0,5,ECX_Reg); EmitPtr(&Cpu.GPRs[d->Rd]);    // MOVZX ECX, BYTE PTR Cpu.GPRs[d->Rd]
                }
                Emit8(0xb8+EDX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &IODeviceIndex
                Emit_CALL(IOWriteByte);                    // CALL IOWriteByte
            }
        }
        if (d->R15Modified) {
                    if (ProcessorConfig.ARM.BaseRestoredAbortModel && ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
                        if (d->W) { // writeback
                            // Base Restored abort model:  perform the write-back after completing the load or store
                            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);
                        }
                     }
             CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
        } else {
            Emit_JMPLabel(LoadStoreDone2);
        }

        FixupLabel(AbortException);                            // AbortException:
        Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress)        // MOV ECX, InstructionPointer
        Emit_JMP32(RaiseAbortDataExceptionHelper);            // JMP RaiseAbortDataExceptionHelper
        // Store the IOIndexCache value here - it is purely data
        *IOIndexCachePointer=PtrToLong(CodeLocation);
        Emit8(0);        

        if (Mmu.ControlRegister.Bits.M) {
            // Store the TLBIndexCache value here - it is purely data
            *TLBIndexCachePointer=PtrToLong(CodeLocation);
            Emit8(0);
        }

        if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {
            FixupLabel(UnalignedAccess1);                        // UnalignedAccess:
            FixupLabel(UnalignedAccess2);
            Emit_MOV_Reg_Reg(ECX_Reg, ESI_Reg);                // MOV ECX, ESI   (ESI is EffectiveAddress)
            Emit_CALL(Mmu.RaiseAlignmentException);            // CALL Mmu.RaiseAlignmentException(EffectiveAddress)
            Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);
            Emit_JMP32(RaiseAbortDataExceptionHelper);        // JMP RaiseAbortDataExceptionHelper
        }

        if (!d->R15Modified) {
            FixupLabel(LoadStoreDone1);                                // LoadStoreDone:
            FixupLabel(LoadStoreDone2);
        }
    }
    if (ProcessorConfig.ARM.BaseRestoredAbortModel && ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
        if (d->W) { // writeback
            // Base Restored abort model:  perform the write-back after completing the load or store
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);
        }
    }

    return CodeLocation;
}

// cloned from PlaceLoadStoreExtension
unsigned __int8* PlaceDoubleLoadStoreExtension(unsigned __int8* CodeLocation, Decoded *d)
{
    unsigned __int32* TLBIndexCachePointer=NULL;
    unsigned __int32* IOIndexCachePointer=NULL;
    unsigned __int32* IOIndexCachePointer2=NULL;
    unsigned __int8* UnalignedAccess1=NULL;
    unsigned __int8* UnalignedAccess2=NULL;
    unsigned __int8* AbortExceptionOrIO;
    unsigned __int8* AbortException;
    unsigned __int8* LoadStoreDone1;
    unsigned __int8* LoadStoreDone2;
    bool fNeedsAlignmentCheck = true; // assume the instruction needs an alignment check

    // Immediate
    if (d->I == 0) {
        // register offset

        LogPlace((CodeLocation,"%sRD Rd=%d Rm=%d [Rn=%d]\n",
            (d->L) ? "LD" : "ST",
            d->Rd,
            d->Rm,
            d->Rn));

        if (d->Rn == R15) {
            Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8)); // MOV ECX, R15
        } else {
            Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rn]);        // MOV ECX, Cpu.GPRs[Rn]
        }

        if (d->P) {    // Pre-Index
            if (d->U) { // Up
                if (d->Rm == R15) {
                    Emit_ADD_Reg_Imm32(ECX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8));// ADD ECX, R15
                } else {
                    Emit_ADD_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rm]); // ADD ECX, Cpu.GPRs[Rm]
                }
            } else {
                if (d->Rm == R15) {
                    Emit_SUB_Reg_Imm32(ECX_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8));// SUB ECX, R15
                } else {
                    Emit_SUB_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rm]); // SUB ECX, Cpu.GPRs[Rm]
                }
            }
        }
    } else {
        // immediate offset

        LogPlace((CodeLocation,"%sRD Rd=%d Offset=%4.4x [Rn=%d]\n",
            (d->L) ? "LD" : "ST",
            d->Rd,
            d->Offset,
            d->Rn));

        if (d->Rn == R15) {
            unsigned __int32 InstructionPointer;

            InstructionPointer = d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8);
            if (d->P) {
                if (d->U) {
                    InstructionPointer += d->Offset;
                } else {
                    InstructionPointer -= d->Offset;
                }
            }
            Emit_MOV_Reg_Imm32(ECX_Reg, InstructionPointer); // MOV ECX, R15 +/- offset if pre-indexed
            if ((InstructionPointer & 7) == 0) {
                fNeedsAlignmentCheck = false;
            }
        } else {
            Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rn]);        // MOV ECX, Cpu.GPRs[Rn]
        }

        if (d->P && d->Rn != R15) {
            // Pre-index
            if (d->U) {
                Emit_ADD_Reg_Imm32(ECX_Reg, d->Offset);                // ADD ECX, Offset
            } else {
                Emit_SUB_Reg_Imm32(ECX_Reg, d->Offset);                // SUB ECX, Offset
            }
        }
    }

    if (d->W) {
        Emit_MOV_Reg_Reg(ESI_Reg, ECX_Reg);                    // MOV ESI, ECX            preserve the EffectiveAddress across the MMU call
    }

    if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A == 0) {
        Emit_AND_Reg_Imm32(ECX_Reg, ~7u);                    // AND ECX, ~7        mask off the low 3 bits to force alignment
    }
    if (Mmu.ControlRegister.Bits.M) {
        Emit8(0xb8+EDX_Reg); TLBIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &TLBIndexCache
        if (d->L) {
            Emit_CALL(Mmu.MmuMapRead.MapGuestVirtualToHost);
        } else {
            Emit_CALL(Mmu.MmuMapWrite.MapGuestVirtualToHost);
        }
    } else {
        Emit_CALL(Mmu.MapGuestPhysicalToHost);
    }

    if (d->W) {
        // Writeback
        if (d->P == 0) { // Post-index
            if (d->I == 0) { // Register offset
                if (d->U) { // up
                    if (d->Rm == R15) {
                        Emit_ADD_Reg_Imm32(ESI_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8));// ADD ESI, R15
                    } else {
                        Emit_ADD_Reg_DWORDPTR(ESI_Reg, &Cpu.GPRs[d->Rm]); // ADD ESI, Cpu.GPRs[Rm]
                    }
                } else { // down
                    if (d->Rm == R15) {
                        Emit_SUB_Reg_Imm32(ESI_Reg, d->GuestAddress + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8));// SUB ESI, R15
                    } else {
                        Emit_SUB_Reg_DWORDPTR(ESI_Reg, &Cpu.GPRs[d->Rm]); // SUB ESI, Cpu.GPRs[Rm]
                    }
                }
            } else { // immediate offset
                if (d->U) {
                    Emit_ADD_Reg_Imm32(ESI_Reg, d->Offset);                    // ADD ESI, Offset
                } else {
                    Emit_SUB_Reg_Imm32(ESI_Reg, d->Offset);                    // SUB ESI, Offset
                }
            }
        }
    }

    if (!ProcessorConfig.ARM.BaseRestoredAbortModel) {
        if (d->W) { // writeback
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);
        }
    }
    Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                    // TEST EAX, EAX
    Emit_JZLabel(AbortExceptionOrIO);                        // JZ AbortExceptionOrIO
    // else memory access
    if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {
        // check for unaligned address
        Emit_TEST_Reg_Imm32(EAX_Reg, 7);                    // TEST EAX, 7
        Emit_JNZLabel(UnalignedAccess1);                    // JNZ UnalignedAccess
    }

    if (ProcessorConfig.ARM.BaseRestoredAbortModel && !ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
        if (d->W) { // writeback
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
        }
    }

    if (d->L) { // Load
        Emit_MOVQ_MMReg_QWORDAtReg(MM0_Reg,EAX_Reg);        // MOVQ MM0,[EAX]
        Emit_MOVQ_QWORDPTR_MMReg(&Cpu.GPRs[d->Rd],MM0_Reg); // MOVQ Cpu.GPRs[d->Rd],MM0
    } else { // store
        Emit_MOVQ_MMReg_QWORDPTR(MM0_Reg,&Cpu.GPRs[d->Rd]); // MOVQ MM0,Cpu.GPRs[d->Rd]
        Emit_MOVQ_QWORDAtReg_MMReg(EAX_Reg,MM0_Reg);        // MOVQ [EAX],MM0
    }
    Emit_EMMS();                                            // EMMS
    Emit_JMPLabel(LoadStoreDone1);                            // JMP LoadStoreDone1

    FixupLabel(AbortExceptionOrIO);                            // AbortExceptionOrIO:
    Emit_MOV_Reg_DWORDPTR(EAX_Reg, PtrToLong(&BoardIOAddress)); // MOV EAX, BoardIOAddress
    Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);                    // TEST EAX, EAX
    Emit_JZLabel(AbortException);                            // JZ AbortException
    if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {
        // check for unaligned address
        Emit_TEST_Reg_Imm32(EAX_Reg, 7);                    // TEST EAX, 7
        Emit_JNZLabel(UnalignedAccess2);                    // JNZ UnalignedAccess
    }
    if (ProcessorConfig.ARM.BaseRestoredAbortModel && !ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
        if (d->W) { // writeback
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
        }
    }
    if (d->L) { // Load
        Emit8(0xb8+ECX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV ECX, &IODeviceIndex
        Emit_CALL(IOReadWord);                                // CALL IOReadWord
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);    // MOV Cpu.GPRs[Rd], EAX
        Emit8(0x83); EmitModRmReg(0,5,0); EmitPtr(&BoardIOAddress); Emit8(4); // ADD BoardIOAddress, 4
        Emit8(0xb8+ECX_Reg); IOIndexCachePointer2=(unsigned __int32*)CodeLocation; Emit32(0); // MOV ECX, &IODeviceIndex
        Emit_CALL(IOReadWord);                                // CALL IOReadWord
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd+1], EAX_Reg);    // MOV Cpu.GPRs[Rd+1], EAX
    } else {
        Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rd]);    // MOV ECX, Cpu.GPRs[Rd]
        Emit8(0xb8+EDX_Reg); IOIndexCachePointer=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &IODeviceIndex
        Emit_CALL(IOWriteWord);                                // CALL IOWriteWord
        Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rd+1]);    // MOV ECX, Cpu.GPRs[Rd+1]
        Emit8(0xb8+EDX_Reg); IOIndexCachePointer2=(unsigned __int32*)CodeLocation; Emit32(0); // MOV EDX, &IODeviceIndex
        Emit_CALL(IOWriteWord);                                // CALL IOWriteWord
    }
    Emit_JMPLabel(LoadStoreDone2);                            // JMP LoadStoreDone2
    // Store the IOIndexCache value here - it is purely data
    *IOIndexCachePointer=PtrToLong(CodeLocation);
    *IOIndexCachePointer2=PtrToLong(CodeLocation);
    Emit8(0);

    FixupLabel(AbortException);                                // AbortException:
    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);            // MOV ECX, InstructionPointer
    Emit_JMP32(RaiseAbortDataExceptionHelper);                // JMP RaiseAbortDataExceptionHelper

    if (Mmu.ControlRegister.Bits.M) {
        // Store the TLBIndexCache value here - it is purely data
        *TLBIndexCachePointer=PtrToLong(CodeLocation);
        Emit8(0);
    }

    if (fNeedsAlignmentCheck && Mmu.ControlRegister.Bits.A) {
        FixupLabel(UnalignedAccess1);                        // UnalignedAccess:
        FixupLabel(UnalignedAccess2);
        // Raise the alignment fault
        Emit_MOV_Reg_Imm32(ESI_Reg, d->GuestAddress);        // MOV ESI, Instruction Guest Address
        Emit_CALL(Mmu.RaiseAlignmentException);                // CALL Mmu.RaiseAlignmentException
        Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);        // MOV ECX, Instruction Guest Address
        Emit_JMP32(RaiseAbortDataExceptionHelper);            // JMP RaiseAbortDataExceptionHelper
    }
    FixupLabel(LoadStoreDone1);                                // LoadStoreDone:
    FixupLabel(LoadStoreDone2);

    if (ProcessorConfig.ARM.BaseRestoredAbortModel && ProcessorConfig.ARM.MemoryBeforeWritebackModel) {
        if (d->W) { // writeback
            Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rn], ESI_Reg);    // MOV Cpu.GPRs[Rn], ESI
        }
    }
    return CodeLocation;
}

unsigned __int8* PlaceSaturate(unsigned __int8* CodeLocation, bool add)
{
    unsigned __int8* SetSaturateFlag;
    unsigned __int8* NoSaturation;
    Emit_JNOLabel(NoSaturation);                    //   JNO NoSaturation      (result isn't saturated, skip)
    Emit_MOV_Reg_Imm32(EAX_Reg, 0x7fffffff);        //   MOV EAX,7FFFFFFF      (positive saturation)
    if (add) {
        Emit_JNCLabel(SetSaturateFlag);             //   JNC SetSaturateFlag   (for adds)
    }
    else {
        Emit_JCLabel(SetSaturateFlag);              //   JC SetSaturateFlag    (for subtracts)
    }
    Emit_INC_Reg(EAX_Reg);                          //   INC EAX               (EAX=0x80000000 - negative saturation)
    FixupLabel(SetSaturateFlag);                    // SetSaturateFlag:
    Emit_OR_BYTEPTR_Imm8(PtrToLong(&Cpu.CPSR)+2,8); //   OR B[(&Cpu.CPSR)+2],8 (set Q flag)    
    FixupLabel(NoSaturation);                       // StoreResult:
    return CodeLocation;
}

unsigned __int8* PlaceQADD(unsigned __int8* CodeLocation, Decoded *d)
{
    bool add=((d->Op1&1)==0),dbl=((d->Op1&2)!=0);
    LogPlace((CodeLocation,"Q%s%s Rd=%d Rm=%d Rn=%d\n",
        dbl ? "D" : "",
        add ? "ADD" : "SUB",
        d->Rd,
        d->Rm,
        d->Rn));

    if (d->Op1!=1) {
        // We optimise this away for QSUB
        Emit_MOV_Reg_DWORDPTR(EAX_Reg,&Cpu.GPRs[d->Rn]);     // MOV EAX,[Rn]
    }
    if (dbl) {
        Emit_ADD_Reg32_Reg32(EAX_Reg,EAX_Reg);               // ADD EAX,EAX  (double)
        CodeLocation=PlaceSaturate(CodeLocation,true);       // Saturate EAX and set Q if necessary
    }
    if (add) {
        Emit_ADD_Reg_DWORDPTR(EAX_Reg,&Cpu.GPRs[d->Rm]);     // ADD EAX,[Rn]
        CodeLocation=PlaceSaturate(CodeLocation,true);       // Saturate EAX and set Q if necessary
    }
    else {
        if (dbl) {
            // We optimise this away for QSUB
            Emit_MOV_Reg_Reg(EBX_Reg,EAX_Reg);               // MOV EBX,EAX
        }
        Emit_MOV_Reg_DWORDPTR(EAX_Reg,&Cpu.GPRs[d->Rm]);     // MOV EAX,[Rm]
        if (dbl) {
            Emit_SUB_Reg32_Reg32(EAX_Reg,EBX_Reg);           // SUB EAX,EBX
        }
        else {
            Emit_SUB_Reg_DWORDPTR(EAX_Reg,&Cpu.GPRs[d->Rn]); // SUB EAX,[Rn]
        }
        CodeLocation=PlaceSaturate(CodeLocation,false);      // Saturate EAX and set Q if necessary
    }
    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd],EAX_Reg);         // MOV [Rd],EAX
    return CodeLocation;
}

// Helper function for PlaceDSPMul
unsigned __int8* PlaceSelect(unsigned __int8* CodeLocation, int xy)
{
    if (xy==1) {
        // Select high word - shift it down to AX
        Emit_SHR_Reg32_Imm(EAX_Reg,16);   // SHR EAX,16
    }
    // The word we want is now in AX. Sign-extend it to EAX
    Emit_CWDE();
    return CodeLocation;
}

unsigned __int8* PlaceDSPMul(unsigned __int8* CodeLocation, Decoded *d)
{
    // Rd and Rn are the other way around for these instructions as they are for the QADD instructions 
    { unsigned __int32 t=d->Rn; d->Rn=d->Rd; d->Rd=t; }

    bool W=(d->Op1==1),accum=((W && d->X==0) || (d->Op1 & 1)==0);

    switch(d->Op1) {
        case 0: // SMLA<x><y>
            LogPlace((CodeLocation,"SMLA%c%c Rd=%d Rm=%d Rs=%d Rn=%d\n",
                d->X ? 'T' : 'B',
                d->Y ? 'T' : 'B',
                d->Rd,
                d->Rm,
                d->Rs,
                d->Rn));
            break;
        case 1: 
            if (d->X==0) { // SMLAW<y>
                LogPlace((CodeLocation,"SMLAW%c Rd=%d Rm=%d Rs=%d Rn=%d\n",
                    d->Y ? 'T' : 'B',
                    d->Rd,
                    d->Rm,
                    d->Rs,
                    d->Rn));
            }
            else { // SMULW<y>
                LogPlace((CodeLocation,"SMULW%c Rd=%d Rm=%d Rs=%d\n",
                    d->Y ? 'T' : 'B',
                    d->Rd,
                    d->Rm,
                    d->Rs));
            }
            break;
        case 2: // SMLAL<x><y>
            LogPlace((CodeLocation,"SMLAL%c%c RdLo=%d RdHi=%d Rm=%d Rs=%d\n",
                d->X ? 'T' : 'B',
                d->Y ? 'T' : 'B',
                d->Rn,
                d->Rd,
                d->Rm,
                d->Rs));
            break;
        case 3: // SMUL<x><y>
            LogPlace((CodeLocation,"SMUL%c%c Rd=%d Rm=%d Rs=%d\n",
                d->X ? 'T' : 'B',
                d->Y ? 'T' : 'B',
                d->Rd,
                d->Rm,
                d->Rs));
            break;
        default:
            ASSERT(false); // We should never get here
    }
    
    if (W) {
        Emit_MOV_Reg_DWORDPTR(EBX_Reg,&Cpu.GPRs[d->Rm]);     //   MOV EBX,[Rm]
    }
    else {
        Emit_MOV_Reg_DWORDPTR(EAX_Reg,&Cpu.GPRs[d->Rm]);     //   MOV EAX,[Rm]
        CodeLocation=PlaceSelect(CodeLocation,d->X);         //   Select high or low word and sign extend
        Emit_MOV_Reg_Reg(EBX_Reg,EAX_Reg);                   //   MOV EBX,EAX
    }
    Emit_MOV_Reg_DWORDPTR(EAX_Reg,&Cpu.GPRs[d->Rs]);         //   MOV EAX,[Rs]
    CodeLocation=PlaceSelect(CodeLocation,d->Y);             //   Select high or low word and sign extend
    Emit_IMUL_Reg32(EBX_Reg);                                //   IMUL EBX
    if (W) {
        Emit_SHL_Reg32_Imm(EDX_Reg,16);                      //   SHL EDX,16
        Emit_SHR_Reg32_Imm(EAX_Reg,16);                      //   SHR EAX,16
        Emit_OR_Reg32_Reg32(EAX_Reg,EDX_Reg);                //   OR EAX,EDX
    }
    if (d->Op1==2) {
        Emit_ADD_DWORDPTR_Reg(&Cpu.GPRs[d->Rn],EAX_Reg);     //   ADD [RdLo],EAX
        Emit_ADC_DWORDPTR_Reg(&Cpu.GPRs[d->Rd],EDX_Reg);     //   ADC [RdHi],EDX
    }
    else {
        if (accum) {
            unsigned __int8* NoSaturation;
            Emit_ADD_Reg_DWORDPTR(EAX_Reg,&Cpu.GPRs[d->Rn]); //   ADD EAX,[Rn]
            Emit_JNOLabel(NoSaturation);                     //   JNO NoSatuation
            Emit_OR_BYTEPTR_Imm8(PtrToLong(&Cpu.CPSR)+2,8);  //   OR B[(&Cpu.CPSR)+2],8 (set Q flag)
            FixupLabel(NoSaturation);                        // NoSaturation:
        }
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd],EAX_Reg);     //   MOV [Rd],EAX
    }
    return CodeLocation;
}

unsigned __int8* PlaceCoprocExtension(unsigned __int8* CodeLocation, Decoded* d)
{
    // (MCRR, MRRC);
    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);    // MOV ECX, GuestAddress
    Emit_CALL(CpuRaiseUndefinedException);            // CALL CpuRaiseUndefinedException
    Emit_RETN(0);                                    // RETN back to C/C++

    return CodeLocation;
}

Decoded *PerformCoprocExtension(Decoded *d)
{
    // (MCRR, MRRC);
    CpuRaiseUndefinedException(d->GuestAddress);
    return NULL;
}

void DisplayRegisterBanner(unsigned __int32 CodeLocation, unsigned __int32 InstructionPointer)
{
#if LOGGING_ENABLED
    LogIf {
        LogPrint((output,"\n"));
        LogPrint((output,"InstructionCount: 0x%8.8x\n",InstructionCount));
        for (int j=0; j<4; ++j) {
            for (int i=0; i<4; ++i) {
                LogPrint((output,"R%-2i=%8.8x ", i+4*j, Cpu.GPRs[i+4*j]));
            }
            LogPrint((output,"\n"));
        }
        LogPrint((output,"CPSR M=%2.2d FIQ=%d IRQ=%d V=%d C=%d Z=%d N=%d    "
                "SPSR M=%2.2d FIQ=%d IRQ=%d V=%d C=%d Z=%d N=%d\n",
                Cpu.CPSR.Bits.Mode, Cpu.CPSR.Bits.FIQDisable, Cpu.CPSR.Bits.IRQDisable,
                Cpu.x86_Overflow.Word, Cpu.x86_Flags.Bits.CarryFlag, 
                Cpu.x86_Flags.Bits.ZeroFlag, Cpu.x86_Flags.Bits.SignFlag,
                Cpu.SPSR.Bits.Mode, Cpu.SPSR.Bits.FIQDisable, Cpu.SPSR.Bits.IRQDisable,
                Cpu.SPSR.Bits.OverflowFlag, Cpu.SPSR.Bits.CarryFlag, Cpu.SPSR.Bits.ZeroFlag, Cpu.SPSR.Bits.NegativeFlag));
        LogPrint((output,"%8.8x(%8.8x) %s ", InstructionPointer, CodeLocation, (Cpu.CPSR.Bits.ThumbMode) ? "Thumb" : "ARM"));
    }
#if GOOD_EMULATOR
        *(unsigned __int32*)pEmulatorGoodData = InstructionPointer;
        memcpy((void*)( (size_t)pEmulatorGoodData + 4), &Cpu, sizeof(Cpu));

        ResetEvent(hEmulatorResumeGood);
        SetEvent(hEmulatorGoodDataReady);
        WaitForSingleObject(hEmulatorResumeGood, INFINITE);
#elif BAD_EMULATOR
        PCPU pCpu = (PCPU)((size_t)pEmulatorGoodData + 4);

        WaitForSingleObject(hEmulatorGoodDataReady, INFINITE);

        if (*(unsigned __int32*)pEmulatorGoodData != InstructionPointer) {
            LogPrint((output, "BAD_EMULATOR: instruction pointer mismatch (good=%x bad=%x)\n",
                *(unsigned __int32*)pEmulatorGoodData,
                InstructionPointer));
            DebugBreak();
        }
        if (memcmp(&Cpu.GPRs[0], &pCpu->GPRs[0], 15*4) != 0) {  // note: R15 is not included in the check
            LogPrint((output, "BAD_EMULATOR: GPRs mismatch - please check register banners\n"));
            DebugBreak();
        }
        
        if (pCpu->CPSR.Partial_Word != Cpu.CPSR.Partial_Word) {
            LogPrint((output, "BAD_EMULATOR: CPSR mismatch - please check register banners\n"));
            DebugBreak();
        }
        
        if (Cpu.SPSR.Word != pCpu->SPSR.Word) {
            LogPrint((output, "BAD_EMULATOR: CPSR_Full mismatch - please check register banners\n"));
            DebugBreak();
        }

        if (Cpu.x86_Flags.Byte != pCpu->x86_Flags.Byte) {
            LogPrint((output, "BAD_EMULATOR: x86_Flags mismatch - please check register banners\n"));
            DebugBreak();
        }

        if (Cpu.x86_Overflow.Byte != pCpu->x86_Overflow.Byte) {
            LogPrint((output, "BAD_EMULATOR: x86_Flags mismatch - please check register banners\n"));
            DebugBreak();
        }
        ResetEvent(hEmulatorGoodDataReady);
        SetEvent(hEmulatorResumeGood);

#endif // BAD_EMULATOR
#endif // LOGGING_ENABLED
}

bool DecodeARMInstruction(Decoded *d, OPCODE Opcode){
    d->R15Modified=0; // assume the instruction doesn't modify R15
    d->Cond = Opcode.Generic.Cond;

    if (d->Cond == 15) {
        // "Never" prefix means an unconditional extension instruction
        if ((Opcode.UnconditionalExtension.opcode1 & 0xd7) == 0x55 &&
            (Opcode.UnconditionalExtension.x2 & 0x0f0) == 0x0f0) {
            // PLD, which is a hint.  We'll implement it as a no-op
            // See the ARM ARM A10-14.
            d->fp = PlaceNop;
            d->Cond=14;
            return true;
        } else {
            // (BLX, STC2, LDC2, CDP2, MCR2, MRC2)
            goto RaiseException;
        }
    }


    // Instruction is to be executed.  Decode it and copy fields into the Decoded struct
    switch (Opcode.Generic.InstructionClass) {
    case 0:  // 000 - DataProcessing or Multiply or SingleDataSwap or Bx or BKPT
        if (Opcode.ControlExtension.Reserved3 == 2 && Opcode.ControlExtension.Reserved2 == 0 &&
            ((Opcode.ControlExtension.Operand2 & 0x10) == 0 || (Opcode.ControlExtension.Operand2 & 0x90) == 0x10)) {
            // ControlExtension
            switch ((Opcode.ControlExtension.Operand2 >> 4) & 0xf) {
            case 0: // "MSR register" or MRS
                d->fp = PlaceMRSorMSR;
                d->S = 1; // PlaceMRSorMSR might call CPSR, thereby Setting the flags.
                d->Rd = Opcode.ControlExtension.Rd;
                d->Rn = Opcode.ControlExtension.Rn; // this is the mask to use, not an actual Rn register number
                d->Rm = Opcode.ControlExtension.Operand2 & 0xf;
                d->Op1 = Opcode.ControlExtension.Op1;
                if (d->Op1 == 0 && d->Rd == R15) {  // MRS R15, ...
                    d->R15Modified = true;
                }
                return true;

            case 1:
                switch (Opcode.ControlExtension.Op1) {
                case 1:    // BX
                    d->fp = PlaceBx;
                    d->Rd = Opcode.BranchExchange.Rd;
                    d->R15Modified = true;
                    return true;

                case 3: // CLX
                    goto RaiseException;

                default:
                    goto RaiseException;
                }
                break;

            case 3:
                if (Opcode.ControlExtension.Op1 == 1) { // BLX
                    goto RaiseException;
                } else {
                    goto RaiseException;
                }
                break;

            case 5:
                if (!(Configuration.ProcessorFeatures & Feature_DSP)) {
                    // Instruction only supported when the feature is turned on
                    goto RaiseException;
                }
                d->Rn=Opcode.DSPExtension.Rn;
                d->Rm=Opcode.DSPExtension.Rm;
                d->Rd=Opcode.DSPExtension.Rd;
                if (d->Rn==R15 || d->Rm==R15 || d->Rd==R15) {
                    // R15 not allowed in any operand
                    goto RaiseException;
                }
                d->Op1=Opcode.DSPExtension.Op1;
                d->fp=PlaceQADD;
                return true;

            case 7:
                if (Opcode.ControlExtension.Op1 == 1) { // BKPT
                    d->fp = PlaceBKPT;
                    return true;
                } else {
                    goto RaiseException;
                }
                break;

            case 8:
            case 0xa:
            case 0xc:
            case 0xe:
                if (!(Configuration.ProcessorFeatures & Feature_DSP)) {
                    // Instruction only supported when the feature is turned on
                    goto RaiseException;
                }
                d->Rn=Opcode.DSPExtension.Rn;
                d->Rm=Opcode.DSPExtension.Rm;
                d->Rd=Opcode.DSPExtension.Rd;
                d->Rs=Opcode.DSPExtension.Rs;
                if (d->Rn==R15 || d->Rm==R15 || d->Rd==R15 || d->Rs==R15) {
                    // R15 not allowed in any operand
                    goto RaiseException;
                }
                d->Op1=Opcode.DSPExtension.Op1;
                d->X=Opcode.DSPExtension.X;
                d->Y=Opcode.DSPExtension.Y;
                d->fp=PlaceDSPMul;
                return true;

            default:
                goto RaiseException;
            }
        }else if (Opcode.ArithmeticExtension.Reserved2==0 && Opcode.ArithmeticExtension.Reserved1==9) {
            d->Rm = Opcode.ArithmeticExtension.Rm;
            d->Rs = Opcode.ArithmeticExtension.Rs;
            d->Rn = Opcode.ArithmeticExtension.Rn;
            d->Rd = Opcode.ArithmeticExtension.Rd;
            d->Op1 = Opcode.ArithmeticExtension.op1;

            if ((  d->Op1 != 0 && (d->Rd == 15 || d->Rn == 15 || d->Rm == 15 || d->Rs == 15))
                || d->Op1 == 0 && (d->Rd == 15 || d->Rn != 0  || d->Rm == 15 || d->Rs == 15))
            {
                goto RaiseException;
            }
            d->fp = PlaceArithmeticExtension;
            d->S = Opcode.ArithmeticExtension.S;
            return true;
        } else if (Opcode.LoadStoreExtension.Reserved1==1 && Opcode.LoadStoreExtension.Reserved2==1 &&
                    !(Opcode.LoadStoreExtension.P==0 && Opcode.LoadStoreExtension.op1==0)) {
            if (Opcode.DoubleLoadStoreExtension.Reserved3==0 && Opcode.DoubleLoadStoreExtension.Reserved2==3) {
                if (!(Configuration.ProcessorFeatures & Feature_LoadStoreDouble)) {
                    // Instruction only supported when the feature is turned on
                    goto RaiseException;
                }
                d->L = !Opcode.DoubleLoadStoreExtension.L;  // 0 for LDRD, 1 for STRD
                d->Rm = Opcode.DoubleLoadStoreExtension.Rm;
                d->Rs = Opcode.DoubleLoadStoreExtension.Rs;
                d->Rd = Opcode.DoubleLoadStoreExtension.Rd;
                if ((d->Rd&1) || d->Rd == R14) {
                    // Only even-numbered destination registers can be used with this instruction
                    // R14/R15 is not allowed
                    goto RaiseException;
                }
                d->Rn = Opcode.DoubleLoadStoreExtension.Rn;
                d->W = Opcode.DoubleLoadStoreExtension.W;
                d->P = Opcode.DoubleLoadStoreExtension.P;
                d->I = Opcode.DoubleLoadStoreExtension.I;
                d->U = Opcode.DoubleLoadStoreExtension.U;

                if (d->I == 0) { //register offset
                    if(d->Rm == 15){
                        goto RaiseException;
                    }
                }
                if (d->P == 0){
                    // Post-index always writes back.  The assembler convention is to
                    // leave W set to 0 in the opcode.  To simplify logic below,
                    // set W when post-indexing is enabled->
                    d->W = 1;
                }
                if (d->W &&    d->Rn == R15){
                    // Write-back to R15 not allowed
                    goto RaiseException;
                }

                d->fp = PlaceDoubleLoadStoreExtension;
                return true;
            }

            d->Op1 = Opcode.LoadStoreExtension.op1;
            d->Rm = Opcode.LoadStoreExtension.Rm;                
            d->Rs = Opcode.LoadStoreExtension.Rs;
            d->Rd = Opcode.LoadStoreExtension.Rd;
            d->Rn = Opcode.LoadStoreExtension.Rn;
            d->W = Opcode.LoadStoreExtension.W;
            d->P = Opcode.LoadStoreExtension.P;
            d->L = Opcode.LoadStoreExtension.L;
            d->Reserved3 = Opcode.HalfWordSignedTransferRegister.Reserved3;
            if(d->Op1 == 0){// SWP/SWPB
                if(d->Rd == 15 || d->Rm == 15 || d->Rn == 15){
                    // ARM ARM 4.1.52 use of R15 as Rd, Rm or Rn is UNPREDICTABLE
                    goto RaiseException;
                }
            } else {// HalfWordSignedTransferRegister or Immediate
                if(d->Reserved3 == 0){ //register offset
                    if(d->Rm == 15){
                        goto RaiseException;
                    }
                }
                if (d->P == 0){
                    // Post-index always writes back.  The assembler convention is to
                    // leave W set to 0 in the opcode.  To simplify logic below,
                    // set W when post-indexing is enabled->
                    d->W = 1;
                }
                if (d->W &&    d->Rn == R15){
                    // Write-back to R15 not allowed
                    goto RaiseException;
                }

                if (d->L && d->Rd == R15) { // load into R15 - BUG!!! d->L not set yet
                    d->R15Modified = true;
                }
            }

            d->fp = PlaceLoadStoreExtension;
            d->B = Opcode.LoadStoreExtension.B;
            d->U = Opcode.LoadStoreExtension.U;
            // performloadstoreextension also uses the halfwords
            d->Offset = (Opcode.HalfWordSignedTransferImmediate.OffsetHigh << 4) |
                            Opcode.HalfWordSignedTransferImmediate.OffsetLow;
            d->H = Opcode.HalfWordSignedTransferRegister.H;
            d->S = Opcode.HalfWordSignedTransferRegister.S;
            return true;
        } else {
DataProcessingCode:
            d->fp = PlaceDataProcessing;
            d->Operand2 = Opcode.DataProcessing.Operand2;
            d->S = Opcode.DataProcessing.S;
            d->Opcode = Opcode.DataProcessing.Opcode;
            d->I = Opcode.DataProcessing.I;
            if (d->I) {
                d->Reserved3 = _rotr((unsigned __int8)d->Operand2, (d->Operand2 >> 8) << 1);
            }
            switch (d->Opcode) {
                case 0: // 0000 = AND - Rd = Rn AND Immediate
                case 1: // 0001 = EOR - Rd = Rn EOR Immediate
                case 2: // 0010 = SUB - Rd = Rn - Immediate
                case 3: // 0011 = RSB - Rd = Immediate - Rn
                case 4: // 0100 = ADD - Rd = Rn + Immediate
                case 5: // 0101 = ADC - Rd = Rn + Immediate + C
                case 6: // 0110 = SBC - Rd = Rn - Immediate + C - 1
                case 7: // 0111 = RSC - Rd = Immediate - Rn + C - 1
                case 12: // 1100 = ORR - Rd = Rn OR Immediate
                case 14: // 1110 = BIC = Rd = Rn AND NOT Immediate
                    d->Rn = Opcode.DataProcessing.Rn;
                    d->Rd = Opcode.DataProcessing.Rd;
                    if (d->Rd == R15) {
                        d->R15Modified=true;
                    }
                    break;

                case 8: // 1000 = TST - set condition codes on Rn AND Immediate
                case 9: // 1001 = TEQ - set condition codes on Rn EOR Immediate
                case 10: // 1010 = CMP - set condition codes on Rn - Immediate
                case 11: // 1011 = CMN - set condition codes on Rn + Immediate
                    d->Rn = Opcode.DataProcessing.Rn;
                    d->Rd = 0; // The UPDATE_FLAGS_ARITHMETIC_SUB macro consumes Rd.  We just have to
                                // ensure that it is not R15 here.
                    break;

                case 13: // 1101 = MOV = Rd = Immediate
                case 15: // 1111 = MVN = Rd = NOT Immediate
                    d->Rd = Opcode.DataProcessing.Rd;
                    if (d->Rd == R15) {
                        d->R15Modified=true;
                    }
                    d->Rm = d->Operand2 & 0xf;
                    break;

                default:
                    ASSERT(FALSE);
                    break;
            }
            return true;
        }
    case 1:  // 001 - DataProcessing
        if (Opcode.ControlExtension.Reserved3 == 6 && Opcode.ControlExtension.Reserved2==0) {
            // Control extension with Opcode[25]==1.  There is only one opcode
            // in this space - "MSR (immediate form)"
            if ((Opcode.ControlExtension.Op1 & 1) == 1) {
                // MSR (immediate form)
                d->fp=PlaceMSRImmediate;
                d->S = 1;    // PlaceMSRImmediate might call UpdateCPSR, thereby Setting the flags
                d->Op1 = Opcode.ControlExtension.Op1;
                d->Rn = Opcode.ControlExtension.Rn; // this is the mask used when updating, not actually an Rn register number
                d->Immediate = _rotr((unsigned __int8)Opcode.ControlExtension.Operand2,(Opcode.ControlExtension.Operand2 >> 8) << 1);
                return true;
            } else {
                goto RaiseException;
            }
        } else {                
            goto DataProcessingCode;
        }
    case 2: {  // 010 - SingleDataTransfer
SingleDataTransferCode:                
            d->Offset = Opcode.SingleDataTransfer.Offset;
            d->I = Opcode.SingleDataTransfer.I;
            d->Rn = Opcode.SingleDataTransfer.Rn;
            d->W = Opcode.SingleDataTransfer.W;
            d->P = Opcode.SingleDataTransfer.P;
            d->U = Opcode.SingleDataTransfer.U;
            d->fp = PlaceSingleDataTransfer;
            d->Rd = Opcode.SingleDataTransfer.Rd;
            d->L = Opcode.SingleDataTransfer.L;
            d->B = Opcode.SingleDataTransfer.B;
            d->Operand2 = Opcode.DataProcessing.Operand2;
            if(d->P==0){
                // Post-index always writes back.  The assembler convention is to
                // leave W set to 0 in the opcode.  To simplify logic below,
                // set W when post-indexing is enabled.
                d->W=1;
            }
            if (d->W && d->Rn == R15){
                // ARM ARM 5.5.4: Specifying Rn==R15 has UNPREDICTABLE results with writeback enabled
                goto RaiseException;
            }
            if (d->Rd == R15 && d->L) { // load into R15
                d->R15Modified=true;
            }
            if (d->Rn == R15 && d->I == 0) {
                // Pre-compute R15+/-Offset and cache it in Reserved3.
                if (d->U) {
                    d->Reserved3 = d->GuestAddress + d->Offset + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8);
                } else {
                    d->Reserved3 = d->GuestAddress - d->Offset + ((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8);
                }
            }
            if (d->I && (d->Offset & 0xf) == R15) {  // Rm cannot be R15
                goto RaiseException;
            }
            return true;
        }
    case 3:  // 011 - SingleDataTransfer or Undefined
        // Undefined shall not be used by software.  Disambiguation is done
        // by coprocessors.  If all refuse to accept it as Undefined, then
        // it is treated as a SingleDataTranfer.
        if (Opcode.UndefinedExtension.Reserved1 == 1)
            goto RaiseException;
        else
            goto SingleDataTransferCode;
    case 4:{  // 100 - BlockDataTransfer
            d->Rn = Opcode.BlockDataTransfer.Rn;
            if(d->Rn == R15){
                // ARM ARM 5.5.4: Specifying Rn==R15 has UNPREDICTABLE results with writeback enabled
                goto RaiseException;
            }
            d->fp = PlaceBlockDataTransfer;
            d->RegisterList = Opcode.BlockDataTransfer.RegisterList;                
            d->L = Opcode.BlockDataTransfer.L;
            d->W = Opcode.BlockDataTransfer.W;
            d->S = Opcode.BlockDataTransfer.S;
            d->U = Opcode.BlockDataTransfer.U;
            d->P = Opcode.BlockDataTransfer.P;
            if ((d->RegisterList & (1 << R15)) && d->L) { // LDM including R15
                d->R15Modified = true;
            }
            return true;
            }
    case 5:{  // 101 - Branch
            d->fp = PlaceBranch;
            d->Offset = d->GuestAddress+8+4*Opcode.Branch.Offset; // account for the 2-word instruction prefetch
            d->L = Opcode.Branch.L;
            d->R15Modified = true;
            return true;
            }
    case 6:  // 110 - CoprocDataTransfer
        if (Opcode.CoprocessorExtension.Reserved2 == 24 && Opcode.CoprocessorExtension.Reserved1==0) {
            d->fp = PlaceCoprocExtension;
            d->Offset = Opcode.CoprocessorExtension.Offset;
            d->CPNum = Opcode.CoprocessorExtension.cp_num;
            d->CRd = Opcode.CoprocessorExtension.CRd;
            d->Rn = Opcode.CoprocessorExtension.Rn;
            d->X1 = Opcode.CoprocessorExtension.x1;
            d->X2 = Opcode.CoprocessorExtension.x2;
            return true;
        } else {
            d->W = Opcode.CoprocDataTransfer.W;
            d->Rn = Opcode.CoprocDataTransfer.Rn;
            d->P = Opcode.CoprocDataTransfer.P;
            if(!d->P){ // not pre-index
                // Post index always sets writeback
                d->W = 1;
            }
            if (d->W && d->Rn == R15){
                // ARM ARM 5.5.4: Specifying Rn==R15 has UNPREDICTABLE results with writeback enabled
                goto RaiseException;
            }
            d->Offset = ((unsigned __int32)Opcode.CoprocDataTransfer.Offset)<<2;
            if(!d->U)
                d->Offset = -d->Offset;

            d->fp = PlaceCoprocDataTransfer;
            d->CPNum = Opcode.CoprocDataTransfer.CPNum;
            d->CRd = Opcode.CoprocDataTransfer.CRd;
            d->L = Opcode.CoprocDataTransfer.L;
            d->N = Opcode.CoprocDataTransfer.N;
            d->U = Opcode.CoprocDataTransfer.U;
            return true;
        }
    case 7:  // 111 - CoprocDataOperation or CoprocRegisterTransfer or SoftwareInterrupt
        if (Opcode.SoftwareInterrupt.Reserved1 == 15) {
            if (ProcessorConfig.ARM.GenerateSyscalls && Opcode.SoftwareInterrupt.Ignored == 0x123456) {
                d->fp = PlaceSyscall;
            } else {
                d->fp = PlaceSoftwareInterrupt;
                d->R15Modified = true;
            }
            return true;
        } else if (Opcode.CoprocDataOperation.Reserved1 == 0) {
            d->fp = PlaceCoprocDataOperation;
            d->CRm = Opcode.CoprocDataOperation.CRm;
            d->CP = Opcode.CoprocDataOperation.CP;
            d->CPNum = Opcode.CoprocDataOperation.CPNum;
            d->CRd = Opcode.CoprocDataOperation.CRd;
            d->CRn = Opcode.CoprocDataOperation.CRn;
            d->CPOpc = Opcode.CoprocDataOperation.CPOpc;
            return true;
        } else {
            d->fp = PlaceCoprocRegisterTransfer;
            d->CRm = Opcode.CoprocRegisterTransfer.CRm;
            d->CP = Opcode.CoprocRegisterTransfer.CP;
            d->CPNum = Opcode.CoprocRegisterTransfer.CPNum;
            d->Rd = Opcode.CoprocRegisterTransfer.Rd;
            d->CRn = Opcode.CoprocRegisterTransfer.CRn;
            d->L = Opcode.CoprocRegisterTransfer.L;
            d->CPOpc = Opcode.CoprocRegisterTransfer.CPOpc;
            return true;
        }
    default:
        ASSERT(FALSE);
        break;
    }
RaiseException:
    d->Cond = 14;
    d->fp = PlaceRaiseUndefinedException;
    return false;
}

unsigned __int8* PlaceThumbBranchAndExchange(unsigned __int8* CodeLocation, Decoded* d)
{
    int Rd;

    // Like PlaceBx, this routine calls PlaceR15Modifier twice, allowing it to cache
    // both Thumb and ARM destinations concurrently.

    LogPlace((CodeLocation,"Bx Rd=%d\n", d->RsHs + 8*d->H2));
    Rd = d->RsHs + 8*d->H2;
    if (Rd == R15) {
        // "BX R15" can be special-cased:  it always switches to ARM mode.
        Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress+4);                    // MOV EAX, GuestAddress+Thumb instruction prefetch
        Emit8(0x81); EmitModRmReg(0,5,4); EmitPtr(&Cpu.CPSR); Emit32(~0x20u); // AND Cpu.CPSR, ~0x20 - CPU.CPSR.Bits.ThumbMode = 0
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R15], EAX_Reg);                    // MOV Cpu.GPRs[R15], EAX
    } else {
        unsigned __int8* RemainInThumb;
        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->RsHs + 8*d->H2]);    // MOV EAX, Cpu.GPRs[...]
        Emit_TEST_Reg_Imm32(EAX_Reg, 1);                                // TEST EAX, 1
        Emit_JNZLabel(RemainInThumb);                                    // JNZ RemainInThumb
        // else switch to ARM mode
        Emit8(0x81); EmitModRmReg(0,5,4); EmitPtr(&Cpu.CPSR); Emit32(~0x20u); // AND Cpu.CPSR, ~0x20 - CPU.CPSR.Bits.ThumbMode = 0
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R15], EAX_Reg);                    // MOV Cpu.GPRs[R15], EAX
        CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);

        FixupLabel(RemainInThumb);                                        // RemainInThumb:
        Emit_AND_Reg_Imm32(EAX_Reg, 0xfffffffe);                        // AND EAX, 0xfffffffe
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R15], EAX_Reg);                    // MOV Cpu.GPRs[R15], EAX
    }
    CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);

    return CodeLocation;
}

unsigned __int8* PlaceThumbLoadAddressPC(unsigned __int8* CodeLocation, Decoded* d)
{
    unsigned __int32 Address;

    LogPlace((CodeLocation,"ADD Rd=%d, PC, #%d\n", d->Rd, (unsigned __int32)(d->Word8) << 2));
    Address = (d->GuestAddress+4) & 0xfffffffc;
    Address += (unsigned __int32)(d->Word8) << 2;
    Emit_MOV_Reg_Imm32(EAX_Reg, Address);            // MOV EAX, Address
    Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);    // MOV Cpu.GPRs[Rd], EAX

    return CodeLocation;
}

unsigned __int8* PlaceThumbLongBranch(unsigned __int8* CodeLocation, Decoded* d)
{
    switch (d->HTwoBits) {
    case 0:
        // Unconditional Branch
        LogPlace((CodeLocation,"B 0x%8.8x\n", d->GuestAddress+4+d->Offset));
        Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress+4+d->Offset);    // MOV EAX, destination
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R15], EAX_Reg);                // MOV Cpu.GPRs[R15], EAX
        CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
        break;

    case 2:
        LogPlace((CodeLocation,"BL high half\n"));
        Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress+4+d->Offset);    // MOV EAX, destination
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R14], EAX_Reg);                // MOV Cpu.GPRs[R14], EAX
        break;

    case 1:
        LogPlace((CodeLocation, "BL low half\n"));
        Emit8(0x81); EmitModRmReg(0,5,4); EmitPtr(&Cpu.CPSR); Emit32(~0x20u); // AND Cpu.CPSR, ~0x20 - CPU.CPSR.Bits.ThumbMode = 0
        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[R14]);                // MOV EAX, Cpu.GPRs[R14]
        Emit_ADD_Reg_Imm32(EAX_Reg, d->Offset);                        // ADD EAX, d->Offset
        Emit_AND_Reg_Imm32(EAX_Reg, 0xfffffffc);                    // AND EAX, 0xfffffffe
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R15], EAX_Reg);                // MOV Cpu.GPRs[R15], EAX
        Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress+3);                // one opcode ahead, plus 1 to indicate a Thumb address
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R14], EAX_Reg)                // MOV Cpu.GPRs[R14], EAX
        CodeLocation = PlacePushShadowStack(CodeLocation);
        CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
        break;

    case 3:
        LogPlace((CodeLocation,"BL low half\n"));
        Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[R14]);                // MOV EAX, Cpu.GPRS[R14]
        Emit_ADD_Reg_Imm32(EAX_Reg, d->Offset);                        // ADD EAX, d->Offset
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R15], EAX_Reg);                // MOV Cpu.GPRs[R15], EAX
        Emit_MOV_Reg_Imm32(EAX_Reg, d->GuestAddress+3);                // one opcode ahead, plus 1 to indicate a Thumb address
        Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[R14], EAX_Reg)                // MOV Cpu.GPRs[R14], EAX
        CodeLocation = PlacePushShadowStack(CodeLocation);
        CodeLocation = PlaceR15ModifiedHelper(CodeLocation, d);
        break;

    default:
        ASSERT(FALSE);
        break;
    }

    return CodeLocation;
}

PVOID __fastcall HardwareBreakpointHelper(unsigned __int32 GuestAddress)
{
    EnterCriticalSection(&InterruptLock);
    Cpu.DebuggerInterruptPending |= DEBUGGER_INTERRUPT_BREAKPOINT;
    LeaveCriticalSection(&InterruptLock);

    return DebugEntry(GuestAddress, etDebuggerInterrupt);
}

unsigned __int8* PlaceHardwareBreakpoint(unsigned __int8* CodeLocation, Decoded* d)
{
    LogPlace((CodeLocation, "Hardware breakpoint\n"));
    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);       // ECX = d->GuestAddress
    Emit_CALL(HardwareBreakpointHelper);                // CALL HardwareBreakpointHelper(d->GuestAddress)
    Emit_RETN(0);                                       // Return to CpuSimulate(), assuming R15 was modified

    return CodeLocation;
}

__declspec(naked) void SingleStepHelper(void)
{
    __asm {
        call SingleStepEntry
        // See the InterruptCheck for for documentation on this code:
        cmp eax, 1
        jb ReturnToCpuSimulate
        jz ReturnToTranslatedCode
        pop eax
        jmp eax
ReturnToCpuSimulate:
        pop eax
ReturnToTranslatedCode:
        retn
    }
}

unsigned __int8* PlaceSingleStepCheck(unsigned __int8* CodeLocation, Decoded *d)
{
    Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);       // ECX = d->GuestAddress
    Emit_CALL(SingleStepHelper);                        // CALL SingleStepHelper(d->GuestAddress)

    return CodeLocation;
}

void DecodeThumbMoveAddSub(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    ArmOpcode.DataProcessing.Cond = 0xe;
    ArmOpcode.DataProcessing.Reserved1 = 0;
    ArmOpcode.DataProcessing.S = 1; // update CPSR flags

    switch (Opcode.MoveShiftedRegister.Op2) {
    case 0: // THUMB: LSL Rd,Rs,#Offset5     ARM:  MOVS Rd,Rs,LSL#Offset5
    case 1: // THUMB: LSR Rd,Rs,#Offset5     ARM: MOVS Rd,Rs,LSR#Offset5
    case 2: // THUMB: ASR Rd,Rs,#Offset5     ARM: MOVS Rd,Rs,ASR#Offset5
        ArmOpcode.DataProcessing.Rd = Opcode.MoveShiftedRegister.Rd;
        ArmOpcode.DataProcessing.I = 0;
        ArmOpcode.DataProcessing.Operand2 = (Opcode.MoveShiftedRegister.Offset5 << 7) |
                                            (Opcode.MoveShiftedRegister.Op2 << 5) |
                                            Opcode.MoveShiftedRegister.Rs; // shift Rs left/right/asr by 5-bit unsigned integer
        ArmOpcode.DataProcessing.Rn = 0; // ignored
        ArmOpcode.DataProcessing.Opcode = 13; // MOV Rd:=Op2
        break;

    case 3: // add/subtract
        ArmOpcode.DataProcessing.Opcode = (Opcode.AddSubtract.Op == 0) ? 4 : 2; // ADD ? SUB
        ArmOpcode.DataProcessing.Rn = Opcode.AddSubtract.Rs;
        ArmOpcode.DataProcessing.Rd = Opcode.AddSubtract.Rd;
        ArmOpcode.DataProcessing.I = Opcode.AddSubtract.I;
        ArmOpcode.DataProcessing.Operand2 = Opcode.AddSubtract.RnOffset;
        break;

    default:
        ASSERT(FALSE);
        break;
    }
    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceDataProcessing);
}

const unsigned __int8 MathImmediateToARM[] = {
    13, // MOV
    10, // CMP
    4,  // ADD
    2   // SUB
};


void DecodeThumbMathImmediate(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    ArmOpcode.DataProcessing.Cond = 0xe;
    ArmOpcode.DataProcessing.Reserved1 = 0;
    ArmOpcode.DataProcessing.Opcode = MathImmediateToARM[Opcode.MathImmediate.Op];
    ArmOpcode.DataProcessing.S = 1; // update CPSR flags
    ArmOpcode.DataProcessing.Rn = Opcode.MathImmediate.Rd;  // Op1 is Rd
    ArmOpcode.DataProcessing.Rd = Opcode.MathImmediate.Rd;
    // Rotate immediate value by zero
    ArmOpcode.DataProcessing.I = 1;
    ArmOpcode.DataProcessing.Operand2 = Opcode.MathImmediate.Offset8;

    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceDataProcessing);
}

const unsigned __int8 ALUOperationToARM[16] = {
    // Thumb opcode = ARM opcode
    0,  // AND=AND
    1,  // EOR=EOR
    13, // LSL=MOV
    13, // LSR=MOV
    13, // ASR=MOV
    5,  // ADC=ADC
    6,  // SBC=ADC
    13, // ROR=MOV
    8,  // TST=TST
    3,  // NEG=RSB
    10, // CMP=CMP
    11, // CMN=CMN
    12, // ORR=ORR
    0xff, // MUL=no equivalent
    14, // BIC=BIC
    15, // NVM=NVM
};

const unsigned __int8 ALUOperationToARMShift[] = {
    0xff,
    0xff,
    0, // LSL
    1, // LSR
    2, // ASR
    0xff,
    0xff,
    3, // ROR
};
void DecodeThumbALUOperation(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    if (Opcode.ALUOperation.Op == 13) {
        // THUMB:  MUL Rd, Rs    ARM: MULS Rd, Rs, Rd
        ArmOpcode.ArithmeticExtension.Cond = 0xe;
        ArmOpcode.ArithmeticExtension.Reserved2 = 0;
        ArmOpcode.ArithmeticExtension.op1=0; // MUL
        ArmOpcode.ArithmeticExtension.S = 1;
        ArmOpcode.ArithmeticExtension.Rd = Opcode.ALUOperation.Rd;
        ArmOpcode.ArithmeticExtension.Rs = Opcode.ALUOperation.Rs;
        ArmOpcode.ArithmeticExtension.Rn = 0;
        ArmOpcode.ArithmeticExtension.Reserved1 = 9;
        ArmOpcode.ArithmeticExtension.Rm = Opcode.ALUOperation.Rd;
        DecodeARMInstruction(d, ArmOpcode);
        ASSERT(d->fp == PlaceArithmeticExtension);
    } else {
        ArmOpcode.DataProcessing.Cond = 0xe;
        ArmOpcode.DataProcessing.Reserved1 = 0;
        ArmOpcode.DataProcessing.Opcode = ALUOperationToARM[Opcode.ALUOperation.Op];
        ArmOpcode.DataProcessing.S = 1;
        ArmOpcode.DataProcessing.Rd = Opcode.ALUOperation.Rd;
        if (ArmOpcode.DataProcessing.Opcode == 13) {
            // MOV
            ArmOpcode.DataProcessing.I=0;
            ArmOpcode.DataProcessing.Rn=0;  // Operand1, Rn, is ignored for MOV instructions
            // Operand2 is Rd {LSL,LSR,ASR} Rs
            ASSERT(Opcode.ALUOperation.Op < sizeof(ALUOperationToARMShift));
            ArmOpcode.DataProcessing.Operand2 = (Opcode.ALUOperation.Rs << 8) |
                                                (ALUOperationToARMShift[Opcode.ALUOperation.Op] << 5) |
                                                0x10 |
                                                Opcode.ALUOperation.Rd;
        } else if (ArmOpcode.DataProcessing.Opcode == 3) {
            // Op1 is Rs
            // Op2 is #0
            ArmOpcode.DataProcessing.I=1;
            ArmOpcode.DataProcessing.Rn = Opcode.ALUOperation.Rs;
            ArmOpcode.DataProcessing.Operand2=0;  // 0 rotated 0
        } else {
            // Op1 is Rd
            // Op2 is Rs << 0
            ArmOpcode.DataProcessing.I=0;
            ArmOpcode.DataProcessing.Rn = Opcode.ALUOperation.Rd;
            ArmOpcode.DataProcessing.Operand2=Opcode.ALUOperation.Rs; // Rs << 0
        }
        DecodeARMInstruction(d, ArmOpcode);
        ASSERT(d->fp == PlaceDataProcessing);
    }
}

const unsigned __int8 HiOpsToArm[] = {
    4,  // ADD
    10, // CMP
    13, // MOV
};

void DecodeThumbHiOps(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    if (Opcode.HiOps.Op == 3) { // Branch and exchange (Bx)
        d->RsHs = Opcode.HiOps.RsHs;
        d->H2 =    Opcode.HiOps.H2;
        d->R15Modified = true;
        d->fp = PlaceThumbBranchAndExchange;
    } else {
        // ADD, SUB, CMP, where H1 and H2 control whether the register number
        // is 0-7 or 8-15.
        ArmOpcode.DataProcessing.Cond = 0xe;
        ArmOpcode.DataProcessing.Reserved1 = 0;
        ArmOpcode.DataProcessing.I = 0;
        ArmOpcode.DataProcessing.Opcode = HiOpsToArm[Opcode.HiOps.Op];
        ArmOpcode.DataProcessing.Rn = Opcode.HiOps.RdHd + 8*Opcode.HiOps.H1;
        if (ArmOpcode.DataProcessing.Opcode == 10) {    // CMP is special-cased:
            ArmOpcode.DataProcessing.Rd = 0;            // Rd is set to 0
            ArmOpcode.DataProcessing.S = 1;             // Flags are updated
        } else {
            ArmOpcode.DataProcessing.Rd = ArmOpcode.DataProcessing.Rn;
            ArmOpcode.DataProcessing.S = 0;
        }
        ArmOpcode.DataProcessing.Operand2 = Opcode.HiOps.RsHs + 8*Opcode.HiOps.H2;
        DecodeARMInstruction(d, ArmOpcode);
        ASSERT(d->fp == PlaceDataProcessing);
    }
}

void DecodeThumbPCRelativeLoad(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    ArmOpcode.SingleDataTransfer.Cond = 0xe;
    ArmOpcode.SingleDataTransfer.Reserved1=1; // this was 2. needs to be 1.
    ArmOpcode.SingleDataTransfer.I=0; // Offset is an immediate value
    ArmOpcode.SingleDataTransfer.P=1; // pre index
    ArmOpcode.SingleDataTransfer.U=1; // up; add offset to base
    ArmOpcode.SingleDataTransfer.B=0; // word, not byte
    ArmOpcode.SingleDataTransfer.W=0; // no writeback
    ArmOpcode.SingleDataTransfer.L=1; // load, not store
    ArmOpcode.SingleDataTransfer.Rn = R15;
    ArmOpcode.SingleDataTransfer.Rd = Opcode.PCRelativeLoad.Rd;
    ArmOpcode.SingleDataTransfer.Offset = Opcode.PCRelativeLoad.Word8 << 2;
    if ((d->GuestAddress+4) & 2) {
        // Force bit 2 clear in the PC by adjusting the offset a little
        ArmOpcode.SingleDataTransfer.Offset -= 2;
    }
    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceSingleDataTransfer);
}

void DecodeThumbLoadStoreRegisterOffset(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    ArmOpcode.SingleDataTransfer.Cond=0xe;
    ArmOpcode.SingleDataTransfer.Reserved1=1;
    ArmOpcode.SingleDataTransfer.I=1; // offset is a register
    ArmOpcode.SingleDataTransfer.P=1; // pre index
    ArmOpcode.SingleDataTransfer.U=1; // up; add offset to base
    ArmOpcode.SingleDataTransfer.B=Opcode.LoadStoreRegisterOffset.B; // transfer byte or word
    ArmOpcode.SingleDataTransfer.W=0; // no writeback
    ArmOpcode.SingleDataTransfer.L=Opcode.LoadStoreRegisterOffset.L; // load or store
    ArmOpcode.SingleDataTransfer.Rn=Opcode.LoadStoreRegisterOffset.Rb; // base register
    ArmOpcode.SingleDataTransfer.Rd=Opcode.LoadStoreRegisterOffset.Rd; // destintation register
    ArmOpcode.SingleDataTransfer.Offset=Opcode.LoadStoreRegisterOffset.Ro; // offset register with no shift

    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceSingleDataTransfer);
}

void DecodeThumbLoadStoreByteHalfWord(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;
    
    ArmOpcode.HalfWordSignedTransferRegister.Cond=0xe;
    ArmOpcode.HalfWordSignedTransferRegister.Reserved4=0;
    ArmOpcode.HalfWordSignedTransferRegister.P=1; // pre-index
    ArmOpcode.HalfWordSignedTransferRegister.U=1; // up
    ArmOpcode.HalfWordSignedTransferRegister.Reserved3=0;
    ArmOpcode.HalfWordSignedTransferRegister.W=0; // no write-back

    ArmOpcode.HalfWordSignedTransferRegister.H=Opcode.LoadStoreByteHalfWord.H;
    ArmOpcode.HalfWordSignedTransferRegister.S=Opcode.LoadStoreByteHalfWord.S;
    if (ArmOpcode.HalfWordSignedTransferRegister.H == 0 &&
        ArmOpcode.HalfWordSignedTransferRegister.S == 0) {
        // STRH is encoded for thumb with H and S both 0.  ARM encodes it differently.    
        ArmOpcode.HalfWordSignedTransferRegister.L=0;
        ArmOpcode.HalfWordSignedTransferRegister.H=1;
    } else {
        ArmOpcode.HalfWordSignedTransferRegister.L=1;
    }

    ArmOpcode.HalfWordSignedTransferRegister.Rn=Opcode.LoadStoreByteHalfWord.Rb;
    ArmOpcode.HalfWordSignedTransferRegister.Rd=Opcode.LoadStoreByteHalfWord.Rd;
    ArmOpcode.HalfWordSignedTransferRegister.Reserved2=1;
    ArmOpcode.HalfWordSignedTransferRegister.Reserved1=1;
    ArmOpcode.HalfWordSignedTransferRegister.Rm=Opcode.LoadStoreByteHalfWord.Ro;

    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceLoadStoreExtension);
}

void DecodeThumbCase2(Decoded *d, THUMB_OPCODE Opcode)
{
    switch (Opcode.ALUOperation.Reserved) {
    case 0: // ALU operation
        DecodeThumbALUOperation(d, Opcode);
        break;

    case 1: // Hi register operations / branch exchange
        DecodeThumbHiOps(d, Opcode);
        break;

    case 2: // PC-relative load
    case 3: // PC-relative load
        DecodeThumbPCRelativeLoad(d, Opcode);
        break;

    default: // load/store of some sort
        if (Opcode.LoadStoreRegisterOffset.Reserved1 == 0) {
            // load/store with register offset
            DecodeThumbLoadStoreRegisterOffset(d, Opcode);
        } else {
            // load/store sign-extended byte/halfword
            DecodeThumbLoadStoreByteHalfWord(d, Opcode);
        }
        break;
    }
}

void DecodeThumbLoadStoreImmediateOffset(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    ArmOpcode.SingleDataTransfer.Cond=0xe;
    ArmOpcode.SingleDataTransfer.Reserved1=1;
    ArmOpcode.SingleDataTransfer.I=0; // offset is an immediate
    ArmOpcode.SingleDataTransfer.P=1; // pre index
    ArmOpcode.SingleDataTransfer.U=1; // up; add offset to base
    ArmOpcode.SingleDataTransfer.B=Opcode.LoadStoreImmediateOffset.B; // transfer byte or word
    ArmOpcode.SingleDataTransfer.W=0; // no writeback
    ArmOpcode.SingleDataTransfer.L=Opcode.LoadStoreImmediateOffset.L; // load or store
    ArmOpcode.SingleDataTransfer.Rn=Opcode.LoadStoreImmediateOffset.Rb; // base register
    ArmOpcode.SingleDataTransfer.Rd=Opcode.LoadStoreImmediateOffset.Rd; // destination register
    ArmOpcode.SingleDataTransfer.Offset=(Opcode.LoadStoreImmediateOffset.B) ?
                                          Opcode.LoadStoreImmediateOffset.Offset5 : // byte accesses use the offset unmodified
                                          Opcode.LoadStoreImmediateOffset.Offset5 << 2; // word accesses multiply the offset by 4
    
    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceSingleDataTransfer);
}

void DecodeThumbLoadStoreHalfWord(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    ArmOpcode.HalfWordSignedTransferImmediate.Cond=0xe;
    ArmOpcode.HalfWordSignedTransferImmediate.Reserved4=0;
    ArmOpcode.HalfWordSignedTransferImmediate.P=1; // pre-increment
    ArmOpcode.HalfWordSignedTransferImmediate.U=1; // up
    ArmOpcode.HalfWordSignedTransferImmediate.Reserved3=1;
    ArmOpcode.HalfWordSignedTransferImmediate.W=0; // no write-back
    ArmOpcode.HalfWordSignedTransferImmediate.L=Opcode.LoadStoreHalfWord.L;
    ArmOpcode.HalfWordSignedTransferImmediate.Rn=Opcode.LoadStoreHalfWord.Rb;
    ArmOpcode.HalfWordSignedTransferImmediate.Rd=Opcode.LoadStoreHalfWord.Rd;
    ArmOpcode.HalfWordSignedTransferImmediate.OffsetHigh=Opcode.LoadStoreHalfWord.Offset5 >> 3;
    ArmOpcode.HalfWordSignedTransferImmediate.Reserved2=1;
    ArmOpcode.HalfWordSignedTransferImmediate.S=0; // unsigned
    ArmOpcode.HalfWordSignedTransferImmediate.H=1; // halfword
    ArmOpcode.HalfWordSignedTransferImmediate.Reserved1=1;
    ArmOpcode.HalfWordSignedTransferImmediate.OffsetLow=(Opcode.LoadStoreHalfWord.Offset5 & 0x7) << 1;

    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceLoadStoreExtension);
}

void DecodeThumbLoadStoreSPRelative(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    ArmOpcode.SingleDataTransfer.Cond=0xe;
    ArmOpcode.SingleDataTransfer.Reserved1=1;
    ArmOpcode.SingleDataTransfer.I=0; // offset is an immediate
    ArmOpcode.SingleDataTransfer.P=1; // pre index
    ArmOpcode.SingleDataTransfer.U=1; // up; add offset to base
    ArmOpcode.SingleDataTransfer.B=0; // transfer word
    ArmOpcode.SingleDataTransfer.W=0; // no writeback
    ArmOpcode.SingleDataTransfer.L=Opcode.LoadStoreSPRelative.L; // load or store
    ArmOpcode.SingleDataTransfer.Rn=R13; // R13 is the stack pointer
    ArmOpcode.SingleDataTransfer.Rd=Opcode.LoadStoreSPRelative.Rd; // destintation register
    ArmOpcode.SingleDataTransfer.Offset=Opcode.LoadStoreSPRelative.Word8 << 2; // immediate

    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceSingleDataTransfer);
}



void DecodeThumbLoadAddress(Decoded *d, THUMB_OPCODE Opcode)
{
    // The 'PC' version of this instruction does not have an exact ARM equivalent:
    //        ADD Rd, PC, #<immed8>*4
    // as it operates as:
    //      Rd = (PC & 0xfffffffc) + (immed_8 << 2)
    // However, the 'SP' version does have an exact match.

    if (Opcode.LoadAddress.SP) {
        OPCODE ArmOpcode;

        ArmOpcode.DataProcessing.Cond=0xe;
        ArmOpcode.DataProcessing.Reserved1=0;
        ArmOpcode.DataProcessing.I=1; // op2 is rotated immediate
        ArmOpcode.DataProcessing.Opcode=4; // add
        ArmOpcode.DataProcessing.S=0; // do not set condition codes
        ArmOpcode.DataProcessing.Rn=R13;
        ArmOpcode.DataProcessing.Rd=Opcode.LoadAddress.Rd;
        if ((Opcode.LoadAddress.Word8 & 0xc0)) {
            // One or more of the top two bits are set.  If the value is
            // shifted left by 2, they'll overflow the 8-bit immediate
            // value allowed by the ARM data processing opcode.  Instead,
            // encode the value via a rotation.  Rotate the value right by
            // 30 bits to accomplish the shift of 2.
            ArmOpcode.DataProcessing.Operand2 = 0xf00 | Opcode.LoadAddress.Word8;
        } else {
            ArmOpcode.DataProcessing.Operand2=Opcode.LoadAddress.Word8 << 2;
            ASSERT((ArmOpcode.DataProcessing.Operand2 & 0xf00) == 0); // ensure no rotate is accidentally encoded
        }
        DecodeARMInstruction(d, ArmOpcode);
        ASSERT(d->fp == PlaceDataProcessing);
    } else {
        d->Rd = Opcode.LoadAddress.Rd;
        d->Word8 = Opcode.LoadAddress.Word8;
        d->fp = PlaceThumbLoadAddressPC;
    }
}

void DecodeThumbAddToStackPointer(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;
    unsigned __int16 Immediate;

    ArmOpcode.DataProcessing.Cond=0xe;
    ArmOpcode.DataProcessing.Reserved1=0;
    ArmOpcode.DataProcessing.I=1; // offset is an immediate
    ArmOpcode.DataProcessing.Opcode=(Opcode.AddToStackPointer.S) ? 2 :4; // subtract or add
    ArmOpcode.DataProcessing.S=0; // do not set condidion codes
    ArmOpcode.DataProcessing.Rn=R13;
    ArmOpcode.DataProcessing.Rd=R13;
    Immediate=Opcode.AddToStackPointer.SWord7*4; // Shift immed left by 2
    if (Immediate > 0xff) {
        // Operand2 can only accept an 8-bit immediate value.  Re-encode
        // as an 8-bit immediate with a rotate-right of 30
        Immediate = (15 << 8) | Opcode.AddToStackPointer.SWord7;
    }
    ArmOpcode.DataProcessing.Operand2=Immediate;

    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceDataProcessing);
}

void DecodeThumbPushPopRegisters(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    ArmOpcode.BlockDataTransfer.Cond=0xe;
    ArmOpcode.BlockDataTransfer.Reserved1=4;
    ArmOpcode.BlockDataTransfer.S=0; // do not load PSR or force user mode
    ArmOpcode.BlockDataTransfer.W=1; // do write-back
    ArmOpcode.BlockDataTransfer.L = Opcode.PushPopRegisters.L;
    if (Opcode.PushPopRegisters.L) { // POP
        // LDMIA
        ArmOpcode.BlockDataTransfer.P=0; // post-indexed
        ArmOpcode.BlockDataTransfer.U=1; // up (add offset from base)
    } else { // PUSH
        // STMDB
        ArmOpcode.BlockDataTransfer.P=1; // pre-indexed
        ArmOpcode.BlockDataTransfer.U=0; // down (subtract offset from base)
    }
    ArmOpcode.BlockDataTransfer.Rn=R13; // base address - the stack pointer
    ArmOpcode.BlockDataTransfer.RegisterList=Opcode.PushPopRegisters.Rlist;
    if (Opcode.PushPopRegisters.R) {
        if (Opcode.PushPopRegisters.L) {
            // POP - Include the PC register, R15
            ArmOpcode.BlockDataTransfer.RegisterList |= 1<<15;
        } else {
            // PUSH - Include the Link register, R14
            ArmOpcode.BlockDataTransfer.RegisterList |= 1<<14;
        }
    }

    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceBlockDataTransfer);
}

void DecodeThumbMultipleLoadStore(Decoded *d, THUMB_OPCODE Opcode)
{
    OPCODE ArmOpcode;

    ArmOpcode.BlockDataTransfer.Cond=0xe;
    ArmOpcode.BlockDataTransfer.Reserved1=4;
    ArmOpcode.BlockDataTransfer.S=0; // do not load PSR or force user mode
    ArmOpcode.BlockDataTransfer.W=1; // do write-back
    // Generate LDMIA/STMIA
    ArmOpcode.BlockDataTransfer.L = Opcode.MultipleLoadStore.L;
    ArmOpcode.BlockDataTransfer.P=0; // post-indexed
    ArmOpcode.BlockDataTransfer.U=1; // up (add offset from base)
    ArmOpcode.BlockDataTransfer.Rn=Opcode.MultipleLoadStore.Rb;
    ArmOpcode.BlockDataTransfer.RegisterList=Opcode.MultipleLoadStore.Rlist;

    DecodeARMInstruction(d, ArmOpcode);
    ASSERT(d->fp == PlaceBlockDataTransfer);
}



void DecodeThumbConditionalBranch(Decoded *d, THUMB_OPCODE Opcode)
{
    if (Opcode.ConditionalBranch.Cond == 0xf) {
        OPCODE ArmOpcode;

        // Software interrupt
        ArmOpcode.SoftwareInterrupt.Cond=0xe;
        ArmOpcode.SoftwareInterrupt.Reserved1=0xf;
        if (ProcessorConfig.ARM.GenerateSyscalls && Opcode.SoftwareInterrupt.Value8 == 0xAB) {
            ArmOpcode.SoftwareInterrupt.Ignored=0x123456;
        } else {
            ArmOpcode.SoftwareInterrupt.Ignored=Opcode.SoftwareInterrupt.Value8;
        }
        DecodeARMInstruction(d, ArmOpcode);
        ASSERT(d->fp == PlaceSoftwareInterrupt);
    } else if (Opcode.ConditionalBranch.Cond == 0xe) {
        // break opcode (not BKPT, but identified as "break" by the disassembler).  WinCE
        // uses this in its assert mechanism.
        d->fp = PlaceRaiseUndefinedException;
    } else {
        // Branch, accounting for 1-word prefetch here.
        d->Cond = Opcode.ConditionalBranch.Cond;
        d->Offset = d->GuestAddress+2*Opcode.ConditionalBranch.Soffset8+4;
        d->L=0;
        d->R15Modified = true;
        d->fp = PlaceBranch;
    }
}


bool DecodeThumbInstruction(Decoded *d, THUMB_OPCODE Opcode){
    d->Cond=14;
    switch (Opcode.Generic.Opcode) {
    case 0:
        // Move shifted register or Add/subtract
        DecodeThumbMoveAddSub(d, Opcode);
        break;

    case 1:
        // Move/compare/add/subtract immediate
        DecodeThumbMathImmediate(d, Opcode);
        break;

    case 2:
        // ALU operations, Hi register operations, PC-relative load, load/store with register
        // offset, or load/store with sign-extended byte/halfword
        DecodeThumbCase2(d, Opcode);
        break;

    case 3:
        // Load/store with immediate offset
        DecodeThumbLoadStoreImmediateOffset(d, Opcode);
        break;

    case 4:
        if (Opcode.LoadStoreHalfWord.Reserved1 == 0) {
            DecodeThumbLoadStoreHalfWord(d, Opcode);
        } else {
            DecodeThumbLoadStoreSPRelative(d, Opcode);
        }
        break;

    case 5:
        if (Opcode.LoadAddress.Reserved1 == 0) {
            DecodeThumbLoadAddress(d, Opcode);
        } else if (Opcode.AddToStackPointer.Reserved1 == 0x10) {
            DecodeThumbAddToStackPointer(d, Opcode);
        } else if (Opcode.AddToStackPointer.Reserved1 == 0x1E) {
            d->fp = PlaceBKPT;
        } else if (Opcode.PushPopRegisters.Reserved2 == 1 && Opcode.PushPopRegisters.Reserved1 == 2) {
            DecodeThumbPushPopRegisters(d, Opcode);
        } else {
            d->fp = PlaceRaiseUndefinedException;
        }
        break;

    case 6:
        if (Opcode.MultipleLoadStore.Reserved1 == 0) {
            DecodeThumbMultipleLoadStore(d, Opcode);
        } else {
            // This also handles Software Interrupt
            DecodeThumbConditionalBranch(d, Opcode);
        }
        break;


    case 7:
        d->Cond = 14;
        d->HTwoBits = Opcode.LongBranch.H;         // 2 bits
        switch(d->HTwoBits){
            case 0:
                d->Offset = 2* Opcode.UnconditionalBranch.Offset11;
                d->R15Modified=true;
                break;
            case 2:
                if (Opcode.LongBranch.Offset & 0x200) {
                    // Offset is negative
                    d->Offset = (0xfffffc00 | Opcode.LongBranch.Offset) << 12;
                } else {
                    d->Offset = Opcode.LongBranch.Offset << 12;
                }
                break;
            case 1: // fall through into case 3
            case 3:
                d->Offset = Opcode.LongBranch.Offset << 1;
                d->R15Modified = true;
                break;
            default:
                ASSERT(FALSE);
                break;
        }
        d->fp = PlaceThumbLongBranch;
        break;

    default:
        ASSERT(FALSE);
        break;
    }

    if (d->fp == PlaceRaiseUndefinedException) {
        // Stop disassembling if an undefined opcode is detected
        return false;
    } else {
        return true;
    }
}

void JitDecode(PENTRYPOINT ContainingEntrypoint, unsigned __int32 InstructionPointer)
{
    unsigned __int32 i;
    unsigned __int32 EndAddress;
    int InstructionSize;
    __int8 TLBIndexCache;
    unsigned __int32 ActualInstructionPointer;

    if (Cpu.CPSR.Bits.ThumbMode) {
        InstructionSize = 2;
        InstructionPointer &= 0xfffffffe;
    } else {
        InstructionSize = 4;
        InstructionPointer &= 0xfffffffc;
    }

    if (ContainingEntrypoint) {
        // No need to compile past the end of the current entrypoint
        EndAddress = ContainingEntrypoint->GuestEnd;
    } else {
        PENTRYPOINT ep;

        ep = GetNextEPFromGuestAddr(InstructionPointer);
        if (ep) {
            EndAddress = ep->GuestStart;
        } else {
            EndAddress = 0xffffffff;
        }
    }

    memset(&Instructions, 0, sizeof(Instructions));
    TLBIndexCache=0;

    ActualInstructionPointer = MmuActualGuestAddress(InstructionPointer);

    for (i=0; i<ARRAY_SIZE(Instructions) && ActualInstructionPointer < EndAddress; ++i, InstructionPointer += InstructionSize, ActualInstructionPointer += InstructionSize) {
        size_t HostEffectiveAddress;

        Instructions[i].GuestAddress = InstructionPointer;
        Instructions[i].ActualGuestAddress = ActualInstructionPointer;
        Instructions[i].JmpFixupLocation = NULL;

        if (Mmu.ControlRegister.Bits.M) {
            HostEffectiveAddress = Mmu.MmuMapExecute.MapGuestVirtualToHost(ActualInstructionPointer, &TLBIndexCache);
        } else {
            HostEffectiveAddress = Mmu.MapGuestPhysicalToHost(ActualInstructionPointer);
        }
        if (HostEffectiveAddress == 0) {
            if (BoardIOAddress) {
                LogPrint((output,"Attempt to execute from IO space at 0x%8.8x\n", InstructionPointer));
                ASSERT(FALSE);
            }

            if(InstructionPointer>0xf0000000){
                // this instruction will definitely raise and abort prefetch exception
                Instructions[i].fp = PlaceRaiseAbortPrefetchException;
                Instructions[i].GuestAddress=InstructionPointer;
                Instructions[i].Cond=14;
                Instructions[i].Immediate = Mmu.FaultAddress;        // capture the MMU state for replay at runtime
                Instructions[i].Reserved3 = Mmu.FaultStatus.Word;    // capture the MMU state for replay at runtime
                i++;
                break;
            } else{
                // stop decoding and just execute all instructions decoded so far.
                // if zero instructions have been decoded, JitCompile will call CpuRaiseAbortPrefetchException explicitly.
                break;
            }
        }

        if (DebugHardwareBreakpointPresent(ActualInstructionPointer)) {
            Instructions[i].fp = PlaceHardwareBreakpoint;
            Instructions[i].Cond = 15; // unconditional
            Instructions[i].R15Modified = 1; // assume the instruction modifies R15
        } else if (Cpu.CPSR.Bits.ThumbMode) {
            THUMB_OPCODE Opcode;

            Opcode = *(PTHUMB_OPCODE)HostEffectiveAddress;

            if (!DecodeThumbInstruction(&Instructions[i], Opcode)) {
                i++;
                break;
            }

        } else {
            OPCODE Opcode;

            Opcode = *(POPCODE)HostEffectiveAddress;

            if (!DecodeARMInstruction(&Instructions[i], Opcode)) {
                i++;
                break;
            }
                                                    
            if (Opcode.Word == 0xeafffffe) {
                // the branch-to-self instructions is a power-off instruction.
                // if its the one at address 0x92001004, this is the one that's used for
                // powering down in suspend mode.

                if(Instructions[i].ActualGuestAddress == 0x92001010){
                    Instructions[i].fp = PlacePowerDown;
                }
            }
        }
    }
    NumberOfInstructions = i;
}

int LocateEntrypoints(void)
{
    unsigned __int32 i;
    unsigned __int32 GuestStart;
    unsigned __int32 GuestEnd;
    int EntrypointCounter;
    unsigned __int32 InstructionSize;

    if (Cpu.CPSR.Bits.ThumbMode) {
        InstructionSize = 2;
    } else {
        InstructionSize = 4;
    }

    //
    // Find all instructions which need Entrypoints.
    //     Performance is O(n^2) in the worst case, although
    //     it will be typically much closer to O(n)
    //
    //  Instructions which mark the starts of Entrypoints have
    //  their .EntryPoint pointer set to non-NULL.  Instructions which
    //  don't require entrypoints have it set to NULL;

    GuestStart=Instructions[0].ActualGuestAddress;
    GuestEnd=Instructions[NumberOfInstructions-1].ActualGuestAddress+1;

    //
    // The first instruction always gets an entrypoint
    //
    Instructions[0].Entrypoint = (PENTRYPOINT)1;

    //
    // Visit each instruction in turn
    //
    for (i=0; i<NumberOfInstructions; i++) {

        if (g_fOptimizeCode == false || ((i+1) < NumberOfInstructions && Instructions[i].R15Modified)) {
            //
            // This instruction marks the end of an Entrypoint.  The next
            // instruction gets a new Entrypoint.
            //
            Instructions[i+1].Entrypoint = (PENTRYPOINT)1;
        }

        //
        // Now see if it is a direct control transfer instruction with a
        // destination that lies within this instruction stream.  If it is,
        // we want to create an Entry Point at the destination so that the
        // control transfer will be compiled directly to the updated form,
        // and won't have to be updated later.
        //
        if (Instructions[i].fp == PlaceBranch) {
            unsigned __int32 ActualDestinationAddress = MmuActualGuestAddress((unsigned __int32)Instructions[i].Offset);
            Instructions[i].Reserved3 = (unsigned __int32)Instructions[i].Offset; 
            if (ActualDestinationAddress >= GuestStart && ActualDestinationAddress < GuestEnd) {
                unsigned int Offset = (ActualDestinationAddress - Instructions[0].ActualGuestAddress) / InstructionSize;

                Instructions[Offset].Entrypoint = (PENTRYPOINT)1;

                if (ProcessorConfig.ARM.GenerateSyscalls == 0 &&
                    Offset == i-2 &&    // the destination is two instructions before the branch
                    // LDR Rd, [Rn+offset]  where Rd is not R15 and Rd is not Rn
                    Instructions[i-2].fp == PlaceSingleDataTransfer &&
                    Instructions[i-2].W == 0 &&
                    Instructions[i-2].P == 1 &&
                    Instructions[i-2].L == 1 &&
                    Instructions[i-2].B == 0 &&
                    Instructions[i-2].I == 0 &&
                    Instructions[i-2].Rd != R15 &&
                    Instructions[i-2].Rd != Instructions[i-2].Rn &&
                    // CMP Rn, xxxx    where Rn is the same Rn from the above LDR
                    Instructions[i-1].fp == PlaceDataProcessing &&
                    Instructions[i-1].Opcode == 10 &&
                    Instructions[i-1].Rn == Instructions[i-2].Rd &&
                    // conditional branch backward to the LDR
                    Instructions[i].Cond != 14) {

                    // All of the above conditions have been met - this is an idle
                    // loop which intends to be broken only when an interrupt
                    // arrives.
                    Instructions[i].fp = PlaceIdleLoop;
                }
            }
        } else if (Instructions[i].fp == PlaceDataProcessing &&
                   Instructions[i].Opcode == 4 &&
                   Instructions[i].Rd == R14 &&
                   Instructions[i].Rn == R15 &&
                   Instructions[i].I == 1) {
            // "ADD LR, PC, #Imm" - mark "PC+Imm+
            unsigned __int32 Immediate = _rotr((unsigned __int8)Instructions[i].Operand2, (Instructions[i].Operand2 >> 8) << 1);
            unsigned __int32 ActualDestinationAddress = Immediate+Instructions[i].ActualGuestAddress+Immediate+((Cpu.CPSR.Bits.ThumbMode) ? 4 : 8);
            if (ActualDestinationAddress >= GuestStart && ActualDestinationAddress < GuestEnd) {
                int Offset = (ActualDestinationAddress - Instructions[0].ActualGuestAddress) / InstructionSize;
                Instructions[Offset].Entrypoint = (PENTRYPOINT)1;
            }
        }

        // calculate which flags are consumed by this instruction.
        Instructions[i].FlagsNeeded = FlagList[Instructions[i].Cond/2];

        if(    Instructions[i].fp == PlaceDataProcessing) {
            if ((Instructions[i].Opcode == 5 || Instructions[i].Opcode == 6 ||Instructions[i].Opcode == 7) ||
                 (Instructions[i].I == 0 && (Instructions[i].Operand2 & 0xff0) == 0x060)){
                // ADC, SBC, and RSC consume the carry flag, as does any instruction
                // which has "RRX" as Operand2 (which is encoded as "ROR #0").
                Instructions[i].FlagsNeeded |= FLAG_CF;
            }
        }

        //calculate which flags this instruction generates.
        if(Instructions[i].S){

            if(Instructions[i].fp == PlaceDataProcessing) {
                unsigned __int32 Opcode = Instructions[i].Opcode;

                if(Opcode < 2 || Opcode == 8 || Opcode == 9 || Opcode > 11){
                    // These are logical instructions
                    Instructions[i].FlagsSet = FLAG_NF | FLAG_ZF | FLAG_CF;
                    Instructions[i].FlagsNeeded = FLAG_CF; // in case shifter_carry_out == CF
                } else {
                    // arithmetic
                    Instructions[i].FlagsSet = ALL_FLAGS;
                }

                if(Instructions[i].R15Modified){
                    if(Instructions[i].Opcode < 8 || Instructions[i].Opcode > 11){
                        // if the instruction modifies R15, and it is not TST, TEQ, CMP, or CMN (opcodes 8,9,10,11)
                        // then it will copy the SPSR into the CPSR and all flags are set
                        Instructions[i].FlagsSet = ALL_FLAGS;
                    }
                }

            } else if( Instructions[i].fp == PlaceBlockDataTransfer){

                if(Instructions[i].L && Instructions[i].R15Modified){
                    // this instruction loads the SPSR into the CPSR;
                    Instructions[i].FlagsSet = ALL_FLAGS;
                }

            } else if (Instructions[i].fp == PlaceArithmeticExtension){

                Instructions[i].FlagsSet = FLAG_NF | FLAG_ZF;

            } else if (Instructions[i].fp == PlaceMSRImmediate){

                // UpdateCPSR is called under this condition
                if( (!(Instructions[i].Op1 & 2)) && (Instructions[i].Rn & 8) ){
                    Instructions[i].FlagsSet = ALL_FLAGS;
                }

            } else if(Instructions[i].fp == PlaceMRSorMSR){

                // UpdateCPSR is called under this condition
                if(Instructions[i].Op1 == 1){
                    Instructions[i].FlagsSet = ALL_FLAGS;
                }
                // MoveFromCPSR is called under this condition
                else if(Instructions[i].Op1 == 0){
                    Instructions[i].FlagsNeeded |= ALL_FLAGS;
                }

            } else if (Instructions[i].fp == PlaceLoadStoreExtension){

                // LoadStoreExtension uses the S bit for sign extension.
                // no flags are generated

            } else {
                ASSERT(FALSE);
            }
        }
    }

    //
    // Convert the EntryPoint field from NULL/non-NULL to a unique
    // value for each range of instructions.
    //
    unsigned int j;
    EntrypointCounter=1;
    for (i=0; i<NumberOfInstructions; i=j, EntrypointCounter++) {
        //
        // This instruction marks the beginning of a basic block
        //
        Instructions[i].Entrypoint = (PENTRYPOINT)LongToPtr(EntrypointCounter);
        for (j=i+1; j<NumberOfInstructions; ++j) {
            if ((j >= NumberOfInstructions) || Instructions[j].Entrypoint) {
                //
                // Either ran out of instructions, or encountered an instruction
                // which marks the start of the next basic block.
                //
                break;
            }
            Instructions[j].Entrypoint = (PENTRYPOINT)LongToPtr(EntrypointCounter);
        }
    }

    //
    // At this point, EntrypointCounter holds the number of EntryPoints
    // plus one, because we started the counter at 1, not 0.  Correct
    // that now.
    //
    return EntrypointCounter-1;
}


// Returns the number of entrypoints that must be created.
int JitOptimizeIR(void)
{
    int EntrypointCount;
    unsigned int DCacheOptimizationSlot = (unsigned)-1;

    // Perform optimizations here that don't depend on entrypoint information

    // For now, create each instruction in its own entrypoint
    EntrypointCount = LocateEntrypoints();

    // Perform optimizations here that do depend on entrypoint information
    for (unsigned int i=0; i<NumberOfInstructions; ++i) {
        //
        // Perform single-instruction optimizations here...
        //

        if (Instructions[i].fp == PlaceSingleDataTransfer &&
            Instructions[i].Rn == R15  &&
            Instructions[i].P &&
            Instructions[i].B == 0 &&
            Instructions[i].W == 0 &&
            Instructions[i].L &&
            Instructions[i].I == 0) {
            // "LDR Rd, [PC+immediate]" without writeback, but potentially with a condition code.
            unsigned __int32 InstructionPointer = MmuActualGuestAddress(Instructions[i].Reserved3);

             if (Mmu.ControlRegister.Bits.M && (InstructionPointer & 3) == 0) {
                 // MMU is enabled, and a load from a dword-aligned PC-relative address.  ("LDR Rd, [R15+Offset]"). Sneak a peek and see if the
                 // address is accessible.
                 static __int8 TLBIndexCache=0;
                 size_t HostEffectiveAddress;
    
                HostEffectiveAddress = Mmu.MmuMapRead.MapGuestVirtualToHost(InstructionPointer, &TLBIndexCache);
                if (HostEffectiveAddress && Mmu.MmuMapReadWrite.MapGuestVirtualToHost(InstructionPointer, &TLBIndexCache) == 0) {
                    // the InstructionPointer is in read (but not read-write) memory.  It is safe to cache.
                     unsigned __int32 Value;
    
                     // The address is accessible - retrieve the value and inline it into the Translation
                     // Cache.  Note that this optimization is slightly unsafe, as it is possible for R15
                     // to point in read/write memory and the value at PC+Offset could change at
                     // runtime.
                    // Replace the "LDR Rd, [PC+Imm]" with "MOV Rd, Value"
                     Value = *(unsigned __int32*)HostEffectiveAddress;
                    Instructions[i].fp = PlaceDataProcessing;
                    Instructions[i].Opcode = 13; // MOV
                    Instructions[i].S=0;
                    Instructions[i].I=1;
                    Instructions[i].Reserved3 = Value;
                 }
            }
        }
        
        if (i < 1) {
            continue;
        }
        //
        // Perform two-or-more instruction optimizations here, as i >= 1 from this point onwards.
        //
        if (Instructions[i-1].Entrypoint == Instructions[i].Entrypoint &&
            Instructions[i-1].Cond == Instructions[i].Cond &&
            Instructions[i-1].fp == PlaceSingleDataTransfer &&
            Instructions[i].fp == PlaceSingleDataTransfer &&
            Instructions[i-1].L == 0 &&
            Instructions[i].L == 1 &&
            Instructions[i-1].Rn == R13 &&
            Instructions[i].Rn == R13 &&
            Instructions[i-1].U == Instructions[i].U &&
            Instructions[i-1].I == Instructions[i].I &&
            Instructions[i-1].W == 0 &&
            Instructions[i].W == 0 &&
            Instructions[i-1].P == 1 &&
            Instructions[i].P == 1 &&
            Instructions[i-1].B == 0 &&
            Instructions[i].B == 0 &&
            Instructions[i-1].Operand2 == Instructions[i].Operand2) {

            // STR X, [sp, #xxxx]
            // LDR Y, [sp, #xxxx] where the immediate values are identical
            // Replace the LDR by "MOV Y, X" to avoid calling the MMU
            Instructions[i].fp = PlaceDataProcessing;
            Instructions[i].I = 0;
            Instructions[i].S = 0;
            Instructions[i].Opcode = 13;
            //Instructions[i].Rd is already set correctly
            Instructions[i].Operand2 = Instructions[i-1].Rd;
        } else if (Instructions[i-1].Entrypoint == Instructions[i].Entrypoint &&
            Instructions[i-1].Cond == Instructions[i].Cond &&
            Instructions[i-1].fp == PlaceDataProcessing &&
            Instructions[i-1].Opcode == 15 && // MVN
            Instructions[i-1].I == 1 &&
            Instructions[i-1].S == 0 &&
            Instructions[i-1].Rd != R15) {

            // MVN X, immediate
            if (g_fOptimizeCode && 
                Instructions[i].fp == PlaceDataProcessing &&
                Instructions[i].Opcode == 4 && // ADD
                Instructions[i].I == 0 &&
                Instructions[i].S == 0 &&
                Instructions[i].Rd == Instructions[i-1].Rd &&
                Instructions[i].Operand2 == Instructions[i].Rd) {

                // MVN X, immediate
                // ADD Y, X, Y
                // Replace with: NOP / ADD Y, X, immediate
                // Note that the full immediate value is stored in Instructions[i].Reserved3.
                Instructions[i-1].fp = PlaceNop;
                Instructions[i].I = 1;
                Instructions[i].Reserved3 = ~Instructions[i-1].Reserved3;
            } else if (Instructions[i].fp == PlaceDataProcessing &&
                Instructions[i].Opcode == 4 && // ADD
                Instructions[i].I == 1 &&
                Instructions[i].S == 0 &&
                Instructions[i].Rd == R15 &&
                Instructions[i].Rn == Instructions[i-1].Rd) {
                // MVN X, immediate
                // ADD R15, X, immediate
                // Replace the ADD with "B destination"
                Instructions[i].fp = PlaceBranch;
                Instructions[i].L = 0;
                Instructions[i].Offset = ~Instructions[i-1].Reserved3 + Instructions[i].Reserved3;
                Instructions[i].Reserved3 = Instructions[i].Offset;
            } else if (g_fOptimizeCode &&
                i+1 < NumberOfInstructions &&
                Instructions[i].fp == PlaceDataProcessing &&
                Instructions[i].Opcode == 13 && // MOV
                Instructions[i].I == 0 &&
                Instructions[i].Operand2 == R15 &&
                Instructions[i].Rd == R14 &&
                Instructions[i].S == 0 &&
                Instructions[i+1].Entrypoint == Instructions[i].Entrypoint &&
                Instructions[i+1].Cond == Instructions[i].Cond &&
                Instructions[i+1].fp == PlaceDataProcessing &&
                Instructions[i+1].Opcode == 4 && // ADD
                Instructions[i+1].I == 1 &&
                Instructions[i+1].S == 0 &&
                Instructions[i+1].Rd == R15 &&
                Instructions[i+1].Rn == Instructions[i-1].Rd) {
                // MVN X, immediate
                // MOV LR, R15
                // ADD R15, X, immediate
                // Rewrite as:
                // MVN X, immediate
                // nop
                // BL destination
                Instructions[i].fp = PlaceNop;
                Instructions[i+1].fp = PlaceBranch;
                Instructions[i+1].L = 1;
                Instructions[i+1].Offset = ~Instructions[i-1].Reserved3 + Instructions[i+1].Reserved3;
                Instructions[i+1].Reserved3 = Instructions[i+1].Offset;
                }
        } else if (Instructions[i-1].fp == PlaceDataProcessing &&
            Instructions[i-1].Opcode == 13 &&
            Instructions[i-1].I == 0 &&
            Instructions[i-1].Rm == R15 &&
            Instructions[i-1].Rd == R14) {
            // Encountered a "MOV R14, R15" instruction            

            if (// followed by a DataProcessing instruction that modifies R15
                Instructions[i].fp == PlaceDataProcessing &&
                Instructions[i].R15Modified ) {

                // this is making a CALL instruction
                // use PlaceDataProcessingCALL instead, so the return address is pushed onto the ShadowStack
                Instructions[i].fp = PlaceDataProcessingCALL;
            } else if (Instructions[i].fp == PlaceBx) {
                // MOV R14, R15 followed by BX:  this is a CALL instruction
                Instructions[i].fp = PlaceBxCALL;
            } else if (Instructions[i].fp == PlaceSingleDataTransfer && 
                       Instructions[i].Rd == R15 && 
                       Instructions[i].Rn != R14 &&
                       Instructions[i].L) {
                // MOV R14, R15 followed by a LDR PC, [ea], but where the EA isn't
                // relative to R14:  this is a CALL instruction.  If the EA is
                // relative to R14, then it is most likely the beginning of a NetCF
                // switch statement, which does "MOV R14, R15" followed by
                // "LDR R15, [R14, +R0, lsl #2]" to jump into the dispatch table.
                Instructions[i].fp = PlaceSingleDataTransferCALL;
            }
        } else if (Instructions[i-1].fp == PlaceDataProcessing &&
            Instructions[i-1].Opcode == 4 &&
            Instructions[i-1].Rd == R14 &&
            Instructions[i-1].Rn == R15 &&
            Instructions[i-1].I == 1 &&
            Instructions[i].fp == PlaceDataProcessing &&
            Instructions[i].R15Modified) {
            // Encountered "ADD LR, PC, #Imm" followed by a data processing that modified R15 (like "MOV PC, R12").
            Instructions[i].fp = PlaceDataProcessingCALL;
                
        } else if (Instructions[i-1].fp == PlaceSingleDataTransfer &&
                   Instructions[i-1].L == 0 &&
                   Instructions[i].fp == PlaceSingleDataTransfer &&
                   Instructions[i].L == 1 &&
                   Instructions[i].Rd == R15 &&
                   Instructions[i-1].Rd  == Instructions[i].Rn) {
            // STR Rx, [ea]
            // LDR R15, [Rx+ea]
            // This is a NetCF method epilog.  The return address is stored at an offset inside a "RunState"
            // structure on the stack.  The STR updates the pCurRunState within a per-thread structure, then
            // the LDR jumps to the return address saved within the current RunState.  In NetCF v2, Rx is R4
            // and the per-thread structure is pointed to by R11.
            Instructions[i].fp = PlaceSingleDataTransferRET;
        }
        
        /*Optimizing DCleanCache Instruction.  In the function, there are two nested loops
        which iterate and clean each line in the sets of the caches
        The function uses a mcr instruction to clean the line, and in the emulator the
        mcr instruction is a no-op.  We will optimize the loop into a single pass
        which will end with the same flags and registers as if the loops have been executed

        Optimizing the inner loop 
        clean_setloop
        ; clean the line
        mcr     p15, 0, r12, c7, c10, 2
        ; add the set index
        add     r12, r12, r3

        ; decrement the set number
        subs    r1, r1, #1
        bpl     clean_setloop
        */
        
        else if (g_fOptimizeCode &&
            i+3 < NumberOfInstructions &&
            Instructions[i+1].Entrypoint == Instructions[i].Entrypoint &&
            Instructions[i+2].Entrypoint == Instructions[i].Entrypoint &&
            Instructions[i+3].Entrypoint == Instructions[i].Entrypoint &&
            Instructions[i+1].Cond == Instructions[i].Cond &&
            Instructions[i+2].Cond == Instructions[i].Cond &&
            Instructions[i].fp == PlaceCoprocRegisterTransfer &&        //mcr p15, 0, r12, c7, c10, 2
            Instructions[i].L == 0 &&
            Instructions[i].CPNum == 15 &&    
            Instructions[i].CPOpc == 0 &&
            Instructions[i].Rd == 12 &&
            Instructions[i].CRn == 7 &&
            Instructions[i].CP == 2 &&
            Instructions[i+1].fp == PlaceDataProcessing &&        //add r12, r12, r3
            Instructions[i+1].Opcode == 4 && 
            Instructions[i+1].Rd == 12 &&
            Instructions[i+1].Operand2 == 3 &&        
            Instructions[i+1].Rn == 12 &&
            Instructions[i+2].fp == PlaceDataProcessing &&        //subs r1, r1, #1
            Instructions[i+2].Opcode == 2 && 
            Instructions[i+2].I == 1 &&        
            Instructions[i+2].S == 1 &&
            Instructions[i+2].Rd == 1 &&
            Instructions[i+2].Rn == 1 &&        
            Instructions[i+2].Reserved3 == 1 &&
            Instructions[i+3].fp == PlaceBranch &&                //bpl clean_setloop
            Instructions[i+3].Cond == 5 &&                        //pl condition    
            (Instructions[i+3].GuestAddress - Instructions[i+3].Offset) == 12){
                //we replace the  mcr p15, 0, r12, c7, c10, 2 with a MOV R1, 0
                //instruction.  The decrement on R1 following the MOV will set 
                //R1 = -1, thereby setting the correct flags and ending register values
                //The loop is optimized out and the code segment becomes one single pass
                Instructions[i].fp = PlaceDataProcessing;        //MOVS R1, 0
                Instructions[i].Rd = 1;         
                Instructions[i].I =  1;
                Instructions[i].Reserved3 = 0;
                Instructions[i].Opcode = 13;
                Instructions[i].S = 0;
                //we store a placeholder on the last instruction of the segment
                //so that the optimization of the outer loop will know that the 
                //inner loop has indeed taken place.
                DCacheOptimizationSlot = i+3;    
        }
        /*
            Second Stage of the optimization, which will remove the outer loop.
            The outerloop will not be removed if the first stage of the optimization
            did not take place.
            The out loop is shown as follows

            clean_indexloop
            mov     r12, r0, lsl r2                 ; index goes in the top 6 bits
            mov     r1, r4                          ; reload number of sets
            ...
                inner loop
            ...
            
            ; decrement the index number
            subs    r0, r0, #1
            bpl     clean_indexloop

            ; drain the write buffer
             mov     r0, #0
            mcr     p15, 0, r0, c7, c10, 4
        */
        else if (g_fOptimizeCode &&
            i+1 < NumberOfInstructions &&
            Instructions[i+1].Entrypoint == Instructions[i].Entrypoint &&
            i-1 == DCacheOptimizationSlot &&  //confirm the first optimization pass has been completed
            Instructions[i].fp == PlaceDataProcessing &&    //subs r0, r0, #1
            Instructions[i].Opcode == 2 && 
            Instructions[i].I == 1 &&
            Instructions[i].S == 1 &&
            Instructions[i].Rd == 0 &&
            Instructions[i].Rm == 0 &&                        
            Instructions[i].Reserved3 == 1 &&
            Instructions[i+1].fp == PlaceBranch &&            //bpl clean_indexloop
            Instructions[i+1].Cond == 5 &&                    //pl condition    
            (Instructions[i+1].GuestAddress - Instructions[i+1].Offset) == 28){
                
                //We can replace the subs and bpl with no ops because the first optimization
                //pass will have already set the condition flags to the correct value
                //r0 will be set to zero in the instruction following the branch.
                Instructions[i].fp = PlaceNop;
                Instructions[i+1].fp = PlaceNop;
                DCacheOptimizationSlot = (unsigned)-1; //reset the place holder
        }
        /*Optimizing ArmFlushICacheLines Instruction.  The function is shown as follows
            -        r0  start address
            -        r1  length of region
 
        ; invalidate the range of lines
        10
        mcr        p15, 0, r0, c7, c5, 1   ; invalidate each entry
        add     r0, r0, r2              ; on to the next line
        subs       r1, r1, r2              ; reduce the number of bytes left
        bgt     %b10                    ; loop while > 0 bytes left
 
        RETURN
         END
    
        The function invokes a mcr instruction, which in turn call MMU::FlushTranslationCacheHelper
        to invalidate each line of the selected range in the ICache.  Each line is repeated
        until the entire set is invalidate, hence after many invokations of the FlushTranslationCacheHelper
        Since FlushTranslationHelper can flush the entire section in one call, we optimize out
        the loop and pass in the entire cache range into the function.  r1 stores the length of the region
        while r0 stores the start address

        On Bowmore, the add/subs use r3 as the cache line size, instead of r2.

        */
        else if (g_fOptimizeCode && 
            i+3 < NumberOfInstructions &&        
            Instructions[i+1].Entrypoint == Instructions[i].Entrypoint &&
            Instructions[i+2].Entrypoint == Instructions[i].Entrypoint &&
            Instructions[i+3].Entrypoint == Instructions[i].Entrypoint &&
            Instructions[i+1].Cond == Instructions[i].Cond &&
            Instructions[i+2].Cond == Instructions[i].Cond &&
            Instructions[i].fp == PlaceCoprocRegisterTransfer &&        //mcr p15, 0, r0, c7, c5, 1
            Instructions[i].L == 0 &&
            Instructions[i].CPNum == 15 &&    
            Instructions[i].CPOpc == 0 &&
            Instructions[i].Rd == 0 &&
            Instructions[i].CRn == 7 &&
            Instructions[i].CRm == 5 &&
            Instructions[i].CP == 1 &&
            Instructions[i+1].fp == PlaceDataProcessing &&        //add r0, r0, rX
            Instructions[i+1].Opcode == 4 && 
            Instructions[i+1].Rd == 0 &&
            Instructions[i+1].Operand2 == Instructions[i+2].Operand2 &&        
            Instructions[i+1].Rn == 0 &&
            Instructions[i+2].fp == PlaceDataProcessing &&        //subs r1, r1, rX
            Instructions[i+2].Opcode == 2 && 
            Instructions[i+2].Rd == 1 &&
            Instructions[i+2].Rn == 1 &&        
            Instructions[i+3].fp == PlaceBranch &&                //bgt %b10
            Instructions[i+3].Cond == 12 &&                        //gt condition    
            (Instructions[i+3].GuestAddress - Instructions[i+3].Reserved3) == 12){

                //In the MCR instruction, we set Operand2 (currently unused) to 
                //an identifier (0xffffffff was selected arbitrary) to differentiate 
                //between the optimized and regular ICacheFlush calls
                Instructions[i].Operand2 = 0xffffffff;

		// The flush triggered by Instructions[i] never returns
		Instructions[i+1].fp = PlaceNop;
		Instructions[i+2].fp = PlaceNop;
		Instructions[i+3].fp = PlaceNop;
            }

    }

    return EntrypointCount;
}

void OptimizeARMFlags()
{
    unsigned __int32 FlagsNeeded;        // flags required to execute current x86 instr
    unsigned __int32 FlagsToGenerate;    // flags which current x86 instr must generate
    unsigned __int32 PassNumber = 0;    // number of times outer loop has looped
    unsigned __int32 KnownFlagsNeeded[MAX_INSTRUCTION_COUNT]; // flags needed for each instr
    unsigned __int32 InstructionSize;
    bool fPassNeeded = true;            // TRUE if the outer loop needs to loop once more
    unsigned __int32 i;

    unsigned __int32 GuestStart;
    unsigned __int32 GuestEnd;

    // These two lines are needed only to satisfy PREfix:  NumberOfInstructions is always > 0 so 
    // KnownFlagsNeeded[0] is always initialized below.  PREfix doesn't know that.
    ASSERT(NumberOfInstructions > 0);
    KnownFlagsNeeded[0] = 0; 

    GuestStart = Instructions[0].ActualGuestAddress;
    GuestEnd = Instructions[NumberOfInstructions-1].ActualGuestAddress+1;

    if (Cpu.CPSR.Bits.ThumbMode) {
        InstructionSize = 2;
    } else {
        InstructionSize = 4;
    }

    while (fPassNeeded) {

        // This loop is executed at most two times.  The second pass is only
        // required if there is a control-transfer instruction whose
        // destination is within the Instruction Stream and at a lower
        // Intel address  (ie. a backwards JMP).

        fPassNeeded = false;
        PassNumber++;
        ASSERT(PassNumber <= 2);

        // Iterate over all decoded instructions, from bottom to top,
        // propagating flags info up.  Start off by assuming all flags
        // must be up-to-date at the end of the last basic block.

        FlagsNeeded = ALL_FLAGS;
        i = NumberOfInstructions;

        do {
            i--;

            if(Instructions[i].FlagsNeeded || Instructions[i].FlagsSet || Instructions[i].R15Modified ){
                // what flags this instruction has to generate. doesn't generate flags that aren't needed
                FlagsToGenerate = FlagsNeeded & Instructions[i].FlagsSet;

                Instructions[i].FlagsSet = FlagsToGenerate;

                // Calculate what flags this instruction will need to have
                // computed before it can be executed.
                if(Instructions[i].FlagsNeeded){
                    // if it's conditionally executed, assume worst - that this instructions doesn't set flags.
                    FlagsNeeded |= Instructions[i].FlagsNeeded;
                } else {
                    FlagsNeeded = (FlagsNeeded & ~FlagsToGenerate) | Instructions[i].FlagsNeeded;        // this does the 'reset', if all flags were generated
                }

                if (Instructions[i].R15Modified) { // control-transfer

                    if(i<NumberOfInstructions-1){
                        // Instructions[i+1] marks the start of an entrypoint
                        (Instructions[i+1].Entrypoint)->FlagsNeeded = KnownFlagsNeeded[i+1];
                    }

                    if (Instructions[i].fp == PlaceBranch) {
                        unsigned __int32 ActualDestinationAddress = MmuActualGuestAddress(Instructions[i].Reserved3);
                        // Destination address is known. See if it lies within the current instruction stream.
                        if (ActualDestinationAddress >= GuestStart && ActualDestinationAddress < GuestEnd) {
                            if (ActualDestinationAddress > Instructions[i].ActualGuestAddress) {
                                // The destination of the control-transfer is at a higher
                                // address in the Instruction Stream.  Pick up the
                                // already-computed FlagsNeeded for the destination.
                                unsigned __int32 NewFlagsNeeded = ALL_FLAGS;   // assume a pun
                                unsigned __int32 distance = (ActualDestinationAddress - Instructions[i].ActualGuestAddress) / InstructionSize;

                                if(Instructions[i+distance].ActualGuestAddress == ActualDestinationAddress){
                                    NewFlagsNeeded = KnownFlagsNeeded[i+distance];
                                }

                                FlagsNeeded |= NewFlagsNeeded;
                            } else {

                                // The destination of the control-transfer is at a lower
                                // address in the Instruction Stream.
                                if (PassNumber == 1) {
                                    // Need to make a second pass over the flags
                                    // optimizations in order to determine what flags are
                                    // needed for the destination address.
                                    fPassNeeded = true;
                                    FlagsNeeded = ALL_FLAGS; // assume all flags are needed
                                } else {
                                    
                                    // Search for the destination address within the Instruction
                                    // Stream.  Destination address may not be found if there is
                                    // a pun.
                                    unsigned __int32 NewFlagsNeeded = ALL_FLAGS;  // assume there is a pun
                                    unsigned __int32 distance = (Instructions[i].ActualGuestAddress - ActualDestinationAddress) / InstructionSize;

                                    if(Instructions[i-distance].ActualGuestAddress == ActualDestinationAddress){
                                        NewFlagsNeeded = KnownFlagsNeeded[i-distance];
                                    }

                                    FlagsNeeded |= NewFlagsNeeded;
                                }
                            }
                        } else {
                            // this address does not lie within the current instruction stream.
                            // lookup the entrypoint for this destination address.
                            // if it exists, get its FlagsNeeded field
                            PENTRYPOINT ep = EPFromGuestAddrExact(Instructions[i].Offset);
                            if(ep){
                                FlagsNeeded |= ep->FlagsNeeded;
                            } else {
                                // ep wasnt found. assume the worst
                                FlagsNeeded = ALL_FLAGS;
                            }
                        }
                    } else{
                        // this branches to an address unknown until run-time. assume the worst
                        FlagsNeeded = ALL_FLAGS;
                    }
                }
            }

            // KnownFlagsNeeded contains what flags are needed when jumping to this instruction.
            KnownFlagsNeeded[i] = FlagsNeeded;

        } while (i);
    }

    (Instructions[0].Entrypoint)->FlagsNeeded = KnownFlagsNeeded[0];

}

void JitCreateEntrypoints(PENTRYPOINT ContainingEntrypoint, size_t EntrypointMemory)
{
    unsigned __int32 i;
    unsigned __int32 j;
    PEPNODE EP = NULL;
    PENTRYPOINT Entrypoint;
    PENTRYPOINT PrevEntrypoint;

    PrevEntrypoint = Instructions[0].Entrypoint;
    for (i=0; i<NumberOfInstructions; ) {
        if (ContainingEntrypoint) {
            Entrypoint = (PENTRYPOINT)EntrypointMemory;
            EntrypointMemory += sizeof(ENTRYPOINT);
        } else {
            EP = (PEPNODE)EntrypointMemory;
            Entrypoint = &EP->ep;
            EntrypointMemory += sizeof(EPNODE);
        }

        for (j=i+1; j<NumberOfInstructions; ++j) {
            if (Instructions[j].Entrypoint != PrevEntrypoint) {
                PrevEntrypoint = Instructions[j].Entrypoint;
                break;
            }
            Instructions[j].Entrypoint = Entrypoint;
        }

        // Fill in the ENTRYPOINT structure
        Entrypoint->GuestStart = Instructions[i].ActualGuestAddress;
        Entrypoint->FlagsNeeded = ALL_FLAGS;
        if (j < NumberOfInstructions) {
            Entrypoint->GuestEnd = Instructions[j].ActualGuestAddress-1;
        } else {
            int Prev;

            //for (Prev=j-1; Instructions[Prev].Size==0; --Prev) {
            //}
            Prev=j-1;
            Entrypoint->GuestEnd = Instructions[Prev].ActualGuestAddress+((Cpu.CPSR.Bits.ThumbMode) ? 1 : 3);
        }
        Instructions[i].Entrypoint = Entrypoint;

        if (ContainingEntrypoint) {
            // Link this sub-entrypoint into the containing entrypoint
            Entrypoint->SubEP = ContainingEntrypoint->SubEP;
            ContainingEntrypoint->SubEP = Entrypoint;
        } else {
            Entrypoint->SubEP = NULL;
            insertEntryPoint(EP);
        }

        // Advance to the next instruction which contains an Entrypoint
        i=j;
    }
}

#if LOGGING_ENABLED
// On entry, edx is the instructions' GuestAddress
__declspec(naked) void DisplayARMBanner(void)
{
    __asm {
        mov        eax, dword ptr [LogInstructionStart]
        inc        dword ptr [InstructionCount]
        cmp     eax, 0xffffffff
// don't show register banner
        jz        NoDisplay
// show register banner
        //jge        NoDisplay
        mov        ecx, dword ptr [esp]
        call    DisplayRegisterBanner
NoDisplay:
        retn
    }
}

__declspec(naked) void DisplayThumbBanner(void)
{
    __asm {
        mov        eax, dword ptr [LogInstructionStart]
        inc        dword ptr [InstructionCount]
        cmp     eax, 0xffffffff
        jz        NoDisplay
        mov        ecx, dword ptr [esp]
        call    DisplayRegisterBanner
NoDisplay:
        retn
    }
}

#endif

unsigned __int8* PlaceConditionCheck(unsigned __int8* CodeLocation, const Decoded *d, unsigned __int8** Skip1, unsigned __int8** Skip2)
{
    bool fCNZFlagsLoaded;
    bool fAllFlagsSet;

    fCNZFlagsLoaded=false;
    fAllFlagsSet=false;

    if (d->Cond < 14 &&                                    // if the instruction is conditional...
        d != &Instructions[0] &&                        // and it isn't the first instruction...
        d->Entrypoint == (d-1)->Entrypoint &&            // and the previous instruction is within the same basic block...
        (d-1)->Cond == 14 &&                            // and the previous instruction is always executed...
        *(__int16*)(CodeLocation-6) == 0x2588 &&        // and the previous generated instruction was "MOV BYTE PTR &x86_Flags, AH"
        *(__int32*)(CodeLocation-4) == PtrToLong(&Cpu.x86_Flags)){ // then flags are already in AH;
        
            fCNZFlagsLoaded=true;                        
        
            if ((d-1)->FlagsSet == ALL_FLAGS)            //If all flags was set, then no mask was generated and 
                fAllFlagsSet=true;                        //the condition flags will still be in the EFLAGS register, hence no SAHF
                                                        //This flag can only be set if fCNZFlagsLoaded is true.
    }
    
    

    switch (d->Cond) {
    case 0:    // EQ - Z set

        if (!fCNZFlagsLoaded){
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        
        else if (!fAllFlagsSet){
            Emit8(0x9e);                                                //SAHF
        }
        
        Emit_JNZLabelFar(*Skip1);                                        // JE skipinstruction
        break;

    case 1: // NE - Z clear
        if (!fCNZFlagsLoaded){ 
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        
        else if (!fAllFlagsSet){
            Emit8(0x9e);                                                //SAHF
        }
        Emit_JZLabelFar(*Skip1);                                        // JNE skipinstruction
        break;

    case 2: // CS - C set
        if (!fCNZFlagsLoaded){         
            Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);                // BT Cpu.x86_Flags.Word, 0
        }
        
        else if (!fAllFlagsSet){
            Emit8(0x9e);                                                //SAHF
        }
        Emit_JNCLabelFar(*Skip1);                                        // JNB skipinstruction
        break;

    case 3: // CC - C clear
        if (!fCNZFlagsLoaded){
            Emit_BT_DWORDPTR_Imm(&Cpu.x86_Flags.Word, 0);                // BT Cpu.x86_Flags.Word, 0
        }
        
        else if (!fAllFlagsSet){
            Emit8(0x9e);                                                //SAHF
        }
        Emit_JCLabelFar(*Skip1);                                        // JB skipinstruction
        break;

    case 4: // MI - N set
        if (!fCNZFlagsLoaded){ 
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        
        else if (!fAllFlagsSet){
            Emit8(0x9e);                                                //SAHF
        }
        Emit_JNSLabelFar(*Skip1);                                        // JNS skipinstruction
        break;

    case 5: // PL - N clear
        if (!fCNZFlagsLoaded){ 
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        else if (!fAllFlagsSet){
            Emit8(0x9e);                                                //SAHF
        }
        Emit_JSLabelFar(*Skip1);                                        // JS skipinstruction
        break;

    case 6: // VS - V set
        
        Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Overflow.Byte);            //Mov Byte Ptr AH, &Cpu.x86_Overflow
        Emit8(0xD0); EmitModRmReg(3,AH_Reg,1);                            //ROR AH, 1
        
        Emit_JNOLabelFar(*Skip1);                                        // JNO skipinstruction
        break;

    case 7: // VC - V clear
        
        Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Overflow.Byte);            //Mov Byte Ptr AH, &Cpu.x86_Overflow
        Emit8(0xD0); EmitModRmReg(3,AH_Reg,1);                            //ROR AH, 1
        
        Emit_JOLabelFar(*Skip1);                                        // JO skipinstruction
        break;

    case 8: // HI - C set and Z clear
        if (!fCNZFlagsLoaded) {
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        else if (!fAllFlagsSet){
            Emit8(0x9e);                                                //SAHF
        }
	// brif C clear or Z set
	Emit_CMC();
	// now... brif c set or z set
	Emit_JBELabelFar(*Skip1); // JBE skipinstruction
        break;

    case 9: // LS - C clear or Z set
        if (!fCNZFlagsLoaded) {
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        else if (!fAllFlagsSet){
            Emit8(0x9e);                                                //SAHF
        }

	// brif c set and z clear
	Emit_CMC();
	// now... brif c clear and z clear
        Emit_JALabelFar(*Skip1); // JA SkipInstruction
        break;

    case 10: // GC - N set and V set, or N clear and V clear
        
        if (!fCNZFlagsLoaded) {
            Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Overflow.Byte);        //Mov Byte Ptr AH, &Cpu.x86_Overflow
            Emit8(0xD0); EmitModRmReg(3,AL_Reg,1);                        //ROR AH, 1
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        else if (!fAllFlagsSet){
            Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Overflow.Byte);        //Mov Byte Ptr AH, &Cpu.x86_Overflow
            Emit8(0xD0); EmitModRmReg(3,AL_Reg,1);                        //ROR AH, 1
            Emit8(0x9e);                                                //SAHF
        }
        Emit_JLLabelFar(*Skip1);                                        // JL skipinstruction - SF <> OF
        break;

    case 11: // LT - N set and V clear, or N clear and V set
        
        

        if (!fCNZFlagsLoaded) {
            Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Overflow.Byte);        //Mov AH, &Cpu.x86_Overflow
            Emit8(0xD0); EmitModRmReg(3,AL_Reg,1);                        //ROR AH, 1
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        
        else if (!fAllFlagsSet){
            Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Overflow.Byte);        //Mov AH, &Cpu.x86_Overflow
            Emit8(0xD0); EmitModRmReg(3,AL_Reg,1);                        //ROR AH, 1
            Emit8(0x9e);                                                //SAHF
        }
        Emit_JGELabelFar(*Skip1);                                        // JZ skipinstruction  - both are clear
        break;

    case 12: // GT - Z clear, and either N set and V set, or N clear and V clear
            
        if (!fCNZFlagsLoaded) {
            Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Overflow.Byte);        //Mov AH, &Cpu.x86_Overflow
            Emit8(0xD0); EmitModRmReg(3,AL_Reg,1);                        //ROR AH, 1
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        
        else if (!fAllFlagsSet){
            Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Overflow.Byte);        //Mov AH, &Cpu.x86_Overflow
            Emit8(0xD0); EmitModRmReg(3,AL_Reg,1);                        //ROR AH, 1
            Emit8(0x9e);                                                //SAHF
        }
        Emit_JLELabelFar(*Skip1);                                        // JLE skipinstruction - ZF = 1 or SF<>OF
        break;

    case 13: // LE - Z set, or N set and V clear, or  N clear and V set
        
        if (!fCNZFlagsLoaded) {
            Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Overflow.Byte);        //Mov AH, &Cpu.x86_Overflow
            Emit8(0xD0); EmitModRmReg(3,AL_Reg,1);                        //ROR AH, 1
            Emit_MOV_Reg_BYTEPTR(AH_Reg, &Cpu.x86_Flags.Byte);            //MOV Byte Ptr AH, cpu.x86_Flags.Byte
            Emit8(0x9e);
        }
        
        else if (!fAllFlagsSet){
            Emit_MOV_Reg_BYTEPTR(AL_Reg, &Cpu.x86_Overflow.Byte);        //Mov AH, &Cpu.x86_Overflow
            Emit8(0xD0); EmitModRmReg(3,AL_Reg,1);                        //ROR AH, 1
            Emit8(0x9e);                                                //SAHF
        }
        Emit_JGLabelFar(*Skip1);                                        // JG skipinstruction - Z = 0 and N == V
        break;

    case 14:
        ASSERT(FALSE);
        break;

    case 15:
        ASSERT(FALSE);
        break;

    default:
        ASSERT(FALSE);
        break;
    }
    return CodeLocation;
}

void PlaceEndConditionCheck(unsigned __int8* CodeLocation)
{
    while(BigSkipCount){
        BigSkipCount--;
        if (BigSkips1[BigSkipCount]) {
            FixupLabelFar(BigSkips1[BigSkipCount]);
            if (BigSkips2[BigSkipCount]) {
                FixupLabelFar(BigSkips2[BigSkipCount]);
            }
        }
    }
}

size_t JitGenerateCode(unsigned __int8 *OriginalCodeLocation, int EntrypointCount)
{
    // We can never get here with no instruction
    ASSERT( NumberOfInstructions > 0);

    unsigned __int32 i;
    unsigned __int8 *CodeLocation = OriginalCodeLocation;
    PENTRYPOINT ep;
    unsigned __int32 PreviousCond;

    ep = NULL;
    PreviousCond = 16; // start with an illegal value
    BigSkipCount = 0;
    PCCachedAddressIsValid = false;

    for (i=0; i<NumberOfInstructions; ++i) {

        if (Instructions[i].Entrypoint != ep) {
            PlaceEndConditionCheck(CodeLocation);
            PreviousCond=16;
            if (ep) {
                Decoded EndEP;
                memset(&EndEP, 0, sizeof(EndEP));
                EndEP.R15Modified=true;
                EndEP.Cond=14;
                EndEP.fp = PlaceEntrypointEnd;
                EndEP.GuestAddress=Instructions[i].GuestAddress;
                CodeLocation=PlaceEntrypointMiddle(CodeLocation, &EndEP);
                ep->nativeEnd = CodeLocation;
            }

            if (g_fOptimizeCode == false) {
                // This is placed at the beginning of the native code-gen for an instruction,
                // to poll for the single-step flag.
                CodeLocation=PlaceSingleStepCheck(CodeLocation, &Instructions[i]);
            }
            ep = Instructions[i].Entrypoint;
            ep->nativeStart = CodeLocation;
            PCCachedAddressIsValid=false;
#if ENTRYPOINT_HITCOUNTS
            Emit8(0xff); EmitModRmReg(0,5,0); EmitPtr(&ep->HitCount);    // INC ep->HitCount
#endif // ENTRYPOINT_HITCOUNTS
        }

        if(Instructions[i].Cond != PreviousCond) {
            PlaceEndConditionCheck(CodeLocation);
            PreviousCond = Instructions[i].Cond;
            if(PreviousCond < 14){
                BigSkips1[BigSkipCount] = NULL;
                BigSkips2[BigSkipCount] = NULL;
                CodeLocation = PlaceConditionCheck(CodeLocation, &Instructions[i], &BigSkips1[BigSkipCount], &BigSkips2[BigSkipCount]);
                BigSkipCount++;
            }
        } else { // Instructions[i].Cond == PreviousCond

            // In a series of instructions with the same condition code (less than 14)
            // if the previous instruction sets flags, PlaceConditionCheck is called.
            // At the end of the run of instructions, PlaceEndConditionCheck makes all the
            // BigSkips point to the end of the run.

            // for these instructions
            //  cmp
            //  orrne
            //  cmpne
            //  orrne

            // current codegen does the following:
            //  cmp
            //    if(!ne){ goto end; }
            //    orr
            //    cmp
            //    if(!ne){ goto end; }
            //    orr
            //end:

            if(PreviousCond < 14 && i>0 && Instructions[i-1].FlagsSet){
                BigSkips1[BigSkipCount] = NULL;
                BigSkips2[BigSkipCount] = NULL;
                CodeLocation = PlaceConditionCheck(CodeLocation, &Instructions[i], &BigSkips1[BigSkipCount], &BigSkips2[BigSkipCount]);
                BigSkipCount++;
            }
        }
#if LOGGING_ENABLED
        Emit_MOV_Reg_Imm32(EDX_Reg, Instructions[i].ActualGuestAddress);        // MOV EDX, Instruction's GuestAddress
        if (Cpu.CPSR.Bits.ThumbMode) {
            Emit_CALL(DisplayThumbBanner);                    // CALL DisplayThumbBanner
        } else {
            Emit_CALL(DisplayARMBanner);                    // CALL DisplayARMBanner
        }
        // prints: (FlagsNeeded) ActualGuestAddress (FlagsSet)    (Cond)    Instruction
        LogPlace((CodeLocation,"%s %s %s", FlagStrings[Instructions[i].FlagsNeeded],
            FlagStrings[Instructions[i].FlagsSet], CondStrings[Instructions[i].Cond]));
#endif
        CodeLocation = (Instructions[i].fp)(CodeLocation, &Instructions[i]);
    }

    PlaceEndConditionCheck(CodeLocation);
    Decoded EndEP;
    memset(&EndEP, 0, sizeof(EndEP));
    EndEP.R15Modified=true;
    EndEP.Cond=14;
    EndEP.fp = PlaceEntrypointEnd;
    #pragma prefast(suppress:26001, "Prefast doesn't understand that NumberOfInstructions > 0")
    EndEP.GuestAddress=Instructions[i-1].GuestAddress+((Cpu.CPSR.Bits.ThumbMode) ? 2 : 4);
    CodeLocation=PlaceEntrypointEnd(CodeLocation, &EndEP);
    if (ep) {
        ep->nativeEnd = CodeLocation;
    }

    return (size_t)CodeLocation-(size_t)OriginalCodeLocation;
}

void JitApplyFixups(void)
{
    for (unsigned __int32 i=0; i<NumberOfInstructions; ++i) {
        if (Instructions[i].JmpFixupLocation) {
            unsigned __int8* CodeLocation;
            unsigned int Offset = (Instructions[i].Reserved3 - Instructions[0].GuestAddress) / ((Cpu.CPSR.Bits.ThumbMode) ? 2 : 4);

            ASSERT(Offset < NumberOfInstructions);
            ASSERT(Instructions[Offset].GuestAddress == Instructions[i].Reserved3);
            CodeLocation = Instructions[i].JmpFixupLocation;
            Emit_JMP32(Instructions[Offset].Entrypoint->nativeStart);
        }
    }
}


PVOID JitCompile(PENTRYPOINT ContainingEntrypoint, unsigned __int32 InstructionPointer)
{
    int EntrypointCount;
    unsigned __int8 *CodeLocation;
    size_t CodeSize;
    size_t EPSize;
    size_t NativeSize;
    unsigned __int32 CachedMmuFaultStatus;
    unsigned __int32 CachedMmuFaultAddress;

    // Preserve the original MMU FaultAddress/FaultStatus values.  They'll be restored after the JIT
    // decoder and optimizer have finished using the MMU (unless the first instruction the JIT tries
    // to decode happens to trigger a prefetch abort).
    CachedMmuFaultStatus = Mmu.FaultStatus.Word;
    CachedMmuFaultAddress = Mmu.FaultAddress;

    do{
        // Disassemble instructions and fill in the Instructions[] array.
        // Depending on Cpu.CPSR.Bits.ThumbMode, this may decode either ARM
        // or Thumb opcodes.
        JitDecode(ContainingEntrypoint, InstructionPointer);

        if(!NumberOfInstructions){
            // No instructions were decoded by JitDecode
            // InstructionPointer contains address < 0xf0000000

            CpuRaiseAbortPrefetchException(InstructionPointer);

            // Lookup EP for new R15 value
            if (NativeAbortPrefetchInterruptAddress) {
                return NativeAbortPrefetchInterruptAddress;
            } else {
                ContainingEntrypoint = EPFromGuestAddrExact(Cpu.GPRs[R15]);

                if(ContainingEntrypoint){
                    NativeAbortPrefetchInterruptAddress = ContainingEntrypoint->nativeStart;
                    return NativeAbortPrefetchInterruptAddress;
                }
            }

            // EP wasn't found. update InstructionPointer and try JitDecode again.
            InstructionPointer = Cpu.GPRs[R15];

            // Before trying JitDecode again, cache the new MMU FaultStatus/FaultAddress:
            // these values will be reported to the ISR.
            CachedMmuFaultStatus = Mmu.FaultStatus.Word;
            CachedMmuFaultAddress = Mmu.FaultAddress;
        }
    }while(!NumberOfInstructions);

    // Optimize the Instructions[] array and compute the number of entrypoints
    // contained within it.
    EntrypointCount = JitOptimizeIR();

    // Allocate the entrypoints
    if (ContainingEntrypoint) {
        EPSize = EntrypointCount * sizeof(ENTRYPOINT);
    } else {
        EPSize = EntrypointCount * sizeof(EPNODE);
    }

    // Allocate space in the Translation Cache to store the jitted code
    CodeSize = 32*1024;
    CodeLocation = AllocateTranslationCache(EPSize + CodeSize);

    if (CodeLocation == NULL) {
        // The cache was full, so the AllocateTranslationCache() flushed it, and
        // returned NULL, allowing us to recompute the allocation size and retry.
        ContainingEntrypoint=NULL; // Our containing entrypoint was flushed
        EPSize = EntrypointCount * sizeof(EPNODE);
        CodeLocation = AllocateTranslationCache(EPSize + CodeSize);
        ASSERT(CodeLocation); // first allocation after a flush must always succeed
        if (CodeLocation == NULL) {
            ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
            exit(1);
        }
    }

    // Fill in the entrypoints
    JitCreateEntrypoints(ContainingEntrypoint, (size_t)CodeLocation);

    OptimizeARMFlags();

    // Generate native code
    #pragma prefast(suppress:22009, "Prefast doesn't understand that the check on EPSize + CodeSize")
    NativeSize = JitGenerateCode(CodeLocation+EPSize, EntrypointCount);

    // Apply fixups within the native code (jumps from lower-addressed
    // native code to higher-address code across ENTRYPOINTs).
    JitApplyFixups();

    // Restore the MMU FaultStatus/FaultAddress in case the JIT modified them as
    // a side-effect of its lookahead or optimizer.
    Mmu.FaultStatus.Word = CachedMmuFaultStatus;
    Mmu.FaultAddress = CachedMmuFaultAddress;

    // Give back the unused portion of the Translation Cache allocation
    FreeUnusedTranslationCache(CodeLocation + EPSize + NativeSize);

    // Flush the native instruction cache
    FlushInstructionCache(GetCurrentProcess(), CodeLocation + EPSize, NativeSize);

    return ((PENTRYPOINT)CodeLocation)->nativeStart;
}

__declspec(naked) void __fastcall RunTranslatedCode(void *NativeAddress)
{
    __asm {
        push esi
        push edi
        call ecx
        pop  edi
        pop  esi
          ret
    }
}

__declspec(noreturn) void CpuSimulate(void)
{
    BankSwitch(); // bank-switch in the privileged-mode registers if needed

    DebugEntry(Cpu.GPRs[15], etReset);

    for (;;) {
        PENTRYPOINT ep;
        PVOID NativeStart;
        unsigned __int32 IP;

        IP = Cpu.GPRs[R15];

        ep = EPFromGuestAddr(IP);
        if (ep == NULL || ep->GuestStart != MmuActualGuestAddress(IP)) {
            // No translation exists or a translation exists that contains
            // this instruction pointer but doesn't begin at this specific
            // instruction.  Compile it now.
            NativeStart = JitCompile(ep, IP);
        } else {
            NativeStart = ep->nativeStart;
        }
        RunTranslatedCode(NativeStart);
    }
}

void __fastcall CpuSetInstructionPointer(unsigned __int32 InstructionPointer)
{
    Cpu.GPRs[R15] = InstructionPointer;
}

void __fastcall CpuSetStackPointer(unsigned __int32 StackPointer)
{
    // This assumes the CPU has recently been reset and has not yet run any code
    Cpu.GPRs_svc[0] = StackPointer;
}


bool __fastcall CpuAreInterruptsEnabled()
{
    return (!Cpu.CPSR.Bits.IRQDisable || !Cpu.CPSR.Bits.FIQDisable);
}

// Note: this can be called on threads other than the CpuSimulate() thread, by the motherboard, peripheral devices, and COM interface
//       The IOLock will be held.
void __fastcall CpuSetResetPending(void)
{
    ASSERT_CRITSEC_OWNED(IOLock);

    // Simulate assertion of the CPU's RESET pin by forcibly enabling 
    // IRQ interrupt and triggering an emulator-reserved IRQ line.  
    // CpuRaiseIRQException() will special-case this interrupt and execute 
    // a CPU reset on our behalf.
    Cpu.SPSR.Bits.IRQDisable=0;
    Cpu.ResetPending=true;
    EnterCriticalSection(&InterruptLock);
    Cpu.CPSR.Bits.IRQDisable=0;
    CpuSetInterruptPending();
    LeaveCriticalSection(&InterruptLock);
}

// Note: this can be called on threads other than the CpuSimulate() thread, by the motherboard and peripheral devices
//       The IOLock will be held.
void __fastcall CpuSetInterruptPending(void)
{
    ASSERT_CRITSEC_OWNED(IOLock);

    EnterCriticalSection(&InterruptLock);
    Cpu.IRQInterruptPending = true;
    if (!Cpu.CPSR.Bits.IRQDisable) {
        EnableInterruptOnPoll();
    }
    LeaveCriticalSection(&InterruptLock);

    // Raise the event outside of the InterruptLock.  Otherwise, if the interrupt
    // is raised by a worker thread and the CPU is blocked on hIdleEvent, waking
    // the CPU will trigger contention on the InterruptLock when CpuRaiseIRQInterrupt()
    // attempts to disable interrupts by writing to Cpu.CPSR.Bits.IRQDisable.
    SetEvent(hIdleEvent);
}

// Note: this can be called on threads other than the CpuSimulate() thread, by the motherboard and peripheral devices
//       The IOLock will be held.
void __fastcall CpuClearInterruptPending(void)
{
    ASSERT_CRITSEC_OWNED(IOLock);

    EnterCriticalSection(&InterruptLock);
    Cpu.IRQInterruptPending = false;
    DisableInterruptOnPoll();
    LeaveCriticalSection(&InterruptLock);
}

void __fastcall CpuFlushCachedTranslations(void)
{
    StackCount=0;

    NativeIRQInterruptAddress=0;
    NativeSoftwareInterruptAddress=0;
    NativeAbortDataInterruptAddress=0;
    NativeAbortPrefetchInterruptAddress=0;
    NativeUndefinedInterruptAddress=0;
}
