/*
 * Copyright (C) 2000-2003 the xine project
 *
 * This file is part of xine, a free video player.
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
 * $Id: video_out_vidix.c,v 1.35 2003/03/16 22:28:14 jstembridge Exp $
 * 
 * video_out_vidix.c
 *
 * xine video_out driver to vidix library by Miguel Freitas 30/05/2002
 *
 * based on video_out_xv.c, video_out_syncfb.c and video_out_pgx64.c
 *
 * some vidix specific code from mplayer (file vosub_vidix.c)
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#endif

#ifdef HAVE_FB
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <errno.h>
#endif

#include "xine.h"
#include "vidixlib.h"
#include "fourcc.h"

#include "video_out.h"
#include "xine_internal.h"
#include "alphablend.h"
#include "xineutils.h"
#include "vo_scale.h"
              
#undef LOG
           

#define NUM_FRAMES 3

typedef struct vidix_driver_s vidix_driver_t;


typedef struct vidix_property_s {
  int                value;
  int                min;
  int                max;

  cfg_entry_t       *entry;

  vidix_driver_t       *this;
} vidix_property_t;


typedef struct vidix_frame_s {
    vo_frame_t vo_frame;
    int width, height, ratio_code, format;
} vidix_frame_t;


struct vidix_driver_s {

  vo_driver_t         vo_driver;

  config_values_t    *config;

  char               *vidix_name;
  VDL_HANDLE          vidix_handler;
  uint8_t            *vidix_mem;
  vidix_capability_t  vidix_cap;
  vidix_playback_t    vidix_play;
  vidix_grkey_t       vidix_grkey;
  vidix_video_eq_t    vidix_eq;
  vidix_yuv_t         dstrides;
  int                 vidix_started;
  int                 next_frame;
  vidix_frame_t      *current;

  int                 use_colourkey;
  uint32_t            colourkey;
  int                 use_doublebuffer;
    
  int                 supports_yv12;
          
  pthread_mutex_t     mutex;

  vidix_property_t    props[VO_NUM_PROPERTIES];
  uint32_t            capabilities;

  int                 visual_type;
  
  /* X11 related stuff */
#ifdef HAVE_X11
  Display            *display;
  int                 screen;
  Drawable            drawable;
  GC                  gc;
#endif
  
  /* fb related stuff */
  int                 fb_width;
  int                 fb_height;
  
  int                 depth;

  vo_scale_t          sc;

  int                 delivered_format;
  
  xine_t             *xine;
};

typedef struct vidix_class_s {
  video_driver_class_t driver_class;

  config_values_t     *config;

  VDL_HANDLE          vidix_handler;
  vidix_capability_t  vidix_cap;
  
  xine_t             *xine;
} vidix_class_t;


static void free_framedata(vidix_frame_t* frame)
{
   if(frame->vo_frame.base[0]) {
      free(frame->vo_frame.base[0]);
      frame->vo_frame.base[0] = NULL;
   }

   if(frame->vo_frame.base[1]) {
      free(frame->vo_frame.base[1]);
      frame->vo_frame.base[1] = NULL;
   }

   if(frame->vo_frame.base[2]) {
      free(frame->vo_frame.base[2]);
      frame->vo_frame.base[2] = NULL;
   }
}

static void write_frame_YUV422(vidix_driver_t* this, vidix_frame_t* frame)
{
   uint8_t*  y  = (uint8_t *)frame->vo_frame.base[0];
   uint8_t*  cb = (uint8_t *)frame->vo_frame.base[1];
   uint8_t*  cr = (uint8_t *)frame->vo_frame.base[2];
   uint8_t*  crp;
   uint8_t*  cbp;
   uint32_t* dst32 = (uint32_t *)(this->vidix_mem + 
                     this->vidix_play.offsets[this->next_frame] + 
                     this->vidix_play.offset.y);
   int h,w;

   for(h = 0; h < (frame->height / 2); h++) {
      cbp = cb;
      crp = cr;
      
      for(w = 0; w < (frame->width / 2); w++) {
	 *dst32++ = (*y) + ((*cb)<<8) + ((*(y+1))<<16) + ((*cr)<<24);
	 y++; y++; cb++; cr++;
      }

      dst32 += (this->dstrides.y - frame->width) / 2;

      for(w=0; w < (frame->width / 2); w++) {
	 *dst32++ = (*y) + ((*cbp)<<8) + ((*(y+1))<<16) + ((*crp)<<24);
	 y++; y++; cbp++; crp++;
      }
      
      dst32 += (this->dstrides.y - frame->width) / 2;
   }
}

static void write_frame_YUV420P2(vidix_driver_t* this, vidix_frame_t* frame)
{   
   uint8_t* y    = (uint8_t *)frame->vo_frame.base[0];
   uint8_t* cb   = (uint8_t *)frame->vo_frame.base[1];
   uint8_t* cr   = (uint8_t *)frame->vo_frame.base[2];
   uint8_t* dst8 = (this->vidix_mem +
                    this->vidix_play.offsets[this->next_frame] + 
                    this->vidix_play.offset.y);
   int h, w; 
   
   for(h = 0; h < frame->height; h++) {
      xine_fast_memcpy(dst8, y, frame->width);
      y    += frame->vo_frame.pitches[0];
      dst8 += this->dstrides.y;
   }
   
   dst8 = (this->vidix_mem + this->vidix_play.offsets[this->next_frame] +
           this->vidix_play.offset.v);

   for(h = 0; h < (frame->height / 2); h++) {
     for(w = 0; w < (frame->width / 2); w++) {
       dst8[2*w+0] = cb[w];
       dst8[2*w+1] = cr[w];
     }
      cb += frame->vo_frame.pitches[2];
      cr += frame->vo_frame.pitches[1];
      dst8 += this->dstrides.y;
   }
}

static void write_frame_YUV420P3(vidix_driver_t* this, vidix_frame_t* frame)
{   
   uint8_t* y    = (uint8_t *)frame->vo_frame.base[0];
   uint8_t* cb   = (uint8_t *)frame->vo_frame.base[1];
   uint8_t* cr   = (uint8_t *)frame->vo_frame.base[2];
   uint8_t* dst8 = (this->vidix_mem + 
                    this->vidix_play.offsets[this->next_frame] +
                    this->vidix_play.offset.y);
   int h, half_width = frame->width / 2;
   
   for(h = 0; h < frame->height; h++) {
      xine_fast_memcpy(dst8, y, frame->width);
      y    += frame->vo_frame.pitches[0];
      dst8 += this->dstrides.y;
   }

   dst8 = (this->vidix_mem + 
           this->vidix_play.offsets[this->next_frame]);

   for(h = 0; h < (frame->height / 2); h++) {
      xine_fast_memcpy(dst8 + this->vidix_play.offset.v, cb, half_width);
      xine_fast_memcpy(dst8 + this->vidix_play.offset.u, cr, half_width);
      
      cb   += frame->vo_frame.pitches[2];
      cr   += frame->vo_frame.pitches[1];
   
      dst8 += (this->dstrides.v / 2);
   }
}

static void write_frame_YUY2(vidix_driver_t* this, vidix_frame_t* frame)
{   
   uint8_t* src8 = (uint8_t *)frame->vo_frame.base[0];
   uint8_t* dst8 = (uint8_t *)(this->vidix_mem + 
                     this->vidix_play.offsets[this->next_frame] +
                     this->vidix_play.offset.y);
   int h, double_width = frame->width * 2;
                     
   for(h = 0; h < frame->height; h++) {
      xine_fast_memcpy(dst8, src8, double_width);

      dst8 += (this->dstrides.y * 2);
      src8 += frame->vo_frame.pitches[0];
   }
}

static void write_frame_sfb(vidix_driver_t* this, vidix_frame_t* frame)
{
  switch(frame->format) {   
    case XINE_IMGFMT_YUY2:
      write_frame_YUY2(this, frame);
      break;
      
    case XINE_IMGFMT_YV12:
      if(this->supports_yv12) {
        if(this->vidix_play.flags & VID_PLAY_INTERLEAVED_UV)
          write_frame_YUV420P2(this, frame);
        else
          write_frame_YUV420P3(this, frame);
      } else
          write_frame_YUV422(this,frame);
      break;
      
    default:
      printf("video_out_vidix: error. (unknown frame format %04x)\n", frame->format);
      break;
   }
}


static void vidix_clean_output_area(vidix_driver_t *this) {

  if(this->visual_type == XINE_VISUAL_TYPE_X11) {
#ifdef HAVE_X11
    XLockDisplay(this->display);      
      
    XSetForeground(this->display, this->gc, BlackPixel(this->display, this->screen));
    XFillRectangle(this->display, this->drawable, this->gc, this->sc.border[0].x, this->sc.border[0].y, this->sc.border[0].w, this->sc.border[0].h);
    XFillRectangle(this->display, this->drawable, this->gc, this->sc.border[1].x, this->sc.border[1].y, this->sc.border[1].w, this->sc.border[1].h);
    XFillRectangle(this->display, this->drawable, this->gc, this->sc.border[2].x, this->sc.border[2].y, this->sc.border[2].w, this->sc.border[2].h);
    XFillRectangle(this->display, this->drawable, this->gc, this->sc.border[3].x, this->sc.border[3].y, this->sc.border[3].w, this->sc.border[3].h);
  
    if(this->use_colourkey) {
      XSetForeground(this->display, this->gc, this->colourkey);
      XFillRectangle(this->display, this->drawable, this->gc, this->sc.output_xoffset, this->sc.output_yoffset, this->sc.output_width, this->sc.output_height);
    }
  
    XFlush(this->display);

    XUnlockDisplay(this->display);
#endif
  }
}


static void vidix_update_colourkey(vidix_driver_t *this) {

  if(this->use_colourkey) {
    this->vidix_grkey.ckey.op = CKEY_TRUE;
    
    switch(this->depth) {
    
      case 15:
        this->colourkey = ((this->vidix_grkey.ckey.red   & 0xF8) << 7) |
                          ((this->vidix_grkey.ckey.green & 0xF8) << 2) |
                          ((this->vidix_grkey.ckey.blue  & 0xF8) >> 3);
        break;
        
      case 16:
        this->colourkey = ((this->vidix_grkey.ckey.red   & 0xF8) << 8) |
                          ((this->vidix_grkey.ckey.green & 0xFC) << 3) |
                          ((this->vidix_grkey.ckey.blue  & 0xF8) >> 3);
        break;
        
      case 24:
      case 32:
        this->colourkey = ((this->vidix_grkey.ckey.red   & 0xFF) << 16) |
                          ((this->vidix_grkey.ckey.green & 0xFF) << 8) |
                          ((this->vidix_grkey.ckey.blue  & 0xFF));
        break;
      
      default:
        break;
    }
                  
    vidix_clean_output_area(this);
  } else
    this->vidix_grkey.ckey.op = CKEY_FALSE;

  vdlSetGrKeys(this->vidix_handler, &this->vidix_grkey);
}
    

static uint32_t vidix_get_capabilities (vo_driver_t *this_gen) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  return this->capabilities;
}

static void vidixfb_frame_output_cb(void *user_data, int video_width, int video_height, double video_pixel_aspect, int *dest_x, int *dest_y, int *dest_width, int *dest_height, double *dest_pixel_aspect, int *win_x, int *win_y) {
  vidix_driver_t *this = (vidix_driver_t *) user_data;

  *dest_x            = 0;
  *dest_y            = 0;
  *dest_width        = this->fb_width;
  *dest_height       = this->fb_height;
  *dest_pixel_aspect = 1.0;
  *win_x             = 0;
  *win_y             = 0;
}

static void vidix_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed for vidix */
}

static void vidix_frame_dispose (vo_frame_t *vo_img) {

  vidix_frame_t  *frame = (vidix_frame_t *) vo_img ;

  free_framedata(frame);  
  free (frame);
}

static vo_frame_t *vidix_alloc_frame (vo_driver_t *this_gen) {

  vidix_frame_t     *frame ;

  frame = (vidix_frame_t *) malloc (sizeof (vidix_frame_t));
  memset (frame, 0, sizeof(vidix_frame_t));

  if (frame==NULL) {
    printf ("vidix_alloc_frame: out of memory\n");
  }

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);
    
  frame->vo_frame.base[0] = NULL;
  frame->vo_frame.base[1] = NULL;
  frame->vo_frame.base[2] = NULL;

  /*
   * supply required functions
   */

  frame->vo_frame.copy    = NULL;
  frame->vo_frame.field   = vidix_frame_field;
  frame->vo_frame.dispose = vidix_frame_dispose;

  return (vo_frame_t *) frame;
}


static void vidix_compute_ideal_size (vidix_driver_t *this) {

  vo_scale_compute_ideal_size( &this->sc );

}

/*
 * Configure vidix device
 */

static void vidix_config_playback (vidix_driver_t *this) {

  uint32_t apitch;
  int err,i;
  
  vo_scale_compute_output_size( &this->sc );
  
  if( this->vidix_started ) {
#ifdef LOG
    printf("video_out_vidix: overlay off\n");
#endif
    vdlPlaybackOff(this->vidix_handler);
  }

  memset(&this->vidix_play,0,sizeof(vidix_playback_t));
      
  if(this->delivered_format == XINE_IMGFMT_YV12 && this->supports_yv12)
    this->vidix_play.fourcc = IMGFMT_YV12;
  else
    this->vidix_play.fourcc = IMGFMT_YUY2;
  
  this->vidix_play.capability = this->vidix_cap.flags; /* every ;) */
  this->vidix_play.blend_factor = 0; /* for now */
  this->vidix_play.src.x = this->sc.displayed_xoffset;
  this->vidix_play.src.y = this->sc.displayed_yoffset;
  this->vidix_play.src.w = this->sc.displayed_width;
  this->vidix_play.src.h = this->sc.displayed_height;
  this->vidix_play.dest.x = this->sc.gui_win_x+this->sc.output_xoffset;
  this->vidix_play.dest.y = this->sc.gui_win_y+this->sc.output_yoffset;
  this->vidix_play.dest.w = this->sc.output_width;
  this->vidix_play.dest.h = this->sc.output_height;
  this->vidix_play.num_frames= this->use_doublebuffer ? NUM_FRAMES : 1;
  this->vidix_play.src.pitch.y = this->vidix_play.src.pitch.u = this->vidix_play.src.pitch.v = 0;

  if((err=vdlConfigPlayback(this->vidix_handler,&this->vidix_play))!=0)
  {
     printf("video_out_vidix: Can't configure playback: %s\n",strerror(err));
  }

#ifdef LOG
  printf("video_out_vidix: dga_addr = %p frame_size = %d frames = %d\n",
         this->vidix_play.dga_addr, this->vidix_play.frame_size,
         this->vidix_play.num_frames );
  
  printf("video_out_vidix: offsets[0..2] = %d %d %d\n",
         this->vidix_play.offsets[0], this->vidix_play.offsets[1],
         this->vidix_play.offsets[2] );
  
  printf("video_out_vidix: offset.y/u/v = %d/%d/%d\n",
         this->vidix_play.offset.y, this->vidix_play.offset.u,
         this->vidix_play.offset.v );
  
  printf("video_out_vidix: src.x/y/w/h = %d/%d/%d/%d\n",
         this->vidix_play.src.x, this->vidix_play.src.y,
         this->vidix_play.src.w, this->vidix_play.src.h );
  
  printf("video_out_vidix: dest.x/y/w/h = %d/%d/%d/%d\n",
         this->vidix_play.dest.x, this->vidix_play.dest.y,
         this->vidix_play.dest.w, this->vidix_play.dest.h );

  printf("video_out_vidix: dest.pitch.y/u/v = %d/%d/%d\n",
         this->vidix_play.dest.pitch.y, this->vidix_play.dest.pitch.u,
         this->vidix_play.dest.pitch.v );
#endif
         
  this->vidix_mem = this->vidix_play.dga_addr;

  this->next_frame = 0;

  /* clear every frame with correct address and frame_size */
  for (i = 0; i < this->vidix_play.num_frames; i++)
    memset(this->vidix_mem + this->vidix_play.offsets[i], 0x80,
           this->vidix_play.frame_size);

  apitch = this->vidix_play.dest.pitch.y-1;
  this->dstrides.y = (this->sc.delivered_width + apitch) & ~apitch;
  apitch = this->vidix_play.dest.pitch.v-1;
  this->dstrides.v = (this->sc.delivered_width + apitch) & ~apitch;
  apitch = this->vidix_play.dest.pitch.u-1;
  this->dstrides.u = (this->sc.delivered_width + apitch) & ~apitch;
     
#ifdef LOG
  printf("video_out_vidix: overlay on\n");
#endif  
  vdlPlaybackOn(this->vidix_handler);
  this->vidix_started = 1;
}

static void vidix_update_frame_format (vo_driver_t *this_gen,
				    vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height,
				    int ratio_code, int format, int flags) {

  vidix_frame_t   *frame = (vidix_frame_t *) frame_gen;
  
  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

    /*
     * (re-) allocate image
     */
    
      free_framedata(frame);
      
      frame->width  = width;
      frame->height = height;
      frame->format = format;

      switch(format) {
       case XINE_IMGFMT_YV12:
	 frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
	 frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
	 frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
	 frame->vo_frame.base[0] = malloc(frame->vo_frame.pitches[0] * height);
         frame->vo_frame.base[1] = malloc(frame->vo_frame.pitches[1] * ((height+1)/2));
	 frame->vo_frame.base[2] = malloc(frame->vo_frame.pitches[2] * ((height+1)/2));
	 break;
       case XINE_IMGFMT_YUY2:
	 frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
	 frame->vo_frame.base[0] = malloc(frame->vo_frame.pitches[0] * height);
	 frame->vo_frame.base[1] = NULL;
	 frame->vo_frame.base[2] = NULL;
	 break;
       default:
	 printf("video_out_vidix: error. (unknown frame format: %04x)\n", format);
      }
      
      if((format == XINE_IMGFMT_YV12 && (frame->vo_frame.base[0] == NULL || frame->vo_frame.base[1] == NULL || frame->vo_frame.base[2] == NULL))
	 || (format == XINE_IMGFMT_YUY2 && frame->vo_frame.base[0] == NULL)) {
	 printf("video_out_vidix: error. (framedata allocation failed: out of memory)\n");
	 
	 free_framedata(frame);
      }
   }

  frame->ratio_code = ratio_code;
}


/*
 *
 */
static void vidix_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {

  vidix_frame_t   *frame = (vidix_frame_t *) frame_gen;
  
  if (overlay->rle) {
    if( frame->format == XINE_IMGFMT_YV12 )
      blend_yuv( frame->vo_frame.base, overlay, frame->width, frame->height, frame->vo_frame.pitches);
    else
      blend_yuy2( frame->vo_frame.base[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0]);
  }
}


static void vidix_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  vidix_driver_t  *this = (vidix_driver_t *) this_gen;
  vidix_frame_t   *frame = (vidix_frame_t *) frame_gen;

  pthread_mutex_lock(&this->mutex);

  if ( (frame->width != this->sc.delivered_width)
	 || (frame->height != this->sc.delivered_height)
	 || (frame->ratio_code != this->sc.delivered_ratio_code) 
	 || (frame->format != this->delivered_format ) ) {
#ifdef LOG
	 printf("video_out_vidix: change frame format\n");
#endif
      
      this->sc.delivered_width      = frame->width;
      this->sc.delivered_height     = frame->height;
      this->sc.delivered_ratio_code = frame->ratio_code;
      this->delivered_format     = frame->format;

      vidix_compute_ideal_size( this );
      this->sc.force_redraw = 1;
  }
    
  /* 
   * check if we have to reconfigure vidix because of
   * format/window position change
   */
  if(vo_scale_redraw_needed(&this->sc)) {
    vidix_config_playback(this);
    vidix_clean_output_area(this);
  }
  
  write_frame_sfb(this, frame);
  if( this->vidix_play.num_frames > 1 ) {
    vdlPlaybackFrameSelect(this->vidix_handler,this->next_frame);
    this->next_frame=(this->next_frame+1)%this->vidix_play.num_frames;
  }
  
  if((this->current != NULL) && (this->current != frame)) {
    frame->vo_frame.displayed(&this->current->vo_frame);
  }
  this->current = frame;  
  
  pthread_mutex_unlock(&this->mutex);
}


static int vidix_redraw_needed (vo_driver_t *this_gen) {
  vidix_driver_t  *this = (vidix_driver_t *) this_gen;
  int ret = 0;


  if( vo_scale_redraw_needed( &this->sc ) ) {

    if(this->current) {
      this->sc.force_redraw = 1;
      vidix_display_frame(this_gen, (vo_frame_t *) this->current);
    }

    ret = 1;
  }
  
  return ret;
}


static int vidix_get_property (vo_driver_t *this_gen, int property) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;
  
#ifdef LOG  
  printf ("video_out_vidix: property #%d = %d\n", property,
	  this->props[property].value);
#endif

  return this->props[property].value;
}


static int vidix_set_property (vo_driver_t *this_gen,
			    int property, int value) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;
  int err;
    
  if ((value >= this->props[property].min) && 
      (value <= this->props[property].max))
  {
  this->props[property].value = value;
  
  if ( property == VO_PROP_ASPECT_RATIO) {
#ifdef LOG
    printf("video_out_vidix: aspect ratio changed to %s\n",
	   vo_scale_aspect_ratio_name(value));
#endif
    
    if(value == NUM_ASPECT_RATIOS)
      value = this->props[property].value = ASPECT_AUTO;

    this->sc.user_ratio = value;    
    vidix_compute_ideal_size (this);
    this->sc.force_redraw = 1;
  } 

  if ( property == VO_PROP_ZOOM_X ) {
      this->sc.zoom_factor_x = (double)value / (double)VO_ZOOM_STEP;

      vidix_compute_ideal_size (this);
      this->sc.force_redraw = 1;
  } 

  if ( property == VO_PROP_ZOOM_Y ) {
      this->sc.zoom_factor_y = (double)value / (double)VO_ZOOM_STEP;

      vidix_compute_ideal_size (this);
      this->sc.force_redraw = 1;
  } 
  
  if ( property == VO_PROP_HUE ) {
    this->vidix_eq.cap = VEQ_CAP_HUE;
    this->vidix_eq.hue = value;
      
    if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)) != 0)
      if(this->xine->verbosity >= XINE_VERBOSITY_LOG)
        printf("video_out_vidix: can't set hue: %s\n", strerror(err));
  }
      
  if ( property == VO_PROP_SATURATION ) {
    this->vidix_eq.cap = VEQ_CAP_SATURATION;
    this->vidix_eq.saturation = value;
      
    if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)) != 0)
      if(this->xine->verbosity >= XINE_VERBOSITY_LOG)
        printf("video_out_vidix: can't set saturation: %s\n", strerror(err));
  }
    
  if ( property == VO_PROP_BRIGHTNESS ) {
    this->vidix_eq.cap = VEQ_CAP_BRIGHTNESS;
    this->vidix_eq.brightness = value;
      
    if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)) != 0)
      if(this->xine->verbosity >= XINE_VERBOSITY_LOG)
        printf("video_out_vidix: can't set brightness: %s\n", strerror(err));
  }
      
  if ( property == VO_PROP_CONTRAST ) {
    this->vidix_eq.cap = VEQ_CAP_CONTRAST;
    this->vidix_eq.contrast = value;
      
    if((err = vdlPlaybackSetEq(this->vidix_handler, &this->vidix_eq)) != 0)
      if(this->xine->verbosity >= XINE_VERBOSITY_LOG)
        printf("video_out_vidix: can't set contrast: %s\n", strerror(err));
  }
  }
    
  return value;
}


static void vidix_ckey_callback(vo_driver_t *this_gen, xine_cfg_entry_t *entry) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;  
  
  if(strcmp(entry->key, "video.vidix_use_colour_key") == 0) {
    this->use_colourkey = entry->num_value;
  }
  
  if(strcmp(entry->key, "video.vidix_colour_key_red") == 0) {
    this->vidix_grkey.ckey.red = entry->num_value;
  }
  
  if(strcmp(entry->key, "video.vidix_colour_key_green") == 0) {
    this->vidix_grkey.ckey.green = entry->num_value;
  }
  
  if(strcmp(entry->key, "video.vidix_colour_key_blue") == 0) {
    this->vidix_grkey.ckey.blue = entry->num_value;
  }
  
  vidix_update_colourkey(this);
  this->sc.force_redraw = 1;
}


static void vidix_db_callback(vo_driver_t *this_gen, xine_cfg_entry_t *entry) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen; 

  this->use_doublebuffer = entry->num_value;
  this->sc.force_redraw = 1;
}


static void vidix_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  *min = this->props[property].min;
  *max = this->props[property].max;
}

static int vidix_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {

  int ret = 0;
  vidix_driver_t     *this = (vidix_driver_t *) this_gen;

  pthread_mutex_lock(&this->mutex);
    
  switch (data_type) {

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
#ifdef LOG
      printf ("video_out_vidix: GUI_DATA_EX_DRAWABLE_CHANGED\n");
#endif
    
    if(this->visual_type == XINE_VISUAL_TYPE_X11) {
#ifdef HAVE_X11
      this->drawable = (Drawable) data;
      XLockDisplay(this->display);
      XFreeGC(this->display, this->gc);
      this->gc = XCreateGC(this->display, this->drawable, 0, NULL);
      XUnlockDisplay(this->display);
#endif
    }
    break;
  
  case XINE_GUI_SEND_EXPOSE_EVENT:
#ifdef LOG
      printf ("video_out_vidix: GUI_DATA_EX_EXPOSE_EVENT\n");
#endif
    vidix_clean_output_area(this);
    break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      vo_scale_translate_gui2video(&this->sc, rect->x, rect->y,
			     &x1, &y1);
      vo_scale_translate_gui2video(&this->sc, rect->x + rect->w, rect->y + rect->h,
			     &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
    }
    break;
      
  default:
    ret = -1;
  }
  pthread_mutex_unlock(&this->mutex);

  return ret;
}
                            
static void vidix_exit (vo_driver_t *this_gen) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  if( this->vidix_started ) {
    vdlPlaybackOff(this->vidix_handler);
  }
  vdlClose(this->vidix_handler);
}

static vidix_driver_t *open_plugin (video_driver_class_t *class_gen) {
  vidix_class_t        *class = (vidix_class_t *) class_gen;
  config_values_t      *config = class->config;
  vidix_driver_t       *this;
  int                   err;
    
  this = malloc (sizeof (vidix_driver_t));

  if (!this) {
    printf ("video_out_vidix: malloc failed\n");
    return NULL;
  }
  memset (this, 0, sizeof(vidix_driver_t));
  
  pthread_mutex_init (&this->mutex, NULL);

  this->vidix_handler = class->vidix_handler;
  this->vidix_cap = class->vidix_cap;

  vo_scale_init( &this->sc, 1, /*this->vidix_cap.flags & FLAG_UPSCALER,*/ 0, config );
  
  this->xine              = class->xine;
  this->config            = config;
  
  this->current           = NULL;
  
  this->capabilities      = 0;

  /* Find what equalizer flags are supported */  
  if(this->vidix_cap.flags & FLAG_EQUALIZER) {
    if((err = vdlPlaybackGetEq(this->vidix_handler, &this->vidix_eq)) != 0) {
      if(this->xine->verbosity >= XINE_VERBOSITY_LOG)
        printf("video_out_vidix: couldn't get equalizer capabilities: %s\n", strerror(err));
    } else {
      if(this->vidix_eq.cap & VEQ_CAP_BRIGHTNESS) {
        this->capabilities |= VO_CAP_BRIGHTNESS;
        
        this->props[VO_PROP_BRIGHTNESS].value = 0;
        this->props[VO_PROP_BRIGHTNESS].min = -1000;
        this->props[VO_PROP_BRIGHTNESS].max = 1000;
     }
      
      if(this->vidix_eq.cap & VEQ_CAP_CONTRAST) {
        this->capabilities |= VO_CAP_CONTRAST;
        
        this->props[VO_PROP_CONTRAST].value = 0;
        this->props[VO_PROP_CONTRAST].min = -1000;
        this->props[VO_PROP_CONTRAST].max = 1000;
      }
      
      if(this->vidix_eq.cap & VEQ_CAP_SATURATION) {
        this->capabilities |= VO_CAP_SATURATION;
        
        this->props[VO_PROP_SATURATION].value = 0;
        this->props[VO_PROP_SATURATION].min = -1000;
        this->props[VO_PROP_SATURATION].max = 1000;
      }
            
      if(this->vidix_eq.cap & VEQ_CAP_HUE) {
        this->capabilities |= VO_CAP_HUE;
        
        this->props[VO_PROP_HUE].value = 0;
        this->props[VO_PROP_HUE].min = -1000;
        this->props[VO_PROP_HUE].max = 1000;
      }
    }
  }
  
  /* Configuration for double buffering */
  this->use_doublebuffer = config->register_bool(config,
    "video.vidix_use_double_buffer", 1, "double buffer to sync video to retrace", NULL, 10,
    (void*) vidix_db_callback, this);
    
  /* Set up remaining props */
  this->props[VO_PROP_ASPECT_RATIO].value = ASPECT_AUTO;
  this->props[VO_PROP_ASPECT_RATIO].min = 0;
  this->props[VO_PROP_ASPECT_RATIO].max = NUM_ASPECT_RATIOS;
  
  this->props[VO_PROP_ZOOM_X].value = 100;
  this->props[VO_PROP_ZOOM_X].min = VO_ZOOM_MIN;
  this->props[VO_PROP_ZOOM_X].max = VO_ZOOM_MAX;
  
  this->props[VO_PROP_ZOOM_Y].value = 100;
  this->props[VO_PROP_ZOOM_Y].min = VO_ZOOM_MIN;
  this->props[VO_PROP_ZOOM_Y].max = VO_ZOOM_MAX;
     
  this->vo_driver.get_capabilities     = vidix_get_capabilities;
  this->vo_driver.alloc_frame          = vidix_alloc_frame;
  this->vo_driver.update_frame_format  = vidix_update_frame_format;
  this->vo_driver.overlay_begin        = NULL; /* not used */
  this->vo_driver.overlay_blend        = vidix_overlay_blend;
  this->vo_driver.overlay_end          = NULL; /* not used */
  this->vo_driver.display_frame        = vidix_display_frame;
  this->vo_driver.get_property         = vidix_get_property;
  this->vo_driver.set_property         = vidix_set_property;
  this->vo_driver.get_property_min_max = vidix_get_property_min_max;
  this->vo_driver.gui_data_exchange    = vidix_gui_data_exchange;
  this->vo_driver.dispose              = vidix_exit;
  this->vo_driver.redraw_needed        = vidix_redraw_needed;

  return this;
}

static void query_fourccs (vidix_driver_t *this) {
  vidix_fourcc_t        vidix_fourcc;
  int                   err;
    
  /* Detect if YUY2 is supported */
  memset(&vidix_fourcc, 0, sizeof(vidix_fourcc_t));
  vidix_fourcc.fourcc = IMGFMT_YUY2;
  vidix_fourcc.depth = this->depth;
  
  if((err = vdlQueryFourcc(this->vidix_handler, &vidix_fourcc)) == 0) {
    this->capabilities |= VO_CAP_YUY2;
    if(this->xine->verbosity >= XINE_VERBOSITY_LOG)
      printf("video_out_vidix: adaptor supports the yuy2 format\n");
  }
    
  /* Detect if YV12 is supported - we always support yv12 but we need
     to know if we have to convert */
  this->capabilities |= VO_CAP_YV12;
  vidix_fourcc.fourcc = IMGFMT_YV12;
  
  if((err = vdlQueryFourcc(this->vidix_handler, &vidix_fourcc)) == 0) {
    this->supports_yv12 = 1;
    if(this->xine->verbosity >= XINE_VERBOSITY_LOG)
      printf("video_out_vidix: adaptor supports the yv12 format\n");
  } else
    this->supports_yv12 = 0;
}

static void *init_class (xine_t *xine, void *visual_gen) {
  vidix_class_t        *this;
  int                   err;
  
  this = malloc (sizeof (vidix_class_t));
  
  if (!this) {
    printf ("video_out_vidix: malloc failed\n");
    return NULL;
  }
  memset (this, 0, sizeof(vidix_class_t));
  
  
  if(vdlGetVersion() != VIDIX_VERSION)
  {
    printf("video_out_vidix: You have wrong version of VIDIX library\n");
    free(this);
    return NULL;
  }
  this->vidix_handler = vdlOpen((XINE_PLUGINDIR"/vidix/"), NULL, TYPE_OUTPUT, 0);
  if(this->vidix_handler == NULL)
  {
    printf("video_out_vidix: Couldn't find working VIDIX driver\n");
    free(this);
    return NULL;
  }
  if((err=vdlGetCapability(this->vidix_handler,&this->vidix_cap)) != 0)
  {
    printf("video_out_vidix: Couldn't get capability: %s\n",strerror(err));
    free(this);
    return NULL;
  }
  if(xine->verbosity >= XINE_VERBOSITY_LOG)
    printf("video_out_vidix: using driver: %s by %s\n",this->vidix_cap.name,this->vidix_cap.author);


  this->xine              = xine;
  this->config            = xine->config;
  
  return this;
}

static void dispose_class (video_driver_class_t *this_gen) {
  vidix_class_t        *this = (vidix_class_t *) this_gen;

  free (this);
}

#ifdef HAVE_X11
static vo_driver_t *vidix_open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  vidix_driver_t       *this   = open_plugin(class_gen);
  config_values_t      *config = this->config;
    
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XWindowAttributes     window_attributes;  
  
  this->visual_type       = XINE_VISUAL_TYPE_X11;
  
  this->display           = visual->display;
  this->screen            = visual->screen;
  this->drawable          = visual->d;
  this->gc                = XCreateGC(this->display, this->drawable, 0, NULL);
 
  XGetWindowAttributes(this->display, this->drawable, &window_attributes);
  this->sc.gui_width      = window_attributes.width;
  this->sc.gui_height     = window_attributes.height;
  this->depth             = window_attributes.depth;
  
  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;
  
  /* We'll assume all drivers support colour keying (which they do 
     at the moment) */
  this->capabilities |= VO_CAP_COLORKEY;
  
  /* Someone might want to disable colour keying (?) */
  this->use_colourkey = config->register_bool(config, 
    "video.vidix_use_colour_key", 1, "enable use of overlay colour key", 
    NULL, 10, (void*) vidix_ckey_callback, this);
    
  /* Colour key components */
  this->vidix_grkey.ckey.red = config->register_range(config,
    "video.vidix_colour_key_red", 255, 0, 255, 
    "video overlay colour key red component", NULL, 10,
    (void*) vidix_ckey_callback, this); 
  
  this->vidix_grkey.ckey.green = config->register_range(config,
    "video.vidix_colour_key_green", 0, 0, 255, 
    "video overlay colour key green component", NULL, 10,
    (void*) vidix_ckey_callback, this);     
  
  this->vidix_grkey.ckey.blue = config->register_range(config,
    "video.vidix_colour_key_blue", 255, 0, 255, 
    "video overlay colour key blue component", NULL, 10,
    (void*) vidix_ckey_callback, this);     
    
  vidix_update_colourkey(this);

  query_fourccs(this);

  return &this->vo_driver;
}

static char* vidix_get_identifier (video_driver_class_t *this_gen) {
  return "vidix";
}

static char* vidix_get_description (video_driver_class_t *this_gen) {
  return _("xine video output plugin using libvidix for x11");
}

static void *vidix_init_class (xine_t *xine, void *visual_gen) {

  vidix_class_t *this = init_class (xine, visual_gen);
  
  if(this) {
    this->driver_class.open_plugin     = vidix_open_plugin;
    this->driver_class.get_identifier  = vidix_get_identifier;
    this->driver_class.get_description = vidix_get_description;
    this->driver_class.dispose         = dispose_class;
  }
  
  return this;
}

static vo_info_t vo_info_vidix = {
  2,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};
#endif

#ifdef HAVE_FB
static vo_driver_t *vidixfb_open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  vidix_driver_t           *this = open_plugin(class_gen);
  config_values_t          *config = this->config;
  char                     *device;
  int                       fd;
  struct fb_var_screeninfo  fb_var;
    
  this->visual_type = XINE_VISUAL_TYPE_FB;
  
  /* Register config option for fb device */
  device = config->register_string(config, "video.vidixfb_device", "/dev/fb0",
    "frame buffer device for vidix overlay", NULL, 10, NULL, NULL);
  
  /* Open fb device for reading */
  if((fd = open("/dev/fb0", O_RDONLY)) < 0) {
    printf("video_out_vidix: unable to open frame buffer device \"%s\": %s\n", 
      device, strerror(errno));
    return NULL;
  }
  
  /* Read screen info */
  if(ioctl(fd, FBIOGET_VSCREENINFO, &fb_var) != 0) {
    perror("video_out_vidix: error in ioctl FBIOGET_VSCREENINFO");
    close(fd);
    return NULL;
  }
  
  /* Store screen bpp and dimensions */
  this->depth = fb_var.bits_per_pixel;
  this->fb_width = fb_var.xres;
  this->fb_height = fb_var.yres;
  
  /* Close device */
  close(fd);
  
  this->sc.frame_output_cb   = vidixfb_frame_output_cb;
  this->sc.user_data         = this;
    
  /* No need for colour key on the frame buffer */
  this->use_colourkey = 0;
  
  query_fourccs(this);
  
  return &this->vo_driver;
}

static char* vidixfb_get_identifier (video_driver_class_t *this_gen) {
  return "vidixfb";
} 

static char* vidixfb_get_description (video_driver_class_t *this_gen) {
  return _("xine video output plugin using libvidix for linux frame buffer");
}

static void *vidixfb_init_class (xine_t *xine, void *visual_gen) {

  vidix_class_t *this = init_class (xine, visual_gen);
  
  if(this) {
    this->driver_class.open_plugin     = vidixfb_open_plugin;
    this->driver_class.get_identifier  = vidixfb_get_identifier;
    this->driver_class.get_description = vidixfb_get_description;
    this->driver_class.dispose         = dispose_class;
  }
  
  return this;
}

static vo_info_t vo_info_vidixfb = {
  2,                    /* priority    */
  XINE_VISUAL_TYPE_FB   /* visual type */
};
#endif

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
#ifdef HAVE_X11
  { PLUGIN_VIDEO_OUT, 14, "vidix", XINE_VERSION_CODE, &vo_info_vidix, vidix_init_class },
#endif
#ifdef HAVE_FB
  { PLUGIN_VIDEO_OUT, 14, "vidixfb", XINE_VERSION_CODE, &vo_info_vidixfb, vidixfb_init_class },
#endif
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
