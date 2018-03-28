/*
 * Copyright (C) 2003-2018 the xine project
 * Copyright (C) 2003 Jeroen Asselman <j.asselman@itsec-ps.nl>
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
 */

/*
 * dummy demultiplexer for raw yuv frames (delivered by v4l)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_yuv_frames"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "group_video.h"

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/demux.h>

#define WRAP_THRESHOLD       20000

typedef struct demux_yuv_frames_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;
  input_plugin_t       *input;
  int                   status;

  int                   seek_flag;
  int64_t               last_pts;
} demux_yuv_frames_t ;


static int demux_yuv_frames_get_status (demux_plugin_t *this_gen) {
  demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen;

  return this->status;
}

static int switch_buf(demux_yuv_frames_t *this , buf_element_t *buf){
  if (!buf)
    return 0;

  if (this->seek_flag) {
    this->seek_flag = 0;
    _x_demux_control_newpts(this->stream, buf->pts, BUF_FLAG_SEEK);
  } else if (llabs(this->last_pts - buf->pts) > WRAP_THRESHOLD) {
    _x_demux_control_newpts(this->stream, buf->pts, 0);
  }

  this->last_pts = buf->pts;

  switch (buf->type) {
    case BUF_VIDEO_I420:
    case BUF_VIDEO_YUY2:
      this->video_fifo->put(this->video_fifo, buf);
      return 1;	/* 1, we still should read audio */
    case BUF_AUDIO_LPCM_LE:
      if (!_x_stream_info_get(this->stream, XINE_STREAM_INFO_HAS_VIDEO))
        _x_demux_control_newpts(this->stream, buf->pts, 0);
      this->audio_fifo->put(this->audio_fifo, buf);
      break;
    default:
      lprintf ("dhelp, unknown buffer type %08x\n", buf->type);
      buf->free_buffer(buf);
  }

  return 0;
}

static int demux_yuv_frames_send_chunk (demux_plugin_t *this_gen){
  demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen;
  buf_element_t      *buf;

  do {
    if ( _x_stream_info_get(this->stream, XINE_STREAM_INFO_HAS_VIDEO) )
      buf = this->input->read_block (this->input, this->video_fifo, 0);
    else
      buf = this->input->read_block (this->input, this->audio_fifo, 0);
  } while (switch_buf(this, buf));

  return this->status;
}

static void demux_yuv_frames_send_headers (demux_plugin_t *this_gen){
  demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen;
  buf_element_t      *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  _x_demux_control_start(this->stream);

  if(_x_stream_info_get(this->stream, XINE_STREAM_INFO_HAS_AUDIO)) {
    buf = this->input->read_block(this->input, this->audio_fifo, 0);

    if (buf)
      this->audio_fifo->put(this->audio_fifo, buf);
    else
      this->status = DEMUX_FINISHED;
  }

  if(_x_stream_info_get(this->stream, XINE_STREAM_INFO_HAS_VIDEO)) {
    buf = this->input->read_block(this->input, this->video_fifo, 0);

    if (buf)
      this->video_fifo->put(this->video_fifo, buf);
    else
      this->status = DEMUX_FINISHED;
  }

  this->status = DEMUX_OK;
}

static int demux_yuv_frames_seek (demux_plugin_t *this_gen,
			    off_t start_pos, int start_time, int playing) {
  demux_yuv_frames_t *this = (demux_yuv_frames_t *) this_gen;

  this->seek_flag = 1;
  this->last_pts = 0;
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

static demux_plugin_t *open_plugin (demux_class_t *class_gen,
				    xine_stream_t *stream,
				    input_plugin_t *input) {

  demux_yuv_frames_t *this;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
    return NULL;
    break;

  case METHOD_BY_MRL: {
    const char *const mrl = input->get_mrl (input);

    if (strncmp (mrl, "v4l:/", 5))
      return NULL;

  }
  break;

  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }
  lprintf ("input accepted.\n");

  /*
   * if we reach this point, the input has been accepted.
   */

  this         = calloc(1, sizeof(demux_yuv_frames_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_yuv_frames_send_headers;
  this->demux_plugin.send_chunk        = demux_yuv_frames_send_chunk;
  this->demux_plugin.seek              = demux_yuv_frames_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_yuv_frames_get_status;
  this->demux_plugin.get_stream_length = demux_yuv_frames_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_yuv_frames_get_capabilities;
  this->demux_plugin.get_optional_data = demux_yuv_frames_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  return &this->demux_plugin;
}



/*
 * demuxer class
 */
void *demux_yuv_frames_init_class (xine_t *xine, const void *data) {
  demux_class_t *this;

  this = calloc(1, sizeof(demux_class_t));
  if (!this)
    return NULL;

  this->open_plugin     = open_plugin;
  this->description     = N_("YUV frames dummy demux plugin");
  this->identifier      = "YUV_FRAMES";
  this->mimetypes       = NULL;
  this->extensions      = NULL;
  this->dispose         = default_demux_class_dispose;

  return this;
}

/*
 * vim:sw=3:sts=3:
 */


