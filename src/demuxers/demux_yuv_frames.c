/* 
 * Copyright (C) 2003 the xine project
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
 * $Id: demux_yuv_frames.c,v 1.3 2003/01/26 15:56:21 tmmm Exp $
 *
 * dummy demultiplexer for raw yuv frames (delivered by v4l)
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

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

/*
#define LOG
*/

typedef struct demux_yuv_frames_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  
  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  int                   status;
  
} demux_yuv_frames_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_yuv_frames_class_t;


static int demux_yuv_frames_get_status (demux_plugin_t *this_gen) {
  demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen;

  return this->status;
}

static int demux_yuv_frames_send_chunk (demux_plugin_t *this_gen) {

  demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen;
  buf_element_t      *buf;

  buf = this->input->read_block (this->input, NULL, 0);

  if (!buf)
    this->status = DEMUX_FINISHED;
  else {

    switch (buf->type) {
    case BUF_VIDEO_YUV_FRAMES:
      this->video_fifo->put (this->video_fifo, buf);
      break;
    default:
      printf ("demux_yuv_frames: help, unknown buffer type %08x\n",
	      buf->type);

      buf->free_buffer (buf);
    }
  }

  return this->status;
}

static void demux_yuv_frames_send_headers (demux_plugin_t *this_gen) {

  demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
}

static int demux_yuv_frames_seek (demux_plugin_t *this_gen,
			    off_t start_pos, int start_time) {

  demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen; 
  
  return this->status;
}

static int demux_yuv_frames_get_stream_length (demux_plugin_t *this_gen) {

  /* demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen;  */

  return 0;
}

static uint32_t demux_yuv_frames_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_yuv_frames_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static void demux_yuv_frames_dispose (demux_plugin_t *this_gen) {

  demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen;  

  free (this);
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, 
				    xine_stream_t *stream, 
				    input_plugin_t *input) {
  
  demux_yuv_frames_t *this;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
    return NULL;
    break;

  case METHOD_BY_EXTENSION: {

    char *mrl;
    
    mrl = input->get_mrl (input);

    if (strncmp (mrl, "v4l:/", 5))
      return NULL;

  }
  break;

  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }

  printf ("demux_yuv_frames: input accepted.\n");

  /*
   * if we reach this point, the input has been accepted.
   */

  this         = xine_xmalloc (sizeof (demux_yuv_frames_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_yuv_frames_send_headers;
  this->demux_plugin.send_chunk        = demux_yuv_frames_send_chunk;
  this->demux_plugin.seek              = demux_yuv_frames_seek;
  this->demux_plugin.dispose           = demux_yuv_frames_dispose;
  this->demux_plugin.get_status        = demux_yuv_frames_get_status;
  this->demux_plugin.get_stream_length = demux_yuv_frames_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_yuv_frames_get_capabilities;
  this->demux_plugin.get_optional_data = demux_yuv_frames_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;
  
  this->status = DEMUX_FINISHED;
  
  return &this->demux_plugin;
}



/*
 * ogg demuxer class
 */

static char *get_description (demux_class_t *this_gen) {
  return "YUV frames dummy demux plugin";
}
 
static char *get_identifier (demux_class_t *this_gen) {
  return "YUV_FRAMES";
}

static char *get_extensions (demux_class_t *this_gen) {
  return NULL;
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_yuv_frames_class_t *this = (demux_yuv_frames_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {
  
  demux_yuv_frames_class_t     *this;
  
  this         = xine_xmalloc (sizeof (demux_yuv_frames_class_t));
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
  { PLUGIN_DEMUX, 20, "ogg", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
