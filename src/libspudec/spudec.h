/*
 * spudec.h
 *
 * Copyright (C) Rich Wareham <rjw57@cam.ac.uk> - Jan 2001
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING. If not, write to
 * the Free Software Foundation, 
 *
 */

#ifndef HAVE_SPUDEC_H
#define HAVE_SPUDEC_H

#include "metronom.h"
#include "input/input_plugin.h"

typedef struct spudec_s spudec_t;

struct spudec_s {

  /*
   * reset spudec for a new stream
   *
   * clut     : pointer to array of 16 cluts for palette info 
   */

  void (*spudec_start) (spudec_t *this,	clut_t *clut);

  /*
   * overlay functions: spudec decodes all subpicture data until
   * it reaches the given vpts, then overlays the subpicture
   */

  void (*spudec_overlay_yuv) (spudec_t *this, uint32_t vpts, 
			      uint8_t *y, uint8_t *u, uint8_t *v);
  void (*spudec_overlay_rgb) (spudec_t *this, uint32_t vpts, 
			      uint8_t *rgb_data, int mode);
};

/*
 * generate a new subpicture decoder
 *
 * metronom : metronom for pts <-> vpts conversion
 * spu_fifo : fifo buffer where subpicture packages arrive
 */

spudec_t *spudec_init (metronom_t *metronom, fifo_buffer_t *spu_fifo);

#endif /* HAVE_SPUDEC_H */
