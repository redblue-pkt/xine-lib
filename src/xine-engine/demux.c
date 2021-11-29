/*
 * Copyright (C) 2000-2021 the xine project
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
 * Demuxer helper functions
 * hide some xine engine details from demuxers and reduce code duplication
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define LOG_MODULE "demux"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include "xine_private.h"

#ifdef WIN32
#include <winsock.h>
#endif

/*
 *  Flush audio and video buffers. It is called from demuxers on
 *  seek/stop, and may be useful when user input changes a stream and
 *  xine-lib has cached buffers that have yet to be played.
 *
 * warning: after clearing decoders fifos an absolute discontinuity
 *          indication must be sent. relative discontinuities are likely
 *          to cause "jumps" on metronom.
 */
void _x_demux_flush_engine (xine_stream_t *s) {

  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  xine_private_t *xine = (xine_private_t *)stream->s.xine;
  buf_element_t *buf;

  stream = stream->side_streams[0];

  if (stream->gapless_switch || stream->finished_naturally)
    return;

  xine->port_ticket->acquire (xine->port_ticket, 1);

  /* only flush/discard output ports on master streams */
  if (stream->s.master == &stream->s) {
    if (stream->s.video_out) {
      stream->s.video_out->set_property (stream->s.video_out, VO_PROP_DISCARD_FRAMES, 1);
    }
    if (stream->s.audio_out) {
      stream->s.audio_out->set_property (stream->s.audio_out, AO_PROP_DISCARD_BUFFERS, 1);
    }
  }

  stream->s.video_fifo->clear (stream->s.video_fifo);
  stream->s.audio_fifo->clear (stream->s.audio_fifo);

  pthread_mutex_lock (&stream->demux.pair);

  buf = stream->s.video_fifo->buffer_pool_alloc (stream->s.video_fifo);
  buf->type = BUF_CONTROL_RESET_DECODER;
  stream->s.video_fifo->put (stream->s.video_fifo, buf);

  buf = stream->s.audio_fifo->buffer_pool_alloc (stream->s.audio_fifo);
  buf->type = BUF_CONTROL_RESET_DECODER;
  stream->s.audio_fifo->put (stream->s.audio_fifo, buf);

  pthread_mutex_unlock (&stream->demux.pair);

  /* on seeking we must wait decoder fifos to process before doing flush.
   * otherwise we flush too early (before the old data has left decoders)
   */
  _x_demux_control_headers_done (&stream->s);

  if (stream->s.video_out) {
    video_overlay_manager_t *ovl = stream->s.video_out->get_overlay_manager (stream->s.video_out);
    ovl->flush_events(ovl);
  }

  /* only flush/discard output ports on master streams */
  if (stream->s.master == &stream->s) {
    if (stream->s.video_out) {
      stream->s.video_out->flush (stream->s.video_out);
      stream->s.video_out->set_property (stream->s.video_out, VO_PROP_DISCARD_FRAMES, 0);
    }

    if (stream->s.audio_out) {
      stream->s.audio_out->flush (stream->s.audio_out);
      stream->s.audio_out->set_property (stream->s.audio_out, AO_PROP_DISCARD_BUFFERS, 0);
    }
  }

  xine->port_ticket->release (xine->port_ticket, 1);
}


void _x_demux_control_newpts (xine_stream_t *s, int64_t pts, uint32_t flags) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  buf_element_t *bufa, *bufv;

  stream = stream->side_streams[0];

  if (flags & BUF_FLAG_SEEK) {
    pthread_mutex_lock (&stream->demux.pair);
    if (stream->demux.max_seek_bufs == 0) {
      pthread_mutex_unlock (&stream->demux.pair);
      return;
    }
    stream->demux.max_seek_bufs--;
    pthread_mutex_unlock (&stream->demux.pair);
  }

  bufv = stream->s.video_fifo->buffer_pool_alloc (stream->s.video_fifo);
  bufa = stream->s.audio_fifo->buffer_pool_alloc (stream->s.audio_fifo);

  pthread_mutex_lock (&stream->demux.pair);

  bufv->type = BUF_CONTROL_NEWPTS;
  bufv->decoder_flags = flags;
  bufv->disc_off = pts;
  stream->s.video_fifo->put (stream->s.video_fifo, bufv);

  bufa->type = BUF_CONTROL_NEWPTS;
  bufa->decoder_flags = flags;
  bufa->disc_off = pts;
  stream->s.audio_fifo->put (stream->s.audio_fifo, bufa);

  pthread_mutex_unlock (&stream->demux.pair);
}

/* avoid ao_loop being stuck in a pthread_cond_wait, waiting for data;
 * return 1 if the stream is stopped
 * (better fix wanted!)
 */
static int demux_unstick_ao_loop (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
/*  if (!stream->audio_thread_created)
    return 0;
*/
  int status = xine_get_status (&stream->s);
  if (status != XINE_STATUS_QUIT && status != XINE_STATUS_STOP && stream->demux.plugin->get_status (stream->demux.plugin) != DEMUX_FINISHED)
    return 0;
#if 0
  /* right, stream is stopped... */
  audio_buffer_t *buf = stream->s.audio_out->get_buffer (stream->s.audio_out);
  buf->num_frames = 0;
  buf->stream = NULL;
  stream->s.audio_out->put_buffer (stream->s.audio_out, buf, stream);
#endif
  lprintf("stuck\n");
  return 1;
}

/* sync with decoder fifos, making sure everything gets processed */
void _x_demux_control_headers_done (xine_stream_t *s) {

  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  int headers_audio;
  int headers_video;
  unsigned int max_iterations;
  buf_element_t *buf_video, *buf_audio;

  stream = stream->side_streams[0];

  /* we use demux.action_pending to wake up sleeping spu decoders */
  _x_action_raise (&stream->s);

  /* allocate the buffers before grabbing the lock to prevent cyclic wait situations */
  buf_video = stream->s.video_fifo->buffer_pool_alloc (stream->s.video_fifo);
  buf_audio = stream->s.audio_fifo->buffer_pool_alloc (stream->s.audio_fifo);

  pthread_mutex_lock (&stream->counter.lock);

  if (stream->video_thread_created) {
    headers_video = stream->counter.headers_video + 1;
  } else {
    headers_video = 0;
  }

  if (stream->audio_thread_created) {
    headers_audio = stream->counter.headers_audio + 1;
  } else {
    headers_audio = 0;
  }

  pthread_mutex_lock (&stream->demux.pair);

  buf_video->type = BUF_CONTROL_HEADERS_DONE;
  stream->s.video_fifo->put (stream->s.video_fifo, buf_video);

  buf_audio->type = BUF_CONTROL_HEADERS_DONE;
  stream->s.audio_fifo->put (stream->s.audio_fifo, buf_audio);

  pthread_mutex_unlock (&stream->demux.pair);
  max_iterations = 0;

  while ((stream->counter.headers_audio < headers_audio) ||
         (stream->counter.headers_video < headers_video)) {
    struct timespec ts = {0, 0};
    int ret_wait;

    lprintf ("waiting for headers. v:%d %d   a:%d %d\n",
	     stream->counter.headers_video, headers_video,
	     stream->counter.headers_audio, headers_audio);

    xine_gettime (&ts);
    ts.tv_sec += 1;

    /* use timedwait to workaround buggy pthread broadcast implementations */
    ret_wait = pthread_cond_timedwait (&stream->counter.changed, &stream->counter.lock, &ts);

    if (ret_wait == ETIMEDOUT && demux_unstick_ao_loop (&stream->s) && ++max_iterations > 4) {
      xine_log (stream->s.xine,
	  XINE_LOG_MSG,_("Stuck in _x_demux_control_headers_done(). Taking the emergency exit\n"));
      stream->emergency_brake = 1;
      break;
    }
  }

  _x_action_lower (&stream->s);

  lprintf ("headers processed.\n");

  pthread_mutex_unlock (&stream->counter.lock);
}

void _x_demux_control_start (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  buf_element_t *bufa, *bufv;
  uint32_t flags, id_flag;

  id_flag = stream->id_flag;
  stream = stream->side_streams[0];

  /* if an _other_ side stream already sent this, skip our duplicate.
   * the _same_ side is supposed to know what it is doing (input_vdr). */
  pthread_mutex_lock (&stream->demux.pair);
  if (stream->demux.start_buffers_sent & (~id_flag)) {
    pthread_mutex_unlock (&stream->demux.pair);
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
      "demux: stream %p: skipping duplicate start buffers.\n", (void *)stream);
    return;
  }
  pthread_mutex_unlock (&stream->demux.pair);

  flags = (stream->gapless_switch || stream->finished_naturally) ? BUF_FLAG_GAPLESS_SW : 0;

  bufv = stream->s.video_fifo->buffer_pool_alloc (stream->s.video_fifo);
  bufa = stream->s.audio_fifo->buffer_pool_alloc (stream->s.audio_fifo);

  pthread_mutex_lock (&stream->demux.pair);

  bufv->type = BUF_CONTROL_START;
  bufv->decoder_flags = flags;
  stream->s.video_fifo->put (stream->s.video_fifo, bufv);

  bufa->type = BUF_CONTROL_START;
  bufa->decoder_flags = flags;
  stream->s.audio_fifo->put (stream->s.audio_fifo, bufa);

  stream->demux.start_buffers_sent |= id_flag;

  pthread_mutex_unlock (&stream->demux.pair);
}

void _x_demux_control_end (xine_stream_t *s, uint32_t flags) {

  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  buf_element_t *bufa, *bufv;

  stream = stream->side_streams[0];

  bufv = stream->s.video_fifo->buffer_pool_alloc (stream->s.video_fifo);
  bufa = stream->s.audio_fifo->buffer_pool_alloc (stream->s.audio_fifo);

  pthread_mutex_lock (&stream->demux.pair);

  bufv->type = BUF_CONTROL_END;
  bufv->decoder_flags = flags;
  stream->s.video_fifo->put (stream->s.video_fifo, bufv);

  bufa->type = BUF_CONTROL_END;
  bufa->decoder_flags = flags;
  stream->s.audio_fifo->put (stream->s.audio_fifo, bufa);

  pthread_mutex_unlock (&stream->demux.pair);
}

void _x_demux_control_nop (xine_stream_t *s, uint32_t flags ) {

  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  buf_element_t *bufa, *bufv;

  stream = stream->side_streams[0];

  bufv = stream->s.video_fifo->buffer_pool_alloc (stream->s.video_fifo);
  bufa = stream->s.audio_fifo->buffer_pool_alloc (stream->s.audio_fifo);

  pthread_mutex_lock (&stream->demux.pair);

  bufv->type = BUF_CONTROL_NOP;
  bufv->decoder_flags = flags;
  stream->s.video_fifo->put (stream->s.video_fifo, bufv);

  bufa->type = BUF_CONTROL_NOP;
  bufa->decoder_flags = flags;
  stream->s.audio_fifo->put (stream->s.audio_fifo, bufa);

  pthread_mutex_unlock (&stream->demux.pair);
}

static void *demux_loop (void *stream_gen) {
  xine_stream_private_t *stream = (xine_stream_private_t *)stream_gen;
  xine_stream_private_t *m = stream->side_streams[0];
  int status;
  int non_user;
  int iterations = 0, seeks = 0;

  struct timespec seek_time = {0, 0};

  lprintf ("loop starting...\n");

  xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
    "demux: starting stream %p.\n", (void *)stream);
  pthread_mutex_lock (&m->counter.lock);
  m->counter.demuxers_running++;
  pthread_mutex_unlock (&m->counter.lock);

  pthread_mutex_lock (&stream->demux.lock);
  m->emergency_brake = 0;

  /* do-while needed to seek after demux finished */
  do {
    uint32_t input_caps = 0;

    xine_gettime (&seek_time);

    /* tell xine_play_internal () whether we can seek.
     * so it may flush fifos early, and suspend us faster. */
    if (stream->s.input_plugin)
      input_caps = stream->s.input_plugin->get_capabilities (stream->s.input_plugin);
    pthread_mutex_lock (&stream->demux.action_lock);
    stream->demux.input_caps = input_caps;
    pthread_mutex_unlock (&stream->demux.action_lock);

    /* main demuxer loop */
    status = stream->demux.plugin->get_status (stream->demux.plugin);
    while (status == DEMUX_OK && stream->demux.thread_running && !m->emergency_brake) {

      iterations++;
      status = stream->demux.plugin->send_chunk (stream->demux.plugin);

      if (!(iterations & 31)) {
        uint32_t new_caps = stream->s.input_plugin
                          ? stream->s.input_plugin->get_capabilities (stream->s.input_plugin)
                          : 0;
        if (new_caps != input_caps) {
          input_caps = new_caps;
          pthread_mutex_lock (&stream->demux.action_lock);
          stream->demux.input_caps = input_caps;
          pthread_mutex_unlock (&stream->demux.action_lock);
        }
      }

      /* someone may want to interrupt us */
      if (stream->demux.action_pending > 0) {
        pthread_mutex_lock (&stream->demux.action_lock);
        if (stream->demux.action_pending > 0) {
          if (stream->s.input_plugin)
            stream->demux.input_caps = stream->s.input_plugin->get_capabilities (stream->s.input_plugin);
          pthread_mutex_unlock (&stream->demux.lock);
          do {
            pthread_cond_wait (&stream->demux.resume, &stream->demux.action_lock);
          } while (stream->demux.action_pending > 0);
          pthread_mutex_unlock (&stream->demux.action_lock);
          pthread_mutex_lock (&stream->demux.lock);
          xine_gettime (&seek_time);
          seeks++;
        } else {
          pthread_mutex_unlock (&stream->demux.action_lock);
        }
      }
    }

    lprintf ("main demuxer loop finished (status: %d)\n", status);

    /* let demux plugin do some needed cleanup */
    if (stream->demux.plugin->get_capabilities (stream->demux.plugin) & DEMUX_CAP_STOP)
      stream->demux.plugin->get_optional_data (stream->demux.plugin, NULL, DEMUX_OPTIONAL_DATA_STOP);

    /* tell to the net_buf_ctrl that we are at the end of the stream
     * then the net_buf_ctrl will not pause
     */
    _x_demux_control_nop (&stream->s, BUF_FLAG_END_STREAM);

    /* wait before sending end buffers: user might want to do a new seek */
    while(stream->demux.thread_running &&
          ((stream->s.video_fifo->size (stream->s.video_fifo)) ||
           (stream->s.audio_fifo->size (stream->s.audio_fifo))) &&
          status == DEMUX_FINISHED && !m->emergency_brake){
      struct timespec ts = {0, 0};
      xine_gettime (&ts);
      ts.tv_nsec += 100000000;
      if (ts.tv_nsec >= 1000000000) {
        ts.tv_nsec -= 1000000000;
        ts.tv_sec  += 1;
      }
      pthread_cond_timedwait (&stream->demux.resume, &stream->demux.lock, &ts);
      status = stream->demux.plugin->get_status (stream->demux.plugin);
    }

    if (stream->demux.thread_running && (status == DEMUX_FINISHED)) do {
      struct timespec ts = {0, 0};
      if (stream->delay_finish_event > 0) {
        /* delay sending finished event - used for image presentations */
        xine_gettime (&ts);
        ts.tv_sec  +=  stream->delay_finish_event / 10;
        ts.tv_nsec += (stream->delay_finish_event % 10) * 100000000;
        if (ts.tv_nsec >= 1000000000) {
          ts.tv_nsec -= 1000000000;
          ts.tv_sec  += 1;
        }
        stream->delay_finish_event = 0;
      } else if (stream->delay_finish_event < 0) {
        /* infinitely delay sending finished event - used for image presentations */
        stream->delay_finish_event = 0;
        xine_gettime (&ts);
        do {
          ts.tv_sec += 1;
          pthread_cond_timedwait (&stream->demux.resume, &stream->demux.lock, &ts);
          status = stream->demux.plugin->get_status (stream->demux.plugin);
        } while (stream->demux.thread_running && (status == DEMUX_FINISHED));
        break;
      } else {
        /* there may be no first frame at all here.
         * make sure xine_play returns first. */
        pthread_mutex_lock (&m->first_frame.lock);
        if (m->first_frame.flag) {
          m->first_frame.flag = 0;
          pthread_cond_broadcast (&m->first_frame.reached);
          pthread_mutex_unlock (&m->first_frame.lock);
          xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
            "demux: unblocked xine_play_internal ().\n");
        } else {
          pthread_mutex_unlock (&m->first_frame.lock);
        }
        /* stream end may well happen during xine_play () (seek close to end).
         * Lets not confuse frontend, and delay that message a bit. */
        ts = seek_time;
        ts.tv_nsec += 300000000;
        if (ts.tv_nsec >= 1000000000) {
          ts.tv_nsec -= 1000000000;
          ts.tv_sec  += 1;
        }
        xine_gettime (&seek_time);
        if (seek_time.tv_sec > ts.tv_sec)
          break;
        if ((seek_time.tv_sec == ts.tv_sec) && (seek_time.tv_nsec >= ts.tv_nsec))
          break;
        xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
          "demux: very short seek segment, delaying finish message.\n");
      }
      do {
        int e = pthread_cond_timedwait (&stream->demux.resume, &stream->demux.lock, &ts);
        status = stream->demux.plugin->get_status (stream->demux.plugin);
        if (e == ETIMEDOUT)
          break;
      } while (stream->demux.thread_running && (status == DEMUX_FINISHED));
    } while (0);

  } while (status == DEMUX_OK && stream->demux.thread_running && !m->emergency_brake);

  lprintf ("loop finished (status: %d)\n", status);

  /* demux.thread_running is zero if demux loop has been stopped by user */
  non_user = stream->demux.thread_running;
  stream->demux.thread_running = 0;


  /* do stream end stuff only if this is the last side stream. */
  {
    unsigned int n;

    pthread_mutex_lock (&m->counter.lock);
    n = --(m->counter.demuxers_running);
    if (n == 0) {

      int finisheds_audio = 0;
      int finisheds_video = 0;

      if (m->audio_thread_created)
        finisheds_audio = m->counter.finisheds_audio + 1;
      if (m->video_thread_created)
        finisheds_video = m->counter.finisheds_video + 1;
      pthread_mutex_unlock (&m->counter.lock);

      _x_demux_control_end (&m->s, non_user);
      lprintf ("loop finished, end buffer sent\n");
      pthread_mutex_unlock (&stream->demux.lock);

      pthread_mutex_lock (&m->counter.lock);
      n = 0;
      while ((m->counter.finisheds_audio < finisheds_audio) ||
             (m->counter.finisheds_video < finisheds_video)) {
        int ret_wait;
        struct timespec ts = {0, 0};
        lprintf ("waiting for finisheds.\n");
        xine_gettime (&ts);
        ts.tv_sec += 1;
        ret_wait = pthread_cond_timedwait (&m->counter.changed, &m->counter.lock, &ts);
        if (ret_wait == ETIMEDOUT && demux_unstick_ao_loop (&m->s) && ++n > 4) {
          xine_log (m->s.xine, XINE_LOG_MSG,_("Stuck in demux_loop(). Taking the emergency exit\n"));
          m->emergency_brake = 1;
          break;
        }
      }
      pthread_mutex_unlock (&m->counter.lock);

      _x_handle_stream_end (&m->s, non_user);
      xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
        "demux: %s last stream %p after %d iterations and %d seeks.\n",
        non_user ? "finished" : "stopped", (void *)stream, iterations, seeks);

    } else {

      pthread_mutex_unlock (&m->counter.lock);
      pthread_mutex_unlock (&stream->demux.lock);
      xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
        "demux: %s stream %p after %d iterations and %d seeks.\n",
        non_user ? "finished" : "stopped", (void *)stream, iterations, seeks);

    }
  }

  return NULL;
}

int _x_demux_called_from (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  if (!stream->demux.thread_running)
    return 0;
  return pthread_equal (pthread_self (), stream->demux.thread);
}

int _x_demux_start_thread (xine_stream_t *s) {

  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  int err;

  lprintf ("start thread called\n");

  _x_action_raise (&stream->s);
  pthread_mutex_lock (&stream->demux.lock);
  _x_action_lower (&stream->s);

  if (!stream->demux.thread_running) {

    if (stream->demux.thread_created) {
      void *p;
      pthread_join (stream->demux.thread, &p);
    }

    stream->demux.thread_running = 1;
    stream->demux.thread_created = 1;
    if ((err = pthread_create (&stream->demux.thread,
			       NULL, demux_loop, (void *)stream)) != 0) {
      xprintf (stream->s.xine, XINE_VERBOSITY_LOG,
               "demux: can't create new thread (%s)\n", strerror(err));
      stream->demux.thread_running = 0;
      stream->demux.thread_created = 0;
      return -1;
    }
  }

  pthread_mutex_unlock (&stream->demux.lock);
  return 0;
}

int _x_demux_stop_thread (xine_stream_t *s) {

  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  void *p;

  lprintf ("stop thread called\n");

  _x_action_raise (&stream->s);
  pthread_mutex_lock (&stream->demux.lock);
  stream->demux.thread_running = 0;
  _x_action_lower (&stream->s);

  /* At that point, the demuxer has sent the last audio/video buffer,
   * so it's a safe place to flush the engine.
   */
  _x_demux_flush_engine (&stream->s);
  pthread_mutex_unlock (&stream->demux.lock);

  lprintf ("joining thread\n" );

  if (stream->demux.thread_created) {
    pthread_join (stream->demux.thread, &p);
    stream->demux.thread_created = 0;
  }

  /*
   * Wake up xine_play if it's waiting for a frame
   */
  {
    xine_stream_private_t *m = stream->side_streams[0];
    pthread_mutex_lock (&m->first_frame.lock);
    if (m->first_frame.flag) {
      m->first_frame.flag = 0;
      pthread_cond_broadcast (&m->first_frame.reached);
    }
    pthread_mutex_unlock (&m->first_frame.lock);
  }

  return 0;
}

int _x_demux_read_header (input_plugin_t *input, void *buffer, off_t size) {
  int want_size = size;
  uint32_t caps;

  if (!input || !buffer || (want_size <= 0))
    return 0;

  caps = input->get_capabilities (input);

  if ((caps & INPUT_CAP_SIZED_PREVIEW) && (want_size >= (int)sizeof (want_size))) {
    memcpy (buffer, &want_size, sizeof (want_size));
    return input->get_optional_data (input, buffer, INPUT_OPTIONAL_DATA_SIZED_PREVIEW);
  }

  if (caps & INPUT_CAP_SEEKABLE) {
    if (input->seek (input, 0, SEEK_SET) != 0)
      return 0;
    want_size = input->read (input, buffer, want_size);
    if (input->seek (input, 0, SEEK_SET) != 0)
      return 0; /* no point to continue any further */
    return want_size;
  }

  if (caps & INPUT_CAP_PREVIEW) {
    if (want_size < MAX_PREVIEW_SIZE) {
      int read_size;
      uint8_t *temp = malloc (MAX_PREVIEW_SIZE);
      if (!temp)
        return 0;
      read_size = input->get_optional_data (input, temp, INPUT_OPTIONAL_DATA_PREVIEW);
      if (read_size <= 0) {
        free (temp);
        return 0;
      }
      if (read_size < want_size)
        want_size = read_size;
      memcpy (buffer, temp, want_size);
      free (temp);
      return want_size;
    }
    return input->get_optional_data (input, buffer, INPUT_OPTIONAL_DATA_PREVIEW);
  }

  return 0;
}

int _x_demux_check_extension (const char *mrl, const char *extensions){
  char *last_dot, *e, *ext_copy, *ext_work;
  int found = 0;

  /* An empty extensions string means that the by-extension method can't
     be used, so consider those cases as always passing. */
  if ( extensions == NULL ) return 1;

  ext_copy = strdup(extensions);
  ext_work = ext_copy;

  last_dot = strrchr (mrl, '.');
  if (last_dot) {
    last_dot++;
  }

  while ( ( e = xine_strsep(&ext_work, " ")) != NULL ) {
    if ( strstr(e, ":/") ) {
      if ( strncasecmp (mrl, e, strlen (e)) == 0 ) {
	found = 1;
	break;
      }
    } else if (last_dot) {
      if (strcasecmp (last_dot, e) == 0) {
	found = 1;
	break;
      }
    }
  }
  free(ext_copy);
  return found;
}


/*
 * read from socket/file descriptor checking demux.action_pending
 *
 * network input plugins should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if no data is available and demux.action_pending is set
 */
off_t _x_read_abort (xine_stream_t *stream, int fd, char *buf, off_t todo) {
  size_t have, left;

  if (todo <= 0)
    return 0;
  left = todo;
  have = 0;

  while (left) {

    while (1) {
      fd_set rset;
      struct timeval timeout;

      FD_ZERO (&rset);
      FD_SET  (fd, &rset);
      timeout.tv_sec  = 0;
      timeout.tv_usec = 50000;

      if (select (fd + 1, &rset, NULL, NULL, &timeout) > 0)
        break;
      /* aborts current read if action pending. otherwise xine
       * cannot be stopped when no more data is available. */
      if (_x_action_pending (stream))
        return have;
    }

    {
      ssize_t r;
#ifndef WIN32
      r = read (fd, buf + have, left);
      if (r <= 0) {
        if (r == 0) /* EOF */
          break;
        if (errno == EAGAIN)
          continue;
        perror ("_x_read_abort");
        return r;
      }
#else
      r = recv (fd, buf + have, left, 0);
      if (r <= 0) {
        perror ("_x_read_abort");
        return r;
      }
#endif
      have += r;
      left -= r;
    }
  }

  return have;
}

int _x_action_pending (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  int a;
  if (!stream)
    return 0;
  a = stream->demux.action_pending & 0xffff;
  if (a) {
    /* On seek, xine_play_internal () sets this, waits for demux to stop,
     * grabs demux lock, resets this again, performs the seek, and finally
     * unlocks demux. Due to per processor core L1 data caches, demux may
     * still see this set for some time, and abort input for no real reason.
     * Avoid that trap by checking again with lock here. */
    pthread_mutex_lock (&stream->demux.action_lock);
    a = stream->demux.action_pending & 0xffff;
    pthread_mutex_unlock (&stream->demux.action_lock);
  }
  return a;
}

/* set demux.action_pending in a thread-safe way */
void _x_action_raise (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  pthread_mutex_lock (&stream->demux.action_lock);
  stream->demux.action_pending += 0x10001;
  pthread_mutex_unlock (&stream->demux.action_lock);
}

/* reset demux.action_pending in a thread-safe way */
void _x_action_lower (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  pthread_mutex_lock (&stream->demux.action_lock);
  stream->demux.action_pending -= 0x10001;
  if (stream->demux.action_pending <= 0)
    pthread_cond_signal (&stream->demux.resume);
  pthread_mutex_unlock (&stream->demux.action_lock);
}

/*
 * demuxer helper function to send data to fifo, breaking into smaller
 * pieces (bufs) as needed.
 *
 * it has quite some parameters, but only the first 6 are needed.
 *
 * the other ones help enforcing that demuxers provide the information
 * they have about the stream, so things like the GUI slider bar can
 * work as expected.
 */
void _x_demux_send_data(fifo_buffer_t *fifo, uint8_t *data, int size,
                        int64_t pts, uint32_t type, uint32_t decoder_flags,
                        int input_normpos,
                        int input_time, int total_time,
                        uint32_t frame_number) {
  buf_element_t *buf;

  decoder_flags |= BUF_FLAG_FRAME_START;

  _x_assert(size > 0);
  while (fifo && size > 0) {

    buf = fifo->buffer_pool_size_alloc (fifo, size);

    if ( size > buf->max_size ) {
      buf->size          = buf->max_size;
      buf->decoder_flags = decoder_flags;
    } else {
      buf->size          = size;
      buf->decoder_flags = BUF_FLAG_FRAME_END | decoder_flags;
    }
    decoder_flags &= ~BUF_FLAG_FRAME_START;

    xine_fast_memcpy (buf->content, data, buf->size);
    data += buf->size;
    size -= buf->size;

    buf->pts = pts;
    pts = 0;

    buf->extra_info->input_normpos = input_normpos;
    buf->extra_info->input_time    = input_time;
    buf->extra_info->total_time    = total_time;
    buf->extra_info->frame_number  = frame_number;

    buf->type                      = type;

    fifo->put (fifo, buf);
  }
}

/*
 * Analogous to above, but reads data from input plugin
 *
 * If reading fails, -1 is returned
 */
int _x_demux_read_send_data(fifo_buffer_t *fifo, input_plugin_t *input,
                            int size, int64_t pts, uint32_t type,
                            uint32_t decoder_flags, off_t input_normpos,
                            int input_time, int total_time,
                            uint32_t frame_number) {
  buf_element_t *buf;

  decoder_flags |= BUF_FLAG_FRAME_START;

  _x_assert(size > 0);
  while (fifo && size > 0) {

    buf = fifo->buffer_pool_size_alloc (fifo, size);

    if ( size > buf->max_size ) {
      buf->size          = buf->max_size;
      buf->decoder_flags = decoder_flags;
    } else {
      buf->size          = size;
      buf->decoder_flags = BUF_FLAG_FRAME_END | decoder_flags;
    }
    decoder_flags &= ~BUF_FLAG_FRAME_START;

    if(input->read(input, buf->content, buf->size) < buf->size) {
      buf->free_buffer(buf);
      return -1;
    }
    size -= buf->size;

    buf->pts = pts;
    pts = 0;

    buf->extra_info->input_normpos = input_normpos;
    buf->extra_info->input_time    = input_time;
    buf->extra_info->total_time    = total_time;
    buf->extra_info->frame_number  = frame_number;

    buf->type                      = type;

    fifo->put (fifo, buf);
  }

  return 0;
}

/*
 * Helper function for sending MRL reference events
 */
void _x_demux_send_mrl_reference (xine_stream_t *stream, int alternative,
				  const char *mrl, const char *title,
				  int start_time, int duration)
{
  xine_event_t event;
  union {
    xine_mrl_reference_data_ext_t *e;
    XINE_DISABLE_DEPRECATION_WARNINGS
    xine_mrl_reference_data_t *b;
    XINE_ENABLE_DEPRECATION_WARNINGS
  } data;
  const size_t mrl_len = strlen (mrl);

  if (!title)
    title = "";

  /* extended MRL reference event */

  event.stream = stream;
  event.data_length = offsetof (xine_mrl_reference_data_ext_t, mrl) +
                      mrl_len + strlen (title) + 2;
  data.e = event.data = malloc (event.data_length);

  data.e->alternative = alternative;
  data.e->start_time = start_time;
  data.e->duration = duration;
  strcpy((char *)data.e->mrl, mrl);
  strcpy((char *)data.e->mrl + mrl_len + 1, title);

  event.type = XINE_EVENT_MRL_REFERENCE_EXT;
  xine_event_send (stream, &event);

  /* plain MRL reference event */

  XINE_DISABLE_DEPRECATION_WARNINGS
  event.data_length = offsetof (xine_mrl_reference_data_t, mrl) + mrl_len + 1;
  XINE_ENABLE_DEPRECATION_WARNINGS

  /*data.b->alternative = alternative;*/
  strcpy (data.b->mrl, mrl);

  event.type = XINE_EVENT_MRL_REFERENCE;
  xine_event_send (stream, &event);

  free (data.e);
}

int _x_demux_seek (xine_stream_t *s, off_t start_pos, int start_time, int playing) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  int ret = -1;

  pthread_mutex_lock (&stream->side_streams[0]->frontend_lock);
  if (stream->demux.plugin)
    ret = stream->demux.plugin->seek (stream->demux.plugin, start_pos, start_time, playing);
  pthread_mutex_unlock (&stream->side_streams[0]->frontend_lock);

  return ret;
}

