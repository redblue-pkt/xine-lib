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
 * Westwood Studios AUD File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the AUD file format, refer to:
 *   http://www.geocities.com/SiliconValley/8682/aud3.txt
 *
 * Implementation note: There is no definite file signature in this format.
 * This demuxer uses a probabilistic strategy for content detection. This
 * entails performing sanity checks on certain header values in order to
 * qualify a file. Refer to open_aud_file() for the precise parameters.
 *
 * Implementation note #2: The IMA ADPCM data stored in this file format
 * does not encode any initialization information; decoding parameters are
 * initialized to 0 at the start of the file and maintained throughout the
 * data. This makes seeking conceptually impossible. Upshot: Random
 * seeking is not supported.
 *
 * $Id: demux_aud.c,v 1.5 2003/03/07 12:51:47 guenter Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "demux.h"
#include "bswap.h"

#define AUD_HEADER_SIZE 12
#define AUD_CHUNK_PREAMBLE_SIZE 8

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;
  int                  send_end_buffers;

  off_t                data_start;
  off_t                data_size;
  int                  status;

  int                  audio_samplerate;
  int                  audio_channels;
  int                  audio_bits;
  int                  audio_type;
  int64_t              audio_frame_counter;

  char                 last_mrl[1024];

} demux_aud_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_aud_class_t;


/* returns 1 if the AUD file was opened successfully, 0 otherwise */
static int open_aud_file(demux_aud_t *this) {

  unsigned char header[AUD_HEADER_SIZE];

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, header, AUD_HEADER_SIZE) !=
    AUD_HEADER_SIZE)
    return 0;

  /* Probabilistic content detection strategy: There is no file signature
   * so perform sanity checks on various header parameters:
   *   8000 <= sample rate (16 bits) <= 48000  ==> 40001 acceptable numbers
   *   compression type (8 bits) = 1 or 99     ==> 2 acceptable numbers
   * There is a total of 24 bits. The number space contains 2^24 =
   * 16777216 numbers. There are 40001 * 2 = 80002 acceptable combinations 
   * of numbers. There is a 80002/16777216 = 0.48% chance of a false
   * positive.
   */
  this->audio_samplerate = LE_16(&header[0]);
  if ((this->audio_samplerate < 8000) || (this->audio_samplerate > 48000))
    return 0;

  if (header[11] == 1)
    this->audio_type = BUF_AUDIO_WESTWOOD;  
  else if (header[11] == 99)
    this->audio_type = BUF_AUDIO_VQA_IMA;  
  else
    return 0;

  /* flag 0 indicates stereo */
  this->audio_channels = (header[10] & 0x1) + 1;
  /* flag 1 indicates 16 bit audio */
  this->audio_bits = (((header[10] & 0x2) >> 1) + 1) * 8;

  this->data_start = AUD_HEADER_SIZE;
  this->data_size = this->input->get_length(this->input) - this->data_start;
  this->audio_frame_counter = 0;

  return 1;
}

static int demux_aud_send_chunk(demux_plugin_t *this_gen) {

  demux_aud_t *this = (demux_aud_t *) this_gen;
  unsigned char chunk_preamble[AUD_CHUNK_PREAMBLE_SIZE];
  unsigned int chunk_size;
  off_t current_file_pos;
  int64_t audio_pts;
  buf_element_t *buf;

  if (this->input->read(this->input, chunk_preamble, AUD_CHUNK_PREAMBLE_SIZE) !=
    AUD_CHUNK_PREAMBLE_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* validate the chunk */
  if (LE_32(&chunk_preamble[4]) != 0x0000DEAF) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  chunk_size = LE_16(&chunk_preamble[0]);

  current_file_pos = this->input->get_current_pos(this->input) -
    this->data_start;

  /* 2 samples/byte, 1 or 2 samples per frame depending on stereo */
  this->audio_frame_counter += (chunk_size * 2) / this->audio_channels;
  audio_pts = this->audio_frame_counter;
  audio_pts *= 90000;
  audio_pts /= this->audio_samplerate;

  while (chunk_size) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->extra_info->input_pos = current_file_pos;
    buf->extra_info->input_length = this->data_size;
    buf->extra_info->input_time = audio_pts / 90;
    buf->pts = audio_pts;

    if (chunk_size > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = chunk_size;
    chunk_size -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) !=
      buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      break;
    }

    if (!chunk_size)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    this->audio_fifo->put (this->audio_fifo, buf);
  }

  return this->status;
}

static void demux_aud_send_headers(demux_plugin_t *this_gen) {

  demux_aud_t *this = (demux_aud_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->audio_samplerate;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->audio_bits;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to the audio decoder */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->audio_samplerate;
    buf->decoder_info[2] = this->audio_bits;
    buf->decoder_info[3] = this->audio_channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_aud_seek (demux_plugin_t *this_gen,
                               off_t start_pos, int start_time) {

  demux_aud_t *this = (demux_aud_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    /* send new pts */
    xine_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;

    /* reposition stream right after headers */
    this->input->seek(this->input, this->data_start, SEEK_SET);
  }

  return this->status;
}

static void demux_aud_dispose (demux_plugin_t *this) {

  free(this);
}

static int demux_aud_get_status (demux_plugin_t *this_gen) {
  demux_aud_t *this = (demux_aud_t *) this_gen;

  return this->status;
}

static int demux_aud_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static uint32_t demux_aud_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_aud_get_optional_data(demux_plugin_t *this_gen,
                                           void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_aud_t    *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    if (stream->xine->verbosity >= XINE_VERBOSITY_DEBUG) 
      printf(_("demux_aud.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_aud_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_aud_send_headers;
  this->demux_plugin.send_chunk        = demux_aud_send_chunk;
  this->demux_plugin.seek              = demux_aud_seek;
  this->demux_plugin.dispose           = demux_aud_dispose;
  this->demux_plugin.get_status        = demux_aud_get_status;
  this->demux_plugin.get_stream_length = demux_aud_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_aud_get_capabilities;
  this->demux_plugin.get_optional_data = demux_aud_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_aud_file(this)) {
      free (this);
      return NULL;
    }
  break;

  case METHOD_BY_EXTENSION: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');

    if (!ending) {
      free (this);
      return NULL;
    }

    if (strncasecmp (ending, ".aud", 4)) {
      free (this);
      return NULL;
    }

    if (!open_aud_file(this)) {
      free (this);
      return NULL;
    }

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
  return "Westwood Studios AUD file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "Westwood Studios AUD";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "aud";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_aud_class_t *this = (demux_aud_class_t *) this_gen;

  free (this);
}

void *demux_aud_init_plugin (xine_t *xine, void *data) {

  demux_aud_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_aud_class_t));
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

/*
 * exported plugin catalog entry
 */
#if 0
plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 20, "aud", XINE_VERSION_CODE, NULL, demux_aud_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif
