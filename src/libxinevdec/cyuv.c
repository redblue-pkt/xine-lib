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
 * $Id: cyuv.c,v 1.2 2002/05/01 19:42:57 guenter Exp $
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

#include "video_out.h"
#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"

#define VIDEOBUFSIZE 128*1024

typedef struct cyuv_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t    *video_out;
  int               video_step;
  int               skipframes;
  unsigned char    *buf;
  int               bufsize;
  int               size;
  int               width;
  int               height;
} cyuv_decoder_t;

static int cyuv_can_handle (video_decoder_t *this_gen, int buf_type) {
  return (buf_type == BUF_VIDEO_CYUV);
}

static void cyuv_init (video_decoder_t *this_gen, vo_instance_t *video_out) {
  cyuv_decoder_t *this = (cyuv_decoder_t *) this_gen;

  this->video_out = video_out;
  this->buf = NULL;
}

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
    this->video_out->open (this->video_out);

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
    img = this->video_out->get_frame (this->video_out, this->width,
      this->height, XINE_ASPECT_RATIO_SQUARE, IMGFMT_YUY2,
      VO_BOTH_FIELDS);

    img->pts = buf->pts;
    img->duration = this->video_step;

    cyuv_decode(this->buf, this->size, img->base[0],
      this->width, this->height, 0);

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

static void cyuv_close (video_decoder_t *this_gen) {

  cyuv_decoder_t *this = (cyuv_decoder_t *) this_gen;

  this->video_out->close(this->video_out);
}

static char *cyuv_get_id(void) {
  return "CYUV";
}

static void cyuv_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

video_decoder_t *init_video_decoder_plugin (int iface_version, xine_t *xine) {

  cyuv_decoder_t *this ;

  if (iface_version != 8) {
    printf( "CYUV: plugin doesn't support plugin API version %d.\n"
      "CYUV: this means there's a version mismatch between xine and this "
      "CYUV: decoder plugin.\nInstalling current plugins should help.\n",
      iface_version);
    return NULL;
  }

  this = (cyuv_decoder_t *) malloc (sizeof (cyuv_decoder_t));

  this->video_decoder.interface_version   = iface_version;
  this->video_decoder.can_handle          = cyuv_can_handle;
  this->video_decoder.init                = cyuv_init;
  this->video_decoder.decode_data         = cyuv_decode_data;
  this->video_decoder.flush               = cyuv_flush;
  this->video_decoder.reset               = cyuv_reset;
  this->video_decoder.close               = cyuv_close;
  this->video_decoder.get_identifier      = cyuv_get_id;
  this->video_decoder.dispose             = cyuv_dispose;
  this->video_decoder.priority            = 1;

  return (video_decoder_t *) this;
}
