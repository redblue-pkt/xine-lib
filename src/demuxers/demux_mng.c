/*
 * Copyright (C) 2000-2002 the xine project
 *
 * This file is part of xine, a unix video player.
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
 * $Id: demux_mng.c,v 1.4 2003/01/04 14:48:12 miguelfreitas Exp $
 *
 * demux_mng.c, Demuxer plugin for Multiple-image Network Graphics format
 *
 * written and currently maintained by Robin Kay <komadori@myrealbox.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <libmng.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

typedef struct {
  demux_plugin_t demux_plugin;
  xine_stream_t *stream;
  config_values_t *config;
  fifo_buffer_t *video_fifo;
  input_plugin_t *input;

  int thread_running;
  int status;

  mng_handle mngh;
  xine_bmiheader bih;
  uint8_t *image;
  int started, tick_count, timer_count;
  
  char last_mrl[1024];
} demux_mng_t;

typedef struct {
  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_mng_class_t;

static mng_ptr mymng_alloc(mng_uint32 size)
{
  return (mng_ptr)calloc(1, size);
}

static void mymng_free(mng_ptr p, mng_uint32 size)
{
  free(p);
}

mng_bool mymng_open_stream(mng_handle mngh)
{
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  if (this->input->get_current_pos(this->input) != 0) {
    if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) == 0) {
      return MNG_FALSE;
    }
    this->input->seek(this->input, 0, SEEK_SET);
  }

  return MNG_TRUE;
}

mng_bool mymng_close_stream(mng_handle mngh)
{
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  this->status = DEMUX_FINISHED;

  return MNG_TRUE;
}

mng_bool mymng_read_stream(mng_handle mngh, mng_ptr buffer, mng_uint32 size, mng_uint32 *bytesread)
{
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  *bytesread = this->input->read(this->input, buffer, size);

  return MNG_TRUE;
}

mng_bool mymng_process_header(mng_handle mngh, mng_uint32 width, mng_uint32 height)
{
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  this->bih.biWidth = width;
  this->bih.biHeight = height;

  this->image = malloc(width * height * 3);

  mng_set_canvasstyle(mngh, MNG_CANVAS_RGB8);

  return MNG_TRUE;
}

mng_uint32 mymng_get_tick_count(mng_handle mngh)
{
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  return this->tick_count;
}

mng_bool mymng_set_timer(mng_handle mngh, mng_uint32 msecs)
{
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  this->timer_count = msecs;

  return MNG_TRUE;
}

mng_ptr mymng_get_canvas_line(mng_handle mngh, mng_uint32 line)
{
  demux_mng_t *this = (demux_mng_t*)mng_get_userdata(mngh);

  return this->image + line * this->bih.biWidth * 3;
}

mng_bool mymng_refresh(mng_handle mngh, mng_uint32 x, mng_uint32 y, mng_uint32 w, mng_uint32 h)
{
  return MNG_TRUE;
}

/*
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 * All the following functions are defined by the xine demuxer API
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 */

static int demux_mng_send_chunk(demux_mng_t *this)
{
  int size = this->bih.biWidth * this->bih.biHeight * 3;
  uint8_t *image_ptr = this->image;

  int err = mng_display_resume(this->mngh);
  if ((err != MNG_NOERROR) && (err != MNG_NEEDTIMERWAIT)) {
    fprintf(stderr, "demux_mng: mng_display_resume returned an error (%d)\n", err);
    this->status = DEMUX_FINISHED;
  }

  while (size > 0) {
    buf_element_t *buf;

    buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
    buf->type = BUF_VIDEO_RGB;
    buf->decoder_flags = BUF_FLAG_FRAMERATE;
    buf->decoder_info[0] = 90 * this->timer_count;
    buf->extra_info->input_pos = this->input->get_current_pos(this->input);
    buf->extra_info->input_time = this->tick_count / 1000;
    buf->pts = 90 * this->tick_count;

    if (size > buf->max_size) {
      buf->size = buf->max_size;
    }
    else {
      buf->size = size;
    }
    size -= buf->size;

    memcpy(buf->content, image_ptr, buf->size);
    image_ptr += buf->size;

    if (size == 0) {
      buf->decoder_flags |= BUF_FLAG_FRAME_END;
    }

    this->video_fifo->put(this->video_fifo, buf);
  }

  this->tick_count += this->timer_count;
  this->timer_count = 0;

  return this->status;
}

static void demux_mng_send_headers(demux_mng_t *this)
{
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = this->bih.biWidth;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->bih.biHeight;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoder */
  this->bih.biBitCount = 24;
  buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
  buf->type = BUF_VIDEO_RGB;
  buf->size = sizeof(xine_bmiheader);
  memcpy(buf->content, &this->bih, sizeof(xine_bmiheader));
  buf->decoder_flags = BUF_FLAG_HEADER;
  this->video_fifo->put(this->video_fifo, buf);
}

static int demux_mng_seek(demux_mng_t *this, off_t start_pos, int start_time)
{
  return this->status;
}

static void demux_mng_dispose(demux_mng_t *this)
{
  mng_cleanup(&this->mngh);

  if (this->image) {
    free(this->image);
  }

  free(this);
}

static int demux_mng_get_status(demux_mng_t *this)
{
  return this->status;
}

static int demux_mng_get_stream_length(demux_mng_t *this)
{
  return 0;
}

static uint32_t demux_mng_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mng_get_optional_data(demux_plugin_t *this_gen, void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t* open_plugin(demux_class_t *class_gen, xine_stream_t *stream, input_plugin_t *input_gen)
{
  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_mng_t    *this;

  this         = xine_xmalloc (sizeof (demux_mng_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = (void*)demux_mng_send_headers;
  this->demux_plugin.send_chunk        = (void*)demux_mng_send_chunk;
  this->demux_plugin.seek              = (void*)demux_mng_seek;
  this->demux_plugin.dispose           = (void*)demux_mng_dispose;
  this->demux_plugin.get_status        = (void*)demux_mng_get_status;
  this->demux_plugin.get_stream_length = (void*)demux_mng_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_mng_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mng_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {
    case METHOD_BY_CONTENT:
    case METHOD_EXPLICIT:
      if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) == 0) {
        free(this);
        return NULL;
      }
    break;

    case METHOD_BY_EXTENSION: {
      char *ending, *mrl;

      mrl = input->get_mrl(input);

      ending = strrchr(mrl, '.');
      if (!ending) {
        free(this);
        return NULL;
      }

      if (strncasecmp(ending, ".mng", 4) && strncasecmp(ending, ".png", 4)) {
        free(this);
        return NULL;
      }
    }
    break;

    default:
      free(this);
      return NULL;
    break;
  }

  if ((this->mngh = mng_initialize(this, mymng_alloc, mymng_free, MNG_NULL)) == MNG_NULL) {
    free(this);
    return NULL;
  }

  if (mng_setcb_openstream(this->mngh, mymng_open_stream) ||
      mng_setcb_closestream(this->mngh, mymng_close_stream) ||
      mng_setcb_readdata(this->mngh, mymng_read_stream) ||
      mng_setcb_processheader(this->mngh, mymng_process_header) ||
      mng_setcb_gettickcount(this->mngh, mymng_get_tick_count) ||
      mng_setcb_settimer(this->mngh, mymng_set_timer) ||
      mng_setcb_getcanvasline(this->mngh, mymng_get_canvas_line) ||
      mng_setcb_refresh(this->mngh, mymng_refresh)) {
    mng_cleanup(&this->mngh);
    free(this);
    return NULL;
  }

  {
    int err = mng_readdisplay(this->mngh);
    if ((err != MNG_NOERROR) && (err != MNG_NEEDTIMERWAIT)) {
      mng_cleanup(&this->mngh);
      free(this);
      return NULL;
    }
  }

  strncpy (this->last_mrl, input->get_mrl(input), 1024);

  return &this->demux_plugin;
}

static char *get_description(demux_class_t *this_gen)
{
  return "Multiple-image Network Graphics demux plugin";
}

static char *get_identifier(demux_class_t *this_gen)
{
  return "MNG";
}

static char *get_extensions(demux_class_t *this_gen)
{
  return "png mng";
}

static char *get_mimetypes(demux_class_t *this_gen)
{
  return "image/png: png: PNG image"
         "image/x-png: png: PNG image"
         "video/mng: mng: MNG animation"
         "video/x-mng: mng: MNG animation";
}

static void class_dispose(demux_class_t *this)
{
  free (this);
}

static void *init_plugin(xine_t *xine, void *data)
{
  demux_mng_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_mng_class_t));
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

plugin_info_t xine_plugin_info[] = {
  { PLUGIN_DEMUX, 20, "mng", XINE_VERSION_CODE, NULL, (void*)init_plugin},
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
