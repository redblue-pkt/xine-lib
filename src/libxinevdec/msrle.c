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
 * MS RLE Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information on the MS RLE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 * 
 * $Id: msrle.c,v 1.6 2002/09/05 20:44:41 mroi Exp $
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

typedef struct msrle_decoder_s {
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

  unsigned char     yuv_palette[256 * 4];
  yuv_planes_t      yuv_planes;

} msrle_decoder_t;

/**************************************************************************
 * MS RLE specific decode functions
 *************************************************************************/

#define FETCH_NEXT_STREAM_BYTE() \
    if (stream_ptr >= this->size) \
    { \
      printf(_("MS RLE: stream ptr just went out of bounds (1)\n")); \
      return; \
    } \
    stream_byte = this->buf[stream_ptr++];

void decode_msrle8(msrle_decoder_t *this) {

  int stream_ptr = 0;
  unsigned char rle_code;
  unsigned char extra_byte;
  unsigned char stream_byte;
  int pixel_ptr = 0;
  int row_dec = this->yuv_planes.row_width;
  int row_ptr = (this->height - 1) * row_dec;
  int frame_size = this->yuv_planes.row_width * this->height;
  unsigned char y, u, v;

  while (row_ptr >= 0) {
    FETCH_NEXT_STREAM_BYTE();
    rle_code = stream_byte;
    if (rle_code == 0) {
      /* fetch the next byte to see how to handle escape code */
      FETCH_NEXT_STREAM_BYTE();
      if (stream_byte == 0) {
        /* line is done, goto the next one */
        row_ptr -= row_dec;
        pixel_ptr = 0;
      } else if (stream_byte == 1) {
        /* decode is done */
        return;
      } else if (stream_byte == 2) {
        /* reposition frame decode coordinates */
        FETCH_NEXT_STREAM_BYTE();
        pixel_ptr += stream_byte;
        FETCH_NEXT_STREAM_BYTE();
        row_ptr -= stream_byte * row_dec;
      } else {
        /* copy pixels from encoded stream */
        if ((row_ptr + pixel_ptr + stream_byte > frame_size) ||
            (row_ptr < 0)) {
          printf(_("MS RLE: frame ptr just went out of bounds (1)\n"));
          return;
        }

        rle_code = stream_byte;
        extra_byte = stream_byte & 0x01;
        if (stream_ptr + rle_code + extra_byte > this->size) {
          printf(_("MS RLE: stream ptr just went out of bounds (2)\n"));
          return;
        }

        while (rle_code--) {
          FETCH_NEXT_STREAM_BYTE();
          y = this->yuv_palette[stream_byte * 4 + 0];
          u = this->yuv_palette[stream_byte * 4 + 1];
          v = this->yuv_palette[stream_byte * 4 + 2];
          this->yuv_planes.y[row_ptr + pixel_ptr] = y;
          this->yuv_planes.u[row_ptr + pixel_ptr] = u;
          this->yuv_planes.v[row_ptr + pixel_ptr] = v;
          pixel_ptr++;
        }

        /* if the RLE code is odd, skip a byte in the stream */
        if (extra_byte)
          stream_ptr++;
      }
    } else {
      /* decode a run of data */
      if ((row_ptr + pixel_ptr + stream_byte > frame_size) ||
          (row_ptr < 0)) {
        printf(_("MS RLE: frame ptr just went out of bounds (2)\n"));
        return;
      }

      FETCH_NEXT_STREAM_BYTE();

      y = this->yuv_palette[stream_byte * 4 + 0];
      u = this->yuv_palette[stream_byte * 4 + 1];
      v = this->yuv_palette[stream_byte * 4 + 2];

      while(rle_code--) {
        this->yuv_planes.y[row_ptr + pixel_ptr] = y;
        this->yuv_planes.u[row_ptr + pixel_ptr] = u;
        this->yuv_planes.v[row_ptr + pixel_ptr] = v;
        pixel_ptr++;
      }
    }
  }

  /* one last sanity check on the way out */
  if (stream_ptr < this->size)
    printf(_("MS RLE: ended frame decode with bytes left over (%d < %d)\n"),
      stream_ptr, this->size);
}

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function is responsible is called to initialize the video decoder
 * for use. Initialization usually involves setting up the fields in your
 * private video decoder object.
 */
static void msrle_init (video_decoder_t *this_gen, 
  vo_instance_t *video_out) {
  msrle_decoder_t *this = (msrle_decoder_t *) this_gen;

  /* set our own video_out object to the one that xine gives us */
  this->video_out  = video_out;

  /* indicate that the decoder is not quite ready yet */
  this->decoder_ok = 0;
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void msrle_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  msrle_decoder_t *this = (msrle_decoder_t *) this_gen;
  xine_bmiheader *bih;
  palette_entry_t *palette;
  int i;
  vo_frame_t *img; /* video out frame */

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  /* load the palette */
  if ((buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      (buf->decoder_info[1] == BUF_SPECIAL_PALETTE)) {
    palette = (palette_entry_t *)buf->decoder_info[3];
    for (i = 0; i < buf->decoder_info[2]; i++) {
      this->yuv_palette[i * 4 + 0] =
        COMPUTE_Y(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 1] =
        COMPUTE_U(palette[i].r, palette[i].g, palette[i].b);
      this->yuv_palette[i * 4 + 2] =
        COMPUTE_V(palette[i].r, palette[i].g, palette[i].b);
    }
  }

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

    init_yuv_planes(&this->yuv_planes, this->width, this->height);

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

      img = this->video_out->get_frame (this->video_out,
                                        this->width, this->height,
                                        XINE_VO_ASPECT_DONT_TOUCH, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      decode_msrle8(this);
      yuv444_to_yuy2(&this->yuv_planes, img->base[0], img->pitches[0]);

      if (img->copy) {
	int height = img->height;
	uint8_t *src[3];

	src[0] = img->base[0];

	while ((height -= 16) >= 0) {
	  img->copy(img, src);
	  src[0] += 16 * img->pitches[0];
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
static void msrle_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void msrle_reset (video_decoder_t *this_gen) {
  msrle_decoder_t *this = (msrle_decoder_t *) this_gen;

  this->size = 0;
}

/*
 * This function is called when xine shuts down the decoder. It should
 * free any memory and release any other resources allocated during the
 * execution of the decoder.
 */
static void msrle_close (video_decoder_t *this_gen) {
  msrle_decoder_t *this = (msrle_decoder_t *) this_gen;

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
static char *msrle_get_id(void) {
  return "Microsoft RLE";
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void msrle_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

/*
 * This function should be the plugin's only advertised function to the
 * outside world. It allows xine to query the plugin module for the addresses
 * to the necessary functions in the video decoder object. The video
 * decoder object also has a priority field which allows different decoder
 * plugins for the same buffer types to coexist peacefully. The higher the
 * priority number, the more precedence a decoder has. E.g., 9 beats 1.
 */
static void *init_video_decoder_plugin (xine_t *xine, void *data) {

  msrle_decoder_t *this ;

  this = (msrle_decoder_t *) malloc (sizeof (msrle_decoder_t));
  memset(this, 0, sizeof (msrle_decoder_t));

  this->video_decoder.init                = msrle_init;
  this->video_decoder.decode_data         = msrle_decode_data;
  this->video_decoder.flush               = msrle_flush;
  this->video_decoder.reset               = msrle_reset;
  this->video_decoder.close               = msrle_close;
  this->video_decoder.get_identifier      = msrle_get_id;
  this->video_decoder.dispose             = msrle_dispose;
  this->video_decoder.priority            = 1;

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t video_types[] = { 
  BUF_VIDEO_MSRLE,
  0
 };

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 10, "msrle", XINE_VERSION_CODE, &dec_info_video, init_video_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
