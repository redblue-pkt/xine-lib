/*
 * Copyright (C) 2000-2001 the xine project
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
 * Interplay DPCM Audio Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Interplay MVE file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: interplayaudio.c,v 1.1 2002/12/28 18:25:08 tmmm Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "xine_internal.h"
#include "audio_out.h"
#include "buffer.h"
#include "xineutils.h"
#include "bswap.h"

#define AUDIOBUFSIZE 128*1024

typedef struct {
  audio_decoder_class_t   decoder_class;
} interplay_class_t;

typedef struct interplay_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;

  int              sample_rate;       /* audio sample rate */
  int              bits_per_sample;   /* bits/sample, usually 8 or 16 */
  int              channels;          /* 1 or 2, usually */

  int              output_open;       /* flag to indicate audio is ready */

  unsigned char   *buf;               /* data accumulation buffer */
  int              bufsize;           /* maximum size of buf */
  int              size;              /* size of accumulated data in buf */

  int              last_left_delta;
  int              last_right_delta;

} interplay_decoder_t;

/**************************************************************************
 * Interplay DPCM specific decode functions
 *************************************************************************/

int interplay_delta_table[] = {
         0,      1,      2,      3,      4,      5,      6,      7,
         8,      9,     10,     11,     12,     13,     14,     15,
        16,     17,     18,     19,     20,     21,     22,     23,     
        24,     25,     26,     27,     28,     29,     30,     31,
        32,     33,     34,     35,     36,     37,     38,     39,
        40,     41,     42,     43,     47,     51,     56,     61,
        66,     72,     79,     86,     94,    102,    112,    122,
       133,    145,    158,    173,    189,    206,    225,    245,
       267,    292,    318,    348,    379,    414,    452,    493,    
       538,    587,    640,    699,    763,    832,    908,    991,
      1081,   1180,   1288,   1405,   1534,   1673,   1826,   1993,   
      2175,   2373,   2590,   2826,   3084,   3365,   3672,   4008,
      4373,   4772,   5208,   5683,   6202,   6767,   7385,   8059,   
      8794,   9597,  10472,  11428,  12471,  13609,  14851,  16206,
     17685,  19298,  21060,  22981,  25078,  27367,  29864,  32589, 
    -29973, -26728, -23186, -19322, -15105, -10503,  -5481,     -1,
         1,      1,   5481,  10503,  15105,  19322,  23186,  26728,  
     29973, -32589, -29864, -27367, -25078, -22981, -21060, -19298,
    -17685, -16206, -14851, -13609, -12471, -11428, -10472,  -9597,  
     -8794,  -8059,  -7385,  -6767,  -6202,  -5683,  -5208,  -4772,
     -4373,  -4008,  -3672,  -3365,  -3084,  -2826,  -2590,  -2373,  
     -2175,  -1993,  -1826,  -1673,  -1534,  -1405,  -1288,  -1180,
     -1081,   -991,   -908,   -832,   -763,   -699,   -640,   -587,   
      -538,   -493,   -452,   -414,   -379,   -348,   -318,   -292,
      -267,   -245,   -225,   -206,   -189,   -173,   -158,   -145,   
      -133,   -122,   -112,   -102,    -94,    -86,    -79,    -72,
       -66,    -61,    -56,    -51,    -47,    -43,    -42,    -41,    
       -40,    -39,    -38,    -37,    -36,    -35,    -34,    -33,
       -32,    -31,    -30,    -29,    -28,    -27,    -26,    -25,    
       -24,    -23,    -22,    -21,    -20,    -19,    -18,    -17,
       -16,    -15,    -14,    -13,    -12,    -11,    -10,     -9,     
        -8,     -7,     -6,     -5,     -4,     -3,     -2,     -1

};

/**************************************************************************
 * xine audio plugin functions
 *************************************************************************/

/* clamp a number within a signed 16-bit range */
#define CLAMP_S16(x)  if (x < -32768) x = -32768; \
  else if (x > 32767) x = 32767;

static void interplay_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  interplay_decoder_t *this = (interplay_decoder_t *) this_gen;
  audio_buffer_t *audio_buffer;
  int i;
  int samples_to_send;
  int delta[2];
  int stream_ptr = 0;
  int sequence_number;
  int channel_number = 0;

  if (buf->decoder_flags & BUF_FLAG_HEADER) {

    /* When the engine sends a BUF_FLAG_HEADER flag, it is time to initialize
     * the decoder. The buffer element type has 4 decoder_info fields,
     * 0..3. Field 1 is the sample rate. Field 2 is the bits/sample. Field
     * 3 is the number of channels. */
    this->sample_rate = buf->decoder_info[1];
    this->bits_per_sample = buf->decoder_info[2];
    this->channels = buf->decoder_info[3];

    /* initialize the data accumulation buffer */
    this->buf = xine_xmalloc(AUDIOBUFSIZE);
    this->bufsize = AUDIOBUFSIZE;
    this->size = 0;

    /* take this opportunity to initialize stream/meta information */
    this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup("Interplay MVE DPCM");
    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED] = 1;

    return;
  }

  /* if the audio output is not open yet, open the audio output */
  if (!this->output_open) {
    this->output_open = this->stream->audio_out->open(
      this->stream->audio_out,
      this->stream,
      16,
      this->sample_rate,
      (this->channels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO);
  }

  /* if the audio still isn't open, do not go any further with the decode */
  if (!this->output_open)
    return;

  /* accumulate the data passed through the buffer element type; increase
   * the accumulator buffer size as necessary */
  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
    printf("interplayaudio: increasing source buffer to %d to avoid overflow.\n",
      this->bufsize);
    this->buf = realloc( this->buf, this->bufsize );
  }
  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  /* When a buffer element type has the BUF_FLAG_FRAME_END flag set, it is
   * time to decode the data in the buffer. */
  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  {

    /* if this is the first frame, fetch the first deltas from the stream */
    sequence_number = LE_16(&this->buf[stream_ptr]);
    stream_ptr += 6;  /* skip over the stream mask and stream length */
    if (sequence_number == 1) {
      delta[0] = LE_16(&this->buf[stream_ptr]);
      stream_ptr += 2;
      if (this->channels == 2) {
        delta[1] = LE_16(&this->buf[stream_ptr]);
        stream_ptr += 2;
      }
    } else {
      delta[0] = this->last_left_delta;
      if (this->channels == 2)
        delta[1] = this->last_right_delta;
    }

    /* iterate through each 8-bit delta in the input buffer */
    while (stream_ptr < this->size) {

      audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
      if (audio_buffer->mem_size == 0) {
        printf ("interplayaudio: Help! Allocated audio buffer with nothing in it!\n");
        return;
      }

      /* this->size and stream_ptr are sample counts, mem_size is a byte count */
      if ((this->size - stream_ptr) > (audio_buffer->mem_size / 2))
        samples_to_send = audio_buffer->mem_size / 2;
      else
        samples_to_send = this->size - stream_ptr;

      /* fill up this buffer */
      for (i = 0; i < samples_to_send; i++) {
        delta[channel_number] += interplay_delta_table[this->buf[stream_ptr++]];
        CLAMP_S16(delta[channel_number]);
        audio_buffer->mem[i] = delta[channel_number];

        /* toggle channel */
        channel_number ^= this->channels - 1;
      }

      audio_buffer->num_frames = samples_to_send / this->channels;
      audio_buffer->vpts = buf->pts;
      buf->pts = 0;  /* only first buffer gets the real pts */
      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
    }

    /* stash away the deltas for the next block */
    this->last_left_delta = delta[0];
    this->last_right_delta = delta[1];

    /* reset data accumulation buffer */
    this->size = 0;
  }
}

/* This function resets the state of the audio decoder. This usually
 * entails resetting the data accumulation buffer. */
static void interplay_reset (audio_decoder_t *this_gen) {

  interplay_decoder_t *this = (interplay_decoder_t *) this_gen;

  this->size = 0;
}

/* This function resets the last pts value of the audio decoder. */
static void interplay_discontinuity (audio_decoder_t *this_gen) {
}

/* This function closes the audio output and frees the private audio decoder
 * structure. */
static void interplay_dispose (audio_decoder_t *this_gen) {

  interplay_decoder_t *this = (interplay_decoder_t *) this_gen;

  /* close the audio output */
  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  /* free anything that was allocated during operation */
  free(this->buf);
  free(this);
}

/* This function allocates, initializes, and returns a private audio
 * decoder structure. */
static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  interplay_decoder_t *this ;

  this = (interplay_decoder_t *) malloc (sizeof (interplay_decoder_t));

  /* connect the member functions */
  this->audio_decoder.decode_data         = interplay_decode_data;
  this->audio_decoder.reset               = interplay_reset;
  this->audio_decoder.discontinuity       = interplay_discontinuity;
  this->audio_decoder.dispose             = interplay_dispose;

  /* connect the stream */
  this->stream = stream;

  /* audio output is not open at the start */
  this->output_open = 0;

  /* initialize the basic audio parameters */
  this->channels = 0;
  this->sample_rate = 0;
  this->bits_per_sample = 0;

  /* initialize the data accumulation buffer */
  this->buf = NULL;
  this->bufsize = 0;
  this->size = 0;

  /* return the newly-initialized audio decoder */
  return &this->audio_decoder;
}

/* This function returns a brief string that describes (usually with the
 * decoder's most basic name) the audio decoder plugin. */
static char *get_identifier (audio_decoder_class_t *this) {
  return "Interplay DPCM Audio";
}

/* This function returns a slightly longer string describing the audio
 * decoder plugin. */
static char *get_description (audio_decoder_class_t *this) {
  return "Interplay DPCM audio decoder plugin";
}

/* This function frees the audio decoder class and any other memory that was
 * allocated. */
static void dispose_class (audio_decoder_class_t *this_gen) {

  interplay_class_t *this = (interplay_class_t *)this_gen;

  free (this);
}

/* This function allocates a private audio decoder class and initializes 
 * the class's member functions. */
static void *init_plugin (xine_t *xine, void *data) {

  interplay_class_t *this ;

  this = (interplay_class_t *) malloc (sizeof (interplay_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/* This is a list of all of the internal xine audio buffer types that 
 * this decoder is able to handle. Check src/xine-engine/buffer.h for a
 * list of valid buffer types (and add a new one if the one you need does
 * not exist). Terminate the list with a 0. */
static uint32_t audio_types[] = { 
  BUF_AUDIO_INTERPLAY,
  0
};

/* This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1) 
 * will be used instead of a plugin with priority (n). */
static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

/* The plugin catalog entry. This is the only information that this plugin
 * will export to the public. */
plugin_info_t xine_plugin_info[] = {
  /* { type, API version, "name", version, special_info, init_function }, */
  { PLUGIN_AUDIO_DECODER, 13, "interplayaudio", XINE_VERSION_CODE, &dec_info_audio, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

