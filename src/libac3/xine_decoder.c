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
 * $Id: xine_decoder.c,v 1.1 2001/04/29 16:40:33 guenter Exp $
 *
 * stuff needed to turn libac3 into a xine decoder plugin
 */

/*
 * FIXME: libac3 uses global variables (that are written)
 */


#include <stdlib.h>

#include "audio_out.h"
#include "ac3.h"
#include "buffer.h"
#include "xine_internal.h"


typedef struct ac3dec_decoder_s {
  audio_decoder_t  audio_decoder;
  ac3_config_t     ac3c; /* FIXME - global variables, see above */
} ac3dec_decoder_t;

int ac3dec_can_handle (audio_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_AUDIO_AC3) ;
}


void ac3dec_init (audio_decoder_t *this_gen, ao_functions_t *audio_out) {

  ac3dec_decoder_t *this = (ac3dec_decoder_t *) this_gen;

  ac3_init (&this->ac3c, audio_out);
}

void ac3dec_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {
  /* ac3dec_decoder_t *this = (ac3dec_decoder_t *) this_gen;*/

  ac3_decode_data (buf->content, buf->content + buf->size,
		   buf->PTS);
}

void ac3dec_close (audio_decoder_t *this_gen) {

  /* ac3dec_decoder_t *this = (ac3dec_decoder_t *) this_gen; */

  ac3_reset ();
}

static char *ac3dec_get_id(void) {
  return "ac3dec";
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  ac3dec_decoder_t *this ;

  if (iface_version != 1)
    return NULL;

  this = (ac3dec_decoder_t *) malloc (sizeof (ac3dec_decoder_t));

  this->ac3c.flags                = 0;
  this->ac3c.fill_buffer_callback = NULL;
  this->ac3c.num_output_ch        = 2; /* FIXME */
  this->ac3c.dual_mono_ch_sel     = 0;

  this->audio_decoder.interface_version   = 1;
  this->audio_decoder.can_handle          = ac3dec_can_handle;
  this->audio_decoder.init                = ac3dec_init;
  this->audio_decoder.decode_data         = ac3dec_decode_data;
  this->audio_decoder.close               = ac3dec_close;
  this->audio_decoder.get_identifier      = ac3dec_get_id;
  
  return (audio_decoder_t *) this;
}

