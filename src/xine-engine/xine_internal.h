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
 * $Id: xine_internal.h,v 1.3 2001/04/20 18:01:55 guenter Exp $
 *
 */

#ifndef HAVE_XINE_INTERNAL_H
#define HAVE_XINE_INTERNAL_H

#include <inttypes.h>
#include "input/input_plugin.h"
#include "demuxers/demux.h"
#include "video_out.h"
#include "audio_out.h"
#include "metronom.h"

#define INPUT_PLUGIN_MAX   50
#define DEMUXER_PLUGIN_MAX 50

/* nStatus : current xine status */
typedef void (*gui_status_callback_func_t)(int nStatus);

/*
 * player status constants:
 */

#define XINE_STOP      0 
#define XINE_PLAY      1 
#define XINE_PAUSE     2 
#define XINE_QUIT      3

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
 * player status constants:
 */

#define XINE_STOP      0 
#define XINE_PLAY      1 
#define XINE_PAUSE     2 
#define XINE_QUIT      3

/*
 * read config file and init a config object
 * (if it exists)
 */
config_values_t *config_file_init (char *filename);

/*
 * init xine - call once at startup
 *
 */

xine_t *xine_init (vo_instance_t *vo, 
		   ao_functions_t *ao,
		   gui_status_callback_func_t gui_status_callback,
		   config_values_t *config, int demux_strategy, uint32_t debug_lvl) ;

/*
 * open a stream and play it
 *
 * name : mrl to open
 * pos  : start position 0..65535
 *
 */
void xine_play (xine_t *this, char *MRL, int pos);


/*
 * toggle pause mode
 */
void xine_pause (xine_t *this);


/*
 * stop playing
 */
void xine_stop (xine_t *this);

/*
 * tell current input plugin to eject media.
 */
int xine_eject(xine_t *this);

/*
 * return current status (XINE_PLAY/XINE_STOP...)
 */
int xine_get_status (xine_t *this);

/*
 * get current position in stream
 * returns position (range : 0 - 65535)
 */
int xine_get_current_position (xine_t *this);

/*
 * return the current audio channel
 */
int xine_get_audio_channel (xine_t *this);

/*
 * set desired audio channel
 */
void xine_select_audio_channel (xine_t *this, int channel);

/*
 * return the current SPU channel
 */
int xine_get_spu_channel (xine_t *this);

/*
 * set desired SPU channel
 */
void xine_select_spu_channel (xine_t *this, int channel);

/*
 * exit xine
 */
void xine_exit (xine_t *this);

/*
 * browsing support
 */

/*
 * some input plugins are browseable
 * returns a list of ids of these plugins
 */
char **xine_get_browsable_input_plugin_ids (xine_t *this) ;

/*
 * browse function
 * asks input plugin named <plugin_id> to return
 * a list of available MRLs in domain/directory <start_mrl>
 * 
 * start_mrl may be NULL indicating the toplevel domain/dir
 * returns start_mrl if start_mrl is a valid MRL, not a directory
 * returns NULL if start_mrl is an invalid MRL, not even a directory
 */

char **xine_get_browse_mrls (xine_t *this, char *plugin_id, 
			     char *start_mrl);

/*
 * autoplay support
 */

/*
 * some input plugins can generate autoplay lists
 * returns a list of ids of these plugins
 */
char **xine_get_autoplay_input_plugin_ids (xine_t *this) ;

/*
 * get autoplay MRL list for input plugin named <plugin_id>
 */
char **xine_get_autoplay_mrls (xine_t *this, char *plugin_id);

/* 
 * Load input/demux/audio_out/video_out plugins
 * prototypes of load_plugins.c functions.
 */
void xine_load_demux_plugins (xine_t *this);
void xine_load_input_plugins (xine_t *this);

#endif
