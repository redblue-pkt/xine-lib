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
 * QT RPZA Video Decoder by Roberto Togni <rtogni@bresciaonline.it>
 * For more information about the RPZA format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: qtrpza.c,v 1.11 2002/12/18 21:35:42 esnel Exp $
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

typedef struct {
  video_decoder_class_t   decoder_class;
} qtrpza_class_t;

typedef struct qtrpza_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  qtrpza_class_t   *class;
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

  yuv_planes_t      yuv_planes;

} qtrpza_decoder_t;

/**************************************************************************
 * RPZA specific decode functions
 *************************************************************************/

#define ADVANCE_BLOCK() \
{ \
  pixel_ptr += 4; \
  if (pixel_ptr >= this->width) \
  { \
    pixel_ptr = 0; \
    row_ptr += this->width * 4; \
  } \
  total_blocks--; \
  if (total_blocks < 0) \
  { \
    printf(_("warning: block counter just went negative (this should not happen)\n")); \
    return; \
  } \
}

#define COLOR_FIX(col_out, col_in) (col_out) = ((col_in) << 3) | ((col_in) >> 2)

/* r, g, and b are defined in the function from which this is called */
#define COLOR_TO_YUV(y_val, u_val, v_val, color) \
{ \
  unsigned short tmp; \
  tmp = (color >> 10) & 0x1f; \
  COLOR_FIX (r, tmp); \
  tmp = (color >> 5) & 0x1f; \
  COLOR_FIX (g, tmp); \
  tmp = color & 0x1f; \
  COLOR_FIX (b, tmp); \
  y_val = COMPUTE_Y(r, g, b); \
  u_val = COMPUTE_U(r, g, b); \
  v_val = COMPUTE_V(r, g, b); \
}

void decode_qtrpza(qtrpza_decoder_t *this) {

  int i;
  int stream_ptr = 0;
  int chunk_size;
  unsigned char opcode;
  int n_blocks;
  unsigned short colorA = 0, colorB;
  unsigned char r, g, b;
  unsigned char y, u, v;
  unsigned char rgb4[4][3];
  unsigned char yuv4[4][3];
  unsigned char index, idx;

  int row_ptr = 0;
  int pixel_ptr = 0;
  int pixel_x, pixel_y;
  int row_inc = this->width - 4;
  int block_ptr;
  int total_blocks;
  unsigned short ta, tb, tt;

  /* First byte is always 0xe1. Warn if it's different */
  if ((unsigned char)this->buf[stream_ptr] != 0xe1)
    printf(_("First chunk byte is 0x%02x instead of 0x1e\n"),
           (unsigned char)this->buf[stream_ptr]);

  /* Get chunk size, ingnoring first byte */
  chunk_size = BE_32(&this->buf[stream_ptr]) & 0x00FFFFFF;
  stream_ptr += 4;

  /* If length mismatch use size from MOV file and try to decode anyway */
  if (chunk_size != this->size)
    printf(_("MOV chunk size != encoded chunk size; using MOV chunk size\n"));

  chunk_size = this->size;

  /* Number of 4x4 blocks in frame. */
  total_blocks = (this->width * this->height) / (4 * 4);

  /* Process chunk data */
  while (stream_ptr < chunk_size) {
    opcode = this->buf[stream_ptr++]; /* Get opcode */

    n_blocks = (opcode & 0x1f) +1; /* Extract block counter from opcode */

    /* If opcode MSbit is 0, we need more data to decide what to do */
    if ((opcode & 0x80) == 0) {
      colorA = (opcode << 8) | ((unsigned char)this->buf[stream_ptr++]);
      opcode = 0;
      if ((this->buf[stream_ptr] & 0x80) != 0) {
        /* Must behave as opcode 110xxxxx, using colorA computed above.*/
        /* Use fake opcode 0x20 to enter switch block at the right place */
        opcode = 0x20;
        n_blocks = 1;
      }
    }

    switch (opcode & 0xe0) {
      /* Skip blocks */
      case 0x80:
        while (n_blocks--)
          ADVANCE_BLOCK();
        break;

      /* Fill blocks with one color */
      case 0xa0:
        colorA = BE_16 (&this->buf[stream_ptr]);
        stream_ptr += 2;
        COLOR_TO_YUV (y, u, v, colorA);
        while (n_blocks--) {
          block_ptr = row_ptr + pixel_ptr;
          for (pixel_y = 0; pixel_y < 4; pixel_y++) {
            for (pixel_x = 0; pixel_x < 4; pixel_x++){
              this->yuv_planes.y[block_ptr] = y;
              this->yuv_planes.u[block_ptr] = u;
              this->yuv_planes.v[block_ptr] = v;
              block_ptr++;
            }
            block_ptr += row_inc;
          }
          ADVANCE_BLOCK();
        }
        break;

      /* Fill blocks with 4 colors */
      case 0xc0:
        colorA = BE_16 (&this->buf[stream_ptr]);
        stream_ptr += 2;
      case 0x20:
        colorB = BE_16 (&this->buf[stream_ptr]);
        stream_ptr += 2;

        /* sort out the colors */
        ta = (colorA >> 10) & 0x1f;
        tb = (colorB >> 10) & 0x1f;
        COLOR_FIX (rgb4[3][0], ta);
        COLOR_FIX (rgb4[0][0], tb);
        tt = (11 * ta + 21 * tb) >> 5;
        COLOR_FIX (rgb4[1][0], tt);
        tt = (21 * ta + 11 * tb) >> 5;
        COLOR_FIX (rgb4[2][0], tt);
        ta = (colorA >> 5) & 0x1f;
        tb = (colorB >> 5) & 0x1f;
        COLOR_FIX (rgb4[3][1], ta);
        COLOR_FIX (rgb4[0][1], tb);
        tt = (11 * ta + 21 * tb) >> 5;
        COLOR_FIX (rgb4[1][1], tt);
        tt = (21 * ta + 11 * tb) >> 5;
        COLOR_FIX (rgb4[2][1], tt);
        ta = colorA & 0x1f;
        tb = colorB & 0x1f;
        COLOR_FIX (rgb4[3][2], ta);
        COLOR_FIX (rgb4[0][2], tb);
        tt = (11 * ta + 21 * tb) >> 5;
        COLOR_FIX (rgb4[1][2], tt);
        tt = (21 * ta + 11 * tb) >> 5;
        COLOR_FIX (rgb4[2][2], tt);

        /* RGB -> YUV */
        for (i = 0; i < 4; i++) {
          yuv4[i][0] = COMPUTE_Y(rgb4[i][0], rgb4[i][1], rgb4[i][2]);
          yuv4[i][1] = COMPUTE_U(rgb4[i][0], rgb4[i][1], rgb4[i][2]);
          yuv4[i][2] = COMPUTE_V(rgb4[i][0], rgb4[i][1], rgb4[i][2]);
        }

        while (n_blocks--) {
          block_ptr = row_ptr + pixel_ptr;
          for (pixel_y = 0; pixel_y < 4; pixel_y++) {
            index = this->buf[stream_ptr++];
            for (pixel_x = 0; pixel_x < 4; pixel_x++){
              idx = (index >> (2 * (3 - pixel_x))) & 0x03;
              this->yuv_planes.y[block_ptr] = yuv4[idx][0];
              this->yuv_planes.u[block_ptr] = yuv4[idx][1];
              this->yuv_planes.v[block_ptr] = yuv4[idx][2];
              block_ptr++;
            }
            block_ptr += row_inc;
          }
          ADVANCE_BLOCK();
        }
        break;
        
      /* Fill block with 16 colors */
      case 0x00:
        block_ptr = row_ptr + pixel_ptr;
        for (pixel_y = 0; pixel_y < 4; pixel_y++) {
          for (pixel_x = 0; pixel_x < 4; pixel_x++){
            /* We already have color of upper left pixel */
            if ((pixel_y != 0) || (pixel_x !=0)) {
              colorA = BE_16 (&this->buf[stream_ptr]);
              stream_ptr += 2;
            }
            COLOR_TO_YUV (y, u, v, colorA);
            this->yuv_planes.y[block_ptr] = y;
            this->yuv_planes.u[block_ptr] = u;
            this->yuv_planes.v[block_ptr] = v;
            block_ptr++;
          }
          block_ptr += row_inc;
        }
        ADVANCE_BLOCK();
        break;
        
      /* Unknown opcode */
      default:
        printf(_("Unknown opcode %d in rpza chunk."
               " Skip remaining %d bytes of chunk data.\n"), opcode,
               chunk_size - stream_ptr);
        return;
    } /* Opcode switch */

  }
}

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void qtrpza_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  qtrpza_decoder_t *this = (qtrpza_decoder_t *) this_gen;
  xine_bmiheader *bih;

  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) { /* need to initialize */
    this->stream->video_out->open (this->stream->video_out, this->stream);

    if(this->buf)
      free(this->buf);

    bih = (xine_bmiheader *) buf->content;
    this->width = (bih->biWidth + 3) & ~0x03;
    this->height = (bih->biHeight + 3) & ~0x03;
    this->video_step = buf->decoder_info[1];

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    init_yuv_planes(&this->yuv_planes, this->width, this->height);

    this->stream->video_out->open (this->stream->video_out, this->stream);
    this->decoder_ok = 1;

    /* load the stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = strdup("Quicktime Video (RPZA)");
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;

    return;
  } else if (this->decoder_ok && !(buf->decoder_flags & BUF_FLAG_SPECIAL)) {

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
                                        42, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      decode_qtrpza(this);
      yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

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
static void qtrpza_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void qtrpza_reset (video_decoder_t *this_gen) {
  qtrpza_decoder_t *this = (qtrpza_decoder_t *) this_gen;

  this->size = 0;
}

static void qtrpza_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void qtrpza_dispose (video_decoder_t *this_gen) {

  qtrpza_decoder_t *this = (qtrpza_decoder_t *) this_gen;

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

  qtrpza_decoder_t  *this ;

  this = (qtrpza_decoder_t *) xine_xmalloc (sizeof (qtrpza_decoder_t));

  this->video_decoder.decode_data         = qtrpza_decode_data;
  this->video_decoder.flush               = qtrpza_flush;
  this->video_decoder.reset               = qtrpza_reset;
  this->video_decoder.discontinuity       = qtrpza_discontinuity;
  this->video_decoder.dispose             = qtrpza_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (qtrpza_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "QT RPZA";
}

static char *get_description (video_decoder_class_t *this) {
  return "Quicktime Video (RPZA) decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  qtrpza_class_t *this;

  this = (qtrpza_class_t *) xine_xmalloc (sizeof (qtrpza_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/* plugin catalog information */
static uint32_t supported_types[] = { BUF_VIDEO_RPZA, 0 };

static decoder_info_t video_decoder_info = {
  supported_types,     /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 13, "rpza", XINE_VERSION_CODE, &video_decoder_info, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
