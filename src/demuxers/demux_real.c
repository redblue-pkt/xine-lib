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
 * Real Media File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Real file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_real.c,v 1.9 2002/11/18 03:03:09 guenter Exp $
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

#define LOG

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch3) | \
        ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | \
        ( (long)(unsigned char)(ch0) << 24 ) )

#define RMF_TAG   FOURCC_TAG('.', 'R', 'M', 'F')
#define PROP_TAG  FOURCC_TAG('P', 'R', 'O', 'P')
#define MDPR_TAG  FOURCC_TAG('M', 'D', 'P', 'R')
#define CONT_TAG  FOURCC_TAG('C', 'O', 'N', 'T')
#define DATA_TAG  FOURCC_TAG('D', 'A', 'T', 'A')
#define INDX_TAG  FOURCC_TAG('I', 'N', 'D', 'X')

#define PREAMBLE_SIZE 8
#define REAL_SIGNATURE_SIZE 4
#define DATA_CHUNK_HEADER_SIZE 10
#define DATA_PACKET_HEADER_SIZE 12

#define PN_KEYFRAME_FLAG 0x0002

#define MAX_STREAMS 10

typedef struct {
  int            stream;
  int64_t        offset;
  unsigned int   size;
  int64_t        pts;
  int            keyframe;
} real_packet;

typedef struct {
  int             num;
  int32_t         buf_type ;
  fifo_buffer_t  *fifo;
} stream_info_t;

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
  unsigned int         duration;
  int                  packet_count;
  int                  bitrate;

  unsigned int         video_type;
  unsigned int         audio_type;

  unsigned int         video_width;
  unsigned int         video_height;
  unsigned int         audio_channels;
  unsigned int         audio_sample_rate;
  unsigned int         audio_bits;

  int                  num_streams;
  stream_info_t        streams[MAX_STREAMS];

  unsigned int         current_data_chunk_packet_count;
  unsigned int         next_data_chunk_offset;

  char                 last_mrl[1024];
} demux_real_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_real_class_t;

typedef struct {

  u_int16_t  object_version;

  u_int16_t  stream_number;
  u_int32_t  max_bit_rate;
  u_int32_t  avg_bit_rate;
  u_int32_t  max_packet_size;
  u_int32_t  avg_packet_size;
  u_int32_t  start_time;
  u_int32_t  preroll;
  u_int32_t  duration;
  char       stream_name_size;
  char      *stream_name;
  char       mime_type_size;
  char      *mime_type;
  u_int32_t  type_specific_len;
  char      *type_specific_data;

} pnm_mdpr_t;

void hexdump (char *buf, int length) {

  int i;

  printf ("pnm: ascii contents>");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    if ((c >= 32) && (c <= 128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");

  printf ("pnm: complete hexdump of package follows:\npnm:  ");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\npnm: ");

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");
}

static pnm_mdpr_t *pnm_parse_mdpr(const char *data) {

  pnm_mdpr_t *mdpr=malloc(sizeof(pnm_mdpr_t));

  mdpr->object_version=BE_16(&data[0]);

  if (mdpr->object_version != 0) {
    printf("warning: unknown object version in MDPR: 0x%04x\n",
	   mdpr->object_version);
  }

  mdpr->stream_number=BE_16(&data[2]);
  mdpr->max_bit_rate=BE_32(&data[4]);
  mdpr->avg_bit_rate=BE_32(&data[8]);
  mdpr->max_packet_size=BE_32(&data[12]);
  mdpr->avg_packet_size=BE_32(&data[16]);
  mdpr->start_time=BE_32(&data[20]);
  mdpr->preroll=BE_32(&data[24]);
  mdpr->duration=BE_32(&data[28]);
  
  mdpr->stream_name_size=data[32];
  mdpr->stream_name=malloc(sizeof(char)*(mdpr->stream_name_size+1));
  memcpy(mdpr->stream_name, &data[33], mdpr->stream_name_size);
  mdpr->stream_name[mdpr->stream_name_size]=0;
  
  mdpr->mime_type_size=data[33+mdpr->stream_name_size];
  mdpr->mime_type=malloc(sizeof(char)*(mdpr->mime_type_size+1));
  memcpy(mdpr->mime_type, &data[34+mdpr->stream_name_size], mdpr->mime_type_size);
  mdpr->mime_type[mdpr->mime_type_size]=0;
  
  mdpr->type_specific_len=BE_32(&data[34+mdpr->stream_name_size+mdpr->mime_type_size]);
  mdpr->type_specific_data=malloc(sizeof(char)*(mdpr->type_specific_len));
  memcpy(mdpr->type_specific_data, 
      &data[38+mdpr->stream_name_size+mdpr->mime_type_size], mdpr->type_specific_len);
  
  printf("pnm: MDPR: stream number: %i\n", mdpr->stream_number);
  printf("pnm: MDPR: maximal bit rate: %i\n", mdpr->max_bit_rate);
  printf("pnm: MDPR: average bit rate: %i\n", mdpr->avg_bit_rate);
  printf("pnm: MDPR: largest packet size: %i bytes\n", mdpr->max_packet_size);
  printf("pnm: MDPR: average packet size: %i bytes\n", mdpr->avg_packet_size);
  printf("pnm: MDPR: start time: %i\n", mdpr->start_time);
  printf("pnm: MDPR: pre-buffer: %i ms\n", mdpr->preroll);
  printf("pnm: MDPR: duration of stream: %i ms\n", mdpr->duration);
  printf("pnm: MDPR: stream name: %s\n", mdpr->stream_name);
  printf("pnm: MDPR: mime type: %s\n", mdpr->mime_type);
  printf("pnm: MDPR: type specific data:\n");
  hexdump(mdpr->type_specific_data, mdpr->type_specific_len);
  printf("\n");

  return mdpr;
}

typedef struct dp_hdr_s {
  uint32_t chunks;	/* number of chunks             */
  uint32_t timestamp;   /* timestamp from packet header */
  uint32_t len;	        /* length of actual data        */
  uint32_t chunktab;	/* offset to chunk offset array */
} dp_hdr_t;

static void send_real_buf (demux_real_t *this, uint32_t timestamp, int len, 
			   fifo_buffer_t *fifo,
			   uint32_t buf_type, uint32_t decoder_flags) {

  dp_hdr_t      *hdr;
  buf_element_t *buf;

  if (!fifo) {
    this->input->seek (this->input, len, SEEK_CUR);
    return;
  }
  
  buf = fifo->buffer_pool_alloc (fifo);

  buf->content = buf->mem;

  hdr = buf->content;
  hdr->chunks    = 1;
  hdr->timestamp = timestamp;
  hdr->len       = len;
  hdr->chunktab  = 0;

  this->input->read (this->input, buf->content+16, len);

  buf->size = len+16;

  buf->input_pos     = 0 ; /* FIXME */
  buf->input_time    = 0 ; /* FIXME */
  buf->type          = buf_type;
  buf->decoder_flags = decoder_flags;
    
  fifo->put (fifo, buf);  

}


static void real_parse_headers (demux_real_t *this) {

  char           preamble[PREAMBLE_SIZE];
  unsigned int   chunk_type = 0;
  unsigned int   chunk_size;
  unsigned char *chunk_buffer;
  int            field_size;
  int            stream_ptr;
  unsigned char  data_chunk_header[DATA_CHUNK_HEADER_SIZE];
  unsigned char  signature[REAL_SIGNATURE_SIZE];

  this->data_start = 0;
  this->data_size = 0;

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, signature, REAL_SIGNATURE_SIZE) !=
      REAL_SIGNATURE_SIZE) {
    this->status = DEMUX_FINISHED;
    return;
  }

  if (BE_32(signature) != RMF_TAG) {
    this->status = DEMUX_FINISHED;
    return;
  }

  /* skip to the start of the first chunk (the first chunk is 0x12 bytes
   * long) and start traversing */
  this->input->seek(this->input, 0x12, SEEK_SET);

  /* iterate through chunks and gather information until the first DATA
   * chunk is located */
  while (chunk_type != DATA_TAG) {

    if (this->input->read(this->input, preamble, PREAMBLE_SIZE) != 
	PREAMBLE_SIZE) {
      this->status = DEMUX_FINISHED;
      return;
    }
    chunk_type = BE_32(&preamble[0]);
    chunk_size = BE_32(&preamble[4]);

    switch (chunk_type) {

    case PROP_TAG:
    case MDPR_TAG:
    case CONT_TAG:

      chunk_size -= PREAMBLE_SIZE;
      chunk_buffer = xine_xmalloc(chunk_size);
      if (this->input->read(this->input, chunk_buffer, chunk_size) != 
	  chunk_size) {
	this->status = DEMUX_FINISHED;
	return;
      }

      if (chunk_type == PROP_TAG) {

        this->packet_count = BE_32(&chunk_buffer[18]);
        this->duration = BE_32(&chunk_buffer[22]);
        this->data_start = BE_32(&chunk_buffer[34]);

      } else if (chunk_type == MDPR_TAG) {

	pnm_mdpr_t *mdpr;

	mdpr = pnm_parse_mdpr (chunk_buffer);

	this->bitrate = mdpr->avg_bit_rate;
	this->stream->stream_info[XINE_STREAM_INFO_BITRATE] = mdpr->avg_bit_rate;

	/* detect streamtype */

	this->streams[this->num_streams].num = mdpr->stream_number;

	if (!strncmp (mdpr->type_specific_data+4, "VIDORV20", 8)) {

	  buf_element_t *buf;

	  this->streams[this->num_streams].buf_type = BUF_VIDEO_RV20;
	  this->streams[this->num_streams].fifo     = this->video_fifo;

	  printf ("demux_real: RV20 video detected\n");

	  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;

	  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

	  buf->content = buf->mem;

	  memcpy (buf->content, mdpr->type_specific_data, 
		  mdpr->type_specific_len);

	  buf->size = mdpr->type_specific_len;

	  buf->input_pos     = 0 ; 
	  buf->input_time    = 0 ; 
	  buf->type          = BUF_VIDEO_RV20;
	  buf->decoder_flags = BUF_FLAG_HEADER;
    
	  this->video_fifo->put (this->video_fifo, buf);  
	
	} else if (!strncmp (mdpr->type_specific_data+4, "VIDORV30", 8)) {

	  buf_element_t *buf;

	  this->streams[this->num_streams].buf_type = BUF_VIDEO_RV30;
	  this->streams[this->num_streams].fifo     = this->video_fifo;

	  printf ("demux_real: RV30 video detected\n");

	  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 1;

	  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

	  buf->content = buf->mem;

	  memcpy (buf->content, mdpr->type_specific_data, 
		  mdpr->type_specific_len);

	  buf->size = mdpr->type_specific_len;

	  buf->input_pos     = 0 ; 
	  buf->input_time    = 0 ; 
	  buf->type          = BUF_VIDEO_RV30;
	  buf->decoder_flags = BUF_FLAG_HEADER;
    
	  this->video_fifo->put (this->video_fifo, buf);  
	
	} else if ((mdpr->type_specific_len>61) 
		   && (!strncmp (mdpr->type_specific_data+57, "sipr", 4))) {

	  this->streams[this->num_streams].buf_type = BUF_AUDIO_SIPRO;
	  this->streams[this->num_streams].fifo     = this->audio_fifo;

	  printf ("demux_real: sipro audio detected\n");

	  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
 
	} else {

	  printf ("demux_real: codec not recognized\n");

	  this->streams[this->num_streams].buf_type = 0;
	  this->streams[this->num_streams].fifo     = NULL;
	}

	this->num_streams++;
	free (mdpr);

      } else if (chunk_type == CONT_TAG) {

        stream_ptr = 2;

        /* load the title string */
        field_size = BE_16(&chunk_buffer[stream_ptr]);
        stream_ptr += 2;
        this->stream->meta_info[XINE_META_INFO_TITLE] =
          xine_xmalloc(field_size + 1);
        strncpy(this->stream->meta_info[XINE_META_INFO_TITLE],
          &chunk_buffer[stream_ptr], field_size);
        this->stream->meta_info[XINE_META_INFO_TITLE][field_size] = '\0';
        stream_ptr += field_size;

        /* load the author string */
        field_size = BE_16(&chunk_buffer[stream_ptr]);
        stream_ptr += 2;
        this->stream->meta_info[XINE_META_INFO_ARTIST] =
          xine_xmalloc(field_size + 1);
        strncpy(this->stream->meta_info[XINE_META_INFO_ARTIST],
          &chunk_buffer[stream_ptr], field_size);
        this->stream->meta_info[XINE_META_INFO_ARTIST][field_size] = '\0';
        stream_ptr += field_size;

        /* load the copyright string as the year */
        field_size = BE_16(&chunk_buffer[stream_ptr]);
        stream_ptr += 2;
        this->stream->meta_info[XINE_META_INFO_YEAR] =
          xine_xmalloc(field_size + 1);
        strncpy(this->stream->meta_info[XINE_META_INFO_YEAR],
          &chunk_buffer[stream_ptr], field_size);
        this->stream->meta_info[XINE_META_INFO_YEAR][field_size] = '\0';
        stream_ptr += field_size;

        /* load the comment string */
        field_size = BE_16(&chunk_buffer[stream_ptr]);
        stream_ptr += 2;
        this->stream->meta_info[XINE_META_INFO_COMMENT] =
          xine_xmalloc(field_size + 1);
        strncpy(this->stream->meta_info[XINE_META_INFO_COMMENT],
          &chunk_buffer[stream_ptr], field_size);
        this->stream->meta_info[XINE_META_INFO_COMMENT][field_size] = '\0';
        stream_ptr += field_size;
      }

      free(chunk_buffer);
      break;

    case DATA_TAG:
      if (this->input->read(this->input, data_chunk_header, 
			    DATA_CHUNK_HEADER_SIZE) != DATA_CHUNK_HEADER_SIZE) {
	this->status = DEMUX_FINISHED;
        return ;
      }

      this->current_data_chunk_packet_count = BE_32(&data_chunk_header[2]);
      this->next_data_chunk_offset = BE_32(&data_chunk_header[6]);
      break;

    default:
      /* this should not occur, but in case it does, skip the chunk */
      this->input->seek(this->input, chunk_size - PREAMBLE_SIZE, SEEK_CUR);
      break;

    }
  }
}

static int demux_real_send_chunk(demux_plugin_t *this_gen) {

  demux_real_t   *this = (demux_real_t *) this_gen;
  char            preamble[PREAMBLE_SIZE];
  unsigned char   data_chunk_header[DATA_CHUNK_HEADER_SIZE];
  char            header[DATA_PACKET_HEADER_SIZE];
  int             stream_num,i;
  int             stream, size, keyframe;
  uint32_t        timestamp;
  int64_t         pts;
  off_t           offset;

  /* load a header from wherever the stream happens to be pointing */
  if (this->input->read(this->input, header, DATA_PACKET_HEADER_SIZE) !=
      DATA_PACKET_HEADER_SIZE) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* read the packet information */
  stream   = BE_16(&header[4]);
  offset   = this->input->get_current_pos(this->input);
  size     = BE_16(&header[2]) - DATA_PACKET_HEADER_SIZE;
  timestamp= BE_32(&header[6]);
  pts      = (int64_t) timestamp * 90;
  keyframe = header[11] & PN_KEYFRAME_FLAG;

#ifdef LOG
  printf ("demux_real: packet of stream %d, 0x%X bytes @ %llX, pts = %lld%s\n",
	  stream, size, offset, pts, keyframe ? ", keyframe" : "");
#endif

  stream_num = -1;

  for (i=0; i<this->num_streams; i++) {

    if (this->streams[i].num == stream) 
      stream_num = i;
  }

  if (stream_num >= 0) {

    printf ("demux_real: buf type is %08x\n", this->streams[stream_num].buf_type);

    send_real_buf (this, timestamp, size, 
		   this->streams[stream_num].fifo,
		   this->streams[stream_num].buf_type,
		   0 /* FIXME */);
  } else
    this->input->seek(this->input, size, SEEK_CUR);

  this->current_data_chunk_packet_count--;

  /* check if it's time to reload */
  if (!this->current_data_chunk_packet_count && 
    this->next_data_chunk_offset) {

    /* seek to the next DATA chunk offset */
    this->input->seek(this->input, this->next_data_chunk_offset, SEEK_SET);

    /* load the DATA chunk preamble */
    if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
      PREAMBLE_SIZE) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    /* load the rest of the DATA chunk header */
    if (this->input->read(this->input, data_chunk_header, 
      DATA_CHUNK_HEADER_SIZE) != DATA_CHUNK_HEADER_SIZE) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }
#ifdef LOG
    printf ("demux_real: **** found next DATA tag\n");
#endif
    this->current_data_chunk_packet_count = BE_32(&data_chunk_header[2]);
    this->next_data_chunk_offset = BE_32(&data_chunk_header[6]);
  }

  if (!this->current_data_chunk_packet_count) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  return this->status;
}

static void demux_real_send_headers(demux_plugin_t *this_gen) {

  demux_real_t *this = (demux_real_t *) this_gen;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  this->num_streams = 0;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */

  this->input->seek (this->input, 0, SEEK_SET);

  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 0;
  real_parse_headers (this);


  /* print vital stats */
  xine_log (this->stream->xine, XINE_LOG_MSG,
    _("demux_real: Real media file, running time: %d min, %d sec\n"),
    this->duration / 1000 / 60,
    this->duration / 1000 % 60);

  xine_demux_control_headers_done (this->stream);
}

static int demux_real_seek (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {

  demux_real_t *this = (demux_real_t *) this_gen;

  /* if thread is not running, initialize demuxer */
  if( !this->stream->demux_thread_running ) {

    /* send new pts */
/*    xine_demux_control_newpts(this->stream, 0, 0);
*/

    this->status = DEMUX_OK;
  }

  return this->status;
}

static void demux_real_dispose (demux_plugin_t *this_gen) {

  demux_real_t *this = (demux_real_t *) this_gen;

  free(this);
}

static int demux_real_get_status (demux_plugin_t *this_gen) {
  demux_real_t *this = (demux_real_t *) this_gen;

  return this->status;
}

static int demux_real_get_stream_length (demux_plugin_t *this_gen) {

  demux_real_t *this = (demux_real_t *) this_gen;

  /* duration is stored in the file as milliseconds */
  return this->duration / 1000;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_real_t   *this;

  printf ("demux_real: open_plugin\n");

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
    {
      uint8_t buf[4096];

      if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {

	input->seek(input, 0, SEEK_SET);

	if (input->read(input, buf, 4)) {

	  if ((buf[0] != 0x2e)
	      || (buf[1] != 'R')
	      || (buf[2] != 'M')
	      || (buf[3] != 'F')) 
	    return NULL;
	}
      } else if (input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW)) {
	if ((buf[0] != 0x2e)
	    || (buf[1] != 'R')
	    || (buf[2] != 'M')
	    || (buf[3] != 'F')) 
	  return NULL;
      } else
	return NULL;
    }
    

    printf ("demux_real: by content accepted.\n");

  break;

  case METHOD_BY_EXTENSION: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');

    if (!ending) 
      return NULL;

    if (strncasecmp (ending, ".rm", 3)
	&& strncasecmp (ending, ".ra", 3) 
	&& strncasecmp (ending, ".ram", 4)) 
      return NULL;

  }

  break;

  case METHOD_EXPLICIT:
    break;

  default:
    return NULL;
  }


  this         = xine_xmalloc (sizeof (demux_real_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_real_send_headers;
  this->demux_plugin.send_chunk        = demux_real_send_chunk;
  this->demux_plugin.seek              = demux_real_seek;
  this->demux_plugin.dispose           = demux_real_dispose;
  this->demux_plugin.get_status        = demux_real_get_status;
  this->demux_plugin.get_stream_length = demux_real_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.demux_class       = class_gen;

  strncpy (this->last_mrl, input->get_mrl (input), 1024);

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "RealMedia file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "Real";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "rm ra ram";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "audio/x-pn-realaudio: ra, rm, ram: Real Media File;"
         "audio/x-realaudio: ra: Real Media File;"; 
}

static void class_dispose (demux_class_t *this_gen) {

  demux_real_class_t *this = (demux_real_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_real_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_real_class_t));
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
  { PLUGIN_DEMUX, 16, "real", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
