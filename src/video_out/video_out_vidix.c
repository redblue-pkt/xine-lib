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
 * $Id: video_out_vidix.c,v 1.7 2002/07/15 21:42:34 esnel Exp $
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
#include "vidixlib.h"

#include "video_out.h"
#include "xine_internal.h"
#include "alphablend.h"
#include "xineutils.h"

#include <X11/Xlib.h>
#include "video_out_x11.h"

              
#undef LOG
           

#define NUM_FRAMES 1

typedef struct vidix_driver_s vidix_driver_t;

typedef struct vidix_frame_s {
    vo_frame_t vo_frame;
    int width, height, ratio_code, format;
} vidix_frame_t;


struct vidix_driver_s {

  vo_driver_t        vo_driver;

  config_values_t   *config;

  char               *vidix_name;
  VDL_HANDLE         vidix_handler;
  uint8_t            *vidix_mem;
  vidix_capability_t vidix_cap;
  vidix_playback_t   vidix_play;
  vidix_fourcc_t     vidix_fourcc;
  vidix_yuv_t        dstrides;
  int                vidix_started;
  int                next_frame;

  int                yuv_format;
          
  pthread_mutex_t    mutex;

  int user_ratio;

  uint32_t           capabilities;

   /* X11 / Xv related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;

  /* 
   * "delivered" size:
   * frame dimension / aspect as delivered by the decoder
   * used (among other things) to detect frame size changes
   */
  int                delivered_width;   
  int                delivered_height;     
  int                delivered_ratio_code;
  int                delivered_format;

  /* 
   * displayed part of delivered images,
   * taking zoom into account
   */

  int                displayed_xoffset;
  int                displayed_yoffset;
  int                displayed_width;
  int                displayed_height;
  
  /* 
   * "ideal" size :
   * displayed width/height corrected by aspect ratio
   */

  int                ideal_width, ideal_height;
  double             ratio_factor;         /* output frame must fullfill:
					      height = width * ratio_factor   */

  /*
   * "gui" size / offset:
   * what gui told us about where to display the video
   */
  
  int                gui_x, gui_y;
  int                gui_width, gui_height;
  int                gui_win_x, gui_win_y;
  
  /*
   * "output" size:
   *
   * this is finally the ideal size "fitted" into the
   * gui size while maintaining the aspect ratio
   * 
   */

  /* Window */
  int                output_width;
  int                output_height;
  int                output_xoffset;
  int                output_yoffset;

  /* display anatomy */
  double             display_ratio;        /* given by visual parameter
					      from init function              */

  void              *user_data;

  /* gui callback */

  void (*frame_output_cb) (void *user_data,
			   int video_width, int video_height,
			   int *dest_x, int *dest_y, 
			   int *dest_height, int *dest_width,
			   int *win_x, int *win_y);
};

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
    case IMGFMT_YUY2:
      write_frame_YUY2(this, frame);
/*
      else
	printf("video_out_vidix: error. (YUY2 not supported by your graphic card)\n");	
*/
      break;
      
    case IMGFMT_YV12:
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


static uint32_t vidix_get_capabilities (vo_driver_t *this_gen) {

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
/*
 * make ideal width/height "fit" into the gui
 */

static void vidix_compute_output_size (vidix_driver_t *this) {
  
  double x_factor, y_factor;
  uint32_t apitch;
  int err,i;
  
  if( !this->ideal_width || !this->ideal_height )
    return;
  
  x_factor = (double) this->gui_width  / (double) this->ideal_width;
  y_factor = (double) this->gui_height / (double) this->ideal_height;
  
  if ( x_factor < y_factor ) {
    this->output_width   = (double) this->gui_width;
    this->output_height  = (double) this->ideal_height * x_factor ;
  } else {
    this->output_width   = (double) this->ideal_width  * y_factor ;
    this->output_height  = (double) this->gui_height;
  }

  this->output_xoffset = (this->gui_width - this->output_width) / 2 + this->gui_x;
  this->output_yoffset = (this->gui_height - this->output_height) / 2 + this->gui_y;

#ifdef LOG
  printf ("video_out_vidix: frame source %d x %d => screen output %d x %d\n",
	  this->delivered_width, this->delivered_height,
	  this->output_width, this->output_height);
#endif
  
  if( this->vidix_started ) {
    vdlPlaybackOff(this->vidix_handler);
  }

  memset(&this->vidix_play,0,sizeof(vidix_playback_t));
      
  this->vidix_play.fourcc = this->delivered_format;
  this->vidix_play.capability = this->vidix_cap.flags; /* every ;) */
  this->vidix_play.blend_factor = 0; /* for now */
  this->vidix_play.src.x = this->vidix_play.src.y = 0;
  this->vidix_play.src.w = this->delivered_width;
  this->vidix_play.src.h = this->delivered_height;
  this->vidix_play.dest.x = this->gui_win_x+this->output_xoffset;
  this->vidix_play.dest.y = this->gui_win_y+this->output_yoffset;
  this->vidix_play.dest.w = this->output_width;
  this->vidix_play.dest.h = this->output_height;
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
  this->dstrides.y = (this->delivered_width + apitch) & ~apitch;
  apitch = this->vidix_play.dest.pitch.v-1;
  this->dstrides.v = (this->delivered_width + apitch) & ~apitch;
  apitch = this->vidix_play.dest.pitch.u-1;
  this->dstrides.u = (this->delivered_width + apitch) & ~apitch;
     
  vdlPlaybackOn(this->vidix_handler);
  this->vidix_started = 1;
}

static void vidix_compute_ideal_size (vidix_driver_t *this) {

  double image_ratio, desired_ratio, corr_factor;
  
  this->displayed_xoffset = (this->delivered_width  - this->displayed_width) / 2;
  this->displayed_yoffset = (this->delivered_height - this->displayed_height) / 2;

  /* 
   * aspect ratio
   */

  image_ratio = (double) this->delivered_width / (double) this->delivered_height;
  
  switch (this->user_ratio) {
  case ASPECT_AUTO:
    switch (this->delivered_ratio_code) {
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
      printf ("video_out_vidix: invalid ratio, using 4:3\n");
    default:
      printf ("video_out_vidix: unknown aspect ratio (%d) in stream => using 4:3\n",
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

  corr_factor = this->ratio_factor / image_ratio ;

  if (fabs(corr_factor - 1.0) < 0.005) {
    this->ideal_width  = this->delivered_width;
    this->ideal_height = this->delivered_height;

  } else {

    if (corr_factor >= 1.0) {
      this->ideal_width  = this->delivered_width * corr_factor + 0.5;
      this->ideal_height = this->delivered_height;
    } else {
      this->ideal_width  = this->delivered_width;
      this->ideal_height = this->delivered_height / corr_factor + 0.5;
    }
  }
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
       case IMGFMT_YV12:
	 frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
	 frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
	 frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
	 frame->vo_frame.base[0] = malloc(frame->vo_frame.pitches[0] * height);
         frame->vo_frame.base[1] = malloc(frame->vo_frame.pitches[1] * ((height+1)/2));
	 frame->vo_frame.base[2] = malloc(frame->vo_frame.pitches[2] * ((height+1)/2));
	 break;
       case IMGFMT_YUY2:
	 frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
	 frame->vo_frame.base[0] = malloc(frame->vo_frame.pitches[0] * height);
	 frame->vo_frame.base[1] = NULL;
	 frame->vo_frame.base[2] = NULL;
	 break;
       default:
	 printf("video_out_vidix: error. (unable to allocate framedata because of unknown frame format: %04x)\n", format);
      }
      
      if((format == IMGFMT_YV12 && (frame->vo_frame.base[0] == NULL || frame->vo_frame.base[1] == NULL || frame->vo_frame.base[2] == NULL))
	 || (format == IMGFMT_YUY2 && frame->vo_frame.base[0] == NULL)) {
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
    if( frame->format == IMGFMT_YV12 )
      blend_yuv( frame->vo_frame.base, overlay, frame->width, frame->height);
    else
      blend_yuy2( frame->vo_frame.base[0], overlay, frame->width, frame->height);
  }
}

static int vidix_redraw_needed (vo_driver_t *this_gen) {
  vidix_driver_t  *this = (vidix_driver_t *) this_gen;
  int ret = 0;

  int gui_x, gui_y, gui_width, gui_height, gui_win_x, gui_win_y;
  
  this->frame_output_cb (this->user_data,
			 this->ideal_width, this->ideal_height, 
			 &gui_x, &gui_y, &gui_width, &gui_height,
			 &gui_win_x, &gui_win_y );

  if ( (gui_x != this->gui_x) || (gui_y != this->gui_y)
      || (gui_width != this->gui_width) || (gui_height != this->gui_height)
      || (gui_win_x != this->gui_win_x) || (gui_win_y != this->gui_win_y) ) {

    this->gui_x      = gui_x;
    this->gui_y      = gui_y;
    this->gui_width  = gui_width;
    this->gui_height = gui_height;
    this->gui_win_x  = gui_win_x;
    this->gui_win_y  = gui_win_y;

    vidix_compute_output_size (this);

    ret = 1;
  }
  
  return ret;
}


static void vidix_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  vidix_driver_t  *this = (vidix_driver_t *) this_gen;
  vidix_frame_t   *frame = (vidix_frame_t *) frame_gen;

  pthread_mutex_lock(&this->mutex);

  if ( (frame->width != this->delivered_width)
	 || (frame->height != this->delivered_height)
	 || (frame->ratio_code != this->delivered_ratio_code) 
	 || (frame->format != this->delivered_format ) ) {
	 printf("video_out_vidix: change frame format\n");
      
      this->delivered_width      = frame->width;
      this->delivered_height     = frame->height;
      this->delivered_ratio_code = frame->ratio_code;
      this->delivered_format     = frame->format;

      vidix_compute_ideal_size( this );
      this->gui_width = 0; /* trigger re-calc of output size */
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

static int vidix_get_property (vo_driver_t *this_gen, int property) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;
  
  if ( property == VO_PROP_ASPECT_RATIO)
    return this->user_ratio ;
  
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

static int vidix_set_property (vo_driver_t *this_gen,
			    int property, int value) {

  vidix_driver_t *this = (vidix_driver_t *) this_gen;
  
  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>=NUM_ASPECT_RATIOS)
      value = ASPECT_AUTO;
    this->user_ratio = value;
    printf("video_out_vidix: aspect ratio changed to %s\n",
	   aspect_ratio_name(value));
    
    vidix_compute_ideal_size (this);
  } 
  
  return value;
}

static void vidix_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

/*  vidix_driver_t *this = (vidix_driver_t *) this_gen; */
}

static void vidix_translate_gui2video(vidix_driver_t *this,
				   int x, int y,
				   int *vid_x, int *vid_y)
{
}

static int vidix_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {

  int ret = 0;
  vidix_driver_t     *this = (vidix_driver_t *) this_gen;

  pthread_mutex_lock(&this->mutex);
    
  switch (data_type) {

  case GUI_DATA_EX_DRAWABLE_CHANGED:
#ifdef LOG
      printf ("video_out_vidix: GUI_DATA_EX_DRAWABLE_CHANGED\n");
#endif
    
    this->drawable = (Drawable) data;
    break;
  
  case GUI_DATA_EX_EXPOSE_EVENT:
#ifdef LOG
      printf ("video_out_vidix: GUI_DATA_EX_EXPOSE_EVENT\n");
#endif
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

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  vidix_driver_t          *this;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XWindowAttributes window_attributes;
  int err;
    
  this = malloc (sizeof (vidix_driver_t));

  if (!this) {
    printf ("video_out_vidix: malloc failed\n");
    return NULL;
  }
  memset (this, 0, sizeof(vidix_driver_t));

  
  if(vdlGetVersion() != VIDIX_VERSION)
  {
    printf("video_out_vidix: You have wrong version of VIDIX library\n");
    return NULL;
  }
  this->vidix_handler = vdlOpen((XINE_PLUGINDIR"/vidix/"), NULL, TYPE_OUTPUT, 0);
  if(this->vidix_handler == NULL)
  {
    printf("video_out_vidix: Couldn't find working VIDIX driver\n");
    return NULL;
  }
  if((err=vdlGetCapability(this->vidix_handler,&this->vidix_cap)) != 0)
  {
    printf("video_out_vidix: Couldn't get capability: %s\n",strerror(err));
    return NULL;
  }
  printf("video_out_vidix: Using: %s by %s\n",this->vidix_cap.name,this->vidix_cap.author);

  this->display           = visual->display;
  this->screen            = visual->screen;
  this->display_ratio     = visual->display_ratio;
  this->drawable          = visual->d;
  this->frame_output_cb   = visual->frame_output_cb;
  this->user_data         = visual->user_data;
 
  
  this->config            = config;
  pthread_mutex_init (&this->mutex, NULL);
  
  this->output_xoffset    = 0;
  this->output_yoffset    = 0;
  this->output_width      = 0;
  this->output_height     = 0;
  this->capabilities      = VO_CAP_YUY2 | VO_CAP_YV12;

  XGetWindowAttributes(this->display, this->drawable, &window_attributes);
  this->gui_width         = window_attributes.width;
  this->gui_height        = window_attributes.height;

  
  
  this->vo_driver.get_capabilities     = vidix_get_capabilities;
  this->vo_driver.alloc_frame          = vidix_alloc_frame;
  this->vo_driver.update_frame_format  = vidix_update_frame_format;
  this->vo_driver.overlay_blend        = vidix_overlay_blend;
  this->vo_driver.display_frame        = vidix_display_frame;
  this->vo_driver.get_property         = vidix_get_property;
  this->vo_driver.set_property         = vidix_set_property;
  this->vo_driver.get_property_min_max = vidix_get_property_min_max;
  this->vo_driver.gui_data_exchange    = vidix_gui_data_exchange;
  this->vo_driver.exit                 = vidix_exit;
  this->vo_driver.redraw_needed        = vidix_redraw_needed;

  printf ("video_out_vidix: warning, xine's vidix driver is EXPERIMENTAL\n");
  return &this->vo_driver;
}

static vo_info_t vo_info_vidix = {
  5,
  "vidix",
  NULL,
  VISUAL_TYPE_X11,
  2  
};

vo_info_t *get_video_out_plugin_info() {
  vo_info_vidix.description = _("xine video output plugin using libvidix");
  return &vo_info_vidix;
}


