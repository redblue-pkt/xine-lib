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
 * General description and author credits go here...
 * 
 * Leave the following line intact for when the decoder is committed to CVS:
 * $Id: foovideo.c,v 1.5 2002/09/05 20:44:41 mroi Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "video_out.h"
#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "bswap.h"

#define VIDEOBUFSIZE 128*1024

typedef struct foovideo_decoder_s {
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

  /* these are variables exclusive to the foo video decoder */
  unsigned char     current_yuv_byte;
  
} foovideo_decoder_t;

/**************************************************************************
 * foovideo specific decode functions
 *************************************************************************/

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

/*
 * This function is called by xine to determine which buffer types this
 * decoder knows how to handle. 
 * Parameters:
 *  this_gen: A video decoder object
 *  buf_type: The number of the buffer type that xine is querying for;
 *    these buffer constants are defined in src/xine-engine/buffer.h.
 * Return:
 *  1 if the decoder is capable of handling buf_type
 *  0 if the decoder is not capable of handling buf_type
 */
static int foovideo_can_handle (video_decoder_t *this_gen, int buf_type) {

  /* this function will usually take the form of:

  return (buf_type == BUF_VIDEO_FOOVIDEO_V1 ||
          buf_type == BUF_VIDEO_FOOVIDEO_V2);

     where the constants such as BUF_VIDEO_FOOVIDEO_V1 are defined in
     src/xine-engine/buffer.h.

     But for this example, return 0, indicating that this plugin handles
     no buffer types.
  */

  return 0;
}

/*
 * This function is responsible is called to initialize the video decoder
 * for use. Initialization usually involves setting up the fields in your
 * private video decoder object.
 */
static void foovideo_init (video_decoder_t *this_gen, 
  vo_instance_t *video_out) {
  foovideo_decoder_t *this = (foovideo_decoder_t *) this_gen;

  /* set our own video_out object to the one that xine gives us */
  this->video_out  = video_out;

  /* indicate that the decoder is not quite ready yet */
  this->decoder_ok = 0;
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void foovideo_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  foovideo_decoder_t *this = (foovideo_decoder_t *) this_gen;
  xine_bmiheader *bih;

  vo_frame_t *img; /* video out frame */

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

    /* do anything else relating to initializing this decoder */
    this->current_yuv_byte = 0;

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
                                        42, IMGFMT_YUY2, VO_BOTH_FIELDS);

      img->duration  = this->video_step;
      img->pts       = buf->pts;
      img->bad_frame = 0;

      memset(img->base[0], this->current_yuv_byte,
        this->width * this->height * 2);
      this->current_yuv_byte += 3;

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
static void foovideo_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void foovideo_reset (video_decoder_t *this_gen) {
  foovideo_decoder_t *this = (foovideo_decoder_t *) this_gen;

  this->size = 0;
}

/*
 * This function is called when xine shuts down the decoder. It should
 * free any memory and release any other resources allocated during the
 * execution of the decoder.
 */
static void foovideo_close (video_decoder_t *this_gen) {
  foovideo_decoder_t *this = (foovideo_decoder_t *) this_gen;

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
static char *foovideo_get_id(void) {
  return "foovideo";
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void foovideo_dispose (video_decoder_t *this_gen) {
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
static void *init_video_decoder_plugin (int iface_version, xine_t *xine) {

  foovideo_decoder_t *this ;

  if (iface_version != 10) {
    printf( "foovideo: plugin doesn't support plugin API version %d.\n"
            "foovideo: this means there's a version mismatch between xine and this "
            "foovideo: decoder plugin.\nInstalling current plugins should help.\n",
            iface_version);
    return NULL;
  }

  this = (foovideo_decoder_t *) malloc (sizeof (foovideo_decoder_t));
  memset(this, 0, sizeof (foovideo_decoder_t));

  this->video_decoder.interface_version   = iface_version;
  this->video_decoder.can_handle          = foovideo_can_handle;
  this->video_decoder.init                = foovideo_init;
  this->video_decoder.decode_data         = foovideo_decode_data;
  this->video_decoder.flush               = foovideo_flush;
  this->video_decoder.reset               = foovideo_reset;
  this->video_decoder.close               = foovideo_close;
  this->video_decoder.get_identifier      = foovideo_get_id;
  this->video_decoder.dispose             = foovideo_dispose;
  this->video_decoder.priority            = 1;

  return (video_decoder_t *) this;
}

