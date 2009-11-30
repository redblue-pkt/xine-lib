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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * a gdk-pixbuf-based image video decoder
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif



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

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
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

  GdkPixbufLoader  *loader;

} image_decoder_t;


static void image_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  image_decoder_t *this = (image_decoder_t *) this_gen;
  GError *error = NULL;

  if (!this->video_open) {
    lprintf("opening video\n");
    (this->stream->video_out->open) (this->stream->video_out, this->stream);
    this->video_open = 1;
  }

  if (this->loader == NULL) {
    this->loader = gdk_pixbuf_loader_new ();
  }

  if (gdk_pixbuf_loader_write (this->loader, buf->mem, buf->size, &error) == FALSE) {
    lprintf("error loading image: %s\n", error->message);
    g_error_free (error);
    gdk_pixbuf_loader_close (this->loader, NULL);
    g_object_unref (G_OBJECT (this->loader));
    this->loader = NULL;
    return;
  }

  if (buf->decoder_flags & BUF_FLAG_FRAME_END) {
    GdkPixbuf         *pixbuf;
    int                width, height, x, y, rowstride, n_channels, i;
    guchar            *img_buf;
    yuv_planes_t       yuv_planes;
    vo_frame_t        *img;

    /*
     * this->image -> rgb data
     */
    if (gdk_pixbuf_loader_close (this->loader, &error) == FALSE) {
      lprintf("error loading image: %s\n", error->message);
      g_error_free (error);
      g_object_unref (G_OBJECT (this->loader));
      this->loader = NULL;
      return;
    }

    pixbuf = gdk_pixbuf_loader_get_pixbuf (this->loader);
    if (pixbuf != NULL)
      g_object_ref (G_OBJECT (pixbuf));
    g_object_unref (this->loader);
    this->loader = NULL;

    if (pixbuf == NULL) {
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

  if (this->loader != NULL) {
    gdk_pixbuf_loader_close (this->loader, NULL);
    g_object_unref (G_OBJECT (this->loader));
    this->loader = NULL;
  }
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

  if (this->loader != NULL) {
    gdk_pixbuf_loader_close (this->loader, NULL);
    g_object_unref (G_OBJECT (this->loader));
    this->loader = NULL;
  }

  lprintf("closed\n");
  free (this);
}


static video_decoder_t *open_plugin (video_decoder_class_t *class_gen,
				     xine_stream_t *stream) {

  image_class_t   *cls = (image_class_t *) class_gen;
  image_decoder_t *this;

  lprintf("opened\n");

  g_type_init ();

  this = (image_decoder_t *) calloc(1, sizeof(image_decoder_t));

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

  return &this->video_decoder;
}

/*
 * image plugin class
 */
static void *init_class (xine_t *xine, void *data) {

  image_class_t       *this;

  this = (image_class_t *) calloc(1, sizeof(image_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.identifier      = "gdkpixbuf";
  this->decoder_class.description     = N_("gdk-pixbuf image video decoder plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;

  /*
   * initialisation of privates
   */

  lprintf("class opened\n");

  return this;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t supported_types[] = { BUF_VIDEO_IMAGE, BUF_VIDEO_JPEG, 0 };

static const decoder_info_t dec_info_image = {
  supported_types,     /* supported types */
  7                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "gdkpixbuf", XINE_VERSION_CODE, &dec_info_image, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
