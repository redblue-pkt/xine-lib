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
 * $Id: read_cache.c,v 1.8 2001/11/19 22:31:35 miguelfreitas Exp $
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "xineutils.h"

#include "read_cache.h"

#define NUM_BUFFERS 512
#define NUM_MACRO_BUFFERS 32

typedef struct macro_buf_s macro_buf_t;

struct macro_buf_s {

  macro_buf_t  *next;

  int           ref;
  off_t         adr;

  uint8_t      *data;
  int		size_valid;	/* amount of valid data bytes in 'data' */
  read_cache_t *source;
};

struct read_cache_s {

  int              fd;

  macro_buf_t     *mbuf_pool_top;
  char            *multi_base;
  buf_element_t   *buf_pool_top;

  macro_buf_t     *cur_mbuf;

  pthread_mutex_t  lock;
  pthread_cond_t   buf_pool_not_empty;
  pthread_cond_t   mbuf_pool_not_empty;
};

/* 
 * helper function to release buffer pool lock
 * in case demux thread is cancelled
 */

static void cache_release_lock (void *arg) {
   
  pthread_mutex_t *mutex = (pthread_mutex_t *) arg;

  /* printf ("pool release lock\n"); */

  pthread_mutex_unlock (mutex);

}

static void buf_free (buf_element_t *buf) {
  
  macro_buf_t  *mbuf = (macro_buf_t *) buf->source;
  read_cache_t *this = mbuf->source;
  
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);

  pthread_cleanup_push( cache_release_lock, &this->lock);

  pthread_mutex_lock (&this->lock);

  /* free buffer */

  buf->next = this->buf_pool_top;
  this->buf_pool_top = buf;

  pthread_cond_signal (&this->buf_pool_not_empty);

  /* maybe free mbuf too */

  mbuf->ref--;
  
  if (!mbuf->ref && (mbuf != this->cur_mbuf)) {
    
    mbuf->next = this->mbuf_pool_top;
    this->mbuf_pool_top = mbuf;

    pthread_cond_signal (&this->mbuf_pool_not_empty);
  }

  pthread_cleanup_pop (0); 
  pthread_mutex_unlock (&this->lock);
  /* needed because cancellation points defined by POSIX
     (eg. 'read') would leak allocated buffers */
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);
}

read_cache_t *read_cache_new () {

  read_cache_t *this;
  int           i;
  char         *multi_buffer = NULL;
  int           buf_size;

  this = (read_cache_t *) xine_xmalloc (sizeof (read_cache_t));

  pthread_mutex_init (&this->lock, NULL);
  pthread_cond_init (&this->buf_pool_not_empty, NULL);
  pthread_cond_init (&this->mbuf_pool_not_empty, NULL);

  /* init buffer pool */

  this->buf_pool_top = NULL;
  for (i = 0; i<NUM_BUFFERS; i++) {
    buf_element_t *buf;
    
    buf = xine_xmalloc (sizeof (buf_element_t));

    buf->max_size    = 2048;
    buf->free_buffer = buf_free;
    
    buf->next = this->buf_pool_top;
    this->buf_pool_top = buf;
  }

  /* init macro buffer pool */

  buf_size = NUM_MACRO_BUFFERS * 2048 * 16;
  buf_size += 2048; /* alignment space */
  this->multi_base = xine_xmalloc (buf_size);
  multi_buffer = this->multi_base;
  while ((int) multi_buffer % 2048)
    multi_buffer++;

  this->mbuf_pool_top = NULL;
  for (i = 0; i<NUM_MACRO_BUFFERS; i++) {
    macro_buf_t *mbuf;
    
    mbuf = xine_xmalloc (sizeof (macro_buf_t));

    mbuf->data   = (uint8_t *)multi_buffer;
    multi_buffer += 2048*16;
    mbuf->source = this;

    mbuf->next = this->mbuf_pool_top;
    this->mbuf_pool_top = mbuf;
  }

  return this;
}

void read_cache_set_fd (read_cache_t *this, int fd) {
  this->fd = fd;
}


buf_element_t *read_cache_read_block (read_cache_t *this,
				      off_t pos) {

  macro_buf_t   *mbuf;
  buf_element_t *buf;
  off_t          madr;
  int            badr;
  int		 bytes_read;

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);

  pthread_cleanup_push( cache_release_lock, &this->lock);

  pthread_mutex_lock (&this->lock);

  /* address calculations */

  madr = pos & (~ (off_t) 0x7FFF);
  badr = pos & ((off_t) 0x7FFF);

  /* find or create macroblock that contains this block */

  if ( !this->cur_mbuf || (this->cur_mbuf->adr != madr) ||
       (this->cur_mbuf->size_valid <= badr) ) {

    if (this->cur_mbuf && (!this->cur_mbuf->ref)) {

      mbuf = this->cur_mbuf;

    } else {

      this->cur_mbuf = NULL;
      
      while (this->mbuf_pool_top==NULL) {
	pthread_cond_wait (&this->mbuf_pool_not_empty, &this->lock);
      }

      mbuf = this->mbuf_pool_top;

      this->mbuf_pool_top = this->mbuf_pool_top->next;
    }

    mbuf->adr = madr;
    mbuf->ref = 0;
    mbuf->size_valid = 0;
    
    this->cur_mbuf = mbuf;

    if (lseek (this->fd, madr, SEEK_SET) < 0) {
      fprintf(stderr, "read_cache: can't seek to offset %lld (%s)\n",
	      (long long)madr, strerror (errno));
    } else {
      pthread_testcancel();
      if ((bytes_read = read (this->fd, mbuf->data, 2048*16)) != 2048*16) {
	if (bytes_read < 0) /* reading encrypted dvd without authentication? */
	  fprintf(stderr, "read_cache: read error (%s)\n", strerror (errno));
	else
	  fprintf(stderr, "read_cache: short read (%d != %d)\n", bytes_read, 2048*16);
      }
      mbuf->size_valid = bytes_read;
    }

  } else {
    mbuf = this->cur_mbuf;
  }

  /* check for read errors */
  if ( badr > mbuf->size_valid ) {

    buf = NULL;

  } else {

    /* create buf */

    while (this->buf_pool_top==NULL) {
      pthread_cond_wait (&this->buf_pool_not_empty, &this->lock);
    }

    buf = this->buf_pool_top;

    this->buf_pool_top = this->buf_pool_top->next;

    buf->mem     = mbuf->data + badr;
    buf->content = buf->mem;
    buf->source  = mbuf;
    
    mbuf->ref++;
  }

  pthread_cleanup_pop (0);
  pthread_mutex_unlock (&this->lock);
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);

  return buf;
}

void read_cache_free (read_cache_t *this) {

  buf_element_t *buf;
  macro_buf_t   *mbuf;

  buf = this->buf_pool_top;
  while (buf) {
    buf_element_t *next = buf->next;

    free(buf);
    buf = next;
  }

  mbuf = this->mbuf_pool_top;
  while (mbuf) {
    macro_buf_t   *mnext = mbuf->next;

    free (mbuf);
    mbuf = mnext;
  }

  free (this->multi_base);

  free (this);
}

