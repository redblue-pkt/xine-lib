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
 * h264.c, a generic H264 video stream parser for VDPAU and VAAPI hardware decoders
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#undef LOG
#define LOG_MODULE "vdec_hw_h264"

#ifdef LOG
#  define lprintf(_fmt, ...) fprintf (stderr, LOG_MODULE _fmt, ...)
#else
#  define lprintf(_fmt, ...) /* nothing */
#endif

#include <stdio.h>
#include <stdarg.h>
#include "vdec_hw_h264.h"
#include "vdec_hw_bits_reader.h"

#define PICTURE_TOP_DONE    1
#define PICTURE_BOTTOM_DONE 2
#define PICTURE_DONE        3

#define SHORT_TERM_REF 1
#define LONG_TERM_REF  2

#define MAX_SPS 32
#define MAX_PPS 256
#define MAX_SLICES 80 /* 68? */
#define MAX_REF_FRAMES 16
#define MIN_BUFFER_SIZE 10000
#define MAX_BUFFER_SIZE 3145728
#define BUF_PAD 8

#define NAL_UNSPECIFIED 0
#define NAL_SLICE_NO_IDR 1
#define NAL_SLICE_IDR 5
#define NAL_SEI 6
#define NAL_SEQUENCE 7
#define NAL_PICTURE 8
#define NAL_SEEK_POINT 9
#define NAL_END_SEQUENCE 10
#define NAL_END_STREAM 11
#define NAL_FILLER 12
#define NAL_SEQUENCE_EXT 13

#define SLICE_TYPE_P 0
#define SLICE_TYPE_B 1
#define SLICE_TYPE_I 2
#define SLICE_TYPE_SP 3
#define SLICE_TYPE_SI 4

#define START_IDR_FLAG 1000

#define MAX_POC 2147483647

#define DPB_DRAW_CLEAR   1
#define DPB_DRAW_REFS   2
#define DPB_DRAW_CURRENT 3

/* SPS: sequence parameter set
 * PPS: picture parameter set
 * LPS: slice parameter set */

typedef struct{
  uint8_t aspect_ratio_info;
  uint8_t aspect_ratio_idc;
  uint16_t sar_width;
  uint16_t sar_height;
  uint8_t colour_desc;
  uint8_t colour_primaries;
  uint8_t timing_info;
  uint32_t num_units_in_tick;
  uint32_t time_scale;
} vdec_hw_h264_vui_t;

typedef struct {
  uint8_t reused;
  uint8_t profile_idc;
  uint8_t level_idc;
  uint8_t sps_id;
  uint8_t constraint_set0_flag;
  uint8_t constraint_set1_flag;
  uint8_t constraint_set2_flag;
  uint8_t constraint_set3_flag;
  uint8_t chroma_format_idc;
  uint8_t separate_colour_plane_flag;
  uint8_t bit_depth_luma_minus8;
  uint8_t bit_depth_chroma_minus8;
  uint8_t qpprime_y_zero_transform_bypass_flag;
  uint8_t seq_scaling_matrix_present_flag;
  uint8_t scaling_lists_4x4[6][16];
  uint8_t scaling_lists_8x8[2][64];
  uint8_t log2_max_frame_num_minus4;
  uint8_t pic_order_cnt_type;
  uint8_t log2_max_pic_order_cnt_lsb_minus4;
  uint8_t delta_pic_order_always_zero_flag;
  int32_t offset_for_non_ref_pic;
  int32_t offset_for_top_to_bottom_field;
  uint8_t ref_frames_used_in_pic_order_cnt_cycle;
  int32_t offset_for_ref_frame[256];
  uint8_t ref_frames_used;
  uint8_t gaps_in_frame_num_value_allowed_flag;
  uint8_t pic_width_in_mbs_minus1;
  uint8_t pic_height_in_map_units_minus1;
  uint8_t frame_mbs_only_flag;
  uint8_t mb_adaptive_frame_field_flag;
  uint8_t direct_8x8_inference_flag;
  uint8_t frame_cropping_flag;
  uint16_t frame_crop_left_offset;
  uint16_t frame_crop_right_offset;
  uint16_t frame_crop_top_offset;
  uint16_t frame_crop_bottom_offset;
  uint8_t vui_parameters_present_flag;
  vdec_hw_h264_vui_t vui;
} vdec_hw_h264_sps_t;

typedef struct{
  uint8_t pps_id;
  uint8_t sps_id;
  uint8_t entropy_coding_mode_flag;
  uint8_t pic_order_present_flag;
  /*uint8_t num_slice_groups_minus1;
     uint8_t slice_group_map_type;
     uint16_t run_length_minus1[64];
     uint16_t top_left[64];
     uint16_t bottom_right[64];
     uint8_t slice_group_change_direction_flag;
     uint16_t slice_group_change_rate_minus1;
     uint16_t pic_size_in_map_units_minus1;
     uint8_t slice_group_id[64]; */
  uint8_t num_ref_idx_l0_active_minus1;
  uint8_t num_ref_idx_l1_active_minus1;
  uint8_t weighted_pred_flag;
  uint8_t weighted_bipred_idc;
  int8_t pic_init_qp_minus26;
  int8_t pic_init_qs_minus26;
  int8_t chroma_qp_index_offset;
  uint8_t deblocking_filter_control_present_flag;
  uint8_t constrained_intra_pred_flag;
  uint8_t redundant_pic_cnt_present_flag;
  uint8_t transform_8x8_mode_flag;
  uint8_t pic_scaling_matrix_present_flag;
  uint8_t pic_scaling_list_present_flag[8];
  uint8_t scaling_lists_4x4[6][16];
  uint8_t scaling_lists_8x8[2][64];
  int8_t second_chroma_qp_index_offset;
} vdec_hw_h264_pps_t;

typedef struct {
  uint8_t nal_ref_idc;
  uint8_t nal_unit_type;
  uint8_t slice_type;
  uint8_t pps_id;
  uint16_t frame_num;
  uint32_t MaxFrameNum;
  uint8_t field_pic_flag;
  uint8_t bottom_field_flag;
  uint16_t idr_pic_id;
  uint16_t pic_order_cnt_lsb;
  int32_t delta_pic_order_cnt_bottom;
  int32_t delta_pic_order_cnt[2];
  uint8_t redundant_pic_cnt;
  uint8_t num_ref_idx_l0_active_minus1;
  uint8_t num_ref_idx_l1_active_minus1;
} vdec_hw_h264_lps_t;

typedef struct vdec_hw_h264_frame_int_s {
  vdec_hw_h264_frame_t f;

  struct vdec_hw_h264_frame_int_s *link;
  int drawn;
  uint8_t used;
  uint8_t missing_header;
  uint8_t drop_pts;
  uint8_t completed;
  uint16_t FrameNum;
  int32_t FrameNumWrap;
  int32_t PicNum[2];            /* 0:top, 1:bottom */
  uint8_t is_reference[2];      /* 0:top, 1:bottom, short or long term */
  uint8_t field_pic_flag;
  int32_t PicOrderCntMsb;
  int32_t TopFieldOrderCnt;
  int32_t BottomFieldOrderCnt;
  uint16_t pic_order_cnt_lsb;
  uint8_t mmc5;
} vdec_hw_h264_frame_int_t;

static __attribute__((format (printf, 3, 4))) int _vdec_hw_h264_dummy_logg (void *user_data,
  vdec_hw_h264_logg_t level, const char *fmt, ...) {
  (void)user_data;
  if (level == VDEC_HW_H264_LOGG_ERR) {
    va_list args;
    int n;

    va_start (args, fmt);
    n = vfprintf (stderr, fmt, args);
    va_end (args);
    return n;
  }
  return 0;
}

typedef struct {
  uint32_t coded_width;
  uint32_t coded_height;
  uint64_t video_step; /** << frame duration in pts units */
  double ratio;
  double user_ratio;
  uint8_t profile;
  uint8_t level;
  int color_matrix;

  int slices_count;
  int slice_mode;
  const uint8_t *slices_bitstream[MAX_SLICES];
  uint32_t slices_bytes[MAX_SLICES];

  vdec_hw_h264_sps_t *sps[MAX_SPS];
  vdec_hw_h264_pps_t *pps[MAX_PPS];
  vdec_hw_h264_lps_t lps;

  vdec_hw_h264_frame_int_t *dpb[MAX_REF_FRAMES + 1];
  uint16_t prevFrameNum;
  uint16_t prevFrameNumOffset;
  uint8_t prevMMC5;

  int chroma;

  struct {                /** << bytestream buf */
    uint8_t *mem;         /** << memory */
    uint32_t max;         /** << allocated size, minus a pad reserve */
    int32_t  nal_unit;    /** << offs of a yet unprocessed nal unit, or -1 */
    uint32_t read;        /** << parse here */
    uint32_t write;       /** << append here */
  } buf;

  int64_t pic_pts;

  bits_reader_t br;

  int reset;
  int startup_frame;

  /* 0: standard 00 00 01
   * 1..4: a big endian int of that many bytes telling the unit size */
  uint8_t nal_unit_prefix;
} vdec_hw_h264_sequence_t;

struct vdec_hw_h264_s {
  /* user supolied */
  __attribute__((format (printf, 3, 4))) int (*logg) (void *user_data, vdec_hw_h264_logg_t level, const char *fmt, ...);
  void *user_data;
  int  (*frame_new)    (void *user_data, vdec_hw_h264_frame_t *frame);
  int  (*frame_render) (void *user_data, vdec_hw_h264_frame_t *frame);
  int  (*frame_ready)  (void *user_data, vdec_hw_h264_frame_t *frame);
  void (*frame_delete) (void *user_data, vdec_hw_h264_frame_t *frame);

  vdec_hw_h264_sequence_t seq;

  struct {
    int sps;
    int pps;
    int slices;
    int frame_ready;
  } stats;

  int32_t user_frames;
  uint32_t ref_frames_max;
  uint32_t ref_frames_used;
  vdec_hw_h264_frame_int_t frames[MAX_REF_FRAMES + 1];

  uint8_t tempbuf[1 << 16];
};

static const uint8_t zigzag_4x4[16] = {
  0, 1, 4, 8,
  5, 2, 3, 6,
  9, 12, 13, 10,
  7, 11, 14, 15
};

static const uint8_t zigzag_8x8[64] = {
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

static const uint8_t default_4x4_intra[16] = {
  6, 13, 13, 20,
  20, 20, 28, 28,
  28, 28, 32, 32,
  32, 37, 37, 42
};

static const uint8_t default_4x4_inter[16] = {
  10, 14, 14, 20,
  20, 20, 24, 24,
  24, 24, 27, 27,
  27, 30, 30, 34
};

static const uint8_t default_8x8_intra[64] = {
  6, 10, 10, 13, 11, 13, 16, 16,
  16, 16, 18, 18, 18, 18, 18, 23,
  23, 23, 23, 23, 23, 25, 25, 25,
  25, 25, 25, 25, 27, 27, 27, 27,
  27, 27, 27, 27, 29, 29, 29, 29,
  29, 29, 29, 31, 31, 31, 31, 31,
  31, 33, 33, 33, 33, 33, 36, 36,
  36, 36, 38, 38, 38, 40, 40, 42
};

static const uint8_t default_8x8_inter[64] = {
  9, 13, 13, 15, 13, 15, 17, 17,
  17, 17, 19, 19, 19, 19, 19, 21,
  21, 21, 21, 21, 21, 22, 22, 22,
  22, 22, 22, 22, 24, 24, 24, 24,
  24, 24, 24, 24, 25, 25, 25, 25,
  25, 25, 25, 27, 27, 27, 27, 27,
  27, 28, 28, 28, 28, 28, 30, 30,
  30, 30, 32, 32, 32, 33, 33, 35
};

/** 00 00 03 foo -> 00 00 foo */
static uint32_t _vdec_hw_h264_unescape (uint8_t *b, uint32_t len) {
  uint8_t *p = b, *e = b + len, *q, *a;
  uint32_t v = 0xffffff00;

  while (p < e) {
    v = (v + *p) << 8;
    if (v == 0x00000300)
      break;
    p++;
  }
  if (p >= e)
    return p - b;

  q = p;
  do {
    int32_t d;

    a = ++p;
    while (p < e) {
      v = (v + *p) << 8;
      if (v == 0x00000300)
        break;
      p++;
    }
    d = p - a;
    if (d > 0) {
      memmove (q, a, d);
      q += d;
    }
  } while (p < e);
  return q - b;
}

static void _vdec_hw_h264_frame_free (vdec_hw_h264_t *vdec, vdec_hw_h264_frame_int_t *frame, int zero) {
  if (frame->link) {
    if (frame->link->link == frame) {
      frame->link->link = NULL;
      frame->f.user_data = NULL;
    }
    frame->link = NULL;
  }
  if (frame->f.user_data && vdec->frame_delete) {
    vdec->frame_delete (vdec->user_data, &frame->f);
    frame->f.user_data = NULL;
    vdec->user_frames--;
    if (vdec->user_frames < 0)
      vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
        LOG_MODULE ": ERROR: too few user frames (%d).\n", (int)vdec->user_frames);
  }
  if (zero) {
    memset (frame, 0, sizeof (*frame));
    frame->f.vdec = vdec;
  }
}

static void _vdec_hw_h264_frame_new (vdec_hw_h264_t *vdec, vdec_hw_h264_frame_int_t *frame) {
  if (frame->link) {
    if (frame->link->link == frame)
      return;
    frame->link = NULL;
  }
  _vdec_hw_h264_frame_free (vdec, frame, 0);
  if (!frame->f.user_data && vdec->frame_new) {
    vdec->frame_new (vdec->user_data, &frame->f);
    frame->drawn = 0;
    vdec->user_frames++;
    if (vdec->user_frames > (int32_t)vdec->ref_frames_max + 1)
      vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
        LOG_MODULE ": ERROR: too many user frames (%d).\n", (int)vdec->user_frames);
  }
}

static void _vdec_hw_h264_frame_link (vdec_hw_h264_t *vdec, vdec_hw_h264_frame_int_t *frame, vdec_hw_h264_frame_int_t *to) {
  (void)vdec;
  if ((to->link && (to->link != frame))
    || (to->f.user_data && (to->f.user_data != frame->f.user_data)))
    _vdec_hw_h264_frame_free (vdec, to, 0);
  if (frame->link && (frame->link != to))
    _vdec_hw_h264_frame_free (vdec, frame, 0);
  *to = *frame;
  frame->link = to;
  to->link = frame;
}

static void _vdec_hw_h264_frame_draw (vdec_hw_h264_t *vdec, vdec_hw_h264_frame_int_t *frame) {
  if (!frame->drawn && vdec->frame_ready) {
    vdec->stats.frame_ready++;
    vdec->frame_ready (vdec->user_data, &frame->f);
    frame->drawn = 1;
    if (frame->link)
      frame->link->drawn = 1;
  }
}

/*-------- DPB -------------------------------------------*/
#if 0
static void
dpb_print (vdec_hw_h264_sequence_t * sequence)
{
  int i;
  vdec_hw_h264_frame_int_t *frame;
  uint32_t sf;

  for (i = 0; i < MAX_REF_FRAMES; i++)
  {
    frame = vdec->seq.dpb[i];
    if (!frame->used)
      break;
    vo_frame_t *vo = (vo_frame_t *) frame->videoSurface;
    vdpau_accel_t *accel;
    if (vo)
      accel = (vdpau_accel_t *) vo->accel_data;
    sf = (vo) ? accel->surface : (uint32_t)-1;
    fprintf (stderr,
             "{ i:%d u:%d c:%d pn:%d-%d ir:%d-%d tpoc:%d bpoc:%d sf:%u }\n",
             i, frame->used, frame->completed, frame->PicNum[0],
             frame->PicNum[1], frame->is_reference[0], frame->is_reference[1],
             frame->TopFieldOrderCnt, frame->BottomFieldOrderCnt, sf);
  }
}
#endif

int vdec_hw_h264_zero_pts (vdec_hw_h264_t *vdec) {
  uint32_t u, n = 0;

  if (!vdec)
    return 0;

  vdec->seq.reset = VDEC_HW_H264_FRAME_NEW_SEQ;

  for (u = 0; u < vdec->ref_frames_used; u++) {
    vdec_hw_h264_frame_int_t *frame = vdec->seq.dpb[u];

    if (frame->f.pts)
      frame->f.pts = 0, frame->drop_pts = 1, n++;
  }
  {
    vdec_hw_h264_frame_int_t *frame = vdec->frames + MAX_REF_FRAMES;

    if (frame->f.pts)
      frame->f.pts = 0, frame->drop_pts = 1, n++;
  }
  return n;
}

static void _vdec_hw_h264_dpb_reset (vdec_hw_h264_t *vdec) {
  uint32_t u;

  for (u = 0; u < vdec->ref_frames_used; u++)
    _vdec_hw_h264_frame_free (vdec, vdec->seq.dpb[u], 1);
  vdec->ref_frames_used = 0;
  if (!vdec->frames[MAX_REF_FRAMES].is_reference[0] && !vdec->frames[MAX_REF_FRAMES].is_reference[1])
    _vdec_hw_h264_frame_free (vdec, vdec->frames + MAX_REF_FRAMES, 0);
}

static void _vdec_hw_h264_dpb_remove (vdec_hw_h264_t *vdec, uint32_t index) {
  vdec_hw_h264_frame_int_t *frame = vdec->seq.dpb[index];
  uint32_t u;

  lprintf ("|||||||||||||||||||||||||||||||||||||||| dbp_remove\n");
  _vdec_hw_h264_frame_free (vdec, frame, 1);
  for (u = index + 1; u < vdec->ref_frames_used; u++)
    vdec->seq.dpb[u - 1] = vdec->seq.dpb[u];
  vdec->seq.dpb[u - 1] = frame;
  vdec->ref_frames_used = u - 1;
}

static vdec_hw_h264_frame_int_t *_vdec_hw_h264_dpb_get_prev_ref (vdec_hw_h264_t *vdec) {
  return vdec->ref_frames_used > 0 ? vdec->seq.dpb[vdec->ref_frames_used - 1] : NULL;
}

static void _vdec_hw_h264_dpb_draw_frames (vdec_hw_h264_t *vdec, int32_t curpoc, int draw_mode) {
  int i, index;
  int32_t poc, tpoc;
  vdec_hw_h264_frame_int_t *frame;

  do {
    index = -1;
    poc = curpoc;
    for (i = 0; i < (int)vdec->ref_frames_used; i++) {
      frame = vdec->seq.dpb[i];
      tpoc = (frame->TopFieldOrderCnt > frame->BottomFieldOrderCnt)
           ? frame->TopFieldOrderCnt : frame->BottomFieldOrderCnt;
      if (!frame->drawn && (tpoc <= poc)) {
        poc = tpoc;
        index = i;
      }
    }
    if ((index < 0) || (poc > curpoc))
      break;
    //fprintf(stderr,"|||||||||||||||||||||||||||||||||||||||| dpb_draw_frame = %d\n", poc);
    frame = vdec->seq.dpb[index];
    //fprintf(stderr,"H264 PTS = %llu\n", frame->pts);
    _vdec_hw_h264_frame_draw (vdec, frame);
    if ((draw_mode != DPB_DRAW_CLEAR) && !frame->is_reference[0] && !frame->is_reference[1])
      _vdec_hw_h264_dpb_remove (vdec, index);
  } while (index >= 0);

  if (draw_mode == DPB_DRAW_CURRENT) {
    frame = &vdec->frames[MAX_REF_FRAMES];
    //fprintf(stderr,"|||||||||||||||||||||||||||||||||||||||| dpb_draw_frame = %d\n", curpoc);
    _vdec_hw_h264_frame_draw (vdec, frame);
    _vdec_hw_h264_frame_free (vdec, frame, 1);
  } else if (draw_mode == DPB_DRAW_CLEAR) {
    _vdec_hw_h264_dpb_reset (vdec);
  }
}

static vdec_hw_h264_frame_int_t *_vdec_hw_h264_dpb_get_PicNum (vdec_hw_h264_t *vdec, int32_t pic_num, int *index) {
  vdec_hw_h264_frame_int_t *frame;
  uint32_t i;

  for (i = 0; i < vdec->ref_frames_used; i++) {
    frame = vdec->seq.dpb[i];
    if ((frame->PicNum[0] == pic_num) || (frame->PicNum[1] == pic_num)) {
      *index = i;
      return frame;
    }
  }
  return NULL;
}

static void _vdec_hw_h264_dpb_mmc1 (vdec_hw_h264_t *vdec, int32_t picnum) {
  int index;
  vdec_hw_h264_frame_int_t *frame = _vdec_hw_h264_dpb_get_PicNum (vdec, picnum, &index);

  lprintf ("_vdec_hw_h264_dpb_mmc1\n");

  if (frame) {
    frame->is_reference[0] = frame->is_reference[1] = 0;
    if (frame->drawn)
      _vdec_hw_h264_dpb_remove (vdec, index);
    else
      _vdec_hw_h264_dpb_draw_frames (vdec,
        (frame->TopFieldOrderCnt > frame->BottomFieldOrderCnt)
        ? frame->TopFieldOrderCnt : frame->BottomFieldOrderCnt,
        DPB_DRAW_REFS);
  }
}

static vdec_hw_h264_sps_t *_vdec_hw_h264_get_sps (vdec_hw_h264_t *vdec) {
  vdec_hw_h264_lps_t *lps = &vdec->seq.lps;
  vdec_hw_h264_pps_t *pps = vdec->seq.pps[lps->pps_id];

  if (!pps)
    return NULL;
  return vdec->seq.sps[pps->sps_id];
}

static void _vdec_hw_h264_dbp_append (vdec_hw_h264_t *vdec, int second_field) {
  uint32_t i, index = 0, max_refs = vdec->ref_frames_max;
  int32_t fnw = MAX_POC;
  vdec_hw_h264_frame_int_t *tmp = NULL, *cur_pic = &vdec->frames[MAX_REF_FRAMES];
  vdec_hw_h264_sps_t *sps = _vdec_hw_h264_get_sps (vdec);

  if (sps) {
    max_refs = sps->ref_frames_used ? sps->ref_frames_used : 1;
    if (max_refs > vdec->ref_frames_max)
      max_refs = vdec->ref_frames_max;
  }

  if (second_field) {
    tmp = _vdec_hw_h264_dpb_get_prev_ref (vdec);
    if (tmp) {
      _vdec_hw_h264_frame_link (vdec, cur_pic, tmp);
    } else {
      vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
        LOG_MODULE ": no frame to store the second field ?!\n");
    }
    return;
  }

  for (i = 0; i < vdec->ref_frames_used; i++) {
    if (vdec->seq.dpb[i]->FrameNumWrap < fnw) {
      fnw = vdec->seq.dpb[i]->FrameNumWrap;
      index = i;
    }
  }

  if (vdec->ref_frames_used >= max_refs) {
    if (0 /* sps && sps->reused && (max_refs < vdec->ref_frames_max) */) {
      vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_INFO,
        LOG_MODULE ": SPS #%d is reused, raising ref frame count from %d to %d.\n",
        (int)sps->sps_id, (int)max_refs, (int)max_refs + 1);
      sps->ref_frames_used = ++max_refs;
    } else {
      lprintf ("sliding window\n");
      tmp = vdec->seq.dpb[index];
      tmp->is_reference[0] = tmp->is_reference[1] = 0;
      if (tmp->drawn)
        _vdec_hw_h264_dpb_remove (vdec, index);
      else
        _vdec_hw_h264_dpb_draw_frames (vdec,
          (tmp->TopFieldOrderCnt > tmp->BottomFieldOrderCnt)
          ? tmp->TopFieldOrderCnt : tmp->BottomFieldOrderCnt,
          DPB_DRAW_REFS);
      i = vdec->ref_frames_used;
    }
  }

  if (i < max_refs) { /* should always be true */
    if (cur_pic->field_pic_flag) {
      _vdec_hw_h264_frame_link (vdec, cur_pic, vdec->seq.dpb[i]);
    } else {
      *vdec->seq.dpb[i] = *cur_pic;
      cur_pic->f.user_data = NULL;
    }
    vdec->ref_frames_used = i + 1;
  } else {
    vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
      LOG_MODULE ": too many reference frames (%d).\n", i + 1);
  }
}

/*--------------------------------------------------------*/

static void _vdec_hw_h264_reset_slices (vdec_hw_h264_t *vdec) {
  vdec->seq.slices_count = 0;
  vdec->seq.slice_mode = 0;
}

static void _vdec_hw_h264_reset_sequence (vdec_hw_h264_t *vdec) {
  vdec->seq.prevFrameNum = 0;
  vdec->seq.prevFrameNumOffset = 0;
  vdec->seq.prevMMC5 = 0;

  vdec->seq.startup_frame = 0;
  vdec->seq.reset = 0;
  vdec->seq.chroma = 0;
  vdec->seq.pic_pts = 0;
  vdec->seq.buf.write = 0;
  vdec->seq.buf.read = 0;
  vdec->seq.buf.nal_unit = -1;
  _vdec_hw_h264_reset_slices (vdec);
  _vdec_hw_h264_dpb_reset (vdec);
  _vdec_hw_h264_frame_free (vdec, vdec->frames + MAX_REF_FRAMES, 1);
  vdec->seq.reset = VDEC_HW_H264_FRAME_NEW_SEQ;
}

static void _vdec_hw_h264_set_ratio (vdec_hw_h264_t *vdec, vdec_hw_h264_sps_t *sps) {
  static const double fixed_ratios[] = {
    (double)1, /* ASPECT_UNSPECIFIED */
    (double)1, /* 1/1 */
    (double)12 / (double)11,
    (double)10 / (double)11,
    (double)16 / (double)11,
    (double)40 / (double)33,
    (double)24 / (double)11,
    (double)20 / (double)11,
    (double)32 / (double)11,
    (double)80 / (double)33,
    (double)18 / (double)11,
    (double)15 / (double)11,
    (double)64 / (double)33,
    (double)160 / (double)99,
    (double)4 / (double)3,
    (double)3 / (double)2,
    (double)2 / (double)1,
    (double)1 /* RESERVED */
  };

  if (!vdec->seq.coded_height)
    vdec->seq.coded_height = 1;

  vdec->seq.ratio = (double)vdec->seq.coded_width / (double)vdec->seq.coded_height;
  if (sps->vui.aspect_ratio_info) {
    if (sps->vui.aspect_ratio_idc < sizeof (fixed_ratios) / sizeof (fixed_ratios[0])) {
      vdec->seq.ratio *= fixed_ratios[sps->vui.aspect_ratio_idc];
    } else if (sps->vui.aspect_ratio_idc == 255) {
      if (sps->vui.sar_height)
        vdec->seq.ratio *= (double)sps->vui.sar_width / sps->vui.sar_height;
    }
  }
}

static void parse_scaling_list (bits_reader_t *br, uint8_t *scaling_list, int len, int index) {
  int last_scale = 8;
  int next_scale = 8;
  int32_t delta_scale;
  uint8_t use_default_scaling_matrix_flag = 0;
  int i;
  uint32_t u;

  const uint8_t *zigzag = (len == 64) ? zigzag_8x8 : zigzag_4x4;

  for (i = 0; i < len; i++)
  {
    if (next_scale != 0)
    {
      delta_scale = read_exp_se (br);
      next_scale = (last_scale + delta_scale + 256) % 256;
      if (i == 0 && next_scale == 0)
      {
        use_default_scaling_matrix_flag = 1;
        break;
      }
    }
    scaling_list[zigzag[i]] = last_scale =
      (next_scale == 0) ? last_scale : next_scale;
  }

  if (use_default_scaling_matrix_flag)
  {
    switch (index)
    {
    case 0:
    case 1:
    case 2:
      {
        for (u = 0; u < sizeof (default_4x4_intra); u++)
          scaling_list[zigzag_4x4[u]] = default_4x4_intra[u];
        break;
      }
    case 3:
    case 4:
    case 5:
      {
        for (u = 0; u < sizeof (default_4x4_inter); u++)
          scaling_list[zigzag_4x4[u]] = default_4x4_inter[u];
        break;
      }
    case 6:
      {
        for (u = 0; u < sizeof (default_8x8_intra); u++)
          scaling_list[zigzag_8x8[u]] = default_8x8_intra[u];
        break;
      }
    case 7:
      {
        for (u = 0; u < sizeof (default_8x8_inter); u++)
          scaling_list[zigzag_8x8[u]] = default_8x8_inter[u];
        break;
      }
    }
  }
}

static void _vdec_hw_h264_scaling_list_fallback_A (uint8_t * scaling_lists_4x4, uint8_t * scaling_lists_8x8, int i) {
  uint32_t j;

  switch (i) {
    case 0:
      for (j = 0; j < sizeof (default_4x4_intra); j++)
        scaling_lists_4x4[(i * 16) + zigzag_4x4[j]] = default_4x4_intra[j];
      break;
    case 3:
      for (j = 0; j < sizeof (default_4x4_inter); j++)
        scaling_lists_4x4[(i * 16) + zigzag_4x4[j]] = default_4x4_inter[j];
      break;
    case 1:
    case 2:
    case 4:
    case 5:
      memcpy (&scaling_lists_4x4[i * 16], &scaling_lists_4x4[(i - 1) * 16], 16);
      break;
    case 6:
      for (j = 0; j < sizeof (default_8x8_intra); j++)
        scaling_lists_8x8[(i - 6) * 64 + zigzag_8x8[j]] = default_8x8_intra[j];
      break;
    case 7:
      for (j = 0; j < sizeof (default_8x8_inter); j++)
        scaling_lists_8x8[(i - 6) * 64 + zigzag_8x8[j]] = default_8x8_inter[j];
      break;
  }
}

static void _vdec_hw_h264_scaling_list_fallback_B (vdec_hw_h264_sps_t *sps, vdec_hw_h264_pps_t *pps, int i) {
  switch (i) {
    case 0:
    case 3:
      memcpy (pps->scaling_lists_4x4[i], sps->scaling_lists_4x4[i], sizeof (pps->scaling_lists_4x4[i]));
      break;
    case 1:
    case 2:
    case 4:
    case 5:
      memcpy (pps->scaling_lists_4x4[i], pps->scaling_lists_4x4[i - 1], sizeof (pps->scaling_lists_4x4[i]));
      break;
    case 6:
    case 7:
      memcpy (pps->scaling_lists_8x8[i - 6], sps->scaling_lists_8x8[i - 6], sizeof (pps->scaling_lists_8x8[i - 6]));
      break;
  }
}

static void _vdec_hw_h264_read_vui (vdec_hw_h264_t *vdec, vdec_hw_h264_vui_t *vui) {
  bits_reader_t *br = &vdec->seq.br;
  vdec->seq.color_matrix = 4; /* undefined, mpeg range */

  vui->aspect_ratio_info = read_bits (br, 1);
  lprintf ("aspect_ratio_info_present_flag = %d\n", (int)vui->aspect_ratio_info);
  if (vui->aspect_ratio_info) {
    vui->aspect_ratio_idc = read_bits (br, 8);
    lprintf ("aspect_ratio_idc = %d\n", (int)vui->aspect_ratio_idc);
    if (vui->aspect_ratio_idc == 255) {
      vui->sar_width = read_bits (br, 16);
      lprintf ("sar_width = %d\n", (int)vui->sar_width);
      vui->sar_height = read_bits (br, 16);
      lprintf ("sar_height = %d\n", (int)vui->sar_height);
    }
  }

  if (read_bits (br, 1))        /* overscan_info_present_flag */
    skip_bits (br, 1);          /* overscan_appropriate_falg */

  if (read_bits (br, 1)) {      /* video_signal_type_present_flag */
    skip_bits (br, 3);          /* video_format */
    vdec->seq.color_matrix = (vdec->seq.color_matrix & ~1) | read_bits (br, 1);  /*video_full_range_flag */
    vui->colour_desc = read_bits (br, 1);
    lprintf ("colour_desc = %d\n", (int)vui->colour_desc);
    if (vui->colour_desc) {
      skip_bits (br, 8);        /* colour_primaries */
      skip_bits (br, 8);        /* transfer_characteristics */
      vdec->seq.color_matrix = (vdec->seq.color_matrix & 1) | (read_bits (br, 8) << 1);  /* matrix_coefficients */
    }
  }

  if (read_bits (br, 1)) {      /* chroma_loc_info_present_flag */
    read_exp_ue (br);           /* chroma_sample_loc_type_top_field */
    read_exp_ue (br);           /* chroma_sample_loc_type_bottom_field */
  }

  vui->timing_info = read_bits (br, 1);
  lprintf ("timing_info = %d\n", (int)vui->timing_info);
  if (vui->timing_info) {
    /* seen (500, (24000 + 0x80000000)) here... */
    vui->num_units_in_tick = read_bits (br, 32) & 0x7fffffff;
    lprintf ("num_units_in_tick = %d\n", (int)vui->num_units_in_tick);
    vui->time_scale = read_bits (br, 32) & 0x7fffffff;
    lprintf ("time_scale = %d\n", (int)vui->time_scale);
    if (vui->time_scale > 0) {
      /* good: 2 * 1001 / 48000. */
      vdec->seq.video_step = (uint64_t)90000 * 2
        * vui->num_units_in_tick / vui->time_scale;
      if (vdec->seq.video_step < 90) {
        /* bad: 2 * 1 / 60000. seen this once from broken h.264 video usability info (VUI).
         * VAAPI seems to apply a similar HACK. */
        vdec->seq.video_step = (uint64_t)90000000 * 2 * vui->num_units_in_tick / vui->time_scale;
        /* seen 1 / 180000, ignore. */
        if (vdec->seq.video_step < 1500)
          vdec->seq.video_step = 0;
      }
    }
  }
}

static vdec_hw_h264_sps_t *_vdec_hw_h264_read_sps (vdec_hw_h264_t *vdec) {
  vdec_hw_h264_sps_t tsps, *sps;
  uint8_t bits;
  int i;

  memset (&tsps, 0, sizeof (tsps));

  tsps.profile_idc = read_bits (&vdec->seq.br, 8);
  bits = read_bits (&vdec->seq.br, 8);
  tsps.constraint_set0_flag = (bits >> 7) & 1;
  tsps.constraint_set1_flag = (bits >> 6) & 1;
  tsps.constraint_set2_flag = (bits >> 5) & 1;
  tsps.constraint_set3_flag = (bits >> 4) & 1;
  /* skip 4 */
  tsps.level_idc = read_bits (&vdec->seq.br, 8);
  tsps.sps_id = read_exp_ue (&vdec->seq.br);
  if (tsps.sps_id > 31) {
    lprintf ("invalid SPS id %d!!\n", (int)tsps.sps_id);
    return NULL;
  }

  /* this is read only for now. */
  tsps.reused = 0;

  memset (&tsps.scaling_lists_4x4, 16, sizeof (tsps.scaling_lists_4x4));
  memset (&tsps.scaling_lists_8x8, 16, sizeof (tsps.scaling_lists_8x8));

  tsps.chroma_format_idc = 1;
  tsps.separate_colour_plane_flag = 0;
  if  (tsps.profile_idc == 100 || tsps.profile_idc == 110
    || tsps.profile_idc == 122 || tsps.profile_idc == 244
    || tsps.profile_idc ==  44 || tsps.profile_idc ==  83
    || tsps.profile_idc ==  86) {
    tsps.chroma_format_idc = read_exp_ue (&vdec->seq.br);
    lprintf ("chroma_format_idc = %d\n", (int)tsps.chroma_format_idc);
    if (tsps.chroma_format_idc == 3) {
      tsps.separate_colour_plane_flag = read_bits (&vdec->seq.br, 1);
      lprintf ("separate_colour_plane_flag = %d\n", (int)tsps.separate_colour_plane_flag);
    }

    tsps.bit_depth_luma_minus8 = read_exp_ue (&vdec->seq.br);
    lprintf ("bit_depth_luma_minus8 = %d\n", (int)tsps.bit_depth_luma_minus8);
    tsps.bit_depth_chroma_minus8 = read_exp_ue (&vdec->seq.br);
    lprintf ("bit_depth_chroma_minus8 = %d\n", (int)tsps.bit_depth_chroma_minus8);

    tsps.qpprime_y_zero_transform_bypass_flag = read_bits (&vdec->seq.br, 1);
    lprintf ("qpprime_y_zero_transform_bypass_flag = %d\n", (int)tsps.qpprime_y_zero_transform_bypass_flag);

    tsps.seq_scaling_matrix_present_flag = read_bits (&vdec->seq.br, 1);
    lprintf ("seq_scaling_matrix_present_flag = %d\n", (int)tsps.seq_scaling_matrix_present_flag);
    if (tsps.seq_scaling_matrix_present_flag) {
      for (i = 0; i < 8; i++) {
        int scaling_flag = read_bits (&vdec->seq.br, 1);
        if (scaling_flag) {
          if (i < 6)
            parse_scaling_list (&vdec->seq.br, &tsps.scaling_lists_4x4[i][0], 16, i);
          else
            parse_scaling_list (&vdec->seq.br, &tsps.scaling_lists_8x8[i - 6][0], 64, i);
        } else {
          _vdec_hw_h264_scaling_list_fallback_A ((uint8_t *)tsps.scaling_lists_4x4, (uint8_t *)tsps.scaling_lists_8x8, i);
        }
      }
    }
  }

  tsps.log2_max_frame_num_minus4 = read_exp_ue (&vdec->seq.br);
  lprintf ("log2_max_frame_num_minus4 = %d\n", (int)tsps.log2_max_frame_num_minus4);

  tsps.pic_order_cnt_type = read_exp_ue (&vdec->seq.br);
  lprintf ("pic_order_cnt_type = %d\n", (int)tsps.pic_order_cnt_type);
  if (tsps.pic_order_cnt_type == 0) {
    tsps.log2_max_pic_order_cnt_lsb_minus4 = read_exp_ue (&vdec->seq.br);
    lprintf ("log2_max_pic_order_cnt_lsb_minus4 = %d\n", (int)tsps.log2_max_pic_order_cnt_lsb_minus4);
  } else if (tsps.pic_order_cnt_type == 1) {
    tsps.delta_pic_order_always_zero_flag = read_bits (&vdec->seq.br, 1);
    lprintf ("delta_pic_order_always_zero_flag = %d\n", (int)tsps.delta_pic_order_always_zero_flag);
    tsps.offset_for_non_ref_pic = read_exp_se (&vdec->seq.br);
    lprintf ("offset_for_non_ref_pic = %d\n", (int)tsps.offset_for_non_ref_pic);
    tsps.offset_for_top_to_bottom_field = read_exp_se (&vdec->seq.br);
    lprintf ("offset_for_top_to_bottom_field = %d\n", (int)tsps.offset_for_top_to_bottom_field);
    tsps.ref_frames_used_in_pic_order_cnt_cycle = read_exp_ue (&vdec->seq.br);
    lprintf ("ref_frames_used_in_pic_order_cnt_cycle = %d\n", (int)tsps.ref_frames_used_in_pic_order_cnt_cycle);
    for (i = 0; i < (int)tsps.ref_frames_used_in_pic_order_cnt_cycle; i++) {
      tsps.offset_for_ref_frame[i] = read_exp_se (&vdec->seq.br);
      lprintf ("offset_for_ref_frame[%d] = %d\n", i, (int)tsps.offset_for_ref_frame[i]);
    }
  }

  tsps.ref_frames_used = read_exp_ue (&vdec->seq.br);
  if (tsps.ref_frames_used > 16)
    tsps.ref_frames_used = 16;
  lprintf ("ref_frames_used = %d\n", (int)tsps.ref_frames_used);

  tsps.gaps_in_frame_num_value_allowed_flag = read_bits (&vdec->seq.br, 1);
  lprintf ("gaps_in_frame_num_value_allowed_flag = %d\n", (int)tsps.gaps_in_frame_num_value_allowed_flag);

  tsps.pic_width_in_mbs_minus1 = read_exp_ue (&vdec->seq.br);
  lprintf ("pic_width_in_mbs_minus1 = %d\n", (int)tsps.pic_width_in_mbs_minus1);
  tsps.pic_height_in_map_units_minus1 = read_exp_ue (&vdec->seq.br);
  lprintf ("pic_height_in_map_units_minus1 = %d\n", (int)tsps.pic_height_in_map_units_minus1);

  tsps.frame_mbs_only_flag = read_bits (&vdec->seq.br, 1);
  lprintf ("frame_mbs_only_flag = %d\n", (int)tsps.frame_mbs_only_flag);

  vdec->seq.coded_width = (tsps.pic_width_in_mbs_minus1 + 1) * 16;
  vdec->seq.coded_height = (2 - tsps.frame_mbs_only_flag) * (tsps.pic_height_in_map_units_minus1 + 1) * 16;

  if (!tsps.frame_mbs_only_flag) {
    tsps.mb_adaptive_frame_field_flag = read_bits (&vdec->seq.br, 1);
    lprintf ("mb_adaptive_frame_field_flag = %d\n", (int)tsps.mb_adaptive_frame_field_flag);
  } else {
    tsps.mb_adaptive_frame_field_flag = 0;
  }

  tsps.direct_8x8_inference_flag = read_bits (&vdec->seq.br, 1);
  lprintf ("direct_8x8_inference_flag = %d\n", (int)tsps.direct_8x8_inference_flag);

  tsps.frame_cropping_flag = read_bits (&vdec->seq.br, 1);
  lprintf ("frame_cropping_flag = %d\n", (int)tsps.frame_cropping_flag);
  if (tsps.frame_cropping_flag) {
    tsps.frame_crop_left_offset = read_exp_ue (&vdec->seq.br);
    lprintf ("frame_crop_left_offset = %d\n", (int)tsps.frame_crop_left_offset);
    tsps.frame_crop_right_offset = read_exp_ue (&vdec->seq.br);
    lprintf ("frame_crop_right_offset = %d\n", (int)tsps.frame_crop_right_offset);
    tsps.frame_crop_top_offset = read_exp_ue (&vdec->seq.br);
    lprintf ("frame_crop_top_offset = %d\n", (int)tsps.frame_crop_top_offset);
    tsps.frame_crop_bottom_offset = read_exp_ue (&vdec->seq.br);
    lprintf ("frame_crop_bottom_offset = %d\n", (int)tsps.frame_crop_bottom_offset);
    vdec->seq.coded_height -= (2 - tsps.frame_mbs_only_flag) * 2 * tsps.frame_crop_bottom_offset;
  }

  /* XXX? */
  if (vdec->seq.coded_height == 1088)
    vdec->seq.coded_height = 1080;

  tsps.vui_parameters_present_flag = read_bits (&vdec->seq.br, 1);
  lprintf ("vui_parameters_present_flag = %d\n", (int)tsps.vui_parameters_present_flag);
  if (tsps.vui_parameters_present_flag)
    _vdec_hw_h264_read_vui (vdec, &tsps.vui);
  _vdec_hw_h264_set_ratio (vdec, &tsps);

  sps = vdec->seq.sps[tsps.sps_id];
  if (!sps) {
    vdec->seq.sps[tsps.sps_id] = sps = malloc  (sizeof (*sps));
    if (!sps) {
      vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR, LOG_MODULE ": no memory for SPS #%d.}n", (int)tsps.sps_id);
      return NULL;
    }
    vdec->stats.sps++;
  } else {
    sps->reused = 0;
    if (!memcmp (&tsps, sps, sizeof (tsps)))
      return sps;
  }
  memcpy (sps, &tsps, sizeof (tsps));
  vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_INFO, LOG_MODULE ": new SPS #%d.\n", (int)tsps.sps_id);
  return sps;
}

static vdec_hw_h264_pps_t *_vdec_hw_h264_read_pps (vdec_hw_h264_t *vdec) {
  vdec_hw_h264_pps_t *pps, tpps;
  vdec_hw_h264_sps_t *sps;
  uint32_t more;
  uint8_t num_slice_groups_minus1, bits;
  int i;

  memset (&tpps, 0, sizeof (tpps));

  tpps.pps_id = read_exp_ue (&vdec->seq.br);
  tpps.sps_id = read_exp_ue (&vdec->seq.br);
  if (tpps.sps_id > 31) {
    lprintf ("referenced SPS #%d does not exist!!\n", (int)tpps.sps_id);
    return NULL;
  }
  sps = vdec->seq.sps[tpps.sps_id];
  if (!sps) {
    lprintf ("referenced SPS #%d does not exist!!\n", (int)tpps.sps_id);
    return NULL;
  }

  bits = read_bits (&vdec->seq.br, 2);
  tpps.entropy_coding_mode_flag = (bits >> 1) & 1;
  tpps.pic_order_present_flag   = (bits >> 0) & 1;

  num_slice_groups_minus1 = read_exp_ue (&vdec->seq.br);
  lprintf ("num_slice_groups_minus1 = %d\n", (int)num_slice_groups_minus1);
  if (num_slice_groups_minus1 > 0) {
    uint8_t slice_group_map_type = read_exp_ue (&vdec->seq.br);

    lprintf ("slice_group_map_type = %d\n", (int)slice_group_map_type);
    if (!slice_group_map_type) {
      for (i = 0; i < (int)num_slice_groups_minus1; i++)
        read_exp_ue (&vdec->seq.br);
    } else if (slice_group_map_type == 2) {
      for (i = 0; i < (int)num_slice_groups_minus1; i++) {
        read_exp_ue (&vdec->seq.br);
        read_exp_ue (&vdec->seq.br);
      }
    } else if ((slice_group_map_type == 3) || (slice_group_map_type == 4) || (slice_group_map_type == 5)) {
      read_bits (&vdec->seq.br, 1);
      read_exp_ue (&vdec->seq.br);
    } else if (slice_group_map_type == 6) {
      read_exp_ue (&vdec->seq.br);
    }
  }

  tpps.num_ref_idx_l0_active_minus1 = read_exp_ue (&vdec->seq.br);
  tpps.num_ref_idx_l1_active_minus1 = read_exp_ue (&vdec->seq.br);

  bits = read_bits (&vdec->seq.br, 3);
  tpps.weighted_pred_flag  = (bits >> 2) & 1;
  tpps.weighted_bipred_idc = (bits >> 0) & 3;

  tpps.pic_init_qp_minus26 = read_exp_se (&vdec->seq.br);
  tpps.pic_init_qs_minus26 = read_exp_se (&vdec->seq.br);
  tpps.chroma_qp_index_offset = read_exp_se (&vdec->seq.br);

  bits = read_bits (&vdec->seq.br, 3);
  tpps.deblocking_filter_control_present_flag = (bits >> 2) & 1;
  tpps.constrained_intra_pred_flag = (bits >> 1) & 1;
  tpps.redundant_pic_cnt_present_flag = (bits >> 0) & 1;

  more = more_rbsp_data (&vdec->seq.br);
  /* no typo, we want at least 2 "1" bits. */
  if (more > 1) {
    bits = read_bits (&vdec->seq.br, 2);
    tpps.transform_8x8_mode_flag = (bits >> 1) & 1;
    tpps.pic_scaling_matrix_present_flag = (bits >> 0) & 1;
    if (tpps.pic_scaling_matrix_present_flag) {
      for (i = 0; i < 8; i++) {
        if ((i < 6) || tpps.transform_8x8_mode_flag)
          tpps.pic_scaling_list_present_flag[i] = read_bits (&vdec->seq.br, 1);
        else
          tpps.pic_scaling_list_present_flag[i] = 0;
        if (tpps.pic_scaling_list_present_flag[i]) {
          if (i < 6)
            parse_scaling_list (&vdec->seq.br, &tpps.scaling_lists_4x4[i][0], 16, i);
          else
            parse_scaling_list (&vdec->seq.br, &tpps.scaling_lists_8x8[i - 6][0], 64, i);
        } else {
          if (!sps->seq_scaling_matrix_present_flag)
            _vdec_hw_h264_scaling_list_fallback_A ((uint8_t *)tpps.scaling_lists_4x4, (uint8_t *)tpps.scaling_lists_8x8, i);
          else
            _vdec_hw_h264_scaling_list_fallback_B (sps, &tpps, i);
        }
      }
    }
    tpps.second_chroma_qp_index_offset = read_exp_se (&vdec->seq.br);
    lprintf ("second_chroma_qp_index_offset = %d\n", (int)tpps.second_chroma_qp_index_offset);
  } else {
    tpps.transform_8x8_mode_flag = 0;
    tpps.pic_scaling_matrix_present_flag = 0;
    tpps.second_chroma_qp_index_offset = tpps.chroma_qp_index_offset;
  }

  pps = vdec->seq.pps[tpps.pps_id];
  if (!pps) {
    vdec->seq.pps[tpps.pps_id] = pps = malloc  (sizeof (*pps));
    if (!pps) {
      vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR, LOG_MODULE ": no memory for PPS #%d.}n", (int)tpps.pps_id);
      return NULL;
    }
    vdec->stats.pps++;
  } else {
    if (!memcmp (&tpps, pps, sizeof (tpps)))
      return pps;
  }
  memcpy (pps, &tpps, sizeof (tpps));
  vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_INFO, LOG_MODULE ": new PPS #%d.\n", (int)tpps.pps_id);
  return pps;
}

static void _vdec_hw_h264_pred_weight_table (vdec_hw_h264_t *vdec,
  uint8_t slice_type, uint8_t ChromaArrayType, uint8_t l0, uint8_t l1) {
  int i;

  read_exp_ue (&vdec->seq.br);
  if (ChromaArrayType)
    read_exp_ue (&vdec->seq.br);
  for (i = 0; i <= l0; i++) {
    if (read_bits (&vdec->seq.br, 1)) {
      read_exp_se (&vdec->seq.br);
      read_exp_se (&vdec->seq.br);
    }
    if (ChromaArrayType && read_bits (&vdec->seq.br, 1)) {
      read_exp_se (&vdec->seq.br);
      read_exp_se (&vdec->seq.br);
      read_exp_se (&vdec->seq.br);
      read_exp_se (&vdec->seq.br);
    }
  }
  if (slice_type == SLICE_TYPE_B) {
    for (i = 0; i <= l1; i++) {
      if (read_bits (&vdec->seq.br, 1)) {
        read_exp_se (&vdec->seq.br);
        read_exp_se (&vdec->seq.br);
      }
      if (ChromaArrayType) {
        if (read_bits (&vdec->seq.br, 1)) {
          read_exp_se (&vdec->seq.br);
          read_exp_se (&vdec->seq.br);
          read_exp_se (&vdec->seq.br);
          read_exp_se (&vdec->seq.br);
        }
      }
    }
  }
}

static void _vdec_hw_h264_ref_pic_list_reordering (vdec_hw_h264_t *vdec) {
  vdec_hw_h264_lps_t *lps = &vdec->seq.lps;

  if ((lps->slice_type != SLICE_TYPE_I) && (lps->slice_type != SLICE_TYPE_SI)) {
    if (read_bits (&vdec->seq.br, 1)) {
      uint32_t tmp /*, diff */;
      do {
        tmp = read_exp_ue (&vdec->seq.br);
        if (tmp == 0 || tmp == 1)
          /*diff =*/ read_exp_ue (&vdec->seq.br);
        else if (tmp == 2)
          /*diff =*/ read_exp_ue (&vdec->seq.br);
      }
      while (tmp != 3 && !vdec->seq.br.oflow);
    }
  }
  if (lps->slice_type == SLICE_TYPE_B) {
    if (read_bits (&vdec->seq.br, 1)) {
      uint32_t tmp2/*, diff2*/;
      do {
        tmp2 = read_exp_ue (&vdec->seq.br);
        if (tmp2 == 0 || tmp2 == 1)
          /*diff2 =*/ read_exp_ue (&vdec->seq.br);
        else if (tmp2 == 2)
          /*diff2 =*/ read_exp_ue (&vdec->seq.br);
      }
      while (tmp2 != 3 && !vdec->seq.br.oflow);
    }
  }
}

static void _vdec_hw_h264_dec_ref_pic_marking (vdec_hw_h264_t *vdec, uint8_t idr) {
  int32_t pic_num;

  if (idr) {
#ifdef LOG
    uint8_t no_output_of_prior_pics_flag = read_bits (&vdec->seq.br, 1);
    uint8_t long_term_reference_flag = read_bits (&vdec->seq.br, 1);
    lprintf ("no_output_of_prior_pics_flag = %d\n", (int)no_output_of_prior_pics_flag);
    lprintf ("long_term_reference_flag = %d\n", (int)long_term_reference_flag);
#else
    skip_bits (&vdec->seq.br, 2);
#endif
  } else {
    uint8_t adaptive_ref_pic_marking_mode_flag = read_bits (&vdec->seq.br, 1);
    lprintf ("adaptive_ref_pic_marking_mode_flag = %d\n", (int)adaptive_ref_pic_marking_mode_flag);
    if (!adaptive_ref_pic_marking_mode_flag) {
      if (vdec->frames[MAX_REF_FRAMES].field_pic_flag
        && (vdec->frames[MAX_REF_FRAMES].completed == PICTURE_DONE)
        && (vdec->frames[MAX_REF_FRAMES].is_reference[0] || vdec->frames[MAX_REF_FRAMES].is_reference[1])) {
        vdec->frames[MAX_REF_FRAMES].is_reference[0] = vdec->frames[MAX_REF_FRAMES].is_reference[1] = SHORT_TERM_REF;
        lprintf ("short_ref marking\n");
      }
      // sliding window is always performed in dpb_append()
    } else {
      uint8_t memory_management_control_operation;
      do {
        memory_management_control_operation = read_exp_ue (&vdec->seq.br);
        lprintf ("memory_management_control_operation = %d\n",
          (int)memory_management_control_operation);
        if ((memory_management_control_operation == 1)
          || (memory_management_control_operation == 3)) {
          uint32_t difference_of_pic_nums_minus1 = read_exp_ue (&vdec->seq.br);
          lprintf ("difference_of_pic_nums_minus1 = %d\n", difference_of_pic_nums_minus1);
          pic_num = vdec->frames[MAX_REF_FRAMES].PicNum[0] - (difference_of_pic_nums_minus1 + 1);
          _vdec_hw_h264_dpb_mmc1 (vdec, pic_num);
        }
        if (memory_management_control_operation == 2) {
#ifdef LOG
          uint32_t long_term_pic_num = read_exp_ue (&vdec->seq.br);
          lprintf ("long_term_pic_num = %d\n", (int)long_term_pic_num);
#else
          read_exp_ue (&vdec->seq.br);
#endif
        }
        if ((memory_management_control_operation == 3)
          || (memory_management_control_operation == 6)) {
#ifdef LOG
          uint32_t long_term_frame_idx = read_exp_ue (&vdec->seq.br);
          lprintf ("long_term_frame_idx = %d\n", (int)long_term_frame_idx);
#else
          read_exp_ue (&vdec->seq.br);
#endif
        }
        if (memory_management_control_operation == 4) {
#ifdef LOG
          uint32_t max_long_term_frame_idx_plus1 = read_exp_ue (&vdec->seq.br);
          lprintf ("max_long_term_frame_idx_plus1 = %d\n", max_long_term_frame_idx_plus1);
#else
          read_exp_ue (&vdec->seq.br);
#endif
        }
      } while (memory_management_control_operation && !vdec->seq.br.oflow);
    }
  }
}

static void _vdec_hw_h264_slice_header (vdec_hw_h264_t *vdec, uint8_t nal_ref_idc, uint8_t nal_unit_type) {
  vdec_hw_h264_lps_t *lps = &vdec->seq.lps;
  vdec_hw_h264_pps_t *pps;
  vdec_hw_h264_sps_t *sps;

  lps->nal_ref_idc = nal_ref_idc;
  lps->nal_unit_type = nal_unit_type;

  read_exp_ue (&vdec->seq.br);  /* first_mb_in_slice */
  lps->slice_type = read_exp_ue (&vdec->seq.br) % 5;
  lprintf ("slice_type = %u\n", lps->slice_type);

  lps->pps_id = read_exp_ue (&vdec->seq.br);
  lprintf ("_vdec_hw_h264_read_pps_id = %d\n", (int)lps->pps_id);
  pps = vdec->seq.pps[lps->pps_id];
  if (!pps) {
    vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
      LOG_MODULE ": referenced PPS #%d does not exist!!\n", (int)lps->pps_id);
    vdec->frames[MAX_REF_FRAMES].missing_header = 1;
    return;
  }

  sps = vdec->seq.sps[pps->sps_id];
  if (!sps) {
    vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
      LOG_MODULE ": referenced SPS #%d does not exist!!\n", (int)pps->sps_id);
    vdec->frames[MAX_REF_FRAMES].missing_header = 1;
    return;
  }

  if (!vdec->seq.startup_frame && (lps->slice_type == SLICE_TYPE_I) && !vdec->frames[MAX_REF_FRAMES].completed)
    vdec->seq.startup_frame = 1;

  lprintf ("sps_id = %d\n", (int)pps->sps_id);
  if (sps->separate_colour_plane_flag)
    read_bits (&vdec->seq.br, 2);       /* colour_plane_id */

  lps->frame_num = read_bits (&vdec->seq.br, sps->log2_max_frame_num_minus4 + 4);
  lprintf ("frame_num = %d\n", (int)lps->frame_num);
  lps->MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);

  lps->field_pic_flag = lps->bottom_field_flag = lps->delta_pic_order_cnt_bottom = 0;
  lps->delta_pic_order_cnt[0] = lps->delta_pic_order_cnt[1] = 0;

  if (!sps->frame_mbs_only_flag) {
    lps->field_pic_flag = read_bits (&vdec->seq.br, 1);
    lprintf ("field_pic_flag = %d\n", (int)lps->field_pic_flag);
    if (lps->field_pic_flag) {
      lps->bottom_field_flag = read_bits (&vdec->seq.br, 1);
      lprintf ("bottom_field_flag = %d\n", (int)lps->bottom_field_flag);
    }
  }
  if (nal_unit_type == NAL_SLICE_IDR) {
    lps->idr_pic_id = read_exp_ue (&vdec->seq.br);
    lprintf ("idr_pic_id = %d\n", (int)lps->idr_pic_id);
  }
  if (sps->pic_order_cnt_type == 0) {
    lps->pic_order_cnt_lsb = read_bits (&vdec->seq.br, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    lprintf ("pic_order_cnt_lsb = %d\n", (int)lps->pic_order_cnt_lsb);
    if (pps->pic_order_present_flag && !lps->field_pic_flag) {
      lps->delta_pic_order_cnt_bottom = read_exp_se (&vdec->seq.br);
      lprintf ("delta_pic_order_cnt_bottom = %d\n", (int)lps->delta_pic_order_cnt_bottom);
    }
  }
  if ((sps->pic_order_cnt_type == 1) && !sps->delta_pic_order_always_zero_flag) {
    lps->delta_pic_order_cnt[0] = read_exp_se (&vdec->seq.br);
    lprintf ("delta_pic_order_cnt[0] = %d\n", (int)lps->delta_pic_order_cnt[0]);
    if (pps->pic_order_present_flag && !lps->field_pic_flag) {
      lps->delta_pic_order_cnt[1] = read_exp_se (&vdec->seq.br);
      lprintf ("delta_pic_order_cnt[1] = %d\n", (int)lps->delta_pic_order_cnt[1]);
    }
  }
  if (pps->redundant_pic_cnt_present_flag) {
    lps->redundant_pic_cnt = read_exp_ue (&vdec->seq.br);
    lprintf ("redundant_pic_cnt = %d\n", (int)lps->redundant_pic_cnt);
  }
  if (lps->slice_type == SLICE_TYPE_B)
    skip_bits (&vdec->seq.br, 1);       /* direct_spatial_mv_pred_flag */

  lps->num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_active_minus1;
  lps->num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_active_minus1;

  if ((lps->slice_type == SLICE_TYPE_P) || (lps->slice_type == SLICE_TYPE_SP) || (lps->slice_type == SLICE_TYPE_B)) {
    if (read_bits (&vdec->seq.br, 1)) {
      lprintf ("num_ref_idx_active_override_flag = 1\n");
      lps->num_ref_idx_l0_active_minus1 = read_exp_ue (&vdec->seq.br);
      if (lps->slice_type == SLICE_TYPE_B)
        lps->num_ref_idx_l1_active_minus1 = read_exp_ue (&vdec->seq.br);
      lprintf ("num_ref_idx_l0_active_minus1 = %d\n", (int)lps->num_ref_idx_l0_active_minus1);
      lprintf ("num_ref_idx_l1_active_minus1 = %d\n", (int)lps->num_ref_idx_l1_active_minus1);
    }
  }
}

static void _vdec_hw_h264_slice_header_post (vdec_hw_h264_t *vdec) {
  vdec_hw_h264_lps_t *lps = &vdec->seq.lps;
  vdec_hw_h264_pps_t *pps;
  vdec_hw_h264_sps_t *sps;

  if (!lps->nal_ref_idc)
    return;

  pps = vdec->seq.pps[lps->pps_id];
  sps = vdec->seq.sps[pps->sps_id];

  if ((pps->weighted_pred_flag && ((lps->slice_type == SLICE_TYPE_P) || (lps->slice_type == SLICE_TYPE_SP)))
    || ((pps->weighted_bipred_idc == 1) && (lps->slice_type == SLICE_TYPE_B))) {
    uint8_t chroma = (sps->separate_colour_plane_flag) ? 0 : sps->chroma_format_idc;

    _vdec_hw_h264_pred_weight_table (vdec, lps->slice_type, chroma,
      lps->num_ref_idx_l0_active_minus1, lps->num_ref_idx_l1_active_minus1);
  }

  _vdec_hw_h264_dec_ref_pic_marking (vdec, (lps->nal_unit_type == 5) ? 1 : 0);
}

static void _vdec_hw_h264_decode_poc (vdec_hw_h264_t *vdec) {
  vdec_hw_h264_lps_t *lps = &vdec->seq.lps;
  vdec_hw_h264_pps_t *pps = vdec->seq.pps[lps->pps_id];
  vdec_hw_h264_sps_t *sps = vdec->seq.sps[pps->sps_id];
  int parity = lps->bottom_field_flag ? 1 : 0;

  vdec->frames[MAX_REF_FRAMES].used = 1;
  vdec->frames[MAX_REF_FRAMES].FrameNum = lps->frame_num;
  vdec->frames[MAX_REF_FRAMES].is_reference[parity] = lps->nal_ref_idc;
  vdec->frames[MAX_REF_FRAMES].field_pic_flag = lps->field_pic_flag;

  if (lps->field_pic_flag) {
    if (!vdec->frames[MAX_REF_FRAMES].completed)
      vdec->frames[MAX_REF_FRAMES].f.top_field_first = !parity;
    vdec->frames[MAX_REF_FRAMES].completed |= (parity ? PICTURE_BOTTOM_DONE : PICTURE_TOP_DONE);
  } else {
    vdec->frames[MAX_REF_FRAMES].is_reference[!parity] = vdec->frames[MAX_REF_FRAMES].is_reference[parity];
    vdec->frames[MAX_REF_FRAMES].completed = PICTURE_DONE;
  }

  if (sps->pic_order_cnt_type == 0) {
    vdec_hw_h264_frame_int_t *prev_frame = _vdec_hw_h264_dpb_get_prev_ref (vdec);
    int32_t prevPicOrderCntMsb, prevPicOrderCntLsb;
    uint32_t MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

    vdec->frames[MAX_REF_FRAMES].pic_order_cnt_lsb = lps->pic_order_cnt_lsb;
    vdec->frames[MAX_REF_FRAMES].f.top_field_first = (lps->delta_pic_order_cnt_bottom < 0) ? 0 : 1;

    if (!prev_frame) {
      vdec->frames[MAX_REF_FRAMES].PicOrderCntMsb = vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt =
        vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt = 0;
      return;
    }
    if (lps->nal_unit_type == NAL_SLICE_IDR) {
      prevPicOrderCntMsb = prevPicOrderCntLsb = 0;
    } else if (prev_frame->mmc5) {
      if (!lps->bottom_field_flag) {
        prevPicOrderCntMsb = 0;
        prevPicOrderCntLsb = prev_frame->TopFieldOrderCnt;
      } else {
        prevPicOrderCntMsb = prevPicOrderCntLsb = 0;
      }
    } else {
      prevPicOrderCntMsb = prev_frame->PicOrderCntMsb;
      prevPicOrderCntLsb = prev_frame->pic_order_cnt_lsb;
    }

    if ((lps->pic_order_cnt_lsb < prevPicOrderCntLsb)
      && ((prevPicOrderCntLsb - lps->pic_order_cnt_lsb) >= (int32_t)(MaxPicOrderCntLsb / 2)))
      vdec->frames[MAX_REF_FRAMES].PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb;
    else if ((lps->pic_order_cnt_lsb > prevPicOrderCntLsb)
      && ((lps->pic_order_cnt_lsb - prevPicOrderCntLsb) > (int32_t)(MaxPicOrderCntLsb / 2)))
      vdec->frames[MAX_REF_FRAMES].PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb;
    else
      vdec->frames[MAX_REF_FRAMES].PicOrderCntMsb = prevPicOrderCntMsb;

    if (!lps->field_pic_flag) {
      vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt =
        vdec->frames[MAX_REF_FRAMES].PicOrderCntMsb + lps->pic_order_cnt_lsb;
      vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt =
        vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt + lps->delta_pic_order_cnt_bottom;
    } else {
      if (lps->bottom_field_flag)
        vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt =
          vdec->frames[MAX_REF_FRAMES].PicOrderCntMsb + lps->pic_order_cnt_lsb;
      else
        vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt =
          vdec->frames[MAX_REF_FRAMES].PicOrderCntMsb + lps->pic_order_cnt_lsb;
    }
  } else {
    int16_t FrameNumOffset, prevFrameNumOffset;
    uint16_t MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);

    if (lps->nal_unit_type == NAL_SLICE_IDR) {
      FrameNumOffset = 0;
    } else {
      if (vdec->seq.prevMMC5)
        prevFrameNumOffset = 0;
      else
        prevFrameNumOffset = vdec->seq.prevFrameNumOffset;

      if (vdec->seq.prevFrameNum > lps->frame_num)
        FrameNumOffset = prevFrameNumOffset + MaxFrameNum;
      else
        FrameNumOffset = prevFrameNumOffset;
    }

    if (sps->pic_order_cnt_type == 1) {
      int16_t absFrameNum = 0, picOrderCntCycleCnt = 0,
        frameNumInPicOrderCntCycle = 0, expectedDeltaPerPicOrderCntCycle = 0, expectedPicOrderCnt = 0;
      int i;

      if (sps->ref_frames_used_in_pic_order_cnt_cycle)
        absFrameNum = FrameNumOffset + lps->frame_num;
      if (!lps->nal_ref_idc && (absFrameNum > 0))
        --absFrameNum;

      for (i = 0; i < sps->ref_frames_used_in_pic_order_cnt_cycle; i++)
        expectedDeltaPerPicOrderCntCycle += sps->offset_for_ref_frame[i];

      if (absFrameNum > 0) {
        picOrderCntCycleCnt = (absFrameNum - 1) / sps->ref_frames_used_in_pic_order_cnt_cycle;
        frameNumInPicOrderCntCycle = (absFrameNum - 1) % sps->ref_frames_used_in_pic_order_cnt_cycle;
        expectedPicOrderCnt = picOrderCntCycleCnt * expectedDeltaPerPicOrderCntCycle;
        for (i = 0; i < frameNumInPicOrderCntCycle; i++)
          expectedPicOrderCnt += sps->offset_for_ref_frame[i];
      }
      if (!lps->nal_ref_idc)
        expectedPicOrderCnt += sps->offset_for_non_ref_pic;

      if (!lps->field_pic_flag) {
        vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt =
          expectedPicOrderCnt + lps->delta_pic_order_cnt[0];
        vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt =
          vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt + sps->offset_for_top_to_bottom_field +
          lps->delta_pic_order_cnt[1];
      }
      else if (!lps->bottom_field_flag)
        vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt =
          expectedPicOrderCnt + lps->delta_pic_order_cnt[0];
      else
        vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt =
          expectedPicOrderCnt + sps->offset_for_top_to_bottom_field +
          lps->delta_pic_order_cnt[1];
    } else {
      int32_t tmpPicOrderCnt;

      if (lps->nal_unit_type == NAL_SLICE_IDR)
        tmpPicOrderCnt = 0;
      else if (!lps->nal_ref_idc)
        tmpPicOrderCnt = 2 * (FrameNumOffset + lps->frame_num) - 1;
      else
        tmpPicOrderCnt = 2 * (FrameNumOffset + lps->frame_num);

      if (!lps->field_pic_flag)
        vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt = vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt = tmpPicOrderCnt;
      else if (lps->bottom_field_flag)
        vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt = tmpPicOrderCnt;
      else
        vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt = tmpPicOrderCnt;
    }
    vdec->seq.prevFrameNum = vdec->frames[MAX_REF_FRAMES].FrameNum;
    vdec->seq.prevFrameNumOffset = FrameNumOffset;
  }

  if (vdec->frames[MAX_REF_FRAMES].completed < PICTURE_DONE) {
    if (lps->bottom_field_flag)
      vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt = vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt;
    else
      vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt = vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt;
  }
}

static void _vdec_hw_h264_decode_picnum (vdec_hw_h264_t *vdec) {
  vdec_hw_h264_lps_t *lps = &vdec->seq.lps;
  vdec_hw_h264_frame_int_t *frame;
  int parity = lps->bottom_field_flag ? 1 : 0;
  uint32_t i;

  frame = &vdec->frames[MAX_REF_FRAMES];
  if (!frame->field_pic_flag)
    frame->PicNum[0] = frame->FrameNum;
  else
    frame->PicNum[parity] = 2 * frame->FrameNum + 1;

  for (i = 0; i < vdec->ref_frames_used; i++) {
    frame = vdec->seq.dpb[i];
    if (frame->FrameNum > vdec->frames[MAX_REF_FRAMES].FrameNum)
      frame->FrameNumWrap = frame->FrameNum - lps->MaxFrameNum;
    else
      frame->FrameNumWrap = frame->FrameNum;

    if (!lps->field_pic_flag) {
      frame->PicNum[0] = frame->PicNum[1] = frame->FrameNumWrap;
    } else {
      frame->PicNum[0] = 2 * frame->FrameNumWrap + (parity ? 0 : 1);
      frame->PicNum[1] = 2 * frame->FrameNumWrap + (parity ? 1 : 0);
    }
  }
}

static int _vdec_hw_h264_check_ref_list (vdec_hw_h264_t *vdec) {
  int i, j, bad_frame = 0;
  vdec_hw_h264_frame_int_t *frame;
  vdec_hw_h264_lps_t *lps = &vdec->seq.lps;
  vdec_hw_h264_pps_t *pps = vdec->seq.pps[lps->pps_id];
  vdec_hw_h264_sps_t *sps = vdec->seq.sps[pps->sps_id];
  int prefs = 0;
  int brefs = 0;
  int poc, curpoc;
  int fps;

  // int fps = (double)sps->vui.time_scale / (double)sps->vui.num_units_in_tick / ( 2 - lps->field_pic_flag );
  fps = (1 + lps->field_pic_flag) * 2 * sps->ref_frames_used;

  if (vdec->seq.startup_frame >= fps)
    return 0;

  curpoc =
    (vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt > vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt)
    ? vdec->frames[MAX_REF_FRAMES].TopFieldOrderCnt : vdec->frames[MAX_REF_FRAMES].BottomFieldOrderCnt;

  for (i = vdec->ref_frames_used - 1; i >= 0; i--) {
    frame = vdec->seq.dpb[i];
    poc = (frame->TopFieldOrderCnt > frame->BottomFieldOrderCnt)
        ? frame->TopFieldOrderCnt : frame->BottomFieldOrderCnt;
    if (vdec->frames[MAX_REF_FRAMES].field_pic_flag) {
      if (!frame->f.bad_frame) {
        for (j = 0; j < 2; j++) {
          if (frame->is_reference[j]) {
            if (poc <= curpoc)
              ++prefs;
            else
              ++brefs;
          }
        }
      }
    } else {
      if (!frame->f.bad_frame) {
        if (poc <= curpoc)
          ++prefs;
        else
          ++brefs;
      }
    }
  }

  if (lps->slice_type != SLICE_TYPE_I) {
    if (prefs < (lps->num_ref_idx_l0_active_minus1 + 1))
      bad_frame = 1;
    if (lps->slice_type == SLICE_TYPE_B) {
      if (brefs < (lps->num_ref_idx_l1_active_minus1 + 1))
        bad_frame = 1;
    }
  }

  if (bad_frame) {
    vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
      LOG_MODULE ": Missing refframes, dropping. nrf=%d lo=%d prefs=%d l1=%d brefs=%d type=%d (%d fps)\n",
        (int)sps->ref_frames_used, (int)lps->num_ref_idx_l0_active_minus1 + 1, (int)prefs,
        (int)lps->num_ref_idx_l1_active_minus1 + 1, (int)brefs, (int)lps->slice_type, fps);
  }
/*
  else {
    vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_DEBUG,
        LOG_MODULE ": GOOD ! nrf=%d lo=%d prefs=%d l1=%d brefs=%d type=%d (%d fps)\n",
        (int)sps->ref_frames_used, (int)lps->num_ref_idx_l0_active_minus1 + 1, (int)prefs,
        (int)lps->num_ref_idx_l1_active_minus1 + 1, (int)brefs, lps->slice_type, fps );
  ]
*/
  if (vdec->frames[MAX_REF_FRAMES].is_reference[0] || vdec->frames[MAX_REF_FRAMES].is_reference[1])
    ++vdec->seq.startup_frame;

  return bad_frame;
}

static void _vdec_hw_h264_render (vdec_hw_h264_t *vdec, int bad_frame) {
  int i, j;
  vdec_hw_h264_frame_int_t *frame;
  vdec_hw_h264_frame_info_t info;
  vdec_hw_h264_sps_t *sps;
  vdec_hw_h264_pps_t *pps;
  vdec_hw_h264_lps_t *lps;

  frame = vdec->frames + MAX_REF_FRAMES;

  frame->f.width        = vdec->seq.coded_width;
  frame->f.height       = vdec->seq.coded_height;
  frame->f.duration     = vdec->seq.video_step;
  frame->f.ratio        = vdec->seq.user_ratio > 0.001 ? vdec->seq.user_ratio : vdec->seq.ratio;
  frame->f.color_matrix = vdec->seq.color_matrix;
  frame->f.flags        = VDEC_HW_H264_FRAME_TOP_FIELD | VDEC_HW_H264_FRAME_BOTTOM_FIELD | vdec->seq.reset;
  if (frame->completed == PICTURE_DONE) {
    frame->f.pts = vdec->seq.pic_pts;
    vdec->seq.pic_pts = 0;
  }
  if (frame->drop_pts)
    frame->f.pts = 0;
  frame->f.bad_frame = bad_frame;

  lps = &vdec->seq.lps;
  pps = vdec->seq.pps[lps->pps_id];
  if (!pps)
    return;
  sps = vdec->seq.sps[pps->sps_id];
  if (!sps)
    return;

  frame->f.profile = sps->profile_idc;
  frame->f.level   = sps->level_idc;
  frame->f.num_ref_frames = sps->ref_frames_used;
  if (sps->frame_mbs_only_flag)
    frame->f.progressive_frame = -1;

  if (!frame->field_pic_flag || (frame->completed < PICTURE_DONE)) {
    _vdec_hw_h264_frame_new (vdec, frame);
    vdec->seq.reset = 0;
  }

  info.field_order_cnt[0] = frame->TopFieldOrderCnt;
  info.field_order_cnt[1] = frame->BottomFieldOrderCnt;

  info.is_reference = lps->nal_ref_idc ? 1 : 0;
  info.frame_num = lps->frame_num;
  info.field_pic_flag = lps->field_pic_flag;
  info.bottom_field_flag = lps->bottom_field_flag;
  info.num_ref_frames = sps->ref_frames_used;
  info.mb_adaptive_frame_field_flag = sps->mb_adaptive_frame_field_flag && !lps->field_pic_flag;
  info.constrained_intra_pred_flag = pps->constrained_intra_pred_flag;
  info.weighted_pred_flag = pps->weighted_pred_flag;
  info.weighted_bipred_idc = pps->weighted_bipred_idc;
  info.frame_mbs_only_flag = sps->frame_mbs_only_flag;
  info.transform_8x8_mode_flag = pps->transform_8x8_mode_flag;
  info.chroma_qp_index_offset = pps->chroma_qp_index_offset;
  info.second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset;
  info.pic_init_qp_minus26 = pps->pic_init_qp_minus26;
  info.num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_active_minus1;
  info.num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_active_minus1;
  info.log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4;
  info.pic_order_cnt_type = sps->pic_order_cnt_type;
  info.log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4;
  info.delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag;
  info.direct_8x8_inference_flag = sps->direct_8x8_inference_flag;
  info.entropy_coding_mode_flag = pps->entropy_coding_mode_flag;
  info.pic_order_present_flag = pps->pic_order_present_flag;
  info.deblocking_filter_control_present_flag = pps->deblocking_filter_control_present_flag;
  info.redundant_pic_cnt_present_flag = pps->redundant_pic_cnt_present_flag;

  info.slice_count = vdec->seq.slices_count;
  info.slices_bitstream = (const uint8_t * const *)vdec->seq.slices_bitstream;
  info.slices_bytes = (const uint32_t *)vdec->seq.slices_bytes;

  if (!pps->pic_scaling_matrix_present_flag) {
    memcpy (info.scaling_lists_4x4, sps->scaling_lists_4x4, sizeof (info.scaling_lists_4x4));
    memcpy (info.scaling_lists_8x8, sps->scaling_lists_8x8, sizeof (info.scaling_lists_8x8));
  } else {
    memcpy (info.scaling_lists_4x4, pps->scaling_lists_4x4, sizeof (info.scaling_lists_4x4));
    memcpy (info.scaling_lists_8x8, pps->scaling_lists_8x8, sizeof (info.scaling_lists_8x8));
  }

  for (i = vdec->ref_frames_used - 1, j = 0; i >= 0; i--, j++) {
    vdec_hw_h264_info_ref_frame_t *ref = info.referenceFrames + j;
    vdec_hw_h264_frame_int_t *rf = vdec->seq.dpb[i];

    ref->frame = &rf->f;
    ref->is_long_term = 0;
    ref->frame_idx = rf->FrameNum;
    ref->top_is_reference = rf->is_reference[0] ? 1 : 0;
    ref->bottom_is_reference = rf->is_reference[1] ? 1 : 0;
    ref->field_order_cnt[0] = rf->TopFieldOrderCnt;
    ref->field_order_cnt[1] = rf->BottomFieldOrderCnt;
  }
  for (; j < MAX_REF_FRAMES; j++) {
    vdec_hw_h264_info_ref_frame_t *ref = info.referenceFrames + j;

    ref->frame = NULL;
    ref->is_long_term = 0;
    ref->frame_idx = 0;
    ref->top_is_reference = 0;
    ref->bottom_is_reference = 0;
    ref->field_order_cnt[0] = 0;
    ref->field_order_cnt[1] = 0;
  }

  frame->f.info = &info;
  if (vdec->frame_render)
    vdec->frame_render (vdec->user_data, &frame->f);
  frame->f.info = NULL;
}

static void _vdec_hw_h264_decode_picture (vdec_hw_h264_t *vdec) {
  vdec_hw_h264_frame_int_t *cur_frame = &vdec->frames[MAX_REF_FRAMES];
  vdec_hw_h264_lps_t *lps = &vdec->seq.lps;

  if (cur_frame->missing_header || !vdec->seq.startup_frame) {
    _vdec_hw_h264_frame_free (vdec, cur_frame, 1);
    lprintf ("MISSING_HEADER or !startup_frame\n\n");
    return;
  }

  if (cur_frame->completed && cur_frame->field_pic_flag) {
    int wrong_field = 0;
    if ((lps->frame_num != cur_frame->FrameNum)
      || (lps->bottom_field_flag && (cur_frame->completed == PICTURE_BOTTOM_DONE))
      || (!lps->bottom_field_flag && (cur_frame->completed == PICTURE_TOP_DONE))
      || !lps->field_pic_flag) {
      wrong_field = 1;
    }
    if (wrong_field) {
      fprintf (stderr, "vdpau_h264_alter : Wrong field, skipping.\n");
      _vdec_hw_h264_frame_free (vdec, cur_frame, 1);
      _vdec_hw_h264_dpb_reset (vdec);
      cur_frame->missing_header = 1;
      vdec->seq.startup_frame = 0;
      return;
    }
  }

  /* picture decoding */
  _vdec_hw_h264_decode_poc (vdec);
  lprintf ("TopFieldOrderCnt = %d - BottomFieldOrderCnt = %d\n",
    cur_frame->TopFieldOrderCnt, cur_frame->BottomFieldOrderCnt);
  if (lps->nal_unit_type == 5) {
    _vdec_hw_h264_dpb_draw_frames (vdec, MAX_POC, DPB_DRAW_CLEAR);
    vdec->seq.startup_frame = START_IDR_FLAG;
  }
  _vdec_hw_h264_decode_picnum (vdec);
  _vdec_hw_h264_ref_pic_list_reordering (vdec);
  lprintf ("............................. slices_count = %d\n", vdec->seq.slices_count);

  _vdec_hw_h264_render (vdec, _vdec_hw_h264_check_ref_list (vdec));

  /* _vdec_hw_h264_dec_ref_pic_marking */
  _vdec_hw_h264_slice_header_post (vdec);

  if (!cur_frame->is_reference[0] && !cur_frame->is_reference[1]) {
    if (cur_frame->completed == PICTURE_DONE) {
      _vdec_hw_h264_dpb_draw_frames (vdec,
        (cur_frame->TopFieldOrderCnt > cur_frame->BottomFieldOrderCnt)
        ? cur_frame->TopFieldOrderCnt : cur_frame->BottomFieldOrderCnt,
        DPB_DRAW_CURRENT);
    }
  } else {
    if (vdec->seq.sps[vdec->seq.pps[lps->pps_id]->sps_id]->pic_order_cnt_type == 2)
      _vdec_hw_h264_dpb_draw_frames (vdec,
        (cur_frame->TopFieldOrderCnt > cur_frame->BottomFieldOrderCnt)
        ? cur_frame->TopFieldOrderCnt : cur_frame->BottomFieldOrderCnt,
        DPB_DRAW_REFS);
    _vdec_hw_h264_dbp_append (vdec,
      (!lps->field_pic_flag || cur_frame->completed < PICTURE_DONE) ? 0 : 1);
  }

  if (cur_frame->completed == PICTURE_DONE)
    _vdec_hw_h264_frame_free (vdec, cur_frame, 1);

  lprintf ("\n___________________________________________________________________________________________\n\n");
}

static int _vdec_hw_h264_parse_startcodes (vdec_hw_h264_t *vdec, uint8_t *buf, uint32_t len) {
  int ret = 0;
  uint8_t nal_ref_idc, nal_unit_type;
  uint32_t ulen;
  vdec_hw_h264_sps_t *sps;

  /* forbidden_zero_bit 7 */
  nal_ref_idc = (*buf >> 5) & 3;
  nal_unit_type = *buf & 0x1f;
  lprintf ("NAL size = %d, nal_ref_idc = %d, nal_unit_type = %d\n", (int)len, (int)nal_ref_idc, (int)nal_unit_type);

  /* a lost reader looks for standard nal heads (0, 0, 1, type) to get back on track.
   * unfortunately, such byte sequence may appear elsewhere - for example from sps.vui
   * frame timing fields who are plain uint32, not exp_ue.
   * encoders/muxers work around and insert 0x03 break bytes.
   * remove them here (not generally in bits reader like before). */

  switch (nal_unit_type) {
    case NAL_END_SEQUENCE:
      _vdec_hw_h264_dpb_draw_frames (vdec, MAX_POC, DPB_DRAW_CLEAR);
      vdec->seq.reset = VDEC_HW_H264_FRAME_NEW_SEQ;
      vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_INFO,
        LOG_MODULE ": sequence end.\n");
      /* TJ. got some buggy .mp4 files there made from multiple sequences.
       * the NAL_END_SEQUENCE's are still there, buf the NAL_SEQUENCE's have
       * been dropped in favour of a single global config. */
      sps = _vdec_hw_h264_get_sps (vdec);
      if (sps)
        sps->reused = 1;
      break;
    case NAL_SEQUENCE:
      /* this is just for us, unescape and read in place. */
      ulen = _vdec_hw_h264_unescape (buf + 1, len);
      bits_reader_set (&vdec->seq.br, buf + 1, ulen);
      sps = _vdec_hw_h264_read_sps (vdec);
      if (!sps) {
        vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
          LOG_MODULE ": ERROR: sequence start failed.\n");
      }
      break;
    case NAL_PICTURE:
      /* this is just for us, unescape and read in place. */
      ulen = _vdec_hw_h264_unescape (buf + 1, len);
      bits_reader_set (&vdec->seq.br, buf + 1, ulen);
      _vdec_hw_h264_read_pps (vdec);
      break;
    case NAL_SLICE_IDR:
    case NAL_SLICE_NO_IDR:
      vdec->seq.slice_mode = nal_unit_type;
      /* copy, unescape and read the small head here, and pass the whole original to hw later. */
      ulen = len > 256 ? 256 : len;
      memcpy (vdec->tempbuf, buf + 1, ulen);
      ulen = _vdec_hw_h264_unescape (vdec->tempbuf, ulen);
      bits_reader_set (&vdec->seq.br, vdec->tempbuf, ulen);
      _vdec_hw_h264_slice_header (vdec, nal_ref_idc, nal_unit_type);
      vdec->seq.slices_bitstream[vdec->seq.slices_count] = buf;
      vdec->seq.slices_bytes[vdec->seq.slices_count] = len;
      if (vdec->seq.slices_count < MAX_SLICES) {
        vdec->seq.slices_count++;
        if ((int)vdec->seq.slices_count > vdec->stats.slices)
          vdec->stats.slices = vdec->seq.slices_count;
      } else {
        vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
          LOG_MODULE ": too many slices!!\n");
      }
      break;
    case NAL_SEI:
    case NAL_SEEK_POINT:
    case NAL_FILLER:
      break;
    default:
      vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_INFO,
        LOG_MODULE ": unhandled nal unit type %d.\n", (int)nal_unit_type);
  }

  return ret;
}

int vdec_hw_h264_put_config (vdec_hw_h264_t *vdec, const uint8_t *bitstream, uint32_t num_bytes) {
  const uint8_t *buf, *e;
  uint32_t count, i;

  if (!vdec || !bitstream || (num_bytes < 7))
    return 0;

  lprintf ("vdec_hw_h264_put_config\n");

  /* reserved:8, profile_idc:8, reserved:8, level_idc:8, reserved:6 frame_header_size_minus_1:2 */
  vdec->seq.nal_unit_prefix = (bitstream[4] & 3) + 1;

  buf = bitstream + 5;
  e = bitstream + num_bytes;
  /* reserved:3, sps_count:5 */
  count = *buf++ & 31;
  for (i = 0; i < count; i++) {
    uint32_t sps_size, s2;

    if (buf + 2 > e)
      return 1;
    sps_size = ((uint32_t)buf[0] << 8) + buf[1], buf += 2;
    if (buf + sps_size > e)
      sps_size = e - buf;
    memcpy (vdec->tempbuf, buf, sps_size);
    s2 = _vdec_hw_h264_unescape (vdec->tempbuf, sps_size);
    /* reserved:8 */
    bits_reader_set (&vdec->seq.br, vdec->tempbuf + 1, s2 ? s2 - 1 : 0);
    _vdec_hw_h264_read_sps (vdec);
    buf += sps_size;
  }
  if (buf + 1 > e)
    return 1;
  count = *buf++;
  for (i = 0; i < count; i++) {
    uint32_t pps_size, s2;

    if (buf + 2 > e)
      return 1;
    pps_size = ((uint32_t)buf[0] << 8) + buf[1], buf += 2;
    if (buf + pps_size > e)
      pps_size = e - buf;
    memcpy (vdec->tempbuf, buf, pps_size);
    s2 = _vdec_hw_h264_unescape (vdec->tempbuf, pps_size);
    /* reserved:8 */
    bits_reader_set (&vdec->seq.br, vdec->tempbuf + 1, s2 ? s2 - 1 : 0);
    _vdec_hw_h264_read_pps (vdec);
    buf += pps_size;
  }
  return 1;
}

static void _vdec_hw_h264_flush_buffer (vdec_hw_h264_t *vdec) {
  uint32_t drop = vdec->seq.buf.nal_unit >= 0 ? (uint32_t)vdec->seq.buf.nal_unit : vdec->seq.buf.read;
  uint32_t keep = vdec->seq.buf.write - drop;

  if (drop) {
    int i;

    if (keep) {
      if (keep > drop)
        memmove (vdec->seq.buf.mem, vdec->seq.buf.mem + drop, keep);
      else
        memcpy (vdec->seq.buf.mem, vdec->seq.buf.mem + drop, keep);
    }
    for (i = 0; i < vdec->seq.slices_count; i++)
      vdec->seq.slices_bitstream[i] -= drop;
  }
  vdec->seq.buf.write = keep;
  vdec->seq.buf.read -= drop;
  if (vdec->seq.buf.nal_unit >= 0)
    vdec->seq.buf.nal_unit -= drop;
}

int vdec_hw_h264_put_frame (vdec_hw_h264_t *vdec, int64_t pts, const uint8_t *bitstream, uint32_t num_bytes, int frame_end) {
  uint32_t nal_unit_prefix = vdec->seq.nal_unit_prefix;
  int flush;

  if (!vdec)
    return 1;

  if (!bitstream || !num_bytes) {
    if (!frame_end)
      return 1;
  } else {
    uint32_t s = vdec->seq.buf.write + num_bytes;

    if (s > vdec->seq.buf.max) {
      if (s > MAX_BUFFER_SIZE)
        vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
          LOG_MODULE ": frame too large, truncating.\n");
      s = s * 3 / 2;
      if (s > MAX_BUFFER_SIZE)
        s = MAX_BUFFER_SIZE;
      if (s > vdec->seq.buf.max) {
        uint8_t *nb = realloc (vdec->seq.buf.mem, s + BUF_PAD);

        if (nb) {
          int i;

          for (i = 0; i < vdec->seq.slices_count; i++)
            vdec->seq.slices_bitstream[i] = nb + (vdec->seq.slices_bitstream[i] - vdec->seq.buf.mem);
          vdec->seq.buf.mem = nb;
          vdec->seq.buf.max = s;
          vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_DEBUG,
            LOG_MODULE ": enlarged bitstream buffer to %u bytes.\n", (unsigned int)vdec->seq.buf.max);
        } else {
          vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
            LOG_MODULE ": cannot enlarge bitstream buffer, truncating.\n");
        }
      }
    }
    s = vdec->seq.buf.max - vdec->seq.buf.write;
    if (s > num_bytes)
      s = num_bytes;
    memcpy (vdec->seq.buf.mem + vdec->seq.buf.write, bitstream, s);
    vdec->seq.buf.write += s;
    memset (vdec->seq.buf.mem + vdec->seq.buf.write, 0, BUF_PAD);
  }

  /* always decode into cur_frame (vdec->frames[MAX_REF_FRAMES]).
   * if this is a reference frame, move it to the dpb array.
   * otherwise, draw and free immediately. */

  /* TJ. I once made an .mp4 file with ffmpeg -vcodec copy. It has a global config (just fine),
   * but then the frames have standard nal units... */
  if (nal_unit_prefix && (vdec->seq.buf.write - vdec->seq.buf.read > 4)) {
    if (!memcmp (vdec->seq.buf.mem + vdec->seq.buf.read, "\x00\x00\x00\x01", 4)
        && ((vdec->seq.buf.mem[vdec->seq.buf.read + 4] & 0x1f) != NAL_END_SEQUENCE))
      nal_unit_prefix = 0;
  }
  if (nal_unit_prefix) {
    if (!vdec->seq.pic_pts)
      vdec->seq.pic_pts = pts;
    if (frame_end) {
      uint8_t *p = vdec->seq.buf.mem + vdec->seq.buf.read;
      uint8_t *e = vdec->seq.buf.mem + vdec->seq.buf.write;

      lprintf ("frame_end && vdec->seq.nal_unit_prefix\n");
      while (p < e) {
        uint8_t tb;
        uint32_t s = 0;

        vdec->seq.buf.read = p - vdec->seq.buf.mem;
        switch (vdec->seq.nal_unit_prefix) {
          case 4:
            s = *p++;
            s <<= 8;
            /* fall through */
          case 3:
            s += *p++;
            s <<= 8;
            /* fall through */
          case 2:
            s += *p++;
            s <<= 8;
            /* fall through */
          default:
            s += *p++;
        }
        if (p >= e)
          break;
        if ((s > 0x00ffffff) || (p + s > e))
          s = e - p;
        tb = *p & 0x1F;
        if (vdec->seq.slice_mode && (tb != vdec->seq.slice_mode)) {
          _vdec_hw_h264_decode_picture (vdec);
          _vdec_hw_h264_reset_slices (vdec);
        }
        _vdec_hw_h264_parse_startcodes (vdec, p, s);
        p += s;
      }
      if (vdec->seq.slice_mode) {
        _vdec_hw_h264_decode_picture (vdec);
        _vdec_hw_h264_reset_slices (vdec);
      }
      vdec->seq.buf.read =
      vdec->seq.buf.write = 0;
    }
    return 0;
  }

  flush = 0;
  while (1) {
    uint8_t *b = vdec->seq.buf.mem + vdec->seq.buf.read;
    uint8_t *e = vdec->seq.buf.mem + vdec->seq.buf.write;
    uint32_t v;

    memcpy (e, "\x00\x00\x01", 4);
    v = 0xffffff00;
    do {
      v = (v + *b++) << 8;
    } while (v != 0x00000100);
    if (b >= e) {
      /* we hit our fake stop mark. last unit may or may not be complete.
       * rewind read pos a bit to amke sure that a possible begun standard unit
       * prefix will be recognized next time. */
      vdec->seq.buf.read = vdec->seq.buf.read + 3 > vdec->seq.buf.write ? vdec->seq.buf.read : vdec->seq.buf.write - 3;
      break;
    }
    vdec->seq.buf.read = b - vdec->seq.buf.mem - 3;
    if (vdec->seq.buf.nal_unit >= 0) {
      _vdec_hw_h264_parse_startcodes (vdec, vdec->seq.buf.mem + vdec->seq.buf.nal_unit + 3,
        vdec->seq.buf.read - vdec->seq.buf.nal_unit - 3);
      vdec->seq.buf.nal_unit = -1;
    }
    {
      uint8_t tb = b[0] & 0x1f;

      vdec->seq.buf.nal_unit = vdec->seq.buf.read;
      if (((tb == NAL_SLICE_NO_IDR) || (tb == NAL_SLICE_IDR)) && !vdec->seq.pic_pts)
        vdec->seq.pic_pts = pts;
      if (vdec->seq.slice_mode && (tb != vdec->seq.slice_mode)) {
        _vdec_hw_h264_decode_picture (vdec);
        _vdec_hw_h264_reset_slices (vdec);
        flush = 1;
      }
      if (tb == NAL_END_SEQUENCE) {
        _vdec_hw_h264_dpb_draw_frames (vdec, MAX_POC, DPB_DRAW_CLEAR);
        lprintf ("NAL_END_SEQUENCE\n");
      }
      vdec->seq.buf.read += 1;
    }
    if (vdec->seq.buf.read > vdec->seq.buf.write)
      vdec->seq.buf.read = vdec->seq.buf.write;
  }
  /* sequence end just has the type byte. do it right now. */
  if ((vdec->seq.buf.nal_unit >= 0) && ((vdec->seq.buf.mem[vdec->seq.buf.nal_unit + 3] & 0x1f) == NAL_END_SEQUENCE)) {
    _vdec_hw_h264_parse_startcodes (vdec, vdec->seq.buf.mem + vdec->seq.buf.nal_unit + 3, 1);
    vdec->seq.buf.nal_unit = -1;
  }
  /* frame_end seems to be unreliable here. */
  if (flush)
    _vdec_hw_h264_flush_buffer (vdec);
  return 0;
}

int vdec_hw_h264_flush (vdec_hw_h264_t *vdec) {
  uint32_t n;

  lprintf ("vdec_hw_h264_flush\n");
  if (!vdec)
    return 0;
  if ((vdec->seq.buf.nal_unit >= 0) && ((uint32_t)vdec->seq.buf.nal_unit + 3 < vdec->seq.buf.write)) {
    _vdec_hw_h264_parse_startcodes (vdec, vdec->seq.buf.mem + vdec->seq.buf.nal_unit + 3,
      vdec->seq.buf.write - vdec->seq.buf.nal_unit - 3);
    vdec->seq.buf.nal_unit = -1;
    if (vdec->seq.slice_mode) {
      _vdec_hw_h264_decode_picture (vdec);
      _vdec_hw_h264_reset_slices (vdec);
    }
  }
  n = vdec->ref_frames_used;
  _vdec_hw_h264_dpb_draw_frames (vdec, MAX_POC, DPB_DRAW_REFS);
  _vdec_hw_h264_flush_buffer (vdec);
  return n;
}

vdec_hw_h264_t *vdec_hw_h264_new (
  int (*logg) (void *user_data, vdec_hw_h264_logg_t level, const char *fmt, ...),
  void *user_data,
  int  (*frame_new)    (void *user_data, vdec_hw_h264_frame_t *frame),
  int  (*frame_render) (void *user_data, vdec_hw_h264_frame_t *frame),
  int  (*frame_ready)  (void *user_data, vdec_hw_h264_frame_t *frame),
  void (*frame_delete) (void *user_data, vdec_hw_h264_frame_t *frame),
  int num_frames
) {
  vdec_hw_h264_t *vdec;
  uint32_t u;

  vdec = calloc (1, sizeof (*vdec));
  if (!vdec)
    return NULL;

  vdec->logg = logg ? logg : _vdec_hw_h264_dummy_logg;
  vdec->user_data = user_data;

  vdec->frame_new    = frame_new;
  vdec->frame_render = frame_render;
  vdec->frame_ready  = frame_ready;
  vdec->frame_delete = frame_delete;

  vdec->ref_frames_max = num_frames <= 1 ? 1 : num_frames > MAX_REF_FRAMES + 1 ? MAX_REF_FRAMES : num_frames - 1;

  vdec->seq.buf.mem = malloc (MIN_BUFFER_SIZE + BUF_PAD);
  if (!vdec->seq.buf.mem) {
    free (vdec);
    return NULL;
  }
  vdec->seq.buf.max = MIN_BUFFER_SIZE;
  vdec->seq.buf.read = 0;
  vdec->seq.buf.write = 0;

  for (u = 0; u < MAX_REF_FRAMES + 1; u++)
    vdec->seq.dpb[u] = vdec->frames + u;
  vdec->ref_frames_used = 0;
  vdec->user_frames = 0;

  vdec->seq.reset = VDEC_HW_H264_FRAME_NEW_SEQ;
  vdec->seq.ratio = 0.0;
  vdec->seq.user_ratio = 0.0;
  vdec->seq.video_step = 3600;
  vdec->seq.coded_width = 1280;
  vdec->seq.coded_height = 720;
  vdec->seq.nal_unit_prefix = 0;
  _vdec_hw_h264_reset_sequence (vdec);

  for (u = 0; u < MAX_SPS; u++)
    vdec->seq.sps[u] = NULL;
  for (u = 0; u < MAX_PPS; u++)
    vdec->seq.pps[u] = NULL;

  vdec->stats.sps = 0;
  vdec->stats.pps = 0;
  vdec->stats.slices = 0;
  vdec->stats.frame_ready = 0;

  return vdec;
}

int vdec_hw_h264_reset (vdec_hw_h264_t *vdec) {
  int n = 0;

  if (!vdec)
    return 0;

  n = vdec->ref_frames_used;
  _vdec_hw_h264_reset_sequence (vdec);
  return n;
}

int vdec_hw_h264_put_container_info (vdec_hw_h264_t *vdec, int width, int height, int duration, double ratio) {
  if (!vdec)
    return 0;
  if (width > 0)
    vdec->seq.coded_width = width;
  if (height > 0)
    vdec->seq.coded_height = height;
  if (duration > 0)
    vdec->seq.video_step = duration;
  if (ratio > 0.001)
    vdec->seq.user_ratio = ratio;
  return 1;
}

void vdec_hw_h264_delete (vdec_hw_h264_t **dec) {
  vdec_hw_h264_t *vdec;
  uint32_t u;

  if (!dec)
    return;
  vdec = *dec;
  if (!vdec)
    return;
  *dec = NULL;

  for (u = 0; u < vdec->ref_frames_used; u++)
    _vdec_hw_h264_frame_free (vdec, vdec->seq.dpb[u], 1);
  vdec->ref_frames_used = 0;
  _vdec_hw_h264_frame_free (vdec, vdec->frames + MAX_REF_FRAMES, 1);
  vdec_hw_h264_reset (vdec);
  _vdec_hw_h264_reset_sequence (vdec);
  if (vdec->user_frames)
    vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_ERR,
      LOG_MODULE ": ERROR: %d user frames still in use.\n", (int)vdec->user_frames);
  for (u = 0; u < MAX_SPS; u++)
    if (vdec->seq.sps[u])
      free (vdec->seq.sps[u]);
  for (u = 0; u < MAX_PPS; u++)
    if (vdec->seq.pps[u])
      free (vdec->seq.pps[u]);
  free (vdec->seq.buf.mem);

  vdec->logg (vdec->user_data, VDEC_HW_H264_LOGG_INFO,
    LOG_MODULE ": used %d SPS, %d PPS, %d slices per frame, %d stream bytes, %d render calls.\n",
    vdec->stats.sps, vdec->stats.pps, vdec->stats.slices,
    (int)vdec->seq.buf.max, (int)vdec->stats.frame_ready);

  free (vdec);
}
