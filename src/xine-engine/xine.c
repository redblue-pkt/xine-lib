/*
 * Copyright (C) 2000-2004 the xine project
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
 * $Id: xine.c,v 1.294 2004/06/02 19:46:10 tmattern Exp $
 */

/*
 * top-level xine functions
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

#ifdef HAVE_SETLOCALE
#include <locale.h>
#endif

#define LOG_MODULE "xine"
#define LOG_VERBOSE
/*
#define LOG
*/

#define XINE_ENABLE_EXPERIMENTAL_FEATURES
#define XINE_ENGINE_INTERNAL
#define METRONOM_CLOCK_INTERNAL

#include "xine_internal.h"
#include "plugin_catalog.h"
#include "audio_out.h"
#include "video_out.h"
#include "demuxers/demux.h"
#include "buffer.h"
#include "spu_decoder.h"
#include "input/input_plugin.h"
#include "metronom.h"
#include "configfile.h"
#include "osd.h"

#include "xineutils.h"
#include "compat.h"

#ifdef WIN32
#   include <fcntl.h>
#   include <winsock.h>
#endif /* WIN32 */


void _x_handle_stream_end (xine_stream_t *stream, int non_user) {

  if (stream->status == XINE_STATUS_QUIT)
    return;
  stream->status = XINE_STATUS_STOP;

  /* join thread if needed to fix resource leaks */
  _x_demux_stop_thread( stream );

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

void _x_extra_info_reset( extra_info_t *extra_info ) {
  memset( extra_info, 0, sizeof(extra_info_t) );
}

void _x_extra_info_merge( extra_info_t *dst, extra_info_t *src ) {

  if (!src->invalid) {
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
}

static void ticket_acquire(xine_ticket_t *this, int irrevocable) {

  pthread_mutex_lock(&this->lock);
  
  if (this->ticket_revoked && !this->irrevocable_tickets)
    pthread_cond_wait(&this->issued, &this->lock);
  else if (this->atomic_revoke && !pthread_equal(this->atomic_revoker_thread, pthread_self()))
    pthread_cond_wait(&this->issued, &this->lock);
  
  this->tickets_granted++;
  if (irrevocable)
    this->irrevocable_tickets++;
  
  pthread_mutex_unlock(&this->lock);  
}

static void ticket_release(xine_ticket_t *this, int irrevocable) {

  pthread_mutex_lock(&this->lock);
  
  this->tickets_granted--;
  if (irrevocable)
    this->irrevocable_tickets--;
  
  if (this->ticket_revoked && !this->tickets_granted)
    pthread_cond_broadcast(&this->revoked);
  if (this->ticket_revoked && !this->irrevocable_tickets)
    pthread_cond_wait(&this->issued, &this->lock);
  
  pthread_mutex_unlock(&this->lock);
}

static void ticket_renew(xine_ticket_t *this, int irrevocable) {

  pthread_mutex_lock(&this->lock);
  
  this->tickets_granted--;
  
  _x_assert(this->ticket_revoked);
  if (!this->tickets_granted)
    pthread_cond_broadcast(&this->revoked);
  if (!this->irrevocable_tickets || !irrevocable)
    pthread_cond_wait(&this->issued, &this->lock);
  
  this->tickets_granted++;
  
  pthread_mutex_unlock(&this->lock);
}

static void ticket_issue(xine_ticket_t *this, int atomic) {

  if (!atomic)
    pthread_mutex_lock(&this->revoke_lock);
  pthread_mutex_lock(&this->lock);
  
  this->pending_revocations--;
  if (!this->pending_revocations)
    pthread_cond_broadcast(&this->issued);
  this->atomic_revoke = 0;
  
  pthread_mutex_unlock(&this->lock);
  pthread_mutex_unlock(&this->revoke_lock);
}

static void ticket_revoke(xine_ticket_t *this, int atomic) {

  pthread_mutex_lock(&this->revoke_lock);
  pthread_mutex_lock(&this->lock);
  
  this->pending_revocations++;
  this->ticket_revoked = 1;
  if (this->tickets_granted)
    pthread_cond_wait(&this->revoked, &this->lock);
  _x_assert(!this->tickets_granted);
  this->ticket_revoked = 0;
  if (atomic) {
    this->atomic_revoke = 1;
    this->atomic_revoker_thread = pthread_self();
  }
  
  pthread_mutex_unlock(&this->lock);
  if (!atomic)
    pthread_mutex_unlock(&this->revoke_lock);
}

static void ticket_dispose(xine_ticket_t *this) {

  pthread_mutex_destroy(&this->lock);
  pthread_mutex_destroy(&this->revoke_lock);
  pthread_cond_destroy(&this->issued);
  pthread_cond_destroy(&this->revoked);
  
  free(this);
}

static xine_ticket_t *ticket_init(void) {
  xine_ticket_t *port_ticket;
  
  port_ticket = (xine_ticket_t *) xine_xmalloc(sizeof(xine_ticket_t));
  
  port_ticket->acquire = ticket_acquire;
  port_ticket->release = ticket_release;
  port_ticket->renew   = ticket_renew;
  port_ticket->issue   = ticket_issue;
  port_ticket->revoke  = ticket_revoke;
  port_ticket->dispose = ticket_dispose;
  
  pthread_mutex_init(&port_ticket->lock, NULL);
  pthread_mutex_init(&port_ticket->revoke_lock, NULL);
  pthread_cond_init(&port_ticket->issued, NULL);
  
  return port_ticket;
}

static void __set_speed_internal (xine_stream_t *stream, int speed) {
  xine_t *xine = stream->xine;

  if (xine->clock->speed != XINE_SPEED_PAUSE && speed == XINE_SPEED_PAUSE)
    /* get all decoder and post threads in a state where they agree to be blocked */
    xine->port_ticket->revoke(xine->port_ticket, 0);
  
  if (xine->clock->speed == XINE_SPEED_PAUSE && speed != XINE_SPEED_PAUSE)
    /* all decoder and post threads may continue now */
    xine->port_ticket->issue(xine->port_ticket, 0);
  
  stream->xine->clock->set_speed (stream->xine->clock, speed);

  /* see coment on audio_out loop about audio_paused */
  if( stream->audio_out ) {
    xine->port_ticket->acquire(xine->port_ticket, 1);
    
    /*
     * slow motion / fast forward does not play sound, drop buffered
     * samples from the sound driver
     */
    if (speed != XINE_SPEED_NORMAL && speed != XINE_SPEED_PAUSE)
      stream->audio_out->control (stream->audio_out, AO_CTRL_FLUSH_BUFFERS, NULL);

    stream->audio_out->control(stream->audio_out,
			       speed == XINE_SPEED_PAUSE ? AO_CTRL_PLAY_PAUSE : AO_CTRL_PLAY_RESUME, NULL);
    
    xine->port_ticket->release(xine->port_ticket, 1);
  }
}


/* stream->ignore_speed_change must be set, when entering this function */
static void __stop_internal (xine_stream_t *stream) {

  int finished_count_audio = 0;
  int finished_count_video = 0;

  lprintf ("status before = %d\n", stream->status);

  if (stream->status == XINE_STATUS_STOP) {
    lprintf ("ignored\n");
    return;
  }

  /* make sure we're not in "paused" state */
  __set_speed_internal (stream, XINE_SPEED_NORMAL);

  /* Don't change status if we're quitting */
  if (stream->status != XINE_STATUS_QUIT)
    stream->status = XINE_STATUS_STOP;

  /*
   * stop demux
   */

  pthread_mutex_lock (&stream->counter_lock);
  if (stream->audio_thread)
    finished_count_audio = stream->finished_count_audio + 1;
  else
    finished_count_audio = 0;

  if (stream->video_thread)
    finished_count_video = stream->finished_count_video + 1;
  else
    finished_count_video = 0;
    
  pthread_mutex_unlock (&stream->counter_lock);

  lprintf ("stopping demux\n");
  if (stream->demux_plugin) {
    
    _x_demux_stop_thread( stream );
    lprintf ("stop thread done\n");
  
    _x_demux_flush_engine( stream );
    lprintf ("flush engine done\n");

    /*
     * wait until engine has really stopped
     */

#if 0
    pthread_mutex_lock (&stream->counter_lock);
    while ((stream->finished_count_audio<finished_count_audio) || 
           (stream->finished_count_video<finished_count_video)) {
      
      lprintf ("waiting for finisheds.\n");
      pthread_cond_wait (&stream->counter_changed, &stream->counter_lock);
    }
    pthread_mutex_unlock (&stream->counter_lock);
#endif
  }
  lprintf ("demux stopped\n");
  lprintf ("done\n");
}

void xine_stop (xine_stream_t *stream) {

  pthread_mutex_lock (&stream->frontend_lock);

  stream->ignore_speed_change = 1;
  stream->xine->port_ticket->acquire(stream->xine->port_ticket, 1);

  if (stream->audio_out)
    stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 1);
  if (stream->video_out)
    stream->video_out->set_property(stream->video_out, VO_PROP_DISCARD_FRAMES, 1);

  __stop_internal (stream);
  
  if (stream->slave && (stream->slave_affection & XINE_MASTER_SLAVE_STOP))
    xine_stop(stream->slave);

  if (stream->video_out)
    stream->video_out->set_property(stream->video_out, VO_PROP_DISCARD_FRAMES, 0);  
  if (stream->audio_out)
    stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 0);
  
  stream->xine->port_ticket->release(stream->xine->port_ticket, 1);
  stream->ignore_speed_change = 0;
  
  pthread_mutex_unlock (&stream->frontend_lock);
}


static void __close_internal (xine_stream_t *stream) {

  int i ;

  if( stream->slave ) {
    xine_close( stream->slave );
    if( stream->slave_is_subtitle ) {
      xine_dispose(stream->slave);
      stream->slave = NULL;
      stream->slave_is_subtitle = 0;
    }
  }

  stream->ignore_speed_change = 1;
  __stop_internal( stream );
  stream->ignore_speed_change = 0;
  
  lprintf ("disposing demux\n");
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
    _x_stream_info_reset(stream, i);
    _x_stream_info_public_reset(stream, i);
    _x_meta_info_reset(stream, i);
    _x_meta_info_public_reset(stream, i);
  }
}

void xine_close (xine_stream_t *stream) {

  pthread_mutex_lock (&stream->frontend_lock);

  __close_internal (stream);

  pthread_mutex_unlock (&stream->frontend_lock);
}

static int __stream_rewire_audio(xine_post_out_t *output, void *data)
{
  xine_stream_t *stream = (xine_stream_t *)output->data;
  xine_audio_port_t *new_port = (xine_audio_port_t *)data;
  uint32_t bits, rate;
  int mode;

  if (!data)
    return 0;

  stream->xine->port_ticket->revoke(stream->xine->port_ticket, 1);
  
  if (stream->audio_out->status(stream->audio_out, stream, &bits, &rate, &mode)) {
    /* register our stream at the new output port */
    new_port->open(new_port, stream, bits, rate, mode);
    stream->audio_out->close(stream->audio_out, stream);
  }
  stream->audio_out = new_port;
  
  stream->xine->port_ticket->issue(stream->xine->port_ticket, 1);

  return 1;
}

static int __stream_rewire_video(xine_post_out_t *output, void *data)
{
  xine_stream_t *stream = (xine_stream_t *)output->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  int64_t img_duration;
  int width, height;
  
  if (!data)
    return 0;

  stream->xine->port_ticket->revoke(stream->xine->port_ticket, 1);
  
  if (stream->video_out->status(stream->video_out, stream, &width, &height, &img_duration)) {
    /* register our stream at the new output port */
    new_port->open(new_port, stream);
    stream->video_out->close(stream->video_out, stream);
  }
  stream->video_out = new_port;
  
  stream->xine->port_ticket->issue(stream->xine->port_ticket, 1);

  return 1;
}


xine_stream_t *xine_stream_new (xine_t *this,
				xine_audio_port_t *ao, xine_video_port_t *vo) {

  xine_stream_t *stream;
  int            i;

  xprintf (this, XINE_VERBOSITY_DEBUG, "xine_stream_new\n");

  /*
   * create a new stream object
   */

  pthread_mutex_lock (&this->streams_lock);

  stream = (xine_stream_t *) xine_xmalloc (sizeof (xine_stream_t)) ;
  stream->current_extra_info       = malloc( sizeof( extra_info_t ) );
  stream->audio_decoder_extra_info = malloc( sizeof( extra_info_t ) );
  stream->video_decoder_extra_info = malloc( sizeof( extra_info_t ) );
  _x_extra_info_reset( stream->current_extra_info );
  _x_extra_info_reset( stream->video_decoder_extra_info );
  _x_extra_info_reset( stream->audio_decoder_extra_info );

  stream->xine                   = this;
  stream->status                 = XINE_STATUS_STOP;

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
  if (vo)
    stream->video_driver           = vo->driver;
  else
    stream->video_driver           = NULL;
    
  stream->video_channel          = 0;
  stream->video_decoder_plugin   = NULL;
  stream->video_decoder_streamtype = -1;
  stream->header_count_audio     = 0; 
  stream->header_count_video     = 0; 
  stream->finished_count_audio   = 0; 
  stream->finished_count_video   = 0;
  stream->err                    = 0;
  stream->broadcaster            = NULL;
  
  /*
   * initial master/slave
   */
  stream->master                 = stream;
  stream->slave                  = NULL;
  stream->slave_is_subtitle      = 0;
  
  /*
   * init mutexes and conditions
   */


  pthread_mutex_init (&stream->info_mutex, NULL);
  pthread_mutex_init (&stream->meta_mutex, NULL);
  pthread_mutex_init (&stream->demux_lock, NULL);
  pthread_mutex_init (&stream->frontend_lock, NULL);
  pthread_mutex_init (&stream->event_queues_lock, NULL);
  pthread_mutex_init (&stream->counter_lock, NULL);
  pthread_cond_init  (&stream->counter_changed, NULL);
  pthread_mutex_init (&stream->first_frame_lock, NULL);
  pthread_cond_init  (&stream->first_frame_reached, NULL);
  pthread_mutex_init (&stream->current_extra_info_lock, NULL);

  /*
   * Clear meta/stream info
   */
  for (i = 0; i < XINE_STREAM_INFO_MAX; i++) {
    _x_stream_info_reset(stream, i);
    _x_stream_info_public_reset(stream, i);
    _x_meta_info_reset(stream, i);
    _x_meta_info_public_reset(stream, i);
  }
  
  /*
   * event queues
   */

  stream->event_queues = xine_list_new ();

  /*
   * create a metronom
   */

  stream->metronom = _x_metronom_init ( (vo != NULL), (ao != NULL), this);

  /*
   * alloc fifos, init and start decoder threads
   */

  _x_video_decoder_init (stream);

  _x_audio_decoder_init (stream);

  /*
   * osd
   */
  if (vo)
    stream->osd_renderer = _x_osd_renderer_init(stream);
  else
    stream->osd_renderer = NULL;
  
  /*
   * register stream
   */

  xine_list_append_content (this->streams, stream);

  pthread_mutex_unlock (&this->streams_lock);

  stream->video_source.name   = "video source";
  stream->video_source.type   = XINE_POST_DATA_VIDEO;
  stream->video_source.data   = stream;
  stream->video_source.rewire = __stream_rewire_video;
  
  stream->audio_source.name   = "audio source";
  stream->audio_source.type   = XINE_POST_DATA_AUDIO;
  stream->audio_source.data   = stream;
  stream->audio_source.rewire = __stream_rewire_audio;
  
  return stream;
}

static void __mrl_unescape(char *mrl) {
  int i, len = strlen(mrl);

  for (i = 0; i < len; i++) {
    if ((mrl[i]=='%') && (i<(len-2))) {
      int c;
      
      if (sscanf(&mrl[i + 1], "%02x", &c) == 1) {
	mrl[i]= (char)c;
	memmove(mrl + i + 1, mrl + i + 3, len - i - 3);
	len -= 2;
      }
    }
  }
  mrl[len] = 0;
}

void _x_flush_events_queues (xine_stream_t *stream) {

  xine_event_queue_t *queue;

  pthread_mutex_lock (&stream->event_queues_lock);

  /* No events queue? */
  for (queue = xine_list_first_content (stream->event_queues);
       queue; queue = xine_list_next_content (stream->event_queues)) {
    pthread_mutex_lock (&queue->lock);
    pthread_mutex_unlock (&stream->event_queues_lock);

    while (!xine_list_is_empty (queue->events)) {
      pthread_cond_wait (&queue->events_processed, &queue->lock);
    }

    pthread_mutex_unlock (&queue->lock);
    pthread_mutex_lock (&stream->event_queues_lock);
  }

  pthread_mutex_unlock (&stream->event_queues_lock);
}

static int __open_internal (xine_stream_t *stream, const char *mrl) {

  const char *stream_setup;

  if (!mrl) {
    xprintf (stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
    stream->err = XINE_ERROR_MALFORMED_MRL;
    stream->status = XINE_STATUS_STOP;
    return 0;
  }

  lprintf ("opening MRL '%s'...\n", mrl);

  /*
   * stop engine if necessary
   */

  __close_internal (stream);

  lprintf ("engine should be stopped now\n");

  /*
   * look for a stream_setup in MRL and try finding an input plugin
   */

  stream_setup = mrl;
  /* look for the next '#' or try the whole MRL, if none is found */
  while (*stream_setup &&
	(stream_setup = (strchr(stream_setup, '#') ? strchr(stream_setup, '#') : strlen(mrl) + mrl))) {
    char *input_source = (char *)malloc(stream_setup - mrl + 1);
    memcpy(input_source, mrl, stream_setup - mrl);
    input_source[stream_setup - mrl] = '\0';

    /*
     * find an input plugin
     */

    if ((stream->input_plugin = _x_find_input_plugin (stream, input_source))) {
      xine_log (stream->xine, XINE_LOG_MSG, _("xine: found input plugin  : %s\n"),
		stream->input_plugin->input_class->get_description(stream->input_plugin->input_class));
      if (stream->input_plugin->input_class->eject_media)
        stream->eject_class = stream->input_plugin->input_class;
      _x_meta_info_set(stream, XINE_META_INFO_INPUT_PLUGIN, 
		       (stream->input_plugin->input_class->get_identifier (stream->input_plugin->input_class)));

      if (!stream->input_plugin->open(stream->input_plugin)) {
	xine_log (stream->xine, XINE_LOG_MSG, _("xine: input plugin cannot open MRL [%s]\n"),mrl);
	stream->input_plugin->dispose(stream->input_plugin);
	stream->input_plugin = NULL;
	stream->err = XINE_ERROR_INPUT_FAILED;
      } else {
        free(input_source);
        break;
      }
    }

    free(input_source);
    /* if we fail when passing up to the first '#' to the input plugins,
     * maybe the user stated a (invalid) MRL, with a '#' belonging to the
     * input source -> look for the next '#' and try again */
    if (*stream_setup) stream_setup++;
  }
  
  if (!stream->input_plugin) {
    xine_log (stream->xine, XINE_LOG_MSG, _("xine: cannot find input plugin for MRL [%s]\n"),mrl);
    stream->err = XINE_ERROR_NO_INPUT_PLUGIN;
    _x_flush_events_queues (stream);
    return 0;
  }

  if (*stream_setup) {

    while (stream_setup && *stream_setup && *(++stream_setup)) {
      if (strncasecmp(stream_setup, "demux", 5) == 0) {
        if (*(stream_setup += 5) == ':') {
	  /* demuxer specified by name */
	  const char *tmp = ++stream_setup;
	  char *demux_name;
	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    demux_name = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(demux_name, tmp, stream_setup - tmp);
	    demux_name[stream_setup - tmp] = '\0';
	  } else {
	    demux_name = (char *)malloc(strlen(tmp) + 1);
	    memcpy(demux_name, tmp, strlen(tmp));
	    demux_name[strlen(tmp)] = '\0';
	  }
	  __mrl_unescape(demux_name);
	  if (!(stream->demux_plugin = _x_find_demux_plugin_by_name(stream, demux_name, stream->input_plugin))) {
	    xine_log(stream->xine, XINE_LOG_MSG, _("xine: specified demuxer %s failed to start\n"), demux_name);
	    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;
	    stream->status = XINE_STATUS_STOP;
	    free(demux_name);
	    return 0;
	  }

	  _x_meta_info_set(stream, XINE_META_INFO_SYSTEMLAYER,
			   (stream->demux_plugin->demux_class->get_identifier(stream->demux_plugin->demux_class)));
	  free(demux_name);
	} else {
	  xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      if (strncasecmp(stream_setup, "save", 4) == 0) {
        if (*(stream_setup += 4) == ':') {
	  /* filename to save */
	  const char     *tmp = ++stream_setup;
	  char           *filename;
	  input_plugin_t *input_saver;

	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    filename = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(filename, tmp, stream_setup - tmp);
	    filename[stream_setup - tmp] = '\0';
	  } else {
	    filename = (char *)malloc(strlen(tmp) + 1);
	    memcpy(filename, tmp, strlen(tmp));
	    filename[strlen(tmp)] = '\0';
	  }

	  xine_log(stream->xine, XINE_LOG_MSG, _("xine: join rip input plugin\n"));
	  input_saver = _x_rip_plugin_get_instance (stream, filename);
	  if( input_saver ) {
	    stream->input_plugin = input_saver;
	  } else {
	    xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error opening rip input plugin instance\n"));
	    stream->err = XINE_ERROR_MALFORMED_MRL;
	    stream->status = XINE_STATUS_STOP;
	    return 0;
	  }

	} else {
	  xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      if (strncasecmp(stream_setup, "lastdemuxprobe", 14) == 0) {
        if (*(stream_setup += 14) == ':') {
	  /* all demuxers will be probed before the specified one */
	  const char *tmp = ++stream_setup;
	  char *demux_name;
	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    demux_name = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(demux_name, tmp, stream_setup - tmp);
	    demux_name[stream_setup - tmp] = '\0';
	  } else {
	    demux_name = (char *)malloc(strlen(tmp) + 1);
	    memcpy(demux_name, tmp, strlen(tmp));
	    demux_name[strlen(tmp)] = '\0';
	  }
	  __mrl_unescape(demux_name);
	  if (!(stream->demux_plugin = _x_find_demux_plugin_last_probe(stream, demux_name, stream->input_plugin))) {
	    xine_log(stream->xine, XINE_LOG_MSG, _("xine: last_probed demuxer %s failed to start\n"), demux_name);
	    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;
	    stream->status = XINE_STATUS_STOP;
	    free(demux_name);
	    return 0;
	  }
	  lprintf ("demux and input plugin found\n");

	  _x_meta_info_set(stream, XINE_META_INFO_SYSTEMLAYER,
			   (stream->demux_plugin->demux_class->get_identifier(stream->demux_plugin->demux_class)));
	  free(demux_name);
	} else {
	  xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      if (strncasecmp(stream_setup, "novideo", 7) == 0) {
        stream_setup += 7;
        if (*stream_setup == ';' || *stream_setup == '\0') {
	  _x_stream_info_set(stream, XINE_STREAM_INFO_IGNORE_VIDEO, 1);
	} else {
	  xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	xprintf (stream->xine, XINE_VERBOSITY_LOG, _("ignoring video\n"));
	continue;
      }
      if (strncasecmp(stream_setup, "noaudio", 7) == 0) {
        stream_setup += 7;
        if (*stream_setup == ';' || *stream_setup == '\0') {
	  _x_stream_info_set(stream, XINE_STREAM_INFO_IGNORE_AUDIO, 1);
	} else {
	  xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	xprintf (stream->xine, XINE_VERBOSITY_LOG, _("ignoring audio\n"));
	continue;
      }
      if (strncasecmp(stream_setup, "nospu", 5) == 0) {
        stream_setup += 5;
        if (*stream_setup == ';' || *stream_setup == '\0') {
	  _x_stream_info_set(stream, XINE_STREAM_INFO_IGNORE_SPU, 1);
	} else {
	  xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	xprintf (stream->xine, XINE_VERBOSITY_LOG, _("ignoring subpicture\n"));
	continue;
      }
      if (strncasecmp(stream_setup, "volume", 6) == 0) {
        if (*(stream_setup += 6) == ':') {
	  const char *tmp = ++stream_setup;
	  char *volume;
	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    volume = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(volume, tmp, stream_setup - tmp);
	    volume[stream_setup - tmp] = '\0';
	  } else {
	    volume = (char *)malloc(strlen(tmp) + 1);
	    memcpy(volume, tmp, strlen(tmp));
	    volume[strlen(tmp)] = '\0';
	  }
	  __mrl_unescape(volume);
	  xine_set_param(stream, XINE_PARAM_AUDIO_VOLUME, atoi(volume));
	  free(volume);
	} else {
	  xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      if (strncasecmp(stream_setup, "compression", 11) == 0) {
        if (*(stream_setup += 11) == ':') {
	  const char *tmp = ++stream_setup;
	  char *compression;
	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    compression = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(compression, tmp, stream_setup - tmp);
	    compression[stream_setup - tmp] = '\0';
	  } else {
	    compression = (char *)malloc(strlen(tmp) + 1);
	    memcpy(compression, tmp, strlen(tmp));
	    compression[strlen(tmp)] = '\0';
	  }
	  __mrl_unescape(compression);
	  xine_set_param(stream, XINE_PARAM_AUDIO_COMPR_LEVEL, atoi(compression));
	  free(compression);
	} else {
	  xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      if (strncasecmp(stream_setup, "subtitle", 8) == 0) {
        if (*(stream_setup += 8) == ':') {
	  const char *tmp = ++stream_setup;
	  char *subtitle_mrl;
	  stream_setup = strchr(stream_setup, ';');
	  if (stream_setup) {
	    subtitle_mrl = (char *)malloc(stream_setup - tmp + 1);
	    memcpy(subtitle_mrl, tmp, stream_setup - tmp);
	    subtitle_mrl[stream_setup - tmp] = '\0';
	  } else {
	    subtitle_mrl = (char *)malloc(strlen(tmp) + 1);
	    memcpy(subtitle_mrl, tmp, strlen(tmp));
	    subtitle_mrl[strlen(tmp)] = '\0';
	  }
	  __mrl_unescape(subtitle_mrl);
	  stream->slave = xine_stream_new (stream->xine, NULL, stream->video_out );
	  stream->slave_affection = XINE_MASTER_SLAVE_PLAY | XINE_MASTER_SLAVE_STOP;
	  if( xine_open( stream->slave, subtitle_mrl ) ) {
	    xprintf (stream->xine, XINE_VERBOSITY_LOG, _("subtitle mrl opened '%s'\n"), subtitle_mrl);
	    stream->slave->master = stream;
	    stream->slave_is_subtitle = 1; 
	  } else {
	    xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error opening subtitle mrl\n"));
	    xine_dispose( stream->slave );
	    stream->slave = NULL;
	  }
	  free(subtitle_mrl);
	} else {
	  xprintf(stream->xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
	  stream->err = XINE_ERROR_MALFORMED_MRL;
	  stream->status = XINE_STATUS_STOP;
	  return 0;
	}
	continue;
      }
      {
        /* when we got here, the stream setup parameter must be a config entry */
	const char *tmp = stream_setup;
	char *config_entry;
	int retval;
	if ((stream_setup = strchr(stream_setup, ';'))) {
	  config_entry = (char *)malloc(stream_setup - tmp + 1);
	  memcpy(config_entry, tmp, stream_setup - tmp);
	  config_entry[stream_setup - tmp] = '\0';
	} else {
	  config_entry = (char *)malloc(strlen(tmp) + 1);
	  memcpy(config_entry, tmp, strlen(tmp));
	  config_entry[strlen(tmp)] = '\0';
	}
	__mrl_unescape(config_entry);
	retval = _x_config_change_opt(stream->xine->config, config_entry);
	if (retval <= 0) {
	  if (retval == 0) {
	    /* the option not found */
	    xine_log(stream->xine, XINE_LOG_MSG, _("xine: error while parsing MRL\n"));
	  } else {
            /* not permitted to change from MRL */
            xine_log(stream->xine, XINE_LOG_MSG, _("xine: changing option '%s' from MRL isn't permitted\n"),
	      config_entry);
	  }
          stream->err = XINE_ERROR_MALFORMED_MRL;
          stream->status = XINE_STATUS_STOP;
	  free(config_entry);
          return 0;
	}
	free(config_entry);
      }
    }

  }

  if (!stream->demux_plugin) {

    /*
     * find a demux plugin
     */
    if (!(stream->demux_plugin = _x_find_demux_plugin (stream, stream->input_plugin))) {
      xine_log (stream->xine, XINE_LOG_MSG, _("xine: couldn't find demux for >%s<\n"), mrl);
      stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;

      stream->status = XINE_STATUS_STOP;

      /* force the engine to unregister fifo callbacks */
      _x_demux_control_nop(stream, BUF_FLAG_END_STREAM);

      return 0;
    }
    lprintf ("demux and input plugin found\n");

    _x_meta_info_set(stream, XINE_META_INFO_SYSTEMLAYER,
		     (stream->demux_plugin->demux_class->get_identifier(stream->demux_plugin->demux_class)));
  }

  xine_log (stream->xine, XINE_LOG_MSG, _("xine: found demuxer plugin: %s\n"),
	    stream->demux_plugin->demux_class->get_description(stream->demux_plugin->demux_class));

  _x_extra_info_reset( stream->current_extra_info );
  _x_extra_info_reset( stream->video_decoder_extra_info );
  _x_extra_info_reset( stream->audio_decoder_extra_info );

  /* assume handled for now. we will only know for sure after trying
   * to init decoders (which should happen when headers are sent)
   */
  _x_stream_info_set(stream, XINE_STREAM_INFO_VIDEO_HANDLED, 1);
  _x_stream_info_set(stream, XINE_STREAM_INFO_AUDIO_HANDLED, 1);

  /*
   * send and decode headers
   */

  stream->demux_plugin->send_headers (stream->demux_plugin);

  if (stream->demux_plugin->get_status(stream->demux_plugin) != DEMUX_OK) {
    xine_log (stream->xine, XINE_LOG_MSG, _("xine: demuxer failed to start\n"));

    stream->demux_plugin->dispose (stream->demux_plugin);
    stream->demux_plugin = NULL;

    xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "demux disposed\n");

    stream->input_plugin->dispose (stream->input_plugin);
    stream->input_plugin = NULL;
    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;

    stream->status = XINE_STATUS_STOP;

    xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "return from\n");
    return 0;
  }

  _x_demux_control_headers_done (stream);

  lprintf ("done\n");
  return 1;
}

int xine_open (xine_stream_t *stream, const char *mrl) {

  int ret;

  pthread_mutex_lock (&stream->frontend_lock);

  lprintf ("open MRL:%s\n", mrl);

  ret = __open_internal (stream, mrl);

  pthread_mutex_unlock (&stream->frontend_lock);

  return ret;
}

static void __wait_first_frame (xine_stream_t *stream) {
  if (stream->video_decoder_plugin) {
    pthread_mutex_lock (&stream->first_frame_lock);
    if (stream->first_frame_flag > 0) {
      struct timeval  tv;
      struct timespec ts;
      gettimeofday(&tv, NULL);
      ts.tv_sec  = tv.tv_sec + 10;
      ts.tv_nsec = tv.tv_usec * 1000;
      pthread_cond_timedwait(&stream->first_frame_reached, &stream->first_frame_lock, &ts);
    }
    pthread_mutex_unlock (&stream->first_frame_lock);
  }
}

static int __play_internal (xine_stream_t *stream, int start_pos, int start_time) {

  double     share ;
  off_t      pos, len;
  int        demux_status;
  int        demux_thread_running;

  xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "xine_play\n");

  if (!stream->demux_plugin) {
    xine_log (stream->xine, XINE_LOG_MSG, _("xine_play: no demux available\n"));
    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;

    return 0;
  }

  /* hint demuxer thread we want to interrupt it */
  stream->demux_action_pending = 1;

  /* set normal speed */
  if (stream->xine->clock->speed != XINE_SPEED_NORMAL)
    __set_speed_internal (stream, XINE_SPEED_NORMAL);
  
  stream->xine->port_ticket->acquire(stream->xine->port_ticket, 1);
  
  /* discard audio/video buffers to get engine going and take the lock faster */
  if (stream->audio_out)
    stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 1);
  if (stream->video_out)
    stream->video_out->set_property(stream->video_out, VO_PROP_DISCARD_FRAMES, 1);

  pthread_mutex_lock( &stream->demux_lock );
  /* demux_lock taken. now demuxer is suspended */
  stream->demux_action_pending = 0;

  /* set normal speed again (now that demuxer/input pair is suspended) 
   * some input plugin may have changed speed by itself, we must ensure
   * the engine is not paused.
   */
  if (stream->xine->clock->speed != XINE_SPEED_NORMAL)
    __set_speed_internal (stream, XINE_SPEED_NORMAL);
  
  /*
   * start/seek demux
   */
  if (start_pos) {
    pthread_mutex_lock( &stream->current_extra_info_lock );
    len = stream->current_extra_info->input_length;
    pthread_mutex_unlock( &stream->current_extra_info_lock );
    if ((len == 0) && stream->input_plugin)
      len = stream->input_plugin->get_length (stream->input_plugin);
    share = (double) start_pos / 65535;
    pos = (off_t) (share * len) ;
  } else
    pos = 0;

  /* seek to new position (no data is sent to decoders yet) */
  demux_status = stream->demux_plugin->seek (stream->demux_plugin,
					     pos, start_time, 
					     stream->demux_thread_running);

  if (stream->audio_out)
    stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 0);
  if (stream->video_out)
    stream->video_out->set_property(stream->video_out, VO_PROP_DISCARD_FRAMES, 0);

  stream->xine->port_ticket->release(stream->xine->port_ticket, 1);
  
  /* before resuming the demuxer, set first_frame_flag */
  pthread_mutex_lock (&stream->first_frame_lock);
  stream->first_frame_flag = 2;
  pthread_mutex_unlock (&stream->first_frame_lock);

  /* before resuming the demuxer, reset current position information */
  pthread_mutex_lock( &stream->current_extra_info_lock );
  _x_extra_info_reset( stream->current_extra_info );
  pthread_mutex_unlock( &stream->current_extra_info_lock );

  demux_thread_running = stream->demux_thread_running;
  
  /* now resume demuxer thread if it is running already */
  pthread_mutex_unlock( &stream->demux_lock );

  if (demux_status != DEMUX_OK) {
    xine_log (stream->xine, XINE_LOG_MSG, _("xine_play: demux failed to start\n"));

    stream->err = XINE_ERROR_DEMUX_FAILED;
    stream->first_frame_flag = 0;
    return 0;

  } else {
    if (!demux_thread_running) {
      _x_demux_start_thread( stream );
      stream->status = XINE_STATUS_PLAY;
    }
  }


  /* Wait until the first frame produced is displayed
   * see video_out.c
   */
  __wait_first_frame (stream);
  
  xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "__play_internal ...done\n");

  return 1;
}

int xine_play (xine_stream_t *stream, int start_pos, int start_time) {

  int ret;

  pthread_mutex_lock (&stream->frontend_lock);

  ret = __play_internal (stream, start_pos, start_time);
  if( stream->slave && (stream->slave_affection & XINE_MASTER_SLAVE_PLAY) )
    xine_play (stream->slave, start_pos, start_time);

  pthread_mutex_unlock (&stream->frontend_lock);
  
  return ret;
}

int xine_eject (xine_stream_t *stream) {
  
  int status;

  if (!stream->eject_class) 
    return 0;
  
  pthread_mutex_lock (&stream->frontend_lock);

  status = 0;
  /* only eject, if we are stopped OR a different input plugin is playing */
  if (stream->eject_class && stream->eject_class->eject_media &&
      ((stream->status == XINE_STATUS_STOP) ||
      stream->eject_class != stream->input_plugin->input_class)) {

    status = stream->eject_class->eject_media (stream->eject_class);
  }

  pthread_mutex_unlock (&stream->frontend_lock);
  return status;
}

void xine_dispose (xine_stream_t *stream) {

  xine_stream_t *s;

  xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "xine_dispose\n");

  stream->status = XINE_STATUS_QUIT;

  xine_close(stream);

  if( stream->master != stream ) {
    stream->master->slave = NULL;  
  }
  if( stream->slave && stream->slave->master == stream ) {
    stream->slave->master = NULL;
  }

  if(stream->broadcaster)
    _x_close_broadcaster(stream->broadcaster);
  
  xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "shutdown audio\n");
  _x_audio_decoder_shutdown (stream);

  xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "shutdown video\n");
  _x_video_decoder_shutdown (stream);

  if (stream->osd_renderer)
    stream->osd_renderer->close( stream->osd_renderer );

  pthread_mutex_destroy (&stream->info_mutex);
  pthread_mutex_destroy (&stream->meta_mutex);
  pthread_mutex_destroy (&stream->frontend_lock);
  pthread_mutex_destroy (&stream->counter_lock);
  pthread_mutex_destroy (&stream->event_queues_lock);
  pthread_mutex_destroy (&stream->current_extra_info_lock);
  pthread_cond_destroy  (&stream->counter_changed);
  pthread_mutex_destroy (&stream->demux_lock);
  pthread_mutex_destroy (&stream->first_frame_lock);
  pthread_cond_destroy  (&stream->first_frame_reached);

  stream->metronom->exit (stream->metronom);

  pthread_mutex_lock(&stream->xine->streams_lock);
  for (s = xine_list_first_content(stream->xine->streams);
       s; s = xine_list_next_content(stream->xine->streams)) {
    if (s == stream) {
      xine_list_delete_current (stream->xine->streams);
      break;
    }
  }
  pthread_mutex_unlock(&stream->xine->streams_lock);

  free (stream->current_extra_info);
  free (stream->video_decoder_extra_info);
  free (stream->audio_decoder_extra_info);
  free (stream);
}

void xine_exit (xine_t *this) {
  int i;

  xprintf (this, XINE_VERBOSITY_DEBUG, "xine_exit: bye!\n");

  for (i = 0; i < XINE_LOG_NUM; i++)
    this->log_buffers[i]->dispose (this->log_buffers[i]);

  _x_dispose_plugins (this);

  if(this->streams) {
    xine_list_free(this->streams);
    pthread_mutex_destroy(&this->streams_lock);
  }

  if(this->clock)
    this->clock->exit (this->clock);
  
  if(this->config)
    this->config->dispose(this->config);

  if(this->port_ticket)
    this->port_ticket->dispose(this->port_ticket);
  
#if defined(WIN32)
  WSACleanup();
#endif
  
  free (this);
}

xine_t *xine_new (void) {
  xine_t      *this;
  int          i;

#ifdef WIN32
    WSADATA Data;
    int i_err;
#endif /*  WIN32 */

  this = xine_xmalloc (sizeof (xine_t));
  if (!this)
    _x_abort();

  this->plugin_catalog = NULL;
  this->save_path      = NULL;
  this->streams        = NULL;
  this->clock          = NULL;
  this->port_ticket    = NULL;

#ifdef ENABLE_NLS
  /*
   * i18n
   */

  bindtextdomain("xine-lib", XINE_LOCALEDIR);
#endif

  /*
   * config
   */

  this->config = _x_config_init ();

  /*
   * log buffers
   */

  for (i = 0; i < XINE_LOG_NUM; i++)
    this->log_buffers[i] = _x_new_scratch_buffer (150);


#ifdef WIN32
    /* WinSock Library Init. */
    i_err = WSAStartup( MAKEWORD( 1, 1 ), &Data );

    if( i_err )
    {
        fprintf( stderr, "error: can't initiate WinSocks, error %i\n", i_err );
    }

#endif /* WIN32 */

  this->verbosity = XINE_VERBOSITY_NONE;
  
  return this;
}

void xine_engine_set_param(xine_t *this, int param, int value) {

  if(this) {
    switch(param) {

    case XINE_ENGINE_PARAM_VERBOSITY:
      this->verbosity = value;
      break;

    default:
      lprintf("Unknown parameter %d\n", param);
      break;
    }
  }
}

int xine_engine_get_param(xine_t *this, int param) {

  if(this) {
    switch(param) {

    case XINE_ENGINE_PARAM_VERBOSITY:
      return this->verbosity;
      break;

    default:
      lprintf("Unknown parameter %d\n", param);
      break;
    }
  }
  return -1;
}

static void __config_demux_strategy_cb (void *this_gen, xine_cfg_entry_t *entry) {
  xine_t *this = (xine_t *)this_gen;

  this->demux_strategy = entry->num_value;
}

static void __config_save_cb (void *this_gen, xine_cfg_entry_t *entry) {
  xine_t *this = (xine_t *)this_gen;
  char *homedir_trail_slash;

  homedir_trail_slash = (char *)malloc(strlen(xine_get_homedir()) + 2);
  sprintf(homedir_trail_slash, "%s/", xine_get_homedir());
  if (entry->str_value[0] &&
      (entry->str_value[0] != '/' || strstr(entry->str_value, "/.") ||
       strcmp(entry->str_value, xine_get_homedir()) == 0 ||
       strcmp(entry->str_value, homedir_trail_slash) == 0)) {
    xine_stream_t *stream;
    
    xine_log(this, XINE_LOG_MSG,
	     _("xine: The specified save_dir \"%s\" might be a security risk.\n"), entry->str_value);
    
    pthread_mutex_lock(&this->streams_lock);
    if ((stream = (xine_stream_t *)xine_list_first_content(this->streams)))
      _x_message(stream, XINE_MSG_SECURITY, _("The specified save_dir might be a security risk."), NULL);
    pthread_mutex_unlock(&this->streams_lock);
  }
  
  free(homedir_trail_slash);
  this->save_path = entry->str_value;
}

void xine_init (xine_t *this) {
  static char *demux_strategies[] = {"default", "reverse", "content",
				     "extension", NULL};

  /* initialize color conversion tables and functions */
  init_yuv_conversion();

  /* probe for optimized memcpy or config setting */
  xine_probe_fast_memcpy (this);

  /*
   * plugins
   */
  _x_scan_plugins(this);

#ifdef HAVE_SETLOCALE
  if (!setlocale(LC_CTYPE, ""))
    xprintf(this, XINE_VERBOSITY_LOG, _("xine: locale not supported by C library\n"));
#endif

  /*
   * content detection strategy
   */
  this->demux_strategy  = this->config->register_enum (
      this->config, "misc.demux_strategy", 0,
      demux_strategies,
      _("media format detection strategy"),
      _("xine offers various methods to detect the media format of input to play. "
	"The individual values are:\n\n"
	"default\n"
	"First try to detect by content, then by file name extension.\n\n"
	"reverse\n"
	"First try to detect by file name extension, then by content.\n\n"
	"content\n"
	"Detect by content only.\n\n"
	"extension\n"
	"Detect by file name extension only.\n"),
      20, __config_demux_strategy_cb, this);

  /*
   * save directory
   */
  this->save_path  = this->config->register_string (
      this->config, 
      "misc.save_dir", "",
      _("directory for saving streams"),
      _("When using the stream save feature, files will be written only into this directory.\n"
	"This setting is security critical, because when changed to a different directory, xine "
	"can be used to fill files in it with arbitrary content. So you should be careful that "
	"the directory you specify is robust against any content in any file."),
      XINE_CONFIG_SECURITY, __config_save_cb, this);
  
  /*
   * implicit configuration changes
   */
  this->config->register_bool(this->config,
      "misc.implicit_config", 0,
      _("allow implicit changes to the configuration (e.g. by MRL)"),
      _("If enabled, you allow xine to change your configuration without "
	"explicit actions from your side. For example configuration changes "
	"demanded by MRLs or embedded into playlist will be executed.\n"
	"This setting is security critcal, because xine can receive MRLs or "
	"playlists from untrusted remote sources. If you allow them to "
	"arbitrarily change your configuration, you might end with a totally "
	"messed up xine."),
      XINE_CONFIG_SECURITY, NULL, this);

  /*
   * keep track of all opened streams
   */
  this->streams = xine_list_new();

  /*
   * streams lock
   */
  pthread_mutex_init (&this->streams_lock, NULL);
  
  /*
   * start metronom clock
   */

  this->clock = _x_metronom_clock_init(this);

  this->clock->start_clock (this->clock, 0);
  
  /*
   * tickets
   */
  this->port_ticket = ticket_init();
}

void _x_select_spu_channel (xine_stream_t *stream, int channel) {

  pthread_mutex_lock (&stream->frontend_lock);
  stream->spu_channel_user = (channel >= -2 ? channel : -2);

  stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
  
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
  lprintf("set to %d\n",stream->spu_channel);

  stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
  
  pthread_mutex_unlock (&stream->frontend_lock);
}

static int __get_current_position (xine_stream_t *stream) {

  off_t len;
  double share;

  pthread_mutex_lock (&stream->frontend_lock);

  if (!stream->input_plugin) {
    lprintf ("no input source\n");
    pthread_mutex_unlock (&stream->frontend_lock);
    return -1;
  }

  if ( (!stream->video_decoder_plugin && !stream->audio_decoder_plugin) ) {
    if( _x_stream_info_get(stream, XINE_STREAM_INFO_HAS_VIDEO) )
      _x_extra_info_merge( stream->current_extra_info, stream->video_decoder_extra_info );
    else
      _x_extra_info_merge( stream->current_extra_info, stream->audio_decoder_extra_info );
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

void _x_get_current_info (xine_stream_t *stream, extra_info_t *extra_info, int size) {

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

void _x_set_speed (xine_stream_t *stream, int speed) {

  if (stream->ignore_speed_change)
    return;

  if (speed <= XINE_SPEED_PAUSE)
    speed = XINE_SPEED_PAUSE;
  else if (speed > XINE_SPEED_FAST_4)
    speed = XINE_SPEED_FAST_4;

  xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "set_speed %d\n", speed);
  __set_speed_internal (stream, speed);
  
  if (stream->slave && (stream->slave_affection & XINE_MASTER_SLAVE_SPEED))
    __set_speed_internal (stream->slave, speed);
}


/*
 * time measurement / seek
 */

static int __get_stream_length (xine_stream_t *stream) {

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

  int pos = __get_current_position (stream); /* force updating extra_info */

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
    *length_time = __get_stream_length (stream);

  return 1;
}

int xine_get_current_frame (xine_stream_t *stream, int *width, int *height,
			    int *ratio_code, int *format,
			    uint8_t *img) {

  vo_frame_t *frame;

  stream->xine->port_ticket->acquire(stream->xine->port_ticket, 0);
  frame = stream->video_out->get_last_frame (stream->video_out);
  stream->xine->port_ticket->release(stream->xine->port_ticket, 0);
  
  if (!frame)
    return 0;

  *width = frame->width;
  *height = frame->height;

  *ratio_code = frame->ratio;
  *format = frame->format;

  if (img){
    switch (frame->format) {

    case XINE_IMGFMT_YV12:
      yv12_to_yv12(
       /* Y */
        frame->base[0], frame->pitches[0],
        img, frame->width,
       /* U */
        frame->base[1], frame->pitches[1],
        img+frame->width*frame->height, frame->width/2,
       /* V */
        frame->base[2], frame->pitches[2],
        img+frame->width*frame->height+frame->width*frame->height/4, frame->width/2,
       /* width x height */
        frame->width, frame->height);
      break;

    case XINE_IMGFMT_YUY2:
      yuy2_to_yuy2(
       /* src */
        frame->base[0], frame->pitches[0],
       /* dst */
        img, frame->width*2,
       /* width x height */
        frame->width, frame->height);
      break;

    default:
      xprintf (stream->xine, XINE_VERBOSITY_DEBUG, 
	       "xine: error, snapshot function not implemented for format 0x%x\n", frame->format);
      _x_abort ();
    }
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
  xprintf (stream->xine, XINE_VERBOSITY_DEBUG, "xine: xine_get_video_frame not implemented yet.\n");
  _x_abort ();
  return 0;
}

int xine_get_spu_lang (xine_stream_t *stream, int channel, char *lang) {

  /* Ask the demuxer first (e.g. TS extracts this information from
   * the stream)
   **/
  if (stream->demux_plugin) {
    if (stream->demux_plugin->get_capabilities (stream->demux_plugin) & DEMUX_CAP_SPULANG) {
      /* pass the channel number to the plugin in the data field */
      *((int *)lang) = channel;
      if (stream->demux_plugin->get_optional_data (stream->demux_plugin, lang,
	  DEMUX_OPTIONAL_DATA_SPULANG) == DEMUX_OPTIONAL_SUCCESS)
        return 1;
    }
  }

  /* No match, check with input plugin instead (e.g. DVD gets this
   * info from the IFO).
   **/
  if (stream->input_plugin) {
    if (stream->input_plugin->get_capabilities (stream->input_plugin) & INPUT_CAP_SPULANG) {
      /* pass the channel number to the plugin in the data field */
      *((int *)lang) = channel;
      if (stream->input_plugin->get_optional_data (stream->input_plugin, lang,
	  INPUT_OPTIONAL_DATA_SPULANG) == INPUT_OPTIONAL_SUCCESS)
        return 1;
    }
  }

  return 0;
}

int xine_get_audio_lang (xine_stream_t *stream, int channel, char *lang) {

  if (stream->demux_plugin) {
    if (stream->demux_plugin->get_capabilities (stream->demux_plugin) & DEMUX_CAP_AUDIOLANG) {
      /* pass the channel number to the plugin in the data field */
      *((int *)lang) = channel;
      if (stream->demux_plugin->get_optional_data (stream->demux_plugin, lang,
	  DEMUX_OPTIONAL_DATA_AUDIOLANG) == DEMUX_OPTIONAL_SUCCESS)
        return 1;
    }
  }

  if (stream->input_plugin) {
    if (stream->input_plugin->get_capabilities (stream->input_plugin) & INPUT_CAP_AUDIOLANG) {
      /* pass the channel number to the plugin in the data field */
      *((int *)lang) = channel;
      if (stream->input_plugin->get_optional_data (stream->input_plugin, lang,
	  INPUT_OPTIONAL_DATA_AUDIOLANG) == INPUT_OPTIONAL_SUCCESS)
        return 1;
    }
  }

  return 0;
}

int _x_get_spu_channel (xine_stream_t *stream) {
  return stream->spu_channel_user;
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
  log_sections[XINE_LOG_TRACE]    = _("trace");
  log_sections[XINE_LOG_NUM]      = NULL;

  return log_sections;
}

void xine_log (xine_t *this, int buf, const char *format, ...) {
  va_list argp;
  char    buffer[SCRATCH_LINE_LEN_MAX];
  
  va_start (argp, format);
  this->log_buffers[buf]->scratch_printf (this->log_buffers[buf], format, argp);
  va_end(argp);

  if(this->verbosity) {
    va_start(argp, format);
    vsnprintf(buffer, SCRATCH_LINE_LEN_MAX, format, argp);
    printf("%s", buffer);
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
  _x_abort();
}


int xine_get_error (xine_stream_t *stream) {
  return stream->err;
}

int xine_trick_mode (xine_stream_t *stream, int mode, int value) {
  printf ("xine: xine_trick_mode not implemented yet.\n");
  _x_abort ();
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
