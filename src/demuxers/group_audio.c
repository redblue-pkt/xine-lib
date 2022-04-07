/*
 * Copyright (C) 2000-2022 the xine project
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
 * This file contains plugin entries for several demuxers used in games
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/xine_plugin.h>

#include "group_audio.h"

/*
 * exported plugin catalog entries
 */
static const demuxer_info_t demux_info_minus_3 = { .priority = -3 };
static const demuxer_info_t demux_info_minus_1 = { .priority = -1 };
static const demuxer_info_t demux_info_plus__0 = { .priority =  0 };
static const demuxer_info_t demux_info_plus__1 = { .priority =  1 };
static const demuxer_info_t demux_info_plus__6 = { .priority =  6 };
static const demuxer_info_t demux_info_plus__8 = { .priority =  8 };
static const demuxer_info_t demux_info_plus_10 = { .priority = 10 };

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "aac",       XINE_VERSION_CODE, &demux_info_minus_1, demux_aac_init_plugin },
  { PLUGIN_DEMUX, 27, "ac3",       XINE_VERSION_CODE, &demux_info_plus__8, demux_ac3_init_plugin },
  { PLUGIN_DEMUX, 27, "aud",       XINE_VERSION_CODE, &demux_info_minus_3, demux_aud_init_plugin },
  { PLUGIN_DEMUX, 27, "aiff",      XINE_VERSION_CODE, &demux_info_plus_10, demux_aiff_init_plugin },
  { PLUGIN_DEMUX, 27, "cdda",      XINE_VERSION_CODE, &demux_info_plus__6, demux_cdda_init_plugin },
  { PLUGIN_DEMUX, 27, "dts",       XINE_VERSION_CODE, &demux_info_plus__8, demux_dts_init_plugin },
  { PLUGIN_DEMUX, 27, "flac",      XINE_VERSION_CODE, &demux_info_plus_10, demux_flac_init_plugin },
  { PLUGIN_DEMUX, 27, "mp3",       XINE_VERSION_CODE, &demux_info_plus__0, demux_mpgaudio_init_class },
  { PLUGIN_DEMUX, 27, "mpc",       XINE_VERSION_CODE, &demux_info_plus__1, demux_mpc_init_plugin },
  { PLUGIN_DEMUX, 27, "realaudio", XINE_VERSION_CODE, &demux_info_plus_10, demux_realaudio_init_plugin },
  { PLUGIN_DEMUX, 27, "shn",       XINE_VERSION_CODE, &demux_info_plus__0, demux_shn_init_plugin },
  { PLUGIN_DEMUX, 27, "snd",       XINE_VERSION_CODE, &demux_info_plus_10, demux_snd_init_plugin },
  { PLUGIN_DEMUX, 27, "tta",       XINE_VERSION_CODE, &demux_info_plus_10, demux_tta_init_plugin },
  { PLUGIN_DEMUX, 27, "voc",       XINE_VERSION_CODE, &demux_info_plus_10, demux_voc_init_plugin },
  { PLUGIN_DEMUX, 27, "vox",       XINE_VERSION_CODE, &demux_info_plus_10, demux_vox_init_plugin },
  { PLUGIN_DEMUX, 27, "wav",       XINE_VERSION_CODE, &demux_info_plus__6, demux_wav_init_plugin },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
