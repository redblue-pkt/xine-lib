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
 * $Id: dxr3_overlay.h,v 1.1 2001/07/26 16:03:10 ehasenle Exp $
 *
 * Overlay support routines for video_out_dxr3
 */


struct coeff {
    float k,m;
};

typedef struct {
	int fd_control;
	int overlay_enabled;
	int xoffset;
	int yoffset;
	int xcorr;
	int jitter;
	int stability;
	int colorkey;
	float color_interval;
	int screen_xres;
	int screen_yres;
	int screen_depth;
	struct coeff colcal_upper[3];
	struct coeff colcal_lower[3];
} dxr3_overlay_t;

int dxr3_overlay_set_mode(dxr3_overlay_t *this, int mode);

int dxr3_overlay_set_attributes(dxr3_overlay_t *this);

int dxr3_overlay_set_screen(dxr3_overlay_t *this);

int dxr3_overlay_set_window(dxr3_overlay_t *this,
 int xpos, int ypos, int width, int height);

void dxr3_overlay_buggy_preinit(dxr3_overlay_t *this, int fd);

int dxr3_overlay_read_state(dxr3_overlay_t *this);

