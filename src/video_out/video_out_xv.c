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
 * $Id: video_out_xv.c,v 1.127 2002/08/15 18:24:14 miguelfreitas Exp $
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(__FreeBSD__)
#include <machine/param.h>
#endif
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include "video_out.h"
#include "video_out_x11.h"
#include "xine_internal.h"
/* #include "overlay.h" */
#include "alphablend.h"
#include "deinterlace.h"
#include "xineutils.h"
#include "vo_scale.h"

/*
#define LOG
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

  /* all scaling information goes here */
  vo_scale_t         sc;
  
  xv_frame_t         deinterlace_frame;
  int                deinterlace_method;
  int                deinterlace_enabled;

  char               scratch[256];

  int                use_colorkey;
  uint32_t           colorkey;

  int (*x11_old_error_handler)  (Display *, XErrorEvent *);
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
  xv_driver_t *this = (xv_driver_t *) vo_img->driver;

  if (frame->image) {
    
    if (this->use_shm) {
      XLockDisplay (this->display);
      XShmDetach (this->display, &frame->shminfo);
      XFree (frame->image);
      XUnlockDisplay (this->display);

      shmdt (frame->shminfo.shmaddr);
      shmctl (frame->shminfo.shmid, IPC_RMID, NULL);
    }
    else {
      XLockDisplay (this->display);
      XFree (frame->image);
      XUnlockDisplay (this->display);
    }
  }

  free (frame);
}

static vo_frame_t *xv_alloc_frame (vo_driver_t *this_gen) {

  xv_frame_t     *frame ;

  frame = (xv_frame_t *) malloc (sizeof (xv_frame_t));
  memset (frame, 0, sizeof(xv_frame_t));

  if (frame == NULL) {
    printf ("xv_alloc_frame: out of memory\n");
  }

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */

  frame->vo_frame.copy    = NULL;
  frame->vo_frame.field   = xv_frame_field;
  frame->vo_frame.dispose = xv_frame_dispose;

  frame->vo_frame.driver  = this_gen;

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
  this->x11_old_error_handler = XSetErrorHandler (HandleXError);
  XFlush (this->display);
}

static void x11_DeInstallXErrorHandler (xv_driver_t *this)
{
  XSetErrorHandler (this->x11_old_error_handler);
  XFlush (this->display);
  this->x11_old_error_handler = NULL;
}

static XvImage *create_ximage (xv_driver_t *this, XShmSegmentInfo *shminfo,
			       int width, int height, int format) {

  unsigned int  xv_format;
  XvImage      *image=NULL;

  switch (format) {
  case IMGFMT_YV12:
    xv_format = this->xv_format_yv12;
    break;
  case IMGFMT_YUY2:
    xv_format = this->xv_format_yuy2;
    break;
  default:
    fprintf (stderr, "create_ximage: unknown format %08x\n",format);
    abort();
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

    switch (format) {
    case IMGFMT_YV12:
      data = malloc (width * height * 3/2);
      break;
    case IMGFMT_YUY2:
      data = malloc (width * height * 2);
      break;
    default:
      fprintf (stderr, "create_ximage: unknown format %08x\n",format);
      abort();
    }

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

    frame->vo_frame.pitches[0] = frame->image->pitches[0];
    frame->vo_frame.pitches[1] = frame->image->pitches[2];
    frame->vo_frame.pitches[2] = frame->image->pitches[1];
    frame->vo_frame.base[0] = frame->image->data + frame->image->offsets[0];
    frame->vo_frame.base[1] = frame->image->data + frame->image->offsets[2];
    frame->vo_frame.base[2] = frame->image->data + frame->image->offsets[1];

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

static void xv_clean_output_area (xv_driver_t *this) {

  XLockDisplay (this->display);

  XSetForeground (this->display, this->gc, this->black.pixel);

  XFillRectangle(this->display, this->drawable, this->gc,
		 this->sc.gui_x, this->sc.gui_y, this->sc.gui_width, this->sc.gui_height);

  if (this->use_colorkey) {
    XSetForeground (this->display, this->gc, this->colorkey);
    XFillRectangle (this->display, this->drawable, this->gc,
		    this->sc.output_xoffset, this->sc.output_yoffset, 
		    this->sc.output_width, this->sc.output_height);
  }
  
  XUnlockDisplay (this->display);
}

/*
 * convert delivered height/width to ideal width/height
 * taking into account aspect ratio and zoom factor
 */

static void xv_compute_ideal_size (xv_driver_t *this) {

  vo_scale_compute_ideal_size( &this->sc );
}


/*
 * make ideal width/height "fit" into the gui
 */

static void xv_compute_output_size (xv_driver_t *this) {
  
  vo_scale_compute_output_size( &this->sc );
  
  /* onefield_xv divide by 2 the number of lines */
  if (this->deinterlace_enabled
       && (this->deinterlace_method == DEINTERLACE_ONEFIELDXV)
       && (this->cur_frame->format == IMGFMT_YV12)) {
    this->sc.displayed_height  = this->sc.displayed_height / 2;
    this->sc.displayed_yoffset = this->sc.displayed_yoffset / 2;
  }
}

static void xv_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {

  xv_frame_t   *frame = (xv_frame_t *) frame_gen;

  /* Alpha Blend here
   * As XV drivers improve to support Hardware overlay, we will change this function.
   */

  if (overlay->rle) {
    if( frame->format == IMGFMT_YV12 )
      blend_yuv( frame->vo_frame.base, overlay, frame->width, frame->height);
    else
      blend_yuy2( frame->vo_frame.base[0], overlay, frame->width, frame->height);
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

static int xv_redraw_needed (vo_driver_t *this_gen) {
  xv_driver_t  *this = (xv_driver_t *) this_gen;
  int ret = 0;
  
  if( vo_scale_redraw_needed( &this->sc ) ) {

    xv_compute_output_size (this);

    xv_clean_output_area (this);
    
    ret = 1;
  }
  
  return ret;
}
  
static void xv_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  xv_driver_t  *this = (xv_driver_t *) this_gen;
  xv_frame_t   *frame = (xv_frame_t *) frame_gen;
  /*
  printf ("video_out_xv: xv_display_frame...\n");
  */
  if (this->expecting_event) {

    frame->vo_frame.displayed (&frame->vo_frame);
    this->expecting_event--;
#ifdef LOG
    printf ("video_out_xv: xv_display_frame... not displayed, waiting for completion event\n");
#endif
  } else {

    /* 
     * queue frames (deinterlacing)
     * free old frames
     */

    xv_add_recent_frame (this, frame); /* deinterlacing */

    this->cur_frame = frame;

    /* 
     * deinterlace frame if necessary
     * (currently only working for YUV images)
     */

    if (this->deinterlace_enabled && this->deinterlace_method 
	&& frame->format == IMGFMT_YV12)
      xv_deinterlace_frame (this);

    /*
     * let's see if this frame is different in size / aspect
     * ratio from the previous one
     */

    if ( (frame->width != this->sc.delivered_width)
	 || (frame->height != this->sc.delivered_height)
	 || (frame->ratio_code != this->sc.delivered_ratio_code) ) {
#ifdef LOG
      printf("video_out_xv: frame format changed\n");
#endif

      this->sc.delivered_width      = frame->width;
      this->sc.delivered_height     = frame->height;
      this->sc.delivered_ratio_code = frame->ratio_code;

      xv_compute_ideal_size (this);
      
      this->sc.force_redraw = 1;    /* trigger re-calc of output size */
    }

    /* 
     * tell gui that we are about to display a frame,
     * ask for offset and output size
     */
    xv_redraw_needed (this_gen);

    XLockDisplay (this->display);

    if (this->use_shm) {
      XvShmPutImage(this->display, this->xv_port,
		    this->drawable, this->gc, this->cur_frame->image,
		    this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		    this->sc.displayed_width, this->sc.displayed_height,
		    this->sc.output_xoffset, this->sc.output_yoffset,
		    this->sc.output_width, this->sc.output_height, True);

      this->expecting_event = 10;
    } else {
      XvPutImage(this->display, this->xv_port,
		    this->drawable, this->gc, this->cur_frame->image,
		    this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		    this->sc.displayed_width, this->sc.displayed_height,
		    this->sc.output_xoffset, this->sc.output_yoffset,
		    this->sc.output_width, this->sc.output_height);
    }

    XFlush(this->display);

    XUnlockDisplay (this->display);
    
  }
  /*
  printf ("video_out_xv: xv_display_frame... done\n");
  */
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
      if (this->deinterlace_method == DEINTERLACE_ONEFIELDXV) {
         xv_compute_ideal_size (this);
         xv_compute_output_size (this);
      }
      break;
    case VO_PROP_ASPECT_RATIO:

      if (value>=NUM_ASPECT_RATIOS)
	value = ASPECT_AUTO;

      this->props[property].value = value;
      printf("video_out_xv: VO_PROP_ASPECT_RATIO(%d)\n",
	     this->props[property].value);
      this->sc.user_ratio = value;
      
      xv_compute_ideal_size (this);
      
      this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      break;
    case VO_PROP_ZOOM_X:

      if ((value >= VO_ZOOM_MIN) && (value <= VO_ZOOM_MAX)) {
        this->props[property].value = value;
        printf ("video_out_xv: VO_PROP_ZOOM_X = %d\n",
		this->props[property].value);
  
	this->sc.zoom_factor_x = (double)value / (double)VO_ZOOM_STEP;
	           
	xv_compute_ideal_size (this);
      
	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    case VO_PROP_ZOOM_Y:

      if ((value >= VO_ZOOM_MIN) && (value <= VO_ZOOM_MAX)) {
        this->props[property].value = value;
        printf ("video_out_xv: VO_PROP_ZOOM_Y = %d\n",
		this->props[property].value);
  
	this->sc.zoom_factor_y = (double)value / (double)VO_ZOOM_STEP;
	           
	xv_compute_ideal_size (this);
      
	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
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

static int xv_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {

  xv_driver_t     *this = (xv_driver_t *) this_gen;
  
  switch (data_type) {
  case GUI_DATA_EX_COMPLETION_EVENT: {
   
    XShmCompletionEvent *cev = (XShmCompletionEvent *) data;

    if (cev->drawable == this->drawable) {
      this->expecting_event = 0;

    }

  }
  break;

  case GUI_DATA_EX_EXPOSE_EVENT: {

    /* XExposeEvent * xev = (XExposeEvent *) data; */
    
    /* FIXME : take care of completion events */
    
    if (this->cur_frame) {
      int i;
      
      XLockDisplay (this->display);
      
      if (this->use_shm) {
	XvShmPutImage(this->display, this->xv_port,
		      this->drawable, this->gc, this->cur_frame->image,
		      this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		      this->sc.displayed_width, this->sc.displayed_height,
		      this->sc.output_xoffset, this->sc.output_yoffset,
		      this->sc.output_width, this->sc.output_height, True);
      } else {
	XvPutImage(this->display, this->xv_port,
		   this->drawable, this->gc, this->cur_frame->image,
		   this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		   this->sc.displayed_width, this->sc.displayed_height,
		   this->sc.output_xoffset, this->sc.output_yoffset,
		   this->sc.output_width, this->sc.output_height);
      }

      XSetForeground (this->display, this->gc, this->black.pixel);

      for( i = 0; i < 4; i++ ) {
        if( this->sc.border[i].w && this->sc.border[i].h )
          XFillRectangle(this->display, this->drawable, this->gc,
                         this->sc.border[i].x, this->sc.border[i].y,
                         this->sc.border[i].w, this->sc.border[i].h);
      }

      XFlush(this->display);
      
      XUnlockDisplay (this->display);
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
    return -1;
  }

  return 0;
}

static void xv_exit (vo_driver_t *this_gen) {

  xv_driver_t *this = (xv_driver_t *) this_gen;
  int i;

  if (this->deinterlace_frame.image) {
    dispose_ximage (this, &this->deinterlace_frame.shminfo,
		    this->deinterlace_frame.image);
    this->deinterlace_frame.image = NULL;
  }

  XLockDisplay (this->display);
  if(XvUngrabPort (this->display, this->xv_port, CurrentTime) != Success) {
    printf ("video_out_xv: xv_exit: XvUngrabPort() failed.\n");
  }
  XUnlockDisplay (this->display);

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ )
  {
    if( this->recent_frames[i] )
      this->recent_frames[i]->vo_frame.dispose
         (&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }

  free (this);
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
   * some Xv drivers (Gatos ATI) report some ~0 as max values, this is confusing.
   */
  if (VO_PROP_COLORKEY && (attr.max_value == ~0))
    attr.max_value = 2147483615;
  
  this->props[property].min  = attr.min_value;
  this->props[property].max  = attr.max_value;
  this->props[property].atom = XInternAtom (this->display, str_prop, False);
  
  XvGetPortAttribute (this->display, this->xv_port,
		      this->props[property].atom, &int_default);

  printf ("video_out_xv: port attribute %s value is %d\n",
	  str_prop, int_default);

  sprintf (this->scratch, "video.%s", str_prop);

  /* is this a boolean property ? */
  if ((attr.min_value == 0) && (attr.max_value == 1)) {
    this->config->register_bool (this->config, this->scratch, int_default,
				 _("Xv property"),
				 NULL, xv_property_callback, &this->props[property]);
  
  } else {
    this->config->register_range (this->config, this->scratch, int_default,
				  this->props[property].min, this->props[property].max,   
				  _("Xv property"), 
				  NULL, xv_property_callback, &this->props[property]);
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
  
  vo_scale_init( &this->sc, visual->display_ratio, 1, 0 );
  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;
  
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
  this->x11_old_error_handler = NULL;

  XAllocNamedColor(this->display,
		   DefaultColormap(this->display, this->screen),
		   "black", &this->black, &dummy);

  this->vo_driver.get_capabilities     = xv_get_capabilities;
  this->vo_driver.alloc_frame          = xv_alloc_frame;
  this->vo_driver.update_frame_format  = xv_update_frame_format;
  this->vo_driver.overlay_begin        = NULL; /* not used */
  this->vo_driver.overlay_blend        = xv_overlay_blend;
  this->vo_driver.overlay_end          = NULL; /* not used */
  this->vo_driver.display_frame        = xv_display_frame;
  this->vo_driver.get_property         = xv_get_property;
  this->vo_driver.set_property         = xv_set_property;
  this->vo_driver.get_property_min_max = xv_get_property_min_max;
  this->vo_driver.gui_data_exchange    = xv_gui_data_exchange;
  this->vo_driver.exit                 = xv_exit;
  this->vo_driver.redraw_needed        = xv_redraw_needed;

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
  this->sc.user_ratio = this->props[VO_PROP_ASPECT_RATIO].value   = ASPECT_AUTO;
  this->props[VO_PROP_ZOOM_X].value    = 100;
  this->props[VO_PROP_ZOOM_Y].value    = 100;

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
					      _("bilinear scaling mode (permedia 2/3)"),
					      NULL, xv_update_XV_FILTER, this);
	  config->update_num(config,"video.XV_FILTER",xv_filter);
	} else if(!strcmp(attr[k].name, "XV_DOUBLE_BUFFER")) {
	  int xv_double_buffer;
	  xv_double_buffer = config->register_bool (config, "video.XV_DOUBLE_BUFFER", 1,
						    _("double buffer to sync video to the retrace"),
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
  
  for(i = 0; i < formats; i++) {
#ifdef LOG
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
    }
  }

  /*
   * try to create a shared image
   * to find out if MIT shm really works, using supported format
   */
  myimage = create_ximage (this, &myshminfo, 100, 100, 
			   (this->xv_format_yv12 != 0) ? IMGFMT_YV12 : IMGFMT_YUY2);
  dispose_ximage (this, &myshminfo, myimage);

  this->deinterlace_method = config->register_enum (config, "video.deinterlace_method", 4,
						    deinterlace_methods, 
						    _("Software deinterlace method (Key I toggles deinterlacer on/off)"),
						    NULL, xv_update_deinterlace, this);
  this->deinterlace_enabled = 0;

  return &this->vo_driver;
}

static vo_info_t vo_info_xv = {
  6,
  "Xv",
  NULL,
  VISUAL_TYPE_X11,
  9
};

vo_info_t *get_video_out_plugin_info() {
  vo_info_xv.description = _("xine video output plugin using the MIT X video extension");
  return &vo_info_xv;
}

#endif
