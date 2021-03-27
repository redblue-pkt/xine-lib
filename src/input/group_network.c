/*
 * Copyright (C) 2017-2021 the xine project
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
 * simple network input plugins
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "group_network.h"

#include <xine/xine_plugin.h>

/*
 * exported plugin catalog entry
 */

static const input_info_t input_hls_info = {
  /* need to try high level protocol before plain file or http(s). */
  .priority = 1
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT,                       18, "tcp",  XINE_VERSION_CODE, NULL, input_net_init_class },
  { PLUGIN_INPUT,                       18, "tls",  XINE_VERSION_CODE, NULL, input_tls_init_class },
  { PLUGIN_INPUT,                       18, "gopher", XINE_VERSION_CODE, NULL, input_gopher_init_class },
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "http", XINE_VERSION_CODE, NULL, input_http_init_class },
  { PLUGIN_INPUT,                       18, "rtsp", XINE_VERSION_CODE, NULL, input_rtsp_init_class },
  { PLUGIN_INPUT,                       18, "pnm",  XINE_VERSION_CODE, NULL, input_pnm_init_class },
  { PLUGIN_INPUT,                       18, "ftp",  XINE_VERSION_CODE, NULL, input_ftp_init_class },
  { PLUGIN_INPUT,                       18, "ftpes", XINE_VERSION_CODE, NULL, input_ftpes_init_class },
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "hls",  XINE_VERSION_CODE, &input_hls_info, input_hls_init_class },
  { PLUGIN_INPUT,                       18, "mpegdash", XINE_VERSION_CODE, &input_hls_info, input_mpegdash_init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};

