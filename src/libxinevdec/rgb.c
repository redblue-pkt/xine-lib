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
 * $Id: rgb.c,v 1.18 2003/01/08 01:02:32 miguelfreitas Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "xineutils.h"
#include "bswap.h"

#define VIDEOBUFSIZE 128*1024

typedef struct {
  video_decoder_class_t   decoder_class;
} rgb_class_t;

typedef struct rgb_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  rgb_class_t      *class;
  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object */
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
    palette = (palette_entry_t *)buf->decoder_info_ptr[2];
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
    this->stream->video_out->open (this->stream->video_out, this->stream);

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
    this->buf = xine_xmalloc(this->bufsize);
    this->size = 0;

    init_yuv_planes(&this->yuv_planes, this->width, this->height);

    this->stream->video_out->open (this->stream->video_out, this->stream);
    this->decoder_ok = 1;

    /* load the stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = strdup("Raw RGB");

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

      img = this->stream->video_out->get_frame (this->stream->video_out,
                                        this->width, this->height,
                                        XINE_VO_ASPECT_DONT_TOUCH, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      /* iterate through each row */
      buf_ptr = 0;

      for (row_ptr = 0; row_ptr < this->yuv_planes.row_width * this->yuv_planes.row_count; row_ptr += this->yuv_planes.row_width) {
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

            buf_ptr += this->bytes_per_pixel - 3;

            this->yuv_planes.y[row_ptr + pixel_ptr] = 
              COMPUTE_Y(r, g, b);
            this->yuv_planes.u[row_ptr + pixel_ptr] = 
              COMPUTE_U(r, g, b);
            this->yuv_planes.v[row_ptr + pixel_ptr] = 
              COMPUTE_V(r, g, b);

          }
        }
      }

      yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

      img->draw(img, this->stream);
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

static void rgb_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void rgb_dispose (video_decoder_t *this_gen) {
  rgb_decoder_t *this = (rgb_decoder_t *) this_gen;

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  rgb_decoder_t  *this ;

  this = (rgb_decoder_t *) xine_xmalloc (sizeof (rgb_decoder_t));

  this->video_decoder.decode_data         = rgb_decode_data;
  this->video_decoder.flush               = rgb_flush;
  this->video_decoder.reset               = rgb_reset;
  this->video_decoder.discontinuity       = rgb_discontinuity;
  this->video_decoder.dispose             = rgb_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (rgb_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "RGB";
}

static char *get_description (video_decoder_class_t *this) {
  return "Raw RGB video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  rgb_class_t *this;

  this = (rgb_class_t *) xine_xmalloc (sizeof (rgb_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t video_types[] = { 
  BUF_VIDEO_RGB,
  0
 };

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 14, "rgb", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
