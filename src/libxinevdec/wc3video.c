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
 * Video Decoder for Origin's Wing Commander III MVE Movie Files
 * by Mario Brito (mbrito@student.dei.uc.pt)
 * For more information on the WC3 Movie format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: wc3video.c,v 1.10 2002/11/20 11:57:48 mroi Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "buffer.h"
#include "xine_internal.h"
#include "video_out.h"
#include "xineutils.h"
#include "bswap.h"

#define VIDEOBUFSIZE 128*1024

#define WC3_WIDTH   320
#define WC3_HEIGHT  165

typedef struct {
  video_decoder_class_t   decoder_class;
} wc3video_class_t;

typedef struct wc3video_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  wc3video_class_t *class;
  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object */
  uint64_t          video_step;  /* frame duration in pts units */
  int               decoder_ok;  /* current decoder status */
  int               skipframes;

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */

  unsigned char     yuv_palette[256 * 4];

  yuv_planes_t      yuv_planes1;
  yuv_planes_t      yuv_planes2;

  yuv_planes_t     *current_frame;
  yuv_planes_t     *last_frame;

  /* this is either 1 or 2 indicating the current_frame points to yuv_planes
   * structure 1 or 2 */
  int               current_planes;

} wc3video_decoder_t;

/**************************************************************************
 * WC3 video specific decode functions
 *************************************************************************/

#define SIZE (WC3_WIDTH * WC3_HEIGHT)
#define BYTE unsigned char

static BYTE buffer1[SIZE];
static BYTE buffer2[SIZE];

static BYTE * part1;
static BYTE * part2;
static BYTE * part3;
static BYTE * part4;

static void bytecopy(BYTE * dest, BYTE * src, int count) {

  int i;

  /* Don't use memcpy because the memory locations often overlap and
   * memcpy doesn't like that; it's not uncommon, for example, for 
   * dest = src+1, to turn byte A into  pattern AAAAAAAA.
   * This was originally repz movsb. */
   for (i = 0; i < count; i ++)
     dest[i] = src[i];
}

static void wc3_build_frame (wc3video_decoder_t *this) {

  int frame_size = WC3_WIDTH * WC3_HEIGHT;
  int index = 0;
  BYTE pixel;
  BYTE y, u, v;
  int size = 0;
  BYTE flag = 0;
  int func;

  while (index < frame_size) {
    func = *part1++;
    size = 0;

    switch (func) {
      case 0:
        flag ^= 1;
        continue;

      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
        size = func;
        break;

      case 12:
      case 13:
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
        size += (func - 10);
        break;

      case 9:
      case 19:
        size = (*part2++);
        break;

      case 10:
      case 20:
        size = BE_16(&part2[0]);
        part2 += 2;
        break;

      case 11:
      case 21:
        size = (part2[0] << 16) | (part2[1] << 8) | part2[2];
        part2 += 3;
        break;
    }

    /* run is unchanged from last frame */
    if (func < 12) {
      flag = flag ^ 1;
      if ( flag ) {
        xine_fast_memcpy(
          &this->current_frame->y[index], 
          &this->last_frame->y[index],
          size);
        xine_fast_memcpy(
          &this->current_frame->u[index], 
          &this->last_frame->u[index],
          size);
        xine_fast_memcpy(
          &this->current_frame->v[index], 
          &this->last_frame->v[index],
          size);
        index += size;
      } else {
        while (size > 0) {
          pixel = *part4++;
          y = this->yuv_palette[pixel * 4 + 0];
          u = this->yuv_palette[pixel * 4 + 1];
          v = this->yuv_palette[pixel * 4 + 2];
          this->current_frame->y[index] = y;
          this->current_frame->u[index] = u;
          this->current_frame->v[index] = v;
          index++;
          size --;
        }
      }
    } else {
      /* run displacement from last frame */
      int x = (*part3 >> 4) & 0xf;
      int y = *part3 & 0xf;
      part3++;

      /* extend sign */
      if (x & 8)  x |= 0xfffffff0;
      if (y & 8)  y |= 0xfffffff0;

      /* copy a run of pixels from the previous frame */
      xine_fast_memcpy(
        &this->current_frame->y[index], 
        &this->last_frame->y[index + x + y * WC3_WIDTH],
        size);
      xine_fast_memcpy(
        &this->current_frame->u[index], 
        &this->last_frame->u[index + x + y * WC3_WIDTH],
        size);
      xine_fast_memcpy(
        &this->current_frame->v[index], 
        &this->last_frame->v[index + x + y * WC3_WIDTH],
        size);
      index += size;

      flag = 0;
    }
  }
}

static void wc3_do_part4 (BYTE * dest, BYTE * src) {

  int func;
  int size;
  int offset;
  int byte1, byte2, byte3;

  for (;;) {
    func = *src++;

    if ( (func & 0x80) == 0 ) {

      offset = *src++;

      size = func & 3;
      bytecopy(dest, src, size);  dest += size;  src += size;

      size = ((func & 0x1c) >> 2) + 3;
      bytecopy (dest, dest - (  ((func & 0x60) << 3) + offset + 1  ), size);
      dest += size;

    } else if ( (func & 0x40) == 0 ) {

      byte1 = *src++;
      byte2 = *src++;

      size = byte1 >> 6;
      bytecopy (dest, src, size);  dest += size;  src += size;

      size = (func & 0x3f) + 4;
      bytecopy (dest, dest - (((byte1 & 0x3f) << 8) + byte2 + 1), size);
      dest += size;

    } else if ( (func & 0x20) == 0 ) {

      byte1 = *src++;
      byte2 = *src++;
      byte3 = *src++;

      size = func & 3;
      bytecopy (dest, src, size);  dest += size;  src += size;

      size = byte3 + 5 + ((func & 0xc) << 6);
      bytecopy (dest,
        dest - ((((func & 0x10) >> 4) << 0x10) + 1 + (byte1 << 8) + byte2),
        size);
      dest += size;
    } else {
      size = ((func & 0x1f) << 2) + 4;

      if (size > 0x70)
        break;

      bytecopy (dest, src, size);  dest += size;  src += size;
    }
  }

  size = func & 3;
  bytecopy(dest, src, size);  dest += size;  src += size;
}

static void wc3_do_part1 (BYTE * dest, BYTE * src) {

  BYTE byte = *src++;
  BYTE ival = byte + 0x16;
  BYTE * ptr = src + byte*2;
  BYTE val = ival;
  int counter = 0;

  BYTE bits = *ptr++;

  while ( val != 0x16 ) {
    if ( (1 << counter) & bits )
      val = src[byte + val - 0x17];
    else
      val = src[val - 0x17];

    if ( val < 0x16 ) {
      *dest++ = val;
      val = ival;
    }

    if (counter++ == 7) {
      counter = 0;
      bits = *ptr++;
    }
  }
}

static void wc3_decode_frame (wc3video_decoder_t *this) {

  BYTE * ptr;

  part1 = buffer1;
  part4 = buffer2;

  wc3_do_part1(part1, this->buf + LE_16(&this->buf[0]));
  part2 = this->buf + LE_16(&this->buf[2]);
  part3 = this->buf + LE_16(&this->buf[4]);

  ptr = this->buf + LE_16(&this->buf[6]);
  if (*ptr++ == 2)
    wc3_do_part4(part4, ptr);
  else
    part4 = ptr;

  wc3_build_frame(this);
}

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void wc3video_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  wc3video_decoder_t *this = (wc3video_decoder_t *) this_gen;
  palette_entry_t *palette;
  int i;

  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  /* convert the RGB palette to a YUV palette */
  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette = (palette_entry_t *)buf->decoder_info[3];
    for (i = 0; i < buf->decoder_info[2]; i++) {
      this->yuv_palette[i * 4 + 0] =
        COMPUTE_Y(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 1] =
        COMPUTE_U(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 2] =
        COMPUTE_V(palette[i].r, palette[i].g, palette[i].b);
    }
  }

  if (buf->decoder_flags & BUF_FLAG_HEADER) { /* need to initialize */
    this->stream->video_out->open (this->stream->video_out, this->stream);

    if(this->buf)
      free(this->buf);

    this->video_step = buf->decoder_info[1];

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    init_yuv_planes(&this->yuv_planes1, WC3_WIDTH, WC3_HEIGHT);
    init_yuv_planes(&this->yuv_planes2, WC3_WIDTH, WC3_HEIGHT);
    this->current_planes = 1;

    this->decoder_ok = 1;

    /* load the stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = strdup("Wing Commander III Video");
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;

    return;
  } else if (this->decoder_ok) {

    if (this->size + buf->size > this->bufsize) {
      this->bufsize = this->size + 2 * buf->size;
      this->buf = realloc (this->buf, this->bufsize);
    }

    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
      this->video_step = buf->decoder_info[0];

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      img = this->stream->video_out->get_frame (this->stream->video_out,
                                        WC3_WIDTH, WC3_HEIGHT,
                                        42, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      if (this->current_planes == 1) {
        this->current_planes = 2;
        this->current_frame = &this->yuv_planes1;
        this->last_frame = &this->yuv_planes2;
      } else {
        this->current_planes = 1;
        this->current_frame = &this->yuv_planes2;
        this->last_frame = &this->yuv_planes1;
      }

      wc3_decode_frame (this);
      yuv444_to_yuy2(this->current_frame, img->base[0], img->pitches[0]);

      if (img->copy) {
        int height = img->height;
        uint8_t *src[3];

        src[0] = img->base[0];

        while ((height -= 16) >= 0) {
          img->copy(img, src);
          src[0] += 16 * img->pitches[0];
        }
      }

      img->draw(img, this->stream);
      img->free(img);

      this->size = 0;
    }
  }
}

/*
 * This function is called when xine needs to flush the system. Not
 * sure when or if this is used or even if it needs to do anything.
 */
static void wc3video_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void wc3video_reset (video_decoder_t *this_gen) {
  wc3video_decoder_t *this = (wc3video_decoder_t *) this_gen;

  this->size = 0;
}

static void wc3video_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void wc3video_dispose (video_decoder_t *this_gen) {

  wc3video_decoder_t *this = (wc3video_decoder_t *) this_gen;

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  wc3video_decoder_t  *this ;

  this = (wc3video_decoder_t *) xine_xmalloc (sizeof (wc3video_decoder_t));

  this->video_decoder.decode_data         = wc3video_decode_data;
  this->video_decoder.flush               = wc3video_flush;
  this->video_decoder.reset               = wc3video_reset;
  this->video_decoder.discontinuity       = wc3video_discontinuity;
  this->video_decoder.dispose             = wc3video_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (wc3video_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "WC3 Video";
}

static char *get_description (video_decoder_class_t *this) {
  return "Wing Commander III video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  wc3video_class_t *this;

  this = (wc3video_class_t *) xine_xmalloc (sizeof (wc3video_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/* plugin catalog information */
static uint32_t supported_types[] = { BUF_VIDEO_WC3, 0 };

static decoder_info_t video_decoder_info = {
  supported_types,     /* supported types */
  9                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 13, "wc3video", XINE_VERSION_CODE, &video_decoder_info, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
