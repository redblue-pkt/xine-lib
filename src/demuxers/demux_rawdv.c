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
 * $Id: demux_rawdv.c,v 1.1 2002/12/23 21:29:59 miguelfreitas Exp $
 *
 * demultiplexer for raw dv streams
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

#define NTSC_FRAME_SIZE 120000
#define PAL_FRAME_SIZE  144000

typedef struct {  

  demux_plugin_t      demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  frame_size;
  int                  bytes_left;
  
  uint32_t             cur_frame;
  uint32_t             duration;
  uint64_t             pts;
  
  int                  status;
  
} demux_raw_dv_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_raw_dv_class_t;


static int demux_raw_dv_next (demux_raw_dv_t *this) {
  buf_element_t *buf;
  int n;

  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->content = buf->mem;
    
  if( this->bytes_left <= buf->max_size ) {
    buf->size = this->bytes_left;
    buf->decoder_flags |= BUF_FLAG_FRAME_END;
  } else {
    buf->size = buf->max_size;
  }
  this->bytes_left -= buf->size;
  
  n = this->input->read (this->input, buf->content, buf->size);
  
  if (n != buf->size) {
    buf->free_buffer(buf);
    return 0;
  }

  /* TODO: duplicate data and send to audio fifo.
   * however we don't have dvaudio decoder yet.
   */
  
  buf->pts                    = this->pts;
  buf->extra_info->input_time = this->pts/90000;
  buf->extra_info->input_pos  = this->input->get_current_pos(this->input);
  buf->extra_info->frame_number  = this->cur_frame;
  buf->type                   = BUF_VIDEO_DV;
  
  this->video_fifo->put(this->video_fifo, buf);
  
  if (!this->bytes_left) {
    this->bytes_left = this->frame_size;
    this->pts += this->duration;
    this->cur_frame++;
  }
  
  return 1;
}

static int demux_raw_dv_send_chunk (demux_plugin_t *this_gen) {

  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;

  if (!demux_raw_dv_next(this))
    this->status = DEMUX_FINISHED;
  return this->status;
}

static int demux_raw_dv_get_status (demux_plugin_t *this_gen) {
  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;

  return this->status;
}


static void demux_raw_dv_send_headers (demux_plugin_t *this_gen) {

  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;
  buf_element_t *buf;
  xine_bmiheader *bih;
  unsigned char scratch[4];

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  xine_demux_control_start(this->stream);
  
  if ( !(this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE)) {
    printf("demux_rawdv: not seekable, can't handle!\n");
    return;
  }
  
  this->input->seek(this->input, 0, SEEK_SET);
  if( this->input->read (this->input, scratch, 4) != 4 )
    return;
  this->input->seek(this->input, 0, SEEK_SET);
  
  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->content = buf->mem;
  buf->type = BUF_VIDEO_DV;
  buf->decoder_flags |= BUF_FLAG_HEADER;
  
  bih = (xine_bmiheader *)buf->content;
  
  if( !(scratch[3] & 0x80) ) {
    /* NTSC */
    this->frame_size = NTSC_FRAME_SIZE; 
    this->duration = buf->decoder_info[1] = 3003;
    bih->biWidth = 720;
    bih->biHeight = 480;
  } else {
    /* PAL */
    this->frame_size = PAL_FRAME_SIZE; 
    this->duration = buf->decoder_info[1] = 3600;
    bih->biWidth = 720;
    bih->biHeight = 576;
  }
  bih->biSize = sizeof(xine_bmiheader);
  bih->biPlanes = 1;
  bih->biBitCount = 24;
  memcpy(&bih->biCompression,"dvsd",4);
  bih->biSizeImage = bih->biWidth*bih->biHeight;

  this->video_fifo->put(this->video_fifo, buf);
  
  this->pts = 0;
  this->cur_frame = 0;
  this->bytes_left = this->frame_size;
 
  this->status = DEMUX_OK;

  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 0;
}

static int demux_raw_dv_seek (demux_plugin_t *this_gen,
				  off_t start_pos, int start_time) {

  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;

  
  if( !start_pos && start_time ) {
    start_pos = (start_time * 90000 / this->duration) * this->frame_size;
  }
  
  start_pos = start_pos - (start_pos % this->frame_size);  
  this->input->seek(this->input, start_pos, SEEK_SET);

  this->cur_frame = start_pos / this->frame_size;
  this->pts = this->cur_frame * this->duration;
  this->bytes_left = this->frame_size;
  
  xine_demux_flush_engine (this->stream);

  xine_demux_control_newpts (this->stream, this->pts, BUF_FLAG_SEEK);
  
  return this->status;
}

static void demux_raw_dv_dispose (demux_plugin_t *this_gen) {
  demux_raw_dv_t *this = (demux_raw_dv_t *) this_gen;

  free (this);
}

static int demux_raw_dv_get_stream_length(demux_plugin_t *this_gen) {
  return 0 ; /*FIXME: implement */
}

static uint32_t demux_raw_dv_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_raw_dv_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_raw_dv_t *this;

  this         = xine_xmalloc (sizeof (demux_raw_dv_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_raw_dv_send_headers;
  this->demux_plugin.send_chunk        = demux_raw_dv_send_chunk;
  this->demux_plugin.seek              = demux_raw_dv_seek;
  this->demux_plugin.dispose           = demux_raw_dv_dispose;
  this->demux_plugin.get_status        = demux_raw_dv_get_status;
  this->demux_plugin.get_stream_length = demux_raw_dv_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_raw_dv_get_capabilities;
  this->demux_plugin.get_optional_data = demux_raw_dv_get_optional_data;
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

    if (strncasecmp (ending, ".dv", 3)) {
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

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "Raw DV Video stream";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "raw_dv";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "dv";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_raw_dv_class_t *this = (demux_raw_dv_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_raw_dv_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_raw_dv_class_t));
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
  { PLUGIN_DEMUX, 19, "rawdv", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
