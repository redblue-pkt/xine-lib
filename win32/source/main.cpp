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
#include "common.h"

#include <fcntl.h>
#include <io.h>

#define MAX_CONSOLE_LINES 1000

gGui_t *gGui;

void RedirectIOToConsole()
{
    int                        hConHandle;
    long                       lStdHandle;
    CONSOLE_SCREEN_BUFFER_INFO coninfo;
    FILE                       *fp;

    // allocate a console for this app
    AllocConsole();

    // set the screen buffer to be big enough to let us scroll text
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), 
                               &coninfo);
    coninfo.dwSize.Y = MAX_CONSOLE_LINES;
    SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), 
                               coninfo.dwSize);

    // redirect unbuffered STDOUT to the console
    lStdHandle = (long)GetStdHandle(STD_OUTPUT_HANDLE);
    hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);

	/* This was happening when launched from a Cygwin shell */
	if (hConHandle == -1) {
		FreeConsole();
		return;
	}

    fp = _fdopen( hConHandle, "w" );
    *stdout = *fp;
    setvbuf( stdout, NULL, _IONBF, 0 );

    // redirect unbuffered STDIN to the console
    lStdHandle = (long)GetStdHandle(STD_INPUT_HANDLE);
    hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);

	if (hConHandle == -1) {
		FreeConsole();
		return;
	}

    fp = _fdopen( hConHandle, "r" );
    *stdin = *fp;
    setvbuf( stdin, NULL, _IONBF, 0 );

    // redirect unbuffered STDERR to the console
    lStdHandle = (long)GetStdHandle(STD_ERROR_HANDLE);
    hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);

	if (hConHandle == -1) {
		FreeConsole();
		return;
	}

    fp = _fdopen( hConHandle, "w" );
    *stderr = *fp;
    setvbuf( stderr, NULL, _IONBF, 0 );
    
    // make cout, wcout, cin, wcin, wcerr, cerr, wclog and clog 
    // point to console as well
    /*ios::sync_with_stdio();*/
}

int WINAPI WinMain(	HINSTANCE hinst, HINSTANCE hprevinst, LPSTR cmdline, int ncmdshow )
{
	XINE_UI xine_ui;


#if !defined (__MINGW32__)
	/* We only need the output window for MSVC */
	RedirectIOToConsole();
#endif

	// prepair our mrl(s) and add them
	// to our playlist

	char * next_mrl = cmdline;
	while( next_mrl )
	{
		char temp_mrl[ 1024 ];
		memset( temp_mrl, 0, sizeof( temp_mrl ) );

		if( *next_mrl == 0 )
			break;

		if( *next_mrl == ' ' )
		{
			next_mrl++;
			continue;
		}

		if( *next_mrl == '\"' )
		{
			strcpy( temp_mrl, next_mrl + 1 );

			char * end_mrl = strchr( temp_mrl, '\"' );
			if( end_mrl )
			{
				*end_mrl = 0;
				next_mrl = end_mrl + 1;
			}
			else
				next_mrl = 0;
		}
		else
		{
			strcpy( temp_mrl, next_mrl );

			char * end_mrl = strchr( temp_mrl, ' ' );
			if( end_mrl )
			{
				*end_mrl = 0;
				next_mrl = end_mrl + 1;
			}
			else
				next_mrl = 0;
		}

		char * back_slash = strrchr( temp_mrl, '\\' );
		char * fore_slash = strrchr( temp_mrl, '/' );
		char * last_slash = 0;

		if( back_slash > temp_mrl )
			if( *( back_slash - 1 ) == ':' )
				back_slash = 0;

		if( back_slash > fore_slash )
			last_slash = back_slash;
		else
			last_slash = fore_slash;

		if( last_slash )
			xine_ui.PlaylistAdd( last_slash + 1, temp_mrl, 0 );
		else
			xine_ui.PlaylistAdd( temp_mrl, temp_mrl, 0 );
	}

	// initialize common control tools

	InitCommonControls();

	// init gui

	if( !xine_ui.InitGui( hinst ) )
		return 1;

	// init libxine

	if( !xine_ui.InitXine() )
		return 1;

	// start playback

	if( xine_ui.playcount )
		xine_ui.Play( 0 );


	// start the message loop. 
 
	MSG msg; 

	while( GetMessage( &msg, ( HWND ) NULL, 0, 0 ) ) 
	{ 
		TranslateMessage( &msg ); 
		DispatchMessage( &msg ); 
	} 
 
	// return the exit code to Windows. 

	return msg.wParam;
}
