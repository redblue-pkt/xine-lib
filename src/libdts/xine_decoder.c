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
 * $Id: xine_decoder.c,v 1.30 2002/10/31 07:27:55 jcdutton Exp $
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
#include <netinet/in.h> /* ntohs */

#include "xine_internal.h"
#include "audio_out.h"
#include "buffer.h"


#define LOG_DEBUG

typedef struct {
  audio_decoder_class_t   decoder_class;
} dts_class_t;

typedef struct dts_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;
  audio_decoder_class_t *class;

  uint32_t         rate;
  uint32_t         bits_per_sample; 
  uint32_t         number_of_channels; 
   
  int              output_open;
} dts_decoder_t;


void dts_reset (audio_decoder_t *this_gen) {

  /* dts_decoder_t *this = (dts_decoder_t *) this_gen; */

}


void dts_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  dts_decoder_t  *this = (dts_decoder_t *) this_gen;
  uint8_t        *data_in = (uint8_t *)buf->content;
  uint8_t        *data_out;
  audio_buffer_t *audio_buffer;
  uint32_t  ac5_type;
  uint32_t  ac5_spdif_type;
  uint32_t  ac5_length=0;
  uint32_t  ac5_pcm_length;
  uint32_t  number_of_frames;
  uint32_t  first_access_unit;
  int n, i ;
  printf("DTS decode_data called.\n");

  if ((this->stream->audio_out->get_capabilities(this->stream->audio_out) & AO_CAP_MODE_AC5) == 0) {
    return;
  }
  if (!this->output_open) {      
    this->output_open = (this->stream->audio_out->open (this->stream->audio_out, this->bits_per_sample, 
                                                this->rate,
                                                AO_CAP_MODE_AC5));
  }
  if (!this->output_open) 
    return;

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
      printf("DTS length error\n");
      return;
    }
      
    if ((data_in[0] != 0x7f) || 
        (data_in[1] != 0xfe) ||
        (data_in[2] != 0x80) ||
        (data_in[3] != 0x01)) {
      printf("DTS Sync bad\n");
      return;
    }
    
    audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
    audio_buffer->frame_header_count = buf->decoder_info[1]; /* Number of frames */
    audio_buffer->first_access_unit = buf->decoder_info[2]; /* First access unit */

#ifdef LOG_DEBUG
    printf("DTS frame_header_count = %u\n",audio_buffer->frame_header_count);
    printf("DTS first access unit = %u\n",audio_buffer->first_access_unit);
#endif

    if (n == first_access_unit) {
      audio_buffer->vpts       = buf->pts;
    } else {
      audio_buffer->vpts       = 0;
    }
 
    data_out=(uint8_t *) audio_buffer->mem;

    ac5_type=((data_in[4] & 0x01) << 6) | ((data_in[5] >>2) & 0x3f);
    ac5_length=((data_in[5] & 0x03) << 12) | ((data_in[6] & 0xff) << 4) | ((data_in[7] & 0xf0) >> 4);
    ac5_length++;
    switch(ac5_type) {
    case 0x0f:
      ac5_spdif_type = 0x000b;  /* DTS          */
      break;
    case 0x1f:
      ac5_spdif_type = 0x000c;  /* DTS          */
      break;
    case 0x3f:
      ac5_spdif_type = 0x000d;  /* DTS          */
      break;
    default:
      ac5_spdif_type = 0x0000;  /* DTS          */
      break;
    }

#ifdef LOG_DEBUG
    printf("DTS AC5 type=%d\n",ac5_type);
    printf("DTS AC5_spdif_type=%d\n",ac5_spdif_type);
    printf("DTS AC5 length=%d\n",ac5_length);
    for(i=2000;i<2048;i++) {
      printf("%02x ",data_in[i]);
    }
    printf("\n");
#endif
  
    if (ac5_length > 8191) {
      printf("ac5_length too long\n");
      ac5_pcm_length = 0;
    } else {
  
      if (ac5_length <= 248) {
        ac5_pcm_length = 64;
      } else if (ac5_length <= 504) {
        ac5_pcm_length = 128;
      } else if (ac5_length <= 1016) {
        ac5_pcm_length = 256;
      } else if (ac5_length <= 2040) {
        ac5_pcm_length = 512;
      } else if (ac5_length <= 4088) {
        ac5_pcm_length = 1024;
      } else {
        printf("BAD AC5 length\n");
        ac5_pcm_length = 512; 
      }
      if (ac5_pcm_length < (512 )) {
         ac5_pcm_length = 512 ;
      }
    }
    
#ifdef LOG_DEBUG
    printf("DTS length=%d loop=%d pts=%lld\n",ac5_pcm_length,n,audio_buffer->vpts);
#endif

    audio_buffer->num_frames = ac5_pcm_length;

    data_out[0] = 0x72; data_out[1] = 0xf8;	/* spdif syncword    */
    data_out[2] = 0x1f; data_out[3] = 0x4e;	/* ..............    */
    data_out[4] = ac5_spdif_type;			/* DTS data          */
    data_out[5] = 0;		                /* Unknown */
    data_out[6] = (ac5_length << 3) & 0xff;   /* ac5_length * 8   */
    data_out[7] = ((ac5_length ) >> 5) & 0xff;

    if( ac5_pcm_length )
      swab(data_in, &data_out[8], ac5_length );
      
    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer);
  }
}

static void dts_dispose (audio_decoder_t *this_gen) {
  dts_decoder_t *this = (dts_decoder_t *) this_gen; 
  if (this->output_open) 
    this->stream->audio_out->close (this->stream->audio_out);
  this->output_open = 0;
  free (this);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t
*stream) {
  dts_decoder_t *this ;
  printf("DTS open_plugin called.\n");

  this = (dts_decoder_t *) malloc (sizeof (dts_decoder_t));

  this->audio_decoder.decode_data         = dts_decode_data;
  this->audio_decoder.reset               = dts_reset;
  this->audio_decoder.dispose             = dts_dispose;

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
  printf("DTS class dispose called.\n");
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  dts_class_t *this ;
  printf("DTS class init_plugin called.\n");
  this = (dts_class_t *) malloc (sizeof (dts_class_t));

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
  { PLUGIN_AUDIO_DECODER, 10, "dts", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
