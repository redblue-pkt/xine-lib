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
 * $Id: demux_eawve.c,v 1.4 2002/11/03 21:03:09 komadori Exp $
 *
 * demux_eawve.c, Demuxer plugin for Electronic Arts' WVE file format
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

#include "xine_internal.h"
#include "xineutils.h"
#include "bswap.h"
#include "demux.h"

#define FOURCC_TAG(ch0, ch1, ch2, ch3) \
                 (((uint32_t)(ch3)) | \
                  ((uint32_t)(ch2) << 8) | \
                  ((uint32_t)(ch1) << 16) | \
                  ((uint32_t)(ch0) << 24))

typedef struct {
  demux_plugin_t demux_plugin;
  xine_stream_t *stream;
  config_values_t *config;
  fifo_buffer_t *video_fifo;
  fifo_buffer_t *audio_fifo;
  input_plugin_t *input;

  int thread_running;

  int status;
  int send_end_buffers;
  
  int num_channels;
  int compression_type;
  int num_samples;

  int sample_counter;
  char last_mrl[1024];
} demux_eawve_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_eawve_class_t;

typedef struct {
  uint32_t id;
  uint32_t size;
} chunk_header_t;

/*
 * Read an arbitary number of byte into a word
 */
 
static uint32_t read_arbitary(input_plugin_t *input)
{
  uint8_t size, byte;
  int i;
  uint32_t word;

  if (input->read(input, (void*)&size, 1) != 1) {
    return 0;
  }

  word = 0;
  for (i=0;i<size;i++) {
    if (input->read(input, (void*)&byte, 1) != 1) {
      return 0;
    }
    word <<= 8;
    word |= byte;
  }

  return word;
}

/*
 * Skip a number of bytes
 */

static int skip_bytes(input_plugin_t *input, int bytes)
{
  if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
    return input->seek(input, bytes, SEEK_CUR);
  }
  else {
    int i, dummy;

    for (i=0;i<bytes;i++) {
      if (input->read(input, (void*)&dummy, 1) != 1) {
        return -1;
      }
    }
    return input->get_current_pos(input);
  }
}

/*
 * Process WVE file header
 * Returns 1 if the WVE file is valid and successfully opened, 0 otherwise
 */

static int process_header(demux_eawve_t *this)
{
  int inHeader;
  uint32_t blockid, size;

  if (this->input->get_current_pos(this->input) != 0) {
    if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) == 0) {
      return 0;
    }
    this->input->seek(this->input, 0, SEEK_SET);
  }

  if (this->input->read(this->input, (void*)&blockid, 4) != 4) {
    return 0;
  }
  if (be2me_32(blockid) != FOURCC_TAG('S', 'C', 'H', 'l')) {
    return 0;
  }

  if (this->input->read(this->input, (void*)&size, 4) != 4) {
    return 0;
  }
  size = le2me_32(size);

  if (this->input->read(this->input, (void*)&blockid, 4) != 4) {
    return 0;
  }
  if (be2me_32(blockid) != FOURCC_TAG('P', 'T', '\0', '\0')) {
    printf("demux_eawve: PT header missing\n");
    return 0;
  }

  inHeader = 1;
  while (inHeader) {
    int inSubheader;
    uint8_t byte;
    if (this->input->read(this->input, (void*)&byte, 1) != 1) {
      return 0;
    }

    switch (byte) {
      case 0xFD:
        printf("demux_eawve: entered audio subheader\n");
        inSubheader = 1;
        while (inSubheader) {
          uint8_t subbyte;
          if (this->input->read(this->input, (void*)&subbyte, 1) != 1) {
            return 0;
          }

          switch (subbyte) {
            case 0x82:
              this->num_channels = read_arbitary(this->input);
              printf("demux_eawve: num_channels (element 0x82) set to 0x%08x\n", this->num_channels);
            break;
            case 0x83:
              this->compression_type = read_arbitary(this->input);
              printf("demux_eawve: compression_type (element 0x83) set to 0x%08x\n", this->compression_type);
            break;
            case 0x85:
              this->num_samples = read_arbitary(this->input);
              printf("demux_eawve: num_samples (element 0x85) set to 0x%08x\n", this->num_samples);
            break;
            default:
              printf("demux_eawve: element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(this->input));
            break;
            case 0x8A:
              printf("demux_eawve: element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(this->input));
              printf("demux_eawve: exited audio subheader\n");
              inSubheader = 0;
            break;
          }
        }
      break;
      default:
        printf("demux_eawve: header element 0x%02x set to 0x%08x\n", byte, read_arbitary(this->input));
      break;
      case 0xFF:
        printf("demux_eawve: end of header block reached\n");
        inHeader = 0;
      break;
    }
  }

  if ((this->num_channels != 2) || (this->compression_type != 7)) {
    printf("demux_eawve: unsupported stream type\n");
    return 0;
  }

  if (skip_bytes(this->input, size - this->input->get_current_pos(this->input)) < 0) {
    return 0;
  }

  return 1;
}

/*
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 * All the following functions are defined by the xine demuxer API
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 */

static int demux_eawve_send_chunk(demux_eawve_t *this)
{
  chunk_header_t header;

  if (this->input->read(this->input, (void*)&header, sizeof(chunk_header_t)) != sizeof(chunk_header_t)) {
    printf("demux_eawve: read error\n");
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  header.id = be2me_32(header.id);
  header.size = le2me_32(header.size) - 8;

  switch (header.id) {
    case FOURCC_TAG('S', 'C', 'D', 'l'): {
      int first_segment = 1;

      while (header.size > 0) {
        buf_element_t *buf;

        buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);
        buf->type = BUF_AUDIO_EA_ADPCM;
        buf->input_pos = this->input->get_current_pos(this->input);
        buf->input_time = this->sample_counter / 22050;
        buf->pts = this->sample_counter;
        buf->pts *= 90000;
        buf->pts /= 22050;

        if (header.size > buf->max_size) {
          buf->size = buf->max_size;
        }
        else {
          buf->size = header.size;
        }
        header.size -= buf->size;

        if (this->input->read(this->input, buf->content, buf->size) != buf->size) {
          printf("demux_eawve: read error\n");
          this->status = DEMUX_FINISHED;
          buf->free_buffer(buf);
          break;
        }

        if (first_segment) {
          buf->decoder_flags |= BUF_FLAG_FRAME_START;
          this->sample_counter += LE_32(buf->content);
          first_segment = 0;
        }

        if (header.size == 0) {
          buf->decoder_flags |= BUF_FLAG_FRAME_END;
        }

        this->audio_fifo->put(this->audio_fifo, buf);
      }
    }
    break;

    case FOURCC_TAG('S', 'C', 'E', 'l'): {
      this->status = DEMUX_FINISHED;
    }
    break;

    default: {
      if (skip_bytes(this->input, header.size) < 0) {
        printf("demux_eawve: read error\n");
        this->status = DEMUX_FINISHED;
      }
    }
    break;
  }

  return this->status;
}

static void demux_eawve_send_headers(demux_plugin_t *this_gen)
{
  demux_eawve_t *this = (demux_eawve_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = 2;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] = 22050;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] = 16;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo) {
    buf_element_t *buf;

    buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);
    buf->type = BUF_AUDIO_EA_ADPCM;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = 22050;
    buf->decoder_info[2] = 16;
    buf->decoder_info[3] = 2;
    this->audio_fifo->put(this->audio_fifo, buf);
  }

  xine_demux_control_headers_done (this->stream);
}

static int demux_eawve_seek(demux_eawve_t *this, off_t start_pos, int start_time)
{

  if (!this->thread_running) {
    xine_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
    this->sample_counter = 0;

    this->thread_running = 1;
  }

  return this->status;
}

static void demux_eawve_dispose(demux_eawve_t *this)
{
  free(this);
}

static int demux_eawve_get_status(demux_eawve_t *this)
{
  return this->status;
}

static int demux_eawve_get_stream_length(demux_eawve_t *this)
{
  return this->num_samples / 22050;
}

static demux_plugin_t* open_plugin (demux_class_t *class_gen, xine_stream_t *stream, input_plugin_t *input_gen)
{

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_eawve_t    *this;

  this         = xine_xmalloc (sizeof (demux_eawve_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = (void*)demux_eawve_send_headers;
  this->demux_plugin.send_chunk        = (void*)demux_eawve_send_chunk;
  this->demux_plugin.seek              = (void*)demux_eawve_seek;
  this->demux_plugin.dispose           = (void*)demux_eawve_dispose;
  this->demux_plugin.get_status        = (void*)demux_eawve_get_status;
  this->demux_plugin.get_stream_length = (void*)demux_eawve_get_stream_length;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:
    if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) == 0) {
      return NULL;
    }

    if (!process_header(this)) {
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
    if (strncasecmp (ending, ".wve", 4)) {
      free (this);
      return NULL;
    }

    if (!process_header(this)) {
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

static char *get_description (demux_class_t *this_gen)
{
  return "Electronics Arts WVE format demux plugin";
}

static char *get_identifier (demux_class_t *this_gen)
{
  return "EA WVE";
}

static char *get_extensions (demux_class_t *this_gen)
{
  return "wve";
}

static char *get_mimetypes (demux_class_t *this_gen)
{
  return NULL;
}

static void class_dispose (demux_class_t *this)
{
  free (this);
}

static void *init_plugin (xine_t *xine, void *data)
{
  demux_eawve_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_eawve_class_t));
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
  { PLUGIN_DEMUX, 15, "wve", XINE_VERSION_CODE, NULL, (void*)init_plugin},
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
