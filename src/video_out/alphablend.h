
/*
 *
 * Copyright (C) 2000  Thomas Mirlacher
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * The author may be reached as <dent@linuxvideo.org>
 *
 *------------------------------------------------------------
 *
 */

#ifndef __ALPHABLEND_H__
#define __ALPHABLEND_H__

#include "video_out.h"

typedef struct {         /* CLUT == Color LookUp Table */
  uint8_t cb    : 8;
  uint8_t cr    : 8;
  uint8_t y     : 8;
  uint8_t foo   : 8;
} __attribute__ ((packed)) clut_t;

void blend_rgb16 (uint8_t * img, vo_overlay_t * overlay,
		  int img_width, int img_height,
		  int delivered_width, int delivered_height);
void blend_rgb24 (uint8_t * img, vo_overlay_t * overlay,
		  int img_width, int img_height,
		  int delivered_iwdth, int delivered_height);
void blend_rgb32 (uint8_t * img, vo_overlay_t * overlay,
		  int img_width, int img_height,
		  int delivered_iwdth, int delivered_height);
void blend_yuv (uint8_t * img, vo_overlay_t * overlay,
		int width, int height);
void crop_overlay (vo_overlay_t * overlay);

#endif
