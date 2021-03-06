/*
 * Copyright (C) 2000-2021 the xine project
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/********** logging **********/
#define LOG_MODULE "buffer"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/buffer.h>
#include <xine/xineutils.h>
#include <xine/xine_internal.h>
#include "xine_private.h"

/* The large buffer feature.
 * If we have enough contigous memory, and if we can afford to hand if out,
 * provide an oversize item there. The buffers covering that extra space will
 * hide inside our buffer array, and buffer_pool_free () will reappear them
 * mysteriously later.
 * Small bufs are requested frequently, so we dont do a straightforward
 * heap manager. Instead, we keep bufs in pool sorted by address, and
 * be_ei_t.nbufs holds the count of contigous bufs when this is the
 * first of such a group.
 * API permits using bufs in a different fifo than their pool origin
 * (see demux_mpeg_block). Thats why we test buf->source.
 * It is also possible to supply fully custom bufs. We detect these by
 * buf->free_buffer != buffer_pool_free.
 */

typedef struct {
  buf_element_t elem; /* needs to be first */
  int nbufs;          /* # of contigous bufs */
  extra_info_t  ei;
} be_ei_t;

#define LARGE_NUM 0x7fffffff

/* The file buf ctrl feature.
 * After stream start/seek (fifo flush), there is a phase when a few decoded frames
 * are better than a lot of merely demuxed ones. Net_buf_ctrl wants large fifos to
 * handle fragment and other stuttering streams. Lets assume that it knows what to
 * do there. For plain files, however, demux is likely to drain processor time from
 * decoders initially.
 * A separate file_buf_ctrl module should not mess with fifo internals, thus lets
 * do a little soft start version here when there are no callbacks:
 * fifo->alloc_cb[0] == fbc_dummy,
 * fifo->alloc_cb_data[0] == count of yet not to be used bufs. */

static void fbc_dummy (fifo_buffer_t *this, void *data) {
  (void)this;
  (void)data;
}

int xine_fbc_set (fifo_buffer_t *fifo, int on) {
  if (!fifo)
    return 0;
  pthread_mutex_lock (&fifo->mutex);

  if (on) {
    int n;
    if (fifo->alloc_cb[0]) {
      n = (fifo->alloc_cb[0] == fbc_dummy);
      pthread_mutex_unlock (&fifo->mutex);
      return n;
    }
    fifo->alloc_cb[0] = fbc_dummy;
    n = (fifo->buffer_pool_capacity * 3) >> 2;
    if (n < 75)
      n = 0;
    fifo->alloc_cb_data[0] = (void *)(intptr_t)n;
    pthread_mutex_unlock (&fifo->mutex);
    return 1;
  }

  if (fifo->alloc_cb[0] == fbc_dummy) {
    fifo->alloc_cb[0] = NULL;
    fifo->alloc_cb_data[0] = (void *)0;
  }
  pthread_mutex_unlock (&fifo->mutex);
  return 0;
}

static int fbc_avail (fifo_buffer_t *this) {
  return this->alloc_cb[0] != fbc_dummy
    ? this->buffer_pool_num_free
    : this->buffer_pool_num_free - (intptr_t)this->alloc_cb_data[0];
}

static void fbc_reset (fifo_buffer_t *this) {
  if (this->alloc_cb[0] == fbc_dummy) {
    int n = (this->buffer_pool_capacity * 3) >> 2;
    if (n < 75)
      n = 0;
    this->alloc_cb_data[0] = (void *)(intptr_t)n;
  }
}

static void fbc_sub (fifo_buffer_t *this, int n) {
  if (this->alloc_cb[0] == fbc_dummy) {
    n = (intptr_t)this->alloc_cb_data[0] - n;
    if (n < 0)
      n = 0;
    this->alloc_cb_data[0] = (void *)(intptr_t)n;
  }
}

/*
 * put a previously allocated buffer element back into the buffer pool
 */
static void buffer_pool_free (buf_element_t *element) {
  fifo_buffer_t *this = (fifo_buffer_t *) element->source;
  be_ei_t *newhead, *newtail, *nexthead;
  int n;

  pthread_mutex_lock (&this->buffer_pool_mutex);

  newhead = (be_ei_t *)element;
  n = newhead->nbufs;
  fbc_sub (this, n);
  this->buffer_pool_num_free += n;
  if (this->buffer_pool_num_free > this->buffer_pool_capacity) {
    fprintf(stderr, _("xine-lib: buffer.c: There has been a fatal error: TOO MANY FREE's\n"));
    _x_abort();
  }

  /* we might be a new chunk */
  newtail = newhead + 1;
  while (--n > 0) {
    newtail[-1].elem.next = &newtail[0].elem;
    newtail++;
  }

  nexthead = (be_ei_t *)this->buffer_pool_top;
  if (!nexthead || (nexthead >= newtail)) {
    /* add head */
    this->buffer_pool_top = &newhead->elem;
    newtail[-1].elem.next = &nexthead->elem;
    /* merge with next chunk if no gap */
    if (newtail == nexthead)
      newhead->nbufs += nexthead->nbufs;
  } else {
    /* Keep the pool sorted, elem1 > elem2 implies elem1->mem > elem2->mem. */
    be_ei_t *prevhead, *prevtail;
    while (1) {
      prevhead = nexthead;
      prevtail = prevhead + prevhead->nbufs;
      nexthead = (be_ei_t *)prevtail[-1].elem.next;
      if (!nexthead || (nexthead >= newtail))
        break;
    }
    prevtail[-1].elem.next = &newhead->elem;
    newtail[-1].elem.next = &nexthead->elem;
    /* merge with next chunk if no gap */
    if (newtail == nexthead)
      newhead->nbufs += nexthead->nbufs;
    /* merge with prev chunk if no gap */
    if (prevtail == newhead)
      prevhead->nbufs += newhead->nbufs;
  }

  /* dont provoke useless wakeups */
  if (this->buffer_pool_num_waiters ||
    (this->buffer_pool_large_wait <= fbc_avail (this)))
    pthread_cond_signal (&this->buffer_pool_cond_not_empty);

  pthread_mutex_unlock (&this->buffer_pool_mutex);
}

/*
 * allocate a buffer from buffer pool
 */

static buf_element_t *buffer_pool_size_alloc_int (fifo_buffer_t *this, int n) {

  int i;
  be_ei_t *buf;

  for (i = 0; this->alloc_cb[i]; i++)
    this->alloc_cb[i] (this, this->alloc_cb_data[i]);

  if (n < 1)
    n = 1;
  /* we always keep one free buffer for emergency situations like
   * decoder flushes that would need a buffer in buffer_pool_try_alloc() */
  n += 2;
  if (fbc_avail (this) < n) {
    /* Paranoia: someone else than demux calling this in parallel ?? */
    if (this->buffer_pool_large_wait != LARGE_NUM) {
      this->buffer_pool_num_waiters++;
      do {
        pthread_cond_wait (&this->buffer_pool_cond_not_empty, &this->buffer_pool_mutex);
      } while (fbc_avail (this) < n);
      this->buffer_pool_num_waiters--;
    } else {
      this->buffer_pool_large_wait = n;
      do {
        pthread_cond_wait (&this->buffer_pool_cond_not_empty, &this->buffer_pool_mutex);
      } while (fbc_avail (this) < n);
      this->buffer_pool_large_wait = LARGE_NUM;
    }
  }
  n -= 2;

  buf = (be_ei_t *)this->buffer_pool_top;
  if (n == 1) {

    this->buffer_pool_top = buf->elem.next;
    i = buf->nbufs - 1;
    if (i > 0)
      buf[1].nbufs = i;
    this->buffer_pool_num_free--;

  } else {

    buf_element_t **link = &this->buffer_pool_top, **bestlink = link;
    int bestsize = 0;
    while (1) {
      int l = buf->nbufs;
      if (l > n) {
        be_ei_t *next = buf + n;
        next->nbufs = l - n;
        *link = &next->elem;
        break;
      } else if (l == n) {
        *link = buf[l - 1].elem.next;
        break;
      }
      if (l > bestsize) {
        bestsize = l;
        bestlink = link;
      }
      buf += l - 1;
      link = &buf->elem.next;
      buf = (be_ei_t *)(*link);
      if (!buf) {
        buf = (be_ei_t *)(*bestlink);
        n = bestsize;
        *bestlink = buf[n - 1].elem.next;
        break;
      }
    }
    this->buffer_pool_num_free -= n;

  }

  pthread_mutex_unlock (&this->buffer_pool_mutex);

  /* set sane values to the newly allocated buffer */
  buf->elem.content = buf->elem.mem; /* 99% of demuxers will want this */
  buf->elem.pts = 0;
  buf->elem.size = 0;
  buf->elem.max_size = n * this->buffer_pool_buf_size;
  buf->elem.decoder_flags = 0;
  buf->nbufs = n;
  memset (buf->elem.decoder_info, 0, sizeof (buf->elem.decoder_info));
  memset (buf->elem.decoder_info_ptr, 0, sizeof (buf->elem.decoder_info_ptr));
  _x_extra_info_reset (buf->elem.extra_info);

  return &buf->elem;
}

static buf_element_t *buffer_pool_size_alloc (fifo_buffer_t *this, size_t size) {
  int n = size ? ((int)size + this->buffer_pool_buf_size - 1) / this->buffer_pool_buf_size : 1;
  if (n > (this->buffer_pool_capacity >> 2))
    n = this->buffer_pool_capacity >> 2;
  pthread_mutex_lock (&this->buffer_pool_mutex);
  return buffer_pool_size_alloc_int (this, n);
}


static buf_element_t *buffer_pool_alloc (fifo_buffer_t *this) {
  be_ei_t *buf;
  int i;

  pthread_mutex_lock (&this->buffer_pool_mutex);

  for(i = 0; this->alloc_cb[i]; i++)
    this->alloc_cb[i](this, this->alloc_cb_data[i]);

  /* we always keep one free buffer for emergency situations like
   * decoder flushes that would need a buffer in buffer_pool_try_alloc() */
  if (fbc_avail (this) < 2) {
    this->buffer_pool_num_waiters++;
    do {
      pthread_cond_wait (&this->buffer_pool_cond_not_empty, &this->buffer_pool_mutex);
    } while (fbc_avail (this) < 2);
    this->buffer_pool_num_waiters--;
  }

  buf = (be_ei_t *)this->buffer_pool_top;
  this->buffer_pool_top = buf->elem.next;
  i = buf->nbufs - 1;
  if (i > 0)
    buf[1].nbufs = i;
  this->buffer_pool_num_free--;

  pthread_mutex_unlock (&this->buffer_pool_mutex);

  /* set sane values to the newly allocated buffer */
  buf->elem.content = buf->elem.mem; /* 99% of demuxers will want this */
  buf->elem.pts = 0;
  buf->elem.size = 0;
  buf->elem.max_size = this->buffer_pool_buf_size;
  buf->elem.decoder_flags = 0;
  buf->nbufs = 1;
  memset (buf->elem.decoder_info, 0, sizeof (buf->elem.decoder_info));
  memset (buf->elem.decoder_info_ptr, 0, sizeof (buf->elem.decoder_info_ptr));
  _x_extra_info_reset (buf->elem.extra_info);

  return &buf->elem;
}

static buf_element_t *buffer_pool_realloc (buf_element_t *buf, size_t new_size) {
  fifo_buffer_t *this;
  buf_element_t **last_buf;
  be_ei_t *old_buf = (be_ei_t *)buf, *new_buf, *want_buf;
  int n;

  if (!old_buf)
    return NULL;
  if ((int)new_size <= old_buf->elem.max_size)
    return NULL;
  if (old_buf->elem.free_buffer != buffer_pool_free)
    return NULL;
  this = (fifo_buffer_t *)old_buf->elem.source;
  if (!this)
    return NULL;

  n = ((int)new_size + this->buffer_pool_buf_size - 1) / this->buffer_pool_buf_size;
  /* limit size to keep pool fluent */
  if (n > (this->buffer_pool_capacity >> 3))
    n = this->buffer_pool_capacity >> 3;
  n -= old_buf->nbufs;

  want_buf = old_buf + old_buf->nbufs;
  last_buf = &this->buffer_pool_top;
  pthread_mutex_lock (&this->buffer_pool_mutex);
  while (1) {
    new_buf = (be_ei_t *)(*last_buf);
    if (!new_buf)
      break;
    if (new_buf == want_buf)
      break;
    if (new_buf > want_buf) {
      new_buf = NULL;
      break;
    }
    new_buf += new_buf->nbufs;
    last_buf = &(new_buf[-1].elem.next);
  }

  if (new_buf) do {
    int s;
    /* save emergecy buf */
    if (n > this->buffer_pool_num_free - 1)
      n = this->buffer_pool_num_free - 1;
    if (n < 1)
      break;
    s = new_buf->nbufs - n;
    if (s > 0) {
      new_buf += n;
      new_buf->nbufs = s;
      *last_buf = &new_buf->elem;
    } else {
      n = new_buf->nbufs;
      new_buf += n;
      *last_buf = new_buf[-1].elem.next;
    }
    this->buffer_pool_num_free -= n;
    pthread_mutex_unlock (&this->buffer_pool_mutex);
    old_buf->nbufs += n;
    old_buf->elem.max_size = old_buf->nbufs * this->buffer_pool_buf_size;
    return NULL;
  } while (0);

  return buffer_pool_size_alloc_int (this, n);
}

/*
 * allocate a buffer from buffer pool - may fail if none is available
 */

static buf_element_t *buffer_pool_try_alloc (fifo_buffer_t *this) {
  be_ei_t *buf;
  int i;

  pthread_mutex_lock (&this->buffer_pool_mutex);
  buf = (be_ei_t *)this->buffer_pool_top;
  if (!buf) {
    pthread_mutex_unlock (&this->buffer_pool_mutex);
    return NULL;
  }

  this->buffer_pool_top = buf->elem.next;
  i = buf->nbufs - 1;
  if (i > 0)
    buf[1].nbufs = i;
  this->buffer_pool_num_free--;
  pthread_mutex_unlock (&this->buffer_pool_mutex);

  /* set sane values to the newly allocated buffer */
  buf->elem.content = buf->elem.mem; /* 99% of demuxers will want this */
  buf->elem.pts = 0;
  buf->elem.size = 0;
  buf->elem.max_size = this->buffer_pool_buf_size;
  buf->elem.decoder_flags = 0;
  buf->nbufs = 1;
  memset (buf->elem.decoder_info, 0, sizeof (buf->elem.decoder_info));
  memset (buf->elem.decoder_info_ptr, 0, sizeof (buf->elem.decoder_info_ptr));
  _x_extra_info_reset (buf->elem.extra_info);

  return &buf->elem;
}


/*
 * append buffer element to fifo buffer
 */
static void fifo_buffer_put (fifo_buffer_t *fifo, buf_element_t *element) {
  int i;

  pthread_mutex_lock (&fifo->mutex);

  if (element->decoder_flags & BUF_FLAG_MERGE) {
    be_ei_t *new = (be_ei_t *)element, *prev = (be_ei_t *)fifo->last;
    new->elem.decoder_flags &= ~BUF_FLAG_MERGE;
    if (prev && (prev + prev->nbufs == new)
      && (prev->elem.type == new->elem.type)
      && (prev->nbufs < (fifo->buffer_pool_capacity >> 3))) {
      fifo->fifo_size += new->nbufs;
      fifo->fifo_data_size += new->elem.size;
      prev->nbufs += new->nbufs;
      prev->elem.max_size += new->elem.max_size;
      prev->elem.size += new->elem.size;
      prev->elem.decoder_flags |= new->elem.decoder_flags;
      pthread_mutex_unlock (&fifo->mutex);
      return;
    }
  }

  for(i = 0; fifo->put_cb[i]; i++)
    fifo->put_cb[i](fifo, element, fifo->put_cb_data[i]);

  if (fifo->last)
    fifo->last->next = element;
  else
    fifo->first = element;

  fifo->last = element;
  element->next = NULL;

  if (element->free_buffer == buffer_pool_free) {
    be_ei_t *beei = (be_ei_t *)element;
    fifo->fifo_size += beei->nbufs;
  } else {
    fifo->fifo_size += 1;
  }
  fifo->fifo_data_size += element->size;

  if (fifo->fifo_num_waiters)
    pthread_cond_signal (&fifo->not_empty);

  pthread_mutex_unlock (&fifo->mutex);
}

/*
 * simulate append buffer element to fifo buffer
 */
static void dummy_fifo_buffer_put (fifo_buffer_t *fifo, buf_element_t *element) {
  int i;

  pthread_mutex_lock (&fifo->mutex);

  for(i = 0; fifo->put_cb[i]; i++)
    fifo->put_cb[i](fifo, element, fifo->put_cb_data[i]);

  pthread_mutex_unlock (&fifo->mutex);

  element->free_buffer(element);
}

/*
 * insert buffer element to fifo buffer (demuxers MUST NOT call this one)
 */
static void fifo_buffer_insert (fifo_buffer_t *fifo, buf_element_t *element) {
  pthread_mutex_lock (&fifo->mutex);

  element->next = fifo->first;
  fifo->first = element;

  if( !fifo->last )
    fifo->last = element;

  if (element->free_buffer == buffer_pool_free) {
    be_ei_t *beei = (be_ei_t *)element;
    fifo->fifo_size += beei->nbufs;
  } else {
    fifo->fifo_size += 1;
  }
  fifo->fifo_data_size += element->size;

  if (fifo->fifo_num_waiters)
    pthread_cond_signal (&fifo->not_empty);

  pthread_mutex_unlock (&fifo->mutex);
}

/*
 * insert buffer element to fifo buffer (demuxers MUST NOT call this one)
 */
static void dummy_fifo_buffer_insert (fifo_buffer_t *fifo, buf_element_t *element) {
  (void)fifo;
  element->free_buffer(element);
}

/*
 * get element from fifo buffer
 */
static buf_element_t *fifo_buffer_get (fifo_buffer_t *fifo) {
  buf_element_t *buf;
  int i;

  pthread_mutex_lock (&fifo->mutex);

  if (!fifo->first) {
    fifo->fifo_num_waiters++;
    do {
      pthread_cond_wait (&fifo->not_empty, &fifo->mutex);
    } while (!fifo->first);
    fifo->fifo_num_waiters--;
  }

  buf = fifo->first;

  fifo->first = fifo->first->next;
  if (fifo->first==NULL)
    fifo->last = NULL;

  if (buf->free_buffer == buffer_pool_free) {
    be_ei_t *beei = (be_ei_t *)buf;
    fifo->fifo_size -= beei->nbufs;
  } else {
    fifo->fifo_size -= 1;
  }
  fifo->fifo_data_size -= buf->size;

  for(i = 0; fifo->get_cb[i]; i++)
    fifo->get_cb[i](fifo, buf, fifo->get_cb_data[i]);

  pthread_mutex_unlock (&fifo->mutex);

  return buf;
}

static buf_element_t *fifo_buffer_tget (fifo_buffer_t *fifo, xine_ticket_t *ticket) {
  /* Optimization: let decoders hold port ticket by default.
   * Unfortunately, fifo callbacks are 1 big freezer, as they run with fifo locked,
   * and may try to revoke ticket for pauseing or other stuff.
   * Always releasing ticket when there are callbacks is safe but inefficient.
   * Instead, we release ticket when we are going to wait for fifo or a buffer,
   * and of course, when the ticket has been revoked.
   * This should melt the "put" side. We could still freeze ourselves directly
   * at the "get" side, what ticket->revoke () self grant hack shall fix.
   */
  buf_element_t *buf;
  int mode = ticket ? 2 : 0, i;

  if (pthread_mutex_trylock (&fifo->mutex)) {
    if (mode & 2) {
      ticket->release (ticket, 0);
      mode = 1;
    }
    pthread_mutex_lock (&fifo->mutex);
  }

  if (!fifo->first) {
    if (mode & 2) {
      ticket->release (ticket, 0);
      mode = 1;
    }
    fifo->fifo_num_waiters++;
    do {
      pthread_cond_wait (&fifo->not_empty, &fifo->mutex);
    } while (!fifo->first);
    fifo->fifo_num_waiters--;
  }

  buf = fifo->first;

  fifo->first = fifo->first->next;
  if (fifo->first==NULL)
    fifo->last = NULL;

  if (buf->free_buffer == buffer_pool_free) {
    be_ei_t *beei = (be_ei_t *)buf;
    fifo->fifo_size -= beei->nbufs;
  } else {
    fifo->fifo_size -= 1;
  }
  fifo->fifo_data_size -= buf->size;

  if ((mode & 2) && ticket->ticket_revoked) {
    ticket->release (ticket, 0);
    mode = 1;
  }

  for(i = 0; fifo->get_cb[i]; i++)
    fifo->get_cb[i](fifo, buf, fifo->get_cb_data[i]);

  pthread_mutex_unlock (&fifo->mutex);

  if (mode & 1)
    ticket->acquire (ticket, 0);

  return buf;
}


/*
 * clear buffer (put all contained buffer elements back into buffer pool)
 */
static void fifo_buffer_clear (fifo_buffer_t *fifo) {
  be_ei_t *start;

  pthread_mutex_lock (&fifo->mutex);

  /* take out all at once */
  start = (be_ei_t *)fifo->first;
  fifo->first = fifo->last = NULL;
  fifo->fifo_size = 0;
  fifo->fifo_data_size = 0;

  while (start) {
    be_ei_t *buf, *next;
    int n;

    /* keep control bufs (flush, ...) */
    if ((start->elem.type & BUF_MAJOR_MASK) == BUF_CONTROL_BASE) {
      if (!fifo->first)
        fifo->first = &start->elem;
      else
        fifo->last->next = &start->elem;
      fifo->last = &start->elem;
      fifo->fifo_size += 1;
      fifo->fifo_data_size += start->elem.size;
      buf = (be_ei_t *)start->elem.next;
      start->elem.next = NULL;
      start = buf;
      continue;
    }

    /* free custom buf */
    if (start->elem.free_buffer != buffer_pool_free) {
      buf = (be_ei_t *)start->elem.next;
      start->elem.next = NULL;
      start->elem.free_buffer (&start->elem);
      start = buf;
      continue;
    }

    /* optimize: get contiguous chunk */
    buf = start;
    n = 0;
    while (1) {
      int i = buf->nbufs;
      next = (be_ei_t *)buf->elem.next;
      n += i;
      if (buf + i != next) /* includes next == NULL et al ;-) */
        break;
      if ((next->elem.type & BUF_MAJOR_MASK) == BUF_CONTROL_BASE)
        break;
      buf = next;
    }
    start->nbufs = n;
    start->elem.free_buffer (&start->elem);
    start = next;
  }

  fbc_reset (fifo);
  /* printf("Free buffers after clear: %d\n", fifo->buffer_pool_num_free); */
  pthread_mutex_unlock (&fifo->mutex);
}

static void fifo_buffer_all_clear (fifo_buffer_t *fifo) {
  be_ei_t *start;

  pthread_mutex_lock (&fifo->mutex);

  /* take out all at once */
  start = (be_ei_t *)fifo->first;
  fifo->first = fifo->last = NULL;
  fifo->fifo_size = 0;
  fifo->fifo_data_size = 0;

  while (start) {
    be_ei_t *buf, *next;
    int n;

    /* free custom buf */
    if (start->elem.free_buffer != buffer_pool_free) {
      buf = (be_ei_t *)start->elem.next;
      start->elem.next = NULL;
      start->elem.free_buffer (&start->elem);
      start = buf;
      continue;
    }

    /* optimize: get contiguous chunk */
    buf = start;
    n = 0;
    while (1) {
      int i = buf->nbufs;
      next = (be_ei_t *)buf->elem.next;
      n += i;
      if (buf + i != next) /* includes next == NULL ;-) */
        break;
      buf = next;
    }
    /* free just sibling bufs */
    if (start->elem.source != (void *)fifo) {
      start->nbufs = n;
      start->elem.free_buffer (&start->elem);
    }
    start = next;
  }

  pthread_mutex_unlock (&fifo->mutex);
}

/*
 * Return the number of elements in the fifo buffer
 */
static int fifo_buffer_size (fifo_buffer_t *this) {
  int size;

  pthread_mutex_lock(&this->mutex);
  size = this->fifo_size;
  pthread_mutex_unlock(&this->mutex);

  return size;
}

/*
 * Return the amount of the data in the fifo buffer
 */
static uint32_t fifo_buffer_data_size (fifo_buffer_t *this) {
  uint32_t data_size;

  pthread_mutex_lock(&this->mutex);
  data_size = this->fifo_data_size;
  pthread_mutex_unlock(&this->mutex);

  return data_size;
}

/*
 * Return the number of free elements in the pool
 */
static int fifo_buffer_num_free (fifo_buffer_t *this) {
  int buffer_pool_num_free;

  pthread_mutex_lock (&this->buffer_pool_mutex);
  buffer_pool_num_free = this->buffer_pool_num_free;
  pthread_mutex_unlock (&this->buffer_pool_mutex);

  return buffer_pool_num_free;
}

/*
 * Destroy the buffer
 */
static void fifo_buffer_dispose (fifo_buffer_t *this) {
  fifo_buffer_all_clear (this);
  xine_free_aligned (this->buffer_pool_base);
  pthread_mutex_destroy(&this->mutex);
  pthread_cond_destroy(&this->not_empty);
  pthread_mutex_destroy(&this->buffer_pool_mutex);
  pthread_cond_destroy(&this->buffer_pool_cond_not_empty);
  free (this);
}

/*
 * Register an "alloc" callback
 */
static void fifo_register_alloc_cb (fifo_buffer_t *this,
                                    void (*cb)(fifo_buffer_t *this,
                                               void *data_cb),
                                    void *data_cb) {
  int i;

  pthread_mutex_lock(&this->mutex);
  if (this->alloc_cb[0] == fbc_dummy) {
    this->alloc_cb[0] = NULL;
    this->alloc_cb_data[0] = NULL;
  }
  for(i = 0; this->alloc_cb[i]; i++)
    ;
  if( i != BUF_MAX_CALLBACKS-1 ) {
    this->alloc_cb[i] = cb;
    this->alloc_cb_data[i] = data_cb;
    this->alloc_cb[i+1] = NULL;
    this->alloc_cb_data[i+1] = (void *)(intptr_t)0;
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Register a "put" callback
 */
static void fifo_register_put_cb (fifo_buffer_t *this,
                                  void (*cb)(fifo_buffer_t *this,
                                             buf_element_t *buf,
                                             void *data_cb),
                                  void *data_cb) {
  int i;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->put_cb[i]; i++)
    ;
  if( i != BUF_MAX_CALLBACKS-1 ) {
    this->put_cb[i] = cb;
    this->put_cb_data[i] = data_cb;
    this->put_cb[i+1] = NULL;
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Register a "get" callback
 */
static void fifo_register_get_cb (fifo_buffer_t *this,
                                  void (*cb)(fifo_buffer_t *this,
                                             buf_element_t *buf,
                                             void *data_cb),
                                  void *data_cb) {
  int i;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->get_cb[i]; i++)
    ;
  if( i != BUF_MAX_CALLBACKS-1 ) {
    this->get_cb[i] = cb;
    this->get_cb_data[i] = data_cb;
    this->get_cb[i+1] = NULL;
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Unregister an "alloc" callback
 */
static void fifo_unregister_alloc_cb (fifo_buffer_t *this,
                                      void (*cb)(fifo_buffer_t *this,
                                                 void *data_cb) ) {
  int i,j;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->alloc_cb[i]; i++) {
    if( this->alloc_cb[i] == cb ) {
      for(j = i; this->alloc_cb[j]; j++) {
        this->alloc_cb[j] = this->alloc_cb[j+1];
        this->alloc_cb_data[j] = this->alloc_cb_data[j+1];
      }
    }
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Unregister a "put" callback
 */
static void fifo_unregister_put_cb (fifo_buffer_t *this,
                                  void (*cb)(fifo_buffer_t *this,
                                             buf_element_t *buf,
                                             void *data_cb) ) {
  int i,j;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->put_cb[i]; i++) {
    if( this->put_cb[i] == cb ) {
      for(j = i; this->put_cb[j]; j++) {
        this->put_cb[j] = this->put_cb[j+1];
        this->put_cb_data[j] = this->put_cb_data[j+1];
      }
    }
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * Unregister a "get" callback
 */
static void fifo_unregister_get_cb (fifo_buffer_t *this,
                                  void (*cb)(fifo_buffer_t *this,
                                             buf_element_t *buf,
                                             void *data_cb) ) {
  int i,j;

  pthread_mutex_lock(&this->mutex);
  for(i = 0; this->get_cb[i]; i++) {
    if( this->get_cb[i] == cb ) {
      for(j = i; this->get_cb[j]; j++) {
        this->get_cb[j] = this->get_cb[j+1];
        this->get_cb_data[j] = this->get_cb_data[j+1];
      }
    }
  }
  pthread_mutex_unlock(&this->mutex);
}

/*
 * allocate and initialize new (empty) fifo buffer
 */
fifo_buffer_t *_x_fifo_buffer_new (int num_buffers, uint32_t buf_size) {

  fifo_buffer_t *this;
  int            i;
  unsigned char *multi_buffer;
  be_ei_t       *beei;

  this = calloc(1, sizeof(fifo_buffer_t));
  if (!this)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  /* Do these first, when compiler still knows "this" is all zeroed.
   * Let it optimize away this on most systems where clear mem
   * interpretes as 0, 0f or NULL safely.
   */
  this->first                   = NULL;
  this->last                    = NULL;
  this->fifo_size               = 0;
  this->fifo_num_waiters        = 0;
  this->buffer_pool_num_waiters = 0;
  this->alloc_cb[0]             = NULL;
  this->get_cb[0]               = NULL;
  this->put_cb[0]               = NULL;
  this->alloc_cb_data[0]        = NULL;
  this->get_cb_data[0]          = NULL;
  this->put_cb_data[0]          = NULL;
#endif

  /* printf ("Allocating %d buffers of %ld bytes in one chunk\n", num_buffers, (long int) buf_size); */
  multi_buffer = xine_mallocz_aligned (num_buffers * (buf_size + sizeof (be_ei_t)));
  if (!multi_buffer) {
    free (this);
    return NULL;
  }

  this->put                 = fifo_buffer_put;
  this->insert              = fifo_buffer_insert;
  this->get                 = fifo_buffer_get;
  this->tget                = fifo_buffer_tget;
  this->clear               = fifo_buffer_clear;
  this->size                = fifo_buffer_size;
  this->num_free            = fifo_buffer_num_free;
  this->data_size           = fifo_buffer_data_size;
  this->dispose             = fifo_buffer_dispose;
  this->register_alloc_cb   = fifo_register_alloc_cb;
  this->register_get_cb     = fifo_register_get_cb;
  this->register_put_cb     = fifo_register_put_cb;
  this->unregister_alloc_cb = fifo_unregister_alloc_cb;
  this->unregister_get_cb   = fifo_unregister_get_cb;
  this->unregister_put_cb   = fifo_unregister_put_cb;
  pthread_mutex_init (&this->mutex, NULL);
  pthread_cond_init (&this->not_empty, NULL);

  /* init buffer pool */

  pthread_mutex_init (&this->buffer_pool_mutex, NULL);
  pthread_cond_init (&this->buffer_pool_cond_not_empty, NULL);

  this->buffer_pool_num_free   =
  this->buffer_pool_capacity   = num_buffers;
  this->buffer_pool_buf_size   = buf_size;
  this->buffer_pool_alloc      = buffer_pool_alloc;
  this->buffer_pool_try_alloc  = buffer_pool_try_alloc;
  this->buffer_pool_size_alloc = buffer_pool_size_alloc;
  this->buffer_pool_realloc    = buffer_pool_realloc;

  this->buffer_pool_large_wait  = LARGE_NUM;

  this->buffer_pool_base = multi_buffer;
  beei = (be_ei_t *)(multi_buffer + num_buffers * buf_size);
  this->buffer_pool_top  = &beei->elem;
  beei->nbufs = num_buffers;

  for (i = 0; i < num_buffers; i++) {
    beei->elem.mem         = multi_buffer;
    multi_buffer          += buf_size;
    beei->elem.max_size    = buf_size;
    beei->elem.free_buffer = buffer_pool_free;
    beei->elem.source      = this;
    beei->elem.extra_info  = &beei->ei;
    beei->elem.next        = &(beei + 1)->elem;
    beei++;
  }

  (beei - 1)->elem.next = NULL;

  return this;
}

/*
 * allocate and initialize new (empty) fifo buffer
 */
fifo_buffer_t *_x_dummy_fifo_buffer_new (int num_buffers, uint32_t buf_size) {
  fifo_buffer_t *this = _x_fifo_buffer_new (num_buffers, buf_size);
  if (this) {
    this->put    = dummy_fifo_buffer_put;
    this->insert = dummy_fifo_buffer_insert;
  }
  return this;
}

void _x_free_buf_elements(buf_element_t *head) {

  if (head) {
    buf_element_t  *cur, *next;

    cur = head;
    while (cur) {
      next = cur->next;
      cur->free_buffer(cur);
      cur = next;
    }
  }
}
