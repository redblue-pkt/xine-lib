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
 * $Id: roqaudio.c,v 1.13 2002/11/20 11:57:46 mroi Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "xine_internal.h"
#include "audio_out.h"
#include "buffer.h"
#include "xineutils.h"
#include "bswap.h"

#define RoQ_AUDIO_SAMPLE_RATE 22050
#define RoQ_AUDIO_BITS_PER_SAMPLE 16

#define AUDIOBUFSIZE 128*1024

#define CLAMP_S16(x)  if (x < -32768) x = -32768; \
  else if (x > 32767) x = 32767;
#define SE_16BIT(x)  if (x & 0x8000) x -= 0x10000;

typedef struct {
  audio_decoder_class_t   decoder_class;
} roqaudio_class_t;

typedef struct roqaudio_decoder_s {
  audio_decoder_t   audio_decoder;

  xine_stream_t    *stream;

  int64_t           pts;

  int               output_open;
  int               output_channels;

  unsigned char    *buf;
  int               bufsize;
  int               size;

  short             square_array[256];
} roqaudio_decoder_t;

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

    /* stream/meta info */
    this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = 
      strdup("RoQ DPCM Audio");
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED] = 1;

    return;
  }

  if (!this->output_open) {
    this->output_open = this->stream->audio_out->open(this->stream->audio_out,
      this->stream, RoQ_AUDIO_BITS_PER_SAMPLE, RoQ_AUDIO_SAMPLE_RATE, 
      (this->output_channels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO);
  }

  /* if the audio still isn't open, bail */
  if (!this->output_open)
    return;

  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
    printf("RoQ: increasing source buffer to %d to avoid overflow.\n",
      this->bufsize);
    this->buf = realloc( this->buf, this->bufsize );
  }

  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  { /* time to decode a frame */
    audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);

    out = 0;

    /* prepare the initial predictors */
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

      /* toggle channel */
      channel_number ^= this->output_channels - 1;
    }

    audio_buffer->vpts = buf->pts;
    audio_buffer->num_frames = 
      (buf->size - 8) / this->output_channels;

    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

    this->size = 0;
  }
}

static void roqaudio_reset (audio_decoder_t *this_gen) {
}

static void roqaudio_discontinuity (audio_decoder_t *this_gen) {

  roqaudio_decoder_t *this = (roqaudio_decoder_t *) this_gen;

  this->pts = 0;
}

static void roqaudio_dispose (audio_decoder_t *this_gen) {

  roqaudio_decoder_t *this = (roqaudio_decoder_t *) this_gen;

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  if (this->buf)
    free(this->buf);

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  roqaudio_decoder_t  *this;
  int i;
  short square;

  this = (roqaudio_decoder_t *) xine_xmalloc (sizeof (roqaudio_decoder_t));

  this->audio_decoder.decode_data         = roqaudio_decode_data;
  this->audio_decoder.reset               = roqaudio_reset;
  this->audio_decoder.discontinuity       = roqaudio_discontinuity;
  this->audio_decoder.dispose             = roqaudio_dispose;
  this->size                              = 0;

  this->stream                            = stream;

  this->buf              = NULL;
  this->output_open      = 0;
  this->output_channels  = 0;

  /* initialize tables of squares */
  for (i = 0; i < 128; i++) {
    square = i * i;
    this->square_array[i] = square;
    this->square_array[i + 128] = -square;
  }

  return &this->audio_decoder;
}

static char *get_identifier (audio_decoder_class_t *this) {
  return "RoQ Audio";
}

static char *get_description (audio_decoder_class_t *this) {
  return "Id Roq audio decoder plugin";
}

static void dispose_class (audio_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  roqaudio_class_t *this;

  this = (roqaudio_class_t *) xine_xmalloc (sizeof (roqaudio_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

static uint32_t audio_types[] = { 
  BUF_AUDIO_ROQ, 0
};

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 12, "roqaudio", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
