/* 
 * Copyright (C) 2000-2001 the xine project
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
 * A decoder to fill the video buffer with similar frames
 */


#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "video_out.h"
#include "buffer.h"
#include "xine_internal.h"
#include "memcpy.h"

/* The videofill decoder's job in life is to copy the last frame displayed into
 * the current display queue, incrementing the PTS value accordingly. It probably
 * needs some work */

typedef struct videofill_decoder_s {
  video_decoder_t  video_decoder;

  vo_instance_t   *video_out;
} videofill_decoder_t;

static int videofill_can_handle (video_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_VIDEO_FILL) ;
}

static void videofill_init (video_decoder_t *this_gen, vo_instance_t *video_out) {

  videofill_decoder_t *this = (videofill_decoder_t *) this_gen;

  this->video_out = video_out;
  this->video_out->open (this->video_out);
}

static void videofill_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {

  videofill_decoder_t *this = (videofill_decoder_t *) this_gen;
  vo_frame_t *img, *last_img;

  last_img = this->video_out->get_last_frame (this->video_out);

  /* printf ("videofill: "); */

  if (last_img) {
    int image_size;

    /* printf (" duplicate "); */

    img = this->video_out->get_frame (this->video_out,
				      last_img->width,
				      last_img->height,
				      last_img->ratio, 
				      last_img->format,
				      last_img->duration,
				      VO_BOTH_FIELDS);

    image_size = last_img->width * last_img->height;

    fast_memcpy(img->base[0], last_img->base[0], image_size);
    fast_memcpy(img->base[1], last_img->base[1], image_size >> 2);
    fast_memcpy(img->base[2], last_img->base[2], image_size >> 2);

    img->PTS = 0;
    img->bad_frame = 0;

    if (img->copy) {
      int height = last_img->height;
      int stride = last_img->width;
      uint8_t* src[3];
	  
      src[0] = img->base[0];
      src[1] = img->base[1];
      src[2] = img->base[2];
      while ((height -= 16) >= 0) {
	img->copy(img, src);
	src[0] += 16 * stride;
	src[1] +=  4 * stride;
	src[2] +=  4 * stride;
      }
    }
    img->draw(img);
    img->free(img);
  }
  /* printf ("\n"); */
}

static void videofill_flush (video_decoder_t *this_gen) {
}

static void videofill_close (video_decoder_t *this_gen) {

  videofill_decoder_t *this = (videofill_decoder_t *) this_gen;

  this->video_out->close(this->video_out); 
}

static char *videofill_get_id(void) {
  return "videofill";
}

video_decoder_t *init_video_decoder_plugin (int iface_version, config_values_t *cfg) {

  videofill_decoder_t *this ;

  if (iface_version != 3) {
    printf( "videofill: plugin doesn't support plugin API version %d.\n"
	    "videofill: this means there's a version mismatch between xine and this "
	    "videofill: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  this = (videofill_decoder_t *) malloc (sizeof (videofill_decoder_t));

  this->video_decoder.interface_version   = 3;
  this->video_decoder.can_handle          = videofill_can_handle;
  this->video_decoder.init                = videofill_init;
  this->video_decoder.decode_data         = videofill_decode_data;
  this->video_decoder.flush               = videofill_flush;
  this->video_decoder.close               = videofill_close;
  this->video_decoder.get_identifier      = videofill_get_id;
  this->video_decoder.priority            = 2;

  return (video_decoder_t *) this;
}

