//TOAST_SPU will define ALL spu entries - no matter the tranparency
//#define TOAST_SPU
/* #define PRIV_CLUT */
/* Currently only blend_yuv(..) works */
/*
 *
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
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

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "video_out.h"
#include "alphablend.h"


#define BLEND_COLOR(dst, src, mask, o) ((((src&mask)*o + ((dst&mask)*(0x0f-o)))/0xf) & mask)

#define BLEND_BYTE(dst, src, o) (((src)*o + ((dst)*(0xf-o)))/0xf)

static void mem_blend16(uint16_t *mem, uint16_t clr, uint8_t o, int len) {
  uint16_t *limit = mem + len;
  while (mem < limit) {
    *mem =
     BLEND_COLOR(*mem, clr, 0xf800, o) |
     BLEND_COLOR(*mem, clr, 0x07e0, o) |
     BLEND_COLOR(*mem, clr, 0x001f, o);
    mem++;
  }
}

static void mem_blend24(uint8_t *mem, uint8_t r, uint8_t g, uint8_t b,
 uint8_t o, int len) {
  uint8_t *limit = mem + len*3;
  while (mem < limit) {
    *mem = BLEND_BYTE(*mem, r, o);
    mem++;
    *mem = BLEND_BYTE(*mem, g, o);
    mem++;
    *mem = BLEND_BYTE(*mem, b, o);
    mem++;
  }
}

static void mem_blend32(uint8_t *mem, uint8_t r, uint8_t g, uint8_t b,
 uint8_t o, int len) {
  uint8_t *limit = mem + len*4;
  while (mem < limit) {
    *mem = BLEND_BYTE(*mem, r, o);
    mem++;
    *mem = BLEND_BYTE(*mem, g, o);
    mem++;
    *mem = BLEND_BYTE(*mem, b, o);
    mem += 2;
  }
}

/* TODO: RGB color clut, only b/w now */
void blend_rgb16 (uint8_t * img, vo_overlay_t * img_overl, int dst_width,
		int dst_height)
{
  uint8_t *my_trans;
  uint16_t my_clut[4];
  clut_t* clut = (clut_t*) img_overl->color;

  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x;
  int y_off = img_overl->y;
  int mask;
  int x, y;

  uint16_t *dst_pix = (uint16_t *) img;
  dst_pix += dst_width * y_off + x_off;

  for (x = 0; x < 4; x++) {
    uint16_t clr = clut[x].y >> 2;
    my_clut[x] = (clr & 0xfe) << 10 | clr << 5 | (clr >> 1);
  }
  my_trans = img_overl->trans;

  for (y = 0; y < src_height; y++) {
    mask = !(img_overl->clip_top > y || img_overl->clip_bottom < y);

    for (x = 0; x < src_width;) {
      uint8_t clr;
      uint16_t o;

      clr = rle->color;
      o   = my_trans[clr];

      if (o) if (img_overl->clip_left   > x ||
		 img_overl->clip_right  < x)
		   o = 0;

      if (o && mask) {
	mem_blend16(dst_pix+x, my_clut[clr], o, rle->len);
      }

      x += rle->len;
      rle++;
      if (rle >= rle_limit) break;
    }
    if (rle >= rle_limit) break;
    dst_pix += dst_width;
  }
}

/* TODO: RGB color clut, only b/w now */
void blend_rgb24 (uint8_t * img, vo_overlay_t * img_overl, int dst_width,
		  int dst_height)
{
  clut_t *my_clut;
  uint8_t *my_trans;
  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x;
  int y_off = img_overl->y;
  int mask;
  int x, y;

  uint8_t *dst_pix = img + (dst_width * y_off + x_off) * 3;

  my_clut = (clut_t*) img_overl->color;
  my_trans = img_overl->trans;

  for (y = 0; y < src_height; y++) {
    mask = !(img_overl->clip_top > y || img_overl->clip_bottom < y);

    for (x = 0; x < src_width;) {
      uint8_t clr;
      uint16_t o;

      clr = rle->color;
      o   = my_trans[clr];

      if (o) if (img_overl->clip_left   > x ||
		 img_overl->clip_right  < x)
		   o = 0;

      if (o && mask) {
        uint8_t v = my_clut[clr].y;
	mem_blend24(dst_pix + x*3, v, v, v, o, rle->len);
      }

      x += rle->len;
      rle++;
      if (rle >= rle_limit) break;
    }
    if (rle >= rle_limit) break;
    dst_pix += dst_width * 3;
  }
}

/* TODO: RGB color clut, only b/w now */
void blend_rgb32 (uint8_t * img, vo_overlay_t * img_overl, int dst_width,
		  int dst_height)
{
  clut_t *my_clut;
  uint8_t *my_trans;
  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x;
  int y_off = img_overl->y;
  int mask;
  int x, y;

  uint8_t *dst_pix = img + (dst_width * y_off + x_off) * 4;

  my_clut = (clut_t*) img_overl->color;
  my_trans = img_overl->trans;

  for (y = 0; y < src_height; y++) {
    mask = !(img_overl->clip_top > y || img_overl->clip_bottom < y);

    for (x = 0; x < src_width;) {
      uint8_t clr;
      uint16_t o;

      clr = rle->color;
      o   = my_trans[clr];

      if (o) if (img_overl->clip_left   > x ||
		 img_overl->clip_right  < x)
		   o = 0;

      if (o && mask) {
        uint8_t v = my_clut[clr].y;
	mem_blend32(dst_pix + x*4, v, v, v, o, rle->len);
      }

      x += rle->len;
      rle++;
      if (rle >= rle_limit) break;
    }
    if (rle >= rle_limit) break;
    dst_pix += dst_width * 4;
  }
}

static void mem_blend8(uint8_t *mem, uint8_t val, uint8_t o, size_t sz)
{
  uint8_t *limit = mem + sz;
  while (mem < limit) {
    *mem = BLEND_BYTE(*mem, val, o);
    mem++;
  }
}

void blend_yuv (uint8_t * dst_img, vo_overlay_t * img_overl,
                int dst_width, int dst_height)
{
  clut_t *my_clut;
  uint8_t *my_trans;

  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x_off = img_overl->x;
  int y_off = img_overl->y;
  int mask;
  int x, y;

  uint8_t *dst_y = dst_img + dst_width * y_off + x_off;
  uint8_t *dst_cr = dst_img + dst_width * dst_height +
    (y_off / 2) * (dst_width / 2) + (x_off / 2) + 1;
  uint8_t *dst_cb = dst_cr + (dst_width * dst_height) / 4;

  my_clut = (clut_t*) img_overl->color;
  my_trans = img_overl->trans;

  for (y = 0; y < src_height; y++) {
    mask = !(img_overl->clip_top > y || img_overl->clip_bottom < y);

    for (x = 0; x < src_width;) {
      uint8_t clr;
      uint16_t o;

      clr = rle->color;
      o   = my_trans[clr];

      /* These three lines assume that menu buttons are "clean" separated
       * and do not overlap with the button clip borders */
      if (o) if (img_overl->clip_left   > x ||
		 img_overl->clip_right  < x)
		   o = 0;

      if (o && mask) {
	if (o >= 15) {
	  memset(dst_y + x, my_clut[clr].y, rle->len);
	  if (y & 1) {
	    memset(dst_cr + (x >> 1), my_clut[clr].cr, rle->len >> 1);
	    memset(dst_cb + (x >> 1), my_clut[clr].cb, rle->len >> 1);
	  }
	} else {
	  mem_blend8(dst_y + x, my_clut[clr].y, o, rle->len);
	  if (y & 1) {
	    mem_blend8(dst_cr + (x >> 1), my_clut[clr].cr, o, rle->len >> 1);
	    mem_blend8(dst_cb + (x >> 1), my_clut[clr].cb, o, rle->len >> 1);
	  }
	}
      }

      x += rle->len;
      rle++;
      if (rle >= rle_limit) break;
    }
    if (rle >= rle_limit) break;

    dst_y += dst_width;

    if (y & 1) {
      dst_cr += (dst_width + 1) / 2;
      dst_cb += (dst_width + 1) / 2;
    }
  }
}
