/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * This file is part of xine for win32 video player.
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
 * Xine win32 UI
 * by Matthew Grooms <elon@altavista.com>
 */

#include "xineui.h"

#define WINDOW_WIDTH	640
#define WINDOW_HEIGHT	480

LRESULT CALLBACK proc_videownd( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
	XINE_UI * xine_ui = ( XINE_UI * ) GetWindowLong( hwnd, GWL_USERDATA );
 
    switch( msg )
    {
		case WM_RBUTTONDOWN:
		{
			if( xine_ui )
				if( xine_ui->hctrlwnd )
				{
					SetWindowPos( xine_ui->hctrlwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW );
					UpdateWindow( xine_ui->hpanelwnd );
				}

            return 0L;
		}

        case WM_MOVE:
		{
	        if( xine_ui )
				xine_ui->DriverMessage( GUI_WIN32_MOVED_OR_RESIZED, 0 );
            return 0L;
		}

        case WM_SIZE:
		{
	        if( xine_ui )
				xine_ui->DriverMessage( GUI_WIN32_MOVED_OR_RESIZED, 0 );
            return 0L;
		}

        case WM_DESTROY:
		{
            PostQuitMessage( 0 );
            return 0L;
		}
    }

    return DefWindowProc( hwnd, msg, wparam, lparam);
}


bool XINE_UI::init_videownd()
{
    WNDCLASSEX	wc;
    HWND		desktop;
	HDC			hdc;
    COLORREF	colorkey; 

	// colorkey section borrowed from videolan code

	desktop = GetDesktopWindow();
    hdc = GetDC( desktop );
    for( colorkey = 5; colorkey < 0xFF /*all shades of red*/; colorkey++ )
    {
        if( colorkey == GetNearestColor( hdc, colorkey ) )
          break;
    }
    ReleaseDC( desktop, hdc );

    // create the brush

    win32_visual.Brush = CreateSolidBrush( colorkey );
    win32_visual.ColorKey = ( int ) colorkey;

    // register our window class

    wc.cbSize        = sizeof( wc );
    wc.lpszClassName = TEXT( "xinevideowindow" );
    wc.lpfnWndProc   = proc_videownd;
    wc.style         = CS_VREDRAW | CS_HREDRAW;
    wc.hInstance     = hinst;
    wc.hIcon         = LoadIcon( hinst, MAKEINTRESOURCE( ico_xine_logo ) );
    wc.hIconSm       = LoadIcon( hinst, MAKEINTRESOURCE( ico_xine_logo ) );
    wc.hCursor       = LoadCursor( NULL, IDC_ARROW );
    wc.hbrBackground = ( HBRUSH ) win32_visual.Brush;
    wc.lpszMenuName  = 0;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;

    if( !RegisterClassEx( &wc ) )
	{
		error( "init_videownd : cannot register window class" );
    	return false;
	}

    // calculate the proper size for the windows given client size

    DWORD dwFrameWidth    = GetSystemMetrics( SM_CXSIZEFRAME );
    DWORD dwFrameHeight   = GetSystemMetrics( SM_CYSIZEFRAME );
    DWORD dwMenuHeight    = GetSystemMetrics( SM_CYMENU );
    DWORD dwCaptionHeight = GetSystemMetrics( SM_CYCAPTION );
    DWORD dwWindowWidth   = WINDOW_WIDTH  + dwFrameWidth * 2;
    DWORD dwWindowHeight  = WINDOW_HEIGHT + dwFrameHeight * 2 + 
                            dwMenuHeight + dwCaptionHeight;

    // create and show the main window

    hvideownd = CreateWindowEx(	0,
								TEXT( "xinevideowindow" ),
								TEXT( "xine Video Output" ),
								WS_SIZEBOX | WS_SYSMENU | WS_MAXIMIZEBOX,
								CW_USEDEFAULT, CW_USEDEFAULT,
  								dwWindowWidth, dwWindowHeight,
								NULL,
								NULL,
								hinst,
								NULL );
    if( !hvideownd )
	{
		error( "init_videownd : cannot create video window" );
    	return false;
	}

    ShowWindow( hvideownd, SW_SHOW );
    UpdateWindow( hvideownd );

	win32_visual.WndHnd = hvideownd;
	SetWindowLong( hvideownd, GWL_USERDATA, ( long ) this );

	return true;
}

void XINE_UI::end_videownd()
{
	DeleteObject( win32_visual.Brush );
	DestroyWindow( hvideownd );
	UnregisterClass( "xinevideowindow", hinst );
}
