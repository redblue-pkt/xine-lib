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
 * $Id: video_out_dxr3.c,v 1.1 2001/07/20 17:28:21 ehasenle Exp $
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

#include <pthread.h>

#include "xine_internal.h"
#include "utils.h"

char devname[]="/dev/em8300";

typedef struct dxr3_driver_s {
	vo_driver_t      vo_driver;
	int fd_control;
	int aspectratio;
	int tv_mode;
	em8300_bcs_t bcs;
} dxr3_driver_t;

static uint32_t dxr3_get_capabilities (vo_driver_t *this_gen)
{
	/* Since we have no vo format, we return dummy values here */
	return VO_CAP_YV12 | IMGFMT_YUY2 | IMGFMT_RGB |
		VO_CAP_SATURATION | VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST;
}

/* This are dummy functions to fill in the frame object */
static void dummy_frame_copy (vo_frame_t *vo_img, uint8_t **src)
{
	printf("prevent this: xshm_frame_copy called\n");
}

static void dummy_frame_field (vo_frame_t *vo_img, int which_field)
{
	printf("prevent this: xshm_frame_field called\n");
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
				      int ratio_code, int format)
{
	/* dxr3_driver_t  *this = (dxr3_driver_t *) this_gen; */
	printf("dummy function: dxr3_update_frame_format\n");
}

static void dxr3_display_frame (vo_driver_t *this_gen, vo_frame_t *frame)
{
	/* dxr3_driver_t  *this = (dxr3_driver_t *) this_gen; */
	printf("dummy function: dxr3_frame called\n");
}

static void dxr3_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
 vo_overlay_t *overlay)
{
	/* dxr3_driver_t *this = (dxr3_driver_t *) this_gen; */
	printf("dummy function: dxr3_overlay_blend\n");
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
	default:
		val = 0;
		fprintf(stderr, "dxr3: property %d not implemented!\n", property);
	}

	return val;
}

static int dxr3_set_property (vo_driver_t *this_gen, 
			      int property, int value)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;
	int val, bcs_changed = 0;

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
		/* xitk just increments the value, so we make
		 * just a two value "loop"
		 */
		if (value > ASPECT_FULL)
			value = ASPECT_ANAMORPHIC;
		this->aspectratio = value;

		if (value == ASPECT_ANAMORPHIC) {
			fprintf(stderr, "dxr3: setting aspect ratio to anamorphic\n");
			val = EM8300_ASPECTRATIO_16_9;
		} else {
			fprintf(stderr, "dxr3: setting aspect ratio to full\n");
			val = EM8300_ASPECTRATIO_4_3;
		}

		if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &val))
			fprintf(stderr, "dxr3: failed to set aspect ratio (%s)\n",
			 strerror(errno));

		break;
	}

	if (bcs_changed)
		if (ioctl(this->fd_control, EM8300_IOCTL_SETBCS, &this->bcs))
			fprintf(stderr, "dxr3: bcs set failed (%s)\n",
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

static int dxr3_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data)
{
  /* dxr3_driver_t     *this = (dxr3_driver_t *) this_gen; */
  return 0;
}

static void dxr3_exit (vo_driver_t *this_gen)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;
	close(this->fd_control);
}


vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen)
{
  dxr3_driver_t        *this;
  char* str;

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
	if ((this->fd_control = open(devname, O_WRONLY)) < 0) {
		fprintf(stderr, "dxr3: Failed to open control device %s (%s)\n",
		 devname, strerror(errno));
		return 0;
	}

	if (ioctl(this->fd_control, EM8300_IOCTL_GETBCS, &this->bcs))
		fprintf(stderr, "dxr3: cannot read bcs values (%s)\n",
		 strerror(errno));
	this->vo_driver.set_property(&this->vo_driver,
	 VO_PROP_ASPECT_RATIO, ASPECT_FULL);

	str = config->lookup_str(config, "dxr3_tvmode", "default");
	if (!strcmp(str, "ntsc")) {
		this->tv_mode = EM8300_VIDEOMODE_NTSC;
		fprintf(stderr, "dxr3: setting tv_mode to NTSC\n");
	} else if (!strcmp(str, "pal")) {
		this->tv_mode = EM8300_VIDEOMODE_PAL;
		fprintf(stderr, "dxr3: setting tv_mode to PAL 50Hz\n");
	} else if (!strcmp(str, "pal60")) {
		this->tv_mode = EM8300_VIDEOMODE_PAL60;
		fprintf(stderr, "dxr3: setting tv_mode to PAL 60Hz\n");
	} else {
		this->tv_mode = EM8300_VIDEOMODE_DEFAULT;
	}
	if (this->tv_mode != EM8300_VIDEOMODE_DEFAULT)
		if (ioctl(this->fd_control, EM8300_IOCTL_SET_VIDEOMODE, &this->tv_mode))
			fprintf(stderr, "dxr3: setting video mode failed.");

  return &this->vo_driver;
}

/* TODO: VISUAL_TYPE_HARDWARE ? */
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

