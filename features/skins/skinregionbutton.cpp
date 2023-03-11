/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "emulator.h"
#include "config.h"
#include "WinController.h"
#include "XMLSkin.h"
#include "skinregionbutton.h"
#include "bitmaputils.h"
#include "resource.h"

#define EMUL_RESET_MAPPED_KEY  0x75
/*------------------------------------------------------------------
	Construction/Destruction
------------------------------------------------------------------*/

ISkinRegionButton* CreateSkinRegionButton(WinController * controller)
{
	return new SkinRegionButton(controller);
}

SkinRegionButton::SkinRegionButton(WinController * controller)
:m_controller(controller),m_state(Normal),m_activeToolTip(false),
 m_region(NULL), m_scaledRegion(NULL),m_caption(NULL),m_continueRepeat(false),
 m_repeatDetected(false) // m_boundingRect, m_scaledBoundingRect
{
}

SkinRegionButton::~SkinRegionButton()
{
    if (m_caption != NULL)
        delete [] m_caption;
}

/*------------------------------------------------------------------
	Save and restore the button state from the saved state file
------------------------------------------------------------------*/
void SkinRegionButton::SaveState(StateFiler& filer)
{
    // filer.Write(m_controller)
    // filer.Write(m_state) - reset upon restore
    // filer.Write(m_activeToolTip) - reset upon restore
    // filer.Write(m_scaledRegion) - can be regenerated from the m_region + transform
    // filer.Write(m_boundingRect) - can be regenerated from m_region
    // filer.Write(m_scaledBoundingRect) - can be regenerated from m_scaledRegion
    // filer.Write(m_ToolTipInfo) - needs to be recreate on restore
    filer.Write('SBTN');

    // Store m_region
    RGNDATAHEADER * RegionDataHeader = NULL;
    RECT * Rectangles = NULL;
    unsigned int RegionDataSize;
    std::vector<unsigned __int32>::iterator forwardIntIter;

    if ( !getButtonRegionData( &RegionDataHeader, &Rectangles, RegionDataSize ) )
    {
        filer.setStatus(false);
        goto DoneStore;
    }

    filer.Write( RegionDataSize );
    filer.LZWrite( (unsigned char *)RegionDataHeader, RegionDataSize );

    // Store the caption string
    filer.WriteString(m_caption);

    // Store the keystroke vectors
    filer.Write((unsigned int)m_onPressAndHold.size());
    forwardIntIter = m_onPressAndHold.begin();

    while ( forwardIntIter != m_onPressAndHold.end() )
    {
        filer.Write(*forwardIntIter);
        forwardIntIter++;
    }

    filer.Write((unsigned int)m_onClick.size());
    forwardIntIter = m_onClick.begin();

    while ( forwardIntIter != m_onClick.end() )
    {
        filer.Write(*forwardIntIter);
        forwardIntIter++;
    }

DoneStore:
    if ( RegionDataHeader != NULL )
        delete [] (unsigned char *)RegionDataHeader;
}

void SkinRegionButton::RestoreState(StateFiler& filer)
{
    // filer.Write(m_scaledRegion) - can be regenerated from the m_region + transform
    // filer.Write(m_boundingRect) - can be regenerated from m_region
    // filer.Write(m_scaledBoundingRect) - can be regenerated from m_scaledRegion
    // filer.Write(m_ToolTipInfo) - needs to be recreate on restore

    filer.Verify('SBTN');

    bool success = false;
    RGNDATAHEADER * RegionDataHeader = NULL;
    RECT * Rectangles = NULL;
    unsigned char * RegionDataBuffer = NULL;
    unsigned int RegionDataSize;

    filer.Read( RegionDataSize );
    if ( (RegionDataSize < sizeof(RGNDATAHEADER)) ||
         ((RegionDataSize - sizeof(RGNDATAHEADER)) % sizeof(RECT) != 0) )
        goto DoneRestore;

    RegionDataBuffer = new unsigned char[RegionDataSize];
    if ( RegionDataBuffer == NULL )
        goto DoneRestore;

    filer.LZRead( (unsigned char *)RegionDataBuffer, RegionDataSize );

    RegionDataHeader = (RGNDATAHEADER *)RegionDataBuffer;
    Rectangles = (RECT *)(RegionDataBuffer + sizeof(RGNDATAHEADER));

    if ( RegionDataHeader->iType != RDH_RECTANGLES )
        goto DoneRestore;

    m_region = ExtCreateRegion(NULL, RegionDataSize, (const RGNDATA *)RegionDataHeader);

    if ( !m_region )
        goto DoneRestore;

    GetRgnBox(m_region, &m_boundingRect);

    filer.ReadString(m_caption);

    // Restore the keystroke vectors
    unsigned vector_size = 0;
    filer.Read(vector_size);

    for ( unsigned int i = 0; i < vector_size; i++)
    {
        unsigned value = 0;
        filer.Read(value);
        m_onPressAndHold.insert(m_onPressAndHold.end(), value );
    }

    vector_size = 0;
    filer.Read(vector_size);

    for ( unsigned int i = 0; i < vector_size; i++)
    {
        unsigned value = 0;
        filer.Read(value);
        m_onClick.insert(m_onClick.end(), value );
    }

    success = true && filer.getStatus();

DoneRestore:
    filer.setStatus(success);
    if ( RegionDataBuffer != NULL )
        delete [] RegionDataBuffer;
}

/*------------------------------------------------------------------
	Determine the button region from the mapping image
------------------------------------------------------------------*/
void SkinRegionButton::SetRegion( HBITMAP inBitmap, COLORREF inColorRef )
{
	if(inBitmap != NULL)
	{
		// Get the region for the button from the mapping image
		m_region = BitmapToRegion(inBitmap, inColorRef, RGB(0,0,0) );
		// Get the bounding box for the region
		GetRgnBox(m_region, &m_boundingRect);
		// Initially there is no scale/transform
		m_scaledRegion = m_region;
		m_scaledBoundingRect = m_boundingRect;
	}
}

/*------------------------------------------------------------------
	Intialize the tooltipinfo structure
------------------------------------------------------------------*/

void SkinRegionButton::initToolTip( HWND hWnd, int id, const wchar_t * tip )
{
    if ( tip != NULL )
    {
        m_caption = _wcsdup(tip);

        if (m_caption == NULL) {
            TerminateWithMessage(ID_MESSAGE_INTERNAL_ERROR);
        }
    }

    m_ToolTipInfo.hwnd = hWnd;
    m_ToolTipInfo.rect = m_boundingRect;
    m_ToolTipInfo.lpszText = (LPWSTR)m_caption;
    m_ToolTipInfo.uFlags = TTF_TRANSPARENT | TTF_CENTERTIP;
    m_ToolTipInfo.uId = id; 
    m_ToolTipInfo.cbSize = sizeof(m_ToolTipInfo);
    m_ToolTipInfo.lParam = NULL;
    m_ToolTipInfo.hinst  = NULL;
}

/*------------------------------------------------------------------
	Event handlers
------------------------------------------------------------------*/

void SkinRegionButton::OnLButtonDown( HWND hWnd, UINT uMsg, LPARAM point)
{
    if ( m_state != Disabled )
    {
        if ( PtInRegion(m_region, LOWORD(point),HIWORD(point) ) )
        {
            m_state = Push;

            InvalidateRect(hWnd, &m_scaledBoundingRect, FALSE);

            // Handle on press and hold items
            std::vector<unsigned __int32>::iterator forwardIntIter;
            forwardIntIter = m_onPressAndHold.begin();

            while ( forwardIntIter != m_onPressAndHold.end() )
            {
                m_controller->QueueKeyEvent((*forwardIntIter)& 0x0000FFFF, 1);
                forwardIntIter++;
                // We should repeat the keystrokes
                m_continueRepeat = true;
            }
            if ( m_continueRepeat )
            {
                //EnterCriticalSection(&m_KeyRepeatLock);
                SetTimer(hWnd,                // handle to main window 
                         IDT_TIMERKEYREPEAT,               // timer identifier 
                         InitialPeriod,                     // 5-second interval 
                         NULL); // timer callback
                //LeaveCriticalSection(&m_KeyRepeatLock);
            }
        }
    } // m_state != Disabled
}

void SkinRegionButton::OnTimer(HWND hWnd)
{
    // Note that this code doesn't need to be protected with a critsec
    // because it always runs on the same thread as OnLButtonDown and OnLButtonUp
    // if that ever changes the critical section will become necessary
    //EnterCriticalSection(&m_KeyRepeatLock);
    if (m_continueRepeat) {
        std::vector<unsigned __int32>::iterator forwardIntIter;
        forwardIntIter = m_onPressAndHold.begin();

        while ( forwardIntIter != m_onPressAndHold.end() && m_continueRepeat)
        {
            if ( (*forwardIntIter) != EMUL_RESET_MAPPED_KEY ) {
                m_controller->QueueKeyEvent((*forwardIntIter)& 0x0000FFFF, 1);
            }
            forwardIntIter++;
        }
        
        // Restart a faster repeat timer after first keypress
        if ( m_continueRepeat && !m_repeatDetected )
        {
           m_repeatDetected = true;
           KillTimer(hWnd,IDT_TIMERKEYREPEAT);
           SetTimer(hWnd, IDT_TIMERKEYREPEAT, KeyRepeatPeriod, NULL);
        }
    }
    //LeaveCriticalSection(&m_KeyRepeatLock);
}


void SkinRegionButton::OnLButtonUp( HWND hWnd, UINT uMsg, LPARAM point)
{
    if ( m_state != Disabled )
    {
        if (m_state == Push)
        {
            // Shut down the key repeat timer
            m_continueRepeat = false;
            m_repeatDetected = false;
            //EnterCriticalSection(&m_KeyRepeatLock);
            KillTimer(hWnd,IDT_TIMERKEYREPEAT);
            //LeaveCriticalSection(&m_KeyRepeatLock);
            std::vector<unsigned __int32>::iterator forwardIntIter;
            // Handle on press and hold items
            forwardIntIter = m_onPressAndHold.begin();

            while ( forwardIntIter != m_onPressAndHold.end() )
            {
                m_controller->QueueKeyEvent((*forwardIntIter)& 0x0000FFFF, 2);
                forwardIntIter++;
            }

            // Handle on click items
            forwardIntIter = m_onClick.begin();

            // Special Shutdown flag
            if( m_onClick.size() == 1 && *forwardIntIter & 0x00010000 )
            {
                m_controller->shutDown();
            }

            while ( forwardIntIter != m_onClick.end() )
            {
                if ( *forwardIntIter & 0x80000000 )
                    m_controller->QueueKeyEvent((*forwardIntIter)& 0x0000FFFF, 1);
                else if( *forwardIntIter & 0x40000000 )
                    m_controller->QueueKeyEvent((*forwardIntIter)& 0x0000FFFF, 2);
                else
                    m_controller->QueueKeyEvent(*(forwardIntIter)& 0x0000FFFF, 3);
                
                forwardIntIter++;
            }
        }

        if ( m_state != Normal )
        {
            m_state = Normal;
            InvalidateRect(hWnd, &m_scaledBoundingRect, FALSE);
        }
    } // m_state != Disabled
}

void SkinRegionButton::OnMouseMotion(HWND hWnd, UINT uMsg, LPARAM point)
{
    if ( m_state != Disabled )
    {
        HWND hToolTip = m_controller->getToolTip();
        bool InvalidateImage = false;

        if ( PtInRegion(m_region, LOWORD(point),HIWORD(point) ) )
        {
            // If the button is pressed do nothing - wait for button up event
            if ( m_state == Push )
                return;

            // Update the button state and force the redraw if necessary
            if ( m_state != Focus )
                InvalidateImage = true;
            m_state = Focus;

            // Activate the tool tip
            if ( hToolTip != NULL )
                if (!SendMessage(hToolTip,TTM_TRACKACTIVATE,(WPARAM)TRUE,(LPARAM)&m_ToolTipInfo))
                    m_controller->setToolTip(NULL);
                else
                    m_activeToolTip = true;
        }
        else
        {
            // Update the button state and force the redraw if necessary
            if ( m_state == Focus )
                InvalidateImage = true;
            else if ( m_state == Push )
                OnLButtonUp(hWnd, uMsg, point ); // Force button release

            m_state = Normal;

            // Deactivate the tooltip
            if ( hToolTip != NULL && m_activeToolTip)
            {
                m_activeToolTip = false;
                if (!SendMessage(hToolTip,TTM_TRACKACTIVATE,(WPARAM)FALSE,(LPARAM)&m_ToolTipInfo))
                    m_controller->setToolTip(NULL);
            }
        }

        if ( InvalidateImage )
            InvalidateRect(hWnd, &m_scaledBoundingRect, FALSE);
    }
}

void SkinRegionButton::OnKillFocus(HWND hWnd)
{
    // Release the button if it is currently pressed
    if ( m_state == Push )
        OnLButtonUp(hWnd, 0, LPARAM(0) );

    // Remove focus if the button currently has it
    if ( m_state == Focus || m_activeToolTip )
    {
        HWND hToolTip = m_controller->getToolTip();

        m_state = Normal;

        // Deactivate the tooltip
        if ( hToolTip != NULL && m_activeToolTip)
        {
            m_activeToolTip = false;
            SendMessage(hToolTip,TTM_TRACKACTIVATE,(WPARAM)FALSE,(LPARAM)&m_ToolTipInfo);
        }

        InvalidateRect(hWnd, &m_scaledBoundingRect, FALSE);
    }
}

void SkinRegionButton::OnPaint(PAINTSTRUCT ps, XFORM * transform )
{
	// Get the bitmaps from the controller
	HBITMAP * skinBitmaps = m_controller->getSkinBitmaps();
	ASSERT( skinBitmaps != NULL && skinBitmaps[0] != NULL);

	// use the main bitmap for up (Normal state)
	HBITMAP pBitmap = skinBitmaps[0];

	if ((m_state == Push ) && skinBitmaps[1] != NULL)
		pBitmap = skinBitmaps[1];// the selected bitmap for down
	else if ((m_state == Focus) && skinBitmaps[2] != NULL)
		pBitmap = skinBitmaps[2];// third image for focused
	else if ((m_state == Disabled ) && skinBitmaps[3] != NULL)
		pBitmap = skinBitmaps[3]; // last image for disabled

	HDC hdcButton = CreateCompatibleDC(ps.hdc);
	HGDIOBJ hbmButton = SelectObject(hdcButton, pBitmap);

	SetGraphicsMode(ps.hdc, GM_ADVANCED);
	SetWorldTransform(ps.hdc, transform); 
	SelectClipRgn(ps.hdc, m_scaledRegion );
	BitBlt(ps.hdc, m_boundingRect.left, m_boundingRect.top, 
		m_boundingRect.right-m_boundingRect.left,
		m_boundingRect.bottom-m_boundingRect.top,
		hdcButton, m_boundingRect.left, m_boundingRect.top, SRCCOPY);
	SelectClipRgn(ps.hdc, NULL );
	ModifyWorldTransform(ps.hdc, NULL, MWT_IDENTITY);
	SetGraphicsMode(ps.hdc, GM_COMPATIBLE);

	SelectObject(hdcButton, hbmButton);
	DeleteDC(hdcButton);
}

void SkinRegionButton::OnResize(XFORM * transform)
{
    // Transform the region and calculate the new boundary
    transformBoundingRegion(transform);
    // Update the tool tip
    updateTooltip();
}

/*------------------------------------------------------------------
	Update the tool tip with the current scaled boundary
------------------------------------------------------------------*/

void SkinRegionButton::updateTooltip()
{
    HWND hToolTip = m_controller->getToolTip();
    if ( hToolTip != NULL )
    {
        // Check if the update is necessary
        if ( !EqualRect(&m_ToolTipInfo.rect, &m_scaledBoundingRect) )
        {
            // Disable the tooltip before resizing the rectangle
            if ( m_activeToolTip )
                if (!SendMessage(hToolTip,TTM_TRACKACTIVATE,(WPARAM)FALSE,(LPARAM)&m_ToolTipInfo))
                    { m_controller->setToolTip(NULL); return; }
            m_activeToolTip = false;        

            // Resize the tool tip rectangle
            m_ToolTipInfo.rect = m_scaledBoundingRect;
            SendMessage(hToolTip, TTM_NEWTOOLRECT, 0, (LPARAM)&m_ToolTipInfo);
        }
    }
}

HRGN SkinRegionButton::transformBoundingRegion(XFORM * t)
{
    HRGN newRegion = NULL;

    RGNDATAHEADER * RegionDataHeader = NULL;
    RECT * Rectangles = NULL;
    unsigned int RegionDataSize;

    if ( !getButtonRegionData( &RegionDataHeader, &Rectangles, RegionDataSize ) )
        goto DoneTransform;

    for (unsigned int i =0; i < RegionDataHeader->nCount; i++)
    {
        m_controller->translateFromMapToView( Rectangles[i].left, Rectangles[i].top);
        m_controller->translateFromMapToView( Rectangles[i].right, Rectangles[i].bottom);

        // Figure out if maximum and minimum have switched
        if ( Rectangles[i].top > Rectangles[i].bottom )
        {
            int temp = Rectangles[i].top; Rectangles[i].top = Rectangles[i].bottom; 
             Rectangles[i].bottom = temp;
        }

        if ( Rectangles[i].left > Rectangles[i].right )
        {
            int temp = Rectangles[i].left; Rectangles[i].left = Rectangles[i].right; 
            Rectangles[i].right = temp;
        }
    }

    newRegion = ExtCreateRegion(NULL, RegionDataSize, (const RGNDATA *)RegionDataHeader);
    
    // Get the bounding box for the region
    GetRgnBox(newRegion, &m_scaledBoundingRect);

DoneTransform:
    // Clean up 
    if ( RegionDataHeader != NULL )
        delete [] (unsigned char *)RegionDataHeader;
    if ( newRegion != NULL )
    {
        if ( m_scaledRegion != m_region )
            DeleteObject( m_scaledRegion );
        m_scaledRegion = newRegion;
    }

    return newRegion;
}


bool SkinRegionButton::getButtonRegionData(RGNDATAHEADER **  RegionDataHeaderOut, 
                                           RECT ** RectanglesOut,
                                           unsigned int & RegionDataSize )
{
    bool success = false;
    *RegionDataHeaderOut = NULL;
    *RectanglesOut = NULL;
    char * RegionDataBuffer = NULL;

    RegionDataSize = GetRegionData(m_region, 0,NULL);

    if ( !RegionDataSize)
        goto DoneGetData;

    RegionDataBuffer = new  char[RegionDataSize];

    if ( RegionDataBuffer == NULL)
    {
        TerminateWithMessage(ID_MESSAGE_INTERNAL_ERROR);
        goto DoneGetData;
    }

    if (RegionDataSize != 
            GetRegionData(m_region, RegionDataSize, (LPRGNDATA)RegionDataBuffer ))
        goto DoneGetData;

    RGNDATAHEADER * RegionDataHeader = (RGNDATAHEADER *)RegionDataBuffer;
    RECT * Rectangles = (RECT *)(RegionDataBuffer + sizeof(RGNDATAHEADER));

    if ( RegionDataHeader->iType != RDH_RECTANGLES )
        goto DoneGetData;

    success = true;
    *RegionDataHeaderOut = RegionDataHeader;
    *RectanglesOut = Rectangles;

DoneGetData:
    // Clean up if failed
    if ( !success && RegionDataBuffer != NULL )
        delete [] RegionDataBuffer;

    return success;
}

