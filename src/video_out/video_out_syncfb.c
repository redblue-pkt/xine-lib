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
 * $Id: video_out_syncfb.c,v 1.14 2001/10/10 10:06:52 jkeil Exp $
 * 
 * video_out_syncfb.c, Matrox G400 video extension interface for xine
 *
 * based on video_out_mga code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * xine-specific code by Joachim Koenig <joachim.koenig@gmx.net>
 * Underlaying window by Matthias Dahl  <matthew2k@web.de>
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#if defined(__linux__)
#include <linux/config.h> /* Check for DEVFS */
#endif

#if defined(__sun)
#include <sys/ioccom.h>
#endif

#include "video_out.h"
#include "video_out_x11.h"
#include "video_out_syncfb.h"
#include "xine_internal.h"
#include "alphablend.h"

#include "monitor.h"
#include "configfile.h"

#define MWM_HINTS_DECORATIONS   (1L << 1)
#define PROP_MWM_HINTS_ELEMENTS 5
typedef struct _mwmhints {
  uint32_t flags;
  uint32_t functions;
  uint32_t decorations;
  int32_t  input_mode;
  uint32_t status;
} MWMHints;

// extern Window   gVideoWin;
// extern Pixmap   gXineLogo;
// extern int      gXineLogoWidth, gXineLogoHeight;

uint32_t xine_debug;

typedef struct mga_frame_s {
  vo_frame_t         vo_frame;
  int                width, height, ratio_code, format;
  int  	             id;

} mga_frame_t;


typedef struct _display {
  int    width;
  int    height;
  int    depth;
  int    default_screen;
} display;

typedef struct _window {
  char        title[20];
   
  Window      clasped_window;
  int         visibility;
} window;

typedef struct _mga_globals {

  syncfb_config_t          mga_vid_config;
  syncfb_capability_t      caps;
  syncfb_buffer_info_t     bufinfo;
  syncfb_param_t           param;
  Display 	  *lDisplay;
  Display 	  *gDisplay;
  uint8_t         *vid_data, *frame0, *frame1;
  int             next_frame;
  int             fd;
  uint32_t 	  bespitch;

  int             bFullscreen;
  int             bIsFullscreen;
  int             image_width;
  int             image_height;
  int             image_xoff;
  int             image_yoff;
  int             orig_width; /* image size correct. by ratio */
  int             orig_height;
  int             dest_width;
  int             dest_height;
  int             fourcc_format;
  int             interlaced;

  uint32_t        ratio;
  int             user_ratio, user_ratio_changed;

//  XvImage        *cur_image;

  int             bLogoMode;

  int             bright_min, bright_current, bright_max;
  int             cont_min, cont_current, cont_max;

  int             overlay_state;

  /* gui callback */
  void (*request_dest_size) (int video_width, int video_height,
                             int *dest_x, int *dest_y,
                             int *dest_height, int *dest_width);


} mga_globals;

mga_globals _mga_priv;
display     _display;
window      _window;

int nLocks;



static int _mga_write_frame_g400 (uint8_t *src[])
{
        uint8_t *dest;
        int h;
        uint8_t *y = src[0];
        uint8_t *cb = src[1];
        uint8_t *cr = src[2];

        dest = _mga_priv.vid_data;

        if (_mga_priv.fourcc_format == IMGFMT_YUY2) {
          for (h=0; h < _mga_priv.mga_vid_config.src_height; h++) {
                memcpy(dest, y, _mga_priv.mga_vid_config.src_width*2);
                y += _mga_priv.mga_vid_config.src_width*2;
                dest += _mga_priv.bespitch*2;
          }
          return 0;
        }


        for (h=0; h < _mga_priv.mga_vid_config.src_height; h++) {
                memcpy(dest, y, _mga_priv.mga_vid_config.src_width);
                y += _mga_priv.mga_vid_config.src_width;
                dest += _mga_priv.bespitch;
        }


        for (h=0; h < _mga_priv.mga_vid_config.src_height/2; h++) {
                memcpy(dest, cb, _mga_priv.mga_vid_config.src_width/2);
                cb += _mga_priv.mga_vid_config.src_width/2;
                dest += _mga_priv.bespitch/2;
        }

        dest = _mga_priv.vid_data + _mga_priv.bespitch * _mga_priv.mga_vid_config.src_height * 3 / 2;

        for (h=0; h < _mga_priv.mga_vid_config.src_height/2; h++) {
                memcpy(dest, cr, _mga_priv.mga_vid_config.src_width/2);
                cr += _mga_priv.mga_vid_config.src_width/2;
                dest += _mga_priv.bespitch/2;
        }

        return 0;
}




static void setup_window_mga () {
  int                   ww=0, wh=0;
  float                 aspect;

  XWindowAttributes     wattr;

  XGetWindowAttributes(_mga_priv.lDisplay, DefaultRootWindow(_mga_priv.lDisplay), &wattr);
   
  _display.width  = wattr.width;
  _display.height = wattr.height;
  _display.depth  = wattr.depth;
   
  ww = _display.width ;
  wh = _display.height;

xprintf(VERBOSE|VIDEO,"setup_window_mga: unscaled size should be %d x %d \n",_mga_priv.orig_width,_mga_priv.orig_height);
printf("setup_window_mga: unscaled size should be %d x %d \n",_mga_priv.orig_width,_mga_priv.orig_height);
   
  if (_mga_priv.bFullscreen) {

    /*
     * zoom to fullscreen
     */

    if (_mga_priv.orig_width != ww) {
      aspect = (float) _mga_priv.orig_width / (float) _mga_priv.orig_height ;

      _mga_priv.dest_width = ww;
      _mga_priv.dest_height = ww / aspect;

      if (_mga_priv.dest_height > wh) {

	_mga_priv.dest_width = wh * aspect;
	_mga_priv.dest_height = wh;
      }

    } else  {

      _mga_priv.dest_width   = _mga_priv.orig_width  ; 
      _mga_priv.dest_height  = _mga_priv.orig_height ;

    } 
    _mga_priv.image_xoff = ( ww - _mga_priv.dest_width) / 2;
    _mga_priv.image_yoff = ( wh - _mga_priv.dest_height) / 2;


    _mga_priv.bIsFullscreen = 1;


  } else {

    /*
     * zoom to mpeg1 to double size
     */
    
//    if (_mga_priv.orig_width < 600) {
//      _mga_priv.dest_width   = _mga_priv.orig_width  *2;
//      _mga_priv.dest_height  = _mga_priv.orig_height *2;
//    } else {
      
     if (_mga_priv.orig_width > ww) {
        aspect = (float) _mga_priv.orig_width / (float) _mga_priv.orig_height ;

        _mga_priv.dest_width = ww;
        _mga_priv.dest_height = ww / aspect;

        if (_mga_priv.dest_height > wh) {
          _mga_priv.dest_width = wh * aspect;
          _mga_priv.dest_height = wh;
        }

      } else {
        _mga_priv.dest_width   = _mga_priv.orig_width  ;
        _mga_priv.dest_height  = _mga_priv.orig_height ;
      }
//    }

    _mga_priv.bIsFullscreen = 0;

    _mga_priv.image_xoff = ( ww - _mga_priv.dest_width) / 2;
    _mga_priv.image_yoff = ( wh - _mga_priv.dest_height) / 2;

  }
  xprintf(VERBOSE|VIDEO,"Calculated size should be %d x %d xoff %d yoff %d Display Is %d x %d\n",_mga_priv.dest_width,_mga_priv.dest_height,_mga_priv.image_xoff,_mga_priv.image_yoff,ww,wh);
  printf("Calculated size should be %d x %d xoff %d yoff %d Display Is %d x %d\n",_mga_priv.dest_width,_mga_priv.dest_height,_mga_priv.image_xoff,_mga_priv.image_yoff,ww,wh);
 
  if (ioctl(_mga_priv.fd,SYNCFB_GET_CAPS,&_mga_priv.caps)) perror("Error in config ioctl");
  xprintf(VERBOSE|VIDEO,"Syncfb device name is '%s'\n", _mga_priv.caps.name);
  xprintf(VERBOSE|VIDEO,"Memory size is %ld \n", _mga_priv.caps.memory_size);

  if (ioctl(_mga_priv.fd,SYNCFB_GET_CONFIG,&_mga_priv.mga_vid_config))
     printf("Error in get_config ioctl\n");

   _mga_priv.mga_vid_config.fb_screen_size = _display.width * _display.height * (_display.depth/8);  // maybe wrong if depth = 15 (?)
   _mga_priv.mga_vid_config.src_width = _mga_priv.image_width;
   _mga_priv.mga_vid_config.src_height= _mga_priv.image_height;
   _mga_priv.bespitch = (_mga_priv.image_width + 31) & ~31;

   switch  (_mga_priv.fourcc_format) {
   case IMGFMT_YV12: 
     _mga_priv.mga_vid_config.src_palette = VIDEO_PALETTE_YUV420P3;
     break;
   case IMGFMT_YUY2:
     _mga_priv.mga_vid_config.src_palette = VIDEO_PALETTE_YUYV;
     break;
   default:
     _mga_priv.mga_vid_config.src_palette = VIDEO_PALETTE_YUV420P3;
     break;
   }
   _mga_priv.mga_vid_config.image_width = _mga_priv.dest_width;
   _mga_priv.mga_vid_config.image_height= _mga_priv.dest_height;
//   _mga_priv.mga_vid_config.syncfb_mode = SYNCFB_FEATURE_BLOCK_REQUEST | SYNCFB_FEATURE_SCALE_H | SYNCFB_FEATURE_SCALE_V | SYNCFB_FEATURE_CROP ; /*   | SYNCFB_FEATURE_DEINTERLACE; */
   _mga_priv.mga_vid_config.syncfb_mode = SYNCFB_FEATURE_SCALE_H | SYNCFB_FEATURE_SCALE_V | SYNCFB_FEATURE_CROP ; /*   | SYNCFB_FEATURE_DEINTERLACE; */
   if (_mga_priv.interlaced)
     _mga_priv.mga_vid_config.syncfb_mode |= SYNCFB_FEATURE_DEINTERLACE;
   _mga_priv.mga_vid_config.image_xorg= _mga_priv.image_xoff;
   _mga_priv.mga_vid_config.image_yorg= _mga_priv.image_yoff;

   _mga_priv.mga_vid_config.src_crop_top = 0;
   _mga_priv.mga_vid_config.src_crop_bot = 0;

#ifdef CINEMODE
   _mga_priv.mga_vid_config.default_repeat = 3;
#else
   _mga_priv.mga_vid_config.default_repeat = 2;
#endif

   if (ioctl(_mga_priv.fd,SYNCFB_SET_CONFIG,&_mga_priv.mga_vid_config))
      xprintf(VERBOSE|VIDEO,"Error in set_config ioctl\n");
   if (_mga_priv.bLogoMode) {
     if (ioctl(_mga_priv.fd,SYNCFB_OFF)) {
       xprintf(VERBOSE|VIDEO,"Error in OFF ioctl\n");
     }
     else
       _mga_priv.overlay_state = 0;
   }

//   if (ioctl(_mga_priv.fd,SYNCFB_ON))
//      xprintf(VERBOSE|VIDEO,"Error in ON ioctl\n");
#if 0
   // create a simple window without anything. Just make overlay clickable. :)
   if (!_window.clasped_window) {
     Atom       prop;
     MWMHints	mwmhints;
   
     _window.clasped_window = XCreateSimpleWindow(_mga_priv.lDisplay, RootWindow(_mga_priv.lDisplay, _display.default_screen), 0, 0, _mga_priv.dest_width, _mga_priv.dest_height, 0, 0, 0);
//     gVideoWin = _window.clasped_window;

     // turn off all borders etc. (taken from the Xv plugin)
     prop = XInternAtom(_mga_priv.lDisplay, "_MOTIF_WM_HINTS", False);
     mwmhints.flags = MWM_HINTS_DECORATIONS;
     mwmhints.decorations = 0;
//     XChangeProperty(_mga_priv.lDisplay, gVideoWin, prop, prop, 32,
//		     PropModeReplace, (unsigned char *) &mwmhints,
//		     PROP_MWM_HINTS_ELEMENTS);
//     XSetTransientForHint(_mga_priv.lDisplay, gVideoWin, None);
      
     XSelectInput(_mga_priv.gDisplay, _window.clasped_window, VisibilityChangeMask | KeyPressMask | ButtonPressMask | SubstructureNotifyMask | StructureNotifyMask);
     XMapRaised(_mga_priv.lDisplay, _window.clasped_window);
     XSync(_mga_priv.lDisplay,0);
   }   

   XSetStandardProperties(_mga_priv.lDisplay, _window.clasped_window, _window.title, _window.title, None, NULL, 0, NULL);
   XMoveResizeWindow(_mga_priv.lDisplay, _window.clasped_window, (_mga_priv.bIsFullscreen) ? 0 : _mga_priv.image_xoff, (_mga_priv.bIsFullscreen) ? 0 : _mga_priv.image_yoff, (_mga_priv.bIsFullscreen) ? _display.width : _mga_priv.dest_width, (_mga_priv.bIsFullscreen) ? _display.height : _mga_priv.dest_height);
   XMapRaised(_mga_priv.lDisplay, _window.clasped_window);
   XSync(_mga_priv.lDisplay,0);
#endif
}



/* setup internal variables and (re-)init window if necessary */
static void mga_set_image_format (uint32_t width, uint32_t height, int ratio, int format) {


  double res_h, res_v, display_ratio, aspect_ratio;
  if ( (_mga_priv.image_width == width) 
       && (_mga_priv.image_height == height)
       && (_mga_priv.ratio == ratio) 
       && (_mga_priv.fourcc_format == format)
       && !_mga_priv.user_ratio_changed ) {
    return ;
  }

printf("new frame format width %d height %d ratio %d format %x\n",width,height,ratio,format);
  _mga_priv.image_width        = width;
  _mga_priv.image_height       = height;
  _mga_priv.ratio              = ratio;
  _mga_priv.fourcc_format      = format;
  _mga_priv.user_ratio_changed = 0;


  /*
   * Mpeg-2:
   */

  res_h = 1; // _display.width; 
  res_v = 1; // _display.height; 

  display_ratio = res_h / res_v;

  xprintf (VERBOSE | VIDEO, "display_ratio : %f\n",display_ratio);

  if (_mga_priv.user_ratio == ASPECT_AUTO) {
    switch (_mga_priv.ratio) {
    case 0: /* forbidden */
      fprintf (stderr, "invalid ratio\n");
      exit (1);
      break;
    case 2:
      aspect_ratio = 4.0 / 3.0;
      break;
    case 3:
      aspect_ratio = 16.0 / 9.0;
      break;
    case 4:
      aspect_ratio = 2.11/1.0;
      break;
    case 42: /* some stupid stream => don't touch aspect ratio */
    default:
      xprintf (VIDEO, "unknown aspect ratio (%d) in stream. untouched.\n", _mga_priv.ratio);
    case 1: /* "square" source pels */
      aspect_ratio = (double)_mga_priv.image_width / (double)_mga_priv.image_height;
      break;
    }
  } else if (_mga_priv.user_ratio == ASPECT_ANAMORPHIC) {
    aspect_ratio = 16.0 / 9.0;
  } else if (_mga_priv.user_ratio == ASPECT_DVB) {
    aspect_ratio = 2.0 / 1.0;
  } else if (_mga_priv.user_ratio == ASPECT_SQUARE) {
    aspect_ratio = (double)_mga_priv.image_width / (double)_mga_priv.image_height;
  } else {
    aspect_ratio =  4.0 / 3.0;
  }
  aspect_ratio *= display_ratio;
  if (_mga_priv.image_height * aspect_ratio >= _mga_priv.image_width) {
    _mga_priv.orig_width = rint((double)((double)_mga_priv.image_height * aspect_ratio));
    _mga_priv.orig_height = _mga_priv.image_height;
  }
  else {
    _mga_priv.orig_width = _mga_priv.image_width;
    _mga_priv.orig_height = rint((double)((double)_mga_priv.image_width / aspect_ratio));
  }

 
  xprintf (VERBOSE|VIDEO, "picture size : %d x %d (Ratio: %d)\n",
	   width, height, ratio);

  setup_window_mga () ;
printf("behind setup window mga\n");
  return ;
}

static void mga_update_frame_format (vo_driver_t *this_gen, vo_frame_t *frame_gen,
                                    uint32_t width, uint32_t height, int ratio_code,
                                    int format, int flags) {

  mga_frame_t   *frame = (mga_frame_t *) frame_gen;

// printf("MGA update frame format width %d height %d format %d\n",width,height,format);
  
  frame->ratio_code = ratio_code;
  if (frame->width == width && frame->height == height && frame->format == format)
    return;


  if (frame->vo_frame.base[0]) {
    shmdt(frame->vo_frame.base[0]);
    shmctl(frame->id,IPC_RMID,NULL);
    frame->vo_frame.base[0] = NULL;
  }

  frame->width = width;
  frame->height = height;
  frame->format = format;

 // we only know how to do 4:2:0 planar yuv right now.
 // we prepare for YUY2 sizes
  frame->id = shmget(IPC_PRIVATE,
               frame->width * frame->height * 2,
               IPC_CREAT | 0777);

  if (frame->id < 0 ) {
      perror("syncfb: shared memory error in shmget: ");
      exit (1);
  }

  frame->vo_frame.base[0] = shmat(frame->id, 0, 0);

  if (frame->vo_frame.base[0] == NULL) {
      fprintf(stderr, "syncfb: shared memory error (address error NULL)\n");
      exit (1);
  }

  if (frame->vo_frame.base[0] == (void *) -1) {
      fprintf(stderr, "syncfb: shared memory error (address error)\n");
      exit (1);
  }
  shmctl(frame->id, IPC_RMID, 0);

  frame->vo_frame.base[1] = frame->vo_frame.base[0] + width * height * 5 / 4;
  frame->vo_frame.base[2] = frame->vo_frame.base[0] + width * height;

  return;
}

static void mga_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed for MGA */
}

static void mga_frame_dispose (vo_frame_t *vo_img) {

  mga_frame_t  *frame = (mga_frame_t *) vo_img ;

  if (frame->vo_frame.base[0]) {
    shmdt(frame->vo_frame.base[0]);
    shmctl(frame->id,IPC_RMID,NULL);
    frame->vo_frame.base[0] = NULL;
  }

  free (frame);
}


static vo_frame_t *mga_alloc_frame (vo_driver_t *this_gen) {

  mga_frame_t     *frame ;

  frame = (mga_frame_t *) malloc (sizeof (mga_frame_t));
  memset (frame, 0, sizeof(mga_frame_t));

  if (frame==NULL) {
    printf ("mga_alloc_frame: out of memory\n");
  }

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);


  /*
   * supply required functions
   */

  frame->vo_frame.copy    = NULL;
  frame->vo_frame.field   = mga_frame_field;
  frame->vo_frame.dispose = mga_frame_dispose;


  return (vo_frame_t *) frame;
}

/*
 *
 */
static void mga_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {
#if 0
  xv_frame_t   *frame = (xv_frame_t *) frame_gen;

  /* Alpha Blend here
   * As XV drivers improve to support Hardware overlay, we will change this function.
   */

   if (overlay->data) {
        blend_yuv( frame->image->data, overlay, frame->width, frame->height);
   }
#endif
}

static void mga_display_frame(vo_driver_t *this, vo_frame_t *frame_gen) {

  mga_frame_t *frame = (mga_frame_t *) frame_gen;
  
 
  if (frame->width != _mga_priv.image_width ||
      frame->height != _mga_priv.image_height ||
      frame->format != _mga_priv.fourcc_format ||
      frame->ratio_code != _mga_priv.ratio) {
      mga_set_image_format(frame->width,frame->height,frame->ratio_code,frame->format);
  }


  // only write frame if overlay is active (otherwise syncfb hangs)
  if (_mga_priv.overlay_state == 1) {
    ioctl(_mga_priv.fd,SYNCFB_REQUEST_BUFFER,&_mga_priv.bufinfo);
    //printf("get buffer %d\n",_mga_priv.bufinfo.id);
    if ( _mga_priv.bufinfo.id == -1 ) {
      printf( "Got buffer #%d\n", _mga_priv.bufinfo.id );
      frame->vo_frame.displayed (&frame->vo_frame);
      return;
    }

    _mga_priv.vid_data = (uint_8 *)(_mga_priv.frame0 + _mga_priv.bufinfo.offset);

    _mga_write_frame_g400(&frame->vo_frame.base[0]);

    ioctl(_mga_priv.fd,SYNCFB_COMMIT_BUFFER,&_mga_priv.bufinfo);
  }
  /* Image is copied so release buffer */
  frame->vo_frame.displayed (&frame->vo_frame);
}

#if 0
void draw_logo_xv () {
  XClearWindow (gDisplay, gXv.window); 
    
  XCopyArea (gDisplay, gXineLogo, gXv.window, gXv.gc, 0, 0,
	     gXineLogoWidth, gXineLogoHeight, 
	     (gXv.dest_width - gXineLogoWidth)/2, 
	     (gXv.dest_height - gXineLogoHeight)/2);
	     
  XFlush(gDisplay);
}
#endif

void handle_event_mga (XEvent *event) {
  switch (event->type) {
  case VisibilityNotify:
    if (event->xany.window == _window.clasped_window) {
      if (event->xvisibility.state == VisibilityFullyObscured)
	{
	  _window.visibility = 0;
	   
	  if (_mga_priv.overlay_state == 1) {
	    if (ioctl(_mga_priv.fd,SYNCFB_OFF)) {
              xprintf(VERBOSE|VIDEO,"Error in OFF ioctl\n");
            }
            else
              _mga_priv.overlay_state = 0;
	  }
	}
      else
       {
	  _window.visibility = 1;
	  
	 if (_mga_priv.overlay_state == 0 && !_mga_priv.bLogoMode) {
           if (ioctl(_mga_priv.fd,SYNCFB_GET_CONFIG,&_mga_priv.mga_vid_config))
             printf("Error in get_config ioctl\n");
           if (ioctl(_mga_priv.fd,SYNCFB_SET_CONFIG,&_mga_priv.mga_vid_config))
             printf("Error in get_config ioctl\n");
           if (ioctl(_mga_priv.fd,SYNCFB_ON)) {
             xprintf(VERBOSE|VIDEO,"Error in ON ioctl\n");
           }
           else
             _mga_priv.overlay_state = 1;	  
	 }
       }
    }
    break;
  }
}

void set_logo_mode_mga (int bLogoMode) {
  if (_mga_priv.bLogoMode == bLogoMode)
    return;

  _mga_priv.bLogoMode = bLogoMode;

  if (bLogoMode) {
    if (ioctl(_mga_priv.fd,SYNCFB_OFF)) {
      xprintf(VERBOSE|VIDEO,"Error in OFF ioctl\n");
    }
    else
      _mga_priv.overlay_state = 0;
  }
  else {
     if (_window.visibility == 1) {
       if (ioctl(_mga_priv.fd,SYNCFB_GET_CONFIG,&_mga_priv.mga_vid_config))
         printf("Error in get_config ioctl\n");
       if (ioctl(_mga_priv.fd,SYNCFB_SET_CONFIG,&_mga_priv.mga_vid_config))
         printf("Error in get_config ioctl\n");
       if (ioctl(_mga_priv.fd,SYNCFB_ON)) {
         xprintf(VERBOSE|VIDEO,"Error in ON ioctl\n");
       }
       else {
        _mga_priv.overlay_state = 1;
       }
     }
  }  
 
#if 0
  XLOCK
  if (bLogoMode)
    draw_logo_xv ();
  else { /* FIXME : Bad hack to reinstall Xv overlay */
    XUnmapWindow(gDisplay, gXv.window);
    XMapRaised(gDisplay, gXv.window);

    /* Spark
     * move the window back to 0,0 in case the window manager moved it
     * do this only is the window is fullscreen or
     * we lose the top and left windowborders
     */
    if (gXv.bIsFullscreen) {
      XMoveWindow(gDisplay, gXv.window, 0, 0);
    }

  }
  XUNLOCK 
#endif
}


void mga_exit (vo_driver_t *this) {
printf("exit mga\n");
   if (ioctl(_mga_priv.fd,SYNCFB_ON)) {
      xprintf(VERBOSE|VIDEO,"Error in ON ioctl\n");
   }
   if (ioctl(_mga_priv.fd,SYNCFB_OFF)) {
      xprintf(VERBOSE|VIDEO,"Error in OFF ioctl\n");
   }
   close(_mga_priv.fd);
   _mga_priv.fd = -1;
}


static uint32_t mga_get_capabilities (vo_driver_t *this) {
  return VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_CONTRAST | VO_CAP_BRIGHTNESS;
}

/*
 * Properties
 */
static int mga_set_property(vo_driver_t *this, int property, int value) {
printf("set property %d value %d\n",property,value);
  switch (property) {
  case VO_PROP_CONTRAST:
        _mga_priv.cont_current=value;
	break;
  case VO_PROP_BRIGHTNESS:
        _mga_priv.bright_current=value;
	break;
  case VO_PROP_INTERLACED:
        if (value != _mga_priv.interlaced) {
	  if (value > 1)
            value = 0;
          _mga_priv.interlaced = value;
          _mga_priv.user_ratio_changed = 1;
          mga_set_image_format (_mga_priv.image_width, _mga_priv.image_height, _mga_priv.ratio, _mga_priv.fourcc_format);
        }
        return value;
  case VO_PROP_ASPECT_RATIO:
	if (value >= NUM_ASPECT_RATIOS)
	  value = ASPECT_AUTO;
        if (value != _mga_priv.user_ratio) {
          _mga_priv.user_ratio         = value;
          _mga_priv.user_ratio_changed = 1;
          mga_set_image_format (_mga_priv.image_width, _mga_priv.image_height, _mga_priv.ratio, _mga_priv.fourcc_format);
        }
        return value;
  default:
        return value;
  }

  _mga_priv.param.contrast = _mga_priv.cont_current;
  _mga_priv.param.brightness = _mga_priv.bright_current;

  if (ioctl(_mga_priv.fd,SYNCFB_SET_PARAMS,&_mga_priv.param) == 0) {
    return value;
  }
  return 0;
}


static void mga_get_property_min_max (vo_driver_t *this, int property, int *min, int *max) {

  switch (property) {
  case VO_PROP_CONTRAST:
	*min = _mga_priv.cont_min;
        *max = _mga_priv.cont_max;
	break;
  case VO_PROP_BRIGHTNESS:
	*min = _mga_priv.bright_min;
        *max = _mga_priv.bright_max;
	break;
  default:
	break;
  }
}  

static int mga_get_property (vo_driver_t *this, int property) {

  switch (property) {
  case VO_PROP_CONTRAST:
        return _mga_priv.cont_current;
  case VO_PROP_BRIGHTNESS:
        return _mga_priv.bright_current;
  case VO_PROP_ASPECT_RATIO:
        return _mga_priv.user_ratio;
  case VO_PROP_INTERLACED:
        return _mga_priv.interlaced;
  default:
        return 0;
  }
}



static int mga_gui_data_exchange (vo_driver_t *this, int data_type, void *data) {

//  xv_driver_t *this = (xv_driver_t *) this_gen;
  x11_rectangle_t *area;
printf("gui_data \n");

  switch (data_type) {
  case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:

    area = (x11_rectangle_t *) data;
printf("move to %d %d with %d %d\n",area->x,area->y,area->w,area->h);
//    xv_adapt_to_output_area (this, area->x, area->y, area->w, area->h);
      if (area->w == 1024)
         _mga_priv.bFullscreen = 1;
      else
         _mga_priv.bFullscreen = 0;
      if (_mga_priv.fourcc_format)
        setup_window_mga();


    break;
  case GUI_DATA_EX_COMPLETION_EVENT:

    /* FIXME : implement */

    break;

  /* FIXME: implement this
  case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO:
    {
      x11_rectangle_t *rect = data;
      int x1, y1, x2, y2;
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
  */

  default:
    return -1;
  }

  return 0;
}


static vo_driver_t vo_mga = {
  mga_get_capabilities,
  mga_alloc_frame,
  mga_update_frame_format,
  mga_display_frame,
  mga_overlay_blend,
  mga_get_property,
  mga_set_property,
  mga_get_property_min_max,
  mga_gui_data_exchange,
  mga_exit,
};


/* 
 * connect to server, create and map window,
 * allocate colors and (shared) memory
 */

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual) {

#ifdef CONFIG_DEVFS_FS
  char name[]= "/dev/fb/syncfb";
#else
  char name[]= "/dev/syncfb";
#endif


  if ((_mga_priv.fd = open ((char *) name, O_RDWR)) < 0) {
       xprintf(VERBOSE|VIDEO, "Can't open %s\n", (char *) name);
       return NULL;
  }

  if (ioctl(_mga_priv.fd,SYNCFB_GET_CAPS,&_mga_priv.caps)) {
     xprintf(VERBOSE|VIDEO,"Error in config ioctl");
     close(_mga_priv.fd);
     return NULL;
  }

  _mga_priv.vid_data = (char*)mmap(0,_mga_priv.caps.memory_size,PROT_WRITE,MAP_SHARED,_mga_priv.fd,0);

  //clear the buffer
  // memset(_mga_priv.vid_data,0,1024*768*2);


  _mga_priv.frame0   = _mga_priv.vid_data;



  /*
   * init global variables
   */

  strcpy(_window.title, "Xine syncfb overlay\0");
  _window.visibility              = 1;
   
  _mga_priv.lDisplay = XOpenDisplay(":0.0");  /* Xine may run on another Display but syncfb always goes to :0.0 */
//  lDisplay = gDisplay;

  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  _mga_priv.gDisplay              = (Display *) visual;
  _mga_priv.bFullscreen           = 0;
  _mga_priv.bIsFullscreen         = 0;
  _mga_priv.image_width           = 0;
  _mga_priv.image_height          = 0;
  _mga_priv.ratio                 = 0;
  _mga_priv.bLogoMode             = 0;
//  _mga_priv.cur_image             = NULL;
  _mga_priv.user_ratio            = ASPECT_AUTO;
  _mga_priv.user_ratio_changed    = 0 ;
  _mga_priv.fourcc_format         = 0;
  _mga_priv.request_dest_size     = ((x11_visual_t*) visual)->request_dest_size;

  _window.clasped_window          = 0;
  _display.default_screen         = DefaultScreen(_mga_priv.lDisplay);
  _mga_priv.cont_min              = 0;
  _mga_priv.cont_max              = 255;
  _mga_priv.bright_max            = 127;
  _mga_priv.bright_min            = -128;

  _mga_priv.overlay_state         = 1;             // 0 = off, 1 = on

  if (ioctl(_mga_priv.fd,SYNCFB_GET_PARAMS,&_mga_priv.param) == 0) {
     _mga_priv.cont_current   = _mga_priv.param.contrast;
     _mga_priv.bright_current = _mga_priv.param.brightness;
  }
  else {
     _mga_priv.cont_current   = 0x80;
     _mga_priv.bright_current = 0;
     xprintf(VERBOSE|VIDEO,"syncfb:Brightness and Contrast control not available, please update your syncfb module");
     printf("syncfb:Brightness and Contrast control not available, please update your syncfb module\n");

  }

  set_logo_mode_mga(0);

  return &vo_mga;
}


static vo_info_t vo_info_mga = {
  2,
  "Syncfb",
  "xine video output plugin using MGA Teletux (syncfb) video extension",
  VISUAL_TYPE_X11,
  10
};

vo_info_t *get_video_out_plugin_info() {
  return &vo_info_mga;
}




/*  #else  *//* no MGA */

/*  vo_functions_t *init_video_out_xv () { */
/*    fprintf (stderr, "Xvideo support not compiled in\n"); */
/*    return NULL; */
/*  } */

/* #endif */ /* HAVE_XV */
