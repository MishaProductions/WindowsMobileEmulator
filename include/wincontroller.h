/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef WINCONTROLLER__H_
#define WINCONTROLLER__H_

class WinController
{
public:
//	WinController();
//	~WinController();

    // Methods for interaction with buttons
    __inline HWND getToolTip() { return hwndTip; }
    __inline HWND setToolTip(HWND handle) { return hwndTip = handle; }
    __inline HBITMAP * getSkinBitmaps() { return (HBITMAP *)&m_bitmapHandles; }
 
    void translateFromMapToView( LONG & x, LONG & y )
    {
           // Cache the current value
           LONG old_x = x, old_y = y;
           // Calculate the map value
           x = (LONG)(old_x*skinTransform.eM11 + old_y*skinTransform.eM21+skinTransform.eDx);
           y = (LONG)(old_x*skinTransform.eM12 + old_y*skinTransform.eM22+skinTransform.eDy);
    }

    void QueueKeyEvent( unsigned __int32 keymap, unsigned __int32 event );

    void shutDown();
    void resetToDefaultOrientation();

    // Supporting dialogs parented at the WinController's window
    __inline HWND GetWincontrollerHWND(void) { return hWnd; }

protected:
    HWND hWnd;
    HWND hwndTip;
    HBITMAP m_bitmapHandles[4];
    XFORM skinTransform;        // Maping to the screen view
};

extern WinController * WinCtrlInstance;

#endif // WINCONTROLLER__H_
