/*
 * Copyright (C) 2000-2003 the xine project
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
 * $Id: demux_mpgaudio.c,v 1.137 2004/05/05 22:37:46 tmattern Exp $
 *
 * demultiplexer for mpeg audio (i.e. mp3) streams
 *
 * mp3 file structure:
 *   [id3v2] [Xing] Frame1 Frame2 Frame3... [id3v1]
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_mpeg_audio"
#define LOG_VERBOSE
/*
#define LOG
*/
#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "compat.h"
#include "bswap.h"
#include "group_audio.h"
#include "id3.h"

#define NUM_PREVIEW_BUFFERS  10
#define SNIFF_BUFFER_LENGTH  1024

#define WRAP_THRESHOLD       120000

#define FOURCC_TAG BE_FOURCC
#define RIFF_CHECK_BYTES 1024
#define RIFF_TAG FOURCC_TAG('R', 'I', 'F', 'F')
#define AVI_TAG FOURCC_TAG('A', 'V', 'I', ' ')
#define CDXA_TAG FOURCC_TAG('C', 'D', 'X', 'A')
#define MPEG_MARKER FOURCC_TAG( 0x00, 0x00, 0x01, 0xBA )


/* Xing header stuff */
#define XING_TAG FOURCC_TAG('X', 'i', 'n', 'g')
#define XING_FRAMES_FLAG     0x0001
#define XING_BYTES_FLAG      0x0002
#define XING_TOC_FLAG        0x0004
#define XING_VBR_SCALE_FLAG  0x0008
#define XING_TOC_LENGTH      100

typedef struct {
  /* header */
  uint16_t  frame_sync;
  uint8_t   mpeg25_bit;
  uint8_t   lsf_bit;
  uint8_t   layer;
  uint8_t   protection_bit;
  uint8_t   bitrate_idx;
  uint8_t   freq_idx;
  uint8_t   padding_bit;
  uint8_t   private_bit;
  uint8_t   channel_mode;
  uint8_t   mode_extension;
  uint8_t   copyright;
  uint8_t   original;
  uint8_t   emphasis;

  uint8_t   version_idx;
  int       bitrate;
  int       samplerate;
  int       length;               /* in bytes */
  double    duration;             /* in 1/90000 s */
} mpg_audio_frame_t;


typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  int                  status;

  int                  stream_length;
  int                  br;               /* bitrate */
  int                  abr;              /* average bitrate */
  int64_t              last_pts;
  int                  send_newpts;
  int                  buf_flag_seek;
  uint32_t             blocksize;

  mpg_audio_frame_t    cur_frame;
  double               cur_fpts;
  int                  is_vbr;
  int                  meta_info_flag;

  /* Xing header */
  int                  check_xing;
  uint32_t             xflags;
  uint32_t             xframes;
  uint32_t             xbytes;
  uint8_t              xtoc[100];
  uint32_t             xvbr_scale;
} demux_mpgaudio_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;

} demux_mpgaudio_class_t;

/* bitrate table tabsel_123[mpeg version][layer][bitrate index]
 * values stored in kbps
 */
const int tabsel_123[2][3][16] = {
   { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,},
     {0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384,},
     {0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,} },

   { {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,},
     {0, 8,16,24,32,40,48, 56, 64, 80, 96,112,128,144,160,},
     {0, 8,16,24,32,40,48, 56, 64, 80, 96,112,128,144,160,} }
};

static int frequencies[3][3] = {
	{ 44100, 48000, 32000 },
	{ 22050, 24000, 16000 },
	{ 11025, 12000,  8000 }
};


static int mpg123_xhead_check(char *buf)
{
  return (BE_32(buf) == XING_TAG);
}

static void check_newpts (demux_mpgaudio_t *this, int64_t pts) {

  int64_t diff;

  diff = pts - this->last_pts;

  if( pts &&
      (this->send_newpts || (this->last_pts && abs(diff)>WRAP_THRESHOLD) ) ) {
    if (this->buf_flag_seek) {
      _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      _x_demux_control_newpts(this->stream, pts, 0);
    }
    this->send_newpts = 0;
  }

  if( pts )
    this->last_pts = pts;
}

static int mpg123_parse_frame_header(mpg_audio_frame_t *frame, uint8_t *buf) {
  uint32_t head;

  head = BE_32(buf);

  lprintf("header: %08X\n", head);
  frame->frame_sync     =  head >> 21;
  if (frame->frame_sync != 0x7ff) {
    lprintf("invalid frame sync\n");
    return 0;
  }

  frame->mpeg25_bit     = (head >> 20) & 0x1;
  frame->lsf_bit        = (head >> 19) & 0x1;
  if (!frame->mpeg25_bit) {
    if (frame->lsf_bit) {
      lprintf("reserved mpeg25 lsf combination\n");
      return 0;
    } else
      frame->version_idx = 2;  /* MPEG Version 2.5 */
  } else {
    if (!frame->lsf_bit)
      frame->version_idx = 1;  /* MPEG Version 2 */
    else
      frame->version_idx = 0;  /* MPEG Version 1 */
  }

  frame->layer = 4 - ((head >> 17) & 0x3);
  if (frame->layer == 4) {
    lprintf("reserved layer\n");
    return 0;
  }

  frame->protection_bit = (head >> 16) & 0x1;
  frame->bitrate_idx    = (head >> 12) & 0xf;
  if ((frame->bitrate_idx == 0) || (frame->bitrate_idx == 15)) {
    lprintf("invalid bitrate index\n");
    return 0;
  }

  frame->freq_idx       = (head >> 10) & 0x3;
  if (frame->freq_idx == 3) {
    lprintf("invalid frequence index\n");
    return 0;
  }

  frame->padding_bit    = (head >>  9) & 0x1;
  frame->private_bit    = (head >>  8) & 0x1;
  frame->channel_mode   = (head >>  6) & 0x3;
  frame->mode_extension = (head >>  4) & 0x3;
  frame->copyright      = (head >>  3) & 0x1;
  frame->original       = (head >>  2) & 0x1;
  frame->emphasis       =  head        & 0x3;

#if defined(OPT_STRICT)
  /*
   * ISO/IEC 11172-3 says this is a reserved emphasis value, but
   * streams exist which use it anyway. Since the value is not important
   * to the decoder proper, we allow it unless OPT_STRICT is defined.
   */
  if (frame->emphasis == 2) {
    lprintf("reserved emphasis\n");
    return 0;
  }
#endif
  
  frame->bitrate = tabsel_123[!frame->lsf_bit][frame->layer - 1][frame->bitrate_idx] * 1000;
  frame->samplerate = frequencies[frame->version_idx][frame->freq_idx];
  if (frame->layer == 1) {
    frame->length = (12 * frame->bitrate / frame->samplerate + frame->padding_bit) * 4;
    frame->duration = 90000.0 * 384.0 / (double)frame->samplerate;
  } else {
    int slots_per_frame;
    slots_per_frame = (frame->layer == 3 && !frame->lsf_bit) ? 72 : 144;

    frame->length = slots_per_frame * frame->bitrate / frame->samplerate +
                    frame->padding_bit;
    frame->duration = 90000.0 * slots_per_frame * 8.0 / (double)frame->samplerate;
  }

  lprintf("mpeg %d, layer %d\n", frame->version_idx + 1, frame->layer);
  lprintf("bitrate: %d bps, samplerate: %d Hz\n", frame->bitrate, frame->samplerate);
  lprintf("length: %d bytes, %f pts\n", frame->length, frame->duration);
  return 1;
}

static int mpg123_parse_xing_header(demux_mpgaudio_t *this, uint8_t *buf, int bufsize) {

  int i;
  uint8_t *ptr = buf;
  double frame_duration;

  /* offset of the Xing header */
  if (this->cur_frame.lsf_bit) {
    if( this->cur_frame.channel_mode != 3 )
      ptr += (32 + 4);
    else
      ptr += (17 + 4);
  } else {
    if( this->cur_frame.channel_mode != 3 )
      ptr += (17 + 4);
    else
      ptr += (9 + 4);
  }
  
  if (ptr >= (buf + bufsize - 4)) return 0;
  lprintf("checking %08X\n", *ptr);
  if (mpg123_xhead_check(ptr)) {
    lprintf("Xing header found\n");
    ptr += 4;
    
    if (ptr >= (buf + bufsize - 4)) return 0;
    this->xflags = BE_32(ptr); ptr += 4;

    if (this->xflags & XING_FRAMES_FLAG) {
	    if (ptr >= (buf + bufsize - 4)) return 0;
      this->xframes = BE_32(ptr); ptr += 4;
      lprintf("xframes: %d\n", this->xframes);
    }
    if (this->xflags & XING_BYTES_FLAG) {
      if (ptr >= (buf + bufsize - 4)) return 0;
      this->xbytes = BE_32(ptr); ptr += 4;
      lprintf("xbytes: %d\n", this->xbytes);
    }
    if (this->xflags & XING_TOC_FLAG) {
      lprintf("toc found\n");
      if (ptr >= (buf + bufsize - XING_TOC_LENGTH)) return 0;

      for (i = 0; i < XING_TOC_LENGTH; i++) {
        this->xtoc[i] = *(ptr + i);
#ifdef LOG
        printf("%d ", this->xtoc[i]);
#endif
      }
#ifdef LOG
      printf("\n");
#endif
	    ptr += XING_TOC_LENGTH;
    }
    this->xvbr_scale = -1;
    if (this->xflags & XING_VBR_SCALE_FLAG) {
      if (ptr >= (buf + bufsize - 4)) return 0;
      this->xvbr_scale = BE_32(ptr);
      lprintf("xvbr_scale: %d\n", this->xvbr_scale);
    }

    /* 1 kbit = 1000 bits ! (and not 1024 bits) */
    if (this->xflags & (XING_FRAMES_FLAG | XING_BYTES_FLAG)) {
      if (this->cur_frame.layer == 1) {
        frame_duration = 384.0 / (double)this->cur_frame.samplerate;
      } else {
        int slots_per_frame;
        slots_per_frame = (this->cur_frame.layer == 3 &&
                           !this->cur_frame.lsf_bit) ? 72 : 144;
        frame_duration = slots_per_frame * 8.0 / (double)this->cur_frame.samplerate;
      }
      this->abr = ((double)this->xbytes * 8.0) / ((double)this->xframes * frame_duration);
      this->stream_length = (double)this->xframes * frame_duration;
      this->is_vbr = 1;
      lprintf("abr: %d bps\n", this->abr);
      lprintf("stream_length: %d s, %d min %d s\n", this->stream_length,
              this->stream_length / 60, this->stream_length % 60);
    } else {
      /* it's a stupid Xing header */
      this->is_vbr = 0;
    }
    return 1;
  } else {
    lprintf("Xing header not found\n");
    return 0;
  }
}

static int mpg123_parse_frame_payload(demux_mpgaudio_t *this,
                                      uint8_t *frame_header,
                                      int decoder_flags) {
  buf_element_t *buf;
  off_t          frame_pos, len;
  uint64_t       pts = 0;

  frame_pos = this->input->get_current_pos(this->input) - 4;
  lprintf("frame_pos = %lld\n", frame_pos);

  buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);

  if (this->cur_frame.length > buf->max_size) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_mpgaudio: frame size is greater than fifo buffer size\n");
    buf->free_buffer(buf);
    return 0;
  }
  
  /* the decoder needs the frame header */
  memcpy(buf->mem, frame_header, 4);

  len = this->input->read(this->input, buf->mem + 4, this->cur_frame.length - 4);
  if (len != (this->cur_frame.length - 4)) {
    buf->free_buffer(buf);
    return 0;
  }

  /*
   * compute stream length (in s)
   * use the Xing header if there is one (VBR) otherwise use CBR formula
   * set stream_info before sending any buffer to the decoder
   */
  if (this->check_xing) {
    mpg123_parse_xing_header(this, buf->mem, len + 4);
    if (!this->is_vbr) {
      this->stream_length = this->input->get_length(this->input) / (this->br / 8);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_BITRATE, this->br);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, this->br);
    } else {
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_BITRATE, this->abr);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE, this->abr);
    }
    this->check_xing = 0;
  }
  
  /* set codec infos here
   * the decoder doesn't know if the stream is VBR
   */
  if (!this->meta_info_flag) {
    char scratch_buf[256];
    char *mpeg_ver[3] = {"1", "2", "2.5"};
    
    snprintf(scratch_buf, 256, "MPEG %s Layer %1d%s",
             mpeg_ver[this->cur_frame.version_idx], this->cur_frame.layer,
             (this->is_vbr)? " VBR" : " CBR" );
    _x_meta_info_set(this->stream, XINE_META_INFO_AUDIOCODEC, scratch_buf);
    
    this->meta_info_flag = 1;
  }

  this->cur_fpts += this->cur_frame.duration;

  pts = (int64_t)this->cur_fpts;
  check_newpts(this, pts);

  buf->extra_info->input_pos  = frame_pos;
  buf->extra_info->input_time = pts / 90;
  buf->pts                    = pts;
  buf->size                   = len + 4;
  buf->content                = buf->mem;
  buf->type                   = BUF_AUDIO_MPEG;
  buf->decoder_info[0]        = 1;
  buf->decoder_flags          = decoder_flags;

  this->audio_fifo->put(this->audio_fifo, buf);
  return 1;
}


static unsigned char * demux_mpgaudio_read_buffer_header (input_plugin_t *input)
{
  int count;
  uint8_t buf[MAX_PREVIEW_SIZE];
  unsigned char *retval;

  if(!input)
    return 0;

  if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
    input->seek(input, 0, SEEK_SET);

    count = input->read(input, buf, SNIFF_BUFFER_LENGTH);
    if (count < SNIFF_BUFFER_LENGTH)
    {
      return NULL;
    }
  } else if ((input->get_capabilities(input) & INPUT_CAP_PREVIEW) != 0) {
    input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW);
  } else {
    return NULL;
  }

  retval = xine_xmalloc (SNIFF_BUFFER_LENGTH);
  memcpy (retval, buf, SNIFF_BUFFER_LENGTH);

  return retval;
}

/* Scan through the first SNIFF_BUFFER_LENGTH bytes of the
 * buffer to find a potential 32-bit MP3 frame header. */
static int sniff_buffer_looks_like_mp3 (input_plugin_t *input)
{
  int offset;
  unsigned char *buf;
  mpg_audio_frame_t frame;

  buf = demux_mpgaudio_read_buffer_header (input);
  if (buf == NULL)
    return 0;

  for (offset = 0; offset + 4 < SNIFF_BUFFER_LENGTH; offset++) {

    if (mpg123_parse_frame_header(&frame, buf + offset)) {
      size_t length = frame.length;

      /* Since one frame is available, is there another frame
       * just to be sure this is more likely to be a real MP3
       * buffer? */
      offset += length;

      if (offset + 4 > SNIFF_BUFFER_LENGTH)
      {
        free (buf);
        return 0;
      }

      if (mpg123_parse_frame_header(&frame, buf + offset)) {
        free (buf);
        lprintf("mpeg audio frame detected\n");
        return 1;
      }
      break;
    }
  }

  free (buf);
  return 0;
}

static int mpg123_read_frame_header(demux_mpgaudio_t *this, uint8_t *header_buf, int bytes) {
  off_t len;
  int i;
  
  for (i = 0; i < (4 - bytes); i++) {
    header_buf[i] = header_buf[i + bytes];
  }

  len = this->input->read(this->input, header_buf + 4 - bytes, bytes);
  if (len != ((off_t) bytes)) {
    return 0;
  }
  return 1;
}

static int demux_mpgaudio_next (demux_mpgaudio_t *this, int decoder_flags) {
  uint8_t  header_buf[4];
  int      bytes = 4;

  if (!this->audio_fifo)
    return 0;

  for (;;) {

    if (mpg123_read_frame_header(this, header_buf, bytes)) {

      if (mpg123_parse_frame_header(&this->cur_frame, header_buf)) {
        if (!this->br) {
          this->br = this->cur_frame.bitrate;
        }
        return mpg123_parse_frame_payload(this, header_buf, decoder_flags);
        
      } else if ((BE_32(header_buf)) == ID3V22_TAG) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "demux_mpgaudio: ID3V2.2 tag\n");
        if (!id3v22_parse_tag(this->input, this->stream, header_buf)) {
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  "demux_mpgaudio: ID3V2.2 tag parsing error\n");
          bytes = 1; /* resync */
        } else {
          bytes = 4;
        }

      } else if ((BE_32(header_buf)) == ID3V23_TAG) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "demux_mpgaudio: ID3V2.3 tag\n");
        if (!id3v23_parse_tag(this->input, this->stream, header_buf)) {
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  "demux_mpgaudio: ID3V2.3 tag parsing error\n");
          bytes = 1; /* resync */
        } else {
          bytes = 4;
        }

      } else if ((BE_32(header_buf)) == ID3V24_TAG) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "demux_mpgaudio: ID3V2.4 tag\n");
        /* TODO: add parsing here */
        bytes = 1; /* resync */

      } else {
        /* skip */
        bytes = 1;
      }

    } else {
      lprintf("read error\n");
      return 0;
    }
  }
}

static int demux_mpgaudio_send_chunk (demux_plugin_t *this_gen) {

  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  if (!demux_mpgaudio_next (this, 0))
    this->status = DEMUX_FINISHED;

  return this->status;
}

static int demux_mpgaudio_get_status (demux_plugin_t *this_gen) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  return this->status;
}

static int demux_mpgaudio_read_head(input_plugin_t *input, uint8_t *buf) {

  int       bs = 0;
  int       i, optional;

  if(!input)
    return 0;

  if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
    input->seek(input, 0, SEEK_SET);

    if (input->get_capabilities (input) & INPUT_CAP_BLOCK)
      bs = input->get_blocksize(input);

    if(!bs)
      bs = MAX_PREVIEW_SIZE;

    input->read(input, buf, bs);

    lprintf("stream is seekable\n");

  } else if ((input->get_capabilities(input) & INPUT_CAP_PREVIEW) != 0) {

    lprintf("input plugin provides preview\n");

    optional = input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW);
    optional = optional > 256 ? 256 : optional;

    lprintf("got preview %02x %02x %02x %02x\n",
	    buf[0], buf[1], buf[2], buf[3]);
    
    for(i = 0; i < (optional - 4); i++) {
      if (BE_32(buf + i) == RIFF_TAG)
        return 1;
    }
  } else {
    lprintf("not seekable, no preview\n");
    return 0;
  }
  return 1;
}

static void demux_mpgaudio_send_headers (demux_plugin_t *this_gen) {

  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;
  int i;

  this->stream_length = 0;
  this->last_pts      = 0;
  this->status        = DEMUX_OK;
  this->check_xing    = 1;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, 0);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, 1);

  /* read id3 info only from inputs with seeking and without "live" flag */
  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {
    off_t pos;

    /* check ID3 v1 at the end of the stream */
    pos = this->input->get_length(this->input) - 128;
    if(pos > 0) {
      if (pos == this->input->seek (this->input, pos, SEEK_SET))
        id3v1_parse_tag (this->input, this->stream);
    }
  }

  /*
   * send preview buffers
   */
  _x_demux_control_start (this->stream);

  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0)
    this->input->seek (this->input, 0, SEEK_SET);

  for (i = 0; i < NUM_PREVIEW_BUFFERS; i++) {
    if (!demux_mpgaudio_next (this, BUF_FLAG_PREVIEW)) {
      break;
    }
  }

  if (this->cur_frame.samplerate) {
    if (this->cur_frame.layer == 1)
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION,
        384000 / this->cur_frame.samplerate);
    else
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION,
        1152000 / this->cur_frame.samplerate);
  }
  
  this->status = DEMUX_OK;
}

/* interpolate in Xing TOC to get file seek point in bytes */
static off_t xing_get_seek_point(demux_mpgaudio_t *this, int time)
{
  off_t seekpoint;
  int a;
  float fa, fb, fx;
  float percent;

  percent = ((float)time / 10.0f)/ (float)this->stream_length;
  if (percent < 0.0f)   percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;

  a = (int)percent;
  if (a > 99) a = 99;
  fa = this->xtoc[a];
  if (a < 99) {
      fb = this->xtoc[a + 1];
  } else {
      fb = 256.0f;
  }

  fx = fa + (fb - fa) * (percent - a);
  seekpoint = (off_t)((1.0f / 256.0f) * fx * this->xbytes);

  return seekpoint;
}

/* interpolate in Xing TOC to get file seek point in ms */
static int xing_get_seek_time(demux_mpgaudio_t *this, off_t pos)
{
  int seektime;
  int a, b;
  float fb, fx;
  float percent;

  fx = 256.0f * (float)pos / (float)this->xbytes;
  if (fx < 0.0f)   fx = 0.0f;
  if (fx > 256.0f) fx = 256.0f;

  for (b = 0; b < 100; b++) {
    fb = this->xtoc[b];
    if (fb > fx)
      break;
  }
 
  if (b > 0) {
    a = b - 1;
  } else {
    a = 0;
  }

  percent = a + (fx - this->xtoc[a]);
  seektime = 10.0f * percent * this->stream_length;

  return seektime;
}

static int demux_mpgaudio_seek (demux_plugin_t *this_gen,
                                off_t start_pos, int start_time, int playing) {

  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  if ((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    if (!start_pos && start_time && this->stream_length > 0) {
      if (this->is_vbr && (this->xflags & (XING_TOC_FLAG | XING_BYTES_FLAG))) {
        /* vbr  */
        start_pos = xing_get_seek_point(this, start_time);
        lprintf("time seek: vbr: time=%d, pos=%lld\n", start_time, start_pos);
      } else {
        /* cbr  */
        off_t input_length = this->input->get_length(this->input);

        if ((input_length > 0) && (this->stream_length > 0)) {
          start_pos = start_time * input_length / (1000 * this->stream_length);
          lprintf("time seek: cbr: time=%d, pos=%lld\n", start_time, start_pos);
        }
      }
    } else {
      if (this->is_vbr && (this->xflags & (XING_TOC_FLAG | XING_BYTES_FLAG))) {
        /* vbr  */
        start_time = xing_get_seek_time(this, start_pos);
        lprintf("pos seek: vbr: time=%d, pos=%lld\n", start_time, start_pos);
      } else {
        /* cbr  */
        off_t input_length = this->input->get_length(this->input);

        if((input_length > 0) && (this->stream_length > 0)) {
          start_time = (1000 * start_pos * this->stream_length) / input_length;
          lprintf("pos seek: cbr\n");
        }
      }
    }
    this->cur_fpts = 90.0 * (double)start_time;
    this->input->seek (this->input, start_pos, SEEK_SET);
  }

  this->status = DEMUX_OK;
  this->send_newpts = 1;

  if( !playing ) {
    this->buf_flag_seek = 0;
  } else {
    this->buf_flag_seek = 1;
    _x_demux_flush_engine(this->stream);
  }

  return this->status;
}

static void demux_mpgaudio_dispose (demux_plugin_t *this) {

  free (this);
}

static int demux_mpgaudio_get_stream_length (demux_plugin_t *this_gen) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  if (this->stream_length > 0) {
    return this->stream_length * 1000;
  } else
    return 0;
}

static uint32_t demux_mpgaudio_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_mpgaudio_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
} 

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_mpgaudio_t *this;
  id3v2_header_t    tag_header;
  mpg_audio_frame_t frame;
  uint8_t           buf[MAX_PREVIEW_SIZE];
  uint8_t          *riff_check;
  int               i;
  uint8_t          *ptr;

  lprintf("trying to open %s...\n", input->get_mrl(input));

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint32_t head;

    if (!demux_mpgaudio_read_head(input, buf))
      return NULL;

    head = BE_32(buf);
    lprintf("head is %8X\n", head);
    
    if (head == RIFF_TAG) {
      int ok;

      lprintf("found RIFF tag\n");
      /* skip the length */
      ptr = buf + 8;

      riff_check = ptr; ptr += 4;
      if ((buf + MAX_PREVIEW_SIZE) < ptr)
        return NULL;

      /* disqualify the file if it is, in fact, an AVI file or has a CDXA
       * marker */
      if ((BE_32(riff_check) == AVI_TAG) ||
          (BE_32(riff_check) == CDXA_TAG)) {
        lprintf("found AVI or CDXA tag\n");
        return NULL;
      }

      /* skip 4 more bytes */
      ptr += 4;

      /* get the length of the next chunk */
      riff_check = ptr; ptr += 4;
      if ((buf + MAX_PREVIEW_SIZE) < ptr)
        return NULL;
      /* head gets to be a generic variable in this case */
      head = LE_32(riff_check);
      /* skip over the chunk and the 'data' tag and length */
      ptr += 8;

      /* load the next, I don't know...n bytes, and check for a valid
       * MPEG audio header */
      riff_check = ptr; ptr += RIFF_CHECK_BYTES;
      if ((buf + MAX_PREVIEW_SIZE) < ptr)
        return NULL;

      ok = 0;
      for (i = 0; i < RIFF_CHECK_BYTES - 4; i++) {
        head = BE_32(riff_check + i);

        lprintf("checking %08X\n", head);

        if (sniff_buffer_looks_like_mp3(input))
          ok = 1;
      }
      if (!ok)
        return NULL;

    } else if ((head == ID3V22_TAG) ||
               (head == ID3V23_TAG) ||
               (head == ID3V24_TAG)) {
      /* check if a mp3 frame follows the tag
       * id3v2 are not specific to mp3 files,
       * flac files can contain id3v2 tags
       */
      ptr = buf + 4;
      tag_header.size = (ptr[2] << 21) + (ptr[3] << 14) + (ptr[4] << 7) + ptr[5];
      lprintf("id3v2.%d tag detected, size: %d\n", buf[3], tag_header.size);

      ptr += tag_header.size + 6;
      if ((ptr + 4) <= (buf + MAX_PREVIEW_SIZE)) {
        if (!mpg123_parse_frame_header(&frame, ptr)) {
          lprintf ("invalid mp3 frame header\n");
          return NULL;
        }
        lprintf ("a valid mp3 frame follows the id3 tag\n");
      } else {
        lprintf ("the id3v2 tag is too long\n");
        return NULL;
      }

    } else if (head == MPEG_MARKER) {
      lprintf ("discard mpeg video\n");
      return NULL;
    } else if (!sniff_buffer_looks_like_mp3 (input)) {
      lprintf ("sniff_buffer_looks_like_mp3 failed\n");
      return NULL;
    }
  }
  break;

  case METHOD_BY_EXTENSION: {
    char *mrl = input->get_mrl(input);
    char *extensions = class_gen->get_extensions (class_gen);
    
    lprintf ("stage by extension %s\n", mrl);
    
    if (!_x_demux_check_extension (mrl, extensions))
      return NULL;
      
  }
  break;

  case METHOD_EXPLICIT:
  break;
  
  default:
    return NULL;
  }
  
  this = xine_xmalloc (sizeof (demux_mpgaudio_t));

  this->demux_plugin.send_headers      = demux_mpgaudio_send_headers;
  this->demux_plugin.send_chunk        = demux_mpgaudio_send_chunk;
  this->demux_plugin.seek              = demux_mpgaudio_seek;
  this->demux_plugin.dispose           = demux_mpgaudio_dispose;
  this->demux_plugin.get_status        = demux_mpgaudio_get_status;
  this->demux_plugin.get_stream_length = demux_mpgaudio_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_mpgaudio_get_capabilities;
  this->demux_plugin.get_optional_data = demux_mpgaudio_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;
  
  this->input      = input;
  this->audio_fifo = stream->audio_fifo;
  this->status     = DEMUX_FINISHED;
  this->stream     = stream;
  
  return &this->demux_plugin;
}

/*
 * demux mpegaudio class
 */

static char *get_description (demux_class_t *this_gen) {
  return "MPEG audio demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "MPEGAUDIO";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "mp3 mp2 mpa mpega";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "audio/mpeg2: mp2: MPEG audio;"
         "audio/x-mpeg2: mp2: MPEG audio;"
         "audio/mpeg3: mp3: MPEG audio;"
         "audio/x-mpeg3: mp3: MPEG audio;"
         "audio/mpeg: mpa,abs,mpega: MPEG audio;"
         "audio/x-mpeg: mpa,abs,mpega: MPEG audio;"
         "x-mpegurl: mp3: MPEG audio;"
         "audio/mpegurl: mp3: MPEG audio;"
         "audio/mp3: mp3: MPEG audio;"
         "audio/x-mp3: mp3: MPEG audio;";
}

static void class_dispose (demux_class_t *this_gen) {

  demux_mpgaudio_class_t *this = (demux_mpgaudio_class_t *) this_gen;

  free (this);
}

void *demux_mpgaudio_init_class (xine_t *xine, void *data) {
  
  demux_mpgaudio_class_t     *this;
  
  this         = xine_xmalloc (sizeof (demux_mpgaudio_class_t));
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}
