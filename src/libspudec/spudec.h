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
#include "xine_internal.h"

/* typedef struct spudec_s spudec_t; */
typedef struct spudec_priv_s spudec_priv_t;

struct spudec_s {

  /*
   * reset spudec for a new stream
   *
   * clut     : pointer to array of 16 cluts for palette info 
   *            or NULL to use default (not recommended!).
   */

  void (*start) (spudec_t *this, clut_t *clut);

  /* 
   * change the colour lookup table (clut) used by the decoder.
   *
   * clut     : pointer to array of 16 cluts for palette info 
   *            or NULL to use default (not recommended!).
   */

  void (*change_clut) (spudec_t *this, clut_t *clut);

  /*
   * pass a packet demux-ed from the MPEG2 stream to the decoder.
   * (the buffer is copied so may be 'free'-ed).
   *
   * buf      : a buf_element_t containing the packet.
   */
  void (*push_packet) (spudec_t *this, buf_element_t *buf);

  /*
   * overlay functions: spudec decodes all subpicture data until
   * it reaches the given vpts, then overlays the subpicture
   */

  void (*overlay_yuv) (spudec_t *this, uint32_t vpts, 
		       uint8_t *y, uint8_t *u, uint8_t *v);
  void (*overlay_rgb) (spudec_t *this, uint32_t vpts, 
		       uint8_t *rgb_data, int mode);

  /* PRIVATE DATA -- Not to be touched. */
  spudec_priv_t    *private;
};

/*
 * generate a new subpicture decoder
 *
 * metronom : metronom for pts <-> vpts conversion
 * spu_fifo : fifo buffer where subpicture packages arrive
 */

extern spudec_t *spudec_init (xine_t *xine);

/*
 * close a given subpicture decoder
 *
 * decoder  : The decoder previously returned by spudec_init
 */
extern void spudec_close (spudec_t *decoder);

#endif /* HAVE_SPUDEC_H */
