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
 * This file contains plugin entries for several demuxers used in games
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/xine_plugin.h>

#include "group_games.h"

/*
 * exported plugin catalog entries
 */

static const demuxer_info_t demux_info_eawve = {
  .priority = 10,
};

static const demuxer_info_t demux_info_idcin = {
  .priority = 10,
};

static const demuxer_info_t demux_info_ipmovie = {
  .priority = 10,
};

static const demuxer_info_t demux_info_vqa = {
  .priority = 10,
};

static const demuxer_info_t demux_info_wc3movie = {
  .priority = 10,
};

static const demuxer_info_t demux_info_roq = {
  .priority = 10,
};

static const demuxer_info_t demux_info_str = {
  .priority = 10,
};

static const demuxer_info_t demux_info_film = {
  .priority = 10,
};

static const demuxer_info_t demux_info_smjpeg = {
  .priority = 10,
};

static const demuxer_info_t demux_info_fourxm = {
  .priority = 10,
};

static const demuxer_info_t demux_info_vmd = {
  .priority = 10,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 27, "wve",      XINE_VERSION_CODE, &demux_info_eawve,    demux_eawve_init_plugin},
  { PLUGIN_DEMUX, 27, "idcin",    XINE_VERSION_CODE, &demux_info_idcin,    demux_idcin_init_plugin },
  { PLUGIN_DEMUX, 27, "ipmovie",  XINE_VERSION_CODE, &demux_info_ipmovie,  demux_ipmovie_init_plugin },
  { PLUGIN_DEMUX, 27, "vqa",      XINE_VERSION_CODE, &demux_info_vqa,      demux_vqa_init_plugin },
  { PLUGIN_DEMUX, 27, "wc3movie", XINE_VERSION_CODE, &demux_info_wc3movie, demux_wc3movie_init_plugin },
  { PLUGIN_DEMUX, 27, "roq",      XINE_VERSION_CODE, &demux_info_roq,      demux_roq_init_plugin },
  { PLUGIN_DEMUX, 27, "str",      XINE_VERSION_CODE, &demux_info_str,      demux_str_init_plugin },
  { PLUGIN_DEMUX, 27, "film",     XINE_VERSION_CODE, &demux_info_film,     demux_film_init_plugin },
  { PLUGIN_DEMUX, 27, "smjpeg",   XINE_VERSION_CODE, &demux_info_smjpeg,   demux_smjpeg_init_plugin },
  { PLUGIN_DEMUX, 27, "fourxm",   XINE_VERSION_CODE, &demux_info_fourxm,   demux_fourxm_init_plugin },
  { PLUGIN_DEMUX, 27, "vmd",      XINE_VERSION_CODE, &demux_info_vmd,      demux_vmd_init_plugin },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
