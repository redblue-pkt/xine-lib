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
 * Interplay MVE File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Interplay MVE file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_ipmovie.c,v 1.5 2003/01/17 16:52:35 miguelfreitas Exp $
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

/* debugging support */
#define DEBUG_IPMOVIE 0
#if DEBUG_IPMOVIE
#define debug_ipmovie printf
#else
static inline void debug_ipmovie(const char *format, ...) { }
#endif

#define IPMOVIE_SIGNATURE "Interplay MVE File\x1A\0"
#define IPMOVIE_SIGNATURE_SIZE 20
#define CHUNK_PREAMBLE_SIZE 4
#define OPCODE_PREAMBLE_SIZE 4

#define CHUNK_INIT_AUDIO   0x0000
#define CHUNK_AUDIO_ONLY   0x0001
#define CHUNK_INIT_VIDEO   0x0002
#define CHUNK_VIDEO        0x0003
#define CHUNK_SHUTDOWN     0x0004
#define CHUNK_END          0x0005
/* this last type is used internally */
#define CHUNK_BAD          0xFFFF

#define OPCODE_END_OF_STREAM           0x00
#define OPCODE_END_OF_CHUNK            0x01
#define OPCODE_CREATE_TIMER            0x02
#define OPCODE_INIT_AUDIO_BUFFERS      0x03
#define OPCODE_START_STOP_AUDIO        0x04
#define OPCODE_INIT_VIDEO_BUFFERS      0x05
#define OPCODE_UNKNOWN_06              0x06
#define OPCODE_SEND_BUFFER             0x07
#define OPCODE_AUDIO_FRAME             0x08
#define OPCODE_SILENCE_FRAME           0x09
#define OPCODE_INIT_VIDEO_MODE         0x0A
#define OPCODE_CREATE_GRADIENT         0x0B
#define OPCODE_SET_PALETTE             0x0C
#define OPCODE_SET_PALETTE_COMPRESSED  0x0D
#define OPCODE_UNKNOWN_0E              0x0E
#define OPCODE_SET_DECODING_MAP        0x0F
#define OPCODE_UNKNOWN_10              0x10
#define OPCODE_VIDEO_DATA              0x11
#define OPCODE_UNKNOWN_12              0x12
#define OPCODE_UNKNOWN_13              0x13
#define OPCODE_UNKNOWN_14              0x14
#define OPCODE_UNKNOWN_15              0x15

#define PALETTE_COUNT 256

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

  unsigned int         fps;
  unsigned int         frame_pts_inc;

  unsigned int         video_width;
  unsigned int         video_height;
  int64_t              video_pts;
  unsigned int         audio_bits;
  unsigned int         audio_channels;
  unsigned int         audio_sample_rate;
  unsigned int         audio_type;
  unsigned int         audio_frame_count;

  palette_entry_t      palette[PALETTE_COUNT];

  char                 last_mrl[1024];

} demux_ipmovie_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_ipmovie_class_t;

/* This function loads and processes a single chunk in an IP movie file.
 * It returns the type of chunk that was processed. */
static int process_ipmovie_chunk(demux_ipmovie_t *this) {

  unsigned char chunk_preamble[CHUNK_PREAMBLE_SIZE];
  int chunk_type;
  int chunk_size;
  unsigned char opcode_preamble[OPCODE_PREAMBLE_SIZE];
  unsigned char opcode_type;
  unsigned char opcode_version;
  int opcode_size;
  unsigned char scratch[1024];
  int i, j;
  int first_color, last_color;
  int audio_flags;
  buf_element_t *buf = NULL;
  off_t current_file_pos;
  int64_t audio_pts = 0;

  /* read the next chunk, wherever the file happens to be pointing */
  if (this->input->read(this->input, chunk_preamble, CHUNK_PREAMBLE_SIZE) !=
    CHUNK_PREAMBLE_SIZE)
    return CHUNK_BAD;
  chunk_size = LE_16(&chunk_preamble[0]);
  chunk_type = LE_16(&chunk_preamble[2]);

  debug_ipmovie ("ipmovie: chunk type 0x%04X, 0x%04X bytes: ", 
    chunk_type, chunk_size);

  switch (chunk_type) {

    case CHUNK_INIT_AUDIO:
      debug_ipmovie ("initialize audio\n");
      break;

    case CHUNK_AUDIO_ONLY:
      debug_ipmovie ("audio only\n");
      break;

    case CHUNK_INIT_VIDEO:
      debug_ipmovie ("initialize video\n");
      break;

    case CHUNK_VIDEO:
      debug_ipmovie ("video (and audio)\n");
      break;

    case CHUNK_SHUTDOWN:
      debug_ipmovie ("shutdown\n");
      break;

    case CHUNK_END:
      debug_ipmovie ("end\n");
      break;

    default:
      debug_ipmovie ("invalid chunk\n");
      chunk_type = CHUNK_BAD;
      break;

  }

  while ((chunk_size > 0) && (chunk_type != CHUNK_BAD)) {

    /* read the next chunk, wherever the file happens to be pointing */
    if (this->input->read(this->input, opcode_preamble, 
      OPCODE_PREAMBLE_SIZE) != OPCODE_PREAMBLE_SIZE) {
      chunk_type = CHUNK_BAD;
      break;
    }

    opcode_size = LE_16(&opcode_preamble[0]);
    opcode_type = opcode_preamble[2];
    opcode_version = opcode_preamble[3];

    chunk_size -= OPCODE_PREAMBLE_SIZE;
    chunk_size -= opcode_size;
    if (chunk_size < 0) {
      printf ("demux_ipmovie: chunk_size countdown just went negative\n");
      chunk_type = CHUNK_BAD;
      break;
    }
    debug_ipmovie ("  opcode type %02X, version %d, 0x%04X bytes: ",
      opcode_type, opcode_version, opcode_size);
    switch (opcode_type) {

      case OPCODE_END_OF_STREAM:
        debug_ipmovie ("end of stream\n");
        this->input->seek(this->input, opcode_size, SEEK_CUR);
        break;

      case OPCODE_END_OF_CHUNK:
        debug_ipmovie ("end of chunk\n");
        this->input->seek(this->input, opcode_size, SEEK_CUR);
        break;

      case OPCODE_CREATE_TIMER:
        debug_ipmovie ("create timer\n");
        if ((opcode_version > 0) || (opcode_size > 6)) {
          printf ("demux_ipmovie: bad create_timer opcode\n");
          chunk_type = CHUNK_BAD;
          break;
        }
        if (this->input->read(this->input, scratch, opcode_size) != 
          opcode_size) {
          chunk_type = CHUNK_BAD;
          break;
        }
        this->fps = 1000000 / (LE_32(&scratch[0]) * LE_16(&scratch[4]));
this->fps++;  /* above calculation usually yields 14.9; we need 15 */
        this->frame_pts_inc = 90000 / this->fps;
        debug_ipmovie ("    %d frames/second (timer div = %d, subdiv = %d)\n",
          this->fps, LE_32(&scratch[0]), LE_16(&scratch[4]));
        break;

      case OPCODE_INIT_AUDIO_BUFFERS:
        debug_ipmovie ("initialize audio buffers\n");
        if ((opcode_version > 1) || (opcode_size > 10)) {
          printf ("demux_ipmovie: bad init_audio_buffers opcode\n");
          chunk_type = CHUNK_BAD;
          break;
        }
        if (this->input->read(this->input, scratch, opcode_size) != 
          opcode_size) {
          chunk_type = CHUNK_BAD;
          break;
        }
        this->audio_sample_rate = LE_16(&scratch[4]);
        audio_flags = LE_16(&scratch[2]);
        /* bit 0 of the flags: 0 = mono, 1 = stereo */
        this->audio_channels = (audio_flags & 1) + 1;
        /* bit 1 of the flags: 0 = 8 bit, 1 = 16 bit */
        this->audio_bits = (((audio_flags >> 1) & 1) + 1) * 8;
        /* bit 2 indicates compressed audio in version 1 opcode */
        if ((opcode_version == 1) && (audio_flags & 0x4))
          this->audio_type = BUF_AUDIO_INTERPLAY;
        else
          this->audio_type = BUF_AUDIO_LPCM_LE;
        debug_ipmovie ("    audio: %d bits, %d Hz, %s, %s format\n",
          this->audio_bits,
          this->audio_sample_rate,
          (this->audio_channels == 2) ? "stereo" : "mono",
          (this->audio_type == BUF_AUDIO_LPCM_LE) ? "PCM" : "Interplay audio");
        break;

      case OPCODE_START_STOP_AUDIO:
        debug_ipmovie ("start/stop audio\n");
        this->input->seek(this->input, opcode_size, SEEK_CUR);
        break;

      case OPCODE_INIT_VIDEO_BUFFERS:
        debug_ipmovie ("initialize video buffers\n");
        if ((opcode_version > 2) || (opcode_size > 8)) {
          printf ("demux_ipmovie: bad init_video_buffers opcode\n");
          chunk_type = CHUNK_BAD;
          break;
        }
        if (this->input->read(this->input, scratch, opcode_size) != 
          opcode_size) {
          chunk_type = CHUNK_BAD;
          break;
        }
        this->video_width = LE_16(&scratch[0]) * 8;
        this->video_height = LE_16(&scratch[2]) * 8;
        debug_ipmovie ("    video resolution: %d x %d\n",
          this->video_width, this->video_height);
        break;

      case OPCODE_UNKNOWN_06:
      case OPCODE_UNKNOWN_0E:
      case OPCODE_UNKNOWN_10:
      case OPCODE_UNKNOWN_12:
      case OPCODE_UNKNOWN_13:
      case OPCODE_UNKNOWN_14:
      case OPCODE_UNKNOWN_15:
        debug_ipmovie ("unknown (but documented) opcode %02X\n", opcode_type);
        this->input->seek(this->input, opcode_size, SEEK_CUR);
        break;

      case OPCODE_SEND_BUFFER:
        debug_ipmovie ("send buffer\n");
        this->input->seek(this->input, opcode_size, SEEK_CUR);
        break;

      case OPCODE_AUDIO_FRAME:
        debug_ipmovie ("audio frame\n");

        current_file_pos = this->input->get_current_pos(this->input);

        /* figure out the number of audio frames */
        if (this->audio_type == BUF_AUDIO_LPCM_LE)
          this->audio_frame_count +=
            (opcode_size / this->audio_channels / (this->audio_bits / 8));
        else
          this->audio_frame_count +=
            (opcode_size - 6) / this->audio_channels;
        audio_pts = 90000;
        audio_pts *= this->audio_frame_count;
        audio_pts /= this->audio_sample_rate;

        debug_ipmovie ("    sending audio frame with pts %lld (%d audio frames)\n",
          audio_pts, this->audio_frame_count);

        if(this->audio_fifo) {
          while (opcode_size) {
            buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
            buf->type = this->audio_type;
            buf->extra_info->input_pos = current_file_pos;
            buf->extra_info->input_length = this->data_size;
            buf->extra_info->input_time = audio_pts / 90;
            buf->pts = audio_pts;
  
            if (opcode_size > buf->max_size)
              buf->size = buf->max_size;
            else
              buf->size = opcode_size;
            opcode_size -= buf->size;
  
            if (this->input->read(this->input, buf->content, buf->size) !=
              buf->size) {
              buf->free_buffer(buf);
              chunk_type = CHUNK_BAD;
              break;
            }
  
            if (!opcode_size)
              buf->decoder_flags |= BUF_FLAG_FRAME_END;
  
            this->audio_fifo->put (this->audio_fifo, buf);
          }
        }else{
          this->input->seek(this->input, opcode_size, SEEK_CUR);
        }
        break;

      case OPCODE_SILENCE_FRAME:
        debug_ipmovie ("silence frame\n");
        this->input->seek(this->input, opcode_size, SEEK_CUR);
        break;

      case OPCODE_INIT_VIDEO_MODE:
        debug_ipmovie ("initialize video mode\n");
        this->input->seek(this->input, opcode_size, SEEK_CUR);
        break;

      case OPCODE_CREATE_GRADIENT:
        debug_ipmovie ("create gradient\n");
        this->input->seek(this->input, opcode_size, SEEK_CUR);
        break;

      case OPCODE_SET_PALETTE:
        debug_ipmovie ("set palette\n");
        /* check for the logical maximum palette size 
         * (3 * 256 + 4 bytes) */
        if (opcode_size > 0x304) {
          printf ("demux_ipmovie: set_palette opcode too large\n");
          chunk_type = CHUNK_BAD;
          break;
        }
        if (this->input->read(this->input, scratch, opcode_size) != 
          opcode_size) {
          chunk_type = CHUNK_BAD;
          break;
        }

        /* load the palette into internal data structure */
        first_color = LE_16(&scratch[0]);
        last_color = LE_16(&scratch[2]);
        /* sanity check (since they are 16 bit values) */
        if ((first_color > 0xFF) || (last_color > 0xFF)) {
          printf ("demux_ipmovie: set_palette indices out of range (%d -> %d)\n",
            first_color, last_color);
          chunk_type = CHUNK_BAD;
          break;
        }
        j = 4;  /* offset of first palette data */
        for (i = first_color; i <= last_color; i++) {
          this->palette[i].r = scratch[j++] * 4;
          this->palette[i].g = scratch[j++] * 4;
          this->palette[i].b = scratch[j++] * 4;
        }
        break;

      case OPCODE_SET_PALETTE_COMPRESSED:
        debug_ipmovie ("set palette compressed\n");
        this->input->seek(this->input, opcode_size, SEEK_CUR);
        break;

      case OPCODE_SET_DECODING_MAP:
        debug_ipmovie ("set decoding map\n");

        current_file_pos = this->input->get_current_pos(this->input);
        debug_ipmovie ("    sending decoding map along with duration %d\n",
          this->frame_pts_inc);

        while (opcode_size) {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->type = BUF_VIDEO_INTERPLAY;
          buf->extra_info->input_pos = current_file_pos;
          buf->extra_info->input_length = this->data_size;
          buf->extra_info->input_time = this->video_pts / 90;
          buf->pts = this->video_pts;

          if (opcode_size > buf->max_size)
            buf->size = buf->max_size;
          else
            buf->size = opcode_size;
          opcode_size -= buf->size;

          if (this->input->read(this->input, buf->content, buf->size) !=
            buf->size) {
            buf->free_buffer(buf);
            this->status = DEMUX_FINISHED;
            break;
          }

          if (!opcode_size)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;

          /* send the duration since it was not known when headers were sent */
          buf->decoder_flags |= BUF_FLAG_FRAMERATE;
          buf->decoder_info[0] = this->frame_pts_inc;

          this->video_fifo->put (this->video_fifo, buf);
        }

        break;

      case OPCODE_VIDEO_DATA:
        debug_ipmovie ("set video data\n");

        current_file_pos = this->input->get_current_pos(this->input);
        debug_ipmovie ("    sending video data with pts %lld\n",
          this->video_pts);

        while (opcode_size) {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->type = BUF_VIDEO_INTERPLAY;
          buf->extra_info->input_pos = current_file_pos;
          buf->extra_info->input_length = this->data_size;
          buf->extra_info->input_time = this->video_pts / 90;
          buf->pts = this->video_pts;

          if (opcode_size > buf->max_size)
            buf->size = buf->max_size;
          else
            buf->size = opcode_size;
          opcode_size -= buf->size;

          if (this->input->read(this->input, buf->content, buf->size) !=
            buf->size) {
            buf->free_buffer(buf);
            this->status = DEMUX_FINISHED;
            break;
          }

          if (!opcode_size)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;

          /* Abuse the keyframe flag: Since a video chunk consists of 2
           * distinct parts (the decode map, then the video data), and since
           * the format has no real notion of keyframes or seeking, use the
           * keyframe flag to indicate that this is the video portion of
           * the frame. */
          buf->decoder_flags |= BUF_FLAG_KEYFRAME;

          this->video_fifo->put (this->video_fifo, buf);
        }

        this->video_pts += this->frame_pts_inc;
        break;

      default:
        debug_ipmovie (" *** unknown opcode type\n");
        chunk_type = CHUNK_BAD;
        break;

    }

  }

  return chunk_type;
}

/* returns 1 if the MVE file was opened successfully, 0 otherwise */
static int open_ipmovie_file(demux_ipmovie_t *this) {

  unsigned char signature[IPMOVIE_SIGNATURE_SIZE];

  this->audio_type = 0;

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, signature, IPMOVIE_SIGNATURE_SIZE) !=
    IPMOVIE_SIGNATURE_SIZE)
    return 0;

  if (strncmp(signature, IPMOVIE_SIGNATURE, IPMOVIE_SIGNATURE_SIZE) != 0)
    return 0;

  /* skip the 6 unknown bytes */
  this->input->seek(this->input, 6, SEEK_CUR);

  /* process the first chunk which should be CHUNK_INIT_VIDEO */
  if (process_ipmovie_chunk(this) != CHUNK_INIT_VIDEO)
    return 0;

  /* process the next chunk which should be CHUNK_INIT_AUDIO */
  if (process_ipmovie_chunk(this) != CHUNK_INIT_AUDIO)
    return 0;

  debug_ipmovie ("detected Interplay MVE file\n");
  this->data_size = this->input->get_length(this->input);
  this->audio_frame_count = 0;
  this->video_pts = 0;

  return 1;
}

static int demux_ipmovie_send_chunk(demux_plugin_t *this_gen) {

  demux_ipmovie_t *this = (demux_ipmovie_t *) this_gen;

  if (process_ipmovie_chunk(this) == CHUNK_BAD)
    this->status = DEMUX_FINISHED;

  return this->status;
}

static void demux_ipmovie_send_headers(demux_plugin_t *this_gen) {

  demux_ipmovie_t *this = (demux_ipmovie_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = this->video_width;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->video_height;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to video decoder */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER;
  buf->decoder_info[0] = 0;
  /* bogus initial video_step, but we won't know for sure until we see
   * the first video frame; however, fps for these files is usually 15 */
  buf->decoder_info[1] = 6000;
  /* really be a rebel: No structure at all, just put the video width
   * and height straight into the buffer, BE_16 format */
  buf->content[0] = (this->video_width >> 8) & 0xFF;
  buf->content[1] = (this->video_width >> 0) & 0xFF;
  buf->content[2] = (this->video_height >> 8) & 0xFF;
  buf->content[3] = (this->video_height >> 0) & 0xFF;
  buf->size = 4;
  buf->type = BUF_VIDEO_INTERPLAY;
  this->video_fifo->put (this->video_fifo, buf);

  /* send off the palette */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_SPECIAL;
  buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
  buf->decoder_info[2] = 256;
  buf->decoder_info_ptr[2] = &this->palette;
  buf->size = 0;
  buf->type = BUF_VIDEO_INTERPLAY;
  this->video_fifo->put (this->video_fifo, buf);

  /* send init info to the audio decoder */
  if ((this->audio_fifo) && (this->audio_type)) {
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

static int demux_ipmovie_seek (demux_plugin_t *this_gen,
                               off_t start_pos, int start_time) {

  demux_ipmovie_t *this = (demux_ipmovie_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    /* send new pts */
    xine_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;

    /* make sure to start just after the header and 6 unknown bytes */
    this->input->seek(this->input, IPMOVIE_SIGNATURE_SIZE + 6, SEEK_SET);
  }

  return this->status;
}

static void demux_ipmovie_dispose (demux_plugin_t *this) {

  free(this);
}

static int demux_ipmovie_get_status (demux_plugin_t *this_gen) {
  demux_ipmovie_t *this = (demux_ipmovie_t *) this_gen;

  return this->status;
}

static int demux_ipmovie_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static uint32_t demux_ipmovie_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_ipmovie_get_optional_data(demux_plugin_t *this_gen,
                                           void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_ipmovie_t    *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_ipmovie.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_ipmovie_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_ipmovie_send_headers;
  this->demux_plugin.send_chunk        = demux_ipmovie_send_chunk;
  this->demux_plugin.seek              = demux_ipmovie_seek;
  this->demux_plugin.dispose           = demux_ipmovie_dispose;
  this->demux_plugin.get_status        = demux_ipmovie_get_status;
  this->demux_plugin.get_stream_length = demux_ipmovie_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_ipmovie_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ipmovie_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_ipmovie_file(this)) {
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

    if ((strncasecmp (ending, ".mve", 4)) &&
        (strncasecmp (ending, ".mv8", 4))) {
      free (this);
      return NULL;
    }

    if (!open_ipmovie_file(this)) {
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
  return "Interplay MVE Movie demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "Interplay MVE";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "mve mv8";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_ipmovie_class_t *this = (demux_ipmovie_class_t *) this_gen;

  free (this);
}

void *demux_ipmovie_init_plugin (xine_t *xine, void *data) {

  demux_ipmovie_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_ipmovie_class_t));
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
  { PLUGIN_DEMUX, 20, "ipmovie", XINE_VERSION_CODE, NULL, demux_ipmovie_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif
