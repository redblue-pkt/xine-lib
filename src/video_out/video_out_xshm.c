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
 * $Id: video_out_xshm.c,v 1.102 2003/02/02 11:21:34 esnel Exp $
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

#include "xine.h"
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
#include "alphablend.h"
#include "yuv2rgb.h"
#include "xineutils.h"
#include "vo_scale.h"

/*
#define LOG
*/

extern int XShmGetEventBase(Display *);

typedef struct xshm_frame_s {
  vo_frame_t         vo_frame;

  /* frame properties as delivered by the decoder: */
  /* obs: for width/height use vo_scale_t struct */
  int                format;
  int                flags;

  vo_scale_t         sc;

  XImage            *image;
  XShmSegmentInfo    shminfo;

  uint8_t           *chunk[3]; /* mem alloc by xmalloc_aligned           */

  yuv2rgb_t         *yuv2rgb; /* yuv2rgb converter set up for this frame */
  uint8_t           *rgb_dst;
  int                yuv_stride;

} xshm_frame_t;

typedef struct xshm_driver_s {

  vo_driver_t        vo_driver;

  /* X11 / XShm related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;
  Visual	    *visual;
  GC                 gc;
  int                depth, bpp, bytes_per_pixel, image_byte_order;
  int                use_shm;
  XColor             black;
  
  int                yuv2rgb_mode;
  int                yuv2rgb_swap;
  int                yuv2rgb_gamma;
  uint8_t           *yuv2rgb_cmap;
  yuv2rgb_factory_t *yuv2rgb_factory;

  vo_scale_t         sc;
  
  int                expecting_event; /* completion event */

  xshm_frame_t      *cur_frame; /* for completion event handling */
  vo_overlay_t      *overlay;

  int (*x11_old_error_handler)  (Display *, XErrorEvent *);

} xshm_driver_t;

typedef struct {
  video_driver_class_t driver_class;

  config_values_t     *config;
} xshm_class_t;


int gX11Fail;

/*
 * first, some utility functions
 */
static int HandleXError (Display *display, XErrorEvent *xevent) {
  
  char str [1024];

  XGetErrorText (display, xevent->error_code, str, 1024);

  printf ("received X error event: %s\n", str);

  gX11Fail = 1;
  return 0;

}

static void x11_InstallXErrorHandler (xshm_driver_t *this)
{
  this->x11_old_error_handler = XSetErrorHandler (HandleXError);
  XFlush (this->display);
}

static void x11_DeInstallXErrorHandler (xshm_driver_t *this)
{
  XSetErrorHandler (this->x11_old_error_handler);
  XFlush (this->display);
  this->x11_old_error_handler = NULL;
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

    myimage->data = xine_xmalloc (width * this->bytes_per_pixel * height);
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
  return VO_CAP_COPIES_IMAGE | VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_BRIGHTNESS;
}

static void xshm_frame_copy (vo_frame_t *vo_img, uint8_t **src) {
  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  /*xshm_driver_t *this = (xshm_driver_t *) vo_img->driver; */

  vo_img->copy_called = 1;
  
#ifdef LOG
  printf ("video_out_xshm: copy... (format %d)\n", frame->format);
#endif

  if (frame->format == XINE_IMGFMT_YV12) {
    frame->yuv2rgb->yuv2rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				 src[0], src[1], src[2]);
  } else {

    frame->yuv2rgb->yuy22rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				  src[0]);
				 
  }
  
#ifdef LOG
  printf ("video_out_xshm: copy...done\n");
#endif
}

static void xshm_frame_field (vo_frame_t *vo_img, int which_field) {

  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  /* xshm_driver_t *this = (xshm_driver_t *) vo_img->driver; */

  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->image->data;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->image->data + frame->image->bytes_per_line ;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = (uint8_t *)frame->image->data;
    break;
  }

  frame->yuv2rgb->next_slice (frame->yuv2rgb, NULL);
}

static void xshm_frame_dispose (vo_frame_t *vo_img) {

  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  xshm_driver_t *this = (xshm_driver_t *) vo_img->driver;

  if (frame->image) {
    XLockDisplay (this->display); 
    dispose_ximage (this, &frame->shminfo, frame->image);
    XUnlockDisplay (this->display); 
  }

  frame->yuv2rgb->dispose (frame->yuv2rgb);

  free (frame->chunk[0]);
  free (frame->chunk[1]);
  free (frame->chunk[2]);
  free (frame);
}


static vo_frame_t *xshm_alloc_frame (vo_driver_t *this_gen) {

  xshm_frame_t  *frame ;
  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  frame = (xshm_frame_t *) malloc (sizeof (xshm_frame_t));
  if (frame==NULL) {
    printf ("xshm_alloc_frame: out of memory\n");
    return NULL;
  }

  memset (frame, 0, sizeof(xshm_frame_t));
  memcpy (&frame->sc, &this->sc, sizeof(vo_scale_t));

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions/fields
   */
  
  frame->vo_frame.copy    = xshm_frame_copy;
  frame->vo_frame.field   = xshm_frame_field; 
  frame->vo_frame.dispose = xshm_frame_dispose;
  frame->vo_frame.driver  = this_gen;

  /*
   * colorspace converter for this frame
   */

  frame->yuv2rgb = this->yuv2rgb_factory->create_converter (this->yuv2rgb_factory);


  return (vo_frame_t *) frame;
}

static void xshm_compute_ideal_size (xshm_driver_t *this, xshm_frame_t *frame) {

  vo_scale_compute_ideal_size( &frame->sc );
}

static void xshm_compute_rgb_size (xshm_driver_t *this, xshm_frame_t *frame) {

  vo_scale_compute_output_size( &frame->sc );

  /* avoid problems in yuv2rgb */
  if (frame->sc.output_height < 1)
    frame->sc.output_height = 1;
  if (frame->sc.output_width < 8)
    frame->sc.output_width = 8;
  if (frame->sc.output_width & 1) /* yuv2rgb_mlib needs an even YUV2 width */
    frame->sc.output_width++;


#ifdef LOG
  printf("video_out_xshm: frame source %d x %d => screen output %d x %d%s\n",
	 frame->sc.delivered_width, frame->sc.delivered_height,
	 frame->sc.output_width, frame->sc.output_height,
	 ( frame->sc.delivered_width != frame->sc.output_width
	   || frame->sc.delivered_height != frame->sc.output_height
	   ? ", software scaling"
	   : "" )
	 );
#endif
}

static void xshm_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags) {

  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;
  int             do_adapt;
  int             gui_width, gui_height;
  double          gui_pixel_aspect;

  flags &= VO_BOTH_FIELDS;

  /* find out if we need to adapt this frame */

  do_adapt = 0;

  if ((width != frame->sc.delivered_width)
      || (height != frame->sc.delivered_height)
      || (ratio_code != frame->sc.delivered_ratio_code)
      || (flags != frame->flags)
      || (format != frame->format)
      || (this->sc.user_ratio != frame->sc.user_ratio)
      || (this->sc.gui_pixel_aspect != frame->sc.gui_pixel_aspect)) {

    do_adapt = 1;

#ifdef LOG
    printf ("video_out_xshm: frame format (from decoder) has changed => adapt\n");
#endif

    frame->sc.delivered_width      = width;
    frame->sc.delivered_height     = height;
    frame->sc.delivered_ratio_code = ratio_code;
    frame->flags                   = flags;
    frame->format                  = format;
    frame->sc.user_ratio           = this->sc.user_ratio;
    frame->sc.gui_pixel_aspect     = this->sc.gui_pixel_aspect;

    xshm_compute_ideal_size (this, frame);
  }

  /* ask gui what output size we'll have for this frame*/

  this->sc.dest_size_cb (this->sc.user_data, frame->sc.delivered_width, frame->sc.delivered_height,
			 frame->sc.video_pixel_aspect, &gui_width, &gui_height, &gui_pixel_aspect);

  if ((frame->sc.gui_width != gui_width) || (frame->sc.gui_height != gui_height) 
      || (frame->sc.gui_pixel_aspect != gui_pixel_aspect)
      || do_adapt) {

    do_adapt = 1;
    frame->sc.gui_width  = gui_width;
    frame->sc.gui_height = gui_height;
    frame->sc.gui_pixel_aspect = gui_pixel_aspect;

    xshm_compute_rgb_size (this, frame);
 
#ifdef LOG
    printf ("video_out_xshm: gui_size has changed => adapt\n");
#endif
 }
    

  /* ok, now do what we have to do */

  if (do_adapt) {

#ifdef LOG
    printf ("video_out_xshm: updating frame to %d x %d\n",
	    frame->sc.output_width, frame->sc.output_height);
#endif

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
				  frame->sc.output_width, frame->sc.output_height);

    XUnlockDisplay (this->display); 

    if (format == XINE_IMGFMT_YV12) {
      frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
      frame->vo_frame.base[0] = xine_xmalloc_aligned (16, frame->vo_frame.pitches[0] * height,  (void **) &frame->chunk[0]);
      frame->vo_frame.base[1] = xine_xmalloc_aligned (16, frame->vo_frame.pitches[1] * ((height+1)/2), (void **) &frame->chunk[1]);
      frame->vo_frame.base[2] = xine_xmalloc_aligned (16, frame->vo_frame.pitches[2] * ((height+1)/2), (void **) &frame->chunk[2]);
    } else {
      frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
      frame->vo_frame.base[0] = xine_xmalloc_aligned (16, frame->vo_frame.pitches[0] * height, (void **) &frame->chunk[0]);
      frame->chunk[1] = NULL;
      frame->chunk[2] = NULL;
    }

#ifdef LOG
    printf ("video_out_xshm: stripe out_ht=%i, deliv_ht=%i\n",
	    frame->sc.output_height, frame->sc.delivered_height);
#endif

    /* 
     * set up colorspace converter
     */

    switch (flags) {
    case VO_TOP_FIELD:
    case VO_BOTTOM_FIELD:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 frame->sc.delivered_width,
				 frame->sc.delivered_height,
				 2*frame->vo_frame.pitches[0],
				 2*frame->vo_frame.pitches[1],
				 frame->sc.output_width,
				 frame->sc.output_height,
				 frame->image->bytes_per_line*2);
      frame->yuv_stride = frame->image->bytes_per_line*2;
      break;
    case VO_BOTH_FIELDS:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 frame->sc.delivered_width,
				 frame->sc.delivered_height,
				 frame->vo_frame.pitches[0],
				 frame->vo_frame.pitches[1],
				 frame->sc.output_width,
				 frame->sc.output_height,
				 frame->image->bytes_per_line);
      frame->yuv_stride = frame->image->bytes_per_line;
      break;
    }
  }

  xshm_frame_field ((vo_frame_t *)frame, flags);
}

static void xshm_overlay_clut_yuv2rgb(xshm_driver_t  *this, vo_overlay_t *overlay,
				      xshm_frame_t *frame) {

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

static void xshm_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;

  /* Alpha Blend here */
   if (overlay->rle) {
     if (!overlay->rgb_clut || !overlay->clip_rgb_clut)
       xshm_overlay_clut_yuv2rgb (this, overlay, frame);

     switch (this->bpp) {
       case 16:
        blend_rgb16 ((uint8_t *)frame->image->data, overlay,
		     frame->sc.output_width, frame->sc.output_height,
		     frame->sc.delivered_width, frame->sc.delivered_height);
        break;
       case 24:
        blend_rgb24 ((uint8_t *)frame->image->data, overlay,
		     frame->sc.output_width, frame->sc.output_height,
		     frame->sc.delivered_width, frame->sc.delivered_height);
        break;
       case 32:
        blend_rgb32 ((uint8_t *)frame->image->data, overlay,
		     frame->sc.output_width, frame->sc.output_height,
		     frame->sc.delivered_width, frame->sc.delivered_height);
        break;
       default:
        printf("xine-lib:video_out_xshm:xshm_overlay_blend: Cannot blend bpp:%i\n", this->bpp);
	/* it should never get here, unless a user tries to play in bpp:8 */
	break;
     }        
   }
}

static void clean_output_area (xshm_driver_t *this, xshm_frame_t *frame) {

  int i;
  
  memcpy( this->sc.border, frame->sc.border, sizeof(this->sc.border) );
  
  XLockDisplay (this->display);

  XSetForeground (this->display, this->gc, this->black.pixel);
  
  for( i = 0; i < 4; i++ ) {
    if( this->sc.border[i].w && this->sc.border[i].h )
      XFillRectangle(this->display, this->drawable, this->gc,
                     this->sc.border[i].x, this->sc.border[i].y,
                     this->sc.border[i].w, this->sc.border[i].h);
  }

  XUnlockDisplay (this->display);
}

static int xshm_redraw_needed (vo_driver_t *this_gen) {
  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  int ret = 0;

  if( this->cur_frame ) {
    
    this->sc.delivered_height   = this->cur_frame->sc.delivered_height;
    this->sc.delivered_width    = this->cur_frame->sc.delivered_width;
    this->sc.video_pixel_aspect = this->cur_frame->sc.video_pixel_aspect;
    if( vo_scale_redraw_needed( &this->sc ) ) {  

      clean_output_area (this, this->cur_frame);
      ret = 1;
    }
  } 
  else
    ret = 1;
      
  return ret;
}

static void xshm_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;

#ifdef LOG
  printf ("video_out_xshm: display frame...\n");
#endif

  if (this->expecting_event) {
#ifdef LOG
    printf ("video_out_xshm: display frame...expecting event (%d)\n",
	    this->expecting_event);
#endif
    this->expecting_event--;
    frame->vo_frame.displayed (&frame->vo_frame);
  } else {

#ifdef LOG
    printf ("video_out_xshm: about to draw frame %d x %d...\n",
	    frame->sc.output_width, frame->sc.output_height);
#endif

    /* 
     * tell gui that we are about to display a frame,
     * ask for offset
     */
    
    this->sc.delivered_height   = frame->sc.delivered_height;
    this->sc.delivered_width    = frame->sc.delivered_width;
    this->sc.video_pixel_aspect = frame->sc.video_pixel_aspect;
    if( vo_scale_redraw_needed( &this->sc ) ) {  

      clean_output_area (this, frame);
    }
    
    if (this->cur_frame) {

      if ( (this->cur_frame->sc.output_width != frame->sc.output_width) 
	   || (this->cur_frame->sc.output_height != frame->sc.output_height)
	   || (this->cur_frame->sc.output_xoffset != frame->sc.output_xoffset)
	   || (this->cur_frame->sc.output_yoffset != frame->sc.output_yoffset) )
	clean_output_area (this, frame);

      this->cur_frame->vo_frame.displayed (&this->cur_frame->vo_frame);
    }

    this->cur_frame = frame;
    
    XLockDisplay (this->display);
#ifdef LOG
    printf ("video_out_xshm: display locked...\n");
#endif

    if (this->use_shm) {

#ifdef LOG
      printf ("video_out_xshm: put image (shm)\n");
#endif
      XShmPutImage(this->display,
		   this->drawable, this->gc, frame->image,
		   0, 0, frame->sc.output_xoffset, frame->sc.output_yoffset,
		   frame->sc.output_width, frame->sc.output_height, True);

      this->expecting_event = 10;

      XFlush(this->display);

    } else {

#ifdef LOG
      printf ("video_out_xshm: put image (plain/remote)\n");
#endif

      XPutImage(this->display,
		this->drawable, this->gc, frame->image,
		0, 0, frame->sc.output_xoffset, frame->sc.output_yoffset,
		frame->sc.output_width, frame->sc.output_height);

      XFlush(this->display);
    }

    XUnlockDisplay (this->display);

  }

#ifdef LOG
  printf ("video_out_xshm: display frame done\n");
#endif
}

static int xshm_get_property (vo_driver_t *this_gen, int property) {

  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  switch (property) {
  case VO_PROP_ASPECT_RATIO:
    return this->sc.user_ratio ;
  case VO_PROP_MAX_NUM_FRAMES:
    return 15;
  case VO_PROP_BRIGHTNESS:
    return this->yuv2rgb_gamma;
  default:
    printf ("video_out_xshm: tried to get unsupported property %d\n", 
	    property);
  }

  return 0;
}

static int xshm_set_property (vo_driver_t *this_gen, 
			      int property, int value) {

  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {

    if (value>=NUM_ASPECT_RATIOS)
      value = ASPECT_AUTO;
    this->sc.user_ratio = value;
    printf ("video_out_xshm: aspect ratio changed to %s\n",
	    vo_scale_aspect_ratio_name(value));

  } else if ( property == VO_PROP_BRIGHTNESS) {

    this->yuv2rgb_gamma = value;
    this->yuv2rgb_factory->set_gamma (this->yuv2rgb_factory, value);

    this->sc.force_redraw = 1;
#ifdef LOG
    printf ("video_out_xshm: gamma changed to %d\n",value);
#endif
  } else {
    printf ("video_out_xshm: tried to set unsupported property %d\n", property);
  }

  return value;
}

static void xshm_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

  /* xshm_driver_t *this = (xshm_driver_t *) this_gen;  */
  if ( property == VO_PROP_BRIGHTNESS) {
    *min = -100;
    *max = +100;
  } else {
    *min = 0;
    *max = 0;
  }
}

static int xshm_gui_data_exchange (vo_driver_t *this_gen, 
				   int data_type, void *data) {

  xshm_driver_t   *this = (xshm_driver_t *) this_gen;

  switch (data_type) {
  case XINE_GUI_SEND_COMPLETION_EVENT: {

    XShmCompletionEvent *cev = (XShmCompletionEvent *) data;

    if (cev->drawable == this->drawable) {
      this->expecting_event = 0;

      /*
      if (this->cur_frame) {
	this->cur_frame->vo_frame.displayed (&this->cur_frame->vo_frame);
	this->cur_frame = NULL; 
      }
      */
    }

  }
  break;

  case XINE_GUI_SEND_EXPOSE_EVENT:
    
  /* FIXME : take care of completion events */
#ifdef LOG
    printf ("video_out_xshm: expose event\n");
#endif

    if (this->cur_frame) {
      
      XExposeEvent * xev = (XExposeEvent *) data;
      
      if (xev->count == 0) {
	int i;
	
	XLockDisplay (this->display);
	
	  if (this->use_shm) {
	    
	    XShmPutImage(this->display,
			 this->drawable, this->gc, this->cur_frame->image,
			 0, 0, this->cur_frame->sc.output_xoffset, this->cur_frame->sc.output_yoffset,
			 this->cur_frame->sc.output_width, this->cur_frame->sc.output_height,
			 False);
	    
	  } else {
	    
	    XPutImage(this->display, 
		      this->drawable, this->gc, this->cur_frame->image,
		      0, 0, this->cur_frame->sc.output_xoffset, this->cur_frame->sc.output_yoffset,
		      this->cur_frame->sc.output_width, this->cur_frame->sc.output_height);
	  }

	XSetForeground (this->display, this->gc, this->black.pixel);

	for( i = 0; i < 4; i++ ) {
	  if( this->sc.border[i].w && this->sc.border[i].h )
	    XFillRectangle(this->display, this->drawable, this->gc,
			   this->sc.border[i].x, this->sc.border[i].y,
			   this->sc.border[i].w, this->sc.border[i].h);
	}

	XFlush (this->display);
	
	XUnlockDisplay (this->display);
      }
      
    }
    
  break;

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    this->drawable = (Drawable) data;

    XLockDisplay (this->display);
    XFreeGC(this->display, this->gc);
    this->gc = XCreateGC (this->display, this->drawable, 0, NULL);
    XUnlockDisplay (this->display);
	
    break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:

    if (this->cur_frame) {
      
      x11_rectangle_t *rect = data;
      int x1, y1, x2, y2;
      
      vo_scale_translate_gui2video(&this->cur_frame->sc,
			       rect->x, rect->y,
			       &x1, &y1);
      vo_scale_translate_gui2video(&this->cur_frame->sc,
			       rect->x + rect->w, rect->y + rect->h,
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

static void xshm_dispose (vo_driver_t *this_gen) {

  xshm_driver_t *this = (xshm_driver_t *) this_gen;
  
  if (this->cur_frame)
    this->cur_frame->vo_frame.dispose (&this->cur_frame->vo_frame);

  this->yuv2rgb_factory->dispose (this->yuv2rgb_factory);

  free (this);
}


static int ImlibPaletteLUTGet(xshm_driver_t *this) {

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
      this->yuv2rgb_cmap = malloc(sizeof(uint8_t) * 32 * 32 * 32);
      for (i = 0; i < 32 * 32 * 32 && j < num_ret; i++)
	this->yuv2rgb_cmap[i] = retval[1+4*retval[j++]+3];

      XFree(retval);
      return 1;
    } else
      XFree(retval);
  }
  return 0;
}


static char *visual_class_name(Visual *visual) {

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


static vo_driver_t *xshm_open_plugin (video_driver_class_t *class_gen, const void *visual_gen) 
{
  xshm_class_t         *class = (xshm_class_t *) class_gen;
  config_values_t      *config = class->config;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  Display              *display = visual->display;

  xshm_driver_t        *this;
  XWindowAttributes     attribs;
  XImage               *myimage;
  XShmSegmentInfo       myshminfo;
  int                   mode;
  int			swapped;
  int			cpu_byte_order;
  XColor                dummy;

  this = malloc (sizeof (xshm_driver_t));

  if (!this) {
    printf ("video_out_xshm: malloc failed\n");
    return NULL;
  }

  memset (this, 0, sizeof(xshm_driver_t));

  this->display		    = visual->display;
  this->screen		    = visual->screen;

  vo_scale_init( &this->sc, 0, 0, config );
  this->sc.frame_output_cb  = visual->frame_output_cb;
  this->sc.dest_size_cb     = visual->dest_size_cb;
  this->sc.user_data        = visual->user_data;
  
  this->sc.user_ratio       = ASPECT_AUTO;
  
  this->sc.scaling_disabled = config->register_bool (config, "video.disable_scaling", 0,
						     _("disable all video scaling (faster!)"),
						     NULL, 10, NULL, NULL);
  this->drawable	    = visual->d;
  this->expecting_event	    = 0;
  this->cur_frame           = NULL;
  this->gc		    = XCreateGC (this->display, this->drawable,
					 0, NULL);

  this->x11_old_error_handler = NULL;

  this->vo_driver.get_capabilities     = xshm_get_capabilities;
  this->vo_driver.alloc_frame          = xshm_alloc_frame;
  this->vo_driver.update_frame_format  = xshm_update_frame_format;
  this->vo_driver.overlay_begin        = NULL; /* not used */
  this->vo_driver.overlay_blend        = xshm_overlay_blend;
  this->vo_driver.overlay_end          = NULL; /* not used */
  this->vo_driver.display_frame        = xshm_display_frame;
  this->vo_driver.get_property         = xshm_get_property;
  this->vo_driver.set_property         = xshm_set_property;
  this->vo_driver.get_property_min_max = xshm_get_property_min_max;
  this->vo_driver.gui_data_exchange    = xshm_gui_data_exchange;
  this->vo_driver.dispose              = xshm_dispose;
  this->vo_driver.redraw_needed        = xshm_redraw_needed;

  XAllocNamedColor (this->display,
		    DefaultColormap (this->display, this->screen),
		    "black", &this->black, &dummy);


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
    case 32:
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

  this->yuv2rgb_mode  = mode;
  this->yuv2rgb_swap  = swapped;
  this->yuv2rgb_gamma = config->register_range (config, "video.xshm_gamma", 0,
						-100, 100, 
						_("gamma correction for XShm driver"),
						NULL, 0, NULL, NULL);

  this->yuv2rgb_factory = yuv2rgb_factory_init (mode, swapped, 
						this->yuv2rgb_cmap);
  this->yuv2rgb_factory->set_gamma (this->yuv2rgb_factory, this->yuv2rgb_gamma);

  return &this->vo_driver;
}

/*
 * class functions
 */

static char* xshm_get_identifier (video_driver_class_t *this_gen) {
  return "XShm";
}

static char* xshm_get_description (video_driver_class_t *this_gen) {
  return _("xine video output plugin using the MIT X shared memory extension");
}

static void xshm_dispose_class (video_driver_class_t *this_gen) {

  xshm_class_t         *this = (xshm_class_t *) this_gen;

  free (this);
}

static void *xshm_init_class (xine_t *xine, void *visual_gen) {

  xshm_class_t	       *this = (xshm_class_t *) malloc (sizeof (xshm_class_t));

  this->driver_class.open_plugin     = xshm_open_plugin;
  this->driver_class.get_identifier  = xshm_get_identifier;
  this->driver_class.get_description = xshm_get_description;
  this->driver_class.dispose         = xshm_dispose_class;

  this->config          = xine->config;

  return this;
}


static vo_info_t vo_info_xshm = {
  6,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};


/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_OUT, 14, "xshm", XINE_VERSION_CODE, &vo_info_xshm, xshm_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
