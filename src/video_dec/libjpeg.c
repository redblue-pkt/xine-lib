/*
 * Copyright (C) 2003-2019 the xine project
 * Copyright (C) 2012      Petri Hintukainen <phintuka@users.sourceforge.net>
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
 * Fast JPEG image decoder using libjpeg
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#define LOG_MODULE "jpeg_video_decoder"
#define LOG_VERBOSE

/*
#define LOG
*/

#include <jpeglib.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xine_buffer.h>


typedef struct jpeg_decoder_s {
  video_decoder_t   video_decoder;

  xine_stream_t    *stream;
  int64_t           pts;

  vo_frame_t       *vo_frame;

  unsigned char    *image;
  int               index;

  int               enable_downscaling;
  int               video_open;

} jpeg_decoder_t;

/*
 * memory source
 */

METHODDEF(void)
mem_init_source (j_decompress_ptr cinfo)
{
  (void)cinfo;
}

METHODDEF(boolean)
mem_fill_input_buffer (j_decompress_ptr cinfo)
{
  static const JOCTET EOI[] = { 0xFF, JPEG_EOI };

  cinfo->src->next_input_byte = EOI;
  cinfo->src->bytes_in_buffer = 2;

  return TRUE;
}

METHODDEF(void)
mem_skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
  if (num_bytes <= (int)cinfo->src->bytes_in_buffer) {
      cinfo->src->bytes_in_buffer -= num_bytes;
      cinfo->src->next_input_byte += num_bytes;
  } else {
    mem_fill_input_buffer(cinfo);
  }
}

METHODDEF(void)
mem_term_source (j_decompress_ptr cinfo)
{
  (void)cinfo;
}

static void jpeg_memory_src (j_decompress_ptr cinfo, const JOCTET *data, size_t size)
{
  if (!cinfo->src) {
    cinfo->src = (struct jpeg_source_mgr *)(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr));
  }

  cinfo->src->init_source       = mem_init_source;
  cinfo->src->fill_input_buffer = mem_fill_input_buffer;
  cinfo->src->skip_input_data   = mem_skip_input_data;
  cinfo->src->resync_to_restart = jpeg_resync_to_restart;
  cinfo->src->term_source       = mem_term_source;

  cinfo->src->bytes_in_buffer = size;
  cinfo->src->next_input_byte = data;
}

/*
 * xine-lib decoder interface
 */

static vo_frame_t *_jpeg_decode_data (jpeg_decoder_t *this, const char *data, size_t size) {
  vo_frame_t *f = NULL;

  if (!this->video_open) {
    lprintf("opening video\n");
    (this->stream->video_out->open) (this->stream->video_out, this->stream);
    this->video_open = 1;
  }

  if (size > 0) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPARRAY buffer;

    int         i, linesize;
    int         width, height;
    int         max_width, max_height;
    uint8_t    *slice_start[3] = {NULL, NULL, NULL};
    int         slice_line = 0;
    int         fullrange;
    uint8_t     ytab[256], ctab[256];
    int         frame_flags = VO_BOTH_FIELDS;
    int         format;

    /* query max. image size vo can handle */
    max_width = this->stream->video_out->get_property( this->stream->video_out,
                                                       VO_PROP_MAX_VIDEO_WIDTH);
    max_height = this->stream->video_out->get_property( this->stream->video_out,
                                                        VO_PROP_MAX_VIDEO_HEIGHT);

    /* init and parse header */

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_memory_src (&cinfo, data, size);
    jpeg_read_header(&cinfo, TRUE);

    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  cinfo.image_width);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, cinfo.image_height);

    lprintf("header parsed\n");

    /* set decoding parameters */

    cinfo.out_color_space = JCS_YCbCr;

    /* request scaling when image is too large for vo */
    if (this->enable_downscaling) {
      cinfo.output_width  = cinfo.image_width;
      cinfo.output_height = cinfo.image_height;
      cinfo.scale_num   = 1;
      cinfo.scale_denom = 1;
      while ((max_width  > 0 && (int)cinfo.output_width  > max_width) ||
             (max_height > 0 && (int)cinfo.output_height > max_height)) {
        cinfo.scale_denom   <<= 1;
        cinfo.output_width  >>= 1;
        cinfo.output_height >>= 1;
      }
      if (cinfo.scale_denom > 1) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                LOG_MODULE ": downscaling image by 1:%d to %dx%d\n",
                cinfo.scale_denom, cinfo.output_width, cinfo.output_height);
      }
    }

    /* start decompress */

    jpeg_start_decompress(&cinfo);

    width = cinfo.output_width;
    height = cinfo.output_height;

    /* crop when image is too large for vo */
    if (max_width > 0 && (int)cinfo.output_width > max_width)
      width = max_width;
    if (max_height > 0 && (int)cinfo.output_height > max_height)
      height = max_height;

    /* TJ. As far as I know JPEG always uses fullrange ITU-R 601 YUV.
       Let vo handle it (fast, good quality) or shrink to mpeg range here.
       In any case, set cm flags _before_ calling proc_slice (). */
    fullrange = !!(this->stream->video_out->get_capabilities (this->stream->video_out) & VO_CAP_FULLRANGE);
    if (fullrange) {
      VO_SET_FLAGS_CM (11, frame_flags);
    } else {
      unsigned int i;
      for (i = 0; i < 256; i++) {
        ytab[i] = (219 * i + 16 * 255 + 127) / 255;
        ctab[i] = (112 * i + (127 - 112) * 128 + 63) / 127;
      }
      VO_SET_FLAGS_CM (10, frame_flags);
    }

    format = (this->stream->video_out->get_capabilities (this->stream->video_out) & VO_CAP_YUY2) ?
             XINE_IMGFMT_YUY2 : XINE_IMGFMT_YV12;
    f = this->stream->video_out->get_frame (this->stream->video_out,
      width, height, (double)width/(double)height, format, frame_flags | VO_GET_FRAME_MAY_FAIL);
    if (!f) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              LOG_MODULE ": get_frame(%dx%d) failed\n", width, height);
      jpeg_finish_decompress(&cinfo);
      jpeg_destroy_decompress(&cinfo);
      this->index = 0;
      return NULL;
    }

    linesize = cinfo.output_width * cinfo.output_components;
    buffer = (cinfo.mem->alloc_sarray)((void*)&cinfo, JPOOL_IMAGE, linesize, 1);
    if (f->proc_slice && !(f->height & 0xf)) {
      slice_start[0] = f->base[0];
      slice_start[1] = f->base[1];
      slice_start[2] = f->base[2];
    }

    /* cut to frame width */
    if ((int)cinfo.output_width > f->width) {
      lprintf ("cut right border %d pixels\n", cinfo.output_width - f->width);
      linesize = f->width * 3;
    }

    /* YUV444->YUV422 simple */
    while (cinfo.output_scanline < cinfo.output_height) {
      uint8_t *dst = f->base[0] + f->pitches[0] * cinfo.output_scanline;

      jpeg_read_scanlines(&cinfo, buffer, 1);

      /* cut to frame height */
      if ((int)cinfo.output_scanline > f->height) {
        lprintf("cut bottom scanline %d\n", cinfo.output_scanline - 1);
        continue;
      }

      if (f->format == XINE_IMGFMT_YV12) {
        if (fullrange) {
          for (i = 0; i < linesize; i += 3) {
            *dst++ = buffer[0][i];
          }
          if (!(cinfo.output_scanline & 1)) {
            dst = f->base[1] + f->pitches[1] * cinfo.output_scanline / 2;
            for (i = 0; i < linesize; i += 6) {
              *dst++ = buffer[0][i + 1];
            }
            dst = f->base[2] + f->pitches[2] * cinfo.output_scanline / 2;
            for (i = 0; i < linesize; i += 6) {
              *dst++ = buffer[0][i + 2];
            }
          }
        } else {
          for (i = 0; i < linesize; i += 3) {
            *dst++ = ytab[(uint8_t)buffer[0][i]];
          }
          if (!(cinfo.output_scanline & 1)) {
            dst = f->base[1] + f->pitches[1] * cinfo.output_scanline / 2;
            for (i = 0; i < linesize; i += 6) {
              *dst++ = ctab[(uint8_t)buffer[0][i + 1]];
            }
            dst = f->base[2] + f->pitches[2] * cinfo.output_scanline / 2;
            for (i = 0; i < linesize; i += 6) {
              *dst++ = ctab[(uint8_t)buffer[0][i + 2]];
            }
          }
        }

        if (slice_start[0]) {
          slice_line++;
          if (slice_line == 16) {
            f->proc_slice (f, slice_start);
            slice_start[0] += 16 * f->pitches[0];
            slice_start[1] +=  8 * f->pitches[1];
            slice_start[2] +=  8 * f->pitches[2];
            slice_line = 0;
          }
        }

      } else /* XINE_IMGFMT_YUY2 */ {

        if (fullrange) {
          for (i = 0; i < linesize; i += 3) {
            *dst++ = buffer[0][i];
            if (i & 1) {
              *dst++ = buffer[0][i + 2];
            } else {
              *dst++ = buffer[0][i + 1];
            }
          }
        } else {
          for (i = 0; i < linesize; i += 3) {
            /* are these casts paranoid? */
            *dst++ = ytab[(uint8_t)buffer[0][i]];
            if (i & 1) {
              *dst++ = ctab[(uint8_t)buffer[0][i + 2]];
            } else {
              *dst++ = ctab[(uint8_t)buffer[0][i + 1]];
            }
          }
        }

        if (slice_start[0]) {
          slice_line++;
          if (slice_line == 16) {
            f->proc_slice (f, slice_start);
            slice_start[0] += 16 * f->pitches[0];
            slice_line = 0;
          }
        }
      }
    }

    /* final slice */
    if (slice_start[0] && slice_line) {
      f->proc_slice (f, slice_start);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    f->duration  = 3600;
    f->bad_frame = 0;

    _x_stream_info_set (this->stream, XINE_STREAM_INFO_FRAME_DURATION, f->duration);

    this->index = 0;
  }

  return f;
}

static void jpeg_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
  jpeg_decoder_t *this = (jpeg_decoder_t *) this_gen;
  vo_frame_t *f = NULL;
  /* demux_image sends everything as preview at open time,
   * then an empty buf at play time.
   * we need to defer output to the latter because
   * - we want it to get correct vpts,
   * - we want it marked as first frame after seek, and
   * - we dont want it flushed by a previous stream stop. */

  if (!(buf->decoder_flags & BUF_FLAG_PREVIEW) && buf->pts)
    this->pts = buf->pts;

  do {
    if (buf->size > 0) {
      if (this->index == 0 && (buf->decoder_flags & BUF_FLAG_FRAME_END)) {
        /* complete frame */
        f = _jpeg_decode_data (this, buf->content, buf->size);
        break;
      }
      xine_buffer_copyin (this->image, this->index, buf->mem, buf->size);
      this->index += buf->size;
    }
    if ((buf->decoder_flags & BUF_FLAG_FRAME_END) && (this->index > 0)) {
      f = _jpeg_decode_data (this, this->image, this->index);
      this->index = 0;
    }
  } while (0);

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


static void jpeg_flush (video_decoder_t *this_gen) {
  jpeg_decoder_t *this = (jpeg_decoder_t *) this_gen;
  /*
   * flush out any frames that are still stored in the decoder
   */
  if (this->vo_frame) {
    this->vo_frame->pts = this->pts;
    this->vo_frame->draw (this->vo_frame, this->stream);
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
}


static void jpeg_reset (video_decoder_t *this_gen) {
  jpeg_decoder_t *this = (jpeg_decoder_t *) this_gen;

  /*
   * reset decoder after engine flush (prepare for new
   * video data not related to recently decoded data)
   */
  if (this->vo_frame) {
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
  this->index = 0;
}


static void jpeg_discontinuity (video_decoder_t *this_gen) {
  (void)this_gen;
  /*
   * a time reference discontinuity has happened.
   * that is, it must forget any currently held pts value
   */
}

static void jpeg_dispose (video_decoder_t *this_gen) {
  jpeg_decoder_t *this = (jpeg_decoder_t *) this_gen;

  if (this->vo_frame) {
    this->vo_frame->free (this->vo_frame);
    this->vo_frame = NULL;
  }
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

  jpeg_decoder_t *this;
  cfg_entry_t    *cfg_entry;

  lprintf("opened\n");

  (void)class_gen;

  this = calloc(1, sizeof(jpeg_decoder_t));
  if (!this)
    return NULL;

  this->video_decoder.decode_data         = jpeg_decode_data;
  this->video_decoder.flush               = jpeg_flush;
  this->video_decoder.reset               = jpeg_reset;
  this->video_decoder.discontinuity       = jpeg_discontinuity;
  this->video_decoder.dispose             = jpeg_dispose;
  this->stream                            = stream;

  /*
   * initialisation of privates
   */

  this->vo_frame = NULL;
  this->image = xine_buffer_init(10240);

  cfg_entry = stream->xine->config->lookup_entry(stream->xine->config, "video.processing.libjpeg_downscaling");
  if (cfg_entry) {
    this->enable_downscaling = cfg_entry->num_value;
  }

  return &this->video_decoder;
}

/*
 * jpeg plugin class
 */
static void *init_class (xine_t *xine, const void *data) {

  (void)data;

  static video_decoder_class_t decode_video_libjpeg_class = {
    .open_plugin     = open_plugin,
    .identifier      = "jpegvdec",
    .description     = N_("JPEG image video decoder plugin"),
    .dispose         = NULL,
  };

  /*
   * initialisation of privates
   */

  xine->config->register_bool(xine->config,
      "video.processing.libjpeg_downscaling", 1,
      _("allow downscaling of JPEG images (an alternative is to crop)"),
      _("If enabled, you allow xine to downscale JPEG images "
	"so that those can be viewed with your graphics hardware. "
	"If scaling is disabled, images will be cropped."),
      10, NULL, NULL);

  lprintf("class opened\n");

  return (void *)&decode_video_libjpeg_class;
}

/*
 * exported plugin catalog entry
 */

static const uint32_t supported_types[] = { BUF_VIDEO_JPEG, 0 };

static const decoder_info_t dec_info_jpeg = {
  .supported_types = supported_types,
  .priority        = 10,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER, 19, "jpeg", XINE_VERSION_CODE, &dec_info_jpeg, init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
