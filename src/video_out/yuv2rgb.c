/*
 * yuv2rgb.c
 *
 * This file is part of xine, a unix video player.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: yuv2rgb.c,v 1.17 2001/09/21 14:34:58 jkeil Exp $
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "yuv2rgb.h"
#include "attributes.h"
#include "cpu_accel.h"
#include "monitor.h"
#include "utils.h"


static int prof_scale_line = -1;

static scale_line_func_t find_scale_line_func(int step);


const int32_t Inverse_Table_6_9[8][4] = {
  {117504, 138453, 13954, 34903}, /* no sequence_display_extension */
  {117504, 138453, 13954, 34903}, /* ITU-R Rec. 709 (1990) */
  {104597, 132201, 25675, 53279}, /* unspecified */
  {104597, 132201, 25675, 53279}, /* reserved */
  {104448, 132798, 24759, 53109}, /* FCC */
  {104597, 132201, 25675, 53279}, /* ITU-R Rec. 624-4 System B, G */
  {104597, 132201, 25675, 53279}, /* SMPTE 170M */
  {117579, 136230, 16907, 35559}  /* SMPTE 240M (1987) */
};


static void *my_malloc_aligned (size_t alignment, size_t size, void **chunk) {

  void *pMem;

  pMem = xmalloc (size+alignment);

  *chunk = pMem;

  while ((int) pMem % alignment)
    pMem++;

  return pMem;
}


int yuv2rgb_setup (yuv2rgb_t *this, 
		   int source_width, int source_height,
		   int y_stride, int uv_stride,
		   int dest_width, int dest_height,
		   int rgb_stride) {
  /*
  printf ("yuv2rgb setup (%d x %d => %d x %d)\n", source_width, source_height,
	  dest_width, dest_height);
	  */
  if (prof_scale_line == -1)
    prof_scale_line = profiler_allocate_slot("xshm scale line");

  this->source_width  = source_width;
  this->source_height = source_height;
  this->y_stride      = y_stride;
  this->uv_stride     = uv_stride;
  this->dest_width    = dest_width;
  this->dest_height   = dest_height;
  this->rgb_stride    = rgb_stride;
  
  if (this->y_chunk) {
    free (this->y_chunk);
    this->y_buffer = this->y_chunk = NULL;
  }
  if (this->u_chunk) {
    free (this->u_chunk);
    this->u_buffer = this->u_chunk = NULL;
  }
  if (this->v_chunk) {
    free (this->v_chunk);
    this->v_buffer = this->v_chunk = NULL;
  }

  
  this->step_dx = source_width  * 32768 / dest_width;
  this->step_dy = source_height * 32768 / dest_height;
    
  this->scale_line = find_scale_line_func(this->step_dx);

  if ((source_width == dest_width) && (source_height == dest_height)) {
    this->do_scale = 0;

    /*
     * space for two y-lines (for yuv2rgb_mlib)
     * u,v subsampled 2:1
     */
    this->y_buffer = my_malloc_aligned (16, 2*dest_width, &this->y_chunk);
    if (!this->y_buffer)
      return 0;
    this->u_buffer = my_malloc_aligned (16, (dest_width+1)/2, &this->u_chunk);
    if (!this->u_buffer)
      return 0;
    this->v_buffer = my_malloc_aligned (16, (dest_width+1)/2, &this->v_chunk);
    if (!this->v_buffer)
      return 0;

  } else {
    this->do_scale = 1;
    
    /*
     * space for two y-lines (for yuv2rgb_mlib)
     * u,v subsampled 2:1
     */
    this->y_buffer = my_malloc_aligned (16, 2*dest_width, &this->y_chunk);
    if (!this->y_buffer)
      return 0;
    this->u_buffer = my_malloc_aligned (16, (dest_width+1)/2, &this->u_chunk);
    if (!this->u_buffer)
      return 0;
    this->v_buffer = my_malloc_aligned (16, (dest_width+1)/2, &this->v_chunk);
    if (!this->v_buffer)
      return 0;
  }
  return 1;
}


static void scale_line_gen (uint8_t *source, uint8_t *dest,
			    int width, int step) {
  /*
   * scales a yuv source row to a dest row, with interpolation
   * (good quality, but slow)
   */
  int p1;
  int p2;
  int dx;

  profiler_start_count(prof_scale_line);

  p1 = *source++;
  p2 = *source++;
  dx = 0;

  while (width) {

    *dest = (p1 * (32768 - dx) + p2 * dx) / 32768;

    dx += step;
    while (dx > 32768) {
      dx -= 32768;
      p1 = p2;
      p2 = *source++;
    }

    dest ++;
    width --;
  }

  profiler_stop_count(prof_scale_line);
}

#if 0
static void scale_line_fast (uint8_t *source, uint8_t *dest,
			     int width, int step) {
  /*
   * scales a yuv source row to a dest row, without interpolation
   * (faster then scale_line_gen, but quality suffers especially for
   * large scale factors)
   */
  int dx;

  profiler_start_count(prof_scale_line);

  dx = 0;

  while (--width >= 0) {

    *dest++ = *source;

    dx += step;
    while (dx > 32768) {
      dx -= 32768;
      source++;
    }
  }

  profiler_stop_count(prof_scale_line);
}
#endif

/*
 * Interpolates 7 output pixels from 5 source pixels using shifts.
 * Useful for scaling an mpeg2 dvd input source to 16:9 format
 * (720 x 576 ==> 1008 x 576)
 *
 * Interpolated source pixels are at n*5/7 (n=0..6):
 * 0.0, 0.71428, 1.42857, 2.14285, 2.85714, 3.57142, 4.28571
 *
 * We use:
 * 0.0				source[0]
 * 0.71428 => 3/4 (0.75)	(1*source[0] + 3*source[1]) >> 2
 * 1.42857 => 1+3/8 (1.375)	(5*source[1] + 3*source[2]) >> 3
 * 2.14285 => 2+1/8 (2.125)	(7*source[2] + 1*source[3]) >> 3
 * 2.85714 => 2+7/8 (2.875)	(1*source[2] + 7*source[3]) >> 3
 * 3.57142 => 3+5/8 (3.625)	(3*source[3] + 5*source[4]) >> 3
 * 4.28571 => 4+1/4 (4.25)	(3*source[4] + 1*source[5]) >> 2
 */
static void scale_line_5_7 (uint8_t *source, uint8_t *dest,
			    int width, int step) {

  int p1, p2;

  profiler_start_count(prof_scale_line);

  p1 = source[0];
  while ((width -= 7) >= 0) {
    dest[0] = p1;
    p2 = source[1];
    dest[1] = (1*p1 + 3*p2) >> 2;
    p1 = source[2];
    dest[2] = (5*p2 + 3*p1) >> 3;
    p2 = source[3];
    dest[3] = (7*p1 + 1*p2) >> 3;
    dest[4] = (1*p1 + 7*p2) >> 3;
    p1 = source[4];
    dest[5] = (3*p2 + 5*p1) >> 3;
    p2 = source[5];
    dest[6] = (3*p1 + 1*p2) >> 2;
    source += 5;
    dest += 7;
    p1 = p2;
  }

  switch (width + 7) {
  case 6: *dest++ = source[0];
  case 5: *dest++ = (1*source[0] + 3*source[1]) >> 2;
  case 4: *dest++ = (5*source[1] + 3*source[2]) >> 3;
  case 3: *dest++ = (7*source[2] + 1*source[3]) >> 3;
  case 2: *dest++ = (1*source[2] + 7*source[3]) >> 3;
  case 1: *dest++ = (3*source[3] + 5*source[4]) >> 3;
  }

  profiler_stop_count(prof_scale_line);
}

			
/*
 * Interpolates 16 output pixels from 15 source pixels using shifts.
 * Useful for scaling an mpeg2 dvd input source to 4:3 format
 * (720 x 576 ==> 768 x 576)
 */
static void scale_line_15_16 (uint8_t *source, uint8_t *dest,
			      int width, int step) {

  int p1, p2;

  profiler_start_count(prof_scale_line);

  while ((width -= 16) >= 0) {
    p1 = source[0];
    dest[0] = p1;
    p2 = source[1];
    dest[1] = (1*p1 + 7*p2) >> 3;
    p1 = source[2];
    dest[2] = (1*p2 + 7*p1) >> 3;
    p2 = source[3];
    dest[3] = (1*p1 + 3*p2) >> 2;
    p1 = source[4];
    dest[4] = (1*p2 + 3*p1) >> 2;
    p2 = source[5];
    dest[5] = (3*p1 + 5*p2) >> 3;
    p1 = source[6];
    dest[6] = (3*p2 + 5*p1) >> 3;
    p2 = source[7];
    dest[7] = (1*p1 + 1*p1) >> 1;
    p1 = source[8];
    dest[8] = (1*p2 + 1*p1) >> 1;
    p2 = source[9];
    dest[9] = (5*p1 + 3*p2) >> 3;
    p1 = source[10];
    dest[10] = (5*p2 + 3*p1) >> 3;
    p2 = source[11];
    dest[11] = (3*p1 + 1*p2) >> 2;
    p1 = source[12];
    dest[12] = (3*p2 + 1*p1) >> 2;
    p2 = source[13];
    dest[13] = (7*p1 + 1*p2) >> 3;
    p1 = source[14];
    dest[14] = (7*p2 + 1*p1) >> 3;
    dest[15] = p1;
    source += 15;
    dest += 16;
  }

  switch (width + 16) {
  case 15: *dest++ = source[0];
  case 14: *dest++ = (1*source[0] + 7*source[1]) >> 3;
  case 13: *dest++ = (1*source[1] + 7*source[2]) >> 3;
  case 12: *dest++ = (1*source[2] + 3*source[3]) >> 2;
  case 11: *dest++ = (1*source[3] + 3*source[4]) >> 2;
  case 10: *dest++ = (3*source[4] + 5*source[5]) >> 3;
  case  9: *dest++ = (3*source[5] + 5*source[6]) >> 3;
  case  8: *dest++ = (1*source[6] + 1*source[7]) >> 1;
  case  7: *dest++ = (1*source[7] + 1*source[8]) >> 1;
  case  6: *dest++ = (5*source[8] + 3*source[9]) >> 3;
  case  5: *dest++ = (5*source[9] + 3*source[10]) >> 3;
  case  4: *dest++ = (3*source[10] + 1*source[11]) >> 2;
  case  3: *dest++ = (3*source[11] + 1*source[12]) >> 2;
  case  2: *dest++ = (7*source[12] + 1*source[13]) >> 3;
  case  1: *dest++ = (7*source[13] + 1*source[14]) >> 3;
  }

  profiler_stop_count(prof_scale_line);
}

			
static void scale_line_1_2 (uint8_t *source, uint8_t *dest,
			    int width, int step) {
  int p1, p2;

  /* Interpolates 2 output pixels from one source pixels. */

  profiler_start_count(prof_scale_line);

  p1 = *source;
  while ((width -= 4) >= 0) {
    *dest++ = p1;
    p2 = *++source;
    *dest++ = (p1 + p2) >> 1;
    *dest++ = p2;
    p1 = *++source;
    *dest++ = (p2 + p1) >> 1;
  }

  switch (width + 4) {
  case 3: *dest++ = source[0];
  case 2: *dest++ = (source[0] + source[1]) >> 1;
  case 1: *dest++ = source[1];
  }

  profiler_stop_count(prof_scale_line);
}

			
static scale_line_func_t find_scale_line_func(int step) {
#if 1
  if (abs(step - 5*32768/7) < 10) {
    printf("yuv2rgb: using dvd 16:9 optimized scale_line\n");
    return scale_line_5_7;
  }
  if (abs(step - 15*32768/16) < 10) {
    printf("yuv2rgb: using dvd 4:3 optimized scale_line\n");
    return scale_line_15_16;
  }
  if (abs(step - 1*32768/2) < 10) {
    printf("yuv2rgb: using optimized 2*zoom scale_line\n");
    return scale_line_1_2;
  }
  printf("yuv2rgb: using generic scale_line with interpolation\n");
  return scale_line_gen;
#else
  printf("yuv2rgb: using generic scale_line without interpolation\n");
  return scale_line_fast;
#endif
}


static void scale_line_2 (uint8_t *source, uint8_t *dest,
			  int width, int step) {
  int p1;
  int p2;
  int dx;

  p1 = *source; source+=2;
  p2 = *source; source+=2;
  dx = 0;

  while (width) {

    *dest = (p1 * (32768 - dx) + p2 * dx) / 32768;

    dx += step;
    while (dx > 32768) {
      dx -= 32768;
      p1 = p2;
      p2 = *source;
      source+=2;
    }

    dest ++;
    width --;
  }
}

static void scale_line_4 (uint8_t *source, uint8_t *dest,
			  int width, int step) {
  int p1;
  int p2;
  int dx;

  p1 = *source; source+=4;
  p2 = *source; source+=4;
  dx = 0;

  while (width) {

    *dest = (p1 * (32768 - dx) + p2 * dx) / 32768;

    dx += step;
    while (dx > 32768) {
      dx -= 32768;
      p1 = p2;
      p2 = *source;
      source+=4;
    }

    dest ++;
    width --;
  }
}
			

#define RGB(i)							\
	U = pu[i];						\
	V = pv[i];						\
	r = this->table_rV[V];					\
	g = (void *) (((uint8_t *)this->table_gU[U]) + this->table_gV[V]);	\
	b = this->table_bU[U];

#define DST1(i)					\
	Y = py_1[2*i];                          \
	dst_1[2*i] = r[Y] + g[Y] + b[Y];	\
	Y = py_1[2*i+1];			\
	dst_1[2*i+1] = r[Y] + g[Y] + b[Y];

#define DST2(i)					\
	Y = py_2[2*i];				\
	dst_2[2*i] = r[Y] + g[Y] + b[Y];	\
	Y = py_2[2*i+1];			\
	dst_2[2*i+1] = r[Y] + g[Y] + b[Y];

#define DST1RGB(i)							\
	Y = py_1[2*i];							\
	dst_1[6*i] = r[Y]; dst_1[6*i+1] = g[Y]; dst_1[6*i+2] = b[Y];	\
	Y = py_1[2*i+1];						\
	dst_1[6*i+3] = r[Y]; dst_1[6*i+4] = g[Y]; dst_1[6*i+5] = b[Y];

#define DST2RGB(i)							\
	Y = py_2[2*i];							\
	dst_2[6*i] = r[Y]; dst_2[6*i+1] = g[Y]; dst_2[6*i+2] = b[Y];	\
	Y = py_2[2*i+1];						\
	dst_2[6*i+3] = r[Y]; dst_2[6*i+4] = g[Y]; dst_2[6*i+5] = b[Y];

#define DST1BGR(i)							\
	Y = py_1[2*i];							\
	dst_1[6*i] = b[Y]; dst_1[6*i+1] = g[Y]; dst_1[6*i+2] = r[Y];	\
	Y = py_1[2*i+1];						\
	dst_1[6*i+3] = b[Y]; dst_1[6*i+4] = g[Y]; dst_1[6*i+5] = r[Y];

#define DST2BGR(i)							\
	Y = py_2[2*i];							\
	dst_2[6*i] = b[Y]; dst_2[6*i+1] = g[Y]; dst_2[6*i+2] = r[Y];	\
	Y = py_2[2*i+1];						\
	dst_2[6*i+3] = b[Y]; dst_2[6*i+4] = g[Y]; dst_2[6*i+5] = r[Y];

static void yuv2rgb_c_32 (yuv2rgb_t *this, uint8_t * _dst,
			  uint8_t * _py, uint8_t * _pu, uint8_t * _pv)
{
  int U, V, Y;
  uint8_t  * py_1, * py_2, * pu, * pv;
  uint32_t * r, * g, * b;
  uint32_t * dst_1, * dst_2;
  int width, height, dst_height;
  int dy;

  if (this->do_scale) {
    scale_line_func_t scale_line = this->scale_line;

    scale_line (_pu, this->u_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_pv, this->v_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_py, this->y_buffer, 
		this->dest_width, this->step_dx);

    dy = 0;
    dst_height = this->dest_height;

    for (height = 0;; height++) {
      dst_1 = (uint32_t*)_dst;
      py_1  = this->y_buffer;
      pu    = this->u_buffer;
      pv    = this->v_buffer;

      width = this->dest_width >> 3;

      do {
	  RGB(0);
	  DST1(0);

	  RGB(1);
	  DST1(1);
      
	  RGB(2);
	  DST1(2);

	  RGB(3);
	  DST1(3);

	  pu += 4;
	  pv += 4;
	  py_1 += 8;
	  dst_1 += 8;
      } while (--width);

      dy += this->step_dy;
      _dst += this->rgb_stride;

      while (--dst_height > 0 && dy < 32768) {

	memcpy (_dst, (uint8_t*)_dst-this->rgb_stride, this->dest_width*4); 

	dy += this->step_dy;
	_dst += this->rgb_stride;
      }

      if (dst_height <= 0)
	break;

      dy -= 32768;
      _py += this->y_stride;

      scale_line (_py, this->y_buffer, 
		  this->dest_width, this->step_dx);

      if (height & 1) {
	_pu += this->uv_stride;
	_pv += this->uv_stride;
	  
	scale_line (_pu, this->u_buffer,
		    this->dest_width >> 1, this->step_dx);
	scale_line (_pv, this->v_buffer,
		    this->dest_width >> 1, this->step_dx);
	  
      }
    }
  } else {
    height = this->source_height >> 1;
    do {
      dst_1 = (uint32_t*)_dst;
      dst_2 = (void*)( (uint8_t *)_dst + this->rgb_stride );
      py_1 = _py;
      py_2 = _py + this->y_stride;
      pu   = _pu;
      pv   = _pv;

      width = this->source_width >> 3;
      do {
	RGB(0);
	DST1(0);
	DST2(0);

	RGB(1);
	DST2(1);
	DST1(1);

	RGB(2);
	DST1(2);
	DST2(2);

	RGB(3);
	DST2(3);
	DST1(3);
      
	pu += 4;
	pv += 4;
	py_1 += 8;
	py_2 += 8;
	dst_1 += 8;
	dst_2 += 8;
      } while (--width);

      _dst += 2 * this->rgb_stride; 
      _py += 2 * this->y_stride;
      _pu += this->uv_stride;
      _pv += this->uv_stride;

    } while (--height);
  }
}

/* This is very near from the yuv2rgb_c_32 code */
static void yuv2rgb_c_24_rgb (yuv2rgb_t *this, uint8_t * _dst,
			      uint8_t * _py, uint8_t * _pu, uint8_t * _pv)
{
  int U, V, Y;
  uint8_t * py_1, * py_2, * pu, * pv;
  uint8_t * r, * g, * b;
  uint8_t * dst_1, * dst_2;
  int width, height, dst_height;
  int dy;

  if (this->do_scale) {

    scale_line_func_t scale_line = this->scale_line;

    scale_line (_pu, this->u_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_pv, this->v_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_py, this->y_buffer, 
		this->dest_width, this->step_dx);

    dy = 0;
    dst_height = this->dest_height;

    for (height = 0;; height++) {
      dst_1 = _dst;
      py_1  = this->y_buffer;
      pu    = this->u_buffer;
      pv    = this->v_buffer;

      width = this->dest_width >> 3;

      do {
	  RGB(0);
	  DST1RGB(0);

	  RGB(1);
	  DST1RGB(1);
      
	  RGB(2);
	  DST1RGB(2);

	  RGB(3);
	  DST1RGB(3);

	  pu += 4;
	  pv += 4;
	  py_1 += 8;
	  dst_1 += 24;
      } while (--width);

      dy += this->step_dy;
      _dst += this->rgb_stride;

      while (--dst_height > 0 && dy < 32768) {

	memcpy (_dst, _dst-this->rgb_stride, this->dest_width*3); 

	dy += this->step_dy;
	_dst += this->rgb_stride;
      }

      if (dst_height <= 0)
	break;

      dy -= 32768;
      _py += this->y_stride;

      scale_line (_py, this->y_buffer, 
		  this->dest_width, this->step_dx);

      if (height & 1) {
	_pu += this->uv_stride;
	_pv += this->uv_stride;
	  
	scale_line (_pu, this->u_buffer,
		    this->dest_width >> 1, this->step_dx);
	scale_line (_pv, this->v_buffer,
		    this->dest_width >> 1, this->step_dx);
	  
      }
    }
  } else {
    height = this->source_height >> 1;
    do {
      dst_1 = _dst;
      dst_2 = (void*)( (uint8_t *)_dst + this->rgb_stride );
      py_1  = _py;
      py_2  = _py + this->y_stride;
      pu    = _pu;
      pv    = _pv;

      width = this->source_width >> 3;
      do {
	RGB(0);
	DST1RGB(0);
	DST2RGB(0);

	RGB(1);
	DST2RGB(1);
	DST1RGB(1);

	RGB(2);
	DST1RGB(2);
	DST2RGB(2);

	RGB(3);
	DST2RGB(3);
	DST1RGB(3);

	pu += 4;
	pv += 4;
	py_1 += 8;
	py_2 += 8;
	dst_1 += 24;
	dst_2 += 24;
      } while (--width);

      _dst += 2 * this->rgb_stride; 
      _py += 2 * this->y_stride;
      _pu += this->uv_stride;
      _pv += this->uv_stride;
      
    } while (--height);
  }
}

/* only trivial mods from yuv2rgb_c_24_rgb */
static void yuv2rgb_c_24_bgr (yuv2rgb_t *this, uint8_t * _dst,
			      uint8_t * _py, uint8_t * _pu, uint8_t * _pv)
{
  int U, V, Y;
  uint8_t * py_1, * py_2, * pu, * pv;
  uint8_t * r, * g, * b;
  uint8_t * dst_1, * dst_2;
  int width, height, dst_height;
  int dy;

  if (this->do_scale) {

    scale_line_func_t scale_line = this->scale_line;

    scale_line (_pu, this->u_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_pv, this->v_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_py, this->y_buffer, 
		this->dest_width, this->step_dx);

    dy = 0;
    dst_height = this->dest_height;

    for (height = 0;; height++) {
      dst_1 = _dst;
      py_1  = this->y_buffer;
      pu    = this->u_buffer;
      pv    = this->v_buffer;

      width = this->dest_width >> 3;

      do {
	  RGB(0);
	  DST1BGR(0);

	  RGB(1);
	  DST1BGR(1);
      
	  RGB(2);
	  DST1BGR(2);

	  RGB(3);
	  DST1BGR(3);

	  pu += 4;
	  pv += 4;
	  py_1 += 8;
	  dst_1 += 24;
      } while (--width);

      dy += this->step_dy;
      _dst += this->rgb_stride;

      while (--dst_height > 0 && dy < 32768) {

	memcpy (_dst, _dst-this->rgb_stride, this->dest_width*3); 

	dy += this->step_dy;
	_dst += this->rgb_stride;
      }

      if (dst_height <= 0)
	break;

      dy -= 32768;
      _py += this->y_stride;

      scale_line (_py, this->y_buffer, 
		  this->dest_width, this->step_dx);

      if (height & 1) {
	_pu += this->uv_stride;
	_pv += this->uv_stride;
	  
	scale_line (_pu, this->u_buffer,
		    this->dest_width >> 1, this->step_dx);
	scale_line (_pv, this->v_buffer,
		    this->dest_width >> 1, this->step_dx);
	  
      }
    }

  } else {
    height = this->source_height >> 1;
    do {
      dst_1 = _dst;
      dst_2 = (void*)( (uint8_t *)_dst + this->rgb_stride );
      py_1 = _py;
      py_2 = _py + this->y_stride;
      pu   = _pu;
      pv   = _pv;
      width = this->source_width >> 3;
      do {
	RGB(0);
	DST1BGR(0);
	DST2BGR(0);

	RGB(1);
	DST2BGR(1);
	DST1BGR(1);

	RGB(2);
	DST1BGR(2);
	DST2BGR(2);

	RGB(3);
	DST2BGR(3);
	DST1BGR(3);

	pu += 4;
	pv += 4;
	py_1 += 8;
	py_2 += 8;
	dst_1 += 24;
	dst_2 += 24;
      } while (--width);

      _dst += 2 * this->rgb_stride; 
      _py += 2 * this->y_stride;
      _pu += this->uv_stride;
      _pv += this->uv_stride;

    } while (--height);
  }
}

/* This is exactly the same code as yuv2rgb_c_32 except for the types of */
/* r, g, b, dst_1, dst_2 */
static void yuv2rgb_c_16 (yuv2rgb_t *this, uint8_t * _dst,
			  uint8_t * _py, uint8_t * _pu, uint8_t * _pv)
{
  int U, V, Y;
  uint8_t * py_1, * py_2, * pu, * pv;
  uint16_t * r, * g, * b;
  uint16_t * dst_1, * dst_2;
  int width, height, dst_height;
  int dy;

  if (this->do_scale) {
    scale_line_func_t scale_line = this->scale_line;

    scale_line (_pu, this->u_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_pv, this->v_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_py, this->y_buffer, 
		this->dest_width, this->step_dx);

    dy = 0;
    dst_height = this->dest_height;

    for (height = 0;; height++) {
      dst_1 = (uint16_t*)_dst;
      py_1  = this->y_buffer;
      pu    = this->u_buffer;
      pv    = this->v_buffer;

      width = this->dest_width >> 3;

      do {
	  RGB(0);
	  DST1(0);

	  RGB(1);
	  DST1(1);
      
	  RGB(2);
	  DST1(2);

	  RGB(3);
	  DST1(3);

	  pu += 4;
	  pv += 4;
	  py_1 += 8;
	  dst_1 += 8;
      } while (--width);

      dy += this->step_dy;
      _dst += this->rgb_stride;

      while (--dst_height > 0 && dy < 32768) {

	memcpy (_dst, (uint8_t*)_dst-this->rgb_stride, this->dest_width*2); 

	dy += this->step_dy;
	_dst += this->rgb_stride;
      }

      if (dst_height <= 0)
	break;

      dy -= 32768;
      _py += this->y_stride;

      scale_line (_py, this->y_buffer, 
		  this->dest_width, this->step_dx);

      if (height & 1) {
	_pu += this->uv_stride;
	_pv += this->uv_stride;
	  
	scale_line (_pu, this->u_buffer,
		    this->dest_width >> 1, this->step_dx);
	scale_line (_pv, this->v_buffer,
		    this->dest_width >> 1, this->step_dx);
	  
      }
    }
  } else {
    height = this->source_height >> 1;
    do {
      dst_1 = (uint16_t*)_dst;
      dst_2 = (void*)( (uint8_t *)_dst + this->rgb_stride );
      py_1 = _py;
      py_2 = _py + this->y_stride;
      pu   = _pu;
      pv   = _pv;
      width = this->source_width >> 3;
      do {
	RGB(0);
	DST1(0);
	DST2(0);

	RGB(1);
	DST2(1);
	DST1(1);

	RGB(2);
	DST1(2);
	DST2(2);

	RGB(3);
	DST2(3);
	DST1(3);

	pu += 4;
	pv += 4;
	py_1 += 8;
	py_2 += 8;
	dst_1 += 8;
	dst_2 += 8;
      } while (--width);

      _dst += 2 * this->rgb_stride; 
      _py += 2 * this->y_stride;
      _pu += this->uv_stride;
      _pv += this->uv_stride;

    } while (--height);
  }
}

/* now for something different: 256 color mode */
static void yuv2rgb_c_palette (yuv2rgb_t *this, uint8_t * _dst,
			       uint8_t * _py, uint8_t * _pu, uint8_t * _pv)
{
  uint8_t * py_1, * py_2, * pu, * pv;
  uint8_t * dst_1, * dst_2;
  int width, height, dst_height;
  int dy;

  if (this->do_scale) {
    scale_line_func_t scale_line = this->scale_line;

    scale_line (_pu, this->u_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_pv, this->v_buffer,
		this->dest_width >> 1, this->step_dx);
    scale_line (_py, this->y_buffer, 
		this->dest_width, this->step_dx);

    dy = 0;
    dst_height = this->dest_height;

    for (height = 0;; height++) {
      dst_1 = _dst;
      py_1  = this->y_buffer;
      pu    = this->u_buffer;
      pv    = this->v_buffer;

      width = this->dest_width >> 3;

      do {
	/* FIXME
	  RGB(0);
	  DST1(0);

	  RGB(1);
	  DST1(1);
      
	  RGB(2);
	  DST1(2);

	  RGB(3);
	  DST1(3);
	  */

	  pu += 4;
	  pv += 4;
	  py_1 += 8;
	  dst_1 += 8;
      } while (--width);

      dy += this->step_dy;
      _dst += this->rgb_stride;

      while (--dst_height > 0 && dy < 32768) {

	memcpy (_dst, (uint8_t*)_dst-this->rgb_stride, this->dest_width*2); 

	dy += this->step_dy;
	_dst += this->rgb_stride;
      }

      if (dst_height <= 0)
	break;

      dy -= 32768;
      _py += this->y_stride;

      scale_line (_py, this->y_buffer, 
		  this->dest_width, this->step_dx);

      if (height & 1) {
	_pu += this->uv_stride;
	_pv += this->uv_stride;
	  
	scale_line (_pu, this->u_buffer,
		    this->dest_width >> 1, this->step_dx);
	scale_line (_pv, this->v_buffer,
		    this->dest_width >> 1, this->step_dx);
	  
      }
    }
  } else {
    height = this->source_height >> 1;
    do {
      dst_1 = _dst;
      dst_2 = _dst + this->rgb_stride;
      py_1 = _py;
      py_2 = _py + this->y_stride;
      pu   = _pu;
      pv   = _pv;
      width = this->source_width >> 3;
      do {
	/*
	RGB(0);
	DST1(0);
	DST2(0);

	RGB(1);
	DST2(1);
	DST1(1);

	RGB(2);
	DST1(2);
	DST2(2);

	RGB(3);
	DST2(3);
	DST1(3);
	*/

	pu += 4;
	pv += 4;
	py_1 += 8;
	py_2 += 8;
	dst_1 += 8;
	dst_2 += 8;
      } while (--width);

      _dst += 2 * this->rgb_stride; 
      _py += 2 * this->y_stride;
      _pu += this->uv_stride;
      _pv += this->uv_stride;

    } while (--height);
  }
}

static int div_round (int dividend, int divisor)
{
  if (dividend > 0)
    return (dividend + (divisor>>1)) / divisor;
  else
    return -((-dividend + (divisor>>1)) / divisor);
}

static void yuv2rgb_setup_tables (yuv2rgb_t *this, int mode, int swapped) 
{
  int i;
  uint8_t table_Y[1024];
  uint32_t * table_32 = 0;
  uint16_t * table_16 = 0;
  uint8_t * table_8 = 0;
  int entry_size = 0;
  void *table_r = 0, *table_g = 0, *table_b = 0;
  int shift_r = 0, shift_g = 0, shift_b = 0;

  int crv = Inverse_Table_6_9[this->matrix_coefficients][0];
  int cbu = Inverse_Table_6_9[this->matrix_coefficients][1];
  int cgu = -Inverse_Table_6_9[this->matrix_coefficients][2];
  int cgv = -Inverse_Table_6_9[this->matrix_coefficients][3];

  for (i = 0; i < 1024; i++) {
    int j;

    j = (76309 * (i - 384 - 16) + 32768) >> 16;
    j = (j < 0) ? 0 : ((j > 255) ? 255 : j);
    table_Y[i] = j;
  }

  switch (mode) {
  case MODE_32_RGB:
  case MODE_32_BGR:
    table_32 = malloc ((197 + 2*682 + 256 + 132) * sizeof (uint32_t));

    entry_size = sizeof (uint32_t);
    table_r = table_32 + 197;
    table_b = table_32 + 197 + 685;
    table_g = table_32 + 197 + 2*682;

    if (swapped) {
      switch (mode) {
      case MODE_32_RGB: shift_r =  8; shift_g = 16; shift_b = 24; break;
      case MODE_32_BGR:	shift_r = 24; shift_g = 16; shift_b =  8; break;
      }
    } else {
      switch (mode) {
      case MODE_32_RGB:	shift_r = 16; shift_g =  8; shift_b =  0; break;
      case MODE_32_BGR:	shift_r =  0; shift_g =  8; shift_b = 16; break;
      }
    }

    for (i = -197; i < 256+197; i++)
      ((uint32_t *) table_r)[i] = table_Y[i+384] << shift_r;
    for (i = -132; i < 256+132; i++)
      ((uint32_t *) table_g)[i] = table_Y[i+384] << shift_g;
    for (i = -232; i < 256+232; i++)
      ((uint32_t *) table_b)[i] = table_Y[i+384] << shift_b;
    break;

  case MODE_24_RGB:
  case MODE_24_BGR:
    table_8 = malloc ((256 + 2*232) * sizeof (uint8_t));

    entry_size = sizeof (uint8_t);
    table_r = table_g = table_b = table_8 + 232;

    for (i = -232; i < 256+232; i++)
      ((uint8_t * )table_b)[i] = table_Y[i+384];
    break;

  case MODE_15_BGR:
  case MODE_16_BGR:
  case MODE_15_RGB:
  case MODE_16_RGB:
    table_16 = malloc ((197 + 2*682 + 256 + 132) * sizeof (uint16_t));

    entry_size = sizeof (uint16_t);
    table_r = table_16 + 197;
    table_b = table_16 + 197 + 685;
    table_g = table_16 + 197 + 2*682;

    if (swapped) {
      switch (mode) {
      case MODE_15_BGR: shift_r =  8; shift_g =  5; shift_b = 2; break;
      case MODE_16_BGR:	shift_r =  8; shift_g =  5; shift_b = 3; break;
      case MODE_15_RGB:	shift_r =  2; shift_g =  5; shift_b = 8; break;
      case MODE_16_RGB:	shift_r =  3; shift_g =  5; shift_b = 8; break;
      }
    } else {
      switch (mode) {
      case MODE_15_BGR:	shift_r =  0; shift_g =  5; shift_b = 10; break;
      case MODE_16_BGR:	shift_r =  0; shift_g =  5; shift_b = 11; break;
      case MODE_15_RGB:	shift_r = 10; shift_g =  5; shift_b =  0; break;
      case MODE_16_RGB:	shift_r = 11; shift_g =  5; shift_b =  0; break;
      }
    }

    for (i = -197; i < 256+197; i++)
      ((uint16_t *)table_r)[i] = (table_Y[i+384] >> 3) << shift_r;

    for (i = -132; i < 256+132; i++) {
      int j = table_Y[i+384] >> (((mode==MODE_16_RGB) || (mode==MODE_16_BGR)) ? 2 : 3);
      if (swapped)
	((uint16_t *)table_g)[i] = (j&7) << 13 | (j>>3);
      else
	((uint16_t *)table_g)[i] = j << 5;
    }
    for (i = -232; i < 256+232; i++)
      ((uint16_t *)table_b)[i] = (table_Y[i+384] >> 3) << shift_b;

    break;

  case MODE_PALETTE:

    /* FIXME: set up 256 color table */

    break;

  default:
    fprintf (stderr, "mode %d not supported by yuv2rgb\n", mode);
    exit (1);
  }

  for (i = 0; i < 256; i++) {
    this->table_rV[i] = (((uint8_t *) table_r) +
			 entry_size * div_round (crv * (i-128), 76309));
    this->table_gU[i] = (((uint8_t *) table_g) +
			 entry_size * div_round (cgu * (i-128), 76309));
    this->table_gV[i] = entry_size * div_round (cgv * (i-128), 76309);
    this->table_bU[i] = (((uint8_t *)table_b) +
			 entry_size * div_round (cbu * (i-128), 76309));
  }
}

static void yuv2rgb_c_init (yuv2rgb_t *this, int mode, int swapped)
{  
  switch (mode) {
  case MODE_32_RGB:
  case MODE_32_BGR:
    this->yuv2rgb_fun = yuv2rgb_c_32;
    break;

  case MODE_24_RGB:
  case MODE_24_BGR:
    this->yuv2rgb_fun =
	(mode==MODE_24_RGB && !swapped) || (mode==MODE_24_BGR && swapped) 
	    ? yuv2rgb_c_24_rgb
	    : yuv2rgb_c_24_bgr;
    break;

  case MODE_15_BGR:
  case MODE_16_BGR:
  case MODE_15_RGB:
  case MODE_16_RGB:
    this->yuv2rgb_fun = yuv2rgb_c_16;
    break;

  case MODE_PALETTE:
    this->yuv2rgb_fun = yuv2rgb_c_palette;
    break;

  default:
    fprintf (stderr, "mode %d not supported by yuv2rgb\n", mode);
    exit (1);
  }

}


/*
 * yuy2 stuff
 */

static void yuy22rgb_c_32 (yuv2rgb_t *this, uint8_t * _dst, uint8_t * _p)
{
  int U, V, Y;
  uint8_t * py_1, * pu, * pv;
  uint32_t * r, * g, * b;
  uint32_t * dst_1;
  int width, height;
  int dy;

  /* FIXME: implement unscaled version */

  scale_line_4 (_p+1, this->u_buffer,
		this->dest_width >> 1, this->step_dx);
  scale_line_4 (_p+3, this->v_buffer,
		this->dest_width >> 1, this->step_dx);
  scale_line_2 (_p, this->y_buffer, 
		this->dest_width, this->step_dx);
  
  dy = 0;
  height = this->dest_height;
  
  for (;;) {
    dst_1 = (uint32_t*)_dst;
    py_1  = this->y_buffer;
    pu    = this->u_buffer;
    pv    = this->v_buffer;
    
    width = this->dest_width >> 3;

    do {

      RGB(0);
      DST1(0);
      
      RGB(1);
      DST1(1);
      
      RGB(2);
      DST1(2);
      
      RGB(3);
      DST1(3);
      
      pu += 4;
      pv += 4;
      py_1 += 8;
      dst_1 += 8;
    } while (--width);
    
    dy += this->step_dy;
    _dst += this->rgb_stride;
    
    while (--height > 0 && dy < 32768) {
      
      memcpy (_dst, (uint8_t*)_dst-this->rgb_stride, this->dest_width*4); 
      
      dy += this->step_dy;
      _dst += this->rgb_stride;
    }
    
    if (height <= 0)
      break;

    dy -= 32768;
    _p += this->y_stride*2;
    
    scale_line_4 (_p+1, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
    scale_line_4 (_p+3, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
    scale_line_2 (_p, this->y_buffer, 
		  this->dest_width, this->step_dx);
  }
}

static void yuy22rgb_c_24_rgb (yuv2rgb_t *this, uint8_t * _dst, uint8_t * _p)
{
  int U, V, Y;
  uint8_t * py_1, * pu, * pv;
  uint8_t * r, * g, * b;
  uint8_t * dst_1;
  int width, height;
  int dy;

  /* FIXME: implement unscaled version */

  scale_line_4 (_p+1, this->u_buffer,
		this->dest_width >> 1, this->step_dx);
  scale_line_4 (_p+3, this->v_buffer,
		this->dest_width >> 1, this->step_dx);
  scale_line_2 (_p, this->y_buffer, 
		this->dest_width, this->step_dx);
  
  dy = 0;
  height = this->dest_height;
  
  for (;;) {
    dst_1 = _dst;
    py_1  = this->y_buffer;
    pu    = this->u_buffer;
    pv    = this->v_buffer;
    
    width = this->dest_width >> 3;
    
    do {
      RGB(0);
      DST1RGB(0);
      
      RGB(1);
      DST1RGB(1);
      
      RGB(2);
      DST1RGB(2);
      
      RGB(3);
      DST1RGB(3);
      
      pu += 4;
      pv += 4;
      py_1 += 8;
      dst_1 += 24;
    } while (--width);
    
    dy += this->step_dy;
    _dst += this->rgb_stride;
    
    while (--height > 0 && dy < 32768) {
      
      memcpy (_dst, (uint8_t*)_dst-this->rgb_stride, this->dest_width*3); 
      
      dy += this->step_dy;
      _dst += this->rgb_stride;
    }
    
    if (height <= 0)
      break;

    dy -= 32768;
    _p += this->y_stride*2;
    
    scale_line_4 (_p+1, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
    scale_line_4 (_p+3, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
    scale_line_2 (_p, this->y_buffer, 
		  this->dest_width, this->step_dx);
  }
}

static void yuy22rgb_c_24_bgr (yuv2rgb_t *this, uint8_t * _dst, uint8_t * _p)
{
  int U, V, Y;
  uint8_t * py_1, * pu, * pv;
  uint8_t * r, * g, * b;
  uint8_t * dst_1;
  int width, height;
  int dy;

  /* FIXME: implement unscaled version */

  scale_line_4 (_p+1, this->u_buffer,
		this->dest_width >> 1, this->step_dx);
  scale_line_4 (_p+3, this->v_buffer,
		this->dest_width >> 1, this->step_dx);
  scale_line_2 (_p, this->y_buffer, 
		this->dest_width, this->step_dx);
  
  dy = 0;
  height = this->dest_height;
  
  for (;;) {
    dst_1 = _dst;
    py_1  = this->y_buffer;
    pu    = this->u_buffer;
    pv    = this->v_buffer;
    
    width = this->dest_width >> 3;
    
    do {
      RGB(0);
      DST1BGR(0);
      
      RGB(1);
      DST1BGR(1);
      
      RGB(2);
      DST1BGR(2);
      
      RGB(3);
      DST1BGR(3);
      
      pu += 4;
      pv += 4;
      py_1 += 8;
      dst_1 += 24;
    } while (--width);
    
    dy += this->step_dy;
    _dst += this->rgb_stride;
    
    while (--height > 0 && dy < 32768) {
      
      memcpy (_dst, (uint8_t*)_dst-this->rgb_stride, this->dest_width*3); 
      
      dy += this->step_dy;
      _dst += this->rgb_stride;
    }
    
    if (height <= 0)
      break;

    dy -= 32768;
    _p += this->y_stride*2;
    
    scale_line_4 (_p+1, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
    scale_line_4 (_p+3, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
    scale_line_2 (_p, this->y_buffer, 
		  this->dest_width, this->step_dx);
  }
}

static void yuy22rgb_c_16 (yuv2rgb_t *this, uint8_t * _dst, uint8_t * _p)
{
  int U, V, Y;
  uint8_t * py_1, * pu, * pv;
  uint16_t * r, * g, * b;
  uint16_t * dst_1;
  int width, height;
  int dy;

  /* FIXME: implement unscaled version */

  scale_line_4 (_p+1, this->u_buffer,
		this->dest_width >> 1, this->step_dx);
  scale_line_4 (_p+3, this->v_buffer,
		this->dest_width >> 1, this->step_dx);
  scale_line_2 (_p, this->y_buffer, 
		this->dest_width, this->step_dx);
  
  dy = 0;
  height = this->dest_height;
  
  for (;;) {
    dst_1 = (uint16_t*)_dst;
    py_1  = this->y_buffer;
    pu    = this->u_buffer;
    pv    = this->v_buffer;
    
    width = this->dest_width >> 3;
    
    do {
      RGB(0);
      DST1(0);
      
      RGB(1);
      DST1(1);
      
      RGB(2);
      DST1(2);
      
      RGB(3);
      DST1(3);
      
      pu += 4;
      pv += 4;
      py_1 += 8;
      dst_1 += 8;
    } while (--width);
    
    dy += this->step_dy;
    _dst += this->rgb_stride;
    
    while (--height > 0 && dy < 32768) {
      
      memcpy (_dst, (uint8_t*)_dst-this->rgb_stride, this->dest_width*2); 
      
      dy += this->step_dy;
      _dst += this->rgb_stride;
    }
    
    if (height <= 0)
      break;

    dy -= 32768;
    _p += this->y_stride*2;
    
    scale_line_4 (_p+1, this->u_buffer,
		  this->dest_width >> 1, this->step_dx);
    scale_line_4 (_p+3, this->v_buffer,
		  this->dest_width >> 1, this->step_dx);
    scale_line_2 (_p, this->y_buffer, 
		  this->dest_width, this->step_dx);
  }
}

static void yuy22rgb_c_init (yuv2rgb_t *this, int mode, int swapped)
{  
  switch (mode) {
  case MODE_32_RGB:
  case MODE_32_BGR:
    this->yuy22rgb_fun = yuy22rgb_c_32;
    break;

  case MODE_24_RGB:
  case MODE_24_BGR:
    this->yuy22rgb_fun =
	(mode==MODE_24_RGB && !swapped) || (mode==MODE_24_BGR && swapped)
	    ? yuy22rgb_c_24_rgb 
	    : yuy22rgb_c_24_bgr;
    break;
  case MODE_15_BGR:
  case MODE_16_BGR:
  case MODE_15_RGB:
  case MODE_16_RGB:
    this->yuy22rgb_fun = yuy22rgb_c_16;
    break;

  default:
    printf ("yuv2rgb: mode %d not supported for yuy2\n", mode);
  }
}

yuv2rgb_t *yuv2rgb_init (int mode, int swapped) {

#ifdef ARCH_X86
  uint32_t mm = mm_accel();
#endif
  yuv2rgb_t *this = xmalloc (sizeof (yuv2rgb_t));


  this->matrix_coefficients = 6;

  this->y_chunk = this->y_buffer = NULL;
  this->u_chunk = this->u_buffer = NULL;
  this->v_chunk = this->v_buffer = NULL;

  yuv2rgb_setup_tables(this, mode, swapped);

  /*
   * auto-probe for the best yuv2rgb function
   */
  
  this->yuv2rgb_fun = NULL;
#ifdef ARCH_X86
  if ((this->yuv2rgb_fun == NULL) && (mm & MM_ACCEL_X86_MMXEXT)) {
    yuv2rgb_init_mmxext (this, mode, swapped);
    if (this->yuv2rgb_fun != NULL)
      printf ("yuv2rgb: using MMXEXT for colorspace transform\n");
  }
  if ((this->yuv2rgb_fun == NULL) && (mm & MM_ACCEL_X86_MMX)) {
    yuv2rgb_init_mmx (this, mode, swapped);
    if (this->yuv2rgb_fun != NULL)
      printf ("yuv2rgb: using MMX for colorspace transform\n"); 
  }
#endif
#if HAVE_MLIB
  if (this->yuv2rgb_fun == NULL) {
    yuv2rgb_init_mlib (this, mode, swapped);
    if (this->yuv2rgb_fun != NULL)
      printf ("yuv2rgb: using medialib for colorspace transform\n"); 
  }
#endif
  if (this->yuv2rgb_fun == NULL) {
    printf ("yuv2rgb: no accelerated colorspace conversion found\n");
    yuv2rgb_c_init (this, mode, swapped);
  }

  /*
   * auto-probe for the best yuy22rgb function
   */

  /* FIXME: implement mmx/mlib functions */
  yuy22rgb_c_init (this, mode, swapped);

  return this;
}
