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
 * $Id: demux_mpeg_block.c,v 1.41 2001/09/08 00:44:40 guenter Exp $
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
#include <string.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "monitor.h"
#include "demux.h"
#include "utils.h"

#define NUM_PREVIEW_BUFFERS 250

static uint32_t xine_debug;

typedef struct demux_mpeg_block_s {
  demux_plugin_t        demux_plugin;

  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  pthread_t             thread;

  int                   status;
  
  int                   blocksize;
  int                   rate;

  int                   send_end_buffers;

  gui_get_next_mrl_cb_t next_mrl_cb;
  gui_branched_cb_t     branched_cb;

  char                  cur_mrl[256];

  uint8_t              *scratch;

} demux_mpeg_block_t ;


static void demux_mpeg_block_parse_pack (demux_mpeg_block_t *this, int preview_mode) {

  buf_element_t *buf = NULL;
  unsigned char *p;
  int            bMpeg1=0;
  uint32_t       header_len;
  uint32_t       PTS;
  uint32_t       packet_len;
  uint32_t       stream_id;

  buf = this->input->read_block (this->input, this->video_fifo, this->blocksize);

  /* If this is not a block for the demuxer, pass it
   * straight through. */
  if(buf->type != BUF_DEMUX_BLOCK) {
    this->video_fifo->put (this->video_fifo, buf);
    return;
  }

  if (buf==NULL) {
    char *next_mrl;

    printf ("demux_mpeg_block: read_block failed\n");

    /*
     * check if seamless branching is possible
     */

    if (this->next_mrl_cb 
	&& (next_mrl = this->next_mrl_cb () )) {
      printf ("demux_mpeg_block: checking if we can branch to %s\n", next_mrl);

      if (this->input->is_branch_possible 
	  && this->input->is_branch_possible (this->input, next_mrl)) {

        printf ("demux_mpeg_block: branching\n");

	this->input->close (this->input);
        this->input->open (this->input, next_mrl);
	
	if (this->branched_cb)
	  this->branched_cb ();

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

  p = buf->content; /* len = this->mnBlocksize; */
  if (preview_mode)
    buf->decoder_info[0] = 0;
  else
    buf->decoder_info[0] = 1;

  buf->input_pos = this->input->get_current_pos (this->input);
  
  if (this->rate)
    buf->input_time = buf->input_pos / (this->rate * 50);

  if (p[3] == 0xBA) { /* program stream pack header */


    xprintf (VERBOSE|DEMUX, "program stream pack header\n");

    bMpeg1 = (p[4] & 0x40) == 0;

    if (bMpeg1) {

      if (!this->rate) {
	this->rate = (p[9] & 0x7F) << 15;
	this->rate |= (p[10] << 7);
	this->rate |= (p[11] >> 1);
      }

      buf->input_time = buf->input_pos / (this->rate * 50);

      p   += 12;

    } else { /* mpeg2 */

      int   num_stuffing_bytes;

      /* SCR decoding code works but is not used by xine
      int   scr;

      scr  = (p[4] & 0x38) << 27 ;
      scr |= (p[4] & 0x03) << 28 ;
      scr |= p[5] << 20;
      scr |= (p[6] & 0xF8) << 12 ;
      scr |= (p[6] & 0x03) << 13 ;
      scr |= p[7] << 5;
      scr |= (p[8] & 0xF8) >> 3;

      optional - decode extension:

      scr *=300;
      scr += ( (p[8] & 0x03 << 7) | (p[9] & 0xFE >> 1) );
      */

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

    xprintf (VERBOSE|DEMUX, "program stream system header\n");

    header_len = (p[4] << 8) | p[5];

    p    += 6 + header_len;
  }

  /* we should now have a PES packet here */

  if (p[0] || p[1] || (p[2] != 1)) {
    printf ("demux_mpeg_block: error! %02x %02x %02x (should be 0x000001) \n",
	    p[0], p[1], p[2]);
    buf->free_buffer (buf);
    return ;
  }

  packet_len = p[4] << 8 | p[5];
  stream_id  = p[3];

  xprintf (VERBOSE|DEMUX, "packet id = %02x len = %d\n",stream_id, packet_len);

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

    PTS = 0; 
    if ((p[0] & 0xf0) == 0x20) {
      PTS  = (p[ 0] & 0x0E) << 29 ;
      PTS |=  p[ 1]         << 22 ;
      PTS |= (p[ 2] & 0xFE) << 14 ;
      PTS |=  p[ 3]         <<  7 ;
      PTS |= (p[ 4] & 0xFE) >>  1 ;
      p   += 5;
      packet_len -=5;
    } else if ((p[0] & 0xf0) == 0x30) {
      PTS  = (p[ 0] & 0x0E) << 29 ;
      PTS |=  p[ 1]         << 22 ;
      PTS |= (p[ 2] & 0xFE) << 14 ;
      PTS |=  p[ 3]         <<  7 ;
      PTS |= (p[ 4] & 0xFE) >>  1 ;
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

    if (p[7] & 0x80) { /* PTS avail */
      
      PTS  = (p[ 9] & 0x0E) << 29 ;
      PTS |=  p[10]         << 22 ;
      PTS |= (p[11] & 0xFE) << 14 ;
      PTS |=  p[12]         <<  7 ;
      PTS |= (p[13] & 0xFE) >>  1 ;
      
    } else
      PTS = 0;

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

      xprintf(VERBOSE|DEMUX, "SPU PES packet, id 0x%03x\n",p[0] & 0x1f);

      buf->content   = p+1;
      buf->size      = packet_len-1;
      buf->type      = BUF_SPU_PACKAGE + spu_id;
      buf->PTS       = PTS;
      buf->input_pos = this->input->get_current_pos(this->input);
      
      this->video_fifo->put (this->video_fifo, buf);    
      
      return;
    }

    if ((p[0]&0xF0) == 0x80) {

      xprintf (VERBOSE|DEMUX|AC3, "ac3 PES packet, track %02x\n",track);
      /*  printf ( "ac3 PES packet, track %02x\n",track);  */

      buf->content   = p+4;
      buf->size      = packet_len-4;
      if (track & 0x8)
        buf->type      = BUF_AUDIO_DTS + track;
      else
        buf->type      = BUF_AUDIO_A52 + track;
      buf->PTS       = PTS;

      buf->input_pos = this->input->get_current_pos(this->input);

      if(this->audio_fifo)
	this->audio_fifo->put (this->audio_fifo, buf);
      else
	buf->free_buffer(buf);
      
      return ;
    } else if ((p[0]&0xf0) == 0xa0) {

      int pcm_offset;

      xprintf (VERBOSE|DEMUX,"LPCMacket, len : %d %02x\n",packet_len-4, p[0]);  

      for( pcm_offset=0; ++pcm_offset < packet_len-1 ; ){
	if ( p[pcm_offset] == 0x01 && p[pcm_offset+1] == 0x80 ) { /* START */
	  pcm_offset += 2;
	  break;
	}
      }
  
      buf->content   = p+pcm_offset;
      buf->size      = packet_len-pcm_offset;
      buf->type      = BUF_AUDIO_LPCM_BE + track;
      buf->PTS       = PTS;

      buf->input_pos = this->input->get_current_pos(this->input);

      if(this->audio_fifo)
	this->audio_fifo->put (this->audio_fifo, buf);
      else
	buf->free_buffer(buf);
      
      return ;
    }



  } else if ((stream_id >= 0xbc) && ((stream_id & 0xf0) == 0xe0)) {

    xprintf (VERBOSE|DEMUX, "video %d\n", stream_id);

    buf->content   = p;
    buf->size      = packet_len;
    buf->type      = BUF_VIDEO_MPEG;
    buf->PTS       = PTS;

    buf->input_pos = this->input->get_current_pos(this->input);

    this->video_fifo->put (this->video_fifo, buf);

    return ;

  }  else if ((stream_id & 0xe0) == 0xc0) {
    int track;

    track = stream_id & 0x1f;

    xprintf (VERBOSE|DEMUX|MPEG, "mpg audio #%d", track);

    buf->content   = p;
    buf->size      = packet_len;
    buf->type      = BUF_AUDIO_MPEG + track;
    buf->PTS       = PTS;

    buf->input_pos = this->input->get_current_pos(this->input);
      
    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);
    else
      buf->free_buffer(buf);

    return ;

  } else {
    xprintf (VERBOSE | DEMUX, "unknown packet, id = %x\n",stream_id);
  }

  buf->free_buffer (buf);

  return ;
  
}

static void *demux_mpeg_block_loop (void *this_gen) {

  buf_element_t *buf = NULL;
  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  /* printf ("demux_mpeg_block: demux loop starting...\n"); */

  this->send_end_buffers = 1;

  do {

    demux_mpeg_block_parse_pack(this, 0);

  } while (this->status == DEMUX_OK) ;

  /*
  printf ("demux_mpeg_block: demux loop finished (status: %d)\n",
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

/* estimate bitrate */

static int demux_mpeg_block_estimate_rate (demux_mpeg_block_t *this) {

  buf_element_t *buf = NULL;
  unsigned char *p;
  int            is_mpeg1=0;
  off_t          pos, last_pos;
  off_t          step;
  uint32_t       PTS, last_PTS;
  int            rate;
  int            count;

  if (!(this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE)) 
    return 0;

  pos      = 0;
  last_pos = 0;
  last_PTS = 0;
  rate     = 0;
  step     = this->input->get_length (this->input) / 10;
  step     = (step / this->blocksize) * this->blocksize;
  count    = 0;
  
  this->input->seek (this->input, 0, SEEK_SET);

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
      printf ("demux_mpeg_block: error %02x %02x %02x (should be 0x000001) \n",p[0],p[1],p[2]);
      buf->free_buffer (buf);
      return rate;
    }

    PTS = 0; 

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
	  PTS  = (p[ 0] & 0x0E) << 29 ;
	  PTS |=  p[ 1]         << 22 ;
	  PTS |= (p[ 2] & 0xFE) << 14 ;
	  PTS |=  p[ 3]         <<  7 ;
	  PTS |= (p[ 4] & 0xFE) >>  1 ;
	} 
      }
    } else { /* mpeg 2 */
      
      if (p[7] & 0x80) { /* PTS avail */
	
	PTS  = (p[ 9] & 0x0E) << 29 ;
	PTS |=  p[10]         << 22 ;
	PTS |= (p[11] & 0xFE) << 14 ;
	PTS |=  p[12]         <<  7 ;
	PTS |= (p[13] & 0xFE) >>  1 ;
	
      } else
	PTS = 0;
    }

    if (PTS) {

      if ( (pos>last_pos) && (PTS>last_PTS) ) {
	int cur_rate;
      
	cur_rate = ((pos - last_pos)*90000) / ((PTS - last_PTS) * 50);
	
	rate = (count * rate + cur_rate) / (count+1);

	count ++;
	
	/* printf ("demux_mpeg_block: cur_rate = %d, overall rate : %d\n", cur_rate, rate); */
      }

      last_pos = pos;
      last_PTS = PTS;
      pos += step;
    } else
      pos += (off_t) this->blocksize;

    buf->free_buffer (buf);

    if (this->input->seek (this->input, pos, SEEK_SET) == (off_t)-1)
      break;
  }

  return rate;
  
}

static void demux_mpeg_block_close (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  free (this);
  
}

static void demux_mpeg_block_stop (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  buf_element_t *buf;
  void *p;

  if (this->status != DEMUX_OK) {
    printf ("demux_mpeg_block: stop...ignored\n");
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

static int demux_mpeg_block_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  return this->status;
}

static void demux_mpeg_block_start (demux_plugin_t *this_gen,
				    fifo_buffer_t *video_fifo, 
				    fifo_buffer_t *audio_fifo,
				    off_t start_pos, int start_time,
				    gui_get_next_mrl_cb_t next_mrl_cb,
				    gui_branched_cb_t branched_cb) 
{

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = video_fifo;
  this->audio_fifo  = audio_fifo;
  this->next_mrl_cb = next_mrl_cb;
  this->branched_cb = branched_cb;

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

  if (!this->rate)
    this->rate = demux_mpeg_block_estimate_rate (this);


  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {

    int num_buffers = NUM_PREVIEW_BUFFERS;

    this->input->seek (this->input, 0, SEEK_SET);

    this->status = DEMUX_OK ;
    while ( (num_buffers>0) && (this->status == DEMUX_OK) ) {

      demux_mpeg_block_parse_pack(this, 1);
      num_buffers --;
    }

    if (start_pos) {
      start_pos /= (off_t) this->blocksize;
      start_pos *= (off_t) this->blocksize;

      this->input->seek (this->input, start_pos, SEEK_SET);
    } else if (start_time) {
      start_pos = start_time * this->rate * 50;
      start_pos /= (off_t) this->blocksize;
      start_pos *= (off_t) this->blocksize;

      this->input->seek (this->input, start_pos, SEEK_SET);
    }
  }

  /*
   * query CLUT from the input plugin
   */

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

  if(this->input->get_optional_data(this->input, buf->mem,
     INPUT_OPTIONAL_DATA_CLUT) == INPUT_OPTIONAL_SUCCESS) {
    buf->type = BUF_SPU_CLUT;
    buf->content = buf->mem;

    this->video_fifo->put(this->video_fifo, buf);
  } else {
    buf->free_buffer(buf);
  }

  /*
   * now start demuxing
   */

  this->status = DEMUX_OK ;

  pthread_create (&this->thread, NULL, demux_mpeg_block_loop, this) ;
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

static int demux_mpeg_block_open(demux_plugin_t *this_gen,
				 input_plugin_t *input, int stage) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  switch(stage) {

  case STAGE_BY_CONTENT: {
    
    if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      
      this->blocksize = input->get_blocksize(input);
      
      if (!this->blocksize) {

	/* detect blocksize */
	input->seek(input, 2048, SEEK_SET);
	if (!input->read(input, this->scratch, 4)) 
	  return DEMUX_CANNOT_HANDLE;

	if (this->scratch[0] || this->scratch[1] 
	    || (this->scratch[2] != 0x01) || (this->scratch[3] != 0xba)) {

	  input->seek(input, 2324, SEEK_SET);
	  if (!input->read(input, this->scratch, 4)) 
	    return DEMUX_CANNOT_HANDLE;
	  if (this->scratch[0] || this->scratch[1] 
	      || (this->scratch[2] != 0x01) || (this->scratch[3] != 0xba)) 
	    return DEMUX_CANNOT_HANDLE;
	  this->blocksize = 2324;
	  
	} else
	  this->blocksize = 2048;
      }

      input->seek(input, 0, SEEK_SET);
      if (input->read(input, this->scratch, this->blocksize)) {

	if (this->scratch[0] || this->scratch[1] 
	    || (this->scratch[2] != 0x01) || (this->scratch[3] != 0xba))
	  return DEMUX_CANNOT_HANDLE;

	/* if it's a file then make sure it's mpeg-2 */
	if ( !input->get_blocksize(input) 
	     && ((this->scratch[4]>>4) != 4) )
	  return DEMUX_CANNOT_HANDLE;

	demux_mpeg_block_accept_input (this, input);	  
	return DEMUX_CAN_HANDLE;
      }	
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;

  case STAGE_BY_EXTENSION: {
    char *media;
    char *ending;
    char *MRL;
    
    MRL = input->get_mrl (input);
    
    media = strstr(MRL, "://");
    if(media) {
      if(!strncmp(MRL, "dvd", 3) || !strncmp(MRL, "d4d", 3)
	 || (((!strncmp(MRL, "stdin", 5) || !strncmp(MRL, "fifo", 4))
	      && (!strncmp((media+3), "mpeg2", 5) ))) 
	 ) {
	this->blocksize = 2048;
	demux_mpeg_block_accept_input (this, input);	  
	return DEMUX_CAN_HANDLE;
      }
      if(!strncmp(MRL, "vcd", 3)) {
	this->blocksize = 2324;
	demux_mpeg_block_accept_input (this, input);	  
	return DEMUX_CAN_HANDLE;
      }
    } 
    
    /*
     * check ending
     */
    
    ending = strrchr(MRL, '.');
    
    xprintf(VERBOSE|DEMUX, "demux_mpeg_block_can_handle: ending %s of %s\n",
	    ending ? ending :"(none)", MRL);
    
    if(!ending)
      return DEMUX_CANNOT_HANDLE;
    
    if(!strcasecmp(ending, ".vob")) {
      this->blocksize = 2048;
      demux_mpeg_block_accept_input (this, input);	  
      return DEMUX_CAN_HANDLE;
    }
  }
  break;

  default:
    return DEMUX_CANNOT_HANDLE;
    break;
  }

  return DEMUX_CANNOT_HANDLE;
}

static char *demux_mpeg_block_get_id(void) {
  return "MPEG_BLOCK";
}

static int demux_mpeg_block_get_stream_length (demux_plugin_t *this_gen) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  if (this->rate) 
    return this->input->get_length (this->input) / (this->rate * 50);
  else
    return 0;

}

demux_plugin_t *init_demuxer_plugin(int iface, config_values_t *config) {

  demux_mpeg_block_t *this;

  if (iface != 3) {
    printf( "demux_mpeg: plugin doesn't support plugin API version %d.\n"
	    "demux_mpeg: this means there's a version mismatch between xine and this "
	    "demux_mpeg: demuxer plugin.\nInstalling current demux plugins should help.\n",
	    iface);
    return NULL;
  }

  this = xmalloc (sizeof (demux_mpeg_block_t));
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_mpeg_block_open;
  this->demux_plugin.start             = demux_mpeg_block_start;
  this->demux_plugin.stop              = demux_mpeg_block_stop;
  this->demux_plugin.close             = demux_mpeg_block_close;
  this->demux_plugin.get_status        = demux_mpeg_block_get_status;
  this->demux_plugin.get_identifier    = demux_mpeg_block_get_id;
  this->demux_plugin.get_stream_length = demux_mpeg_block_get_stream_length;
  
  this->scratch = xmalloc_aligned (512, 4096);
    
  return (demux_plugin_t *) this;
}
