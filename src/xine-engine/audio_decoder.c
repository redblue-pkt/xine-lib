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
 * $Id: audio_decoder.c,v 1.58 2002/02/09 07:13:24 guenter Exp $
 *
 *
 * functions that implement audio decoding
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>

#include "xine_internal.h"
#include "xineutils.h"


void *audio_decoder_loop (void *this_gen) {

  buf_element_t   *buf;
  xine_t          *this = (xine_t *) this_gen;
  int              running = 1;
  audio_decoder_t *decoder;
  static int	   prof_audio_decode = -1;

  if (prof_audio_decode == -1)
    prof_audio_decode = xine_profiler_allocate_slot ("audio decoder/output");

  while (running) {

#ifdef AUDIO_DECODER_LOG
    printf ("audio_loop: waiting for package...\n");  
#endif

    buf = this->audio_fifo->get (this->audio_fifo);

    
#ifdef AUDIO_DECODER_LOG
    printf ("audio_loop: got package pts = %d, type = %08x\n", 
	    buf->PTS, buf->type); 
#endif    

    if (buf->input_pos)
      this->cur_input_pos = buf->input_pos;
    
    if (buf->input_time)
      this->cur_input_time = buf->input_time;
    
    switch (buf->type) {
      
    case BUF_CONTROL_START:
      if (this->cur_audio_decoder_plugin) {
	this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
	this->cur_audio_decoder_plugin = NULL;
	this->audio_type = 0;
      }
      
      pthread_mutex_lock (&this->finished_lock);
      this->audio_finished = 0;
      pthread_mutex_unlock (&this->finished_lock);

      this->metronom->expect_audio_discontinuity (this->metronom);
      
      break;
      
    case BUF_CONTROL_END:

      if (this->cur_audio_decoder_plugin) {
	this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
	this->cur_audio_decoder_plugin = NULL;
	this->audio_type = 0;
      }
      
      pthread_mutex_lock (&this->finished_lock);

      if (!this->audio_finished && (buf->decoder_info[0]==0)) {
        this->audio_finished = 1;

        if (this->video_finished) {
          xine_notify_stream_finished (this);
        }
      }

      pthread_mutex_unlock (&this->finished_lock);

      this->audio_channel_auto = -1;

      /* future magic - coming soon
      lrb_flush (this->audio_temp);
      */

      break;
      
    case BUF_CONTROL_QUIT:
      if (this->cur_audio_decoder_plugin) {
	this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
	this->cur_audio_decoder_plugin = NULL;
	this->audio_type = 0;
      }
      running = 0;
      break;

    case BUF_CONTROL_NOP:
      break;

    case BUF_CONTROL_DISCONTINUITY:
      printf ("audio_decoder: discontinuity ahead\n");

      this->metronom->expect_audio_discontinuity (this->metronom);
      break;

    case BUF_CONTROL_AUDIO_CHANNEL:
      {
	printf ("audio_decoder: suggested switching to stream_id %02x\n",
		buf->decoder_info[0]);
	this->audio_channel_auto = buf->decoder_info[0] & 0xff;
      }
      break;

    default:

      xine_profiler_start_count (prof_audio_decode);

      if ( (buf->type & 0xFF000000) == BUF_AUDIO_BASE ) {
	
	uint32_t audio_type = 0;
	int      i,j;

	/*
        printf("audio_decoder: buf_type=%08x auto=%08x user=%08x\n",
	       buf->type, 
	       this->audio_channel_auto,
	       this->audio_channel_user);
	       */

        /* update track map */
        
        i = 0;
        while ( (i<this->audio_track_map_entries) && (this->audio_track_map[i]<buf->type) ) 
          i++;
        
        if ( (i==this->audio_track_map_entries) || (this->audio_track_map[i] != buf->type) ) {
          
          j = this->audio_track_map_entries;
          while (j>i) {
            this->audio_track_map[j] = this->audio_track_map[j-1];
            j--;
          }
          this->audio_track_map[i] = buf->type;
          this->audio_track_map_entries++;
        }

	/* find out which audio type to decode */

	if (this->audio_channel_user > -2) {

	  if (this->audio_channel_user == -1) {

	    /* auto */

	    if (this->audio_channel_auto>=0) {
 
	      if ((buf->type & 0xFF) == this->audio_channel_auto) {
		audio_type = buf->type;
	      } else
		audio_type = -1;

	    } else
	      audio_type = this->audio_track_map[0];

	  } else 
	    audio_type = this->audio_track_map[this->audio_channel_user];

	  /* now, decode this buffer if it's the right audio type */
	  
	  if (buf->type == audio_type) {
	    
	    int streamtype = (buf->type>>16) & 0xFF;
	    
	    decoder = this->audio_decoder_plugins [streamtype];
	    
	    /* close old decoder of audio type has changed */
	    
	    if (audio_type != this->audio_type) {
	      
	      if (this->cur_audio_decoder_plugin) {
		this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
		this->cur_audio_decoder_plugin = NULL;
	      }
	      
	      if (decoder) {
		xine_event_t event;
		printf ("audio_loop: using decoder >%s< \n",
			decoder->get_identifier());
		this->cur_audio_decoder_plugin = decoder;
		this->cur_audio_decoder_plugin->init (this->cur_audio_decoder_plugin, this->audio_out);
		
		this->audio_type = audio_type;

		event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
		xine_send_event(this, &event);
	      }
	    }
	    
	    /* finally - decode data */
	    
	    if (decoder) 
	    decoder->decode_data (decoder, buf);
	  }
	} 
      } else
	printf ("audio_loop: unknown buffer type: %08x\n", buf->type);

      xine_profiler_stop_count (prof_audio_decode);
    }
    
    buf->free_buffer (buf);
  }

  pthread_exit(NULL);
}

void audio_decoder_init (xine_t *this) {

  pthread_attr_t       pth_attrs;
  struct sched_param   pth_params;
  int                  err;

  if (this->audio_out == NULL) {
    this->audio_finished = 1;    
    this->audio_fifo     = NULL;
    return;
  }
  
  this->audio_fifo = fifo_buffer_new (50, 8192);
  this->audio_channel_user = -1;
  this->audio_channel_auto = 0;
  this->audio_type = 0;

  /* future magic - coming soon
  this->audio_temp = lrb_new (100, this->audio_fifo);
  */

  pthread_attr_init(&pth_attrs);
  pthread_attr_getschedparam(&pth_attrs, &pth_params);
  pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
  pthread_attr_setschedparam(&pth_attrs, &pth_params);
  pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
  
  if ((err = pthread_create (&this->audio_thread,
			     &pth_attrs, audio_decoder_loop, this)) != 0) {
    fprintf (stderr, "audio_decoder: can't create new thread (%s)\n",
	     strerror(err));
    exit (1);
  }
}

void audio_decoder_shutdown (xine_t *this) {

  buf_element_t *buf;
  void          *p;

  if (this->audio_fifo) {
    /* this->audio_fifo->clear(this->audio_fifo); */

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_CONTROL_QUIT;
    this->audio_fifo->put (this->audio_fifo, buf);
    
    pthread_join (this->audio_thread, &p); 
  }
  
  if(this->audio_out)
    this->audio_out->exit (this->audio_out);

}

int xine_get_audio_channel (xine_t *this) {

  return this->audio_type & 0xFFFF; 
}

void xine_select_audio_channel (xine_t *this, int channel) {

  pthread_mutex_lock (&this->xine_lock);

  if (channel < -2)
    channel = -2;

  this->audio_channel_user = channel;

  pthread_mutex_unlock (&this->xine_lock);
}

int xine_get_audio_selection (xine_t *this) {

  return this->audio_channel_user;
}
