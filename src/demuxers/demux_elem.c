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
 * $Id: demux_elem.c,v 1.5 2001/04/29 23:22:32 f1rmb Exp $
 *
 * demultiplexer for elementary mpeg streams
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

#define DEMUX_MPEG_ELEM_IFACE_VERSION 1

typedef struct {  

  demux_plugin_t   demux_plugin;

  fifo_buffer_t   *video_fifo;
  fifo_buffer_t   *audio_fifo;

  input_plugin_t  *input;

  pthread_t        thread;

  int              blocksize;
  int              status;

} demux_mpeg_elem_t ;

static uint32_t xine_debug;

/*
 *
 */
static int demux_mpeg_elem_next (demux_mpeg_elem_t *this) {
  buf_element_t *buf;

  buf = this->input->read_block(this->input, 
				this->video_fifo, this->blocksize);

  if (buf == NULL) {
    this->status = DEMUX_FINISHED;
    return 0;
  }

  buf->content   = buf->mem;
  buf->DTS       = 0;
  buf->PTS       = 0;
  buf->size      = this->input->read(this->input, buf->mem, this->blocksize);
  buf->input_pos = this->input->seek(this->input, 0, SEEK_CUR);
  buf->type      = BUF_VIDEO_MPEG;

  this->video_fifo->put(this->video_fifo, buf);

  return (buf->size == this->blocksize);
}

/*
 *
 */
static void *demux_mpeg_elem_loop (void *this_gen) {
  buf_element_t *buf;
  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  do {

    if (!demux_mpeg_elem_next(this))
      this->status = DEMUX_FINISHED;

  } while (this->status == DEMUX_OK) ;

  xprintf (VERBOSE|DEMUX, "demux loop finished (status: %d)\n", this->status);

  this->status = DEMUX_FINISHED;

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type    = BUF_CONTROL_END;
  this->video_fifo->put (this->video_fifo, buf);

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type    = BUF_CONTROL_END;
  this->audio_fifo->put (this->audio_fifo, buf);

  return NULL;
}

/*
 *
 */
static void demux_mpeg_elem_stop (demux_plugin_t *this_gen) {
  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;
  void *p;

  this->status = DEMUX_FINISHED;

  pthread_join (this->thread, &p);
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
				    fifo_buffer_t *spu_fifo,
				    off_t pos) {
  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;
  buf_element_t *buf;
  
  this->video_fifo  = video_fifo;
  this->audio_fifo  = audio_fifo;
  
  this->status = DEMUX_OK;
  
  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    xprintf (VERBOSE|DEMUX, "=>seek to %Ld\n",pos);
    this->input->seek (this->input, pos, SEEK_SET);
  }
  
  this->blocksize = 2048;

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type    = BUF_CONTROL_START;
  this->video_fifo->put (this->video_fifo, buf);
  buf = this->audio_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type    = BUF_CONTROL_START;
  this->audio_fifo->put (this->audio_fifo, buf);

  /*
   * now start demuxing
   */

  pthread_create (&this->thread, NULL, demux_mpeg_elem_loop, this) ;
}

/*
 *
 */
static int demux_mpeg_elem_open(demux_plugin_t *this_gen,
			       input_plugin_t *input, int stage) {

  demux_mpeg_elem_t *this = (demux_mpeg_elem_t *) this_gen;

  switch(stage) {
    
  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];
    int bs = 0;
    
    if(!input)
      return DEMUX_CANNOT_HANDLE;
  
    if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      input->seek(input, 0, SEEK_SET);

      if(input->get_blocksize)
	bs = input->get_blocksize(input);
      
      bs = (bs > 4) ? bs : 4;

      if(input->read(input, buf, bs)) {
	
	if(buf[0] || buf[1] || (buf[2] != 0x01))
	  return DEMUX_CANNOT_HANDLE;
	
	switch(buf[3]) {
	case 0xb3:
	  this->input = input;
	  return DEMUX_CAN_HANDLE;
	  break;
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

/*
 *
 */
demux_plugin_t *init_demuxer_plugin(int iface, config_values_t *config) {

  demux_mpeg_elem_t *this = malloc (sizeof (demux_mpeg_elem_t));

  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  switch (iface) {

  case 1:

    this->demux_plugin.interface_version = DEMUX_MPEG_ELEM_IFACE_VERSION;
    this->demux_plugin.open              = demux_mpeg_elem_open;
    this->demux_plugin.start             = demux_mpeg_elem_start;
    this->demux_plugin.stop              = demux_mpeg_elem_stop;
    this->demux_plugin.close             = demux_mpeg_elem_close;
    this->demux_plugin.get_status        = demux_mpeg_elem_get_status;
    this->demux_plugin.get_identifier    = demux_mpeg_elem_get_id;
    
    return &this->demux_plugin;
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

