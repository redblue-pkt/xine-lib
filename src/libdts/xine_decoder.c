/* 
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: xine_decoder.c,v 1.40 2003/05/24 13:21:24 jcdutton Exp $
 *
 * 04-09-2001 DTS passtrough  (C) Joachim Koenig 
 * 09-12-2001 DTS passthrough inprovements (C) James Courtier-Dutton
 *
 */

#ifndef __sun
/* required for swab() */
#define _XOPEN_SOURCE 500
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h> /* ntohs */
#include <assert.h>

#include "xine_internal.h"
#include "audio_out.h"
#include "buffer.h"

/*
#define LOG_DEBUG
*/

/*
#define ENABLE_DTS_PARSE
*/

typedef struct {
  audio_decoder_class_t   decoder_class;
} dts_class_t;

typedef struct dts_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;
  audio_decoder_class_t *class;

  uint32_t         rate;
  uint32_t         bits_per_sample; 
  uint32_t         number_of_channels; 
   
  int              output_open;
} dts_decoder_t;

#ifdef ENABLE_DTS_PARSE

typedef struct {
  uint8_t *start;
  uint32_t byte_position;
  uint32_t bit_position;
  uint8_t byte;
} getbits_state_t;

static float AdjTable[] = {
  1.0000,
  1.1250,
  1.2500,
  1.4375
};


static int32_t getbits_init(getbits_state_t *state, uint8_t *start) {
  if ((state == NULL) || (start == NULL)) return -1;
  state->start = start;
  state->bit_position = 0;
  state->byte_position = 0;
  state->byte = start[0];
  return 0;
}
/* Non-optimized getbits. */
/* This can easily be optimized for particular platforms. */
static uint32_t getbits(getbits_state_t *state, uint32_t number_of_bits) {
  uint32_t result=0;
  uint8_t byte=0;
  if (number_of_bits > 32) {
    printf("Number of bits > 32 in getbits\n");
    assert(0);
  }

  if ((state->bit_position) > 0) {  /* Last getbits left us in the middle of a byte. */
    if (number_of_bits > (8-state->bit_position)) { /* this getbits will span 2 or more bytes. */
      byte = state->byte;
      byte = byte >> (state->bit_position);
      result = byte;
      number_of_bits -= (8-state->bit_position);
      state->bit_position = 0;
      state->byte_position++;
      state->byte = state->start[state->byte_position];
    } else {
      byte=state->byte;
      state->byte = state->byte << number_of_bits;
      byte = byte >> (8 - number_of_bits);
      result = byte;
      state->bit_position += number_of_bits; /* Here it is impossible for bit_position > 8 */
      if (state->bit_position == 8) {
        state->bit_position = 0;
        state->byte_position++;
        state->byte = state->start[state->byte_position];
      }
      number_of_bits = 0;
    }
  }
  if ((state->bit_position) == 0)
    while (number_of_bits > 7) {
      result = (result << 8) + state->byte;
      state->byte_position++;
      state->byte = state->start[state->byte_position];
      number_of_bits -= 8;
    }
    if (number_of_bits > 0) { /* number_of_bits < 8 */
      byte = state->byte;
      state->byte = state->byte << number_of_bits;
      state->bit_position += number_of_bits; /* Here it is impossible for bit_position > 7 */
      if (state->bit_position > 7) printf ("bit_pos2 too large: %d\n",state->bit_position);
      byte = byte >> (8 - number_of_bits);
      result = (result << number_of_bits) + byte;
      number_of_bits = 0;
    }

  return result;
}

/* Used by dts.wav files, only 14 bits of the 16 possible are used in the CD. */
static void squash14to16(uint8_t *buf_from, uint8_t *buf_to, uint32_t number_of_bytes) {
  int32_t from;
  int32_t to=0;
  uint16_t sample1;
  uint16_t sample2;
  uint16_t sample3;
  uint16_t sample4;
  uint16_t sample16bit;
  /* This should convert the 14bit sync word into a 16bit one. */  
  printf("libdts: squashing %d bytes.\n", number_of_bytes);
  for(from=0;from<number_of_bytes;from+=8) {
    sample1 = buf_from[from+0] | buf_from[from+1] << 8;
    sample1 = (sample1 & 0x1fff) | ((sample1 & 0x8000) >> 2);
    sample2 = buf_from[from+2] | buf_from[from+3] << 8;
    sample2 = (sample2 & 0x1fff) | ((sample2 & 0x8000) >> 2);
    sample16bit = (sample1 << 2) | (sample2 >> 12);
    buf_to[to++] = sample16bit >> 8; /* Add some swabbing in as well */
    buf_to[to++] = sample16bit & 0xff;
    sample3 = buf_from[from+4] | buf_from[from+5] << 8;
    sample3 = (sample3 & 0x1fff) | ((sample3 & 0x8000) >> 2);
    sample16bit = ((sample2 & 0xfff) << 4) | (sample3 >> 10);
    buf_to[to++] = sample16bit >> 8; /* Add some swabbing in as well */
    buf_to[to++] = sample16bit & 0xff;
    sample4 = buf_from[from+6] | buf_from[from+7] << 8;
    sample4 = (sample4 & 0x1fff) | ((sample4 & 0x8000) >> 2);
    sample16bit = ((sample3 & 0x3ff) << 6) | (sample4 >> 8);
    buf_to[to++] = sample16bit >> 8; /* Add some swabbing in as well */
    buf_to[to++] = sample16bit & 0xff;
    buf_to[to++] = sample4 & 0xff;
  }

}
#endif


void dts_reset (audio_decoder_t *this_gen) {

  /* dts_decoder_t *this = (dts_decoder_t *) this_gen; */

}

void dts_discontinuity (audio_decoder_t *this_gen) {
}

#ifdef ENABLE_DTS_PARSE

static void dts_parse_data (dts_decoder_t *this, buf_element_t *buf) {
  uint8_t        *data_in = (uint8_t *)buf->content;
  getbits_state_t state;
  uint32_t       sync_type=0;
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
  uint16_t       header_crc_check_bytes=0;
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
  float        scale_factor_adjustment_index[8][10];
  uint16_t       audio_header_crc_check_word;



  uint32_t       channel_extension_sync_word;
  uint16_t       extension_primary_frame_byte_size; 
  uint8_t        extension_channel_arrangement;

  uint32_t       extension_sync_word_SYNC96;
  uint16_t       extension_frame_byte_data_size_FSIZE96;
  uint8_t        revision_number;

  int32_t        n, ch, i;
  printf("libdts: buf->size = %d\n", buf->size);
  printf("libdts: parse1: ");
  for(i=0;i<16;i++) {
    printf("%02x ",data_in[i]);
  }
  printf("\n");
  
  if ((data_in[0] == 0x7f) && 
      (data_in[1] == 0xfe) &&
      (data_in[2] == 0x80) &&
      (data_in[3] == 0x01)) {
    sync_type=1;
  }
  if (data_in[0] == 0xff &&
      data_in[1] == 0x1f &&
      data_in[2] == 0x00 &&
      data_in[3] == 0xe8 &&
      data_in[4] == 0xf1 &&    /* DTS standard document was wrong here! */
      data_in[5] == 0x07 ) {   /* DTS standard document was wrong here! */
    squash14to16(&data_in[0], &data_in[0], buf->size);
    buf->size = buf->size - (buf->size / 8); /* size = size * 7 / 8; */
    sync_type=2;
  }
  if (sync_type == 0) {
    printf("libdts: DTS Sync bad\n");
    return;
  }
  printf("libdts: DTS Sync OK. type=%d\n", sync_type);
  printf("libdts: parse2: ");
  for(i=0;i<16;i++) {
    printf("%02x ",data_in[i]);
  }
  printf("\n");

  getbits_init(&state, &data_in[4]);
  frame_type = getbits(&state, 1); /* 1: Normal Frame, 2:Termination Frame */
  deficit_sample_count = getbits(&state, 5);
  crc_present_flag = getbits(&state, 1);
  number_of_pcm_blocks = getbits(&state, 7);
  primary_frame_byte_size = getbits(&state, 14);
  audio_channel_arrangement = getbits(&state, 6);
  core_audio_sampling_frequency = getbits(&state, 4);
  transmission_bit_rate = getbits(&state, 5);
  embedded_down_mix_enabled = getbits(&state, 1);
  embedded_dynamic_range_flag = getbits(&state, 1);
  embedded_time_stamp_flag = getbits(&state, 1);
  auxiliary_data_flag = getbits(&state, 1);
  hdcd = getbits(&state, 1);
  extension_audio_descriptor_flag = getbits(&state, 3);
  extended_coding_flag = getbits(&state, 1);
  audio_sync_word_insertion_flag = getbits(&state, 1);
  low_frequency_effects_flag = getbits(&state, 2);
  predictor_history_flag_switch = getbits(&state, 1);
  if (crc_present_flag == 1) 
    header_crc_check_bytes  = getbits(&state, 16);
  multirate_interpolator_switch = getbits(&state, 1);
  encoder_software_revision = getbits(&state, 4);
  copy_history = getbits(&state, 2);
  source_pcm_resolution = getbits(&state, 3);
  front_sum_difference_flag = getbits(&state, 1);
  surrounds_sum_difference_flag = getbits(&state, 1);
  switch (encoder_software_revision) {
  case 6:
    dialog_normalisation_unspecified = 0;
    dialog_normalisation_parameter = getbits(&state, 4);
    dialog_normalisation_gain = - (16+dialog_normalisation_parameter);
    break;
  case 7:
    dialog_normalisation_unspecified = 0;
    dialog_normalisation_parameter = getbits(&state, 4);
    dialog_normalisation_gain = - (dialog_normalisation_parameter);
    break;
  default:
    dialog_normalisation_unspecified = getbits(&state, 4);
    dialog_normalisation_gain = dialog_normalisation_parameter = 0;
    break;
  }


  number_of_subframes = getbits(&state, 4) + 1 ;
  number_of_primary_audio_channels = getbits(&state, 3) + 1 ;
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    subband_activity_count[ch] = getbits(&state, 5) + 2 ;
  }
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    high_frequency_VQ_start_subband[ch] = getbits(&state, 5) + 1 ;
  }
  for (n=0; ch<number_of_primary_audio_channels; ch++) {
    joint_intensity_coding_index[ch] = getbits(&state, 3) ;
  }
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    transient_mode_code_book[ch] = getbits(&state, 2) ;
  }
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    scales_factor_code_book[ch] = getbits(&state, 3) ;
  }
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    bit_allocation_quantizer_select[ch] = getbits(&state, 3) ;
  }

  /* ABITS=1: */
  n=0;
  for (ch=0; ch<number_of_primary_audio_channels; ch++)
    quantization_index_codebook_select[ch][n] = getbits(&state, 1);
  /* ABITS = 2 to 5: */
  for (n=1; n<5; n++)
    for (ch=0; ch<number_of_primary_audio_channels; ch++)
      quantization_index_codebook_select[ch][n] = getbits(&state, 2);
  /* ABITS = 6 to 10: */
  for (n=5; n<10; n++)
    for (ch=0; ch<number_of_primary_audio_channels; ch++)
      quantization_index_codebook_select[ch][n] = getbits(&state, 3);
  /* ABITS = 11 to 26: */
  for (n=10; n<26; n++)
    for (ch=0; ch<number_of_primary_audio_channels; ch++)
      quantization_index_codebook_select[ch][n] = 0; /* Not transmitted, set to zero. */

  /* ABITS = 1: */
  n = 0;
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    int32_t adj;
    if ( quantization_index_codebook_select[ch][n] == 0 ) { /* Transmitted only if SEL=0 (Huffman code used) */
      /* Extract ADJ index */
      adj = getbits(&state, 2);
      /* Look up ADJ table */
      scale_factor_adjustment_index[ch][n] = AdjTable[adj];
    }
  }
  /* ABITS = 2 to 5: */
  for (n=1; n<5; n++){
    for (ch=0; ch<number_of_primary_audio_channels; ch++){
      int32_t adj;
      if ( quantization_index_codebook_select[ch][n] < 3 ) { /* Transmitted only when SEL<3 */
        /* Extract ADJ index */
        adj = getbits(&state, 2);
        /* Look up ADJ table */
        scale_factor_adjustment_index[ch][n] = AdjTable[adj];
      }
    }
  }
  /* ABITS = 6 to 10: */
  for (n=5; n<10; n++){
    for (ch=0; ch<number_of_primary_audio_channels; ch++){
      int32_t adj;
      if ( quantization_index_codebook_select[ch][n] < 7 ) { /* Transmitted only when SEL<7 */
        /* Extract ADJ index */
        adj = getbits(&state, 2);
        /* Look up ADJ table */
        scale_factor_adjustment_index[ch][n] = AdjTable[adj];
      }
    }
  }

  if (crc_present_flag == 1) { /* Present only if CPF=1. */
    audio_header_crc_check_word = getbits(&state, 16);
  }


  printf("getbits status: byte_pos = %d, bit_pos = %d\n", 
          state.byte_position,
          state.bit_position);
#if 0
  for(n=0;n<2016;n++) {
    if((n % 32) == 0) printf("\n");
    printf("%02X ",state.start[state.byte_position+n]);
  }
  printf("\n");
#endif

#if 0
  if ((extension_audio_descriptor_flag == 0)
     || (extension_audio_descriptor_flag == 3)) {
    printf("libdts:trying extension...\n");
    channel_extension_sync_word = getbits(&state, 32);
    extension_primary_frame_byte_size = getbits(&state, 10); 
    extension_channel_arrangement = getbits(&state, 4);
  }
#endif

#if 0
    extension_sync_word_SYNC96 = getbits(&state, 32);
    extension_frame_byte_data_size_FSIZE96 = getbits(&state, 12);
    revision_number = getbits(&state, 4);
#endif


  printf("frame_type = %d\n",
          frame_type);
  printf("deficit_sample_count = %d\n",
          deficit_sample_count);
  printf("crc_present_flag = %d\n",
          crc_present_flag);
  printf("number_of_pcm_blocks = %d\n",
          number_of_pcm_blocks);
  printf("primary_frame_byte_size = %d\n",
          primary_frame_byte_size);
  printf("audio_channel_arrangement = %d\n",
          audio_channel_arrangement);
  printf("core_audio_sampling_frequency = %d\n",
          core_audio_sampling_frequency);
  printf("transmission_bit_rate = %d\n",
          transmission_bit_rate);
  printf("embedded_down_mix_enabled = %d\n",
          embedded_down_mix_enabled);
  printf("embedded_dynamic_range_flag = %d\n",
          embedded_dynamic_range_flag);
  printf("embedded_time_stamp_flag = %d\n",
          embedded_time_stamp_flag);
  printf("auxiliary_data_flag = %d\n",
          auxiliary_data_flag);
  printf("hdcd = %d\n",
          hdcd);
  printf("extension_audio_descriptor_flag = %d\n",
          extension_audio_descriptor_flag);
  printf("extended_coding_flag = %d\n",
          extended_coding_flag);
  printf("audio_sync_word_insertion_flag = %d\n",
          audio_sync_word_insertion_flag);
  printf("low_frequency_effects_flag = %d\n",
          low_frequency_effects_flag);
  printf("predictor_history_flag_switch = %d\n",
          predictor_history_flag_switch);
  if (crc_present_flag == 1) { 
    printf("header_crc_check_bytes = %d\n",
            header_crc_check_bytes);
  }
  printf("multirate_interpolator_switch = %d\n",
          multirate_interpolator_switch);
  printf("encoder_software_revision = %d\n",
          encoder_software_revision);
  printf("copy_history = %d\n",
          copy_history);
  printf("source_pcm_resolution = %d\n",
          source_pcm_resolution);
  printf("front_sum_difference_flag = %d\n",
          front_sum_difference_flag);
  printf("surrounds_sum_difference_flag = %d\n",
          surrounds_sum_difference_flag);
  printf("dialog_normalisation_parameter = %d\n",
          dialog_normalisation_parameter);
  printf("dialog_normalisation_unspecified = %d\n",
          dialog_normalisation_unspecified);
  printf("dialog_normalisation_gain = %d\n",
          dialog_normalisation_gain);

  printf("number_of_subframes = %d\n",number_of_subframes);
  printf("number_of_primary_audio_channels = %d\n", number_of_primary_audio_channels);
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    printf("subband_activity_count[%d] = %d\n", ch, subband_activity_count[ch]);
  }
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    printf("high_frequency_VQ_start_subband[%d] = %d\n", ch, high_frequency_VQ_start_subband[ch]);
  }
  for (n=0; ch<number_of_primary_audio_channels; ch++) {
    printf("joint_intensity_coding_index[%d] = %d\n", ch, joint_intensity_coding_index[ch]);
  }
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    printf("transient_mode_code_book[%d] = %d\n", ch, transient_mode_code_book[ch]);
  }
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    printf("scales_factor_code_book[%d] = %d\n", ch, scales_factor_code_book[ch]);
  }
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    printf("bit_allocation_quantizer_select[%d] = %d\n", ch, bit_allocation_quantizer_select[ch]);
  }

  printf("quantization_index_codebook_select: -\n");
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    for(n=0; n < 10;n++) {
      printf("%04d ",quantization_index_codebook_select[ch][n]);
    }
    printf("\n");
  }


#if 0
  printf("channel_extension_sync_word = 0x%08X\n",
          channel_extension_sync_word);
  printf("extension_primary_frame_byte_sizes = %d\n", 
          extension_primary_frame_byte_size); 
  printf("extension_channel_arrangement = %d\n",
          extension_channel_arrangement);

  printf("extension_sync_word_SYNC96 = 0x%08X\n",
          extension_sync_word_SYNC96);
  printf("extension_frame_byte_data_size_FSIZE96 = %d\n",
          extension_frame_byte_data_size_FSIZE96);
  printf("revision_number = %d\n",
          revision_number);
#endif


assert(0);

return;
}
#endif

void dts_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  dts_decoder_t  *this = (dts_decoder_t *) this_gen;
  uint8_t        *data_in = (uint8_t *)buf->content;
  uint8_t        *data_out;
  audio_buffer_t *audio_buffer;
  uint32_t  ac5_pcm_samples;
  uint32_t  ac5_spdif_type=0;
  uint32_t  ac5_length=0;
  uint32_t  ac5_pcm_length;
  uint32_t  number_of_frames;
  uint32_t  first_access_unit;
  int n;
  
#ifdef LOG_DEBUG
  printf("libdts: DTS decode_data called.\n");
#endif
#ifdef ENABLE_DTS_PARSE
  dts_parse_data (this, buf);
#endif

  if ((this->stream->audio_out->get_capabilities(this->stream->audio_out) & AO_CAP_MODE_AC5) == 0) {
    return;
  }
  if (!this->output_open) {      
    this->output_open = (this->stream->audio_out->open (this->stream->audio_out, this->stream,
                                                this->bits_per_sample, 
                                                this->rate,
                                                AO_CAP_MODE_AC5));
  }
  if (!this->output_open) 
    return;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  number_of_frames = buf->decoder_info[1];  /* Number of frames  */
  first_access_unit = buf->decoder_info[2]; /* First access unit */

  if (number_of_frames > 2) {
    return;
  }
  for(n=1;n<=number_of_frames;n++) {
    data_in += ac5_length;
    if(data_in >= (buf->content+buf->size)) {
      printf("libdts: DTS length error\n");
      return;
    }
      
    if ((data_in[0] != 0x7f) || 
        (data_in[1] != 0xfe) ||
        (data_in[2] != 0x80) ||
        (data_in[3] != 0x01)) {
      printf("libdts: DTS Sync bad\n");
      return;
    }
    
    audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
    audio_buffer->frame_header_count = buf->decoder_info[1]; /* Number of frames */
    audio_buffer->first_access_unit = buf->decoder_info[2]; /* First access unit */

#ifdef LOG_DEBUG
    printf("libdts: DTS frame_header_count = %u\n",audio_buffer->frame_header_count);
    printf("libdts: DTS first access unit = %u\n",audio_buffer->first_access_unit);
#endif

    if (n == first_access_unit) {
      audio_buffer->vpts       = buf->pts;
    } else {
      audio_buffer->vpts       = 0;
    }
 
    data_out=(uint8_t *) audio_buffer->mem;

    ac5_pcm_samples=((data_in[4] & 0x01) << 6) | ((data_in[5] >>2) & 0x3f);


    ac5_length=((data_in[5] & 0x03) << 12) | (data_in[6] << 4) | ((data_in[7] & 0xf0) >> 4);
    ac5_length++;

    if (ac5_length > 8191) {
      printf("libdts: ac5_length too long\n");
      ac5_pcm_length = 0;
    } else {
      ac5_pcm_length = (ac5_pcm_samples + 1) * 32;
    }

    switch (ac5_pcm_length) {
    case 512:
      ac5_spdif_type = 0x0b; /* DTS-1 (512-sample bursts) */
      break;
    case 1024:
      ac5_spdif_type = 0x0c; /* DTS-1 (1024-sample bursts) */
      break;
    case 2048:
      ac5_spdif_type = 0x0d; /* DTS-1 (2048-sample bursts) */
      break;
    default:
      printf("libdts: DTS %i-sample bursts not supported\n", ac5_pcm_length);
      return;
    }

#ifdef LOG_DEBUG
    {
    int i;
    printf("libdts: DTS frame type=%d\n",data_in[4] >> 7);
    printf("libdts: DTS deficit frame count=%d\n",(data_in[4] & 0x7f) >> 2);
    printf("libdts: DTS AC5 PCM samples=%d\n",ac5_pcm_samples);
    printf("libdts: DTS AC5 length=%d\n",ac5_length);
    printf("libdts: DTS AC5 bitrate=%d\n",((data_in[8] & 0x03) << 4) | (data_in[8] >> 4));
    printf("libdts: DTS AC5 spdif type=%d\n", ac5_spdif_type);

    printf("libdts: ");
    for(i=2000;i<2048;i++) {
      printf("%02x ",data_in[i]);
    }
    printf("\n");
    }
#endif


#ifdef LOG_DEBUG
    printf("libdts: DTS length=%d loop=%d pts=%lld\n",ac5_pcm_length,n,audio_buffer->vpts);
#endif

    audio_buffer->num_frames = ac5_pcm_length;

    data_out[0] = 0x72; data_out[1] = 0xf8;	/* spdif syncword    */
    data_out[2] = 0x1f; data_out[3] = 0x4e;	/* ..............    */
    data_out[4] = ac5_spdif_type;		/* DTS data          */
    data_out[5] = 0;		                /* Unknown */
    data_out[6] = (ac5_length << 3) & 0xff;   /* ac5_length * 8   */
    data_out[7] = ((ac5_length ) >> 5) & 0xff;

    if( ac5_pcm_length ) {
      if( ac5_pcm_length % 2) {
        swab(data_in, &data_out[8], ac5_length );
      } else {
        swab(data_in, &data_out[8], ac5_length + 1);
      }
    }
    this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
  }
}

static void dts_dispose (audio_decoder_t *this_gen) {
  dts_decoder_t *this = (dts_decoder_t *) this_gen; 
  if (this->output_open) 
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;
  free (this);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t
*stream) {
  dts_decoder_t *this ;
#ifdef LOG_DEBUG
  printf("libdts: DTS open_plugin called.\n");
#endif

  this = (dts_decoder_t *) malloc (sizeof (dts_decoder_t));

  this->audio_decoder.decode_data         = dts_decode_data;
  this->audio_decoder.reset               = dts_reset;
  this->audio_decoder.discontinuity       = dts_discontinuity;
  this->audio_decoder.dispose             = dts_dispose;

  this->stream        = stream;
  this->class         = class_gen;
  this->output_open   = 0;
  this->rate          = 48000;
  this->bits_per_sample=16;
  this->number_of_channels=2;
  return &this->audio_decoder;
}

static char *get_identifier (audio_decoder_class_t *this) {
  return "DTS";
}

static char *get_description (audio_decoder_class_t *this) {
  return "DTS passthru audio format decoder plugin";
}

static void dispose_class (audio_decoder_class_t *this) {
#ifdef LOG_DEBUG
  printf("libdts: DTS class dispose called.\n");
#endif
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  dts_class_t *this ;
#ifdef LOG_DEBUG
  printf("DTS class init_plugin called.\n");
#endif
  this = (dts_class_t *) malloc (sizeof (dts_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

static uint32_t audio_types[] = { 
  BUF_AUDIO_DTS, 0
 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 13, "dts", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
