/* 
 * Copyright (C) 2000 the xine project
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
 * $Id: demux_mpeg_block.c,v 1.18 2001/06/17 21:50:51 f1rmb Exp $
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

#include "xine_internal.h"
#include "monitor.h"
#include "demux.h"
#include "utils.h"

#define NUM_PREVIEW_BUFFERS 50

static uint32_t xine_debug;

typedef struct demux_mpeg_block_s {
  demux_plugin_t        demux_plugin;

  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;
  fifo_buffer_t        *spu_fifo;

  input_plugin_t       *input;

  pthread_t             thread;

  int                   status;
  
  int                   blocksize;

  int                   send_end_buffers;

  gui_get_next_mrl_cb_t next_mrl_cb;
  gui_branched_cb_t     branched_cb;
} demux_mpeg_block_t ;


static void demux_mpeg_block_parse_pack (demux_mpeg_block_t *this, int preview_mode) {

  buf_element_t *buf = NULL;
  unsigned char *p;
  int            bMpeg1=0;
  uint32_t       nHeaderLen;
  uint32_t       nPTS;
  uint32_t       nDTS;
  uint32_t       nPacketLen;
  uint32_t       nStreamID;
  

  buf = this->input->read_block (this->input, this->video_fifo, this->blocksize);

  if (buf==NULL) {
    printf ("demux_mpeg_block: read_block failed\n");
    this->status = DEMUX_FINISHED;
    return ;
  }

  p = buf->content; /* len = this->mnBlocksize; */
  if (preview_mode)
    buf->decoder_info[0] = 0;
  else
    buf->decoder_info[0] = 1;

  if (p[3] == 0xBA) { /* program stream pack header */

    int nStuffingBytes;

    xprintf (VERBOSE|DEMUX, "program stream pack header\n");

    bMpeg1 = (p[4] & 0x40) == 0;

    if (bMpeg1) {

      p   += 12;

    } else { /* mpeg2 */

      nStuffingBytes = p[0xD] & 0x07;

      xprintf (VERBOSE|DEMUX, "%d stuffing bytes\n",nStuffingBytes);

      p   += 14 + nStuffingBytes;
    }
  }


  if (p[3] == 0xbb) { /* program stream system header */
    
    int nHeaderLen;

    xprintf (VERBOSE|DEMUX, "program stream system header\n");

    nHeaderLen = (p[4] << 8) | p[5];

    p    += 6 + nHeaderLen;
  }

  /* we should now have a PES packet here */

  if (p[0] || p[1] || (p[2] != 1)) {
    fprintf (stderr, "demux error! %02x %02x %02x (should be 0x000001) \n",p[0],p[1],p[2]);
    buf->free_buffer (buf);
    return ;
  }

  nPacketLen = p[4] << 8 | p[5];
  nStreamID  = p[3];

  xprintf (VERBOSE|DEMUX, "packet id = %02x len = %d\n",nStreamID, nPacketLen);

  if (bMpeg1) {

    if (nStreamID == 0xBF) {
      buf->free_buffer (buf);
      return ;
    }

    p   += 6; /* nPacketLen -= 6; */

    while ((p[0] & 0x80) == 0x80) {
      p++; 
      nPacketLen--;
      /* printf ("stuffing\n");*/
    }

    if ((p[0] & 0xc0) == 0x40) {
      /* STD_buffer_scale, STD_buffer_size */
      p += 2;
      nPacketLen -=2;
    }

    nPTS = 0; 
    nDTS = 0;
    if ((p[0] & 0xf0) == 0x20) {
      nPTS  = (p[ 0] & 0x0E) << 29 ;
      nPTS |=  p[ 1]         << 22 ;
      nPTS |= (p[ 2] & 0xFE) << 14 ;
      nPTS |=  p[ 3]         <<  7 ;
      nPTS |= (p[ 4] & 0xFE) >>  1 ;
      p   += 5;
      nPacketLen -=5;
    } else if ((p[0] & 0xf0) == 0x30) {
      nPTS  = (p[ 0] & 0x0E) << 29 ;
      nPTS |=  p[ 1]         << 22 ;
      nPTS |= (p[ 2] & 0xFE) << 14 ;
      nPTS |=  p[ 3]         <<  7 ;
      nPTS |= (p[ 4] & 0xFE) >>  1 ;
      nDTS  = (p[ 5] & 0x0E) << 29 ;
      nDTS |=  p[ 6]         << 22 ;
      nDTS |= (p[ 7] & 0xFE) << 14 ;
      nDTS |=  p[ 8]         <<  7 ;
      nDTS |= (p[ 9] & 0xFE) >>  1 ;
      p   += 10;
      nPacketLen -= 10;
    } else {
      p++; 
      nPacketLen --;
    }

  } else { /* mpeg 2 */

    if (p[7] & 0x80) { /* PTS avail */
      
      nPTS  = (p[ 9] & 0x0E) << 29 ;
      nPTS |=  p[10]         << 22 ;
      nPTS |= (p[11] & 0xFE) << 14 ;
      nPTS |=  p[12]         <<  7 ;
      nPTS |= (p[13] & 0xFE) >>  1 ;
      
    } else
      nPTS = 0;
    
    if (p[7] & 0x40) { /* PTS avail */
      
      nDTS  = (p[14] & 0x0E) << 29 ;
      nDTS |=  p[15]         << 22 ;
      nDTS |= (p[16] & 0xFE) << 14 ;
      nDTS |=  p[17]         <<  7 ;
      nDTS |= (p[18] & 0xFE) >>  1 ;
      
    } else
      nDTS = 0;


    nHeaderLen = p[8];

    p    += nHeaderLen + 9;
    nPacketLen -= nHeaderLen + 3;
  }

  xprintf (VERBOSE|DEMUX, "stream_id=%x len=%d pts=%d dts=%d\n", nStreamID, nPacketLen, nPTS, nDTS);

  if (nStreamID == 0xbd) {

    int nTrack, nSPUID;
    
    nTrack = p[0] & 0x0F; /* hack : ac3 track */

    if((p[0] & 0xE0) == 0x20) {
      nSPUID = (p[0] & 0x1f);

      xprintf(VERBOSE|DEMUX, "SPU PES packet, id 0x%03x\n",p[0] & 0x1f);

      buf->content   = p+1;
      buf->size      = nPacketLen-1;
      buf->type      = BUF_SPU_PACKAGE + nSPUID;
      buf->PTS       = nPTS;
      buf->DTS       = nDTS ;
      buf->input_pos = this->input->get_current_pos(this->input);
      
      this->spu_fifo->put (this->spu_fifo, buf);    
      
      return;
    }

    if ((p[0]&0xF0) == 0x80) {

      xprintf (VERBOSE|DEMUX|AC3, "ac3 PES packet, track %02x\n",nTrack);
      /* printf ( "ac3 PES packet, track %02x\n",nTrack);  */

      buf->content   = p+4;
      buf->size      = nPacketLen-4;
      buf->type      = BUF_AUDIO_AC3 + nTrack;
      buf->PTS       = nPTS;
      buf->DTS       = nDTS ;
      buf->input_pos = this->input->get_current_pos(this->input);

      if(this->audio_fifo)
	this->audio_fifo->put (this->audio_fifo, buf);
      else
	buf->free_buffer(buf);
      
      return ;
    } else if ((p[0]&0xf0) == 0xa0) {

      int pcm_offset;

      xprintf (VERBOSE|DEMUX,"LPCMacket, len : %d %02x\n",nPacketLen-4, p[0]);  

      for( pcm_offset=0; ++pcm_offset < nPacketLen-1 ; ){
	if ( p[pcm_offset] == 0x01 && p[pcm_offset+1] == 0x80 ) { /* START */
	  pcm_offset += 2;
	  break;
	}
      }
  
      buf->content   = p+pcm_offset;
      buf->size      = nPacketLen-pcm_offset;
      buf->type      = BUF_AUDIO_LPCM + nTrack;
      buf->PTS       = nPTS;
      buf->DTS       = nDTS ;
      buf->input_pos = this->input->get_current_pos(this->input);

      if(this->audio_fifo)
	this->audio_fifo->put (this->audio_fifo, buf);
      else
	buf->free_buffer(buf);
      
      return ;
    }

  } else if ((nStreamID >= 0xbc) && ((nStreamID & 0xf0) == 0xe0)) {

    xprintf (VERBOSE|DEMUX, "video %d\n", nStreamID);

    buf->content   = p;
    buf->size      = nPacketLen;
    buf->type      = BUF_VIDEO_MPEG;
    buf->PTS       = nPTS;
    buf->DTS       = nDTS;
    buf->input_pos = this->input->get_current_pos(this->input);

    this->video_fifo->put (this->video_fifo, buf);

    return ;

  }  else if ((nStreamID & 0xe0) == 0xc0) {
    int nTrack;

    nTrack = nStreamID & 0x1f;

    xprintf (VERBOSE|DEMUX|MPEG, "mpg audio #%d", nTrack);

    buf->content   = p;
    buf->size      = nPacketLen;
    buf->type      = BUF_AUDIO_MPEG + nTrack;
    buf->PTS       = nPTS;
    buf->DTS       = nDTS;
    buf->input_pos = this->input->get_current_pos(this->input);
      
    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);
    else
      buf->free_buffer(buf);

    return ;

  } else {
    xprintf (VERBOSE | DEMUX, "unknown packet, id = %x\n",nStreamID);
  }

  buf->free_buffer (buf);

  return ;
  
}

static void *demux_mpeg_block_loop (void *this_gen) {

  buf_element_t *buf = NULL;
  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  printf ("demux_mpeg_block: demux loop starting...\n");

  this->send_end_buffers = 1;

  do {

    demux_mpeg_block_parse_pack(this, 0);
    
  } while (this->status == DEMUX_OK) ;

  printf ("demux_mpeg_block: demux loop finished (status: %d)\n",
	  this->status);

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

static void demux_mpeg_block_stop (demux_plugin_t *this_gen) {
  
  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  void *p;
  buf_element_t *buf;

  printf ("demux_mpeg_block: stop(...)\n");
  
  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

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
				    fifo_buffer_t *spu_fifo,
				    off_t pos,
				    gui_get_next_mrl_cb_t next_mrl_cb,
				    gui_branched_cb_t branched_cb) 
{

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = video_fifo;
  this->audio_fifo  = audio_fifo;
  this->spu_fifo    = spu_fifo;
  this->next_mrl_cb = next_mrl_cb;
  this->branched_cb = branched_cb;

  pos /= (off_t) this->blocksize;
  pos *= (off_t) this->blocksize;

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

  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {

    int num_buffers = NUM_PREVIEW_BUFFERS;

    this->input->seek (this->input, 0, SEEK_SET);

    this->status = DEMUX_OK ;
    while ( (num_buffers>0) && (this->status == DEMUX_OK) ) {

      demux_mpeg_block_parse_pack(this, 1);
      num_buffers --;
    }

    xprintf (VERBOSE|DEMUX, "=>seek to %Ld\n",pos);
    this->input->seek (this->input, pos, SEEK_SET);
  }

  /*
   * now start demuxing
   */

  this->status = DEMUX_OK ;

  pthread_create (&this->thread, NULL, demux_mpeg_block_loop, this) ;
}

static int demux_mpeg_block_open(demux_plugin_t *this_gen,
				 input_plugin_t *input, int stage) {

  demux_mpeg_block_t *this = (demux_mpeg_block_t *) this_gen;

  switch(stage) {

  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];
    
    if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      
      this->blocksize = input->get_blocksize(input);
      
      if (!this->blocksize) {

	/* detect blocksize */
	input->seek(input, 2048, SEEK_SET);
	if (!input->read(input, buf, 4)) 
	  return DEMUX_CANNOT_HANDLE;

	if(buf[0] || buf[1] || (buf[2] != 0x01) || (buf[3] != 0xba)) {

	  input->seek(input, 2324, SEEK_SET);
	  if (!input->read(input, buf, 4)) 
	    return DEMUX_CANNOT_HANDLE;
	  if(buf[0] || buf[1] || (buf[2] != 0x01) || (buf[3] != 0xba)) 
	    return DEMUX_CANNOT_HANDLE;
	  this->blocksize = 2324;
	  
	} else
	  this->blocksize = 2048;
      }

      /* make sure it's mpeg-2 */

      input->seek(input, 0, SEEK_SET);
      if (input->read(input, buf, this->blocksize)) {

	{
	  int i=0,j=0;

	  while(i<this->blocksize) {
	    if(buf[i] && !j) {printf("***%d\n", i); j++;}
	    i++;
	  }
	}
	
	if(buf[0] || buf[1] || (buf[2] != 0x01) || (buf[3] != 0xba))
	  return DEMUX_CANNOT_HANDLE;

	if ((buf[4]>>4) != 4)
	  return DEMUX_CANNOT_HANDLE;
	  
	this->input = input;
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
      if(!strncmp(MRL, "dvd", 3)
	 || (((!strncmp(MRL, "stdin", 5) || !strncmp(MRL, "fifo", 4))
	      && (!strncmp((media+3), "mpeg2", 5) ))) 
	 ) {
	this->blocksize = 2048;
	this->input = input;
	return DEMUX_CAN_HANDLE;
      }
      if(!strncmp(MRL, "vcd", 3)) {
	this->blocksize = 2324;
	this->input = input;
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
      this->input = input;
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

static void demux_mpeg_block_close (demux_plugin_t *this) {
  /* nothing */
}

demux_plugin_t *init_demuxer_plugin(int iface, config_values_t *config) {

  demux_mpeg_block_t *this = xmalloc (sizeof (demux_mpeg_block_t));

  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  switch (iface) {

  case 1:

    this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
    this->demux_plugin.open              = demux_mpeg_block_open;
    this->demux_plugin.start             = demux_mpeg_block_start;
    this->demux_plugin.stop              = demux_mpeg_block_stop;
    this->demux_plugin.close             = demux_mpeg_block_close;
    this->demux_plugin.get_status        = demux_mpeg_block_get_status;
    this->demux_plugin.get_identifier    = demux_mpeg_block_get_id;
    
    return (demux_plugin_t *) this;
    break;
    
  default:
    fprintf(stderr,
	    "Demuxer plugin doesn't support plugin API version %d.\n"
	    "PLUGIN DISABLED.\n"
	    "This means there's a version mismatch between xine and this "
	    "demuxer plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }
}
