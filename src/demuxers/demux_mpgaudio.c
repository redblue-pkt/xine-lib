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
 * $Id: demux_mpgaudio.c,v 1.15 2001/07/14 12:50:34 guenter Exp $
 *
 * demultiplexer for mpeg audio (i.e. mp3) streams
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

#define DEMUX_MPGAUDIO_IFACE_VERSION 1

typedef struct {

  demux_plugin_t       demux_plugin;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  pthread_t            thread;

  int                  status;

  int                  send_end_buffers;
} demux_mpgaudio_t ;

static uint32_t xine_debug;

static int mpg123_head_check(unsigned long head) {
  if ((head & 0xffe00000) != 0xffe00000)
    return 0;
  if (!((head >> 17) & 3))
    return 0;
  if (((head >> 12) & 0xf) == 0xf)
    return 0;
  if (!((head >> 12) & 0xf))
    return 0;
  if (((head >> 10) & 0x3) == 0x3)
    return 0;
  if (((head >> 19) & 1) == 1 
      && ((head >> 17) & 3) == 3 
      && ((head >> 16) & 1) == 1)
    return 0;
  if ((head & 0xffff0000) == 0xfffe0000)
    return 0;
  
  return 1;
}

static int demux_mpgaudio_next (demux_mpgaudio_t *this) {

  buf_element_t *buf = NULL;
  
  if(this->audio_fifo)
    buf = this->input->read_block(this->input, 
				  this->audio_fifo, 2048);

  if (buf == NULL) {
    this->status = DEMUX_FINISHED;
    return 0;
  }

  buf->DTS             = 0;
  buf->PTS             = 0;
  buf->input_pos       = this->input->get_current_pos(this->input);
  buf->type            = BUF_AUDIO_MPEG;
  buf->decoder_info[0] = 1;

  if(this->audio_fifo)
    this->audio_fifo->put(this->audio_fifo, buf);

  return (buf->size == 2048);
}

static void *demux_mpgaudio_loop (void *this_gen) {
  buf_element_t *buf;
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  this->send_end_buffers = 1;

  do {

    if (!demux_mpgaudio_next(this))
      this->status = DEMUX_FINISHED;

  } while (this->status == DEMUX_OK) ;

  xprintf (VERBOSE|DEMUX, "demux loop finished (status: %d)\n", this->status);

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
}

static void demux_mpgaudio_stop (demux_plugin_t *this_gen) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;
  void *p;
  buf_element_t *buf;

  if (this->status != DEMUX_OK) {
    printf ("demux_mpgaudio_block: stop...ignored\n");
    return;
  }

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

static int demux_mpgaudio_get_status (demux_plugin_t *this_gen) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  return this->status;
}

static void demux_mpgaudio_start (demux_plugin_t *this_gen,
				  fifo_buffer_t *video_fifo, 
				  fifo_buffer_t *audio_fifo,
				  off_t pos,
				  gui_get_next_mrl_cb_t next_mrl_cb,
				  gui_branched_cb_t branched_cb) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = video_fifo;
  this->audio_fifo  = audio_fifo;
  
  this->status = DEMUX_OK;
  
  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    xprintf (VERBOSE|DEMUX, "=>seek to %Ld\n",pos);
    this->input->seek (this->input, pos, SEEK_SET);
  }
  
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type    = BUF_CONTROL_START;
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type    = BUF_CONTROL_START;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
  
  /*
   * now start demuxing
   */

  pthread_create (&this->thread, NULL, demux_mpgaudio_loop, this) ;
}

static int demux_mpgaudio_open(demux_plugin_t *this_gen,
			       input_plugin_t *input, int stage) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  switch(stage) {
    
  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];
    uint32_t head;
    int bs = 0;
    
    if(!input)
      return DEMUX_CANNOT_HANDLE;

    if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      input->seek(input, 0, SEEK_SET);

      if (input->get_capabilities (input) & INPUT_CAP_BLOCK) 
	bs = input->get_blocksize(input);
      
      if(!bs) 
	bs = 4;

      if(input->read(input, buf, bs)) {

	head = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
	  
	if (mpg123_head_check(head)) {
	  this->input = input;
	  return DEMUX_CAN_HANDLE;
	}
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;
  
  case STAGE_BY_EXTENSION: {
    char *suffix;
    char *MRL;
    
    MRL = input->get_mrl (input);
    
    suffix = strrchr(MRL, '.');
    xprintf(VERBOSE|DEMUX, "%s: suffix %s of %s\n", __FUNCTION__, suffix, MRL);
    
    if(!suffix)
      return DEMUX_CANNOT_HANDLE;
    
    if(!strcasecmp(suffix, ".mp3") 
       || (!strcasecmp(suffix, ".mp2"))) {
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

/*
 *
 */
static char *demux_mpgaudio_get_id(void) {
  return "MPGAUDIO";
}

static void demux_mpgaudio_close (demux_plugin_t *this) {
  /* nothing */
}

demux_plugin_t *init_demuxer_plugin(int iface, config_values_t *config) {

  demux_mpgaudio_t *this;

  if (iface != 2) {
    printf( "demux_mpeg: plugin doesn't support plugin API version %d.\n"
	    "demux_mpeg: this means there's a version mismatch between xine and this "
	    "demux_mpeg: demuxer plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }

  this = malloc (sizeof (demux_mpgaudio_t));
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  this->demux_plugin.interface_version = DEMUX_MPGAUDIO_IFACE_VERSION;
  this->demux_plugin.open              = demux_mpgaudio_open;
  this->demux_plugin.start             = demux_mpgaudio_start;
  this->demux_plugin.stop              = demux_mpgaudio_stop;
  this->demux_plugin.close             = demux_mpgaudio_close;
  this->demux_plugin.get_status        = demux_mpgaudio_get_status;
  this->demux_plugin.get_identifier    = demux_mpgaudio_get_id;
  
  return &this->demux_plugin;
}

