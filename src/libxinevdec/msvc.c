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
 * $Id: msvc.c,v 1.21 2002/12/21 12:56:49 miguelfreitas Exp $
 */

#include <stdlib.h>
#include <unistd.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "bswap.h"
#include "xineutils.h"

#define VIDEOBUFSIZE	128 * 1024

typedef struct {
  uint16_t yu;
  uint16_t yv;
} yuy2_t;

typedef struct {
  video_decoder_class_t   decoder_class;
} msvc_class_t;

typedef struct msvc_decoder_s {
  video_decoder_t   video_decoder;

  msvc_class_t     *class;
  xine_stream_t    *stream;

  int64_t           video_step;
  int		    decoder_ok;

  unsigned char	   *buf;
  int		    bufsize;
  int		    size;

  unsigned int	    coded_width;
  unsigned int	    coded_height;
  int		    pitch;
  int		    depth;
  uint8_t	   *current;
  yuy2_t	    color_table[256];

  unsigned int	    width;
  unsigned int	    height;
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

static int msvc_decode_frame (msvc_decoder_t *this, uint8_t *data, int size) {
  uint8_t  *eod = (data + size);
  uint32_t  ctrl, clr8, skip;
  int	    x, y, i, j;
  uint16_t *out[4];
  yuy2_t    c[2];

  skip = 0;

  for (y=(this->coded_height - 4); y >= 0; y-=4) {

    out[0] = (uint16_t *) &this->current[y * this->pitch];
    out[1] = (uint16_t *) (((uint8_t *) out[0]) + this->pitch);
    out[2] = (uint16_t *) (((uint8_t *) out[1]) + this->pitch);
    out[3] = (uint16_t *) (((uint8_t *) out[2]) + this->pitch);

    for (x=0; x < this->coded_width; x+=4) {
      if (skip == 0 || --skip == 0) {
	if ((data + 2) >= eod)
	  return 0;

	ctrl  = LE_16 (data);
	data += 2;

	if ((ctrl & ((this->depth == 8) ? 0xF000 : 0x8000)) == 0x8000) {
	  if ((ctrl & ~0x3FF) == 0x8400) {
	    skip = (ctrl & 0x3FF);
	  } else {
	    if (this->depth == 8)
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
	  if (this->depth == 8) {
	    if ((data + 2) >= eod)
	      return -1;

	    c[1] = this->color_table[data[0]];
	    c[0] = this->color_table[data[1]];
	    clr8 = (ctrl >= 0x9000);
	    data += 2;
	  } else {
	    if ((data + 4) >= eod)
	      return -1;

	    rgb_to_yuy2 (15, LE_16 (&data[0]), &c[1]);
	    rgb_to_yuy2 (15, LE_16 (&data[2]), &c[0]);
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
		if (this->depth == 8) {
		  if ((data + 2) >= eod)
		    return -1;

		  c[1] = this->color_table[data[0]];
		  c[0] = this->color_table[data[1]];
		  data += 2;
		} else {
		  if ((data + 4) >= eod)
		   return -1;
 
		  rgb_to_yuy2 (15, LE_16 (&data[0]), &c[1]);
		  rgb_to_yuy2 (15, LE_16 (&data[2]), &c[0]);
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

  return 0;
}

static void msvc_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  msvc_decoder_t *this = (msvc_decoder_t *) this_gen;

  int i;
  palette_entry_t *palette;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette = (palette_entry_t *)buf->decoder_info_ptr[2];
    for (i = 0; i < buf->decoder_info[2]; i++)
      rgb_to_yuy2(
        32,
        (palette[i].r << 16) |
        (palette[i].g <<  8) |
        (palette[i].b <<  0),
        &this->color_table[i]);
  }

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    xine_bmiheader *bih;
    int		    image_size;

    bih = (xine_bmiheader *) buf->content;
    this->video_step = buf->decoder_info[1];

    this->width		= (bih->biWidth + 1) & ~0x1;
    this->height	= bih->biHeight;
    this->coded_width	= (this->width + 3) & ~0x3;
    this->coded_height	= (this->height + 3) & ~0x3;
    this->pitch		= 2*this->coded_width;
    this->depth		= bih->biBitCount;

    if (this->depth != 8 && this->depth != 16) {
      fprintf (stderr, "Unsupported bit depth (%d)\n", this->depth);
      return;
    }

    image_size		= (this->pitch * this->coded_height);
    this->current	= (uint8_t *) realloc (this->current, image_size);

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    this->stream->video_out->open (this->stream->video_out, this->stream);
    this->decoder_ok = 1;

    /* load the stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = strdup("Microsoft Video-1");
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;

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

      vo_frame_t *img;
      int	  result;

      result = msvc_decode_frame (this, this->buf, this->size);

      img = this->stream->video_out->get_frame (this->stream->video_out,
						this->width, this->height,
						XINE_VO_ASPECT_DONT_TOUCH,
						XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts	     = buf->pts;
      img->bad_frame = (result != 0);

      if (this->pitch == img->pitches[0]) {
	xine_fast_memcpy (img->base[0], this->current, img->pitches[0]*this->height);
      } else {
	uint8_t *src, *dst;

	src = (uint8_t *) this->current;
	dst = img->base[0];

	for (i=0; i < this->height; i++) {
	  xine_fast_memcpy (dst, src, 2*this->width);
	  src += this->pitch;
	  dst += img->pitches[0];
	}
      }

      img->draw(img, this->stream);
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

static void msvc_discontinuity (video_decoder_t *this_gen) {
}

static void msvc_dispose (video_decoder_t *this_gen) {

  msvc_decoder_t *this = (msvc_decoder_t *) this_gen;

  if (this->current) {
    free (this->current);
    this->current = NULL;
  }

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

  msvc_decoder_t  *this ;

  this = (msvc_decoder_t *) xine_xmalloc (sizeof (msvc_decoder_t));

  this->video_decoder.decode_data         = msvc_decode_data;
  this->video_decoder.flush               = msvc_flush;
  this->video_decoder.reset               = msvc_reset;
  this->video_decoder.discontinuity       = msvc_discontinuity;
  this->video_decoder.dispose             = msvc_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (msvc_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "MSVC";
}

static char *get_description (video_decoder_class_t *this) {
  return "Microsoft Video-1 video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  msvc_class_t *this;

  this = (msvc_class_t *) xine_xmalloc (sizeof (msvc_class_t));

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
  BUF_VIDEO_MSVC,
  0
 };

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  5                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 14, "msvc", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
