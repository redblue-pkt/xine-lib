
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
 * a52 frame parser
 */

#ifndef _XINE_A52_PARSER_H
#define _XINE_A52_PARSER_H

#include <sys/types.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>

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

typedef struct {
  uint8_t          got_frame;
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

static size_t xine_a52_parse_data(xine_a52_parser_t *this, xine_stream_t *stream, const uint8_t *data, size_t size) {

  const uint8_t *const end = data + size;
  const uint8_t       *current = data;
  const uint8_t       *sync_start = current + 1;

  this->got_frame = 0;

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
          a52_meta_info_set(stream, this->a52_flags, this->a52_bit_rate, this->a52_sample_rate);
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

  return size;
}


#endif /* _XINE_A52_PARSER_H */
