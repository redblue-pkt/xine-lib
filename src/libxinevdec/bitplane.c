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
 *
 * Bitplane "Decoder" by Manfred Tremmel (Manfred.Tremmel@iiv.de)
 * Converts Amiga typical bitplane pictures to a YUV2 map
 * suitable for display under xine. It's based on the rgb-decoder
 * and the development documentation from the Amiga Developer CD
 *
 * $Id: bitplane.c,v 1.6 2004/02/25 18:57:36 manfredtremmel Exp $
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

#include "demuxers/iff.h"

#define IFF_REPLACE_BYTE(ptr, old_data, new_data, colorindex ) { \
  register uint8_t  *index_ptr = ptr; \
  *index_ptr    -= ((old_data & 0x80) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x80) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x40) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x40) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x20) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x20) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x10) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x10) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x08) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x08) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x04) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x04) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x02) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x02) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x01) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x01) ? colorindex : 0); \
  old_data       = new_data; \
}

#ifdef WORDS_BIGENDIAN
#define IFF_REPLACE_SHORT(ptr, old_data, new_data, colorindex ) { \
  register uint8_t  *index_ptr = ptr; \
  *index_ptr    -= ((old_data & 0x8000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x8000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x4000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x4000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x2000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x2000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x1000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x1000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0800) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0800) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0400) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0400) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0200) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0200) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0100) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0100) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0080) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0080) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0040) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0040) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0020) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0020) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0010) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0010) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0008) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0008) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0004) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0004) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0002) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0002) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0001) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x0001) ? colorindex : 0); \
  old_data       = new_data; \
}
#else
#define IFF_REPLACE_SHORT(ptr, old_data, new_data, colorindex ) { \
  register uint8_t  *index_ptr = ptr; \
  *index_ptr    -= ((old_data & 0x0080) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0080) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0040) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0040) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0020) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0020) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0010) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0010) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0008) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0008) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0004) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0004) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0002) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0002) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0001) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0001) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x8000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x8000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x4000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x4000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x2000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x2000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x1000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x1000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0800) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0800) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0400) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0400) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0200) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x0200) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x0100) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x0100) ? colorindex : 0); \
  old_data       = new_data; \
}
#endif

#ifdef WORDS_BIGENDIAN
#define IFF_REPLACE_LONG(ptr, old_data, new_data, colorindex ) { \
  register uint8_t  *index_ptr = ptr; \
  *index_ptr    -= ((old_data & 0x80000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x80000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x40000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x40000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x20000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x20000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x10000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x10000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x08000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x08000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x04000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x04000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x02000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x02000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x01000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x01000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00800000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00800000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00400000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00400000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00200000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00200000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00100000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00100000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00080000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00080000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00040000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00040000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00020000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00020000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00010000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00010000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00008000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00008000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00004000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00004000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00002000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00002000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00001000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00001000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000800) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000800) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000400) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000400) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000200) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000200) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000100) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000100) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000080) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000080) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000040) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000040) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000020) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000020) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000010) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000010) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000008) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000008) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000004) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000004) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000002) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000002) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000001) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x00000001) ? colorindex : 0); \
  old_data       = new_data; \
}
#else
#define IFF_REPLACE_LONG(ptr, old_data, new_data, colorindex ) { \
  register uint8_t  *index_ptr = ptr; \
  *index_ptr    -= ((old_data & 0x00000080) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000080) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000040) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000040) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000020) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000020) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000010) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000010) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000008) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000008) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000004) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000004) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000002) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000002) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000001) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000001) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00008000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00008000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00004000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00004000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00002000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00002000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00001000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00001000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000800) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000800) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000400) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000400) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000200) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000200) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00000100) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00000100) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00800000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00800000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00400000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00400000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00200000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00200000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00100000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00100000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00080000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00080000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00040000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00040000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00020000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00020000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x00010000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x00010000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x80000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x80000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x40000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x40000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x20000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x20000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x10000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x10000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x08000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x08000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x04000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x04000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x02000000) ? colorindex : 0); \
  *index_ptr++  += ((new_data & 0x02000000) ? colorindex : 0); \
  *index_ptr    -= ((old_data & 0x01000000) ? colorindex : 0); \
  *index_ptr    += ((new_data & 0x01000000) ? colorindex : 0); \
  old_data       = new_data; \
}
#endif

typedef struct {
  video_decoder_class_t   decoder_class;
} bitplane_class_t;

typedef struct bitplane_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  bitplane_class_t *class;
  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object */
  uint64_t          video_step;  /* frame duration in pts units */
  int               decoder_ok;  /* current decoder status */
  int               skipframes;

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */
  int               size_uk;     /* size of unkompressed bitplane */

  int               width_decode;/* the width of a video frame decoding*/
  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */
  double            ratio;       /* the width to height ratio */
  int               bytes_per_pixel;
  int               full_bytes_per_pixel;
  int               num_bitplanes;
  int               camg_mode;

  unsigned char     yuv_palette[256 * 4];
  unsigned char     rgb_palette[256 * 4];
  yuv_planes_t      yuv_planes;

  uint8_t          *buf_uk;      /* uncompressed buffer                */
  uint8_t          *buf_uk_hist; /* uncompressed buffer historic       */
  uint8_t          *index_buf;   /* index buffer (for indexed pics)    */
  uint8_t          *index_buf_hist;/* index buffer historic            */
  uint8_t          *rgb_buf;     /* rgb buffer (for HAM and TrueColor) */

} bitplane_decoder_t;

/* create a new buffer and decde a byterun1 decoded buffer into it */
static uint8_t *bitplane_decode_byterun1 (uint8_t *compressed,
  int size_compressed,
  int size_uncompressed) {
  
  /* BytRun1 decompression */
  int pixel_ptr                         = 0;
  int i                                 = 0;
  int j                                 = 0;
  
  uint8_t *uncompressed                 = xine_xmalloc( size_uncompressed );
  
  while ( i < size_compressed &&
          pixel_ptr < size_uncompressed ) {
    if( compressed[i] <= 127 ) {
      j = compressed[i++];
      if( (i+j) > size_compressed )
        return NULL;
      for( ; (j >= 0) && (pixel_ptr < size_uncompressed); j-- ) {
        uncompressed[pixel_ptr++] = compressed[i++];
      }
    } else if ( compressed[i] > 128 ) {
      j = 256 - compressed[i++];
      if( i >= size_compressed )
        return NULL;
      for( ; (j >= 0) && (pixel_ptr < size_uncompressed); j-- ) {
        uncompressed[pixel_ptr++] = compressed[i];
      }
      i++;
    }
  }
  return uncompressed;
}

/* create a new buffer with "normal" index or rgb numbers out of a bitplane */
static uint8_t *bitplane_decode_bitplane (uint8_t *bitplane_buffer,
  uint8_t *index_buf,
  int width,
  int height,
  int num_bitplanes,
  int bytes_per_pixel ) {
  
  int rowsize                           = width / 8;
  int pixel_ptr                         = 0;
  int row_ptr                           = 0;
  int palette_index                     = 0;
  int i                                 = 0;
  int j                                 = 0;
  uint8_t color                         = 0;
  uint8_t data                          = 0;
  
  for (i = 0; i < (width * height * bytes_per_pixel); index_buf[i++] = 0);

  /* decode Bitplanes to RGB/Index Numbers */
  for (row_ptr = 0; row_ptr < height; row_ptr++) {
    for (palette_index = 0; palette_index < num_bitplanes; palette_index++) {
      for (pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        i                               = (row_ptr * width * bytes_per_pixel) +
                                          (pixel_ptr * bytes_per_pixel * 8) +
                                          ((palette_index > 15) ? 2 : (palette_index > 7) ? 1 : 0);
        j                               = (row_ptr * rowsize * num_bitplanes) +
                                          (palette_index * rowsize) +
                                          pixel_ptr;
        color                           = bitplainoffeset[palette_index];
        data                            = bitplane_buffer[j];
        
        index_buf[i]                   += ((data & 0x80) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x40) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x20) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x10) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x08) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x04) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x02) ? color : 0);
        i                              += bytes_per_pixel;
        index_buf[i]                   += ((data & 0x01) ? color : 0);
      }
    }
  }
  return index_buf;
}

/* create Buffer decode HAM6 and HAM8 to 24 Bit RGB color */
static uint8_t *bitplane_decode_ham (uint8_t *ham_buffer,
  uint8_t *truecolor_buf,
  int width,
  int height,
  int num_bitplanes,
  int bytes_per_pixel,
  unsigned char *rgb_palette ) {
  
  int pixel_ptr                         = 0;
  int row_ptr                           = 0;
  int buf_ptr                           = 0;
  int i                                 = 0;
  int j                                 = 0;
  unsigned char r                       = 0;
  unsigned char g                       = 0;
  unsigned char b                       = 0;
  /* position of special HAM-Bits differs in HAM6 and HAM8, detect them */
  int hambits                           = num_bitplanes > 6 ? 6 : 4;
        /* the other bits contain the real data, dreate a mask out of it */
  int maskbits                          = 8 - hambits;
  int mask                              = ( 1 << hambits ) - 1;
  
  for (row_ptr = 0; row_ptr < height; row_ptr++) {
    for (pixel_ptr = 0; pixel_ptr < width; pixel_ptr++) {
      i                                 = (row_ptr * width) + pixel_ptr;
      buf_ptr                           = (row_ptr * width * bytes_per_pixel) +
                                          (pixel_ptr * bytes_per_pixel);
      j                                 = ham_buffer[i];
      switch ( j >> hambits ) {
        case HAMBITS_CMAP:
          /* Take colors from palette */
          r                             = rgb_palette[(j & mask) * 4 + 0];
          g                             = rgb_palette[(j & mask) * 4 + 1];
          b                             = rgb_palette[(j & mask) * 4 + 2];
          break;
        case HAMBITS_BLUE:
          /* keep red and green and modify blue */
          b                             = ( j & mask ) << maskbits;
          b                            |= b >> hambits;
          break;
        case HAMBITS_RED:
          /* keep green and blue and modify red */
          r                             = ( j & mask ) << maskbits;
          r                            |= r >> hambits;
          break;
        case HAMBITS_GREEN:
          /* keep red and blue and modify green */
          g                             = ( j & mask ) << maskbits;
          g                            |= g >> hambits;
          break;
        default:
          break;
      }
      /* put colors to buffer */
      truecolor_buf[buf_ptr++]          = r;
      truecolor_buf[buf_ptr++]          = g;
      truecolor_buf[buf_ptr]            = b;
    }
  }

  return truecolor_buf;
}

/* decoding method 4 */
static void bitplane_set_dlta_short (uint8_t *current_buffer,
  uint8_t *index_buf,
  /* uint8_t *delta_buffer,*/
  uint8_t *delta,
  /*int delta_buf_size,*/
  int dsize,
  int width,
  int height,
  int num_bitplanes ) {
  
  uint32_t rowsize                      = width / 8;
  
  uint32_t palette_index                = 0;
  uint32_t *deltadata                   = (uint32_t *)delta;
  uint16_t *ptr                         = NULL;
  uint16_t *planeptr                    = NULL;
  uint16_t *data                        = NULL;
  uint16_t *dest                        = NULL;
  int32_t s                             = 0;
  int32_t size                          = 0;
  int32_t nw                            = rowsize >> 1;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < num_bitplanes; palette_index++) {

    planeptr                            = (uint16_t *)(&current_buffer[(palette_index * rowsize)]);
    /* data starts at beginn of delta-Buffer + offset of the first */
    /* 32 Bit long word in the buffer. The buffer starts with 8    */
    /* of this Offset, for every bitplane (max 8) one              */
    data                                = (uint16_t *)(delta + BE_32(&deltadata[palette_index]));
    /* This 8 Pointers are followd by another 8                    */
    ptr                                 = (uint16_t *)(delta + BE_32(&deltadata[(palette_index+8)]));

    /* in this case, I think big/little endian is not important ;-) */
    while( *ptr !=  0xFFFF) {
      dest                              = planeptr + BE_16(ptr);
      ptr++;
      size                              = BE_16(ptr);
      ptr++;
      if (size < 0) {
        for (s = size; s < 0; s++) {
          *dest                         = *data;
          dest                         += nw;
        }
        data++;
      }
      else {
        for (s = 0; s < size; s++) {
          *dest = *data++;
          dest += nw;
        }
      }
    }
  }
  bitplane_decode_bitplane(current_buffer, index_buf, width, height, num_bitplanes, 1);
}

/* decoding method 5 */
static void bitplane_dlta_5 (uint8_t *current_buffer,
  uint8_t *index_buf,
  uint8_t *delta,
  int dsize,
  int width,
  int height,
  int num_bitplanes ) {
  
  uint32_t rowsize                      = width / 8;
  uint32_t rowsize_all_planes           = rowsize * num_bitplanes;

  uint32_t delta_offset                 = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t row_ptr                      = 0;
  uint32_t *deltadata                   = (uint32_t *)delta;
  uint8_t  *planeptr                    = NULL;
  uint8_t  *rowworkptr                  = NULL;
  uint8_t  *picture_end                 = current_buffer + (rowsize_all_planes * height);
  uint8_t  *data                        = NULL;
  uint8_t  *data_end                    = delta + dsize;
  uint8_t  op_count                     = 0;
  uint8_t  op                           = 0;
  uint8_t  count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < num_bitplanes; palette_index++) {

    planeptr                            = &current_buffer[(palette_index * rowsize)];
    /* data starts at beginn of delta-Buffer + offset of the first */
    /* 32 Bit long word in the buffer. The buffer starts with 8    */
    /* of this Offset, for every bitplane (max 8) one              */
    delta_offset                        = BE_32(&deltadata[palette_index]);

    if (delta_offset > 0) {
      data                              = delta + delta_offset;
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = planeptr + pixel_ptr;
        row_ptr                         = 0;
        /* execute ops */
        for( op_count = *data++; op_count; op_count--) {
          op                            = *data++;
          if (op & 0x80) {
            /* Uniq ops */
            count                       = op & 0x7f; /* get count */
            while(count--) {
              if (data > data_end || rowworkptr > picture_end)
                 return;
              IFF_REPLACE_BYTE( &index_buf[((row_ptr * width) + (pixel_ptr * 8))],
                                *rowworkptr, *data, bitplainoffeset[palette_index] );
              data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = *data++;
              while(count--) {
                if (data > data_end || rowworkptr > picture_end)
                   return;
                IFF_REPLACE_BYTE( &index_buf[((row_ptr * width) + (pixel_ptr * 8))],
                                  *rowworkptr, *data, bitplainoffeset[palette_index] );
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
              /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
}

/* decoding method 7 (short version) */
static void bitplane_dlta_7_short (uint8_t *current_buffer,
  uint8_t *index_buf,
  uint8_t *delta,
  int dsize,
  int width,
  int height,
  int num_bitplanes ) {

  uint32_t rowsize                      = width / 16;
  uint32_t rowsize_all_planes           = rowsize * num_bitplanes;

  uint32_t opcode_offset                = 0;
  uint32_t data_offset                  = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t row_ptr                      = 0;
  uint32_t *deltadata                   = (uint32_t *)delta;
  uint8_t  *planeptr                    = NULL;
  uint16_t *rowworkptr                  = NULL;
  uint16_t *picture_end                 = (uint16_t *)(&current_buffer[(rowsize_all_planes * 2 * height)]);
  uint16_t *data                        = NULL;
  uint16_t *data_end                    = (uint16_t *)(&delta[dsize]);
  uint8_t  *op_ptr                      = NULL;
  uint8_t  op_count                     = 0;
  uint8_t  op                           = 0;
  uint8_t  count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < num_bitplanes; palette_index++) {

    planeptr                            = &current_buffer[(palette_index * rowsize * 2)];
    /* find opcode and data offset (up to 8 pointers, one for every bitplane */
    opcode_offset                       = BE_32(&deltadata[palette_index]);
    data_offset                         = BE_32(&deltadata[palette_index + 8]);

    if (opcode_offset > 0 && data_offset > 0) {
      data                              = (uint16_t *)(&delta[data_offset]);
      op_ptr                            = delta + opcode_offset;
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = (uint16_t *)(&planeptr[pixel_ptr * 2]);
        row_ptr                         = 0;
        /* execute ops */
        for( op_count = *op_ptr++; op_count; op_count--) {
          op                            = *op_ptr++;
          if (op & 0x80) {
            /* Uniq ops */
            count                       = op & 0x7f; /* get count */
            while(count--) {
              if (data > data_end || rowworkptr > picture_end)
                 return;
/*              IFF_REPLACE_SHORT( &index_buf[((row_ptr * width) + (pixel_ptr * 16))],
                                *rowworkptr, *data, bitplainoffeset[palette_index] );
              data++;*/
              *rowworkptr += *data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = *op_ptr++;
              while(count--) {
                if (data > data_end || rowworkptr > picture_end)
                   return;
/*                IFF_REPLACE_SHORT( &index_buf[((row_ptr * width) + (pixel_ptr * 16))],
                                  *rowworkptr, *data, bitplainoffeset[palette_index] );*/
                *rowworkptr += *data;
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
              /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
}
              
/* decoding method 7 (long version) */
static void bitplane_dlta_7_long (uint8_t *current_buffer,
  uint8_t *index_buf,
  uint8_t *delta,
  int dsize,
  int width,
  int height,
  int num_bitplanes ) {
  
  uint32_t rowsize                      = width / 32;
  uint32_t rowsize_all_planes           = rowsize * num_bitplanes;

  uint32_t opcode_offset                = 0;
  uint32_t data_offset                  = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t row_ptr                      = 0;
  uint32_t *deltadata                   = (uint32_t *)delta;
  uint8_t  *planeptr                    = NULL;
  uint32_t *rowworkptr                  = NULL;
  uint32_t *picture_end                 = (uint32_t *)(&current_buffer[(rowsize_all_planes * 4 * height)]);
  uint32_t *data                        = NULL;
  uint32_t *data_end                    = (uint32_t *)(&delta[dsize]);
  uint8_t  *op_ptr                      = NULL;
  uint8_t  op_count                     = 0;
  uint8_t  op                           = 0;
  uint8_t  count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < num_bitplanes; palette_index++) {
    planeptr                            = &current_buffer[(palette_index * rowsize * 4)];
    /* find opcode and data offset (up to 8 pointers, one for every bitplane */
    opcode_offset                       = BE_32(&deltadata[palette_index]);
    data_offset                         = BE_32(&deltadata[palette_index + 8]);

    if (opcode_offset > 0 && data_offset > 0) {
      data                              = (uint32_t *)(&delta[data_offset]);
      op_ptr                            = delta + opcode_offset;
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = (uint32_t *)(&planeptr[pixel_ptr * 4]);
        row_ptr                         = 0;
        /* execute ops */
        for( op_count = *op_ptr++; op_count; op_count--) {
          op                            = *op_ptr++;
          if (op & 0x80) {
            /* Uniq ops */
            count                       = op & 0x7f; /* get count */
            while(count--) {
              if (data > data_end || rowworkptr > picture_end)
                return;
              IFF_REPLACE_LONG( &index_buf[((row_ptr * width) + (pixel_ptr * 32))],
                                *rowworkptr, *data, bitplainoffeset[palette_index] );
              data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = *op_ptr++;
              while(count--) {
                if (data > data_end || rowworkptr > picture_end)
                  return;
                IFF_REPLACE_LONG( &index_buf[((row_ptr * width) + (pixel_ptr * 32))],
                                  *rowworkptr, *data, bitplainoffeset[palette_index] );
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
             /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
}

/* decoding method 8 short */
static void bitplane_dlta_8_short (uint8_t *current_buffer,
  uint8_t *index_buf,
  uint8_t *delta,
  int dsize,
  int width,
  int height,
  int num_bitplanes ) {
  
  uint32_t rowsize                      = width / 16;
  uint32_t rowsize_all_planes           = rowsize * num_bitplanes;

  uint32_t delta_offset                 = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t row_ptr                      = 0;
  uint32_t *deltadata                   = (uint32_t *)delta;
  uint16_t *planeptr                    = NULL;
  uint16_t *rowworkptr                  = NULL;
  uint16_t *picture_end                 = (uint16_t *)(&current_buffer[(rowsize_all_planes * 2 * height)]);
  uint16_t *data                        = NULL;
  uint16_t *data_end                    = (uint16_t *)(&delta[dsize]);
  uint16_t op_count                     = 0;
  uint16_t op                           = 0;
  uint16_t count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < num_bitplanes; palette_index++) {

    planeptr                            = (uint16_t *)(&current_buffer[(palette_index * rowsize * 2)]);
    /* data starts at beginn of delta-Buffer + offset of the first */
    /* 32 Bit long word in the buffer. The buffer starts with 8    */
    /* of this Offset, for every bitplane (max 8) one              */
    delta_offset                        = BE_32(&deltadata[palette_index]);

    if (delta_offset > 0) {
      data                              = (uint16_t *)(&delta[delta_offset]);
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = planeptr + pixel_ptr;
        row_ptr                         = 0;
        /* execute ops */
        op_count = BE_16(data);
        data++;
        for( ; op_count; op_count--) {
          op                            = BE_16(data);
          data++;
          if (op & 0x8000) {
            /* Uniq ops */
            count                       = op & 0x7fff; /* get count */
            while(count--) {
              if (data > data_end || rowworkptr > picture_end)
                 return;
              IFF_REPLACE_SHORT( &index_buf[((row_ptr * width) + (pixel_ptr * 16))],
                                *rowworkptr, *data, bitplainoffeset[palette_index] );
              data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = BE_16(data);
              data++;
              while(count--) {
                if (data > data_end || rowworkptr > picture_end)
                   return;
                IFF_REPLACE_SHORT( &index_buf[((row_ptr * width) + (pixel_ptr * 16))],
                                  *rowworkptr, *data, bitplainoffeset[palette_index] );
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
              /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
}

/* decoding method 8 long */
static void bitplane_dlta_8_long (uint8_t *current_buffer,
  uint8_t *index_buf,
  uint8_t *delta,
  int dsize,
  int width,
  int height,
  int num_bitplanes ) {
  
  uint32_t rowsize                      = width / 32;
  uint32_t rowsize_all_planes           = rowsize * num_bitplanes;

  uint32_t delta_offset                 = 0;
  uint32_t palette_index                = 0;
  uint32_t pixel_ptr                    = 0;
  uint32_t row_ptr                      = 0;
  uint32_t *deltadata                   = (uint32_t *)delta;
  uint32_t *planeptr                    = NULL;
  uint32_t *rowworkptr                  = NULL;
  uint32_t *picture_end                 = (uint32_t *)(&current_buffer[(rowsize_all_planes * 4 * height)]);
  uint32_t *data                        = NULL;
  uint32_t *data_end                    = (uint32_t *)(&delta[dsize]);
  uint32_t op_count                     = 0;
  uint32_t op                           = 0;
  uint32_t count                        = 0;

  /* Repeat for each plane */
  for(palette_index = 0; palette_index < num_bitplanes; palette_index++) {

    planeptr                            = (uint32_t *)(&current_buffer[(palette_index * rowsize * 4)]);
    /* data starts at beginn of delta-Buffer + offset of the first */
    /* 32 Bit long word in the buffer. The buffer starts with 8    */
    /* of this Offset, for every bitplane (max 8) one              */
    delta_offset                        = BE_32(&deltadata[palette_index]);

    if (delta_offset > 0) {
      data                              = (uint32_t *)(&delta[delta_offset]);
      for( pixel_ptr = 0; pixel_ptr < rowsize; pixel_ptr++) {
        rowworkptr                      = planeptr + pixel_ptr;
        row_ptr                         = 0;
        /* execute ops */
        op_count = BE_32(data);
        data++;
        for( ; op_count; op_count--) {
          op                            = BE_32(data);
          data++;
          if (op & 0x80000000) {
            /* Uniq ops */
            count                       = op & 0x7fffffff; /* get count */
            while(count--) {
              if (data > data_end || rowworkptr > picture_end)
                 return;
              IFF_REPLACE_LONG( &index_buf[((row_ptr * width) + (pixel_ptr * 32))],
                                *rowworkptr, *data, bitplainoffeset[palette_index] );
              data++;
              rowworkptr               += rowsize_all_planes;
              row_ptr++;
            }
          } else {
            if (op == 0) {
              /* Same ops */
              count                     = BE_32(data);
              data++;
              while(count--) {
                if (data > data_end || rowworkptr > picture_end)
                   return;
                IFF_REPLACE_LONG( &index_buf[((row_ptr * width) + (pixel_ptr * 32))],
                                  *rowworkptr, *data, bitplainoffeset[palette_index] );
                rowworkptr             += rowsize_all_planes;
                row_ptr++;
              }
              data++;
            } else {
              /* Skip ops */
              rowworkptr               += (rowsize_all_planes * op);
              row_ptr                  += op;
            }
          }
        }
      }
    }
  }
}

static void bitplane_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  bitplane_decoder_t *this              = (bitplane_decoder_t *) this_gen;
  xine_bmiheader *bih                   = 0;
  palette_entry_t *palette              = 0;
  AnimHeader *anhd                      = NULL;
  int i                                 = 0;
  int j                                 = 0;
  int pixel_ptr                         = 0;
  int row_ptr                           = 0;
  int buf_ptr                           = 0;
  unsigned char r                       = 0;
  unsigned char g                       = 0;
  unsigned char b                       = 0;
  uint8_t *buf_exchange                 = NULL;

  vo_frame_t *img                       = 0; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette                             = (palette_entry_t *)buf->decoder_info_ptr[2];

    for (i = 0; i < buf->decoder_info[2]; i++) {
      this->yuv_palette[i * 4 + 0]      =
        COMPUTE_Y(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 1]      =
        COMPUTE_U(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 2]      =
        COMPUTE_V(palette[i].r, palette[i].g, palette[i].b);
      this->rgb_palette[i * 4 + 0]      = palette[i].r;
      this->rgb_palette[i * 4 + 1]      = palette[i].g;
      this->rgb_palette[i * 4 + 2]      = palette[i].b;
    }

    /* EHB Pictures not allways contain all 64 colors, sometimes only    */
    /* the first 32 are included and sometimes all 64 colors are provide,*/
    /* but second 32 are only stupid dirt, so recalculate them           */
    if (((this->num_bitplanes  == 6) &&
         (buf->decoder_info[2] == 32)) ||
        (this->camg_mode & CAMG_EHB)) {
      for (i = 32; i < 64; i++) {
        this->rgb_palette[i * 4 + 0]    = palette[(i-32)].r / 2;
        this->rgb_palette[i * 4 + 1]    = palette[(i-32)].g / 2;
        this->rgb_palette[i * 4 + 2]    = palette[(i-32)].b / 2;
        this->yuv_palette[i * 4 + 0]    =
           COMPUTE_Y(this->rgb_palette[i*4+0], this->rgb_palette[i*4+1], this->rgb_palette[i*4+2]);
        this->yuv_palette[i * 4 + 1]    =
           COMPUTE_U(this->rgb_palette[i*4+0], this->rgb_palette[i*4+1], this->rgb_palette[i*4+2]);
        this->yuv_palette[i * 4 + 2]    =
           COMPUTE_V(this->rgb_palette[i*4+0], this->rgb_palette[i*4+1], this->rgb_palette[i*4+2]);
       }
    }

    return;
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) { /* need to initialize */
    this->stream->video_out->open (this->stream->video_out, this->stream);

    if(this->buf)
      free(this->buf);

    bih                                 = (xine_bmiheader *) buf->content;
    this->width                         = bih->biWidth;
    this->width_decode                  = (bih->biWidth + 15) & ~0x0f;
    this->height                        = bih->biHeight;
    this->ratio                         = (double)this->width/(double)this->height;
    this->video_step                    = buf->decoder_info[1];
    /* Palette based Formates use up to 8 Bit per pixel, always use 8 Bit if less */
    this->bytes_per_pixel               = (bih->biBitCount + 1) / 8;
    if( this->bytes_per_pixel < 1 )
      this->bytes_per_pixel             = 1;
    if(bih->biCompression & CAMG_HAM )
      this->full_bytes_per_pixel        = 3;
    else
      this->full_bytes_per_pixel        = this->bytes_per_pixel;

    /* New Buffer for indexes (palette based formats) */
    if( this->bytes_per_pixel < 3 )
    {
      this->index_buf                   = xine_xmalloc( (this->width_decode * this->height * this->bytes_per_pixel) );
      this->index_buf_hist              = xine_xmalloc( (this->width_decode * this->height * this->bytes_per_pixel) );
    }
    
    /* New Buffer for RGB Colors */
    if(this->full_bytes_per_pixel > 1)
      this->rgb_buf                     = xine_xmalloc( (this->width_decode * this->height * this->full_bytes_per_pixel) );

    this->num_bitplanes                 = bih->biPlanes;
    this->camg_mode                     = bih->biCompression;

    if( buf->decoder_info[2]           != buf->decoder_info[3] &&
        buf->decoder_info[3]            > 0 ) {
      this->ratio                      *= buf->decoder_info[2];
      this->ratio                      /= buf->decoder_info[3];
    }

    if( (bih->biCompression & CAMG_HIRES) &&
        !(bih->biCompression & CAMG_LACE) ) {
      if( (buf->decoder_info[2] * 16) > (buf->decoder_info[3] * 10) )
        this->ratio                    /= 2.0;
    }

    if( !(bih->biCompression & CAMG_HIRES) &&
        (bih->biCompression & CAMG_LACE) ) {
      if( (buf->decoder_info[2] * 10) < (buf->decoder_info[3] * 16) )
        this->ratio                    *= 2.0;
    }

    if (this->buf)
      free (this->buf);
    this->bufsize                       = VIDEOBUFSIZE;
    this->buf                           = xine_xmalloc(this->bufsize);
    this->size                          = 0;

    init_yuv_planes(&this->yuv_planes, this->width, this->height);

    this->stream->video_out->open (this->stream->video_out, this->stream);
    this->decoder_ok                    = 1;

    /* load the stream/meta info */
    switch( buf->type ) {
      case BUF_VIDEO_BITPLANE:
        _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Uncompressed bitplane");
        break;
      case BUF_VIDEO_BITPLANE_BR1:
        _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "ByteRun1 bitplane");
        break;
      default:
        _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Unknown bitplane");
        break;
    }

    return;
  } else if (this->decoder_ok) {

    if (this->size + buf->size > this->bufsize) {
      this->bufsize                     = this->size + 2 * buf->size;
      this->buf                         = realloc (this->buf, this->bufsize);
    }

    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size                         += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
      this->video_step = buf->decoder_info[0];

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      img = this->stream->video_out->get_frame (this->stream->video_out,
                                        this->width, this->height,
                                        this->ratio, XINE_IMGFMT_YUY2,
                                        VO_BOTH_FIELDS);

      img->duration                     = this->video_step;
      img->pts                          = buf->pts;
      img->bad_frame                    = 0;
      anhd                              = (AnimHeader *)(buf->decoder_info_ptr[0]);

      if( (this->buf_uk    == NULL) ||
          (anhd            == NULL) ||
          (anhd->operation == IFF_ANHD_ILBM) ) {
       
        /* iterate through each row */
        buf_ptr                         = 0;
        this->size_uk                   = (((this->width_decode * this->height) / 8) * this->num_bitplanes);
      
        if( this->buf_uk_hist != NULL )
          xine_fast_memcpy (this->buf_uk_hist, this->buf_uk, this->size_uk);
        switch( buf->type ) {
          case BUF_VIDEO_BITPLANE:
            /* uncompressed Buffer, set decoded_buf pointer direct to input stream */
            if( this->buf_uk == NULL )
              this->buf_uk              = xine_xmalloc( (this->size) );
            xine_fast_memcpy (this->buf_uk, this->buf, this->size);
            break;
          case BUF_VIDEO_BITPLANE_BR1:
            /* create Buffer for decompressed bitmap */
            this->buf_uk                = bitplane_decode_byterun1(
                                                   this->buf,          /* compressed buffer         */
                                                   this->size,         /* size of compressed data   */
                                                   this->size_uk );    /* size of uncompressed data */

            if( this->buf_uk == NULL ) {
              xine_log(this->stream->xine, XINE_LOG_MSG,
                       _("bitplane: error doing ByteRun1 decompression\n"));
              _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
              return;
            }
            /* set pointer to decompressed Buffer */
            break;
          default:
            break;
        }
        if( this->bytes_per_pixel > 1 ) {
          bitplane_decode_bitplane(   this->buf_uk,              /* bitplane buffer         */
                                      this->rgb_buf,             /* rgb buffer, direct 24Bit*/
                                      this->width_decode,        /* width                   */
                                      this->height,              /* hight                   */
                                      this->num_bitplanes,       /* number bitplanes        */
                                      this->bytes_per_pixel);    /* used Bytes per pixel    */
        } else {
          bitplane_decode_bitplane(   this->buf_uk,              /* bitplane buffer         */
                                      this->index_buf,           /* index buffer            */
                                      this->width_decode,        /* width                   */
                                      this->height,              /* hight                   */
                                      this->num_bitplanes,       /* number bitplanes        */
                                      this->bytes_per_pixel);    /* used Bytes per pixel    */

          if( this->buf_uk_hist == NULL ) {
            this->buf_uk_hist           = xine_xmalloc( (this->size_uk) );
            xine_fast_memcpy (this->buf_uk_hist, this->buf_uk, this->size_uk);
            xine_fast_memcpy (this->index_buf_hist, this->index_buf,
                              (this->width_decode * this->height * this->bytes_per_pixel));
          }
        }
      } else {
        /* when no start-picture is given, create a empty one */
        if( this->buf_uk_hist == NULL ) {
          this->size_uk                 = (((this->width_decode * this->height) / 8) * this->num_bitplanes);
          this->buf_uk                  = xine_xmalloc( (this->size_uk) );
          this->buf_uk_hist             = xine_xmalloc( (this->size_uk) );
          for (i = 0; i < this->size_uk; i++) {
            this->buf_uk                = 0;
            this->buf_uk_hist           = 0;
          }
        }
        if( this->index_buf == NULL ) {
          this->index_buf               = xine_xmalloc( (this->width_decode * this->height * this->bytes_per_pixel) );
          this->index_buf_hist          = xine_xmalloc( (this->width_decode * this->height * this->bytes_per_pixel) );
          for (i = 0; i < (this->width_decode * this->height * this->bytes_per_pixel); i++) {
            this->index_buf[i]          = 0;
            this->index_buf_hist[i]     = 0;
          }
        }

        switch( anhd->operation ) {
          /* also known as IFF-ANIM OPT1 (never seen in real world) */
          case IFF_ANHD_XOR:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: Anim Opt 1 is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
          /* also known as IFF-ANIM OPT2 (never seen in real world) */
          case IFF_ANHD_LDELTA:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: Anim Opt 2 is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
          /* also known as IFF-ANIM OPT3 */
          case IFF_ANHD_SDELTA:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: Anim Opt 3 is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
          /* also known as IFF-ANIM OPT4 (never seen in real world) */
          case IFF_ANHD_SLDELTA:
            _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT4 (SLDELTA)");
            bitplane_set_dlta_short ( this->buf_uk_hist,
                                      this->index_buf_hist,
                                      this->buf,
                                      this->size,
                                      this->width,
                                      this->height,
                                      this->num_bitplanes);
            break;
          /* also known as IFF-ANIM OPT5 */
          case IFF_ANHD_BVDELTA:
            _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT5 (BVDELTA)");
            bitplane_dlta_5 (         this->buf_uk_hist,
                                      this->index_buf_hist,
                                      this->buf,
                                      this->size,
                                      this->width,
                                      this->height,
                                      this->num_bitplanes);
            break;
          case IFF_ANHD_STEREOO5:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: Anim Opt 6 is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
          case IFF_ANHD_OPT7:
            if(anhd->bits == 0) {
              _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT7 (SHORT)");
              bitplane_dlta_7_short ( this->buf_uk_hist,
                                      this->index_buf_hist,
                                      this->buf,
                                      this->size,
                                      this->width,
                                      this->height,
                                      this->num_bitplanes);
            } else {
              _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT7 (LONG)");
              bitplane_dlta_7_long (  this->buf_uk_hist,
                                      this->index_buf_hist,
                                      this->buf,
                                      this->size,
                                      this->width,
                                      this->height,
                                      this->num_bitplanes);
            }
            break;
          case IFF_ANHD_OPT8:
            if(anhd->bits == 0) {
              _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT8 (SHORT)");
              bitplane_dlta_8_short ( this->buf_uk_hist,
                                      this->index_buf_hist,
                                      this->buf,
                                      this->size,
                                      this->width,
                                      this->height,
                                      this->num_bitplanes);
            } else {
              _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Anim OPT8 (LONG)");
              bitplane_dlta_8_long (  this->buf_uk_hist,
                                      this->index_buf_hist,
                                      this->buf,
                                      this->size,
                                      this->width,
                                      this->height,
                                      this->num_bitplanes);
            }
            break;
          case IFF_ANHD_ASCIIJ:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: Anim ASCIIJ is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
          default:
            xine_log(this->stream->xine, XINE_LOG_MSG,
                     _("bitplane: This anim-type is not supported at the moment\n"));
            _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
            return;
            break;
        }
        /* change old bitmap buffer (which now is the new one) with new buffer */
        buf_exchange                    = this->buf_uk;
        this->buf_uk                    = this->buf_uk_hist;
        this->buf_uk_hist               = buf_exchange;
        /* do the same with the index buffer */
        buf_exchange                    = this->index_buf;
        this->index_buf                 = this->index_buf_hist;
        this->index_buf_hist            = buf_exchange;
      }

      /* HAM-pictrues need special handling */
      if( this->camg_mode & CAMG_HAM ) {
        /* HAM-Pictures must always be extended to 24-Bit RGB, so extended buffer is needed */
        bitplane_decode_ham( this->index_buf,                /* HAM-bitplane buffer     */
                             this->rgb_buf,                  /* 24 Bit RGB buffer       */
                             this->width_decode,             /* width                   */
                             this->height,                   /* hight                   */
                             this->num_bitplanes,            /* number bitplanes        */
                             this->full_bytes_per_pixel,     /* used Bytes per pixel    */
                             this->rgb_palette);             /* Palette (RGB)           */
      }

      switch (this->full_bytes_per_pixel) {
        case 1:
          for (row_ptr = 0; row_ptr < this->height; row_ptr++) {
            for (pixel_ptr = 0; pixel_ptr < this->width; pixel_ptr++) {
              i = (row_ptr * this->width_decode) + pixel_ptr;
              j = (row_ptr * this->width) + pixel_ptr;
              this->yuv_planes.y[j]     = this->yuv_palette[this->index_buf[i] * 4 + 0];
              this->yuv_planes.u[j]     = this->yuv_palette[this->index_buf[i] * 4 + 1];
              this->yuv_planes.v[j]     = this->yuv_palette[this->index_buf[i] * 4 + 2];
            }
          }
          break;
        case 3:
          for (row_ptr = 0; row_ptr < this->height; row_ptr++) {
            for (pixel_ptr = 0; pixel_ptr < this->width; pixel_ptr++) {
              i                         = (row_ptr * this->width_decode * this->full_bytes_per_pixel) +
                                          (pixel_ptr * this->full_bytes_per_pixel);
              j                         = (row_ptr * this->width) + pixel_ptr;
              r                         = this->rgb_buf[i++];
              g                         = this->rgb_buf[i++];
              b                         = this->rgb_buf[i];

              this->yuv_planes.y[j]     = COMPUTE_Y(r, g, b);
              this->yuv_planes.u[j]     = COMPUTE_U(r, g, b);
              this->yuv_planes.v[j]     = COMPUTE_V(r, g, b);
            }
          }
          break;
        default:
          break;
      }

      yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

      img->draw(img, this->stream);
      img->free(img);

      this->size                        = 0;
      if ( buf->decoder_info[1] > 90000 )
        xine_usec_sleep(buf->decoder_info[1]);
    }
  }
}

/*
 * This function is called when xine needs to flush the system. Not
 * sure when or if this is used or even if it needs to do anything.
 */
static void bitplane_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void bitplane_reset (video_decoder_t *this_gen) {
  bitplane_decoder_t *this              = (bitplane_decoder_t *) this_gen;

  this->size                            = 0;
}

static void bitplane_discontinuity (video_decoder_t *this_gen) {
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void bitplane_dispose (video_decoder_t *this_gen) {
  bitplane_decoder_t *this              = (bitplane_decoder_t *) this_gen;

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->buf_uk) {
    free (this->buf_uk);
    this->buf_uk = NULL;
  }

  if (this->buf_uk_hist) {
    free (this->buf_uk_hist);
    this->buf_uk_hist = NULL;
  }

  if (this->index_buf) {
    free (this->index_buf);
    this->index_buf = NULL;
  }

  if (this->index_buf_hist) {
    free (this->index_buf_hist);
    this->index_buf_hist = NULL;
  }

  if (this->rgb_buf) {
    free (this->rgb_buf);
    this->rgb_buf = NULL;
  }

  if (this->decoder_ok) {
    this->decoder_ok                    = 0;
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  bitplane_decoder_t  *this             = (bitplane_decoder_t *) xine_xmalloc (sizeof (bitplane_decoder_t));

  this->video_decoder.decode_data       = bitplane_decode_data;
  this->video_decoder.flush             = bitplane_flush;
  this->video_decoder.reset             = bitplane_reset;
  this->video_decoder.discontinuity     = bitplane_discontinuity;
  this->video_decoder.dispose           = bitplane_dispose;
  this->size                            = 0;

  this->stream                          = stream;
  this->class                           = (bitplane_class_t *) class_gen;

  this->decoder_ok                      = 0;
  this->buf                             = NULL;
  this->buf_uk                          = NULL;
  this->index_buf                       = NULL;
  this->rgb_buf                         = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "bitplane";
}

static char *get_description (video_decoder_class_t *this) {
  return "Raw bitplane video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  bitplane_class_t *this                = (bitplane_class_t *) xine_xmalloc (sizeof (bitplane_class_t));

  this->decoder_class.open_plugin       = open_plugin;
  this->decoder_class.get_identifier    = get_identifier;
  this->decoder_class.get_description   = get_description;
  this->decoder_class.dispose           = dispose_class;

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t video_types[] = {
  BUF_VIDEO_BITPLANE,
  BUF_VIDEO_BITPLANE_BR1,
  0
};

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 18, "bitplane", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
