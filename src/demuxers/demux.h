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
 * $Id: demux.h,v 1.22 2002/10/28 03:24:43 miguelfreitas Exp $
 */

#ifndef HAVE_DEMUX_H
#define HAVE_DEMUX_H

#include "buffer.h"
#include "xine_internal.h"
#if defined(XINE_COMPILE)
#include "input/input_plugin.h"
#else
#include "input_plugin.h"
#endif

#define DEMUXER_PLUGIN_IFACE_VERSION    15

#define DEMUX_OK                   0
#define DEMUX_FINISHED             1

#define DEMUX_CANNOT_HANDLE        0
#define DEMUX_CAN_HANDLE           1

#define METHOD_BY_CONTENT          1
#define METHOD_BY_EXTENSION        2

typedef struct demux_class_s demux_class_t ;
typedef struct demux_plugin_s demux_plugin_t;

struct demux_class_s {

  /*
   * open a new instance of this plugin class
   */
  demux_plugin_t* (*open_plugin) (demux_class_t *this, xine_stream_t *stream, input_plugin_t *input);

  /*
   * return human readable (verbose = 1 line) description for this plugin
   */
  char* (*get_description) (demux_class_t *this);

  /*
   * return human readable identifier for this plugin
   */

  char* (*get_identifier) (demux_class_t *this);
  
  /*
   * return MIME types supported for this plugin
   */

  char* (*get_mimetypes) (demux_class_t *this);

  /*
   * return ' ' seperated list of file extensions this
   * demuxer is likely to handle
   * (will be used to filter media files in 
   * file selection dialogs)
   */

  char* (*get_extensions) (demux_class_t *this);

  /*
   * close down, free all resources
   */
  void (*dispose) (demux_class_t *this);
};


/*
 * any demux plugin must implement these functions
 */

struct demux_plugin_s {

  /*
   * send headers, followed by BUF_CONTROL_HEADERS_DONE down the
   * fifos, then return. do not start demux thread (yet)
   */

  void (*send_headers) (demux_plugin_t *this);

  /*
   * ask demux to seek 
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
   * send a chunk of data down to decoder fifos 
   *
   * the meaning of "chunk" is specific to every demux, usually
   * it involves parsing one unit of data from stream.
   *
   * this function will be called from demux loop and should return
   * the demux current status
   */

  int (*send_chunk) (demux_plugin_t *this);
          
  /*
   * free resources 
   */

  void (*dispose) (demux_plugin_t *this) ;

  /*
   * returns DEMUX_OK or  DEMUX_FINISHED 
   */

  int (*get_status) (demux_plugin_t *this) ;

  /*
   * estimate stream length in seconds
   * may return 0 for non-seekable streams
   */

  int (*get_stream_length) (demux_plugin_t *this);


  /*
   * "backwards" link to plugin class
   */

  demux_class_t *demux_class;
} ;

#endif
