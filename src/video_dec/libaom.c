/*
 * Copyright (C) 2013-2019 the xine project
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
 * libaom AV1 decoder wrapped by Petri Hintukainen <phintuka@users.sourceforge.net>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include <aom/aom_decoder.h>
#include <aom/aomdx.h>

#define LOG_MODULE "libaom_video_decoder"
#define LOG_VERBOSE

/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

typedef struct aom_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  xine_stream_t    *stream;

  struct aom_codec_ctx decoder;

  int64_t           pts;
  int               video_open;
  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */
  double            ratio;
} aom_decoder_t;

static void _copy_yv12_16_to_8(vo_frame_t *img, struct aom_image *aom_img, int width, int height)
{
  int i;

  for (i = 0; i < 3; i++) {
    int w = width;
    int h = height;
    int x, y;

    if (i) {
      w = (w + 1) >> 1;
      h = (h + 1) >> 1;
    }

    for (y = 0; y < h; y++) {
      uint16_t *src = (uint16_t *)(aom_img->planes[i] + y * aom_img->stride[i]);
      uint8_t  *dst = img->base[i] + y * img->pitches[i];
      for (x = 0; x < w; x++) {
        *dst++ = *src++;
      }
    }
  }
}

static void _draw_image(aom_decoder_t *this, aom_image_t *aom_img)
{
  vo_frame_t *img;
  int64_t     pts;
  int         width, height;
  int         frame_flags = 0;

  pts = (int64_t)(intptr_t)aom_img->user_priv;

  /* check format */

  switch (aom_img->fmt) {
    case AOM_IMG_FMT_I420:
    case AOM_IMG_FMT_YV12:
    case AOM_IMG_FMT_I42016:
      break;
    default:
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
              "unsupported image format %d 0x%x depth=%d\n",
              aom_img->fmt, aom_img->fmt, aom_img->bit_depth);
      return;
  }
  if (aom_img->bit_depth != 8) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "unsupported color depth %d\n",
            aom_img->bit_depth);
    return;
  }

  VO_SET_FLAGS_CM ((aom_img->mc << 1) | (aom_img->range == AOM_CR_FULL_RANGE), frame_flags);

  /* get frame */

  img = this->stream->video_out->get_frame (this->stream->video_out,
                                            aom_img->d_w, aom_img->d_h, this->ratio,
                                            XINE_IMGFMT_YV12,
                                            frame_flags | VO_BOTH_FIELDS | VO_GET_FRAME_MAY_FAIL);
  if (!img) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "get_frame(%dx%d) failed\n", aom_img->d_w, aom_img->d_h);
    return;
  }

  /* copy */

  /* crop if allocated frame is smaller than requested */
  width  = ((int)aom_img->d_w > img->width)  ? img->width  : (int)aom_img->d_w;
  height = ((int)aom_img->d_h > img->height) ? img->height : (int)aom_img->d_h;

  switch (aom_img->fmt) {
    case AOM_IMG_FMT_I420:
    case AOM_IMG_FMT_YV12:
      yv12_to_yv12(/* Y */
                   aom_img->planes[0], aom_img->stride[0],
                   img->base[0], img->pitches[0],
                   /* U */
                   aom_img->planes[1], aom_img->stride[1],
                   img->base[1], img->pitches[1],
                   /* V */
                   aom_img->planes[2], aom_img->stride[2],
                   img->base[2], img->pitches[2],
                   /* width x height */
                   width, height);
      break;
    case AOM_IMG_FMT_I42016:
      _copy_yv12_16_to_8(img, aom_img, width, height);
      break;
    default:
      /* not reached */
      break;
  }

  /* draw */

  VO_SET_FLAGS_CM ((aom_img->mc << 1) | (aom_img->range == AOM_CR_FULL_RANGE), img->flags);

  img->pts       = pts;
  img->bad_frame = 0;
  img->progressive_frame = 1;

  img->draw(img, this->stream);
  img->free(img);
}

static void _decode(aom_decoder_t *this, const uint8_t *buf, size_t size)
{
  const void *iter = NULL;
  struct aom_image *aom_img;
  void *priv;

  priv = (void*)(intptr_t)this->pts;
  this->pts = 0;

  if (aom_codec_decode(&this->decoder, buf, size, priv) != AOM_CODEC_OK) {
    const char *error  = aom_codec_error(&this->decoder);
    const char *detail = aom_codec_error_detail(&this->decoder);
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "error decoding frame: %s%s%s",
            error, detail ? ": " : "", detail ? detail : "");
    return;
  }

  while ((aom_img = aom_codec_get_frame(&this->decoder, &iter))) {

    if (aom_img->d_h < 1 || aom_img->d_w < 1)
      continue;

    if (!this->video_open) {
      (this->stream->video_out->open) (this->stream->video_out, this->stream);
      this->video_open = 1;

      if (this->ratio < 0.01)
        this->ratio = (double)aom_img->d_w / (double)aom_img->d_h;

      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "AV1");
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  aom_img->d_w);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, aom_img->d_h);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->ratio*10000);
    }

    _draw_image(this, aom_img);
  }
}

static void _aom_decode_data(video_decoder_t *this_gen, buf_element_t *buf)
{
  aom_decoder_t *this = (aom_decoder_t *) this_gen;
  uint8_t *data;
  size_t   size;

  if (buf->decoder_flags & (BUF_FLAG_PREVIEW | BUF_FLAG_SPECIAL | BUF_FLAG_STDHEADER | BUF_FLAG_ASPECT)) {
    if (buf->decoder_flags & (BUF_FLAG_PREVIEW | BUF_FLAG_SPECIAL | BUF_FLAG_STDHEADER)) {
      return;
    }

    if (buf->decoder_flags & BUF_FLAG_ASPECT) {
      if (buf->decoder_info[2]) {
        this->ratio = (double)buf->decoder_info[1] / (double)buf->decoder_info[2];
      }
    }
  }

  /* save pts */
  if (buf->pts > 0) {
    this->pts = buf->pts;
  }

  /* collect data */

  if (this->size == 0 && (buf->decoder_flags & BUF_FLAG_FRAME_END)) {
    /* complete buffer - no need to copy */
    data = buf->content;
    size = buf->size;
  } else {
    if (this->size + buf->size > this->bufsize) {
      this->bufsize = this->size + 2 * buf->size;
      this->buf = realloc (this->buf, this->bufsize);
    }
    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
    this->size += buf->size;
    if (!(buf->decoder_flags & BUF_FLAG_FRAME_END)) {
      return;
    }
    data  = this->buf;
    size = this->size;
    this->size = 0;
  }

  /* decode */

  _decode(this, data, size);
}

static void _aom_flush(video_decoder_t *this_gen)
{
  aom_decoder_t *this = (aom_decoder_t *) this_gen;

  _decode(this, NULL, 0);
}

static void _aom_reset(video_decoder_t *this_gen)
{
  aom_decoder_t *this = (aom_decoder_t *) this_gen;
  const void *iter = NULL;

  while (aom_codec_get_frame(&this->decoder, &iter) != NULL) {
  }

  this->pts  = 0;
  this->size = 0;
}

static void _aom_discontinuity(video_decoder_t *this_gen)
{
  aom_decoder_t *this = (aom_decoder_t *) this_gen;

  _aom_flush(this_gen);

  this->pts  = 0;
  this->size = 0;
}

static void _aom_dispose(video_decoder_t *this_gen)
{
  aom_decoder_t *this = (aom_decoder_t *) this_gen;

  aom_codec_destroy(&this->decoder);
  _x_freep(&this->buf);

  if (this->video_open) {
    this->video_open = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *_open_plugin(video_decoder_class_t *class_gen, xine_stream_t *stream)
{
  aom_decoder_t *this;
  struct aom_codec_dec_cfg deccfg = {
    .threads = xine_cpu_count(),
    .allow_lowbitdepth = 1,
  };

  (void)class_gen;

  xprintf(stream->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
          "using %d CPU cores\n", deccfg.threads);

  this = (aom_decoder_t *) calloc(1, sizeof(aom_decoder_t));
  if (!this)
    return NULL;

  this->size          = 0;
  this->buf           = NULL;
  this->stream        = stream;
  this->ratio         = 0.0;

  this->video_decoder.decode_data   = _aom_decode_data;
  this->video_decoder.flush         = _aom_flush;
  this->video_decoder.reset         = _aom_reset;
  this->video_decoder.discontinuity = _aom_discontinuity;
  this->video_decoder.dispose       = _aom_dispose;

  xprintf(stream->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
          "using libaom %s\n", aom_codec_version_str());

  if (aom_codec_dec_init(&this->decoder, &aom_codec_av1_dx_algo, &deccfg, 0) != AOM_CODEC_OK) {
    xine_log(stream->xine, XINE_LOG_MSG,
             "Failed to initialize libaom AV1 decoder: %s\n",
             aom_codec_error(&this->decoder));
    free(this);
    return NULL;
  }

  return &this->video_decoder;
}

static void *init_plugin_aom(xine_t *xine, const void *data)
{
  (void)xine;
  (void)data;

  static const video_decoder_class_t decode_video_aom_class = {
    .open_plugin     = _open_plugin,
    .identifier      = "libaom",
    .description     = N_("AV1 (libaom) video decoder"),
    .dispose         = NULL,
  };

  return (void *)&decode_video_aom_class;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t video_types_aom[] = {
  BUF_VIDEO_AV1,
  0
};

static const decoder_info_t dec_info_video_aom = {
  .supported_types = video_types_aom,
  .priority        = 1,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "libaom", XINE_VERSION_CODE, &dec_info_video_aom, init_plugin_aom },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
