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
 * $Id: xine_decoder.c,v 1.52 2004/05/19 19:41:28 jcdutton Exp $
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
  int              audio_caps;  
  uint32_t         rate;
  uint32_t         bits_per_sample;
  uint32_t         number_of_channels;

  int              output_open;
  
  int              bypass_mode;
  int              dts_flags;
  int              dts_sample_rate;
  int              dts_bit_rate;
  int              dts_flags_map[11]; /* Convert from stream dts_flags to the dts_flags we want from the dts downmixer */
  int              ao_flags_map[11];  /* Convert from the xine AO_CAP's to dts_flags. */
  int              have_lfe;
  
  
} dts_decoder_t;

static void dts_reset (audio_decoder_t *this_gen);
static void dts_discontinuity (audio_decoder_t *this_gen);

static void dts_reset (audio_decoder_t *this_gen) {

  /* dts_decoder_t *this = (dts_decoder_t *) this_gen; */

}

static void dts_discontinuity (audio_decoder_t *this_gen) {
}

#if 0
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
#endif

static inline void float_to_int (float * _f, int16_t * s16, int num_channels) {
  int i;
  float f;
  for (i = 0; i < 256; i++) {
    f = _f[i] * 32767;
    if (f > INT16_MAX) f = INT16_MAX;
    if (f < INT16_MIN) f = INT16_MIN;
    s16[num_channels*i] = f;
    /* printf("samples[%d] = %f, %d\n", i, _f[i], s16[num_channels*i]); */
  }
}


static inline void mute_channel (int16_t * s16, int num_channels) {
  int i;
                                                                                                                           
  for (i = 0; i < 256; i++) {
    s16[num_channels*i] = 0;
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
  int       old_dts_flags;
  int       old_dts_sample_rate;
  int       old_dts_bit_rate;
  int output_mode = AO_CAP_MODE_STEREO;
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
    old_dts_flags = this->dts_flags;
    old_dts_sample_rate = this->dts_sample_rate;
    old_dts_bit_rate = this->dts_bit_rate;

    ac5_length = dts_syncinfo(this->dts_state, data_in, &this->dts_flags, &this->dts_sample_rate, 
                              &this->dts_bit_rate, &ac5_pcm_length);

    if(!ac5_length) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: DTS Sync bad\n");
      return;
    }

    if (!_x_meta_info_get(this->stream, XINE_META_INFO_AUDIOCODEC) ||
          old_dts_flags       != this->dts_flags ||
          old_dts_sample_rate != this->dts_sample_rate ||
          old_dts_bit_rate    != this->dts_bit_rate) {
      _x_meta_info_set(this->stream, XINE_META_INFO_AUDIOCODEC, "DTS");
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, this->dts_bit_rate);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, this->dts_sample_rate);
    }


    audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
    
    if (n == first_access_unit) {
      audio_buffer->vpts       = buf->pts;
    } else {
      audio_buffer->vpts       = 0;
    }
     
    if(this->bypass_mode) {
      /* SPDIF digital output */
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
      /* Software decode */
      int       i, dts_flags;
      int16_t  *int_samples = audio_buffer->mem;
      int       number_of_dts_blocks;

      level_t   level = 1.0;
      sample_t *samples;

      this->have_lfe = this->dts_flags & DTS_LFE;
      dts_flags = this->dts_flags_map[this->dts_flags & DTS_CHANNEL_MASK];
      if (this->have_lfe)
        if (this->audio_caps & AO_CAP_MODE_5_1CHANNEL) {
          output_mode = AO_CAP_MODE_5_1CHANNEL;
          dts_flags |= DTS_LFE;
        } else if (this->audio_caps & AO_CAP_MODE_4_1CHANNEL) {
          output_mode = AO_CAP_MODE_4_1CHANNEL;
          dts_flags |= DTS_LFE;
        } else {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "liba52: WHAT DO I DO!!!\n");
          output_mode = this->ao_flags_map[dts_flags & DTS_CHANNEL_MASK];
        }
      else
        output_mode = this->ao_flags_map[dts_flags & DTS_CHANNEL_MASK];

      if(dts_frame(this->dts_state, data_in, &dts_flags, &level, 0)) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libdts: dts_frame error\n");
        return;
      }
      if (!this->output_open) {
        this->output_open = (this->stream->audio_out->open (this->stream->audio_out, this->stream,
                                                this->bits_per_sample, 
                                                this->rate,
                                                output_mode));
      }
      
      if (!this->output_open) 
        return;
      number_of_dts_blocks = dts_blocks_num (this->dts_state); 
      audio_buffer->num_frames = 256*number_of_dts_blocks;
      for(i = 0; i < number_of_dts_blocks; i++) {
        if(dts_block(this->dts_state)) {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, 
                  "libdts: dts_block error on audio channel %d\n", i);
          audio_buffer->num_frames = 0;
          break;
        }

        samples = dts_samples(this->dts_state);
        switch (output_mode) {
        case AO_CAP_MODE_MONO:
          float_to_int (&samples[0], int_samples+(i*256), 1);
          break;
        case AO_CAP_MODE_STEREO:
          /* Tested, working. */
          float_to_int (&samples[0*256], int_samples+(i*256*2), 2);   /*  L */
          float_to_int (&samples[1*256], int_samples+(i*256*2)+1, 2); /*  R */
          break;
        case AO_CAP_MODE_4CHANNEL:
          /* Tested, working */
          float_to_int (&samples[0*256], int_samples+(i*256*4),   4); /*  L */
          float_to_int (&samples[1*256], int_samples+(i*256*4)+1, 4); /*  R */
          float_to_int (&samples[2*256], int_samples+(i*256*4)+2, 4); /* RL */
          float_to_int (&samples[3*256], int_samples+(i*256*4)+3, 4); /* RR */
          break;
        case AO_CAP_MODE_4_1CHANNEL:
          /* Tested, working */
          float_to_int (&samples[0*256], int_samples+(i*256*6)+0, 6); /*   L */
          float_to_int (&samples[1*256], int_samples+(i*256*6)+1, 6); /*   R */
          float_to_int (&samples[2*256], int_samples+(i*256*6)+2, 6); /*  RL */
          float_to_int (&samples[3*256], int_samples+(i*256*6)+3, 6); /*  RR */
          float_to_int (&samples[4*256], int_samples+(i*256*6)+5, 6); /* LFE */
          mute_channel ( int_samples+(i*256*6)+4, 6); /* C */
          break;
        case AO_CAP_MODE_5CHANNEL:
          /* Tested, working */
          float_to_int (&samples[0*256], int_samples+(i*256*6)+4, 6); /*   C */
          float_to_int (&samples[1*256], int_samples+(i*256*6)+0, 6); /*   L */
          float_to_int (&samples[2*256], int_samples+(i*256*6)+1, 6); /*   R */
          float_to_int (&samples[3*256], int_samples+(i*256*6)+2, 6); /*  RL */
          float_to_int (&samples[4*256], int_samples+(i*256*6)+3, 6); /*  RR */
          mute_channel ( int_samples+(i*256*6)+5, 6); /* LFE */
          break;
        case AO_CAP_MODE_5_1CHANNEL:
          float_to_int (&samples[0*256], int_samples+(i*256*6)+4, 6); /*   C */
          float_to_int (&samples[1*256], int_samples+(i*256*6)+0, 6); /*   L */
          float_to_int (&samples[2*256], int_samples+(i*256*6)+1, 6); /*   R */
          float_to_int (&samples[3*256], int_samples+(i*256*6)+2, 6); /*  RL */
          float_to_int (&samples[4*256], int_samples+(i*256*6)+3, 6); /*  RR */
          float_to_int (&samples[5*256], int_samples+(i*256*6)+5, 6); /* LFE */ /* Not working yet */
          break;
        default:
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "liba52: help - unsupported mode %08x\n", output_mode);
        }
      }
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
  this->audio_caps        = stream->audio_out->get_capabilities(stream->audio_out);
  if(this->audio_caps & AO_CAP_MODE_AC5)
    this->bypass_mode = 1;
  else {
    this->bypass_mode = 0;
    /* FIXME: Leave "DOLBY pro logic" downmix out for now. */

    this->dts_flags_map[DTS_MONO]   = DTS_MONO;
    this->dts_flags_map[DTS_STEREO] = DTS_STEREO;
    this->dts_flags_map[DTS_3F]     = DTS_STEREO;
    this->dts_flags_map[DTS_2F1R]   = DTS_STEREO;
    this->dts_flags_map[DTS_3F1R]   = DTS_STEREO;
    this->dts_flags_map[DTS_2F2R]   = DTS_STEREO;
    this->dts_flags_map[DTS_3F2R]   = DTS_STEREO;

    this->ao_flags_map[DTS_MONO]    = AO_CAP_MODE_MONO;
    this->ao_flags_map[DTS_STEREO]  = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_3F]      = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_2F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_3F1R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_STEREO;
    this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_STEREO;

    /* find best mode */
    if (this->audio_caps & AO_CAP_MODE_5_1CHANNEL) {

      this->dts_flags_map[DTS_2F2R]   = DTS_2F2R;
      this->dts_flags_map[DTS_3F2R]   = DTS_3F2R | DTS_LFE;
      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_5CHANNEL;
                                                                                                                           
    } else if (this->audio_caps & AO_CAP_MODE_5CHANNEL) {

      this->dts_flags_map[DTS_2F2R]   = DTS_2F2R;
      this->dts_flags_map[DTS_3F2R]   = DTS_3F2R;
      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_5CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_4_1CHANNEL) {

      this->dts_flags_map[DTS_2F2R]   = DTS_2F2R;
      this->dts_flags_map[DTS_3F2R]   = DTS_2F2R | DTS_LFE;
      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_4CHANNEL;

    } else if (this->audio_caps & AO_CAP_MODE_4CHANNEL) {

      this->dts_flags_map[DTS_2F2R]   = DTS_2F2R;
      this->dts_flags_map[DTS_3F2R]   = DTS_2F2R;

      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_4CHANNEL;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_4CHANNEL;

      /* else if (this->audio_caps & AO_CAP_MODE_STEREO)
         defaults are ok */
    } else if (!(this->audio_caps & AO_CAP_MODE_STEREO)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG, _("HELP! a mono-only audio driver?!\n"));

      this->dts_flags_map[DTS_MONO]   = DTS_MONO;
      this->dts_flags_map[DTS_STEREO] = DTS_MONO;
      this->dts_flags_map[DTS_3F]     = DTS_MONO;
      this->dts_flags_map[DTS_2F1R]   = DTS_MONO;
      this->dts_flags_map[DTS_3F1R]   = DTS_MONO;
      this->dts_flags_map[DTS_2F2R]   = DTS_MONO;
      this->dts_flags_map[DTS_3F2R]   = DTS_MONO;

      this->ao_flags_map[DTS_MONO]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_STEREO]  = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_3F]      = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_2F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_3F1R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_2F2R]    = AO_CAP_MODE_MONO;
      this->ao_flags_map[DTS_3F2R]    = AO_CAP_MODE_MONO;
    }
  }

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
