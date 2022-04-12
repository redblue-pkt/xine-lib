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

typedef enum {
  DEMUX_AAC_ADTS = 0,
  DEMUX_AAC_ADIF
} demux_aac_mode_t;

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  demux_aac_mode_t     mode;
  int                  status;
  int                  id3v2_tag_size;
  int                  last_read_res;
  int                  pts_offs;
  /* this is set when a seek just occurred */
  int                  seek_flag;
  /* time of last config change */
  int64_t              base_pts;
  /* frames since that */
  uint32_t             frame_num;
  /* frames in current sync */
  uint32_t             frame_count;
  /* current config. in sbr mode, these really are to be doubled, but we just want the ratio ;-) */
  uint32_t             samples_per_frame;
  uint32_t             samples_per_second;
  /* maximum ADTS frame size is 13 bits (8191). */
  uint32_t             bgot, bdelivered;
  uint8_t              buf[9 << 10];
} demux_aac_t;


static int probe_aac_file (xine_stream_t *stream, input_plugin_t *input, demux_aac_mode_t *mode) {
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
    *mode = DEMUX_AAC_ADIF;
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
  *mode = DEMUX_AAC_ADTS;
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

/* FIXME: both ADTS and ADIF allow the first 4 AAC object types only,
 * and have no short frame bit like in a regular config bitstream.
 * for now, just assume always 1024 frame samples. */
static uint32_t demux_aac_samples_per_frame (uint32_t object_type) {
  (void)object_type;
  return 1024;
}

static const uint32_t demux_aac_sample_rates[16] = {
  96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
  16000, 12000, 11025,  8000,     0,     0,     0,     0
};

static void demux_aac_apply_adts (demux_aac_t *this, const uint8_t *buf) {
  uint32_t samples_per_frame, samples_per_second = 48000, object_type;
  uint32_t word = _X_BE_32 (buf);

  object_type = ((word >> 14) & 3) + 1;
  samples_per_frame = demux_aac_samples_per_frame (object_type);
  samples_per_second = demux_aac_sample_rates[(word >> 10) & 15];
  if (!samples_per_second)
    return;
  this->frame_count = (buf[6] & 3) + 1;
  if ((this->samples_per_frame ^ samples_per_frame) | (this->samples_per_second ^ samples_per_second)) {
    if (this->samples_per_second)
      this->base_pts += (int64_t)this->frame_num * 90000 * (int64_t)this->samples_per_frame / (int64_t)this->samples_per_second;
    this->frame_num = 0;
    this->samples_per_frame = samples_per_frame;
    this->samples_per_second = samples_per_second;
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      LOG_MODULE ": ADTS frame duration %u/%u.\n",
      (unsigned int)this->samples_per_frame, (unsigned int)this->samples_per_second);
  }
}

static uint32_t demux_aac_get_bits (const uint8_t *buf, uint32_t bitpos, uint32_t bits) {
  uint32_t word = _X_BE_32 (buf + (bitpos >> 3));

  return (word << (bitpos & 7)) >> (32 - bits);
}

#undef _FULL_ADIF

static void demux_aac_apply_adif (demux_aac_t *this, const uint8_t *buf) {
  uint32_t samples_per_frame, samples_per_second = 48000;
  uint32_t word, bitpos = 0;
  uint32_t object_type, sf_index, bitstream_type;
#ifdef _FULL_ADIF
  uint32_t num_program_config_elements, u;
  uint32_t num_front_channel_elements, num_side_channel_elements;
  uint32_t num_back_channel_elements, num_lfe_channel_elements;
  uint32_t num_assoc_data_elements, num_valid_cc_elements;
#endif

  bitpos += 32; /* "AIDF" */
  word = demux_aac_get_bits (buf, bitpos, 1); bitpos += 1; /* copyright */
  if (word)
    bitpos += 72; /* copyright_string */
  bitpos += 2; /* original_copy:1, home:1 */
  bitstream_type = demux_aac_get_bits (buf, bitpos, 1); bitpos += 1;
  bitpos += 23; /* bitrate:23 */
#ifdef _FULL_ADIF
  num_program_config_elements = demux_aac_get_bits (buf, bitpos, 4) + 1;
#endif
  bitpos += 4;
#ifdef _FULL_ADIF
  for (u = 0; u < num_program_config_elements; u++)
#endif
  {
    if (!bitstream_type)
      bitpos += 20; /* buffer_fullness:20 */
    bitpos += 4; /* element_instance_tag:4 */
    object_type = demux_aac_get_bits (buf, bitpos, 2) + 1; bitpos += 2;
    sf_index = demux_aac_get_bits (buf, bitpos, 4); bitpos += 4;
#ifdef _FULL_ADIF
    num_front_channel_elements = demux_aac_get_bits (buf, bitpos, 4); bitpos += 4;
    num_side_channel_elements = demux_aac_get_bits (buf, bitpos, 4); bitpos += 4;
    num_back_channel_elements = demux_aac_get_bits (buf, bitpos, 4); bitpos += 4;
    num_lfe_channel_elements = demux_aac_get_bits (buf, bitpos, 2); bitpos += 2;
    num_assoc_data_elements = demux_aac_get_bits (buf, bitpos, 3); bitpos += 3;
    num_valid_cc_elements = demux_aac_get_bits (buf, bitpos, 4); bitpos += 4;
    word = demux_aac_get_bits (buf, bitpos, 1); bitpos += 1; /* mono_mixdown_present:1 */
    if (word)
      bitpos += 4; /* mono_mixdown_element_number:4 */
    word = demux_aac_get_bits (buf, bitpos, 1); bitpos += 1; /* stereo_mixdown_present:1 */
    if (word)
      bitpos += 4; /* stereo_mixdown_element_number:4 */
    word = demux_aac_get_bits (buf, bitpos, 1); bitpos += 1; /* matrix_mixdown_idx_present:1 */
    if (word)
      bitpos += 3; /* matrix_mixdown_idx:2, pseudo_surround_enable:1 */
    bitpos += num_front_channel_elements * 5; /* front_element_is_cpe[i]:1, front_element_tag_select[i]:4 */
    bitpos += num_side_channel_elements * 5; /* side_element_is_cpe[i]:1, side_element_tag_select[i]:4 */
    bitpos += num_back_channel_elements * 5; /* back_element_is_cpe[i]:1, back_element_tag_select[i]:4 */
    bitpos += num_lfe_channel_elements * 4; /* lfe_element_tag_select[i]:4 */
    bitpos += num_assoc_data_elements * 4; /* assoc_data_element_tag_select[i]:4 */
    bitpos += num_valid_cc_elements * 5; /* cc_element_is_ind_sw[i]:1, valid_cc_element_tag_select[i]:4 */
    bitpos = (bitpos + 7) & ~7; /* byte align */
    word = buf[bitpos >> 3]; /* comment_field_bytes */
    bitpos += word << 3; /* comment_field_data[i] */
#endif
  }

  samples_per_frame = demux_aac_samples_per_frame (object_type);
  samples_per_second = demux_aac_sample_rates[sf_index];
  if (!samples_per_second)
    return;
#ifdef _FULL_ADIF
  this->frame_count = num_program_config_elements;
#else
  this->frame_count = 1;
#endif
  if ((this->samples_per_frame ^ samples_per_frame) | (this->samples_per_second ^ samples_per_second)) {
    if (this->samples_per_second)
      this->base_pts += (int64_t)this->frame_num * 90000 * (int64_t)this->samples_per_frame / (int64_t)this->samples_per_second;
    this->frame_num = 0;
    this->samples_per_frame = samples_per_frame;
    this->samples_per_second = samples_per_second;
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      LOG_MODULE ": AIDF frame duration %u/%u.\n",
      (unsigned int)this->samples_per_frame, (unsigned int)this->samples_per_second);
  }
}

static int demux_aac_next (demux_aac_t *this, uint8_t *buf) {
  if (this->mode == DEMUX_AAC_ADTS) {
    uint32_t u = this->bdelivered + 7, word = 0;

    while (1) {
      if (u < this->bgot) {
        uint32_t size = (_X_BE_32 (this->buf + this->bdelivered + 2) >> 5) & 0x1fff;

        do {
          word = (word << 8) + this->buf[u++];
          if ((word & 0xfff6) == 0xfff0)
            break;
        } while (u < this->bgot);
        if ((word & 0xfff6) == 0xfff0) {
          if (this->bdelivered + size <= u - 2)
            break;
          continue;
        }
      }
      if (u > sizeof (this->buf) - 512) {
        uint32_t l = this->bgot - this->bdelivered;

        if (this->bdelivered < 512) /* a single frame of 8 kbyte ?? */
          l = 4;
        if (this->bdelivered >= l)
          memcpy (this->buf, this->buf + this->bdelivered, l);
        else
          memmove (this->buf, this->buf + this->bdelivered, l);
        u -= this->bdelivered;
        this->bdelivered = 0;
        this->bgot = l;
      }
      this->last_read_res = this->input->read (this->input, this->buf + this->bgot, 512);
      if (this->last_read_res <= 0)
        break;
      this->bgot += this->last_read_res;
    }
    if ((word & 0xfff6) == 0xfff0) {
      demux_aac_apply_adts (this, this->buf + this->bdelivered);
      u -= 2 + this->bdelivered;
      memcpy (buf, this->buf + this->bdelivered, u);
      this->bdelivered += u;
      return u;
    }
  } else { /* DEMUX_AAC_ADIF */
    /* SIGH. ADIF is merely a single head plus a sequence of raw frames.
     * This can hardly do more than play from the beginning.
     * nobody seems to use that anymore, and even ffmpeg doer not support all that.
     * lets not repeat half the decoders work here just to count frames,
     * and let decoder and engine add time info. */
    int r = this->input->read (this->input, buf, 2048);

    if (r > 0) {
      if ((r > 4) && !memcmp (buf, "ADIF", 4))
        demux_aac_apply_adif (this, buf);
    }
    this->frame_count = 0;
    return r;
  }
  {
    uint32_t l = this->bgot - this->bdelivered;

    if (l)
      memcpy (buf, this->buf + this->bdelivered, l);
    this->bgot = 0;
    this->bdelivered = 0;
    return l;
  }
}

static int demux_aac_send_chunk(demux_plugin_t *this_gen) {
  demux_aac_t *this = (demux_aac_t *) this_gen;
  off_t current_pos = this->input->get_current_pos (this->input) + this->bdelivered - this->bgot;
  off_t length = this->input->get_length (this->input);
  int bitrate = _x_stream_info_get (this->stream, XINE_STREAM_INFO_AUDIO_BITRATE);
  buf_element_t *buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

  if (this->seek_flag) {
    this->seek_flag = 0;
    _x_demux_control_newpts (this->stream, this->base_pts, BUF_FLAG_SEEK);
  }

  buf->type = BUF_AUDIO_AAC;
  buf->decoder_flags |= BUF_FLAG_FRAME_END;
  buf->size = demux_aac_next (this, buf->content);
  if (buf->size > 0) {
    if (!this->frame_count) {
      buf->pts = this->base_pts;
      buf->extra_info->input_time = -1;
    } else if (this->samples_per_second) {
      buf->pts = this->base_pts + this->pts_offs
               + (int64_t)this->frame_num * 90000 * (int64_t)this->samples_per_frame / (int64_t)this->samples_per_second;
      buf->extra_info->input_time = buf->pts / 90;
    } else if (bitrate > 0) {
      buf->pts = this->base_pts;
      buf->extra_info->input_time = 8000 * current_pos / bitrate;
    }
    if (length > 0)
      buf->extra_info->input_normpos = (int)((double)current_pos * 65535 / length);
    this->audio_fifo->put (this->audio_fifo, buf);
    this->frame_num += this->frame_count;
  } else {
    buf->free_buffer (buf);
    this->status = DEMUX_FINISHED;
  }
  return this->status;
}

static void demux_aac_send_headers(demux_plugin_t *this_gen) {
  demux_aac_t *this = (demux_aac_t *) this_gen;
  buf_element_t *buf;

  this->audio_fifo  = this->stream->audio_fifo;

  this->pts_offs = this->input->get_optional_data (this->input, NULL, INPUT_OPTIONAL_DATA_PTSOFFS);

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

  if (playing) {
    int ntime = 0;
    off_t pos = -1;
    uint32_t input_caps = this->input->get_capabilities (this->input);

    this->pts_offs = this->input->get_optional_data (this->input, NULL, INPUT_OPTIONAL_DATA_PTSOFFS);

    if (input_caps & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE | INPUT_CAP_TIME_SEEKABLE)) {
      /* this container has neither frame num nor pts. maybe input can help? */
      if ((input_caps & INPUT_CAP_TIME_SEEKABLE) && start_time) {
        pos = this->input->seek_time (this->input, start_time, SEEK_SET);
        if (pos >= 0)
          ntime = this->input->get_current_time ? this->input->get_current_time (this->input) : 0;
      } else {
        off_t len = this->input->get_length (this->input);
        int br = _x_stream_info_get (this->stream, XINE_STREAM_INFO_AUDIO_BITRATE);

        if (len > 0) {
          pos = this->input->seek (this->input, (int64_t)len * start_pos / (int)0xffff, SEEK_SET);
          if (pos >= 0)
            ntime = br > 0 ? (int64_t)pos * 8000 / br : 0;
        }
      }
      if (pos >= 0) {
        this->base_pts = (int64_t)ntime * 90;
        this->frame_num = 0;
        this->bdelivered = 0;
        this->bgot = 0;
        this->seek_flag = 1;
        this->status = DEMUX_OK;
      }
    }
  } else {
    /* if thread is not running, initialize demuxer */
    /* send new pts */
    _x_demux_control_newpts (this->stream, 0, 0);
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
  demux_aac_mode_t mode;
  int id3v2_tag_size;

  switch (stream->content_detection_method) {
    case METHOD_BY_MRL:
    case METHOD_BY_CONTENT:
    case METHOD_EXPLICIT:
      if ((id3v2_tag_size = probe_aac_file (stream, input, &mode)) < 0)
        return NULL;
      break;
    default:
      return NULL;
  }

  this = calloc(1, sizeof(demux_aac_t));
  if (!this)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  this->seek_flag = 0;
  this->pts_offs = 0;
  this->base_pts = 0;
  this->frame_num = 0;
  this->samples_per_frame = 0;
  this->samples_per_second = 0;
  this->bdelivered = 0;
  this->bgot   = 0;
#endif
  this->frame_count = 1;
  this->last_read_res = 1;
  this->stream = stream;
  this->input  = input;
  this->mode   = mode;
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
