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
 * $Id: decoder.c,v 1.1 2003/08/05 11:30:56 jcdutton Exp $
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

#include "huffman_tables.h"

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

static int32_t huff_lookup(getbits_state_t *state, int32_t HuffTable[][2] ) {
  int32_t n=1;
  int32_t bit;

  {
    bit = getbits(state, 1);
    n = HuffTable[n][bit];
  } while (n > 0);
  /* printf("returning %d\n", n + HuffTable[0][0]); */
  return n + HuffTable[0][0];
}


static int32_t qscales(int32_t nQSelect, getbits_state_t *state, int32_t *nScale) {
/* FIXME: IMPLEMENT */
return 0;
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

#if 0
/* FIXME: Make this re-entrant */
static void InverseADPCM(void) {
/*
 * NumADPCMCoeff =4, the number of ADPCM coefficients.
 * raADPCMcoeff[] are the ADPCM coefficients extracted
 * from the bit stream.
 * raSample[NumADPCMCoeff], ..., raSample[-1] are the
 * history from last subframe or subsubframe. It must
 * updated each time before reverse ADPCM is run for a
 * block of samples for each subband.
 */
for (m=0; m<nNumSample; m++)
for (n=0; n<NumADPCMCoeff; n++)
raSample[m] += raADPCMcoeff[n]*raSample[m-n-1];
}
#endif


void dts_parse_data (dts_decoder_t *this, buf_element_t *buf) {
  uint8_t        *data_in = (uint8_t *)buf->content;
  getbits_state_t state;
  decoder_data_t decoder_data;
  decoder_data.sync_type=0;
  decoder_data.header_crc_check_bytes=0;

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
    decoder_data.sync_type=1;
  }
  if (data_in[0] == 0xff &&
      data_in[1] == 0x1f &&
      data_in[2] == 0x00 &&
      data_in[3] == 0xe8 &&
      data_in[4] == 0xf1 &&    /* DTS standard document was wrong here! */
      data_in[5] == 0x07 ) {   /* DTS standard document was wrong here! */
    squash14to16(&data_in[0], &data_in[0], buf->size);
    buf->size = buf->size - (buf->size / 8); /* size = size * 7 / 8; */
    decoder_data.sync_type=2;
  }
  if (decoder_data.sync_type == 0) {
    printf("libdts: DTS Sync bad\n");
    return;
  }
  printf("libdts: DTS Sync OK. type=%d\n", decoder_data.sync_type);
  printf("libdts: parse2: ");
  for(i=0;i<16;i++) {
    printf("%02x ",data_in[i]);
  }
  printf("\n");

  getbits_init(&state, &data_in[4]);

  /* B.2 Unpack Frame Header Routine */
  /* Frame Type V FTYPE 1 bit */
  decoder_data.frame_type = getbits(&state, 1); /* 1: Normal Frame, 2:Termination Frame */
  /* Deficit Sample Count V SHORT 5 bits */
  decoder_data.deficit_sample_count = getbits(&state, 5);
  /* CRC Present Flag V CPF 1 bit */
  decoder_data.crc_present_flag = getbits(&state, 1);
  /* Number of PCM Sample Blocks V NBLKS 7 bits */
  decoder_data.number_of_pcm_blocks = getbits(&state, 7);
  /* Primary Frame Byte Size V FSIZE 14 bits */
  decoder_data.primary_frame_byte_size = getbits(&state, 14);
  /* Audio Channel Arrangement ACC AMODE 6 bits */
  decoder_data.audio_channel_arrangement = getbits(&state, 6);
  /* Core Audio Sampling Frequency ACC SFREQ 4 bits */
  decoder_data.core_audio_sampling_frequency = getbits(&state, 4);
  /* Transmission Bit Rate ACC RATE 5 bits */
  decoder_data.transmission_bit_rate = getbits(&state, 5);
  /* Embedded Down Mix Enabled V MIX 1 bit */
  decoder_data.embedded_down_mix_enabled = getbits(&state, 1);
  /* Embedded Dynamic Range Flag V DYNF 1 bit */
  decoder_data.embedded_dynamic_range_flag = getbits(&state, 1);
  /* Embedded Time Stamp Flag V TIMEF 1 bit */
  decoder_data.embedded_time_stamp_flag = getbits(&state, 1);
  /* Auxiliary Data Flag V AUXF 1 bit */
  decoder_data.auxiliary_data_flag = getbits(&state, 1);
  /* HDCD NV HDCD 1 bits */
  decoder_data.hdcd = getbits(&state, 1);
  /* Extension Audio Descriptor Flag ACC EXT_AUDIO_ID 3 bits */
  decoder_data.extension_audio_descriptor_flag = getbits(&state, 3);
  /* Extended Coding Flag ACC EXT_AUDIO 1 bit */
  decoder_data.extended_coding_flag = getbits(&state, 1);
  /* Audio Sync Word Insertion Flag ACC ASPF 1 bit */
  decoder_data.audio_sync_word_insertion_flag = getbits(&state, 1);
  /* Low Frequency Effects Flag V LFF 2 bits */
  decoder_data.low_frequency_effects_flag = getbits(&state, 2);
  /* Predictor History Flag Switch V HFLAG 1 bit */
  decoder_data.predictor_history_flag_switch = getbits(&state, 1);
  /* Header CRC Check Bytes V HCRC 16 bits */
  if (decoder_data.crc_present_flag == 1) 
    decoder_data.header_crc_check_bytes  = getbits(&state, 16);
  /* Multirate Interpolator Switch NV FILTS 1 bit */
  decoder_data.multirate_interpolator_switch = getbits(&state, 1);
  /* Encoder Software Revision ACC/NV VERNUM 4 bits */
  decoder_data.encoder_software_revision = getbits(&state, 4);
  /* Copy History NV CHIST 2 bits */
  decoder_data.copy_history = getbits(&state, 2);
  /* Source PCM Resolution ACC/NV PCMR 3 bits */
  decoder_data.source_pcm_resolution = getbits(&state, 3);
  /* Front Sum/Difference Flag V SUMF 1 bit */
  decoder_data.front_sum_difference_flag = getbits(&state, 1);
  /* Surrounds Sum/Difference Flag V SUMS 1 bit */
  decoder_data.surrounds_sum_difference_flag = getbits(&state, 1);
  /* Dialog Normalisation Parameter/Unspecified V DIALNORM/UNSPEC 4 bits */
  switch (decoder_data.encoder_software_revision) {
  case 6:
    decoder_data.dialog_normalisation_unspecified = 0;
    decoder_data.dialog_normalisation_parameter = getbits(&state, 4);
    decoder_data.dialog_normalisation_gain = - (16+decoder_data.dialog_normalisation_parameter);
    break;
  case 7:
    decoder_data.dialog_normalisation_unspecified = 0;
    decoder_data.dialog_normalisation_parameter = getbits(&state, 4);
    decoder_data.dialog_normalisation_gain = - (decoder_data.dialog_normalisation_parameter);
    break;
  default:
    decoder_data.dialog_normalisation_unspecified = getbits(&state, 4);
    decoder_data.dialog_normalisation_gain = decoder_data.dialog_normalisation_parameter = 0;
    break;
  }

  /* B.3 Audio Decoding */
  /* B.3.1 Primary Audio Coding Header */

  /* Number of Subframes V SUBFS 4 bits */
  decoder_data.number_of_subframes = getbits(&state, 4) + 1 ;
  /* Number of Primary Audio Channels V PCHS 3 bits */
  decoder_data.number_of_primary_audio_channels = getbits(&state, 3) + 1 ;
  /* Subband Activity Count V SUBS 5 bits per channel */
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    decoder_data.subband_activity_count[ch] = getbits(&state, 5) + 2 ;
  }
  /* High Frequency VQ Start Subband V VQSUB 5 bits per channel */
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    decoder_data.high_frequency_VQ_start_subband[ch] = getbits(&state, 5) + 1 ;
  }
  /* Joint Intensity Coding Index V JOINX 3 bits per channel */
  for (n=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    decoder_data.joint_intensity_coding_index[ch] = getbits(&state, 3) ;
  }
  /* Transient Mode Code Book V THUFF 2 bits per channel */
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    decoder_data.transient_mode_code_book[ch] = getbits(&state, 2) ;
  }
  /* Scale Factor Code Book V SHUFF 3 bits per channel */
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    decoder_data.scales_factor_code_book[ch] = getbits(&state, 3) ;
  }
  /* Bit Allocation Quantizer Select BHUFF V 3 bits per channel */
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    decoder_data.bit_allocation_quantizer_select[ch] = getbits(&state, 3) ;
  }
  /* Quantization Index Codebook Select V SEL variable bits */
  /* ABITS=1: */
  n=0;
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++)
    decoder_data.quantization_index_codebook_select[ch][n] = getbits(&state, 1);
  /* ABITS = 2 to 5: */
  for (n=1; n<5; n++)
    for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++)
      decoder_data.quantization_index_codebook_select[ch][n] = getbits(&state, 2);
  /* ABITS = 6 to 10: */
  for (n=5; n<10; n++)
    for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++)
      decoder_data.quantization_index_codebook_select[ch][n] = getbits(&state, 3);
  /* ABITS = 11 to 26: */
  for (n=10; n<26; n++)
    for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++)
      decoder_data.quantization_index_codebook_select[ch][n] = 0; /* Not transmitted, set to zero. */

  /* Scale Factor Adjustment Index V ADJ 2 bits per occasion */
  /* ABITS = 1: */
  n = 0;
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    int32_t adj;
    if ( decoder_data.quantization_index_codebook_select[ch][n] == 0 ) { /* Transmitted only if quantization_index_codebook_select=0 (Huffman code used) */
      /* Extract ADJ index */
      adj = getbits(&state, 2);
      /* Look up ADJ table */
      decoder_data.scale_factor_adjustment_index[ch][n] = AdjTable[adj];
    }
  }
  /* ABITS = 2 to 5: */
  for (n=1; n<5; n++){
    for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++){
      int32_t adj;
      if ( decoder_data.quantization_index_codebook_select[ch][n] < 3 ) { /* Transmitted only when quantization_index_codebook_select<3 */
        /* Extract ADJ index */
        adj = getbits(&state, 2);
        /* Look up ADJ table */
        decoder_data.scale_factor_adjustment_index[ch][n] = AdjTable[adj];
      }
    }
  }
  /* ABITS = 6 to 10: */
  for (n=5; n<10; n++){
    for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++){
      int32_t adj;
      if ( decoder_data.quantization_index_codebook_select[ch][n] < 7 ) { /* Transmitted only when quantization_index_codebook_select<7 */
        /* Extract ADJ index */
        adj = getbits(&state, 2);
        /* Look up ADJ table */
        decoder_data.scale_factor_adjustment_index[ch][n] = AdjTable[adj];
      }
    }
  }

  if (decoder_data.crc_present_flag == 1) { /* Present only if CPF=1. */
    decoder_data.audio_header_crc_check_word = getbits(&state, 16);
  }

/* B.3.2          Unpack Subframes */
/* B.3.2.1 Primary Audio Coding Side Information */

/* Subsubframe Count V SSC 2 bit */
  decoder_data.subsubframe_count = getbits(&state, 2) + 1;
/* Partial Subsubframe Sample Count V PSC 3 bit */
  decoder_data.partial_subsubframe_sample_count = getbits(&state, 3);
/* Prediction Mode V PMODE 1 bit per subband */
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    for (n=0; n<decoder_data.subband_activity_count[ch]; n++) {
      decoder_data.prediction_mode[ch][n] = getbits(&state, 1);
    }
  }

/* Prediction Coefficients VQ Address V PVQ 12 bits per occurrence */
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    for (n=0; n<decoder_data.subband_activity_count[ch]; n++) {
      decoder_data.PVQIndex[ch][n] = 0;
      if ( decoder_data.prediction_mode[ch][n]>0 ) { /* Transmitted only when ADPCM active */
        /* Extract the VQindex */
        decoder_data.nVQIndex = getbits(&state,12);
        /* Look up the VQ table for prediction coefficients. */
        /* FIXME: How to implement LookUp? */
        decoder_data.PVQIndex[ch][n] = decoder_data.nVQIndex;
        /* FIXME: We don't have the ADPCMCoeff table. */
        /* ADPCMCoeffVQ.LookUp(nVQIndex, PVQ[ch][n]);*/ /*  4 coefficients  FIXME: Need to work out what this does. */
      }
    }
  }


  /* Bit Allocation Index V ABITS variable bits */
  /* FIXME: No getbits here InverseQ does the getbits */
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
    /* Bit Allocation Quantizer Select tells which codebook was used */
    decoder_data.nQSelect = decoder_data.bit_allocation_quantizer_select[ch]; 
    /* Use this codebook to decode the bit stream for bit_allocation_index[ch][n] */
    for (n=0; n<decoder_data.high_frequency_VQ_start_subband[ch]; n++) {
      /* Not for VQ encoded subbands. */
      /* FIXME: What is Inverse Quantization(InverseQ) ? */
      /* This basically selects a huffman table number nQSelect, */
      /* and uses it to read a variable amount of bits and does a huffman search to find the value. */
      /* FIXME: Need to implement InverseQ, so we can uncomment this line */
      if (decoder_data.nQSelect == 6) {
          decoder_data.bit_allocation_index[ch][n] = getbits(&state,5);
      } else {
        XINE_ASSERT(0, "bit_alloc parse failed, (nQSelect != 6) not implemented yet.");
      }

      /*QABITS.ppQ[nQSelect]->InverseQ(&state, bit_allocation_index[ch][n]); */
    }
  }

  /* Transition Mode V TMODE variable bits */

  /* Always assume no transition unless told */
  for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++){
    for (n=0; n<decoder_data.subband_activity_count[ch]; n++) {
      decoder_data.transition_mode[ch][n] = 0;
    } 
    /* Decode transition_mode[ch][n] */
    if ( decoder_data.subsubframe_count>1 ) {
      /* Transient possible only if more than one subsubframe. */
      for (ch=0; ch<decoder_data.number_of_primary_audio_channels; ch++) {
        /* transition_mode[ch][n] is encoded by a codebook indexed by transient_mode_code_book[ch] */
        decoder_data.nQSelect = decoder_data.transient_mode_code_book[ch];
        for (n=0; n<decoder_data.high_frequency_VQ_start_subband[ch]; n++) {
          /* No VQ encoded subbands */
          if ( decoder_data.bit_allocation_index[ch][n] >0 ) {
            /* Present only if bits allocated */
            /* Use codebook nQSelect to decode transition_mode from the bit stream */
            /* FIXME: What is Inverse Quantization(InverseQ) ? */
            if (decoder_data.nQSelect == 0) {
            decoder_data.transition_mode[ch][n] = huff_lookup(&state, HuffA4);
            } else {
              XINE_ASSERT(0, "transition mod parse failed, (nQSelect != 0) not implemented yet.");
            }

            /* QTMODE.ppQ[nQSelect]->InverseQ(&state,transition_mode[ch][n]); */
          } else {
            decoder_data.transition_mode[ch][n] = 0;
          }
        }
      }
    }
  }

/* WORKING ON THIS BIT */


#if 0
  /* Scale Factors V SCALES variable bits */
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    /* Clear scale_factors */
    for (n=0; n<subband_activity_count[ch]; n++) {
      scale_factors[ch][n][0] = 0;
      scale_factors[ch][n][1] = 0;
    }
    /* scales_factor_code_book indicates which codebook was used to encode scale_factors */
    nQSelect = scales_factor_code_book[ch];
    /* Select the root square table (scale_factors were nonlinearly */
    /* quantized). */
    /* Assume nQSelect != 6 */
    /* So RMS is always 6 bit. */
    if ( nQSelect == 6 ) {
      /* pScaleTable = &RMS7Bit;*/  /* 7-bit root square table */
    } else {
      /* pScaleTable = &RMS6Bit;*/ /* 6-bit root square table */
    }
    /*
     * Clear accumulation (if Huffman code was used, the difference
     * of scale_factors was encoded).
     */
    nScaleSum = 0;
    /*
     * Extract scale_factors for Subbands up to high_frequency_VQ_start_subband[ch]
     */
    for (n=0; n<high_frequency_VQ_start_subband[ch]; n++) {
      if ( bit_allocation_index[ch][n] >0 ) { /* Not present if no bit allocated */
        /*
         * First scale factor
         */
        /* Use the (Huffman) code indicated by nQSelect to decode */
        /* the quantization index of scale_factors from the bit stream */
        /* FIXME: What is Inverse Quantization(InverseQ) ? */
        qscales(nQSelect, &state, &nScale);
        /* QSCALES.ppQ[nQSelect]->InverseQ(InputFrame, nScale); */
        /* Take care of difference encoding */
        if ( nQSelect < 5 ) { /* Huffman encoded, nScale is the difference */
          nScaleSum += nScale; /* of the quantization indexes of scale_factors. */
        } else { /* Otherwise, nScale is the quantization */
          nScaleSum = nScale; /* level of scale_factors. */
        }
        /* Look up scale_factors from the root square table */
        /* FIXME: How to implement LookUp? */
        pScaleTable->LookUp(nScaleSum, scale_factors[ch][n][0])
        /*
         * Two scale factors transmitted if there is a transient
         */
        if (transition_mode[ch][n]>0) {
          /* Use the (Huffman) code indicated by nQSelect to decode */
          /* the quantization index of scale_factors from the bit stream */
          /* FIXME: What is Inverse Quantization(InverseQ) ? */
          QSCALES.ppQ[nQSelect]->InverseQ(InputFrame, nScale);
          /* Take care of difference encoding */
          if ( nQSelect < 5 ) /* Huffman encoded, nScale is the difference */
            nScaleSum += nScale; /* of the quantization indexes of scale_factors. */
          else /* Otherwise, nScale is the quantization */
            nScaleSum = nScale; /* level of scale_factors. */
          /* Look up scale_factors from the root square table */
          /* FIXME: How to implement LookUp? */
          pScaleTable->LookUp(nScaleSum, scale_factors[ch][n][1]);
        }
      }
    }
    /*
     * High frequency VQ subbands
     */
    for (n=high_frequency_VQ_start_subband[ch]; n<subband_activity_count[ch]; n++) {
      /* Use the code book indicated by nQSelect to decode */
      /* the quantization index of scale_factors from the bit stream */
      /* FIXME: What is Inverse Quantization(InverseQ) ? */
      QSCALES.ppQ[nQSelect]->InverseQ(InputFrame, nScale);
      /* Take care of difference encoding */
      if ( nQSelect < 5 ) /* Huffman encoded, nScale is the difference */
        nScaleSum += nScale; /* of the quantization indexes of scale_factors. */
      else /* Otherwise, nScale is the quantization */
        nScaleSum = nScale; /* level of scale_factors. */
      /* Look up scale_factors from the root square table */
      /* FIXME: How to implement LookUp? */
      pScaleTable->LookUp(nScaleSum, scale_factors[ch][n][0])
    }
  }

/* #if 0 */
/* FIXME: ALL CODE BELOW HERE does not compile yet. */


  /* Joint Subband Scale Factor Codebook Select V JOIN SHUFF 3 bits per channel */
  for (ch=0; ch<number_of_primary_audio_channels; ch++)
    if (joint_intensity_coding_index[ch]>0 ) /* Transmitted only if joint subband coding enabled. */
      joint_subband_scale_factor_codebook_select[ch] = getbits(&state,3);

  /* Scale Factors for Joint Subband Coding V JOIN SCALES variable bits */
  int nSourceCh;
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    if (joint_intensity_coding_index[ch]>0 ) { /* Only if joint subband coding enabled. */
      nSourceCh = joint_intensity_coding_index[ch]-1; /* Get source channel. joint_intensity_coding_index counts */
      /* channels as 1,2,3,4,5, so minus 1. */
      nQSelect = joint_subband_scale_factor_codebook_select[ch]; /* Select code book. */
      for (n=subband_activity_count[ch]; n<subband_activity_count[nSourceCh]; n++) {
        /* Use the code book indicated by nQSelect to decode */
        /* the quantization index of scale_factors_for_joint_subband_coding */
        /* FIXME: What is Inverse Quantization(InverseQ) ? */
        QSCALES.ppQ[nQSelect]->InverseQ(InputFrame, nJScale);
        /* Bias by 64 */
        nJScale = nJScale + 64;
        /* Look up scale_factors_for_joint_subband_coding from the joint scale table */
        /* FIXME: How to implement LookUp? */
        JScaleTbl.LookUp(nJScale, scale_factors_for_joint_subband_coding[ch][n]);
      }
    }
  }

  /* Stereo Down-Mix Coefficients NV DOWN 7 bits per coefficient */
  if ( (MIX!=0) && (number_of_primary_audio_channels>2) ) {
    /* Extract down mix indexes */
    for (ch=0; ch<number_of_primary_audio_channels; ch++) { /* Each primary channel */
      stereo_down_mix_coefficients[ch][0] = getbits(&state,7);
      stereo_down_mix_coefficients[ch][1] = getbits(&state,7);
    }
  }
  /* Look up down mix coefficients */
  for (n=0; n<subband_activity_count; n++) { /* Each active subbands */
    LeftChannel = 0;
    RightChannel = 0;
    for (ch=0; ch<number_of_primary_audio_channels; ch++) { /* Each primary channels */
      LeftChannel += stereo_down_mix_coefficients[ch][0]*Sample[Ch];
      RightChannel += stereo_down_mix_coefficients[ch][1]*Sample[Ch];
    }
  }
  /* Down mixing may also be performed on the PCM samples after the filterbank reconstruction. */

  /* Dynamic Range Coefficient NV RANGE 8 bits */
  if ( embedded_dynamic_range_flag != 0 ) {
    nIndex = getbits(&state,8);
    /* FIXME: How to implement LookUp? */
    RANGEtbl.LookUp(nIndex,dynamic_range_coefficient);
    /* The following range adjustment is to be performed */
    /* after QMF reconstruction */
    for (ch=0; ch<number_of_primary_audio_channels; ch++)
      for (n=0; n<nNumSamples; n++)
        AudioCh[ch].ReconstructedSamples[n] *= dynamic_range_coefficient;
  }

  /* Side Information CRC Check Word V SICRC 16 bits */
  if ( CPF==1 ) /* Present only if CPF=1. */
    SICRC = getbits(&state,16);

  /* B.3.3 Primary Audio Data Arrays */

  /* VQ Encoded High Frequency Subbands NV HFREQ 10 bits per applicable subbands */
  for (ch=0; ch<number_of_primary_audio_channels; ch++) {
    for (n=high_frequency_VQ_start_subband[ch]; n<subband_activity_count[ch]; n++) {
      /* Extract the VQ address from the bit stream */
      nVQIndex = getbits(&state,10);
      /* Look up the VQ code book for 32 subband samples. */
      /* FIXME: How to implement LookUp? */
      HFreqVQ.LookUp(nVQIndex, VQ_encoded_high_frequency_subbands[ch][n])
      /* Scale and take the samples */
      Scale = (real)scale_factors[ch][n][0]; /* Get the scale factor */
      for (m=0; m<subsubframe_count*8; m++, nSample++) {
        aPrmCh[ch].aSubband[n].raSample[m] = rScale*VQ_encoded_high_frequency_subbands[ch][n][m];
      }
    }
  }

  /* Low Frequency Effect Data V LFE 8 bits per sample */
  if ( low_frequency_effects_flag>0 ) { /* Present only if flagged by low_frequency_effects_flag */
    /* extract low_frequency_effect_data samples from the bit stream */
    for (n=0; n<2*low_frequency_effects_flag*subsubframe_count; n++) {
      low_frequency_effect_data[n] = (signed int)(signed char)getbits(&state,8);
      /* Use char to get sign extension because it */
      /* is 8-bit 2's compliment. */
      /* Extract scale factor index from the bit stream */
    }
    LFEscaleIndex = getbits(&state,8);
    /* Look up the 7-bit root square quantization table */
    /* FIXME: How to implement LookUp? */
    pLFE_RMS->LookUp(LFEscaleIndex,nScale);
    /* Account for the quantizer step size which is 0.035 */
    rScale = nScale*0.035;
    /* Get the actual low_frequency_effect_data samples */
    for (n=0; n<2*low_frequency_effects_flag*subsubframe_count; n++) {
      LFECh.rLFE[k] = low_frequency_effect_data[n]*rScale;
    }
    /* Interpolation low_frequency_effect_data samples */
    LFECh.InterpolationFIR(low_frequency_effects_flag); /* low_frequency_effects_flag indicates which */
    /* interpolation filter to use */
  }

  /* Audio Data V AUDIO variable bits */
  /*
   * Select quantization step size table
   */
  if ( RATE == 0x1f ) {
    pStepSizeTable = &StepSizeLossLess; /* Lossless quantization */
  } else {
    pStepSizeTable = &StepSizeLossy; /* Lossy */
  }
  /*
   * Unpack the subband samples
   */
  for (nSubSubFrame=0; nSubSubFrame<subsubframe_count; nSubSubFrame++) {
    for (ch=0; ch<number_of_primary_audio_channels; ch++) {
      for (n=0; n<high_frequency_VQ_start_subband[ch]; n++) { /* Not high frequency VQ subbands */
      /*
       * Select the mid-tread linear quantizer
       */
      nABITS = bit_allocation_index[ch][n]; /* Select the mid-tread quantizer */
      pCQGroup = &pCQGroupAUDIO[nABITS-1];/* Select the group of */
      /* code books corresponding to the */
      /* the mid-tread linear quantizer. */
      nNumQ = pCQGroupAUDIO[nABITS-1].nNumQ-1;/* Number of code */
      /* books in this group */
      /*
       * Determine quantization index code book and its type
       */
      /* Select quantization index code book */
      nSEL = quantization_index_codebook_select[ch][nABITS-1];
      /* Determine its type */
      nQType = 1; /* Assume Huffman type by default */
      if ( nSEL==nNumQ ) { /* Not Huffman type */
        if ( nABITS<=7 ) {
          nQType = 3; /* Block code */
        } else {
          nQType = 2; /* No further encoding */
        }
      }
      if ( nABITS==0 ) { /* No bits allocated */
        nQType = 0;
      }
      /*
       * Extract bits from the bit stream
       * This retrieves 8 AUDIO values
       */
      switch ( nQType ) {
        case 0: /* No bits allocated */
          for (m=0; m<8; m++)
            AUDIO[m] = 0;
          break;
        case 1: /* Huffman code */
          for (m=0; m<8; m++)
            /* FIXME: What is Inverse Quantization(InverseQ) ? */
            pCQGroup->ppQ[nSEL]->InverseQ(InputFrame,AUDIO[m]);
          break;
        case 2: /* No further encoding */
          for (m=0; m<8; m++) {
            /* Extract quantization index from the bit stream */
            /* FIXME: What is Inverse Quantization(InverseQ) ? */
            pCQGroup->ppQ[nSEL]->InverseQ(InputFrame, nCode)
            /* Take care of 2's compliment */
            AUDIO[m] = pCQGroup->ppQ[nSEL]->SignExtension(nCode);
          }
          break;
        case 3: /* Block code */
          /* Block code is just 1 value with 4 samples derived from it.
           * with each sample a digit from the number (using a base derived from nABITS via a table)
           * E.g. nABITS = 10, base = 5 (Base value taken from table.)
           * 1st sample = (value % 5) - (int(5/2); (Values between -2 and +2 )
           * 2st sample = ((value / 5) % 5) - (int(5/2);
           * 3rd sample = ((value / 25) % 5) - (int(5/2);
           * 4th sample = ((value / 125) % 5) - (int(5/2);
           * 
           */
          pCBQ = &pCBlockQ[nABITS-1]; /* Select block code book */
          m = 0;
          for (nBlock=0; nBlock<2; nBlock++) {
            /* Extract the block code index from the bit stream */
            /* FIXME: What is Inverse Quantization(InverseQ) ? */
            pCQGroup->ppQ[nSEL]->InverseQ(InputFrame, nCode)
            /* Look up 4 samples from the block code book */
            /* FIXME: How to implement LookUp? */
            pCBQ->LookUp(nCode,&AUDIO[m])
            m += 4;
          }
          break;
        default: /* Undefined */
          printf("ERROR: Unknown AUDIO quantization index code book.");
      }
      /*
       * Account for quantization step size and scale factor
       */
      /* Look up quantization step size */
      nABITS = bit_allocation_index[ch][n];
      /* FIXME: How to implement LookUp? */
      pStepSizeTable->LookUp(nABITS, rStepSize);
      /* Identify transient location */
      nTmode = transition_mode[ch][n];
      if ( nTmode == 0 ) /* No transient */
        nTmode = subsubframe_count;
      /* Determine proper scale factor */
      if (nSubSubFrame<nTmode) /* Pre-transient */
        rScale = rStepSize * scale_factors[ch][n][0]; /* Use first scale factor */
      else /* After-transient */
        rScale = rStepSize * scale_factors[ch][n][1]; /* Use second scale factor */
      /* Adjustmemt of scale factor */
      rScale *= scale_factor_adjustment_index[ch][quantization_index_codebook_select[ch][nABITS-1]]; /* scale_factor_adjustment_index[ ][ ] are assumed 1 */
      /* unless changed by bit */
      /* stream when quantization_index_codebook_select indicates */
      /* Huffman code. */
      /* Scale the samples */
      nSample = 8*nSubSubFrame; /* Set sample index */
      for (m=0; m<8; m++, nSample++)
        aPrmCh[ch].aSubband[n].aSample[nSample] = rScale*AUDIO[m];
      /*
       * Inverse ADPCM
       */
      if ( PMODE[ch][n] != 0 ) /* Only when prediction mode is on. */
        aPrmCh[ch].aSubband[n].InverseADPCM();
      /*
       * Check for DSYNC
       */
      if ( (nSubSubFrame==(subsubframe_count-1)) || (ASPF==1) ) {
        DSYNC = getbits(&state,16);
        if ( DSYNC != 0xffff )
          printf("DSYNC error at end of subsubframe #%d", nSubSubFrame);
      }
    }
  }
/* B.3.4 Unpack Optional Information */
/* TODO ^^^ */

#endif
/* CODE BELOW here does compile */

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
dts_print_decoded_data(&decoder_data);
}

#endif
