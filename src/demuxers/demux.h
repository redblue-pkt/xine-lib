/* 
 * Copyright (C) 2000 the xine project
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
 * $Id: demux.h,v 1.1 2001/04/18 22:33:54 f1rmb Exp $
 */

#ifndef HAVE_DEMUX_H
#define HAVE_DEMUX_H

#include "buffer.h"
#include "xine.h"
#if defined(XINE_COMPILE)
#include "input/input_plugin.h"
#else
#include "input_plugin.h"
#endif

#define DEMUX_OK                  0
#define DEMUX_FINISHED            1

#define DEMUX_CANNOT_HANDLE       0
#define DEMUX_CAN_HANDLE          1

#define DEMUX_DEFAULT_STRATEGY    0
#define DEMUX_REVERT_STRATEGY     1
#define DEMUX_CONTENT_STRATEGY    2
#define DEMUX_EXTENSION_STRATEGY  3

#define STAGE_BY_CONTENT          1
#define STAGE_BY_EXTENSION        2

/*
 * a demux plugin (no matter if it's staically built into xine
 * or dynamically loaded at run-time) must implement these functions
 */

typedef struct demux_plugin_s demux_plugin_t;

struct demux_plugin_s
{
  /*
   * ask demuxer to open the given stream (input-plugin) 
   * using the content-detection method specified in <stage>
   *
   * return values: 
   *    DEMUX_CAN_HANDLE    on success
   *    DEMUX_CANNOT_HANDLE on failure
   */

  int (*open) (demux_plugin_t *this, input_plugin_t *ip, 
	       int stage);

  /*
   * start demux thread
   * pos : 0..65535
   */

  void (*start) (demux_plugin_t *this, fifo_buffer_t *video_fifo, 
		 fifo_buffer_t *audio_fifo, fifo_buffer_t *spu_fifo,
		 off_t pos) ;
  
  /*
   * stop & kill demux thread, free resources associated with current
   * input stream
   */

  void (*stop) (demux_plugin_t *this) ;

  /*
   * close demuxer, free all resources
   */

  void (*close) (demux_plugin_t *this) ;

  /*
   * returns DEMUX_OK or  DEMUX_FINISHED 
   */

  int (*get_status) (demux_plugin_t *this) ;

  /*
   * return human readable identifier for this plugin
   */

  char* (*get_identifier) (demux_plugin_t *this);

} ;

/*
 * for dynamic demux plugins:
 *
 * make sure you provide this (and only this!) function call:
 *
 * demux_plugin_t *init_demux_plugin (config_values_t *cfg, uint32_t xd);
 *
 */

demux_plugin_t *init_demux_mpeg (config_values_t *cfg, uint32_t xd);

demux_plugin_t *init_demux_mpeg_block (config_values_t *cfg, uint32_t xd);

demux_plugin_t *init_demux_avi (config_values_t *cfg, uint32_t xd);

demux_plugin_t *init_demux_mpeg_audio (config_values_t *cfg, uint32_t xd);

demux_plugin_t *init_demux_mpeg_elem(config_values_t *cfg, uint32_t xd);

#endif

