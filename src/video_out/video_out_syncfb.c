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
 * $Id: video_out_syncfb.c,v 1.1 2001/04/24 20:53:00 f1rmb Exp $
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

#include <X11/Xutil.h>

#include "video_out.h"
#include "video_out_syncfb.h"

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

Display *lDisplay;
extern Display *gDisplay;
extern Window   gVideoWin;
extern Pixmap   gXineLogo;
extern int      gXineLogoWidth, gXineLogoHeight;

extern uint32_t xine_debug;



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

  uint32_t        ratio;
  int             user_ratio, user_ratio_changed;

//  XvImage        *cur_image;

  /*
   * misc (read: fun ;))
   */
  int             bLogoMode;

  int             bright_min, bright_current, bright_max;
  int             cont_min, cont_current, cont_max;

  int             overlay_state;

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






static int get_capabilities_mga () {
  return VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_CONTRAST | VO_CAP_BRIGHTNESS;
}


static void setup_window_mga () {
  int                   ww=0, wh=0;
  float                 aspect;

  XWindowAttributes     wattr;

  Atom                  prop;
  MWMHints              mwmhints;
   
  XGetWindowAttributes(lDisplay, DefaultRootWindow(lDisplay), &wattr);
   
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
   _mga_priv.mga_vid_config.syncfb_mode = SYNCFB_FEATURE_BLOCK_REQUEST | SYNCFB_FEATURE_SCALE_H | SYNCFB_FEATURE_SCALE_V | SYNCFB_FEATURE_CROP ; /*   | SYNCFB_FEATURE_DEINTERLACE; */
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

   // create a simple window without anything. Just make overlay clickable. :)
   if (!_window.clasped_window) {
     _window.clasped_window = XCreateSimpleWindow(lDisplay, RootWindow(lDisplay, _display.default_screen), 0, 0, _mga_priv.dest_width, _mga_priv.dest_height, 0, 0, 0);
     gVideoWin = _window.clasped_window;

     // turn off all borders etc. (taken from the Xv plugin)
     prop = XInternAtom(lDisplay, "_MOTIF_WM_HINTS", False);
     mwmhints.flags = MWM_HINTS_DECORATIONS;
     mwmhints.decorations = 0;
     XChangeProperty(lDisplay, gVideoWin, prop, prop, 32,
		     PropModeReplace, (unsigned char *) &mwmhints,
		     PROP_MWM_HINTS_ELEMENTS);
     XSetTransientForHint(lDisplay, gVideoWin, None);
      
     XSelectInput(gDisplay, _window.clasped_window, VisibilityChangeMask | KeyPressMask | ButtonPressMask | SubstructureNotifyMask | StructureNotifyMask);
     XMapRaised(lDisplay, _window.clasped_window);
     XSync(lDisplay,0);
   }   

   XSetStandardProperties(lDisplay, _window.clasped_window, _window.title, _window.title, None, NULL, 0, NULL);
   XMoveResizeWindow(lDisplay, _window.clasped_window, (_mga_priv.bIsFullscreen) ? 0 : _mga_priv.image_xoff, (_mga_priv.bIsFullscreen) ? 0 : _mga_priv.image_yoff, (_mga_priv.bIsFullscreen) ? _display.width : _mga_priv.dest_width, (_mga_priv.bIsFullscreen) ? _display.height : _mga_priv.dest_height);
   XMapRaised(lDisplay, _window.clasped_window);
   XSync(lDisplay,0);
}



/* setup internal variables and (re-)init window if necessary */
static int set_image_format_mga (uint32_t width, uint32_t height, uint32_t ratio, int format) {


  double res_h, res_v, display_ratio, aspect_ratio;

  if ( (_mga_priv.image_width == width) 
       && (_mga_priv.image_height == height)
       && (_mga_priv.ratio == ratio) 
       && (_mga_priv.bFullscreen == _mga_priv.bIsFullscreen)
       && (_mga_priv.fourcc_format == format)
       && !_mga_priv.user_ratio_changed ) {


    return 0;
  }

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
    case 1: /* "square" => 4:3 */
    case 2:
      aspect_ratio = 4.0 / 3.0;
      break;
    case 3:
      aspect_ratio = 16.0 / 9.0;
      break;
    case 42: /* some stupid stream => don't touch aspect ratio */
    default:
      xprintf (VIDEO, "unknown aspect ratio (%d) in stream. untouched.\n", _mga_priv.ratio);
      aspect_ratio = (double)_mga_priv.image_width / (double)_mga_priv.image_height;
      break;
    }
  } else if (_mga_priv.user_ratio == ASPECT_ANAMORPHIC) {
    aspect_ratio = 16.0 / 9.0;
  } else if (_mga_priv.user_ratio == ASPECT_DVB) {
    aspect_ratio = 2.0 / 1.0;
  } else {
    aspect_ratio = 1.0;
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

  return 1;
}

static void dispose_image_buffer_mga (vo_image_buffer_t *image) {

  free (image->mem[0]);
  free (image);

}

static vo_image_buffer_t *alloc_image_buffer_mga () {

  vo_image_buffer_t *image;

  if (!(image = malloc (sizeof (vo_image_buffer_t))))
       return NULL;

 // we only know how to do 4:2:0 planar yuv right now.
 // we prepare for YUY2 sizes
  if (!(image->mem[0] = malloc (_mga_priv.image_width * _mga_priv.image_height * 2))) {
         free(image);
         return NULL;
  }

  image->mem[2] = image->mem[0] + _mga_priv.image_width * _mga_priv.image_height;
  image->mem[1] = image->mem[0] + _mga_priv.image_width * _mga_priv.image_height * 5 / 4;

  pthread_mutex_init (&image->mutex, NULL);

  return image;
}

static void process_macroblock_mga (vo_image_buffer_t *img, 
				   uint8_t *py, uint8_t *pu, uint8_t *pv,
				   int slice_offset,
				   int offset){
}

int is_fullscreen_mga () {
  return _mga_priv.bFullscreen;
}

void set_fullscreen_mga (int bFullscreen) {
  _mga_priv.bFullscreen = bFullscreen;

  
  set_image_format_mga (_mga_priv.image_width, _mga_priv.image_height, _mga_priv.ratio,
			_mga_priv.fourcc_format);
  
}


static void display_frame_mga(vo_image_buffer_t *vo_img) {
   
  // only write frame if overlay is active (otherwise syncfb hangs)
  if (_mga_priv.overlay_state == 1) {
    ioctl(_mga_priv.fd,SYNCFB_REQUEST_BUFFER,&_mga_priv.bufinfo);
    //printf("get buffer %d\n",_mga_priv.bufinfo.id);
    if ( _mga_priv.bufinfo.id == -1 ) {
      printf( "Got buffer #%d\n", _mga_priv.bufinfo.id );
      return;
    }

    _mga_priv.vid_data = (uint_8 *)(_mga_priv.frame0 + _mga_priv.bufinfo.offset);

    _mga_write_frame_g400(vo_img->mem);

    ioctl(_mga_priv.fd,SYNCFB_COMMIT_BUFFER,&_mga_priv.bufinfo);
  }
  /* Image is copied so release buffer */
  vo_image_drawn (  (vo_image_buffer_t *) vo_img);
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

void reset_mga () {
}

void display_cursor_mga () {
}

void set_aspect_mga (int ratio) {

  ratio %= 4;

  if (ratio != _mga_priv.user_ratio) {
    _mga_priv.user_ratio         = ratio;
    _mga_priv.user_ratio_changed = 1;

    set_image_format_mga (_mga_priv.image_width, _mga_priv.image_height, _mga_priv.ratio, _mga_priv.fourcc_format);
  }
}

int get_aspect_mga () {
  return _mga_priv.user_ratio;
}

void exit_mga () {
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

static int get_noop(void) {
  return 0;
}

static int set_noop(int v) {
  return v;
}

/*
 * Contrast settings
 */
static int get_contrast_min(void) {
  return _mga_priv.cont_min;
}
static int get_current_contrast(void) {
  return _mga_priv.cont_current;
}
static int set_contrast(int value) {

  _mga_priv.param.contrast = value;
  _mga_priv.param.brightness = _mga_priv.bright_current;
  
  if (ioctl(_mga_priv.fd,SYNCFB_SET_PARAMS,&_mga_priv.param) == 0) {
    _mga_priv.cont_current = _mga_priv.param.contrast;
    return _mga_priv.cont_current;
  }
  return value;
}

static int get_contrast_max(void) {
  return _mga_priv.cont_max;
}

/*
 * Brightness settings
 */
static int get_brightness_min(void) {
  return _mga_priv.bright_min;
}
static int get_current_brightness(void) {
  return _mga_priv.bright_current;
}
static int set_brightness(int value) {

  _mga_priv.param.brightness = value;
  _mga_priv.param.contrast = _mga_priv.cont_current;

  if (ioctl(_mga_priv.fd,SYNCFB_SET_PARAMS,&_mga_priv.param) == 0) {
    _mga_priv.bright_current = _mga_priv.param.brightness;
    return _mga_priv.bright_current;
  }
  return value;
}

static int get_brightness_max(void) {
  return _mga_priv.bright_max;
}

static void reset_settings(void) {
    set_contrast(config_file_lookup_int ("contrast", _mga_priv.cont_current));
    set_brightness(config_file_lookup_int ("brightness", _mga_priv.bright_current));
}

static void save_settings(void) {
    config_file_set_int ("brightness", _mga_priv.bright_current);
    config_file_set_int ("contrast", _mga_priv.cont_current);
}


static vo_driver_t vo_mga = {
  get_capabilities_mga,
  set_image_format_mga,
  alloc_image_buffer_mga,
  dispose_image_buffer_mga,
  process_macroblock_mga,
  display_frame_mga,
  set_fullscreen_mga,
  is_fullscreen_mga,
  handle_event_mga,
  set_logo_mode_mga,
  reset_mga,
  display_cursor_mga,
  set_aspect_mga,
  get_aspect_mga,
  exit_mga,
  /* HUE min, current, set , max */
  get_noop,
  get_noop,
  set_noop,
  get_noop,

  /* SATURATION min, current, set , max */
  get_noop,
  get_noop,
  set_noop,
  get_noop,

  /* Brightness min, current, set , max */
  get_brightness_min,
  get_current_brightness,
  set_brightness,
  get_brightness_max,

  /* Contrast min, current, set , max */
  get_contrast_min,
  get_current_contrast,
  set_contrast,
  get_contrast_max,
  
  /* Colorkey min , current set */
  get_noop,
  get_noop,
  set_noop,
  reset_settings,
  save_settings
};


/* 
 * connect to server, create and map window,
 * allocate colors and (shared) memory
 */

vo_driver_t *init_video_out_mga () {

  char name[]= "/dev/syncfb";

  _mga_priv.image_width=720;
  _mga_priv.image_height=576;


  if ((_mga_priv.fd = open ((char *) name, O_RDWR)) < 0) {
       xprintf(VERBOSE|VIDEO, "Can't open %s\n", (char *) name);
       return 0;
  }

  if (ioctl(_mga_priv.fd,SYNCFB_GET_CAPS,&_mga_priv.caps)) {
     xprintf(VERBOSE|VIDEO,"Error in config ioctl");
     close(_mga_priv.fd);
     return 0;
  }

  _mga_priv.vid_data = (char*)mmap(0,_mga_priv.caps.memory_size,PROT_WRITE,MAP_SHARED,_mga_priv.fd,0);

  //clear the buffer
//  memset(_mga_priv.vid_data,0,1024*768*2);


  _mga_priv.frame0   = _mga_priv.vid_data;



  /*
   * init global variables
   */

  strcpy(_window.title, "Xine syncfb overlay\0");
  _window.visibility              = 1;
   
  lDisplay = XOpenDisplay(":0.0");  /* Xine may run on another Display but syncfb always goes to :0.0 */
//   lDisplay = gDisplay;
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

  _window.clasped_window          = 0;
  _display.default_screen         = DefaultScreen(lDisplay);
  _mga_priv.cont_min              = 0;
  _mga_priv.cont_max              = 255;
  _mga_priv.bright_max            = 127;
  _mga_priv.bright_min            = -128;

  _mga_priv.overlay_state         = 0;             // 0 = off, 1 = on

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

  set_logo_mode_mga(1);

  return &vo_mga;
}

/*  #else  *//* no MGA */

/*  vo_functions_t *init_video_out_xv () { */
/*    fprintf (stderr, "Xvideo support not compiled in\n"); */
/*    return NULL; */
/*  } */

/* #endif */ /* HAVE_XV */
