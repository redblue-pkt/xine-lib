/* 
 * Copyright (C) 2002 the xine project
 * 
 * This file is part of xine, a unix video player.
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
 * based on overview of cinepak algorithm and example decoder
 * by Tim Ferguson: http://www.csse.monash.edu.au/~timf/
 */

#include <stdlib.h>
#include <unistd.h>

#include "video_out.h"
#include "buffer.h"
#include "xineutils.h"
#include "xine_internal.h"

#define MAX_STRIPS	32
#define VIDEOBUFSIZE	64*1024

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
  uint8_t	    flags;
  uint16_t	    width;
  uint16_t	    height;
  uint16_t	    num_strips;
  cvid_strip_t	    strips[MAX_STRIPS];
  uint8_t	   *buffer;
} cvid_frame_t;

typedef struct cvid_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t	   *video_out;
  int64_t           video_step;
  int		    decoder_ok;

  unsigned char	   *buf;
  int		    bufsize;
  int		    size;

  cvid_frame_t	    frame;
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

static void cinepak_decode_vectors (cvid_frame_t *frame, cvid_strip_t *strip,
				    int chunk_id, int size, uint8_t *data)
{
  uint8_t	  *eod = (data + size);
  uint32_t	   flag, mask;
  cvid_codebook_t *codebook;
  int		   x, y;
  uint8_t	  *uvbase;
  uint8_t	  *iy[4];
  uint8_t	  *iu[2];
  uint8_t	  *iv[2];

  uvbase = frame->buffer + frame->width*frame->height;
  flag = 0;
  mask = 0;

  for (y=strip->y1; y < strip->y2; y+=4) {

    iy[0] = frame->buffer + (y*frame->width) + strip->x1;
    iy[1] = iy[0] + frame->width;
    iy[2] = iy[1] + frame->width;
    iy[3] = iy[2] + frame->width;
    iu[0] = uvbase + (y >> 1)*(frame->width >> 1) + (strip->x1 >> 1);
    iu[1] = iu[0] + (frame->width >> 1);
    iv[0] = iu[0] + (frame->width >> 1)*(frame->height >> 1);
    iv[1] = iv[0] + (frame->width >> 1);

    for (x=strip->x1; x < strip->x2; x+=4) {
      if ((chunk_id & 0x0100) && !(mask >>= 1)) {
	if ((data + 4) > eod)
	  break;

	flag  = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	data += 4;
	mask  = 0x80000000;
      }

      if (!(chunk_id & 0x0100) || (flag & mask)) {
	if (!(chunk_id & 0x0200) && !(mask >>= 1)) {
	  if ((data + 4) > eod)
	    break;

	  flag  = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	  data += 4;
	  mask  = 0x80000000;
	}

	if ((chunk_id & 0x0200) || (~flag & mask)) {
	  if (data >= eod)
	    break;

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
	    break;

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

static void cinepak_decode_strip (cvid_frame_t *frame, cvid_strip_t *strip,
				  cvid_strip_t *prev, uint8_t *data, int size)
{
  uint8_t *eod = (data + size);
  int	   chunk_id, chunk_size;

  /* workaround */
  strip->x1 = 0;
  strip->y1 = 0;
  strip->x2 = frame->width;

  if (prev) {
    strip->y1 += prev->y2;
    strip->y2 += prev->y2;
  }

  /* avoid segmentation fault */
  if (strip->x1 >= frame->width  || strip->x2 > frame->width  ||
      strip->y1 >= frame->height || strip->y2 > frame->height ||
      strip->x1 >= strip->x2     || strip->y1 >= strip->y2)
    return;

  if (prev && !(frame->flags & 0x01)) {
    xine_fast_memcpy (strip->v4_codebook, prev->v4_codebook,
		      sizeof(strip->v4_codebook));

    xine_fast_memcpy (strip->v1_codebook, prev->v1_codebook,
		      sizeof(strip->v1_codebook));
  }

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
      cinepak_decode_vectors (frame, strip, chunk_id, chunk_size, data);
      return;
    }

    data += chunk_size;
  }
}

static void cinepak_decode_frame (cvid_frame_t *frame, uint8_t *data, int size)
{
  cvid_strip_t *prev = 0;
  uint8_t      *eod = (data + size);
  int		i, strip_size;
  int		img_width, img_height;

  if (size < 10)
    return;

  frame->flags	    =  data[0];
  frame->num_strips = (data[8] << 8) | data[9];

  img_width  = (((data[4] << 8) | data[5]) + 3) & ~0x03;
  img_height = (((data[6] << 8) | data[7]) + 3) & ~0x03;
  data	    += 10;

  if (!img_width || !img_height)
    return;

  if (img_width != frame->width || img_height != frame->height) {
    if (frame->buffer)
      free (frame->buffer);
    frame->width  = img_width;
    frame->height = img_height;
    frame->buffer = malloc ((frame->width * frame->height * 3) >> 1);
  }

  if (frame->num_strips > MAX_STRIPS) {
    frame->num_strips = MAX_STRIPS;
  }

  for (i=0; i < frame->num_strips; i++) {
    if ((data + 12) > eod)
      break;

    frame->strips[i].id = (data[0] << 8) | data[1];
    frame->strips[i].y1 = (data[4] << 8) | data[5];
    frame->strips[i].x1 = (data[6] << 8) | data[7];
    frame->strips[i].y2 = (data[8] << 8) | data[9];
    frame->strips[i].x2 = (data[10]<< 8) | data[11];

    strip_size = (data[2] << 8) + data[3] - 12;
    data      += 12;
    strip_size = ((data + strip_size) > eod) ? (eod - data) : strip_size;

    cinepak_decode_strip( frame, &frame->strips[i], prev, data, strip_size );

    data += strip_size;
    prev = &frame->strips[i];
  }
}

static void cinepak_store_frame (cvid_frame_t *frame, vo_frame_t *img)
{
  uint8_t *src;
  uint8_t *dst;
  int      i, j;

  src = frame->buffer;
  dst = img->base[0];

  for (i=0; i < frame->height; i++) {
    xine_fast_memcpy (dst, src, frame->width);
    dst += img->width;
    src += frame->width;
  }

  for (i=0; i < 2; i++) {
    src = frame->buffer + (4+i)*(frame->height >> 1)*(frame->width >> 1);
    dst = img->base[i+1];

    for (j=0; j < (frame->height >> 1); j++) {
      xine_fast_memcpy (dst, src, (frame->width >> 1));
      dst += (img->width >> 1);
      src += (frame->width >> 1);
    }
  }
}

static void cinepak_reset (cvid_frame_t *frame)
{
  frame->width  = 0;
  frame->height = 0;

  if (frame->buffer) {
    free (frame->buffer);
    frame->buffer = NULL;
  }
}

static int cvid_can_handle (video_decoder_t *this_gen, int buf_type)
{
  return ((buf_type & 0xFFFF0000) == BUF_VIDEO_CINEPAK);
}

static void cvid_init (video_decoder_t *this_gen, vo_instance_t *video_out)
{
  cvid_decoder_t *this = (cvid_decoder_t *) this_gen;

  this->video_out  = video_out;
  this->decoder_ok = 0;
}

static void cvid_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
  cvid_decoder_t *this = (cvid_decoder_t *) this_gen;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    if (buf->type & 0xff)
      return;

    cinepak_reset (&this->frame);

    this->size = 0;

    if ( this->buf )
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);

    this->video_out->open (this->video_out);
    this->video_step = buf->decoder_info[1];
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

      cinepak_decode_frame (&this->frame, this->buf, this->size);

      img = this->video_out->get_frame (this->video_out,
					this->frame.width, this->frame.height,
					42, IMGFMT_YV12, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts	     = buf->pts;
      img->bad_frame = 0;

      cinepak_store_frame (&this->frame, img);

      img->draw(img);
      img->free(img);

      this->size = 0;
    }
  }
}

static void cvid_flush (video_decoder_t *this_gen)
{
}

static void cvid_reset (video_decoder_t *this_gen)
{
  cvid_decoder_t *this = (cvid_decoder_t *) this_gen;

  cinepak_reset (&this->frame);

  this->size = 0;
}

static void cvid_close (video_decoder_t *this_gen)
{
  cvid_decoder_t *this = (cvid_decoder_t *) this_gen;

  cinepak_reset (&this->frame);

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->decoder_ok) {  
    this->decoder_ok = 0;
    this->video_out->close(this->video_out);
  }
}

static char *cvid_get_id(void)
{
  return "cinepak";
}

static void cvid_dispose (video_decoder_t *this_gen)
{
  free (this_gen);
}

video_decoder_t *init_video_decoder_plugin (int iface_version, xine_t *xine)
{
  cvid_decoder_t *this ;

  if (iface_version != 7) {
    printf( "cinepak: plugin doesn't support plugin API version %d.\n"
	    "cinepak: this means there's a version mismatch between xine and this "
	    "cinepak: decoder plugin.\nInstalling current plugins should help.\n",
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
