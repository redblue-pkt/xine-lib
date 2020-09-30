/*
 * Copyright (C) 2000-2019 the xine project
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
 * stuff needed to turn liba52 into a xine decoder plugin
 */

#define _DEFAULT_SOURCE 1
#ifndef __sun
/* required for swab() */
#define _XOPEN_SOURCE 500
#endif
/* avoid compiler warnings */
#define _BSD_SOURCE 1

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#define LOG_MODULE "a52_decoder"
#define LOG_VERBOSE
/*
#define LOG
#define LOG_PTS
*/

#include <xine/xine_internal.h>
#include <xine/audio_out.h>

#ifdef HAVE_A52DEC_A52_H
# include <a52dec/a52.h>
#else
# include "a52.h"
#endif

#include <xine/buffer.h>
#include <xine/xineutils.h>

#if defined(LIBA52_FIXED)
# define SAMPLE_OFFS 0
#elif defined(LIBA52_DOUBLE)
# define SAMPLE_OFFS 48
#else
# define SAMPLE_OFFS 384
#endif

#undef DEBUG_A52
#ifdef DEBUG_A52
int a52file;
#endif

typedef struct {
  audio_decoder_class_t   decoder_class;
  config_values_t *config;

  sample_t         a52_level;
  int              disable_dynrng_compress;
  int              enable_surround_downmix;

  sample_t         lfe_level_1;
  sample_t         lfe_level_2;
} a52dec_class_t;

typedef struct {
  uint8_t          got_frame;
  uint8_t          format_changed;
  uint8_t          sync_state;

  int              a52_flags;
  int              a52_bit_rate;
  int              a52_sample_rate;

  int              frame_length, frame_todo;

  uint16_t         syncword;

  uint8_t         *frame_ptr;
  uint8_t          frame_buffer[3840];
} xine_a52_parser_t;

static void xine_a52_parser_reset(xine_a52_parser_t *this) {
  this->syncword   = 0;
  this->sync_state = 0;
}

typedef struct a52dec_decoder_s {
  audio_decoder_t  audio_decoder;

  a52dec_class_t  *class;
  xine_stream_t   *stream;
  int64_t          pts;

  a52_state_t     *a52_state;
  int              have_lfe;

  int              a52_flags_map[11];
  int              a52_flags_map_lfe[11];
  int              ao_flags_map[11];
  int              ao_flags_map_lfe[11];

  int              bypass_mode;
  int              output_sampling_rate;
  int              output_open;
  int              output_mode;

  xine_a52_parser_t parser;
} a52dec_decoder_t;

struct frmsize_s
{
  uint16_t bit_rate;
  uint16_t frm_size[3];
};

static const struct frmsize_s frmsizecod_tbl[64] =
{
  { 32  ,{64   ,69   ,96   } },
  { 32  ,{64   ,70   ,96   } },
  { 40  ,{80   ,87   ,120  } },
  { 40  ,{80   ,88   ,120  } },
  { 48  ,{96   ,104  ,144  } },
  { 48  ,{96   ,105  ,144  } },
  { 56  ,{112  ,121  ,168  } },
  { 56  ,{112  ,122  ,168  } },
  { 64  ,{128  ,139  ,192  } },
  { 64  ,{128  ,140  ,192  } },
  { 80  ,{160  ,174  ,240  } },
  { 80  ,{160  ,175  ,240  } },
  { 96  ,{192  ,208  ,288  } },
  { 96  ,{192  ,209  ,288  } },
  { 112 ,{224  ,243  ,336  } },
  { 112 ,{224  ,244  ,336  } },
  { 128 ,{256  ,278  ,384  } },
  { 128 ,{256  ,279  ,384  } },
  { 160 ,{320  ,348  ,480  } },
  { 160 ,{320  ,349  ,480  } },
  { 192 ,{384  ,417  ,576  } },
  { 192 ,{384  ,418  ,576  } },
  { 224 ,{448  ,487  ,672  } },
  { 224 ,{448  ,488  ,672  } },
  { 256 ,{512  ,557  ,768  } },
  { 256 ,{512  ,558  ,768  } },
  { 320 ,{640  ,696  ,960  } },
  { 320 ,{640  ,697  ,960  } },
  { 384 ,{768  ,835  ,1152 } },
  { 384 ,{768  ,836  ,1152 } },
  { 448 ,{896  ,975  ,1344 } },
  { 448 ,{896  ,976  ,1344 } },
  { 512 ,{1024 ,1114 ,1536 } },
  { 512 ,{1024 ,1115 ,1536 } },
  { 576 ,{1152 ,1253 ,1728 } },
  { 576 ,{1152 ,1254 ,1728 } },
  { 640 ,{1280 ,1393 ,1920 } },
  { 640 ,{1280 ,1394 ,1920 } }
};

static void a52dec_reset (audio_decoder_t *this_gen) {

  a52dec_decoder_t *this = xine_container_of(this_gen, a52dec_decoder_t, audio_decoder);

  xine_a52_parser_reset(&this->parser);
  this->pts               = 0;
}

static void a52dec_discontinuity (audio_decoder_t *this_gen) {

  a52dec_decoder_t *this = xine_container_of(this_gen, a52dec_decoder_t, audio_decoder);

  this->pts               = 0;
}

/* Note we write to the samples array here.
   This seems OK since liba52 itself does so when downmixing non-lfe channels. */

#if defined(LIBA52_FIXED)

static inline void downmix_lfe_1 (sample_t *target, sample_t *lfe, sample_t gain) {
  int i;
  for (i = 0; i < 256; i++)
    target[i] += (lfe[i] * (int64_t)gain) >> 26;
}

static inline void downmix_lfe_2 (sample_t *target1, sample_t *target2, sample_t *lfe, sample_t gain) {
  int i;
  for (i = 0; i < 256; i++) {
    sample_t v = (lfe[i] * (int64_t)gain) >> 26;
    target1[i] += v;
    target2[i] += v;
  }
}

static inline void float_to_int (sample_t *_f, int16_t *s16, int num_channels) {
  int i;
  for (i = 0; i < 256; i++) {
    int32_t v = _f[i] >> (30 - 15);
    s16[num_channels * i] = (v + 0x8000) & ~0xffff ? (v >> 31) ^ 0x7fff : v;
  }
}

#elif defined(LIBA52_DOUBLE)

static inline void downmix_lfe_1 (sample_t *target, sample_t *lfe, sample_t gain) {
  int i;
  for (i = 0; i < 256; i++)
    target[i] += (lfe[i] - 48.0) * gain;
}

static inline void downmix_lfe_2 (sample_t *target1, sample_t *target2, sample_t *lfe, sample_t gain) {
  int i;
  for (i = 0; i < 256; i++) {
    sample_t v = (lfe[i] - 48.0) * gain;
    target1[i] += v;
    target2[i] += v;
  }
}

static inline void float_to_int (sample_t *_f, int16_t *s16, int num_channels) {
  int i;
  /* HACK: assume IEEE double, then simplify (int32_t)((double)v * (1 << 15)) to
   * (int32_t)(<reinterpret_cast>(int64_t)((double)v + 48.0) >> 32) - 0x40480000.
   * This will work with -32.0 < v < 32.0 only, but liba52 hardly sends more than +/- 2.0. */
  for (i = 0; i < 256; i++) {
#  ifdef WORDS_BIGENDIAN
#    define HI_INDEX 0
#  else
#    define HI_INDEX 1
#  endif
    union {sample_t s; int32_t i[2];} u;
    int32_t v;
    u.s = _f[i];
    v = u.i[HI_INDEX] - 0x40480000;
    s16[num_channels * i] = (v + 0x8000) & ~0xffff ? (v >> 31) ^ 0x7fff : v;
  }
}

#else /* float */

static inline void downmix_lfe_1 (sample_t *target, sample_t *lfe, sample_t gain) {
  int i;
  for (i = 0; i < 256; i++)
    target[i] += (lfe[i] - 384.0) * gain;
}

static inline void downmix_lfe_2 (sample_t *target1, sample_t *target2, sample_t *lfe, sample_t gain) {
  int i;
  for (i = 0; i < 256; i++) {
    sample_t v = (lfe[i] - 384.0) * gain;
    target1[i] += v;
    target2[i] += v;
  }
}

static inline void float_to_int (sample_t *_f, int16_t *s16, int num_channels) {
  int i;
  /* XXX assumes IEEE float format, range -2.0 ... 2.0 */
  for (i = 0; i < 256; i++) {
    union {sample_t s; int32_t i;} u;
    int32_t v;
    u.s = _f[i];
    v = u.i - 0x43c00000;
    s16[num_channels * i] = (v + 0x8000) & ~0xffff ? (v >> 31) ^ 0x7fff : v;
  }
}

#endif

static inline void mute_channel (int16_t * s16, int num_channels) {
  int i;

  for (i = 0; i < 256; i++) {
    s16[num_channels*i] = 0;
  }
}

static void a52dec_decode_frame (a52dec_decoder_t *this, int64_t pts, int preview_mode) {

  int output_mode = AO_CAP_MODE_STEREO;

  /*
   * do we want to decode this frame in software?
   */
#ifdef LOG_PTS
  printf("a52dec:decode_frame:pts=%lld\n",pts);
#endif
  if (!this->bypass_mode) {

    int              a52_output_flags, i;
    sample_t         level = this->class->a52_level;
    audio_buffer_t  *buf;
    int16_t         *int_samples;
    sample_t        *samples = a52_samples(this->a52_state);

    /*
     * oki, decode this frame in software
     */

    /* determine output mode */

    a52_output_flags = this->parser.a52_flags & A52_LFE ?
      this->a52_flags_map_lfe[this->parser.a52_flags & A52_CHANNEL_MASK] :
      this->a52_flags_map[this->parser.a52_flags];

    if (a52_frame (this->a52_state, this->parser.frame_buffer, &a52_output_flags, &level, SAMPLE_OFFS)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "liba52: a52_frame error\n");
      return;
    }

    if (this->class->disable_dynrng_compress)
      a52_dynrng (this->a52_state, NULL, NULL);

    this->have_lfe = a52_output_flags & A52_LFE;
    if (this->have_lfe) {
      output_mode = this->ao_flags_map_lfe[a52_output_flags & A52_CHANNEL_MASK];
      samples += 256;
    } else
      output_mode = this->ao_flags_map[a52_output_flags];
    /*
     * (re-)open output device
     */

    if (!this->output_open
        || (this->parser.a52_sample_rate != this->output_sampling_rate)
	|| (output_mode != this->output_mode)) {

      if (this->output_open)
	this->stream->audio_out->close (this->stream->audio_out, this->stream);


      this->output_open = (this->stream->audio_out->open) (this->stream->audio_out,
							 this->stream, 16,
                                                           this->parser.a52_sample_rate,
							 output_mode) ;
      this->output_sampling_rate = this->parser.a52_sample_rate;
      this->output_mode = output_mode;
    }


    if (!this->output_open || preview_mode)
      return;


    /*
     * decode a52 and convert/interleave samples
     */

    buf = this->stream->audio_out->get_buffer (this->stream->audio_out);
    int_samples = buf->mem;
    buf->num_frames = 256*6;

    for (i = 0; i < 6; i++) {
      if (a52_block (this->a52_state)) {
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "liba52: a52_block error on audio channel %d\n", i);
#if 0
	for(n=0;n<2000;n++) {
	  printf("%02x ",this->parser.frame_buffer[n]);
	  if ((n % 32) == 0) printf("\n");
	}
	printf("\n");
#endif
	buf->num_frames = 0;
	break;
      }

      /* We now have up to 6 groups of 256 samples each, in the order
         LFE L C R RL RR. Channels not present and/or not requested
         are simply left out (no gaps). Downmixing had only been applied
         to non-LFE stuff, so we need to do that one ourselves. */

      switch (output_mode) {
      case AO_CAP_MODE_MONO:
	if (this->have_lfe)
	  downmix_lfe_1 (&samples[0*256], &samples[-1*256], this->class->lfe_level_1);
	float_to_int (&samples[0], int_samples+(i*256), 1);
	break;
      case AO_CAP_MODE_STEREO:
	if (this->have_lfe)
	  downmix_lfe_2 (&samples[0*256], &samples[1*256], &samples[-1*256], this->class->lfe_level_2);
	float_to_int (&samples[0*256], int_samples+(i*256*2), 2);
	float_to_int (&samples[1*256], int_samples+(i*256*2)+1, 2);
	break;
      case AO_CAP_MODE_4CHANNEL:
	if (this->have_lfe)
	  downmix_lfe_2 (&samples[0*256], &samples[1*256], &samples[-1*256], this->class->lfe_level_2);
	float_to_int (&samples[0*256], int_samples+(i*256*4),   4); /*  L */
	float_to_int (&samples[1*256], int_samples+(i*256*4)+1, 4); /*  R */
	float_to_int (&samples[2*256], int_samples+(i*256*4)+2, 4); /* RL */
	float_to_int (&samples[3*256], int_samples+(i*256*4)+3, 4); /* RR */
	break;
      case AO_CAP_MODE_4_1CHANNEL:
	if (this->have_lfe)
	  float_to_int (&samples[-1*256], int_samples+(i*256*6)+5, 6); /* LFE */
	else
	  mute_channel (int_samples+(i*256*6)+5, 6);
	float_to_int (&samples[0*256], int_samples+(i*256*6)+0, 6); /* L   */
        float_to_int (&samples[1*256], int_samples+(i*256*6)+1, 6); /* R   */
	float_to_int (&samples[2*256], int_samples+(i*256*6)+2, 6); /* RL */
	float_to_int (&samples[3*256], int_samples+(i*256*6)+3, 6); /* RR */
	mute_channel ( int_samples+(i*256*6)+4, 6); /* C */
	break;
      case AO_CAP_MODE_5CHANNEL:
	if (this->have_lfe)
	  downmix_lfe_2 (&samples[0*256], &samples[2*256], &samples[-1*256], this->class->lfe_level_2);
	float_to_int (&samples[0*256], int_samples+(i*256*6)+0, 6); /*  L */
        float_to_int (&samples[1*256], int_samples+(i*256*6)+4, 6); /*  C */
	float_to_int (&samples[2*256], int_samples+(i*256*6)+1, 6); /*  R */
	float_to_int (&samples[3*256], int_samples+(i*256*6)+2, 6); /* RL */
	float_to_int (&samples[4*256], int_samples+(i*256*6)+3, 6); /* RR */
	mute_channel ( int_samples+(i*256*6)+5, 6); /* LFE */
	break;
      case AO_CAP_MODE_5_1CHANNEL:
	if (this->have_lfe)
	  float_to_int (&samples[-1*256], int_samples+(i*256*6)+5, 6); /* lfe */
	else
	  mute_channel (int_samples+(i*256*6)+5, 6);
	float_to_int (&samples[0*256], int_samples+(i*256*6)+0, 6); /*   L */
	float_to_int (&samples[1*256], int_samples+(i*256*6)+4, 6); /*   C */
	float_to_int (&samples[2*256], int_samples+(i*256*6)+1, 6); /*   R */
	float_to_int (&samples[3*256], int_samples+(i*256*6)+2, 6); /*  RL */
	float_to_int (&samples[4*256], int_samples+(i*256*6)+3, 6); /*  RR */
	break;
      default:
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "liba52: help - unsupported mode %08x\n", output_mode);
      }
    }

    lprintf ("%d frames output\n", buf->num_frames);

    /*  output decoded samples */

    buf->vpts       = pts;

    this->stream->audio_out->put_buffer (this->stream->audio_out, buf, this->stream);

  } else {

    /*
     * loop through a52 data
     */

    if (!this->output_open) {
      this->output_open = (this->stream->audio_out->open) (this->stream->audio_out,
						 this->stream, 16,
                                                           this->parser.a52_sample_rate,
						 AO_CAP_MODE_A52) ;
      this->output_mode = AO_CAP_MODE_A52;
    }

    if (this->output_open && !preview_mode) {
      /* SPDIF Passthrough
       * Build SPDIF Header and encaps the A52 audio data in it.
       */
      uint32_t /*syncword, crc1,*/ fscod,frmsizecod,/*bsid,*/bsmod,frame_size;
      uint8_t *data_out,*data_in;
      audio_buffer_t *buf = this->stream->audio_out->get_buffer (this->stream->audio_out);
      data_in=(uint8_t *) this->parser.frame_buffer;
      data_out=(uint8_t *) buf->mem;
      /*syncword = data_in[0] | (data_in[1] << 8);*/
      /*crc1 = data_in[2] | (data_in[3] << 8);*/
      fscod = (data_in[4] >> 6) & 0x3;
      frmsizecod = data_in[4] & 0x3f;
      /*bsid = (data_in[5] >> 3) & 0x1f;*/
      bsmod = data_in[5] & 0x7;		/* bsmod, stream = 0 */
      frame_size = frmsizecod_tbl[frmsizecod].frm_size[fscod] ;

      data_out[0] = 0x72; data_out[1] = 0xf8;	/* spdif syncword    */
      data_out[2] = 0x1f; data_out[3] = 0x4e;	/* ..............    */
      data_out[4] = 0x01;			/* AC3 data          */
      data_out[5] = bsmod;			/* bsmod, stream = 0 */
      data_out[6] = (frame_size << 4) & 0xff;   /* frame_size * 16   */
      data_out[7] = ((frame_size ) >> 4) & 0xff;
      swab(data_in, &data_out[8], frame_size * 2 );

      buf->num_frames = 1536;
      buf->vpts       = pts;

      this->stream->audio_out->put_buffer (this->stream->audio_out, buf, this->stream);

    }
  }
}

static const char *a52_channel_info(int a52_flags) {
  switch (a52_flags & A52_CHANNEL_MASK) {
    case A52_3F2R:
      return (a52_flags & A52_LFE) ? "A/52 5.1" : "A/52 5.0";
    case A52_3F1R:
    case A52_2F2R:
      return (a52_flags & A52_LFE) ? "A/52 4.1" : "A/52 4.0";
    case A52_2F1R:
    case A52_3F:
      return "A/52 3.0";
    case A52_STEREO:
      return "A/52 2.0 (stereo)";
    case A52_DOLBY:
      return "A/52 2.0 (dolby)";
    case A52_MONO:
      return "A/52 1.0";
    default:
      return "A/52";
  }
}

static void a52_meta_info_set(xine_stream_t *stream, int a52_flags, int bit_rate, int sample_rate) {
  _x_meta_info_set_utf8 (stream, XINE_META_INFO_AUDIOCODEC,         a52_channel_info(a52_flags));
  _x_stream_info_set    (stream, XINE_STREAM_INFO_AUDIO_BITRATE,    bit_rate);
  _x_stream_info_set    (stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE, sample_rate);
}

static void do_swab(uint8_t *p, uint8_t *end) {
    lprintf ("byte-swapping dnet\n");

    while (p != end) {
      uint8_t byte = *p++;
      *(p - 1) = *p;
      *p++ = byte;
    }
}

static size_t a52dec_parse_data(xine_a52_parser_t *this, xine_stream_t *stream, const uint8_t *data, size_t size);

static void a52dec_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  a52dec_decoder_t *this = xine_container_of(this_gen, a52dec_decoder_t, audio_decoder);

  lprintf ("decode data %d bytes of type %08x, pts=%"PRId64"\n",
	   buf->size, buf->type, buf->pts);
  lprintf ("decode data decoder_info=%d, %d\n",buf->decoder_info[1],buf->decoder_info[2]);

  if (buf->decoder_flags & BUF_FLAG_HEADER)
    return;

  /* swap byte pairs if this is RealAudio DNET data */
  if (buf->type == BUF_AUDIO_DNET) {
    do_swab(buf->content, buf->content + buf->size);
  }

  /* A52 packs come from the DVD in blocks of about 2048 bytes.
   * Only 1 PTS values can be assigned to each block.
   * An A52 frame is about 1700 bytes long.
   * So, a single A52 packs can contain 2 A52 frames (or the beginning of an A52 frame at least).
   * If we have a PTS value, which A52 frame does it apply to? The A52 pack tells us that.
   * So, the info about which A52 frame the PTS applies to is contained in decoder_info sent from the demuxer.
   *
   * The PTS value from the A52 pack (DVD sector) can only be applied at the start of an A52 frame.
   * We call the start of an A52 frame a frame header.
   * So, if a A52 pack has 2 "Number of frame headers" is means that the A52 pack contains 2 A52 frame headers.
   * The "First access unit" then tells us which A52 frame the PTS value applies to.
   *
   * Take the following example: -
   * PACK1: PTS = 10. Contains the entire A52 frame1, followed by the beginning of the frame2. PTS applies to frame1.
   * PACK2: PTS = 1000, Contains the rest of frame2, and the whole of frame3. and the start of frame4. PTS applies to frame4.
   * PACK3: PTS = 0 (none), Contains the rest of frame4.
   *
   * Output should be: -
   * frame1, PTS=10
   * frame2, PTS=0
   * frame3, PTS=0
   * frame4, PTS=1000
   *
   * So, we have to keep track of PTS values from previous A52 packs here, otherwise they get put on the wrong frame.
   */

  /* FIXME: the code here does not match the explanation above */
  if (buf->pts) {
    this->pts = buf->pts;
  }
#if 0
  for(n=0;n < buf->size;n++) {
    if ((n % 32) == 0) printf("\n");
    printf("%x ", current[n]);
  }
  printf("\n");
#endif

  lprintf ("processing...state %d\n", this->parser.sync_state);

  while (buf->size > 0) {
    int consumed = a52dec_parse_data(&this->parser, this->stream, buf->content, buf->size);
    buf->content += consumed;
    buf->size -= consumed;
    if (this->parser.got_frame) {
      if (this->parser.format_changed) {
        a52_meta_info_set(this->stream, this->parser.a52_flags, this->parser.a52_bit_rate, this->parser.a52_sample_rate);
      }
      a52dec_decode_frame (this, this->pts, buf->decoder_flags & BUF_FLAG_PREVIEW);
      this->pts = 0;
    }
  }
}

static size_t a52dec_parse_data(xine_a52_parser_t *this, xine_stream_t *stream, const uint8_t *data, size_t size) {

  const uint8_t *const end = data + size;
  const uint8_t       *current = data;
  const uint8_t       *sync_start = current + 1;

  this->format_changed = this->got_frame = 0;

  while (current < end) {
    switch (this->sync_state) {
    case 0:  /* Looking for sync header */
	  this->syncword = (this->syncword << 8) | *current++;
	  if (this->syncword == 0x0b77) {

	    this->frame_buffer[0] = 0x0b;
	    this->frame_buffer[1] = 0x77;

	    this->sync_state = 1;
	    this->frame_ptr = this->frame_buffer+2;
	  }
          break;

    case 1:  /* Looking for enough bytes for sync_info. */
          sync_start = current - 1;
	  *this->frame_ptr++ = *current++;
          if ((this->frame_ptr - this->frame_buffer) > 16) {
	    int a52_flags_old       = this->a52_flags;
	    int a52_sample_rate_old = this->a52_sample_rate;
	    int a52_bit_rate_old    = this->a52_bit_rate;

	    this->frame_length = a52_syncinfo (this->frame_buffer,
					       &this->a52_flags,
					       &this->a52_sample_rate,
					       &this->a52_bit_rate);

            if (this->frame_length < 80) { /* Invalid a52 frame_length */
	      this->syncword = 0;
	      current = sync_start;
	      this->sync_state = 0;
	      break;
	    }

            lprintf("Frame length = %d\n",this->frame_length);

	    this->frame_todo = this->frame_length - 17;
	    this->sync_state = 2;
            if (a52_flags_old       != this->a52_flags ||
                a52_sample_rate_old != this->a52_sample_rate ||
		a52_bit_rate_old    != this->a52_bit_rate) {
              this->format_changed = 1;
            }
          }
          break;

    case 2:  /* Filling frame_buffer with sync_info bytes */
	  *this->frame_ptr++ = *current++;
	  this->frame_todo--;
          if (this->frame_todo > 0)
            break;
          /* Ready for decode */
      this->syncword = 0;
      this->sync_state = 0;
      if (xine_crc16_ansi (0, &this->frame_buffer[2], this->frame_length - 2) != 0) { /* CRC16 failed */
	xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "liba52:a52 frame failed crc16 checksum.\n");
	current = sync_start;
	break;
      }
      this->got_frame = 1;
      return (current - data);
          break;
    default: /* No come here */
          break;
    }
  }

#ifdef DEBUG_A52
      write (a52file, this->frame_buffer, this->frame_length);
#endif

  return size;
}

static void a52dec_dispose (audio_decoder_t *this_gen) {

  a52dec_decoder_t *this = xine_container_of(this_gen, a52dec_decoder_t, audio_decoder);

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);

  this->output_open = 0;

  a52_free(this->a52_state);
  this->a52_state = NULL;

#ifdef DEBUG_A52
  close (a52file);
#endif
  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  a52dec_decoder_t *this ;
  int               audio_caps;

  lprintf ("open_plugin called\n");

  this = calloc(1, sizeof (a52dec_decoder_t));
  if (!this)
    return NULL;

  /* Do these first, when compiler still knows "this" is all zeroed.
   * Let it optimize away this on most systems where clear mem
   * interpretes as 0, 0f or NULL safely.
   */
  xine_a52_parser_reset(&this->parser);
  this->output_open       = 0;
  this->pts               = 0;

  this->audio_decoder.decode_data   = a52dec_decode_data;
  this->audio_decoder.reset         = a52dec_reset;
  this->audio_decoder.discontinuity = a52dec_discontinuity;
  this->audio_decoder.dispose       = a52dec_dispose;

  this->stream = stream;
  this->class  = (a52dec_class_t *)class_gen;

  audio_caps = stream->audio_out->get_capabilities (stream->audio_out);

  if( !this->a52_state ) {
    this->a52_state =
#ifdef HAVE_A52DEC_A52_H /* External liba52 */
#  ifdef HAVE_A52_INIT_VOID
      a52_init ()
#  else
      /* When using external liba52, enable _all_ capabilities, even
	 if that might break stuff if they add some new capability
	 that depends on CPU's caps.
	 At the moment the only capability is DJBFFT, which is tested
	 only if djbfft is being used at compile time.

	 The actual question would be: why don't they check for
	 capabilities themselves?
      */
      a52_init (0xFFFFFFFF)
#  endif
#else
      a52_init (xine_mm_accel())
#endif
      ;
  }

  /*
   * find out if this driver supports a52 output
   * or, if not, how many channels we've got
   */

  if (audio_caps & AO_CAP_MODE_A52)
    this->bypass_mode = 1;
  else {
    const int modes[] = {
      AO_CAP_MODE_MONO,        A52_MONO,
      AO_CAP_MODE_STEREO,      A52_STEREO,
      AO_CAP_MODE_4CHANNEL,    A52_2F2R,
      AO_CAP_MODE_4_1CHANNEL,  A52_2F2R | A52_LFE,
      AO_CAP_MODE_5CHANNEL,    A52_3F2R,
      AO_CAP_MODE_5_1CHANNEL,  A52_3F2R | A52_LFE
    }, wishlist[] = {
      A52_MONO,   0, 2, 4, 6, 8, 10,
      A52_STEREO, 2, 4, 6, 8, 10, 0,
      A52_3F,     8, 10, 2, 4, 6, 0,
      A52_2F1R,   4, 6, 8, 10, 2, 0,
      A52_3F1R,   8, 10, 4, 6, 2, 0,
      A52_2F2R,   4, 6, 8, 10, 2, 0,
      A52_3F2R,   8, 10, 4, 6, 2, 0,
      A52_DOLBY,  2, 4, 6, 8, 10, 0,
      /* same thing again with lfe */
      A52_MONO,   6, 10, 0, 2, 4, 8,
      A52_STEREO, 6, 10, 2, 4, 8, 0,
      A52_3F,     10, 6, 8, 2, 4, 0,
      A52_2F1R,   6, 10, 4, 8, 2, 0,
      A52_3F1R,   10, 6, 8, 4, 2, 0,
      A52_2F2R,   6, 10, 4, 8, 2, 0,
      A52_3F2R,   10, 6, 8, 4, 2, 0,
      A52_DOLBY,  2, 4, 6, 8, 10, 0
    };
    int i, j;

    this->bypass_mode = 0;

    /* guard against weird audio out */
    if (!(audio_caps & (AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO |
      AO_CAP_MODE_4CHANNEL | AO_CAP_MODE_4_1CHANNEL |
      AO_CAP_MODE_5CHANNEL | AO_CAP_MODE_5_1CHANNEL)))
      audio_caps |= AO_CAP_MODE_MONO;

    /* find best mode */
    for (i = 0; i < 8 * 7; i += 7) {
      for (j = 1; j < 7; j++) {
        if (audio_caps & modes[wishlist[i + j]]) {
          this->a52_flags_map[wishlist[i]] = modes[wishlist[i + j] + 1];
          this->ao_flags_map[wishlist[i]] = modes[wishlist[i + j]];
          break;
        }
      }
    }
    /* Always request extra bass. Liba52 will discard it if we dont. */
    /* Instead, downmix it manually if present and audio out does not support it. */
    for (; i < 16 * 7; i += 7) {
      for (j = 1; j < 7; j++) {
        if (audio_caps & modes[wishlist[i + j]]) {
          this->a52_flags_map_lfe[wishlist[i]] = modes[wishlist[i + j] + 1] | A52_LFE;
          this->ao_flags_map_lfe[wishlist[i]] = modes[wishlist[i + j]];
          break;
        }
      }
    }

    /* downmix to analogue dematrix? */
    if (this->class->enable_surround_downmix) {
      for (i = 0; i < 11; i++) {
        if (this->a52_flags_map[i] == A52_STEREO)
          this->a52_flags_map[i] = A52_DOLBY;
        if (this->a52_flags_map_lfe[i] == (A52_STEREO | A52_LFE))
          this->a52_flags_map_lfe[i] = A52_DOLBY | A52_LFE;
      }
    }

    if (this->ao_flags_map[A52_STEREO] == AO_CAP_MODE_MONO) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG, _("HELP! a mono-only audio driver?!\n"));
    }
  }

  /*
    for (i = 0; i<8; i++)
    this->a52_flags_map[i] |= A52_ADJUST_LEVEL;
  */
#ifdef DEBUG_A52
  a52file = xine_create_cloexec("test.a52", O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
#endif
  return &this->audio_decoder;
}

static void dispose_class (audio_decoder_class_t *this_gen) {

  a52dec_class_t  *this = (a52dec_class_t *)this_gen;
  config_values_t *config = this->config;

  config->unregister_callbacks (config, NULL, NULL, this, sizeof (*this));

  free (this);
}

static void lfe_level_change_cb (void *this_gen, xine_cfg_entry_t *entry) {
  a52dec_class_t *this = (a52dec_class_t *)this_gen;
#ifdef LIBA52_FIXED
  this->lfe_level_1 = ((1 << 26) * 0.7 / 100.0) * entry->num_value;
  this->lfe_level_2 = ((1 << 26) * 0.5 / 100.0) * entry->num_value;
#else
  this->lfe_level_1 = (0.7 / 100.0) * entry->num_value;
  this->lfe_level_2 = (0.5 / 100.0) * entry->num_value;
#endif
}

static void a52_level_change_cb (void *this_gen, xine_cfg_entry_t *entry) {
  a52dec_class_t *this = (a52dec_class_t *)this_gen;
#ifdef LIBA52_FIXED
  this->a52_level = ((1 << 26) / 100.0) * entry->num_value;
#else
  this->a52_level = (1.0 / 100.0) * entry->num_value;
#endif
}

static void dynrng_compress_change_cb (void *this_gen, xine_cfg_entry_t *entry) {
  ((a52dec_class_t *)this_gen)->disable_dynrng_compress = !entry->num_value;
}

static void surround_downmix_change_cb (void *this_gen, xine_cfg_entry_t *entry) {
  ((a52dec_class_t *)this_gen)->enable_surround_downmix = entry->num_value;
}

static void *init_plugin (xine_t *xine, const void *data) {

  a52dec_class_t *this;
  config_values_t *cfg;

  (void)data;
  this = calloc(1, sizeof (a52dec_class_t));
  if (!this)
    return NULL;

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "a/52dec";
  this->decoder_class.description     = N_("liba52 based a52 audio decoder plugin");
  this->decoder_class.dispose         = dispose_class;

  cfg = this->config = xine->config;

  {
    int v = cfg->register_range (cfg, "audio.a52.level",
      100, 0, 200,
      _("A/52 volume"),
      _("With A/52 audio, you can modify the volume at the decoder level. This has the advantage "
        "of the audio being already decoded for the specified volume, so later operations like "
        "channel downmixing will work on an audio stream of the given volume."),
      10, a52_level_change_cb, this);
#ifdef LIBA52_FIXED
      this->a52_level = ((1 << 26) / 100.0) * v;
#else
      this->a52_level = (1.0 / 100.0) * v;
#endif
  }

  this->disable_dynrng_compress = !cfg->register_bool (cfg, "audio.a52.dynamic_range",
    0,
    _("use A/52 dynamic range compression"),
    _("Dynamic range compression limits the dynamic range of the audio. This means making the loud "
      "sounds softer, and the soft sounds louder, so you can more easily listen to the audio in a noisy "
      "environment without disturbing anyone."),
    0, dynrng_compress_change_cb, this);

  this->enable_surround_downmix = cfg->register_bool (cfg, "audio.a52.surround_downmix",
    0,
    _("downmix audio to 2 channel surround stereo"),
    _("When you want to listen to multichannel surround sound, but you have only two speakers or a "
      "surround decoder or amplifier which does some sort of matrix surround decoding like prologic, "
      "you should enable this option so that the additional channels are mixed into the stereo signal."),
    0, surround_downmix_change_cb, this);

  {
    int v = cfg->register_range (cfg, "audio.a52.lfe_level",
      100, 0, 200,
      _("A/52 bass downmix volume"),
      _("Use this volume to mix in the bass effect,\n"
        "if you have large stereo speakers\n"
        "or an analogue subwoofer."),
      10, lfe_level_change_cb, this);
#ifdef LIBA52_FIXED
      this->lfe_level_1 = ((1 << 26) * 0.7 / 100.0) * v;
      this->lfe_level_2 = ((1 << 26) * 0.5 / 100.0) * v;
#else
      this->lfe_level_1 = (0.7 / 100.0) * v;
      this->lfe_level_2 = (0.5 / 100.0) * v;
#endif
  }

  lprintf ("init_plugin called\n");
  return this;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_A52,
  BUF_AUDIO_DNET,
  0
 };

static const decoder_info_t dec_info_audio = {
  .supported_types = audio_types,
  .priority        = 8,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_DECODER | PLUGIN_MUST_PRELOAD, 16, "a/52", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};

