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
 * $Id: video_out_xv.c,v 1.22 2001/05/26 00:48:47 guenter Exp $
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

#include "monitor.h"
#include "video_out.h"
#include "video_out_x11.h"
#include "xine_internal.h"

uint32_t xine_debug;

/* override xprintf definition */
/* #define xprintf(LVL, FMT, ARGS...) { printf(FMT, ##ARGS); } */

typedef struct xv_property_s {
  int   value;
  int   min;
  int   max;
  Atom  atom;
  char *key;
} xv_property_t;

typedef struct xv_frame_s {
  vo_frame_t         vo_frame;

  int                width, height, ratio_code, format;

  XvImage           *image;
  XShmSegmentInfo    shminfo;

} xv_frame_t;

typedef struct xv_driver_s {

  vo_driver_t      vo_driver;

  config_values_t *config;

  /* X11 / Xv related stuff */
  Display         *display;
  int              screen;
  Drawable         drawable;
  unsigned int     xv_format_rgb, xv_format_yv12, xv_format_yuy2;
  XVisualInfo      vinfo;
  GC               gc;
  unsigned int     xv_port;

  xv_property_t    props[VO_NUM_PROPERTIES];
  uint32_t         capabilities;

  xv_frame_t      *cur_frame;

  /* size / aspect ratio calculations */
  int              delivered_width;      /* everything is set up for these frame dimensions    */
  int              delivered_height;     /* the dimension as they come from the decoder        */
  int              delivered_ratio_code;
  double           ratio_factor; /* output frame must fullfill: height = width * ratio_factor  */
  int              output_width;         /* frames will appear in this size (pixels) on screen */
  int              output_height;
  int              output_xoffset;
  int              output_yoffset;

  /* display anatomy */
  double           display_ratio;        /* given by visual parameter from init function */

  /* gui callback */

  void (*request_dest_size) (int video_width, int video_height,
			     int *dest_x, int *dest_y, 
			     int *dest_height, int *dest_width);

} xv_driver_t;

static uint32_t xv_get_capabilities (vo_driver_t *this_gen) {

  xv_driver_t *this = (xv_driver_t *) this_gen;

  return this->capabilities;
}

static vo_frame_t *xv_alloc_frame (vo_driver_t *this_gen) {

  xv_frame_t     *frame ;

  frame = (xv_frame_t *) malloc (sizeof (xv_frame_t));
  memset (frame, 0, sizeof(xv_frame_t));

  if (frame==NULL) {
    printf ("xv_alloc_frame: out of memory\n");
  }

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  return (vo_frame_t *) frame;
}

static void xv_update_frame_format (vo_driver_t *this_gen, vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height, int ratio_code,
				    int format) {

  xv_driver_t  *this = (xv_driver_t *) this_gen;
  xv_frame_t   *frame = (xv_frame_t *) frame_gen;
  unsigned int  xv_format;

  if ((frame->width != width) || (frame->height != height) || (frame->format != format)) {

    /* printf ("video_out_xv: updating frame to %d x %d (ratio=%d, format=%08x)\n",width,height,ratio_code,format); */

    XLockDisplay (this->display); 

    /*
     * (re-) allocate xvimage
     */

    if (frame->image) {
      XShmDetach (this->display, &frame->shminfo);
      
      XFree (frame->image);
      shmdt (frame->shminfo.shmaddr);
      shmctl (frame->shminfo.shmid, IPC_RMID,NULL);

      frame->image = NULL;
    }

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
      fprintf (stderr, "xv_update_frame_format: unknown format %08x\n",format);
      exit (1);
    }

    frame->image = XvShmCreateImage(this->display, this->xv_port, xv_format, 0,
				    width, height, &frame->shminfo);
  
    if (frame->image == NULL )  {
      fprintf(stderr, "xv_image_format: XvShmCreateImage failed.\n");
      exit (1);
    }

    frame->shminfo.shmid=shmget(IPC_PRIVATE, 
				frame->image->data_size, 
				IPC_CREAT | 0777);

    if (frame->image->data_size==0) {  
      fprintf(stderr, "xv_update_frame_format: XvShmCreateImage returned a zero size\n");
      exit (1);
    }   

    if (frame->shminfo.shmid < 0 ) {
      perror("xv_update_frame_format: shared memory error in shmget: "); 
      exit (1);
    }

    frame->shminfo.shmaddr  = (char *) shmat(frame->shminfo.shmid, 0, 0);
  
    if (frame->shminfo.shmaddr == NULL) {
      fprintf(stderr, "xv_update_frame_format: shared memory error (address error NULL)\n");
      exit (1);
    }

    if (frame->shminfo.shmaddr == ((char *) -1)) {
      fprintf(stderr, "xv_update_frame_format: shared memory error (address error)\n");
      exit (1);
    }

    frame->shminfo.readOnly = False;
    frame->image->data = frame->shminfo.shmaddr;

    XShmAttach(this->display, &frame->shminfo);

    XSync(this->display, False);
    shmctl(frame->shminfo.shmid, IPC_RMID, 0);
  
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

static void xv_adapt_to_output_area (xv_driver_t *this, int dest_x, int dest_y, int dest_width, int dest_height) {

  /*
   * make the frames fit into the given destination area
   */

  if ( ((double) dest_width / this->ratio_factor) < dest_height ) {

    this->output_width   = dest_width ;
    this->output_height  = (double) dest_width / this->ratio_factor ;
    this->output_xoffset = dest_x;
    this->output_yoffset = dest_y + (dest_height - this->output_height) / 2;

  } else {
    
    this->output_width    = (double) dest_height * this->ratio_factor ;
    this->output_height   = dest_height;
    this->output_xoffset  = dest_x + (dest_width - this->output_width) / 2;
    this->output_yoffset  = dest_y;
  } 
}

static void xv_calc_format (xv_driver_t *this, int width, int height, int ratio_code) {

  double image_ratio, desired_ratio;
  double corr_factor;
  int ideal_width, ideal_height;
  int dest_x, dest_y, dest_width, dest_height;

  this->delivered_width      = width;
  this->delivered_height     = height;
  this->delivered_ratio_code = ratio_code;

  /*
   * aspect ratio calculation
   */

  image_ratio = (double) this->delivered_width / (double) this->delivered_height;

  xprintf (VERBOSE | VIDEO, "display_ratio : %f\n",this->display_ratio);
  xprintf (VERBOSE | VIDEO, "stream aspect ratio : %f , code : %d\n", image_ratio, ratio_code);

  switch (this->props[VO_PROP_ASPECT_RATIO].value) {
  case ASPECT_AUTO: 
    switch (ratio_code) {
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
      xprintf (VIDEO, "unknown aspect ratio (%d) in stream => using 4:3\n", ratio_code);
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

  /*
   * ask gui to adapt to this size
   */

  this->request_dest_size (ideal_width, ideal_height, 
			   &dest_x, &dest_y, &dest_width, &dest_height);
  
  xv_adapt_to_output_area (this, dest_x, dest_y, dest_width, dest_height);
}

static void xv_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  xv_driver_t  *this = (xv_driver_t *) this_gen;
  xv_frame_t   *frame = (xv_frame_t *) frame_gen;

  if ( (frame->width != this->delivered_width) || (frame->height != this->delivered_height) 
       || (frame->ratio_code != this->delivered_ratio_code) ) {

    xv_calc_format (this, frame->width, frame->height, frame->ratio_code);
  }

  XLockDisplay (this->display);

  XvShmPutImage(this->display, this->xv_port, this->drawable, this->gc, frame->image,
		0, 0,  frame->width, frame->height,
		this->output_xoffset, this->output_yoffset,
		this->output_width, this->output_height, False);


  XFlush(this->display); 

  XUnlockDisplay (this->display);
  
  /* FIXME: this should be done using the completion event */
  if (this->cur_frame) {
    this->cur_frame->vo_frame.displayed (&this->cur_frame->vo_frame);
  }

  this->cur_frame = frame;
}

static int xv_get_property (vo_driver_t *this_gen, int property) {
  
  xv_driver_t *this = (xv_driver_t *) this_gen;

  return this->props[property].value;
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

    this->config->set_int (this->config, this->props[property].key, 
			   this->props[property].value);

    return this->props[property].value;
  } else {
    switch (property) {
    case VO_PROP_INTERLACED:
      this->props[property].value = value;
      printf("video_out_xv: VO_PROP_INTERLACED(%d)\n", this->props[property].value);
      break;
    case VO_PROP_ASPECT_RATIO:
      this->props[property].value = value;
      printf("video_out_xv: VO_PROP_ASPECT_RATIO(%d)\n", this->props[property].value);
      break;
    }
  }
  
  return value;
}

static void xv_get_property_min_max (vo_driver_t *this_gen, 
				     int property, int *min, int *max) {

  xv_driver_t *this = (xv_driver_t *) this_gen;

  printf("this property(%d): min=%d, max=%d\n", property,
	 this->props[property].min,
	 this->props[property].max);

  *min = this->props[property].min;
  *max = this->props[property].max;
}

static int xv_gui_data_exchange (vo_driver_t *this_gen, int data_type, void *data) {

  xv_driver_t *this = (xv_driver_t *) this_gen;
  x11_rectangle_t *area;

  switch (data_type) {
  case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:

    area = (x11_rectangle_t *) data;

    xv_adapt_to_output_area (this, area->x, area->y, area->w, area->h);

    break;
  case GUI_DATA_EX_COMPLETION_EVENT:
    
    /* FIXME : implement */

    break;

  case GUI_DATA_EX_DRAWABLE_CHANGED:
    this->drawable = (Drawable) data;
    break;
  }

  return 0;
}

static void xv_exit (vo_driver_t *this_gen) {

  xv_driver_t *this = (xv_driver_t *) this_gen;

  if(XvUngrabPort (this->display, this->xv_port, CurrentTime) != Success) {
    fprintf(stderr, "xv_exit: XvUngrabPort() failed.\n");
  }
}

static int xv_check_yv12 (Display *display, XvPortID port)
{
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
				 uint32_t capability, int property, 
				 XvAttribute attr, int base_id, char *str_prop) {

  int          nDefault;
  
  this->capabilities |= capability;
  this->props[property].min  = attr.min_value;
  this->props[property].max  = attr.max_value;
  this->props[property].atom = XInternAtom (this->display, str_prop, False);
  this->props[property].key  = str_prop;
  
  XvGetPortAttribute (this->display, this->xv_port, this->props[property].atom, &nDefault);

  xv_set_property (&this->vo_driver, property, this->config->lookup_int (this->config, str_prop, nDefault) );
}

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  xv_driver_t          *this;
  Display              *display = NULL;
  unsigned int          adaptor_num, adaptors, i, j, formats;
  unsigned int          ver,rel,req,ev,err;
  unsigned int          xv_port;
  XvAttribute          *attr;
  XvAdaptorInfo        *adaptor_info;
  XvImageFormatValues  *fo;
  int                   nattr;
  x11_visual_t         *visual;


  visual = (x11_visual_t *) visual_gen;
  display = visual->display;
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

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
  adaptor_num = 0;

  while ( (adaptor_num < adaptors) && !xv_port) {
    if (adaptor_info[adaptor_num].type & XvImageMask)
      for (j = 0; j < adaptor_info[adaptor_num].num_ports; j++)
	if (( !(xv_check_yv12 (display, adaptor_info[adaptor_num].base_id + j))) 
	    && (XvGrabPort (display, adaptor_info[adaptor_num].base_id + j, 0) == Success)) {
	  xv_port = adaptor_info[adaptor_num].base_id + j;
	  break; 
	}

    adaptor_num++;
  }


  if (!xv_port) {
    printf ("video_out_xv: Xv extension is present but I couldn't find a usable yuv12 port.\n");
    printf ("              Looks like your graphics hardware driver doesn't support Xv?!\n");
    XvFreeAdaptorInfo (adaptor_info);
    return NULL;
  } else
    printf ("video_out_xv: using Xv port %d for hardware colorspace conversion and scaling.\n", xv_port);

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
  this->screen            = visual->screen;
  this->display_ratio     = visual->display_ratio;
  this->request_dest_size = visual->request_dest_size;
  this->output_xoffset    = 0;
  this->output_yoffset    = 0;
  this->output_width      = 0;
  this->output_height     = 0;
  this->drawable          = visual->d;
  this->gc                = XCreateGC (this->display, this->drawable, 0, NULL);
  this->xv_port           = xv_port;
  this->capabilities      = 0;

  this->vo_driver.get_capabilities     = xv_get_capabilities;
  this->vo_driver.alloc_frame          = xv_alloc_frame;
  this->vo_driver.update_frame_format  = xv_update_frame_format;
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
    this->props[i].key   = NULL;
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
      
      if(attr[k].flags & XvSettable) {
	if(!strcmp(attr[k].name, "XV_HUE")) {
	  xv_check_capability (this, VO_CAP_HUE, 
			       VO_PROP_HUE, attr[k],
			       adaptor_info[i].base_id, "XV_HUE");
	  printf("XV_HUE ");
	}
	else if(!strcmp(attr[k].name, "XV_SATURATION")) {
	  xv_check_capability (this, VO_CAP_SATURATION, 
			       VO_PROP_SATURATION, attr[k],
			       adaptor_info[i].base_id, "XV_SATURATION");
	  printf("XV_SATURATION ");
	}
	else if(!strcmp(attr[k].name, "XV_BRIGHTNESS")) {
	  xv_check_capability (this, VO_CAP_BRIGHTNESS,
			       VO_PROP_BRIGHTNESS, attr[k],
			       adaptor_info[i].base_id, "XV_BRIGHTNESS");
	  printf("XV_BRIGHTNESS ");
	}
	else if(!strcmp(attr[k].name, "XV_CONTRAST")) {
	  xv_check_capability (this, VO_CAP_CONTRAST, 
			       VO_PROP_CONTRAST, attr[k],
			       adaptor_info[i].base_id, "XV_CONTRAST");
	  printf("XV_CONTRAST ");
	}
	else if(!strcmp(attr[k].name, "XV_COLORKEY")) {
	  xv_check_capability (this, VO_CAP_COLORKEY, 
			       VO_PROP_COLORKEY, attr[k],
			       adaptor_info[i].base_id, "XV_COLORKEY");
	  printf("XV_COLORKEY ");
	}
      }
    }
    printf("\n");
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
    xprintf(VERBOSE|VIDEO, "video_out_xv: Xv image format: 0x%x (%4.4s) %s\n", 
	    fo[i].id, (char*)&fo[i].id, 
	    (fo[i].format == XvPacked) ? "packed" : "planar");      
    if (fo[i].id == IMGFMT_YV12)  {
      this->xv_format_yv12 = fo[i].id;
      this->capabilities |= VO_CAP_YV12;
      printf ("video_out_xv: this adaptor supports the yv12 format.\n");
    } else if (fo[i].id == IMGFMT_YUY2) {
      this->xv_format_yuy2 = fo[i].id;
      this->capabilities |= VO_CAP_YUY2;
      printf ("video_out_xv: this adaptor supports the yuy2 format.\n");
    } else if (fo[i].id == IMGFMT_RGB) {
      this->xv_format_rgb = fo[i].id;
      this->capabilities |= VO_CAP_RGB;
      printf ("video_out_xv: this adaptor supports the rgb format.\n");
    }
  }

  return &this->vo_driver;
}

static vo_info_t vo_info_xv = {
  VIDEO_OUT_IFACE_VERSION,
  "Xv",
  "xine video output plugin using the MIT X video extension",
  VISUAL_TYPE_X11,
  10
};

vo_info_t *get_video_out_plugin_info() {
  return &vo_info_xv;
}


#endif
