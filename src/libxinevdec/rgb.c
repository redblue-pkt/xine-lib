/*
 * Copyright (C) 2000-2002 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * Raw RGB "Decoder" by Mike Melanson (melanson@pcisys.net)
 * Actually, this decoder just converts a raw RGB image to a YUY2 map
 * suitable for display under xine.
 * 
 * $Id: rgb.c,v 1.2 2002/07/15 21:42:34 esnel Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "video_out.h"
#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "bswap.h"

#define VIDEOBUFSIZE 128*1024

#define LE_16(x) (le2me_16(*(uint16_t *)(x)))
#define LE_32(x) (le2me_32(*(uint32_t *)(x)))

typedef struct rgb_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  /* these are traditional variables in a video decoder object */
  vo_instance_t    *video_out;   /* object that will receive frames */
  uint64_t          video_step;  /* frame duration in pts units */
  int               decoder_ok;  /* current decoder status */
  int               skipframes;

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */

  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */
  int               bytes_per_pixel;

  unsigned char     yuv_palette[256 * 4];
  yuv_planes_t      yuv_planes;
  
} rgb_decoder_t;

static int rgb_can_handle (video_decoder_t *this_gen, int buf_type) {

  return (buf_type == BUF_VIDEO_RGB);
}

static void rgb_init (video_decoder_t *this_gen, 
  vo_instance_t *video_out) {
  rgb_decoder_t *this = (rgb_decoder_t *) this_gen;

  /* set our own video_out object to the one that xine gives us */
  this->video_out  = video_out;

  /* indicate that the decoder is not quite ready yet */
  this->decoder_ok = 0;
}

static void rgb_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  rgb_decoder_t *this = (rgb_decoder_t *) this_gen;
  xine_bmiheader *bih;
  palette_entry_t *palette;
  int i;
  int pixel_ptr, row_ptr;
  int palette_index;
  int buf_ptr;
  unsigned int packed_pixel;
  unsigned char r, g, b;

  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette = (palette_entry_t *)buf->decoder_info[3];
    for (i = 0; i < buf->decoder_info[2]; i++) {
      this->yuv_palette[i * 4 + 0] =
        COMPUTE_Y(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 1] =
        COMPUTE_U(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 2] =
        COMPUTE_V(palette[i].r, palette[i].g, palette[i].b);
    }
  }

  if (buf->decoder_flags & BUF_FLAG_HEADER) { /* need to initialize */
    this->video_out->open (this->video_out);

    if(this->buf)
      free(this->buf);

    bih = (xine_bmiheader *) buf->content;
    this->width = (bih->biWidth + 3) & ~0x03;
    this->height = (bih->biHeight + 3) & ~0x03;
    this->video_step = buf->decoder_info[1];
    /* round this number up in case of 15 */
    this->bytes_per_pixel = (bih->biBitCount + 1) / 8;

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    this->video_out->open (this->video_out);
    this->decoder_ok = 1;

    init_yuv_planes(&this->yuv_planes, this->width, this->height);

    return;
  } else if (this->decoder_ok) {

    if (this->size + buf->size > this->bufsize) {
      this->bufsize = this->size + 2 * buf->size;
      this->buf = realloc (this->buf, this->bufsize);
    }

    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
      this->video_step = buf->decoder_info[0];

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      img = this->video_out->get_frame (this->video_out,
                                        this->width, this->height,
                                        42, IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      /* iterate through each row */
      buf_ptr = 0;
      row_ptr = this->yuv_planes.row_stride *
        (this->yuv_planes.row_count - 1);
      for (; row_ptr >= 0; row_ptr -= this->yuv_planes.row_stride) {
        for (pixel_ptr = 0; pixel_ptr < this->width; pixel_ptr++) {

          if (this->bytes_per_pixel == 1) {

            palette_index = this->buf[buf_ptr++];

            this->yuv_planes.y[row_ptr + pixel_ptr] = 
              this->yuv_palette[palette_index * 4 + 0];
            this->yuv_planes.u[row_ptr + pixel_ptr] = 
              this->yuv_palette[palette_index * 4 + 1];
            this->yuv_planes.v[row_ptr + pixel_ptr] = 
              this->yuv_palette[palette_index * 4 + 2];

          } else if (this->bytes_per_pixel == 2) {

            packed_pixel = LE_16(&this->buf[buf_ptr]);
            buf_ptr += 2;
            UNPACK_BGR15(packed_pixel, r, g, b);

            this->yuv_planes.y[row_ptr + pixel_ptr] = 
              COMPUTE_Y(r, g, b);
            this->yuv_planes.u[row_ptr + pixel_ptr] = 
              COMPUTE_U(r, g, b);
            this->yuv_planes.v[row_ptr + pixel_ptr] = 
              COMPUTE_V(r, g, b);

          } else {

            b = this->buf[buf_ptr++];
            g = this->buf[buf_ptr++];
            r = this->buf[buf_ptr++];

            this->yuv_planes.y[row_ptr + pixel_ptr] = 
              COMPUTE_Y(r, g, b);
            this->yuv_planes.u[row_ptr + pixel_ptr] = 
              COMPUTE_U(r, g, b);
            this->yuv_planes.v[row_ptr + pixel_ptr] = 
              COMPUTE_V(r, g, b);

          }
        }

        // take care of the extra 2 pixels on the C lines
        this->yuv_planes.u[row_ptr + pixel_ptr] =
          this->yuv_planes.u[row_ptr + pixel_ptr - 1];
        this->yuv_planes.u[row_ptr + pixel_ptr + 1] =
          this->yuv_planes.u[row_ptr + pixel_ptr - 2];

        this->yuv_planes.v[row_ptr + pixel_ptr] =
          this->yuv_planes.v[row_ptr + pixel_ptr - 1];
        this->yuv_planes.v[row_ptr + pixel_ptr + 1] =
          this->yuv_planes.v[row_ptr + pixel_ptr - 2];
      }

      yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

      if (img->copy) {
	int height = img->height;
	uint8_t *src[3];

	src[0] = img->base[0];

	while ((height -= 16) >= 0) {
	  img->copy(img, src);
	  src[0] += 16 * img->pitches[0];
	}
      }

      img->draw(img);
      img->free(img);

      this->size = 0;
    }
  }
}

/*
 * This function is called when xine needs to flush the system. Not
 * sure when or if this is used or even if it needs to do anything.
 */
static void rgb_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void rgb_reset (video_decoder_t *this_gen) {
  rgb_decoder_t *this = (rgb_decoder_t *) this_gen;

  this->size = 0;
}

/*
 * This function is called when xine shuts down the decoder. It should
 * free any memory and release any other resources allocated during the
 * execution of the decoder.
 */
static void rgb_close (video_decoder_t *this_gen) {
  rgb_decoder_t *this = (rgb_decoder_t *) this_gen;

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->video_out->close(this->video_out);
  }
}

/*
 * This function returns the human-readable ID string to identify 
 * this decoder.
 */
static char *rgb_get_id(void) {
  return "Raw RGB";
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void rgb_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

video_decoder_t *init_video_decoder_plugin (int iface_version, xine_t *xine) {

  rgb_decoder_t *this ;

  if (iface_version != 10) {
    printf( "rgb: plugin doesn't support plugin API version %d.\n"
            "rgb: this means there's a version mismatch between xine and this "
            "rgb: decoder plugin.\nInstalling current plugins should help.\n",
            iface_version);
    return NULL;
  }

  this = (rgb_decoder_t *) malloc (sizeof (rgb_decoder_t));
  memset(this, 0, sizeof (rgb_decoder_t));

  this->video_decoder.interface_version   = iface_version;
  this->video_decoder.can_handle          = rgb_can_handle;
  this->video_decoder.init                = rgb_init;
  this->video_decoder.decode_data         = rgb_decode_data;
  this->video_decoder.flush               = rgb_flush;
  this->video_decoder.reset               = rgb_reset;
  this->video_decoder.close               = rgb_close;
  this->video_decoder.get_identifier      = rgb_get_id;
  this->video_decoder.dispose             = rgb_dispose;
  this->video_decoder.priority            = 1;

  return (video_decoder_t *) this;
}

