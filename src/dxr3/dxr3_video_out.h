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
 * $Id: dxr3_video_out.h,v 1.15 2002/03/08 00:24:40 jcdutton Exp $
 *
 */

/*
 * Globals for dxr3 videoout plugins
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

#include <linux/em8300.h>
#include "video_out.h"
#include "xine_internal.h"

/* for fast_memcpy: */
#include "xineutils.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include "../video_out/video_out_x11.h"

#define LOOKUP_DEV "dxr3.devicename"
#define DEFAULT_DEV "/dev/em8300"

/* image format used by dxr3_decoder to tag undecoded mpeg data */
#define IMGFMT_MPEG (('G'<<24)|('E'<<16)|('P'<<8)|'M')

struct coeff {
    	float 	k,m;
};

typedef struct {
	int 	fd_control;

	int 	xoffset;
	int 	yoffset;
	int 	xcorr;
	int 	jitter;
	int 	stability;
	int 	colorkey;
	float 	color_interval;
	int 	screen_xres;
	int 	screen_yres;
	int 	screen_depth;

	struct coeff colcal_upper[3];
	struct coeff colcal_lower[3];
} dxr3_overlay_t;

typedef enum { ENC_FAME, ENC_RTE } encoder_type;
typedef struct encoder_data_s encoder_data_t;

typedef struct dxr3_driver_s {
	vo_driver_t     vo_driver;
	config_values_t *config;
	int 		fd_control;
        int 		fd_video;
	int 		aspectratio;
	int 		tv_mode;
	int		enhanced_mode; /* enhanced play mode */
	em8300_bcs_t 	bcs;
	char		devname[128];
	char		devnum[3];

	/* for encoder plugin */
	encoder_data_t	*enc; /* encoder data */
	double		fps; /* frames per second */
	int		format; /* color format */
	const char	*file_out;
	int		swap_fields; /* swap fields */
	int		add_bars; /* add black bars to correct a.r. */
	/* height after adding black bars to correct a.r. */
        int 		oheight; 
	int		top_bar; /* number of lines in top black bar */
	/* input height (before adding black bars) */
	int 		video_iheight; 
	/* output height (after adding bars) */
	int 		video_height;  

	/* for overlay */
	dxr3_overlay_t overlay;
	Display 	*display;
	Drawable 	win;
	GC 		gc;     
	XColor 		color;
	int 		xpos, ypos;
	int 		width, height; 
	int 		overlay_enabled;
	int		tv_switchable;	/* can switch from overlay<->tvout */
	int		fullscreen_rectangle;
	float 		desired_ratio;

	int 		zoom_enabled;   /* zoomed 16:9 mode */

	int 		video_width;
	int 		video_aspect;
	
	char 		*user_data;

	void 		(*frame_output_cb) (char *userdata, int video_width, 
				int video_height, int *dest_x,
		        	int *dest_y, int *dest_height, int *dest_width);
} dxr3_driver_t;

typedef struct dxr3_frame_s {
  vo_frame_t    vo_frame;
  int           width, height,oheight;
  uint8_t       *mem; 		/* allocated for YV12 or YUY2 buffers */
  uint8_t       *real_base[3]; 	/* yuv/yuy2 buffers in mem aligned on 16 */
  int           copy_calls; 	/* counts calls to dxr3_frame_copy function */
  int		swap_fields;	/* shifts Y buffer one line to exchange odd/even lines*/
} dxr3_frame_t;

struct encoder_data_s {
	encoder_type type;
	int (*on_update_format)(dxr3_driver_t *);
	int (*on_frame_copy)(dxr3_driver_t *, dxr3_frame_t *, uint8_t **src);
	int (*on_display_frame)(dxr3_driver_t *, dxr3_frame_t *);
	int (*on_close)(dxr3_driver_t *);
}; 

/* func definitions */
/* Overlay functions */
int dxr3_overlay_set_mode(dxr3_overlay_t *this, int mode);
int dxr3_overlay_set_attributes(dxr3_overlay_t *this);
int dxr3_overlay_set_screen(dxr3_overlay_t *this);
int dxr3_overlay_set_window(dxr3_overlay_t *this,
				 int xpos, int ypos, int width, int height);

void dxr3_overlay_buggy_preinit(dxr3_overlay_t *this, int fd);
int dxr3_overlay_read_state(dxr3_overlay_t *this);
void dxr3_get_keycolor(dxr3_driver_t *this);
void dxr3_read_config(dxr3_driver_t *this);

void *malloc_aligned (size_t alignment, size_t size, void **mem);
void gather_screen_vars(dxr3_driver_t *this, x11_visual_t *vis);

/* xine accessable functions */
int dxr3_get_property (vo_driver_t *this_gen, int property);
int dxr3_set_property (vo_driver_t *this_gen, int property, int value);
void dxr3_get_property_min_max (vo_driver_t *this_gen, int property, int *min, int *max);
int dxr3_gui_data_exchange (vo_driver_t *this_gen,  int data_type, void *data);

