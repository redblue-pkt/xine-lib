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
 * $Id: demux_mpeg_block.c,v 1.122 2002/10/22 04:23:19 storri Exp $
 *
 * demultiplexer for mpeg 1/2 program streams
 *
 */
 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

/*
#define LOG
*/

#define VALID_MRLS          "dvd,stdin,fifo"
#define VALID_ENDS          "vob"

#define NUM_PREVIEW_BUFFERS   250
#define DISC_TRESHOLD       90000

#define WRAP_THRESHOLD     120000 
#define PTS_AUDIO 0
#define PTS_VIDEO 1


/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( (x<0) ? (-x) : (x) )

typedef struct demux_mpeg_block_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;  
  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  pthread_t             thread;
  int                   thread_running;
  pthread_mutex_t       mutex;

  int                   status;
  
  int                   blocksize;
  int                   rate;

  int                   send_end_buffers;
  int                   warned; /* encryption warning */

  char                  cur_mrl[256];

  uint8_t              *scratch, *scratch_base;

  int64_t               nav_last_end_pts;
  int64_t               nav_last_start_pts;
  int64_t               last_pts[2];
  int                   send_newpts;
  int                   buf_flag_seek;

} demux_mpeg_block_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_mpeg_block_class_t;

/* OK, i think demux_mpeg_block discontinuity handling demands some
   explanation:
   
   - The preferred discontinuity handling/detection for DVD is based on
   NAV packets information. Most of the time it will provide us very
   accurate and reliable information.
   
   - Has been shown that sometimes a DVD discontinuity may happen before
   a new NAV packet arrives (seeking?). To avoid sending wrong PTS to
   decoders we _used_ to check for SCR discontinuities. Unfortunately
   some VCDs have very broken SCR values causing false triggering.
   
   - To fix the above problem (also note that VCDs don't have NAV
   packets) we fallback to the same PTS-based wrap detection as used
   in demux_mpeg. The only trick is to not send discontinuity information
   if NAV packets have already done the job.
   
   [Miguel 02-05-2002]
*/

static void check_newpts( demux_mpeg_block_t *this, int64_t pts, int video )
{
  int64_t diff;
  
  diff = pts - this->last_pts[video];
  
  if( pts && (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD) ) ) {

    /* check if pts is outside nav pts range. any stream without nav must enter here. */
    if( pts > this->nav_last_end_pts || pts < this->nav_last_start_pts )
    {
#ifdef LOG
      printf("demux_mpeg_block: discontinuity detected by pts wrap\n");
#endif
      if (this->buf_flag_seek) {
        xine_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
        this->buf_flag_seek = 0;
      } else {
        xine_demux_control_newpts(this->stream, pts, 0);
      }
      this->send_newpts = 0;
    } else {
#ifdef LOG
      printf("demux_mpeg_block: no wrap detected\n" );
#endif
    }
    
    this->last_pts[1-video] = 0;
  }
  
  if( pts )
    this->last_pts[video] = pts;
}


static void demux_mpeg_block_parse_pack (demux_mpeg_block_t *this, int preview_mode) {

  buf_element_t *buf = NULL;
  uint8_t       *p;
  int            bMpeg1=0;
  uint32_t       header_len;
  int64_t        pts;
  uint32_t       packet_len;
  uint32_t       stream_id;
  int64_t        scr = 0;

#ifdef LOG
  printf ("demux_mpeg_block: read_block\n");
#endif

  buf = this->input->read_block (this->input, this->video_fifo, this->blocksize);

  if (buf==NULL) {
    this->status = DEMUX_FINISHED;
    return ;
  }
#if 0

  if (buf==NULL) {
    xine_next_mrl_event_t event;

#ifdef LOG
    printf ("demux_mpeg_block: read_block failed\n");
#endif

    /*
     * check if seamless branching is possible
     */

    event.event.type = XINE_EVENT_NEED_NEXT_MRL;
    event.handled = 0;
    xine_send_event (this->xine, &event.event);

    /* strdup segfaults is passed a NULL */
    if (event.handled && event.mrl) {

      char *next_mrl = strdup(event.mrl);

#ifdef LOG      
      printf ("demux_mpeg_block: checking if we can branch to %s\n", next_mrl);
#endif

      if (next_mrl && this->input->is_branch_possible 
	  && this->input->is_branch_possible (this->input, next_mrl)) {

#ifdef LOG      
        printf ("demux_mpeg_block: branching\n");
#endif

	this->input->close (this->input);
        this->input->open (this->input, next_mrl);

	free(next_mrl);

	event.event.type = XINE_EVENT_BRANCHED;
	xine_send_event (this->xine, &event.event);
	
	buf = this->input->read_block (this->input, this->video_fifo, this->blocksize);
	if (!buf) {
	  this->status = DEMUX_FINISHED;
	  return ;
        }

      } else {
	this->status = DEMUX_FINISHED;
	return ;
      }
    } else {
      this->status = DEMUX_FINISHED;
      return ;
    }
  }
#endif

  /* If this is not a block for the demuxer, pass it
   * straight through. */
  if (buf->type != BUF_DEMUX_BLOCK) {
    buf_element_t *cbuf;

    this->video_fifo->put (this->video_fifo, buf);

    /* duplicate goes to audio fifo */

    if (this->audio_fifo) {
      cbuf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

      cbuf->type = buf->type;
      cbuf->decoder_flags = buf->decoder_flags;
      cbuf->decoder_info[0] = buf->decoder_info[0];
      cbuf->decoder_info[1] = buf->decoder_info[1];
      cbuf->decoder_info[2] = buf->decoder_info[2];
      cbuf->decoder_info[3] = buf->decoder_info[3];

      this->audio_fifo->put (this->audio_fifo, cbuf);
    }

#ifdef LOG
    printf ("demux_mpeg_block: type %08x != BUF_DEMUX_BLOCK\n", buf->type);
#endif

    return;
  }

  p = buf->content; /* len = this->mnBlocksize; */
  if (preview_mode)
    buf->decoder_flags = BUF_FLAG_PREVIEW;
  else
    buf->decoder_flags = 0;
    
  buf->input_pos = this->input->get_current_pos (this->input);
  buf->input_length = this->input->get_length (this->input);

  if (this->rate)
    buf->input_time = buf->input_pos / (this->rate * 50);

  if (p[3] == 0xBA) { /* program stream pack header */


    bMpeg1 = (p[4] & 0x40) == 0;

    if (bMpeg1) {

      /* system_clock_reference */

      scr  = (p[4] & 0x02) << 30;
      scr |= (p[5] & 0xFF) << 22;
      scr |= (p[6] & 0xFE) << 14;
      scr |= (p[7] & 0xFF) <<  7;
      scr |= (p[8] & 0xFE) >>  1;

      /* buf->scr = scr; */

      /* mux_rate */

      if (!this->rate) {
	this->rate = (p[9] & 0x7F) << 15;
	this->rate |= (p[10] << 7);
	this->rate |= (p[11] >> 1);
      }

      buf->input_time = buf->input_pos / (this->rate * 50);

      p   += 12;

    } else { /* mpeg2 */
      
      int      num_stuffing_bytes;

      /* system_clock_reference */

      scr  = (p[4] & 0x08) << 27 ;
      scr |= (p[4] & 0x03) << 28 ;
      scr |= p[5] << 20;
      scr |= (p[6] & 0xF8) << 12 ;
      scr |= (p[6] & 0x03) << 13 ;
      scr |= p[7] << 5;
      scr |= (p[8] & 0xF8) >> 3;
      /*  optional - decode extension:
	  scr *=300;
	  scr += ( (p[8] & 0x03 << 7) | (p[9] & 0xFE >> 1) );
      */

#ifdef LOG
      printf ("demux_mpeg_block: SCR=%lld\n", scr);
#endif

      /* mux_rate */

      if (!this->rate) {
	this->rate = (p[0xA] << 14);
	this->rate |= (p[0xB] << 6);
	this->rate |= (p[0xB] >> 2);
      }

      num_stuffing_bytes = p[0xD] & 0x07;

      p += 14 + num_stuffing_bytes;
    }
  } 

  if (p[3] == 0xbb) { /* program stream system header */

    int header_len;

    header_len = (p[4] << 8) | p[5];

    p    += 6 + header_len;
  }

  /* we should now have a PES packet here */

  if (p[0] || p[1] || (p[2] != 1)) {
    printf ("demux_mpeg_block: error! %02x %02x %02x (should be 0x000001)\n",
	    p[0], p[1], p[2]);
    buf->free_buffer (buf);

    this->warned++;
    if (this->warned > 5) {
      xine_log (this->stream->xine, XINE_LOG_MSG,
		_("demux_mpeg_block: too many errors, stopping playback. Maybe this stream is scrambled?\n"));
      this->status = DEMUX_FINISHED;
    }

    return;
  }

  packet_len = p[4] << 8 | p[5];
  stream_id  = p[3];

  if (stream_id == 0xbf) {  /* NAV Packet */

    int64_t start_pts, end_pts;

    start_pts  = (p[7+12] << 24);
    start_pts |= (p[7+13] << 16);
    start_pts |= (p[7+14] << 8);
    start_pts |= p[7+15];

    end_pts  = (p[7+16] << 24);
    end_pts |= (p[7+17] << 16);
    end_pts |= (p[7+18] << 8);
    end_pts |= p[7+19];

    buf->content   = p;
    buf->size      = packet_len;
    buf->type      = BUF_SPU_NAV;
    buf->pts       = 0;   /* NAV packets do not have PES values */
    buf->input_pos = this->input->get_current_pos(this->input);
    buf->input_length = this->input->get_length (this->input);
    this->video_fifo->put (this->video_fifo, buf);

#ifdef LOG
    printf ("demux_mpeg_block: NAV packet, start pts = %lld, end_pts = %lld\n",
	    start_pts, end_pts);
#endif

    if (this->nav_last_end_pts != start_pts && !preview_mode) {

#ifdef LOG
      printf("demux_mpeg_block: discontinuity detected by nav packet\n" );
#endif
      if (this->buf_flag_seek) {
        xine_demux_control_newpts(this->stream, start_pts, BUF_FLAG_SEEK);
        this->buf_flag_seek = 0;
      } else {
        xine_demux_control_newpts(this->stream, start_pts, 0);
      }
    }
    this->nav_last_end_pts = end_pts;
    this->nav_last_start_pts = start_pts;
    this->send_newpts = 0;
    this->last_pts[PTS_AUDIO] = this->last_pts[PTS_VIDEO] = 0;

    return ;
  }

  if (bMpeg1) {

    if (stream_id == 0xBF) {
      buf->free_buffer (buf);
      return ;
    }

    p   += 6; /* packet_len -= 6; */

    while ((p[0] & 0x80) == 0x80) {
      p++; 
      packet_len--;
      /* printf ("stuffing\n");*/
    }

    if ((p[0] & 0xc0) == 0x40) {
      /* STD_buffer_scale, STD_buffer_size */
      p += 2;
      packet_len -=2;
    }

    pts = 0; 
    if ((p[0] & 0xf0) == 0x20) {
      pts  = (p[ 0] & 0x0E) << 29 ;
      pts |=  p[ 1]         << 22 ;
      pts |= (p[ 2] & 0xFE) << 14 ;
      pts |=  p[ 3]         <<  7 ;
      pts |= (p[ 4] & 0xFE) >>  1 ;
      p   += 5;
      packet_len -=5;
    } else if ((p[0] & 0xf0) == 0x30) {
      pts  = (p[ 0] & 0x0E) << 29 ;
      pts |=  p[ 1]         << 22 ;
      pts |= (p[ 2] & 0xFE) << 14 ;
      pts |=  p[ 3]         <<  7 ;
      pts |= (p[ 4] & 0xFE) >>  1 ;
      /* DTS decoding code is working, but not used in xine
	 DTS  = (p[ 5] & 0x0E) << 29 ;
	 DTS |=  p[ 6]         << 22 ;
	 DTS |= (p[ 7] & 0xFE) << 14 ;
	 DTS |=  p[ 8]         <<  7 ;
	 DTS |= (p[ 9] & 0xFE) >>  1 ;
      */
      p   += 10;
      packet_len -= 10;
    } else {
      p++; 
      packet_len --;
    }

  } else { /* mpeg 2 */
    /* check PES scrambling_control */
    if (((p[6] & 0x30) != 0) && !this->warned) {

      xine_log (this->stream->xine, XINE_LOG_MSG,
		_("demux_mpeg_block: warning: pes header indicates that this stream may be encrypted (encryption mode %d)\n"), (p[6] & 0x30) >> 4);

      this->warned = 1;
    }

    if (p[7] & 0x80) { /* pts avail */

      pts  = (p[ 9] & 0x0E) << 29 ;
      pts |=  p[10]         << 22 ;
      pts |= (p[11] & 0xFE) << 14 ;
      pts |=  p[12]         <<  7 ;
      pts |= (p[13] & 0xFE) >>  1 ;

#ifdef LOG
      printf ("demux_mpeg_block: pts = %lld\n", pts);
#endif

    } else
      pts = 0;

    /* code is working but not used in xine
       if (p[7] & 0x40) {  
      
       DTS  = (p[14] & 0x0E) << 29 ;
       DTS |=  p[15]         << 22 ;
       DTS |= (p[16] & 0xFE) << 14 ;
       DTS |=  p[17]         <<  7 ;
       DTS |= (p[18] & 0xFE) >>  1 ;
      
       } else
       DTS = 0;
    */


    header_len = p[8];

    p    += header_len + 9;
    packet_len -= header_len + 3;
  }

  if (stream_id == 0xbd) {

    int track, spu_id;
    
    track = p[0] & 0x0F; /* hack : ac3 track */

    if((p[0] & 0xE0) == 0x20) {
      spu_id = (p[0] & 0x1f);

      buf->content   = p+1;
      buf->size      = packet_len-1;
      buf->type      = BUF_SPU_PACKAGE + spu_id;
      buf->pts       = pts;
      if( !preview_mode )
        check_newpts( this, pts, PTS_VIDEO );
      
      buf->input_pos = this->input->get_current_pos(this->input);
      buf->input_length = this->input->get_length (this->input);
      
      this->video_fifo->put (this->video_fifo, buf);    
#ifdef LOG
      printf ("demux_mpeg_block: SPU PACK put on fifo\n");
#endif
      
      return;
    }

    if ((p[0]&0xF0) == 0x80) {
    
      /*  printf ( "ac3 PES packet, track %02x\n",track);  */
      buf->decoder_info[1] = p[1]; /* Number of frame headers */
      buf->decoder_info[2] = p[2] << 8 | p[3]; /* First access unit pointer */

      buf->content   = p+4;
      buf->size      = packet_len-4;
      if (track & 0x8) {
        buf->type      = BUF_AUDIO_DTS + (track & 0x07); /* DVDs only have 8 tracks */
      } else {
        buf->type      = BUF_AUDIO_A52 + track;
      }
      buf->pts       = pts;
      if( !preview_mode )
        check_newpts( this, pts, PTS_AUDIO );

      buf->input_pos = this->input->get_current_pos(this->input);
      buf->input_length = this->input->get_length (this->input);

      if(this->audio_fifo) {
	this->audio_fifo->put (this->audio_fifo, buf);
#ifdef LOG
        printf ("demux_mpeg_block: A52 PACK put on fifo\n");
#endif
      } else
	buf->free_buffer(buf);
      
      return ;
    } else if ((p[0]&0xf0) == 0xa0) {

      int pcm_offset;
      int number_of_frame_headers;
      int first_access_unit_pointer;
      int audio_frame_number;
      int bits_per_sample;
      int sample_rate;
      int num_channels;
      int dynamic_range;

      /*
       * found in http://members.freemail.absa.co.za/ginggs/dvd/mpeg2_lpcm.txt
       * appears to be correct.
       */

      number_of_frame_headers = p[1];
      /* unknown = p[2]; */
      first_access_unit_pointer = p[3];
      audio_frame_number = p[4];

      /*
       * 000 => mono
       * 001 => stereo
       * 010 => 3 channel
       * ...
       * 111 => 8 channel
       */
      num_channels = (p[5] & 0x7) + 1;
      sample_rate = p[5] & 0x10 ? 96000 : 48000;
      switch ((p[5]>>6) & 3) {
      case 3: /* illegal, use 16-bits? */
      default:
	printf ("illegal lpcm sample format (%d), assume 16-bit samples\n",
		(p[5]>>6) & 3 );
      case 0: bits_per_sample = 16; break;
      case 1: bits_per_sample = 20; break;
      case 2: bits_per_sample = 24; break;
      }
      dynamic_range = p[6];

      /* send lpcm config byte */
      buf->decoder_flags |= BUF_FLAG_SPECIAL;
      buf->decoder_info[1] = BUF_SPECIAL_LPCM_CONFIG;
      buf->decoder_info[2] = p[5];
      
      pcm_offset = 7;

      buf->content   = p+pcm_offset;
      buf->size      = packet_len-pcm_offset;
      buf->type      = BUF_AUDIO_LPCM_BE + track;
      buf->pts       = pts;
      if( !preview_mode )
        check_newpts( this, pts, PTS_AUDIO );

      buf->input_pos = this->input->get_current_pos(this->input);
      buf->input_length = this->input->get_length (this->input);

      if(this->audio_fifo) {
	this->audio_fifo->put (this->audio_fifo, buf);
#ifdef LOG
        printf ("demux_mpeg_block: LPCM PACK put on fifo\n");
#endif
      } else
	buf->free_buffer(buf);
      
      return ;
    }
  } else if ((stream_id >= 0xbc) && ((stream_id & 0xf0) == 0xe0)) {


    buf->content   = p;
    buf->size      = packet_len;
    buf->type      = BUF_VIDEO_MPEG;
    buf->pts       = pts;
    if( !preview_mode )
      check_newpts( this, pts, PTS_VIDEO );

    buf->input_pos = this->input->get_current_pos(this->input);
    buf->input_length = this->input->get_length (this->input);

    this->video_fifo->put (this->video_fifo, buf);
#ifdef LOG
        printf ("demux_mpeg_block: MPEG Video PACK put on fifo\n");
#endif

    return ;

  }  else if ((stream_id & 0xe0) == 0xc0) {
    int track;

    track = stream_id & 0x1f;

    buf->content   = p;
    buf->size      = packet_len;
    buf->type      = BUF_AUDIO_MPEG + track;
    buf->pts       = pts;
    if( !preview_mode )
        check_newpts( this, pts, PTS_AUDIO );

    buf->input_pos = this->input->get_current_pos(this->input);
    buf->input_length = this->input->get_length (this->input);

    if(this->audio_fifo) {
      this->audio_fifo->put (this->audio_fifo, buf);
#ifdef LOG
        printf ("demux_mpeg_block: MPEG Audio PACK put on fifo\n");
#endif
    } else
      buf->free_buffer(buf);

    return ;

  } 
  buf->free_buffer (buf);

  return ;
  
}

static void *demux_mpeg_block_loop (void *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  /* printf ("demux_mpeg_block: demux loop starting...\n"); */
  pthread_mutex_lock( &this->mutex );

  /* do-while needed to seek after demux finished */
  do {

    /* main demuxer loop */
    while(this->status == DEMUX_OK) {

      demux_mpeg_block_parse_pack(this, 0);

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      /* give demux_*_stop a chance to interrupt us */
      sched_yield();
      pthread_mutex_lock( &this->mutex );
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->video_fifo->size(this->video_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while( this->status == DEMUX_OK );

  /*
  printf ("demux_mpeg_block: demux loop finished (status: %d)\n",
	  this->status);
  */

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->stream, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );
  
  pthread_exit(NULL);

  return NULL;
}

/* estimate bitrate */

static int demux_mpeg_block_estimate_rate (demux_mpeg_block_t *this) {

  buf_element_t *buf = NULL;
  unsigned char *p;
  int            is_mpeg1=0;
  off_t          pos, last_pos;
  off_t          step;
  int64_t        pts, last_pts;
  int            rate;
  int            count;
  int            stream_id;

  if (!(this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) ||
      (this->input->get_capabilities(this->input) & INPUT_CAP_VARIABLE_BITRATE)) 
    return 0;

  last_pos = 0;
  last_pts = 0;
  rate     = 0;
  step     = this->input->get_length (this->input) / 10;
  step     = (step / this->blocksize) * this->blocksize;
  if (step <= 0) step = this->blocksize; /* avoid endless loop for tiny files */
  pos      = step;
  count    = 0;
  
  this->input->seek (this->input, pos, SEEK_SET);

  while ((buf = this->input->read_block (this->input, this->video_fifo, this->blocksize)) ) {

    p = buf->content; /* len = this->mnBlocksize; */

    if (p[3] == 0xBA) { /* program stream pack header */

      is_mpeg1 = (p[4] & 0x40) == 0;

      if (is_mpeg1) 
	p   += 12;
      else 
	p += 14 + (p[0xD] & 0x07);
    }

    if (p[3] == 0xbb)  /* program stream system header */
      p  += 6 + ((p[4] << 8) | p[5]);

    /* we should now have a PES packet here */

    if (p[0] || p[1] || (p[2] != 1)) {
      printf ("demux_mpeg_block: error %02x %02x %02x (should be 0x000001) \n",
	      p[0], p[1], p[2]);
      buf->free_buffer (buf);
      return rate;
    }

    stream_id  = p[3];
    pts = 0; 

    if ((stream_id < 0xbc) || ((stream_id & 0xf0) != 0xe0)) {
      pos += (off_t) this->blocksize;
      buf->free_buffer (buf);
      continue; /* only use video packets */
    }

    if (is_mpeg1) {

      if (p[3] != 0xBF) { /* stream_id */

	p += 6; /* packet_len -= 6; */

	while ((p[0] & 0x80) == 0x80) {
	  p++; /* stuffing */
	}

	if ((p[0] & 0xc0) == 0x40) {
	  /* STD_buffer_scale, STD_buffer_size */
	  p += 2;
	}

	if ( ((p[0] & 0xf0) == 0x20) || ((p[0] & 0xf0) == 0x30) ) {
	  pts  = (p[ 0] & 0x0E) << 29 ;
	  pts |=  p[ 1]         << 22 ;
	  pts |= (p[ 2] & 0xFE) << 14 ;
	  pts |=  p[ 3]         <<  7 ;
	  pts |= (p[ 4] & 0xFE) >>  1 ;
	} 
      }
    } else { /* mpeg 2 */
      
      if (p[7] & 0x80) { /* pts avail */
	
	pts  = (p[ 9] & 0x0E) << 29 ;
	pts |=  p[10]         << 22 ;
	pts |= (p[11] & 0xFE) << 14 ;
	pts |=  p[12]         <<  7 ;
	pts |= (p[13] & 0xFE) >>  1 ;
	
      } else
	pts = 0;
    }

    if (pts) {


      if ( (pos>last_pos) && (pts>last_pts) ) {
	int cur_rate;
      
	cur_rate = ((pos - last_pos)*90000) / ((pts - last_pts) * 50);
	
	rate = (count * rate + cur_rate) / (count+1);

	count ++;
	
	/*
	printf ("demux_mpeg_block: stream_id %02x, pos: %lld, pts: %d, cur_rate = %d, overall rate : %d\n", 
		stream_id, pos, pts, cur_rate, rate); 
	*/
      }

      last_pos = pos;
      last_pts = pts;
      pos += step;
    } else
      pos += (off_t) this->blocksize;

    buf->free_buffer (buf);

    if (this->input->seek (this->input, pos, SEEK_SET) == (off_t)-1)
      break;

  }

#ifdef LOG
  printf("demux_mpeg_block:est_rate=%d\n",rate);
#endif
  return rate;
  
}

static void demux_mpeg_block_dispose (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  free (this->scratch_base);
  free (this);
  
}

static void demux_mpeg_block_stop (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );
  
  if (!this->thread_running) {
    printf ("demux_mpeg_block: stop...ignored\n");
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;
  
  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_demux_flush_engine(this->stream);
  
  xine_demux_control_end(this->stream, BUF_FLAG_END_USER);
}

static int demux_mpeg_block_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  return (this->thread_running?DEMUX_OK:DEMUX_FINISHED);
}

static int demux_mpeg_detect_blocksize(demux_mpeg_block_t *this, 
				       input_plugin_t *input)
{
  input->seek(input, 2048, SEEK_SET);
  if (!input->read(input, this->scratch, 4))
    return 0;

  if (this->scratch[0] || this->scratch[1]
      || (this->scratch[2] != 0x01) || (this->scratch[3] != 0xba)) {

    input->seek(input, 2324, SEEK_SET);
    if (!input->read(input, this->scratch, 4))
      return 0;
    if (this->scratch[0] || this->scratch[1] 
        || (this->scratch[2] != 0x01) || (this->scratch[3] != 0xba)) 
      return 0;
     
    return 2324;
  } else
    return 2048;
}

static void demux_mpeg_block_send_headers (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  pthread_mutex_lock (&this->mutex);

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    if (!this->blocksize)
      this->blocksize = demux_mpeg_detect_blocksize( this, this->input );

    if (!this->blocksize)
      return;
  }
  
  /* 
   * send start buffer
   */
  
  xine_demux_control_start(this->stream);
  
  if (!this->rate)
    this->rate = demux_mpeg_block_estimate_rate (this);
  
  if((this->input->get_capabilities(this->input) & INPUT_CAP_PREVIEW) != 0) {
    
    int num_buffers = NUM_PREVIEW_BUFFERS;
    
    this->input->seek (this->input, 0, SEEK_SET);
    
    this->status = DEMUX_OK ;
    while ( (num_buffers>0) && (this->status == DEMUX_OK) ) {
      
      demux_mpeg_block_parse_pack(this, 1);
      num_buffers --;
    }
  }
  this->status = DEMUX_FINISHED;

  xine_demux_control_headers_done (this->stream);

  pthread_mutex_unlock (&this->mutex);

  return;
}


static int demux_mpeg_block_start (demux_plugin_t *this_gen,
				    off_t start_pos, int start_time) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  buf_element_t *buf;
  int err;
  int status;

  pthread_mutex_lock( &this->mutex );

  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    
    if (start_pos) {
      start_pos /= (off_t) this->blocksize;
      start_pos *= (off_t) this->blocksize;
      
      this->input->seek (this->input, start_pos, SEEK_SET);
    } else if (start_time) {
      start_pos = start_time * this->rate * 50;
      start_pos /= (off_t) this->blocksize;
      start_pos *= (off_t) this->blocksize;
      
      this->input->seek (this->input, start_pos, SEEK_SET);
    } else
      this->input->seek (this->input, 0, SEEK_SET);
  }
  
  /*
   * query CLUT from the input plugin
   */
  
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  
  if ((this->input->get_capabilities(this->input) & INPUT_CAP_CLUT) &&
      ((this->input->get_optional_data(this->input, buf->mem, INPUT_OPTIONAL_DATA_CLUT)
	== INPUT_OPTIONAL_SUCCESS))) {
    buf->type = BUF_SPU_CLUT;
    
    this->video_fifo->put(this->video_fifo, buf);
  } else {
    buf->free_buffer(buf);
  }
  
  /*
   * now start demuxing
   */
  this->send_newpts = 1;
  if( !this->thread_running ) {
    
    this->buf_flag_seek = 0;
    this->nav_last_end_pts = this->nav_last_start_pts = 0;
    this->status   = DEMUX_OK ;
    this->last_pts[0]   = 0;
    this->last_pts[1]   = 0;
    
    this->send_end_buffers = 1;
    this->thread_running = 1;
    if ((err = pthread_create (&this->thread,
			       NULL, demux_mpeg_block_loop, this)) != 0) {
      printf ("demux_mpeg_block: can't create new thread (%s)\n",
	      strerror(err));
      abort();
    }
  } else {
    this->buf_flag_seek = 1;
    this->nav_last_end_pts = this->nav_last_start_pts = 0;
    xine_demux_flush_engine(this->stream);
  }
  
  /* this->status is saved because we can be interrupted between
   * pthread_mutex_unlock and return
   */
  status = this->status;
  pthread_mutex_unlock( &this->mutex );
  return status;
}

static int demux_mpeg_block_seek (demux_plugin_t *this_gen,
				  off_t start_pos, int start_time) {
  /* demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen; */

  return demux_mpeg_block_start (this_gen, start_pos, start_time);
}


static void demux_mpeg_block_accept_input (demux_mpeg_block_t *this,
					   input_plugin_t *input) {

  this->input = input;

  if (strcmp (this->cur_mrl, input->get_mrl(input))) {

    this->rate = 0;

    strncpy (this->cur_mrl, input->get_mrl(input), 256);

    printf ("demux_mpeg_block: mrl %s is new, will estimated bitrate\n",
	    this->cur_mrl);

  } else
    printf ("demux_mpeg_block: mrl %s is known, estimated bitrate: %d\n",
	    this->cur_mrl, this->rate * 50 * 8);
}

static int demux_mpeg_block_get_stream_length (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
                        /*
   * find input plugin
   */

  if (this->rate) 
    return this->input->get_length (this->input) / (this->rate * 50);
  else
    return 0;

}
static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                   input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;

  demux_mpeg_block_t *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf("demux_mpeg_block.c: not seekable, can't handle!\n");
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_mpeg_block_t));
  this->stream = stream;
  this->input  = input;
/*  
  this->config = xine->config;
  this->xine   = xine;
*/  
  this->demux_plugin.send_headers      = demux_mpeg_block_send_headers;
  this->demux_plugin.start             = demux_mpeg_block_start;
  this->demux_plugin.seek              = demux_mpeg_block_seek;
  this->demux_plugin.stop              = demux_mpeg_block_stop;
  this->demux_plugin.dispose           = demux_mpeg_block_dispose;
  this->demux_plugin.get_status        = demux_mpeg_block_get_status;
  this->demux_plugin.get_stream_length = demux_mpeg_block_get_stream_length;
  this->demux_plugin.demux_class       = class_gen;

/*  this->demux_plugin.open              = demux_mpeg_block_open; ????*/

#if 0
  /* Calling register_string() configure valid mrls in configfile */
  (void*) this->config->register_string(this->config, "mrl.mrls_mpeg_block", VALID_MRLS,
					_("valid mrls for mpeg block demuxer"),
					NULL, 20, NULL, NULL);
  (void*) this->config->register_string(this->config,
					"mrl.ends_mpeg_block", VALID_ENDS,
					_("valid mrls ending for mpeg block demuxer"),
					NULL, 20, NULL, NULL);    
#endif
  
  this->scratch = xine_xmalloc_aligned (512, 4096, (void**) &this->scratch_base);
  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );

#ifdef LOG
  printf("demux_mpeg_block:open_plugin:detection_method=%d\n",stream->content_detection_method);
#endif
 
  switch (stream->content_detection_method) {

  case XINE_DEMUX_CONTENT_STRATEGY: {

    if(((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) &&
       ((input->get_capabilities(input) & INPUT_CAP_VARIABLE_BITRATE) == 0) ) {

      this->blocksize = input->get_blocksize(input);

      if (!this->blocksize)
        this->blocksize = demux_mpeg_detect_blocksize( this, input );

      if (!this->blocksize) {
        free (this);
        return NULL;
      }

      input->seek(input, 0, SEEK_SET);
      if (input->read(input, this->scratch, this->blocksize)) {

        if (this->scratch[0] || this->scratch[1]
            || (this->scratch[2] != 0x01) || (this->scratch[3] != 0xba)) {
          free (this);
          return NULL;
        }

        /* if it's a file then make sure it's mpeg-2 */
        if ( !input->get_blocksize(input)
             && ((this->scratch[4]>>4) != 4) ) {
          free (this);
          return NULL;
        }

        demux_mpeg_block_accept_input (this, input);
#ifdef LOG
        printf("demux_mpeg_block:open_plugin:Accepting detection_method XINE_DEMUX_CONTENT_STRATEGY blocksize=%d\n",this->blocksize);
#endif
        break;
      }
    }
    free (this);
    return NULL;
  }
  break;

  case XINE_DEMUX_EXTENSION_STRATEGY: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');

    if (!ending) {
      free (this);
      return NULL;
    }
    if( (strncmp((ending + 3), "mpeg2", 5)) ||
            (strncmp((ending + 3), "mpeg1", 5)) ) {
      this->blocksize = 2048;
      demux_mpeg_block_accept_input(this, input);
    } else if(!strncmp(mrl, "vcd", 3)) {
      this->blocksize = 2324;
      demux_mpeg_block_accept_input (this, input);
    } else {
      free (this);
      return NULL;
    }
  }
  break;
  default:
    free (this);
    return NULL;
  }
/*  strncpy (this->last_mrl, input->get_mrl (input), 1024); */
  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "DVD/VOB demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "MPEG_BLOCK";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "vob";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "";
}

static void class_dispose (demux_class_t *this_gen) {

  demux_mpeg_block_class_t *this = (demux_mpeg_block_class_t *) this_gen;

  free (this);
 }

static void *init_plugin (xine_t *xine, void *data) {

  demux_mpeg_block_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_mpeg_block_class_t));
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
  { PLUGIN_DEMUX, 14, "mpeg_block", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
