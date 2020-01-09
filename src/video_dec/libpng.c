/*
 * Copyright (C) 2003-2019 the xine project
 * Copyright (C) 2018-2019 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 * PNG image decoder using libpng
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define LOG_MODULE "png_video_decoder"
#define LOG_VERBOSE

/*
#define LOG
*/

#include <png.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xine_buffer.h>

/* decoder input data */
typedef struct {
  xine_t           *xine;
  const uint8_t    *image;
  int               size;
  int               pos;
} dec_data;

typedef struct png_decoder_s {
  video_decoder_t   video_decoder;

  xine_stream_t    *stream;
  int64_t           pts;

  vo_frame_t       *vo_frame;

  uint8_t          *buf;
  int               buf_size;

  uint8_t           error;
  uint8_t           video_open;

  /* private to _png_decode_data (). we place it here just to prevent setjump () /
   * longjump () from resetting values. */
  struct {
    png_structp     png;
    png_infop       png_info, png_end_info;
    png_bytep       row_pointers[16];
    dec_data        png_data;
    rgb2yuy2_t     *rgb2yuy2;
    vo_frame_t     *img, *free_img;
  } pdd;

} png_decoder_t;

/*
 * libpng callbacks
 */

static void _user_error(png_structp png, png_const_charp msg)
{
  png_decoder_t *this = (png_decoder_t *)png_get_error_ptr(png);
  this->error = 1;
  xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
          "%s\n", msg);
}

static void _user_warning(png_structp png, png_const_charp msg)
{
  png_decoder_t *this = (png_decoder_t *)png_get_error_ptr(png);
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
          "%s\n", msg);
}

static void _user_read(png_structp png, png_bytep data, png_size_t length)
{
  dec_data *this = (dec_data *)png_get_io_ptr(png);

  if (this->pos + length > (png_size_t)this->size) {
    xprintf(this->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
            "not enough data\n");
    return;
  }

  memcpy(data, this->image + this->pos, length);
  this->pos += length;
}

/*
 * decoding
 */

static vo_frame_t *_png_decode_data (png_decoder_t *this, const uint8_t *data, size_t size)
{
  this->pdd.rgb2yuy2 = NULL;
  this->pdd.img      = NULL;
  this->pdd.free_img = NULL;

  this->pdd.row_pointers[0] = NULL;

  this->pdd.png_data.xine  = this->stream->xine;
  this->pdd.png_data.image = data;
  this->pdd.png_data.size  = size;
  this->pdd.png_data.pos   = 0;

  if (!this->video_open) {
    (this->stream->video_out->open) (this->stream->video_out, this->stream);
    this->video_open = 1;
  }

  do {
    vo_frame_t     *img;
    uint8_t        *slice_start[3] = {NULL, NULL, NULL};
    png_uint_32     width, height, y;
    int             max_width, max_height;
    int             frame_flags = VO_BOTH_FIELDS;
    int             format;
    int             cm;
    int             color_type, interlace_type, compression_type, filter_type, bit_depth;
    int             linesize;

    /* set up decoding */
    this->pdd.png = png_create_read_struct (PNG_LIBPNG_VER_STRING, 0, 0, 0);
    if (!this->pdd.png)
      break;
    this->pdd.png_end_info = NULL;
    this->pdd.png_info = png_create_info_struct (this->pdd.png);
    if (!this->pdd.png_info)
      break;
    this->pdd.png_end_info = png_create_info_struct (this->pdd.png);
    if (!this->pdd.png_end_info)
      break;

    /* libpng bail out path */
    if (setjmp (png_jmpbuf (this->pdd.png)))
      break;

    png_set_read_fn (this->pdd.png, &this->pdd.png_data, _user_read);
    png_set_error_fn (this->pdd.png, this, _user_error, _user_warning);

    /* parse header */

    png_read_info (this->pdd.png, this->pdd.png_info);
    if (this->error)
      break;

    png_get_IHDR (this->pdd.png, this->pdd.png_info, &width, &height,
      &bit_depth, &color_type, &interlace_type, &compression_type, &filter_type);
    if (this->error)
      break;

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  width);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, height);


    /* set up libpng csc */

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
      png_set_palette_to_rgb (this->pdd.png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
      png_set_gray_to_rgb (this->pdd.png);
    }
    if (bit_depth == 16) {
      png_set_scale_16 (this->pdd.png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
      png_set_expand_gray_1_2_4_to_8 (this->pdd.png);
    }
    png_set_strip_alpha (this->pdd.png);

    /* alloc decoder image */

    linesize = png_get_rowbytes (this->pdd.png, this->pdd.png_info);
    this->pdd.row_pointers[0] = png_malloc (this->pdd.png, 16 * linesize);
    if (!this->pdd.row_pointers[0])
      break;
    for (y = 1; y < 16; y++) {
      this->pdd.row_pointers[y] = this->pdd.row_pointers[y - 1] + linesize;
    }

    /* check output capabilities */

    /* check max. image size */
    max_width = this->stream->video_out->get_property (this->stream->video_out, VO_PROP_MAX_VIDEO_WIDTH);
    max_height = this->stream->video_out->get_property (this->stream->video_out, VO_PROP_MAX_VIDEO_HEIGHT);
    /* crop when image is too large for vo */
    if (max_width > 0 && width > (png_uint_32)max_width)
      width = max_width;
    if (max_height > 0 && height > (png_uint_32)max_height)
      height = max_height;

    /* check full range capability */
    cm = 10; /* mpeg range ITU-R 601 */
    if (this->stream->video_out->get_capabilities (this->stream->video_out) & VO_CAP_FULLRANGE)
      cm = 11; /* full range */
    VO_SET_FLAGS_CM (cm, frame_flags);

    /* check output format - prefer YUY2 */
    format = (this->stream->video_out->get_capabilities (this->stream->video_out) & VO_CAP_YUY2) ?
      XINE_IMGFMT_YUY2 : XINE_IMGFMT_YV12;

    /* allocate output frame and set up slices */

    this->pdd.img =
    this->pdd.free_img =
    img = this->stream->video_out->get_frame (this->stream->video_out,
      width, height, (double)width / (double)height, format, frame_flags | VO_GET_FRAME_MAY_FAIL);
    if (!img) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
        LOG_MODULE ": get_frame (%dx%d) failed\n", width, height);
      break;
    }
    /* init slices */
    if (img->proc_slice && !(img->height & 0xf)) {
      slice_start[0] = img->base[0];
      slice_start[1] = img->base[1];
      slice_start[2] = img->base[2];
    }

    /* decode and convert in slices */

    this->pdd.rgb2yuy2 = rgb2yuy2_alloc (cm, "rgb");
    if (!this->pdd.rgb2yuy2)
      break;

    for (y = 0; y < height; y += 16) {
      int lines = y + 16 <= height ? 16 : height - y;
      png_read_rows (this->pdd.png, &this->pdd.row_pointers[0], NULL, lines);
      if (img->format == XINE_IMGFMT_YV12) {
        rgb2yv12_slice (this->pdd.rgb2yuy2, this->pdd.row_pointers[0],
          png_get_rowbytes (this->pdd.png, this->pdd.png_info),
          img->base[0] + y * img->pitches[0], img->pitches[0],
          img->base[1] + (y / 2) * img->pitches[1], img->pitches[1],
          img->base[2] + (y / 2) * img->pitches[2], img->pitches[2],
          width, lines);
      } else {
        rgb2yuy2_slice (this->pdd.rgb2yuy2, this->pdd.row_pointers[0],
          png_get_rowbytes (this->pdd.png, this->pdd.png_info),
          img->base[0] + y * img->pitches[0], img->pitches[0],
          width, lines);
      }
      if (slice_start[0]) {
        img->proc_slice (img, slice_start);
        slice_start[0] += 16 * img->pitches[0];
        slice_start[1] += 8 * img->pitches[1];
        slice_start[2] += 8 * img->pitches[2];
      }
    }

    /* from here, always return valid image. */
    img->duration  = 3600;
    img->bad_frame = 0;
    _x_stream_info_set (this->stream, XINE_STREAM_INFO_FRAME_DURATION, img->duration);
    this->pdd.free_img = NULL;

    png_read_end (this->pdd.png, this->pdd.png_end_info);
  } while (0);

  /* joint exit path */

  if (this->pdd.rgb2yuy2) {
    rgb2yuy2_free (this->pdd.rgb2yuy2);
    this->pdd.rgb2yuy2 = NULL;
  }

  if (this->pdd.free_img) {
    this->pdd.free_img->free (this->pdd.free_img);
    this->pdd.img = NULL;
    this->pdd.free_img = NULL;
  }

  if (this->pdd.row_pointers[0]) {
    png_free (this->pdd.png, this->pdd.row_pointers[0]);
    this->pdd.row_pointers[0] = NULL;
  }

  if (this->pdd.png)
    png_destroy_read_struct (&this->pdd.png, &this->pdd.png_info, &this->pdd.png_end_info);

  if (!this->pdd.img)
    this->pts = 0;

  return this->pdd.img;
}

/*
 * xine-lib decoder interface
 */

static void png_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
  png_decoder_t *this = (png_decoder_t *) this_gen;
  vo_frame_t *f = NULL;
  /* demux_image sends everything as preview at open time,
   * then an empty buf at play time.
   * we need to defer output to the latter because
   * - we want it to get correct vpts,
   * - we want it marked as first frame after seek, and
   * - we dont want it flushed by a previous stream stop. */

  if (!(buf->decoder_flags & BUF_FLAG_PREVIEW) && buf->pts)
    this->pts = buf->pts;

  do {
    if (buf->size > 0) {
      if (this->buf_size == 0 && (buf->decoder_flags & BUF_FLAG_FRAME_END)) {
        /* complete frame */
        f = _png_decode_data(this, buf->content, buf->size);
        break;
      }
      xine_buffer_copyin (this->buf, this->buf_size, buf->mem, buf->size);
      this->buf_size += buf->size;
    }
    if ((buf->decoder_flags & BUF_FLAG_FRAME_END) && (this->buf_size > 0)) {
      f = _png_decode_data(this, this->buf, this->buf_size);
      this->buf_size = 0;
    }
  } while (0);

  if (f) {
    if (this->vo_frame) {
      if (!(buf->decoder_flags & BUF_FLAG_PREVIEW)) {
        this->vo_frame->pts = this->pts;
        this->vo_frame->draw (this->vo_frame, this->stream);
      }
      this->vo_frame->free (this->vo_frame);
    }
    this->vo_frame = f;
  }

  if (this->vo_frame && !(buf->decoder_flags & BUF_FLAG_PREVIEW)) {
    this->vo_frame->pts = this->pts;
    this->vo_frame->draw (this->vo_frame, this->stream);
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
}

static void png_flush (video_decoder_t *this_gen)
{
  png_decoder_t *this = (png_decoder_t *) this_gen;

  if (this->vo_frame) {
    this->vo_frame->pts = this->pts;
    this->vo_frame->draw (this->vo_frame, this->stream);
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
}

static void png_reset (video_decoder_t *this_gen)
{
  png_decoder_t *this = (png_decoder_t *) this_gen;

  if (this->vo_frame) {
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
  this->buf_size = 0;
  this->pts = 0;
}

static void png_discontinuity (video_decoder_t *this_gen)
{
  png_decoder_t *this = (png_decoder_t *) this_gen;
  this->pts = 0;
}

static void png_dispose (video_decoder_t *this_gen)
{
  png_decoder_t *this = (png_decoder_t *) this_gen;

  if (this->vo_frame) {
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
  if (this->video_open) {
    this->stream->video_out->close(this->stream->video_out, this->stream);
    this->video_open = 0;
  }

  xine_buffer_free(this->buf);
  free (this);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen,
                                     xine_stream_t *stream)
{
  png_decoder_t *this;

  (void)class_gen;

  this = calloc(1, sizeof(png_decoder_t));
  if (!this)
    return NULL;

  this->video_decoder.decode_data         = png_decode_data;
  this->video_decoder.flush               = png_flush;
  this->video_decoder.reset               = png_reset;
  this->video_decoder.discontinuity       = png_discontinuity;
  this->video_decoder.dispose             = png_dispose;
  this->stream                            = stream;

  this->vo_frame = NULL;

  this->buf = xine_buffer_init(65536);
  if (!this->buf) {
    free(this);
    return NULL;
  }

  return &this->video_decoder;
}

/*
 * plugin class
 */

static void *init_class (xine_t *xine, const void *data) {

  (void)xine;
  (void)data;

  static video_decoder_class_t decode_video_png_class = {
    .open_plugin     = open_plugin,
    .identifier      = "pngdec",
    .description     = N_("PNG image video decoder"),
    .dispose         = NULL,
  };

  return (void *)&decode_video_png_class;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t supported_types[] = { BUF_VIDEO_PNG, 0 };

static const decoder_info_t dec_info_png = {
  .supported_types = supported_types,
  .priority        = 10,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "png", XINE_VERSION_CODE, &dec_info_png, init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
