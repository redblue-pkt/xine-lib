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
 * $Id: video_out_xv.c,v 1.87 2002/01/05 13:49:30 miguelfreitas Exp $
 * 
 * video_out_xv.c, X11 video extension interface for xine
 *
 * based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * Xv image support by Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * xine-specific code by Guenter Bartsch <bartscgr@studbox.uni-stuttgart.de>
 *
 * overlay support by James Courtier-Dutton <James@superbug.demon.co.uk> - July 2001
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_XV

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video_out.h"
#include "video_out_x11.h"
#include "xine_internal.h"
/* #include "overlay.h" */
#include "alphablend.h"
#include "deinterlace.h"
#include "xineutils.h"

/*
#define XV_LOG
*/

typedef struct xv_driver_s xv_driver_t;

typedef struct {
  int                value;
  int                min;
  int                max;
  Atom               atom;

  cfg_entry_t       *entry;

  xv_driver_t       *this;
} xv_property_t;


typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, ratio_code, format;

  XvImage           *image;
  XShmSegmentInfo    shminfo;

} xv_frame_t;


struct xv_driver_s {

  vo_driver_t        vo_driver;

  config_values_t   *config;

  /* X11 / Xv related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;
  unsigned int       xv_format_rgb;
  unsigned int       xv_format_yv12;
  unsigned int       xv_format_yuy2;
  XVisualInfo        vinfo;
  GC                 gc;
  XvPortID           xv_port;
  XColor             black;
  int                expecting_event; /* completion event handling */
  int                use_shm;

  xv_property_t      props[VO_NUM_PROPERTIES];
  uint32_t           capabilities;

  xv_frame_t        *recent_frames[VO_NUM_RECENT_FRAMES];
  xv_frame_t        *cur_frame;
  vo_overlay_t      *overlay;

  /* size / aspect ratio calculations */
  /* delivered images */
  int                delivered_width;      /* everything is set up for
					      these frame dimensions          */
  int                delivered_height;     /* the dimension as they come
					      from the decoder                */
  int                delivered_ratio_code;
  double             ratio_factor;         /* output frame must fullfill:
					      height = width * ratio_factor   */

  /* displayed part of delivered images */
  int                displayed_width;
  int                displayed_height;
  int                displayed_xoffset;
  int                displayed_yoffset;

  /* Window */
  int                window_width;
  int                window_height;
  int                window_xoffset;
  int                window_yoffset;

  /* output screen area */
  int                output_width;         /* frames will appear in this
					      size (pixels) on screen         */
  int                output_height;
  int                output_xoffset;
  int                output_yoffset;

  xv_frame_t         deinterlace_frame;
  int                deinterlace_method;
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

  char               scratch[256];

  int                use_colorkey;
  uint32_t           colorkey;
};

int gX11Fail;

static uint32_t xv_get_capabilities (vo_driver_t *this_gen) {

  xv_driver_t *this = (xv_driver_t *) this_gen;

  return this->capabilities;
}

static void xv_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed for Xv */
}

static void xv_frame_dispose (vo_frame_t *vo_img) {

  xv_frame_t  *frame = (xv_frame_t *) vo_img ;
  xv_driver_t *this = (xv_driver_t *) vo_img->instance->driver;

  if (frame->image) {
    XLockDisplay (this->display);
    XShmDetach (this->display, &frame->shminfo);
    XFree (frame->image);
    XUnlockDisplay (this->display);

    shmdt (frame->shminfo.shmaddr);
    shmctl (frame->shminfo.shmid, IPC_RMID,NULL);
  }

  free (frame);
}

static vo_frame_t *xv_alloc_frame (vo_driver_t *this_gen) {

  xv_frame_t     *frame ;

  frame = (xv_frame_t *) malloc (sizeof (xv_frame_t));
  memset (frame, 0, sizeof(xv_frame_t));

  if (frame==NULL) {
    printf ("xv_alloc_frame: out of memory\n");
  }

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */

  frame->vo_frame.copy    = NULL;
  frame->vo_frame.field   = xv_frame_field;
  frame->vo_frame.dispose = xv_frame_dispose;

  return (vo_frame_t *) frame;
}

int HandleXError (Display *display, XErrorEvent *xevent) {

  char str [1024];

  XGetErrorText (display, xevent->error_code, str, 1024);

  printf ("received X error event: %s\n", str);

  gX11Fail = 1;
  return 0;

}

static void x11_InstallXErrorHandler (xv_driver_t *this)
{
  XSetErrorHandler (HandleXError);
  XFlush (this->display);
}

static void x11_DeInstallXErrorHandler (xv_driver_t *this)
{
  XSetErrorHandler (NULL);
  XFlush (this->display);
}

static XvImage *create_ximage (xv_driver_t *this, XShmSegmentInfo *shminfo,
			      int width, int height, int format) {

  unsigned int  xv_format;
  XvImage      *image=NULL;

  switch (format) {
  case IMGFMT_YV12:
    xv_format = this->xv_format_yv12;
    break;
  case IMGFMT_RGB:
    xv_format = this->xv_format_rgb;
    break;
  case IMGFMT_YUY2:
    xv_format = this->xv_format_yuy2;
    break;
  default:
    fprintf (stderr, "create_ximage: unknown format %08x\n",format);
    exit (1);
  }

  if (this->use_shm) {

    /*
     * try shm
     */

    gX11Fail = 0;
    x11_InstallXErrorHandler (this);

    image = XvShmCreateImage(this->display, this->xv_port, xv_format, 0,
			     width, height, shminfo);

    if (image == NULL )  {
      printf("video_out_xv: XvShmCreateImage failed\n");
      printf("video_out_xv: => not using MIT Shared Memory extension.\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->shmid=shmget(IPC_PRIVATE,
			 image->data_size,
			 IPC_CREAT | 0777);

    if (image->data_size==0) {
      printf("video_out_xv: XvShmCreateImage returned a zero size\n");
      printf("video_out_xv: => not using MIT Shared Memory extension.\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }

    if (shminfo->shmid < 0 ) {
      perror("video_out_xv: shared memory error in shmget: ");
      printf("video_out_xv: => not using MIT Shared Memory extension.\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->shmaddr  = (char *) shmat(shminfo->shmid, 0, 0);

    if (shminfo->shmaddr == NULL) {
      printf("video_out_xv: shared memory error (address error NULL)\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }

    if (shminfo->shmaddr == ((char *) -1)) {
      printf("video_out_xv: shared memory error (address error)\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }

    shminfo->readOnly = False;
    image->data = shminfo->shmaddr;

    XShmAttach(this->display, shminfo);
  
    XSync(this->display, False);
    shmctl(shminfo->shmid, IPC_RMID, 0);

    if (gX11Fail) {
      printf ("video_out_xv: x11 error during shared memory XImage creation\n");
      printf ("video_out_xv: => not using MIT Shared Memory extension.\n");
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
   * fall back to plain Xv if necessary
   */

  if (!this->use_shm) {

    char *data;

    data = malloc (width * height * 3/2);

    image = XvCreateImage (this->display, this->xv_port,
			   xv_format, data, width, height);


  }
  return image;
}

static void dispose_ximage (xv_driver_t *this,
			    XShmSegmentInfo *shminfo,
			    XvImage *myimage) {

  if (this->use_shm) {

    XShmDetach (this->display, shminfo);
    XFree (myimage);
    shmdt (shminfo->shmaddr);
    if (shminfo->shmid >= 0) {
      shmctl (shminfo->shmid, IPC_RMID, 0);
      shminfo->shmid = -1;
    }

  } else {

    XFree (myimage);

  }
}

static void xv_update_frame_format (vo_driver_t *this_gen,
				    vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height,
				    int ratio_code, int format, int flags) {

  xv_driver_t  *this = (xv_driver_t *) this_gen;
  xv_frame_t   *frame = (xv_frame_t *) frame_gen;

  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

    /* printf ("video_out_xv: updating frame to %d x %d (ratio=%d, format=%08x)\n",width,height,ratio_code,format); */

    XLockDisplay (this->display);

    /*
     * (re-) allocate xvimage
     */

    if (frame->image) {
      dispose_ximage (this, &frame->shminfo, frame->image);
      frame->image = NULL;
    }

    frame->image = create_ximage (this, &frame->shminfo, width, height, format);

    frame->vo_frame.base[0] = frame->image->data;
    frame->vo_frame.base[1] = frame->image->data + width * height * 5 / 4;
    frame->vo_frame.base[2] = frame->image->data + width * height;

    frame->width  = width;
    frame->height = height;
    frame->format = format;

    XUnlockDisplay (this->display);
  }

  frame->ratio_code = ratio_code;
}

#define DEINTERLACE_CROMA
static void xv_deinterlace_frame (xv_driver_t *this) {

  uint8_t *recent_bitmaps[VO_NUM_RECENT_FRAMES];
  xv_frame_t *frame = this->recent_frames[0];
  int i;
  int xvscaling;

  xvscaling = (this->deinterlace_method == DEINTERLACE_ONEFIELDXV) ? 2 : 1;
  
  if ( !this->deinterlace_frame.image
      || (frame->width != this->deinterlace_frame.width)
      || (frame->height / xvscaling != this->deinterlace_frame.height )
      || (frame->format != this->deinterlace_frame.format)) {
    XLockDisplay (this->display);

    if( this->deinterlace_frame.image )
      dispose_ximage (this, &this->deinterlace_frame.shminfo,
                      this->deinterlace_frame.image);

    this->deinterlace_frame.image = create_ximage (this, &this->deinterlace_frame.shminfo,
                                             frame->width,frame->height / xvscaling,
                                             frame->format);
    this->deinterlace_frame.width  = frame->width;
    this->deinterlace_frame.height = frame->height / xvscaling;
    this->deinterlace_frame.format = frame->format;

    XUnlockDisplay (this->display);
  }

  
  if ( this->deinterlace_method != DEINTERLACE_ONEFIELDXV ) {
#ifdef DEINTERLACE_CROMA

    /* I don't think this is the right way to do it (deinterlacing croma by croma info).
       DScaler deinterlaces croma together with luma, but it's easier for them because
       they have that components 1:1 at the same table.
    */
    for( i = 0; i < VO_NUM_RECENT_FRAMES; i++ )
      recent_bitmaps[i] = (this->recent_frames[i]) ? this->recent_frames[i]->image->data
      + frame->width*frame->height : NULL;
    deinterlace_yuv( this->deinterlace_frame.image->data+frame->width*frame->height,
      recent_bitmaps, frame->width/2, frame->height/2, this->deinterlace_method );
    for( i = 0; i < VO_NUM_RECENT_FRAMES; i++ )
      recent_bitmaps[i] = (this->recent_frames[i]) ? this->recent_frames[i]->image->data
      + frame->width*frame->height*5/4 : NULL;
    deinterlace_yuv( this->deinterlace_frame.image->data+frame->width*frame->height*5/4,
      recent_bitmaps, frame->width/2, frame->height/2, this->deinterlace_method );

#else

    /* know bug: we are not deinterlacing Cb and Cr */
    xine_fast_memcpy(this->deinterlace_frame.image->data + frame->width*frame->height,
           frame->image->data + frame->width*frame->height,
           frame->width*frame->height*1/2);

#endif

    for( i = 0; i < VO_NUM_RECENT_FRAMES; i++ )
      recent_bitmaps[i] = (this->recent_frames[i]) ? this->recent_frames[i]->image->data :
                          NULL;

    deinterlace_yuv( this->deinterlace_frame.image->data, recent_bitmaps,
                     frame->width, frame->height, this->deinterlace_method );
  }
  else {
    /*
       dirty and cheap deinterlace method: we give half of the lines to xv
                                           driver and let it scale for us.
       note that memcpy's below don't seem to impact much on performance,
       specially when fast memcpys are available.
    */
    uint8_t *dst, *src;
    
    dst = this->deinterlace_frame.image->data;
    src = this->recent_frames[0]->image->data;
    for( i = 0; i < frame->height; i+=2 ) {
      xine_fast_memcpy(dst,src,frame->width);
      dst+=frame->width;
      src+=2*frame->width;
    }
    
    dst = this->deinterlace_frame.image->data + frame->width*frame->height/2;
    src = this->recent_frames[0]->image->data + frame->width*frame->height;
    for( i = 0; i < frame->height; i+=4 ) {
      xine_fast_memcpy(dst,src,frame->width/2);
      dst+=frame->width/2;
      src+=frame->width;
    }
    
    dst = this->deinterlace_frame.image->data + frame->width*frame->height*5/8;
    src = this->recent_frames[0]->image->data + frame->width*frame->height*5/4;
    for( i = 0; i < frame->height; i+=4 ) {
      xine_fast_memcpy(dst,src,frame->width/2);
      dst+=frame->width/2;
      src+=frame->width;
    }
  }
  
  this->cur_frame = &this->deinterlace_frame;
}

static void xv_clear_unused_output_area (xv_driver_t *this,
					 int dest_x, int dest_y,
					 int dest_width, int dest_height) {

  XLockDisplay (this->display);

  if (this->use_colorkey) {
    XSetForeground (this->display, this->gc, this->colorkey);
    XFillRectangle (this->display, this->drawable, this->gc,
		    dest_x, dest_y, dest_width, dest_height);
  }
  
  XSetForeground (this->display, this->gc, this->black.pixel);
  

  /* top black band */
  XFillRectangle(this->display, this->drawable, this->gc,
		 dest_x, dest_y, dest_width, this->output_yoffset - dest_y);

  /* left black band */
  XFillRectangle(this->display, this->drawable, this->gc, 
		 dest_x, dest_y, this->output_xoffset-dest_x, dest_height);

  /* bottom black band */
  XFillRectangle(this->display, this->drawable, this->gc,
		 dest_x, this->output_yoffset+this->output_height,
		 dest_width,
		 dest_height - this->output_yoffset - this->output_height);

  /* right black band */
  XFillRectangle(this->display, this->drawable, this->gc, 
		 this->output_xoffset+this->output_width, dest_y, 
		 dest_width - this->output_xoffset - this->output_width,
		 dest_height);

  

  XUnlockDisplay (this->display);
}

static void xv_adapt_to_zoom_x(xv_driver_t *this, int normal_xoffset, int normal_width)
{
  int ideal_width;
  double zoom_factor_x;

  zoom_factor_x = ((double)this->props[VO_PROP_ZOOM_X].value + (double)VO_ZOOM_STEP) / (double)VO_ZOOM_STEP;
  ideal_width  = (double)normal_width * zoom_factor_x;
  if (ideal_width > this->window_width)
  {
    /* cut left and right borders */
    this->output_width = this->window_width;
    this->output_xoffset = this->window_xoffset;
    this->displayed_width = (double)this->delivered_width * ((double)this->window_width / (double)ideal_width);
    this->displayed_xoffset = (this->delivered_width - this->displayed_width) / 2;

  } else {
    this->output_width = ideal_width;
    this->output_xoffset = this->window_xoffset + (this->window_width - ideal_width) / 2;
    this->displayed_xoffset = 0;
    this->displayed_width = this->delivered_width;
  }    
}

static void xv_adapt_to_zoom_y(xv_driver_t *this, int normal_yoffset, int normal_height)
{
  int ideal_height;
  double zoom_factor_y;
  int xvscaling;

  xvscaling = (this->deinterlace_enabled &&
               this->deinterlace_method == DEINTERLACE_ONEFIELDXV) ? 2 : 1;

  zoom_factor_y = ((double)this->props[VO_PROP_ZOOM_Y].value + (double)VO_ZOOM_STEP) / (double)VO_ZOOM_STEP;
  ideal_height  = (double)normal_height * zoom_factor_y;
  if (ideal_height > this->window_height)
  {
    /* cut */
    this->output_height = this->window_height;
    this->output_yoffset = this->window_yoffset;
    this->displayed_height = (double)(this->delivered_height / xvscaling) *
                             ((double)this->window_height / (double)ideal_height);
    this->displayed_yoffset = ((this->delivered_height / xvscaling) - this->displayed_height) / 2;
  } else {
    this->output_height = ideal_height;
    this->output_yoffset = this->window_yoffset + (this->window_height - ideal_height) / 2;
    this->displayed_yoffset = 0;
    this->displayed_height = this->delivered_height / xvscaling;
  }
}

static void xv_adapt_to_offset (xv_driver_t *this, int xoffset, int yoffset)
{
  int ideal_x, ideal_y;

  /* try move displayed area */
  ideal_x = this->displayed_xoffset + xoffset;
  ideal_y = this->displayed_yoffset + yoffset;
  
  if (ideal_x < 0)
    ideal_x = 0;
  if ((ideal_x + this->displayed_width) > this->delivered_width)
    ideal_x = this->delivered_width - this->displayed_width;
  if (ideal_y < 0)
    ideal_y = 0;
  if ((ideal_y + this->displayed_height) > this->delivered_height)
    ideal_y = this->delivered_height - this->displayed_height;

  this->displayed_xoffset = ideal_x;
  this->displayed_yoffset = ideal_y;
}

static void xv_adapt_to_output_area (xv_driver_t *this,
				     int dest_x, int dest_y,
				     int dest_width, int dest_height) {
  int normal_width, normal_height, normal_xoffset, normal_yoffset;

  this->window_xoffset  = dest_x;
  this->window_yoffset  = dest_y;
  this->window_width    = dest_width;
  this->window_height   = dest_height;

  /*
   * make the frames fit into the given destination area
   */
  if ( ((double) dest_width / this->ratio_factor) < dest_height ) {

    normal_width   = dest_width ;
    normal_height  = (double) dest_width / this->ratio_factor ;
    normal_xoffset = dest_x;
    normal_yoffset = dest_y + (dest_height - this->output_height) / 2;

  } else {

    normal_width    = (double) dest_height * this->ratio_factor ;
    normal_height   = dest_height;
    normal_xoffset  = dest_x + (dest_width - this->output_width) / 2;
    normal_yoffset  = dest_y;
  }

  /* Calc output area size, and displayed area size */
  xv_adapt_to_zoom_x(this, normal_xoffset, normal_width);
  xv_adapt_to_zoom_y(this, normal_yoffset, normal_height);

  xv_adapt_to_offset(this, this->props[VO_PROP_OFFSET_X].value, this->props[VO_PROP_OFFSET_Y].value);
  xv_clear_unused_output_area (this, dest_x, dest_y, dest_width, dest_height);
}

static void xv_calc_format (xv_driver_t *this,
			    int width, int height, int ratio_code) {

  double image_ratio, desired_ratio;
  double corr_factor;
  int ideal_width, ideal_height;
  int dest_x, dest_y, dest_width, dest_height;

  this->delivered_width      = width;
  this->delivered_height     = height;
  this->delivered_ratio_code = ratio_code;

  if ( (!width) || (!height) )
    return;

  /*
   * aspect ratio calculation
   */

  image_ratio =
    (double) this->delivered_width / (double) this->delivered_height;

#ifdef XV_LOG
  printf ("video_out_xv: display_ratio : %f\n", this->display_ratio);
  printf ("video_out_xv: stream aspect ratio : %f , code : %d\n",
	  image_ratio, ratio_code);
#endif

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
      printf ("video_out_xv: unknown aspect ratio (%d) in stream => using 4:3\n",
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

  /* this->ratio_factor = display_ratio * desired_ratio / image_ratio ;  */
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

  /* little hack to zoom mpeg1 / other small streams  by default*/
  if ( this->use_shm && (ideal_width<400)) {
    ideal_width  *=2;
    ideal_height *=2;
  }

  /*
   * ask gui to adapt to this size
   */

  this->request_dest_size (this->user_data,
			   ideal_width, ideal_height,
			   &dest_x, &dest_y, &dest_width, &dest_height);

  /*
   * Reset zoom values to 100%
   */
  this->props[VO_PROP_ZOOM_X].value = 0;
  this->props[VO_PROP_ZOOM_Y].value = 0;

  xv_adapt_to_output_area (this, dest_x, dest_y, dest_width, dest_height);
}

/*
 *
 */
static void xv_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {

  xv_frame_t   *frame = (xv_frame_t *) frame_gen;

  /* Alpha Blend here
   * As XV drivers improve to support Hardware overlay, we will change this function.
   */

  if (overlay->rle) {
    if( frame->format == IMGFMT_YV12 )
      blend_yuv( frame->image->data, overlay, frame->width, frame->height);
    else
      blend_yuy2( frame->image->data, overlay, frame->width, frame->height);
  }
}

static void xv_add_recent_frame (xv_driver_t *this, xv_frame_t *frame) {
  int i;

  i = VO_NUM_RECENT_FRAMES-1;
  if( this->recent_frames[i] )
    this->recent_frames[i]->vo_frame.displayed
       (&this->recent_frames[i]->vo_frame);

  for( ; i ; i-- )
    this->recent_frames[i] = this->recent_frames[i-1];

  this->recent_frames[0] = frame;
}

/* currently not used - we could have a method to call this from video loop */
#if 0
static void xv_flush_recent_frames (xv_driver_t *this) {

  int i;

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ )
  {
    if( this->recent_frames[i] )
      this->recent_frames[i]->vo_frame.displayed
         (&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }
}
#endif


static void xv_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  xv_driver_t  *this = (xv_driver_t *) this_gen;
  xv_frame_t   *frame = (xv_frame_t *) frame_gen;

  if (this->expecting_event) {

    frame->vo_frame.displayed (&frame->vo_frame);
    this->expecting_event--;

  } else {
    xv_add_recent_frame (this, frame);
    this->cur_frame = frame;


    if ( (frame->width != this->delivered_width)
	 || (frame->height != this->delivered_height)
	 || (frame->ratio_code != this->delivered_ratio_code) ) {
	 printf("video_out_xv: change frame format\n");
      xv_calc_format (this, frame->width, frame->height, frame->ratio_code);
    }

    if (this->deinterlace_enabled && this->deinterlace_method)
      xv_deinterlace_frame (this);

    XLockDisplay (this->display);
    
    if (this->use_shm) {
      XvShmPutImage(this->display, this->xv_port,
		    this->drawable, this->gc, this->cur_frame->image,
		    this->displayed_xoffset, this->displayed_yoffset,
		    this->displayed_width, this->displayed_height - 5,
		    this->output_xoffset, this->output_yoffset,
		    this->output_width, this->output_height, True);

      this->expecting_event = 10;
    } else {
      XvPutImage(this->display, this->xv_port,
		    this->drawable, this->gc, this->cur_frame->image,
		    this->displayed_xoffset, this->displayed_yoffset,
		    this->displayed_width, this->displayed_height - 5,
		    this->output_xoffset, this->output_yoffset,
		    this->output_width, this->output_height);
    }

    XFlush(this->display);

    XUnlockDisplay (this->display);
    
  }
}

static int xv_get_property (vo_driver_t *this_gen, int property) {

  xv_driver_t *this = (xv_driver_t *) this_gen;
  
  return this->props[property].value;
}

static void xv_property_callback (void *property_gen, cfg_entry_t *entry) {

  xv_property_t *property = (xv_property_t *) property_gen;
  xv_driver_t   *this = property->this;
  
  XvSetPortAttribute (this->display, this->xv_port,
		      property->atom, entry->num_value);

}

static int xv_set_property (vo_driver_t *this_gen,
			    int property, int value) {

  xv_driver_t *this = (xv_driver_t *) this_gen;
  
  if (this->props[property].atom != None) {
    XvSetPortAttribute (this->display, this->xv_port,
			this->props[property].atom, value);
    XvGetPortAttribute (this->display, this->xv_port,
			this->props[property].atom,
			&this->props[property].value);

    this->props[property].entry->num_value = this->props[property].value;

    return this->props[property].value;
  } else {
    switch (property) {
    case VO_PROP_INTERLACED:
      this->props[property].value = value;
      printf("video_out_xv: VO_PROP_INTERLACED(%d)\n",
	     this->props[property].value);
      this->deinterlace_enabled = value;
      xv_adapt_to_output_area (this, this->window_xoffset, this->window_yoffset,
                               this->window_width, this->window_height);
      break;
    case VO_PROP_ASPECT_RATIO:

      if (value>=NUM_ASPECT_RATIOS)
	value = ASPECT_AUTO;

      this->props[property].value = value;
      printf("video_out_xv: VO_PROP_ASPECT_RATIO(%d)\n",
	     this->props[property].value);

      xv_calc_format (this, this->delivered_width, this->delivered_height,
		      this->delivered_ratio_code) ;
      break;
    case VO_PROP_ZOOM_X:
      if ((value >= VO_ZOOM_MIN) && (value <= VO_ZOOM_MAX))
      {
        this->props[property].value = value;
        printf("video_out_xv: VO_PROP_ZOOM_X(%d) \n",
	           this->props[property].value);
	           
        xv_adapt_to_output_area (this, this->window_xoffset, this->window_yoffset,
				 this->window_width, this->window_height);
      }
      break;
    case VO_PROP_ZOOM_Y:
      if ((value >= VO_ZOOM_MIN) && (value <= VO_ZOOM_MAX))
      {
        this->props[property].value = value;
        printf("video_out_xv: VO_PROP_ZOOM_Y(%d) \n",
	           this->props[property].value);
	           
        xv_adapt_to_output_area (this, this->window_xoffset, this->window_yoffset,
				 this->window_width, this->window_height);
      }
      break;
    case VO_PROP_OFFSET_X:
      this->props[property].value = value;
      printf("video_out_xv: VO_PROP_OFFSET_X(%d) \n",
          this->props[property].value);
	           
      xv_adapt_to_output_area (this, this->window_xoffset, this->window_yoffset,
			       this->window_width, this->window_height);
      break;
    case VO_PROP_OFFSET_Y:
      this->props[property].value = value;
      printf("video_out_xv: VO_PROP_OFFSET_Y(%d) \n",
	     this->props[property].value);
	           
      xv_adapt_to_output_area (this, this->window_xoffset, this->window_yoffset,
			       this->window_width, this->window_height);
      break;
    }
  }

  return value;
}

static void xv_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

  xv_driver_t *this = (xv_driver_t *) this_gen;

  *min = this->props[property].min;
  *max = this->props[property].max;
}

static void xv_translate_gui2video(xv_driver_t *this,
				   int x, int y,
				   int *vid_x, int *vid_y)
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

static int xv_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {

  xv_driver_t     *this = (xv_driver_t *) this_gen;
  x11_rectangle_t *area;
  
  switch (data_type) {
  case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:

    area = (x11_rectangle_t *) data;
    xv_adapt_to_output_area (this, area->x, area->y, area->w, area->h);
    break;

  case GUI_DATA_EX_LOGO_VISIBILITY:

    if (!data)
      xv_clear_unused_output_area (this,
				   this->window_xoffset,
				   this->window_yoffset,
				   this->window_width,
				   this->window_height);
    break;


  case GUI_DATA_EX_COMPLETION_EVENT: {
   
    XShmCompletionEvent *cev = (XShmCompletionEvent *) data;

    if (cev->drawable == this->drawable) {
      this->expecting_event = 0;

    }

  }
    break;

  case GUI_DATA_EX_EXPOSE_EVENT: {

    XExposeEvent * xev = (XExposeEvent *) data;
    
    /* FIXME : take care of completion events */

    if (xev->count == 0) {
      if (this->cur_frame) {
	XLockDisplay (this->display);

	if (this->use_shm) {
	  XvShmPutImage(this->display, this->xv_port,
			this->drawable, this->gc, this->cur_frame->image,
			this->displayed_xoffset, this->displayed_yoffset,
			this->displayed_width, this->displayed_height - 5,
			this->output_xoffset, this->output_yoffset,
			this->output_width, this->output_height, True);
	} else {
	  XvPutImage(this->display, this->xv_port,
		     this->drawable, this->gc, this->cur_frame->image,
		     this->displayed_xoffset, this->displayed_yoffset,
		     this->displayed_width, this->displayed_height - 5,
		     this->output_xoffset, this->output_yoffset,
		     this->output_width, this->output_height);
	}
	XFlush(this->display);

	XUnlockDisplay (this->display);
      }
    }
  }
    break;

  case GUI_DATA_EX_DRAWABLE_CHANGED:
    this->drawable = (Drawable) data;
    this->gc       = XCreateGC (this->display, this->drawable, 0, NULL);
    break;

  case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO:
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      xv_translate_gui2video(this, rect->x, rect->y,
			     &x1, &y1);
      xv_translate_gui2video(this, rect->x + rect->w, rect->y + rect->h,
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

static void xv_exit (vo_driver_t *this_gen) {

  xv_driver_t *this = (xv_driver_t *) this_gen;

  if( this->deinterlace_frame.image )
  {
     dispose_ximage (this, &this->deinterlace_frame.shminfo,
                      this->deinterlace_frame.image);
     this->deinterlace_frame.image = NULL;
  }

  XLockDisplay (this->display);
  if(XvUngrabPort (this->display, this->xv_port, CurrentTime) != Success) {
    fprintf(stderr, "xv_exit: XvUngrabPort() failed.\n");
  }
  XUnlockDisplay (this->display);
}

static int xv_check_yv12 (Display *display, XvPortID port) {
  XvImageFormatValues * formatValues;
  int formats;
  int i;

  formatValues = XvListImageFormats (display, port, &formats);
  for (i = 0; i < formats; i++)
    if ((formatValues[i].id == IMGFMT_YV12) &&
	(! (strcmp (formatValues[i].guid, "YV12")))) {
      XFree (formatValues);
      return 0;
    }
  XFree (formatValues);
  return 1;
}

static void xv_check_capability (xv_driver_t *this,
				 uint32_t capability,
				 int property, XvAttribute attr,
				 int base_id, char *str_prop) {

  int          int_default;
  cfg_entry_t *entry;

  this->capabilities |= capability;

  /* 
   * Some Xv drivers (Gatos ATI) report some ~0 as max values, this is confusing.
   */
  if(VO_PROP_COLORKEY && (attr.max_value == ~0))
    attr.max_value = 2147483615;
  
  this->props[property].min  = attr.min_value;
  this->props[property].max  = attr.max_value;
  this->props[property].atom = XInternAtom (this->display, str_prop, False);
  
  XvGetPortAttribute (this->display, this->xv_port,
		      this->props[property].atom, &int_default);

  printf ("video_out_xv: port attribute %s value is %d\n",
	  str_prop, int_default);

  sprintf (this->scratch, "video.%s", str_prop);

  /* This is a boolean property */
  if((attr.min_value == 0) && (attr.max_value == 1)) {
    this->config->register_bool (this->config, this->scratch, int_default,
				 "Xv property", NULL, xv_property_callback, &this->props[property]);
  
  }
  else {
    this->config->register_range (this->config, this->scratch, int_default,
				  this->props[property].min, this->props[property].max,   
				  "Xv property", NULL, xv_property_callback, &this->props[property]);
  }
  
  entry = this->config->lookup_entry (this->config, this->scratch);
  
  this->props[property].entry = entry;
  
  xv_set_property (&this->vo_driver, property, entry->num_value);

  if (capability == VO_CAP_COLORKEY) {
    this->use_colorkey = 1;
    this->colorkey = entry->num_value;
  }
}

static void xv_update_deinterlace(void *this_gen, cfg_entry_t *entry)
{
  xv_driver_t *this = (xv_driver_t *) this_gen;
  
  this->deinterlace_method = entry->num_value;
}

static void xv_update_XV_FILTER(void *this_gen, cfg_entry_t *entry)
{
  xv_driver_t *this = (xv_driver_t *) this_gen;
  Atom atom;
  int xv_filter;
  
  xv_filter = entry->num_value;
  
  atom = XInternAtom (this->display, "XV_FILTER", False);

  XvSetPortAttribute (this->display, this->xv_port, atom, xv_filter);
  printf("video_out_xv: bilinear scaling mode (XV_FILTER) = %d\n",xv_filter);
}

static void xv_update_XV_DOUBLE_BUFFER(void *this_gen, cfg_entry_t *entry)
{
  xv_driver_t *this = (xv_driver_t *) this_gen;
  Atom atom;
  int xv_double_buffer;
  
  xv_double_buffer = entry->num_value;
  
  atom = XInternAtom (this->display, "XV_DOUBLE_BUFFER", False);

  XvSetPortAttribute (this->display, this->xv_port, atom, xv_double_buffer);
  printf("video_out_xv: double buffering mode = %d\n",xv_double_buffer);
}


vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  xv_driver_t          *this;
  Display              *display = NULL;
  unsigned int          adaptor_num, adaptors, i, j, formats;
  unsigned int          ver,rel,req,ev,err;
  XvPortID              xv_port;
  XvAttribute          *attr;
  XvAdaptorInfo        *adaptor_info;
  XvImageFormatValues  *fo;
  int                   nattr;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XColor                dummy;
  XvImage              *myimage;
  XShmSegmentInfo       myshminfo;
  static char          *deinterlace_methods[] = {"none", "bob", "weave", "greedy", "onefield",
						 "onefield_xv", NULL};

  display = visual->display;

  /*
   * check for Xvideo support
   */

  if (Success != XvQueryExtension(display,&ver,&rel,&req,&ev,&err)) {
    printf ("video_out_xv: Xv extension not present.\n");
    return NULL;
  }

  /*
   * check adaptors, search for one that supports (at least) yuv12
   */

  if (Success != XvQueryAdaptors(display,DefaultRootWindow(display),
				 &adaptors,&adaptor_info))  {
    printf("video_out_xv: XvQueryAdaptors failed.\n");
    return NULL;
  }

  xv_port = 0;

  for ( adaptor_num = 0; (adaptor_num < adaptors) && !xv_port; adaptor_num++ ) {

    if (adaptor_info[adaptor_num].type & XvImageMask) {

      for (j = 0; j < adaptor_info[adaptor_num].num_ports && !xv_port; j++)
        if (( !(xv_check_yv12 (display,
                   adaptor_info[adaptor_num].base_id + j)))
            && (XvGrabPort (display,
                adaptor_info[adaptor_num].base_id + j,
                0) == Success)) {
          xv_port = adaptor_info[adaptor_num].base_id + j;
        }

      if( xv_port )
        break;
    }
  }

  if (!xv_port) {
    printf ("video_out_xv: Xv extension is present but "
	    "I couldn't find a usable yuv12 port.\n");
    printf ("              Looks like your graphics hardware "
	    "driver doesn't support Xv?!\n");
    /* XvFreeAdaptorInfo (adaptor_info); this crashed on me (gb)*/
    return NULL;
  } else
    printf ("video_out_xv: using Xv port %ld from adaptor %s for hardware "
            "colorspace conversion and scaling.\n", xv_port,
            adaptor_info[adaptor_num].name);

  /*
   * from this point on, nothing should go wrong anymore; so let's start initializing this driver
   */

  this = malloc (sizeof (xv_driver_t));

  if (!this) {
    printf ("video_out_xv: malloc failed\n");
    return NULL;
  }

  memset (this, 0, sizeof(xv_driver_t));

  this->config            = config;
  this->display           = visual->display;
  this->overlay           = NULL;
  this->screen            = visual->screen;
  this->display_ratio     = visual->display_ratio;
  this->request_dest_size = visual->request_dest_size;
  this->user_data         = visual->user_data;
  this->output_xoffset    = 0;
  this->output_yoffset    = 0;
  this->output_width      = 0;
  this->output_height     = 0;
  this->displayed_xoffset = 0;
  this->displayed_yoffset = 0;
  this->displayed_width   = 0;
  this->displayed_height  = 0;
  this->window_xoffset    = 0;
  this->window_yoffset    = 0;
  this->window_width      = 0;
  this->window_height     = 0;
  this->drawable          = visual->d;
  this->gc                = XCreateGC (this->display, this->drawable, 0, NULL);
  this->xv_port           = xv_port;
  this->capabilities      = 0;
  this->expecting_event   = 0;
  this->use_shm           = 1;
  this->deinterlace_method = 0;
  this->deinterlace_frame.image = NULL;
  this->use_colorkey      = 0;
  this->colorkey          = 0;

  XAllocNamedColor(this->display,
		   DefaultColormap(this->display, this->screen),
		   "black", &this->black, &dummy);

  this->vo_driver.get_capabilities     = xv_get_capabilities;
  this->vo_driver.alloc_frame          = xv_alloc_frame;
  this->vo_driver.update_frame_format  = xv_update_frame_format;
  this->vo_driver.overlay_blend        = xv_overlay_blend;
  this->vo_driver.display_frame        = xv_display_frame;
  this->vo_driver.get_property         = xv_get_property;
  this->vo_driver.set_property         = xv_set_property;
  this->vo_driver.get_property_min_max = xv_get_property_min_max;
  this->vo_driver.gui_data_exchange    = xv_gui_data_exchange;
  this->vo_driver.exit                 = xv_exit;

  /*
   * init properties
   */

  for (i=0; i<VO_NUM_PROPERTIES; i++) {
    this->props[i].value = 0;
    this->props[i].min   = 0;
    this->props[i].max   = 0;
    this->props[i].atom  = None;
    this->props[i].entry = NULL;
    this->props[i].this  = this;
  }

  this->props[VO_PROP_INTERLACED].value     = 0;
  this->props[VO_PROP_ASPECT_RATIO].value   = ASPECT_AUTO;

  /*
   * check this adaptor's capabilities
   */

  attr = XvQueryPortAttributes(display, xv_port, &nattr);
  if(attr && nattr) {
    int k;

    for(k = 0; k < nattr; k++) {
      if((attr[k].flags & XvSettable) && (attr[k].flags & XvGettable)) {
	if(!strcmp(attr[k].name, "XV_HUE")) {
	  xv_check_capability (this, VO_CAP_HUE,
			       VO_PROP_HUE, attr[k],
			       adaptor_info[adaptor_num].base_id, "XV_HUE");

	} else if(!strcmp(attr[k].name, "XV_SATURATION")) {
	  xv_check_capability (this, VO_CAP_SATURATION,
			       VO_PROP_SATURATION, attr[k],
			       adaptor_info[adaptor_num].base_id, "XV_SATURATION");

	} else if(!strcmp(attr[k].name, "XV_BRIGHTNESS")) {
	  xv_check_capability (this, VO_CAP_BRIGHTNESS,
			       VO_PROP_BRIGHTNESS, attr[k],
			       adaptor_info[adaptor_num].base_id, "XV_BRIGHTNESS");

	} else if(!strcmp(attr[k].name, "XV_CONTRAST")) {
	  xv_check_capability (this, VO_CAP_CONTRAST,
			       VO_PROP_CONTRAST, attr[k],
			       adaptor_info[adaptor_num].base_id, "XV_CONTRAST");

	} else if(!strcmp(attr[k].name, "XV_COLORKEY")) {
	  xv_check_capability (this, VO_CAP_COLORKEY,
			       VO_PROP_COLORKEY, attr[k],
			       adaptor_info[adaptor_num].base_id, "XV_COLORKEY");
	  
	} else if(!strcmp(attr[k].name, "XV_AUTOPAINT_COLORKEY")) {
	  xv_check_capability (this, VO_CAP_AUTOPAINT_COLORKEY,
			       VO_PROP_AUTOPAINT_COLORKEY, attr[k],
			       adaptor_info[adaptor_num].base_id, "XV_AUTOPAINT_COLORKEY");

	} else if(!strcmp(attr[k].name, "XV_FILTER")) {
	  int xv_filter;
	  /* This setting is specific to Permedia 2/3 cards. */
	  xv_filter = config->register_range (config, "video.XV_FILTER", 0,
					    attr[k].min_value, attr[k].max_value,
					    "bilinear scaling mode (permedia 2/3)",
					    NULL, xv_update_XV_FILTER, this);
	  config->update_num(config,"video.XV_FILTER",xv_filter);
	} else if(!strcmp(attr[k].name, "XV_DOUBLE_BUFFER")) {
	  int xv_double_buffer;
	  xv_double_buffer = config->register_bool (config, "video.XV_DOUBLE_BUFFER", 1,
					    "double buffer to sync video to the retrace",
					    NULL, xv_update_XV_DOUBLE_BUFFER, this);
	  config->update_num(config,"video.XV_DOUBLE_BUFFER",xv_double_buffer);
	}
      }
    }
    XFree(attr);
  } else {
    printf("video_out_xv: no port attributes defined.\n");
  }

  XvFreeAdaptorInfo (adaptor_info);

  /*
   * check supported image formats
   */

  fo = XvListImageFormats(display, this->xv_port, (int*)&formats);

  this->xv_format_yv12 = 0;
  this->xv_format_yuy2 = 0;
  this->xv_format_rgb  = 0;
  
  for(i = 0; i < formats; i++) {
#ifdef XV_LOG
    printf ("video_out_xv: Xv image format: 0x%x (%4.4s) %s\n",
	    fo[i].id, (char*)&fo[i].id,
	    (fo[i].format == XvPacked) ? "packed" : "planar");
#endif
    if (fo[i].id == IMGFMT_YV12)  {
      this->xv_format_yv12 = fo[i].id;
      this->capabilities |= VO_CAP_YV12;
      printf("video_out_xv: this adaptor supports the yv12 format.\n");
    } else if (fo[i].id == IMGFMT_YUY2) {
      this->xv_format_yuy2 = fo[i].id;
      this->capabilities |= VO_CAP_YUY2;
      printf("video_out_xv: this adaptor supports the yuy2 format.\n");
    } else if (fo[i].id == IMGFMT_RGB) {
      this->xv_format_rgb = fo[i].id;
      this->capabilities |= VO_CAP_RGB;
      printf("video_out_xv: this adaptor supports the rgb format.\n");
    }
  }

  /*
   * try to create a shared image
   * to find out if MIT shm really works
   */

  myimage = create_ximage (this, &myshminfo, 100, 100, IMGFMT_YV12);
  dispose_ximage (this, &myshminfo, myimage);

  this->deinterlace_method = config->register_enum (config, "video.deinterlace_method", 4,
						    deinterlace_methods, 
						    "Software deinterlace method (Key I toggles deinterlacer on/off)",
						    NULL, xv_update_deinterlace, this);
  this->deinterlace_enabled = 0;

  return &this->vo_driver;
}

static vo_info_t vo_info_xv = {
  2,
  "Xv",
  "xine video output plugin using the MIT X video extension",
  VISUAL_TYPE_X11,
  10
};

vo_info_t *get_video_out_plugin_info() {
  return &vo_info_xv;
}

#endif

