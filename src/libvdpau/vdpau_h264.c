/*
 * Copyright (C) 2008 Julian Scheel
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <vdpau/vdpau.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "xineutils.h"
#include "bswap.h"
#include "accel_vdpau.h"
#include "h264_parser.h"
#include "dpb.h"

#define VIDEOBUFSIZE 128*1024

typedef struct {
  video_decoder_class_t   decoder_class;
} vdpau_h264_class_t;

typedef struct vdpau_h264_decoder_s {
  video_decoder_t   video_decoder;  /* parent video decoder structure */

  vdpau_h264_class_t *class;
  xine_stream_t    *stream;

  /* these are traditional variables in a video decoder object */
  uint64_t          video_step;  /* frame duration in pts units */

  int               width;       /* the width of a video frame */
  int               height;      /* the height of a video frame */
  double            ratio;       /* the width to height ratio */


  struct nal_parser *nal_parser;  /* h264 nal parser. extracts stream data for vdpau */
  uint8_t           wait_for_bottom_field;
  struct decoded_picture *last_ref_pic;
  uint32_t          last_top_field_order_cnt;

  int               have_frame_boundary_marks;
  int               wait_for_frame_start;

  VdpDecoder        decoder;
  int               decoder_started;

  VdpColorStandard  color_standard;
  VdpDecoderProfile profile;
  vdpau_accel_t     *vdpau_accel;

  xine_t            *xine;

  int64_t           curr_pts;
  int64_t           next_pts;

  vo_frame_t        *last_img;
  vo_frame_t        *dangling_img;

  uint8_t           *codec_private;
  uint32_t          codec_private_len;

  int               vdp_runtime_nr;

} vdpau_h264_decoder_t;

static void vdpau_h264_reset (video_decoder_t *this_gen);

/**************************************************************************
 * vdpau_h264 specific decode functions
 *************************************************************************/

/**************************************************************************
 * xine video plugin functions
 *************************************************************************/


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

static void set_ratio(video_decoder_t *this_gen)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;

  this->ratio = (double)this->width / (double)this->height;
  if(this->nal_parser->current_nal->sps->vui_parameters.aspect_ration_info_present_flag) {
    switch(this->nal_parser->current_nal->sps->vui_parameters.aspect_ratio_idc) {
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
          (double)this->nal_parser->current_nal->sps->vui_parameters.sar_width/
          (double)this->nal_parser->current_nal->sps->vui_parameters.sar_height;
        break;
    }
  }
}

static void fill_vdpau_pictureinfo_h264(video_decoder_t *this_gen, uint32_t slice_count, VdpPictureInfoH264 *pic)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;

  struct pic_parameter_set_rbsp *pps = this->nal_parser->current_nal->pps;
  struct seq_parameter_set_rbsp *sps = this->nal_parser->current_nal->sps;
  struct slice_header *slc = this->nal_parser->current_nal->slc;

  pic->slice_count = slice_count;
  pic->field_order_cnt[0] = this->nal_parser->current_nal->top_field_order_cnt;
  pic->field_order_cnt[1] = this->nal_parser->current_nal->bottom_field_order_cnt;
  pic->is_reference =
    (this->nal_parser->current_nal->nal_ref_idc != 0) ? VDP_TRUE : VDP_FALSE;
  pic->frame_num = slc->frame_num;
  pic->field_pic_flag = slc->field_pic_flag;
  pic->bottom_field_flag = slc->bottom_field_flag;
  pic->num_ref_frames = sps->num_ref_frames;
  pic->mb_adaptive_frame_field_flag = sps->mb_adaptive_frame_field_flag;
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
  fill_vdpau_reference_list(&(this->nal_parser->dpb), pic->referenceFrames);

}

static int vdpau_decoder_init(video_decoder_t *this_gen)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;
  vo_frame_t *img;

  this->curr_pts = this->next_pts;
  this->next_pts = 0;

  if(this->width == 0) {
    this->width = this->nal_parser->current_nal->sps->pic_width;
    this->height = this->nal_parser->current_nal->sps->pic_height;
  }

  set_ratio(this_gen);

  _x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->width );
  _x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height );
  _x_stream_info_set( this->stream, XINE_STREAM_INFO_VIDEO_RATIO, ((double)10000*this->ratio) );
  _x_stream_info_set( this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step );
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

  switch(this->nal_parser->current_nal->sps->profile_idc) {
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
  if(this->nal_parser->current_nal->sps->num_ref_frames) {
    ref_frames = this->nal_parser->current_nal->sps->num_ref_frames;
  } else {
    uint32_t round_width = (this->width + 15) & ~15;
    uint32_t round_height = (this->height + 15) & ~15;
    uint32_t surf_size = (round_width * round_height * 3) / 2;
    ref_frames = (12 * 1024 * 1024) / surf_size;
  }

  if (ref_frames > 16) {
      ref_frames = 16;
  }

  printf("Allocate %d reference frames\n", ref_frames);
  /* get the vdpau context from vo */
  //(this->stream->video_out->open) (this->stream->video_out, this->stream);
  img = this->stream->video_out->get_frame (this->stream->video_out,
                                    this->width, this->height,
                                    this->ratio,
                                    XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS);

  img->duration = this->video_step;
  img->pts = this->curr_pts;

  if (this->dangling_img) {
    fprintf(stderr, "broken stream: current img wasn't processed -- freeing it\n!");
    this->dangling_img->free(this->dangling_img);
  }
  this->dangling_img = img;
  this->last_img = img;

  this->vdpau_accel = (vdpau_accel_t*)img->accel_data;

  /*VdpBool is_supported;
  uint32_t max_level, max_references, max_width, max_height;*/
  if(this->vdpau_accel->vdp_runtime_nr > 0) {
   xprintf(this->xine, XINE_VERBOSITY_LOG,
       "Create decoder: vdp_device: %d, profile: %d, res: %dx%d\n",
       this->vdpau_accel->vdp_device, this->profile, this->width, this->height);

   VdpStatus status = this->vdpau_accel->vdp_decoder_create(this->vdpau_accel->vdp_device,
       this->profile, this->width, this->height, 16, &this->decoder);

   if(status != VDP_STATUS_OK) {
     xprintf(this->xine, XINE_VERBOSITY_LOG, "vdpau_h264: ERROR: VdpDecoderCreate returned status != OK (%s)\n", this->vdpau_accel->vdp_get_error_string(status));
     return 0;
   }
  }
  return 1;
}

static int vdpau_decoder_render(video_decoder_t *this_gen, VdpBitstreamBuffer *vdp_buffer, uint32_t slice_count)
{
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *)this_gen;
  vo_frame_t *img = this->last_img;

  if(this->nal_parser->current_nal->nal_unit_type == NAL_SLICE_IDR) {
    dpb_flush(&(this->nal_parser->dpb));
  }

  VdpPictureInfoH264 pic;

  fill_vdpau_pictureinfo_h264(this_gen, slice_count, &pic);

  //printf("next decode: %d, %d\n", pic.field_order_cnt[0], pic.field_order_cnt[1]);

  if(!this->decoder_started && !pic.is_reference)
    return 0;

  this->decoder_started = 1;

  struct seq_parameter_set_rbsp *sps = this->nal_parser->current_nal->sps;
  struct slice_header *slc = this->nal_parser->current_nal->slc;

  if(sps->vui_parameters_present_flag &&
      sps->vui_parameters.timing_info_present_flag &&
      this->video_step == 0) {
    this->video_step = 2*90000/(1/((double)sps->vui_parameters.num_units_in_tick/(double)sps->vui_parameters.time_scale));
  }

  /* flush the DPB if this frame was an IDR */
  //printf("is_idr: %d\n", this->nal_parser->is_idr);
  this->nal_parser->is_idr = 0;

  /* go and decode a frame */

  //dump_pictureinfo_h264(&pic);

  /*int i;
  printf("Decode data: \n");
  for(i = 0; i < ((vdp_buffer->bitstream_bytes < 20) ? vdp_buffer->bitstream_bytes : 20); i++) {
    printf("%02x ", ((uint8_t*)vdp_buffer->bitstream)[i]);
    if((i+1) % 10 == 0)
      printf("\n");
  }
  printf("\n...\n");
  for(i = vdp_buffer->bitstream_bytes - 20; i < vdp_buffer->bitstream_bytes; i++) {
    printf("%02x ", ((uint8_t*)vdp_buffer->bitstream)[i]);
    if((i+1) % 10 == 0)
      printf("\n");
  }*/


  if(img == NULL) {
    img = this->stream->video_out->get_frame (this->stream->video_out,
                                              this->width, this->height,
                                              this->ratio,
                                              XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS);
    this->vdpau_accel = (vdpau_accel_t*)img->accel_data;

    img->duration  = this->video_step;
    img->pts       = this->curr_pts;

    if (this->dangling_img) {
      fprintf(stderr, "broken stream: current img wasn't processed -- freeing it\n!");
      this->dangling_img->free(this->dangling_img);
    }
    this->dangling_img = img;
  }

  if(this->vdp_runtime_nr != *(this->vdpau_accel->current_vdp_runtime_nr)) {
    printf("VDPAU was preempted. Reinitialise the decoder.\n");
    this->decoder = VDP_INVALID_HANDLE;
    vdpau_h264_reset(this_gen);
    this->vdp_runtime_nr = this->vdpau_accel->vdp_runtime_nr;
    return 0;
  }

  VdpVideoSurface surface = this->vdpau_accel->surface;

  //printf("Decode: NUM: %d, REF: %d, BYTES: %d, PTS: %lld\n", pic.frame_num, pic.is_reference, vdp_buffer.bitstream_bytes, this->curr_pts);
  VdpStatus status = this->vdpau_accel->vdp_decoder_render(this->decoder,
      surface, (VdpPictureInfo*)&pic, 1, vdp_buffer);

  /* only free the actual data, as the start seq is only
   * locally allocated anyway. */
  if(((uint8_t*)vdp_buffer->bitstream) != NULL) {
    free((uint8_t*)vdp_buffer->bitstream);
  }

  this->curr_pts = this->next_pts;
  this->next_pts = 0;

  process_mmc_operations(this->nal_parser);

  if(status != VDP_STATUS_OK)
  {
    xprintf(this->xine, XINE_VERBOSITY_LOG, "vdpau_h264: Decoder failure: %s\n",  this->vdpau_accel->vdp_get_error_string(status));
    if (this->dangling_img)
      this->dangling_img->free(this->dangling_img);
    img = this->last_img = this->dangling_img = NULL;
  }
  else {
    img->bad_frame = 0;

    if((sps->vui_parameters_present_flag &&
        sps->vui_parameters.pic_struct_present_flag &&
        !this->nal_parser->current_nal->interlaced) ||
        (!pic.field_pic_flag && !pic.mb_adaptive_frame_field_flag))
      img->progressive_frame = 1;
    else
      img->progressive_frame = 0;

    if(!img->progressive_frame && this->nal_parser->current_nal->repeat_pic)
      img->repeat_first_field = 1;
    //else if(img->progressive_frame && this->nal_parser->current_nal->repeat_pic)
    //  img->duration *= this->nal_parser->current_nal->repeat_pic;

    /* only bt601 and bt701 handled so far. others seem to be rarely used */
    if(sps->vui_parameters.colour_description_present) {
      switch (sps->vui_parameters.colour_primaries) {
        case 1:
          this->color_standard = VDP_COLOR_STANDARD_ITUR_BT_709;
          break;
        case 5:
        case 6:
        default:
          this->color_standard = VDP_COLOR_STANDARD_ITUR_BT_601;
          break;
      }
    }

    this->vdpau_accel->color_standard = this->color_standard;

    struct decoded_picture *decoded_pic = NULL;
    if(pic.is_reference) {
      if(!slc->field_pic_flag || !this->wait_for_bottom_field) {
        decoded_pic = init_decoded_picture(this->nal_parser->current_nal, surface, img);
        this->last_ref_pic = decoded_pic;
        decoded_pic->used_for_reference = 1;
        dpb_add_picture(&(this->nal_parser->dpb), decoded_pic, sps->num_ref_frames);
        this->dangling_img = NULL;
      } else if(slc->field_pic_flag && this->wait_for_bottom_field) {
        if(this->last_ref_pic) {
          decoded_pic = this->last_ref_pic;
          //copy_nal_unit(decoded_pic->nal, this->nal_parser->current_nal);
          decoded_pic->nal->bottom_field_order_cnt = this->nal_parser->current_nal->bottom_field_order_cnt;
          this->last_ref_pic->bottom_is_reference = 1;
        }
      }
    }

    if(!slc->field_pic_flag ||
        (slc->field_pic_flag && slc->bottom_field_flag && this->wait_for_bottom_field)) {
      if(!decoded_pic) {
        decoded_pic = init_decoded_picture(this->nal_parser->current_nal, surface, img);
        decoded_pic->delayed_output = 1;
        dpb_add_picture(&(this->nal_parser->dpb), decoded_pic, sps->num_ref_frames);
        this->dangling_img = NULL;
        if(decoded_pic->nal->slc->bottom_field_flag)
          decoded_pic->nal->top_field_order_cnt = this->last_top_field_order_cnt;
      } else
        decoded_pic->delayed_output = 1;

      if(this->wait_for_bottom_field && slc->bottom_field_flag)
        decoded_pic->nal->bottom_field_order_cnt = this->nal_parser->current_nal->bottom_field_order_cnt;

      this->last_img = img = NULL;

      /* now retrieve the next output frame */
      if ((decoded_pic = dpb_get_next_out_picture(&(this->nal_parser->dpb))) != NULL) {
        decoded_pic->img->top_field_first = (decoded_pic->nal->top_field_order_cnt <= decoded_pic->nal->bottom_field_order_cnt);
        //printf("draw pts: %lld\n", decoded_pic->img->pts);
        decoded_pic->img->draw(decoded_pic->img, this->stream);
        dpb_set_output_picture(&(this->nal_parser->dpb), decoded_pic);
      }

      this->wait_for_bottom_field = 0;

    } else if(slc->field_pic_flag && !slc->bottom_field_flag) {
      // don't draw yet, second field is missing.
      this->last_top_field_order_cnt = this->nal_parser->current_nal->top_field_order_cnt;
      this->wait_for_bottom_field = 1;
      this->last_img = img;
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
      uint8_t *codec_private = buf->decoder_info_ptr[2];
      uint32_t codec_private_len = buf->decoder_info[2];
      this->codec_private_len = codec_private_len;
      this->codec_private = malloc(codec_private_len);
      memcpy(this->codec_private, codec_private, codec_private_len);

      if(codec_private_len > 0) {
        parse_codec_private(this->nal_parser, codec_private, codec_private_len);
      }
    } else if (buf->decoder_info[1] == BUF_SPECIAL_PALETTE) {
      printf("SPECIAL PALETTE is not yet handled\n");
    } else
      printf("UNKNOWN SPECIAL HEADER\n");

  } else {
    /* parse the first nal packages to retrieve profile type */
    int len = 0;
    uint32_t slice_count;

    while(len < buf->size && !(this->wait_for_frame_start && !(buf->decoder_flags & BUF_FLAG_FRAME_START))) {
      this->wait_for_frame_start = 0;
      len += parse_frame(this->nal_parser, buf->content + len, buf->size - len,
          (uint8_t*)&vdp_buffer.bitstream, &vdp_buffer.bitstream_bytes, &slice_count);

      if(this->decoder == VDP_INVALID_HANDLE &&
          this->nal_parser->current_nal->sps != NULL &&
          this->nal_parser->current_nal->sps->pic_width > 0 &&
          this->nal_parser->current_nal->sps->pic_height > 0) {

        vdpau_decoder_init(this_gen);
      }

      if(this->decoder != VDP_INVALID_HANDLE &&
          vdp_buffer.bitstream_bytes > 0 &&
          this->nal_parser->current_nal->slc != NULL &&
          this->nal_parser->current_nal->pps != NULL) {
        vdpau_decoder_render(this_gen, &vdp_buffer, slice_count);
      }
    }

    if(buf->pts != 0 && buf->pts != this->next_pts) {
      //printf("next pts: %lld\n", buf->pts);
      this->next_pts = buf->pts;
    }
  }

  if(buf->decoder_flags & BUF_FLAG_FRAME_END)
    this->wait_for_frame_start = 0;
}

/*
 * This function is called when xine needs to flush the system.
 */
static void vdpau_h264_flush (video_decoder_t *this_gen) {
}

/*
 * This function resets the video decoder.
 */
static void vdpau_h264_reset (video_decoder_t *this_gen) {
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *) this_gen;

  printf("vdpau_h264_reset\n");

  dpb_free_all( &(this->nal_parser->dpb) );

  if (this->decoder != VDP_INVALID_HANDLE) {
    this->vdpau_accel->vdp_decoder_destroy( this->decoder );
    this->decoder = VDP_INVALID_HANDLE;
  }

  free_parser(this->nal_parser);

  this->color_standard = VDP_COLOR_STANDARD_ITUR_BT_601;
  this->wait_for_bottom_field = 0;
  this->video_step = 0;
  this->curr_pts = 0;
  this->next_pts = 0;

  this->nal_parser = init_parser();
  if(this->codec_private_len > 0) {
    parse_codec_private(this->nal_parser, this->codec_private, this->codec_private_len);

    /* if the stream does not contain frame boundary marks we
     * have to hope that the next nal will start with the next
     * incoming buf... seems to work, though...
     */
    this->wait_for_frame_start = this->have_frame_boundary_marks;
  }

  if (this->dangling_img) {
    this->dangling_img->free(this->dangling_img);
    this->dangling_img = NULL;
  }

  this->last_img = NULL;
}

/*
 * The decoder should forget any stored pts values here.
 */
static void vdpau_h264_discontinuity (video_decoder_t *this_gen) {
  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *) this_gen;

  this->curr_pts = 0;
  this->next_pts = 0;
  dpb_clear_all_pts(&this->nal_parser->dpb);
}

/*
 * This function frees the video decoder instance allocated to the decoder.
 */
static void vdpau_h264_dispose (video_decoder_t *this_gen) {

  vdpau_h264_decoder_t *this = (vdpau_h264_decoder_t *) this_gen;

  if (this->dangling_img) {
    this->dangling_img->free(this->dangling_img);
    this->dangling_img = NULL;
  }

  dpb_free_all( &(this->nal_parser->dpb) );

  if (this->decoder != VDP_INVALID_HANDLE) {
    this->vdpau_accel->vdp_decoder_destroy( this->decoder );
    this->decoder = VDP_INVALID_HANDLE;
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

  /* the videoout must be vdpau-capable to support this decoder */
  if ( !(stream->video_driver->get_capabilities(stream->video_driver) & VO_CAP_VDPAU_H264) )
	  return NULL;

  /* now check if vdpau has free decoder resource */
  vo_frame_t *img = stream->video_out->get_frame( stream->video_out, 1920, 1080, 1, XINE_IMGFMT_VDPAU, VO_BOTH_FIELDS );
  vdpau_accel_t *accel = (vdpau_accel_t*)img->accel_data;
  int runtime_nr = accel->vdp_runtime_nr;
  img->free(img);
  VdpDecoder decoder;
  VdpStatus st = accel->vdp_decoder_create( accel->vdp_device, VDP_DECODER_PROFILE_H264_MAIN, 1920, 1080, 16, &decoder );
  if ( st!=VDP_STATUS_OK ) {
    lprintf( "can't create vdpau decoder.\n" );
    return 1;
  }

  accel->vdp_decoder_destroy( decoder );

  this = (vdpau_h264_decoder_t *) calloc(1, sizeof(vdpau_h264_decoder_t));

  this->video_decoder.decode_data         = vdpau_h264_decode_data;
  this->video_decoder.flush               = vdpau_h264_flush;
  this->video_decoder.reset               = vdpau_h264_reset;
  this->video_decoder.discontinuity       = vdpau_h264_discontinuity;
  this->video_decoder.dispose             = vdpau_h264_dispose;

  this->stream                            = stream;
  this->xine                              = stream->xine;
  this->class                             = (vdpau_h264_class_t *) class_gen;

  this->decoder                           = VDP_INVALID_HANDLE;
  this->vdp_runtime_nr                    = runtime_nr;
  this->color_standard                    = VDP_COLOR_STANDARD_ITUR_BT_601;

  this->nal_parser = init_parser();

  (this->stream->video_out->open) (this->stream->video_out, this->stream);

  return &this->video_decoder;
}

/*
 * This function returns a brief string that describes (usually with the
 * decoder's most basic name) the video decoder plugin.
 */
static char *get_identifier (video_decoder_class_t *this) {
  return "vdpau_h264";
}

/*
 * This function returns a slightly longer string describing the video
 * decoder plugin.
 */
static char *get_description (video_decoder_class_t *this) {
  return "vdpau_h264: h264 decoder plugin using VDPAU hardware decoding.\n"
	  "Must be used along with video_out_vdpau.";
}

/*
 * This function frees the video decoder class and any other memory that was
 * allocated.
 */
static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

/*
 * This function allocates a private video decoder class and initializes
 * the class's member functions.
 */
static void *init_plugin (xine_t *xine, void *data) {

  vdpau_h264_class_t *this;

  this = (vdpau_h264_class_t *) calloc(1, sizeof(vdpau_h264_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/*
 * This is a list of all of the internal xine video buffer types that
 * this decoder is able to handle. Check src/xine-engine/buffer.h for a
 * list of valid buffer types (and add a new one if the one you need does
 * not exist). Terminate the list with a 0.
 */
static const uint32_t video_types[] = {
  /* BUF_VIDEO_FOOVIDEO, */
  BUF_VIDEO_H264,
  0
};

/*
 * This data structure combines the list of supported xine buffer types and
 * the priority that the plugin should be given with respect to other
 * plugins that handle the same buffer type. A plugin with priority (n+1)
 * will be used instead of a plugin with priority (n).
 */
static const decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  7                    /* priority        */
};

/*
 * The plugin catalog entry. This is the only information that this plugin
 * will export to the public.
 */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* { type, API, "name", version, special_info, init_function } */
  { PLUGIN_VIDEO_DECODER, 18, "vdpau_h264", XINE_VERSION_CODE, &dec_info_video, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
