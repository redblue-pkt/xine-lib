/*
 * Copyright (C) 2018-2020 the xine project
 * Copyright (C) 2018 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>

#include <dav1d/dav1d.h>

#define LOG_MODULE "dav1d_video_decoder"
#define LOG_VERBOSE

/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>

typedef struct dav1d_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  xine_stream_t    *stream;

  Dav1dContext     *ctx;
  Dav1dPicAllocator default_allocator; /* used as fallback when dri is not possible */

  uint8_t           cap_deep;
  uint8_t           dri;
  uint8_t           video_open;
  uint8_t           meta_set;
  int64_t           pts;             /* current incoming pts */
  double            ratio;

  unsigned char    *buf;         /* the accumulated buffer data */
  size_t            bufsize;     /* the maximum size of buf */
  size_t            size;        /* the current size of buf */
} dav1d_decoder_t;

/*
 * frame allocator for DRI
 */

static void _free_frame_cb(Dav1dPicture *pic, void *cookie)
{
  dav1d_decoder_t *this = cookie;
  vo_frame_t      *img;

  if (!this->dri || !pic->allocator_data || pic->allocator_data == pic->data[0]) {
    this->default_allocator.release_picture_callback(pic, this->default_allocator.cookie);
    return;
  }

  img = pic->allocator_data;
  img->free(img);
}

static int _alloc_frame_cb(Dav1dPicture *pic, void *cookie)
{
  dav1d_decoder_t *this = cookie;
  vo_frame_t      *img;
  int width, height, format, flags = 0;
  int i;

  if (!this->dri)
    return this->default_allocator.alloc_picture_callback(pic, this->default_allocator.cookie);

  switch (pic->p.layout) {
    case DAV1D_PIXEL_LAYOUT_I400:  /* monochrome */
    case DAV1D_PIXEL_LAYOUT_I420:  /* 4:2:0 planar */
      this->dri = (pic->p.bpc == 8 || this->cap_deep);
      break;
    case DAV1D_PIXEL_LAYOUT_I422:  /* 4:2:2 planar */
    case DAV1D_PIXEL_LAYOUT_I444:  /* 4:4:4 planar */
      this->dri = 0;
      break;
    default:
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
              "get_frame() failed: unknown layout %d\n", pic->p.layout);
      return -1;
  }

  if (this->ratio < 0.01)
    this->ratio = (double)pic->p.w / (double)pic->p.h;

  if (!this->dri) {
    /* unsupported frame format, need to copy ... */
    return this->default_allocator.alloc_picture_callback(pic, this->default_allocator.cookie);
  }

  /* The data[0], data[1] and data[2] must be 32 bytes aligned and with a
   * pixel width/height multiple of 128 pixels.
   * data[1] and data[2] must share the same stride[1].
   */
  width  = (pic->p.w + 127) & ~127;
  height = (pic->p.h + 127) & ~127;

  if (pic->p.bpc > 8) {
    format = XINE_IMGFMT_YV12_DEEP;
    VO_SET_FLAGS_DEPTH (pic->p.bpc, flags);
  } else {
    format = XINE_IMGFMT_YV12;
  }
  img = this->stream->video_out->get_frame (this->stream->video_out,
                                            width, height, this->ratio, format,
                                            VO_BOTH_FIELDS | VO_GET_FRAME_MAY_FAIL | flags);

  if (!img || img->width < width || img->height < height) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "get_frame(%dx%d) failed\n", width, height);
    if (img)
      img->free(img);
    return -1;
  }
  if (format == XINE_IMGFMT_YV12 && img->pitches[1] != img->pitches[2]) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "get_frame(%dx%d) returned incompatible frame\n", width, height);
    img->free(img);
    return -1;
  }

  img->crop_right  = width - pic->p.w;
  img->crop_bottom = height - pic->p.h;

  for (i = 0; i < 3; i++)
    pic->data[i] = img->base[i];

  pic->stride[0] = img->pitches[0];
  pic->stride[1] = img->pitches[1];
  _x_assert(img->pitches[1] == img->pitches[2]);

  pic->allocator_data = img;

  return 0;
}

/*
 * image copy / conversion (when dri not in use)
 */

static void _copy_plane(uint8_t *dst, const uint8_t *src, int dst_pitch, int src_pitch,
                        int width, int height, int shift)
{
  int x, y;

  if (shift > 0) {
    for (y = 0; y < height; y++) {
      const uint16_t *restrict s = (uint16_t *)(src + y * src_pitch);
      uint8_t        *restrict d = dst + y * dst_pitch;
      for (x = 0; x < width; x++)
        d[x] = s[x] >> shift;
    }
  } else {
    for (y = 0; y < height; y++) {
      xine_fast_memcpy(dst, src, width);
      src += src_pitch;
      dst += dst_pitch;
    }
  }
}

/* I420 / I400 -> YV12 */
static void _copy_planes(vo_frame_t *img, const Dav1dPicture *pic,
                         int width, int height, int shift)
{
  _copy_plane(img->base[0], pic->data[0], img->pitches[0], pic->stride[0],
              width, height, shift);

  if (pic->p.layout == DAV1D_PIXEL_LAYOUT_I400) {
    memset(img->base[1], 0x80, img->height * img->pitches[1]);
    memset(img->base[2], 0x80, img->height * img->pitches[2]);
  } else {
    _copy_plane(img->base[1], pic->data[1], img->pitches[1], pic->stride[1],
                width/2, height/2, shift);
    _copy_plane(img->base[2], pic->data[2], img->pitches[2], pic->stride[1],
                width/2, height/2, shift);
  }
}

/* I422 / I444 planar -> YUY2 */
static void _merge_planes(uint8_t *dst, int dst_pitch,
                          const Dav1dPicture *pic,
                          int width, int height, int shift, int subsamp)
{
  uint8_t *src_y = pic->data[0], *src_u = pic->data[1], *src_v = pic->data[2];
  int x, y;
  int skip = 2 - !!subsamp;

  if (shift > 0) {
    for (y = 0; y < height; y++) {
      const uint16_t *restrict sy = (const uint16_t *)(src_y + y * pic->stride[0]);
      const uint16_t *restrict su = (const uint16_t *)(src_u + y * pic->stride[1]);
      const uint16_t *restrict sv = (const uint16_t *)(src_v + y * pic->stride[1]);
      uint8_t        *restrict d  = dst + y * dst_pitch;
      for (x = 0; x < width; x++) {
        *d++ = *sy++ >> shift;
        *d++ = *su >> shift;
        *d++ = *sy++ >> shift;
        *d++ = *sv >> shift;
        su += skip;
        sv += skip;
      }
    }
  } else {
    for (y = 0; y < height; y++) {
      const uint8_t *restrict sy = src_y + y * pic->stride[0];
      const uint8_t *restrict su = src_u + y * pic->stride[1];
      const uint8_t *restrict sv = src_v + y * pic->stride[1];
      uint8_t       *restrict d  = dst + y * dst_pitch;
      for (x = 0; x < width; x++) {
        *d++ = *sy++;
        *d++ = *su;
        *d++ = *sy++;
        *d++ = *sv;
        su += skip;
        sv += skip;
      }
    }
  }
}

static vo_frame_t * _copy_image(dav1d_decoder_t *this, Dav1dPicture *pic)
{
  vo_frame_t *img = NULL;
  int width  = pic->p.w;
  int height = pic->p.h;
  int shift  = pic->p.bpc - 8;
  int format;

  switch (pic->p.layout) {
    case DAV1D_PIXEL_LAYOUT_I400:  /* monochrome */
    case DAV1D_PIXEL_LAYOUT_I420:  /* 4:2:0 planar */
      format = XINE_IMGFMT_YV12;
      break;
    case DAV1D_PIXEL_LAYOUT_I422:  /* 4:2:2 planar */
    case DAV1D_PIXEL_LAYOUT_I444:  /* 4:4:4 planar */
      format = XINE_IMGFMT_YUY2;
      break;
    default:
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
              "unknown dav1d pixl layout %d\n", pic->p.layout);
      return NULL;
  }

  img = this->stream->video_out->get_frame (this->stream->video_out,
                                            width, height, this->ratio, format,
                                            VO_BOTH_FIELDS | VO_GET_FRAME_MAY_FAIL);
  if (!img || img->width < width || img->height < height) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "get_frame(%dx%d) failed\n", width, height);
    if (img)
      img->free(img);
    return NULL;
  }

  switch (pic->p.layout) {
    case DAV1D_PIXEL_LAYOUT_I400:  /* monochrome */
    case DAV1D_PIXEL_LAYOUT_I420:  /* 4:2:0 planar */
      _copy_planes(img, pic, width, height, shift);
      break;

    case DAV1D_PIXEL_LAYOUT_I422:  /* 4:2:2 planar */
    case DAV1D_PIXEL_LAYOUT_I444:  /* 4:4:4 planar */
      _merge_planes(img->base[0], img->pitches[0],
                    pic,
                    width, height, shift,
                    (pic->p.layout == DAV1D_PIXEL_LAYOUT_I422));
      break;
  }

  return img;
}

/*
 *
 */

static void _draw_image(dav1d_decoder_t *this, Dav1dPicture *pic)
{
  vo_frame_t *img;

  if (!this->meta_set) {
    this->meta_set = 1;

    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC, "AV1");
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  pic->p.w);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, pic->p.h);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->ratio*10000);
  }

  if (this->dri) {
    img = pic->allocator_data;
  } else {
    img = _copy_image(this, pic);
  }
  if (!img)
    return;

  img->progressive_frame = 1;

  VO_SET_FLAGS_CM ((pic->seq_hdr->mtrx << 1) | (!!pic->seq_hdr->color_range), img->flags);

  switch (pic->frame_hdr->frame_type) {
    case DAV1D_FRAME_TYPE_KEY:    /* Key Intra frame */
    case DAV1D_FRAME_TYPE_INTRA:  /* Non key Intra frame */
      img->picture_coding_type = 1;
      break;
    case DAV1D_FRAME_TYPE_INTER:  /* Inter frame */
      img->picture_coding_type = 2;
      break;
    case DAV1D_FRAME_TYPE_SWITCH: /* Switch Inter frame */
      img->picture_coding_type = 3;
      break;
  }

  img->pts = pic->m.timestamp;
  img->bad_frame = 0;
  img->progressive_frame = 1;

  img->draw(img, this->stream);

  /* when using dri, frame may still be used as a reference frame inside decoder.
   * it is freed in free_frame_cb().
   */
  if (!this->dri)
    img->free(img);
}

static void _decode(dav1d_decoder_t *this, Dav1dData *data)
{
  Dav1dPicture pic;
  int r;

  memset(&pic, 0, sizeof(pic));

  data->m.timestamp = this->pts;
  this->pts = 0;

  do {
    if (data->sz > 0) {
      r = dav1d_send_data(this->ctx, data);
      if (r < 0 && r != -EAGAIN) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
                "send_data() failed: %d\n", r);
        break;
      }
    }

    r = dav1d_get_picture(this->ctx, &pic);
    if (r == 0) {
      _draw_image(this, &pic);
      dav1d_picture_unref(&pic);
    } else if (r != -EAGAIN) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
              "get_picture() failed: %d\n", r);
      break;
    }

  } while (data->sz > 0);
}

static void _data_free_wrapper(const uint8_t *data, void *opaque) {
  (void)data;
  free(opaque);
}

static void _dav1d_decode_data(video_decoder_t *this_gen, buf_element_t *buf)
{
  dav1d_decoder_t *this = xine_container_of(this_gen, dav1d_decoder_t, video_decoder);
  Dav1dData data;
  int r;

  if (buf->decoder_flags & (BUF_FLAG_PREVIEW | BUF_FLAG_SPECIAL | BUF_FLAG_STDHEADER |
                            BUF_FLAG_ASPECT)) {
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

  if (this->size + buf->size > this->bufsize) {
    this->bufsize = this->size + 2 * buf->size;
    this->buf = realloc (this->buf, this->bufsize);
    if (!this->buf) {
      this->bufsize = 0;
      return;
    }
  }
  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  if (!(buf->decoder_flags & BUF_FLAG_FRAME_END)) {
    return;
  }

  /* wrap gathered data */
  r = dav1d_data_wrap(&data, this->buf, this->size, _data_free_wrapper, this->buf);
  this->size = 0;
  if (r < 0) {
    return;
  }
  this->buf = NULL;
  this->bufsize = 0;

  if (!this->video_open) {
    (this->stream->video_out->open) (this->stream->video_out, this->stream);
    this->video_open = 1;
  }

  /* decode */
  _decode(this, &data);
}

static void _dav1d_flush(video_decoder_t *this_gen)
{
  dav1d_decoder_t *this = xine_container_of(this_gen, dav1d_decoder_t, video_decoder);
  Dav1dPicture     pic;

  memset(&pic, 0, sizeof(pic));

  while (1) {
    if (dav1d_get_picture(this->ctx, &pic) < 0)
      break;
    _draw_image(this, &pic);
    dav1d_picture_unref(&pic);
  }
}

static void _dav1d_discontinuity(video_decoder_t *this_gen)
{
  dav1d_decoder_t *this = xine_container_of(this_gen, dav1d_decoder_t, video_decoder);

  this->pts  = 0;
  this->size = 0;
}

static void _dav1d_reset(video_decoder_t *this_gen)
{
  dav1d_decoder_t *this = xine_container_of(this_gen, dav1d_decoder_t, video_decoder);
  Dav1dPicture     pic;

  dav1d_flush(this->ctx);

  /* drop frames */
  memset(&pic, 0, sizeof(pic));
  while (1) {
    if (dav1d_get_picture(this->ctx, &pic) < 0)
      break;
    dav1d_picture_unref(&pic);
  }

  this->pts  = 0;
  this->size = 0;
}

static void _dav1d_dispose(video_decoder_t *this_gen)
{
  dav1d_decoder_t *this = xine_container_of(this_gen, dav1d_decoder_t, video_decoder);

  _dav1d_reset(this_gen);

  dav1d_close(&this->ctx);
  _x_freep(&this->buf);

  if (this->video_open) {
    this->video_open = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *_open_plugin(video_decoder_class_t *class_gen, xine_stream_t *stream)
{
  dav1d_decoder_t *this;
  Dav1dSettings    settings;
  int              ncpu;

  (void)class_gen;

  xprintf(stream->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
          "using dav1d version %s\n", dav1d_version());

  this = (dav1d_decoder_t *)calloc(1, sizeof(*this));
  if (!this)
    return NULL;

  this->size          = 0;
  this->buf           = NULL;
  this->stream        = stream;
  this->ratio         = 0.0;
  this->dri           = 1;
  this->cap_deep      = !!(stream->video_out->get_capabilities(stream->video_out) & VO_CAP_YV12_DEEP);

  this->video_decoder.decode_data   = _dav1d_decode_data;
  this->video_decoder.flush         = _dav1d_flush;
  this->video_decoder.reset         = _dav1d_reset;
  this->video_decoder.discontinuity = _dav1d_discontinuity;
  this->video_decoder.dispose       = _dav1d_dispose;

  /*
   * set up decoder
   */

  dav1d_default_settings(&settings);

  /* save default allocator before overriding. It will be used when dri is not possible. */
  this->default_allocator = settings.allocator;

  /* multithreading */
  ncpu = xine_cpu_count();
#if DAV1D_API_VERSION_MAJOR > 5
  settings.n_threads = ncpu + 1;
  xprintf(stream->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
            "Using %d threads\n", settings.n_threads);
#else
  settings.n_frame_threads = (ncpu > 8) ? 4 : (ncpu < 2) ? 1 : ncpu/2;
  settings.n_tile_threads = MAX(1, ncpu - settings.n_frame_threads + 1);
  xprintf(stream->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
            "Using %d frame threads, %d tile threads\n",
            settings.n_frame_threads, settings.n_tile_threads);
#endif

  /* dri frame allocator */
  settings.allocator.cookie = this;
  settings.allocator.alloc_picture_callback = _alloc_frame_cb;
  settings.allocator.release_picture_callback = _free_frame_cb;

  if (dav1d_open(&this->ctx, &settings) < 0) {
    xine_log(stream->xine, XINE_LOG_MSG,
             "Failed to initialize dav1d AV1 decoder\n");
    free(this);
    return NULL;
  }

  return &this->video_decoder;
}

static void *init_plugin_dav1d(xine_t *xine, const void *data)
{
  (void)xine;
  (void)data;

  static const video_decoder_class_t decode_video_dav1d_class = {
    .open_plugin     = _open_plugin,
    .identifier      = "dav1d",
    .description     = N_("AV1 (dav1d) video decoder"),
    .dispose         = NULL,
  };

  return (void *)&decode_video_dav1d_class;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t video_types_dav1d[] = {
  BUF_VIDEO_AV1,
  0
};

static const decoder_info_t dec_info_video_dav1d = {
  .supported_types = video_types_dav1d,
  .priority        = 10,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "dav1d", XINE_VERSION_CODE, &dec_info_video_dav1d, init_plugin_dav1d },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
