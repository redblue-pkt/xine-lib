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
 * $Id: xine_internal.h,v 1.100 2002/09/18 00:51:34 guenter Exp $
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
#include "libspudec/spu_decoder_api.h"
#else
#include "spu_decoder_api.h"
#endif

#include "osd.h"
#include "scratch.h"
#include "xineintl.h"
#include "plugin_catalog.h"

#define VIDEO_DECODER_IFACE_VERSION      10
#define AUDIO_DECODER_IFACE_VERSION      9
#define XINE_MAX_EVENT_LISTENERS         50

/* used by plugin loader */
#define XINE_VERSION_CODE                XINE_MAJOR_VERSION*10000+XINE_MINOR_VERSION*100+XINE_SUB_VERSION


/*
 * generic xine video decoder plugin interface
 *
 * for a dynamic plugin make sure you provide this function call:
 * video_decoder_t *init_video_decoder_plugin (int iface_version,  
 *                                             xine_t *xine);
 */

typedef struct video_decoder_s video_decoder_t;

struct video_decoder_s {

  void (*init) (video_decoder_t *this, vo_instance_t *video_out);

  void (*decode_data) (video_decoder_t *this, buf_element_t *buf);

  void (*reset) (video_decoder_t *this);
  
  void (*flush) (video_decoder_t *this);

  void (*close) (video_decoder_t *this);

  char* (*get_identifier) (void);

  void (*dispose) (video_decoder_t *this);

};

/*
 * generic xine audio decoder plugin interface
 *
 * for a dynamic plugin make sure you provide this function call:
 * audio_decoder_t *init_audio_decoder_plugin (int iface_version,  
 *                                             xine_t *xine);
 */

typedef struct audio_decoder_s audio_decoder_t;

struct audio_decoder_s {

  void (*init) (audio_decoder_t *this, ao_instance_t *audio_out);

  void (*decode_data) (audio_decoder_t *this, buf_element_t *buf);

  void (*reset) (audio_decoder_t *this);
  
  void (*close) (audio_decoder_t *this);

  char* (*get_identifier) (void);

  void (*dispose) (audio_decoder_t *this);

};

/*
 * log constants
 */

#define XINE_LOG_MSG       0 /* warnings, errors, ... */
#define XINE_LOG_PLUGIN    1
#define XINE_LOG_NUM       2 /* # of log buffers defined */

#define XINE_STREAM_INFO_MAX 99

/*
 * the big xine struct, holding everything together
 */

struct xine_s {
  
  /* private : */

  metronom_t                *metronom;
  
  config_values_t           *config;

  /* MRL of displayed logo */
  char                      *logo_mrl;
  /* Logo manipulation mutex */
  pthread_mutex_t            logo_lock;

  plugin_catalog_t          *plugin_catalog;
  
  input_plugin_t            *cur_input_plugin;
  /* kept to do proper ejecting (otherwise we eject the logo) */
  input_plugin_t            *last_input_plugin;

  demux_plugin_t            *cur_demuxer_plugin;
  int                        demux_strategy;

  int                        status;
  int                        speed;
  off_t                      cur_input_pos;
  off_t                      cur_input_length;
  int                        cur_input_time;
  char                       cur_mrl[1024];

  spu_functions_t           *spu_out;
  pthread_t                  spu_thread;
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
  int                        spu_channel_letterbox;
  int                        spu_channel_pan_scan;
  int                        spu_channel;

  xine_vo_driver_t          *video_driver;
  vo_instance_t             *video_out;
  fifo_buffer_t             *video_fifo;
  pthread_t                  video_thread;
  video_decoder_t           *cur_video_decoder_plugin;
  int                        video_finished;
  int                        video_in_discontinuity;
  int                        video_channel;
  
  osd_renderer_t            *osd_renderer;
  osd_object_t              *osd;
  int                        osd_display;

  ao_instance_t             *audio_out;
  fifo_buffer_t             *audio_fifo;
  lrb_t                     *audio_temp;
  pthread_t                  audio_thread;
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
  xine_event_listener_cb_t   event_listeners[XINE_MAX_EVENT_LISTENERS];
  void                      *event_listener_user_data[XINE_MAX_EVENT_LISTENERS];
  uint16_t                   num_event_listeners;

  /* scratch string buffers */
  char                       str[1024];
  char                       spu_lang[80];
  char                       audio_lang[80];
  
  /* log output that may be presented to the user */
  scratch_buffer_t          *log_buffers[XINE_LOG_NUM];

  int                        err;

  pthread_t                  finished_thread;
  int                        finished_thread_running;
  
  xine_report_codec_cb_t     report_codec_cb;
  void                      *report_codec_user_data;
  
  int                        playing_logo;
  int                        curtime_needed_for_osd;
  pthread_mutex_t            osd_lock;

  /* stream meta information */
  int                        stream_info[XINE_STREAM_INFO_MAX];
  char                      *meta_info  [XINE_STREAM_INFO_MAX];

  int                        header_sent_counter; /* wait for headers sent */
};

/*
 * private function prototypes:
 */

int  xine_open_internal          (xine_t *this, const char *mrl);
int  xine_play_internal          (xine_t *this,
				  int start_pos, int start_time);
void xine_stop_internal          (xine_t *this);
void xine_notify_stream_finished (xine_t *this);
void xine_report_codec           (xine_t *this, int codec_type, 
				  uint32_t fourcc, uint32_t buf_type, int handled );
void xine_internal_osd           (xine_t *this, char *str, int duration);

void video_decoder_init          (xine_t *this);
void video_decoder_shutdown      (xine_t *this);

void audio_decoder_init          (xine_t *this);
void audio_decoder_shutdown      (xine_t *this);

/* 
 * demuxer helper functions from demux.c 
 */

void xine_demux_flush_engine     (xine_t *this);

void xine_demux_control_newpts   (xine_t *this, int64_t pts, uint32_t flags );

void xine_demux_control_headers_done (xine_t *this );

void xine_demux_control_start    (xine_t *this );

void xine_demux_control_end      (xine_t *this, uint32_t flags );

/*
 * plugin management
 */

/*
 * on-demand loading of audio/video/spu decoder plugins
 */

video_decoder_t *get_video_decoder (xine_t *this, uint8_t stream_type); 
audio_decoder_t *get_audio_decoder (xine_t *this, uint8_t stream_type); 
spu_decoder_t   *get_spu_decoder   (xine_t *this, uint8_t stream_type); 

/* 
 * plugin_loader functions
 *
 */

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


void xine_set_speed (xine_t *this, int speed) ;

void xine_select_spu_channel (xine_t *this, int channel) ;

int xine_get_audio_channel (xine_t *this) ;

int xine_get_spu_channel (xine_t *this) ;

#ifdef __cplusplus
}
#endif

#endif
