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
 * $Id: xine_decoder.c,v 1.2 2001/11/10 13:48:02 guenter Exp $
 *
 * 04-09-2001 DTS passtrough  (C) Joachim Koenig 
 *
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h> /* ntohs */
#include "audio_out.h"
#include "buffer.h"
#include "xine_internal.h"


typedef struct dts_decoder_s {
  audio_decoder_t  audio_decoder;

  uint32_t         rate;
  uint32_t         bits_per_sample; 
  uint32_t         number_of_channels; 
  uint32_t	   audio_caps; 
   
  ao_instance_t   *audio_out;
  int              output_open;
} dts_decoder_t;

int dts_can_handle (audio_decoder_t *this_gen, int buf_type) {
  buf_type &= 0xFFFF0000;

  return ( buf_type == BUF_AUDIO_DTS);
}


void dts_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  dts_decoder_t *this = (dts_decoder_t *) this_gen;

  this->audio_out     = audio_out;
  this->output_open   = 0;
  this->rate          = 48000;
  this->bits_per_sample=16; 
  this->number_of_channels=2; 
  this->audio_caps    = audio_out->get_capabilities(audio_out); 
}


void dts_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  dts_decoder_t  *this = (dts_decoder_t *) this_gen;
  int16_t        *sample_buffer=(int16_t *)buf->content;
  audio_buffer_t *audio_buffer;

  if ((this->audio_caps & AO_CAP_MODE_AC5) == 0) {
    return;
  }

  if (!this->output_open) {      
    this->output_open = (this->audio_out->open (this->audio_out, this->bits_per_sample, 
                                                this->rate,
                                                AO_CAP_MODE_AC5));
  }
  if (!this->output_open) 
    return;
  
  audio_buffer = this->audio_out->get_buffer (this->audio_out);

  audio_buffer->vpts       = buf->PTS;
  audio_buffer->scr        = buf->SCR;
  audio_buffer->num_frames = 1536;
  
  this->audio_out->put_buffer (this->audio_out, audio_buffer);

}

void dts_close (audio_decoder_t *this_gen) {

  dts_decoder_t *this = (dts_decoder_t *) this_gen; 
printf("libdts: close \n");
  if (this->output_open) 
    this->audio_out->close (this->audio_out);
  this->output_open = 0;
}

static char *dts_get_id(void) {
  return "dts";
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  dts_decoder_t *this ;

  if (iface_version != 2) {
    printf( "libdts: plugin doesn't support plugin API version %d.\n"
	    "libdts: this means there's a version mismatch between xine and this "
	    "libdts: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  this = (dts_decoder_t *) malloc (sizeof (dts_decoder_t));

  this->audio_decoder.interface_version   = 2;
  this->audio_decoder.can_handle          = dts_can_handle;
  this->audio_decoder.init                = dts_init;
  this->audio_decoder.decode_data         = dts_decode_data;
  this->audio_decoder.close               = dts_close;
  this->audio_decoder.get_identifier      = dts_get_id;
  this->audio_decoder.priority            = 1;
    
  return (audio_decoder_t *) this;
}

