/*
 * Copyright (C) 2001-2002 the xine project
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
 * RealAudio File Demuxer by Mike Melanson (melanson@pcisys.net)
 *
 * $Id: demux_realaudio.c,v 1.13 2003/01/17 16:25:49 miguelfreitas Exp $
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

#define RA_FILE_HEADER_SIZE 16
#define RA_AUDIO_HEADER_SIZE 0x39

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  status;

  xine_waveformatex    wave;
  unsigned int         audio_fourcc;
  unsigned int         audio_type;

  off_t                data_start;
  off_t                data_size;

  int                  seek_flag;  /* this is set when a seek just occurred */

  char                 last_mrl[1024];
} demux_ra_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_ra_class_t;

/* returns 1 if the RealAudio file was opened successfully, 0 otherwise */
static int open_ra_file(demux_ra_t *this) {

  unsigned char file_header[RA_FILE_HEADER_SIZE];
  unsigned char audio_header[RA_AUDIO_HEADER_SIZE];

  /* check the signature */
  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, file_header, RA_FILE_HEADER_SIZE) !=
    RA_FILE_HEADER_SIZE)
    return 0;

  if ((file_header[0] != '.') ||
      (file_header[1] != 'r') ||
      (file_header[2] != 'a'))
    return 0;

  /* load the audio header */
  if (this->input->read(this->input, audio_header, RA_AUDIO_HEADER_SIZE) !=
    RA_AUDIO_HEADER_SIZE)
    return 0;

  /* find the important information */
  this->data_start = RA_FILE_HEADER_SIZE + RA_AUDIO_HEADER_SIZE;
  this->data_size = BE_32(&audio_header[0x0C]);
  this->wave.nChannels = audio_header[0x27];
  this->wave.nSamplesPerSec = BE_16(&audio_header[0x20]);
  this->wave.nBlockAlign = BE_16(&audio_header[0x1A]);
  this->wave.wBitsPerSample = audio_header[0x25];
  this->audio_fourcc = *(unsigned int *)&audio_header[0x2E];
  this->audio_type = formattag_to_buf_audio(this->audio_fourcc);

  if( !this->audio_type )
    this->audio_type = BUF_AUDIO_UNKNOWN;
  
  return 1;
}

static int demux_ra_send_chunk(demux_plugin_t *this_gen) {

  demux_ra_t *this = (demux_ra_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int remaining_sample_bytes;
  off_t current_file_pos;
  int64_t current_pts;

  /* just load data chunks from wherever the stream happens to be
   * pointing; issue a DEMUX_FINISHED status if EOF is reached */
  remaining_sample_bytes = this->wave.nBlockAlign;
  current_file_pos =
    this->input->get_current_pos(this->input) - this->data_start;



  current_pts = 0;  /* let the engine sort out the pts for now */



  if (this->seek_flag) {
    xine_demux_control_newpts(this->stream, current_pts, 0);
    this->seek_flag = 0;
  }

  while (remaining_sample_bytes) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->extra_info->input_pos = current_file_pos;
    buf->extra_info->input_length = this->data_size;
    buf->extra_info->input_time = current_pts / 90;
    buf->pts = current_pts;

    if (remaining_sample_bytes > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = remaining_sample_bytes;
    remaining_sample_bytes -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) !=
      buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      break;
    }

    if (!remaining_sample_bytes)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    this->audio_fifo->put (this->audio_fifo, buf);
  }
  return this->status;
}

static void demux_ra_send_headers(demux_plugin_t *this_gen) {

  demux_ra_t *this = (demux_ra_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->wave.nChannels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->wave.nSamplesPerSec;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->wave.wBitsPerSample;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->wave.nSamplesPerSec;
    buf->decoder_info[2] = this->wave.wBitsPerSample;
    buf->decoder_info[3] = this->wave.nChannels;
    memcpy(buf->content, &this->wave, sizeof(this->wave));
    buf->size = sizeof(this->wave);
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_ra_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  demux_ra_t *this = (demux_ra_t *) this_gen;

  /* check the boundary offsets */
  if (start_pos <= 0)
    this->input->seek(this->input, this->data_start, SEEK_SET);
  else if (start_pos >= this->data_size) {
    this->status = DEMUX_FINISHED;
    return this->status;
  } else {
    /* This function must seek along the block alignment. The start_pos
     * is in reference to the start of the data. Divide the start_pos by
     * the block alignment integer-wise, and multiply the quotient by the
     * block alignment to get the new aligned offset. Add the data start
     * offset and seek to the new position. */
    start_pos /= this->wave.nBlockAlign;
    start_pos *= this->wave.nBlockAlign;
    start_pos += this->data_start;

    this->input->seek(this->input, start_pos, SEEK_SET);
  }

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  xine_demux_flush_engine (this->stream);
  
  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    this->status = DEMUX_OK;
  }

  return this->status;
}


static void demux_ra_dispose (demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;

  free(this);
}

static int demux_ra_get_status (demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_ra_get_stream_length (demux_plugin_t *this_gen) {

  demux_ra_t *this = (demux_ra_t *) this_gen;

  if(this->wave.nAvgBytesPerSec) 
    return (int)((int64_t) this->data_size * 1000 / this->wave.nAvgBytesPerSec);
  else 
    return 0;
}

static uint32_t demux_ra_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_ra_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_ra_t     *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_ra.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_ra_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_ra_send_headers;
  this->demux_plugin.send_chunk        = demux_ra_send_chunk;
  this->demux_plugin.seek              = demux_ra_seek;
  this->demux_plugin.dispose           = demux_ra_dispose;
  this->demux_plugin.get_status        = demux_ra_get_status;
  this->demux_plugin.get_stream_length = demux_ra_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_ra_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ra_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_ra_file(this)) {
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

    if (strncasecmp (ending, ".ra", 3)) {
      free (this);
      return NULL;
    }

    if (!open_ra_file(this)) {
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

  /* print vital stats */
  xine_log (this->stream->xine, XINE_LOG_MSG,
    _("demux_realaudio: RealAudio file, %d bits, %d Hz, %s %c%c%c%c audio\n"),
    this->wave.wBitsPerSample,
    this->wave.nSamplesPerSec,
    (this->wave.nChannels == 1) ? "monaural" : "stereo",
    *((char *)&this->audio_fourcc + 0),
    *((char *)&this->audio_fourcc + 1),
    *((char *)&this->audio_fourcc + 2),
    *((char *)&this->audio_fourcc + 3));

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "RealAudio file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "RA";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "ra";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_ra_class_t *this = (demux_ra_class_t *) this_gen;

  free (this);
}

void *demux_realaudio_init_plugin (xine_t *xine, void *data) {

  demux_ra_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_ra_class_t));
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
  { PLUGIN_DEMUX, 20, "realaudio", XINE_VERSION_CODE, NULL, demux_realaudio_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif
