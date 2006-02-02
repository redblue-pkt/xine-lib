/*
 * Copyright (C) 2006 the xine project
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
 * $Id: gdkpixbuf.c,v 1.1 2006/02/02 20:39:32 hadess Exp $
 *
 * a gdk-pixbuf-based image video decoder
 */


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#define LOG_MODULE "gdkpixbuf_video_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "xineutils.h"
#include "bswap.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct {
  video_decoder_class_t   decoder_class;

  /*
   * private variables
   */

} image_class_t;


typedef struct image_decoder_s {
  video_decoder_t   video_decoder;

  image_class_t    *cls;

  xine_stream_t    *stream;
  int               video_open;
  
  unsigned char    *image;
  int               index;

} image_decoder_t;


static void image_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  image_decoder_t *this = (image_decoder_t *) this_gen;

  if (!this->video_open) {
    lprintf("opening video\n");
    this->stream->video_out->open(this->stream->video_out, this->stream);
    this->video_open = 1;
  }

  xine_buffer_copyin(this->image, this->index, buf->mem, buf->size);
  this->index += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAME_END) {
    GdkPixbufLoader   *loader;
    GdkPixbuf         *pixbuf;
    int                width, height, x, y, rowstride, n_channels, i;
    guchar            *img_buf;
    yuv_planes_t       yuv_planes;
    vo_frame_t        *img;

    /*
     * this->image -> rgb data
     */
    loader = gdk_pixbuf_loader_new ();
    if (gdk_pixbuf_loader_write (loader, this->image, this->index, NULL) == FALSE) {
      lprintf("error loading image\n");
      return;
    }

    if (gdk_pixbuf_loader_close (loader, NULL) == FALSE) {
      lprintf("error loading image\n");
      return;
    }

    pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
    this->index = 0;

    if (pixbuf == NULL) {
      g_object_unref (loader);
      lprintf("error loading image\n");
      return;
    }

    width = gdk_pixbuf_get_width (pixbuf) & ~1; /* must be even for init_yuv_planes */
    height = gdk_pixbuf_get_height (pixbuf);
    img_buf = gdk_pixbuf_get_pixels (pixbuf);

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, width);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, height);

    lprintf("image loaded successfully\n");

    /*
     * rgb data -> yuv_planes
     */
    init_yuv_planes(&yuv_planes, width, height);

    n_channels = gdk_pixbuf_get_n_channels (pixbuf);
    rowstride = gdk_pixbuf_get_rowstride (pixbuf);
    i = 0;
    for (y = 0; y < height; y++) {
      for (x = 0; x < width; x++) {
        guchar *p;
        p = img_buf + y * rowstride + x * n_channels;

	yuv_planes.y[i] = COMPUTE_Y (p[0], p[1], p[2]);
	yuv_planes.u[i] = COMPUTE_U (p[0], p[1], p[2]);
	yuv_planes.v[i] = COMPUTE_V (p[0], p[1], p[2]);

	i++;
      }
    }
    gdk_pixbuf_unref (pixbuf);

    /*
     * alloc and draw video frame
     */
    img = this->stream->video_out->get_frame (this->stream->video_out, width,
					      height, (double)width/(double)height, 
					      XINE_IMGFMT_YUY2, 
					      VO_BOTH_FIELDS);
    img->pts = buf->pts;
    img->duration = 3600;
    img->bad_frame = 0;

    yuv444_to_yuy2(&yuv_planes, img->base[0], img->pitches[0]);
    free_yuv_planes(&yuv_planes);
    
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, img->duration);
    
    img->draw(img, this->stream);
    img->free(img);
  }
}


static void image_flush (video_decoder_t *this_gen) {
  /* image_decoder_t *this = (image_decoder_t *) this_gen; */
  
  /*
   * flush out any frames that are still stored in the decoder
   */
}


static void image_reset (video_decoder_t *this_gen) {
  image_decoder_t *this = (image_decoder_t *) this_gen;
   
  /*
   * reset decoder after engine flush (prepare for new
   * video data not related to recently decoded data)
   */
  
  this->index = 0;
}


static void image_discontinuity (video_decoder_t *this_gen) {
  /* image_decoder_t *this = (image_decoder_t *) this_gen; */
 
  /*
   * a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
}

static void image_dispose (video_decoder_t *this_gen) {
  image_decoder_t *this = (image_decoder_t *) this_gen;

  if (this->video_open) {
    lprintf("closing video\n");

    this->stream->video_out->close(this->stream->video_out, this->stream);
    this->video_open = 0;
  }

  xine_buffer_free(this->image);

  lprintf("closed\n");
  free (this);
}


static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, 
				     xine_stream_t *stream) {

  image_class_t   *cls = (image_class_t *) class_gen;
  image_decoder_t *this;

  lprintf("opened\n");

  g_type_init ();

  this = (image_decoder_t *) xine_xmalloc (sizeof (image_decoder_t));

  this->video_decoder.decode_data         = image_decode_data;
  this->video_decoder.flush               = image_flush;
  this->video_decoder.reset               = image_reset;
  this->video_decoder.discontinuity       = image_discontinuity;
  this->video_decoder.dispose             = image_dispose;
  this->cls                               = cls;
  this->stream                            = stream;

  /*
   * initialisation of privates
   */

  this->image = xine_buffer_init(10240);

  return &this->video_decoder;
}

/*
 * image plugin class
 */

static char *get_identifier (video_decoder_class_t *this) {
  return "gdkpixbuf";
}

static char *get_description (video_decoder_class_t *this) {
  return "gdk-pixbuf image video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this_gen) {
  image_class_t   *this = (image_class_t *) this_gen;

  lprintf("class closed\n");
  
  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  image_class_t       *this;

  this = (image_class_t *) xine_xmalloc (sizeof (image_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  /*
   * initialisation of privates
   */

  lprintf("class opened\n");
    
  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t supported_types[] = { BUF_VIDEO_IMAGE,
                                      0 };

static decoder_info_t dec_info_image = {
  supported_types,     /* supported types */
  6                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 18, "gdkpixbuf", XINE_VERSION_CODE, &dec_info_image, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
