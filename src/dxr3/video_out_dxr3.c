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
 * $Id: video_out_dxr3.c,v 1.10 2001/10/14 14:49:54 ehasenle Exp $
 *
 * Dummy video out plugin for the dxr3. Is responsible for setting
 * tv_mode, bcs values and the aspectratio.
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

#include <linux/em8300.h>
#include "video_out.h"
#include "xine_internal.h"
#include "dxr3_overlay.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include "../video_out/video_out_x11.h"

#define LOOKUP_DEV "dxr3_devname"
#define DEFAULT_DEV "/dev/em8300"
static char *devname;

typedef struct dxr3_driver_s {
	vo_driver_t      vo_driver;
	int fd_control;
	int aspectratio;
	int tv_mode;
	em8300_bcs_t bcs;
	
	/* for overlay */
	dxr3_overlay_t overlay;
	Display *display;
	Drawable win;
	GC gc;     
	XColor color;
	int xpos, ypos;
	int width, height;
	int overlay_enabled;
	float desired_ratio;

	int video_width;
	int video_height;
	int video_aspect;
	void (*request_dest_size) (int video_width, int video_height, int *dest_x,
	 int *dest_y, int *dest_height, int *dest_width);
} dxr3_driver_t;

static int dxr3_set_property (vo_driver_t *this_gen, int property, int value);

static void dxr3_overlay_adapt_area(dxr3_driver_t *this,
 int dest_x, int dest_y, int dest_width, int dest_height)
{
	XWindowAttributes a;
	Window junkwin;
	int rx, ry;

	XLockDisplay(this->display);

	XSetForeground(this->display, this->gc, this->color.pixel);
	XGetWindowAttributes(this->display, this->win, &a);
    XTranslateCoordinates (this->display, this->win, a.root,
	 dest_x, dest_y, &rx, &ry, &junkwin);

	XUnlockDisplay(this->display);
	
	this->xpos = rx; this->ypos = ry;
	this->width = dest_width; this->height = dest_height;

	dxr3_overlay_set_window(&this->overlay, this->xpos, this->ypos,
	 this->width, this->height);
}

static void dxr3_get_keycolor(dxr3_driver_t *this)
{
	this->color.red   = ((this->overlay.colorkey >> 16) & 0xff) * 256;
	this->color.green = ((this->overlay.colorkey >>  8) & 0xff) * 256;
	this->color.blue  = ((this->overlay.colorkey      ) & 0xff) * 256;

	XAllocColor(this->display, DefaultColormap(this->display,0), &this->color);
}
	
void dxr3_read_config(dxr3_driver_t *this, config_values_t *config)
{
	char* str;

	if (ioctl(this->fd_control, EM8300_IOCTL_GETBCS, &this->bcs))
		fprintf(stderr, "dxr3_vo: cannot read bcs values (%s)\n",
		 strerror(errno));
	this->vo_driver.set_property(&this->vo_driver,
	 VO_PROP_ASPECT_RATIO, ASPECT_FULL);

	str = config->lookup_str(config, "dxr3_tvmode", "default");
	if (!strcmp(str, "ntsc")) {
		this->tv_mode = EM8300_VIDEOMODE_NTSC;
		fprintf(stderr, "dxr3_vo: setting tv_mode to NTSC\n");
	} else if (!strcmp(str, "pal")) {
		this->tv_mode = EM8300_VIDEOMODE_PAL;
		fprintf(stderr, "dxr3_vo: setting tv_mode to PAL 50Hz\n");
	} else if (!strcmp(str, "pal60")) {
		this->tv_mode = EM8300_VIDEOMODE_PAL60;
		fprintf(stderr, "dxr3_vo: setting tv_mode to PAL 60Hz\n");
	} else if (!strcmp(str, "overlay")) {
		this->tv_mode = EM8300_VIDEOMODE_DEFAULT;
		fprintf(stderr, "dxr3_vo: setting up overlay mode\n");
		if (dxr3_overlay_read_state(&this->overlay) == 0) {
			this->overlay_enabled = 1;
			
			str = config->lookup_str(config, "dxr3_keycolor", "0x80a040");
			sscanf(str, "%x", &this->overlay.colorkey);

			str = config->lookup_str(config, "dxr3_color_interval", "50.0");
			sscanf(str, "%f", &this->overlay.color_interval);
		} else {
			fprintf(stderr, "dxr3_vo: please run autocal, overlay disabled\n");
		}
	} else {
		this->tv_mode = EM8300_VIDEOMODE_DEFAULT;
	}
	if (this->tv_mode != EM8300_VIDEOMODE_DEFAULT)
		if (ioctl(this->fd_control, EM8300_IOCTL_SET_VIDEOMODE, &this->tv_mode))
			fprintf(stderr, "dxr3_vo: setting video mode failed.");
}

static uint32_t dxr3_get_capabilities (vo_driver_t *this_gen)
{
	/* Since we have no vo format, we return dummy values here */
	return VO_CAP_YV12 | IMGFMT_YUY2 | IMGFMT_RGB |
		VO_CAP_SATURATION | VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST;
}

/* This are dummy functions to fill in the frame object */
static void dummy_frame_copy (vo_frame_t *vo_img, uint8_t **src)
{
	fprintf(stderr, "dxr3_vo: dummy_frame_copy called!\n");
}

static void dummy_frame_field (vo_frame_t *vo_img, int which_field)
{
	fprintf(stderr, "dxr3_vo: dummy_frame_field called!\n");
}

static void dummy_frame_dispose (vo_frame_t *frame)
{
	free(frame);
}

static vo_frame_t *dxr3_alloc_frame (vo_driver_t *this_gen)
{
	vo_frame_t     *frame;

	frame = (vo_frame_t *) malloc (sizeof (vo_frame_t));
	memset (frame, 0, sizeof(vo_frame_t));

	frame->copy    = dummy_frame_copy;
	frame->field   = dummy_frame_field; 
	frame->dispose = dummy_frame_dispose;

	return frame;
}

static void dxr3_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags)
{
	dxr3_driver_t  *this = (dxr3_driver_t *) this_gen; 

	int aspect;
	this->video_width  = width;
	this->video_height = height;
	this->video_aspect = ratio_code;

	if (ratio_code < 3 || ratio_code>4)
	  aspect = ASPECT_FULL;
	else
	  aspect = ASPECT_ANAMORPHIC;
	if(this->aspectratio!=aspect)
	  dxr3_set_property (this_gen, VO_PROP_ASPECT_RATIO, aspect);
}

static void dxr3_display_frame (vo_driver_t *this_gen, vo_frame_t *frame)
{
	/* dxr3_driver_t  *this = (dxr3_driver_t *) this_gen; */
	fprintf(stderr, "dxr3_vo: dummy function dxr3_display_frame called!\n");
}

static void dxr3_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
 vo_overlay_t *overlay)
{
	/* dxr3_driver_t *this = (dxr3_driver_t *) this_gen; */
	fprintf(stderr, "dxr3_vo: dummy function dxr3_overlay_blend called!\n");
}

static int dxr3_get_property (vo_driver_t *this_gen, int property)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;
	int val;

	switch (property) {
	case VO_PROP_SATURATION:
		val = this->bcs.saturation;
		break;
		
	case VO_PROP_CONTRAST:
		val = this->bcs.contrast;
		break;
		
	case VO_PROP_BRIGHTNESS:
		val = this->bcs.brightness;
		break;

	case VO_PROP_ASPECT_RATIO:
		val = this->aspectratio;
		break;
	
	case VO_PROP_COLORKEY:
		val = this->overlay.colorkey;
		break;
	default:
		val = 0;
		fprintf(stderr, "dxr3_vo: property %d not implemented!\n", property);
	}

	return val;
}

static int is_fullscreen(dxr3_driver_t *this)
{
	XWindowAttributes a;

	XGetWindowAttributes(this->display, this->win, &a);
	/* this is a good place for gathering the with and height
	 * although it is a mis-use for is_fullscreen */
	this->width  = a.width;
	this->height = a.height;
	
	return a.x==0 && a.y==0 &&
	 a.width  == this->overlay.screen_xres &&
	 a.height == this->overlay.screen_yres;
}

static int dxr3_set_property (vo_driver_t *this_gen, 
			      int property, int value)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;
	int val, bcs_changed = 0;
	int fullscreen;

	switch (property) {
	case VO_PROP_SATURATION:
		this->bcs.saturation = value;
		bcs_changed = 1;
		break;
	case VO_PROP_CONTRAST:
		this->bcs.contrast = value;
		bcs_changed = 1;
		break;
	case VO_PROP_BRIGHTNESS:
		this->bcs.brightness = value;
		bcs_changed = 1;
		break;
	case VO_PROP_ASPECT_RATIO:
		/* xitk-ui just increments the value, so we make
		 * just a two value "loop"
		 */
		if (value > ASPECT_FULL)
			value = ASPECT_ANAMORPHIC;
		this->aspectratio = value;
		fullscreen = is_fullscreen(this);

		if (value == ASPECT_ANAMORPHIC) {
			fprintf(stderr, "dxr3_vo: setting aspect ratio to anamorphic\n");
			if (!this->overlay_enabled || fullscreen)
				val = EM8300_ASPECTRATIO_16_9;
			else /* The overlay window can adapt to the ratio */
				val = EM8300_ASPECTRATIO_4_3;
			this->desired_ratio = 16.0/9.0;
		} else {
			fprintf(stderr, "dxr3_vo: setting aspect ratio to full\n");
			val = EM8300_ASPECTRATIO_4_3;
			this->desired_ratio = 4.0/3.0;
		}

		if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &val))
			fprintf(stderr, "dxr3_vo: failed to set aspect ratio (%s)\n",
			 strerror(errno));
		if (this->overlay_enabled && !fullscreen){
			int foo;
			this->request_dest_size(this->width,
			 this->width/this->desired_ratio, &foo, &foo, &foo, &foo);
		}
		break;
	case VO_PROP_COLORKEY:
		fprintf(stderr, "dxr3_vo: VO_PROP_COLORKEY not implemented!");
		this->overlay.colorkey = val;
		break;
	}

	if (bcs_changed)
		if (ioctl(this->fd_control, EM8300_IOCTL_SETBCS, &this->bcs))
			fprintf(stderr, "dxr3_vo: bcs set failed (%s)\n",
			 strerror(errno));
	return value;
}

static void dxr3_get_property_min_max (vo_driver_t *this_gen, 
 int property, int *min, int *max)
{
	/* dxr3_driver_t *this = (dxr3_driver_t *) this_gen;  */

	switch (property) {
	case VO_PROP_SATURATION:
	case VO_PROP_CONTRAST:
	case VO_PROP_BRIGHTNESS:
		*min = 0;
		*max = 1000;
		break;

	default:
		*min = 0;
		*max = 0;
	}
}

static void dxr3_translate_gui2video(dxr3_driver_t *this,
				     int x, int y,
				     int *vid_x, int *vid_y)
{
	x = x * this->video_width / this->width;
	y = y * this->video_height / this->height;
	*vid_x = x;
	*vid_y = y;
}

static int dxr3_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data)
{
	dxr3_driver_t *this = (dxr3_driver_t*) this_gen;
	x11_rectangle_t *area;
	XWindowAttributes a;

	if (!this->overlay_enabled) return 0;

	switch (data_type) {
	case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:
		area = (x11_rectangle_t*) data;
		dxr3_overlay_adapt_area(this, area->x, area->y, area->w, area->h);
		break;
	case GUI_DATA_EX_EXPOSE_EVENT:
		XLockDisplay(this->display);
		XFillRectangle(this->display, this->win,
		 this->gc, 0, 0, this->width, this->height);
		XUnlockDisplay(this->display);
		break;
	case GUI_DATA_EX_DRAWABLE_CHANGED:
		this->win = (Drawable) data;
		this->gc = XCreateGC(this->display, this->win, 0, NULL);
		XGetWindowAttributes(this->display, this->win, &a);
		dxr3_set_property((vo_driver_t*) this,
		 VO_PROP_ASPECT_RATIO, this->aspectratio);
		break;
	case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO:
		{
			int x1, y1, x2, y2;
			x11_rectangle_t *rect = data;

			dxr3_translate_gui2video(this, rect->x, rect->y,
						 &x1, &y1);
			dxr3_translate_gui2video(this, rect->w, rect->h,
						 &x2, &y2);
			rect->x = x1;
			rect->y = y1;
			rect->w = x2-x1;
			rect->h = y2-y1;
		}
		break;
	default:
		return -1;
	}
	return 0;
}

static void dxr3_exit (vo_driver_t *this_gen)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;

	if(this->overlay_enabled)
		dxr3_overlay_set_mode(&this->overlay, EM8300_OVERLAY_MODE_OFF );
	close(this->fd_control);
}

static void gather_screen_vars(dxr3_driver_t *this, x11_visual_t *vis)
{
	int scrn;
#ifdef HAVE_XINERAMA
	int screens;
	int dummy_a, dummy_b;
	XineramaScreenInfo *screeninfo = NULL;
#endif

	this->win = vis->d;
	this->display = vis->display;
	this->gc = XCreateGC(this->display, this->win, 0, NULL);
	scrn = DefaultScreen(this->display);

	/* Borrowed from xine-ui in order to detect fullscreen */
#ifdef HAVE_XINERAMA
	if (XineramaQueryExtension(this->display, &dummy_a, &dummy_b) &&
	 (screeninfo = XineramaQueryScreens(this->display, &screens)) &&
	 XineramaIsActive(this->display))
	{
		this->overlay.screen_xres = screeninfo[0].width;
		this->overlay.screen_yres = screeninfo[0].height;
	} else
#endif
	{
		this->overlay.screen_xres = DisplayWidth(this->display, scrn);
		this->overlay.screen_yres = DisplayHeight(this->display, scrn);
	}

	this->overlay.screen_depth = DisplayPlanes(this->display, scrn);
	this->request_dest_size = vis->request_dest_size;
	printf("xres %d yres %d depth %d\n", this->overlay.screen_xres, this->overlay.screen_yres, this->overlay.screen_depth);
}

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen)
{
	dxr3_driver_t *this;

	/*
	* allocate plugin struct
	*/

	this = malloc (sizeof (dxr3_driver_t));

	if (!this) {
		printf ("video_out_dxr3: malloc failed\n");
		return NULL;
	}

	memset (this, 0, sizeof(dxr3_driver_t));

	this->vo_driver.get_capabilities     = dxr3_get_capabilities;
	this->vo_driver.alloc_frame          = dxr3_alloc_frame;
	this->vo_driver.update_frame_format  = dxr3_update_frame_format;
	this->vo_driver.display_frame        = dxr3_display_frame;
	this->vo_driver.overlay_blend        = dxr3_overlay_blend;
	this->vo_driver.get_property         = dxr3_get_property;
	this->vo_driver.set_property         = dxr3_set_property;
	this->vo_driver.get_property_min_max = dxr3_get_property_min_max;
	this->vo_driver.gui_data_exchange    = dxr3_gui_data_exchange;
	this->vo_driver.exit                 = dxr3_exit;

	/* open control device */
	devname = config->lookup_str (config, LOOKUP_DEV, DEFAULT_DEV);
	if ((this->fd_control = open(devname, O_WRONLY)) < 0) {
		fprintf(stderr, "dxr3_vo: Failed to open control device %s (%s)\n",
		 devname, strerror(errno));
		return 0;
	}

	gather_screen_vars(this, visual_gen);
	
	/* default values */
	this->overlay_enabled = 0;
	this->aspectratio = ASPECT_FULL;

	dxr3_read_config(this, config);
	
	if (this->overlay_enabled) {
		dxr3_get_keycolor(this);
		dxr3_overlay_buggy_preinit(&this->overlay, this->fd_control);
	}

	return &this->vo_driver;
}

static vo_info_t vo_info_dxr3 = {
  2, /* api version */
  "dxr3",
  "xine dummy video output plugin for dxr3 cards",
  VISUAL_TYPE_X11,
  20  /* priority */
};

vo_info_t *get_video_out_plugin_info()
{
	return &vo_info_dxr3;
}

