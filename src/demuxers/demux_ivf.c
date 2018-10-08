/*
 * Copyright (C) 2000-2018 the xine project
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

#include <stdlib.h>

/* #define LOG */
#define LOG_MODULE "demux_ivf"
#define LOG_VERBOSE

#include "group_video.h"

#include <xine/xine_internal.h>
#include <xine/demux.h>
#include "bswap.h"

static const struct {
  uint32_t buf_type;
  char     fourcc[4];
} ivf_tag_map[] = {
  { BUF_VIDEO_AV1,  "AV01" },
  { BUF_VIDEO_H264, "H264" },
  { BUF_VIDEO_HEVC, "HEVC" },
  { BUF_VIDEO_VP8,  "VP80" },
  { BUF_VIDEO_VP9,  "VP90" },
};

typedef struct {
  demux_plugin_t      demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  input_plugin_t      *input;
  int                  status;
  int                  seek_flag;
  int64_t              last_pts;
  uint32_t             buf_type;

  uint32_t             num_frames;
  uint32_t             frame_number;
  uint32_t             frame_rate_num, frame_rate_den;
} demux_ivf_t;

static int demux_ivf_send_chunk(demux_plugin_t *this_gen)
{
  demux_ivf_t *this = (demux_ivf_t *)this_gen;
  uint8_t  hdr[12];
  uint32_t len;
  int64_t  pts;
  int      normpos = 0;
  off_t    length;
  int      input_time, total_time;

  /* read and parse header */

  if (this->input->read(this->input, hdr, 12) != 12) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  len = _X_LE_32(hdr);
  pts = _X_LE_64(hdr + 4);

  lprintf("frame %d: pts %ld %ld\n", this->frame_number, pts, pts * 90000 * this->frame_rate_num / this->frame_rate_den);
  pts = pts * 90000 * this->frame_rate_num / this->frame_rate_den;

  /* handle seek and discontinuity */
  if (this->seek_flag) {
    _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
    this->seek_flag = 0;
  } else if (this->last_pts - pts > 270000 || this->last_pts - pts < -270000) {
    _x_demux_control_newpts(this->stream, pts, 0);
  }
  this->last_pts = pts;

  /* calculate normpos */
  length = this->input->get_length(this->input);
  if (length > 0) {
    off_t curpos = this->input->get_current_pos(this->input);
    if (curpos > 0) {
      normpos = (int)((double) curpos * 65535 / length);
    }
  }

  /* calculate timepos */
  input_time = (uint64_t)this->frame_number * 1000 * this->frame_rate_num / this->frame_rate_den;
  total_time = (uint64_t)this->num_frames   * 1000 * this->frame_rate_num / this->frame_rate_den;

  /* send frame */
  if (_x_demux_read_send_data(this->video_fifo,
                              this->input,
                              len, pts, this->buf_type, 0,
                              normpos, input_time, total_time,
                              this->frame_number) < 0) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  this->frame_number++;

  return this->status;
}

static int demux_ivf_get_status(demux_plugin_t *this_gen)
{
  demux_ivf_t *this = (demux_ivf_t *)this_gen;
  return this->status;
}

static void demux_ivf_send_headers(demux_plugin_t *this_gen)
{
  demux_ivf_t    *this = (demux_ivf_t *)this_gen;
  buf_element_t  *buf;
  uint8_t         hdr[32];
  int             width, height;
  xine_bmiheader *bih;
  off_t           file_length;

  this->video_fifo = this->stream->video_fifo;
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
  _x_demux_control_start(this->stream);

  /* read and parse header */

  if (this->input->seek(this->input, 0, SEEK_SET) != 0) {
    this->status = DEMUX_FINISHED;
    return;
  }

  if (this->input->read(this->input, hdr, 32) != 32) {
    this->status = DEMUX_FINISHED;
    return;
  }

  width                = _X_LE_16(&hdr[12]);
  height               = _X_LE_16(&hdr[14]);
  this->frame_rate_den = _X_LE_32(&hdr[16]);
  this->frame_rate_num = _X_LE_32(&hdr[20]);
  this->num_frames     = _X_LE_32(&hdr[24]);

  if (!this->frame_rate_num || !this->frame_rate_den) {
    this->status = DEMUX_FINISHED;
    return;
  }
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
          "codec=%4.4s size=%dx%d rate=%u:%u num_frames=%u\n",
          (const char *)hdr + 8, width, height,
          this->frame_rate_num, this->frame_rate_den, this->num_frames);

  _x_stream_info_set (this->stream, XINE_STREAM_INFO_FRAME_DURATION, (int64_t)this->frame_rate_num * 90000 / this->frame_rate_den);
  file_length = this->input->get_length(this->input);
  if (file_length > 32 + 12*this->num_frames) {
    file_length = file_length - 32 - 12*this->num_frames;
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_BITRATE,
                       file_length / this->frame_rate_num * this->frame_rate_den / this->num_frames * 8);
  }

  /* configure decoder */

  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);

  buf->decoder_flags = BUF_FLAG_HEADER | BUF_FLAG_STDHEADER | BUF_FLAG_FRAME_END;
  buf->type          = this->buf_type;

  bih = (xine_bmiheader *)buf->content;
  memset(bih, 0, sizeof(*bih));
  bih->biSize   = sizeof(xine_bmiheader);
  bih->biWidth  = width;
  bih->biHeight = height;
  buf->size = sizeof(*bih);

  buf->decoder_flags   |= BUF_FLAG_FRAMERATE;
  buf->decoder_info[0]  = (int64_t)this->frame_rate_num * 90000 / this->frame_rate_den;

  buf->decoder_flags   |= BUF_FLAG_ASPECT;
  buf->decoder_info[1]  = width;
  buf->decoder_info[2]  = height;

  this->video_fifo->put(this->video_fifo, buf);

  this->status = DEMUX_OK;
}

static int demux_ivf_seek(demux_plugin_t *this_gen, off_t start_pos, int start_time, int playing)
{
  demux_ivf_t *this = (demux_ivf_t *)this_gen;

  this->seek_flag = 1;

  /* no seek table, only seeking to beginning is supported */
  if (start_pos == 0 && start_time == 0) {

    if (playing)
      _x_demux_flush_engine(this->stream);

    if (this->input->seek (this->input, 32, SEEK_SET) != 32) {
      return this->status;
    }

    this->frame_number = 0;
    this->status = DEMUX_OK;
    return this->status;
  }

  return this->status;
}

static int demux_ivf_get_stream_length(demux_plugin_t *this_gen)
{
  demux_ivf_t *this = (demux_ivf_t *)this_gen;

  if (!this->frame_rate_den)
    return 0;

  return (uint64_t)this->num_frames * 1000 * this->frame_rate_num / this->frame_rate_den;
}

static uint32_t demux_ivf_get_capabilities(demux_plugin_t *this_gen)
{
  (void)this_gen;
  return DEMUX_CAP_NOCAP;
}

static int demux_ivf_get_optional_data(demux_plugin_t *this_gen, void *data, int data_type)
{
  (void)this_gen;
  (void)data;
  (void)data_type;
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin(demux_class_t *class_gen, xine_stream_t *stream, input_plugin_t *input)
{
  demux_ivf_t *this;
  uint8_t      scratch[32];
  size_t       i;
  uint32_t     buf_type = 0;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
    if (_x_demux_read_header(input, scratch, 32) != 32)
      return 0;
    if ( !_x_is_fourcc(scratch, "DKIF") )
      return 0;
    if (_X_LE_16(scratch + 4) != 0 /* version */ ||
        _X_LE_16(scratch + 6) != 32 /* header size */)
      return 0;
    if (!_X_LE_32(scratch + 16) /* frame_rate_den */ ||
        !_X_LE_32(scratch + 20) /* frame_rate_num */ )
      return 0;
    for (i = 0; i < sizeof(ivf_tag_map) / sizeof(ivf_tag_map[0]); i++) {
      if (_x_is_fourcc(&scratch[8], ivf_tag_map[i].fourcc)) {
        buf_type = ivf_tag_map[i].buf_type;
        break;
      }
    }
    if (!buf_type) {
      xprintf(stream->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
              "unsupportd codec tag %4.4s\n", &scratch[8]);
      return 0;
    }
    break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
    break;

  default:
    return NULL;
  }

  this = calloc(1, sizeof(*this));
  if (!this)
    return NULL;

  this->stream   = stream;
  this->input    = input;
  this->status   = DEMUX_FINISHED;
  this->buf_type = buf_type;

  this->demux_plugin.send_headers      = demux_ivf_send_headers;
  this->demux_plugin.send_chunk        = demux_ivf_send_chunk;
  this->demux_plugin.seek              = demux_ivf_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_ivf_get_status;
  this->demux_plugin.get_stream_length = demux_ivf_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_ivf_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ivf_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  return &this->demux_plugin;
}

void *demux_ivf_init_class(xine_t *xine, const void *data)
{
  (void)xine;
  (void)data;

  static const demux_class_t demux_ivf_class = {
    .open_plugin     = open_plugin,
    .description     = N_("IVF demuxer"),
    .identifier      = "ivf",
    .mimetypes       = NULL,
    .extensions      = "ivf",
    .dispose         = NULL,
  };

  return (void *)&demux_ivf_class;
}

