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
 * VQA File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the VQA file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * Quick technical note: VQA files begin with a header that includes a
 * frame index. This ought to be useful for seeking within a VQA file.
 * However, seeking is infeasible due to the audio encoding: Each audio 
 * block needs information from the previous audio block in order to be
 * decoded, thus making random seeking difficult.
 *
 * $Id: demux_vqa.c,v 1.21 2002/11/22 20:13:14 guenter Exp $
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
        ( (long)(unsigned char)(ch3) | \
        ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | \
        ( (long)(unsigned char)(ch0) << 24 ) )

#define FORM_TAG FOURCC_TAG('F', 'O', 'R', 'M')
#define WVQA_TAG FOURCC_TAG('W', 'V', 'Q', 'A')
#define VQHD_TAG FOURCC_TAG('V', 'Q', 'H', 'D')
#define FINF_TAG FOURCC_TAG('F', 'I', 'N', 'F')
#define SND0_TAG FOURCC_TAG('S', 'N', 'D', '0')
#define SND2_TAG FOURCC_TAG('S', 'N', 'D', '2')
#define VQFR_TAG FOURCC_TAG('V', 'Q', 'F', 'R')

#define VQA_HEADER_SIZE 0x2A
#define VQA_FRAMERATE 15
#define VQA_PTS_INC (90000 / VQA_FRAMERATE)
#define VQA_PREAMBLE_SIZE 8

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  off_t                data_start;
  off_t                filesize;
  int                  status;

  unsigned char        header[VQA_HEADER_SIZE];

  unsigned int         audio_sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;

  int64_t              video_pts;
  unsigned int         audio_frames;
  unsigned int         iteration;

  char                 last_mrl[1024];
} demux_vqa_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_vqa_class_t;

/* returns 1 if the VQA file was opened successfully, 0 otherwise */
static int open_vqa_file(demux_vqa_t *this) {

  unsigned char scratch[12];
  unsigned int chunk_size;

  /* get the actual filesize */
  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {
    this->filesize = this->input->get_length(this->input);
    this->input->seek(this->input, 0, SEEK_SET);
  } else
    this->filesize = 1;

  if (this->input->read(this->input, scratch, 12) != 12)
    return 0;

  /* check for the VQA signatures */
  if ((BE_32(&scratch[0]) != FORM_TAG) ||
      (BE_32(&scratch[8]) != WVQA_TAG))
    return 0;

  /* skip to the start of the VQA header */
  this->input->seek(this->input, 8, SEEK_CUR);

  /* load the VQA header */
  if (this->input->read(this->input, this->header, VQA_HEADER_SIZE)
    != VQA_HEADER_SIZE)
    return 0;

  this->audio_sample_rate = LE_16(&this->header[24]);
  this->audio_channels = this->header[26];

  /* skip the FINF chunk */
  if (this->input->read(this->input, scratch, VQA_PREAMBLE_SIZE) !=
    VQA_PREAMBLE_SIZE)
    return 0;
  chunk_size = BE_32(&scratch[4]);
  printf ("current pos @ %llX + ", this->input->get_current_pos(this->input));
  printf ("%X bytes seek forward = new pos @ %llX)\n", 
    chunk_size,
    this->input->seek(this->input, chunk_size, SEEK_CUR));

  this->video_pts = this->audio_frames = 0;
  this->iteration = 0;

  return 1;
}

static int demux_vqa_send_chunk(demux_plugin_t *this_gen) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned char preamble[VQA_PREAMBLE_SIZE];
  unsigned int chunk_size;
  off_t current_file_pos;
  int skip_byte;
  int64_t audio_pts;

  /* load and dispatch the audio portion of the frame */
  if (this->input->read(this->input, preamble, VQA_PREAMBLE_SIZE) !=
    VQA_PREAMBLE_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  current_file_pos = this->input->get_current_pos(this->input);
  chunk_size = BE_32(&preamble[4]);
  skip_byte = chunk_size & 0x1;
  audio_pts = this->audio_frames;
  audio_pts *= 90000;
  audio_pts /= this->audio_sample_rate;
  this->audio_frames += (chunk_size * 2 / this->audio_channels);

  while (chunk_size) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_VQA_IMA;
    buf->input_pos = current_file_pos;
    buf->input_length = this->filesize;
    buf->input_time = audio_pts / 90000;
    buf->pts = audio_pts;

    if (chunk_size > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = chunk_size;
    chunk_size -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) !=
      buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      break;
    }

    if (!chunk_size)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    this->audio_fifo->put (this->audio_fifo, buf);
  }
  /* stay on 16-bit alignment */
  if (skip_byte)
    this->input->seek(this->input, 1, SEEK_CUR);

  /* load and dispatch the video portion of the frame but only if this
   * is not frame #0 */
  if (this->iteration > 0) {
    if (this->input->read(this->input, preamble, VQA_PREAMBLE_SIZE) !=
      VQA_PREAMBLE_SIZE) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    current_file_pos = this->input->get_current_pos(this->input);
    chunk_size = BE_32(&preamble[4]);
    while (chunk_size) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_VIDEO_VQA;
      buf->input_pos = current_file_pos;
      buf->input_length = this->filesize;
      buf->input_time = this->video_pts / 90000;
      buf->pts = this->video_pts;

      if (chunk_size > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = chunk_size;
      chunk_size -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (!chunk_size)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      this->video_fifo->put (this->video_fifo, buf);
    }
    this->video_pts += VQA_PTS_INC;
  }

  this->iteration++;

  return this->status;
}

static void demux_vqa_send_headers(demux_plugin_t *this_gen) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 
    (this->audio_channels) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = 
    LE_16(&this->header[6]);
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = 
    LE_16(&this->header[8]);
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->audio_sample_rate;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->audio_bits;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = VQA_PTS_INC;  /* initial video_step */
  /* send the VQA header in place of the bitmapinfo header that many
   * demuxers send; the VQA video decoder will understand what this means */
  memcpy(buf->content, this->header, VQA_HEADER_SIZE);
  buf->size = VQA_HEADER_SIZE;
  buf->type = BUF_VIDEO_VQA;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo && this->audio_channels) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_VQA_IMA;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->audio_sample_rate;
    buf->decoder_info[2] = 16;  /* bits/samples */
    buf->decoder_info[3] = 1;   /* channels */
    this->audio_fifo->put (this->audio_fifo, buf);
  }

  xine_demux_control_headers_done (this->stream);
}

static int demux_vqa_seek (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    this->status = DEMUX_OK;
  }

  return this->status;
}

static void demux_vqa_dispose (demux_plugin_t *this_gen) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;

  free(this);
}

static int demux_vqa_get_status (demux_plugin_t *this_gen) {
  demux_vqa_t *this = (demux_vqa_t *) this_gen;

  return this->status;
}

static int demux_vqa_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_vqa_t    *this;

/*
  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_vqa.c: input not seekable, can not handle!\n"));
    return NULL;
  }
*/

  this         = xine_xmalloc (sizeof (demux_vqa_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_vqa_send_headers;
  this->demux_plugin.send_chunk        = demux_vqa_send_chunk;
  this->demux_plugin.seek              = demux_vqa_seek;
  this->demux_plugin.dispose           = demux_vqa_dispose;
  this->demux_plugin.get_status        = demux_vqa_get_status;
  this->demux_plugin.get_stream_length = demux_vqa_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!(this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE)) {
      free (this);
      return NULL;
    }

    if (!open_vqa_file(this)) {
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

    if (strncasecmp (ending, ".vqa", 4)) {
      free (this);
      return NULL;
    }

    if (!open_vqa_file(this)) {
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
  return "Westwood Studios VQA file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "VQA";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "vqa";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_vqa_class_t *this = (demux_vqa_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_vqa_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_vqa_class_t));
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
  { PLUGIN_DEMUX, 17, "vqa", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
