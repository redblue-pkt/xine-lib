/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * This file is part of xine, a unix video player.
 * 
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * video_out_directx.c, direct draw video output plugin for xine
 * by Matthew Grooms <elon@altavista.com>
 */

typedef unsigned char boolean;

#include <windows.h>
#include <ddraw.h>

#include "xine.h"
#include "video_out.h"
#include "video_out_win32.h"
#include "alphablend.h"
#include "xine_internal.h"
#include "yuv2rgb.h"

/**/
#define LOG 1
/**/

#define NEW_YUV 1

/* Set to 1 for RGB support */
#define RGB_SUPPORT 0

#define BORDER_SIZE		8
#define IMGFMT_NATIVE	4

// -----------------------------------------
//
//  vo_directx frame struct
//
// -----------------------------------------

typedef struct win32_frame_s
{
	vo_frame_t	vo_frame;

	uint8_t *	buffer;
	int			format;
	int			width;
	int			height;
	int			size;
	int			rcode;

}win32_frame_t;

// -----------------------------------------
//
//  vo_directx driver struct
//
// -----------------------------------------

typedef struct
{
	vo_driver_t			vo_driver;
	win32_visual_t *	win32_visual;

	LPDIRECTDRAW7			ddobj;		// direct draw object
	LPDIRECTDRAWSURFACE	    primary;	// primary dd surface
	LPDIRECTDRAWSURFACE 	secondary;	// secondary dd surface 
	LPDIRECTDRAWCLIPPER		ddclipper;	// dd clipper object
	uint8_t *				contents;	// secondary contents
	win32_frame_t          *current;    // current frame

	int			            req_format;	// requested frame format
	int			            act_format;	// actual frame format
	int			            width;	    // frame with
	int			            height;		// frame height
	double		            ratio;		// frame ratio

	yuv2rgb_factory_t      *yuv2rgb_factory;	// used for format conversion
	yuv2rgb_t              *yuv2rgb;    // used for format conversion
	int			            mode;		// rgb mode
	int		             	bytespp;	// rgb bits per pixel

}win32_driver_t;


typedef struct {
  video_driver_class_t driver_class;

  config_values_t     *config;

  char *device_name;
} directx_class_t;

// -----------------------------------------
//
//  BEGIN : Direct Draw and win32 handlers
//          for xine video output plugins.
//
// -----------------------------------------

//  Display formatted error message in 
//  popup message box.

void Error( HWND hwnd, LPSTR szfmt, ... )
{
    char tempbuff[ 256 ];
    *tempbuff = 0;
    wvsprintf(	&tempbuff[ strlen( tempbuff ) ], szfmt, ( char * )( &szfmt + 1 ) );
    MessageBox( hwnd, tempbuff, "Error", MB_ICONERROR | MB_OK | MB_APPLMODAL | MB_SYSTEMMODAL );
}

//  Update our drivers current knowledge
//  of our windows video out posistion

void UpdateRect( win32_visual_t * win32_visual )
{
    if( win32_visual->FullScreen )
    {
        SetRect( &win32_visual->WndRect, 0, 0, 
				 GetSystemMetrics( SM_CXSCREEN ),
				 GetSystemMetrics( SM_CYSCREEN ) );
    }
    else
    {
        GetClientRect( win32_visual->WndHnd, &win32_visual->WndRect );
        ClientToScreen( win32_visual->WndHnd, ( POINT * ) &win32_visual->WndRect );
        ClientToScreen( win32_visual->WndHnd, ( POINT * ) &win32_visual->WndRect + 1 );
    }
}

//  Create our direct draw object, primary 
//  surface and clipper object.
//
//  NOTE : The primary surface is more or 
//  less a viewport into the parent desktop
//  window and will always have a pixel format 
//  identical to the current display mode.

boolean CreatePrimary( win32_driver_t * win32_driver )
{
	LPDIRECTDRAW			ddobj;
    DDSURFACEDESC2			ddsd;
    HRESULT					result;

	// create direct draw object

	result = DirectDrawCreate( 0, &ddobj, 0 );
	if( result != DD_OK )
	{
		Error( 0, "DirectDrawCreate : error %i", result );
		printf( "vo_out_directx : DirectDrawCreate : error %i\n", result );
		return 0;
	}

	// set cooperative level

    result = IDirectDraw_SetCooperativeLevel( ddobj, win32_driver->win32_visual->WndHnd, DDSCL_NORMAL );
	if( result != DD_OK )
	{
		Error( 0, "SetCooperativeLevel : error %i", result );
		return 0;
	}

	// try to get new interface

	result = IDirectDraw_QueryInterface( ddobj, &IID_IDirectDraw7, (LPVOID *) &win32_driver->ddobj );
	if( result != DD_OK )
	{
		Error( 0, "ddobj->QueryInterface : DirectX 7 or higher required" );
		return 0;
	}

	// release our old interface

	IDirectDraw_Release( ddobj );

    // create primary_surface

    memset( &ddsd, 0, sizeof( ddsd ) );
    ddsd.dwSize         = sizeof( ddsd );
    ddsd.dwFlags        = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

	result = IDirectDraw7_CreateSurface( win32_driver->ddobj, &ddsd, &win32_driver->primary, 0 );
	if( result != DD_OK )
	{
		Error( 0, "CreateSurface ( primary ) : error %i ", result );
		return 0;
	}

    // create our clipper object

	result = IDirectDraw7_CreateClipper( win32_driver->ddobj, 0, &win32_driver->ddclipper, 0 );
	if( result != DD_OK )
	{
		Error( 0, "CreateClipper : error %i", result );
		return 0;
	}

    // associate our clipper with our window

    result = IDirectDrawClipper_SetHWnd( win32_driver->ddclipper, 0, win32_driver->win32_visual->WndHnd );
	if( result != DD_OK )
	{
		Error( 0, "ddclipper->SetHWnd : error %i", result );
		return 0;
	}

    // associate our primary surface with our clipper

    result = IDirectDrawSurface7_SetClipper( win32_driver->primary, win32_driver->ddclipper );
	if( result != DD_OK )
	{
		Error( 0, "ddclipper->SetHWnd : error %i", result );
		return 0;
	}

	// store our objects in our visual struct

	UpdateRect( win32_driver->win32_visual );

    return 1;
}

//  Create our secondary ( off screen ) buffer.
//  The optimal secondary buffer is a h/w 
//  overlay with the same pixel format as the
//  xine frame type. However, since this is
//  not always supported by the host h/w,
//  we will fall back to creating an rgb buffer
//  in video memory qith the same pixel format
//  as the primary surface. At least then we
//  can use h/w scaling if supported.

boolean CreateSecondary( win32_driver_t * win32_driver, int width, int height, int format )
{
    DDSURFACEDESC2			ddsd;

	if( format == XINE_IMGFMT_YV12 )
		printf( "vo_out_directx : switching to YV12 overlay type\n" );

	if( format == XINE_IMGFMT_YUY2 )
		printf( "vo_out_directx : switching to YUY2 overlay type\n" );

#if RGB_SUPPORT
	if( format == IMGFMT_RGB )
		printf( "vo_out_directx : switching to RGB overlay type\n" );
#endif

	if( !win32_driver->ddobj )
		return FALSE;

	// store our reqested format,
	// width and height

	win32_driver->req_format	= format;
	win32_driver->width			= width;
	win32_driver->height		= height;

	// if we already have a secondary
	// surface then release it

	if( win32_driver->secondary )
		IDirectDrawSurface7_Release( win32_driver->secondary );

    memset( &ddsd, 0, sizeof( ddsd ) );
    ddsd.dwSize         = sizeof( ddsd );
    ddsd.dwWidth        = width;
    ddsd.dwHeight       = height;

	if( format == XINE_IMGFMT_YV12 )
	{
		// the requested format is XINE_IMGFMT_YV12

	    ddsd.dwFlags        = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
		ddsd.ddsCaps.dwCaps = DDSCAPS_VIDEOMEMORY | DDSCAPS_OVERLAY; 
		ddsd.ddpfPixelFormat.dwSize =			sizeof(DDPIXELFORMAT);
		ddsd.ddpfPixelFormat.dwFlags =			DDPF_FOURCC;
		ddsd.ddpfPixelFormat.dwYUVBitCount =	16;
		ddsd.ddpfPixelFormat.dwFourCC =			mmioFOURCC( 'Y', 'V', '1', '2' );

#ifdef LOG
		printf("CreateSecondary() - act_format = (YV12) %d\n", XINE_IMGFMT_YV12);
#endif

		win32_driver->act_format = XINE_IMGFMT_YV12;
	}

	if( format == XINE_IMGFMT_YUY2 )
	{
		// the requested format is XINE_IMGFMT_YUY2

	    ddsd.dwFlags        = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
	    ddsd.ddsCaps.dwCaps = DDSCAPS_VIDEOMEMORY | DDSCAPS_OVERLAY; 
		ddsd.ddpfPixelFormat.dwSize =			sizeof(DDPIXELFORMAT);
		ddsd.ddpfPixelFormat.dwFlags =			DDPF_FOURCC;
		ddsd.ddpfPixelFormat.dwYUVBitCount =	16;
		ddsd.ddpfPixelFormat.dwFourCC =			mmioFOURCC( 'Y', 'U', 'Y', '2' );

#ifdef LOG
		printf("CreateSecondary() - act_format = (YUY2) %d\n", XINE_IMGFMT_YUY2);
#endif

		win32_driver->act_format = XINE_IMGFMT_YUY2;
	}

#if RGB_SUPPORT
	if( format == IMGFMT_RGB )
	{
		// the requested format is IMGFMT_RGB

	    ddsd.dwFlags        = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
	    ddsd.ddsCaps.dwCaps = DDSCAPS_VIDEOMEMORY | DDSCAPS_OVERLAY; 
		ddsd.ddpfPixelFormat.dwSize =			sizeof(DDPIXELFORMAT);
		ddsd.ddpfPixelFormat.dwFlags =			DDPF_RGB;
		ddsd.ddpfPixelFormat.dwYUVBitCount =	24;
		ddsd.ddpfPixelFormat.dwRBitMask =		0xff0000;
		ddsd.ddpfPixelFormat.dwGBitMask =		0x00ff00;
		ddsd.ddpfPixelFormat.dwBBitMask =		0x0000ff;

#ifdef LOG
		printf("CreateSecondary() - act_format = (RGB) %d\n", IMGFMT_RGB);
#endif

		win32_driver->act_format = IMGFMT_RGB;
	} 
#endif /* RGB_SUPPORT */

#ifdef LOG
	printf("CreateSecondary() - IDirectDraw7_CreateSurface()\n");
#endif

	if( IDirectDraw7_CreateSurface( win32_driver->ddobj, &ddsd, &win32_driver->secondary, 0 ) == DD_OK )
		return TRUE;

	// Our fallback method is to create a back buffer
	// with the same image format as the primary surface

#ifdef LOG
	printf("CreateSecondary() - Falling back to back buffer same as primary\n");
#endif

    ddsd.dwFlags        = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;

#ifdef LOG
	printf("CreateSecondary() - act_format = (NATIVE) %d\n", IMGFMT_NATIVE);
#endif

	win32_driver->act_format = IMGFMT_NATIVE;

	if( IDirectDraw7_CreateSurface( win32_driver->ddobj, &ddsd, &win32_driver->secondary, 0 ) == DD_OK )
		return TRUE;

	// This is bad. We cant even create a surface with
	// the same format as the primary surface.

	Error( 0, "CreateSurface ( Secondary ) : unable to create a suitable rendering surface" );

	return FALSE;
}

//  Destroy all direct draw driver allocated
//  resources.

void Destroy( win32_driver_t * win32_driver )
{
	if( win32_driver->ddclipper )
		IDirectDrawClipper_Release( win32_driver->ddclipper );

	if( win32_driver->primary )
		IDirectDrawSurface7_Release( win32_driver->primary );

	if( win32_driver->secondary )
		IDirectDrawSurface7_Release( win32_driver->secondary );

	if( win32_driver->ddobj )
		IDirectDraw_Release( win32_driver->ddobj );

	free( win32_driver );
}

//  Check the current pixel format of the
//  display mode. This is neccesary in case
//  the h/w does not support an overlay for
//  the native frame format.

boolean CheckPixelFormat( win32_driver_t * win32_driver )
{
	DDPIXELFORMAT	ddpf;
	HRESULT			result;

	// get the pixel format of our primary surface

    memset( &ddpf, 0, sizeof( DDPIXELFORMAT ));
    ddpf.dwSize = sizeof( DDPIXELFORMAT );
	result = IDirectDrawSurface7_GetPixelFormat( win32_driver->primary, &ddpf );
	if( result != DD_OK )
	{
		Error( 0, "IDirectDrawSurface7_GetPixelFormat ( CheckPixelFormat ) : error %u", result );
		return 0;
	}

	// TODO : support paletized video modes

	if( ( ddpf.dwFlags & DDPF_PALETTEINDEXED1 ) ||
		( ddpf.dwFlags & DDPF_PALETTEINDEXED2 ) ||
		( ddpf.dwFlags & DDPF_PALETTEINDEXED4 ) ||
		( ddpf.dwFlags & DDPF_PALETTEINDEXED8 ) ||
		( ddpf.dwFlags & DDPF_PALETTEINDEXEDTO8 ) )
		return FALSE;

	// store bytes per pixel

	win32_driver->bytespp = ddpf.dwRGBBitCount / 8;

	// find the rgb mode for software
	// colorspace conversion

	if( ddpf.dwRGBBitCount == 32 )
	{
		if( ddpf.dwRBitMask == 0xff0000 )
			win32_driver->mode = MODE_32_RGB;
		else
			win32_driver->mode = MODE_32_BGR;
	}

	if( ddpf.dwRGBBitCount == 24 )
	{
		if( ddpf.dwRBitMask == 0xff0000 )
			win32_driver->mode = MODE_24_RGB;
		else
			win32_driver->mode = MODE_24_BGR;
	}

	if( ddpf.dwRGBBitCount == 16 )
	{
		if( ddpf.dwRBitMask == 0xf800 )
			win32_driver->mode = MODE_16_RGB;
		else
			win32_driver->mode = MODE_16_BGR;
	}

	if( ddpf.dwRGBBitCount == 15 )
	{
		if( ddpf.dwRBitMask == 0x7C00 )
			win32_driver->mode = MODE_15_RGB;
		else
			win32_driver->mode = MODE_15_BGR;
	}

	return TRUE;
}

//  Create a Direct draw surface from
//  a bitmap resource..
//
//  NOTE : This is not really useful 
//  anymore since the xine logo code is
//  being pushed to the backend.


LPDIRECTDRAWSURFACE7 CreateBMP( win32_driver_t * win32_driver, int resource )
{
	LPDIRECTDRAWSURFACE7	bmp_surf;
    DDSURFACEDESC2			bmp_ddsd;
	HBITMAP					bmp_hndl;
	BITMAP					bmp_head;
	HDC						hdc_dds;
	HDC						hdc_mem;

	// load our bitmap from a resource

	if( !( bmp_hndl = LoadBitmap( win32_driver->win32_visual->HInst, MAKEINTRESOURCE( resource ) ) ) )
	{
		Error( 0, "CreateBitmap : could not load bmp resource" );
		return 0;
	}

	// create an off screen surface with
	// the same dimentions as our bitmap

	GetObject( bmp_hndl, sizeof( bmp_head ), &bmp_head );

    memset( &bmp_ddsd, 0, sizeof( bmp_ddsd ) );
    bmp_ddsd.dwSize         = sizeof( bmp_ddsd );
    bmp_ddsd.dwFlags        = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    bmp_ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    bmp_ddsd.dwWidth        = bmp_head.bmWidth;
    bmp_ddsd.dwHeight       = bmp_head.bmHeight;

	if( IDirectDraw7_CreateSurface( win32_driver->ddobj, &bmp_ddsd, &bmp_surf, 0 ) != DD_OK )
	{
		Error( 0, "CreateSurface ( bitmap ) : could not create dd surface" );
		return 0;
	}
	
	// get a handle to our surface dc,
	// create a compat dc and load
	// our bitmap into the compat dc

	IDirectDrawSurface7_GetDC( bmp_surf, &hdc_dds );
	hdc_mem = CreateCompatibleDC( hdc_dds );
	SelectObject( hdc_mem, bmp_hndl );

	// copy our bmp from the compat dc
	// into our dd surface

	BitBlt( hdc_dds, 0, 0, bmp_head.bmWidth, bmp_head.bmHeight,
			hdc_mem, 0, 0, SRCCOPY );

	// clean up

	DeleteDC( hdc_mem );
	DeleteObject( bmp_hndl );
	IDirectDrawSurface7_ReleaseDC( bmp_surf, hdc_dds );

	return bmp_surf;
}

//  Merge overlay with the current primary 
//  surface. This funtion is only used when
//  a h/w overlay of the current frame type
//  is supported.

boolean Overlay( LPDIRECTDRAWSURFACE7 src_surface, RECT * src_rect,
			 LPDIRECTDRAWSURFACE7 dst_surface, RECT * dst_rect,
			 COLORREF color_key )
{
    DWORD					dw_color_key;
    DDPIXELFORMAT			ddpf;
	DDOVERLAYFX				ddofx;	
	int						flags;
	HRESULT					result;

	// compute the colorkey pixel value from the RGB value we've got/
	// NOTE : based on videolan colorkey code

    memset( &ddpf, 0, sizeof( DDPIXELFORMAT ));
    ddpf.dwSize = sizeof( DDPIXELFORMAT );
    result = IDirectDrawSurface7_GetPixelFormat( dst_surface, &ddpf );
	if( result != DD_OK )
	{
		Error( 0, "IDirectDrawSurface7_GetPixelFormat : could not get surface pixel format" );
		return FALSE;
	}

    dw_color_key = ( DWORD ) color_key;
    dw_color_key = ( DWORD ) ( ( ( dw_color_key * ddpf.dwRBitMask ) / 255 ) & ddpf.dwRBitMask );

    memset( &ddofx, 0, sizeof( DDOVERLAYFX ) );
    ddofx.dwSize = sizeof( DDOVERLAYFX );
    ddofx.dckDestColorkey.dwColorSpaceLowValue = dw_color_key;
    ddofx.dckDestColorkey.dwColorSpaceHighValue = dw_color_key;

	// set our overlay flags

    flags = DDOVER_SHOW | DDOVER_KEYDESTOVERRIDE;

	// attempt to overlay the surface

	result = IDirectDrawSurface7_UpdateOverlay( src_surface, src_rect, dst_surface, dst_rect, flags, &ddofx );
	if( result != DD_OK )
	{
		if( result == DDERR_SURFACELOST )
	    {
			IDirectDrawSurface7_Restore( src_surface );
			IDirectDrawSurface7_Restore( dst_surface );

			IDirectDrawSurface7_UpdateOverlay( src_surface, src_rect, dst_surface, dst_rect, flags, &ddofx );
		}
		else
		{
			Error( 0, "IDirectDrawSurface7_UpdateOverlay : error %i", result );
			return FALSE;
		}
	}

	return TRUE;
}

//  Copy our off screen surface into our primary
//  surface. This funtion is only used when a
//  h/w overlay of the current frame format is
//  not supported.

boolean BltCopy( LPDIRECTDRAWSURFACE7 src_surface, RECT * src_rect,
				 LPDIRECTDRAWSURFACE7 dst_surface, RECT * dst_rect )
{
	DDSURFACEDESC	ddsd_target;
	HRESULT			result;

	memset( &ddsd_target, 0, sizeof( ddsd_target ) );
	ddsd_target.dwSize = sizeof( ddsd_target );

	// attempt to blt the surface sontents

	result = IDirectDrawSurface7_Blt( dst_surface, dst_rect, src_surface, src_rect, DDBLT_WAIT, 0 );
	if( result != DD_OK )
	{
		if( result != DDERR_SURFACELOST )
		{
			IDirectDrawSurface7_Restore( src_surface );
			IDirectDrawSurface7_Restore( dst_surface );

			IDirectDrawSurface7_Blt( dst_surface, dst_rect, src_surface, src_rect, DDBLT_WAIT, 0 );
		}
		else
		{
			Error( 0, "IDirectDrawSurface7_Blt : error %i", result );
			return FALSE;
		}
	}

	return TRUE;
}

//  Display our current frame. This function
//  corrects frame output ratio and clipps the
//  frame if nessesary. It will then handle
//  moving the image contents contained in our
//  secondary surface to our primary surface.

boolean DisplayFrame( win32_driver_t * win32_driver )
{
	int						view_width;
	int						view_height;
	int						scaled_width;
	int						scaled_height;
	int						screen_width;
	int						screen_height;
	RECT					clipped;
	RECT					centered;

	// aspect ratio calculations

	// TODO : account for screen ratio as well

	view_width	= win32_driver->win32_visual->WndRect.right - win32_driver->win32_visual->WndRect.left;
	view_height	= win32_driver->win32_visual->WndRect.bottom - win32_driver->win32_visual->WndRect.top;

	if( view_width / win32_driver->ratio < view_height )
	{
		scaled_width = view_width - BORDER_SIZE;
		scaled_height = view_width / win32_driver->ratio - BORDER_SIZE;
	}
	else
	{
		scaled_width = view_height * win32_driver->ratio - BORDER_SIZE;
		scaled_height = view_height - BORDER_SIZE;
	}

	// center our overlay in our view frame

	centered.left = ( view_width - scaled_width ) / 2 + win32_driver->win32_visual->WndRect.left;
	centered.right = centered.left + scaled_width;
	centered.top = ( view_height - scaled_height ) / 2 + win32_driver->win32_visual->WndRect.top;
	centered.bottom = centered.top + scaled_height;

	// clip our overlay if it is off screen

	screen_width	= GetSystemMetrics( SM_CXSCREEN );
	screen_height	= GetSystemMetrics( SM_CYSCREEN );

	if( centered.left < 0 )
	{
		double x_scale = ( double ) ( view_width + centered.left ) / ( double ) view_width;
		clipped.left = win32_driver->width - ( int ) ( win32_driver->width * x_scale );
		centered.left = 0;
	}
	else
		clipped.left = 0;

	if( centered.top < 0 )
	{
		double y_scale = ( double ) ( view_height + centered.top ) / ( double ) view_height;
		clipped.left = win32_driver->height - ( int ) ( win32_driver->height * y_scale );
		centered.left = 0;
	}
	else
		clipped.top = 0;

	if( centered.right > screen_width )
	{
		double x_scale = ( double ) ( view_width - ( centered.right - screen_width ) ) / ( double ) view_width;
		clipped.right = ( int ) ( win32_driver->width * x_scale );
		centered.right = screen_width;
	}
	else
		clipped.right = win32_driver->width;

	if( centered.bottom > screen_height )
	{
		double y_scale = ( double ) ( view_height - ( centered.bottom - screen_height ) ) / ( double ) view_height;
		clipped.bottom = ( int ) ( win32_driver->height * y_scale );
		centered.bottom = screen_height;
	}
	else
		clipped.bottom = win32_driver->height;

	// if surface is entirely off screen or the
	// width or height is 0 for the overlay or
	// the output view area, then return without
	// overlay update

	if( ( centered.left > screen_width ) ||
		( centered.top >  screen_height ) ||
		( centered.right < 0 ) ||
		( centered.bottom < 0 ) ||
		( clipped.left >= clipped.right ) ||
		( clipped.top >= clipped.bottom ) ||
		( view_width <= 0 ) ||
		( view_height <= 0 ) )

		return 1;

	// we have a h/w supported overlay

	if( ( win32_driver->act_format == XINE_IMGFMT_YV12 ) || ( win32_driver->act_format == XINE_IMGFMT_YUY2 ) )
		return Overlay( win32_driver->secondary, &clipped, win32_driver->primary, &centered, win32_driver->win32_visual->ColorKey );

	// we do not have a h/w supported overlay

	return BltCopy( win32_driver->secondary, &clipped, win32_driver->primary, &centered );
}

// Lock our back buffer to update its contents.

void * Lock( void * surface )
{
	LPDIRECTDRAWSURFACE7	lock_surface = ( LPDIRECTDRAWSURFACE7 ) surface;
	DDSURFACEDESC2			ddsd;
	HRESULT					result;

	if( !surface )
		return 0;

	memset( &ddsd, 0, sizeof( ddsd ) );
	ddsd.dwSize = sizeof( ddsd );

	result = IDirectDrawSurface7_Lock( lock_surface, 0, &ddsd, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, 0 );
	if( result == DDERR_SURFACELOST )
    {
		IDirectDrawSurface7_Restore( lock_surface );
		result = IDirectDrawSurface7_Lock( lock_surface, 0, &ddsd, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, 0 );

		if( result != DD_OK )
			return 0;

	}
	else if( result != DD_OK )
	{
		if( result == DDERR_GENERIC )
		{
			Error( 0, "surface->Lock : error, DDERR_GENERIC" );
			exit( 1 );
		}
	}

	return ddsd.lpSurface;

	return 0;
}

// Unlock our back buffer to prepair for display.

void Unlock( void * surface )
{
	LPDIRECTDRAWSURFACE7 lock_surface = ( LPDIRECTDRAWSURFACE7 ) surface;

	if( !surface )
		return;

	IDirectDrawSurface7_Unlock( lock_surface, 0 );
}

// -----------------------------------------
//
//  BEGIN : Xine driver video output plugin
//          handlers.
//
// -----------------------------------------

static uint32_t win32_get_capabilities( vo_driver_t * vo_driver )
{
	uint32_t retVal;

	retVal = VO_CAP_YV12 | VO_CAP_YUY2;

#if RGB_SUPPORT
	retVal |= VO_CAP_RGB;
#endif /* RGB_SUPPORT */

	return retVal;
}

static void win32_frame_field( vo_frame_t * vo_frame, int which_field )
{
	// I have no idea what this even
	//  does, frame interlace stuff?
}

static void win32_free_framedata(vo_frame_t* vo_frame)
{

   win32_frame_t * frame = ( win32_frame_t * ) vo_frame;

   if(frame->vo_frame.base[0]) {
      free(frame->vo_frame.base[0]);
      frame->vo_frame.base[0] = NULL;
   }

   if(frame->vo_frame.base[1]) {
      free(frame->vo_frame.base[1]);
      frame->vo_frame.base[1] = NULL;
   }

   if(frame->vo_frame.base[2]) {
      free(frame->vo_frame.base[2]);
      frame->vo_frame.base[2] = NULL;
   }
}

static void win32_frame_dispose( vo_frame_t * vo_frame )
{
	win32_frame_t * win32_frame = ( win32_frame_t * ) vo_frame;

	if( win32_frame->buffer )
		free( win32_frame->buffer );

	win32_free_framedata(vo_frame);

	free( win32_frame );
}

static vo_frame_t * win32_alloc_frame( vo_driver_t * vo_driver )
{
	win32_frame_t * win32_frame;

	win32_frame = ( win32_frame_t * ) malloc( sizeof( win32_frame_t ) );
	memset( win32_frame, 0, sizeof( win32_frame_t ) );

	win32_frame->vo_frame.copy    = NULL;
	win32_frame->vo_frame.field   = win32_frame_field;
	win32_frame->vo_frame.dispose = win32_frame_dispose;
	win32_frame->format = -1;

	return ( vo_frame_t * ) win32_frame;
}


static void win32_update_frame_format( vo_driver_t * vo_driver, vo_frame_t * vo_frame, uint32_t width, 
									   uint32_t height, int ratio_code, int format, int flags )
{
	win32_driver_t * win32_driver = ( win32_driver_t * ) vo_driver;
	win32_frame_t * win32_frame = ( win32_frame_t * ) vo_frame;

	/*printf("vo_out_directx : win32_update_frame_format() - width = %d, height=%d, ratio_code=%d, format=%d, flags=%d\n", width, height, ratio_code, format, flags);*/

	if( ( win32_frame->format	!= format	) ||
		( win32_frame->width	!= width	) ||
		( win32_frame->height	!= height	) )
	{
		// free our allocated memory

		win32_free_framedata((vo_frame_t *)&win32_frame->vo_frame);

		// create new render buffer
		if( format == XINE_IMGFMT_YV12 )
		{
  	 	    win32_frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
	        win32_frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
	        win32_frame->vo_frame.pitches[2] = 8*((width + 15) / 16);

  		    win32_frame->vo_frame.base[0] = malloc(win32_frame->vo_frame.pitches[0] * height);
			win32_frame->vo_frame.base[1] = malloc(win32_frame->vo_frame.pitches[1] * ((height+1)/2));
			win32_frame->vo_frame.base[2] = malloc(win32_frame->vo_frame.pitches[2] * ((height+1)/2));

			win32_frame->size = win32_frame->vo_frame.pitches[0] * height * 2;
		}
		else if( format == XINE_IMGFMT_YUY2 )
		{
   	        win32_frame->vo_frame.pitches[0] = 8*((width + 3) / 4);

			win32_frame->vo_frame.base[0] = malloc(win32_frame->vo_frame.pitches[0] * height * 2);
	        win32_frame->vo_frame.base[1] = NULL;
	        win32_frame->vo_frame.base[2] = NULL;

			win32_frame->size = win32_frame->vo_frame.pitches[0] * height * 2;
		}
#if RGB_SUPPORT
		else if( format == IMGFMT_RGB )
		{
			win32_frame->size = width * height * 3;
			win32_frame->buffer = malloc( win32_frame->size );
			vo_frame->base[0] = win32_frame->buffer;
		}
#endif
		else
		{
			printf ( "vo_out_directx : !!! unsupported image format %04x !!!\n", format );
			exit (1);
		}

		win32_frame->format	= format;
		win32_frame->width	= width;
		win32_frame->height	= height;
		win32_frame->rcode	= ratio_code;
	}
}

static void win32_display_frame( vo_driver_t * vo_driver, vo_frame_t * vo_frame )
{
	win32_driver_t * win32_driver = ( win32_driver_t * ) vo_driver;
	win32_frame_t * win32_frame = ( win32_frame_t * ) vo_frame;
	int offset;
	int size;

	// if the required width, height or format has changed
	// then recreate the secondary buffer

	if( ( win32_driver->req_format	!= win32_frame->format	) ||
		( win32_driver->width		!= win32_frame->width	) ||
		( win32_driver->height		!= win32_frame->height	) )
	{
		CreateSecondary( win32_driver, win32_frame->width, win32_frame->height, win32_frame->format );
	}

	// determine desired ratio

	switch( win32_frame->rcode )
	{
		case ASPECT_ANAMORPHIC:
			win32_driver->ratio = 16.0 / 9.0;
		break;

		case ASPECT_DVB:
			win32_driver->ratio = 2.0 / 1.0;
		break;

		case ASPECT_SQUARE:
			win32_driver->ratio = win32_frame->width / win32_frame->height;
		break;

		case ASPECT_FULL:
		default:
			win32_driver->ratio = 4.0 / 3.0;
	}

	// lock our surface to update its contents

	win32_driver->contents = Lock( win32_driver->secondary );

	// surface unavailable, skip frame render

	if( !win32_driver->contents )
	{
		vo_frame->free( vo_frame );
		return;
	}

	// if our actual frame format is the native screen
	// pixel format, we need to convert it

	if( win32_driver->act_format == IMGFMT_NATIVE )
	{
		// use the software color conversion functions
		// to rebuild the frame in our native screen
		// pixel format ... this is slow

		if( win32_driver->req_format == XINE_IMGFMT_YV12 )
		{
			// convert from yv12 to native
			// screen pixel format

#if NEW_YUV
			win32_driver->yuv2rgb->configure( win32_driver->yuv2rgb,
						   win32_driver->width, win32_driver->height,
						   win32_driver->width, win32_driver->width/2,
						   win32_driver->width, win32_driver->height,
						   win32_driver->width * win32_driver->bytespp );
#else
			yuv2rgb_setup( win32_driver->yuv2rgb,
						   win32_driver->width, win32_driver->height,
						   win32_driver->width, win32_driver->width/2,
						   win32_driver->width, win32_driver->height,
						   win32_driver->width * win32_driver->bytespp );

#endif

			win32_driver->yuv2rgb->yuv2rgb_fun( win32_driver->yuv2rgb,
										win32_driver->contents,
										win32_frame->vo_frame.base[0],
										win32_frame->vo_frame.base[1],
										win32_frame->vo_frame.base[2] );
		}

		if( win32_driver->req_format == XINE_IMGFMT_YUY2 )
		{
			// convert from yuy2 to native
			// screen pixel format
#if NEW_YUV
			win32_driver->yuv2rgb->configure( win32_driver->yuv2rgb,
						   win32_driver->width, win32_driver->height,
						   win32_driver->width, win32_driver->width/2,
						   win32_driver->width, win32_driver->height,
						   win32_driver->width * win32_driver->bytespp );
#else

			yuv2rgb_setup( win32_driver->yuv2rgb,
						   win32_driver->width, win32_driver->height,
						   win32_driver->width, win32_driver->width/2,
						   win32_driver->width, win32_driver->height,
						   win32_driver->width * win32_driver->bytespp );

#endif
			win32_driver->yuv2rgb->yuy22rgb_fun( win32_driver->yuv2rgb,
										win32_driver->contents,
										win32_frame->vo_frame.base[0] );
		}

#if RGB_SUPPORT
		if( win32_driver->req_format == IMGFMT_RGB )
		{
			// convert from 24 bit rgb to native
			// screen pixel format

			// TODO : rgb2rgb conversion
		}
#endif
	}
	else
	{
		// the actual format is identical to our
		// stream format. we just need to copy it

		switch(win32_frame->format)
		{
		    case XINE_IMGFMT_YV12:
			{
				vo_frame_t *frame;
				uint8_t *img;

				frame = vo_frame;
				img = (uint8_t *)win32_driver->contents;

				offset = 0;
				size = frame->pitches[0] * frame->height;
		        memcpy( img+offset, frame->base[0], size);

				offset += size;
				size = frame->pitches[2]* frame->height / 2;
				memcpy( img+offset, frame->base[2], size);
				
				offset += size;
				size = frame->pitches[1] * frame->height / 2;
				memcpy( img+offset, frame->base[1], size);
			}
				break;
			case XINE_IMGFMT_YUY2:
		        memcpy( win32_driver->contents, win32_frame->vo_frame.base[0], win32_frame->vo_frame.pitches[0] * win32_frame->vo_frame.height * 2);
				break;
			default:
		        memcpy( win32_driver->contents, win32_frame->vo_frame.base[0], win32_frame->vo_frame.pitches[0] * win32_frame->vo_frame.height * 2);
				break;
		}
	}

	// unlock the surface 

	Unlock( win32_driver->secondary );

	// scale, clip and display our frame

	DisplayFrame( win32_driver );

	// tag our frame as displayed
    if((win32_driver->current != NULL) && (win32_driver->current != vo_frame)) {
        vo_frame->free(&win32_driver->current->vo_frame);
	}
    win32_driver->current = vo_frame;  
}

static void win32_overlay_blend( vo_driver_t * vo_driver, vo_frame_t * vo_frame, vo_overlay_t * vo_overlay )
{
	win32_frame_t * win32_frame = ( win32_frame_t * ) vo_frame;

	// temporary overlay support, somthing more appropriate
	// for win32 will be devised at a later date

	if( vo_overlay->rle )
	{
		if( vo_frame->format == XINE_IMGFMT_YV12 )
			blend_yuv( win32_frame->vo_frame.base, vo_overlay, win32_frame->width, win32_frame->height, win32_frame->vo_frame.pitches );
		else
			blend_yuy2( win32_frame->vo_frame.base[0], vo_overlay, win32_frame->width, win32_frame->height, win32_frame->vo_frame.pitches[0] );
	}
}

static int win32_get_property( vo_driver_t * vo_driver, int property )
{
#ifdef LOG
	printf( "win32_get_property\n" );
#endif

	return 0;
}

static int win32_set_property( vo_driver_t * vo_driver, int property, int value )
{
	return value;
}

static void win32_get_property_min_max( vo_driver_t * vo_driver, int property, int * min, int * max )
{
	*min = 0;
	*max = 0;
}

static int win32_gui_data_exchange( vo_driver_t * vo_driver, int data_type, void * data )
{
	win32_driver_t * win32_driver = ( win32_driver_t * ) vo_driver;

	switch( data_type )
	{
		case GUI_WIN32_MOVED_OR_RESIZED:
			UpdateRect( win32_driver->win32_visual );
			DisplayFrame( win32_driver );
		break;
	}

  return 0;
}


static int win32_redraw_needed(vo_driver_t* this_gen)
{
  win32_driver_t* win32_driver = (win32_driver_t *) this_gen;

  int ret = 0;

  /* TC - May need to revisit this! */
#ifdef TC  
  if( vo_scale_redraw_needed( &win32_driver->sc ) ) {
    win32_gui_data_exchange(this_gen, GUI_WIN32_MOVED_OR_RESIZED, 0);    
    ret = 1;
  }
#endif
  
  return ret;
}

static void win32_exit( vo_driver_t * vo_driver )
{
	win32_driver_t * win32_driver = ( win32_driver_t * ) vo_driver;

	free(win32_driver->win32_visual);

	Destroy( win32_driver );
}

static vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *win32_visual)
/*vo_driver_t *init_video_out_plugin( config_values_t * config, void * win32_visual )*/
{

	/* Make sure that the DirectX drivers are available and present! */
	/* Not complete yet */

	win32_driver_t * win32_driver = ( win32_driver_t * ) malloc ( sizeof( win32_driver_t ) );
	memset( win32_driver, 0, sizeof( win32_driver_t ) );

	win32_driver->win32_visual						= win32_visual;
	win32_driver->vo_driver.get_capabilities		= win32_get_capabilities;
	win32_driver->vo_driver.alloc_frame				= win32_alloc_frame ;
	win32_driver->vo_driver.update_frame_format		= win32_update_frame_format;
	win32_driver->vo_driver.display_frame			= win32_display_frame;
	win32_driver->vo_driver.overlay_blend			= win32_overlay_blend;
	win32_driver->vo_driver.get_property			= win32_get_property;
	win32_driver->vo_driver.set_property			= win32_set_property;
	win32_driver->vo_driver.get_property_min_max	= win32_get_property_min_max;
	win32_driver->vo_driver.gui_data_exchange		= win32_gui_data_exchange;
	win32_driver->vo_driver.dispose					= win32_exit;
    win32_driver->vo_driver.redraw_needed           = win32_redraw_needed;

	CreatePrimary( win32_driver );
	if( !CheckPixelFormat( win32_driver ) )
	{
		Error( 0, "vo_directx : Your screen pixel format is not supported" );
		Destroy( win32_driver );
		return 0;
	}

#if (NEW_YUV)
	win32_driver->yuv2rgb_factory = yuv2rgb_factory_init( win32_driver->mode, 0, 0 );
	win32_driver->yuv2rgb = win32_driver->yuv2rgb_factory->create_converter(win32_driver->yuv2rgb_factory);
#else
	win32_driver->yuv2rgb = yuv2rgb_init( win32_driver->mode, 0, 0 );
#endif

	return ( vo_driver_t * ) win32_driver;
}    


static char* get_identifier (video_driver_class_t *this_gen) {
  return "DirectX";
}

static char* get_description (video_driver_class_t *this_gen) {
  return _("xine video output plugin for win32 using directx");
}

static void dispose_class (video_driver_class_t *this_gen) {

  directx_class_t *directx = (directx_class_t *) this_gen;
  free (directx);
}

static void *init_class (xine_t *xine, void *visual_gen) {

    directx_class_t    *directx;
    char*             device_name;

#ifdef TC
    int               fd;
#endif

    device_name = xine->config->register_string(xine->config,
					"video.directx_device", "/dev/directx",
					_("xine video output plugin for win32 using directx"), 
					NULL, 10, NULL, NULL);

#ifdef TC
    /* check for directx device */
    if((fd = open(device_name, O_RDWR)) < 0) {
       printf("video_out_directx: aborting. (unable to open directx device \"%s\")\n", device_name);
       return NULL;
	}
    close(fd);
#endif
    
  /*
   * from this point on, nothing should go wrong anymore
   */
  directx = (directx_class_t *) malloc (sizeof (directx_class_t));
  memset( directx, 0, sizeof( directx_class_t ) );

  directx->driver_class.open_plugin     = open_plugin;
  directx->driver_class.get_identifier  = get_identifier;
  directx->driver_class.get_description = get_description;
  directx->driver_class.dispose         = dispose_class;

  directx->config            = xine->config;
  directx->device_name       = device_name;
  
  return directx;
}

static vo_info_t vo_info_win32 = {
  7,                    /* priority    */
  XINE_VISUAL_TYPE_DIRECTX  /* visual type */
};

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 15, "vo_directx", XINE_VERSION_CODE, &vo_info_win32, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
