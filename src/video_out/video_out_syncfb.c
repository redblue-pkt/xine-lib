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
 * $Id: video_out_syncfb.c,v 1.41 2001/11/07 21:02:31 matt2000 Exp $
 * 
 * video_out_syncfb.c, SyncFB (for Matrox G200/G400 cards) interface for xine
 * 
 * based on video_out_xv.c     by (see file for original authors)
 * 
 * with lot's of code from:
 *          video_out_syncfb.c by Joachim Koenig   <joachim.koenig@gmx.net>
 *                         and by Matthias Oelmann <mao@well.com>
 *          video_out_mga      by Aaron Holtzman   <aholtzma@ess.engr.uvic.ca>
 * 
 * tied togehter with lot of glue for xine by Matthias Dahl <matthew2k@web.de>
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h> 
#ifdef __sun
#include <sys/ioccom.h>
#endif
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "video_out_syncfb.h"

#include "monitor.h"
#include "video_out.h"
#include "video_out_x11.h"
#include "xine_internal.h"
#include "alphablend.h"
#include "memcpy.h"

uint32_t xine_debug;

typedef struct {
  int                value;
  int                min;
  int                max;
  char              *key;
} syncfb_property_t;

typedef struct {
  vo_frame_t         vo_frame;
  int                width, height, ratio_code, format, id;
} syncfb_frame_t;

typedef struct {   
  vo_driver_t        vo_driver;
  config_values_t   *config;

  /* X11 related stuff */
  Display           *display;
  Drawable           drawable;
  GC                 gc;
  XColor             black;
  int                screen;
   
  int                virtual_screen_width;
  int                virtual_screen_height;
  int                screen_depth;

  int                video_win_visibility;
   
  syncfb_property_t props[VO_NUM_PROPERTIES];

  vo_overlay_t      *overlay;

  // syncfb module related stuff
  int               fd;              // file descriptor of the syncfb device
  int               yuv_format;      // either YUV420P3, YUV420P2 or YUV422
  int               overlay_state;   // 0 = off, 1 = on
  uint8_t*          video_mem;       // mmapped video memory
  int               default_repeat;  // how many times a frame will be repeatedly displayed
  uint32_t          supported_capabilities;

  syncfb_config_t      syncfb_config;
  syncfb_capability_t  capabilities;
  syncfb_buffer_info_t bufinfo;
   
  /* size / aspect ratio calculations */
  int                delivered_width;      /* everything is set up for
					      these frame dimensions          */
  int                delivered_height;     /* the dimension as they come
					      from the decoder                */
  int                delivered_ratio_code;
  double             ratio_factor;         /* output frame must fullfill:
					      height = width * ratio_factor   */
  int                output_width;         /* frames will appear in this
					      size (pixels) on screen         */
  int                output_height;
  int                output_xoffset;
  int                output_yoffset;

  int                frame_width;
  int                frame_height;
  int                frame_format;

  int                deinterlace_enabled;

  /* display anatomy */
  double             display_ratio;        /* given by visual parameter
					      from init function              */

  void              *user_data;

  /* gui callback */

  void             (*request_dest_size) (void *user_data,
					 int video_width, int video_height,
					 int *dest_x, int *dest_y,
					 int *dest_height, int *dest_width);
} syncfb_driver_t;

int gX11Fail;

//
// internal video_out_syncfb functions
// 

// returns boolean value (1 success, 0 failure)
int syncfb_overlay_on(syncfb_driver_t* this)
{  
   if(ioctl(this->fd, SYNCFB_ON)) {	
      printf("video_out_syncfb: error. (on ioctl failed)\n");
      return 0;
   } else {
      this->overlay_state = 1;
      return 1;
   }
}

// returns boolean value (1 success, 0 failure)
int syncfb_overlay_off(syncfb_driver_t* this)
{  
   if(ioctl(this->fd, SYNCFB_OFF)) {
      printf("video_out_syncfb: error. (off ioctl failed)\n");
      return 0;
   } else {
      this->overlay_state = 0;
      return 1;
   }
}

static void write_frame_YUV422(syncfb_driver_t* this, syncfb_frame_t* frame)
{
   uint_8*  y  = (uint_8 *)frame->vo_frame.base[0];
   uint_8*  cb = (uint_8 *)frame->vo_frame.base[1];
   uint_8*  cr = (uint_8 *)frame->vo_frame.base[2];
   uint_8*  crp;
   uint_8*  cbp;
   uint_32* dst32 = (uint_32 *)(this->video_mem + this->bufinfo.offset);
   int h,w;
   int bespitch = (frame->width + 31) & ~31; 

   for(h = 0; h < (frame->height / 2); h++) {
      cbp = cb;
      crp = cr;
      
      for(w = 0; w < (frame->width / 2); w++) {
	 *dst32++ = (*y) + ((*cb)<<8) + ((*(y+1))<<16) + ((*cr)<<24);
	 y++; y++; cb++; cr++;
      }

      dst32 += (bespitch - frame->width) / 2;

      for(w=0; w < (frame->width / 2); w++) {
	 *dst32++ = (*y) + ((*cbp)<<8) + ((*(y+1))<<16) + ((*crp)<<24);
	 y++; y++; cbp++; crp++;
      }
      
      dst32 += (bespitch - frame->width) / 2;
   }
}

static void write_frame_YUV420P2(syncfb_driver_t* this, syncfb_frame_t* frame)
{   
   uint_8*  y    = (uint_8 *)frame->vo_frame.base[0];
   uint_8*  cb   = (uint_8 *)frame->vo_frame.base[1];
   uint_8*  cr   = (uint_8 *)frame->vo_frame.base[2];
   uint_8*  dst8 = this->video_mem + this->bufinfo.offset_p2;
   int h, w;
   int bespitch = (frame->width + 31) & ~31; 
   
   register uint_32 *tmp32;
   register uint_8  *rcr;
   register uint_8  *rcb;

   rcr = cr;
   rcb = cb;
	
   for(h = 0; h < (frame->height / 2); h++) {
      tmp32 = (uint_32 *)dst8;
      w = (frame->width / 8) * 2;
      
      while(w--) {
	 register uint_32 temp;
	 
	 temp = (*rcb) | (*rcr << 8);
	 rcr++;
	 rcb++;
	 temp |= (*rcb << 16) | (*rcr << 24);
	 rcr++;
	 rcb++;
	 *tmp32 = temp;
	 tmp32++;
      }
      
      dst8 += bespitch;
   }

   dst8 = this->video_mem + this->bufinfo.offset;
   for(h = 0; h < frame->height; h++) {
      fast_memcpy(dst8, y, frame->width);
      y    += frame->width;
      dst8 += bespitch;
   }
}

static void write_frame_YUV420P3(syncfb_driver_t* this, syncfb_frame_t* frame)
{   
   uint_8*  y    = (uint_8 *)frame->vo_frame.base[0];
   uint_8*  cb   = (uint_8 *)frame->vo_frame.base[1];
   uint_8*  cr   = (uint_8 *)frame->vo_frame.base[2];
   uint_8*  dst8 = this->video_mem + this->bufinfo.offset;
   int h;
   int bespitch = (frame->width + 31) & ~31; 

   for(h = 0; h < frame->height; h++) {
      fast_memcpy(dst8, y, frame->width);
      y    += frame->width;
      dst8 += bespitch;
   }

   dst8 = this->video_mem + this->bufinfo.offset_p2;
   for(h = 0; h < (frame->height / 2); h++) {
      fast_memcpy(dst8, cb, (frame->width / 2));
      cb   += (frame->width / 2);
      dst8 += (bespitch / 2);
   }

   dst8 = this->video_mem + this->bufinfo.offset_p3;
   for(h=0; h < (frame->height / 2); h++) {
      fast_memcpy(dst8, cr, (frame->width / 2));
      cr   += (frame->width / 2);
      dst8 += (bespitch / 2);
   }
}

static void write_frame_YUY2(syncfb_driver_t* this, syncfb_frame_t* frame)
{   
   uint_8* src8 = (uint_8 *)frame->vo_frame.base[0];
   uint_8* dst8 = (uint_8 *)(this->video_mem + this->bufinfo.offset);
   int h;
   int bespitch = (frame->width + 31) & ~31; 

   for(h = 0; h < frame->height; h++) {
      fast_memcpy(dst8, src8, (frame->width * 2));

      dst8 += (bespitch * 2);
      src8 += (frame->width * 2);
   }
}

static void write_frame_sfb(syncfb_driver_t* this, syncfb_frame_t* frame)
{
   switch(frame->format) {
      
    case IMGFMT_YUY2:
      if(this->capabilities.palettes & (1<<VIDEO_PALETTE_YUV422))
	write_frame_YUY2(this, frame);
      else
	 printf("video_out_syncfb: error. (YUY2 not supported by your graphic card)\n");	
      break;
      
    case IMGFMT_YV12:
      if(this->yuv_format == VIDEO_PALETTE_YUV422) {
	 write_frame_YUV422(this, frame);
      } else if(this->yuv_format == VIDEO_PALETTE_YUV420P2) {
	 write_frame_YUV420P2(this, frame);
      } else if(this->yuv_format == VIDEO_PALETTE_YUV420P3) {
	 write_frame_YUV420P3(this, frame);
      } else {
	 printf("video_out_syncfb: error. (YV12 not supported by your graphic card)\n");
      }	   
      break;
      
    case IMGFMT_RGB:
      printf("video_out_syncfb: error. (RGB565 not yet supported)\n");
      break;
      
    default:
      printf("video_out_syncfb: error. (unknown frame format)\n");
      break;
   }
}

static void syncfb_adapt_to_output_area(syncfb_driver_t* this,
					int dest_x, int dest_y,
					int dest_width, int dest_height)
{
   XWindowAttributes window_attributes;
   Window            temp_window;
   
   int posx, posy;
   
   static int prev_output_width   = 0;
   static int prev_output_height  = 0;
   static int prev_output_xoffset = 0;
   static int prev_output_yoffset = 0;
   static int prev_deinterlacing  = 0;
   static int prev_posx           = 0;
   static int prev_posy           = 0;
   static int prev_v_w_visibility = 0;

   XLockDisplay(this->display);

   XGetWindowAttributes(this->display, this->drawable, &window_attributes);

   if(!this->video_win_visibility || window_attributes.map_state == IsUnmapped || window_attributes.map_state == IsUnviewable)
     posx = posy = -1;
   else
     XTranslateCoordinates(this->display, this->drawable, window_attributes.root, 0, 0, &posx, &posy, &temp_window);
   
   if(((double) dest_width / this->ratio_factor) < dest_height) {
      this->output_width   = dest_width;
      this->output_height  = (double) dest_width / this->ratio_factor;
      this->output_xoffset = dest_x;
      this->output_yoffset = dest_y + (dest_height - this->output_height) / 2;
   } else {
     this->output_width    = (double) dest_height * this->ratio_factor;
     this->output_height   = dest_height;
     this->output_xoffset  = dest_x + (dest_width - this->output_width) / 2;
     this->output_yoffset  = dest_y;
   }

   // try to minimize our config ioctls by checking if anything really has
   // changed, otherwise leave things untouched because every config ioctl
   // also turns off and on the SyncFB module.
   if(prev_output_width   != this->output_width        ||
      prev_output_height  != this->output_height       ||
      prev_output_xoffset != this->output_xoffset      ||
      prev_output_yoffset != this->output_yoffset      ||
      prev_deinterlacing  != this->deinterlace_enabled ||
      prev_posx           != posx                      ||
      prev_posy           != posy                      ||
      prev_v_w_visibility != this->video_win_visibility) {	
      
      prev_output_width   = this->output_width;
      prev_output_height  = this->output_height;
      prev_output_xoffset = this->output_xoffset;
      prev_output_yoffset = this->output_yoffset;
      prev_deinterlacing  = this->deinterlace_enabled;
      prev_posx           = posx;
      prev_posy           = posy;
      prev_v_w_visibility = this->video_win_visibility;
	
      //
      // configuring SyncFB module from this point on.
      //
      syncfb_overlay_off(this);

      // sanity checking - certain situations *may* crash the SyncFB module, so
      // take care that we always have valid numbers.
      if(posx >= 0 && posy >= 0 && this->frame_width > 0 && this->frame_height > 0 && this->output_width > 0 && this->output_height > 0 && this->frame_format > 0) {
	 if(ioctl(this->fd, SYNCFB_GET_CONFIG, &this->syncfb_config))
	   printf("video_out_syncfb: error. (get_config ioctl failed)\n");
	
	 this->syncfb_config.syncfb_mode = SYNCFB_FEATURE_SCALE;
	 if(this->deinterlace_enabled) {
	    this->syncfb_config.syncfb_mode |= SYNCFB_FEATURE_DEINTERLACE | SYNCFB_FEATURE_CROP;
	    this->syncfb_config.src_crop_top   = 0;
	    this->syncfb_config.src_crop_bot   = 1;
	    this->syncfb_config.src_crop_left  = 0;
	    this->syncfb_config.src_crop_right = 0;
	 }
	 
	 switch(this->frame_format) {
	  case IMGFMT_YV12:
	    this->syncfb_config.src_palette = this->yuv_format;
	    break;
	    
	  case IMGFMT_YUY2:
	    this->syncfb_config.src_palette = VIDEO_PALETTE_YUV422;
	    break;
	    
	  case IMGFMT_RGB:
	    this->syncfb_config.src_palette = VIDEO_PALETTE_RGB565;
	    break;
	    
	  default:
	    this->syncfb_config.src_palette = 0;
	    break;
	 }
	 
	 this->syncfb_config.fb_screen_size = this->virtual_screen_width * this->virtual_screen_height * (this->screen_depth / 8);
	 this->syncfb_config.src_width      = this->frame_width;
	 this->syncfb_config.src_height     = this->frame_height;

	 this->syncfb_config.image_width    = this->output_width;
	 this->syncfb_config.image_height   = (this->deinterlace_enabled) ? (this->output_height-2) : this->output_height;

	 this->syncfb_config.image_xorg     = posx+this->output_xoffset;
	 this->syncfb_config.image_yorg     = posy+this->output_yoffset;

	 this->syncfb_config.default_repeat   = (this->deinterlace_enabled) ? 1 : this->default_repeat;

	 if(this->capabilities.palettes & (1<<this->syncfb_config.src_palette)) {
	    if(ioctl(this->fd,SYNCFB_SET_CONFIG,&this->syncfb_config))
	      printf("video_out_syncfb: error. (set_config ioctl failed)\n");
	    
	    syncfb_overlay_on(this);
	 }
      }
   }
   
  /*
   * clear unused output area
   */

  XSetForeground (this->display, this->gc, this->black.pixel);

  XFillRectangle(this->display, this->drawable, this->gc,
		 dest_x, dest_y, dest_width, this->output_yoffset - dest_y);

  XFillRectangle(this->display, this->drawable, this->gc, 
		 dest_x, dest_y, this->output_xoffset-dest_x, dest_height);

  XFillRectangle(this->display, this->drawable, this->gc,
		 dest_x, this->output_yoffset+this->output_height,
		 dest_width,
		 dest_height - this->output_yoffset - this->output_height);

  XFillRectangle(this->display, this->drawable, this->gc, 
		 this->output_xoffset+this->output_width, dest_y, 
		 dest_width - this->output_xoffset - this->output_width,
		 dest_height);
   
  XUnlockDisplay (this->display);
}

static void syncfb_calc_format(syncfb_driver_t* this,
			       int width, int height, int ratio_code) {

  double image_ratio, desired_ratio;
  double corr_factor;
  int ideal_width, ideal_height;
  int dest_x, dest_y, dest_width, dest_height;

  this->delivered_width      = width;
  this->delivered_height     = height;
  this->delivered_ratio_code = ratio_code;

  if((!width) || (!height))
    return;

  /*
   * aspect ratio calculation
   */

  image_ratio =
    (double) this->delivered_width / (double) this->delivered_height;

  xprintf (VERBOSE | VIDEO, "display_ratio : %f\n", this->display_ratio);
  xprintf (VERBOSE | VIDEO, "stream aspect ratio : %f , code : %d\n",
	   image_ratio, ratio_code);

  switch (this->props[VO_PROP_ASPECT_RATIO].value) {
  case ASPECT_AUTO:
    switch (ratio_code) {
    case XINE_ASPECT_RATIO_ANAMORPHIC:  /* anamorphic     */
      desired_ratio = 16.0 /9.0;
      break;
    case XINE_ASPECT_RATIO_211_1:        /* 2.11:1 */
      desired_ratio = 2.11/1.0;
      break;
    case XINE_ASPECT_RATIO_SQUARE:       /* "square" source pels */
    case XINE_ASPECT_RATIO_DONT_TOUCH:   /* probably non-mpeg stream => don't touch aspect ratio */
      desired_ratio = image_ratio;
      break;
    case 0:                              /* forbidden       */
      fprintf (stderr, "invalid ratio, using 4:3\n");
    default:
      xprintf (VIDEO, "unknown aspect ratio (%d) in stream => using 4:3\n",
	       ratio_code);
    case XINE_ASPECT_RATIO_4_3:          /* 4:3             */
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

  if (corr_factor >= 1.0) {
    ideal_width  = this->delivered_width * corr_factor;
    ideal_height = this->delivered_height ;
  }
  else {
    ideal_width  = this->delivered_width;
    ideal_height = this->delivered_height / corr_factor;
  }

  /* little hack to zoom mpeg1 / other small streams by default*/
  if(ideal_width<400) {
    ideal_width  *=2;
    ideal_height *=2;
  }

  /*
   * ask gui to adapt to this size
   */

  this->request_dest_size (this->user_data,
			   ideal_width, ideal_height,
			   &dest_x, &dest_y, &dest_width, &dest_height);

  syncfb_adapt_to_output_area(this, dest_x, dest_y, dest_width, dest_height);
}

static void syncfb_translate_gui2video(syncfb_driver_t* this,
		                       int x, int y,
				       int* vid_x, int* vid_y)
{
  if (this->output_width > 0 && this->output_height > 0) {
    /*
     * 1.
     * the xv driver may center a small output area inside a larger
     * gui area.  This is the case in fullscreen mode, where we often
     * have black borders on the top/bottom/left/right side.
     */
    x -= this->output_xoffset;
    y -= this->output_yoffset;

    /*
     * 2.
     * the xv driver scales the delivered area into an output area.
     * translate output area coordianates into the delivered area
     * coordiantes.
     */
    x = x * this->delivered_width  / this->output_width;
    y = y * this->delivered_height / this->output_height;
  }

  *vid_x = x;
  *vid_y = y;
}


//
// X error handler functions
// (even though the syncfb plugin doesn't check for gX11Fail yet, it is
//  probably a good idea to leave this in place for future use)
//
int HandleXError(Display* display, XErrorEvent* xevent) {

  char str [1024];

  XGetErrorText (display, xevent->error_code, str, 1024);

  printf ("received X error event: %s\n", str);

  gX11Fail = 1;
  return 0;
}

static void x11_InstallXErrorHandler(syncfb_driver_t* this)
{
  XSetErrorHandler (HandleXError);
  XFlush (this->display);
}

static void x11_DeInstallXErrorHandler(syncfb_driver_t* this)
{
  XSetErrorHandler (NULL);
  XFlush (this->display);
}

//
// video_out_syncfb functions available to the outside world :)
// 
static uint32_t syncfb_get_capabilities(vo_driver_t* this_gen) {
  // FIXME: VO_CAP_CONTRAST and VO_CAP_BRIGHTNESS unsupported at the moment,
  //        because they seem to be disabled in the syncfb module anyway. :(
  syncfb_driver_t* this = (syncfb_driver_t *) this_gen;
  
  return this->supported_capabilities;
}

static void syncfb_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed for Xv */
}

static void syncfb_frame_dispose(vo_frame_t* vo_img)
{
  syncfb_frame_t*  frame = (syncfb_frame_t *) vo_img ;
   
   if(frame->vo_frame.base[0]) {
      shmdt(frame->vo_frame.base[0]);
      shmctl(frame->id,IPC_RMID,NULL);
      frame->vo_frame.base[0] = NULL;
   }

  free (frame);
}

static vo_frame_t* syncfb_alloc_frame(vo_driver_t* this_gen)
{
   syncfb_frame_t*  frame;

   frame = (syncfb_frame_t *) malloc(sizeof (syncfb_frame_t));
   memset(frame, 0, sizeof(syncfb_frame_t));

   if(frame == NULL) {
      printf ("video_out_syncfb: error. (memory allocating of frame failed)\n");
   }

   pthread_mutex_init (&frame->vo_frame.mutex, NULL);

   /*
    * supply required functions
    */

   frame->vo_frame.copy    = NULL;
   frame->vo_frame.field   = syncfb_frame_field;
   frame->vo_frame.dispose = syncfb_frame_dispose;

   return (vo_frame_t *) frame;
}

static void syncfb_update_frame_format(vo_driver_t* this_gen,
				       vo_frame_t* frame_gen,
				       uint32_t width, uint32_t height,
				       int ratio_code, int format, int flags)
{
   syncfb_frame_t* frame = (syncfb_frame_t *) frame_gen;
   
   if((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

      if(frame->vo_frame.base[0]) {
	 shmdt(frame->vo_frame.base[0]);
	 shmctl(frame->id,IPC_RMID,NULL);
	 frame->vo_frame.base[0] = NULL;
      }
      
      frame->width  = width;
      frame->height = height;
      frame->format = format;
      
      // we only know how to do 4:2:0 planar yuv right now.
      // we prepare for YUY2 sizes
      frame->id = shmget(IPC_PRIVATE, frame->width * frame->height * 2, IPC_CREAT | 0777);
      
      if(frame->id < 0 ) {
	 printf("video_out_syncfb: aborted. (shared memory error in shmget)\n");
	 exit(1);
      }
      
      frame->vo_frame.base[0] = shmat(frame->id, 0, 0);   
      
      if(frame->vo_frame.base[0] == NULL) {
	 printf("video_out_syncfb: failed. (shared memory error => address error NULL)\n");
	 exit(1);
      }
  
      if(frame->vo_frame.base[0] == (void *) -1) {
	 fprintf(stderr, "syncfb: shared memory error (address error)\n");
	 exit (1);
      }
      
      shmctl(frame->id, IPC_RMID, 0);
      
      frame->vo_frame.base[1] = frame->vo_frame.base[0] + width * height * 5 / 4;
      frame->vo_frame.base[2] = frame->vo_frame.base[0] + width * height;
   }

   frame->ratio_code = ratio_code;
}

static void syncfb_overlay_blend(vo_driver_t* this_gen, vo_frame_t* frame_gen, vo_overlay_t* overlay)
{
  syncfb_frame_t* frame = (syncfb_frame_t *) frame_gen;

  if(overlay->rle) {
    blend_yuv_vo_frame(&frame->vo_frame, overlay);
  }
}

static void syncfb_display_frame(vo_driver_t* this_gen, vo_frame_t* frame_gen)
{
   syncfb_driver_t* this = (syncfb_driver_t *) this_gen;
   syncfb_frame_t* frame = (syncfb_frame_t *) frame_gen;

   if((frame->width != this->frame_width) || (frame->height != this->frame_height) || (frame->format != this->frame_format)) {
      this->frame_height = frame->height;
      this->frame_width  = frame->width;
      this->frame_format = frame->format;
   }
   
   if((frame->width != this->delivered_width)
      || (frame->height != this->delivered_height)
      || (frame->ratio_code != this->delivered_ratio_code) ) {
      syncfb_calc_format(this, frame->width, frame->height, frame->ratio_code);
   }
   
   // the rest is only successful and safe, if the overlay is really on
   if(this->overlay_state) {
      if(this->bufinfo.id != -1) {
	 printf("video_out_syncfb: error. (invalid syncfb image buffer state)\n");
	 return;
      }

      if(ioctl(this->fd, SYNCFB_REQUEST_BUFFER, &this->bufinfo))
	printf("video_out_syncfb: error. (request ioctl failed)\n");
   
      if(this->bufinfo.id == -1) {
	 printf("video_out_syncfb: error. (syncfb module couldn't allocate image buffer)\n");
	 frame->vo_frame.displayed(&frame->vo_frame);
      
	 return;
      }

      write_frame_sfb(this, frame);
   
      if(ioctl(this->fd, SYNCFB_COMMIT_BUFFER, &this->bufinfo))
	printf("video_out_syncfb: error. (commit ioctl failed)\n");
   }
   
   frame->vo_frame.displayed(&frame->vo_frame);
   this->bufinfo.id = -1;  
}

static int syncfb_get_property(vo_driver_t* this_gen, int property)
{
  syncfb_driver_t* this = (syncfb_driver_t *) this_gen;
  
  return this->props[property].value;
}

static int syncfb_set_property(vo_driver_t* this_gen, int property, int value)
{
  syncfb_driver_t* this = (syncfb_driver_t *) this_gen;
  
  switch (property) {
   case VO_PROP_INTERLACED:
      this->props[property].value = value;
      printf("video_out_syncfb: VO_PROP_INTERLACED(%d)\n",
	     this->props[property].value);
      this->deinterlace_enabled = value;
      syncfb_calc_format(this, this->delivered_width, this->delivered_height, this->delivered_ratio_code);
      break;
   case VO_PROP_ASPECT_RATIO:
      if(value>=NUM_ASPECT_RATIOS)
	value = ASPECT_AUTO;

      this->props[property].value = value;
      printf("video_out_syncfb: VO_PROP_ASPECT_RATIO(%d)\n",
	     this->props[property].value);

      syncfb_calc_format(this, this->delivered_width, this->delivered_height,
	                 this->delivered_ratio_code);
      break;
  }

  return value;
}

static void syncfb_get_property_min_max (vo_driver_t *this_gen, int property, int *min, int *max)
{
  syncfb_driver_t *this = (syncfb_driver_t *) this_gen;

  *min = this->props[property].min;
  *max = this->props[property].max;
}

static int syncfb_gui_data_exchange (vo_driver_t* this_gen, int data_type, void *data)
{
  syncfb_driver_t* this = (syncfb_driver_t *) this_gen;
  x11_rectangle_t* area;

  switch (data_type) {
   case GUI_DATA_EX_DEST_POS_SIZE_CHANGED: {
     area = (x11_rectangle_t *) data;
     
     syncfb_adapt_to_output_area(this, area->x, area->y, area->w, area->h);
   }     
     break;

  // FIXME: consider if this is of use for us...
  case GUI_DATA_EX_EXPOSE_EVENT:
     break;

  case GUI_DATA_EX_DRAWABLE_CHANGED:
    this->drawable = (Drawable) data;
    this->gc       = XCreateGC (this->display, this->drawable, 0, NULL);
    break;

  case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO: {
    int x1, y1, x2, y2;
    x11_rectangle_t *rect = data;

    syncfb_translate_gui2video(this, rect->x, rect->y,
                               &x1, &y1);
    syncfb_translate_gui2video(this, rect->x + rect->w, rect->y + rect->h,
	                       &x2, &y2);
    rect->x = x1;
    rect->y = y1;
    rect->w = x2-x1;
    rect->h = y2-y1;
  }
    break;

  case GUI_DATA_EX_VIDEOWIN_VISIBLE: {
     this->video_win_visibility = (int)(int *)data;
     
     syncfb_adapt_to_output_area(this, this->output_xoffset, this->output_yoffset, this->output_width, this->output_height);
  }
    break;
	  
  default:
    return -1;
  }

  return 0;
}

static void syncfb_exit (vo_driver_t* this_gen)
{
   syncfb_driver_t *this = (syncfb_driver_t *) this_gen;

   // get it off the screen - I wanna see my desktop again :-)
   syncfb_overlay_off(this);
   
   // don't know if it is necessary are even right, but anyway...?!
   munmap(0, this->capabilities.memory_size);   
   
   close(this->fd);
}

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen)
{
   XWindowAttributes attr;
   XColor dummy;
   
   syncfb_driver_t*  this;
   x11_visual_t*     visual = (x11_visual_t *) visual_gen;

   int               i = 0;
   char*             device_name;
   
   device_name = config->lookup_str(config, "syncfb_device", "/dev/syncfb");
   xine_debug  = config->lookup_int(config, "xine_debug", 0);
   
   if(!(this = malloc (sizeof (syncfb_driver_t)))) {
      printf("video_out_syncfb: aborting. (malloc failed)\n");
      return NULL;
   }
   memset(this, 0, sizeof(syncfb_driver_t));
 
   // check for syncfb device
   if((this->fd = open(device_name, O_RDWR)) < 0) {
      printf("video_out_syncfb: aborting. (unable to open device \"%s\")\n", device_name);
      free(this);
      return NULL;
   }
   
   // get capabilities from the syncfb module
   if(ioctl(this->fd, SYNCFB_GET_CAPS, &this->capabilities)) {
      printf("video_out_syncfb: aborting. (syncfb_get_caps ioctl failed)\n");
      
      close(this->fd);
      free(this);
      
      return NULL;
   }

   // mmap whole video memory
   this->video_mem = (char *) mmap(0, this->capabilities.memory_size, PROT_WRITE, MAP_SHARED, this->fd, 0);
     
   // check for formats we need...
   this->supported_capabilities = 0;
   this->yuv_format = 0;

   // simple fallback mechanism - we want YUV 4:2:0 (3 plane) but we can also
   // convert YV12 material to YUV 4:2:0 (2 plane) and YUV 4:2:2 ...
   if(this->capabilities.palettes & (1<<VIDEO_PALETTE_YUV420P3)) {
      this->supported_capabilities |= VO_CAP_YV12;
      this->yuv_format = VIDEO_PALETTE_YUV420P3;
      printf("video_out_syncfb: SyncFB module supports YUV 4:2:0 (3 plane).\n");
   } else if(this->capabilities.palettes & (1<<VIDEO_PALETTE_YUV420P2)) {
      this->supported_capabilities |= VO_CAP_YV12;
      this->yuv_format = VIDEO_PALETTE_YUV420P2;
      printf("video_out_syncfb: SyncFB module supports YUV 4:2:0 (2 plane)\n");
   } else if(this->capabilities.palettes & (1<<VIDEO_PALETTE_YUV422)) {	   
      this->supported_capabilities |= VO_CAP_YV12;
      this->yuv_format = VIDEO_PALETTE_YUV422;
      printf("video_out_syncfb: SyncFB module supports YUV 4:2:2.\n");   
   }
   
   if(this->capabilities.palettes & (1<<VIDEO_PALETTE_YUV422)) {
      this->supported_capabilities |= VO_CAP_YUY2;
      printf("video_out_syncfb: SyncFB module supports YUY2.\n");
   }
   if(this->capabilities.palettes & (1<<VIDEO_PALETTE_RGB565)) {
// FIXME: no RGB support yet
//      this->supported_capabilities |= VO_CAP_RGB;
      printf("video_out_syncfb: SyncFB module supports RGB565.\n");
   }
   
   if(!this->supported_capabilities) {
      printf("video_out_syncfb: aborting. (SyncFB module does not support YV12, YUY2 nor RGB565)\n");
      
      close(this->fd);
      free(this);
      
      return NULL;
   }
   
  XGetWindowAttributes(visual->display, DefaultRootWindow(visual->display), &attr);
   
  this->bufinfo.id            = -1;   
  this->config                = config;
  this->default_repeat        = config->lookup_int(config, "syncfb_default_repeat", 3);
  this->display               = visual->display;
  this->display_ratio         = visual->display_ratio;
  this->drawable              = visual->d;
  this->frame_format          = 0;
  this->frame_height          = 0;
  this->frame_width           = 0;
  this->gc                    = XCreateGC (this->display, this->drawable, 0, NULL);
  this->output_height         = 0;
  this->output_width          = 0;
  this->output_xoffset        = 0;
  this->output_yoffset        = 0;
  this->overlay               = NULL;
  this->overlay_state         = 0;
  this->request_dest_size     = visual->request_dest_size;
  this->screen                = visual->screen;
  this->screen_depth          = attr.depth;
  this->user_data             = visual->user_data;
  this->video_win_visibility  = 1;
  this->virtual_screen_height = attr.height;
  this->virtual_screen_width  = attr.width;

  XAllocNamedColor(this->display,
		   DefaultColormap(this->display, this->screen),
		   "black", &this->black, &dummy);

  this->vo_driver.get_capabilities     = syncfb_get_capabilities;
  this->vo_driver.alloc_frame          = syncfb_alloc_frame;
  this->vo_driver.update_frame_format  = syncfb_update_frame_format;
  this->vo_driver.overlay_blend        = syncfb_overlay_blend;
  this->vo_driver.display_frame        = syncfb_display_frame;
  this->vo_driver.get_property         = syncfb_get_property;
  this->vo_driver.set_property         = syncfb_set_property;
  this->vo_driver.get_property_min_max = syncfb_get_property_min_max;
  this->vo_driver.gui_data_exchange    = syncfb_gui_data_exchange;
  this->vo_driver.exit                 = syncfb_exit;

  this->deinterlace_enabled = 0;
   
  /*
   * init properties
   */
   
   for(i = 0; i < VO_NUM_PROPERTIES; i++) {
      this->props[i].value = 0;
      this->props[i].min   = 0;
      this->props[i].max   = 0;
      this->props[i].key   = NULL;
   }
   
  return &this->vo_driver;
}

static vo_info_t vo_info_syncfb = {
  2,
  "SyncFB",
  "xine video output plugin using the SyncFB module for Matrox G200/G400 cards",
  VISUAL_TYPE_X11,
  10
};

vo_info_t *get_video_out_plugin_info()
{
  return &vo_info_syncfb;
}
