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
 * $Id: demux_elem.c,v 1.65 2002/12/20 22:57:48 guenter Exp $
 *
 * demultiplexer for elementary mpeg streams
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "demux.h"

/*
#define LOG
*/

#define NUM_PREVIEW_BUFFERS 50

typedef struct {  

  demux_plugin_t      demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  blocksize;
  int                  status;
  
  uint8_t              scratch[4096];

  char                 last_mrl[1024];
} demux_mpeg_elem_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_mpeg_elem_class_t;

static int demux_mpeg_elem_next (demux_mpeg_elem_t *this, int preview_mode) {
  buf_element_t *buf;
  int n;

#ifdef LOG
  printf ("demux_elem: next piece\n");
#endif

  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  n = this->input->read (this->input, buf->mem, this->blocksize);

  if (n<=0) {
    buf->free_buffer (buf);
    this->status = DEMUX_FINISHED;
    return 0;
  }

  buf->size = n;

  buf->pts             = 0;
  buf->input_pos       = this->input->get_current_pos(this->input);
  buf->type            = BUF_VIDEO_MPEG;

  if (preview_mode)
    buf->decoder_flags = BUF_FLAG_PREVIEW;

  this->video_fifo->put(this->video_fifo, buf);
  
  return buf->size>0;
}

static int demux_mpeg_elem_send_chunk (demux_plugin_t *this_gen) {

  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  if (!demux_mpeg_elem_next(this, 0))
    this->status = DEMUX_FINISHED;
  return this->status;
}

static int demux_mpeg_elem_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  return this->status;
}


static void demux_mpeg_elem_send_headers (demux_plugin_t *this_gen) {

  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->blocksize = this->input->get_blocksize(this->input);
  if (!this->blocksize)
    this->blocksize = 2048;
  
  xine_demux_control_start(this->stream);
  
  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    int num_buffers = NUM_PREVIEW_BUFFERS;
    
    this->input->seek (this->input, 0, SEEK_SET);
    
    this->status = DEMUX_OK ;
    while ((num_buffers > 0) && (this->status == DEMUX_OK)) {
      demux_mpeg_elem_next(this, 1);
      num_buffers--;
    }
  }

  this->status = DEMUX_OK;

  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 0;
}

static int demux_mpeg_elem_seek (demux_plugin_t *this_gen,
				  off_t start_pos, int start_time) {

  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;
  
  this->status = DEMUX_OK;

  if (this->stream->demux_thread_running) 
    xine_demux_flush_engine(this->stream);
  
  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
  
    /* FIXME: implement time seek */

    this->input->seek (this->input, start_pos, SEEK_SET);

#ifdef LOG
    printf ("demux_elem: seeking to %d\n", start_pos);
#endif
  }
  
  /*
   * now start demuxing
   */
  this->status = DEMUX_OK;

  return this->status;
}

static void demux_mpeg_elem_dispose (demux_plugin_t *this) {

  free (this);
}

static int demux_mpeg_elem_get_stream_length(demux_plugin_t *this_gen) {
  return 0 ; /*FIXME: implement */
}

static uint32_t demux_mpeg_elem_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mpeg_elem_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_mpeg_elem_t *this;

  this         = xine_xmalloc (sizeof (demux_mpeg_elem_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_mpeg_elem_send_headers;
  this->demux_plugin.send_chunk        = demux_mpeg_elem_send_chunk;
  this->demux_plugin.seek              = demux_mpeg_elem_seek;
  this->demux_plugin.dispose           = demux_mpeg_elem_dispose;
  this->demux_plugin.get_status        = demux_mpeg_elem_get_status;
  this->demux_plugin.get_stream_length = demux_mpeg_elem_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_mpeg_elem_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mpeg_elem_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    int got_sample;
    int bs = 4;
    
    if(!input) {
      free (this);
      return NULL;
    }

    got_sample = 0;
  
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      input->seek(input, 0, SEEK_SET);

      bs = input->get_blocksize(input);

      if (bs<4)
	bs = 4;

      if (input->read(input, this->scratch, bs) == bs) 
	got_sample = 1;
    } else if ((input->get_capabilities(input) & INPUT_CAP_PREVIEW) != 0) {
      input->get_optional_data (input, this->scratch, INPUT_OPTIONAL_DATA_PREVIEW);
      got_sample = 1;
    }

    if (!got_sample) {
      free (this);
      return NULL;
    }

#ifdef LOG	
    printf ("demux_elem: %02x %02x %02x %02x (bs=%d)\n",
	    this->scratch[0], this->scratch[1], 
	    this->scratch[2], this->scratch[3], bs);
#endif
	
    if (this->scratch[0] || this->scratch[1] 
	|| (this->scratch[2] != 0x01) || (this->scratch[3] != 0xb3)) {
      free (this);
      return NULL;
    }
	
    this->input = input;
    
#ifdef LOG	
    printf ("demux_elem: input accepted.\n");
#endif

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

    if (strncasecmp (ending, ".mpv", 4)) {
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
  return "Elementary MPEG stream demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "MPEG_ELEM";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "mpv";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_mpeg_elem_class_t *this = (demux_mpeg_elem_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_mpeg_elem_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_mpeg_elem_class_t));
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
  { PLUGIN_DEMUX, 18, "elem", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
