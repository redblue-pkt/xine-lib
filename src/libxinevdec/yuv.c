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
 * YUV "Decoder" by Mike Melanson (melanson@pcisys.net)
 * Actually, this decoder just reorganizes chunks of raw YUV data in such
 * a way that xine can display them.
 * 
 * $Id: yuv.c,v 1.6 2002/09/05 22:19:03 mroi Exp $
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

typedef struct yuv_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  /* these are traditional variables in a video decoder object */
  vo_instance_t    *video_out;   /* object that will receive frames */
  uint64_t          video_step;  /* frame duration in pts units */
  int               decoder_ok;  /* current decoder status */
  int               skipframes;

  unsigned char    *buf;         /* the accumulated buffer data */
  int               bufsize;     /* the maximum size of buf */
  int               size;        /* the current size of buf */

  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */

} yuv_decoder_t;

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function is responsible is called to initialize the video decoder
 * for use. Initialization usually involves setting up the fields in your
 * private video decoder object.
 */
static void yuv_init (video_decoder_t *this_gen, 
  vo_instance_t *video_out) {
  yuv_decoder_t *this = (yuv_decoder_t *) this_gen;

  /* set our own video_out object to the one that xine gives us */
  this->video_out  = video_out;

  /* indicate that the decoder is not quite ready yet */
  this->decoder_ok = 0;
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void yuv_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  yuv_decoder_t *this = (yuv_decoder_t *) this_gen;
  xine_bmiheader *bih;

  vo_frame_t *img; /* video out frame */

  int c_plane_stride;
  int c_plane_x_ptr, c_plane_y_ptr;
  int raw_plane_ptr;
  unsigned char *raw_u_plane;
  unsigned char *raw_v_plane;

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) { /* need to initialize */
    this->video_out->open (this->video_out);

    if(this->buf)
      free(this->buf);

    bih = (xine_bmiheader *) buf->content;
    this->width = (bih->biWidth + 3) & ~0x03;
    this->height = (bih->biHeight + 3) & ~0x03;
    this->video_step = buf->decoder_info[1];

    if (this->buf)
      free (this->buf);
    this->bufsize = VIDEOBUFSIZE;
    this->buf = malloc(this->bufsize);
    this->size = 0;

    this->video_out->open (this->video_out);
    this->decoder_ok = 1;

    return;
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

      if (buf->type == BUF_VIDEO_YVU9) {

        img = this->video_out->get_frame (this->video_out,
                                          this->width, this->height,
                                          XINE_VO_ASPECT_DONT_TOUCH, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);

        xine_fast_memcpy(img->base[0], this->buf, this->width * this->height);

        raw_u_plane = &this->buf[this->width * this->height];
        raw_v_plane = &this->buf[(this->width * this->height) +
          (this->width * this->height) / 16];
        c_plane_stride = this->width / 2;
        c_plane_y_ptr = c_plane_x_ptr = 0;
        raw_plane_ptr = 0;

        while (raw_plane_ptr++ < (this->width * this->height / 16)) {

          img->base[2][c_plane_y_ptr + c_plane_x_ptr + 0] = 
            raw_u_plane[raw_plane_ptr];
          img->base[2][c_plane_y_ptr + c_plane_x_ptr + 1] = 
            raw_u_plane[raw_plane_ptr];

          img->base[2][c_plane_y_ptr + c_plane_stride + c_plane_x_ptr + 0] = 
            raw_u_plane[raw_plane_ptr];
          img->base[2][c_plane_y_ptr + c_plane_stride + c_plane_x_ptr + 1] = 
            raw_u_plane[raw_plane_ptr];

          img->base[1][c_plane_y_ptr + c_plane_x_ptr + 0] = 
            raw_v_plane[raw_plane_ptr];
          img->base[1][c_plane_y_ptr + c_plane_x_ptr + 1] = 
            raw_v_plane[raw_plane_ptr];

          img->base[1][c_plane_y_ptr + c_plane_stride + c_plane_x_ptr + 0] = 
            raw_v_plane[raw_plane_ptr];
          img->base[1][c_plane_y_ptr + c_plane_stride + c_plane_x_ptr + 1] = 
            raw_v_plane[raw_plane_ptr];

          c_plane_x_ptr += 2;
          if (c_plane_x_ptr >= this->width / 2) {
            c_plane_x_ptr = 0;
            c_plane_y_ptr += c_plane_stride * 2;
          }
        }
      } else if (buf->type == BUF_VIDEO_GREY) {

        img = this->video_out->get_frame (this->video_out,
                                          this->width, this->height,
                                          42, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);

        xine_fast_memcpy(img->base[0], this->buf, this->width * this->height);
        memset( img->base[1], 0x80, this->width * this->height / 4 );
        memset( img->base[2], 0x80, this->width * this->height / 4 );

      } else {

        /* just allocate something to avoid compiler warnings */
        img = this->video_out->get_frame (this->video_out,
                                          this->width, this->height,
                                          XINE_VO_ASPECT_DONT_TOUCH, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);

      }

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

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

/*
 * This function is called when xine needs to flush the system. Not
 * sure when or if this is used or even if it needs to do anything.
 */
static void yuv_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void yuv_reset (video_decoder_t *this_gen) {
  yuv_decoder_t *this = (yuv_decoder_t *) this_gen;

  this->size = 0;
}

/*
 * This function is called when xine shuts down the decoder. It should
 * free any memory and release any other resources allocated during the
 * execution of the decoder.
 */
static void yuv_close (video_decoder_t *this_gen) {
  yuv_decoder_t *this = (yuv_decoder_t *) this_gen;

  if (this->buf) {
    free (this->buf);
    this->buf = NULL;
  }

  if (this->decoder_ok) {
    this->decoder_ok = 0;
    this->video_out->close(this->video_out);
  }
}

/*
 * This function returns the human-readable ID string to identify 
 * this decoder.
 */
static char *yuv_get_id(void) {
  return "Raw YUV";
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void yuv_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

/*
 * This function should be the plugin's only advertised function to the
 * outside world. It allows xine to query the plugin module for the addresses
 * to the necessary functions in the video decoder object.
 */
static void *init_video_decoder_plugin (xine_t *xine, void *data) {

  yuv_decoder_t *this ;

  this = (yuv_decoder_t *) malloc (sizeof (yuv_decoder_t));
  memset(this, 0, sizeof (yuv_decoder_t));

  this->video_decoder.init                = yuv_init;
  this->video_decoder.decode_data         = yuv_decode_data;
  this->video_decoder.flush               = yuv_flush;
  this->video_decoder.reset               = yuv_reset;
  this->video_decoder.close               = yuv_close;
  this->video_decoder.get_identifier      = yuv_get_id;
  this->video_decoder.dispose             = yuv_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t video_types[] = { 
  BUF_VIDEO_YVU9,
  0
 };

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 10, "yuv", XINE_VERSION_CODE, &dec_info_video, init_video_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
