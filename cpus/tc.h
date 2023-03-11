/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

Module Name:

    tc.h

Abstract:
    
    The interface to the Translation Cache

--*/

#ifndef _TC_H_
#define _TC_H_

bool
InitializeTranslationCache(
    void
    );

unsigned __int8 *
AllocateTranslationCache(
    size_t Size
    );

void
FreeUnusedTranslationCache(
    unsigned __int8 *StartOfFree
    );

void
FlushTranslationCache(
    unsigned __int32 GuestAddr,
    unsigned __int32 Length
    );

#endif
