/* 
 * Copyright (C) 2002 the xine project
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
 * by Ewald Snel <ewald@rambo.its.tudelft.nl>
 *
 * based on overview of Microsoft Video-1 algorithm
 * by Mike Melanson: http://www.pcisys.net/~melanson/codecs/video1.txt
 *
 * $Id: msvc.c,v 1.3 2002/05/25 19:19:19 siggi Exp $
 */

#include <stdlib.h>
#include <unistd.h>

#include "video_out.h"
#include "buffer.h"
#include "bswap.h"
#include "xineutils.h"
#include "xine_internal.h"

#define VIDEOBUFSIZE	128 * 1024

/* now this is ripped of wine's vfw.h */
typedef struct {
    long        biSize;
    long        biWidth;
    long        biHeight;
    short       biPlanes;
    short       biBitCount;
    long        biCompression;
    long        biSizeImage;
    long        biXPelsPerMeter;
    long        biYPelsPerMeter;
    long        biClrUsed;
    long        biClrImportant;
} BITMAPINFOHEADER;

typedef struct {
  uint16_t yu;
  uint16_t yv;
} yuy2_t;

typedef struct msvc_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t	   *video_out;
  int64_t           video_step;
  int		    decoder_ok;

  unsigned char	   *buf;
  int		    bufsize;
  int		    size;

  uint16_t	    biWidth;
  uint16_t	    biHeight;
  uint32_t	    biBitCount;
  uint16_t	   *img_buffer;
  yuy2_t	    color_table[256];
} msvc_decoder_t;

/* taken from libw32dll */
#define	MAXSAMPLE	255
#define	CENTERSAMPLE	128

#define	SCALEBITS	16
#define	FIX(x)	 	( (int32_t) ( (x) * (1<<SCALEBITS) + 0.5 ) )
#define	ONE_HALF	( (int32_t) (1<< (SCALEBITS-1)) )
#define	CBCR_OFFSET	(CENTERSAMPLE << SCALEBITS)

static inline void rgb_to_yuy2 (const int bits, uint32_t rgb, yuy2_t *c) {
  uint8_t  r, g, b;
  uint8_t  y, u, v;

  if (bits == 15) {
    b = (rgb & 0x001F) << 3;
    g = (rgb & 0x03E0) >> 5 << 3;
    r = (rgb & 0x7C00) >> 10 << 3;
  } else {
    b = (rgb & 0x0000FF);
    g = (rgb & 0x00FF00) >> 8;
    r = (rgb & 0xFF0000) >> 16;
  }

  y = (FIX(0.299) * r + FIX(0.587) * g + FIX(0.114) * b + ONE_HALF) >> SCALEBITS;
  u = (- FIX(0.16874) * r - FIX(0.33126) * g + FIX(0.5) * b + CBCR_OFFSET + ONE_HALF-1) >> SCALEBITS;
  v = (FIX(0.5) * r - FIX(0.41869) * g - FIX(0.08131) * b + CBCR_OFFSET + ONE_HALF-1) >> SCALEBITS;

  c->yu = le2me_16 (y | (u << 8));
  c->yv = le2me_16 (y | (v << 8));
}

static void cram_decode_frame (msvc_decoder_t *this, uint8_t *data, int size) {
  uint8_t  *eod = (data + size);
  uint32_t  ctrl, clr8, skip;
  int	    x, y, i, j;
  uint16_t *out[4];
  yuy2_t    c[2];

  skip = 0;

  for (y=(this->biHeight - 4); y >= 0; y-=4) {

    out[0] = this->img_buffer + (y * this->biWidth);
    out[1] = out[0] + this->biWidth;
    out[2] = out[1] + this->biWidth;
    out[3] = out[2] + this->biWidth;

    for (x=0; x < this->biWidth; x+=4) {
      if (skip == 0 || --skip == 0) {
	if ((data + 2) >= eod)
	  return;

	ctrl  = data[0] | (data[1] << 8);
	data += 2;

	if ((ctrl & ((this->biBitCount == 8) ? 0xF000 : 0x8000)) == 0x8000) {
	  if ((ctrl & ~0x3FF) == 0x8400) {
	    skip = (ctrl & 0x3FF);
	  } else {
	    if (this->biBitCount == 8)
	      c[0] = this->color_table[(ctrl & 0xFF)];
	    else
	      rgb_to_yuy2 (15, ctrl, &c[0]);

	    for (i=0; i < 4; i++) {
	      out[i][0] = c[0].yu;
	      out[i][1] = c[0].yv;
	      out[i][2] = c[0].yu;
	      out[i][3] = c[0].yv;
	    }
	  }
	} else {
	  if (this->biBitCount == 8) {
	    if ((data + 2) >= eod)
	      return;

	    c[1] = this->color_table[data[0]];
	    c[0] = this->color_table[data[1]];
	    clr8 = (ctrl >= 0x9000);
	    data += 2;
	  } else {
	    if ((data + 4) >= eod)
	      return;

	    rgb_to_yuy2 (15, (data[0] | (data[1] << 8)), &c[1]);
	    rgb_to_yuy2 (15, (data[2] | (data[3] << 8)), &c[0]);
	    clr8 = (data[1] & 0x80);
	    data += 4;
	  }

	  for (i=0; i < 4; i+=2) {
	    for (j=0; j < 4; j+=2) {
	      out[3-i][j]   = c[(ctrl & 1)     ].yu;
	      out[3-i][j+1] = c[(ctrl & 2) >> 1].yv;
	      out[2-i][j]   = c[(ctrl & 16)>> 4].yu;
	      out[2-i][j+1] = c[(ctrl & 32)>> 5].yv;
	      ctrl >>= 2;

	      if (clr8 && !(i & j)) {
		if (this->biBitCount == 8) {
		  if ((data + 2) >= eod)
		    return;

		  c[1] = this->color_table[data[0]];
		  c[0] = this->color_table[data[1]];
		  data += 2;
		} else {
		  if ((data + 4) >= eod)
		   return;
 
		  rgb_to_yuy2 (15, (data[0] | (data[1] << 8)), &c[1]);
		  rgb_to_yuy2 (15, (data[2] | (data[3] << 8)), &c[0]);
		  data += 4;
		}
	      }
	    }

	    ctrl >>= 4;
	  }
        }
      }

      out[0] += 4;
      out[1] += 4;
      out[2] += 4;
      out[3] += 4;
    }
  }
}

static int msvc_can_handle (video_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_VIDEO_MSVC);
}

static void msvc_init (video_decoder_t *this_gen, vo_instance_t *video_out) {
  msvc_decoder_t *this = (msvc_decoder_t *) this_gen;

  this->video_out  = video_out;
  this->decoder_ok = 0;
}

static void msvc_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  msvc_decoder_t *this = (msvc_decoder_t *) this_gen;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    BITMAPINFOHEADER *bih;

    bih = (BITMAPINFOHEADER *) buf->content;
    this->biWidth = (le2me_32 (bih->biWidth) + 3) & ~0x03;
    this->biHeight = (le2me_32 (bih->biHeight) + 3) & ~0x03;
    this->biBitCount = le2me_32 (bih->biBitCount);
    this->video_step = buf->decoder_info[1];

    if (this->biBitCount != 8 && this->biBitCount != 16) {
      fprintf (stderr, "Unsupported bit depth (%d)\n", this->biBitCount);
      return;
    }

    if (this->img_buffer)
      free (this->img_buffer);
    this->img_buffer = malloc((this->biWidth * this->biHeight) << 1);

    /* FIXME: Palette not loaded */
#if 0
    for (i=0; i < 256; i++) {
      rgb_to_yuy2 (32, le2me_32 (rgb[i]), &this->color_table[i]);
    }
#endif

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    this->video_out->open (this->video_out);
    this->decoder_ok = 1;

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

      vo_frame_t *img;
      int         n = (this->biWidth * this->biHeight);

      cram_decode_frame (this, this->buf, this->size);

      img = this->video_out->get_frame (this->video_out,
					this->biWidth, this->biHeight,
					42, IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts	     = buf->pts;
      img->bad_frame = 0;

      xine_fast_memcpy (img->base[0], this->img_buffer, (n << 1));

      img->draw(img);
      img->free(img);

      this->size = 0;
    }
  }
}

static void msvc_flush (video_decoder_t *this_gen) {
}

static void msvc_reset (video_decoder_t *this_gen) {
  msvc_decoder_t *this = (msvc_decoder_t *) this_gen;

  this->size = 0;
}

static void msvc_close (video_decoder_t *this_gen) {

  msvc_decoder_t *this = (msvc_decoder_t *) this_gen;

  if (this->img_buffer) {
    free (this->img_buffer);
    this->img_buffer = NULL;
  }

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->decoder_ok) {  
    this->decoder_ok = 0;
    this->video_out->close(this->video_out);
  }
}

static char *msvc_get_id(void) {
  return "msvc";
}

static void msvc_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

video_decoder_t *init_video_decoder_plugin (int iface_version, xine_t *xine) {

  msvc_decoder_t *this ;

  if (iface_version != 9) {
    printf( "msvc: plugin doesn't support plugin API version %d.\n"
	    "msvc: this means there's a version mismatch between xine and this "
	    "msvc: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  this = (msvc_decoder_t *) malloc (sizeof (msvc_decoder_t));
  memset(this, 0, sizeof (msvc_decoder_t));

  this->video_decoder.interface_version   = iface_version;
  this->video_decoder.can_handle          = msvc_can_handle;
  this->video_decoder.init                = msvc_init;
  this->video_decoder.decode_data         = msvc_decode_data;
  this->video_decoder.flush               = msvc_flush;
  this->video_decoder.reset               = msvc_reset;
  this->video_decoder.close               = msvc_close;
  this->video_decoder.get_identifier      = msvc_get_id;
  this->video_decoder.dispose             = msvc_dispose;
  this->video_decoder.priority            = 5;

  return (video_decoder_t *) this;
}
