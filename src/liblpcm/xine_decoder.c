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
 * $Id: xine_decoder.c,v 1.38 2002/11/20 11:57:43 mroi Exp $
 * 
 * 31-8-2001 Added LPCM rate sensing.
 *   (c) 2001 James Courtier-Dutton James@superbug.demon.co.uk
 *
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

#include "xine_internal.h"
#include "audio_out.h"
#include "buffer.h"

typedef struct {
  audio_decoder_class_t   decoder_class;
} lpcm_class_t;

typedef struct lpcm_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t   *stream;

  uint32_t         rate;
  uint32_t         bits_per_sample; 
  uint32_t         number_of_channels; 
  uint32_t         ao_cap_mode; 
   
  int              output_open;
  int		   cpu_be;	/* TRUE, if we're a Big endian CPU */
} lpcm_decoder_t;

void lpcm_reset (audio_decoder_t *this_gen) {

  /* lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen; */

}

void lpcm_discontinuity (audio_decoder_t *this_gen) {
}

void lpcm_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;
  int16_t        *sample_buffer=(int16_t *)buf->content;
  int             stream_be;
  audio_buffer_t *audio_buffer;
  int             format_changed = 0;
  
  /* Drop preview data */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  /* get config byte from mpeg2 stream */
  if ( (buf->decoder_flags & BUF_FLAG_SPECIAL) &&
        buf->decoder_info[1] == BUF_SPECIAL_LPCM_CONFIG ) {
    int bits_per_sample = 16;
    int sample_rate;
    int num_channels;
      
    num_channels = (buf->decoder_info[2] & 0x7) + 1;
    sample_rate = buf->decoder_info[2] & 0x10 ? 96000 : 48000;
    switch ((buf->decoder_info[2]>>6) & 3) {
      case 0: bits_per_sample = 16; break;
      case 1: bits_per_sample = 20; break;
      case 2: bits_per_sample = 24; break;
    }
    
    if( this->bits_per_sample != bits_per_sample ||
        this->number_of_channels != num_channels ||
        this->rate != sample_rate ||
        !this->output_open ) {
      this->bits_per_sample = bits_per_sample;
      this->number_of_channels = num_channels;
      this->rate = sample_rate;
      format_changed++;
    } 
  }
  
  if( buf->decoder_flags & BUF_FLAG_HEADER ) {
    this->rate=buf->decoder_info[1];
    this->bits_per_sample=buf->decoder_info[2] ; 
    this->number_of_channels=buf->decoder_info[3] ; 
    format_changed++;

    /* stream/meta info */
    this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup("Linear PCM");
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED] = 1;
  }
  
  /*
   * (re-)open output device
   */
  if ( format_changed ) {
    if (this->output_open)
        this->stream->audio_out->close (this->stream->audio_out, this->stream);

    this->ao_cap_mode=(this->number_of_channels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO; 
   
    this->output_open = this->stream->audio_out->open (this->stream->audio_out, this->stream,
                                               (this->bits_per_sample>16)?16:this->bits_per_sample,
                                               this->rate,
                                               this->ao_cap_mode) ;
  }

  if (!this->output_open || (buf->decoder_flags & BUF_FLAG_HEADER) ) 
    return;

  audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
  
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

  this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

}

static void lpcm_dispose (audio_decoder_t *this_gen) {
  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen; 

  if (this->output_open) 
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  lpcm_decoder_t *this ;

  this = (lpcm_decoder_t *) malloc (sizeof (lpcm_decoder_t));

  this->audio_decoder.decode_data         = lpcm_decode_data;
  this->audio_decoder.reset               = lpcm_reset;
  this->audio_decoder.discontinuity       = lpcm_discontinuity;
  this->audio_decoder.dispose             = lpcm_dispose;

  this->output_open   = 0;
  this->rate          = 0;
  this->bits_per_sample=0; 
  this->number_of_channels=0; 
  this->ao_cap_mode=0; 
  this->stream = stream;
 
  this->cpu_be        = ( htons(1) == 1 );

  return &this->audio_decoder;
}

static char *get_identifier (audio_decoder_class_t *this) {
  return "Linear PCM";
}

static char *get_description (audio_decoder_class_t *this) {
  return "Linear PCM audio decoder plugin";
}

static void dispose_class (audio_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  lpcm_class_t *this ;

  this = (lpcm_class_t *) malloc (sizeof (lpcm_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

static uint32_t audio_types[] = { 
  BUF_AUDIO_LPCM_BE, BUF_AUDIO_LPCM_LE, 0
};

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 12, "pcm", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
