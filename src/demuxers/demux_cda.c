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
 * $Id: demux_cda.c,v 1.36 2002/12/08 21:43:50 miguelfreitas Exp $
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

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  off_t                start;

  int                  status;
  int                  blocksize;

  char                 last_mrl[1024];
} demux_cda_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_cda_class_t;

/*
 *
 */
static int demux_cda_next (demux_cda_t *this) {
  buf_element_t *buf;
  int            pos, len;
  
  buf = this->input->read_block(this->input, this->video_fifo, this->blocksize);
  
  pos = this->input->get_current_pos(this->input);
  len = this->input->get_length(this->input);
  
  buf->pts             = 0;
  buf->input_pos       = pos;
  buf->input_time      = buf->input_pos / this->blocksize;
  buf->type            = BUF_CONTROL_NOP; /* Fake */
  
  this->video_fifo->put(this->video_fifo, buf);
  
  return ((pos < len));
}

/*
 *
 */
static int demux_cda_send_chunk (demux_plugin_t *this_gen) {
  demux_cda_t    *this = (demux_cda_t *) this_gen;

  if (!demux_cda_next(this))
    this->status = DEMUX_FINISHED;

  return this->status;
}

/*
 *
 */
static int demux_cda_seek (demux_plugin_t *this_gen,
			     off_t start_pos, int start_time) {
  demux_cda_t    *this = (demux_cda_t *) this_gen;

  this->start      = start_pos;
  
  this->blocksize  = this->input->get_blocksize(this->input);

  /*
   * now start demuxing
   */
  this->input->seek(this->input, this->start, SEEK_SET);

  this->status = DEMUX_OK;
  
  return this->status;
}

/*
 *
 */
static void demux_cda_send_headers(demux_plugin_t *this_gen) {

  demux_cda_t *this = (demux_cda_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;
  
  xine_demux_control_start(this->stream);

  /* hardwired stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = 2;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] = 44100;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] = 16;
}

/*
 *
 */
static void demux_cda_dispose (demux_plugin_t *this) {
  free (this);
}

/*
 *
 */
static int demux_cda_get_status (demux_plugin_t *this_gen) {
  demux_cda_t *this = (demux_cda_t *) this_gen;
  
  return this->status;
}

/*
 *
 */
static int demux_cda_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static uint32_t demux_cda_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_cda_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_cda_t    *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_cda.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_cda_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_cda_send_headers;
  this->demux_plugin.send_chunk        = demux_cda_send_chunk;
  this->demux_plugin.seek              = demux_cda_seek;
  this->demux_plugin.dispose           = demux_cda_dispose;
  this->demux_plugin.get_status        = demux_cda_get_status;
  this->demux_plugin.get_stream_length = demux_cda_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_cda_get_capabilities;
  this->demux_plugin.get_optional_data = demux_cda_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:

    /* for CD audio, there is no content to validate */
    free (this);
    return NULL;

  break;

  case METHOD_BY_EXTENSION: {
    char *media;
    char *MRL = input->get_mrl(input);
    
    media = strstr(MRL, ":/");
    if (media) {
      if(strncasecmp(MRL, "cda", 3) != 0) {
        free (this);
        return NULL;
      }
    } else {
      free (this);
      return NULL;
    }
  }
  break;

  case METHOD_EXPLICIT:
  break;

  default:
    free (this);
    return NULL;
  }

  strncpy (this->last_mrl, input->get_mrl (input), 1024);

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "CD audio demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "CDA";
}

static char *get_extensions (demux_class_t *this_gen) {
  return NULL;
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "audio/cda: CD Audio";
}

static void class_dispose (demux_class_t *this_gen) {

  demux_cda_class_t *this = (demux_cda_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_cda_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_cda_class_t));
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

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 18, "cda", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
