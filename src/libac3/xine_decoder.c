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
 * $Id: xine_decoder.c,v 1.20 2001/07/30 17:13:21 guenter Exp $
 *
 * stuff needed to turn libac3 into a xine decoder plugin
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "audio_out.h"
#include "ac3.h"
#include "ac3_internal.h"
#include "buffer.h"
#include "xine_internal.h"

#define FRAME_SIZE 4096+512

#undef DEBUG_AC3
#ifdef DEBUG_AC3
int ac3file; 
#endif

typedef struct ac3dec_decoder_s {
  audio_decoder_t  audio_decoder;

  uint32_t         pts;
  uint32_t         last_pts;

  uint8_t          frame_buffer[FRAME_SIZE];
  uint8_t         *frame_ptr;
  int              sync_todo;
  int              frame_length, frame_todo;
  uint16_t         syncword;

  ac3_state_t      ac3_state;
  int              ac3_flags;
  int              ac3_bit_rate;
  int              ac3_sample_rate;
  float            ac3_level;
  int              have_lfe;

  int              ac3_flags_map[8];
  int              ao_flags_map[8];

  int16_t          int_samples [6 * 256];
  sample_t         delay[6*256];
  sample_t         samples[6][256];

  ao_functions_t  *audio_out;
  int              audio_caps;
  int              bypass_mode;
  int              output_sampling_rate;
  int              output_open;
  int              output_mode;

} ac3dec_decoder_t;

int ac3dec_can_handle (audio_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_AUDIO_AC3) ;
}


void ac3dec_init (audio_decoder_t *this_gen, ao_functions_t *audio_out) {

  ac3dec_decoder_t *this = (ac3dec_decoder_t *) this_gen;
  /* int i; */

  this->audio_out     = audio_out;
  this->audio_caps    = audio_out->get_capabilities(audio_out);
  this->syncword      = 0;
  this->sync_todo     = 7;
  this->output_open   = 0;
  this->pts           = 0;
  this->last_pts      = 0;

  ac3_init ();

  /*
   * find out if this driver supports ac3 output
   * or, if not, how many channels we've got
   */

  if (this->audio_caps & AO_CAP_MODE_AC3)
    this->bypass_mode = 1;
  else {
    this->bypass_mode = 0;

    this->ac3_flags_map[AC3_MONO]   = AC3_MONO;
    this->ac3_flags_map[AC3_STEREO] = AC3_STEREO;
    this->ac3_flags_map[AC3_3F]     = AC3_STEREO; 
    this->ac3_flags_map[AC3_2F1R]   = AC3_STEREO; 
    this->ac3_flags_map[AC3_3F1R]   = AC3_STEREO; 
    this->ac3_flags_map[AC3_2F2R]   = AC3_STEREO;
    this->ac3_flags_map[AC3_3F2R]   = AC3_STEREO;
    
    this->ao_flags_map[AC3_MONO]    = AO_CAP_MODE_MONO;
    this->ao_flags_map[AC3_STEREO]  = AO_CAP_MODE_STEREO;
    this->ao_flags_map[AC3_3F]      = AO_CAP_MODE_STEREO;
    this->ao_flags_map[AC3_2F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[AC3_3F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[AC3_2F2R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[AC3_3F2R]    = AO_CAP_MODE_STEREO;

    /* find best mode */
    if (this->audio_caps & AO_CAP_MODE_5_1CHANNEL) {

      this->ac3_flags_map[AC3_2F2R]   = AC3_2F2R;
      this->ac3_flags_map[AC3_3F2R]   = AC3_3F2R | AC3_LFE;
      this->ao_flags_map[AC3_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[AC3_3F2R]    = AO_CAP_MODE_5CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_5CHANNEL) {

      this->ac3_flags_map[AC3_2F2R]   = AC3_2F2R;
      this->ac3_flags_map[AC3_3F2R]   = AC3_3F2R;
      this->ao_flags_map[AC3_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[AC3_3F2R]    = AO_CAP_MODE_5CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_4CHANNEL) {

      this->ac3_flags_map[AC3_2F2R]   = AC3_2F2R;
      this->ac3_flags_map[AC3_3F2R]   = AC3_2F2R;

      this->ao_flags_map[AC3_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[AC3_3F2R]    = AO_CAP_MODE_4CHANNEL;

    /* else if (this->audio_caps & AO_CAP_MODE_STEREO)
       defaults are ok */
    } else if (!(this->audio_caps & AO_CAP_MODE_STEREO)) {
      printf ("HELP! a mono-only audio driver?!\n");

      this->ac3_flags_map[AC3_MONO]   = AC3_MONO;
      this->ac3_flags_map[AC3_STEREO] = AC3_MONO;
      this->ac3_flags_map[AC3_3F]     = AC3_MONO; 
      this->ac3_flags_map[AC3_2F1R]   = AC3_MONO; 
      this->ac3_flags_map[AC3_3F1R]   = AC3_MONO; 
      this->ac3_flags_map[AC3_2F2R]   = AC3_MONO;
      this->ac3_flags_map[AC3_3F2R]   = AC3_MONO;
      
      this->ao_flags_map[AC3_MONO]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[AC3_STEREO]  = AO_CAP_MODE_MONO;
      this->ao_flags_map[AC3_3F]      = AO_CAP_MODE_MONO;
      this->ao_flags_map[AC3_2F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[AC3_3F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[AC3_2F2R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[AC3_3F2R]    = AO_CAP_MODE_MONO;
    }
  }

  /*
  for (i = 0; i<8; i++)
    this->ac3_flags_map[i] |= AC3_ADJUST_LEVEL;
*/
#ifdef DEBUG_AC3
  ac3file = open ("test.ac3", O_CREAT | O_WRONLY | O_TRUNC, 0644); 
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

static void ac3dec_decode_frame (ac3dec_decoder_t *this, uint32_t pts) {

  int output_mode = AO_CAP_MODE_STEREO;

  /* 
   * do we want to decode this frame in software?
   */
  
  if (!this->bypass_mode) {
    
    int ac3_output_flags, i;
    float level = this->ac3_level;
    
    /* 
     * oki, decode this frame in software
     */
    
    /* determine output mode */
    
    ac3_output_flags = this->ac3_flags_map[this->ac3_flags & AC3_CHANNEL_MASK];
    
    if (ac3_frame (&this->ac3_state, 
		   this->frame_buffer, 
		   &ac3_output_flags,
		   &level, 384, this->delay)) {
      printf ("libac3: ac3_frame error\n");
      return;
    }
    
    this->have_lfe = ac3_output_flags & AC3_LFE;
    if (this->have_lfe)
      output_mode = AO_CAP_MODE_5_1CHANNEL;
    else
      output_mode = this->ao_flags_map[ac3_output_flags];
    
    /*
     * (re-)open output device
     */
    
    if (!this->output_open 
	|| (this->ac3_sample_rate != this->output_sampling_rate) 
	|| (output_mode != this->output_mode)) {
      
      if (this->output_open)
	this->audio_out->close (this->audio_out);
      
      
      this->output_open = (this->audio_out->open (this->audio_out, 16, 
						  this->ac3_sample_rate,
						  output_mode) == 1);
      this->output_sampling_rate = this->ac3_sample_rate;
      this->output_mode = output_mode;
    }
    
    
    if (!this->output_open) 
      return;
    
    
    /*
     * decode ac3 and convert/interleave samples
     */

    for (i = 0; i < 6; i++) {
      if (ac3_block (&this->ac3_state, this->samples)) {
	printf ("libac3: ac3_block error\n");
	return; 
      }
      
      switch (output_mode) {
      case AO_CAP_MODE_MONO:
	float_to_int (this->samples[0], this->int_samples, 1);
	break;
      case AO_CAP_MODE_STEREO:
	float_to_int (this->samples[0], this->int_samples, 2);
	float_to_int (this->samples[1], this->int_samples+1, 2);
	break;
      case AO_CAP_MODE_4CHANNEL:
	float_to_int (this->samples[0], this->int_samples,   4); /*  L */
	float_to_int (this->samples[1], this->int_samples+1, 4); /*  R */
	float_to_int (this->samples[2], this->int_samples+2, 4); /* RL */
	float_to_int (this->samples[3], this->int_samples+3, 4); /* RR */
	break;
      case AO_CAP_MODE_5CHANNEL:
	float_to_int (this->samples[0], this->int_samples+0, 5); /*  L */
	float_to_int (this->samples[1], this->int_samples+4, 5); /*  C */
	float_to_int (this->samples[2], this->int_samples+1, 5); /*  R */
	float_to_int (this->samples[3], this->int_samples+2, 5); /* RL */
	float_to_int (this->samples[4], this->int_samples+3, 5); /* RR */
	break;
      case AO_CAP_MODE_5_1CHANNEL:
	float_to_int (this->samples[0], this->int_samples+5, 6); /* lfe */
	float_to_int (this->samples[1], this->int_samples+0, 6); /*   L */
	float_to_int (this->samples[2], this->int_samples+4, 6); /*   C */
	float_to_int (this->samples[3], this->int_samples+1, 6); /*   R */
	float_to_int (this->samples[4], this->int_samples+2, 6); /*  RL */
	float_to_int (this->samples[5], this->int_samples+3, 6); /*  RR */
	break;
      default:
	printf ("libac3: help - unsupported mode %08x\n", output_mode);
      }
      
      /*  output decoded samples */
      
      this->audio_out->write_audio_data (this->audio_out,
					 this->int_samples,
					 256,
					 pts);
      pts = 0;
    }
    
  } else {

    /*
     * loop through ac3 data
     */
    
    if (!this->output_open) {
      
      int sample_rate, bit_rate, flags;
      
      ac3_syncinfo (this->frame_buffer, &flags, &sample_rate, &bit_rate);
      
      this->output_open = (this->audio_out->open (this->audio_out, 16, 
						  sample_rate,
						  AO_CAP_MODE_AC3) == 1);
      this->output_mode = AO_CAP_MODE_AC3;
    }
    
    if (this->output_open) {
      this->audio_out->write_audio_data (this->audio_out,
					 (int16_t*)this->frame_buffer,
					 this->frame_length,
					 pts);
    }
  }
}

void ac3dec_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  ac3dec_decoder_t *this = (ac3dec_decoder_t *) this_gen;
  uint8_t          *current = buf->content;
  uint8_t          *end = buf->content + buf->size;
  uint8_t           byte;
  
  if (buf->decoder_info[0] == 0)
    return;
  
  /*
  printf ("libac3: got buffer, pts =%d, pts - last_pts=%d\n", 
	  buf->PTS, buf->PTS - this->last_pts);

  this->last_pts = buf->PTS;
  */

  if (buf->PTS) 
    this->pts = buf->PTS; 

  
  while (current != end) {

    if ( (this->sync_todo == 0) && (this->frame_todo == 0) ) {
      ac3dec_decode_frame (this, this->pts);
#ifdef DEBUG_AC3
      write (ac3file, this->frame_buffer, this->frame_length);
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

	    this->frame_length = ac3_syncinfo (this->frame_buffer,
					       &this->ac3_flags,
					       &this->ac3_sample_rate,
					       &this->ac3_bit_rate);
	    if (this->frame_length) {
	      this->frame_todo = this->frame_length - 7;
	    } else {
	      this->sync_todo = 7;
	      this->syncword  = 0;
	      printf ("libac3: skip frame of zero length\n");
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

void ac3dec_close (audio_decoder_t *this_gen) {

  ac3dec_decoder_t *this = (ac3dec_decoder_t *) this_gen; 

  if (this->output_open) 
    this->audio_out->close (this->audio_out);

  this->output_open = 0;

#ifdef DEBUG_AC3
  close (ac3file); 
#endif
}

static char *ac3dec_get_id(void) {
  return "ac3dec";
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  ac3dec_decoder_t *this ;

  if (iface_version != 2) {
    printf( "libac3: plugin doesn't support plugin API version %d.\n"
	    "libac3: this means there's a version mismatch between xine and this "
	    "libac3: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  this = (ac3dec_decoder_t *) malloc (sizeof (ac3dec_decoder_t));

  this->audio_decoder.interface_version   = 2;
  this->audio_decoder.can_handle          = ac3dec_can_handle;
  this->audio_decoder.init                = ac3dec_init;
  this->audio_decoder.decode_data         = ac3dec_decode_data;
  this->audio_decoder.close               = ac3dec_close;
  this->audio_decoder.get_identifier      = ac3dec_get_id;
  this->audio_decoder.priority            = 0;
  

  this->ac3_level = (float) cfg->lookup_int (cfg, "ac3_level", 100) / 100.0;
  
  return (audio_decoder_t *) this;
}

