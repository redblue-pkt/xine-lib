/*
 * Copyright (C) 2001-2003 the xine project
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
 * AC3 File Demuxer by Mike Melanson (melanson@pcisys.net)
 * This demuxer detects raw AC3 data in a file and shovels AC3 data
 * directly to the AC3 decoder.
 *
 * $Id: demux_ac3.c,v 1.5 2003/02/27 22:26:48 mroi Exp $
 *
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

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "buffer.h"
#include "bswap.h"

#define AC3_PREAMBLE_BYTES 5

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  status;
  int                  seek_flag;

  int                  sample_rate;
  int                  frame_size;
  int                  running_time;

  char                 last_mrl[1024];
} demux_ac3_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_ac3_class_t;

/* borrow some knowledge from the AC3 decoder */
struct frmsize_s
{
  uint16_t bit_rate;
  uint16_t frm_size[3];
};

static const struct frmsize_s frmsizecod_tbl[64] =
{
  { 32  ,{64   ,69   ,96   } },
  { 32  ,{64   ,70   ,96   } },
  { 40  ,{80   ,87   ,120  } },
  { 40  ,{80   ,88   ,120  } },
  { 48  ,{96   ,104  ,144  } },
  { 48  ,{96   ,105  ,144  } },
  { 56  ,{112  ,121  ,168  } },
  { 56  ,{112  ,122  ,168  } },
  { 64  ,{128  ,139  ,192  } },
  { 64  ,{128  ,140  ,192  } },
  { 80  ,{160  ,174  ,240  } },
  { 80  ,{160  ,175  ,240  } },
  { 96  ,{192  ,208  ,288  } },
  { 96  ,{192  ,209  ,288  } },
  { 112 ,{224  ,243  ,336  } },
  { 112 ,{224  ,244  ,336  } },
  { 128 ,{256  ,278  ,384  } },
  { 128 ,{256  ,279  ,384  } },
  { 160 ,{320  ,348  ,480  } },
  { 160 ,{320  ,349  ,480  } },
  { 192 ,{384  ,417  ,576  } },
  { 192 ,{384  ,418  ,576  } },
  { 224 ,{448  ,487  ,672  } },
  { 224 ,{448  ,488  ,672  } },
  { 256 ,{512  ,557  ,768  } },
  { 256 ,{512  ,558  ,768  } },
  { 320 ,{640  ,696  ,960  } },
  { 320 ,{640  ,697  ,960  } },
  { 384 ,{768  ,835  ,1152 } },
  { 384 ,{768  ,836  ,1152 } },
  { 448 ,{896  ,975  ,1344 } },
  { 448 ,{896  ,976  ,1344 } },
  { 512 ,{1024 ,1114 ,1536 } },
  { 512 ,{1024 ,1115 ,1536 } },
  { 576 ,{1152 ,1253 ,1728 } },
  { 576 ,{1152 ,1254 ,1728 } },
  { 640 ,{1280 ,1393 ,1920 } },
  { 640 ,{1280 ,1394 ,1920 } }
};

/* returns 1 if the AC3 file was opened successfully, 0 otherwise */
static int open_ac3_file(demux_ac3_t *this) {

  unsigned char preamble[AC3_PREAMBLE_BYTES];

  /* check if the sync mark matches up */
  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, preamble, AC3_PREAMBLE_BYTES) != 
    AC3_PREAMBLE_BYTES)
    return 0;

  if ((preamble[0] != 0x0B) ||
      (preamble[1] != 0x77))
    return 0;

  this->sample_rate = preamble[4] >> 6;
  if (this->sample_rate > 2)
    return 0;

  this->frame_size = 
    frmsizecod_tbl[preamble[4] & 0x3F].frm_size[this->sample_rate];

  /* convert the sample rate to a more useful number */
  if (this->sample_rate == 0)
    this->sample_rate = 48000;
  else if (this->sample_rate == 1)
    this->sample_rate = 44100;
  else
    this->sample_rate = 32000;

  this->running_time = this->input->get_length(this->input);
  this->running_time /= this->frame_size;
  this->running_time *= (90000 / 1000) * (256 * 3);
  this->running_time /= this->sample_rate;

  return 1;
}

static int demux_ac3_send_chunk (demux_plugin_t *this_gen) {

  demux_ac3_t *this = (demux_ac3_t *) this_gen;
  buf_element_t *buf = NULL;
  off_t current_file_pos;
  int64_t audio_pts;
  int bytes_read;
  int frame_number;

  current_file_pos = this->input->get_current_pos(this->input);
  frame_number = current_file_pos / this->frame_size;

  /* 
   * Each frame represents 256 new audio samples according to the a52 spec.
   * Thus, the pts computation should work something like:
   *
   *   pts = frame #  *  256 samples        1 sec        90000 pts
   *                     -----------  *  -----------  *  ---------
   *                       1 frame       sample rate       1 sec
   *
   * For some reason, the computation only works out correctly
   * assuming 256 * 3 = 768 samples/frame.
   */
  audio_pts = frame_number;
  audio_pts *= 90000;
  audio_pts *= 256 * 3;
  audio_pts /= this->sample_rate;

  if (this->seek_flag) {
    xine_demux_control_newpts(this->stream, audio_pts, 0);
    this->seek_flag = 0;
  }

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_A52;
  bytes_read = this->input->read(this->input, buf->content, this->frame_size);
  if (bytes_read == 0) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  buf->size = bytes_read;

  buf->extra_info->input_pos = current_file_pos;
  buf->extra_info->input_length = this->input->get_length(this->input);
  buf->extra_info->input_time = audio_pts / 90;
  buf->pts = audio_pts;
  buf->decoder_flags |= BUF_FLAG_FRAME_END;

  this->audio_fifo->put (this->audio_fifo, buf);

  return this->status;
}

static void demux_ac3_send_headers(demux_plugin_t *this_gen) {

  demux_ac3_t *this = (demux_ac3_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_A52;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->size = 0;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_ac3_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  demux_ac3_t *this = (demux_ac3_t *) this_gen;

  /* divide the requested offset integer-wise by the frame alignment and
   * multiply by the frame alignment to determine the new starting block */
  start_pos /= this->frame_size;
  start_pos *= this->frame_size;
  this->input->seek(this->input, start_pos, SEEK_SET);

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  xine_demux_flush_engine (this->stream);

  return this->status;
}

static void demux_ac3_dispose (demux_plugin_t *this_gen) {
  demux_ac3_t *this = (demux_ac3_t *) this_gen;

  free(this);
}

static int demux_ac3_get_status (demux_plugin_t *this_gen) {
  demux_ac3_t *this = (demux_ac3_t *) this_gen;

  return this->status;
}

/* return the approximate length in milliseconds */
static int demux_ac3_get_stream_length (demux_plugin_t *this_gen) {

  demux_ac3_t *this = (demux_ac3_t *) this_gen;

  return this->running_time;
}

static uint32_t demux_ac3_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_ac3_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                   input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_ac3_t   *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_ac3.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_ac3_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_ac3_send_headers;
  this->demux_plugin.send_chunk        = demux_ac3_send_chunk;
  this->demux_plugin.seek              = demux_ac3_seek;
  this->demux_plugin.dispose           = demux_ac3_dispose;
  this->demux_plugin.get_status        = demux_ac3_get_status;
  this->demux_plugin.get_stream_length = demux_ac3_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_ac3_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ac3_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_EXTENSION: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');

    if (!ending) {
      free (this);
      return NULL;
    }

    if (strncasecmp (ending, ".ac3", 4)) {
      free (this);
      return NULL;
    }

  }
  /* falling through is intended */
  
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_ac3_file(this)) {
      free (this);
      return NULL;
    }

  break;


  default:
    free (this);
    return NULL;
  }

  strncpy (this->last_mrl, input->get_mrl (input), 1024);

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "Raw AC3 file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "AC3";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "ac3";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_ac3_class_t *this = (demux_ac3_class_t *) this_gen;

  free (this);
}

void *demux_ac3_init_plugin (xine_t *xine, void *data) {

  demux_ac3_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_ac3_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}
