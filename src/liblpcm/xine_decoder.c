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
 * $Id: xine_decoder.c,v 1.10 2001/09/16 23:13:45 f1rmb Exp $
 * 
 * 31-8-2001 Added LPCM rate sensing.
 *   (c) 2001 James Courtier-Dutton James@superbug.demon.co.uk
 *
 * stuff needed to turn libac3 into a xine decoder plugin
 */
#define _XOPEN_SOURCE 500

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


typedef struct lpcm_decoder_s {
  audio_decoder_t  audio_decoder;

  uint32_t         pts;
  uint32_t         rate;
  uint32_t         bits_per_sample; 
  uint32_t         number_of_channels; 
  uint32_t         ao_cap_mode; 
   
  ao_instance_t  *audio_out;
  int              output_open;
  int		   cpu_be;	/* TRUE, if we're a Big endian CPU */
} lpcm_decoder_t;

int lpcm_can_handle (audio_decoder_t *this_gen, int buf_type) {
  buf_type &= 0xFFFF0000;

  return ( buf_type == BUF_AUDIO_LPCM_BE ||
	   buf_type == BUF_AUDIO_LPCM_LE );
}


void lpcm_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;

  this->audio_out     = audio_out;
  this->output_open   = 0;
  this->pts           = 0;
  this->rate          = 0;
  this->bits_per_sample=0; 
  this->number_of_channels=0; 
  this->ao_cap_mode=0; 
 
  this->cpu_be        = ( htons(1) == 1 );
}


void lpcm_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;
  int16_t *sample_buffer=(int16_t *)buf->content;
  int stream_be;

  this->pts = buf->PTS;
  if (buf->decoder_info[0] == 0) {
    this->rate=buf->decoder_info[1];
    this->bits_per_sample=buf->decoder_info[2] ; 
    this->number_of_channels=buf->decoder_info[3] ; 
    this->ao_cap_mode=(this->number_of_channels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO; 
    return;
  }

  if (!this->output_open) {      
    printf ("liblpcm: opening audio output (%d Hz sampling rate, mode=%d)\n",
	    this->rate, this->ao_cap_mode);
    this->output_open = this->audio_out->open (this->audio_out, this->bits_per_sample, 
						this->rate,
						this->ao_cap_mode) ;
  }
  if (!this->output_open) 
    return;

  /* Swap LPCM samples into native byte order, if necessary */
  stream_be = ( buf->type == BUF_AUDIO_LPCM_BE );
  if (stream_be != this->cpu_be)
    swab(sample_buffer, sample_buffer, buf->size);

  this->audio_out->write (this->audio_out,
                            sample_buffer,
                            (((buf->size*8)/this->number_of_channels)/this->bits_per_sample),
                            this->pts);

  
}

void lpcm_close (audio_decoder_t *this_gen) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen; 

  if (this->output_open) 
    this->audio_out->close (this->audio_out);
  this->output_open = 0;
}

static char *lpcm_get_id(void) {
  return "lpcm";
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  lpcm_decoder_t *this ;

  if (iface_version != 2) {
    printf( "liblpcm: plugin doesn't support plugin API version %d.\n"
	    "liblpcm: this means there's a version mismatch between xine and this "
	    "liblpcm: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  this = (lpcm_decoder_t *) malloc (sizeof (lpcm_decoder_t));

  this->audio_decoder.interface_version   = 2;
  this->audio_decoder.can_handle          = lpcm_can_handle;
  this->audio_decoder.init                = lpcm_init;
  this->audio_decoder.decode_data         = lpcm_decode_data;
  this->audio_decoder.close               = lpcm_close;
  this->audio_decoder.get_identifier      = lpcm_get_id;
  this->audio_decoder.priority            = 1;
    
  return (audio_decoder_t *) this;
}

