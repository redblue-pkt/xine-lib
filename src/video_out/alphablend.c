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


/*
 * Some macros for fixed point arithmetic.
 *
 * The blend_rgb* routines perform rle image scaling using
 * scale factors that are expressed as integers scaled with
 * a factor of 2**16.
 *
 * INT_TO_SCALED()/SCALED_TO_INT() converts from integer
 * to scaled fixed point and back.
 */
#define	SCALE_SHIFT	  16
#define	SCALE_FACTOR	  (1<<SCALE_SHIFT)
#define	INT_TO_SCALED(i)  ((i)  << SCALE_SHIFT)
#define	SCALED_TO_INT(sc) ((sc) >> SCALE_SHIFT)


static rle_elem_t *
rle_img_advance_line(rle_elem_t *rle, rle_elem_t *rle_limit, int w)
{
  int x;

  for (x = 0; x < w && rle < rle_limit; ) {
    x += rle->len;
    rle++;
  }
  return rle;
}


void blend_rgb16 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height)
{
  uint8_t *trans;
  clut_t* clut = (clut_t*) img_overl->color;

  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x, y, x1_scaled, x2_scaled;
  int dy, dy_step, x_scale;	/* scaled 2**SCALE_SHIFT */
  uint16_t *img_pix;

  dy_step = INT_TO_SCALED(dst_height) / img_height;
  x_scale = INT_TO_SCALED(img_width)  / dst_width;

  img_pix = (uint16_t *) img
      + (img_overl->y * img_height / dst_height) * img_width
      + (img_overl->x * img_width / dst_width);

  trans = img_overl->trans;

  for (y = dy = 0; y < src_height && rle < rle_limit;) {
    int mask = !(img_overl->clip_top > y || img_overl->clip_bottom < y);
    rle_elem_t *rle_start = rle;

    for (x = x1_scaled = 0; x < src_width;) {
      uint8_t clr;
      uint16_t o;

      clr = rle->color;
      o   = trans[clr];

      if (o) if (img_overl->clip_left   > x ||
		 img_overl->clip_right  < x)
		   o = 0;

      x2_scaled = SCALED_TO_INT((x + rle->len) * x_scale);
      if (o && mask) {
	mem_blend16(img_pix+x1_scaled, *((uint16_t *)&clut[clr]), o, x2_scaled-x1_scaled);
      }

      x1_scaled = x2_scaled;
      x += rle->len;
      rle++;
      if (rle >= rle_limit) break;
    }

    img_pix += img_width;
    dy += dy_step;
    if (dy >= INT_TO_SCALED(1)) {
      dy -= INT_TO_SCALED(1);
      ++y;
      while (dy >= INT_TO_SCALED(1)) {
	rle = rle_img_advance_line(rle, rle_limit, src_width);
	dy -= INT_TO_SCALED(1);
	++y;
      }
    } else {
      rle = rle_start;		/* y-scaling, reuse the last rle encoded line */
    }
  }
}

void blend_rgb24 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height)
{
  clut_t* clut = (clut_t*) img_overl->color;
  uint8_t *trans;
  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x, y, x1_scaled, x2_scaled;
  int dy, dy_step, x_scale;	/* scaled 2**SCALE_SHIFT */
  uint8_t *img_pix;

  dy_step = INT_TO_SCALED(dst_height) / img_height;
  x_scale = INT_TO_SCALED(img_width)  / dst_width;

  img_pix = img + 3 * (  (img_overl->y * img_height / dst_height) * img_width
		       + (img_overl->x * img_width  / dst_width));

  trans = img_overl->trans;

  for (dy = y = 0; y < src_height && rle < rle_limit; ) {
    int mask = !(img_overl->clip_top > y || img_overl->clip_bottom < y);
    rle_elem_t *rle_start = rle;

    for (x = x1_scaled = 0; x < src_width;) {
      uint8_t clr;
      uint16_t o;

      clr = rle->color;
      o   = trans[clr];

      if (o) if (img_overl->clip_left   > x ||
		 img_overl->clip_right  < x)
		   o = 0;

      x2_scaled = SCALED_TO_INT((x + rle->len) * x_scale);
      if (o && mask) {
        mem_blend24(img_pix + x1_scaled*3, clut[clr].cb,
                    clut[clr].cr, clut[clr].y,
                    o, x2_scaled-x1_scaled);
      }

      x1_scaled = x2_scaled;
      x += rle->len;
      rle++;
      if (rle >= rle_limit) break;
    }

    img_pix += img_width * 3;
    dy += dy_step;
    if (dy >= INT_TO_SCALED(1)) {
      dy -= INT_TO_SCALED(1);
      ++y;
      while (dy >= INT_TO_SCALED(1)) {
	rle = rle_img_advance_line(rle, rle_limit, src_width);
	dy -= INT_TO_SCALED(1);
	++y;
      }
    } else {
      rle = rle_start;		/* y-scaling, reuse the last rle encoded line */
    }
  }
}

void blend_rgb32 (uint8_t * img, vo_overlay_t * img_overl,
		  int img_width, int img_height,
		  int dst_width, int dst_height)
{
  clut_t* clut = (clut_t*) img_overl->color;
  uint8_t *trans;
  int src_width = img_overl->width;
  int src_height = img_overl->height;
  rle_elem_t *rle = img_overl->rle;
  rle_elem_t *rle_limit = rle + img_overl->num_rle;
  int x, y, x1_scaled, x2_scaled;
  int dy, dy_step, x_scale;	/* scaled 2**SCALE_SHIFT */
  uint8_t *img_pix;

  dy_step = INT_TO_SCALED(dst_height) / img_height;
  x_scale = INT_TO_SCALED(img_width)  / dst_width;

  img_pix = img + 4 * (  (img_overl->y * img_height / dst_height) * img_width
		       + (img_overl->x * img_width / dst_width));

  trans = img_overl->trans;

  for (y = dy = 0; y < src_height && rle < rle_limit; ) {
    int mask = !(img_overl->clip_top > y || img_overl->clip_bottom < y);
    rle_elem_t *rle_start = rle;

    for (x = x1_scaled = 0; x < src_width;) {
      uint8_t clr;
      uint16_t o;

      clr = rle->color;
      o   = trans[clr];

      if (o) if (img_overl->clip_left   > x ||
		 img_overl->clip_right  < x)
		   o = 0;

      x2_scaled = SCALED_TO_INT((x + rle->len) * x_scale);
      if (o && mask) {
        mem_blend32(img_pix + x1_scaled*4, clut[clr].cb,
                    clut[clr].cr, clut[clr].y,
                    o, x2_scaled-x1_scaled);
      }

      x1_scaled = x2_scaled;
      x += rle->len;
      rle++;
      if (rle >= rle_limit) break;
    }

    img_pix += img_width * 4;
    dy += dy_step;
    if (dy >= INT_TO_SCALED(1)) {
      dy -= INT_TO_SCALED(1);
      ++y;
      while (dy >= INT_TO_SCALED(1)) {
	rle = rle_img_advance_line(rle, rle_limit, src_width);
	dy -= INT_TO_SCALED(1);
	++y;
      }
    } else {
      rle = rle_start;		/* y-scaling, reuse the last rle encoded line */
    }
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
