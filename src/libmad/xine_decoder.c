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
 * $Id: xine_decoder.c,v 1.1 2001/08/12 02:57:55 guenter Exp $
 *
 * stuff needed to turn libmad into a xine decoder plugin
 */

#include <stdlib.h>
#include <string.h>

#include "audio_out.h"
#include "buffer.h"
#include "xine_internal.h"
#include "frame.h"
#include "synth.h"

#define MAX_NUM_SAMPLES 8192
#define INPUT_BUF_SIZE  16384

typedef struct mad_decoder_s {
  audio_decoder_t   audio_decoder;

  uint32_t          pts;

  struct mad_synth  synth; 
  struct mad_stream stream;
  struct mad_frame  frame;

  ao_functions_t   *audio_out;
  int               output_sampling_rate;
  int               output_open;

  int16_t           samples[MAX_NUM_SAMPLES];

  uint8_t           buffer[INPUT_BUF_SIZE];
  int               bytes_in_buffer;

} mad_decoder_t;

static int mad_can_handle (audio_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_AUDIO_MPEG) ;
}


static void mad_init (audio_decoder_t *this_gen, ao_functions_t *audio_out) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen;

  this->audio_out       = audio_out;
  this->output_open     = 0;
  this->bytes_in_buffer = 0;

  mad_synth_init  (&this->synth);
  mad_stream_init (&this->stream);
  mad_frame_init  (&this->frame);

}

/* utility to scale and round samples to 16 bits */

static inline
signed int scale(mad_fixed_t sample)
{
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 16));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 16);
}

/*
static int head_check(mad_decoder_t *this) {

  if( (this->header & 0xffe00000) != 0xffe00000)
    return 0;
  if(!((this->header>>17)&3))
    return 0;
  if( ((this->header>>12)&0xf) == 0xf)
    return 0;
  if( ((this->header>>10)&0x3) == 0x3 )
    return 0;
  return 1;
}
*/

static void mad_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen;

  /*  printf ("libmad: decode data\n");
      fflush (stdout); */

  if (buf->size>(INPUT_BUF_SIZE-this->bytes_in_buffer)) {
    printf ("libmad: ALERT input buffer too small!\n");
  }
  
  if (buf->decoder_info[0] >0) {

    memcpy (&this->buffer[this->bytes_in_buffer], 
	    buf->content, buf->size);
    this->bytes_in_buffer += buf->size;

    /*
      printf ("libmad: decode data - doing it\n");
      fflush (stdout);
      */

    mad_stream_buffer (&this->stream, this->buffer, 
		       this->bytes_in_buffer);

    while (1) {

      if (mad_frame_decode (&this->frame, &this->stream) != 0) {

	if (this->stream.next_frame) {
	  int num_bytes =
	    this->buffer + this->bytes_in_buffer - this->stream.next_frame;
	  
	  /* printf("libmad: MAD_ERROR_BUFLEN\n"); */

	  memmove(this->buffer, this->stream.next_frame, num_bytes);
	  this->bytes_in_buffer = num_bytes;
	}

	switch (this->stream.error) {

	case MAD_ERROR_BUFLEN:
	  return;

	default: 
	  mad_stream_buffer (&this->stream, this->buffer, 
			     this->bytes_in_buffer);
	}

      } else {

	if (!this->output_open) {

	  printf ("libmad: audio sample rate %d mode %d\n",
		  this->frame.header.samplerate,
		  this->frame.header.mode);

	  this->audio_out->open(this->audio_out,
				16, this->frame.header.samplerate, 
				(this->frame.header.mode == MAD_MODE_SINGLE_CHANNEL) ? AO_CAP_MODE_MONO : AO_CAP_MODE_STEREO);
	  this->output_open = 1;
	  this->output_sampling_rate = this->frame.header.samplerate;
	}

	mad_synth_frame (&this->synth, &this->frame);

	{
	  unsigned int nchannels, nsamples;
	  mad_fixed_t const *left_ch, *right_ch;
	  struct mad_pcm *pcm = &this->synth.pcm;
	  uint16_t *output = this->samples;

	  nchannels = pcm->channels;
	  nsamples  = pcm->length;
	  left_ch   = pcm->samples[0];
	  right_ch  = pcm->samples[1];
	  
	  while (nsamples--) {
	    /* output sample(s) in 16-bit signed little-endian PCM */
	    
	    *output++ = scale(*left_ch++);
	    
	    if (nchannels == 2) 
	      *output++ = scale(*right_ch++);

	  }

	  this->audio_out->write_audio_data (this->audio_out,
					     this->samples,
					     pcm->length,
					     buf->PTS);
	  buf->PTS = 0;

	}
	/* printf ("libmad: decode worked\n"); */
      }
    } 

  }
}

static void mad_close (audio_decoder_t *this_gen) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen; 

  mad_synth_finish (&this->synth);
  mad_frame_finish (&this->frame);
  mad_stream_finish(&this->stream);

  if (this->output_open) 
    this->audio_out->close (this->audio_out);
}

static char *mad_get_id(void) {
  return "mad";
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  mad_decoder_t *this ;

  if (iface_version != 2) {
    printf( "libmad: plugin doesn't support plugin API version %d.\n"
	    "libmad: this means there's a version mismatch between xine and this "
	    "libmad: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);

    return NULL;
  }

  this = (mad_decoder_t *) malloc (sizeof (mad_decoder_t));

  this->audio_decoder.interface_version   = 2;
  this->audio_decoder.can_handle          = mad_can_handle;
  this->audio_decoder.init                = mad_init;
  this->audio_decoder.decode_data         = mad_decode_data;
  this->audio_decoder.close               = mad_close;
  this->audio_decoder.get_identifier      = mad_get_id;
  this->audio_decoder.priority            = 5;
  
  return (audio_decoder_t *) this;
}

