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

#ifndef HAVE_XINE_INTERNAL_H
#define HAVE_XINE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * include public part of xine header
 */

#include <xine.h>
#include <xine/tickets.h>
#include <xine/refcounter.h>
#include <xine/input_plugin.h>
#include <xine/demux.h>
#include <xine/video_out.h>
#include <xine/audio_out.h>
#include <xine/metronom.h>
#include <xine/osd.h>
#include <xine/xineintl.h>
#include <xine/plugin_catalog.h>
#include <xine/video_decoder.h>
#include <xine/audio_decoder.h>
#include <xine/spu_decoder.h>
#include <xine/scratch.h>
#include <xine/broadcaster.h>
#include <xine/io_helper.h>
#include <xine/info_helper.h>
#include <xine/alphablend.h>

#define XINE_MAX_EVENT_LISTENERS         50
#define XINE_MAX_EVENT_TYPES             100
#define XINE_MAX_TICKET_HOLDER_THREADS   64

/* used by plugin loader */
#define XINE_VERSION_CODE                XINE_MAJOR_VERSION*10000+XINE_MINOR_VERSION*100+XINE_SUB_VERSION


/*
 * log constants
 */

#define XINE_LOG_MSG       0 /* warnings, errors, ... */
#define XINE_LOG_PLUGIN    1
#define XINE_LOG_TRACE     2
#define XINE_LOG_NUM       3 /* # of log buffers defined */

#define XINE_STREAM_INFO_MAX 99

/*
 * the "big" xine struct, holding everything together
 */

#ifndef XDG_BASEDIR_H
/* present here for internal convenience only */
typedef struct { void *reserved; } xdgHandle;
#endif

struct xine_s {

  config_values_t           *config;

  plugin_catalog_t          *plugin_catalog;

  int                        verbosity;

  int                        demux_strategy;
  const char                *save_path;

  /* log output that may be presented to the user */
  scratch_buffer_t          *log_buffers[XINE_LOG_NUM];

  xine_list_t               *streams;
  pthread_mutex_t            streams_lock;

  metronom_clock_t          *clock;

  /** Handle for libxdg-basedir functions. */
  xdgHandle                  basedir_handle;
};

/*
 * xine event queue
 */

struct xine_event_queue_s {
  xine_list_t               *events;
  pthread_mutex_t            lock;
  pthread_cond_t             new_event;
  pthread_cond_t             events_processed;
  xine_stream_t             *stream;
  pthread_t                 *listener_thread;
  void                      *user_data;
  xine_event_listener_cb_t   callback;
  int                        callback_running;
};

/*
 * xine_stream - per-stream parts of the xine engine
 */

struct xine_stream_s {

  /* reference to xine context */
  xine_t                    *xine;

  /* metronom instance used by current stream */
  metronom_t                *metronom;

  /* demuxers use input_plugin to read data */
  input_plugin_t            *input_plugin;

  /* used by video decoders, may change by port rewire */
  xine_video_port_t         * volatile video_out;

  /* demuxers send data to video decoders using this fifo */
  fifo_buffer_t             *video_fifo;

  /* used by audio decoders, may change by port rewire */
  xine_audio_port_t         * volatile audio_out;

  /* demuxers send data to audio decoders using this fifo */
  fifo_buffer_t             *audio_fifo;

  /* provide access to osd api */
  osd_renderer_t            *osd_renderer;

  /* master/slave streams */
  xine_stream_t             *master; /* usually a pointer to itself */
  xine_stream_t             *slave;

  /* input_dvd uses this one. is it possible to add helper functions instead? */
  spu_decoder_t             *spu_decoder_plugin;

  /* dxr3 use this one, should be possible to fix to use the port instead */
  vo_driver_t               *video_driver;

  /* these definitely should be made private! */
  int                        audio_channel_auto;
  int                        spu_decoder_streamtype;
  int                        spu_channel_user;
  int                        spu_channel_auto;
  int                        spu_channel_letterbox;
  int                        spu_channel;

  /* current content detection method, see METHOD_BY_xxx */
  int                        content_detection_method;
};

/* when explicitly noted, some functions accept an anonymous stream,
 * which is a valid stream that does not want to be addressed. */
#define XINE_ANON_STREAM ((xine_stream_t *)-1)

typedef struct
{
  int total;
  int ready;
  int avail;
}
xine_query_buffers_data_t;

typedef struct
{
  xine_query_buffers_data_t vi;
  xine_query_buffers_data_t ai;
  xine_query_buffers_data_t vo;
  xine_query_buffers_data_t ao;
}
xine_query_buffers_t;

/*
 * private function prototypes:
 */

int _x_query_network_timeout (xine_t *xine) XINE_PROTECTED;
int _x_query_buffers(xine_stream_t *stream, xine_query_buffers_t *query) XINE_PROTECTED;
int _x_query_buffer_usage(xine_stream_t *stream, int *num_video_buffers, int *num_audio_buffers, int *num_video_frames, int *num_audio_frames) XINE_PROTECTED;
int _x_lock_port_rewiring(xine_t *xine, int ms_to_time_out) XINE_PROTECTED;
void _x_unlock_port_rewiring(xine_t *xine) XINE_PROTECTED;
int _x_lock_frontend(xine_stream_t *stream, int ms_to_time_out) XINE_PROTECTED;
void _x_unlock_frontend(xine_stream_t *stream) XINE_PROTECTED;
int _x_query_unprocessed_osd_events(xine_stream_t *stream) XINE_PROTECTED;
int _x_demux_seek(xine_stream_t *stream, off_t start_pos, int start_time, int playing) XINE_PROTECTED;
int _x_continue_stream_processing(xine_stream_t *stream) XINE_PROTECTED;
void _x_trigger_relaxed_frame_drop_mode(xine_stream_t *stream) XINE_PROTECTED;
void _x_reset_relaxed_frame_drop_mode(xine_stream_t *stream) XINE_PROTECTED;

void _x_handle_stream_end      (xine_stream_t *stream, int non_user) XINE_PROTECTED;

/* report message to UI. usually these are async errors */

int _x_message(xine_stream_t *stream, int type, ...) XINE_SENTINEL XINE_PROTECTED;

/* flush the message queues */

void _x_flush_events_queues (xine_stream_t *stream) XINE_PROTECTED;

/* extra_info operations */
void _x_extra_info_reset( extra_info_t *extra_info ) XINE_PROTECTED;

void _x_extra_info_merge( extra_info_t *dst, extra_info_t *src ) XINE_PROTECTED;

void _x_get_current_info (xine_stream_t *stream, extra_info_t *extra_info, int size) XINE_PROTECTED;


/** @brief Register a list of stream keyframes.
    @param stream The stream that index is for.
    @param list   The array of entries to add.
    @param size   The count of entries.
    @return 0 (OK), 1 (Fail).
*/
int _x_keyframes_set (xine_stream_t *stream, xine_keyframes_entry_t *list, int size) XINE_PROTECTED;

/** @brief Register a stream keyframe to seek index.
    @note  This will try not to duplicate already registered frames.
    @param stream The stream that index is for.
    @param pos    The frame time AND normpos.
    @return  The index *g* into the index where that frame has been added, or -1.
*/
int _x_keyframes_add (xine_stream_t *stream, xine_keyframes_entry_t *pos) XINE_PROTECTED;


/* demuxer helper functions from demux.c */

/*
 *  Flush audio and video buffers. It is called from demuxers on
 *  seek/stop, and may be useful when user input changes a stream and
 *  xine-lib has cached buffers that have yet to be played.
 *
 * warning: after clearing decoders fifos an absolute discontinuity
 *          indication must be sent. relative discontinuities are likely
 *          to cause "jumps" on metronom.
 */
void _x_demux_flush_engine         (xine_stream_t *stream) XINE_PROTECTED;

void _x_demux_control_nop          (xine_stream_t *stream, uint32_t flags) XINE_PROTECTED;
void _x_demux_control_newpts       (xine_stream_t *stream, int64_t pts, uint32_t flags) XINE_PROTECTED;
void _x_demux_control_headers_done (xine_stream_t *stream) XINE_PROTECTED;
void _x_demux_control_start        (xine_stream_t *stream) XINE_PROTECTED;
void _x_demux_control_end          (xine_stream_t *stream, uint32_t flags) XINE_PROTECTED;
int _x_demux_start_thread          (xine_stream_t *stream) XINE_PROTECTED;
int _x_demux_stop_thread           (xine_stream_t *stream) XINE_PROTECTED;
int _x_demux_read_header           (input_plugin_t *input, void *buffer, off_t size) XINE_PROTECTED;
int _x_demux_check_extension       (const char *mrl, const char *extensions);

off_t _x_read_abort (xine_stream_t *stream, int fd, char *buf, off_t todo) XINE_PROTECTED;

int _x_action_pending (xine_stream_t *stream) XINE_PROTECTED;

void _x_action_raise (xine_stream_t *stream) XINE_PROTECTED;
void _x_action_lower (xine_stream_t *stream) XINE_PROTECTED;

void _x_demux_send_data(fifo_buffer_t *fifo, uint8_t *data, int size,
                        int64_t pts, uint32_t type, uint32_t decoder_flags,
                        int input_normpos, int input_time, int total_time,
                        uint32_t frame_number) XINE_PROTECTED;

int _x_demux_read_send_data(fifo_buffer_t *fifo, input_plugin_t *input,
                            int size, int64_t pts, uint32_t type,
                            uint32_t decoder_flags, off_t input_normpos,
                            int input_time, int total_time,
                            uint32_t frame_number) XINE_USED XINE_PROTECTED;

void _x_demux_send_mrl_reference (xine_stream_t *stream, int alternative,
				  const char *mrl, const char *title,
				  int start_time, int duration) XINE_PROTECTED;

/*
 * MRL escaped-character decoding (overwrites the source string)
 */
void _x_mrl_unescape(char *mrl) XINE_PROTECTED;

/*
 * Return a copy of mrl without authentication credentials
 */
char *_x_mrl_remove_auth(const char *mrl) XINE_PROTECTED;

/*
 * plugin_loader functions
 *
 */

/* allow input plugins to use other input plugins */
input_plugin_t *_x_find_input_plugin (xine_stream_t *stream, const char *mrl) XINE_PROTECTED;

/* on-demand loading of generic modules / sub-plugins */
struct xine_module_s; /* xine_module.h */
struct xine_module_s *_x_find_module(xine_t *xine, const char *type, const char *id, unsigned sub_type, const void *params) XINE_PROTECTED;
void _x_free_module(xine_t *xine, struct xine_module_s **pmodule) XINE_PROTECTED;

/* on-demand loading of audio/video/spu decoder plugins */

video_decoder_t *_x_get_video_decoder  (xine_stream_t *stream, uint8_t stream_type) XINE_PROTECTED;
void             _x_free_video_decoder (xine_stream_t *stream, video_decoder_t *decoder) XINE_PROTECTED;
audio_decoder_t *_x_get_audio_decoder  (xine_stream_t *stream, uint8_t stream_type) XINE_PROTECTED;
void             _x_free_audio_decoder (xine_stream_t *stream, audio_decoder_t *decoder) XINE_PROTECTED;
spu_decoder_t   *_x_get_spu_decoder    (xine_stream_t *stream, uint8_t stream_type) XINE_PROTECTED;
void             _x_free_spu_decoder   (xine_stream_t *stream, spu_decoder_t *decoder) XINE_PROTECTED;
/* check for decoder availability - but don't try to initialize it */
int              _x_decoder_available  (xine_t *xine, uint32_t buftype) XINE_PROTECTED;

/* on-demand loading of demux plugins */
demux_plugin_t *_x_find_demux_plugin (xine_stream_t *stream, input_plugin_t *input) XINE_PROTECTED;
demux_plugin_t *_x_find_demux_plugin_by_name (xine_stream_t *stream, const char *name, input_plugin_t *input) XINE_PROTECTED;
void _x_free_demux_plugin (xine_stream_t *stream, demux_plugin_t **demux) XINE_PROTECTED;

/*
 * load_video_output_plugin
 *
 * load a specific video output plugin
 */

vo_driver_t *_x_load_video_output_plugin(xine_t *this_gen,
                                         const char *id, int visual_type,
                                         const void *visual) XINE_PROTECTED;

/*
 * audio output plugin dynamic loading stuff
 */

/*
 * load_audio_output_plugin
 *
 * load a specific audio output plugin
 */

ao_driver_t *_x_load_audio_output_plugin (xine_t *self, const char *id) XINE_PROTECTED;


void _x_set_speed (xine_stream_t *stream, int speed) XINE_PROTECTED;

int _x_get_speed (xine_stream_t *stream) XINE_PROTECTED;

void _x_set_fine_speed (xine_stream_t *stream, int speed) XINE_PROTECTED;

int _x_get_fine_speed (xine_stream_t *stream) XINE_PROTECTED;

void _x_select_spu_channel (xine_stream_t *stream, int channel) XINE_PROTECTED;

int _x_get_audio_channel (xine_stream_t *stream) XINE_PROTECTED;

int _x_get_spu_channel (xine_stream_t *stream) XINE_PROTECTED;

int _x_get_video_streamtype (xine_stream_t *) XINE_PROTECTED;

/*
 * internal events
 */

/* sent by dvb frontend to inform ts demuxer of new pids */
#define XINE_EVENT_PIDS_CHANGE	          0x80000000
/* sent by BluRay input plugin to inform ts demuxer about end of clip */
#define XINE_EVENT_END_OF_CLIP            0x80000001

/*
 * pids change event - inform ts demuxer of new pids
 */
typedef struct {
  int                 vpid; /* video program id */
  int                 apid; /* audio program id */
} xine_pids_data_t;

#ifdef __cplusplus
}
#endif

#endif

