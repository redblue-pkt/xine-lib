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
 * $Id: audio_decoder.c,v 1.7 2001/05/01 00:55:23 guenter Exp $
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

      /* FIXME gAD.mnCurPos = pBuf->nInputPos; */

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

      case BUF_AUDIO_AC3:
      case BUF_AUDIO_MPEG:
      case BUF_AUDIO_LPCM:
      case BUF_AUDIO_AVI:
      
	printf ("audio_decoder: got an audio buffer, type %08x\n", buf->type);

	/* update track map */

	i = 0;
	while ( (i<this->audio_track_map_entries) && (this->audio_track_map[i]<buf->type) ) 
	  i++;
	  
	if ( (i==this->audio_track_map_entries) || (this->audio_track_map[i] != buf->type) ) {

	  printf ("audio_decoder: inserting audio type %08x into track map\n", buf->type);
	  
	  j = this->audio_track_map_entries;
	  while (j>i) {
	    this->audio_track_map[j] = this->audio_track_map[j-1];
	    j--;
	  }
	  this->audio_track_map[i] = buf->type;
	  this->audio_track_map_entries++;

	  if (i<=this->audio_channel) {
	    printf ("audio_decoder: resetting audio decoder because of new channel\n");
	    
	    if (this->cur_audio_decoder_plugin) {
	      this->cur_audio_decoder_plugin->close (this->cur_audio_decoder_plugin);
	      this->cur_audio_decoder_plugin = NULL;
	    }
	  }
	}

	/* now, decode this buffer if it's the right track */
	
	if (buf->type == this->audio_track_map[this->audio_channel]) {

	  int streamtype = (buf->type>>16) & 0xFF;

	  printf ("audio_decoder_c: buffer is from the right track => decode it\n");


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

      }
    }
    buf->free_buffer (buf);
  }

  return NULL;
}

void audio_decoder_init (xine_t *this) {

  this->audio_fifo = fifo_buffer_new (1500, 4096);

  pthread_create (&this->audio_thread, NULL, audio_decoder_loop, this) ;

  printf ("audio_decoder_init: audio thread created\n");
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


