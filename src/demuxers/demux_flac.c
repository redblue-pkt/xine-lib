/*
 * Copyright (C) 2000-2022 the xine project
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
 */

/*
 * FLAC File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information on the FLAC file format, visit:
 *   http://flac.sourceforge.net/
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#define LOG_MODULE "demux_flac"
#define LOG_VERBOSE
/*
#define LOG
*/

#define USE_FRAME_BUF

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/demux.h>
#include "bswap.h"
#include "group_audio.h"

#include "id3.h"
#include "flacutils.h"

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  int                  sample_rate;
  int                  bits_per_sample;
  int                  channels;
  int64_t              total_samples;
  off_t                data_start;
  off_t                data_size;

  flac_seekpoint_t    *seekpoints;
  int                  seekpoint_count;

#ifdef USE_FRAME_BUF
  uint8_t              frame_head_crc_tab[256];
  uint8_t              frame_head[16];
  uint8_t             *frame_buf;
  uint32_t             frame_buf_used;
  uint32_t             frame_buf_size;
  off_t                frame_buf_filepos;
  struct {
    off_t              filepos;
    uint32_t           rate;
    uint32_t           bits;
    uint32_t           channels;
    uint32_t           vbs;
    uint32_t           hsize;
    uint32_t           bsize;
    uint32_t           num;
    uint32_t           max_size;
    uint32_t           buf_pos;
  }                    frame1, frame2;
  int64_t              last_pts;
  int                  seek_flag;
  int                  read_errs;
#endif

  unsigned char        streaminfo[sizeof(xine_waveformatex) + FLAC_STREAMINFO_SIZE];
} demux_flac_t;

#ifdef USE_FRAME_BUF
static const uint32_t flac_sample_rates[16] = {
      0, 88200, 176400, 192000,
   8000, 16000,  22050,  24000,
  32000, 44100,  48000,  96000,
      1,     2,      3,      0
};

static const uint32_t flac_blocksizes[16] = {
         0,      192,      576,   2 * 576,
   4 * 576,  8 * 576,        1,         2,
       256,  2 * 256,  4 * 256,   8 * 256,
  16 * 256, 32 * 256, 64 * 256, 128 * 256
};

static const uint8_t flac_sample_sizes[8] = {
   0,  8, 12, 0,
  16, 20, 24, 0
};

static const uint8_t flac_channels[16] = {
  1, /* mono */
  2, /* stereo */
  3, /* surround */
  4, /* quadrophonic */
  5, /* 5.0 */
  6, /* 5.1 */
  7, /* 6.1 */
  8, /* 7.1 */
  2, /* left + side */
  2, /* right + side */
  2, /* mid + side */
  0,
  0,
  0,
  0,
  0
};

static void flac_init_frame_head (demux_flac_t *flac) {
  uint32_t i, j, v;
  for (i = 0; i < 256; i++) {
    v = i << 24;
    for (j = 0; j < 8; j++)
      v = (v << 1) ^ (0x07000000 & (((int32_t)v) >> 31));
    flac->frame_head_crc_tab[i] = v >> 24;
  }
}

static void flac_reset_head (demux_flac_t *flac) {
  flac->frame1.buf_pos = 0;
  flac->frame2.buf_pos = 0;
  flac->frame1.hsize   = 0;
  flac->frame2.hsize   = 0;
  flac->frame_buf_used = 0;
}

static uint32_t flac_test_frame_head (demux_flac_t *flac, uint32_t len) {
  uint8_t *p = flac->frame_head;
  uint32_t v = 0;
  while (len--)
    v = (v >> 8) ^ flac->frame_head_crc_tab[*p++ ^ (v & 0xff)];
  return v;
}

static int flac_parse_frame_head (demux_flac_t *flac) {
  const uint8_t *p = flac->frame_head;
  uint32_t v;
  /* sync word */
  if (p[0] != 0xff)
    return 1;
  if ((p[1] & 0xfe) != 0xf8)
    return 1;
  /* reserved */
  if (flac->frame_head[3] & 1)
    return 2;
  /* variable block size */
  flac->frame2.vbs = flac->frame_head[1] & 0x01;
  /* channels */
  v = flac_channels[flac->frame_head[3] >> 4];
  if (v == 0)
    return 2;
  flac->frame2.channels = v;
  /* bits */
  v = flac_sample_sizes[(flac->frame_head[3] >> 1) & 7];
  if (v == 0)
    return 2;
  flac->frame2.bits = v;
  /* frame num (fixed block size) or sample num (utf8) */
  p += 4;
  v = _X_BE_32 (p);
  if ((v & 0x80000000) == 0) {
    flac->frame2.num = v >> 24;
    p += 1;
  } else if ((v & 0xe0c00000) == 0xc0800000) {
    flac->frame2.num = ((v & 0x1f000000) >> 18)
                     | ((v & 0x003f0000) >> 16);
    p += 2;
  } else if ((v & 0xf0c0c000) == 0xe0808000) {
    flac->frame2.num = ((v & 0x0f000000) >> 12)
                     | ((v & 0x003f0000) >> 10)
                     | ((v & 0x00003f00) >>  8);
    p += 3;
  } else if ((v & 0xf8c0c0c0) == 0xf0808080) {
    flac->frame2.num = ((v & 0x07000000) >>  6)
                     | ((v & 0x003f0000) >>  4)
                     | ((v & 0x00003f00) >>  2)
                     |  (v & 0x0000003f);
    p += 4;
  } else if ((v & 0xfcc0c0c0) == 0xf8808080) {
    flac->frame2.num =  (v & 0x03000000)
                     | ((v & 0x003f0000) <<  2)
                     | ((v & 0x00003f00) <<  4)
                     | ((v & 0x0000003f) <<  6)
                     |  (p[4] & 0x3f);
    p += 5;
  } else if ((v & 0xfec0c0c0) == 0xfc808080) {
    flac->frame2.num = ((v & 0x01000000) <<  6)
                     | ((v & 0x003f0000) <<  8)
                     | ((v & 0x00003f00) << 10)
                     | ((v & 0x0000003f) << 12)
                     | ((p[4] & 0x3f)    <<  6)
                     |  (p[5] & 0x3f);
    p += 6;
  } else {
    return 2;
  }
  /* block size */
  v = flac_blocksizes[flac->frame_head[2] >> 4];
  if (v < 4) {
    switch (v) {
      case 1:
        v = *p++ + 1;
        break;
      case 2:
        v = _X_BE_16 (p) + 1; p += 2;
        break;
      default:
        return 2;
    }
  }
  flac->frame2.bsize = v;
  /* sample rate */
  v = flac_sample_rates[flac->frame_head[2] & 0x0f];
  if (v < 4) {
    switch (v) {
      case 1:
        v = *p++ * 1000;
        break;
      case 2:
        v = _X_BE_16 (p); p += 2;
        break;
      case 3:
        v = _X_BE_16 (p) * 10; p += 2;
        break;
      default:
        return 2;
    }
  }
  flac->frame2.rate = v;
  /* crc test */
  p++;
  flac->frame2.hsize = p - flac->frame_head;
  if (flac_test_frame_head (flac, flac->frame2.hsize) != 0)
    return 2;
  /* flac shall make things smaller than the uncompressed size. */
  /* frame head + frame crc + channel heads */
  v = 18 + flac->frame2.channels * (((flac->frame2.bits + 7) >> 3) + 1);
  /* uncompressed samples */
  if (flac->frame2.channels == 2)
    v += ((2 * flac->frame2.bits + 1) * flac->frame2.bsize + 7) >> 3;
  else
    v += (flac->frame2.channels * flac->frame2.bits * flac->frame2.bsize + 7) >> 3;
  flac->frame2.max_size = v;
  return 0;
}

static int flac_get_frame (demux_flac_t *flac) {
  int r = -1;
  uint32_t v;
  uint8_t *p, *e;

  flac->frame1 = flac->frame2;
  p = flac->frame_buf + flac->frame2.buf_pos + flac->frame2.hsize;
  e = flac->frame_buf + flac->frame_buf_used;
  memcpy (e, "\xff\xf8\x00\x00", 4);
  v = _X_BE_32 (p); p += 4;

  while (1) {
    uint32_t l;
    int32_t  s;
    while ((v & 0xfffe0001) != 0xfff80000)
      v = (v << 8) | *p++;
    if (p + sizeof (flac->frame_head) - 4 <= e) {
      flac->frame2.buf_pos = p - 4 - flac->frame_buf;
      flac->frame2.filepos = flac->frame_buf_filepos + flac->frame2.buf_pos;
      memcpy (flac->frame_head, flac->frame_buf + flac->frame2.buf_pos, sizeof (flac->frame_head));
      r = flac_parse_frame_head (flac);
      if (r == 0)
        break;
      v = (v << 8) | *p++;
      continue;
    }
    if (flac->frame1.bits == 0) {
      p = flac->frame_buf + 4;
      flac->frame1.buf_pos = 0;
      flac->frame_buf_used = 0;
    } else if (flac->frame1.buf_pos) {
      l = flac->frame_buf_used - flac->frame1.buf_pos;
      if (l) {
        if (l <= flac->frame1.buf_pos)
          memcpy (flac->frame_buf, flac->frame_buf + flac->frame1.buf_pos, l);
        else
          memmove (flac->frame_buf, flac->frame_buf + flac->frame1.buf_pos, l);
      }
      p -= flac->frame1.buf_pos;
      flac->frame1.buf_pos = 0;
      flac->frame_buf_used = l;
    }
    flac->frame2.buf_pos = p - 4 - flac->frame_buf;
    flac->frame2.hsize = 0;
    l = flac->frame_buf_size - flac->frame_buf_used;
    if (!l)
      break;
    flac->frame_buf_filepos = flac->input->get_current_pos (flac->input) - flac->frame_buf_used;
    s = flac->input->read (flac->input, flac->frame_buf + flac->frame_buf_used, l);
    if (s <= 0)
      break;
    flac->frame_buf_used += s;
    e = flac->frame_buf + flac->frame_buf_used;
    memcpy (e, "\xff\xf8\x00\x00", 4);
    v = _X_BE_32 (p - 4);
  }

  /* enlarge buf? */
  if ((r == 0) && (flac->frame2.max_size > flac->frame_buf_size)) {
    uint32_t need = 3 * flac->frame2.max_size / 2;
    uint8_t *n = realloc (flac->frame_buf, need + 16);
    if (n) {
      flac->frame_buf = n;
      flac->frame_buf_size = need;
      xprintf (flac->stream->xine, XINE_VERBOSITY_DEBUG,
        "demux_flac: frame buffer enlarged to %u bytes.\n", (unsigned int)need);
    }
  }

  return flac->frame2.buf_pos - flac->frame1.buf_pos;
}
#endif

/* Open a flac file
 * This function is called from the _open() function of this demuxer.
 * It returns 1 if flac file was opened successfully. */
static int open_flac_file(demux_flac_t *flac) {

  uint32_t signature;
  unsigned char preamble[10];
  unsigned int block_length;
  unsigned char buffer[FLAC_SEEKPOINT_SIZE];
  unsigned char *streaminfo = flac->streaminfo + sizeof(xine_waveformatex);
  int id3v2_tag_size, i;

  /* Unfortunately some FLAC files have an ID3 flag prefixed on them
   * before the actual FLAC headers... these are barely legal, but
   * users use them and want them working, so check and skip the ID3
   * tag if present.
   */
  id3v2_tag_size = xine_parse_id3v2_tag (flac->stream, flac->input);
  /* fetch the file signature, 4 bytes will read both the fLaC
   * signature and the */
  if (_x_demux_read_stream_header (flac->stream, flac->input, &signature, 4) != 4)
    return 0;

  flac->input->seek (flac->input, id3v2_tag_size + 4, SEEK_SET);

  /* validate signature */
  if ( signature != ME_FOURCC('f', 'L', 'a', 'C') )
      return 0;

  /* loop through the metadata blocks; use a do-while construct since there
   * will always be 1 metadata block */
  do {

    if (flac->input->read(flac->input, preamble, FLAC_SIGNATURE_SIZE) !=
        FLAC_SIGNATURE_SIZE)
      return 0;

    block_length = (preamble[1] << 16) |
                   (preamble[2] <<  8) |
                   (preamble[3] <<  0);

    switch (preamble[0] & 0x7F) {

    /* STREAMINFO */
    case 0:
      lprintf ("STREAMINFO metadata\n");
      if (block_length != FLAC_STREAMINFO_SIZE) {
        lprintf ("expected STREAMINFO chunk of %d bytes\n",
          FLAC_STREAMINFO_SIZE);
        return 0;
      }
      if (flac->input->read(flac->input,
        flac->streaminfo + sizeof(xine_waveformatex),
        FLAC_STREAMINFO_SIZE) != FLAC_STREAMINFO_SIZE)
        return 0;
      flac->sample_rate = _X_BE_32(&streaminfo[10]);
      flac->channels = ((flac->sample_rate >> 9) & 0x07) + 1;
      flac->bits_per_sample = ((flac->sample_rate >> 4) & 0x1F) + 1;
      flac->sample_rate >>= 12;
      flac->total_samples = _X_BE_64(&streaminfo[10]) & UINT64_C(0x0FFFFFFFFF);  /* 36 bits */
      lprintf ("%d Hz, %d bits, %d channels, %"PRId64" total samples\n",
        flac->sample_rate, flac->bits_per_sample,
        flac->channels, flac->total_samples);
      break;

    /* PADDING */
    case 1:
      lprintf ("PADDING metadata\n");
      flac->input->seek(flac->input, block_length, SEEK_CUR);
      break;

    /* APPLICATION */
    case 2:
      lprintf ("APPLICATION metadata\n");
      flac->input->seek(flac->input, block_length, SEEK_CUR);
      break;

    /* SEEKTABLE */
    case 3:
      lprintf ("SEEKTABLE metadata, %d bytes\n", block_length);
      if (!flac->sample_rate)
        break;
      flac->seekpoint_count = block_length / FLAC_SEEKPOINT_SIZE;
      if (!flac->seekpoint_count)
        break;
      flac->seekpoints = calloc(flac->seekpoint_count, sizeof(flac_seekpoint_t));
      if (!flac->seekpoints)
        return 0;
      for (i = 0; i < flac->seekpoint_count; i++) {
        if (flac->input->read(flac->input, buffer, FLAC_SEEKPOINT_SIZE) != FLAC_SEEKPOINT_SIZE)
          return 0;
        flac->seekpoints[i].sample_number = _X_BE_64(&buffer[0]);
        lprintf (" %d: sample %"PRId64", ", i, flac->seekpoints[i].sample_number);
        flac->seekpoints[i].offset = _X_BE_64(&buffer[8]);
        flac->seekpoints[i].size = _X_BE_16(&buffer[16]);
        lprintf ("@ 0x%"PRIX64", size = %d bytes, ",
          flac->seekpoints[i].offset, flac->seekpoints[i].size);
        flac->seekpoints[i].pts = flac->seekpoints[i].sample_number;
        flac->seekpoints[i].pts *= 90000;
        flac->seekpoints[i].pts /= flac->sample_rate;
        lprintf ("pts = %"PRId64"\n", flac->seekpoints[i].pts);
      }
      break;

    /* VORBIS_COMMENT
     *
     * For a description of the format please have a look at
     * http://www.xiph.org/vorbis/doc/v-comment.html */
    case 4:
      lprintf ("VORBIS_COMMENT metadata\n");
      {
        char *comments;
        uint32_t length, user_comment_list_length, cn;
        char *comment;
        char c;

        if (block_length < 8)
          break;

        comments = malloc(block_length + 1); /* last byte for NUL termination */
        if (!comments)
          break;

        if (flac->input->read(flac->input, comments, block_length) == block_length) {
          char *ptr = comments;
          int tracknumber = -1;
          int tracktotal = -1;

          length = _X_LE_32(ptr);
          ptr += 4 + length;
          if (length > block_length - 8) {
            free(comments);
            return 0; /* bad length or too little left in the buffer */
          }

          user_comment_list_length = _X_LE_32(ptr);
          ptr += 4;

          cn = 0;
          for (; cn < user_comment_list_length; cn++) {
            if (ptr > comments + block_length - 4) {
              free(comments);
              return 0; /* too little left in the buffer */
            }

            length = _X_LE_32(ptr);
            ptr += 4;
            if (length >= block_length || ptr + length > comments + block_length) {
              free(comments);
              return 0; /* bad length */
            }

            comment = (char*) ptr;
            c = comment[length];
            comment[length] = 0; /* NUL termination */

            lprintf ("comment[%02d] = %s\n", cn, comment);

            if ((strncasecmp ("TITLE=", comment, 6) == 0)
                && (length - 6 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_TITLE, comment + 6);
            } else if ((strncasecmp ("ARTIST=", comment, 7) == 0)
                && (length - 7 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_ARTIST, comment + 7);
            } else if ((strncasecmp ("COMPOSER=", comment, 9) == 0)
                && (length - 9 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_COMPOSER, comment + 9);
            } else if ((strncasecmp ("ALBUM=", comment, 6) == 0)
                && (length - 6 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_ALBUM, comment + 6);
            } else if ((strncasecmp ("DATE=", comment, 5) == 0)
                && (length - 5 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_YEAR, comment + 5);
            } else if ((strncasecmp ("GENRE=", comment, 6) == 0)
                && (length - 6 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_GENRE, comment + 6);
            } else if ((strncasecmp ("Comment=", comment, 8) == 0)
                && (length - 8 > 0)) {
              _x_meta_info_set_utf8 (flac->stream, XINE_META_INFO_COMMENT, comment + 8);
            } else if ((strncasecmp ("TRACKNUMBER=", comment, 12) == 0)
                && (length - 12 > 0)) {
              tracknumber = atoi (comment + 12);
            } else if ((strncasecmp ("TRACKTOTAL=", comment, 11) == 0)
                && (length - 11 > 0)) {
              tracktotal = atoi (comment + 11);
            }
            comment[length] = c;

            ptr += length;
          }

          if ((tracknumber > 0) && (tracktotal > 0)) {
            char tn[24];
            snprintf (tn, 24, "%02d/%02d", tracknumber, tracktotal);
            _x_meta_info_set(flac->stream, XINE_META_INFO_TRACK_NUMBER, tn);
          }
          else if (tracknumber > 0) {
            char tn[16];
            snprintf (tn, 16, "%02d", tracknumber);
            _x_meta_info_set(flac->stream, XINE_META_INFO_TRACK_NUMBER, tn);
          }
        }
        free(comments);
      }
      break;

    /* CUESHEET */
    case 5:
      lprintf ("CUESHEET metadata\n");
      flac->input->seek(flac->input, block_length, SEEK_CUR);
      break;

    /* 6-127 are presently reserved */
    default:
      lprintf ("unknown metadata chunk: %d\n", preamble[0] & 0x7F);
      flac->input->seek(flac->input, block_length, SEEK_CUR);
      break;

    }

  } while ((preamble[0] & 0x80) == 0);

  /* do not bail out yet, maybe decoder can handle this
  if (!flac->sample_rate)
    return 0;
  */

  flac->data_start = flac->input->get_current_pos(flac->input);
  flac->data_size = flac->input->get_length(flac->input) - flac->data_start;

  /* now at the beginning of the audio, adjust the seekpoint offsets */
  for (i = 0; i < flac->seekpoint_count; i++) {
    flac->seekpoints[i].offset += flac->data_start;
  }

  return 1;
}

static int demux_flac_send_chunk(demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;
  buf_element_t *buf = NULL;
#ifdef USE_FRAME_BUF
  int l;
#else
  int64_t input_time_guess;
#endif

#ifdef USE_FRAME_BUF
  l = flac_get_frame (this);
  if (l) {
    uint32_t flags, normpos = 0;
    uint8_t *p;
    int64_t  pts;
    int      itime;

    if (this->data_size > 0)
      normpos = (double)(this->frame1.filepos - this->data_start) * 65535 / this->data_size;

    if (this->frame1.rate) {
      if (this->frame1.vbs)
        pts = (double)this->frame1.num * 90000 / this->frame1.rate;
      else
        pts = (double)this->frame1.num * this->frame1.bsize * 90000 / this->frame1.rate;
      itime = pts / 90;
    } else {
      pts = 0;
      itime = 0;
    }

    if (this->seek_flag) {
      _x_demux_control_newpts (this->stream, pts, BUF_FLAG_SEEK);
      this->seek_flag = 0;
    } else {
      int64_t diff = pts - this->last_pts;
      if (diff < 0)
        diff = -diff;
      if (diff > 220000)
        _x_demux_control_newpts (this->stream, pts, 0);
    }
    this->last_pts = pts;

    this->read_errs = 3;
    flags = BUF_FLAG_FRAME_START;
    p = this->frame_buf + this->frame1.buf_pos;
    while (l) {
      buf = this->audio_fifo->buffer_pool_size_alloc (this->audio_fifo, l);
      buf->type = BUF_AUDIO_FLAC;
      buf->size = l > buf->max_size ? buf->max_size : l;
      memcpy (buf->content, p, buf->size);
      p += buf->size;
      l -= buf->size;
      if (!l)
        flags |= BUF_FLAG_FRAME_END;
      buf->decoder_flags |= flags;
      flags &= ~BUF_FLAG_FRAME_START;
      buf->extra_info->input_normpos = normpos;
      buf->pts = pts;
      buf->extra_info->input_time = itime;
      pts = 0;
      itime = 0;
      this->audio_fifo->put (this->audio_fifo, buf);
    }
  } else if (!this->seek_flag) {
    if (--this->read_errs < 0)
      this->status = DEMUX_FINISHED;
  }
#else
  /* just send a buffer-sized chunk; let the decoder figure out the
   * boundaries and let the engine figure out the pts */
  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_FLAC;
  if( this->data_size )
    buf->extra_info->input_normpos = (int) ( (double) (this->input->get_current_pos(this->input) -
                                     this->data_start) * 65535 / this->data_size );
  buf->pts = 0;
  buf->size = buf->max_size;

  /*
   * Estimate the input_time field based on file position:
   *
   *   current_pos     input time
   *   -----------  =  ----------
   *    total_pos      total time
   *
   *  total time = total samples / sample rate * 1000
   */

  /* do this one step at a time to make sure all the numbers stay safe */
  if (this->sample_rate) {
    input_time_guess = this->total_samples;
    input_time_guess /= this->sample_rate;
    input_time_guess *= 1000;
    input_time_guess *= buf->extra_info->input_normpos;
    input_time_guess /= 65535;
    buf->extra_info->input_time = input_time_guess;
  }

  if (this->input->read(this->input, buf->content, buf->size) !=
    buf->size) {
    buf->free_buffer(buf);
    this->status = DEMUX_FINISHED;
    return this->status;
  }
  buf->decoder_flags |= BUF_FLAG_FRAME_END;
  this->audio_fifo->put(this->audio_fifo, buf);
#endif
  return this->status;
}

static void demux_flac_send_headers(demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;
  buf_element_t *buf;
  xine_waveformatex wave;
  int bits;

  memset(&wave, 0, sizeof(wave));

  this->audio_fifo  = this->stream->audio_fifo;

  /* send start buffers */
  _x_demux_control_start(this->stream);

  if ( ! this->audio_fifo )
  {
    this->status = DEMUX_FINISHED;
    return;
  }

  /* lie about 24bps */
  bits = this->bits_per_sample > 16 ? 16 : this->bits_per_sample;

  buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
  buf->type = BUF_AUDIO_FLAC;
  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_STDHEADER|BUF_FLAG_FRAME_END;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = this->sample_rate;
  buf->decoder_info[2] = bits;
  buf->decoder_info[3] = this->channels;
  /* copy the faux WAV header */
  buf->size = sizeof(xine_waveformatex) + FLAC_STREAMINFO_SIZE;
  memcpy(buf->content, this->streaminfo, buf->size);
  /* forge a WAV header with the proper length */
  wave.cbSize = FLAC_STREAMINFO_SIZE;
  memcpy(buf->content, &wave, sizeof(xine_waveformatex));
  this->audio_fifo->put (this->audio_fifo, buf);

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
                       this->channels);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
                       this->sample_rate);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
                       bits);

  this->status = DEMUX_OK;
}

static int demux_flac_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time, int playing) {
  demux_flac_t *this = (demux_flac_t *) this_gen;
  int seekpoint_index = 0;
  int64_t start_pts;
#ifndef USE_FRAME_BUF
  unsigned char buf[4];
#endif

  start_pos = (off_t) ( (double) start_pos / 65535 *
              this->data_size );

  /* if thread is not running, initialize demuxer */
  if( !playing && !start_pos) {

    /* send new pts */
    _x_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
  } else {

    if (this->seekpoints == NULL && !start_pos) {
      /* cannot seek if there is no seekpoints */
      this->status = DEMUX_OK;
      return this->status;
    }

    /* Don't use seekpoints if start_pos != 0. This allows smooth seeking */
    if (start_pos) {
      /* offset-based seek */
      this->status = DEMUX_OK;
      start_pos += this->data_start;
      this->input->seek(this->input, start_pos, SEEK_SET);
#ifdef USE_FRAME_BUF
      flac_reset_head (this);
      this->seek_flag = 1;
#else
      while(1){ /* here we try to find something that resembles a frame header */

	if (this->input->read(this->input, buf, 2) != 2){
	  this->status = DEMUX_FINISHED; /* we sought past the end of stream ? */
	  break;
	}

	if (buf[0] == 0xff && buf[1] == 0xf8)
	  break; /* this might be the frame header... or it may be not. We pass it to the decoder
		  * to decide, but this way we reduce the number of warnings */
	start_pos +=2;
      }
#endif
      _x_demux_flush_engine(this->stream);
      this->input->seek(this->input, start_pos, SEEK_SET);
      _x_demux_control_newpts(this->stream, 0, BUF_FLAG_SEEK);
      return this->status;

    } else {
#ifdef USE_FRAME_BUF
      flac_reset_head (this);
      this->seek_flag = 1;
#endif
      /* do a lazy, linear seek based on the assumption that there are not
       * that many seek points; time-based seek */
      start_pts = start_time;
      start_pts *= 90;
      if (start_pts < this->seekpoints[0].pts)
        seekpoint_index = 0;
      else {
        for (seekpoint_index = 0; seekpoint_index < this->seekpoint_count - 1;
          seekpoint_index++) {
          if (start_pts < this->seekpoints[seekpoint_index + 1].pts) {
            break;
          }
        }
      }
    }

    _x_demux_flush_engine(this->stream);
    this->input->seek(this->input, this->seekpoints[seekpoint_index].offset,
      SEEK_SET);
    _x_demux_control_newpts(this->stream,
      this->seekpoints[seekpoint_index].pts, BUF_FLAG_SEEK);
  }

  return this->status;
}

static void demux_flac_dispose (demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;

  free(this->seekpoints);
#ifdef USE_FRAME_BUF
  free (this->frame_buf);
#endif
  free(this);
}

static int demux_flac_get_status (demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;

  return this->status;
}

static int demux_flac_get_stream_length (demux_plugin_t *this_gen) {
  demux_flac_t *this = (demux_flac_t *) this_gen;
  int64_t length = this->total_samples;

  if (!this->sample_rate)
    return 0;

  length *= 1000;
  length /= this->sample_rate;

  return length;
}

static uint32_t demux_flac_get_capabilities(demux_plugin_t *this_gen) {
  (void)this_gen;
  return DEMUX_CAP_NOCAP;
}

static int demux_flac_get_optional_data(demux_plugin_t *this_gen,
                                        void *data, int data_type) {
  (void)this_gen;
  (void)data;
  (void)data_type;
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_flac_t    *this;

  /* this should change eventually... */
  if (!INPUT_IS_SEEKABLE(input)) {
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input not seekable, can not handle!\n");
    return NULL;
  }

  this = calloc(1, sizeof(demux_flac_t));
  if (!this)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
#  ifdef USE_FRAME_BUF
  this->frame_buf_filepos = 0;
  this->frame_buf_used    = 0;
  this->frame2.filepos    = 0;
  this->frame2.rate       = 0;
  this->frame2.bits       = 0;
  this->frame2.channels   = 0;
  this->frame2.vbs        = 0;
  this->frame2.bsize      = 0;
  this->frame2.num        = 0;
  this->frame2.max_size   = 0;
  this->frame2.buf_pos    = 0;
  this->last_pts          = 0;
  this->seek_flag         = 0;
#  endif
  this->seekpoints        = NULL;
#endif

#ifdef USE_FRAME_BUF
  this->frame_buf_size = 8 << 10;
  this->frame_buf      = malloc (this->frame_buf_size + 16);
  if (!this->frame_buf) {
    free (this);
    return NULL;
  }
  flac_init_frame_head (this);
  this->read_errs = 3;
#endif

  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_flac_send_headers;
  this->demux_plugin.send_chunk        = demux_flac_send_chunk;
  this->demux_plugin.seek              = demux_flac_seek;
  this->demux_plugin.dispose           = demux_flac_dispose;
  this->demux_plugin.get_status        = demux_flac_get_status;
  this->demux_plugin.get_stream_length = demux_flac_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_flac_get_capabilities;
  this->demux_plugin.get_optional_data = demux_flac_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_MRL:
  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_flac_file(this)) {
      demux_flac_dispose (&this->demux_plugin);
      return NULL;
    }

  break;

  default:
#ifdef USE_FRAME_BUF
    free (this->frame_buf);
#endif
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

void *demux_flac_init_plugin (xine_t *xine, const void *data) {

  (void)xine;
  (void)data;

  static const demux_class_t demux_flac_class = {
    .open_plugin     = open_plugin,
    .description     = N_("Free Lossless Audio Codec (flac) demux plugin"),
    .identifier      = "FLAC",
    .mimetypes       =
      "audio/x-flac: flac: FLAC Audio;"
      "audio/flac: flac: FLAC Audio;",
    .extensions      = "flac",
    .dispose         = NULL,
  };

  return (void *)&demux_flac_class;
}


