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
 * $Id: print_info.c,v 1.2 2003/12/07 15:34:30 f1rmb Exp $
 *
 * 04-08-2003 DTS software decode (C) James Courtier-Dutton
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
#include "xineutils.h"
#include "audio_out.h"
#include "buffer.h"
#include "dts_debug.h"
#include "decoder.h"
#include "decoder_internal.h"
#include "print_info.h"

#ifdef ENABLE_DTS_PARSE

void dts_print_decoded_data(decoder_data_t *decoder_data) {
  int32_t        n, ch, i;
  printf("frame_type = %d\n",
          decoder_data->frame_type);
  printf("deficit_sample_count = %d\n",
          decoder_data->deficit_sample_count);
  printf("crc_present_flag = %d\n",
          decoder_data->crc_present_flag);
  printf("number_of_pcm_blocks = %d\n",
          decoder_data->number_of_pcm_blocks);
  printf("primary_frame_byte_size = %d\n",
          decoder_data->primary_frame_byte_size);
  printf("audio_channel_arrangement = %d\n",
          decoder_data->audio_channel_arrangement);
  printf("core_audio_sampling_frequency = %d\n",
          decoder_data->core_audio_sampling_frequency);
  printf("transmission_bit_rate = %d\n",
          decoder_data->transmission_bit_rate);
  printf("embedded_down_mix_enabled = %d\n",
          decoder_data->embedded_down_mix_enabled);
  printf("embedded_dynamic_range_flag = %d\n",
          decoder_data->embedded_dynamic_range_flag);
  printf("embedded_time_stamp_flag = %d\n",
          decoder_data->embedded_time_stamp_flag);
  printf("auxiliary_data_flag = %d\n",
          decoder_data->auxiliary_data_flag);
  printf("hdcd = %d\n",
          decoder_data->hdcd);
  printf("extension_audio_descriptor_flag = %d\n",
          decoder_data->extension_audio_descriptor_flag);
  printf("extended_coding_flag = %d\n",
          decoder_data->extended_coding_flag);
  printf("audio_sync_word_insertion_flag = %d\n",
          decoder_data->audio_sync_word_insertion_flag);
  printf("low_frequency_effects_flag = %d\n",
          decoder_data->low_frequency_effects_flag);
  printf("predictor_history_flag_switch = %d\n",
          decoder_data->predictor_history_flag_switch);
  if (decoder_data->crc_present_flag == 1) { 
    printf("header_crc_check_bytes = %d\n",
            decoder_data->header_crc_check_bytes);
  }
  printf("multirate_interpolator_switch = %d\n",
          decoder_data->multirate_interpolator_switch);
  printf("encoder_software_revision = %d\n",
          decoder_data->encoder_software_revision);
  printf("copy_history = %d\n",
          decoder_data->copy_history);
  printf("source_pcm_resolution = %d\n",
          decoder_data->source_pcm_resolution);
  printf("front_sum_difference_flag = %d\n",
          decoder_data->front_sum_difference_flag);
  printf("surrounds_sum_difference_flag = %d\n",
          decoder_data->surrounds_sum_difference_flag);
  printf("dialog_normalisation_parameter = %d\n",
          decoder_data->dialog_normalisation_parameter);
  printf("dialog_normalisation_unspecified = %d\n",
          decoder_data->dialog_normalisation_unspecified);
  printf("dialog_normalisation_gain = %d\n",
          decoder_data->dialog_normalisation_gain);

  printf("number_of_subframes = %d\n",decoder_data->number_of_subframes);
  printf("number_of_primary_audio_channels = %d\n", decoder_data->number_of_primary_audio_channels);
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    printf("subband_activity_count[%d] = %d\n", ch, decoder_data->subband_activity_count[ch]);
  }
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    printf("high_frequency_VQ_start_subband[%d] = %d\n", ch, decoder_data->high_frequency_VQ_start_subband[ch]);
  }
  for (n=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    printf("joint_intensity_coding_index[%d] = %d\n", ch, decoder_data->joint_intensity_coding_index[ch]);
  }
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    printf("transient_mode_code_book[%d] = %d\n", ch, decoder_data->transient_mode_code_book[ch]);
  }
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    printf("scales_factor_code_book[%d] = %d\n", ch, decoder_data->scales_factor_code_book[ch]);
  }
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    printf("bit_allocation_quantizer_select[%d] = %d\n", ch, decoder_data->bit_allocation_quantizer_select[ch]);
  }

  printf("quantization_index_codebook_select: -\n");
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    for(n=0; n < 11;n++) {
      printf("%04d ",decoder_data->quantization_index_codebook_select[ch][n]);
    }
    printf("\n");
  }

  printf("scale_factor_adjustment_index: -\n");
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    for(n=0; n < 10;n++) {
      printf("%06f ",decoder_data->scale_factor_adjustment_index[ch][n]);
    }
    printf("\n");
  }
/* B.3.2          Unpack Subframes */
/* B.3.2.1 Primary Audio Coding Side Information */

/* Subsubframe Count V SSC 2 bit */
  printf("subsubframe_count = %d\n",
      decoder_data->subsubframe_count);
/* Partial Subsubframe Sample Count V PSC 3 bit */
  printf("partial_subsubframe_sample_count = %d\n",
      decoder_data->partial_subsubframe_sample_count);

/* Prediction Mode V PMODE 1 bit per subband */
  printf("prediction_mode: -\n");
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    for (n=0; n<decoder_data->subband_activity_count[ch]; n++) {
      printf("%01d ",
          decoder_data->prediction_mode[ch][n]);
    }
    printf("\n");
  }

/* Prediction Coefficients VQ Address V PVQ 12 bits per occurrence */
  printf("PVQIndex: -\n");
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    for (n=0; n<decoder_data->subband_activity_count[ch]; n++) {
      printf("%03x ",
          decoder_data->PVQIndex[ch][n]);
    }
    printf("\n");
  }

/* Bit Allocation Index V ABITS variable bits */
  printf("bit_allocation_index: -\n");
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    for (n=0; n<decoder_data->high_frequency_VQ_start_subband[ch]; n++) {
      printf("%02x ",
          decoder_data->bit_allocation_index[ch][n]);
    }
    printf("\n");
  }

  /* Transition Mode V TMODE variable bits */
  printf("transition_mode: -\n");
  for (ch=0; ch<decoder_data->number_of_primary_audio_channels; ch++) {
    for (n=0; n<decoder_data->high_frequency_VQ_start_subband[ch]; n++) {
      printf("%02x ",
          decoder_data->transition_mode[ch][n]);
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


  printf("Parse exited as required!");
  abort();

  return;
}
#endif
