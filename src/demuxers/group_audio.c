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
 * $Id: group_audio.c,v 1.12 2004/02/11 20:40:00 tmattern Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"
#include "demux.h"

#include "group_audio.h"

/*
 * exported plugin catalog entries
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 24, "aac", XINE_VERSION_CODE, NULL, demux_aac_init_plugin },
  { PLUGIN_DEMUX, 24, "ac3", XINE_VERSION_CODE, NULL, demux_ac3_init_plugin },
  { PLUGIN_DEMUX, 24, "aud", XINE_VERSION_CODE, NULL, demux_aud_init_plugin },
  { PLUGIN_DEMUX, 24, "aiff", XINE_VERSION_CODE, NULL, demux_aiff_init_plugin },
  { PLUGIN_DEMUX, 24, "cdda", XINE_VERSION_CODE, NULL, demux_cdda_init_plugin },
  { PLUGIN_DEMUX, 24, "mp3", XINE_VERSION_CODE, NULL, demux_mpgaudio_init_class },
  { PLUGIN_DEMUX, 24, "nsf", XINE_VERSION_CODE, NULL, demux_nsf_init_plugin },
  { PLUGIN_DEMUX, 24, "realaudio", XINE_VERSION_CODE, NULL, demux_realaudio_init_plugin },
  { PLUGIN_DEMUX, 24, "snd", XINE_VERSION_CODE, NULL, demux_snd_init_plugin },
  { PLUGIN_DEMUX, 24, "voc", XINE_VERSION_CODE, NULL, demux_voc_init_plugin },
  { PLUGIN_DEMUX, 24, "vox", XINE_VERSION_CODE, NULL, demux_vox_init_plugin },
  { PLUGIN_DEMUX, 24, "wav", XINE_VERSION_CODE, NULL, demux_wav_init_plugin },
#ifdef HAVE_MODPLUG
  { PLUGIN_DEMUX, 24, "mod", XINE_VERSION_CODE, NULL, demux_mod_init_plugin },
#endif
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
