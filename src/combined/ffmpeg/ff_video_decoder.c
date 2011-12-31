/*
 * Copyright (C) 2001-2008 the xine project
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
 * xine video decoder plugin using ffmpeg
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <assert.h>

#define LOG_MODULE "ffmpeg_video_dec"
#define LOG_VERBOSE
/*
#define LOG
*/
#include <xine/xine_internal.h>
#include "bswap.h"
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "ffmpeg_decoder.h"
#include "ff_mpeg_parser.h"

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <postprocess.h>
#else
#  include <libpostproc/postprocess.h>
#endif

#define VIDEOBUFSIZE        (128*1024)
#define SLICE_BUFFER_SIZE   (1194*1024)

#define SLICE_OFFSET_SIZE   128

#define ENABLE_DIRECT_RENDERING

#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 32)
#  define AVVIDEO 2
#else
#  define AVVIDEO 1
#  define pp_context	pp_context_t
#  define pp_mode	pp_mode_t
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 112)
#  define DEPRECATED_AVCODEC_THREAD_INIT 1
#endif

typedef struct ff_video_decoder_s ff_video_decoder_t;

typedef struct ff_video_class_s {
  video_decoder_class_t   decoder_class;

  int                     pp_quality;
  int                     thread_count;
  int8_t                  skip_loop_filter_enum;
  int8_t                  choose_speed_over_accuracy;

  xine_t                 *xine;
} ff_video_class_t;

struct ff_video_decoder_s {
  video_decoder_t   video_decoder;

  ff_video_class_t *class;

  xine_stream_t    *stream;
  int64_t           pts;
  int64_t           last_pts;
  uint64_t          pts_tag_mask;
  uint64_t          pts_tag;
  int               pts_tag_counter;
  int               pts_tag_stable_counter;
  int               video_step;
  int               reported_video_step;

  uint8_t           decoder_ok:1;
  uint8_t           decoder_init_mode:1;
  uint8_t           is_mpeg12:1;
  uint8_t           pp_available:1;
  uint8_t           yuv_init:1;
  uint8_t           is_direct_rendering_disabled:1;
  uint8_t           cs_convert_init:1;
  uint8_t           assume_bad_field_picture:1;

  xine_bmiheader    bih;
  unsigned char    *buf;
  int               bufsize;
  int               size;
  int               skipframes;

  int               slice_offset_size;

  AVFrame          *av_frame;
  AVCodecContext   *context;
  AVCodec          *codec;

  int               pp_quality;
  int               pp_flags;
  pp_context       *our_context;
  pp_mode          *our_mode;

  /* mpeg-es parsing */
  mpeg_parser_t    *mpeg_parser;

  double            aspect_ratio;
  int               aspect_ratio_prio;
  int               frame_flags;
  int               crop_right, crop_bottom;

  int               output_format;

  xine_list_t       *dr1_frames;

  yuv_planes_t      yuv;

  AVPaletteControl  palette_control;

#ifdef LOG
  enum PixelFormat  debug_fmt;
#endif
};


static void set_stream_info(ff_video_decoder_t *this) {
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->bih.biHeight);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->aspect_ratio * 10000);
}

#ifdef ENABLE_DIRECT_RENDERING
/* called from ffmpeg to do direct rendering method 1 */
static int get_buffer(AVCodecContext *context, AVFrame *av_frame){
  ff_video_decoder_t *this = (ff_video_decoder_t *)context->opaque;
  vo_frame_t *img;
  int width  = context->width;
  int height = context->height;

  if (!this->bih.biWidth || !this->bih.biHeight) {
    this->bih.biWidth = width;
    this->bih.biHeight = height;

    if (this->aspect_ratio_prio == 0) {
      this->aspect_ratio = (double)width / (double)height;
      this->aspect_ratio_prio = 1;
      lprintf("default aspect ratio: %f\n", this->aspect_ratio);
      set_stream_info(this);
    }
  }

  avcodec_align_dimensions(context, &width, &height);

  if( this->context->pix_fmt != PIX_FMT_YUV420P && this->context->pix_fmt != PIX_FMT_YUVJ420P ) {
    if (!this->is_direct_rendering_disabled) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("ffmpeg_video_dec: unsupported frame format, DR1 disabled.\n"));
      this->is_direct_rendering_disabled = 1;
    }

    /* FIXME: why should i have to do that ? */
    av_frame->data[0]= NULL;
    av_frame->data[1]= NULL;
    av_frame->data[2]= NULL;
    return avcodec_default_get_buffer(context, av_frame);
  }

  if((width != this->bih.biWidth) || (height != this->bih.biHeight)) {
    if(this->stream->video_out->get_capabilities(this->stream->video_out) & VO_CAP_CROP) {
      this->crop_right = width - this->bih.biWidth;
      this->crop_bottom = height - this->bih.biHeight;
    } else {
      if (!this->is_direct_rendering_disabled) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                _("ffmpeg_video_dec: unsupported frame dimensions, DR1 disabled.\n"));
        this->is_direct_rendering_disabled = 1;
      }
      /* FIXME: why should i have to do that ? */
      av_frame->data[0]= NULL;
      av_frame->data[1]= NULL;
      av_frame->data[2]= NULL;
      return avcodec_default_get_buffer(context, av_frame);
    }
  }

  img = this->stream->video_out->get_frame (this->stream->video_out,
                                            width,
                                            height,
                                            this->aspect_ratio,
                                            this->output_format,
                                            VO_BOTH_FIELDS|this->frame_flags);

  av_frame->opaque = img;

  av_frame->data[0]= img->base[0];
  av_frame->data[1]= img->base[1];
  av_frame->data[2]= img->base[2];

  av_frame->linesize[0] = img->pitches[0];
  av_frame->linesize[1] = img->pitches[1];
  av_frame->linesize[2] = img->pitches[2];

  /* We should really keep track of the ages of xine frames (see
   * avcodec_default_get_buffer in libavcodec/utils.c)
   * For the moment tell ffmpeg that every frame is new (age = bignumber) */
  av_frame->age = 256*256*256*64;

  av_frame->type= FF_BUFFER_TYPE_USER;

  /* take over pts for this frame to have it reordered */
  av_frame->reordered_opaque = context->reordered_opaque;

  xine_list_push_back(this->dr1_frames, av_frame);

  return 0;
}

static void release_buffer(struct AVCodecContext *context, AVFrame *av_frame){
  ff_video_decoder_t *this = (ff_video_decoder_t *)context->opaque;

  if (av_frame->type == FF_BUFFER_TYPE_USER) {
    if ( av_frame->opaque ) {
      vo_frame_t *img = (vo_frame_t *)av_frame->opaque;

      img->free(img);
    }

    xine_list_iterator_t it;

    it = xine_list_find(this->dr1_frames, av_frame);
    assert(it);
    if( it != NULL )
      xine_list_remove(this->dr1_frames, it);
  } else {
    avcodec_default_release_buffer(context, av_frame);
  }

  av_frame->opaque = NULL;
  av_frame->data[0]= NULL;
  av_frame->data[1]= NULL;
  av_frame->data[2]= NULL;
}
#endif

#include "ff_video_list.h"

static const char *const skip_loop_filter_enum_names[] = {
  "default", /* AVDISCARD_DEFAULT */
  "none",    /* AVDISCARD_NONE */
  "nonref",  /* AVDISCARD_NONREF */
  "bidir",   /* AVDISCARD_BIDIR */
  "nonkey",  /* AVDISCARD_NONKEY */
  "all",     /* AVDISCARD_ALL */
  NULL
};

static const int skip_loop_filter_enum_values[] = {
  AVDISCARD_DEFAULT,
  AVDISCARD_NONE,
  AVDISCARD_NONREF,
  AVDISCARD_BIDIR,
  AVDISCARD_NONKEY,
  AVDISCARD_ALL
};

static void init_video_codec (ff_video_decoder_t *this, unsigned int codec_type) {
  size_t i;

  /* find the decoder */
  this->codec = NULL;

  for(i = 0; i < sizeof(ff_video_lookup)/sizeof(ff_codec_t); i++)
    if(ff_video_lookup[i].type == codec_type) {
      pthread_mutex_lock(&ffmpeg_lock);
      this->codec = avcodec_find_decoder(ff_video_lookup[i].id);
      pthread_mutex_unlock(&ffmpeg_lock);
      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
                            ff_video_lookup[i].name);
      break;
    }

  if (!this->codec) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
            _("ffmpeg_video_dec: couldn't find ffmpeg decoder for buf type 0x%X\n"),
            codec_type);
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
    return;
  }

  lprintf("lavc decoder found\n");

  this->context->width = this->bih.biWidth;
  this->context->height = this->bih.biHeight;
  this->context->stream_codec_tag = this->context->codec_tag =
    _x_stream_info_get(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC);


  /* Some codecs (eg rv10) copy flags in init so it's necessary to set
   * this flag here in case we are going to use direct rendering */
  if(this->codec->capabilities & CODEC_CAP_DR1 && this->codec->id != CODEC_ID_H264) {
    this->context->flags |= CODEC_FLAG_EMU_EDGE;
  }

  /* TJ. without this, it wont work at all on my machine */
  this->context->codec_id = this->codec->id;
  this->context->codec_type = this->codec->type;

  if (this->class->choose_speed_over_accuracy)
    this->context->flags2 |= CODEC_FLAG2_FAST;

  pthread_mutex_lock(&ffmpeg_lock);
  if (avcodec_open (this->context, this->codec) < 0) {
    pthread_mutex_unlock(&ffmpeg_lock);
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             _("ffmpeg_video_dec: couldn't open decoder\n"));
    free(this->context);
    this->context = NULL;
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
    return;
  }

  if (this->codec->id == CODEC_ID_VC1 &&
      (!this->bih.biWidth || !this->bih.biHeight)) {
    /* VC1 codec must be re-opened with correct width and height. */
    avcodec_close(this->context);

    if (avcodec_open (this->context, this->codec) < 0) {
      pthread_mutex_unlock(&ffmpeg_lock);
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
	       _("ffmpeg_video_dec: couldn't open decoder (pass 2)\n"));
      free(this->context);
      this->context = NULL;
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
      return;
    }
  }

  if (this->class->thread_count > 1) {
    if (this->codec->id != CODEC_ID_SVQ3
#ifndef DEPRECATED_AVCODEC_THREAD_INIT
	&& avcodec_thread_init(this->context, this->class->thread_count) != -1
#endif
	)
      this->context->thread_count = this->class->thread_count;
  }

  this->context->skip_loop_filter = skip_loop_filter_enum_values[this->class->skip_loop_filter_enum];

  pthread_mutex_unlock(&ffmpeg_lock);

  lprintf("lavc decoder opened\n");

  this->decoder_ok = 1;

  if ((codec_type != BUF_VIDEO_MPEG) &&
      (codec_type != BUF_VIDEO_DV)) {

    if (!this->bih.biWidth || !this->bih.biHeight) {
      this->bih.biWidth = this->context->width;
      this->bih.biHeight = this->context->height;
    }


    set_stream_info(this);
  }

  (this->stream->video_out->open) (this->stream->video_out, this->stream);

  this->skipframes = 0;

  /* enable direct rendering by default */
  this->output_format = XINE_IMGFMT_YV12;
#ifdef ENABLE_DIRECT_RENDERING
  if( this->codec->capabilities & CODEC_CAP_DR1 && this->codec->id != CODEC_ID_H264 ) {
    this->context->get_buffer = get_buffer;
    this->context->release_buffer = release_buffer;
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("ffmpeg_video_dec: direct rendering enabled\n"));
  }
#endif

  /* flag for interlaced streams */
  this->frame_flags = 0;
  /* FIXME: which codecs can be interlaced?
      FIXME: check interlaced DCT and other codec specific info. */
  switch( codec_type ) {
    case BUF_VIDEO_DV:
      this->frame_flags |= VO_INTERLACED_FLAG;
      break;
    case BUF_VIDEO_MPEG:
      this->frame_flags |= VO_INTERLACED_FLAG;
      break;
    case BUF_VIDEO_MJPEG:
      this->frame_flags |= VO_INTERLACED_FLAG;
      break;
    case BUF_VIDEO_HUFFYUV:
      this->frame_flags |= VO_INTERLACED_FLAG;
      break;
    case BUF_VIDEO_H264:
      this->frame_flags |= VO_INTERLACED_FLAG;
      break;
  }

  /* dont want initial AV_NOPTS_VALUE here */
  this->context->reordered_opaque = 0;

}

static void choose_speed_over_accuracy_cb(void *user_data, xine_cfg_entry_t *entry) {
  ff_video_class_t   *class = (ff_video_class_t *) user_data;

  class->choose_speed_over_accuracy = entry->num_value;
}

static void skip_loop_filter_enum_cb(void *user_data, xine_cfg_entry_t *entry) {
  ff_video_class_t   *class = (ff_video_class_t *) user_data;

  class->skip_loop_filter_enum = entry->num_value;
}

static void thread_count_cb(void *user_data, xine_cfg_entry_t *entry) {
  ff_video_class_t   *class = (ff_video_class_t *) user_data;

  class->thread_count = entry->num_value;
}

static void pp_quality_cb(void *user_data, xine_cfg_entry_t *entry) {
  ff_video_class_t   *class = (ff_video_class_t *) user_data;

  class->pp_quality = entry->num_value;
}

static void pp_change_quality (ff_video_decoder_t *this) {
  this->pp_quality = this->class->pp_quality;

  if(this->pp_available && this->pp_quality) {
    if(!this->our_context && this->context)
      this->our_context = pp_get_context(this->context->width, this->context->height,
                                        this->pp_flags);
    if(this->our_mode)
      pp_free_mode(this->our_mode);

    this->our_mode = pp_get_mode_by_name_and_quality("hb:a,vb:a,dr:a",
                                                    this->pp_quality);
  } else {
    if(this->our_mode) {
      pp_free_mode(this->our_mode);
      this->our_mode = NULL;
    }

    if(this->our_context) {
      pp_free_context(this->our_context);
      this->our_context = NULL;
    }
  }
}

static void init_postprocess (ff_video_decoder_t *this) {
  uint32_t cpu_caps;

  /* Allow post processing on mpeg-4 (based) codecs */
  switch(this->codec->id) {
    case CODEC_ID_MPEG4:
    case CODEC_ID_MSMPEG4V1:
    case CODEC_ID_MSMPEG4V2:
    case CODEC_ID_MSMPEG4V3:
    case CODEC_ID_WMV1:
    case CODEC_ID_WMV2:
      this->pp_available = 1;
      break;
    default:
      this->pp_available = 0;
      break;
  }

  /* Detect what cpu accel we have */
  cpu_caps = xine_mm_accel();
  this->pp_flags = PP_FORMAT_420;

  if(cpu_caps & MM_ACCEL_X86_MMX)
    this->pp_flags |= PP_CPU_CAPS_MMX;

  if(cpu_caps & MM_ACCEL_X86_MMXEXT)
    this->pp_flags |= PP_CPU_CAPS_MMX2;

  if(cpu_caps & MM_ACCEL_X86_3DNOW)
    this->pp_flags |= PP_CPU_CAPS_3DNOW;

  /* Set level */
  pp_change_quality(this);
}

static int ff_handle_mpeg_sequence(ff_video_decoder_t *this, mpeg_parser_t *parser) {

  /* frame format change */
  if ((parser->width != this->bih.biWidth) ||
      (parser->height != this->bih.biHeight) ||
      (parser->frame_aspect_ratio != this->aspect_ratio)) {
    xine_event_t event;
    xine_format_change_data_t data;

    this->bih.biWidth  = parser->width;
    this->bih.biHeight = parser->height;
    this->aspect_ratio = parser->frame_aspect_ratio;
    this->aspect_ratio_prio = 2;
    lprintf("mpeg seq aspect ratio: %f\n", this->aspect_ratio);
    set_stream_info(this);

    event.type = XINE_EVENT_FRAME_FORMAT_CHANGE;
    event.stream = this->stream;
    event.data = &data;
    event.data_length = sizeof(data);
    data.width = this->bih.biWidth;
    data.height = this->bih.biHeight;
    data.aspect = this->aspect_ratio;
    data.pan_scan = 0;
    xine_event_send(this->stream, &event);
  }
  this->video_step = this->mpeg_parser->frame_duration;

  return 1;
}

static void ff_convert_frame(ff_video_decoder_t *this, vo_frame_t *img) {
  int         y;
  uint8_t    *dy, *du, *dv, *sy, *su, *sv;

#ifdef LOG
  if (this->debug_fmt != this->context->pix_fmt)
    printf ("frame format == %08x\n", this->debug_fmt = this->context->pix_fmt);
#endif

  dy = img->base[0];
  du = img->base[1];
  dv = img->base[2];
  sy = this->av_frame->data[0];
  su = this->av_frame->data[1];
  sv = this->av_frame->data[2];

  /* Some segfaults & heap corruption have been observed with img->height,
   * so we use this->bih.biHeight instead (which is the displayed height)
   */

  if (this->context->pix_fmt == PIX_FMT_YUV410P) {

    yuv9_to_yv12(
     /* Y */
      this->av_frame->data[0],
      this->av_frame->linesize[0],
      img->base[0],
      img->pitches[0],
     /* U */
      this->av_frame->data[1],
      this->av_frame->linesize[1],
      img->base[1],
      img->pitches[1],
     /* V */
      this->av_frame->data[2],
      this->av_frame->linesize[2],
      img->base[2],
      img->pitches[2],
     /* width x height */
      img->width,
      this->bih.biHeight);

  } else if (this->context->pix_fmt == PIX_FMT_YUV411P) {

    yuv411_to_yv12(
     /* Y */
      this->av_frame->data[0],
      this->av_frame->linesize[0],
      img->base[0],
      img->pitches[0],
     /* U */
      this->av_frame->data[1],
      this->av_frame->linesize[1],
      img->base[1],
      img->pitches[1],
     /* V */
      this->av_frame->data[2],
      this->av_frame->linesize[2],
      img->base[2],
      img->pitches[2],
     /* width x height */
      img->width,
      this->bih.biHeight);

  } else if (this->context->pix_fmt == PIX_FMT_RGB32) {

    int x, plane_ptr = 0;
    uint32_t *argb_pixels;
    uint32_t argb;

    for(y = 0; y < this->bih.biHeight; y++) {
      argb_pixels = (uint32_t *)sy;
      for(x = 0; x < img->width; x++) {
        uint8_t r, g, b;

        /* this is endian-safe as the ARGB pixels are stored in
         * machine order */
        argb = *argb_pixels++;
        r = (argb >> 16) & 0xFF;
        g = (argb >>  8) & 0xFF;
        b = (argb >>  0) & 0xFF;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }

    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);

  } else if (this->context->pix_fmt == PIX_FMT_RGB565) {

    int x, plane_ptr = 0;
    uint8_t *src;
    uint16_t pixel16;

    for(y = 0; y < this->bih.biHeight; y++) {
      src = sy;
      for(x = 0; x < img->width; x++) {
        uint8_t r, g, b;

        /* a 16-bit RGB565 pixel is supposed to be stored in native-endian
         * byte order; the following should be endian-safe */
        pixel16 = *((uint16_t *)src);
        src += 2;
        b = (pixel16 << 3) & 0xFF;
        g = (pixel16 >> 3) & 0xFF;
        r = (pixel16 >> 8) & 0xFF;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }

    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);

  } else if (this->context->pix_fmt == PIX_FMT_RGB555) {

    int x, plane_ptr = 0;
    uint8_t *src;
    uint16_t pixel16;

    for(y = 0; y < this->bih.biHeight; y++) {
      src = sy;
      for(x = 0; x < img->width; x++) {
        uint8_t r, g, b;

        /* a 16-bit RGB555 pixel is supposed to be stored in native-endian
         * byte order; the following should be endian-safe */
        pixel16 = *((uint16_t *)src);
        src += 2;
        b = (pixel16 << 3) & 0xFF;
        g = (pixel16 >> 2) & 0xFF;
        r = (pixel16 >> 7) & 0xFF;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }

    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);

  } else if (this->context->pix_fmt == PIX_FMT_BGR24) {

    int x, plane_ptr = 0;
    uint8_t *src;

    for(y = 0; y < this->bih.biHeight; y++) {
      src = sy;
      for(x = 0; x < img->width; x++) {
        uint8_t r, g, b;

        b = *src++;
        g = *src++;
        r = *src++;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }

    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);

  } else if (this->context->pix_fmt == PIX_FMT_RGB24) {

    int x, plane_ptr = 0;
    uint8_t *src;

    for(y = 0; y < this->bih.biHeight; y++) {
      src = sy;
      for(x = 0; x < img->width; x++) {
        uint8_t r, g, b;

        r = *src++;
        g = *src++;
        b = *src++;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }

    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);

  } else if (this->context->pix_fmt == PIX_FMT_PAL8) {

    int x, plane_ptr = 0;
    uint8_t *src;
    uint8_t pixel;
    uint32_t *palette32 = (uint32_t *)su;  /* palette is in data[1] */
    uint32_t rgb_color;
    uint8_t r, g, b;
    uint8_t y_palette[256];
    uint8_t u_palette[256];
    uint8_t v_palette[256];

    for (x = 0; x < 256; x++) {
      rgb_color = palette32[x];
      b = rgb_color & 0xFF;
      rgb_color >>= 8;
      g = rgb_color & 0xFF;
      rgb_color >>= 8;
      r = rgb_color & 0xFF;
      y_palette[x] = COMPUTE_Y(r, g, b);
      u_palette[x] = COMPUTE_U(r, g, b);
      v_palette[x] = COMPUTE_V(r, g, b);
    }

    for(y = 0; y < this->bih.biHeight; y++) {
      src = sy;
      for(x = 0; x < img->width; x++) {
        pixel = *src++;

        this->yuv.y[plane_ptr] = y_palette[pixel];
        this->yuv.u[plane_ptr] = u_palette[pixel];
        this->yuv.v[plane_ptr] = v_palette[pixel];
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }

    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);

  } else {

    for (y = 0; y < this->bih.biHeight; y++) {
      xine_fast_memcpy (dy, sy, img->width);

      dy += img->pitches[0];

      sy += this->av_frame->linesize[0];
    }

    for (y = 0; y < this->bih.biHeight / 2; y++) {

      if (this->context->pix_fmt != PIX_FMT_YUV444P) {

        xine_fast_memcpy (du, su, img->width/2);
        xine_fast_memcpy (dv, sv, img->width/2);

      } else {

        int x;
        uint8_t *src;
        uint8_t *dst;

        /* subsample */

        src = su; dst = du;
        for (x=0; x<(img->width/2); x++) {
          *dst = *src;
          dst++;
          src += 2;
        }
        src = sv; dst = dv;
        for (x=0; x<(img->width/2); x++) {
          *dst = *src;
          dst++;
          src += 2;
        }

      }

      du += img->pitches[1];
      dv += img->pitches[2];

      if (this->context->pix_fmt != PIX_FMT_YUV420P) {
        su += 2*this->av_frame->linesize[1];
        sv += 2*this->av_frame->linesize[2];
      } else {
        su += this->av_frame->linesize[1];
        sv += this->av_frame->linesize[2];
      }
    }
  }
}

static void ff_check_bufsize (ff_video_decoder_t *this, int size) {
  if (size > this->bufsize) {
    this->bufsize = size + size / 2;
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("ffmpeg_video_dec: increasing buffer to %d to avoid overflow.\n"),
	    this->bufsize);
    this->buf = realloc(this->buf, this->bufsize + FF_INPUT_BUFFER_PADDING_SIZE );
  }
}

static int ff_vc1_find_header(ff_video_decoder_t *this, buf_element_t *buf)
{
  uint8_t *p = buf->content;

  if (!p[0] && !p[1] && p[2] == 1 && p[3] == 0x0f) {
    int i;

    this->context->extradata = calloc(1, buf->size);
    this->context->extradata_size = 0;

    for (i = 0; i < buf->size && i < 128; i++) {
      if (!p[i] && !p[i+1] && p[i+2]) {
	lprintf("00 00 01 %02x at %d\n", p[i+3], i);
	if (p[i+3] != 0x0e && p[i+3] != 0x0f)
	  break;
      }
      this->context->extradata[i] = p[i];
      this->context->extradata_size++;
    }

    lprintf("ff_video_decoder: found VC1 sequence header\n");
    return 1;
  }

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	  "ffmpeg_video_dec: VC1 extradata missing !\n");
  return 0;
}

static int ff_check_extradata(ff_video_decoder_t *this, unsigned int codec_type, buf_element_t *buf)
{
  if (this->context && this->context->extradata)
    return 1;

  switch (codec_type) {
  case BUF_VIDEO_VC1:
    return ff_vc1_find_header(this, buf);
  default:;
  }

  return 1;
}

static void ff_init_mpeg12_mode(ff_video_decoder_t *this)
{
  this->is_mpeg12 = 1;

  if (this->decoder_init_mode) {
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_VIDEOCODEC,
                          "mpeg-1 (ffmpeg)");

    init_video_codec (this, BUF_VIDEO_MPEG);
    this->decoder_init_mode = 0;
  }

  if ( this->mpeg_parser == NULL ) {
    this->mpeg_parser = calloc(1, sizeof(mpeg_parser_t));
    mpeg_parser_init(this->mpeg_parser);
  }
}

static void ff_handle_preview_buffer (ff_video_decoder_t *this, buf_element_t *buf) {
  int codec_type;

  lprintf ("preview buffer\n");

  codec_type = buf->type & 0xFFFF0000;
  if (codec_type == BUF_VIDEO_MPEG) {
    ff_init_mpeg12_mode(this);
  }

  if (this->decoder_init_mode && !this->is_mpeg12) {

    if (!ff_check_extradata(this, codec_type, buf))
      return;

    init_video_codec(this, codec_type);
    init_postprocess(this);
    this->decoder_init_mode = 0;
  }
}

static void ff_handle_header_buffer (ff_video_decoder_t *this, buf_element_t *buf) {

  lprintf ("header buffer\n");

  /* accumulate data */
  ff_check_bufsize(this, this->size + buf->size);
  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;

  if (buf->decoder_flags & BUF_FLAG_FRAME_END) {
    int codec_type;

    lprintf ("header complete\n");
    codec_type = buf->type & 0xFFFF0000;

    if (buf->decoder_flags & BUF_FLAG_STDHEADER) {

      lprintf("standard header\n");

      /* init package containing bih */
      memcpy ( &this->bih, this->buf, sizeof(xine_bmiheader) );

      if (this->bih.biSize > sizeof(xine_bmiheader)) {
      this->context->extradata_size = this->bih.biSize - sizeof(xine_bmiheader);
        this->context->extradata = malloc(this->context->extradata_size +
                                          FF_INPUT_BUFFER_PADDING_SIZE);
        memcpy(this->context->extradata, this->buf + sizeof(xine_bmiheader),
              this->context->extradata_size);
      }

      this->context->bits_per_sample = this->bih.biBitCount;

    } else {

      switch (codec_type) {
      case BUF_VIDEO_RV10:
      case BUF_VIDEO_RV20:
      case BUF_VIDEO_RV30:
      case BUF_VIDEO_RV40:
        this->bih.biWidth  = _X_BE_16(&this->buf[12]);
        this->bih.biHeight = _X_BE_16(&this->buf[14]);

        this->context->sub_id = _X_BE_32(&this->buf[30]);

        this->context->slice_offset = calloc(SLICE_OFFSET_SIZE, sizeof(int));
        this->slice_offset_size = SLICE_OFFSET_SIZE;

        this->context->extradata_size = this->size - 26;
	if (this->context->extradata_size < 8) {
	  this->context->extradata_size= 8;
	  this->context->extradata = malloc(this->context->extradata_size +
		                            FF_INPUT_BUFFER_PADDING_SIZE);
          ((uint32_t *)this->context->extradata)[0] = 0;
	  if (codec_type == BUF_VIDEO_RV10)
	     ((uint32_t *)this->context->extradata)[1] = 0x10000000;
	  else
	     ((uint32_t *)this->context->extradata)[1] = 0x10003001;
	} else {
          this->context->extradata = malloc(this->context->extradata_size +
	                                    FF_INPUT_BUFFER_PADDING_SIZE);
	  memcpy(this->context->extradata, this->buf + 26,
	         this->context->extradata_size);
	}

	xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "ffmpeg_video_dec: buf size %d\n", this->size);

	lprintf("w=%d, h=%d\n", this->bih.biWidth, this->bih.biHeight);

        break;
      default:
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                "ffmpeg_video_dec: unknown header for buf type 0x%X\n", codec_type);
        return;
      }
    }

    /* reset accumulator */
    this->size = 0;
  }
}

static void ff_handle_special_buffer (ff_video_decoder_t *this, buf_element_t *buf) {
  /* take care of all the various types of special buffers
  * note that order is important here */
  lprintf("special buffer\n");

  if (buf->decoder_info[1] == BUF_SPECIAL_STSD_ATOM &&
      !this->context->extradata_size) {

    lprintf("BUF_SPECIAL_STSD_ATOM\n");
    this->context->extradata_size = buf->decoder_info[2];
    this->context->extradata = malloc(buf->decoder_info[2] +
				      FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(this->context->extradata, buf->decoder_info_ptr[2],
      buf->decoder_info[2]);

  } else if (buf->decoder_info[1] == BUF_SPECIAL_DECODER_CONFIG &&
            !this->context->extradata_size) {

    lprintf("BUF_SPECIAL_DECODER_CONFIG\n");
    this->context->extradata_size = buf->decoder_info[2];
    this->context->extradata = malloc(buf->decoder_info[2] +
				      FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(this->context->extradata, buf->decoder_info_ptr[2],
      buf->decoder_info[2]);

  } else if (buf->decoder_info[1] == BUF_SPECIAL_PALETTE) {
    unsigned int i;

    palette_entry_t *demuxer_palette;
    AVPaletteControl *decoder_palette;

    lprintf("BUF_SPECIAL_PALETTE\n");
    this->context->palctrl = &this->palette_control;
    decoder_palette = (AVPaletteControl *)this->context->palctrl;
    demuxer_palette = (palette_entry_t *)buf->decoder_info_ptr[2];

    for (i = 0; i < buf->decoder_info[2]; i++) {
      decoder_palette->palette[i] =
        (demuxer_palette[i].r << 16) |
        (demuxer_palette[i].g <<  8) |
        (demuxer_palette[i].b <<  0);
    }
    decoder_palette->palette_changed = 1;

  } else if (buf->decoder_info[1] == BUF_SPECIAL_RV_CHUNK_TABLE) {
    int i;

    lprintf("BUF_SPECIAL_RV_CHUNK_TABLE\n");
    this->context->slice_count = buf->decoder_info[2]+1;

    lprintf("slice_count=%d\n", this->context->slice_count);

    if(this->context->slice_count > this->slice_offset_size) {
      this->context->slice_offset = realloc(this->context->slice_offset,
                                            sizeof(int)*this->context->slice_count);
      this->slice_offset_size = this->context->slice_count;
    }

    for(i = 0; i < this->context->slice_count; i++) {
      this->context->slice_offset[i] =
        ((uint32_t *) buf->decoder_info_ptr[2])[(2*i)+1];
      lprintf("slice_offset[%d]=%d\n", i, this->context->slice_offset[i]);
    }
  }
}

static uint64_t ff_tag_pts(ff_video_decoder_t *this, uint64_t pts)
{
  return pts | this->pts_tag;
}

static uint64_t ff_untag_pts(ff_video_decoder_t *this, uint64_t pts)
{
  if (this->pts_tag_mask == 0)
    return pts; /* pts tagging inactive */

  if (this->pts_tag != 0 && (pts & this->pts_tag_mask) != this->pts_tag)
    return 0; /* reset pts if outdated while waiting for first pass (see below) */

  return pts & ~this->pts_tag_mask;
}

static void ff_check_pts_tagging(ff_video_decoder_t *this, uint64_t pts)
{
  if (this->pts_tag_mask == 0)
    return; /* pts tagging inactive */
  if ((pts & this->pts_tag_mask) != this->pts_tag) {
    this->pts_tag_stable_counter = 0;
    return; /* pts still outdated */
  }

  /* the tag should be stable for 100 frames */
  this->pts_tag_stable_counter++;

  if (this->pts_tag != 0) {
    if (this->pts_tag_stable_counter >= 100) {
      /* first pass: reset pts_tag */
      this->pts_tag = 0;
      this->pts_tag_stable_counter = 0;
    }
  } else if (pts == 0)
    return; /* cannot detect second pass */
  else {
    if (this->pts_tag_stable_counter >= 100) {
      /* second pass: reset pts_tag_mask and pts_tag_counter */
      this->pts_tag_mask = 0;
      this->pts_tag_counter = 0;
      this->pts_tag_stable_counter = 0;
    }
  }
}

static void ff_handle_mpeg12_buffer (ff_video_decoder_t *this, buf_element_t *buf) {

  vo_frame_t *img;
  int         free_img;
  int         got_picture, len;
  int         offset = 0;
  int         flush = 0;
  int         size = buf->size;

  lprintf("handle_mpeg12_buffer\n");

  if (!this->is_mpeg12) {
    /* initialize mpeg parser */
    ff_init_mpeg12_mode(this);
  }

  while ((size > 0) || (flush == 1)) {

    uint8_t *current;
    int next_flush;

    /* apply valid pts to first frame _starting_ thereafter only */
    if (this->pts && !this->context->reordered_opaque) {
      this->context->reordered_opaque = 
      this->av_frame->reordered_opaque = ff_tag_pts (this, this->pts);
      this->pts = 0;
    }

    got_picture = 0;
    if (!flush) {
      current = mpeg_parser_decode_data(this->mpeg_parser,
                                        buf->content + offset, buf->content + offset + size,
                                        &next_flush);
    } else {
      current = buf->content + offset + size; /* end of the buffer */
      next_flush = 0;
    }
    if (current == NULL) {
      lprintf("current == NULL\n");
      return;
    }

    if (this->mpeg_parser->has_sequence) {
      ff_handle_mpeg_sequence(this, this->mpeg_parser);
    }

    if (!this->decoder_ok)
      return;

    if (flush) {
      lprintf("flush lavc buffers\n");
      /* hack: ffmpeg outputs the last frame if size=0 */
      this->mpeg_parser->buffer_size = 0;
    }

    /* skip decoding b frames if too late */
#if AVVIDEO > 1
    this->context->skip_frame = (this->skipframes > 0) ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
#else
    this->context->hurry_up = (this->skipframes > 0);
#endif

    lprintf("avcodec_decode_video: size=%d\n", this->mpeg_parser->buffer_size);
#if AVVIDEO > 1
    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = (uint8_t *)this->mpeg_parser->chunk_buffer;
    avpkt.size = this->mpeg_parser->buffer_size;
    avpkt.flags = AV_PKT_FLAG_KEY;
    len = avcodec_decode_video2 (this->context, this->av_frame,
				 &got_picture, &avpkt);
#else
    len = avcodec_decode_video (this->context, this->av_frame,
                                &got_picture, this->mpeg_parser->chunk_buffer,
                                this->mpeg_parser->buffer_size);
#endif
    lprintf("avcodec_decode_video: decoded_size=%d, got_picture=%d\n",
            len, got_picture);
    len = current - buf->content - offset;
    lprintf("avcodec_decode_video: consumed_size=%d\n", len);

    flush = next_flush;

    if ((len < 0) || (len > buf->size)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                "ffmpeg_video_dec: error decompressing frame\n");
      size = 0; /* draw a bad frame and exit */
    } else {
      size -= len;
      offset += len;
    }

    if (got_picture && this->av_frame->data[0]) {
      /* got a picture, draw it */
      if(!this->av_frame->opaque) {
        /* indirect rendering */
        img = this->stream->video_out->get_frame (this->stream->video_out,
                                                  this->bih.biWidth,
                                                  this->bih.biHeight,
                                                  this->aspect_ratio,
                                                  this->output_format,
                                                  VO_BOTH_FIELDS|this->frame_flags);
        free_img = 1;
      } else {
        /* DR1 */
        img = (vo_frame_t*) this->av_frame->opaque;
        free_img = 0;
      }

      /* get back reordered pts */
      img->pts = ff_untag_pts (this, this->av_frame->reordered_opaque);
      ff_check_pts_tagging (this, this->av_frame->reordered_opaque);
      this->av_frame->reordered_opaque = 0;
      this->context->reordered_opaque = 0;

      if (this->av_frame->repeat_pict)
        img->duration = this->video_step * 3 / 2;
      else
        img->duration = this->video_step;

      img->crop_right  = this->crop_right;
      img->crop_bottom = this->crop_bottom;

      this->skipframes = img->draw(img, this->stream);

      if(free_img)
        img->free(img);

    } else {

      if (
#if AVVIDEO > 1
	  this->context->skip_frame != AVDISCARD_DEFAULT
#else
	  this->context->hurry_up
#endif
	 ) {
        /* skipped frame, output a bad frame */
        img = this->stream->video_out->get_frame (this->stream->video_out,
                                                  this->bih.biWidth,
                                                  this->bih.biHeight,
                                                  this->aspect_ratio,
                                                  this->output_format,
                                                  VO_BOTH_FIELDS|this->frame_flags);
        img->pts       = 0;
        img->duration  = this->video_step;
        img->bad_frame = 1;
        this->skipframes = img->draw(img, this->stream);
        img->free(img);
      }
    }
  }
}

static void ff_handle_buffer (ff_video_decoder_t *this, buf_element_t *buf) {
  uint8_t *chunk_buf = this->buf;
  AVRational avr00 = {0, 1};

  lprintf("handle_buffer\n");

  if (!this->decoder_ok) {
    if (this->decoder_init_mode) {
      int codec_type = buf->type & (BUF_MAJOR_MASK | BUF_DECODER_MASK);

      if (!ff_check_extradata(this, codec_type, buf))
	return;

      /* init ffmpeg decoder */
      init_video_codec(this, codec_type);
      init_postprocess(this);
      this->decoder_init_mode = 0;
    } else {
      return;
    }
  }

  if (buf->decoder_flags & BUF_FLAG_FRAME_START) {
    lprintf("BUF_FLAG_FRAME_START\n");
    this->size = 0;
  }

  if (this->size == 0) {
    /* take over pts when we are about to buffer a frame */
    this->av_frame->reordered_opaque = ff_tag_pts(this, this->pts);
    if (this->context) /* shouldn't be NULL */
      this->context->reordered_opaque = ff_tag_pts(this, this->pts);
    this->pts = 0;
  }

  /* data accumulation */
  if (buf->size > 0) {
    if ((this->size == 0) &&
	((buf->size + FF_INPUT_BUFFER_PADDING_SIZE) < buf->max_size) &&
	(buf->decoder_flags & BUF_FLAG_FRAME_END)) {
      /* buf contains a complete frame */
      /* no memcpy needed */
      chunk_buf = buf->content;
      this->size = buf->size;
      lprintf("no memcpy needed to accumulate data\n");
    } else {
      /* copy data into our internal buffer */
      ff_check_bufsize(this, this->size + buf->size);
      chunk_buf = this->buf; /* ff_check_bufsize might realloc this->buf */

      xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

      this->size += buf->size;
      lprintf("accumulate data into this->buf\n");
    }
  }

  if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

    vo_frame_t *img;
    int         free_img;
    int         got_picture, len;
    int         got_one_picture = 0;
    int         offset = 0;
    int         codec_type = buf->type & 0xFFFF0000;
    int         video_step_to_use = this->video_step;

    /* pad input data */
    /* note: bitstream, alt bitstream reader or something will cause
     * severe mpeg4 artifacts if padding is less than 32 bits.
     */
    memset(&chunk_buf[this->size], 0, FF_INPUT_BUFFER_PADDING_SIZE);

    while (this->size > 0) {

      /* DV frames can be completely skipped */
      if( codec_type == BUF_VIDEO_DV && this->skipframes ) {
        this->size = 0;
        got_picture = 0;
      } else {
        /* skip decoding b frames if too late */
#if AVVIDEO > 1
	this->context->skip_frame = (this->skipframes > 0) ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
#else
        this->context->hurry_up = (this->skipframes > 0);
#endif
        lprintf("buffer size: %d\n", this->size);
#if AVVIDEO > 1
	AVPacket avpkt;
	av_init_packet(&avpkt);
	avpkt.data = (uint8_t *)&chunk_buf[offset];
	avpkt.size = this->size;
	avpkt.flags = AV_PKT_FLAG_KEY;
	len = avcodec_decode_video2 (this->context, this->av_frame,
				     &got_picture, &avpkt);
#else
        len = avcodec_decode_video (this->context, this->av_frame,
                                    &got_picture, &chunk_buf[offset],
                                    this->size);
#endif
        /* reset consumed pts value */
        this->context->reordered_opaque = ff_tag_pts(this, 0);

        lprintf("consumed size: %d, got_picture: %d\n", len, got_picture);
        if ((len <= 0) || (len > this->size)) {
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                    "ffmpeg_video_dec: error decompressing frame\n");
          this->size = 0;

        } else {

          offset += len;
          this->size -= len;

          if (this->size > 0) {
            ff_check_bufsize(this, this->size);
            memmove (this->buf, &chunk_buf[offset], this->size);
            chunk_buf = this->buf;

            /* take over pts for next access unit */
            this->av_frame->reordered_opaque = ff_tag_pts(this, this->pts);
            this->context->reordered_opaque = ff_tag_pts(this, this->pts);
            this->pts = 0;
          }
        }
      }

      /* use externally provided video_step or fall back to stream's time_base otherwise */
      video_step_to_use = (this->video_step || !this->context->time_base.den)
                        ? this->video_step
                        : (int)(90000ll
                                * this->context->ticks_per_frame
                                * this->context->time_base.num / this->context->time_base.den);

      /* aspect ratio provided by ffmpeg, override previous setting */
      if ((this->aspect_ratio_prio < 2) &&
	  av_cmp_q(this->context->sample_aspect_ratio, avr00)) {

        if (!this->bih.biWidth || !this->bih.biHeight) {
          this->bih.biWidth  = this->context->width;
          this->bih.biHeight = this->context->height;
        }

	this->aspect_ratio = av_q2d(this->context->sample_aspect_ratio) *
	  (double)this->bih.biWidth / (double)this->bih.biHeight;
	this->aspect_ratio_prio = 2;
	lprintf("ffmpeg aspect ratio: %f\n", this->aspect_ratio);
	set_stream_info(this);
      }

      if (got_picture && this->av_frame->data[0]) {
        /* got a picture, draw it */
        got_one_picture = 1;
        if(!this->av_frame->opaque) {
	  /* indirect rendering */

	  /* initialize the colorspace converter */
	  if (!this->cs_convert_init) {
	    if ((this->context->pix_fmt == PIX_FMT_RGB32) ||
	        (this->context->pix_fmt == PIX_FMT_RGB565) ||
	        (this->context->pix_fmt == PIX_FMT_RGB555) ||
	        (this->context->pix_fmt == PIX_FMT_BGR24) ||
	        (this->context->pix_fmt == PIX_FMT_RGB24) ||
	        (this->context->pix_fmt == PIX_FMT_PAL8)) {
	      this->output_format = XINE_IMGFMT_YUY2;
	      init_yuv_planes(&this->yuv, (this->bih.biWidth + 15) & ~15, this->bih.biHeight);
	      this->yuv_init = 1;
	    }
	    this->cs_convert_init = 1;
	  }

	  if (this->aspect_ratio_prio == 0) {
	    this->aspect_ratio = (double)this->bih.biWidth / (double)this->bih.biHeight;
	    this->aspect_ratio_prio = 1;
	    lprintf("default aspect ratio: %f\n", this->aspect_ratio);
	    set_stream_info(this);
	  }

          /* xine-lib expects the framesize to be a multiple of 16x16 (macroblock) */
          img = this->stream->video_out->get_frame (this->stream->video_out,
                                                    (this->bih.biWidth  + 15) & ~15,
                                                    (this->bih.biHeight + 15) & ~15,
                                                    this->aspect_ratio,
                                                    this->output_format,
                                                    VO_BOTH_FIELDS|this->frame_flags);
          free_img = 1;
        } else {
          /* DR1 */
          img = (vo_frame_t*) this->av_frame->opaque;
          free_img = 0;
        }

        /* post processing */
        if(this->pp_quality != this->class->pp_quality)
          pp_change_quality(this);

        if(this->pp_available && this->pp_quality) {

          if(this->av_frame->opaque) {
            /* DR1 */
            img = this->stream->video_out->get_frame (this->stream->video_out,
                                                      (img->width  + 15) & ~15,
                                                      (img->height + 15) & ~15,
                                                      this->aspect_ratio,
                                                      this->output_format,
                                                      VO_BOTH_FIELDS|this->frame_flags);
            free_img = 1;
          }

          pp_postprocess(this->av_frame->data, this->av_frame->linesize,
                        img->base, img->pitches,
                        img->width, img->height,
                        this->av_frame->qscale_table, this->av_frame->qstride,
                        this->our_mode, this->our_context,
                        this->av_frame->pict_type);

        } else if (!this->av_frame->opaque) {
	  /* colorspace conversion or copy */
          ff_convert_frame(this, img);
        }

        img->pts  = ff_untag_pts(this, this->av_frame->reordered_opaque);
        ff_check_pts_tagging(this, this->av_frame->reordered_opaque); /* only check for valid frames */
        this->av_frame->reordered_opaque = 0;

        /* workaround for weird 120fps streams */
        if( video_step_to_use == 750 ) {
          /* fallback to the VIDEO_PTS_MODE */
          video_step_to_use = 0;
        }

        if (video_step_to_use && video_step_to_use != this->reported_video_step)
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = video_step_to_use));

        if (this->av_frame->repeat_pict)
          img->duration = video_step_to_use * 3 / 2;
        else
          img->duration = video_step_to_use;

        /* additionally crop away the extra pixels due to adjusting frame size above */
        img->crop_right  = this->crop_right  + (img->width  - this->bih.biWidth);
        img->crop_bottom = this->crop_bottom + (img->height - this->bih.biHeight);

        /* transfer some more frame settings for deinterlacing */
        img->progressive_frame = !this->av_frame->interlaced_frame;
        img->top_field_first   = this->av_frame->top_field_first;

        this->skipframes = img->draw(img, this->stream);

        if(free_img)
          img->free(img);
      }
    }

    /* workaround for demux_mpeg_pes sending fields as frames:
     * do not generate a bad frame for the first field picture
     */
    if (!got_one_picture && (this->size || this->video_step || this->assume_bad_field_picture)) {
      /* skipped frame, output a bad frame (use size 16x16, when size still uninitialized) */
      img = this->stream->video_out->get_frame (this->stream->video_out,
                                                (this->bih.biWidth  <= 0) ? 16 : ((this->bih.biWidth  + 15) & ~15),
                                                (this->bih.biHeight <= 0) ? 16 : ((this->bih.biHeight + 15) & ~15),
                                                this->aspect_ratio,
                                                this->output_format,
                                                VO_BOTH_FIELDS|this->frame_flags);
      /* set PTS to allow early syncing */
      img->pts       = ff_untag_pts(this, this->av_frame->reordered_opaque);
      this->av_frame->reordered_opaque = 0;

      img->duration  = video_step_to_use;

      /* additionally crop away the extra pixels due to adjusting frame size above */
      img->crop_right  = ((this->bih.biWidth  <= 0) ? 0 : this->crop_right)  + (img->width  - this->bih.biWidth);
      img->crop_bottom = ((this->bih.biHeight <= 0) ? 0 : this->crop_bottom) + (img->height - this->bih.biHeight);

      img->bad_frame = 1;
      this->skipframes = img->draw(img, this->stream);
      img->free(img);
    }

    this->assume_bad_field_picture = !got_one_picture;
  }
}

static void ff_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  ff_video_decoder_t *this = (ff_video_decoder_t *) this_gen;

  lprintf ("processing packet type = %08x, len = %d, decoder_flags=%08x\n",
           buf->type, buf->size, buf->decoder_flags);

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = this->video_step));
  }

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {

    ff_handle_preview_buffer(this, buf);

  } else {

    if (buf->decoder_flags & BUF_FLAG_SPECIAL) {

      ff_handle_special_buffer(this, buf);

    }

    if (buf->decoder_flags & BUF_FLAG_HEADER) {

      ff_handle_header_buffer(this, buf);

      if (buf->decoder_flags & BUF_FLAG_ASPECT) {
	if (this->aspect_ratio_prio < 3) {
	  this->aspect_ratio = (double)buf->decoder_info[1] / (double)buf->decoder_info[2];
	  this->aspect_ratio_prio = 3;
	  lprintf("aspect ratio: %f\n", this->aspect_ratio);
	  set_stream_info(this);
	}
      }

    } else {
      if (this->decoder_init_mode && !this->is_mpeg12)
        ff_handle_preview_buffer(this, buf);

      /* decode */
      /* PES: each valid pts shall be used only once */
      if (buf->pts && (buf->pts != this->last_pts))
	this->last_pts = this->pts = buf->pts;

      if ((buf->type & 0xFFFF0000) == BUF_VIDEO_MPEG) {
	ff_handle_mpeg12_buffer(this, buf);
      } else {
	ff_handle_buffer(this, buf);
      }

    }
  }
}

static void ff_flush (video_decoder_t *this_gen) {
  lprintf ("ff_flush\n");
}

static void ff_reset (video_decoder_t *this_gen) {
  ff_video_decoder_t *this = (ff_video_decoder_t *) this_gen;

  lprintf ("ff_reset\n");

  this->size = 0;

  if(this->context && this->decoder_ok)
    avcodec_flush_buffers(this->context);

  if (this->is_mpeg12)
    mpeg_parser_reset(this->mpeg_parser);

  this->pts_tag_mask = 0;
  this->pts_tag = 0;
  this->pts_tag_counter = 0;
  this->pts_tag_stable_counter = 0;
}

static void ff_discontinuity (video_decoder_t *this_gen) {
  ff_video_decoder_t *this = (ff_video_decoder_t *) this_gen;

  lprintf ("ff_discontinuity\n");
  this->pts = 0;

  /*
   * there is currently no way to reset all the pts which are stored in the decoder.
   * therefore, we add a unique tag (generated from pts_tag_counter) to pts (see
   * ff_tag_pts()) and wait for it to appear on returned frames.
   * until then, any retrieved pts value will be reset to 0 (see ff_untag_pts()).
   * when we see the tag returned, pts_tag will be reset to 0. from now on, any
   * untagged pts value is valid already.
   * when tag 0 appears too, there are no tags left in the decoder so pts_tag_mask
   * and pts_tag_counter will be reset to 0 too (see ff_check_pts_tagging()).
   */
  this->pts_tag_counter++;
  this->pts_tag_mask = 0;
  this->pts_tag = 0;
  this->pts_tag_stable_counter = 0;
  {
    /* pts values typically don't use the uppermost bits. therefore we put the tag there */
    int counter_mask = 1;
    int counter = 2 * this->pts_tag_counter + 1; /* always set the uppermost bit in tag_mask */
    uint64_t tag_mask = 0x8000000000000000ull;
    while (this->pts_tag_counter >= counter_mask)
    {
      /*
       * mirror the counter into the uppermost bits. this allows us to enlarge mask as
       * necessary and while previous taggings can still be detected to be outdated.
       */
      if (counter & counter_mask)
        this->pts_tag |= tag_mask;
      this->pts_tag_mask |= tag_mask;
      tag_mask >>= 1;
      counter_mask <<= 1;
    }
  }
}

static void ff_dispose (video_decoder_t *this_gen) {
  ff_video_decoder_t *this = (ff_video_decoder_t *) this_gen;

  lprintf ("ff_dispose\n");

  if (this->decoder_ok) {
    xine_list_iterator_t it;
    AVFrame *av_frame;

    pthread_mutex_lock(&ffmpeg_lock);
    avcodec_close (this->context);
    pthread_mutex_unlock(&ffmpeg_lock);

    /* frame garbage collector here - workaround for buggy ffmpeg codecs that
     * don't release their DR1 frames */
    while( (it = xine_list_front(this->dr1_frames)) != NULL )
    {
      av_frame = (AVFrame *)xine_list_get_value(this->dr1_frames, it);
      release_buffer(this->context, av_frame);
    }

    this->stream->video_out->close(this->stream->video_out, this->stream);
    this->decoder_ok = 0;
  }

  if(this->context && this->context->slice_offset)
    free(this->context->slice_offset);

  if(this->context && this->context->extradata)
    free(this->context->extradata);

  if(this->yuv_init)
    free_yuv_planes(&this->yuv);

  if( this->context )
    av_free( this->context );

  if( this->av_frame )
    av_free( this->av_frame );

  if (this->buf)
    free(this->buf);
  this->buf = NULL;

  if(this->our_context)
    pp_free_context(this->our_context);

  if(this->our_mode)
    pp_free_mode(this->our_mode);

  mpeg_parser_dispose(this->mpeg_parser);

  xine_list_delete(this->dr1_frames);

  free (this_gen);
}

static video_decoder_t *ff_video_open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  ff_video_decoder_t  *this ;

  lprintf ("open_plugin\n");

  this = calloc(1, sizeof (ff_video_decoder_t));

  this->video_decoder.decode_data         = ff_decode_data;
  this->video_decoder.flush               = ff_flush;
  this->video_decoder.reset               = ff_reset;
  this->video_decoder.discontinuity       = ff_discontinuity;
  this->video_decoder.dispose             = ff_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (ff_video_class_t *) class_gen;

  this->av_frame          = avcodec_alloc_frame();
  this->context           = avcodec_alloc_context();
  this->context->opaque   = this;
  this->context->palctrl  = NULL;

  this->decoder_ok        = 0;
  this->decoder_init_mode = 1;
  this->buf               = calloc(1, VIDEOBUFSIZE + FF_INPUT_BUFFER_PADDING_SIZE);
  this->bufsize           = VIDEOBUFSIZE;

  this->is_mpeg12         = 0;
  this->aspect_ratio      = 0;

  this->pp_quality        = 0;
  this->our_context       = NULL;
  this->our_mode          = NULL;

  this->mpeg_parser       = NULL;

  this->dr1_frames        = xine_list_new();

#ifdef LOG
  this->debug_fmt = -1;
#endif

  return &this->video_decoder;
}

void *init_video_plugin (xine_t *xine, void *data) {

  ff_video_class_t *this;
  config_values_t  *config;

  this = calloc(1, sizeof (ff_video_class_t));

  this->decoder_class.open_plugin     = ff_video_open_plugin;
  this->decoder_class.identifier      = "ffmpeg video";
  this->decoder_class.description     = N_("ffmpeg based video decoder plugin");
  this->decoder_class.dispose         = default_video_decoder_class_dispose;
  this->xine                          = xine;

  pthread_once( &once_control, init_once_routine );

  /* Configuration for post processing quality - default to mid (3) for the
   * moment */
  config = xine->config;

  this->pp_quality = xine->config->register_range(config, "video.processing.ffmpeg_pp_quality", 3,
    0, PP_QUALITY_MAX,
    _("MPEG-4 postprocessing quality"),
    _("You can adjust the amount of post processing applied to MPEG-4 video.\n"
      "Higher values result in better quality, but need more CPU. Lower values may "
      "result in image defects like block artifacts. For high quality content, "
      "too heavy post processing can actually make the image worse by blurring it "
      "too much."),
    10, pp_quality_cb, this);

  this->thread_count = xine->config->register_num(config, "video.processing.ffmpeg_thread_count", 1,
    _("FFmpeg video decoding thread count"),
    _("You can adjust the number of video decoding threads which FFmpeg may use.\n"
      "Higher values should speed up decoding but it depends on the codec used "
      "whether parallel decoding is supported. A rule of thumb is to have one "
      "decoding thread per logical CPU (typically 1 to 4).\n"
      "A change of this setting will take effect with playing the next stream."),
    10, thread_count_cb, this);

  this->skip_loop_filter_enum = xine->config->register_enum(config, "video.processing.ffmpeg_skip_loop_filter", 0,
    (char **)skip_loop_filter_enum_names,
    _("Skip loop filter"),
    _("You can control for which frames the loop filter shall be skipped after "
      "decoding.\n"
      "Skipping the loop filter will speedup decoding but may lead to artefacts. "
      "The number of frames for which it is skipped increases from 'none' to 'all'. "
      "The default value leaves the decision up to the implementation.\n"
      "A change of this setting will take effect with playing the next stream."),
    10, skip_loop_filter_enum_cb, this);

  this->choose_speed_over_accuracy = xine->config->register_bool(config, "video.processing.ffmpeg_choose_speed_over_accuracy", 0,
    _("Choose speed over specification compliance"),
    _("You may want to allow speed cheats which violate codec specification.\n"
      "Cheating may speed up decoding but can also lead to decoding artefacts.\n"
      "A change of this setting will take effect with playing the next stream."),
    10, choose_speed_over_accuracy_cb, this);

  return this;
}

static const uint32_t wmv8_video_types[] = {
  BUF_VIDEO_WMV8,
  0
};

static const uint32_t wmv9_video_types[] = {
  BUF_VIDEO_WMV9,
  0
};

decoder_info_t dec_info_ffmpeg_video = {
  supported_video_types,   /* supported types */
  6                        /* priority        */
};

decoder_info_t dec_info_ffmpeg_wmv8 = {
  wmv8_video_types,        /* supported types */
  0                        /* priority        */
};

decoder_info_t dec_info_ffmpeg_wmv9 = {
  wmv9_video_types,        /* supported types */
  0                        /* priority        */
};
