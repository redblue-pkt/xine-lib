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
 * $Id: video_decoder.c,v 1.59 2001/10/21 12:09:06 jcdutton Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xine_internal.h"
#include "monitor.h"
#include <sched.h>

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
    prof_video_decode = profiler_allocate_slot ("video decoder");
  if (prof_spu_decode == -1)
    prof_spu_decode = profiler_allocate_slot ("spu decoder");

  while (running) {

    /* printf ("video_decoder: getting buffer...\n");  */

    buf = this->video_fifo->get (this->video_fifo);
    if (buf->input_pos)
      this->cur_input_pos = buf->input_pos;
    if (buf->input_time)
      this->cur_input_time = buf->input_time;

    /* printf ("video_decoder: got buffer %d\n", buf->type);    */

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

      this->metronom->video_stream_start (this->metronom);

      break;

    case BUF_SPU_CLUT:
      profiler_start_count (prof_spu_decode);

      spu_decoder = update_spu_decoder(this, buf->type);

      if (spu_decoder)
        spu_decoder->decode_data (spu_decoder, buf);

      profiler_stop_count (prof_spu_decode);
      break;

    case BUF_SPU_SUBP_CONTROL:
      profiler_start_count (prof_spu_decode);

      spu_decoder = update_spu_decoder(this, buf->type);

      if (spu_decoder)
        spu_decoder->decode_data (spu_decoder, buf);

      profiler_stop_count (prof_spu_decode);
      break;

    case BUF_CONTROL_SPU_CHANNEL:
      this->spu_channel = buf->decoder_info[0];
      break;

    case BUF_SPU_PACKAGE:
      profiler_start_count (prof_spu_decode);

      /* now, decode this buffer if it's the right track */
//      if ( (buf->type  & 0xFFFF)== this->spu_channel) {

        spu_decoder = update_spu_decoder (this, buf->type);

        if (spu_decoder)
          spu_decoder->decode_data (spu_decoder, buf);

//      }

      profiler_stop_count (prof_spu_decode);
      break;

    case BUF_CONTROL_END:

      this->metronom->video_stream_end (this->metronom);

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

      if (!this->video_finished && (buf->decoder_info[0]==0)) {

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

    case BUF_CONTROL_DISCONTINUITY:
      this->metronom->expect_video_discontinuity (this->metronom);
      break;

    case BUF_CONTROL_NOP:
      break;

    default:
      profiler_start_count (prof_video_decode);

      if ( (buf->type & 0xFF000000) == BUF_VIDEO_BASE ) {

	/*
	  printf ("video_decoder: got package %d, decoder_info[0]:%d\n", 
	  buf, buf->decoder_info[0]);
	*/      
	
	streamtype = (buf->type>>16) & 0xFF;
	
	decoder = this->video_decoder_plugins [streamtype];
	
	if (decoder) {
	  
	  if (this->cur_video_decoder_plugin != decoder) {
	    
	    if (this->cur_video_decoder_plugin) 
	      this->cur_video_decoder_plugin->close (this->cur_video_decoder_plugin);
	    
	    this->cur_video_decoder_plugin = decoder;
	    this->cur_video_decoder_plugin->init (this->cur_video_decoder_plugin, this->video_out);
	    
	    printf ("video_decoder: using decoder >%s< \n",
		    decoder->get_identifier());
	    
	  }

	  decoder->decode_data (this->cur_video_decoder_plugin, buf);  
	}
      } else
	printf ("video_decoder: unknown buffer type: %08x\n", buf->type);

      profiler_stop_count (prof_video_decode);

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

  this->video_fifo = fifo_buffer_new (500, 4096);

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
}

void video_decoder_shutdown (xine_t *this) {

  buf_element_t *buf;
  void          *p;

  /* this->video_fifo->clear(this->video_fifo); */

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type = BUF_CONTROL_QUIT;
  this->video_fifo->put (this->video_fifo, buf);

  pthread_join (this->video_thread, &p);

  this->video_out->exit (this->video_out);
}

