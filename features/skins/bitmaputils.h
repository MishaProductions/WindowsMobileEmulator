/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef BITMAP_UTILS_H
#define BITMAP_UTILS_H
#pragma once

#include "emulator.h"

bool LoadBitmapFromBMPFile( const std::wstring& fileName, HBITMAP *phBitmap, bool & TopDown );

HRGN BitmapToRegion( HBITMAP hBmp, COLORREF cOpaqueColor, COLORREF cTolerance );

#endif // BITMAP_UTILS_H

