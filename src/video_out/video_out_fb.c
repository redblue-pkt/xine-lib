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
 * $Id: video_out_fb.c,v 1.14 2002/08/10 21:25:20 miguelfreitas Exp $
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

#define LOG

typedef struct fb_frame_s {
  vo_frame_t         vo_frame;

  int                width, height;
  int                ratio_code;
  int                format;
  int                flags;

  int                user_ratio;
  
  /* 
   * "ideal" size of this frame :
   * width/height corrected by aspect ratio
   */

  int                ideal_width, ideal_height;
  
  /*
   * "output" size of this frame:
   * this is finally the ideal size "fitted" into the
   * gui size while maintaining the aspect ratio
   */
  int                output_width, output_height;
  
  double             ratio_factor;/* ideal/rgb size must fulfill: height = width * ratio_factor  */

  uint8_t           *chunk[3]; /* mem alloc by xmalloc_aligned           */

  yuv2rgb_t         *yuv2rgb; /* yuv2rgb converter set up for this frame */
  uint8_t           *rgb_dst;
  int                yuv_stride;
  int                stripe_height, stripe_inc;
  
  int                bytes_per_line;
  uint8_t           *data;
} fb_frame_t;

typedef struct fb_driver_s {

  vo_driver_t      vo_driver;

  config_values_t *config;

  int		   fd;
  int              mem_size;
  uint8_t*         video_mem;       /* mmapped video memory */
  
  int		   scaling_disabled;
  int              depth, bpp, bytes_per_pixel;
  int              expecting_event;
  
  int                yuv2rgb_mode;
  int                yuv2rgb_swap;
  int                yuv2rgb_gamma;
  uint8_t           *yuv2rgb_cmap;
  yuv2rgb_factory_t *yuv2rgb_factory;

  vo_overlay_t    *overlay;

  /* size / aspect ratio calculations */
  int              user_ratio;
  double	   output_scale_factor;	 /* additional scale factor for the output frame */

  int		   last_frame_output_width; /* size of scaled rgb output img gui */
  int		   last_frame_output_height; /* has most recently adopted to */

  int              gui_width;		 /* size of gui window */
  int              gui_height;
  int              gui_changed;
  int              gui_linelength;

  /* display anatomy */
  double           display_ratio;        /* given by visual parameter from init function */

} fb_driver_t;

/* possible values for fb_driver_t, field gui_changed */
#define	GUI_SIZE_CHANGED	1
#define	GUI_ASPECT_CHANGED	2


/*
 * and now, the driver functions
 */

static uint32_t fb_get_capabilities (vo_driver_t *this_gen) {
  return VO_CAP_COPIES_IMAGE | VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_BRIGHTNESS;
}

static void fb_frame_copy (vo_frame_t *vo_img, uint8_t **src) {
  fb_frame_t  *frame = (fb_frame_t *) vo_img ;

  if (frame->format == IMGFMT_YV12) {
    frame->yuv2rgb->yuv2rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				 src[0], src[1], src[2]);
  } else {

    frame->yuv2rgb->yuy22rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				  src[0]);
				 
  }
  
  frame->rgb_dst += frame->stripe_inc; 
}

static void fb_frame_field (vo_frame_t *vo_img, int which_field) {

  fb_frame_t  *frame = (fb_frame_t *) vo_img ;
  
  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->data;
    frame->stripe_inc = 2*frame->stripe_height * frame->bytes_per_line;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->data + frame->bytes_per_line ;
    frame->stripe_inc = 2*frame->stripe_height * frame->bytes_per_line;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = (uint8_t *)frame->data;
    break;
  }
}

static void fb_frame_dispose (vo_frame_t *vo_img) {

  fb_frame_t  *frame = (fb_frame_t *) vo_img ;

  if (frame->data) {
     free(frame->data);
  }

  free (frame);
}


static vo_frame_t *fb_alloc_frame (vo_driver_t *this_gen) {
  fb_driver_t  *this = (fb_driver_t *) this_gen;
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
  frame->vo_frame.driver  = this_gen;
  
  /*
   * colorspace converter for this frame
   */

  frame->yuv2rgb = this->yuv2rgb_factory->create_converter (this->yuv2rgb_factory);
  
  return (vo_frame_t *) frame;
}


static void fb_compute_ideal_size (fb_driver_t *this, fb_frame_t *frame) {

  if (this->scaling_disabled) {

    frame->ideal_width   = frame->width;
    frame->ideal_height  = frame->height;
    frame->ratio_factor  = 1.0;

  } else {
    
    double image_ratio, desired_ratio, corr_factor;

    image_ratio = (double) frame->width / (double) frame->height;

    switch (frame->user_ratio) {
    case ASPECT_AUTO:
      switch (frame->ratio_code) {
      case XINE_ASPECT_RATIO_ANAMORPHIC:  /* anamorphic     */
      case XINE_ASPECT_RATIO_PAN_SCAN:    /* we display pan&scan as widescreen */
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
		frame->ratio_code);
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

    frame->ratio_factor = this->display_ratio * desired_ratio;

    corr_factor = frame->ratio_factor / image_ratio ;

    if (fabs(corr_factor - 1.0) < 0.005) {
      frame->ideal_width  = frame->width;
      frame->ideal_height = frame->height;

    } else {

      if (corr_factor >= 1.0) {
	frame->ideal_width  = frame->width * corr_factor + 0.5;
	frame->ideal_height = frame->height;
      } else {
	frame->ideal_width  = frame->width;
	frame->ideal_height = frame->height / corr_factor + 0.5;
      }

    }
  }
}

static void fb_compute_rgb_size (fb_driver_t *this, fb_frame_t *frame) {

  double x_factor, y_factor;

  /*
   * make the frame fit into the given destination area
   */
  
  x_factor = (double) this->gui_width  / (double) frame->ideal_width;
  y_factor = (double) this->gui_height / (double) frame->ideal_height;
  
  if ( x_factor < y_factor ) {
    frame->output_width   = (double) frame->ideal_width  * x_factor ;
    frame->output_height  = (double) frame->ideal_height * x_factor ;
  } else {
    frame->output_width   = (double) frame->ideal_width  * y_factor ;
    frame->output_height  = (double) frame->ideal_height * y_factor ;
  }

#ifdef LOG
  printf("video_out_fb: frame source %d x %d => screen output %d x %d%s\n",
	 frame->width, frame->height,
	 frame->output_width, frame->output_height,
	 ( frame->width != frame->output_width
	   || frame->height != frame->output_height
	   ? ", software scaling"
	   : "" )
	 );
#endif
}

static void fb_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags) {

  fb_driver_t  *this = (fb_driver_t *) this_gen;
  fb_frame_t   *frame = (fb_frame_t *) frame_gen;

  flags &= VO_BOTH_FIELDS;

  /* find out if we need to adapt this frame */

  if ((width != frame->width)
      || (height != frame->height)
      || (ratio_code != frame->ratio_code)
      || (flags != frame->flags)
      || (format != frame->format)
      || (this->user_ratio != frame->user_ratio)
      || this->gui_changed ) {

#ifdef LOG
    printf ("video_out_fb: frame format (from decoder) has changed => adapt\n");
#endif

    frame->width      = width;
    frame->height     = height;
    frame->ratio_code = ratio_code;
    frame->flags      = flags;
    frame->format     = format;
    frame->user_ratio = this->user_ratio;
    this->gui_changed = 0;

    fb_compute_ideal_size (this, frame);
    fb_compute_rgb_size (this, frame);


    /*
    printf ("video_out_fb: updating frame to %d x %d\n",
	    frame->output_width, frame->output_height);
    */

    /*
     * (re-) allocate 
     */

    if (frame->data) {
      if( frame->chunk[0] ) {
        free( frame->chunk[0] );
        frame->chunk[0] = NULL;
      }
      if( frame->chunk[1] ) {
        free( frame->chunk[1] );
        frame->chunk[1] = NULL;
      }
      if( frame->chunk[2] ) {
        free( frame->chunk[2] );
        frame->chunk[2] = NULL;
      }
      
      free (frame->data);
    }


    frame->data = xine_xmalloc (frame->output_width * frame->output_height *
                                this->bytes_per_pixel );
    
    if (format == IMGFMT_YV12) {
      frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
      frame->vo_frame.base[0] = xine_xmalloc_aligned (16, frame->vo_frame.pitches[0] * height,
                                                      (void **)&frame->chunk[0]);
      frame->vo_frame.base[1] = xine_xmalloc_aligned (16, frame->vo_frame.pitches[1] * ((height+1)/2),
                                                      (void **)&frame->chunk[1]);
      frame->vo_frame.base[2] = xine_xmalloc_aligned (16, frame->vo_frame.pitches[2] * ((height+1)/2),
                                                      (void **)&frame->chunk[2]);
    } else {
      frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
      frame->vo_frame.base[0] = xine_xmalloc_aligned (16, frame->vo_frame.pitches[0] * height,
                                                      (void **)&frame->chunk[0]);
      frame->chunk[1] = NULL;
      frame->chunk[2] = NULL;
    }
    
    frame->format = format;
    frame->width  = width;
    frame->height = height;

    frame->stripe_height = 16 * frame->output_height / frame->height;
    frame->bytes_per_line = frame->output_width * this->bytes_per_pixel;
  
    /* 
     * set up colorspace converter
     */

    switch (flags) {
    case VO_TOP_FIELD:
    case VO_BOTTOM_FIELD:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 frame->width,
				 16,
				 2*frame->vo_frame.pitches[0],
				 2*frame->vo_frame.pitches[1],
				 frame->output_width,
				 frame->stripe_height,
				 frame->bytes_per_line*2);
      frame->yuv_stride = frame->bytes_per_line*2;
      break;
    case VO_BOTH_FIELDS:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 frame->width,
				 16,
				 frame->vo_frame.pitches[0],
				 frame->vo_frame.pitches[1],
				 frame->output_width,
				 frame->stripe_height,
				 frame->bytes_per_line);
      frame->yuv_stride = frame->bytes_per_line;
      break;
    }
  }

  /*
   * reset dest pointers
   */

  if (frame->data) {
    switch (flags) {
    case VO_TOP_FIELD:
      frame->rgb_dst    = (uint8_t *)frame->data;
      frame->stripe_inc = 2 * frame->stripe_height * frame->bytes_per_line;
      break;
    case VO_BOTTOM_FIELD:
      frame->rgb_dst    = (uint8_t *)frame->data + frame->bytes_per_line ;
      frame->stripe_inc = 2 * frame->stripe_height * frame->bytes_per_line;
      break;
    case VO_BOTH_FIELDS:
      frame->rgb_dst    = (uint8_t *)frame->data;
      frame->stripe_inc = frame->stripe_height * frame->bytes_per_line;
      break;
    }
  }
}

static void fb_overlay_clut_yuv2rgb(fb_driver_t  *this, vo_overlay_t *overlay,
				    fb_frame_t *frame) {
  int i;
  clut_t* clut = (clut_t*) overlay->color;
  if (!overlay->rgb_clut) {
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
	frame->yuv2rgb->yuv2rgb_single_pixel_fun (frame->yuv2rgb,
						  clut[i].y, clut[i].cb, clut[i].cr);
    }
    overlay->rgb_clut++;
  }
  if (!overlay->clip_rgb_clut) {
    clut = (clut_t*) overlay->clip_color;
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
	frame->yuv2rgb->yuv2rgb_single_pixel_fun(frame->yuv2rgb,
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
       fb_overlay_clut_yuv2rgb(this,overlay,frame);

     switch(this->bpp) {
       case 16:
        blend_rgb16( (uint8_t *)frame->data, overlay,
		     frame->output_width, frame->output_height,
		     frame->width, frame->height);
        break;
       case 24:
        blend_rgb24( (uint8_t *)frame->data, overlay,
		     frame->output_width, frame->output_height,
		     frame->width, frame->height);
        break;
       case 32:
        blend_rgb32( (uint8_t *)frame->data, overlay,
		     frame->output_width, frame->output_height,
		     frame->width, frame->height);
        break;
       default:
	/* It should never get here */
	break;
     }        
   }
}

static int fb_redraw_needed (vo_driver_t *this_gen) {
  return 0;
}

static void fb_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  fb_driver_t  *this = (fb_driver_t *) this_gen;
  fb_frame_t   *frame = (fb_frame_t *) frame_gen;
  int		  xoffset;
  int		  yoffset;
  int             y;
  uint8_t	*dst, *src;

  if ( (frame->output_width != this->last_frame_output_width)
       || (frame->output_height != this->last_frame_output_height) ) {

    this->last_frame_output_width    = frame->output_width;
    this->last_frame_output_height   = frame->output_height;

    printf ("video_out_fb: gui size %d x %d, frame size %d x %d\n",
            this->gui_width, this->gui_height,
            frame->output_width, frame->output_height);
    
    memset(this->video_mem, 0, this->gui_linelength * this->gui_height );

  }
    
  xoffset  = (this->gui_width - frame->output_width) / 2;
  yoffset  = (this->gui_height - frame->output_height) / 2;

  dst = this->video_mem + yoffset * this->gui_linelength +
        xoffset * this->bytes_per_pixel;
  src = frame->data;
   
  for( y = 0; y < frame->output_height; y++ ) {
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
    return this->yuv2rgb_gamma;
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

    this->yuv2rgb_gamma = value;
    this->yuv2rgb_factory->set_gamma (this->yuv2rgb_factory, value);
    
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
  this->output_scale_factor = 1.0;
  
  this->scaling_disabled    = config->register_bool (config, "video.disable_scaling", 0,
						     _("disable all video scaling (faster!)"),
						     NULL, NULL, NULL);
  
  this->vo_driver.get_capabilities     = fb_get_capabilities;
  this->vo_driver.alloc_frame          = fb_alloc_frame;
  this->vo_driver.update_frame_format  = fb_update_frame_format;
  this->vo_driver.overlay_begin        = NULL; /* not used */
  this->vo_driver.overlay_blend        = fb_overlay_blend;
  this->vo_driver.overlay_end          = NULL; /* not used */
  this->vo_driver.display_frame        = fb_display_frame;
  this->vo_driver.get_property         = fb_get_property;
  this->vo_driver.set_property         = fb_set_property;
  this->vo_driver.get_property_min_max = fb_get_property_min_max;
  this->vo_driver.gui_data_exchange    = fb_gui_data_exchange;
  this->vo_driver.exit                 = fb_exit;
  this->vo_driver.redraw_needed        = fb_redraw_needed;

  device_name = config->register_string (config, "video.fb_device", "/dev/fb0",
					 _("framebuffer device"), NULL, NULL, NULL);
  
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
  
  if( (fix.visual != FB_VISUAL_TRUECOLOR && fix.visual != FB_VISUAL_DIRECTCOLOR) || fix.type != FB_TYPE_PACKED_PIXELS ) {
    printf("video_out_fb: only packed truecolor/directcolor is supported (%d).\n",fix.visual);
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
  this->user_ratio          = ASPECT_AUTO;
  

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
  case FB_VISUAL_DIRECTCOLOR:
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

  this->yuv2rgb_mode  = mode;
  this->yuv2rgb_swap  = 0;
  this->yuv2rgb_gamma = config->register_range (config, "video.fb_gamma", 0,
						-100, 100, 
						"gamma correction for fb driver",
						NULL, NULL, NULL);

  this->yuv2rgb_factory = yuv2rgb_factory_init (mode, this->yuv2rgb_swap, 
						this->yuv2rgb_cmap);
  this->yuv2rgb_factory->set_gamma (this->yuv2rgb_factory, this->yuv2rgb_gamma);
                                  
  printf ("video_out_fb: warning, xine's framebuffer driver is EXPERIMENTAL\n");
  return &this->vo_driver;
}

static vo_info_t vo_info_fb = {
  6,
  "fb",
  NULL,
  VISUAL_TYPE_FB,
  5
};

vo_info_t *get_video_out_plugin_info() {
  vo_info_fb.description = _("xine video output plugin using linux framebuffer device");
  return &vo_info_fb;
}
