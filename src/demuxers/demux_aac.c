/*
 * Copyright (C) 2001-2018 the xine project
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
 * Raw AAC File Demuxer by Mike Melanson (melanson@pcisys.net)
 * This demuxer detects ADIF and ADTS headers in AAC files.
 * Then it shovels buffer-sized chunks over to the AAC decoder.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define LOG_MODULE "demux_aac"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "bswap.h"
#include "group_audio.h"

#include "id3.h"

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  int                  id3v2_tag_size;

  int                  seek_flag;  /* this is set when a seek just occurred */
} demux_aac_t;


static int probe_aac_file(xine_stream_t *stream, input_plugin_t *input) {
  uint8_t buf[MAX_PREVIEW_SIZE];
  uint16_t syncword = 0;
  int data_start = -1, bsize, i;

  /* Check if there's an ID3v2 tag at the start */
  data_start = xine_parse_id3v2_tag (stream, input);
  lprintf("Getting a buffer of size %u\n", sizeof (buf));
  bsize = _x_demux_read_stream_header (stream, input, buf, sizeof (buf));
  if (bsize < 10)
    return -1;

  /* Check for an ADIF header - should be at the start of the file */
  if (_x_is_fourcc (buf, "ADIF")) {
    lprintf("found ADIF header\n");
    return data_start;
  }

  /* Look for an ADTS header - might not be at the start of the file */
  for (i = 0; i < bsize; i++) {
    if ((syncword & 0xfff6) == 0xfff0)
      break;
    syncword = (syncword << 8) | buf[i];
  }

  /* did we really find the ADTS header? */
  if (i == bsize)
    return -1; /* No, we didn't */

  data_start += i - 2;
  lprintf ("found ADTS header at offset %d\n", i - 2);

  /* Look for second ADTS header to confirm it's really aac */
  if (data_start + 5 < bsize) {
    int frame_size = (_X_BE_32 (buf + data_start + 2) >> 5) & 0x1fff;

    lprintf("first frame size %d\n", frame_size);

    if ((frame_size > 0) && (data_start + frame_size + 4 <= bsize) &&
      /* first 28 bits must be identical */
      !((_X_BE_32 (buf + data_start) ^ _X_BE_32 (buf + data_start + frame_size)) & 0xfffffff0)) {
      lprintf("found second ADTS header\n");
      if (input->seek (input, data_start, SEEK_SET) < 0)
        input->seek (input, data_start + frame_size, SEEK_SET);
      return data_start;
    }
  }

  return -1;
}

static int demux_aac_send_chunk(demux_plugin_t *this_gen) {
  demux_aac_t *this = (demux_aac_t *) this_gen;
  int bytes_read;
  off_t current_pos, length;
  uint32_t bitrate;

  buf_element_t *buf = NULL;

  /* just load an entire buffer from wherever the audio happens to be */
  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_AAC;
  buf->pts = 0;

  length = this->input->get_length(this->input);
  current_pos = this->input->get_current_pos(this->input);
  bitrate = _x_stream_info_get(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE);

  if (length)
    buf->extra_info->input_normpos = (int)((double) current_pos * 65535/length);

  if (bitrate)
    buf->extra_info->input_time = (8*current_pos) / (bitrate/1000);

  bytes_read = this->input->read(this->input, buf->content, buf->max_size);
  if (bytes_read <= 0) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return this->status;
  } else
    buf->size = bytes_read;

  /* each buffer stands on its own */
  buf->decoder_flags |= BUF_FLAG_FRAME_END;

  this->audio_fifo->put (this->audio_fifo, buf);

  return this->status;
}

static void demux_aac_send_headers(demux_plugin_t *this_gen) {
  demux_aac_t *this = (demux_aac_t *) this_gen;
  buf_element_t *buf;

  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_AAC;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_FRAME_END;
    buf->content = NULL;
    buf->size = 0;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_aac_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {
  demux_aac_t *this = (demux_aac_t *) this_gen;

  (void)start_pos;
  (void)start_time;
  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static int demux_aac_get_status (demux_plugin_t *this_gen) {
  demux_aac_t *this = (demux_aac_t *) this_gen;

  return this->status;
}

static int demux_aac_get_stream_length (demux_plugin_t *this_gen) {
//  demux_aac_t *this = (demux_aac_t *) this_gen;

  (void)this_gen;
  return 0;
}

static uint32_t demux_aac_get_capabilities(demux_plugin_t *this_gen) {
  (void)this_gen;
  return DEMUX_CAP_NOCAP;
}

static int demux_aac_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  (void)this_gen;
  (void)data;
  (void)data_type;
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_aac_t    *this;
  int id3v2_tag_size;

  switch (stream->content_detection_method) {
    case METHOD_BY_MRL:
    case METHOD_BY_CONTENT:
    case METHOD_EXPLICIT:
      if ((id3v2_tag_size = probe_aac_file (stream, input)) < 0)
        return NULL;
      break;
    default:
      return NULL;
  }

  this = calloc(1, sizeof(demux_aac_t));
  if (!this)
    return NULL;

  this->stream = stream;
  this->input  = input;
  this->id3v2_tag_size = id3v2_tag_size;

  this->demux_plugin.send_headers      = demux_aac_send_headers;
  this->demux_plugin.send_chunk        = demux_aac_send_chunk;
  this->demux_plugin.seek              = demux_aac_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_aac_get_status;
  this->demux_plugin.get_stream_length = demux_aac_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_aac_get_capabilities;
  this->demux_plugin.get_optional_data = demux_aac_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  /* _x_stream_info_set(stream, XINE_STREAM_INFO_HAS_VIDEO, 0); */
  _x_stream_info_set(stream, XINE_STREAM_INFO_HAS_AUDIO, 1);

  return &this->demux_plugin;
}

void *demux_aac_init_plugin (xine_t *xine, const void *data) {

  (void)xine;
  (void)data;

  static const demux_class_t demux_aac_class = {
    .open_plugin     = open_plugin,
    .description     = N_("ADIF/ADTS AAC demux plugin"),
    .identifier      = "AAC",
    .mimetypes       = NULL,
    .extensions      = "aac",
    .dispose         = NULL,
  };

  return (void*)&demux_aac_class;
}

