/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

Module Name:

    redblack.h

Abstract:
    
    Prototypes for the red/black tree implementation.

--*/

PEPNODE
EPInsert(
    PEPNODE root,
    PEPNODE x,
    PEPNODE NIL
    );

PEPNODE
EPFindNode(
    PEPNODE root,
    unsigned __int32 addr,
    PEPNODE NIL
    );

PEPNODE
EPFindNext(
    PEPNODE root,
    unsigned __int32 addr,
    PEPNODE NIL
    );

PEPNODE
EPDelete(
    PEPNODE root,
    PEPNODE z,
    PEPNODE NIL
    );

bool
EPContainsRange(
    PEPNODE root,
    PEPNODE NIL,
    unsigned __int32 StartAddr,
    unsigned __int32 EndAddr
    );
