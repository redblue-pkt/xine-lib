/*
 * Copyright (C) 2006-2019 the xine project
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

typedef struct image_decoder_s {
  video_decoder_t   video_decoder;

  xine_stream_t    *stream;
  vo_frame_t       *vo_frame;
  int64_t           pts;

  int               video_open;

  GdkPixbufLoader  *loader;

} image_decoder_t;


static void image_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  image_decoder_t *this = (image_decoder_t *) this_gen;
  GError *error = NULL;
  vo_frame_t *f = NULL;
  /* demux_image sends everything as preview at open time,
   * then an empty buf at play time.
   * we need to defer output to the latter because
   * - we want it to get correct vpts,
   * - we want it marked as first frame after seek, and
   * - we dont want it flushed by a previous stream stop. */

  if (!(buf->decoder_flags & BUF_FLAG_PREVIEW) && buf->pts)
    this->pts = buf->pts;

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
    int                width, height, rowstride, n_channels;
    guchar            *img_buf;
    int                color_matrix, flags, format;
    rgb2yuy2_t        *rgb2yuy2;

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

    flags = VO_BOTH_FIELDS;
    color_matrix =
      this->stream->video_out->get_capabilities (this->stream->video_out) & VO_CAP_FULLRANGE ? 11 : 10;
    VO_SET_FLAGS_CM (color_matrix, flags);
    
    /*
     * alloc video frame
     */
    format = (this->stream->video_out->get_capabilities (this->stream->video_out) & VO_CAP_YUY2) ?
             XINE_IMGFMT_YUY2 : XINE_IMGFMT_YV12;
    f = this->stream->video_out->get_frame (this->stream->video_out, width, height,
      (double)width / (double)height, format, flags | VO_GET_FRAME_MAY_FAIL);
    if (!f) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              LOG_MODULE ": get_frame(%dx%d) failed\n", width, height);
      g_object_unref (pixbuf);
      return;
    }

    /* crop if allocated frame is smaller than requested */
    if (width > f->width)
      width = f->width;
    if (height > f->height)
      height = f->height;
    f->ratio = (double)width / (double)height;

    /* rgb data -> yuv */
    n_channels = gdk_pixbuf_get_n_channels (pixbuf);
    rowstride = gdk_pixbuf_get_rowstride (pixbuf);

    rgb2yuy2 = rgb2yuy2_alloc (color_matrix, n_channels > 3 ? "rgba" : "rgb");

    if (f->format == XINE_IMGFMT_YV12) {
      rgb2yv12_slice (rgb2yuy2, img_buf, rowstride,
                      f->base[0], f->pitches[0],
                      f->base[1], f->pitches[1],
                      f->base[2], f->pitches[2],
                      width, height);
    } else {
      if (!f->proc_slice || (f->height & 15)) {
        /* do all at once */
        rgb2yuy2_slice (rgb2yuy2, img_buf, rowstride, f->base[0], f->pitches[0], width, height);
      } else {
        /* sliced */
        uint8_t *sptr[1];
        int y, h = 16;
        for (y = 0; y < height; y += 16) {
          if (y + 16 > height)
            h = height & 15;
          sptr[0] = f->base[0] + y * f->pitches[0];
          rgb2yuy2_slice (rgb2yuy2, img_buf + y * rowstride, rowstride, sptr[0], f->pitches[0], width, h);
          f->proc_slice (f, sptr);
        }
      }
    }
    rgb2yuy2_free (rgb2yuy2);
    g_object_unref (pixbuf);

    /*
     * draw video frame
     */
    f->duration = 3600;
    f->bad_frame = 0;

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, f->duration);
  }

  if (f) {
    if (this->vo_frame) {
      if (!(buf->decoder_flags & BUF_FLAG_PREVIEW)) {
        this->vo_frame->pts = this->pts;
        this->vo_frame->draw (this->vo_frame, this->stream);
      }
      this->vo_frame->free (this->vo_frame);
    }
    this->vo_frame = f;
  }

  if (this->vo_frame && !(buf->decoder_flags & BUF_FLAG_PREVIEW)) {
    this->vo_frame->pts = this->pts;
    this->vo_frame->draw (this->vo_frame, this->stream);
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
}


static void image_flush (video_decoder_t *this_gen) {
  image_decoder_t *this = (image_decoder_t *) this_gen;
  /*
   * flush out any frames that are still stored in the decoder
   */
  if (this->vo_frame) {
    this->vo_frame->draw (this->vo_frame, this->stream);
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
}


static void image_reset (video_decoder_t *this_gen) {
  image_decoder_t *this = (image_decoder_t *) this_gen;

  /*
   * reset decoder after engine flush (prepare for new
   * video data not related to recently decoded data)
   */
  if (this->vo_frame) {
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
  if (this->loader != NULL) {
    gdk_pixbuf_loader_close (this->loader, NULL);
    g_object_unref (G_OBJECT (this->loader));
    this->loader = NULL;
  }
}


static void image_discontinuity (video_decoder_t *this_gen) {
  /* image_decoder_t *this = (image_decoder_t *) this_gen; */

  (void)this_gen;
  /*
   * a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
}

static void image_dispose (video_decoder_t *this_gen) {
  image_decoder_t *this = (image_decoder_t *) this_gen;

  if (this->vo_frame) {
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
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

  image_decoder_t *this;

  lprintf("opened\n");

  (void)class_gen;

#if (GLIB_MAJOR_VERSION < 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION <= 35))
  g_type_init ();
#endif

  this = (image_decoder_t *) calloc(1, sizeof(image_decoder_t));
  if (!this)
    return NULL;

  this->video_decoder.decode_data         = image_decode_data;
  this->video_decoder.flush               = image_flush;
  this->video_decoder.reset               = image_reset;
  this->video_decoder.discontinuity       = image_discontinuity;
  this->video_decoder.dispose             = image_dispose;
  this->stream                            = stream;

  /*
   * initialisation of privates
   */
  this->vo_frame = NULL;
  this->loader = NULL;

  return &this->video_decoder;
}

/*
 * image plugin class
 */

static void *init_class (xine_t *xine, const void *data) {

  (void)xine;
  (void)data;

  static const video_decoder_class_t decode_video_gdkpixbuf_class = {
    .open_plugin     = open_plugin,
    .identifier      = "gdkpixbuf",
    .description     = N_("gdk-pixbuf image video decoder plugin"),
    .dispose         = NULL,
  };

  return (void *)&decode_video_gdkpixbuf_class;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t supported_types[] = {
  BUF_VIDEO_IMAGE,
  BUF_VIDEO_JPEG,
  BUF_VIDEO_PNG,
  0
};

static const decoder_info_t dec_info_image = {
  .supported_types = supported_types,
  .priority        = 8,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "gdkpixbuf", XINE_VERSION_CODE, &dec_info_image, init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
