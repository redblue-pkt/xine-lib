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
 * $Id: buffer.h,v 1.4 2001/07/13 23:43:13 jcdutton Exp $
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

#ifndef HAVE_BUFFER_H
#define HAVE_BUFFER_H

#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>

/*
 * buffer types
 *
 * a buffer type ID describes the contents of a buffer
 * it consists of three fields:
 *
 * buf_type = 0xMMDDCCCC
 *
 * MM   : major buffer type (CONTROL, VIDEO, AUDIO, SPU)
 * DD   : decoder selection (e.g. MPEG, OPENDIVX ... for VIDEO)
 * CCCC : channel number or other subtype information for the decoder
 */

#define BUF_MAJOR_MASK       0xFF000000
#define BUF_DECODER_MASK     0x00FF0000

/* control buffer types */

#define BUF_CONTROL_BASE     0x01000000
#define BUF_CONTROL_START    0x01000000
#define BUF_CONTROL_END      0x01010000
#define BUF_CONTROL_QUIT     0x01020000

/* video buffer types:  */

#define BUF_VIDEO_BASE       0x02000000
#define BUF_VIDEO_MPEG       0x02000000
#define BUF_VIDEO_OPENDIVX   0x02010000
#define BUF_VIDEO_QUICKTIME  0x02020000
#define BUF_VIDEO_AVI        0x02030000

/* audio buffer types:  */

#define BUF_AUDIO_BASE       0x03000000
#define BUF_AUDIO_AC3        0x03000000
#define BUF_AUDIO_MPEG       0x03010000
#define BUF_AUDIO_LPCM       0x03020000
#define BUF_AUDIO_AVI        0x03030000

/* spu buffer types:    */
 
#define BUF_SPU_BASE         0x04000000
#define BUF_SPU_CLUT         0x04000000
#define BUF_SPU_PACKAGE      0x04010000


typedef struct buf_element_s buf_element_t;
struct buf_element_s {
  buf_element_t        *next;

  unsigned char        *mem;
  unsigned char        *content; /* start of raw content in pMem (without header etc) */

  uint32_t              size ;   /* size of _content_ */
  uint32_t              max_size;        
  uint32_t              type;
  uint32_t              PTS, DTS;
  off_t                 input_pos; /* remember where this buf came from in the input source */
  uint32_t              decoder_info[4]; /* additional decoder flags and other dec-spec. stuff */

  void (*free_buffer) (buf_element_t *buf);

  void                 *source;   /* pointer to source of this buffer for */
                                  /* free_buffer                          */

} ;

typedef struct fifo_buffer_s fifo_buffer_t;
struct fifo_buffer_s
{
  buf_element_t  *first, *last;

  pthread_mutex_t mutex;
  pthread_cond_t  not_empty;

  /*
   * functions to access this fifo:
   */

  void (*put) (fifo_buffer_t *fifo, buf_element_t *buf);
  
  buf_element_t *(*get) (fifo_buffer_t *fifo);

  void (*clear) (fifo_buffer_t *fifo) ;

  /* 
   * alloc buffer for this fifo from global buf pool 
   * you don't have to use this function to allocate a buffer,
   * an input plugin can decide to implement it's own
   * buffer allocation functions
   */

  buf_element_t *(*buffer_pool_alloc) (fifo_buffer_t *this);

  /*
   * private variables for buffer pool management
   */

  buf_element_t   *buffer_pool_top;    /* a stack actually */
  pthread_mutex_t  buffer_pool_mutex;
  pthread_cond_t   buffer_pool_cond_not_empty;
  int              buffer_pool_num_free;
} ;

/*
 * allocate and initialize new (empty) fifo buffer,
 * init buffer pool for it:
 * allocate num_buffers of buf_size bytes each 
 */

fifo_buffer_t *fifo_buffer_new (int num_buffers, uint32_t buf_size);

#endif
