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
 * $Id: cinepak.c,v 1.10 2002/07/15 21:42:34 esnel Exp $
 */

#include <stdlib.h>
#include <unistd.h>

#include "video_out.h"
#include "buffer.h"
#include "bswap.h"
#include "xineutils.h"
#include "xine_internal.h"

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

typedef struct cvid_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t	   *video_out;
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

      codebook[i].y0 = *data++;
      codebook[i].y1 = *data++;
      codebook[i].y2 = *data++;
      codebook[i].y3 = *data++;
      codebook[i].u  = 128 + ((n == 4) ? 0 : *data++);
      codebook[i].v  = 128 + ((n == 4) ? 0 : *data++);
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

static int cvid_can_handle (video_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_VIDEO_CINEPAK);
}

static void cvid_init (video_decoder_t *this_gen, vo_instance_t *video_out) {
  cvid_decoder_t *this = (cvid_decoder_t *) this_gen;

  this->video_out  = video_out;
  this->decoder_ok = 0;
}

static void cvid_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  cvid_decoder_t *this = (cvid_decoder_t *) this_gen;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    xine_bmiheader *bih;

    bih = (xine_bmiheader *) buf->content;
    this->biWidth = (bih->biWidth + 3) & ~0x03;
    this->biHeight = (bih->biHeight + 3) & ~0x03;
    this->video_step = buf->decoder_info[1];

    if (this->img_buffer)
      free (this->img_buffer);
    this->img_buffer = malloc((this->biWidth * this->biHeight * 3) >> 1);

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

      cinepak_decode_frame (this, this->buf, this->size);

      img = this->video_out->get_frame (this->video_out,
					this->biWidth, this->biHeight,
					42, IMGFMT_YV12, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts	     = buf->pts;
      img->bad_frame = 0;

      /* FIXME: use img->pitches[3] */
      xine_fast_memcpy (img->base[0], this->img_buffer, n);
      xine_fast_memcpy (img->base[1], this->img_buffer + n, (n >> 2));
      xine_fast_memcpy (img->base[2], this->img_buffer + n + (n >> 2), (n >> 2));

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

      img->draw(img);
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

static void cvid_close (video_decoder_t *this_gen) {
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
    this->video_out->close(this->video_out);
  }
}

static char *cvid_get_id(void) {
  return "cinepak";
}

static void cvid_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

video_decoder_t *init_video_decoder_plugin (int iface_version, xine_t *xine) {

  cvid_decoder_t *this ;

  if (iface_version != 10) {
    printf(_("cinepak: plugin doesn't support plugin API version %d.\n"
	     "cinepak: this means there's a version mismatch between xine and this "
	     "cinepak: decoder plugin.\nInstalling current plugins should help.\n"),
	   iface_version);
    return NULL;
  }

  this = (cvid_decoder_t *) malloc (sizeof (cvid_decoder_t));
  memset(this, 0, sizeof (cvid_decoder_t));

  this->video_decoder.interface_version   = iface_version;
  this->video_decoder.can_handle          = cvid_can_handle;
  this->video_decoder.init                = cvid_init;
  this->video_decoder.decode_data         = cvid_decode_data;
  this->video_decoder.flush               = cvid_flush;
  this->video_decoder.reset               = cvid_reset;
  this->video_decoder.close               = cvid_close;
  this->video_decoder.get_identifier      = cvid_get_id;
  this->video_decoder.dispose             = cvid_dispose;
  this->video_decoder.priority            = 5;

  return (video_decoder_t *) this;
}
