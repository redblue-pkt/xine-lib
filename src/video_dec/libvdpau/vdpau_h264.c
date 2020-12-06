/*
 * Copyright (C) 2008-2020 the xine project
 * Copyright (C) 2008-2009 Julian Scheel
 *
 * kate: space-indent on; indent-width 2; mixedindent off; indent-mode cstyle; remove-trailing-space on;
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
 * vdpau_h264.c: H264 Video Decoder utilizing nvidia VDPAU engine
 */

#define LOG_MODULE "vdpau_h264"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <vdpau/vdpau.h>

#include <xine/xine_internal.h>
#include <xine/video_out.h>
#include <xine/buffer.h>
#include <xine/xineutils.h>
#include <xine/list.h>
#include "bswap.h"
#include "accel_vdpau.h"
#include "group_vdpau.h"

//#define DEBUG_H264
//#define DEBUG_DPB

/*************************************************************************
* nal.c: nal-structure utility functions                                 *
*************************************************************************/

enum nal_unit_types
{
  NAL_UNSPECIFIED = 0,
  NAL_SLICE,
  NAL_PART_A,
  NAL_PART_B,
  NAL_PART_C,
  NAL_SLICE_IDR,
  NAL_SEI,
  NAL_SPS,
  NAL_PPS,
  NAL_AU_DELIMITER,
  NAL_END_OF_SEQUENCE,
  NAL_END_OF_STREAM,
  NAL_FILLER_DATA,
  NAL_SPS_EXT
};

enum pic_struct {
  DISP_FRAME = 0,
  DISP_TOP,
  DISP_BOTTOM,
  DISP_TOP_BOTTOM,
  DISP_BOTTOM_TOP,
  DISP_TOP_BOTTOM_TOP,
  DISP_BOTTOM_TOP_BOTTOM,
  DISP_FRAME_DOUBLING,
  DISP_FRAME_TRIPLING
};

enum ct_type {
    CT_PROGRESSIVE = 0,
    CT_INTERLACED,
    CT_UNKNOWN,
    CT_RESERVED
};

/* slice types repeat from 5-9, we
 * need a helper function for comparison
 */
enum slice_types
{
  SLICE_P = 0, SLICE_B, SLICE_I, SLICE_SP, SLICE_SI
};

enum aspect_ratio
{
  ASPECT_UNSPECIFIED = 0,
  ASPECT_1_1,
  ASPECT_12_11,
  ASPECT_10_11,
  ASPECT_16_11,
  ASPECT_40_33,
  ASPECT_24_11,
  ASPECT_20_11,
  ASPECT_32_11,
  ASPECT_80_33,
  ASPECT_18_11,
  ASPECT_15_11,
  ASPECT_64_33,
  ASPECT_160_99,
  ASPECT_4_3,
  ASPECT_3_2,
  ASPECT_2_1,
  ASPECT_RESERVED,
  ASPECT_EXTENDED_SAR=255
};

static inline uint32_t slice_type(uint32_t slice_type)
{
  return (slice_type < 10 ? slice_type % 5 : slice_type);
}

#if 0
static inline void print_slice_type(uint32_t slice_type)
{
  switch(slice_type) {
    case SLICE_P:
      printf("SLICE_P\n");
      break;
    case SLICE_B:
      printf("SLICE_B\n");
      break;
    case SLICE_I:
      printf("SLICE_I\n");
      break;
    case SLICE_SP:
      printf("SLICE_SP\n");
      break;
    case SLICE_SI:
      printf("SLICE_SI\n");
      break;
    default:
      printf("Unknown SLICE\n");
  }
}
#endif

struct hrd_parameters
{
  uint32_t cpb_cnt_minus1;
  uint8_t bit_rate_scale;
  uint8_t cpb_size_scale;

  uint32_t bit_rate_value_minus1[32];
  uint32_t cpb_size_value_minus1[32];
  uint8_t cbr_flag[32];

  uint8_t initial_cpb_removal_delay_length_minus1;
  uint8_t cpb_removal_delay_length_minus1;
  uint8_t dpb_output_delay_length_minus1;
  uint8_t time_offset_length;
};

struct seq_parameter_set_rbsp
{
  uint8_t profile_idc; // 0xff
  uint8_t constraint_setN_flag; // 0x0f
  uint8_t level_idc; // 0xff
  uint32_t seq_parameter_set_id;
  uint32_t chroma_format_idc;
  uint8_t separate_colour_plane_flag; // 0x01
  uint32_t bit_depth_luma_minus8;
  uint32_t bit_depth_chroma_minus8;
  uint8_t qpprime_y_zero_transform_bypass_flag;
  uint8_t seq_scaling_matrix_present_flag;

  /* if(seq_scaling_matrix_present_flag) */
  uint8_t seq_scaling_list_present_flag[8];

  uint8_t scaling_lists_4x4[6][16];
  uint8_t scaling_lists_8x8[2][64];
  /* endif */

  uint32_t log2_max_frame_num_minus4;
  uint32_t max_frame_num;
  uint32_t pic_order_cnt_type;
  // if pic_order_cnt_type==0
  uint32_t log2_max_pic_order_cnt_lsb_minus4;
  // else
  uint8_t delta_pic_order_always_zero_flag;
  int32_t offset_for_non_ref_pic;
  int32_t offset_for_top_to_bottom_field;
  uint8_t num_ref_frames_in_pic_order_cnt_cycle;
  int32_t offset_for_ref_frame[256];
  // TODO: some more ignored here
  uint32_t num_ref_frames;
  uint8_t gaps_in_frame_num_value_allowed_flag;
  /*uint32_t    pic_width_in_mbs_minus1;
   uint32_t    pic_height_in_map_units_minus1;*/
  uint32_t pic_width;
  uint32_t pic_height;
  uint8_t frame_mbs_only_flag;
  uint8_t mb_adaptive_frame_field_flag;
  uint8_t direct_8x8_inference_flag;
  uint8_t frame_cropping_flag;
  uint32_t frame_crop_left_offset;
  uint32_t frame_crop_right_offset;
  uint32_t frame_crop_top_offset;
  uint32_t frame_crop_bottom_offset;
  uint8_t vui_parameters_present_flag;

  /* vui_parameters */
  struct
  {
    uint8_t aspect_ration_info_present_flag;

    /* aspect_ration_info_present_flag == 1 */
    uint8_t aspect_ratio_idc;
    uint16_t sar_width;
    uint16_t sar_height;

    uint8_t overscan_info_present_flag;
    /* overscan_info_present_flag == 1 */
    uint8_t overscan_appropriate_flag;

    uint8_t video_signal_type_present_flag;
    /* video_signal_type_present_flag == 1 */
    uint8_t video_format;
    uint8_t video_full_range_flag;
    uint8_t colour_description_present;
    /* colour_description_present == 1 */
    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;

    uint8_t chroma_loc_info_present_flag;
    /* chroma_loc_info_present_flag == 1 */
    uint8_t chroma_sample_loc_type_top_field;
    uint8_t chroma_sample_loc_type_bottom_field;

    uint8_t timing_info_present_flag;
    /* timing_info_present_flag == 1 */
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint8_t fixed_frame_rate_flag;

    uint8_t nal_hrd_parameters_present_flag;
    struct hrd_parameters nal_hrd_parameters;

    uint8_t vc1_hrd_parameters_present_flag;
    struct hrd_parameters vc1_hrd_parameters;

    uint8_t low_delay_hrd_flag;

    uint8_t pic_struct_present_flag;
    uint8_t bitstream_restriction_flag;

    /* bitstream_restriction_flag == 1 */
    uint8_t motion_vectors_over_pic_boundaries;
    uint32_t max_bytes_per_pic_denom;
    uint32_t max_bits_per_mb_denom;
    uint32_t log2_max_mv_length_horizontal;
    uint32_t log2_max_mv_length_vertical;
    uint32_t num_reorder_frames;
    uint32_t max_dec_frame_buffering;
  } vui_parameters;

};

struct pic_parameter_set_rbsp
{
  uint32_t pic_parameter_set_id;
  uint32_t seq_parameter_set_id;
  uint8_t entropy_coding_mode_flag;
  uint8_t pic_order_present_flag;

  uint32_t num_slice_groups_minus1;

  /* num_slice_groups_minus1 > 0 */
  uint32_t slice_group_map_type;

  /* slice_group_map_type == 1 */
  uint32_t run_length_minus1[64];

  /* slice_group_map_type == 2 */
  uint32_t top_left[64];
  uint32_t bottom_right[64];

  /* slice_group_map_type == 3,4,5 */
  uint8_t slice_group_change_direction_flag;
  uint32_t slice_group_change_rate_minus1;

  /* slice_group_map_type == 6 */
  uint32_t pic_size_in_map_units_minus1;
  uint8_t slice_group_id[64];

  uint32_t num_ref_idx_l0_active_minus1;
  uint32_t num_ref_idx_l1_active_minus1;
  uint8_t weighted_pred_flag;
  uint8_t weighted_bipred_idc;
  int32_t pic_init_qp_minus26;
  int32_t pic_init_qs_minus26;
  int32_t chroma_qp_index_offset;
  uint8_t deblocking_filter_control_present_flag;
  uint8_t constrained_intra_pred_flag;
  uint8_t redundant_pic_cnt_present_flag;

  /* if(more_rbsp_data) */
  uint8_t transform_8x8_mode_flag;
  uint8_t pic_scaling_matrix_present_flag;

  /* if(pic_scaling_matrix_present_flag) */
  uint8_t pic_scaling_list_present_flag[8];

  uint8_t scaling_lists_4x4[6][16];
  uint8_t scaling_lists_8x8[2][64];

  int32_t second_chroma_qp_index_offset;
};

/*struct clock_timestamp {
  uint8_t ct_type;
  uint8_t nuit_fiel_based_flag;
  uint8_t counting_type;
  uint8_t full_timestamp_flag;
  uint8_t discontinuity_flag;
  uint8_t cnt_dropped_flag;
  uint8_t n_frames
};*/

/* sei contains several additional info, we do
 * only care for pic_timing, to handle display
 * reordering
 */
struct sei_message
{
  uint32_t payload_type;
  uint8_t last_payload_type_byte;
  uint32_t payload_size;
  uint8_t last_payload_size_byte;

  struct
  {
    /* cpb_dpb_delays_present_flag == 1 */
    uint8_t cpb_removal_delay;
    uint8_t dpb_output_delay;

    uint8_t pic_struct;
    uint8_t ct_type : 1;
    uint8_t nuit_field_based_flag : 1;
    uint8_t counting_type : 5;
    uint8_t full_timestamp_flag : 1;
    uint8_t discontinuity_flag : 1;
    uint8_t cnt_dropped_flag : 1;
    uint8_t n_frames;

    uint8_t seconds_value : 6;
    uint8_t minutes_value : 6;
    uint8_t hours_value : 5;

    int32_t time_offset;
  } pic_timing;
};

struct slice_header
{
  uint32_t first_mb_in_slice;
  uint32_t slice_type;
  uint32_t pic_parameter_set_id;
  uint8_t colour_plane_id;
  uint32_t frame_num;
  uint8_t field_pic_flag;
  uint8_t bottom_field_flag;
  uint32_t idr_pic_id;

  /* sps->pic_order_cnt_type == 0 */
  uint32_t pic_order_cnt_lsb;
  int32_t delta_pic_order_cnt_bottom;
  /* sps->pic_order_cnt_type == 1 && !sps->delta_pic_order_always_zero_flag */
  int32_t delta_pic_order_cnt[2];

  /* pps->redundant_pic_cnt_present_flag == 1 */
  int32_t redundant_pic_cnt;

  /* slice_type == B */
  uint8_t direct_spatial_mv_pred_flag;

  /* slice_type == P, SP, B */
  uint8_t num_ref_idx_active_override_flag;
  /* num_ref_idx_active_override_flag == 1 */
  uint32_t num_ref_idx_l0_active_minus1;
  /* slice type == B */
  uint32_t num_ref_idx_l1_active_minus1;

  /* ref_pic_list_reordering */
  struct
  {
    /* slice_type != I && slice_type != SI */
    uint8_t ref_pic_list_reordering_flag_l0;

    /* slice_type == B */
    uint8_t ref_pic_list_reordering_flag_l1;

    /* ref_pic_list_reordering_flag_l0 == 1 */
    uint32_t reordering_of_pic_nums_idc;

    /* reordering_of_pic_nums_idc == 0, 1 */
    uint32_t abs_diff_pic_num_minus1;

    /* reordering_of_pic_nums_idc == 2) */
    uint32_t long_term_pic_num;
  } ref_pic_list_reordering;

  /* pred_weight_table */
  struct
  {
    uint32_t luma_log2_weight_denom;

    /* chroma_format_idc != 0 */
    uint32_t chroma_log2_weight_denom;

    int32_t luma_weight_l0[32];
    int32_t luma_offset_l0[32];

    int32_t chroma_weight_l0[32][2];
    int32_t chroma_offset_l0[32][2];

    int32_t luma_weight_l1[32];
    int32_t luma_offset_l1[32];

    int32_t chroma_weight_l1[32][2];
    int32_t chroma_offset_l1[32][2];
  } pred_weight_table;

  /* def_rec_pic_marking */
  struct
  {

    /* nal_unit_type == NAL_SLICE_IDR */
    uint8_t no_output_of_prior_pics_flag;
    uint8_t long_term_reference_flag;

    /* else */
    uint8_t adaptive_ref_pic_marking_mode_flag;
    uint32_t memory_management_control_operation;

    uint32_t difference_of_pic_nums_minus1;
    uint32_t long_term_pic_num;
    uint32_t long_term_frame_idx;
    uint32_t max_long_term_frame_idx_plus1;
  } dec_ref_pic_marking[10];
  uint32_t dec_ref_pic_marking_count;
};

struct nal_unit {
    uint8_t nal_ref_idc; // 0x03
    enum nal_unit_types nal_unit_type; // 0x1f

    //union {
      struct sei_message sei;
      struct seq_parameter_set_rbsp sps;
      struct pic_parameter_set_rbsp pps;
      struct slice_header slc;
    //};

    struct nal_unit *prev;
    struct nal_unit *next;

    uint32_t lock_counter;
};

struct nal_buffer {
    struct nal_unit *first;
    struct nal_unit *last;

    uint8_t max_size;
    uint8_t used;
};

static struct nal_buffer* create_nal_buffer(uint8_t max_size)
{
    struct nal_buffer *nal_buffer = calloc(1, sizeof(struct nal_buffer));
    if (!nal_buffer)
      return NULL;
    nal_buffer->max_size = max_size;

    return nal_buffer;
}

static void release_nal_unit(struct nal_unit *nal)
{
  if(!nal)
    return;

  nal->lock_counter--;

  if(nal->lock_counter <= 0) {
    free(nal);
  }
}

/**
 * destroys a nal buffer. all referenced nals are released
 */
static void free_nal_buffer(struct nal_buffer *nal_buffer)
{
  struct nal_unit *nal = nal_buffer->first;

  while (nal) {
    struct nal_unit *delete = nal;
    nal = nal->next;
    release_nal_unit(delete);
  }

  free(nal_buffer);
}

static void nal_buffer_remove(struct nal_buffer *nal_buffer, struct nal_unit *nal)
{
  if (nal == nal_buffer->first && nal == nal_buffer->last) {
    nal_buffer->first = nal_buffer->last = NULL;
  } else {
    if (nal == nal_buffer->first) {
      nal_buffer->first = nal->next;
      nal_buffer->first->prev = NULL;
    } else {
      nal->prev->next = nal->next;
    }

    if (nal == nal_buffer->last) {
      nal_buffer->last = nal->prev;
      nal_buffer->last->next = NULL;
    } else {
      nal->next->prev = nal->prev;
    }
  }

  nal->next = nal->prev = NULL;
  release_nal_unit(nal);

  nal_buffer->used--;
}


static void lock_nal_unit(struct nal_unit *nal)
{
  nal->lock_counter++;
}

/**
 * appends a nal unit to the end of the buffer
 */
static void nal_buffer_append(struct nal_buffer *nal_buffer, struct nal_unit *nal)
{
  if(nal_buffer->used == nal_buffer->max_size) {
    nal_buffer_remove(nal_buffer, nal_buffer->first);
  }

  if (nal_buffer->first == NULL) {
    nal_buffer->first = nal_buffer->last = nal;
    nal->prev = nal->next = NULL;

    lock_nal_unit(nal);
    nal_buffer->used++;
  } else if (nal_buffer->last != NULL) {
    nal_buffer->last->next = nal;
    nal->prev = nal_buffer->last;
    nal_buffer->last = nal;

    lock_nal_unit(nal);
    nal_buffer->used++;
  } else {
    lprintf("ERR: nal_buffer is in a broken state\n");
  }
}

#if 0
static void nal_buffer_flush(struct nal_buffer *nal_buffer)
{
  while(nal_buffer->used > 0) {
    nal_buffer_remove(nal_buffer, nal_buffer->first);
  }
}
#endif

/**
 * returns the last element in the buffer
 */
static struct nal_unit *nal_buffer_get_last(struct nal_buffer *nal_buffer)
{
  return nal_buffer->last;
}

/**
 * get a nal unit from a nal_buffer from it's
 * seq parameter_set_id
 */
static struct nal_unit* nal_buffer_get_by_sps_id(struct nal_buffer *nal_buffer,
    uint32_t seq_parameter_set_id)
{
  struct nal_unit *nal = nal_buffer->last;

  if (nal != NULL) {
    do {
      if(nal->nal_unit_type == NAL_SPS) {
        if(nal->sps.seq_parameter_set_id == seq_parameter_set_id) {
          return nal;
        }
      }

      nal = nal->prev;
    } while(nal != NULL);
  }

  return NULL;
}

/**
 * get a nal unit from a nal_buffer from it's
 * pic parameter_set_id
 */
static struct nal_unit* nal_buffer_get_by_pps_id(struct nal_buffer *nal_buffer,
    uint32_t pic_parameter_set_id)
{
  struct nal_unit *nal = nal_buffer->last;

  if (nal != NULL) {
    do {
      if(nal->nal_unit_type == NAL_PPS) {
        if(nal->pps.pic_parameter_set_id == pic_parameter_set_id) {
          return nal;
        }
      }

      nal = nal->prev;
    } while(nal != NULL);
  }

  return NULL;
}

/**
 * create a new nal unit, with a lock_counter of 1
 */
static struct nal_unit* create_nal_unit()
{
  struct nal_unit *nal = calloc(1, sizeof(struct nal_unit));
  if (!nal)
    return NULL;
  nal->lock_counter = 1;

  return nal;
}

#if 0
/**
 * creates a copy of a nal unit with a single lock
 */
static void copy_nal_unit(struct nal_unit *dest, struct nal_unit *src)
{
  /* size without pps, sps and slc units: */
  int size = sizeof(struct nal_unit);

  xine_fast_memcpy(dest, src, size);
  dest->lock_counter = 1;
  dest->prev = dest->next = NULL;
}
#endif

/*************************************************************************
* cpb.h: Coded Picture Buffer                                            *
*************************************************************************/

enum picture_flags {
  IDR_PIC = 0x01,
  REFERENCE = 0x02,
  NOT_EXISTING = 0x04,
  INTERLACED = 0x08
};

struct coded_picture
{
  uint32_t flag_mask;

  uint32_t max_pic_num;
  int32_t pic_num;

  uint8_t used_for_long_term_ref;
  uint32_t long_term_pic_num;
  uint32_t long_term_frame_idx;

  int32_t top_field_order_cnt;
  int32_t bottom_field_order_cnt;

  uint8_t repeat_pic;

  /* buffer data for the image slices, which
   * are passed to the decoder
   */
  uint32_t slice_cnt;

  int64_t pts;

  struct nal_unit *sei_nal;
  struct nal_unit *sps_nal;
  struct nal_unit *pps_nal;
  struct nal_unit *slc_nal;
};

static inline struct coded_picture* create_coded_picture(void)
{
  struct coded_picture* pic = calloc(1, sizeof(struct coded_picture));
  return pic;
}

static inline void free_coded_picture(struct coded_picture *pic)
{
  if(!pic)
    return;

  release_nal_unit(pic->sei_nal);
  release_nal_unit(pic->sps_nal);
  release_nal_unit(pic->pps_nal);
  release_nal_unit(pic->slc_nal);

  free(pic);
}

/*************************************************************************
* dpb.c: Implementing Decoded Picture Buffer                             *
*************************************************************************/

#define MAX_DPB_COUNT 16

#define USED_FOR_REF (top_is_reference || bottom_is_reference)

/**
 * ----------------------------------------------------------------------------
 * decoded picture
 * ----------------------------------------------------------------------------
 */

struct decoded_picture {
  vo_frame_t *img; /* this is the image we block, to make sure
                    * the surface is not double-used */

  /**
   * a decoded picture always contains a whole frame,
   * respective a field pair, so it can contain up to
   * 2 coded pics
   */
  struct coded_picture *coded_pic[2];

  int32_t frame_num_wrap;

  uint8_t top_is_reference;
  uint8_t bottom_is_reference;

  uint32_t lock_counter;
};

/* from parser */
enum parser_flags {
    CPB_DPB_DELAYS_PRESENT = 0x01,
    PIC_STRUCT_PRESENT = 0x02
};

/**
 * ----------------------------------------------------------------------------
 * dpb code starting here
 * ----------------------------------------------------------------------------
 */

/* Decoded Picture Buffer */
struct dpb {
  xine_list_t *reference_list;
  xine_list_t *output_list;

  int max_reorder_frames;
  int max_dpb_frames;
};

static int dp_top_field_first(struct decoded_picture *decoded_pic)
{
  int top_field_first = 1;


  if (decoded_pic->coded_pic[1] != NULL) {
    if (!decoded_pic->coded_pic[0]->slc_nal->slc.bottom_field_flag &&
        decoded_pic->coded_pic[1]->slc_nal->slc.bottom_field_flag &&
        decoded_pic->coded_pic[0]->top_field_order_cnt !=
            decoded_pic->coded_pic[1]->bottom_field_order_cnt) {
      top_field_first = decoded_pic->coded_pic[0]->top_field_order_cnt < decoded_pic->coded_pic[1]->bottom_field_order_cnt;
    } else if (decoded_pic->coded_pic[0]->slc_nal->slc.bottom_field_flag &&
        !decoded_pic->coded_pic[1]->slc_nal->slc.bottom_field_flag &&
        decoded_pic->coded_pic[0]->bottom_field_order_cnt !=
            decoded_pic->coded_pic[1]->top_field_order_cnt) {
      top_field_first = decoded_pic->coded_pic[0]->bottom_field_order_cnt > decoded_pic->coded_pic[1]->top_field_order_cnt;
    }
  }

  if (decoded_pic->coded_pic[0]->flag_mask & PIC_STRUCT_PRESENT && decoded_pic->coded_pic[0]->sei_nal != NULL) {
    uint8_t pic_struct = decoded_pic->coded_pic[0]->sei_nal->sei.pic_timing.pic_struct;
    if(pic_struct == DISP_TOP_BOTTOM ||
        pic_struct == DISP_TOP_BOTTOM_TOP) {
      top_field_first = 1;
    } else if (pic_struct == DISP_BOTTOM_TOP ||
        pic_struct == DISP_BOTTOM_TOP_BOTTOM) {
      top_field_first = 0;
    } else if (pic_struct == DISP_FRAME) {
      top_field_first = 1;
    }
  }

  return top_field_first;
}

/**
 * ----------------------------------------------------------------------------
 * decoded picture
 * ----------------------------------------------------------------------------
 */

static void free_decoded_picture(struct decoded_picture *pic)
{
  if(!pic)
    return;

  if(pic->img != NULL) {
    pic->img->free(pic->img);
  }

  free_coded_picture(pic->coded_pic[1]);
  free_coded_picture(pic->coded_pic[0]);
  pic->coded_pic[0] = NULL;
  pic->coded_pic[1] = NULL;
  free(pic);
}

static void decoded_pic_check_reference(struct decoded_picture *pic)
{
  int i;
  for(i = 0; i < 2; i++) {
    struct coded_picture *cpic = pic->coded_pic[i];
    if(cpic && (cpic->flag_mask & REFERENCE)) {
      // FIXME: this assumes Top Field First!
      if(i == 0) {
        pic->top_is_reference = cpic->slc_nal->slc.field_pic_flag
                    ? (cpic->slc_nal->slc.bottom_field_flag ? 0 : 1) : 1;
      }

      pic->bottom_is_reference = cpic->slc_nal->slc.field_pic_flag
                    ? (cpic->slc_nal->slc.bottom_field_flag ? 1 : 0) : 1;
    }
  }
}

static struct decoded_picture* init_decoded_picture(struct coded_picture *cpic, vo_frame_t *img)
{
  struct decoded_picture *pic = calloc(1, sizeof(struct decoded_picture));
  if (!pic)
    return NULL;

  pic->coded_pic[0] = cpic;

  decoded_pic_check_reference(pic);
  pic->img = img;
  pic->lock_counter = 1;

  return pic;
}

static void decoded_pic_add_field(struct decoded_picture *pic,
    struct coded_picture *cpic)
{
  pic->coded_pic[1] = cpic;

  decoded_pic_check_reference(pic);
}

static void release_decoded_picture(struct decoded_picture *pic)
{
  if(!pic)
    return;

  pic->lock_counter--;
  //printf("release decoded picture: %p (%d)\n", pic, pic->lock_counter);

  if(pic->lock_counter <= 0) {
    free_decoded_picture(pic);
  }
}

static void lock_decoded_picture(struct decoded_picture *pic)
{
  if(!pic)
    return;

  pic->lock_counter++;
  //printf("lock decoded picture: %p (%d)\n", pic, pic->lock_counter);
}




/**
 * ----------------------------------------------------------------------------
 * dpb code starting here
 * ----------------------------------------------------------------------------
 */

static struct dpb* create_dpb(void)
{
    struct dpb *dpb = calloc(1, sizeof(struct dpb));
    if (!dpb)
      return NULL;

    dpb->output_list = xine_list_new();
    dpb->reference_list = xine_list_new();

    dpb->max_reorder_frames = MAX_DPB_COUNT;
    dpb->max_dpb_frames = MAX_DPB_COUNT;

    return dpb;
}

/**
 * calculates the total number of frames in the dpb
 * when frames are used for reference and are not drawn
 * yet the result would be less then reference_list-size+
 * output_list-size
 */
static int dpb_total_frames(struct dpb *dpb)
{
  int num_frames = xine_list_size(dpb->output_list);

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while(ite) {
    struct decoded_picture *pic = xine_list_get_value(dpb->reference_list, ite);
    if (xine_list_find(dpb->output_list, pic) == NULL) {
      num_frames++;
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return num_frames;
}

static struct decoded_picture* dpb_get_next_out_picture(struct dpb *dpb, int do_flush)
{
  struct decoded_picture *pic = NULL;;
  struct decoded_picture *outpic = NULL;

  if(!do_flush &&
      (int)xine_list_size(dpb->output_list) < dpb->max_reorder_frames &&
      dpb_total_frames(dpb) < dpb->max_dpb_frames) {
    return NULL;
  }

  xine_list_iterator_t ite = NULL;
  while ((pic = xine_list_prev_value (dpb->output_list, &ite))) {

    int32_t out_top_field_order_cnt = outpic != NULL ?
        outpic->coded_pic[0]->top_field_order_cnt : 0;
    int32_t top_field_order_cnt = pic->coded_pic[0]->top_field_order_cnt;

    int32_t out_bottom_field_order_cnt = outpic != NULL ?
        (outpic->coded_pic[1] != NULL ?
          outpic->coded_pic[1]->bottom_field_order_cnt :
          outpic->coded_pic[0]->top_field_order_cnt) : 0;
    int32_t bottom_field_order_cnt = pic->coded_pic[1] != NULL ?
              pic->coded_pic[1]->bottom_field_order_cnt :
              pic->coded_pic[0]->top_field_order_cnt;

    if (outpic == NULL ||
            (top_field_order_cnt <= out_top_field_order_cnt &&
                bottom_field_order_cnt <= out_bottom_field_order_cnt) ||
            (out_top_field_order_cnt <= 0 && top_field_order_cnt > 0 &&
               out_bottom_field_order_cnt <= 0 && bottom_field_order_cnt > 0) ||
            outpic->coded_pic[0]->flag_mask & IDR_PIC) {
      outpic = pic;
    }
  }

  return outpic;
}

static struct decoded_picture* dpb_get_picture(struct dpb *dpb, uint32_t picnum)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    if ((pic->coded_pic[0]->pic_num == (int32_t)picnum ||
        (pic->coded_pic[1] != NULL &&
            pic->coded_pic[1]->pic_num == (int32_t)picnum))) {
      return pic;
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return NULL;
}

static struct decoded_picture* dpb_get_picture_by_ltpn(struct dpb *dpb,
    uint32_t longterm_picnum)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    if (pic->coded_pic[0]->long_term_pic_num == longterm_picnum ||
        (pic->coded_pic[1] != NULL &&
            pic->coded_pic[1]->long_term_pic_num == longterm_picnum)) {
      return pic;
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return NULL;
}

static struct decoded_picture* dpb_get_picture_by_ltidx(struct dpb *dpb,
    uint32_t longterm_idx)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    if (pic->coded_pic[0]->long_term_frame_idx == longterm_idx ||
        (pic->coded_pic[1] != NULL &&
            pic->coded_pic[1]->long_term_frame_idx == longterm_idx)) {
      return pic;
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return NULL;
}

static int dpb_unmark_reference_picture(struct dpb *dpb, struct decoded_picture *pic)
{
  if(!pic)
    return -1;

  xine_list_iterator_t ite = xine_list_find(dpb->reference_list, pic);
  if (ite) {
    xine_list_remove(dpb->reference_list, ite);
    release_decoded_picture(pic);

    return 0;
  }

  return -1;
}

static int dpb_set_unused_ref_picture_byltpn(struct dpb *dpb, uint32_t longterm_picnum)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    uint8_t found = 0;

    if (pic->coded_pic[0]->long_term_pic_num == longterm_picnum) {
      pic->coded_pic[0]->used_for_long_term_ref = 0;
      found = 1;
    }

    if ((pic->coded_pic[1] != NULL &&
          pic->coded_pic[1]->long_term_pic_num == longterm_picnum)) {
      pic->coded_pic[1]->used_for_long_term_ref = 0;
      found = 1;
    }

    if(found && !pic->coded_pic[0]->used_for_long_term_ref &&
        (pic->coded_pic[1] == NULL ||
            !pic->coded_pic[1]->used_for_long_term_ref)) {
      dpb_unmark_reference_picture(dpb, pic);
    }

    if (found)
      return 0;

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return -1;
}

static int dpb_set_unused_ref_picture_bylidx(struct dpb *dpb, uint32_t longterm_idx)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    uint8_t found = 0;

    if (pic->coded_pic[0]->long_term_frame_idx == longterm_idx) {
      pic->coded_pic[0]->used_for_long_term_ref = 0;
      found = 1;
    }

    if ((pic->coded_pic[1] != NULL &&
          pic->coded_pic[1]->long_term_frame_idx == longterm_idx)) {
      pic->coded_pic[1]->used_for_long_term_ref = 0;
      found = 1;
    }

    if(found && !pic->coded_pic[0]->used_for_long_term_ref &&
        (pic->coded_pic[1] == NULL ||
            !pic->coded_pic[1]->used_for_long_term_ref)) {
      dpb_unmark_reference_picture(dpb, pic);
    }

    if (found)
      return 0;

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return -1;
}

static int dpb_set_unused_ref_picture_lidx_gt(struct dpb *dpb, int32_t longterm_idx)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    uint8_t found = 0;

    if ((int32_t)(pic->coded_pic[0]->long_term_frame_idx) >= longterm_idx) {
      pic->coded_pic[0]->used_for_long_term_ref = 0;
      found = 1;
    }

    if ((pic->coded_pic[1] != NULL &&
          (int32_t)(pic->coded_pic[1]->long_term_frame_idx) >= longterm_idx)) {
      pic->coded_pic[1]->used_for_long_term_ref = 0;
      found = 1;
    }

    if(found && !pic->coded_pic[0]->used_for_long_term_ref &&
        (pic->coded_pic[1] == NULL ||
            !pic->coded_pic[1]->used_for_long_term_ref)) {
      dpb_unmark_reference_picture(dpb, pic);
    }

    ite = xine_list_next(dpb->reference_list, ite);
  }

  return -1;
}


static int dpb_unmark_picture_delayed(struct dpb *dpb, struct decoded_picture *pic)
{
  if(!pic)
    return -1;

  xine_list_iterator_t ite = xine_list_find(dpb->output_list, pic);
  if (ite) {
    xine_list_remove(dpb->output_list, ite);
    release_decoded_picture(pic);

    return 0;
  }

  return -1;
}

/*static int dpb_remove_picture_by_img(struct dpb *dpb, vo_frame_t *remimg)
{
  int retval = -1;
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->output_list);
  while (ite) {
    pic = xine_list_get_value(dpb->output_list, ite);

    if (pic->img == remimg) {
      dpb_unmark_picture_delayed(dpb, pic);
      dpb->used--;
      retval = 0;
    }

    ite = xine_list_next(dpb->output_list, ite);
  }

  return retval;
}*/


static int dpb_add_picture(struct dpb *dpb, struct decoded_picture *pic, uint32_t num_ref_frames)
{
#if 0
  /* this should never happen */
  pic->img->lock(pic->img);
  if (0 == dpb_remove_picture_by_img(dpb, pic->img))
    lprintf("H264/DPB broken stream: current img was already in dpb -- freed it\n");
  else
    pic->img->free(pic->img);
#endif

  /* add the pic to the output picture list, as no
   * pic would be immediately drawn.
   * acquire a lock for this list
   */
  lock_decoded_picture(pic);
  xine_list_push_back(dpb->output_list, pic);


  /* check if the pic is a reference pic,
   * if it is it should be added to the reference
   * list. another lock has to be acquired in that case
   */
  if (pic->coded_pic[0]->flag_mask & REFERENCE ||
      (pic->coded_pic[1] != NULL &&
          pic->coded_pic[1]->flag_mask & REFERENCE)) {
    lock_decoded_picture(pic);
    xine_list_push_back(dpb->reference_list, pic);

    /*
     * always apply the sliding window reference removal, if more reference
     * frames than expected are in the list. we will always remove the oldest
     * reference frame
     */
    if(xine_list_size(dpb->reference_list) > num_ref_frames) {
      struct decoded_picture *discard = xine_list_get_value(dpb->reference_list, xine_list_front(dpb->reference_list));
      dpb_unmark_reference_picture(dpb, discard);
    }
  }

#if DEBUG_DPB
  printf("DPB list sizes: Total: %2d, Output: %2d, Reference: %2d\n",
      dpb_total_frames(dpb), xine_list_size(dpb->output_list),
      xine_list_size(dpb->reference_list));
#endif

  return 0;
}

static int dpb_flush(struct dpb *dpb)
{
  struct decoded_picture *pic = NULL;

  xine_list_iterator_t ite = xine_list_front(dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(dpb->reference_list, ite);

    dpb_unmark_reference_picture(dpb, pic);

    /* CAUTION: xine_list_next would return an item, but not the one we
     * expect, as the current one was deleted
     */
    ite = xine_list_front(dpb->reference_list);
  }

  return 0;
}

static void dpb_free_all(struct dpb *dpb)
{
  xine_list_iterator_t ite = xine_list_front(dpb->output_list);
  while(ite) {
    dpb_unmark_picture_delayed(dpb, xine_list_get_value(dpb->output_list, ite));
    /* CAUTION: xine_list_next would return an item, but not the one we
     * expect, as the current one was deleted
     */
    ite = xine_list_front(dpb->output_list);
  }

  ite = xine_list_front(dpb->reference_list);
  while(ite) {
    dpb_unmark_reference_picture(dpb, xine_list_get_value(dpb->reference_list, ite));
    /* CAUTION: xine_list_next would return an item, but not the one we
     * expect, as the current one was deleted
     */
    ite = xine_list_front(dpb->reference_list);
  }
}

static void dpb_clear_all_pts(struct dpb *dpb)
{
  xine_list_iterator_t ite = xine_list_front(dpb->output_list);
  while(ite) {
    struct decoded_picture *pic = xine_list_get_value(dpb->output_list, ite);
    pic->img->pts = 0;

    ite = xine_list_next(dpb->output_list, ite);
  }
}

static int fill_vdpau_reference_list(struct dpb *dpb, VdpReferenceFrameH264 *reflist)
{
  struct decoded_picture *pic = NULL;

  int i = 0;
  int used_refframes = 0;

  xine_list_iterator_t ite = NULL;
  while ((pic = xine_list_prev_value (dpb->reference_list, &ite))) {
    reflist[i].surface = ((vdpau_accel_t*)pic->img->accel_data)->surface;
    reflist[i].is_long_term = pic->coded_pic[0]->used_for_long_term_ref ||
        (pic->coded_pic[1] != NULL && pic->coded_pic[1]->used_for_long_term_ref);

    reflist[i].frame_idx = pic->coded_pic[0]->used_for_long_term_ref ?
        pic->coded_pic[0]->long_term_pic_num :
        pic->coded_pic[0]->slc_nal->slc.frame_num;
    reflist[i].top_is_reference = pic->top_is_reference;
    reflist[i].bottom_is_reference = pic->bottom_is_reference;
    reflist[i].field_order_cnt[0] = pic->coded_pic[0]->top_field_order_cnt;
    reflist[i].field_order_cnt[1] = pic->coded_pic[1] != NULL ?
        pic->coded_pic[1]->bottom_field_order_cnt :
        pic->coded_pic[0]->bottom_field_order_cnt;
    i++;
  }

  used_refframes = i;

  // fill all other frames with invalid handles
  while(i < 16) {
    reflist[i].bottom_is_reference = VDP_FALSE;
    reflist[i].top_is_reference = VDP_FALSE;
    reflist[i].frame_idx = 0;
    reflist[i].is_long_term = VDP_FALSE;
    reflist[i].surface = VDP_INVALID_HANDLE;
    reflist[i].field_order_cnt[0] = 0;
    reflist[i].field_order_cnt[1] = 0;
    i++;
  }

  return used_refframes;
}

/*************************************************************************
* h264_parser.c: Almost full-features H264 NAL-Parser                    *
*************************************************************************/

#define MAX_FRAME_SIZE  1024*1024

/* specifies wether the parser last parsed
 * non-vcl or vcl nal units. depending on
 * this the access unit boundaries are detected
 */
enum parser_position {
    NON_VCL,
    VCL
};

struct h264_parser {
    uint8_t buf[MAX_FRAME_SIZE];
    uint32_t buf_len;

    /* prebuf is used to store the currently
     * processed nal unit */
    uint8_t prebuf[MAX_FRAME_SIZE];
    uint32_t prebuf_len;
    uint32_t next_nal_position;

    uint8_t last_nal_res;

    uint8_t nal_size_length;
    uint32_t next_nal_size;
    uint8_t *nal_size_length_buf;
    uint8_t have_nal_size_length_buf;

    enum parser_position position;

    struct coded_picture *pic;

    struct nal_unit *last_vcl_nal;
    struct nal_buffer *sps_buffer;
    struct nal_buffer *pps_buffer;

    uint32_t prev_pic_order_cnt_lsb;
    uint32_t prev_pic_order_cnt_msb;
    uint32_t frame_num_offset;

    int32_t prev_top_field_order_cnt;

    uint32_t curr_pic_num;

    uint16_t flag_mask;

    /* this is dpb used for reference frame
     * heading to vdpau + unordered frames
     */
    struct dpb *dpb;

    xine_t *xine;
};

static int parse_nal(const uint8_t *buf, int buf_len, struct h264_parser *parser,
    struct coded_picture **completed_picture);

static int seek_for_nal(uint8_t *buf, int buf_len, struct h264_parser *parser);

#if 0
static void reset_parser(struct h264_parser *parser);
#endif
static void free_parser(struct h264_parser *parser);
static int parse_frame(struct h264_parser *parser, const uint8_t *inbuf, int inbuf_len,
    int64_t pts,
    const void **ret_buf, uint32_t *ret_len, struct coded_picture **ret_pic);

/* this has to be called after decoding the frame delivered by parse_frame,
 * but before adding a decoded frame to the dpb.
 */
static void process_mmc_operations(struct h264_parser *parser, struct coded_picture *picture);

static void parse_codec_private(struct h264_parser *parser, const uint8_t *inbuf, int inbuf_len);

/* XXX duplicated in two decoders. Move to shared .c file ? */
static const uint8_t zigzag_4x4[16] = {
  0+0*4, 1+0*4, 0+1*4, 0+2*4,
  1+1*4, 2+0*4, 3+0*4, 2+1*4,
  1+2*4, 0+3*4, 1+3*4, 2+2*4,
  3+1*4, 3+2*4, 2+3*4, 3+3*4,
};

static const uint8_t zigzag_8x8[64] = {
  0+0*8, 1+0*8, 0+1*8, 0+2*8,
  1+1*8, 2+0*8, 3+0*8, 2+1*8,
  1+2*8, 0+3*8, 0+4*8, 1+3*8,
  2+2*8, 3+1*8, 4+0*8, 5+0*8,
  4+1*8, 3+2*8, 2+3*8, 1+4*8,
  0+5*8, 0+6*8, 1+5*8, 2+4*8,
  3+3*8, 4+2*8, 5+1*8, 6+0*8,
  7+0*8, 6+1*8, 5+2*8, 4+3*8,
  3+4*8, 2+5*8, 1+6*8, 0+7*8,
  1+7*8, 2+6*8, 3+5*8, 4+4*8,
  5+3*8, 6+2*8, 7+1*8, 7+2*8,
  6+3*8, 5+4*8, 4+5*8, 3+6*8,
  2+7*8, 3+7*8, 4+6*8, 5+5*8,
  6+4*8, 7+3*8, 7+4*8, 6+5*8,
  5+6*8, 4+7*8, 5+7*8, 6+6*8,
  7+5*8, 7+6*8, 6+7*8, 7+7*8,
};

/* default scaling_lists according to Table 7-2 */
static const uint8_t default_4x4_intra[16] = { 6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32,
    32, 32, 37, 37, 42 };

static const uint8_t default_4x4_inter[16] = { 10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27,
    27, 27, 30, 30, 34 };

static const uint8_t default_8x8_intra[64] = { 6, 10, 10, 13, 11, 13, 16, 16, 16, 16, 18,
    18, 18, 18, 18, 23, 23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27,
    27, 27, 27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31, 31,
    33, 33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42 };

static const uint8_t default_8x8_inter[64] = { 9, 13, 13, 15, 13, 15, 17, 17, 17, 17, 19,
    19, 19, 19, 19, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 24, 24,
    24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27, 27,
    28, 28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35 };

struct buf_reader
{
  const uint8_t *buf;
  const uint8_t *cur_pos;
  int len;
  int cur_offset;
};


static uint8_t parse_sps(struct buf_reader *buf, struct seq_parameter_set_rbsp *sps);

static void parse_vui_parameters(struct buf_reader *buf,
    struct seq_parameter_set_rbsp *sps);
static void parse_hrd_parameters(struct buf_reader *buf, struct hrd_parameters *hrd);

static uint8_t parse_pps(struct buf_reader *buf, struct pic_parameter_set_rbsp *pps);

static void parse_sei(struct buf_reader *buf, struct sei_message *sei,
    struct h264_parser *parser);

static uint8_t parse_slice_header(struct buf_reader *buf, struct nal_unit *slc_nal,
    struct h264_parser *parser);

static void parse_ref_pic_list_reordering(struct buf_reader *buf,
    struct slice_header *slc);

static void parse_pred_weight_table(struct buf_reader *buf, struct slice_header *slc,
    struct h264_parser *parser);
static void parse_dec_ref_pic_marking(struct buf_reader *buf,
    struct nal_unit *slc_nal);

/* here goes the parser implementation */

#if 0
static void decode_nal(uint8_t **ret, int *len_ret, uint8_t *buf, int buf_len)
{
  // TODO: rework without copying
  uint8_t *end = &buf[buf_len];
  uint8_t *pos = malloc(buf_len);

  *ret = pos;
  while (buf < end) {
    if (buf < end - 3 && buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x03) {

      *pos++ = 0x00;
      *pos++ = 0x00;

      buf += 3;
      continue;
    }
    *pos++ = *buf++;
  }

  *len_ret = pos - *ret;
}
#endif

#if 0
static inline void dump_bits(const char *label, const struct buf_reader *buf, int bits)
{
  struct buf_reader lbuf;
  memcpy(&lbuf, buf, sizeof(struct buf_reader));

  int i;
  printf("%s: 0b", label);
  for(i=0; i < bits; i++)
    printf("%d", read_bits(&lbuf, 1));
  printf("\n");
}
#endif

/**
 * @return total number of bits read by the buf_reader
 */
static inline uint32_t bits_read(struct buf_reader *buf)
{
  int bits_read = 0;
  bits_read = (buf->cur_pos - buf->buf)*8;
  bits_read += (8-buf->cur_offset);

  return bits_read;
}

/* skips stuffing bytes in the buf_reader */
static inline void skip_emulation_prevention_three_byte(struct buf_reader *buf)
{
  if(buf->cur_pos - buf->buf > 2 &&
      *(buf->cur_pos-2) == 0x00 &&
      *(buf->cur_pos-1) == 0x00 &&
      *buf->cur_pos == 0x03) {
    buf->cur_pos++;
  }
}

/*
 * read len bits from the buffer and return them
 * @return right aligned bits
 */
static inline uint32_t read_bits(struct buf_reader *buf, int len)
{
  static uint32_t i_mask[33] = { 0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f,
      0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff,
      0x1ffff, 0x3ffff, 0x7ffff, 0xfffff, 0x1fffff, 0x3fffff, 0x7fffff,
      0xffffff, 0x1ffffff, 0x3ffffff, 0x7ffffff, 0xfffffff, 0x1fffffff,
      0x3fffffff, 0x7fffffff, 0xffffffff };

  int i_shr;
  uint32_t bits = 0;

  while (len > 0 && (buf->cur_pos - buf->buf) < buf->len) {
    if ((i_shr = buf->cur_offset - len) >= 0) {
      bits |= (*buf->cur_pos >> i_shr) & i_mask[len];
      buf->cur_offset -= len;
      if (buf->cur_offset == 0) {
        buf->cur_pos++;
        buf->cur_offset = 8;

        skip_emulation_prevention_three_byte(buf);
      }
      return bits;
    }
    else {
      bits |= (*buf->cur_pos & i_mask[buf->cur_offset]) << -i_shr;
      len -= buf->cur_offset;
      buf->cur_pos++;
      buf->cur_offset = 8;

      skip_emulation_prevention_three_byte(buf);
    }
  }
  return bits;
}

/* determines if following bits are rtsb_trailing_bits */
static inline int rbsp_trailing_bits(const uint8_t *buf, int buf_len)
{
  const uint8_t *cur_buf = buf+(buf_len-1);
  uint8_t cur_val;
  int parsed_bits = 0;
  int i;

  while(buf_len > 0) {
    cur_val = *cur_buf;
    for(i = 0; i < 9; i++) {
      if (cur_val&1)
        return parsed_bits+i;
      cur_val>>=1;
    }
    parsed_bits += 8;
    cur_buf--;
  }

  lprintf("rbsp trailing bits could not be found\n");
  return 0;
}

static uint32_t read_exp_golomb(struct buf_reader *buf)
{
  int leading_zero_bits = 0;

  while (read_bits(buf, 1) == 0 && leading_zero_bits < 32)
    leading_zero_bits++;

  uint32_t code = ((uint64_t)1 << leading_zero_bits) - 1 + read_bits(buf,
      leading_zero_bits);
  return code;
}

static int32_t read_exp_golomb_s(struct buf_reader *buf)
{
  uint32_t ue = read_exp_golomb(buf);
  int32_t code = ue & 0x01 ? (ue + 1) / 2 : -(ue / 2);
  return code;
}


/**
 * parses the NAL header data and calls the subsequent
 * parser methods that handle specific NAL units
 */
static struct nal_unit* parse_nal_header(struct buf_reader *buf,
    struct coded_picture *pic, struct h264_parser *parser)
{
  if (buf->len < 1)
    return NULL;

  (void)pic;
  struct nal_unit *nal = create_nal_unit();

  nal->nal_ref_idc = (buf->buf[0] >> 5) & 0x03;
  nal->nal_unit_type = buf->buf[0] & 0x1f;

  buf->cur_pos = buf->buf + 1;
  //lprintf("NAL: %d\n", nal->nal_unit_type);

  //struct buf_reader ibuf;
  //ibuf.cur_offset = 8;

  switch (nal->nal_unit_type) {
    case NAL_SPS:
      parse_sps(buf, &nal->sps);
      break;
    case NAL_PPS:
      parse_pps(buf, &nal->pps);
      break;
    case NAL_SLICE:
    case NAL_PART_A:
    case NAL_PART_B:
    case NAL_PART_C:
    case NAL_SLICE_IDR:
      parse_slice_header(buf, nal, parser);
      break;
    case NAL_SEI:
      memset(&(nal->sei), 0x00, sizeof(struct sei_message));
      parse_sei(buf, &nal->sei, parser);
      break;
    default:
      break;
  }

  return nal;
}

/**
 * calculates the picture order count according to ITU-T Rec. H.264 (11/2007)
 * chapter 8.2.1, p104f
 */
static void calculate_pic_order(struct h264_parser *parser, struct coded_picture *pic,
    struct slice_header *slc)
{
  /* retrieve sps and pps from the buffers */
  struct nal_unit *pps_nal =
      nal_buffer_get_by_pps_id(parser->pps_buffer, slc->pic_parameter_set_id);

  if (pps_nal == NULL) {
    xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
        "ERR: calculate_pic_order: pic_parameter_set_id %d not found in buffers\n",
        slc->pic_parameter_set_id);
    return;
  }

  struct pic_parameter_set_rbsp *pps = &pps_nal->pps;

  struct nal_unit *sps_nal =
      nal_buffer_get_by_sps_id(parser->sps_buffer, pps->seq_parameter_set_id);

  if (sps_nal == NULL) {
    xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
        "ERR: calculate_pic_order: seq_parameter_set_id %d not found in buffers\n",
        pps->seq_parameter_set_id);
    return;
  }

  struct seq_parameter_set_rbsp *sps = &sps_nal->sps;

  if (sps->pic_order_cnt_type == 0) {

    if (pic->flag_mask & IDR_PIC) {
      parser->prev_pic_order_cnt_lsb = 0;
      parser->prev_pic_order_cnt_msb = 0;


      // FIXME
      parser->frame_num_offset = 0;
    }

    const int max_poc_lsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

    uint32_t pic_order_cnt_msb = 0;

    if (slc->pic_order_cnt_lsb < parser->prev_pic_order_cnt_lsb
        && (int)(parser->prev_pic_order_cnt_lsb - slc->pic_order_cnt_lsb)
            >= max_poc_lsb / 2)
      pic_order_cnt_msb = parser->prev_pic_order_cnt_msb + max_poc_lsb;
    else if (slc->pic_order_cnt_lsb > parser->prev_pic_order_cnt_lsb
        && (int)(parser->prev_pic_order_cnt_lsb - slc->pic_order_cnt_lsb)
            < -max_poc_lsb / 2)
      pic_order_cnt_msb = parser->prev_pic_order_cnt_msb - max_poc_lsb;
    else
      pic_order_cnt_msb = parser->prev_pic_order_cnt_msb;

    if(!slc->field_pic_flag || !slc->bottom_field_flag) {
      pic->top_field_order_cnt = pic_order_cnt_msb + slc->pic_order_cnt_lsb;
      parser->prev_top_field_order_cnt = pic->top_field_order_cnt;
    }

    if (pic->flag_mask & REFERENCE) {
      parser->prev_pic_order_cnt_msb =  pic_order_cnt_msb;
    }

    pic->bottom_field_order_cnt = 0;

    if(!slc->field_pic_flag)
      pic->bottom_field_order_cnt = pic->top_field_order_cnt + slc->delta_pic_order_cnt_bottom;
    else //if(slc->bottom_field_flag) //TODO: this is not spec compliant, but works...
      pic->bottom_field_order_cnt = pic_order_cnt_msb + slc->pic_order_cnt_lsb;

    if(slc->field_pic_flag && slc->bottom_field_flag)
      pic->top_field_order_cnt = parser->prev_top_field_order_cnt;

  } else if (sps->pic_order_cnt_type == 2) {
    uint32_t prev_frame_num = parser->last_vcl_nal ? parser->last_vcl_nal->slc.frame_num : 0;
    uint32_t prev_frame_num_offset = parser->frame_num_offset;
    uint32_t temp_pic_order_cnt = 0;

    if (parser->pic->flag_mask & IDR_PIC)
      parser->frame_num_offset = 0;
    else if (prev_frame_num > slc->frame_num)
      parser->frame_num_offset = prev_frame_num_offset + sps->max_frame_num;
    else
      parser->frame_num_offset = prev_frame_num_offset;

    if(parser->pic->flag_mask & IDR_PIC)
      temp_pic_order_cnt = 0;
    else if(!(parser->pic->flag_mask & REFERENCE))
      temp_pic_order_cnt = 2 * (parser->frame_num_offset + slc->frame_num)-1;
    else
      temp_pic_order_cnt = 2 * (parser->frame_num_offset + slc->frame_num);

    if(!slc->field_pic_flag)
      pic->top_field_order_cnt = pic->bottom_field_order_cnt = temp_pic_order_cnt;
    else if(slc->bottom_field_flag)
      pic->bottom_field_order_cnt = temp_pic_order_cnt;
    else
      pic->top_field_order_cnt = temp_pic_order_cnt;

  } else {
    xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
        "FIXME: Unsupported poc_type: %d\n", sps->pic_order_cnt_type);
  }
}

#if 0
static void skip_scaling_list(struct buf_reader *buf, int size)
{
  int i;
  for (i = 0; i < size; i++) {
    read_exp_golomb_s(buf);
  }
}
#endif

static void parse_scaling_list(struct buf_reader *buf, uint8_t *scaling_list,
    int length, int index)
{
  int last_scale = 8;
  int next_scale = 8;
  int32_t delta_scale;
  uint8_t use_default_scaling_matrix_flag = 0;
  int i;
  unsigned int u;

  const uint8_t *zigzag = (length==64) ? zigzag_8x8 : zigzag_4x4;

  for (i = 0; i < length; i++) {
    if (next_scale != 0) {
      delta_scale = read_exp_golomb_s(buf);
      next_scale = (last_scale + delta_scale + 256) % 256;
      if (i == 0 && next_scale == 0) {
        use_default_scaling_matrix_flag = 1;
        break;
      }
    }
    scaling_list[zigzag[i]] = last_scale = (next_scale == 0) ? last_scale : next_scale;
  }

  if (use_default_scaling_matrix_flag) {
    switch (index) {
      case 0:
      case 1:
      case 2: {
        for(u = 0; u < sizeof(default_4x4_intra); u++) {
          scaling_list[zigzag_4x4[u]] = default_4x4_intra[u];
        }
        //memcpy(scaling_list, default_4x4_intra, sizeof(default_4x4_intra));
        break;
      }
      case 3:
      case 4:
      case 5: {
        for(u = 0; u < sizeof(default_4x4_inter); u++) {
          scaling_list[zigzag_4x4[u]] = default_4x4_inter[u];
        }
        //memcpy(scaling_list, default_4x4_inter, sizeof(default_4x4_inter));
        break;
      }
      case 6: {
        for(u = 0; u < sizeof(default_8x8_intra); u++) {
          scaling_list[zigzag_8x8[u]] = default_8x8_intra[u];
        }
        //memcpy(scaling_list, default_8x8_intra, sizeof(default_8x8_intra));
        break;
      }
      case 7: {
        for(u = 0; u < sizeof(default_8x8_inter); u++) {
          scaling_list[zigzag_8x8[u]] = default_8x8_inter[u];
        }
        //memcpy(scaling_list, default_8x8_inter, sizeof(default_8x8_inter));
        break;
      }
    }
  }
}

static void sps_scaling_list_fallback(struct seq_parameter_set_rbsp *sps, int i)
{
  unsigned int j;
  switch (i) {
    case 0:
      for(j = 0; j < sizeof(default_4x4_intra); j++) {
        sps->scaling_lists_4x4[i][zigzag_4x4[j]] = default_4x4_intra[j];
      }
      //memcpy(sps->scaling_lists_4x4[i], default_4x4_intra, sizeof(sps->scaling_lists_4x4[i]));
      break;
    case 3:
      for(j = 0; j < sizeof(default_4x4_inter); j++) {
        sps->scaling_lists_4x4[i][zigzag_4x4[j]] = default_4x4_inter[j];
      }
      //memcpy(sps->scaling_lists_4x4[i], default_4x4_inter, sizeof(sps->scaling_lists_4x4[i]));
      break;
    case 1:
    case 2:
    case 4:
    case 5:
      memcpy(sps->scaling_lists_4x4[i], sps->scaling_lists_4x4[i-1], sizeof(sps->scaling_lists_4x4[i]));
      break;
    case 6:
      for(j = 0; j < sizeof(default_8x8_intra); j++) {
        sps->scaling_lists_8x8[i-6][zigzag_8x8[j]] = default_8x8_intra[j];
      }
      //memcpy(sps->scaling_lists_8x8[i-6], default_8x8_intra, sizeof(sps->scaling_lists_8x8[i-6]));
      break;
    case 7:
      for(j = 0; j < sizeof(default_8x8_inter); j++) {
        sps->scaling_lists_8x8[i-6][zigzag_8x8[j]] = default_8x8_inter[j];
      }
      //memcpy(sps->scaling_lists_8x8[i-6], default_8x8_inter, sizeof(sps->scaling_lists_8x8[i-6]));
      break;
  }
}

static void pps_scaling_list_fallback(struct seq_parameter_set_rbsp *sps, struct pic_parameter_set_rbsp *pps, int i)
{
  switch (i) {
    case 0:
    case 3:
      memcpy(pps->scaling_lists_4x4[i], sps->scaling_lists_4x4[i], sizeof(pps->scaling_lists_4x4[i]));
      break;
    case 1:
    case 2:
    case 4:
    case 5:
      memcpy(pps->scaling_lists_4x4[i], pps->scaling_lists_4x4[i-1], sizeof(pps->scaling_lists_4x4[i]));
      break;
    case 6:
    case 7:
      memcpy(pps->scaling_lists_8x8[i-6], sps->scaling_lists_8x8[i-6], sizeof(pps->scaling_lists_8x8[i-6]));
      break;

  }
}


static uint8_t parse_sps(struct buf_reader *buf, struct seq_parameter_set_rbsp *sps)
{
  sps->profile_idc = read_bits(buf, 8);
  sps->constraint_setN_flag = read_bits(buf, 4);
  read_bits(buf, 4);
  sps->level_idc = read_bits(buf, 8);

  sps->seq_parameter_set_id = read_exp_golomb(buf);

  memset(sps->scaling_lists_4x4, 16, sizeof(sps->scaling_lists_4x4));
  memset(sps->scaling_lists_8x8, 16, sizeof(sps->scaling_lists_8x8));
  if (sps->profile_idc == 100 || sps->profile_idc == 110 || sps->profile_idc
      == 122 || sps->profile_idc == 244 || sps->profile_idc == 44 ||
      sps->profile_idc == 83 || sps->profile_idc == 86) {
    sps->chroma_format_idc = read_exp_golomb(buf);
    if (sps->chroma_format_idc == 3) {
      sps->separate_colour_plane_flag = read_bits(buf, 1);
    }

    sps->bit_depth_luma_minus8 = read_exp_golomb(buf);
    sps->bit_depth_chroma_minus8 = read_exp_golomb(buf);
    sps->qpprime_y_zero_transform_bypass_flag = read_bits(buf, 1);
    sps->seq_scaling_matrix_present_flag = read_bits(buf, 1);
    if (sps->seq_scaling_matrix_present_flag) {
      int i;
      for (i = 0; i < 8; i++) {
        sps->seq_scaling_list_present_flag[i] = read_bits(buf, 1);

        if (sps->seq_scaling_list_present_flag[i]) {
          if (i < 6)
            parse_scaling_list(buf, sps->scaling_lists_4x4[i], 16, i);
          else
            parse_scaling_list(buf, sps->scaling_lists_8x8[i - 6], 64, i);
        } else {
          sps_scaling_list_fallback(sps, i);
        }
      }
    }
  } else
    sps->chroma_format_idc = 1;

  sps->log2_max_frame_num_minus4 = read_exp_golomb(buf);
  sps->max_frame_num = 1 << (sps->log2_max_frame_num_minus4 + 4);

  sps->pic_order_cnt_type = read_exp_golomb(buf);
  if (!sps->pic_order_cnt_type)
    sps->log2_max_pic_order_cnt_lsb_minus4 = read_exp_golomb(buf);
  else if(sps->pic_order_cnt_type == 1) {
    sps->delta_pic_order_always_zero_flag = read_bits(buf, 1);
    sps->offset_for_non_ref_pic = read_exp_golomb_s(buf);
    sps->offset_for_top_to_bottom_field = read_exp_golomb_s(buf);
    sps->num_ref_frames_in_pic_order_cnt_cycle = read_exp_golomb(buf);
    int i;
    for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++) {
      sps->offset_for_ref_frame[i] = read_exp_golomb_s(buf);
    }
  }

  sps->num_ref_frames = read_exp_golomb(buf);
  sps->gaps_in_frame_num_value_allowed_flag = read_bits(buf, 1);

  /*sps->pic_width_in_mbs_minus1 = read_exp_golomb(buf);
   sps->pic_height_in_map_units_minus1 = read_exp_golomb(buf);*/
  sps->pic_width = 16 * (read_exp_golomb(buf) + 1);
  sps->pic_height = 16 * (read_exp_golomb(buf) + 1);

  sps->frame_mbs_only_flag = read_bits(buf, 1);

  /* compute the height correctly even for interlaced material */
  sps->pic_height = (2 - sps->frame_mbs_only_flag) * sps->pic_height;
  if (sps->pic_height == 1088)
    sps->pic_height = 1080;

  if (!sps->frame_mbs_only_flag)
    sps->mb_adaptive_frame_field_flag = read_bits(buf, 1);

  sps->direct_8x8_inference_flag = read_bits(buf, 1);
  sps->frame_cropping_flag = read_bits(buf, 1);
  if (sps->frame_cropping_flag) {
    sps->frame_crop_left_offset = read_exp_golomb(buf);
    sps->frame_crop_right_offset = read_exp_golomb(buf);
    sps->frame_crop_top_offset = read_exp_golomb(buf);
    sps->frame_crop_bottom_offset = read_exp_golomb(buf);
  }
  sps->vui_parameters_present_flag = read_bits(buf, 1);
  if (sps->vui_parameters_present_flag) {
    parse_vui_parameters(buf, sps);
  }

  return 0;
}

/* evaluates values parsed by sps and modifies the current
 * picture according to them
 */
static void interpret_sps(struct coded_picture *pic, struct h264_parser *parser)
{
  if(pic->sps_nal == NULL) {
    xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
        "WARNING: Picture contains no seq_parameter_set\n");
    return;
  }

  struct seq_parameter_set_rbsp *sps = &pic->sps_nal->sps;

  if(sps->vui_parameters_present_flag &&
        sps->vui_parameters.pic_struct_present_flag) {
    parser->flag_mask |= PIC_STRUCT_PRESENT;
  } else {
    parser->flag_mask &= ~PIC_STRUCT_PRESENT;
  }

  if(sps->vui_parameters_present_flag &&
      (sps->vui_parameters.nal_hrd_parameters_present_flag ||
       sps->vui_parameters.vc1_hrd_parameters_present_flag)) {
    parser->flag_mask |= CPB_DPB_DELAYS_PRESENT;
  } else {
    parser->flag_mask &= ~(CPB_DPB_DELAYS_PRESENT);
  }

  if(pic->slc_nal != NULL) {
    struct slice_header *slc = &pic->slc_nal->slc;
    if (slc->field_pic_flag == 0) {
      pic->max_pic_num = sps->max_frame_num;
      parser->curr_pic_num = slc->frame_num;
    } else {
      pic->max_pic_num = 2 * sps->max_frame_num;
      parser->curr_pic_num = 2 * slc->frame_num + 1;
    }
  }
}

static void parse_sei(struct buf_reader *buf, struct sei_message *sei,
    struct h264_parser *parser)
{
  uint8_t tmp;

  struct nal_unit *sps_nal =
      nal_buffer_get_last(parser->sps_buffer);

  if (sps_nal == NULL) {
    xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
        "ERR: parse_sei: seq_parameter_set_id not found in buffers\n");
    return;
  }

  struct seq_parameter_set_rbsp *sps = &sps_nal->sps;

  sei->payload_type = 0;
  while((tmp = read_bits(buf, 8)) == 0xff) {
    sei->payload_type += 255;
  }
  sei->last_payload_type_byte = tmp;
  sei->payload_type += sei->last_payload_type_byte;

  sei->payload_size = 0;
  while((tmp = read_bits(buf, 8)) == 0xff) {
    sei->payload_size += 255;
  }
  sei->last_payload_size_byte = tmp;
  sei->payload_size += sei->last_payload_size_byte;

  /* pic_timing */
  if(sei->payload_type == 1) {
    if(parser->flag_mask & CPB_DPB_DELAYS_PRESENT) {
      sei->pic_timing.cpb_removal_delay = read_bits(buf, 5);
      sei->pic_timing.dpb_output_delay = read_bits(buf, 5);
    }

    if(parser->flag_mask & PIC_STRUCT_PRESENT) {
      sei->pic_timing.pic_struct = read_bits(buf, 4);

      uint8_t NumClockTs = 0;
      switch(sei->pic_timing.pic_struct) {
        case 0:
        case 1:
        case 2:
          NumClockTs = 1;
          break;
        case 3:
        case 4:
        case 7:
          NumClockTs = 2;
          break;
        case 5:
        case 6:
        case 8:
          NumClockTs = 3;
          break;
      }

      int i;
      for(i = 0; i < NumClockTs; i++) {
        if(read_bits(buf, 1)) { /* clock_timestamp_flag == 1 */
          sei->pic_timing.ct_type = read_bits(buf, 2);
          sei->pic_timing.nuit_field_based_flag = read_bits(buf, 1);
          sei->pic_timing.counting_type = read_bits(buf, 5);
          sei->pic_timing.full_timestamp_flag = read_bits(buf, 1);
          sei->pic_timing.discontinuity_flag = read_bits(buf, 1);
          sei->pic_timing.cnt_dropped_flag = read_bits(buf, 1);
          sei->pic_timing.n_frames = read_bits(buf, 8);
          if(sei->pic_timing.full_timestamp_flag) {
            sei->pic_timing.seconds_value = read_bits(buf, 6);
            sei->pic_timing.minutes_value = read_bits(buf, 6);
            sei->pic_timing.hours_value = read_bits(buf, 5);
          } else {
            if(read_bits(buf, 1)) {
              sei->pic_timing.seconds_value = read_bits(buf, 6);

              if(read_bits(buf, 1)) {
                sei->pic_timing.minutes_value = read_bits(buf, 6);

                if(read_bits(buf, 1)) {
                  sei->pic_timing.hours_value = read_bits(buf, 5);
                }
              }
            }
          }

          if(sps->vui_parameters_present_flag &&
              sps->vui_parameters.nal_hrd_parameters_present_flag) {
            sei->pic_timing.time_offset =
                read_bits(buf,
                    sps->vui_parameters.nal_hrd_parameters.time_offset_length);
          }
        }
      }
    }
  } /*else {
    fprintf(stderr, "Unimplemented SEI payload: %d\n", sei->payload_type);
  }*/

}

static void interpret_sei(struct coded_picture *pic)
{
  if(!pic->sps_nal || !pic->sei_nal)
    return;

  struct seq_parameter_set_rbsp *sps = &pic->sps_nal->sps;
  struct sei_message *sei = &pic->sei_nal->sei;

  if(sps && sps->vui_parameters_present_flag &&
      sps->vui_parameters.pic_struct_present_flag) {
    switch(sei->pic_timing.pic_struct) {
      case DISP_FRAME:
        pic->flag_mask &= ~INTERLACED;
        pic->repeat_pic = 0;
        break;
      case DISP_TOP:
      case DISP_BOTTOM:
      case DISP_TOP_BOTTOM:
      case DISP_BOTTOM_TOP:
        pic->flag_mask |= INTERLACED;
        break;
      case DISP_TOP_BOTTOM_TOP:
      case DISP_BOTTOM_TOP_BOTTOM:
        pic->flag_mask |= INTERLACED;
        pic->repeat_pic = 1;
        break;
      case DISP_FRAME_DOUBLING:
        pic->flag_mask &= ~INTERLACED;
        pic->repeat_pic = 2;
        break;
      case DISP_FRAME_TRIPLING:
        pic->flag_mask &= ~INTERLACED;
        pic->repeat_pic = 3;
    }
  }
}

static void parse_vui_parameters(struct buf_reader *buf,
    struct seq_parameter_set_rbsp *sps)
{
  sps->vui_parameters.aspect_ration_info_present_flag = read_bits(buf, 1);
  if (sps->vui_parameters.aspect_ration_info_present_flag == 1) {
    sps->vui_parameters.aspect_ratio_idc = read_bits(buf, 8);
    if (sps->vui_parameters.aspect_ratio_idc == ASPECT_EXTENDED_SAR) {
      sps->vui_parameters.sar_width = read_bits(buf, 16);
      sps->vui_parameters.sar_height = read_bits(buf, 16);
    }
  }

  sps->vui_parameters.overscan_info_present_flag = read_bits(buf, 1);
  if (sps->vui_parameters.overscan_info_present_flag) {
    sps->vui_parameters.overscan_appropriate_flag = read_bits(buf, 1);
  }

  sps->vui_parameters.video_signal_type_present_flag = read_bits(buf, 1);
  if (sps->vui_parameters.video_signal_type_present_flag) {
    sps->vui_parameters.video_format = read_bits(buf, 3);
    sps->vui_parameters.video_full_range_flag = read_bits(buf, 1);
    sps->vui_parameters.colour_description_present = read_bits(buf, 1);
    if (sps->vui_parameters.colour_description_present) {
      sps->vui_parameters.colour_primaries = read_bits(buf, 8);
      sps->vui_parameters.transfer_characteristics = read_bits(buf, 8);
      sps->vui_parameters.matrix_coefficients = read_bits(buf, 8);
    }
  }

  sps->vui_parameters.chroma_loc_info_present_flag = read_bits(buf, 1);
  if (sps->vui_parameters.chroma_loc_info_present_flag) {
    sps->vui_parameters.chroma_sample_loc_type_top_field = read_exp_golomb(buf);
    sps->vui_parameters.chroma_sample_loc_type_bottom_field = read_exp_golomb(
        buf);
  }

  sps->vui_parameters.timing_info_present_flag = read_bits(buf, 1);
  if (sps->vui_parameters.timing_info_present_flag) {
    uint32_t num_units_in_tick = read_bits(buf, 32);
    uint32_t time_scale = read_bits(buf, 32);
    sps->vui_parameters.num_units_in_tick = num_units_in_tick;
    sps->vui_parameters.time_scale = time_scale;
    sps->vui_parameters.fixed_frame_rate_flag = read_bits(buf, 1);
  }

  sps->vui_parameters.nal_hrd_parameters_present_flag = read_bits(buf, 1);
  if (sps->vui_parameters.nal_hrd_parameters_present_flag)
    parse_hrd_parameters(buf, &sps->vui_parameters.nal_hrd_parameters);

  sps->vui_parameters.vc1_hrd_parameters_present_flag = read_bits(buf, 1);
  if (sps->vui_parameters.vc1_hrd_parameters_present_flag)
    parse_hrd_parameters(buf, &sps->vui_parameters.vc1_hrd_parameters);

  if (sps->vui_parameters.nal_hrd_parameters_present_flag
      || sps->vui_parameters.vc1_hrd_parameters_present_flag)
    sps->vui_parameters.low_delay_hrd_flag = read_bits(buf, 1);

  sps->vui_parameters.pic_struct_present_flag = read_bits(buf, 1);
  sps->vui_parameters.bitstream_restriction_flag = read_bits(buf, 1);

  if (sps->vui_parameters.bitstream_restriction_flag) {
    sps->vui_parameters.motion_vectors_over_pic_boundaries = read_bits(buf, 1);
    sps->vui_parameters.max_bytes_per_pic_denom = read_exp_golomb(buf);
    sps->vui_parameters.max_bits_per_mb_denom = read_exp_golomb(buf);
    sps->vui_parameters.log2_max_mv_length_horizontal = read_exp_golomb(buf);
    sps->vui_parameters.log2_max_mv_length_vertical = read_exp_golomb(buf);
    sps->vui_parameters.num_reorder_frames = read_exp_golomb(buf);
    sps->vui_parameters.max_dec_frame_buffering = read_exp_golomb(buf);
  }
}

static void parse_hrd_parameters(struct buf_reader *buf, struct hrd_parameters *hrd)
{
  hrd->cpb_cnt_minus1 = read_exp_golomb(buf);
  hrd->bit_rate_scale = read_bits(buf, 4);
  hrd->cpb_size_scale = read_bits(buf, 4);

  if (hrd->cpb_cnt_minus1 > 31)
    hrd->cpb_cnt_minus1 = 31;

  unsigned int i;
  for (i = 0; i <= hrd->cpb_cnt_minus1; i++) {
    hrd->bit_rate_value_minus1[i] = read_exp_golomb(buf);
    hrd->cpb_size_value_minus1[i] = read_exp_golomb(buf);
    hrd->cbr_flag[i] = read_bits(buf, 1);
  }

  hrd->initial_cpb_removal_delay_length_minus1 = read_bits(buf, 5);
  hrd->cpb_removal_delay_length_minus1 = read_bits(buf, 5);
  hrd->dpb_output_delay_length_minus1 = read_bits(buf, 5);
  hrd->time_offset_length = read_bits(buf, 5);
}

static uint8_t parse_pps(struct buf_reader *buf, struct pic_parameter_set_rbsp *pps)
{
  pps->pic_parameter_set_id = read_exp_golomb(buf);
  pps->seq_parameter_set_id = read_exp_golomb(buf);
  pps->entropy_coding_mode_flag = read_bits(buf, 1);
  pps->pic_order_present_flag = read_bits(buf, 1);

  pps->num_slice_groups_minus1 = read_exp_golomb(buf);
  if (pps->num_slice_groups_minus1 > 0) {
    pps->slice_group_map_type = read_exp_golomb(buf);
    if (pps->slice_group_map_type == 0) {
      unsigned int i_group;
      for (i_group = 0; i_group <= pps->num_slice_groups_minus1; i_group++) {
        if (i_group < 64)
          pps->run_length_minus1[i_group] = read_exp_golomb(buf);
        else { // FIXME: skips if more than 64 groups exist
          lprintf("Error: Only 64 slice_groups are supported\n");
          read_exp_golomb(buf);
        }
      }
    }
    else if (pps->slice_group_map_type == 3 || pps->slice_group_map_type == 4
        || pps->slice_group_map_type == 5) {
      pps->slice_group_change_direction_flag = read_bits(buf, 1);
      pps->slice_group_change_rate_minus1 = read_exp_golomb(buf);
    }
    else if (pps->slice_group_map_type == 6) {
      pps->pic_size_in_map_units_minus1 = read_exp_golomb(buf);
      unsigned int i_group;
      for (i_group = 0; i_group <= pps->num_slice_groups_minus1; i_group++) {
        pps->slice_group_id[i_group] = read_bits(buf, ceil(log(
            pps->num_slice_groups_minus1 + 1)));
      }
    }
  }

  pps->num_ref_idx_l0_active_minus1 = read_exp_golomb(buf);
  pps->num_ref_idx_l1_active_minus1 = read_exp_golomb(buf);
  pps->weighted_pred_flag = read_bits(buf, 1);
  pps->weighted_bipred_idc = read_bits(buf, 2);
  pps->pic_init_qp_minus26 = read_exp_golomb_s(buf);
  pps->pic_init_qs_minus26 = read_exp_golomb_s(buf);
  pps->chroma_qp_index_offset = read_exp_golomb_s(buf);
  pps->deblocking_filter_control_present_flag = read_bits(buf, 1);
  pps->constrained_intra_pred_flag = read_bits(buf, 1);
  pps->redundant_pic_cnt_present_flag = read_bits(buf, 1);

  int bit_length = (buf->len*8)-rbsp_trailing_bits(buf->buf, buf->len);
  int bit_read = bits_read(buf);

  memset(pps->scaling_lists_4x4, 16, sizeof(pps->scaling_lists_4x4));
  memset(pps->scaling_lists_8x8, 16, sizeof(pps->scaling_lists_8x8));
  if (bit_length-bit_read > 1) {
    pps->transform_8x8_mode_flag = read_bits(buf, 1);
    pps->pic_scaling_matrix_present_flag = read_bits(buf, 1);
    if (pps->pic_scaling_matrix_present_flag) {
      int i;
      for (i = 0; i < 8; i++) {
        if(i < 6 || pps->transform_8x8_mode_flag)
          pps->pic_scaling_list_present_flag[i] = read_bits(buf, 1);
        else
          pps->pic_scaling_list_present_flag[i] = 0;

        if (pps->pic_scaling_list_present_flag[i]) {
          if (i < 6)
            parse_scaling_list(buf, pps->scaling_lists_4x4[i], 16, i);
          else
            parse_scaling_list(buf, pps->scaling_lists_8x8[i - 6], 64, i);
        }
      }
    }

    pps->second_chroma_qp_index_offset = read_exp_golomb_s(buf);
  } else
    pps->second_chroma_qp_index_offset = pps->chroma_qp_index_offset;

  return 0;
}

static void interpret_pps(struct coded_picture *pic)
{
  if(pic->sps_nal == NULL) {
    lprintf("WARNING: Picture contains no seq_parameter_set\n");
    return;
  } else if(pic->pps_nal == NULL) {
    lprintf("WARNING: Picture contains no pic_parameter_set\n");
    return;
  }

  struct seq_parameter_set_rbsp *sps = &pic->sps_nal->sps;
  struct pic_parameter_set_rbsp *pps = &pic->pps_nal->pps;

  int i;
  for (i = 0; i < 8; i++) {
    if (!pps->pic_scaling_list_present_flag[i]) {
      pps_scaling_list_fallback(sps, pps, i);
    }
  }

  if (!pps->pic_scaling_matrix_present_flag && sps != NULL) {
    memcpy(pps->scaling_lists_4x4, sps->scaling_lists_4x4,
        sizeof(pps->scaling_lists_4x4));
    memcpy(pps->scaling_lists_8x8, sps->scaling_lists_8x8,
        sizeof(pps->scaling_lists_8x8));
  }
}

static uint8_t parse_slice_header(struct buf_reader *buf, struct nal_unit *slc_nal,
    struct h264_parser *parser)
{
  struct slice_header *slc = &slc_nal->slc;

  slc->first_mb_in_slice = read_exp_golomb(buf);
  /* we do some parsing on the slice type, because the list is doubled */
  slc->slice_type = slice_type(read_exp_golomb(buf));

  //print_slice_type(slc->slice_type);
  slc->pic_parameter_set_id = read_exp_golomb(buf);

  /* retrieve sps and pps from the buffers */
  struct nal_unit *pps_nal =
      nal_buffer_get_by_pps_id(parser->pps_buffer, slc->pic_parameter_set_id);

  if (pps_nal == NULL) {
    xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
        "ERR: parse_slice_header: pic_parameter_set_id %d not found in buffers\n",
        slc->pic_parameter_set_id);
    return -1;
  }

  struct pic_parameter_set_rbsp *pps = &pps_nal->pps;

  struct nal_unit *sps_nal =
      nal_buffer_get_by_sps_id(parser->sps_buffer, pps->seq_parameter_set_id);

  if (sps_nal == NULL) {
    xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
        "ERR: parse_slice_header: seq_parameter_set_id %d not found in buffers\n",
        pps->seq_parameter_set_id);
    return -1;
  }

  struct seq_parameter_set_rbsp *sps = &sps_nal->sps;

  if(sps->separate_colour_plane_flag)
    slc->colour_plane_id = read_bits(buf, 2);

  slc->frame_num = read_bits(buf, sps->log2_max_frame_num_minus4 + 4);
  if (!sps->frame_mbs_only_flag) {
    slc->field_pic_flag = read_bits(buf, 1);
    if (slc->field_pic_flag)
      slc->bottom_field_flag = read_bits(buf, 1);
    else
      slc->bottom_field_flag = 0;
  }
  else {
    slc->field_pic_flag = 0;
    slc->bottom_field_flag = 0;
  }

  if (slc_nal->nal_unit_type == NAL_SLICE_IDR)
    slc->idr_pic_id = read_exp_golomb(buf);

  if (!sps->pic_order_cnt_type) {
    slc->pic_order_cnt_lsb = read_bits(buf,
        sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    if (pps->pic_order_present_flag && !slc->field_pic_flag)
      slc->delta_pic_order_cnt_bottom = read_exp_golomb_s(buf);
  }

  if (sps->pic_order_cnt_type == 1 && !sps->delta_pic_order_always_zero_flag) {
    slc->delta_pic_order_cnt[0] = read_exp_golomb_s(buf);
    if (pps->pic_order_present_flag && !slc->field_pic_flag)
      slc->delta_pic_order_cnt[1] = read_exp_golomb_s(buf);
  }

  if (pps->redundant_pic_cnt_present_flag == 1) {
    slc->redundant_pic_cnt = read_exp_golomb(buf);
  }

  if (slc->slice_type == SLICE_B)
    slc->direct_spatial_mv_pred_flag = read_bits(buf, 1);

  /* take default values in case they are not set here */
  slc->num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_active_minus1;
  slc->num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_active_minus1;

  if (slc->slice_type == SLICE_P || slc->slice_type == SLICE_SP
      || slc->slice_type == SLICE_B) {
    slc->num_ref_idx_active_override_flag = read_bits(buf, 1);

    if (slc->num_ref_idx_active_override_flag == 1) {
      slc->num_ref_idx_l0_active_minus1 = read_exp_golomb(buf);

      if (slc->slice_type == SLICE_B) {
        slc->num_ref_idx_l1_active_minus1 = read_exp_golomb(buf);
      }
    }
  }

  /* --- ref_pic_list_reordering --- */
  parse_ref_pic_list_reordering(buf, slc);

  /* --- pred_weight_table --- */
  if ((pps->weighted_pred_flag && (slc->slice_type == SLICE_P
      || slc->slice_type == SLICE_SP)) || (pps->weighted_bipred_idc == 1
      && slc->slice_type == SLICE_B)) {
    parse_pred_weight_table(buf, slc, parser);
  }

  /* --- dec_ref_pic_marking --- */
  if (slc_nal->nal_ref_idc != 0)
    parse_dec_ref_pic_marking(buf, slc_nal);
  else
    slc->dec_ref_pic_marking_count = 0;

  return 0;
}

static void interpret_slice_header(struct h264_parser *parser, struct nal_unit *slc_nal)
{
  struct coded_picture *pic = parser->pic;
  struct slice_header *slc = &slc_nal->slc;

  /* retrieve sps and pps from the buffers */
  struct nal_unit *pps_nal =
      nal_buffer_get_by_pps_id(parser->pps_buffer, slc->pic_parameter_set_id);

  if (pps_nal == NULL) {
    xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
        "ERR: interpret_slice_header: pic_parameter_set_id %d not found in buffers\n",
        slc->pic_parameter_set_id);
    return;
  }

  struct nal_unit *sps_nal =
      nal_buffer_get_by_sps_id(parser->sps_buffer, pps_nal->pps.seq_parameter_set_id);

  if (sps_nal == NULL) {
    xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
        "ERR: interpret_slice_header: seq_parameter_set_id %d not found in buffers\n",
        pps_nal->pps.seq_parameter_set_id);
    return;
  }

  if (pic->sps_nal) {
    release_nal_unit(pic->sps_nal);
  }
  if (pic->pps_nal) {
    release_nal_unit(pic->pps_nal);
  }
  lock_nal_unit(sps_nal);
  pic->sps_nal = sps_nal;
  lock_nal_unit(pps_nal);
  pic->pps_nal = pps_nal;
}

static void parse_ref_pic_list_reordering(struct buf_reader *buf, struct slice_header *slc)
{
  if (slc->slice_type != SLICE_I && slc->slice_type != SLICE_SI) {
    slc->ref_pic_list_reordering.ref_pic_list_reordering_flag_l0 = read_bits(
        buf, 1);

    if (slc->ref_pic_list_reordering.ref_pic_list_reordering_flag_l0 == 1) {
      do {
        slc->ref_pic_list_reordering.reordering_of_pic_nums_idc
            = read_exp_golomb(buf);

        if (slc->ref_pic_list_reordering.reordering_of_pic_nums_idc == 0
            || slc->ref_pic_list_reordering.reordering_of_pic_nums_idc == 1) {
          slc->ref_pic_list_reordering.abs_diff_pic_num_minus1
              = read_exp_golomb(buf);
        }
        else if (slc->ref_pic_list_reordering.reordering_of_pic_nums_idc == 2) {
          slc->ref_pic_list_reordering.long_term_pic_num = read_exp_golomb(buf);
        }
      } while (slc->ref_pic_list_reordering.reordering_of_pic_nums_idc != 3);
    }
  }

  if (slc->slice_type == SLICE_B) {
    slc->ref_pic_list_reordering.ref_pic_list_reordering_flag_l1 = read_bits(
        buf, 1);

    if (slc->ref_pic_list_reordering.ref_pic_list_reordering_flag_l1 == 1) {
      do {
        slc->ref_pic_list_reordering.reordering_of_pic_nums_idc
            = read_exp_golomb(buf);

        if (slc->ref_pic_list_reordering.reordering_of_pic_nums_idc == 0
            || slc->ref_pic_list_reordering.reordering_of_pic_nums_idc == 1) {
          slc->ref_pic_list_reordering.abs_diff_pic_num_minus1
              = read_exp_golomb(buf);
        }
        else if (slc->ref_pic_list_reordering.reordering_of_pic_nums_idc == 2) {
          slc->ref_pic_list_reordering.long_term_pic_num = read_exp_golomb(buf);
        }
      } while (slc->ref_pic_list_reordering.reordering_of_pic_nums_idc != 3);
    }
  }
}

static void parse_pred_weight_table(struct buf_reader *buf, struct slice_header *slc,
    struct h264_parser *parser)
{
  unsigned int i;
  /* retrieve sps and pps from the buffers */
  struct pic_parameter_set_rbsp *pps =
      &nal_buffer_get_by_pps_id(parser->pps_buffer, slc->pic_parameter_set_id)
      ->pps;

  struct seq_parameter_set_rbsp *sps =
      &nal_buffer_get_by_sps_id(parser->sps_buffer, pps->seq_parameter_set_id)
      ->sps;

  slc->pred_weight_table.luma_log2_weight_denom = read_exp_golomb(buf);

  uint32_t ChromaArrayType = sps->chroma_format_idc;
  if(sps->separate_colour_plane_flag)
    ChromaArrayType = 0;

  if (ChromaArrayType != 0)
    slc->pred_weight_table.chroma_log2_weight_denom = read_exp_golomb(buf);

  for (i = 0; i <= slc->num_ref_idx_l0_active_minus1; i++) {
    uint8_t luma_weight_l0_flag = read_bits(buf, 1);

    if (luma_weight_l0_flag == 1) {
      slc->pred_weight_table.luma_weight_l0[i] = read_exp_golomb_s(buf);
      slc->pred_weight_table.luma_offset_l0[i] = read_exp_golomb_s(buf);
    }

    if (ChromaArrayType != 0) {
      uint8_t chroma_weight_l0_flag = read_bits(buf, 1);

      if (chroma_weight_l0_flag == 1) {
        int j;
        for (j = 0; j < 2; j++) {
          slc->pred_weight_table.chroma_weight_l0[i][j]
              = read_exp_golomb_s(buf);
          slc->pred_weight_table.chroma_offset_l0[i][j]
              = read_exp_golomb_s(buf);
        }
      }
    }
  }

  if ((slc->slice_type % 5) == SLICE_B) {
    /* FIXME: Being spec-compliant here and loop to num_ref_idx_l0_active_minus1
     * will break Divx7 files. Keep this in mind if any other streams are broken
     */
    for (i = 0; i <= slc->num_ref_idx_l1_active_minus1; i++) {
      uint8_t luma_weight_l1_flag = read_bits(buf, 1);

      if (luma_weight_l1_flag == 1) {
        slc->pred_weight_table.luma_weight_l1[i] = read_exp_golomb_s(buf);
        slc->pred_weight_table.luma_offset_l1[i] = read_exp_golomb_s(buf);
      }

      if (ChromaArrayType != 0) {
        uint8_t chroma_weight_l1_flag = read_bits(buf, 1);

        if (chroma_weight_l1_flag == 1) {
          int j;
          for (j = 0; j < 2; j++) {
            slc->pred_weight_table.chroma_weight_l1[i][j]
                = read_exp_golomb_s(buf);
            slc->pred_weight_table.chroma_offset_l1[i][j]
                = read_exp_golomb_s(buf);
          }
        }
      }
    }
  }
}

/**
 * PicNum calculation following ITU-T H264 11/2007
 * 8.2.4.1 p112f
 */
static void calculate_pic_nums(struct h264_parser *parser, struct coded_picture *cpic)
{
  struct decoded_picture *pic = NULL;
  struct slice_header *cslc = &cpic->slc_nal->slc;

  xine_list_iterator_t ite = xine_list_front(parser->dpb->reference_list);
  while (ite) {
    pic = xine_list_get_value(parser->dpb->reference_list, ite);

    int i;
    for (i=0; i<2; i++) {
      if(pic->coded_pic[i] == NULL)
        continue;

      struct slice_header *slc = &pic->coded_pic[i]->slc_nal->slc;
      struct seq_parameter_set_rbsp *sps = &pic->coded_pic[i]->sps_nal->sps;

      if (!pic->coded_pic[i]->used_for_long_term_ref) {
        int32_t frame_num_wrap = 0;
        if (slc->frame_num > cslc->frame_num)
          frame_num_wrap = slc->frame_num - sps->max_frame_num;
        else
          frame_num_wrap = slc->frame_num;

        if(i == 0) {
          pic->frame_num_wrap = frame_num_wrap;
        }

        if (cslc->field_pic_flag == 0) {
          pic->coded_pic[i]->pic_num = frame_num_wrap;
        } else {
          pic->coded_pic[i]->pic_num = 2 * frame_num_wrap;
          if((slc->field_pic_flag == 1 &&
              cslc->bottom_field_flag == slc->bottom_field_flag) ||
              (slc->field_pic_flag == 0 && !cslc->bottom_field_flag))
            pic->coded_pic[i]->pic_num++;
        }
      } else {
        pic->coded_pic[i]->long_term_pic_num = pic->coded_pic[i]->long_term_frame_idx;
        if(slc->bottom_field_flag == cslc->bottom_field_flag)
          pic->coded_pic[i]->long_term_pic_num++;
      }
    }

    ite = xine_list_next(parser->dpb->reference_list, ite);
  }
}

static void execute_ref_pic_marking(struct coded_picture *cpic,
    uint32_t memory_management_control_operation,
    uint32_t marking_nr,
    struct h264_parser *parser)
{
  /**
   * according to NOTE 6, p83 the dec_ref_pic_marking
   * structure is identical for all slice headers within
   * a coded picture, so we can simply use the last
   * slice_header we saw in the pic
   */
  if (!cpic->slc_nal)
    return;
  struct slice_header *slc = &cpic->slc_nal->slc;
  struct dpb *dpb = parser->dpb;

  calculate_pic_nums(parser, cpic);

  if (cpic->flag_mask & IDR_PIC) {
    if(slc->dec_ref_pic_marking[marking_nr].long_term_reference_flag) {
      cpic->used_for_long_term_ref = 1;
      dpb_set_unused_ref_picture_lidx_gt(dpb, 0);
    } else {
      dpb_set_unused_ref_picture_lidx_gt(dpb, -1);
    }
    return;
  }

  /* MMC operation == 1 : 8.2.5.4.1, p. 120 */
  if (memory_management_control_operation == 1) {
    // short-term -> unused for reference
    int32_t pic_num_x = (parser->curr_pic_num
        - (slc->dec_ref_pic_marking[marking_nr].difference_of_pic_nums_minus1 + 1));
        //% cpic->max_pic_num;
    struct decoded_picture* pic = NULL;
    if ((pic = dpb_get_picture(dpb, pic_num_x)) != NULL) {
      if (cpic->slc_nal->slc.field_pic_flag == 0) {
        dpb_unmark_reference_picture(dpb, pic);
      } else {

        if (pic->coded_pic[0]->slc_nal->slc.field_pic_flag == 1) {
          if (pic->top_is_reference)
            pic->top_is_reference = 0;
          else if (pic->bottom_is_reference)
            pic->bottom_is_reference = 0;

          if(!pic->top_is_reference && !pic->bottom_is_reference)
            dpb_unmark_reference_picture(dpb, pic);
        } else {
          pic->top_is_reference = pic->bottom_is_reference = 0;
          dpb_unmark_reference_picture(dpb, pic);
        }
      }
    } else {
        xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
            "H264: mmc 1 failed: %d not existent - curr_pic: %d\n",
            pic_num_x, parser->curr_pic_num);
    }
  } else if (memory_management_control_operation == 2) {
    // long-term -> unused for reference
    struct decoded_picture* pic = dpb_get_picture_by_ltpn(dpb,
        slc->dec_ref_pic_marking[marking_nr].long_term_pic_num);
    if (pic != NULL) {
      if (cpic->slc_nal->slc.field_pic_flag == 0)
        dpb_set_unused_ref_picture_byltpn(dpb,
            slc->dec_ref_pic_marking[marking_nr].long_term_pic_num);
      else {

        if (pic->coded_pic[0]->slc_nal->slc.field_pic_flag == 1) {
          if (pic->top_is_reference)
            pic->top_is_reference = 0;
          else if (pic->bottom_is_reference)
            pic->bottom_is_reference = 0;

          if(!pic->top_is_reference && !pic->bottom_is_reference) {
            dpb_set_unused_ref_picture_byltpn(dpb,
                slc->dec_ref_pic_marking[marking_nr].long_term_pic_num);
          }
        } else {
          pic->top_is_reference = pic->bottom_is_reference = 0;
          dpb_set_unused_ref_picture_byltpn(dpb,
              slc->dec_ref_pic_marking[marking_nr].long_term_pic_num);
        }
      }
    }
  } else if (memory_management_control_operation == 3) {
    // short-term -> long-term, set long-term frame index
    uint32_t pic_num_x = parser->curr_pic_num
        - (slc->dec_ref_pic_marking[marking_nr].difference_of_pic_nums_minus1 + 1);
    struct decoded_picture* pic = dpb_get_picture_by_ltidx(dpb,
        slc->dec_ref_pic_marking[marking_nr].long_term_pic_num);
    if (pic != NULL)
      dpb_set_unused_ref_picture_bylidx(dpb,
          slc->dec_ref_pic_marking[marking_nr].long_term_frame_idx);

    pic = dpb_get_picture(dpb, pic_num_x);
    if (pic) {
      pic = dpb_get_picture(dpb, pic_num_x);

      if (pic->coded_pic[0]->slc_nal->slc.field_pic_flag == 0) {
        pic->coded_pic[0]->long_term_frame_idx
            = slc->dec_ref_pic_marking[marking_nr].long_term_frame_idx;
        pic->coded_pic[0]->long_term_pic_num = pic->coded_pic[0]->long_term_frame_idx;
      }
      else {
        if(pic->coded_pic[0]->pic_num == (int32_t)pic_num_x) {
          pic->coded_pic[0]->long_term_frame_idx
              = slc->dec_ref_pic_marking[marking_nr].long_term_frame_idx;
          pic->coded_pic[0]->long_term_pic_num = pic->coded_pic[0]->long_term_frame_idx * 2 + 1;
        } else if(pic->coded_pic[1] != NULL &&
            pic->coded_pic[1]->pic_num == (int32_t)pic_num_x) {
          pic->coded_pic[1]->long_term_frame_idx
              = slc->dec_ref_pic_marking[marking_nr].long_term_frame_idx;
          pic->coded_pic[1]->long_term_pic_num = pic->coded_pic[1]->long_term_frame_idx * 2 + 1;
        }
      }
    }
    else {
      xprintf(parser->xine, XINE_VERBOSITY_DEBUG,
          "memory_management_control_operation: 3 failed. No such picture.\n");
    }

  } else if (memory_management_control_operation == 4) {
    /* set max-long-term frame index,
     * mark all long-term pictures with long-term frame idx
     * greater max-long-term farme idx as unused for ref */
    if (slc->dec_ref_pic_marking[marking_nr].max_long_term_frame_idx_plus1 == 0)
      dpb_set_unused_ref_picture_lidx_gt(dpb, 0);
    else
      dpb_set_unused_ref_picture_lidx_gt(dpb,
          slc->dec_ref_pic_marking[marking_nr].max_long_term_frame_idx_plus1 - 1);
  } else if (memory_management_control_operation == 5) {
    /* mark all ref pics as unused for reference,
     * set max-long-term frame index = no long-term frame idxs */
    dpb_flush(dpb);

    if (!slc->bottom_field_flag) {
      parser->prev_pic_order_cnt_lsb = cpic->top_field_order_cnt;
      parser->prev_pic_order_cnt_msb = 0;
    } else {
      parser->prev_pic_order_cnt_lsb = 0;
      parser->prev_pic_order_cnt_msb = 0;
    }
  } else if (memory_management_control_operation == 6) {
    /* mark current picture as used for long-term ref,
     * assing long-term frame idx to it */
    struct decoded_picture* pic = dpb_get_picture_by_ltidx(dpb,
        slc->dec_ref_pic_marking[marking_nr].long_term_frame_idx);
    if (pic != NULL)
      dpb_set_unused_ref_picture_bylidx(dpb,
          slc->dec_ref_pic_marking[marking_nr].long_term_frame_idx);

    cpic->long_term_frame_idx = slc->dec_ref_pic_marking[marking_nr].long_term_frame_idx;
    cpic->used_for_long_term_ref = 1;

    if (slc->field_pic_flag == 0) {
      cpic->long_term_pic_num = cpic->long_term_frame_idx;
    }
    else {
      cpic->long_term_pic_num = cpic->long_term_frame_idx * 2 + 1;
    }

  }
}

static void parse_dec_ref_pic_marking(struct buf_reader *buf,
    struct nal_unit *slc_nal)
{
  struct slice_header *slc = &slc_nal->slc;

  if (!slc)
    return;

  slc->dec_ref_pic_marking_count = 0;
  int i = slc->dec_ref_pic_marking_count;

  if (slc_nal->nal_unit_type == NAL_SLICE_IDR) {
    slc->dec_ref_pic_marking[i].no_output_of_prior_pics_flag = read_bits(buf, 1);
    slc->dec_ref_pic_marking[i].long_term_reference_flag = read_bits(buf, 1);
    i+=2;
  } else {
    slc->dec_ref_pic_marking[i].adaptive_ref_pic_marking_mode_flag = read_bits(
        buf, 1);

    if (slc->dec_ref_pic_marking[i].adaptive_ref_pic_marking_mode_flag) {
      do {
        slc->dec_ref_pic_marking[i].memory_management_control_operation
            = read_exp_golomb(buf);

        if (slc->dec_ref_pic_marking[i].memory_management_control_operation == 1
            || slc->dec_ref_pic_marking[i].memory_management_control_operation
                == 3)
          slc->dec_ref_pic_marking[i].difference_of_pic_nums_minus1
              = read_exp_golomb(buf);

        if (slc->dec_ref_pic_marking[i].memory_management_control_operation == 2)
          slc->dec_ref_pic_marking[i].long_term_pic_num = read_exp_golomb(buf);

        if (slc->dec_ref_pic_marking[i].memory_management_control_operation == 3
            || slc->dec_ref_pic_marking[i].memory_management_control_operation
                == 6)
          slc->dec_ref_pic_marking[i].long_term_frame_idx = read_exp_golomb(buf);

        if (slc->dec_ref_pic_marking[i].memory_management_control_operation == 4)
          slc->dec_ref_pic_marking[i].max_long_term_frame_idx_plus1
              = read_exp_golomb(buf);

        i++;
        if(i >= 10) {
          lprintf("Error: Not more than 10 MMC operations supported per slice. Dropping some.\n");
          i = 0;
        }
      } while (slc->dec_ref_pic_marking[i-1].memory_management_control_operation
          != 0);
    }
  }

  slc->dec_ref_pic_marking_count = (i>0) ? (i-1) : 0;
}

/* ----------------- NAL parser ----------------- */

#if 0
static void reset_parser(struct h264_parser *parser)
{
  parser->position = NON_VCL;
  parser->buf_len = parser->prebuf_len = 0;
  parser->next_nal_position = 0;
  parser->last_nal_res = 0;

  if(parser->last_vcl_nal) {
    release_nal_unit(parser->last_vcl_nal);
  }
  parser->last_vcl_nal = NULL;

  parser->prev_pic_order_cnt_msb = 0;
  parser->prev_pic_order_cnt_lsb = 0;
  parser->frame_num_offset = 0;
  parser->prev_top_field_order_cnt = 0;
  parser->curr_pic_num = 0;
  parser->flag_mask = 0;

  if(parser->pic != NULL) {
    free_coded_picture(parser->pic);
    parser->pic = create_coded_picture();
  }
}
#endif

static void release_dpb(struct dpb *dpb)
{
  if(!dpb)
    return;

  dpb_free_all(dpb);

  xine_list_delete(dpb->output_list);
  xine_list_delete(dpb->reference_list);

  free(dpb);
}

static void free_parser(struct h264_parser *parser)
{
  dpb_free_all(parser->dpb);
  release_dpb(parser->dpb);
  free_nal_buffer(parser->pps_buffer);
  free_nal_buffer(parser->sps_buffer);
  free(parser);
}

static struct h264_parser* init_parser(xine_t *xine)
{
  struct h264_parser *parser = calloc(1, sizeof(struct h264_parser));
  if (!parser)
    return NULL;

  parser->pic = create_coded_picture();
  parser->position = NON_VCL;
  parser->last_vcl_nal = NULL;
  parser->sps_buffer = create_nal_buffer(32);
  parser->pps_buffer = create_nal_buffer(32);
  parser->xine = xine;
  parser->dpb = create_dpb();

  if (!parser->dpb || !parser->pic || !parser->pps_buffer || !parser->sps_buffer) {
    free_parser(parser);
    return NULL;
  }
  return parser;
}


static void parse_codec_private(struct h264_parser *parser, const uint8_t *inbuf, int inbuf_len)
{
  struct buf_reader bufr;

  bufr.buf = inbuf;
  bufr.cur_pos = inbuf;
  bufr.cur_offset = 8;
  bufr.len = inbuf_len;

  // FIXME: Might be broken!
  struct nal_unit *nal = calloc(1, sizeof(struct nal_unit));
  if (!nal)
    return;

  /* reserved */
  read_bits(&bufr, 8);
  nal->sps.profile_idc = read_bits(&bufr, 8);
  read_bits(&bufr, 8);
  nal->sps.level_idc = read_bits(&bufr, 8);
  read_bits(&bufr, 6);

  parser->nal_size_length = read_bits(&bufr, 2) + 1;
  parser->nal_size_length_buf = calloc(1, parser->nal_size_length);
  read_bits(&bufr, 3);
  uint8_t sps_count = read_bits(&bufr, 5);

  inbuf += 6;
  inbuf_len -= 6;
  int i;

  struct coded_picture *dummy = NULL;
  for(i = 0; i < sps_count; i++) {
    uint16_t sps_size = read_bits(&bufr, 16);
    inbuf += 2;
    inbuf_len -= 2;
    parse_nal(inbuf, sps_size, parser, &dummy);
    inbuf += sps_size;
    inbuf_len -= sps_size;
  }

  bufr.buf = inbuf;
  bufr.cur_pos = inbuf;
  bufr.cur_offset = 8;
  bufr.len = inbuf_len;

  uint8_t pps_count = read_bits(&bufr, 8);
  inbuf += 1;
  for(i = 0; i < pps_count; i++) {
    uint16_t pps_size = read_bits(&bufr, 16);
    inbuf += 2;
    inbuf_len -= 2;
    parse_nal(inbuf, pps_size, parser, &dummy);
    inbuf += pps_size;
    inbuf_len -= pps_size;
  }

  nal_buffer_append(parser->sps_buffer, nal);
}

static void process_mmc_operations(struct h264_parser *parser, struct coded_picture *picture)
{
  if (picture->flag_mask & REFERENCE) {
    parser->prev_pic_order_cnt_lsb
          = picture->slc_nal->slc.pic_order_cnt_lsb;
  }

  uint32_t i;
  for(i = 0; i < picture->slc_nal->slc.
      dec_ref_pic_marking_count; i++) {
    execute_ref_pic_marking(
        picture,
        picture->slc_nal->slc.dec_ref_pic_marking[i].
        memory_management_control_operation,
        i,
        parser);
  }
}

static int parse_frame(struct h264_parser *parser, const uint8_t *inbuf, int inbuf_len,
    int64_t pts,
    const void **ret_buf, uint32_t *ret_len, struct coded_picture **ret_pic)
{
  int32_t next_nal = 0;
  int32_t offset = 0;
  int start_seq_len = 3;

  *ret_pic = NULL;
  *ret_buf = NULL;
  *ret_len = 0;

  if(parser->nal_size_length > 0)
    start_seq_len = offset = parser->nal_size_length;

  if (parser->prebuf_len + inbuf_len > MAX_FRAME_SIZE) {
    xprintf(parser->xine, XINE_VERBOSITY_LOG,"h264_parser: prebuf underrun\n");
    *ret_len = 0;
    *ret_buf = NULL;
    parser->prebuf_len = 0;
    return inbuf_len;
  }

  /* copy the whole inbuf to the prebuf,
   * then search for a nal-start sequence in the prebuf,
   * if it's in there, parse the nal and append to parser->buf
   * or return a frame */

  xine_fast_memcpy(parser->prebuf + parser->prebuf_len, inbuf, inbuf_len);
  parser->prebuf_len += inbuf_len;

  while((next_nal = seek_for_nal(parser->prebuf+start_seq_len-offset, parser->prebuf_len-start_seq_len+offset, parser)) > 0) {

    struct coded_picture *completed_pic = NULL;

    if(!parser->nal_size_length &&
        (parser->prebuf[0] != 0x00 || parser->prebuf[1] != 0x00 ||
            parser->prebuf[2] != 0x01)) {
      xprintf(parser->xine, XINE_VERBOSITY_LOG, "Broken NAL, skip it.\n");
      parser->last_nal_res = 2;
    } else {
      parser->last_nal_res = parse_nal(parser->prebuf+start_seq_len,
          next_nal, parser, &completed_pic);
    }

    if (completed_pic != NULL &&
        completed_pic->slice_cnt > 0 &&
        parser->buf_len > 0) {

      //lprintf("Frame complete: %d bytes\n", parser->buf_len);
      *ret_len = parser->buf_len;
      {
        uint8_t *rbuf = malloc(parser->buf_len);
        *ret_buf = rbuf;
        if (rbuf) xine_fast_memcpy(rbuf, parser->buf, parser->buf_len);
      }
      *ret_pic = completed_pic;

      parser->buf_len = 0;

      if (pts != 0 && (parser->pic->pts == 0 || parser->pic->pts != pts)) {
        parser->pic->pts = pts;
      }

      /**
       * if the new coded picture started with a VCL nal
       * we have to copy this to buffer for the next picture
       * now.
       */
      if(parser->last_nal_res == 1) {
        if(parser->nal_size_length > 0) {
          static const uint8_t start_seq[3] = { 0x00, 0x00, 0x01 };
          xine_fast_memcpy(parser->buf, start_seq, 3);
          parser->buf_len += 3;
        }

        xine_fast_memcpy(parser->buf+parser->buf_len, parser->prebuf+offset, next_nal+start_seq_len-2*offset);
        parser->buf_len += next_nal+start_seq_len-2*offset;
      }

      memmove(parser->prebuf, parser->prebuf+(next_nal+start_seq_len-offset), parser->prebuf_len-(next_nal+start_seq_len-offset));
      parser->prebuf_len -= next_nal+start_seq_len-offset;

      return inbuf_len;
    }

    /* got a new nal, which is part of the current
     * coded picture. add it to buf
     */
    if (parser->last_nal_res < 2) {
      if (parser->buf_len + next_nal+start_seq_len-offset > MAX_FRAME_SIZE) {
        xprintf(parser->xine, XINE_VERBOSITY_LOG, "h264_parser: buf underrun!\n");
        parser->buf_len = 0;
        *ret_len = 0;
        *ret_buf = NULL;
        return inbuf_len;
      }

      if(parser->nal_size_length > 0) {
        static const uint8_t start_seq[3] = { 0x00, 0x00, 0x01 };
        xine_fast_memcpy(parser->buf+parser->buf_len, start_seq, 3);
        parser->buf_len += 3;
      }

      xine_fast_memcpy(parser->buf+parser->buf_len, parser->prebuf+offset, next_nal+start_seq_len-2*offset);
      parser->buf_len += next_nal+start_seq_len-2*offset;

      memmove(parser->prebuf, parser->prebuf+(next_nal+start_seq_len-offset), parser->prebuf_len-(next_nal+start_seq_len-offset));
      parser->prebuf_len -= next_nal+start_seq_len-offset;
    } else {
      /* got a non-relevant nal, just remove it */
      memmove(parser->prebuf, parser->prebuf+(next_nal+start_seq_len-offset), parser->prebuf_len-(next_nal+start_seq_len-offset));
      parser->prebuf_len -= next_nal+start_seq_len-offset;
    }
  }

  if (pts != 0 && (parser->pic->pts == 0 || parser->pic->pts != pts)) {
    parser->pic->pts = pts;
  }

  *ret_buf = NULL;
  *ret_len = 0;
  return inbuf_len;
}


/**
 * @return 0: NAL is part of coded picture
 *         2: NAL is not part of coded picture
 *         1: NAL is the beginning of a new coded picture
 *         3: NAL is marked as END_OF_SEQUENCE
 */
static int parse_nal(const uint8_t *buf, int buf_len, struct h264_parser *parser,
    struct coded_picture **completed_picture)
{
  int ret = 0;

  struct buf_reader bufr;

  bufr.buf = buf;
  bufr.cur_pos = buf;
  bufr.cur_offset = 8;
  bufr.len = buf_len;

  *completed_picture = NULL;

  struct nal_unit *nal = parse_nal_header(&bufr, parser->pic, parser);

  /**
   * we detect the start of a new access unit if
   * a non-vcl nal unit is received after a vcl
   * nal unit
   * NAL_END_OF_SEQUENCE terminates the current
   * access unit
   */
  if (nal->nal_unit_type >= NAL_SLICE &&
      nal->nal_unit_type <= NAL_SLICE_IDR) {
    parser->position = VCL;
  } else if ((parser->position == VCL &&
      nal->nal_unit_type >= NAL_SEI &&
      nal->nal_unit_type <= NAL_PPS) ||
      nal->nal_unit_type == NAL_AU_DELIMITER ||
      nal->nal_unit_type == NAL_END_OF_SEQUENCE) {
    /* start of a new access unit! */
    *completed_picture = parser->pic;
    parser->pic = create_coded_picture();

    if(parser->last_vcl_nal != NULL) {
      release_nal_unit(parser->last_vcl_nal);
      parser->last_vcl_nal = NULL;
    }
    parser->position = NON_VCL;
  } else {
    parser->position = NON_VCL;
  }

  switch(nal->nal_unit_type) {
    case NAL_SPS:
      nal_buffer_append(parser->sps_buffer, nal);
      break;
    case NAL_PPS:
      nal_buffer_append(parser->pps_buffer, nal);
      break;
    case NAL_SEI: {
      if (parser->pic != NULL) {
        if(parser->pic->sei_nal) {
          release_nal_unit(parser->pic->sei_nal);
        }
        lock_nal_unit(nal);
        parser->pic->sei_nal = nal;
        interpret_sei(parser->pic);
      }
    }
    default:
      break;
  }

  /**
   * in case of an access unit which does not contain any
   * non-vcl nal units we have to detect the new access
   * unit through the algorithm for detecting first vcl nal
   * units of a primary coded picture
   */
  if (parser->position == VCL && parser->last_vcl_nal != NULL &&
      nal->nal_unit_type >= NAL_SLICE && nal->nal_unit_type <= NAL_SLICE_IDR) {
    /**
     * frame boundary detection according to
     * ITU-T Rec. H264 (11/2007) chapt 7.4.1.2.4, p65
     */
    struct nal_unit* last_nal = parser->last_vcl_nal;

    if (nal == NULL || last_nal == NULL) {
      ret = 1;
    } else if (nal->slc.frame_num != last_nal->slc.frame_num) {
      ret = 1;
    } else if (nal->slc.pic_parameter_set_id
        != last_nal->slc.pic_parameter_set_id) {
      ret = 1;
    } else if (nal->slc.field_pic_flag
        != last_nal->slc.field_pic_flag) {
      ret = 1;
    } else if (nal->slc.bottom_field_flag
        != last_nal->slc.bottom_field_flag) {
      ret = 1;
    } else if (nal->nal_ref_idc != last_nal->nal_ref_idc &&
        (nal->nal_ref_idc == 0 || last_nal->nal_ref_idc == 0)) {
      ret = 1;
    } else if (nal->sps.pic_order_cnt_type == 0
            && last_nal->sps.pic_order_cnt_type == 0
            && (nal->slc.pic_order_cnt_lsb != last_nal->slc.pic_order_cnt_lsb
                || nal->slc.delta_pic_order_cnt_bottom
                != last_nal->slc.delta_pic_order_cnt_bottom)) {
      ret = 1;
    } else if (nal->sps.pic_order_cnt_type == 1
        && last_nal->sps.pic_order_cnt_type == 1
        && (nal->slc.delta_pic_order_cnt[0]
            != last_nal->slc.delta_pic_order_cnt[0]
            || nal->slc.delta_pic_order_cnt[1]
                != last_nal->slc.delta_pic_order_cnt[1])) {
      ret = 1;
    } else if (nal->nal_unit_type != last_nal->nal_unit_type && (nal->nal_unit_type
        == NAL_SLICE_IDR || last_nal->nal_unit_type == NAL_SLICE_IDR)) {
      ret = 1;
    } else if (nal->nal_unit_type == NAL_SLICE_IDR
        && last_nal->nal_unit_type == NAL_SLICE_IDR && nal->slc.idr_pic_id
        != last_nal->slc.idr_pic_id) {
      ret = 1;
    }

    /* increase the slice_cnt until a new frame is detected */
    if (ret && *completed_picture == NULL) {
      *completed_picture = parser->pic;
      parser->pic = create_coded_picture();
    }

  } else if (nal->nal_unit_type == NAL_PPS || nal->nal_unit_type == NAL_SPS) {
    ret = 2;
  } else if (nal->nal_unit_type == NAL_AU_DELIMITER) {
    ret = 2;
  } else if (nal->nal_unit_type == NAL_END_OF_SEQUENCE) {
    ret = 3;
  } else if (nal->nal_unit_type >= NAL_SEI) {
    ret = 2;
  }

  if (parser->pic) {

    if (nal->nal_unit_type == NAL_SLICE_IDR) {
      parser->pic->flag_mask |= IDR_PIC;
    }

    /* reference flag is only set for slice NALs,
     * as PPS/SPS/SEI only references are not relevant
     * for the vdpau decoder.
     */
    if (nal->nal_ref_idc &&
        nal->nal_unit_type <= NAL_SLICE_IDR) {
      parser->pic->flag_mask |= REFERENCE;
    } else if (!nal->nal_ref_idc &&
        nal->nal_unit_type >= NAL_SLICE &&
        nal->nal_unit_type <= NAL_PART_C) {
      /* remove reference flag if a picture is not
       * continously flagged as reference. */
      parser->pic->flag_mask &= ~REFERENCE;
    }

    if (nal->nal_unit_type >= NAL_SLICE &&
        nal->nal_unit_type <= NAL_SLICE_IDR) {
      lock_nal_unit(nal);
      if(parser->last_vcl_nal) {
        release_nal_unit(parser->last_vcl_nal);
      }
      parser->last_vcl_nal = nal;

      parser->pic->slice_cnt++;
      if(parser->pic->slc_nal) {
        release_nal_unit(parser->pic->slc_nal);
      }
      lock_nal_unit(nal);
      parser->pic->slc_nal = nal;

      interpret_slice_header(parser, nal);
    }

    if (*completed_picture != NULL &&
        (*completed_picture)->slice_cnt > 0) {
      calculate_pic_order(parser, *completed_picture,
          &((*completed_picture)->slc_nal->slc));
      interpret_sps(*completed_picture, parser);
      interpret_pps(*completed_picture);
    }
  }

  release_nal_unit(nal);
  return ret;
}

static int seek_for_nal(uint8_t *buf, int buf_len, struct h264_parser *parser)
{
  if(buf_len <= 0)
    return -1;

  if(parser->nal_size_length > 0) {
    if(buf_len < parser->nal_size_length) {
      return -1;
    }

    uint32_t next_nal = parser->next_nal_position;
    if(!next_nal) {
      struct buf_reader bufr;

      bufr.buf = buf;
      bufr.cur_pos = buf;
      bufr.cur_offset = 8;
      bufr.len = buf_len;

      next_nal = read_bits(&bufr, parser->nal_size_length*8)+parser->nal_size_length;
    }

    if(next_nal > (uint32_t)buf_len) {
      parser->next_nal_position = next_nal;
      return -1;
    } else
      parser->next_nal_position = 0;

    return next_nal;
  }

  /* NAL_END_OF_SEQUENCE has only 1 byte, so
   * we do not need to search for the next start sequence */
  if(buf[0] == NAL_END_OF_SEQUENCE)
    return 1;

  int i;
  for (i = 0; i < buf_len - 2; i++) {
    if (buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x01) {
      //lprintf("found nal at: %d\n", i);
      return i;
    }
  }

  return -1;
}

/*************************************************************************
* main                                                                   *
*************************************************************************/

#define VIDEOBUFSIZE 128*1024

typedef struct vdpau_h264_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object */
  uint64_t          video_step;  /* frame duration in pts units */
  uint64_t          reported_video_step;  /* frame duration in pts units */

  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */
  double            ratio;       /* the width to height ratio */


  struct h264_parser *nal_parser;  /* h264 nal parser. extracts stream data for vdpau */

  struct decoded_picture *incomplete_pic;
  uint32_t          last_top_field_order_cnt;

  int               have_frame_boundary_marks;
  int               wait_for_frame_start;

  VdpDecoder        decoder;
  int               decoder_started;
  int               progressive_cnt; /* count of progressive marked frames in line */

  VdpDecoderProfile profile;
  vdpau_accel_t     *vdpau_accel;

  xine_t            *xine;

  struct coded_picture *completed_pic;
  vo_frame_t        *dangling_img;

  uint8_t           *codec_private;
  uint32_t          codec_private_len;

  int               vdp_runtime_nr;

  int               reset;

} vdpau_h264_decoder_t;

static void vdpau_h264_reset (video_decoder_t *this_gen);
static void vdpau_h264_flush (video_decoder_t *this_gen);

/**************************************************************************
 * vdpau_h264 specific decode functions
 *************************************************************************/

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/

#ifdef DEBUG_H264
static inline void dump_pictureinfo_h264(VdpPictureInfoH264 *pic)
{
  printf("C: slice_count: %d\n", pic->slice_count);
  printf("C: field_order_cnt[0]: %d\n", pic->field_order_cnt[0]);
  printf("C: field_order_cnt[1]: %d\n", pic->field_order_cnt[1]);
  printf("C: is_reference: %d\n", pic->is_reference);
  printf("C: frame_num: %d\n", pic->frame_num);
  printf("C: field_pic_flag: %d\n", pic->field_pic_flag);
  printf("C: bottom_field_flag: %d\n", pic->bottom_field_flag);
  printf("C: num_ref_frames: %d\n", pic->num_ref_frames);
  printf("C: mb_adaptive_frame_field_flag: %d\n", pic->mb_adaptive_frame_field_flag);
  printf("C: constrained_intra_pred_flag: %d\n", pic->constrained_intra_pred_flag);
  printf("C: weighted_pred_flag: %d\n", pic->weighted_pred_flag);
  printf("C: weighted_bipred_idc: %d\n", pic->weighted_bipred_idc);
  printf("C: frame_mbs_only_flag: %d\n", pic->frame_mbs_only_flag);
  printf("C: transform_8x8_mode_flag: %d\n", pic->transform_8x8_mode_flag);
  printf("C: chroma_qp_index_offset: %d\n", pic->chroma_qp_index_offset);
  printf("C: second_chroma_qp_index_offset: %d\n", pic->second_chroma_qp_index_offset);
  printf("C: pic_init_qp_minus26: %d\n", pic->pic_init_qp_minus26);
  printf("C: num_ref_idx_l0_active_minus1: %d\n", pic->num_ref_idx_l0_active_minus1);
  printf("C: num_ref_idx_l1_active_minus1: %d\n", pic->num_ref_idx_l1_active_minus1);
  printf("C: log2_max_frame_num_minus4: %d\n", pic->log2_max_frame_num_minus4);
  printf("C: pic_order_cnt_type: %d\n", pic->pic_order_cnt_type);
  printf("C: log2_max_pic_order_cnt_lsb_minus4: %d\n", pic->log2_max_pic_order_cnt_lsb_minus4);
  printf("C: delta_pic_order_always_zero_flag: %d\n", pic->delta_pic_order_always_zero_flag);
  printf("C: direct_8x8_inference_flag: %d\n", pic->direct_8x8_inference_flag);
  printf("C: entropy_coding_mode_flag: %d\n", pic->entropy_coding_mode_flag);
  printf("C: pic_order_present_flag: %d\n", pic->pic_order_present_flag);
  printf("C: deblocking_filter_control_present_flag: %d\n", pic->deblocking_filter_control_present_flag);
  printf("C: redundant_pic_cnt_present_flag: %d\n", pic->redundant_pic_cnt_present_flag);

  int i, j;
  for(i = 0; i < 6; i++) {
    printf("C: scalint_list4x4[%d]:\nC:", i);
    for(j = 0; j < 16; j++) {
      printf(" [%d]", pic->scaling_lists_4x4[i][j]);
      if(j%8 == 0)
        printf("\nC:");
    }
    printf("C: \n");
  }
  for(i = 0; i < 2; i++) {
    printf("C: scalint_list8x8[%d]:\nC:", i);
    for(j = 0; j < 64; j++) {
      printf(" [%d] ", pic->scaling_lists_8x8[i][j]);
      if(j%8 == 0)
        printf("\nC:");
    }
    printf("C: \n");
  }

  //int i;
  for(i = 0; i < 16; i++) {
    if(pic->referenceFrames[i].surface != VDP_INVALID_HANDLE) {
    printf("C: -------------------\n");
      printf("C: Reference Frame %d:\n", i);
    printf("C: frame_idx: %d\n", pic->referenceFrames[i].frame_idx);
    printf("C: field_order_cnt[0]: %d\n", pic->referenceFrames[i].field_order_cnt[0]);
    printf("C: field_order_cnt[1]: %d\n", pic->referenceFrames[i].field_order_cnt[0]);
    printf("C: is_long_term: %d\n", pic->referenceFrames[i].is_long_term);
    printf("C: top_is_reference: %d\n", pic->referenceFrames[i].top_is_reference);
    printf("C: bottom_is_reference: %d\n", pic->referenceFrames[i].bottom_is_reference);
    }
  }
  printf("C: ---------------------------------------------------------------\n");
  /*memcpy(pic.scaling_lists_4x4, pps->scaling_lists_4x4, 6*16);
  memcpy(pic.scaling_lists_8x8, pps->scaling_lists_8x8, 2*64);
  memcpy(pic.referenceFrames, this->reference_frames, sizeof(this->reference_frames));*/

}
#endif

static void set_ratio(video_decoder_t *this_gen)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;

  this->ratio = (double)this->width / (double)this->height;
  if(this->completed_pic->sps_nal->sps.vui_parameters.aspect_ration_info_present_flag) {
    switch(this->completed_pic->sps_nal->sps.vui_parameters.aspect_ratio_idc) {
      case ASPECT_1_1:
        this->ratio = 1 * this->ratio;
        break;
      case ASPECT_12_11:
        this->ratio *= 12.0/11.0;
        break;
      case ASPECT_10_11:
        this->ratio *= 10.0/11.0;
        break;
      case ASPECT_16_11:
        this->ratio *= 16.0/11.0;
        break;
      case ASPECT_40_33:
        this->ratio *= 40.0/33.0;
        break;
      case ASPECT_24_11:
        this->ratio *= 24.0/11.0;
        break;
      case ASPECT_20_11:
        this->ratio *= 20.0/11.0;
        break;
      case ASPECT_32_11:
        this->ratio *= 32.0/11.0;
        break;
      case ASPECT_80_33:
        this->ratio *= 80.0/33.0;
        break;
      case ASPECT_18_11:
        this->ratio *= 18.0/11.0;
        break;
      case ASPECT_15_11:
        this->ratio *= 15.0/11.0;
        break;
      case ASPECT_64_33:
        this->ratio *= 64.0/33.0;
        break;
      case ASPECT_160_99:
        this->ratio *= 160.0/99.0;
        break;
      case ASPECT_4_3:
        this->ratio *= 4.0/3.0;
        break;
      case ASPECT_3_2:
        this->ratio *= 3.0/2.0;
        break;
      case ASPECT_2_1:
        this->ratio *= 2.0/1.0;
        break;
      case ASPECT_EXTENDED_SAR:
        this->ratio *=
          (double)this->completed_pic->sps_nal->sps.vui_parameters.sar_width/
          (double)this->completed_pic->sps_nal->sps.vui_parameters.sar_height;
        break;
    }
  }
}

static void fill_vdpau_pictureinfo_h264(video_decoder_t *this_gen, uint32_t slice_count, VdpPictureInfoH264 *pic)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;

  struct pic_parameter_set_rbsp *pps = &this->completed_pic->pps_nal->pps;
  struct seq_parameter_set_rbsp *sps = &this->completed_pic->sps_nal->sps;
  struct slice_header *slc = &this->completed_pic->slc_nal->slc;

  pic->slice_count = slice_count;
  pic->field_order_cnt[0] = this->completed_pic->top_field_order_cnt;
  pic->field_order_cnt[1] = this->completed_pic->bottom_field_order_cnt;
  pic->is_reference =
    (this->completed_pic->flag_mask & REFERENCE) ? VDP_TRUE : VDP_FALSE;
  pic->frame_num = slc->frame_num;
  pic->field_pic_flag = slc->field_pic_flag;
  pic->bottom_field_flag = slc->bottom_field_flag;
  pic->num_ref_frames = sps->num_ref_frames;
  pic->mb_adaptive_frame_field_flag = sps->mb_adaptive_frame_field_flag && !slc->field_pic_flag;
  pic->constrained_intra_pred_flag = pps->constrained_intra_pred_flag;
  pic->weighted_pred_flag = pps->weighted_pred_flag;
  pic->weighted_bipred_idc = pps->weighted_bipred_idc;
  pic->frame_mbs_only_flag = sps->frame_mbs_only_flag;
  pic->transform_8x8_mode_flag = pps->transform_8x8_mode_flag;
  pic->chroma_qp_index_offset = pps->chroma_qp_index_offset;
  pic->second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset;
  pic->pic_init_qp_minus26 = pps->pic_init_qp_minus26;
  pic->num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_active_minus1;
  pic->num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_active_minus1;
  pic->log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4;
  pic->pic_order_cnt_type = sps->pic_order_cnt_type;
  pic->log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4;
  pic->delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag;
  pic->direct_8x8_inference_flag = sps->direct_8x8_inference_flag;
  pic->entropy_coding_mode_flag = pps->entropy_coding_mode_flag;
  pic->pic_order_present_flag = pps->pic_order_present_flag;
  pic->deblocking_filter_control_present_flag = pps->deblocking_filter_control_present_flag;
  pic->redundant_pic_cnt_present_flag = pps->redundant_pic_cnt_present_flag;

  memcpy(pic->scaling_lists_4x4, pps->scaling_lists_4x4, sizeof(pic->scaling_lists_4x4));
  memcpy(pic->scaling_lists_8x8, pps->scaling_lists_8x8, sizeof(pic->scaling_lists_8x8));

  /* set num_ref_frames to the number of actually available reference frames,
   * if this is not set generation 3 decoders will fail. */
  /*pic->num_ref_frames =*/
  fill_vdpau_reference_list(this->nal_parser->dpb, pic->referenceFrames);

}

static int check_progressive(video_decoder_t *this_gen, struct decoded_picture *dpic)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;
  int progressive = 0;
  int i;

  for(i = 0; i < 2; i++) {
    struct coded_picture *pic = dpic->coded_pic[i];
    if (!pic) {
      continue;
    }

    if (pic->flag_mask & PIC_STRUCT_PRESENT && pic->sei_nal != NULL) {
      uint8_t pic_struct = pic->sei_nal->sei.pic_timing.pic_struct;

      if (pic_struct == DISP_FRAME) {
        progressive = 1;
        continue;
      } else if (pic_struct == DISP_TOP_BOTTOM ||
          pic_struct == DISP_BOTTOM_TOP) {
        progressive = 0;
        break;
      }

      /* FIXME: seems unreliable, maybe it's has to be interpreted more complex */
      /*if (pic->sei_nal->sei.pic_timing.ct_type == CT_INTERLACED) {
        return 0;
      } else if (pic->sei_nal->sei.pic_timing.ct_type == CT_PROGRESSIVE) {
        return 1;
      } */
    }

    if (pic->slc_nal->slc.field_pic_flag && pic->pps_nal->pps.pic_order_present_flag) {
      if(pic->slc_nal->slc.delta_pic_order_cnt_bottom == 1 ||
          pic->slc_nal->slc.delta_pic_order_cnt_bottom == -1) {
        progressive = 0;
        break;
      } else {
        progressive = 1;
        continue;
      }
    }
    if (!pic->slc_nal->slc.field_pic_flag && pic->sps_nal->sps.frame_mbs_only_flag) {
      progressive = 1;
      continue;
    }
  }

  if (progressive) {
    this->progressive_cnt++;
  } else {
    this->progressive_cnt = 0;
  }

  /* only switch to progressive mode if at least 5
   * frames in order were marked as progressive */
  return (this->progressive_cnt >= 5);
}

static int vdpau_decoder_init(video_decoder_t *this_gen)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;
  vo_frame_t *img;

  if(this->width == 0) {
    this->width = this->completed_pic->sps_nal->sps.pic_width;
    this->height = this->completed_pic->sps_nal->sps.pic_height;
  }

  set_ratio(this_gen);

  _x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->width );
  _x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height );
  _x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*this->ratio) );
  _x_stream_info_set( this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = this->video_step) );
  _x_meta_info_set_utf8( this->stream, XINE_META_INFO_VIDEOCODEC, "H264/AVC (vdpau)" );
  xine_event_t event;
  xine_format_change_data_t data;
  event.type = XINE_EVENT_FRAME_FORMAT_CHANGE;
  event.stream = this->stream;
  event.data = &data;
  event.data_length = sizeof(data);
  data.width = this->width;
  data.height = this->height;
  data.aspect = this->ratio;
  xine_event_send( this->stream, &event );

  switch(this->completed_pic->sps_nal->sps.profile_idc) {
    case 100:
      this->profile = VDP_DECODER_PROFILE_H264_HIGH;
      break;
    case 77:
      this->profile = VDP_DECODER_PROFILE_H264_MAIN;
      break;
    case 66:
    default:
      // nvidia's VDPAU doesn't support BASELINE. But most (every?) streams marked BASELINE do not use BASELINE specifics,
      // so, just force MAIN.
      //this->profile = VDP_DECODER_PROFILE_H264_BASELINE;
      this->profile = VDP_DECODER_PROFILE_H264_MAIN;
      break;
  }

  // Level 4.1 limits:
  int ref_frames = 0;
  if(this->completed_pic->sps_nal->sps.num_ref_frames) {
    ref_frames = this->completed_pic->sps_nal->sps.num_ref_frames;
  } else {
    uint32_t round_width = (this->width + 15) & ~15;
    uint32_t round_height = (this->height + 15) & ~15;
    uint32_t surf_size = (round_width * round_height * 3) / 2;
    ref_frames = (12 * 1024 * 1024) / surf_size;
  }

  if (ref_frames > 16) {
      ref_frames = 16;
  }

  xprintf(this->xine, XINE_VERBOSITY_LOG, "Allocate %d reference frames\n",
      ref_frames);
  /* get the vdpau context from vo */
  //(this->stream->video_out->open) (this->stream->video_out, this->stream);
  img = this->stream->video_out->get_frame (this->stream->video_out,
                                    this->width, this->height,
                                    this->ratio,
                                    XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS | this->reset);
  this->reset = 0;                                    

  this->vdpau_accel = (vdpau_accel_t*)img->accel_data;

  img->free(img);
  img = NULL;

  /*VdpBool is_supported;
  uint32_t max_level, max_references, max_width, max_height;*/
  if(this->vdpau_accel->vdp_runtime_nr > 0) {
   xprintf(this->xine, XINE_VERBOSITY_LOG,
       "Create decoder: vdp_device: %d, profile: %d, res: %dx%d\n",
       this->vdpau_accel->vdp_device, this->profile, this->width, this->height);

   VdpStatus status;
   if (this->vdpau_accel->lock)
    this->vdpau_accel->lock (this->vdpau_accel->vo_frame);
   status = this->vdpau_accel->vdp_decoder_create(this->vdpau_accel->vdp_device,
       this->profile, this->width, this->height, 16, &this->decoder);
   if (this->vdpau_accel->unlock)
    this->vdpau_accel->unlock (this->vdpau_accel->vo_frame);

   if(status != VDP_STATUS_OK) {
     xprintf(this->xine, XINE_VERBOSITY_LOG, "vdpau_h264: ERROR: VdpDecoderCreate returned status != OK (%s)\n", this->vdpau_accel->vdp_get_error_string(status));
     return 0;
   }
  }
  return 1;
}

static void draw_frames(video_decoder_t *this_gen, int flush)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;

  struct decoded_picture *decoded_pic = NULL;
  while ((decoded_pic = dpb_get_next_out_picture(this->nal_parser->dpb, flush)) != NULL) {
    decoded_pic->img->top_field_first = dp_top_field_first(decoded_pic);
    decoded_pic->img->progressive_frame = check_progressive(this_gen, decoded_pic);
#ifdef DEBUG_H264
    printf("progressive: %d\n", decoded_pic->img->progressive_frame);
#endif
    if (flush) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG,
          "h264 flush, draw pts: %"PRId64"\n", decoded_pic->img->pts);
    }

    decoded_pic->img->draw(decoded_pic->img, this->stream);
    dpb_unmark_picture_delayed(this->nal_parser->dpb, decoded_pic);
    decoded_pic = NULL;
  }
}

static int vdpau_decoder_render(video_decoder_t *this_gen, VdpBitstreamBuffer *vdp_buffer, uint32_t slice_count)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;
  vo_frame_t *img = NULL;

  /* if we wait for a second field for this frame, we
   * have to render to the same surface again.
   */
  if (this->incomplete_pic) {
    img = this->incomplete_pic->img;
  }

  // FIXME: what is if this is the second field of a field coded
  // picture? - should we keep the first field in dpb?
  if(this->completed_pic->flag_mask & IDR_PIC) {
    dpb_flush(this->nal_parser->dpb);
    if(this->incomplete_pic) {
      release_decoded_picture(this->incomplete_pic);
      this->incomplete_pic = NULL;
    }
  }

  struct seq_parameter_set_rbsp *sps = &this->completed_pic->sps_nal->sps;
  struct slice_header *slc = &this->completed_pic->slc_nal->slc;

  if ((this->video_step == 0) &&
       sps->vui_parameters_present_flag &&
       sps->vui_parameters.timing_info_present_flag &&
       sps->vui_parameters.time_scale) {
    /* good: 2 * 1001 / 48000. */
    this->video_step = (uint64_t)90000 * 2
                     * sps->vui_parameters.num_units_in_tick / sps->vui_parameters.time_scale;
    if (this->video_step < 90) {
      /* bad: 2 * 1 / 60000. seen this once from broken h.264 video usability info (VUI).
       * VAAPI seems to apply a similar HACK.*/
      this->video_step = (uint64_t)90000000 * 2
                       * sps->vui_parameters.num_units_in_tick / sps->vui_parameters.time_scale;
    }
  }

  /* go and decode a frame */

  /* check if we expect a second field, but got a frame */
  if (this->incomplete_pic && img) {
    if ((this->completed_pic->slc_nal->slc.frame_num !=
        this->incomplete_pic->coded_pic[0]->slc_nal->slc.frame_num) ||
        !slc->field_pic_flag) {
      xprintf(this->xine, XINE_VERBOSITY_DEBUG, "H264 warning: Expected a second field, stream might be broken\n");

      /* remove this pic from dpb, as it is not complete */
      dpb_unmark_picture_delayed(this->nal_parser->dpb, this->incomplete_pic);
      dpb_unmark_reference_picture(this->nal_parser->dpb, this->incomplete_pic);

      release_decoded_picture(this->incomplete_pic);
      this->incomplete_pic = NULL;
      img = NULL;
    }
  }


  VdpPictureInfoH264 pic;

  fill_vdpau_pictureinfo_h264(this_gen, slice_count, &pic);

#ifdef DEBUG_H264
  dump_pictureinfo_h264(&pic);

  int i;
  printf("E: Bytes used: %d\n", vdp_buffer->bitstream_bytes);
  printf("E: Decode data: \nE:");
  for(i = 0; i < ((vdp_buffer->bitstream_bytes < 20) ? vdp_buffer->bitstream_bytes : 20); i++) {
    printf("%02x ", ((uint8_t*)vdp_buffer->bitstream)[i]);
    if((i+1) % 10 == 0)
      printf("\nE:");
  }
  printf("\n...\n");
  for(i = vdp_buffer->bitstream_bytes - 20; i < vdp_buffer->bitstream_bytes; i++) {
    printf("%02x ", ((uint8_t*)vdp_buffer->bitstream)[i]);
    if((i+1) % 10 == 0)
      printf("\nE:");
  }
  printf("\nE: ---------------------------------------------------------------\n");
#endif

  if(!this->decoder_started && !pic.is_reference)
    return 0;

  this->decoder_started = 1;

  if(img == NULL) {
    int frame_flags = VO_BOTH_FIELDS;
    int color_matrix = 4; /* undefined, mpeg range */
    if (sps->vui_parameters.video_signal_type_present_flag) {
      if (sps->vui_parameters.colour_description_present)
        color_matrix = sps->vui_parameters.matrix_coefficients << 1;
      color_matrix |= sps->vui_parameters.video_full_range_flag;
    }
    VO_SET_FLAGS_CM (color_matrix, frame_flags);

    img = this->stream->video_out->get_frame (this->stream->video_out,
                                              this->width, this->height,
                                              this->ratio,
                                              XINE_IMGFMT_VDPAU, frame_flags);
    this->vdpau_accel = (vdpau_accel_t*)img->accel_data;

    img->duration  = this->video_step;
    img->pts       = this->completed_pic->pts;

    if (this->dangling_img) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
          "broken stream: current img wasn't processed -- freeing it!\n");
      this->dangling_img->free(this->dangling_img);
    }
    this->dangling_img = img;
  } else {
    if (img->pts == 0) {
      img->pts = this->completed_pic->pts;
    }
  }

  if(this->vdp_runtime_nr != *(this->vdpau_accel->current_vdp_runtime_nr)) {
    xprintf(this->xine, XINE_VERBOSITY_LOG,
        "VDPAU was preempted. Reinitialise the decoder.\n");
    this->decoder = VDP_INVALID_HANDLE;
    vdpau_h264_reset(this_gen);
    this->vdp_runtime_nr = this->vdpau_accel->vdp_runtime_nr;
    return 0;
  }

  VdpVideoSurface surface = this->vdpau_accel->surface;

  /*xprintf(this->xine, XINE_VERBOSITY_DEBUG,
      "Decode: NUM: %d, REF: %d, BYTES: %d, PTS: %lld\n", pic.frame_num, pic.is_reference, vdp_buffer->bitstream_bytes, this->completed_pic->pts);*/
  if (this->vdpau_accel->lock)
    this->vdpau_accel->lock (this->vdpau_accel->vo_frame);
  VdpStatus status = this->vdpau_accel->vdp_decoder_render(this->decoder,
      surface, CAST_VdpPictureInfo_PTR &pic, 1, vdp_buffer);
  if (this->vdpau_accel->unlock)
    this->vdpau_accel->unlock (this->vdpau_accel->vo_frame);

  /* free the image data */
  if(((uint8_t*)vdp_buffer->bitstream) != NULL) {
    free((uint8_t*)vdp_buffer->bitstream);
  }

  process_mmc_operations(this->nal_parser, this->completed_pic);

  if(status != VDP_STATUS_OK)
  {
    xprintf(this->xine, XINE_VERBOSITY_LOG, "vdpau_h264: Decoder failure: %s\n",  this->vdpau_accel->vdp_get_error_string(status));
    if (this->dangling_img)
      this->dangling_img->free(this->dangling_img);
    img = NULL;
    this->dangling_img = NULL;
    free_coded_picture(this->completed_pic);
    this->completed_pic = NULL;
  }
  else {
    img->bad_frame = 0;

    if(!img->progressive_frame && this->completed_pic->repeat_pic)
      img->repeat_first_field = 1;
    //else if(img->progressive_frame && this->nal_parser->current_nal->repeat_pic)
    //  img->duration *= this->nal_parser->current_nal->repeat_pic;

    struct decoded_picture *decoded_pic = NULL;


    uint8_t draw_frame = 0;
    if (!slc->field_pic_flag) { /* frame coded: simply add to dpb */
      decoded_pic = init_decoded_picture(this->completed_pic, img);
      this->completed_pic = NULL;
      this->dangling_img = NULL;

      dpb_add_picture(this->nal_parser->dpb, decoded_pic, sps->num_ref_frames);

      draw_frame = 1;
    } else { /* field coded: check for second field */
      if (!this->incomplete_pic) {
        decoded_pic = init_decoded_picture(this->completed_pic, img);
        this->completed_pic = NULL;
        this->dangling_img = NULL;
        this->incomplete_pic = decoded_pic;
        lock_decoded_picture(this->incomplete_pic);

        dpb_add_picture(this->nal_parser->dpb, decoded_pic, sps->num_ref_frames);

        /* don't do a draw yet as the field was incomplete */
        draw_frame = 0;
      } else {
        decoded_pic = this->incomplete_pic;
        lock_decoded_picture(decoded_pic);

        /* picture is complete now */
        release_decoded_picture(this->incomplete_pic);
        this->incomplete_pic = NULL;
        this->dangling_img = NULL;

        decoded_pic_add_field(decoded_pic, this->completed_pic);
        this->completed_pic = NULL;

        draw_frame = 1;
      }
    }

    release_decoded_picture(decoded_pic);

    /* draw the next frame in display order */
    if (draw_frame) {
      draw_frames(this_gen, 0);
    }
  }

  return 1;
}

/*
 * This function receives a buffer of data from the demuxer layer and
 * figures out how to handle it based on its header flags.
 */
static void vdpau_h264_decode_data (video_decoder_t *this_gen,
  buf_element_t *buf) {

  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *) this_gen;

  VdpBitstreamBuffer vdp_buffer;
  vdp_buffer.struct_version = VDP_BITSTREAM_BUFFER_VERSION;

  /* a video decoder does not care about this flag (?) */
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if(buf->decoder_flags & BUF_FLAG_FRAME_START || buf->decoder_flags & BUF_FLAG_FRAME_END)
    this->have_frame_boundary_marks = 1;

  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
  }

  if (this->video_step != this->reported_video_step){
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, (this->reported_video_step = this->video_step));
  }
  
  if (buf->decoder_flags & BUF_FLAG_STDHEADER) { /* need to initialize */
    this->have_frame_boundary_marks = 0;

    xine_bmiheader *bih = (xine_bmiheader*)buf->content;
    this->width                         = bih->biWidth;
    this->height                        = bih->biHeight;

    uint8_t *codec_private = buf->content + sizeof(xine_bmiheader);
    uint32_t codec_private_len = bih->biSize - sizeof(xine_bmiheader);
    this->codec_private_len = codec_private_len;
    this->codec_private = malloc(codec_private_len);
    memcpy(this->codec_private, codec_private, codec_private_len);

    if(codec_private_len > 0) {
      parse_codec_private(this->nal_parser, codec_private, codec_private_len);
    }
  } else if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
    this->have_frame_boundary_marks = 0;

    if(buf->decoder_info[1] == BUF_SPECIAL_DECODER_CONFIG) {
      const uint8_t *codec_private = buf->decoder_info_ptr[2];
      uint32_t codec_private_len = buf->decoder_info[2];
      this->codec_private_len = codec_private_len;
      this->codec_private = malloc(codec_private_len);
      memcpy(this->codec_private, codec_private, codec_private_len);

      if(codec_private_len > 0) {
        parse_codec_private(this->nal_parser, codec_private, codec_private_len);
      }
    } else if (buf->decoder_info[1] == BUF_SPECIAL_PALETTE) {
      xprintf(this->xine, XINE_VERBOSITY_LOG,
          "SPECIAL PALETTE is not yet handled\n");
    } else
      xprintf(this->xine, XINE_VERBOSITY_LOG,
          "UNKNOWN SPECIAL HEADER\n");

  } else {
    /* parse the first nal packages to retrieve profile type */
    int len = 0;

    while(len < buf->size && !(this->wait_for_frame_start && !(buf->decoder_flags & BUF_FLAG_FRAME_START))) {
      this->wait_for_frame_start = 0;
      len += parse_frame(this->nal_parser, buf->content + len, buf->size - len,
          buf->pts,
          &vdp_buffer.bitstream, &vdp_buffer.bitstream_bytes, &this->completed_pic);

      if(this->decoder == VDP_INVALID_HANDLE &&
          this->completed_pic &&
          this->completed_pic->sps_nal != NULL &&
          this->completed_pic->sps_nal->sps.pic_width > 0 &&
          this->completed_pic->sps_nal->sps.pic_height > 0) {

        vdpau_decoder_init(this_gen);
      }

      if(this->completed_pic &&
          this->completed_pic->sps_nal != NULL &&
          this->completed_pic->sps_nal->sps.vui_parameters_present_flag &&
          this->completed_pic->sps_nal->sps.vui_parameters.bitstream_restriction_flag) {

        this->nal_parser->dpb->max_reorder_frames =
            this->completed_pic->sps_nal->sps.vui_parameters.num_reorder_frames + 1;
        this->nal_parser->dpb->max_dpb_frames = this->completed_pic->sps_nal->sps.vui_parameters.max_dec_frame_buffering + 1;

        xprintf(this->xine, XINE_VERBOSITY_DEBUG,
                    "max reorder count: %d, max dpb count %d\n",
                    this->nal_parser->dpb->max_reorder_frames,
                    this->nal_parser->dpb->max_dpb_frames);
      }

      if(this->decoder != VDP_INVALID_HANDLE &&
          vdp_buffer.bitstream_bytes > 0 &&
          this->completed_pic->slc_nal != NULL &&
          this->completed_pic->pps_nal != NULL) {
        vdpau_decoder_render(this_gen, &vdp_buffer, this->completed_pic->slice_cnt);
      } else if (this->completed_pic != NULL) {
        free_coded_picture(this->completed_pic);
      }

      /* in case the last nal was detected as END_OF_SEQUENCE
       * we will flush the dpb, so that all pictures get drawn
       */
      if(this->nal_parser->last_nal_res == 3) {
        xprintf(this->xine, XINE_VERBOSITY_DEBUG,
            "END_OF_SEQUENCE, flush buffers\n");
        vdpau_h264_flush(this_gen);
      }
    }
  }

  if(buf->decoder_flags & BUF_FLAG_FRAME_END)
    this->wait_for_frame_start = 0;
}

/*
 * This function is called when xine needs to flush the system.
 */
static void vdpau_h264_flush (video_decoder_t *this_gen) {
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t*) this_gen;
  //struct decoded_picture *decoded_pic = NULL;

  if(this->dangling_img){
    this->dangling_img->free(this->dangling_img);
    this->dangling_img = NULL;
  }

  if (this->incomplete_pic) {
    release_decoded_picture(this->incomplete_pic);
    this->incomplete_pic = NULL;
  }

  draw_frames(this_gen, 1);
  dpb_free_all(this->nal_parser->dpb);
  this->reset = VO_NEW_SEQUENCE_FLAG;
}

/*
 * This function resets the video decoder.
 */
static void vdpau_h264_reset (video_decoder_t *this_gen) {
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *) this_gen;

  dpb_free_all(this->nal_parser->dpb);

  if (this->decoder != VDP_INVALID_HANDLE) {
    if (this->vdpau_accel->lock)
      this->vdpau_accel->lock (this->vdpau_accel->vo_frame);
    this->vdpau_accel->vdp_decoder_destroy( this->decoder );
    this->decoder = VDP_INVALID_HANDLE;
    if (this->vdpau_accel->unlock)
      this->vdpau_accel->unlock (this->vdpau_accel->vo_frame);
  }

  // Doing a full parser reinit here works more reliable than
  // resetting

  //reset_parser(this->nal_parser);
  free_parser(this->nal_parser);
  this->nal_parser = init_parser(this->xine);

  this->video_step = 0;

  if(this->codec_private_len > 0) {
    parse_codec_private(this->nal_parser, this->codec_private, this->codec_private_len);

    /* if the stream does not contain frame boundary marks we
     * have to hope that the next nal will start with the next
     * incoming buf... seems to work, though...
     */
    this->wait_for_frame_start = this->have_frame_boundary_marks;
  }

  if (this->incomplete_pic) {
    release_decoded_picture(this->incomplete_pic);
    this->incomplete_pic = NULL;
  }

  if (this->dangling_img) {
    this->dangling_img->free(this->dangling_img);
    this->dangling_img = NULL;
  }

  this->progressive_cnt = 0;
  this->reset = VO_NEW_SEQUENCE_FLAG;
}

/*
 * The decoder should forget any stored pts values here.
 */
static void vdpau_h264_discontinuity (video_decoder_t *this_gen) {
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *) this_gen;

  dpb_clear_all_pts(this->nal_parser->dpb);
  this->reset = VO_NEW_SEQUENCE_FLAG;
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void vdpau_h264_dispose (video_decoder_t *this_gen) {

  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *) this_gen;

  if (this->incomplete_pic) {
    release_decoded_picture(this->incomplete_pic);
    this->incomplete_pic = NULL;
  }

  if (this->dangling_img) {
    this->dangling_img->free(this->dangling_img);
    this->dangling_img = NULL;
  }

  dpb_free_all(this->nal_parser->dpb);

  if (this->decoder != VDP_INVALID_HANDLE) {
    if (this->vdpau_accel->lock)
      this->vdpau_accel->lock (this->vdpau_accel->vo_frame);
    this->vdpau_accel->vdp_decoder_destroy( this->decoder );
    this->decoder = VDP_INVALID_HANDLE;
    if (this->vdpau_accel->unlock)
      this->vdpau_accel->unlock (this->vdpau_accel->vo_frame);
  }

  this->stream->video_out->close( this->stream->video_out, this->stream );

  free_parser (this->nal_parser);
  free (this_gen);
}

/*
 * This function allocates, initializes, and returns a private video
 * decoder structure.
 */
static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  vdpau_h264_decoder_t  *this ;

  (void)class_gen;

  /* the videoout must be vdpau-capable to support this decoder */
  if ( !(stream->video_out->get_capabilities(stream->video_out) & VO_CAP_VDPAU_H264) )
	  return NULL;

  /* now check if vdpau has free decoder resource */
  vo_frame_t *img = stream->video_out->get_frame( stream->video_out, 1920, 1080, 1, XINE_IMGFMT_VDPAU,
                                                  VO_BOTH_FIELDS | VO_GET_FRAME_MAY_FAIL );
  if (!img) {
    return NULL;
  }
  vdpau_accel_t *accel = (vdpau_accel_t*)img->accel_data;
  int runtime_nr = accel->vdp_runtime_nr;
  img->free(img);
  VdpDecoder decoder;
  if (accel->lock)
    accel->lock (accel->vo_frame);
  VdpStatus st = accel->vdp_decoder_create( accel->vdp_device, VDP_DECODER_PROFILE_H264_MAIN, 1920, 1080, 16, &decoder );
  if (accel->unlock)
    accel->unlock (accel->vo_frame);
  if ( st!=VDP_STATUS_OK ) {
    lprintf( "can't create vdpau decoder.\n" );
    return NULL;
  }

  if (accel->lock)
    accel->lock (accel->vo_frame);
  accel->vdp_decoder_destroy( decoder );
  if (accel->unlock)
    accel->unlock (accel->vo_frame);

  this = (vdpau_h264_decoder_t *) calloc(1, sizeof(vdpau_h264_decoder_t));
  if (!this)
    return NULL;
  this->nal_parser = init_parser(stream->xine);
  if (!this->nal_parser) {
    free(this);
    return NULL;
  }
  this->video_decoder.decode_data         = vdpau_h264_decode_data;
  this->video_decoder.flush               = vdpau_h264_flush;
  this->video_decoder.reset               = vdpau_h264_reset;
  this->video_decoder.discontinuity       = vdpau_h264_discontinuity;
  this->video_decoder.dispose             = vdpau_h264_dispose;

  this->stream                            = stream;
  this->xine                              = stream->xine;

  this->decoder                           = VDP_INVALID_HANDLE;
  this->vdp_runtime_nr                    = runtime_nr;
  this->progressive_cnt                   = 0;

  this->reset = VO_NEW_SEQUENCE_FLAG;

  (this->stream->video_out->open) (this->stream->video_out, this->stream);

  return &this->video_decoder;
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
void *h264_init_plugin (xine_t *xine, const void *data) {

  (void)xine;
  (void)data;

  static const video_decoder_class_t decode_video_vdpau_h264_class = {
    .open_plugin     = open_plugin,
    .identifier      = "vdpau_h264",
    .description     =
        N_("vdpau_h264: h264 decoder plugin using VDPAU hardware decoding.\n"
           "Must be used along with video_out_vdpau."),
    .dispose         = NULL,
  };

  return (void *)&decode_video_vdpau_h264_class;
}
