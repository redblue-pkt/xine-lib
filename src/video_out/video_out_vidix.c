/*
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: video_out_vidix.c,v 1.13 2002/11/04 22:48:14 f1rmb Exp $
 * 
 * video_out_vidix.c
 *
 * xine video_out driver to vidix library by Miguel Freitas 30/05/2002
 *
 * based on video_out_xv.c and video_out_syncfb.c
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
#include <math.h>

#include <X11/Xlib.h>

#include "xine.h"
#include "vidixlib.h"

#include "video_out.h"
#include "xine_internal.h"
#include "alphablend.h"
#include "xineutils.h"
#include "vo_scale.h"
              
#undef LOG
           

#define NUM_FRAMES 1

typedef struct vidix_driver_s vidix_driver_t;

typedef struct vidix_frame_s {
    vo_frame_t vo_frame;
    int width, height, ratio_code, format;
} vidix_frame_t;


struct vidix_driver_s {

  xine_vo_driver_t    vo_driver;

  config_values_t    *config;

  char               *vidix_name;
  VDL_HANDLE          vidix_handler;
  uint8_t            *vidix_mem;
  vidix_capability_t  vidix_cap;
  vidix_playback_t    vidix_play;
  vidix_fourcc_t      vidix_fourcc;
  vidix_yuv_t         dstrides;
  int                 vidix_started;
  int                 next_frame;

  int                 yuv_format;
          
  pthread_mutex_t     mutex;

  uint32_t            capabilities;

   /* X11 / Xv related stuff */
  Display            *display;
  int                 screen;
  Drawable            drawable;

  vo_scale_t          sc;

  int                 delivered_format;
  int                 zoom_x, zoom_y;
};

typedef struct {
  video_driver_class_t driver_class;

  config_values_t     *config;

  VDL_HANDLE          vidix_handler;
  vidix_capability_t  vidix_cap;
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

#if 0
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
   uint8_t* dst8 = (this->vidix_mem + this->vidix_play.offset.u +
                     this->vidix_play.offsets[this->next_frame] + 
                     this->vidix_play.offset.u);
   int h, w; 
   
   register uint32_t* tmp32;
   register uint8_t*  rcr;
   register uint8_t*  rcb;

   rcr = cr;
   rcb = cb;
	
   for(h = 0; h < (frame->height / 2); h++) {
      tmp32 = (uint32_t *)dst8;
      w = (frame->width / 8) * 2;
      
      while(w--) {
	 register uint32_t temp;
	 
	 temp = (*rcb) | (*rcr << 8);
	 rcr++;
	 rcb++;
	 temp |= (*rcb << 16) | (*rcr << 24);
	 rcr++;
	 rcb++;
	 *tmp32 = temp;
	 tmp32++;
      }
      
      dst8 += this->dstrides.y;
   }

   dst8 = (this->vidix_mem + 
           this->vidix_play.offsets[this->next_frame] +
           this->vidix_play.offset.y);
   for(h = 0; h < frame->height; h++) {
      xine_fast_memcpy(dst8, y, frame->width);
      y    += frame->width;
      dst8 += this->dstrides.y;
   }
}
#endif
static void write_frame_YUV420P3(vidix_driver_t* this, vidix_frame_t* frame)
{   
   uint8_t* y    = (uint8_t *)frame->vo_frame.base[0];
   uint8_t* cb   = (uint8_t *)frame->vo_frame.base[1];
   uint8_t* cr   = (uint8_t *)frame->vo_frame.base[2];
   uint8_t* dst8 = (this->vidix_mem + 
                    this->vidix_play.offsets[this->next_frame] +
                    this->vidix_play.offset.y);
   int h, half_width = (frame->width/2);
   
   for(h = 0; h < frame->height; h++) {
      xine_fast_memcpy(dst8, y, frame->width);
      y    += frame->width;
      dst8 += this->dstrides.y;
   }

   dst8 = (this->vidix_mem + 
           this->vidix_play.offsets[this->next_frame]);
   for(h = 0; h < (frame->height / 2); h++) {
      xine_fast_memcpy((dst8 + this->vidix_play.offset.v), cb, half_width);
      xine_fast_memcpy((dst8 + this->vidix_play.offset.u), cr, half_width);
      
      cb   += half_width;
      cr   += half_width;
   
      dst8 += (this->dstrides.y / 2);
   }
}

static void write_frame_YUY2(vidix_driver_t* this, vidix_frame_t* frame)
{   
   uint8_t* src8 = (uint8_t *)frame->vo_frame.base[0];
   uint8_t* dst8 = (uint8_t *)(this->vidix_mem + 
                     this->vidix_play.offsets[this->next_frame] +
                     this->vidix_play.offset.y);
   int h, double_width = (frame->width * 2);
                     
   for(h = 0; h < frame->height; h++) {
      xine_fast_memcpy(dst8, src8, double_width);

      dst8 += (this->dstrides.y * 2);
      src8 += double_width;
   }
}

static void write_frame_sfb(vidix_driver_t* this, vidix_frame_t* frame)
{
   switch(frame->format) {   
    case XINE_IMGFMT_YUY2:
      write_frame_YUY2(this, frame);
/*
      else
	printf("video_out_vidix: error. (YUY2 not supported by your graphic card)\n");	
*/
      break;
      
    case XINE_IMGFMT_YV12:
	 write_frame_YUV420P3(this, frame);
/*      switch(this->yuv_format) {
       case VIDEO_PALETTE_YUV422:
	 write_frame_YUV422(this, frame);
	 break;
       case VIDEO_PALETTE_YUV420P2:
	 write_frame_YUV420P2(this, frame);
	 break;
       case VIDEO_PALETTE_YUV420P3:
	 write_frame_YUV420P3(this, frame);
	 break;
       default:
	 printf("video_out_vidix: error. (YV12 not supported by your graphic card)\n");
      }	
*/
      break;
      
    default:
      printf("video_out_vidix: error. (unknown frame format)\n");
      break;
   }
   
   frame->vo_frame.displayed(&frame->vo_frame);
}


static uint32_t vidix_get_capabilities (xine_vo_driver_t *this_gen) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  return this->capabilities;
}

static void vidix_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed for vidix */
}

static void vidix_frame_dispose (vo_frame_t *vo_img) {

  vidix_frame_t  *frame = (vidix_frame_t *) vo_img ;

  free_framedata(frame);  
  free (frame);
}

static vo_frame_t *vidix_alloc_frame (xine_vo_driver_t *this_gen) {

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
 * make ideal width/height "fit" into the gui
 */

static void vidix_compute_output_size (vidix_driver_t *this) {

  uint32_t apitch;
  int err,i;
  
  //  if( !this->sc.ideal_width || !this->sc.ideal_height )
  //    return;

  vo_scale_compute_output_size( &this->sc );
  
#ifdef LOG
  printf ("video_out_vidix: frame source %d x %d => screen output %d x %d\n",
	  this->sc.delivered_width, this->sc.delivered_height,
	  this->sc.output_width, this->sc.output_height);
#endif
  
  if( this->vidix_started ) {
    vdlPlaybackOff(this->vidix_handler);
  }

  memset(&this->vidix_play,0,sizeof(vidix_playback_t));
      
  this->vidix_play.fourcc = this->delivered_format;
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
  this->vidix_play.num_frames=NUM_FRAMES;
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
     
  vdlPlaybackOn(this->vidix_handler);
  this->vidix_started = 1;
}

static void vidix_update_frame_format (xine_vo_driver_t *this_gen,
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
	 printf("video_out_vidix: error. (unable to allocate framedata because of unknown frame format: %04x)\n", format);
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
static void vidix_overlay_blend (xine_vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {

  vidix_frame_t   *frame = (vidix_frame_t *) frame_gen;
  
  if (overlay->rle) {
    if( frame->format == XINE_IMGFMT_YV12 )
      blend_yuv( frame->vo_frame.base, overlay, frame->width, frame->height, frame->vo_frame.pitches);
    else
      blend_yuy2( frame->vo_frame.base[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0]);
  }
}

static int vidix_redraw_needed (xine_vo_driver_t *this_gen) {
  vidix_driver_t  *this = (vidix_driver_t *) this_gen;
  int ret = 0;


  if( vo_scale_redraw_needed( &this->sc ) ) {

    vidix_compute_output_size (this);

    ret = 1;
  }
  
  return ret;
}


static void vidix_display_frame (xine_vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  vidix_driver_t  *this = (vidix_driver_t *) this_gen;
  vidix_frame_t   *frame = (vidix_frame_t *) frame_gen;

  pthread_mutex_lock(&this->mutex);

  if ( (frame->width != this->sc.delivered_width)
	 || (frame->height != this->sc.delivered_height)
	 || (frame->ratio_code != this->sc.delivered_ratio_code) 
	 || (frame->format != this->delivered_format ) ) {
	 printf("video_out_vidix: change frame format\n");
      
      this->sc.delivered_width      = frame->width;
      this->sc.delivered_height     = frame->height;
      this->sc.delivered_ratio_code = frame->ratio_code;
      this->delivered_format     = frame->format;

      vidix_compute_ideal_size( this );
      this->sc.force_redraw = 1;
  }
    
  /* 
   * tell gui that we are about to display a frame,
   * ask for offset and output size
   */
  vidix_redraw_needed (this_gen);
    
  write_frame_sfb(this, frame);
  if( this->vidix_play.num_frames > 1 ) {
    vdlPlaybackFrameSelect(this->vidix_handler,this->next_frame);
    this->next_frame=(this->next_frame+1)%this->vidix_play.num_frames;
  }
  
  pthread_mutex_unlock(&this->mutex);
}

static int vidix_get_property (xine_vo_driver_t *this_gen, int property) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;
  
  if ( property == VO_PROP_ASPECT_RATIO)
    return this->sc.user_ratio ;
  
  if ( property == VO_PROP_ZOOM_X )
    return this->zoom_x;

  if ( property == VO_PROP_ZOOM_Y )
    return this->zoom_y;

  return 0;
}


static int vidix_set_property (xine_vo_driver_t *this_gen,
			    int property, int value) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;
  
  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>=NUM_ASPECT_RATIOS)
      value = ASPECT_AUTO;
    this->sc.user_ratio = value;
    printf("video_out_vidix: aspect ratio changed to %s\n",
	   vo_scale_aspect_ratio_name(value));
    
    vidix_compute_ideal_size (this);
    this->sc.force_redraw = 1;
  } 

  if ( property == VO_PROP_ZOOM_X ) {
    if ((value >= VO_ZOOM_MIN) && (value <= VO_ZOOM_MAX)) {
      this->zoom_x = value;
      this->sc.zoom_factor_x = (double)value / (double)VO_ZOOM_STEP;

      vidix_compute_ideal_size (this);
      this->sc.force_redraw = 1;
    }
  } 

  if ( property == VO_PROP_ZOOM_Y ) {
    if ((value >= VO_ZOOM_MIN) && (value <= VO_ZOOM_MAX)) {
      this->zoom_y = value;
      this->sc.zoom_factor_y = (double)value / (double)VO_ZOOM_STEP;

      vidix_compute_ideal_size (this);
      this->sc.force_redraw = 1;
    }
  } 
  
  return value;
}

static void vidix_get_property_min_max (xine_vo_driver_t *this_gen,
				     int property, int *min, int *max) {

/*  vidix_driver_t *this = (vidix_driver_t *) this_gen; */

  if ( property == VO_PROP_ZOOM_X || property == VO_PROP_ZOOM_Y ) {
    *min = VO_ZOOM_MIN;
    *max = VO_ZOOM_MAX;
  }
}

static int vidix_gui_data_exchange (xine_vo_driver_t *this_gen,
				 int data_type, void *data) {

  int ret = 0;
  vidix_driver_t     *this = (vidix_driver_t *) this_gen;

  pthread_mutex_lock(&this->mutex);
    
  switch (data_type) {

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
#ifdef LOG
      printf ("video_out_vidix: GUI_DATA_EX_DRAWABLE_CHANGED\n");
#endif
    
    this->drawable = (Drawable) data;
    break;
  
  case XINE_GUI_SEND_EXPOSE_EVENT:
#ifdef LOG
      printf ("video_out_vidix: GUI_DATA_EX_EXPOSE_EVENT\n");
#endif
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
                            
static void vidix_exit (xine_vo_driver_t *this_gen) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;

  if( this->vidix_started ) {
    vdlPlaybackOff(this->vidix_handler);
  }
  vdlClose(this->vidix_handler);
}

static xine_vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  vidix_class_t        *class = (vidix_class_t *) class_gen;
  config_values_t      *config = class->config;
  vidix_driver_t       *this;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XWindowAttributes     window_attributes;
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

  this->display           = visual->display;
  this->screen            = visual->screen;
  this->drawable          = visual->d;
 
  vo_scale_init( &this->sc, 1, /*this->vidix_cap.flags & FLAG_UPSCALER,*/ 0 );
  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;
  this->zoom_x = this->zoom_y = 100;
  
  this->config            = config;
  
  this->capabilities      = VO_CAP_YUY2 | VO_CAP_YV12;

  XGetWindowAttributes(this->display, this->drawable, &window_attributes);
  this->sc.gui_width         = window_attributes.width;
  this->sc.gui_height        = window_attributes.height;

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

  printf ("video_out_vidix: warning, xine's vidix driver is EXPERIMENTAL\n");
  return &this->vo_driver;
}

static char* get_identifier (video_driver_class_t *this_gen) {
  return "Vidix";
}

static char* get_description (video_driver_class_t *this_gen) {
  return _("xine video output plugin using libvidix");
}

static void dispose_class (video_driver_class_t *this_gen) {
  vidix_class_t        *this = (vidix_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *visual_gen) {
  vidix_class_t        *this;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XWindowAttributes     window_attributes;
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
  printf("video_out_vidix: Using: %s by %s\n",this->vidix_cap.name,this->vidix_cap.author);

  this->config            = xine->config;
  
  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.get_identifier  = get_identifier;
  this->driver_class.get_description = get_description;
  this->driver_class.dispose         = dispose_class;

  return this;
}

static vo_info_t vo_info_vidix = {
  2,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_OUT, 10, "vidix", XINE_VERSION_CODE, &vo_info_vidix, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
