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
 * $Id: xine_internal.h,v 1.72 2002/02/17 17:32:51 guenter Exp $
 *
 */

#ifndef HAVE_XINE_INTERNAL_H
#define HAVE_XINE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#ifdef XINE_COMPILE
#include "input/input_plugin.h"
#include "demuxers/demux.h"
#else
#include "input_plugin.h"
#include "demux.h"
#endif
#include "video_out.h"
#include "audio_out.h"
#include "metronom.h"
#include "spu_decoder.h"
#include "events.h"
#include "lrb.h"
#ifdef XINE_COMPILE
#include "libspudec/spu_decoder_api.h"
#else
#include "spu_decoder_api.h"
#endif
#include "osd.h"
#include "scratch.h"
#include "xineintl.h"

#define INPUT_PLUGIN_MAX       50
#define DEMUXER_PLUGIN_MAX     50
#define DECODER_PLUGIN_MAX     256
#define DECODER_PLUGIN_IFACE_VERSION      5
#define AUDIO_OUT_PLUGIN_MAX   50
#define VIDEO_OUT_PLUGIN_MAX   50
#define XINE_MAX_EVENT_LISTENERS 50

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

  void (*flush) (video_decoder_t *this);

  void (*close) (video_decoder_t *this);

  char* (*get_identifier) (void);

  int priority;

  metronom_t *metronom;

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

  void (*init) (audio_decoder_t *this, ao_instance_t *audio_out);

  void (*decode_data) (audio_decoder_t *this, buf_element_t *buf);

  void (*reset) (audio_decoder_t *this);
  
  void (*close) (audio_decoder_t *this);

  char* (*get_identifier) (void);

  int priority;

};

/*
 * gui callback functions
 *
 */

/*
 * player status constants:
 */

#define XINE_STOP      0 
#define XINE_PLAY      1 
#define XINE_QUIT      2

/*
 * log output
 */
#define XINE_LOG_MSG       0 /* warnings, errors, ... */
#define XINE_LOG_FORMAT    1 /* stream format, decoders, video size... */
#define XINE_LOG_PLUGIN    2
#define XINE_LOG_NUM       3 /* # of log buffers defined */

typedef void (*xine_event_listener_t) (void *user_data, xine_event_t *);

struct xine_s {
  
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
  int                        speed;
  off_t                      cur_input_pos;
  int                        cur_input_time;
  char                       cur_mrl[1024];

  spu_functions_t           *spu_out;
  pthread_t                  spu_thread;
  spu_decoder_t             *spu_decoder_plugins[DECODER_PLUGIN_MAX];
  int                        num_spu_decoder_plugins;
  spu_decoder_t             *cur_spu_decoder_plugin;
  int                        spu_finished;

  /* *_user: -2 => off
             -1 => auto (use *_auto value)
	    >=0 => respect the user's choice
  */

  int                        audio_channel_user;
  int                        audio_channel_auto;
  int                        spu_channel_user;
  int                        spu_channel_auto;
  int                        spu_channel;

  vo_driver_t               *video_driver;
  vo_instance_t             *video_out;
  fifo_buffer_t             *video_fifo;
  pthread_t                  video_thread;
  video_decoder_t           *video_decoder_plugins[DECODER_PLUGIN_MAX];
  video_decoder_t           *cur_video_decoder_plugin;
  int                        video_finished;
  int                        video_in_discontinuity;
  
  osd_renderer_t            *osd_renderer;
  osd_object_t              *osd;
  int                        osd_display;

  ao_instance_t             *audio_out;
  fifo_buffer_t             *audio_fifo;
  lrb_t                     *audio_temp;
  pthread_t                  audio_thread;
  audio_decoder_t           *audio_decoder_plugins[DECODER_PLUGIN_MAX];
  int                        num_audio_decoder_plugins;
  audio_decoder_t           *cur_audio_decoder_plugin;
  uint32_t                   audio_track_map[50];
  int                        audio_track_map_entries;
  int                        audio_finished;
  uint32_t                   audio_type;

  /* Lock for xine player functions */
  pthread_mutex_t            xine_lock;

  /* Lock for xxx_finished variables */
  pthread_mutex_t            finished_lock;

  /* Array of event handlers. */
  xine_event_listener_t      event_listeners[XINE_MAX_EVENT_LISTENERS];
  void                      *event_listener_user_data[XINE_MAX_EVENT_LISTENERS];
  uint16_t                   num_event_listeners;

  /* scratch string buffer */
  char                       str[1024];
  
  /* log output that may be presented to the user */
  scratch_buffer_t          *log_buffers[XINE_LOG_NUM];

  int                        err;

};

/*
 * read config file and init a config object
 * (if it exists)
 */
config_values_t *xine_config_file_init (char *filename);

/*
 * init xine - call once at startup
 */

xine_t *xine_init (vo_driver_t *vo, 
		   ao_driver_t *ao,
		   config_values_t *config);

/*
 * open a stream sekk to a given position and play it
 *
 * name       : mrl to open
 * start_pos  : position in input source (0..65535)
 * start_time : position measured in seconds from stream start
 *
 * if both parameters are !=0 start_pos will be used
 * for non-seekable streams both values will be ignored
 *
 * returns 1 on succ, 0 on failure
 */
int xine_play (xine_t *this, char *MRL, int start_pos, int start_time);


/*
 * set/get playback speed
 *
 * constants see below
 */

void xine_set_speed (xine_t *this, int speed);
int xine_get_speed (xine_t *this);

#define SPEED_PAUSE   0
#define SPEED_SLOW_4  1
#define SPEED_SLOW_2  2
#define SPEED_NORMAL  4
#define SPEED_FAST_2  8
#define SPEED_FAST_4 16

/*
 * manually adjust a/v sync
 */

void xine_set_av_offset (xine_t *this, int offset_pts);
int xine_get_av_offset (xine_t *this);

/*
 * stop playing
 */
void xine_stop_internal (xine_t *this);
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
 * get current position measured in seconds from 
 * the beginning of the stream
 */
int xine_get_current_time (xine_t *this);

/*
 * estimate length of input stream in seconds
 * may return 0 if stream is not seekable
 */
int xine_get_stream_length (xine_t *this);

/*
 * return the current physical audio channel
 */
int xine_get_audio_channel (xine_t *this);

/*
 * return the current logical audio channel
 */
int xine_get_audio_selection (xine_t *this);

/*
 * try to find out current audio language
 */
void xine_get_audio_lang (xine_t *this, char *str);

/*
 * set desired logical audio channel (-1 => auto)
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
 * try to find out current spu language
 */
void xine_get_spu_lang (xine_t *this, char *str);

/*
 * check if the stream is seekable (at the moment)
 */

int xine_is_stream_seekable (xine_t *this);

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

mrl_t **xine_get_browse_mrls (xine_t *this, char *plugin_id, 
			      char *start_mrl, int *num_mrls);

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
char **xine_get_autoplay_mrls (xine_t *this, char *plugin_id, int *num_mrls);

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
 * spu decoder stuff
 */

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
 *  list (open and close) all available demuxer plugins
 */
void xine_list_demux_plugins (config_values_t *config,
                          char **identifiers, char **mimetypes);
                          
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
#define VISUAL_TYPE_AA    2
#define VISUAL_TYPE_FB    3
#define VISUAL_TYPE_GTK   4
#define VISUAL_TYPE_DFB   5

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

ao_driver_t *xine_load_audio_output_plugin(config_values_t *config, char *id);

/*
 * sending events
 * event dispatcher mechanism
 */

/*
 * register an event listener callback.
 * returns 0 if the listener was registerd, non-zero if it could not.
 */

int xine_register_event_listener(xine_t *this, xine_event_listener_t listener,
				 void *user_data);

/*
 * attempt to remove a registered event listener.
 * returns 0 if the listener was removed, non-zero if not (e.g. not found).
 */

int xine_remove_event_listener(xine_t *this, xine_event_listener_t listener);

/*
 * send an event to all listeners.
 */

void xine_send_event(xine_t *this, xine_event_t *event);

/*
 * snapshot function
 *
 * returns:
 * width, height : size of image (be aware that u,v may be subsampled)
 * ratio_code    : aspect ratio of the frame
 * format        : subsampling format YUV 4:2:0 or 4:2:2
 * y             : lumiance information
 * u,v           : subsample color information
 */
int xine_get_current_frame (xine_t *this, int *width, int *height,
			    int *ratio_code, int *format,
			    uint8_t **y, uint8_t **u,
			    uint8_t **v);

#define XINE_ASPECT_RATIO_SQUARE      1
#define XINE_ASPECT_RATIO_4_3         2
#define XINE_ASPECT_RATIO_ANAMORPHIC  3
#define XINE_ASPECT_RATIO_211_1       4
#define XINE_ASPECT_RATIO_DONT_TOUCH 42

osd_renderer_t *xine_get_osd_renderer (xine_t *this);
  
/*
 * xine log functions 
 */
  
const char **xine_get_log_names(void);

void xine_log (xine_t *this, int buf, const char *format, ...);

char **xine_get_log (xine_t *this, int buf);

/*
 * xine error reporting
 */

#define XINE_ERROR_NONE              0
#define XINE_ERROR_NO_INPUT_PLUGIN   1
#define XINE_ERROR_NO_DEMUXER_PLUGIN 2

int xine_get_error (xine_t *this);

#ifdef __cplusplus
}
#endif

#endif
