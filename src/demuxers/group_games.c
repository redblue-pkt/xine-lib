/*
 * Copyright (C) 2000-2003 the xine project
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * This file contains plugin entries for several demuxers used in games
 *
 * $Id: group_games.c,v 1.7 2003/11/15 14:01:05 miguelfreitas Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"
#include "demux.h"

#include "group_games.h"

/*
 * exported plugin catalog entries
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 23, "wve", XINE_VERSION_CODE, NULL, demux_eawve_init_plugin},
  { PLUGIN_DEMUX, 23, "idcin", XINE_VERSION_CODE, NULL, demux_idcin_init_plugin },
  { PLUGIN_DEMUX, 23, "ipmovie", XINE_VERSION_CODE, NULL, demux_ipmovie_init_plugin },
  { PLUGIN_DEMUX, 23, "vqa", XINE_VERSION_CODE, NULL, demux_vqa_init_plugin },
  { PLUGIN_DEMUX, 23, "wc3movie", XINE_VERSION_CODE, NULL, demux_wc3movie_init_plugin },
  { PLUGIN_DEMUX, 23, "roq", XINE_VERSION_CODE, NULL, demux_roq_init_plugin },
  { PLUGIN_DEMUX, 23, "str", XINE_VERSION_CODE, NULL, demux_str_init_plugin },
  { PLUGIN_DEMUX, 23, "film", XINE_VERSION_CODE, NULL, demux_film_init_plugin },
  { PLUGIN_DEMUX, 23, "smjpeg", XINE_VERSION_CODE, NULL, demux_smjpeg_init_plugin },
  { PLUGIN_DEMUX, 23, "fourxm", XINE_VERSION_CODE, NULL, demux_fourxm_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
