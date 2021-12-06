/* kate: space-indent on; indent-width 2; mixedindent off; indent-mode cstyle; remove-trailing-space on;
 * Copyright (C) 2008-2021 the xine project
 * Copyright (C) 2008 Christophe Thommeret <hftom@free.fr>
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
 * alterh264_decode.c, a H264 video stream parser using VDPAU hardware decoder
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

//#define LOG
#define LOG_MODULE "vdpau_h264"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include "accel_vdpau.h"
#include <vdpau/vdpau.h>

#include "group_vdpau.h"
#include "vdec_hw_h264.h"

typedef struct {
  video_decoder_t video_decoder;        /* parent video decoder structure */

  xine_stream_t *stream;

  vdec_hw_h264_t *vdec;

  vdpau_accel_t *accel;

  VdpDecoderProfile profile;
  int vdp_runtime_nr;
  vdpau_accel_t *accel_vdpau;
  VdpDecoder decoder;
  VdpDecoderProfile decoder_profile;

  int decoder_width;
  int decoder_height;

  double reported_ratio;
  int reported_video_step;
  int reported_width;
  int reported_height;
} vdpau_h264_alter_decoder_t;

static VdpDecoderProfile vdpau_h264_map_profile (int profile_idc) {
  /* nvidia's vdpau doesn't suppot baseline (66), force main (77) */
  return profile_idc >= 100 ? VDP_DECODER_PROFILE_H264_HIGH : VDP_DECODER_PROFILE_H264_MAIN;
}

static __attribute__((format (printf, 3, 4))) int vdpau_h264_alter_logg (void *user_data,
  vdec_hw_h264_logg_t level, const char *fmt, ...) {
  char b[2048];
  vdpau_h264_alter_decoder_t *this = (vdpau_h264_alter_decoder_t *)user_data;
  int l2 = level == VDEC_HW_H264_LOGG_ERR ? XINE_VERBOSITY_LOG
         : level == VDEC_HW_H264_LOGG_INFO ? XINE_VERBOSITY_DEBUG
         : /* VDEC_HW_H264_LOGG_DEBUG */ XINE_VERBOSITY_DEBUG + 1;

  if (l2 >= this->stream->xine->verbosity) {
    va_list va;

    va_start (va, fmt);
    vsnprintf (b, sizeof (b), fmt, va);
    va_end (va);
    xprintf (this->stream->xine, l2, LOG_MODULE ": %s", b);
    return 1;
  }
  return 0;
}

static int vdpau_h264_alter_frame_new (void *user_data, vdec_hw_h264_frame_t *frame) {
  vdpau_h264_alter_decoder_t * this = (vdpau_h264_alter_decoder_t *)user_data;
  int flags = (frame->flags & VDEC_HW_H264_FRAME_TOP_FIELD ? VO_TOP_FIELD : 0)
            | (frame->flags & VDEC_HW_H264_FRAME_BOTTOM_FIELD ? VO_BOTTOM_FIELD : 0)
            | (frame->flags & VDEC_HW_H264_FRAME_NEW_SEQ ? VO_NEW_SEQUENCE_FLAG : 0);
  vo_frame_t *img;

  VO_SET_FLAGS_CM (frame->color_matrix, flags);
  frame->user_data = img = this->stream->video_out->get_frame (this->stream->video_out,
    frame->width, frame->height, frame->ratio, XINE_IMGFMT_VDPAU, flags);
    img->progressive_frame = -1;
  img->pts = frame->pts;
  img->duration = frame->duration;
  img->progressive_frame = frame->progressive_frame;
  img->bad_frame = frame->bad_frame;
  return 1;
}

static int vdpau_h264_alter_frame_render (void *user_data, vdec_hw_h264_frame_t *frame) {
  vdpau_h264_alter_decoder_t *this = (vdpau_h264_alter_decoder_t *)user_data;
  vo_frame_t *img = (vo_frame_t *)frame->user_data;
  vdpau_accel_t *accel;
  VdpPictureInfoH264 info;
  VdpDecoderProfile profile;
  VdpStatus st;

  if (!img)
    return 0;
  accel = (vdpau_accel_t *)img->accel_data;

  if (!this->accel_vdpau)
    this->accel_vdpau = accel;

  if (this->vdp_runtime_nr != *(this->accel_vdpau->current_vdp_runtime_nr))
    this->decoder = VDP_INVALID_HANDLE;

  profile = vdpau_h264_map_profile (frame->profile);

  if ((this->decoder == VDP_INVALID_HANDLE) || (this->decoder_profile != profile)
    || (this->decoder_width != frame->width) || (this->decoder_height != frame->height)) {
    if (accel->lock)
      accel->lock (accel->vo_frame);
    if (this->decoder != VDP_INVALID_HANDLE) {
      accel->vdp_decoder_destroy (this->decoder);
      this->decoder = VDP_INVALID_HANDLE;
    }
    st = accel->vdp_decoder_create (accel->vdp_device, profile,
        frame->width, frame->height, frame->num_ref_frames, &this->decoder);
    if (accel->unlock)
      accel->unlock (accel->vo_frame);
    if (st != VDP_STATUS_OK) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
        LOG_MODULE ": failed to create decoder !! %s\n", accel->vdp_get_error_string (st));
    } else {
      this->decoder_profile = profile;
      this->decoder_width = frame->width;
      this->decoder_height = frame->height;
      this->vdp_runtime_nr = accel->vdp_runtime_nr;
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        LOG_MODULE ": created decoder for %dx%d %s @#%d.\n",
        this->decoder_width, this->decoder_height,
        this->decoder_profile == VDP_DECODER_PROFILE_H264_HIGH ? "high" : "main",
        this->vdp_runtime_nr);
    }
  }

  info.slice_count                            = frame->info->slice_count;
  info.field_order_cnt[0]                     = frame->info->field_order_cnt[0];
  info.field_order_cnt[1]                     = frame->info->field_order_cnt[1];
  info.is_reference                           = frame->info->is_reference ? VDP_TRUE : VDP_FALSE;
  info.frame_num                              = frame->info->frame_num;
  info.field_pic_flag                         = frame->info->field_pic_flag;
  info.bottom_field_flag                      = frame->info->bottom_field_flag;
  info.num_ref_frames                         = frame->info->num_ref_frames;
  info.mb_adaptive_frame_field_flag           = frame->info->mb_adaptive_frame_field_flag;
  info.constrained_intra_pred_flag            = frame->info->constrained_intra_pred_flag;
  info.weighted_pred_flag                     = frame->info->weighted_pred_flag;
  info.weighted_bipred_idc                    = frame->info->weighted_bipred_idc;
  info.frame_mbs_only_flag                    = frame->info->frame_mbs_only_flag;
  info.transform_8x8_mode_flag                = frame->info->transform_8x8_mode_flag;
  info.chroma_qp_index_offset                 = frame->info->chroma_qp_index_offset;
  info.second_chroma_qp_index_offset          = frame->info->second_chroma_qp_index_offset;
  info.pic_init_qp_minus26                    = frame->info->pic_init_qp_minus26;
  info.num_ref_idx_l0_active_minus1           = frame->info->num_ref_idx_l0_active_minus1;
  info.num_ref_idx_l1_active_minus1           = frame->info->num_ref_idx_l1_active_minus1;
  info.log2_max_frame_num_minus4              = frame->info->log2_max_frame_num_minus4;
  info.pic_order_cnt_type                     = frame->info->pic_order_cnt_type;
  info.log2_max_pic_order_cnt_lsb_minus4      = frame->info->log2_max_pic_order_cnt_lsb_minus4;
  info.delta_pic_order_always_zero_flag       = frame->info->delta_pic_order_always_zero_flag;
  info.direct_8x8_inference_flag              = frame->info->direct_8x8_inference_flag;
  info.entropy_coding_mode_flag               = frame->info->entropy_coding_mode_flag;
  info.pic_order_present_flag                 = frame->info->pic_order_present_flag;
  info.deblocking_filter_control_present_flag = frame->info->deblocking_filter_control_present_flag;
  info.redundant_pic_cnt_present_flag         = frame->info->redundant_pic_cnt_present_flag;

  xine_fast_memcpy (info.scaling_lists_4x4, frame->info->scaling_lists_4x4, sizeof (info.scaling_lists_4x4));
  xine_fast_memcpy (info.scaling_lists_8x8, frame->info->scaling_lists_8x8, sizeof (info.scaling_lists_8x8));

  {
    uint32_t u;

    for (u = 0; u < sizeof (info.referenceFrames) / sizeof (info.referenceFrames[0]); u++) {
      if (frame->info->referenceFrames[u].frame) {
        vo_frame_t *rimg = (vo_frame_t *)frame->info->referenceFrames[u].frame->user_data;
        vdpau_accel_t *accel = (vdpau_accel_t *)rimg->accel_data;

        info.referenceFrames[u].surface = accel->surface;
      } else {
        info.referenceFrames[u].surface = VDP_INVALID_HANDLE;
      }
      info.referenceFrames[u].is_long_term = 0;
      info.referenceFrames[u].frame_idx = frame->info->referenceFrames[u].frame_idx;
      info.referenceFrames[u].top_is_reference = frame->info->referenceFrames[u].top_is_reference ? VDP_TRUE : VDP_FALSE;
      info.referenceFrames[u].bottom_is_reference = frame->info->referenceFrames[u].bottom_is_reference ? VDP_TRUE : VDP_FALSE;
      info.referenceFrames[u].field_order_cnt[0] = frame->info->referenceFrames[u].field_order_cnt[0];
      info.referenceFrames[u].field_order_cnt[1] = frame->info->referenceFrames[u].field_order_cnt[1];
    }
  }

  {
    const uint8_t sc[3] = { 0, 0, 1 };
    VdpBitstreamBuffer vbits[80 * 2];
    uint32_t u, m = frame->info->slice_count < 80 ? frame->info->slice_count : 80;

    for (u = 0; u < m; u++) {
      vbits[u * 2].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
      vbits[u * 2].bitstream = sc;
      vbits[u * 2].bitstream_bytes = 3;
      vbits[u * 2 + 1].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
      vbits[u * 2 + 1].bitstream = frame->info->slices_bitstream[u];
      vbits[u * 2 + 1].bitstream_bytes = frame->info->slices_bytes[u];
    }
    if (accel->lock)
      accel->lock (accel->vo_frame);
    st = accel->vdp_decoder_render (this->decoder, accel->surface,
      CAST_VdpPictureInfo_PTR &info, m * 2, vbits);
    if (accel->unlock)
      accel->unlock (accel->vo_frame);
  }
  if (st != VDP_STATUS_OK)
    lprintf ("**********************DECODING failed! - surface = %d - %s\n",
             accel->surface, accel->vdp_get_error_string (st));
  else
    lprintf ("**********************DECODING success! - surface = %d\n",
             accel->surface);

  if ((frame->ratio != this->reported_ratio)
    || (frame->width != this->reported_width)
    || (frame->height != this->reported_height)
    || (frame->duration != this->reported_video_step)) {
    xine_event_t event;
    xine_format_change_data_t data;

    this->reported_ratio = frame->ratio;
    this->reported_width = frame->width;
    this->reported_height = frame->height;
    this->reported_video_step = frame->duration;
    _x_stream_info_set (this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, frame->width);
    _x_stream_info_set (this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, frame->height);
    _x_stream_info_set (this->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double) 10000 * frame->ratio));
    _x_stream_info_set (this->stream, XINE_STREAM_INFO_FRAME_DURATION, frame->duration);
    _x_meta_info_set_utf8 (this->stream, XINE_META_INFO_VIDEOCODEC, "H264/AVC (vdpau_alter)");

    event.type = XINE_EVENT_FRAME_FORMAT_CHANGE;
    event.stream = this->stream;
    event.data = &data;
    event.data_length = sizeof (data);
    data.width = frame->width;
    data.height = frame->height;
    data.aspect = frame->ratio;
    xine_event_send (this->stream, &event);
  }
  return 1;
}

static int vdpau_h264_alter_frame_ready (void *user_data, vdec_hw_h264_frame_t *frame) {
  vdpau_h264_alter_decoder_t *this = (vdpau_h264_alter_decoder_t *)user_data;
  vo_frame_t *img = (vo_frame_t *)frame->user_data;

  if (!img)
    return 0;

  img->pts = frame->pts;
  img->top_field_first = frame->top_field_first;
  img->draw (img, this->stream);
  return 1;
}

static void vdpau_h264_alter_frame_delete (void *user_data, vdec_hw_h264_frame_t *frame) {
  vdpau_h264_alter_decoder_t *this = (vdpau_h264_alter_decoder_t *)user_data;
  vo_frame_t *img = (vo_frame_t *)frame->user_data;

  (void)this;
  if (img) {
    img->free (img);
    frame->user_data = NULL;
  }
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void vdpau_h264_alter_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  vdpau_h264_alter_decoder_t *this = (vdpau_h264_alter_decoder_t *)this_gen;

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    lprintf ("BUF_FLAG_FRAMERATE\n");
    vdec_hw_h264_put_container_info (this->vdec, 0, 0, buf->decoder_info[0], 0);
  }

  if (buf->decoder_flags & BUF_FLAG_ASPECT) {
    lprintf ("BUF_FLAG_ASPECT\n");
    vdec_hw_h264_put_container_info (this->vdec, 0, 0, 0, (double)buf->decoder_info[1] / (double) buf->decoder_info[2]);
  }

  if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    xine_bmiheader *bih = (xine_bmiheader *) buf->content;
    uint8_t *codec_private = buf->content + sizeof (xine_bmiheader);
    uint32_t codec_private_len = bih->biSize - sizeof (xine_bmiheader);

    lprintf ("BUF_FLAG_STDHEADER\n");
    vdec_hw_h264_put_container_info (this->vdec, bih->biWidth, bih->biHeight, 0, 0);
    vdec_hw_h264_put_config (this->vdec, codec_private, codec_private_len);
    return;
  }

  if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
    if (buf->decoder_info[1] == BUF_SPECIAL_DECODER_CONFIG) {
      const uint8_t *codec_private = buf->decoder_info_ptr[2];
      uint32_t codec_private_len = buf->decoder_info[2];

      lprintf ("BUF_SPECIAL_DECODER_CONFIG\n");
      vdec_hw_h264_put_config (this->vdec, codec_private, codec_private_len);
    }
    return;
  }

  if (!buf->size)
    return;

  vdec_hw_h264_put_frame (this->vdec, buf->pts, (const uint8_t *)buf->content, buf->size,
    !!(buf->decoder_flags & BUF_FLAG_FRAME_END));
}


/*
 * This function is called when xine needs to flush the system.
 */
static void vdpau_h264_alter_flush (video_decoder_t *this_gen) {
  vdpau_h264_alter_decoder_t *this = (vdpau_h264_alter_decoder_t *) this_gen;

  lprintf ("vdpau_h264_alter_flush\n");

  vdec_hw_h264_flush (this->vdec);
}

/*
 * This function resets the video decoder.
 */
static void vdpau_h264_alter_reset (video_decoder_t *this_gen) {
  vdpau_h264_alter_decoder_t *this = (vdpau_h264_alter_decoder_t *) this_gen;

  lprintf ("vdpau_h264_alter_reset\n");

  vdec_hw_h264_reset (this->vdec);
}

/*
 * The decoder should forget any stored pts values here.
 */
static void vdpau_h264_alter_discontinuity (video_decoder_t *this_gen) {
  vdpau_h264_alter_decoder_t *this = (vdpau_h264_alter_decoder_t *) this_gen;

  lprintf ("vdpau_h264_alter_discontinuity\n");

  vdec_hw_h264_zero_pts (this->vdec);
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void vdpau_h264_alter_dispose (video_decoder_t *this_gen) {
  vdpau_h264_alter_decoder_t *this = (vdpau_h264_alter_decoder_t *) this_gen;

  lprintf ("vdpau_h264_alter_dispose\n");

  vdec_hw_h264_delete (&this->vdec);

  if ((this->decoder != VDP_INVALID_HANDLE) && this->accel_vdpau) {
    if (this->accel_vdpau->lock)
      this->accel_vdpau->lock (this->accel_vdpau->vo_frame);
    this->accel_vdpau->vdp_decoder_destroy (this->decoder);
    this->decoder = VDP_INVALID_HANDLE;
    if (this->accel_vdpau->unlock)
      this->accel_vdpau->unlock (this->accel_vdpau->vo_frame);
  }

  this->stream->video_out->close (this->stream->video_out, this->stream);

  free (this_gen);
}


/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {
  vdpau_h264_alter_decoder_t *this;
  vo_frame_t *img;
  vdpau_accel_t *accel;
  int runtime_nr;
  VdpDecoder decoder;
  VdpStatus st;
  vdec_hw_h264_t *vdec;

  (void)class_gen;
  /* the videoout must be vdpau-capable to support this decoder */
  if (!(stream->video_out->get_capabilities (stream->video_out) & VO_CAP_VDPAU_H264))
    return NULL;

  /* now check if vdpau has free decoder resource */
  img = stream->video_out->get_frame (stream->video_out, 1920, 1080, 1, XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS | VO_GET_FRAME_MAY_FAIL);
  if (!img)
    return NULL;

  accel = (vdpau_accel_t *)img->accel_data;
  runtime_nr = accel->vdp_runtime_nr;
  img->free (img);

  if (accel->lock)
    accel->lock (accel->vo_frame);
  st = accel->vdp_decoder_create (accel->vdp_device, VDP_DECODER_PROFILE_H264_MAIN, 1920, 1080, 16, &decoder);
  if (st != VDP_STATUS_OK) {
    if (accel->unlock)
      accel->unlock (accel->vo_frame);
    xprintf (stream->xine, XINE_VERBOSITY_LOG, "can't create vdpau decoder!\n");
    return NULL;
  }
  accel->vdp_decoder_destroy (decoder);
  if (accel->unlock)
    accel->unlock (accel->vo_frame);

  this = (vdpau_h264_alter_decoder_t *)calloc (1, sizeof (*this));
  if (!this)
    return NULL;

  vdec = vdec_hw_h264_new (vdpau_h264_alter_logg, this, vdpau_h264_alter_frame_new,
    vdpau_h264_alter_frame_render, vdpau_h264_alter_frame_ready, vdpau_h264_alter_frame_delete,
    stream->video_out->get_property (stream->video_out, VO_PROP_BUFS_TOTAL));
  if (!vdec) {
    free (this);
    return NULL;
  }

  this->video_decoder.decode_data = vdpau_h264_alter_decode_data;
  this->video_decoder.flush = vdpau_h264_alter_flush;
  this->video_decoder.reset = vdpau_h264_alter_reset;
  this->video_decoder.discontinuity = vdpau_h264_alter_discontinuity;
  this->video_decoder.dispose = vdpau_h264_alter_dispose;

  this->stream = stream;
  this->vdec = vdec;

  this->vdp_runtime_nr = runtime_nr;
  this->reported_ratio = 0.0;
  this->reported_video_step = 0;
  this->reported_width = 0;
  this->reported_height = 0;

  this->decoder = VDP_INVALID_HANDLE;
  this->accel_vdpau = NULL;

  stream->video_out->open (stream->video_out, stream);

  return &this->video_decoder;
}


/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
void *h264_alter_init_plugin (xine_t * xine, const void *data) {
  static const video_decoder_class_t decode_video_vdpau_h264_alter_class = {
    .open_plugin = open_plugin,
    .identifier  = "vdpau_h264_alter",
    .description =
      N_
      ("vdpau_h264_alter: H264 decoder plugin using VDPAU hardware decoding.\n"
       "Must be used along with video_out_vdpau."),
    .dispose = NULL,
  };

  (void)xine;
  (void)data;
  return (void *)&decode_video_vdpau_h264_alter_class;
}

