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
 * $Id: buffer.c,v 1.6 2001/08/12 15:12:54 guenter Exp $
 *
 *
 * contents:
 *
 * buffer_entry structure - serves as a transport encapsulation
 *   of the mpeg audio/video data through xine
 *
 * free buffer pool management routines
 *
 * FIFO buffer structures/routines
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "buffer.h"
#include "utils.h"

/*
 * put a previously allocated buffer element back into the buffer pool
 */
static void buffer_pool_free (buf_element_t *element) {

  fifo_buffer_t *this = (fifo_buffer_t *) element->source;

  pthread_mutex_lock (&this->buffer_pool_mutex);

  element->next = this->buffer_pool_top;
  this->buffer_pool_top = element;

  this->buffer_pool_num_free++;

  pthread_cond_signal (&this->buffer_pool_cond_not_empty);

  pthread_mutex_unlock (&this->buffer_pool_mutex);
}

/* 
 * helper function to release buffer pool lock
 * in case demux thread is cancelled
 */

void pool_release_lock (void *arg) {
   
  pthread_mutex_t *mutex = (pthread_mutex_t *) arg;

  /* printf ("pool release lock\n"); */

  pthread_mutex_unlock (mutex);

}

/*
 * allocate a buffer from buffer pool
 */

static buf_element_t *buffer_pool_alloc (fifo_buffer_t *this) {
  
  buf_element_t *buf;

  pthread_cleanup_push( pool_release_lock, &this->buffer_pool_mutex);

  pthread_mutex_lock (&this->buffer_pool_mutex);

  while (!this->buffer_pool_top) {
    pthread_cond_wait (&this->buffer_pool_cond_not_empty, &this->buffer_pool_mutex);
  }

  buf = this->buffer_pool_top;
  this->buffer_pool_top = this->buffer_pool_top->next;
  this->buffer_pool_num_free--;

  pthread_cleanup_pop (0); 

  pthread_mutex_unlock (&this->buffer_pool_mutex);

  return buf;
}

/*
 * append buffer element to fifo buffer
 */
static void fifo_buffer_put (fifo_buffer_t *fifo, buf_element_t *element) {
  
  pthread_mutex_lock (&fifo->mutex);

  if (fifo->last) 
    fifo->last->next = element;
  else 
    fifo->first = element;

  fifo->last  = element;
  element->next = NULL;

  pthread_cond_signal (&fifo->not_empty);

  pthread_mutex_unlock (&fifo->mutex);
}

/*
 * get element from fifo buffer
 */
static buf_element_t *fifo_buffer_get (fifo_buffer_t *fifo) {

  buf_element_t *buf;
  
  pthread_mutex_lock (&fifo->mutex);

  while (fifo->first==NULL) {
    pthread_cond_wait (&fifo->not_empty, &fifo->mutex);
  }

  buf = fifo->first;

  fifo->first = fifo->first->next;
  if (fifo->first==NULL)
    fifo->last = NULL;

  pthread_mutex_unlock (&fifo->mutex);

  return buf;
}

/*
 * clear buffer (put all contained buffer elements back into buffer pool)
 */
static void fifo_buffer_clear (fifo_buffer_t *fifo) {
  
  buf_element_t *buf, *next, *prev;

  pthread_mutex_lock (&fifo->mutex);

  buf = fifo->first;
  prev = NULL;

  while (buf != NULL) {

    next = buf->next;

    if ((buf->type & BUF_MAJOR_MASK) !=  BUF_CONTROL_BASE) {
      /* remove this buffer */

      if (prev)
	prev->next = next;
      else
	fifo->first = next;
      
      if (!next)
	fifo->last = prev;
      
      buf->free_buffer(buf);
    } else
      prev = buf;
    
    buf = next;
  }
  /*
  while (fifo->first != NULL) {

    buf = fifo->first;

    fifo->first = fifo->first->next;
    if (fifo->first==NULL)
      fifo->last = NULL;

    buf->free_buffer(buf);
  }
  */

  pthread_mutex_unlock (&fifo->mutex);
}

/*
 * allocate and initialize new (empty) fifo buffer
 */
fifo_buffer_t *fifo_buffer_new (int num_buffers, uint32_t buf_size) {

  fifo_buffer_t *this;
  int            i;
  int            alignment = 2048;
  char          *multi_buffer = NULL;

  this = xmalloc (sizeof (fifo_buffer_t));

  this->first           = NULL;
  this->last            = NULL;
  this->put             = fifo_buffer_put;
  this->get             = fifo_buffer_get;
  this->clear           = fifo_buffer_clear;

  pthread_mutex_init (&this->mutex, NULL);
  pthread_cond_init (&this->not_empty, NULL);

  /*
   * init buffer pool, allocate nNumBuffers of buf_size bytes each 
   */


  buf_size += buf_size % alignment;

  /*
  printf ("Allocating %d buffers of %ld bytes in one chunk (alignment = %d)\n", 
	  num_buffers, (long int) buf_size, alignment);
	  */
  multi_buffer = xmalloc_aligned (alignment, num_buffers * buf_size);

  this->buffer_pool_top = NULL;

  pthread_mutex_init (&this->buffer_pool_mutex, NULL);
  pthread_cond_init (&this->buffer_pool_cond_not_empty, NULL);

  for (i = 0; i<num_buffers; i++) {
    buf_element_t *buf;

    buf = xmalloc (sizeof (buf_element_t));

    buf->mem = multi_buffer;
    multi_buffer += buf_size;

    buf->max_size    = buf_size;
    buf->free_buffer = buffer_pool_free;
    buf->source      = this;
    
    buffer_pool_free (buf);
  }
  this->buffer_pool_num_free = num_buffers;
  this->buffer_pool_alloc    = buffer_pool_alloc;

  return this;
}



