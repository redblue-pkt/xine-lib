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
 * $Id: audio_decoder.c,v 1.16 2001/06/11 03:14:29 heikos Exp $
 *
 *
 * functions that implement audio decoding
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"

void *audio_decoder_loop (void *this_gen) {

  buf_element_t   *buf;
  xine_t          *this = (xine_t *) this_gen;
  int              running = 1;
  int              i,j;
  audio_decoder_t *decoder;

  while (running) {

    buf = this->audio_fifo->get (this->audio_fifo);

    if (this->audio_out) {

      this->cur_input_pos = buf->input_pos;

      /* 
       * Call update status callback function if
       * there is no video decoder initialized, like
       *  in .mp3 playback.
       */
      if(this->cur_video_decoder_plugin == NULL) {
	if(this->status == XINE_PLAY)
	  this->status_callback (this->status);
      }

      switch (buf->type) {

      case BUF_CONTROL_START:
	if (this->cur_audio_decoder_plugin) {
	  this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
	  this->cur_audio_decoder_plugin = NULL;
	}

	pthread_mutex_lock (&this->xine_lock);
	this->audio_finished = 0;
	pthread_mutex_unlock (&this->xine_lock);

	for (i=0 ; i<50; i++)
	  this->audio_track_map[0] = 0;

	this->audio_track_map_entries = 0;

      break;

      case BUF_CONTROL_END:
	if (this->cur_audio_decoder_plugin) {
	  this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
	  this->cur_audio_decoder_plugin = NULL;
	}

	pthread_mutex_lock (&this->xine_lock);

	this->audio_finished = 1;
      
	if (this->video_finished) {
	  pthread_mutex_unlock (&this->xine_lock);
	  xine_notify_stream_finished (this);
	} else
	  pthread_mutex_unlock (&this->xine_lock);

	break;

      case BUF_CONTROL_QUIT:
	if (this->cur_audio_decoder_plugin) {
	  this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
	  this->cur_audio_decoder_plugin = NULL;
	}
	running = 0;
	break;

      default:
	if ( (buf->type & 0xFF000000) == BUF_AUDIO_BASE ) {
      
	  /* printf ("audio_decoder: got an audio buffer, type %08x\n", buf->type);  */

	  /* update track map */
	  
	  i = 0;
	  while ( (i<this->audio_track_map_entries) && (this->audio_track_map[i]<buf->type) ) 
	    i++;

	  /*
	  printf ("audio_decoder: got an audio buffer, type %08x, %d map entries, i=%d\n", 
		  buf->type, this->audio_track_map_entries, i); 
	  */
	  
	  if ( (i==this->audio_track_map_entries) || (this->audio_track_map[i] != buf->type) ) {
	    
	    j = this->audio_track_map_entries;
	    while (j>i) {
	      this->audio_track_map[j] = this->audio_track_map[j-1];
	      j--;
	    }
	    this->audio_track_map[i] = buf->type;
	    this->audio_track_map_entries++;
	    
	    if (i<=this->audio_channel) {
	      /* close old audio decoder */
	      if (this->cur_audio_decoder_plugin) {
		this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
		this->cur_audio_decoder_plugin = NULL;
	      }
	    }
	    
	  }
	  
	  /* now, decode this buffer if it's the right track */
	  
	  if (buf->type == this->audio_track_map[this->audio_channel]) {
	    
	    int streamtype = (buf->type>>16) & 0xFF;
	    
	    decoder = this->audio_decoder_plugins [streamtype];
	    
	    if (decoder) {
	      if (this->cur_audio_decoder_plugin != decoder) {
		
		if (this->cur_audio_decoder_plugin) 
		  this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
		
		this->cur_audio_decoder_plugin = decoder;
		this->cur_audio_decoder_plugin->init (this->cur_audio_decoder_plugin, this->audio_out);
		
	      }
	      
	      decoder->decode_data (decoder, buf);
	    }
	  }
	} else
	  printf ("audio_decoder: unknown buffer type: %08x\n", buf->type);
      }
    }
    else {
      /* No audio driver loaded */
      pthread_mutex_lock (&this->xine_lock);
      
      this->audio_finished = 1;
      
      if (this->video_finished) {
	pthread_mutex_unlock (&this->xine_lock);
	xine_notify_stream_finished (this);
      } else
	pthread_mutex_unlock (&this->xine_lock);
      
      pthread_exit(NULL);
    }
    
    buf->free_buffer (buf);
  }

  pthread_exit(NULL);
}

void audio_decoder_init (xine_t *this) {
  if (this->audio_out == NULL)
    return;

  this->audio_fifo = fifo_buffer_new (1500, 4096);

  pthread_create (&this->audio_thread, NULL, audio_decoder_loop, this) ;
}

void audio_decoder_stop (xine_t *this) {
  this->audio_fifo->clear(this->audio_fifo);

  if (this->cur_audio_decoder_plugin) {
    this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
    this->cur_audio_decoder_plugin = NULL;
  }
}

void audio_decoder_shutdown (xine_t *this) {

  buf_element_t *buf;
  void          *p;

  this->audio_fifo->clear(this->audio_fifo);

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_CONTROL_QUIT;
  this->audio_fifo->put (this->audio_fifo, buf);

  pthread_join (this->audio_thread, &p);
}


