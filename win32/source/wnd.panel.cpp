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

#define VOLBAR_WIDTH		13
#define VOLBAR_HEIGHT		46

#define	VOLBUTTON_WIDTH		22
#define	VOLBUTTON_HEIGHT	40

#define	ARROW_WIDTH			7
#define	ARROW_HEIGHT		4

#define PANEL_SPLIT			110

static	HFONT smallfont;
static	HFONT largefont;

static	HBITMAP	configure_normal_bmp;
static	HBITMAP	configure_selected_bmp;
static	HBITMAP	fullscreenbutton_off_normal_bmp;
static	HBITMAP	fullscreenbutton_off_selected_bmp;
static	HBITMAP	fullscreenbutton_on_normal_bmp;
static	HBITMAP	fullscreenbutton_on_selected_bmp;
static	HBITMAP	volbutton_on_bmp;
static	HBITMAP	volbutton_off_bmp;
static	HBITMAP	arrowbutton_up_normal_bmp;
static	HBITMAP	arrowbutton_up_selected_bmp;
static	HBITMAP	arrowbutton_down_normal_bmp;
static	HBITMAP	arrowbutton_down_selected_bmp;


static void ResizeChildren( HWND hpanelwnd )
{
	RECT rect;
	GetClientRect( hpanelwnd, &rect );

	HWND htitlewnd = GetDlgItem( hpanelwnd, ID_TITLE );
	if( htitlewnd )
	{
		SetWindowPos( htitlewnd, HWND_TOP,
					  5, 5,
					  PANEL_SPLIT, 14,
					  SWP_SHOWWINDOW );
	}

	HWND htimewnd = GetDlgItem( hpanelwnd, ID_TIME );
	if( htimewnd )
	{
		SetWindowPos( htimewnd, HWND_TOP,
					  5, 25,
					  PANEL_SPLIT, 16,
					  SWP_SHOWWINDOW );
	}

	HWND hfullscreenwnd = GetDlgItem( hpanelwnd, ID_FULLSCREEN );
	if( hfullscreenwnd )
	{
		SetWindowPos( hfullscreenwnd, HWND_TOP,
					  rect.right - 90, 5,
					  16, 12,
					  SWP_SHOWWINDOW );
	}

	HWND hconfigurewnd = GetDlgItem( hpanelwnd, ID_CONFIG );
	if( hconfigurewnd )
	{
		SetWindowPos( hconfigurewnd, HWND_TOP,
					  rect.right - 72, 5,
					  32, 12,
					  SWP_SHOWWINDOW );
	}

	HWND hspulabelwnd = GetDlgItem( hpanelwnd, ID_SPULABEL );
	if( hspulabelwnd )
	{
		SetWindowPos( hspulabelwnd, HWND_TOP,
					  rect.right - 103, 18,
					  28, 12,
					  SWP_SHOWWINDOW );
	}

	HWND haudiolabelwnd = GetDlgItem( hpanelwnd, ID_AUDIOLABEL );
	if( haudiolabelwnd )
	{
		SetWindowPos( haudiolabelwnd, HWND_TOP,
					  rect.right - 103, 31,
					  28, 12,
					  SWP_SHOWWINDOW );
	}

	HWND hspuvaluewnd = GetDlgItem( hpanelwnd, ID_SPUVALUE );
	if( hspuvaluewnd )
	{
		SetWindowPos( hspuvaluewnd, HWND_TOP,
					  rect.right - 61, 18,
					  23, 12,
					  SWP_SHOWWINDOW );
	}

	HWND haudiovaluewnd = GetDlgItem( hpanelwnd, ID_AUDIOVALUE );
	if( haudiovaluewnd )
	{
		SetWindowPos( haudiovaluewnd, HWND_TOP,
					  rect.right - 61, 31,
					  23, 12,
					  SWP_SHOWWINDOW );
	}

	HWND hspuinc = GetDlgItem( hpanelwnd, ID_SPUINC );
	if( hspuinc )
	{
		SetWindowPos( hspuinc, HWND_TOP,
					  rect.right - 71, rect.top + 20,
					  ARROW_WIDTH, ARROW_HEIGHT,
					  SWP_SHOWWINDOW );
	}

	HWND hspudec = GetDlgItem( hpanelwnd, ID_SPUDEC );
	if( hspudec )
	{
		SetWindowPos( hspudec, HWND_TOP,
					  rect.right - 71, rect.top + 26,
					  ARROW_WIDTH, ARROW_HEIGHT,
					  SWP_SHOWWINDOW );
	}

	HWND haudioinc = GetDlgItem( hpanelwnd, ID_AUDIOINC );
	if( haudioinc )
	{
		SetWindowPos( haudioinc, HWND_TOP,
					  rect.right - 71, rect.top + 33,
					  ARROW_WIDTH, ARROW_HEIGHT,
					  SWP_SHOWWINDOW );
	}

	HWND haudiodec = GetDlgItem( hpanelwnd, ID_AUDIODEC );
	if( haudiodec )
	{
		SetWindowPos( haudiodec, HWND_TOP,
					  rect.right - 71, rect.top + 39,
					  ARROW_WIDTH, ARROW_HEIGHT,
					  SWP_SHOWWINDOW );
	}

	HWND hvolbutton = GetDlgItem( hpanelwnd, ID_VOLBUTTON );
	if( hvolbutton )
	{
		SetWindowPos( hvolbutton, HWND_TOP,
					  rect.right - ( VOLBAR_WIDTH + VOLBUTTON_WIDTH ) - 2, rect.top + 4,
					  VOLBUTTON_WIDTH, VOLBUTTON_HEIGHT,
					  SWP_SHOWWINDOW );
	}


	HWND hvolbar = GetDlgItem( hpanelwnd, ID_VOLBAR );
	if( hvolbar )
	{
		SetWindowPos( hvolbar, HWND_TOP,
					  rect.right - VOLBAR_WIDTH, rect.top + 1,
					  VOLBAR_WIDTH, VOLBAR_HEIGHT,
					  SWP_SHOWWINDOW );
	}
}

LRESULT CALLBACK proc_panelwnd( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
	XINE_UI * xine_ui = ( XINE_UI * ) GetWindowLong( hwnd, GWL_USERDATA );
 
    switch( msg )
    {
		case WM_COMMAND:
		{
			WORD ncode = HIWORD( wparam );	// notification code 
			WORD cid = LOWORD( wparam );	// item, control, or accelerator identifier 
			HWND chwnd = ( HWND ) lparam;	// handle of control 
			
			if( cid == ID_FULLSCREEN )
			{
				if( ncode == BN_CLICKED )
				{
					if( GetWindowLong( chwnd, GWL_USERDATA ) )
					{
						SetWindowLong( chwnd, GWL_USERDATA, 0 );
						xine_ui->DriverMessage( GUI_WIN32_MOVED_OR_RESIZED, 0 );
						xine_ui->win32_visual.FullScreen = false;

						int style = GetWindowLong( xine_ui->hvideownd, GWL_STYLE );
						SetWindowLong( xine_ui->hvideownd, GWL_STYLE, style | WS_CAPTION | WS_SIZEBOX | WS_SYSMENU | WS_MAXIMIZEBOX );
						ShowWindow( xine_ui->hvideownd, SW_SHOWNORMAL );

					}
					else
					{
						SetWindowLong( chwnd, GWL_USERDATA, 1 );
						xine_ui->DriverMessage( GUI_WIN32_MOVED_OR_RESIZED, 0 );
						xine_ui->win32_visual.FullScreen = true;

						int style = GetWindowLong( xine_ui->hvideownd, GWL_STYLE );
						SetWindowLong( xine_ui->hvideownd, GWL_STYLE, style & ~( WS_CAPTION | WS_BORDER | WS_SIZEBOX | WS_SYSMENU | WS_MAXIMIZEBOX ) );
						ShowWindow( xine_ui->hvideownd, SW_MAXIMIZE );
					}

					// FIXME : There must be a better way to
					//         force a WM_DRAITEM message

				    ShowWindow( chwnd, SW_HIDE );
				    ShowWindow( chwnd, SW_SHOW );

					return 0L;
				}
 			}

			if( cid == ID_SPUINC )
			{
				if( ncode == BN_CLICKED )
				{
					xine_ui->SelectSpuChannel( xine_get_param(gGui->stream, XINE_PARAM_SPU_CHANNEL) + 1 );
					xine_ui->spu_channel = xine_get_param(gGui->stream, XINE_PARAM_SPU_CHANNEL);
					return 0L;
				}
 			}

			if( cid == ID_SPUDEC )
			{
				if( ncode == BN_CLICKED )
				{
					xine_ui->SelectSpuChannel( xine_get_param(gGui->stream, XINE_PARAM_SPU_CHANNEL) - 1 );
					xine_ui->spu_channel = xine_get_param(gGui->stream, XINE_PARAM_SPU_CHANNEL);
					return 0L;
				}
 			}

			if( cid == ID_AUDIOINC )
			{
				if( ncode == BN_CLICKED )
				{
					xine_ui->SelectAudioChannel( xine_get_param(gGui->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL) + 1 );
					xine_ui->audio_channel = xine_get_param(gGui->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL);
					return 0L;
				}
 			}

			if( cid == ID_AUDIODEC )
			{
				if( ncode == BN_CLICKED )
				{
					xine_ui->SelectAudioChannel( xine_get_param(gGui->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL) - 1 );
					xine_ui->audio_channel = xine_get_param(gGui->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL);
					return 0L;
				}
 			}

			if( cid == ID_VOLBUTTON )
			{
				if( ncode == BN_CLICKED )
				{
					HWND hvolbar = GetDlgItem( hwnd, ID_VOLBAR );

					if( GetWindowLong( chwnd, GWL_USERDATA ) )
					{
						SetWindowLong( chwnd, GWL_USERDATA, 0 );
						EnableWindow( hvolbar, false );
						xine_ui->SetMute( true );
					}
					else
					{
						SetWindowLong( chwnd, GWL_USERDATA, 1 );
						EnableWindow( hvolbar, true );
						xine_ui->SetMute( false );
					}

					// FIXME : There must be a better way to
					//         force a WM_DRAITEM message

				    ShowWindow( chwnd, SW_HIDE );
				    ShowWindow( chwnd, SW_SHOW );

					return 0L;
				}
 			}
		}
		break;

		case WM_VSCROLL:
		{
			int		code = ( int ) LOWORD( wparam );
			HWND	hcntrl = ( HWND ) lparam;

			switch( code )
			{
				case TB_THUMBTRACK:
				case TB_LINEUP:
				case TB_LINEDOWN:
				case TB_PAGEUP:
				case TB_PAGEDOWN:
				case TB_TOP:
				case TB_BOTTOM:
				case TB_ENDTRACK:
				{
					int new_volume = SendMessage( hcntrl, TBM_GETPOS, (WPARAM) 0, (LPARAM) 0 );
					xine_ui->SetVolume(	new_volume );
					return 0L;
				}
			}

		}
		break;

        case WM_SIZE:
		{
			ResizeChildren( hwnd );
		}
		break;

        case WM_DRAWITEM:
		{
			LPDRAWITEMSTRUCT lpdis = ( LPDRAWITEMSTRUCT ) lparam; 

			if( lpdis->CtlID == ID_FULLSCREEN )
			{
				HDC hdcMem = CreateCompatibleDC( lpdis->hDC );
				long bstate = GetWindowLong( lpdis->hwndItem, GWL_USERDATA );
 
				if( bstate )
				{
					if( lpdis->itemState & ODS_SELECTED )
		                SelectObject( hdcMem, fullscreenbutton_on_selected_bmp );
					else
		                SelectObject( hdcMem, fullscreenbutton_on_normal_bmp );
				}
				else
				{
					if( lpdis->itemState & ODS_SELECTED )
		                SelectObject( hdcMem, fullscreenbutton_off_selected_bmp );
					else
		                SelectObject( hdcMem, fullscreenbutton_off_normal_bmp );
				}
 
				BitBlt( lpdis->hDC, 0, 0, 16, 12, hdcMem, 0, 0, SRCCOPY );
 
				DeleteDC( hdcMem );
				return TRUE;
			}

			if( lpdis->CtlID == ID_CONFIG )
			{
				HDC hdcMem = CreateCompatibleDC( lpdis->hDC );
 
				if( lpdis->itemState & ODS_SELECTED )
	                SelectObject( hdcMem, configure_selected_bmp );
				else
	                SelectObject( hdcMem, configure_normal_bmp );
 
				BitBlt( lpdis->hDC, 0, 0, 32, 12, hdcMem, 0, 0, SRCCOPY );
 
				DeleteDC( hdcMem );
				return TRUE;
			}

			if( ( lpdis->CtlID == ID_SPUINC ) || ( lpdis->CtlID == ID_AUDIOINC ) )
			{
				HDC hdcMem = CreateCompatibleDC( lpdis->hDC );
 
				if( lpdis->itemState & ODS_SELECTED )
	                SelectObject( hdcMem, arrowbutton_up_selected_bmp ); 
				else 
	                SelectObject( hdcMem, arrowbutton_up_normal_bmp ); 
 
				BitBlt( lpdis->hDC, 0, 0, 7, 4,	hdcMem, 0, 0, SRCCOPY );
 
				DeleteDC( hdcMem );
				return TRUE;
			}

			if( ( lpdis->CtlID == ID_SPUDEC ) || ( lpdis->CtlID == ID_AUDIODEC ) )
			{
				HDC hdcMem = CreateCompatibleDC( lpdis->hDC );
 
				if( lpdis->itemState  & ODS_SELECTED )
	                SelectObject( hdcMem, arrowbutton_down_selected_bmp ); 
				else 
	                SelectObject( hdcMem, arrowbutton_down_normal_bmp ); 
 
				BitBlt( lpdis->hDC, 0, 0, 7, 4,	hdcMem, 0, 0, SRCCOPY );
 
				DeleteDC( hdcMem );
				return TRUE;
			}

			if( lpdis->CtlID == ID_VOLBUTTON )
			{
				HDC hdcMem = CreateCompatibleDC( lpdis->hDC );
				long bstate = GetWindowLong( lpdis->hwndItem, GWL_USERDATA );
 
				if( bstate )
	                SelectObject( hdcMem, volbutton_on_bmp ); 
				else 
	                SelectObject( hdcMem, volbutton_off_bmp ); 
 
				BitBlt( lpdis->hDC, 0, 0, VOLBUTTON_WIDTH, VOLBUTTON_HEIGHT,
						hdcMem, 0, 0, SRCCOPY );
 
				DeleteDC( hdcMem );
				return TRUE;
			}
		}
		break;

		case WM_CTLCOLORBTN:
		case WM_CTLCOLORSTATIC:
		{
			HDC hdcstatic = ( HDC ) wparam;
			SetTextColor( hdcstatic, RGB( 255, 255, 255 ) );
			SetBkColor( hdcstatic, RGB( 0, 0, 0 ) );

			HBRUSH bkgrd = ( HBRUSH ) GetClassLong( hwnd, GCL_HBRBACKGROUND );

			return ( long ) bkgrd;
		}
		break;

        case WM_DESTROY:
			if( xine_ui )
				xine_ui->end_panelwnd();
            return 0L;
    }

    return DefWindowProc( hwnd, msg, wparam, lparam);
}

bool _XINE_UI::init_panelwnd()
{
    WNDCLASSEX	wc;

    // register our window class

    wc.cbSize        = sizeof( wc );
    wc.lpszClassName = TEXT( "xinepanelwindow" );
    wc.lpfnWndProc   = proc_panelwnd;
    wc.style         = CS_VREDRAW | CS_HREDRAW;
    wc.hInstance     = hinst;
    wc.hIcon         = 0,
    wc.hIconSm       = 0,
    wc.hCursor       = LoadCursor( NULL, IDC_ARROW );
    wc.hbrBackground = ( HBRUSH ) GetStockObject( BLACK_BRUSH );
    wc.lpszMenuName  = 0;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;

    if( !RegisterClassEx( &wc ) )
	{
		error( "Error RegisterClassEx : for xinepanelwindow" );
    	return false;
	}

    // create the ctrl window

    hpanelwnd = CreateWindowEx(	WS_EX_STATICEDGE,
								TEXT( "xinepanelwindow" ),
								0,
								WS_CHILD,
								0, 0,
  								0, 0,
								hctrlwnd,
								( HMENU ) ID_PANEL,
								hinst,
								NULL );
    if( !hpanelwnd )
	{
		error( "Error CreateWindowEx : for xinepanelwindow" );
    	return false;
	}

	// create our fonts

	smallfont = CreateFont(	13,			// logical height of font
							5,			// logical average character width
							0,			// angle of escapement
							0,			// base-line orientation angle
							0,			// font weight
							0,			// italic attribute flag
							0,			// underline attribute flag
							0,			// strikeout attribute flag
							0,			// character set identifier
							0,			// output precision
							0,				// clipping precision
							ANTIALIASED_QUALITY,	// output quality
							FF_MODERN | VARIABLE_PITCH ,		// pitch and family
							"Areal" );		// pointer to typeface name string

	largefont = CreateFont(	20,			// logical height of font
							7,			// logical average character width
							0,			// angle of escapement
							0,			// base-line orientation angle
							0,			// font weight
							0,			// italic attribute flag
							0,			// underline attribute flag
							0,			// strikeout attribute flag
							0,			// character set identifier
							0,			// output precision
							0,				// clipping precision
							ANTIALIASED_QUALITY,	// output quality
							FF_MODERN | VARIABLE_PITCH ,		// pitch and family
							"Areal" );		// pointer to typeface name string

	// create our title window

	HWND htitle = CreateWindow(	"STATIC",
								0,
								WS_CHILD | WS_VISIBLE | SS_LEFT,
								0, 0,
								0, 0,
								hpanelwnd,
								(HMENU) ID_TITLE,
								hinst,
								0 ); 
	
	if( !htitle )
	{
		error( "Error CreateWindowEx : for STATIC ( htitle )" );
    	return false;
	}

	SendMessage( htitle, WM_SETFONT, ( WPARAM ) smallfont, false );

	// create our time window

	HWND htime = CreateWindow(	"STATIC",
								0,
								WS_CHILD | WS_VISIBLE | SS_LEFT,
								0, 0,
								0, 0,
								hpanelwnd,
								(HMENU) ID_TIME,
								hinst,
								0 ); 
	
	if( !htime )
	{
		error( "Error CreateWindowEx : for STATIC ( time )" );
    	return false;
	}

	SendMessage( htime, WM_SETFONT, ( WPARAM ) largefont, false );

	// create our fullscreen button

	fullscreenbutton_off_normal_bmp		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_fullscreen_off_normal ) ); 
	fullscreenbutton_off_selected_bmp	= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_fullscreen_off_selected ) );
	fullscreenbutton_on_normal_bmp		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_fullscreen_on_normal ) ); 
	fullscreenbutton_on_selected_bmp	= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_fullscreen_on_selected ) );

	if( !fullscreenbutton_off_normal_bmp || !fullscreenbutton_off_selected_bmp ||
		!fullscreenbutton_on_normal_bmp || !fullscreenbutton_on_selected_bmp )
	{
		error( "Error LoadBitmap : for fullscreenbutton (s)" );
    	return false;
	}

	HWND hfullscrrenbutton = CreateWindow(	"BUTTON",
											0,
											WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
											0, 0,
											0, 0,
											hpanelwnd,
											(HMENU) ID_FULLSCREEN,
											hinst,
											0 ); 
	
	if( !hfullscrrenbutton )
	{
		error( "Error CreateWindowEx : for BUTTON ( hfullscrrenbutton )" );
    	return false;
	}

	SetWindowLong( hfullscrrenbutton, GWL_USERDATA, 0 );

	// create our configure button

	configure_normal_bmp	= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_configure_normal ) ); 
	configure_selected_bmp	= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_configure_selected ) );

	if( !configure_normal_bmp || !configure_selected_bmp )
	{
		error( "Error LoadBitmap : for configure button(s)" );
    	return false;
	}

	HWND hconfigbutton = CreateWindow(	"BUTTON",
											0,
											WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
											0, 0,
											0, 0,
											hpanelwnd,
											(HMENU) ID_CONFIG,
											hinst,
											0 ); 
	
	if( !hconfigbutton )
	{
		error( "Error CreateWindowEx : for BUTTON ( hconfigbutton )" );
    	return false;
	}

	SetWindowLong( hfullscrrenbutton, GWL_USERDATA, 0 );

	// create our spu and audio label windows

	HWND hspulabelwnd = CreateWindow(	"STATIC",
										"spu",
										WS_CHILD | WS_VISIBLE | SS_RIGHT,
										0, 0,
										0, 0,
										hpanelwnd,
										(HMENU) ID_SPULABEL,
										hinst,
										0 ); 
	
	if( !hspulabelwnd )
	{
		error( "Error CreateWindowEx : for STATIC ( hspulabelwnd )" );
    	return false;
	}

	SendMessage( hspulabelwnd, WM_SETFONT, ( WPARAM ) smallfont, false );

	HWND haudiolabelwnd = CreateWindow(	"STATIC",
										"aud",
										WS_CHILD | WS_VISIBLE | SS_RIGHT,
										0, 0,
										0, 0,
										hpanelwnd,
										(HMENU) ID_AUDIOLABEL,
										hinst,
										0 ); 
	
	if( !haudiolabelwnd )
	{
		error( "Error CreateWindowEx : for STATIC ( haudiolabelwnd )" );
    	return false;
	}

	SendMessage( haudiolabelwnd, WM_SETFONT, ( WPARAM ) smallfont, false );

	// create our spu and audio inc & dec buttons

	arrowbutton_up_normal_bmp		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_arrow_up_normal ) ); 
	arrowbutton_up_selected_bmp		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_arrow_up_selected ) ); 
	arrowbutton_down_normal_bmp		= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_arrow_down_normal ) );
	arrowbutton_down_selected_bmp	= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_arrow_down_selected ) );

	if( !arrowbutton_up_normal_bmp || !arrowbutton_up_selected_bmp ||
		!arrowbutton_down_normal_bmp || !arrowbutton_down_selected_bmp )
	{
		error( "Error LoadBitmap : for bmp_volume_button (s)" );
    	return false;
	}

	HWND hspuinc = CreateWindow(	"BUTTON",
									0,
									WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
									0, 0,
									0, 0,
									hpanelwnd,
									(HMENU) ID_SPUINC,
									hinst,
									0 ); 
	
	if( !hspuinc )
	{
		error( "Error CreateWindowEx : for BUTTON ( hspuinc )" );
    	return false;
	}

	HWND hspudec = CreateWindow(	"BUTTON",
									0,
									WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
									0, 0,
									0, 0,
									hpanelwnd,
									(HMENU) ID_SPUDEC,
									hinst,
									0 ); 
	
	if( !hspudec )
	{
		error( "Error CreateWindowEx : for BUTTON ( hspudec )" );
    	return false;
	}

	HWND haudioinc = CreateWindow(	"BUTTON",
									0,
									WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
									0, 0,
									0, 0,
									hpanelwnd,
									(HMENU) ID_AUDIOINC,
									hinst,
									0 ); 
	
	if( !haudioinc )
	{
		error( "Error CreateWindowEx : for BUTTON ( haudioinc )" );
    	return false;
	}

	HWND haudiodec = CreateWindow(	"BUTTON",
									0,
									WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
									0, 0,
									0, 0,
									hpanelwnd,
									(HMENU) ID_AUDIODEC,
									hinst,
									0 ); 
	
	if( !haudiodec )
	{
		error( "Error CreateWindowEx : for BUTTON ( haudiodec )" );
    	return false;
	}

	// create our spu and audio value windows

	HWND hspuvaluewnd = CreateWindow(	"STATIC",
										"None",
										WS_CHILD | WS_VISIBLE | SS_LEFT,
										0, 0,
										0, 0,
										hpanelwnd,
										(HMENU) ID_SPUVALUE,
										hinst,
										0 ); 
	
	if( !hspuvaluewnd )
	{
		error( "Error CreateWindowEx : for STATIC ( hspuvaluewnd )" );
    	return false;
	}

	SendMessage( hspuvaluewnd, WM_SETFONT, ( WPARAM ) smallfont, false );

	HWND haudiovaluewnd = CreateWindow(	"STATIC",
										"None",
										WS_CHILD | WS_VISIBLE | SS_LEFT,
										0, 0,
										0, 0,
										hpanelwnd,
										(HMENU) ID_AUDIOVALUE,
										hinst,
										0 ); 
	
	if( !haudiovaluewnd )
	{
		error( "Error CreateWindowEx : for STATIC ( haudiovaluewnd )" );
    	return false;
	}

	SendMessage( haudiovaluewnd, WM_SETFONT, ( WPARAM ) smallfont, false );

	// create our volume button

	volbutton_on_bmp	= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_volume_on_button ) ); 
	volbutton_off_bmp	= LoadBitmap( hinst, MAKEINTRESOURCE( bmp_volume_off_button ) );

	if( !volbutton_on_bmp || !volbutton_off_bmp )
	{
		error( "Error LoadBitmap : for bmp_volume_button (s)" );
    	return false;
	}

	HWND hvolbutton = CreateWindow(	"BUTTON",
									0,
									WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
									0, 0,
									0, 0,
									hpanelwnd,
									(HMENU) ID_VOLBUTTON,
									hinst,
									0 ); 
	
	if( !hvolbutton )
	{
		error( "Error CreateWindowEx : for BUTTON ( volume )" );
    	return false;
	}

	SetWindowLong( hvolbutton, GWL_USERDATA, 1 );

	// create our volume slider

	HWND hvolbar = CreateWindowEx(	WS_EX_TOOLWINDOW,
									TRACKBAR_CLASS,
									"Volume Control",
									WS_CHILD | WS_VISIBLE | TBS_NOTICKS | TBS_VERT,
									0, 0,
									0, 0,
									hpanelwnd,
									(HMENU) ID_VOLBAR,
									hinst,
									0 ); 
 
	if( !hvolbar )
	{
		error( "Error CreateWindowEx : for TRACKBAR_CLASS ( volume )" );
    	return false;
	}


	SendMessage( hvolbar, TBM_SETRANGE, (WPARAM) TRUE, (LPARAM) MAKELONG( 0, 100 ) );
	SendMessage( hvolbar, TBM_SETPAGESIZE, 0, (LPARAM) 1 );
    SendMessage( hvolbar, TBM_SETPOS, (WPARAM) TRUE, (LPARAM) 0 );

    ShowWindow( hpanelwnd, SW_SHOW );
    UpdateWindow( hpanelwnd );

	UpdatePanel();

	SetWindowLong( hpanelwnd, GWL_USERDATA, ( long ) this );

	return true;
}

bool XINE_UI::UpdatePanel()
{
    char  buffer[10];
    char *lang = NULL;

    UpdateWindow( hpanelwnd );

	// set our title

	if( mrl_short_name )
		SetDlgItemText( hpanelwnd, ID_TITLE, mrl_short_name );
	else
		SetDlgItemText( hpanelwnd, ID_TITLE, "<no input>" );
	
	// set our time

	char tmpbuff[ 50 ];
	sprintf( tmpbuff, "%u:%u:%u / %u:%u:%u",
			 mrl_time_current / ( 60 * 60 ), mrl_time_current / 60, mrl_time_current % 60,
			 mrl_time_length / ( 60 * 60 ), mrl_time_length / 60, mrl_time_length % 60 );

	SetDlgItemText( hpanelwnd, ID_TIME, tmpbuff );

	// set our spu channel
	if (gGui != NULL) {
		memset(&buffer, 0, sizeof(buffer));
		switch (spu_channel) {
		case -2:
		  lang = "off";
		  break;

		case -1:
  		  if(!xine_get_spu_lang (gGui->stream, spu_channel, &buffer[0]))
			lang = "auto";
		  else
			lang = buffer;
		  break;

		default:
		  if(!xine_get_spu_lang (gGui->stream, spu_channel, &buffer[0]))
			 sprintf(buffer, "%3d", spu_channel);
		  lang = buffer;
		  break;
		}

		sprintf( tmpbuff, "%s", lang );
	}
	else {
		sprintf( tmpbuff, "%i", spu_channel );
	}

	SetDlgItemText( hpanelwnd, ID_SPUVALUE, tmpbuff );

	// set our audio channel
	if (gGui != NULL) {
		memset(&buffer, 0, sizeof(buffer));
		switch (audio_channel) {
		case -2:
		  lang = "off";
		  break;

		case -1:
  		  if(!xine_get_audio_lang (gGui->stream, audio_channel, &buffer[0]))
			lang = "auto";
		  else
			lang = buffer;
		  break;

		default:
		  if(!xine_get_audio_lang (gGui->stream, audio_channel, &buffer[0]))
			 sprintf(buffer, "%3d", audio_channel);
		  lang = buffer;
		  break;
		}

		sprintf( tmpbuff, "%s", lang );
	}
	else {
		sprintf( tmpbuff, "%i", audio_channel );
	}

	SetDlgItemText( hpanelwnd, ID_AUDIOVALUE, tmpbuff );

 	return true;
}

void XINE_UI::end_panelwnd()
{
	DeleteObject( win32_visual.Brush );
	DestroyWindow( hvideownd );
	UnregisterClass( "xinevideowindow", hinst );

	HWND hvolbar = GetDlgItem( hpanelwnd, ID_VOLBAR );
	DestroyWindow( hvolbar );

	DeleteObject( smallfont );
	DeleteObject( largefont );

	DeleteObject( configure_normal_bmp );
	DeleteObject( configure_selected_bmp );
	DeleteObject( fullscreenbutton_off_normal_bmp );
	DeleteObject( fullscreenbutton_off_selected_bmp );
	DeleteObject( fullscreenbutton_on_normal_bmp );
	DeleteObject( fullscreenbutton_on_selected_bmp );
	DeleteObject( volbutton_on_bmp );
	DeleteObject( volbutton_off_bmp );
	DeleteObject( arrowbutton_up_normal_bmp );
	DeleteObject( arrowbutton_up_selected_bmp );
	DeleteObject( arrowbutton_down_normal_bmp );
	DeleteObject( arrowbutton_down_selected_bmp );

	HWND hvolbutton = GetDlgItem( hpanelwnd, ID_VOLBUTTON );
	DestroyWindow( hvolbutton );
	
	DestroyWindow( hpanelwnd );
	UnregisterClass( "xinepanelwindow", hinst );
}

