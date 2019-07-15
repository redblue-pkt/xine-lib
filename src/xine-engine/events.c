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
  struct {
    xine_event_private_t e;
    uint8_t data[MAX_REUSE_DATA];
  } revents[MAX_REUSE_EVENTS];
};

xine_event_t *xine_event_get  (xine_event_queue_t *queue) {
  xine_event_t *event;
  xine_list_iterator_t ite;

  pthread_mutex_lock (&queue->lock);
  ite = NULL;
  event = xine_list_next_value (queue->events, &ite);
  if (ite)
    xine_list_remove (queue->events, ite);
  pthread_mutex_unlock (&queue->lock);

  return event;
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

static void xine_event_queue_delete (xine_event_queue_private_t *queue) {
  xine_list_delete (queue->q.events);
  xine_list_delete (queue->free_events);

  pthread_mutex_destroy (&queue->q.lock);
  pthread_cond_destroy (&queue->q.new_event);
  pthread_cond_destroy (&queue->q.events_processed);

  free (queue);
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
    int refs;
    pthread_mutex_lock (&queue->q.lock);
    queue->refs -= 1;
    refs = queue->refs;
    xine_list_push_back (queue->free_events, e);
    pthread_mutex_unlock (&queue->q.lock);
    if (refs == 0)
      xine_event_queue_delete (queue);
  } else {
    free (event);
  }
}

void xine_event_send (xine_stream_t *s, const xine_event_t *event) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  xine_list_iterator_t ite;
  xine_event_queue_private_t *queue;
  struct timeval now = {0, 0};

  gettimeofday (&now, NULL);
  pthread_mutex_lock (&stream->event_queues_lock);

  ite = NULL;
  while ((queue = xine_list_next_value (stream->event_queues, &ite))) {
    xine_event_private_t *new_event;

    /* calm down bursting progress events pt 1:
     * if list tail has an earlier instance, update it without signal. */
    if (((event->type == XINE_EVENT_PROGRESS) || (event->type == XINE_EVENT_NBC_STATS)) && event->data) {
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
          xine_progress_data_t *pd1 = event->data, *pd2 = e2->data;
          if (pd1->description && pd2->description && !strcmp (pd1->description, pd2->description)) {
            pd2->percent = pd1->percent;
            e2->tv = now;
            pthread_mutex_unlock (&queue->q.lock);
            continue;
          }
        } else { /* XINE_EVENT_NBC_STATS */
          size_t l = event->data_length < e2->data_length ? event->data_length : e2->data_length;
          memcpy (e2->data, event->data, l);
          e2->tv = now;
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
        if (event->data_length > 0) {
          new_event->e.data = (uint8_t *)new_event + sizeof (*new_event);
          xine_small_memcpy (new_event->e.data, event->data, event->data_length);
        } else {
          new_event->e.data = NULL;
        }
        new_event->queue         = queue;
        new_event->e.type        = event->type;
        new_event->e.stream      = &stream->s;
        new_event->e.data_length = event->data_length;
        new_event->e.tv          = now;
        xine_list_push_back (queue->q.events, new_event);
        pthread_cond_signal (&queue->q.new_event);
        pthread_mutex_unlock (&queue->q.lock);
        continue;
      }
      pthread_mutex_unlock (&queue->q.lock);
    }

    if ((event->data_length > 0) && (event->data)) {
      new_event = malloc (sizeof (*new_event) + event->data_length);
      if (!new_event)
        continue;
      new_event->e.data = (uint8_t *)new_event + sizeof (*new_event);
      memcpy (new_event->e.data, event->data, event->data_length);
    } else {
      new_event = malloc (sizeof (*new_event));
      if (!new_event)
        continue;
      new_event->e.data = NULL;
    }
    new_event->queue         = queue;
    new_event->e.type        = event->type;
    new_event->e.stream      = &stream->s;
    new_event->e.data_length = event->data_length;
    new_event->e.tv          = now;
    pthread_mutex_lock (&queue->q.lock);
    xine_list_push_back (queue->q.events, new_event);
    pthread_cond_signal (&queue->q.new_event);
    pthread_mutex_unlock (&queue->q.lock);
  }

  pthread_mutex_unlock (&stream->event_queues_lock);
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

  queue->q.events = xine_list_new ();
  if (!queue->q.events) {
    free (queue);
    return NULL;
  }
  queue->free_events = xine_list_new ();
  if (!queue->free_events) {
    free (queue->q.events);
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

  pthread_mutex_lock (&stream->event_queues_lock);
  xine_list_push_back (stream->event_queues, &queue->q);
  pthread_mutex_unlock (&stream->event_queues_lock);

  return &queue->q;
}

void xine_event_dispose_queue (xine_event_queue_t *queue) {

  xine_stream_private_t *stream = (xine_stream_private_t *)queue->stream;
  xine_event_t         *event;
  xine_event_t         *qevent;
  xine_event_queue_t   *q;
  xine_list_iterator_t  ite;

  pthread_mutex_lock (&stream->event_queues_lock);

  q = NULL;
  ite = NULL;
  while ((q = xine_list_next_value (stream->event_queues, &ite))) {
    if (q == queue)
      break;
  }

  if (q != queue) {
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "events: tried to dispose queue which is not in list\n");

    pthread_mutex_unlock (&stream->event_queues_lock);
    return;
  }

  xine_list_remove (stream->event_queues, ite);
  pthread_mutex_unlock (&stream->event_queues_lock);

  /*
   * send quit event
   */
  qevent = (xine_event_t *)malloc(sizeof(xine_event_t));

  qevent->type        = XINE_EVENT_QUIT;
  qevent->stream      = &stream->s;
  qevent->data        = NULL;
  qevent->data_length = 0;
  gettimeofday (&qevent->tv, NULL);

  pthread_mutex_lock (&queue->lock);
  xine_list_push_back (queue->events, qevent);
  pthread_cond_signal (&queue->new_event);
  pthread_mutex_unlock (&queue->lock);

  /*
   * join listener thread, if any
   */

  if (queue->listener_thread) {
    void *p;
    pthread_join (*queue->listener_thread, &p);
    _x_freep (&queue->listener_thread);
  }

  _x_refcounter_dec(stream->refcounter);

  /*
   * clean up pending events
   */

  while ( (event = xine_event_get (queue)) ) {
    xine_event_free (event);
  }

  {
    xine_event_queue_private_t *q = (xine_event_queue_private_t *)queue;
    int refs;
    pthread_mutex_lock (&q->q.lock);
    q->refs -= 1;
    refs = q->refs;
    pthread_mutex_unlock (&q->q.lock);
    if (refs == 0)
      xine_event_queue_delete (q);
  }
}


static void *listener_loop (void *queue_gen) {

  xine_event_queue_t *queue = (xine_event_queue_t *) queue_gen;
  int running = 1;

  pthread_mutex_lock (&queue->lock);

  while (running) {

    xine_event_t *event;

    event = xine_event_wait_locked (queue);

    if (event->type == XINE_EVENT_QUIT)
      running = 0;

    queue->callback_running = 1;

    pthread_mutex_unlock (&queue->lock);

    queue->callback (queue->user_data, event);
    xine_event_free (event);

    pthread_mutex_lock (&queue->lock);

    queue->callback_running = 0;

    if (xine_list_empty (queue->events)) {
      pthread_cond_signal (&queue->events_processed);
    }
  }

  pthread_mutex_unlock (&queue->lock);
  return NULL;
}


int xine_event_create_listener_thread (xine_event_queue_t *queue,
                                       xine_event_listener_cb_t callback,
                                       void *user_data) {
  int err;

  queue->listener_thread = malloc (sizeof (pthread_t));
  queue->callback        = callback;
  queue->user_data       = user_data;

  if (!queue->listener_thread)
    return 0;

  if ((err = pthread_create (queue->listener_thread,
			     NULL, listener_loop, queue)) != 0) {
    xprintf (queue->stream->xine, XINE_VERBOSITY_NONE,
	     "events: can't create new thread (%s)\n", strerror(err));
    _x_freep(&queue->listener_thread);
    return 0;
  }

  return 1;
}
