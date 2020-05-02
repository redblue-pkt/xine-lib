/*
 * Copyright (C) 2000-2020 the xine project
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * stuff needed to turn libmad into a xine decoder plugin
 */

#include <stdlib.h>
#include <string.h>
#include "config.h"

#define LOG_MODULE "mad_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/audio_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "bswap.h"

#ifdef HAVE_MAD_H
# include <mad.h>
#else
# include "frame.h"
# include "synth.h"
#endif

#define REST_SIZE 16384

/* According to Rob Leslie (libmad author) :
 * The absolute theoretical maximum frame size is 2881 bytes: MPEG 2.5 Layer II,
 * 8000 Hz @ 160 kbps, with a padding slot. (Such a frame is unlikely, but it was
 * a useful exercise to compute all possible frame sizes.) Add to this an 8 byte
 * MAD_BUFFER_GUARD, and the minimum buffer size you should be streaming to
 * libmad in the general case is 2889 bytes.

 * Theoretical frame sizes for Layer III range from 24 to 1441 bytes, but there
 * is a "soft" limit imposed by the standard of 960 bytes. Nonetheless MAD can
 * decode frames of any size as long as they fit entirely in the buffer you pass,
 * not including the MAD_BUFFER_GUARD bytes.

 * TJ. Well.
 * 1. (MAD_ERROR_BUFLEN) libmad always wants >= MAD_BUFFER_GUARD explicit extra bytes,
 * containing a next frame head. A fake one does the trick with layers 1 and 2.
 * 2. (MAD_ERROR_BADDATAPTR) Layer 3 uses forward references, and therefore
 * needs at least 2 frames.
 */
#define MAD_MIN_SIZE 2889

typedef struct mad_decoder_s {
  audio_decoder_t   audio_decoder;

  xine_stream_t    *xstream;

#define MAD_PTS_LD 3
#define MAD_PTS_SIZE (1 << MAD_PTS_LD)
#define MAD_PTS_MASK (MAD_PTS_SIZE - 1)
  unsigned int      pts_samples;
  unsigned int      pts_delay;
  uint32_t          pts_read;
  uint32_t          pts_write;
  struct {
    int64_t         pts;
    int             bytes;
  } pts_ring[MAD_PTS_SIZE];

  struct mad_synth  synth;
  struct mad_stream stream;
  struct mad_frame  frame;

  unsigned int      output_sampling_rate;
  int               output_open;
  int               output_mode;

  int               preview_mode;
  unsigned int      start_padding;
  unsigned int      end_padding;
  int               needs_more_data;

  uint8_t          *inbuf;
  int               insize;

  mad_fixed_t       peak;
  uint32_t          seek;
  uint32_t          declip;
  unsigned int      num_inbufs;
  unsigned int      num_bytes_d;
  unsigned int      num_bytes_r;
  unsigned int      num_outbufs;

  /* sliding window reassemble buf */
  uint32_t          rest_read, rest_write;
  uint8_t           rest_buf[REST_SIZE + MAD_BUFFER_GUARD];
} mad_decoder_t;

static int _mad_frame_size (const uint8_t *h) {
  /* bitrate table[type, 6][bitrate index, 16] */
  static const uint8_t mp3_byterates[] = {
    0,  4,  8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 255,
    0,  4,  6,  7,  8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 255,
    0,  4,  5,  6,  7,  8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 255,
    0,  4,  6,  7,  8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 255,
    0,  1,  2,  3,  4,  5,  6,  7,  8, 10, 12, 14, 16, 18, 20, 255,
    0,  1,  2,  3,  4,  5,  6,  7,  8, 10, 12, 14, 16, 18, 20, 255
  };
  /* frequency table[mpeg version bits, 4][frequence index, 4] (in Hz) */
  static const uint16_t mp3_freqs[] = {
    11025, 12000,  8000, 0, /* v2.5 */
        0,     0,     0, 0, /* reserved */
    22050, 24000, 16000, 0, /* v2 */
    44100, 48000, 32000, 0  /* v1 */
  };
  /* samples per frame table[type, 6] */
  static const uint16_t mp3_samples[6] = {
    384, 1152, 1152, 384, 1152, 576
  };
  /* padding bytes table[type, 6] */
  static const uint8_t mp3_padding[6] = {
    4, 1, 1, 4, 1, 1
  };
  static const uint8_t mp3_type[16] = {
    /* Layer        4(aac_latm)   3    2    1 */
    /* MPEG Version 2.5 */ 255,   5,   4,   3,
    /* reserved */         255, 255, 255, 255,
    /* v2 */               255,   5,   4,   3,
    /* v1 */               255,   2,   1,   0
  };
  uint32_t byterate, freq, padding, samples;
  uint32_t type;
  /* read header */
  uint32_t head = _X_BE_32 (h);
  /* check if really mp3 */
  if ((head >> 21) != 0x7ff)
    return 0;
  type = mp3_type[(head >> 17) & 15];
  if (type & 128)
    return 0;

  samples  = mp3_samples[type];
  byterate = mp3_byterates[16 * type + ((head >> 12) & 15)];
  if (byterate & 128)
    return 0;
  freq     = mp3_freqs[((head >> 17) & 12) + ((head >> 10) & 3)];
  if (!freq)
    return 0;
  padding  = head & (1 << 9) ? mp3_padding[type] : 0;

  if (byterate)
    return samples * byterate * 1000 / freq + padding;
  return 0;
}

static void mad_reset (audio_decoder_t *this_gen) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen;

  mad_synth_finish (&this->synth);
  mad_frame_finish (&this->frame);
  mad_stream_finish(&this->stream);

  this->pts_read = 0;
  this->pts_write = 0;
  this->rest_read = 0;
  this->rest_write = 0;
  this->preview_mode = 0;
  this->start_padding = 0;
  this->end_padding = 0;
  this->needs_more_data = 0;
  this->seek = 2;

  mad_synth_init  (&this->synth);
  mad_stream_init (&this->stream);
  this->stream.options = MAD_OPTION_IGNORECRC;
  mad_frame_init  (&this->frame);
}


static void mad_discontinuity (audio_decoder_t *this_gen) {
  mad_decoder_t *this = (mad_decoder_t *) this_gen;
  int b = this->rest_write - this->rest_read;
  if (b > 0) {
    this->pts_ring[0].pts = 0;
    this->pts_ring[0].bytes = b;
    this->pts_write = 1;
  } else {
    this->pts_write = 0;
  }
  this->pts_read = 0;
  this->seek = 2;
}

/* utility to scale and round samples to 16 bits */

static inline int32_t _mad_scale (mad_fixed_t sample) {
  int32_t v = (sample + (1L << (MAD_F_FRACBITS - 16))) >> (MAD_F_FRACBITS + 1 - 16);
  return ((v + 0x8000) & 0xffff0000) ? (v >> 31) ^ 0x7fff : v;
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

static int _mad_consume (mad_decoder_t *this) {
  int d, l;
  if (!this->stream.next_frame)
    return 0;
  d = (const uint8_t *)this->stream.next_frame - (const uint8_t *)this->inbuf;
  if (d <= 0)
    return 0;
  this->inbuf += d;
  this->insize -= d;
  if (this->rest_write)
    this->rest_read += d;
  l = d;
  while (this->pts_read != this->pts_write) {
    l -= this->pts_ring[this->pts_read].bytes;
    if (l >= 0) {
      this->pts_read = (this->pts_read + 1) & MAD_PTS_MASK;
      if (l == 0)
        break;
    } else {
      this->pts_ring[this->pts_read].bytes += l;
      break;
    }
  }
  return d;
}

static void mad_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen;

  lprintf ("decode data, size: %d, decoder_flags: %d\n", buf->size, buf->decoder_flags);

  if (!(buf->decoder_flags & BUF_FLAG_HEADER)) {
    int donebytes = 0;

    this->insize = 0;
    this->inbuf = NULL;

    /* reset decoder on leaving preview mode */
    if ((buf->decoder_flags & BUF_FLAG_PREVIEW) == 0) {
      if (this->preview_mode) {
	mad_reset (this_gen);
      }
    } else {
      this->preview_mode = 1;
    }

    this->num_inbufs += 1;
    this->pts_ring[this->pts_write].pts = buf->pts;
    this->pts_ring[this->pts_write].bytes = buf->size;
    this->pts_write = (this->pts_write + 1) & MAD_PTS_MASK;
    xprintf (this->xstream->xine, XINE_VERBOSITY_DEBUG + 1,
      "mad_audio_decoder: inbuf bytes %d, pts %" PRId64 ".\n", buf->size, buf->pts);

    while (1) {
      int err;

      if (this->insize <= 0) {
        int left = buf->size - donebytes;
        if (left <= 0)
          break;
        if (!this->rest_write) {
          /* try using buf mem itself. */
          this->inbuf = buf->content + donebytes;
          this->insize = left;
          this->num_bytes_d += left;
          donebytes = buf->size;
        } else {
          /* fall back to reassembling. */
          int n = REST_SIZE - this->rest_write;
          if ((n < left) && this->rest_read) {
            n = this->rest_write - this->rest_read;
            if (n > 0) {
              if (this->rest_read >= (uint32_t)n)
                memcpy (this->rest_buf, this->rest_buf + this->rest_read, n);
              else
                memmove (this->rest_buf, this->rest_buf + this->rest_read, n);
            }
            this->rest_write = n;
            this->rest_read = 0;
            n = REST_SIZE - n;
          }
          if (n > left)
            n = left;
          xine_fast_memcpy (this->rest_buf + this->rest_write, buf->content + donebytes, n);
          donebytes += n;
          this->rest_write += n;
          this->num_bytes_r += n;
          this->insize = this->rest_write - this->rest_read;
          this->inbuf = this->rest_buf + this->rest_read;
        }
        mad_stream_buffer (&this->stream, this->inbuf, this->insize);
      }

      if (!this->needs_more_data) {
        if (buf->decoder_flags & BUF_FLAG_AUDIO_PADDING) {
          this->start_padding = buf->decoder_info[1];
          this->end_padding = buf->decoder_info[2];
        } else {
          this->start_padding = 0;
          this->end_padding = 0;
        }
      }

      do {
        err = mad_frame_decode (&this->frame, &this->stream);
        if (!err)
          break;

        {
          int d = _mad_consume (this);
          if (d > 0) {
            xprintf (this->xstream->xine, XINE_VERBOSITY_DEBUG + 1,
              "mad_audio_decoder: error 0x%x, consumed %d.\n", (unsigned int)this->stream.error, d);
          }
        }

        /* HACK: demuxers - even mpeg-ts - usually send whole frames.
         * libmad refuses to decode the last frame there.
         * retry with a fake next. */
        do {
          if ((this->stream.error != MAD_ERROR_BUFLEN) || (this->insize < 4))
            break;
          if (!(this->inbuf[1] & 0x04)) /* layer > 2 */
            break;
          if (this->rest_write || (buf->content != buf->mem) || (buf->max_size - buf->size < MAD_BUFFER_GUARD))
            break;
          if (_mad_frame_size (this->inbuf) != this->insize)
            break;
          memset (this->inbuf + this->insize, 0, MAD_BUFFER_GUARD);
          this->inbuf[this->insize] = 0xff;
          this->inbuf[this->insize + 1] = 0xe0;
          mad_stream_buffer (&this->stream, this->inbuf, this->insize + MAD_BUFFER_GUARD);
          err = mad_frame_decode (&this->frame, &this->stream);
          if (!err)
            this->insize = 0;
        } while (0);
        if (!err)
          break;

        if ((this->stream.error == MAD_ERROR_BUFLEN) ||
            (this->stream.error == MAD_ERROR_BADDATAPTR)) {
          /* libmad wants more data */
          this->needs_more_data = 1;
          if (this->insize > 0) {
            if (!this->rest_write) {
              memcpy (this->rest_buf, this->inbuf, this->insize);
              this->rest_write = this->insize;
              this->num_bytes_d -= this->insize;
              this->num_bytes_r += this->insize;
              this->rest_read = 0;
            }
            this->insize = 0;
          } else {
            this->rest_read = 0;
            this->rest_write = 0;
          }
        } else {
          int d;
          lprintf ("error 0x%04X\n", this->stream.error);
          d = _mad_consume (this);
          xprintf (this->xstream->xine, XINE_VERBOSITY_DEBUG + 1,
            "mad_audio_decoder: error 0x%x, consumed %d.\n", (unsigned int)this->stream.error, d);
          if (this->insize > 0)
            mad_stream_buffer (&this->stream, this->inbuf, this->insize);
        }
      } while (0);

      if (!err) {
        mad_fixed_t old_peak = this->peak;
        int mode = (this->frame.header.mode == MAD_MODE_SINGLE_CHANNEL) ? AO_CAP_MODE_MONO : AO_CAP_MODE_STEREO;

	if (!this->output_open
            || (this->output_sampling_rate != this->frame.header.samplerate)
	    || (this->output_mode != mode)) {

	  lprintf ("audio sample rate %d mode %08x\n", this->frame.header.samplerate, mode);

          /* the mpeg audio demuxer can set audio bitrate */
          if (! _x_stream_info_get(this->xstream, XINE_STREAM_INFO_AUDIO_BITRATE)) {
            _x_stream_info_set(this->xstream, XINE_STREAM_INFO_AUDIO_BITRATE,
                               this->frame.header.bitrate);
          }

          /* the mpeg audio demuxer can set this meta info */
          if (! _x_meta_info_get(this->xstream, XINE_META_INFO_AUDIOCODEC)) {
            const char *s;
            switch (this->frame.header.layer) {
              case MAD_LAYER_I:   s = "MPEG audio layer 1 (lib: MAD)"; break;
              case MAD_LAYER_II:  s = "MPEG audio layer 2 (lib: MAD)"; break;
              case MAD_LAYER_III: s = "MPEG audio layer 3 (lib: MAD)"; break;
              default:            s = "MPEG audio (lib: MAD)";
            }
            _x_meta_info_set_utf8 (this->xstream, XINE_META_INFO_AUDIOCODEC, s);
          }

          if (this->output_open)
            this->xstream->audio_out->close (this->xstream->audio_out, this->xstream);
          this->output_open = this->xstream->audio_out->open (this->xstream->audio_out,
            this->xstream, 16, this->frame.header.samplerate, mode);
          if (!this->output_open)
            break;
	  this->output_sampling_rate = this->frame.header.samplerate;
	  this->output_mode = mode;
          this->pts_samples = 0;
	}

	mad_synth_frame (&this->synth, &this->frame);

	if ( (buf->decoder_flags & BUF_FLAG_PREVIEW) == 0 ) {

	  unsigned int         nsamples;
	  audio_buffer_t      *audio_buffer;

          this->num_outbufs += 1;
	  audio_buffer = this->xstream->audio_out->get_buffer (this->xstream->audio_out);

          nsamples  = this->synth.pcm.length;

	  /* padding */
	  if (this->start_padding || this->end_padding) {
	    /* check padding validity */
            if (nsamples < this->start_padding + this->end_padding) {
	      lprintf("invalid padding data");
	      this->start_padding = 0;
	      this->end_padding = 0;
            } else {
              lprintf ("nsamples=%u, start_padding=%u, end_padding=%u\n",
                nsamples, this->start_padding, this->end_padding);
              nsamples -= this->start_padding + this->end_padding;
            }
          }
	  audio_buffer->num_frames = nsamples;

          if (this->synth.pcm.channels == 2) {
            const mad_fixed_t *left = this->synth.pcm.samples[0] + this->start_padding;
            const mad_fixed_t *right = this->synth.pcm.samples[1] + this->start_padding;
            int16_t *output = audio_buffer->mem;
            if (this->declip) {
              while (nsamples--) {
                mad_fixed_t v, a;
                v = *left++;
                a = v < 0 ? -v : v;
                if (a > this->peak)
                  this->peak = a;
                v -= v >> 2;
                *output++ = _mad_scale (v);
                v = *right++;
                a = v < 0 ? -v : v;
                if (a > this->peak)
                  this->peak = a;
                v -= v >> 2;
                *output++ = _mad_scale (v);
              }
            } else {
              while (nsamples--) {
                mad_fixed_t v, a;
                v = *left++;
                a = v < 0 ? -v : v;
                if (a > this->peak)
                  this->peak = a;
                *output++ = _mad_scale (v);
                v = *right++;
                a = v < 0 ? -v : v;
                if (a > this->peak)
                  this->peak = a;
                *output++ = _mad_scale (v);
              }
            }
          } else {
            const mad_fixed_t *mid = this->synth.pcm.samples[0] + this->start_padding;
            int16_t *output = audio_buffer->mem;
            if (this->declip) {
              while (nsamples--) {
                mad_fixed_t v, a;
                v = *mid++;
                a = v < 0 ? -v : v;
                if (a > this->peak)
                  this->peak = a;
                v -= v >> 2;
                *output++ = _mad_scale (v);
              }
            } else {
              while (nsamples--) {
                mad_fixed_t v, a;
                v = *mid++;
                a = v < 0 ? -v : v;
                if (a > this->peak)
                  this->peak = a;
                *output++ = _mad_scale (v);
              }
            }
          }
          /* disregard glitches after seek. */
          if (this->seek) {
            this->seek -= 1;
            this->peak = old_peak;
          }
          /* auto declip when peaks exnceed 0.4dB. */
          if (!this->declip && ((uint32_t)this->peak > (0x860dbbdb >> (31 - MAD_F_FRACBITS)))) {
            this->declip = 1;
            xprintf (this->xstream->xine, XINE_VERBOSITY_LOG,
              "mad_audio_decoder: source too loud, adding -2.5dB declip filter.\n");
          }

          /* pts computing */
          if (this->synth.pcm.length != this->pts_samples) {
            /* mpeg audio involves frequency transform, which needs some delay on both the
             * encoding and decoding sides. encode delay varies with encoding algorithm.
             * it is available from optional Xing head via BUF_FLAG_AUDIO_PADDING in the
             * buf->decoder_info[1] field.
             * decode delay is more or less standardized.
             * the simpliest approach to deal with this is to let each side compensate
             * for its own known delay. thus, remove decoder delay from timeline here. */
            this->pts_samples = this->synth.pcm.length;
            if (this->frame.header.samplerate) {
              if (this->frame.header.layer == MAD_LAYER_III) {
                /* 528 of 1152 samples. */
                this->pts_delay = this->synth.pcm.length * (528 * 90000 / 1152) / this->frame.header.samplerate;
              } else {
                /* 240 of 1152 samples. */
                this->pts_delay = this->synth.pcm.length * (240 * 90000 / 1152) / this->frame.header.samplerate;
              }
            }
            xprintf (this->xstream->xine, XINE_VERBOSITY_DEBUG,
              "mad_audio_decoder: decoder delay %u pts.\n", this->pts_delay);
          }
          if (this->pts_read != this->pts_write) {
            audio_buffer->vpts = this->pts_ring[this->pts_read].pts;
            this->pts_ring[this->pts_read].pts = 0;
            if (audio_buffer->vpts)
              audio_buffer->vpts -= this->pts_delay;
          } else {
            audio_buffer->vpts = 0;
          }
          {
            int d = _mad_consume (this);
            xprintf (this->xstream->xine, XINE_VERBOSITY_DEBUG + 1,
              "mad_audio_decoder: outbuf consumed %d, samples %d, pts %" PRId64 ".\n",
              d, audio_buffer->num_frames, audio_buffer->vpts);
          }
          {
#if 0
            int bitrate = this->frame.header.bitrate;
            if (bitrate <= 0) {
              bitrate = _x_stream_info_get (this->xstream, XINE_STREAM_INFO_AUDIO_BITRATE);
              lprintf ("offset %d bps\n", bitrate);
            }
            if (audio_buffer->vpts && (bitrate > 0)) {
              int pts_offset = (bytes_in_buffer_at_pts * 8 * 90) / (bitrate / 1000);
              lprintf ("pts: %"PRId64", offset: %d pts, %d bytes\n", buf->pts, pts_offset, bytes_in_buffer_at_pts);
              if (audio_buffer->vpts < pts_offset)
                pts_offset = audio_buffer->vpts;
              audio_buffer->vpts -= pts_offset;
            }
#endif
          }

	  this->xstream->audio_out->put_buffer (this->xstream->audio_out, audio_buffer, this->xstream);

	  if (buf->decoder_flags & BUF_FLAG_AUDIO_PADDING) {
	    this->start_padding = buf->decoder_info[1];
	    this->end_padding = buf->decoder_info[2];
	    buf->decoder_info[1] = 0;
	    buf->decoder_info[2] = 0;
	  } else {
	    this->start_padding = 0;
	    this->end_padding = 0;
	  }
	}
	lprintf ("decode worked\n");
      }

    }
  }
}

/* Report peak level _before_ clipping. Seen up to +3.5dB there. Why?
 * Well, most audio CDs are mastered with a "wave shaper" like the one
 * audacity once had. It folds some peaks in towards 0, and allowes to
 * louden everything then. We hardly hear a difference, which is why
 * mpeg and most other audio codecs drop that info. After decoding,
 * some of those peaks are back, and get chopped off. That yields
 * distortion, and kills background sounds during loud passages.
 * Even worse, lame encoder seems to be affected by internal clipping
 * there, too. Recommended: lame --scale 0.707 -V1 foo.wav. */
static int _mad_fixed_2_db (mad_fixed_t v) {
  static const uint32_t tab[] = {
    /* 0x80000000 * pow (2.0, index / 60.0) */
    0x80000000, 0x817CBEEE, 0x82FDEA6A, 0x84838F9F, 0x860DBBDB,
    0x879C7C96, 0x892FDF71, 0x8AC7F232, 0x8C64C2CC, 0x8E065F59,
    0x8FACD61E, 0x91583589, 0x93088C35, 0x94BDE8E8, 0x96785A92,
    0x9837F051, 0x99FCB971, 0x9BC6C569, 0x9D9623DF, 0x9F6AE4AA,
    0xA14517CC, 0xA324CD79, 0xA50A1615, 0xA6F50235, 0xA8E5A29D,
    0xAADC0847, 0xACD8445C, 0xAEDA6839, 0xB0E28570, 0xB2F0ADC6,
    0xB504F333, 0xB71F67E9, 0xB9401E4D, 0xBB6728FB, 0xBD949AC6,
    0xBFC886BB, 0xC203001D, 0xC4441A6B, 0xC68BE95B, 0xC8DA80E1,
    0xCB2FF529, 0xCD8C5A9E, 0xCFEFC5E6, 0xD25A4BE4, 0xD4CC01BB,
    0xD744FCCA, 0xD9C552B4, 0xDC4D1957, 0xDEDC66D6, 0xE1735195,
    0xE411F03A, 0xE6B859AE, 0xE966A51F, 0xEC1CEA00, 0xEEDB4008,
    0xF1A1BF38, 0xF4707FD5, 0xF7479A6E, 0xFA2727DB, 0xFD0F413D
  };
  uint32_t u, b, m, e;
  int r = 60 * (31 - MAD_F_FRACBITS);
  u = v;
  while (!(u & 0x80000000)) {
    r -= 60;
    u <<= 1;
  }
  b = 0;
  e = sizeof (tab) / sizeof (tab[0]);
  do {
    m = (b + e) >> 1;
    if (u < tab[m])
      e = m;
    else
      b = m + 1;
  } while (b != e);
  r += b;
  return r;
}

static void mad_dispose (audio_decoder_t *this_gen) {

  mad_decoder_t *this = (mad_decoder_t *) this_gen;

  mad_synth_finish (&this->synth);
  mad_frame_finish (&this->frame);
  mad_stream_finish(&this->stream);

  if (this->output_open) {
    this->xstream->audio_out->close (this->xstream->audio_out, this->xstream);
    this->output_open = 0;
  }

  xprintf (this->xstream->xine, XINE_VERBOSITY_DEBUG,
    "mad_audio_decoder: %u inbufs, %u direct bytes, %u reassembled bytes, %u outbufs.\n",
    this->num_inbufs, this->num_bytes_d, this->num_bytes_r, this->num_outbufs);
  {
    int l = this->declip ? XINE_VERBOSITY_LOG : XINE_VERBOSITY_DEBUG;
    int peak = _mad_fixed_2_db (this->peak);
    const char *s = peak < 0 ? "-" : "+";
    peak = peak < 0 ? -peak : peak;
    xprintf (this->xstream->xine, l,
      "mad_audio_decoder: peak level %d / %s%0d.%01ddB.\n",
      (int)this->peak, s, peak / 10, peak % 10);
  }

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  mad_decoder_t *this ;

  (void)class_gen;
  this = (mad_decoder_t *) calloc(1, sizeof(mad_decoder_t));
  if (!this)
    return NULL;

  /* Do these first, when compiler still knows "this" is all zeroed.
   * Let it optimize away this on most systems where clear mem
   * interpretes as 0, 0f or NULL safely.
   */
#ifndef HAVE_ZERO_SAFE_MEM
  this->output_open     = 0;
  this->rest_read       = 0;
  this->rest_write      = 0;
  this->preview_mode    = 0;
  this->num_inbufs      = 0;
  this->num_bytes_d     = 0;
  this->num_bytes_r     = 0;
  this->num_outbufs     = 0;
  this->declip          = 0;
  this->pts_samples     = 0;
  this->pts_delay       = 0;
  this->pts_read        = 0;
  this->pts_write       = 0;
#endif

  this->seek            = 2;
  this->peak            = 1;

  this->audio_decoder.decode_data   = mad_decode_data;
  this->audio_decoder.reset         = mad_reset;
  this->audio_decoder.discontinuity = mad_discontinuity;
  this->audio_decoder.dispose       = mad_dispose;

  this->xstream = stream;

  mad_synth_init  (&this->synth);
  mad_stream_init (&this->stream);
  mad_frame_init  (&this->frame);

  this->stream.options = MAD_OPTION_IGNORECRC;

  lprintf ("init\n");

  return &this->audio_decoder;
}

/*
 * mad plugin class
 */
static void *init_plugin (xine_t *xine, const void *data) {
  static const audio_decoder_class_t this = {
    .open_plugin     = open_plugin,
    .identifier      = "mad",
    .description     = N_("libmad based mpeg audio layer 1/2/3 decoder plugin"),
    .dispose         = NULL
  };
  (void)xine;
  (void)data;
  return (void *)&this;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_MPEG, 0
};

static const decoder_info_t dec_info_audio = {
  .supported_types = audio_types,
  .priority        = 8,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_DECODER, 16, "mad", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
