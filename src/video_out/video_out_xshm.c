/* 
 * Copyright (C) 2000 the xine project
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
 * $Id: video_out_xshm.c,v 1.7 2001/06/14 09:19:44 guenter Exp $
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

#include "video_out.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <errno.h>

#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <pthread.h>

#include "xine_internal.h"
#include "monitor.h"
#include "utils.h"
#include "video_out_x11.h"
#include "yuv2rgb.h"

uint32_t xine_debug;

extern int XShmGetEventBase(Display *);

typedef struct xshm_frame_s {
  vo_frame_t         vo_frame;

  int                width, height;
  int                rgb_width, rgb_height;
  int                ratio_code;

  XImage            *image;
  uint8_t           *rgb_dst;
  int                stripe_inc;
  XShmSegmentInfo    shminfo;

} xshm_frame_t;

typedef struct xshm_driver_s {

  vo_driver_t      vo_driver;

  config_values_t *config;

  /* X11 / Xv related stuff */
  Display         *display;
  int              screen;
  Drawable         drawable;
  XVisualInfo      vinfo;
  GC               gc;
  XColor           black;
  int              use_shm;
  int              depth, bpp, bytes_per_pixel;
  int              expecting_event;

  yuv2rgb_t       *yuv2rgb;

  xshm_frame_t    *cur_frame;

  /* size / aspect ratio calculations */
  int              delivered_width;      /* everything is set up for these frame dimensions    */
  int              delivered_height;     /* the dimension as they come from the decoder        */
  int              delivered_ratio_code;
  double           ratio_factor; /* output frame must fullfill: height = width * ratio_factor  */
  int              output_width;         /* frames will appear in this size (pixels) on screen */
  int              output_height;
  int              output_xoffset;
  int              output_yoffset;
  int              stripe_height;

  int              user_ratio;

  int              dest_width;           /* size of image gui has most recently adopted to     */
  int              dest_height;
  int              dest_x;
  int              dest_y;

  /* display anatomy */
  double           display_ratio;        /* given by visual parameter from init function */

  /* gui callbacks */

  void (*request_dest_size) (int video_width, int video_height,
			     int *dest_x, int *dest_y, 
			     int *dest_height, int *dest_width);

  void (*calc_dest_size) (int video_width, int video_height, 
			  int *dest_width, int *dest_height);

} xshm_driver_t;

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
			      this->vinfo.visual,
			      this->vinfo.depth,
			      ZPixmap, NULL,
			      shminfo,
			      width, 
			      height);

    if (myimage == NULL )  {
      printf ("video_out_xshm: shared memory error when allocating image\n");
      printf ("=> not using MIT Shared Memory extension.\n");
      this->use_shm = 0;
      goto finishShmTesting;
    }
  
    this->bpp = myimage->bits_per_pixel;
    this->bytes_per_pixel = this->bpp / 8;
    
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
      this->use_shm = 0;
    }
  finishShmTesting:
    x11_DeInstallXErrorHandler(this);

  }

  /*
   * fall back to plain X11 if necessary
   */

  if (!this->use_shm) {

    char *data;

    switch (this->vinfo.depth) {
    case 15:
    case 16:
      this->bpp = 16;
      break;
    case 24:
      this->bpp = 24;
      break;
    case 32:
      this->bpp = 32;
    default:
      printf ("video_out_xshm: !!!!!!! unknown depth : %d\n",this->vinfo.depth);
      this->bpp = 16;
    }      


    this->bytes_per_pixel = this->bpp / 8;

    data = xmalloc (width * this->bytes_per_pixel * height);

    myimage = XCreateImage (this->display,
			    this->vinfo.visual,
			    this->vinfo.depth,
			    ZPixmap, 0,
			    data,
			    width, 
			    height,
			    8, width * this->bytes_per_pixel);
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
    shmctl (shminfo->shmid, IPC_RMID, 0);

  } else {

    XDestroyImage (myimage);

  }
}

/*
 * and now, the driver functions
 */

static uint32_t xshm_get_capabilities (vo_driver_t *this_gen) {
  return VO_CAP_COPIES_IMAGE | VO_CAP_YV12;
}

static void xshm_frame_copy (vo_frame_t *vo_img, uint8_t **src) {
  xshm_frame_t  *frame = (xshm_frame_t *) vo_img ;
  xshm_driver_t *this = (xshm_driver_t *) vo_img->instance->driver;

  this->yuv2rgb->yuv2rgb_fun (this->yuv2rgb, frame->rgb_dst,
			      src[0], src[1], src[2]);

  frame->rgb_dst += frame->stripe_inc; 
}

static void xshm_frame_field (vo_frame_t *vo_img, int which_field) {
  /* FIXME: implement */
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

  xshm_frame_t     *frame ;

  frame = (xshm_frame_t *) malloc (sizeof (xshm_frame_t));
  memset (frame, 0, sizeof(xshm_frame_t));

  if (frame==NULL) {
    printf ("xshm_alloc_frame: out of memory\n");
  }

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
  double corr_factor;
  int ideal_width, ideal_height;
  int dest_width, dest_height;

  /*
   * aspect ratio calculation
   */

  image_ratio = 
    (double) this->delivered_width / (double) this->delivered_height;

  switch (this->user_ratio) { 
  case ASPECT_AUTO: 
    switch (this->delivered_ratio_code) {
    case 3:  /* anamorphic     */
      desired_ratio = 16.0 /9.0;
      break;
    case 42: /* probably non-mpeg stream => don't touch aspect ratio */
      desired_ratio = image_ratio;
      break;
    case 0: /* forbidden       */
      fprintf (stderr, "invalid ratio, using 4:3\n");
    case 1: /* "square" => 4:3 */
    case 2: /* 4:3             */
    default:
      xprintf (VIDEO, "unknown aspect ratio (%d) in stream => using 4:3\n", 
	       this->delivered_ratio_code);
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
  if (ideal_width<400) {
    ideal_width  *=2;
    ideal_height *=2;
  }

  ideal_width &= 0xFFFFFE0;

  this->calc_dest_size (ideal_width, ideal_height, 
			&dest_width, &dest_height);


  /*
   * make the frames fit into the given destination area
   */

  if ( ((double) dest_width / this->ratio_factor) < dest_height ) {

    this->output_width   = dest_width ;
    this->output_height  = (double) dest_width / this->ratio_factor ;
    this->output_xoffset = 0;
    this->output_yoffset = (dest_height - this->output_height) / 2;

  } else {
    
    this->output_width    = (double) dest_height * this->ratio_factor ;
    this->output_height   = dest_height;

    this->output_xoffset  = (dest_width - this->output_width) / 2;
    this->output_yoffset  = 0;

  } 
}

static void xshm_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format) {

  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;

  if ((frame->rgb_width != this->output_width) 
      || (frame->rgb_height != this->output_height)
      || (frame->width != width)
      || (frame->height != height)
      || (frame->ratio_code != ratio_code)) {

    int image_size;

    this->delivered_width      = width;
    this->delivered_height     = height;
    this->delivered_ratio_code = ratio_code;

    xshm_calc_output_size (this);

    this->stripe_height = 16 * this->output_height / this->delivered_height;

    /*
    printf ("video_out_xshm: updating frame to %d x %d\n",
	    this->output_width,this->output_height);
	    */

    XLockDisplay (this->display); 

    /*
     * (re-) allocate xvimage
     */

    if (frame->image) {

      dispose_ximage (this, &frame->shminfo, frame->image);

      /* FIXME: free yuv (base) memory !!!!! */


      frame->image = NULL;
    }

    frame->image = create_ximage (this, &frame->shminfo, 
				  this->output_width, this->output_height);

    XUnlockDisplay (this->display); 

    image_size = width * height;
    frame->vo_frame.base[0] = xmalloc_aligned(16,image_size);
    frame->vo_frame.base[1] = xmalloc_aligned(16,image_size/4);
    frame->vo_frame.base[2] = xmalloc_aligned(16,image_size/4);

    frame->width  = width;
    frame->height = height;

    frame->rgb_width  = this->output_width;
    frame->rgb_height = this->output_height;

    frame->ratio_code = ratio_code;
  
    yuv2rgb_setup (this->yuv2rgb,
		   this->delivered_width,
		   16,
		   this->delivered_width,
		   this->delivered_width/2,
		   this->output_width,
		   this->stripe_height,
		   frame->image->bytes_per_line);

  }

  if (frame->image) {
    frame->rgb_dst    = frame->image->data;
    frame->stripe_inc = this->stripe_height * frame->image->bytes_per_line;
  }
}

static void xshm_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  xshm_driver_t  *this = (xshm_driver_t *) this_gen;
  xshm_frame_t   *frame = (xshm_frame_t *) frame_gen;
  
  /* printf ("video_out_xshm: display frame %d\n", frame); */

  if (this->expecting_event) {

    frame->vo_frame.displayed (&frame->vo_frame);
    
  } else {

    if ( (frame->rgb_width != this->dest_width)
	 || (frame->rgb_height != this->dest_height) ) {
      
      int width, height; /* too late for these -> ignored */
      
      this->request_dest_size (frame->rgb_width, frame->rgb_height, 
			       &this->dest_x, &this->dest_y, &width, &height);
      
      this->dest_width = frame->rgb_width;
      this->dest_height = frame->rgb_height;

    }
    
    XLockDisplay (this->display);

    this->cur_frame = frame;

    if (this->use_shm) {
    
      XShmPutImage(this->display, 
		   this->drawable, this->gc, frame->image,
		   0, 0,  this->output_xoffset, this->output_yoffset,
		   frame->rgb_width, frame->rgb_height, True);

      this->expecting_event = 1;

    } else {
      XPutImage(this->display, 
		this->drawable, this->gc, frame->image,
		0, 0,  0, 0,
		frame->rgb_width, frame->rgb_height);
      frame->vo_frame.displayed (&frame->vo_frame);
    }
    
    XFlush(this->display); 
    
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

static int xshm_set_property (vo_driver_t *this_gen, 
			      int property, int value) {

  xshm_driver_t *this = (xshm_driver_t *) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>ASPECT_DVB)
      value = ASPECT_AUTO;
    this->user_ratio = value;

    xshm_calc_output_size (this);

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

static int xshm_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data) {

  xshm_driver_t     *this = (xshm_driver_t *) this_gen;
  /* x11_rectangle_t *area; */

  switch (data_type) {
  case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:

    /* area = (x11_rectangle_t *) data; */

    xshm_calc_output_size (this);
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

    if (xev->count == 0) {

      if (this->use_shm) {
	
	XShmPutImage(this->display, 
		     this->drawable, this->gc, this->cur_frame->image,
		     0, 0,  this->output_xoffset, this->output_yoffset,
		     this->cur_frame->rgb_width, this->cur_frame->rgb_height, 
		     False);
    
      } else {
	XPutImage(this->display, 
		  this->drawable, this->gc, this->cur_frame->image,
		  0, 0,  this->output_xoffset, this->output_yoffset,
		  this->cur_frame->rgb_width, this->cur_frame->rgb_height);
      }
    }

  }
  break;

  case GUI_DATA_EX_DRAWABLE_CHANGED:
    this->drawable = (Drawable) data;
    this->gc       = XCreateGC (this->display, this->drawable, 0, NULL);
    break;
  }

  return 0;
}

static void xshm_exit (vo_driver_t *this_gen) {

  /* xshm_driver_t *this = (xshm_driver_t *) this_gen; */

}


vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  xshm_driver_t        *this;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  Display              *display = NULL;
  XColor                dummy;
  XWindowAttributes     attribs;
  XImage               *myimage;
  XShmSegmentInfo       myshminfo;

  visual = (x11_visual_t *) visual_gen;
  display = visual->display;
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  /*
   * allocate plugin struct
   */

  this = malloc (sizeof (xshm_driver_t));

  if (!this) {
    printf ("video_out_xv: malloc failed\n");
    return NULL;
  }

  memset (this, 0, sizeof(xshm_driver_t));

  this->config            = config;
  this->display           = visual->display;
  this->screen            = visual->screen;
  this->display_ratio     = visual->display_ratio;
  this->request_dest_size = visual->request_dest_size;
  this->calc_dest_size    = visual->calc_dest_size;
  this->output_xoffset    = 0;
  this->output_yoffset    = 0;
  this->output_width      = 0;
  this->output_height     = 0;
  this->drawable          = visual->d;
  this->expecting_event   = 0;
  this->gc                = XCreateGC (this->display, this->drawable, 0, NULL);

  XAllocNamedColor(this->display, 
		   DefaultColormap(this->display, this->screen), 
		   "black", &this->black, &dummy);

  this->vo_driver.get_capabilities     = xshm_get_capabilities;
  this->vo_driver.alloc_frame          = xshm_alloc_frame;
  this->vo_driver.update_frame_format  = xshm_update_frame_format;
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
  
  XGetWindowAttributes(display, DefaultRootWindow(display), &attribs);
  this->depth = attribs.depth;
  
  if (this->depth != 15 && this->depth != 16 && this->depth != 24 && this->depth != 32)  {
    /* The root window may be 8bit but there might still be
     * visuals with other bit depths. For example this is the 
     * case on Sun/Solaris machines.
     */
    this->depth = 24;
  }

  if (this->depth>16) 
    printf ("\n\nWARNING: current display depth is %d. For better performance\na depth of 16 bpp is recommended!\n\n",
	    this->depth);

  XMatchVisualInfo(display, this->screen, this->depth, TrueColor, &this->vinfo);

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
   * try to create shared image 
   * to find out if MIT shm really works
   * and what bpp it uses
   */

  myimage = create_ximage (this, &myshminfo, 100, 100);
  dispose_ximage (this, &myshminfo, myimage);

  this->yuv2rgb = yuv2rgb_init (MODE_16_RGB); /* FIXME mode */

  return &this->vo_driver;
}

static vo_info_t vo_info_shm = {
  VIDEO_OUT_IFACE_VERSION,
  "XShm",
  "xine video output plugin using the MIT X shared memory extension",
  VISUAL_TYPE_X11,
  5
};

vo_info_t *get_video_out_plugin_info() {
  return &vo_info_shm;
}

