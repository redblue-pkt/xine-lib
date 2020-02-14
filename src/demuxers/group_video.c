/*
 * Copyright (C) 2000-2020 the xine project
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
static const demuxer_info_t demux_info_avi        = { .priority = 10 };
static const demuxer_info_t demux_info_elem       = { .priority = 0  };
static const demuxer_info_t demux_info_flv        = { .priority = 10 };
static const demuxer_info_t demux_info_iff        = { .priority = 10 };
static const demuxer_info_t demux_info_ivf        = { .priority = 1  };
static const demuxer_info_t demux_info_mpeg       = { .priority = 9  };
static const demuxer_info_t demux_info_mpeg_block = { .priority = 10 };
static const demuxer_info_t demux_info_mpeg_pes   = { .priority = 10 };
static const demuxer_info_t demux_info_matroska   = { .priority = 10 };
static const demuxer_info_t demux_info_qt         = { .priority = 10 };
static const demuxer_info_t demux_info_raw_dv     = { .priority = 1  };
static const demuxer_info_t demux_info_real       = { .priority = 10 };
/* probe mpeg-ts first, and detect TV recordings cut at an unhappy byte pos. */
static const demuxer_info_t demux_info_ts         = { .priority = 12 };
static const demuxer_info_t demux_info_vc1es      = { .priority = 0  };
static const demuxer_info_t demux_info_yuv_frames = { .priority = 0  };
static const demuxer_info_t demux_info_yuv4mpeg2  = { .priority = 10 };


const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "avi",        XINE_VERSION_CODE, &demux_info_avi,        demux_avi_init_class },
  { PLUGIN_DEMUX, 27, "elem",       XINE_VERSION_CODE, &demux_info_elem,       demux_elem_init_class },
  { PLUGIN_DEMUX, 27, "flashvideo", XINE_VERSION_CODE, &demux_info_flv,        demux_flv_init_class },
  { PLUGIN_DEMUX, 27, "iff",        XINE_VERSION_CODE, &demux_info_iff,        demux_iff_init_class },
  { PLUGIN_DEMUX, 27, "ivf",        XINE_VERSION_CODE, &demux_info_ivf,        demux_ivf_init_class },
  { PLUGIN_DEMUX, 27, "matroska",   XINE_VERSION_CODE, &demux_info_matroska,   demux_matroska_init_class },
  { PLUGIN_DEMUX, 27, "mpeg",       XINE_VERSION_CODE, &demux_info_mpeg,       demux_mpeg_init_class },
  { PLUGIN_DEMUX, 27, "mpeg_block", XINE_VERSION_CODE, &demux_info_mpeg_block, demux_mpeg_block_init_class },
  { PLUGIN_DEMUX, 27, "mpeg-ts",    XINE_VERSION_CODE, &demux_info_ts,         demux_ts_init_class },
  { PLUGIN_DEMUX, 27, "mpeg_pes",   XINE_VERSION_CODE, &demux_info_mpeg_pes,   demux_pes_init_class },
  { PLUGIN_DEMUX, 27, "quicktime",  XINE_VERSION_CODE, &demux_info_qt,         demux_qt_init_class },
  { PLUGIN_DEMUX, 27, "rawdv",      XINE_VERSION_CODE, &demux_info_raw_dv,     demux_rawdv_init_class },
  { PLUGIN_DEMUX, 27, "real",       XINE_VERSION_CODE, &demux_info_real,       demux_real_init_class },
  { PLUGIN_DEMUX, 27, "vc1es",      XINE_VERSION_CODE, &demux_info_vc1es,      demux_vc1es_init_class },
  { PLUGIN_DEMUX, 27, "yuv_frames", XINE_VERSION_CODE, &demux_info_yuv_frames, demux_yuv_frames_init_class },
  { PLUGIN_DEMUX, 27, "yuv4mpeg2",  XINE_VERSION_CODE, &demux_info_yuv4mpeg2,  demux_yuv4mpeg2_init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};

