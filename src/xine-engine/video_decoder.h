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
 * $Id: video_decoder.h,v 1.1 2001/04/18 22:36:09 f1rmb Exp $
 *
 *
 * functions that implement video decoding
 */

#ifndef HAVE_VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <pthread.h>
#include "buffer.h"
#include "video_out.h"

/*
 * generic xine video decoder plugin interface
 */

typedef struct video_decoder_s
{

  /* get interface version */
  int (*get_version) (void);

  int (*can_handle) (int buf_type);

  void (*init) (vo_instance_t *video_out);

  void (*decode_data) (buf_element_t *buf);

  void (*release_img_buffers) (void);

  void (*close) (void);

} video_decoder_t;

/*
 * init video decoders, allocate video fifo,
 * start video decoder thread
 */

fifo_buffer_t *video_decoder_init (vo_instance_t *video_out,
				   pthread_mutex_t xine_lock) ;

/*
 * quit video thread
 */

void video_decoder_shutdown ();

uint32_t video_decoder_get_pos ();

int video_decoder_is_stream_finished ();

#endif
