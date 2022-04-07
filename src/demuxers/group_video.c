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
 * This file contains plugin entries for several av demuxers.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/xine_plugin.h>

#include "group_video.h"

/*
 * exported plugin catalog entries
 */
static const demuxer_info_t demux_info_plus__0 = { .priority =  0 };
static const demuxer_info_t demux_info_plus__1 = { .priority =  1 };
static const demuxer_info_t demux_info_plus__9 = { .priority =  9 };
static const demuxer_info_t demux_info_plus_10 = { .priority = 10 };
/* probe mpeg-ts first, and detect TV recordings cut at an unhappy byte pos. */
static const demuxer_info_t demux_info_plus_12 = { .priority = 12 };


const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "avi",        XINE_VERSION_CODE, &demux_info_plus_10, demux_avi_init_class },
  { PLUGIN_DEMUX, 27, "elem",       XINE_VERSION_CODE, &demux_info_plus__0, demux_elem_init_class },
  { PLUGIN_DEMUX, 27, "flashvideo", XINE_VERSION_CODE, &demux_info_plus_10, demux_flv_init_class },
  { PLUGIN_DEMUX, 27, "iff",        XINE_VERSION_CODE, &demux_info_plus_10, demux_iff_init_class },
  { PLUGIN_DEMUX, 27, "ivf",        XINE_VERSION_CODE, &demux_info_plus__1, demux_ivf_init_class },
  { PLUGIN_DEMUX, 27, "matroska",   XINE_VERSION_CODE, &demux_info_plus_10, demux_matroska_init_class },
  { PLUGIN_DEMUX, 27, "mpeg",       XINE_VERSION_CODE, &demux_info_plus__9, demux_mpeg_init_class },
  { PLUGIN_DEMUX, 27, "mpeg_block", XINE_VERSION_CODE, &demux_info_plus_10, demux_mpeg_block_init_class },
  { PLUGIN_DEMUX, 27, "mpeg-ts",    XINE_VERSION_CODE, &demux_info_plus_12, demux_ts_init_class },
  { PLUGIN_DEMUX, 27, "mpeg_pes",   XINE_VERSION_CODE, &demux_info_plus_10, demux_pes_init_class },
  { PLUGIN_DEMUX, 27, "quicktime",  XINE_VERSION_CODE, &demux_info_plus_10, demux_qt_init_class },
  { PLUGIN_DEMUX, 27, "rawdv",      XINE_VERSION_CODE, &demux_info_plus__1, demux_rawdv_init_class },
  { PLUGIN_DEMUX, 27, "real",       XINE_VERSION_CODE, &demux_info_plus_10, demux_real_init_class },
  { PLUGIN_DEMUX, 27, "vc1es",      XINE_VERSION_CODE, &demux_info_plus__0, demux_vc1es_init_class },
  { PLUGIN_DEMUX, 27, "yuv_frames", XINE_VERSION_CODE, &demux_info_plus__0, demux_yuv_frames_init_class },
  { PLUGIN_DEMUX, 27, "yuv4mpeg2",  XINE_VERSION_CODE, &demux_info_plus_10, demux_yuv4mpeg2_init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};

