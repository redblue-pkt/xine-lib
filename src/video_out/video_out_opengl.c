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
 * $Id: video_out_opengl.c,v 1.17 2002/09/04 23:31:12 guenter Exp $
 * 
 * video_out_glut.c, glut based OpenGL rendering interface for xine
 * Matthias Hopf <mat@mshopf.de>
 *
 * Based on video_out_xshm.c (1.70) and video_out_xv.c (1.108)
 */

/*
 * TODO:
 *
 * - BUG: xitk is creating images in the wrong visual type on e.g. SGI
 *   (due to driver visual selection?). this is a xitk issue.
 *   this creates a strange looking gui interface and all dialogs fail
 *   (X_PutImage: BadMatch).
 * - Rendering method to be chosen on runtime
 * - glut autoconf detection buggy
 * - Check extensions (GL_BGRA)
 * - Color conversion in hardware?
 *   - Video extension
 *   - ColorMatrix (OpenGL-1.2 or SGI_color_matrix) ?possible? don't think so
 * - Alpha Blending for overlays using texture hardware
 */


#if 0					/* set to 1 for debugging messages */
#  define DEBUGF(x) fprintf x
#else
#  define DEBUGF(x) ((void) 0)
#endif


#define USE_SPHERE	0	/* 1 for some fun! */ /* FIXME: untested */
#define USE_TEXTURES	1	/* 1 Use texture hardware   0 glDrawPixels */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#warning DISABLED: FIXME
#if 0

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
#define USE_SPHERE 0			/* unable to do that w/o glut */
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

    /* frame properties as delivered by the decoder: */
    int                width, height;
    int                ratio_code;
    int                format;
    int                flags;

    /* opengl only data */
    uint8_t           *rgb_dst;
    yuv2rgb_t         *yuv2rgb;
    uint8_t	      *chunk[3];
    int                stripe_inc;
    uint8_t           *texture;
} opengl_frame_t;

typedef struct opengl_driver_s {

    vo_driver_t      vo_driver;

    config_values_t *config;

    /* X11 / Xv related stuff */
    Display         *display;
    int              screen;
    Drawable         drawable;

    /* Output data */
    opengl_frame_t  *cur_frame;
    vo_overlay_t    *overlay;

    /* display anatomy */
    double           display_ratio;

    /* OpenGL related */
    GLXContext       context;
    volatile int     context_state;        /* is the context ok, or reload? */
    XVisualInfo     *vinfo;
    pthread_t        renderthread;

    /* current texture size - this is not frame dependend! */
    int              texture_width, texture_height;

    /* last frame delivered from the decoder */
    int              last_width;
    int              last_height;
    int              last_ratio_code;

    /* ideal size */
    int              ideal_width, ideal_height;
    int              user_ratio;

    /* gui size */
    int              gui_width, gui_height;
    int              gui_x, gui_y, gui_win_x, gui_win_y;

    /* output size */
    int              output_width, output_height;
    int              output_xoffset, output_yoffset;
  
    /* software yuv2rgb related */
    int                yuv2rgb_gamma;
    uint8_t           *yuv2rgb_cmap;
    yuv2rgb_factory_t *yuv2rgb_factory;


    void            *user_data;

    /* gui callbacks */

    void (*frame_output_cb) (void *user_data,
			     int video_width, int video_height,
			     int *dest_x, int *dest_y, 
			     int *dest_height, int *dest_width,
			     int *win_x, int *win_u);

    void (*dest_size_cb) (void *user_data,
			  int video_width, int video_height, 
			  int *dest_width, int *dest_height);

} opengl_driver_t;


enum { CONTEXT_BAD = 0, CONTEXT_SAME_DRAWABLE, CONTEXT_RELOAD, CONTEXT_SET };


/*
 * and now, the driver functions
 */


static uint32_t opengl_get_capabilities (vo_driver_t *this_gen) {
    return VO_CAP_COPIES_IMAGE | VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_BRIGHTNESS;
}


static void opengl_frame_copy (vo_frame_t *vo_img, uint8_t **src) {
    opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;

/*  DEBUGF ((stderr, "*** %p: frame_copy src %p/%p/%p to %p\n", frame, src[0], src[1], src[2], frame->rgb_dst)); */

    if (frame->format == IMGFMT_YV12) {
	frame->yuv2rgb->yuv2rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				     src[0], src[1], src[2]);
    } else {

	frame->yuv2rgb->yuy22rgb_fun (frame->yuv2rgb, frame->rgb_dst,
				      src[0]);
				 
    }

    frame->rgb_dst += frame->stripe_inc; 
/*  DEBUGF ((stderr, "frame_copy done\n")); */
}

static void opengl_frame_field (vo_frame_t *vo_img, int which_field) {

    opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;

    switch (which_field & VO_BOTH_FIELDS) {
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
/*  DEBUGF ((stderr, "*** %p: frame_field: rgb_dst %p\n", frame, frame->rgb_dst)); */
}


static void opengl_frame_dispose (vo_frame_t *vo_img) {

    opengl_frame_t  *frame = (opengl_frame_t *) vo_img ;
    opengl_driver_t *this = (opengl_driver_t *) vo_img->driver;

    DEBUGF ((stderr, "*** frame_dispose ***\n"));
    if (frame)
    {
	XLockDisplay (this->display);
	free (frame->texture);
	free (frame->chunk[0]);
	free (frame->chunk[1]);
	free (frame->chunk[2]);
	frame->texture = NULL;
	frame->chunk[0] = frame->chunk[1] = frame->chunk[2] = NULL;
	XUnlockDisplay (this->display);
    }
    free (frame);
}


static vo_frame_t *opengl_alloc_frame (vo_driver_t *this_gen) {

    opengl_frame_t   *frame ;
    opengl_driver_t *this = (opengl_driver_t *) this_gen;

    DEBUGF ((stderr, "*** alloc_frame ***\n"));
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
  
    /*
     * colorspace converter for this frame
     */
    frame->yuv2rgb = this->yuv2rgb_factory->create_converter (this->yuv2rgb_factory);

    return (vo_frame_t *) frame;
}


static void opengl_update_frame_format (vo_driver_t *this_gen,
					vo_frame_t *frame_gen,
					uint32_t width, uint32_t height,
					int ratio_code, int format, int flags) {

    opengl_driver_t  *this = (opengl_driver_t *) this_gen;
    opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;

/*  DEBUGF ((stderr, "*** %p: update_frame_format ***\n", frame)); */
    flags &= VO_BOTH_FIELDS;

    if ((frame->width     != width)
	|| (frame->height != height)
	|| (frame->format != format)
	|| (frame->flags  != flags)) {

	int image_size = width * height;
	
	DEBUGF ((stderr, "video_out_opengl: updating frame to %dx%d (ratio=%d, format=%c%c%c%c)\n",
		 width, height, ratio_code, format&0xff, (format>>8)&0xff,
		 (format>>16)&0xff, (format>>24)&0xff));

	/* update frame allocated data */

	XLockDisplay (this->display);

	free (frame->texture);
	free (frame->chunk[0]);
	free (frame->chunk[1]);
	free (frame->chunk[2]);
	frame->chunk[0] = frame->chunk[1] = frame->chunk[2] = NULL;
	
	frame->texture = calloc (1, BYTES_PER_PIXEL * image_size);
	assert (frame->texture);

	switch (format) {
	case IMGFMT_YV12:
	    frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
	    frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
	    frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
	    frame->vo_frame.base[0] = xine_xmalloc_aligned(16, frame->vo_frame.pitches[0] * height,
							   &frame->chunk[0]);
	    frame->vo_frame.base[1] = xine_xmalloc_aligned(16, frame->vo_frame.pitches[1] * ((height+1)/2),
							   &frame->chunk[1]);
	    frame->vo_frame.base[2] = xine_xmalloc_aligned(16, frame->vo_frame.pitches[2] * ((height+1)/2),
							   &frame->chunk[2]);
	    break;
	case IMGFMT_YUY2:
	    frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
	    frame->vo_frame.base[0] = xine_xmalloc_aligned(16, frame->vo_frame.pitches[0] * height,
							   &frame->chunk[0]);
	    break;
	default:
	    fprintf (stderr, "video_out_opengl: image format %d not supported, update video driver!\n", format);
	    return;
	}

	switch (flags) {
	case VO_TOP_FIELD:
	case VO_BOTTOM_FIELD:
	    frame->yuv2rgb->configure (frame->yuv2rgb,
				       width, 16, width*2, width, width,
				       STRIPE_HEIGHT,
				       width * BYTES_PER_PIXEL * 2);
	    break;
	case VO_BOTH_FIELDS:
	    frame->yuv2rgb->configure (frame->yuv2rgb,
				       width, 16, width, width/2, width,
				       STRIPE_HEIGHT,
				       width * BYTES_PER_PIXEL);
	    break;
	}
	
	frame->width  = width;
	frame->height = height;
	frame->format = format;
	frame->flags  = flags;
	
	XUnlockDisplay (this->display);
    }

    frame->ratio_code = ratio_code;
    opengl_frame_field ((vo_frame_t *)frame, flags);
}


static void opengl_compute_ideal_size (opengl_driver_t *this,
                                       int width, int height, int ratio_code) {
    double image_ratio, desired_ratio;
    double corr_factor;

/*  DEBUGF ((stderr, "*** compute_ideal_size\n")); */
    /*
     * aspect ratio calculation
     */
    image_ratio = (double) width / (double) height;

    switch (this->user_ratio) {
    case ASPECT_AUTO:
	switch (ratio_code) {
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
	    printf ("video_out_opengl: invalid ratio, using 4:3\n");
	default:
	    printf ("video_out_opengl: unknown aspect ratio (%d) in stream => using 4:3\n",
		    ratio_code);
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

    corr_factor = desired_ratio * this->display_ratio / image_ratio;

    if (fabs (corr_factor - 1.0) < 0.005) {
	this->ideal_width  = width;
	this->ideal_height = height;
    } else {
	if (corr_factor >= 1.0) {
	    this->ideal_width  = width * corr_factor + 0.5;
	    this->ideal_height = height;
	} else {
	    this->ideal_width  = width;
	    this->ideal_height = height / corr_factor + 0.5;
	}
    }

    DEBUGF ((stderr, "*** opengl_compute_ideal_size %dx%d -> %dx%d\n",
	     width, height,
	     this->ideal_width, this->ideal_height));
}


static void opengl_compute_output_size (opengl_driver_t *this) {

    double x_factor, y_factor;
    int    old_width = this->output_width,   old_height = this->output_height;
    int    old_x     = this->output_xoffset, old_y      = this->output_yoffset;

    DEBUGF ((stderr, "*** compute_output_size for gui %dx%d ideal %dx%d\n",
	     this->gui_width, this->gui_height,
	     this->ideal_width, this->ideal_height));
    /*
     * make the frame fit into the given destination area
     */
  
    x_factor = (double) this->gui_width  / (double) this->ideal_width;
    y_factor = (double) this->gui_height / (double) this->ideal_height;
  
    if ( x_factor < y_factor ) {
	this->output_width   = (double) this->ideal_width  * x_factor ;
	this->output_height  = (double) this->ideal_height * x_factor ;
    } else {
	this->output_width   = (double) this->ideal_width  * y_factor ;
	this->output_height  = (double) this->ideal_height * y_factor ;
    }
    this->output_xoffset = (this->gui_width - this->output_width) / 2 + this->gui_x;
    this->output_yoffset = (this->gui_height - this->output_height) / 2 - this->gui_y;

    DEBUGF ((stderr,
	     "video_out_opengl: screen output %d x %d offset %d/%d\n",
	     this->output_width, this->output_height,
	     this->output_xoffset, this->output_yoffset
	));

    /* Force state reinitialization / clear */
    if ( (old_width != this->ideal_width || old_height != this->ideal_height
	  || old_x  != this->output_xoffset || old_y   != this->output_yoffset)
	 && this->context_state == CONTEXT_SET)
	this->context_state = CONTEXT_RELOAD;
}


static void opengl_overlay_clut_yuv2rgb (opengl_driver_t  *this,
					 vo_overlay_t *overlay,
					 opengl_frame_t *frame) {
    int i;
    clut_t* clut = (clut_t*) overlay->color;
    if (!overlay->rgb_clut) {
	for (i = 0; i < sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
	    *((uint32_t *)&clut[i]) =
		frame->yuv2rgb->yuv2rgb_single_pixel_fun(frame->yuv2rgb,
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


static void opengl_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
				  vo_overlay_t *overlay) {
    opengl_driver_t  *this = (opengl_driver_t *) this_gen;
    opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;

    DEBUGF ((stderr, "*** overlay_blend\n"));
    /* Alpha Blend here */
    if (overlay->rle) {
	if( !overlay->rgb_clut || !overlay->clip_rgb_clut)
	    opengl_overlay_clut_yuv2rgb (this,overlay, frame);

#       if BYTES_PER_PIXEL == 3
	    blend_rgb24 ((uint8_t *)frame->texture, overlay,
		         frame->width, frame->height,
		         frame->width, frame->height);
#       elif BYTES_PER_PIXEL == 4
	    blend_rgb32 ((uint8_t *)frame->texture, overlay,
		         frame->width, frame->height,
		         frame->width, frame->height);
#       else
#           error "bad BYTES_PER_PIXEL"
#       endif
    }
}


static int opengl_redraw_needed (vo_driver_t *this_gen) {
    opengl_driver_t  *this = (opengl_driver_t *) this_gen;
    int gui_x, gui_y, gui_width, gui_height, gui_win_x, gui_win_y;

    DEBUGF ((stderr, "*** redraw_needed %dx%d\n", this->ideal_width, this->ideal_height));
    this->frame_output_cb (this->user_data,
			   this->ideal_width,
			   this->ideal_height, 
			   &gui_x, &gui_y, &gui_width, &gui_height,
			   &gui_win_x, &gui_win_y );
    DEBUGF ((stderr, "*** frame_output_cb returned\n"));

    if ( (this->gui_x     != gui_x)     || (this->gui_y      != gui_y)      ||
	 (this->gui_width != gui_width) || (this->gui_height != gui_height) ||
	 (this->gui_win_x != gui_win_x) || (this->gui_win_y  != gui_win_y)  ||
         (this->context_state != CONTEXT_SET)) {

	this->gui_x      = gui_x;
	this->gui_y      = gui_y;
	this->gui_width  = gui_width;
	this->gui_height = gui_height;
	this->gui_win_x  = gui_win_x;
	this->gui_win_y  = gui_win_y;
	
	opengl_compute_output_size (this);
	
	/* Actually, the output area is cleared in render_image */
	return 1;
    }
    return 0;
}


static void opengl_render_image (opengl_driver_t *this, opengl_frame_t *frame,
                                 GLXContext ctx)
{
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

    /* already initialized? */
    if (! this->drawable || ! this->vinfo)
    {
	fprintf (stderr, "video_out_opengl: early exit due to missing drawable %lx vinfo %p\n", this->drawable, this->vinfo);
	return;
    }

    /*
     * check for size changes
     */
    if (frame->width      != this->last_width    ||
	frame->height     != this->last_height   ||
	frame->ratio_code != this->last_ratio_code) {
	
	this->last_width      = frame->width;
	this->last_height     = frame->height;
	this->last_ratio_code = frame->ratio_code;
	
	DEBUGF ((stderr, "video_out_opengl: display format changed\n"));
	opengl_compute_ideal_size (this, frame->width, frame->height,
	                           frame->ratio_code);
	this->gui_width = 0;		/* trigger re-calc of output size */
    }
	
    /*
     * tell gui about image size and ask for offset
     */
    this->cur_frame = frame;
    opengl_redraw_needed ((vo_driver_t *) this);

    /*
     * check whether a new context has to be created
     */
    if (((ctx == this->context || ! ctx) &&
	 (this->context_state == CONTEXT_BAD ||
	  this->context_state == CONTEXT_SAME_DRAWABLE)) ||
	(self != this->renderthread))
    {
	assert (this->vinfo);
	if ((this->context_state == CONTEXT_SAME_DRAWABLE) &&
	    (self == this->renderthread))
	{
	    DEBUGF ((stderr, "destroy: %p\n", this->context));
	    /* Unfortunately for _BAD the drawable is already destroyed.
	     * This cannot be resolved right now and will be a memory leak. */
	    if (this->context)
		glXDestroyContext (this->display, this->context);
	}
	DEBUGF ((stderr, "create\n"));
	ctx = glXCreateContext (this->display, this->vinfo, NULL, True);
	assert (ctx);
	this->context = ctx;
	this->context_state = CONTEXT_RELOAD;
	this->renderthread  = self;
    }

    if (this->context_state == CONTEXT_RELOAD && ! ctx)
	ctx = this->context;

    /*
     * reload and initialize context and clear display
     * this is handled together due to close relationship
     */
    if (ctx)
    {
	int i;
	void *texture_data;

	DEBUGF ((stderr, "set context %p\n", ctx));
	/*
	 * Set and initialize context
	 */
	if (! glXMakeCurrent (this->display, this->drawable, ctx)) {
	    fprintf (stderr, "video_out_opengl: no OpenGL support available (glXMakeCurrent)\n    The drawable does not seem to be updated correctly.\n");
	    abort();
	}
	DEBUGF ((stderr, "set context done\n"));
	if (ctx == this->context)
	    this->context_state = CONTEXT_SET;
	else if (this->context_state == CONTEXT_SET ||
		 this->context_state == CONTEXT_RELOAD)
	    this->context_state = CONTEXT_RELOAD;
	glViewport (0, 0, this->gui_width, this->gui_height);
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();
	glClearColor (0, 0, 0, 0);
	glClearDepth (1.0f);
	glDisable    (GL_BLEND);
	glDisable    (GL_DEPTH_TEST);
	glDepthMask  (GL_FALSE);
	glDisable    (GL_CULL_FACE);
	glShadeModel (GL_FLAT);
	glHint       (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

#if USE_SPHERE
	glClear    (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDepthFunc     (GL_LEQUAL);
	glEnable        (GL_DEPTH_TEST);
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glScalef((float)(frame->width)/(float)(this->texture_width),
		 (float)(frame->height)/(float)(this->texture_height),
		 1.0f);
	glMatrixMode    (GL_PROJECTION);
	glLoadIdentity  ();
	gluPerspective  (45.0f, 
			 (GLfloat)(this->gui_width)/
			 (GLfloat)(this->gui_height),
			 1.0f, 1000.0f);
	glTexGeni       (GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
	glTexGeni       (GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

#else
	glClear    (GL_COLOR_BUFFER_BIT);
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	glOrtho (0, this->gui_width, this->gui_height, 0, -1, 1);
#endif

#if USE_TEXTURES
	glEnable        (GL_TEXTURE_2D);
	glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D,  GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexEnvi       (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,   GL_REPLACE);

	/*
	 * check necessary texture size and allocate
	 */
	this->texture_width = 1;
	while (this->texture_width < frame->width)
	    this->texture_width <<= 1;
	this->texture_height = 1;
	while (this->texture_height < frame->height)
	    this->texture_height <<= 1;

	texture_data = malloc (this->texture_width * this->texture_height * 3);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
		      this->texture_width, this->texture_height,
		      0, GL_RGB, GL_UNSIGNED_BYTE, texture_data);
	free (texture_data);
#endif
    }


    if (frame)
    {
	/*
	 * Render one image
	 */
#if USE_TEXTURES
        int x1 = this->output_xoffset,    y1 = this->output_yoffset;
	int x2 = x1 + this->output_width, y2 = y1 + this->output_height;
	float tx = (float) frame->width  / this->texture_width;
	float ty = (float) frame->height / this->texture_height;

	glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
			 (GLsizei)(frame->width),
			 (GLsizei)(frame->height),
			 RGB_TEXTURE_FORMAT, GL_UNSIGNED_BYTE,
			 frame->texture);
	
	glBegin (GL_QUADS);
	glTexCoord2f (tx, ty);   glVertex2i   (x2, y2);
	glTexCoord2f (0,  ty);   glVertex2i   (x1, y2);
	glTexCoord2f (0,  0);    glVertex2i   (x1, y1);
	glTexCoord2f (tx, 0);    glVertex2i   (x2, y1);
	glEnd ();	
	
#if USE_SPHERE
	glEnable(GL_TEXTURE_GEN_S);
	glEnable(GL_TEXTURE_GEN_T);	

	glTranslatef(0.0f, 0.0f, -10.0f);
	glutSolidSphere(3.0f, 20, 10);

	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);
#endif

#else
	glPixelZoom (((float)this->output_width)    / frame->width,
                 - ((float)this->output_height) / frame->height);
	glRasterPos2i (this->output_xoffset, this->output_yoffset);
	glDrawPixels (frame->width, frame->height, RGB_TEXTURE_FORMAT,
		      GL_UNSIGNED_BYTE, frame->texture);
#endif
	DEBUGF ((stderr, "render done\n"));
    }
  
    glFlush ();
    DEBUGF ((stderr, "video_output_opengl: OpenGL error: '%s'\n", gluErrorString (glGetError ())));
    /* Note: no glFinish() - work concurrently to the graphics pipe */
}


static void opengl_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

    opengl_driver_t  *this = (opengl_driver_t *) this_gen;
    opengl_frame_t   *frame = (opengl_frame_t *) frame_gen;

    DEBUGF ((stderr, "*** display_frame ***\n"));
    if( this->cur_frame )
	this->cur_frame->vo_frame.displayed (&this->cur_frame->vo_frame);
    this->cur_frame = frame;

    XLockDisplay (this->display);
    opengl_render_image (this, frame, NULL);
    XUnlockDisplay (this->display);
    
    /* Theoretically, the frame data is not used immedeately, and the
     * graphics system might address altered data - but only if we
     * are faster than the graphics hardware... */
    /* If the image seems to be clobbered, remove the following two lines */
    /* Note: We cannot do expose events, when the frame is deleted. */
    /* Note: We cannot do expose events anyway right now (errors with
     * multiple threads rendering in many OpenGL implementations) */
    /* FIXME: check that */
#if 1
    frame->vo_frame.displayed (&frame->vo_frame);
    this->cur_frame = NULL;
#endif
    DEBUGF ((stderr, "done display_frame\n"));
}


static int opengl_get_property (vo_driver_t *this_gen, int property) {

    opengl_driver_t *this = (opengl_driver_t *) this_gen;

    DEBUGF ((stderr, "*** get_property\n"));
    switch (property) {
    case VO_PROP_ASPECT_RATIO:
	return this->user_ratio ;
    case VO_PROP_BRIGHTNESS:
	return this->yuv2rgb_gamma;
    default:
	printf ("video_out_opengl: tried to get unsupported property %d\n", 
		property);
    }

    return 0;
}


static char *aspect_ratio_name(int a) {
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
    
    DEBUGF ((stderr, "*** set_property\n"));
    switch (property) {
    case VO_PROP_ASPECT_RATIO:
	if (value>=NUM_ASPECT_RATIOS)
	    value = ASPECT_AUTO;
	this->user_ratio = value;
	fprintf(stderr, "video_out_opengl: aspect ratio changed to %s\n",
	        aspect_ratio_name(value));
	this->gui_width = 0;		/* trigger re-calc of output size */
	opengl_compute_ideal_size (this, this->last_width,
	                           this->last_height, this->last_ratio_code);
	opengl_redraw_needed      ((vo_driver_t *) this);
	break;
    case VO_PROP_BRIGHTNESS:
	this->yuv2rgb_gamma = value;
	this->yuv2rgb_factory->set_gamma (this->yuv2rgb_factory, value);
	printf("video_out_opengl: gamma changed to %d\n",value);
	break;
    default:
	printf ("video_out_opengl: tried to set unsupported property %d\n", property);
    }
    
    return value;
}

static void opengl_get_property_min_max (vo_driver_t *this_gen,
					 int property, int *min, int *max) {

    DEBUGF ((stderr, "get_property_min_max\n"));
    /* opengl_driver_t *this = (opengl_driver_t *) this_gen;  */
    if ( property == VO_PROP_BRIGHTNESS) {
	*min = -100;
	*max = +100;
    } else {
	*min = 0;
	*max = 0;
    }
}


static void opengl_translate_gui2video(opengl_driver_t *this,
				       opengl_frame_t  *frame,
				       int x, int y,
				       int *vid_x, int *vid_y) {

    DEBUGF ((stderr, "*** translate_gui2video ***\n"));
    if (this->output_width > 0 && this->output_height > 0) {
	/*
	 * the driver may center a small output area inside a larger
	 * gui area.  This is the case in fullscreen mode, where we often
	 * have black borders on the top/bottom/left/right side.
	 */
	x -= this->output_xoffset;
	y -= this->output_yoffset;

	/*
	 * the driver scales the delivered area into an output area.
	 * translate output area coordianates into the delivered area
	 * coordiantes.
	 */
	x = x * frame->width  / this->output_width;
	y = y * frame->height / this->output_height;
    }

    *vid_x = x;
    *vid_y = y;
}

static int opengl_gui_data_exchange (vo_driver_t *this_gen, 
				     int data_type, void *data) {

    opengl_driver_t   *this = (opengl_driver_t *) this_gen;
    static int glxAttrib[] = {
	GLX_RGBA, GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1, None
    } ;

    DEBUGF ((stderr, "*** gui_data_exchange ***\n"));

    switch (data_type) {
    case GUI_SELECT_VISUAL:
	DEBUGF ((stderr, "*** gui_select_visual ***\n"));
	XLockDisplay (this->display);
	this->vinfo = glXChooseVisual (this->display, this->screen, glxAttrib);
	XUnlockDisplay (this->display);
	if (this->vinfo == NULL)
	    fprintf (stderr, "video_out_opengl: no OpenGL support available (glXChooseVisual)\n");
	*(XVisualInfo**)data = this->vinfo;
	DEBUGF ((stderr, "*** visual %p depth %d\n", this->vinfo->visual, this->vinfo->depth));
	break;
      
    case GUI_DATA_EX_EXPOSE_EVENT:
	DEBUGF ((stderr, "*** gui_expose ***\n"));

	/* Note that the global GLX context is not available on all
	 * architectures in this thread */
	/* Note that using different contextes in different threads
	 * for the same drawable seems to be broken with several
	 * OpenGL implementations */
	/* Thus nothing is updated here, just the render thread is
	 * notified */
	if (this->context_state == CONTEXT_SET)
	    this->context_state = CONTEXT_RELOAD;
	break;

    case GUI_DATA_EX_DRAWABLE_CHANGED:
	DEBUGF ((stderr, "*** gui_drawable_changed: %ld\n", (Drawable) data));
	XLockDisplay (this->display);
	/* Unfortunately, the last drawable is already gone, so we cannot
	 * destroy the former context. This is a memory leak. Unfortunately. */
	/* Even if the drawable remains the same, this does not seem to
	 * work :( */
	/* FIXME: check that */
#if 0
	if (this->drawable == (Drawable) data)
	    this->context_state = CONTEXT_SAME_DRAWABLE;
	else
	    this->context_state = CONTEXT_BAD;
#else
	this->context_state = CONTEXT_BAD;
#endif
	if (this->context_state == CONTEXT_BAD)
	    DEBUGF ((stderr, "*** drawable changed, state now bad\n"));
	this->drawable = (Drawable) data;
	XUnlockDisplay (this->display);
	break;

    case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO:
/*  	DEBUGF ((stderr, "*** gui_translate_gui_to_video ***\n")); */
	if (this->cur_frame) {
	    int x1, y1, x2, y2;
	    x11_rectangle_t *rect = data;

	    opengl_translate_gui2video(this, this->cur_frame,
				       rect->x, rect->y, &x1, &y1);
	    opengl_translate_gui2video(this, this->cur_frame,
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

/*      DEBUGF ((stderr, "done gui_data_exchange\n")); */
    return 0;
}


static void opengl_exit (vo_driver_t *this_gen) {
    opengl_driver_t *this = (opengl_driver_t *) this_gen;

    XLockDisplay (this->display);
    if (this->cur_frame)
	this->cur_frame->vo_frame.dispose (&this->cur_frame->vo_frame);
    XUnlockDisplay (this->display);

    glXMakeCurrent (this->display, None, NULL);
    glXDestroyContext (this->display, this->context);
    this->context = NULL;

    free (this);
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
    this->frame_output_cb     = visual->frame_output_cb;
    this->dest_size_cb        = visual->dest_size_cb;
    this->user_data           = visual->user_data;
    this->gui_x               = 0;
    this->gui_y               = 0;
    this->gui_width	    = 0;
    this->gui_height	    = 0;
    this->user_ratio          = ASPECT_AUTO;
    this->texture_width       = 0;
    this->texture_height      = 0;
    this->drawable	    = None;      /* We need a different one with a dedicated visual anyway */
    this->context_state       = CONTEXT_BAD;
    this->cur_frame           = NULL;

    this->vo_driver.get_capabilities     = opengl_get_capabilities;
    this->vo_driver.alloc_frame          = opengl_alloc_frame;
    this->vo_driver.update_frame_format  = opengl_update_frame_format;
    this->vo_driver.overlay_begin        = NULL; /* not used */
    this->vo_driver.overlay_blend        = opengl_overlay_blend;
    this->vo_driver.overlay_end          = NULL; /* not used */
    this->vo_driver.display_frame        = opengl_display_frame;
    this->vo_driver.get_property         = opengl_get_property;
    this->vo_driver.set_property         = opengl_set_property;
    this->vo_driver.get_property_min_max = opengl_get_property_min_max;
    this->vo_driver.gui_data_exchange    = opengl_gui_data_exchange;
    this->vo_driver.redraw_needed        = opengl_redraw_needed;
    this->vo_driver.exit                 = opengl_exit;

    this->yuv2rgb_gamma = config->register_range (config,
						  "video.opengl_gamma", 0,
						  -100, 100,
						  _("gamma correction for OpenGL driver"),
						  NULL, NULL, NULL);
    this->yuv2rgb_factory = yuv2rgb_factory_init (YUV_FORMAT, YUV_SWAP_MODE, 
						  this->yuv2rgb_cmap);
    this->yuv2rgb_factory->set_gamma (this->yuv2rgb_factory, this->yuv2rgb_gamma);

    return &this->vo_driver;
}

static vo_info_t vo_info_shm = {
    6,
    "OpenGL",
    NULL,
    VISUAL_TYPE_X11,
    3
};

vo_info_t *get_video_out_plugin_info() {
    vo_info_shm.description = _("xine video output plugin using OpenGL(tm)");
    return &vo_info_shm;
}

#endif
