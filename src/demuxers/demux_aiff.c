/*
 * Copyright (C) 2001-2002 the xine project
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
 * AIFF File Demuxer by Mike Melanson (melanson@pcisys.net)
 *
 * $Id: demux_aiff.c,v 1.25 2003/01/17 16:52:33 miguelfreitas Exp $
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
#include <ctype.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "buffer.h"
#include "bswap.h"

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch3) | \
        ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | \
        ( (long)(unsigned char)(ch0) << 24 ) )

#define FORM_TAG FOURCC_TAG('F', 'O', 'R', 'M')
#define AIFF_TAG FOURCC_TAG('A', 'I', 'F', 'F')
#define COMM_TAG FOURCC_TAG('C', 'O', 'M', 'M')
#define SSND_TAG FOURCC_TAG('S', 'S', 'N', 'D')
#define APCM_TAG FOURCC_TAG('A', 'P', 'C', 'M')

#define PREAMBLE_SIZE 8
#define AIFF_SIGNATURE_SIZE 12
#define PCM_BLOCK_ALIGN 1024

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  status;

  unsigned int         audio_type;
  unsigned int         audio_frames;
  unsigned int         audio_sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;
  unsigned int         audio_block_align;
  unsigned int         audio_bytes_per_second;

  unsigned int         running_time;

  off_t                data_start;
  off_t                data_size;

  int                  seek_flag;  /* this is set when a seek just occurred */

  char                 last_mrl[1024];
} demux_aiff_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_aiff_class_t;

/* returns 1 if the AIFF file was opened successfully, 0 otherwise */
static int open_aiff_file(demux_aiff_t *this) {

  unsigned char signature[AIFF_SIGNATURE_SIZE];
  unsigned char preamble[PREAMBLE_SIZE];
  unsigned int chunk_type;
  unsigned int chunk_size;
  unsigned char buffer[100];

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, signature, AIFF_SIGNATURE_SIZE) !=
    AIFF_SIGNATURE_SIZE)
    return 0;

  /* check the signature */
  if ((BE_32(&signature[0]) != FORM_TAG) ||
      (BE_32(&signature[8]) != AIFF_TAG))
    return 0;

  /* audio type is PCM unless proven otherwise */
  this->audio_type = BUF_AUDIO_LPCM_BE;
  this->audio_frames = 0;
  this->audio_channels = 0;
  this->audio_bits = 0;
  this->audio_sample_rate = 0;
  this->audio_bytes_per_second = 0;

  /* skip past the file header and traverse the chunks */
  this->input->seek(this->input, 12, SEEK_SET);
  while (1) {
    if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
      PREAMBLE_SIZE) {
      this->status = DEMUX_FINISHED;
      return DEMUX_CANNOT_HANDLE;
    }
    chunk_type = BE_32(&preamble[0]);
    chunk_size = BE_32(&preamble[4]);

    if (chunk_type == COMM_TAG) {
      if (this->input->read(this->input, buffer, chunk_size) !=
        chunk_size) {
        this->status = DEMUX_FINISHED;
        return DEMUX_CANNOT_HANDLE;
      }

      this->audio_channels = BE_16(&buffer[0]);
      this->audio_frames = BE_32(&buffer[2]);
      this->audio_bits = BE_16(&buffer[6]);
      this->audio_sample_rate = BE_16(&buffer[0x0A]);
      this->audio_bytes_per_second = this->audio_channels *
        (this->audio_bits / 8) * this->audio_sample_rate;

    } else if ((chunk_type == SSND_TAG) || 
               (chunk_type == APCM_TAG)) {

      /* audio data has been located; proceed to demux loop after
       * skipping 8 more bytes (2 more 4-byte numbers) */
      this->input->seek(this->input, 8, SEEK_CUR);
      this->data_start = this->input->get_current_pos(this->input);
      this->data_size = this->audio_frames * this->audio_channels *
        (this->audio_bits / 8);
      this->running_time = this->audio_frames / this->audio_sample_rate;

      this->audio_block_align = PCM_BLOCK_ALIGN;

      break;

    } else {
      /* unrecognized; skip it */
      this->input->seek(this->input, chunk_size, SEEK_CUR);
    }
  }

  /* the audio parameters should have been set at this point */
  if (!this->audio_channels)
    return 0;

  return 1;
}

static int demux_aiff_send_chunk (demux_plugin_t *this_gen) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int remaining_sample_bytes;
  off_t current_file_pos;
  int64_t current_pts;
  int i;

  /* just load data chunks from wherever the stream happens to be
   * pointing; issue a DEMUX_FINISHED status if EOF is reached */
  remaining_sample_bytes = this->audio_block_align;
  current_file_pos = 
    this->input->get_current_pos(this->input) - this->data_start;

  current_pts = current_file_pos;
  current_pts *= 90000;
  current_pts /= this->audio_bytes_per_second;

  if (this->seek_flag) {
    xine_demux_control_newpts(this->stream, current_pts, 0);
    this->seek_flag = 0;
  }

  while (remaining_sample_bytes) {
    if( !this->audio_fifo ) {
      this->status = DEMUX_FINISHED;
      break;
    }

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->extra_info->input_pos = current_file_pos;
    buf->extra_info->input_length = this->data_size;
    buf->extra_info->input_time = current_pts / 90;
    buf->pts = current_pts;

    if (remaining_sample_bytes > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = remaining_sample_bytes;
    remaining_sample_bytes -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) !=
      buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      break;
    }

    /* convert 8-bit signed -> unsigned */
    if (this->audio_bits == 8)
      for (i = 0; i < buf->size; i++)
        buf->content[i] += 0x80;

    if (!remaining_sample_bytes)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    this->audio_fifo->put (this->audio_fifo, buf);
  }
  
  return this->status;
}

static void demux_aiff_send_headers(demux_plugin_t *this_gen) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->audio_sample_rate;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->audio_bits;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->audio_sample_rate;
    buf->decoder_info[2] = this->audio_bits;
    buf->decoder_info[3] = this->audio_channels;
    buf->size = 0;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_aiff_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;

  /* check the boundary offsets */
  if (start_pos < 0)
    this->input->seek(this->input, this->data_start, SEEK_SET);
  else if (start_pos >= this->data_size) {
    this->status = DEMUX_FINISHED;
    return this->status;
  } else {
    /* This function must seek along the block alignment. The start_pos
     * is in reference to the start of the data. Divide the start_pos by
     * the block alignment integer-wise, and multiply the quotient by the
     * block alignment to get the new aligned offset. Add the data start
     * offset and seek to the new position. */
    start_pos /= this->audio_block_align;
    start_pos *= this->audio_block_align;
    start_pos += this->data_start;

    this->input->seek(this->input, start_pos, SEEK_SET);
  }

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  xine_demux_flush_engine (this->stream);

  return this->status;
}

static void demux_aiff_dispose (demux_plugin_t *this_gen) {
  demux_aiff_t *this = (demux_aiff_t *) this_gen;

  free(this);
}

static int demux_aiff_get_status (demux_plugin_t *this_gen) {
  demux_aiff_t *this = (demux_aiff_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_aiff_get_stream_length (demux_plugin_t *this_gen) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;

  return this->running_time * 1000;
}

static uint32_t demux_aiff_get_capabilities(demux_plugin_t *this_gen)
{
  return DEMUX_CAP_NOCAP;
}

static int demux_aiff_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type)
{
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_aiff_t   *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_aiff.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_aiff_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_aiff_send_headers;
  this->demux_plugin.send_chunk        = demux_aiff_send_chunk;
  this->demux_plugin.seek              = demux_aiff_seek;
  this->demux_plugin.dispose           = demux_aiff_dispose;
  this->demux_plugin.get_status        = demux_aiff_get_status;
  this->demux_plugin.get_stream_length = demux_aiff_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_aiff_get_capabilities;
  this->demux_plugin.get_optional_data = demux_aiff_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_aiff_file(this)) {
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

    if (strncasecmp (ending, ".aif", 4) &&
        strncasecmp (ending, ".aiff", 5)) {
      free (this);
      return NULL;
    }

    if (!open_aiff_file(this)) {
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

  /* print vital stats */
  xine_log(this->stream->xine, XINE_LOG_MSG,
    _("demux_aiff: %d Hz, %d channels, %d bits, %d frames\n"),
    this->audio_sample_rate,
    this->audio_channels,
    this->audio_bits,
    this->audio_frames);
  xine_log(this->stream->xine, XINE_LOG_MSG,
    _("demux_aiff: running time: %d min, %d sec\n"),
    this->running_time / 60,
    this->running_time % 60);

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "AIFF file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "AIFF";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "aif aiff";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_aiff_class_t *this = (demux_aiff_class_t *) this_gen;

  free (this);
}

void *demux_aiff_init_plugin (xine_t *xine, void *data) {

  demux_aiff_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_aiff_class_t));
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
  { PLUGIN_DEMUX, 20, "aiff", XINE_VERSION_CODE, NULL, demux_aiff_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif
