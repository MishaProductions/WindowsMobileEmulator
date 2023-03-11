/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef XMLSKIN_TAG_H
#define XMLSKIN_TAG_H
#pragma once

#include "emulator.h"

class StateFiler;

#pragma warning (disable:4995) // name was marked as #pragma deprecated - triggered by strsafe.h
#include <commctrl.h> // For tooltips
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#pragma warning(default:4995)

// Constant definitions
const unsigned __int32 fKeyDown = 0x80000000;
const unsigned __int32 fKeyUp = 0x40000000;

class XMLButton
{
public:
	XMLButton(ISAXAttributes* inAttributes);

	static void Create(void* skin, ISAXAttributes* inAttributes);

	static void SetToolTip(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetOnClick(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetOnPressAndHold(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetMappingColor(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);

	std::wstring			m_toolTip;
	std::vector<unsigned __int32>		m_onClick;
	std::vector<unsigned __int32>		m_onPressAndHold;

	COLORREF		m_mappingColor;
};

class XMLView
{
public:

	XMLView(ISAXAttributes* inAttributes);

	static void Create(void* skin, ISAXAttributes* inAttributes);

	static void SetTitleBar( void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetDisplayXPos(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetDisplayYPos(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetDisplayWidth(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetDisplayHeight(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetDisplayDepth(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);

	static void	SetMappingImage(void* inView, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetNormalImage(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetDownImage(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetFocusImage(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);
	static void SetDisabledImage(void* inButton, __in_z const wchar_t* inStr, __in_opt wchar_t* inAtt);

	std::wstring	m_titleBar;

	int m_displayXPos;
	int m_displayYPos;

	int m_displayWidth;
	int m_displayHeight;
	
	int m_displayDepth;
	
	std::wstring			m_mappingImage;
	std::wstring			m_normalImage;
	std::wstring			m_downImage;
	std::wstring			m_focusImage;
	std::wstring			m_disabledImage;

	// XMLSkin buttons
	std::vector<XMLButton>	m_buttons;
};



class XMLSkin //: public IXMLSkin
{
public:
	XMLSkin();
	virtual ~XMLSkin();
	
	bool 	Load( const wchar_t * inFullFilePath );
	
	bool 	GetSizeRect( RECT* outRect );
	bool 	GetDisplayRect( RECT* outRect );
	unsigned __int32	GetDisplayDepth( void );
	
	XMLView*	GetView( void );
	void		SetView( XMLView* );
	
	std::wstring	m_skinPath;
	XMLView*		m_view;
	
	RECT			m_skinRect;
};

class WinController;

enum SkinButtonState { Normal, Push, Focus, Disabled };

#define IDT_TIMERKEYREPEAT 0

class ISkinRegionButton
{
public:
	virtual void SaveState(StateFiler& filer) = 0;
	virtual void RestoreState(StateFiler& filer) = 0;
	static bool UseBitmaps( HBITMAP hBitmapNormal, HBITMAP hBitmapSel = NULL,
							HBITMAP hBitmapFocus = NULL, HBITMAP hBitmapDisabled = NULL );
	virtual void SetRegion( HBITMAP inBitmap, COLORREF inColorRef ) = 0;

	// Event handling
	virtual void OnLButtonDown(HWND hWnd, UINT uMsg, LPARAM point) = 0;
	virtual void OnLButtonUp(HWND hWnd, UINT uMsg, LPARAM point) = 0;
	virtual void OnMouseMotion(HWND hWnd, UINT uMsg, LPARAM point) = 0;
	virtual void OnPaint(PAINTSTRUCT ps, XFORM * transform) = 0;
	virtual void OnResize(XFORM * transform) = 0;
	virtual void OnKillFocus(HWND hWnd) = 0;
	virtual void OnTimer(HWND hWnd) = 0;

	virtual void setState( SkinButtonState state ) = 0;
	virtual void initToolTip( HWND hWnd, int id, const wchar_t * tip ) = 0;

	virtual SkinButtonState getState() = 0;
	virtual TOOLINFO * getToolTipInfo() = 0;

	// Per instance data exposed to make interaction with WinController simpler
	std::vector<unsigned __int32>	m_onClick;
	std::vector<unsigned __int32>	m_onPressAndHold;
};

ISkinRegionButton * CreateSkinRegionButton(WinController * controller);

bool LoadBitmapFromBMPFile( const std::wstring& fileName, HBITMAP *phBitmap, bool & TopDown );

#endif //	XMLSKIN_TAG_H

