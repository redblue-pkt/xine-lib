/*
 * Copyright (C) 2000, 2001 the xine project
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
 * $Id: demux_asf.c,v 1.9 2001/11/10 13:48:02 guenter Exp $
 *
 * demultiplexer for asf streams
 *
 * based on ffmpeg's 
 * ASF compatible encoder and decoder.
 * Copyright (c) 2000, 2001 Gerard Lantau.
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
#include "monitor.h"
#include "demux.h"
#include "utils.h"

#define	WINE_TYPEDEFS_ONLY
#include "libw32dll/wine/avifmt.h"
#include "libw32dll/wine/windef.h"
#include "libw32dll/wine/vfw.h"
#include "libw32dll/wine/mmreg.h"

#define PACKET_SIZE        3200
#define PACKET_HEADER_SIZE   12
#define FRAME_HEADER_SIZE    17
#define CODEC_TYPE_AUDIO      0
#define CODEC_TYPE_VIDEO      1
#define MAX_NUM_STREAMS      23

typedef struct {
  int               num;
  int               seq;

  int               frag_offset;

  uint32_t          buf_type;
  int               stream_id;
  fifo_buffer_t    *fifo;
} asf_stream_t;

static uint32_t xine_debug;

typedef struct demux_asf_s {
  demux_plugin_t    demux_plugin;

  fifo_buffer_t    *audio_fifo;
  fifo_buffer_t    *video_fifo;

  input_plugin_t   *input;

  int               keyframe_found;

  int               seqno;
  int               packet_size;
  int               packet_flags;
  
  asf_stream_t      streams[MAX_NUM_STREAMS];
  int               num_streams;
  int               num_audio_streams;
  int               num_video_streams;

  uint16_t          wavex[128];
  int               wavex_size;

  uint16_t          bih[128];
  int               bih_size;

  char              title[512];
  char              author[512];
  char              copyright[512];
  char              comment[512];

  uint32_t          length, rate;

  /* packet filling */
  int               packet_size_left;
  
  /* only for reading */
  int               packet_padsize;
  int               nb_frames;
  int               segtype;
  int               frame;
  
  pthread_t         thread;

  int               status;

  int               send_end_buffers;

} demux_asf_t ;


typedef struct {
  uint32_t v1;
  uint16_t v2;
  uint16_t v3;
  uint8_t  v4[8];
} GUID;

static const GUID asf_header = {
  0x75B22630, 0x668E, 0x11CF, { 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C },
};

static const GUID file_header = {
  0x8CABDCA1, 0xA947, 0x11CF, { 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID stream_header = {
  0xB7DC0791, 0xA9B7, 0x11CF, { 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID audio_stream = {
  0xF8699E40, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID audio_conceal_none = {
  0x49f1a440, 0x4ece, 0x11d0, { 0xa3, 0xac, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 },
};

static const GUID video_stream = {
  0xBC19EFC0, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID video_conceal_none = {
  0x20FB5700, 0x5B55, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};


static const GUID comment_header = {
  0x75b22633, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c },
};

static const GUID codec_comment_header = {
  0x86D15240, 0x311D, 0x11D0, { 0xA3, 0xA4, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 },
};
static const GUID codec_comment1_header = {
  0x86d15241, 0x311d, 0x11d0, { 0xa3, 0xa4, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 },
};

static const GUID data_header = {
  0x75b22636, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c },
};

static const GUID index_guid = {
  0x33000890, 0xe5b1, 0x11cf, { 0x89, 0xf4, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xcb },
};

static const GUID head1_guid = {
  0x5fbf03b5, 0xa92e, 0x11cf, { 0x8e, 0xe3, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 },
};

static const GUID head2_guid = {
  0xabd3d211, 0xa9ba, 0x11cf, { 0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 },
};
    
/* I am not a number !!! This GUID is the one found on the PC used to
   generate the stream */
static const GUID my_guid = {
  0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 },
};

static uint8_t get_byte (demux_asf_t *this) {

  uint8_t buf;
  int     i;

  i = this->input->read (this->input, &buf, 1);

  /* printf ("%02x ", buf); */
  
  if (i != 1) {
    printf ("demux_asf: end of data\n");
    this->status = DEMUX_FINISHED;
  }

  return buf;
}

static uint16_t get_le16 (demux_asf_t *this) {

  uint8_t buf[2];
  int     i;

  i = this->input->read (this->input, buf, 2);

  /* printf (" [%02x %02x] ", buf[0], buf[1]); */

  if (i != 2) {
    printf ("demux_asf: end of data\n");
    this->status = DEMUX_FINISHED;
  }

  return buf[0] | (buf[1] << 8);
}

static uint32_t get_le32 (demux_asf_t *this) {

  uint8_t buf[4];
  int     i;

  i = this->input->read (this->input, buf, 4);

  /* printf ("%02x %02x %02x %02x ", buf[0], buf[1], buf[2], buf[3]); */

  if (i != 4) {
    printf ("demux_asf: end of data\n");
    this->status = DEMUX_FINISHED;
  }

  return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

static uint64_t get_le64 (demux_asf_t *this) {

  uint8_t buf[8];
  int     i;

  i = this->input->read (this->input, buf, 8);

  if (i != 8) {
    printf ("demux_asf: end of data\n");
    this->status = DEMUX_FINISHED;
  }

  return (uint64_t) buf[0] 
    | ((uint64_t) buf[1] << 8)
    | ((uint64_t) buf[2] << 16)
    | ((uint64_t) buf[3] << 24)
    | ((uint64_t) buf[4] << 32)
    | ((uint64_t) buf[5] << 40)
    | ((uint64_t) buf[6] << 48)
    | ((uint64_t) buf[7] << 54) ;
}

static void get_guid (demux_asf_t *this, GUID *g) {
  int i; 

  g->v1 = get_le32(this);
  g->v2 = get_le16(this);
  g->v3 = get_le16(this);
  for(i=0;i<8;i++)
    g->v4[i] = get_byte(this);

}

static void get_str16_nolen(demux_asf_t *this, int len, 
			    char *buf, int buf_size) {

  int c;
  char *q;

  q = buf;
  while (len > 0) {
    c = get_le16(this);
    if ((q - buf) < buf_size - 1)
      *q++ = c;
    len-=2;
  }
  *q = '\0';
}

static void asf_send_audio_header (demux_asf_t *this, int stream_id) {

  buf_element_t *buf;
  WAVEFORMATEX  *wavex = (WAVEFORMATEX *) this->wavex ;

  if (!this->audio_fifo)
    return;

  this->streams[this->num_streams].buf_type = 
    formattag_to_buf_audio ( wavex->wFormatTag );
    
  if ( !this->streams[this->num_streams].buf_type ) {
    printf ("demux_asf: unknown audio type 0x%x\n", wavex->wFormatTag);
    this->streams[this->num_streams].buf_type     = BUF_CONTROL_NOP;
  }
  else
    printf ("demux_asf: audio format : %s (wFormatTag 0x%x)\n", 
	    buf_audio_name(this->streams[this->num_streams].buf_type),
	    wavex->wFormatTag);

  this->streams[this->num_streams].buf_type   |= this->num_audio_streams;
  this->streams[this->num_streams].fifo        = this->audio_fifo;
  this->streams[this->num_streams].stream_id   = stream_id;
  this->streams[this->num_streams].frag_offset = 0;
  this->streams[this->num_streams].seq         = 0;

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->content = buf->mem;
  memcpy (buf->content, this->wavex, this->wavex_size);

  printf ("demux_asf: wavex header is %d bytes long\n", this->wavex_size);

  buf->size = this->wavex_size;
  buf->type = this->streams[this->num_streams].buf_type;
  buf->decoder_info[0] = 0; /* first package, containing wavex */
  buf->decoder_info[1] = wavex->nSamplesPerSec;
  buf->decoder_info[2] = wavex->wBitsPerSample;
  buf->decoder_info[3] = wavex->nChannels;
  this->audio_fifo->put (this->audio_fifo, buf);

  this->num_streams++;
  this->num_audio_streams++;
}

static unsigned long str2ulong(unsigned char *str) {
  return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

static void asf_send_video_header (demux_asf_t *this, int stream_id) {

  buf_element_t    *buf;
  BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *) this->bih;

  this->streams[this->num_streams].buf_type = 
    fourcc_to_buf_video((void*)&bih->biCompression);
    
  if( !this->streams[this->num_streams].buf_type ) {
    printf ("demux_asf: unknown video format %.4s\n",
	    (char*)&bih->biCompression);
    
    this->status = DEMUX_FINISHED;
    return;
  }
  
  this->streams[this->num_streams].buf_type    |= this->num_video_streams;
  this->streams[this->num_streams].fifo         = this->video_fifo;
  this->streams[this->num_streams].stream_id    = stream_id;
  this->streams[this->num_streams].frag_offset  = 0;

  /* 
  printf ("demux_asf: video format : %.4s\n", (char*)&bih->biCompression);
  */
  printf ("demux_asf: video format : %s\n", 
           buf_video_name(this->streams[this->num_streams].buf_type));

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->content = buf->mem;
  buf->decoder_info[0] = 0; /* first package, containing bih */
  buf->decoder_info[1] = 3000; /* FIXME ? */
  memcpy (buf->content, &this->bih, this->bih_size);
  buf->size = this->bih_size;
  buf->type = this->streams[this->num_streams].buf_type ;
  this->video_fifo->put (this->video_fifo, buf);

  this->num_streams++;
  this->num_video_streams++;
  
}

static int asf_read_header (demux_asf_t *this) {

  GUID           g;
  uint64_t       gsize;

  get_guid(this, &g);
  if (memcmp(&g, &asf_header, sizeof(GUID))) {
    printf ("demux_asf: file doesn't start with an asf header\n");
    return 0;
  }
  get_le64(this);
  get_le32(this);
  get_byte(this);
  get_byte(this);

  for(;;) {
    get_guid(this, &g);

    gsize = get_le64(this);

    if (gsize < 24)
      goto fail;

    if (!memcmp(&g, &file_header, sizeof(GUID))) {

      uint64_t start_time, end_time;

      get_guid(this, &g);
      get_le64(this); /* file size */
      get_le64(this); /* file time */
      get_le64(this); /* nb_packets */

      end_time =  get_le64 (this); 
      
      this->length = get_le64(this) / 10000000; 
      if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {
	this->rate = this->input->get_length (this->input) / this->length;
      } else
	this->rate = 0;

      printf ("demux_asf: stream length is %d sec, rate is %d bytes/sec\n",
	      this->length, this->rate);

      start_time = get_le32(this); /* start timestamp in 1/1000 s*/

      get_le32(this);
      get_le32(this);
      this->packet_size = get_le32(this);
      get_le32(this);
      get_le32(this);

    } else if (!memcmp(&g, &stream_header, sizeof(GUID))) {

      int           type, total_size, stream_id;
      uint64_t      pos1, pos2;
            
      pos1 = this->input->get_current_pos (this->input);

      get_guid(this, &g);
      if (!memcmp(&g, &audio_stream, sizeof(GUID))) {
	type = CODEC_TYPE_AUDIO;
      } else if (!memcmp(&g, &video_stream, sizeof(GUID))) {
	type = CODEC_TYPE_VIDEO;
      } else {
	goto fail;
      }
      get_guid(this, &g);
      get_le64(this);
      total_size = get_le32(this);
      get_le32(this);
      stream_id = get_le16(this); /* stream id */
      get_le32(this);

      if (type == CODEC_TYPE_AUDIO) {

	this->input->read (this->input, (uint8_t *) this->wavex, total_size);

	printf ("total size: %d bytes\n", total_size);

	/*
	this->input->read (this->input, (uint8_t *) &this->wavex[9], this->wavex[8]);
	*/

	this->wavex_size = total_size; /* 18 + this->wavex[8]; */

	asf_send_audio_header (this, stream_id);

      } else {

	get_le32(this); /* width */
	get_le32(this); /* height */
	get_byte(this);
	this->bih_size = get_le16(this); /* size */

	this->input->read (this->input, (uint8_t *) this->bih, this->bih_size);

	asf_send_video_header (this, stream_id);
      }
      pos2 = this->input->get_current_pos (this->input);
      this->input->seek (this->input, gsize - (pos2 - pos1 + 24), SEEK_CUR);

    } else if (!memcmp(&g, &data_header, sizeof(GUID))) {
      break;

    } else if (!memcmp(&g, &comment_header, sizeof(GUID))) {
      int len1, len2, len3, len4, len5;

      len1 = get_le16(this);
      len2 = get_le16(this);
      len3 = get_le16(this);
      len4 = get_le16(this);
      len5 = get_le16(this);
      get_str16_nolen(this, len1, this->title, sizeof(this->title));
      get_str16_nolen(this, len2, this->author, sizeof(this->author));
      get_str16_nolen(this, len3, this->copyright, sizeof(this->copyright));
      get_str16_nolen(this, len4, this->comment, sizeof(this->comment));
      this->input->seek (this->input, len5, SEEK_CUR);
      /*
        } else if (url_feof(this)) {
	goto fail;
      */
    } else {
      this->input->seek (this->input, gsize - 24, SEEK_CUR);
    }
  }
  get_guid(this, &g);
  get_le64(this);
  get_byte(this);
  get_byte(this);

  this->packet_size_left = 0;

  return 1;

 fail:
  return 0;
}


static int asf_get_packet(demux_asf_t *this) {

  int timestamp, hdr_size;

  uint32_t sig = 0;

  hdr_size = 11;

  while ( (this->status == DEMUX_OK) && (sig != 0x820000) ) {
    sig = ((sig << 8) | get_byte(this)) & 0xFFFFFF;
  }

  this->packet_flags = get_byte(this);
  this->segtype = get_byte(this);
  this->packet_padsize = 0;
  
  if (this->packet_flags & 0x40) {
    get_le16(this);
    printf("demux_asf: absolute size ignored\n");
    hdr_size += 2;
  }
      
  if (this->packet_flags & 0x10) {
    this->packet_padsize = get_le16(this);
    hdr_size += 2;
  } else if (this->packet_flags & 0x08) {
    this->packet_padsize = get_byte(this);
    hdr_size++;
  }
  timestamp = get_le32(this);
  get_le16(this); /* duration */
  if (this->packet_flags & 0x01) {
    this->nb_frames = get_byte(this); /* nb_frames */
    hdr_size++;
  }
  else
    this->nb_frames = 1;
  this->frame = 0;
    
  this->packet_size_left = this->packet_size - hdr_size;

  /*
  printf ("demux_asf: new packet, size = %d, flags = 0x%02x, padsize = %d\n",
	  this->packet_size_left, this->packet_flags, this->packet_padsize);
  */

  return 1;
}

static void hexdump (unsigned char *data, int len) {
  int i;

  for (i=0; i<len; i++)
    printf ("%02x ", data[i]);
  printf ("\n");

}

static void asf_send_buffer (demux_asf_t *this, asf_stream_t *stream, 
			     int frag_offset, int seq, int timestamp, 
			     int frag_len, int payload_size) {

  buf_element_t *buf;

  if (stream->frag_offset == 0) {
    /* new packet */
    stream->seq = seq;
  } else {
    if (seq == stream->seq && 
	frag_offset == stream->frag_offset) {
      /* continuing packet */
    } else {
      /* cannot continue current packet: free it */
      stream->frag_offset = 0;
      if (frag_offset != 0) {
	/* cannot create new packet */
	this->input->seek (this->input, frag_len, SEEK_CUR);
	return ;
      } else {
	/* create new packet */
	stream->seq = seq;
      }
    }
  }
  
  buf = stream->fifo->buffer_pool_alloc (stream->fifo);
  buf->content = buf->mem;
  this->input->read (this->input, buf->content, frag_len);

  /*
  printf ("demux_asf: read %d bytes :", frag_len);
  hexdump (buf->content, frag_len);
  */
  if( frag_len > stream->fifo->buffer_pool_buf_size )
    printf("demux_asf: fragment may be larger than fifo buffer (frag_len=%d)\n", frag_len );
  
  stream->frag_offset += frag_len;

  if (stream->fifo == this->video_fifo) {
    buf->input_pos  = this->input->get_current_pos (this->input);
    buf->input_time = buf->input_pos / this->rate;
  } else {
    buf->input_pos  = 0 ;
    buf->input_time = 0 ;
  }
  buf->PTS        = timestamp * 90;
  buf->SCR        = timestamp * 90;
  buf->type       = stream->buf_type;
  buf->size       = frag_len;
  
  /* test if whole packet read */
  
  if (stream->frag_offset == payload_size) {
    buf->decoder_info[0] = 2;
    stream->frag_offset = 0;
  } else 
    buf->decoder_info[0] = 1;
  
  stream->fifo->put (stream->fifo, buf);
}

static void asf_read_packet(demux_asf_t *this) {

  int            raw_id, stream_id, seq, frag_offset, payload_size, frag_len;
  int            timestamp, flags, i;
  asf_stream_t  *stream;

  if ((this->packet_size_left < FRAME_HEADER_SIZE) ||
      (this->packet_size_left <= this->packet_padsize) ||
      (++this->frame == (this->nb_frames & 0x3f)) ) {
    /* fail safe */

    /* printf ("demux_asf: reading new packet, packet size left %d\n", this->packet_size_left);
    */
    if (this->packet_size_left)
      this->input->seek (this->input, this->packet_size_left, SEEK_CUR);
    
    if (!asf_get_packet(this)) {
      printf ("demux_asf: get_packet failed\n");
      this->status = DEMUX_FINISHED;
      return ;
    }
  }
  
  /* read segment header, find stream */

  raw_id     = get_byte(this);
  stream_id  = raw_id & 0x7f;

  stream    = NULL;
  if ( (raw_id & 0x80) || this->keyframe_found ) {
    for (i=0; i<this->num_streams; i++)
      if (this->streams[i].stream_id == stream_id)
	stream = &this->streams[i];
    this->keyframe_found = 1;
  }

  seq           = get_byte(this);
  switch (this->segtype){
  case 0x55:
    frag_offset = get_byte(this);
    this->packet_size_left -= 1;
    break;
  case 0x59:
    frag_offset = get_le16(this);
    this->packet_size_left -= 2;
    break;
  case 0x5D:
    frag_offset = get_le32(this);
    this->packet_size_left -= 4;
    break;
  default:
    printf("demux_asf: unknow segtype %d\n",this->segtype);
    frag_offset = get_le32(this);
    this->packet_size_left -= 4;
    break;
  }
  flags         = get_byte(this); 

  /*
  printf ("\ndemux_asf: segment header, stream id %02x, frag_offset %d, flags : %02x\n", 
	  stream_id, frag_offset, flags);
  */

  if (flags == 1) {
    int data_length, data_sent=0;

    timestamp = frag_offset;
    get_byte (this);


    if (this->packet_flags & 0x01) {
      if( (this->nb_frames & 0xc0) == 0x40 ) {
        data_length = get_byte (this);
	this->packet_size_left --;
      } else {
        data_length = get_le16 (this);
	this->packet_size_left -= 2;
      }
      this->packet_size_left -= data_length + 4;
      
      /*
      printf ("demux_asf: reading grouping part segment, size = %d\n",
	      data_length);
      */

    } else {

      data_length = this->packet_size_left - 4 - this->packet_padsize; 
      this->packet_size_left = this->packet_padsize;

      /*
      printf ("demux_asf: reading grouping single segment, size = %d\n", data_length); 
      */
    }

    while (data_sent < data_length) {
      int object_length = get_byte(this);

      /*
      printf ("demux_asf: sending grouped object, len = %d\n", object_length);
      */

      if (stream)
	asf_send_buffer (this, stream, 0, seq, timestamp, 
			 object_length, object_length);
      else {
	printf ("demux_asf: unhandled stream type, id %d\n", stream_id);
	this->input->seek (this->input, object_length, SEEK_CUR);
      }

      seq++;
      data_sent += object_length+1;
      timestamp = 0;

    }

  } else {

    payload_size  = get_le32(this);
    timestamp     = get_le32(this);
    if (this->packet_flags & 0x01) {
      frag_len      = get_le16(this);
      this->packet_size_left -= FRAME_HEADER_SIZE + frag_len - 4;
      /*
      printf ("demux_asf: reading part segment, size = %d\n",
	      frag_len);
      */

    } else {
      frag_len = this->packet_size_left - 11 - this->packet_padsize; 
      this->packet_size_left = this->packet_padsize;
      
      /*
      printf ("demux_asf: reading single segment, size = %d\n", frag_len); 
      */

    }

    
    if (stream) {

      asf_send_buffer (this, stream, frag_offset, seq, timestamp, frag_len, payload_size);
      
    } else {
      printf ("demux_asf: unhandled stream type, id %d\n", stream_id);
      this->input->seek (this->input, frag_len, SEEK_CUR);
    }  
  }
}
  
/* 
 * xine specific functions start here
 */

static void *demux_asf_loop (void *this_gen) {
  buf_element_t *buf;

  demux_asf_t *this = (demux_asf_t *) this_gen;

  /* printf ("demux_asf: demux loop starting...\n"); */

  this->send_end_buffers = 1;

  while (this->status == DEMUX_OK) {

    asf_read_packet (this);

  }

  /*
    printf ("demux_asf: demux loop finished (status: %d)\n",
    this->status);
  */

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_info[0] = 0; /* stream finished */
    this->video_fifo->put (this->video_fifo, buf);
    
    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type            = BUF_CONTROL_END;
      buf->decoder_info[0] = 0; /* stream finished */
      this->audio_fifo->put (this->audio_fifo, buf);
    }

  }

  pthread_exit(NULL);

  return NULL;
}

static void demux_asf_close (demux_plugin_t *this_gen) {

  demux_asf_t *this = (demux_asf_t *) this_gen;
  free (this);
  
}

static void demux_asf_stop (demux_plugin_t *this_gen) {

  demux_asf_t *this = (demux_asf_t *) this_gen;
  buf_element_t *buf;
  void *p;

  if (this->status != DEMUX_OK) {
    printf ("demux_asf: stop...ignored\n");
    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_cancel (this->thread);
  pthread_join (this->thread, &p);

  this->video_fifo->clear(this->video_fifo);
  if (this->audio_fifo)
    this->audio_fifo->clear(this->audio_fifo);

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type            = BUF_CONTROL_END;
  buf->decoder_info[0] = 1; /* forced */

  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_info[0] = 1; /* forced */
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_asf_get_status (demux_plugin_t *this_gen) {
  demux_asf_t *this = (demux_asf_t *) this_gen;

  return this->status;
}

static void demux_asf_start (demux_plugin_t *this_gen,
			     fifo_buffer_t *video_fifo, 
			     fifo_buffer_t *audio_fifo,
			     off_t start_pos, int start_time) {

  demux_asf_t *this = (demux_asf_t *) this_gen;
  buf_element_t *buf;
  int err;

  this->video_fifo  = video_fifo;
  this->audio_fifo  = audio_fifo;

  /* 
   * send start buffer
   */

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type    = BUF_CONTROL_START;
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type    = BUF_CONTROL_START;
    this->audio_fifo->put (this->audio_fifo, buf);
  }

  /*
   * initialize asf engine
   */

  this->num_streams              = 0;
  this->num_audio_streams        = 0;
  this->num_video_streams        = 0;
  this->packet_size              = 0;
  this->seqno                    = 0;

  
  if (this->input->get_capabilities (this->input) & INPUT_CAP_SEEKABLE)
    this->input->seek (this->input, 0, SEEK_SET);

  if (!asf_read_header (this)) {
    
    this->status = DEMUX_FINISHED;
    return;
  } 

  printf ("demux_asf: title        : %s\n", this->title);
  printf ("demux_asf: author       : %s\n", this->author);
  printf ("demux_asf: copyright    : %s\n", this->copyright);
  printf ("demux_asf: comment      : %s\n", this->comment);

  /*
   * seek to start position
   */

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {

    off_t cur_pos = this->input->get_current_pos (this->input);
    
    if ( (!start_pos) && (start_time))
      start_pos = start_time * this->rate;

    if (start_pos<cur_pos)
      start_pos = cur_pos;

    this->input->seek (this->input, start_pos, SEEK_SET);
  }

  /*
   * now start demuxing
   */

  this->keyframe_found = 0;
  this->status = DEMUX_OK;

  if ((err = pthread_create (&this->thread,
			     NULL, demux_asf_loop, this)) != 0) {
    fprintf (stderr, "demux_asf: can't create new thread (%s)\n",
	     strerror(err));
    exit (1);
  }
}

static int demux_asf_open(demux_plugin_t *this_gen,
			  input_plugin_t *input, int stage) {

  demux_asf_t *this = (demux_asf_t *) this_gen;

  switch(stage) {

  case STAGE_BY_CONTENT:
    return DEMUX_CANNOT_HANDLE;
    break;

  case STAGE_BY_EXTENSION: {
    char *ending;
    char *MRL;
    
    MRL = input->get_mrl (input);
    
    /*
     * check ending
     */
    
    ending = strrchr(MRL, '.');
    
    if(!ending)
      return DEMUX_CANNOT_HANDLE;
    
    if(!strcasecmp(ending, ".asf")) {
      this->input = input;
      return DEMUX_CAN_HANDLE;
    } else if(!strcasecmp(ending, ".wmv")) {
      this->input = input;
      return DEMUX_CAN_HANDLE;
    }
  }
  break;
  }

  return DEMUX_CANNOT_HANDLE;
}

static char *demux_asf_get_id(void) {
  return "ASF";
}

static char *demux_asf_get_mimetypes(void) {
  return "video/x-ms-asf: asf: ASF animation;"
         "video/x-ms-wmv: wmv: WMV animation;";
}

static int demux_asf_get_stream_length (demux_plugin_t *this_gen) {

  demux_asf_t *this = (demux_asf_t *) this_gen;

  return this->length;
}

demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {

  demux_asf_t     *this;
  config_values_t *config;

  if (iface != 6) {
    printf( "demux_asf: plugin doesn't support plugin API version %d.\n"
	    "demux_asf: this means there's a version mismatch between xine and this "
	    "demux_asf: demuxer plugin.\nInstalling current demux plugins should help.\n",
	    iface);
    return NULL;
  }

  this        = xmalloc (sizeof (demux_asf_t));
  config      = xine->config;
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_asf_open;
  this->demux_plugin.start             = demux_asf_start;
  this->demux_plugin.stop              = demux_asf_stop;
  this->demux_plugin.close             = demux_asf_close;
  this->demux_plugin.get_status        = demux_asf_get_status;
  this->demux_plugin.get_identifier    = demux_asf_get_id;
  this->demux_plugin.get_stream_length = demux_asf_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_asf_get_mimetypes;
  
  return (demux_plugin_t *) this;
}
