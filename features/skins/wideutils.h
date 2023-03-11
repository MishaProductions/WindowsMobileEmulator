/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef __XML_WIDE_UTILS__
#define __XML_WIDE_UTILS__
#pragma once

#include "emulator.h"

typedef struct
{
	wchar_t	tokenString[128];
	int	token;
}TokenListItem;

typedef TokenListItem* TokenList;

//////////////////////////////////////////////////////////////////////////////////////////
bool WideStringToInt( const wchar_t* inStr, int* outInt );

bool WideStringToIntVector(const wchar_t* inStr, std::vector<unsigned __int32> & outVector );

bool WideStringToIntToken(
		const wchar_t* inStr, 
		TokenList inTokenList, 
		int inTokenCount, 
		int* outInt );
		
#endif // __XML_WIDE_UTILS__

