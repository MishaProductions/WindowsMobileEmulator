/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef _MMU_H_INCLUDED
#define _MMU_H_INCLUDED

typedef union {
	struct {
		unsigned __int32 M:1; // MMU (0=disabled, 1=enabled)
		unsigned __int32 A:1; // alignment faults (0=disabled, 1=enabled)
		unsigned __int32 C:1; // data cache enable
		unsigned __int32 W:1; // write buffer enable
		unsigned __int32 P:1; // Enter exception handlers in 26-bit mode if 0, 32-bit if 1
		unsigned __int32 D:1; // 26-bit address checking if 0, 32-bit if 1
		unsigned __int32 L:1; // Abort Model (1 = Late, 0=Early, now obsolete)
		unsigned __int32 B:1; // big-endian (0=little-endian)
		unsigned __int32 S:1; // system-protect
		unsigned __int32 R:1; // ROM-protect
		unsigned __int32 Reserved2:1; // must be zero
		unsigned __int32 Z:1; // Branch target buffer (0=disabled, 1=enabled)
		unsigned __int32 I:1; // instruction cache enable
		unsigned __int32 V:1; // exception vector relocation (0=address 00000000, 1=address ffff0000)
		unsigned __int32 RR:1;// cache replacement strategy
		unsigned __int32 L4:1;// 0=normal behavior, 1=load instructions use bit0 ignore Thumb mode
		unsigned __int32 Reserved3:16; // must be 0
	} Bits;

	unsigned __int32 Word;
} ControlRegisterType;

typedef union {
	struct {
		unsigned __int32 K:1; // write coalescing
		unsigned __int32 P:1; // page table memory
		unsigned __int32 Reserved1:2; // must be 0
		unsigned __int32 MD:2; // mini data cache
		unsigned __int32 Reserved2:26; // must be 0
	} Bits;

	unsigned __int32 Word;
} AuxControlRegisterType;

typedef union {
	struct {
		unsigned __int32 Reserved1:14; // must be 0
		unsigned __int32 Base:18;
	} Bits;

	unsigned __int32 Word;
} TranslationTableBaseRegisterType;

typedef struct {
	unsigned __int32 VirtualAddress;
	unsigned __int32 PTE;
	size_t HostAdjust;
	bool Valid;
} TLBRegister;


#define MAX_TLB_REGISTERS 64
typedef struct {
	__int8 NextFreeTLB;
	TLBRegister TLBRegisters[MAX_TLB_REGISTERS];
} TLBUnit;


// Values for the MMU.FaultStatus.Status.  See ARM ARM B3-19
#define FAULTSTATUS_TERMINAL_EXCEPTION           2
#define FAULTSTATUS_VECTOR_EXCEPTION             0
#define FAULTSTATUS_ALIGNMENT                    1 // can also be 3
#define FAULTSTATUS_EXTERNAL_ABORT_TRANSLATION_1 12
#define FAULTSTATUS_EXTERNAL_ABORT_TRANSLATION_2 14
#define FAULTSTATUS_TRANSLATION_SECTION          5
#define FAULTSTATUS_TRANSLATION_PAGE             7
#define FAULTSTATUS_DOMAIN_SECTION               9
#define FAULTSTATUS_DOMAIN_PAGE                  11
#define FAULTSTATUS_PERMISSION_SECTION           13
#define FAULTSTATUS_PERMISSION_PAGE              15
#define FAULTSTATUS_EXTERNAL_ABORT_LINEFETCH_SECTION 4
#define FAULTSTATUS_EXTERNAL_ABORT_LINEFETCH_PAGE 6
#define FAULTSTATUS_EXTERNAL_ABORT_SECTION       8
#define FAULTSTATUS_EXTERNAL_ABORT_PAGE          10


// State of coprocessor 15
class MMU {
public:
	
	// Register 1  Control and AuxControl, readwrite
	ControlRegisterType ControlRegister;
	AuxControlRegisterType AuxControlRegister;

	// Register 2  Translation Table Base, readwrite
	TranslationTableBaseRegisterType TranslationTableBase;

	// Register 3  Domain Access Table, readwrite
	unsigned __int32 DomainAccessControl;  // 16 pairs of bits

	// Register 5  Fault Status, readwrite
	union {
		unsigned __int32 Word;

		struct {
			unsigned __int32 Status:4;
			unsigned __int32 Domain:4;
			unsigned __int32 Reserved:1; // must be zero
			unsigned __int32 D:1; // debug event
			unsigned __int32 X:1; // status field extension (bit 10)
			unsigned __int32 SBZ:21;
		} Bits;
	} FaultStatus;

	// Register 6  Fault Address, readwrite
	unsigned __int32 FaultAddress;

	// Register 13: Process ID
	unsigned __int32 ProcessId;

	// Register 15: Coprocessor access
	unsigned __int32 CoprocessorAccess;

	typedef enum {
		NoAccess=0,
		ReadAccess=2,
		WriteAccess=4,
		ReadWriteAccess=6, // read|write
		ExecuteAccess=10    // note: ARM does not support 'execute', only 'read', but the MMU selects
							//       the ITLB vs DTLB based on the MmuAccessType.  So we use execute+read to disambiguate.
	} AccessType;

	// MMU coprocessor interface
	static unsigned __int8* __fastcall PlaceCoprocDataTransfer(unsigned __int8* CodeLocation, Decoded* d);
	static unsigned __int8* __fastcall PlaceCoprocRegisterTransfer(unsigned __int8* CodeLocation, Decoded* d);
	static unsigned __int8* __fastcall PlaceCoprocDataOperation(unsigned __int8* CodeLocation, Decoded* d);

	static size_t __fastcall MapGuestPhysicalToHost(unsigned __int32 EffectiveAddress);
	static size_t __fastcall MapGuestPhysicalToHostRAM(unsigned __int32 EffectiveAddress);
	static bool __fastcall IsGuestAddressInRAM(unsigned __int32 p);

	static void __fastcall RaiseAlignmentException(unsigned __int32 EffectiveAddress);

	static void __fastcall Initialize(void);

private:
	static size_t __fastcall MapGuestToHost(unsigned __int32 EffectiveAddress, AccessType AccessType, __int8* pTLBIndexHint);
	static void __fastcall RaiseAbortException(unsigned __int32 EffectiveAddress, unsigned __int32 Status);
	static void __fastcall FlushAllTLBs(TLBUnit *pTLBUnit);
	static void __fastcall FlushOneTLB(TLBUnit *pTLBUnit, unsigned __int32 p);
	static int __fastcall SetControlRegisterHelper2(unsigned __int32 NewValue, unsigned __int32 GuestAddress);
	static void SetControlRegisterHelper(void);
	static void FlushTranslationCacheHelper(void);

	// Translation Lookaside Buffers - one for data fetches and one for instructions
	TLBUnit DataTLB;
	TLBUnit InstructionTLB;

	template <MMU::AccessType TAccessType> class MMUMap {
	public:
		static size_t __fastcall MapGuestVirtualToHost(unsigned __int32 p, __int8 *pTLBIndexCache);
	};
public:
	MMUMap<MMU::ReadAccess> MmuMapRead;
	MMUMap<MMU::ReadWriteAccess> MmuMapReadWrite;
	MMUMap<MMU::WriteAccess> MmuMapWrite;
	MMUMap<MMU::ExecuteAccess> MmuMapExecute;
};


extern MMU Mmu;

// Fold in the process ID as appropriate
#define MmuActualGuestAddress(p) (((Mmu.ControlRegister.Bits.M && ((p) & 0xfe000000) == 0) ) ? (p | Mmu.ProcessId) : p)

#endif //!_MMU_H_INCLUDED
