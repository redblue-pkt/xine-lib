/*
 * Copyright (C) 2000-2019 the xine project
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
 */

#ifndef HAVE_DEMUX_H
#define HAVE_DEMUX_H

#include <xine/attributes.h>
#include <xine/input_plugin.h>
#include <xine/buffer.h>
#include <xine/xine_internal.h>

struct plugin_node_s;

#define DEMUXER_PLUGIN_IFACE_VERSION    27

#define DEMUX_OK                   0
#define DEMUX_FINISHED             1

#define DEMUX_CANNOT_HANDLE        0
#define DEMUX_CAN_HANDLE           1

#define METHOD_BY_CONTENT          1
#define METHOD_BY_MRL              2
#define METHOD_EXPLICIT            3

typedef struct demux_class_s demux_class_t ;
typedef struct demux_plugin_s demux_plugin_t;

struct demux_class_s {

  /*
   * open a new instance of this plugin class
   */
  demux_plugin_t* (*open_plugin) (demux_class_t *this_gen, xine_stream_t *stream, input_plugin_t *input);

  /**
   * @brief short human readable identifier for this plugin class
   */
  const char *identifier;

  /**
   * @brief human readable (verbose = 1 line) description for this plugin class
   *
   * The description is passed to gettext() to internationalise.
   */
  const char *description;

  /**
   * @brief Optional non-standard catalog to use with dgettext() for description.
   */
  const char *text_domain;

  /**
   * @brief MIME types supported for this plugin
   */

  const char* mimetypes;

  /**
   * @brief space separated list of file extensions this demuxer is
   * likely to handle
   *
   * (will be used to filter media files in file selection dialogs)
   */
  const char* extensions;

  /*
   * close down, free all resources
   */
  void (*dispose) (demux_class_t *this_gen);
};

#define default_demux_class_dispose (void (*) (demux_class_t *this_gen))free

/*
 * any demux plugin must implement these functions
 */

struct demux_plugin_s {

  /*
   * send headers, followed by BUF_CONTROL_HEADERS_DONE down the
   * fifos, then return. do not start demux thread (yet)
   */

  void (*send_headers) (demux_plugin_t *this_gen);

  /*
   * ask demux to seek
   *
   * for seekable streams, a start position can be specified
   *
   * start_pos  : position in input source (0..65535)
   *              this is defined as most convenient to demuxer, can be
   *              either time or offset based.
   * start_time : position measured in miliseconds from stream start
   * playing : true if this is a new seek within an already playing stream
   *           false if playback of this stream has not started yet
   *
   * if both parameters are !=0 start_pos will be used
   * for non-seekable streams both values will be ignored
   *
   * returns the demux status (like get_status, but immediately after
   *                           starting the demuxer)
   */

  int (*seek) (demux_plugin_t *this_gen,
	       off_t start_pos, int start_time, int playing );

  /*
   * send a chunk of data down to decoder fifos
   *
   * the meaning of "chunk" is specific to every demux, usually
   * it involves parsing one unit of data from stream.
   *
   * this function will be called from demux loop and should return
   * the demux current status
   */

  int (*send_chunk) (demux_plugin_t *this_gen);

  /*
   * free resources
   */

  void (*dispose) (demux_plugin_t *this_gen) ;

  /*
   * returns DEMUX_OK or  DEMUX_FINISHED
   */

  int (*get_status) (demux_plugin_t *this_gen) ;

  /*
   * gets stream length in miliseconds (might be estimated)
   * may return 0 for non-seekable streams
   */

  int (*get_stream_length) (demux_plugin_t *this_gen);

  /*
   * return capabilities of demuxed stream
   */

  uint32_t (*get_capabilities) (demux_plugin_t *this_gen);

  /*
   * request optional data from input plugin.
   */
  int (*get_optional_data) (demux_plugin_t *this_gen, void *data, int data_type);

  /*
   * "backwards" link to plugin class
   */

  demux_class_t *demux_class;

  /**
   * @brief Pointer to the loaded plugin node.
   *
   * Used by the plugins loader. It's an opaque type when using the
   * structure outside of xine's build.
   */
  struct plugin_node_s *node XINE_PRIVATE_FIELD;
};

#define default_demux_plugin_dispose (void (*) (demux_plugin_t *this_gen))free

/*
 * possible capabilites a demux plugin can have:
 */
#define DEMUX_CAP_NOCAP                0x00000000

/*
 * DEMUX_CAP_AUDIOLANG:
 * DEMUX_CAP_SPULANG:
 *   demux plugin knows something about audio/spu languages,
 *   e.g. knows that audio stream #0 is english,
 *   audio stream #1 is german, ...  Same bits as INPUT
 *   capabilities .
 */

#define DEMUX_CAP_AUDIOLANG            0x00000008
#define DEMUX_CAP_SPULANG              0x00000010

/*
 * DEMUX_CAP_CHAPTERS:
 *   The media streams provided by this plugin have an internal
 *   structure dividing it into segments usable for navigation.
 *   For those plugins, the behaviour of the skip button in UIs
 *   should be changed from "next MRL" to "next chapter" by
 *   sending XINE_EVENT_INPUT_NEXT.
 *   Same bits as INPUT capabilities.
 */

#define DEMUX_CAP_CHAPTERS             0x00000080

/*
 * DEMUX_CAP_STOP:
 *   demux plugin needs to do some cleanup work _before_ end buffers
 *   when the stream is stopped (eg flushing a pending discontinuity).
 *   This shall be done by calling
 *   demux_plugin->get_optional_data (demux_plugin, NULL, DEMUX_OPTIONAL_DATA_STOP).
 */

#define DEMUX_CAP_STOP                 0x00000100
   


#define DEMUX_OPTIONAL_UNSUPPORTED    0
#define DEMUX_OPTIONAL_SUCCESS        1

#define DEMUX_OPTIONAL_DATA_AUDIOLANG 2
#define DEMUX_OPTIONAL_DATA_SPULANG   3
#define DEMUX_OPTIONAL_DATA_STOP      4

#endif
