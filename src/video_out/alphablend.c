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

/* FIXME: CLUT_T should go elsewhere. */
#ifndef CLUT_T
#define CLUT_T
typedef struct {                // CLUT == Color LookUp Table
        uint8_t:8;
        uint8_t y:8;
        uint8_t cr:8;
        uint8_t cb:8;
} __attribute__ ((packed)) clut_t;
#endif


#define BLEND_COLOR(dst, src, mask, o) ((((src&mask)*o + ((dst&mask)*(0x0f-o)))/0xf) & mask)

static inline uint16_t blendpixel_rgb16 (uint16_t dst, uint16_t src,
					 uint8_t o)
{
	return BLEND_COLOR (dst, src, 0xf800, o) |
	    BLEND_COLOR (dst, src, 0x07e0, o) |
	    BLEND_COLOR (dst, src, 0x001f, o);
}

static inline uint32_t blendpixel_rgb24 (uint32_t dst, uint32_t src,
					 uint8_t o)
{
	return BLEND_COLOR (dst, src, 0xff0000, o) |
	    BLEND_COLOR (dst, src, 0x00ff00, o) |
	    BLEND_COLOR (dst, src, 0x0000ff, o);
}

static inline uint32_t blendpixel_rgb32 (uint32_t dst, uint32_t src,
					 uint8_t o)
{
	return BLEND_COLOR (dst, src, 0xff0000, o) |
	    BLEND_COLOR (dst, src, 0x00ff00, o) |
	    BLEND_COLOR (dst, src, 0x0000ff, o);
}
/*
void blend_tux_rgb16 (uint8_t * img, int dst_width, int dst_height)
{
	int src_width = bg_width;
	int src_height = bg_height;
	uint8_t *src = (uint8_t *) bg_img_data;
	static int x_off;
	static int y_off;
	static int x_dir = 1;
	static int y_dir = 1;
	static int o = 5;
	static int o_dir = 1;

// align right bottom
	x_off += x_dir;
	if (x_off > (dst_width - src_width))
		x_dir = -x_dir;
	if (x_off <= 0)
		x_dir = -x_dir;

	y_off += y_dir;
	if (y_off > (dst_height - src_height))
		y_dir = -y_dir;
	if (y_off <= 0)
		y_dir = -y_dir;

// cycle parameters
	o += o_dir;
	if (o >= 0xf)
		o_dir = -o_dir;
	if (o <= 1)
		o_dir = -o_dir;
//
	{
		uint16_t *dst = (uint16_t *) img;
		int x,
		 y;

		dst += y_off * dst_width;
		for (y = 0; y < src_height; y++) {
			dst += x_off;
			for (x = 0; x < src_width; x++) {
				if ((*src) - bg_start_index)
					*dst = blendpixel_rgb16 (bg_palette_to_rgb [(*src) - bg_start_index], *dst, o);
				src++;
				dst++;
			}
			dst += dst_width - x - x_off;
		}
	}
}
*/
// convenience

#define uint24_t uint32_t

#define BLEND(bpp, img, img_overl, dst_width, dst_height)\
{									\
	static int o=5;							\
	uint8_t *src = (uint8_t *) img_overl->data;			\
        uint##bpp##_t *dst = (uint##bpp##_t *) img;			\
        int x, y;							\
									\
	dst += img_overl->y*dst_width;					\
        for (y=0; y<img_overl->height; y++) {				\
		dst += img_overl->x;					\
                for (x=0; x<img_overl->width; x++) {			\
			o = img_overl->trans[*src&0x0f];		\
									\
/*			if ((*src&0x0f) != 0)	 if alpha is != 0 */	\
			if (o)		/* if alpha is != 0 */		\
				*dst = blendpixel_rgb##bpp (*dst, img_overl->clut[(*src&0x0f)]/*.y*/, o); \
/*				*dst = blendpixel_rgb##bpp (*dst, myclut[img_overl->clut[(*src&0x0f)]], o);*/\
			src++;						\
			dst++;						\
                }							\
		dst += dst_width - x - img_overl->x;			\
        }								\
}

//void blend_rgb16 (uint8_t *img, overlay_buf_t *img_overl, int dst_width, int dst_height)
void blend_rgb (uint8_t * img, vo_overlay_t * img_overl, int dst_width,
		int dst_height)
{
#ifdef PRIV_CLUT
	u_int myclut[] = {
		0x0000,
		0x20e2,
		0x83ac,
		0x4227,
		0xa381,
		0xad13,
		0xbdf8,
		0xd657,
		0xee67,
		0x6a40,
		0xd4c1,
		0xf602,
		0xf664,
		0xe561,
		0xad13,
		0xffdf,
	};
#endif

	BLEND (16, img, img_overl, dst_width, dst_height);
	//blend_tux_rgb16 (img, dst_width, dst_height);
}

void blend_rgb24 (uint8_t * img, vo_overlay_t * img_overl, int dst_width,
		  int dst_height)
{
//FIXME CLUT
#ifdef PRIV_CLUT
	u_int myclut[] = {
		0x0000,
		0x20e2,
		0x83ac,
		0x4227,
		0xa381,
		0xad13,
		0xbdf8,
		0xd657,
		0xee67,
		0x6a40,
		0xd4c1,
		0xf602,
		0xf664,
		0xe561,
		0xad13,
		0xffdf,
	};
#endif
	BLEND (24, img, img_overl, dst_width, dst_height);
}

void blend_rgb32 (uint8_t * img, vo_overlay_t * img_overl, int dst_width,
		  int dst_height)
{
//FIXME CLUT
#ifdef PRIV_CLUT
	u_int myclut[] = {
		0x0000,
		0x20e2,
		0x83ac,
		0x4227,
		0xa381,
		0xad13,
		0xbdf8,
		0xd657,
		0xee67,
		0x6a40,
		0xd4c1,
		0xf602,
		0xf664,
		0xe561,
		0xad13,
		0xffdf,
	};
#endif
	BLEND (32, img, img_overl, dst_width, dst_height);
}

#define BLEND_YUV(dst, src, o) (((src)*o + ((dst)*(0xf-o)))/0xf)

void blend_yuv (uint8_t * dst_img, vo_overlay_t * img_overl,
		int dst_width, int dst_height)
{
/* FIXME: my_clut should disappear once I find out how to get the clut from the MPEG2 stream. */
/* It looks like it comes from the ,IFO file, so will have to wait for IFO parser in xine.
 * Here is an extract of another DVD player (oms)
 *               clut = ifoGetCLUT (priv->pgci);
 *               codec->ctrl (codec, CTRL_SPU_SET_CLUT, clut);
 */ 
/* This happens to work with "The Matrix" using 0(edges), 8(white) */
	clut_t my_clut[] = {
              {y: 0x00, cr: 0x80, cb:0x80},
	      {y: 0xbf, cr: 0x80, cb:0x80},
	      {y: 0x10, cr: 0x80, cb:0x80},
	      {y: 0x28, cr: 0x6d, cb:0xef},
	      {y: 0x51, cr: 0xef, cb:0x5a},
	      {y: 0xbf, cr: 0x80, cb:0x80},
	      {y: 0x36, cr: 0x80, cb:0x80},
	      {y: 0x28, cr: 0x6d, cb:0xef},
	      {y: 0xbf, cr: 0x80, cb:0x80},
              {y: 0x51, cr: 0x80, cb:0x80},
              {y: 0xbf, cr: 0x80, cb:0x80},
	      {y: 0x10, cr: 0x80, cb:0x80},
	      {y: 0x28, cr: 0x6d, cb:0xef},
	      {y: 0x5c, cr: 0x80, cb:0x80},
	      {y: 0xbf, cr: 0x80, cb:0x80},
	      {y: 0x1c, cr: 0x80, cb:0x80},
	      {y: 0x28, cr: 0x6d, cb:0xef}
	};

	int src_width = img_overl->width;
	int src_height = img_overl->height;
	uint8_t *src_data = img_overl->data;
	int step=dst_width - src_width;
	int x_off = img_overl->x;
	int y_off = img_overl->y;

	uint8_t *dst_y = dst_img + dst_width * y_off + x_off;
	uint8_t *dst_cr = dst_img + dst_width * dst_height +
	    (y_off / 2) * (dst_width / 2) + (x_off / 2) + 1;
	uint8_t *dst_cb = dst_img + (dst_width * dst_height * 5) / 4 +
	    (y_off / 2) * (dst_width / 2) + (x_off / 2) + 1;

	int x, y;
	for (y = 0; y < src_height; y++) {
		for (x = 0; x < src_width; x++) {
			uint8_t clr;
			uint8_t mask;
			uint8_t o;

			mask = (*src_data) >> 4 ;

			if (mask) {
			clr = img_overl->clut[*src_data & 0x03];
			o = img_overl->trans[*src_data & 0x03];
				*dst_y = BLEND_YUV (*dst_y, my_clut[clr].y, o);
                        }
			dst_y++;

			if (y & x & 1) {
				if (mask) {
					*dst_cr = BLEND_YUV (*dst_cr, my_clut[clr].cr, o);
					*dst_cb = BLEND_YUV (*dst_cb, my_clut[clr].cb, o);
				}
				dst_cr++;
				dst_cb++;
			}
			src_data++;
		}

		dst_y += step;

		if (y & 1) {
			dst_cr += (step + 1) / 2;
			dst_cb += (step + 1) / 2;
		}
	}
}

inline int is_blank (uint8_t * ptr, int width)
{
	int x;

	for (x = 0; x < width; x++) {
		if ((*ptr & 0x0f) && (*ptr >> 4))
			return 0;	// color != 0 && alpha != 0
		ptr++;
	}

	return 1;		// blank line
}

void crop_overlay (vo_overlay_t * overlay)
{
	uint8_t *data = overlay->data;
	int height = overlay->height;
	int width = overlay->width;
	int y;

	/*
	 * Shrink from bottom 
	 */

	for (y=height - 1;y >= 0 && is_blank (&data[y * width], width); y--);
	height = y + 1;

	/*
	 * Shrink from top 
	 */
	for (y=0; y < height && is_blank (&data[y * width], width); y++);
	height -= y;

	/*
	 * Shift data 
	 */
	overlay->y -= y;
	overlay->height = height;

	memcpy (data, &data[y * width], height * width);
}
