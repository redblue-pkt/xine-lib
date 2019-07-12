/*
 * Copyright (C) 2000-2019 the xine project
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
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "group_w32.h"

static const uint32_t qtv_supported_types[] = { BUF_VIDEO_SORENSON_V3, 0 };
static const decoder_info_t qtv_info = {
  .supported_types = qtv_supported_types,
  .priority        = 1
};
static const uint32_t qta_supported_types[] = {
  BUF_AUDIO_QDESIGN1,
  BUF_AUDIO_QDESIGN2,
  BUF_AUDIO_QCLP,
  0
};
static const decoder_info_t qta_info = {
  .supported_types = qta_supported_types,
  .priority        = 1
};

static const uint32_t w32v_supported_types[] = {
  BUF_VIDEO_MSMPEG4_V1, BUF_VIDEO_MSMPEG4_V2, BUF_VIDEO_MSMPEG4_V3,
  BUF_VIDEO_IV50, BUF_VIDEO_IV41, BUF_VIDEO_IV32, BUF_VIDEO_IV31,
  BUF_VIDEO_CINEPAK, /* BUF_VIDEO_ATIVCR1, */
  BUF_VIDEO_ATIVCR2, BUF_VIDEO_I263, BUF_VIDEO_MSVC,
  BUF_VIDEO_DV, BUF_VIDEO_WMV7, BUF_VIDEO_WMV8, BUF_VIDEO_WMV9,
  BUF_VIDEO_VP31, BUF_VIDEO_MSS1, BUF_VIDEO_TSCC, BUF_VIDEO_UCOD,
  BUF_VIDEO_VP4, BUF_VIDEO_VP5, BUF_VIDEO_VP6,
  0
};
static const decoder_info_t w32v_info = {
  .supported_types = w32v_supported_types,
  .priority        = 1
};

static const uint32_t w32a_supported_types[] = {
  BUF_AUDIO_WMAV1, BUF_AUDIO_WMAV2, BUF_AUDIO_WMAPRO, BUF_AUDIO_MSADPCM,
  BUF_AUDIO_MSIMAADPCM, BUF_AUDIO_MSGSM, BUF_AUDIO_IMC, BUF_AUDIO_LH,
  BUF_AUDIO_VOXWARE, BUF_AUDIO_ACELPNET, BUF_AUDIO_VIVOG723, BUF_AUDIO_WMAV,
  BUF_AUDIO_WMALL,
  0
 };
static const decoder_info_t w32a_info = {
  .supported_types = w32a_supported_types,
  .priority        = 1
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 19, "win32v", XINE_VERSION_CODE, &w32v_info, w32v_init_class },
  { PLUGIN_AUDIO_DECODER | PLUGIN_MUST_PRELOAD, 16, "win32a", XINE_VERSION_CODE, &w32a_info, w32a_init_class },
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 19, "qtv",    XINE_VERSION_CODE, &qtv_info,  qtv_init_class },
  { PLUGIN_AUDIO_DECODER | PLUGIN_MUST_PRELOAD, 16, "qta",    XINE_VERSION_CODE, &qta_info,  qta_init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
