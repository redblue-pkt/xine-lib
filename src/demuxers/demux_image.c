/*
 * Copyright (C) 2003-2019 the xine project
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
 * image dummy demultiplexer
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_image"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include "bswap.h"
#include <xine/demux.h>

#define IMAGE_HEADER_LEN 4

typedef struct demux_image_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  fifo_buffer_t        *video_fifo;
  input_plugin_t       *input;
  int                   status;
  int                   buf_type;
  int                   bytes_left;
} demux_image_t ;


static uint32_t _probe (xine_t *xine, const uint8_t *header) {
  if (memcmp (header, "GIF", 3) == 0) { /* GIF */
    if (_x_decoder_available (xine, BUF_VIDEO_IMAGE))
      return BUF_VIDEO_IMAGE;
  } else if (memcmp (header, "BM", 2) == 0) { /* BMP */
    if (_x_decoder_available (xine, BUF_VIDEO_IMAGE))
      return BUF_VIDEO_IMAGE;
  } else if (memcmp (header, "\x89PNG", 4) == 0) { /* PNG */
    if (_x_decoder_available (xine, BUF_VIDEO_PNG))
      return BUF_VIDEO_PNG;
  } else if (memcmp (header, "\xff\xd8", 2) == 0) { /* JPEG */
    if (_x_decoder_available (xine, BUF_VIDEO_JPEG))
      return BUF_VIDEO_JPEG;
  }
  return 0;
}


static int demux_image_get_status (demux_plugin_t *this_gen) {
  demux_image_t *this = (demux_image_t *) this_gen;

  return this->status;
}

static int demux_image_next (demux_plugin_t *this_gen, int decoder_flags) {
  demux_image_t *this = (demux_image_t *) this_gen;
  buf_element_t *buf = this->video_fifo->buffer_pool_size_alloc (this->video_fifo, this->bytes_left);

  buf->content = buf->mem;
  buf->decoder_flags = decoder_flags;

  buf->size = this->input->read (this->input, (char *)buf->mem, buf->max_size);

  this->bytes_left -= buf->size;
  if (this->bytes_left < 0)
    this->bytes_left = 0;

  if (buf->size <= 0) {
    buf->size = 0;
    buf->decoder_flags |= BUF_FLAG_FRAME_END;
    this->status = DEMUX_FINISHED;
  } else {
    if (!this->buf_type) {
      this->buf_type = _probe (this->stream->xine, buf->content);
      if (!this->buf_type) {
        /* allow forcing any file to generic image decoders */
        this->buf_type = BUF_VIDEO_IMAGE;
      }
    }
    this->status = DEMUX_OK;
  }

  buf->type = this->buf_type;

  this->video_fifo->put (this->video_fifo, buf);

  return this->status;
}

static int demux_image_send_chunk (demux_plugin_t *this_gen) {
  return demux_image_next(this_gen, 0);
}

static void demux_image_send_headers (demux_plugin_t *this_gen) {
  demux_image_t *this = (demux_image_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;

  _x_demux_control_start(this->stream);

  if (this->input->seek (this->input, 0, SEEK_SET) != 0) {
    this->status = DEMUX_FINISHED;
    return;
  }
  this->bytes_left = this->input->get_length (this->input);
  if (this->bytes_left < 0)
    this->bytes_left = 0;

  /* we can send everything here. this makes image decoder a lot easier */
  while (demux_image_next(this_gen, BUF_FLAG_PREVIEW) == DEMUX_OK);

  this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
}

static int demux_image_seek (demux_plugin_t *this_gen,
			    off_t start_pos, int start_time, int playing) {

  demux_image_t *this = (demux_image_t *) this_gen;

  (void)start_pos;
  (void)start_time;
  (void)playing;
  /* delay finished event for presentation mode.
   * -1 => wait forever
   * 0  => do not wait
   * xx => wait xx/10 seconds
   */
  xine_set_param (this->stream, XINE_PARAM_DELAY_FINISHED_EVENT, -1);

  return this->status;
}

static int demux_image_get_stream_length (demux_plugin_t *this_gen) {
  /* demux_image_t *this = (demux_image_t *) this_gen;  */

  (void)this_gen;
  return 0;
}

static uint32_t demux_image_get_capabilities(demux_plugin_t *this_gen) {
  (void)this_gen;
  return DEMUX_CAP_NOCAP;
}

static int demux_image_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  (void)this_gen;
  (void)data;
  (void)data_type;
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen,
				    xine_stream_t *stream,
				    input_plugin_t *input) {

  demux_image_t *this;
  int buf_type = 0;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t header[IMAGE_HEADER_LEN];
    if (_x_demux_read_header(input, header, IMAGE_HEADER_LEN) != IMAGE_HEADER_LEN) {
      return NULL;
    }
    buf_type = _probe (stream->xine, header);
    if (buf_type)
      break;
    return NULL;
  }
  break;

  case METHOD_BY_MRL:
  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }

  lprintf ("input accepted.\n");
  /*
   * if we reach this point, the input has been accepted.
   */

  this = calloc(1, sizeof(demux_image_t));
  if (!this)
    return NULL;

  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_image_send_headers;
  this->demux_plugin.send_chunk        = demux_image_send_chunk;
  this->demux_plugin.seek              = demux_image_seek;
  this->demux_plugin.dispose           = default_demux_plugin_dispose;
  this->demux_plugin.get_status        = demux_image_get_status;
  this->demux_plugin.get_stream_length = demux_image_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_image_get_capabilities;
  this->demux_plugin.get_optional_data = demux_image_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;
  this->buf_type = buf_type;

  lprintf("opened\n");
  return &this->demux_plugin;
}

/*
 * image demuxer class
 */
static void *init_class (xine_t *xine, const void *data) {

  (void)xine;
  (void)data;

  static const demux_class_t demux_image_class = {
    .open_plugin     = open_plugin,
    .description     = N_("image demux plugin"),
    .identifier      = "imagedmx",
    .mimetypes       = NULL,
    /* NOTE: a leading, trailing, or double space would add the empty extension "" to the list. Avoid that.
     * FIXME: this frozen at build time. */
    .extensions      = ""
#if defined(HAVE_GDK_PIXBUF) || defined(HAVE_IMAGEMAGICK)
        "bmp gif jpg jpeg png"
#else
#  if defined(HAVE_LIBJPEG)
        "jpg jpeg"
#  endif
#  if defined(HAVE_LIBJPEG) && defined(HAVE_LIBPNG)
        " "
#  endif
#  if defined(HAVE_LIBPNG)
        "png"
#  endif
#endif
    ,
    .dispose         = NULL,
  };

  return (void *)&demux_image_class;
}

/*
 * exported plugin catalog entry
 */
static const demuxer_info_t demux_info_image = {
  .priority = 11,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "image", XINE_VERSION_CODE, &demux_info_image, init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};

