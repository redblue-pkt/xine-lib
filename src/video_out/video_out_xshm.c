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
 * $Id: video_out_xshm.c,v 1.2 2001/05/28 12:35:54 guenter Exp $
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
#include "video_out_x11.h"
#include "yuv2rgb.h"

uint32_t xine_debug;

extern int XShmGetEventBase(Display *);

typedef struct xshm_property_s {
  int   value;
  int   min;
  int   max;
  Atom  atom;
  char *key;
} xshm_property_t;

typedef struct xshm_frame_s {
  vo_frame_t         vo_frame;

  int                width, height, ratio_code, format;

  XImage            *image;
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

  xshm_property_t  props[VO_NUM_PROPERTIES];
  uint32_t         capabilities;

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

  /* display anatomy */
  double           display_ratio;        /* given by visual parameter from init function */

  /* gui callback */

  void (*request_dest_size) (int video_width, int video_height,
			     int *dest_x, int *dest_y, 
			     int *dest_height, int *dest_width);

} xshm_driver_t;

int HandleXError (Display *gDisplay, XErrorEvent *xevent) {
  
  xprintf (VERBOSE|VIDEO, "\n\n*** X ERROR *** \n\n\n");

  gXshm.bFail = 1;
  return 0;

}

static void x11_InstallXErrorHandler ()
{
  XSetErrorHandler (HandleXError);
  XFlush (gDisplay);
}

static void x11_DeInstallXErrorHandler ()
{
  XSetErrorHandler (NULL);
  XFlush (gDisplay);
}

static int get_capabilities_xshm () {
  return VO_CAP_COPIES_IMAGE | VO_CAP_YV12;
}

static void setup_window_xshm () {
  char                 *hello = "xine video output";
  Colormap              theCmap = 0;
  XSizeHints            hint;
  XWMHints             *wm_hint;
  XEvent                xev;
  XGCValues             xgcv;
  XColor                background, ignored;
  XSetWindowAttributes  attr;
  int                   ww, wh;
  float                 aspect;
  Atom                  WM_DELETE_WINDOW;
  XClassHint            *xshm_xclasshint;

  XLOCK ();

    
  if((xshm_xclasshint = XAllocClassHint()) != NULL) {
    xshm_xclasshint->res_name = "Xine Xshm Video";
    xshm_xclasshint->res_class = "Xine";
  }

  if (gXshm.bFullscreen) {

    ww = DisplayWidth (gDisplay, gXshm.screen);
    wh = DisplayHeight (gDisplay, gXshm.screen);
    
    /*
     * zoom to fullscreen
     */

    if (gXshm.dest_width < ww) {
      aspect = (float) gXshm.dest_width / (float) gXshm.dest_height ;

      gXshm.dest_width = ww;
      gXshm.dest_height = ww / aspect;
    }

    gXshm.image_xoff = ( ww - gXshm.dest_width) / 2;
    gXshm.image_yoff = ( wh - gXshm.dest_height) / 2;


    if (gXshm.window) {

      if (gXshm.bIsFullscreen) {
	XUNLOCK ();
	return;
      }

      XDestroyWindow(gDisplay, gXshm.window);
      gXshm.window = 0;

    }

    gXshm.bIsFullscreen = 1;

    /*
     * open fullscreen window
     */

    if (XAllocNamedColor (gDisplay, DefaultColormap (gDisplay, gXshm.screen), 
			  "black",
			  &background, &ignored) == 0) {
      fprintf (stderr, "Cannot allocate color black\n");
      exit(1);
    }

    attr.background_pixel  = background.pixel;
    attr.override_redirect = True;

    gXshm.window =
      XCreateWindow (gDisplay, RootWindow (gDisplay, gXshm.screen), 0, 0, 
		     ww, wh, 0, gXshm.depth,
		     CopyFromParent, DefaultVisual (gDisplay, gXshm.screen),
		     CWBackPixel|CWOverrideRedirect, &attr);

    if(xshm_xclasshint != NULL)
      XSetClassHint(gDisplay, gXshm.window, xshm_xclasshint);

    gVideoWin = gXshm.window;

  } else {

    if (gXshm.window) {

      if (gXshm.bIsFullscreen) {
	XDestroyWindow(gDisplay, gXshm.window);
	gXshm.window = 0;
      } else {
	
	XResizeWindow (gDisplay, gXshm.window, gXshm.dest_width, 
		       gXshm.dest_height);

	XFlush(gDisplay);
	XSync(gDisplay, False);
	XUNLOCK ();
	return;
	
      }
    }

    gXshm.bIsFullscreen = 0;


    hint.x = 0;
    hint.y = 0;
    hint.width  = gXshm.dest_width;
    hint.height = gXshm.dest_height;
    hint.flags  = PPosition | PSize;
    
    theCmap   = XCreateColormap(gDisplay, RootWindow(gDisplay,gXshm.screen), 
			      gXshm.vinfo.visual, AllocNone);
  
    attr.background_pixel = 0;
    attr.border_pixel     = 1;
    attr.colormap         = theCmap;

    gXshm.window = XCreateWindow(gDisplay, RootWindow(gDisplay, gXshm.screen),
			       hint.x, hint.y, hint.width, hint.height, 4, 
			       gXshm.depth,CopyFromParent,gXshm.vinfo.visual,
			       CWBackPixel | CWBorderPixel |CWColormap,&attr);

    if(xshm_xclasshint != NULL)
      XSetClassHint(gDisplay, gXshm.window, xshm_xclasshint);

    gVideoWin = gXshm.window;
    
    /* Tell other applications about this window */

    XSetStandardProperties(gDisplay, gXshm.window, hello, hello, 
			   None, NULL, 0, &hint);

    gXshm.image_xoff = 0;
    gXshm.image_yoff = 0;
  }
  
  XSelectInput(gDisplay, gXshm.window, StructureNotifyMask | ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);

  wm_hint = XAllocWMHints();
  if (wm_hint != NULL) {
    wm_hint->input = True;
    wm_hint->initial_state = NormalState;
    wm_hint->flags = InputHint | StateHint;
    XSetWMHints(gDisplay, gXshm.window, wm_hint);
    XFree(wm_hint);
  }

  WM_DELETE_WINDOW = XInternAtom(gDisplay, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(gDisplay, gXshm.window, &WM_DELETE_WINDOW, 1);

  /* Map window. */
  
  XMapWindow(gDisplay, gXshm.window);
  
  /* Wait for map. */
  do  {
    XMaskEvent(gDisplay, 
	       StructureNotifyMask, 
	       &xev) ;
  } while (xev.type != MapNotify || xev.xmap.event != gXshm.window);

  XFlush(gDisplay);
  XSync(gDisplay, False);
  
  gXshm.gc = XCreateGC(gDisplay, gXshm.window, 0L, &xgcv);

  if (gXshm.bFullscreen)
    XSetInputFocus (gDisplay, gXshm.window, RevertToNone, CurrentTime);

  if (gXshm.bFullscreen) {
    if (!gXshm.mcursor[0]) {
      create_cursor_xshm (theCmap);
    }
    
    //display_cursor_xshm(HIDE_CURSOR);
  }
  /* drag and drop */

  XUNLOCK ();
  
  if(!gXshm.xdnd)
    gXshm.xdnd = (DND_struct_t *) malloc(sizeof(DND_struct_t));
  
  gui_init_dnd(gXshm.xdnd);
  gui_dnd_set_callback (gXshm.xdnd, gui_dndcallback);
  gui_make_window_dnd_aware (gXshm.xdnd, gXshm.window);
  
}

/* allocates ximages if necessary */
static int set_image_format_xshm (uint32_t width, uint32_t height, uint32_t ratio, int format) {

  /* FIXME: handle format argument */

  if ( (gXshm.image_width == width) 
       && (gXshm.image_height == height)
       && (gXshm.ratio == ratio) 
       && (gXshm.bFullscreen == gXshm.bIsFullscreen)
       && !gXshm.user_ratio_changed ) 
    return 0;

  gXshm.image_width  = width;
  gXshm.image_height = height;
  gXshm.hstride_rgb  = gXshm.dest_width * gXshm.bytes_per_pixel;
  gXshm.hstride_y    = gXshm.image_width;
  gXshm.hstride_uv   = gXshm.image_width / 2;
  gXshm.ratio        = ratio;
  gXshm.user_ratio_changed = 0;

  /*
   * calculate dest size from ratio
   */

  /*
   * Mpeg-2:
   */

  if (gXshm.user_ratio == ASPECT_AUTO) {
    switch (gXshm.ratio) {
    case 0: /* forbidden */
      fprintf (stderr, "invalid ratio\n");
      exit (1);
      break;
    case 1: /* square */
      gXshm.dest_width = width;
      gXshm.dest_height = height;
      gXshm.anamorphic  = 0;
      break;
    case 2: /* 4:3 */
      gXshm.dest_width = width ;
      gXshm.dest_height = height;
      gXshm.anamorphic  = 0;
      break;
    case 3: /* 16:9 */
      gXshm.dest_width = width * 5/4;
      gXshm.dest_height = height;
      gXshm.anamorphic  = 1;
      break;
    default:
      gXshm.dest_width = width;
      gXshm.dest_height = height;
      xprintf (VERBOSE|VIDEO, "invalid ratio\n");
      /*exit (1); */
      break;
    }
  } else if (gXshm.user_ratio == ASPECT_ANAMORPHIC) {
    gXshm.dest_width = width *5/4 ;
    gXshm.dest_height = height;
    gXshm.anamorphic  = 1;
  } else {
    gXshm.dest_width = width;
    gXshm.dest_height = height;
    gXshm.anamorphic  = 0;
  }

    
  xprintf (VERBOSE|VIDEO, "picture size : %d x %d (Ratio: %d)\n",
	   width, height, ratio);

  setup_window_xshm () ;

  return 1;
}

static void dispose_image_buffer_xshm (vo_image_buffer_t *vo_img) {

  xshm_image_buffer_t *img = (xshm_image_buffer_t *) vo_img;

  XLOCK ();

  if (gXshm.bUseShm) {

    XShmDetach(gDisplay, &img->mShminfo);
    XDestroyImage(img->mImage); 
    shmdt(img->mShminfo.shmaddr);
    shmctl (img->mShminfo.shmid, IPC_RMID,NULL);

  } else {

    XDestroyImage(img->mImage); 

  }

  free (img);

  XUNLOCK ();
}


static vo_image_buffer_t *alloc_image_buffer_xshm () {

  uint32_t               image_size;
  xshm_image_buffer_t   *img ;

  XLOCK ();

  img = (xshm_image_buffer_t *) malloc (sizeof (xshm_image_buffer_t));

  if (img==NULL) {
    printf ("out of memory while allocating image buffer\n");
    exit (1);
  }

  if (gXshm.bUseShm) {
    img->mImage = XShmCreateImage(gDisplay, 
				  gXshm.vinfo.visual,
				  gXshm.vinfo.depth,
				  ZPixmap, NULL,
				  &img->mShminfo,
				  gXshm.dest_width, 
				  gXshm.dest_height);
    
    if (img->mImage == NULL )  {
      fprintf(stderr, "Shared memory error when allocating image => exit (Ximage error)\n");
      exit (1);
    }
    
    gXshm.bpp = img->mImage->bits_per_pixel;
    gXshm.bytes_per_pixel = gXshm.bpp / 8;
    gXshm.hstride_rgb  = gXshm.dest_width * gXshm.bytes_per_pixel;
    img->mShminfo.shmid=shmget(IPC_PRIVATE, 
			       img->mImage->bytes_per_line * img->mImage->height, 
			       IPC_CREAT | 0777);
    
    if (img->mShminfo.shmid < 0 ) {
      printf("%s: allocating image \n",strerror(errno));
      exit (1);
    }
  
    img->mShminfo.shmaddr  = (char *) shmat(img->mShminfo.shmid, 0, 0);
  
    if (img->mShminfo.shmaddr == ((char *) -1)) {
      fprintf(stderr, "Shared memory error (address error) when allocating image \n");
      exit (1);
    }
    
    img->mShminfo.readOnly = False;
    img->mImage->data = img->mShminfo.shmaddr;
    
    XShmAttach(gDisplay, &img->mShminfo);
    
    XSync(gDisplay, False);
    /* shmctl(img->mShminfo.shmid, IPC_RMID, 0);*/
  } else { /* no shm */

    /*
    XResizeWindow (gDisplay, gXshm.window, gXshm.dest_width, 
		   gXshm.dest_height);

    XSync(gDisplay, False);
    */
    img->mImage = XGetImage (gDisplay, gXshm.window, 0, 0,
			     gXshm.dest_width, gXshm.dest_height,
			     AllPlanes, ZPixmap);
    XSync(gDisplay, False);
  }

  
  image_size = gXshm.image_width * gXshm.image_height;
  img->mImageBuffer.mem[0] = malloc(image_size);
  img->mImageBuffer.mem[1] = malloc(image_size/4);
  img->mImageBuffer.mem[2] = malloc(image_size/4);
  
  if (!gXshm.bYuvInitialized) {
    int mode = ((img->mImage->blue_mask & 0x01)) ? MODE_RGB : MODE_BGR;
    yuv2rgb_init((gXshm.depth == 24) ? gXshm.bpp : gXshm.depth, mode);
    gXshm.bYuvInitialized = 1;
  }

  XUNLOCK ();

  pthread_mutex_init (&img->mImageBuffer.mutex, NULL);

  return (vo_image_buffer_t *) img;
}

static void process_macroblock_xshm (vo_image_buffer_t *img, 
				     uint8_t *py, uint8_t *pu, uint8_t *pv,
				     int slice_num,
				     int subslice_num) {

  uint8_t *dst; 
  int subslice_offset; 
  xshm_image_buffer_t *xshm_img = (xshm_image_buffer_t *) img;
  int slice_offset;

  slice_offset    = slice_num * 16 * gXshm.dest_width ;
  if (gXshm.anamorphic)
    subslice_offset = subslice_num *20 ; 
  else
    subslice_offset = subslice_num *16 ; 

  dst = xshm_img->mImage->data + (slice_offset + subslice_offset) * gXshm.bytes_per_pixel ; 

  if (gXshm.anamorphic)
    yuv2rgb_anamorphic(dst, 
		       py, pu, pv,
		       gXshm.hstride_rgb, 
		       gXshm.hstride_y, 
		       gXshm.hstride_uv);
  else
    yuv2rgb(dst, 
	    py, pu, pv,
	    gXshm.hstride_rgb, 
	    gXshm.hstride_y, 
	    gXshm.hstride_uv);
}

static int is_fullscreen_xshm () {
  return gXshm.bFullscreen;
}

static void set_fullscreen_xshm (int bFullscreen) {

  xprintf(VERBOSE|VIDEO, "(!)Fullscreen not implemented for xshm.\n");

  return ; /* FIXME - not implemented for xshm */

  gXshm.bFullscreen = bFullscreen;
  
  set_image_format_xshm (gXshm.image_width, gXshm.image_height, gXshm.ratio, IMGFMT_YV12);
}

static void display_frame_xshm (vo_image_buffer_t *vo_img) {

  xshm_image_buffer_t *img = (xshm_image_buffer_t *) vo_img;

  XLOCK ();

  if (gXshm.bUseShm) {

    XShmPutImage(gDisplay, gXshm.window, gXshm.gc, img->mImage, 
		 0, 0, 0, 0, 
		 img->mImage->width, img->mImage->height, True); 
  } else {

    xprintf (VERBOSE|VIDEO, "display frame\n");

    XPutImage(gDisplay, gXshm.window, gXshm.gc, img->mImage, 
	      0, 0, 0, 0, 
	      img->mImage->width, img->mImage->height); 
  }

  XFlush(gDisplay);

  if (gXshm.cur_image) {
    vo_image_drawn (  (vo_image_buffer_t *) gXshm.cur_image);
  }

  gXshm.cur_image = img;

  XUNLOCK ();
}

static void draw_logo_xshm () {
  ImlibImage *resized_image;
  int xwin, ywin, tmp;
  unsigned int wwin, hwin, bwin, dwin;
  double ratio = 1;
  Window rootwin;

  XLOCK ();;

  XClearWindow (gDisplay, gXshm.window); 

  if(XGetGeometry(gDisplay, gXshm.window, &rootwin, 
		  &xwin, &ywin, &wwin, &hwin, &bwin, &dwin) != BadDrawable) {

    tmp = (wwin / 100) * 86;
    ratio = (tmp < gXineLogoWidth && tmp >= 1) ? 
      (ratio = (double)tmp / (double)gXineLogoWidth) : 1;
  }
  
  resized_image = Imlib_clone_image(gImlib_data, gXineLogoImg);
  Imlib_render (gImlib_data, resized_image, 
		(int)gXineLogoWidth * ratio, 
		(int)gXineLogoHeight * ratio);

  XCopyArea (gDisplay, resized_image->pixmap, gXshm.window, gXshm.gc, 0, 0,
	     resized_image->width, resized_image->height, 
	     (wwin - resized_image->width) / 2, 
	     (hwin - resized_image->height) / 2);

  XFlush(gDisplay);

  Imlib_destroy_image(gImlib_data, resized_image);

  XUNLOCK ();;
}

static void handle_event_xshm (XEvent *event) {

  switch (event->type) {
  case Expose:
    if (event->xexpose.window == gXshm.window) {
      if (gXshm.bLogoMode) {
	draw_logo_xshm ();
      } else {
	XLOCK ();
	XClearWindow (gDisplay, gXshm.window); 
	if (gXshm.cur_image)
	  XShmPutImage(gDisplay, gXshm.window, gXshm.gc, 
		       gXshm.cur_image->mImage, 
		       0, 0, 0, 0, 
		       gXshm.cur_image->mImage->width, 
		       gXshm.cur_image->mImage->height, True); 
	XUNLOCK ();
      } 
    }
    break;
    
  case ClientMessage:
    if(event->xany.window == gXshm.window)
      gui_dnd_process_client_message (gXshm.xdnd, event);
    break;
  }
}

static void set_logo_mode_xshm (int bLogoMode) {
  gXshm.bLogoMode = bLogoMode;

  if (bLogoMode)
    draw_logo_xshm ();
}

static void reset_xshm () {

  if (gXshm.cur_image) {
    vo_image_drawn ( (vo_image_buffer_t *) gXshm.cur_image);
    gXshm.cur_image = NULL;
  }

}

void set_aspect_xshm (int ratio) {

  ratio %= 3; /* DVB unsupported */

  if (ratio != gXshm.user_ratio) {
    gXshm.user_ratio         = ratio;
    gXshm.user_ratio_changed = 1;

    set_image_format_xshm (gXshm.image_width, gXshm.image_height, gXshm.ratio, IMGFMT_YV12);
  }

  printf ("set_aspect done\n");
}

int get_aspect_xshm () {
  return gXshm.user_ratio;
}

void exit_xshm () {
}

static int get_noop(void) {
  return 0;
}

static int set_noop(int v) {
  return v;
}

static vo_driver_t vo_xshm = {
  get_capabilities_xshm,
  set_image_format_xshm,
  alloc_image_buffer_xshm,
  dispose_image_buffer_xshm,
  process_macroblock_xshm,
  display_frame_xshm,
  set_fullscreen_xshm,
  is_fullscreen_xshm,
  handle_event_xshm,
  set_logo_mode_xshm,
  reset_xshm,
  display_cursor_xshm,
  set_aspect_xshm,
  get_aspect_xshm,
  exit_xshm,

  get_noop,
  get_noop,
  set_noop,
  get_noop,

  get_noop,
  get_noop,
  set_noop,
  get_noop,

  get_noop,
  get_noop,
  set_noop,
  get_noop,

  get_noop,
  get_noop,
  set_noop,
  get_noop,

  get_noop,
  get_noop,
  set_noop,

  NULL,
  NULL
};

/* 
 * connect to server, create and map window,
 * allocate colors and (shared) memory
 */

vo_driver_t *init_video_out_xshm () {

  int                  completionType = -1;
  XWindowAttributes    attribs;
  int                  minor, major;
  Bool                 bPixmaps;
  XImage              *myimage;
  XShmSegmentInfo      myshminfo;

  /* XLOCK (); */

  gXshm.image_width=720;
  gXshm.image_height=576;

  /*
   * ok, X11, prepare for some video!
   */

  gXshm.screen = DefaultScreen(gDisplay);

  XGetWindowAttributes(gDisplay, DefaultRootWindow(gDisplay), &attribs);

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
  
  gXshm.depth = attribs.depth;
  
  if (gXshm.depth != 15 && gXshm.depth != 16 && gXshm.depth != 24 && gXshm.depth != 32)  {
    /* The root window may be 8bit but there might still be
     * visuals with other bit depths. For example this is the 
     * case on Sun/Solaris machines.
     */
    gXshm.depth = 24;
  }

  XMatchVisualInfo(gDisplay, gXshm.screen, gXshm.depth, TrueColor, &gXshm.vinfo);

  gXshm.bUseShm = 1;

  /*
   * check for X shared memory support
   */

  if (!XShmQueryExtension(gDisplay)) {
    xprintf(VERBOSE|VIDEO, "Shared memory not supported\n\n");
    gXshm.bUseShm = 0;
    goto finishShmTesting;
  }

  if (!XShmQueryVersion (gDisplay, &major, &minor, &bPixmaps)){
    xprintf(VERBOSE|VIDEO, "Shared memory not supported\n\n");
    gXshm.bUseShm = 0;
    goto finishShmTesting;
  }

  xprintf (VERBOSE|VIDEO, "pixmaps : %d ?= %d\n", bPixmaps, True);

  if (!bPixmaps) {
    xprintf(VERBOSE|VIDEO, "creation of shared pixmaps not possible\n\n");
    gXshm.bUseShm = 0;
    goto finishShmTesting;
  }


  completionType = XShmGetEventBase(gDisplay) + ShmCompletion;

  /* try to create shared image */
  gXshm.bFail = 0;
  x11_InstallXErrorHandler ();

  myimage = XShmCreateImage(gDisplay, 
			    gXshm.vinfo.visual,
			    gXshm.vinfo.depth,
			    ZPixmap, NULL,
			    &myshminfo,
			    100, 
			    100);

  if (myimage == NULL )  {
    xprintf(VERBOSE|VIDEO, "Shared memory error when allocating image => exit (Ximage error)\n");
    gXshm.bUseShm = 0;
    x11_DeInstallXErrorHandler();
    goto finishShmTesting;
  }
  
  gXshm.bpp = myimage->bits_per_pixel;
  gXshm.bytes_per_pixel = gXshm.bpp / 8;

  myshminfo.shmid=shmget(IPC_PRIVATE, 
			 myimage->bytes_per_line * myimage->height, 
			 IPC_CREAT | 0777);
  
  if (myshminfo.shmid < 0 ) {
    xprintf(VERBOSE|VIDEO, "%s: allocating image \n",strerror(errno));
    gXshm.bUseShm = 0;
    x11_DeInstallXErrorHandler();
    goto finishShmTesting;
  }
  
  myshminfo.shmaddr  = (char *) shmat(myshminfo.shmid, 0, 0);
  
  if (myshminfo.shmaddr == ((char *) -1)) {
    xprintf(VERBOSE|VIDEO, "Shared memory error (address error) when allocating image \n");
    gXshm.bUseShm = 0;
    x11_DeInstallXErrorHandler();
    goto finishShmTesting;
  }
  
  myshminfo.readOnly = False;
  myimage->data = myshminfo.shmaddr;
  
  XShmAttach(gDisplay, &myshminfo);
  
  XSync(gDisplay, False);

  if (gXshm.bFail) {
    xprintf (VERBOSE|VIDEO, "Couldn't create shared memory image\n");
    gXshm.bUseShm = 0;
    x11_DeInstallXErrorHandler();
    goto finishShmTesting;
  }

  XShmDetach (gDisplay, &myshminfo);
  XDestroyImage (myimage);
  shmdt (myshminfo.shmaddr);
  shmctl (myshminfo.shmid, IPC_RMID, 0);

  x11_DeInstallXErrorHandler();

 finishShmTesting:

  if (!gXshm.bUseShm) {
    printf ("\n\n!!! failed to initialize X shared memory extension. Fall back to plain X11 functions. This is very slow and mostly untested. Are you sure you're using a local graphics device for output? Remote playing via network is not supported by xine!!!\n\n");
  }


  /*
   * init global variables
   */

  gXshm.bFullscreen           = 0;
  gXshm.bIsFullscreen         = 0;
  gXshm.ratio                 = 0;
  gXshm.user_ratio            = ASPECT_AUTO;
  gXshm.user_ratio_changed    = 0 ;
  gXshm.bLogoMode             = 1;
  gXshm.bYuvInitialized       = 0;
  gXshm.cur_image             = NULL;

  set_image_format_xshm (720,576,1,IMGFMT_YV12);

  create_cursor_xshm(DefaultColormap(gDisplay, 0));
  gXshm.current_cursor        = SHOW_CURSOR;

  /* 
   * If gVo.depth is 24 then it may either be a 3 or 4 byte per pixel
   * format. We can't use bpp because then we would lose the 
   * distinction between 15/16bit gVo.depth (2 byte formate assumed).
   *
   * FIXME - change yuv2rgb_init to take both gVo.depth and bpp
   * parameters
   */

  if (gXshm.depth>16) 
    printf ("\n\nWARNING: current display depth is %d. For better performance\na depth of 16 bpp is recommended!\n\n",
	    gXshm.depth);

  /*  yuv2rgb_init((gXshm.depth == 24) ? gXshm.bpp : gXshm.depth,MODE_RGB); */

  /* XUNLOCK (); */

  return &vo_xshm;
}

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  xshm_driver_t        *this;
  Display              *display = NULL;
  unsigned int          adaptor_num, adaptors, i, j, formats;
  unsigned int          ver,rel,req,ev,err;
  int                   nattr;
  x11_visual_t         *visual;
  XColor                dummy;

  visual = (x11_visual_t *) visual_gen;
  display = visual->display;
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  /*
   * check for XShm support 
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

  xshm_port = 0;
  adaptor_num = 0;

  while ( (adaptor_num < adaptors) && !xshm_port) {
    if (adaptor_info[adaptor_num].type & XvImageMask)
      for (j = 0; j < adaptor_info[adaptor_num].num_ports; j++)
	if (( !(xshm_check_yv12 (display, adaptor_info[adaptor_num].base_id + j))) 
	    && (XvGrabPort (display, adaptor_info[adaptor_num].base_id + j, 0) == Success)) {
	  xshm_port = adaptor_info[adaptor_num].base_id + j;
	  break; 
	}

    adaptor_num++;
  }


  if (!xshm_port) {
    printf ("video_out_xv: Xv extension is present but I couldn't find a usable yuv12 port.\n");
    printf ("              Looks like your graphics hardware driver doesn't support Xv?!\n");
    XvFreeAdaptorInfo (adaptor_info);
    return NULL;
  } else
    printf ("video_out_xv: using Xv port %d for hardware colorspace conversion and scaling.\n", xshm_port);

  /*
   * from this point on, nothing should go wrong anymore; so let's start initializing this driver
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
  this->output_xoffset    = 0;
  this->output_yoffset    = 0;
  this->output_width      = 0;
  this->output_height     = 0;
  this->drawable          = visual->d;
  this->gc                = XCreateGC (this->display, this->drawable, 0, NULL);
  this->xshm_port           = xshm_port;
  this->capabilities      = 0;

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

  attr = XvQueryPortAttributes(display, xshm_port, &nattr);
  if(attr && nattr) {
    int k;
    
    for(k = 0; k < nattr; k++) {
      
      if(attr[k].flags & XvSettable) {
	if(!strcmp(attr[k].name, "XSHM_HUE")) {
	  xshm_check_capability (this, VO_CAP_HUE, 
			       VO_PROP_HUE, attr[k],
			       adaptor_info[i].base_id, "XSHM_HUE");
	  printf("XSHM_HUE ");
	}
	else if(!strcmp(attr[k].name, "XSHM_SATURATION")) {
	  xshm_check_capability (this, VO_CAP_SATURATION, 
			       VO_PROP_SATURATION, attr[k],
			       adaptor_info[i].base_id, "XSHM_SATURATION");
	  printf("XSHM_SATURATION ");
	}
	else if(!strcmp(attr[k].name, "XSHM_BRIGHTNESS")) {
	  xshm_check_capability (this, VO_CAP_BRIGHTNESS,
			       VO_PROP_BRIGHTNESS, attr[k],
			       adaptor_info[i].base_id, "XSHM_BRIGHTNESS");
	  printf("XSHM_BRIGHTNESS ");
	}
	else if(!strcmp(attr[k].name, "XSHM_CONTRAST")) {
	  xshm_check_capability (this, VO_CAP_CONTRAST, 
			       VO_PROP_CONTRAST, attr[k],
			       adaptor_info[i].base_id, "XSHM_CONTRAST");
	  printf("XSHM_CONTRAST ");
	}
	else if(!strcmp(attr[k].name, "XSHM_COLORKEY")) {
	  xshm_check_capability (this, VO_CAP_COLORKEY, 
			       VO_PROP_COLORKEY, attr[k],
			       adaptor_info[i].base_id, "XSHM_COLORKEY");
	  printf("XSHM_COLORKEY ");
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

  fo = XvListImageFormats(display, this->xshm_port, (int*)&formats);

  this->xshm_format_yv12 = 0;
  this->xshm_format_yuy2 = 0;
  this->xshm_format_rgb  = 0;
  
  for(i = 0; i < formats; i++) {
    xprintf(VERBOSE|VIDEO, "video_out_xv: Xv image format: 0x%x (%4.4s) %s\n", 
	    fo[i].id, (char*)&fo[i].id, 
	    (fo[i].format == XvPacked) ? "packed" : "planar");      
    if (fo[i].id == IMGFMT_YV12)  {
      this->xshm_format_yv12 = fo[i].id;
      this->capabilities |= VO_CAP_YV12;
      printf ("video_out_xv: this adaptor supports the yv12 format.\n");
    } else if (fo[i].id == IMGFMT_YUY2) {
      this->xshm_format_yuy2 = fo[i].id;
      this->capabilities |= VO_CAP_YUY2;
      printf ("video_out_xv: this adaptor supports the yuy2 format.\n");
    } else if (fo[i].id == IMGFMT_RGB) {
      this->xshm_format_rgb = fo[i].id;
      this->capabilities |= VO_CAP_RGB;
      printf ("video_out_xv: this adaptor supports the rgb format.\n");
    }
  }

  return &this->vo_driver;
}



static vo_info_t vo_info_xv = {
  VIDEO_OUT_IFACE_VERSION,
  "XShm",
  "xine video output plugin using the MIT X shared memory extension",
  VISUAL_TYPE_X11,
  5
};

vo_info_t *get_video_out_plugin_info() {
  return &vo_info_xv;
}

