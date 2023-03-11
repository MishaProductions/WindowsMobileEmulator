/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "XMLSkin.h"
#include "XMLReader.h"
#include "WideUtils.h"
#include "BitmapUtils.h"
#pragma warning (disable:4995) // name was marked as #pragma deprecated - triggered by strsafe.h
#include <ShlwAPI.h>
#pragma warning(default:4995)
#include "resource.h"

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "xmlskin.tmh"
#include "vsd_logging_inc.h"

using namespace std;

/*------------------------------------------------------------------
------------------------------------------------------------------*/

TokenListItem	keyWStringsToCodes[] =
{
	// Special codes
	{L"DOWN",					fKeyDown},
	{L"UP",						fKeyUp},
	
	{L"SHUTDOWN",				0x00010000},
	
	// top row
	{L"Key_Escape",					0x00000001},
	{L"Key_F1",						0x0000003b},
	{L"Key_F2",						0x0000003c},
	{L"Key_F3",						0x0000003d},
	{L"Key_F4",						0x0000003e},
	{L"Key_F5",						0x0000003f},
	{L"Key_F6",						0x00000040},
	{L"Key_F7",						0x00000041},
	{L"Key_F8",						0x00000042},
	{L"Key_F9",						0x00000043},
	{L"Key_F10",					0x00000044},
	{L"Key_F11",					0x00000057},
	{L"Key_F12",					0x00000058},
	
	// numbers row
	{L"Key_LeftApostrophe",			0x00000029},
	{L"Key_1",						0x00000002},
	{L"Key_2",						0x00000003},
	{L"Key_3",						0x00000004},
	{L"Key_4",						0x00000005},
	{L"Key_5",						0x00000006},
	{L"Key_6",						0x00000007},
	{L"Key_7",						0x00000008},
	{L"Key_8",						0x00000009},
	{L"Key_9",						0x0000000a},
	{L"Key_0",						0x0000000b},
	{L"Key_Hyphen",					0x0000000c},
	{L"Key_Equals",					0x0000000d},
	{L"Key_Backspace",				0x0000000e},
	{L"Key_Insert",					0x00000052},
	{L"Key_Home",					0x00000047},
	{L"Key_PageUp",					0x00000049},
	{L"Key_NumLock",				0x00000045},
	{L"KeyPad_Divide",				0x00000035},
	{L"KeyPad_Multiply",			0x00000037},
	{L"KeyPad_Minus",				0x0000004a},
	
	// QWERTY row
	{L"Key_Tab",						0x0000000f},
	{L"Key_Q",							0x00000010},
	{L"Key_W",							0x00000011},
	{L"Key_E",							0x00000012},
	{L"Key_R",							0x00000013},
	{L"Key_T",							0x00000014},
	{L"Key_Y",							0x00000015},
	{L"Key_U",							0x00000016},
	{L"Key_I",							0x00000017},
	{L"Key_O",							0x00000018},
	{L"Key_P",							0x00000019},
	{L"Key_LeftBracket",				0x0000001a},
	{L"Key_RightBracket",				0x0000001b},
	{L"Key_Backslash",					0x0000002b},
	{L"Key_Delete",						0x05000053},
	{L"Key_End",						0x0500004f},
	{L"Key_PageDown",					0x05000051},
	{L"KeyPad_7",						0x00000047},
	{L"KeyPad_8",						0x00000048},
	{L"KeyPad_9",						0x00000049},
	{L"KeyPad_Plus",					0x0000004e},

	// ASDF row
	{L"Key_A",						0x0000001e},
	{L"Key_S",						0x0000001f},
	{L"Key_D",						0x00000020},
	{L"Key_F",						0x00000021},
	{L"Key_G",						0x00000022},
	{L"Key_H",						0x00000023},
	{L"Key_J",						0x00000024},
	{L"Key_K",						0x00000025},
	{L"Key_L",						0x00000026},
	{L"Key_SemiColon",				0x00000027},
	{L"Key_SingleQuote",			0x00000028},
	{L"Key_Enter",					0x0000001c},
	{L"KeyPad_4",					0x0000004b},
	{L"KeyPad_5",					0x0000004c},
	{L"KeyPad_6",					0x0000004d},

	// ZXCVB row
	{L"Key_LeftShift",				0x0000002a},
	{L"Key_Z",						0x0000002c},
	{L"Key_X",						0x0000002d},
	{L"Key_C",						0x0000002e},
	{L"Key_V",						0x0000002f},
	{L"Key_B",						0x00000030},
	{L"Key_N",						0x00000031},
	{L"Key_M",						0x00000032},
	{L"Key_Comma",					0x00000033},
	{L"Key_Period",					0x00000034},
	{L"Key_Slash",					0x00000035},
	{L"Key_RightShift",				0x00000036},
	{L"Key_Up",						0x00000048},
	{L"KeyPad_1",					0x0000004f},
	{L"KeyPad_2",					0x00000050},
	{L"KeyPad_3",					0x00000051},
	{L"KeyPad_Enter",				0x0000001c},

	// modifiers row
	{L"Key_LeftCtrl",				0x0000001d},
	{L"Key_LeftWindows",			0x0000005b},
	{L"Key_LeftAlt",				0x00000038},
	{L"Key_Space",					0x00000039},
	{L"Key_RightAlt",				0x00000038},
	{L"Key_RightWindows",			0x0000005c},
	{L"Key_Application",			0x0000005d},
	{L"Key_RightCtrl",				0x0000001d},
	{L"Key_Left",					0x0000004b},
	{L"Key_Down",					0x00000050},
	{L"Key_Right",					0x0000004d},
	{L"KeyPad_0",					0x00000052},
	{L"KeyPad_DecimalPoint",		0x00000053} };

/*------------------------------------------------------------------
	Globals
------------------------------------------------------------------*/

XMLReader* gReader = NULL;

/*------------------------------------------------------------------
	static ptrotos
------------------------------------------------------------------*/
static bool GetSizeRectFromFile( RECT* outRect, const std::wstring& filePathName );

/*------------------------------------------------------------------
	Create. Factory method to instantiate an object.
------------------------------------------------------------------*/

void XMLButton::Create(void* skin, ISAXAttributes* inAttributes)
{
	XMLButton button(inAttributes);				// Creates the button

	XMLView* view = (XMLView*)((XMLSkin*)skin)->GetView();	// Finds pointer to the current view

	if( view != NULL )
	{
		view->m_buttons.push_back(button);						// Adds button to skin view
	}
	else
	{
		gReader->fatalError( NULL, sSkinEngineXMLSkinEmptyButton, 0xFFFF, NULL );
	}
}

//////////////////////////////////////////////////////////////////////////////////////////
// Functions to set XMLButton attributes
//////////////////////////////////////////////////////////////////////////////////////////
void XMLButton::SetToolTip(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLButton& buttonRef = *reinterpret_cast<XMLButton*>(inButton);

	buttonRef.m_toolTip = inStr;
}

void XMLButton::SetOnClick(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLButton& 		buttonRef = *reinterpret_cast<XMLButton*>(inButton);
wchar_t 		peekChar;
wistringstream	converter( inStr );
int				intToken;
wstring 		token;
bool			found;

	if( !converter.str().empty() )
	{
		while(1)
		{
			peekChar = converter.peek();
			token.erase();

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
				case WEOF:
					// We should have found at least one valid token.
					if( buttonRef.m_onClick.empty() )
					{
						gReader->fatalError(	NULL, sSkinEngineXMLSkinOnClickValue, 0xFFFF, inStr);
					}
					goto Done;

				default:
					found = false;
					
					converter >> token;
					wistringstream	separator( token );
					
					std::getline( separator, token, L':' );	
					
					if( WideStringToIntToken(	token.c_str(),
												keyWStringsToCodes, 									
									 			sizeof(keyWStringsToCodes) / sizeof(TokenListItem),
									   			&intToken ) == true )
					{
						if( intToken == fKeyUp || intToken == fKeyDown )
						{
							unsigned __int32 tokenFlag = intToken; // Save off flag.
							
							peekChar = separator.peek();
													
							// Remove leading whitespace and colon.
							while( peekChar == L':' ||
								   peekChar == L' ' ||
								   peekChar == L'\t' ||
								   peekChar == L'\r' ||
								   peekChar == L'\n' )
							{
								peekChar = separator.get();
								peekChar = separator.peek();
							}

							if( peekChar == WEOF )
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

								if( peekChar == WEOF )
								{
									gReader->fatalError( NULL, sSkinEngineXMLSkinOnClickValue, 0xFFFF, inStr);
									goto Done;
								}
								converter >> token;
							}
							else
							{
								separator >> token;
							}

							if( WideStringToIntToken(	token.c_str(),
														keyWStringsToCodes,
														sizeof(keyWStringsToCodes) / sizeof(TokenListItem),
														&intToken ) == true )
							{
								intToken |= tokenFlag;
								found = true;
							}
							else if( WideStringToInt(token.c_str(), &intToken ) )
							{
								intToken |= tokenFlag;
								found = true;
							}
						}
						else
						{
							found = true;
						}
					}
					else if( WideStringToInt(token.c_str(), &intToken ) )
					{
						found = true;
					}

					if( found == true )
					{
						if( buttonRef.m_onClick.size() < 32 )
						{
							buttonRef.m_onClick.push_back(intToken);
						}
						else
						{
							gReader->fatalError( NULL, sSkinEngineXMLSkinOnClickOverflow, 0xFFFF, inStr);
							goto Done;
						}
					}
					else
					{
						gReader->fatalError( NULL, sSkinEngineXMLSkinOnClickValue,0xFFFF, inStr);
						goto Done;
					}
					break;
			}
		}
	}
Done:;
}

void XMLButton::SetOnPressAndHold(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLButton& 		buttonRef = *reinterpret_cast<XMLButton*>(inButton);
wchar_t 		peekChar;
wistringstream	converter( inStr );
int				intToken;
wstring 		token;
bool			found;

	if( !converter.str().empty() )
	{
		while(1)
		{
			peekChar = converter.peek();
			token.erase();

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
				case WEOF:
					// We should have found at least one valid token.
					if( buttonRef.m_onPressAndHold.empty() )
					{
						gReader->fatalError( NULL, sSkinEngineXMLSkinOnPressAndHoldValue, 0xFFFF, inStr );
					}
					goto Done;

				default:
					found = false;

					converter >> token;
					wistringstream	separator( token );

					std::getline( separator, token, L':' );

					if( WideStringToIntToken(	token.c_str(),
												keyWStringsToCodes,
									 			sizeof(keyWStringsToCodes) / sizeof(TokenListItem),
									   			&intToken ) == true )
					{
						if( intToken == fKeyUp || intToken == fKeyDown )
						{
							unsigned __int32 tokenFlag = intToken; // Save off flag.

							peekChar = separator.peek();

							// Remove leading whitespace and colon.
							while( peekChar == L':' ||
								   peekChar == L' ' ||
								   peekChar == L'\t' ||
								   peekChar == L'\r' ||
								   peekChar == L'\n' )
							{
								peekChar = separator.get();
								peekChar = separator.peek();
							}

							if( peekChar == WEOF )
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
								
								if( peekChar == WEOF )
								{
									gReader->fatalError( NULL, sSkinEngineXMLSkinOnPressAndHoldValue,
														 0xFFFF, inStr);
									goto Done;
								}
								converter >> token;
							}
							else
							{
								separator >> token;
							}

							if( WideStringToIntToken(	token.c_str(),
														keyWStringsToCodes,
														sizeof(keyWStringsToCodes) / sizeof(TokenListItem),
														&intToken ) == true )
							{
								intToken |= tokenFlag;
								found = true;
							}
							else if( WideStringToInt(token.c_str(), &intToken ) )
							{
								intToken |= tokenFlag;
								found = true;
							}
						}
						else
						{
							found = true;
						}
					}
					else if( WideStringToInt(token.c_str(), &intToken ) )
					{
						found = true;
					}

					if( found == true )
					{
						if( buttonRef.m_onPressAndHold.size() < 32 )
						{
							buttonRef.m_onPressAndHold.push_back(intToken);
						}
						else
						{
							gReader->fatalError( NULL,sSkinEngineXMLSkinOnPressAndHoldOverflow, 0xFFFF, inStr);
							goto Done;
						}
					}
					else
					{
						gReader->fatalError( NULL, sSkinEngineXMLSkinOnPressAndHoldValue, 0xFFFF, inStr);
						goto Done;
					}
					break;
			}
		}
	}
Done:;
}


void XMLButton::SetMappingColor(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLButton& buttonRef = *reinterpret_cast<XMLButton*>(inButton);
INT32 mappingColor;

	if( WideStringToInt(inStr, &mappingColor ) == false )
	{
		gReader->fatalError( NULL, sSkinEngineXMLSkinMappingColorValue, 0xFFFF, inStr);
	}
	else
	{
		buttonRef.m_mappingColor = mappingColor;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////
// Structure defines handlers for button attributes encountered
//////////////////////////////////////////////////////////////////////////////////////////
const AttributeInfo kButtonAttributes[] =
{
	{
		L"toolTip",
		XMLButton::SetToolTip
	},

	{
		L"onClick",
		XMLButton::SetOnClick
	},

	{
		L"onPressAndHold",
		XMLButton::SetOnPressAndHold
	},

	{
		L"mappingColor",
		XMLButton::SetMappingColor
	}
};

const int kNumButtonAttributes = sizeof(kButtonAttributes) / sizeof(AttributeInfo);

//////////////////////////////////////////////////////////////////////////////////////////
// c'tor
//////////////////////////////////////////////////////////////////////////////////////////
XMLButton::XMLButton(ISAXAttributes* inAttributes)
{
	int numAttributes;

	inAttributes->getLength(&numAttributes);
	for ( int lc = 0; lc < numAttributes; lc++ )
	{
		wchar_t* attributeName;
		wchar_t* attributeValue;

		int attributeNameLength;
		int attributeValueLength;

		inAttributes->getLocalName(lc, const_cast<const wchar_t**>( &attributeName ), &attributeNameLength);
		inAttributes->getValue(lc, const_cast<const wchar_t**>( &attributeValue ), &attributeValueLength);

		int index;
		for (index = 0;  index < kNumButtonAttributes  &&  _wcsnicmp(attributeName, kButtonAttributes[index].name, attributeNameLength);  ++index) {}

		if (index < kNumButtonAttributes)
			kButtonAttributes[index].Handler(this, attributeValue, attributeName);
		//else
			// FIX ME: Unknown tag attribute[name].
	}
}


//////////////////////////////////////////////////////////////////////////////////////////
// Factory
//////////////////////////////////////////////////////////////////////////////////////////
void XMLView::Create(void* skin, ISAXAttributes* inAttributes)
{
	if( ((XMLSkin*)skin)->GetView() == NULL )
	{
		XMLView* view = new XMLView( inAttributes );	// Creates XMLView
		((XMLSkin*)skin)->SetView( view );				// Adds view to skin
	}
	else
	{
		gReader->fatalError( NULL, sSkinEngineXMLSkinMultipleViews, 0xFFFF, NULL);
	}
}

//////////////////////////////////////////////////////////////////////
// Functions to set XMLView attributes
//////////////////////////////////////////////////////////////////////

void XMLView::SetTitleBar( void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& buttonRef = *reinterpret_cast<XMLView*>(inView);

	buttonRef.m_titleBar = inStr;
}

void XMLView::SetDisplayXPos(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);

	if( WideStringToInt(inStr, &viewRef.m_displayXPos) == false )
	{
		gReader->fatalError( NULL, sSkinEngineXMLSkinDisplayXPosValue, 0xFFFF, inStr);
	}
}

void XMLView::SetDisplayYPos(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);

	if( WideStringToInt(inStr, &viewRef.m_displayYPos) == false )
	{
		gReader->fatalError( NULL, sSkinEngineXMLSkinDisplayYPosValue, 0xFFFF, inStr);
	}
}

void XMLView::SetDisplayWidth(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);
int displayWidth;

	if( WideStringToInt(inStr, &displayWidth) == true )
	{
		if( displayWidth < 0 )
		{
			gReader->fatalError( NULL, sSkinEngineXMLSkinInvalidDisplayWidth, 0xFFFF, NULL);
		}
		else
		{
			viewRef.m_displayWidth = displayWidth;
		}
	}
	else
	{
		gReader->fatalError( NULL, sSkinEngineXMLSkinDisplayWidthValue, 0xFFFF, inStr);
	}
}

void XMLView::SetDisplayHeight(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);
int displayHeight;

	if( WideStringToInt(inStr, &displayHeight) == true )
	{
		if( displayHeight < 0 )
		{
			gReader->fatalError( NULL, sSkinEngineXMLSkinInvalidDisplayHeight, 0xFFFF, NULL);
		}
		else
		{
			viewRef.m_displayHeight = displayHeight;
		}
	}
	else
	{
		gReader->fatalError( NULL, sSkinEngineXMLSkinDisplayHeightValue, 0xFFFF, inStr);
	}
}

void XMLView::SetDisplayDepth(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);
int displayDepth;

	if( WideStringToInt(inStr, &displayDepth) == true )
	{
		if( displayDepth < 0 )
		{
			gReader->fatalError( NULL, sSkinEngineXMLSkinInvalidDisplayDepth, 0xFFFF, NULL);
		}
		else
		{
			viewRef.m_displayDepth = displayDepth;
		}
	}
	else
	{
		gReader->fatalError( NULL, sSkinEngineXMLSkinDisplayDepthValue, 0xFFFF, inStr);
	}
}

void XMLView::SetMappingImage(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);

	viewRef.m_mappingImage = inStr;
}

void XMLView::SetNormalImage(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);

	viewRef.m_normalImage = inStr;
}

void XMLView::SetDownImage(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);

	viewRef.m_downImage = inStr;
}

void XMLView::SetFocusImage(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);

	viewRef.m_focusImage = inStr;
}

void XMLView::SetDisabledImage(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* /*inAtt*/)
{
XMLView& viewRef = *reinterpret_cast<XMLView*>(inView);

	viewRef.m_disabledImage = inStr;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Structure defines handlers for view attributes encountered
//////////////////////////////////////////////////////////////////////////////////////////
const AttributeInfo kViewAttributes[] =
{
	{
		L"titleBar",
		XMLView::SetTitleBar
	},

	{
		L"displayPosX",
		XMLView::SetDisplayXPos
	},

	{
		L"displayPosY",
		XMLView::SetDisplayYPos
	},

	{
		L"displayWidth",
		XMLView::SetDisplayWidth
	},

	{
		L"displayHeight",
		XMLView::SetDisplayHeight
	},

	{
		L"displayDepth",
		XMLView::SetDisplayDepth
	},

	{
		L"mappingImage",
		XMLView::SetMappingImage
	},
	
	{
		L"normalImage",
		XMLView::SetNormalImage
	},

	{
		L"downImage",
		XMLView::SetDownImage
	},

	{
		L"focusImage",
		XMLView::SetFocusImage
	},

	{
		L"disabledImage",
		XMLView::SetDisabledImage
	},
};

const int kNumViewAttributes = sizeof(kViewAttributes) / sizeof(AttributeInfo);

//////////////////////////////////////////////////////////////////////////////////////////
// c'tor
//////////////////////////////////////////////////////////////////////////////////////////
XMLView::XMLView(ISAXAttributes* inAttributes)
{
	int numAttributes;
	inAttributes->getLength(&numAttributes);

	for ( int lc = 0; lc < numAttributes; lc++ )
	{
		wchar_t* attributeName;
		wchar_t* attributeValue;

		int attributeNameLength;
		int attributeValueLength;

		inAttributes->getLocalName(lc, const_cast<const wchar_t**>( &attributeName ), &attributeNameLength);
		inAttributes->getValue(lc, const_cast<const wchar_t**>( &attributeValue ), &attributeValueLength);

		int index;
		for (index = 0;  index < kNumViewAttributes  &&  _wcsnicmp(attributeName, kViewAttributes[index].name, attributeNameLength);  ++index) {}
		
		if( index != kNumViewAttributes )
			kViewAttributes[index].Handler(this, attributeValue, attributeName);
	}
}

// Structure defines the possible tag names and the XML parsing handlers for each tag
static TagInfo skinXMLTags[] =
{
	{
		L"button",
		XMLButton::Create
	},

	{
		L"view",
		XMLView::Create
	},

	{
		L"skin",
		NULL
	}
};

const int kNumTags = sizeof(skinXMLTags) / sizeof(TagInfo);

//////////////////////////////////////////////////////////////////////////////////////////
// Exported (de)allocators
//////////////////////////////////////////////////////////////////////////////////////////
XMLSkin* ExCreateSkin( void )
{
	return new XMLSkin;
}

void ExDeleteSkin( XMLSkin* inSkin )
{
	::delete reinterpret_cast<XMLSkin*>(inSkin);
}

//////////////////////////////////////////////////////////////////////////////////////////
// c'tor
//////////////////////////////////////////////////////////////////////////////////////////
XMLSkin::XMLSkin()
{
	m_view = NULL;
}

XMLSkin::~XMLSkin()
{
	if( m_view != NULL )
	{
		::delete m_view;
	}
}


bool XMLSkin::Load( const wchar_t * inFullFilePath )
{
	bool success = false;
	// Save off the skin file path for use by the CSkinWindow setup code.
	wstring tempString = inFullFilePath;
    wstring fulFilePath;
	
	gReader = new XMLReader(inFullFilePath, skinXMLTags, kNumTags, this);
	
	if (gReader ==  NULL)
	{
		TerminateWithMessage(ID_MESSAGE_INTERNAL_ERROR);
	}

	if( gReader->GetError() != NO_ERROR )
	{
		if (FAILED(gReader->m_error))
			gReader->fatalError( NULL, sSkinEngineXMLSkinNoView, 0xFFFF, NULL);
		goto Done;
	}

	gReader->Parse();
	
	if( gReader->GetError() != NO_ERROR )
	{
		if (FAILED(gReader->m_error))
			gReader->fatalError( NULL, sSkinEngineXMLSkinNoView, 0xFFFF, NULL);
		goto Done;
	}

	int charIndex = (int)tempString.rfind('\\');
	if (charIndex != (int)string::npos )
		m_skinPath = tempString.substr(0, charIndex+1);
	else
		m_skinPath = L"";

	XMLView* view = (XMLView*)GetView();
	
	if( view == NULL )
	{
		if (FAILED(gReader->m_error))
			gReader->fatalError( NULL, sSkinEngineXMLSkinNoView, 0xFFFF, NULL);
		goto Done;
	}

	if( view->m_normalImage.rfind('\\') == string::npos &&
		view->m_normalImage.rfind(':') == string::npos )
		fulFilePath = m_skinPath + view->m_normalImage;
	else
		fulFilePath = view->m_normalImage;

	if( view->m_normalImage.empty() || GetSizeRectFromFile( &m_skinRect, fulFilePath ) == false )
	{
		gReader->fatalError( NULL, sSkinEngineXMLSkinInvalidOrEmptyNormalImage, 0xFFFF, NULL);
		goto Done;
	}

	success = true;

Done:;
	if( gReader != NULL )
	{
		::delete gReader;
	}	
	return success;
}

bool XMLSkin::GetSizeRect( RECT* outRect )
{
	*outRect = m_skinRect;
	return ( m_skinRect.bottom >= m_skinRect.top || m_skinRect.left >= m_skinRect.right );
}

bool XMLSkin::GetDisplayRect( RECT* outRect )
{
	bool succeeded = false;

	outRect->left = outRect->top = outRect->right = outRect->bottom = 0;
	
	XMLView* view = (XMLView*)GetView();

	if( view != NULL )
	{
		outRect->left = view->m_displayXPos;
		outRect->top = view->m_displayYPos;
		outRect->right = view->m_displayXPos + view->m_displayWidth;
		outRect->bottom = view->m_displayYPos + view->m_displayHeight;
			
		succeeded = true;
	}
	return succeeded;
}

unsigned __int32	XMLSkin::GetDisplayDepth( void )
{
	XMLView* view = (XMLView*)GetView();
	
	if( view != NULL )
		return view->m_displayDepth;
	
	return 0;
}

XMLView*	XMLSkin::GetView( void )
{
	return m_view;
}

void	XMLSkin::SetView( XMLView* inView )
{
	m_view = inView;
}

bool GetSizeRectFromFile( RECT* outRect, const wstring& filePathName )
{
	bool succeeded = false;

	if( outRect != NULL )
	{
		outRect->left = outRect->top = outRect->right = outRect->bottom = 0;
		
		// Build a graphical button...
		HBITMAP hBitmap;
		bool ignored = false;
		if(LoadBitmapFromBMPFile( filePathName, &hBitmap, ignored ))
		{
			BITMAP  bm;
			if (GetObject(hBitmap, sizeof(BITMAP), (LPSTR)&bm) )
			{
				// calc new window size an coords
				outRect->left = 0;
				outRect->top = 0;
				outRect->right = bm.bmWidth;
				outRect->bottom = bm.bmHeight;

				succeeded = true;
			}
			else
			{
				DWORD error = GetLastError();
				LOG_ERROR(NETWORK, "Failed to obtain information about skin image while loading the skin - %x", error);
				ASSERT(false && "Failed to obtain information about skin image");
			}

			DeleteObject( hBitmap );
		}
	}
	return succeeded;
}
