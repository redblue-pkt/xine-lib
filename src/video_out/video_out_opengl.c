/*
 * Copyright (C) 2000-2003 the xine project
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
 * $Id: video_out_opengl.c,v 1.42 2004/11/23 15:01:23 mshopf Exp $
 * 
 * video_out_opengl.c, OpenGL based interface for xine
 *
 * Written by Matthias Hopf <mat@mshopf.de>,
 * based on the xshm and xv video output plugins.
 * 
 */

/* #define LOG */
#define LOG_MODULE "video_out_opengl"


#define BYTES_PER_PIXEL      4
#define NUM_FRAMES_BACKLOG   4	/* Allow thread some time to render frames */

#define SECONDS_PER_CYCLE    60	/* Animation parameters */
#define CYCLE_FACTOR1        3
#define CYCLE_FACTOR2        5

#ifdef NDEBUG
#define CHECKERR(a) ((void)0)
#else
#define CHECKERR(a) do { int i = glGetError (); if (i != GL_NO_ERROR) fprintf (stderr, "   *** %s: 0x%x = %s\n", a, i, gluErrorString (i)); } while (0)
#endif


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <pthread.h>

#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glu.h>

#include "xine.h"
#include "video_out.h"

#include "xine_internal.h"
#include "alphablend.h"
#include "yuv2rgb.h"
#include "xineutils.h"
#include "x11osd.h"


#if (BYTES_PER_PIXEL != 4)
/* currently nothing else is supported */
#   error "BYTES_PER_PIXEL bad"
#endif
/* TODO: haven't checked bigendian so far... */
#ifdef WORDS_BIGENDIAN
#  define RGB_TEXTURE_FORMAT GL_RGBA
#  define YUV_FORMAT         MODE_32_BGR
#  define YUV_SWAP_MODE      1
#else
/* TODO: GL_BGRA needs extension check */
/* TODO: GL_RGBA / MODE_32_BGR does not need extension, but might be slower
 * on ix86, and overlays would use wrong pixel order */
/* TODO:  MODE_32_BGR may not work with some accelerated yuv2rgb routines */
#  define RGB_TEXTURE_FORMAT GL_BGRA
#  define YUV_FORMAT         MODE_32_RGB
#  define YUV_SWAP_MODE      0
#endif

#define MY_2PI               6.2831853

typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, format, flags;
  double             ratio;

  uint8_t           *chunk[4]; /* mem alloc by xmalloc_aligned           */
  uint8_t           *rgb, *rgb_dst;
    
  yuv2rgb_t         *yuv2rgb; /* yuv2rgb converter set up for this frame */

} opengl_frame_t;


/* RENDER_DRAW to RENDER_SETUP are asynchronous actions, but later actions
 * imply former actions -> only check '>' on update */
/* RENDER_CREATE and later are synchronous actions and override async ones */
enum render_e { RENDER_NONE=0, RENDER_DRAW, RENDER_CLEAN, RENDER_SETUP,
		RENDER_CREATE, RENDER_VISUAL, RENDER_RELEASE, RENDER_EXIT };

typedef struct {

  vo_driver_t        vo_driver;
  vo_scale_t         sc;

  /* X11 related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;

  /* Render thread */
  pthread_t          render_thread;
  enum render_e      render_action;
  int                render_frame_changed;
  pthread_mutex_t    render_action_mutex;
  pthread_cond_t     render_action_cond;
  pthread_cond_t     render_return_cond;
  int                last_width, last_height;

  /* Render parameters */
  int                render_fun_id;
  int                render_min_fps;
  int                render_double_buffer;
  int                gui_width, gui_height;

  /* OpenGL state */
  GLXContext         context;
  XVisualInfo       *vinfo;
  int                tex_width, tex_height; /* independend of frame */

  int                yuv2rgb_brightness;
  int                yuv2rgb_contrast;
  int                yuv2rgb_saturation;
  uint8_t           *yuv2rgb_cmap;
  yuv2rgb_factory_t *yuv2rgb_factory;

  /* Frame state */
  opengl_frame_t    *frame[NUM_FRAMES_BACKLOG];
  x11osd            *xoverlay;
  int                ovl_changed;

  xine_t            *xine;
} opengl_driver_t;

typedef struct {
  video_driver_class_t driver_class;
  config_values_t     *config;
  xine_t              *xine;
} opengl_class_t;

typedef void *(*thread_run_t)(void *);


/*
 * Render functions
 */

/* Static 2d texture based display */
static void render_tex2d (opengl_driver_t *this, opengl_frame_t *frame) {
  int             x1, x2, y1, y2;
  float           tx, ty;

  /* Calc texture/rectangle coords */
  x1 = this->sc.output_xoffset;
  y1 = this->sc.output_yoffset;
  x2 = x1 + this->sc.output_width;
  y2 = y1 + this->sc.output_height;
  tx = (float) frame->width  / this->tex_width;
  ty = (float) frame->height / this->tex_height;
  /* Draw quad */
  glBegin (GL_QUADS);
  glTexCoord2f (tx, ty);   glVertex2i   (x2, y2);
  glTexCoord2f (0,  ty);   glVertex2i   (x1, y2);
  glTexCoord2f (0,  0);    glVertex2i   (x1, y1);
  glTexCoord2f (tx, 0);    glVertex2i   (x2, y1);
  glEnd ();
}

/* Static image pipline based display */
static void render_draw (opengl_driver_t *this, opengl_frame_t *frame) {
  glPixelZoom   (((float)this->sc.output_width)    / frame->width,
		 - ((float)this->sc.output_height) / frame->height);
  glRasterPos2i (this->sc.output_xoffset, this->sc.output_yoffset);
  glDrawPixels  (frame->width, frame->height, RGB_TEXTURE_FORMAT,
		 GL_UNSIGNED_BYTE, frame->rgb);
}

/* Animated spinning cylinder */
#define CYL_TESSELATION    128
#define CYL_WIDTH          2.5
#define CYL_HEIGHT         3.0
static void render_cyl (opengl_driver_t *this, opengl_frame_t *frame) {
  int             i;
  float           off;
  float           tx, ty;
  struct timeval  curtime;
  
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  /* Calc timing + texture coords */
  gettimeofday (&curtime, NULL);
  off = ((curtime.tv_sec % SECONDS_PER_CYCLE) + curtime.tv_usec * 1e-6)
    * (360.0 / SECONDS_PER_CYCLE);
  tx = (float) frame->width  / this->tex_width;
  ty = (float) frame->height / this->tex_height;

  /* Spin it */
  glMatrixMode   (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef   (0, 0, -10);
  glRotatef      (off * CYCLE_FACTOR1, 1, 0, 0);  
  glRotatef      (off,                 0, 0, 1); 
  glRotatef      (off * CYCLE_FACTOR2, 0, 1, 0);  
 
  /* Note that this is not aspect ratio corrected */
  glBegin (GL_QUADS);
  for (i = 0; i < CYL_TESSELATION; i++) {
    float x1 = CYL_WIDTH * sin (i     * MY_2PI / CYL_TESSELATION);
    float x2 = CYL_WIDTH * sin ((i+1) * MY_2PI / CYL_TESSELATION);
    float z1 = CYL_WIDTH * cos (i     * MY_2PI / CYL_TESSELATION);
    float z2 = CYL_WIDTH * cos ((i+1) * MY_2PI / CYL_TESSELATION);
    float tx1 = tx * i / CYL_TESSELATION;
    float tx2 = tx * (i+1) / CYL_TESSELATION;
    glTexCoord2f (tx1, 0);     glVertex3f (x1, CYL_HEIGHT, z1);
    glTexCoord2f (tx2, 0);     glVertex3f (x2, CYL_HEIGHT, z2);
    glTexCoord2f (tx2, ty);    glVertex3f (x2, -CYL_HEIGHT, z2);
    glTexCoord2f (tx1, ty);    glVertex3f (x1, -CYL_HEIGHT, z1);
  }
  glEnd ();
}

/* Animated spinning environment mapped torus */
#define DIST_FACTOR   16.568542  	/* 2 * (sqrt(2)-1) * 20 */
static void render_env_tor (opengl_driver_t *this, opengl_frame_t *frame) {
  float           off;
  float           x1, y1, x2, y2, tx, ty;
  struct timeval  curtime;

  /* No glClear() necessary - rendering background w/ depth test success */

  /* Calc timing + texture coords */
  gettimeofday (&curtime, NULL);
  off = ((curtime.tv_sec % SECONDS_PER_CYCLE) + curtime.tv_usec * 1e-6)
    * (360.0 / SECONDS_PER_CYCLE);
  /* Fovy is angle in y direction */
  x1 = (this->sc.output_xoffset - this->gui_width/2.0)
    * DIST_FACTOR / this->gui_height;
  x2 = (this->sc.output_xoffset+this->sc.output_width - this->gui_width/2.0)
    * DIST_FACTOR / this->gui_height;
  y1 = (this->sc.output_yoffset - this->gui_height/2.0)
    * DIST_FACTOR / this->gui_height;
  y2 = (this->sc.output_yoffset+this->sc.output_height - this->gui_height/2.0)
    * DIST_FACTOR / this->gui_height;

  tx = (float) frame->width  / this->tex_width;
  ty = (float) frame->height / this->tex_height;

  glMatrixMode   (GL_MODELVIEW);
  glLoadIdentity ();

  /* Draw background, Y swapped */
  glMatrixMode   (GL_TEXTURE);
  glPushMatrix   ();
  glLoadIdentity ();
  glDepthFunc    (GL_ALWAYS);
  
  glBegin        (GL_QUADS);
  glColor3f      (1, 1, 1);
  glTexCoord2f   (tx, 0);     glVertex3f   (x2, y2, -20);
  glTexCoord2f   (0,  0);     glVertex3f   (x1, y2, -20);
  glTexCoord2f   (0,  ty);    glVertex3f   (x1, y1, -20);
  glTexCoord2f   (tx, ty);    glVertex3f   (x2, y1, -20);
  glEnd          ();
  
  glPopMatrix    ();
  glDepthFunc    (GL_LEQUAL);

  /* Spin it */
  glMatrixMode   (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef   (0, 0, -10);
  glRotatef      (off * CYCLE_FACTOR1, 1, 0, 0);
  glRotatef      (off,                 0, 0, 1);
  glRotatef      (off * CYCLE_FACTOR2, 0, 1, 0);
  glEnable       (GL_TEXTURE_GEN_S);
  glEnable       (GL_TEXTURE_GEN_T);
  glColor3f      (1, 0.8, 0.6);
  glCallList     (1);
  glDisable      (GL_TEXTURE_GEN_S);
  glDisable      (GL_TEXTURE_GEN_T);
}

/*
 * Image setup functions
 */
static void render_image_nop (opengl_driver_t *this, opengl_frame_t *frame) {
}

static void render_image_tex (opengl_driver_t *this, opengl_frame_t *frame) {
  int tex_w, tex_h;

  /* check necessary texture size and allocate */
  if (frame->width != this->last_width ||
      frame->height != this->last_height ||
      ! this->tex_width || ! this->tex_height) {
    tex_w = tex_h = 16;
    while (tex_w < frame->width)
      tex_w <<= 1;
    while (tex_h < frame->height)
      tex_h <<= 1;
    
    if (tex_w != this->tex_width || tex_h != this->tex_height) {
      char *tmp = malloc (tex_w * tex_h * BYTES_PER_PIXEL);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, tex_w, tex_h,
		    0, RGB_TEXTURE_FORMAT, GL_UNSIGNED_BYTE, tmp);
      CHECKERR ("texImage");
      free (tmp);
      this->tex_width  = tex_w;
      this->tex_height = tex_h;
      lprintf ("* new texsize: %dx%d\n", tex_w, tex_h);
    }	
    this->last_width  = frame->width;
    this->last_height = frame->height;
  }
  /* Load texture */
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
		   (GLsizei)(frame->width),
		   (GLsizei)(frame->height),
		   RGB_TEXTURE_FORMAT, GL_UNSIGNED_BYTE,
		   frame->rgb);
  CHECKERR ("texsubimage");
}

static void render_image_envtex (opengl_driver_t *this, opengl_frame_t *frame) {
  static float mTex[] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

  /* update texture matrix if frame size changed */
  if (frame->width != this->last_width ||
      frame->height != this->last_height ||
      ! this->tex_width || ! this->tex_height) {
    render_image_tex (this, frame);
    /* Texture matrix has to skale/shift tex origin + swap y coords */
    mTex[0]  =   1.0 * frame->width  / this->tex_width;
    mTex[5]  =  -1.0 * frame->height / this->tex_height;
    mTex[12] = (-2.0 * mTex[0]) / mTex[0];
    mTex[13] =  -mTex[5];
    glMatrixMode  (GL_TEXTURE);
    glLoadMatrixf (mTex);
  } else {
    render_image_tex (this, frame);
  }
}


/*
 * Render setup functions
 */
static void render_help_setup_tex (opengl_driver_t *this) {
  CHECKERR ("pre-tex_setup");
  glEnable        (GL_TEXTURE_2D);
  glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexEnvi       (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,   GL_REPLACE);
  glMatrixMode    (GL_TEXTURE);
  glLoadIdentity  ();
  CHECKERR ("post-tex_setup");
}

static void render_setup_2d (opengl_driver_t *this) {
  CHECKERR ("pre-frustum_setup");
  glViewport   (0, 0, this->gui_width, this->gui_height);
  glDepthRange (-1, 1);
  glClearColor (0, 0, 0, 0);
  glClearDepth (1.0f);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho      (0, this->gui_width, this->gui_height, 0, -1, 1);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glDisable    (GL_BLEND);
  glDisable    (GL_DEPTH_TEST);
  glDepthMask  (GL_FALSE);
  glDisable    (GL_CULL_FACE);
  glShadeModel (GL_FLAT);
  glDisable    (GL_TEXTURE_2D);
  glHint       (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
  CHECKERR ("post-frustum_setup");
}

static void render_setup_tex2d (opengl_driver_t *this) {
  render_setup_2d (this);
  render_help_setup_tex (this);
}

static void render_setup_3d (opengl_driver_t *this) {
  CHECKERR ("pre-3dfrustum_setup");
  glViewport   (0, 0, this->gui_width, this->gui_height);
  glDepthRange (0, 1);
  glClearColor (0, 0, 0, 0);
  glClearDepth (1.0f);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  gluPerspective  (45.0f,
		   (GLfloat)(this->gui_width) / (GLfloat)(this->gui_height),
		   1.0f, 1000.0f);
  glDisable    (GL_BLEND);
  glEnable     (GL_DEPTH_TEST);
  glDepthFunc  (GL_LEQUAL);
  glDepthMask  (GL_TRUE);
  glDisable    (GL_CULL_FACE);
  glShadeModel (GL_FLAT);
  glDisable    (GL_TEXTURE_2D);
  glHint       (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  CHECKERR ("post-3dfrustum_setup");
}

static void render_setup_cyl (opengl_driver_t *this) {
  render_setup_3d       (this);
  render_help_setup_tex (this);
  glClearColor  (0, .2, .3, 0);
}

#define TOR_TESSELATION_B  128
#define TOR_TESSELATION_S  64
#define TOR_RADIUS_B       2.5
#define TOR_RADIUS_S       1.0

static void render_setup_torus (opengl_driver_t *this) {
  int i, j, k;
  
  render_setup_3d       (this);
  render_help_setup_tex (this);

  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,   GL_MODULATE);
  glTexGeni (GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
  glTexGeni (GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

  /* create display list */
  glNewList (1, GL_COMPILE);
  for (i = 0; i < TOR_TESSELATION_B; i++) {
    glBegin (GL_QUAD_STRIP);
    for (j = 0; j <= TOR_TESSELATION_S; j++) {
      float phi = MY_2PI * j / TOR_TESSELATION_S;
      for (k = 0; k <= 1; k++) {
        float theta = MY_2PI * (i + k) / TOR_TESSELATION_B;
	float nx    = TOR_RADIUS_S * cos(phi) * cos(theta);
	float ny    = TOR_RADIUS_S * cos(phi) * sin(theta);
	float nz    = TOR_RADIUS_S * sin(phi);
	float nnorm = 1.0 / sqrt (nx*nx + ny*ny + nz*nz);
	float x     = (TOR_RADIUS_B + TOR_RADIUS_S * cos(phi)) * cos(theta);
	float y     = (TOR_RADIUS_B + TOR_RADIUS_S * cos(phi)) * sin(theta);
	float z     = TOR_RADIUS_S * sin(phi);
        glNormal3f (nx * nnorm, ny * nnorm, nz * nnorm);
        glVertex3f (x, y, z);
      }
    }
    glEnd   ();
  }
  glEndList ();
}

static char *opengl_render_fun_names[] = {
  "2D_Textures", "Image_Pipeline", "Cylinder", "Environment_Mapped_Torus", NULL
};
static void (*opengl_display_funs[])(opengl_driver_t *, opengl_frame_t *) = {
  render_tex2d, render_draw, render_cyl, render_env_tor
};
static void (*opengl_image_funs[])(opengl_driver_t *, opengl_frame_t *) = {
  render_image_tex, render_image_nop, render_image_tex, render_image_envtex
};
static void (*opengl_setup_funs[])(opengl_driver_t *) = {
  render_setup_tex2d, render_setup_2d, render_setup_cyl,
  render_setup_torus
};
static enum render_e opengl_default_action[] = {
    RENDER_NONE, RENDER_NONE, RENDER_DRAW, RENDER_DRAW
};

/*
 * GFX state management
 */
static void render_gfx_vinfo (opengl_driver_t *this) {
  static int glxAttrib[] = {
    GLX_RGBA, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1,
    GLX_DEPTH_SIZE, 1, None, None
  } ;
  if (this->render_double_buffer)
    glxAttrib[9] = GLX_DOUBLEBUFFER;
  else
    glxAttrib[9] = None;
  this->vinfo = glXChooseVisual (this->display, this->screen, glxAttrib);
  CHECKERR ("choosevisual");
}

/*
 * Render thread
 */
static void *render_run (opengl_driver_t *this) {
  int             action, changed;
  opengl_frame_t *frame;
  struct timeval  curtime;
  struct timespec timeout;
  
  lprintf ("* render thread created\n");
  while (1) {
      
    /* Wait for render action */
    pthread_mutex_lock (&this->render_action_mutex);
    if (! this->render_action) {
      this->render_action = opengl_default_action [this->render_fun_id];
      if (this->render_action) {
	/* we have to animate even static images */
	gettimeofday (&curtime, NULL);
	timeout.tv_nsec = 1000 * curtime.tv_usec + 1e9L / this->render_min_fps;
	timeout.tv_sec  = curtime.tv_sec;
	if (timeout.tv_nsec > 1e9L) {
	  timeout.tv_nsec -= 1e9L;
	  timeout.tv_sec  += 1;
	}
	pthread_cond_timedwait (&this->render_action_cond,
				&this->render_action_mutex, &timeout);
      } else {
	pthread_cond_wait (&this->render_action_cond,
			   &this->render_action_mutex);
      }
    }
    action  = this->render_action;
    changed = this->render_frame_changed;
    /* frame may be updated/deleted outside mutex, but still atomically */
    /* we do not (yet) care to check frames for validity - this is a race.. */
    /* but we do not delete/change frames for at least 4 frames after update */
    frame  = this->frame[0];

    lprintf ("* render action: %d   frame %d   changed %d   drawable %lx\n",
	     action, frame ? frame->vo_frame.id : -1, changed, this->drawable);
    switch (action) {

    case RENDER_NONE:
      pthread_mutex_unlock (&this->render_action_mutex);
      break;

    case RENDER_DRAW:
      this->render_action = RENDER_NONE;
      this->render_frame_changed = 0;
      pthread_mutex_unlock (&this->render_action_mutex);
      if (this->context && frame) {
	XLockDisplay (this->display);
	CHECKERR ("pre-render");
	if (changed)
	  (opengl_image_funs   [this->render_fun_id]) (this, frame);
	(opengl_display_funs [this->render_fun_id]) (this, frame);
	glXSwapBuffers(this->display, this->drawable);
	/* Note: no glFinish() - work concurrently to the graphics pipe */
	CHECKERR ("post-render");
	XUnlockDisplay (this->display);
      }
      break;

    case RENDER_CLEAN:
      this->render_action = RENDER_DRAW;
      this->render_frame_changed = 0;
      pthread_mutex_unlock (&this->render_action_mutex);
      if (this->context && frame) {
	XLockDisplay (this->display);
	CHECKERR ("pre-clean");
	if (changed)
	  (opengl_image_funs   [this->render_fun_id]) (this, frame);
	if (this->render_double_buffer) {
	  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
		   | GL_STENCIL_BUFFER_BIT);
	  (opengl_display_funs [this->render_fun_id]) (this, frame);
	  glXSwapBuffers(this->display, this->drawable);
	}
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
		 | GL_STENCIL_BUFFER_BIT);
	XUnlockDisplay (this->display);
      }
      break;

    case RENDER_SETUP:
      this->render_action = RENDER_CLEAN;
      this->render_frame_changed = 1;
      pthread_mutex_unlock (&this->render_action_mutex);
      if (this->context) {
	XLockDisplay (this->display);
	(opengl_setup_funs [this->render_fun_id]) (this);
	XUnlockDisplay (this->display);
	this->tex_width = this->tex_height = 0;
      }
      break;

    case RENDER_CREATE:
      this->render_action = RENDER_NONE;
      pthread_mutex_unlock (&this->render_action_mutex);
      _x_assert (this->vinfo);
      if (this->context)
	xprintf (this->xine, XINE_VERBOSITY_LOG,
		 "video_out_opengl: last context not destroyed\n"
		 "   (frontend does not support XINE_GUI_SEND_WILL_DESTROY_DRAWABLE)\n"
		 "   This will be a memory leak.\n");
      XLockDisplay (this->display);
      this->context = glXCreateContext (this->display, this->vinfo, NULL, True);
      if (this->context) {
        glXMakeCurrent (this->display, this->drawable, this->context);
	CHECKERR ("create+makecurrent");
      }
      XUnlockDisplay (this->display);
      break;

    case RENDER_VISUAL:
      this->render_action = RENDER_NONE;
      pthread_mutex_unlock (&this->render_action_mutex);
      XLockDisplay (this->display);
      render_gfx_vinfo (this);
      XUnlockDisplay (this->display);
      if (this->vinfo == NULL)
	xprintf (this->xine, XINE_VERBOSITY_NONE,
		 "video_out_opengl: no OpenGL support available (glXChooseVisual)\n");
      else
        lprintf ("* visual %p id %lx depth %d\n", this->vinfo->visual,
		 this->vinfo->visualid, this->vinfo->depth);
      break;

    case RENDER_RELEASE:
	this->render_action = RENDER_NONE;
      pthread_mutex_unlock (&this->render_action_mutex);
      if (this->context) {
	XLockDisplay (this->display);
	glXMakeCurrent    (this->display, None, NULL);
	glXDestroyContext (this->display, this->context);
	CHECKERR ("release");
	XUnlockDisplay (this->display);
	this->context = NULL;
      }
      break;

    case RENDER_EXIT:
      pthread_mutex_unlock (&this->render_action_mutex);
      if (this->context) {
	XLockDisplay (this->display);
	glXMakeCurrent    (this->display, None, NULL);
	glXDestroyContext (this->display, this->context);
	CHECKERR ("exit");
	XUnlockDisplay (this->display);
      }
      pthread_exit      (NULL);
      break;

    default:
      this->render_action = RENDER_NONE;
      pthread_mutex_unlock (&this->render_action_mutex);
      _x_assert (!action);		/* unknown action */
    }
    lprintf ("* render action: %d   frame %d   done\n", action,
	     frame ? frame->vo_frame.id : -1);
    pthread_cond_signal (&this->render_return_cond);
  }
  /* NOTREACHED */
  return NULL;
}


/*
 * and now, the driver functions
 */

static uint32_t opengl_get_capabilities (vo_driver_t *this_gen) {
/*   opengl_driver_t *this = (opengl_driver_t *) this_gen; */
  uint32_t capabilities = VO_CAP_YV12 | VO_CAP_YUY2;

  /* TODO: somehow performance goes down during the first few frames */
/*   if (this->xoverlay) */
/*     capabilities |= VO_CAP_UNSCALED_OVERLAY; */

  return capabilities;
}

static void opengl_frame_proc_slice (vo_frame_t *vo_img, uint8_t **src) {
  opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;
  /*opengl_driver_t *this = (opengl_driver_t *) vo_img->driver; */
  
  vo_img->proc_called = 1;                                    

/*   lprintf ("%p: frame_copy src %p/%p/%p to %p\n", frame, src[0], src[1], src[2], frame->rgb_dst); */

  if( frame->vo_frame.crop_left || frame->vo_frame.crop_top || 
      frame->vo_frame.crop_right || frame->vo_frame.crop_bottom )
  {
    /* TODO: opengl *could* support this?!? */
    /* cropping will be performed by video_out.c */
    return;
  }
  
  if (frame->format == XINE_IMGFMT_YV12)
    frame->yuv2rgb->yuv2rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				 src[0], src[1], src[2]);
  else
    frame->yuv2rgb->yuy22rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				  src[0]);
  
/*   lprintf ("frame_copy...done\n"); */
}

static void opengl_frame_field (vo_frame_t *vo_img, int which_field) {
  opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;
  /* opengl_driver_t *this = (opengl_driver_t *) vo_img->driver; */

/*   lprintf ("%p: frame_field image %p which_field %x\n", frame, frame->image->data, which_field); */

  switch (which_field) {
  case VO_TOP_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->rgb;
    break;
  case VO_BOTTOM_FIELD:
    frame->rgb_dst    = (uint8_t *)frame->rgb + frame->width * BYTES_PER_PIXEL;
    break;
  case VO_BOTH_FIELDS:
    frame->rgb_dst    = (uint8_t *)frame->rgb;
    break;
  }

  frame->yuv2rgb->next_slice (frame->yuv2rgb, NULL);
/*   lprintf ("frame_field...done\n"); */
}

static void opengl_frame_dispose (vo_frame_t *vo_img) {
  opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;

  frame->yuv2rgb->dispose (frame->yuv2rgb);

  free (frame->chunk[0]);
  free (frame->chunk[1]);
  free (frame->chunk[2]);
  free (frame->chunk[3]);
  free (frame);
}


static vo_frame_t *opengl_alloc_frame (vo_driver_t *this_gen) {
  opengl_frame_t  *frame;
  opengl_driver_t *this = (opengl_driver_t *) this_gen;

  frame = (opengl_frame_t *) xine_xmalloc (sizeof (opengl_frame_t));
  if (!frame)
    return NULL;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions/fields
   */
  frame->vo_frame.proc_slice = opengl_frame_proc_slice;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = opengl_frame_field; 
  frame->vo_frame.dispose    = opengl_frame_dispose;
  frame->vo_frame.driver     = this_gen;

  /*
   * colorspace converter for this frame
   */
  frame->yuv2rgb = this->yuv2rgb_factory->create_converter (this->yuv2rgb_factory);

  return (vo_frame_t *) frame;
}

static void opengl_compute_ideal_size (opengl_driver_t *this) {
  _x_vo_scale_compute_ideal_size( &this->sc );
}

static void opengl_compute_rgb_size (opengl_driver_t *this) {
  _x_vo_scale_compute_output_size( &this->sc );
}

static void opengl_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      double ratio, int format, int flags) {
  opengl_driver_t  *this = (opengl_driver_t *) this_gen;
  opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;
  int     g_width, g_height;
  double  g_pixel_aspect;

  /* Check output size to signal render thread output size changes */
  this->sc.dest_size_cb (this->sc.user_data, width, height,
			 this->sc.video_pixel_aspect, &g_width, &g_height,
			 &g_pixel_aspect);
  lprintf ("update_frame_format %dx%d output %dx%d\n", width, height,
	   g_width, g_height);

  if (g_width != this->gui_width || g_height != this->gui_height) {
      this->gui_width  = g_width;
      this->gui_height = g_height;
      pthread_mutex_lock (&this->render_action_mutex);
      if (this->render_action <= RENDER_SETUP) {
	  this->render_action = RENDER_SETUP;
	  pthread_cond_signal  (&this->render_action_cond);
      }
      pthread_mutex_unlock (&this->render_action_mutex);
  }

  /* Check frame size and format and reallocate if necessary */
  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)
      || (frame->flags  != flags)) {
    lprintf ("updating frame to %d x %d (ratio=%g, format=%08x)\n",
	     width, height, ratio, format);

    flags &= VO_BOTH_FIELDS;
	
    XLockDisplay (this->display);

    /* (re-) allocate render space */
    free (frame->chunk[0]);
    free (frame->chunk[1]);
    free (frame->chunk[2]);
    free (frame->chunk[3]);

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
    frame->rgb = xine_xmalloc_aligned (16, BYTES_PER_PIXEL*width*height,
				       (void **) &frame->chunk[3]);

    /* set up colorspace converter */
    switch (flags) {
    case VO_TOP_FIELD:
    case VO_BOTTOM_FIELD:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 width,
				 height,
				 2*frame->vo_frame.pitches[0],
				 2*frame->vo_frame.pitches[1],
				 width,
				 height,
				 BYTES_PER_PIXEL*width * 2);
      break;
    case VO_BOTH_FIELDS:
      frame->yuv2rgb->configure (frame->yuv2rgb,
				 width,
				 height,
				 frame->vo_frame.pitches[0],
				 frame->vo_frame.pitches[1],
				 width,
				 height,
				 BYTES_PER_PIXEL*width);
      break;
    }

    frame->width = width;
    frame->height = height;
    frame->format = format;

    XUnlockDisplay (this->display); 

    opengl_frame_field ((vo_frame_t *)frame, flags);
  }

  frame->ratio = ratio;
  lprintf ("done...update_frame_format\n");
}


static void opengl_overlay_clut_yuv2rgb(opengl_driver_t  *this, vo_overlay_t *overlay,
				      opengl_frame_t *frame) {
  int     i;
  clut_t* clut = (clut_t*) overlay->color;

  if (!overlay->rgb_clut) {
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
	frame->yuv2rgb->yuv2rgb_single_pixel_fun (frame->yuv2rgb, clut[i].y,
						  clut[i].cb, clut[i].cr);
    }
    overlay->rgb_clut++;
  }
  if (!overlay->clip_rgb_clut) {
    clut = (clut_t*) overlay->clip_color;
    for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) =
	frame->yuv2rgb->yuv2rgb_single_pixel_fun(frame->yuv2rgb, clut[i].y,
						 clut[i].cb, clut[i].cr);
    }
    overlay->clip_rgb_clut++;
  }
}

static void opengl_overlay_begin (vo_driver_t *this_gen, 
			      vo_frame_t *frame_gen, int changed) {
  opengl_driver_t  *this  = (opengl_driver_t *) this_gen;

  this->ovl_changed += changed;

  if (this->ovl_changed && this->xoverlay) {
    XLockDisplay (this->display);
    x11osd_clear(this->xoverlay); 
    XUnlockDisplay (this->display);
  }
}

static void opengl_overlay_end (vo_driver_t *this_gen, vo_frame_t *vo_img) {
  opengl_driver_t  *this  = (opengl_driver_t *) this_gen;

  if (this->ovl_changed && this->xoverlay) {
    XLockDisplay (this->display);
    x11osd_expose(this->xoverlay);
    XUnlockDisplay (this->display);
  }

  this->ovl_changed = 0;
}

static void opengl_overlay_blend (vo_driver_t *this_gen, 
				vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  opengl_driver_t  *this  = (opengl_driver_t *) this_gen;
  opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;

  /* Alpha Blend here */
  if (overlay->rle) {
    if (overlay->unscaled) {
      if (this->ovl_changed && this->xoverlay) {
        XLockDisplay (this->display);
        x11osd_blend (this->xoverlay, overlay); 
        XUnlockDisplay (this->display);
      }
    } else {
      if (!overlay->rgb_clut || !overlay->clip_rgb_clut)
        opengl_overlay_clut_yuv2rgb (this, overlay, frame);

#     if BYTES_PER_PIXEL == 3
        blend_rgb24 ((uint8_t *)frame->rgb, overlay,
		     frame->width, frame->height,
		     frame->width, frame->height);
#     elif BYTES_PER_PIXEL == 4
	blend_rgb32 ((uint8_t *)frame->rgb, overlay,
		     frame->width, frame->height,
		     frame->width, frame->height);
#     else
#       error "bad BYTES_PER_PIXEL"
#     endif
    }
  }
}

static int opengl_redraw_needed (vo_driver_t *this_gen) {
  opengl_driver_t  *this = (opengl_driver_t *) this_gen;
  int             ret = 0;

/*   lprintf ("redraw_needed\n"); */
  if (this->frame[0]) {
    this->sc.delivered_height   = this->frame[0]->height;
    this->sc.delivered_width    = this->frame[0]->width;
    this->sc.delivered_ratio    = this->frame[0]->ratio;

    this->sc.crop_left        = this->frame[0]->vo_frame.crop_left;
    this->sc.crop_right       = this->frame[0]->vo_frame.crop_right;
    this->sc.crop_top         = this->frame[0]->vo_frame.crop_top;
    this->sc.crop_bottom      = this->frame[0]->vo_frame.crop_bottom;

    opengl_compute_ideal_size(this);

    if( _x_vo_scale_redraw_needed( &this->sc ) ) {  
      opengl_compute_rgb_size(this);
      pthread_mutex_lock (&this->render_action_mutex);
      if (this->render_action <= RENDER_CLEAN) {
	  this->render_action = RENDER_CLEAN;
	  pthread_cond_signal  (&this->render_action_cond);
      }
      pthread_mutex_unlock (&this->render_action_mutex);
      ret = 1;
    }
  } 
  else
    ret = 1;
      
/*   lprintf ("done...redraw_needed: %d\n", ret); */
  return ret;
}

static void opengl_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  opengl_driver_t  *this  = (opengl_driver_t *) this_gen;
  opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;
  int i;

  lprintf ("about to draw frame (%d) %d x %d...\n", frame->vo_frame.id, frame->width, frame->height);

/*   lprintf ("video_out_opengl: freeing frame %d\n", this->frame[NUM_FRAMES_BACKLOG-1] ? this->frame[NUM_FRAMES_BACKLOG-1]->vo_frame.id : -1); */
  if (this->frame[NUM_FRAMES_BACKLOG-1])
    this->frame[NUM_FRAMES_BACKLOG-1]->vo_frame.free (&this->frame[NUM_FRAMES_BACKLOG-1]->vo_frame);
  for (i = NUM_FRAMES_BACKLOG-1; i > 0; i--)
    this->frame[i] = this->frame[i-1];
  this->frame[0] = frame;
  this->render_frame_changed = 1;
/*   lprintf ("video_out_opengl: cur_frame updated to %d\n", frame->vo_frame.id); */
  
  /*
   * let's see if this frame is different in size / aspect
   * ratio from the previous one
   */
  if ( (frame->width != this->sc.delivered_width)
       || (frame->height != this->sc.delivered_height)
       || (frame->ratio != this->sc.delivered_ratio) ) {
    lprintf("frame format changed\n");
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */
  }
  
  /*
   * tell gui that we are about to display a frame,
   * ask for offset and output size
   */
  opengl_redraw_needed (this_gen);

  pthread_mutex_lock (&this->render_action_mutex);
  if (this->render_action <= RENDER_DRAW) {
      this->render_action = RENDER_DRAW;
      pthread_cond_signal  (&this->render_action_cond);
  }
  pthread_mutex_unlock (&this->render_action_mutex);

  lprintf ("display frame done\n");
}

static int opengl_get_property (vo_driver_t *this_gen, int property) {
  opengl_driver_t *this = (opengl_driver_t *) this_gen;

  switch (property) {
  case VO_PROP_ASPECT_RATIO:
    return this->sc.user_ratio;
  case VO_PROP_MAX_NUM_FRAMES:
    return 15;
  case VO_PROP_BRIGHTNESS:
    return this->yuv2rgb_brightness;
  case VO_PROP_CONTRAST:
    return this->yuv2rgb_contrast;
  case VO_PROP_SATURATION:
    return this->yuv2rgb_saturation;
  case VO_PROP_WINDOW_WIDTH:
    return this->sc.gui_width;
  case VO_PROP_WINDOW_HEIGHT:
    return this->sc.gui_height;
  default:
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, 
	    "video_out_opengl: tried to get unsupported property %d\n", property);
  }

  return 0;
}

static int opengl_set_property (vo_driver_t *this_gen, 
			      int property, int value) {
  opengl_driver_t *this = (opengl_driver_t *) this_gen;

  switch (property) {
  case VO_PROP_ASPECT_RATIO:
    if (value>=XINE_VO_ASPECT_NUM_RATIOS)
      value = XINE_VO_ASPECT_AUTO;
    this->sc.user_ratio = value;
    opengl_compute_ideal_size (this);
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */

    xprintf(this->xine, XINE_VERBOSITY_DEBUG, 
	    "video_out_opengl: aspect ratio changed to %s\n", _x_vo_scale_aspect_ratio_name(value));
    break;
  case VO_PROP_BRIGHTNESS:
    this->yuv2rgb_brightness = value;
    this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
					   this->yuv2rgb_brightness,
					   this->yuv2rgb_contrast,
					   this->yuv2rgb_saturation);
    this->sc.force_redraw = 1;
    break;
  case VO_PROP_CONTRAST:
    this->yuv2rgb_contrast = value;
    this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
					   this->yuv2rgb_brightness,
					   this->yuv2rgb_contrast,
					   this->yuv2rgb_saturation);
    this->sc.force_redraw = 1;
    break;
  case VO_PROP_SATURATION:
    this->yuv2rgb_saturation = value;
    this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
					   this->yuv2rgb_brightness,
					   this->yuv2rgb_contrast,
					   this->yuv2rgb_saturation);
    this->sc.force_redraw = 1;
    break;
  default:
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, 
	     "video_out_opengl: tried to set unsupported property %d\n", property);
  }

  return value;
}

static void opengl_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {
  /* opengl_driver_t *this = (opengl_driver_t *) this_gen;  */

  switch (property) {
  case VO_PROP_BRIGHTNESS:
    *min = -128;    *max = 127;     break;
  case VO_PROP_CONTRAST:
    *min = 0;       *max = 255;     break;
  case VO_PROP_SATURATION:
    *min = 0;       *max = 255;     break;
  default:
    *min = 0;       *max = 0;
  }
}

static int opengl_gui_data_exchange (vo_driver_t *this_gen, 
				   int data_type, void *data) {
  opengl_driver_t   *this = (opengl_driver_t *) this_gen;

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
  case XINE_GUI_SEND_COMPLETION_EVENT:
    break;
#endif

  case XINE_GUI_SEND_EXPOSE_EVENT:
    
    lprintf ("expose event\n");

    if (this->frame[0]) {
      XExposeEvent * xev = (XExposeEvent *) data;
      
      if (xev && xev->count == 0) {
	pthread_mutex_lock (&this->render_action_mutex);
	if (this->render_action <= RENDER_CLEAN) {
	    this->render_action = RENDER_CLEAN;
	    pthread_cond_signal  (&this->render_action_cond);
	}
	pthread_mutex_unlock (&this->render_action_mutex);
	XLockDisplay (this->display);
        if(this->xoverlay)
          x11osd_expose(this->xoverlay);
	XSync(this->display, False);
	XUnlockDisplay (this->display);
      }
    }
    break;

  case XINE_GUI_SEND_SELECT_VISUAL:
    if (! this->context) {
      pthread_mutex_lock   (&this->render_action_mutex);
      this->render_action = RENDER_VISUAL;
      pthread_cond_signal  (&this->render_action_cond);
      pthread_cond_wait    (&this->render_return_cond,
			    &this->render_action_mutex);
      pthread_mutex_unlock (&this->render_action_mutex);
      *(XVisualInfo**)data = this->vinfo;
    }
    break;
    /* TODO: this event is yet to be implemented in the gui */
  case XINE_GUI_SEND_WILL_DESTROY_DRAWABLE:
    pthread_mutex_lock   (&this->render_action_mutex);
    this->render_action = RENDER_RELEASE;
    pthread_cond_signal  (&this->render_action_cond);
    pthread_cond_wait    (&this->render_return_cond,
			  &this->render_action_mutex);
    pthread_mutex_unlock (&this->render_action_mutex);
    break;
    
  case XINE_GUI_SEND_DRAWABLE_CHANGED:
      
    this->drawable = (Drawable) data;
    pthread_mutex_lock   (&this->render_action_mutex);
    this->render_action = RENDER_CREATE;
    pthread_cond_signal  (&this->render_action_cond);
    pthread_cond_wait    (&this->render_return_cond,
			  &this->render_action_mutex);
    pthread_mutex_unlock (&this->render_action_mutex);
    if (! this->context)
      xprintf (this->xine, XINE_VERBOSITY_NONE,
	       "video_out_opengl: cannot create OpenGL capable visual.\n"
	       "   plugin will not work.\n");
    XLockDisplay (this->display);
    if(this->xoverlay)
      x11osd_drawable_changed(this->xoverlay, this->drawable);
    this->ovl_changed = 1;
    XUnlockDisplay (this->display);
    break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:

    if (this->frame[0]) {
      x11_rectangle_t *rect = data;
      int              x1, y1, x2, y2;
      
      _x_vo_scale_translate_gui2video(&this->sc,
			       rect->x, rect->y,
			       &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->sc,
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

static void opengl_dispose (vo_driver_t *this_gen) {
  opengl_driver_t *this = (opengl_driver_t *) this_gen;
  int i;

  pthread_mutex_lock    (&this->render_action_mutex);
  this->render_action = RENDER_EXIT;
  pthread_cond_signal   (&this->render_action_cond);
  pthread_mutex_unlock  (&this->render_action_mutex);
  pthread_join          (this->render_thread, NULL);  
  pthread_mutex_destroy (&this->render_action_mutex);
  pthread_cond_destroy  (&this->render_action_cond);
  pthread_cond_destroy  (&this->render_return_cond);

  for (i = 0; i < NUM_FRAMES_BACKLOG; i++)
    if (this->frame[i])
      this->frame[i]->vo_frame.dispose (&this->frame[i]->vo_frame);

  this->yuv2rgb_factory->dispose (this->yuv2rgb_factory);

  if (this->xoverlay) {
    XLockDisplay (this->display);
    x11osd_destroy (this->xoverlay);
    XUnlockDisplay (this->display);
  }

  free (this);
}

static void opengl_cb_render_fun (void *this_gen, xine_cfg_entry_t *entry) {
  opengl_driver_t *this = (opengl_driver_t *) this_gen;
  this->render_fun_id = entry->num_value;
  pthread_mutex_lock (&this->render_action_mutex);
  if (this->render_action <= RENDER_SETUP) {
    this->render_action = RENDER_SETUP;
    pthread_cond_signal  (&this->render_action_cond);
  }
  pthread_mutex_unlock (&this->render_action_mutex);
}

static void opengl_cb_default (void *val_gen, xine_cfg_entry_t *entry) {
  int *val = (int *) val_gen;
  *val = entry->num_value;
}

static vo_driver_t *opengl_open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {
  opengl_class_t       *class   = (opengl_class_t *) class_gen;
  config_values_t      *config  = class->config;
  x11_visual_t         *visual  = (x11_visual_t *) visual_gen;
  opengl_driver_t      *this;
  
  this = (opengl_driver_t *) xine_xmalloc (sizeof (opengl_driver_t));

  if (!this)
    return NULL;

  this->display		    = visual->display;
  this->screen		    = visual->screen;

  _x_vo_scale_init (&this->sc, 0, 0, config);
  this->sc.frame_output_cb  = visual->frame_output_cb;
  this->sc.dest_size_cb     = visual->dest_size_cb;
  this->sc.user_data        = visual->user_data;
  this->sc.user_ratio       = XINE_VO_ASPECT_AUTO;
  
  this->drawable	    = visual->d;
  this->gui_width  = this->gui_height  = -1;
  this->last_width = this->last_height = -1;

  this->xoverlay                = NULL;
  this->ovl_changed             = 0;
  this->xine                  = class->xine;
  
  this->vo_driver.get_capabilities     = opengl_get_capabilities;
  this->vo_driver.alloc_frame          = opengl_alloc_frame;
  this->vo_driver.update_frame_format  = opengl_update_frame_format;
  this->vo_driver.overlay_begin        = opengl_overlay_begin;
  this->vo_driver.overlay_blend        = opengl_overlay_blend;
  this->vo_driver.overlay_end          = opengl_overlay_end;
  this->vo_driver.display_frame        = opengl_display_frame;
  this->vo_driver.get_property         = opengl_get_property;
  this->vo_driver.set_property         = opengl_set_property;
  this->vo_driver.get_property_min_max = opengl_get_property_min_max;
  this->vo_driver.gui_data_exchange    = opengl_gui_data_exchange;
  this->vo_driver.dispose              = opengl_dispose;
  this->vo_driver.redraw_needed        = opengl_redraw_needed;

  this->yuv2rgb_brightness = config->register_range (config, "video.opengl_gamma", 0,
						     -128, 127,
						     _("brightness correction"),
						     _("The brightness correction can be used to "
						       "lighten or darken the image. It changes the "
						       "blacklevel without modifying the contrast, "
						       "but it limits the tonal range."),
						     0, NULL, NULL);
  this->yuv2rgb_contrast = 128;
  this->yuv2rgb_saturation = 128;
  
  this->yuv2rgb_factory = yuv2rgb_factory_init (YUV_FORMAT, YUV_SWAP_MODE, 
						NULL);
  this->yuv2rgb_factory->set_csc_levels (this->yuv2rgb_factory,
					 this->yuv2rgb_brightness,
					 this->yuv2rgb_contrast,
					 this->yuv2rgb_saturation);

  XLockDisplay (this->display);
  this->xoverlay = x11osd_create (this->xine, this->display, this->screen,
                                  this->drawable, X11OSD_SHAPED);
  XUnlockDisplay (this->display);

  this->render_fun_id = config->register_enum (config, "video.opengl_renderer",
					       0, opengl_render_fun_names,
					       _("OpenGL renderer"),
					       _("The OpenGL plugin provides several render modules:\n\n"
						 "2D_Textures\n"
						 "This module downloads the images as 2D textures and renders a textured slice.\n"
						 "This is typically the fastest method.\n\n"
						 "Image_Pipeline\n"
						 "This module uses glDraw() to render the images.\n"
						 "Only accelerated on few drivers.\n"
						 "Does not interpolate on scaling.\n\n"
						 "Cylinder\n"
						 "Shows images on a rotating cylinder. Nice effect :)\n\n"
						 "Environment_Mapped_Torus\n"
						 "Show images reflected in a spinning torus. Way cool =)"),
					       10, opengl_cb_render_fun, this);
  this->render_min_fps = config->register_range (config,
						 "video.opengl_min_fps",
						 20, 1, 120,
						 _("OpenGL minimum framerate"),
						 _("Minimum framerate for animated render routines.\n"
						   "Ignored for static render routines.\n"),
						 20, opengl_cb_default,
						 &this->render_min_fps);
  this->render_double_buffer = config->register_bool (config, "video.opengl_double_buffer", 1,
						      _("enable double buffering"),
						      _("For OpenGL double buffering does not only remove tearing artifacts,\n"
							"it also reduces flickering a lot.\n"
							" It should not have any performance impact."),
						      20, NULL, NULL);
  
  pthread_mutex_init (&this->render_action_mutex, NULL);
  pthread_cond_init  (&this->render_action_cond, NULL);
  pthread_cond_init  (&this->render_return_cond, NULL);
  pthread_create (&this->render_thread, NULL, (thread_run_t) render_run, this);

  /* Check for OpenGL capable visual */
  pthread_mutex_lock   (&this->render_action_mutex);
  this->render_action = RENDER_VISUAL;
  pthread_cond_signal  (&this->render_action_cond);
  pthread_cond_wait    (&this->render_return_cond,
			&this->render_action_mutex);
  if (this->vinfo) {
    /* Create context if possible w/o drawable change */
    this->render_action = RENDER_CREATE;
    pthread_cond_signal  (&this->render_action_cond);
    pthread_cond_wait    (&this->render_return_cond,
			  &this->render_action_mutex);
  }
  pthread_mutex_unlock (&this->render_action_mutex);

  if (! this->vinfo) {
    /* no OpenGL capable visual available */
    opengl_dispose (&this->vo_driver);
    return NULL;
  }
  if (! this->context)
    xprintf (this->xine, XINE_VERBOSITY_LOG,
	     "video_out_opengl: default visual not OpenGL capable\n"
	     "   plugin will only work with clients supporting XINE_GUI_SEND_SELECT_VISUAL.\n");
    
  return &this->vo_driver;
}

/*
 * class functions
 */

static char* opengl_get_identifier (video_driver_class_t *this_gen) {
  return "opengl";
}

static char* opengl_get_description (video_driver_class_t *this_gen) {
  return _("xine video output plugin using the MIT X shared memory extension");
}

static void opengl_dispose_class (video_driver_class_t *this_gen) {
  opengl_class_t         *this = (opengl_class_t *) this_gen;

  free (this);
}

static void *opengl_init_class (xine_t *xine, void *visual_gen) {
  opengl_class_t	       *this = (opengl_class_t *) xine_xmalloc (sizeof (opengl_class_t));

  this->driver_class.open_plugin     = opengl_open_plugin;
  this->driver_class.get_identifier  = opengl_get_identifier;
  this->driver_class.get_description = opengl_get_description;
  this->driver_class.dispose         = opengl_dispose_class;
  this->config                       = xine->config;
  this->xine                         = xine;

  return this;
}


static vo_info_t vo_info_opengl = {
  7,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};


/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_OUT, 20, "opengl", XINE_VERSION_CODE, &vo_info_opengl, opengl_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
