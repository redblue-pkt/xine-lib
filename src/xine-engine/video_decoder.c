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
 * $Id: video_decoder.c,v 1.81 2002/04/09 03:38:01 miguelfreitas Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xine_internal.h"
#include "xineutils.h"
#include <sched.h>

/*
#define VIDEO_DECODER_LOG
*/

static spu_decoder_t* update_spu_decoder(xine_t *this, int type) {
  int streamtype = (type>>16) & 0xFF;
  spu_decoder_t *spu_decoder = this->spu_decoder_plugins [streamtype];

  if (spu_decoder && this->cur_spu_decoder_plugin != spu_decoder) {

    if (this->cur_spu_decoder_plugin)
      this->cur_spu_decoder_plugin->close (this->cur_spu_decoder_plugin);

    this->cur_spu_decoder_plugin = spu_decoder;

    this->cur_spu_decoder_plugin->init (this->cur_spu_decoder_plugin,
                                        this->video_out);
  }
  return spu_decoder;
}

void *video_decoder_loop (void *this_gen) {

  buf_element_t   *buf;
  xine_t          *this = (xine_t *) this_gen;
  int              running = 1;
  int              streamtype;
  video_decoder_t *decoder;
  spu_decoder_t   *spu_decoder;
  static int	   prof_video_decode = -1;
  static int	   prof_spu_decode = -1;

  if (prof_video_decode == -1)
    prof_video_decode = xine_profiler_allocate_slot ("video decoder");
  if (prof_spu_decode == -1)
    prof_spu_decode = xine_profiler_allocate_slot ("spu decoder");

  while (running) {

#ifdef VIDEO_DECODER_LOG
    printf ("video_decoder: getting buffer...\n");  
#endif

    buf = this->video_fifo->get (this->video_fifo);

    if (buf->input_pos)
      this->cur_input_pos = buf->input_pos;
    if (buf->input_time)
      this->cur_input_time = buf->input_time;

#ifdef VIDEO_DECODER_LOG
    printf ("video_decoder: got buffer 0x%08x\n", buf->type);      
#endif

    switch (buf->type & 0xffff0000) {
    case BUF_CONTROL_START:

      if (this->cur_video_decoder_plugin) {
	this->cur_video_decoder_plugin->close (this->cur_video_decoder_plugin);
	this->cur_video_decoder_plugin = NULL;
      }

      if (this->cur_spu_decoder_plugin) {
        this->cur_spu_decoder_plugin->close (this->cur_spu_decoder_plugin);
        this->cur_spu_decoder_plugin = NULL;
      }

      pthread_mutex_lock (&this->finished_lock);
      this->video_finished = 0;
      this->spu_finished = 0;

      pthread_mutex_unlock (&this->finished_lock);
 
      this->metronom->handle_video_discontinuity (this->metronom, DISC_STREAMSTART, 0);

      break;

    case BUF_SPU_SUBP_CONTROL:
    case BUF_SPU_CLUT:
    case BUF_SPU_PACKAGE:
    case BUF_SPU_TEXT:
    case BUF_SPU_NAV:
      xine_profiler_start_count (prof_spu_decode);

      spu_decoder = update_spu_decoder(this, buf->type);

      if (spu_decoder) {
        spu_decoder->decode_data (spu_decoder, buf);
      }

      xine_profiler_stop_count (prof_spu_decode);
      break;

    case BUF_CONTROL_SPU_CHANNEL:
      {
	xine_ui_event_t  ui_event;
	
	this->spu_channel_auto = buf->decoder_info[0];
	if (this->spu_channel_user == -1)
	  this->spu_channel = this->spu_channel_auto;
	
	/* Inform UI of SPU channel changes */
	ui_event.event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
	ui_event.data       = NULL;
	xine_send_event(this, &ui_event.event);
	
      }
      break;

    case BUF_CONTROL_END:

      if (this->cur_video_decoder_plugin) {
	this->cur_video_decoder_plugin->close (this->cur_video_decoder_plugin);
	this->cur_video_decoder_plugin = NULL;
      }
      if (this->cur_spu_decoder_plugin) {
        this->cur_spu_decoder_plugin->close (this->cur_spu_decoder_plugin);
        this->cur_spu_decoder_plugin = NULL;
      }
      pthread_mutex_lock (&this->finished_lock);
      this->spu_finished = 1;

      if (!this->video_finished && (buf->decoder_flags & BUF_FLAG_END_STREAM )) {

        this->video_finished = 1;

        if (this->audio_finished) {
          xine_notify_stream_finished (this);
        }
      }

      pthread_mutex_unlock (&this->finished_lock);

      break;

    case BUF_CONTROL_QUIT:
      if (this->cur_video_decoder_plugin) {
	this->cur_video_decoder_plugin->close (this->cur_video_decoder_plugin);
	this->cur_video_decoder_plugin = NULL;
      }
      if (this->cur_spu_decoder_plugin) {
        this->cur_spu_decoder_plugin->close (this->cur_spu_decoder_plugin);
        this->cur_spu_decoder_plugin = NULL;
      }

      running = 0;
      break;

    case BUF_CONTROL_RESET_DECODER:
      if (this->cur_video_decoder_plugin) {
        this->cur_video_decoder_plugin->reset (this->cur_video_decoder_plugin);
      }
      break;
    
    case BUF_CONTROL_DISCONTINUITY:
      printf ("video_decoder: discontinuity ahead\n");

      this->video_in_discontinuity = 1;

      this->metronom->handle_video_discontinuity (this->metronom, DISC_RELATIVE, buf->disc_off);
      
      this->video_in_discontinuity = 0;
      break;
    
    case BUF_CONTROL_NEWPTS:
      printf ("video_decoder: new pts %lld\n", buf->disc_off);

      this->video_in_discontinuity = 1;

      this->metronom->handle_video_discontinuity (this->metronom, DISC_ABSOLUTE, buf->disc_off);
      
      this->video_in_discontinuity = 0;
      break;
    
    case BUF_CONTROL_AUDIO_CHANNEL:
      {
	xine_ui_event_t  ui_event;
	/* Inform UI of AUDIO channel changes */
	ui_event.event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
	ui_event.data       = NULL;
	xine_send_event(this, &ui_event.event);
      }
      break;

    case BUF_CONTROL_NOP:
      break;

    default:
      xine_profiler_start_count (prof_video_decode);

      if ( (buf->type & 0xFF000000) == BUF_VIDEO_BASE ) {

	/*
	  printf ("video_decoder: got package %d, decoder_info[0]:%d\n", 
	  buf, buf->decoder_info[0]);
	*/      
	
	streamtype = (buf->type>>16) & 0xFF;
	
	decoder = this->video_decoder_plugins [streamtype];
	
	if (decoder) {
	  
	  if (this->cur_video_decoder_plugin != decoder) {
	    
	    if (this->cur_video_decoder_plugin) {
	      this->cur_video_decoder_plugin->close (this->cur_video_decoder_plugin);
	      printf ("video_decoder: closing old decoder >%s<\n",this->cur_video_decoder_plugin->get_identifier());
	    }
	    
	    this->cur_video_decoder_plugin = decoder;
	    this->cur_video_decoder_plugin->init (this->cur_video_decoder_plugin, this->video_out);
	    
	    printf ("video_decoder: using decoder >%s< \n",
		    decoder->get_identifier());

	    xine_log (this, XINE_LOG_FORMAT, "using video decoder plugin '%s'\n",
		      decoder->get_identifier());
	    
	  }

	  decoder->decode_data (this->cur_video_decoder_plugin, buf);  
	}
      } else
	printf ("video_decoder: unknown buffer type: %08x\n", buf->type);

      xine_profiler_stop_count (prof_video_decode);

      break;

    }

    buf->free_buffer (buf);
  }

  pthread_exit(NULL);
}

void video_decoder_init (xine_t *this) {
  
  pthread_attr_t       pth_attrs;
  struct sched_param   pth_params;
  int		       err;

  this->video_fifo = fifo_buffer_new (500, 8192);

  pthread_attr_init(&pth_attrs);
  pthread_attr_getschedparam(&pth_attrs, &pth_params);
  pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
  pthread_attr_setschedparam(&pth_attrs, &pth_params);
  pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
  
  if ((err = pthread_create (&this->video_thread,
			     &pth_attrs, video_decoder_loop, this)) != 0) {
    fprintf (stderr, "video_decoder: can't create new thread (%s)\n",
	     strerror(err));
    exit (1);
  }

  this->video_in_discontinuity = 0;
}

void video_decoder_shutdown (xine_t *this) {

  buf_element_t *buf;
  void          *p;

  /* this->video_fifo->clear(this->video_fifo); */

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type = BUF_CONTROL_QUIT;
  this->video_fifo->put (this->video_fifo, buf);

  pthread_join (this->video_thread, &p);
}

