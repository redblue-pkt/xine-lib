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
 * Event handling functions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include <sys/time.h>

#include <xine/xine_internal.h>
#include "xine_private.h"

#define MAX_REUSE_EVENTS 8
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
  int                 num_all;
  int                 num_alloc;
  int                 num_skip;
  struct {
    xine_event_private_t e;
    uint8_t data[MAX_REUSE_DATA];
  } revents[MAX_REUSE_EVENTS];
};

xine_event_t *xine_event_get  (xine_event_queue_t *queue) {
  xine_event_t *event;
  xine_list_iterator_t ite;

  if (!queue)
    return NULL;
  pthread_mutex_lock (&queue->lock);
  ite = NULL;
  event = xine_list_next_value (queue->events, &ite);
  if (ite)
    xine_list_remove (queue->events, ite);
  pthread_mutex_unlock (&queue->lock);

  return event;
}

xine_event_t *xine_event_next (xine_event_queue_t *queue, xine_event_t *prev_event) {
  xine_event_queue_private_t *q = (xine_event_queue_private_t *)queue;

  if (!q)
    return NULL;
  pthread_mutex_lock (&q->q.lock);
  if (prev_event) {
    if (PTR_IN_RANGE (prev_event, &q->revents, sizeof (q->revents))) {
      q->refs -= 1;
      xine_list_push_back (q->free_events, prev_event);
    } else {
      free (prev_event);
    }
  }
  {
    xine_list_iterator_t ite = NULL;
    xine_event_t *event = xine_list_next_value (q->q.events, &ite);
    if (ite)
      xine_list_remove (q->q.events, ite);
    pthread_mutex_unlock (&q->q.lock);
    return event;
  }
}

static xine_event_t *xine_event_wait_locked (xine_event_queue_t *queue) {
  xine_event_t  *event, *first_event;
  xine_list_iterator_t ite, first_ite;
  int wait;

  /* wait until there is at least 1 event */
  while (1) {
    first_ite = NULL;
    first_event = xine_list_next_value (queue->events, &first_ite);
    if (first_ite)
      break;
    pthread_cond_wait (&queue->new_event, &queue->lock);
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
    event = xine_list_next_value (queue->events, &ite);
  } while (ite);
  if (wait) {
    struct timespec ts = {0, 0};
    xine_gettime (&ts);
    ts.tv_nsec += 50000000;
    if (ts.tv_nsec >= 1000000000) {
      ts.tv_nsec -= 1000000000;
      ts.tv_sec += 1;
    }
    pthread_cond_timedwait (&queue->new_event, &queue->lock, &ts);
    /* paranoia ? */
    while (1) {
      first_ite = NULL;
      first_event = xine_list_next_value (queue->events, &first_ite);
      if (first_ite)
        break;
      pthread_cond_wait (&queue->new_event, &queue->lock);
    }
  }

  xine_list_remove (queue->events, first_ite);
  return first_event;
}

xine_event_t *xine_event_wait (xine_event_queue_t *queue) {

  xine_event_t  *event;

  pthread_mutex_lock (&queue->lock);
  event = xine_event_wait_locked(queue);
  pthread_mutex_unlock (&queue->lock);

  return event;
}

static int xine_event_queue_unref_unlock (xine_event_queue_private_t *queue) {
  int refs;

  queue->refs -= 1;
  refs = queue->refs;
  pthread_mutex_unlock (&queue->q.lock);

  if (refs == 0) {
    xine_list_delete (queue->q.events);
    xine_list_delete (queue->free_events);
    pthread_mutex_destroy (&queue->q.lock);
    pthread_cond_destroy (&queue->q.new_event);
    pthread_cond_destroy (&queue->q.events_processed);
    free (queue);
  }

  return refs;
}

void xine_event_free (xine_event_t *event) {
  /* XXX; assuming this was returned by xine_event_get () or xine_event_wait () before. */
  xine_event_private_t *e = (xine_event_private_t *)event;
  xine_event_queue_private_t *queue;

  if (!e)
    return;
  queue = e->queue;
  if (!queue)
    return;
  if (PTR_IN_RANGE (e, &queue->revents, sizeof (queue->revents))) {
    pthread_mutex_lock (&queue->q.lock);
    xine_list_push_back (queue->free_events, e);
    xine_event_queue_unref_unlock (queue);
  } else {
    free (event);
  }
}

void xine_event_send (xine_stream_t *s, const xine_event_t *event) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s, *mstream;
  xine_list_iterator_t ite;
  xine_event_queue_private_t *queue;
  void *data;
  struct timeval now = {0, 0};

  if (!stream || !event)
    return;
  mstream = stream->side_streams[0];
  data = (event->data_length <= 0) ? NULL : event->data;

  gettimeofday (&now, NULL);
  pthread_mutex_lock (&mstream->event_queues_lock);

  ite = NULL;
  while ((queue = xine_list_next_value (mstream->event_queues, &ite))) {
    xine_event_private_t *new_event;

    /* XINE_EVENT_VDR_DISCONTINUITY: .data == NULL, .data_length == DISC_* */
    if (!data) {
      xine_list_iterator_t it2 = NULL;
      pthread_mutex_lock (&queue->q.lock);
      new_event = xine_list_next_value (queue->free_events, &it2);
      if (new_event) {
        xine_list_remove (queue->free_events, it2);
        queue->refs += 1;
      } else {
        new_event = malloc (sizeof (*new_event));
        if (!new_event) {
          pthread_mutex_unlock (&queue->q.lock);
          continue;
        }
        queue->num_alloc += 1;
      }
      new_event->e.data        = NULL;
      new_event->queue         = queue;
      new_event->e.type        = event->type;
      new_event->e.data_length = event->data_length;
      new_event->e.stream      = &stream->s;
      new_event->e.tv          = now;
      queue->num_all += 1;
      xine_list_push_back (queue->q.events, new_event);
      pthread_cond_signal (&queue->q.new_event);
      pthread_mutex_unlock (&queue->q.lock);
      continue;
    }

    /* calm down bursting progress events pt 1:
     * if list tail has an earlier instance, update it without signal. */
    if ((event->type == XINE_EVENT_PROGRESS) || (event->type == XINE_EVENT_NBC_STATS)) {
      xine_event_t *e2;
      pthread_mutex_lock (&queue->q.lock);
      do {
        xine_list_iterator_t it2 = NULL;
        e2 = xine_list_prev_value (queue->q.events, &it2);
        if (!it2)
          break;
        if (e2 && (e2->type == event->type) && e2->data)
          break;
        e2 = xine_list_prev_value (queue->q.events, &it2);
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
            queue->num_all += 1;
            queue->num_skip += 1;
            pthread_mutex_unlock (&queue->q.lock);
            continue;
          }
        } else { /* XINE_EVENT_NBC_STATS */
          size_t l = event->data_length < e2->data_length ? event->data_length : e2->data_length;
          memcpy (e2->data, data, l);
          e2->tv = now;
          queue->num_all += 1;
          queue->num_skip += 1;
          pthread_mutex_unlock (&queue->q.lock);
          continue;
        }
      }
      pthread_mutex_unlock (&queue->q.lock);
    }

    if (event->data_length <= MAX_REUSE_DATA) {
      xine_list_iterator_t it2 = NULL;
      pthread_mutex_lock (&queue->q.lock);
      new_event = xine_list_next_value (queue->free_events, &it2);
      if (new_event) {
        xine_list_remove (queue->free_events, it2);
        queue->refs += 1;
        new_event->e.data = (uint8_t *)new_event + sizeof (*new_event);
        xine_small_memcpy (new_event->e.data, data, event->data_length);
        new_event->queue         = queue;
        new_event->e.type        = event->type;
        new_event->e.data_length = event->data_length;
        new_event->e.stream      = &stream->s;
        new_event->e.tv          = now;
        queue->num_all += 1;
        xine_list_push_back (queue->q.events, new_event);
        pthread_cond_signal (&queue->q.new_event);
        pthread_mutex_unlock (&queue->q.lock);
        continue;
      }
      pthread_mutex_unlock (&queue->q.lock);
    }

    new_event = malloc (sizeof (*new_event) + event->data_length);
    if (!new_event)
      continue;
    new_event->e.data = (uint8_t *)new_event + sizeof (*new_event);
    memcpy (new_event->e.data, data, event->data_length);
    new_event->queue         = queue;
    new_event->e.type        = event->type;
    new_event->e.data_length = event->data_length;
    new_event->e.stream      = &stream->s;
    new_event->e.tv          = now;
    pthread_mutex_lock (&queue->q.lock);
    queue->num_all += 1;
    queue->num_alloc += 1;
    xine_list_push_back (queue->q.events, new_event);
    pthread_cond_signal (&queue->q.new_event);
    pthread_mutex_unlock (&queue->q.lock);
  }

  pthread_mutex_unlock (&mstream->event_queues_lock);
}


xine_event_queue_t *xine_event_new_queue (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  xine_event_queue_private_t *queue;
  uint32_t n;

  if (!stream)
    return NULL;
  queue = malloc (sizeof (*queue));
  if (!queue)
    return NULL;

  queue->refs = 1;
  queue->num_all = 0;
  queue->num_alloc = 0;
  queue->num_skip = 0;

  queue->q.events = xine_list_new ();
  if (!queue->q.events) {
    free (queue);
    return NULL;
  }
  queue->free_events = xine_list_new ();
  if (!queue->free_events) {
    xine_list_delete (queue->q.events);
    free (queue);
    return NULL;
  }
  for (n = 0; n < MAX_REUSE_EVENTS; n++)
    xine_list_push_back (queue->free_events, &queue->revents[n]);

  _x_refcounter_inc(stream->refcounter);

  pthread_mutex_init (&queue->q.lock, NULL);
  pthread_cond_init (&queue->q.new_event, NULL);
  pthread_cond_init (&queue->q.events_processed, NULL);
  queue->q.stream = &stream->s;
  queue->q.listener_thread = NULL;
  queue->q.callback_running = 0;

  {
    xine_stream_private_t *mstream = stream->side_streams[0];
    pthread_mutex_lock (&mstream->event_queues_lock);
    xine_list_push_back (mstream->event_queues, &queue->q);
    pthread_mutex_unlock (&mstream->event_queues_lock);
  }

  return &queue->q;
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
    _x_freep (&q->q.listener_thread);
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
    pthread_cond_signal (&q->q.events_processed);
    num_all = q->num_all;
    num_alloc = q->num_alloc;
    num_skip = q->num_skip;
    refs = xine_event_queue_unref_unlock (q);
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
      "events: stream %p: %d total, %d allocated, %d skipped, %d left and dropped, %d refs.\n",
      (void *)stream, num_all, num_alloc, num_skip, num_left, refs);
  }

  _x_refcounter_dec (stream->refcounter);
}


static void *listener_loop (void *queue_gen) {

  xine_event_queue_private_t *queue = queue_gen;
  int running = 1;

  pthread_mutex_lock (&queue->q.lock);

  while (running) {
    xine_event_t *event = xine_event_wait_locked (&queue->q);
    queue->q.callback_running = 1;
    pthread_mutex_unlock (&queue->q.lock);
    if (event->type == XINE_EVENT_QUIT)
      running = 0;

    queue->q.callback (queue->q.user_data, event);

    if (PTR_IN_RANGE (event, &queue->revents, sizeof (queue->revents))) {
      pthread_mutex_lock (&queue->q.lock);
      queue->refs -= 1;
      /* refs == 0 cannot heppen here. */
      xine_list_push_back (queue->free_events, event);
    } else {
      free (event);
      pthread_mutex_lock (&queue->q.lock);
    }
    queue->q.callback_running = 0;
    if (xine_list_empty (queue->q.events))
      pthread_cond_signal (&queue->q.events_processed);
  }

  pthread_mutex_unlock (&queue->q.lock);
  return NULL;
}


int xine_event_create_listener_thread (xine_event_queue_t *queue,
                                       xine_event_listener_cb_t callback,
                                       void *user_data) {
  int err;

  _x_assert(queue != NULL);
  _x_assert(callback != NULL);

  if (queue->listener_thread) {
    xprintf (queue->stream->xine, XINE_VERBOSITY_NONE,
             "events: listener thread already created\n");
    return 0;
  }

  queue->listener_thread = malloc (sizeof (pthread_t));
  if (!queue->listener_thread) {
    return 0;
  }

  queue->callback        = callback;
  queue->user_data       = user_data;

  if ((err = pthread_create (queue->listener_thread,
                             NULL, listener_loop, queue)) != 0) {
    xprintf (queue->stream->xine, XINE_VERBOSITY_NONE,
             "events: can't create new thread (%s)\n", strerror(err));
    _x_freep(&queue->listener_thread);
    queue->callback        = NULL;
    queue->user_data       = NULL;
    return 0;
  }

  return 1;
}

void _x_flush_events_queues (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  if (!stream)
    return;
  stream = stream->side_streams[0];

  while (1) {
    xine_event_queue_private_t *queue;
    xine_list_iterator_t ite = NULL;
    int list_locked = 1;

    pthread_mutex_lock (&stream->event_queues_lock);
    while ((queue = xine_list_next_value (stream->event_queues, &ite))) {
      pthread_mutex_lock (&queue->q.lock);
      /* we might have been called from the very same function that
       * processes events, therefore waiting here would cause deadlock.
       * check only queues with listener threads which are not
       * currently executing their callback functions. */
      if (queue->q.listener_thread && !queue->q.callback_running && !xine_list_empty (queue->q.events)) {
        /* make sure this queue does not go away, then unlock list to prevent freezes. */
        queue->refs += 1;
        pthread_mutex_unlock (&stream->event_queues_lock);
        do {
          pthread_cond_wait (&queue->q.events_processed, &queue->q.lock);
        } while (!xine_list_empty (queue->q.events));
        xine_event_queue_unref_unlock (queue);
        /* list may have changed, restart. */
        list_locked = 0;
        break;
      }
      pthread_mutex_unlock (&queue->q.lock);
    }
    if (list_locked) {
      pthread_mutex_unlock (&stream->event_queues_lock);
      break;
    }
  }
}
