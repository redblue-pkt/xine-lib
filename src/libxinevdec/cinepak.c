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
 * based on overview of Cinepak algorithm and example decoder
 * by Tim Ferguson: http://www.csse.monash.edu.au/~timf/
 *
 * $Id: cinepak.c,v 1.21 2002/11/20 11:57:46 mroi Exp $
 */

#include <stdlib.h>
#include <unistd.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "bswap.h"
#include "xineutils.h"

#define MAX_STRIPS	32
#define VIDEOBUFSIZE	128 * 1024


typedef struct {
  uint8_t  y0, y1, y2, y3;
  uint8_t  u, v;
} cvid_codebook_t;

typedef struct {
  uint16_t	    id;
  uint16_t	    x1, y1;
  uint16_t	    x2, y2;
  cvid_codebook_t   v4_codebook[256];
  cvid_codebook_t   v1_codebook[256];
} cvid_strip_t;

typedef struct {
  video_decoder_class_t   decoder_class;
} cvid_class_t;

typedef struct cvid_decoder_s {
  video_decoder_t   video_decoder;

  cvid_class_t     *class;

  xine_stream_t    *stream;
  int64_t           video_step;
  int		    decoder_ok;

  unsigned char	   *buf;
  int		    bufsize;
  int		    size;

  long		    biWidth;
  long		    biHeight;
  uint8_t          *img_buffer;

  cvid_strip_t	    strips[MAX_STRIPS];
} cvid_decoder_t;

static unsigned char     yuv_palette[256 * 4];
static int color_depth;

static void cinepak_decode_codebook (cvid_codebook_t *codebook,
				     int chunk_id, int size, uint8_t *data)
{
  uint8_t *eod = (data + size);
  uint32_t flag, mask;
  int	   i, n;

  n    = (chunk_id & 0x0400) ? 4 : 6;
  flag = 0;
  mask = 0;

  for (i=0; i < 256; i++) {
    if ((chunk_id & 0x0100) && !(mask >>= 1)) {
      if ((data + 4) > eod)
        break;

      flag  = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
      data += 4;
      mask  = 0x80000000;
    }

    if (!(chunk_id & 0x0100) || (flag & mask)) {
      if ((data + n) > eod)
        break;

      if (n == 6) {
        codebook[i].y0 = *data++;
        codebook[i].y1 = *data++;
        codebook[i].y2 = *data++;
        codebook[i].y3 = *data++;
        codebook[i].u  = 128 + *data++;
        codebook[i].v  = 128 + *data++;
      } else if (color_depth == 8) {
        /* if color depth is 8, this codebook type indicates palette lookup */
        codebook[i].y0 = yuv_palette[*(data + 0) * 4];
        codebook[i].y1 = yuv_palette[*(data + 1) * 4];
        codebook[i].y2 = yuv_palette[*(data + 2) * 4];
        codebook[i].y3 = yuv_palette[*(data + 3) * 4];
        codebook[i].u =
          (yuv_palette[*(data + 0) * 4 + 1] +
           yuv_palette[*(data + 1) * 4 + 1] +
           yuv_palette[*(data + 2) * 4 + 1] +
           yuv_palette[*(data + 3) * 4 + 1]) / 4;
        codebook[i].v =
          (yuv_palette[*(data + 0) * 4 + 2] +
           yuv_palette[*(data + 1) * 4 + 2] +
           yuv_palette[*(data + 2) * 4 + 2] +
           yuv_palette[*(data + 3) * 4 + 2]) / 4;
        data += 4;
      } else {
        /* if color depth is 40, this codebook type indicates greyscale */
        codebook[i].y0 = *data++;
        codebook[i].y1 = *data++;
        codebook[i].y2 = *data++;
        codebook[i].y3 = *data++;
        codebook[i].u  = 128;
        codebook[i].v  = 128;
      }
    }
  }
}

static void cinepak_decode_vectors (cvid_decoder_t *this, cvid_strip_t *strip,
				    int chunk_id, int size, uint8_t *data)
{
  uint8_t	  *eod = (data + size);
  uint32_t	   flag, mask;
  cvid_codebook_t *codebook;
  int		   x, y;
  int              n = (this->biWidth * this->biHeight);
  uint8_t	  *iy[4];
  uint8_t	  *iu[2];
  uint8_t	  *iv[2];

  flag = 0;
  mask = 0;

  for (y=strip->y1; y < strip->y2; y+=4) {

    iy[0] = this->img_buffer + (y * this->biWidth) + strip->x1;
    iy[1] = iy[0] + this->biWidth;
    iy[2] = iy[1] + this->biWidth;
    iy[3] = iy[2] + this->biWidth;
    iu[0] = this->img_buffer + n + ((y * this->biWidth) >> 2) + (strip->x1 >> 1);
    iu[1] = iu[0] + (this->biWidth >> 1);
    iv[0] = iu[0] + (n >> 2);
    iv[1] = iv[0] + (this->biWidth >> 1);

    for (x=strip->x1; x < strip->x2; x+=4) {
      if ((chunk_id & 0x0100) && !(mask >>= 1)) {
	if ((data + 4) > eod)
	  return;

	flag  = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	data += 4;
	mask  = 0x80000000;
      }

      if (!(chunk_id & 0x0100) || (flag & mask)) {
	if (!(chunk_id & 0x0200) && !(mask >>= 1)) {
	  if ((data + 4) > eod)
	    return;

	  flag  = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	  data += 4;
	  mask  = 0x80000000;
	}

	if ((chunk_id & 0x0200) || (~flag & mask)) {
	  if (data >= eod)
	    return;

	  codebook = &strip->v1_codebook[*data++];
	  iy[0][0] = codebook->y0;  iy[0][1] = codebook->y0;
	  iy[1][0] = codebook->y0;  iy[1][1] = codebook->y0;
	  iu[0][0] = codebook->u;   iv[0][0] = codebook->v;

	  iy[0][2] = codebook->y1;  iy[0][3] = codebook->y1;
	  iy[1][2] = codebook->y1;  iy[1][3] = codebook->y1;
	  iu[0][1] = codebook->u;   iv[0][1] = codebook->v;

	  iy[2][0] = codebook->y2;  iy[2][1] = codebook->y2;
	  iy[3][0] = codebook->y2;  iy[3][1] = codebook->y2;
	  iu[1][0] = codebook->u;   iv[1][0] = codebook->v;

	  iy[2][2] = codebook->y3;  iy[2][3] = codebook->y3;
	  iy[3][2] = codebook->y3;  iy[3][3] = codebook->y3;
	  iu[1][1] = codebook->u;   iv[1][1] = codebook->v;

	} else if (flag & mask) {
	  if ((data + 4) > eod)
	    return;

	  codebook = &strip->v4_codebook[*data++];
	  iy[0][0] = codebook->y0;  iy[0][1] = codebook->y1;
	  iy[1][0] = codebook->y2;  iy[1][1] = codebook->y3;
	  iu[0][0] = codebook->u;   iv[0][0] = codebook->v;

	  codebook = &strip->v4_codebook[*data++];
	  iy[0][2] = codebook->y0;  iy[0][3] = codebook->y1;
	  iy[1][2] = codebook->y2;  iy[1][3] = codebook->y3;
	  iu[0][1] = codebook->u;   iv[0][1] = codebook->v;

	  codebook = &strip->v4_codebook[*data++];
	  iy[2][0] = codebook->y0;  iy[2][1] = codebook->y1;
	  iy[3][0] = codebook->y2;  iy[3][1] = codebook->y3;
	  iu[1][0] = codebook->u;   iv[1][0] = codebook->v;

	  codebook = &strip->v4_codebook[*data++];
	  iy[2][2] = codebook->y0;  iy[2][3] = codebook->y1;
	  iy[3][2] = codebook->y2;  iy[3][3] = codebook->y3;
	  iu[1][1] = codebook->u;   iv[1][1] = codebook->v;
	}
      }

      iy[0] += 4;  iy[1] += 4;
      iy[2] += 4;  iy[3] += 4;
      iu[0] += 2;  iu[1] += 2;
      iv[0] += 2;  iv[1] += 2;
    }
  }
}

static void cinepak_decode_strip (cvid_decoder_t *this,
				  cvid_strip_t *strip, uint8_t *data, int size)
{
  uint8_t *eod = (data + size);
  int	   chunk_id, chunk_size;

  if (strip->x1 >= this->biWidth  || strip->x2 > this->biWidth  ||
      strip->y1 >= this->biHeight || strip->y2 > this->biHeight ||
      strip->x1 >= strip->x2      || strip->y1 >= strip->y2)
    return;

  while ((data + 4) <= eod) {
    chunk_id   =  (data[0] << 8) | data[1];
    chunk_size = ((data[2] << 8) | data[3]) - 4;
    data      += 4;
    chunk_size = ((data + chunk_size) > eod) ? (eod - data) : chunk_size;

    switch (chunk_id) {
    case 0x2000:
    case 0x2100:
    case 0x2400:
    case 0x2500:
      cinepak_decode_codebook (strip->v4_codebook, chunk_id, chunk_size, data);
      break;
    case 0x2200:
    case 0x2300:
    case 0x2600:
    case 0x2700:
      cinepak_decode_codebook (strip->v1_codebook, chunk_id, chunk_size, data);
      break;
    case 0x3000:
    case 0x3100:
    case 0x3200:
      cinepak_decode_vectors (this, strip, chunk_id, chunk_size, data);
      return;
    }

    data += chunk_size;
  }
}

static void cinepak_decode_frame (cvid_decoder_t *this, uint8_t *data, int size) {
  uint8_t      *eod = (data + size);
  int		i, strip_size, frame_flags, num_strips;
  int		y0 = 0;

  if (size < 10)
    return;

  frame_flags =  data[0];
  num_strips  = (data[8] << 8) | data[9];
  data	     += 10;

  if (num_strips > MAX_STRIPS)
    num_strips = MAX_STRIPS;

  for (i=0; i < num_strips; i++) {
    if ((data + 12) > eod)
      break;

    this->strips[i].id = (data[0] << 8) | data[1];
    this->strips[i].y1 = y0;
    this->strips[i].x1 = 0;
    this->strips[i].y2 = y0 + ((data[8] << 8) | data[9]);
    this->strips[i].x2 = this->biWidth;

    strip_size = (data[2] << 8) + data[3] - 12;
    data      += 12;
    strip_size = ((data + strip_size) > eod) ? (eod - data) : strip_size;

    if ((i > 0) && !(frame_flags & 0x01)) {
      xine_fast_memcpy (this->strips[i].v4_codebook, this->strips[i-1].v4_codebook,
			sizeof(this->strips[i].v4_codebook));

      xine_fast_memcpy (this->strips[i].v1_codebook, this->strips[i-1].v1_codebook,
			sizeof(this->strips[i].v1_codebook));
    }

    cinepak_decode_strip (this, &this->strips[i], data, strip_size);

    data += strip_size;
    y0    = this->strips[i].y2;
  }
}

static void cvid_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  cvid_decoder_t *this = (cvid_decoder_t *) this_gen;

  palette_entry_t *palette;
  int i;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  /* convert the RGB palette to a YUV palette */
  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette = (palette_entry_t *)buf->decoder_info[3];
    for (i = 0; i < buf->decoder_info[2]; i++) {
      yuv_palette[i * 4 + 0] =
        COMPUTE_Y(palette[i].r, palette[i].g, palette[i].b);
      yuv_palette[i * 4 + 1] =
        COMPUTE_U(palette[i].r, palette[i].g, palette[i].b);
      yuv_palette[i * 4 + 2] =
        COMPUTE_V(palette[i].r, palette[i].g, palette[i].b);
    }
  }

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    xine_bmiheader *bih;

    bih = (xine_bmiheader *) buf->content;
    this->biWidth = (bih->biWidth + 3) & ~0x03;
    this->biHeight = (bih->biHeight + 3) & ~0x03;
    this->video_step = buf->decoder_info[1];

    if (this->img_buffer)
      free (this->img_buffer);
    this->img_buffer = malloc((this->biWidth * this->biHeight * 3) / 2);

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    color_depth = bih->biBitCount;

    this->stream->video_out->open (this->stream->video_out, this->stream);
    this->decoder_ok = 1;

    /* stream/meta info */
    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC] = strdup("Cinepak");
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;

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
      uint8_t    *dy, *du, *dv, *sy, *su, *sv;
      int	  y;

      cinepak_decode_frame (this, this->buf, this->size);

      img = this->stream->video_out->get_frame (this->stream->video_out,
					this->biWidth, this->biHeight,
					XINE_VO_ASPECT_SQUARE,
					XINE_IMGFMT_YV12, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts	     = buf->pts;
      img->bad_frame = 0;

      dy = img->base[0];
      du = img->base[1];
      dv = img->base[2];
      sy = this->img_buffer;
      su = this->img_buffer + (this->biWidth * this->biHeight);
      sv = this->img_buffer + ((this->biWidth * this->biHeight * 5) / 4);

      for (y=0; y < this->biHeight; y++) {
	xine_fast_memcpy (dy, sy, this->biWidth);
	  
	dy += img->pitches[0];  
	sy += this->biWidth;
      }

      for (y=0; y < (this->biHeight/2); y++) {
	xine_fast_memcpy (du, su, this->biWidth/2);
	xine_fast_memcpy (dv, sv, this->biWidth/2);

	du += img->pitches[1];
	dv += img->pitches[2];
	su += this->biWidth/2;
	sv += this->biWidth/2;
      }
	
      if (img->copy) {
	int height = img->height;
	uint8_t *src[3];

	src[0] = img->base[0];
	src[1] = img->base[1];
	src[2] = img->base[2];

	while ((height -= 16) >= 0) {
	  img->copy(img, src);
	  src[0] += 16 * img->pitches[0];
	  src[1] +=  8 * img->pitches[1];
	  src[2] +=  8 * img->pitches[2];
	}
      }      

      img->draw(img, this->stream);
      img->free(img);

      this->size = 0;
    }
  }
}

static void cvid_flush (video_decoder_t *this_gen) {
}

static void cvid_reset (video_decoder_t *this_gen) {
  cvid_decoder_t *this = (cvid_decoder_t *) this_gen;

  this->size = 0;
}

static void cvid_discontinuity (video_decoder_t *this_gen) {
}

static void cvid_dispose (video_decoder_t *this_gen) {
  cvid_decoder_t *this = (cvid_decoder_t *) this_gen;

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
    this->stream->video_out->close(this->stream->video_out, this->stream);
  }

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  cvid_decoder_t  *this ;

  this = (cvid_decoder_t *) malloc (sizeof (cvid_decoder_t));

  this->video_decoder.decode_data         = cvid_decode_data;
  this->video_decoder.flush               = cvid_flush;
  this->video_decoder.reset               = cvid_reset;
  this->video_decoder.discontinuity       = cvid_discontinuity;
  this->video_decoder.dispose             = cvid_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (cvid_class_t *) class_gen;

  this->decoder_ok    = 0;
  this->buf           = NULL;
  this->img_buffer    = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "Cinepak";
}

static char *get_description (video_decoder_class_t *this) {
  return "Cinepak (CVID) video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  cvid_class_t *this;

  this = (cvid_class_t *) malloc (sizeof (cvid_class_t));

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
  BUF_VIDEO_CINEPAK,
  0
 };

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  5                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 13, "cinepak", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
