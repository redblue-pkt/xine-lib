/* 
 * Copyright (C) 2000, 2001 the xine project
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
 * $Id: demux.h,v 1.14 2002/05/21 00:12:31 siggi Exp $
 */

#ifndef HAVE_DEMUX_H
#define HAVE_DEMUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "buffer.h"
#include "xine_internal.h"
#if defined(XINE_COMPILE)
#include "input/input_plugin.h"
#else
#include "input_plugin.h"
#endif

#define DEMUXER_PLUGIN_IFACE_VERSION    7

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
 * a demux plugin must implement these functions
 */

typedef struct demux_plugin_s demux_plugin_t;

struct demux_plugin_s
{
  /*
   * plugin interface version, lower versions _may_ be supported
   */
  int interface_version;

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
   *
   * for seekable streams, a start position can be specified
   *
   * start_pos  : position in input source
   * start_time : position measured in seconds from stream start
   *
   * if both parameters are !=0 start_pos will be used
   * for non-seekable streams both values will be ignored
   *
   * returns the demux status (like get_status, but immediately after
   *                           starting the demuxer)
   */

  int (*start) (demux_plugin_t *this, fifo_buffer_t *video_fifo, 
		 fifo_buffer_t *audio_fifo, 
		 off_t start_pos, int start_time);

  /*
   * ask running demux thread to seek 
   *
   * for seekable streams, a start position can be specified
   *
   * start_pos  : position in input source
   * start_time : position measured in seconds from stream start
   *
   * if both parameters are !=0 start_pos will be used
   * for non-seekable streams both values will be ignored
   *
   * returns the demux status (like get_status, but immediately after
   *                           starting the demuxer)
   */

  int (*seek) (demux_plugin_t *this, 
		 off_t start_pos, int start_time);
  
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

  char* (*get_identifier) (void);
  
 /*
   * return MIME types supported for this plugin
   */

  char* (*get_mimetypes) (void);
  /*
   * estimate stream length in seconds
   * may return 0 for non-seekable streams
   */

  int (*get_stream_length) (demux_plugin_t *this);
} ;

/*
 * demuxer plugins should provide this (and only this!) function call:
 *
 * demux_plugin_t *init_demux_plugin (int iface_version, xine_t *xine);
 */

#ifdef __cplusplus
}
#endif

#endif
