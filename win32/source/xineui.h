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

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <math.h>
#include <ddraw.h>

#include "xine.h"
#include "xineint.h"
#include "resource.h"

#include "common.h"

#include "video_out.h"
#include "audio_out.h"

#include "video_out_win32.h"

#ifndef _XINEUI_H_
#define _XINEUI_H_

#define ID_PANEL		10001
#define ID_TIMEBAR		10002
#define ID_TOOLBAR		10003

#define ID_PLAY_BTTN	20001
#define ID_PAUSE_BTTN	20002
#define ID_STOP_BTTN	20003
#define ID_PREV_BTTN	20004
#define ID_NEXT_BTTN	20005
#define ID_FFWD_BTTN	20006
#define ID_RWND_BTTN	20007
#define ID_EJECT_BTTN	20008

#define ID_TITLE		10001
#define ID_TIME			10002
#define ID_CONFIG		10003
#define ID_FULLSCREEN	10004
#define ID_SPULABEL		10005
#define ID_SPUINC		10006
#define ID_SPUDEC		10007
#define ID_SPUVALUE		10008
#define ID_AUDIOLABEL	10009
#define ID_AUDIOINC		10010
#define ID_AUDIODEC		10011
#define ID_AUDIOVALUE	10012
#define ID_VOLBUTTON	10013
#define ID_VOLBAR		10014

#define ID_STATUS		10001
#define ID_LIST			10002
#define ID_ADD			10003
#define ID_DEL			10004

typedef struct _PLAYITEM
{
	char *	mrl_short_name;
	char *	mrl_long_name;
	int		mrl_type;

}PLAYITEM;

#define MAX_PLAYITEMS			10004

typedef class _XINE_UI
{
	public:

	config_values_t *	config;

	gGui_t  *gui;

	bool	init_ctrlwnd();
	void	end_ctrlwnd();
	bool	init_videownd();
	void	end_videownd();
	bool	init_panelwnd();
	void	end_panelwnd();
	bool	init_playlistwnd();
	void	end_playlistwnd();

	void error( LPSTR szfmt, ... );
	void warning( LPSTR szfmt, ... );

	char *	mrl_long_name;
	char *	mrl_short_name;
	int		mrl_type;
	int		mrl_time_length;
	int		mrl_time_current;
	int		spu_channel;
	int		audio_channel;
	int		mode;

	PLAYITEM *	playlist[ MAX_PLAYITEMS ];
	int			playcount;
	int			playindex;

	win32_visual_t		win32_visual;

	HINSTANCE	hinst;
	HWND		hctrlwnd;
	HWND		hpanelwnd;
	HWND		hvideownd;
	HWND		hplaylistwnd;
	bool		tracking;

	_XINE_UI();
	~_XINE_UI();

	DWORD	UpdateLoop();
	bool	UpdateCtrl();
	bool	UpdatePanel();

	bool	InitGui( HINSTANCE hinstance );
	void	EndGui();
	bool	InitXine();
	void	EndXine();

	PLAYITEM *	PlaylistAdd( char * short_name, char * long_name, int type );
	bool		PlaylistDel( int index );

	bool	Play( int playindex );
	bool	Stop();
	bool	SetTime( int time );

	bool	SetSpeed( int speed );
	int		GetSpeed();

	bool	SelectSpuChannel( int channel );
	bool	SelectAudioChannel( int channel );

	bool	SetVolume( int volume );
	bool	SetMute( bool mute );

	bool	DriverMessage( int type, void * param );

}XINE_UI;

extern gGui_t *gGui;

#endif