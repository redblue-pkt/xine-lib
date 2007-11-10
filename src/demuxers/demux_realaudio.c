/*
 * Copyright (C) 2001-2003 the xine project
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
 * RealAudio File Demuxer by Mike Melanson (melanson@pcisys.net)
 *     improved by James Stembridge (jstembridge@users.sourceforge.net)
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
#include "group_audio.h"

#define RA_FILE_HEADER_PREV_SIZE 22

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  unsigned int         fourcc;
  unsigned int         audio_type;

  unsigned short       block_align;

  uint8_t              seek_flag:1; /* this is set when a seek just occurred */

  off_t                data_start;
  off_t                data_size;
  
  unsigned char       *header;
  unsigned int         header_size;
} demux_ra_t;

typedef struct {
  demux_class_t     demux_class;
} demux_ra_class_t;

/* returns 1 if the RealAudio file was opened successfully, 0 otherwise */
static int open_ra_file(demux_ra_t *this) {
  unsigned char   file_header[RA_FILE_HEADER_PREV_SIZE], len;
  unsigned short  version;
  off_t           offset;
  

  /* check the signature */
  if (_x_demux_read_header(this->input, file_header, RA_FILE_HEADER_PREV_SIZE) !=
      RA_FILE_HEADER_PREV_SIZE)
    return 0;

  if ((file_header[0] != '.') ||
      (file_header[1] != 'r') ||
      (file_header[2] != 'a'))
    return 0;

  /* read version */
  version = _X_BE_16(&file_header[0x04]);
  
  /* read header size according to version */
  if (version == 3)
    this->header_size = _X_BE_16(&file_header[0x06]) + 8;
  else if (version == 4)
    this->header_size = _X_BE_32(&file_header[0x12]) + 16;
  else {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_realaudio: unknown version number %d\n", version);
    return 0;
  }
    
  /* allocate for and read header data */
  this->header = xine_xmalloc(this->header_size);
  
  if (_x_demux_read_header(this->input, this->header, this->header_size) != this->header_size) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_realaudio: unable to read header\n");
    free(this->header);
    return 0;
  }
    
  /* read header data according to version */
  if((version == 3) && (this->header_size >= 32)) {
    this->data_size = _X_BE_32(&this->header[0x12]);
    
    this->block_align = 240;
    
    offset = 0x16;
  } else if(this->header_size >= 72) {
    this->data_size = _X_BE_32(&this->header[0x1C]);    
    
    this->block_align = _X_BE_16(&this->header[0x2A]);
    
    if(this->header[0x3D] == 4)
      this->fourcc = _X_ME_32(&this->header[0x3E]);
    else {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, 
	      "demux_realaudio: invalid fourcc size %d\n", this->header[0x3D]);
      free(this->header);
      return 0;
    }
    
    offset = 0x45;
  } else {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_realaudio: header too small\n");
    free(this->header);
    return 0;
  }
  
  /* Read title */
  len = this->header[offset];
  if(len && ((offset+len+2) < this->header_size)) {
    _x_meta_info_n_set(this->stream, XINE_META_INFO_TITLE,
                        &this->header[offset+1], len);
    offset += len+1;
  } else
    offset++;
  
  /* Author */
  len = this->header[offset];
  if(len && ((offset+len+1) < this->header_size)) {
    _x_meta_info_n_set(this->stream, XINE_META_INFO_ARTIST,
                        &this->header[offset+1], len);
    offset += len+1;
  } else
    offset++;
  
  /* Copyright/Date */
  len = this->header[offset];
  if(len && ((offset+len) <= this->header_size)) {
    _x_meta_info_n_set(this->stream, XINE_META_INFO_YEAR,
                        &this->header[offset+1], len);
    offset += len+1;
  } else
    offset++;
  
  /* Fourcc for version 3 comes after meta info */
  if((version == 3) && ((offset+7) <= this->header_size)) {
    if(this->header[offset+2] == 4)
      this->fourcc = _X_ME_32(&this->header[offset+3]);
    else {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, 
	      "demux_realaudio: invalid fourcc size %d\n", this->header[offset+2]);
      free(this->header);
      return 0;
    }
  }
  
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC, this->fourcc);
  this->audio_type = _x_formattag_to_buf_audio(this->fourcc);

  /* seek to start of data */
  this->data_start = this->header_size;
  if (this->input->seek(this->input, this->data_start, SEEK_SET) !=
      this->data_start) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "demux_realaudio: unable to seek to data start\n");
    return 0;
  }

  if( !this->audio_type )
    this->audio_type = BUF_AUDIO_UNKNOWN;

  return 1;
}

static int demux_ra_send_chunk(demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;

  off_t current_normpos = 0;
  int64_t current_pts;

  /* just load data chunks from wherever the stream happens to be
   * pointing; issue a DEMUX_FINISHED status if EOF is reached */
  if( this->input->get_length (this->input) )
    current_normpos = (int)( (double) (this->input->get_current_pos (this->input) - this->data_start) * 
                      65535 / this->data_size );

  current_pts = 0;  /* let the engine sort out the pts for now */

  if (this->seek_flag) {
    _x_demux_control_newpts(this->stream, current_pts, BUF_FLAG_SEEK);
    this->seek_flag = 0;
  }

  if(_x_demux_read_send_data(this->audio_fifo, this->input, this->block_align, 
                             current_pts, this->audio_type, 0, current_normpos, 
                             current_pts / 90, 0, 0) < 0) {
    this->status = DEMUX_FINISHED;                           
  }
  
  return this->status;
}

static void demux_ra_send_headers(demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_FOURCC, this->fourcc);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_FRAME_END;
    
    if(this->header_size > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = this->header_size;
    
    memcpy(buf->content, this->header, buf->size);

    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_ra_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_ra_t *this = (demux_ra_t *) this_gen;
  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  _x_demux_flush_engine (this->stream);

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  /* check the boundary offsets */
  if (start_pos <= 0)
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
    start_pos /= this->block_align;
    start_pos *= this->block_align;
    start_pos += this->data_start;

    this->input->seek(this->input, start_pos, SEEK_SET);
  }

  return this->status;
}


static void demux_ra_dispose (demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;
  
  if(this->header)
    free(this->header);

  free(this);
}

static int demux_ra_get_status (demux_plugin_t *this_gen) {
  demux_ra_t *this = (demux_ra_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_ra_get_stream_length (demux_plugin_t *this_gen) {
  return 0;
}

static uint32_t demux_ra_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_ra_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_ra_t     *this;

  this         = xine_xmalloc (sizeof (demux_ra_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_ra_send_headers;
  this->demux_plugin.send_chunk        = demux_ra_send_chunk;
  this->demux_plugin.seek              = demux_ra_seek;
  this->demux_plugin.dispose           = demux_ra_dispose;
  this->demux_plugin.get_status        = demux_ra_get_status;
  this->demux_plugin.get_stream_length = demux_ra_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_ra_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ra_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_EXTENSION: {
    const char *extensions, *mrl;

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

    if (!open_ra_file(this)) {
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

static const char *get_description (demux_class_t *this_gen) {
  return "RealAudio file demux plugin";
}

static const char *get_identifier (demux_class_t *this_gen) {
  return "RA";
}

static const char *get_extensions (demux_class_t *this_gen) {
  return "ra";
}

static const char *get_mimetypes (demux_class_t *this_gen) {
  return "audio/x-realaudio: ra: RealAudio File;";
}

static void class_dispose (demux_class_t *this_gen) {
  demux_ra_class_t *this = (demux_ra_class_t *) this_gen;

  free (this);
}

void *demux_realaudio_init_plugin (xine_t *xine, void *data) {
  demux_ra_class_t     *this;

  this = xine_xmalloc (sizeof (demux_ra_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}
