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
 * $Id: xine_decoder.c,v 1.2 2001/04/23 22:43:59 f1rmb Exp $
 *
 * stuff needed to turn libmpeg2 into a xine decoder plugin
 */


#include <stdlib.h>

#include "video_out.h"
#include "mpeg2.h"
#include "mpeg2_internal.h"
#include "buffer.h"
#include "xine_internal.h"


typedef struct mpeg2dec_decoder_s {
  video_decoder_t  video_decoder;
  mpeg2dec_t       mpeg2;
} mpeg2dec_decoder_t;

int mpeg2dec_can_handle (video_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_VIDEO_MPEG) ;
}


void mpeg2dec_init (video_decoder_t *this_gen, vo_instance_t *video_out) {

  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

  mpeg2_init (&this->mpeg2, video_out);
}

void mpeg2dec_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

  mpeg2_decode_data (&this->mpeg2, buf->content, buf->content + buf->size,
		     buf->PTS);
}

void mpeg2dec_release_img_buffers (video_decoder_t *this_gen) {

  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;
     
  decode_free_image_buffers (&this->mpeg2);
}

void mpeg2dec_close (video_decoder_t *this_gen) {

  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

  mpeg2_close (&this->mpeg2);
}

static char *mpeg2dec_get_id(void) {
  return "MPEG2DEC";
}

video_decoder_t *init_video_decoder_plugin (int iface_version, config_values_t *cfg) {

  mpeg2dec_decoder_t *this ;

  if (iface_version != 1)
    return NULL;

  this = (mpeg2dec_decoder_t *) malloc (sizeof (mpeg2dec_decoder_t));

  this->video_decoder.interface_version   = 1;
  this->video_decoder.can_handle          = mpeg2dec_can_handle;
  this->video_decoder.init                = mpeg2dec_init;
  this->video_decoder.decode_data         = mpeg2dec_decode_data;
  this->video_decoder.release_img_buffers = mpeg2dec_release_img_buffers;
  this->video_decoder.close               = mpeg2dec_close;
  this->video_decoder.get_identifier      = mpeg2dec_get_id;

  return (video_decoder_t *) this;
}

