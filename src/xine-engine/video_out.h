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
 * $Id: video_out.h,v 1.8 2001/06/18 15:43:01 richwareham Exp $
 *
 *
 * xine version of video_out.h 
 *
 */

#ifndef HAVE_VIDEO_OUT_H
#define HAVE_VIDEO_OUT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <pthread.h>

#if defined(XINE_COMPILE)
#include "configfile.h"
#include "metronom.h"
#else
#include "xine/configfile.h"
#include "xine/metronom.h"
#endif

#define VIDEO_OUT_PLUGIN_IFACE_VERSION 1

typedef struct vo_frame_s vo_frame_t; 
typedef struct vo_driver_s vo_driver_t ;
typedef struct vo_instance_s vo_instance_t;
typedef struct img_buf_fifo_s img_buf_fifo_t;

typedef struct spudec_s spudec_t;

/* public part, video drivers may add private fields */
struct vo_frame_s {
  struct vo_frame_s         *next;

  uint32_t                   PTS;
  int                        bFrameBad; /* e.g. frame skipped or based on skipped frame */
  uint8_t                   *base[3];
  int                        nType;     /* I, B or P frame */

  int                        bDisplayLock, bDecoderLock, bDriverLock;
  pthread_mutex_t            mutex; /* so the various locks will be serialized */

  int                        nID; /* debugging purposes only */

  vo_instance_t             *instance;

  /*
   * member functions
   */

  /* this frame is no longer used by decoder */
  void (*free) (vo_frame_t *vo_img);
  
  /* tell video driver to copy/convert a slice of this frame */
  void (*copy) (vo_frame_t *vo_img, uint8_t **src);

  /* tell video driver that the decoder starts a new field */
  void (*field) (vo_frame_t *vo_img, int which_field);

  /* append this frame to the display queue, 
     returns number of frames to skip if decoder is late */
  int (*draw) (vo_frame_t *vo_img);

  /* this frame is no longer used by the video driver */
  void (*displayed) (vo_frame_t *vo_img);

  /* free memory/resources for this frame */
  void (*dispose) (vo_frame_t *vo_img);
};


struct vo_instance_s {

  uint32_t (*get_capabilities) (vo_instance_t *this); /* for constants see below */

  /* open display driver for video output */
  void (*open) (vo_instance_t *this);

  /* 
   * get_frame - allocate an image buffer from display driver 
   *
   * params : width      == width of video to display.
   *          height     == height of video to display.
   *          ratio      == aspect ration information
   *          format     == FOURCC descriptor of image format
   *          duration   == frame duration in 1/90000 sec
   */
  vo_frame_t* (*get_frame) (vo_instance_t *this, uint32_t width, 
			    uint32_t height, int ratio_code, 
			    int format, uint32_t duration);

  /* video driver is no longer used by decoder => close */
  void (*close) (vo_instance_t *this);

  /* called on xine exit */
  void (*exit) (vo_instance_t *this);

  /* private stuff */

  vo_driver_t       *driver;
  metronom_t        *metronom;
  spudec_t          *spu_decoder;
  
  img_buf_fifo_t    *free_img_buf_queue;
  img_buf_fifo_t    *display_img_buf_queue;

  int                video_loop_running;
  pthread_t          video_thread;

  int                pts_per_half_frame;
  int                pts_per_frame;

  int                num_frames_delivered;
  int                num_frames_skipped;
  int                num_frames_discarded;

} ;

/* constants for the get/set property functions */

#define VO_PROP_INTERLACED      0
#define VO_PROP_ASPECT_RATIO    1
#define VO_PROP_HUE             2
#define VO_PROP_SATURATION      3
#define VO_PROP_CONTRAST        4
#define VO_PROP_BRIGHTNESS      5 
#define VO_PROP_COLORKEY        6 
#define VO_NUM_PROPERTIES       7

/* image formats that can be supported by display drivers: */

#define IMGFMT_YV12 0x32315659
#define IMGFMT_YUY2 (('2'<<24)|('Y'<<16)|('U'<<8)|'Y')
#define IMGFMT_RGB  (('R'<<24)|('G'<<16)|('B'<<8))

/* possible ratios for the VO_PROP_ASPECT_RATIO call */

#define ASPECT_AUTO        0
#define ASPECT_ANAMORPHIC  1 /* 16:9 */
#define ASPECT_FULL        2 /* 4:3  */
#define ASPECT_DVB         3 /* 1:2  */

/* video driver capabilities */

/* driver copies image (i.e. converts it to 
   rgb buffers in the private fields of image buffer) */
#define VO_CAP_COPIES_IMAGE 0x00000001

#define VO_CAP_RGB          0x00000002 /* driver can handle 24bit rgb pictures */
#define VO_CAP_YV12         0x00000004 /* driver can handle YUV 4:2:0 pictures */
#define VO_CAP_YUY2         0x00000008 /* driver can handle YUY2      pictures */

#define VO_CAP_HUE          0x00000010 /* driver can set HUE value             */
#define VO_CAP_SATURATION   0x00000020 /* driver can set SATURATION value      */
#define VO_CAP_BRIGHTNESS   0x00000040 /* driver can set BRIGHTNESS value      */
#define VO_CAP_CONTRAST     0x00000080 /* driver can set CONTRAST value        */
#define VO_CAP_COLORKEY     0x00000100 /* driver can set COLORKEY value        */

/*
 * vo_driver_s contains the functions every display driver
 * has to implement. The vo_new_instance function (see below)
 * should then be used to construct a vo_instance using this
 * driver. Some of the function pointers will be copied
 * directly into vo_instance_s, others will be called
 * from generic vo functions.
 */

#define VIDEO_OUT_IFACE_VERSION 1

struct vo_driver_s {

  uint32_t (*get_capabilities) (vo_driver_t *this); /* for constants see above */

  /* 
   * allocate an vo_frame_t struct,
   * the driver must supply the copy, field and dispose functions
   */
  vo_frame_t* (*alloc_frame) (vo_driver_t *this);


  /* 
   * check if the given image fullfills the format specified
   * (re-)allocate memory if necessary
   */
  void (*update_frame_format) (vo_driver_t *this, vo_frame_t *img,
			       uint32_t width, uint32_t height, 
			       int ratio_code, int format);

  /* display a given frame */
  void (*display_frame) (vo_driver_t *this, vo_frame_t *vo_img);

  /*
   * these can be used by the gui directly:
   */
  
  int (*get_property) (vo_driver_t *this, int property);
  int (*set_property) (vo_driver_t *this, 
		       int property, int value);
  void (*get_property_min_max) (vo_driver_t *this,
				int property, int *min, int *max);

  /*
   * general purpose communication channel between gui and driver
   *
   * this should be used to propagate events, display data, window sizes
   * etc. to the driver
   */

  int (*gui_data_exchange) (vo_driver_t *this, int data_type,
			    void *data);

  void (*exit) (vo_driver_t *this);

};


/*
 * build a video_out_instance from
 * a given video driver
 */

vo_instance_t *vo_new_instance (vo_driver_t *driver, metronom_t *metronom, spudec_t *spu_decoder) ;

/*
 * to build a dynamic video output plugin
 * you have to implement these functions:
 *
 *
 * vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual);
 *
 * init and set up driver so it is fully operational
 * 
 * parameters: config      - config object pointer
 *             visual      - driver specific info (e.g. Display*)
 *
 * return value: video_driver_t* in case of success,
 *               NULL            on failure (e.g. wrong interface version, 
 *                               wrong visual type...)
 *
 *
 *
 * vo_info_t *get_video_out_plugin_info ();
 *
 * peek at some (static) information about the plugin without initializing it
 *
 * parameters: none
 *
 * return value: vo_info_t* : some information about the plugin
 */

typedef struct vo_info_s {
   
  int    interface_version; /* plugin interface version                  */
  char  *id;                /* id of this plugin                         */
  char  *description;       /* human-readable description of this plugin */
  int    visual_type;       /* visual type supported by this plugin      */
  int    priority;          /* priority of this plugin for auto-probing  */

} vo_info_t;

#endif

