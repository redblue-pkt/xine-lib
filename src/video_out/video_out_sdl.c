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
 * $Id: video_out_sdl.c,v 1.9 2002/07/10 14:04:41 mroi Exp $
 * 
 * video_out_sdl.c, Simple DirectMedia Layer
 *
 * based on mpeg2dec code from
 *   Ryan C. Gordon <icculus@lokigames.com> and
 *   Dominik Schnitzer <aeneas@linuxvideo.org>
 *
 * xine version by Miguel Freitas (Jan/2002)
 *   Missing features:
 *    - mouse position translation
 *    - fullscreen
 *    - stability, testing, etc?? ;) 
 *   Known bugs:
 *    - without X11, a resize is need to show image (?)
 *
 *  BIG WARNING HERE: if you use RedHat SDL packages you will probably
 *  get segfault when no hwaccel is available. more info at:
 *    https://bugzilla.redhat.com/bugzilla/show_bug.cgi?id=58408
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <SDL/SDL.h>

#include "video_out.h"
#include "xine_internal.h"
#include "alphablend.h"
#include "xineutils.h"

#ifdef HAVE_X11
#include <X11/Xlib.h>
#include "video_out_x11.h"
#endif

/*
#define SDL_LOG
*/           

typedef struct sdl_driver_s sdl_driver_t;

typedef struct sdl_frame_s {
    vo_frame_t vo_frame;
    int width, height, ratio_code, format;
    SDL_Overlay * overlay;
} sdl_frame_t;


struct sdl_driver_s {

  vo_driver_t        vo_driver;

  config_values_t   *config;

  SDL_Surface * surface;
  uint32_t sdlflags;
  uint8_t bpp;
  
  pthread_mutex_t    mutex;

  int user_ratio;

  uint32_t           capabilities;

#ifdef HAVE_X11
   /* X11 / Xv related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;
#endif

  /* 
   * "delivered" size:
   * frame dimension / aspect as delivered by the decoder
   * used (among other things) to detect frame size changes
   */
  int                delivered_width;   
  int                delivered_height;     
  int                delivered_ratio_code;

  /* 
   * displayed part of delivered images,
   * taking zoom into account
   */

  int                displayed_xoffset;
  int                displayed_yoffset;
  int                displayed_width;
  int                displayed_height;
  
  /* 
   * "ideal" size :
   * displayed width/height corrected by aspect ratio
   */

  int                ideal_width, ideal_height;
  double             ratio_factor;         /* output frame must fullfill:
					      height = width * ratio_factor   */

  /*
   * "gui" size / offset:
   * what gui told us about where to display the video
   */
  
  int                gui_x, gui_y;
  int                gui_width, gui_height;
  int                gui_win_x, gui_win_y;
  
  /*
   * "output" size:
   *
   * this is finally the ideal size "fitted" into the
   * gui size while maintaining the aspect ratio
   * 
   */

  /* Window */
  int                output_width;
  int                output_height;
  int                output_xoffset;
  int                output_yoffset;

  /* display anatomy */
  double             display_ratio;        /* given by visual parameter
					      from init function              */

  void              *user_data;

  /* gui callback */

  void (*frame_output_cb) (void *user_data,
			   int video_width, int video_height,
			   int *dest_x, int *dest_y, 
			   int *dest_height, int *dest_width,
			   int *win_x, int *win_y);
};

static uint32_t sdl_get_capabilities (vo_driver_t *this_gen) {

  sdl_driver_t *this = (sdl_driver_t *) this_gen;

  return this->capabilities;
}

static void sdl_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed for SDL */
}

static void sdl_frame_dispose (vo_frame_t *vo_img) {

  sdl_frame_t  *frame = (sdl_frame_t *) vo_img ;

  if( frame->overlay )
    SDL_FreeYUVOverlay (frame->overlay);
  
  free (frame);
}

static vo_frame_t *sdl_alloc_frame (vo_driver_t *this_gen) {

  sdl_frame_t     *frame ;

  frame = (sdl_frame_t *) malloc (sizeof (sdl_frame_t));
  memset (frame, 0, sizeof(sdl_frame_t));

  if (frame==NULL) {
    printf ("sdl_alloc_frame: out of memory\n");
  }

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */

  frame->vo_frame.copy    = NULL;
  frame->vo_frame.field   = sdl_frame_field;
  frame->vo_frame.dispose = sdl_frame_dispose;

  return (vo_frame_t *) frame;
}
/*
 * make ideal width/height "fit" into the gui
 */

static void sdl_compute_output_size (sdl_driver_t *this) {
  
  double x_factor, y_factor;

  x_factor = (double) this->gui_width  / (double) this->ideal_width;
  y_factor = (double) this->gui_height / (double) this->ideal_height;
  
  if ( x_factor < y_factor ) {
    this->output_width   = (double) this->gui_width;
    this->output_height  = (double) this->ideal_height * x_factor ;
  } else {
    this->output_width   = (double) this->ideal_width  * y_factor ;
    this->output_height  = (double) this->gui_height;
  }

  this->output_xoffset = (this->gui_width - this->output_width) / 2 + this->gui_x;
  this->output_yoffset = (this->gui_height - this->output_height) / 2 + this->gui_y;

#ifdef LOG
  printf ("video_out_sdl: frame source %d x %d => screen output %d x %d\n",
	  this->delivered_width, this->delivered_height,
	  this->output_width, this->output_height);
#endif
}

static void sdl_compute_ideal_size (sdl_driver_t *this) {

  double image_ratio, desired_ratio, corr_factor;
  
  this->displayed_xoffset = (this->delivered_width  - this->displayed_width) / 2;
  this->displayed_yoffset = (this->delivered_height - this->displayed_height) / 2;

  /* 
   * aspect ratio
   */

  image_ratio = (double) this->delivered_width / (double) this->delivered_height;
  
  switch (this->user_ratio) {
  case ASPECT_AUTO:
    switch (this->delivered_ratio_code) {
    case XINE_ASPECT_RATIO_ANAMORPHIC:  /* anamorphic     */
    case XINE_ASPECT_RATIO_PAN_SCAN:    /* we display pan&scan as widescreen */
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
      printf ("video_out_sdl: invalid ratio, using 4:3\n");
    default:
      printf ("video_out_sdl: unknown aspect ratio (%d) in stream => using 4:3\n",
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

  corr_factor = this->ratio_factor / image_ratio ;

  if (fabs(corr_factor - 1.0) < 0.005) {
    this->ideal_width  = this->delivered_width;
    this->ideal_height = this->delivered_height;

  } else {

    if (corr_factor >= 1.0) {
      this->ideal_width  = this->delivered_width * corr_factor + 0.5;
      this->ideal_height = this->delivered_height;
    } else {
      this->ideal_width  = this->delivered_width;
      this->ideal_height = this->delivered_height / corr_factor + 0.5;
    }
  }
}


static void sdl_update_frame_format (vo_driver_t *this_gen,
				    vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height,
				    int ratio_code, int format, int flags) {

  sdl_driver_t  *this = (sdl_driver_t *) this_gen;
  sdl_frame_t   *frame = (sdl_frame_t *) frame_gen;
  
  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

    /*
     * (re-) allocate image
     */
    
    if( frame->overlay ) {
      SDL_FreeYUVOverlay (frame->overlay);
      frame->overlay = NULL; 
    }
      
    if( format == IMGFMT_YV12 ) {
#ifdef SDL_LOG
      printf ("video_out_sdl: format YV12 ");
#endif
      frame->overlay = SDL_CreateYUVOverlay (width, height, SDL_YV12_OVERLAY,
					     this->surface);
      
    } else if( format == IMGFMT_YUY2 ) {
#ifdef SDL_LOG
      printf ("video_out_sdl: format YUY2 ");
#endif
      frame->overlay = SDL_CreateYUVOverlay (width, height, SDL_YUY2_OVERLAY,
					     this->surface);
    }
    
    if (frame->overlay == NULL)
      return;
 
    frame->vo_frame.base[0] = frame->overlay->pixels[0];
    frame->vo_frame.base[1] = frame->overlay->pixels[2];
    frame->vo_frame.base[2] = frame->overlay->pixels[1];
    
    frame->width  = width;
    frame->height = height;
    frame->format = format;
  }

  frame->ratio_code = ratio_code;
  SDL_LockYUVOverlay (frame->overlay);
}


/*
 *
 */
static void sdl_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {

  sdl_frame_t   *frame = (sdl_frame_t *) frame_gen;
  
  if (overlay->rle) {
    if( frame->format == IMGFMT_YV12 )
      blend_yuv( frame->vo_frame.base, overlay, frame->width, frame->height);
    else
      blend_yuy2( frame->vo_frame.base[0], overlay, frame->width, frame->height);
  }
  
}

static void sdl_check_events (sdl_driver_t * this)
{
  SDL_Event event;

  while (SDL_PollEvent (&event)) {
    if (event.type == SDL_VIDEORESIZE) {
      if( event.resize.w != this->gui_width || event.resize.h != this->gui_height ) {
        this->gui_width = event.resize.w;
        this->gui_height = event.resize.h;
        
        sdl_compute_output_size(this);
        
        this->surface = SDL_SetVideoMode (this->gui_width, this->gui_height, 
                                      this->bpp, this->sdlflags);
      }
    }
  }
}

static int sdl_redraw_needed (vo_driver_t *this_gen) {
  sdl_driver_t  *this = (sdl_driver_t *) this_gen;
  int ret = 0;
#ifdef HAVE_X11
  int gui_x, gui_y, gui_width, gui_height, gui_win_x, gui_win_y;
  
  this->frame_output_cb (this->user_data,
			 this->ideal_width, this->ideal_height, 
			 &gui_x, &gui_y, &gui_width, &gui_height,
			 &gui_win_x, &gui_win_y );

  if ( (gui_x != this->gui_x) || (gui_y != this->gui_y)
      || (gui_width != this->gui_width) || (gui_height != this->gui_height)
      || (gui_win_x != this->gui_win_x) || (gui_win_y != this->gui_win_y) ) {

    this->gui_x      = gui_x;
    this->gui_y      = gui_y;
    this->gui_width  = gui_width;
    this->gui_height = gui_height;
    this->gui_win_x  = gui_win_x;
    this->gui_win_y  = gui_win_y;

    sdl_compute_output_size (this);

    ret = 1;
  }
  
  return ret;
#else
  static int last_gui_width, last_gui_height;
  
  if( last_gui_width != this->gui_width ||
      last_gui_height != this->gui_height ) {
    
    last_gui_width = this->gui_width;
    last_gui_height = this->gui_height;
  
    sdl_compute_output_size (this);

    ret = 1;
  }
  return 0;
#endif
}


static void sdl_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  sdl_driver_t  *this = (sdl_driver_t *) this_gen;
  sdl_frame_t   *frame = (sdl_frame_t *) frame_gen;
  SDL_Rect clip_rect;

  pthread_mutex_lock(&this->mutex);

  if ( (frame->width != this->delivered_width)
	 || (frame->height != this->delivered_height)
	 || (frame->ratio_code != this->delivered_ratio_code) ) {
	 printf("video_out_sdl: change frame format\n");
      
      this->delivered_width      = frame->width;
      this->delivered_height     = frame->height;
      this->delivered_ratio_code = frame->ratio_code;

      sdl_compute_ideal_size( this );
  }
    
  /* 
   * tell gui that we are about to display a frame,
   * ask for offset and output size
   */
  sdl_check_events (this);
  sdl_redraw_needed (this_gen);
    
  SDL_UnlockYUVOverlay (frame->overlay);
  /*
  SDL_DisplayYUVOverlay (frame->overlay, &(this->surface->clip_rect));
  */
  clip_rect.x = this->output_xoffset;  
  clip_rect.y = this->output_yoffset;  
  clip_rect.w = this->output_width;  
  clip_rect.h = this->output_height;  
  SDL_DisplayYUVOverlay (frame->overlay, &clip_rect);
  
  frame->vo_frame.displayed (&frame->vo_frame);
  
  pthread_mutex_unlock(&this->mutex);

}

static int sdl_get_property (vo_driver_t *this_gen, int property) {

  sdl_driver_t *this = (sdl_driver_t *) this_gen;
  
  if ( property == VO_PROP_ASPECT_RATIO)
    return this->user_ratio ;
  
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

static int sdl_set_property (vo_driver_t *this_gen,
			    int property, int value) {

  sdl_driver_t *this = (sdl_driver_t *) this_gen;
  
  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>=NUM_ASPECT_RATIOS)
      value = ASPECT_AUTO;
    this->user_ratio = value;
    printf("video_out_sdl: aspect ratio changed to %s\n",
	   aspect_ratio_name(value));
    
    sdl_compute_ideal_size (this);
  } 
  
  return value;
}

static void sdl_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

/*  sdl_driver_t *this = (sdl_driver_t *) this_gen; */
}

static void sdl_translate_gui2video(sdl_driver_t *this,
				   int x, int y,
				   int *vid_x, int *vid_y)
{
}

static int sdl_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {

  int ret = 0;
#ifdef HAVE_X11
  sdl_driver_t     *this = (sdl_driver_t *) this_gen;

  pthread_mutex_lock(&this->mutex);
    
  switch (data_type) {

  case GUI_DATA_EX_DRAWABLE_CHANGED:
#ifdef SDL_LOG
      printf ("video_out_sdl: GUI_DATA_EX_DRAWABLE_CHANGED\n");
#endif
    
    this->drawable = (Drawable) data;
    /* OOPS! Is it possible to change SDL window id? */
    /* probably we need to close and reinitialize SDL */
    break;
  
  case GUI_DATA_EX_EXPOSE_EVENT:
#ifdef SDL_LOG
      printf ("video_out_sdl: GUI_DATA_EX_EXPOSE_EVENT\n");
#endif
    break;
      
  default:
    ret = -1;
  }
  pthread_mutex_unlock(&this->mutex);

#else
  ret = -1;
#endif

  return ret;
}
                            
static void sdl_exit (vo_driver_t *this_gen) {

  sdl_driver_t *this = (sdl_driver_t *) this_gen;

  SDL_FreeSurface (this->surface);
  SDL_QuitSubSystem (SDL_INIT_VIDEO);
}

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  sdl_driver_t          *this;
  const SDL_VideoInfo * vidInfo;
#ifdef HAVE_X11
  static char SDL_windowhack[32];
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XWindowAttributes window_attributes;
#endif
    
  this = malloc (sizeof (sdl_driver_t));

  if (!this) {
    printf ("video_out_sdl: malloc failed\n");
    return NULL;
  }
  memset (this, 0, sizeof(sdl_driver_t));

  this->sdlflags = SDL_HWSURFACE | SDL_RESIZABLE;

  xine_setenv("SDL_VIDEO_YUV_HWACCEL", "1", 1);
  xine_setenv("SDL_VIDEO_X11_NODIRECTCOLOR", "1", 1);
  
#ifdef HAVE_X11
  this->display           = visual->display;
  this->screen            = visual->screen;
  this->display_ratio     = visual->display_ratio;
  this->drawable          = visual->d;
  this->frame_output_cb   = visual->frame_output_cb;
  this->user_data         = visual->user_data;
  
  /* set SDL to use our existing X11 window */
  sprintf(SDL_windowhack,"SDL_WINDOWID=0x%x", (uint32_t) this->drawable );
  putenv(SDL_windowhack);
#else
  this->display_ratio     = 1.0;
#endif
  
  if (SDL_Init (SDL_INIT_VIDEO)) {
    fprintf (stderr, "sdl video initialization failed.\n");
    return NULL;
  }

  vidInfo = SDL_GetVideoInfo ();
  if (!SDL_ListModes (vidInfo->vfmt, SDL_HWSURFACE | SDL_RESIZABLE)) {
    this->sdlflags = SDL_RESIZABLE;
    if (!SDL_ListModes (vidInfo->vfmt, SDL_RESIZABLE)) {
      fprintf (stderr, "sdl couldn't get any acceptable video mode\n");
      return NULL;
    }
  }
  
  this->bpp = vidInfo->vfmt->BitsPerPixel;
  if (this->bpp < 16) {
    fprintf(stderr, "sdl has to emulate a 16 bit surfaces, "
                    "that will slow things down.\n");
    this->bpp = 16;
  }
      
  this->config            = config;
  pthread_mutex_init (&this->mutex, NULL);
  
  this->output_xoffset    = 0;
  this->output_yoffset    = 0;
  this->output_width      = 0;
  this->output_height     = 0;
  this->capabilities      = VO_CAP_YUY2 | VO_CAP_YV12;

#ifdef HAVE_X11  
  XGetWindowAttributes(this->display, this->drawable, &window_attributes);
  this->gui_width         = window_attributes.width;
  this->gui_height        = window_attributes.height;
#else
  this->gui_width         = 320;
  this->gui_height        = 240;
#endif

  this->surface = SDL_SetVideoMode (this->gui_width, this->gui_height,
                                    this->bpp, this->sdlflags);
  
  this->vo_driver.get_capabilities     = sdl_get_capabilities;
  this->vo_driver.alloc_frame          = sdl_alloc_frame;
  this->vo_driver.update_frame_format  = sdl_update_frame_format;
  this->vo_driver.overlay_blend        = sdl_overlay_blend;
  this->vo_driver.display_frame        = sdl_display_frame;
  this->vo_driver.get_property         = sdl_get_property;
  this->vo_driver.set_property         = sdl_set_property;
  this->vo_driver.get_property_min_max = sdl_get_property_min_max;
  this->vo_driver.gui_data_exchange    = sdl_gui_data_exchange;
  this->vo_driver.exit                 = sdl_exit;
  this->vo_driver.redraw_needed        = sdl_redraw_needed;

  printf ("video_out_sdl: warning, xine's SDL driver is EXPERIMENTAL\n");
  printf ("video_out_sdl: fullscreen mode is NOT supported\n");
  return &this->vo_driver;
}

static vo_info_t vo_info_sdl = {
  5,
  "sdl",
  NULL,
  VISUAL_TYPE_X11,
  4  
};

vo_info_t *get_video_out_plugin_info() {
  vo_info_sdl.description = _("xine video output plugin using Simple DirectMedia Layer");
  return &vo_info_sdl;
}


