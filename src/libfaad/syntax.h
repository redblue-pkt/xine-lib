/*
** FAAD - Freeware Advanced Audio Decoder
** Copyright (C) 2002 M. Bakker
**  
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software 
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: syntax.h,v 1.5 2003/04/12 14:58:47 miguelfreitas Exp $
**/

#ifndef __SYNTAX_H__
#define __SYNTAX_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "decoder.h"
#include "drc.h"
#include "bits.h"

#define MAIN       0
#define LC         1
#define SSR        2
#define LTP        3
#define LD        23
#define ER_LC     17
#define ER_LTP    19
#define DRM_ER_LC 27 /* special object type for DRM */


/* First object type that has ER */
#define ER_OBJECT_START 17


/* Bitstream */
#define LEN_SE_ID 3
#define LEN_TAG   4
#define LEN_BYTE  8

#define EXT_FIL            0
#define EXT_FILL_DATA      1
#define EXT_DATA_ELEMENT   2
#define EXT_DYNAMIC_RANGE 11
#define ANC_DATA           0

/* Syntax elements */
#define ID_SCE 0x0
#define ID_CPE 0x1
#define ID_CCE 0x2
#define ID_LFE 0x3
#define ID_DSE 0x4
#define ID_PCE 0x5
#define ID_FIL 0x6
#define ID_END 0x7

#define ONLY_LONG_SEQUENCE   0x0
#define LONG_START_SEQUENCE  0x1
#define EIGHT_SHORT_SEQUENCE 0x2
#define LONG_STOP_SEQUENCE   0x3

#define ZERO_HCB       0
#define FIRST_PAIR_HCB 5
#define ESC_HCB        11
#define QUAD_LEN       4
#define PAIR_LEN       2
#define BOOKSCL        12
#define NOISE_HCB      13
#define INTENSITY_HCB2 14
#define INTENSITY_HCB  15


int8_t GASpecificConfig(bitfile *ld, mp4AudioSpecificConfig *mp4ASC);

uint8_t adts_frame(adts_header *adts, bitfile *ld);
void get_adif_header(adif_header *adif, bitfile *ld);


/* static declarations moved to avoid compiler warnings [MF] */

#ifdef ERROR_RESILIENCE
uint8_t reordered_spectral_data(faacDecHandle hDecoder, ic_stream *ics,
                                bitfile *ld, int16_t *spectral_data);
#endif


#ifdef __cplusplus
}
#endif
#endif
