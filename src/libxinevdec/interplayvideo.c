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
 * Interplay MVE File Video Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Interplay MVE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: interplayvideo.c,v 1.1 2002/12/28 18:27:14 tmmm Exp $
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

/* debugging support */
#define DEBUG_INTERPLAY 0
#if DEBUG_INTERPLAY
#define debug_interplay printf
#else
static inline void debug_interplay(const char *format, ...) { }
#endif

#define VIDEOBUFSIZE 128*1024

typedef struct {
  video_decoder_class_t   decoder_class;
} interplay_class_t;

typedef struct interplay_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  interplay_class_t *class;
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

  unsigned char     yuv_palette[256 * 4];

  yuv_planes_t      yuv_planes1;
  yuv_planes_t      yuv_planes2;

  yuv_planes_t     *current_frame;
  yuv_planes_t     *previous_frame;

  /* this is either 1 or 2 indicating the current_frame points to yuv_planes
   * structure 1 or 2 */
  int               current_planes;

  unsigned char    *decode_map;
  int               decode_map_index;

} interplay_decoder_t;

/**************************************************************************
 * Interplay Video specific decode functions
 *************************************************************************/

#define CHECK_STREAM_PTR(n) \
  if ((stream_ptr + n) > this->size) { \
    printf ("Interplay video warning: stream_ptr out of bounds (%d >= %d)\n", \
      stream_ptr + n, this->size); \
    return; \
  }

#define COPY_BLOCK_FROM_CURRENT() \
  if (motion_pixel_ptr < 0) \
    printf ("Interplay video warning: motion pixel ptr < 0 (%d), motion byte = %d (0x%X)\n", \
    motion_pixel_ptr, motion_byte, motion_byte); \
  else if (motion_pixel_ptr >= motion_limit) \
    printf ("Interplay video warning: motion pixel ptr out of motion range (%d >= %d), motion byte = %d (0x%X)\n", \
    motion_pixel_ptr, motion_limit, motion_byte, motion_byte); \
  else for (i = 0; i < 8; i++) { \
    xine_fast_memcpy(&this->current_frame->y[pixel_ptr], \
      &this->current_frame->y[motion_pixel_ptr], 8); \
    xine_fast_memcpy(&this->current_frame->u[pixel_ptr], \
      &this->current_frame->u[motion_pixel_ptr], 8); \
    xine_fast_memcpy(&this->current_frame->v[pixel_ptr], \
      &this->current_frame->v[motion_pixel_ptr], 8); \
    pixel_ptr += this->width; \
  }

#define COPY_BLOCK_FROM_PREVIOUS() \
  if (motion_pixel_ptr < 0) \
    printf ("Interplay video warning: motion pixel ptr < 0 (%d), motion byte = %d (0x%X)\n", \
    motion_pixel_ptr, motion_byte, motion_byte); \
  else if (motion_pixel_ptr >= motion_limit) \
    printf ("Interplay video warning: motion pixel ptr out of motion range (%d >= %d), motion byte = %d (0x%X)\n", \
    motion_pixel_ptr, motion_limit, motion_byte, motion_byte); \
  else for (i = 0; i < 8; i++) { \
    xine_fast_memcpy(&this->current_frame->y[pixel_ptr], \
      &this->previous_frame->y[motion_pixel_ptr], 8); \
    xine_fast_memcpy(&this->current_frame->u[pixel_ptr], \
      &this->previous_frame->u[motion_pixel_ptr], 8); \
    xine_fast_memcpy(&this->current_frame->v[pixel_ptr], \
      &this->previous_frame->v[motion_pixel_ptr], 8); \
    pixel_ptr += this->width; \
  }

void interplay_decode_frame(interplay_decoder_t *this) {

  int pixel_ptr, motion_pixel_ptr;
  int x, y;
  int xp, yp;
  int i, j;
  unsigned char code;
  int index = 0;
  int stream_ptr = 14;
  unsigned char color0, color1, color2, color3;
  unsigned char y0, u0, v0;
  unsigned char y1, u1, v1;
  unsigned char y2, u2, v2;
  unsigned char y3, u3, v3;
  unsigned char y4 = 0, u4 = 0, v4 = 0;
  int row_inc = this->width - 8;
  unsigned char motion_byte;
  int motion_limit = this->width * (this->height - 8);
  unsigned int flags;
  unsigned int flag_mask;
  int code_counts[16];

  for (i = 0; i < 6; i++) {
    for (j = 0; j < 16; j++)
      debug_interplay (" %02X", this->buf[i * 16 + j]);
    debug_interplay ("\n");
  }

  for (i = 0; i < 16; i++)
    code_counts[i] = 0;

  /* interate through the 8x8 blocks, left -> right, top -> bottom */
  for (y = 0; y < (this->width * this->height); y += (this->width * 8)) {
    for (x = y; x < (y + this->width); x += 8) {
      if (index & 1)
        code = this->decode_map[index >> 1] >> 4;
      else
        code = this->decode_map[index >> 1] & 0xF;
      index++;

      debug_interplay ("  block %3d, %3d: encoding 0x%X\n", 
        x - y, y / this->width, code);
      code_counts[code]++;
      switch (code) {

        case 0x0:
        case 0x1:
          /* skip block (actually, copy from previous frame) */
          motion_pixel_ptr = pixel_ptr = x;
          motion_byte = 0;
          COPY_BLOCK_FROM_PREVIOUS();
          break;

        case 0x2:
          /* copy block from current frame, right-bottom */
          CHECK_STREAM_PTR(1);

          motion_byte = this->buf[stream_ptr++];
          motion_pixel_ptr = pixel_ptr = x;

          if (motion_byte < 56) {
            /* horizontal component */
            motion_pixel_ptr += 8 + (motion_byte % 7);
            /* vertical component */
            motion_pixel_ptr += ((motion_byte / 7) * this->width);
          } else {
            /* horizontal component */
            motion_pixel_ptr += (-14 + ((motion_byte - 56) % 29));
            /* vertical component */
            motion_pixel_ptr += ((8 + ((motion_byte - 56) / 29)) * this->width);
          }
          COPY_BLOCK_FROM_CURRENT();
          break;

        case 0x3:
          /* copy block from current frame, left-top */
          CHECK_STREAM_PTR(1);

          motion_byte = this->buf[stream_ptr++];
          motion_pixel_ptr = pixel_ptr = x;

          if (motion_byte < 56) {
            /* horizontal component */
            motion_pixel_ptr -= 8 + (motion_byte % 7);
            /* vertical component */
            motion_pixel_ptr -= ((motion_byte / 7) * this->width);
          } else {
            /* horizontal component */
            motion_pixel_ptr -= (-14 + ((motion_byte - 56) % 29));
            /* vertical component */
            motion_pixel_ptr -= ((8 + ((motion_byte - 56) / 29)) * this->width);
          }
          COPY_BLOCK_FROM_CURRENT();
          break;

        case 0x4:
          /* copy block from previous frame, right-bottom */
          CHECK_STREAM_PTR(1);

          motion_byte = this->buf[stream_ptr++];
          motion_pixel_ptr = pixel_ptr = x;

          /* horizontal component */
          motion_pixel_ptr += (-8 + (motion_byte & 0xF));
          /* vertical component */
          motion_pixel_ptr += ((-8 + ((motion_byte >> 4) & 0xF)) * this->width);
          COPY_BLOCK_FROM_PREVIOUS();
          break;

        case 0x5:
          /* copy block from previous frame, left-top */
          CHECK_STREAM_PTR(2);

          motion_pixel_ptr = pixel_ptr = x;
          /* horizontal component */
          motion_byte = this->buf[stream_ptr++];
          motion_pixel_ptr += (signed char)motion_byte;
          /* vertical component */
          motion_byte = this->buf[stream_ptr++];
          motion_pixel_ptr += ((signed char)motion_byte * this->width);
          COPY_BLOCK_FROM_PREVIOUS();
          break;

        case 0x6:
          /* mystery opcode? skip multiple blocks? */
          break;

        case 0x7:
          /* 2-color encoding */
          color0 = this->buf[stream_ptr++];
          y0 = this->yuv_palette[color0 * 4 + 0];
          u0 = this->yuv_palette[color0 * 4 + 1];
          v0 = this->yuv_palette[color0 * 4 + 2];
          color1 = this->buf[stream_ptr++];
          y1 = this->yuv_palette[color1 * 4 + 0];
          u1 = this->yuv_palette[color1 * 4 + 1];
          v1 = this->yuv_palette[color1 * 4 + 2];

          if (color0 <= color1) {
            /* get 8 bytes, or 64 flags from the bytestream; if a flag is 0
             * use color 0; 1, use color 1 */
            CHECK_STREAM_PTR(8);
            pixel_ptr = x;
            for (i = 0; i < 8; i++) {
              flags = this->buf[stream_ptr++];
              for (flag_mask = 0x80; flag_mask != 0; flag_mask >>= 1) {
                if (flags & flag_mask) {
                  this->current_frame->y[pixel_ptr] = y1;
                  this->current_frame->u[pixel_ptr] = u1;
                  this->current_frame->v[pixel_ptr] = v1;
                } else {
                  this->current_frame->y[pixel_ptr] = y0;
                  this->current_frame->u[pixel_ptr] = u0;
                  this->current_frame->v[pixel_ptr] = v0;
                }
                pixel_ptr++;
              }
              pixel_ptr += row_inc;
            }
          } else {
            /* get 2 bytes, or 16 flags from the bytestream; use them to
             * paint 2x2 blocks */
            CHECK_STREAM_PTR(2);
            flags = BE_16(&this->buf[stream_ptr]);
            stream_ptr += 2;
            flag_mask = 0x8000;
            
            for (yp = x; yp < (x + this->width * 8); yp += (this->width * 2)) {
              for (xp = yp; xp < (yp + 8); xp += 2) {

                if (flags & flag_mask) {
                  y2 = y1;
                  u2 = u1;
                  v2 = v1;
                } else {
                  y2 = y0;
                  u2 = u0;
                  v2 = v0;
                }
                flag_mask >>= 1;

                pixel_ptr = xp;
                for (i = 0; i < 2; i++) {
                  for (j = 0; j < 2; j++) {
                    this->current_frame->y[pixel_ptr] = y2;
                    this->current_frame->u[pixel_ptr] = u2;
                    this->current_frame->v[pixel_ptr] = v2;
                    pixel_ptr++;
                  }
                  pixel_ptr += this->width - 2;
                }
              }
            }
          }
          break;

        case 0x8:
          /* 2-color encoding for each 4x4 quadrant, or 2-color encoding on
           * either top and bottom or left and right halves */
          CHECK_STREAM_PTR(2);
          color0 = this->buf[stream_ptr++];
          color1 = this->buf[stream_ptr++];
          if (color0 <= color1) {
            /* there are 14 more byte in the stream, for a total of 16;
             * each set of 4 bytes contains 2 colors and 16 bit flags to
             * specify how one 4x4 quadrant should me painted */
            CHECK_STREAM_PTR(14);

            /* iterate through 4 quadrants */
            for (xp = 0; xp < 4; xp++) {
              if (xp == 0)
                pixel_ptr = x;
              else if (xp == 1)
                pixel_ptr = x + (this->width * 4);
              else if (xp == 2)
                pixel_ptr = x + 4;
              else
                pixel_ptr = x + (this->width * 4) + 4;

              /* get this quadrant's color pair if this is not the first
               * quadrant (those colors have already been obtained) */
              if (xp > 0) {
                color0 = this->buf[stream_ptr++];
                color1 = this->buf[stream_ptr++];
              }
              y0 = this->yuv_palette[color0 * 4 + 0];
              u0 = this->yuv_palette[color0 * 4 + 1];
              v0 = this->yuv_palette[color0 * 4 + 2];
              y1 = this->yuv_palette[color1 * 4 + 0];
              u1 = this->yuv_palette[color1 * 4 + 1];
              v1 = this->yuv_palette[color1 * 4 + 2];

              /* get the flags for this quadrant */
              flags = BE_16(&this->buf[stream_ptr]);
              stream_ptr += 2;
              flag_mask = 0x8000;

              /* paint the 4x4 block */
              for (i = 0; i < 4; i++) {
                for (j = 0; j < 4; j++) {
                  if (flags & flag_mask) {
                    this->current_frame->y[pixel_ptr] = y1;
                    this->current_frame->u[pixel_ptr] = u1;
                    this->current_frame->v[pixel_ptr] = v1;
                  } else {
                    this->current_frame->y[pixel_ptr] = y0;
                    this->current_frame->u[pixel_ptr] = u0;
                    this->current_frame->v[pixel_ptr] = v0;
                  }
                  pixel_ptr++;
                  flag_mask >>= 1;
                }
                pixel_ptr += this->width - 4;
              }
            }
          } else {
            /* there are 10 more bytes in the stream for a total of 12;
             * each set of 6 bytes contains 2 colors and 32 bit flags to
             * specify how half of the 8x8 block (either 8x4 or 4x8) will
             * be painted */
            CHECK_STREAM_PTR(10);
            color2 = this->buf[stream_ptr + 4];
            color3 = this->buf[stream_ptr + 5];
            if (color2 <= color3) {
              /* block is split into left and right halves */
              for (xp = 0; xp < 2; xp++) {
                if (xp == 0) {
                  pixel_ptr = x;
                  y0 = this->yuv_palette[color0 * 4 + 0];
                  u0 = this->yuv_palette[color0 * 4 + 1];
                  v0 = this->yuv_palette[color0 * 4 + 2];
                  y1 = this->yuv_palette[color1 * 4 + 0];
                  u1 = this->yuv_palette[color1 * 4 + 1];
                  v1 = this->yuv_palette[color1 * 4 + 2];
                  flags = BE_32(&this->buf[stream_ptr]);
                } else {
                  pixel_ptr = x + 4;
                  y0 = this->yuv_palette[color2 * 4 + 0];
                  u0 = this->yuv_palette[color2 * 4 + 1];
                  v0 = this->yuv_palette[color2 * 4 + 2];
                  y1 = this->yuv_palette[color3 * 4 + 0];
                  u1 = this->yuv_palette[color3 * 4 + 1];
                  v1 = this->yuv_palette[color3 * 4 + 2];
                  flags = BE_32(&this->buf[stream_ptr + 6]);
                }
                flag_mask = 0x80000000;

                /* paint the 4x8 block */
                for (i = 0; i < 8; i++) {
                  for (j = 0; j < 4; j++) {
                    if (flags & flag_mask) {
                      this->current_frame->y[pixel_ptr] = y1;
                      this->current_frame->u[pixel_ptr] = u1;
                      this->current_frame->v[pixel_ptr] = v1;
                    } else {
                      this->current_frame->y[pixel_ptr] = y0;
                      this->current_frame->u[pixel_ptr] = u0;
                      this->current_frame->v[pixel_ptr] = v0;
                    }
                    pixel_ptr++;
                    flag_mask >>= 1;
                  }
                  pixel_ptr += this->width - 4;
                }
              }
            } else {
              /* block is split into top and bottom halves */
              for (xp = 0; xp < 2; xp++) {
                if (xp == 0) {
                  pixel_ptr = x;
                  y0 = this->yuv_palette[color0 * 4 + 0];
                  u0 = this->yuv_palette[color0 * 4 + 1];
                  v0 = this->yuv_palette[color0 * 4 + 2];
                  y1 = this->yuv_palette[color1 * 4 + 0];
                  u1 = this->yuv_palette[color1 * 4 + 1];
                  v1 = this->yuv_palette[color1 * 4 + 2];
                  flags = BE_32(&this->buf[stream_ptr]);
                } else {
                  pixel_ptr = x + (this->width * 4);
                  y0 = this->yuv_palette[color2 * 4 + 0];
                  u0 = this->yuv_palette[color2 * 4 + 1];
                  v0 = this->yuv_palette[color2 * 4 + 2];
                  y1 = this->yuv_palette[color3 * 4 + 0];
                  u1 = this->yuv_palette[color3 * 4 + 1];
                  v1 = this->yuv_palette[color3 * 4 + 2];
                  flags = BE_32(&this->buf[stream_ptr + 6]);
                }
                flag_mask = 0x80000000;

                /* paint the 8x4 block */
                for (i = 0; i < 4; i++) {
                  for (j = 0; j < 8; j++) {
                    if (flags & flag_mask) {
                      this->current_frame->y[pixel_ptr] = y1;
                      this->current_frame->u[pixel_ptr] = u1;
                      this->current_frame->v[pixel_ptr] = v1;
                    } else {
                      this->current_frame->y[pixel_ptr] = y0;
                      this->current_frame->u[pixel_ptr] = u0;
                      this->current_frame->v[pixel_ptr] = v0;
                    }
                    pixel_ptr++;
                    flag_mask >>= 1;
                  }
                  pixel_ptr += row_inc;
                }
              }
            }

            stream_ptr += 10;
          }
          break;

        case 0x9:
          /* 4-color encoding */
          CHECK_STREAM_PTR(4);
          color0 = this->buf[stream_ptr++];
          y0 = this->yuv_palette[color0 * 4 + 0];
          u0 = this->yuv_palette[color0 * 4 + 1];
          v0 = this->yuv_palette[color0 * 4 + 2];
          color1 = this->buf[stream_ptr++];
          y1 = this->yuv_palette[color1 * 4 + 0];
          u1 = this->yuv_palette[color1 * 4 + 1];
          v1 = this->yuv_palette[color1 * 4 + 2];
          color2 = this->buf[stream_ptr++];
          y2 = this->yuv_palette[color2 * 4 + 0];
          u2 = this->yuv_palette[color2 * 4 + 1];
          v2 = this->yuv_palette[color2 * 4 + 2];
          color3 = this->buf[stream_ptr++];
          y3 = this->yuv_palette[color3 * 4 + 0];
          u3 = this->yuv_palette[color3 * 4 + 1];
          v3 = this->yuv_palette[color3 * 4 + 2];

          if ((color0 <= color1) && (color2 <= color3)) {

            /* there are 16 bytes in the stream which form 64 2-bit
             * flags to select the color of each of the 64 pixels in
             * the 8x8 block */
            CHECK_STREAM_PTR(16);
            pixel_ptr = x;
            for (i = 0; i < 8; i++) {
              /* reload the flags at every row */
              flags = BE_16(&this->buf[stream_ptr]);
              stream_ptr += 2;
              flag_mask = 14;
              for (j = 0; j < 8; j++) {
                switch ((flags >> flag_mask) & 0x03) {

                  case 0:
                    this->current_frame->y[pixel_ptr] = y0;
                    this->current_frame->u[pixel_ptr] = u0;
                    this->current_frame->v[pixel_ptr] = v0;
                    break;

                  case 1:
                    this->current_frame->y[pixel_ptr] = y1;
                    this->current_frame->u[pixel_ptr] = u1;
                    this->current_frame->v[pixel_ptr] = v1;
                    break;

                  case 2:
                    this->current_frame->y[pixel_ptr] = y2;
                    this->current_frame->u[pixel_ptr] = u2;
                    this->current_frame->v[pixel_ptr] = v2;
                    break;

                  case 3:
                    this->current_frame->y[pixel_ptr] = y3;
                    this->current_frame->u[pixel_ptr] = u3;
                    this->current_frame->v[pixel_ptr] = v3;
                    break;

                }
                pixel_ptr++;
                flag_mask -= 2;
              }
              pixel_ptr += row_inc;
            }

          } else if ((color0 <= color1) && (color2 > color3)) {

            /* there are 4 bytes in the stream comprising 16 2-bit
             * flags which specify 1 of 4 colors for each of 16 2x2
             * blocks */
            CHECK_STREAM_PTR(4);
            flags = BE_32(&this->buf[stream_ptr]);
            stream_ptr += 4;
            flag_mask = 30;

            for (yp = x; yp < (x + this->width * 8); yp += (this->width * 2)) {
              for (xp = yp; xp < (yp + 8); xp += 2) {

                switch ((flags >> flag_mask) & 0x03) {

                  case 0:
                    y4 = y0;
                    u4 = u0;
                    v4 = v0;
                    break;

                  case 1:
                    y4 = y1;
                    u4 = u1;
                    v4 = v1;
                    break;

                  case 2:
                    y4 = y2;
                    u4 = u2;
                    v4 = v2;
                    break;

                  case 3:
                    y4 = y3;
                    u4 = u3;
                    v4 = v3;
                    break;

                }
                flag_mask -= 2;

                pixel_ptr = xp;
                for (i = 0; i < 2; i++) {
                  for (j = 0; j < 2; j++) {
                    this->current_frame->y[pixel_ptr] = y4;
                    this->current_frame->u[pixel_ptr] = u4;
                    this->current_frame->v[pixel_ptr] = v4;
                    pixel_ptr++;
                  }
                  pixel_ptr += this->width - 2;
                }
              }
            }

          } else if ((color0 > color1) && (color2 <= color3)) {

            /* there are 8 bytes in the stream comprising 32 2-bit
             * flags which specify 1 of 4 colors for each of 32 2x1
             * blocks */
            CHECK_STREAM_PTR(8);
            flags = BE_32(&this->buf[stream_ptr]);
            stream_ptr += 4;
            flag_mask = 30;

            for (yp = x; yp < (x + this->width * 8); yp += this->width) {

              /* time to reload flags? */
              if (yp == (x + this->width * 4)) {
                flags = BE_32(&this->buf[stream_ptr]);
                stream_ptr += 4;
                flag_mask = 30;
              }

              for (xp = yp; xp < (yp + 8); xp += 2) {

                switch ((flags >> flag_mask) & 0x03) {

                  case 0:
                    y4 = y0;
                    u4 = u0;
                    v4 = v0;
                    break;

                  case 1:
                    y4 = y1;
                    u4 = u1;
                    v4 = v1;
                    break;

                  case 2:
                    y4 = y2;
                    u4 = u2;
                    v4 = v2;
                    break;

                  case 3:
                    y4 = y3;
                    u4 = u3;
                    v4 = v3;
                    break;

                }
                flag_mask -= 2;

                pixel_ptr = xp;
                for (i = 0; i < 2; i++) {
                  this->current_frame->y[pixel_ptr] = y4;
                  this->current_frame->u[pixel_ptr] = u4;
                  this->current_frame->v[pixel_ptr] = v4;
                  pixel_ptr++;
                }
              }
            }

          } else {

            /* there are 8 bytes in the stream comprising 32 2-bit
             * flags which specify 1 of 4 colors for each of 32 1x2
             * blocks */
            CHECK_STREAM_PTR(8);
            flags = BE_32(&this->buf[stream_ptr]);
            stream_ptr += 4;
            flag_mask = 30;

            for (yp = x; yp < (x + this->width * 8); yp += (this->width * 2)) {

              /* time to reload flags? */
              if (yp == (x + this->width * 4)) {
                flags = BE_32(&this->buf[stream_ptr]);
                stream_ptr += 4;
                flag_mask = 30;
              }

              for (xp = yp; xp < (yp + 8); xp++) {

                switch ((flags >> flag_mask) & 0x03) {

                  case 0:
                    y4 = y0;
                    u4 = u0;
                    v4 = v0;
                    break;

                  case 1:
                    y4 = y1;
                    u4 = u1;
                    v4 = v1;
                    break;

                  case 2:
                    y4 = y2;
                    u4 = u2;
                    v4 = v2;
                    break;

                  case 3:
                    y4 = y3;
                    u4 = u3;
                    v4 = v3;
                    break;

                }
                flag_mask -= 2;

                pixel_ptr = xp;
                for (i = 0; i < 2; i++) {
                  this->current_frame->y[pixel_ptr] = y4;
                  this->current_frame->u[pixel_ptr] = u4;
                  this->current_frame->v[pixel_ptr] = v4;
                  pixel_ptr += row_inc;
                }
              }
            }
          }
          break;

        case 0xA:
          /* 4-color encoding for each 4x4 quadrant, or 4-color encoding on
           * either top and bottom or left and right halves */
          CHECK_STREAM_PTR(4);
          color0 = this->buf[stream_ptr++];
          color1 = this->buf[stream_ptr++];
          color2 = this->buf[stream_ptr++];
          color3 = this->buf[stream_ptr++];
          if (color0 <= color1) {
            /* there are 28 more byte in the stream, for a total of 32;
             * each set of 8 bytes contains 4 colors and 16 2-bit flags to
             * specify how one 4x4 quadrant should me painted */
            CHECK_STREAM_PTR(28);

            /* iterate through 4 quadrants */
            for (xp = 0; xp < 4; xp++) {
              if (xp == 0)
                pixel_ptr = x;
              else if (xp == 1)
                pixel_ptr = x + (this->width * 4);
              else if (xp == 2)
                pixel_ptr = x + 4;
              else
                pixel_ptr = x + (this->width * 4) + 4;

              /* get this quadrant's color pair if this is not the first
               * quadrant (those colors have already been obtained) */
              if (xp > 0) {
                color0 = this->buf[stream_ptr++];
                color1 = this->buf[stream_ptr++];
                color2 = this->buf[stream_ptr++];
                color3 = this->buf[stream_ptr++];
              }
              y0 = this->yuv_palette[color0 * 4 + 0];
              u0 = this->yuv_palette[color0 * 4 + 1];
              v0 = this->yuv_palette[color0 * 4 + 2];
              y1 = this->yuv_palette[color1 * 4 + 0];
              u1 = this->yuv_palette[color1 * 4 + 1];
              v1 = this->yuv_palette[color1 * 4 + 2];
              y2 = this->yuv_palette[color2 * 4 + 0];
              u2 = this->yuv_palette[color2 * 4 + 1];
              v2 = this->yuv_palette[color2 * 4 + 2];
              y3 = this->yuv_palette[color3 * 4 + 0];
              u3 = this->yuv_palette[color3 * 4 + 1];
              v3 = this->yuv_palette[color3 * 4 + 2];

              /* get the flags for this quadrant */
              flags = BE_32(&this->buf[stream_ptr]);
              stream_ptr += 4;
              flag_mask = 30;

              /* paint the 4x4 block */
              for (i = 0; i < 4; i++) {
                for (j = 0; j < 4; j++) {

                  switch ((flags >> flag_mask) & 0x03) {

                    case 0:
                      this->current_frame->y[pixel_ptr] = y0;
                      this->current_frame->u[pixel_ptr] = u0;
                      this->current_frame->v[pixel_ptr] = v0;
                      break;

                    case 1:
                      this->current_frame->y[pixel_ptr] = y1;
                      this->current_frame->u[pixel_ptr] = u1;
                      this->current_frame->v[pixel_ptr] = v1;
                      break;

                    case 2:
                      this->current_frame->y[pixel_ptr] = y2;
                      this->current_frame->u[pixel_ptr] = u2;
                      this->current_frame->v[pixel_ptr] = v2;
                      break;

                    case 3:
                      this->current_frame->y[pixel_ptr] = y3;
                      this->current_frame->u[pixel_ptr] = u3;
                      this->current_frame->v[pixel_ptr] = v3;
                      break;

                  }
                  flag_mask -= 2;
                  pixel_ptr++;
                }
                pixel_ptr += this->width - 4;
              }
            }
          } else {
            /* there are 20 more bytes in the stream for a total of 24;
             * each set of 12 bytes contains 4 colors and 64 2-bit flags to
             * specify how half of the 8x8 block (either 8x4 or 4x8) will
             * be painted */
            CHECK_STREAM_PTR(20);

            if (color2 <= color3) {
              /* block is split into left and right halves */
              for (xp = 0; xp < 2; xp++) {
                if (xp == 0)
                  pixel_ptr = x;
                else {
                  color0 = this->buf[stream_ptr++];
                  color1 = this->buf[stream_ptr++];
                  color2 = this->buf[stream_ptr++];
                  color3 = this->buf[stream_ptr++];
                  pixel_ptr = x + 4;
                }
                y0 = this->yuv_palette[color0 * 4 + 0];
                u0 = this->yuv_palette[color0 * 4 + 1];
                v0 = this->yuv_palette[color0 * 4 + 2];
                y1 = this->yuv_palette[color1 * 4 + 0];
                u1 = this->yuv_palette[color1 * 4 + 1];
                v1 = this->yuv_palette[color1 * 4 + 2];
                y2 = this->yuv_palette[color2 * 4 + 0];
                u2 = this->yuv_palette[color2 * 4 + 1];
                v2 = this->yuv_palette[color2 * 4 + 2];
                y3 = this->yuv_palette[color3 * 4 + 0];
                u3 = this->yuv_palette[color3 * 4 + 1];
                v3 = this->yuv_palette[color3 * 4 + 2];
                flags = BE_32(&this->buf[stream_ptr]);
                stream_ptr += 4;
                flag_mask = 30;

                /* paint the 4x8 block */
                for (i = 0; i < 8; i++) {

                  /* time to reload flags? */
                  if (i == 4) {
                    flags = BE_32(&this->buf[stream_ptr]);
                    stream_ptr += 4;
                    flag_mask = 30;
                  }
                  for (j = 0; j < 4; j++) {

                    switch ((flags >> flag_mask) & 0x03) {

                      case 0:
                        this->current_frame->y[pixel_ptr] = y0;
                        this->current_frame->u[pixel_ptr] = u0;
                        this->current_frame->v[pixel_ptr] = v0;
                        break;

                      case 1:
                        this->current_frame->y[pixel_ptr] = y1;
                        this->current_frame->u[pixel_ptr] = u1;
                        this->current_frame->v[pixel_ptr] = v1;
                        break;

                      case 2:
                        this->current_frame->y[pixel_ptr] = y2;
                        this->current_frame->u[pixel_ptr] = u2;
                        this->current_frame->v[pixel_ptr] = v2;
                        break;

                      case 3:
                        this->current_frame->y[pixel_ptr] = y3;
                        this->current_frame->u[pixel_ptr] = u3;
                        this->current_frame->v[pixel_ptr] = v3;
                        break;

                    }

                    pixel_ptr++;
                    flag_mask -= 2;
                  }
                  pixel_ptr += this->width - 4;
                }
              }
            } else {
              /* block is split into top and bottom halves */
              for (xp = 0; xp < 2; xp++) {
                if (xp == 0)
                  pixel_ptr = x;
                else {
                  color0 = this->buf[stream_ptr++];
                  color1 = this->buf[stream_ptr++];
                  color2 = this->buf[stream_ptr++];
                  color3 = this->buf[stream_ptr++];
                  pixel_ptr = x + (this->width * 4);
                }
                y0 = this->yuv_palette[color0 * 4 + 0];
                u0 = this->yuv_palette[color0 * 4 + 1];
                v0 = this->yuv_palette[color0 * 4 + 2];
                y1 = this->yuv_palette[color1 * 4 + 0];
                u1 = this->yuv_palette[color1 * 4 + 1];
                v1 = this->yuv_palette[color1 * 4 + 2];
                y2 = this->yuv_palette[color2 * 4 + 0];
                u2 = this->yuv_palette[color2 * 4 + 1];
                v2 = this->yuv_palette[color2 * 4 + 2];
                y3 = this->yuv_palette[color3 * 4 + 0];
                u3 = this->yuv_palette[color3 * 4 + 1];
                v3 = this->yuv_palette[color3 * 4 + 2];
                flags = BE_32(&this->buf[stream_ptr]);
                stream_ptr += 4;
                flag_mask = 30;

                /* paint the 8x4 block */
                for (i = 0; i < 4; i++) {

                  /* time to reload flags? */
                  if (i == 2) {
                    flags = BE_32(&this->buf[stream_ptr]);
                    stream_ptr += 4;
                    flag_mask = 30;
                  }
                  for (j = 0; j < 8; j++) {

                    switch ((flags >> flag_mask) & 0x03) {

                      case 0:
                        this->current_frame->y[pixel_ptr] = y0;
                        this->current_frame->u[pixel_ptr] = u0;
                        this->current_frame->v[pixel_ptr] = v0;
                        break;

                      case 1:
                        this->current_frame->y[pixel_ptr] = y1;
                        this->current_frame->u[pixel_ptr] = u1;
                        this->current_frame->v[pixel_ptr] = v1;
                        break;

                      case 2:
                        this->current_frame->y[pixel_ptr] = y2;
                        this->current_frame->u[pixel_ptr] = u2;
                        this->current_frame->v[pixel_ptr] = v2;
                        break;

                      case 3:
                        this->current_frame->y[pixel_ptr] = y3;
                        this->current_frame->u[pixel_ptr] = u3;
                        this->current_frame->v[pixel_ptr] = v3;
                        break;

                    }

                    pixel_ptr++;
                    flag_mask -= 2;
                  }
                  pixel_ptr += row_inc;
                }
              }
            }

            stream_ptr += 10;
          }
          break;

        case 0xB:
          /* 64-color encoding (each pixel is a different color) */
          CHECK_STREAM_PTR(64);

          pixel_ptr = x;
          for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++) {
              color0 = this->buf[stream_ptr++];
              this->current_frame->y[pixel_ptr] =
                this->yuv_palette[color0 * 4 + 0];
              this->current_frame->u[pixel_ptr] =
                this->yuv_palette[color0 * 4 + 1];
              this->current_frame->v[pixel_ptr] =
                this->yuv_palette[color0 * 4 + 2];
              pixel_ptr++;
            }
            pixel_ptr += row_inc;
          }
          break;

        case 0xC:
          /* 16-color block encoding: each 2x2 block is a different color */
          CHECK_STREAM_PTR(16);
          for (yp = x; yp < (x + this->width * 8); yp += (this->width * 2)) {
            for (xp = yp; xp < (yp + 8); xp += 2) {
              color0 = this->buf[stream_ptr++];
              y0 = this->yuv_palette[color0 * 4 + 0];
              u0 = this->yuv_palette[color0 * 4 + 1];
              v0 = this->yuv_palette[color0 * 4 + 2];

              pixel_ptr = xp;
              for (i = 0; i < 2; i++) {
                for (j = 0; j < 2; j++) {
                  this->current_frame->y[pixel_ptr] = y0;
                  this->current_frame->u[pixel_ptr] = u0;
                  this->current_frame->v[pixel_ptr] = v0;
                  pixel_ptr++;
                }
                pixel_ptr += this->width - 2;
              }
            }
          }
          break;

        case 0xD:
          /* 4-color block encoding: each 4x4 block is a different color */
          CHECK_STREAM_PTR(4);
          for (yp = x; yp < (x + this->width * 8); yp += (this->width * 4)) {
            for (xp = yp; xp < (yp + 8); xp += 4) {
              color0 = this->buf[stream_ptr++];
              y0 = this->yuv_palette[color0 * 4 + 0];
              u0 = this->yuv_palette[color0 * 4 + 1];
              v0 = this->yuv_palette[color0 * 4 + 2];

              pixel_ptr = xp;
              for (i = 0; i < 4; i++) {
                for (j = 0; j < 4; j++) {
                  this->current_frame->y[pixel_ptr] = y0;
                  this->current_frame->u[pixel_ptr] = u0;
                  this->current_frame->v[pixel_ptr] = v0;
                  pixel_ptr++;
                }
                pixel_ptr += this->width - 4;
              }
            }
          }
          break;

        case 0xE:
          /* 1-color encoding: the whole block is 1 solid color */
          CHECK_STREAM_PTR(1);
          color0 = this->buf[stream_ptr++];
          y0 = this->yuv_palette[color0 * 4 + 0];
          u0 = this->yuv_palette[color0 * 4 + 1];
          v0 = this->yuv_palette[color0 * 4 + 2];

          pixel_ptr = x;
          for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++) {
              this->current_frame->y[pixel_ptr] = y0;
              this->current_frame->u[pixel_ptr] = u0;
              this->current_frame->v[pixel_ptr] = v0;
              pixel_ptr++;
            }
            pixel_ptr += row_inc;
          }
          break;

        case 0xF:
          /* dithered encoding */
          CHECK_STREAM_PTR(2);
          color0 = this->buf[stream_ptr++];
          y0 = this->yuv_palette[color0 * 4 + 0];
          u0 = this->yuv_palette[color0 * 4 + 1];
          v0 = this->yuv_palette[color0 * 4 + 2];
          color1 = this->buf[stream_ptr++];
          y1 = this->yuv_palette[color1 * 4 + 0];
          u1 = this->yuv_palette[color1 * 4 + 1];
          v1 = this->yuv_palette[color1 * 4 + 2];

          pixel_ptr = x;
          for (i = 0; i < 8; i++) {
            for (j = 0; j < 4; j++) {
              if (i & 1) {
                this->current_frame->y[pixel_ptr] = y1;
                this->current_frame->u[pixel_ptr] = u1;
                this->current_frame->v[pixel_ptr] = v1;
                pixel_ptr++;
                this->current_frame->y[pixel_ptr] = y0;
                this->current_frame->u[pixel_ptr] = u0;
                this->current_frame->v[pixel_ptr] = v0;
                pixel_ptr++;
              } else {
                this->current_frame->y[pixel_ptr] = y0;
                this->current_frame->u[pixel_ptr] = u0;
                this->current_frame->v[pixel_ptr] = v0;
                pixel_ptr++;
                this->current_frame->y[pixel_ptr] = y1;
                this->current_frame->u[pixel_ptr] = u1;
                this->current_frame->v[pixel_ptr] = v1;
                pixel_ptr++;
              }
            }
            pixel_ptr += row_inc;
          }
          break;

      }
    }
  }

  /* on the way out, make sure all the video data bytes were consumed */
  if (stream_ptr != this->size)
    printf ("Interplay video warning: Finished decode with bytes left over (%d < %d)\n",
      stream_ptr, this->size);

  debug_interplay ("code counts:\n");
  for (i = 0; i < 16; i++)
    debug_interplay ("  code %X: %d\n", i, code_counts[i]);
}

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void interplay_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  interplay_decoder_t *this = (interplay_decoder_t *) this_gen;
  palette_entry_t *palette;
  int i;

  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  /* load the palette */
  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette = (palette_entry_t *)buf->decoder_info_ptr[2];
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

    this->width = (buf->content[0] << 8) | buf->content[1];
    this->height = (buf->content[2] << 8) | buf->content[3];
    this->video_step = buf->decoder_info[1];

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    /* the decode map consists of 4 bits for every 8x8 video block, or
     * 1 byte for every 2 blocks (128 pixels) */
    this->decode_map = xine_xmalloc(this->width * this->height / 128);
    this->decode_map_index = 0;

    init_yuv_planes(&this->yuv_planes1, this->width, this->height);
    init_yuv_planes(&this->yuv_planes2, this->width, this->height);
    this->current_planes = 1;

    /* take this opportunity to load the stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = 
      strdup("Interplay MVE Video");
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;

    this->decoder_ok = 1;

    return;
  } else if (this->decoder_ok) {

    /* check if the decoder map is being sent (keyframe flag is cleared */
    if ((buf->decoder_flags & BUF_FLAG_KEYFRAME) == 0) {
      xine_fast_memcpy (&this->decode_map[this->decode_map_index],
        buf->content, buf->size);
      return;
    }

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

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      if (this->current_planes == 1) {
        this->current_planes = 2;
        this->current_frame = &this->yuv_planes1;
        this->previous_frame = &this->yuv_planes2;
      } else {
        this->current_planes = 1;
        this->current_frame = &this->yuv_planes2;
        this->previous_frame = &this->yuv_planes1;
      }

      interplay_decode_frame (this);
      yuv444_to_yuy2(this->current_frame, img->base[0], img->pitches[0]);

      img->draw(img, this->stream);
      img->free(img);

      this->size = this->decode_map_index = 0;
    }
  }
}

/*
 * This function is called when xine needs to flush the system.
 */
static void interplay_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void interplay_reset (video_decoder_t *this_gen) {
  interplay_decoder_t *this = (interplay_decoder_t *) this_gen;

  this->size = 0;
}

/*
 * The decoder should forget any stored pts values here.
 */
static void interplay_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void interplay_dispose (video_decoder_t *this_gen) {

  interplay_decoder_t *this = (interplay_decoder_t *) this_gen;

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->decode_map) {
    free (this->decode_map);
    this->decode_map = NULL;
  }

  free_yuv_planes(&this->yuv_planes1);
  free_yuv_planes(&this->yuv_planes2);

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  interplay_decoder_t  *this ;

  this = (interplay_decoder_t *) xine_xmalloc (sizeof (interplay_decoder_t));

  this->video_decoder.decode_data         = interplay_decode_data;
  this->video_decoder.flush               = interplay_flush;
  this->video_decoder.reset               = interplay_reset;
  this->video_decoder.discontinuity       = interplay_discontinuity;
  this->video_decoder.dispose             = interplay_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (interplay_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

/*
 * This function returns a brief string that describes (usually with the
 * decoder's most basic name) the video decoder plugin.
 */
static char *get_identifier (video_decoder_class_t *this) {
  return "Interplay MVE Video";
}

/*
 * This function returns a slightly longer string describing the video
 * decoder plugin.
 */
static char *get_description (video_decoder_class_t *this) {
  return "Interplay MVE File video decoder plugin";
}

/*
 * This function frees the video decoder class and any other memory that was
 * allocated.
 */
static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
static void *init_plugin (xine_t *xine, void *data) {

  interplay_class_t *this;

  this = (interplay_class_t *) xine_xmalloc (sizeof (interplay_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/*
 * This is a list of all of the internal xine video buffer types that
 * this decoder is able to handle. Check src/xine-engine/buffer.h for a
 * list of valid buffer types (and add a new one if the one you need does
 * not exist). Terminate the list with a 0.
 */
static uint32_t video_types[] = { 
  BUF_VIDEO_INTERPLAY,
  0
};

/*
 * This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1)
 * will be used instead of a plugin with priority (n).
 */
static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  5                    /* priority        */
};

/*
 * The plugin catalog entry. This is the only information that this plugin
 * will export to the public.
 */
plugin_info_t xine_plugin_info[] = {
  /* { type, API, "name", version, special_info, init_function } */
  { PLUGIN_VIDEO_DECODER, 14, "interplay", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
