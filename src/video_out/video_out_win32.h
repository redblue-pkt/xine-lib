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
 * $Id: video_out_win32.h,v 1.1 2003/04/22 20:09:39 tchamp Exp $
 *
 * structs and defines specific to all win32 related output plugins
 * (any win32 base xine ui should include this)
 */

#include <windows.h>
#include <windowsx.h>
#include "inttypes.h"

#include "vo_scale.h"

#ifndef HAVE_VIDEO_OUT_WIN32_H
#define HAVE_VIDEO_OUT_WIN32_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * this is the visual data struct any win32 gui should supply
 * (pass this to init_video_out_plugin or the xine_load_video_output_plugin
 * utility function)
 */

typedef struct {

	HWND		WndHnd;			/* handle of window associated with primary surface */
	HINSTANCE	HInst;			/* handle of windows application instance */
	RECT		WndRect;		/* rect of window client points translated to screen cooridnates */
	boolean		FullScreen;		/* is window fullscreen */
	HBRUSH		Brush;			/* window brush for background color */
	COLORREF	ColorKey;		/* window brush color key */
	vo_scale_t  vs;

} win32_visual_t;

/*
 * constants for gui_data_exchange's data_type parameter
 */

#define GUI_WIN32_MOVED_OR_RESIZED	0

#ifdef __cplusplus
}
#endif

#endif
