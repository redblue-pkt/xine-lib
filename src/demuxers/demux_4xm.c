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
 *
 * 4X Technologies (.4xm) File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information on the 4xm file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_4xm.c,v 1.1 2003/05/26 21:06:01 tmmm Exp $
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

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch0) | \
        ( (long)(unsigned char)(ch1) << 8 ) | \
        ( (long)(unsigned char)(ch2) << 16 ) | \
        ( (long)(unsigned char)(ch3) << 24 ) )

#define  RIFF_TAG FOURCC_TAG('R', 'I', 'F', 'F')
#define _4XMV_TAG FOURCC_TAG('4', 'X', 'M', 'V')
#define  LIST_TAG FOURCC_TAG('L', 'I', 'S', 'T')
#define  HEAD_TAG FOURCC_TAG('H', 'E', 'A', 'D')
#define  TRK__TAG FOURCC_TAG('T', 'R', 'K', '_')
#define  MOVI_TAG FOURCC_TAG('M', 'O', 'V', 'I')
#define  VTRK_TAG FOURCC_TAG('V', 'T', 'R', 'K')
#define  STRK_TAG FOURCC_TAG('S', 'T', 'R', 'K')
#define  name_TAG FOURCC_TAG('n', 'a', 'm', 'e')
#define  vtrk_TAG FOURCC_TAG('v', 't', 'r', 'k')
#define  strk_TAG FOURCC_TAG('s', 't', 'r', 'k')
#define  ifrm_TAG FOURCC_TAG('i', 'f', 'r', 'm')
#define  pfrm_TAG FOURCC_TAG('p', 'f', 'r', 'm')
#define  cfrm_TAG FOURCC_TAG('c', 'f', 'r', 'm')
#define  snd__TAG FOURCC_TAG('s', 'n', 'd', '_')

#define vtrk_SIZE 0x44
#define strk_SIZE 0x28

typedef struct AudioTrack {
  unsigned int audio_type;
  int sample_rate;
  int bits;
  int channels;
} audio_track_t;

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  off_t                data_start;
  off_t                data_size;
  int                  status;
  unsigned int         filesize;

  xine_bmiheader       bih;

  unsigned int         track_count;
  audio_track_t       *tracks;

  int64_t              pts;
  int                  last_chunk_was_audio;
  int                  last_audio_frame_count;

} demux_fourxm_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_fourxm_class_t;

/* Open a 4xm file
 * This function is called from the _open() function of this demuxer.
 * It returns 1 if 4xm file was opened successfully. */
static int open_fourxm_file(demux_fourxm_t *fourxm) {

  unsigned char preview[MAX_PREVIEW_SIZE];
  int header_size;
  unsigned char *header;
  int i;
  unsigned int fourcc_tag;
  unsigned int size;
  unsigned int current_track;

  if (fourxm->input->get_capabilities(fourxm->input) & INPUT_CAP_SEEKABLE) {
    /* reset the file */
    fourxm->input->seek(fourxm->input, 0, SEEK_SET);

    /* the file signature will be in the first 12 bytes */
    if (fourxm->input->read(fourxm->input, preview, 12) != 12) {
      return 0;
    }
  } else {
    fourxm->input->get_optional_data(fourxm->input, preview,
      INPUT_OPTIONAL_DATA_PREVIEW);
  }

  /* check for the signature tags */
  if ((LE_32(&preview[0]) !=  RIFF_TAG) ||
      (LE_32(&preview[8]) != _4XMV_TAG))
    return 0;

  /* file is qualified; if the input was not seekable, skip over the header
   * bytes in the stream */
  if ((fourxm->input->get_capabilities(fourxm->input) & INPUT_CAP_SEEKABLE) == 0) {
    fourxm->input->seek(fourxm->input, 12, SEEK_SET);
  }

  fourxm->filesize = LE_32(&preview[8]);
  if (fourxm->filesize == 0)
    fourxm->filesize = 0xFFFFFFFF;

  /* fetch the LIST-HEAD header */
  if (fourxm->input->read(fourxm->input, preview, 12) != 12)
    return 0;
  if ((LE_32(&preview[0]) != LIST_TAG) ||
      (LE_32(&preview[8]) != HEAD_TAG))
    return 0;

  /* read the whole header */
  header_size = LE_32(&preview[4]) - 4;
  header = xine_xmalloc(header_size);
  if (fourxm->input->read(fourxm->input, header, header_size) != header_size)
    return 0;

  fourxm->bih.biWidth = 0;
  fourxm->bih.biHeight = 0;
  fourxm->track_count = 0;
  fourxm->tracks = NULL;
  fourxm->pts = 0;
  fourxm->last_chunk_was_audio = 0;
  fourxm->last_audio_frame_count = 0;

  /* take the lazy approach and search for any and all vtrk and strk chunks */
  for (i = 0; i < header_size - 8; i++) {
    fourcc_tag = LE_32(&header[i]);
    size = LE_32(&header[i + 4]);

    if (fourcc_tag == vtrk_TAG) {
      /* check that there is enough data */
      if (size != vtrk_SIZE) {
        free(header);
        return 0;
      }
      fourxm->bih.biWidth = LE_32(&header[i + 36]);
      fourxm->bih.biHeight = LE_32(&header[i + 40]);
      i += 8 + size;
    } else if (fourcc_tag == strk_TAG) {
      /* check that there is enough data */
      if (size != strk_SIZE) {
        free(header);
        return 0;
      }
      current_track = LE_32(&header[i + 8]);
      if (current_track + 1 > fourxm->track_count) {
        fourxm->track_count = current_track + 1;
        fourxm->tracks = realloc(fourxm->tracks, 
          fourxm->track_count * sizeof(audio_track_t));
        if (!fourxm->tracks) {
          free(header);
          return 0;
        }
      }

      fourxm->tracks[current_track].channels = LE_32(&header[i + 36]);
      fourxm->tracks[current_track].sample_rate = LE_32(&header[i + 40]);
      fourxm->tracks[current_track].bits = LE_32(&header[i + 44]);
      fourxm->tracks[current_track].audio_type = 
        BUF_AUDIO_LPCM_LE + (current_track & 0x0000FFFF);
      i += 8 + size;
    }
  }

  free(header);



  return 1;
}

static int demux_fourxm_send_chunk(demux_plugin_t *this_gen) {

  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int fourcc_tag;
  unsigned int size;
  unsigned char header[8];
  int64_t pts_inc;
  unsigned int remaining_bytes;
  unsigned int current_track;

  /* read the next header */
  if (this->input->read(this->input, header, 8) != 8) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  fourcc_tag = LE_32(&header[0]);
  size = LE_32(&header[4]);

  switch (fourcc_tag) {

  case ifrm_TAG:
  case pfrm_TAG:
  case cfrm_TAG:
    if (this->last_chunk_was_audio) {
      this->last_chunk_was_audio = 0;
      pts_inc = this->last_audio_frame_count;
      pts_inc *= 90000;
      pts_inc /= this->tracks[0].sample_rate;
      this->pts += pts_inc;
    }

    /* send the 8-byte chunk header first */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_VIDEO_4XM;
    buf->extra_info->input_pos = this->input->get_current_pos(this->input);
    buf->extra_info->input_length = this->filesize;
    buf->extra_info->input_time = this->pts / 90;
    buf->pts = this->pts;
    buf->size = 8;
    memcpy(buf->content, header, 8);
    if (fourcc_tag == ifrm_TAG)
      buf->decoder_flags |= BUF_FLAG_KEYFRAME;
    this->video_fifo->put(this->video_fifo, buf);

    remaining_bytes = size;
    while (remaining_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_VIDEO_4XM;
      buf->extra_info->input_pos = this->input->get_current_pos(this->input);
      buf->extra_info->input_length = this->filesize;
      buf->extra_info->input_time = this->pts / 90;
      buf->pts = this->pts;

      if (remaining_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_bytes;
      remaining_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (!remaining_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
      if (fourcc_tag == ifrm_TAG)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;

      this->video_fifo->put(this->video_fifo, buf);
    }

    break;

  case snd__TAG:
    /* fetch the track number and audio chunk size */
    if (this->input->read(this->input, header, 8) != 8) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
    current_track = LE_32(&header[0]);
    size = LE_32(&header[4]);

    if (current_track >= this->track_count) {
      printf ("bad audio track number (%d >= %d)\n", current_track,
        this->track_count);
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    this->last_chunk_was_audio = 1;
    this->last_audio_frame_count = size / 
      ((this->tracks[current_track].bits / 8) * 
        this->tracks[current_track].channels);

    remaining_bytes = size;
    while (remaining_bytes) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->tracks[current_track].audio_type;
      buf->extra_info->input_pos = this->input->get_current_pos(this->input);
      buf->extra_info->input_length = this->filesize;
      buf->extra_info->input_time = this->pts / 90;
      buf->pts = this->pts;

      if (remaining_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_bytes;
      remaining_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (!remaining_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
        this->audio_fifo->put(this->audio_fifo, buf);
      }
    break;

  case LIST_TAG:
    /* skip LIST header */
    this->input->seek(this->input, 4, SEEK_CUR);
    break;

  }

  return this->status;
}

static void demux_fourxm_send_headers(demux_plugin_t *this_gen) {

  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] =
    (this->track_count > 0) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]  = this->bih.biWidth;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->bih.biHeight;
  if (this->track_count > 0) {
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
      this->tracks[0].channels;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
      this->tracks[0].sample_rate;
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
      this->tracks[0].bits;
  }

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = 3000;  /* initial video_step */
  memcpy(buf->content, &this->bih, sizeof(this->bih));
  buf->size = sizeof(this->bih);
  buf->type = BUF_VIDEO_4XM;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo && this->track_count > 0) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->tracks[0].audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->tracks[0].sample_rate;
    buf->decoder_info[2] = this->tracks[0].bits;
    buf->decoder_info[3] = this->tracks[0].channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_fourxm_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {
  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    /* send new pts */
    xine_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  }

  return this->status;
}

static void demux_fourxm_dispose (demux_plugin_t *this_gen) {
  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;

  free(this->tracks);
}

static int demux_fourxm_get_status (demux_plugin_t *this_gen) {
  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;

  return this->status;
}

static int demux_fourxm_get_stream_length (demux_plugin_t *this_gen) {

/*  demux_fourxm_t *this = (demux_fourxm_t *) this_gen;*/

  return 0;
}

static uint32_t demux_fourxm_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_fourxm_get_optional_data(demux_plugin_t *this_gen,
                                        void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_fourxm_t    *this;

  this         = xine_xmalloc (sizeof (demux_fourxm_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_fourxm_send_headers;
  this->demux_plugin.send_chunk        = demux_fourxm_send_chunk;
  this->demux_plugin.seek              = demux_fourxm_seek;
  this->demux_plugin.dispose           = demux_fourxm_dispose;
  this->demux_plugin.get_status        = demux_fourxm_get_status;
  this->demux_plugin.get_stream_length = demux_fourxm_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_fourxm_get_capabilities;
  this->demux_plugin.get_optional_data = demux_fourxm_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_fourxm_file(this)) {
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

    if (strncasecmp (ending, ".4xm", 4)) {
      free (this);
      return NULL;
    }

    if (!open_fourxm_file(this)) {
      free (this);
      return NULL;
    }

  }

  break;

  default:
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "4X Technologies (4xm) demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "4X Technologies";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "4xm";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_fourxm_class_t *this = (demux_fourxm_class_t *) this_gen;

  free (this);
}

void *demux_fourxm_init_plugin (xine_t *xine, void *data) {

  demux_fourxm_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_fourxm_class_t));
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

