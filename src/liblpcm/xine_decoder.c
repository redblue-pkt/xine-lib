/* 
 * Copyright (C) 2000-2002 the xine project
 * 
 * This file is part of xine, a free video player.
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
 * $Id: xine_decoder.c,v 1.25 2002/05/25 19:19:18 siggi Exp $
 * 
 * 31-8-2001 Added LPCM rate sensing.
 *   (c) 2001 James Courtier-Dutton James@superbug.demon.co.uk
 *
 * stuff needed to turn libac3 into a xine decoder plugin
 */
#ifndef __sun
#define _XOPEN_SOURCE 500
#endif

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

  uint32_t         rate;
  uint32_t         bits_per_sample; 
  uint32_t         number_of_channels; 
  uint32_t         ao_cap_mode; 
   
  ao_instance_t   *audio_out;
  int              output_open;
  int		   cpu_be;	/* TRUE, if we're a Big endian CPU */
} lpcm_decoder_t;

int lpcm_can_handle (audio_decoder_t *this_gen, int buf_type) {
  buf_type &= 0xFFFF0000;

  return ( buf_type == BUF_AUDIO_LPCM_BE ||
	   buf_type == BUF_AUDIO_LPCM_LE );
}


void lpcm_reset (audio_decoder_t *this_gen) {

  /* lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen; */

}

void lpcm_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;

  this->audio_out     = audio_out;
  this->output_open   = 0;
  this->rate          = 0;
  this->bits_per_sample=0; 
  this->number_of_channels=0; 
  this->ao_cap_mode=0; 
 
  this->cpu_be        = ( htons(1) == 1 );
}

void lpcm_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;
  int16_t        *sample_buffer=(int16_t *)buf->content;
  int             stream_be;
  audio_buffer_t *audio_buffer;
  
  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    this->rate=buf->decoder_info[1];
    this->bits_per_sample=buf->decoder_info[2] ; 
    this->number_of_channels=buf->decoder_info[3] ; 
    this->ao_cap_mode=(this->number_of_channels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO; 
    return;
  }

  if (!this->output_open) {      
    /*
     * with dvdnav we do not get a preview buffer with audio format
     * information (buf->decoder_flags & BUF_FLAG_PREVIEW).
     * grab the audio format from the first audio data buffer, in case
     * the audio format is not yet known.
     */
    if (this->rate == 0 && this->bits_per_sample == 0) {
      this->rate=buf->decoder_info[1];
      this->bits_per_sample=buf->decoder_info[2] ; 
      this->number_of_channels=buf->decoder_info[3] ; 
      this->ao_cap_mode=(this->number_of_channels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO; 
    }
    printf ("liblpcm: opening audio output (%d Hz sampling rate, mode=%d)\n",
	    this->rate, this->ao_cap_mode);
    this->output_open = this->audio_out->open (this->audio_out, 
						(this->bits_per_sample<=16)?this->bits_per_sample:16, 
						this->rate,
						this->ao_cap_mode) ;
  }
  if (!this->output_open) 
    return;

  audio_buffer = this->audio_out->get_buffer (this->audio_out);
  
  /* Swap LPCM samples into native byte order, if necessary */
  buf->type &= 0xffff0000;
  stream_be = ( buf->type == BUF_AUDIO_LPCM_BE );
  
  if( this->bits_per_sample == 16 ){
    if (stream_be != this->cpu_be)
      swab (sample_buffer, audio_buffer->mem, buf->size);
    else
      memcpy (audio_buffer->mem, sample_buffer, buf->size);
  }
  else if( this->bits_per_sample == 20 ) {
    uint8_t *s = (uint8_t *)sample_buffer;
    uint8_t *d = (uint8_t *)audio_buffer->mem;
    int n = buf->size;
    
    if (stream_be != this->cpu_be) {
      while( n >= 0 ) {
        swab( s, d, 8 );
        s += 10;
        d += 8;
        n -= 10; 
      }
    } else {
      while( n >= 0 ) {
        memcpy( d, s, 8 );
        s += 10;
        d += 8;
        n -= 10; 
      }
    }
  }
  else {
    memcpy (audio_buffer->mem, sample_buffer, buf->size);
  }
  
  audio_buffer->vpts       = buf->pts;
  audio_buffer->num_frames = (((buf->size*8)/this->number_of_channels)/this->bits_per_sample);

  this->audio_out->put_buffer (this->audio_out, audio_buffer);

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

static void lpcm_dispose (audio_decoder_t *this_gen) {
  free (this_gen);
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, xine_t *xine) {

  lpcm_decoder_t *this ;

  if (iface_version != 8) {
    printf( "liblpcm: plugin doesn't support plugin API version %d.\n"
	    "liblpcm: this means there's a version mismatch between xine and this "
	    "liblpcm: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  this = (lpcm_decoder_t *) malloc (sizeof (lpcm_decoder_t));

  this->audio_decoder.interface_version   = iface_version;
  this->audio_decoder.can_handle          = lpcm_can_handle;
  this->audio_decoder.init                = lpcm_init;
  this->audio_decoder.decode_data         = lpcm_decode_data;
  this->audio_decoder.reset               = lpcm_reset;
  this->audio_decoder.close               = lpcm_close;
  this->audio_decoder.get_identifier      = lpcm_get_id;
  this->audio_decoder.dispose             = lpcm_dispose;
  this->audio_decoder.priority            = 1;
    
  return (audio_decoder_t *) this;
}

