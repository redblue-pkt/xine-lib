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

/* _MSC_VER port changes */
#undef ATTRIBUTE_PACKED
#undef PRAGMA_PACK_BEGIN 
#undef PRAGMA_PACK_END

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)
#define ATTRIBUTE_PACKED __attribute__ ((packed))
#define PRAGMA_PACK 0
#endif

#if !defined(ATTRIBUTE_PACKED)
#define ATTRIBUTE_PACKED
#define PRAGMA_PACK 1
#endif

#if PRAGMA_PACK
#pragma pack(8)
#endif

typedef struct {         /* CLUT == Color LookUp Table */
  uint8_t cb    : 8;
  uint8_t cr    : 8;
  uint8_t y     : 8;
  uint8_t foo   : 8;
} ATTRIBUTE_PACKED clut_t;

#if PRAGMA_PACK
#pragma pack()
#endif

void blend_rgb16 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height);

void blend_rgb24 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height);

void blend_rgb32 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height);

void blend_yuv (uint8_t *dst_base[3], vo_overlay_t * img_overl,
                int dst_width, int dst_height, int dst_pitches[3]);

void blend_yuy2 (uint8_t * dst_img, vo_overlay_t * img_overl,
                int dst_width, int dst_height, int dst_pitch);

void crop_overlay (vo_overlay_t * overlay);

#endif
