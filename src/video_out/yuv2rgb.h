/*
 * yuv2rgb.h
 *
 * Copyright (C) 2001-2017 the xine project
 * This file is part of xine, a free video player.
 *
 * based on work from mpeg2dec:
 * Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */

#ifndef HAVE_YUV2RGB_H
#define HAVE_YUV2RGB_H

#include "config.h"

#ifdef HAVE_MLIB
#include <mlib_video.h>
#endif

#include <inttypes.h>

typedef struct yuv2rgb_s yuv2rgb_t;

typedef struct yuv2rgb_factory_s yuv2rgb_factory_t;

/*
 * function types for functions which can be replaced
 * by hardware-accelerated versions
 */

/* internal function use to scale yuv data */
typedef void (*scale_line_func_t) (uint8_t *source, uint8_t *dest, int width, int step);

typedef void (*yuv2rgb_fun_t) (yuv2rgb_t *this, uint8_t * image, uint8_t * py, uint8_t * pu, uint8_t * pv) ;

typedef void (*yuy22rgb_fun_t) (yuv2rgb_t *this, uint8_t * image, uint8_t * p);

typedef uint32_t (*yuv2rgb_single_pixel_fun_t) (yuv2rgb_t *this, uint8_t y, uint8_t u, uint8_t v);

/*
 * modes supported - feel free to implement yours
 */

#define MODE_8_RGB    1
#define MODE_8_BGR    2
#define MODE_15_RGB   3
#define MODE_15_BGR   4
#define MODE_16_RGB   5
#define MODE_16_BGR   6
#define MODE_24_RGB   7
#define MODE_24_BGR   8
#define MODE_32_RGB   9
#define MODE_32_BGR  10
#define	MODE_8_GRAY  11
#define MODE_PALETTE 12

  /*
   * colormatrix values - (mpeg_matrix_index << 1) | fullrange
   */

#define CM_DEFAULT   10
#define CM_SD        10
#define CM_HD         2
#define CM_FULLRANGE  1

struct yuv2rgb_s {
  /*
   * configure converter for scaling factors
   */
  int (*configure) (yuv2rgb_t *this,
		    int source_width, int source_height,
		    int y_stride, int uv_stride,
		    int dest_width, int dest_height,
		    int rgb_stride);

  /*
   * start a new field or frame if dest is NULL
   */
  int (*next_slice) (yuv2rgb_t *this, uint8_t **dest);

  /*
   * free resources
   */
  void (*dispose) (yuv2rgb_t *this);

  /*
   * this is the function to call for the yuv2rgb and scaling process
   */
  yuv2rgb_fun_t     yuv2rgb_fun;

  /*
   * this is the function to call for the yuy2->rgb and scaling process
   */
  yuy22rgb_fun_t    yuy22rgb_fun;

  /*
   * this is the function to call for the yuv2rgb for a single pixel
   * (used for converting clut colors)
   */

  yuv2rgb_single_pixel_fun_t yuv2rgb_single_pixel_fun;

  /* private stuff below */

  int               source_width, source_height;
  int               y_stride, uv_stride;
  int               dest_width, dest_height;
  int               rgb_stride;
  int               slice_height, slice_offset;
  int               step_dx, step_dy;
  int               do_scale, swapped;

  uint8_t          *y_buffer;
  uint8_t          *u_buffer;
  uint8_t          *v_buffer;
  void	           *y_chunk;
  void	           *u_chunk;
  void	           *v_chunk;

#ifdef HAVE_MLIB
  uint8_t          *mlib_buffer;
  uint8_t          *mlib_resize_buffer;
  void	           *mlib_chunk;
  void	           *mlib_resize_chunk;
  mlib_filter      mlib_filter_type;
#endif

  void            **table_rV;
  void            **table_gU;
  int              *table_gV;
  void            **table_bU;
  void             *table_mmx;

  uint8_t          *cmap;
  scale_line_func_t scale_line;
} ;

/*
 * convenience class to easily create a lot of converters
 */

struct yuv2rgb_factory_s {
  yuv2rgb_t* (*create_converter) (yuv2rgb_factory_t *this);

  /*
   * set color space conversion levels
   * for all converters produced by this factory
   */
  void (*set_csc_levels) (yuv2rgb_factory_t *this,
    int brightness, int contrast, int saturation, int colormatrix);

  /*
   * free resources
   */
  void (*dispose) (yuv2rgb_factory_t *this);

  /* private data */

  int      mode;
  int      swapped;
  uint8_t *cmap;

  void    *table_base;
  void    *table_rV[256];
  void    *table_gU[256];
  int      table_gV[256];
  void    *table_bU[256];
  void    *table_mmx_base;
  void    *table_mmx;

  /* preselected functions for mode/swap/hardware */
  yuv2rgb_fun_t               yuv2rgb_fun;
  yuy22rgb_fun_t              yuy22rgb_fun;
  yuv2rgb_single_pixel_fun_t  yuv2rgb_single_pixel_fun;
};

yuv2rgb_factory_t *yuv2rgb_factory_init (int mode, int swapped, uint8_t *colormap);


/*
 * internal stuff below this line
 */

void mmx_yuv2rgb_set_csc_levels(yuv2rgb_factory_t *this,
  int brightness, int contrast, int saturation, int colormatrix);
void yuv2rgb_init_mmxext (yuv2rgb_factory_t *this);
void yuv2rgb_init_mmx (yuv2rgb_factory_t *this);
void yuv2rgb_init_mlib (yuv2rgb_factory_t *this);

#endif
