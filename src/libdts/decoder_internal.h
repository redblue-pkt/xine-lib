/*
 * Copyright (C) 2000-2003 the xine project
 *
 * This file is part of xine, a unix video player.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: decoder_internal.h,v 1.1 2003/08/05 11:30:56 jcdutton Exp $
 *
 * 04-08-2003 DTS software decode (C) James Courtier-Dutton
 *
 */

#ifndef DTS_DECODER_INTERNAL_H
#define DTS_DECODER_INTERNAL_H 1

typedef struct decoder_data_s {
  uint32_t       sync_type;
  uint8_t        frame_type;
  uint8_t        deficit_sample_count;
  uint8_t        crc_present_flag;
  uint8_t        number_of_pcm_blocks;
  uint16_t       primary_frame_byte_size;
  uint8_t        audio_channel_arrangement;
  uint8_t        core_audio_sampling_frequency;
  uint8_t        transmission_bit_rate;
  uint8_t        embedded_down_mix_enabled;
  uint8_t        embedded_dynamic_range_flag;
  uint8_t        embedded_time_stamp_flag;
  uint8_t        auxiliary_data_flag;
  uint8_t        hdcd;
  uint8_t        extension_audio_descriptor_flag;
  uint8_t        extended_coding_flag;
  uint8_t        audio_sync_word_insertion_flag;
  uint8_t        low_frequency_effects_flag;
  uint8_t        predictor_history_flag_switch;
  uint16_t       header_crc_check_bytes;
  uint8_t        multirate_interpolator_switch;
  uint8_t        encoder_software_revision;
  uint8_t        copy_history;
  uint8_t        source_pcm_resolution;
  uint8_t        front_sum_difference_flag;
  uint8_t        surrounds_sum_difference_flag;
  int8_t         dialog_normalisation_parameter;
  int8_t         dialog_normalisation_unspecified;
  int8_t         dialog_normalisation_gain;
  int8_t         number_of_subframes;
  int8_t         number_of_primary_audio_channels;
  int8_t         subband_activity_count[8];
  int8_t         high_frequency_VQ_start_subband[8];
  int8_t         joint_intensity_coding_index[8];
  int8_t         transient_mode_code_book[8];
  int8_t         scales_factor_code_book[8];
  int8_t         bit_allocation_quantizer_select[8];
  int8_t         quantization_index_codebook_select[8][26];
  float          scale_factor_adjustment_index[8][10];
  uint16_t       audio_header_crc_check_word;

  int32_t        nVQIndex;
  int32_t        nQSelect;
  int8_t         subsubframe_count;
  int8_t         partial_subsubframe_sample_count;
  int8_t         prediction_mode[8][33];
  int32_t        PVQIndex[8][33];
  int32_t        bit_allocation_index[8][33];
  int32_t        transition_mode[8][33];
  int32_t        scale_factors[8][33][2];
  int32_t        nScaleSum;


  uint32_t       channel_extension_sync_word;
  uint16_t       extension_primary_frame_byte_size;
  uint8_t        extension_channel_arrangement;

  uint32_t       extension_sync_word_SYNC96;
  uint16_t       extension_frame_byte_data_size_FSIZE96;
  uint8_t        revision_number;
} decoder_data_t;

#endif /* DTS_DECODER_H */
