/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

Module Name:

    entrypt.h

Abstract:
    
    The interface to the entry point module.
    
--*/

#ifndef _ENTRYPT_H_
#define _ENTRYPT_H_

// The colors
typedef enum {RED, BLACK} COL;

// The Entry Point Structure
typedef struct _entryPoint {
    unsigned __int32 GuestStart;
    unsigned __int32 GuestEnd;
    PVOID nativeStart;
    PVOID nativeEnd;
    unsigned __int32 FlagsNeeded;
    struct _entryPoint *SubEP;
#if ENTRYPOINT_HITCOUNTS
	unsigned __int32 HitCount;
#endif //ENTRYPOINT_HITCOUNTS
} ENTRYPOINT, *PENTRYPOINT;


// The EPNODE structure
typedef struct _epNode
{
    ENTRYPOINT ep;

    struct _epNode *Left;
    struct _epNode *Right;
    struct _epNode *Parent;
    COL Color;

} EPNODE, *PEPNODE;


// Prototypes

void
EntrypointInitialize(
    void
    );

PENTRYPOINT 
EPFromGuestAddr(
    unsigned __int32 GuestAddr
    );

PENTRYPOINT
EPFromGuestAddrExact(
	unsigned __int32 GuestAddr
	);

PENTRYPOINT
GetNextEPFromGuestAddr(
    unsigned __int32 GuestAddr
    );

void
FlushEntrypoints(
    void
    );

void
insertEntryPoint(
    PEPNODE pNewEntryPoint
    );

bool
IsGuestRangeInCache(
    unsigned __int32 Addr,
    unsigned __int32 Length
    );


#endif
