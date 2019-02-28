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
 *
 *
 * functions that implement audio decoding
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#define LOG_MODULE "audio_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include "xine_private.h"

static void *audio_decoder_loop (void *stream_gen) {

  xine_stream_private_t *stream = (xine_stream_private_t *)stream_gen;
  buf_element_t   *buf = NULL;
  buf_element_t   *first_header = NULL;
  buf_element_t   *last_header = NULL;
  int              replaying_headers = 0;
  xine_ticket_t   *running_ticket = stream->s.xine->port_ticket;
  int              running = 1;
  int              prof_audio_decode = -1;
  uint32_t         buftype_unknown = 0;
  int              audio_channel_user = stream->audio_channel_user;
  /* generic bitrate estimation. */
  int64_t          audio_br_lasttime = 0;
  uint32_t         audio_br_lastsize = 0;
  uint32_t         audio_br_time     = 1;
  uint32_t         audio_br_bytes    = 0;
  int              audio_br_num      = 20;
  int              audio_br_value    = 0;

  if (prof_audio_decode == -1)
    prof_audio_decode = xine_profiler_allocate_slot ("audio decoder/output");

  running_ticket->acquire (running_ticket, 0);

  while (running) {

    lprintf ("audio_loop: waiting for package...\n");

    if( !replaying_headers )
      buf = stream->s.audio_fifo->tget (stream->s.audio_fifo, running_ticket);

    lprintf ("audio_loop: got package pts = %"PRId64", type = %08x\n", buf->pts, buf->type);

    _x_extra_info_merge( stream->audio_decoder_extra_info, buf->extra_info );
    stream->audio_decoder_extra_info->seek_count = stream->video_seek_count;

    switch (buf->type) {

    case BUF_CONTROL_HEADERS_DONE:
      pthread_mutex_lock (&stream->counter_lock);
      stream->header_count_audio++;
      pthread_cond_broadcast (&stream->counter_changed);
      pthread_mutex_unlock (&stream->counter_lock);
      break;

    case BUF_CONTROL_START:

      lprintf ("start\n");

      /* decoder dispose might call port functions */
      /* running_ticket->acquire(running_ticket, 0); */

      if (stream->audio_decoder_plugin) {

	lprintf ("close old decoder\n");

	stream->keep_ao_driver_open = !!(buf->decoder_flags & BUF_FLAG_GAPLESS_SW);
	_x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
	stream->audio_decoder_plugin = NULL;
	stream->audio_track_map_entries = 0;
	stream->audio_type = 0;
	stream->keep_ao_driver_open = 0;
      }

      /* running_ticket->release(running_ticket, 0); */

      if (!(buf->decoder_flags & BUF_FLAG_GAPLESS_SW)) {
        running_ticket->release (running_ticket, 0);
        stream->s.metronom->handle_audio_discontinuity (stream->s.metronom, DISC_STREAMSTART, 0);
        running_ticket->acquire (running_ticket, 0);
      }
      buftype_unknown = 0;
      break;

    case BUF_CONTROL_END:

      /* free all held header buffers, see comments below */
      _x_free_buf_elements( first_header );
      first_header = last_header = NULL;

      /*
       * wait the output fifos to run dry before sending the notification event
       * to the frontend. this test is only valid if there is only a single
       * stream attached to the current output port.
       */
      while(1) {
        int num_bufs, num_streams;

        /* running_ticket->acquire(running_ticket, 0); */
        num_bufs = stream->s.audio_out->get_property (stream->s.audio_out, AO_PROP_BUFS_IN_FIFO);
        num_streams = stream->s.audio_out->get_property (stream->s.audio_out, AO_PROP_NUM_STREAMS);
        /* running_ticket->release(running_ticket, 0); */

        if( num_bufs > 0 && num_streams == 1 && !stream->early_finish_event) {
          running_ticket->release (running_ticket, 0);
          xine_usec_sleep (10000);
          running_ticket->acquire (running_ticket, 0);
        } else
          break;
      }

      running_ticket->release (running_ticket, 0);

      /* wait for video to reach this marker, if necessary */
      pthread_mutex_lock (&stream->counter_lock);

      stream->finished_count_audio++;

      lprintf ("reached end marker # %d\n", stream->finished_count_audio);

      pthread_cond_broadcast (&stream->counter_changed);

      if (stream->video_thread_created) {
        while (stream->finished_count_video < stream->finished_count_audio) {
          struct timespec ts = {0, 0};
          xine_gettime (&ts);
          ts.tv_sec += 1;
          /* use timedwait to workaround buggy pthread broadcast implementations */
          pthread_cond_timedwait (&stream->counter_changed, &stream->counter_lock, &ts);
        }
      }
      pthread_mutex_unlock (&stream->counter_lock);
      stream->s.audio_channel_auto = -1;

      running_ticket->acquire (running_ticket, 0);
      break;

    case BUF_CONTROL_QUIT:
      /* decoder dispose might call port functions */
      /* running_ticket->acquire(running_ticket, 0); */

      if (stream->audio_decoder_plugin) {
        _x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
	stream->audio_decoder_plugin = NULL;
	stream->audio_track_map_entries = 0;
	stream->audio_type = 0;
      }

      /* running_ticket->release(running_ticket, 0); */
      running = 0;
      break;

    case BUF_CONTROL_NOP:
      break;

    case BUF_CONTROL_RESET_DECODER:
      lprintf ("reset\n");

      _x_extra_info_reset( stream->audio_decoder_extra_info );
      if (stream->audio_decoder_plugin) {
	/* running_ticket->acquire(running_ticket, 0); */
	stream->audio_decoder_plugin->reset (stream->audio_decoder_plugin);
	/* running_ticket->release(running_ticket, 0); */
      }
      break;

    case BUF_CONTROL_DISCONTINUITY:
      if (stream->audio_decoder_plugin) {
	/* running_ticket->acquire(running_ticket, 0); */
	stream->audio_decoder_plugin->discontinuity (stream->audio_decoder_plugin);
	/* running_ticket->release(running_ticket, 0); */
      }

      running_ticket->release (running_ticket, 0);
      stream->s.metronom->handle_audio_discontinuity (stream->s.metronom, DISC_RELATIVE, buf->disc_off);
      running_ticket->acquire (running_ticket, 0);
      break;

    case BUF_CONTROL_NEWPTS:
      if (stream->audio_decoder_plugin) {
	/* running_ticket->acquire(running_ticket, 0); */
	stream->audio_decoder_plugin->discontinuity (stream->audio_decoder_plugin);
	/* running_ticket->release(running_ticket, 0); */
      }

      running_ticket->release (running_ticket, 0);
      if (buf->decoder_flags & BUF_FLAG_SEEK) {
        stream->s.metronom->handle_audio_discontinuity (stream->s.metronom, DISC_STREAMSEEK, buf->disc_off);
      } else {
        stream->s.metronom->handle_audio_discontinuity (stream->s.metronom, DISC_ABSOLUTE, buf->disc_off);
      }
      running_ticket->acquire (running_ticket, 0);

      /* audio_br_discontinuity */
      audio_br_lasttime = 0;
      audio_br_lastsize = 0;

      break;

    case BUF_CONTROL_AUDIO_CHANNEL:
      {
        xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
          "audio_decoder: suggested switching to stream_id %02x\n", buf->decoder_info[0]);
        stream->s.audio_channel_auto = buf->decoder_info[0] & 0xff;
      }
      break;

    case BUF_CONTROL_RESET_TRACK_MAP:
      if (stream->audio_track_map_entries)
      {
        xine_event_t ui_event;

        stream->audio_track_map_entries = 0;

        ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
        ui_event.data_length = 0;
        xine_event_send (&stream->s, &ui_event);
      }
      break;

    case BUF_AUDIO_UNKNOWN:
      break;

    default:

      if (_x_stream_info_get (&stream->s, XINE_STREAM_INFO_IGNORE_AUDIO))
        break;

      xine_profiler_start_count (prof_audio_decode);

      /* running_ticket->acquire (running_ticket, 0); */

      if ( (buf->type & 0xFF000000) == BUF_AUDIO_BASE ) {

	uint32_t audio_type = 0;
	int      i,j;
	uint32_t chan=buf->type&0x0000FFFF;

	/*
        printf("audio_decoder: buf_type=%08x auto=%08x user=%08x\n",
	       buf->type,
	       stream->audio_channel_auto,
	       audio_channel_user);
	       */

        /* update track map */

        i = 0;
        while ( (i<stream->audio_track_map_entries) && ((stream->audio_track_map[i]&0x0000FFFF)<chan) )
          i++;

        if ( (i==stream->audio_track_map_entries)
	     || ((stream->audio_track_map[i]&0x0000FFFF)!=chan) ) {
          xine_event_t  ui_event;

          j = stream->audio_track_map_entries;

          if (j >= 50)
            break;

          while (j>i) {
            stream->audio_track_map[j] = stream->audio_track_map[j-1];
            j--;
          }
          stream->audio_track_map[i] = buf->type;
          stream->audio_track_map_entries++;
          /* implicit channel change - reopen decoder below */
          if ((i == 0) && (audio_channel_user == -1) && (stream->s.audio_channel_auto < 0))
            stream->audio_decoder_streamtype = -1;

	  ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
	  ui_event.data_length = 0;
          xine_event_send (&stream->s, &ui_event);
        }

	/* find out which audio type to decode */

	lprintf ("audio_channel_user = %d, map[0]=%08x\n",
		 audio_channel_user,
		 stream->audio_track_map[0]);

	if (audio_channel_user > -2) {

	  if (audio_channel_user == -1) {

	    /* auto */

            lprintf ("audio_channel_auto = %d\n", stream->s.audio_channel_auto);

            if (stream->s.audio_channel_auto >= 0) {

              if ((int)(buf->type & 0xFF) == stream->s.audio_channel_auto) {
		audio_type = buf->type;
	      } else
		audio_type = -1;

	    } else
	      audio_type = stream->audio_track_map[0];

	  } else {
	    if (audio_channel_user <= stream->audio_track_map_entries)
	      audio_type = stream->audio_track_map[audio_channel_user];
	    else
	      audio_type = -1;
	  }

	  /* now, decode stream buffer if it's the right audio type */

	  if (buf->type == audio_type) {

	    int streamtype = (buf->type>>16) & 0xFF;

	    /* close old decoder of audio type has changed */

            if( buf->type != buftype_unknown &&
                (stream->audio_decoder_streamtype != streamtype ||
                !stream->audio_decoder_plugin) ) {

              if (stream->audio_decoder_plugin) {
                _x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
              }

              stream->audio_decoder_streamtype = streamtype;
              stream->audio_decoder_plugin = _x_get_audio_decoder (&stream->s, streamtype);

              _x_stream_info_set (&stream->s, XINE_STREAM_INFO_AUDIO_HANDLED, 
				 (stream->audio_decoder_plugin != NULL));

              /* audio_br_reset */
              audio_br_lasttime = 0;
              audio_br_lastsize = 0;
              audio_br_time     = 1; /* No / 0 please. */
              audio_br_bytes    = 0;
              audio_br_num      = 20;
              audio_br_value    = 0;
            }

	    if (audio_type != stream->audio_type) {

	      if (stream->audio_decoder_plugin) {
		xine_event_t event;

		stream->audio_type = audio_type;

		event.type         = XINE_EVENT_UI_CHANNELS_CHANGED;
		event.data_length  = 0;
                xine_event_send (&stream->s, &event);
	      }
	    }

	    /* finally - decode data */

	    if (stream->audio_decoder_plugin)
	      stream->audio_decoder_plugin->decode_data (stream->audio_decoder_plugin, buf);

            /* audio_br_add */
            if (buf->pts) {
              int64_t d = buf->pts - audio_br_lasttime;
              if (d > 0) {
                if (d < 220000) {
                  audio_br_time += d;
                  audio_br_bytes += audio_br_lastsize;
                  audio_br_lastsize = 0;
                  if (--audio_br_num < 0) {
                    int br, bdiff;
                    audio_br_num = 20;
                    if ((audio_br_bytes | audio_br_time) & 0x80000000) {
                      audio_br_bytes >>= 1;
                      audio_br_time  >>= 1;
                    }
                    br = (uint64_t)audio_br_bytes * 90000 * 8 / audio_br_time;
                    bdiff = br - audio_br_value;
                    if (bdiff < 0)
                      bdiff = -bdiff;
                    if (bdiff > (br >> 6)) {
                      audio_br_value = br;
                      _x_stream_info_set (&stream->s, XINE_STREAM_INFO_AUDIO_BITRATE, br);
                    }
                  }
                }
                audio_br_lasttime = buf->pts;
              } else {
                /* Do we really need to care for reordered audio? So what. */
                if (d <= -220000)
                  audio_br_lasttime = buf->pts;
              }
            }
            audio_br_lastsize += buf->size;

	    if (buf->type != buftype_unknown &&
              !_x_stream_info_get (&stream->s, XINE_STREAM_INFO_AUDIO_HANDLED)) {
              xine_log (stream->s.xine, XINE_LOG_MSG,
			_("audio_decoder: no plugin available to handle '%s'\n"), _x_buf_audio_name( buf->type ) );

              if (!_x_meta_info_get (&stream->s, XINE_META_INFO_AUDIOCODEC))
                _x_meta_info_set_utf8 (&stream->s, XINE_META_INFO_AUDIOCODEC, _x_buf_audio_name( buf->type ));

	      buftype_unknown = buf->type;

	      /* fatal error - dispose plugin */
	      if (stream->audio_decoder_plugin) {
                _x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
	        stream->audio_decoder_plugin = NULL;
	      }
	    }
	  }
	}
      } else if( buf->type != buftype_unknown ) {
        xine_log (stream->s.xine, XINE_LOG_MSG,
		    _("audio_decoder: error, unknown buffer type: %08x\n"), buf->type );
	  buftype_unknown = buf->type;
      }

      /* if (running_ticket->ticket_revoked)
       *   running_ticket->renew (running_ticket, 0);
       * running_ticket->release (running_ticket, 0);
       */

      xine_profiler_stop_count (prof_audio_decode);
    }

    /* some decoders require a full reinitialization when audio
     * channel is changed (rate might be change and even a
     * different codec may be used).
     *
     * we must close the old decoder and process all the headers
     * again, since they are needed for decoder initialization.
     */
    if( audio_channel_user != stream->audio_channel_user &&
        !replaying_headers ) {
      audio_channel_user = stream->audio_channel_user;

      if (stream->audio_decoder_plugin) {
	/* decoder dispose might call port functions */
        /* running_ticket->acquire (running_ticket, 0); */
        _x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
        /* running_ticket->release (running_ticket, 0); */

        stream->audio_decoder_plugin = NULL;
        stream->audio_track_map_entries = 0;
        stream->audio_type = 0;
      }

      buf->free_buffer (buf);
      if( first_header ) {
        replaying_headers = 1;
        buf = first_header;
      } else {
        replaying_headers = 0;
      }
    } else if( !replaying_headers ) {

      /* header buffers are never freed. instead they
       * are added to a list to allow replaying them
       * in case of a channel change.
       */
      if( (buf->decoder_flags & BUF_FLAG_HEADER) ) {
        if( last_header )
          last_header->next = buf;
        else
          first_header = buf;
        buf->next = NULL;
        last_header = buf;
      } else {
        buf->free_buffer (buf);
      }
    } else {
      buf = buf->next;
      if( !buf )
        replaying_headers = 0;
    }
  }

  running_ticket->release (running_ticket, 0);

  /* free all held header buffers */
  _x_free_buf_elements( first_header );

  return NULL;
}

int _x_audio_decoder_init (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  if (stream->s.audio_out == NULL) {

    stream->s.audio_fifo = _x_dummy_fifo_buffer_new (5, 8192);
    return !!stream->s.audio_fifo;

  } else {

    pthread_attr_t     pth_attrs;
#if defined(_POSIX_THREAD_PRIORITY_SCHEDULING) && (_POSIX_THREAD_PRIORITY_SCHEDULING > 0)
    struct sched_param pth_params;
#endif
    int err;
    int num_buffers;

    /* The fifo size is based on dvd playback where buffers are filled
     * with 2k of data. With 230 buffers and a typical audio data rate
     * of 1.8 Mbit/s (four ac3 streams), the fifo can hold about 2 seconds
     * of audio, wich should be enough to compensate for drive delays.
     * We provide buffers of 8k size instead of 2k for demuxers sending
     * larger chunks.
     */

    num_buffers = stream->s.xine->config->register_num (stream->s.xine->config,
      "engine.buffers.audio_num_buffers", 230,
      _("number of audio buffers"),
      _("The number of audio buffers (each is 8k in size) xine uses in its "
        "internal queue. Higher values mean smoother playback for unreliable "
        "inputs, but also increased latency and memory consumption."),
      20, NULL, NULL);
    if (num_buffers > 2000)
      num_buffers = 2000;

    stream->s.audio_fifo = _x_fifo_buffer_new (num_buffers, 8192);
    if (!stream->s.audio_fifo)
      return 0;

    stream->audio_channel_user = -1;
    stream->s.audio_channel_auto = -1;
    stream->audio_track_map_entries = 0;
    stream->audio_type = 0;

    /* future magic - coming soon
     * stream->audio_temp = lrb_new (100, stream->audio_fifo);
     */

    pthread_attr_init(&pth_attrs);
#if defined(_POSIX_THREAD_PRIORITY_SCHEDULING) && (_POSIX_THREAD_PRIORITY_SCHEDULING > 0)
    pthread_attr_getschedparam(&pth_attrs, &pth_params);
    pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
    pthread_attr_setschedparam(&pth_attrs, &pth_params);
    pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
#endif

    stream->audio_thread_created = 1;
    if ((err = pthread_create (&stream->audio_thread,
                               &pth_attrs, audio_decoder_loop, stream)) != 0) {
      xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
        "audio_decoder: can't create new thread (%s)\n", strerror(err));
      stream->audio_thread_created = 0;
      pthread_attr_destroy(&pth_attrs);
      stream->s.audio_fifo->dispose (stream->s.audio_fifo);
      stream->s.audio_fifo = NULL;
      return 0;
    }

    pthread_attr_destroy(&pth_attrs);
    return 1;
  }
}

void _x_audio_decoder_shutdown (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  buf_element_t *buf;
  void          *p;

  if (stream->audio_thread_created) {
    /* stream->audio_fifo->clear(stream->audio_fifo); */

    buf = stream->s.audio_fifo->buffer_pool_alloc (stream->s.audio_fifo);
    buf->type = BUF_CONTROL_QUIT;
    stream->s.audio_fifo->put (stream->s.audio_fifo, buf);

    pthread_join (stream->audio_thread, &p);
    stream->audio_thread_created = 0;
  }

  stream->s.audio_fifo->dispose (stream->s.audio_fifo);
  stream->s.audio_fifo = NULL;
}

int _x_get_audio_channel (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  return stream->audio_type & 0xFFFF;
}


