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
 * RoQ DPCM Audio Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the RoQ file format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 *
 * $Id: roqaudio.c,v 1.2 2002/06/03 00:57:34 tmmm Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "audio_out.h"
#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "bswap.h"

#define RoQ_AUDIO_SAMPLE_RATE 22050
#define RoQ_AUDIO_BITS_PER_SAMPLE 16

#define AUDIOBUFSIZE 128*1024

#define LE_16(x) (le2me_16(*(unsigned short *)(x)))
#define LE_32(x) (le2me_32(*(unsigned int *)(x)))

#define CLAMP_S16(x)  if (x < -32768) x = -32768; \
  else if (x > 32767) x = 32767;
#define SE_16BIT(x)  if (x & 0x8000) x -= 0x10000;

typedef struct roqaudio_decoder_s {
  audio_decoder_t   audio_decoder;

  int64_t           pts;

  ao_instance_t    *audio_out;
  int               output_open;
  int               output_channels;

  unsigned char    *buf;
  int               bufsize;
  int               size;

  short             square_array[256];
} roqaudio_decoder_t;

static int roqaudio_can_handle (audio_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_AUDIO_ROQ);
}

static void roqaudio_reset (audio_decoder_t *this_gen) {
}

static void roqaudio_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  roqaudio_decoder_t *this = (roqaudio_decoder_t *) this_gen;
  int i;
  short square;

  this->audio_out       = audio_out;
  this->output_open     = 0;

  /* initialize tables of squares */
  for (i = 0; i < 128; i++) {
    square = i * i;
    this->square_array[i] = square;
    this->square_array[i + 128] = -square;
  }
}

static void roqaudio_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {
  roqaudio_decoder_t *this = (roqaudio_decoder_t *) this_gen;
  audio_buffer_t *audio_buffer;
  int in, out;
  int predictor[2];
  int channel_number = 0;

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    this->output_channels = buf->decoder_info[3];

    this->buf = xine_xmalloc(AUDIOBUFSIZE);
    this->bufsize = AUDIOBUFSIZE;
    this->size = 0;

    return;
  }

  if (!this->output_open) {
    this->output_open = this->audio_out->open(this->audio_out,
      RoQ_AUDIO_BITS_PER_SAMPLE, RoQ_AUDIO_SAMPLE_RATE, 
      (this->output_channels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO);
  }

  /* if the audio still isn't open, bail */
  if (!this->output_open)
    return;

//printf ("received audio packet: %d bytes, pts = %lld, flags = %X\n",
//  buf->size, buf->pts, buf->decoder_flags);

  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
    printf("RoQ: increasing source buffer to %d to avoid overflow.\n",
      this->bufsize);
    this->buf = realloc( this->buf, this->bufsize );
  }

  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  { /* time to decode a frame */
    audio_buffer = this->audio_out->get_buffer (this->audio_out);

    out = 0;

    // prepare the initial predictors
    if (this->output_channels == 1)
      predictor[0] = LE_16(&this->buf[6]);
    else
    {
      predictor[0] = this->buf[7] << 8;
      predictor[1] = this->buf[6] << 8;
    }
    SE_16BIT(predictor[0]);
    SE_16BIT(predictor[1]);

    /* decode the samples */
    for (in = 8; in < this->size; in++)
    {
      predictor[channel_number] += this->square_array[this->buf[in]];
      CLAMP_S16(predictor[channel_number]);
      audio_buffer->mem[out++] = predictor[channel_number];

      // toggle channel
      channel_number ^= this->output_channels - 1;
    }

    audio_buffer->vpts = buf->pts;
    audio_buffer->num_frames = 
      (buf->size - 8) / this->output_channels;

//printf ("  audio buffer size = %d, buf size = %d, chan = %d, # of frames sent = %d\n",
//  audio_buffer->mem_size, this->size, 
//  this->output_channels, audio_buffer->num_frames);
    this->audio_out->put_buffer (this->audio_out, audio_buffer);

    this->size = 0;
  }
}

static void roqaudio_close (audio_decoder_t *this_gen) {
  roqaudio_decoder_t *this = (roqaudio_decoder_t *) this_gen;

  if (this->output_open)
    this->audio_out->close (this->audio_out);
  this->output_open = 0;
}

static char *roqaudio_get_id(void) {
  return "RoQ Audio";
}

static void roqaudio_dispose (audio_decoder_t *this_gen) {
  free (this_gen);
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, xine_t *xine) {

  roqaudio_decoder_t *this ;

  if (iface_version != 8) {
    printf( "RoQ Audio: plugin doesn't support plugin API version %d.\n"
            "RoQ Audio: this means there's a version mismatch between xine and this\n"
            "RoQ Audio: decoder plugin.\nInstalling current plugins should help.\n",
            iface_version);

    return NULL;
  }

  this = (roqaudio_decoder_t *) malloc (sizeof (roqaudio_decoder_t));

  this->audio_decoder.interface_version   = iface_version;
  this->audio_decoder.can_handle          = roqaudio_can_handle;
  this->audio_decoder.init                = roqaudio_init;
  this->audio_decoder.decode_data         = roqaudio_decode_data;
  this->audio_decoder.reset               = roqaudio_reset;
  this->audio_decoder.close               = roqaudio_close;
  this->audio_decoder.get_identifier      = roqaudio_get_id;
  this->audio_decoder.dispose             = roqaudio_dispose;
  this->audio_decoder.priority            = 5;

  return (audio_decoder_t *) this;
}

