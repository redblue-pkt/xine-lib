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
 * $Id: spu_decoder.c,v 1.2 2001/06/18 15:43:01 richwareham Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"

#include "libspudec/spudec.h"

void *spu_decoder_loop (void *this_gen) {

  buf_element_t   *buf;
  xine_t          *this = (xine_t *) this_gen;
  int              running = 1;
  int              streamtype;
  spudec_t        *decoder;

  decoder = this->spu_decoder;
  while (running) {

    /* printf ("video_decoder: getting buffer...\n"); */

    buf = this->spu_fifo->get (this->spu_fifo);
    if (buf->input_pos)
      this->cur_input_pos = buf->input_pos;

    /* printf ("spu_decoder: got buffer %d\n", buf->type); */

    switch (buf->type) {
    case BUF_CONTROL_START:
      break;

    case BUF_CONTROL_END:
      break;

    case BUF_CONTROL_QUIT:
      running = 0;
      break;

    default:
      if ( (buf->type & 0xFF000000) == BUF_SPU_BASE ) {
	int stream_id;

	printf ("spu_decoder: got an SPU buffer, type %08x\n", buf->type); 

	stream_id = buf->type & 0xFF;

	if(decoder) {
	  decoder->push_packet(decoder, buf);
	}
      }
    }

    buf->free_buffer (buf);
  }

  pthread_exit(NULL);
}

void spu_decoder_init (xine_t *this) {
  buf_element_t *buf;

  this->spu_decoder = spudec_init(this); 

  this->spu_fifo = fifo_buffer_new (1500, 4096);

  buf = this->spu_fifo->buffer_pool_alloc (this->spu_fifo);
  buf->type = BUF_CONTROL_START;
  this->spu_fifo->put (this->spu_fifo, buf);

  pthread_create (&this->spu_thread, NULL, spu_decoder_loop, this) ;
}

void spu_decoder_shutdown (xine_t *this) {

  buf_element_t *buf;
  void          *p;

  this->spu_fifo->clear(this->spu_fifo);

  buf = this->spu_fifo->buffer_pool_alloc (this->spu_fifo);
  buf->type = BUF_CONTROL_QUIT;
  this->spu_fifo->put (this->spu_fifo, buf);

  spudec_close(this->spu_decoder);
  this->spu_decoder = NULL;

  pthread_join (this->spu_thread, &p);
}

