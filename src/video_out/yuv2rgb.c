/*
 * yuv2rgb.c
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
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "yuv2rgb.h"
#include "attributes.h"
#include "cpu_accel.h"
#include "utils.h"


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

static void yuv2rgb_c (yuv2rgb_t *this, uint8_t *image, 
		       uint8_t *py, uint8_t *pu, uint8_t *pv) {

  /* int dy = this->step_dy; */
  int height = this->source_height >>= 1;

  do {
    this->yuv2rgb_c_internal (this, py, py + this->y_stride, pu, pv,
			      image, ((uint8_t *)image) + this->rgb_stride, 
			      this->source_width);
    
    py += 2 * this->y_stride;
    pu += this->uv_stride;
    pv += this->uv_stride;
    image = ((uint8_t *) image) + 2 * this->rgb_stride;
  } while (--height);
}

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

  printf ("yuv2rgb setup (%d x %d => %d x %d)\n", source_width, source_height,
	  dest_width, dest_height);
  this->source_width  = source_width;
  this->source_height = source_height;
  this->y_stride      = y_stride;
  this->uv_stride     = uv_stride;
  this->dest_width    = dest_width;
  this->dest_height   = dest_height;
  this->rgb_stride    = rgb_stride;
  
  if ((source_width == dest_width) && (source_height == dest_height)) 
    this->do_scale = 0;
  else {
    this->do_scale = 1;
    
    this->step_dx = source_width  * 32768 / dest_width;
    this->step_dy = source_height * 32768 / dest_height;
    
    if (this->y_chunk) free (this->y_chunk);
    if (this->u_chunk) free (this->u_chunk);
    if (this->v_chunk) free (this->v_chunk);
    
    this->y_buffer = my_malloc_aligned (16, dest_width, &this->y_chunk);
    if (!this->y_buffer)
      return 0;
    this->u_buffer = my_malloc_aligned (16, dest_width, &this->u_chunk);
    if (!this->u_buffer)
      return 0;
    this->v_buffer = my_malloc_aligned (16, dest_width, &this->v_chunk);
    if (!this->v_buffer)
      return 0;
  }
  return 1;
}


#define RGB(i)							\
	U = pu[i];						\
	V = pv[i];						\
	r = this->table_rV[V];					\
	g = (void *) (((uint8_t *)this->table_gU[U]) + this->table_gV[V]);	\
	b = this->table_bU[U];

#define DST1(i)					\
	Y = py_1[2*i];				\
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

static void yuv2rgb_c_32 (yuv2rgb_t *this,
			  uint8_t * py_1, uint8_t * py_2,
			  uint8_t * pu, uint8_t * pv,
			  void * _dst_1, void * _dst_2, int width)
{
  int U, V, Y;
  uint32_t * r, * g, * b;
  uint32_t * dst_1, * dst_2;

  width >>= 3;
  dst_1 = _dst_1;
  dst_2 = _dst_2;

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
}

/* This is very near from the yuv2rgb_c_32 code */
static void yuv2rgb_c_24_rgb (yuv2rgb_t *this,
			      uint8_t * py_1, uint8_t * py_2,
			      uint8_t * pu, uint8_t * pv,
			      void * _dst_1, void * _dst_2, int width)
{
  int U, V, Y;
  uint8_t * r, * g, * b;
  uint8_t * dst_1, * dst_2;

  width >>= 3;
  dst_1 = _dst_1;
  dst_2 = _dst_2;

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
}

/* only trivial mods from yuv2rgb_c_24_rgb */
static void yuv2rgb_c_24_bgr (yuv2rgb_t *this,
			      uint8_t * py_1, uint8_t * py_2,
			      uint8_t * pu, uint8_t * pv,
			      void * _dst_1, void * _dst_2, int width)
{
  int U, V, Y;
  uint8_t * r, * g, * b;
  uint8_t * dst_1, * dst_2;

  width >>= 3;
  dst_1 = _dst_1;
  dst_2 = _dst_2;

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
}

/* This is exactly the same code as yuv2rgb_c_32 except for the types of */
/* r, g, b, dst_1, dst_2 */
static void yuv2rgb_c_16 (yuv2rgb_t *this,
			  uint8_t * py_1, uint8_t * py_2,
			  uint8_t * pu, uint8_t * pv,
			  void * _dst_1, void * _dst_2, int width)
{
  int U, V, Y;
  uint16_t * r, * g, * b;
  uint16_t * dst_1, * dst_2;

  width >>= 3;
  dst_1 = _dst_1;
  dst_2 = _dst_2;

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
}

static int div_round (int dividend, int divisor)
{
  if (dividend > 0)
    return (dividend + (divisor>>1)) / divisor;
  else
    return -((-dividend + (divisor>>1)) / divisor);
}

static void yuv2rgb_c_init (yuv2rgb_t *this, int mode)
{  
  int i;
  uint8_t table_Y[1024];
  uint32_t * table_32 = 0;
  uint16_t * table_16 = 0;
  uint8_t * table_8 = 0;
  int entry_size = 0;
  void *table_r = 0, *table_g = 0, *table_b = 0;

  int crv = Inverse_Table_6_9[this->matrix_coefficients][0];
  int cbu = Inverse_Table_6_9[this->matrix_coefficients][1];
  int cgu = -Inverse_Table_6_9[this->matrix_coefficients][2];
  int cgv = -Inverse_Table_6_9[this->matrix_coefficients][3];

  this->yuv2rgb_fun = yuv2rgb_c;

  for (i = 0; i < 1024; i++) {
    int j;

    j = (76309 * (i - 384 - 16) + 32768) >> 16;
    j = (j < 0) ? 0 : ((j > 255) ? 255 : j);
    table_Y[i] = j;
  }

  switch (mode) {
  case MODE_32_RGB:
  case MODE_32_BGR:
    this->yuv2rgb_c_internal = yuv2rgb_c_32;

    table_32 = malloc ((197 + 2*682 + 256 + 132) * sizeof (uint32_t));

    entry_size = sizeof (uint32_t);
    table_r = table_32 + 197;
    table_b = table_32 + 197 + 685;
    table_g = table_32 + 197 + 2*682;

    for (i = -197; i < 256+197; i++)
      ((uint32_t *) table_r)[i] =
	table_Y[i+384] << ((mode==MODE_32_RGB) ? 16 : 0);
    for (i = -132; i < 256+132; i++)
      ((uint32_t *) table_g)[i] = table_Y[i+384] << 8;
    for (i = -232; i < 256+232; i++)
      ((uint32_t *) table_b)[i] =
	table_Y[i+384] << ((mode==MODE_32_RGB) ? 0 : 16);
    break;

  case MODE_24_RGB:
  case MODE_24_BGR:
    this->yuv2rgb_c_internal = (mode==MODE_24_RGB) ? yuv2rgb_c_24_rgb : yuv2rgb_c_24_bgr;

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
    this->yuv2rgb_c_internal = yuv2rgb_c_16;

    table_16 = malloc ((197 + 2*682 + 256 + 132) * sizeof (uint16_t));

    entry_size = sizeof (uint16_t);
    table_r = table_16 + 197;
    table_b = table_16 + 197 + 685;
    table_g = table_16 + 197 + 2*682;

    for (i = -197; i < 256+197; i++) {
      int j = table_Y[i+384] >> 3;

      if (mode == MODE_16_RGB)
	j <<= 11; 
      else if (mode == MODE_15_RGB) 
	j <<= 10;

      ((uint16_t *)table_r)[i] = j;
    }
    for (i = -132; i < 256+132; i++) {
      int j = table_Y[i+384] >> (((mode==MODE_16_RGB) || (mode==MODE_16_BGR)) ? 2 : 3);

      ((uint16_t *)table_g)[i] = j << 5;
    }
    for (i = -232; i < 256+232; i++) {
      int j = table_Y[i+384] >> 3;

      if (mode == MODE_16_BGR)
	j <<= 11;
      if (mode == MODE_15_BGR)
	j <<= 10;

      ((uint16_t *)table_b)[i] = j;
    }
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

yuv2rgb_t *yuv2rgb_init (int mode) {

  uint32_t mm = mm_accel();
  yuv2rgb_t *this = xmalloc (sizeof (yuv2rgb_t));


  this->matrix_coefficients = 6;

  this->y_buffer = NULL;
  this->u_buffer = NULL;
  this->v_buffer = NULL;

  /*
   * auto-probe for the best yuv2rgb function
   */
  
  this->yuv2rgb_fun = NULL;
#ifdef ARCH_X86
  if ((this->yuv2rgb_fun == NULL) && (mm & MM_ACCEL_X86_MMXEXT)) {
    yuv2rgb_init_mmxext (this, mode);
    if (this->yuv2rgb_fun != NULL)
      printf ("yuv2rgb: using MMXEXT for colorspace transform\n");
  }
  if ((this->yuv2rgb_fun == NULL) && (mm & MM_ACCEL_X86_MMX)) {
    yuv2rgb_init_mmx (this, mode);
    if (this->yuv2rgb_fun != NULL)
      printf ("yuv2rgb: using MMX for colorspace transform\n"); 
  }
#endif
  if (this->yuv2rgb_fun == NULL) {
    printf ("yuv2rgb: no accelerated colorspace conversion found\n");
    yuv2rgb_c_init (this, mode);
  }
  return this;
}
