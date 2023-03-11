/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "emulator.h"
#include "state.h"
#include "Board.h"
#include "cpu.h"
#include "entrypt.h"
#include "ARMCpu.h"
#include "mmu.h"
#include "tc.h"
#include "place.h"
#include "resource.h"

MMU Mmu;

// http://intel.forums.liveworld.com/thread.jsp?forum=160&thread=5428 describes some
// of the PTE layout.  The first level contains 4096 pointers to 2nd level
// tables or to a 1MB page (section).  2nd level table can be either a) a course
// page with 256 pointers to large, extended small or small pages, B) fine page
// with 1024 pointers to large, small, or tiny pages.
//
// What is a bit tricky is that there can be only 16 large pages in the course page 
// table [and 256 small /extended small pages] - large pages require duplication of 
// sequential entries in the table. The same is true for the fine page table - 16 
// large pages, 256 small pages and 1024 tiny pages. Both small and large will have 
// duplicated entries.

// Also see ddi0184a_922t_trm.pdf section 3.3.1 for a detailed walkthrough.


typedef struct FaultPTEFormat_Tag {
	unsigned __int32 PTEType:2;  // must be 0
	unsigned __int32 SBZ:30;     // should be 0
} FaultPTEFormat;

typedef struct ReservedPTEFormat_Tag {
	unsigned __int32 PTEType:2;  // must be 3
	unsigned __int32 SBZ:30;     // should be 0
} ReservedPTEFormat;

typedef union L1PTEFormat_Tag {
	unsigned __int32 Word;

	FaultPTEFormat FaultPTE;

	struct {
		unsigned __int32 PTEType:2; // must be 1
		unsigned __int32 Reserved1:3; // Should be 0
		unsigned __int32 Domain:4;
		unsigned __int32 P:1;
		unsigned __int32 PageTableBaseAddress:22;
	} CoarseGrainedPTE;

	struct {
		unsigned __int32 PTEType:2; // must be 2
		unsigned __int32 B:1;       // write buffering
		unsigned __int32 C:1;       // caching
		unsigned __int32 Reserved1:1; // Should be 0
		unsigned __int32 Domain:4;
		unsigned __int32 P:1;
		unsigned __int32 AP:2;
		unsigned __int32 TEX:4;
		unsigned __int32 Reserved2:4; // Should be 0
		unsigned __int32 SectionBaseAddress:12;
	} SectionPTE; // 1-meg sections

	struct {
		unsigned __int32 PTEType:2; // must be 1
		unsigned __int32 Reserved1:3; // Should be 0
		unsigned __int32 Domain:4;
		unsigned __int32 P:1;
		unsigned __int32 Reserved2:2; // Should be 0
		unsigned __int32 PageTableBaseAddress:18;
	} FineGrainedPTE;
	
} L1PTEFormat;

typedef union L2PTEFormat_Tag {
	unsigned __int32 Word;

	FaultPTEFormat FaultPTE;

	struct {
		unsigned __int32 PTEType:2; // must be 1
		unsigned __int32 B:1;       // write buffering
		unsigned __int32 C:1;	    // caching
		unsigned __int32 APs:8;     // AP0/AP1/AP2/AP3
		unsigned __int32 TEX:4;
		unsigned __int32 LargePageBaseAddress:16;
	} LargePagePTE; // 64k page

	struct {
		unsigned __int32 PTEType:2; // must be 2
		unsigned __int32 B:1;       // write buffering
		unsigned __int32 C:1;       // caching
		unsigned __int32 APs:8;     // AP0/AP1/AP2/AP3
		unsigned __int32 SmallPageBaseAddress:20;
	} SmallPagePTE; // 4k page

	struct {
		unsigned __int32 PTEType:2; // must be 2
		unsigned __int32 B:1;       // write buffering
		unsigned __int32 C:1;       // caching
		unsigned __int32 AP:2;
		unsigned __int32 TEX:4;
		unsigned __int32 ExtendedSmallPageBaseAddress:22;
	} ExtendedSmallPagePTE; // 1k page

} L2PTEFormat;

// this assumes that DomainAccessControl is set to 1 by WinCE
#define CHECK_DOMAIN(Domain, FaultStatus)							\
	if (Domain != 0) {												\
		Mmu.RaiseAbortException(p, (FaultStatus));					\
		BoardIOAddress=0;											\
		return 0;													\
	}

// This assumes that S==0 and R==1
#define CHECK_AP(PageAP, FaultStatus) {									\
if (TAccessType == MMU::ReadAccess || TAccessType == MMU::ExecuteAccess) { \
	if (Cpu.CPSR.Bits.Mode == UserModeValue) {							\
		if ((PageAP) == 1) {												\
			Mmu.RaiseAbortException(p, (FaultStatus));					\
			BoardIOAddress=0;											\
			return 0;													\
		}																\
	}																	\
} else if (TAccessType == MMU::ReadWriteAccess || TAccessType == MMU::WriteAccess) { \
	if (Cpu.CPSR.Bits.Mode == UserModeValue) {							\
		if ((PageAP) != 3) {												\
			Mmu.RaiseAbortException(p, (FaultStatus));					\
			BoardIOAddress=0;											\
			return 0;													\
		}																\
	} else {															\
		if ((PageAP) == 0) {												\
			Mmu.RaiseAbortException(p, (FaultStatus));					\
			BoardIOAddress=0;											\
			return 0;													\
		}																\
	}																	\
}                                                                       \
}


void __fastcall MMU::Initialize(void)
{
	Mmu.FlushAllTLBs(&Mmu.DataTLB);
	Mmu.FlushAllTLBs(&Mmu.InstructionTLB);
	// Initalize the TranslationBase to a valid value because we optimized away
	// the range checks in the MMU accesses, requiring the TranslationBase to always
	// point at a valid RAM address at least 8kb away from edge.
	Mmu.TranslationTableBase.Word = BoardGetPhysicalRAMBase();
}

unsigned __int8* __fastcall MMU::PlaceCoprocDataTransfer(unsigned __int8* CodeLocation, Decoded* d)
{
	Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
	Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
	Emit_RETN(0);											// RETN

	return CodeLocation;
}

void __fastcall MMU::FlushAllTLBs(TLBUnit *pTLBUnit)
{
	int i;

	for (i=0; i<MAX_TLB_REGISTERS; ++i) {
		pTLBUnit->TLBRegisters[i].Valid = false;
		pTLBUnit->TLBRegisters[i].PTE=3;	// smallpagepte
	}
	pTLBUnit->NextFreeTLB=0; // this isn't necessary, but makes logging and debugging easier
}

void __fastcall MMU::FlushOneTLB(TLBUnit *pTLBUnit, unsigned __int32 p)
{
	int i;

	// Fold in the process ID as appropriate
	if ((p & 0xfe000000) == 0) {
		p |= Mmu.ProcessId;
	}

	for (i=0; i<MAX_TLB_REGISTERS; ++i) {
		if (pTLBUnit->TLBRegisters[i].Valid) {
			L2PTEFormat L2PTE;

			L2PTE.Word = pTLBUnit->TLBRegisters[i].PTE;
			switch (L2PTE.FaultPTE.PTEType) {
			case 0:
				// 0 is normally invalid.  Here, it is used for a Section L1PTE
				if ((p >> 20) == pTLBUnit->TLBRegisters[i].VirtualAddress) {
					goto TLBHit;
				}
				break;

			case 1: // LargePagePTE
				if ((p >> 16) == pTLBUnit->TLBRegisters[i].VirtualAddress) {
					goto TLBHit;
				}
				break;

			case 2: // SmallPagePTE
				if ((p >> 12) == pTLBUnit->TLBRegisters[i].VirtualAddress) {
					goto TLBHit;
				}
				break;

			case 3: // ExtendedSmallPagePTE (aka TinyPTE)
				if ((p >> 10) == pTLBUnit->TLBRegisters[i].VirtualAddress) {
					goto TLBHit;
				}
				break;

			default:
				ASSERT(FALSE);
				break;
			}
		}
	}
	return;

TLBHit:
	pTLBUnit->TLBRegisters[i].Valid = false;
	pTLBUnit->TLBRegisters[i].PTE = 3;	// smallpagepte
}


__declspec(naked) void MMU::SetControlRegisterHelper(void)
{
	__asm {
		// ECX/EDX are already set up for this call
		call	MMU::SetControlRegisterHelper2
		test	eax,eax
		jnz     Done			// branch if eax is nonzero - the Translation Cache wasn't flushed
		pop		eax				// the cache was flushed - return to C/C++ code by popping the TC return address from the stack
Done:
		retn
	}
}

int MMU::SetControlRegisterHelper2(unsigned __int32 NewValue, unsigned __int32 GuestAddress)
{
	ControlRegisterType Reg;

	Reg.Word = NewValue;
	if (Reg.Bits.Reserved2 != 0 ||
		(Reg.Bits.Reserved3 != 0xc000 && Reg.Bits.Reserved3 != 0)
	) {
		CpuRaiseUndefinedException(GuestAddress);
		return 0;
	}

	// Santize the value
	Reg.Bits.C=1; // Unified cache is always enabled
	Reg.Bits.W=1; // Write buffer is always enabled
	Reg.Bits.P=1; // 26-bit exception handlers not supported
	Reg.Bits.D=1; // 26-bit address checking not suported
	Reg.Bits.L=1; // Late abort model only
	Reg.Bits.Z=1; // Branch prediction always enabled
	Reg.Bits.I=1; // Instruction cache always enabled
	Reg.Bits.B=0; // Don't support big-endian
	Reg.Bits.RR=0;// Always normal replacement strategy
	Reg.Bits.L4=0;// Always support Thumb mode

	if (Reg.Bits.M && !Mmu.ControlRegister.Bits.M) {
		//ASSERT(Reg.Bits.R==1);  // MMU protection checking assumes that the ROM control bit is set
		ASSERT(Reg.Bits.S==0);  // MMU protection checking assumes that the System control bit is clear
		// Enabling the MMU.  Lie a little here:  WinCE expects the
		// next instruction, "mov pc, r0" to be prefetched already,
		// but doesn't have a PTE mapping it.  In the interpreter,
		// the attempt to look its address up fails and we crash.
		// "Fix" it here by creating a ITLB which contains the
		// next instruction's address.
		L1PTEFormat SectionPTE;
		unsigned __int32 VirtualAddress;
		unsigned __int32 ActualGuestAddress = ((GuestAddress & 0xfe000000) == 0)  ? 
											   (GuestAddress | Mmu.ProcessId) : GuestAddress;

		SectionPTE.Word=0;
		SectionPTE.SectionPTE.PTEType=0; // special meaning to the TLB unit - a section PTE
		SectionPTE.SectionPTE.SectionBaseAddress=(ActualGuestAddress+4) >> 20;

		Mmu.InstructionTLB.TLBRegisters[0].Valid=1;
		Mmu.InstructionTLB.TLBRegisters[0].VirtualAddress=(ActualGuestAddress+4) >> 20;
		Mmu.InstructionTLB.TLBRegisters[0].PTE = SectionPTE.Word;
		VirtualAddress = Mmu.InstructionTLB.TLBRegisters[0].VirtualAddress << 20;
		// We don't want the process ID in the address when we call physical to host
		Mmu.InstructionTLB.TLBRegisters[0].HostAdjust = Mmu.MapGuestPhysicalToHost((GuestAddress+4) & 0xfff00000)-VirtualAddress;

		// Flush the translation cache:  code-gen up to this point contains guest physical addresses
		// and all code now must switch to contain guest virtual addresses.
		FlushTranslationCache(0, 0xffffffff);
		Mmu.ControlRegister.Word = Reg.Word;
		Cpu.GPRs[R15] = GuestAddress+4;
		return 0; // report that the cache was flushed
	}

	// A note about disabling the MMU:  On SMDK2410,
	// CPUPowerReset() calls ARMFlushDCache() after the MMU has been
	// disabled.  However, when the call happens, R13 still contains a virtual address.
	// When the MMU is disabled, the stack pointer reference appears to point into IO space
	// (it is ffffc78c) and the "STM" instruction in ARMFlushDCache() ends up writing into
	// IO space.  It turns out that the values stored and loaded don't matter, so it's OK
	// for the emulator to fall into its "unknown IO address" code.

	Mmu.ControlRegister.Word = Reg.Word;
	return 1;
}

__declspec(naked) void MMU::FlushTranslationCacheHelper(void)
{
	__asm {
		call FlushTranslationCache // call FlushTranslationCache(0, 0xffffffff)
		retn			// return to C/C++ code
	}
}

unsigned __int8* MMU::PlaceCoprocRegisterTransfer(unsigned __int8* CodeLocation, Decoded* d)
{
	switch (d->CRn) {
	case 0: // Register 0 - ID or Cache Type Register
		if (d->L) {
			if (d->CP == 0) {
				// See 278796-001.pdf Table 7-4.  This reports
				//  Intel(R) Corporation
				//  ARM Architecture 5TE
				//  ARM920 Core
				//  First version of the code
				//  A0 stepping

				// MOV Cpu.GPRs[Rd], 0x69059201
				Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[d->Rd]); Emit32(0x69059201); 
			} else if (d->CP == 1) {
				// See 278796-001.pdf Table 7-5
				// MOV Cpu.GPRs[Rd], 0x0b172172
				Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[d->Rd]); Emit32(0x0b172172); 
			} else {
				Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);		// MOV ECX, GuestAddress
				Emit_CALL(CpuRaiseUndefinedException);				// CALL CpuRaiseUndefinedException
				Emit_RETN(0);										// RETN
			}
		} else {
			// Writes are ignored for CP=0 and CP=1
			if (d->CP > 1) {
				Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);		// MOV ECX, GuestAddress
				Emit_CALL(CpuRaiseUndefinedException);				// CALL CpuRaiseUndefinedException
				Emit_RETN(0);										// RET back to C/C++ code
			}
		}
		break;

	case 1: // Register 1 - ARM Control or Auxiliary Control register
		if (d->L) {
			if (d->CP == 0) {
				Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Mmu.ControlRegister); // MOV EAX, Mmu.ControlRegister
				Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);	// MOV Cpu.GPRs[d->Rd], EAX
			} else if (d->CP == 1) {
				Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Mmu.AuxControlRegister); // MOV EAX, Mmu.AuxControlRegister
				Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);	// MOV Cpu.GPRs[d->Rd], EAX
			} else {
				Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);		// MOV ECX, GuestAddress
				Emit_CALL(CpuRaiseUndefinedException);				// CALL CpuRaiseUndefinedException
				Emit_RETN(0);										// RETN
				break;
			}
		} else {
			if (d->CP == 0) {
				Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rd]);	// MOV ECX, Cpu.GPRs[Rd]	- ECX = new control register value
				Emit_MOV_Reg_Imm32(EDX_Reg, d->GuestAddress);		// MOV EDX, GuestAddress
				Emit_CALL(MMU::SetControlRegisterHelper);			// CALL MMU::SetControlRegisterHelper
			} else if (d->CP == 1) {
				unsigned __int8* RaiseException;
				unsigned __int8* NoException;

				Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rd]);	// MOV EAX, Cpu.GPRs[Rd]
				Emit8(0xa9); Emit32(0xffffffcc);					// TEST EAX, const - Reg.Bits.Reserved1 != 0 || Reg.Bits.Reserved2 != 0
				Emit_JNZLabel(RaiseException);						// JNZ RaiseException
				Emit8(0xa9); Emit32(2);								// TEST EAX, 2 - Reg.Bits.P
				Emit_JNZLabel(NoException);							// JNZ NoException
				FixupLabel(RaiseException);							// RaiseException:
				Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);		// MOV ECX, GuestAddress
				Emit_CALL(CpuRaiseUndefinedException);				// CALL CpuRaiseUndefinedException
				Emit_RETN(0);										// RETN

				FixupLabel(NoException);							// NoException:
				Emit_MOV_DWORDPTR_Reg(&Mmu.AuxControlRegister, EAX_Reg); // MOV Mmu.AuxControlRegister, EAX
			} else {
				Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);		// MOV ECX, GuestAddress
				Emit_CALL(CpuRaiseUndefinedException);				// CALL CpuRaiseUndefinedException
				Emit_RETN(0);										// RETN
			}
		}
		break;
	case 2: // Register 2 - Translation Table Base
		if (d->L) {
			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Mmu.TranslationTableBase); // MOV EAX, Mmu.TranslationTableBase
			Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);		// MOV Cpu.GPRs[d->Rd], EAX
		} else {
			unsigned __int8* NoException;
			unsigned __int8* RaiseException1;

			Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rd]);		// MOV ECX, Cpu.GPRs[Rd]
			Emit_MOV_Reg_Reg(EAX_Reg, ECX_Reg);				// MOV EAX, ECX
			Emit_AND_Reg_Imm32(EAX_Reg, 0x00003fff);			// AND EAX, 0x3fff - check the TranslationTableBaseRegisterType.Bits.Reserved1 value
			Emit_JNZLabel(RaiseException1);					// JNZ RaiseException
			Emit_CALL(BoardMapGuestPhysicalToHostRAM);			// CALL BoardMapGuestPhysicalToHostRAM
			Emit_TEST_Reg_Reg(EAX_Reg, EAX_Reg);				// TEST EAX, EAX
			Emit_JNZLabel(NoException);					// JNZ NoException - no exception:  the address is within host RAM

			FixupLabel(RaiseException1);					// RaiseException:
			Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
			Emit_CALL(CpuRaiseUndefinedException);				// CALL CpuRaiseUndefinedException
			Emit_RETN(0);							// RETN

			FixupLabel(NoException);					// NoException:
			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rd]);		// MOV EAX, Cpu.GPRs[Rd]
			Emit_MOV_DWORDPTR_Reg(&Mmu.TranslationTableBase, EAX_Reg);	// MOV Mmu.TranslationTableBase, EAX
		}
		break;

	case 3: // Register 3 - Domain Access Control
		if (d->L) {
			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Mmu.DomainAccessControl); // MOV EAX, Mmu.DomainAccessControl
			Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);		// MOV Cpu.GPRs[d->Rd], EAX
		} else {
			unsigned __int8* NoException;

			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rd]);		// MOV EAX, Cpu.GPRs[Rd]
			// set to 1 during startup: domain0 is "Client" (accesses are checked
			// against the permission bits in the section/page descriptor).  All
			// other domains are "No access".  Because of this, the MMU needs only
			// confirm that the domain value is 0 or 1 in PTEs and doens't need to
			// go through the expense of checking the DomainAccessControl value.
			Emit_CMP_Reg_Imm32(EAX_Reg, 1);							// CMP EAX, 1
			Emit_JZLabel(NoException);								// JZ NoException
			Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
			Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
			Emit_RETN(0);											// RETN

			FixupLabel(NoException);								// NoException:
			Emit_MOV_DWORDPTR_Reg(&Mmu.DomainAccessControl, EAX_Reg); // MOV Mmu.DomainAccessControl, EAX
		}
		break;

	case 4: // Register 4 - reserved
		Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
		Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
		Emit_RETN(0);											// RETN
		break;

	case 5: // Register 5 - Fault Status Register
		if (d->L) {
			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Mmu.FaultStatus);		// MOV EAX, Mmu.FaultStatus
			Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);		// MOV Cpu.GPRs[d->Rd], EAX
		} else {
			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rd]);		// MOV EAX, Cpu.GPRs[Rd]
			Emit_MOV_DWORDPTR_Reg(&Mmu.FaultStatus, EAX_Reg);		// MOV Mmu.FaultStatus, EAX
		}
		break;

	case 6: // Register 6 - Fault Address Register
		if (d->L) {
			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Mmu.FaultAddress);		// MOV EAX, Mmu.FaultAddress
			Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);		// MOV Cpu.GPRs[d->Rd], EAX
		} else {
			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rd]);		// MOV EAX, Cpu.GPRs[Rd]
			Emit_MOV_DWORDPTR_Reg(&Mmu.FaultAddress, EAX_Reg);		// MOV Mmu.FaultAddress, EAX
		}
		break;

	case 7: // Register 7 - Cache operations, Write-only
		if (d->L) {
			Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
			Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
			Emit_RETN(0);											// RETN
		} else {
			switch (d->CPOpc) {
			case 0:
				switch (d->CRm) {
				case 0:	
					// CP==4: wait for interrupt
                    // CP==others: undefined
					break;

                case 5:
                    switch (d->CP) {
                    case 0: // Invalidate entire instruction cache
					    Emit_XOR_Reg_Reg(ECX_Reg, ECX_Reg);			// XOR ECX, ECX
					    Emit_MOV_Reg_Imm32(EDX_Reg, 0xffffffff);	// MOV EDX, -1
					    Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[R15]); Emit32(d->GuestAddress+4); // MOV Cpu.GPRs[R15], GuestAddress+4
					    Emit_JMP32(MMU::FlushTranslationCacheHelper);	// FlushTranslationCache(0, -1) and return to C/C++ code
					    break;
                    case 1: // Invalidate instruction cache line (Rd contains virtual address)
						if( d->Operand2 == 0xffffffff){
							//Optimized Partial flush code path.  See JitOptimizeIR
							Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rd]);	// MOV ECX, Cpu.GPRs[Rd]
							//The length of the flush is stored in R1
							Emit_XOR_Reg_Reg(EDX_Reg, EDX_Reg);			// XOR EDX, EDX
							Emit8(0x87); EmitModRmReg(0,5,EDX_Reg); EmitPtr(&Cpu.GPRs[R1]); // XCHG EDX, DWORD PTR &Cpu.GPRs[R1] (so EDX=R1, then R1=0)
							Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[R15]); Emit32(d->GuestAddress+0x10); // MOV Cpu.GPRs[R15], GuestAddress+0x10 (so execution resumes at the 'ret' instr)
							Emit_JMP32(MMU::FlushTranslationCacheHelper);	// FlushTranslationCache(Rd, R1) and return to C/C++ code
						}else {
							Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rd]);	// MOV ECX, Cpu.GPRs[Rd]
							Emit_MOV_Reg_Imm32(EDX_Reg, 32);			// MOV EDX, 32 (cache line size)
							Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[R15]); Emit32(d->GuestAddress+4); // MOV Cpu.GPRs[R15], GuestAddress+4
							Emit_JMP32(MMU::FlushTranslationCacheHelper);	// FlushTranslationCache(Rd, 32) and return to C/C++ code
						}
						break;
                    case 2: // Invalidate instruction cache line (Rd contains set/index)
                    	TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
                        break;
                    case 4: // Flush Prefetch Buffer
                        break;
                    case 6: // Flush Entire Branch Target Cache
                        break;
                    case 7: // Flush Branch Target Cache Entry
                        break;
                    default:
	                TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
                        break;
                    }
                    break;
                
                case 6:
                    switch (d->CP) {
                    case 0: // Flush entire data cache
                    case 1: // Invalidate data cache line (Rd contians virtual address)
                    case 2: // Invalidate data cache line (Rd contains set/index)
                    case 4:
                        break;
                    default:
                    	TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
                        break;
                    }
                    break;

                case 7:
                    switch (d->CP) { 
                    case 0: // Invalidate entire unified cache or both instruction and data caches
					    Emit_XOR_Reg_Reg(ECX_Reg, ECX_Reg);			// XOR ECX, ECX
					    Emit_MOV_Reg_Imm32(EDX_Reg, 0xffffffff);	// MOV EDX, -1
					    Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[R15]); Emit32(d->GuestAddress+4); // MOV Cpu.GPRs[R15], GuestAddress+4
					    Emit_JMP32(MMU::FlushTranslationCacheHelper);	// FlushTranslationCache(0, -1) and return to C/C++ code
					    break;
                    case 1: // Invalidate unified cache line (Rd contains virtual address)
					    Emit_MOV_Reg_DWORDPTR(ECX_Reg, &Cpu.GPRs[d->Rd]);	// MOV ECX, Cpu.GPRs[Rd]
					    Emit_MOV_Reg_Imm32(EDX_Reg, 32);			// MOV EDX, 32 (cache line size)
					    Emit8(0xc7); EmitModRmReg(0,5,0); EmitPtr(&Cpu.GPRs[R15]); Emit32(d->GuestAddress+4); // MOV Cpu.GPRs[R15], GuestAddress+4
					    Emit_JMP32(MMU::FlushTranslationCacheHelper);	// FlushTranslationCache(Rd, 32) and return to C/C++ code
                        break;
                    case 2: // Invalidate unified cache line (Rd contains set/index)
                    default:
	                TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
                        break;
                    }
                    break;
				case 8:
					// wait for interrupt
					break;
				case 10:
					// clean data cache line
					break;
				case 11:
					// clean unified cache line
					break;
				case 13:
					// prefetch instruction cache line
					break;
				case 14:
					// Clean and invalidate Cache Line
					break;
				case 15:
					// clean and invalidate unified cache line
					break;
				default:
					ASSERT(FALSE); // illegal d
				}
				break;
			case 1:
				switch (d->CRm) {
				case 6:
					// Flush D-Cache entry, Data (Rd) is the virtual address to flush
					break;
				case 10:
					// Clean D-Cache entry, Data (Rd) is the virtual address to flush
					break;
				default:
					Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
					Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
					Emit_RETN(0);											// RETN
					break;
				}
				break;
			case 4:
				if (d->CRm == 10) {
					// Drain write buffer, Data ignored
				} else {
					Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
					Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
					Emit_RETN(0);											// RETN
				}
				break;
			case 6:
				if (d->CRm == 5) {
					// Invalidate Branch Target Buffer, Data ignored
				} else {
					Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
					Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
					Emit_RETN(0);											// RETN
				}
				break;

			default: // CPOpc
				Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
				Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
				Emit_RETN(0);											// RETN
				break;
			}
		}
		break;

	case 8:  // Register 8 - TLB management
		if (d->L) {
			Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
			Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
			Emit_RETN(0);											// RETN
		} else {
			switch (d->CPOpc) {
			case 0:
				switch (d->CRm) {
				case 7:
					// Flush I+D, Data ignored
					Emit_MOV_Reg_Imm32(ECX_Reg, PtrToLong(&Mmu.DataTLB));	// MOV ECX, Mmu.DataTLB
					Emit_CALL(FlushAllTLBs);								// CALL FlushAllTLBs
					Emit_MOV_Reg_Imm32(ECX_Reg, PtrToLong(&Mmu.InstructionTLB)); // MOV ECX, Mmu.InstructionTLB
					Emit_CALL(FlushAllTLBs);								// CALL FlushAllTLBs
					break;
				case 5:
					// Flush I, Data ignored
					Emit_MOV_Reg_Imm32(ECX_Reg, PtrToLong(&Mmu.InstructionTLB)); // MOV ECX, Mmu.InstructionTLB
					Emit_CALL(FlushAllTLBs);								// CALL FlushAllTLBs
					break;
				case 6:
					// Flush D, Data ignored
					Emit_MOV_Reg_Imm32(ECX_Reg, PtrToLong(&Mmu.DataTLB));	// MOV ECX, Mmu.DataTLB
					Emit_CALL(FlushAllTLBs);								// CALL FlushAllTLBs
					break;
				default:
					Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
					Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
					Emit_RETN(0);											// RETN
					break;
				}
				break;

			case 1:
				if (d->CRm == 5) {
					// Flush ITLB entry, Rd is the address
					Emit_MOV_Reg_DWORDPTR(EDX_Reg, &Cpu.GPRs[d->Rd]);		// MOV EDX, Cpu.GPRs[d->Rd]
					Emit_MOV_Reg_Imm32(ECX_Reg, PtrToLong(&Mmu.InstructionTLB)); // MOV ECX, Mmu.InstructionTLB
					Emit_CALL(FlushOneTLB);									// CALL FlushOneTLB
				} else if (d->CRm == 6) {
					// Flush DTLB Entry, Rd is the address
					Emit_MOV_Reg_DWORDPTR(EDX_Reg, &Cpu.GPRs[d->Rd]);		// MOV EDX, Cpu.GPRs[d->Rd]
					Emit_MOV_Reg_Imm32(ECX_Reg, PtrToLong(&Mmu.DataTLB));	// MOV ECX, Mmu.DataTLB
					Emit_CALL(FlushOneTLB);									// CALL FlushOneTLB
				} else {
					Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);			// MOV ECX, GuestAddress
					Emit_CALL(CpuRaiseUndefinedException);					// CALL CpuRaiseUndefinedException
					Emit_RETN(0);											// RETN
				}
				break;
			default:
				Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);				// MOV ECX, GuestAddress
				Emit_CALL(CpuRaiseUndefinedException);						// CALL CpuRaiseUndefinedException
				Emit_RETN(0);												// RETN
				break;
			}
		}
		break;

	// Register 9:  Cache lock down
	case 9:
		// Unlock and lock of cache not supported on the emulator
		Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);					// MOV ECX, GuestAddress
		Emit_CALL(CpuRaiseUndefinedException);							// CALL CpuRaiseUndefinedException
		Emit_RETN(0);													// RETN
		break;

	// Register 10: TLB lock down
	case 10:
		// Unlock and lock of TLBs not supported on the emulator
		Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);					// MOV ECX, GuestAddress
		Emit_CALL(CpuRaiseUndefinedException);							// CALL CpuRaiseUndefinedException
		Emit_RETN(0);													// RETN
		break;

	// Register 11: reserved
	// Register 12: reserved
	case 11:
	case 12:
		Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);					// MOV ECX, GuestAddress
		Emit_CALL(CpuRaiseUndefinedException);							// CALL CpuRaiseUndefinedException
		Emit_RETN(0);													// RETN
		break;

	// Register 13: Process ID
	case 13:
		if (d->L) {
			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Mmu.ProcessId);					// MOV EAX, Mmu.ProcessId
			Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);				// MOV Cpu.GPRs[d->Rd], EAX
		} else {
			unsigned __int8* NoException;

			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rd]);				// MOV EAX, Cpu.GPRs[Rd]
			Emit_TEST_Reg_Imm32(EAX_Reg, 0x01ffffff);						// TEST EAX, 0x01ffffff
			Emit_JZLabel(NoException);										// JZ NoException
			Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);					// MOV ECX, GuestAddress
			Emit_CALL(CpuRaiseUndefinedException);							// CALL CpuRaiseUndefinedException
			Emit_RETN(0);													// RETN

			FixupLabel(NoException);										// NoException:
			Emit_MOV_DWORDPTR_Reg(&Mmu.ProcessId, EAX_Reg);					// MOV Mmu.ProcessId, EAX
		}
		break;

	// Register 14: Breakpoint registers
	case 14:
		// Breakpoint registers not supported on the emulator
		Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);					// MOV ECX, GuestAddress
		Emit_CALL(CpuRaiseUndefinedException);							// CALL CpuRaiseUndefinedException
		Emit_RETN(0);													// RETN
		break;

	// Register 15: Coprocessor access
	case 15:
		if (d->L) {
			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Mmu.CoprocessorAccess);		// MOV EAX, Mmu.CoprocessorAccess
			Emit_MOV_DWORDPTR_Reg(&Cpu.GPRs[d->Rd], EAX_Reg);			// MOV Cpu.GPRs[d->Rd], EAX
		} else {
			unsigned __int8* NoException;

			Emit_MOV_Reg_DWORDPTR(EAX_Reg, &Cpu.GPRs[d->Rd]);				// MOV EAX, Cpu.GPRs[Rd]
			Emit_TEST_Reg_Imm32(EAX_Reg, 0xffffc000);						// AND EAX, 0xffffc000
			Emit_JZLabel(NoException);										// JZ NoException
			Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);					// MOV ECX, GuestAddress
			Emit_CALL(CpuRaiseUndefinedException);							// CALL CpuRaiseUndefinedException
			Emit_RETN(0);													// RETN

			FixupLabel(NoException)
			Emit_MOV_DWORDPTR_Reg(&Mmu.CoprocessorAccess, EAX_Reg);			// MOV Mmu.CoprocessorAccess, EAX
		}
		break;

	default:
		ASSERT(FALSE);
		Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);					// MOV ECX, GuestAddress
		Emit_CALL(CpuRaiseUndefinedException);							// CALL CpuRaiseUndefinedException
		Emit_RETN(0);													// RETN
		break;
	}
	return CodeLocation;
}

unsigned __int8* MMU::PlaceCoprocDataOperation(unsigned __int8* CodeLocation, Decoded* d)
{
	Emit_MOV_Reg_Imm32(ECX_Reg, d->GuestAddress);					// MOV ECX, GuestAddress
	Emit_CALL(CpuRaiseUndefinedException);							// CALL CpuRaiseUndefinedException
	Emit_RETN(0);													// RETN

	return CodeLocation;
}

void MMU::RaiseAlignmentException(unsigned __int32 EffectiveAddress)
{
	Mmu.FaultStatus.Bits.Status=FAULTSTATUS_ALIGNMENT;
	Mmu.FaultStatus.Bits.D=0;
	Mmu.FaultStatus.Bits.X=0;
	Mmu.FaultAddress=EffectiveAddress;
	// The MMU caller must call CpuRaiseAbortDataException() to complete the
	// exception raise.
}

void MMU::RaiseAbortException(unsigned __int32 EffectiveAddress, unsigned __int32 Status)
{
	Mmu.FaultStatus.Bits.Status=Status;
	Mmu.FaultStatus.Bits.D=0;
	Mmu.FaultStatus.Bits.X=1;
	Mmu.FaultAddress=EffectiveAddress;
	// The caller of the MMU must call CpuRaiseAbortDataException() or 
	// CpuRaiseAbortPrefetchException() in order to complete the
	// exception raise.
}

template <MMU::AccessType TAccessType> size_t __fastcall MMU::MMUMap<TAccessType>::MapGuestVirtualToHost(unsigned __int32 p, __int8 *pTLBIndexCache)
{
	unsigned __int32 EffectiveAddress;
	size_t HostAddress;
	int i;
	__int8 TLBIndexCache;
	TLBUnit *pTLBUnit;
	TLBRegister *pTLBRegister;

	// Fold in the process ID as appropriate
	if ((p & 0xfe000000) == 0) {
		p |= Mmu.ProcessId;
	}

	if (TAccessType == MMU::ExecuteAccess) {
		pTLBUnit = &Mmu.InstructionTLB;
	} else {
		pTLBUnit = &Mmu.DataTLB;
	}

	// Scan the TLB for the address
	TLBIndexCache = *pTLBIndexCache;

	for (i=0, TLBIndexCache = *pTLBIndexCache; 
			i<MAX_TLB_REGISTERS; 
			++i, TLBIndexCache = (TLBIndexCache+1) % MAX_TLB_REGISTERS) {

		L2PTEFormat L2PTE;

		pTLBRegister = &pTLBUnit->TLBRegisters[TLBIndexCache];

		L2PTE.Word = pTLBRegister->PTE;
		switch (L2PTE.FaultPTE.PTEType) {
		case 0:
			// 0 is normally invalid.  Here, it is used for a Section L1PTE
			if ((p >> 20) == pTLBRegister->VirtualAddress) {
				L1PTEFormat L1PTE;
				
				L1PTE.Word = pTLBRegister->PTE;
				// CHECK_DOMAIN is not required - it was checked before the PTE was installed
				// in the TLB.
				CHECK_AP(L1PTE.SectionPTE.AP, FAULTSTATUS_PERMISSION_SECTION);		// this may not return

				EffectiveAddress = (L1PTE.SectionPTE.SectionBaseAddress << 20) | (p & 0x000fffff);
				goto TLBHit;
			}
			break;

		case 1: // LargePagePTE
			if ((p >> 16) == pTLBRegister->VirtualAddress) {
				CHECK_AP((L2PTE.LargePagePTE.APs >> ((p >> 13) & 6)) & 3, FAULTSTATUS_PERMISSION_PAGE); // this may not return
				EffectiveAddress = (L2PTE.LargePagePTE.LargePageBaseAddress << 16) | (p & 0xffff);
				goto TLBHit;
			}
			break;

		case 2: // SmallPagePTE
			if ((p >> 12) == pTLBRegister->VirtualAddress) {
				CHECK_AP((L2PTE.SmallPagePTE.APs >> ((p >> 9) & 6)) & 3, FAULTSTATUS_PERMISSION_PAGE); // this may not return
				EffectiveAddress = (L2PTE.SmallPagePTE.SmallPageBaseAddress << 12) | (p & 0x0fff);
				goto TLBHit;
			}
			break;

		case 3: // ExtendedSmallPagePTE (aka TinyPTE)
			if (!pTLBRegister->Valid) {
				continue;
			}
			if ((p >> 10) == pTLBRegister->VirtualAddress) {
				CHECK_AP(L2PTE.ExtendedSmallPagePTE.AP, FAULTSTATUS_PERMISSION_PAGE); // this may not return
				EffectiveAddress = (L2PTE.ExtendedSmallPagePTE.ExtendedSmallPageBaseAddress << 10) | (p & 0x01ff);
				goto TLBHit;
			}
			break;
		}
	}

	// TLB miss.  Time to walk the actual page table and add the entry to the TLB

	// 18 bits from translation base, top 12 bits from p, 2 zero bits
	EffectiveAddress = Mmu.TranslationTableBase.Word | ((p >> 20)<<2);

	// Fetch the first-level PTE.  No range check is needed:  the Mmu.TranslationBase
	// value was checked when it was set, to ensure that all 8k of the top-level PTEs
	// are contained within RAM.
	L1PTEFormat L1PTE;
	L1PTE = *(L1PTEFormat *)BoardMapGuestPhysicalToHostRAM(EffectiveAddress); // this doesn't require any range checking, and is performance-critical

	pTLBRegister = &pTLBUnit->TLBRegisters[pTLBUnit->NextFreeTLB];

	switch (L1PTE.FaultPTE.PTEType) {
	case 0: // Fault PTE
		Mmu.RaiseAbortException(p, FAULTSTATUS_TRANSLATION_SECTION);
		BoardIOAddress=0;
		return 0;

	case 1: // CoarseGrainedPTE
		{
			L2PTEFormat *pL2PTE;
			L2PTEFormat L2PTE;

			CHECK_DOMAIN(L1PTE.CoarseGrainedPTE.Domain, FAULTSTATUS_DOMAIN_PAGE); // this may not return

			// 22 bits from L1 PTE Translation Base, 8 bits from p = 30 bits
			EffectiveAddress = (L1PTE.CoarseGrainedPTE.PageTableBaseAddress << 10) | (((p >> 12) & 0xff)<<2);

			pL2PTE = (L2PTEFormat *)BoardMapGuestPhysicalToHostRAM(EffectiveAddress); 
			if (!pL2PTE) {
				// The level 2 PTE isn't contained within RAM.  Abort now.
				Mmu.RaiseAbortException(p, FAULTSTATUS_EXTERNAL_ABORT_TRANSLATION_2);
				BoardIOAddress=0;
				return 0;
			}
			L2PTE = *pL2PTE;

			switch (L2PTE.FaultPTE.PTEType) {
			case 0: // fault or extended small page
			case 3:
				Mmu.RaiseAbortException(p, FAULTSTATUS_TRANSLATION_PAGE);
				BoardIOAddress=0;
				return 0;

			case 1: // LargePagePTE
				CHECK_AP((L2PTE.LargePagePTE.APs >> ((p >> 13) & 6)) & 3, FAULTSTATUS_PERMISSION_PAGE); // this may not return
				pTLBRegister->PTE = L2PTE.Word;
				pTLBRegister->VirtualAddress = p >> 16;
				EffectiveAddress = (L2PTE.LargePagePTE.LargePageBaseAddress << 16) | (p & 0xffff);
				break;

			case 2: // SmallPagePTE
				CHECK_AP((L2PTE.SmallPagePTE.APs >> ((p >> 9) & 6)) & 3, FAULTSTATUS_PERMISSION_PAGE); // this may not return
				pTLBRegister->PTE = L2PTE.Word;
				pTLBRegister->VirtualAddress = p >> 12;
				EffectiveAddress = (L2PTE.SmallPagePTE.SmallPageBaseAddress << 12) | (p & 0x0fff);
				break;

			default:
				ASSERT(FALSE);
				break;
			}
		}
		break;

	case 2: // SectionPTE
		CHECK_DOMAIN(L1PTE.SectionPTE.Domain, FAULTSTATUS_DOMAIN_SECTION);	// this may not return
		CHECK_AP(L1PTE.SectionPTE.AP, FAULTSTATUS_PERMISSION_SECTION);		// this may not return

		pTLBRegister->PTE = L1PTE.Word & ~3; // Set the PTE type to 0
		pTLBRegister->VirtualAddress = p >> 20;
		EffectiveAddress = (L1PTE.SectionPTE.SectionBaseAddress << 20) | (p & 0x000fffff);
		break;

	case 3: // FineGrainedPTE
		{
			L2PTEFormat *pL2PTE;
			L2PTEFormat L2PTE;

			CHECK_DOMAIN(L1PTE.FineGrainedPTE.Domain, FAULTSTATUS_DOMAIN_PAGE); // this may not return

			// 22 bits from L1 PTE Translation Base, 10 bits from p = 30 bits
			EffectiveAddress = (L1PTE.FineGrainedPTE.PageTableBaseAddress << 14) | (((p >> 10) & 0x3ff)<<2);
			pL2PTE = (L2PTEFormat*)BoardMapGuestPhysicalToHostRAM(EffectiveAddress);
			if (!pL2PTE) {
				// The level 2 PTE isn't contained within RAM.  Abort now.
				Mmu.RaiseAbortException(p, FAULTSTATUS_EXTERNAL_ABORT_TRANSLATION_2);
				BoardIOAddress=0;
				return 0;
			}
			L2PTE = *pL2PTE;

			switch (L2PTE.FaultPTE.PTEType) {
			case 0:
				Mmu.RaiseAbortException(p, FAULTSTATUS_TRANSLATION_PAGE);
				BoardIOAddress=0;
				return 0;

			case 1: // LargePagePTE
				CHECK_AP((L2PTE.LargePagePTE.APs >> ((p >> 13) & 6)) & 3, FAULTSTATUS_PERMISSION_PAGE); // this may not return
				pTLBRegister->PTE = L2PTE.Word;
				pTLBRegister->VirtualAddress = p >> 16;
				EffectiveAddress = (L2PTE.LargePagePTE.LargePageBaseAddress << 16) | (p & 0xffff);
				break;

			case 2: // SmallPagePTE
				CHECK_AP((L2PTE.SmallPagePTE.APs >> ((p >> 9) & 6)) & 3, FAULTSTATUS_PERMISSION_PAGE); // this may not return
				pTLBRegister->PTE = L2PTE.Word;
				pTLBRegister->VirtualAddress = p >> 12;
				EffectiveAddress = (L2PTE.SmallPagePTE.SmallPageBaseAddress << 12) | (p & 0x0fff);
				break;

			case 3: // ExtendedSmallPagePTE (aka TinyPTE)
				CHECK_AP(L2PTE.ExtendedSmallPagePTE.AP, FAULTSTATUS_PERMISSION_PAGE); // this may not return
				pTLBRegister->PTE = L2PTE.Word;
				pTLBRegister->VirtualAddress = p >> 10;
				EffectiveAddress = (L2PTE.ExtendedSmallPagePTE.ExtendedSmallPageBaseAddress << 10) | (p & 0x01ff);
				break;

			default:
				ASSERT(FALSE);
				break;
			}
		}
		break;

	default:
		ASSERT(FALSE);
		break;
	}

	pTLBRegister->Valid = true;
	TLBIndexCache = pTLBUnit->NextFreeTLB;
	pTLBUnit->NextFreeTLB = (pTLBUnit->NextFreeTLB+1) % MAX_TLB_REGISTERS;

	if (*pTLBIndexCache != TLBIndexCache) {
		*pTLBIndexCache=TLBIndexCache;
	}

	if (TAccessType != MMU::WriteAccess) {
		HostAddress = BoardMapGuestPhysicalToHost(EffectiveAddress, &pTLBRegister->HostAdjust);
	} else {
		HostAddress = BoardMapGuestPhysicalToHostWrite(EffectiveAddress, &pTLBRegister->HostAdjust);
	}

#if PRINT_VAS
	if (TAccessType != MMU::ExecuteAccess && HostAddress != 0) {
		LogPrint((output, "Guest VA %8.8x mapped to Guest PA %8.8x mapped to Host %p\n",
			p, EffectiveAddress, HostAddress));
	}
#endif

	return HostAddress;

TLBHit:
	if (*pTLBIndexCache != TLBIndexCache) {
		*pTLBIndexCache=TLBIndexCache;
	}

	if (pTLBRegister->HostAdjust) {
		HostAddress = EffectiveAddress + pTLBRegister->HostAdjust;
	} else {
		size_t HostFlashAddress;
		if (TAccessType != MMU::WriteAccess) {
			HostFlashAddress = BoardMapGuestPhysicalToFlash(EffectiveAddress);
			if (HostFlashAddress) {
				// Attempt to read from 0...32mb, but the TLB has cached a previous write, which
				// performs I/O.
				HostAddress = HostFlashAddress;
			} else {
				BoardIOAddress = EffectiveAddress;
				return 0;
			}
		} else {
			BoardIOAddress = EffectiveAddress;
			if ( BoardIOAddress == 0 )
			{
				// Since we use zero to indicate error we can't return set BoardIO to 0
				BoardIOAddress = 1;
				BoardIOAddressAdj = (unsigned int)-1;
			}
			return 0;
		}
	}

#if PRINT_VAS
	if (TAccessType != MMU::ExecuteAccess) {
		LogPrint((output, "Guest VA %8.8x mapped to Guest PA %8.8x mapped to Host %p\n",
			p, EffectiveAddress, HostAddress));
	}
#endif

	return HostAddress;
}

// This routine may be called by peripheral device emulators, to map
// physical addresses from WinCE to Win32 - useful for DMA.
size_t MMU::MapGuestPhysicalToHostRAM(unsigned __int32 EffectiveAddress)
{
	return BoardMapGuestPhysicalToHostRAM(EffectiveAddress);
}

size_t MMU::MapGuestPhysicalToHost(unsigned __int32 EffectiveAddress)
{
	size_t HostAddress;
	size_t HostAdjust;

	HostAddress = BoardMapGuestPhysicalToHost(EffectiveAddress, &HostAdjust);

#if PRINT_VAS
	if (HostAddress) {
		LogPrint((output, "Guest VA %8.8x mapped to Guest PA %8.8x mapped to Host %p\n",
				EffectiveAddress, EffectiveAddress, HostAddress));
	}
#endif

	return HostAddress;
}

bool __fastcall MMU::IsGuestAddressInRAM(unsigned __int32 p)
{
	size_t HostAddress;

	if (Mmu.ControlRegister.Bits.M) {
		static __int8 TLBIndexCache;
		HostAddress = Mmu.MmuMapExecute.MapGuestVirtualToHost(p, &TLBIndexCache);
	} else {
		HostAddress = Mmu.MapGuestPhysicalToHost(p);
	}

	return BoardIsHostAddressInRAM(HostAddress);
}

// This routine is private in the MMU class and is not called.  However, it
// must remain here:  otherwise, the linker doesn't generate instantiations
// of the MapGuestVirtualToHost methods and we get link errors.
size_t MMU::MapGuestToHost(unsigned __int32 EffectiveAddress, MMU::AccessType AccessType, __int8 *pTLBIndexHint)
{
	switch (AccessType) {
	case MMU::ReadAccess:
		return Mmu.MmuMapRead.MapGuestVirtualToHost(EffectiveAddress, pTLBIndexHint);

	case MMU::ReadWriteAccess:
		return Mmu.MmuMapReadWrite.MapGuestVirtualToHost(EffectiveAddress, pTLBIndexHint);

	case MMU::WriteAccess:
		return Mmu.MmuMapWrite.MapGuestVirtualToHost(EffectiveAddress, pTLBIndexHint);

	case MMU::ExecuteAccess:
		return Mmu.MmuMapExecute.MapGuestVirtualToHost(EffectiveAddress, pTLBIndexHint);

        case MMU::NoAccess:
                // fall through into the default case: this is an unexpected input parameter
	default:
		ASSERT(FALSE);
		break;
	}
	ASSERT(FALSE);
	return 0;
}

