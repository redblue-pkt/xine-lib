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
 * $Id: dxr3_vo_standard.c,v 1.9 2001/12/15 20:56:21 hrm Exp $
 *
 *******************************************************************
 * Dummy video out plugin for the dxr3. Is responsible for setting *
 * tv_mode, bcs values, setting up the overlay (if enabled),	   *
 * and the aspectratio.						   *
 *								   *
 * All general functions have been moved to dxr3_vo_core.c.	   *
   Functions and globals included here (in order of appearance):
	 	dxr3_get_capabilities
	 	dummy_frame_copy
	 	dummy_frame_field
	 	dummy_frame_dispose
	 	dxr3_alloc_frame
	 	dxr3_update_frame_format
	 	dxr3_display_frame
	 	dxr3_overlay_blend
	 	dxr3_exit
	 	init_video_out_plugin
	 	vo_info_dxr3
	 	get_video_out_plugin_info
 ********************************************************************/

/* Dxr3 videoout globals */
#include "dxr3_video_out.h"

/* some helper stuff so that the decoder plugin can test for the
 * presence of the dxr3 vo driver */
/* to be called by dxr3 video out init and exit handlers */
static void dxr3_set_vo(dxr3_driver_t* this, int active)
{
	cfg_entry_t *entry;
	config_values_t *config = this->config;

	entry = config->lookup_entry(config, "dxr3.active");
	if (! entry) {
		/* register first */
		config->register_num(config, "dxr3.active", active, 
			"state of dxr3 video out", 
			"(internal variable; do not edit)",
			NULL, NULL);
	}
	else {
		entry->num_value = active;
	}
	printf("dxr3: %s dxr3 video out", (active ? "enabled" : "disabled"));
}

static uint32_t dxr3_get_capabilities (vo_driver_t *this_gen)
{
	/* Since we have no vo format, we return dummy values here */
	return VO_CAP_YV12 | IMGFMT_YUY2 | 
		VO_CAP_SATURATION | VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST;
}

/* This are dummy functions to fill in the frame object */
static void dummy_frame_copy (vo_frame_t *vo_img, uint8_t **src)
{
	fprintf(stderr, "dxr3_vo: This plugin doesn't play non-mpeg video!\n");
}

static void dummy_frame_field (vo_frame_t *vo_img, int which_field)
{
	fprintf(stderr, "dxr3_vo: This plugin doesn't play non-mpeg video!\n");
}

static void dummy_frame_dispose (vo_frame_t *frame_gen)
{
  dxr3_frame_t  *frame = (dxr3_frame_t *) frame_gen;

  if (frame->mem)
  	free (frame->mem);
  free(frame);
}

static vo_frame_t *dxr3_alloc_frame (vo_driver_t *this_gen)
{
        dxr3_frame_t   *frame;
        
        frame = (dxr3_frame_t *) malloc (sizeof (dxr3_frame_t));
        memset (frame, 0, sizeof(dxr3_frame_t));
                 
        frame->vo_frame.copy    = dummy_frame_copy;
        frame->vo_frame.field   = dummy_frame_field;
        frame->vo_frame.dispose = dummy_frame_dispose;
        
	return (vo_frame_t*) frame;
}

static void dxr3_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags)
{
	dxr3_driver_t  *this = (dxr3_driver_t *) this_gen; 
	dxr3_frame_t  *frame = (dxr3_frame_t *) frame_gen; 
	int image_size = -1;
	
  if ((frame->width != width) || (frame->height != height)
        || (frame->format != format)) {
         
        if (frame->mem) {
           free (frame->mem);
           frame->mem = NULL;
        }
        frame->width  = width;
        frame->height = height;
        frame->format = format;

	if(flags == DXR3_VO_UPDATE_FLAG){ /* dxr3 flag anyone? :) */
		int aspect;
		this->video_width  = width;
		this->video_height = height;
		this->video_aspect = ratio_code;
		
		if (ratio_code < 3 || ratio_code>4)
		  aspect = ASPECT_FULL;
		else
		  aspect = ASPECT_ANAMORPHIC;
		if(this->aspectratio!=aspect)
		  dxr3_set_property ((vo_driver_t*)this, VO_PROP_ASPECT_RATIO, aspect);
	}
        
        if (format == IMGFMT_YV12) {
           image_size = width * height;
      	   frame->vo_frame.base[0] = malloc_aligned(16,image_size*3/2, 
      	    		(void**) &frame->mem);
	   frame->vo_frame.base[1] = frame->vo_frame.base[0] + image_size; 
	   frame->vo_frame.base[2] = frame->vo_frame.base[1] + image_size/4; 
	}else if (format == IMGFMT_YUY2) {
      	   frame->vo_frame.base[0] = malloc_aligned(16, image_size*2,
      	   		 (void**)&frame->mem);
           frame->vo_frame.base[1] = frame->vo_frame.base[2] = 0;
	}
      }
}

static void dxr3_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
	/* dxr3_driver_t  *this = (dxr3_driver_t *) this_gen; */
	dxr3_frame_t *frame = (dxr3_frame_t *) frame_gen; 

        frame->vo_frame.displayed (&frame->vo_frame); 
}

static void dxr3_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
 vo_overlay_t *overlay)
{
	/* dxr3_driver_t *this = (dxr3_driver_t *) this_gen; */
	fprintf(stderr, "dxr3_vo: dummy function dxr3_overlay_blend called!\n");
}

void dxr3_exit (vo_driver_t *this_gen)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;


	if(this->overlay_enabled)
		dxr3_overlay_set_mode(&this->overlay, EM8300_OVERLAY_MODE_OFF );
	close(this->fd_control);

	dxr3_set_vo(this, 0);
	free(this);
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
	this->config			     = config;

	/* open control device */
	this->devname = config->register_string (config, LOOKUP_DEV, DEFAULT_DEV, NULL,NULL,NULL,NULL);

	if ((this->fd_control = open(this->devname, O_WRONLY)) < 0) {
		fprintf(stderr, "dxr3_vo: Failed to open control device %s (%s)\n",
		 this->devname, strerror(errno));
		return 0;
	}

	gather_screen_vars(this, visual_gen);
	
	/* default values */
	this->overlay_enabled = 0;
	this->aspectratio = ASPECT_FULL;

	dxr3_read_config(this);
	
	if (this->overlay_enabled) {
		dxr3_get_keycolor(this);
		dxr3_overlay_buggy_preinit(&this->overlay, this->fd_control);
	}
	
	dxr3_set_vo(this, 1);
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

