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
 * $Id: demux_elem.c,v 1.21 2001/10/03 17:15:43 jkeil Exp $
 *
 * demultiplexer for elementary mpeg streams
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "xine_internal.h"
#include "monitor.h"
#include "demux.h"

#ifndef	__GNUC__
#define	__FUNCTION__	__func__
#endif

#define NUM_PREVIEW_BUFFERS 50

#define DEMUX_MPEG_ELEM_IFACE_VERSION 1

typedef struct {  

  demux_plugin_t   demux_plugin;

  fifo_buffer_t   *video_fifo;
  fifo_buffer_t   *audio_fifo;

  input_plugin_t  *input;

  pthread_t        thread;

  int              blocksize;
  int              status;
  
  int              send_end_buffers;

  uint8_t          scratch[4096];
} demux_mpeg_elem_t ;

static uint32_t xine_debug;

/*
 *
 */
static int demux_mpeg_elem_next (demux_mpeg_elem_t *this, int preview_mode) {
  buf_element_t *buf;

  buf = this->input->read_block(this->input, 
				this->video_fifo, this->blocksize);

  if (buf == NULL) {
    this->status = DEMUX_FINISHED;
    return 0;
  }

  if(preview_mode)
    buf->decoder_info[0] = 0;
  else
    buf->decoder_info[0] = 1;

  buf->PTS             = 0;
  buf->input_pos       = this->input->get_current_pos(this->input);
  buf->type            = BUF_VIDEO_MPEG;

  this->video_fifo->put(this->video_fifo, buf);
  
  return (buf->size == this->blocksize);
}

/*
 *
 */
static void *demux_mpeg_elem_loop (void *this_gen) {
  buf_element_t *buf = NULL;
  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  this->send_end_buffers = 1;

  do {

    if (!demux_mpeg_elem_next(this, 0))
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

/*
 *
 */
static void demux_mpeg_elem_stop (demux_plugin_t *this_gen) {

  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;
  buf_element_t     *buf = NULL;
  void *p;

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

/*
 *
 */
static int demux_mpeg_elem_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  return this->status;
}

/*
 *
 */
static void demux_mpeg_elem_start (demux_plugin_t *this_gen,
				   fifo_buffer_t *video_fifo, 
				   fifo_buffer_t *audio_fifo,
				   off_t start_pos, int start_time,
				   gui_get_next_mrl_cb_t next_mrl_cb,
				   gui_branched_cb_t branched_cb) {

  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;
  buf_element_t *buf;
  int err;
  
  this->video_fifo  = video_fifo;
  this->audio_fifo  = audio_fifo;
  
  this->status = DEMUX_OK;
  
  this->blocksize = this->input->get_blocksize(this->input);
  if (!this->blocksize)
    this->blocksize = 2048;

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
    while ((num_buffers > 0) && (this->status == DEMUX_OK)) {
      demux_mpeg_elem_next(this, 1);
      num_buffers--;
    }

    /* FIXME: implement time seek */

    xprintf (VERBOSE|DEMUX, "=>seek to %Ld\n",start_pos);
    this->input->seek (this->input, start_pos, SEEK_SET);
  }

  /*
   * now start demuxing
   */

  if ((err = pthread_create (&this->thread,
			     NULL, demux_mpeg_elem_loop, this)) != 0) {
    fprintf (stderr, "demux_elem: can't create new thread (%s)\n",
	     strerror(err));
    exit (1);
  }
}

/*
 *
 */
static int demux_mpeg_elem_open(demux_plugin_t *this_gen,
				input_plugin_t *input, int stage) {

  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  switch(stage) {
    
  case STAGE_BY_CONTENT: {
    int bs = 4;
    
    if(!input)
      return DEMUX_CANNOT_HANDLE;
  
    if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      input->seek(input, 0, SEEK_SET);

      bs = input->get_blocksize(input);

      if (bs<4)
	bs = 4;

      if (input->read(input, this->scratch, bs) == bs) {
	/*
	printf ("demux_elem: %02x %02x %02x %02x (bs=%d)\n",
		this->scratch[0], this->scratch[1], 
		this->scratch[2], this->scratch[3], bs);
	*/
	
	if (this->scratch[0] || this->scratch[1] 
	    || (this->scratch[2] != 0x01) || (this->scratch[3] != 0xb3))
	  return DEMUX_CANNOT_HANDLE;
	
	this->input = input;
	return DEMUX_CAN_HANDLE;
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
    
    if(suffix) {
      if(!strcasecmp(suffix, ".mpv")) {
	this->input = input;
	return DEMUX_CAN_HANDLE;
      }
    }

    return DEMUX_CANNOT_HANDLE;
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
static char *demux_mpeg_elem_get_id(void) {
  return "MPEG_ELEM";
}

static void demux_mpeg_elem_close (demux_plugin_t *this) {
  /* nothing */
}

static int demux_mpeg_elem_get_stream_length(demux_plugin_t *this_gen) {
  return 0 ; /*FIXME: implement */
}
/*
 *
 */
demux_plugin_t *init_demuxer_plugin(int iface, config_values_t *config) {

  demux_mpeg_elem_t *this;

  if (iface != 3) {
    printf( "demux_elem: plugin doesn't support plugin API version %d.\n"
	    "demux_elem: this means there's a version mismatch between xine and this "
	    "demux_elem: demuxer plugin.\nInstalling current demux plugins should help.\n",
	    iface);
    return NULL;
  }

  this = malloc (sizeof (demux_mpeg_elem_t));
  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  this->demux_plugin.interface_version = DEMUX_MPEG_ELEM_IFACE_VERSION;
  this->demux_plugin.open              = demux_mpeg_elem_open;
  this->demux_plugin.start             = demux_mpeg_elem_start;
  this->demux_plugin.stop              = demux_mpeg_elem_stop;
  this->demux_plugin.close             = demux_mpeg_elem_close;
  this->demux_plugin.get_status        = demux_mpeg_elem_get_status;
  this->demux_plugin.get_identifier    = demux_mpeg_elem_get_id;
  this->demux_plugin.get_stream_length = demux_mpeg_elem_get_stream_length;
  
  return &this->demux_plugin;
}

