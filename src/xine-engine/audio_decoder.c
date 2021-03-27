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
  xine_private_t *xine = (xine_private_t *)stream->s.xine;
  xine_ticket_t   *running_ticket = xine->port_ticket;
  buf_element_t   *headers_first = NULL, **headers_add = &headers_first, *headers_replay = NULL;
  int              running = 1;
  int              prof_audio_decode = -1;
  uint32_t         buftype_unknown = 0;
  int              audio_channel_user = stream->audio_channel_user;
  int              headers_num = 0;
  /* generic bitrate estimation. */
  int64_t          audio_br_lasttime = 0;
  uint32_t         audio_br_lastsize = 0;
  uint32_t         audio_br_time     = 1;
  uint32_t         audio_br_bytes    = 0;
  int              audio_br_num      = 20;
  int              audio_br_value    = 0;
  /* list of seen audio channels, sorted by number.
   * audio_track_map[foo] & 0xff000000 is always BUF_AUDIO_BASE,
   * and bit 31 may serve as an end marker. */
#define AUDIO_TRACK_MAP_MAX 50
#define AUDIO_TRACK_MAP_MASK 0x8000ffff
#define AUDIO_TRACK_MAP_END 0x80000000
  uint32_t         audio_track_map[AUDIO_TRACK_MAP_MAX + 1];
#define BUFTYPE_BASE(type) ((type) >> 24)
#define BUFTYPE_SUB(type)  (((type) & 0x00ff0000) >> 16)

  if (prof_audio_decode == -1)
    prof_audio_decode = xine_profiler_allocate_slot ("audio decoder/output");

  audio_track_map[0] = AUDIO_TRACK_MAP_END;

  running_ticket->acquire (running_ticket, 0);

  while (running) {
    int handled, ignore;
    buf_element_t *buf;

    lprintf ("audio_loop: waiting for package...\n");

    buf = headers_replay;
    if (!buf)
      buf = stream->s.audio_fifo->tget (stream->s.audio_fifo, running_ticket);

    lprintf ("audio_loop: got package pts = %"PRId64", type = %08x\n", buf->pts, buf->type);

    _x_extra_info_merge( stream->audio_decoder_extra_info, buf->extra_info );
    stream->audio_decoder_extra_info->seek_count = stream->video_seek_count;

    switch (BUFTYPE_BASE (buf->type)) {

      case BUFTYPE_BASE (BUF_AUDIO_BASE):

        if ((buf->type & 0xffff0000) == BUF_AUDIO_UNKNOWN)
          break;
        xine_rwlock_rdlock (&stream->info_lock);
        handled = stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED];
        ignore  = stream->stream_info[XINE_STREAM_INFO_IGNORE_AUDIO];
        xine_rwlock_unlock (&stream->info_lock);
        (void)handled; /* dont optimize away the read. */
        if (ignore)
          break;
        xine_profiler_start_count (prof_audio_decode);

        /* running_ticket->acquire (running_ticket, 0); */

        {
          uint32_t audio_type = 0;
          int      i;
          uint32_t chan;
          /* printf ("audio_decoder: buf_type=%08x auto=%08x user=%08x\n",
               buf->type, stream->audio_channel_auto, audio_channel_user); */

          /* update track map */
          chan = buf->type & 0x0000ffff;
          i = 0;
          while ((audio_track_map[i] & AUDIO_TRACK_MAP_MASK) < chan)
            i++;
          if ((audio_track_map[i] & AUDIO_TRACK_MAP_MASK) != chan) {
            xine_event_t  ui_event;
            int j = stream->audio_track_map_entries;
            if (j >= AUDIO_TRACK_MAP_MAX) {
              xine_profiler_stop_count (prof_audio_decode);
              break;
            }
            while (j >= i) {
              audio_track_map[j + 1] = audio_track_map[j];
              j--;
            }
            audio_track_map[i] = buf->type;
            stream->audio_track_map_entries++;
            /* implicit channel change - reopen decoder below */
            if ((i == 0) && (audio_channel_user == -1) && (stream->s.audio_channel_auto < 0))
              stream->audio_decoder_streamtype = -1;
            ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
            ui_event.data_length = 0;
            xine_event_send (&stream->s, &ui_event);
          }

          /* find out which audio type to decode */
          lprintf ("audio_channel_user = %d, map[0]=%08x\n", audio_channel_user, audio_track_map[0]);
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
                audio_type = audio_track_map[0];
            } else {
              if (audio_channel_user <= stream->audio_track_map_entries)
                audio_type = audio_track_map[audio_channel_user];
              else
                audio_type = -1;
            }

            /* now, decode stream buffer if it's the right audio type */
            if (buf->type == audio_type) {

              int streamtype = (buf->type>>16) & 0xFF;
              /* close old decoder of audio type has changed */
              if (buf->type != buftype_unknown &&
                (stream->audio_decoder_streamtype != streamtype ||
                !stream->audio_decoder_plugin)) {
                if (stream->audio_decoder_plugin) {
                  _x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
                }
                stream->audio_decoder_streamtype = streamtype;
                stream->audio_decoder_plugin = _x_get_audio_decoder (&stream->s, streamtype);
                handled = (stream->audio_decoder_plugin != NULL);
                xine_rwlock_wrlock (&stream->info_lock);
                stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED] = handled;
                xine_rwlock_unlock (&stream->info_lock);
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

              /* audio_br_add. some decoders reset buf->pts, do this first. */
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
                      br = xine_uint_mul_div (audio_br_bytes, 90000 * 8, audio_br_time);
                      bdiff = br - audio_br_value;
                      if (bdiff < 0)
                        bdiff = -bdiff;
                      if (bdiff > (br >> 6)) {
                        audio_br_value = br;
                        xine_rwlock_wrlock (&stream->info_lock);
                        stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE] = br;
                        xine_rwlock_unlock (&stream->info_lock);
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

              /* finally - decode data */
              if (stream->audio_decoder_plugin)
                stream->audio_decoder_plugin->decode_data (stream->audio_decoder_plugin, buf);

              /* no need to lock again. it may have been reset from this thread inside
               * audio_decoder_plugin->decode_data (), if at all.
               * XXX: should we try a different decoder then? */
              handled = stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED];
              if (!handled && (buf->type != buftype_unknown)) {
                const char *aname = _x_buf_audio_name (buf->type);

                xine_log (stream->s.xine, XINE_LOG_MSG,
                  _("audio_decoder: no plugin available to handle '%s'\n"), aname);
                if (!_x_meta_info_get (&stream->s, XINE_META_INFO_AUDIOCODEC))
                  _x_meta_info_set_utf8 (&stream->s, XINE_META_INFO_AUDIOCODEC, aname);
                buftype_unknown = buf->type;
                /* fatal error - dispose plugin */
                if (stream->audio_decoder_plugin) {
                  _x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
                  stream->audio_decoder_plugin = NULL;
                }
              }
            }
          }
        }
        /* if (running_ticket->ticket_revoked)
         *   running_ticket->renew (running_ticket, 0);
         * running_ticket->release (running_ticket, 0);
         */
        xine_profiler_stop_count (prof_audio_decode);
        break;

      case BUFTYPE_BASE (BUF_CONTROL_BASE):

        switch (BUFTYPE_SUB (buf->type)) {
          int t;

          case BUFTYPE_SUB (BUF_CONTROL_HEADERS_DONE):
            pthread_mutex_lock (&stream->counter.lock);
            stream->counter.headers_audio++;
            if (stream->video_thread_created) {
              /* avoid useless wakes on an incomplete pair */
              if (stream->counter.headers_audio <= stream->counter.headers_video)
                pthread_cond_broadcast (&stream->counter.changed);
            } else {
              pthread_cond_broadcast (&stream->counter.changed);
            }
            pthread_mutex_unlock (&stream->counter.lock);
            break;

          case BUFTYPE_SUB (BUF_CONTROL_START):
            lprintf ("start\n");
            /* decoder dispose might call port functions */
            /* running_ticket->acquire(running_ticket, 0); */
            if (stream->audio_decoder_plugin) {
              lprintf ("close old decoder\n");
              stream->keep_ao_driver_open = !!(buf->decoder_flags & BUF_FLAG_GAPLESS_SW);
              _x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
              stream->audio_decoder_plugin = NULL;
              stream->audio_type = 0;
              stream->keep_ao_driver_open = 0;
            }
            /* running_ticket->release(running_ticket, 0); */
            audio_track_map[0] = AUDIO_TRACK_MAP_END;
            stream->audio_track_map_entries = 0;
            if (!(buf->decoder_flags & BUF_FLAG_GAPLESS_SW)) {
              running_ticket->release (running_ticket, 0);
              stream->s.metronom->handle_audio_discontinuity (stream->s.metronom, DISC_STREAMSTART, 0);
              running_ticket->acquire (running_ticket, 0);
            }
            buftype_unknown = 0;
            break;

          case BUFTYPE_SUB (BUF_CONTROL_END):
            /* free all held header buffers, see comments below */
            _x_free_buf_elements (headers_first);
            headers_first  = NULL;
            headers_add    = &headers_first;
            headers_replay = NULL;
            headers_num    = 0;
            /* wait the output fifos to run dry before sending the notification event
             * to the frontend. this test is only valid if there is only a single
             * stream attached to the current output port. */
            while (1) {
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
            pthread_mutex_lock (&stream->counter.lock);
            stream->counter.finisheds_audio++;
            lprintf ("reached end marker # %d\n", stream->counter.finisheds_audio);
            if (stream->video_thread_created) {
              if (stream->counter.finisheds_audio > stream->counter.finisheds_video) {
                do {
                  struct timespec ts = {0, 0};
                  xine_gettime (&ts);
                  ts.tv_sec += 1;
                  /* use timedwait to workaround buggy pthread broadcast implementations */
                  pthread_cond_timedwait (&stream->counter.changed, &stream->counter.lock, &ts);
                } while (stream->counter.finisheds_audio > stream->counter.finisheds_video);
              } else if (stream->counter.finisheds_audio == stream->counter.finisheds_video) {
                pthread_cond_broadcast (&stream->counter.changed);
              }
            } else {
              pthread_cond_broadcast (&stream->counter.changed);
            }
            pthread_mutex_unlock (&stream->counter.lock);
            stream->s.audio_channel_auto = -1;
            running_ticket->acquire (running_ticket, 0);
            break;

          case BUFTYPE_SUB (BUF_CONTROL_QUIT):
            /* decoder dispose might call port functions */
            /* running_ticket->acquire(running_ticket, 0); */
            if (stream->audio_decoder_plugin) {
              _x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
              stream->audio_decoder_plugin = NULL;
              stream->audio_type = 0;
            }
            /* running_ticket->release(running_ticket, 0); */
            audio_track_map[0] = AUDIO_TRACK_MAP_END;
            stream->audio_track_map_entries = 0;
            running = 0;
            break;

          case BUFTYPE_SUB (BUF_CONTROL_NOP):
            break;

          case BUFTYPE_SUB (BUF_CONTROL_RESET_DECODER):
            lprintf ("reset\n");
            _x_extra_info_reset (stream->audio_decoder_extra_info);
            if (stream->audio_decoder_plugin) {
              /* running_ticket->acquire(running_ticket, 0); */
              stream->audio_decoder_plugin->reset (stream->audio_decoder_plugin);
              /* running_ticket->release(running_ticket, 0); */
            }
            break;

          case BUFTYPE_SUB (BUF_CONTROL_DISCONTINUITY):
            t = DISC_RELATIVE;
            goto handle_disc;

          case BUFTYPE_SUB (BUF_CONTROL_NEWPTS):
            t = (buf->decoder_flags & BUF_FLAG_SEEK) ? DISC_STREAMSEEK : DISC_ABSOLUTE;
          handle_disc:
            if (stream->audio_decoder_plugin) {
              /* running_ticket->acquire(running_ticket, 0); */
              stream->audio_decoder_plugin->discontinuity (stream->audio_decoder_plugin);
              /* running_ticket->release(running_ticket, 0); */
            }
            running_ticket->release (running_ticket, 0);
            stream->s.metronom->handle_audio_discontinuity (stream->s.metronom, t, buf->disc_off);
            running_ticket->acquire (running_ticket, 0);
            /* audio_br_discontinuity */
            audio_br_lasttime = 0;
            audio_br_lastsize = 0;
            break;

          case BUFTYPE_SUB (BUF_CONTROL_AUDIO_CHANNEL):
            xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
              "audio_decoder: suggested switching to stream_id %02x\n", buf->decoder_info[0]);
            stream->s.audio_channel_auto = buf->decoder_info[0] & 0xff;
            break;

          case BUFTYPE_SUB (BUF_CONTROL_RESET_TRACK_MAP):
            if (stream->audio_track_map_entries) {
              xine_event_t ui_event;
              audio_track_map[0] = AUDIO_TRACK_MAP_END;
              stream->audio_track_map_entries = 0;
              ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
              ui_event.data_length = 0;
              xine_event_send (&stream->s, &ui_event);
            }
            break;

          default:
            if (buf->type != buftype_unknown) {
              xine_log (stream->s.xine, XINE_LOG_MSG,
              _("audio_decoder: error, unknown buffer type: %08x\n"), buf->type);
              buftype_unknown = buf->type;
            }

        } /* case BUFTYPE_BASE (BUF_CONTROL_BASE) */
        break;

      default:
        if (buf->type != buftype_unknown) {
          xine_log (stream->s.xine, XINE_LOG_MSG,
            _("audio_decoder: error, unknown buffer type: %08x\n"), buf->type);
          buftype_unknown = buf->type;
        }

    } /* switch (BUFTYPE_BASE (buf->type)) */

    /* some decoders require a full reinitialization when audio
     * channel is changed (rate might be change and even a
     * different codec may be used).
     *
     * we must close the old decoder and process all the headers
     * again, since they are needed for decoder initialization.
     */
    if (headers_replay) {
      headers_replay = headers_replay->next;
    } else {
      if (audio_channel_user != stream->audio_channel_user) {
        audio_channel_user = stream->audio_channel_user;
        if (stream->audio_decoder_plugin) {
          /* decoder dispose might call port functions */
          /* running_ticket->acquire (running_ticket, 0); */
          _x_free_audio_decoder (&stream->s, stream->audio_decoder_plugin);
          /* running_ticket->release (running_ticket, 0); */
          stream->audio_decoder_plugin = NULL;
          audio_track_map[0] = AUDIO_TRACK_MAP_END;
          stream->audio_track_map_entries = 0;
          stream->audio_type = 0;
        }
        buf->free_buffer (buf);
        headers_replay = headers_first;
        xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
          "audio_decoder: replaying %d headers.\n", headers_num);
      } else {
        /* header buffers are never freed. instead they
         * are added to a list to allow replaying them
         * in case of a channel change. */
        if (buf->decoder_flags & BUF_FLAG_HEADER) {
          /* drop outdated headers. */
          int num = 0;
          buf_element_t *here = headers_first, **add = &headers_first;
          while (here) {
            buf_element_t *next = here->next;
            uint32_t d = here->type ^ buf->type;
            if (((d & 0x0000ffff) == 0) &&
              (((d & 0xffff0000) != 0) || (here->decoder_flags == buf->decoder_flags))) {
              *add = next;
              here->next = NULL;
              here->free_buffer (here);
              headers_num--;
              num++;
            } else {
              add = &here->next;
            }
            here = next;
          }
          headers_add = add;
          if (num)
            xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
              "audio_decoder: dropped %d outdated headers for track #%u.\n",
              num, (unsigned int)(buf->type & 0x0000ffff));
          *headers_add = buf;
          headers_add  = &buf->next;
          buf->next = NULL;
          headers_num++;
        } else {
          buf->free_buffer (buf);
        }
      }
    }
  }

  running_ticket->release (running_ticket, 0);

  /* free all held header buffers */
  _x_free_buf_elements (headers_first);

  return NULL;
}

int _x_audio_decoder_init (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  if (!stream)
    return 0;
  stream = stream->side_streams[0];
  if (stream->s.audio_fifo)
    return 1;

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
     * TJ. There are live streams with 5...10s fragments. Nice clients fetch
     * 1 fragment, then wait the gap without blocking the server port all the
     * time. With 2s reserve and 48k AAC, we need up to 12*48000/1024 = 563
     * frame buffers. High bitrate video over a budget connection somewhat
     * hides the issue, but web radio turns down all excuses.
     * Lets give away a high multiple of 4 of 2k bufs instead, and LPCM can
     * still have 8k ones via buffer_pool_size_alloc ().
     */

    num_buffers = stream->s.xine->config->register_num (stream->s.xine->config,
      "engine.buffers.audio_num_buffers", 700,
      _("number of audio buffers"),
      _("The number of audio buffers (each is 2k in size) xine uses in its "
        "internal queue. Higher values mean smoother playback for unreliable "
        "inputs, but also increased latency and memory consumption."),
      20, NULL, NULL);
    num_buffers = (num_buffers + 3) & ~4;
    if (num_buffers > 2000)
      num_buffers = 2000;

    stream->s.audio_fifo = _x_fifo_buffer_new (num_buffers, 2048);
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

  if (!stream)
    return;

  if (stream->audio_thread_created) {
    /* stream->audio_fifo->clear(stream->audio_fifo); */

    buf = stream->s.audio_fifo->buffer_pool_alloc (stream->s.audio_fifo);
    buf->type = BUF_CONTROL_QUIT;
    stream->s.audio_fifo->put (stream->s.audio_fifo, buf);

    pthread_join (stream->audio_thread, &p);
    stream->audio_thread_created = 0;
  }

  if (stream->s.audio_fifo) {
    stream->s.audio_fifo->dispose (stream->s.audio_fifo);
    stream->s.audio_fifo = NULL;
  }
}

int _x_get_audio_channel (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  return stream ? (stream->audio_type & 0xFFFF) : 0;
}

