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
 * $Id: xine.c,v 1.177 2002/10/27 01:52:15 guenter Exp $
 *
 * top-level xine functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#if defined (__linux__)
#include <endian.h>
#elif defined (__FreeBSD__)
#include <machine/endian.h>
#endif

#include "xine_internal.h"
#include "plugin_catalog.h"
#include "audio_out.h"
#include "video_out.h"
#include "demuxers/demux.h"
#include "buffer.h"
#include "libspudec/spu_decoder_api.h"
/* TODO: who uses spu_decoder.h ? */
#include "spu_decoder.h"
#include "input/input_plugin.h"
#include "metronom.h"
#include "configfile.h"
#include "osd.h"

#include "xineutils.h"
#include "compat.h"

/*
#define LOG
*/

void xine_handle_stream_end (xine_stream_t *stream, int non_user) {


  if (stream->status == XINE_STATUS_QUIT)
    return;
  stream->status = XINE_STATUS_STOP;
    
  if (non_user) {
    /* frontends will not be interested in receiving this event
     * if they have called xine_stop explicitly, so only send
     * it if stream playback finished because of stream end reached
     */

    xine_event_t event;

    event.data_length = 0;
    event.type        = XINE_EVENT_UI_PLAYBACK_FINISHED;
    
    xine_event_send (stream, &event);
  }
}

void xine_report_codec (xine_stream_t *stream, int codec_type, 
			uint32_t fourcc, uint32_t buf_type, int handled) {

  if (codec_type == XINE_CODEC_VIDEO) {
    stream->stream_info[XINE_STREAM_INFO_VIDEO_FOURCC]  = fourcc;
    stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = handled;
  } else {
    stream->stream_info[XINE_STREAM_INFO_AUDIO_FOURCC]  = fourcc;
    stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED] = handled;
  }
}


static void xine_set_speed_internal (xine_stream_t *stream, int speed) {

  stream->metronom->set_speed (stream->metronom, speed);

  /* see coment on audio_out loop about audio_paused */
  if( stream->audio_out ) {
    stream->audio_out->audio_paused = (speed != XINE_SPEED_NORMAL) + 
      (speed == XINE_SPEED_PAUSE);

    /*
     * slow motion / fast forward does not play sound, drop buffered
     * samples from the sound driver
     */
    if (speed != XINE_SPEED_NORMAL && speed != XINE_SPEED_PAUSE)
      stream->audio_out->control(stream->audio_out, AO_CTRL_FLUSH_BUFFERS);

    stream->audio_out->control(stream->audio_out,
			       speed == XINE_SPEED_PAUSE ? AO_CTRL_PLAY_PAUSE : AO_CTRL_PLAY_RESUME);
  }
  
  stream->speed = speed;
}


static void xine_stop_internal (xine_stream_t *stream) {

  int finished_count_audio = 0;
  int finished_count_video = 0;

#ifdef LOG
  printf ("xine: xine_stop. status before = %d\n", stream->status);
#endif

  if (stream->status == XINE_STATUS_STOP) {
#ifdef LOG
    printf ("xine: xine_stop ignored\n");
#endif
    return;
  }
  
  /* make sure we're not in "paused" state */
  xine_set_speed_internal (stream, XINE_SPEED_NORMAL);

  /* Don't change status if we're quitting */
  if (stream->status != XINE_STATUS_QUIT)
    stream->status = XINE_STATUS_STOP;
    
  /*
   * stop demux
   */

  pthread_mutex_lock (&stream->counter_lock);
  if (stream->audio_fifo)
    finished_count_audio = stream->finished_count_audio + 1;
  else
    finished_count_audio = 0;

  finished_count_video = stream->finished_count_video + 1;
  pthread_mutex_unlock (&stream->counter_lock);

#ifdef LOG
  printf ("xine_stop: stopping demux\n");
#endif
  if (stream->demux_plugin) {
    stream->demux_plugin->dispose (stream->demux_plugin);
    stream->demux_plugin = NULL;

    /*
     * wait until engine has really stopped
     */

#if 0
    pthread_mutex_lock (&stream->counter_lock);
    while ((stream->finished_count_audio<finished_count_audio) || 
	   (stream->finished_count_video<finished_count_video)) {
#ifdef LOG
      printf ("xine: waiting for finisheds.\n");
#endif
      pthread_cond_wait (&stream->counter_changed, &stream->counter_lock);
    }
    pthread_mutex_unlock (&stream->counter_lock);
#endif
  }
#ifdef LOG
  printf ("xine_stop: demux stopped\n");
#endif

  /*
   * close input plugin
   */

  if (stream->input_plugin) {
    stream->input_plugin->dispose(stream->input_plugin);
    stream->input_plugin = NULL;
  }

  /* remove buffered samples from the sound device driver */
  if (stream->audio_out)
    stream->audio_out->control (stream->audio_out, AO_CTRL_FLUSH_BUFFERS);

#ifdef LOG
  printf ("xine_stop: done\n");
#endif
}

void xine_stop (xine_stream_t *stream) {

  pthread_mutex_lock (&stream->frontend_lock);

  xine_stop_internal (stream);
  
  /*
   * stream will make output threads discard about everything
   * am i abusing of xine architeture? :)
   */
  stream->metronom->adjust_clock (stream->metronom,
				  stream->metronom->get_current_time(stream->metronom) + 30 * 90000 );
  
  pthread_mutex_unlock (&stream->frontend_lock);
}

xine_stream_t *xine_stream_new (xine_t *this, 
				xine_ao_driver_t *ao, xine_vo_driver_t *vo) {

  xine_stream_t *stream;
  int            i;

  printf ("xine: xine_stream_new\n");

  /*
   * create a new stream object
   */

  pthread_mutex_lock (&this->streams_lock);

  stream = (xine_stream_t *) xine_xmalloc (sizeof (xine_stream_t)) ;

  stream->xine                   = this;
  stream->status                 = XINE_STATUS_STOP;
  for (i=0; i<XINE_STREAM_INFO_MAX; i++) {
    stream->stream_info[i]       = 0;
    stream->meta_info[i]         = NULL;
  }
  stream->speed                  = XINE_SPEED_NORMAL;
  stream->input_pos              = 0;
  stream->input_length           = 0;
  stream->input_time             = 0;
  stream->spu_out                = NULL;
  stream->spu_decoder_plugin     = NULL;
  stream->spu_decoder_streamtype = -1;
  stream->audio_channel_user     = -1;
  stream->audio_channel_auto     = 0;
  stream->audio_decoder_plugin   = NULL;
  stream->audio_decoder_streamtype = -1;
  stream->spu_channel_auto       = -1;
  stream->spu_channel_letterbox  = -1;
  stream->spu_channel_pan_scan   = -1;
  stream->spu_channel_user       = -1;
  stream->spu_channel            = -1;
  stream->video_driver           = vo;
  stream->video_in_discontinuity = 0;
  stream->video_channel          = 0;
  stream->video_decoder_plugin   = NULL;
  stream->video_decoder_streamtype = -1;
  stream->header_count_audio     = 0; 
  stream->header_count_video     = 0; 
  stream->finished_count_audio   = 0; 
  stream->finished_count_video   = 0; 
  stream->err                    = 0;

  /*
   * init mutexes and conditions
   */

  pthread_mutex_init (&stream->frontend_lock, NULL);
  pthread_mutex_init (&stream->event_queues_lock, NULL);
  pthread_mutex_init (&stream->osd_lock, NULL);
  pthread_mutex_init (&stream->counter_lock, NULL);
  pthread_cond_init  (&stream->counter_changed, NULL);

  /*
   * event queues
   */

  stream->event_queues = xine_list_new ();

  /*
   * create a metronom
   */

  stream->metronom = metronom_init ( (ao != NULL), stream);

  /*
   * alloc fifos, init and start decoder threads
   */

  stream->video_out = vo_new_instance (vo, stream);
  video_decoder_init (stream);

  if (ao) 
    stream->audio_out = ao_new_instance (ao, stream);
  audio_decoder_init (stream);

  /*
   * osd
   */

  stream->osd_renderer = osd_renderer_init (stream->video_out->get_overlay_instance (stream->video_out), stream->xine->config );
  
  /*
   * start metronom clock
   */

  stream->metronom->start_clock (stream->metronom, 0);

  /*
   * register stream
   */

  xine_list_append_content (this->streams, stream);

  pthread_mutex_unlock (&this->streams_lock);

  return stream;
}

static int xine_open_internal (xine_stream_t *stream, const char *mrl) {

  int header_count_audio;
  int header_count_video;

#ifdef LOG
  printf ("xine: xine_open_internal '%s'...\n", mrl);
#endif

  /*
   * stop engine if necessary
   */

  xine_stop_internal (stream);

#ifdef LOG
  printf ("xine: engine should be stopped now\n");
#endif

  /*
   * find an input plugin
   */

  if (!(stream->input_plugin = find_input_plugin (stream, mrl))) {
    xine_log (stream->xine, XINE_LOG_MSG,
	      _("xine: cannot find input plugin for this MRL\n"));

    stream->err = XINE_ERROR_NO_INPUT_PLUGIN;
    return 0;
  }
  stream->input_class = stream->input_plugin->input_class;
  stream->meta_info[XINE_META_INFO_INPUT_PLUGIN]
    = strdup (stream->input_class->get_identifier (stream->input_class));


  /*
   * find a demux plugin
   */
  if (!(stream->demux_plugin=find_demux_plugin (stream, stream->input_plugin))) {
    xine_log (stream->xine, XINE_LOG_MSG,
	      _("xine: couldn't find demux for >%s<\n"), mrl);
    stream->input_plugin->dispose (stream->input_plugin);
    stream->input_plugin = NULL;
    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;

    /* remove buffered samples from the sound device driver */
    if (stream->audio_out)
      stream->audio_out->control (stream->audio_out, AO_CTRL_FLUSH_BUFFERS);

    stream->status = XINE_STATUS_STOP;
    return 0;
  }

#ifdef LOG
  printf ("xine: demux and input plugin found\n");
#endif

  stream->meta_info[XINE_META_INFO_SYSTEMLAYER]
    = strdup (stream->demux_plugin->demux_class->get_identifier(stream->demux_plugin->demux_class));

  /*
   * send and decode headers
   */

  pthread_mutex_lock (&stream->counter_lock);
  if (stream->audio_fifo)
    header_count_audio = stream->header_count_audio + 1;
  else
    header_count_audio = 0;

  header_count_video = stream->header_count_video + 1;
  pthread_mutex_unlock (&stream->counter_lock);
  
  stream->demux_plugin->send_headers (stream->demux_plugin);

  if (stream->demux_plugin->get_status(stream->demux_plugin) != DEMUX_OK) {
    xine_log (stream->xine, XINE_LOG_MSG,
	      _("xine: demuxer failed to start\n"));

    stream->demux_plugin->dispose (stream->demux_plugin);
    stream->demux_plugin = NULL;

    printf ("xine: demux disposed\n");

    stream->input_plugin->dispose (stream->input_plugin);
    stream->input_plugin = NULL;
    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;

    /* remove buffered samples from the sound device driver */
    if (stream->audio_out)
      stream->audio_out->control (stream->audio_out, AO_CTRL_FLUSH_BUFFERS);

    stream->status = XINE_STATUS_STOP;

    printf ("xine: return from xine_open_internal\n");

    return 0;
  }

  pthread_mutex_lock (&stream->counter_lock);
  while ((stream->header_count_audio<header_count_audio) || 
	 (stream->header_count_video<header_count_video)) {
    printf ("xine: waiting for headers.\n");
    pthread_cond_wait (&stream->counter_changed, &stream->counter_lock);
  }
  pthread_mutex_unlock (&stream->counter_lock);

#ifdef LOG
  printf ("xine: xine_open_internal done\n");
#endif
  return 1;
}

int xine_open (xine_stream_t *stream, const char *mrl) {

  int ret;

  pthread_mutex_lock (&stream->frontend_lock);

#ifdef LOG
  printf ("xine: xine_open %s\n", mrl);
#endif

  ret = xine_open_internal (stream, mrl);

  pthread_mutex_unlock (&stream->frontend_lock);

  return ret;
}


static int xine_play_internal (xine_stream_t *stream, int start_pos, int start_time) {

  double     share ;
  off_t      pos, len;
  int        demux_status;

  printf ("xine: xine_play\n");

  if (stream->speed != XINE_SPEED_NORMAL) 
    xine_set_speed_internal (stream, XINE_SPEED_NORMAL);

  /*
   * start/seek demux
   */
  if (start_pos) {
    /* FIXME: do we need to protect concurrent access to input plugin here? */
    len = stream->input_plugin->get_length (stream->input_plugin);
    share = (double) start_pos / 65535;
    pos = (off_t) (share * len) ;
  } else
    pos = 0;
  
  if (!stream->demux_plugin) {
    xine_log (stream->xine, XINE_LOG_MSG, 
	      _("xine_play: no demux available\n"));
    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;
    
    return 0;
  }    
  
  if (stream->status == XINE_STATUS_STOP) {

    demux_status = stream->demux_plugin->start (stream->demux_plugin,
						    pos, start_time);
  } else {
    demux_status = stream->demux_plugin->seek (stream->demux_plugin,
						   pos, start_time);
  }

  if (demux_status != DEMUX_OK) {
    xine_log (stream->xine, XINE_LOG_MSG, 
	      _("xine_play: demux failed to start\n"));
    
    stream->err = XINE_ERROR_DEMUX_FAILED;
    
    if (stream->status == XINE_STATUS_STOP)   
      stream->input_plugin->dispose(stream->input_plugin);
  
    return 0;
    
  } else {
    stream->status = XINE_STATUS_PLAY;
  }

  printf ("xine: xine_play_internal ...done\n");

  return 1;
}             

int xine_play (xine_stream_t *stream, int start_pos, int start_time) {

  int ret;

  pthread_mutex_lock (&stream->frontend_lock);

  ret = xine_play_internal (stream, start_pos, start_time/1000);

  pthread_mutex_unlock (&stream->frontend_lock);
  
  return ret;
}

int xine_eject (xine_stream_t *stream) {
  
  int status;

  if (!stream->input_class) 
    return 0;
  
  pthread_mutex_lock (&stream->frontend_lock);

  status = 0;
  if ((stream->status == XINE_STATUS_STOP)
      && stream->input_class && stream->input_class->eject_media) {

    status = stream->input_class->eject_media (stream->input_class);
  }

  pthread_mutex_unlock (&stream->frontend_lock);
  return status;
}

void xine_dispose (xine_stream_t *stream) {

  printf ("xine: xine_dispose\n");

  stream->status = XINE_STATUS_QUIT;

  xine_stop(stream);

  printf ("xine_exit: shutdown audio\n");

  audio_decoder_shutdown (stream);

  printf ("xine_exit: shutdown video\n");

  video_decoder_shutdown (stream);

  stream->osd_renderer->close( stream->osd_renderer );
  stream->video_out->exit (stream->video_out);
  stream->video_fifo->dispose (stream->video_fifo);

  pthread_mutex_destroy (&stream->frontend_lock);
  pthread_mutex_destroy (&stream->counter_lock);
  pthread_mutex_destroy (&stream->osd_lock);
  pthread_mutex_destroy (&stream->event_queues_lock);
  pthread_cond_destroy (&stream->counter_changed);

  free (stream);
}

void xine_exit (xine_t *this) {

  /* FIXME */

  printf ("xine_exit: bye!\n");

#if 0

  int i;

  for (i = 0; i < XINE_LOG_NUM; i++)
    stream->log_buffers[i]->dispose (stream->log_buffers[i]);

  stream->metronom->exit (stream->metronom);

  dispose_plugins (stream);
  xine_profiler_print_results ();
  stream->config->dispose(stream->config);

#endif
}

xine_t *xine_new (void) {

  xine_t      *this;
  int          i;

  this = xine_xmalloc (sizeof (xine_t));
  if (!this) {
    printf ("xine: failed to malloc xine_t\n");
    abort();
  }

  this->verbosity = 0;

#ifdef ENABLE_NLS
  /*
   * i18n
   */

  bindtextdomain("xine-lib", XINE_LOCALEDIR);
#endif 

  /*
   * config
   */

  this->config = xine_config_init ();

  /* 
   * log buffers 
   */

  for (i = 0; i < XINE_LOG_NUM; i++)
    this->log_buffers[i] = new_scratch_buffer (25);
  
  /*
   * streams_lock
   */

  pthread_mutex_init (&this->streams_lock, NULL);

  return this;
}

void xine_init (xine_t *this) {

  static char *demux_strategies[] = {"default", "reverse", "content",
				     "extension", NULL};

  /* initialize color conversion tables and functions */
  init_yuv_conversion();

  /* probe for optimized memcpy or config setting */
  xine_probe_fast_memcpy (this->config);
  
  /* 
   * plugins
   */
  
  scan_plugins(this);

  /*
   * content detection strategy
   */

  this->demux_strategy  = this->config->register_enum (this->config, 
						       "misc.demux_strategy", 
						       0,
						       demux_strategies, 
						       "media format detection strategy",
						       NULL, 10, NULL, NULL);
  /*
   * keep track of all opened streams 
   */

  this->streams = xine_list_new();
  pthread_mutex_init (&this->streams_lock, NULL);

}

void xine_select_spu_channel (xine_stream_t *stream, int channel) {

  pthread_mutex_lock (&stream->frontend_lock);
  stream->spu_channel_user = (channel >= -2 ? channel : -2);

  switch (stream->spu_channel_user) {
  case -2:
    stream->spu_channel = -1;
    stream->video_out->enable_ovl (stream->video_out, 0);
    break;
  case -1:
    stream->spu_channel = stream->spu_channel_auto;
    stream->video_out->enable_ovl (stream->video_out, 1);
    break;
  default:
    stream->spu_channel = stream->spu_channel_user;
    stream->video_out->enable_ovl (stream->video_out, 1);
  }
  printf("xine-lib:xine_select_spu_channel:set to %d\n",stream->spu_channel);

  pthread_mutex_unlock (&stream->frontend_lock);
}

static int xine_get_current_position (xine_stream_t *stream) {

  off_t len;
  double share;
  
  pthread_mutex_lock (&stream->frontend_lock);

  if (!stream->input_plugin) {
    printf ("xine: xine_get_current_position: no input source\n");
    pthread_mutex_unlock (&stream->frontend_lock);
    return 0;
  }
  
  /* pos = stream->mCurInput->seek (0, SEEK_CUR); */
  len = stream->input_length;
  if (len == 0) len = stream->input_plugin->get_length (stream->input_plugin); 
  share = (double) stream->input_pos / (double) len * 65535;

  pthread_mutex_unlock (&stream->frontend_lock);

  return (int) share;
}

int xine_get_status (xine_stream_t *stream) {
  return stream->status;
}

/*
 * trick play 
 */

void xine_set_speed (xine_stream_t *stream, int speed) {

  pthread_mutex_lock (&stream->frontend_lock);

  if (speed <= XINE_SPEED_PAUSE) 
    speed = XINE_SPEED_PAUSE;
  else if (speed > XINE_SPEED_FAST_4) 
    speed = XINE_SPEED_FAST_4;

  printf ("xine: set_speed %d\n", speed);
  xine_set_speed_internal (stream, speed);

  pthread_mutex_unlock (&stream->frontend_lock);
}


int xine_get_speed (xine_stream_t *stream) {
  return stream->speed;
}

/*
 * time measurement / seek
 */

static int xine_get_stream_length (xine_stream_t *stream) {

  if (stream->demux_plugin)
    return stream->demux_plugin->get_stream_length (stream->demux_plugin);

  return 0;
}

int xine_get_pos_length (xine_stream_t *stream, int *pos_stream, 
			 int *pos_time, int *length_time) {
  
  if (pos_stream)
    *pos_stream  = xine_get_current_position (stream); 
  if (pos_time)
    *pos_time    = stream->input_time * 1000;
  if (length_time)
    *length_time = xine_get_stream_length (stream) * 1000;

  return 1;
}

int xine_get_current_frame (xine_stream_t *stream, int *width, int *height,
			    int *ratio_code, int *format,
			    uint8_t *img) {

  vo_frame_t *frame;

  frame = stream->video_out->get_last_frame (stream->video_out);

  if (!frame)
    return 0;

  *width = frame->width;
  *height = frame->height;

  *ratio_code = frame->ratio;
  *format = frame->format;

  switch (frame->format) {

  case XINE_IMGFMT_YV12:
    memcpy (img, frame->base[0], frame->width*frame->height);
    memcpy (img+frame->width*frame->height, frame->base[1], 
	    frame->width*frame->height/4);
    memcpy (img+frame->width*frame->height+frame->width*frame->height/4, 
	    frame->base[1], 
	    frame->width*frame->height/4);
    break;

  case XINE_IMGFMT_YUY2:
    memcpy (img, frame->base[0], frame->width * frame->height * 2);
    break;

  default:
    printf ("xine: error, snapshot function not implemented for format 0x%x\n",
	    frame->format);
    abort ();
  }

  return 1;
}

int xine_get_spu_lang (xine_stream_t *stream, int channel, char *lang) {

  if (stream->input_plugin) {
    if (stream->input_plugin->get_capabilities (stream->input_plugin) & INPUT_CAP_SPULANG) {
      stream->input_plugin->get_optional_data (stream->input_plugin, lang,
					       INPUT_OPTIONAL_DATA_SPULANG);
      return 1;
    }
  } 

  return 0;
}

int xine_get_audio_lang (xine_stream_t *stream, int channel, char *lang) {

  if (stream->input_plugin) {
    if (stream->input_plugin->get_capabilities (stream->input_plugin) & INPUT_CAP_AUDIOLANG) {
      stream->input_plugin->get_optional_data (stream->input_plugin, lang,
					       INPUT_OPTIONAL_DATA_AUDIOLANG);
      return 1;
    }
  } 

  return 0;
}

int xine_get_spu_channel (xine_stream_t *stream) {
  return stream->spu_channel_user;
}

osd_renderer_t *xine_get_osd_renderer (xine_stream_t *stream) {

  return stream->osd_renderer;
}

/*
 * log functions
 */
int xine_get_log_section_count (xine_t *this) {
  return XINE_LOG_NUM;
}

const char *const *xine_get_log_names (xine_t *this) {
  static const char *log_sections[XINE_LOG_NUM + 1];

  log_sections[XINE_LOG_MSG]      = _("messages");  
  log_sections[XINE_LOG_PLUGIN]   = _("plugin");
  log_sections[XINE_LOG_NUM]      = NULL;
  
  return log_sections;
}

void xine_log (xine_t *this, int buf, const char *format, ...) {

  va_list argp;

  va_start (argp, format);

  this->log_buffers[buf]->scratch_printf (this->log_buffers[buf], format, argp);
  va_end (argp);

  if (this->verbosity) {
    va_start (argp, format);

    vprintf (format, argp);

    va_end (argp);
  }
}

const char *const *xine_get_log (xine_t *this, int buf) {
  
  if(buf >= XINE_LOG_NUM)
    return NULL;
  
  return this->log_buffers[buf]->get_content (this->log_buffers[buf]);
}

void xine_register_log_cb (xine_t *this, xine_log_cb_t cb, void *user_data) {

  printf ("xine: xine_register_log_cb: not implemented yet.\n");
  abort();
}


int xine_get_error (xine_stream_t *stream) {
  return stream->err;
}

int xine_trick_mode (xine_stream_t *stream, int mode, int value) {
  printf ("xine: xine_trick_mode not implemented yet.\n");
  abort ();
}

