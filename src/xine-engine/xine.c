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
 * $Id: xine.c,v 1.221 2003/01/14 00:10:31 miguelfreitas Exp $
 *
 * top-level xine functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
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
  
  /* join thread if needed to fix resource leaks */  
  xine_demux_stop_thread( stream );
    
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

void extra_info_reset( extra_info_t *extra_info ) {
  memset( extra_info, 0, sizeof(extra_info_t) );  
}

void extra_info_merge( extra_info_t *dst, extra_info_t *src ) {
  
  if( src->input_pos )
    dst->input_pos = src->input_pos;
  
  if( src->input_length )
    dst->input_length = src->input_length;
    
  if( src->input_time )
    dst->input_time = src->input_time;
    
  if( src->frame_number )
    dst->frame_number = src->frame_number;
    
  if( src->seek_count )
    dst->seek_count = src->seek_count;

  if( src->vpts )
    dst->vpts = src->vpts;
}

static void xine_set_speed_internal (xine_stream_t *stream, int speed) {

  stream->xine->clock->set_speed (stream->xine->clock, speed);

  /* see coment on audio_out loop about audio_paused */
  if( stream->audio_out ) {
    stream->audio_out->set_property( stream->audio_out, AO_PROP_PAUSED,
      (speed != XINE_SPEED_NORMAL) + (speed == XINE_SPEED_PAUSE) );

    /*
     * slow motion / fast forward does not play sound, drop buffered
     * samples from the sound driver
     */
    if (speed != XINE_SPEED_NORMAL && speed != XINE_SPEED_PAUSE)
      stream->audio_out->control (stream->audio_out, AO_CTRL_FLUSH_BUFFERS);

    stream->audio_out->control(stream->audio_out,
			       speed == XINE_SPEED_PAUSE ? AO_CTRL_PLAY_PAUSE : AO_CTRL_PLAY_RESUME);
  }
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
    
    xine_demux_stop_thread( stream );
    xine_demux_flush_engine( stream );

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

#ifdef LOG
  printf ("xine_stop: done\n");
#endif
}

void xine_stop (xine_stream_t *stream) {

  pthread_mutex_lock (&stream->frontend_lock);

  xine_stop_internal (stream);
  
  /*
   * stream will make output threads discard about everything
   */
  if (stream->audio_out)
    stream->audio_out->flush(stream->audio_out);
    
  if (stream->video_out)
    stream->video_out->flush(stream->video_out);
  
  if (stream->slave && (stream->slave_affection & XINE_MASTER_SLAVE_STOP))
    xine_stop(stream->slave);
  
  pthread_mutex_unlock (&stream->frontend_lock);
}


static void xine_close_internal (xine_stream_t *stream) {

  int i ;

  if( stream->slave ) {
    xine_close( stream->slave );
    if( stream->slave_is_subtitle ) {
      xine_dispose(stream->slave);
      stream->slave = NULL;
      stream->slave_is_subtitle = 0;
    }
  }

  xine_stop_internal( stream );
  
#ifdef LOG
  printf ("xine_close: disposing demux\n");
#endif
  if (stream->demux_plugin) {
    
    stream->demux_plugin->dispose (stream->demux_plugin);
    stream->demux_plugin = NULL;
  }

  /*
   * close input plugin
   */

  if (stream->input_plugin) {
    stream->input_plugin->dispose(stream->input_plugin);
    stream->input_plugin = NULL;
  }

  /*
   * reset / free meta info
   */

  for (i=0; i<XINE_STREAM_INFO_MAX; i++) {
    stream->stream_info[i]       = 0;
    if (stream->meta_info[i])
      free (stream->meta_info[i]);
    stream->meta_info[i]         = NULL;
  }
}

void xine_close (xine_stream_t *stream) {

  pthread_mutex_lock (&stream->frontend_lock);

  xine_close_internal (stream);
  
  pthread_mutex_unlock (&stream->frontend_lock);
}

static int xine_stream_rewire_audio(xine_post_out_t *output, void *data)
{
  xine_stream_t *stream = (xine_stream_t *)output->data;
  xine_audio_port_t *new_port = (xine_audio_port_t *)data;
  uint32_t bits, rate;
  int mode;
  
  if (!data)
    return 0;
    
  if (stream->audio_out && 
      stream->audio_out->status(stream->audio_out, stream, &bits, &rate, &mode)) {
    /* register our stream at the new output port */
    stream->audio_out->close(stream->audio_out, stream);
    new_port->open(new_port, stream, bits, rate, mode);
  }
  /* reconnect ourselves */
  stream->audio_out = new_port;
  return 1;
}

static int xine_stream_rewire_video(xine_post_out_t *output, void *data)
{
  xine_stream_t *stream = (xine_stream_t *)output->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  int width, height;
  int64_t img_duration;

  if (!data)
    return 0;
    
  if (stream->video_out && 
      stream->video_out->status(stream->video_out, stream, 
                                &width, &height, &img_duration)) {
    /* register our stream at the new output port */
    stream->video_out->close(stream->video_out, stream);
    new_port->open(new_port, stream);
  }
  /* reconnect ourselves */
  stream->video_out = new_port;
  return 1;
}


xine_stream_t *xine_stream_new (xine_t *this, 
				xine_audio_port_t *ao, xine_video_port_t *vo) {

  xine_stream_t *stream;
  int            i;

  printf ("xine: xine_stream_new\n");

  /*
   * create a new stream object
   */

  pthread_mutex_lock (&this->streams_lock);
  
  stream = (xine_stream_t *) xine_xmalloc (sizeof (xine_stream_t)) ;
  stream->current_extra_info       = malloc( sizeof( extra_info_t ) );
  stream->audio_decoder_extra_info = malloc( sizeof( extra_info_t ) );
  stream->video_decoder_extra_info = malloc( sizeof( extra_info_t ) );
  extra_info_reset( stream->current_extra_info );
  extra_info_reset( stream->video_decoder_extra_info );
  extra_info_reset( stream->audio_decoder_extra_info );

  stream->xine                   = this;
  stream->status                 = XINE_STATUS_STOP;
  for (i=0; i<XINE_STREAM_INFO_MAX; i++) {
    stream->stream_info[i]       = 0;
    stream->meta_info[i]         = NULL;
  }
  stream->spu_out                = NULL;
  stream->spu_decoder_plugin     = NULL;
  stream->spu_decoder_streamtype = -1;
  stream->audio_out              = ao;
  stream->audio_channel_user     = -1;
  stream->audio_channel_auto     = -1;
  stream->audio_decoder_plugin   = NULL;
  stream->audio_decoder_streamtype = -1;
  stream->spu_channel_auto       = -1;
  stream->spu_channel_letterbox  = -1;
  stream->spu_channel_pan_scan   = -1;
  stream->spu_channel_user       = -1;
  stream->spu_channel            = -1;
  stream->video_out              = vo;
  stream->video_driver           = vo->driver;
  stream->video_channel          = 0;
  stream->video_decoder_plugin   = NULL;
  stream->video_decoder_streamtype = -1;
  stream->header_count_audio     = 0; 
  stream->header_count_video     = 0; 
  stream->finished_count_audio   = 0; 
  stream->finished_count_video   = 0; 
  stream->err                    = 0;
  
  /*
   * initial master/slave
   */
  stream->master                 = stream;
  stream->slave                  = NULL;
  stream->slave_is_subtitle      = 0;
  
  /*
   * init mutexes and conditions
   */

  pthread_mutex_init (&stream->demux_lock, NULL);
  pthread_mutex_init (&stream->frontend_lock, NULL);
  pthread_mutex_init (&stream->event_queues_lock, NULL);
  pthread_mutex_init (&stream->osd_lock, NULL);
  pthread_mutex_init (&stream->counter_lock, NULL);
  pthread_cond_init  (&stream->counter_changed, NULL);
  pthread_mutex_init (&stream->first_frame_lock, NULL);
  pthread_cond_init  (&stream->first_frame_reached, NULL);
  pthread_mutex_init (&stream->current_extra_info_lock, NULL);

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

  video_decoder_init (stream);

  audio_decoder_init (stream);

  /*
   * osd
   */

  stream->osd_renderer = osd_renderer_init (stream->video_out->get_overlay_instance (stream->video_out), stream->xine->config );
  
  /*
   * register stream
   */

  xine_list_append_content (this->streams, stream);

  pthread_mutex_unlock (&this->streams_lock);

  stream->video_source.name   = "video source";
  stream->video_source.type   = XINE_POST_DATA_VIDEO;
  stream->video_source.data   = stream;
  stream->video_source.rewire = xine_stream_rewire_video;
  
  stream->audio_source.name   = "audio source";
  stream->audio_source.type   = XINE_POST_DATA_AUDIO;
  stream->audio_source.data   = stream;
  stream->audio_source.rewire = xine_stream_rewire_audio;
  
  return stream;
}

static int xine_open_internal (xine_stream_t *stream, const char *mrl) {

  char *stream_setup;

#ifdef LOG
  printf ("xine: xine_open_internal '%s'...\n", mrl);
#endif

  /*
   * stop engine if necessary
   */

  xine_close_internal (stream);

#ifdef LOG
  printf ("xine: engine should be stopped now\n");
#endif

  /*
   * look for a stream_setup in MRL
   */
   
  if ((stream_setup = strchr(mrl, '#'))) {
    char *input_source = (char *)malloc(stream_setup - mrl + 1);
    memcpy(input_source, mrl, stream_setup - mrl);
    input_source[stream_setup - mrl] = '\0';
    
    /*
     * find an input plugin
     */

    if (!(stream->input_plugin = find_input_plugin (stream, input_source))) {
      xine_log (stream->xine, XINE_LOG_MSG,
	        _("xine: cannot find input plugin for this MRL\n"));

      stream->err = XINE_ERROR_NO_INPUT_PLUGIN;
      free(input_source);
      return 0;
    }
    if (stream->input_plugin->input_class->eject_media)
      stream->eject_class = stream->input_plugin->input_class;
    stream->meta_info[XINE_META_INFO_INPUT_PLUGIN]
      = strdup (stream->input_plugin->input_class->get_identifier (stream->input_plugin->input_class));
    free(input_source);

#ifdef LOG
    printf ("xine: input plugin %s found\n", 
	    stream->input_plugin->input_class->get_identifier(stream->input_plugin->input_class));
#endif

    while (stream_setup && *stream_setup && *(++stream_setup)) {
      if (strncasecmp(stream_setup, "demux", 5) == 0) {
        if (*(stream_setup += 5) == ':') {
	  /* demuxer specified by name */
	  char *tmp = ++stream_setup;
	  char *demux_name;
	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    demux_name = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(demux_name, tmp, stream_setup - tmp);
	    demux_name[stream_setup - tmp] = '\0';
	  } else {
	    demux_name = (char *)malloc(strlen(tmp));
	    memcpy(demux_name, tmp, strlen(tmp));
	    demux_name[strlen(tmp)] = '\0';
	  }
	  if (!(stream->demux_plugin = find_demux_plugin_by_name(stream, demux_name, stream->input_plugin))) {
	    xine_log(stream->xine, XINE_LOG_MSG,
	      _("xine: specified demuxer %s failed to start\n"), demux_name);
	    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;
	    stream->status = XINE_STATUS_STOP;
	    free(demux_name);
	    return 0;
	  }
#ifdef LOG
	  printf ("xine: demux and input plugin found\n");
#endif

	  stream->meta_info[XINE_META_INFO_SYSTEMLAYER]
	   = strdup (stream->demux_plugin->demux_class->get_identifier(stream->demux_plugin->demux_class));
	  free(demux_name);
	} else {
	  printf("xine: error while parsing mrl\n");
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      if (strncasecmp(stream_setup, "novideo", 7) == 0) {
        stream_setup += 7;
        if (*stream_setup == ';' || *stream_setup == '\0') {
	  stream->stream_info[XINE_STREAM_INFO_IGNORE_VIDEO] = 1;
	} else {
	  printf("xine: error while parsing mrl\n");
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	printf("xine: ignoring video\n");
	continue;
      }
      if (strncasecmp(stream_setup, "noaudio", 7) == 0) {
        stream_setup += 7;
        if (*stream_setup == ';' || *stream_setup == '\0') {
	  stream->stream_info[XINE_STREAM_INFO_IGNORE_AUDIO] = 1;
	} else {
	  printf("xine: error while parsing mrl\n");
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	printf("xine: ignoring audio\n");
	continue;
      }
      if (strncasecmp(stream_setup, "nospu", 5) == 0) {
        stream_setup += 5;
        if (*stream_setup == ';' || *stream_setup == '\0') {
	  stream->stream_info[XINE_STREAM_INFO_IGNORE_SPU] = 1;
	} else {
	  printf("xine: error while parsing mrl\n");
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	printf("xine: ignoring subpicture\n");
	continue;
      }
      if (strncasecmp(stream_setup, "volume", 6) == 0) {
        if (*(stream_setup += 6) == ':') {
	  char *tmp = ++stream_setup;
	  char *volume;
	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    volume = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(volume, tmp, stream_setup - tmp);
	    volume[stream_setup - tmp] = '\0';
	  } else {
	    volume = (char *)malloc(strlen(tmp));
	    memcpy(volume, tmp, strlen(tmp));
	    volume[strlen(tmp)] = '\0';
	  }
	  xine_set_param(stream, XINE_PARAM_AUDIO_VOLUME, atoi(volume));
	  free(volume);
	} else {
	  printf("xine: error while parsing mrl\n");
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      if (strncasecmp(stream_setup, "compression", 11) == 0) {
        if (*(stream_setup += 11) == ':') {
	  char *tmp = ++stream_setup;
	  char *compression;
	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    compression = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(compression, tmp, stream_setup - tmp);
	    compression[stream_setup - tmp] = '\0';
	  } else {
	    compression = (char *)malloc(strlen(tmp));
	    memcpy(compression, tmp, strlen(tmp));
	    compression[strlen(tmp)] = '\0';
	  }
	  xine_set_param(stream, XINE_PARAM_AUDIO_COMPR_LEVEL, atoi(compression));
	  free(compression);
	} else {
	  printf("xine: error while parsing mrl\n");
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      if (strncasecmp(stream_setup, "subtitle", 8) == 0) {
        if (*(stream_setup += 8) == ':') {
	  char *tmp = ++stream_setup;
	  char *subtitle_mrl;
	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    subtitle_mrl = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(subtitle_mrl, tmp, stream_setup - tmp);
	    subtitle_mrl[stream_setup - tmp] = '\0';
	  } else {
	    subtitle_mrl = (char *)malloc(strlen(tmp));
	    memcpy(subtitle_mrl, tmp, strlen(tmp));
	    subtitle_mrl[strlen(tmp)] = '\0';
	  }
	  stream->slave = xine_stream_new (stream->xine, NULL, stream->video_out );
	  stream->slave_affection = XINE_MASTER_SLAVE_PLAY | XINE_MASTER_SLAVE_STOP;
	  if( xine_open( stream->slave, subtitle_mrl ) ) {
	    printf("xine: subtitle mrl opened\n");
	    stream->slave->master = stream;
	    stream->slave_is_subtitle = 1; 
	  } else {
	    printf("xine: error opening subtitle mrl\n");
	    xine_dispose( stream->slave );
	    stream->slave = NULL;
	  }
	  free(subtitle_mrl);
	} else {
	  printf("xine: error while parsing mrl\n");
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      {
        /* when we got here, the stream setup parameter must be a config entry */
	char *tmp = stream_setup;
	if ((stream_setup = strchr(stream_setup, ';'))) {
	  char *config_entry = (char *)malloc(stream_setup - tmp + 1);
	  memcpy(config_entry, tmp, stream_setup - tmp);
	  config_entry[stream_setup - tmp] = '\0';
	  xine_config_change_opt(stream->xine->config, config_entry);
	  free(config_entry);
	} else {
	  xine_config_change_opt(stream->xine->config, tmp);
	}
      }
    }
    
  } else {
  
    /* no stream setup found in MRL, continue as ususal */
  
    /*
     * find an input plugin
     */

    if (!(stream->input_plugin = find_input_plugin (stream, mrl))) {
      xine_log (stream->xine, XINE_LOG_MSG,
	        _("xine: cannot find input plugin for this MRL\n"));

      stream->err = XINE_ERROR_NO_INPUT_PLUGIN;
      return 0;
    }
    if (stream->input_plugin->input_class->eject_media)
      stream->eject_class = stream->input_plugin->input_class;
    stream->meta_info[XINE_META_INFO_INPUT_PLUGIN]
      = strdup (stream->input_plugin->input_class->get_identifier (stream->input_plugin->input_class));

#ifdef LOG
    printf ("xine: input plugin %s found\n", 
	    stream->input_plugin->input_class->get_identifier(stream->input_plugin->input_class));
#endif
  }


  if (!stream->demux_plugin) {
  
    /*
     * find a demux plugin
     */
    if (!(stream->demux_plugin=find_demux_plugin (stream, stream->input_plugin))) {
      xine_log (stream->xine, XINE_LOG_MSG,
	        _("xine: couldn't find demux for >%s<\n"), mrl);
      stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;

      stream->status = XINE_STATUS_STOP;
      return 0;
    }

#ifdef LOG
    printf ("xine: demux and input plugin found\n");
#endif

    stream->meta_info[XINE_META_INFO_SYSTEMLAYER]
      = strdup (stream->demux_plugin->demux_class->get_identifier(stream->demux_plugin->demux_class));
  }

  extra_info_reset( stream->current_extra_info );
  extra_info_reset( stream->video_decoder_extra_info );
  extra_info_reset( stream->audio_decoder_extra_info );

  /* assume handled for now. we will only know for sure after trying
   * to init decoders (which should happen when headers are sent)
   */
  stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 1;
  stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED] = 1;
  
  /*
   * send and decode headers
   */

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
    /* why? */
    /*if (stream->audio_out)
      stream->audio_out->control (stream->audio_out, AO_CTRL_FLUSH_BUFFERS);
    */
    
    stream->status = XINE_STATUS_STOP;

    printf ("xine: return from xine_open_internal\n");

    return 0;
  }
  
  xine_demux_control_headers_done (stream);

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

  if (stream->xine->clock->speed != XINE_SPEED_NORMAL) 
    xine_set_speed_internal (stream, XINE_SPEED_NORMAL);

  /* Wait until the first frame produced by the previous
   * xine_play call is pushed into the video_out fifo
   * see video_out.c
   */
  pthread_mutex_lock (&stream->first_frame_lock);
  /* FIXME: howto detect if video frames will be produced */
  if (stream->first_frame_flag && stream->video_decoder_plugin) {
    struct timeval  tv;
    struct timespec ts;
    gettimeofday(&tv, NULL);
    ts.tv_sec  = tv.tv_sec + 2;
    ts.tv_nsec = tv.tv_usec * 1000;
    pthread_cond_timedwait(&stream->first_frame_reached, &stream->first_frame_lock, &ts);
  }
  pthread_mutex_unlock (&stream->first_frame_lock);

  /*
   * start/seek demux
   */
  if (start_pos) {
    pthread_mutex_lock( &stream->current_extra_info_lock );
    len = stream->current_extra_info->input_length;
    pthread_mutex_unlock( &stream->current_extra_info_lock );
    /* FIXME: do we need to protect concurrent access to input plugin here? */
    if (len == 0) len = stream->input_plugin->get_length (stream->input_plugin);
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
  
  stream->demux_action_pending = 1;
  pthread_mutex_lock( &stream->demux_lock );
  demux_status = stream->demux_plugin->seek (stream->demux_plugin,
						   pos, start_time);
  stream->demux_action_pending = 0;
  pthread_mutex_unlock( &stream->demux_lock );

  if (demux_status != DEMUX_OK) {
    xine_log (stream->xine, XINE_LOG_MSG, 
	      _("xine_play: demux failed to start\n"));
    
    stream->err = XINE_ERROR_DEMUX_FAILED;
    
    return 0;
    
  } else {
    xine_demux_start_thread( stream );
    stream->status = XINE_STATUS_PLAY;
  }
  
  pthread_mutex_lock (&stream->first_frame_lock);
  stream->first_frame_flag = 1;
  pthread_mutex_unlock (&stream->first_frame_lock);
  pthread_mutex_lock( &stream->current_extra_info_lock );
  extra_info_reset( stream->current_extra_info );
  pthread_mutex_unlock( &stream->current_extra_info_lock );

  printf ("xine: xine_play_internal ...done\n");

  return 1;
}             

int xine_play (xine_stream_t *stream, int start_pos, int start_time) {

  int ret;

  pthread_mutex_lock (&stream->frontend_lock);

  ret = xine_play_internal (stream, start_pos, start_time/1000);
  if( stream->slave && (stream->slave_affection & XINE_MASTER_SLAVE_PLAY) )
    xine_play (stream->slave, start_pos, start_time/1000);

  pthread_mutex_unlock (&stream->frontend_lock);
  
  return ret;
}

int xine_eject (xine_stream_t *stream) {
  
  int status;

  if (!stream->eject_class) 
    return 0;
  
  pthread_mutex_lock (&stream->frontend_lock);

  status = 0;
  if ((stream->status == XINE_STATUS_STOP)
      && stream->eject_class && stream->eject_class->eject_media) {

    status = stream->eject_class->eject_media (stream->eject_class);
  }

  pthread_mutex_unlock (&stream->frontend_lock);
  return status;
}

void xine_dispose (xine_stream_t *stream) {

  printf ("xine: xine_dispose\n");

  stream->status = XINE_STATUS_QUIT;

  xine_close(stream);
  
  if( stream->master != stream ) {
    stream->master->slave = NULL;  
  }
  if( stream->slave && stream->slave->master == stream ) {
    stream->slave->master = NULL;
  }

  printf ("xine_exit: shutdown audio\n");

  audio_decoder_shutdown (stream);

  printf ("xine_exit: shutdown video\n");

  video_decoder_shutdown (stream);

  stream->osd_renderer->close( stream->osd_renderer );
  stream->video_fifo->dispose (stream->video_fifo);

  pthread_mutex_destroy (&stream->frontend_lock);
  pthread_mutex_destroy (&stream->counter_lock);
  pthread_mutex_destroy (&stream->osd_lock);
  pthread_mutex_destroy (&stream->event_queues_lock);
  pthread_mutex_destroy (&stream->current_extra_info_lock);
  pthread_cond_destroy (&stream->counter_changed);

  stream->metronom->exit (stream->metronom);

  free (stream->current_extra_info);
  free (stream->video_decoder_extra_info);
  free (stream->audio_decoder_extra_info);
  free (stream);
}

void xine_exit (xine_t *this) {

  int i;

  printf ("xine_exit: bye!\n");

  for (i = 0; i < XINE_LOG_NUM; i++)
    this->log_buffers[i]->dispose (this->log_buffers[i]);

  dispose_plugins (this);
  this->clock->exit (this->clock);
  this->config->dispose(this->config);

  free (this);
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
  
  /*
   * start metronom clock
   */

  this->clock = metronom_clock_init();

  this->clock->start_clock (this->clock, 0);

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
#ifdef LOG
  printf("xine-lib:xine_select_spu_channel:set to %d\n",stream->spu_channel);
#endif

  pthread_mutex_unlock (&stream->frontend_lock);
}

static int xine_get_current_position (xine_stream_t *stream) {

  off_t len;
  double share;
    
  pthread_mutex_lock (&stream->frontend_lock);

  if (!stream->input_plugin) {
    printf ("xine: xine_get_current_position: no input source\n");
    pthread_mutex_unlock (&stream->frontend_lock);
    return -1;
  }

  if ( (!stream->video_decoder_plugin && !stream->audio_decoder_plugin) ) {
    if( stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] )
      extra_info_merge( stream->current_extra_info, stream->video_decoder_extra_info );
    else    
      extra_info_merge( stream->current_extra_info, stream->audio_decoder_extra_info );
  }
  
  if ( stream->current_extra_info->seek_count != stream->video_seek_count ) {
    pthread_mutex_unlock (&stream->frontend_lock);
    return -1; /* position not yet known */  
  }
  
  pthread_mutex_lock( &stream->current_extra_info_lock );
  len = stream->current_extra_info->input_length;
  share = (double) stream->current_extra_info->input_pos;
  pthread_mutex_unlock( &stream->current_extra_info_lock );

  if (len == 0) len = stream->input_plugin->get_length (stream->input_plugin);
  share /= (double) len;
  share *= 65536;

  pthread_mutex_unlock (&stream->frontend_lock);

  return (int) share;
}

void xine_get_current_info (xine_stream_t *stream, extra_info_t *extra_info, int size) {
  
  pthread_mutex_lock( &stream->current_extra_info_lock );
  memcpy( extra_info, stream->current_extra_info, size );  
  pthread_mutex_unlock( &stream->current_extra_info_lock );
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
  return stream->xine->clock->speed;
}

/*
 * time measurement / seek
 */

static int xine_get_stream_length (xine_stream_t *stream) {

  /* pthread_mutex_lock( &stream->demux_lock ); */

  if (stream->demux_plugin) {
    int len = stream->demux_plugin->get_stream_length (stream->demux_plugin); 
    /* pthread_mutex_unlock( &stream->demux_lock ); */

    return len;
  }
  
  /* pthread_mutex_unlock( &stream->demux_lock ); */

  return 0;
}

int xine_get_pos_length (xine_stream_t *stream, int *pos_stream, 
			 int *pos_time, int *length_time) {
  
  int pos = xine_get_current_position (stream); /* force updating extra_info */
  
  if (pos == -1)
    return 0;
  
  if (pos_stream)
    *pos_stream  = pos; 
  if (pos_time) {
    pthread_mutex_lock( &stream->current_extra_info_lock );
    *pos_time    = stream->current_extra_info->input_time;
    pthread_mutex_unlock( &stream->current_extra_info_lock );
  }
  if (length_time)
    *length_time = xine_get_stream_length (stream);

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
	    frame->base[2], 
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

int xine_get_video_frame (xine_stream_t *stream,
			  int timestamp, /* msec */
			  int *width, int *height,
			  int *ratio_code, 
			  int *duration, /* msec */
			  int *format,
			  uint8_t *img) {
  int ret;

  pthread_mutex_lock (&stream->frontend_lock);

  if (stream->status != XINE_STATUS_STOP) 
    xine_stop_internal (stream);

  if (stream->demux_plugin->get_video_frame)
    ret = stream->demux_plugin->get_video_frame (stream->demux_plugin,
						 timestamp, width, height,
						 ratio_code, duration,
						 format, img);
  else
    ret = 0;
  
  pthread_mutex_unlock (&stream->frontend_lock);

  return ret;
}

int xine_get_spu_lang (xine_stream_t *stream, int channel, char *lang) {

  /* Ask the demuxer first (e.g. TS extracts this information from
   * the stream)
   **/
  if (stream->demux_plugin) {
    if (stream->demux_plugin->get_capabilities (stream->demux_plugin) & DEMUX_CAP_SPULANG) {
      stream->demux_plugin->get_optional_data (stream->demux_plugin, lang,
					       DEMUX_OPTIONAL_DATA_SPULANG);
      return 1;
    }
  } 

  /* No match, check with input plugin instead (e.g. DVD gets this
   * info from the IFO).
   **/
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

  if (stream->demux_plugin) {
    if (stream->demux_plugin->get_capabilities (stream->demux_plugin) & DEMUX_CAP_AUDIOLANG) {
      stream->demux_plugin->get_optional_data (stream->demux_plugin, lang,
					       DEMUX_OPTIONAL_DATA_AUDIOLANG);
      return 1;
    }
  }
  
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

int xine_stream_master_slave(xine_stream_t *master, xine_stream_t *slave,
                         int affection) {
  master->slave = slave;
  master->slave_affection = affection;
  /* respect transitivity: if our designated master already has a master
   * of its own, we point to this master's master; if our master is a 
   * standalone stream, its master pointer will point to itself */
  slave->master = master->master;
  return 1;
}                           

