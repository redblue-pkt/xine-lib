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
 * $Id: video_out_opengl.c,v 1.5 2002/02/09 07:13:24 guenter Exp $
 * 
 * video_out_glut.c, glut based OpenGL rendering interface for xine
 * Matthias Hopf <mat@mshopf.de>
 *
 * Based on video_out_xshm.c and video_out_xv.c
 */

/*
 * TODO:
 *
 * - Race Condition? Sometimes the output window remains empty...
 * - Strange frame when stopping playback
 * - Works only with first video so far - the next video seems to be
 *   rendered by a different thread, without notification :-(
 * - Textures instead of glDraw()
 * - Use GL_RGBA vs GL_BGRA (endianess?!?)
 * - Check extensions
 * - Color conversion in hardware?
 *   - Video extension
 *   - ColorMatrix (OpenGL-1.2 or SGI_color_matrix)
 * - Alpha Blending for overlays using texture hardware
 * - Check overlay colors (still YUV?!?)
 */


#if 0
#  define DEBUGF(x) fprintf x
#else
#  define DEBUGF(x) ((void) 0)
#endif

/* Uncomment for some fun! */
/*
#define SPHERE
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <X11/Xlib.h>

#include "video_out.h"
#include "video_out_x11.h"

#include <GL/gl.h>
#include <GL/glx.h>
#ifdef HAVE_GLUT
#include <GL/glut.h>
#else
#ifdef HAVE_GLU
#include <GL/glu.h>
#endif
#endif

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <pthread.h>
#include <netinet/in.h>

#include "xine_internal.h"
#include "alphablend.h"
#include "yuv2rgb.h"
#include "xineutils.h"


#define STRIPE_HEIGHT 16

#define BYTES_PER_PIXEL 4

# if (BYTES_PER_PIXEL != 4)
/* currently nothing else is supported */
#   error "BYTES_PER_PIXEL bad"
# endif
#ifdef WORDS_BIGENDIAN
#  define RGB_TEXTURE_FORMAT GL_RGBA
#  define YUV_FORMAT         MODE_32_BGR
#  define YUV_SWAP_MODE      1
#else
/* FIXME: check extension */
#  define RGB_TEXTURE_FORMAT GL_BGRA
#  define YUV_FORMAT         MODE_32_RGB
#  define YUV_SWAP_MODE      0
#endif

typedef struct opengl_frame_s {
  vo_frame_t         vo_frame;
  uint8_t	    *chunk[3];

  int                width, height;
  int                ratio_code;
  int                format;
  int                stripe_inc;

  uint8_t           *rgb_dst;
  uint8_t           *texture;
} opengl_frame_t;

typedef struct opengl_driver_s {

  vo_driver_t      vo_driver;

  config_values_t *config;

  opengl_frame_t  *cur_frame;
  vo_overlay_t    *overlay;

  /* X11 / Xv related stuff */
  Display         *display;
  int              screen;
  Drawable         drawable;

  /* OpenGL related */
  volatile GLXContext context;
  volatile int     context_state;        /* is the context ok, or reload? */
  XVisualInfo     *vinfo;
  pthread_t        renderthread;
/*  int              texwidth, texheight; */

  /* size / aspect ratio calculations */
  /* delivered from the decoder */
  int              delivered_width;
  int              delivered_height;
  int              delivered_ratio_code;
  int              delivered_flags;
  double           ratio_factor;	 /* output frame must fulfill: height = width * ratio_factor  */

  /* output area */
  int              window_width;
  int              window_height;
  int              output_width;
  int              output_height;
  int              user_ratio;
  
  /* last displayed frame */
  int              last_frame_width;     /* original size */
  int              last_frame_height;    /* original size */
  int              last_frame_ratio_code;

  /* software yuv2rgb related */
  yuv2rgb_t       *yuv2rgb;
  int              prof_yuv2rgb;
  int              yuv_width;            /* width/height yuv2rgb is configured for */
  int              yuv_stride;

  /* TODO: check */
  int              zoom_mpeg1;

  /* display anatomy */
  double           display_ratio;        /* given by visual parameter from init function */
  void            *user_data;
  /* Current frame texture data */
  char            *texture_data;
  int              texture_width, texture_height;

  /* gui callbacks */

  void (*request_dest_size) (void *user_data,
			     int video_width, int video_height,
			     int *dest_x, int *dest_y, 
			     int *dest_height, int *dest_width);

  void (*calc_dest_size) (void *user_data,
			  int video_width, int video_height, 
			  int *dest_width, int *dest_height);

} opengl_driver_t;

#define CONTEXT_BAD             0
#define CONTEXT_SAME_DRAWABLE   1
#define CONTEXT_SET             2
#define CONTEXT_RELOAD          3


/*
 * first, some utility functions
 */
vo_info_t *get_video_out_plugin_info();

static void *my_malloc_aligned (size_t alignment, size_t size, uint8_t **chunk) {

  uint8_t *pMem;

  pMem = xine_xmalloc (size+alignment);

  *chunk = pMem;

  while ((int) pMem % alignment)
    pMem++;

  return pMem;
}


/*
 * and now, the driver functions
 */

static uint32_t opengl_get_capabilities (vo_driver_t *this_gen) {
  return VO_CAP_COPIES_IMAGE | VO_CAP_YV12 | VO_CAP_YUY2;
}


static void opengl_frame_copy (vo_frame_t *vo_img, uint8_t **src) {
  opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;
  opengl_driver_t *this = (opengl_driver_t *) vo_img->driver;

/*fprintf (stderr, "*** frame_copy\n"); */
  xine_profiler_start_count (this->prof_yuv2rgb);

  if (frame->format == IMGFMT_YV12) {
    this->yuv2rgb->yuv2rgb_fun (this->yuv2rgb, frame->rgb_dst,
				src[0], src[1], src[2]);
  } else {

    this->yuv2rgb->yuy22rgb_fun (this->yuv2rgb, frame->rgb_dst,
				 src[0]);
				 
  }
  
  xine_profiler_stop_count (this->prof_yuv2rgb);

  frame->rgb_dst += frame->stripe_inc; 
}

static void opengl_frame_field (vo_frame_t *vo_img, int which_field) {

  opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;

/*fprintf (stderr, "*** frame_field\n"); */
  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = frame->texture;
    frame->stripe_inc = 2*STRIPE_HEIGHT * frame->width * BYTES_PER_PIXEL;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = frame->texture + frame->width * BYTES_PER_PIXEL ;
    frame->stripe_inc = 2*STRIPE_HEIGHT * frame->width * BYTES_PER_PIXEL;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = frame->texture;
    frame->stripe_inc = STRIPE_HEIGHT * frame->width * BYTES_PER_PIXEL;
    break;
  }
}

static void opengl_frame_dispose (vo_frame_t *vo_img) {

  opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;

/*fprintf (stderr, "*** frame_dispose ***\n"); */
  if (frame)
  {
    free (frame->texture);
    free (frame->chunk[0]);
    free (frame->chunk[1]);
    free (frame->chunk[2]);
  }
  free (frame);
}


static vo_frame_t *opengl_alloc_frame (vo_driver_t *this_gen) {
  opengl_frame_t   *frame ;

/*fprintf (stderr, "*** alloc_frame ***\n"); */
  frame = (opengl_frame_t *) calloc (1, sizeof (opengl_frame_t));
  if (frame==NULL) {
    printf ("opengl_alloc_frame: out of memory\n");
    return NULL;
  }

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions/fields
   */
  
  frame->vo_frame.copy    = opengl_frame_copy;
  frame->vo_frame.field   = opengl_frame_field; 
  frame->vo_frame.dispose = opengl_frame_dispose;
  frame->vo_frame.driver  = this_gen;
  
  return (vo_frame_t *) frame;
}


static void opengl_adapt_to_output_area (opengl_driver_t *this,
					 int dest_width, int dest_height)
{
  this->window_width    = dest_width;
  this->window_height   = dest_height;

  /*
   * make the frames fit into the given destination area
   */
  if ( ((double) dest_width / this->ratio_factor) < dest_height ) {
    this->output_width   = dest_width ;
    this->output_height  = (double) dest_width / this->ratio_factor + .5;
  } else {
    this->output_width   = (double) dest_height * this->ratio_factor + .5;
    this->output_height  = dest_height;
  }

  /* Force reload / state reinitialization / clear */
  if (this->context_state == CONTEXT_SET)
    this->context_state = CONTEXT_RELOAD;
}


static void opengl_calc_format (opengl_driver_t *this,
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

  switch (this->user_ratio) {
  case ASPECT_AUTO:
    switch (ratio_code) {
    case XINE_ASPECT_RATIO_ANAMORPHIC:  /* anamorphic     */
        desired_ratio = 16.0 /9.0;
        break;
    case XINE_ASPECT_RATIO_211_1:       /* 2.11:1 */
        desired_ratio = 2.11/1.0;
        break;
    case XINE_ASPECT_RATIO_SQUARE:      /* square pels */
    case XINE_ASPECT_RATIO_DONT_TOUCH:  /* probably non-mpeg stream => don't tou
ch aspect ratio */
        desired_ratio = image_ratio;
        break;
    case 0:                             /* forbidden -> 4:3 */
        printf ("video_out_opengl: invalid ratio, using 4:3\n");
    default:
        printf ("video_out_opengl: unknown aspect ratio (%d) in stream => using 
4:3\n",
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

  /*
   * calc ideal output frame size
   */

  corr_factor = this->ratio_factor / image_ratio ;

  if (fabs(corr_factor - 1.0) < 0.005) {
    ideal_width  = this->delivered_width;
    ideal_height = this->delivered_height;
  }
  else if (corr_factor >= 1.0) {
    ideal_width  = this->delivered_width * corr_factor + 0.5;
    ideal_height = this->delivered_height;
  }
  else {
    ideal_width  = this->delivered_width;
    ideal_height = this->delivered_height / corr_factor + 0.5;
  }

  /* little hack to zoom mpeg1 / other small streams  by default*/
  if (this->zoom_mpeg1 && (this->delivered_width<400)) {
    ideal_width  *= 2;
    ideal_height *= 2;
  }

  /* yuv2rgb_mmx prefers "width%8 == 0" */
  /* but don't change if it would introduce scaling */
  if( ideal_width != this->delivered_width ||
      ideal_height != this->delivered_height )
    ideal_width &= ~7;

  /*
   * ask gui to adapt to this size
   */
  this->request_dest_size (this->user_data,
                           ideal_width, ideal_height,
                           &dest_x, &dest_y, &dest_width, &dest_height);
  DEBUGF ((stderr, "*** request_dest_size %dx%d -> %dx%d\n",
           ideal_width, ideal_height, dest_width, dest_height));

  opengl_adapt_to_output_area (this, dest_width, dest_height);
}


static void opengl_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags) {

  opengl_driver_t  *this = (opengl_driver_t *) this_gen;
  opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;
  int setup_yuv = 0;

/*fprintf (stderr, "*** update_frame_format ***\n"); */
  flags &= VO_BOTH_FIELDS;

  if ((width != this->delivered_width)
      || (height != this->delivered_height)
      || (ratio_code != this->delivered_ratio_code)
      || (flags != this->delivered_flags)) {

    this->delivered_width      = width;
    this->delivered_height     = height;
    this->delivered_ratio_code = ratio_code;
    this->delivered_flags      = flags;
    setup_yuv = 1;
  }

  if ((frame->width != width) || (frame->height != height)
      || (frame->format != format)) {

    int image_size;

    DEBUGF ((stderr, "*** updating frame to %d x %d\n", width, height));

    XLockDisplay (this->display);

    free (frame->texture);
    free (frame->chunk[0]);
    free (frame->chunk[1]);
    free (frame->chunk[2]);

    frame->texture = calloc (1, BYTES_PER_PIXEL * width * height);
    assert (frame->texture);


    if (format == IMGFMT_YV12) {
      image_size = width * height;
      frame->vo_frame.base[0] = my_malloc_aligned(16,image_size, &frame->chunk[0]);
      frame->vo_frame.base[1] = my_malloc_aligned(16,image_size/4, &frame->chunk[1]);
      frame->vo_frame.base[2] = my_malloc_aligned(16,image_size/4, &frame->chunk[2]);
    } else {
      image_size = width * height;
      frame->vo_frame.base[0] = my_malloc_aligned(16,image_size*2, &frame->chunk[0]);
    }
    
    frame->format = format;
    frame->width  = width;
    frame->height = height;

    XUnlockDisplay (this->display);
  }
  frame->ratio_code = ratio_code;

  opengl_frame_field ((vo_frame_t *)frame, flags);

  if (flags == VO_BOTH_FIELDS) {
    if (this->yuv_stride != frame->width * BYTES_PER_PIXEL)
	setup_yuv = 1;
  } else {	/* VO_TOP_FIELD, VO_BOTTOM_FIELD */
      if (this->yuv_stride != (frame->width * BYTES_PER_PIXEL * 2))
	setup_yuv = 1;
  }

  if (setup_yuv || (this->yuv_width != width)) {
    switch (flags) {
    case VO_TOP_FIELD:
    case VO_BOTTOM_FIELD:
	yuv2rgb_setup (this->yuv2rgb,
		       this->delivered_width,
		       16,
		       this->delivered_width*2,
		       this->delivered_width,
		       width,
		       STRIPE_HEIGHT,
		       width * BYTES_PER_PIXEL * 2);
	this->yuv_stride = frame->width * BYTES_PER_PIXEL * 2;
	break;
    case VO_BOTH_FIELDS:
	yuv2rgb_setup (this->yuv2rgb,
		       this->delivered_width,
		       16,
		       this->delivered_width,
		       this->delivered_width/2,
		       width,
		       STRIPE_HEIGHT,
		       width * BYTES_PER_PIXEL);
	this->yuv_stride = frame->width * BYTES_PER_PIXEL;
	break;
    }
    this->yuv_width  = width;
  }
}

static void opengl_overlay_clut_yuv2rgb(opengl_driver_t  *this, vo_overlay_t *overlay)
{
  int i;
  clut_t* clut = (clut_t*) overlay->color;
  if (!overlay->rgb_clut) {
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
                   this->yuv2rgb->yuv2rgb_single_pixel_fun(this->yuv2rgb,
                   clut[i].y, clut[i].cb, clut[i].cr);
    }
  overlay->rgb_clut++;
  }
  if (!overlay->clip_rgb_clut) {
    clut = (clut_t*) overlay->clip_color;
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
                   this->yuv2rgb->yuv2rgb_single_pixel_fun(this->yuv2rgb,
                   clut[i].y, clut[i].cb, clut[i].cr);
    }
  overlay->clip_rgb_clut++;
  }

}

static void opengl_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  opengl_driver_t  *this = (opengl_driver_t *) this_gen;
  opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;

  /* Alpha Blend here */
  if (overlay->rle) {
    if( !overlay->rgb_clut || !overlay->clip_rgb_clut)
      opengl_overlay_clut_yuv2rgb(this,overlay);

    assert (this->delivered_width == frame->width);
    assert (this->delivered_height == frame->height);
#   if BYTES_PER_PIXEL == 3
      blend_rgb24 ((uint8_t *)frame->texture, overlay,
                   frame->width, frame->height,
                   this->delivered_width, this->delivered_height);
#   elif BYTES_PER_PIXEL == 4
      blend_rgb32 ((uint8_t *)frame->texture, overlay,
                   frame->width, frame->height,
                   this->delivered_width, this->delivered_height);
#   else
#     error "bad BYTES_PER_PIXEL"
#   endif
  }
}

static void opengl_render_image (opengl_driver_t *this, opengl_frame_t *frame,
                                 GLXContext ctx)
{
  int		  xoffset;
  int		  yoffset;
  pthread_t       self = pthread_self ();

  DEBUGF ((stderr, "*** render_image %p frame %p %dx%d ctx %p%s\n",
           this, frame, frame->width, frame->height, ctx,
	   self != this->renderthread ?                   " THREAD"         :
	   ctx && ctx != this->context ?                  " gui"            :
	   this->context_state == CONTEXT_SET ?
	          ctx ? " set, but reload anyway" : " set"                  :
	   this->context_state == CONTEXT_RELOAD ?        " reload"         :
	   this->context_state == CONTEXT_SAME_DRAWABLE ? " DESTROY+CREATE" :
	                                                  " CREATE" ));

  if (((ctx == this->context || ! ctx) &&
       (this->context_state == CONTEXT_BAD ||
        this->context_state == CONTEXT_SAME_DRAWABLE)) ||
      (self != this->renderthread))
  {
    assert (this->vinfo);
    if ((this->context_state == CONTEXT_SAME_DRAWABLE) && (self == this->renderthread))
    {
/*fprintf (stderr, "destroy: %p\n", this->context); */
      /* Unfortunately for _BAD the drawable is already destroyed.
       * This cannot be resolved right now and may be a memory leak. */
      if (this->context)
        glXDestroyContext (this->display, this->context);
    }
/*fprintf (stderr, "create\n"); */
    ctx = glXCreateContext (this->display, this->vinfo, NULL, True);
    assert (ctx);
    this->context = ctx;
    this->context_state = CONTEXT_RELOAD;
    this->renderthread  = self;
  }

  if (this->context_state == CONTEXT_RELOAD && ! ctx)
    ctx = this->context;

  if (ctx)
  {
   int i;

/*fprintf (stderr, "set context %p\n", ctx); */
    /* Set and initialize context */
    if (! glXMakeCurrent (this->display, this->drawable, ctx))
    {
      fprintf (stderr, "video_out_opengl: no OpenGL support available (glXMakeCurrent)\n");
      exit (1);
    }
/*fprintf (stderr, "set context done\n"); */
    if (ctx == this->context)
      this->context_state = CONTEXT_SET;
    else if (this->context_state == CONTEXT_SET ||
	     this->context_state == CONTEXT_RELOAD)
      this->context_state = CONTEXT_RELOAD;
    /* glViewport (0, 0, this->window_width, this->window_height);
    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();
    glOrtho (0, this->window_width, this->window_height, 0, -1, 1);
    glMatrixMode (GL_MODELVIEW);
    glLoadIdentity ();
    glDisable    (GL_BLEND);
    glDisable    (GL_DEPTH_TEST);
    glDisable    (GL_CULL_FACE);
    glEnable     (GL_TEXTURE_2D);
    glDepthMask  (GL_FALSE);
    glClearColor (0, 0, 0, 0);
    */
    glViewport (0, 0, this->window_width, 
		this->window_height);
    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();
    
    gluPerspective (45.0f, 
		    (GLfloat)(this->window_width)/
		    (GLfloat)(this->window_height),
		    1.0f, 1000.0f);

    glMatrixMode (GL_MODELVIEW);
    glLoadIdentity ();	
	
    glClearColor (0.0f, 0.0f, 0.0f, 0.5f);
    glClearDepth (1.0f);
    glDepthFunc (GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glShadeModel (GL_FLAT);
    glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

    glEnable(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

    this->texture_width = 1;
    i = frame->width;
    while(i > 0) {
      i = i>>1;
      this->texture_width = (this->texture_width) << 1;
    }
    
    this->texture_height = 1;
    i = frame->height;
    while(i > 0) {
      i = i>>1;
      this->texture_height = (this->texture_height) << 1;
    }
    
    if(this->texture_data) {
      free(this->texture_data);
    }
   
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glScalef((float)(frame->width)/(float)(this->texture_width),
	     (float)(frame->height)/(float)(this->texture_height),
	     1.0f);
    glMatrixMode(GL_MODELVIEW);

    this->texture_data = malloc(this->texture_width * this->texture_height * 3);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 
		 this->texture_width, this->texture_height,
		 0, RGB_TEXTURE_FORMAT, GL_UNSIGNED_BYTE,
		 this->texture_data);

    /* for fullscreen and misratioed modes, clear unused areas of old
     * video area */
    if (this->window_width != this->output_width
	|| this->window_height != this->output_height)
      glClear    (GL_COLOR_BUFFER_BIT);
  }

  if (frame)
  {
   
    xoffset = (this->window_width - this->output_width) / 2;
    yoffset = (this->window_height - this->output_height) / 2;
    if (xoffset < 0)
      xoffset = 0;
    if (yoffset < 0)
      yoffset = 0;

/*fprintf (stderr, "render %p %p: +%d+%d\n", frame, ctx, xoffset, yoffset); */
    
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
		    (GLsizei)(frame->width),
		    (GLsizei)(frame->height),
		    RGB_TEXTURE_FORMAT, GL_UNSIGNED_BYTE,
		    frame->texture);
    glClear (GL_DEPTH_BUFFER_BIT);	
    glLoadIdentity();	
    glBegin(GL_QUADS);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 11.0f,  -8.3f, -20.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-11.0f,  -8.3f, -20.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-11.0f, 8.3f, -20.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 11.0f, 8.3f, -20.0f);
    glEnd();	

#ifdef SPHERE
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);	

    glTranslatef(0.0f, 0.0f, -10.0f);
    glutSolidSphere(3.0f, 20, 10);

    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
#endif

    /*
    glPixelZoom (((float)this->output_width)    / frame->width,
                 - ((float)this->output_height) / frame->height);
    glRasterPos2i (xoffset, yoffset);
    glDrawPixels (frame->width, frame->height, RGB_TEXTURE_FORMAT,
                  GL_UNSIGNED_BYTE, frame->texture);
    */
/*fprintf (stderr, "render done\n"); */
  }
  glFlush ();
  /* Note: no glFinish() - work concurrently to the graphics pipe */
}


static void opengl_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  opengl_driver_t  *this = (opengl_driver_t *) this_gen;
  opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;

/*fprintf (stderr, "*** display_frame ***\n"); */
  if( this->cur_frame )
    this->cur_frame->vo_frame.displayed (&this->cur_frame->vo_frame);
  this->cur_frame = frame;

  if ( (frame->width      != this->last_frame_width)  ||
       (frame->height     != this->last_frame_height) ||
       (frame->ratio_code != this->delivered_ratio_code) )
  {
    opengl_calc_format (this, frame->width, frame->height, frame->ratio_code);
    this->last_frame_width      = frame->width;
    this->last_frame_height     = frame->height;
    this->last_frame_ratio_code = frame->ratio_code;

    printf ("video_out_opengl: window size %d x %d, frame size %d x %d\n",
            this->window_width, this->window_height,
            this->output_width, this->output_height);
  }

  XLockDisplay (this->display);
  opengl_render_image (this, frame, NULL);
  XUnlockDisplay (this->display);

  /* Theoretically, the frame data is not used immedeately, and the
   * graphics system might address altered data - but only if we
   * are faster than the graphics hardware... */
  /* If this doesn't work, remove the following two lines */
  /* Note: We cannot do expose events, when the frame is deleted. */
  /* Note: We cannot do expose events anyway right now (errors with
   * multiple threads rendering in many OpenGL implementations) */
#if 1
  frame->vo_frame.displayed (&frame->vo_frame);
  this->cur_frame = NULL;
#endif
/*fprintf (stderr, "done display_frame\n"); */
}


static int opengl_get_property (vo_driver_t *this_gen, int property) {

  opengl_driver_t *this = (opengl_driver_t *) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    return this->user_ratio ;
  } else if ( property == VO_PROP_BRIGHTNESS) {
    return yuv2rgb_get_gamma(this->yuv2rgb);
  } else {
    printf ("video_out_opengl: tried to get unsupported property %d\n", property);
  }

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

static int opengl_set_property (vo_driver_t *this_gen, 
			      int property, int value) {

  opengl_driver_t *this = (opengl_driver_t *) this_gen;

  if ( property == VO_PROP_ASPECT_RATIO) {
    if (value>=NUM_ASPECT_RATIOS)
      value = ASPECT_AUTO;
    this->user_ratio = value;
    opengl_calc_format (this, this->delivered_width, this->delivered_height,
	                this->delivered_ratio_code);
    printf("video_out_opengl: aspect ratio changed to %s\n",
	   aspect_ratio_name(value));
#if 0
  } else if ( property == VO_PROP_BRIGHTNESS) {
    yuv2rgb_set_gamma(this->yuv2rgb,value);

    printf("video_out_opengl: gamma changed to %d\n",value);
#endif
  } else {
    printf ("video_out_opengl: tried to set unsupported property %d\n", property);
  }

  return value;
}

static void opengl_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

#if 0
  /* opengl_driver_t *this = (opengl_driver_t *) this_gen;  */
  if ( property == VO_PROP_BRIGHTNESS) {
    *min = -100;
    *max = +100;
  } else {
#endif
    *min = 0;
    *max = 0;
#if 0
  }
#endif
}


static int is_fullscreen_size (opengl_driver_t *this, int w, int h)
{
  return w == DisplayWidth(this->display, this->screen)
      && h == DisplayHeight(this->display, this->screen);
}

static void opengl_translate_gui2video(opengl_driver_t *this,
				     int x, int y,
				     int *vid_x, int *vid_y)
{
/*fprintf (stderr, "*** translate_gui2video ***\n"); */
  if (this->output_width > 0 && this->output_height > 0) {
    /*
     * the driver may center a small output area inside a larger
     * gui area.  This is the case in fullscreen mode, where we often
     * have black borders on the top/bottom/left/right side.
     */
    /* Fixme: not true. Simplify? */
    x -= (this->window_width  - this->output_width)  >> 1;
    y -= (this->window_height - this->output_height) >> 1;

    /*
     * the driver scales the delivered area into an output area.
     * translate output area coordianates into the delivered area
     * coordiantes.
     */
    x = x * this->delivered_width  / this->output_width;
    y = y * this->delivered_height / this->output_height;
  }

  *vid_x = x;
  *vid_y = y;
}

static int opengl_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data) {

  opengl_driver_t   *this = (opengl_driver_t *) this_gen;
  x11_rectangle_t *area;
  static int glxAttrib[] = {
    GLX_RGBA, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1, None
  } ;

/*fprintf (stderr, "*** gui_data_exchange ***\n"); */

  switch (data_type) {
  case GUI_SELECT_VISUAL:
fprintf (stderr, "*** gui_select_visual ***\n");
    XLockDisplay (this->display);
    this->vinfo = glXChooseVisual (this->display, this->screen, glxAttrib);
    XUnlockDisplay (this->display);
    if (this->vinfo == NULL)
      fprintf (stderr, "video_out_opengl: no OpenGL support available (glXChooseVisual)\n");
    *(XVisualInfo**)data = this->vinfo;
/*fprintf (stderr, "*** visual %p depth %d\n", this->vinfo->visual, this->vinfo->depth); */
    break;
      
  case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:
/*fprintf (stderr, "*** gui_dest_pos_size_changed ***\n"); */

    area = (x11_rectangle_t *) data;

    if (this->window_width != area->w || this->window_height != area->h)
    {
      XLockDisplay (this->display);
      DEBUGF ((stderr, "*** video window size changed from %d x %d to %d x %d\n",
	       this->window_width, this->window_height,
	       area->w, area->h));
      opengl_adapt_to_output_area (this, area->w, area->h);
      XUnlockDisplay (this->display);
    }
    break;
// ????? What's this?
#if 0
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
#endif

  case GUI_DATA_EX_EXPOSE_EVENT:
/*fprintf (stderr, "*** gui_expose ***\n"); */
    
  /* FIXME: called for EVERY SINGLE expose event (no peek so far) */
  if (this->cur_frame) {

    XExposeEvent * xev = (XExposeEvent *) data;

    if (xev->count == 0) {
      /* Note that the global GLX context is not available on all
       * architectures in this thread */
      /* Note that using different contextes in different threads for the
       * same drawable seems to be broken with several OpenGL implementations */
      /* Thus this is currently disabled */
#if 0
      GLXContext ctx;
      XLockDisplay (this->display);
// FIXME: this does not work - at least on linux...
fprintf (stderr, "create/gui\n");
assert (this->vinfo);
ctx = glXCreateContext (this->display, this->vinfo, NULL, True);
assert (ctx);
opengl_render_image (this, this->cur_frame, ctx);
glXMakeCurrent (this->display, None, NULL);
glXDestroyContext (this->display, ctx);
XUnlockDisplay (this->display);
#endif
    }
  }
      if (this->context_state == CONTEXT_SET || this->context_state == CONTEXT_RELOAD)
        this->context_state = CONTEXT_RELOAD;
  break;

  case GUI_DATA_EX_DRAWABLE_CHANGED:
    DEBUGF ((stderr, "*** gui_drawable_changed: %ld\n", (Drawable) data));
    XLockDisplay (this->display);
    /* Unfortunately, the last drawable is already gone, so we cannot destroy
     * the former context. This is a memory leak. Unfortunately. */
    /* Even if the drawable remains the same, this does not seem to work :( */
#if 0
    if (this->drawable == (Drawable) data)
      this->context_state = CONTEXT_SAME_DRAWABLE;
    else
      this->context_state = CONTEXT_BAD;
#else
    this->context_state = CONTEXT_BAD;
#endif
    if (this->drawable == (Drawable) data)
      DEBUGF ((stderr, "*** drawable changed, state now bad\n"));
    this->last_frame_width = this->last_frame_height = 0;
    this->drawable = (Drawable) data;
    XUnlockDisplay (this->display);
    break;

  case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO:
/*fprintf (stderr, "*** gui_translate_gui_to_video ***\n"); */
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      opengl_translate_gui2video(this, rect->x, rect->y,
			       &x1, &y1);
      opengl_translate_gui2video(this, rect->x + rect->w, rect->y + rect->h,
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

/*fprintf (stderr, "done gui_data_exchange\n"); */
  return 0;
}


static void opengl_exit (vo_driver_t *this_gen) {
  opengl_driver_t *this = (opengl_driver_t *) this_gen;
  glXMakeCurrent (this->display, None, NULL);
  glXDestroyContext (this->display, this->context);
  this->context = NULL;
}


vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen) {

  opengl_driver_t      *this;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  Display              *display = NULL;

  visual = (x11_visual_t *) visual_gen;
  display = visual->display;

  fprintf (stderr, "EXPERIMENTAL opengl output plugin\n");
  /*
   * allocate plugin struct
   */

  this = malloc (sizeof (opengl_driver_t));

  if (!this) {
    printf ("video_out_opengl: malloc failed\n");
    return NULL;
  }

  memset (this, 0, sizeof(opengl_driver_t));

  this->config		    = config;
  this->display		    = visual->display;
  this->screen		    = visual->screen;
  this->display_ratio	    = visual->display_ratio;
  this->request_dest_size   = visual->request_dest_size;
  this->calc_dest_size	    = visual->calc_dest_size;
  this->user_data           = visual->user_data;
  this->output_width	    = 0;
  this->output_height	    = 0;
  this->window_width	    = 0;
  this->window_height	    = 0;
  this->zoom_mpeg1	    = config->register_bool (config, "video.zoom_mpeg1", 1,
						     "Zoom small video formats to double size",
						     NULL, NULL, NULL);
  this->texture_data        = NULL;
  this->texture_width       = 0;
  this->texture_height      = 0;
  this->drawable	    = None;      /* We need a different one with a dedicated visual anyway */
  this->context_state       = CONTEXT_BAD;

  this->prof_yuv2rgb	    = xine_profiler_allocate_slot ("xshm yuv2rgb convert");

  this->vo_driver.get_capabilities     = opengl_get_capabilities;
  this->vo_driver.alloc_frame          = opengl_alloc_frame;
  this->vo_driver.update_frame_format  = opengl_update_frame_format;
  this->vo_driver.overlay_blend        = opengl_overlay_blend;
  this->vo_driver.display_frame        = opengl_display_frame;
  this->vo_driver.get_property         = opengl_get_property;
  this->vo_driver.set_property         = opengl_set_property;
  this->vo_driver.get_property_min_max = opengl_get_property_min_max;
  this->vo_driver.gui_data_exchange    = opengl_gui_data_exchange;
  this->vo_driver.exit                 = opengl_exit;
  this->vo_driver.get_info             = get_video_out_plugin_info;

  this->yuv2rgb = yuv2rgb_init (YUV_FORMAT, YUV_SWAP_MODE, NULL);

  yuv2rgb_set_gamma(this->yuv2rgb, config->register_range (config, "video.opengl_gamma", 0,
							   -100, 100, "(software) gamma correction for OpenGL driver",
							   NULL, NULL, NULL));
  return &this->vo_driver;
}

static vo_info_t vo_info_shm = {
  3,
  "OpenGL",
  "xine video output plugin using the MIT X shared memory extension",
  VISUAL_TYPE_X11,
  8
};

vo_info_t *get_video_out_plugin_info() {
  return &vo_info_shm;
}

