/*
 * Copyright (C) 2004 the xine project
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
 */

/*
 * IFF File Demuxer by Manfred Tremmel (Manfred.Tremmel@iiv.de)
 * Based on the AIFF demuxer and the information of the Amiga Developer CD
 *
 * currently supported iff-formats:
 * * 8SVX (uncompressed, deltacompression fibonacci and exponential)
 *   + volume rescaling is supported
 *   + multiple channels using CHAN and PAN Chunks are supported
 *   - SEQN and FADE chunks are not supported at the moment
 *     (I do understand what to do, but I hate the work behind it ;-) )
 *   - the optional data chunks ATAK and RLSE are not supported at the moment
 *     (no examples found and description isn't as clear as it should)
 *
 * $Id: demux_iff.c,v 1.1 2004/01/03 19:59:00 manfredtremmel Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "buffer.h"
#include "bswap.h"

#define FOURCC_CHUNK BE_FOURCC
#define IFF_8SVX_CHUNK FOURCC_CHUNK('8', 'S', 'V', 'X')
#define IFF_ANFI_CHUNK FOURCC_CHUNK('A', 'N', 'F', 'I')
#define IFF_ANHD_CHUNK FOURCC_CHUNK('A', 'N', 'H', 'D')
#define IFF_ANIM_CHUNK FOURCC_CHUNK('A', 'N', 'I', 'M')
#define IFF_ANNO_CHUNK FOURCC_CHUNK('A', 'N', 'N', 'O')
#define IFF_ANSQ_CHUNK FOURCC_CHUNK('A', 'N', 'S', 'Q')
#define IFF_ATAK_CHUNK FOURCC_CHUNK('A', 'T', 'A', 'K')
#define IFF_AUTH_CHUNK FOURCC_CHUNK('A', 'U', 'T', 'H')
#define IFF_BMHD_CHUNK FOURCC_CHUNK('B', 'M', 'H', 'D')
#define IFF_BODY_CHUNK FOURCC_CHUNK('B', 'O', 'D', 'Y')
#define IFF_CAMG_CHUNK FOURCC_CHUNK('C', 'A', 'M', 'G')
#define IFF_CHAN_CHUNK FOURCC_CHUNK('C', 'H', 'A', 'N')
#define IFF_COPY_CHUNK FOURCC_CHUNK('(', 'c', ')', ' ')
#define IFF_CRNG_CHUNK FOURCC_CHUNK('C', 'R', 'N', 'G')
#define IFF_DLTA_CHUNK FOURCC_CHUNK('D', 'L', 'T', 'A')
#define IFF_DPAN_CHUNK FOURCC_CHUNK('D', 'P', 'A', 'N')
#define IFF_DPI_CHUNK  FOURCC_CHUNK('D', 'P', 'I', ' ')
#define IFF_DPPS_CHUNK FOURCC_CHUNK('D', 'P', 'P', 'S')
#define IFF_DPPV_CHUNK FOURCC_CHUNK('D', 'P', 'P', 'V')
#define IFF_DRNG_CHUNK FOURCC_CHUNK('D', 'R', 'N', 'G')
#define IFF_FACE_CHUNK FOURCC_CHUNK('F', 'A', 'C', 'E')
#define IFF_FADE_CHUNK FOURCC_CHUNK('F', 'A', 'D', 'E')
#define IFF_FORM_CHUNK FOURCC_CHUNK('F', 'O', 'R', 'M')
#define IFF_FVER_CHUNK FOURCC_CHUNK('F', 'V', 'E', 'R')
#define IFF_GRAB_CHUNK FOURCC_CHUNK('G', 'R', 'A', 'B')
#define IFF_ILBM_CHUNK FOURCC_CHUNK('I', 'L', 'B', 'M')
#define IFF_IMRT_CHUNK FOURCC_CHUNK('I', 'M', 'R', 'T')
#define IFF_JUNK_CHUNK FOURCC_CHUNK('J', 'U', 'N', 'K')
#define IFF_LIST_CHUNK FOURCC_CHUNK('L', 'I', 'S', 'T')
#define IFF_NAME_CHUNK FOURCC_CHUNK('N', 'A', 'M', 'E')
#define IFF_PAN_CHUNK  FOURCC_CHUNK('P', 'A', 'N', ' ')
#define IFF_PROP_CHUNK FOURCC_CHUNK('P', 'R', 'O', 'P')
#define IFF_RLSE_CHUNK FOURCC_CHUNK('R', 'L', 'S', 'E')
#define IFF_SEQN_CHUNK FOURCC_CHUNK('S', 'E', 'Q', 'N')
#define IFF_TINY_CHUNK FOURCC_CHUNK('T', 'I', 'N', 'Y')
#define IFF_VHDR_CHUNK FOURCC_CHUNK('V', 'H', 'D', 'R')

#define MONO      0L
#define PAN       1L
#define RIGHT     4L
#define LEFT      2L
#define STEREO    6L

#define PREAMBLE_SIZE 8
#define IFF_JUNK_SIZE 8
#define IFF_SIGNATURE_SIZE 12
#define PCM_BLOCK_ALIGN 1024

#define max_volume 65536                        /* Unity = Fixed 1.0 = maximum volume */

int8_t fibonacci[] = { -34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21 };

int8_t exponential[] = { -128, -64, -32, -16, -8, -4, -2, -1, 0, 1, 2, 4, 8, 16, 32, 64 };


typedef struct {
  uint16_t             atak_duration;           /* segment duration in milliseconds */
  uint32_t             atak_dest;               /* destination volume factor */
} eg_point;

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  uint32_t             iff_type;                /* Type of iff-file, see TAGs above */

  /* chunk infos to be rememberd */
  /* audio chunks */
  uint32_t             vhdr_oneShotHiSamples;   /* # samples in the high octave 1-shot part */
  uint32_t             vhdr_repeatHiSamples;    /* # samples in the high octave repeat part */
  uint32_t             vhdr_samplesPerHiCycle;  /* # samples/cycle in high octave, else 0 */
  uint16_t             vhdr_samplesPerSec;      /* data sampling rate */
  uint8_t              vhdr_ctOctave;           /* # of octaves of waveforms */
  uint8_t              vhdr_sCompression;       /* data compression technique used */
  uint32_t             vhdr_volume;             /* playback nominal volume from 0 to Unity
                                                 * (full volume). Map this value into
                                                 * the output hardware's dynamic range.
                                                 */
  eg_point             *atak_eg_point;          /* ? */
  eg_point             *rlse_eg_point;          /* ? */
  uint32_t             chan_settings;           /* Mono, Stereo, Left or Right Channel */
  uint32_t             pan_sposition;           /*  */

  /* some common informations */
  char                 *title;                  /* Name of the stream from NAME-Tag*/
  char                 *copyright;              /* Copyright entry */
  char                 *author;                 /* author entry */
  char                 *annotations;            /* comment of the author, maybe authoring tool */
  char                 *version;                /* version information of the file */

  /* audio information */
  unsigned int         audio_type;
  unsigned int         audio_frames;
  unsigned int         audio_bits;
  unsigned int         audio_channels;
  unsigned int         audio_block_align;
  unsigned int         audio_bytes_per_second;
  unsigned char        *audio_interleave_buffer;
  uint32_t             audio_interleave_buffer_size;
  unsigned char        *audio_read_buffer;
  uint32_t             audio_read_buffer_size;
  int                  audio_buffer_filled;
  uint32_t             audio_volume_left;
  uint32_t             audio_volume_right;
  uint32_t             audio_position;
  int                  audio_compression_factor;


  unsigned int         running_time;

  off_t                data_start;
  off_t                data_size;

  int                  seek_flag;  /* this is set when a seek just occurred */
} demux_iff_t;

typedef struct {
  demux_class_t     demux_class;
} demux_iff_class_t;


/* Decode delta encoded data from n byte source
 * buffer into double as long dest buffer, given initial data
 * value x. The value x is also returned for incrementally
 * decompression. With different decoding tables you can use
 * different decoding deltas
 */

static int8_t delta_decode_block(int8_t *source, int32_t n, int8_t *dest, int8_t x, int8_t *table) {
  int32_t i;
  int lim = n * 2;

  for (i=0; i < lim; i++) {
    /* Decode a data nibble, high nibble then low nibble */
    x += (i & 1) ?
         table[((uint8_t)(source[i >> 1]) & 0xf)] :
         table[((uint8_t)(source[i >> 1]) >> 4)];
    dest[i] = x;           /* store a 1 byte sample */
  }
  return(x);
}

/* Decode a complete delta encoded array */
static void delta_decode(int8_t *dest, int8_t *source, int32_t n, int8_t *table){
  delta_decode_block(&source[2], n-2, dest, source[1], table);
}

/* returns 1 if the IFF file was opened successfully, 0 otherwise */
static int open_iff_file(demux_iff_t *this) {

  unsigned char signature[IFF_SIGNATURE_SIZE];
  unsigned char buffer[256];
  unsigned int  keep_on_reading = 1;
  uint32_t      junk_size;

  if (_x_demux_read_header(this->input, signature, IFF_SIGNATURE_SIZE) != IFF_SIGNATURE_SIZE)
    return 0;

  /* check the signature */
  if (BE_32(&signature[0]) == IFF_FORM_CHUNK)
  {
    switch( BE_32(&signature[8]) )
    {
      case IFF_8SVX_CHUNK:
/*      case IFF_ANIM_CHUNK:*/
/*      case IFF_ILBM_CHUNK:*/
        this->iff_type = BE_32(&signature[8]);
        break;
      default:
        return 0;
        break;
    }
  } else
    return 0;

  /* file is qualified; skip over the header bytes in the stream */
  this->input->seek(this->input, IFF_SIGNATURE_SIZE, SEEK_SET);

  this->title                           = 0;
  this->copyright                       = 0;
  this->author                          = 0;
  this->annotations                     = 0;
  this->version                         = 0;

  this->vhdr_oneShotHiSamples           = 0;
  this->vhdr_repeatHiSamples            = 0;
  this->vhdr_samplesPerHiCycle          = 0;
  this->vhdr_samplesPerSec              = 0;
  this->vhdr_ctOctave                   = 0;
  this->vhdr_sCompression               = 0;
  this->vhdr_volume                     = 0;
  this->chan_settings                   = 0;
  this->audio_type                      = 0;
  this->audio_frames                    = 0;
  this->audio_bits                      = 0;
  this->audio_channels                  = 0;
  this->audio_block_align               = 0;
  this->audio_bytes_per_second          = 0;
  this->running_time                    = 0;
  this->data_start                      = 0;
  this->data_size                       = 0;
  this->seek_flag                       = 0;
  this->audio_interleave_buffer         = 0;
  this->audio_interleave_buffer_size    = 0;
  this->audio_read_buffer               = 0;
  this->audio_read_buffer_size          = 0;
  this->audio_buffer_filled             = 0;
  this->audio_compression_factor        = 1;
  this->audio_position                  = 0;
  this->atak_eg_point                   = 0;
  this->rlse_eg_point                   = 0;

  while ( keep_on_reading == 1 ) {
    if (this->input->read(this->input, signature, IFF_JUNK_SIZE) == IFF_JUNK_SIZE) {
      junk_size = BE_32(&signature[4]);
      if ( junk_size < 256 ) {
        if (this->input->read(this->input, buffer, junk_size) != junk_size)
          return 0;
      }

      switch( BE_32(&signature[0]) ) {
        case IFF_VHDR_CHUNK:
          this->vhdr_oneShotHiSamples   = BE_32(&buffer[0]);
          this->vhdr_repeatHiSamples    = BE_32(&buffer[4]);
          this->vhdr_samplesPerHiCycle  = BE_32(&buffer[8]);
          this->vhdr_samplesPerSec      = BE_16(&buffer[12]);
          this->vhdr_ctOctave           = buffer[14];
          this->vhdr_sCompression       = buffer[15];
          this->audio_channels          = 1;
          this->chan_settings           = MONO;
          switch( this->vhdr_sCompression ) {
            case 0:  /* 8 Bits */
            case 1:  /* Fibonacci */
            case 2:  /* Exponential*/
              this->audio_bits          = 8;
              this->audio_block_align   = PCM_BLOCK_ALIGN;
              this->audio_type          = BUF_AUDIO_LPCM_BE;
              break;
            default: /* unknown codec */
              xine_log(this->stream->xine, XINE_LOG_MSG,
                       _("iff-8svx: unknown compression: %d\n"),
                       this->vhdr_sCompression);
              return 0;
              break;
          }
          this->vhdr_volume             = BE_32(&buffer[16]);
          if (this->vhdr_volume > max_volume)
            this->vhdr_volume           = max_volume;
          break;
        case IFF_NAME_CHUNK:
          this->title                   = strndup( (const char *)buffer, (size_t)junk_size);
          break;
        case IFF_COPY_CHUNK:
          this->copyright               = strndup( (const char *)buffer, (size_t)junk_size);
          break;
        case IFF_AUTH_CHUNK:
          this->author                  = strndup( (const char *)buffer, (size_t)junk_size);
          break;
        case IFF_ANNO_CHUNK:
          this->annotations             = strndup( (const char *)buffer, (size_t)junk_size);
          break;
        case IFF_FVER_CHUNK:
          this->version                 = strndup( (const char *)buffer, (size_t)junk_size);
           break;
        case IFF_ATAK_CHUNK:
          /* not implemented yet */
          break;
        case IFF_RLSE_CHUNK:
          /* not implemented yet */
          break;
        case IFF_CHAN_CHUNK:
          this->chan_settings           = BE_32(&buffer[0]);
          switch( this->chan_settings ) {
            case STEREO:
              this->audio_volume_left   = this->vhdr_volume;
              this->audio_volume_right  = this->vhdr_volume;
              this->audio_channels      = 2;
              break;
            case LEFT:
              this->audio_volume_left   = this->vhdr_volume;
              this->audio_volume_right  = 0;
              this->audio_channels      = 2;
              break;
            case RIGHT:
              this->audio_volume_left   = 0;
              this->audio_volume_right  = this->vhdr_volume;
              this->audio_channels      = 2;
              break;
            default:
              this->chan_settings       = MONO;
              this->audio_channels      = 1;
              break;
          }
          break;
        case IFF_PAN_CHUNK:
          this->chan_settings           = PAN;
          this->pan_sposition           = BE_32(&buffer[0]);
          this->audio_channels          = 2;
          this->audio_volume_left       = this->vhdr_volume / (max_volume / this->pan_sposition);
          this->audio_volume_right      = this->vhdr_volume - this->audio_volume_left;
          break;
        case IFF_JUNK_CHUNK:
          /* JUNK contains garbage and should be ignored */
          break;
        case IFF_BODY_CHUNK:
          this->data_start              = this->input->get_current_pos(this->input);
          this->data_size               = junk_size;
          if( this->vhdr_sCompression > 0 ) {
            this->audio_interleave_buffer_size = this->data_size * 2;
            this->audio_read_buffer_size       = this->data_size;
          } else {
            this->audio_interleave_buffer_size = this->data_size;
            this->audio_read_buffer_size       = 0;
          }
          if( this->chan_settings == MONO)
            this->audio_volume_left     = this->vhdr_volume;
          keep_on_reading               = 0;
          break;
        default:
          signature[4]                  = 0;
          xine_log(this->stream->xine, XINE_LOG_MSG, _("iff: unknown Chunk: %s\n"), signature);
          break;
      }
    } else
      keep_on_reading                   = 0;
  }

  this->audio_bytes_per_second          = this->audio_channels *
                                          (this->audio_bits / 8) * this->vhdr_samplesPerSec;
  this->running_time                    = ((this->vhdr_oneShotHiSamples +
                                            this->vhdr_repeatHiSamples) *
                                           1000 / this->vhdr_samplesPerSec) /
                                          this->audio_channels;
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr_oneShotHiSamples      %d\n",
           this->vhdr_oneShotHiSamples);
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr_repeatHiSamples       %d\n",
           this->vhdr_repeatHiSamples);
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr_samplesPerHiCycle     %d\n",
           this->vhdr_samplesPerHiCycle);
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr_samplesPerSec         %d\n",
           this->vhdr_samplesPerSec);
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr_ctOctave              %d\n",
           this->vhdr_ctOctave);
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr_sCompression          %d\n",
           this->vhdr_sCompression);
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "vhdr_volume                %d\n",
           this->vhdr_volume);
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "chan_settings              %d\n",
           this->chan_settings);
  if( this->title )
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "title                      %s\n",
           this->title);
  if( this->copyright )
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "copyright                  %s\n",
           this->copyright);
  if( this->author )
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "author                     %s\n",
           this->author);
  if( this->annotations )
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "annotations                %s\n",
           this->annotations);
  if( this->version )
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "version                    %s\n",
           this->version);

  return 1;
}

static int demux_iff_send_chunk(demux_plugin_t *this_gen) {

  demux_iff_t *this                     = (demux_iff_t *) this_gen;

  buf_element_t *buf                    = NULL;
  unsigned int remaining_sample_bytes;
  off_t current_file_pos;
  int32_t input_length;
  int64_t zw_pts;
  int64_t zw_rescale;
  int j, k;
  int first_buf;
  int interleave_index;

  /* when audio is available and it's a stereo, left or right stream
   * at iff 8svx, the complete left stream at the beginning and the
   * right channel at the end of the stream. Both have to be inter-
   * leaved, the same way as in the sega film demuxer. I've copied
   * it out there
   */
  if(this->audio_bits > 0) {
    /* just load data chunks from wherever the stream happens to be
     * pointing; issue a DEMUX_FINISHED status if EOF is reached */
    current_file_pos                    = this->audio_position;

    /* load the whole chunk into the buffer */
    if (this->audio_buffer_filled == 0) {
      if (this->audio_interleave_buffer_size > 0)
        this->audio_interleave_buffer   =
              xine_xmalloc(this->audio_interleave_buffer_size);
      if (this->audio_read_buffer_size > 0)
        this->audio_read_buffer         =
                  xine_xmalloc(this->audio_read_buffer_size);
      if (this->audio_read_buffer) {
        if (this->input->read(this->input, this->audio_read_buffer,
            this->data_size) != this->data_size) {
          this->status                  = DEMUX_FINISHED;
          return this->status;
        }

        switch( this->vhdr_sCompression ) {
          case 1:
            if (this->chan_settings == STEREO) {
              delta_decode((int8_t *)(this->audio_interleave_buffer),
                           (int8_t *)(this->audio_read_buffer),
                           (this->data_size/2),
                           fibonacci);
              delta_decode((int8_t *)(&(this->audio_interleave_buffer[this->data_size])),
                           (int8_t *)(&(this->audio_read_buffer[(this->data_size/2)])),
                           (this->data_size/2),
                           fibonacci);
            } else
              delta_decode((int8_t *)(this->audio_interleave_buffer),
                           (int8_t *)(this->audio_read_buffer),
                           this->data_size,
                           fibonacci);
            this->audio_compression_factor = 2;
            break;
          case 2:
            if (this->chan_settings == STEREO) {
              delta_decode((int8_t *)(this->audio_interleave_buffer),
                           (int8_t *)(this->audio_read_buffer),
                           (this->data_size/2),
                           exponential);
              delta_decode((int8_t *)(&(this->audio_interleave_buffer[this->data_size])),
                           (int8_t *)(&(this->audio_read_buffer[(this->data_size/2)])),
                           (this->data_size/2),
                           exponential);
            } else
              delta_decode((int8_t *)(this->audio_interleave_buffer),
                           (int8_t *)(this->audio_read_buffer),
                           this->data_size,
                           exponential);
            this->audio_compression_factor = 2;
            break;
          default:
            break;
        }
        free( this->audio_read_buffer );
        this->audio_read_buffer         = 0;
      } else {
        if (this->input->read(this->input, this->audio_interleave_buffer,
            this->data_size) != this->data_size) {
          this->status                  = DEMUX_FINISHED;
          return this->status;
        }
      }
      this->audio_buffer_filled         = 1;
    }

    /* proceed to de-interleave into individual buffers */
    if (this->chan_settings == STEREO) {
      remaining_sample_bytes            = ((this->data_size - current_file_pos) *
                                           this->audio_compression_factor) / 2;
      interleave_index                  = (current_file_pos *
                                           this->audio_compression_factor) / 2;
    } else {
      remaining_sample_bytes            = ((this->data_size - current_file_pos) *
                                           this->audio_compression_factor);
      interleave_index                  = (current_file_pos *
                                           this->audio_compression_factor);
    }
    first_buf                           = 1;

    zw_pts                              = current_file_pos;

    if (this->chan_settings == STEREO)
      input_length                      = this->data_size *
                                          this->audio_compression_factor;
    else
      input_length                      = this->data_size *
                                          this->audio_compression_factor *
                                          this->audio_channels;
    while (remaining_sample_bytes) {
      buf                               = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type                         = this->audio_type;
      buf->extra_info->input_pos        = zw_pts;
      buf->extra_info->input_length     = input_length;
      buf->pts                          = zw_pts * 90000 / this->audio_bytes_per_second;
      buf->extra_info->input_time       = buf->pts / 90;

      if (remaining_sample_bytes > buf->max_size / this->audio_channels)
        buf->size                       = buf->max_size;
      else
        buf->size                       = remaining_sample_bytes * this->audio_channels;
      remaining_sample_bytes           -= buf->size / this->audio_channels;
      zw_pts                           += buf->size;

      if (this->audio_bits == 16) {
        if (this->chan_settings == STEREO ||
            this->chan_settings == LEFT   ||
            this->chan_settings == PAN    ||
            this->chan_settings == MONO) {
          for (j = 0, k = interleave_index; j < buf->size; j += 4, k += 2) {
            buf->content[j]             = this->audio_interleave_buffer[k];
            buf->content[j + 1]         = this->audio_interleave_buffer[k + 1];
          }
        } else {
          for (j = 0; j < buf->size; j += 4) {
            buf->content[j]             = 0;
            buf->content[j + 1]         = 0;
          }
        }

        if (this->chan_settings == STEREO ||
            this->chan_settings == RIGHT) {
          if (this->chan_settings == STEREO)
            k                           = interleave_index +
                                          (this->data_size *
                                           this->audio_compression_factor / 2);
          else
            k                           = interleave_index *
                                          this->audio_compression_factor;
          for (j = 2; j < buf->size; j += 4, k += 2) {
            buf->content[j]             = this->audio_interleave_buffer[k];
            buf->content[j + 1]         = this->audio_interleave_buffer[k + 1];
          }
        } else {
          for (j = 2; j < buf->size; j += 4) {
            buf->content[j]             = 0;
            buf->content[j + 1]         = 0;
          }
        }
      } else {
        if (this->chan_settings == STEREO ||
            this->chan_settings == LEFT   ||
            this->chan_settings == PAN    ||
            this->chan_settings == MONO) {
          if( this->audio_volume_left == max_volume ) {
            for (j = 0, k = interleave_index; j < buf->size; j += this->audio_channels) {
              buf->content[j]           = this->audio_interleave_buffer[k++] + 0x80;
            }
          } else {
            for (j = 0, k = interleave_index; j < buf->size; j += 2) {
              zw_rescale                = this->audio_interleave_buffer[k++];
              zw_rescale               *= this->audio_volume_left;
              zw_rescale               /= max_volume;
              zw_rescale               += 0x80;
              buf->content[j]           = (zw_rescale>255) ? 255 : ((zw_rescale<0) ? 0 : zw_rescale);
            }
          }
        } else {
          for (j = 0; j < buf->size; j += 2) {
            buf->content[j]             = 0;
          }
        }
        if (this->chan_settings == STEREO ||
            this->chan_settings == RIGHT  ||
            this->chan_settings == PAN) {
          if (this->chan_settings == STEREO)
            k                           = interleave_index +
                                          (this->data_size *
                                           this->audio_compression_factor / 2);
          else
            k                           = interleave_index;
          if( this->audio_volume_right == max_volume ) {
            for (j = 1; j < buf->size; j += 2) {
              buf->content[j]           = this->audio_interleave_buffer[k++] + 0x80;
            }
          } else {
            for (j = 1; j < buf->size; j += 2) {
              zw_rescale                = this->audio_interleave_buffer[k++];
              zw_rescale               *= this->audio_volume_right;
              zw_rescale               /= max_volume;
              zw_rescale               += 0x80;
              buf->content[j]           = (zw_rescale>255) ? 255 : ((zw_rescale<0) ? 0 : zw_rescale);
            }
          }
        }
      }
      interleave_index                 += buf->size / this->audio_channels;

      if (!remaining_sample_bytes)
        buf->decoder_flags             |= BUF_FLAG_FRAME_END;

      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
               "sending audio buf with %d bytes, %lld pts, %d duration\n",
               buf->size, buf->pts, buf->decoder_info[0]);
      this->audio_fifo->put(this->audio_fifo, buf);
    }
    if (buf)
      buf->free_buffer(buf);
    this->status                        = DEMUX_FINISHED;
  }

  return this->status;
}

static void demux_iff_send_headers(demux_plugin_t *this_gen) {

  demux_iff_t *this                     = (demux_iff_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo                      = this->stream->video_fifo;
  this->audio_fifo                      = this->stream->audio_fifo;

  this->status                          = DEMUX_OK;

  /* load stream information */
  switch( this->iff_type ) {
    case IFF_8SVX_CHUNK:
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_CHANNELS,
                         this->audio_channels);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE,
                         this->vhdr_samplesPerSec);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITS,
                         this->audio_bits);
      break;
    case IFF_ANIM_CHUNK:
    case IFF_ILBM_CHUNK:
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 1);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
      break;
    default:
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 0);
      break;
  }

  if( this->title )
    _x_meta_info_set(this->stream, XINE_META_INFO_TITLE, this->title);

  if( this->author )
    _x_meta_info_set(this->stream, XINE_META_INFO_ARTIST, this->author);

  if( this->annotations )
    _x_meta_info_set(this->stream, XINE_META_INFO_COMMENT, this->annotations);

  /* send start buffers */
  _x_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo && this->audio_type) {
    buf                                 = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type                           = this->audio_type;
    buf->decoder_flags                  = BUF_FLAG_HEADER;
    buf->decoder_info[0]                = 0;
    buf->decoder_info[1]                = this->vhdr_samplesPerSec;
    buf->decoder_info[2]                = this->audio_bits;
    buf->decoder_info[3]                = this->audio_channels;
/*    buf->size                           = 0;*/
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_iff_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time, int playing) {

  demux_iff_t *this                     = (demux_iff_t *) this_gen;

  this->seek_flag                       = 1;
  this->status                          = DEMUX_OK;
  _x_demux_flush_engine (this->stream);

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  /* check the boundary offsets */
  this->audio_position                  = (start_pos < 0) ? 0 :
                                          ((start_pos >= this->data_size) ?
                                           this->data_size : start_pos);
  return this->status;
}

static void demux_iff_dispose (demux_plugin_t *this_gen) {
  demux_iff_t *this                     = (demux_iff_t *) this_gen;

  if( this->title )
    free (this->title);
  if( this->copyright )
    free (this->copyright);
  if( this->author )
    free (this->author);
  if( this->annotations )
    free (this->annotations);
  if( this->version )
    free (this->version);

  if( this->audio_interleave_buffer )
    free (this->audio_interleave_buffer);
  if( this->audio_read_buffer )
    free (this->audio_read_buffer);
  if( this->atak_eg_point )
    free( this->atak_eg_point );
  if( this->rlse_eg_point )
    free( this->rlse_eg_point );
  this->audio_buffer_filled             = 0;

  free(this);
}

static int demux_iff_get_status (demux_plugin_t *this_gen) {
  demux_iff_t *this                     = (demux_iff_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_iff_get_stream_length (demux_plugin_t *this_gen) {
  demux_iff_t *this                     = (demux_iff_t *) this_gen;

  return this->running_time;
}

static uint32_t demux_iff_get_capabilities(demux_plugin_t *this_gen){
  return DEMUX_CAP_NOCAP;
}

static int demux_iff_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type){
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_iff_t   *this;

  this                                  = xine_xmalloc (sizeof (demux_iff_t));
  this->stream                          = stream;
  this->input                           = input;

  this->demux_plugin.send_headers       = demux_iff_send_headers;
  this->demux_plugin.send_chunk         = demux_iff_send_chunk;
  this->demux_plugin.seek               = demux_iff_seek;
  this->demux_plugin.dispose            = demux_iff_dispose;
  this->demux_plugin.get_status         = demux_iff_get_status;
  this->demux_plugin.get_stream_length  = demux_iff_get_stream_length;
  this->demux_plugin.get_capabilities   = demux_iff_get_capabilities;
  this->demux_plugin.get_optional_data  = demux_iff_get_optional_data;
  this->demux_plugin.demux_class        = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

    case METHOD_BY_EXTENSION: {
      char *extensions, *mrl;

      mrl                               = input->get_mrl (input);
      extensions                        = class_gen->get_extensions (class_gen);

      if (!_x_demux_check_extension (mrl, extensions)) {
        free (this);
        return NULL;
      }
    }
    /* falling through is intended */

    case METHOD_BY_CONTENT:
    case METHOD_EXPLICIT:

      if (!open_iff_file(this)) {
        free (this);
        return NULL;
      }

      break;

    default:
      free (this);
      return NULL;
  }

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "IFF demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "IFF";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "iff svx";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "audio/x-8svx: 8svx: IFF-8svx Audio;"
         "audio/8svx: 8svx: IFF-8svx Audio;";
}

static void class_dispose (demux_class_t *this_gen) {
  demux_iff_class_t *this               = (demux_iff_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {
  demux_iff_class_t     *this;

  this = xine_xmalloc (sizeof (demux_iff_class_t));

  this->demux_class.open_plugin         = open_plugin;
  this->demux_class.get_description     = get_description;
  this->demux_class.get_identifier      = get_identifier;
  this->demux_class.get_mimetypes       = get_mimetypes;
  this->demux_class.get_extensions      = get_extensions;
  this->demux_class.dispose             = class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 23, "iff", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

