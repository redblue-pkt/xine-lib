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
 * $Id: buffer.h,v 1.20 2001/11/06 21:46:05 miguelfreitas Exp $
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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>
#include <sys/types.h>

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

#define BUF_CONTROL_BASE          0x01000000
#define BUF_CONTROL_START         0x01000000
#define BUF_CONTROL_END           0x01010000
#define BUF_CONTROL_QUIT          0x01020000
#define BUF_CONTROL_DISCONTINUITY 0x01030000
#define BUF_CONTROL_NOP           0x01040000
#define BUF_CONTROL_AUDIO_CHANNEL 0x01050000
#define BUF_CONTROL_SPU_CHANNEL   0x01060000

/* video buffer types:  */

#define BUF_VIDEO_BASE		0x02000000
#define BUF_VIDEO_MPEG		0x02000000
#define BUF_VIDEO_MPEG4		0x02010000
#define BUF_VIDEO_CINEPAK	0x02020000
#define BUF_VIDEO_SORENSON	0x02030000
#define BUF_VIDEO_MSMPEG4_V12	0x02040000
#define BUF_VIDEO_MSMPEG4_V3	0x02050000
#define BUF_VIDEO_MJPEG		0x02060000
#define BUF_VIDEO_IV50		0x02070000
#define BUF_VIDEO_IV41		0x02080000
#define BUF_VIDEO_IV32		0x02090000
#define BUF_VIDEO_IV31		0x020a0000
#define BUF_VIDEO_ATIVCR1	0x020b0000
#define BUF_VIDEO_ATIVCR2	0x020c0000
#define BUF_VIDEO_I263		0x020d0000
#define BUF_VIDEO_RV10		0x020e0000
#define BUF_VIDEO_FILL		0x020f0000
#define BUF_VIDEO_RGB		0x02100000
#define BUF_VIDEO_YUY2		0x02110000
#define BUF_VIDEO_JPEG		0x02120000
#define BUF_VIDEO_WMV7		0x02130000
#define BUF_VIDEO_WMV8		0x02140000

/* audio buffer types:  */

#define BUF_AUDIO_BASE		0x03000000
#define BUF_AUDIO_A52		0x03000000
#define BUF_AUDIO_MPEG		0x03010000
#define BUF_AUDIO_LPCM_BE	0x03020000
#define BUF_AUDIO_LPCM_LE	0x03030000
#define BUF_AUDIO_DIVXA		0x03040000
#define BUF_AUDIO_DTS		0x03050000
#define BUF_AUDIO_MSADPCM	0x03060000
#define BUF_AUDIO_IMAADPCM	0x03070000
#define BUF_AUDIO_MSGSM		0x03080000 
#define BUF_AUDIO_VORBIS        0x03090000

/* spu buffer types:    */
 
#define BUF_SPU_BASE		0x04000000
#define BUF_SPU_CLUT		0x04000000
#define BUF_SPU_PACKAGE		0x04010000
#define BUF_SPU_SUBP_CONTROL	0x04020000

/* demuxer block types: */

#define BUF_DEMUX_BLOCK		0x05000000

typedef struct buf_element_s buf_element_t;
struct buf_element_s {
  buf_element_t        *next;

  unsigned char        *mem;
  unsigned char        *content; /* start of raw content in pMem (without header etc) */

  uint32_t              size ;   /* size of _content_ */
  uint32_t              max_size;        
  uint32_t              type;
  uint32_t              PTS;
  off_t                 input_pos; /* remember where this buf came from in the input source */
  int                   input_time;/* time offset in seconds from beginning of stream       */
  uint32_t              decoder_info[4]; /* additional decoder flags and other dec-spec. stuff */

  void (*free_buffer) (buf_element_t *buf);

  void                 *source;   /* pointer to source of this buffer for */
                                  /* free_buffer                          */

} ;

typedef struct fifo_buffer_s fifo_buffer_t;
struct fifo_buffer_s
{
  buf_element_t  *first, *last;
  int             fifo_size;

  pthread_mutex_t mutex;
  pthread_cond_t  not_empty;

  /*
   * functions to access this fifo:
   */

  void (*put) (fifo_buffer_t *fifo, buf_element_t *buf);
  
  buf_element_t *(*get) (fifo_buffer_t *fifo);

  void (*clear) (fifo_buffer_t *fifo) ;

  int (*size) (fifo_buffer_t *fifo);

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
  int		   buffer_pool_capacity;
  int		   buffer_pool_buf_size;
} ;

/*
 * allocate and initialize new (empty) fifo buffer,
 * init buffer pool for it:
 * allocate num_buffers of buf_size bytes each 
 */

fifo_buffer_t *fifo_buffer_new (int num_buffers, uint32_t buf_size);

#ifdef __cplusplus
}
#endif

#endif
