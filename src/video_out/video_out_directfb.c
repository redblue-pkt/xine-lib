/*
 * Copyright (C) 2000, 2001 the xine project
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
 * $Id: video_out_directfb.c,v 1.9 2002/06/12 12:22:38 f1rmb Exp $
 *
 * DirectFB based output plugin.
 * Rich Wareham <richwareham@users.sourceforge.net>
 * 
 * Based on video_out_xshm.c and video_out_xv.c
 */

#if 0
#  define DEBUGF(x) fprintf x
#else
#  define DEBUGF(x) ((void) 0)
#endif

/* Uncomment for some fun! */
/*
#define SPHERE
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "video_out.h"

#include <pthread.h>
#include <netinet/in.h>

#include "xine_internal.h"
#include "alphablend.h"
#include "yuv2rgb.h"
#include "xineutils.h"
#include <directfb.h>

#define DFBCHECK(x...)                                         \
  {                                                            \
    DFBResult err = x;                                         \
							       \
    if (err != DFB_OK)                                         \
      {                                                        \
	fprintf( stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
	DirectFBErrorFatal( #x, err );                         \
      }                                                        \
  }

typedef struct directfb_frame_s {
  vo_frame_t         vo_frame;

  int                width, height;
  int                ratio_code;
  int                format;
  int                locked;

  IDirectFBSurface  *surface;
} directfb_frame_t;

typedef struct directfb_driver_s {

  vo_driver_t      vo_driver;

  config_values_t *config;

  directfb_frame_t  *cur_frame;
  vo_overlay_t    *overlay;

  /* DirectFB related stuff */
  IDirectFB        *dfb;
  IDirectFBDisplayLayer *layer;

  /* output area */
  int              frame_width;
  int              frame_height;
  
  /* last displayed frame */
  int              last_frame_width;     /* original size */
  int              last_frame_height;    /* original size */
  int              last_frame_ratio_code;

  /* display anatomy */
  double           display_ratio;        /* given by visual parameter from init function */
  void            *user_data;

  /* gui callbacks */

  void (*request_dest_size) (void *user_data,
			     int video_width, int video_height,
			     int *dest_x, int *dest_y, 
			     int *dest_height, int *dest_width);

  void (*calc_dest_size) (void *user_data,
			  int video_width, int video_height, 
			  int *dest_width, int *dest_height);

} directfb_driver_t;

#define CONTEXT_BAD             0
#define CONTEXT_SAME_DRAWABLE   1
#define CONTEXT_SET             2
#define CONTEXT_RELOAD          3


/*
 * first, some utility functions
 */
static void *my_malloc_aligned (size_t alignment, size_t size, uint8_t **chunk) {

  uint8_t *pMem;

  pMem = xine_xmalloc (size+alignment);

  *chunk = pMem;

  while ((int) pMem % alignment)
    pMem++;

  return pMem;
}


/*
 * and now, the driver functions
 */

static uint32_t directfb_get_capabilities (vo_driver_t *this_gen) {
  return VO_CAP_YV12 | VO_CAP_YUY2;
}

static void directfb_frame_field (vo_frame_t *vo_img, int which_field) {

  /* directfb_frame_t  *frame = (directfb_frame_t *) vo_img ; */

  switch(which_field) {
    case VO_BOTH_FIELDS:
     break;
    default:
     fprintf(stderr, "Interlaced images not supported\n");
  }
#if 0
  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = frame->texture;
    frame->stripe_inc = 2*STRIPE_HEIGHT * frame->width * BYTES_PER_PIXEL;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = frame->texture + frame->width * BYTES_PER_PIXEL ;
    frame->stripe_inc = 2*STRIPE_HEIGHT * frame->width * BYTES_PER_PIXEL;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = frame->texture;
    frame->stripe_inc = STRIPE_HEIGHT * frame->width * BYTES_PER_PIXEL;
    break;
  }
#endif
}

static void directfb_frame_dispose (vo_frame_t *vo_img) {

  directfb_frame_t  *frame = (directfb_frame_t *) vo_img ;

  if (frame) {
    if(frame->locked) 
     DFBCHECK(frame->surface->Unlock(frame->surface));
    DFBCHECK(frame->surface->Release(frame->surface));
    frame->surface = NULL;
  }
  free (frame);
}


static vo_frame_t *directfb_alloc_frame (vo_driver_t *this_gen) {
  directfb_frame_t   *frame ;

  frame = (directfb_frame_t *) calloc (1, sizeof (directfb_frame_t));
  if (frame==NULL) {
    printf ("directfb_alloc_frame: out of memory\n");
    return NULL;
  }

  memset (frame, 0, sizeof(directfb_frame_t));

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */
  
  frame->vo_frame.copy    = NULL; 
  frame->vo_frame.field   = directfb_frame_field; 
  frame->vo_frame.dispose = directfb_frame_dispose;

  frame->surface = NULL;
  frame->locked = 0;
  
  return (vo_frame_t *) frame;
}


static void directfb_adapt_to_output_area (directfb_driver_t *this,
					 int dest_width, int dest_height)
{
  this->frame_width    = dest_width;
  this->frame_height   = dest_height;

}

static void directfb_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags) {

  directfb_driver_t  *this = (directfb_driver_t *) this_gen;
  directfb_frame_t   *frame = (directfb_frame_t *) frame_gen;
  DFBSurfaceDescription s_dsc;
  DFBDisplayLayerConfig l_dsc;
  DFBResult ret;
  DFBDisplayLayerConfigFlags   failed;
  int pitch;
  uint8_t *data;

  flags &= VO_BOTH_FIELDS;

  if ((frame->width != width) 
      || (frame->height != height)
      || (frame->format != format)) {
    if(frame->locked && frame->surface) {
      DFBCHECK(frame->surface->Unlock(frame->surface));
    }

    if(frame->surface) {
      DFBCHECK(frame->surface->Release(frame->surface));
    }

    frame->surface = NULL;
    
    frame->format = format;
    frame->width  = width;
    frame->height = height;
       
    switch(frame->format) {
     case IMGFMT_YV12:
      s_dsc.pixelformat = DSPF_YV12;
      l_dsc.pixelformat = DSPF_YV12;
      break;
     case IMGFMT_YUY2:
      s_dsc.pixelformat = DSPF_YUY2;
      l_dsc.pixelformat = DSPF_YUY2;
      break;
     default:
      fprintf(stderr,"Error unknown image format (%i), assuming YV12\n", frame->format);
      s_dsc.pixelformat = DSPF_YV12;
      l_dsc.pixelformat = DSPF_YV12;
    }
    
    s_dsc.flags = DSDESC_CAPS | DSDESC_PIXELFORMAT
     | DSDESC_WIDTH | DSDESC_HEIGHT;
    s_dsc.caps = 0;
    s_dsc.width = frame->width;
    s_dsc.height = frame->height;
    DFBCHECK(this->dfb->CreateSurface(this->dfb, &s_dsc, &(frame->surface)));

    l_dsc.flags = DLCONF_WIDTH | DLCONF_HEIGHT
     | DLCONF_PIXELFORMAT | DLCONF_OPTIONS;
    l_dsc.width = frame->width;
    l_dsc.height = frame->height;
    l_dsc.options = 0;
    ret = this->layer->TestConfiguration(this->layer, &l_dsc, &failed );
    if (ret == DFB_UNSUPPORTED) {
      fprintf(stderr, "Error: Unsupported operation\n");
      return;
    }
    DFBCHECK(this->layer->SetConfiguration(this->layer, &l_dsc));

    /* Set correct locations for planes in surface */
    DFBCHECK(frame->surface->Lock(frame->surface, DSLF_WRITE,
				  (void**)(&data), &pitch));
    memset(data, 128, 6 * width*height / 4);

    frame->locked = 1;
    switch(frame->format) {
     case IMGFMT_YV12:
      frame->vo_frame.base[0] = data;
      frame->vo_frame.base[1] = data + pitch*height;
      frame->vo_frame.base[2] = data + pitch*height + pitch*height/4;
      break;
     case IMGFMT_YUY2:
      frame->vo_frame.base[0] = data;
      frame->vo_frame.base[1] = data;
      frame->vo_frame.base[2] = data;
      break;
     default:
      break;
    }

    frame->ratio_code = ratio_code;

    directfb_frame_field ((vo_frame_t *)frame, flags);
  }
}

static void directfb_overlay_clut_yuv2rgb(directfb_driver_t  *this, vo_overlay_t *overlay)
{
#if 0
  int i;
  clut_t* clut = (clut_t*) overlay->color;
  if (!overlay->rgb_clut) {
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
                   this->yuv2rgb->yuv2rgb_single_pixel_fun(this->yuv2rgb,
                   clut[i].y, clut[i].cb, clut[i].cr);
    }
  overlay->rgb_clut++;
  }
  if (!overlay->clip_rgb_clut) {
    clut = (clut_t*) overlay->clip_color;
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
                   this->yuv2rgb->yuv2rgb_single_pixel_fun(this->yuv2rgb,
                   clut[i].y, clut[i].cb, clut[i].cr);
    }
  overlay->clip_rgb_clut++;
  }
#endif
  
}

static void directfb_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  /* directfb_driver_t  *this = (directfb_driver_t *) this_gen; */
  /* directfb_frame_t   *frame = (directfb_frame_t *) frame_gen; */

#if 0
  /* Alpha Blend here */
  if (overlay->rle) {
    if( !overlay->rgb_clut || !overlay->clip_rgb_clut)
      directfb_overlay_clut_yuv2rgb(this,overlay);

    assert (this->delivered_width == frame->width);
    assert (this->delivered_height == frame->height);
#   if BYTES_PER_PIXEL == 3
      blend_rgb24 ((uint8_t *)frame->texture, overlay,
                   frame->width, frame->height,
                   this->delivered_width, this->delivered_height);
#   elif BYTES_PER_PIXEL == 4
      blend_rgb32 ((uint8_t *)frame->texture, overlay,
                   frame->width, frame->height,
                   this->delivered_width, this->delivered_height);
#   else
#     error "bad BYTES_PER_PIXEL"
#   endif
  }
#endif
}

static void directfb_render_image (directfb_driver_t *this, directfb_frame_t *frame)
{
  if(frame && frame->surface) {
    IDirectFBSurface *surface;

    if(frame->locked) {
      frame->surface->Unlock(frame->surface);
      frame->locked = 0;
    }

    DFBCHECK(this->layer->GetSurface(this->layer, &surface));
    DFBCHECK(surface->Blit(surface,
		     	   frame->surface, NULL,0,0));
    /* DFBCHECK(surface->Flip(surface, NULL, 0)); */
  }
}


static void directfb_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  directfb_driver_t  *this = (directfb_driver_t *) this_gen;
  directfb_frame_t   *frame = (directfb_frame_t *) frame_gen;

  if( this->cur_frame )
    this->cur_frame->vo_frame.displayed (&this->cur_frame->vo_frame);
  this->cur_frame = frame;

  if ( (frame->width      != this->last_frame_width)  ||
       (frame->height     != this->last_frame_height))
  {
    this->last_frame_width      = frame->width;
    this->last_frame_height     = frame->height;
    this->last_frame_ratio_code = frame->ratio_code;

    fprintf (stderr, "video_out_directfb: frame size %d x %d\n",
            this->frame_width, this->frame_height);
  }

  directfb_render_image (this, frame);

  frame->vo_frame.displayed (&frame->vo_frame);
  this->cur_frame = NULL;
}


static int directfb_get_property (vo_driver_t *this_gen, int property) {

  /* directfb_driver_t *this = (directfb_driver_t *) this_gen; */

  if ( property == VO_PROP_ASPECT_RATIO) {
    return 1;
  } else {
    printf ("video_out_directfb: tried to get unsupported property %d\n", property);
  }

  return 0;
}


static char *aspect_ratio_name(int a)
{
  switch (a) {
  case ASPECT_AUTO:
    return "auto";
  case ASPECT_SQUARE:
    return "square";
  case ASPECT_FULL:
    return "4:3";
  case ASPECT_ANAMORPHIC:
    return "16:9";
  case ASPECT_DVB:
    return "2:1";
  default:
    return "unknown";
  }
}

static int directfb_set_property (vo_driver_t *this_gen, 
			      int property, int value) {

  /* directfb_driver_t *this = (directfb_driver_t *) this_gen; */

  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>=NUM_ASPECT_RATIOS)
      value = ASPECT_AUTO;
    /* this->user_ratio = value; */
    printf("video_out_directfb: aspect ratio changed to %s\n",
	   aspect_ratio_name(value));
  } else {
    printf ("video_out_directfb: tried to set unsupported property %d\n", property);
  }

  return value;
}

static void directfb_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {
    *min = 0;
    *max = 0;
}


static int is_fullscreen_size (directfb_driver_t *this, int w, int h) {
  /* Always fullscreen*/
  return 1;
}

static void directfb_translate_gui2video(directfb_driver_t *this,
				     int x, int y,
				     int *vid_x, int *vid_y)
{
  *vid_x = x;
  *vid_y = y;
}

static int directfb_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data) {

  /* directfb_driver_t   *this = (directfb_driver_t *) this_gen; */


  switch (data_type) {
  default:
    return -1;
  }

fprintf (stderr, "done gui_data_exchange\n"); 
  return 0;
}


static void directfb_exit (vo_driver_t *this_gen) {
  /* directfb_driver_t *this = (directfb_driver_t *) this_gen; */
}

typedef struct {
  IDirectFB *dfb;
  IDirectFBDisplayLayer *video_layer;
} dfb_visual_info_t;

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  directfb_driver_t      *this;
  dfb_visual_info_t      *visual_info = (dfb_visual_info_t*)visual_gen;

  fprintf (stderr, "EXPERIMENTAL directfb output plugin\n");
  /*
   * allocate plugin struct
   */

  this = malloc (sizeof (directfb_driver_t));

  if (!this) {
    printf ("video_out_directfb: malloc failed\n");
    return NULL;
  }

  memset (this, 0, sizeof(directfb_driver_t));

  this->config		    = config;
  this->frame_width	    = 0;
  this->frame_height	    = 0;

  this->vo_driver.get_capabilities     = directfb_get_capabilities;
  this->vo_driver.alloc_frame          = directfb_alloc_frame;
  this->vo_driver.update_frame_format  = directfb_update_frame_format;
  this->vo_driver.overlay_blend        = directfb_overlay_blend;
  this->vo_driver.display_frame        = directfb_display_frame;
  this->vo_driver.get_property         = directfb_get_property;
  this->vo_driver.set_property         = directfb_set_property;
  this->vo_driver.get_property_min_max = directfb_get_property_min_max;
  this->vo_driver.gui_data_exchange    = directfb_gui_data_exchange;
  this->vo_driver.exit                 = directfb_exit;

  this->dfb = visual_info->dfb;
  this->layer = visual_info->video_layer;
  DFBCHECK(this->layer->SetCooperativeLevel(this->layer,
					    DLSCL_ADMINISTRATIVE));

  return &this->vo_driver;
}

static vo_info_t vo_info_directfb = {
  4,
  "DirectFB",
  NULL,
  VISUAL_TYPE_DFB,
  8
};

vo_info_t *get_video_out_plugin_info() {
  vo_info_directfb.description = _("xine video output plugin using the DirectFB library.");
  return &vo_info_directfb;
}

