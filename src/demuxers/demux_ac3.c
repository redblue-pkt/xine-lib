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
 * $Id: demux_ac3.c,v 1.1 2003/01/22 01:30:07 tmmm Exp $
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

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  status;

  char                 last_mrl[1024];
} demux_ac3_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_ac3_class_t;

/* returns 1 if the AC3 file was opened successfully, 0 otherwise */
static int open_ac3_file(demux_ac3_t *this) {

  unsigned char sync_mark[2];

  /* check if the sync mark matches up */
  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, sync_mark, 2) != 2)
    return 0;

  if ((sync_mark[0] == 0x0B) &&
      (sync_mark[1] == 0x77))
    return 1;
  else
    return 0;
}

static int demux_ac3_send_chunk (demux_plugin_t *this_gen) {

  demux_ac3_t *this = (demux_ac3_t *) this_gen;
  buf_element_t *buf = NULL;
  off_t current_file_pos;
  int64_t audio_pts;
  int bytes_read;

  current_file_pos = this->input->get_current_pos(this->input);

  /* let the decoder sort out the pts for now */
  audio_pts = 0;

  /* read a buffer-sized block from the stream; if there is less than a
   * buffer of data, send whatever there is; if there are no bytes returned,
   * demux is finished */
  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_A52;
  bytes_read = this->input->read(this->input, buf->content, buf->max_size);
  if (bytes_read == 0) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return this->status;
  } else if (bytes_read < buf->max_size)
    buf->size = bytes_read;
  else
    buf->size = buf->max_size;

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

  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    /* send new pts */
    xine_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;

    /* start at the beginning of the file */
    this->input->seek(this->input, 0, SEEK_SET);
  }

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

/* return the approximate length in miliseconds */
static int demux_ac3_get_stream_length (demux_plugin_t *this_gen) {

  demux_ac3_t *this = (demux_ac3_t *) this_gen;

  return 0;
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

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

printf ("  **** by content...\n");
    if (!open_ac3_file(this)) {
      free (this);
      return NULL;
    }

  break;

  case METHOD_BY_EXTENSION: {
    char *ending, *mrl;

printf ("  **** by extension...\n");
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
