/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include <string>
#include "BitmapUtils.h"
#include "resource.h"

#include "png.h"

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "bitmaputils.tmh"
#include "vsd_logging_inc.h"

using namespace std;

/*

These defines are required in pngconf.h

#define PNG_READ_EXPAND_SUPPORTED
#define PNG_READ_GRAY_TO_RGB_SUPPORTED
#define PNG_READ_STRIP_ALPHA_SUPPORTED
#define PNG_READ_BGR_SUPPORTED

*/

#pragma warning (disable:4611) // interaction between '_setjmp' and C++ object destruction is non-portable

HBITMAP read_png(const wchar_t *file_name)
{
   png_structp png_ptr;
   png_infop info_ptr = NULL;
   unsigned int sig_read = 0;
   png_uint_32 width = 0, height = 0;
   int bit_depth, color_type, interlace_type;
   png_bytep * row_pointers = NULL;
   HBITMAP imageBitmap = NULL;
   bool keep_image = false;

   FILE *fp = NULL;

   /* Create and initialize the png_struct */
   png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

   if (png_ptr == NULL)
      goto CleaupPNGRead;

   /* Allocate/initialize the memory for image information.  REQUIRED. */
   info_ptr = png_create_info_struct(png_ptr);
   if (info_ptr == NULL)
      goto CleaupPNGRead;

   if ( _wfopen_s(&fp, file_name, L"rb") )
      goto CleaupPNGRead;
   
   /* Set error handling if you are using the setjmp/longjmp method (this is
    * the normal method of doing things with libpng). */
   if (setjmp(png_ptr->jmpbuf))
   {
      goto CleaupPNGRead;
   }

   /* Set up the input control if you are using standard C streams */
   png_init_io(png_ptr, fp);

   /* If we have already read some of the signature */
   png_set_sig_bytes(png_ptr, sig_read);

   /* The call to png_read_info() gives us all of the information from the
    * PNG file before the first IDAT (image data chunk).*/
   png_read_info(png_ptr, info_ptr);

   png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
       &interlace_type, NULL, NULL);

   // Configure transformations prior to reading the image
   /* expand paletted colors into true RGB triplets */
   if (color_type == PNG_COLOR_TYPE_PALETTE)
      png_set_expand(png_ptr);

   /* expand grayscale images to the full 8 bits from 1, 2, or 4 bits/pixel */
   if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
      png_set_expand(png_ptr);

   /* convert grayscale images to RGB */
   if(color_type == PNG_COLOR_TYPE_GRAY)
      png_set_gray_to_rgb(png_ptr);

   // Swap blue and read to match the bitmap order
   png_set_bgr(png_ptr);

   if( (bit_depth != 8) || 
       (color_type != PNG_COLOR_TYPE_RGB && color_type != PNG_COLOR_TYPE_RGB_ALPHA ))
      goto CleaupPNGRead;

    /* merge in the image background if one is present */
    png_color_16 my_background;
    png_color_16p image_background;

    if (png_get_bKGD(png_ptr, info_ptr, &image_background))
        png_set_background(png_ptr, image_background,
             PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
    else
        png_set_background(png_ptr, &my_background,
          PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);

    /* add a filler byte to accomadate an empty alpha channel */
    if (bit_depth == 8 && color_type == PNG_COLOR_TYPE_RGB_ALPHA)
        png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

   // Create windows bitmap
   HDC hDC;
   char BMIData[sizeof(BITMAPINFO)+5*sizeof(RGBQUAD)];
   BITMAPINFO *bmi;

   hDC = GetDC(NULL);
   if (!hDC) {
     ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
     exit(1);
   }

   bmi = (BITMAPINFO*)BMIData;
   bmi->bmiHeader.biSize=sizeof(BITMAPINFO);
   bmi->bmiHeader.biWidth=width;
   bmi->bmiHeader.biHeight=-(int)height; // create a top-down bitmap
   bmi->bmiHeader.biPlanes=1;
   bmi->bmiHeader.biBitCount=(WORD)bit_depth*(color_type == PNG_COLOR_TYPE_RGB_ALPHA ? 4 : 3);
   bmi->bmiHeader.biCompression=BI_RGB;
   bmi->bmiHeader.biSizeImage=0;
   bmi->bmiHeader.biXPelsPerMeter=0;
   bmi->bmiHeader.biYPelsPerMeter=0;
   bmi->bmiHeader.biClrUsed=0;
   bmi->bmiHeader.biClrImportant=0;

   *(unsigned __int32*)&bmi->bmiColors[0] = 0xffffff;
   *(unsigned __int32*)&bmi->bmiColors[1] = 0xffffff;

   unsigned __int8 * image_dest = NULL;
   imageBitmap = CreateDIBSection(hDC, bmi, DIB_RGB_COLORS, (LPVOID*)&image_dest, NULL, 0);

   if ( image_dest == NULL  || imageBitmap == NULL )
      goto CleaupPNGRead;

   /* allocate the memory to hold the image using the fields of info_ptr. */
   row_pointers = new png_bytep[sizeof(png_bytep)*height];

   unsigned int row_bytes = 
    (png_get_rowbytes(png_ptr, info_ptr)+ (sizeof(LONG) - 1)) & (~(sizeof(LONG) - 1));

   if ( row_pointers == NULL )
      goto CleaupPNGRead;

   for (unsigned int row = 0; row < height; row++)
   {
      row_pointers[row] = (png_bytep)&image_dest[row * row_bytes];
      ASSERT( png_get_rowbytes(png_ptr, info_ptr) <= row_bytes );
   }

   /* Now it's time to read the image.  */
   png_read_image(png_ptr, row_pointers);

   /* read rest of file, and get additional chunks in info_ptr */
   png_read_end(png_ptr, info_ptr);

   keep_image = true;

CleaupPNGRead:
   // clean internal PNG structures
   if ( png_ptr != NULL && info_ptr != NULL)
      png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
   // clean up the memory allocated for pixel data
   if ( row_pointers != NULL )
      delete [] row_pointers;
   // close the file handle
   if (fp != NULL)
      fclose(fp);
   // delete the bitmap
   if ( !keep_image && imageBitmap != NULL )
   {
      DeleteObject( imageBitmap );
      imageBitmap = NULL;
   }
   /* that's it */
   return imageBitmap;
}


/*------------------------------------------------------------------
	LoadBitmapFromBMPFile
------------------------------------------------------------------*/

bool
LoadBitmapFromBMPFile(
	const wstring&	fileName,
	HBITMAP*		phBitmap,
	bool & TopDown)
{
	
	*phBitmap = NULL;
	TopDown = false;
	if ( !fileName.empty() )
	{
		// Use LoadImage() to get the image loaded into a DIBSection
		*phBitmap = (HBITMAP)LoadImageW( NULL/*AfxGetInstanceHandle()*/,
						 fileName.c_str(), IMAGE_BITMAP, 0, 0,
						 LR_CREATEDIBSECTION | LR_DEFAULTSIZE | LR_LOADFROMFILE );
		
		if ( *phBitmap == NULL )
		{
			*phBitmap = read_png(fileName.c_str());
			TopDown = true;
		}
	}
	
	return( *phBitmap != NULL );
}

/*------------------------------------------------------------------
	BitmapToRegion
------------------------------------------------------------------*/

HRGN
BitmapToRegion(
	HBITMAP		hBmp,
	COLORREF	cOpaqueColor,
	COLORREF	cTolerance )
{
	HRGN hRgn = NULL;
	
	if (hBmp)
	{
		// Create a memory DC inside which we will scan the bitmap content
		HDC hMemDC = CreateCompatibleDC(NULL);
		if (hMemDC)
		{
			// Get bitmap size
			BITMAP bm;
			GetObject(hBmp, sizeof(bm), &bm);

			// Create a 32 bits depth bitmap and select it into the memory DC 
			BITMAPINFOHEADER RGB32BITSBITMAPINFO = {	
					sizeof(BITMAPINFOHEADER),	// biSize 
					bm.bmWidth,					// biWidth; 
					bm.bmHeight,				// biHeight; 
					1,							// biPlanes; 
					32,							// biBitCount 
					BI_RGB,						// biCompression; 
					0,							// biSizeImage; 
					0,							// biXPelsPerMeter; 
					0,							// biYPelsPerMeter; 
					0,							// biClrUsed; 
					0							// biClrImportant; 
			};
			VOID * pbits32; 
			HBITMAP hbm32 = CreateDIBSection(hMemDC, (BITMAPINFO *)&RGB32BITSBITMAPINFO, DIB_RGB_COLORS, &pbits32, NULL, 0);
			if (hbm32)
			{
				HBITMAP holdBmp = (HBITMAP)SelectObject(hMemDC, hbm32);

				// Create a DC just to copy the bitmap into the memory DC
				HDC hDC = CreateCompatibleDC(hMemDC);
				if (hDC)
				{
					// Get how many bytes per row we have for the bitmap bits (rounded up to 32 bits)
					BITMAP bm32;
					GetObject(hbm32, sizeof(bm32), &bm32);
					while (bm32.bmWidthBytes % 4)
						bm32.bmWidthBytes++;

					// Copy the bitmap into the memory DC
					HBITMAP holdMemDCBmp = (HBITMAP)SelectObject(hDC, hBmp);
					BitBlt(hMemDC, 0, 0, bm.bmWidth, bm.bmHeight, hDC, 0, 0, SRCCOPY);

					// For better performances, we will use the ExtCreateRegion() function to create the
					// region. This function take a RGNDATA structure on entry. We will add rectangles by
					// amount of ALLOC_UNIT number in this structure.
					#define ALLOC_UNIT	100
					DWORD maxRects = ALLOC_UNIT;
					HANDLE hData = GlobalAlloc(GMEM_MOVEABLE, sizeof(RGNDATAHEADER) + (sizeof(RECT) * maxRects));
					if (!hData)
						TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
					RGNDATA *pData = (RGNDATA *)GlobalLock(hData);
					if (!pData)
						TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
					pData->rdh.dwSize = sizeof(RGNDATAHEADER);
					pData->rdh.iType = RDH_RECTANGLES;
					pData->rdh.nCount = pData->rdh.nRgnSize = 0;
					SetRect(&pData->rdh.rcBound, MAXLONG, MAXLONG, 0, 0);

					// Keep on hand highest and lowest values for the "opaque" pixels
					BYTE lr = GetRValue(cOpaqueColor);
					BYTE lg = GetGValue(cOpaqueColor);
					BYTE lb = GetBValue(cOpaqueColor);
					BYTE hr = min(0xff, lr + GetRValue(cTolerance));
					BYTE hg = min(0xff, lg + GetGValue(cTolerance));
					BYTE hb = min(0xff, lb + GetBValue(cTolerance));

					// Scan each bitmap row from bottom to top (the bitmap is inverted vertically)
					BYTE *p32 = (BYTE *)bm32.bmBits + (bm32.bmHeight - 1) * bm32.bmWidthBytes;
					for (int y = 0; y < bm.bmHeight; y++)
					{
						// Scan each bitmap pixel from left to right
						for (int x = 0; x < bm.bmWidth; x++)
						{
							// Search for a continuous range of "non transparent pixels"
							int x0 = x;
							LONG *p = (LONG *)p32 + x;
							while (x < bm.bmWidth)
							{
								BYTE b = GetRValue(*p);
								if (b >= lr && b <= hr)
								{
									b = GetGValue(*p);
									if (b >= lg && b <= hg)
									{
										b = GetBValue(*p);
										if (!( b >= lb && b <= hb ) )
										{
											// This pixel is "transparent"
											break;
										}
									}
									else
									{
										break;
									}
								}
								else
								{
									break;
								}
								p++;
								x++;
							}

							if (x > x0)
							{
								// Add the pixels (x0, y) to (x, y+1) as a new rectangle in the region
								if (pData->rdh.nCount >= maxRects)
								{
									GlobalUnlock(hData);
									maxRects += ALLOC_UNIT;
									HANDLE hTempData = GlobalReAlloc(hData, sizeof(RGNDATAHEADER) + (sizeof(RECT) * maxRects), GMEM_MOVEABLE);
									if (!hTempData)
										TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
									hData = hTempData;
									pData = (RGNDATA *)GlobalLock(hData);
									if (!pData)
										TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
								}
								RECT *pr = (RECT *)&pData->Buffer;
								SetRect(&pr[pData->rdh.nCount], x0, y, x, y+1);
								if (x0 < pData->rdh.rcBound.left)
									pData->rdh.rcBound.left = x0;
								if (y < pData->rdh.rcBound.top)
									pData->rdh.rcBound.top = y;
								if (x > pData->rdh.rcBound.right)
									pData->rdh.rcBound.right = x;
								if (y+1 > pData->rdh.rcBound.bottom)
									pData->rdh.rcBound.bottom = y+1;
								pData->rdh.nCount++;

								// On Windows98, ExtCreateRegion() may fail if the number of rectangles is too
								// large (ie: > 4000). Therefore, we have to create the region by multiple steps.
								if (pData->rdh.nCount == 2000)
								{
									HRGN h = ExtCreateRegion(NULL, sizeof(RGNDATAHEADER) + (sizeof(RECT) * maxRects), pData);
									if (hRgn)
									{
										CombineRgn(hRgn, hRgn, h, RGN_OR);
										DeleteObject(h);
									}
									else
										hRgn = h;
									pData->rdh.nCount = 0;
									SetRect(&pData->rdh.rcBound, MAXLONG, MAXLONG, 0, 0);
								}
							}
						}

						// Go to next row (remember, the bitmap is inverted vertically)
						p32 -= bm32.bmWidthBytes;
					}

					// Create or extend the region with the remaining rectangles
					HRGN h = ExtCreateRegion(NULL, sizeof(RGNDATAHEADER) + (sizeof(RECT) * maxRects), pData);
					if (hRgn)
					{
						CombineRgn(hRgn, hRgn, h, RGN_OR);
						DeleteObject(h);
					}
					else
						hRgn = h;

					// Clean up
					GlobalFree(hData);
					SelectObject(hDC, holdMemDCBmp);
					DeleteDC(hDC);
				}

				DeleteObject(SelectObject(hMemDC, holdBmp));
			}

			DeleteDC(hMemDC);
		}
	}

	return hRgn;
}

