/*
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: plugin_catalog.h,v 1.7 2002/12/01 15:10:04 mroi Exp $
 *
 * xine-internal header: Definitions for plugin lists
 *
 */

#ifndef _PLUGIN_CATALOG_H
#define _PLUGIN_CATALOG_H

#include "xine_plugin.h"
#include "xineutils.h"

#define DECODER_MAX 256
#define PLUGIN_MAX  256

/* the engine takes this many plugins for one stream type */
#define PLUGINS_PER_TYPE 10

typedef struct {
  char            *filename;
  plugin_info_t   *info;
  void            *plugin_class;
  int              ref; /* count instances created of this plugin */
} plugin_node_t ;

struct plugin_catalog_s {
  xine_list_t     *input;
  xine_list_t     *demux;
  xine_list_t     *spu;
  xine_list_t     *audio;
  xine_list_t     *video;
  xine_list_t     *aout;
  xine_list_t     *vout;
  xine_list_t     *post;

  plugin_node_t   *audio_decoder_map[DECODER_MAX][PLUGINS_PER_TYPE];
  plugin_node_t   *video_decoder_map[DECODER_MAX][PLUGINS_PER_TYPE];
  plugin_node_t   *spu_decoder_map[DECODER_MAX][PLUGINS_PER_TYPE];
  
  const char      *ids[PLUGIN_MAX];

  pthread_mutex_t  lock;
};
typedef struct plugin_catalog_s plugin_catalog_t;

/*
 * load plugins into catalog
 *
 * all input+demux plugins will be fully loaded+initialized
 * decoder plugins are loaded on demand
 * video/audio output plugins have special load/probe functions
 */
void scan_plugins (xine_t *this);


/*
 * dispose all currently loaded plugins (shutdown)
 */

void dispose_plugins (xine_t *this);

#endif
