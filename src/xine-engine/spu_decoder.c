/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
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
 * $Id: spu_decoder.c,v 1.5 2001/07/08 18:15:54 guenter Exp $
 *
 
 * functions that implement spu decoding
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"

void *spu_decoder_loop (void *this_gen) {

  buf_element_t   *buf;
  xine_t          *this = (xine_t *) this_gen;
  int              running = 1;
  int              i;
  spu_decoder_t *decoder;

  while (running) {

    buf = this->spu_fifo->get (this->spu_fifo);

    this->cur_input_pos = buf->input_pos;

    switch (buf->type) {

    case BUF_CONTROL_START:
      if (this->cur_spu_decoder_plugin) {
	this->cur_spu_decoder_plugin->close (this->cur_spu_decoder_plugin);
	this->cur_spu_decoder_plugin = NULL;
      }

      pthread_mutex_lock (&this->xine_lock);
      this->spu_finished = 0;
      pthread_mutex_unlock (&this->xine_lock);

/* FIXME: I don't think we need spu_track_map. */
      for (i=0 ; i<50; i++)
	this->spu_track_map[0] = 0;

      this->spu_track_map_entries = 0;

      break;

    case BUF_CONTROL_END:
      if (this->cur_spu_decoder_plugin) {
	this->cur_spu_decoder_plugin->close (this->cur_spu_decoder_plugin);
	this->cur_spu_decoder_plugin = NULL;
      }

      pthread_mutex_lock (&this->xine_lock);

      this->spu_finished = 1;
      
      if (this->video_finished) {
	pthread_mutex_unlock (&this->xine_lock);
	xine_notify_stream_finished (this);
      } else
	pthread_mutex_unlock (&this->xine_lock);

      break;

    case BUF_CONTROL_QUIT:
      if (this->cur_spu_decoder_plugin) {
	this->cur_spu_decoder_plugin->close (this->cur_spu_decoder_plugin);
	this->cur_spu_decoder_plugin = NULL;
      }
      running = 0;
      break;

    default:
      if ( (buf->type & 0xFF000000) == BUF_SPU_BASE ) {

	  
	/* now, decode this buffer if it's the right track */
	  
	if ( (buf->type  & 0xFFFF)== this->spu_channel) {
	    
	  int streamtype = (buf->type>>16) & 0xFF;
	  decoder = this->spu_decoder_plugins [streamtype];
	  if (decoder) {
	    if (this->cur_spu_decoder_plugin != decoder) {
		
	      if (this->cur_spu_decoder_plugin) 
		this->cur_spu_decoder_plugin->close (this->cur_spu_decoder_plugin);
		
	      this->cur_spu_decoder_plugin = decoder;
		
	      this->cur_spu_decoder_plugin->init (this->cur_spu_decoder_plugin, this->video_out);  
	    }
	      
	    decoder->decode_data (decoder, buf);
	  }
	}
      } else
	fprintf (stderr,"spu_decoder: unknown buffer type: %08x\n", buf->type);
    }
    
    buf->free_buffer (buf);
  }

  pthread_exit(NULL);
}

void spu_decoder_init (xine_t *this) {

  this->spu_fifo = fifo_buffer_new (1500, 4096);
	  printf ("spu_decoder_init: thread starting %p\n",this->video_out);

  pthread_create (&this->spu_thread, NULL, spu_decoder_loop, this) ;
}

void spu_decoder_shutdown (xine_t *this) {

  buf_element_t *buf;
  void          *p;

  /* this->spu_fifo->clear(this->spu_fifo); */

  buf = this->spu_fifo->buffer_pool_alloc (this->spu_fifo);
  buf->type = BUF_CONTROL_QUIT;
  this->spu_fifo->put (this->spu_fifo, buf);

  pthread_join (this->spu_thread, &p);
}


