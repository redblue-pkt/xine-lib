/*
 * spudec.c
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

#include "spudec.h"

#include "xine_internal.h"
#include "utils.h"
#include "metronom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

struct spudec_priv_s {
  uint32_t          *clut;
  xine_t            *xine;

  uint8_t           *overlay;
  uint8_t           *mask;
};

#ifdef BIG_ENDIAN
static uint32_t _default_clut[32] = {
  0x80801000, 0x80801000, 0x80808400, 0x8080eb00,
  0x80801000, 0x80801000, 0x80808400, 0x8080eb00,
  0x80801000, 0x80801000, 0x80808400, 0x8080eb00,
  0x80801000, 0x80801000, 0x80808400, 0x8080eb00,
};

#else

static uint32_t _default_clut[32] = {
  0x00108080, 0x00108080, 0x00848080, 0x00eb8080,
  0x00108080, 0x00108080, 0x00848080, 0x00eb8080,
  0x00108080, 0x00108080, 0x00848080, 0x00eb8080,
  0x00108080, 0x00108080, 0x00848080, 0x00eb8080
};
#endif

void spudec_change_clut (spudec_t *this, uint32_t *clut)
{
  if(clut == NULL) {
    this->private->clut = _default_clut;
  } else {
    this->private->clut = clut;
  }
}

void spudec_start (spudec_t *this, clut_t *clut)
{
  /* initialise overlay and mask buffers. */
  this->private->mask = this->private->overlay = NULL;

  spudec_change_clut(this, clut);
}

void spudec_overlay_yuv (spudec_t *this, uint32_t vpts, 
			 uint8_t *y, uint8_t *u, uint8_t *v)
{
  if(this->private->xine->spu_channel != -1)
    memset(y+102400, 255, 2048);
}

void spudec_overlay_rgb (spudec_t *this, uint32_t vpts, 
			 uint8_t *rgb_data, int mode)
{
}

void spudec_push_packet (spudec_t *this, buf_element_t *buf)
{
}

spudec_t *spudec_init (xine_t *xine)
{
  spudec_t *decoder;

  printf("spudec: Creating decoder.\n");

  decoder = xmalloc(sizeof(struct spudec_s));
  decoder->private = xmalloc(sizeof(struct spudec_priv_s));
  decoder->private->clut = _default_clut;
  decoder->private->xine = xine;

  decoder->start       = spudec_start;
  decoder->change_clut = spudec_change_clut;
  decoder->push_packet = spudec_push_packet;
  decoder->overlay_yuv = spudec_overlay_yuv;
  decoder->overlay_rgb = spudec_overlay_rgb;

  return decoder;
}

void spudec_close (spudec_t *decoder)
{
  if(!decoder) {
    /* If some naughty person passed a NULL pointer, don't
     * do silly things. */
    return;
  }

  printf("spudec: Closing decoder.\n");

  free(decoder->private);
  free(decoder);
}
