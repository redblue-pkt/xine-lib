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
 * STR File Demuxer by Mike Melanson (melanson@pcisys.net)
 * This demuxer handles either raw STR files (which are just a concatenation
 * of raw compact disc sectors) or STR files with RIFF headers.
 *
 * $Id: demux_str.c,v 1.3 2003/01/10 11:57:18 miguelfreitas Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "demux.h"
#include "bswap.h"

/* 68 bytes is adequate for empirically determining if a file conforms */
#define STR_CHECK_BYTES 0x68

#define CD_RAW_SECTOR_SIZE 2352

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch3) | \
        ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | \
        ( (long)(unsigned char)(ch0) << 24 ) )

#define RIFF_TAG FOURCC_TAG('R', 'I', 'F', 'F')
#define CDXA_TAG FOURCC_TAG('C', 'D', 'X', 'A')

/* this is a temporary measure; hopefully there is a more accurate method
 * for finding frame duration */
#define FRAME_DURATION 45000

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;
  int                  send_end_buffers;

  off_t                data_start;
  off_t                data_size;
  int                  status;

  xine_bmiheader       bih;
  int                  audio_samplerate;

  char                 last_mrl[1024];

} demux_str_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_str_class_t;


/* returns 1 if the STR file was opened successfully, 0 otherwise */
static int open_str_file(demux_str_t *this) {

  unsigned char check_bytes[STR_CHECK_BYTES];

  this->bih.biWidth = this->bih.biHeight = 0;

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, check_bytes, STR_CHECK_BYTES) !=
    STR_CHECK_BYTES)
    return 0;

  /* check for STR with a RIFF header */
  if ((BE_32(&check_bytes[0]) == RIFF_TAG) &&
      (BE_32(&check_bytes[8]) == CDXA_TAG))
    this->data_start = 0x2C;
  else
    this->data_start = 0;

  /* now that we have the theoretical start of the first sector, check
   * if it really is a raw CD sector; first step: check for 12-byte 
   * sync marker */
  if ((BE_32(&check_bytes[this->data_start + 0]) != 0x00FFFFFF) ||
      (BE_32(&check_bytes[this->data_start + 4]) != 0xFFFFFFFF) ||
      (BE_32(&check_bytes[this->data_start + 8]) != 0xFFFFFF00))
    return 0;

  /* the 32 bits starting at 0x10 and at 0x14 should be the same */
  if (BE_32(&check_bytes[this->data_start + 0x10]) !=
      BE_32(&check_bytes[this->data_start + 0x14]))
    return 0;

  /* check if this an audio or video sector (bit 1 = video, bit 2 =
   * audio) */
  if ((check_bytes[this->data_start + 0x12] & 0x06) == 0x2) {

    /* video is suspected */

    this->bih.biWidth = LE_16(&check_bytes[this->data_start + 0x28]);
    this->bih.biHeight = LE_16(&check_bytes[this->data_start + 0x2A]);

    /* sanity check for the width and height */
    if ((this->bih.biWidth <= 0) ||
        (this->bih.biWidth > 320) ||
        (this->bih.biHeight <= 0) ||
        (this->bih.biHeight > 240))
      return 0;

  } else if ((check_bytes[this->data_start + 0x12] & 0x06) == 0x4) {

    /* audio is suspected */

  } else
    return 0;

  this->data_size = this->input->get_length(this->input) - this->data_start;

  return 1;
}

static int demux_str_send_chunk(demux_plugin_t *this_gen) {

  demux_str_t *this = (demux_str_t *) this_gen;
  unsigned char sector[CD_RAW_SECTOR_SIZE];
  unsigned int frame_number;
  buf_element_t *buf;

  if (this->input->read(this->input, sector, CD_RAW_SECTOR_SIZE) !=
    CD_RAW_SECTOR_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  if ((sector[0x12] & 0x06) == 0x2) {

    /* video chunk */
    frame_number = LE_32(&sector[0x20]);
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_VIDEO_PSX_MDEC;
    buf->extra_info->input_pos = 
      this->data_start + frame_number * CD_RAW_SECTOR_SIZE;
    buf->extra_info->input_length = this->data_size;
    buf->pts = frame_number * FRAME_DURATION;
    buf->extra_info->input_time = buf->pts / 90;

    /* constant size chunk */
    buf->size = 2048;
    memcpy(buf->content, &sector[0x38], 2048);

    /* entirely intracoded */
    buf->decoder_flags |= BUF_FLAG_KEYFRAME;

    /* if the current chunk is 1 less than the chunk count, this is the
     * last chunk of the frame */
    if ((LE_16(&sector[0x1C]) + 1) == LE_16(&sector[0x1E]))
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    this->video_fifo->put(this->video_fifo, buf);

  } else if ((sector[0x12] & 0x06) == 0x4) {
  }

  return this->status;
}

static void demux_str_send_headers(demux_plugin_t *this_gen) {

  demux_str_t *this = (demux_str_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]  = this->bih.biWidth;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->bih.biHeight;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to video decoder */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = FRAME_DURATION;  /* initial video_step */
  memcpy(buf->content, &this->bih, sizeof(this->bih));
  buf->size = sizeof(this->bih);
  buf->type = BUF_VIDEO_PSX_MDEC;
  this->video_fifo->put (this->video_fifo, buf);

  /* send init info to the audio decoder */
  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_XA_ADPCM;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = 37800;
    buf->decoder_info[2] = 16;
    buf->decoder_info[3] = 1;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_str_seek (demux_plugin_t *this_gen,
                               off_t start_pos, int start_time) {

  demux_str_t *this = (demux_str_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    /* send new pts */
    xine_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;


    /* reposition at the start of the sectors */
    this->input->seek(this->input, this->data_start, SEEK_SET);
  }

  return this->status;
}

static void demux_str_dispose (demux_plugin_t *this) {

  free(this);
}

static int demux_str_get_status (demux_plugin_t *this_gen) {
  demux_str_t *this = (demux_str_t *) this_gen;

  return this->status;
}

static int demux_str_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static uint32_t demux_str_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_str_get_optional_data(demux_plugin_t *this_gen,
                                           void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_str_t    *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_str.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_str_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_str_send_headers;
  this->demux_plugin.send_chunk        = demux_str_send_chunk;
  this->demux_plugin.seek              = demux_str_seek;
  this->demux_plugin.dispose           = demux_str_dispose;
  this->demux_plugin.get_status        = demux_str_get_status;
  this->demux_plugin.get_stream_length = demux_str_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_str_get_capabilities;
  this->demux_plugin.get_optional_data = demux_str_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_str_file(this)) {
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

    if (strncasecmp (ending, ".str", 4)) {
      free (this);
      return NULL;
    }

    if (!open_str_file(this)) {
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
  return "Sony Playstation STR file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "PSX STR";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "str";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_str_class_t *this = (demux_str_class_t *) this_gen;

  free (this);
}

void *demux_str_init_plugin (xine_t *xine, void *data) {

  demux_str_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_str_class_t));
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

#if 0
plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 20, "str", XINE_VERSION_CODE, NULL, demux_str_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif
