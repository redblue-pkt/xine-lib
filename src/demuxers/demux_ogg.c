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
 * $Id: demux_ogg.c,v 1.4 2001/10/17 20:33:09 guenter Exp $
 *
 * demultiplexer for ogg streams
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

#include <ogg/ogg.h>

#include "xine_internal.h"
#include "monitor.h"
#include "demux.h"
#include "utils.h"

#define CHUNKSIZE 8500

#define MAX_STREAMS 16

static uint32_t xine_debug;

typedef struct demux_ogg_s {
  demux_plugin_t        demux_plugin;

  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  pthread_t             thread;

  int                   status;
  
  int                   send_end_buffers;

  ogg_sync_state        oy;
  ogg_stream_state      os;
  ogg_page              og;

  ogg_stream_state      oss[MAX_STREAMS];
  uint32_t              buf_types[MAX_STREAMS];
  int                   num_streams;
} demux_ogg_t ;

static void demux_ogg_send_package (demux_ogg_t *this, int is_content) {

  int i;
  int stream_num = -1;
  int cur_serno;
  
  char *buffer;
  long bytes;

  ogg_packet op;
  
  int ret = ogg_sync_pageout(&this->oy,&this->og);
  
  /* printf("demux_ogg: pageout: %d\n", ret); */
  
  if (ret == 0) {
    buffer = ogg_sync_buffer(&this->oy, CHUNKSIZE);
    bytes  = this->input->read(this->input, buffer, CHUNKSIZE);
    
    if (bytes < CHUNKSIZE) {
      this->status = DEMUX_FINISHED;
      return;
    }
    
    ogg_sync_wrote(&this->oy, bytes);
  } else if (ret > 0) {
    /* now we've got at least one new page */
    
    cur_serno = ogg_page_serialno (&this->og);
    
    if (ogg_page_bos(&this->og)) {
      printf("demux_ogg: beginning of stream\n");
      printf("demux_ogg: serial number %d\n",
	     ogg_page_serialno (&this->og));
    }
    
    for (i = 0; i<this->num_streams; i++) {
      if (this->oss[i].serialno == cur_serno) {
	stream_num = i;
	break;
      }
    }
    
    if (stream_num < 0) {
      ogg_stream_init(&this->oss[this->num_streams], cur_serno);
      stream_num = this->num_streams;
      this->buf_types[stream_num] = 0;
      
      printf("demux_ogg: found a new stream, serialnumber %d\n", cur_serno);
      
      this->num_streams++;
    }
    
    ogg_stream_pagein(&this->oss[stream_num], &this->og);
    
    while (ogg_stream_packetout(&this->oss[stream_num], &op) == 1) {
      /* printf("demux_ogg: packet: %.8s\n", op.packet); */
      /* printf("demux_ogg:   got a packet\n"); */
      
      if (!this->buf_types[stream_num]) {
	/* detect buftype */
	if (!strncmp (&op.packet[1], "vorbis", 6)) {
	  this->buf_types[stream_num] = BUF_AUDIO_VORBIS;
	} else {
	  printf ("demux_ogg: unknown streamtype, signature: >%.8s<\n",
		  op.packet);
	  this->buf_types[stream_num] = BUF_CONTROL_NOP;
	}
      }
      
      if ( this->audio_fifo 
	   && (this->buf_types[stream_num] & 0xFF000000) == BUF_AUDIO_BASE) {
	buf_element_t *buf;
	
	buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
	
	buf->content = buf->mem;
	
	{
	  int op_size = sizeof(op);
	  ogg_packet *og_ghost;
	  op_size += (4 - (op_size % 4));
	  
	  /* nasty hack to pack op as well as (vorbis) content
	     in one xine buffer */
	  memcpy (buf->content + op_size, op.packet, op.bytes);
	  memcpy (buf->content, &op, sizeof(op));
	  og_ghost = (ogg_packet *) buf->content;
	  og_ghost->packet = buf->content + op_size;
	  
	}
	
	buf->PTS    = 0; /* FIXME */
	buf->size   = op.bytes;
	
	buf->decoder_info[0] = is_content;

	buf->input_pos  = this->input->get_current_pos (this->input);
	buf->input_time = 0;
	
	buf->type = this->buf_types[stream_num];
	
	this->audio_fifo->put (this->audio_fifo, buf);
      }
    }
  }
}

static void *demux_ogg_loop (void *this_gen) {
  
  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  buf_element_t *buf;

  /* printf ("demux_ogg: demux loop starting...\n"); */

  this->send_end_buffers = 1;

  while (this->status == DEMUX_OK) {
    demux_ogg_send_package (this, 0);
  }

  /*
    printf ("demux_ogg: demux loop finished (status: %d)\n",
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

static void demux_ogg_close (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  free (this);
  
}

static void demux_ogg_stop (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  buf_element_t *buf;
  void *p;

  if (this->status != DEMUX_OK) {
    printf ("demux_ogg: stop...ignored\n");
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

static int demux_ogg_get_status (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  return this->status;
}

static void demux_ogg_start (demux_plugin_t *this_gen,
			     fifo_buffer_t *video_fifo, 
			     fifo_buffer_t *audio_fifo,
			     off_t start_pos, int start_time,
			     gui_get_next_mrl_cb_t next_mrl_cb,
			     gui_branched_cb_t branched_cb) 
{

  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  buf_element_t *buf;
  int err, i;

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
   * initialize ogg engine
   */

  ogg_sync_init(&this->oy);

  this->num_streams = 0;

  this->input->seek (this->input, 0, SEEK_SET);

  /* send header */
  for (i=0; i<5; i++) 
    demux_ogg_send_package (this, 0);


  /*
   * seek to start position
   */

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {

    off_t cur_pos = this->input->get_current_pos (this->input);

    /*
    if ( (!start_pos) && (start_time))
      start_pos = start_time * this->rate;
    */

    if (start_pos<cur_pos)
      start_pos = cur_pos;

    this->input->seek (this->input, start_pos, SEEK_SET);
  }

  /*
   * now start demuxing
   */

  this->status = DEMUX_OK;

  if ((err = pthread_create (&this->thread,
			     NULL, demux_ogg_loop, this)) != 0) {
    fprintf (stderr, "demux_ogg: can't create new thread (%s)\n",
	     strerror(err));
    exit (1);
  }
}

static int demux_ogg_open(demux_plugin_t *this_gen,
			  input_plugin_t *input, int stage) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;

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
    
    if(!strcasecmp(ending, ".ogg")) {
      this->input = input;
      return DEMUX_CAN_HANDLE;
    }
  }
  break;
  }

  return DEMUX_CANNOT_HANDLE;
}

static char *demux_ogg_get_id(void) {
  return "OGG";
}

static int demux_ogg_get_stream_length (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  return 0;
}

demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {

  demux_ogg_t     *this;
  config_values_t *config;

  if (iface != 4) {
    printf( "demux_ogg: plugin doesn't support plugin API version %d.\n"
	    "demux_ogg: this means there's a version mismatch between xine and this "
	    "demux_ogg: demuxer plugin.\nInstalling current demux plugins should help.\n",
	    iface);
    return NULL;
  }

  this        = xmalloc (sizeof (demux_ogg_t));
  config      = xine->config;
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_ogg_open;
  this->demux_plugin.start             = demux_ogg_start;
  this->demux_plugin.stop              = demux_ogg_stop;
  this->demux_plugin.close             = demux_ogg_close;
  this->demux_plugin.get_status        = demux_ogg_get_status;
  this->demux_plugin.get_identifier    = demux_ogg_get_id;
  this->demux_plugin.get_stream_length = demux_ogg_get_stream_length;
  
  return (demux_plugin_t *) this;
}
