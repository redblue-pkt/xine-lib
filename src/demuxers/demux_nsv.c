/*
 * Copyright (C) 2000-2003 the xine project
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
 */

/*
 * Nullsoft Video (NSV) file demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the NSV file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_nsv.c,v 1.15 2004/02/09 22:24:36 jstembridge Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_nsv"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "demux.h"
#include "bswap.h"
#include "buffer.h"

#define FOURCC_TAG BE_FOURCC
#define NSVf_TAG FOURCC_TAG('N', 'S', 'V', 'f')
#define NSVs_TAG FOURCC_TAG('N', 'S', 'V', 's')
#define NONE_TAG FOURCC_TAG('N', 'O', 'N', 'E')

#define BEEF 0xBEEF

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  off_t                data_size;

  unsigned int         fps;
  unsigned int         frame_pts_inc;

  unsigned int         video_type;
  int64_t              video_pts;
  unsigned int         audio_type;

  xine_bmiheader       bih;
} demux_nsv_t;

typedef struct {
  demux_class_t     demux_class;
} demux_nsv_class_t;

/* returns 1 if the NSV file was opened successfully, 0 otherwise */
static int open_nsv_file(demux_nsv_t *this) {
  unsigned char preview[28];
  unsigned int  video_fourcc;
  unsigned int  audio_fourcc;
  int           is_ultravox = 0;
  unsigned int  offset = 0;

  if (_x_demux_read_header(this->input, preview, 4) != 4)
    return 0;

  /* check for a 'NSV' signature */
  if ((preview[0] != 'N') ||
      (preview[1] != 'S') ||
      (preview[2] != 'V'))
  {
    if ((preview[0] != 'Z') ||
        (preview[1] != 0) ||
	(preview[2] != '9') ||
	(preview[3] != 1))
          return 0;

    is_ultravox = 1;
  }

  lprintf("NSV file detected\n");

  this->data_size = this->input->get_length(this->input);

  if (is_ultravox == 1) {
    int i;
    unsigned char buffer[512];

    if (_x_demux_read_header(this->input, buffer, 512) != 512)
      return 0;

    for (i = 0; i < 512 - 3; i++)
    {
      if ((buffer[i] == 'N') &&
          (buffer[i+1] == 'S') &&
	  (buffer[i+2] == 'V')) {
          /* Fill the preview buffer with our nice new NSV tag */
          memcpy (preview, buffer + i, 4);
          offset = i;
	  break;
      }
    }
  }

  /* file is qualified, proceed to load; jump over the first 4 bytes */
  this->input->seek(this->input, 4 + offset, SEEK_SET);

  if (BE_32(&preview[0]) == NSVf_TAG) {

    /* if there is a NSVs tag, load 24 more header bytes; load starting at
     * offset 4 in buffer to keep header data in line with document */
    if (this->input->read(this->input, &preview[4], 24) != 24)
      return 0;

    lprintf("found NSVf chunk\n");
    this->data_size = BE_32(&preview[8]);

    /* skip the rest of the data */
    this->input->seek(this->input, LE_32(&preview[4]) - 28, SEEK_CUR);

    /* get the first 4 bytes of the next chunk */
    if (this->input->read(this->input, preview, 4) != 4)
      return 0;
  } 

  /* make sure it is a 'NSVs' chunk */
  if (preview[3] != 's')
    return 0;

  /* fetch the remaining 12 header bytes of the first chunk to get the 
   * relevant information */
  if (this->input->read(this->input, &preview[4], 12) != 12)
    return 0;

  video_fourcc = ME_32(&preview[4]);
  if (BE_32(&preview[4]) == NONE_TAG)
    this->video_type = 0;
  else
    this->video_type = _x_fourcc_to_buf_video(video_fourcc);

  audio_fourcc = ME_32(&preview[8]);
  if (BE_32(&preview[8]) == NONE_TAG)
    this->audio_type = 0;
  else
    this->audio_type = _x_formattag_to_buf_audio(audio_fourcc);

  this->bih.biSize = sizeof(this->bih);
  this->bih.biWidth = LE_16(&preview[12]);
  this->bih.biHeight = LE_16(&preview[14]);
  this->bih.biCompression = video_fourcc;
  this->video_pts = 0;

  /* may not be true, but set it for the time being */
  this->frame_pts_inc = 3003;

  lprintf("video: %c%c%c%c, buffer type %08X, %dx%d\n",
    preview[4],
    preview[5],
    preview[6],
    preview[7],
    this->video_type,
    this->bih.biWidth,
    this->bih.biHeight);

  return 1;
}

static int demux_nsv_send_chunk(demux_plugin_t *this_gen) {
  demux_nsv_t *this = (demux_nsv_t *) this_gen;

  unsigned char header[8];
  buf_element_t *buf;
  off_t current_file_pos;
  int video_size;
  int audio_size;
  int chunk_id;

  current_file_pos = this->input->get_current_pos(this->input);

  lprintf("dispatching video & audio chunks...\n");

  /*
   * Read 7 bytes and expect the stream to be sitting at 1 of 3 places:
   *  1) start of a new 'NSVs' chunk; need to seek over the next 9 bytes,
   *     read 7 bytes, and move onto case 2
   *  2) at the length info at the start of a NSVs chunk; use the first
   *     as a FPS byte, read one more byte from the stream and use bytes
   *     3-7 as the length info
   *  3) at the BEEF marker indicating a new chunk within the NSVs chunk;
   *     use bytes 2-6 as the length info
   */

  if (this->input->read(this->input, header, 7) != 7) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  chunk_id = LE_16(&header[0]);
  switch (chunk_id) {

  /* situation #3 from the comment */
  case 0xBEEF:
    lprintf("situation #3\n");
    video_size = LE_32(&header[2]);
    video_size >>= 4;
    video_size &= 0xFFFFF;
    audio_size = LE_16(&header[5]);
    break;

  /* situation #1 from the comment, characters 'NS' (swapped) from the stream */
  case 0x534E:
    lprintf("situation #1\n");
    this->input->seek(this->input, 9, SEEK_CUR);
    if (this->input->read(this->input, header, 7) != 7) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    /* yes, fall through intentionally to situation #2, per comment */

  /* situation #2 from the comment */
  default:
    lprintf("situation #2\n");
    /* need 1 more byte */
    if (this->input->read(this->input, &header[7], 1) != 1) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    this->fps = header[0];
    if (this->fps & 0x80) {
      switch (this->fps & 0x7F) {
      case 1:
        /* 29.97 fps */
        this->frame_pts_inc = 3003;
        break;

      case 3:
        /* 23.976 fps */
        this->frame_pts_inc = 3753;
        break;

      case 5:
        /* 14.98 fps */
        this->frame_pts_inc = 6006;
        break;

      default:
        lprintf("unknown framerate: 0x%02X\n", this->fps);
        this->frame_pts_inc = 90000;
        break;
      }
    } else
      this->frame_pts_inc = 90000 / this->fps;

    video_size = LE_32(&header[3]);
    video_size >>= 4;
    video_size &= 0xFFFFF;
    audio_size = LE_16(&header[6]);

    break;

  }

  lprintf("sending video chunk with size 0x%X, audio chunk with size 0x%X\n",
    video_size, audio_size);

  while (video_size) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

    if (video_size > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = video_size;
    video_size -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) != buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    buf->type = this->video_type;
    buf->extra_info->input_pos = current_file_pos;
    buf->extra_info->input_time = this->video_pts / 90;
    buf->extra_info->input_length = this->data_size;
    buf->pts = this->video_pts;
    buf->decoder_flags |= BUF_FLAG_FRAMERATE;
    buf->decoder_info[0] = this->frame_pts_inc;

    if (!video_size)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;
    this->video_fifo->put(this->video_fifo, buf);
  }

  while (audio_size) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

    if (audio_size > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = audio_size;
    audio_size -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) != buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    buf->type = this->audio_type;
    buf->extra_info->input_pos = current_file_pos;
    buf->extra_info->input_time = this->video_pts / 90;
    buf->extra_info->input_length = this->data_size;
    buf->pts = this->video_pts;
    buf->decoder_flags |= BUF_FLAG_FRAMERATE;
    buf->decoder_info[0] = this->frame_pts_inc;

    if (!audio_size)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;
    this->audio_fifo->put(this->audio_fifo, buf);
  }

  this->video_pts += this->frame_pts_inc;
  return this->status;
}

static void demux_nsv_send_headers(demux_plugin_t *this_gen) {
  demux_nsv_t *this = (demux_nsv_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO,
    (this->video_type) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO,
    (this->audio_type) ? 1 : 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,
    this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT,
    this->bih.biHeight);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to the video decoder */
  if (this->video_fifo && this->video_type) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAMERATE|
                         BUF_FLAG_FRAME_END;
    buf->decoder_info[0] = this->frame_pts_inc;
    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = this->video_type;
    this->video_fifo->put (this->video_fifo, buf);
  }
}

static int demux_nsv_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_nsv_t *this = (demux_nsv_t *) this_gen;

  lprintf("starting demuxer\n");
  /* if thread is not running, initialize demuxer */
  if( !playing ) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static void demux_nsv_dispose (demux_plugin_t *this) {

  free(this);
}

static int demux_nsv_get_status (demux_plugin_t *this_gen) {
  demux_nsv_t *this = (demux_nsv_t *) this_gen;

  return this->status;
}

static int demux_nsv_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_nsv_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_nsv_get_optional_data(demux_plugin_t *this_gen,
                                       void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_nsv_t    *this;

  this         = xine_xmalloc (sizeof (demux_nsv_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_nsv_send_headers;
  this->demux_plugin.send_chunk        = demux_nsv_send_chunk;
  this->demux_plugin.seek              = demux_nsv_seek;
  this->demux_plugin.dispose           = demux_nsv_dispose;
  this->demux_plugin.get_status        = demux_nsv_get_status;
  this->demux_plugin.get_stream_length = demux_nsv_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_nsv_get_capabilities;
  this->demux_plugin.get_optional_data = demux_nsv_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_EXTENSION: {
    char *extensions, *mrl;

    mrl = input->get_mrl (input);
    extensions = class_gen->get_extensions (class_gen);

    if (!_x_demux_check_extension (mrl, extensions)) {
      free (this);
      return NULL;
    }
  }
  /* falling through is intended */

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_nsv_file(this)) {
      free (this);
      return NULL;
    }

  break;

  default:
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "Nullsoft Video demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "Nullsoft NSV";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "nsv";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {
  demux_nsv_class_t *this = (demux_nsv_class_t *) this_gen;

  free (this);
}

static void *demux_nsv_init_plugin (xine_t *xine, void *data) {
  demux_nsv_class_t     *this;

  this = xine_xmalloc (sizeof (demux_nsv_class_t));

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
  { PLUGIN_DEMUX, 24, "nsv", XINE_VERSION_CODE, NULL, demux_nsv_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
