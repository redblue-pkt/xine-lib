/*
 * Copyright (C) 2007-2020 the xine project
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/xine_plugin.h>

#include "ogg_combined.h"

static const demuxer_info_t demux_info_anx = {
  .priority = 20,
};

static const demuxer_info_t demux_info_ogg = {
  .priority = 10,
};

#ifdef HAVE_VORBIS
static const uint32_t vorbis_audio_types[] = {
  BUF_AUDIO_VORBIS,
  0
};

static const decoder_info_t dec_info_vorbis = {
  .supported_types = vorbis_audio_types,
  .priority = 5,
};
#endif /* HAVE_VORBIS */

#ifdef HAVE_SPEEX
static const uint32_t speex_audio_types[] = {
  BUF_AUDIO_SPEEX,
  0
};

static const decoder_info_t dec_info_speex = {
  .supported_types = speex_audio_types,
  .priority = 5,
};
#endif /* HAVE_SPEEX */

#ifdef HAVE_THEORA
static const uint32_t theora_video_types[] = {
  BUF_VIDEO_THEORA,
  0
};

static const decoder_info_t dec_info_theora = {
  .supported_types = theora_video_types,
  .priority = 5,
};
#endif /* HAVE_THEORA */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX,         27, "ogg",    XINE_VERSION_CODE, &demux_info_ogg,  ogg_init_class },
  { PLUGIN_DEMUX,         27, "anx",    XINE_VERSION_CODE, &demux_info_anx,  anx_init_class },
#ifdef HAVE_VORBIS
  { PLUGIN_AUDIO_DECODER, 16, "vorbis", XINE_VERSION_CODE, &dec_info_vorbis, vorbis_init_plugin },
#endif
#ifdef HAVE_SPEEX
  { PLUGIN_AUDIO_DECODER, 16, "speex",  XINE_VERSION_CODE, &dec_info_speex,  speex_init_plugin },
#endif
#ifdef HAVE_THEORA
  { PLUGIN_VIDEO_DECODER, 19, "theora", XINE_VERSION_CODE, &dec_info_theora, theora_init_plugin },
#endif
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
