/*
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: demux_cda.c,v 1.5 2002/01/02 18:16:07 jkeil Exp $
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
#include "xineutils.h"
#include "compat.h"
#include "demux.h"

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_DEMUX, message, ##args);                 \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_DEMUX, message, ##args);                 \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_DEMUX, __VA_ARGS__);                     \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_DEMUX, __VA_ARGS__);                     \
    printf(__VA_ARGS__);                                             \
  }
#endif

#define DEMUX_CDA_IFACE_VERSION 3

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_t              *xine;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  pthread_t            thread;

  off_t                start;

  int                  status;
  int                  send_end_buffers;
  int                  blocksize;

} demux_cda_t ;

/*
 *
 */
static int demux_cda_next (demux_cda_t *this) {
  buf_element_t *buf;
  int            pos, len;
  
  buf = this->input->read_block(this->input, this->video_fifo, this->blocksize);
  
  pos = this->input->get_current_pos(this->input);
  len = this->input->get_length(this->input);
  
  buf->PTS             = 0;
  buf->SCR             = 0;
  buf->input_pos       = pos;
  buf->input_time      = buf->input_pos / this->blocksize;
  buf->type            = BUF_VIDEO_FILL; /* Fake */
  
  //  if(this->audio_fifo)
  this->video_fifo->put(this->video_fifo, buf);
  
  return ((pos < len));
}

/*
 *
 */
static void *demux_cda_loop (void *this_gen) {
  demux_cda_t    *this = (demux_cda_t *) this_gen;
  buf_element_t  *buf;

  this->send_end_buffers = 1;

  this->input->seek(this->input, this->start, SEEK_SET);

  do {

    xine_usec_sleep(100000);

    if (!demux_cda_next(this))
      this->status = DEMUX_FINISHED;

  } while (this->status == DEMUX_OK) ;

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
static void demux_cda_stop (demux_plugin_t *this_gen) {
  demux_cda_t    *this = (demux_cda_t *) this_gen;
  buf_element_t  *buf;
  void           *p;
  
  if (this->status != DEMUX_OK) {
    LOG_MSG(this->xine, _("demux_cda: stop...ignored\n"));
    return;
  }

  /* Force stop */  
  this->input->stop(this->input);
  
  this->send_end_buffers = 0;
  this->status           = DEMUX_FINISHED;
  
  pthread_cancel(this->thread);
  pthread_join(this->thread, &p);

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
static int demux_cda_get_status (demux_plugin_t *this_gen) {
  demux_cda_t *this = (demux_cda_t *) this_gen;
  
  return this->status;
}

/*
 *
 */
static void demux_cda_start (demux_plugin_t *this_gen,
			     fifo_buffer_t *video_fifo, 
			     fifo_buffer_t *audio_fifo,
			     off_t start_pos, int start_time) {
  demux_cda_t    *this = (demux_cda_t *) this_gen;
  buf_element_t  *buf;
  int             err;

  this->video_fifo = video_fifo;
  this->audio_fifo = audio_fifo;
  
  this->status     = DEMUX_OK;
  this->start      = start_pos;
  
  this->blocksize  = this->input->get_blocksize(this->input);

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
  
  if ((err = pthread_create (&this->thread,
			     NULL, demux_cda_loop, this)) != 0) {
    LOG_MSG_STDERR(this->xine, _("demux_cda: can't create new thread (%s)\n"), strerror(err));
    exit(1);
  }
}

/*
 *
 */
static int demux_cda_open(demux_plugin_t *this_gen, input_plugin_t *input, int stage) {
  demux_cda_t *this = (demux_cda_t *) this_gen;

  switch(stage) {
    
  case STAGE_BY_CONTENT:
    return DEMUX_CANNOT_HANDLE;
    break;
    
  case STAGE_BY_EXTENSION: {
    char *media;
    char *MRL = input->get_mrl(input);
    
    media = strstr(MRL, "://");
    if(media) {
      if(!strncasecmp(MRL, "cda", 3)) {
	this->input = input;
	return DEMUX_CAN_HANDLE;
      }
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
static char *demux_cda_get_id(void) {
  return "CDA";
}

/*
 *
 */
static char *demux_cda_get_mimetypes(void) {
  return "audio/cda: CD Audio";
}

/*
 *
 */
static void demux_cda_close (demux_plugin_t *this) {

}

/*
 *
 */
static int demux_cda_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

/*
 *
 */
demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {
  demux_cda_t *this;
  
  if (iface != 6) {
    LOG_MSG(xine,
	    _("demux_cda: plugin doesn't support plugin API version %d.\n"
	      "           this means there's a version mismatch between xine and this "
	      "           demuxer plugin.\nInstalling current demux plugins should help.\n"),
	    iface);
    return NULL;
  }

  this         = (demux_cda_t *) xine_xmalloc(sizeof(demux_cda_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_plugin.interface_version = DEMUX_CDA_IFACE_VERSION;
  this->demux_plugin.open              = demux_cda_open;
  this->demux_plugin.start             = demux_cda_start;
  this->demux_plugin.stop              = demux_cda_stop;
  this->demux_plugin.close             = demux_cda_close;
  this->demux_plugin.get_status        = demux_cda_get_status;
  this->demux_plugin.get_identifier    = demux_cda_get_id;
  this->demux_plugin.get_stream_length = demux_cda_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_cda_get_mimetypes;
  
  return &this->demux_plugin;
}
