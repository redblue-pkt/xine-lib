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
 * $Id: audio_decoder.h,v 1.1 2001/04/18 22:36:05 f1rmb Exp $
 *
 *
 * functions that implement audio decoding
 */

#ifndef HAVE_VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include "buffer.h"

/*
 * generic xine audio decoder plugin interface
 */

typedef struct audio_decoder_s
{

  /* get interface version */
  int (*get_version) (void);

  int (*can_handle) (int buf_type);

  void (*init) (ao_instance_t *audio_out);

  void (*decode_data) (buf_element_t *buf);

  void (*close) (void);

} audio_decoder_t;

/*
 * init audio decoders, allocate audio fifo,
 * start audio decoder thread
 */

fifo_buffer_t *audio_decoder_init (ao_instance_t *audio_out,
				   pthread_mutex_t xine_lock) ;

/*
 * quit audio thread
 */

void audio_decoder_shutdown ();


#endif
