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

#define WINDOW_WIDTH	215
#define WINDOW_HEIGHT	85

HIMAGELIST	himagelist;

LRESULT CALLBACK proc_ctrlwnd( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
	XINE_UI * xine_ui = ( XINE_UI * ) GetWindowLong( hwnd, GWL_USERDATA );

    switch( msg )
    {
		case WM_COMMAND:
		{
			WORD ncode = HIWORD( wparam );	// notification code 
			WORD cid = LOWORD( wparam );	// item, control, or accelerator identifier 
			HWND chwnd = ( HWND ) lparam;	// handle of control 
			
			if( cid == ID_PLAY_BTTN )
			{
				xine_ui->Play( 0 );
				return 0L;
			}

			if( cid == ID_STOP_BTTN )
			{
				xine_ui->Stop();
				return 0L;
			}

			if( cid == ID_PAUSE_BTTN )
			{
				xine_ui->SetSpeed( XINE_SPEED_PAUSE );
				return 0L;
			}

			if( cid == ID_NEXT_BTTN )
			{
				xine_ui->Stop();
				xine_ui->Play( xine_ui->playindex + 1 );
				return 0L;
			}

			if( cid == ID_PREV_BTTN )
			{
				xine_ui->Stop();
				xine_ui->Play( xine_ui->playindex - 1 );
				return 0L;
			}

			if( cid == ID_RWND_BTTN )
			{
				int current_speed = xine_ui->GetSpeed();

				if( current_speed == XINE_SPEED_FAST_4 )
					xine_ui->SetSpeed( XINE_SPEED_FAST_2 );

				else if( current_speed == XINE_SPEED_FAST_2 )
					xine_ui->SetSpeed( XINE_SPEED_NORMAL );

				else if( current_speed == XINE_SPEED_NORMAL )
					xine_ui->SetSpeed( XINE_SPEED_SLOW_2 );

				else if( current_speed == XINE_SPEED_SLOW_2 )
					xine_ui->SetSpeed( XINE_SPEED_SLOW_4 );

				else if( current_speed == XINE_SPEED_SLOW_4 )
					xine_ui->SetSpeed( XINE_SPEED_PAUSE );

				return 0L;
			}

			if( cid == ID_FFWD_BTTN )
			{
				int current_speed = xine_ui->GetSpeed();

				if( current_speed == XINE_SPEED_PAUSE )
					xine_ui->SetSpeed( XINE_SPEED_SLOW_4 );

				else if( current_speed == XINE_SPEED_SLOW_4 )
					xine_ui->SetSpeed( XINE_SPEED_SLOW_2 );

				else if( current_speed == XINE_SPEED_SLOW_2 )
					xine_ui->SetSpeed( XINE_SPEED_NORMAL );

				else if( current_speed == XINE_SPEED_NORMAL )
					xine_ui->SetSpeed( XINE_SPEED_FAST_2 );

				else if( current_speed == XINE_SPEED_FAST_2 )
					xine_ui->SetSpeed( XINE_SPEED_FAST_4 );

				return 0L;
			}

			if( cid == ID_EJECT_BTTN )
			{
				xine_ui->init_playlistwnd();
				return 0L;
			}

		}

		case WM_HSCROLL:
		{
			int		code = ( int ) LOWORD( wparam );
			HWND	hctrl = ( HWND ) lparam;

			switch( code )
			{
				case TB_THUMBTRACK:
					xine_ui->tracking = true;
				break;

				case TB_LINEUP:
				case TB_LINEDOWN:
				case TB_PAGEUP:
				case TB_PAGEDOWN:
				case TB_TOP:
				case TB_BOTTOM:
				case TB_ENDTRACK:
				{
					int new_time = SendMessage( hctrl, TBM_GETPOS, (WPARAM) 0, (LPARAM) 0 );
					xine_ui->SetTime( new_time );
					xine_ui->tracking = false;

					return 0L;
				}
			}
		}
		break;

        case WM_DESTROY:
            // Cleanup and close the app
            PostQuitMessage( 0 );
            return 0L;
    }

    return DefWindowProc( hwnd, msg, wparam, lparam);
}

bool ToolbarAddButton( HWND htoolbar, int dataindex, int id )
{
	// define and add icon buttons
	TBBUTTON button;
	button.iBitmap = dataindex;
	button.idCommand = id;
	button.fsState = TBSTATE_ENABLED;
	button.fsStyle = TBSTYLE_BUTTON;
	button.iString = dataindex;

	if( !SendMessage( htoolbar, TB_ADDBUTTONS, (UINT) 1, (LPARAM) &button ) )
		return false;

	return true;
}

bool ToolbarAddDivider( HWND htoolbar )
{
	// define and add icon divider
	TBBUTTON button;
	button.iBitmap = 0;
	button.idCommand = 0;
	button.fsState = TBSTATE_ENABLED;
	button.fsStyle = TBSTYLE_SEP;
	button.iString = 0;

	if( !SendMessage( htoolbar, TB_ADDBUTTONS, (UINT) 1, (LPARAM) &button ) )
		return false;

	return true;
}

bool XINE_UI::init_ctrlwnd()
{
    WNDCLASSEX	wc;

    // register our window class

    wc.cbSize        = sizeof( wc );
    wc.lpszClassName = TEXT( "xinectrlwindow" );
    wc.lpfnWndProc   = proc_ctrlwnd;
    wc.style         = CS_VREDRAW | CS_HREDRAW;
    wc.hInstance     = hinst;
    wc.hIcon         = LoadIcon( hinst, MAKEINTRESOURCE( ico_xine_logo ) );
    wc.hIconSm       = LoadIcon( hinst, MAKEINTRESOURCE( ico_xine_logo ) );
    wc.hCursor       = LoadCursor( NULL, IDC_ARROW );
    wc.hbrBackground = ( HBRUSH ) ( 1 + COLOR_BTNFACE );
    wc.lpszMenuName  = 0;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;

    if( !RegisterClassEx( &wc ) )
	{
		error( "Error RegisterClassEx : for xinectrlwindow" );
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

    // create the ctrl window

    hctrlwnd = CreateWindowEx(	0,
								TEXT( "xinectrlwindow" ),
								TEXT( "xine" ),
								WS_SYSMENU,
								CW_USEDEFAULT, CW_USEDEFAULT,
  								dwWindowWidth, dwWindowHeight,
								NULL,
								NULL,
								hinst,
								NULL );
    if( !hctrlwnd )
	{
		error( "Error CreateWindowEx : for xinectrlwindow" );
    	return 0;
	}

	// create our panel window ( handles its own error reporting )

	init_panelwnd();
	if( !hpanelwnd )
    	return false;

	SetWindowPos( hpanelwnd, HWND_TOP, 5, 5, WINDOW_WIDTH - 5, 50, SWP_SHOWWINDOW );

	// create our time slider

	HWND htimebar = CreateWindowEx(	WS_EX_TOOLWINDOW,
									TRACKBAR_CLASS,
									"Trackbar Control",
									WS_CHILD | WS_VISIBLE | TBS_ENABLESELRANGE | TBS_NOTICKS,
									0, 0,
									0, 0,
									hctrlwnd,
									(HMENU) ID_TIMEBAR,
									hinst,
									0 ); 
 
	if( !htimebar )
	{
		error( "Error CreateWindowEx : for TRACKBAR_CLASS ( time )" );
    	return false;
	}

	SendMessage( htimebar, TBM_SETRANGE, (WPARAM) TRUE, (LPARAM) MAKELONG( 0, 1000 ) );
	SendMessage( htimebar, TBM_SETPAGESIZE, 0, (LPARAM) 1 );
    SendMessage( htimebar, TBM_SETSEL, (WPARAM) FALSE, (LPARAM) MAKELONG( 0, 0 ) );
    SendMessage( htimebar, TBM_SETPOS, (WPARAM) TRUE, (LPARAM) 0 );

	SetWindowPos( htimebar, HWND_TOP, 5, 60, WINDOW_WIDTH - 10, 17, SWP_SHOWWINDOW );

	// create our button toolbar

	HWND htoolbar = CreateWindowEx( WS_EX_TOOLWINDOW,
									TOOLBARCLASSNAME,
									0,
									WS_CHILDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
									TBSTYLE_TRANSPARENT | TBSTYLE_FLAT | CCS_NODIVIDER |
									CCS_NOPARENTALIGN,
									0, 0,
									0, 0,
									hctrlwnd,
									(HMENU) ID_TOOLBAR,
									hinst,
									0 );

	if( !htoolbar )
	{
		error( "Error CreateWindowEx : for TOOLBARCLASSNAME" );
    	return false;
	}

	SendMessage( htoolbar, TB_BUTTONSTRUCTSIZE, sizeof( TBBUTTON ), 0 );

	// create the toolbar image list

	COLORREF TransColor = RGB( 255, 0, 255 );

	himagelist = ImageList_Create( 11, 11, ILC_COLOR8 | ILC_MASK, 0, 7 );

	HBITMAP h_bmp_play_button		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_play_button ) );
	HBITMAP h_bmp_pause_button		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_pause_button ) );
	HBITMAP h_bmp_stop_button		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_stop_button ) );
	HBITMAP h_bmp_prev_button		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_prev_button ) );
	HBITMAP h_bmp_rwind_button		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_rwind_button ) );
	HBITMAP h_bmp_fforward_button	= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_fforward_button ) );
	HBITMAP h_bmp_next_button		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_next_button ) );
	HBITMAP h_bmp_eject_button		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_eject_button ) );

	ImageList_AddMasked( himagelist, h_bmp_play_button, TransColor );
	ImageList_AddMasked( himagelist, h_bmp_pause_button, TransColor );
	ImageList_AddMasked( himagelist, h_bmp_stop_button, TransColor );
	ImageList_AddMasked( himagelist, h_bmp_prev_button, TransColor );
	ImageList_AddMasked( himagelist, h_bmp_rwind_button, TransColor );
	ImageList_AddMasked( himagelist, h_bmp_fforward_button, TransColor );
	ImageList_AddMasked( himagelist, h_bmp_next_button, TransColor );
	ImageList_AddMasked( himagelist, h_bmp_eject_button, TransColor );

	DeleteObject( h_bmp_play_button );
	DeleteObject( h_bmp_pause_button );
	DeleteObject( h_bmp_stop_button );
	DeleteObject( h_bmp_prev_button );
	DeleteObject( h_bmp_rwind_button );
	DeleteObject( h_bmp_fforward_button );
	DeleteObject( h_bmp_next_button );
	DeleteObject( h_bmp_eject_button );

	SendMessage( htoolbar, TB_SETIMAGELIST, 0, (LPARAM) himagelist );
	SendMessage( htoolbar, TB_SETBITMAPSIZE, 0, (LPARAM) MAKELONG( 11, 11 ) );
	SendMessage( htoolbar, TB_SETBUTTONSIZE, 0, (LPARAM) MAKELONG( 22, 18 ) );

	// add our buttons to our toolbar

	ToolbarAddButton( htoolbar, 0, ID_PLAY_BTTN );
	ToolbarAddButton( htoolbar, 1, ID_PAUSE_BTTN );
	ToolbarAddButton( htoolbar, 2, ID_STOP_BTTN );
	ToolbarAddDivider( htoolbar );
	ToolbarAddButton( htoolbar, 3, ID_PREV_BTTN );
	ToolbarAddButton( htoolbar, 4, ID_RWND_BTTN );
	ToolbarAddButton( htoolbar, 5, ID_FFWD_BTTN );
	ToolbarAddButton( htoolbar, 6, ID_NEXT_BTTN );
	ToolbarAddDivider( htoolbar );
	ToolbarAddButton( htoolbar, 7, ID_EJECT_BTTN );

	SetWindowPos( htoolbar, HWND_TOP, 10, 80, 100, 100, SWP_SHOWWINDOW );

    // show the ctrl window

    ShowWindow( hctrlwnd, SW_SHOW );
    UpdateWindow( hctrlwnd );

	SetWindowLong( hctrlwnd, GWL_USERDATA, ( long ) this );

	return true;
}

void XINE_UI::end_ctrlwnd()
{
	end_panelwnd();

	ImageList_Destroy( himagelist );

	HWND htoolbar = GetDlgItem( hctrlwnd, ID_TOOLBAR );
	DestroyWindow( htoolbar );

	HWND htimebar = GetDlgItem( hctrlwnd, ID_TIMEBAR );
	DestroyWindow( htimebar );

	DestroyWindow( hctrlwnd );
	UnregisterClass( "xinectrlwindow", hinst );
}

bool _XINE_UI::UpdateCtrl()
{
	int length_time;

	if( gGui->stream )
	{
		if( mode == XINE_STATUS_PLAY )
		{
		  /*mrl_time_current = xine_get_current_time( gGui->stream );*/
		  length_time = 0;
		  if (xine_get_pos_length(gGui->stream, 0, &mrl_time_current, &length_time))
          {
            if (length_time && ((length_time/1000) != mrl_time_length))
			{
				mrl_time_length = length_time/1000;
		        HWND htimebar = GetDlgItem( hctrlwnd, ID_TIMEBAR );
		        SendMessage( htimebar, TBM_SETRANGE, (WPARAM) TRUE, (LPARAM) MAKELONG( 0, mrl_time_length ) );
			}

			mrl_time_current /= 1000;
		    if( !tracking )
			{
			  HWND htimebar = GetDlgItem( hctrlwnd, ID_TIMEBAR );
			  SendMessage( htimebar, TBM_SETPOS, (WPARAM) TRUE, (LPARAM) mrl_time_current );
			}
		  }
		}
	}

	return true;
}

DWORD __stdcall update_loop_helper( void * param )
{
	XINE_UI * xine_ui = ( XINE_UI * ) param;

	while( xine_ui->mode == XINE_STATUS_PLAY )
	{
		xine_ui->UpdateCtrl();
		xine_ui->UpdatePanel();

		Sleep( 500 );
	}

	return 0;
}

DWORD XINE_UI::UpdateLoop()
{
	// start ctrl update loop

	DWORD panel_loop_id;
	CreateThread( 0, 0, &update_loop_helper, ( void * ) this, 0, &panel_loop_id );

	return 0;
}