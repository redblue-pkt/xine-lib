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
 * $Id: xine_internal.h,v 1.111 2002/10/31 16:58:25 mroi Exp $
 *
 */

#ifndef HAVE_XINE_INTERNAL_H
#define HAVE_XINE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

/*
 * include public part of xine header
 */

#ifdef XINE_COMPILE
#include "include/xine.h"
#else
#include "xine.h"
#endif

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
#include "lrb.h"

#ifdef XINE_COMPILE
#include "libspudec/nav_types.h"
#include "libspudec/spu_decoder_api.h"
#else
#include "nav_types.h"
#include "spu_decoder_api.h"
#endif

#include "osd.h"
#include "scratch.h"
#include "xineintl.h"
#include "plugin_catalog.h"
#include "video_decoder.h"
#include "audio_decoder.h"

#define XINE_MAX_EVENT_LISTENERS         50
#define XINE_MAX_EVENT_TYPES             100

#define XINE_CODEC_AUDIO   0
#define XINE_CODEC_VIDEO   1

/* used by plugin loader */
#define XINE_VERSION_CODE                XINE_MAJOR_VERSION*10000+XINE_MINOR_VERSION*100+XINE_SUB_VERSION


/*
 * log constants
 */

#define XINE_LOG_MSG       0 /* warnings, errors, ... */
#define XINE_LOG_PLUGIN    1
#define XINE_LOG_NUM       2 /* # of log buffers defined */

#define XINE_STREAM_INFO_MAX 99

/*
 * the "big" xine struct, holding everything together
 */

struct xine_s {
  
  config_values_t           *config;

  plugin_catalog_t          *plugin_catalog;
  
  int                        demux_strategy;

  /* log output that may be presented to the user */
  scratch_buffer_t          *log_buffers[XINE_LOG_NUM];

  int                        verbosity;

  xine_list_t               *streams;
  pthread_mutex_t            streams_lock;
};

/*
 * xine event queue
 */

struct xine_event_queue_s {
  xine_list_t               *events;
  pthread_mutex_t            lock;
  pthread_cond_t             new_event;
  xine_stream_t             *stream;
  pthread_t                 *listener_thread;
  xine_event_listener_cb_t   callback;
  void                      *user_data;
};

/*
 * xine_stream - per-stream parts of the xine engine
 */

struct xine_stream_s {
  
  xine_t                    *xine;

  int                        status;

  input_plugin_t            *input_plugin;
  input_class_t             *input_class; /* eject */
  int                        content_detection_method;
  demux_plugin_t            *demux_plugin;

  metronom_t                *metronom;
  int                        speed;
  off_t                      input_pos;
  off_t                      input_length;
  int                        input_time;

  xine_vo_driver_t          *video_driver;
  vo_instance_t             *video_out;
  fifo_buffer_t             *video_fifo;
  pthread_t                  video_thread;
  video_decoder_t           *video_decoder_plugin;
  int                        video_decoder_streamtype;
  int                        video_in_discontinuity;
  int                        video_channel;
  
  ao_instance_t             *audio_out;
  fifo_buffer_t             *audio_fifo;
  lrb_t                     *audio_temp;
  pthread_t                  audio_thread;
  audio_decoder_t           *audio_decoder_plugin;
  int                        audio_decoder_streamtype;
  uint32_t                   audio_track_map[50];
  int                        audio_track_map_entries;
  uint32_t                   audio_type;
  /* *_user: -2 => off
             -1 => auto (use *_auto value)
	    >=0 => respect the user's choice
  */
  int                        audio_channel_user;
  int                        audio_channel_auto;

  spu_functions_t           *spu_out;
  pthread_t                  spu_thread;
  spu_decoder_t             *spu_decoder_plugin;
  int                        spu_decoder_streamtype;
  int                        spu_channel_user;
  int                        spu_channel_auto;
  int                        spu_channel_letterbox;
  int                        spu_channel_pan_scan;
  int                        spu_channel;

  /* lock for public xine player functions */
  pthread_mutex_t            frontend_lock;

  pthread_mutex_t            osd_lock;
  osd_renderer_t            *osd_renderer;

  /* stream meta information */
  int                        stream_info[XINE_STREAM_INFO_MAX];
  char                      *meta_info  [XINE_STREAM_INFO_MAX];

  /* seeking slowdown */
  int                        first_frame_flag;
  pthread_mutex_t            first_frame_lock;
  pthread_cond_t             first_frame_reached;

  /* wait for headers sent / stream decoding finished */
  pthread_mutex_t            counter_lock;
  pthread_cond_t             counter_changed;
  int                        header_count_audio; 
  int                        header_count_video; 
  int                        finished_count_audio; 
  int                        finished_count_video; 

  /* event mechanism */
  xine_list_t               *event_queues;
  pthread_mutex_t            event_queues_lock;
  
  /* demux thread stuff */
  pthread_t                  demux_thread;
  int                        demux_thread_running;
  pthread_mutex_t            demux_lock;
  int                        demux_action_pending;

  int                        err;
};



/*
 * private function prototypes:
 */

void xine_handle_stream_end      (xine_stream_t *stream, int non_user);

/* find and instantiate input and demux plugins */

input_plugin_t *find_input_plugin (xine_stream_t *stream, const char *mrl);
demux_plugin_t *find_demux_plugin (xine_stream_t *stream, input_plugin_t *input);
demux_plugin_t *find_demux_plugin_by_name (xine_stream_t *stream, const char *name, input_plugin_t *input);

/* create decoder fifos and threads */

void video_decoder_init          (xine_stream_t *stream);
void video_decoder_shutdown      (xine_stream_t *stream);

void audio_decoder_init          (xine_stream_t *stream);
void audio_decoder_shutdown      (xine_stream_t *stream);

/* this will just set stream->stream_info accordingly so frontends
   can find out wheter audio/video is handled or not */
void xine_report_codec (xine_stream_t *stream, int codec_type, 
                        uint32_t fourcc, uint32_t buf_type, int handled) ;

/* demuxer helper functions from demux.c */

void xine_demux_flush_engine         (xine_stream_t *stream);
void xine_demux_control_newpts       (xine_stream_t *stream, int64_t pts, uint32_t flags);
void xine_demux_control_headers_done (xine_stream_t *stream);
void xine_demux_control_start        (xine_stream_t *stream);
void xine_demux_control_end          (xine_stream_t *stream, uint32_t flags);
int xine_demux_start_thread          (xine_stream_t *stream);
int xine_demux_stop_thread           (xine_stream_t *stream);

/* 
 * plugin_loader functions
 *
 */

/* on-demand loading of audio/video/spu decoder plugins */

video_decoder_t *get_video_decoder  (xine_stream_t *stream, uint8_t stream_type); 
void             free_video_decoder (xine_stream_t *stream, video_decoder_t *decoder);
audio_decoder_t *get_audio_decoder  (xine_stream_t *stream, uint8_t stream_type); 
void             free_audio_decoder (xine_stream_t *stream, audio_decoder_t *decoder);
spu_decoder_t   *get_spu_decoder    (xine_stream_t *stream, uint8_t stream_type); 
void             free_spu_decoder   (xine_stream_t *stream, spu_decoder_t *decoder);

/*
 * load_video_output_plugin
 *
 * load a specific video output plugin
 */

xine_vo_driver_t *xine_load_video_output_plugin(xine_t *this,
						char *id, int visual_type, void *visual);

/*
 * audio output plugin dynamic loading stuff
 */

/*
 * load_audio_output_plugin
 *
 * load a specific audio output plugin
 */

xine_ao_driver_t *xine_load_audio_output_plugin (xine_t *self, char *id);


void xine_set_speed (xine_stream_t *stream, int speed) ;

void xine_select_spu_channel (xine_stream_t *stream, int channel) ;

int xine_get_audio_channel (xine_stream_t *stream) ;

int xine_get_spu_channel (xine_stream_t *stream) ;

#ifdef __cplusplus
}
#endif

#endif
