/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * This file is part of xine, a unix video player.
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
 * $Id: xine_internal.h,v 1.1 2001/04/18 22:36:09 f1rmb Exp $
 *
 */

#ifndef HAVE_XINE_INTERNAL_H
#define HAVE_XINE_INTERNAL_H

#include <inttypes.h>
#include "xine.h"
#include "input/input_plugin.h"
#include "demuxers/demux.h"
#include "video_out.h"
#include "audio_out.h"
#include "metronom.h"

#define INPUT_PLUGIN_MAX   50
#define DEMUXER_PLUGIN_MAX 50

typedef struct xine_s {
  
  /* private : */

  metronom_t                *metronom;

  input_plugin_t             input_plugins[INPUT_PLUGIN_MAX];
  int                        num_input_plugins;
  input_plugin_t            *cur_input_plugin;

  demux_plugin_t             demuxer_plugins[DEMUXER_PLUGIN_MAX];
  int                        num_demuxer_plugins;
  demux_plugin_t            *cur_demuxer_plugin;
  int                        demux_stragegy;

  int                        status;
  off_t                      cur_input_pos;
  char                       cur_mrl[1024];

  fifo_buffer_t             *spu_fifo;

  int                        audio_channel;
  int                        spu_channel;

  gui_status_callback_func_t status_callback;

  /* Lock for xine player functions */
  pthread_mutex_t            xine_lock;

} xine_t;

/* 
 * Load input/demux/audio_out/video_out plugins
 * prototypes of load_plugins.c functions.
 */
void xine_load_demux_plugins (xine_t *this);
void xine_load_input_plugins (xine_t *this);

#endif
