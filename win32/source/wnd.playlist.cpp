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

#define WINDOW_WIDTH	200
#define WINDOW_HEIGHT	200

HFONT hfont;

bool AddPlaylistColumn( HWND hlistwnd, int width, int index )
{
	LV_COLUMN lvCol;
	lvCol.mask = LVCF_FMT | LVCF_SUBITEM | LVCF_WIDTH;
	lvCol.fmt = LVCFMT_LEFT;
	lvCol.cx = width;
	lvCol.iSubItem = index;

	int columnindex = SendMessage( hlistwnd, LVM_INSERTCOLUMN, ( WPARAM ) index, ( LPARAM ) &lvCol );
	if( columnindex == -1 )
		return false;

	return true;
}

bool AddPlaylistItem( HWND hlistwnd, void * lparam )
{
	int itemcount = ListView_GetItemCount( hlistwnd );
	
	LV_ITEM newItem;
	newItem.mask = LVIF_PARAM | LVIF_TEXT;
	newItem.iItem = itemcount;
	newItem.iSubItem = 0;
	newItem.pszText = LPSTR_TEXTCALLBACK;
	newItem.lParam = ( long ) lparam;

	if( SendMessage( hlistwnd, LVM_INSERTITEM, 0, ( LPARAM ) &newItem ) == -1 )
		return false;

	return true;
}

void ResizeChildren( HWND hplaylistwnd )
{
	RECT rect;
	GetClientRect( hplaylistwnd, &rect );

	HWND hstauswnd = GetDlgItem( hplaylistwnd, ID_STATUS );
	SetWindowPos( hstauswnd, HWND_TOP,
				  rect.left + 5, rect.bottom - 25,
				  rect.right - rect.left - 10, 20,						  
				  SWP_SHOWWINDOW );

	HWND hlistwnd = GetDlgItem( hplaylistwnd, ID_LIST );
	SetWindowPos( hlistwnd, HWND_TOP,
				  rect.left + 5, rect.top + 5,
				  rect.right - rect.left - 55, rect.bottom - rect.top - 30,
				  SWP_SHOWWINDOW );

	HWND haddwnd = GetDlgItem( hplaylistwnd, ID_ADD );
	SetWindowPos( haddwnd, HWND_TOP,
				  rect.right - 40, rect.top + 5,
				  35, 20,
				  SWP_SHOWWINDOW );

	HWND hdelwnd = GetDlgItem( hplaylistwnd, ID_DEL );
	SetWindowPos( hdelwnd, HWND_TOP,
				  rect.right - 40, rect.top + 30,
				  35, 20,
				  SWP_SHOWWINDOW );

	GetClientRect( hlistwnd, &rect );
	SendMessage( hlistwnd, LVM_SETCOLUMNWIDTH, 1, rect.right - rect.left - 22 );
}

LRESULT CALLBACK proc_playlistwnd( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
	XINE_UI * xine_ui = ( XINE_UI * ) GetWindowLong( hwnd, GWL_USERDATA );
 
	switch( msg )
	{
		case WM_NOTIFY:
		{
			int		controlid = ( int ) wparam; 
			NMHDR *			lpnm = ( NMHDR * ) lparam; 
			NMLVDISPINFO *	nmlvdi = ( NMLVDISPINFO * ) lparam;
			
			if( lpnm->code == LVN_GETDISPINFO )
			{
				PLAYITEM * playitem = ( PLAYITEM * ) ( nmlvdi->item.lParam );

				// first column

				if( nmlvdi->item.iSubItem == 0 )
					nmlvdi->item.iImage = playitem->mrl_type;

				// second column

				if( nmlvdi->item.iSubItem == 1 )
					nmlvdi->item.pszText = playitem->mrl_short_name;
			}

			return 0L;
		}
		break;


		case WM_COMMAND:
		{
			WORD ncode = HIWORD( wparam );	// notification code 
			WORD cid = LOWORD( wparam );	// item, control, or accelerator identifier 
			HWND chwnd = ( HWND ) lparam;	// handle of control 
			
			if( cid == ID_ADD )
			{
				OPENFILENAME	ofn;				// common dialog box structure
				char			tmpbuff[ 2048 ];	// buffer for filename
				memset( &tmpbuff, 0, sizeof( tmpbuff ) );

				memset( &ofn, 0, sizeof( OPENFILENAME ) );
				ofn.lStructSize = sizeof( OPENFILENAME );
				ofn.hwndOwner = hwnd;
				ofn.lpstrFile = tmpbuff;
				ofn.nMaxFile = sizeof( tmpbuff );
				ofn.lpstrFilter = "All\0*.*\0";
				ofn.nFilterIndex = 1;
				ofn.lpstrFileTitle = 0;
				ofn.nMaxFileTitle = 0;
				ofn.lpstrInitialDir = 0;
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

				// Display the Open dialog box. 

				if( GetOpenFileName( &ofn ) )
				{
					HWND	hlistwnd = GetDlgItem( hwnd, ID_LIST );
					char *	szItem = tmpbuff;
					char	szLength = strlen( szItem );

					// did we get multiple files

					if( !szItem[ szLength + 1 ] )
					{
						// single file

						// add to playlist and to listview

						PLAYITEM * playitem = xine_ui->PlaylistAdd( szItem + ofn.nFileOffset, szItem, 0 );
						AddPlaylistItem( hlistwnd, playitem );
					}
					else
					{
						// multiple files

						szItem = szItem + szLength + 1;
						szLength	= strlen( szItem );

						while( szLength )
						{
							char tmpfname[ 1024 ];
							sprintf( tmpfname, "%s\\%s", tmpbuff, szItem );

							// add to playlist and to listview

							PLAYITEM * playitem = xine_ui->PlaylistAdd( szItem, tmpfname, 0 );
							AddPlaylistItem( hlistwnd, playitem );

							szItem = szItem + szLength + 1;
							szLength	= strlen( szItem );
						}
					}

					xine_ui->Play( xine_ui->playindex );
				}

				return 0L;
			}

			if( cid == ID_DEL )
			{
				HWND hlistwnd = GetDlgItem( hwnd, ID_LIST );
				int	lvindex;

				while( ( lvindex = ListView_GetNextItem( hlistwnd, -1, LVNI_SELECTED ) ) != -1 )
				{
					LVITEM lvitem;
					lvitem.mask = LVIF_PARAM;
					lvitem.iItem = lvindex;
					ListView_GetItem( hlistwnd, &lvitem );

					PLAYITEM * playitem = ( PLAYITEM * ) lvitem.lParam;

					if( xine_ui->PlaylistDel( lvindex ) )
						ListView_DeleteItem( hlistwnd, lvindex );
				}

				xine_ui->Play( xine_ui->playindex );
				return 0L;
			}
		}
		break;

		case WM_SIZE:
		{
			ResizeChildren( hwnd );
            return 0L;
		}

		case WM_DESTROY:
		{
			xine_ui->end_playlistwnd();
			return 0L;
		}
		
	}

    return DefWindowProc( hwnd, msg, wparam, lparam);
}


bool XINE_UI::init_playlistwnd()
{
	// if our playlist is already open, return

	if( hplaylistwnd )
		return true;

    WNDCLASSEX	wc;

    // register our window class

    wc.cbSize        = sizeof( wc );
    wc.lpszClassName = TEXT( "xineplaylistwindow" );
    wc.lpfnWndProc   = proc_playlistwnd;
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

    hplaylistwnd = CreateWindowEx(	0,
									TEXT( "xineplaylistwindow" ),
									TEXT( "xine Playlist" ),
									WS_POPUP | WS_CAPTION | WS_CHILD | WS_SIZEBOX | WS_SYSMENU,
									CW_USEDEFAULT, CW_USEDEFAULT,
  									dwWindowWidth, dwWindowHeight,
									hctrlwnd,
									NULL,
									hinst,
									NULL );
    if( !hplaylistwnd )
	{
		error( "init_playlistwnd : cannot create video window" );
    	return false;
	}

    ShowWindow( hplaylistwnd, SW_SHOW );
    UpdateWindow( hplaylistwnd );

	SetWindowLong( hplaylistwnd, GWL_USERDATA, ( long ) this );

	if( !CreateStatusWindow( WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 
							 "Add or Delete files from the playlist", 
							 hplaylistwnd, 
							 ID_STATUS ) )
	{
		error( "CreateStatusWindow : cannot create status window" );
    	return false;
	}

	hfont = CreateFont(	13,				// logical height of font
						5,				// logical average character width
						0,				// angle of escapement
						0,				// base-line orientation angle
						0,				// font weight
						0,				// italic attribute flag
						0,				// underline attribute flag
						0,				// strikeout attribute flag
						0,				// character set identifier
						0,				// output precision
						0,				// clipping precision
 						PROOF_QUALITY,					// output quality
						FF_MODERN | VARIABLE_PITCH ,	// pitch and family
						"Areal" );						// pointer to typeface name string

    if( !hfont )
	{
		error( "CreateFont : cannot create font" );
    	return false;
	}

    HWND hlistwnd = CreateWindowEx(	WS_EX_STATICEDGE,
									WC_LISTVIEW,
									0,
									WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOCOLUMNHEADER,
									0, 0,
  									0, 0,
									hplaylistwnd,
									( HMENU ) ID_LIST,
									hinst,
									NULL );
    if( !hlistwnd )
	{
		error( "CreateWindow : cannot create list view" );
    	return false;
	}

	AddPlaylistColumn( hlistwnd, 20, 0 );
	AddPlaylistColumn( hlistwnd, 100, 1 );

	SendMessage( hlistwnd, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, ( LPARAM ) LVS_EX_FULLROWSELECT );
	SendMessage( hlistwnd, WM_SETFONT, ( WPARAM ) hfont, MAKELPARAM( TRUE, 0 ) );
	ListView_SetBkColor( hlistwnd, RGB( 0, 0, 0 ) );
	ListView_SetTextBkColor( hlistwnd, RGB( 0, 0, 0 ) );
	ListView_SetTextColor( hlistwnd, RGB( 255, 255, 255 ) );

    HWND haddwnd = CreateWindow(	"BUTTON",
									TEXT( "Add" ),
									WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
									0, 0,
  									0, 0,
									hplaylistwnd,
									( HMENU ) ID_ADD,
									hinst,
									NULL );
    if( !haddwnd )
	{
		error( "CreateWindow : cannot create add button" );
    	return false;
	}

	SendMessage( haddwnd, WM_SETFONT, ( WPARAM ) hfont, MAKELPARAM( TRUE, 0 ) );

    HWND hdelwnd = CreateWindow(	"BUTTON",
									TEXT( "Del" ),
									WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
									0, 0,
  									0, 0,
									hplaylistwnd,
									( HMENU ) ID_DEL,
									hinst,
									NULL );
    if( !hdelwnd )
	{
		error( "CreateWindow : cannot create del button" );
    	return false;
	}

	SendMessage( hdelwnd, WM_SETFONT, ( WPARAM ) hfont, MAKELPARAM( TRUE, 0 ) );

	// resize all playlist window children

	ResizeChildren( hplaylistwnd );

	// add all playlist items to view

	for( int x = 0; x < playcount; x++ )
		AddPlaylistItem( hlistwnd, playlist[ x ] );

	return true;
}

void XINE_UI::end_playlistwnd()
{
	DestroyWindow( hplaylistwnd );
	UnregisterClass( "xineplaylistwindow", hinst );

	hplaylistwnd = 0;
}
