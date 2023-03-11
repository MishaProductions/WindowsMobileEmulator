/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "emulator.h"
#include "Config.h"
#include "resource.h"
#include "WinController.h"
#include "WinInterface.h"
#include "Board.h"
#include "XMLSkin.h"

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "wininterface.tmh"
#include "vsd_logging_inc.h"

WinController * WinCtrlInstance;
IOWinController* IOWinCtrlInstance;

unsigned __int32 IOWinController::MenuRecordCount;
MenuItemRecord IOWinController::MenuRecords[MENUITEM_MAX];

extern bool ShowHelp( __inout_z wchar_t * buffer );

IOWinController::IOWinController()
:DispScale(1.0),SkinEnabled(false),SkinInitialized(false),
SkinMayHaveFocus(false),m_buttons(NULL),
hPalette(0), hhkLowLevelKybd(NULL), hBitMap(0), screenCaption(NULL),
Skin(NULL)
{
    // Initialize static variables (only one instance of this class is assumed)
    WinCtrlInstance = NULL;
    IOWinCtrlInstance = NULL;
    MenuRecordCount = 0;
    hWnd = NULL;
    hwndTip = NULL;

    // Reset the mapping
    skinTransform.eM11 = 1; skinTransform.eM12 =0;
    skinTransform.eM21 = 0; skinTransform.eM22 =0;
    skinTransform.eDx =0; skinTransform.eDy = 0;

    // Reset the keyboard state
    ASSERT( sizeof(KeyState) % sizeof(int) == 0 );
    for( int i = 0; i < sizeof(KeyState)/sizeof(int); i++)
        ((int *)KeyState)[i] = 0;
}

IOWinController::~IOWinController()
{
    // Delete the skin
    //clearSkin();

    // Clear out the screen caption
    if (screenCaption != NULL)
        delete [] screenCaption;
}

bool IOWinController::CreateLCDThread(void)
{
	HANDLE hThread;
	hThread=CreateThread(NULL, 0, IOWinController::LCDThreadProcStatic, this, 0, &m_wndProcThreadId);
	if (hThread == INVALID_HANDLE_VALUE) {
		return false;
	}
	
	CloseHandle(hThread);
	// block here until the display window is really alive.
	while (!hWnd) {
		Sleep(10);
	}
	
	return true;
}

bool IOWinController::CreateLCDDMAThread(void)
{
	HANDLE hThread;
	DWORD dwThreadId;

	hThread=CreateThread(NULL, 0, IOWinController::LCDDMAThreadProcStatic, this, 0, &dwThreadId);
	if (hThread == INVALID_HANDLE_VALUE) {
		return false;
	}

	CloseHandle(hThread);
	return true;
}

bool __fastcall IOWinController::PowerOn()
{
	if (Configuration.isSkinFileSpecified() && Configuration.fShowSkin &&
		!SkinInitialized) {
		Skin = new  XMLSkin;

		if (!Skin || !Skin->Load(Configuration.getSkinXMLFile())) {
			return false;
		}

		// Read the skin size
		readSkinInfo();
	}

	// There should be only one instance of this class
	WinCtrlInstance = this;
	IOWinCtrlInstance = this;

	LCDEnabledEvent=CreateEvent(NULL, FALSE, FALSE, NULL);
	if (LCDEnabledEvent == NULL) {
		return false;
	}

	if (Configuration.IsZoomed) {
		DispScale = 2.0;
	}
	return true;
}

bool IOWinController::VerifyUpdate( EmulatorConfig * OriginalConf, EmulatorConfig * NewConf)
{
    // Read the data from the original configuration
    ASSERT( OriginalConf != NULL && NewConf != NULL);
    unsigned int height = OriginalConf->ScreenHeight;
    unsigned int width  = OriginalConf->ScreenWidth;
    unsigned int depth  = OriginalConf->ScreenBitsPerPixel;

    // Check if the new configuration can be reapplied
    if (NewConf->isSkinFileSpecified())
    {
        bool match = false;
        // Either the skin is already intialized or we have to load it
        XMLSkin * NewSkin = new  XMLSkin;
        if (NewSkin == NULL || !NewSkin->Load(NewConf->getSkinXMLFile()) )
            goto DoneSkinVerify;
        // We failed to parse the skin file
        if (NewSkin->m_view == NULL)
            goto DoneSkinVerify;

        match = ((unsigned int)NewSkin->m_view->m_displayHeight == height &&
                 (unsigned int)NewSkin->m_view->m_displayWidth  == width &&
                 (unsigned int)NewSkin->m_view->m_displayDepth  == depth );

    DoneSkinVerify:
        if (NewSkin != NULL )
            delete NewSkin;
        return match;
    }
    else if (NewConf->isVideoSpecified() )
    {
        return ( (unsigned int)NewConf->ScreenHeight == height &&
                 (unsigned int)NewConf->ScreenWidth  == width &&
                 (unsigned int)NewConf->ScreenBitsPerPixel == depth );
    }

    // Assume global cache images have the default sizes for the LCDs
    return true;
}
void __fastcall IOWinController::SaveState(StateFiler& filer) const
{
    filer.Write('UWIN');
    // Display state variables
    filer.Write(&skinRect, sizeof(RECT));
    filer.Write(&LCDRect, sizeof(RECT));
    filer.Write(&skinLCD, sizeof(RECT));
    filer.WriteString(screenCaption);
    filer.Write(skinLCDDepth);

    filer.Write(SkinEnabled);
    filer.Write(SkinInitialized);

    // Skin images
    for (int i = 0; i < ARRAY_SIZE(m_bitmapHandles); i++)
    {
        if (m_bitmapHandles[i] != NULL)
        {
            filer.Write(1);
            BITMAP bm;
            GetObject(m_bitmapHandles[i], sizeof(bm), &bm);
            unsigned int sizeBitmap = bm.bmWidthBytes*bm.bmHeight;
            // Check if the image is a top-down DIB
            if (OrientationBMP[i])
            {
                bm.bmHeight = -bm.bmHeight;
            }
            filer.Write(&bm, sizeof(bm));
            filer.LZWrite((unsigned char *)bm.bmBits, sizeBitmap);
        }
        else
        {
            filer.Write(NULL);
        }
    }

    // Skin buttons
    unsigned int ButtonCount = 0;
    ButtonRecord * iter = m_buttons;
    while (iter != NULL )
    {
        ButtonCount++;
        iter = iter->NextButton;
    }
    filer.Write(ButtonCount);
    iter = m_buttons;
    while (iter != NULL )
    {
        iter->button->SaveState(filer);
        iter = iter->NextButton;
    }

    // Skin - unnecessary
    // SkinMayHaveFocus - focus is lost across restore
    // DispScale - can be figured out from Configuration.Zoom
    // skinTransform - recalculated after restore
    // ButtonRecord * m_buttons;
    // KeyState[256] - all keys are released across saved state
    // static MenuItemRecord MenuRecords[MENUITEM_MAX] - reregistered on restore
    // static unsigned __int32 MenuRecordCount  - reregistered on restore
    // OrientaionBMP[i] - recreated from the image height
}

void __fastcall IOWinController::RestoreState(StateFiler& filer)
{
    filer.Verify('UWIN');
    filer.Read(&skinRect, sizeof(RECT));
    filer.Read(&LCDRect, sizeof(RECT));
    filer.Read(&skinLCD, sizeof(RECT));
    filer.ReadString(screenCaption);
    filer.Read(skinLCDDepth);

    // Skin - unnecessary
    // DispScale - can be figured out from Configuration.Zoom

    filer.Read(SkinEnabled);
    filer.Read(SkinInitialized);

    // Skin Images
    for (int i = 0; i < ARRAY_SIZE(m_bitmapHandles); i++)
    {
        DWORD included;
        filer.Read(included);
        if (included && filer.getStatus())
        {
            BITMAP bm;
            filer.Read(&bm, sizeof(bm));

            // Create windows bitmap
            HDC hDC;
            hDC = GetDC(NULL);
            if (!hDC)
            {
                ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
                exit(1);
            }

            char BMIData[sizeof(BITMAPINFO)+5*sizeof(RGBQUAD)];
            BITMAPINFO *bmi = (BITMAPINFO*)BMIData;
            memset(bmi, 0, sizeof(BITMAPINFO));
            bmi->bmiHeader.biSize       = sizeof(BITMAPINFO);
            bmi->bmiHeader.biWidth      = bm.bmWidth;
            bmi->bmiHeader.biHeight     = bm.bmHeight;
            bmi->bmiHeader.biPlanes     = bm.bmPlanes;
            bmi->bmiHeader.biBitCount   = bm.bmBitsPixel;
            bmi->bmiHeader.biCompression= BI_RGB;

            *(unsigned __int32*)&bmi->bmiColors[0] = 0xffffff;
            *(unsigned __int32*)&bmi->bmiColors[1] = 0xffffff;

            unsigned __int8 * image_dest = NULL;
            m_bitmapHandles[i] = CreateDIBSection(hDC, bmi, DIB_RGB_COLORS,
                                                  (LPVOID*)&image_dest, NULL, 0);

            if ( image_dest == NULL  || m_bitmapHandles[i] == NULL )
            {
                TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
            }
            // Deal with image orientation
            unsigned int sizeBitmap = bm.bmWidthBytes*
                                     (bm.bmHeight > 0 ? bm.bmHeight : (-bm.bmHeight));
            OrientationBMP[i] = (bm.bmHeight > 0 ? false : true);

            filer.LZRead(image_dest, sizeBitmap);
            ReleaseDC(NULL,hDC);
        }
        else
        {
            m_bitmapHandles[i] = NULL;
        }
    }

    // Buttons - tool tips are restored on WM_CREATE
    //           and boundaries are scaled when LCD controller calls show window
    unsigned int ButtonCount = 0;
    filer.Read(ButtonCount);
    for ( unsigned int i = 0; i < ButtonCount && filer.getStatus(); i++ )
    {
        ISkinRegionButton * button = CreateSkinRegionButton(this);
        if (button == NULL)
        {
            TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
        }
        button->RestoreState(filer);

        ButtonRecord * ButtonRec = new ButtonRecord;
        if (ButtonRec == NULL)
        {
            TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
        }
        ButtonRec->button = button;
        ButtonRec->NextButton = m_buttons;
        m_buttons = ButtonRec;
    }

    if ( Configuration.UseUpdatedSettings && SkinInitialized )
        clearSkin();
}

// This is called by the configuration UI, when the emulator is
// running, to change which skin is being displayed
bool __fastcall IOWinController::Reconfigure(__in_z const wchar_t * NewParam)
{
    // This function must be called on the window proc thread because it deletes window
    // resources that can only be deleted on that thread
    ASSERT( GetCurrentThreadId() == m_wndProcThreadId );

    // Handle the trivial case: the skin is going away
    if (NewParam == NULL) {
        // If the skin is being deleted the screen size should initialized
        ASSERT( Configuration.ScreenWidth != 0 && Configuration.ScreenHeight != 0);
        // Make sure the video size fits the skin
        ASSERT ( !Configuration.isSkinFileSpecified() ||
                 (Configuration.ScreenWidth  == (skinLCD.right - skinLCD.left) &&
                  Configuration.ScreenHeight == (skinLCD.bottom - skinLCD.top)));
        clearSkin();
        // Update the file path to the new skin
        Configuration.setSkinXMLFile(NewParam);
        return true;
    }

    // Allocate the new skin
    XMLSkin *NewSkin = new  XMLSkin;
    if (!NewSkin) {
        ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
        return false;
    }

    if (!NewSkin->Load(NewParam)) {
        goto Error;
    }

    if (Configuration.isSkinFileSpecified()) {
        // Now confirm that the new skin's size is compatible (
        // equal to) with the old one's.If not, we'll have to fail the reconfiguration
        // since WinCE doesn't support resizing the display at run-time
        if (NewSkin->m_view->m_displayWidth != (skinLCD.right - skinLCD.left)) {
            ShowDialog(ID_MESSAGE_SKIN_WRONG_SIZE);
            goto Error;
        }
        if (NewSkin->m_view->m_displayHeight != (skinLCD.bottom - skinLCD.top)) {
            ShowDialog(ID_MESSAGE_SKIN_WRONG_SIZE);
            goto Error;
        }
        if (NewSkin->m_view->m_displayDepth != (int)skinLCDDepth) {
            ShowDialog(ID_MESSAGE_SKIN_WRONG_SIZE);
            goto Error;
        }

        // Delete the old skin
        clearSkin();
    }
    else // if ( Configuration.ScreenWidth )
    {
        // Now confirm that the new skin's size is compatible (
        // equal to) with the old one's.If not, we'll have to fail the reconfiguration
        // since WinCE doesn't support resizing the display at run-time
        ASSERT( Configuration.ScreenWidth != 0 && Configuration.ScreenHeight != 0);
        if (NewSkin->m_view->m_displayWidth != Configuration.ScreenWidth) {
            ShowDialog(ID_MESSAGE_SKIN_WRONG_SIZE);
            goto Error;
        }
        if (NewSkin->m_view->m_displayHeight != Configuration.ScreenHeight) {
            ShowDialog(ID_MESSAGE_SKIN_WRONG_SIZE);
            goto Error;
        }
        if (NewSkin->m_view->m_displayDepth != Configuration.ScreenBitsPerPixel) {
            ShowDialog(ID_MESSAGE_SKIN_WRONG_SIZE);
            goto Error;
        }
    }

    // Swap in the new skin into the live configuration
    ASSERT( Skin == NULL );
    Skin = NewSkin;

    // Read the skin size
    readSkinInfo();

    // Update the file path to the new skin
    Configuration.setSkinXMLFile(NewParam);

    // Update the window title with the new skin's title
    SetWindowText(hWnd, screenCaption);

    // Load the bitmaps, etc. - also set SkinEnable and SkinInitialized to the right values
    onWM_CREATE(hWnd);

    // Resize the display to match the new skin
    AdjustWindowSize();
    AdjustTopmostSetting();
    ToggleToolTip(Configuration.ToolTipState); // turn the tooltip back into the requested position
    return true;

Error:
    delete NewSkin;
    return false;
}

bool __fastcall IOWinController::RegisterMenuCallback( unsigned short message, MENUCALLBACK func )
{
    // Make sure that there is space for another menu record
    if ( MenuRecordCount >= (MENUITEM_MAX - 1) || func == NULL )
        return false;

    MenuRecords[MenuRecordCount].message  = message;
    MenuRecords[MenuRecordCount].callback = func;

    MenuRecordCount++;

    return true;
}

void __fastcall IOWinController::EnableUIMenuItem( unsigned int itemID )
{
   PostMessage(hWnd, WM_COMMAND, (WPARAM)ID_COMMAND_ENABLEMENU, (LPARAM)itemID);
}

void __fastcall IOWinController::DisableUIMenuItem( unsigned int itemID )
{
    PostMessage(hWnd, WM_COMMAND, (WPARAM)ID_COMMAND_DISABLEMENU, (LPARAM)itemID);
}

const LPCWSTR IOWinController::WindowClassName=L"LCDDisplay";

DWORD WINAPI IOWinController::LCDThreadProcStatic(LPVOID lpvThreadParam)
{
	return ((IOWinController*)lpvThreadParam)->LCDThreadProc();
}

DWORD WINAPI IOWinController::LCDThreadProc(void)
{
	WNDCLASSW WndClass;
	ATOM a;
	MSG Msg;
	RECT rc;
	const DWORD dwStyle = WS_OVERLAPPED|WS_MINIMIZEBOX|WS_BORDER|WS_CAPTION|WS_SYSMENU;

	memset(&WndClass, 0, sizeof(WndClass));
	WndClass.lpfnWndProc=LCDWndProcStatic;
	WndClass.lpszClassName=WindowClassName;
	WndClass.hInstance = GetModuleHandle(NULL);
	WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	WndClass.hIcon = LoadIcon(WndClass.hInstance, MAKEINTRESOURCE(IDC_EMULATORICON));
	a = RegisterClassW(&WndClass);
	if (a == 0) {
		ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
		exit(1);
	}

	rc.left     = 0;
	rc.top      = 0;
	rc.right    = Configuration.isSkinFileSpecified() &&
				  Configuration.fShowSkin ?
				  skinRect.right : Configuration.ScreenWidth;
	rc.bottom   = Configuration.isSkinFileSpecified() &&
				  Configuration.fShowSkin ?
				  skinRect.bottom : Configuration.ScreenHeight;

	AdjustWindowRect(&rc, dwStyle, true);

	hMenu = Configuration.Resources.getMenu(IDR_MENU1);

	if (hMenu == NULL) {
		ShowDialog(ID_MESSAGE_INTERNAL_ERROR);
		exit(1);
	}

	const wchar_t * TitleBar = EMULATOR_NAME_W;
	if (Configuration.isSkinFileSpecified()) {
		TitleBar = screenCaption;
	} else if (Configuration.getVMIDName()) {
		TitleBar = Configuration.getVMIDName();
	}

	hWnd = CreateWindowExW(
							0,
							(LPCWSTR)a,
							TitleBar,
							dwStyle,
							CW_USEDEFAULT, // x
							CW_USEDEFAULT, // y
							rc.right-rc.left, // nWidth
							rc.bottom-rc.top, // nHeight
							NULL, // hWndParent,
							hMenu,
							GetModuleHandle(NULL), // hInstance
							NULL); // lpParam
	if (hWnd == NULL) {
		ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
		exit(1);
	}

	AdjustWindowSize();
	AdjustTopmostSetting();
	if (!Configuration.VSSetup )
		::ShowWindow(hWnd, SW_SHOW);
	
	if(bIsRestoringState)
	{
		DrawLoadingScreen();
	}

	while (GetMessage(&Msg, NULL, 0, 0)) {
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}
	return 0;
}

LRESULT CALLBACK IOWinController::LCDWndProcStatic(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return IOWinCtrlInstance->LCDWndProc(hWnd, uMsg, wParam, lParam);
}


INT_PTR CALLBACK AboutDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_INITDIALOG:
        // perform initialization work here... copy the contents of the
        // Configuration object into the dialog's controls.
        {
            HRESULT hr = E_FAIL;
            wchar_t stringBuffer[MAX_LOADSTRING];
            if (!Configuration.Resources.getString(ID_LEGAL_NOTICE, stringBuffer ))
                goto FailedToInit;
            SetDlgItemText(hwndDlg,
                           IDC_LEGALNOTICE,
                           stringBuffer);
            if (!Configuration.Resources.getString(ID_VERSION_STRING, stringBuffer ))
                goto FailedToInit;
            SetDlgItemText(hwndDlg,
                           IDC_PRODUCTVERSION,
                           stringBuffer);

            wchar_t CopyrightBuffer[MAX_LOADSTRING];
            if (!Configuration.Resources.getString(ID_COPYRIGHT_STRING, stringBuffer ))
                goto FailedToInit;
            hr = StringCchPrintfW(CopyrightBuffer, ARRAY_SIZE(CopyrightBuffer), L"%s", stringBuffer);
            if (FAILED(hr))
                goto FailedToInit;
            if (!Configuration.Resources.getString(ID_COPYRIGHT_YEARS, stringBuffer ))
                goto FailedToInit;
            hr = StringCchCatW(CopyrightBuffer, ARRAY_SIZE(CopyrightBuffer), stringBuffer);
            if (FAILED(hr))
                goto FailedToInit;
            SetDlgItemText(hwndDlg,
                           IDC_COPYRIGHT_STRING,
                           CopyrightBuffer);
            return TRUE; // set focus to the default control
        FailedToInit:
            ShowDialog(ID_MESSAGE_INTERNAL_ERROR);
            PostMessage(hwndDlg, WM_COMMAND, IDCANCEL, NULL );
            return FALSE;
        }

    case WM_COMMAND:
        switch (wParam) {

        case IDOK:
        case IDCANCEL:
            EndDialog(hwndDlg, 0);
            return TRUE;

        default:
            break;
        }
        break;

    default:
        break;
    }
    return FALSE; // message not handled
}

LRESULT CALLBACK IOWinController::LCDWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_LBUTTONDOWN:
		// See comment in onWM_SETFOCUS()
		if ( hhkLowLevelKybd == NULL )
		{
			onWM_SETFOCUS();
		}
		SetCapture(hWnd);
		if (isEmulatorCoord(lParam))
			onWM_LBUTTONDOWN(translateEmulatorCoord(lParam));
		else
			processSkinEvent(hWnd, uMsg, wParam, lParam);
		return (LRESULT)1;

	case WM_MOUSEMOVE:
		if (isEmulatorCoord(lParam))
		{
			onSkinChangeFocus(hWnd, uMsg, wParam);
			onWM_MOUSEMOVE(translateEmulatorCoord(lParam));
		}
		else
		{
			SkinMayHaveFocus = true;
			processSkinEvent(hWnd, uMsg, wParam, lParam);
		}
		return (LRESULT)1;

	case WM_LBUTTONUP:
		if (isEmulatorCoord(lParam))
			onWM_LBUTTONUP(translateEmulatorCoord(lParam));
		else
			processSkinEvent(hWnd, uMsg, wParam, lParam);
		ReleaseCapture();
		return (LRESULT)1;

	case WM_CAPTURECHANGED:
		onWM_CAPTURECHANGED();
		return (LRESULT)0; // report that we handled the message.

	case WM_ERASEBKGND:
		return (LRESULT)1; // handle it by doing nothing

	case WM_PAINT:
		onWM_PAINT(hWnd);
		return (LRESULT)1;

	case WM_CREATE:
		onWM_CREATE(hWnd);
		return (LRESULT)1;

	case WM_PALETTECHANGED:
		if (wParam != (WPARAM)hWnd) { // don't cause an infinite loop if we're the source of the change
			InvalidateRect(hWnd, NULL, FALSE);
		}
		return (LRESULT)1;

	case WM_DISPLAYCHANGE:
		// Resize the window in case there is more screen real estate available
		AdjustWindowSize();
		if (!Configuration.VSSetup )
			::ShowWindowAsync(hWnd, SW_SHOW);
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	case WM_SETFOCUS:
		onWM_SETFOCUS();
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	case WM_CLOSE:
#if FEATURE_SAVESTATE
		if (Configuration.isSaveStateEnabled())
		{
			int Result;

			Result = PromptToAllowWithCancel(ID_MESSAGE_SAVE_STATE);

			if (Result == IDYES)
			{
				bSaveState = true;
				BoardSuspend();
				return 0; // continue without calling DefWindowProc, which would call DestroyWindow
			}
			else if (Result == IDCANCEL)
			{
				return 0;
			}
			else
			{
				ASSERT(Result == IDNO);
				Configuration.setDontSaveState(true);
				BoardSuspend();
				return 0; // continue without calling DefWindowProc, which would call DestroyWindow
			}
		}
#endif //FEATURE_SAVESTATE
#if FEATURE_COMINTEROP
        CoReleaseServerProcess();
#endif
		exit(0);

	case WM_KILLFOCUS:
		onWM_KILLFOCUS();
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	case WM_ACTIVATE:
		onSkinChangeFocus(hWnd, uMsg, wParam);
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	case WM_INITMENU:
		onWM_INITMENU();
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	case WM_TIMER:
		if ( wParam == IDT_TIMERKEYREPEAT ) {
			processSkinEvent(hWnd, uMsg, wParam, lParam);
			return 0;
		} else {
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
		}
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_FILE_SAVESTATE:
			bSaveState = true;
			BoardSuspend();
			return 0;
        case ID_FILE_CLEARSAVESTATE:
            ASSERT(Configuration.UseDefaultSaveState == true);
            if (Configuration.UseDefaultSaveState && Configuration.isSaveStateEnabled()) {
                // Delete the current save-state file so the next emulator cold boots.
                DeleteFileW(Configuration.getSaveStateFileName());
            }
            return 0;
        case ID_FILE_RESET_SOFT:
        case ID_FILE_RESET_HARD:
        {
            if (PromptToAllow(ID_MESSAGE_ARE_YOU_SURE_RESET)) {
                BoardReset((LOWORD(wParam) == ID_FILE_RESET_HARD));
            }
        }
        break;

        case ID_FILE_CONFIGURE:
            if ( IsWindowEnabled(hWnd) )
                BoardShowConfigDialog(hWnd);
            break;

        case ID_FILE_EXIT:
            PostMessage(hWnd, WM_CLOSE, (WPARAM)0, (LPARAM)0);
            break;

        case ID_COMMAND_ROTATE:
            SetRotation((int)lParam);
            break;

        case ID_COMMAND_DISABLEMENU:
            EnableMenuItem(hMenu, (int)lParam, MF_GRAYED);
            break;

        case ID_COMMAND_ENABLEMENU:
            EnableMenuItem(hMenu, (int)lParam, MF_ENABLED);
            break;

        case ID_HELP_CONTENTS:
            ShowHelp((wchar_t*)lParam);
            break;

        case ID_HELP_ABOUT:
            Configuration.Resources.startDialog(IDD_ABOUTDIALOG, hWnd, AboutDialogProc );
            break;

        default:
            // Device specific menu options
            unsigned int i;
            for ( i = 0; i < MenuRecordCount; i++ )
                if ( MenuRecords[i].message == (int)LOWORD(wParam) )
                {
                    MenuRecords[i].callback(hWnd, uMsg, wParam, lParam);
                    break;
                }
            ASSERT(i != MenuRecordCount);
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);

    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

DWORD WINAPI IOWinController::LCDDMAThreadProcStatic(LPVOID lpvThreadParam)
{
	return ((IOWinController*)lpvThreadParam)->LCDDMAThreadProc();
}

void IOWinController::HideWindow(void)
{
	//we want to leave the window visible if we are saving state,
	//to give the user indication the emulator is current saving, and not frozen.
	if(!bSaveState)
		::ShowWindowAsync(hWnd, SW_HIDE);
}

void IOWinController::ShowWindow(void)
{
	SetEvent(LCDEnabledEvent); // Unblock the LCD DMA thread
	AdjustWindowSize();
	if (!Configuration.VSSetup )
		::ShowWindowAsync(hWnd, SW_SHOW);
}

void IOWinController::AdjustWindowSize()
{
    const DWORD dwStyle = WS_OVERLAPPED|WS_VISIBLE|WS_MINIMIZEBOX|WS_BORDER|WS_CAPTION|WS_SYSMENU;

    // The device screen size
    LCDRect.left   = 0;
    LCDRect.top    = 0;
    LCDRect.right  = Configuration.ScreenWidth;
    LCDRect.bottom = Configuration.ScreenHeight;

    // Figure out the actual screen size given the skin state
    if (SkinEnabled)
        onSkinResize();
    else
        updateTransform();

    RECT window;
    if (!SkinEnabled)
        getDeviceView(window);
    else
        getSkinView(window);

    // Get the current window client area size
    RECT curr_window;

    if ( !GetClientRect(hWnd, &curr_window) || !EqualRect( &curr_window, &window) )
    {
        // If we send WM_NCCALCSIZE message before calling SetWindowPos we expose a bug 
        // in windows. The message causes multiline menus to be used if the client
        // area size passed in is sufficiently narrow. However SetWindowPos(like 
        // AdjustWindowRect) is not multiline menu aware so it creates a wider window
        // than we requested and puts the client area one menu height below the top
        // of the window. The menu will be created multiline and the client area will
        // overlap the menu lines below the top one. There will be extra space below the
        // client area. To avoid that we let SetWindowPos create the window of the width
        // it will choose and send WM_NCCALCSIZE with that dimension.
        RECT origReq = window;
        AdjustWindowRect(&window, dwStyle, true);

        SetWindowPos(hWnd, NULL, 0, 0, window.right-window.left, window.bottom-window.top,
                     SWP_NOMOVE|SWP_NOCOPYBITS|SWP_ASYNCWINDOWPOS);

        // Figure out what is the actual client area dimension that windows gave us
        RECT actWindow;
        GetClientRect(hWnd, &actWindow);
        // Don't allow the requested client area to shrink. This may happen if there are
        // multiline menus, since SetWindowPos is not multiline menu aware
        window.left = window.top = 0;
        window.bottom = origReq.bottom > actWindow.bottom ? origReq.bottom : actWindow.bottom;
        window.right  = origReq.right > actWindow.right ? origReq.right : actWindow.right;
        AdjustWindowRect(&window, dwStyle, true);
        // Adjust for wrapping menu lines
        RECT temp = window;
        temp.bottom = 0x7FFF;     // "Infinite" height 
        // Note that using SendMessage here is safe because the window proc
        // is executed on current thread and it doesn't take any locks for this message
        SendMessage(hWnd, WM_NCCALCSIZE, FALSE, (LPARAM)&temp);
        window.bottom += temp.top;

        SetWindowPos(hWnd, NULL, 0, 0, window.right-window.left, window.bottom-window.top,
                     SWP_NOMOVE|SWP_NOCOPYBITS|SWP_ASYNCWINDOWPOS);
    }
    else
        InvalidateRect(hWnd, NULL, FALSE);
}

void IOWinController::DrawLoadingScreen(void)
{
	const DWORD dwStyle = WS_OVERLAPPED|WS_VISIBLE|WS_MINIMIZEBOX|WS_BORDER|WS_CAPTION|WS_SYSMENU;
	RECT LoadScreen;
	
	// If skin enabled, figure out the actual screen size given the skin state
	if (Configuration.isSkinFileSpecified() && Configuration.fShowSkin )
	{	
		LoadScreen.right  = skinRect.right;
		LoadScreen.bottom = skinRect.bottom;
		LoadScreen.left   = 0;
		LoadScreen.top    = 0;
	}
	else
	{    // No skin detected, Use a default screen size
		LoadScreen.left   = 0;
		LoadScreen.top    = 0;
		LoadScreen.right  = Configuration.ScreenWidth;
		LoadScreen.bottom = Configuration.ScreenHeight;
	}


	AdjustWindowRect(&LoadScreen, dwStyle, true);
	SetWindowPos(hWnd, NULL, 0, 0, LoadScreen.right-LoadScreen.left,
				 LoadScreen.bottom-LoadScreen.top, SWP_NOMOVE|SWP_NOCOPYBITS);

	HDC hDC;
	hDC = GetDC(hWnd);
	//set background and text colour
	COLORREF oldTextColor = SetTextColor(hDC, GetSysColor(COLOR_WINDOWTEXT));
	COLORREF oldBKColour = SetBkColor(hDC, GetSysColor(COLOR_WINDOW));

	//get font into
	TEXTMETRIC currentFont;
	GetTextMetrics(hDC, &currentFont);
	
	// Load the usage string
	wchar_t captionString[MAX_LOADSTRING];
	if (!Configuration.Resources.getString(ID_CAPTION_LOADING, captionString ))
	{
		//out of memory
		TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
	}
	int  string_length = lstrlenW(captionString);
	
	//calculate the text center position, taking into account the font height
	RECT currLCD = LoadScreen;
	if ( Configuration.isSkinFileSpecified() )
		currLCD = skinLCD;
	long yTextCenter =  (currLCD.bottom + currLCD.top + currentFont.tmHeight)/2;
	long xTextCenter = (currLCD.right + currLCD.left) / 2;
	
	unsigned int currentAlignment = GetTextAlign(hDC);
	SetTextAlign(hDC, TA_CENTER | TA_BASELINE | VTA_BASELINE |TA_NOUPDATECP );
	ExtTextOut(hDC,	(int)xTextCenter + 1, (int)yTextCenter + 1, ETO_OPAQUE,
		&LoadScreen, captionString, string_length, NULL);
	SetTextAlign(hDC, currentAlignment );


	InvalidateRect(hWnd, &currLCD, FALSE);
	//restore text and background color
	SetTextColor(hDC, oldTextColor);
	SetBkColor(hDC, oldBKColour);
	ReleaseDC(hWnd, hDC);
}

void IOWinController::AdjustTopmostSetting(void)
{
    HWND hWndInsertAfter;

    if (Configuration.IsAlwaysOnTop) {
        hWndInsertAfter = HWND_TOPMOST;
    } else {
        hWndInsertAfter = HWND_NOTOPMOST;
    }
    SetWindowPos(hWnd, hWndInsertAfter, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
}


void IOWinController::SetZoom(bool fZoom)
{
	Configuration.IsZoomed = fZoom;
	DispScale = (Configuration.IsZoomed ? 2.0 : 1.0);

	AdjustWindowSize();
}

void IOWinController::SetTopmost(bool fTopmost)
{
    Configuration.IsAlwaysOnTop = fTopmost;
    AdjustTopmostSetting();
}

void IOWinController::ShowSkin(bool fShow)
{
    Configuration.fShowSkin = fShow;

    SkinEnabled = fShow && SkinInitialized;

    if (SkinEnabled)
        ToggleToolTip(Configuration.ToolTipState);

    AdjustWindowSize();
}

void IOWinController::SetRotation(unsigned int mode)
{
	Configuration.RotationMode = mode;

	AdjustWindowSize();

	// Invalidate the screen
	InvalidateRect(hWnd, NULL, FALSE);
}

void IOWinController::ToggleToolTip( ToolTipStateEnum req_state )
{
    //Disable the tool tip
    if ( SkinEnabled )
    {
        //Disable the tool tip if the requested state is off
        if ( req_state == ToolTipOff )
        {
            ButtonRecord *iter = m_buttons;
            while (iter )
            {
                iter->button->OnKillFocus(hWnd);
                iter = iter->NextButton;
            }

            SendMessage(hwndTip,TTM_ACTIVATE,(WPARAM)FALSE,(LPARAM)0);
            SkinMayHaveFocus = false;
        }
        else
        {
            // Activate the tool tip if it is currently disabled
            if ( req_state == ToolTipOn )
                SendMessage(hwndTip,TTM_ACTIVATE,(WPARAM)TRUE,(LPARAM)0);
            // Set a delay if requested
            if ( req_state == ToolTipDelay )
                SendMessage(hwndTip,TTM_SETDELAYTIME,(WPARAM)TTDT_AUTOMATIC,(LPARAM)MAKELONG(500, 0));
            else
                SendMessage(hwndTip,TTM_SETDELAYTIME,(WPARAM)TTDT_AUTOMATIC,(LPARAM)MAKELONG(-1, 0));
        }
    }
    Configuration.ToolTipState = req_state;
}

void IOWinController::getDeviceView( RECT & view )
{
    // Copy the map LCD view in
    view.left  = LCDRect.left;   view.top    = LCDRect.top;
    view.right = LCDRect.right;  view.bottom = LCDRect.bottom;

    // Transform it
    translateFromMapToView( view.left, view.top);
    translateFromMapToView( view.right, view.bottom);

    // Figure out if maximum and minimum have switched
    if ( view.top > view.bottom )
    {
        int temp = view.top; view.top = view.bottom; view.bottom = temp;
    }

    if ( view.left > view.right )
    {
        int temp = view.left; view.left = view.right; view.right = temp;
    }
}


void IOWinController::getSkinView( RECT & view )
{
    // Copy the map skin view in
    view.left  = skinRect.left;   view.top    = skinRect.top;
    view.right = skinRect.right;  view.bottom = skinRect.bottom;

    // Transform it
    translateFromMapToView( view.left, view.top);
    translateFromMapToView( view.right, view.bottom);

    // Figure out if maximum and minimum have switched
    if ( view.top > view.bottom )
    {
        int temp = view.top; view.top = view.bottom; view.bottom = temp;
    }

    if ( view.left > view.right )
    {
        int temp = view.left; view.left = view.right; view.right = temp;
    }
}

bool IOWinController::isEmulatorCoord(LPARAM lp )
{
    int x = LOWORD(lp);
    int y = HIWORD(lp);

    RECT view; getDeviceView( view );

    return x >= view.left && x <= view.right &&
           y >= view.top && y <= view.bottom;
}

LPARAM IOWinController::translateEmulatorCoord( LPARAM lp )
{
    LONG x = (LONG)(LOWORD(lp)), y = (LONG)(HIWORD(lp));

    translateFromViewToMap( x, y);

    return MAKELPARAM((x - LCDRect.left),(y - LCDRect.top));

}

LPARAM IOWinController::translateSkinCoord( LPARAM lp )
{
    LONG x = (LONG)(LOWORD(lp)), y = (LONG)(HIWORD(lp));

    translateFromViewToMap( x, y);

    return MAKELPARAM(x,y);
}

void IOWinController::translateFromViewToMap( LONG & x, LONG & y )
{
    // Scale down the current values
    x = (LONG)(x/DispScale);
    y = (LONG)(y/DispScale);

    // Rotate back into place
    if (Configuration.RotationMode)
    {
        unsigned int screen_width  = (SkinEnabled ? skinRect.right  : LCDRect.right);
        unsigned int screen_height = (SkinEnabled ? skinRect.bottom : LCDRect.bottom);
        LONG temp;
        switch (Configuration.RotationMode)
        {
            case 0: break;
            case 1: temp = x; x = y ; y = screen_height - temp; break;
            case 2: x = screen_width - x; y = screen_height - y; break;
            case 3: temp = x; x = screen_width - y; y = temp; break;
        }
    }
}

void IOWinController::translateFromMapToView( LONG & x, LONG & y )
{
    // Cache the current value
    LONG old_x = x, old_y = y;
    // Calculate the map value
    x = (LONG)(old_x*skinTransform.eM11 + old_y*skinTransform.eM21+skinTransform.eDx);
    y  = (LONG)(old_x*skinTransform.eM12 + old_y*skinTransform.eM22+skinTransform.eDy);
}

///////////////////////// Keyboard functionality /////////////////////////////////////

LRESULT CALLBACK IOWinController::LowLevelKeyboardProcStatic(int nCode, WPARAM wParam, LPARAM lParam)
{
	return IOWinCtrlInstance->LowLevelKeyboardProc(nCode, wParam, lParam);
}

LRESULT CALLBACK IOWinController::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    WINDOWINFO descr;
    descr.cbSize = sizeof(WINDOWINFO);
    // We only want to grab the keyboard if we are in fact the active window
    // we get a SET_FOCUS event as part of window creation even if the
    // focus is on a different window. 
    if ( GetWindowInfo(hWnd, &descr) && descr.dwWindowStatus == WS_ACTIVECAPTION )
    {
        unsigned __int8 KeyUpDown;

        KeyUpDown=0;  // assume key down
        if (nCode == HC_ACTION) {	
            switch (wParam) {
             case WM_KEYUP:
             case WM_SYSKEYUP:
                onWM_SYSKEYUP(KeyUpDown); // flag this event as a keyup
                // fall through...
             case WM_KEYDOWN:
             case WM_SYSKEYDOWN:
             {
                PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT) lParam;
                unsigned __int8 vKey;

                ASSERT(p->vkCode < 256);
                vKey = (unsigned __int8)p->vkCode;

                // Determine if the keystroke should be passed on to Windows
                if ( Configuration.getKeyboardSelector() )
                {
                    if ( vKey == Configuration.getKeyboardSelector() )
                    {
                         // There is a race condition here - if KeyboardSelector changes
                         // from one non-zero value to another non-zero value and
                         // KeyboardToWindows is reset before the next line is executed.
                         // If this happens all the keys will be sent to Windows until
                         // the new KeyboardSelector is pressed and released. This
                         // present a problem if the KeyboardSeletor changes from R-ALT
                         // to L-ALT because it confuses the state machine in win32k.sys
                         Configuration.setKeyboardToWindows((KeyUpDown ? false : true));
                         break; // Make sure to send the KeyboardSelector key up event to Windows
                    }
                    if ( Configuration.getKeyboardToWindows() )
                        break;
                }
                if (wParam == WM_SYSKEYUP || wParam == WM_KEYUP)
                    KeyState[vKey] = 0;
                else
                    KeyState[vKey] = 1;

                onWMKEY( vKey, KeyUpDown );

                return 1; // report that we handled the event
             }
             break;

            default:
             break;
           }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

#include "ScancodeMapping.cpp"

void WinController::QueueKeyEvent( unsigned __int32 keymap, unsigned __int32 event )
{
    if ( keymap >= ScanCodeTableFirst && keymap <= ScanCodeTableLast)
    {
        // Translate the key from PS2 to VK
        unsigned __int8 vKey = (unsigned __int8)ScanCodeToVKeyTable[keymap];
        unsigned __int8 KeyUpDown = 0;

        // Send key down event
        if ( event & 1 )
            IOWinCtrlInstance->onWMKEY( vKey, KeyUpDown );
        // Send key up event
        if ( event & 2 )
        {
            IOWinCtrlInstance->onWM_SYSKEYUP(KeyUpDown);
            IOWinCtrlInstance->onWMKEY( vKey, KeyUpDown );
        }

        if (event & 2)
            IOWinCtrlInstance->clearKeyState(vKey);
        else
            IOWinCtrlInstance->setKeyState(vKey);
    }
    else
        ASSERT( false && "Failed to map from PS2 scan to VK");
}
void WinController::shutDown( )
{
    PostMessage(hWnd, WM_CLOSE, (WPARAM)0, (LPARAM)0);
}

void WinController::resetToDefaultOrientation( )
{
    PostMessage(hWnd, WM_COMMAND, (WPARAM)ID_COMMAND_ROTATE, (LPARAM)Configuration.DefaultRotationMode);
}

//////////////////Event callbacks (default implementation)//////////////////////////////

void IOWinController::onWM_PAINT(HWND hWnd)
{
	PAINTSTRUCT ps;
	HDC hdcMem;
	HGDIOBJ hbmOld;

	BeginPaint(hWnd, &ps);

	hdcMem = CreateCompatibleDC(ps.hdc); // create a compatible DC
	hbmOld = SelectObject(hdcMem, hBitMap);

	if ( hPalette != NULL )
	{
		SelectPalette(ps.hdc, hPalette, FALSE);
		RealizePalette(ps.hdc);
	}

	if (SkinEnabled)
		onSkinPaint(ps);

	SetGraphicsMode(ps.hdc, GM_ADVANCED);
	SetWorldTransform(ps.hdc, &skinTransform);

	BitBlt(ps.hdc, LCDRect.left, LCDRect.top,
		LCDRect.right - LCDRect.left,
		LCDRect.bottom - LCDRect.top,
		hdcMem, 0, 0, SRCCOPY);

	// Check if we need to fill an area of the window which is not covered by either skin or guest LCD
	// This is possible for really small screen sizes

	ModifyWorldTransform(ps.hdc, NULL, MWT_IDENTITY);
	SetGraphicsMode(ps.hdc, GM_COMPATIBLE);

	// If there is client area left uncovered fill it with the default color
	FillExcessSpace(ps);

	if (bSaveState)
	{
		//display a message that will notify users that the emulator is current saving state
		RECT DeviceView;
		getDeviceView( DeviceView );
		long xPercentage_Indent = 15;	//percentage of LCD use to indent each side of the screen
		long yPercentage_Indent = 40;
		wchar_t captionString[MAX_LOADSTRING];
		
		TEXTMETRIC currentFont;
		GetTextMetrics(ps.hdc, &currentFont);
		
		if (!Configuration.Resources.getString(ID_CAPTION_SAVINGSTATE, captionString ))
		{
			//out of memory
			TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
		}
		int  string_length = lstrlenW(captionString);

		//Calculate percentage of LCD that we want to use for the display dialog
		long xIndent = ((DeviceView.right - DeviceView.left) * xPercentage_Indent) / 100;
		long yIndent = ((DeviceView.bottom - DeviceView.top) * yPercentage_Indent) / 100;
		
		DeviceView.right= DeviceView.right - xIndent;
		DeviceView.left = DeviceView.left + xIndent;
		DeviceView.bottom= DeviceView.bottom - yIndent;
		DeviceView.top = DeviceView.top + yIndent;
		
		//set background and text color
		SetTextColor(ps.hdc, GetSysColor(COLOR_WINDOWTEXT));
		SetBkColor(ps.hdc, GetSysColor(COLOR_WINDOW));

		//calculate the text center position, taking into account the font height

		long yTextCenter = (DeviceView.bottom + DeviceView.top + currentFont.tmHeight) /2;
		long xTextCenter = (DeviceView.right + DeviceView.left) / 2;

		unsigned int currentAlignment = GetTextAlign(ps.hdc);
		SetTextAlign(ps.hdc, TA_CENTER | TA_BASELINE | VTA_BASELINE |TA_NOUPDATECP );
		ExtTextOut(ps.hdc,	(int)xTextCenter, (int)yTextCenter, ETO_OPAQUE,
			&DeviceView, captionString, string_length, NULL);
		SetTextAlign(ps.hdc, currentAlignment );
	}


	SelectObject(hdcMem, hbmOld);
	DeleteDC(hdcMem);


	EndPaint(hWnd, &ps);
}

void IOWinController::FillExcessSpace(PAINTSTRUCT & ps)
{
	// Get the size of the window covered by the skin & LCD in the current viewpoint
	RECT window;
	if (!SkinEnabled)
		getDeviceView(window);
	else
		getSkinView(window);

	// Get the actual size of the window
	RECT windowActual;
	GetClientRect(hWnd, &windowActual);
	// We have no restrictions on the minimum height of the window, so the actuall should match requested
	// if it doesn't it means we are in the middle of boot
	// If there is extra space fill it with default background color
	if ( windowActual.right > window.right && windowActual.bottom <= (window.bottom + 1))
	{
		RECT windowEmpty;           // Area of the window that is not covered by either skin or LCD
		windowEmpty.left = window.right; windowEmpty.right = windowActual.right;
		windowEmpty.top = 0; windowEmpty.bottom = windowActual.bottom;
		HBRUSH bkColorBrush = GetSysColorBrush(COLOR_MENU); // don't delete this brush
		FillRect(ps.hdc, &windowEmpty, bkColorBrush);
	}
}

void IOWinController::onSkinPaint(PAINTSTRUCT & ps)
{
	HDC hdcSkin = NULL;
	HGDIOBJ hbmSkin = NULL;

	// This function should only be called if the skin is enabled and initialized
	ASSERT( SkinEnabled && SkinInitialized && Configuration.isSkinFileSpecified() );

	// create a compatible DC
	hdcSkin = CreateCompatibleDC(ps.hdc);
	hbmSkin = SelectObject(hdcSkin, m_bitmapHandles[0]);

	// To prevent flickering update the skin as a sequence of 4 rectangles around device area
	SetGraphicsMode(ps.hdc, GM_ADVANCED);
	SetWorldTransform(ps.hdc, &skinTransform);
	BitBlt(ps.hdc, 0, 0,
			LCDRect.left,
			skinRect.bottom,
			hdcSkin, 0, 0, SRCCOPY);
	BitBlt(ps.hdc, LCDRect.right, 0,
			skinRect.right - LCDRect.right,
			skinRect.bottom,
			hdcSkin, LCDRect.right, 0, SRCCOPY);
	BitBlt(ps.hdc, LCDRect.left, 0,
			LCDRect.right - LCDRect.left,
			LCDRect.top,
			hdcSkin, LCDRect.left, 0, SRCCOPY);
	BitBlt(ps.hdc, LCDRect.left, LCDRect.bottom,
			LCDRect.right - LCDRect.left,
			skinRect.bottom - LCDRect.bottom,
			hdcSkin, LCDRect.left, LCDRect.bottom, SRCCOPY);
	ModifyWorldTransform(ps.hdc, NULL, MWT_IDENTITY);
	SetGraphicsMode(ps.hdc, GM_COMPATIBLE);

	// Update the buttons
	ButtonRecord * iter = m_buttons;
	while (iter )
	{
		iter->button->OnPaint(ps, &skinTransform);
		iter = iter->NextButton;
	}

	// Clean up
	SelectObject(hdcSkin, hbmSkin);
	DeleteDC(hdcSkin);
}

void IOWinController::updateTransform()
{
    const unsigned __int32 RotationMode = Configuration.RotationMode;

    // Figure out the mapped view
    RECT mappedView;
    if (SkinEnabled)
        mappedView = skinRect;
    else
        mappedView = LCDRect;

    // Calculate the transform
    double cos_pos = ( RotationMode == 1 || RotationMode == 3 ? 0 : ( RotationMode == 2 ? -1 : 1));
    double sin_pos = ( RotationMode == 0 || RotationMode == 2 ? 0 : ( RotationMode == 3 ? -1 : 1));

    double shift_dx = (RotationMode == 1 ? mappedView.bottom :
                      (RotationMode == 2 ? mappedView.right : 0) );
    double shift_dy = (RotationMode == 2 ? mappedView.bottom :
                      (RotationMode == 3 ? mappedView.right : 0) );

    skinTransform.eM11 = (FLOAT) (DispScale * cos_pos);
    skinTransform.eM12 = (FLOAT) (DispScale * sin_pos);
    skinTransform.eM21 = (FLOAT) (DispScale * -sin_pos);
    skinTransform.eM22 = (FLOAT) (DispScale * cos_pos);
    skinTransform.eDx  = (FLOAT) (shift_dx*DispScale - (RotationMode == 2 ? 1 : 0));
    skinTransform.eDy  = (FLOAT) (shift_dy*DispScale - (RotationMode == 2 ? 1 : 0));
}

void IOWinController::onSkinResize()
{
    // This function should only be called if the skin is enabled and initialized
    ASSERT( SkinEnabled && Configuration.isSkinFileSpecified() );

    if ( LCDRect.right  != (skinLCD.right - skinLCD.left) ||
         LCDRect.bottom != (skinLCD.bottom - skinLCD.top) )
    {
        // We should never enable a skin which doesn't fit
        ASSERT(false);
        // The skin doesn't fit the display - don't use it
        SkinInitialized = false; SkinEnabled = false;
        // Update the transform
        updateTransform();
    }
    else
    {
        // Translate the mapped LCDRect
        LCDRect.left   += (skinLCD.left - 1);
        LCDRect.top    += (skinLCD.top  - 1);
        LCDRect.right  += (skinLCD.left - 1);
        LCDRect.bottom += (skinLCD.top  - 1);

        // Update the transform
        updateTransform();

        // Update the buttons
        ButtonRecord *iter = m_buttons;
        while (iter )
        {
            iter->button->OnResize(&skinTransform);
            iter = iter->NextButton;
        }
    }
}

void IOWinController::onSkinChangeFocus(HWND hWnd, UINT uMsg, WPARAM wParam)
{
    if ( SkinEnabled && Configuration.ToolTipState != ToolTipOff )
    {
        // Deactivate the tooltip
        SkinMayHaveFocus = false;

        ButtonRecord *iter = m_buttons;
        while (iter )
        {
            iter->button->OnKillFocus(hWnd);
            iter = iter->NextButton;
        }

        // Disable the tooltip completely if the window is no longer active
        if ( uMsg == WM_ACTIVATE )
        {
            if ( LOWORD(wParam) == WA_INACTIVE )
                SendMessage(hwndTip,TTM_ACTIVATE,(WPARAM)FALSE,(LPARAM)0);
            else
                SendMessage(hwndTip,TTM_ACTIVATE,(WPARAM)TRUE,(LPARAM)0);
        }
    }
}

void IOWinController::onWM_SETFOCUS()
{
    WINDOWINFO descr;
    descr.cbSize = sizeof(WINDOWINFO);
    // We only want to grab the keyboard if we are in fact the active window
    // we get a SET_FOCUS event as part of window creation even if the
    // focus is on a different window. However windows will not generate another
    // SET_FOCUS message when the user clicks on our window. To guard against this
    // we verify that we have focus when the user clicks on the window.
    if ( GetWindowInfo(hWnd, &descr) && descr.dwWindowStatus == WS_ACTIVECAPTION 
        && hhkLowLevelKybd == NULL) {
        ASSERT(hhkLowLevelKybd == NULL);
        hhkLowLevelKybd  = SetWindowsHookEx(WH_KEYBOARD_LL,
                                        IOWinController::LowLevelKeyboardProcStatic,
                                        GetModuleHandle(NULL),
                                        0);
        if (hhkLowLevelKybd == NULL)
        {
                DWORD error = GetLastError();
                LOG_ERROR(GENERAL, "Failed to install the keyboard hook - %x", error);
                ASSERT(false && "Failed to install the keyboard hook");
        }
    }
}

void IOWinController::onWM_KILLFOCUS()
{
    for ( unsigned __int32 i = 0; i < sizeof(KeyState); i++ )
        if ( KeyState[i] )
        {
            // Send key up event
            unsigned __int8 KeyUpDown = 0;
            onWM_SYSKEYUP(KeyUpDown);
            onWMKEY((unsigned __int8)i,KeyUpDown);
            KeyState[i] = 0;
        }
    if ( hhkLowLevelKybd != NULL )
    {
        if (!UnhookWindowsHookEx(hhkLowLevelKybd))
        {
            DWORD error = GetLastError();
            LOG_ERROR(GENERAL, "Failed to unhook the keyboard hook - %x", error);
            ASSERT(false && "Failed to unhook the keyboard hook");
        }
    }
    hhkLowLevelKybd = NULL;
}

void IOWinController::onWM_INITMENU()
{
    if ( !Configuration.getFlashEnabled() )
        EnableMenuItem(hMenu, ID_FLASH_SAVE, MF_GRAYED);
    if ( !Configuration.UseDefaultSaveState )
        EnableMenuItem(hMenu, ID_FILE_CLEARSAVESTATE, MF_GRAYED);
}

void IOWinController::clearSkin()
{
    if (Skin != NULL || SkinInitialized || hwndTip == NULL)
    {
        // This function must be called on the window proc thread
        ASSERT( GetCurrentThreadId() == m_wndProcThreadId || hwndTip == NULL);

        XMLSkin * CurrentSkin = Skin;

        // Disable the skin and redraw the window appropriatly
        ToolTipStateEnum CurrentToolTipState = Configuration.ToolTipState;
        ToggleToolTip(ToolTipOff); // turn the tooltip off
        SkinEnabled = false;
        SkinInitialized = false;
        Skin = NULL;
        AdjustWindowSize();

        // Release the skin specific resources:
        //Delete the buttons
        ButtonRecord * iter = m_buttons;
        while (iter != NULL )
        {
            ButtonRecord * curr_button = iter;
            iter = iter->NextButton;
            delete curr_button;
        }
        m_buttons = NULL;

        // Delete the skin images
        for (int i = 0; i < ARRAY_SIZE(m_bitmapHandles); i++)
            if ( m_bitmapHandles[i] != NULL )
            {
                DeleteObject(m_bitmapHandles);
                m_bitmapHandles[i] = NULL;
            }

        // Delete the tooltips window
        if (hwndTip)
            DestroyWindow(hwndTip);
        // Destroy the skin itself
        if (CurrentSkin)
            delete CurrentSkin;
        // Restore tooltip settings
        Configuration.ToolTipState = CurrentToolTipState;
    }
}

void IOWinController::readSkinInfo()
{
    // Read the skin size
    Skin->GetSizeRect( &skinRect );
    // Clear out the screen caption and read the new caption
    if (screenCaption != NULL)
        delete [] screenCaption;
    screenCaption = _wcsdup(Skin->m_view->m_titleBar.c_str());
    // Read the LCD size and position inside the skin
    skinLCD.left = Skin->m_view->m_displayXPos;
    skinLCD.top  = Skin->m_view->m_displayYPos;
    skinLCD.right = Skin->m_view->m_displayWidth + skinLCD.left;
    skinLCD.bottom = Skin->m_view->m_displayHeight + skinLCD.top;
    skinLCDDepth = Skin->m_view->m_displayDepth;
    // Update the configuration object
    Configuration.ScreenHeight = Skin->m_view->m_displayHeight;
    Configuration.ScreenWidth  = Skin->m_view->m_displayWidth;
    Configuration.ScreenBitsPerPixel = Skin->m_view->m_displayDepth;
    ASSERT( skinRect.bottom >= Configuration.ScreenHeight && skinRect.right >= Configuration.ScreenWidth );
}

void IOWinController::onWM_CREATE(HWND hWnd)
{
	XMLSkin* skin = Skin;
	
	if( skin )
	{
		XMLView* view = skin->GetView();
		
		// Load all 4 Bimaps
		for( int index = 0; index < 4; index++ )
		{
			m_bitmapHandles[index] = NULL;
			
			std::wstring wFile;
			switch( index )
			{
				case 0:
					wFile = view->m_normalImage;
				break;
				
				case 1:
					wFile = view->m_downImage;
					break;

				case 2:
					wFile = view->m_focusImage;
					break;

				case 3:	
					wFile = view->m_disabledImage;
				break;
			}

			if ( !wFile.empty() )
			{
				if( wFile.rfind('\\') == std::string::npos &&
					wFile.rfind(':') == std::string::npos )
					wFile = skin->m_skinPath + wFile;
					
				LoadBitmapFromBMPFile( wFile, &m_bitmapHandles[index], OrientationBMP[index] );
			}
		}
			
		// Load region map bitmap
		HBITMAP hBitMapButtonMap = NULL;
		if(!view->m_mappingImage.empty())
		{
			std::wstring filePath = view->m_mappingImage;
			
			if( filePath.rfind('\\') == std::string::npos &&
				filePath.rfind(':') == std::string::npos )
				 filePath = skin->m_skinPath + filePath;
				
			bool ignored = false;
			LoadBitmapFromBMPFile( filePath, &hBitMapButtonMap, ignored );
		}

		std::vector<XMLButton>::iterator buttonIter = view->m_buttons.begin();

		// Create the tool tip
		hwndTip = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL,
								WS_POPUP | TTS_NOPREFIX | TTS_BALLOON,
								CW_USEDEFAULT, CW_USEDEFAULT,
								CW_USEDEFAULT, CW_USEDEFAULT,
								hWnd, NULL, GetModuleHandle(NULL),
								NULL);

		SetWindowPos(hwndTip, HWND_TOPMOST,0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		unsigned __int32 id = 0;

		// Iterate thru button list and create buttons
		while( buttonIter != view->m_buttons.end() && hBitMapButtonMap != NULL )
		{
			// Build a skin button
			ISkinRegionButton * button = CreateSkinRegionButton(this);
			if (button == NULL)
			{
				TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
			}
			button->m_onClick = buttonIter->m_onClick;
			button->m_onPressAndHold = buttonIter->m_onPressAndHold;

			// Set up button region
			button->SetRegion( hBitMapButtonMap, buttonIter->m_mappingColor );

			if( !buttonIter->m_toolTip.empty() )
			{
				//button->setTooltip( hwndTip );
				button->initToolTip( hWnd, id, buttonIter->m_toolTip.c_str() );
				id++;

				if(!SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)button->getToolTipInfo()))
					{ SkinInitialized = false; return; }
			}
			
			ButtonRecord * ButtonRec = new  ButtonRecord;
			ButtonRec->button = button;
			ButtonRec->NextButton = m_buttons;
			m_buttons = ButtonRec;

			buttonIter++;
		}
		
		// Delete region map bitmap
		if( hBitMapButtonMap != NULL )
		{
			DeleteObject(hBitMapButtonMap);
		}

		SkinInitialized = true;
		SkinEnabled = true; // The skin is ready for display
		ToggleToolTip( Configuration.ToolTipState); // turn the tooltip on/off as requested
	}
	else if (SkinInitialized)
	{
		// Create the tool tip
		hwndTip = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL,
								WS_POPUP | TTS_NOPREFIX | TTS_BALLOON,
								CW_USEDEFAULT, CW_USEDEFAULT,
								CW_USEDEFAULT, CW_USEDEFAULT,
								hWnd, NULL, GetModuleHandle(NULL),
								NULL);

		SetWindowPos(hwndTip, HWND_TOPMOST,0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		// Iterate over buttons adding each one to the tool tip
		ButtonRecord * iter = m_buttons;
		unsigned __int32 id = 0;
		while (iter != NULL )
		{
			iter->button->initToolTip( hWnd, id, NULL );
			id++;

			if(!SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)iter->button->getToolTipInfo()))
				{ SkinInitialized = false; return; }
			iter = iter->NextButton;
		}
		ToggleToolTip( Configuration.ToolTipState); // turn the tooltip on/off as requested
	}
}

bool IOWinController::processSkinEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// If skin is not enabled return
	if (!SkinEnabled)
		return false;

	ButtonRecord * iter = m_buttons;
	while (iter != NULL )
	{
		switch (uMsg) {
		case WM_LBUTTONDOWN:
			iter->button->OnLButtonDown( hWnd, uMsg, translateSkinCoord(lParam));
			break;

		case WM_MOUSEMOVE:
			iter->button->OnMouseMotion( hWnd, uMsg, translateSkinCoord(lParam));
			break;

		case WM_LBUTTONUP:
			iter->button->OnLButtonUp( hWnd, uMsg, translateSkinCoord(lParam));
			break;
		case WM_TIMER:
			iter->button->OnTimer( hWnd );
			break;
		}
		iter = iter->NextButton;
	}
	return true;
}

