/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

Module Name:

    redblack.c

Abstract:
    
    This module implements red/black trees.
    
--*/

#include "emulator.h"
#include "entrypt.h"
#include "redblack.h"

#define START(x)        x->ep.GuestStart
#define END(x)          x->ep.GuestEnd
#define KEY(x)          x->ep.GuestStart
#define RIGHT(x)        x->Right
#define LEFT(x)         x->Left
#define PARENT(x)       x->Parent
#define COLOR(x)        x->Color

#define RB_INSERT       EPInsert
#define FIND            EPFindNode
#define CONTAINSRANGE   EPContainsRange
#define REMOVE          EPRemove
#define LEFT_ROTATE     EPLeftRotate
#define RIGHT_ROTATE    EPRightRotate
#define TREE_INSERT     EPTreeInsert
#define TREE_SUCCESSOR  EPTreeSuccessor
#define RB_DELETE       EPDelete
#define RB_DELETE_FIXUP EPDeleteFixup
#define FINDNEXT        EPFindNext

#include "redblack.fnc"
