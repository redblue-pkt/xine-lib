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
 * $Id: video_out_xshm.c,v 1.46 2001/10/14 23:19:59 f1rmb Exp $
 * 
 * video_out_xshm.c, X11 shared memory extension interface for xine
 *
 * based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * xine-specific code by Guenter Bartsch <bartscgr@studbox.uni-stuttgart.de>
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "video_out.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <errno.h>

#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <pthread.h>
#include <netinet/in.h>

#include "xine_internal.h"
#include "monitor.h"
#include "utils.h"
#include "video_out_x11.h"
#include "alphablend.h"
#include "yuv2rgb.h"

uint32_t xine_debug;

extern int XShmGetEventBase(Display *);

typedef struct xshm_frame_s {
  vo_frame_t         vo_frame;

  int                width, height;
  int                rgb_width, rgb_height;
  Drawable	     drawable_ref;

  XImage            *image;
  uint8_t           *rgb_dst;
  int                stripe_inc;
  XShmSegmentInfo    shminfo;

  int                format;

  uint8_t           *chunk[3]; /* mem alloc by xmalloc_aligned */
} xshm_frame_t;

typedef struct xshm_driver_s {

  vo_driver_t      vo_driver;

  config_values_t *config;

  /* X11 / Xv related stuff */
  Display         *display;
  int              screen;
  Drawable         drawable;
  Visual	  *visual;
  GC               gc;
  int              use_shm;
  int              zoom_mpeg1;
  int		   scaling_disabled;
  int              depth, bpp, bytes_per_pixel, image_byte_order;
  int              expecting_event;
  uint8_t	  *fast_rgb;

  yuv2rgb_t       *yuv2rgb;

  xshm_frame_t    *cur_frame;
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
  Drawable	   last_frame_drawable_ref;

  int              gui_width;		 /* size of gui window */
  int              gui_height;
  int              gui_changed;
  int              dest_x;
  int              dest_y;

  /* display anatomy */
  double           display_ratio;        /* given by visual parameter from init function */

  /* profiler */
  int		   prof_yuv2rgb;

  /* gui callbacks */

  void (*request_dest_size) (int video_width, int video_height,
			     int *dest_x, int *dest_y, 
			     int *dest_height, int *dest_width);

  void (*calc_dest_size) (int video_width, int video_height, 
			  int *dest_width, int *dest_height);

} xshm_driver_t;

/* possible values for xshm_driver_t, field gui_changed */
#define	GUI_SIZE_CHANGED	1
#define	GUI_ASPECT_CHANGED	2


int gX11Fail;

/*
 * first, some utility functions
 */

int HandleXError (Display *display, XErrorEvent *xevent) {
  
  char str [1024];

  XGetErrorText (display, xevent->error_code, str, 1024);

  printf ("received X error event: %s\n", str);

  gX11Fail = 1;
  return 0;

}

static void x11_InstallXErrorHandler (xshm_driver_t *this)
{
  XSetErrorHandler (HandleXError);
  XFlush (this->display);
}

static void x11_DeInstallXErrorHandler (xshm_driver_t *this)
{
  XSetErrorHandler (NULL);
  XFlush (this->display);
}

static void *my_malloc_aligned (size_t alignment, size_t size, uint8_t **chunk) {

  uint8_t *pMem;

  pMem = xmalloc (size+alignment);

  *chunk = pMem;

  while ((int) pMem % alignment)
    pMem++;

  return pMem;
}

/*
 * allocate an XImage, try XShm first but fall back to 
 * plain X11 if XShm should fail
 */

static XImage *create_ximage (xshm_driver_t *this, XShmSegmentInfo *shminfo, 
			      int width, int height) {

  XImage *myimage = NULL;

  if (this->use_shm) {

    /*
     * try shm
     */
    
    gX11Fail = 0;
    x11_InstallXErrorHandler (this);

    myimage = XShmCreateImage(this->display, 
			      this->visual,
			      this->depth,
			      ZPixmap, NULL,
			      shminfo,
			      width, 
			      height);

    if (myimage == NULL )  {
      printf ("video_out_xshm: shared memory error when allocating image\n");
      printf ("video_out_xshm: => not using MIT Shared Memory extension.\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }
  
    this->bpp = myimage->bits_per_pixel;
    this->bytes_per_pixel = this->bpp / 8;
    this->image_byte_order = myimage->byte_order;
    
    shminfo->shmid=shmget(IPC_PRIVATE,
			  myimage->bytes_per_line * myimage->height, 
			  IPC_CREAT | 0777);
    
    if (shminfo->shmid < 0 ) {
      printf ("video_out_xshm: %s: allocating image\n",strerror(errno));
      printf ("video_out_xshm: => not using MIT Shared Memory extension.\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }
  
    shminfo->shmaddr  = (char *) shmat(shminfo->shmid, 0, 0);
  
    if (shminfo->shmaddr == ((char *) -1)) {
      printf ("video_out_xshm: shared memory error (address error) when allocating image \n");
      printf ("video_out_xshm: => not using MIT Shared Memory extension.\n");
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
      this->use_shm = 0;
      goto finishShmTesting;
    }
  
    shminfo->readOnly = False;
    myimage->data = shminfo->shmaddr;
  
    XShmAttach(this->display, shminfo);
  
    XSync(this->display, False);

    if (gX11Fail) {
      printf ("video_out_xshm: x11 error during shared memory XImage creation\n");
      printf ("video_out_xshm: => not using MIT Shared Memory extension.\n");
      shmdt (shminfo->shmaddr);
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
      this->use_shm = 0;
      goto finishShmTesting;
    }

    /* 
     * Now that the Xserver has learned about and attached to the
     * shared memory segment,  delete it.  It's actually deleted by
     * the kernel when all users of that segment have detached from 
     * it.  Gives an automatic shared memory cleanup in case we crash.
     */
    shmctl (shminfo->shmid, IPC_RMID, 0);
    shminfo->shmid = -1;

  finishShmTesting:
    x11_DeInstallXErrorHandler(this);

  }

  /*
   * fall back to plain X11 if necessary
   */

  if (!this->use_shm) {

    myimage = XCreateImage (this->display,
			    this->visual,
			    this->depth,
			    ZPixmap, 0,
			    NULL,
			    width, 
			    height,
			    8, 0);

    this->bpp = myimage->bits_per_pixel;
    this->bytes_per_pixel = this->bpp / 8;
    this->image_byte_order = myimage->byte_order;

    myimage->data = xmalloc (width * this->bytes_per_pixel * height);
  }

  return myimage;

}

static void dispose_ximage (xshm_driver_t *this, 
			    XShmSegmentInfo *shminfo, 
			    XImage *myimage) {

  if (this->use_shm) {

    XShmDetach (this->display, shminfo);
    XDestroyImage (myimage);
    shmdt (shminfo->shmaddr);
    if (shminfo->shmid >= 0) {
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
    }

  } else {

    XDestroyImage (myimage);

  }
}


/*
 * and now, the driver functions
 */

static uint32_t xshm_get_capabilities (vo_driver_t *this_gen) {
  return VO_CAP_COPIES_IMAGE | VO_CAP_YV12 | VO_CAP_YUY2;
}

static void xshm_frame_copy (vo_frame_t *vo_img, uint8_t **src) {
  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  xshm_driver_t *this = (xshm_driver_t *) vo_img->instance->driver;

  profiler_start_count (this->prof_yuv2rgb);

  if (frame->format == IMGFMT_YV12) {
    this->yuv2rgb->yuv2rgb_fun (this->yuv2rgb, frame->rgb_dst,
				src[0], src[1], src[2]);
  } else {

    this->yuv2rgb->yuy22rgb_fun (this->yuv2rgb, frame->rgb_dst,
				 src[0]);
				 
  }
  
  profiler_stop_count (this->prof_yuv2rgb);

  frame->rgb_dst += frame->stripe_inc; 
}

static void xshm_frame_field (vo_frame_t *vo_img, int which_field) {

  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  xshm_driver_t *this = (xshm_driver_t *) vo_img->instance->driver;

  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->image->data;
    frame->stripe_inc = 2*this->stripe_height * frame->image->bytes_per_line;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->image->data + frame->image->bytes_per_line ;
    frame->stripe_inc = 2*this->stripe_height * frame->image->bytes_per_line;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = (uint8_t *)frame->image->data;
    break;
  }
}

static void xshm_frame_dispose (vo_frame_t *vo_img) {

  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  xshm_driver_t *this = (xshm_driver_t *) vo_img->instance->driver;

  if (frame->image) {
    XLockDisplay (this->display); 
    dispose_ximage (this, &frame->shminfo, frame->image);
    XUnlockDisplay (this->display); 
  }

  free (frame);
}


static vo_frame_t *xshm_alloc_frame (vo_driver_t *this_gen) {
  xshm_frame_t   *frame ;

  frame = (xshm_frame_t *) malloc (sizeof (xshm_frame_t));
  if (frame==NULL) {
    printf ("xshm_alloc_frame: out of memory\n");
    return NULL;
  }

  memset (frame, 0, sizeof(xshm_frame_t));

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */
  
  frame->vo_frame.copy    = xshm_frame_copy;
  frame->vo_frame.field   = xshm_frame_field; 
  frame->vo_frame.dispose = xshm_frame_dispose;
  
  return (vo_frame_t *) frame;
}

static void xshm_calc_output_size (xshm_driver_t *this) {

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

    this->calc_dest_size (this->output_width, this->output_height,
			  &dest_width, &dest_height);

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
	fprintf (stderr, "invalid ratio, using 4:3\n");
      default:
	xprintf (VIDEO, "unknown aspect ratio (%d) in stream => using 4:3\n",
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
    if ( this->use_shm && this->zoom_mpeg1 && (this->delivered_width<400)) {
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

    this->calc_dest_size (ideal_width, ideal_height,
			  &dest_width, &dest_height);

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

  printf("video_out_xshm: "
	 "frame source %d x %d => screen output %d x %d%s\n",
	 this->delivered_width, this->delivered_height,
	 this->output_width,    this->output_height,
	 ( this->delivered_width != this->output_width
	   || this->delivered_height != this->output_height
	   ? ", software scaling"
	   : "" )
	 );
}

static void xshm_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags) {

  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;
  int setup_yuv = 0;

  flags &= VO_BOTH_FIELDS;

  frame->drawable_ref = this->drawable;

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
    
    xshm_calc_output_size (this);

    setup_yuv = 1;
  }

  if ((frame->rgb_width != this->output_width) 
      || (frame->rgb_height != this->output_height)
      || (frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

    int image_size;

    /*
    printf ("video_out_xshm: updating frame to %d x %d\n",
	    this->output_width,this->output_height);
    */

    XLockDisplay (this->display); 

    /*
     * (re-) allocate XImage
     */

    if (frame->image) {

      dispose_ximage (this, &frame->shminfo, frame->image);

      if (frame->chunk[0]){
	free (frame->chunk[0]);
	frame->chunk[0] = NULL;
      }
      if (frame->chunk[1]) {
	free (frame->chunk[1]);
	frame->chunk[1] = NULL;
      }
      if (frame->chunk[2]) {
	free (frame->chunk[2]);
	frame->chunk[2] = NULL;
      }

      frame->image = NULL;
    }

    frame->image = create_ximage (this, &frame->shminfo,
				  this->output_width, this->output_height);

    XUnlockDisplay (this->display); 

    if (format == IMGFMT_YV12) {
      image_size = width * height;
      frame->vo_frame.base[0] = my_malloc_aligned(16,image_size, &frame->chunk[0]);
      frame->vo_frame.base[1] = my_malloc_aligned(16,image_size/4, &frame->chunk[1]);
      frame->vo_frame.base[2] = my_malloc_aligned(16,image_size/4, &frame->chunk[2]);
    } else {
      image_size = width * height;
      frame->vo_frame.base[0] = my_malloc_aligned(16,image_size*2, &frame->chunk[0]);
    }
    
    frame->format = format;
    frame->width  = width;
    frame->height = height;

    frame->rgb_width  = this->output_width;
    frame->rgb_height = this->output_height;
  }

  if (frame->image) {
    this->stripe_height = 16 * this->output_height / this->delivered_height;

    frame->rgb_dst    = (uint8_t *)frame->image->data;
    switch (flags) {
    case VO_TOP_FIELD:
      frame->rgb_dst    = (uint8_t *)frame->image->data;
      frame->stripe_inc = 2 * this->stripe_height * frame->image->bytes_per_line;
      break;
    case VO_BOTTOM_FIELD:
      frame->rgb_dst    = (uint8_t *)frame->image->data + frame->image->bytes_per_line ;
      frame->stripe_inc = 2 * this->stripe_height * frame->image->bytes_per_line;
      break;
    case VO_BOTH_FIELDS:
      frame->rgb_dst    = (uint8_t *)frame->image->data;
      frame->stripe_inc = this->stripe_height * frame->image->bytes_per_line;
      break;
    }

    if (flags == VO_BOTH_FIELDS) {
      if (this->yuv_stride != frame->image->bytes_per_line)
	setup_yuv = 1;
    } else {	/* VO_TOP_FIELD, VO_BOTTOM_FIELD */
      if (this->yuv_stride != (frame->image->bytes_per_line*2))
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
		       frame->image->bytes_per_line*2);
	this->yuv_stride = frame->image->bytes_per_line*2;
	break;
      case VO_BOTH_FIELDS:
	yuv2rgb_setup (this->yuv2rgb,
		       this->delivered_width,
		       16,
		       this->delivered_width,
		       this->delivered_width/2,
		       this->output_width,
		       this->stripe_height,
		       frame->image->bytes_per_line);
	this->yuv_stride = frame->image->bytes_per_line;
	break;
      }
      this->yuv_height = this->stripe_height;
      this->yuv_width  = this->output_width;
    }
  }
}

static void xshm_overlay_clut_yuv2rgb(xshm_driver_t  *this, vo_overlay_t *overlay)
{
  int i;
  clut_t* clut = (clut_t*) overlay->color;

  for (i = 0; i < 4; i++) {
    *((uint32_t *)&clut[i]) =
                 this->yuv2rgb->yuv2rgb_single_pixel_fun(this->yuv2rgb,
                 clut[i].y, clut[i].cb, clut[i].cr);
  }
  overlay->rgb_clut++;
}

static void xshm_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;

  /* Alpha Blend here */
   if (overlay->rle) {
     if( !overlay->rgb_clut )
       xshm_overlay_clut_yuv2rgb(this,overlay);

     switch(this->bpp) {
       case 16:
        blend_rgb16( (uint8_t *)frame->image->data, overlay,
		     frame->rgb_width, frame->rgb_height,
		     this->delivered_width, this->delivered_height);
        break;
       case 24:
        blend_rgb24( (uint8_t *)frame->image->data, overlay,
		     frame->rgb_width, frame->rgb_height,
		     this->delivered_width, this->delivered_height);
        break;
       case 32:
        blend_rgb32( (uint8_t *)frame->image->data, overlay,
		     frame->rgb_width, frame->rgb_height,
		     this->delivered_width, this->delivered_height);
        break;
       default:
	/* It should never get here */
	break;
     }        
   }
}

static void xshm_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;
  int		  xoffset;
  int		  yoffset;

  if (this->expecting_event) {
    this->expecting_event--;
    frame->vo_frame.displayed (&frame->vo_frame);
  } else {

    if ( (frame->rgb_width != this->last_frame_rgb_width)
	 || (frame->rgb_height != this->last_frame_rgb_height)
	 || (frame->drawable_ref != this->last_frame_drawable_ref) ) {

      xprintf (VIDEO, "video_out_xshm: requesting dest size of %d x %d \n",
	       frame->rgb_width, frame->rgb_height);

      this->request_dest_size (frame->rgb_width, frame->rgb_height, 
			       &this->dest_x, &this->dest_y, 
			       &this->gui_width, &this->gui_height);
      /* for fullscreen modes, clear unused areas of old video area */
      XClearWindow(this->display, this->drawable);

      this->last_frame_rgb_width    = frame->rgb_width;
      this->last_frame_rgb_height   = frame->rgb_height;
      this->last_frame_drawable_ref = frame->drawable_ref;

      printf ("video_out_xshm: gui size %d x %d, frame size %d x %d\n",
	      this->gui_width, this->gui_height,
	      frame->rgb_width, frame->rgb_height);

    }
    
    XLockDisplay (this->display);

    if( this->cur_frame )
      this->cur_frame->vo_frame.displayed (&this->cur_frame->vo_frame);
    this->cur_frame = frame;

    xoffset  = (this->gui_width - frame->rgb_width) / 2;
    yoffset  = (this->gui_height - frame->rgb_height) / 2;

    if (this->use_shm) {

      XShmPutImage(this->display,
		   this->drawable, this->gc, frame->image,
		   0, 0, xoffset, yoffset,
		   frame->rgb_width, frame->rgb_height, True);

      this->expecting_event = 10;

      XFlush(this->display);

    } else {

      XPutImage(this->display,
		this->drawable, this->gc, frame->image,
		0, 0, xoffset, yoffset,
		frame->rgb_width, frame->rgb_height);

      XFlush(this->display);

      frame->vo_frame.displayed (&frame->vo_frame);
      this->cur_frame = NULL;
    }

    XUnlockDisplay (this->display);

  }
}

static int xshm_get_property (vo_driver_t *this_gen, int property) {

  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    return this->user_ratio ;
  } else {
    printf ("video_out_xshm: tried to get unsupported property %d\n", property);
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

static int xshm_set_property (vo_driver_t *this_gen, 
			      int property, int value) {

  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>=NUM_ASPECT_RATIOS)
      value = ASPECT_AUTO;
    this->user_ratio = value;
    this->gui_changed |= GUI_ASPECT_CHANGED;
    printf("video_out_xshm: aspect ratio changed to %s\n",
	   aspect_ratio_name(value));
  } else if ( property == VO_PROP_SOFT_DEINTERLACE) {
    if( value )
      printf("video_out_xshm: software deinterlace not supported.\n");
  } else {
    printf ("video_out_xshm: tried to set unsupported property %d\n", property);
  }

  return value;
}

static void xshm_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

  /* xshm_driver_t *this = (xshm_driver_t *) this_gen;  */

  *min = 0;
  *max = 0;
}


static int is_fullscreen_size (xshm_driver_t *this, int w, int h)
{
  return w == DisplayWidth(this->display, this->screen)
      && h == DisplayHeight(this->display, this->screen);
}

static void xshm_translate_gui2video(xshm_driver_t *this,
				     int x, int y,
				     int *vid_x, int *vid_y)
{
  if (this->output_width > 0 && this->output_height > 0) {
    /*
     * 1.
     * the xshm driver may center a small output area inside a larger
     * gui area.  This is the case in fullscreen mode, where we often
     * have black borders on the top/bottom/left/right side.
     */
    x -= (this->gui_width  - this->output_width)  >> 1;
    y -= (this->gui_height - this->output_height) >> 1;

    /*
     * 2.
     * the xshm driver scales the delivered area into an output area.
     * translate output area coordianates into the delivered area
     * coordiantes.
     */
    x = x * this->delivered_width  / this->output_width;
    y = y * this->delivered_height / this->output_height;
  }

  *vid_x = x;
  *vid_y = y;
}

static int xshm_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data) {

  xshm_driver_t   *this = (xshm_driver_t *) this_gen;
  x11_rectangle_t *area;

  switch (data_type) {
  case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:

    area = (x11_rectangle_t *) data;

    XLockDisplay (this->display);

    if (this->gui_width != area->w || this->gui_height != area->h) {

      printf("video_out_xshm: video window size changed from %d x %d to %d x %d\n",
	     this->gui_width, this->gui_height,
	     area->w, area->h);

      /*
       * if the old or new video window size size is not for the
       * fullscreen frame, update our output_scale_factor.  We
       * preserve the non-fullscreen output_scale_factor to be able to
       * restore the old window size when going back from fullscreen
       * to windowing mode.
       */
      if (this->gui_width > 0 && this->gui_height > 0
	  && !is_fullscreen_size(this, this->gui_width,  this->gui_height)
	  && !is_fullscreen_size(this, area->w, area->h)) {
	double log_scale;
	double int_scale;

	this->output_scale_factor *=
	    sqrt( (double) (area->w * area->h)
		  / (double) (this->gui_width * this->gui_height) );
	
	/*
	 * if we are near an exact power of 1.2, round the output_scale_factor
	 * to the exact value, to increase the chance that we can avoid
	 * the software image scaler.
	 */
	log_scale = log(this->output_scale_factor) / log(1.2);
	int_scale = rint(log_scale);
	if (fabs(log_scale - int_scale) < 0.03)
	  this->output_scale_factor = pow(1.2, int_scale);
      }
      printf("video_out_xshm: output_scale %f\n", this->output_scale_factor);

      /*
       * The GUI_DATA_EX_DEST_POS_SIZE_CHANGED notification might be
       * slow, and we may already have painted frames at the wrong
       * position on the resized window.  Just clear the window.
       */
      XClearWindow(this->display, this->drawable);

      this->gui_width  = area->w;
      this->gui_height = area->h;

      this->gui_changed |= GUI_SIZE_CHANGED;
    }

    XUnlockDisplay (this->display);


    break;
  case GUI_DATA_EX_COMPLETION_EVENT: {

    XShmCompletionEvent *cev = (XShmCompletionEvent *) data;

    if (cev->drawable == this->drawable) {
      this->expecting_event = 0;

      if (this->cur_frame) {
	this->cur_frame->vo_frame.displayed (&this->cur_frame->vo_frame);
	this->cur_frame = NULL;
      }
    }

  }
  break;

  case GUI_DATA_EX_EXPOSE_EVENT:
    
  /* FIXME : take care of completion events */

  if (this->cur_frame) {

    XExposeEvent * xev = (XExposeEvent *) data;
    int		   xoffset;
    int		   yoffset;

    if (xev->count == 0) {

      XLockDisplay (this->display);
      
      xoffset  = (this->gui_width  - this->cur_frame->rgb_width) / 2;
      yoffset  = (this->gui_height - this->cur_frame->rgb_height) / 2;

      if (this->use_shm) {

	XShmPutImage(this->display,
		     this->drawable, this->gc, this->cur_frame->image,
		     0, 0, xoffset, yoffset,
		     this->cur_frame->rgb_width, this->cur_frame->rgb_height,
		     False);
    
      } else {
	
	XPutImage(this->display, 
		  this->drawable, this->gc, this->cur_frame->image,
		  0, 0, xoffset, yoffset,
		  this->cur_frame->rgb_width, this->cur_frame->rgb_height);
      }
      XFlush (this->display);

      XUnlockDisplay (this->display);
    }

  }
  break;

  case GUI_DATA_EX_DRAWABLE_CHANGED:
    this->drawable = (Drawable) data;

    XFreeGC(this->display, this->gc);
    this->gc       = XCreateGC (this->display, this->drawable, 0, NULL);

    break;

  case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO:
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      xshm_translate_gui2video(this, rect->x, rect->y,
			       &x1, &y1);
      xshm_translate_gui2video(this, rect->x + rect->w, rect->y + rect->h,
			       &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
    }
    break;

  default:
    return -1;
  }

  return 0;
}

static void xshm_exit (vo_driver_t *this_gen) {

  /* xshm_driver_t *this = (xshm_driver_t *) this_gen; */

}


static int
ImlibPaletteLUTGet(xshm_driver_t *this)
{
  unsigned char      *retval;
  Atom                type_ret;
  unsigned long       bytes_after, num_ret;
  int                 format_ret;
  long                length;
  Atom                to_get;
  
  retval = NULL;
  length = 0x7fffffff;
  to_get = XInternAtom(this->display, "_IMLIB_COLORMAP", False);
  XGetWindowProperty(this->display, RootWindow(this->display, this->screen),
		     to_get, 0, length, False,
		     XA_CARDINAL, &type_ret, &format_ret, &num_ret,
		     &bytes_after, &retval);
  if (retval != 0 && num_ret > 0 && format_ret > 0) {
    if (format_ret == 8) {
      int j, i, num_colors;
	  
      num_colors = retval[0];
      j = 1 + num_colors*4;
      this->fast_rgb = malloc(sizeof(uint8_t) * 32 * 32 * 32);
      for (i = 0; i < 32 * 32 * 32 && j < num_ret; i++)
	this->fast_rgb[i] = retval[1+4*retval[j++]+3];

      XFree(retval);
      return 1;
    } else
      XFree(retval);
  }
  return 0;
}


static char *visual_class_name(Visual *visual)
{
  switch (visual->class) {
  case StaticGray:
    return "StaticGray";
  case GrayScale:
    return "GrayScale";
  case StaticColor:
    return "StaticColor";
  case PseudoColor:
    return "PseudoColor";
  case TrueColor:
    return "TrueColor";
  case DirectColor:
    return "DirectColor";
  default:
    return "unknown visual class";
  }
}

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  xshm_driver_t        *this;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  Display              *display = NULL;
  XWindowAttributes     attribs;
  XImage               *myimage;
  XShmSegmentInfo       myshminfo;
  int                   mode;
  int			swapped;
  int			cpu_byte_order;

  visual = (x11_visual_t *) visual_gen;
  display = visual->display;
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  /*
   * allocate plugin struct
   */

  this = malloc (sizeof (xshm_driver_t));

  if (!this) {
    printf ("video_out_xshm: malloc failed\n");
    return NULL;
  }

  memset (this, 0, sizeof(xshm_driver_t));

  this->config		    = config;
  this->display		    = visual->display;
  this->screen		    = visual->screen;
  this->display_ratio	    = visual->display_ratio;
  this->request_dest_size   = visual->request_dest_size;
  this->calc_dest_size	    = visual->calc_dest_size;
  this->output_width	    = 0;
  this->output_height	    = 0;
  this->output_scale_factor = 1.0;
  this->gui_width	    = 0;
  this->gui_height	    = 0;
  this->zoom_mpeg1	    = config->lookup_int (config, "zoom_mpeg1", 1);
  /*
   * FIXME: replace getenv() with config->lookup_int, merge with zoom_mpeg1?
   *
   * this->video_scale = config->lookup_int (config, "video_scale", 2);
   *  0: disable all scaling (including aspect ratio switching, ...)
   *  1: enable aspect ratio switch
   *  2: like 1, double the size for small videos
   */
  this->scaling_disabled    = getenv("VIDEO_OUT_NOSCALE") != NULL;
  this->drawable	    = visual->d;
  this->expecting_event	    = 0;
  this->gc		    = XCreateGC (this->display, this->drawable,
					 0, NULL);

  this->prof_yuv2rgb	    = profiler_allocate_slot ("xshm yuv2rgb convert");

  this->vo_driver.get_capabilities     = xshm_get_capabilities;
  this->vo_driver.alloc_frame          = xshm_alloc_frame;
  this->vo_driver.update_frame_format  = xshm_update_frame_format;
  this->vo_driver.overlay_blend        = xshm_overlay_blend;
  this->vo_driver.display_frame        = xshm_display_frame;
  this->vo_driver.get_property         = xshm_get_property;
  this->vo_driver.set_property         = xshm_set_property;
  this->vo_driver.get_property_min_max = xshm_get_property_min_max;
  this->vo_driver.gui_data_exchange    = xshm_gui_data_exchange;
  this->vo_driver.exit                 = xshm_exit;


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

  XGetWindowAttributes(display, this->drawable, &attribs);
  this->visual = attribs.visual;
  this->depth  = attribs.depth;

  if (this->depth>16)
    printf ("\n\n"
	    "WARNING: current display depth is %d. For better performance\n"
	    "a depth of 16 bpp is recommended!\n\n",
	    this->depth);


  /*
   * check for X shared memory support
   */

  if (XShmQueryExtension(display)) {
    this->use_shm = 1;
  } else {
    printf ("video_out_xshm: MIT shared memory extension not present on display.\n");
    this->use_shm = 0;
  }

  /*
   * try to create a shared image
   * to find out if MIT shm really works
   * and what bpp it uses
   */

  myimage = create_ximage (this, &myshminfo, 100, 100);
  dispose_ximage (this, &myshminfo, myimage);


  /*
   * Is the same byte order in use on the X11 client and server?
   */
  cpu_byte_order = htonl(1) == 1 ? MSBFirst : LSBFirst;
  swapped = cpu_byte_order != this->image_byte_order;

  printf ("video_out_xshm: video mode depth is %d (%d bpp), %s, %sswapped,\n"
	  "\tred: %08lx, green: %08lx, blue: %08lx\n",
	  this->depth, this->bpp,
	  visual_class_name(this->visual),
	  swapped ? "" : "not ",
	  this->visual->red_mask, this->visual->green_mask, this->visual->blue_mask);

  mode = 0;

  switch (this->visual->class) {
  case TrueColor:
    switch (this->depth) {
    case 24:
      if (this->bpp == 32) {
	if (this->visual->red_mask == 0x00ff0000)
	  mode = MODE_32_RGB;
	else
	  mode = MODE_32_BGR;
      } else {
	if (this->visual->red_mask == 0x00ff0000)
	  mode = MODE_24_RGB;
	else
	  mode = MODE_24_BGR;
      }
      break;
    case 16:
      if (this->visual->red_mask == 0xf800)
	mode = MODE_16_RGB;
      else
	mode = MODE_16_BGR;
      break;
    case 15:
      if (this->visual->red_mask == 0x7C00)
	mode = MODE_15_RGB;
      else
	mode = MODE_15_BGR;
      break;
    case 8:
      if (this->visual->red_mask == 0xE0)
	mode = MODE_8_RGB; /* Solaris x86: RGB332 */
      else
	mode = MODE_8_BGR; /* XFree86: BGR233 */
      break;
    }
    break;

  case StaticGray:
    if (this->depth == 8)
      mode = MODE_8_GRAY;
    break;

  case PseudoColor:
  case GrayScale:
    if (this->depth <= 8 && ImlibPaletteLUTGet(this))
      mode = MODE_PALETTE;
    break;
  }

  if (!mode) {
    printf ("video_out_xshm: your video mode was not recognized, sorry :-(\n");
    return NULL;
  }

  this->yuv2rgb = yuv2rgb_init (mode, swapped, this->fast_rgb);

  return &this->vo_driver;
}

static vo_info_t vo_info_shm = {
  2,
  "XShm",
  "xine video output plugin using the MIT X shared memory extension",
  VISUAL_TYPE_X11,
  5
};

vo_info_t *get_video_out_plugin_info() {
  return &vo_info_shm;
}

