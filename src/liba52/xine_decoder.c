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
 * $Id: xine_decoder.c,v 1.6 2001/10/22 00:40:36 matt2000 Exp $
 *
 * stuff needed to turn liba52 into a xine decoder plugin
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "audio_out.h"
#include "a52.h"
#include "a52_internal.h"
#include "buffer.h"
#include "xine_internal.h"
#include "cpu_accel.h"

#undef DEBUG_A52
#ifdef DEBUG_A52
int a52file; 
#endif

typedef struct a52dec_decoder_s {
  audio_decoder_t  audio_decoder;

  uint32_t         pts;
  uint32_t         last_pts;

  uint8_t          frame_buffer[3840];
  uint8_t         *frame_ptr;
  int              sync_todo;
  int              frame_length, frame_todo;
  uint16_t         syncword;

  a52_state_t      a52_state;
  int              a52_flags;
  int              a52_bit_rate;
  int              a52_sample_rate;
  float            a52_level;
  int              have_lfe;

  int              a52_flags_map[11];
  int              ao_flags_map[11];

  int16_t          int_samples [6 * 256 * 6];
  sample_t        *samples;

  ao_instance_t	  *audio_out;
  int              audio_caps;
  int              bypass_mode;
  int              output_sampling_rate;
  int              output_open;
  int              output_mode;

  int              disable_dynrng;
  int              enable_surround_downmix;
} a52dec_decoder_t;

int a52dec_can_handle (audio_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_AUDIO_A52) ;
}


void a52dec_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen;
  /* int i; */

  this->audio_out     = audio_out;
  this->audio_caps    = audio_out->get_capabilities(audio_out);
  this->syncword      = 0;
  this->sync_todo     = 7;
  this->output_open   = 0;
  this->pts           = 0;
  this->last_pts      = 0;

  this->samples = a52_init (mm_accel());

  /*
   * find out if this driver supports a52 output
   * or, if not, how many channels we've got
   */

  if (this->audio_caps & AO_CAP_MODE_A52)
    this->bypass_mode = 1;
  else {
    this->bypass_mode = 0;
     
    this->a52_flags_map[A52_MONO]   = A52_MONO;
    this->a52_flags_map[A52_STEREO] = ((this->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_3F]     = ((this->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_2F1R]   = ((this->enable_surround_downmix ? A52_DOLBY : A52_STEREO)); 
    this->a52_flags_map[A52_3F1R]   = ((this->enable_surround_downmix ? A52_DOLBY : A52_STEREO)); 
    this->a52_flags_map[A52_2F2R]   = ((this->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_3F2R]   = ((this->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    this->a52_flags_map[A52_DOLBY]  = ((this->enable_surround_downmix ? A52_DOLBY : A52_STEREO));
    
    this->ao_flags_map[A52_MONO]    = AO_CAP_MODE_MONO;
    this->ao_flags_map[A52_STEREO]  = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_3F]      = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_2F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_3F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[A52_DOLBY]   = AO_CAP_MODE_STEREO;

    /* find best mode */
    if (this->audio_caps & AO_CAP_MODE_5_1CHANNEL) {

      this->a52_flags_map[A52_2F2R]   = A52_2F2R;
      this->a52_flags_map[A52_3F2R]   = A52_3F2R | A52_LFE;
      this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_5CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_5CHANNEL) {

      this->a52_flags_map[A52_2F2R]   = A52_2F2R;
      this->a52_flags_map[A52_3F2R]   = A52_3F2R;
      this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_5CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_4CHANNEL) {

      this->a52_flags_map[A52_2F2R]   = A52_2F2R;
      this->a52_flags_map[A52_3F2R]   = A52_2F2R;

      this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_4CHANNEL;

    /* else if (this->audio_caps & AO_CAP_MODE_STEREO)
       defaults are ok */
    } else if (!(this->audio_caps & AO_CAP_MODE_STEREO)) {
      printf ("HELP! a mono-only audio driver?!\n");

      this->a52_flags_map[A52_MONO]   = A52_MONO;
      this->a52_flags_map[A52_STEREO] = A52_MONO;
      this->a52_flags_map[A52_3F]     = A52_MONO; 
      this->a52_flags_map[A52_2F1R]   = A52_MONO; 
      this->a52_flags_map[A52_3F1R]   = A52_MONO; 
      this->a52_flags_map[A52_2F2R]   = A52_MONO;
      this->a52_flags_map[A52_3F2R]   = A52_MONO;
      this->a52_flags_map[A52_DOLBY]  = A52_MONO;
      
      this->ao_flags_map[A52_MONO]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_STEREO]  = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_3F]      = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_2F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_3F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_2F2R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_3F2R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[A52_DOLBY]   = AO_CAP_MODE_MONO;
    }
  }

  /*
  for (i = 0; i<8; i++)
    this->a52_flags_map[i] |= A52_ADJUST_LEVEL;
*/
#ifdef DEBUG_A52
  a52file = open ("test.a52", O_CREAT | O_WRONLY | O_TRUNC, 0644); 
#endif
}

static inline int16_t blah (int32_t i)
{
    if (i > 0x43c07fff)
        return 32767;
    else if (i < 0x43bf8000)
        return -32768;
    else
        return i - 0x43c00000;
}

static inline void float_to_int (float * _f, int16_t * s16, int num_channels) {
    int i;
    int32_t * f = (int32_t *) _f;       /* XXX assumes IEEE float format */

    for (i = 0; i < 256; i++) {
        s16[num_channels*i] = blah (f[i]);
    }
}

static void a52dec_decode_frame (a52dec_decoder_t *this, uint32_t pts) {

  int output_mode = AO_CAP_MODE_STEREO;

  /* 
   * do we want to decode this frame in software?
   */
  
  if (!this->bypass_mode) {
    
    int a52_output_flags, i;
    sample_t level = this->a52_level;
    
    /* 
     * oki, decode this frame in software
     */
    
    /* determine output mode */
    
    a52_output_flags = this->a52_flags_map[this->a52_flags & A52_CHANNEL_MASK];
    
    if (a52_frame (&this->a52_state, 
		   this->frame_buffer, 
		   &a52_output_flags,
		   &level, 384)) {
      printf ("liba52: a52_frame error\n");
      return;
    }
    
    if (this->disable_dynrng)
      a52_dynrng (&this->a52_state, NULL, NULL);

    this->have_lfe = a52_output_flags & A52_LFE;
    if (this->have_lfe)
      output_mode = AO_CAP_MODE_5_1CHANNEL;
    else
      output_mode = this->ao_flags_map[a52_output_flags];
    
    /*
     * (re-)open output device
     */
    
    if (!this->output_open 
	|| (this->a52_sample_rate != this->output_sampling_rate) 
	|| (output_mode != this->output_mode)) {
      
      if (this->output_open)
	this->audio_out->close (this->audio_out);
      
      
      this->output_open = this->audio_out->open (this->audio_out, 16, 
						  this->a52_sample_rate,
						  output_mode) ;
      this->output_sampling_rate = this->a52_sample_rate;
      this->output_mode = output_mode;
    }
    
    
    if (!this->output_open) 
      return;
    
    
    /*
     * decode a52 and convert/interleave samples
     */

    for (i = 0; i < 6; i++) {
      if (a52_block (&this->a52_state, this->samples)) {
	printf ("liba52: a52_block error\n");
	return; 
      }
      
      switch (output_mode) {
      case AO_CAP_MODE_MONO:
	float_to_int (&this->samples[0], this->int_samples+(i*256), 1);
	break;
      case AO_CAP_MODE_STEREO:
	float_to_int (&this->samples[0*256], this->int_samples+(i*256*2), 2);
	float_to_int (&this->samples[1*256], this->int_samples+(i*256*2)+1, 2);
	break;
      case AO_CAP_MODE_4CHANNEL:
	float_to_int (&this->samples[0*256], this->int_samples+(i*256*4),   4); /*  L */
	float_to_int (&this->samples[1*256], this->int_samples+(i*256*4)+1, 4); /*  R */
	float_to_int (&this->samples[2*256], this->int_samples+(i*256*4)+2, 4); /* RL */
	float_to_int (&this->samples[3*256], this->int_samples+(i*256*4)+3, 4); /* RR */
	break;
      case AO_CAP_MODE_5CHANNEL:
	float_to_int (&this->samples[0*256], this->int_samples+(i*256*5)+0, 5); /*  L */
	float_to_int (&this->samples[1*256], this->int_samples+(i*256*5)+4, 5); /*  C */
	float_to_int (&this->samples[2*256], this->int_samples+(i*256*5)+1, 5); /*  R */
	float_to_int (&this->samples[3*256], this->int_samples+(i*256*5)+2, 5); /* RL */
	float_to_int (&this->samples[4*256], this->int_samples+(i*256*5)+3, 5); /* RR */
	break;
      case AO_CAP_MODE_5_1CHANNEL:
	float_to_int (&this->samples[0*256], this->int_samples+(i*256*6)+5, 6); /* lfe */
	float_to_int (&this->samples[1*256], this->int_samples+(i*256*6)+0, 6); /*   L */
	float_to_int (&this->samples[2*256], this->int_samples+(i*256*6)+4, 6); /*   C */
	float_to_int (&this->samples[3*256], this->int_samples+(i*256*6)+1, 6); /*   R */
	float_to_int (&this->samples[4*256], this->int_samples+(i*256*6)+2, 6); /*  RL */
	float_to_int (&this->samples[5*256], this->int_samples+(i*256*6)+3, 6); /*  RR */
	break;
      default:
	printf ("liba52: help - unsupported mode %08x\n", output_mode);
      }
    }
      
    /*  output decoded samples */
      
    this->audio_out->write (this->audio_out,
					 this->int_samples,
					 256*6,
					 pts);
    pts = 0;
    
  } else {

    /*
     * loop through a52 data
     */
    
    if (!this->output_open) {
      
      int sample_rate, bit_rate, flags;
      
      a52_syncinfo (this->frame_buffer, &flags, &sample_rate, &bit_rate);
      
      this->output_open = this->audio_out->open (this->audio_out, 16, 
						  sample_rate,
						  AO_CAP_MODE_A52) ;
      this->output_mode = AO_CAP_MODE_A52;
    }
    
    if (this->output_open) {
      this->audio_out->write (this->audio_out,
					 (int16_t*)this->frame_buffer,
					 this->frame_length,
					 pts);
    }
  }
}

void a52dec_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen;
  uint8_t          *current = buf->content;
  uint8_t          *end = buf->content + buf->size;
  uint8_t           byte;
  
  if (buf->decoder_info[0] == 0)
    return;
  
  /*
  printf ("liba52: got buffer, pts =%d, pts - last_pts=%d\n", 
	  buf->PTS, buf->PTS - this->last_pts);

  this->last_pts = buf->PTS;
  */

  if (buf->PTS) 
    this->pts = buf->PTS; 

  
  while (current != end) {

    if ( (this->sync_todo == 0) && (this->frame_todo == 0) ) {
      a52dec_decode_frame (this, this->pts);
#ifdef DEBUG_A52
      write (a52file, this->frame_buffer, this->frame_length);
#endif
      this->pts = 0;
      this->sync_todo = 7;
      this->syncword  = 0;
    }
    
    while (1) {
      byte = *current++;

      if (this->sync_todo>0) {

	/* search and collect syncinfo */

	if (this->syncword != 0x0b77) {
	  this->syncword = (this->syncword << 8) | byte;

	  if (this->syncword == 0x0b77) {

	    this->frame_buffer[0] = 0x0b;
	    this->frame_buffer[1] = 0x77;
	  
	    this->sync_todo = 5;
	    this->frame_ptr = this->frame_buffer+2;
	  }
	} else {
	  *this->frame_ptr++ = byte;
	  this->sync_todo--;

	  if (this->sync_todo==0) {

	    this->frame_length = a52_syncinfo (this->frame_buffer,
					       &this->a52_flags,
					       &this->a52_sample_rate,
					       &this->a52_bit_rate);
	    if (this->frame_length) {
	      this->frame_todo = this->frame_length - 7;
	    } else {
	      this->sync_todo = 7;
	      this->syncword  = 0;
	      printf ("liba52: skip frame of zero length\n");
	    }

	  }

	}
      } else {

	*this->frame_ptr++ = byte;
	this->frame_todo--;
	
	if (this->frame_todo == 0) {
	  if (current == end) 
	    return ;
	  break;
	}
      }

      if (current == end) 
	return ;
    }
  }
}

void a52dec_close (audio_decoder_t *this_gen) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen; 

  if (this->output_open) 
    this->audio_out->close (this->audio_out);

  this->output_open = 0;

#ifdef DEBUG_A52
  close (a52file); 
#endif
}

static char *a52dec_get_id(void) {
  return "a/52dec";
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  a52dec_decoder_t *this ;

  if (iface_version != 2) {
    printf( "liba52: plugin doesn't support plugin API version %d.\n"
	    "liba52: this means there's a version mismatch between xine and this "
	    "liba52: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  this = (a52dec_decoder_t *) malloc (sizeof (a52dec_decoder_t));
  memset(this, 0, sizeof (a52dec_decoder_t));

  this->audio_decoder.interface_version   = 2;
  this->audio_decoder.can_handle          = a52dec_can_handle;
  this->audio_decoder.init                = a52dec_init;
  this->audio_decoder.decode_data         = a52dec_decode_data;
  this->audio_decoder.close               = a52dec_close;
  this->audio_decoder.get_identifier      = a52dec_get_id;
  this->audio_decoder.priority            = 2;
  

  this->a52_level = (float) cfg->lookup_int (cfg, "a52_level", 100) / 100.0;
  this->disable_dynrng = !cfg->lookup_int (cfg, "a52_dynrng", 0);
  this->enable_surround_downmix = cfg->lookup_int(cfg, "a52_surround_downmix", 0);

  return (audio_decoder_t *) this;
}

