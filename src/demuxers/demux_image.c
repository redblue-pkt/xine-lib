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
 * $Id: demux_image.c,v 1.2 2003/04/18 14:11:04 hadess Exp $
 *
 * image dummy demultiplexer
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

typedef struct demux_image_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  int                   status;
  
} demux_image_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;

} demux_image_class_t;


static int demux_image_get_status (demux_plugin_t *this_gen) {
  demux_image_t *this = (demux_image_t *) this_gen;

  return this->status;
}

static int demux_image_send_chunk (demux_plugin_t *this_gen) {
  demux_image_t *this = (demux_image_t *) this_gen;
  buf_element_t *buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

  buf->content = buf->mem;
  buf->type = BUF_VIDEO_IMAGE;

  buf->size = this->input->read (this->input, buf->mem, buf->max_size-1);

  if (buf->size <= 0) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
  } else {

#ifdef LOG
  printf("demux_image: got %i bytes\n", buf->size);
#endif

    this->video_fifo->put (this->video_fifo, buf);
    this->status = DEMUX_OK;
  }
  return this->status;
}

static void demux_image_send_headers (demux_plugin_t *this_gen) {

  demux_image_t *this = (demux_image_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;

  this->status = DEMUX_OK;

  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 0;
  
  this->input->seek (this->input, 0, SEEK_SET);
}

static int demux_image_seek (demux_plugin_t *this_gen,
			    off_t start_pos, int start_time) {

  demux_image_t *this = (demux_image_t *) this_gen; 
  
  return this->status;
}

static int demux_image_get_stream_length (demux_plugin_t *this_gen) {

  /* demux_image_t *this = (demux_image_t *) this_gen;  */

  return 0;
}

static uint32_t demux_image_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_image_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static void demux_image_dispose (demux_plugin_t *this_gen) {

  demux_image_t *this = (demux_image_t *) this_gen;  

#ifdef LOG
  printf("demux_image: closed\n");
#endif

  free (this);
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, 
				    xine_stream_t *stream, 
				    input_plugin_t *input) {
  
  demux_image_t *this;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
    return NULL;
    break;

  case METHOD_BY_EXTENSION: {

    char *extensions, *mrl;

    mrl = input->get_mrl (input);

    extensions = class_gen->get_extensions (class_gen);
    if (!xine_demux_check_extension (mrl, extensions)) {
      return NULL;
    }
  }
  break;

  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }
#ifdef LOG
  printf ("demux_image: input accepted.\n");
#endif
  /*
   * if we reach this point, the input has been accepted.
   */

  this         = xine_xmalloc (sizeof (demux_image_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_image_send_headers;
  this->demux_plugin.send_chunk        = demux_image_send_chunk;
  this->demux_plugin.seek              = demux_image_seek;
  this->demux_plugin.dispose           = demux_image_dispose;
  this->demux_plugin.get_status        = demux_image_get_status;
  this->demux_plugin.get_stream_length = demux_image_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_image_get_capabilities;
  this->demux_plugin.get_optional_data = demux_image_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;
  
  this->status = DEMUX_FINISHED;

#ifdef LOG
  printf("demux_image: opened\n");
#endif
  
  return &this->demux_plugin;
}

/*
 * image demuxer class
 */

static char *get_description (demux_class_t *this_gen) {
  return "image demux plugin";
}
 
static char *get_identifier (demux_class_t *this_gen) {
  return "imagedmx";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "png";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_image_class_t *this = (demux_image_class_t *) this_gen;

#ifdef LOG
  printf("demux_image: class closed\n");
#endif
  
  free (this);
}

static void *init_class (xine_t *xine, void *data) {
  
  demux_image_class_t     *this;
  
  this         = xine_xmalloc (sizeof (demux_image_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

#ifdef LOG
  printf("demux_image: class opened\n");
#endif

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 20, "image", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
