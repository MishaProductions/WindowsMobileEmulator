/*++
                                                                                
 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

Module Name:

    entrypt.cpp

Abstract:
    
    This module stores the Entry Point structures, and retrieves them
    given either an ARM address or a native address.
    
--*/

#include "emulator.h"
#include "cpu.h"
#include "entrypt.h"
#include "ARMCpu.h"
#include "redblack.h"
#include "mmu.h"

class IEntrypoint {
public:
	void Initialize(void);
	PENTRYPOINT EPFromGuestAddr(unsigned __int32 GuestAddr);
	PENTRYPOINT GetNextEPFromGuestAddr(unsigned __int32 GuestAddr);
	void FlushEntrypoints(void);
	void insertEntryPoint(PEPNODE pNewEntryPoint);

	bool IsGuestRangeInCache(unsigned __int32 Addr, unsigned __int32 Length);

private:

#if ENTRYPOINT_HITCOUNTS
	void GatherHitcounts(EPNODE *p);
	void DisplayHitcounts(void);
	static int __cdecl CompareHits(const void *p1, const void *p2);
	int Ptr;
	ENTRYPOINT *EPList[1024*1024];
#endif //ENTRYPOINT_HITCOUNTS

	EPNODE _NIL;
	PEPNODE NIL;
	PEPNODE EPRoot;

};

class IEntrypoint ARMEntrypoints;
class IEntrypoint ThumbEntrypoints;

void
IEntrypoint::Initialize(
    void
    )
/*++

Routine Description:

    Initializes the entry point module by allocating initial dll tables.  
    Must be called once for each process.

Arguments:

    none

Return Value:

    none

--*/
{
	NIL=&_NIL;
	EPRoot=&_NIL;
	NIL->Left = NIL->Right  = NIL->Parent = NIL;
	NIL->Color = BLACK;
}



void
IEntrypoint::insertEntryPoint(
    PEPNODE pNewEntryPoint
    )
/*++

Routine Description:

    Inserts the entry point structure into the red/black tree

Arguments:

    pNewEntryPoint - A pointer to the entry point structure to be inserted 
                     into the trees

Return Value:

	none

--*/
{
#if ENTRYPOINT_HITCOUNTS
	pNewEntryPoint->ep.HitCount=0;
#endif
    EPRoot = EPInsert(EPRoot, pNewEntryPoint, NIL);
}


PENTRYPOINT
IEntrypoint::EPFromGuestAddr(
    unsigned __int32 GuestAddr
    )
/*++

Routine Description:

    Retrieves an entry point structure containing the given intel address

Arguments:
                                                                                
    intelAddr - The intel address contained within the code corresponding to 
        the entry point structure

Return Value:

    return-value - The entry point structure if found, NULL otherwise.

--*/
{
    PENTRYPOINT EP;
    PEPNODE pEPNode;

    pEPNode = EPFindNode(EPRoot, GuestAddr, NIL);
    if (!pEPNode) {
        //
        // No EPNODE contains the address
        //
        return NULL;
    }

    //
    // The ENTRYPOINT inside the EPNODE contains the address.  Search
    // for an ENTRYPOINT which matches that address exactly.
    //
    EP = &pEPNode->ep;
    do {
        if (EP->GuestStart == GuestAddr) {
            //
            // Found a sub-Entrypoint whose Guest address exactly matches
            // the one we were looking for.
            //
            return EP;
        }
        EP=EP->SubEP;
    } while (EP);

    //
    // The EPNODE in the Red-black tree contains the Guest address, but
    // no sub-Entrypoint exactly describes the Guest address.
    //
    return &pEPNode->ep;
}

PENTRYPOINT
IEntrypoint::GetNextEPFromGuestAddr(
    unsigned __int32 GuestAddr
    )
/*++

Routine Description:

    Retrieves the entry point following

Arguments:

    GuestAddr - The guest address contained within the code corresponding to
        the entry point structure

Return Value:

    A pointer to the first EntryPoint which follows a particular guest address.

--*/
{
    PEPNODE pEP;

    pEP = EPFindNext(EPRoot, GuestAddr, NIL);

    return &pEP->ep;
}


bool
IEntrypoint::IsGuestRangeInCache(
    unsigned __int32 Addr,
    unsigned __int32 Length
    )
/*++

Routine Description:

    Determines if any entrypoints are contained within a range of memory.
    Used to determine if the Translation Cache must be flushed.

Arguments:

    Addr/Length - range of addresses to search for.  If the intersection
	of that range with any entrypoints in the cache is non-empty, then
	this routine returns true.

Return Value:

    None

--*/
{
    if (EPRoot == NIL) {
        //
        // Empty tree - no need to flush
        //
        return false;
    }

    return EPContainsRange(EPRoot,
                           NIL,
                           Addr,
                           Addr + Length
                          );
}

#if ENTRYPOINT_HITCOUNTS
void IEntrypoint::GatherHitcounts(EPNODE *p) {
	if (p == NIL || Ptr == ARRAY_SIZE(EPList)) {
		return;
	}
	GatherHitcounts(p->Left);
	EPList[Ptr++] = &p->ep;
	GatherHitcounts(p->Right);
}

void IEntrypoint::DisplayHitcounts(void)
{
	printf("Cache flush...\n");
	Ptr = 0;
	GatherHitcounts(EPRoot);
	qsort(EPList, Ptr, sizeof(EPList[0]), CompareHits);
	for (int i=0; i<10 && i<Ptr; ++i) {
		printf("%d hits:  Entrypoint=%p, NativeStart=%p, NativeEnd=%p\n", EPList[i]->HitCount, EPList[i], EPList[i]->nativeStart, EPList[i]->nativeEnd);
	}
}

int __cdecl IEntrypoint::CompareHits(const void *p1, const void *p2)
{
	const ENTRYPOINT** ppep1 = (const ENTRYPOINT**)p1;
	const ENTRYPOINT** ppep2 = (const ENTRYPOINT**)p2;

	return (*ppep2)->HitCount - (*ppep1)->HitCount;
}
#endif //ENTRYPOINT_HITCOUNTS

void
IEntrypoint::FlushEntrypoints(
    void
    )
/*++

Routine Description:

    Quickly deletes all entrypoints.  Called by the Translation Cache when
    the cache is flushed.

Arguments:

    None

Return Value:

    None

--*/
{
    if (EPRoot != NIL) {
		// FlushTranslationCache() already deletes the heap contains all entrypoints in the tree

#if ENTRYPOINT_HITCOUNTS
		DisplayHitcounts();
#endif //ENTRYPOINT_HITCOUNTS

        // Reset the root of the tree
        EPRoot = NIL;
    }
}

void
EntrypointInitialize(
    void
    )
{
	ARMEntrypoints.Initialize();
	ThumbEntrypoints.Initialize();
}

PENTRYPOINT 
EPFromGuestAddr(
    unsigned __int32 GuestAddr
    )
{
	GuestAddr = MmuActualGuestAddress(GuestAddr);

	if (Cpu.CPSR.Bits.ThumbMode) {
		return ThumbEntrypoints.EPFromGuestAddr(GuestAddr);
	} else {
		return ARMEntrypoints.EPFromGuestAddr(GuestAddr);
	}
}

PENTRYPOINT
EPFromGuestAddrExact(
	unsigned __int32 GuestAddr)
{
	PENTRYPOINT ep;

	GuestAddr = MmuActualGuestAddress(GuestAddr);

	ep = EPFromGuestAddr(GuestAddr);
	if (ep && (ep->GuestStart != GuestAddr || ep->nativeStart == LongToPtr(0xcccccccc))) {
		return NULL;
	}
	return ep;
}

PENTRYPOINT
GetNextEPFromGuestAddr(
    unsigned __int32 GuestAddr
    )
{
	GuestAddr = MmuActualGuestAddress(GuestAddr);

	if (Cpu.CPSR.Bits.ThumbMode) {
		return ThumbEntrypoints.GetNextEPFromGuestAddr(GuestAddr);
	} else {
		return ARMEntrypoints.GetNextEPFromGuestAddr(GuestAddr);
	}
}

void
FlushEntrypoints(
    void
    )
{
	ThumbEntrypoints.FlushEntrypoints();
	ARMEntrypoints.FlushEntrypoints();
}

void
insertEntryPoint(
    PEPNODE pNewEntryPoint
    )
{
	if (Cpu.CPSR.Bits.ThumbMode) {
		ThumbEntrypoints.insertEntryPoint(pNewEntryPoint);
	} else {
		ARMEntrypoints.insertEntryPoint(pNewEntryPoint);
	}
}

bool
IsGuestRangeInCache(
    unsigned __int32 Addr,
    unsigned __int32 Length
    )
{
	Addr = MmuActualGuestAddress(Addr);

	if (Cpu.CPSR.Bits.ThumbMode) {
		return ThumbEntrypoints.IsGuestRangeInCache(Addr, Length);
	} else {
		return ARMEntrypoints.IsGuestRangeInCache(Addr, Length);
	}
}
