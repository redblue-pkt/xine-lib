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
 * $Id: buffer.c,v 1.1 2001/04/18 22:36:01 f1rmb Exp $
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
 * global variables
 */

buf_element_t   *gBufferPoolTop;    /* a stack actually */
pthread_mutex_t  gBufferPoolMutex;
pthread_cond_t   gBufferPoolCondNotEmpty;
int              gBufferPoolNumFree;

/*
 * put a previously allocated buffer element back into the buffer pool
 */
static void buffer_pool_free (buf_element_t *pBufElement) {

  pthread_mutex_lock (&gBufferPoolMutex);

  pBufElement->next = gBufferPoolTop;
  gBufferPoolTop = pBufElement;

  gBufferPoolNumFree++;

  pthread_cond_signal (&gBufferPoolCondNotEmpty);

  pthread_mutex_unlock (&gBufferPoolMutex);
}

/*
 * check if there are no more free elements
 */
static int buffer_pool_isempty (void) {
  return gBufferPoolNumFree<7;
}

/*
 * check if there are no more free elements
 */
static buf_element_t *buffer_pool_alloc (void) {
  
  buf_element_t *pBuf;

  pthread_mutex_lock (&gBufferPoolMutex);

  while (!gBufferPoolTop) {
    pthread_cond_wait (&gBufferPoolCondNotEmpty, &gBufferPoolMutex);
  }

  pBuf = gBufferPoolTop;
  gBufferPoolTop = gBufferPoolTop->next;
  gBufferPoolNumFree--;

  pthread_mutex_unlock (&gBufferPoolMutex);

  return pBuf;
}

/*
 * append buffer element to fifo buffer
 */
static void fifo_buffer_put (fifo_buffer_t *pFifo, buf_element_t *pBufElement) {
  
  pthread_mutex_lock (&pFifo->mMutex);

  if (pFifo->mpLast) 
    pFifo->mpLast->next = pBufElement;
  else 
    pFifo->mpFirst = pBufElement;

  pFifo->mpLast  = pBufElement;
  pBufElement->next = NULL;

  pthread_cond_signal (&pFifo->mNotEmpty);

  pthread_mutex_unlock (&pFifo->mMutex);
}

/*
 * get element from fifo buffer
 */
static buf_element_t *fifo_buffer_get (fifo_buffer_t *pFifo) {

  buf_element_t *pBuf;
  
  pthread_mutex_lock (&pFifo->mMutex);

  while (pFifo->mpFirst==NULL) {
    pthread_cond_wait (&pFifo->mNotEmpty, &pFifo->mMutex);
  }

  pBuf = pFifo->mpFirst;

  pFifo->mpFirst = pFifo->mpFirst->next;
  if (pFifo->mpFirst==NULL)
    pFifo->mpLast = NULL;

  pthread_mutex_unlock (&pFifo->mMutex);

  return pBuf;
}

/*
 * clear buffer (put all contained buffer elements back into buffer pool)
 */
static void fifo_buffer_clear (fifo_buffer_t *pFifo) {
  
  buf_element_t *pBuf;

  pthread_mutex_lock (&pFifo->mMutex);

  while (pFifo->mpFirst != NULL) {

    pBuf = pFifo->mpFirst;

    pFifo->mpFirst = pFifo->mpFirst->next;
    if (pFifo->mpFirst==NULL)
      pFifo->mpLast = NULL;

    buffer_pool_free (pBuf);
  }

  pthread_mutex_unlock (&pFifo->mMutex);
}

/*
 * allocate and initialize new (empty) fifo buffer
 */
static fifo_buffer_t *fifo_buffer_new (void) {

  fifo_buffer_t *pFifo;

  pFifo = xmalloc (sizeof (fifo_buffer_t));

  pFifo->mpFirst           = NULL;
  pFifo->mpLast            = NULL;
  pFifo->fifo_buffer_put   = fifo_buffer_put;
  pFifo->fifo_buffer_get   = fifo_buffer_get;
  pFifo->fifo_buffer_clear = fifo_buffer_clear;

  pthread_mutex_init (&pFifo->mMutex, NULL);
  pthread_cond_init (&pFifo->mNotEmpty, NULL);

  return pFifo;
}

/*
 * init buffer pool, allocate nNumBuffers of buf_size bytes each 
 */
fifobuf_functions_t *buffer_pool_init (int nNumBuffers, uint32_t buf_size) {

  int i;
  const int alignment = 2048;
  char *pMultiBuffer = NULL;

  if ((buf_size % alignment) == 0) {
    printf ("Allocating %d buffers of %ld bytes in one chunk (alignment = %d)\n", nNumBuffers, (long int)buf_size, alignment);
      pMultiBuffer = xmalloc_aligned (alignment, nNumBuffers * buf_size);
  }

  gBufferPoolTop = NULL;

  pthread_mutex_init (&gBufferPoolMutex, NULL);
  pthread_cond_init (&gBufferPoolCondNotEmpty, NULL);

  for (i = 0; i<nNumBuffers; i++) {
    buf_element_t *pBuf;

    pBuf = xmalloc (sizeof (buf_element_t));

    if (pMultiBuffer != NULL) {
        pBuf->pMem = pMultiBuffer;
        pMultiBuffer += buf_size;
    }
    else
        pBuf->pMem = malloc_aligned (buf_size, alignment);

    pBuf->nMaxSize = buf_size;
    pBuf->free_buffer = buffer_pool_free;
    
    buffer_pool_free (pBuf);
  }
  gBufferPoolNumFree = nNumBuffers;

  return &fifobuf_op;
}
