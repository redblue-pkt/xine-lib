/* This is the standard xine header: */
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
 * $Id: cyuv.c,v 1.9 2002/10/20 17:34:11 tmmm Exp $
 */

/* And this is the header that came with the CYUV decoder: */
/* ------------------------------------------------------------------------
 * Creative YUV Video Decoder
 *
 * Dr. Tim Ferguson, 2001.
 * For more details on the algorithm:
 *         http://www.csse.monash.edu.au/~timf/videocodec.html
 *
 * This is a very simple predictive coder.  A video frame is coded in YUV411
 * format.  The first pixel of each scanline is coded using the upper four
 * bits of its absolute value.  Subsequent pixels for the scanline are coded
 * using the difference between the last pixel and the current pixel (DPCM).
 * The DPCM values are coded using a 16 entry table found at the start of the
 * frame.  Thus four bits per component are used and are as follows:
 *     UY VY YY UY VY YY UY VY...
 * This code assumes the frame width will be a multiple of four pixels.  This
 * should probably be fixed.
 *
 * You may freely use this source code.  I only ask that you reference its
 * source in your projects documentation:
 *       Tim Ferguson: http://www.csse.monash.edu.au/~timf/
 * ------------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "xineutils.h"

#define VIDEOBUFSIZE 128*1024

typedef struct {
  video_decoder_class_t   decoder_class;
} cyuv_class_t;

typedef struct cyuv_decoder_s {
  video_decoder_t   video_decoder;

  xine_stream_t    *stream;
  cyuv_class_t     *class;

  int               video_step;
  int               skipframes;
  unsigned char    *buf;
  int               bufsize;
  int               size;
  int               width;
  int               height;
} cyuv_decoder_t;

/* ------------------------------------------------------------------------
 * This function decodes a buffer containing a CYUV encoded frame.
 *
 * buf - the input buffer to be decoded
 * size - the size of the input buffer
 * frame - the output frame buffer (YUY2 format)
 * width - the width of the output frame
 * height - the height of the output frame
 * bit_per_pixel - ignored for now: may be used later for conversions.
 */
void cyuv_decode(unsigned char *buf, int size, unsigned char *frame,
  int width, int height, int bit_per_pixel) {

  int i, xpos, ypos, cur_Y = 0, cur_U = 0, cur_V = 0;
  char *delta_y_tbl, *delta_c_tbl, *ptr;

  delta_y_tbl = buf + 16;
  delta_c_tbl = buf + 32;
  ptr = buf + (16 * 3);

  for(ypos = 0; ypos < height; ypos++) {
    for(xpos = 0; xpos < width; xpos += 4) {
      /* first pixels in scanline */
      if(xpos == 0) {
        cur_U = *(ptr++);
        cur_Y = (cur_U & 0x0f) << 4;
        cur_U = cur_U & 0xf0;
        *frame++ = cur_Y;
        *frame++ = cur_U;

        cur_V = *(ptr++);
        cur_Y = (cur_Y + delta_y_tbl[cur_V & 0x0f]) & 0xff;
        cur_V = cur_V & 0xf0;
        *frame++ = cur_Y;
        *frame++ = cur_V;
      }
      /* subsequent pixels in scanline */
      else {
        i = *(ptr++);
        cur_U = (cur_U + delta_c_tbl[i >> 4]) & 0xff;
        cur_Y = (cur_Y + delta_y_tbl[i & 0x0f]) & 0xff;
        *frame++ = cur_Y;
        *frame++ = cur_U;

        i = *(ptr++);
        cur_V = (cur_V + delta_c_tbl[i >> 4]) & 0xff;
        cur_Y = (cur_Y + delta_y_tbl[i & 0x0f]) & 0xff;
        *frame++ = cur_Y;
        *frame++ = cur_V;
      }

      i = *(ptr++);
      cur_Y = (cur_Y + delta_y_tbl[i & 0x0f]) & 0xff;
      *frame++ = cur_Y;
      *frame++ = cur_U;

      cur_Y = (cur_Y + delta_y_tbl[i >> 4]) & 0xff;
      *frame++ = cur_Y;
      *frame++ = cur_V;
    }
  }
}

static void cyuv_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  cyuv_decoder_t *this = (cyuv_decoder_t *) this_gen;
  vo_frame_t *img; /* video out frame */

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) { /* need to initialize */
    this->stream->video_out->open (this->stream->video_out);

    if(this->buf)
      free(this->buf);

    this->buf = malloc(VIDEOBUFSIZE);
    this->bufsize = VIDEOBUFSIZE;
    this->size = 0;
    this->width = *(unsigned int *)&buf->content[4];
    this->height = *(unsigned int *)&buf->content[8];
    this->skipframes = 0;
    this->video_step = buf->decoder_info[1];

    return;
  }

  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
    printf("CYUV: increasing source buffer to %d to avoid overflow.\n",
      this->bufsize);
    this->buf = realloc( this->buf, this->bufsize );
  }

  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
    this->video_step = buf->decoder_info[0];

  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  { /* time to decode a frame */
    img = this->stream->video_out->get_frame (this->stream->video_out, 
      this->width, this->height, XINE_VO_ASPECT_SQUARE, XINE_IMGFMT_YUY2,
      VO_BOTH_FIELDS);

    img->pts = buf->pts;
    img->duration = this->video_step;

    cyuv_decode(this->buf, this->size, img->base[0],
      this->width, this->height, 0);

    if (img->copy) {
      int height = img->height;
      uint8_t *src[3];

      src[0] = img->base[0];

      while ((height -= 16) >= 0) {
	img->copy(img, src);
	src[0] += 16 * img->pitches[0];
      }
    }

    this->skipframes = img->draw(img);
    if( this->skipframes < 0 )
      this->skipframes = 0;
    img->free(img);

    this->size = 0;
  }
}

static void cyuv_flush (video_decoder_t *this_gen) {
}

static void cyuv_reset (video_decoder_t *this_gen) {
}

static void cyuv_dispose (video_decoder_t *this_gen) {

  cyuv_decoder_t *this = (cyuv_decoder_t *) this_gen;

  this->stream->video_out->close(this->stream->video_out);

  free (this_gen);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  cyuv_decoder_t  *this ;

  this = (cyuv_decoder_t *) malloc (sizeof (cyuv_decoder_t));

  this->video_decoder.decode_data         = cyuv_decode_data;
  this->video_decoder.flush               = cyuv_flush;
  this->video_decoder.reset               = cyuv_reset;
  this->video_decoder.dispose             = cyuv_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (cyuv_class_t *) class_gen;

  this->buf           = NULL;

  return &this->video_decoder;
}

static char *get_identifier (video_decoder_class_t *this) {
  return "CYUV";
}

static char *get_description (video_decoder_class_t *this) {
  return "Creative YUV (CYUV) video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  cyuv_class_t *this;

  this = (cyuv_class_t *) malloc (sizeof (cyuv_class_t));

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
  BUF_VIDEO_CYUV,
  0
 };

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 11, "cyuv", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
