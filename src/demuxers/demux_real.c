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
 * $Id: demux_real.c,v 1.15 2002/11/22 23:37:04 guenter Exp $
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

  int                  video_stream_num;
  uint32_t             video_buf_type;
  int                  audio_stream_num;
  uint32_t             audio_buf_type;

  unsigned int         current_data_chunk_packet_count;
  unsigned int         next_data_chunk_offset;

  char                 last_mrl[1024];

  int                  old_seqnum;
} demux_real_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_real_class_t;

typedef struct {

  uint16_t  object_version;

  uint16_t  stream_number;
  uint32_t  max_bit_rate;
  uint32_t  avg_bit_rate;
  uint32_t  max_packet_size;
  uint32_t  avg_packet_size;
  uint32_t  start_time;
  uint32_t  preroll;
  uint32_t  duration;
  char       stream_name_size;
  char      *stream_name;
  char       mime_type_size;
  char      *mime_type;
  uint32_t  type_specific_len;
  char      *type_specific_data;

} pnm_mdpr_t;

static void hexdump (char *buf, int length) {

  int i;

  printf ("demux_real: ascii contents>");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    if ((c >= 32) && (c <= 128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");

  printf ("demux_real: complete hexdump of package follows:\ndemux_real 0x0000:  ");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\ndemux_real 0x%04x: ", i);

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
  mdpr->stream_name[(int)mdpr->stream_name_size]=0;
  
  mdpr->mime_type_size=data[33+mdpr->stream_name_size];
  mdpr->mime_type=malloc(sizeof(char)*(mdpr->mime_type_size+1));
  memcpy(mdpr->mime_type, &data[34+mdpr->stream_name_size], mdpr->mime_type_size);
  mdpr->mime_type[(int)mdpr->mime_type_size]=0;
  
  mdpr->type_specific_len=BE_32(&data[34+mdpr->stream_name_size+mdpr->mime_type_size]);
  mdpr->type_specific_data=malloc(sizeof(char)*(mdpr->type_specific_len));
  memcpy(mdpr->type_specific_data, 
      &data[38+mdpr->stream_name_size+mdpr->mime_type_size], mdpr->type_specific_len);
  
  printf("demux_real: MDPR: stream number: %i\n", mdpr->stream_number);
  printf("demux_real: MDPR: maximal bit rate: %i\n", mdpr->max_bit_rate);
  printf("demux_real: MDPR: average bit rate: %i\n", mdpr->avg_bit_rate);
  printf("demux_real: MDPR: largest packet size: %i bytes\n", mdpr->max_packet_size);
  printf("demux_real: MDPR: average packet size: %i bytes\n", mdpr->avg_packet_size);
  printf("demux_real: MDPR: start time: %i\n", mdpr->start_time);
  printf("demux_real: MDPR: pre-buffer: %i ms\n", mdpr->preroll);
  printf("demux_real: MDPR: duration of stream: %i ms\n", mdpr->duration);
  printf("demux_real: MDPR: stream name: %s\n", mdpr->stream_name);
  printf("demux_real: MDPR: mime type: %s\n", mdpr->mime_type);
  printf("demux_real: MDPR: type specific data:\n");
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

#if 0
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

  hdr = (dp_hdr_t *) buf->content;
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
#endif

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

  this->input->seek (this->input, 0, SEEK_SET);
  if (this->input->read(this->input, signature, REAL_SIGNATURE_SIZE) !=
      REAL_SIGNATURE_SIZE) {

    printf ("demux_real: signature not read\n");

    this->status = DEMUX_FINISHED;
    return;
  }

  if (BE_32(signature) != RMF_TAG) {
    this->status = DEMUX_FINISHED;
    printf ("demux_real: signature not found '%.4s'\n", 
	    signature);
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

	printf ("demux_real: parsing type specific data...\n");
	
	/* skip unknown stuff - FIXME: find a better/cleaner way */
	{ 
	  int off;

	  off = 0;

	  while (off<=(mdpr->type_specific_len-8)) {

	    printf ("demux_real: got %.4s\n", mdpr->type_specific_data+off);

	    if (!strncmp (mdpr->type_specific_data+off, ".ra", 3)) {
	      int version;
	      char fourcc[5];

	      off += 4;

	      printf ("demux_real: audio detected %.3s\n", 
		      mdpr->type_specific_data+off+4);

	      version = BE_16 (mdpr->type_specific_data+off);

	      printf ("demux_real: audio version %d\n", version);

	      if (version==4) {
		int str_len;
		int sample_rate;

		sample_rate = BE_16(mdpr->type_specific_data+off+44);

		printf ("demux_real: sample_rate %d\n", sample_rate);

		str_len = *(mdpr->type_specific_data+off+50);

		printf ("demux_real: str_len = %d\n", str_len);

		memcpy (fourcc, mdpr->type_specific_data+off+53+str_len, 4);
		fourcc[4]=0;

		printf ("demux_real: fourcc == %s\n", fourcc);

	      } else if (version == 5) {

		memcpy (fourcc, mdpr->type_specific_data+off+62, 4);
		fourcc[4]=0;

		printf ("demux_real: fourcc == %s\n", fourcc);

	      } else {
		printf ("demux_real: error, unknown audio data header version %d\n",
			version);
		abort();
	      }
	      
	      if (!strncmp (fourcc, "dnet", 4)) 
		this->audio_buf_type = BUF_AUDIO_DNET;
	      else if (!strncmp (fourcc, "sipr", 4)) 
		this->audio_buf_type = BUF_AUDIO_SIPRO;
	      else if (!strncmp (fourcc, "cook", 4))
		this->audio_buf_type = BUF_AUDIO_COOK;
	      else if (!strncmp (fourcc, "atrc", 4))
		this->audio_buf_type = BUF_AUDIO_ATRK;
	      else 
		this->audio_buf_type = 0;

	      printf ("demux_real: audio codec, buf type %08x\n",
		      this->audio_buf_type);

	      this->audio_stream_num = mdpr->stream_number;

	      if (this->audio_buf_type) {
		buf_element_t *buf;
		
		/* send header */

		buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

		buf->content = buf->mem;

		memcpy (buf->content, mdpr->type_specific_data+off, 
			mdpr->type_specific_len-off);

		buf->size = mdpr->type_specific_len-off;

		buf->input_pos     = 0 ; 
		buf->input_time    = 0 ; 
		buf->type          = this->audio_buf_type;
		buf->decoder_flags = BUF_FLAG_HEADER;
    
		this->audio_fifo->put (this->audio_fifo, buf);  
		
	      }

	      this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
 
	      break;  /* audio */
	    } 
	    if (!strncmp (mdpr->type_specific_data+off, "VIDO", 4)) {
	      printf ("demux_real: video detected\n");
	      break;  /* video */
	    }
	    off++;
	  }
	}
	
	

	/* detect streamtype */

	if (!strncmp (mdpr->type_specific_data+4, "VIDORV20", 8)) {

	  buf_element_t *buf;

	  this->video_stream_num = mdpr->stream_number;
	  this->video_buf_type   = BUF_VIDEO_RV20;

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

	  this->video_stream_num = mdpr->stream_number;
	  this->video_buf_type   = BUF_VIDEO_RV30;

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
	
	}  else {

	  printf ("demux_real: codec not recognized as video\n");

	}

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

static int stream_read_char (demux_real_t *this) {
  uint8_t ret;
  this->input->read (this->input, &ret, 1);
  return ret;
}

static int stream_read_word (demux_real_t *this) {
  uint16_t ret;
  this->input->read (this->input, (char *) &ret, 2);
  return BE_16(&ret);
}

static int demux_real_send_chunk(demux_plugin_t *this_gen) {

  demux_real_t   *this = (demux_real_t *) this_gen;
  char            preamble[PREAMBLE_SIZE];
  unsigned char   data_chunk_header[DATA_CHUNK_HEADER_SIZE];
  char            header[DATA_PACKET_HEADER_SIZE];
  int             stream, size, keyframe;
  uint32_t        timestamp;
  int64_t         pts;
  off_t           offset;

  /* load a header from wherever the stream happens to be pointing */
  if ( (size=this->input->read(this->input, header, DATA_PACKET_HEADER_SIZE)) !=
      DATA_PACKET_HEADER_SIZE) {

    printf ("demux_real: read failed. wanted %d bytes, but got only %d\n",
	    DATA_PACKET_HEADER_SIZE, size);

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

  if (stream == this->video_stream_num) {

    int            vpkg_header, vpkg_length, vpkg_offset;
    int            vpkg_seqnum = -1;
    int            vpkg_subseq = 0;
    buf_element_t *buf;
    int            n;

    printf ("demux_real: video chunk detected.\n");

    /* sub-demuxer */

    while (size > 2) {

      /*
       * read packet header
       * bit 7: 1=last block in block chain
       * bit 6: 1=short header (only one block?)
       */

      vpkg_header = stream_read_char (this); size--;
      printf ("demux_real: hdr: %02x (size=%d)\n", vpkg_header, size);

      if (0x40==(vpkg_header&0xc0)) {
	/*
	 * seems to be a very short header
	 * 2 bytes, purpose of the second byte yet unknown
	 */
	 int bummer;

	 bummer = stream_read_char (this); size--;
	 printf ("demux_real: bummer == %02X\n",bummer);

	 vpkg_offset = 0;
	 vpkg_length = size;

      } else {

	if (0==(vpkg_header & 0x40)) {
	  /* sub-seqnum (bits 0-6: number of fragment. bit 7: ???) */
	  vpkg_subseq = stream_read_char (this); size--;

	  printf ("demux_real: subseq: %02X ", vpkg_subseq);
	  vpkg_subseq &= 0x7f;
	}

	/*
	 * size of the complete packet
	 * bit 14 is always one (same applies to the offset)
	 */
	vpkg_length = stream_read_word (this); size -= 2;
	printf ("demux_real: l: %02X %02X ", vpkg_length>>8, vpkg_length&0xff);
	if (!(vpkg_length&0xC000)) {
	  vpkg_length <<= 16;
	  vpkg_length |=  stream_read_word (this);
	  printf ("demux_real: l+: %02X %02X ",
		  (vpkg_length>>8)&0xff,vpkg_length&0xff);
	  size-=2;
	} else
	  vpkg_length &= 0x3fff;

	/*
	 * offset of the following data inside the complete packet
	 * Note: if (hdr&0xC0)==0x80 then offset is relative to the
	 * _end_ of the packet, so it's equal to fragment size!!!
	 */

	vpkg_offset = stream_read_word (this); size -= 2;
	printf ("demux_real: o: %02X %02X ",
		vpkg_offset>>8,vpkg_offset&0xff);

	if (!(vpkg_offset&0xC000)) {
	  vpkg_offset <<= 16;
	  vpkg_offset |=  stream_read_word (this);
	  printf ("demux_real: o+: %02X %02X ",
		  (vpkg_offset>>8)&0xff,vpkg_offset&0xff);
	  size -= 2;
	} else
	  vpkg_offset &= 0x3fff;

	vpkg_seqnum = stream_read_char (this); size--;
	printf ("demux_real: seq: %02X ", vpkg_seqnum);
      }

      printf ("demux_real: vpkg seqnum=%d, offset=%d, length=%d\n",
	      vpkg_seqnum, vpkg_offset, vpkg_length);

      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

      buf->content       = buf->mem;
      buf->pts           = timestamp;
      buf->input_pos     = 0 ; /* FIXME */
      buf->input_time    = 0 ; /* FIXME */
      buf->type          = this->video_buf_type;
      buf->decoder_flags = 0;
      

      if (this->old_seqnum>=0) {
	
      
	printf ("demux_real: we have an incomplete packet (oldseq=%d new=%d)\n",
		this->old_seqnum, vpkg_seqnum);
	/*  we have an incomplete packet: */
	if (this->old_seqnum != vpkg_seqnum) {

	  /* this fragment is for new packet, close the old one */

	  printf ("demux_real: closing probably incomplete packet\n");

	  /* ds_add_packet(ds,dp); */
	  this->old_seqnum = -1;

	  buf->decoder_flags |= BUF_FLAG_FRAME_START;

	} else {

	  /*  append data to it! */

	  if (0x80==(vpkg_header&0xc0)){

	    /* stream_read(demuxer->stream, dp_data+dp_hdr->len, vpkg_offset); */
	    n = this->input->read (this->input, buf->content, vpkg_offset);

	    buf->size = vpkg_offset;

	    if (n<vpkg_offset) {
	      printf ("demux_real: read error %d/%d\n", n, vpkg_offset);
	      buf->free_buffer(buf);
	      this->status = DEMUX_FINISHED;
	      return this->status;
	    }

	    this->video_fifo->put (this->video_fifo, buf);  

	    size -= vpkg_offset;

	    /* we know that this is the last fragment -> we can close the packet! */

	    /* ds_add_packet(ds,dp); */
	    this->old_seqnum = -1;

	    continue;
	  }

	  /* non-last fragment: */

	  /* stream_read(demuxer->stream, dp_data+dp_hdr->len, size); */
	  n = this->input->read (this->input, buf->content, size);

	  if (n<size) {
	    printf ("demux_real: read error %d/%d\n", n, size);
	    buf->free_buffer (buf);
	    this->status = DEMUX_FINISHED;
	    return this->status;
	  }

	  this->video_fifo->put (this->video_fifo, buf);  

	  size=0;
	  break; /* no more fragments in this chunk! */
	}
      }
      
      if (0x00 == (vpkg_header&0xc0)) {

	printf ("demux_real: first fragment, %d bytes\n", size);

	/* first fragment: */
	/* this->input->seek (this->input, size, SEEK_CUR); */
	/* stream_read(demuxer->stream, dp_data, size); */
	/* ds->asf_packet=dp; */
	this->old_seqnum = vpkg_seqnum;
	n = this->input->read (this->input, buf->content, size);

	buf->size = size;

	if (n<size) {
	  printf ("demux_real: read error\n");
	  buf->free_buffer (buf);
	  this->status = DEMUX_FINISHED;
	  return this->status;
	}

	buf->decoder_flags |= BUF_FLAG_FRAME_START;

	this->video_fifo->put (this->video_fifo, buf);  

	size=0;

	break;
      }

      printf ("demux_real: whole packet, %d bytes\n", vpkg_length);

      /* whole packet (not fragmented): */
      size -= vpkg_length;

      /* stream_read(demuxer->stream, dp_data, vpkg_length); */

      n = this->input->read (this->input, buf->content, vpkg_length);

      buf->size = vpkg_length;

      if (n<vpkg_length) {
	printf ("demux_real: read error\n");
	buf->free_buffer (buf);
	this->status = DEMUX_FINISHED;
	return this->status;
      }

      buf->decoder_flags |= BUF_FLAG_FRAME_START;
      
      this->video_fifo->put (this->video_fifo, buf);  

      /* ds_add_packet(ds,dp); */

    } /* while(len>2) */
    /*
    send_real_buf (this, timestamp, size, 
		   this->streams[stream_num].fifo,
		   this->streams[stream_num].buf_type,
		   0 );
    */
  } else if (stream == this->audio_stream_num) {
    buf_element_t *buf;
    int            n;

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

    buf->content       = buf->mem;
    buf->pts           = timestamp;
    buf->input_pos     = 0 ; /* FIXME */
    buf->input_time    = 0 ; /* FIXME */
    buf->type          = this->audio_buf_type;
    buf->decoder_flags = 0;
    buf->size          = size;

    n = this->input->read (this->input, buf->content, size);

    if (n<size) {

      printf ("demux_real: read error\n");

      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    this->audio_fifo->put (this->audio_fifo, buf);  

    /* FIXME: dp->flags = (flags & 0x2) ? 0x10 : 0; */

  } else {

    /* discard */

    this->input->seek(this->input, size, SEEK_CUR);

  }

#if 0

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

#endif

  return this->status;
}

static void demux_real_send_headers(demux_plugin_t *this_gen) {

  demux_real_t *this = (demux_real_t *) this_gen;

  this->video_fifo = this->stream->video_fifo;
  this->audio_fifo = this->stream->audio_fifo;

  this->status = DEMUX_OK;

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

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
    {
      uint8_t buf[4096];

      if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {

	input->seek(input, 0, SEEK_SET);

	if (input->read(input, buf, 4)) {

	  printf ("demux_real: input seekable, read 4 bytes: %02x %02x %02x %02x\n",
		  buf[0], buf[1], buf[2], buf[3]);

	  if ((buf[0] != 0x2e)
	      || (buf[1] != 'R')
	      || (buf[2] != 'M')
	      || (buf[3] != 'F')) 
	    return NULL;
	} else
	  return NULL;

      } else if (input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW)) {
	
	printf ("demux_real: input provides preview, read 4 bytes: %02x %02x %02x %02x\n",
		buf[0], buf[1], buf[2], buf[3]);

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

    printf ("demux_real: by extension '%s'\n", mrl); 

    ending = strrchr(mrl, '.');

    printf ("demux_real: ending %s\n", ending);

    if (!ending) 
      return NULL;

    if (strncasecmp (ending, ".rm", 3)
	&& strncasecmp (ending, ".ra", 3) 
	&& strncasecmp (ending, ".ram", 4)) 
      return NULL;

    printf ("demux_real: by extension accepted.\n");

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

  this->old_seqnum = -1;

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

static void *init_class (xine_t *xine, void *data) {

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
  { PLUGIN_DEMUX, 17, "real", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
