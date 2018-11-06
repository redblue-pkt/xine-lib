/*
 * Copyright (C) 2000-2018 the xine project
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
 *
 * contents:
 *
 * buffer types management.
 * convert FOURCC and audioformattag to BUF_xxx defines
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include <xine/buffer.h>
#include <xine/xineutils.h>
#include <xine/xine_internal.h>
#include "bswap.h"

static const uint32_t sorted_audio_tags[] = {
  0x0001, BUF_AUDIO_LPCM_LE,
  0x0002, BUF_AUDIO_MSADPCM,
  0x0006, BUF_AUDIO_ALAW,
  0x0007, BUF_AUDIO_MULAW,
  0x000A, BUF_AUDIO_WMAV,
  0x0011, BUF_AUDIO_MSIMAADPCM,
  0x0022, BUF_AUDIO_TRUESPEECH,
  0x0031, BUF_AUDIO_MSGSM,
  0x0032, BUF_AUDIO_MSGSM,
  0x0050, BUF_AUDIO_MPEG,
  0x0055, BUF_AUDIO_MPEG,
  0x0061, BUF_AUDIO_DK4ADPCM,
  0x0062, BUF_AUDIO_DK3ADPCM,
  0x0075, BUF_AUDIO_VOXWARE,
  0x00ff, BUF_AUDIO_AAC,
  0x0111, BUF_AUDIO_VIVOG723,
  0x0112, BUF_AUDIO_VIVOG723,
  0x0130, BUF_AUDIO_ACELPNET,
  0x0160, BUF_AUDIO_WMAV1,
  0x0161, BUF_AUDIO_WMAV2,
  0x0162, BUF_AUDIO_WMAPRO,
  0x0163, BUF_AUDIO_WMALL,
  0x0401, BUF_AUDIO_IMC,
  0x1101, BUF_AUDIO_LH,
  0x1102, BUF_AUDIO_LH,
  0x1103, BUF_AUDIO_LH,
  0x1104, BUF_AUDIO_LH,
  0x2000, BUF_AUDIO_A52,
  0x2001, BUF_AUDIO_DTS,
  /* these formattags are used by Vorbis ACM encoder and
   * supported by NanDub, a variant of VirtualDub. */
  0x674f, BUF_AUDIO_VORBIS,
  0x6750, BUF_AUDIO_VORBIS,
  0x6751, BUF_AUDIO_VORBIS,
  0x676f, BUF_AUDIO_VORBIS,
  0x6770, BUF_AUDIO_VORBIS,
  0x6771, BUF_AUDIO_VORBIS
};

static const uint32_t sorted_audio_4ccs[] = {
  BE_FOURCC('.', 'm', 'p', '3'), BUF_AUDIO_MPEG,
  BE_FOURCC('2', '8', '_', '8'), BUF_AUDIO_28_8,
  BE_FOURCC('4', '2', 'n', 'i'), BUF_AUDIO_LPCM_LE,
  BE_FOURCC('A', 'A', 'C', ' '), BUF_AUDIO_AAC,
  BE_FOURCC('E', 'A', 'C', '3'), BUF_AUDIO_EAC3,
  BE_FOURCC('M', 'A', 'C', '3'), BUF_AUDIO_MAC3,
  BE_FOURCC('M', 'A', 'C', '6'), BUF_AUDIO_MAC6,
  BE_FOURCC('M', 'P', '3', ' '), BUF_AUDIO_MPEG,
  BE_FOURCC('M', 'P', '4', 'A'), BUF_AUDIO_AAC,
  BE_FOURCC('M', 'P', '4', 'L'), BUF_AUDIO_AAC_LATM,
  BE_FOURCC('O', 'g', 'g', 'S'), BUF_AUDIO_VORBIS,
  BE_FOURCC('O', 'g', 'g', 'V'), BUF_AUDIO_VORBIS,
  BE_FOURCC('O', 'p', 'u', 's'), BUF_AUDIO_OPUS,
  BE_FOURCC('Q', 'D', 'M', '2'), BUF_AUDIO_QDESIGN2,
  BE_FOURCC('Q', 'D', 'M', 'C'), BUF_AUDIO_QDESIGN1,
  BE_FOURCC('Q', 'c', 'l', 'p'), BUF_AUDIO_QCLP,
  BE_FOURCC('T', 'T', 'A', '1'), BUF_AUDIO_TTA,
  BE_FOURCC('W', 'V', 'P', 'K'), BUF_AUDIO_WAVPACK,
  BE_FOURCC('a', 'c', '-', '3'), BUF_AUDIO_A52,
  BE_FOURCC('a', 'd', 'u',0x55), BUF_AUDIO_MP3ADU,
  BE_FOURCC('a', 'g', 's', 'm'), BUF_AUDIO_GSM610,
  BE_FOURCC('a', 'l', 'a', 'c'), BUF_AUDIO_ALAC,
  BE_FOURCC('a', 'l', 'a', 'w'), BUF_AUDIO_ALAW,
  BE_FOURCC('a', 't', 'r', 'c'), BUF_AUDIO_ATRK,
  BE_FOURCC('c', 'o', 'o', 'k'), BUF_AUDIO_COOK,
  BE_FOURCC('d', 'n', 'e', 't'), BUF_AUDIO_DNET,
  BE_FOURCC('e', 'c', '-', '3'), BUF_AUDIO_EAC3,
  BE_FOURCC('i', 'm', 'a', '4'), BUF_AUDIO_QTIMAADPCM,
  BE_FOURCC('i', 'n', '2', '4'), BUF_AUDIO_LPCM_BE,
  BE_FOURCC('l', 'p', 'c', 'J'), BUF_AUDIO_14_4,
  BE_FOURCC('m', 'a', 'c', '3'), BUF_AUDIO_MAC3,
  BE_FOURCC('m', 'a', 'c', '6'), BUF_AUDIO_MAC6,
  BE_FOURCC('m', 'p', '4', 'a'), BUF_AUDIO_AAC,
  BE_FOURCC('r', 'a', 'a', 'c'), BUF_AUDIO_AAC,
  BE_FOURCC('r', 'a', 'c', 'p'), BUF_AUDIO_AAC,
  BE_FOURCC('r', 'a', 'w', ' '), BUF_AUDIO_LPCM_LE,
  BE_FOURCC('s', 'a', 'm', 'r'), BUF_AUDIO_AMR_NB,
  BE_FOURCC('s', 'a', 'w', 'b'), BUF_AUDIO_AMR_WB,
  BE_FOURCC('s', 'i', 'p', 'r'), BUF_AUDIO_SIPRO,
  BE_FOURCC('s', 'o', 'w', 't'), BUF_AUDIO_LPCM_LE,
  BE_FOURCC('t', 'r', 'h', 'd'), BUF_AUDIO_TRUEHD,
  BE_FOURCC('t', 'w', 'o', 's'), BUF_AUDIO_LPCM_BE,
  BE_FOURCC('u', 'l', 'a', 'w'), BUF_AUDIO_MULAW
};

uint32_t _x_formattag_to_buf_audio (uint32_t formattag) {
  uint32_t t = formattag;
  if (t & 0xffff0000) {
    uint32_t b, e, m;
#ifndef WORDS_BIGENDIAN
    t = (t >> 24) | ((t & 0x00ff0000) >> 8) | ((t & 0x0000ff00) << 8) | (t << 24);
#endif
    b = 0;
    e = sizeof (sorted_audio_4ccs) / sizeof (sorted_audio_4ccs[0]) / 2;
    m = e >> 1;
    do {
      uint32_t f = sorted_audio_4ccs[2 * m];
      if (t == f) {
        return sorted_audio_4ccs[2 * m + 1];
      } else if (t < f) {
        e = m;
      } else {
        b = m + 1;
      }
      m = (b + e) >> 1;
    } while (b != e);
    if ((t & 0xffff0000) != BE_FOURCC('m', 's', 0, 0))
      return 0;
    t &= 0xffff;
  }
  {
    uint32_t b, e, m;
    b = 0;
    e = sizeof (sorted_audio_tags) / sizeof (sorted_audio_tags[0]) / 2;
    m = e >> 1;
    do {
      uint32_t f = sorted_audio_tags[2 * m];
      if (t == f) {
        return sorted_audio_tags[2 * m + 1];
      } else if (t < f) {
        e = m;
      } else {
        b = m + 1;
      }
      m = (b + e) >> 1;
    } while (b != e);
    return 0;
  }
}

static const uint32_t sorted_video_tags[] = {
  0x0001, BUF_VIDEO_MSRLE,
  0x0002, BUF_VIDEO_MSRLE  /* MS RLE format identifiers */
};

static const uint32_t sorted_video_4ccs[] = {
  BE_FOURCC(0x02, 0, 0, 0x10),   BUF_VIDEO_MPEG,
  BE_FOURCC('3', 'I', 'V', '1'), BUF_VIDEO_3IVX,
  BE_FOURCC('3', 'I', 'V', '2'), BUF_VIDEO_3IVX,
  BE_FOURCC('3', 'I', 'V', 'D'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('8', 'B', 'P', 'S'), BUF_VIDEO_8BPS,
  BE_FOURCC('A', 'A', 'S', 'C'), BUF_VIDEO_AASC,
  BE_FOURCC('A', 'P', '4', '1'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('A', 'S', 'V', '1'), BUF_VIDEO_ASV1,
  BE_FOURCC('A', 'S', 'V', '2'), BUF_VIDEO_ASV2,
  BE_FOURCC('A', 'V', 'D', 'J'), BUF_VIDEO_MJPEG,
  BE_FOURCC('A', 'V', 'R', 'n'), BUF_VIDEO_MJPEG,
  BE_FOURCC('C', 'O', 'L', '1'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('C', 'R', 'A', 'M'), BUF_VIDEO_MSVC,
  BE_FOURCC('C', 'S', 'C', 'D'), BUF_VIDEO_CSCD,
  BE_FOURCC('C', 'Y', 'U', 'V'), BUF_VIDEO_CYUV,
  BE_FOURCC('D', 'I', 'B', ' '), BUF_VIDEO_RGB, /* device-independent bitmap */
  BE_FOURCC('D', 'I', 'V', '2'), BUF_VIDEO_MSMPEG4_V2,
  BE_FOURCC('D', 'I', 'V', '3'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('D', 'I', 'V', '4'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('D', 'I', 'V', '5'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('D', 'I', 'V', '6'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('D', 'I', 'V', 'X'), BUF_VIDEO_MPEG4,
  BE_FOURCC('D', 'U', 'C', 'K'), BUF_VIDEO_DUCKTM1,
  BE_FOURCC('D', 'V', 'S', 'D'), BUF_VIDEO_DV,
  BE_FOURCC('D', 'X', '5', '0'), BUF_VIDEO_DIVX5,
  BE_FOURCC('D', 'i', 'v', 'X'), BUF_VIDEO_MPEG4,
  BE_FOURCC('D', 'i', 'v', 'x'), BUF_VIDEO_MPEG4,
  BE_FOURCC('F', 'M', 'P', '4'), BUF_VIDEO_MPEG4,
  BE_FOURCC('F', 'P', 'S', '1'), BUF_VIDEO_FPS1,
  BE_FOURCC('G', 'R', 'E', 'Y'), BUF_VIDEO_GREY,
  BE_FOURCC('H', '2', '6', '3'), BUF_VIDEO_H263,
  BE_FOURCC('H', '2', '6', '4'), BUF_VIDEO_H264,
  BE_FOURCC('H', 'F', 'Y', 'U'), BUF_VIDEO_HUFFYUV,
  BE_FOURCC('I', '2', '6', '3'), BUF_VIDEO_I263,
  BE_FOURCC('I', '4', '2', '0'), BUF_VIDEO_I420,
  BE_FOURCC('I', 'M', 'G', ' '), BUF_VIDEO_IMAGE,
  BE_FOURCC('I', 'V', '3', '1'), BUF_VIDEO_IV31,
  BE_FOURCC('I', 'V', '3', '2'), BUF_VIDEO_IV32,
  BE_FOURCC('I', 'V', '4', '1'), BUF_VIDEO_IV41,
  BE_FOURCC('I', 'V', '5', '0'), BUF_VIDEO_IV50,
  BE_FOURCC('I', 'Y', 'U', 'V'), BUF_VIDEO_I420,
  BE_FOURCC('J', 'F', 'I', 'F'), BUF_VIDEO_JPEG,
  BE_FOURCC('K', 'M', 'V', 'C'), BUF_VIDEO_KMVC,
  BE_FOURCC('L', 'O', 'C', 'O'), BUF_VIDEO_LOCO,
  BE_FOURCC('M', '4', 'S', '2'), BUF_VIDEO_MPEG4,
  BE_FOURCC('M', 'J', 'P', 'G'), BUF_VIDEO_MJPEG,
  BE_FOURCC('M', 'P', '4', '1'), BUF_VIDEO_MSMPEG4_V1,
/*BE_FOURCC('M', 'P', '4', '1'), BUF_VIDEO_MSMPEG4_V2,*/
  BE_FOURCC('M', 'P', '4', '2'), BUF_VIDEO_MSMPEG4_V2,
  BE_FOURCC('M', 'P', '4', '3'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('M', 'P', '4', 'S'), BUF_VIDEO_MPEG4,
  BE_FOURCC('M', 'P', 'E', 'G'), BUF_VIDEO_MPEG,
  BE_FOURCC('M', 'P', 'G', '3'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('M', 'P', 'G', '4'), BUF_VIDEO_MSMPEG4_V1,
  BE_FOURCC('M', 'S', 'S', '1'), BUF_VIDEO_MSS1,
  BE_FOURCC('M', 'S', 'V', 'C'), BUF_VIDEO_MSVC,
  BE_FOURCC('M', 'S', 'Z', 'H'), BUF_VIDEO_MSZH,
  BE_FOURCC('M', 'V', 'I', '2'), BUF_VIDEO_MVI2,
  BE_FOURCC('P', 'G', 'V', 'V'), BUF_VIDEO_PGVV,
  BE_FOURCC('P', 'I', 'M', '1'), BUF_VIDEO_MPEG,
  BE_FOURCC('P', 'I', 'X', 'L'), BUF_VIDEO_XL,
  BE_FOURCC('Q', '1', '.', '0'), BUF_VIDEO_QPEG,
  BE_FOURCC('Q', '1', '.', '1'), BUF_VIDEO_QPEG,
  BE_FOURCC('Q', 'P', 'E', 'G'), BUF_VIDEO_QPEG,
  BE_FOURCC('R', 'T', '2', '1'), BUF_VIDEO_RT21,
  BE_FOURCC('R', 'V', '1', '0'), BUF_VIDEO_RV10,
  BE_FOURCC('R', 'V', '2', '0'), BUF_VIDEO_RV20,
  BE_FOURCC('R', 'V', '3', '0'), BUF_VIDEO_RV30,
  BE_FOURCC('R', 'V', '4', '0'), BUF_VIDEO_RV40,
  BE_FOURCC('S', 'E', 'G', 'A'), BUF_VIDEO_SEGA,
  BE_FOURCC('S', 'N', 'O', 'W'), BUF_VIDEO_SNOW,
  BE_FOURCC('S', 'V', 'Q', '1'), BUF_VIDEO_SORENSON_V1,
  BE_FOURCC('S', 'V', 'Q', '3'), BUF_VIDEO_SORENSON_V3,
  BE_FOURCC('T', 'M', '2', '0'), BUF_VIDEO_DUCKTM2,
  BE_FOURCC('U', '2', '6', '3'), BUF_VIDEO_H263,
  BE_FOURCC('U', 'C', 'O', 'D'), BUF_VIDEO_UCOD,
  BE_FOURCC('U', 'L', 'T', 'I'), BUF_VIDEO_ULTI,
  BE_FOURCC('V', 'C', 'R', '1'), BUF_VIDEO_ATIVCR1,
  BE_FOURCC('V', 'C', 'R', '2'), BUF_VIDEO_ATIVCR2,
  BE_FOURCC('V', 'I', 'V', 'O'), BUF_VIDEO_I263,
  BE_FOURCC('V', 'M', 'n', 'c'), BUF_VIDEO_VMNC,
  BE_FOURCC('V', 'P', '3', ' '), BUF_VIDEO_VP31,
  BE_FOURCC('V', 'P', '3', '0'), BUF_VIDEO_VP31,
  BE_FOURCC('V', 'P', '3', '1'), BUF_VIDEO_VP31,
  BE_FOURCC('V', 'P', '4', '0'), BUF_VIDEO_VP4,
  BE_FOURCC('V', 'P', '5', '0'), BUF_VIDEO_VP5,
  BE_FOURCC('V', 'P', '6', '0'), BUF_VIDEO_VP6,
  BE_FOURCC('V', 'P', '6', '1'), BUF_VIDEO_VP6,
  BE_FOURCC('V', 'P', '6', '2'), BUF_VIDEO_VP6,
  BE_FOURCC('V', 'P', '6', 'F'), BUF_VIDEO_VP6F,
  BE_FOURCC('V', 'P', '8', '0'), BUF_VIDEO_VP8,
  BE_FOURCC('V', 'P', '9', '0'), BUF_VIDEO_VP9,
  BE_FOURCC('W', 'H', 'A', 'M'), BUF_VIDEO_MSVC,
  BE_FOURCC('W', 'M', 'V', '1'), BUF_VIDEO_WMV7,
  BE_FOURCC('W', 'M', 'V', '2'), BUF_VIDEO_WMV8,
  BE_FOURCC('W', 'M', 'V', '3'), BUF_VIDEO_WMV9,
  BE_FOURCC('W', 'M', 'V', 'A'), BUF_VIDEO_VC1,
  BE_FOURCC('W', 'M', 'V', 'P'), BUF_VIDEO_WMV9,
  BE_FOURCC('W', 'N', 'V', '1'), BUF_VIDEO_WNV1,
  BE_FOURCC('W', 'V', 'C', '1'), BUF_VIDEO_VC1,
  BE_FOURCC('X', '2', '6', '4'), BUF_VIDEO_H264,
  BE_FOURCC('X', 'I', 'X', 'L'), BUF_VIDEO_XL,
  BE_FOURCC('X', 'V', 'I', 'D'), BUF_VIDEO_XVID,
  BE_FOURCC('X', 'X', 'A', 'N'), BUF_VIDEO_XXAN,
  BE_FOURCC('X', 'x', 'a', 'n'), BUF_VIDEO_XXAN,
  BE_FOURCC('Y', 'U', 'Y', '2'), BUF_VIDEO_YUY2,
  BE_FOURCC('Y', 'V', '1', '2'), BUF_VIDEO_YV12,
  BE_FOURCC('Y', 'V', 'U', '9'), BUF_VIDEO_YVU9,
  BE_FOURCC('Z', 'L', 'I', 'B'), BUF_VIDEO_ZLIB,
  BE_FOURCC('Z', 'M', 'B', 'V'), BUF_VIDEO_ZMBV,
  BE_FOURCC('Z', 'y', 'G', 'o'), BUF_VIDEO_ZYGO,
  BE_FOURCC('a', 'v', '0', '1'), BUF_VIDEO_AV1,
  BE_FOURCC('a', 'v', 'c', '1'), BUF_VIDEO_H264,
  BE_FOURCC('a', 'z', 'p', 'r'), BUF_VIDEO_RPZA,
  BE_FOURCC('c', 'r', 'a', 'm'), BUF_VIDEO_MSVC,
  BE_FOURCC('c', 'v', 'i', 'd'), BUF_VIDEO_CINEPAK,
  BE_FOURCC('c', 'y', 'u', 'v'), BUF_VIDEO_CYUV,
  BE_FOURCC('d', 'i', 'v', '2'), BUF_VIDEO_MSMPEG4_V2,
  BE_FOURCC('d', 'i', 'v', '3'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('d', 'i', 'v', '4'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('d', 'i', 'v', '5'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('d', 'i', 'v', '6'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('d', 'i', 'v', 'x'), BUF_VIDEO_MPEG4,
  BE_FOURCC('d', 'm', 'b', '1'), BUF_VIDEO_MJPEG,
  BE_FOURCC('d', 'v', 'c', 'p'), BUF_VIDEO_DV,
  BE_FOURCC('d', 'v', 's', 'd'), BUF_VIDEO_DV,
  BE_FOURCC('g', 'i', 'f', ' '), BUF_VIDEO_IMAGE,
  BE_FOURCC('h', '2', '6', '3'), BUF_VIDEO_H263,
  BE_FOURCC('h', '2', '6', '4'), BUF_VIDEO_H264,
  BE_FOURCC('h', 'e', 'v', '1'), BUF_VIDEO_HEVC,
  BE_FOURCC('h', 'e', 'v', 'c'), BUF_VIDEO_HEVC,
  BE_FOURCC('h', 'v', 'c', '1'), BUF_VIDEO_HEVC,
  BE_FOURCC('i', '2', '6', '3'), BUF_VIDEO_I263,
  BE_FOURCC('i', 'v', '3', '1'), BUF_VIDEO_IV31,
  BE_FOURCC('i', 'v', '3', '2'), BUF_VIDEO_IV32,
  BE_FOURCC('i', 'v', '4', '1'), BUF_VIDEO_IV41,
  BE_FOURCC('i', 'v', '5', '0'), BUF_VIDEO_IV50,
  BE_FOURCC('j', 'p', 'e', 'g'), BUF_VIDEO_JPEG,
  BE_FOURCC('m', '4', 's', '2'), BUF_VIDEO_MPEG4,
  BE_FOURCC('m', 'j', 'p', 'a'), BUF_VIDEO_MJPEG,
  BE_FOURCC('m', 'j', 'p', 'b'), BUF_VIDEO_MJPEG_B,
  BE_FOURCC('m', 'p', '4', '1'), BUF_VIDEO_MSMPEG4_V1,
/*BE_FOURCC('m', 'p', '4', '1'), BUF_VIDEO_MSMPEG4_V2,*/
  BE_FOURCC('m', 'p', '4', '2'), BUF_VIDEO_MSMPEG4_V2,
  BE_FOURCC('m', 'p', '4', '3'), BUF_VIDEO_MSMPEG4_V3,
  BE_FOURCC('m', 'p', '4', 'v'), BUF_VIDEO_MPEG4,
  BE_FOURCC('m', 'p', 'e', 'g'), BUF_VIDEO_MPEG,
  BE_FOURCC('m', 'p', 'g', '1'), BUF_VIDEO_MPEG,
  BE_FOURCC('m', 'p', 'g', '2'), BUF_VIDEO_MPEG,
  BE_FOURCC('m', 'p', 'g', '4'), BUF_VIDEO_MSMPEG4_V1,
  BE_FOURCC('m', 's', 'v', 'c'), BUF_VIDEO_MSVC,
  BE_FOURCC('m', 'v', 'i', '2'), BUF_VIDEO_MVI2,
  BE_FOURCC('p', 'n', 'g', ' '), BUF_VIDEO_PNG,
  BE_FOURCC('q', 'd', 'r', 'w'), BUF_VIDEO_QDRW,
  BE_FOURCC('r', 'a', 'w', ' '), BUF_VIDEO_RGB,
  BE_FOURCC('r', 'l', 'e', ' '), BUF_VIDEO_QTRLE,
  BE_FOURCC('r', 'p', 'z', 'a'), BUF_VIDEO_RPZA,
  BE_FOURCC('s', '2', '6', '3'), BUF_VIDEO_H263,
  BE_FOURCC('s', 'e', 'g', 'a'), BUF_VIDEO_SEGA,
  BE_FOURCC('s', 'm', 'c', ' '), BUF_VIDEO_SMC,
  BE_FOURCC('s', 'v', 'q', '1'), BUF_VIDEO_SORENSON_V1,
  BE_FOURCC('s', 'v', 'q', '3'), BUF_VIDEO_SORENSON_V3,
  BE_FOURCC('s', 'v', 'q', 'i'), BUF_VIDEO_SORENSON_V1,
  BE_FOURCC('t', 's', 'c', 'c'), BUF_VIDEO_TSCC,
  BE_FOURCC('u', 'c', 'o', 'd'), BUF_VIDEO_UCOD,
  BE_FOURCC('v', 'c', '-', '1'), BUF_VIDEO_VC1,
  BE_FOURCC('v', 'i', 'v', '1'), BUF_VIDEO_I263,
  BE_FOURCC('v', 'i', 'v', 'o'), BUF_VIDEO_I263,
  BE_FOURCC('v', 'p', '3', '0'), BUF_VIDEO_VP31,
  BE_FOURCC('v', 'p', '3', '1'), BUF_VIDEO_VP31,
  BE_FOURCC('w', 'h', 'a', 'm'), BUF_VIDEO_MSVC,
  BE_FOURCC('x', '2', '6', '4'), BUF_VIDEO_H264,
  BE_FOURCC('x', 'v', 'i', 'd'), BUF_VIDEO_XVID,
  BE_FOURCC('x', 'x', 'a', 'n'), BUF_VIDEO_XXAN,
  BE_FOURCC('y', 'u', 'v', '2'), BUF_VIDEO_YUY2,
  BE_FOURCC('y', 'u', 'y', '2'), BUF_VIDEO_YUY2,
  BE_FOURCC('y', 'v', '1', '2'), BUF_VIDEO_YV12
};

uint32_t _x_fourcc_to_buf_video (uint32_t formattag) {
  uint32_t t = formattag;
  if (t & 0xffff0000) {
    uint32_t b, e, m;
#ifndef WORDS_BIGENDIAN
    t = (t >> 24) | ((t & 0x00ff0000) >> 8) | ((t & 0x0000ff00) << 8) | (t << 24);
#endif
    b = 0;
    e = sizeof (sorted_video_4ccs) / sizeof (sorted_video_4ccs[0]) / 2;
    m = e >> 1;
    do {
      uint32_t f = sorted_video_4ccs[2 * m];
      if (t == f) {
        return sorted_video_4ccs[2 * m + 1];
      } else if (t < f) {
        e = m;
      } else {
        b = m + 1;
      }
      m = (b + e) >> 1;
    } while (b != e);
    return 0;
  }
  {
    uint32_t b, e, m;
    b = 0;
    e = sizeof (sorted_video_tags) / sizeof (sorted_video_tags[0]) / 2;
    m = e >> 1;
    do {
      uint32_t f = sorted_video_tags[2 * m];
      if (t == f) {
        return sorted_video_tags[2 * m + 1];
      } else if (t < f) {
        e = m;
      } else {
        b = m + 1;
      }
      m = (b + e) >> 1;
    } while (b != e);
    return 0;
  }
}

static const char * const video_names[] = {
  /* 00 BUF_VIDEO_MPEG        */ "MPEG 1/2",
  /* 01 BUF_VIDEO_MPEG4       */ "ISO-MPEG4/OpenDivx",
  /* 02 BUF_VIDEO_CINEPAK     */ "Cinepak",
  /* 03 BUF_VIDEO_SORENSON_V1 */ "Sorenson Video 1",
  /* 04 BUF_VIDEO_MSMPEG4_V2  */ "Microsoft MPEG-4 v2",
  /* 05 BUF_VIDEO_MSMPEG4_V3  */ "Microsoft MPEG-4 v3",
  /* 06 BUF_VIDEO_MJPEG       */ "Motion JPEG",
  /* 07 BUF_VIDEO_IV50        */ "Indeo Video 5.0",
  /* 08 BUF_VIDEO_IV41        */ "Indeo Video 4.1",
  /* 09 BUF_VIDEO_IV32        */ "Indeo Video 3.2",
  /* 0a BUF_VIDEO_IV31        */ "Indeo Video 3.1",
  /* 0b BUF_VIDEO_ATIVCR1     */ "ATI VCR1",
  /* 0c BUF_VIDEO_ATIVCR2     */ "ATI VCR2",
  /* 0d BUF_VIDEO_I263        */ "I263",
  /* 0e BUF_VIDEO_RV10        */ "Real Video 1.0",
  /* 0f BUF_VIDEO_??          */ "(unused type 0x0f)",
  /* 10 BUF_VIDEO_RGB         */ "Raw RGB",
  /* 11 BUF_VIDEO_YUY2        */ "YUY2",
  /* 12 BUF_VIDEO_JPEG        */ "JPEG",
  /* 13 BUF_VIDEO_WMV7        */ "Windows Media Video 7",
  /* 14 BUF_VIDEO_WMV8        */ "Windows Media Video 8",
  /* 15 BUF_VIDEO_MSVC        */ "Microsoft Video 1",
  /* 16 BUF_VIDEO_DV          */ "Sony Digital Video (DV)",
  /* 17 BUF_VIDEO_REAL        */ "REAL",
  /* 18 BUF_VIDEO_VP31        */ "On2 VP3.1",
  /* 19 BUF_VIDEO_H263        */ "H263",
  /* 1a BUF_VIDEO_3IVX        */ "3ivx MPEG-4",
  /* 1b BUF_VIDEO_CYUV        */ "Creative YUV",
  /* 1c BUF_VIDEO_DIVX5       */ "DivX 5",
  /* 1d BUF_VIDEO_XVID        */ "XviD",
  /* 1e BUF_VIDEO_SMC         */ "Apple Quicktime Graphics (SMC)",
  /* 1f BUF_VIDEO_RPZA        */ "Apple Quicktime (RPZA)",
  /* 20 BUF_VIDEO_QTRLE       */ "Apple Quicktime Animation (RLE)",
  /* 21 BUF_VIDEO_MSRLE       */ "Microsoft RLE",
  /* 22 BUF_VIDEO_DUCKTM1     */ "Duck Truemotion v1",
  /* 23 BUF_VIDEO_FLI         */ "FLI",
  /* 24 BUF_VIDEO_ROQ         */ "Id Software RoQ",
  /* 25 BUF_VIDEO_SORENSON_V3 */ "Sorenson Video 3",
  /* 26 BUF_VIDEO_MSMPEG4_V1  */ "Microsoft MPEG-4 v1",
  /* 27 BUF_VIDEO_MSS1        */ "Windows Screen Video",
  /* 28 BUF_VIDEO_IDCIN       */ "Id Software CIN",
  /* 29 BUF_VIDEO_PGVV        */ "Radius Studio",
  /* 2a BUF_VIDEO_ZYGO        */ "ZyGo Video",
  /* 2b BUF_VIDEO_TSCC        */ "TechSmith Screen Capture Codec",
  /* 2c BUF_VIDEO_YVU9        */ "Raw YVU9 Planar Data",
  /* 2d BUF_VIDEO_VQA         */ "Westwood Studios VQA",
  /* 2e BUF_VIDEO_GREY        */ "Raw Greyscale",
  /* 2f BUF_VIDEO_XXAN        */ "Wing Commander IV Video Codec",
  /* 30 BUF_VIDEO_WC3         */ "Xan WC3",
  /* 31 BUF_VIDEO_YV12        */ "Raw Planar YV12",
  /* 32 BUF_VIDEO_SEGA        */ "Cinepak for Sega",
  /* 33 BUF_VIDEO_RV20        */ "Real Video 2.0",
  /* 34 BUF_VIDEO_RV30        */ "Real Video 3.0",
  /* 35 BUF_VIDEO_MVI2        */ "Motion Pixels",
  /* 36 BUF_VIDEO_UCOD        */ "ClearVideo",
  /* 37 BUF_VIDEO_WMV9        */ "Windows Media Video 9",
  /* 38 BUF_VIDEO_INTERPLAY   */ "Interplay MVE",
  /* 39 BUF_VIDEO_RV40        */ "Real Video 4.0",
  /* 3a BUF_VIDEO_PSX_MDEC    */ "PSX MEDC",
  /* 3b BUF_VIDEO_YUV_FRAMES  */ "Uncompressed YUV",
  /* 3c BUF_VIDEO_HUFFYUV     */ "HuffYUV",
  /* 3d BUF_VIDEO_IMAGE       */ "Image",
  /* 3e BUF_VIDEO_THEORA      */ "Ogg Theora",
  /* 3f BUF_VIDEO_4XM         */ "4X Video",
  /* 40 BUF_VIDEO_I420        */ "Raw Planar I420",
  /* 41 BUF_VIDEO_VP4         */ "On2 VP4",
  /* 42 BUF_VIDEO_VP5         */ "On2 VP5",
  /* 43 BUF_VIDEO_VP6         */ "On2 VP6",
  /* 44 BUF_VIDEO_VMD         */ "Sierra VMD Video",
  /* 45 BUF_VIDEO_MSZH        */ "MSZH Video",
  /* 46 BUF_VIDEO_ZLIB        */ "ZLIB Video",
  /* 47 BUF_VIDEO_8BPS        */ "Planar RGB",
  /* 48 BUF_VIDEO_ASV1        */ "ASV v1 Video",
  /* 49 BUF_VIDEO_ASV2        */ "ASV v2 Video",
  /* 4a BUF_VIDEO_BITPLANE    */ "Amiga picture",
  /* 4b BUF_VIDEO_BITPLANE_BR1*/ "Amiga picture",
  /* 4c BUF_VIDEO_FLV1        */ "Flash video 1",
  /* 4d BUF_VIDEO_H264        */ "Advanced Video Coding (H264)",
  /* 4e BUF_VIDEO_MJPEG_B     */ "Motion JPEG B",
  /* 4f BUF_VIDEO_H261        */ "H.261",
  /* 50 BUF_VIDEO_AASC        */ "Autodesk Animator Studio Codec",
  /* 51 BUF_VIDEO_LOCO        */ "LOCO",
  /* 52 BUF_VIDEO_QDRW        */ "QuickDraw",
  /* 53 BUF_VIDEO_QPEG        */ "Q-Team QPEG Video",
  /* 54 BUF_VIDEO_ULTI        */ "IBM UltiMotion",
  /* 55 BUF_VIDEO_WNV1        */ "Winnow Video",
  /* 56 BUF_VIDEO_XL          */ "Miro/Pinnacle VideoXL",
  /* 57 BUF_VIDEO_RT21        */ "Indeo/RealTime 2",
  /* 58 BUF_VIDEO_FPS1        */ "Fraps FPS1",
  /* 59 BUF_VIDEO_DUCKTM2     */ "Duck TrueMotion 2",
  /* 5a BUF_VIDEO_CSCD        */ "CamStudio",
  /* 5b BUF_VIDEO_ALGMM       */ "American Laser Games MM",
  /* 5c BUF_VIDEO_ZMBV        */ "Zip Motion Blocks Video",
  /* 5d BUF_VIDEO_AVS         */ "AVS",
  /* 5e BUF_VIDEO_SMACKER     */ "Smacker",
  /* 5f BUF_VIDEO_NUV         */ "NullSoft Video",
  /* 60 BUF_VIDEO_KMVC        */ "Karl Morton's Video Codec",
  /* 61 BUF_VIDEO_FLASHSV     */ "Flash Screen Video 1",
  /* 62 BUF_VIDEO_CAVS        */ "Chinese AVS",
  /* 63 BUF_VIDEO_VP6F        */ "On2 VP6 with alpha channel",
  /* 64 BUF_VIDEO_THEORA_RAW  */ "Theora",
  /* 65 BUF_VIDEO_VC1         */ "Windows Media Video VC-1",
  /* 66 BUF_VIDEO_VMNC        */ "VMware Screen Codec",
  /* 67 BUF_VIDEO_SNOW        */ "Snow",
  /* 68 BUF_VIDEO_VP8         */ "On2 VP8",
  /* 69 BUF_VIDEO_VP9         */ "VP9",
  /* 6a BUF_VIDEO_HEVC        */ "HEVC",
  /* 6b BUF_VIDEO_AV1         */ "AV1",
  /* 6c BUF_VIDEO_PNG         */ "PNG",
};

const char *_x_buf_video_name (uint32_t buf_type) {
  if ((buf_type & 0xff000000) != BUF_VIDEO_BASE)
    return "";
  buf_type = (buf_type >> 16) & 0xff;
  if (buf_type >= sizeof (video_names) / sizeof (video_names[0]))
    return "";
  return video_names[buf_type];
}

static const char * const audio_names[] = {
  /* 00 BUF_AUDIO_A52         */ "AC3/A52",
  /* 01 BUF_AUDIO_MPEG        */ "MPEG layer 1/2/3",
  /* 02 BUF_AUDIO_LPCM_BE     */ "Linear PCM big endian",
  /* 03 BUF_AUDIO_LPCM_LE     */ "Linear PCM little endian",
  /* 04 BUF_AUDIO_WMAV1       */ "Windows Media Audio v1",
  /* 05 BUF_AUDIO_DTS         */ "Digitales TonSystem (DTS)",
  /* 06 BUF_AUDIO_MSADPCM     */ "MS ADPCM",
  /* 07 BUF_AUDIO_MSIMAADPCM  */ "MS IMA ADPCM",
  /* 08 BUF_AUDIO_MSGSM       */ "MS GSM",
  /* 09 BUF_AUDIO_VORBIS      */ "OggVorbis Audio",
  /* 0a BUF_AUDIO_IMC         */ "Intel Music Coder",
  /* 0b BUF_AUDIO_LH          */ "Lernout & Hauspie",
  /* 0c BUF_AUDIO_VOXWARE     */ "Voxware Metasound",
  /* 0d BUF_AUDIO_ACELPNET    */ "ACELP.net",
  /* 0e BUF_AUDIO_AAC         */ "Advanced Audio Coding (MPEG-4 AAC)",
  /* 0f BUF_AUDIO_DNET        */ "RealAudio DNET",
  /* 10 BUF_AUDIO_VIVOG723    */ "Vivo G.723/Siren Audio Codec",
  /* 11 BUF_AUDIO_DK3ADPCM    */ "Duck DK3 ADPCM (rogue format number)",
  /* 12 BUF_AUDIO_DK4ADPCM    */ "Duck DK4 ADPCM (rogue format number)",
  /* 13 BUF_AUDIO_ROQ         */ "RoQ DPCM",
  /* 14 BUF_AUDIO_QTIMAADPCM  */ "QT IMA ADPCM",
  /* 15 BUF_AUDIO_MAC3        */ "Apple MACE 3:1 Audio",
  /* 16 BUF_AUDIO_MAC6        */ "Apple MACE 6:1 Audio",
  /* 17 BUF_AUDIO_QDESIGN1    */ "QDesign Audio v1",
  /* 18 BUF_AUDIO_QDESIGN2    */ "QDesign Audio v2",
  /* 19 BUF_AUDIO_QCLP        */ "Qualcomm PureVoice",
  /* 1a BUF_AUDIO_SMJPEG_IMA  */ "SMJPEG IMA",
  /* 1b BUF_AUDIO_VQA_IMA     */ "Westwood Studios IMA",
  /* 1c BUF_AUDIO_MULAW       */ "mu-law logarithmic PCM",
  /* 1d BUF_AUDIO_ALAW        */ "A-law logarithmic PCM",
  /* 1e BUF_AUDIO_GSM610      */ "GSM 6.10",
  /* 1f BUF_AUDIO_EA_ADPCM    */ "EA ADPCM",
  /* 20 BUF_AUDIO_WMAV2       */ "Windows Media Audio v2",
  /* 21 BUF_AUDIO_COOK        */ "RealAudio COOK",
  /* 22 BUF_AUDIO_ATRK        */ "RealAudio ATRK",
  /* 23 BUF_AUDIO_14_4        */ "RealAudio 14.4",
  /* 24 BUF_AUDIO_28_8        */ "RealAudio 28.8",
  /* 25 BUF_AUDIO_SIPRO       */ "RealAudio SIPRO",
  /* 26 BUF_AUDIO_WMAPRO      */ "Windows Media Audio Professional",
  /* 27 BUF_AUDIO_INTERPLAY   */ "Interplay DPCM",
  /* 28 BUF_AUDIO_XA_ADPCM    */ "XA ADPCM",
  /* 29 BUF_AUDIO_WESTWOOD    */ "Westwood",
  /* 2a BUF_AUDIO_DIALOGIC_IMA*/ "DIALOGIC IMA",
  /* 2b BUF_AUDIO_NSF         */ "Nosefart",
  /* 2c BUF_AUDIO_FLAC        */ "Free Lossless Audio Codec (FLAC)",
  /* 2d BUF_AUDIO_DV          */ "DV Audio",
  /* 2e BUF_AUDIO_WMAV        */ "Windows Media Audio Voice",
  /* 2f BUF_AUDIO_SPEEX       */ "Speex",
  /* 30 BUF_AUDIO_RAWPCM      */ "Raw PCM",
  /* 31 BUF_AUDIO_4X_ADPCM    */ "4x ADPCM",
  /* 32 BUF_AUDIO_VMD         */ "VMD",
  /* 33 BUF_AUDIO_XAN_DPCM    */ "XAN DPCM",
  /* 34 BUF_AUDIO_ALAC        */ "Apple Lossless Audio Codec",
  /* 35 BUF_AUDIO_MPC         */ "Musepack",
  /* 36 BUF_AUDIO_SHORTEN     */ "Shorten",
  /* 37 BUF_AUDIO_WESTWOOD_SND1*/ "Westwood",
  /* 38 BUF_AUDIO_WMALL       */ "Windows Media Audio Lossless",
  /* 39 BUF_AUDIO_TRUESPEECH  */ "Truespeech",
  /* 3a BUF_AUDIO_TTA         */ "True Audio Lossless",
  /* 3b BUF_AUDIO_SMACKER     */ "Smacker",
  /* 3c BUF_AUDIO_FLVADPCM    */ "FLV ADPCM",
  /* 3d BUF_AUDIO_WAVPACK     */ "Wavpack",
  /* 3e BUF_AUDIO_MP3ADU      */ "MPEG layer-3 adu",
  /* 3f BUF_AUDIO_AMR_NB      */ "AMR narrow band",
  /* 40 BUF_AUDIO_AMR_WB      */ "AMR wide band",
  /* 41 BUF_AUDIO_EAC3        */ "E-AC-3",
  /* 42 BUF_AUDIO_AAC_LATM    */ "AAC LATM",
  /* 43 BUF_AUDIO_ADPCM_G726  */ "ADPCM G.726",
  /* 44 BUF_AUDIO_OPUS        */ "Opus Audio",
  /* 45 BUF_AUDIO_TRUEHD      */ "TrueHD Audio"
};

const char *_x_buf_audio_name (uint32_t buf_type) {
  if ((buf_type & 0xff000000) != BUF_AUDIO_BASE)
    return "";
  buf_type = (buf_type >> 16) & 0xff;
  if (buf_type >= sizeof (audio_names) / sizeof (audio_names[0]))
    return "";
  return audio_names[buf_type];
}

static void code_to_text (char ascii[5], uint32_t code)
{
  int i;
  for (i = 0; i < 4; ++i)
  {
    int byte = code & 0xFF;
    ascii[i] = (byte < ' ') ? ' ' : (byte >= 0x7F) ? '.' : (char) byte;
    code >>= 8;
  }
  ascii[4] = 0;
}

void _x_report_video_fourcc (xine_t *xine, const char *module, uint32_t code)
{
  if (code)
  {
    char ascii[5];
    code_to_text (ascii, code);
    xprintf (xine, XINE_VERBOSITY_LOG,
             _("%s: unknown video FourCC code %#x \"%s\"\n"),
             module, code, ascii);
  }
}

void _x_report_audio_format_tag (xine_t *xine, const char *module, uint32_t code)
{
  if (code)
  {
    char ascii[5];
    code_to_text (ascii, code);
    xprintf (xine, XINE_VERBOSITY_LOG,
             _("%s: unknown audio format tag code %#x \"%s\"\n"),
             module, code, ascii);
  }
}


void _x_bmiheader_le2me( xine_bmiheader *bih ) {
  /* OBS: fourcc must be read using machine endianness
   *      so don't play with biCompression here!
   */

  bih->biSize = le2me_32(bih->biSize);
  bih->biWidth = le2me_32(bih->biWidth);
  bih->biHeight = le2me_32(bih->biHeight);
  bih->biPlanes = le2me_16(bih->biPlanes);
  bih->biBitCount = le2me_16(bih->biBitCount);
  bih->biSizeImage = le2me_32(bih->biSizeImage);
  bih->biXPelsPerMeter = le2me_32(bih->biXPelsPerMeter);
  bih->biYPelsPerMeter = le2me_32(bih->biYPelsPerMeter);
  bih->biClrUsed = le2me_32(bih->biClrUsed);
  bih->biClrImportant = le2me_32(bih->biClrImportant);
}

void _x_waveformatex_le2me( xine_waveformatex *wavex ) {

  wavex->wFormatTag = le2me_16(wavex->wFormatTag);
  wavex->nChannels = le2me_16(wavex->nChannels);
  wavex->nSamplesPerSec = le2me_32(wavex->nSamplesPerSec);
  wavex->nAvgBytesPerSec = le2me_32(wavex->nAvgBytesPerSec);
  wavex->nBlockAlign = le2me_16(wavex->nBlockAlign);
  wavex->wBitsPerSample = le2me_16(wavex->wBitsPerSample);
  wavex->cbSize = le2me_16(wavex->cbSize);
}

size_t _x_tag32_me2str (char *s, uint32_t tag) {
  static const uint8_t tab_hex[16] = "0123456789abcdef";
  union {uint32_t w; uint8_t b[4];} u;
  uint8_t *q = (uint8_t *)s;
  int i;
  if (!q)
    return 0;
  u.w = tag;
  for (i = 0; i < 4; i++) {
    uint8_t z = u.b[i];
    if ((z < 32) || (z > 127)) {
      *q++ = '\\';
      *q++ = 'x';
      *q++ = tab_hex[z >> 4];
      *q++ = tab_hex[z & 15];
    } else if (z == '\\') {
      *q++ = '\\';
      *q++ = '\\';
    } else {
      *q++ = z;
    }
  }
  *q = 0;
  return q - (uint8_t *)s;
}
