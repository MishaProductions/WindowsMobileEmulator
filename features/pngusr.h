/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#pragma once

// StrSafe uses depreciated APIs
#pragma warning(push)
#pragma warning(disable:4996)
#include <strsafe.h>
#pragma warning(pop)

#include "resource.h"

// Don't enable code for writing PNGs
#define PNG_NO_WRITE_SUPPORTED 1

// Override the default PNG_ABORT(), which calls C Runtime abort(1)
extern void __cdecl ShowDialog(unsigned int Expression, ...);
#define PNG_ABORT() { ShowDialog(ID_MESSAGE_INTERNAL_ERROR); exit(1); }

