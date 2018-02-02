/*
 * Copyright (C) 2000-2016 the xine project
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Raw RGB "Decoder" by Mike Melanson (melanson@pcisys.net)
 * Actually, this decoder just converts a raw RGB image to a YUY2 map
 * suitable for display under xine.
 *
 * This decoder deals with raw RGB data from Microsoft and Quicktime files.
 * Data from a MS file can be 32-, 24-, 16-, or 8-bit. The latter can also
 * be grayscale, depending on whether a palette is present. Data from a QT
 * file can be 32-, 24-, 16-, 8-, 4-, 2-, or 1-bit. Any resolutions <= 8
 * can also be greyscale depending on what the QT file specifies.
 *
 * One more catch: Raw RGB from a Microsoft file is upside down. This is
 * indicated by a negative height parameter.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG_MODULE "rgb"
#define LOG_VERBOSE
/*
#define LOG
*/
#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "group_raw.h"


typedef struct rgb_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

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
  double            ratio;       /* the width to height ratio */
  int               bytes_per_pixel;
  int               bit_depth;
  int               upside_down;

  int               palette_loaded;
  int               color_matrix;
  const char       *fmtstring;
  void             *rgb2yuy2;

} rgb_decoder_t;

static void rgb_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  rgb_decoder_t *this = (rgb_decoder_t *) this_gen;
  xine_bmiheader *bih;

  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
    (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    rgb2yuy2_palette (this->rgb2yuy2, buf->decoder_info_ptr[2], buf->decoder_info[2], this->bit_depth);
    this->palette_loaded = 1;
  }

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) { /* need to initialize */
    (this->stream->video_out->open) (this->stream->video_out, this->stream);

    bih = (xine_bmiheader *) buf->content;
    this->width = bih->biWidth;
    this->height = bih->biHeight;
    if (this->height < 0) {
      this->upside_down = 1;
      this->height = -this->height;
    } else {
      this->upside_down = 0;
    }
    this->ratio = (double)this->width/(double)this->height;

    this->bit_depth = bih->biBitCount;
    if (this->bit_depth > 32)
      this->bit_depth &= 0x1F;
    /* round this number up in case of 15 */
    lprintf("width = %d, height = %d, bit_depth = %d\n", this->width, this->height, this->bit_depth);

    this->bytes_per_pixel = (this->bit_depth + 1) / 8;

    (this->stream->video_out->open) (this->stream->video_out, this->stream);

    if (this->bit_depth <= 8) {
      this->fmtstring = "rgb"; /* see palette_entry_t */
    } else if (this->upside_down) {
      this->fmtstring =
        this->bytes_per_pixel == 2 ? "rgb555le" :
        this->bytes_per_pixel == 3 ? "bgr" : "bgra";
    } else {
      this->fmtstring =
        this->bytes_per_pixel == 2 ? "rgb555be" :
        this->bytes_per_pixel == 3 ? "rgb" : "rgba";
    }
    this->color_matrix =
      this->stream->video_out->get_capabilities (this->stream->video_out) & VO_CAP_FULLRANGE ? 11 : 10;
    rgb2yuy2_free (this->rgb2yuy2);
    this->rgb2yuy2 = rgb2yuy2_alloc (this->color_matrix, this->fmtstring);

    free (this->buf);

    /* minimal buffer size */
    this->bufsize = this->width * this->height * this->bytes_per_pixel;
    this->buf = calloc(1, this->bufsize);
    this->size = 0;

    this->decoder_ok = 1;

    /* load the stream/meta info */
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "Raw RGB");

    return;
  } else if (this->decoder_ok) {

    if (this->size + buf->size > this->bufsize) {
      this->bufsize = this->size + 2 * buf->size;
      this->buf = realloc (this->buf, this->bufsize);
    }
    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      int flags = VO_BOTH_FIELDS;
      VO_SET_FLAGS_CM (this->color_matrix, flags);

      img = this->stream->video_out->get_frame (this->stream->video_out,
                                        this->width, this->height,
                                        this->ratio, XINE_IMGFMT_YUY2,
                                        flags);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;


      if ((this->bit_depth <= 8) && !this->palette_loaded) {
        rgb2yuy2_palette (this->rgb2yuy2, NULL, 1 << this->bit_depth, this->bit_depth);
        this->palette_loaded = 1;
      }

      if (this->upside_down) {
        rgb2yuy2_slice (
          this->rgb2yuy2,
          this->buf + (this->height - 1) * this->width, -this->width,
          img->base[0], img->pitches[0],
          this->width, this->height);
      } else {
        rgb2yuy2_slice (
          this->rgb2yuy2,
          this->buf, this->width,
          img->base[0], img->pitches[0],
          this->width, this->height);
      }

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

  free (this->buf);
  rgb2yuy2_free (this->rgb2yuy2);

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  rgb_decoder_t  *this ;

  this = (rgb_decoder_t *) calloc(1, sizeof(rgb_decoder_t));

  this->video_decoder.decode_data         = rgb_decode_data;
  this->video_decoder.flush               = rgb_flush;
  this->video_decoder.reset               = rgb_reset;
  this->video_decoder.discontinuity       = rgb_discontinuity;
  this->video_decoder.dispose             = rgb_dispose;
  this->size                              = 0;

  this->stream                            = stream;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

void *decode_rgb_init_class (xine_t *xine, const void *data) {

  video_decoder_class_t *this;

  this = calloc(1, sizeof(video_decoder_class_t));
  if (!this)
    return NULL;

  this->open_plugin     = open_plugin;
  this->identifier      = "RGB";
  this->description     = N_("Raw RGB video decoder plugin");
  this->dispose         = default_video_decoder_class_dispose;

  return this;
}


