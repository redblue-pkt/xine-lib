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
 * $Id: xine_decoder.c,v 1.2 2001/05/26 15:07:18 guenter Exp $
 *
 * stuff needed to turn libac3 into a xine decoder plugin
 */

/*
 * FIXME: libac3 uses global variables (that are written to)
 */


#include <stdlib.h>

#include "audio_out.h"
#include "ac3.h"
#include "buffer.h"
#include "xine_internal.h"

#define FRAME_SIZE 4096

typedef struct ac3dec_decoder_s {
  audio_decoder_t  audio_decoder;

  uint32_t         pts;

  uint8_t          frame_buffer[FRAME_SIZE];
  uint8_t         *frame_ptr;
  int              sync_todo;
  int              frame_length, frame_todo;
  uint16_t         syncword;

  ao_functions_t  *audio_out;
  int              output_sampling_rate;
  int              output_open;

} ac3dec_decoder_t;

int ac3dec_can_handle (audio_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_AUDIO_AC3) ;
}


void ac3dec_init (audio_decoder_t *this_gen, ao_functions_t *audio_out) {

  ac3dec_decoder_t *this = (ac3dec_decoder_t *) this_gen;

  this->audio_out = audio_out;
  ac3_init ();
  this->syncword      = 0;
  this->sync_todo     = 6;
  this->output_open   = 0;
}

void ac3dec_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  ac3dec_decoder_t *this = (ac3dec_decoder_t *) this_gen;

  uint8_t     *current = buf->content;
  uint8_t     *end = buf->content + buf->size;
  ac3_frame_t *ac3_frame;
  int          sampling_rate;

  uint8_t byte;
  
  this->pts = buf->PTS;
  
  while (current != end) {

    while (1) {
      byte = *current++;

      if (this->sync_todo>0) {

	/* search and collect syncinfo */

	if (this->syncword != 0x0b77) {
	  this->syncword = (this->syncword << 8) | byte;

	  if (this->syncword == 0x0b77) {

	    this->frame_buffer[0] = 0x0b;
	    this->frame_buffer[1] = 0x77;
	  
	    this->sync_todo = 4;
	    this->frame_ptr = this->frame_buffer+2;
	  }
	} else {
	  *this->frame_ptr++ = byte;
	  this->sync_todo--;

	  if (this->sync_todo==0) {
	    this->frame_length = ac3_frame_length (this->frame_buffer);
	    this->frame_todo = this->frame_length - 6;
	  }

	}
      } else {

	*this->frame_ptr++ = byte;
	this->frame_todo--;
	
	if (this->frame_todo == 0)
	  break;
      }

      if (current == end) 
	return ;
    }

    /* now, decode this frame */

    ac3_frame = ac3_decode_frame (this->frame_buffer);

    /*  output audio */

    if (!this->output_open 
	|| (ac3_frame->sampling_rate != this->output_sampling_rate) ) {
      
      if (this->output_open)
	this->audio_out->close (this->audio_out);

      this->output_open = (this->audio_out->open (this->audio_out, 16, 
						  ac3_frame->sampling_rate,
						  AO_CAP_MODE_STEREO) == 1);
      this->output_sampling_rate = ac3_frame->sampling_rate;
    }

    if (this->output_open) {
      this->audio_out->write_audio_data (this->audio_out,
					 ac3_frame->audio_data,
					 256*6,
					 this->pts);
      this->pts = 0;
    }

    /* done with frame, prepare for next one */

    this->syncword   = 0;
    this->sync_todo  = 6;

  }
}

void ac3dec_close (audio_decoder_t *this_gen) {

  ac3dec_decoder_t *this = (ac3dec_decoder_t *) this_gen; 

  if (this->output_open) 
    this->audio_out->close (this->audio_out);
}

static char *ac3dec_get_id(void) {
  return "ac3dec";
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  ac3dec_decoder_t *this ;

  if (iface_version != 1)
    return NULL;

  this = (ac3dec_decoder_t *) malloc (sizeof (ac3dec_decoder_t));

  this->audio_decoder.interface_version   = 1;
  this->audio_decoder.can_handle          = ac3dec_can_handle;
  this->audio_decoder.init                = ac3dec_init;
  this->audio_decoder.decode_data         = ac3dec_decode_data;
  this->audio_decoder.close               = ac3dec_close;
  this->audio_decoder.get_identifier      = ac3dec_get_id;
  
  return (audio_decoder_t *) this;
}

