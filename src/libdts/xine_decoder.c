/* 
 * Copyright (C) 2000-2004 the xine project
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
 * $Id: xine_decoder.c,v 1.50 2004/05/18 20:38:28 jcdutton Exp $
 *
 * 04-09-2001 DTS passtrough  (C) Joachim Koenig 
 * 09-12-2001 DTS passthrough inprovements (C) James Courtier-Dutton
 *
 */

#ifndef __sun
/* required for swab() */
#define _XOPEN_SOURCE 500
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#define LOG_MODULE "libdts"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine_internal.h"
#include "xineutils.h"
#include "audio_out.h"
#include "buffer.h"
#include "dts.h"


typedef struct {
  audio_decoder_class_t   decoder_class;
} dts_class_t;

typedef struct {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;
  audio_decoder_class_t *class;

  dts_state_t     *dts_state;
  
  uint32_t         rate;
  uint32_t         bits_per_sample;
  uint32_t         number_of_channels;

  int              output_open;
  
  int              bypass_mode;
  int              decoder_flags;
  int              decoder_sample_rate;
  int              decoder_bit_rate;
  
  
} dts_decoder_t;

static void dts_reset (audio_decoder_t *this_gen);
static void dts_discontinuity (audio_decoder_t *this_gen);

static void dts_reset (audio_decoder_t *this_gen) {

  /* dts_decoder_t *this = (dts_decoder_t *) this_gen; */

}

static void dts_discontinuity (audio_decoder_t *this_gen) {
}

static inline int16_t blah (int32_t i) {

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

static void dts_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  dts_decoder_t  *this = (dts_decoder_t *) this_gen;
  uint8_t        *data_in = (uint8_t *)buf->content;
  uint8_t        *data_out;
  audio_buffer_t *audio_buffer;
  uint32_t  ac5_spdif_type=0;
  uint32_t  ac5_length=0;
  uint32_t  ac5_pcm_length;
  uint32_t  number_of_frames;
  uint32_t  first_access_unit;
  int       old_decoder_flags;
  int       old_decoder_sample_rate;
  int       old_decoder_bit_rate;
  int n;
  
  lprintf("decode_data\n");

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  number_of_frames = buf->decoder_info[1];  /* Number of frames  */
  first_access_unit = buf->decoder_info[2]; /* First access unit */

  if (number_of_frames > 2) {
    return;
  }
  for(n=1;n<=number_of_frames;n++) {
    data_in += ac5_length;
    if(data_in >= (buf->content+buf->size)) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: DTS length error\n");
      return;
    }
    old_decoder_flags = this->decoder_flags;
    old_decoder_sample_rate = this->decoder_sample_rate;
    old_decoder_bit_rate = this->decoder_bit_rate;
     
    ac5_length = dts_syncinfo(this->dts_state, data_in, &this->decoder_flags, &this->decoder_sample_rate, 
                              &this->decoder_bit_rate, &ac5_pcm_length);
    
    if(!ac5_length) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: DTS Sync bad\n");
      return;
    }

    if (!_x_meta_info_get(this->stream, XINE_META_INFO_AUDIOCODEC) ||
          old_decoder_flags       != this->decoder_flags ||
          old_decoder_sample_rate != this->decoder_sample_rate ||
          old_decoder_bit_rate    != this->decoder_bit_rate) {
      _x_meta_info_set(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS");
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, this->decoder_bit_rate);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, this->decoder_sample_rate);
    }


    audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
    
    if (n == first_access_unit) {
      audio_buffer->vpts       = buf->pts;
    } else {
      audio_buffer->vpts       = 0;
    }
    
    if(this->bypass_mode) {
      if (!this->output_open) {
        this->output_open = (this->stream->audio_out->open (this->stream->audio_out, this->stream,
                                                            this->bits_per_sample, 
                                                            this->rate,
                                                            AO_CAP_MODE_AC5));
      }
      
      if (!this->output_open) 
        return;
      
      audio_buffer->frame_header_count = buf->decoder_info[1]; /* Number of frames */
      audio_buffer->first_access_unit = buf->decoder_info[2]; /* First access unit */

      lprintf("frame_header_count = %u\n",audio_buffer->frame_header_count);
      lprintf("first access unit = %u\n",audio_buffer->first_access_unit);

      data_out=(uint8_t *) audio_buffer->mem;

      if (ac5_length > 8191) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: ac5_length too long\n");
        ac5_pcm_length = 0;
      }

      switch (ac5_pcm_length) {
      case 512:
        ac5_spdif_type = 0x0b; /* DTS-1 (512-sample bursts) */
        break;
      case 1024:
        ac5_spdif_type = 0x0c; /* DTS-1 (1024-sample bursts) */
        break;
      case 2048:
        ac5_spdif_type = 0x0d; /* DTS-1 (2048-sample bursts) */
        break;
      default:
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, 
		"libdts: DTS %i-sample bursts not supported\n", ac5_pcm_length);
        return;
      }

#ifdef LOG_DEBUG
      {
        int i;
        printf("libdts: DTS frame type=%d\n",data_in[4] >> 7);
        printf("libdts: DTS deficit frame count=%d\n",(data_in[4] & 0x7f) >> 2);
        printf("libdts: DTS AC5 PCM samples=%d\n",ac5_pcm_samples);
        printf("libdts: DTS AC5 length=%d\n",ac5_length);
        printf("libdts: DTS AC5 bitrate=%d\n",((data_in[8] & 0x03) << 4) | (data_in[8] >> 4));
        printf("libdts: DTS AC5 spdif type=%d\n", ac5_spdif_type);

        printf("libdts: ");
        for(i=2000;i<2048;i++) {
          printf("%02x ",data_in[i]);
        }
        printf("\n");
      }
#endif


      lprintf("length=%d loop=%d pts=%lld\n",ac5_pcm_length,n,audio_buffer->vpts);

      audio_buffer->num_frames = ac5_pcm_length;

      data_out[0] = 0x72; data_out[1] = 0xf8;	/* spdif syncword    */
      data_out[2] = 0x1f; data_out[3] = 0x4e;	/* ..............    */
      data_out[4] = ac5_spdif_type;		/* DTS data          */
      data_out[5] = 0;		                /* Unknown */
      data_out[6] = (ac5_length << 3) & 0xff;   /* ac5_length * 8   */
      data_out[7] = ((ac5_length ) >> 5) & 0xff;

      if( ac5_pcm_length ) {
        if( ac5_pcm_length % 2) {
          swab(data_in, &data_out[8], ac5_length );
        } else {
          swab(data_in, &data_out[8], ac5_length + 1);
        }
      }
    } else {
      int       i, flags = DTS_STEREO;
      int16_t  *int_samples = audio_buffer->mem;
      level_t   level = 1.0;
      sample_t *samples = dts_samples(this->dts_state);
      
      if (!this->output_open) {      
        this->output_open = (this->stream->audio_out->open (this->stream->audio_out, this->stream,
                                                this->bits_per_sample, 
                                                this->rate,
                                                AO_CAP_MODE_STEREO));
      }
      
      if (!this->output_open) 
        return;
    
      if(dts_frame(this->dts_state, data_in, &flags, &level, 384)) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: dts_frame error\n");
        return;
      }
      
      for(i = 0; i < 2; i++) {
        if(dts_block(this->dts_state)) {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, 
                  "libdts: dts_block error on audio channel %d\n", i);
          audio_buffer->num_frames = 0;
        }
        
        float_to_int (&samples[0*256], int_samples+(i*256*2), 2);
        float_to_int (&samples[1*256], int_samples+(i*256*2)+1, 2);
      }
      
      audio_buffer->num_frames = 256*2;      
    }
    
    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
   
  }
}

static void dts_dispose (audio_decoder_t *this_gen) {
  dts_decoder_t *this = (dts_decoder_t *) this_gen; 
  
  if (this->output_open) 
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;
  
  free (this);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {
  dts_decoder_t *this ;

  lprintf("open_plugin\n");

  this = (dts_decoder_t *) xine_xmalloc (sizeof (dts_decoder_t));

  this->audio_decoder.decode_data         = dts_decode_data;
  this->audio_decoder.reset               = dts_reset;
  this->audio_decoder.discontinuity       = dts_discontinuity;
  this->audio_decoder.dispose             = dts_dispose;

  this->dts_state = dts_init(0);
  
  if(stream->audio_out->get_capabilities(stream->audio_out) & AO_CAP_MODE_AC5)
    this->bypass_mode = 1;
  else
    this->bypass_mode = 0;
  
  this->stream        = stream;
  this->class         = class_gen;
  this->output_open   = 0;
  this->rate          = 48000;
  this->bits_per_sample=16;
  this->number_of_channels=2;
  return &this->audio_decoder;
}

static char *get_identifier (audio_decoder_class_t *this) {
  return "DTS";
}

static char *get_description (audio_decoder_class_t *this) {
  return "DTS passthru audio format decoder plugin";
}

static void dispose_class (audio_decoder_class_t *this) {
  lprintf("dispose_class\n");

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {
  dts_class_t *this ;

  lprintf("init_plugin\n");

  this = (dts_class_t *) xine_xmalloc (sizeof (dts_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

static uint32_t audio_types[] = { 
  BUF_AUDIO_DTS, 0
 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 15, "dts", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
