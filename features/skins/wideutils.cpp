/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include <string>
#pragma warning (disable:4702)
#include <vector>
#pragma warning (default:4702)
#include <sstream>

#include "WideUtils.h"

using namespace std;

bool WideStringToInt(const wchar_t* inStr, int* outInt)
{
bool success = false;

	wistringstream converter(inStr);
	if( !converter.str().empty() )
	{
		converter.unsetf( ios::dec );
		converter >> *outInt;
		
		if( !!converter )
			success = true;
	}
	return success;
}

bool WideStringToIntVector(const wchar_t* inStr, vector<int> & outVector )
{
	unsigned __int32 num;
	wchar_t peekChar;
	
	wistringstream	converter( inStr );
	if( !converter.str().empty() )
	{	
		while(1)
		{	
			peekChar = converter.peek();
		
			// Remove leading whitespace.
			while( peekChar == L' ' ||
				   peekChar == L'\t' ||
				   peekChar == L'\r' ||
				   peekChar == L'\n' )
			{
				peekChar = converter.get();
				peekChar = converter.peek();
			}
					
			switch (peekChar)
			{
				case L'0':		// get hex number
					converter.unsetf( ios::dec );
					converter >> num;
					outVector.push_back(num);
					break;
				
				case L',':
					peekChar = converter.get();
					break;
				
				case WEOF:
					if( !outVector.empty() )
						goto Success; // we found at least one valid token.
					goto Failure;
					
				default:
					goto Failure;
			}		 
		}
	}
Failure:;	
	return false;

Success:;
	return true;
}

bool WideStringToIntToken(
	const wchar_t* inStr, 
	TokenList inTokenList, 
	int inTokenCount, 
	int* outInt )
{
	wistringstream	converter( inStr );
	bool found = false;
	
	if( !converter.str().empty() )
	{	
		wstring token;	
		converter >> token;
		
		for( int index = 0; index < inTokenCount; index++ )
		{
			if( token == ( inTokenList + index )->tokenString )
			{
				// found a token string
				*outInt = (( inTokenList + index )->token);
				found = true;
				break;
			}
		}	
	}
	return found;
}

