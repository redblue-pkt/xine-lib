/*
 * Copyright (C) 2001-2019 the xine project
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
# include "config.h"
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

#ifdef HAVE_POSTPROC
#ifdef HAVE_FFMPEG_AVUTIL_H
# include <postprocess.h>
#else
# include <libpostproc/postprocess.h>
# include <libavutil/mem.h>
#endif
#endif

#ifdef HAVE_VA_VA_X11_H
# include <libavcodec/vaapi.h>
# include "accel_vaapi.h"
# define ENABLE_VAAPI 1
#endif

#include "ffmpeg_compat.h"

#if defined(ARCH_X86) && defined(HAVE_MMX)
# include "xine_mmx.h"
# define ENABLE_EMMS
#endif

#define VIDEOBUFSIZE        (128*1024)
#define SLICE_BUFFER_SIZE   (1194*1024)

#define SLICE_OFFSET_SIZE   128

#define ENABLE_DIRECT_RENDERING 

#ifndef ENABLE_DIRECT_RENDERING
# undef ENABLE_VAAPI
#endif

typedef struct ff_video_decoder_s ff_video_decoder_t;

typedef struct ff_video_class_s {
  video_decoder_class_t   decoder_class;

#ifdef HAVE_POSTPROC
  int                     pp_quality;
#endif
  int                     thread_count;
  int8_t                  skip_loop_filter_enum;
  int8_t                  choose_speed_over_accuracy;
  uint8_t                 enable_dri;

  uint8_t                 enable_vaapi;
  uint8_t                 vaapi_mpeg_softdec;

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
#ifdef HAVE_POSTPROC
  uint8_t           pp_available:1;
#endif
  uint8_t           is_direct_rendering_disabled:1;  /* used only to avoid flooding log */
  uint8_t           cs_convert_init:1;
  uint8_t           assume_bad_field_picture:1;
  uint8_t           use_bad_frames:1;

  xine_bmiheader    bih;
  unsigned char    *buf;
  int               bufsize;
  int               size;
  int               skipframes;

  int              *slice_offset_table;
  int               slice_offset_size;
  int               slice_offset_pos;

  AVFrame          *av_frame;
  AVFrame          *av_frame2;
  AVCodecContext   *context;
  AVCodec          *codec;

#ifdef HAVE_POSTPROC
  int               pp_quality;
  int               pp_flags;
  pp_context       *our_context;
  pp_mode          *our_mode;
#endif /* HAVE_POSTPROC */

  /* mpeg-es parsing */
  mpeg_parser_t    *mpeg_parser;

  double            aspect_ratio;
  int               aspect_ratio_prio;
  int               frame_flags;
  int               edge;

  int               output_format;

#ifdef ENABLE_DIRECT_RENDERING
  dlist_t           ffsf_free, ffsf_used;
  int               ffsf_num, ffsf_total;
  pthread_mutex_t   ffsf_mutex;
#endif

#if XFF_PALETTE == 1
  AVPaletteControl  palette_control;
#elif XFF_PALETTE == 2 || XFF_PALETTE == 3
  uint32_t          palette[256];
  int               palette_changed;
#endif

  int               color_matrix, full2mpeg;
  unsigned char     ytab[1024], ctab[1024];

  int               pix_fmt;
  rgb2yuy2_t       *rgb2yuy2;

#ifdef LOG
  enum PixelFormat  debug_fmt;
#endif

  uint8_t           set_stream_info;

#ifdef ENABLE_VAAPI
  int                   vaapi_width, vaapi_height;
  int                   vaapi_profile;
  struct vaapi_context  vaapi_context;
  vaapi_accel_t         *accel;
  vo_frame_t            *accel_img;
#endif

  /* Ugly: 2nd guess the reason for flush.
     ff_flush () should really have an extra argument telling this. */
  enum {
    STATE_RESET = 0,
    STATE_DISCONTINUITY,
    STATE_READING_DATA,
    STATE_FRAME_SENT,
    STATE_FLUSHED
  }                 state;
  int               decode_attempts;
#if XFF_VIDEO == 3
  int               flush_packet_sent;
#endif

#ifdef ENABLE_EMMS
  /* see get_buffer () */
  int               use_emms;
#endif
};

/* import color matrix names */
#define CM_HAVE_YCGCO_SUPPORT 1
#include "../../video_out/color_matrix.c"


static void ff_check_colorspace (ff_video_decoder_t *this) {
  int i, cm, caps;

#ifdef XFF_AVCODEC_COLORSPACE
  cm = this->context->colorspace << 1;
#else
  cm = 0;
#endif

  /* ffmpeg bug: color_range not set by svq3 decoder */
  i = this->context->pix_fmt;
  if (cm && ((i == PIX_FMT_YUVJ420P) || (i == PIX_FMT_YUVJ444P)))
    cm |= 1;
#ifdef XFF_AVCODEC_COLORSPACE
  if (this->context->color_range == AVCOL_RANGE_JPEG)
    cm |= 1;
#endif

  /* report changes of colorspyce and/or color range */
  if (cm != this->color_matrix) {
    this->color_matrix = cm;
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
      "ffmpeg_video_dec: color matrix #%d [%s]\n", cm >> 1, cm_names[cm & 31]);

    caps = this->stream->video_out->get_capabilities (this->stream->video_out);

    if (!(caps & VO_CAP_COLOR_MATRIX)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
        "ffmpeg_video_dec: video out plugin does not support colour matrix switching\n");
      cm &= 1;
    }

    /* HD color matrix (ITU-R 709/SMTE 240) serves to provide a bit more depth
       at least for greens where it strikes the most. Having more than 8 bits
       for real makes this trick obsolete, so deep color defaults to good old
       SD mode. Bad thing: now we need real support in video out to benefit.
       Lets at least map to fullrange where available. */
#ifdef AV_PIX_FMT_YUV420P9
    if (this->context->pix_fmt == AV_PIX_FMT_YUV420P9) {
      int mode = (caps & VO_CAP_FULLRANGE) ? ((cm & 1) ? 0 : 1) : (cm & 1) ? -1 : 0;
      if ((cm >> 1) == 2)
        cm = 10 | (cm & 1);
      if ((cm >> 1) == 8)
        mode = 0;
      if (mode > 0) { /* 9 bit mpeg to full */
        memset (this->ytab, 0, 2 * 16);
        for (i = 2 * 16; i < 2 * 235; i++)
          this->ytab[i] = (255 * i - 255 * 2 * 16 + 219) / (2 * 219);
        memset (this->ytab + i, 255, 2 * 21);
        memset (this->ctab, 0, 2 * 16);
        for (i = 2 * 16; i < 2 * 240; i++)
          this->ctab[i] = (254 * i + (224 - 254) * 2 * 128 + 224) / (2 * 224);
        memset (this->ctab + i, 255, 2 * 16);
        cm |= 1;
      } else if (mode < 0) { /* 9 bit full to mpeg */
        for (i = 0; i < 2 * 256; i++) {
          this->ytab[i] = (219 * i + 255) / (2 * 255) + 16;
          this->ctab[i] = (224 * i + (254 - 224) * 2 * 128 + 254) / (2 * 254);
        }
        cm &= ~1;
      } else { /* 9 bit 1:1 */
        for (i = 0; i < 2 * 256 - 1; i++)
          this->ytab[i] = this->ctab[i] = (i + 1) >> 1;
        this->ytab[i] = this->ctab[i] = 255;
      }
    }
#endif
#ifdef AV_PIX_FMT_YUV420P10
    if (this->context->pix_fmt == AV_PIX_FMT_YUV420P10) {
      int mode = (caps & VO_CAP_FULLRANGE) ? ((cm & 1) ? 0 : 1) : (cm & 1) ? -1 : 0;
      if ((cm >> 1) == 2)
        cm = 10 | (cm & 1);
      if ((cm >> 1) == 8)
        mode = 0;
      if (mode > 0) { /* 10 bit mpeg to full */
        memset (this->ytab, 0, 4 * 16);
        for (i = 4 * 16; i < 4 * 235; i++)
          this->ytab[i] = (255 * i - 255 * 4 * 16 + 2 * 219) / (4 * 219);
        memset (this->ytab + i, 255, 4 * 21);
        memset (this->ctab, 0, 4 * 16);
        for (i = 4 * 16; i < 4 * 240; i++)
          this->ctab[i] = (254 * i + (224 - 254) * 4 * 128 + 2 * 224) / (4 * 224);
        memset (this->ctab + i, 255, 4 * 16);
        cm |= 1;
      } else if (mode < 0) { /* 10 bit full to mpeg */
        for (i = 0; i < 4 * 256; i++) {
          this->ytab[i] = (219 * i + 2 * 255) / (4 * 255) + 16;
          this->ctab[i] = (224 * i + (254 - 224) * 4 * 128 + 2 * 254) / (4 * 254);
        }
        cm &= ~1;
      } else { /* 10 bit 1:1 */
        for (i = 0; i < 4 * 256 - 2; i++)
          this->ytab[i] = this->ctab[i] = (i + 2) >> 2;
        this->ytab[i] = this->ctab[i] = 255; i++;
        this->ytab[i] = this->ctab[i] = 255;
      }
    }
#endif

    this->full2mpeg = 0;
    if ((cm & 1) && !(caps & VO_CAP_FULLRANGE)) {
      /* sigh. fall back to manual conversion */
      cm &= ~1;
      this->full2mpeg = 1;
      for (i = 0; i < 256; i++) {
        this->ytab[i] = (219 * i + 127) / 255 + 16;
        this->ctab[i] = 112 * (i - 128) / 127 + 128;
      }
    }

    VO_SET_FLAGS_CM (cm, this->frame_flags);
  }
}

static void set_stream_info(ff_video_decoder_t *this) {
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  this->bih.biWidth);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->bih.biHeight);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->aspect_ratio * 10000);
}


#ifdef ENABLE_DIRECT_RENDERING

/* Keep track of DR1 frames */

typedef struct {
  dnode_t             node;
  int                 refs;
  ff_video_decoder_t *this;
  vo_frame_t         *vo_frame;
# ifdef ENABLE_VAAPI
  ff_vaapi_surface_t *va_surface;
# endif
} ff_saved_frame_t;

static ff_saved_frame_t *ffsf_new (ff_video_decoder_t *this) {
  ff_saved_frame_t *ffsf;

  pthread_mutex_lock (&this->ffsf_mutex);
  if (DLIST_IS_EMPTY (&this->ffsf_free)) {
    ffsf = calloc (1, sizeof (*ffsf));
    if (ffsf) {
      ffsf->this = this;
      DLIST_ADD_TAIL (&ffsf->node, &this->ffsf_used);
      this->ffsf_num++;
      this->ffsf_total++;
    }
  } else {
    ffsf = (ff_saved_frame_t *)this->ffsf_free.head;
    DLIST_REMOVE (&ffsf->node);
    ffsf->refs = 0;
    ffsf->this = this;
    ffsf->vo_frame = NULL;
# ifdef ENABLE_VAAPI
    ffsf->va_surface = NULL;
# endif
    DLIST_ADD_TAIL (&ffsf->node, &this->ffsf_used);
    this->ffsf_num++;
  }
  pthread_mutex_unlock (&this->ffsf_mutex);
  return ffsf;
}

static void ffsf_delete (ff_saved_frame_t *ffsf) {
  if (!ffsf)
    return;

  pthread_mutex_lock (&ffsf->this->ffsf_mutex);
  DLIST_REMOVE (&ffsf->node);
  DLIST_ADD_TAIL (&ffsf->node, &ffsf->this->ffsf_free);
  ffsf->this->ffsf_num--;
  pthread_mutex_unlock (&ffsf->this->ffsf_mutex);
}

# ifdef XFF_AV_BUFFER
static void release_frame (void *saved_frame, uint8_t *data) {
  ff_saved_frame_t *ffsf = saved_frame;
  (void)data;
  /* At this point in time, AVFrame may already be reused. So take our saved values instead. */
  if (ffsf) {
    if (--(ffsf->refs))
      return;
#  ifdef ENABLE_VAAPI
    if (ffsf->va_surface)
      ffsf->this->accel->f->release_vaapi_surface (ffsf->this->accel_img, ffsf->va_surface);
#  endif
    if (ffsf->vo_frame)
      ffsf->vo_frame->free (ffsf->vo_frame);
    ffsf_delete (ffsf);
  }
}
# else /* !XFF_AV_BUFFER */
static void release_buffer(struct AVCodecContext *context, AVFrame *av_frame){
  ff_video_decoder_t *this = (ff_video_decoder_t *)context->opaque;

#  ifdef ENABLE_VAAPI
  if( this->context->pix_fmt == PIX_FMT_VAAPI_VLD ) {
    if(this->accel->f->guarded_render(this->accel_img)) {
      ff_vaapi_surface_t *va_surface = (ff_vaapi_surface_t *)av_frame->data[0];
      if(va_surface != NULL) {
        this->accel->f->release_vaapi_surface(this->accel_img, va_surface);
        lprintf("release_buffer: va_surface_id 0x%08x\n", (unsigned int)av_frame->data[3]);
      }
    }
  }
#  endif

  if (av_frame->type == FF_BUFFER_TYPE_USER) {
    if (av_frame->opaque) {
      ff_saved_frame_t *ffsf = (ff_saved_frame_t *)av_frame->opaque;
      if (ffsf->vo_frame)
        ffsf->vo_frame->free (ffsf->vo_frame);
      ffsf_delete (ffsf);
    }

    av_frame->opaque  = NULL;
    av_frame->data[0] = NULL;
    av_frame->data[1] = NULL;
    av_frame->data[2] = NULL;
    av_frame->linesize[0] = 0;
    av_frame->linesize[1] = 0;
    av_frame->linesize[2] = 0;

  } else {
    avcodec_default_release_buffer(context, av_frame);
  }
}
# endif /* !XFF_AV_BUFFER */

# ifdef ENABLE_VAAPI
static int get_buffer_vaapi_vld (AVCodecContext *context, AVFrame *av_frame)
{
  ff_video_decoder_t *this = (ff_video_decoder_t *)context->opaque;
  ff_saved_frame_t *ffsf;
  int width  = context->width;
  int height = context->height;

  av_frame->opaque  = NULL;
  av_frame->data[0] = NULL;
  av_frame->data[1] = NULL;
  av_frame->data[2] = NULL;
  av_frame->data[3] = NULL;
#  ifdef XFF_FRAME_AGE
  av_frame->age = 1;
#  endif
  av_frame->reordered_opaque = context->reordered_opaque;

  ffsf = ffsf_new (this);
  if (!ffsf)
    return AVERROR (ENOMEM);
  av_frame->opaque = ffsf;

  /* reinitialize vaapi for new image size */
  if (width != this->vaapi_width || height != this->vaapi_height) {
    VAStatus status;

    this->vaapi_width  = width;
    this->vaapi_height = height;
    status = this->accel->f->vaapi_init (this->accel_img, this->vaapi_profile, width, height);

    if (status == VA_STATUS_SUCCESS) {
      ff_vaapi_context_t *va_context = this->accel->f->get_context (this->accel_img);

      if (va_context) {
        this->vaapi_context.config_id  = va_context->va_config_id;
        this->vaapi_context.context_id = va_context->va_context_id;
        this->vaapi_context.display    = va_context->va_display;
      }
    }
  }

  if(!this->accel->f->guarded_render(this->accel_img)) {
    vo_frame_t *img;
    img = this->stream->video_out->get_frame (this->stream->video_out,
                                              width,
                                              height,
                                              this->aspect_ratio,
                                              this->output_format,
                                              VO_BOTH_FIELDS|this->frame_flags);

    vaapi_accel_t *accel = (vaapi_accel_t*)img->accel_data;
    ff_vaapi_surface_t *va_surface = accel->f->get_vaapi_surface(img);

    if(va_surface) {
      av_frame->data[0] = (void *)va_surface;//(void *)(uintptr_t)va_surface->va_surface_id;
      av_frame->data[3] = (void *)(uintptr_t)va_surface->va_surface_id;
    }
    ffsf->vo_frame = img;
  } else {
    ff_vaapi_surface_t *va_surface = this->accel->f->get_vaapi_surface(this->accel_img);

    if(va_surface) {
      av_frame->data[0] = (void *)va_surface;//(void *)(uintptr_t)va_surface->va_surface_id;
      av_frame->data[3] = (void *)(uintptr_t)va_surface->va_surface_id;
    }
    ffsf->va_surface = va_surface;
  }

  lprintf("1: 0x%08x\n", (unsigned int)(intptr_t)av_frame->data[3]);

  av_frame->linesize[0] = 0;
  av_frame->linesize[1] = 0;
  av_frame->linesize[2] = 0;
  av_frame->linesize[3] = 0;

#  ifdef XFF_AV_BUFFER
  /* Does this really work???? */
  av_frame->buf[0] = av_buffer_create (NULL, 0, release_frame, ffsf, 0);
  if (av_frame->buf[0])
    (ffsf->refs)++;
  av_frame->buf[1] = NULL;
  av_frame->buf[2] = NULL;
#  else
  av_frame->type = FF_BUFFER_TYPE_USER;
#  endif
  this->is_direct_rendering_disabled = 1;

  return 0;
}
# endif /* ENABLE_VAAPI */

/* called from ffmpeg to do direct rendering method 1 */
# ifdef XFF_AV_BUFFER
static int get_buffer (AVCodecContext *context, AVFrame *av_frame, int flags)
# else
static int get_buffer (AVCodecContext *context, AVFrame *av_frame)
# endif
  {
  ff_video_decoder_t *this = (ff_video_decoder_t *)context->opaque;
  vo_frame_t *img;
  ff_saved_frame_t *ffsf;
  int buf_width  = av_frame->width;
  int buf_height = av_frame->height;
  /* The visible size, may be smaller. */
  int width  = context->width;
  int height = context->height;
  int top_edge;
  int guarded_render = 0;

# ifdef ENABLE_EMMS
  /* some background thread may call this while still in mmx mode.
    this will trash "double" aspect ratio values, even when only
    passing them to vo_get_frame () verbatim. */
  if (this->use_emms)
    emms ();
# endif

  /* multiple threads have individual contexts !! */
# ifdef XFF_AVCODEC_COLORSPACE
  if (context != this->context) {
    if (this->context->colorspace == 2) /* undefined */
      this->context->colorspace = context->colorspace;
    if (this->context->color_range == 0)
      this->context->color_range = context->color_range;
    if (this->context->pix_fmt < 0)
      this->context->pix_fmt = context->pix_fmt;
  }
# endif

  /* A bit of unmotivated paranoia... */
  if (buf_width < width)
    buf_width = width;
  if (buf_height < height)
    buf_height = height;

  ff_check_colorspace (this);

  if (!this->bih.biWidth || !this->bih.biHeight) {
    this->bih.biWidth = width;
    this->bih.biHeight = height;
  }

  if (this->aspect_ratio_prio == 0) {
    this->aspect_ratio = (double)width / (double)height;
    this->aspect_ratio_prio = 1;
    lprintf("default aspect ratio: %f\n", this->aspect_ratio);
    this->set_stream_info = 1;
  }

  avcodec_align_dimensions(context, &buf_width, &buf_height);

# ifdef ENABLE_VAAPI
  if( context->pix_fmt == PIX_FMT_VAAPI_VLD ) {
    return get_buffer_vaapi_vld(context, av_frame);
  }

  /* on vaapi out do not use direct rendeing */
  if(this->class->enable_vaapi) {
    this->output_format = XINE_IMGFMT_YV12;
  }

  if(this->accel)
    guarded_render = this->accel->f->guarded_render(this->accel_img);
# endif /* ENABLE_VAAPI */

  /* The alignment rhapsody */
  /* SSE2+ requirement (U, V rows need to be 16 byte aligned too) */
  buf_width  += 2 * this->edge + 31;
  buf_width  &= ~31;
  /* 2 extra lines for the edge wrap below plus XINE requirement */
  top_edge = this->edge;
  if (top_edge)
    top_edge += 2;
  buf_height += top_edge + this->edge + 15;
  buf_height &= ~15;

  if (this->full2mpeg || guarded_render ||
    (context->pix_fmt != PIX_FMT_YUV420P && context->pix_fmt != PIX_FMT_YUVJ420P)) {
    if (!this->is_direct_rendering_disabled) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("ffmpeg_video_dec: unsupported frame format, DR1 disabled.\n"));
      this->is_direct_rendering_disabled = 1;
    }

    /* FIXME: why should i have to do that ? */
    av_frame->data[0]= NULL;
    av_frame->data[1]= NULL;
    av_frame->data[2]= NULL;
# ifdef XFF_AV_BUFFER
    return avcodec_default_get_buffer2(context, av_frame, flags);
# else
    return avcodec_default_get_buffer(context, av_frame);
# endif
  }

  if ((buf_width != width) || (buf_height != height)) {
    if (!(this->stream->video_out->get_capabilities(this->stream->video_out) & VO_CAP_CROP)) {
      if (!this->is_direct_rendering_disabled) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                _("ffmpeg_video_dec: unsupported frame dimensions, DR1 disabled.\n"));
        this->is_direct_rendering_disabled = 1;
      }
      /* FIXME: why should i have to do that ? */
      av_frame->data[0]= NULL;
      av_frame->data[1]= NULL;
      av_frame->data[2]= NULL;
# ifdef XFF_AV_BUFFER
      return avcodec_default_get_buffer2(context, av_frame, flags);
# else
      return avcodec_default_get_buffer(context, av_frame);
# endif
    }
  }

  if (this->is_direct_rendering_disabled) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG, _("ffmpeg_video_dec: direct rendering enabled\n"));
    this->is_direct_rendering_disabled = 0;
  }

  img = this->stream->video_out->get_frame (this->stream->video_out,
                                            buf_width,
                                            buf_height,
                                            this->aspect_ratio,
                                            this->output_format,
                                            VO_BOTH_FIELDS|this->frame_flags);

  ffsf = ffsf_new (this);
  if (!ffsf) {
    img->free (img);
    return AVERROR (ENOMEM);
  }
  ffsf->vo_frame = img;

# ifdef XFF_AV_BUFFER
  /* Sigh. Wrap vo image planes into AVBufferRefs. When ff unref's them, they unref our trigger.
   * That one then fires image release.
   * For single chunk render space like video_out_opengl2, just 1 AVBufferRef is enough.
   */
  {
    size_t s0 = img->height * img->pitches[0];
    size_t s1 = (img->height + 1) >> 1, s2 = s1;
    s1 *= img->pitches[1];
    s2 *= img->pitches[2];
    if ((img->base[1] == img->base[0] + s0) && (img->base[2] == img->base[1] + s1)) {
      s0 += s1 + s2;
      s1 = s2 = 0;
      av_frame->buf[1] = av_frame->buf[2] = NULL;
    }
    av_frame->buf[0] = av_buffer_create (img->base[0], s0, release_frame, ffsf, 0);
    if (av_frame->buf[0]) {
      (ffsf->refs)++;
    } else {
      img->free (img);
      ffsf_delete (ffsf);
      return AVERROR (ENOMEM);
    }
    if (s1) {
      av_frame->buf[1] = av_buffer_create (img->base[1], s1, release_frame, ffsf, 0);
      if (av_frame->buf[1])
        (ffsf->refs)++;
      av_frame->buf[2] = av_buffer_create (img->base[2], s2, release_frame, ffsf, 0);
      if (av_frame->buf[2])
        (ffsf->refs)++;
    }
  }
# else
  av_frame->type = FF_BUFFER_TYPE_USER;
# endif

  av_frame->opaque = ffsf;

  av_frame->extended_data = av_frame->data;

  av_frame->data[0]= img->base[0];
  av_frame->data[1]= img->base[1];
  av_frame->data[2]= img->base[2];

  av_frame->linesize[0] = img->pitches[0];
  av_frame->linesize[1] = img->pitches[1];
  av_frame->linesize[2] = img->pitches[2];

  if (this->output_format == XINE_IMGFMT_YV12) {
    /* nasty hack: wrap left edge to the right side to get proper
       SSE2 alignment on all planes. */
    av_frame->data[0] += img->pitches[0] * top_edge;
    av_frame->data[1] += img->pitches[1] * top_edge / 2;
    av_frame->data[2] += img->pitches[2] * top_edge / 2;
    img->crop_left   = 0;
    img->crop_top    = top_edge;
    img->crop_right  = buf_width  - width;
    img->crop_bottom = buf_height - height - top_edge;
  }

  /* We should really keep track of the ages of xine frames (see
   * avcodec_default_get_buffer in libavcodec/utils.c)
   * For the moment tell ffmpeg that every frame is new (age = bignumber) */
# ifdef XFF_FRAME_AGE
  av_frame->age = 256*256*256*64;
# endif

  /* take over pts for this frame to have it reordered */
  av_frame->reordered_opaque = context->reordered_opaque;

  return 0;
}

#endif /* ENABLE_DR1 */


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

#ifdef ENABLE_VAAPI
static int vaapi_pixfmt2imgfmt(enum PixelFormat pix_fmt, unsigned codec_id, int profile)
{
  static const struct {
    unsigned         fmt;
    enum PixelFormat pix_fmt;
# if defined LIBAVCODEC_VERSION_INT && LIBAVCODEC_VERSION_INT >= ((54<<16)|(25<<8))
    enum AVCodecID   codec_id;
# else
    enum CodecID     codec_id;
# endif
    int              profile;
  } conversion_map[] = {
    {IMGFMT_VAAPI_MPEG2,     PIX_FMT_VAAPI_VLD,  CODEC_ID_MPEG2VIDEO, -1},
    {IMGFMT_VAAPI_MPEG2_IDCT,PIX_FMT_VAAPI_IDCT, CODEC_ID_MPEG2VIDEO, -1},
    {IMGFMT_VAAPI_MPEG2_MOCO,PIX_FMT_VAAPI_MOCO, CODEC_ID_MPEG2VIDEO, -1},
    {IMGFMT_VAAPI_MPEG4,     PIX_FMT_VAAPI_VLD,  CODEC_ID_MPEG4,      -1},
    {IMGFMT_VAAPI_H263,      PIX_FMT_VAAPI_VLD,  CODEC_ID_H263,       -1},
    {IMGFMT_VAAPI_H264,      PIX_FMT_VAAPI_VLD,  CODEC_ID_H264,       -1},
    {IMGFMT_VAAPI_WMV3,      PIX_FMT_VAAPI_VLD,  CODEC_ID_WMV3,       -1},
    {IMGFMT_VAAPI_VC1,       PIX_FMT_VAAPI_VLD,  CODEC_ID_VC1,        -1},
# ifdef FF_PROFILE_HEVC_MAIN_10
    {IMGFMT_VAAPI_HEVC_MAIN10, PIX_FMT_VAAPI_VLD,  AV_CODEC_ID_HEVC,  FF_PROFILE_HEVC_MAIN_10},
# endif
# ifdef FF_PROFILE_HEVC_MAIN
    {IMGFMT_VAAPI_HEVC,      PIX_FMT_VAAPI_VLD,  AV_CODEC_ID_HEVC,    -1},
# endif
  };

  unsigned i;
  for (i = 0; i < sizeof(conversion_map)/sizeof(conversion_map[0]); i++) {
    if (conversion_map[i].pix_fmt == pix_fmt &&
        (conversion_map[i].codec_id ==  0 || conversion_map[i].codec_id == codec_id) &&
        (conversion_map[i].profile  == -1 || conversion_map[i].profile  == profile)) {
      return conversion_map[i].fmt;
    }
  }
  return 0;
}

/* TJ. libavcodec calls this with a list of supported pixel formats and lets us choose 1.
   Returning PIX_FMT_VAAPI_VLD enables VAAPI.
   However, at this point we only got image width and height from container, being unreliable
   or zero (MPEG-TS). Thus we repeat vaapi_context initialization in get_buffer when needed.
   This should be OK since NAL unit parsing is always done in software. */
static enum PixelFormat get_format(struct AVCodecContext *context, const enum PixelFormat *fmt)
{
  int i;
  ff_video_decoder_t *this = (ff_video_decoder_t *)context->opaque;

  if(!this->class->enable_vaapi || !this->accel_img)
    return avcodec_default_get_format(context, fmt);

  if (context->codec_id == CODEC_ID_MPEG2VIDEO && this->class->vaapi_mpeg_softdec) {
    return avcodec_default_get_format(context, fmt);
  }

  vaapi_accel_t *accel = (vaapi_accel_t*)this->accel_img->accel_data;

  for (i = 0; fmt[i] != PIX_FMT_NONE; i++) {
    if (fmt[i] != PIX_FMT_VAAPI_VLD)
      continue;

    uint32_t format = vaapi_pixfmt2imgfmt(fmt[i], context->codec_id, context->profile);
    if (!format) {
      continue;
    }

    this->vaapi_profile = accel->f->profile_from_imgfmt (this->accel_img, format);

    if (this->vaapi_profile >= 0) {
      int width  = context->width;
      int height = context->height;
      VAStatus status;

      if (!width || !height) {
        width  = 1920;
        height = 1080;
      }
      this->vaapi_width  = width;
      this->vaapi_height = height;
      status = accel->f->vaapi_init (this->accel_img, this->vaapi_profile, width, height);

      if( status == VA_STATUS_SUCCESS ) {
        ff_vaapi_context_t *va_context = accel->f->get_context(this->accel_img);

        if(!va_context)
          break;

        context->draw_horiz_band = NULL;
        context->slice_flags = SLICE_FLAG_CODED_ORDER | SLICE_FLAG_ALLOW_FIELD;

        this->vaapi_context.config_id    = va_context->va_config_id;
        this->vaapi_context.context_id   = va_context->va_context_id;
        this->vaapi_context.display      = va_context->va_display;

        context->hwaccel_context     = &this->vaapi_context;
        this->pts = 0;

        return fmt[i];
      }
    }
  }

  xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
           _("ffmpeg_video_dec: no suitable format for HW decoding\n"));

  return avcodec_default_get_format(context, fmt);
}
#endif /* ENABLE_VAAPI */

static void init_video_codec (ff_video_decoder_t *this, unsigned int codec_type) {
  int thread_count = this->class->thread_count;
  int use_vaapi = 0;

  this->context->width = this->bih.biWidth;
  this->context->height = this->bih.biHeight;
#ifdef XFF_AVCODEC_STREAM_CODEC_TAG
  this->context->stream_codec_tag =
#endif
  this->context->codec_tag =
    _x_stream_info_get(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC);


  this->stream->video_out->open (this->stream->video_out, this->stream);

  this->edge = 0;
  if(this->codec->capabilities & AV_CODEC_CAP_DR1 && this->class->enable_dri) {
    if (this->stream->video_out->get_capabilities (this->stream->video_out) & VO_CAP_CROP) {
      /* We can crop. Fine. Lets allow decoders to paint over the frame edges.
         This will be slightly faster. And it is also a workaround for buggy
         v54 who likes to ignore EMU_EDGE for wmv2 and xvid. */
      this->edge = XFF_EDGE_WIDTH ();
    }
#ifdef CODEC_FLAG_EMU_EDGE
    else {
      /* Some codecs (eg rv10) copy flags in init so it's necessary to set
       * this flag here in case we are going to use direct rendering */
      this->context->flags |= CODEC_FLAG_EMU_EDGE;
    }
#endif
  }

  /* TJ. without this, it wont work at all on my machine */
  this->context->codec_id = this->codec->id;
  this->context->codec_type = this->codec->type;

  if (this->class->choose_speed_over_accuracy)
    this->context->flags2 |= AV_CODEC_FLAG2_FAST;

  this->context->skip_loop_filter = skip_loop_filter_enum_values[this->class->skip_loop_filter_enum];

  /* disable threads for SVQ3 */
  if (this->codec->id == CODEC_ID_SVQ3) {
    thread_count = 1;
  }

  /* Use "bad frames" to fill pts gaps */
  if (codec_type != BUF_VIDEO_VP9)
    this->use_bad_frames = 1;

  /* Check for VAAPI HWDEC capability */
#ifdef ENABLE_VAAPI
  if( this->class->enable_vaapi ) {
    uint32_t format = vaapi_pixfmt2imgfmt(PIX_FMT_VAAPI_VLD, this->codec->id, -1);
    if (format && this->accel->f->profile_from_imgfmt (this->accel_img, format) >= 0) {
      use_vaapi = 1;
    } else {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "ffmpeg_video_dec: VAAPI decoding of 0x%08x not supported by hardware\n", codec_type);
    }
  }
#endif

  /* enable direct rendering by default */
  this->output_format = XINE_IMGFMT_YV12;
#ifdef ENABLE_DIRECT_RENDERING
  if( this->codec->capabilities & AV_CODEC_CAP_DR1 && this->class->enable_dri ) {
# ifdef XFF_AV_BUFFER
    this->context->get_buffer2 = get_buffer;
    this->context->thread_safe_callbacks = 1;
#  if XFF_VIDEO != 3
    this->context->refcounted_frames = 1;
#  endif
# else
    this->context->get_buffer = get_buffer;
    this->context->release_buffer = release_buffer;
# endif
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("ffmpeg_video_dec: direct rendering enabled\n"));
  }
#endif

#ifdef ENABLE_VAAPI
  if( use_vaapi ) {
    this->context->skip_loop_filter = AVDISCARD_DEFAULT;
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            _("ffmpeg_video_dec: force AVDISCARD_DEFAULT for VAAPI\n"));

    this->class->enable_dri = 1;
    this->output_format = XINE_IMGFMT_VAAPI;
# ifdef XFF_AV_BUFFER
    this->context->get_buffer2 = get_buffer;
# else
    this->context->get_buffer = get_buffer;
    this->context->reget_buffer = get_buffer;
    this->context->release_buffer = release_buffer;
# endif
    this->context->get_format = get_format;
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("ffmpeg_video_dec: direct rendering enabled\n"));

    thread_count = 1;
  }
#endif /* ENABLE_VAAPI */

#ifdef DEPRECATED_AVCODEC_THREAD_INIT
  if (thread_count > 1) {
    this->context->thread_count = thread_count;
  }
#endif

  pthread_mutex_lock(&ffmpeg_lock);
  if (XFF_AVCODEC_OPEN (this->context, this->codec) < 0) {
    pthread_mutex_unlock(&ffmpeg_lock);
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
             _("ffmpeg_video_dec: couldn't open decoder\n"));
    free(this->context);
    this->context = NULL;
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
    this->stream->video_out->close (this->stream->video_out, this->stream);
    return;
  }

  if (this->codec->id == CODEC_ID_VC1 &&
      (!this->bih.biWidth || !this->bih.biHeight)) {
    /* VC1 codec must be re-opened with correct width and height. */
    avcodec_close(this->context);

    if (XFF_AVCODEC_OPEN (this->context, this->codec) < 0) {
      pthread_mutex_unlock(&ffmpeg_lock);
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
	       _("ffmpeg_video_dec: couldn't open decoder (pass 2)\n"));
      free(this->context);
      this->context = NULL;
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
      this->stream->video_out->close (this->stream->video_out, this->stream);
      return;
    }
  }

#ifndef DEPRECATED_AVCODEC_THREAD_INIT
  if (thread_count > 1) {
    if (avcodec_thread_init(this->context, thread_count) != -1)
      this->context->thread_count = thread_count;
  }
#endif

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

  this->skipframes = 0;

  /* flag for interlaced streams */
  this->frame_flags = 0;
  /* FIXME: which codecs can be interlaced?
      FIXME: check interlaced DCT and other codec specific info. */
  if (!use_vaapi) {
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
  }

#ifdef XFF_AVCODEC_REORDERED_OPAQUE
  /* dont want initial AV_NOPTS_VALUE here */
  this->context->reordered_opaque = 0;
#endif
}

#ifdef ENABLE_VAAPI
static void vaapi_enable_vaapi(void *user_data, xine_cfg_entry_t *entry) {
  ff_video_class_t   *class = (ff_video_class_t *) user_data;

  class->enable_vaapi = entry->num_value;
}

static void vaapi_mpeg_softdec_func(void *user_data, xine_cfg_entry_t *entry) {
  ff_video_class_t   *class = (ff_video_class_t *) user_data;

  class->vaapi_mpeg_softdec = entry->num_value;
}
#endif /* ENABLE_VAAPI */

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
  if (class->thread_count < 1)
    class->thread_count = 1;
  else if (class->thread_count > 8)
    class->thread_count = 8;
}

#ifdef HAVE_POSTPROC
static void pp_quality_cb(void *user_data, xine_cfg_entry_t *entry) {
  ff_video_class_t   *class = (ff_video_class_t *) user_data;

  class->pp_quality = entry->num_value;
}
#endif

static void dri_cb(void *user_data, xine_cfg_entry_t *entry) {
  ff_video_class_t   *class = (ff_video_class_t *) user_data;

  class->enable_dri = entry->num_value;
}

#ifdef HAVE_POSTPROC
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
#endif /* HAVE_POSTPROC */

static void init_postprocess (ff_video_decoder_t *this) {
#ifdef HAVE_POSTPROC
#if defined(ARCH_X86)
  uint32_t cpu_caps;
#endif

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

  this->pp_flags = PP_FORMAT_420;

#if defined(ARCH_X86)
  /* Detect what cpu accel we have */
  cpu_caps = xine_mm_accel();

  if(cpu_caps & MM_ACCEL_X86_MMX)
    this->pp_flags |= PP_CPU_CAPS_MMX;

  if(cpu_caps & MM_ACCEL_X86_MMXEXT)
    this->pp_flags |= PP_CPU_CAPS_MMX2;

  if(cpu_caps & MM_ACCEL_X86_3DNOW)
    this->pp_flags |= PP_CPU_CAPS_3DNOW;
#endif

  /* Set level */
  pp_change_quality(this);
#endif /* HAVE_POSTPROC */
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

static void ff_setup_rgb2yuy2 (ff_video_decoder_t *this, int pix_fmt) {
  const char *fmt = "";
  int cm = 10; /* mpeg range ITU-R 601 */

  switch (pix_fmt) {
    case PIX_FMT_ARGB:     fmt = "argb";     break;
    case PIX_FMT_BGRA:     fmt = "bgra";     break;
    case PIX_FMT_RGB24:    fmt = "rgb";      break;
    case PIX_FMT_BGR24:    fmt = "bgr";      break;
    case PIX_FMT_RGB555BE: fmt = "rgb555be"; break;
    case PIX_FMT_RGB555LE: fmt = "rgb555le"; break;
    case PIX_FMT_RGB565BE: fmt = "rgb565be"; break;
    case PIX_FMT_RGB565LE: fmt = "rgb565le"; break;
#ifdef __BIG_ENDIAN__
    case PIX_FMT_PAL8:     fmt = "argb";     break;
#else
    case PIX_FMT_PAL8:     fmt = "bgra";     break;
#endif
  }
  if (this->stream->video_out->get_capabilities (this->stream->video_out) & VO_CAP_FULLRANGE)
    cm = 11; /* full range */
  rgb2yuy2_free (this->rgb2yuy2);
  this->rgb2yuy2 = rgb2yuy2_alloc (cm, fmt);
  this->pix_fmt = pix_fmt;
  VO_SET_FLAGS_CM (cm, this->frame_flags);
  if (pix_fmt == PIX_FMT_PAL8) fmt = "pal8";
  xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
    "ffmpeg_video_dec: converting %s -> %s yuy2\n", fmt, cm_names[cm]);
}

#if defined(AV_PIX_FMT_YUV420P9) || defined(AV_PIX_FMT_YUV420P10)
static void ff_get_deep_color (uint8_t *src, int sstride, uint8_t *dest, int dstride,
  int width, int height, uint8_t *tab) {
  uint16_t *p = (uint16_t *) src;
  uint8_t  *q = dest;
  int       spad = sstride / 2 - width;
  int       dpad = dstride - width;
  int       i;

  while (height--) {
    for (i = width; i; i--)
      *q++ = tab[*p++];
    p += spad;
    q += dpad;
  }
}
#endif

static void ff_convert_frame(ff_video_decoder_t *this, vo_frame_t *img, AVFrame *av_frame) {
  int         y;
  uint8_t    *dy, *du, *dv, *sy, *su, *sv;

#ifdef LOG
  if (this->debug_fmt != this->context->pix_fmt)
    printf ("frame format == %08x\n", this->debug_fmt = this->context->pix_fmt);
#endif

#ifdef ENABLE_VAAPI
  if (this->context->pix_fmt == PIX_FMT_VAAPI_VLD) {
    if (this->accel->f->guarded_render(this->accel_img)) {
      ff_vaapi_surface_t *va_surface = (ff_vaapi_surface_t *)av_frame->data[0];
      this->accel->f->render_vaapi_surface (img, va_surface);
    }
    return;
  }
#endif /* ENABLE_VAAPI */

  ff_check_colorspace (this);

  dy = img->base[0];
  du = img->base[1];
  dv = img->base[2];
  sy = av_frame->data[0];
  su = av_frame->data[1];
  sv = av_frame->data[2];

  /* Some segfaults & heap corruption have been observed with img->height,
   * so we use this->bih.biHeight instead (which is the displayed height)
   */

  switch (this->context->pix_fmt) {
#ifdef AV_PIX_FMT_YUV420P9
    case AV_PIX_FMT_YUV420P9:
      /* Y */
      ff_get_deep_color (av_frame->data[0], av_frame->linesize[0], img->base[0], img->pitches[0],
        img->width, this->bih.biHeight, this->ytab);
      /* U */
      ff_get_deep_color (av_frame->data[1], av_frame->linesize[1], img->base[1], img->pitches[1],
        img->width / 2, this->bih.biHeight / 2, this->ctab);
      /* V */
      ff_get_deep_color (av_frame->data[2], av_frame->linesize[2], img->base[2], img->pitches[2],
        img->width / 2, this->bih.biHeight / 2, this->ctab);
    break;
#endif
#ifdef AV_PIX_FMT_YUV420P10
    case AV_PIX_FMT_YUV420P10:
      /* Y */
      ff_get_deep_color (av_frame->data[0], av_frame->linesize[0], img->base[0], img->pitches[0],
        img->width, this->bih.biHeight, this->ytab);
      /* U */
      ff_get_deep_color (av_frame->data[1], av_frame->linesize[1], img->base[1], img->pitches[1],
        img->width / 2, this->bih.biHeight / 2, this->ctab);
      /* V */
      ff_get_deep_color (av_frame->data[2], av_frame->linesize[2], img->base[2], img->pitches[2],
        img->width / 2, this->bih.biHeight / 2, this->ctab);
    break;
#endif

    case PIX_FMT_YUV410P:
      yuv9_to_yv12(
       /* Y */
        av_frame->data[0],
        av_frame->linesize[0],
        img->base[0],
        img->pitches[0],
       /* U */
        av_frame->data[1],
        av_frame->linesize[1],
        img->base[1],
        img->pitches[1],
       /* V */
        av_frame->data[2],
        av_frame->linesize[2],
        img->base[2],
        img->pitches[2],
       /* width x height */
        img->width,
        this->bih.biHeight);
    break;

    case PIX_FMT_YUV411P:
      yuv411_to_yv12(
       /* Y */
        av_frame->data[0],
        av_frame->linesize[0],
        img->base[0],
        img->pitches[0],
       /* U */
        av_frame->data[1],
        av_frame->linesize[1],
        img->base[1],
        img->pitches[1],
       /* V */
        av_frame->data[2],
        av_frame->linesize[2],
        img->base[2],
        img->pitches[2],
       /* width x height */
        img->width,
        this->bih.biHeight);
    break;

    /* PIX_FMT_RGB32 etc. are only aliases for the native endian versions.
       Lets support them both - wont harm performance here :-) */

    case PIX_FMT_ARGB:
    case PIX_FMT_BGRA:
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:

    case PIX_FMT_RGB555BE:
    case PIX_FMT_RGB555LE:
    case PIX_FMT_RGB565BE:
    case PIX_FMT_RGB565LE:
      if (this->pix_fmt != this->context->pix_fmt)
        ff_setup_rgb2yuy2 (this, this->context->pix_fmt);
      rgb2yuy2_slice (this->rgb2yuy2, sy, av_frame->linesize[0],
        img->base[0], img->pitches[0], img->width, this->bih.biHeight);
    break;

    case PIX_FMT_PAL8:
      if (this->pix_fmt != this->context->pix_fmt)
        ff_setup_rgb2yuy2 (this, this->context->pix_fmt);
      rgb2yuy2_palette (this->rgb2yuy2, su, 256, 8);
      rgb2yuy2_slice (this->rgb2yuy2, sy, av_frame->linesize[0],
        img->base[0], img->pitches[0], img->width, this->bih.biHeight);
    break;

    default: {
      int subsamph = (this->context->pix_fmt == PIX_FMT_YUV444P)
                  || (this->context->pix_fmt == PIX_FMT_YUVJ444P);
      int subsampv = (this->context->pix_fmt != PIX_FMT_YUV420P)
                  && (this->context->pix_fmt != PIX_FMT_YUVJ420P);

      if (this->full2mpeg) {

        uint8_t *ytab = this->ytab;
        uint8_t *ctab = this->ctab;
        uint8_t *p, *q;
        int x;

        for (y = 0; y < this->bih.biHeight; y++) {
          p = sy;
          q = dy;
          for (x = img->width; x > 0; x--) *q++ = ytab[*p++];
          dy += img->pitches[0];
          sy += av_frame->linesize[0];
        }

        for (y = 0; y < this->bih.biHeight / 2; y++) {
          if (!subsamph) {
            p = su, q = du;
            for (x = img->width / 2; x > 0; x--) *q++ = ctab[*p++];
            p = sv, q = dv;
            for (x = img->width / 2; x > 0; x--) *q++ = ctab[*p++];
          } else {
            p = su, q = du;
            for (x = img->width / 2; x > 0; x--) {*q++ = ctab[*p]; p += 2;}
            p = sv, q = dv;
            for (x = img->width / 2; x > 0; x--) {*q++ = ctab[*p]; p += 2;}
          }
          du += img->pitches[1];
          dv += img->pitches[2];
          if (subsampv) {
            su += 2 * av_frame->linesize[1];
            sv += 2 * av_frame->linesize[2];
          } else {
            su += av_frame->linesize[1];
            sv += av_frame->linesize[2];
          }
        }

      } else {

        for (y = 0; y < this->bih.biHeight; y++) {
          xine_fast_memcpy (dy, sy, img->width);
          dy += img->pitches[0];
          sy += av_frame->linesize[0];
        }

        for (y = 0; y < this->bih.biHeight / 2; y++) {
          if (!subsamph) {
            xine_fast_memcpy (du, su, img->width/2);
            xine_fast_memcpy (dv, sv, img->width/2);
          } else {
            int x;
            uint8_t *src;
            uint8_t *dst;
            src = su;
            dst = du;
            for (x = 0; x < (img->width / 2); x++) {
              *dst = *src;
              dst++;
              src += 2;
            }
            src = sv;
            dst = dv;
            for (x = 0; x < (img->width / 2); x++) {
              *dst = *src;
              dst++;
              src += 2;
            }
          }
          du += img->pitches[1];
          dv += img->pitches[2];
          if (subsampv) {
            su += 2*av_frame->linesize[1];
            sv += 2*av_frame->linesize[2];
          } else {
            su += av_frame->linesize[1];
            sv += av_frame->linesize[2];
          }
        }

      }
    }
    break;
  }
}

static void ff_check_bufsize (ff_video_decoder_t *this, int size) {
  if (size > this->bufsize) {
    this->bufsize = size + size / 2;
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	    _("ffmpeg_video_dec: increasing buffer to %d to avoid overflow.\n"),
	    this->bufsize);
    this->buf = realloc(this->buf, this->bufsize + AV_INPUT_BUFFER_PADDING_SIZE );
  }
}

static int ff_vc1_find_header(ff_video_decoder_t *this, buf_element_t *buf)
{
  uint8_t *p = buf->content;

  if (!p[0] && !p[1] && p[2] == 1 && p[3] == 0x0f) {
    int i;

    this->context->extradata = calloc(1, buf->size + AV_INPUT_BUFFER_PADDING_SIZE);
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

#if XFF_PARSE > 1
    AVCodecParserContext *parser_context;
    uint8_t *outbuf;
    int      outsize;

    parser_context = av_parser_init(CODEC_ID_VC1);
    if (!parser_context) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              "ffmpeg_video_dec: couldn't init VC1 parser\n");
      return 1;
    }

    parser_context->flags |= PARSER_FLAG_COMPLETE_FRAMES;
    av_parser_parse2 (parser_context, this->context,
                      &outbuf, &outsize, this->context->extradata, this->context->extradata_size,
                      0, 0, 0);


    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "ffmpeg_video_dec: parsed VC1 video size %dx%d\n",
            this->context->width, this->context->height);

    this->bih.biWidth  = this->context->width;
    this->bih.biHeight = this->context->height;

    av_parser_close(parser_context);
#endif /* XFF_PARSE > 1 */

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
    mpeg_parser_init(this->mpeg_parser, AV_INPUT_BUFFER_PADDING_SIZE);
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
    this->decoder_init_mode = 0;

    if (!this->decoder_ok)
      return;

    init_postprocess(this);
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

      if (this->bih.biSize > (int)sizeof(xine_bmiheader)) {
      this->context->extradata_size = this->bih.biSize - sizeof(xine_bmiheader);
        this->context->extradata = malloc(this->context->extradata_size +
                                          AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(this->context->extradata, this->buf + sizeof(xine_bmiheader),
              this->context->extradata_size);
        memset(this->context->extradata + this->context->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
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
#ifdef XFF_AVCODEC_SUB_ID
        this->context->sub_id = _X_BE_32(&this->buf[30]);
#endif
        this->context->extradata_size = this->size - 26;
	if (this->context->extradata_size < 8) {
	  this->context->extradata_size= 8;
	  this->context->extradata = calloc(1, this->context->extradata_size +
                                            AV_INPUT_BUFFER_PADDING_SIZE);
          ((uint32_t *)this->context->extradata)[0] = 0;
	  if (codec_type == BUF_VIDEO_RV10)
	     ((uint32_t *)this->context->extradata)[1] = 0x10000000;
	  else
	     ((uint32_t *)this->context->extradata)[1] = 0x10003001;
	} else {
          this->context->extradata = malloc(this->context->extradata_size +
                                            AV_INPUT_BUFFER_PADDING_SIZE);
	  memcpy(this->context->extradata, this->buf + 26,
	         this->context->extradata_size);
          memset(this->context->extradata + this->context->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
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
                                      AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(this->context->extradata, buf->decoder_info_ptr[2],
      buf->decoder_info[2]);
    memset(this->context->extradata + this->context->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

  } else if (buf->decoder_info[1] == BUF_SPECIAL_DECODER_CONFIG &&
            !this->context->extradata_size) {

    lprintf("BUF_SPECIAL_DECODER_CONFIG\n");
    this->context->extradata_size = buf->decoder_info[2];
    this->context->extradata = malloc(buf->decoder_info[2] +
                                      AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(this->context->extradata, buf->decoder_info_ptr[2],
      buf->decoder_info[2]);
    memset(this->context->extradata + this->context->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
  }
  else if (buf->decoder_info[1] == BUF_SPECIAL_PALETTE) {
    unsigned int i;
    palette_entry_t *demuxer_palette = (palette_entry_t *)buf->decoder_info_ptr[2];

#if XFF_PALETTE == 1
    AVPaletteControl *decoder_palette = &this->palette_control;

    lprintf ("BUF_SPECIAL_PALETTE\n");
    for (i = 0; i < buf->decoder_info[2]; i++) {
      decoder_palette->palette[i] =
        (demuxer_palette[i].r << 16) |
        (demuxer_palette[i].g <<  8) |
        (demuxer_palette[i].b <<  0);
    }
    decoder_palette->palette_changed = 1;
    this->context->palctrl = decoder_palette;

#elif XFF_PALETTE == 2 || XFF_PALETTE == 3
    lprintf ("BUF_SPECIAL_PALETTE\n");
    for (i = 0; i < buf->decoder_info[2]; i++) {
      this->palette[i] =
        (demuxer_palette[i].r << 16) |
        (demuxer_palette[i].g <<  8) |
        (demuxer_palette[i].b <<  0);
    }
    this->palette_changed = 1;
#endif
  }
  else if (buf->decoder_info[1] == BUF_SPECIAL_RV_CHUNK_TABLE) {
    /* o dear. Multiple decoding threads use individual contexts.
      av_decode_video2 () does only copy the _pointer_ to the offsets,
      not the offsets themselves. So we must not overwrite anything
      that another thread has not yet read. */
    int i, l, total;

    lprintf("BUF_SPECIAL_RV_CHUNK_TABLE\n");
    l = buf->decoder_info[2] + 1;

    total = l * this->class->thread_count;
    if (total < SLICE_OFFSET_SIZE)
      total = SLICE_OFFSET_SIZE;
    if (total > this->slice_offset_size) {
      this->slice_offset_table = realloc (this->slice_offset_table, total * sizeof (int));
      this->slice_offset_size = total;
    }

    if (this->slice_offset_pos + l > this->slice_offset_size)
      this->slice_offset_pos = 0;
    this->context->slice_offset = this->slice_offset_table + this->slice_offset_pos;
    this->context->slice_count = l;

    lprintf ("slice_count=%d\n", l);
    for (i = 0; i < l; i++) {
      this->slice_offset_table[this->slice_offset_pos++] =
        ((uint32_t *)buf->decoder_info_ptr[2])[(2 * i) + 1];
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

static int decode_video_wrapper (ff_video_decoder_t *this,
  AVFrame *av_frame, int *err, void *buf, size_t buf_size) {
  int len;

#if ENABLE_VAAPI
  int locked = 0;
  if (this->accel) {
    locked = this->accel->f->lock_vaapi(this->accel_img);
  }
#endif /* ENABLE_VAAPI */

#if XFF_VIDEO > 1
  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = buf;
  avpkt.size = buf_size;
  avpkt.flags = AV_PKT_FLAG_KEY;

# if XFF_PALETTE == 2 || XFF_PALETTE == 3
  if (buf && this->palette_changed) {
    uint8_t *sd = av_packet_new_side_data (&avpkt, AV_PKT_DATA_PALETTE, 256 * 4);
    if (sd)
      memcpy (sd, this->palette, 256 * 4);
  }
# endif /* XFF_PALETTE */

  this->decode_attempts++;

# if XFF_VIDEO == 3
  {
    int e = AVERROR (EAGAIN);
    if (buf || !this->flush_packet_sent) {
      e = avcodec_send_packet (this->context, &avpkt);
      this->flush_packet_sent = (buf == NULL);
    }
    len = (e == AVERROR (EAGAIN)) ? 0 : buf_size;
    /* that calls av_frame_unref () again but seems safe. */
    *err = avcodec_receive_frame (this->context, av_frame);
  }
# else
  {
    int got_picture = 0;
    len = avcodec_decode_video2 (this->context, av_frame, &got_picture, &avpkt);
    if ((len < 0) || (len > (int)buf_size)) {
      *err = got_picture ? 0 : len;
      len = buf_size;
    } else {
      *err = got_picture ? 0 : AVERROR (EAGAIN);
    }
  }
# endif

# if XFF_PALETTE == 2 || XFF_PALETTE == 3
  if (buf && this->palette_changed) {
    /* Prevent freeing our data buffer */
    avpkt.data = NULL;
    avpkt.size = 0;
#  if XFF_PALETTE == 2
    /* TJ. Oh dear and sigh.
       AVPacket side data handling is broken even in ffmpeg 1.1.1 - see avcodec/avpacket.c
       The suggested av_free_packet () would leave a memory leak here, and
       ff_packet_free_side_data () is private. */
    av_destruct_packet (&avpkt);
#  else /* XFF_PALETTE == 3 */
    XFF_PACKET_UNREF (&avpkt);
#  endif
    this->palette_changed = 0;
  }
# endif /* XFF_PALETTE */

#else /* XFF_VIDEO */
  this->decode_attempts++;
  {
    int got_picture = 0;
    len = avcodec_decode_video (this->context, av_frame, got_picture, buf, buf_size);
    if ((len < 0) || (len > (int)buf_size)) {
      *err = got_picture ? 0 : len;
      len = buf_size;
    } else {
      *err = got_picture ? 0 : AVERROR (EAGAIN);
    }
  }
#endif /* XFF_VIDEO */

#if ENABLE_VAAPI
  if (locked) {
    this->accel->f->unlock_vaapi(this->accel_img);
  }
#endif /* ENABLE_VAAPI */

  return len;
}

static void ff_handle_mpeg12_buffer (ff_video_decoder_t *this, buf_element_t *buf) {

  vo_frame_t *img;
  int         free_img;
  int         len;
  int         offset = 0;
  int         flush = 0;
  int         size = buf->size;

  lprintf("handle_mpeg12_buffer\n");

  if (!this->is_mpeg12) {
    /* initialize mpeg parser */
    ff_init_mpeg12_mode(this);
  }

#ifdef DEBUG_MPEG_PARSER
  printf ("ff_mpeg_parser: buf %d bytes.\n", size);
#endif

  while ((size > 0) || (flush == 1)) {

    uint8_t *current;
    int next_flush, err;
#ifdef XFF_AV_BUFFER
    int need_unref = 0;
#endif

    /* apply valid pts to first frame _starting_ thereafter only */
    if (this->pts && !this->context->reordered_opaque) {
      this->context->reordered_opaque = 
      this->av_frame->reordered_opaque = ff_tag_pts (this, this->pts);
      this->pts = 0;
    }

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
#if XFF_VIDEO > 1
    this->context->skip_frame = (this->skipframes > 0) ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
#else
    this->context->hurry_up = (this->skipframes > 0);
#endif

    lprintf("avcodec_decode_video: size=%d\n", this->mpeg_parser->buffer_size);

    err = 1;
    len = decode_video_wrapper (this, this->av_frame, &err,
      this->mpeg_parser->chunk_buffer, this->mpeg_parser->buffer_size);
#ifdef XFF_AV_BUFFER
    need_unref = 1;
#endif
    lprintf ("avcodec_decode_video: decoded_size=%d, err=%d\n", len, err);
    len = current - buf->content - offset;
    lprintf("avcodec_decode_video: consumed_size=%d\n", len);

    flush = next_flush;

    if ((err < 0) && (err != AVERROR (EAGAIN))) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "ffmpeg_video_dec: error decompressing frame (%d).\n", err);
    }
    size -= len;
    offset += len;

    if (!err && this->class->enable_vaapi) {
      this->bih.biWidth = this->context->width;
      this->bih.biHeight = this->context->height;
    }

    if( this->set_stream_info) {
      set_stream_info(this);
      this->set_stream_info = 0;
    }

    if (!err && this->av_frame->data[0]) {
      /* got a picture, draw it */
      img = NULL;
#ifdef ENABLE_DIRECT_RENDERING
      if (this->av_frame->opaque) {
        ff_saved_frame_t *ffsf = (ff_saved_frame_t *)this->av_frame->opaque;
        img = ffsf->vo_frame;
      }
#endif
      if (!img) {
        /* indirect rendering */
        img = this->stream->video_out->get_frame (this->stream->video_out,
                                                  this->bih.biWidth,
                                                  this->bih.biHeight,
                                                  this->aspect_ratio,
                                                  this->output_format,
                                                  VO_BOTH_FIELDS|this->frame_flags);

        ff_convert_frame(this, img, this->av_frame);

        free_img = 1;
      } else {
        /* DR1 */
        free_img = 0;
      }

      /* transfer some more frame settings for deinterlacing */
      img->progressive_frame = !this->av_frame->interlaced_frame;
      img->top_field_first   = this->av_frame->top_field_first;

      /* get back reordered pts */
      img->pts = ff_untag_pts (this, this->av_frame->reordered_opaque);
      ff_check_pts_tagging (this, this->av_frame->reordered_opaque);
      this->av_frame->reordered_opaque = 0;
      this->context->reordered_opaque = 0;

      if (this->av_frame->repeat_pict)
        img->duration = this->video_step * 3 / 2;
      else
        img->duration = this->video_step;

      this->skipframes = img->draw(img, this->stream);
      this->state = STATE_FRAME_SENT;

      if(free_img)
        img->free(img);

    } else {

      if (
#if XFF_VIDEO > 1
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
#ifdef XFF_AV_BUFFER
    if (need_unref) {
      av_frame_unref (this->av_frame);
      need_unref = 0;
    }
#endif
  }
}

#ifdef HAVE_POSTPROC
static void ff_postprocess (ff_video_decoder_t *this, AVFrame *av_frame, vo_frame_t *img) {
  int qstride, qtype;
  int8_t *qtable;
#ifdef XFF_AV_BUFFER
# if LIBAVUTIL_VERSION_INT < XFF_INT_VERSION(53,0,0)
  qtable = av_frame_get_qp_table (av_frame, &qstride, &qtype);
# else
  /* Why should they keep these long deprecated fields, and remove
    their safe accessor av_frame_get_qp_table () instead?? */
  qtable  = av_frame->qscale_table;
  qstride = av_frame->qstride;
  qtype   = av_frame->qscale_type;
# endif
#else
  qtable  = av_frame->qscale_table;
  qstride = av_frame->qstride;
  qtype   = 0;
#endif
  pp_postprocess ((const uint8_t **)av_frame->data, av_frame->linesize,
                  img->base, img->pitches, this->bih.biWidth, this->bih.biHeight,
                  qtable, qstride, this->our_mode, this->our_context,
                  av_frame->pict_type | (qtype ? PP_PICT_TYPE_QP2 : 0));
}
#endif /* HAVE_POSTPROC */

static void ff_handle_buffer (ff_video_decoder_t *this, buf_element_t *buf) {
  uint8_t *chunk_buf = this->buf;
  AVRational avr00 = {0, 1};

  lprintf("handle_buffer\n");

  if (!this->decoder_ok) {
    if (!this->decoder_init_mode)
      return;

    int codec_type = buf->type & (BUF_MAJOR_MASK | BUF_DECODER_MASK);

    if (!ff_check_extradata(this, codec_type, buf))
      return;

    /* init ffmpeg decoder */
    init_video_codec(this, codec_type);
    this->decoder_init_mode = 0;

    if (!this->decoder_ok)
      return;

    init_postprocess(this);
  }

  if (buf->decoder_flags & BUF_FLAG_FRAME_START) {
    lprintf("BUF_FLAG_FRAME_START\n");
    this->size = 0;
  }

  if (this->size == 0) {
    /* take over pts when we are about to buffer a frame */
    this->av_frame->reordered_opaque = ff_tag_pts(this, this->pts);
    this->context->reordered_opaque = ff_tag_pts(this, this->pts);
    this->pts = 0;
  }

  /* data accumulation */
  if (buf->size > 0) {
    if ((this->size == 0) &&
        ((buf->size + AV_INPUT_BUFFER_PADDING_SIZE) < buf->max_size) &&
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
    int         err, len;
    int         got_one_picture = 0;
    int         offset = 0;
    int         codec_type = buf->type & 0xFFFF0000;
    int         video_step_to_use = this->video_step;
#ifdef XFF_AV_BUFFER
    int         need_unref = 0;
#endif

    /* pad input data */
    /* note: bitstream, alt bitstream reader or something will cause
     * severe mpeg4 artifacts if padding is less than 32 bits.
     */
    memset(chunk_buf + this->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    while (this->size > 0) {

      /* DV frames can be completely skipped */
      if ((codec_type == BUF_VIDEO_DV) && this->skipframes) {
        this->size = 0;
        err = 1;
      } else {
        /* skip decoding b frames if too late */
#if XFF_VIDEO > 1
	this->context->skip_frame = (this->skipframes > 0) ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
#else
        this->context->hurry_up = (this->skipframes > 0);
#endif
        lprintf("buffer size: %d\n", this->size);
#ifdef XFF_AV_BUFFER
        if (need_unref) {
          av_frame_unref (this->av_frame);
          need_unref = 0;
        }
#endif

        len = decode_video_wrapper (this, this->av_frame, &err, &chunk_buf[offset], this->size);
        lprintf ("consumed size: %d, err: %d\n", len, err);

#ifdef XFF_AV_BUFFER
        need_unref = 1;
#endif
        /* reset consumed pts value */
        this->context->reordered_opaque = ff_tag_pts(this, 0);

        if (err) {

          if (err == AVERROR (EAGAIN)) {
            offset += len;
            this->size -= len;
            continue;
          }
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
            "ffmpeg_video_dec: error decompressing frame (%d).\n", err);
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

      if( this->set_stream_info) {
        set_stream_info(this);
        this->set_stream_info = 0;
      }

      if (!err && this->av_frame->data[0]) {
        /* got a picture, draw it */
        got_one_picture = 1;
        img = NULL;
#ifdef ENABLE_DIRECT_RENDERING
        if (this->av_frame->opaque) {
          ff_saved_frame_t *ffsf = (ff_saved_frame_t *)this->av_frame->opaque;
          img = ffsf->vo_frame;
        }
#endif
        if (!img) {
          /* indirect rendering */

          /* prepare for colorspace conversion */
#ifdef ENABLE_VAAPI
          if (this->context->pix_fmt != PIX_FMT_VAAPI_VLD && !this->cs_convert_init)
#else
          if (!this->cs_convert_init)
#endif
          {
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "ff_video_dec: PIX_FMT %d\n", this->context->pix_fmt);
            switch (this->context->pix_fmt) {
              case PIX_FMT_ARGB:
              case PIX_FMT_BGRA:
              case PIX_FMT_RGB24:
              case PIX_FMT_BGR24:
              case PIX_FMT_RGB555BE:
              case PIX_FMT_RGB555LE:
              case PIX_FMT_RGB565BE:
              case PIX_FMT_RGB565LE:
              case PIX_FMT_PAL8:
                this->output_format = XINE_IMGFMT_YUY2;
              break;
              default: ;
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
          img->crop_right  = img->width  - this->bih.biWidth;
          img->crop_bottom = img->height - this->bih.biHeight;
          free_img = 1;
        } else {
          /* DR1 */
          free_img = 0;
        }

        /* post processing */
#ifdef HAVE_POSTPROC
        if(this->pp_quality != this->class->pp_quality && this->context->pix_fmt != PIX_FMT_VAAPI_VLD)
          pp_change_quality(this);

        if(this->pp_available && this->pp_quality && this->context->pix_fmt != PIX_FMT_VAAPI_VLD) {

          if (!free_img) {
            /* DR1: filter into a new frame. Same size to avoid reallcation, just move the
               image to top left corner. */
            img = this->stream->video_out->get_frame (this->stream->video_out,
                                                      img->width,
                                                      img->height,
                                                      this->aspect_ratio,
                                                      this->output_format,
                                                      VO_BOTH_FIELDS|this->frame_flags);
            img->crop_right  = img->width  - this->bih.biWidth;
            img->crop_bottom = img->height - this->bih.biHeight;
            free_img = 1;
          }
          ff_postprocess (this, this->av_frame, img);
        } else
#endif /* HAVE_POSTPROC */
        if (free_img) {
          /* colorspace conversion or copy */
          ff_convert_frame(this, img, this->av_frame);
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

        /* transfer some more frame settings for deinterlacing */
        img->progressive_frame = !this->av_frame->interlaced_frame;
        img->top_field_first   = this->av_frame->top_field_first;

        this->skipframes = img->draw(img, this->stream);
        this->state = STATE_FRAME_SENT;

        if(free_img)
          img->free(img);
      }
    }

    /* workaround for demux_mpeg_pes sending fields as frames:
     * do not generate a bad frame for the first field picture
     */
    if (!got_one_picture && this->use_bad_frames && (this->size || this->video_step || this->assume_bad_field_picture)) {
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
      img->crop_right  = this->bih.biWidth  <= 0 ? 0 : (img->width  - this->bih.biWidth);
      img->crop_bottom = this->bih.biHeight <= 0 ? 0 : (img->height - this->bih.biHeight);

      img->bad_frame = 1;
      this->skipframes = img->draw(img, this->stream);
      img->free(img);
      this->state = STATE_FRAME_SENT;
    }

    this->assume_bad_field_picture = !got_one_picture;

#ifdef XFF_AV_BUFFER
    if (need_unref) {
      av_frame_unref (this->av_frame);
      need_unref = 0;
    }
#endif
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

      this->state = STATE_READING_DATA;
      if ((buf->type & 0xFFFF0000) == BUF_VIDEO_MPEG) {
	ff_handle_mpeg12_buffer(this, buf);
      } else {
	ff_handle_buffer(this, buf);
      }

    }
  }
}

static void ff_flush_internal (ff_video_decoder_t *this, int display) {
  vo_frame_t *img;
  int         free_img, frames = 0;
  int         video_step_to_use = this->video_step;
  AVRational  avr00 = {0, 1};

  /* This is a stripped version of ff_handle_buffer (). It shall return yet undisplayed frames. */

  if (!this->context || !this->decoder_ok || this->state == STATE_FLUSHED)
    return;
  /* For some reason, we are flushed right while reading the first frame?? */
  if (!this->decode_attempts)
    return;
  this->state = STATE_FLUSHED;

  while (1) {
    int err = 1;
    decode_video_wrapper (this, this->av_frame2, &err, NULL, 0);
    if (err || !this->av_frame2->data[0]) {
#ifdef XFF_AV_BUFFER
      av_frame_unref (this->av_frame2);
#endif
      break;
    }

    frames++;
    if (!display) {
#ifdef XFF_AV_BUFFER
      av_frame_unref (this->av_frame2);
#endif
      continue;
    }

    /* All that jizz just to view the last 2 frames of a stream ;-) */
    video_step_to_use = this->video_step || !this->context->time_base.den ?
      this->video_step :
      90000ll * this->context->ticks_per_frame * this->context->time_base.num /
        this->context->time_base.den;

    if ((this->aspect_ratio_prio < 2) && av_cmp_q (this->context->sample_aspect_ratio, avr00)) {
      if (!this->bih.biWidth || !this->bih.biHeight) {
        this->bih.biWidth  = this->context->width;
        this->bih.biHeight = this->context->height;
      }
      this->aspect_ratio = av_q2d(this->context->sample_aspect_ratio) *
        (double)this->bih.biWidth / (double)this->bih.biHeight;
      this->aspect_ratio_prio = 2;
      set_stream_info (this);
    }

    if (this->set_stream_info) {
      set_stream_info (this);
      this->set_stream_info = 0;
    }

    img = NULL;
#ifdef ENABLE_DIRECT_RENDERING
    if (this->av_frame->opaque) {
      ff_saved_frame_t *ffsf = (ff_saved_frame_t *)this->av_frame->opaque;
      img = ffsf->vo_frame;
    }
#endif
    if (!img) {
      /* indirect rendering */
      if (this->aspect_ratio_prio == 0) {
        this->aspect_ratio = (double)this->bih.biWidth / (double)this->bih.biHeight;
        this->aspect_ratio_prio = 1;
        lprintf ("default aspect ratio: %f\n", this->aspect_ratio);
        set_stream_info (this);
      }
      /* xine-lib expects the framesize to be a multiple of 16x16 (macroblock) */
      img = this->stream->video_out->get_frame (this->stream->video_out,
        (this->bih.biWidth  + 15) & ~15, (this->bih.biHeight + 15) & ~15,
        this->aspect_ratio, this->output_format, VO_BOTH_FIELDS | this->frame_flags);
      img->crop_right  = img->width  - this->bih.biWidth;
      img->crop_bottom = img->height - this->bih.biHeight;
      free_img = 1;
    } else {
      /* DR1 */
      free_img = 0;
    }

    /* post processing */
#ifdef HAVE_POSTPROC
    if (this->pp_quality != this->class->pp_quality && this->context->pix_fmt != PIX_FMT_VAAPI_VLD)
      pp_change_quality (this);
    if (this->pp_available && this->pp_quality && this->context->pix_fmt != PIX_FMT_VAAPI_VLD) {
      if (!free_img) {
        /* DR1: filter into a new frame. Same size to avoid reallcation, just move the
           image to top left corner. */
        img = this->stream->video_out->get_frame (this->stream->video_out, img->width, img->height,
          this->aspect_ratio, this->output_format, VO_BOTH_FIELDS | this->frame_flags);
        img->crop_right  = img->width  - this->bih.biWidth;
        img->crop_bottom = img->height - this->bih.biHeight;
        free_img = 1;
      }
      ff_postprocess (this, this->av_frame2, img);
    } else
#endif /* HAVE_POSTPROC */
    if (free_img) {
      /* colorspace conversion or copy */
      ff_convert_frame (this, img, this->av_frame2);
    }

    img->pts = ff_untag_pts (this, this->av_frame2->reordered_opaque);
    ff_check_pts_tagging (this, this->av_frame2->reordered_opaque);

    if (video_step_to_use == 750)
      video_step_to_use = 0;
    img->duration = this->av_frame2->repeat_pict ? video_step_to_use * 3 / 2 : video_step_to_use;
    img->progressive_frame = !this->av_frame2->interlaced_frame;
    img->top_field_first   = this->av_frame2->top_field_first;

    this->skipframes = img->draw (img, this->stream);
    if (free_img)
      img->free (img);

#ifdef XFF_AV_BUFFER
    av_frame_unref (this->av_frame2);
#endif
  }

  this->decode_attempts = 0;

  if (frames)
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "ffmpeg_video_dec: flushed out %s%d frames.\n",
      display ? "and displayed " : "", frames);
}

#ifdef ENABLE_DIRECT_RENDERING
static void ff_free_dr1_frames (ff_video_decoder_t *this, int all) {
  int frames;
  /* Some codecs (wmv2, vp6, svq3...) are hard-wired to a few reference frames.
     They will only be replaced when new ones arrive, and freed on codec close.
     They also have no AVCodec.flush () callback for manual freeing (that is,
     avcodec_flush_buffers () does nothing).
     Even worse: multithreading seems to always do it like that...
     So lets tolerate this behaviour on plain stream seek. */
  if (!all) {
    frames = this->ffsf_num;
    if (!frames)
      return;
    if (frames < 12) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "ffmpeg_video_dec: tolerating %d held DR1 frames.\n", frames);
      return;
    }
  }
  /* frame garbage collector here - workaround for buggy ffmpeg codecs that
   * don't release their DR1 frames */
  /* TJ. A note on libavcodec v55.
     Looks like I found a way not to get to this point.
     If it should happen anyway, we may earn some small memory leaks here.
     Using the old get_buffer () API emulation makes even bigger leaks
     (per frame, +3*AVBufferRef, +1*AVBuffer, +1*AVCodecContext, +1*AVFrame).
     Tracking and killing the AVBufferRef's themselves would risk heap
     corruption and segfaults.
     Now tell me whether I really sigh too much... */
  pthread_mutex_lock (&this->ffsf_mutex);
  frames = 0;
  while (!DLIST_IS_EMPTY (&this->ffsf_used)) {
    ff_saved_frame_t *ffsf = (ff_saved_frame_t *)this->ffsf_used.head;
    if (ffsf->vo_frame) {
      ffsf->vo_frame->free (ffsf->vo_frame);
      frames++;
    }
    DLIST_REMOVE (&ffsf->node);
    DLIST_ADD_TAIL (&ffsf->node, &this->ffsf_free);
    this->ffsf_num--;
  }
  pthread_mutex_unlock (&this->ffsf_mutex);
  /* we probably never get this. */
  if (frames)
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "ffmpeg_video_dec: freed %d orphaned DR1 frames.\n", frames);
}
#endif

static void ff_flush (video_decoder_t *this_gen) {
  ff_video_decoder_t *this = (ff_video_decoder_t *) this_gen;

  lprintf ("ff_flush\n");
  if (this->state == STATE_FRAME_SENT)
    ff_flush_internal (this, 1);
}

static void ff_reset (video_decoder_t *this_gen) {
  ff_video_decoder_t *this = (ff_video_decoder_t *) this_gen;

  lprintf ("ff_reset\n");

  this->size = 0;
  this->state = STATE_RESET;

  if(this->context && this->decoder_ok)
  {
    /* Discard any undisplayed frames. */
    ff_flush_internal (this, 0);
    /* Ask decoder to free held reference frames (which it may ignore). */
    avcodec_flush_buffers(this->context);
#ifdef ENABLE_DIRECT_RENDERING
    /* Free obviously too many DR1 frames. */
    ff_free_dr1_frames (this, 0);
#endif
  }

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
  this->state = STATE_DISCONTINUITY;

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

  ff_flush_internal (this, 0);

  rgb2yuy2_free (this->rgb2yuy2);

  if (this->decoder_ok) {

    pthread_mutex_lock(&ffmpeg_lock);
    avcodec_close (this->context);
    pthread_mutex_unlock(&ffmpeg_lock);

#ifdef ENABLE_DIRECT_RENDERING
    ff_free_dr1_frames (this, 1);
#endif

    this->stream->video_out->close(this->stream->video_out, this->stream);
    this->decoder_ok = 0;
  }

  if (this->slice_offset_table)
    free (this->slice_offset_table);

  if (this->context) {
    _x_freep (&this->context->extradata);
    this->context->extradata_size = 0;
    XFF_FREE_CONTEXT (this->context);
  }

  if( this->av_frame )
    XFF_FREE_FRAME( this->av_frame );
  if (this->av_frame2)
    XFF_FREE_FRAME (this->av_frame2);

  if (this->buf)
    free(this->buf);
  this->buf = NULL;

#ifdef HAVE_POSTPROC
  if(this->our_context)
    pp_free_context(this->our_context);

  if(this->our_mode)
    pp_free_mode(this->our_mode);
#endif /* HAVE_POSTPROC */

  mpeg_parser_dispose(this->mpeg_parser);

#ifdef ENABLE_DIRECT_RENDERING
  while (!DLIST_IS_EMPTY (&this->ffsf_free)) {
    ff_saved_frame_t *ffsf = (ff_saved_frame_t *)this->ffsf_free.head;
    DLIST_REMOVE (&ffsf->node);
    free (ffsf);
  }
  if (this->ffsf_total)
    xprintf (this->class->xine, XINE_VERBOSITY_LOG,
      _("ffmpeg_video_dec: used %d DR1 frames.\n"), this->ffsf_total);
  pthread_mutex_destroy (&this->ffsf_mutex);
#endif

#ifdef ENABLE_VAAPI
  if(this->accel_img)
    this->accel_img->free(this->accel_img);
#endif

  free (this_gen);
}

static video_decoder_t *ff_video_open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  ff_video_decoder_t  *this ;
  AVCodec             *codec = NULL;
  uint32_t             video_type;
  size_t               i;

  lprintf ("open_plugin\n");

  /* check for codec support */
  video_type = BUF_VIDEO_BASE | (_x_get_video_streamtype(stream) << 16);
  for (i = 0; i < sizeof(ff_video_lookup)/sizeof(ff_codec_t); i++) {
    if(ff_video_lookup[i].type == video_type) {
      pthread_mutex_lock(&ffmpeg_lock);
      codec = avcodec_find_decoder(ff_video_lookup[i].id);
      pthread_mutex_unlock(&ffmpeg_lock);
      _x_meta_info_set_utf8(stream, XINE_META_INFO_VIDEOCODEC, ff_video_lookup[i].name);
      break;
    }
  }
  if (!codec) {
    xprintf (stream->xine, XINE_VERBOSITY_LOG,
             _("ffmpeg_video_dec: couldn't find ffmpeg decoder for buf type 0x%X\n"),
             video_type);
    return NULL;
  }
  lprintf("lavc decoder found\n");

  this = calloc(1, sizeof (ff_video_decoder_t));
  if (!this)
    return NULL;

  /* Do these first, when compiler still knows stream is all zeroed.
   * Let it optimize away this on most systems where clear mem
   * interpretes as 0, 0f or NULL safely.
   */
  this->size            = 0;
  this->decoder_ok      = 0;
  this->is_mpeg12       = 0;
  this->aspect_ratio    = 0;
#ifdef HAVE_POSTPROC
  this->pp_quality      = 0;
  this->our_context     = NULL;
  this->our_mode        = NULL;
#endif
  this->mpeg_parser     = NULL;
  this->set_stream_info = 0;
  this->rgb2yuy2        = NULL;
#ifdef ENABLE_VAAPI
  this->accel           = NULL;
  this->accel_img       = NULL;
#endif
#if XFF_VIDEO == 3
  this->flush_packet_sent = 0;
#endif

  this->video_decoder.decode_data   = ff_decode_data;
  this->video_decoder.flush         = ff_flush;
  this->video_decoder.reset         = ff_reset;
  this->video_decoder.discontinuity = ff_discontinuity;
  this->video_decoder.dispose       = ff_dispose;

  this->stream = stream;
  this->class  = (ff_video_class_t *)class_gen;
  this->codec  = codec;

  this->bufsize = VIDEOBUFSIZE;
  do {
    this->buf = malloc (VIDEOBUFSIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    if (this->buf) {
      this->av_frame = XFF_ALLOC_FRAME ();
      if (this->av_frame) {
        this->av_frame2 = XFF_ALLOC_FRAME ();
        if (this->av_frame2) {
          this->context = XFF_ALLOC_CONTEXT ();
          if (this->context)
            break;
          XFF_FREE_FRAME (this->av_frame2);
        }
        XFF_FREE_FRAME (this->av_frame);
      }
      free (this->buf);
    }
    free (this);
    return NULL;
  } while (0);

  this->decoder_init_mode = 1;
  this->context->opaque   = this;
#if XFF_PALETTE == 1
  this->context->palctrl  = NULL;
#endif

#ifdef ENABLE_DIRECT_RENDERING
  DLIST_INIT (&this->ffsf_free);
  DLIST_INIT (&this->ffsf_used);
  pthread_mutex_init (&this->ffsf_mutex, NULL);
#endif

#ifdef ENABLE_EMMS
  this->use_emms = !!(xine_mm_accel () & (MM_ACCEL_X86_MMX | MM_ACCEL_X86_MMXEXT));
#endif

  this->pix_fmt   = -1;
#ifdef LOG
  this->debug_fmt = -1;
#endif

#ifdef ENABLE_VAAPI
  if (this->class->enable_vaapi && (stream->video_out->get_capabilities(stream->video_out) & VO_CAP_VAAPI)) {
    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("ffmpeg_video_dec: vaapi_mpeg_softdec %d\n"),
          this->class->vaapi_mpeg_softdec );

    this->accel_img  = stream->video_out->get_frame( stream->video_out, 1920, 1080, 1, XINE_IMGFMT_VAAPI,
                                                     VO_BOTH_FIELDS | VO_GET_FRAME_MAY_FAIL );

    if( this->accel_img ) {
      this->accel = (vaapi_accel_t*)this->accel_img->accel_data;
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("ffmpeg_video_dec: VAAPI Enabled in config.\n"));
    } else {
      this->class->enable_vaapi = 0;
      xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("ffmpeg_video_dec: VAAPI Enabled disabled by driver.\n"));
    }
  } else {
    this->class->enable_vaapi = 0;
    this->class->vaapi_mpeg_softdec = 0;
    xprintf(this->class->xine, XINE_VERBOSITY_LOG, _("ffmpeg_video_dec: VAAPI Enabled disabled by driver.\n"));
  }
#endif

  return &this->video_decoder;
}

static void dispose_video_class (video_decoder_class_t *this_gen) {

  ff_video_class_t *this = (ff_video_class_t *)this_gen;
  config_values_t *config = this->xine->config;

  config->unregister_callbacks (config, NULL, NULL, this, sizeof (*this));

  free (this);
}

void *init_video_plugin (xine_t *xine, const void *data) {

  ff_video_class_t *this;
  config_values_t  *config;

  (void)data;

  this = calloc(1, sizeof (ff_video_class_t));
  if (!this) {
    return NULL;
  }

  this->decoder_class.open_plugin     = ff_video_open_plugin;
  this->decoder_class.identifier      = "ffmpeg video";
  this->decoder_class.description     = N_("ffmpeg based video decoder plugin");
  this->decoder_class.dispose         = dispose_video_class;
  this->xine                          = xine;

  pthread_once( &once_control, init_once_routine );

  /* Configuration for post processing quality - default to mid (3) for the
   * moment */
  config = xine->config;

#ifdef HAVE_POSTPROC
  this->pp_quality = xine->config->register_range(config, "video.processing.ffmpeg_pp_quality", 3,
    0, PP_QUALITY_MAX,
    _("MPEG-4 postprocessing quality"),
    _("You can adjust the amount of post processing applied to MPEG-4 video.\n"
      "Higher values result in better quality, but need more CPU. Lower values may "
      "result in image defects like block artifacts. For high quality content, "
      "too heavy post processing can actually make the image worse by blurring it "
      "too much."),
    10, pp_quality_cb, this);
#endif /* HAVE_POSTPROC */

  this->thread_count = xine->config->register_num(config, "video.processing.ffmpeg_thread_count", 1,
    _("FFmpeg video decoding thread count"),
    _("You can adjust the number of video decoding threads which FFmpeg may use.\n"
      "Higher values should speed up decoding but it depends on the codec used "
      "whether parallel decoding is supported. A rule of thumb is to have one "
      "decoding thread per logical CPU (typically 1 to 4).\n"
      "A change of this setting will take effect with playing the next stream."),
    10, thread_count_cb, this);
  if (this->thread_count < 1)
    this->thread_count = 1;
  else if (this->thread_count > 8)
    this->thread_count = 8;

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

  this->enable_dri = xine->config->register_bool(config, "video.processing.ffmpeg_direct_rendering", 1,
    _("Enable direct rendering"),
    _("Disable direct rendering if you are experiencing lock-ups with\n"
      "streams with lot of reference frames."),
    10, dri_cb, this);

#ifdef ENABLE_VAAPI
  this->vaapi_mpeg_softdec = xine->config->register_bool(config, "video.processing.vaapi_mpeg_softdec", 0,
    _("VAAPI Mpeg2 softdecoding"),
    _("If the machine freezes on mpeg2 decoding use mpeg2 software decoding."),
    10, vaapi_mpeg_softdec_func, this);

  this->enable_vaapi = xine->config->register_bool(config, "video.processing.ffmpeg_enable_vaapi", 1,
    _("Enable VAAPI"),
    _("Enable or disable usage of vaapi"),
    10, vaapi_enable_vaapi, this);
#endif /* ENABLE_VAAPI */

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

const decoder_info_t dec_info_ffmpeg_video = {
  supported_video_types,   /* supported types */
  6                        /* priority        */
};

const decoder_info_t dec_info_ffmpeg_wmv8 = {
  wmv8_video_types,        /* supported types */
  0                        /* priority        */
};

const decoder_info_t dec_info_ffmpeg_wmv9 = {
  wmv9_video_types,        /* supported types */
  0                        /* priority        */
};
