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
 * $Id: video_out_fb.c,v 1.3 2002/01/15 20:39:39 jcdutton Exp $
 * 
 * video_out_fb.c, frame buffer xine driver by Miguel Freitas
 *
 * based on xine's video_out_xshm.c...
 * ...based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * ideas from ppmtofb - Display P?M graphics on framebuffer devices
 *            by Geert Uytterhoeven and Chris Lawrence
 *
 * TODO: VT switching (configurable)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> 
             
#include "video_out.h"

#include <errno.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <pthread.h>
#include <netinet/in.h>

#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>

#include "xine_internal.h"
#include "alphablend.h"
#include "yuv2rgb.h"
#include "xineutils.h"

typedef struct fb_frame_s {
  vo_frame_t         vo_frame;

  int                width, height;
  int                rgb_width, rgb_height;

  uint8_t           *rgb_dst;
  int                stripe_inc;

  int                format;

  int                bytes_per_line;
  uint8_t           *data;
} fb_frame_t;

typedef struct fb_driver_s {

  vo_driver_t      vo_driver;

  config_values_t *config;

  int		   fd;
  int              mem_size;
  uint8_t*         video_mem;       /* mmapped video memory */
  
  int              zoom_mpeg1;
  int		   scaling_disabled;
  int              depth, bpp, bytes_per_pixel;
  int              expecting_event;
  uint8_t	  *fast_rgb;

  yuv2rgb_t       *yuv2rgb;

  vo_overlay_t    *overlay;

  /* size / aspect ratio calculations */
  int              delivered_width;      /* everything is set up for these frame dimensions    */
  int              delivered_height;     /* the dimension as they come from the decoder        */
  int              delivered_ratio_code;
  int              delivered_flags;
  double           ratio_factor;	 /* output frame must fulfill: height = width * ratio_factor  */
  double	   output_scale_factor;	 /* additional scale factor for the output frame */
  int              output_width;         /* frames will appear in this size (pixels) on screen */
  int              output_height;
  int              stripe_height;
  int              yuv_width;            /* width/height yuv2rgb is configured for */
  int              yuv_height;
  int              yuv_stride;

  int              user_ratio;

  int		   last_frame_rgb_width; /* size of scaled rgb output img gui */
  int		   last_frame_rgb_height; /* has most recently adopted to */

  int              gui_width;		 /* size of gui window */
  int              gui_height;
  int              gui_changed;
  int              gui_linelength;

  /* display anatomy */
  double           display_ratio;        /* given by visual parameter from init function */

  /* profiler */
  int		   prof_yuv2rgb;

} fb_driver_t;

/* possible values for fb_driver_t, field gui_changed */
#define	GUI_SIZE_CHANGED	1
#define	GUI_ASPECT_CHANGED	2


/*
 * first, some utility functions
 */
vo_info_t *get_video_out_plugin_info();


/*
 * and now, the driver functions
 */

static uint32_t fb_get_capabilities (vo_driver_t *this_gen) {
  return VO_CAP_COPIES_IMAGE | VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_BRIGHTNESS;
}

static void fb_frame_copy (vo_frame_t *vo_img, uint8_t **src) {
  fb_frame_t  *frame = (fb_frame_t *) vo_img ;
  fb_driver_t *this = (fb_driver_t *) vo_img->instance->driver;

  xine_profiler_start_count (this->prof_yuv2rgb);

  if (frame->format == IMGFMT_YV12) {
    this->yuv2rgb->yuv2rgb_fun (this->yuv2rgb, frame->rgb_dst,
				src[0], src[1], src[2]);
  } else {

    this->yuv2rgb->yuy22rgb_fun (this->yuv2rgb, frame->rgb_dst,
				 src[0]);
				 
  }
  
  xine_profiler_stop_count (this->prof_yuv2rgb);

  frame->rgb_dst += frame->stripe_inc; 
}

static void fb_frame_field (vo_frame_t *vo_img, int which_field) {

  fb_frame_t  *frame = (fb_frame_t *) vo_img ;
  fb_driver_t *this = (fb_driver_t *) vo_img->instance->driver;

  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->data;
    frame->stripe_inc = 2*this->stripe_height * frame->bytes_per_line;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->data + frame->bytes_per_line ;
    frame->stripe_inc = 2*this->stripe_height * frame->bytes_per_line;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = (uint8_t *)frame->data;
    break;
  }
}

static void fb_frame_dispose (vo_frame_t *vo_img) {

  fb_frame_t  *frame = (fb_frame_t *) vo_img ;
  /* fb_driver_t *this = (fb_driver_t *) vo_img->instance->driver; */

  if (frame->data) {
     free(frame->data);
  }

  free (frame);
}


static vo_frame_t *fb_alloc_frame (vo_driver_t *this_gen) {
  fb_frame_t   *frame ;

  frame = (fb_frame_t *) malloc (sizeof (fb_frame_t));
  if (frame==NULL) {
    printf ("fb_alloc_frame: out of memory\n");
    return NULL;
  }

  memset (frame, 0, sizeof(fb_frame_t));

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */
  
  frame->vo_frame.copy    = fb_frame_copy;
  frame->vo_frame.field   = fb_frame_field; 
  frame->vo_frame.dispose = fb_frame_dispose;
  
  return (vo_frame_t *) frame;
}

static void fb_calc_output_size (fb_driver_t *this) {

  double image_ratio, desired_ratio;
  double corr_factor, x_factor, y_factor;
  int ideal_width, ideal_height;
  int dest_width, dest_height;

  /*
   * aspect ratio calculation
   */

  if (this->delivered_width == 0 && this->delivered_height == 0)
    return; /* ConfigureNotify/VisibilityNotify, no decoder output size known */

  if (this->scaling_disabled) {
    /* quick hack to allow testing of unscaled yuv2rgb conversion routines */
    this->output_width   = this->delivered_width;
    this->output_height  = this->delivered_height;
    this->ratio_factor   = 1.0;

    
    /*
    this->calc_dest_size (this->user_data,
			  this->output_width, this->output_height,
			  &dest_width, &dest_height);
    */
   
  } else {

    image_ratio =
	(double) this->delivered_width / (double) this->delivered_height;

    switch (this->user_ratio) {
    case ASPECT_AUTO:
      switch (this->delivered_ratio_code) {
      case XINE_ASPECT_RATIO_ANAMORPHIC:  /* anamorphic     */
	desired_ratio = 16.0 /9.0;
	break;
      case XINE_ASPECT_RATIO_211_1:       /* 2.11:1 */
	desired_ratio = 2.11/1.0;
	break;
      case XINE_ASPECT_RATIO_SQUARE:      /* square pels */
      case XINE_ASPECT_RATIO_DONT_TOUCH:  /* probably non-mpeg stream => don't touch aspect ratio */
	desired_ratio = image_ratio;
	break;
      case 0:                             /* forbidden -> 4:3 */
	printf ("video_out_fb: invalid ratio, using 4:3\n");
      default:
	printf ("video_out_fb: unknown aspect ratio (%d) in stream => using 4:3\n",
		this->delivered_ratio_code);
      case XINE_ASPECT_RATIO_4_3:         /* 4:3             */
	desired_ratio = 4.0 / 3.0;
	break;
      }
      break;
    case ASPECT_ANAMORPHIC:
      desired_ratio = 16.0 / 9.0;
      break;
    case ASPECT_DVB:
      desired_ratio = 2.0 / 1.0;
      break;
    case ASPECT_SQUARE:
      desired_ratio = image_ratio;
      break;
    case ASPECT_FULL:
    default:
      desired_ratio = 4.0 / 3.0;
    }

    this->ratio_factor = this->display_ratio * desired_ratio;

    /*
     * calc ideal output frame size
     */

    corr_factor = this->ratio_factor / image_ratio ;

    if (fabs(corr_factor - 1.0) < 0.005) {
      ideal_width  = this->delivered_width;
      ideal_height = this->delivered_height;
    }
    else if (corr_factor >= 1.0) {
      ideal_width  = this->delivered_width * corr_factor + 0.5;
      ideal_height = this->delivered_height;
    }
    else {
      ideal_width  = this->delivered_width;
      ideal_height = this->delivered_height / corr_factor + 0.5;
    }

    /* little hack to zoom mpeg1 / other small streams  by default*/
    if ( this->zoom_mpeg1 && (this->delivered_width<400)) {
      ideal_width  *= 2;
      ideal_height *= 2;
    }

    if (fabs(this->output_scale_factor - 1.0) > 0.005) {
      ideal_width  *= this->output_scale_factor;
      ideal_height *= this->output_scale_factor;
    }

    /* yuv2rgb_mmx prefers "width%8 == 0" */
    /* but don't change if it would introduce scaling */
    if( ideal_width != this->delivered_width ||
        ideal_height != this->delivered_height )
      ideal_width &= ~7;
    
    /*
    this->calc_dest_size (this->user_data,
			  ideal_width, ideal_height,
			  &dest_width, &dest_height);
    */
    dest_width = this->gui_width;
    dest_height = this->gui_width;

    /*
     * make the frames fit into the given destination area
     */

    x_factor = (double) dest_width  / (double) ideal_width;
    y_factor = (double) dest_height / (double) ideal_height;

    if ( x_factor < y_factor ) {
      this->output_width   = (double) ideal_width  * x_factor ;
      this->output_height  = (double) ideal_height * x_factor ;
    } else {
      this->output_width   = (double) ideal_width  * y_factor ;
      this->output_height  = (double) ideal_height * y_factor ;
    }

  }

  printf("video_out_fb: "
	 "frame source %d x %d => screen output %d x %d%s\n",
	 this->delivered_width, this->delivered_height,
	 this->output_width,    this->output_height,
	 ( this->delivered_width != this->output_width
	   || this->delivered_height != this->output_height
	   ? ", software scaling"
	   : "" )
	 );
}

static void fb_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags) {

  fb_driver_t  *this = (fb_driver_t *) this_gen;
  fb_frame_t   *frame = (fb_frame_t *) frame_gen;
  int setup_yuv = 0;

  flags &= VO_BOTH_FIELDS;

  if ((width != this->delivered_width)
      || (height != this->delivered_height)
      || (ratio_code != this->delivered_ratio_code)
      || (flags != this->delivered_flags)
      || this->gui_changed) {

    this->delivered_width      = width;
    this->delivered_height     = height;
    this->delivered_ratio_code = ratio_code;
    this->delivered_flags      = flags;
    this->gui_changed	       = 0;
    
    fb_calc_output_size (this);

    setup_yuv = 1;
  }

  if ((frame->rgb_width != this->output_width) 
      || (frame->rgb_height != this->output_height)
      || (frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

    int image_size;

    /*
    printf ("video_out_fb: updating frame to %d x %d\n",
	    this->output_width,this->output_height);
    */

    /*
     * (re-) allocate 
     */

    if (frame->data) {
      if( frame->vo_frame.base[0] ) {
        xine_free_aligned( frame->vo_frame.base[0] );
        frame->vo_frame.base[0] = NULL;
      }
      if( frame->vo_frame.base[1] ) {
        xine_free_aligned( frame->vo_frame.base[1] );
        frame->vo_frame.base[1] = NULL;
      }
      if( frame->vo_frame.base[2] ) {
        xine_free_aligned( frame->vo_frame.base[2] );
        frame->vo_frame.base[2] = NULL;
      }
      
      free (frame->data);
    }


    frame->data = xine_xmalloc (this->output_width * this->output_height *
                                this->bytes_per_pixel );
    
    if (format == IMGFMT_YV12) {
      image_size = width * height;
      frame->vo_frame.base[0] = xine_xmalloc_aligned(16,image_size);
      frame->vo_frame.base[1] = xine_xmalloc_aligned(16,image_size/4);
      frame->vo_frame.base[2] = xine_xmalloc_aligned(16,image_size/4);
    } else {
      image_size = width * height;
      frame->vo_frame.base[0] = xine_xmalloc_aligned(16,image_size*2);
    }
    
    frame->format = format;
    frame->width  = width;
    frame->height = height;

    frame->rgb_width  = this->output_width;
    frame->rgb_height = this->output_height;
    
    frame->bytes_per_line = frame->rgb_width * this->bytes_per_pixel;
  }

  if (frame->data) {
    this->stripe_height = 16 * this->output_height / this->delivered_height;

    frame->rgb_dst    = (uint8_t *)frame->data;
    switch (flags) {
    case VO_TOP_FIELD:
      frame->rgb_dst    = (uint8_t *)frame->data;
      frame->stripe_inc = 2 * this->stripe_height * frame->bytes_per_line;
      break;
    case VO_BOTTOM_FIELD:
      frame->rgb_dst    = (uint8_t *)frame->data + frame->bytes_per_line ;
      frame->stripe_inc = 2 * this->stripe_height * frame->bytes_per_line;
      break;
    case VO_BOTH_FIELDS:
      frame->rgb_dst    = (uint8_t *)frame->data;
      frame->stripe_inc = this->stripe_height * frame->bytes_per_line;
      break;
    }

    if (flags == VO_BOTH_FIELDS) {
      if (this->yuv_stride != frame->bytes_per_line)
	setup_yuv = 1;
    } else {	/* VO_TOP_FIELD, VO_BOTTOM_FIELD */
      if (this->yuv_stride != (frame->bytes_per_line*2))
	setup_yuv = 1;
    }

    if (setup_yuv 
	|| (this->yuv_height != this->stripe_height) 
	|| (this->yuv_width != this->output_width)) {
      switch (flags) {
      case VO_TOP_FIELD:
      case VO_BOTTOM_FIELD:
	yuv2rgb_setup (this->yuv2rgb,
		       this->delivered_width,
		       16,
		       this->delivered_width*2,
		       this->delivered_width,
		       this->output_width,
		       this->stripe_height,
		       frame->bytes_per_line*2);
	this->yuv_stride = frame->bytes_per_line*2;
	break;
      case VO_BOTH_FIELDS:
	yuv2rgb_setup (this->yuv2rgb,
		       this->delivered_width,
		       16,
		       this->delivered_width,
		       this->delivered_width/2,
		       this->output_width,
		       this->stripe_height,
		       frame->bytes_per_line);
	this->yuv_stride = frame->bytes_per_line;
	break;
      }
      this->yuv_height = this->stripe_height;
      this->yuv_width  = this->output_width;
    }
  }
}

static void fb_overlay_clut_yuv2rgb(fb_driver_t  *this, vo_overlay_t *overlay)
{
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
}

static void fb_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  fb_driver_t  *this = (fb_driver_t *) this_gen;
  fb_frame_t   *frame = (fb_frame_t *) frame_gen;

  /* Alpha Blend here */
   if (overlay->rle) {
     if( !overlay->rgb_clut || !overlay->clip_rgb_clut)
       fb_overlay_clut_yuv2rgb(this,overlay);

     switch(this->bpp) {
       case 16:
        blend_rgb16( (uint8_t *)frame->data, overlay,
		     frame->rgb_width, frame->rgb_height,
		     this->delivered_width, this->delivered_height);
        break;
       case 24:
        blend_rgb24( (uint8_t *)frame->data, overlay,
		     frame->rgb_width, frame->rgb_height,
		     this->delivered_width, this->delivered_height);
        break;
       case 32:
        blend_rgb32( (uint8_t *)frame->data, overlay,
		     frame->rgb_width, frame->rgb_height,
		     this->delivered_width, this->delivered_height);
        break;
       default:
	/* It should never get here */
	break;
     }        
   }
}

static void fb_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  fb_driver_t  *this = (fb_driver_t *) this_gen;
  fb_frame_t   *frame = (fb_frame_t *) frame_gen;
  int		  xoffset;
  int		  yoffset;
  int             y;
  uint8_t	*dst, *src;

  if ( (frame->rgb_width != this->last_frame_rgb_width)
       || (frame->rgb_height != this->last_frame_rgb_height) ) {

    /*
    xprintf (VIDEO, "video_out_fb: requesting dest size of %d x %d \n",
             frame->rgb_width, frame->rgb_height);
    */
    /*
    this->request_dest_size (this->user_data,
      		       frame->rgb_width, frame->rgb_height, 
      		       &this->dest_x, &this->dest_y, 
      		       &this->gui_width, &this->gui_height);
    */
    
    this->last_frame_rgb_width    = frame->rgb_width;
    this->last_frame_rgb_height   = frame->rgb_height;

    printf ("video_out_fb: gui size %d x %d, frame size %d x %d\n",
            this->gui_width, this->gui_height,
            frame->rgb_width, frame->rgb_height);
    
    memset(this->video_mem, 0, this->gui_linelength * this->gui_height );

  }
    
  xoffset  = (this->gui_width - frame->rgb_width) / 2;
  yoffset  = (this->gui_height - frame->rgb_height) / 2;

  dst = this->video_mem + yoffset * this->gui_linelength +
        xoffset * this->bytes_per_pixel;
  src = frame->data;
   
  for( y = 0; y < frame->rgb_height; y++ ) {
    xine_fast_memcpy( dst, src, frame->bytes_per_line );
    src += frame->bytes_per_line;
    dst += this->gui_linelength;
  } 
  
  frame->vo_frame.displayed (&frame->vo_frame);
}

static int fb_get_property (vo_driver_t *this_gen, int property) {

  fb_driver_t *this = (fb_driver_t *) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    return this->user_ratio ;
  } else if ( property == VO_PROP_BRIGHTNESS) {
    return yuv2rgb_get_gamma(this->yuv2rgb);
  } else {
    printf ("video_out_fb: tried to get unsupported property %d\n", property);
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

static int fb_set_property (vo_driver_t *this_gen, 
			      int property, int value) {

  fb_driver_t *this = (fb_driver_t *) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>=NUM_ASPECT_RATIOS)
      value = ASPECT_AUTO;
    this->user_ratio = value;
    this->gui_changed |= GUI_ASPECT_CHANGED;
    printf("video_out_fb: aspect ratio changed to %s\n",
	   aspect_ratio_name(value));
  } else if ( property == VO_PROP_BRIGHTNESS) {
    yuv2rgb_set_gamma(this->yuv2rgb,value);

    printf("video_out_fb: gamma changed to %d\n",value);
  } else {
    printf ("video_out_fb: tried to set unsupported property %d\n", property);
  }

  return value;
}

static void fb_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

  /* fb_driver_t *this = (fb_driver_t *) this_gen;  */
  if ( property == VO_PROP_BRIGHTNESS) {
    *min = -100;
    *max = +100;
  } else {
    *min = 0;
    *max = 0;
  }
}


static int is_fullscreen_size (fb_driver_t *this, int w, int h)
{
/*  return w == DisplayWidth(this->display, this->screen)
      && h == DisplayHeight(this->display, this->screen); */
    return 0;
}

static int fb_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data) {

  return 0;
}


static void fb_exit (vo_driver_t *this_gen) {

  fb_driver_t *this = (fb_driver_t *) this_gen;
  
  munmap(0, this->mem_size);   
   
  close(this->fd);
}

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  fb_driver_t        *this;
  int                mode;
  char               *device_name;
  
  struct fb_fix_screeninfo fix;
  struct fb_var_screeninfo var;

  /*
   * allocate plugin struct
   */

  this = malloc (sizeof (fb_driver_t));

  if (!this) {
    printf ("video_out_fb: malloc failed\n");
    return NULL;
  }

  memset (this, 0, sizeof(fb_driver_t));

  this->config		    = config;
  this->output_width	    = 0;
  this->output_height	    = 0;
  this->output_scale_factor = 1.0;
  this->zoom_mpeg1	    = config->register_bool (config, "video.zoom_mpeg1", 1,
						     "Zoom small video formats to double size",
						     NULL, NULL, NULL);
  /*
   * FIXME: replace getenv() with config->lookup_int, merge with zoom_mpeg1?
   *
   * this->video_scale = config->lookup_int (config, "video_scale", 2);
   *  0: disable all scaling (including aspect ratio switching, ...)
   *  1: enable aspect ratio switch
   *  2: like 1, double the size for small videos
   */
  this->scaling_disabled    = getenv("VIDEO_OUT_NOSCALE") != NULL;

  this->prof_yuv2rgb	    = xine_profiler_allocate_slot ("fb yuv2rgb convert");

  this->vo_driver.get_capabilities     = fb_get_capabilities;
  this->vo_driver.alloc_frame          = fb_alloc_frame;
  this->vo_driver.update_frame_format  = fb_update_frame_format;
  this->vo_driver.overlay_blend        = fb_overlay_blend;
  this->vo_driver.display_frame        = fb_display_frame;
  this->vo_driver.get_property         = fb_get_property;
  this->vo_driver.set_property         = fb_set_property;
  this->vo_driver.get_property_min_max = fb_get_property_min_max;
  this->vo_driver.gui_data_exchange    = fb_gui_data_exchange;
  this->vo_driver.exit                 = fb_exit;
  this->vo_driver.get_info             = get_video_out_plugin_info;

  device_name = config->register_string (config, "video.fb_device", "/dev/fb0",
					  "framebuffer device", NULL, NULL, NULL);

  if( (this->fd = open(device_name, O_RDWR) ) < 0) {
    printf("video_out_fb: aborting. (unable to open device \"%s\")\n", device_name);
    free(this);
    return NULL;
  }


  if (ioctl(this->fd, FBIOGET_VSCREENINFO, &var)) {
    printf("video_out_fb: ioctl FBIOGET_VSCREENINFO: %s\n", 
           strerror(errno));
    free(this);
    return NULL;
  }
  
  var.xres_virtual = var.xres;
  var.yres_virtual = var.yres;
  var.xoffset = 0;
  var.yoffset = 0;
  var.nonstd = 0;
  var.vmode &= ~FB_VMODE_YWRAP;
  
  if (ioctl(this->fd, FBIOPUT_VSCREENINFO, &var)) {
    printf("video_out_fb: ioctl FBIOPUT_VSCREENINFO: %s\n", 
           strerror(errno));
    free(this);
    return NULL;
  }

  if (ioctl(this->fd, FBIOGET_FSCREENINFO, &fix)) {
    printf("video_out_fb: ioctl FBIOGET_FSCREENINFO: %s\n", 
           strerror(errno));
    free(this);
    return NULL;
  }
  
  if( fix.visual != FB_VISUAL_TRUECOLOR || fix.type != FB_TYPE_PACKED_PIXELS ) {
    printf("video_out_fb: only packed truecolor is supported.\n");
    printf("              check 'fbset -i' or try 'fbset -depth 16'\n");
    free(this);
    return NULL;
  }
  
  if (fix.line_length)
    this->gui_linelength = fix.line_length;
  else
    this->gui_linelength = (var.xres_virtual * var.bits_per_pixel) / 8; 
    
  this->gui_width   = var.xres;
  this->gui_height  = var.yres;
  this->display_ratio = 1.0;  

  /*
   *
   * depth in X11 terminology land is the number of bits used to
   * actually represent the colour.
   *
   * bpp in X11 land means how many bits in the frame buffer per
   * pixel.
   *
   * ex. 15 bit color is 15 bit depth and 16 bpp. Also 24 bit
   *     color is 24 bit depth, but can be 24 bpp or 32 bpp.
   */

  /* fb assumptions: bpp % 8   = 0 (e.g. 8, 16, 24, 32 bpp)
   *                 bpp      <= 32
   *                 msb_right = 0
   */
   
  this->bytes_per_pixel = (var.bits_per_pixel + 7)/8;
  this->bpp = this->bytes_per_pixel * 8;
  this->depth  = var.red.length + var.green.length + var.blue.length;

  if (this->depth>16)
    printf ("\n\n"
	    "WARNING: current display depth is %d. For better performance\n"
	    "a depth of 16 bpp is recommended!\n\n",
	    this->depth);

  printf ("video_out_fb: video mode depth is %d (%d bpp),\n"
	  "\tred: %d/%d, green: %d/%d, blue: %d/%d\n",
	  this->depth, this->bpp,
          var.red.length, var.red.offset,
          var.green.length, var.green.offset,
          var.blue.length, var.blue.offset );
          
  mode = 0;

  switch (fix.visual) {
  case FB_VISUAL_TRUECOLOR:
    switch (this->depth) {
    case 24:
      if (this->bpp == 32) {
	if (!var.blue.offset)
	  mode = MODE_32_RGB;
	else
	  mode = MODE_32_BGR;
      } else {
	if (!var.blue.offset)
	  mode = MODE_24_RGB;
	else
	  mode = MODE_24_BGR;
      }
      break;
    case 16:
      if (!var.blue.offset)
	mode = MODE_16_RGB;
      else
	mode = MODE_16_BGR;
      break;
    case 15:
      if (!var.blue.offset)
	mode = MODE_15_RGB;
      else
	mode = MODE_15_BGR;
      break;
    case 8:
      if (!var.blue.offset)
	mode = MODE_8_RGB; 
      else
	mode = MODE_8_BGR; 
      break;
    }
    break;
  }

  if (!mode) {
    printf ("video_out_fb: your video mode was not recognized, sorry :-(\n");
    return NULL;
  }
  
  /* mmap whole video memory */
  this->mem_size = fix.smem_len;
  this->video_mem = (char *) mmap(0, this->mem_size, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, this->fd, 0);

                                  
                                 
  this->yuv2rgb = yuv2rgb_init (mode, 0, this->fast_rgb);
  yuv2rgb_set_gamma(this->yuv2rgb, config->register_range (config, "video.fb_gamma", 0,
							   -100, 100, "gamma correction for FB driver",
							   NULL, NULL, NULL));

  return &this->vo_driver;
}

static vo_info_t vo_info_fb = {
  3,
  "fb",
  "xine video output plugin using linux framebuffer device",
  VISUAL_TYPE_FB,
  5
};

vo_info_t *get_video_out_plugin_info() {
  return &vo_info_fb;
}
