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
 * RoQ File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the RoQ file format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 *
 * $Id: demux_roq.c,v 1.32 2002/12/08 21:43:51 miguelfreitas Exp $
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

#define RoQ_MAGIC_NUMBER 0x1084
#define RoQ_CHUNK_PREAMBLE_SIZE 8
#define RoQ_AUDIO_SAMPLE_RATE 22050

#define RoQ_INFO           0x1001
#define RoQ_QUAD_CODEBOOK  0x1002
#define RoQ_QUAD_VQ        0x1011
#define RoQ_SOUND_MONO     0x1020
#define RoQ_SOUND_STEREO   0x1021

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  off_t                start;
  int                  status;

  unsigned int         fps;
  unsigned int         frame_pts_inc; 

  unsigned int         width;
  unsigned int         height;
  unsigned int         audio_channels;

  char                 last_mrl[1024];
} demux_roq_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_roq_class_t;

/* returns 1 if the RoQ file was opened successfully, 0 otherwise */
static int open_roq_file(demux_roq_t *this) {

  char preamble[RoQ_CHUNK_PREAMBLE_SIZE];
  int i;
  unsigned int chunk_type;
  unsigned int chunk_size;

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != 
    RoQ_CHUNK_PREAMBLE_SIZE)
    return 0;

  /* check for the RoQ magic numbers */
  if ((LE_16(&preamble[0]) != RoQ_MAGIC_NUMBER) ||
      (LE_32(&preamble[2]) != 0xFFFFFFFF))
    return 0;

  this->width = this->height = 0;
  this->audio_channels = 0;  /* assume no audio at first */

  /*
   * RoQ files enjoy a constant framerate; pts calculation:
   *
   *   xine pts     frame #
   *   --------  =  -------  =>  xine pts = 90000 * frame # / fps
   *    90000         fps
   *
   * therefore, the frame pts increment is 90000 / fps
   */
  this->fps = LE_16(&preamble[6]);
  this->frame_pts_inc = 90000 / this->fps;

  /* iterate through the first 2 seconds worth of chunks searching for
   * the RoQ_INFO chunk and an audio chunk */
  i = this->fps * 2;
  while (i-- > 0) {
    /* if this read fails, then maybe it's just a really small RoQ file
     * (even less than 2 seconds) */
    if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != 
      RoQ_CHUNK_PREAMBLE_SIZE)
      break;
    chunk_type = LE_16(&preamble[0]);
    chunk_size = LE_32(&preamble[2]);

    if (chunk_type == RoQ_INFO) {
      /* fetch the width and height; reuse the preamble bytes */
      if (this->input->read(this->input, preamble, 8) != 8)
        break;

      this->width = LE_16(&preamble[0]);
      this->height = LE_16(&preamble[2]);

      /* if an audio chunk was already found, search is done */
      if (this->audio_channels)
        break;

      /* prep the size for a seek */
      chunk_size -= 8;
    } else {
      /* if it was an audio chunk and the info chunk has already been
       * found (as indicated by width and height) then break */
      if (chunk_type == RoQ_SOUND_MONO) {
        this->audio_channels = 1;
        if (this->width && this->height)
          break;
      } else if (chunk_type == RoQ_SOUND_STEREO) {
        this->audio_channels = 2;
        if (this->width && this->height)
          break;
      }
    }

    /* skip the rest of the chunk */
    this->input->seek(this->input, chunk_size, SEEK_CUR);
  }

  /* after all is said and done, if there is a width and a height,
   * regard it as being a valid file and reset to the first chunk */
  if (this->width && this->height) {
    this->input->seek(this->input, 8, SEEK_SET);
  } else {
    return 0;
  }

  return 1;
}

static int demux_roq_send_chunk(demux_plugin_t *this_gen) {

  demux_roq_t *this = (demux_roq_t *) this_gen;
  buf_element_t *buf = NULL;
  char preamble[RoQ_CHUNK_PREAMBLE_SIZE];
  unsigned int chunk_type;
  unsigned int chunk_size;
  int64_t video_pts_counter = 0;
  int64_t audio_pts;
  unsigned int audio_byte_count = 0;
  off_t current_file_pos;

  /* fetch the next preamble */
  if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != 
    RoQ_CHUNK_PREAMBLE_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  chunk_type = LE_16(&preamble[0]);
  chunk_size = LE_32(&preamble[2]);

  /* if the chunk is an audio chunk, route it to the audio fifo */
  if ((chunk_type == RoQ_SOUND_MONO) || (chunk_type == RoQ_SOUND_STEREO)) {
    /* rewind over the preamble */
    this->input->seek(this->input, -RoQ_CHUNK_PREAMBLE_SIZE, SEEK_CUR);

    /* adjust the chunk size */
    chunk_size += RoQ_CHUNK_PREAMBLE_SIZE;

    if( this->audio_fifo ) {
        
      /* do this calculation carefully because I can't trust the
       * 64-bit numerical manipulation */
      audio_pts = audio_byte_count;
      audio_pts *= 90000;
      audio_pts /= (RoQ_AUDIO_SAMPLE_RATE * this->audio_channels);
      audio_byte_count += chunk_size - 8;  /* do not count the preamble */
         
      current_file_pos = this->input->get_current_pos(this->input);

      /* packetize the audio */
      while (chunk_size) {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = BUF_AUDIO_ROQ;
        buf->input_pos = current_file_pos;
        buf->pts = audio_pts;

        if (chunk_size > buf->max_size)
          buf->size = buf->max_size;
        else
          buf->size = chunk_size;
        chunk_size -= buf->size;

        if (this->input->read(this->input, buf->content, buf->size) !=
          buf->size) {
          this->status = DEMUX_FINISHED;
          break;
        }

        if (!chunk_size)
          buf->decoder_flags |= BUF_FLAG_FRAME_END;
        this->audio_fifo->put(this->audio_fifo, buf);
      }
    } else {
      /* no audio -> skip chunk */
      this->input->seek(this->input, chunk_size, SEEK_CUR);
    }
  } else if (chunk_type == RoQ_INFO) {
    /* skip 8 bytes */
    this->input->seek(this->input, 8, SEEK_CUR);
  } else if ((chunk_type == RoQ_QUAD_CODEBOOK) ||
    (chunk_type == RoQ_QUAD_VQ)) {

    current_file_pos = this->input->get_current_pos(this->input) -
      RoQ_CHUNK_PREAMBLE_SIZE;

    /* if the chunk is video, check if it is a codebook */
    if (chunk_type == RoQ_QUAD_CODEBOOK) {
      /* if it is, figure in the size of the next VQ chunk, too */
      this->input->seek(this->input, chunk_size, SEEK_CUR);
      if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != 
        RoQ_CHUNK_PREAMBLE_SIZE) {
        this->status = DEMUX_FINISHED;
        return this->status;
      }

      /* rewind to the start of the codebook chunk */
      this->input->seek(this->input, current_file_pos, SEEK_SET);

      /* figure out the total video chunk size */
      chunk_size += (2 * RoQ_CHUNK_PREAMBLE_SIZE) + LE_32(&preamble[2]);
    }

    /* packetize the video chunk and route it to the video fifo */
    while (chunk_size) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_VIDEO_ROQ;
      buf->input_pos = current_file_pos;
      buf->pts = video_pts_counter;

      if (chunk_size > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = chunk_size;
      chunk_size -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        this->status = DEMUX_FINISHED;
        break;
      }

      if (!chunk_size)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
      this->video_fifo->put(this->video_fifo, buf);
    }
    video_pts_counter += this->frame_pts_inc;
  } else {
    printf ("demux_roq: encountered bad chunk type: %d\n", chunk_type);
  }

  return this->status;
}

static void demux_roq_send_headers(demux_plugin_t *this_gen) {

  demux_roq_t *this = (demux_roq_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 
    (this->audio_channels) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = this->width;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->height;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    RoQ_AUDIO_SAMPLE_RATE;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] = 16;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = this->frame_pts_inc;  /* initial video_step */
  /* really be a rebel: No structure at all, just put the video width
   * and height straight into the buffer, BE_16 format */
  buf->content[0] = (this->width >> 8) & 0xFF;
  buf->content[1] = (this->width >> 0) & 0xFF;
  buf->content[2] = (this->height >> 8) & 0xFF;
  buf->content[3] = (this->height >> 0) & 0xFF;
  buf->size = 4;
  buf->type = BUF_VIDEO_ROQ;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo && this->audio_channels) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_ROQ;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = RoQ_AUDIO_SAMPLE_RATE;
    buf->decoder_info[2] = 16;
    buf->decoder_info[3] = this->audio_channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_roq_seek (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {

  demux_roq_t *this = (demux_roq_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    /* send new pts */
    xine_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  
    /* start after the signature chunk */
    this->input->seek(this->input, RoQ_CHUNK_PREAMBLE_SIZE, SEEK_SET);
    
    this->status = DEMUX_OK;
  }
  
  return this->status;
}

static void demux_roq_dispose (demux_plugin_t *this) {

  free(this);
}

static int demux_roq_get_status (demux_plugin_t *this_gen) {
  demux_roq_t *this = (demux_roq_t *) this_gen;

  return this->status;
}

static int demux_roq_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static uint32_t demux_roq_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_roq_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_roq_t    *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_roq.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_roq_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_roq_send_headers;
  this->demux_plugin.send_chunk        = demux_roq_send_chunk;
  this->demux_plugin.seek              = demux_roq_seek;
  this->demux_plugin.dispose           = demux_roq_dispose;
  this->demux_plugin.get_status        = demux_roq_get_status;
  this->demux_plugin.get_stream_length = demux_roq_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_roq_get_capabilities;
  this->demux_plugin.get_optional_data = demux_roq_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;
  
  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_roq_file(this)) {
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

    if (strncasecmp (ending, ".roq", 4)) {
      free (this);
      return NULL;
    }

    if (!open_roq_file(this)) {
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
  xine_log (this->stream->xine, XINE_LOG_MSG,
    _("demux_roq: RoQ file, video is %dx%d, %d frames/sec\n"),
    this->width, this->height, this->fps);
  if (this->audio_channels)
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("demux_roq: 16-bit, 22050 Hz %s RoQ DPCM audio\n"),
      (this->audio_channels == 1) ? "monaural" : "stereo");

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "Id RoQ file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "RoQ";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "roq";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_roq_class_t *this = (demux_roq_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_roq_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_roq_class_t));
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
  { PLUGIN_DEMUX, 18, "roq", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
