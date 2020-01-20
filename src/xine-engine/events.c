/*
 * Copyright (C) 2000-2020 the xine project
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
 * Event handling functions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include <sys/time.h>
#include <errno.h>

#include <xine/xine_internal.h>
#include "xine_private.h"

#define MAX_REUSE_EVENTS 16
#define MAX_REUSE_DATA 256

typedef struct xine_event_queue_private_s xine_event_queue_private_t;

typedef struct {
  xine_event_t e;
  xine_event_queue_private_t *queue;
} xine_event_private_t;

struct xine_event_queue_private_s {
  xine_event_queue_t  q;
  xine_list_t        *free_events;
  int                 refs;
  int                 flush;
  struct timeval      lasttime;
  int                 num_all;
  int                 num_alloc;
  int                 num_skip;
#if (HAVE_ATOMIC_VARS > 0)
  /* give the "get" functions a quick fall through
   * for the most common "no events" case.
   * this is basically noise shaping. */
  xine_refs_t         pending;
#else
  /* skip that mutex fallback. we already have a mutex. */
#endif
  pthread_t           handler;
  struct {
    xine_event_private_t e;
    uint8_t data[MAX_REUSE_DATA];
  } revents[MAX_REUSE_EVENTS];
};

xine_event_t *xine_event_get  (xine_event_queue_t *queue) {
  xine_event_queue_private_t *q = (xine_event_queue_private_t *)queue;
  xine_event_t *event;
  xine_list_iterator_t ite;

  if (!q)
    return NULL;
#if (HAVE_ATOMIC_VARS > 0)
  if (xine_refs_get (&q->pending) <= 1)
    return NULL;
#endif
  pthread_mutex_lock (&q->q.lock);
  ite = NULL;
  event = xine_list_next_value (q->q.events, &ite);
  if (ite) {
    xine_list_remove (q->q.events, ite);
#if (HAVE_ATOMIC_VARS > 0)
    if (xine_list_size (q->q.events) == 0)
      xine_refs_add (&q->pending, -1);
#endif
  }
  pthread_mutex_unlock (&q->q.lock);

  return event;
}

xine_event_t *xine_event_next (xine_event_queue_t *queue, xine_event_t *prev_event) {
  xine_event_queue_private_t *q = (xine_event_queue_private_t *)queue;

  if (!q)
    return NULL;
  if (prev_event) {
    if (PTR_IN_RANGE (prev_event, &q->revents, sizeof (q->revents))) {
      pthread_mutex_lock (&q->q.lock);
      q->refs -= 1;
      xine_list_push_back (q->free_events, prev_event);
    } else {
      free (prev_event);
      pthread_mutex_lock (&q->q.lock);
    }
  } else {
#if (HAVE_ATOMIC_VARS > 0)
    if (xine_refs_get (&q->pending) <= 1)
      return NULL;
#endif
    pthread_mutex_lock (&q->q.lock);
  }
  {
    xine_list_iterator_t ite = NULL;
    xine_event_t *event = xine_list_next_value (q->q.events, &ite);
    if (ite) {
      xine_list_remove (q->q.events, ite);
#if (HAVE_ATOMIC_VARS > 0)
      if (xine_list_size (q->q.events) == 0)
        xine_refs_add (&q->pending, -1);
#endif
    }
    pthread_mutex_unlock (&q->q.lock);
    return event;
  }
}

static xine_event_t *xine_event_wait_locked (xine_event_queue_private_t *q) {
  xine_event_t  *event, *first_event;
  xine_list_iterator_t ite, first_ite;
  int wait;

  /* wait until there is at least 1 event */
  while (1) {
    first_ite = NULL;
    first_event = xine_list_next_value (q->q.events, &first_ite);
    if (first_ite)
      break;
    pthread_cond_wait (&q->q.new_event, &q->q.lock);
  }

  /* calm down bursting progress events pt 2:
   * if list has only statistics, wait for possible update or other stuff. */
  event = first_event;
  ite = first_ite;
  wait = 1;
  do {
    if ((event->type != XINE_EVENT_PROGRESS) && (event->type != XINE_EVENT_NBC_STATS)) {
      wait = 0;
      break;
    }
    event = xine_list_next_value (q->q.events, &ite);
  } while (ite);
  if (wait) {
    struct timespec ts = {0, 0};
    xine_gettime (&ts);
    ts.tv_nsec += 50000000;
    if (ts.tv_nsec >= 1000000000) {
      ts.tv_nsec -= 1000000000;
      ts.tv_sec += 1;
    }
    pthread_cond_timedwait (&q->q.new_event, &q->q.lock, &ts);
    /* paranoia ? */
    while (1) {
      first_ite = NULL;
      first_event = xine_list_next_value (q->q.events, &first_ite);
      if (first_ite)
        break;
      pthread_cond_wait (&q->q.new_event, &q->q.lock);
    }
  }

  xine_list_remove (q->q.events, first_ite);
#if (HAVE_ATOMIC_VARS > 0)
  if (xine_list_size (q->q.events) == 0)
    xine_refs_add (&q->pending, -1);
#endif
  return first_event;
}

xine_event_t *xine_event_wait (xine_event_queue_t *queue) {
  xine_event_queue_private_t *q = (xine_event_queue_private_t *)queue;
  xine_event_t  *event;

  pthread_mutex_lock (&q->q.lock);
  event = xine_event_wait_locked (q);
  pthread_mutex_unlock (&q->q.lock);
  return event;
}

static int xine_event_queue_unref_unlock (xine_event_queue_private_t *q) {
  int refs;

  q->refs -= 1;
  refs = q->refs;
  pthread_mutex_unlock (&q->q.lock);

  if (refs == 0) {
#if (HAVE_ATOMIC_VARS > 0)
    xine_refs_sub (&q->pending, 1);
#endif
    xine_list_delete (q->q.events);
    xine_list_delete (q->free_events);
    pthread_mutex_destroy (&q->q.lock);
    pthread_cond_destroy (&q->q.new_event);
    pthread_cond_destroy (&q->q.events_processed);
    free (q);
  }

  return refs;
}

void xine_event_free (xine_event_t *event) {
  /* XXX; assuming this was returned by xine_event_get () or xine_event_wait () before. */
  xine_event_private_t *e = (xine_event_private_t *)event;
  xine_event_queue_private_t *q;

  if (!e)
    return;
  q = e->queue;
  if (!q)
    return;
  if (PTR_IN_RANGE (e, &q->revents, sizeof (q->revents))) {
    pthread_mutex_lock (&q->q.lock);
    xine_list_push_back (q->free_events, e);
    xine_event_queue_unref_unlock (q);
  } else {
    free (event);
  }
}

void xine_event_send (xine_stream_t *s, const xine_event_t *event) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s, *mstream;
  xine_list_iterator_t ite;
  xine_event_queue_private_t *q;
  void *data;
  struct timeval now = {0, 0};

  if (!stream || !event)
    return;
  mstream = stream->side_streams[0];
  data = (event->data_length <= 0) ? NULL : event->data;

  gettimeofday (&now, NULL);
  pthread_mutex_lock (&mstream->event_queues_lock);

  ite = NULL;
  while ((q = xine_list_next_value (mstream->event_queues, &ite))) {
    xine_event_private_t *new_event;

    /* XINE_EVENT_VDR_DISCONTINUITY: .data == NULL, .data_length == DISC_* */
    if (!data) {
      xine_list_iterator_t it2 = NULL;
      pthread_mutex_lock (&q->q.lock);
      new_event = xine_list_next_value (q->free_events, &it2);
      if (new_event) {
        xine_list_remove (q->free_events, it2);
        q->refs += 1;
      } else {
        new_event = malloc (sizeof (*new_event));
        if (!new_event) {
          pthread_mutex_unlock (&q->q.lock);
          continue;
        }
        q->num_alloc += 1;
      }
      new_event->e.data        = NULL;
      new_event->queue         = q;
      new_event->e.type        = event->type;
      new_event->e.data_length = event->data_length;
      new_event->e.stream      = &stream->s;
      new_event->e.tv          = now;
      q->num_all += 1;
      xine_list_push_back (q->q.events, new_event);
#if (HAVE_ATOMIC_VARS > 0)
      if (xine_list_size (q->q.events) == 1)
        xine_refs_add (&q->pending, 1);
#endif
      pthread_cond_signal (&q->q.new_event);
      pthread_mutex_unlock (&q->q.lock);
      continue;
    }

    /* calm down bursting progress events pt 1:
     * if list tail has an earlier instance, update it without signal. */
    if ((event->type == XINE_EVENT_PROGRESS) || (event->type == XINE_EVENT_NBC_STATS)) {
      xine_event_t *e2;
      pthread_mutex_lock (&q->q.lock);
      do {
        xine_list_iterator_t it2 = NULL;
        e2 = xine_list_prev_value (q->q.events, &it2);
        if (!it2)
          break;
        if (e2 && (e2->type == event->type) && e2->data)
          break;
        e2 = xine_list_prev_value (q->q.events, &it2);
        if (!it2)
          break;
        if (e2 && (e2->type == event->type) && e2->data)
          break;
        e2 = NULL;
      } while (0);
      if (e2) {
        if (event->type == XINE_EVENT_PROGRESS) {
          xine_progress_data_t *pd1 = data, *pd2 = e2->data;
          if (pd1->description && pd2->description && !strcmp (pd1->description, pd2->description)) {
            pd2->percent = pd1->percent;
            e2->tv = now;
            q->num_all += 1;
            q->num_skip += 1;
            pthread_mutex_unlock (&q->q.lock);
            continue;
          }
        } else { /* XINE_EVENT_NBC_STATS */
          size_t l = event->data_length < e2->data_length ? event->data_length : e2->data_length;
          memcpy (e2->data, data, l);
          e2->tv = now;
          q->num_all += 1;
          q->num_skip += 1;
          pthread_mutex_unlock (&q->q.lock);
          continue;
        }
      }
      pthread_mutex_unlock (&q->q.lock);
    }

    if (event->data_length <= MAX_REUSE_DATA) {
      xine_list_iterator_t it2 = NULL;
      pthread_mutex_lock (&q->q.lock);
      new_event = xine_list_next_value (q->free_events, &it2);
      if (new_event) {
        xine_list_remove (q->free_events, it2);
        q->refs += 1;
        new_event->e.data = (uint8_t *)new_event + sizeof (*new_event);
        xine_small_memcpy (new_event->e.data, data, event->data_length);
        new_event->queue         = q;
        new_event->e.type        = event->type;
        new_event->e.data_length = event->data_length;
        new_event->e.stream      = &stream->s;
        new_event->e.tv          = now;
        q->num_all += 1;
        xine_list_push_back (q->q.events, new_event);
#if (HAVE_ATOMIC_VARS > 0)
        if (xine_list_size (q->q.events) == 1)
          xine_refs_add (&q->pending, 1);
#endif
        pthread_cond_signal (&q->q.new_event);
        pthread_mutex_unlock (&q->q.lock);
        continue;
      }
      pthread_mutex_unlock (&q->q.lock);
    }

    new_event = malloc (sizeof (*new_event) + event->data_length);
    if (!new_event)
      continue;
    new_event->e.data = (uint8_t *)new_event + sizeof (*new_event);
    memcpy (new_event->e.data, data, event->data_length);
    new_event->queue         = q;
    new_event->e.type        = event->type;
    new_event->e.data_length = event->data_length;
    new_event->e.stream      = &stream->s;
    new_event->e.tv          = now;
    pthread_mutex_lock (&q->q.lock);
    q->num_all += 1;
    q->num_alloc += 1;
    xine_list_push_back (q->q.events, new_event);
#if (HAVE_ATOMIC_VARS > 0)
    if (xine_list_size (q->q.events) == 1)
      xine_refs_add (&q->pending, 1);
#endif
    pthread_cond_signal (&q->q.new_event);
    pthread_mutex_unlock (&q->q.lock);
  }

  pthread_mutex_unlock (&mstream->event_queues_lock);
}


#if (HAVE_ATOMIC_VARS > 0)
static void xine_event_dummy_cb (void *data) {
  (void)data;
}
#endif

xine_event_queue_t *xine_event_new_queue (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  xine_event_queue_private_t *q;
  uint32_t n;

  if (!stream)
    return NULL;
  q = malloc (sizeof (*q));
  if (!q)
    return NULL;

  q->refs = 1;
  q->flush = 0;
  q->lasttime.tv_sec = 0;
  q->lasttime.tv_usec = 0;
  q->num_all = 0;
  q->num_alloc = 0;
  q->num_skip = 0;

#if (HAVE_ATOMIC_VARS > 0)
  xine_refs_init (&q->pending, xine_event_dummy_cb, NULL);
#endif

  q->q.events = xine_list_new ();
  if (!q->q.events) {
    free (q);
    return NULL;
  }
  q->free_events = xine_list_new ();
  if (!q->free_events) {
    xine_list_delete (q->q.events);
    free (q);
    return NULL;
  }
  for (n = 0; n < MAX_REUSE_EVENTS; n++)
    xine_list_push_back (q->free_events, &q->revents[n]);

  xine_refs_add (&stream->refs, 1);

  pthread_mutex_init (&q->q.lock, NULL);
  pthread_cond_init (&q->q.new_event, NULL);
  pthread_cond_init (&q->q.events_processed, NULL);
  q->q.stream = &stream->s;
  q->q.listener_thread = NULL;
  q->q.callback_running = 0;

  {
    xine_stream_private_t *mstream = stream->side_streams[0];
    pthread_mutex_lock (&mstream->event_queues_lock);
    xine_list_push_back (mstream->event_queues, &q->q);
    pthread_mutex_unlock (&mstream->event_queues_lock);
  }

  return &q->q;
}

void xine_event_dispose_queue (xine_event_queue_t *queue) {
  xine_event_queue_private_t *q = (xine_event_queue_private_t *)queue;
  xine_stream_private_t *stream;

  if (!q)
    return;
  stream = (xine_stream_private_t *)q->q.stream;

  {
    xine_stream_private_t *mstream = stream->side_streams[0];
    xine_list_iterator_t  ite;
    pthread_mutex_lock (&mstream->event_queues_lock);
    ite = xine_list_find (mstream->event_queues, q);
    if (!ite) {
      pthread_mutex_unlock (&mstream->event_queues_lock);
      xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "events: tried to dispose queue which is not in list\n");
      return;
    }
    xine_list_remove (mstream->event_queues, ite);
    pthread_mutex_unlock (&mstream->event_queues_lock);
  }

  /*
   * send quit event
   */
  {
    xine_list_iterator_t ite = NULL;
    xine_event_private_t *qevent;
    pthread_mutex_lock (&q->q.lock);
    qevent = xine_list_next_value (q->free_events, &ite);
    if (ite) {
      xine_list_remove (q->free_events, ite);
      q->refs += 1;
    } else {
      qevent = malloc (sizeof (*qevent));
      q->num_alloc += 1;
    }
    if (qevent) {
      qevent->e.type        = XINE_EVENT_QUIT;
      qevent->e.stream      = &stream->s;
      qevent->e.data        = NULL;
      qevent->e.data_length = 0;
      qevent->queue         = q;
      gettimeofday (&qevent->e.tv, NULL);
      xine_list_push_back (q->q.events, qevent);
#if (HAVE_ATOMIC_VARS > 0)
      if (xine_list_size (q->q.events) == 1)
         xine_refs_add (&q->pending, 1);
#endif
      pthread_cond_signal (&q->q.new_event);
    }
    q->num_all += 1;
    pthread_mutex_unlock (&q->q.lock);
  }

  /*
   * join listener thread, if any
   */

  if (q->q.listener_thread) {
    void *p;
    pthread_join (*q->q.listener_thread, &p);
    q->q.listener_thread = NULL;
  }

  /*
   * clean up pending events, unref.
   */
  {
    xine_list_iterator_t ite = NULL;
    int num_all, num_alloc, num_skip, num_left, refs;
    pthread_mutex_lock (&q->q.lock);
    num_left = xine_list_size (q->q.events);
    while (1) {
      xine_event_t *event = xine_list_next_value (q->q.events, &ite);
      if (!ite)
        break;
      if (PTR_IN_RANGE (event, &q->revents, sizeof (q->revents))) {
        xine_list_push_back (q->free_events, event);
        q->refs -= 1;
      } else {
        free (event);
      }
    }
    xine_list_clear (q->q.events);
    q->flush = 0;
    pthread_cond_signal (&q->q.events_processed);
    num_all = q->num_all;
    num_alloc = q->num_alloc;
    num_skip = q->num_skip;
    refs = xine_event_queue_unref_unlock (q);
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
      "events: stream %p: %d total, %d allocated, %d skipped, %d left and dropped, %d refs.\n",
      (void *)stream, num_all, num_alloc, num_skip, num_left, refs);
  }

  xine_refs_sub (&stream->refs, 1);
}


static void *listener_loop (void *queue_gen) {
  xine_event_queue_private_t *q = queue_gen;
  int last_type;

  pthread_mutex_lock (&q->q.lock);

  do {
    xine_event_t *event = xine_event_wait_locked (q);
    q->lasttime = event->tv;
    q->q.callback_running = 1;
    pthread_mutex_unlock (&q->q.lock);
    last_type = event->type;

    q->q.callback (q->q.user_data, event);

    if (PTR_IN_RANGE (event, &q->revents, sizeof (q->revents))) {
      pthread_mutex_lock (&q->q.lock);
      q->refs -= 1;
      /* refs == 0 cannot heppen here. */
      xine_list_push_back (q->free_events, event);
    } else {
      free (event);
      pthread_mutex_lock (&q->q.lock);
    }
    q->q.callback_running = 0;
    if (q->flush > 0) {
      q->flush -= 1;
      if (q->flush <= 0)
        pthread_cond_signal (&q->q.events_processed);
    }
  } while (last_type != XINE_EVENT_QUIT);

  if (q->flush > 0) {
    q->flush = 0;
    pthread_cond_signal (&q->q.events_processed);
  }
  pthread_mutex_unlock (&q->q.lock);
  return NULL;
}


int xine_event_create_listener_thread (xine_event_queue_t *queue,
                                       xine_event_listener_cb_t callback,
                                       void *user_data) {
  xine_event_queue_private_t *q = (xine_event_queue_private_t *)queue;
  int err;

  _x_assert(queue != NULL);
  _x_assert(callback != NULL);

  if (q->q.listener_thread) {
    xprintf (q->q.stream->xine, XINE_VERBOSITY_NONE,
             "events: listener thread already created\n");
    return 0;
  }

  q->q.listener_thread = &q->handler;
  q->q.callback        = callback;
  q->q.user_data       = user_data;

  if ((err = pthread_create (q->q.listener_thread, NULL, listener_loop, q)) != 0) {
    xprintf (q->q.stream->xine, XINE_VERBOSITY_NONE,
             "events: can't create new thread (%s)\n", strerror(err));
    q->q.listener_thread = NULL;
    q->q.callback        = NULL;
    q->q.user_data       = NULL;
    return 0;
  }

  return 1;
}

void _x_flush_events_queues (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  pthread_t self = pthread_self ();
  struct timespec ts = {0, 0};
  struct timeval tv;

  if (!stream)
    return;
  stream = stream->side_streams[0];
  xine_gettime (&ts);
  tv.tv_sec = ts.tv_sec;
  tv.tv_usec = ts.tv_nsec / 1000;
  ts.tv_sec += 1;

  while (1) {
    xine_event_queue_private_t *q;
    xine_list_iterator_t ite = NULL;
    int list_locked = 1;

    pthread_mutex_lock (&stream->event_queues_lock);
    while ((q = xine_list_next_value (stream->event_queues, &ite))) {
      pthread_mutex_lock (&q->q.lock);
      /* never wait for self. */
      if (q->q.listener_thread && !pthread_equal (self, q->handler) && (q->flush == 0)) {
        /* count pending past events */
        if ((q->lasttime.tv_sec < tv.tv_sec) ||
            ((q->lasttime.tv_sec == tv.tv_sec) && (q->lasttime.tv_usec <= tv.tv_usec))) {
          xine_list_iterator_t ite2 = NULL;
          q->flush = q->q.callback_running ? 1 : 0;
          while (1) {
            xine_event_t *e = xine_list_next_value (q->q.events, &ite2);
            if (!ite2)
              break;
            if ((e->tv.tv_sec > tv.tv_sec) ||
                ((e->tv.tv_sec == tv.tv_sec) && (e->tv.tv_usec > tv.tv_usec)))
              break;
            q->flush += 1;
          }
        }
        if (q->flush > 0) {
          int err, n = q->flush;
          /* make sure this queue does not go away, then unlock list to prevent freezes. */
          q->refs += 1;
          pthread_mutex_unlock (&stream->event_queues_lock);
          do {
            /* paranoia: cyclic wait? */
            err = pthread_cond_timedwait (&q->q.events_processed, &q->q.lock, &ts);
          } while ((q->flush > 0) && (err != ETIMEDOUT));
          xine_event_queue_unref_unlock (q);
          if (err == ETIMEDOUT) {
            xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
              "events: warning: _x_flush_events_queues (%p) timeout.\n", (void *)stream);
          } else {
            xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
              "events: flushed %d events for stream %p.\n", n, (void *)stream);
          }
          /* list may have changed, restart. */
          list_locked = 0;
          break;
        }
      }
      pthread_mutex_unlock (&q->q.lock);
    }
    if (list_locked) {
      pthread_mutex_unlock (&stream->event_queues_lock);
      break;
    }
  }
}
