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
 * Logarithmic PCM Decoder
 * This audio decoder handles the mu-law and A-law logarithmic coding
 * standards.
 *
 * mu-law -> PCM conversion routine found at this site:
 *   http://www.speech.cs.cmu.edu/comp.speech/Section2/Q2.7.html
 * and credited to this person:
 *   Craig Reese: IDA/Supercomputing Research Center
 *
 * A-law -> PCM conversion routine lifted from SoX Sound eXchange:
 *   http://sox.sourceforge.net/
 * which listed the code as being lifted from Sun Microsystems.
 *
 * $Id: logpcm.c,v 1.3 2002/09/04 23:31:10 guenter Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "xine_internal.h"
#include "video_out.h"
#include "audio_out.h"
#include "xineutils.h"
#include "bswap.h"

#define AUDIOBUFSIZE 128*1024
#define LOGPCM_BITS_PER_SAMPLE 16

typedef struct logpcm_decoder_s {
  audio_decoder_t   audio_decoder;

  int64_t           pts;

  ao_instance_t    *audio_out;
  int               output_open;
  int               output_channels;

  unsigned char    *buf;
  int               bufsize;
  int               size;

  short             logtable[256];
} logpcm_decoder_t;

/*
** This routine converts from ulaw to 16 bit linear.
**
** Craig Reese: IDA/Supercomputing Research Center
** 29 September 1989
**
** References:
** 1) CCITT Recommendation G.711  (very difficult to follow)
** 2) MIL-STD-188-113,"Interoperability and Performance Standards
**     for Analog-to_Digital Conversion Techniques,"
**     17 February 1987
**
** Input: 8 bit ulaw sample
** Output: signed 16 bit linear sample
*/

static int mulaw2linear(unsigned char mulawbyte) {

  static int exp_lut[8] = {0,132,396,924,1980,4092,8316,16764};
  int sign, exponent, mantissa, sample;

  mulawbyte = ~mulawbyte;
  sign = (mulawbyte & 0x80);
  exponent = (mulawbyte >> 4) & 0x07;
  mantissa = mulawbyte & 0x0F;
  sample = exp_lut[exponent] + (mantissa << (exponent + 3));
  if (sign != 0) sample = -sample;

  return(sample);
}

/*
 * The following A-law -> PCM conversion function came from SoX which in
 * turn came from Sun Microsystems.
 */
#define SIGN_BIT        (0x80)          /* Sign bit for a A-law byte. */
#define QUANT_MASK      (0xf)           /* Quantization field mask. */
#define SEG_SHIFT       (4)             /* Left shift for segment number. */
#define SEG_MASK        (0x70)          /* Segment field mask. */

/*
 * alaw2linear() - Convert an A-law value to 16-bit signed linear PCM
 *
 */
static int alaw2linear(unsigned char a_val) {

  int t;
  int seg;

  a_val ^= 0x55;

  t = (a_val & QUANT_MASK) << 4;
  seg = ((unsigned)a_val & SEG_MASK) >> SEG_SHIFT;
  switch (seg) {
  case 0:
    t += 8;
    break;

  case 1:
    t += 0x108;
    break;

  default:
    t += 0x108;
    t <<= seg - 1;
  }
  return ((a_val & SIGN_BIT) ? t : -t);
}


static int logpcm_can_handle (audio_decoder_t *this_gen, int buf_type) {

  return (buf_type == BUF_AUDIO_MULAW ||
          buf_type == BUF_AUDIO_ALAW);

}

static void logpcm_reset (audio_decoder_t *this_gen) {
}

static void logpcm_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  logpcm_decoder_t *this = (logpcm_decoder_t *) this_gen;

  this->audio_out       = audio_out;
  this->output_open     = 0;
}

static void logpcm_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  logpcm_decoder_t *this = (logpcm_decoder_t *) this_gen;
  audio_buffer_t *audio_buffer;
  int in;
  int i;
  int bytes_to_send;

  if (buf->decoder_flags & BUF_FLAG_HEADER) {

    this->output_channels = buf->decoder_info[3];
    this->buf = xine_xmalloc(AUDIOBUFSIZE);
    this->bufsize = AUDIOBUFSIZE;
    this->size = 0;

    /* pre-calculate the possible log values */
    if (buf->type == BUF_AUDIO_MULAW)
      for (i = 0; i < 256; i++)
        this->logtable[i] = (short)mulaw2linear(i);
    else if (buf->type == BUF_AUDIO_ALAW)
      for (i = 0; i < 256; i++)
        this->logtable[i] = (short)alaw2linear(i);

    return;
  }

  if (!this->output_open) {
    this->output_open = this->audio_out->open(this->audio_out,
      LOGPCM_BITS_PER_SAMPLE, buf->decoder_info[1],
      (buf->decoder_info[3] == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO);
  }

  /* if the audio still isn't open, bail */
  if (!this->output_open)
    return;

  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
    printf("logpcm: increasing source buffer to %d to avoid overflow.\n",
      this->bufsize);
    this->buf = realloc( this->buf, this->bufsize );
  }

  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  { /* time to decode a frame */

    /* iterate through each 8-bit sample in the input buffer */
    in = 0;
    while (in < this->size) {

      audio_buffer = this->audio_out->get_buffer (this->audio_out);
      if (audio_buffer->mem_size == 0) {
        printf ("adpcm: Help! Allocated audio buffer with nothing in it!\n");
        return;
      }

      /* this->size and in are sample counts, mem_size is a byte count */
      if ((this->size - in) > (audio_buffer->mem_size / 2))
        bytes_to_send = audio_buffer->mem_size / 2;
      else
        bytes_to_send = this->size - in;

      /* fill up this buffer */
      for (i = 0; i < bytes_to_send; i++)
        audio_buffer->mem[i] = this->logtable[this->buf[in++]];

      audio_buffer->num_frames = bytes_to_send / this->output_channels;
      audio_buffer->vpts = buf->pts;
      buf->pts = 0;  /* only first buffer gets the real pts */
      this->audio_out->put_buffer (this->audio_out, audio_buffer);
    }

    /* reset internal accumulation buffer */
    this->size = 0;
  }
}

static void logpcm_close (audio_decoder_t *this_gen) {
  logpcm_decoder_t *this = (logpcm_decoder_t *) this_gen;

  if (this->output_open)
    this->audio_out->close (this->audio_out);
  this->output_open = 0;
}

static char *logpcm_get_id(void) {
  return "Logarithmic PCM";
}

static void logpcm_dispose (audio_decoder_t *this_gen) {
  free (this_gen);
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, xine_t *xine) {

  logpcm_decoder_t *this;
  if (iface_version != 9) {
    printf(_("logpcm: plugin doesn't support plugin API version %d.\n"
             "logpcm: this means there's a version mismatch between xine and this\n"
             "logpcm: decoder plugin.\nInstalling current plugins should help.\n"),
           iface_version);

    return NULL;
  }

  this = (logpcm_decoder_t *) malloc (sizeof (logpcm_decoder_t));

  this->audio_decoder.init                = logpcm_init;
  this->audio_decoder.decode_data         = logpcm_decode_data;
  this->audio_decoder.reset               = logpcm_reset;
  this->audio_decoder.close               = logpcm_close;
  this->audio_decoder.get_identifier      = logpcm_get_id;
  this->audio_decoder.dispose             = logpcm_dispose;
  this->audio_decoder.priority            = 5;

  return (audio_decoder_t *) this;
}

