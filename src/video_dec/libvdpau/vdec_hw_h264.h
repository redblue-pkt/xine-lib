/* kate: tab-indent on; indent-width 2; mixedindent off; indent-mode cstyle; remove-trailing-space on; */
/*
 * Copyright (C) 2008-2021 the xine project
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
 * h264.h, a generic H264 video stream parser for VDPAU and VAAPI hardware decoders
 */

#ifndef VDEC_HW_H264_H
#define VDEC_HW_H264_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>

typedef struct vdec_hw_h264_s vdec_hw_h264_t;
typedef struct vdec_hw_h264_frame_info_s vdec_hw_h264_frame_info_t;

typedef struct {
  void *user_data;
  vdec_hw_h264_t *vdec;
  int profile;
  int level;
  int width;
  int height;
  double ratio;
  int64_t pts; /** << reordered from vdec_hw_info_h264_put_frame () */
  int duration; /** << in pts */
#define VDEC_HW_H264_FRAME_TOP_FIELD 1
#define VDEC_HW_H264_FRAME_BOTTOM_FIELD 2
#define VDEC_HW_H264_FRAME_NEW_SEQ 4
  int flags;
  int bad_frame; /** "here should have been a frame" */
  int progressive_frame;
  int top_field_first;
  int color_matrix; /** << (MPEG matrix # << 1) | fullrange */
  int num_ref_frames;
  const vdec_hw_h264_frame_info_t *info;
} vdec_hw_h264_frame_t;

typedef struct {
  vdec_hw_h264_frame_t *frame;

  int is_long_term;
  int top_is_reference;
  int  bottom_is_reference;

  int32_t field_order_cnt[2];
  uint16_t frame_idx;
} vdec_hw_h264_info_ref_frame_t;

struct vdec_hw_h264_frame_info_s {
  const uint8_t * const *slices_bitstream;
  const uint32_t *slices_bytes;
  uint32_t slice_count;

  int32_t  field_order_cnt[2];
  int      is_reference;

  uint16_t frame_num;
  uint8_t  field_pic_flag;
  uint8_t  bottom_field_flag;
  uint8_t  num_ref_frames;
  uint8_t  mb_adaptive_frame_field_flag;
  uint8_t  constrained_intra_pred_flag;
  uint8_t  weighted_pred_flag;
  uint8_t  weighted_bipred_idc;
  uint8_t  frame_mbs_only_flag;
  uint8_t  transform_8x8_mode_flag;
  int8_t   chroma_qp_index_offset;
  int8_t   second_chroma_qp_index_offset;
  int8_t   pic_init_qp_minus26;
  uint8_t  num_ref_idx_l0_active_minus1;
  uint8_t  num_ref_idx_l1_active_minus1;
  uint8_t  log2_max_frame_num_minus4;
  uint8_t  pic_order_cnt_type;
  uint8_t  log2_max_pic_order_cnt_lsb_minus4;
  uint8_t  delta_pic_order_always_zero_flag;
  uint8_t  direct_8x8_inference_flag;
  uint8_t  entropy_coding_mode_flag;
  uint8_t  pic_order_present_flag;
  uint8_t  deblocking_filter_control_present_flag;
  uint8_t  redundant_pic_cnt_present_flag;

  uint8_t scaling_lists_4x4[6][16];
  uint8_t scaling_lists_8x8[2][64];

  vdec_hw_h264_info_ref_frame_t referenceFrames[16];
};

typedef enum {
  VDEC_HW_H264_LOGG_ERR = 0,
  VDEC_HW_H264_LOGG_INFO,
  VDEC_HW_H264_LOGG_DEBUG
} vdec_hw_h264_logg_t;

vdec_hw_h264_t *vdec_hw_h264_new (
  int __attribute__((format (printf, 3, 4))) (*logg) (void *user_data,
    vdec_hw_h264_logg_t level, const char *fmt, ...), /** << can be NULL */
  void *user_data, /** << passed to logg () and frame_* () verbatim */
  int  (*frame_new)    (void *user_data, vdec_hw_h264_frame_t *frame), /** << get user part of this frame */
  int  (*frame_render) (void *user_data, vdec_hw_h264_frame_t *frame), /** << perform hw decoding based on info */
  int  (*frame_ready)  (void *user_data, vdec_hw_h264_frame_t *frame), /** << put this frame to output queue */
  void (*frame_delete) (void *user_data, vdec_hw_h264_frame_t *frame), /** << unref/free user part */
  int num_frames /** << max frames to use */
);
/** frame_delete () any held frames, set defaults */
int vdec_hw_h264_reset (vdec_hw_h264_t *dec);
/** zero all held frame pts */
int vdec_hw_h264_zero_pts (vdec_hw_h264_t *dec);
/** frame_ready () / frame_delete () any held frames now */
int vdec_hw_h264_flush (vdec_hw_h264_t *dec);
/** optional, 0 means "no change". */
int vdec_hw_h264_put_container_info (vdec_hw_h264_t *dec, int width, int height, int duration, double ratio);
/** optional global head from media container */
int vdec_hw_h264_put_config (vdec_hw_h264_t *dec, const uint8_t *bitstream, uint32_t num_bytes);
/** send what you have */
int vdec_hw_h264_put_frame (vdec_hw_h264_t *dec, int64_t pts, const uint8_t *bitstream, uint32_t num_bytes, int frame_end);
/** done */
void vdec_hw_h264_delete (vdec_hw_h264_t **dec);

#endif
