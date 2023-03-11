/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef _ARMCPU_H_INCLUDED
#define _ARMCPU_H_INCLUDED

typedef union _PSR {
	struct _PSR_Bits {
		unsigned __int32 Mode:5;
		unsigned __int32 ThumbMode:1;
		unsigned __int32 FIQDisable:1;
		unsigned __int32 IRQDisable:1;
		unsigned __int32 Reserved2:19;
		unsigned __int32 SaturateFlag:1;		
		unsigned __int32 Unused:4;
	} Bits;
	unsigned __int32 Partial_Word;
} PSR;

typedef union _PSR_FULL {
	struct _PSR_Bits {
		unsigned __int32 Mode:5;
		unsigned __int32 ThumbMode:1;
		unsigned __int32 FIQDisable:1;
		unsigned __int32 IRQDisable:1;
		unsigned __int32 Reserved2:19;
		unsigned __int32 SaturateFlag:1;		
		unsigned __int32 OverflowFlag:1;
		unsigned __int32 CarryFlag:1;
		unsigned __int32 ZeroFlag:1;
		unsigned __int32 NegativeFlag:1;
	} Bits;
	unsigned __int32 Word;
} PSR_FULL;

typedef union _X86FLAGS {
	struct _Flag_Bits {
		unsigned __int32 CarryFlag:1;
		unsigned __int32 Unused1:1;
		unsigned __int32 ParityFlag:1;
		unsigned __int32 Unused2:1;
		unsigned __int32 AuxiliaryFlag:1;
		unsigned __int32 Unused3:1;		
		unsigned __int32 ZeroFlag:1;
		unsigned __int32 SignFlag:1;
		unsigned __int32 Unused4:24;
	} Bits;
	unsigned __int8  Byte;
	unsigned __int32 Word;
} X86FLAGS;

typedef union _X86OVERFLOW {
	unsigned __int8  Byte;
	unsigned __int32 Word;
} X86OVERFLOW;

#define PSR_NEGATIVE_FLAG	31
#define PSR_ZERO_FLAG		30
#define PSR_CARRY_FLAG		29
#define PSR_OVERFLOW_FLAG	28
#define PSR_SATURATE_FLAG	27

typedef enum _GPRNames {
	R0=0,
	R1=1,
	R2=2,
	R3=3,
	R4=4,
	R5=5,
	R6=6,
	R7=7,
	R8=8,
	R9=9,
	R10=10,
	R11=11,
	R12=12,
	R13=13,
	R14=14,
	R15=15,

	R13_svc=0,
	R14_svc=1,

	R13_abt=0,
	R14_abt=1,

	R13_irq=0,
	R14_irq=1,

	R13_und=0,
	R14_und=1
} GPRNames;

// This enum is the PSR.Mode value
typedef enum _PrivModeValue {
	UserModeValue = 16,
	FIQModeValue = 17,
	IRQModeValue = 18,
	SupervisorModeValue = 19,
	AbortModeValue = 23,
	UndefinedModeValue = 27,
	SystemModeValue = 31
} PRIVMODE_VALUE;

// Values for Cpu.DebuggerInterruptPending
#define DEBUGGER_INTERRUPT_BREAKPOINT   1
#define DEBUGGER_INTERRUPT_BREAKIN      2

typedef struct _CPU {
	unsigned __int32 GPRs[16];	// R0...R15
	PSR		         CPSR;      // Current Program Status Register
	PSR_FULL	     SPSR;      // Saved PSR with flag bits

	
	X86FLAGS x86_Flags;
	X86OVERFLOW x86_Overflow;
	// Banked registers for Supervisor mode
	unsigned __int32 GPRs_svc[2];
	unsigned __int32 SPSR_svc;

	// Banked registers for Abort mode
	unsigned __int32 GPRs_abt[2];
	unsigned __int32 SPSR_abt;

	// Banked registers for IRQ mode
	unsigned __int32 GPRs_irq[2];
	unsigned __int32 SPSR_irq;

	// Banked registers for Undefined mode
	unsigned __int32 GPRs_und[2];
	unsigned __int32 SPSR_und;

	// Emulator-specific data begins here
	unsigned __int32 IRQInterruptPending; // true if an IRQ is pending
	unsigned __int32 ResetPending;        // true if the watchdog timer has asserted the RESET pin on the CPU
    unsigned __int32 DebuggerInterruptPending; // Guarded by InterruptLock.  Nonzero if the debugger wishes to interrupt
} CPU, *PCPU;

extern CPU Cpu;

PVOID CpuRaiseAbortPrefetchException(unsigned __int32 InstructionPointer);
PVOID CpuRaiseAbortDataException(unsigned __int32 InstructionPointer);
PVOID CpuRaiseUndefinedException(unsigned __int32 InstructionPointer);
PVOID CpuRaiseIRQException(unsigned __int32 InstructionPointer);
PVOID CpuRaiseUndefinedInstructionException(unsigned __int32 InstructionPointer);

extern PVOID NativeUndefinedInterruptAddress;
extern PVOID NativeSoftwareInterruptAddress;
extern PVOID NativeAbortPrefetchInterruptAddress;
extern PVOID NativeAbortDataInterruptAddress;
extern PVOID NativeIRQInterruptAddress;

extern unsigned __int32 GetCPSRWithFlags();
extern void UpdateCPSRWithFlags(PSR_FULL NewPSR); 


typedef struct _Decoded{	
	
	unsigned __int8* (*fp)(unsigned __int8*, struct _Decoded*); // function pointer to Place*(CodeLocation, Decoded) function
	unsigned __int32 GuestAddress;  // Virtual address of the instruction, without an explicit ProcessID
	unsigned __int32 ActualGuestAddress; // Virtual address of the instruction, including an explicit ProcessID

	unsigned __int32 Operand2;

	// Registers
	unsigned __int32 Rd:4;
	unsigned __int32 Rn:4;
	unsigned __int32 Rm:4;
	unsigned __int32 Rs:4;
	unsigned __int16 RegisterList;

	// coprocs
	unsigned __int32 CP:3;
	unsigned __int32 CRd:4;
	unsigned __int32 CRm:4;
	unsigned __int32 CRn:4;
	unsigned __int32 CPNum:4;
	unsigned __int32 CPOpc:4;

	// Offset is signed. It can take on a negative value at compile time
	__int32 Offset;

	unsigned __int32 Immediate;

	// its reserved, but used by PerformControlExtension and PerformLoadStoreExtension
	unsigned __int32 Reserved3;

	unsigned __int32 B:1;
	unsigned __int32 Cond:4;
	unsigned __int32 H:1;
	unsigned __int32 I:1;
	unsigned __int32 L:1;
	unsigned __int32 N:1;
	unsigned __int32 Op1:3;
	unsigned __int32 Opcode:4;
	unsigned __int32 P:1;
	unsigned __int32 S:1;
	unsigned __int32 U:1;
	unsigned __int32 W:1;
	unsigned __int32 X1:4;		
	unsigned __int32 X2:12;
	unsigned __int32 R15Modified:1;	// set if the instruction is known to modify R15

    // Bits used by DSP instructions
	unsigned __int32 X:1;
	unsigned __int32 Y:1;

	unsigned __int8 FlagsNeeded:4;
	unsigned __int8 FlagsSet:4;

	//thumb specific. used by instructions for which there are no equivalent ARM instructions
	unsigned __int32 H2:1;
	unsigned __int32 HTwoBits:2;
	unsigned __int32 RsHs:3;
	unsigned __int32 Word8:8;

	PENTRYPOINT Entrypoint;
	unsigned __int8 *JmpFixupLocation;
} Decoded;

// The flags are defined in order matching the CPSR register, so that they can be
// use to mask off the corresponding bits
#define FLAG_NF     (1<<3)          // sign (neg)
#define FLAG_ZF     (1<<2)          // zero
#define FLAG_CF     (1<<1)          // carry
#define FLAG_VF     (1<<0)         // overflow
#define ALL_FLAGS (FLAG_NF|FLAG_ZF|FLAG_CF|FLAG_VF)
#define NO_FLAGS 0

#define X86_FLAG_OF     (1<<0)          // OverFlow
#define X86_FLAG_NF     (1<<7)          // sign (neg)
#define X86_FLAG_ZF     (1<<6)          // zero
#define X86_FLAG_CF     (1<<0)          // carry

extern HANDLE g_hDebuggerEvent;
extern bool g_fOptimizeCode;

void UpdateInterruptOnPoll(void);
extern CRITICAL_SECTION InterruptLock;

#endif //!_ARMCPU_H_INCLUDED
