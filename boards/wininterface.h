/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef WININTERFACE__H_
#define WININTERFACE__H_
 
#include "mappedio.h"
#include "XMLSkin.h"

typedef  bool (*MENUCALLBACK)(HWND , UINT, WPARAM, LPARAM);
 
struct MenuItemRecord {
    unsigned short message;
    MENUCALLBACK callback;
};

struct ButtonRecord {
    ISkinRegionButton * button;
    ButtonRecord * NextButton;
};

#define MENUITEM_MAX 20

class IOWinController : public MappedIODevice, public WinController
{
	friend class WinController;

public:
	IOWinController();
	~IOWinController();

	bool __fastcall RegisterMenuCallback( unsigned short message, MENUCALLBACK func );
        void __fastcall EnableUIMenuItem( unsigned int itemID );
        void __fastcall DisableUIMenuItem( unsigned int itemID );

	bool CreateLCDThread(void);
	bool CreateLCDDMAThread(void);

	bool VerifyUpdate( EmulatorConfig * OriginalConf, EmulatorConfig * NewConf);

	virtual void SetZoom(bool fZoom);
	virtual void SetTopmost(bool fTopmost);
	virtual void ShowSkin(bool fShow);
	virtual void SetRotation(unsigned int mode);
	virtual void ToggleToolTip( ToolTipStateEnum req_state );

	void ShowWindow(void);
	void HideWindow(void);

	// Methods for interaction with buttons
	__inline HWND getToolTip() { return hwndTip; }
	__inline HWND setToolTip(HWND handle) { return hwndTip = handle; }
	__inline HBITMAP * getSkinBitmaps() { return (HBITMAP *)&m_bitmapHandles; }
	void translateFromViewToMap( LONG & x, LONG & y );
	void translateFromMapToView( LONG & x, LONG & y );
	void QueueKeyEvent( unsigned __int32 keymap, unsigned __int32 event );
	void setKeyState( unsigned __int8 vk_code ) { KeyState[vk_code] = 1; }
	void clearKeyState( unsigned __int8 vk_code ) { KeyState[vk_code] = 0; }
	void shutDown();

	__inline virtual unsigned __int32 ScreenX(void) { ASSERT(false); return 0;}
	__inline virtual unsigned __int32 ScreenY(void) { ASSERT(false); return 0;}

	// MappedIODevice methods
	virtual bool __fastcall PowerOn();

	virtual void __fastcall SaveState(StateFiler& filer) const = 0;
	virtual void __fastcall RestoreState(StateFiler& filer) = 0;
	virtual bool __fastcall Reconfigure(__in_z const wchar_t * NewParam);


	virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value) = 0;
	virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress) = 0;

protected:
	static DWORD WINAPI LCDThreadProcStatic(LPVOID lpvThreadParam);
	DWORD WINAPI LCDThreadProc();

	static DWORD WINAPI LCDDMAThreadProcStatic(LPVOID lpvThreadParam);
	virtual DWORD WINAPI LCDDMAThreadProc(void) = 0;

	static LRESULT CALLBACK LCDWndProcStatic(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK LCDWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	static LRESULT CALLBACK LowLevelKeyboardProcStatic(int nCode, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

private:
	bool processSkinEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	void onSkinPaint(PAINTSTRUCT & ps);
	void onSkinResize();
	void onSkinChangeFocus(HWND hWnd, UINT uMsg, WPARAM wParam);

	void AdjustWindowSize(void);
	void AdjustTopmostSetting(void);
	void DrawLoadingScreen(void);
	void FillExcessSpace(PAINTSTRUCT & ps);
	void clearSkin();
	void updateTransform();
	void getSkinView( RECT & view );
	void readSkinInfo();

	static const LPCWSTR WindowClassName;

	LPARAM translateEmulatorCoord( LPARAM lp );
	LPARAM translateSkinCoord( LPARAM lp );
	bool isEmulatorCoord(LPARAM lp );

	// Display state variables
	RECT skinRect;              // Mapped skin rectangle
	RECT LCDRect;               // Mapped LCD rectangle
	RECT skinLCD;               // The LCD position and size specified in the skin
	wchar_t * screenCaption;    // The screen title
	unsigned int skinLCDDepth;  // The depth of the LCD specified in the skin
	bool OrientationBMP[4];     // Specifies if the skin bmps are top-down

	XMLSkin *Skin;

	double DispScale;           // View is scaled by DispScale

	bool SkinEnabled;           // Skin is currently displayed
	bool SkinInitialized;       // Skin is ready to be display
	bool SkinMayHaveFocus;      // Skin may have focus

	ButtonRecord * m_buttons;

	// Menu state variables
	static MenuItemRecord MenuRecords[MENUITEM_MAX];
	static unsigned __int32 MenuRecordCount;

	// Array representing the current state keyboard as visible to the guest OS
	BYTE KeyState[256];

	DWORD m_wndProcThreadId;    // Thread ID of the thread running the window proc
protected:

	virtual bool   LCDEnabled() { ASSERT(false); return true; }
	virtual size_t getFrameBuffer() { ASSERT(false); return 0; }
	void getDeviceView( RECT & view );

	HBITMAP hBitMap;
	HHOOK hhkLowLevelKybd;
	HMENU hMenu;
	HANDLE LCDEnabledEvent;
	HPALETTE hPalette;

	// Event callbacks
	virtual void onWM_LBUTTONDOWN( LPARAM lParam ) {ASSERT(false);}
	virtual void onWM_MOUSEMOVE( LPARAM lParam ) {ASSERT(false);}
	virtual void onWM_LBUTTONUP( LPARAM lParam ) {ASSERT(false);}
	virtual void onWM_CAPTURECHANGED() {ASSERT(false);}
	virtual void onWM_SETFOCUS();
	virtual void onWM_KILLFOCUS();
	virtual void onWM_INITMENU();
	virtual void onWM_PAINT(HWND hWnd);
	virtual void onWM_CREATE(HWND hWnd);
	virtual void onWM_SYSKEYUP(unsigned __int8 & KeyUpDown) { KeyUpDown=0; }
	virtual void onWMKEY( unsigned __int8 vKey, unsigned __int8 & KeyUpDown ) {ASSERT(false);}
};

#endif // WININTERFACE__H_
