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
 * $Id: video_out_sdl.c,v 1.3 2002/01/22 01:43:13 miguelfreitas Exp $
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
 *    - logo state aware
 *    - fullscreen
 *    - stability, testing, etc?? ;) 
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

  /* size / aspect ratio calculations */
  /* delivered images */
  int                delivered_width;      /* everything is set up for
					      these frame dimensions          */
  int                delivered_height;     /* the dimension as they come
					      from the decoder                */
  int                delivered_ratio_code;
  double             ratio_factor;         /* output frame must fullfill:
					      height = width * ratio_factor   */
 
  /* Window */
  int                window_width;
  int                window_height;
  
  /* output screen area */
  int                output_width;         /* frames will appear in this
					      size (pixels) on screen         */
  int                output_height;
  int                output_xoffset;
  int                output_yoffset;

  /* display anatomy */
  double             display_ratio;        /* given by visual parameter
					      from init function              */

  void              *user_data;

  /* gui callback */

  void             (*request_dest_size) (void *user_data,
					 int video_width, int video_height,
					 int *dest_x, int *dest_y,
					 int *dest_height, int *dest_width);
};

vo_info_t *get_video_out_plugin_info();

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

static void sdl_adapt_to_output_area (sdl_driver_t *this,
				     int dest_x, int dest_y,
				     int dest_width, int dest_height) {

#ifdef SDL_LOG
  printf ("video_out_sdl: dest_width %d, dest_height %d\n",
	  dest_width, dest_height );
#endif
 
  /*
   * make the frames fit into the given destination area
   */
  if ( ((double) dest_width / this->ratio_factor) < dest_height ) {
    this->output_width   = dest_width;
    this->output_height  = (double) dest_width / this->ratio_factor ;
    this->output_xoffset = 0;
    this->output_yoffset = (dest_height - this->output_height) / 2;
  } else {
    this->output_width   = (double) dest_height * this->ratio_factor ;
    this->output_height  = dest_height;
    this->output_xoffset = (dest_width - this->output_width) / 2;
    this->output_yoffset = 0;
  }
  
  if( dest_width != this->window_width || dest_height != this->window_height ) {
    this->window_width = dest_width;
    this->window_height = dest_height;
    this->surface = SDL_SetVideoMode (this->window_width, this->window_height, 
                                      this->bpp, this->sdlflags);
  }
}


static void sdl_calc_format (sdl_driver_t *this,
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

#ifdef SDL_LOG
  printf ("video_out_sdl: display_ratio : %f\n", this->display_ratio);
  printf ("video_out_sdl: stream aspect ratio : %f , code : %d\n",
	  image_ratio, ratio_code);
#endif

  switch (this->user_ratio) {
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
      printf ("video_out_sdl: unknown aspect ratio (%d) in stream => using 4:3\n",
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
  if ( ideal_width<400 ) {
    ideal_width  *=2;
    ideal_height *=2;
  }

  /*
   * ask gui to adapt to this size
   */
#ifdef HAVE_X11                             
  this->request_dest_size (this->user_data,
			   ideal_width, ideal_height,
			   &dest_x, &dest_y, &dest_width, &dest_height);
#else
  dest_x = 0;
  dest_y = 0;
  dest_width = ideal_width;
  dest_height = ideal_height;
#endif

  sdl_adapt_to_output_area (this, dest_x, dest_y, dest_width, dest_height);
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
 
#ifdef SDL_LOG
      printf ("[%p %p %p]\n", frame->overlay->pixels[0], frame->overlay->pixels[1],
                              frame->overlay->pixels[2] );
#endif
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
    if (event.type == SDL_VIDEORESIZE)
      sdl_adapt_to_output_area(this, 0, 0, event.resize.w, event.resize.h);
      /*
      this->surface = SDL_SetVideoMode (event.resize.w, event.resize.h,
                                        this->bpp, this->sdlflags);
      */                               
  }
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
      sdl_calc_format (this, frame->width, frame->height, frame->ratio_code);
  }
    
  SDL_UnlockYUVOverlay (frame->overlay);
  /*
  SDL_DisplayYUVOverlay (frame->overlay, &(this->surface->clip_rect));
  */
  clip_rect.x = this->output_xoffset;  
  clip_rect.y = this->output_yoffset;  
  clip_rect.w = this->output_width;  
  clip_rect.h = this->output_height;  
  SDL_DisplayYUVOverlay (frame->overlay, &clip_rect);
  
  sdl_check_events (this);
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
    
    sdl_calc_format (this, this->delivered_width, this->delivered_height,
                    this->delivered_ratio_code) ;

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
  x11_rectangle_t *area;

  pthread_mutex_lock(&this->mutex);
    
  switch (data_type) {
  case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:
#ifdef SDL_LOG
      printf ("video_out_sdl: GUI_DATA_EX_DEST_POS_SIZE_CHANGED\n");
#endif

    area = (x11_rectangle_t *) data;
    sdl_adapt_to_output_area (this, area->x, area->y, area->w, area->h);
    break;

  case GUI_DATA_EX_LOGO_VISIBILITY:
#ifdef SDL_LOG
      printf ("video_out_sdl: GUI_DATA_EX_LOGO_VISIBILITY\n");
#endif
    /* FIXME: implement */
    break;

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
  this->request_dest_size = visual->request_dest_size;
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
  this->window_width         = window_attributes.width;
  this->window_height        = window_attributes.height;
#else
  this->window_width         = 320;
  this->window_height        = 240;
#endif

  this->surface = SDL_SetVideoMode (this->window_width, this->window_height,
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
  this->vo_driver.get_info             = get_video_out_plugin_info;

  printf ("video_out_sdl: warning, xine's SDL driver is EXPERIMENTAL\n");
  printf ("video_out_sdl: fullscreen mode is NOT supported\n");
  return &this->vo_driver;
}

static vo_info_t vo_info_sdl = {
  3,
  "sdl",
  "xine video output plugin using Simple DirectMedia Layer",
  VISUAL_TYPE_X11,
  4  
};

vo_info_t *get_video_out_plugin_info() {
  return &vo_info_sdl;
}


