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
 * $Id: xine_internal.h,v 1.18 2001/05/03 00:02:42 f1rmb Exp $
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

#define INPUT_PLUGIN_MAX       50
#define DEMUXER_PLUGIN_MAX     50
#define DECODER_PLUGIN_MAX     256
#define DECODER_PLUGIN_IFACE_VERSION      1
#define AUDIO_OUT_PLUGIN_MAX   50
#define VIDEO_OUT_PLUGIN_MAX   50

/*
 * generic xine video decoder plugin interface
 *
 * for a dynamic plugin make sure you provide this function call:
 * video_decoder_t *init_video_decoder_plugin (int iface_version,  
 *                                             config_values_t *cfg);
 */

typedef struct video_decoder_s video_decoder_t;

struct video_decoder_s {

  int interface_version;

  int (*can_handle) (video_decoder_t *this, int buf_type);

  void (*init) (video_decoder_t *this, vo_instance_t *video_out);

  void (*decode_data) (video_decoder_t *this, buf_element_t *buf);

  void (*release_img_buffers) (video_decoder_t *this);

  void (*close) (video_decoder_t *this);

  char* (*get_identifier) (void);

};

/*
 * generic xine audio decoder plugin interface
 *
 * for a dynamic plugin make sure you provide this function call:
 * audio_decoder_t *init_audio_decoder_plugin (int iface_version,  
 *                                             config_values_t *cfg);
 */

typedef struct audio_decoder_s audio_decoder_t;

struct audio_decoder_s {

  int interface_version;

  int (*can_handle) (audio_decoder_t *this, int buf_type);

  void (*init) (audio_decoder_t *this, ao_functions_t *audio_out);

  void (*decode_data) (audio_decoder_t *this, buf_element_t *buf);

  void (*close) (audio_decoder_t *this);

  char* (*get_identifier) (void);

};

/*
 * gui callback function - called by xine engine on stream end
 *
 * nStatus : current xine status 
 */
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
  
  config_values_t           *config;

  input_plugin_t            *input_plugins[INPUT_PLUGIN_MAX];
  int                        num_input_plugins;
  input_plugin_t            *cur_input_plugin;

  demux_plugin_t            *demuxer_plugins[DEMUXER_PLUGIN_MAX];
  int                        num_demuxer_plugins;
  demux_plugin_t            *cur_demuxer_plugin;
  int                        demux_strategy;

  int                        status;
  off_t                      cur_input_pos;
  char                       cur_mrl[1024];

  fifo_buffer_t             *spu_fifo;

  int                        audio_channel;
  int                        spu_channel;

  vo_instance_t             *video_out;
  fifo_buffer_t             *video_fifo;
  pthread_t                  video_thread;
  video_decoder_t           *video_decoder_plugins[DECODER_PLUGIN_MAX];
  video_decoder_t           *cur_video_decoder_plugin;
  int                        video_finished;

  ao_functions_t            *audio_out;
  fifo_buffer_t             *audio_fifo;
  pthread_t                  audio_thread;
  audio_decoder_t           *audio_decoder_plugins[DECODER_PLUGIN_MAX];
  int                        num_audio_decoder_plugins;
  audio_decoder_t           *cur_audio_decoder_plugin;
  uint32_t                   audio_track_map[50];
  int                        audio_track_map_entries;
  int                        audio_finished;

  gui_status_callback_func_t status_callback;

  /* Lock for xine player functions */
  pthread_mutex_t            xine_lock;

} xine_t;

/*
 * read config file and init a config object
 * (if it exists)
 */
config_values_t *config_file_init (char *filename);

/*
 * init xine - call once at startup
 *
 */

xine_t *xine_init (vo_driver_t *vo, 
		   ao_functions_t *ao,
		   gui_status_callback_func_t gui_status_callback,
		   config_values_t *config);

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
 * internal use only
 */

void xine_notify_stream_finished (xine_t *this);

/*
 * video decoder stuff
 */

/*
 * init video decoders, allocate video fifo,
 * start video decoder thread
 */

void video_decoder_init (xine_t *this);

/*
 * quit video thread
 */

void video_decoder_shutdown (xine_t *this);

/*
 * init audio decoders, allocate audio fifo,
 * start audio decoder thread
 */

void audio_decoder_init (xine_t *this);

/*
 * quit audio thread
 */

void audio_decoder_shutdown (xine_t *this);


/* 
 * Load input/demux/audio_out/video_out plugins
 */

/* plugin naming scheme */
#define XINE_INPUT_PLUGIN_PREFIXNAME            "xineplug_inp_"
#define XINE_INPUT_PLUGIN_PREFIXNAME_LENGTH     13

#define XINE_DEMUXER_PLUGIN_PREFIXNAME          "xineplug_dmx_"
#define XINE_DEMUXER_PLUGIN_PREFIXNAME_LENGTH   13

#define XINE_VIDEO_OUT_PLUGIN_PREFIXNAME        "xineplug_vo_out_"
#define XINE_VIDEO_OUT_PLUGIN_PREFIXNAME_LENGTH 16

#define XINE_AUDIO_OUT_PLUGIN_PREFIXNAME        "xineplug_ao_out_"
#define XINE_AUDIO_OUT_PLUGIN_PREFIXNAME_LENGTH 16

#define XINE_DECODER_PLUGIN_PREFIXNAME          "xineplug_decode_"
#define XINE_DECODER_PLUGIN_PREFIXNAME_LENGTH   16

/*
 * load all available demuxer plugins
 */
void load_demux_plugins (xine_t *this, 
			 config_values_t *config, int iface_version);

/*
 * load all available input plugins
 */

void load_input_plugins (xine_t *this, 
			 config_values_t *config, int iface_version);

/*
 * load all available decoder plugins
 */
void load_decoder_plugins (xine_t *this, 
			   config_values_t *config, int iface_version);

/*
 * output driver load support functions
 */

/* video */

#define VISUAL_TYPE_X11   1
#define VISUAL_TYPE_FB    2
#define VISUAL_TYPE_GTK   3

/*
 * list_video_output_plugins
 *
 * returns a list of available video output plugins for
 * the specified visual type - the list is sorted by plugin
 * priority
 */

char **xine_list_video_output_plugins (int visual_type);

/*
 * load_video_output_plugin
 *
 * load a specific video output plugin
 */

vo_driver_t *xine_load_video_output_plugin(config_values_t *config,
					   char *id, int visual_type, void *visual);

/*
 * audio output plugin dynamic loading stuff
 */

/*
 * list_audio_output_plugins
 *
 * returns a list of available audio output plugins 
 * the list returned is sorted by plugin priority
 */

char **xine_list_audio_output_plugins ();

/*
 * load_audio_output_plugin
 *
 * load a specific audio output plugin
 */

ao_functions_t *xine_load_audio_output_plugin(config_values_t *config, char *id);

#endif
