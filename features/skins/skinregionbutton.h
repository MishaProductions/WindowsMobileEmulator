/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef SKINREGIONBUTTON_H
#define SKINREGIONBUTTON_H

class WinController;

#define InitialPeriod   500 
#define KeyRepeatPeriod 50

class SkinRegionButton : public ISkinRegionButton
{
	friend class ISkinRegionButton;
	friend ISkinRegionButton* CreateSkinRegionButton(WinController * controller);

public:
	~SkinRegionButton();
	void SaveState(StateFiler& filer);
	void RestoreState(StateFiler& filer);

	static bool UseBitmaps( HBITMAP hBitmapNormal, HBITMAP hBitmapSel = NULL,
							HBITMAP hBitmapFocus = NULL, HBITMAP hBitmapDisabled = NULL );
	void SetRegion( HBITMAP inBitmap, COLORREF inColorRef );

	// Event handling
	void OnLButtonDown(HWND hWnd, UINT uMsg, LPARAM point);
	void OnLButtonUp(HWND hWnd, UINT uMsg, LPARAM point);
	void OnMouseMotion(HWND hWnd, UINT uMsg, LPARAM point);
	void OnPaint(PAINTSTRUCT ps, XFORM * transform);
	void OnResize(XFORM * transform);
	void OnKillFocus(HWND hWnd);
	void OnTimer(HWND hWnd);

	void setState( SkinButtonState state ) { m_state = state; }
	void initToolTip( HWND hWnd, int id, const wchar_t * tip );

	SkinButtonState getState() { return m_state; }
	TOOLINFO * getToolTipInfo() { return &m_ToolTipInfo;}

private:
	SkinRegionButton(WinController * controller);
	HRGN transformBoundingRegion(XFORM * t);
	bool getButtonRegionData(RGNDATAHEADER ** RegionDataHeaderOut, RECT ** RectanglesOut,
							 unsigned int & RegionDataSize );
	void updateTooltip();

	// Per instance data
	WinController * 		m_controller;
	SkinButtonState 		m_state;
	bool 					m_activeToolTip;
	HRGN					m_region;
	HRGN					m_scaledRegion;
	RECT					m_boundingRect;
	RECT					m_scaledBoundingRect;
	TOOLINFO 				m_ToolTipInfo;
	wchar_t *				m_caption;
	bool 					m_continueRepeat;
	bool 					m_repeatDetected;
};

#endif	// SKINREGIONBUTTON_H

