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
 * $Id: bitplane.c,v 1.2 2004/02/09 22:04:11 jstembridge Exp $
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

#define CAMG_LACE  0x0004   /* Interlaced Modi */
#define CAMG_EHB   0x0080   /* extra halfe brite */
#define CAMG_HAM   0x0800   /* hold and modify */
#define CAMG_HIRES 0x8000   /* Hires Modi */

#define HAMBITS_CMAP      0 /* take color from colormap */
#define HAMBITS_BLUE      1 /* modify blue  component */
#define HAMBITS_RED       2 /* modify red   component */
#define HAMBITS_GREEN     3 /* modify green component */

int bitplainoffeset[] = {       1,       2,       4,       8,
                               16,      32,      64,     128,
                                1,       2,       4,       8,
                               16,      32,      64,     128,
                                1,       2,       4,       8,
                               16,      32,      64,     128
                        };

typedef struct {
  video_decoder_class_t   decoder_class;
} bitplane_class_t;

typedef struct bitplane_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  bitplane_class_t      *class;
  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object */
  uint64_t          video_step;  /* frame duration in pts units */
  int               decoder_ok;  /* current decoder status */
  int               skipframes;

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */

  int               width_decode;/* the width of a video frame decoding*/
  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */
  double            ratio;       /* the width to height ratio */
  int               bytes_per_pixel;
  int               num_bitplanes;
  int               camg_mode;

  unsigned char     yuv_palette[256 * 4];
  unsigned char     rgb_palette[256 * 4];
  yuv_planes_t      yuv_planes;

} bitplane_decoder_t;

static void bitplane_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  bitplane_decoder_t *this              = (bitplane_decoder_t *) this_gen;
  xine_bmiheader *bih                   = 0;
  palette_entry_t *palette              = 0;
  int i                                 = 0;
  int j                                 = 0;
  int pixel_ptr                         = 0;
  int row_ptr                           = 0;
  int palette_index                     = 0;
  int buf_ptr                           = 0;
  int hambits                           = 0;
  int maskbits                          = 0;
  int mask                              = 0;
  unsigned char r                       = 0;
  unsigned char g                       = 0;
  unsigned char b                       = 0;
  uint8_t *bitmap_buf                   = 0;
  uint8_t *index_buf                    = 0;
  uint8_t *decoded_buf                  = 0;

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
  
  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
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
    /* Palette based Formates use up to 8 Bit per pixel, always use 8 Bit if less */
    this->bytes_per_pixel               = (bih->biBitCount + 1) / 8;
    if( this->bytes_per_pixel < 1 )
      this->bytes_per_pixel             = 1;

    this->num_bitplanes                 = bih->biPlanes;
    this->camg_mode                     = bih->biCompression;

    if( buf->decoder_info[2]           != buf->decoder_info[3] &&
        buf->decoder_info[3]            > 0 ) {
      this->ratio                      *= buf->decoder_info[2];
      this->ratio                      /= buf->decoder_info[3];
    }

    if( (bih->biCompression & CAMG_HIRES) &&
        !(bih->biCompression & CAMG_LACE) ) {
      if( (buf->decoder_info[2] * 18) > (buf->decoder_info[3] * 10) )
        this->ratio                    /= 2.0;
    }

    if( !(bih->biCompression & CAMG_HIRES) &&
        (bih->biCompression & CAMG_LACE) ) {
      if( (buf->decoder_info[2] * 10) < (buf->decoder_info[3] * 18) )
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

    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      img = this->stream->video_out->get_frame (this->stream->video_out,
                                        this->width, this->height,
                                        this->ratio, XINE_IMGFMT_YUY2,
                                        VO_BOTH_FIELDS);

      img->duration                     = this->video_step;
      img->pts                          = buf->pts;
      img->bad_frame                    = 0;

      /* iterate through each row */
      buf_ptr = 0;

      switch( buf->type ) {
        case BUF_VIDEO_BITPLANE:
          /* uncompressed Buffer, set decoded_buf pointer direct to input stream */
          decoded_buf                   = this->buf;
          break;
        case BUF_VIDEO_BITPLANE_BR1:
          /* create Buffer for decompressed bitmap */
          bitmap_buf = xine_xmalloc( ((this->width_decode * this->height) / 8) * this->num_bitplanes );
          /* BytRun1 decompression */
          pixel_ptr                     = 0;
          i                             = 0;
          while ( i < this->size &&
                  pixel_ptr < (this->width_decode * this->height * this->bytes_per_pixel) ) {
            if( this->buf[i] <= 127 ) {
              j = this->buf[i++];
              if( (i+j) > this->size ) {
                xine_log(this->stream->xine, XINE_LOG_MSG,
                         _("bitplane: error doing ByteRun1 decompression(1)\n"));
                free(bitmap_buf);
                _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
                return;
              }
              for( ; j >= 0; j-- ) {
                bitmap_buf[pixel_ptr++] = this->buf[i++];
              }
            } else if ( this->buf[i] > 128 ) {
              j = 256 - this->buf[i++];
              if( i >= this->size ) {
                xine_log(this->stream->xine, XINE_LOG_MSG,
                         _("bitplane: error doing ByteRun1 decompression(2)\n"));
                free(bitmap_buf);
                _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
                return;
              }
              for( ; j >= 0; j-- ) {
                bitmap_buf[pixel_ptr++] = this->buf[i];
              }
              i++;
            }
          }
          /* set pointer to decompressed Buffer */
          decoded_buf                   = bitmap_buf;
          break;
      }

      /* New Buffer for index (palette based formats) or RGB Colors */
      index_buf                         = xine_xmalloc( (this->width_decode * this->height * this->bytes_per_pixel) );

      /* decode Bitplanes to RGB/Index Numbers */
      for (row_ptr = 0; row_ptr < this->height; row_ptr++) {
        for (palette_index = 0; palette_index < this->num_bitplanes; palette_index++) {
          for (pixel_ptr = 0; pixel_ptr < (this->width_decode / 8); pixel_ptr++) {
            i                           = (row_ptr * this->width_decode * this->bytes_per_pixel) +
                                          (pixel_ptr * this->bytes_per_pixel * 8) +
                                          ((palette_index > 15) ? 2 : (palette_index > 7) ? 1 : 0);
            j                           = (row_ptr * (this->width_decode / 8) * this->num_bitplanes) +
                                          (palette_index * (this->width_decode / 8)) +
                                          pixel_ptr;
            index_buf[i]               += ((decoded_buf[j] & 0x80) ? bitplainoffeset[palette_index] : 0);
            i                          += this->bytes_per_pixel;
            index_buf[i]               += ((decoded_buf[j] & 0x40) ? bitplainoffeset[palette_index] : 0);
            i                          += this->bytes_per_pixel;
            index_buf[i]               += ((decoded_buf[j] & 0x20) ? bitplainoffeset[palette_index] : 0);
            i                          += this->bytes_per_pixel;
            index_buf[i]               += ((decoded_buf[j] & 0x10) ? bitplainoffeset[palette_index] : 0);
            i                          += this->bytes_per_pixel;
            index_buf[i]               += ((decoded_buf[j] & 0x08) ? bitplainoffeset[palette_index] : 0);
            i                          += this->bytes_per_pixel;
            index_buf[i]               += ((decoded_buf[j] & 0x04) ? bitplainoffeset[palette_index] : 0);
            i                          += this->bytes_per_pixel;
            index_buf[i]               += ((decoded_buf[j] & 0x02) ? bitplainoffeset[palette_index] : 0);
            i                          += this->bytes_per_pixel;
            index_buf[i]               += ((decoded_buf[j] & 0x01) ? bitplainoffeset[palette_index] : 0);
          }
        }
      }

      /* is there a buffer for uncompressed bitplane? we don't need anymore! */
      if( bitmap_buf > 0 )
        free(bitmap_buf);
      bitmap_buf                        = 0;

      /* HAM-pictrues need special handling */
      if( (this->bytes_per_pixel == 1) &&
          (this->camg_mode & CAMG_HAM) ) {
        /* HAM-Pictures must always be extended to 24-Bit RGB, so extended buffer is needed */
        this->bytes_per_pixel           = 3;
        /* position of special HAM-Bits differs in HAM6 and HAM8, detect them */
        hambits                         = this->num_bitplanes > 6 ? 6 : 4;
        /* the other bits contain the real data, dreate a mask out of it */
        maskbits                        = 8 - hambits;
        mask                            = ( 1 << hambits ) - 1;

        /* one more step, make index_buf to decode_buf */
        decoded_buf                     = index_buf;
        /* and allocate a new index_buf */
        index_buf                       = xine_xmalloc( (this->width_decode * this->height * this->bytes_per_pixel) );
        for (pixel_ptr = 0; pixel_ptr < (this->width_decode * this->height * this->bytes_per_pixel); pixel_ptr++) {
          index_buf[pixel_ptr]          = 0;
        }

        for (row_ptr = 0; row_ptr < this->height; row_ptr++) {
          for (pixel_ptr = 0; pixel_ptr < this->width_decode; pixel_ptr++) {
            i                           = (row_ptr * this->width_decode) + pixel_ptr;
            buf_ptr                     = (row_ptr * this->width_decode * this->bytes_per_pixel) +
                                          (pixel_ptr * this->bytes_per_pixel);
            j                           = decoded_buf[i];
            switch ( j >> hambits ) {
              case HAMBITS_CMAP:
                /* Take colors from palette */
                r                       = this->rgb_palette[(j & mask) * 4 + 0];
                g                       = this->rgb_palette[(j & mask) * 4 + 1];
                b                       = this->rgb_palette[(j & mask) * 4 + 2];
                break;
              case HAMBITS_BLUE:
                /* keep red and green and modify blue */
                b                        = ( j & mask ) << maskbits;
                b                       |= b >> hambits;
                break;
              case HAMBITS_RED:
                /* keep green and blue and modify red */
                r                        = ( j & mask ) << maskbits;
                r                       |= r >> hambits;
                break;
              case HAMBITS_GREEN:
                /* keep red and blue and modify green */
                g                        = ( j & mask ) << maskbits;
                g                       |= g >> hambits;
                break;
              default:
                break;
            }
            /* put colors to buffer */
            index_buf[buf_ptr]           = r;
            index_buf[buf_ptr+1]         = g;
            index_buf[buf_ptr+2]         = b;
          }
        }
        /* free the buffer of the HAM-Picture */
        if( decoded_buf > 0 )
          free(decoded_buf);
        decoded_buf                     = 0;
      }

      switch (this->bytes_per_pixel) {
        case 1:
          for (row_ptr = 0; row_ptr < this->height; row_ptr++) {
            for (pixel_ptr = 0; pixel_ptr < this->width; pixel_ptr++) {
              i = (row_ptr * this->width_decode) + pixel_ptr;
              j = (row_ptr * this->width) + pixel_ptr;
              this->yuv_planes.y[j]     = this->yuv_palette[index_buf[i] * 4 + 0];
              this->yuv_planes.u[j]     = this->yuv_palette[index_buf[i] * 4 + 1];
              this->yuv_planes.v[j]     = this->yuv_palette[index_buf[i] * 4 + 2];
            }
          }
          break;
        case 3:
          for (row_ptr = 0; row_ptr < this->height; row_ptr++) {
            for (pixel_ptr = 0; pixel_ptr < this->width; pixel_ptr++) {
              i                         = (row_ptr * this->width_decode * this->bytes_per_pixel) +
                                          (pixel_ptr * this->bytes_per_pixel);
              j                         = (row_ptr * this->width) + pixel_ptr;
              r                         = index_buf[i++];
              g                         = index_buf[i++];
              b                         = index_buf[i];

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

      if( index_buf > 0 )
        free(index_buf);
      this->size                        = 0;
      if ( buf->decoder_info[1] > 0 )
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
