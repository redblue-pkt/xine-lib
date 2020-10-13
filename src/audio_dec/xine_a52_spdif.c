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

#define LOG_MODULE "a52_spdif"
#define LOG_VERBOSE
/*
#define LOG
#define LOG_PTS
*/

#include <xine/xine_internal.h>
#include <xine/audio_out.h>

#include <xine/buffer.h>
#include <xine/xineutils.h>

#define A52_MONO 1
#define A52_STEREO 2
#define A52_3F 3
#define A52_2F1R 4
#define A52_3F1R 5
#define A52_2F2R 6
#define A52_3F2R 7
#define A52_DOLBY 10
#define A52_CHANNEL_MASK 15
#define A52_LFE 16

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

static int a52_syncinfo (uint8_t *buf, int *flags, int *sample_rate, int *bit_rate) {

  static const uint16_t rate[] = { 32,  40,  48,  56,  64,  80,  96, 112,
                                   128, 160, 192, 224, 256, 320, 384, 448,
                                   512, 576, 640};
  static const uint8_t lfeon[8] = {0x10, 0x10, 0x04, 0x04, 0x04, 0x01, 0x04, 0x01};
  static const uint8_t halfrate[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3};
  int frmsizecod;
  int bitrate;
  int half;
  int acmod;

  if ((buf[0] != 0x0b) || (buf[1] != 0x77))   /* syncword */
    return 0;

  if (buf[5] >= 0x60)         /* bsid >= 12 */
    return 0;
  half = halfrate[buf[5] >> 3];

  /* acmod, dsurmod and lfeon */
  acmod = buf[6] >> 5;
  *flags = ((((buf[6] & 0xf8) == 0x50) ? A52_DOLBY : acmod) |
            ((buf[6] & lfeon[acmod]) ? A52_LFE : 0));

  frmsizecod = buf[4] & 63;
  if (frmsizecod >= 38)
    return 0;
  bitrate = rate [frmsizecod >> 1];
  *bit_rate = (bitrate * 1000) >> half;

  switch (buf[4] & 0xc0) {
    case 0:
      *sample_rate = 48000 >> half;
      return 4 * bitrate;
    case 0x40:
      *sample_rate = 44100 >> half;
      return 2 * (320 * bitrate / 147 + (frmsizecod & 1));
    case 0x80:
      *sample_rate = 32000 >> half;
      return 6 * bitrate;
    default:
      return 0;
  }
}

#include "xine_a52_parser.h"

typedef struct a52dec_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t   *stream;

  int64_t          pts;
  int              output_open;

  xine_a52_parser_t parser;

} a52dec_decoder_t;

static void a52dec_reset (audio_decoder_t *this_gen) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen;

  xine_a52_parser_reset(&this->parser);
  this->pts               = 0;
}

static void a52dec_discontinuity (audio_decoder_t *this_gen) {

  a52dec_reset(this_gen);
}

static void a52dec_decode_frame (a52dec_decoder_t *this, int64_t pts, int preview_mode) {

#ifdef LOG_PTS
  printf("a52dec:decode_frame:pts=%lld\n",pts);
#endif

  /*
   * loop through a52 data
   */

  if (!this->output_open) {
    this->output_open = (this->stream->audio_out->open) (this->stream->audio_out,
                                                         this->stream, 16,
                                                         this->parser.a52_sample_rate,
                                                         AO_CAP_MODE_A52);
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
    bsmod = data_in[5] & 0x7;         /* bsmod, stream = 0 */
    frame_size = frmsizecod_tbl[frmsizecod].frm_size[fscod] ;

    data_out[0] = 0x72; data_out[1] = 0xf8;   /* spdif syncword    */
    data_out[2] = 0x1f; data_out[3] = 0x4e;   /* ..............    */
    data_out[4] = 0x01;                       /* AC3 data          */
    data_out[5] = bsmod;                      /* bsmod, stream = 0 */
    data_out[6] = (frame_size << 4) & 0xff;   /* frame_size * 16   */
    data_out[7] = ((frame_size ) >> 4) & 0xff;
    swab(data_in, &data_out[8], frame_size * 2 );

    buf->num_frames = 1536;
    buf->vpts       = pts;

    this->stream->audio_out->put_buffer (this->stream->audio_out, buf, this->stream);
  }
}

static void a52dec_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen;

  if (buf->decoder_flags & BUF_FLAG_HEADER)
    return;

  /* swap byte pairs if this is RealAudio DNET data */
  if (buf->type == BUF_AUDIO_DNET) {
    do_swab(buf->content, buf->content + buf->size);
  }

  if (buf->pts) {
    this->pts = buf->pts;
  }

  while (buf->size > 0) {
    int consumed = xine_a52_parse_data(&this->parser, this->stream, buf->content, buf->size);
    buf->content += consumed;
    buf->size -= consumed;
    if (this->parser.got_frame) {
      a52dec_decode_frame (this, this->pts, buf->decoder_flags & BUF_FLAG_PREVIEW);
      this->pts = 0;
    }
  }
}

static void a52dec_dispose (audio_decoder_t *this_gen) {

  a52dec_decoder_t *this = (a52dec_decoder_t *) this_gen;

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);

  this->output_open = 0;

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

  a52dec_decoder_t *this;

  (void)class_gen;

  lprintf ("open_plugin called\n");

  /*
   * find out if this driver supports a52 output
   * if not, bail out early.
   */
  if (!(stream->audio_out->get_capabilities (stream->audio_out) & AO_CAP_MODE_A52))
    return (audio_decoder_t *)1;

  this = calloc(1, sizeof (a52dec_decoder_t));
  if (!this)
    return NULL;

  xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "a52: Using a52 bitstream output (spdif)\n");

  /* Do these first, when compiler still knows "this" is all zeroed.
   * Let it optimize away this on most systems where clear mem
   * interpretes as 0, 0f or NULL safely.
   */
  xine_a52_parser_reset(&this->parser);
  this->output_open       = 0;

  this->audio_decoder.decode_data   = a52dec_decode_data;
  this->audio_decoder.reset         = a52dec_reset;
  this->audio_decoder.discontinuity = a52dec_discontinuity;
  this->audio_decoder.dispose       = a52dec_dispose;

  this->stream = stream;

  return &this->audio_decoder;
}

static void *init_plugin (xine_t *xine, const void *data) {

  static const audio_decoder_class_t decoder_class = {
    .open_plugin     = open_plugin,
    .identifier      = "a/52dec_spdif",
    .description     = N_("liba52 based a52 audio decoder plugin for SPDIF output"),
    .dispose         = NULL,
  };
  (void)xine;
  (void)data;

  return (void *)&decoder_class;
}

static const uint32_t audio_types[] = {
  BUF_AUDIO_A52,
  BUF_AUDIO_DNET,
  0
};

static const decoder_info_t dec_info_audio = {
  .supported_types = audio_types,
  .priority        = 20,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_DECODER | PLUGIN_MUST_PRELOAD, 16, "a/52_spdif", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};

