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
 * Apple Graphics (SMC) Decoder by Mike Melanson (melanson@pcisys.net)
 * Special thanks to Roberto Togni <rtogni@bresciaonline.it> for tracking
 * down the final, nagging bugs.
 * For more information on the SMC format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 * 
 * $Id: qtsmc.c,v 1.8 2002/11/12 18:40:54 miguelfreitas Exp $
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

#define COLORS_PER_TABLE 256
#define BYTES_PER_COLOR 4

#define CPAIR 2
#define CQUAD 4
#define COCTET 8

typedef struct {
  video_decoder_class_t   decoder_class;
} qtsmc_class_t;

typedef struct qtsmc_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  qtsmc_class_t    *class;
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

  /* SMC color tables */
  unsigned char     color_pairs[COLORS_PER_TABLE * BYTES_PER_COLOR * CPAIR];
  unsigned char     color_quads[COLORS_PER_TABLE * BYTES_PER_COLOR * CQUAD];
  unsigned char     color_octets[COLORS_PER_TABLE * BYTES_PER_COLOR * COCTET];

  unsigned char     yuv_palette[256 * 4];
  yuv_planes_t      yuv_planes;
  
} qtsmc_decoder_t;

/**************************************************************************
 * SMC specific decode functions
 *************************************************************************/

#define GET_BLOCK_COUNT \
  (opcode & 0x10) ? (1 + this->buf[stream_ptr++]) : 1 + (opcode & 0x0F);
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

void decode_qtsmc(qtsmc_decoder_t *this) {

  int i;
  int stream_ptr = 0;
  int chunk_size;
  unsigned char opcode;
  int n_blocks;
  unsigned int color_flags;
  unsigned int color_flags_a;
  unsigned int color_flags_b;
  unsigned int flag_mask;

  yuv_planes_t *yuv = &this->yuv_planes;

  int image_size = this->height * this->width;
  int row_ptr = 0;
  int pixel_ptr = 0;
  int pixel_x, pixel_y;
  int row_inc = this->width - 4;
  int block_ptr;
  int prev_block_ptr;
  int prev_block_ptr1, prev_block_ptr2;
  int prev_block_flag;
  int total_blocks;
  int color_table_index;  /* indexes to color pair, quad, or octet tables */
  int color_index;  /* indexes into palette map */

  int color_pair_index = 0;
  int color_quad_index = 0;
  int color_octet_index = 0;

  chunk_size = BE_32(&this->buf[stream_ptr]) & 0x00FFFFFF;
  stream_ptr += 4;
  if (chunk_size != this->size)
    printf(_("warning: MOV chunk size != encoded chunk size (%d != %d); using MOV chunk size\n"),
      chunk_size, this->size);

  chunk_size = this->size;
  total_blocks = (this->width * this->height) / (4 * 4);

  /* traverse through the blocks */
  while (total_blocks) {
    /* sanity checks */
    /* make sure stream ptr hasn't gone out of bounds */
    if (stream_ptr > chunk_size) {
      printf(_(
        "SMC decoder just went out of bounds (stream ptr = %d, chunk size = %d)\n"),
        stream_ptr, chunk_size);
      return;
    }
    /* make sure the row pointer hasn't gone wild */
    if (row_ptr >= image_size) {
      printf(_(
        "SMC decoder just went out of bounds (row ptr = %d, height = %d)\n"),
        row_ptr, image_size);
      return;
    }

    opcode = this->buf[stream_ptr++];
    switch (opcode & 0xF0) {
    /* skip n blocks */
    case 0x00:
    case 0x10:
      n_blocks = GET_BLOCK_COUNT;
      while (n_blocks--)
        ADVANCE_BLOCK();
      break;

    /* repeat last block n times */
    case 0x20:
    case 0x30:
      n_blocks = GET_BLOCK_COUNT;

      /* sanity check */
      if ((row_ptr == 0) && (pixel_ptr == 0)) {
        printf(_(
          "encountered repeat block opcode (%02X) but no blocks rendered yet\n"),
          opcode & 0xF0);
        break;
      }

      /* figure out where the previous block started */
      if (pixel_ptr == 0)
        prev_block_ptr1 = (row_ptr - this->width * 4) + this->width - 4;
      else
        prev_block_ptr1 = row_ptr + pixel_ptr - 4;

      while (n_blocks--) {
        block_ptr = row_ptr + pixel_ptr;
        prev_block_ptr = prev_block_ptr1;
        for (pixel_y = 0; pixel_y < 4; pixel_y++) {
          for (pixel_x = 0; pixel_x < 4; pixel_x++) {
            yuv->y[block_ptr] = yuv->y[prev_block_ptr];
            yuv->u[block_ptr] = yuv->u[prev_block_ptr];
            yuv->v[block_ptr] = yuv->v[prev_block_ptr];
            block_ptr++;
            prev_block_ptr++;
          }
          block_ptr += row_inc;
          prev_block_ptr += row_inc;
        }
        ADVANCE_BLOCK();
      }
      break;

    /* repeat previous pair of blocks n times */
    case 0x40:
    case 0x50:
      n_blocks = GET_BLOCK_COUNT;
      n_blocks *= 2;

      /* sanity check */
      if ((row_ptr == 0) && (pixel_ptr < 2 * 4)) {
        printf(_(
          "encountered repeat block opcode (%02X) but not enough blocks rendered yet\n"),
          opcode & 0xF0);
        break;
      }

      /* figure out where the previous 2 blocks started */
      if (pixel_ptr == 0)
        prev_block_ptr1 = (row_ptr - this->width * 4) + 
          this->width - 4 * 2;
      else if (pixel_ptr == 4)
        prev_block_ptr1 = (row_ptr - this->width * 4) + row_inc;
      else
        prev_block_ptr1 = row_ptr + pixel_ptr - 4 * 2;

      if (pixel_ptr == 0)
        prev_block_ptr2 = (row_ptr - this->width * 4) + row_inc;
      else
        prev_block_ptr2 = row_ptr + pixel_ptr - 4;

      prev_block_flag = 0;
      while (n_blocks--) {
        block_ptr = row_ptr + pixel_ptr;
        if (prev_block_flag)
          prev_block_ptr = prev_block_ptr2;
        else
          prev_block_ptr = prev_block_ptr1;
        prev_block_flag = !prev_block_flag;

        for (pixel_y = 0; pixel_y < 4; pixel_y++) {
          for (pixel_x = 0; pixel_x < 4; pixel_x++) {
            yuv->y[block_ptr] = yuv->y[prev_block_ptr];
            yuv->u[block_ptr] = yuv->u[prev_block_ptr];
            yuv->v[block_ptr] = yuv->v[prev_block_ptr];
            block_ptr++;
            prev_block_ptr++;
          }
          block_ptr += row_inc;
          prev_block_ptr += row_inc;
        }
        ADVANCE_BLOCK();
      }
      break;

    /* 1-color block encoding */
    case 0x60:
    case 0x70:
      n_blocks = GET_BLOCK_COUNT;
      color_index = this->buf[stream_ptr++] * 4;

      while (n_blocks--) {
        block_ptr = row_ptr + pixel_ptr;
        for (pixel_y = 0; pixel_y < 4; pixel_y++) {
          for (pixel_x = 0; pixel_x < 4; pixel_x++) {
            yuv->y[block_ptr] = this->yuv_palette[color_index + 0];
            yuv->u[block_ptr] = this->yuv_palette[color_index + 1];
            yuv->v[block_ptr] = this->yuv_palette[color_index + 2];
            block_ptr++;
          }
          block_ptr += row_inc;
        }
        ADVANCE_BLOCK();
      }
      break;

    /* 2-color block encoding */
    case 0x80:
    case 0x90:
      n_blocks = (opcode & 0x0F) + 1;

      /* figure out which color pair to use to paint the 2-color block */
      if ((opcode & 0xF0) == 0x80) {
        /* fetch the next 2 colors from bytestream and store in next
         * available entry in the color pair table */
        for (i = 0; i < CPAIR; i++) {
          color_index = this->buf[stream_ptr++] * BYTES_PER_COLOR;
          color_table_index = CPAIR * BYTES_PER_COLOR * color_pair_index + 
            (i * BYTES_PER_COLOR);
          this->color_pairs[color_table_index + 0] = 
            this->yuv_palette[color_index + 0];
          this->color_pairs[color_table_index + 1] = 
            this->yuv_palette[color_index + 1];
          this->color_pairs[color_table_index + 2] = 
            this->yuv_palette[color_index + 2];
        }
        /* this is the base index to use for this block */
        color_table_index = CPAIR * BYTES_PER_COLOR * color_pair_index;
        color_pair_index++;
        /* wraparound */
        if (color_pair_index == COLORS_PER_TABLE)
          color_pair_index = 0;
      }
      else
        color_table_index = CPAIR * BYTES_PER_COLOR * this->buf[stream_ptr++];

      while (n_blocks--) {
        color_flags = BE_16(&this->buf[stream_ptr]);
        stream_ptr += 2;
        flag_mask = 0x8000;
        block_ptr = row_ptr + pixel_ptr;
        for (pixel_y = 0; pixel_y < 4; pixel_y++) {
          for (pixel_x = 0; pixel_x < 4; pixel_x++) {
            if (color_flags & flag_mask)
              color_index = color_table_index + BYTES_PER_COLOR;
            else
              color_index = color_table_index;
            flag_mask >>= 1;

            yuv->y[block_ptr] = this->color_pairs[color_index + 0];
            yuv->u[block_ptr] = this->color_pairs[color_index + 1];
            yuv->v[block_ptr] = this->color_pairs[color_index + 2];
            block_ptr++;
          }
          block_ptr += row_inc;
        }
        ADVANCE_BLOCK();
      }
      break;

    /* 4-color block encoding */
    case 0xA0:
    case 0xB0:
      n_blocks = (opcode & 0x0F) + 1;

      /* figure out which color quad to use to paint the 4-color block */
      if ((opcode & 0xF0) == 0xA0) {
        /* fetch the next 4 colors from bytestream and store in next
         * available entry in the color quad table */
        for (i = 0; i < CQUAD; i++) {
          color_index = this->buf[stream_ptr++] * BYTES_PER_COLOR;
          color_table_index = CQUAD * BYTES_PER_COLOR * color_quad_index + 
            (i * BYTES_PER_COLOR);
          this->color_quads[color_table_index + 0] = 
            this->yuv_palette[color_index + 0];
          this->color_quads[color_table_index + 1] = 
            this->yuv_palette[color_index + 1];
          this->color_quads[color_table_index + 2] = 
            this->yuv_palette[color_index + 2];
        }
        /* this is the base index to use for this block */
        color_table_index = CQUAD * BYTES_PER_COLOR * color_quad_index;
        color_quad_index++;
        /* wraparound */
        if (color_quad_index == COLORS_PER_TABLE)
          color_quad_index = 0;
      }
      else
        color_table_index = CQUAD * BYTES_PER_COLOR * this->buf[stream_ptr++];

      while (n_blocks--) {
        color_flags = BE_32(&this->buf[stream_ptr]);
        stream_ptr += 4;
        /* flag mask actually acts as a bit shift count here */
        flag_mask = 30;
        block_ptr = row_ptr + pixel_ptr;
        for (pixel_y = 0; pixel_y < 4; pixel_y++) {
          for (pixel_x = 0; pixel_x < 4; pixel_x++) {
            color_index = color_table_index + (BYTES_PER_COLOR * 
              ((color_flags >> flag_mask) & 0x03));
            flag_mask -= 2;

            yuv->y[block_ptr] = this->color_quads[color_index + 0];
            yuv->u[block_ptr] = this->color_quads[color_index + 1];
            yuv->v[block_ptr] = this->color_quads[color_index + 2];
            block_ptr++;
          }
          block_ptr += row_inc;
        }
        ADVANCE_BLOCK();
      }
      break;

    /* 8-color block encoding */
    case 0xC0:
    case 0xD0:
      n_blocks = (opcode & 0x0F) + 1;

      /* figure out which color octet to use to paint the 8-color block */
      if ((opcode & 0xF0) == 0xC0) {
        /* fetch the next 8 colors from bytestream and store in next
         * available entry in the color octet table */
        for (i = 0; i < COCTET; i++) {
          color_index = this->buf[stream_ptr++] * BYTES_PER_COLOR;
          color_table_index = COCTET * BYTES_PER_COLOR * color_octet_index + 
            (i * BYTES_PER_COLOR);
          this->color_octets[color_table_index + 0] = 
            this->yuv_palette[color_index + 0];
          this->color_octets[color_table_index + 1] = 
            this->yuv_palette[color_index + 1];
          this->color_octets[color_table_index + 2] = 
            this->yuv_palette[color_index + 2];
        }
        /* this is the base index to use for this block */
        color_table_index = COCTET * BYTES_PER_COLOR * color_octet_index;
        color_octet_index++;
        /* wraparound */
        if (color_octet_index == COLORS_PER_TABLE)
          color_octet_index = 0;
      }
      else
        color_table_index = COCTET * BYTES_PER_COLOR * this->buf[stream_ptr++];

      while (n_blocks--) {
        /*
          For this input of 6 hex bytes:
            01 23 45 67 89 AB
          Mangle it to this output:
            flags_a = xx012456, flags_b = xx89A37B
        */
        /* build the color flags */
        color_flags_a = color_flags_b = 0;
        color_flags_a =
          (this->buf[stream_ptr + 0] << 16) |
          ((this->buf[stream_ptr + 1] & 0xF0) << 8) |
          ((this->buf[stream_ptr + 2] & 0xF0) << 4) |
          ((this->buf[stream_ptr + 2] & 0x0F) << 4) |
          ((this->buf[stream_ptr + 3] & 0xF0) >> 4);
        color_flags_b =
          (this->buf[stream_ptr + 4] << 16) |
          ((this->buf[stream_ptr + 5] & 0xF0) << 8) |
          ((this->buf[stream_ptr + 1] & 0x0F) << 8) |
          ((this->buf[stream_ptr + 3] & 0x0F) << 4) |
          (this->buf[stream_ptr + 5] & 0x0F);
        stream_ptr += 6;

        color_flags = color_flags_a;
        /* flag mask actually acts as a bit shift count here */
        flag_mask = 21;
        block_ptr = row_ptr + pixel_ptr;
        for (pixel_y = 0; pixel_y < 4; pixel_y++) {
          /* reload flags at third row (iteration pixel_y == 2) */
          if (pixel_y == 2) {
            color_flags = color_flags_b;
            flag_mask = 21;
          }
          for (pixel_x = 0; pixel_x < 4; pixel_x++) {
            color_index = color_table_index + (BYTES_PER_COLOR * 
              ((color_flags >> flag_mask) & 0x07));
            flag_mask -= 3;

            yuv->y[block_ptr] = this->color_octets[color_index + 0];
            yuv->u[block_ptr] = this->color_octets[color_index + 1];
            yuv->v[block_ptr] = this->color_octets[color_index + 2];
            block_ptr++;
          }
          block_ptr += row_inc;
        }
        ADVANCE_BLOCK();
      }
      break;

    /* 16-color block encoding (every pixel is a different color) */
    case 0xE0:
      n_blocks = (opcode & 0x0F) + 1;

      while (n_blocks--) {
        block_ptr = row_ptr + pixel_ptr;
        for (pixel_y = 0; pixel_y < 4; pixel_y++) {
          for (pixel_x = 0; pixel_x < 4; pixel_x++) {
            color_index = this->buf[stream_ptr++] * BYTES_PER_COLOR;
            yuv->y[block_ptr] = this->yuv_palette[color_index + 0];
            yuv->u[block_ptr] = this->yuv_palette[color_index + 1];
            yuv->v[block_ptr] = this->yuv_palette[color_index + 2];
            block_ptr++;
          }
          block_ptr += row_inc;
        }
        ADVANCE_BLOCK();
      }
      break;

    case 0xF0:
      printf(_("0xF0 opcode seen in SMC chunk (xine developers would like to know)\n"));
      break;
    }
  }
}


/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void qtsmc_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  qtsmc_decoder_t *this = (qtsmc_decoder_t *) this_gen;
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
    this->stream->video_out->open (this->stream->video_out);

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

    this->stream->video_out->open (this->stream->video_out);
    this->decoder_ok = 1;

    /* load the stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = strdup("Quicktime Graphics (SMC)");
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
                                        42, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      decode_qtsmc(this);
      yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

      if (img->copy) {
        int height = img->height;
        uint8_t *src[3];

        src[0] = img->base[0];

        while ((height -= 16) >= 0) {
          img->copy(img, src);
          src[0] += 16 * img->pitches[0];
        }
      }

      img->draw(img);
      img->free(img);

      this->size = 0;
    }
  }
}

/*
 * This function is called when xine needs to flush the system. Not
 * sure when or if this is used or even if it needs to do anything.
 */
static void qtsmc_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void qtsmc_reset (video_decoder_t *this_gen) {
  qtsmc_decoder_t *this = (qtsmc_decoder_t *) this_gen;

  this->size = 0;
}

static void qtsmc_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void qtsmc_dispose (video_decoder_t *this_gen) {

  qtsmc_decoder_t *this = (qtsmc_decoder_t *) this_gen;

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  qtsmc_decoder_t  *this ;

  this = (qtsmc_decoder_t *) xine_xmalloc (sizeof (qtsmc_decoder_t));

  this->video_decoder.decode_data         = qtsmc_decode_data;
  this->video_decoder.flush               = qtsmc_flush;
  this->video_decoder.reset               = qtsmc_reset;
  this->video_decoder.discontinuity       = qtsmc_discontinuity;
  this->video_decoder.dispose             = qtsmc_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (qtsmc_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "QT SMC";
}

static char *get_description (video_decoder_class_t *this) {
  return "Quicktime Graphics (SMC) video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  qtsmc_class_t *this;

  this = (qtsmc_class_t *) xine_xmalloc (sizeof (qtsmc_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/* plugin catalog information */
static uint32_t supported_types[] = { BUF_VIDEO_SMC, 0 };

static decoder_info_t video_decoder_info = {
  supported_types,     /* supported types */
  9                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 12, "smc", XINE_VERSION_CODE, &video_decoder_info, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

