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
 * Nullsoft Video (NSV) file demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the NSV file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_nsv.c,v 1.1 2003/05/19 21:59:46 tmmm Exp $
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
#include "bswap.h"
#include "buffer.h"

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch3) | \
        ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | \
        ( (long)(unsigned char)(ch0) << 24 ) )

#define NSVf_TAG FOURCC_TAG('N', 'S', 'V', 'f')
#define NSVs_TAG FOURCC_TAG('N', 'S', 'V', 's')
#define NONE_TAG FOURCC_TAG('N', 'O', 'N', 'E')

#define BEEF 0xBEEF

/* debug support */
#define DEBUG_NSV 0

#if DEBUG_NSV
#define debug_nsv printf
#else
static inline void debug_nsv(const char *format, ...) { }
#endif

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  thread_running;

  off_t                data_start;
  off_t                data_size;
  int                  status;

  unsigned int         fps;
  unsigned int         frame_pts_inc;

  unsigned int         video_fourcc;
  unsigned int         video_type;
  int64_t              video_pts;

  unsigned int         audio_fourcc;
  unsigned int         audio_type;
  unsigned int         audio_bits;
  unsigned int         audio_channels;
  unsigned int         audio_sample_rate;
  unsigned int         audio_frame_count;

  xine_bmiheader       bih;
  xine_waveformatex    wave;

  char                 last_mrl[1024];

} demux_nsv_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_nsv_class_t;

/* returns 1 if the NSV file was opened successfully, 0 otherwise */
static int open_nsv_file(demux_nsv_t *this) {

  unsigned char preview[MAX_PREVIEW_SIZE];

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {
    this->input->seek(this->input, 0, SEEK_SET);
    if (this->input->read(this->input, preview, 4) != 4)
      return 0;
  } else {
    this->input->get_optional_data(this->input, preview,
      INPUT_OPTIONAL_DATA_PREVIEW);
  }

  /* check for a 'NSV' signature */
  if ((preview[0] != 'N') ||
      (preview[1] != 'S') ||
      (preview[2] != 'V'))
    return 0;

  debug_nsv("  demux_nsv: NSV file detected\n");

  /* file is qualified, proceed to load; jump over the first 4 bytes if
   * stream is non-seekable */
  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) == 0) {
    this->input->seek(this->input, 4, SEEK_SET);
  }

  if (BE_32(&preview[0]) == NSVf_TAG) {

    /* if there is a NSVs tag, load 24 more header bytes; load starting at
     * offset 4 in buffer to keep header data in line with document */
    if (this->input->read(this->input, &preview[4], 24) != 24)
      return 0;

    debug_nsv("  demux_nsv: found NSVf chunk\n");
    this->data_size = BE_32(&preview[8]);

    /* skip the rest of the data */
    this->input->seek(this->input, LE_32(&preview[4]) - 28, SEEK_CUR);

  } 

  this->data_size = this->input->get_length(this->input);

  /* fetch the 16 header bytes of the first chunk to get the relevant
   * information */
  if (this->input->read(this->input, preview, 16) != 16)
    return 0;

  /* make sure it is a 'NSVs' chunk */
  if (preview[3] != 's')
    return 0;

  this->video_fourcc = ME_32(&preview[4]);
  if (BE_32(&preview[4]) == NONE_TAG)
    this->video_type = 0;
  else
    this->video_type = fourcc_to_buf_video(this->video_fourcc);
  this->audio_fourcc = ME_32(&preview[8]);
  if (BE_32(&preview[8]) == NONE_TAG)
    this->audio_type = 0;
  else
    this->audio_type = formattag_to_buf_audio(this->audio_fourcc);

  this->bih.biSize = sizeof(this->bih);
  this->bih.biWidth = LE_16(&preview[12]);
  this->bih.biHeight = LE_16(&preview[14]);
  this->bih.biCompression = this->video_fourcc;
  this->video_pts = 0;

  /* may not be true, but set it for the time being */
  this->frame_pts_inc = 3003;

  debug_nsv("  video: %c%c%c%c, buffer type %08X, %dx%d\n",
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

  debug_nsv (" dispatching video & audio chunks...\n");

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
    debug_nsv ("   situation #3\n");
    video_size = LE_32(&header[2]);
    video_size >>= 4;
    video_size &= 0xFFFFF;
    audio_size = LE_16(&header[5]);
    break;

  /* situation #1 from the comment, characters 'NS' (swapped) from the stream */
  case 0x534E:
    debug_nsv ("   situation #1\n");
    this->input->seek(this->input, 9, SEEK_CUR);
    if (this->input->read(this->input, header, 7) != 7) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    /* yes, fall through intentionally to situation #2, per comment */

  /* situation #2 from the comment */
  default:
    debug_nsv ("   situation #2\n");
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
 
      case 5:
        /* 14.98 fps */
        this->frame_pts_inc = 6006;
        break;

      default:
        printf ("demux_nsv: unknown framerate: 0x%02X\n", this->fps);
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

  debug_nsv ("   sending video chunk with size 0x%X, audio chunk with size 0x%X\n",
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
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = (this->video_type) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = (this->audio_type) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = this->bih.biWidth;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->bih.biHeight;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to the video decoder */
  if (this->video_fifo && this->video_type) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->frame_pts_inc;
    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = this->video_type;
    this->video_fifo->put (this->video_fifo, buf);
  }

  /* send init info to the audio decoder */
  if (this->audio_fifo && this->audio_type) {

  }
}

static int demux_nsv_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  demux_nsv_t *this = (demux_nsv_t *) this_gen;

  debug_nsv("  demux_nsv: starting demuxer\n");
  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    /* send new pts */
    xine_demux_control_newpts(this->stream, 0, 0);

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
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
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
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_nsv_get_capabilities;
  this->demux_plugin.get_optional_data = demux_nsv_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_nsv_file(this)) {
      free (this);
      return NULL;
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

    if (strncasecmp (ending, ".nsv", 4)) {
      free (this);
      return NULL;
    }

    if (!open_nsv_file(this)) {
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

void *demux_nsv_init_plugin (xine_t *xine, void *data) {

  demux_nsv_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_nsv_class_t));
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
  { PLUGIN_DEMUX, 21, "nsv", XINE_VERSION_CODE, NULL, demux_nsv_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
