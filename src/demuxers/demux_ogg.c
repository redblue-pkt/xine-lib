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
 * $Id: demux_ogg.c,v 1.79 2003/04/15 03:41:23 heinchen Exp $
 *
 * demultiplexer for ogg streams
 *
 */
/* 2003.02.09 (dilb) update of the handling for audio/video infos for strongarm cpus. */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

/*
#define LOG
 */

#define CHUNKSIZE                8500
#define PACKET_TYPE_HEADER       0x01
#define PACKET_TYPE_COMMENT      0x03
#define PACKET_TYPE_CODEBOOK     0x05
#define PACKET_TYPE_BITS	 0x07
#define PACKET_LEN_BITS01        0xc0
#define PACKET_LEN_BITS2         0x02
#define PACKET_IS_SYNCPOINT      0x08

#define MAX_STREAMS              16

#define PTS_AUDIO                0
#define PTS_VIDEO                1

#define WRAP_THRESHOLD           900000

#define SUB_BUFSIZE 1024

typedef struct demux_ogg_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  
  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  int                   status;
  
  int                   frame_duration;

  ogg_sync_state        oy;
  ogg_stream_state      os;
  ogg_page              og;

  ogg_stream_state      oss[MAX_STREAMS];
  uint32_t              buf_types[MAX_STREAMS];
  int                   preview_buffers[MAX_STREAMS];
  int64_t               header_granulepos[MAX_STREAMS];
  int                   factor[MAX_STREAMS];
  int                   quotient[MAX_STREAMS];

  int                   num_streams;

  int                   num_audio_streams;
  int                   num_video_streams;
  int                   num_spu_streams;

  off_t                 avg_bitrate;

  int64_t               last_pts[2];
  int                   send_newpts;
  int                   buf_flag_seek;
  int                   keyframe_needed;
  int                   ignore_keyframes;
  int                   time_length;
} demux_ogg_t ;


typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_ogg_class_t;

static int get_stream (demux_ogg_t *this, int serno)
{
  int i;
  for (i = 0; i<this->num_streams; i++) {
    if (this->oss[i].serialno == serno) {
      return i;
    }
  }
  return -1;
}

static int get_pts (demux_ogg_t *this, int stream_num , int64_t granulepos ) {
  return (granulepos*this->factor[stream_num]/this->quotient[stream_num]);
}

static int read_ogg_page (demux_ogg_t *this) {
  char *buffer;
  long bytes;
  while (ogg_sync_pageout(&this->oy,&this->og)!=1) {
    buffer = ogg_sync_buffer(&this->oy, CHUNKSIZE);
    bytes  = this->input->read(this->input, buffer, CHUNKSIZE);
    if (bytes < CHUNKSIZE) {
      return 0;
    }
    ogg_sync_wrote(&this->oy, bytes);
  }
  return 1;
}

static void hex_dump (uint8_t *p, int length) {
  int i,j;
  unsigned char c;
  for (j=0;j<length;j=i) {
    printf ("%04X ",j);
    for (i=j;i<(j+16<length?j+16:length);i++) 
      printf ("%02X ", c=p[i]);
    for (i=j;i<(j+16<length?j+16:length);i++) 
      if ( ((c=p[i])>=20) && (c<128)) printf ("%c", c); else printf (".");
    printf("\n");
  }
}

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

static void check_newpts (demux_ogg_t *this, int64_t pts, int video, int preview) {
  int64_t diff;

#ifdef LOG
  printf ("demux_ogg: new pts %lld found in stream\n",pts);
#endif

  diff = pts - this->last_pts[video];

  if (!preview && (pts>=0) &&
      (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD) ) ) {

    if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG) 
      printf ("demux_ogg: diff=%lld (pts=%lld, last_pts=%lld)\n", 
	      diff, pts, this->last_pts[video]);

    if (this->buf_flag_seek) {
      xine_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      xine_demux_control_newpts(this->stream, pts, 0);
    }
    this->send_newpts = 0;
    this->last_pts[1-video] = 0;
  }

  if (!preview && (pts>=0) )
    this->last_pts[video] = pts;

  /* use pts for bitrate measurement */

  if (pts>180000) {
    this->avg_bitrate = this->input->get_current_pos (this->input) * 8 * 90000/ pts;

    if (this->avg_bitrate<1)
      this->avg_bitrate = 1;

  }
}

/* 
 * utility function to pack one ogg_packet into a xine
 * buffer, fill out all needed fields
 * and send it to the right fifo
 */

static void send_ogg_buf (demux_ogg_t *this,
			  ogg_packet  *op,
			  int          stream_num,
			  uint32_t     decoder_flags) {

  int hdrlen;

  hdrlen = (*op->packet & PACKET_LEN_BITS01) >> 6;
  hdrlen |= (*op->packet & PACKET_LEN_BITS2) << 1;

  if ( this->audio_fifo 
       && (this->buf_types[stream_num] & 0xFF000000) == BUF_AUDIO_BASE) {
    buf_element_t *buf;
	
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
	
    if ((this->buf_types[stream_num] & 0xFFFF0000) == BUF_AUDIO_VORBIS) {
      int op_size = sizeof(ogg_packet);
      ogg_packet *og_ghost;
      op_size += (4 - (op_size % 4));

      /* nasty hack to pack op as well as (vorbis) content
	 in one xine buffer */
      memcpy (buf->content + op_size, op->packet, op->bytes);
      memcpy (buf->content, op, op_size);
      og_ghost = (ogg_packet *) buf->content;
      og_ghost->packet = buf->content + op_size;
      
      buf->size   = op->bytes;
    } else {
      memcpy (buf->content, op->packet+1+hdrlen, op->bytes-1-hdrlen);
      buf->size   = op->bytes-1-hdrlen;
    }

#ifdef LOG
    printf ("demux_ogg: audio buf_size %d\n", buf->size);
#endif
	
    if (op->granulepos!=-1) {
      buf->pts = get_pts(this, stream_num, op->granulepos );
      check_newpts( this, buf->pts, PTS_AUDIO, decoder_flags );

    } else if (this->header_granulepos[stream_num]!=-1) {
      buf->pts = get_pts(this, stream_num, this->header_granulepos[stream_num]);
      this->header_granulepos[stream_num]=-1;
      check_newpts( this, buf->pts, PTS_AUDIO, decoder_flags );
    } else
      buf->pts = 0; 

#ifdef LOG
    printf ("demux_ogg: audio granulepos %lld => pts %lld => time %lld\n",
	    op->granulepos, buf->pts,buf->pts/90);
#endif

    buf->extra_info->input_pos     = this->input->get_current_pos (this->input);
    buf->extra_info->input_time    = buf->pts / 90;
    buf->type          = this->buf_types[stream_num] ;
    buf->decoder_flags = decoder_flags;
    
    this->audio_fifo->put (this->audio_fifo, buf);
    
  } else if ((this->buf_types[stream_num] & 0xFF000000) == BUF_VIDEO_BASE) {
    
    buf_element_t *buf;
    int todo, done;

#ifdef LOG
    printf ("demux_ogg: video buffer, type=%08x\n", this->buf_types[stream_num]);
#endif

    todo = op->bytes;
    done = 1+hdrlen; 
    while (done<todo) {

      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
	
      if ( (todo-done)>(buf->max_size-1)) {
	buf->size  = buf->max_size-1;
	buf->decoder_flags = decoder_flags;
      } else {
	buf->size = todo-done;
	buf->decoder_flags = BUF_FLAG_FRAME_END | decoder_flags;
      }
      
      /*
	printf ("demux_ogg: done %d todo %d doing %d\n", done, todo, buf->size);
      */
      memcpy (buf->content, op->packet+done, buf->size);

      if (op->granulepos!=-1) {
	buf->pts  = get_pts (this, stream_num, op->granulepos);
	check_newpts( this, buf->pts, PTS_VIDEO, decoder_flags );

      } else if (this->header_granulepos[stream_num]!=-1) {
	buf->pts  = get_pts (this, stream_num, this->header_granulepos[stream_num]);
	this->header_granulepos[stream_num]=-1;
	check_newpts( this, buf->pts, PTS_VIDEO, decoder_flags );

      } else
	buf->pts  = 0;

#ifdef LOG
      printf ("demux_ogg: video granulepos %lld, pts %lld, time %d\n", op->granulepos, buf->pts, buf->pts / 90);
#endif
      
      buf->extra_info->input_pos  = this->input->get_current_pos (this->input);
      buf->extra_info->input_time = buf->pts / 90 ;
      buf->type       = this->buf_types[stream_num] ;
	
      done += buf->size;
      
      this->video_fifo->put (this->video_fifo, buf);
      
    }
  } else if ((this->buf_types[stream_num] & 0xFF000000) == BUF_SPU_BASE) {

    buf_element_t *buf;
    int i,ignore;
    char *subtitle,*str;
    int lenbytes;
    int lines,start,end;
    uint32_t *val;

    for (i = 0, lenbytes = 0; i < hdrlen; i++) {
      lenbytes = lenbytes << 8;
      lenbytes += *((unsigned char *) op->packet + hdrlen - i);
    }

    if (op->packet[0] == PACKET_TYPE_HEADER ) {
#ifdef LOG
      printf ("demux_ogg: Textstream-header-packet\n");
#endif
    } else if (op->packet[0] == PACKET_TYPE_COMMENT ) {
#ifdef LOG
      printf ("demux_ogg: Textstream-comment-packet\n");
#endif
    } else {
      subtitle = (char *)&op->packet[hdrlen + 1];
      if ((strlen(subtitle) > 1) || (*subtitle != ' ')) {

	start = op->granulepos;
	end = start+lenbytes;

	buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

	buf->type = this->buf_types[stream_num];
	buf->pts = 0;
       
	val = (uint32_t * )buf->content;

	/*num_lines will be set later, when we know the correct value*/
	*val++ = 0;

	/*times are in ms*/
	*val++ = 1;
	*val++ = start;
	*val++ = end;
	str = (char *)val;

       for ( i=0, ignore=0, lines=0 ; i<strlen(subtitle) ; i++ ) {
	 /*fixme: the maximum SUB_BUFSIZE isn't considered*/
	 /*fixme: font tags aren't ignored correctly*/
	 switch (subtitle[i]) {
	 case '<':
	   ignore=1;
	   break;
	 case '>':
	   ignore=0;
	   break;
	 case 13:
	   if (ignore==0) {
	     str[0]=0;
	     lines++;
	     str+=1;
	   }
	   break;
	 default:
	   if (ignore==0) {
	     str[0]=subtitle[i];
	     str+=1;
	   }
	   break;
	 }
	 str[0]=0;
       }

       val = (uint32_t * )buf->content;
       *val = lines;

       this->video_fifo->put (this->video_fifo, buf);
      }
    }
  }
}

/*
 * interpret stream start packages, send headers
 */
static void demux_ogg_send_header (demux_ogg_t *this) {

  int        stream_num = -1;
  int        cur_serno;
  
  char      *buffer;
  long       bytes;

  int        done = 0;

  int        filelength,position;
 
  ogg_packet op;
  
#ifdef LOG
  printf ("demux_ogg: detecting stream types...\n");
#endif

  this->ignore_keyframes = 0;

  while (!done) {
    if (!read_ogg_page(this)) {
      this->status = DEMUX_FINISHED;
      return;
    }
    /* now we've got at least one new page */
    
    cur_serno = ogg_page_serialno (&this->og);
    
    if (ogg_page_bos(&this->og)) {

#ifdef LOG
      printf ("demux_ogg: beginning of stream\ndemux_ogg: serial number %d\n",
	      cur_serno);
#endif
      
      ogg_stream_init(&this->oss[this->num_streams], cur_serno);
      stream_num = this->num_streams;
      this->buf_types[stream_num] = 0;
      this->header_granulepos[stream_num] = -1;
      this->num_streams++;
    } else {
      stream_num = get_stream(this, cur_serno);
      if (stream_num == -1) {
	printf ("demux_ogg: help, stream with no beginning!\n");
	abort();
      }
    }
    
    ogg_stream_pagein(&this->oss[stream_num], &this->og);
    
    while (ogg_stream_packetout(&this->oss[stream_num], &op) == 1) {
      
      if (!this->buf_types[stream_num]) {
	/* detect buftype */
	if (!strncmp (&op.packet[1], "vorbis", 6)) {
	  
	  vorbis_info       vi;
	  vorbis_comment    vc;
	  
	  this->buf_types[stream_num] = BUF_AUDIO_VORBIS
	    +this->num_audio_streams++;
	  
	  this->preview_buffers[stream_num] = 3;

	  vorbis_info_init(&vi);
	  vorbis_comment_init(&vc);
	  if (vorbis_synthesis_headerin(&vi, &vc, &op) >= 0) {
	    
	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE]
	      = vi.bitrate_nominal;
	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE]
	      = vi.rate;
	      
	    this->factor[stream_num] = 90000;
	    this->quotient[stream_num] = vi.rate;
	    
	    if (vi.bitrate_nominal<1)
	      this->avg_bitrate += 100000; /* assume 100 kbit */
	    else
	      this->avg_bitrate += vi.bitrate_nominal;
	    
	  } else {
	    this->factor[stream_num] = 900;
	    this->quotient[stream_num] = 441;
	    
	    this->preview_buffers[stream_num] = 0;
	    xine_log (this->stream->xine, XINE_LOG_MSG,
		      _("ogg: vorbis audio track indicated but no vorbis stream header found.\n"));
	  }
	  
	} else if (!strncmp (&op.packet[1], "video", 5)) {
	  
	  buf_element_t    *buf;
	  xine_bmiheader    bih;
	  int               channel;
	  
	  int16_t          locbits_per_sample;
	  uint32_t         locsubtype;
	  int32_t          locsize, locdefault_len, locbuffersize, locwidth, locheight;
	  int64_t          loctime_unit, locsamples_per_unit;

	  memcpy(&locsubtype, &op.packet[9], 4);
	  memcpy(&locsize, &op.packet[13], 4);
	  memcpy(&loctime_unit, &op.packet[17], 8);
	  memcpy(&locsamples_per_unit, &op.packet[25], 8);
	  memcpy(&locdefault_len, &op.packet[33], 4);
	  memcpy(&locbuffersize, &op.packet[37], 4);
	  memcpy(&locbits_per_sample, &op.packet[41], 2);
	  memcpy(&locwidth, &op.packet[45], 4);
	  memcpy(&locheight, &op.packet[49], 4);
	  
#ifdef LOG
	  printf ("demux_ogg: direct show filter created stream detected, hexdump:\n");
	  hex_dump (op.packet, op.bytes);
#endif
	  
	  channel = this->num_video_streams++;

	  this->buf_types[stream_num] = fourcc_to_buf_video (locsubtype);
	  if( !this->buf_types[stream_num] )
	    this->buf_types[stream_num] = BUF_VIDEO_UNKNOWN;
	  this->buf_types[stream_num] |= channel;
	  this->preview_buffers[stream_num] = 5; /* FIXME: don't know */

#ifdef LOG
	  printf ("demux_ogg: subtype          %.4s\n", &locsubtype);
	  printf ("demux_ogg: time_unit        %lld\n", loctime_unit);
	  printf ("demux_ogg: samples_per_unit %lld\n", locsamples_per_unit);
	  printf ("demux_ogg: default_len      %d\n", locdefault_len);
	  printf ("demux_ogg: buffersize       %d\n", locbuffersize);
	  printf ("demux_ogg: bits_per_sample  %d\n", locbits_per_sample);
	  printf ("demux_ogg: width            %d\n", locwidth);
	  printf ("demux_ogg: height           %d\n", locheight);
	  printf ("demux_ogg: buf_type         %08x\n",this->buf_types[stream_num]);
#endif

	  bih.biSize=sizeof(xine_bmiheader);
	  bih.biWidth = locwidth;
	  bih.biHeight= locheight;
	  bih.biPlanes= 0;
	  memcpy(&bih.biCompression, &locsubtype, 4);
	  bih.biBitCount= 0;
	  bih.biSizeImage=locwidth*locheight;
	  bih.biXPelsPerMeter=1;
	  bih.biYPelsPerMeter=1;
	  bih.biClrUsed=0;
	  bih.biClrImportant=0;

	  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
	  buf->decoder_flags = BUF_FLAG_HEADER;
	  this->frame_duration = loctime_unit * 9 / 1000;
	  this->factor[stream_num] = loctime_unit * 9;
	  this->quotient[stream_num] = 1000;
	  buf->decoder_info[1] = this->frame_duration;
	  memcpy (buf->content, &bih, sizeof (xine_bmiheader));
	  buf->size = sizeof (xine_bmiheader);
	  buf->type = this->buf_types[stream_num];

	  /*
	   * video metadata
	   */

	  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]
	    = locwidth;
	  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT]
	    = locheight;
	  this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION]
	    = this->frame_duration;

	  this->avg_bitrate += 500000; /* FIXME */

	  this->video_fifo->put (this->video_fifo, buf);

	} else if (!strncmp (&op.packet[1], "audio", 5)) {

	  if (this->audio_fifo) {
	    buf_element_t    *buf;
	    int               codec;
	    char              str[5];
	    int               channel;

	    int16_t          locbits_per_sample, locchannels, locblockalign;
	    uint32_t         locsubtype;
	    int32_t          locsize, locdefault_len, locbuffersize, locavgbytespersec;
	    int64_t          loctime_unit, locsamples_per_unit;

	    memcpy(&locsubtype, &op.packet[9], 4);
	    memcpy(&locsize, &op.packet[13], 4);
	    memcpy(&loctime_unit, &op.packet[17], 8);
	    memcpy(&locsamples_per_unit, &op.packet[25], 8);
	    memcpy(&locdefault_len, &op.packet[33], 4);
	    memcpy(&locbuffersize, &op.packet[37], 4);
	    memcpy(&locbits_per_sample, &op.packet[41], 2);
	    memcpy(&locchannels, &op.packet[45], 2);
	    memcpy(&locblockalign, &op.packet[47], 2);
	    memcpy(&locavgbytespersec, &op.packet[49], 4);

#ifdef LOG	      
	    printf ("demux_ogg: direct show filter created audio stream detected, hexdump:\n");
	    hex_dump (op.packet, op.bytes);
#endif
	      
	    memcpy(str, &locsubtype, 4);
	    str[4] = 0;
	    codec = atoi(str);
	      
	    channel= this->num_audio_streams++;
	      
	    switch (codec) {
	    case 0x01:
	      this->buf_types[stream_num] = BUF_AUDIO_LPCM_LE | channel;
	      break;
	    case 55:
	    case 0x55:
	      this->buf_types[stream_num] = BUF_AUDIO_MPEG | channel;
	      break;
	    case 0x2000:
	      this->buf_types[stream_num] = BUF_AUDIO_A52 | channel;
	      break;
	    default:
	      printf ("demux_ogg: unknown audio codec type 0x%x\n",
		      codec);
	      this->buf_types[stream_num] = BUF_CONTROL_NOP;
	      break;
	    }
	      
#ifdef LOG
	    printf ("demux_ogg: subtype          0x%x\n", codec);
	    printf ("demux_ogg: time_unit        %lld\n", loctime_unit);
	    printf ("demux_ogg: samples_per_unit %lld\n", locsamples_per_unit);
	    printf ("demux_ogg: default_len      %d\n", locdefault_len);
	    printf ("demux_ogg: buffersize       %d\n", locbuffersize);
	    printf ("demux_ogg: bits_per_sample  %d\n", locbits_per_sample);
	    printf ("demux_ogg: channels         %d\n", locchannels);
	    printf ("demux_ogg: blockalign       %d\n", locblockalign);
	    printf ("demux_ogg: avgbytespersec   %d\n", locavgbytespersec);
	    printf ("demux_ogg: buf_type         %08x\n",this->buf_types[stream_num]);
#endif
	    
	    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
	    buf->type = this->buf_types[stream_num];
	    buf->decoder_flags = BUF_FLAG_HEADER;
	    buf->decoder_info[0] = 0;
	    buf->decoder_info[1] = locsamples_per_unit;
	    buf->decoder_info[2] = locbits_per_sample;
	    buf->decoder_info[3] = locchannels;
	    this->audio_fifo->put (this->audio_fifo, buf);
	      
	    this->preview_buffers[stream_num] = 5; /* FIXME: don't know */
	    this->factor[stream_num] = 90000;
	    this->quotient[stream_num] = locsamples_per_unit;

	      
	    
	    this->avg_bitrate += locavgbytespersec*8;

	    /*
	     * audio metadata
	     */

	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS]
	      = locchannels;
	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS]
	      = locbits_per_sample;
	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE]
	      = locsamples_per_unit;
	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE]
	      = locavgbytespersec*8;

	  } else /* no audio_fifo there */
	    this->buf_types[stream_num] = BUF_CONTROL_NOP;

	} else if (op.bytes >= 142 
		   && !strncmp (&op.packet[1], "Direct Show Samples embedded in Ogg", 35) ) {

#ifdef LOG	    
	  printf ("demux_ogg: older direct show filter-generated stream header detected.\n");
	  hex_dump (op.packet, op.bytes);
#endif
	  this->preview_buffers[stream_num] = 5; /* FIXME: don't know */

	  if ( (*(int32_t*)(op.packet+96)==0x05589f80) && (op.bytes>=184)) {

	    buf_element_t    *buf;
	    xine_bmiheader    bih;
	    int               channel;
	    uint32_t          fcc;

#ifdef LOG	    
	    printf ("demux_ogg: seems to be a video stream.\n");
#endif

	    channel = this->num_video_streams++;

	    fcc = *(uint32_t*)(op.packet+68);

#ifdef LOG
	    printf ("demux_ogg: fourcc %08x\n", fcc);
#endif

	    this->buf_types[stream_num] = fourcc_to_buf_video (fcc);
	    if( !this->buf_types[stream_num] )
	      this->buf_types[stream_num] = BUF_VIDEO_UNKNOWN;
	    this->buf_types[stream_num] |= channel;

	    bih.biSize          = sizeof(xine_bmiheader);
	    bih.biWidth         = *(int32_t*)(op.packet+176);
	    bih.biHeight        = *(int32_t*)(op.packet+180);
	    bih.biPlanes        = 0;
	    memcpy (&bih.biCompression, op.packet+68, 4);
	    bih.biBitCount      = *(int16_t*)(op.packet+182);
	    if (!bih.biBitCount) 
	      bih.biBitCount = 24; /* FIXME ? */
	    bih.biSizeImage     = (bih.biBitCount>>3)*bih.biWidth*bih.biHeight;
	    bih.biXPelsPerMeter = 1;
	    bih.biYPelsPerMeter = 1;
	    bih.biClrUsed       = 0;
	    bih.biClrImportant  = 0;

	    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
	    buf->decoder_flags = BUF_FLAG_HEADER;
	    this->frame_duration = (*(int64_t*)(op.packet+164)) * 9 / 1000;
	    this->factor[stream_num] = (*(int64_t*)(op.packet+164)) * 9;
	    this->quotient[stream_num] = 1000;

	    buf->decoder_info[1] = this->frame_duration;
	    memcpy (buf->content, &bih, sizeof (xine_bmiheader));
	    buf->size = sizeof (xine_bmiheader);	  
	    buf->type = this->buf_types[stream_num];
	    this->video_fifo->put (this->video_fifo, buf);

#ifdef LOG
	    printf ("demux_ogg: subtype          %.4s\n", &fcc);
	    printf ("demux_ogg: buf_type         %08x\n", this->buf_types[stream_num]);
	    printf ("demux_ogg: video size       %d x %d\n", bih.biWidth, bih.biHeight);
	    printf ("demux_ogg: frame duration   %d\n", this->frame_duration);
#endif

	    /*
	     * video metadata
	     */

	    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]
	      = bih.biWidth;
	    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT]
	      = bih.biHeight;
	    this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION]
	      = this->frame_duration;

	    this->avg_bitrate += 500000; /* FIXME */

	    this->ignore_keyframes = 1;

	  } else if (*(int32_t*)op.packet+96 == 0x05589F81) {

#if 0
	    /* FIXME: no test streams */

	    buf_element_t    *buf;
	    int               codec;
	    char              str[5];
	    int               channel;
	    int               extra_size;

	    extra_size         = *(int16_t*)(op.packet+140);
	    format             = *(int16_t*)(op.packet+124);
	    channels           = *(int16_t*)(op.packet+126);
	    samplerate         = *(int32_t*)(op.packet+128);
	    nAvgBytesPerSec    = *(int32_t*)(op.packet+132);
	    nBlockAlign        = *(int16_t*)(op.packet+136);
	    wBitsPerSample     = *(int16_t*)(op.packet+138);
	    samplesize         = (sh_a->wf->wBitsPerSample+7)/8;
	    cbSize             = extra_size;
	    if(extra_size > 0)
	      memcpy(wf+sizeof(WAVEFORMATEX),op.packet+142,extra_size);

#endif

	    printf ("demux_ogg: FIXME, old audio format not handled\n");

	    this->buf_types[stream_num] = BUF_CONTROL_NOP;

	  } else {
	    printf ("demux_ogg: old header detected but stream type is unknown\n");
	    this->buf_types[stream_num] = BUF_CONTROL_NOP;
	  }
	} else if (!strncmp (&op.packet[1], "text", 4)) {
	  int channel=0;
#ifdef LOG
	  printf ("demux_ogg: textstream detected.\n");
#endif
	  this->preview_buffers[stream_num] = 2;
	  channel= this->num_spu_streams++;
	  this->buf_types[stream_num] = BUF_SPU_TEXT | channel;
	} else {
	  printf ("demux_ogg: unknown stream type (signature >%.8s<). hex dump of bos packet follows:\n",
		  op.packet);
	    
	  hex_dump (op.packet, op.bytes);
	    
	  this->buf_types[stream_num] = BUF_CONTROL_NOP;
	}
      }

      /*
       * send preview buffer
       */

#ifdef LOG
      printf ("demux_ogg: sending preview buffer of stream type %08x\n",
	      this->buf_types[stream_num]);
#endif

      send_ogg_buf (this, &op, stream_num, BUF_FLAG_PREVIEW);

      if (!ogg_page_bos(&this->og)) {

	int i;

	/* are we finished ? */
	this->preview_buffers[stream_num] --;
	  
	done = 1;

	for (i=0; i<this->num_streams; i++) {
	  if (this->preview_buffers[i]>0)
	    done = 0;

#ifdef LOG
	  printf ("demux_ogg: %d preview buffers left to send from stream %d\n",
		  this->preview_buffers[i], i);
#endif
	}
      }
    }
  }


  this->time_length=-1;

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {
    position=this->input->get_current_pos(this->input);
    filelength=this->input->get_length(this->input);
    
    if (filelength!=-1) {
      if (filelength>70000)
	this->demux_plugin.seek((demux_plugin_t *)this, (off_t) filelength-65536 ,0);
      done=0;
      while (!done) {
	if (!read_ogg_page (this))
	  return;

	stream_num=get_stream(this, ogg_page_serialno (&this->og) );
	if (stream_num!=-1) {
	  if (this->time_length < get_pts(this, stream_num, ogg_page_granulepos(&this->og)))
	    this->time_length = get_pts(this, stream_num, ogg_page_granulepos(&this->og)) / 90;
	}
      }
      this->demux_plugin.seek((demux_plugin_t *)this, position,0);
    }  
  }
}

static void demux_ogg_send_content (demux_ogg_t *this) {

  int stream_num;
  int cur_serno;
  
  ogg_packet op;
  
#ifdef LOG
  printf ("demux_ogg: send package...\n");
#endif

  if (!read_ogg_page(this)) {
    this->status = DEMUX_FINISHED;
#ifdef LOG
      printf ("demux_ogg: EOF\n");
#endif
    return;
  }
  /* now we've got one new page */
    
  cur_serno = ogg_page_serialno (&this->og);
  stream_num=get_stream(this, cur_serno);
  if (stream_num < 0) {
    printf ("demux_ogg: error: unknown stream, serialnumber %d\n", cur_serno);
    
    if (!ogg_page_bos(&this->og)) {
      printf ("demux_ogg: help, stream with no beginning!\n");
    }
    
    printf ("demux_ogg: adding late stream with serial number %d (all content will be discarded)\n",
	    cur_serno);

    ogg_stream_init(&this->oss[this->num_streams], cur_serno);
    stream_num = this->num_streams;
    this->buf_types[stream_num] = 0;      
    this->header_granulepos[stream_num]=-1;
    this->num_streams++;
  }
  
  ogg_stream_pagein(&this->oss[stream_num], &this->og);
    
  if (ogg_page_bos(&this->og)) {
#ifdef LOG
    printf ("demux_ogg: beginning of stream\ndemux_ogg: serial number %d - discard\n",
	    ogg_page_serialno (&this->og));
#endif
    while (ogg_stream_packetout(&this->oss[stream_num], &op) == 1) ;
    return;
  }
            
  while (ogg_stream_packetout(&this->oss[stream_num], &op) == 1) {
    /* printf("demux_ogg: packet: %.8s\n", op.packet); */
    /* printf("demux_ogg:   got a packet\n"); */

    if (*op.packet & PACKET_TYPE_HEADER) {
      if (op.granulepos!=-1) {
	this->header_granulepos[stream_num]=op.granulepos;
#ifdef LOG
	printf ("demux_ogg: header with granulepos, remembering granulepos\n");
#endif
      } else {
#ifdef LOG
	printf ("demux_ogg: header => discard\n");
#endif
      }
      continue;
    }

    if (this->buf_flag_seek)
      if ((op.granulepos==-1) && (this->header_granulepos[stream_num]==-1)) continue;
	

    if (!this->ignore_keyframes && this->keyframe_needed) {
#ifdef LOG
      printf ("demux_ogg: keyframe needed... buf_type=%08x\n", this->buf_types[stream_num]); 
#endif
      if (((this->buf_types[stream_num] & 0xFF000000) == BUF_VIDEO_BASE) &&
	  (*op.packet == PACKET_IS_SYNCPOINT)) {
	/*
	  printf("keyframe: l%ld b%ld e%ld g%ld p%ld str%d\n", 
	  op.bytes,op.b_o_s,op.e_o_s,(long) op.granulepos,
	  (long) op.packetno,stream_num);
	  hex_dump (op.packet, op.bytes);
	*/
	this->keyframe_needed = 0;
      } else continue;
    }
    send_ogg_buf (this, &op, stream_num, 0);
  }
}

static int demux_ogg_send_chunk (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  demux_ogg_send_content (this);

  return this->status;
}

static void demux_ogg_dispose (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  free (this);
}

static int demux_ogg_get_status (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  return this->status;
}

static void demux_ogg_send_headers (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /*
   * send start buffers
   */

  this->last_pts[0]   = 0;
  this->last_pts[1]   = 0;

  /*
   * initialize ogg engine
   */
  ogg_sync_init(&this->oy);
  
  this->num_streams       = 0;
  this->num_audio_streams = 0;
  this->num_video_streams = 0;
  this->num_spu_streams   = 0;
  this->avg_bitrate       = 1;
  
  this->input->seek (this->input, 0, SEEK_SET);

  if (this->status == DEMUX_OK) {
    xine_demux_control_start(this->stream);
    /* send header */
    demux_ogg_send_header (this);

#ifdef LOG
    printf ("demux_ogg: headers sent, avg bitrate is %lld\n",
	    this->avg_bitrate);
#endif
  }

  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = this->num_video_streams>0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = this->num_audio_streams>0;
  this->stream->stream_info[XINE_STREAM_INFO_MAX_SPU_CHANNEL] = this->num_spu_streams;
}

static int demux_ogg_seek (demux_plugin_t *this_gen,
			   off_t start_pos, int start_time) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  
  /*
   * seek to start position
   */
  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {

    this->keyframe_needed = (this->num_video_streams>0);

    if ( (!start_pos) && (start_time)) {
      start_pos = start_time * this->avg_bitrate/8;
#ifdef LOG
      printf ("demux_ogg: seeking to %d seconds => %lld bytes\n",
	      start_time, start_pos);
#endif
    }

    ogg_sync_reset(&this->oy);
    this->input->seek (this->input, start_pos, SEEK_SET);
  }

  this->send_newpts     = 1;

  if( !this->stream->demux_thread_running ) {
    
    this->status            = DEMUX_OK;
    this->buf_flag_seek     = 0;

  } else {
    this->buf_flag_seek = 1;
    xine_demux_flush_engine(this->stream);
  }
  
  return this->status;
}

static int demux_ogg_get_stream_length (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen; 

  if (this->time_length==-1){
    if (this->avg_bitrate)
      return (int)((int64_t)1000 * this->input->get_length (this->input) * 8 /
		   this->avg_bitrate);
    else
      return 0;
  }
  else
    return this->time_length;
}

static uint32_t demux_ogg_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_ogg_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, 
				    xine_stream_t *stream, 
				    input_plugin_t *input) {
  
  demux_ogg_t *this;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
    {
      uint8_t buf[MAX_PREVIEW_SIZE];

      if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {

	input->seek(input, 0, SEEK_SET);

	if (input->read(input, buf, 4)) {

	  if ((buf[0] != 'O')
	      || (buf[1] != 'g')
	      || (buf[2] != 'g')
	      || (buf[3] != 'S')) 
	    return NULL;
	} else {
	  return NULL;
	}
      } else if (input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW)) {
	if ((buf[0] != 'O')
	    || (buf[1] != 'g')
	    || (buf[2] != 'g')
	    || (buf[3] != 'S')) 
	  return NULL;
      } else
	return NULL;
    }
    break;

  case METHOD_BY_EXTENSION: {

    char *ending, *mrl;
    
    mrl = input->get_mrl (input);
    
    /*
     * check extension
     */
    
    ending = strrchr (mrl, '.');
    
    if (!ending)
      return NULL;
      
    if (strncasecmp(ending, ".ogg", 4) &&
        strncasecmp(ending, ".ogm", 4)) {
      return NULL;
    } 

  }
  break;

  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }

  /*
   * if we reach this point, the input has been accepted.
   */

  this         = xine_xmalloc (sizeof (demux_ogg_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_ogg_send_headers;
  this->demux_plugin.send_chunk        = demux_ogg_send_chunk;
  this->demux_plugin.seek              = demux_ogg_seek;
  this->demux_plugin.dispose           = demux_ogg_dispose;
  this->demux_plugin.get_status        = demux_ogg_get_status;
  this->demux_plugin.get_stream_length = demux_ogg_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_ogg_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ogg_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;
  
  this->status = DEMUX_FINISHED;
  
  return &this->demux_plugin;
}



/*
 * ogg demuxer class
 */

static char *get_description (demux_class_t *this_gen) {
  return "OGG demux plugin";
}
 
static char *get_identifier (demux_class_t *this_gen) {
  return "OGG";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "ogg ogm";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "audio/x-ogg: ogg: OggVorbis Audio;"
         "application/x-ogg: ogg: OggVorbis Audio;"; 
}

static void class_dispose (demux_class_t *this_gen) {

  demux_ogg_class_t *this = (demux_ogg_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {
  
  demux_ogg_class_t     *this;
  
  this         = xine_xmalloc (sizeof (demux_ogg_class_t));
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
  { PLUGIN_DEMUX, 20, "ogg", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};




