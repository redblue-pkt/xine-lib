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
 * QT RLE Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information on the QT RLE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 * 
 * $Id: qtrle.c,v 1.7 2002/11/20 11:57:47 mroi Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "xineutils.h"
#include "bswap.h"

#define VIDEOBUFSIZE 128*1024

typedef struct {
  video_decoder_class_t   decoder_class;
} qtrle_class_t;

typedef struct qtrle_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  qtrle_class_t    *class;
  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object */
  uint64_t          video_step;  /* frame duration in pts units */
  int               decoder_ok;  /* current decoder status */
  int               skipframes;

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */

  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */
  int               depth;       /* color depth (bits/pixel) */

  unsigned char     yuv_palette[256 * 4];
  yuv_planes_t      yuv_planes;
  
} qtrle_decoder_t;

/**************************************************************************
 * QT RLE specific decode functions
 *************************************************************************/

/* monochrome color definitions */
#define Y_BLACK COMPUTE_Y(0x00, 0x00, 0x00)
#define U_BLACK COMPUTE_U(0x00, 0x00, 0x00)
#define V_BLACK COMPUTE_V(0x00, 0x00, 0x00)
#define Y_WHITE COMPUTE_Y(0xFF, 0xFF, 0xFF)
#define U_WHITE COMPUTE_U(0xFF, 0xFF, 0xFF)
#define V_WHITE COMPUTE_V(0xFF, 0xFF, 0xFF)

static void decode_qtrle_1(qtrle_decoder_t *this) {

  int stream_ptr;
  int header;
  int start_line;
  int lines_to_change;
  signed char rle_code;
  int row_ptr, pixel_ptr;
  int row_inc = this->width;
  unsigned char y[16], u[16], v[16];
  yuv_planes_t *yuv = &this->yuv_planes;
  int i, shift, index, indices;

  /* check if this frame is even supposed to change */
  if (this->size < 8)
    return;

  /* start after the chunk size */
  stream_ptr = 4;

  /* fetch the header */
  header = BE_16(&this->buf[stream_ptr]);
  stream_ptr += 2;

  /* if a header is present, fetch additional decoding parameters */
  if (header & 0x0008) {
    start_line = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
    lines_to_change = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
  } else {
    start_line = 0;
    lines_to_change = this->height;
  }

  row_ptr = row_inc * start_line;
  while (lines_to_change--) {
//printf ("%d lines left, skip %d bytes...\n", lines_to_change, this->buf[stream_ptr]);
    pixel_ptr = row_ptr + (this->buf[stream_ptr++] - 1);

    while ((rle_code = (signed char)this->buf[stream_ptr++]) != -1) {
      if (rle_code == 0)
        /* there's another skip code in the stream */
        pixel_ptr += (this->buf[stream_ptr++] - 1);
      else if (rle_code < 0) {
        /* decode the run length code */
        rle_code = -rle_code;

        /* get the next 16 bits from the stream and treat them as 16
         * monochrome pixels */
        indices = BE_16(&this->buf[stream_ptr]);
        stream_ptr += 2;
        for (i = 0, shift = 15; i < 16; i++, shift--) {
          index = (indices >> shift) & 0x01;
          if (index) {
            y[i] = Y_WHITE;
            u[i] = U_WHITE;
            v[i] = V_WHITE;
          } else {
            y[i] = Y_BLACK;
            u[i] = U_BLACK;
            v[i] = V_BLACK;
          }
        }
        while (rle_code--) {
          for (i = 0; i < 16; i++) {
            yuv->y[pixel_ptr] = y[i];
            yuv->u[pixel_ptr] = u[i];
            yuv->v[pixel_ptr] = v[i];
            pixel_ptr++;
          }
        }
      } else {
        /* copy pixels directly to output */
        while (rle_code--) {
          indices = BE_16(&this->buf[stream_ptr]);
          stream_ptr += 2;
          for (i = 0, shift = 15; i < 16; i++, shift--) {
            index = (indices >> shift) & 0x01;
            if (index) {
              yuv->y[pixel_ptr] = Y_WHITE;
              yuv->u[pixel_ptr] = U_WHITE;
              yuv->v[pixel_ptr] = V_WHITE;
            } else {
              yuv->y[pixel_ptr] = Y_BLACK;
              yuv->u[pixel_ptr] = U_BLACK;
              yuv->v[pixel_ptr] = V_BLACK;
            }
            pixel_ptr++;
          }
        }
      }
    }

    row_ptr += row_inc;
  }
}

static void decode_qtrle_2(qtrle_decoder_t *this) {

  int stream_ptr;
  int header;
  int start_line;
  int lines_to_change;
  signed char rle_code;
  int row_ptr, pixel_ptr;
  int row_inc = this->width;
  unsigned char y[16], u[16], v[16];
  yuv_planes_t *yuv = &this->yuv_planes;
  int i, shift, index, indices;

  /* check if this frame is even supposed to change */
  if (this->size < 8)
    return;

  /* start after the chunk size */
  stream_ptr = 4;

  /* fetch the header */
  header = BE_16(&this->buf[stream_ptr]);
  stream_ptr += 2;

  /* if a header is present, fetch additional decoding parameters */
  if (header & 0x0008) {
    start_line = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
    lines_to_change = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
  } else {
    start_line = 0;
    lines_to_change = this->height;
  }

  row_ptr = row_inc * start_line;
  while (lines_to_change--) {
    pixel_ptr = row_ptr + (this->buf[stream_ptr++] - 1);

    while ((rle_code = (signed char)this->buf[stream_ptr++]) != -1) {
      if (rle_code == 0)
        /* there's another skip code in the stream */
        pixel_ptr += (this->buf[stream_ptr++] - 1);
      else if (rle_code < 0) {
        /* decode the run length code */
        rle_code = -rle_code;

        /* get the next 32 bits from the stream and treat them as 16
         * 2-bit indices into the palette */
        indices = BE_32(&this->buf[stream_ptr]);
        stream_ptr += 4;
        for (i = 0, shift = 30; i < 16; i++, shift -= 2) {
          index = (indices >> shift) & 0x03;
          y[i] = this->yuv_palette[index * 4 + 0];
          u[i] = this->yuv_palette[index * 4 + 1];
          v[i] = this->yuv_palette[index * 4 + 2];
        }
        while (rle_code--) {
          for (i = 0; i < 16; i++) {
            yuv->y[pixel_ptr] = y[i];
            yuv->u[pixel_ptr] = u[i];
            yuv->v[pixel_ptr] = v[i];
            pixel_ptr++;
          }
        }
      } else {
        /* copy pixels directly to output */
        while (rle_code--) {
          indices = BE_32(&this->buf[stream_ptr]);
          stream_ptr += 4;
          for (i = 0, shift = 30; i < 16; i++, shift -= 2) {
            index = (indices >> shift) & 0x03;
            yuv->y[pixel_ptr] = this->yuv_palette[index * 4 + 0];
            yuv->u[pixel_ptr] = this->yuv_palette[index * 4 + 1];
            yuv->v[pixel_ptr] = this->yuv_palette[index * 4 + 2];
            pixel_ptr++;
          }
        }
      }
    }

    row_ptr += row_inc;
  }
}

static void decode_qtrle_4(qtrle_decoder_t *this) {

  int stream_ptr;
  int header;
  int start_line;
  int lines_to_change;
  signed char rle_code;
  int row_ptr, pixel_ptr;
  int row_inc = this->width;
  unsigned char y[8], u[8], v[8];
  yuv_planes_t *yuv = &this->yuv_planes;
  int i, shift, index, indices;

  /* check if this frame is even supposed to change */
  if (this->size < 8)
    return;

  /* start after the chunk size */
  stream_ptr = 4;

  /* fetch the header */
  header = BE_16(&this->buf[stream_ptr]);
  stream_ptr += 2;

  /* if a header is present, fetch additional decoding parameters */
  if (header & 0x0008) {
    start_line = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
    lines_to_change = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
  } else {
    start_line = 0;
    lines_to_change = this->height;
  }

  row_ptr = row_inc * start_line;
  while (lines_to_change--) {
    pixel_ptr = row_ptr + (this->buf[stream_ptr++] - 1);

    while ((rle_code = (signed char)this->buf[stream_ptr++]) != -1) {
      if (rle_code == 0)
        /* there's another skip code in the stream */
        pixel_ptr += (this->buf[stream_ptr++] - 1);
      else if (rle_code < 0) {
        /* decode the run length code */
        rle_code = -rle_code;

        /* get the next 32 bits from the stream and treat them as 8
         * 4-bit indices into the palette */
        indices = BE_32(&this->buf[stream_ptr]);
        stream_ptr += 4;
        for (i = 0, shift = 28; i < 8; i++, shift -= 4) {
          index = (indices >> shift) & 0x0F;
          y[i] = this->yuv_palette[index * 4 + 0];
          u[i] = this->yuv_palette[index * 4 + 1];
          v[i] = this->yuv_palette[index * 4 + 2];
        }
        while (rle_code--) {
          for (i = 0; i < 8; i++) {
            yuv->y[pixel_ptr] = y[i];
            yuv->u[pixel_ptr] = u[i];
            yuv->v[pixel_ptr] = v[i];
            pixel_ptr++;
          }
        }
      } else {
        /* copy pixels directly to output */
        while (rle_code--) {
          indices = BE_32(&this->buf[stream_ptr]);
          stream_ptr += 4;
          for (i = 0, shift = 28; i < 8; i++, shift -= 4) {
            index = (indices >> shift) & 0x0F;
            yuv->y[pixel_ptr] = this->yuv_palette[index * 4 + 0];
            yuv->u[pixel_ptr] = this->yuv_palette[index * 4 + 1];
            yuv->v[pixel_ptr] = this->yuv_palette[index * 4 + 2];
            pixel_ptr++;
          }
        }
      }
    }

    row_ptr += row_inc;
  }
}

static void decode_qtrle_8(qtrle_decoder_t *this) {

  int stream_ptr;
  int header;
  int start_line;
  int lines_to_change;
  signed char rle_code;
  int row_ptr, pixel_ptr;
  int row_inc = this->width;
  unsigned char y[4], u[4], v[4];
  yuv_planes_t *yuv = &this->yuv_planes;
  int i;

  /* check if this frame is even supposed to change */
  if (this->size < 8)
    return;

  /* start after the chunk size */
  stream_ptr = 4;

  /* fetch the header */
  header = BE_16(&this->buf[stream_ptr]);
  stream_ptr += 2;

  /* if a header is present, fetch additional decoding parameters */
  if (header & 0x0008) {
    start_line = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
    lines_to_change = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
  } else {
    start_line = 0;
    lines_to_change = this->height;
  }

  row_ptr = row_inc * start_line;
  while (lines_to_change--) {
    pixel_ptr = row_ptr + (this->buf[stream_ptr++] - 1);

    while ((rle_code = (signed char)this->buf[stream_ptr++]) != -1) {
      if (rle_code == 0)
        /* there's another skip code in the stream */
        pixel_ptr += (this->buf[stream_ptr++] - 1);
      else if (rle_code < 0) {
        /* decode the run length code */
        rle_code = -rle_code;
        /* get the next 4 bytes from the stream, treat them as palette
         * indices, and output them to the rle_code times */
        for (i = 0; i < 4; i++) {
          y[i] = this->yuv_palette[this->buf[stream_ptr] * 4 + 0];
          u[i] = this->yuv_palette[this->buf[stream_ptr] * 4 + 1];
          v[i] = this->yuv_palette[this->buf[stream_ptr] * 4 + 2];
          stream_ptr++;
        }
        while (rle_code--) {
          for (i = 0; i < 4; i++) {
            yuv->y[pixel_ptr] = y[i];
            yuv->u[pixel_ptr] = u[i];
            yuv->v[pixel_ptr] = v[i];
            pixel_ptr++;
          }
        }
      } else {
        /* copy pixels directly to output */
        while (rle_code--) {
          for (i = 0; i < 4; i++) {
            yuv->y[pixel_ptr] =
              this->yuv_palette[this->buf[stream_ptr] * 4 + 0];
            yuv->u[pixel_ptr] =
              this->yuv_palette[this->buf[stream_ptr] * 4 + 1];
            yuv->v[pixel_ptr] =
              this->yuv_palette[this->buf[stream_ptr] * 4 + 2];
            stream_ptr++;
            pixel_ptr++;
          }
        }
      }
    }

    row_ptr += row_inc;
  }
}

static void decode_qtrle_16(qtrle_decoder_t *this) {

  int stream_ptr;
  int header;
  int start_line;
  int lines_to_change;
  signed char rle_code;
  int row_ptr, pixel_ptr;
  int row_inc = this->width;
  unsigned char r, g, b;
  unsigned char y, u, v;
  unsigned short packed_pixel;
  yuv_planes_t *yuv = &this->yuv_planes;

  /* check if this frame is even supposed to change */
  if (this->size < 8)
    return;

  /* start after the chunk size */
  stream_ptr = 4;

  /* fetch the header */
  header = BE_16(&this->buf[stream_ptr]);
  stream_ptr += 2;

  /* if a header is present, fetch additional decoding parameters */
  if (header & 0x0008) {
    start_line = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
    lines_to_change = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
  } else {
    start_line = 0;
    lines_to_change = this->height;
  }

  row_ptr = row_inc * start_line;
  while (lines_to_change--) {
    pixel_ptr = row_ptr + (this->buf[stream_ptr++] - 1);

    while ((rle_code = (signed char)this->buf[stream_ptr++]) != -1) {
      if (rle_code == 0)
        /* there's another skip code in the stream */
        pixel_ptr += (this->buf[stream_ptr++] - 1);
      else if (rle_code < 0) {
        /* decode the run length code */
        rle_code = -rle_code;
        packed_pixel = BE_16(&this->buf[stream_ptr]);
        stream_ptr += 2;
        UNPACK_RGB15(packed_pixel, r, g, b);
        y = COMPUTE_Y(r, g, b);
        u = COMPUTE_U(r, g, b);
        v = COMPUTE_V(r, g, b);
        while (rle_code--) {
          yuv->y[pixel_ptr] = y;
          yuv->u[pixel_ptr] = u;
          yuv->v[pixel_ptr] = v;
          pixel_ptr++;
        }
      } else {
        /* copy pixels directly to output */
        while (rle_code--) {
          packed_pixel = BE_16(&this->buf[stream_ptr]);
          stream_ptr += 2;
          UNPACK_RGB15(packed_pixel, r, g, b);
          y = COMPUTE_Y(r, g, b);
          u = COMPUTE_U(r, g, b);
          v = COMPUTE_V(r, g, b);
          yuv->y[pixel_ptr] = y;
          yuv->u[pixel_ptr] = u;
          yuv->v[pixel_ptr] = v;
          pixel_ptr++;
        }
      }
    }

    row_ptr += row_inc;
  }
}

static void decode_qtrle_24(qtrle_decoder_t *this) {

  int stream_ptr;
  int header;
  int start_line;
  int lines_to_change;
  signed char rle_code;
  int row_ptr, pixel_ptr;
  int row_inc = this->width;
  unsigned char r, g, b;
  unsigned char y, u, v;
  yuv_planes_t *yuv = &this->yuv_planes;

  /* check if this frame is even supposed to change */
  if (this->size < 8)
    return;

  /* start after the chunk size */
  stream_ptr = 4;

  /* fetch the header */
  header = BE_16(&this->buf[stream_ptr]);
  stream_ptr += 2;

  /* if a header is present, fetch additional decoding parameters */
  if (header & 0x0008) {
    start_line = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
    lines_to_change = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
  } else {
    start_line = 0;
    lines_to_change = this->height;
  }

  row_ptr = row_inc * start_line;
  while (lines_to_change--) {
    pixel_ptr = row_ptr + (this->buf[stream_ptr++] - 1);

    while ((rle_code = (signed char)this->buf[stream_ptr++]) != -1) {
      if (rle_code == 0)
        /* there's another skip code in the stream */
        pixel_ptr += (this->buf[stream_ptr++] - 1);
      else if (rle_code < 0) {
        /* decode the run length code */
        rle_code = -rle_code;
        r = this->buf[stream_ptr++];
        g = this->buf[stream_ptr++];
        b = this->buf[stream_ptr++];
        y = COMPUTE_Y(r, g, b);
        u = COMPUTE_U(r, g, b);
        v = COMPUTE_V(r, g, b);
        while (rle_code--) {
          yuv->y[pixel_ptr] = y;
          yuv->u[pixel_ptr] = u;
          yuv->v[pixel_ptr] = v;
          pixel_ptr++;
        }
      } else {
        /* copy pixels directly to output */
        while (rle_code--) {
          r = this->buf[stream_ptr++];
          g = this->buf[stream_ptr++];
          b = this->buf[stream_ptr++];
          y = COMPUTE_Y(r, g, b);
          u = COMPUTE_U(r, g, b);
          v = COMPUTE_V(r, g, b);
          yuv->y[pixel_ptr] = y;
          yuv->u[pixel_ptr] = u;
          yuv->v[pixel_ptr] = v;
          pixel_ptr++;
        }
      }
    }

    row_ptr += row_inc;
  }
}

static void decode_qtrle_32(qtrle_decoder_t *this) {

  int stream_ptr;
  int header;
  int start_line;
  int lines_to_change;
  signed char rle_code;
  int row_ptr, pixel_ptr;
  int row_inc = this->width;
  unsigned char r, g, b;
  unsigned char y, u, v;
  yuv_planes_t *yuv = &this->yuv_planes;

  /* check if this frame is even supposed to change */
  if (this->size < 8)
    return;

  /* start after the chunk size */
  stream_ptr = 4;

  /* fetch the header */
  header = BE_16(&this->buf[stream_ptr]);
  stream_ptr += 2;

  /* if a header is present, fetch additional decoding parameters */
  if (header & 0x0008) {
    start_line = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
    lines_to_change = BE_16(&this->buf[stream_ptr]);
    stream_ptr += 4;
  } else {
    start_line = 0;
    lines_to_change = this->height;
  }

  row_ptr = row_inc * start_line;
  while (lines_to_change--) {
    pixel_ptr = row_ptr + (this->buf[stream_ptr++] - 1);

    while ((rle_code = (signed char)this->buf[stream_ptr++]) != -1) {
      if (rle_code == 0)
        /* there's another skip code in the stream */
        pixel_ptr += (this->buf[stream_ptr++] - 1);
      else if (rle_code < 0) {
        /* decode the run length code */
        rle_code = -rle_code;
        stream_ptr++;  /* skip alpha transparency (?) byte */
        r = this->buf[stream_ptr++];
        g = this->buf[stream_ptr++];
        b = this->buf[stream_ptr++];
        y = COMPUTE_Y(r, g, b);
        u = COMPUTE_U(r, g, b);
        v = COMPUTE_V(r, g, b);
        while (rle_code--) {
          yuv->y[pixel_ptr] = y;
          yuv->u[pixel_ptr] = u;
          yuv->v[pixel_ptr] = v;
          pixel_ptr++;
        }
      } else {
        /* copy pixels directly to output */
        while (rle_code--) {
          stream_ptr++;  /* skip alpha transparency (?) byte */
          r = this->buf[stream_ptr++];
          g = this->buf[stream_ptr++];
          b = this->buf[stream_ptr++];
          y = COMPUTE_Y(r, g, b);
          u = COMPUTE_U(r, g, b);
          v = COMPUTE_V(r, g, b);
          yuv->y[pixel_ptr] = y;
          yuv->u[pixel_ptr] = u;
          yuv->v[pixel_ptr] = v;
          pixel_ptr++;
        }
      }
    }

    row_ptr += row_inc;
  }
}

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void qtrle_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  qtrle_decoder_t *this = (qtrle_decoder_t *) this_gen;
  xine_bmiheader *bih;
  palette_entry_t *palette;
  int i;

  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

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

    bih = (xine_bmiheader *) buf->content;
    this->width = (bih->biWidth + 3) & ~0x03;
    this->height = (bih->biHeight + 3) & ~0x03;
    this->depth = bih->biBitCount;
    this->video_step = buf->decoder_info[1];

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    init_yuv_planes(&this->yuv_planes, this->width, this->height);

    this->decoder_ok = 1;

    /* load the stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = strdup("Quicktime Animation (RLE)");
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
                                        this->width, this->height,
                                        XINE_VO_ASPECT_DONT_TOUCH,
                                        XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

      switch (this->depth & 0x1F) {

        case 1:
          decode_qtrle_1(this);
          break;

        case 2:
          decode_qtrle_2(this);
          break;

        case 4:
          decode_qtrle_4(this);
          break;

        case 8:
          decode_qtrle_8(this);
          break;

        case 16:
          decode_qtrle_16(this);
          break;

        case 24:
          decode_qtrle_24(this);
          break;

        case 32:
          decode_qtrle_32(this);
          break;

      }

      yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

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
static void qtrle_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void qtrle_reset (video_decoder_t *this_gen) {
  qtrle_decoder_t *this = (qtrle_decoder_t *) this_gen;

  this->size = 0;
}

static void qtrle_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void qtrle_dispose (video_decoder_t *this_gen) {

  qtrle_decoder_t *this = (qtrle_decoder_t *) this_gen;

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

  qtrle_decoder_t  *this ;

  this = (qtrle_decoder_t *) xine_xmalloc (sizeof (qtrle_decoder_t));

  this->video_decoder.decode_data         = qtrle_decode_data;
  this->video_decoder.flush               = qtrle_flush;
  this->video_decoder.reset               = qtrle_reset;
  this->video_decoder.discontinuity       = qtrle_discontinuity;
  this->video_decoder.dispose             = qtrle_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (qtrle_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "QT RLE";
}

static char *get_description (video_decoder_class_t *this) {
  return "Quicktime Animation (RLE) video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  qtrle_class_t *this;

  this = (qtrle_class_t *) xine_xmalloc (sizeof (qtrle_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/*
 * exported plugin catalog entry
 */
static uint32_t video_types[] = { 
  BUF_VIDEO_QTRLE,
  0
};

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  5                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 13, "qtrle", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
