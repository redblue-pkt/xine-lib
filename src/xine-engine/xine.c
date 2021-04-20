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
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#if defined (__linux__) || defined (__GLIBC__)
#include <endian.h>
#elif defined (__FreeBSD__)
#include <machine/endian.h>
#endif

#ifdef HAVE_SETLOCALE
#include <locale.h>
#endif

#include <basedir.h>

#define LOG_MODULE "xine"
#define LOG_VERBOSE
/*
#define LOG
#define DEBUG
*/

#define XINE_ENABLE_EXPERIMENTAL_FEATURES
#define METRONOM_CLOCK_INTERNAL
#define POST_INTERNAL

#include <xine/xine_internal.h>
#include <xine/plugin_catalog.h>
#include <xine/audio_out.h>
#include <xine/video_out.h>
#include <xine/post.h>
#include <xine/demux.h>
#include <xine/buffer.h>
#include <xine/spu_decoder.h>
#include <xine/input_plugin.h>
#include <xine/metronom.h>
#include <xine/configfile.h>
#include <xine/osd.h>
#include <xine/spu.h>

#include <xine/xineutils.h>
#include <xine/compat.h>

#ifdef WIN32
#   include <fcntl.h>
#   include <winsock.h>
#endif /* WIN32 */

#include "xine_private.h"



static void mutex_cleanup (void *mutex) {
  pthread_mutex_unlock ((pthread_mutex_t *) mutex);
}

void _x_handle_stream_end (xine_stream_t *s, int non_user) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  stream = stream->side_streams[0];
  if (stream->status == XINE_STATUS_QUIT)
    return;
  stream->status = XINE_STATUS_STOP;

  if (non_user) {
    /* frontends will not be interested in receiving this event
     * if they have called xine_stop explicitly, so only send
     * it if stream playback finished because of stream end reached
     */

    xine_event_t event;

    stream->finished_naturally = 1;

    event.data_length = 0;
    event.type        = XINE_EVENT_UI_PLAYBACK_FINISHED;

    xine_event_send (&stream->s, &event);
  }
}

void _x_extra_info_reset( extra_info_t *extra_info ) {
  memset( extra_info, 0, sizeof(extra_info_t) );
}

void _x_extra_info_merge( extra_info_t *dst, extra_info_t *src ) {

  if (!src->invalid) {
    if( src->input_normpos )
      dst->input_normpos = src->input_normpos;

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

static void xine_current_extra_info_reset (xine_stream_private_t *stream) {
  int index = xine_refs_get (&stream->current_extra_info_index);
  extra_info_t *b = &stream->current_extra_info[(index + 1) & (XINE_NUM_CURR_EXTRA_INFOS - 1)];

  memset (b, 0, sizeof (*b));
  xine_refs_add (&stream->current_extra_info_index, 1);
}

void xine_current_extra_info_set (xine_stream_private_t *stream, const extra_info_t *info) {
  if (!info->invalid) {
    int index = xine_refs_get (&stream->current_extra_info_index);
    const extra_info_t *a = &stream->current_extra_info[index & (XINE_NUM_CURR_EXTRA_INFOS - 1)];
    extra_info_t *b = &stream->current_extra_info[(index + 1) & (XINE_NUM_CURR_EXTRA_INFOS - 1)];

    b->input_normpos = info->input_normpos ? info->input_normpos : a->input_normpos;
    b->input_time    = info->input_time    ? info->input_time    : a->input_time;
    b->frame_number  = info->frame_number  ? info->frame_number  : a->frame_number;
    b->seek_count    = info->seek_count    ? info->seek_count    : a->seek_count;
    b->vpts          = info->vpts          ? info->vpts          : a->vpts;

    xine_refs_add (&stream->current_extra_info_index, 1);
  }
}

static int xine_current_extra_info_get (xine_stream_private_t *stream, extra_info_t *info) {
  int index = xine_refs_get (&stream->current_extra_info_index);
  const extra_info_t *a = &stream->current_extra_info[index & (XINE_NUM_CURR_EXTRA_INFOS - 1)];

  *info = *a;
  return stream->video_seek_count;
}
  
#define XINE_TICKET_FLAG_PAUSE (int)0x40000000

typedef struct {
  xine_ticket_t   t;

  pthread_mutex_t lock;
  pthread_cond_t  issued;
  pthread_cond_t  revoked;
  xine_rwlock_t   port_rewiring_lock;

  int             pause_revoked;
  int             tickets_granted;
  int             irrevocable_tickets;
  int             plain_renewers;

  int             rewirers;
  int             pending_revocations;
  int             atomic_revokers;
  pthread_t       atomic_revoker_thread;

  struct {
    int count;
    pthread_t holder;
  } *holder_threads;
  unsigned        holder_thread_count;

#define XINE_TICKET_MAX_CB 15
  xine_ticket_revoke_cb_t *revoke_callbacks[XINE_TICKET_MAX_CB + 1];
  void *revoke_cb_data[XINE_TICKET_MAX_CB + 1];
} xine_ticket_private_t;

static void ticket_revoke_cb_register (xine_ticket_t *tgen, xine_ticket_revoke_cb_t *cb, void *user_data) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  int i;
  pthread_mutex_lock (&this->lock);
  for (i = 0; i < XINE_TICKET_MAX_CB; i++) {
    if (!this->revoke_callbacks[i]) {
      this->revoke_callbacks[i] = cb;
      this->revoke_cb_data[i]   = user_data;
      break;
    }
    if ((this->revoke_callbacks[i] == cb) && (this->revoke_cb_data[i] == user_data))
      break;
  }
  pthread_mutex_unlock (&this->lock);
}

static void ticket_revoke_cb_unregister (xine_ticket_t *tgen, xine_ticket_revoke_cb_t *cb, void *user_data) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  int f;
  pthread_mutex_lock (&this->lock);
  for (f = 0; this->revoke_callbacks[f]; f++) {
    if ((this->revoke_callbacks[f] == cb) && (this->revoke_cb_data[f] == user_data)) {
      int l;
      for (l = f; this->revoke_callbacks[l + 1]; l++) ;
      if (f < l) {
        this->revoke_callbacks[f] = this->revoke_callbacks[l];
        this->revoke_cb_data[f]   = this->revoke_cb_data[l];
      }
      this->revoke_callbacks[l] = NULL;
      this->revoke_cb_data[l]   = NULL;
      break;
    }
  }
  pthread_mutex_unlock (&this->lock);
}

static int ticket_acquire_internal (xine_ticket_private_t *this, int irrevocable, int nonblocking) {
  pthread_t self = pthread_self ();
  unsigned int wait, i;

  pthread_mutex_lock (&this->lock);

  /* find ourselves in user list */
  for (i = 0; i < this->holder_thread_count; i++) {
    if (pthread_equal (this->holder_threads[i].holder, self))
      break;
  }

  if (i >= this->holder_thread_count) {
    /* not found */
    wait = this->pending_revocations
        && (this->atomic_revokers ? !pthread_equal (this->atomic_revoker_thread, self) : !irrevocable);
    if (nonblocking && wait) {
      /* not available immediately, bail out */
      pthread_mutex_unlock (&this->lock);
      return 0;
    }
    /* add */
    if (((i & 31) == 31) && (this->holder_threads[i].count == -1000)) {
      /* enlarge list */
      void *new = realloc (this->holder_threads, sizeof (*this->holder_threads) * (i + 33));
      if (!new) {
        pthread_mutex_unlock (&this->lock);
        return 0;
      }
      this->holder_threads = new;
      this->holder_threads[i + 32].count = -1000;
    }
    this->holder_threads[i].count = 1;
    this->holder_threads[i].holder = self;
    this->holder_thread_count = i + 1;

    while (wait) {
      pthread_cond_wait (&this->issued, &this->lock);
      wait = this->pending_revocations
        && (this->atomic_revokers ? !pthread_equal (this->atomic_revoker_thread, self) : !irrevocable);
    }

  } else {
    /* found, we already hold this */
    this->holder_threads[i].count++;
  }

  this->tickets_granted++;
  if (irrevocable)
    this->irrevocable_tickets++;

  pthread_mutex_unlock (&this->lock);
  return 1;
}

static int ticket_acquire_nonblocking(xine_ticket_t *tgen, int irrevocable) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  return ticket_acquire_internal(this, irrevocable, 1);
}

static void ticket_acquire(xine_ticket_t *tgen, int irrevocable) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  ticket_acquire_internal(this, irrevocable, 0);
}

static void ticket_release_internal (xine_ticket_private_t *this, int irrevocable) {
  pthread_t self = pthread_self ();
  unsigned int i;

  pthread_mutex_lock (&this->lock);

  /* find ourselves in user list */
  for (i = 0; i < this->holder_thread_count; i++) {
    if (pthread_equal (this->holder_threads[i].holder, self))
      break;
  }

  if (i >= this->holder_thread_count) {
    lprintf ("BUG! Ticket 0x%p released by a thread that never took it! Allowing code to continue\n", (void*)this);
    _x_assert (0);
  } else {
    this->holder_threads[i].count--;
    if (this->holder_threads[i].count == 0) {
      this->holder_thread_count--;
      if (i < this->holder_thread_count) {
        this->holder_threads[i].count  = this->holder_threads[this->holder_thread_count].count;
        this->holder_threads[i].holder = this->holder_threads[this->holder_thread_count].holder;
      }
    }
  }

  if ((this->irrevocable_tickets > 0) && irrevocable)
    this->irrevocable_tickets--;
  if (this->tickets_granted > 0)
    this->tickets_granted--;

  if (this->pending_revocations
    && (!this->tickets_granted || (this->rewirers && !this->irrevocable_tickets)))
    pthread_cond_broadcast(&this->revoked);

  pthread_mutex_unlock(&this->lock);
}

static void ticket_release_nonblocking (xine_ticket_t *tgen, int irrevocable) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  ticket_release_internal (this, irrevocable);
}

static void ticket_release (xine_ticket_t *tgen, int irrevocable) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  ticket_release_internal (this, irrevocable);
}

static void ticket_renew (xine_ticket_t *tgen, int irrevocable) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  unsigned int i;
  int grants;
  pthread_t self = pthread_self ();
  pthread_mutex_lock (&this->lock);
#if 0
  /* For performance, caller checks ticket_revoked without lock. 0 here is not really a bug. */
  _x_assert (this->ticket_revoked);
#endif
  do {
    /* Never wait for self. This may happen when post plugins call this during their open (). */
    if (this->atomic_revokers && pthread_equal (this->atomic_revoker_thread, self))
        break;
    /* Multithreaded decoders may well allocate frames outside the time window of the main
     * decoding call. Usually, we cannot make those hidden threads acquire and release
     * tickets properly. Blocking one of them may freeze main decoder with ticket held.
     * Lets give them special treatment. */
    for (i = 0; i < this->holder_thread_count; i++) {
      if (pthread_equal (this->holder_threads[i].holder, self))
        break;
    }
    if (i >= this->holder_thread_count) {
      /* If registered threads are safe: Wait for ticket reissue, 
       * and dont burn cpu in endless retries.
       * If registered threads are still running around: Fall through. */
      while (this->pending_revocations && !this->tickets_granted)
        pthread_cond_wait (&this->issued, &this->lock);
      break;
    }
    /* If our thread still has saved self grants and calls this, restore them.
      * Assume later reissue by another thread (engine pause). */
    grants = this->holder_threads[i].count;
    if (grants >= 0x80000) {
      grants &= 0x7ffff;
      this->holder_threads[i].count = grants;
      this->tickets_granted += grants;
    }
    /* Lift _all_ our grants, avoid freeze when we have more than 1. */
    if (irrevocable & XINE_TICKET_FLAG_REWIRE) {
      /* allow everything */
      this->tickets_granted -= grants;
      if (!this->tickets_granted)
        pthread_cond_broadcast (&this->revoked);
      while (this->pending_revocations
        && (!this->irrevocable_tickets || !(irrevocable & ~XINE_TICKET_FLAG_REWIRE)))
        pthread_cond_wait (&this->issued, &this->lock);
      this->tickets_granted += grants;
    } else {
      /* fair wheather (not rewire) mode */
      this->plain_renewers++;
      this->tickets_granted -= grants;
      if (!this->tickets_granted)
        pthread_cond_broadcast (&this->revoked);
      while (this->pending_revocations
        && (!this->irrevocable_tickets || !irrevocable)
        && !this->rewirers)
        pthread_cond_wait (&this->issued, &this->lock);
      this->tickets_granted += grants;
      this->plain_renewers--;
    }
  } while (0);
  pthread_mutex_unlock (&this->lock);
}

/* XINE_TICKET_FLAG_REWIRE implies XINE_TICKET_FLAG_ATOMIC. */
static void ticket_issue (xine_ticket_t *tgen, int flags) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  pthread_t self;
  unsigned int i;

  /* better not miss a reissue, and skip that no lock test. */
  self = pthread_self ();
  pthread_mutex_lock (&this->lock);
  if (flags == XINE_TICKET_FLAG_PAUSE) {
    if (!this->pause_revoked) {
      pthread_mutex_unlock (&this->lock);
      return;
    }
    this->pause_revoked = 0;
    flags = 0;
  }

  if (this->pending_revocations > 0)
    this->pending_revocations--;
  if ((flags & XINE_TICKET_FLAG_REWIRE) && (this->rewirers > 0))
    this->rewirers--;
  if (flags & (XINE_TICKET_FLAG_REWIRE | XINE_TICKET_FLAG_ATOMIC)) {
    if (this->atomic_revokers > 0)
      this->atomic_revokers--;
    pthread_cond_broadcast (&this->revoked);
  }

  {
    int n = !this->pending_revocations ? 0 : (this->rewirers ? (XINE_TICKET_FLAG_REWIRE | 1) : 1);
    if (this->t.ticket_revoked != n) {
      this->t.ticket_revoked = n;
      pthread_cond_broadcast (&this->issued);
    }
  }

  /* HACK: restore self grants. */
  for (i = 0; i < this->holder_thread_count; i++) {
    if (pthread_equal (this->holder_threads[i].holder, self)) {
      int n = this->holder_threads[i].count;
      if (n >= 0x80000) {
        n -= 0x80000;
        this->holder_threads[i].count = n;
        if (n < 0x80000)
          this->tickets_granted += n;
      }
      break;
    }
  }

  pthread_mutex_unlock(&this->lock);
  if (flags & (XINE_TICKET_FLAG_REWIRE | XINE_TICKET_FLAG_ATOMIC))
    xine_rwlock_unlock (&this->port_rewiring_lock);
}

static void ticket_revoke (xine_ticket_t *tgen, int flags) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  pthread_t self;

  if (flags == XINE_TICKET_FLAG_PAUSE) {
    if (this->pause_revoked)
      return;
    self = pthread_self ();
    /* flags & (XINE_TICKET_FLAG_REWIRE | XINE_TICKET_FLAG_ATOMIC) does not happen here. */
    pthread_mutex_lock (&this->lock);
    if (this->pause_revoked) {
      pthread_mutex_unlock (&this->lock);
      return;
    }
    this->pause_revoked = 1;
    flags = 0;
  } else {
    self = pthread_self ();
    if (flags & (XINE_TICKET_FLAG_REWIRE | XINE_TICKET_FLAG_ATOMIC))
      xine_rwlock_wrlock (&this->port_rewiring_lock);
    pthread_mutex_lock (&this->lock);
  }

  /* HACK: dont freeze on self grants, save them.
   * Also, nest revokes at bit 19. */
  {
    unsigned int i;
    for (i = 0; i < this->holder_thread_count; i++) {
      if (pthread_equal (this->holder_threads[i].holder, self)) {
        int n = this->holder_threads[i].count;
        if (n < 0x80000)
          this->tickets_granted -= n;
        this->holder_threads[i].count = n + 0x80000;
        break;
      }
    }
  }

  /* Set these early so release () / renew () see them. */
  this->pending_revocations++;

  /* No joke - see renew (). */
  if (flags & XINE_TICKET_FLAG_REWIRE) {
    this->rewirers++;
    if (this->plain_renewers)
      pthread_cond_broadcast (&this->issued);
  }

  /* New public status. */
  this->t.ticket_revoked = (this->rewirers ? (XINE_TICKET_FLAG_REWIRE | 1) : 1);

  do {
    /* Never wait for self. */
    if (this->atomic_revokers && pthread_equal (this->atomic_revoker_thread, self)) {
      if (flags & (XINE_TICKET_FLAG_REWIRE | XINE_TICKET_FLAG_ATOMIC))
        this->atomic_revokers++;
      break;
    }
    /* Need waiting? */
    if (this->tickets_granted || (this->rewirers && this->plain_renewers) || this->atomic_revokers) {
      /* Notify other holders. */
      int i, f = (this->rewirers ? XINE_TICKET_FLAG_REWIRE : 0)
               | (this->atomic_revokers ? XINE_TICKET_FLAG_ATOMIC : 0);
      for (i = 0; i < XINE_TICKET_MAX_CB; i++) {
        if (!this->revoke_callbacks[i])
          break;
        this->revoke_callbacks[i] (this->revoke_cb_data[i], f);
      }
      /* Wait for them to release/renew. */
      do {
        pthread_cond_wait (&this->revoked, &this->lock);
        this->t.ticket_revoked = (this->rewirers ? (XINE_TICKET_FLAG_REWIRE | 1) : 1);
      } while (this->tickets_granted || (this->rewirers && this->plain_renewers) || this->atomic_revokers);
    }
    /* Its ours now. */
    if (flags & (XINE_TICKET_FLAG_REWIRE | XINE_TICKET_FLAG_ATOMIC)) {
      this->atomic_revoker_thread = self;
      this->atomic_revokers++;
    }
  } while (0);

  pthread_mutex_unlock(&this->lock);
}

static int lock_timeout (pthread_mutex_t *mutex, int ms_timeout) {
  if (ms_timeout == 0)
    return (0 == pthread_mutex_trylock (mutex));
  if (ms_timeout >= 0) {
    struct timespec abstime = {0, 0};
    xine_gettime (&abstime);
    abstime.tv_sec  +=  ms_timeout / 1000;
    abstime.tv_nsec += (ms_timeout % 1000) * 1000000;
    if (abstime.tv_nsec >= 1000000000) {
      abstime.tv_nsec -= 1000000000;
      abstime.tv_sec++;
    }
    return (0 == pthread_mutex_timedlock (mutex, &abstime));
  }
  pthread_mutex_lock (mutex);
  return 1;
}

static int ticket_lock_port_rewiring (xine_ticket_t *tgen, int ms_timeout) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  if (ms_timeout == 0)
    return (0 == xine_rwlock_tryrdlock (&this->port_rewiring_lock));
  if (ms_timeout >= 0) {
    struct timespec abstime = {0, 0};
    xine_gettime (&abstime);
    abstime.tv_sec  +=  ms_timeout / 1000;
    abstime.tv_nsec += (ms_timeout % 1000) * 1000000;
    if (abstime.tv_nsec >= 1000000000) {
      abstime.tv_nsec -= 1000000000;
      abstime.tv_sec++;
    }
    return (0 == xine_rwlock_timedrdlock (&this->port_rewiring_lock, &abstime));
  }
  xine_rwlock_rdlock (&this->port_rewiring_lock);
  return 1;
}

static void ticket_unlock_port_rewiring (xine_ticket_t *tgen) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;
  xine_rwlock_unlock (&this->port_rewiring_lock);
}

static void ticket_dispose (xine_ticket_t *tgen) {
  xine_ticket_private_t *this = (xine_ticket_private_t *)tgen;

  xine_rwlock_destroy   (&this->port_rewiring_lock);
  pthread_mutex_destroy (&this->lock);
  pthread_cond_destroy  (&this->issued);
  pthread_cond_destroy  (&this->revoked);

  free (this->holder_threads);
  free (this);
}

static xine_ticket_t *XINE_MALLOC ticket_init(void) {
  xine_ticket_private_t *port_ticket;

  port_ticket = calloc (1, sizeof (xine_ticket_private_t));
  if (!port_ticket)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  port_ticket->t.ticket_revoked     = 0;
  port_ticket->pause_revoked        = 0;
  port_ticket->holder_thread_count  = 0;
  port_ticket->tickets_granted      = 0;
  port_ticket->irrevocable_tickets  = 0;
  port_ticket->atomic_revokers      = 0;
  port_ticket->rewirers             = 0;
  port_ticket->plain_renewers       = 0;
  {
    int i;
    for (i = 0; i < XINE_TICKET_MAX_CB; i++) {
      port_ticket->revoke_callbacks[i] = NULL;
      port_ticket->revoke_cb_data[i]   = NULL;
    }
  }
#endif
  port_ticket->t.acquire_nonblocking  = ticket_acquire_nonblocking;
  port_ticket->t.acquire              = ticket_acquire;
  port_ticket->t.release_nonblocking  = ticket_release_nonblocking;
  port_ticket->t.release              = ticket_release;
  port_ticket->t.renew                = ticket_renew;
  port_ticket->t.revoke_cb_register   = ticket_revoke_cb_register;
  port_ticket->t.revoke_cb_unregister = ticket_revoke_cb_unregister;
  port_ticket->t.issue                = ticket_issue;
  port_ticket->t.revoke               = ticket_revoke;
  port_ticket->t.lock_port_rewiring   = ticket_lock_port_rewiring;
  port_ticket->t.unlock_port_rewiring = ticket_unlock_port_rewiring;
  port_ticket->t.dispose              = ticket_dispose;
  port_ticket->holder_threads         = malloc (32 * sizeof (*port_ticket->holder_threads));
  if (!port_ticket->holder_threads) {
    free (port_ticket);
    return NULL;
  }
  port_ticket->holder_threads[31].count = -1000;

  pthread_mutex_init       (&port_ticket->lock, NULL);
  xine_rwlock_init_default (&port_ticket->port_rewiring_lock);
  pthread_cond_init        (&port_ticket->issued, NULL);
  pthread_cond_init        (&port_ticket->revoked, NULL);

  return &port_ticket->t;
}

static void set_speed_internal (xine_stream_private_t *stream, int speed) {
  xine_private_t *xine = (xine_private_t *)stream->s.xine;
  int old_speed = xine->x.clock->speed;
  int mode = 2 * (speed == XINE_SPEED_PAUSE) + (old_speed == XINE_SPEED_PAUSE);

  if (mode & 1) {
    if (mode & 2) {
      /* stay paused */
      return;
    } else {
      /* switch pause mode */
      if (speed == XINE_LIVE_PAUSE_ON) {
        xine->port_ticket->issue (xine->port_ticket, XINE_TICKET_FLAG_PAUSE);
        return;
      }
      if (speed == XINE_LIVE_PAUSE_OFF) {
        xine->port_ticket->revoke (xine->port_ticket, XINE_TICKET_FLAG_PAUSE);
        return;
      }
      /* unpause. all decoder and post threads may continue now, if not running already. */
      xine->port_ticket->issue (xine->port_ticket, XINE_TICKET_FLAG_PAUSE);
    }
  } else {
    if (mode & 2) {
      /* pause */
      /* get all decoder and post threads in a state where they agree to be blocked */
      xine->port_ticket->revoke (xine->port_ticket, XINE_TICKET_FLAG_PAUSE);
      /* set master clock so audio_out loop can pause in a safe place */
      xine->x.clock->set_fine_speed (xine->x.clock, speed);
    } else {
      if ((speed == XINE_LIVE_PAUSE_ON) || (speed == XINE_LIVE_PAUSE_OFF))
        return;
      /* plain change */
      if (speed == old_speed)
        return;
    }
  }

  /* see coment on audio_out loop about audio_paused */
  if (stream->s.audio_out) {
    xine->port_ticket->acquire (xine->port_ticket, 1);

    /* inform audio_out that speed has changed - he knows what to do */
    stream->s.audio_out->set_property (stream->s.audio_out, AO_PROP_CLOCK_SPEED, speed);

    xine->port_ticket->release (xine->port_ticket, 1);
  }

  if (mode < 2)
    /* master clock is set after resuming the audio device (audio_out loop may continue) */
    xine->x.clock->set_fine_speed (xine->x.clock, speed);
}


/* SPEED_FLAG_IGNORE_CHANGE must be set in stream->speed_change_flags, when entering this function */
static void stop_internal (xine_stream_private_t *stream) {
  lprintf ("status before = %d\n", stream->status);

  if (stream->side_streams[0] == stream)
    stream->demux.start_buffers_sent = 0;
  stream = stream->side_streams[0];

  if ( stream->status == XINE_STATUS_IDLE ||
       stream->status == XINE_STATUS_STOP ) {
    _x_demux_control_end (&stream->s, 0);
    lprintf("ignored");
  } else {
    /* make sure we're not in "paused" state */
    set_speed_internal (stream, XINE_FINE_SPEED_NORMAL);

    /* Don't change status if we're quitting */
    if (stream->status != XINE_STATUS_QUIT)
      stream->status = XINE_STATUS_STOP;
  }
  /*
   * stop demux
   */
  {
    unsigned int u;
    for (u = 0; u < XINE_NUM_SIDE_STREAMS; u++) {
      xine_stream_private_t *side = stream->side_streams[u];
      if (side && side->demux.plugin && side->demux.thread_created) {
        lprintf ("stopping demux\n");
        _x_demux_stop_thread (&side->s);
        lprintf ("demux stopped\n");
      }
    }
  }
  lprintf ("done\n");
}

/* force engine to run at normal speed. if you are about to grab a ticket,
 * do wait for the rare case of a set_fine_speed (0) in progress. */

static void lock_run (xine_stream_private_t *stream, int wait) {
  xine_private_t *xine = (xine_private_t *)stream->s.xine;

  pthread_mutex_lock (&xine->speed_change_lock);
  if (xine->speed_change_flags & SPEED_FLAG_CHANGING) {
    xine->speed_change_flags |= SPEED_FLAG_IGNORE_CHANGE | SPEED_FLAG_WANT_NEW;
    xine->speed_change_new_speed = XINE_FINE_SPEED_NORMAL;
    if (wait) {
      do {
        pthread_cond_wait (&xine->speed_change_done, &xine->speed_change_lock);
      } while (xine->speed_change_flags & SPEED_FLAG_CHANGING);
    }
    pthread_mutex_unlock (&xine->speed_change_lock);
    return;
  }
  xine->speed_change_flags |= SPEED_FLAG_IGNORE_CHANGE;
  pthread_mutex_unlock (&xine->speed_change_lock);

  if (xine->x.clock->speed == XINE_FINE_SPEED_NORMAL)
    return;
  xprintf (&xine->x, XINE_VERBOSITY_DEBUG, "set_speed %d.\n", XINE_FINE_SPEED_NORMAL);
  set_speed_internal (stream, XINE_FINE_SPEED_NORMAL);
}

static void unlock_run (xine_stream_private_t *stream) {
  xine_private_t *xine = (xine_private_t *)stream->s.xine;

  pthread_mutex_lock (&xine->speed_change_lock);
  xine->speed_change_flags &= ~SPEED_FLAG_IGNORE_CHANGE;
  pthread_mutex_unlock (&xine->speed_change_lock);
}

void xine_stop (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s, *m;
  xine_private_t *xine;

  if (!stream)
    return;
  m = stream->side_streams[0];
  xine = (xine_private_t *)m->s.xine;

  pthread_mutex_lock (&m->frontend_lock);
  pthread_cleanup_push (mutex_cleanup, (void *) &m->frontend_lock);

  /* make sure that other threads cannot change the speed, especially pauseing the stream */
  lock_run (m, 1);

  xine->port_ticket->acquire (xine->port_ticket, 1);

  if (m->s.audio_out)
    m->s.audio_out->set_property (m->s.audio_out, AO_PROP_DISCARD_BUFFERS, 1);
  if (m->s.video_out)
    m->s.video_out->set_property (m->s.video_out, VO_PROP_DISCARD_FRAMES, 1);

  stop_internal (m);

  if (m->s.slave && (m->slave_affection & XINE_MASTER_SLAVE_STOP))
    xine_stop (m->s.slave);

  if (m->s.video_out)
    m->s.video_out->set_property (m->s.video_out, VO_PROP_DISCARD_FRAMES, 0);
  if (m->s.audio_out)
    m->s.audio_out->set_property (m->s.audio_out, AO_PROP_DISCARD_BUFFERS, 0);

  xine->port_ticket->release (xine->port_ticket, 1);
  unlock_run (m);

  pthread_cleanup_pop (0);
  pthread_mutex_unlock (&m->frontend_lock);
}


static void close_internal (xine_stream_private_t *stream) {
  xine_stream_private_t *m = stream->side_streams[0];
  xine_private_t *xine = (xine_private_t *)m->s.xine;
  int flush = !m->gapless_switch && !m->finished_naturally;

  if (m->s.slave) {
    xine_close (m->s.slave);
    if (m->slave_is_subtitle) {
      xine_dispose (m->s.slave);
      m->s.slave = NULL;
      m->slave_is_subtitle = 0;
    }
  }

  /* make sure that other threads cannot change the speed.
   * especially pauseing the stream may hold demux waiting for fifo pool or ticket,
   * and thus freeze stop_internal () -> _x_demux_stop_thread () below. */
  lock_run (m, flush);

  if (flush) {
    xine->port_ticket->acquire (xine->port_ticket, 1);

    if (m->s.audio_out)
      m->s.audio_out->set_property (m->s.audio_out, AO_PROP_DISCARD_BUFFERS, 1);
    if (m->s.video_out)
      m->s.video_out->set_property (m->s.video_out, VO_PROP_DISCARD_FRAMES, 1);
  }

  stop_internal (stream);

  if (flush) {
    if (m->s.video_out)
      m->s.video_out->set_property (m->s.video_out, VO_PROP_DISCARD_FRAMES, 0);
    if (m->s.audio_out)
      m->s.audio_out->set_property (m->s.audio_out, AO_PROP_DISCARD_BUFFERS, 0);

    xine->port_ticket->release (xine->port_ticket, 1);
  }

  unlock_run (m);

  if (stream == m) {
    unsigned int u;
    for (u = 0; u < XINE_NUM_SIDE_STREAMS; u++) {
      xine_stream_private_t *side = stream->side_streams[u];
      if (side) {
        if (side->demux.plugin)
          _x_free_demux_plugin (&side->s, &side->demux.plugin);
        if (side->s.input_plugin) {
          _x_free_input_plugin (&side->s, side->s.input_plugin);
          side->s.input_plugin = NULL;
        }
      }
    }
  } else {
    if (stream->demux.plugin)
      _x_free_demux_plugin (&stream->s, &stream->demux.plugin);
    if (stream->s.input_plugin) {
      _x_free_input_plugin (&stream->s, stream->s.input_plugin);
      stream->s.input_plugin = NULL;
    }
  }

  /*
   * reset / free meta info
   * XINE_STREAM_INFO_MAX is at least 99 but the info arrays are sparsely used.
   * Save a lot of mutex/free calls.
   */
  if (stream == m) {
    int i;
    xine_rwlock_wrlock (&stream->info_lock);
    for (i = 0; i < XINE_STREAM_INFO_MAX; i++)
      stream->stream_info[i] = 0;
    xine_rwlock_unlock (&stream->info_lock);
    xine_rwlock_wrlock (&stream->meta_lock);
    for (i = 0; i < XINE_STREAM_INFO_MAX; i++) {
      if (stream->meta_info_public[i]) {
        if (stream->meta_info_public[i] != stream->meta_info[i])
          free (stream->meta_info_public[i]);
        stream->meta_info_public[i] = NULL;
      }
      if (stream->meta_info[i])
        free (stream->meta_info[i]), stream->meta_info[i] = NULL;
    }
    xine_rwlock_unlock (&stream->meta_lock);
  }
  stream->audio_track_map_entries = 0;
  stream->spu_track_map_entries = 0;

  _x_keyframes_set (&stream->s, NULL, 0);
}

void xine_close (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s, *m;
  /* a function that uses pthread_cleanup_push () should not modify its
   * arguments. it also should not modify a variable that optimizes to
   * be the same as an arg ("stream"). this avoids a "may be clobbered
   * by longjmp () or vfork () warning. */

  /* phonon bug */
  if (!stream) {
    printf ("xine_close: BUG: stream = NULL.\n");
    return;
  }

  m = stream->side_streams[0];
  pthread_mutex_lock (&m->frontend_lock);
  pthread_cleanup_push (mutex_cleanup, (void *) &m->frontend_lock);

  close_internal (m);

  /*
   * set status to idle.
   * not putting this into close_internal because it is also called
   * by open_internal.
   */

  /* Don't change status if we're quitting */
  if (m->status != XINE_STATUS_QUIT)
    m->status = XINE_STATUS_IDLE;

  pthread_cleanup_pop (0);
  pthread_mutex_unlock (&m->frontend_lock);
}

static int stream_rewire_audio(xine_post_out_t *output, void *data)
{
  xine_stream_private_t *stream = (xine_stream_private_t *)output->data;
  xine_private_t *xine = (xine_private_t *)stream->s.xine;
  xine_audio_port_t *new_port = (xine_audio_port_t *)data, *old_port;
  uint32_t bits, rate;
  int mode;

  if (!data)
    return 0;

  stream = stream->side_streams[0];

  xine->port_ticket->revoke (xine->port_ticket, XINE_TICKET_FLAG_REWIRE);
  /* just an optimization. Keep engine paused at rewire safe position for subsequent rewires. */
  set_speed_internal (stream, XINE_LIVE_PAUSE_OFF);

  old_port = stream->s.audio_out;
  _x_post_audio_port_ref (new_port);
  if (old_port->status (old_port, &stream->s, &bits, &rate, &mode)) {
    /* register our stream at the new output port */
    (new_port->open) (new_port, &stream->s, bits, rate, mode);
    old_port->close (old_port, &stream->s);
  }
  stream->s.audio_out = new_port;
  _x_post_audio_port_unref (old_port);

  xine->port_ticket->issue (xine->port_ticket, XINE_TICKET_FLAG_REWIRE);

  return 1;
}

static int stream_rewire_video(xine_post_out_t *output, void *data)
{
  xine_stream_private_t *stream = (xine_stream_private_t *)output->data;
  xine_private_t *xine = (xine_private_t *)stream->s.xine;
  xine_video_port_t *new_port = (xine_video_port_t *)data, *old_port;
  int64_t img_duration;
  int width, height;

  if (!data)
    return 0;

  stream = stream->side_streams[0];

  xine->port_ticket->revoke (xine->port_ticket, XINE_TICKET_FLAG_REWIRE);
  /* just an optimization. Keep engine paused at rewire safe position for subsequent rewires. */
  set_speed_internal (stream, XINE_LIVE_PAUSE_OFF);

  old_port = stream->s.video_out;
  _x_post_video_port_ref (new_port);
  if (old_port->status (old_port, &stream->s, &width, &height, &img_duration)) {
    /* register our stream at the new output port */
    (new_port->open) (new_port, &stream->s);
    old_port->close (old_port, &stream->s);
  }
  stream->s.video_out = new_port;
  _x_post_video_port_unref (old_port);

  xine->port_ticket->issue (xine->port_ticket, XINE_TICKET_FLAG_REWIRE);

  return 1;
}

static void video_decoder_update_disable_flush_at_discontinuity (void *s, xine_cfg_entry_t *entry) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  stream->disable_decoder_flush_at_discontinuity = !!entry->num_value;
}

static void _xine_dummy_dest (void *object) {
  (void)object;
}

static void xine_dispose_internal (xine_stream_private_t *stream);

xine_stream_t *xine_stream_new (xine_t *this, xine_audio_port_t *ao, xine_video_port_t *vo) {

  xine_stream_private_t *stream;
  pthread_mutexattr_t attr;

  xprintf (this, XINE_VERBOSITY_DEBUG, "xine_stream_new\n");

  /* create a new stream object */
  stream = calloc (1, sizeof (*stream));
  if (!stream)
    goto err_null;
#ifndef HAVE_ZERO_SAFE_MEM
  /* Do these first, when compiler still knows stream is all zeroed.
   * Let it optimize away this on most systems where clear mem
   * interpretes as 0, 0f or NULL safely.
   */
  stream->s.spu_decoder_plugin     = NULL;
  stream->audio_decoder_plugin     = NULL;
  stream->early_finish_event       = 0;
  stream->delay_finish_event       = 0;
  stream->gapless_switch           = 0;
  stream->keep_ao_driver_open      = 0;
  stream->video_channel            = 0;
  stream->video_decoder_plugin     = NULL;
  stream->counter.headers_audio    = 0;
  stream->counter.headers_video    = 0;
  stream->counter.finisheds_audio  = 0;
  stream->counter.finisheds_video  = 0;
  stream->counter.demuxers_running = 0;
  stream->counter.nbc_refs         = 0;
  stream->counter.nbc              = NULL;
  stream->demux.action_pending     = 0;
  stream->demux.input_caps         = 0;
  stream->demux.thread_created     = 0;
  stream->demux.thread_running     = 0;
  stream->demux.start_buffers_sent = 0;
  stream->err                      = 0;
  stream->broadcaster              = NULL;
  stream->index.array              = NULL;
  stream->s.slave                  = NULL;
  stream->slave_is_subtitle        = 0;
  stream->query_input_plugins[0]   = NULL;
  stream->query_input_plugins[1]   = NULL;
  stream->seekable                 = 0;
  {
    int i;
    for (i = 1; i < XINE_NUM_SIDE_STREAMS; i++)
      stream->side_streams[i] = NULL;
    for (i = 0; i < XINE_STREAM_INFO_MAX; i++) {
      stream->stream_info[i] = 0;
      stream->meta_info_public[i]   = stream->meta_info[i]   = NULL;
    }
  }
#endif
  /* no need to memset again
  _x_extra_info_reset (&stream->ei[0]);
  _x_extra_info_reset (&stream->ei[1]);
  */

  stream->audio_decoder_extra_info = &stream->ei[0];
  stream->video_decoder_extra_info = &stream->ei[1];

  stream->side_streams[0]       = stream;
  stream->id_flag               = 1 << 0;
  stream->s.xine                = this;
  stream->status                = XINE_STATUS_IDLE;

  stream->video_source.name   = "video source";
  stream->video_source.type   = XINE_POST_DATA_VIDEO;
  stream->video_source.data   = &stream->s;
  stream->video_source.rewire = stream_rewire_video;

  stream->audio_source.name   = "audio source";
  stream->audio_source.type   = XINE_POST_DATA_AUDIO;
  stream->audio_source.data   = &stream->s;
  stream->audio_source.rewire = stream_rewire_audio;

  stream->demux.max_seek_bufs        = 0xffffffff;
  stream->s.spu_decoder_streamtype   = -1;
  stream->s.audio_out                = ao;
  stream->audio_channel_user         = -1;
  stream->s.audio_channel_auto       = -1;
  stream->audio_decoder_streamtype   = -1;
  stream->s.spu_channel_auto         = -1;
  stream->s.spu_channel_letterbox    = -1;
  stream->spu_channel_pan_scan       = -1;
  stream->s.spu_channel_user         = -1;
  stream->s.spu_channel              = -1;
  /* Do not flush output when opening/closing yet unused streams (eg subtitle). */
  stream->finished_naturally         = 1;
  stream->s.video_out                = vo;
  stream->s.video_driver             = vo ? vo->driver : NULL;
  stream->video_decoder_streamtype   = -1;
  /* initial master/slave */
  stream->s.master                   = &stream->s;

  /* event queues */
  stream->event.queues = xine_list_new ();
  if (!stream->event.queues)
    goto err_free;

  /* init mutexes and conditions */
  xine_refs_init (&stream->current_extra_info_index, _xine_dummy_dest, stream);
  xine_rwlock_init_default (&stream->info_lock);
  xine_rwlock_init_default (&stream->meta_lock);
  pthread_mutex_init (&stream->demux.lock, NULL);
  pthread_mutex_init (&stream->demux.action_lock, NULL);
  pthread_mutex_init (&stream->demux.pair, NULL);
  pthread_cond_init  (&stream->demux.resume, NULL);
  pthread_mutex_init (&stream->event.lock, NULL);
  pthread_mutex_init (&stream->counter.lock, NULL);
  pthread_cond_init  (&stream->counter.changed, NULL);
  pthread_mutex_init (&stream->first_frame.lock, NULL);
  pthread_cond_init  (&stream->first_frame.reached, NULL);
  pthread_mutex_init (&stream->index.lock, NULL);

  /* warning: frontend_lock is a recursive mutex. it must NOT be
   * used with neither pthread_cond_wait() or pthread_cond_timedwait()
   */
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (&stream->frontend_lock, &attr);
  pthread_mutexattr_destroy(&attr);

  pthread_mutex_lock (&this->streams_lock);

  /* some user config */
  stream->disable_decoder_flush_at_discontinuity = this->config->register_bool (this->config,
    "engine.decoder.disable_flush_at_discontinuity", 0,
    _("disable decoder flush at discontinuity"),
    _("when watching live tv a discontinuity happens for example about every 26.5 hours due to a pts wrap.\n"
      "flushing the decoder at that time causes decoding errors for images after the pts wrap.\n"
      "to avoid the decoding errors, decoder flush at discontinuity should be disabled.\n\n"
      "WARNING: as the flush was introduced to fix some issues when playing DVD still images, it is\n"
      "likely that these issues may reappear in case they haven't been fixed differently meanwhile.\n"),
    20, video_decoder_update_disable_flush_at_discontinuity, stream);

  /* create a metronom */
  stream->s.metronom = _x_metronom_init ( (vo != NULL), (ao != NULL), this);
  if (!stream->s.metronom)
    goto err_mutex;

  /* alloc fifos, init and start decoder threads */
  if (!_x_video_decoder_init (&stream->s))
    goto err_metronom;

  if (!_x_audio_decoder_init (&stream->s))
    goto err_video;

  /* osd */
  if (vo) {
    _x_spu_misc_init (this);
    stream->s.osd_renderer = _x_osd_renderer_init (&stream->s);
  } else
    stream->s.osd_renderer = NULL;

  /* create a reference counter */
  xine_refs_init (&stream->refs, (void (*)(void *))xine_dispose_internal, &stream->s);

  /* register stream */
  xine_list_push_back (this->streams, &stream->s);
  pthread_mutex_unlock (&this->streams_lock);
  return &stream->s;

  /* err_audio: */
  /* _x_audio_decoder_shutdown (&stream->s); */

  err_video:
  _x_video_decoder_shutdown (&stream->s);

  err_metronom:
  stream->s.metronom->exit (stream->s.metronom);

  err_mutex:
  pthread_mutex_unlock  (&this->streams_lock);
  pthread_mutex_destroy (&stream->frontend_lock);
  pthread_mutex_destroy (&stream->index.lock);
  pthread_cond_destroy  (&stream->first_frame.reached);
  pthread_mutex_destroy (&stream->first_frame.lock);
  pthread_cond_destroy  (&stream->counter.changed);
  pthread_mutex_destroy (&stream->counter.lock);
  pthread_mutex_destroy (&stream->event.lock);
  pthread_cond_destroy  (&stream->demux.resume);
  pthread_mutex_destroy (&stream->demux.pair);
  pthread_mutex_destroy (&stream->demux.action_lock);
  pthread_mutex_destroy (&stream->demux.lock);
  xine_rwlock_destroy   (&stream->meta_lock);
  xine_rwlock_destroy   (&stream->info_lock);
  xine_refs_sub (&stream->current_extra_info_index, xine_refs_get (&stream->current_extra_info_index));
  xine_list_delete      (stream->event.queues);

  err_free:
  free (stream);

  err_null:
  return NULL;
}

static void xine_side_dispose_internal (xine_stream_private_t *stream) {
  xine_t *xine = stream->s.xine;

  lprintf ("stream: %p\n", (void*)stream);

  xine->config->unregister_callbacks (xine->config, NULL, NULL, stream, sizeof (*stream));

  {
    xine_stream_private_t *m = stream->side_streams[0];
    unsigned int u;
    xine_rwlock_wrlock (&m->info_lock);
    for (u = 1; u < XINE_NUM_SIDE_STREAMS; u++) {
      if (m->side_streams[u] == stream) {
        m->side_streams[u] = NULL;
        break;
      }
    }
    xine_rwlock_unlock (&m->info_lock);
    if (u < XINE_NUM_SIDE_STREAMS)
      xine_refs_sub (&m->refs, 1);
  }

  /* these are not used in side streams.
  xine_refs_sub (&stream->current_extra_info_index, xine_refs_get (&stream->current_extra_info_index));
  pthread_mutex_destroy (&stream->frontend_lock);
  pthread_mutex_destroy (&stream->index.lock);
  pthread_mutex_destroy (&stream->demux.pair_mutex);
  pthread_mutex_destroy (&stream->event.lock);
  pthread_mutex_destroy (&stream->counter.lock);
  pthread_mutex_destroy (&stream->first_frame.lock);
  pthread_cond_destroy  (&stream->first_frame.reached);
  pthread_cond_destroy  (&stream->counter.changed);
  xine_rwlock_destroy   (&stream->meta_lock);
  xine_rwlock_destroy   (&stream->info_lock);
  */
  pthread_cond_destroy  (&stream->demux.resume);
  pthread_mutex_destroy (&stream->demux.action_lock);
  pthread_mutex_destroy (&stream->demux.lock);

  free (stream->index.array);
  free (stream);
}

xine_stream_t *xine_get_side_stream (xine_stream_t *master, int index) {
  xine_stream_private_t *m = (xine_stream_private_t *)master, *s;

  if (!m || (index < 0) || (index >= XINE_NUM_SIDE_STREAMS))
    return NULL;
  /* no sub-sides, please. */
  m = m->side_streams[0];
  xine_rwlock_rdlock (&m->info_lock);
  s = m->side_streams[index];
  xine_rwlock_unlock (&m->info_lock);
  if (s)
    return &s->s;

  xprintf (m->s.xine, XINE_VERBOSITY_DEBUG, "xine_side_stream_new (%p, %d)\n", (void *)m, index);

  /* create a new stream object */
  s = calloc (1, sizeof (*s));
  if (!s)
    return NULL;

#ifndef HAVE_ZERO_SAFE_MEM
  s->s.spu_decoder_plugin     = NULL;
  s->audio_decoder_plugin     = NULL;
  s->audio_track_map_entries  = 0;
  s->audio_type               = 0;
  s->early_finish_event       = 0;
  s->delay_finish_event       = 0;
  s->gapless_switch           = 0;
  s->keep_ao_driver_open      = 0;
  s->video_channel            = 0;
  s->video_decoder_plugin     = NULL;
  s->spu_track_map_entries    = 0;
  s->counter.headers_audio    = 0;
  s->counter.headers_video    = 0;
  s->counter.finisheds_audio  = 0;
  s->counter.finisheds_video  = 0;
  s->demux.action_pending     = 0;
  s->demux.input_caps         = 0;
  s->demux.thread_created     = 0;
  s->demux.thread_running     = 0;
  s->err                      = 0;
  s->broadcaster              = NULL;
  s->index.array              = NULL;
  s->s.slave                  = NULL;
  s->slave_is_subtitle        = 0;
  s->query_input_plugins[0]   = NULL;
  s->query_input_plugins[1]   = NULL;
  s->seekable                 = 0;
  {
    int i;
    for (i = 1; i < XINE_NUM_SIDE_STREAMS; i++)
      s->side_streams[i] = NULL;
    for (i = 0; i < XINE_STREAM_INFO_MAX; i++) {
      s->stream_info[i] = 0;
      s->meta_info_public[i] = s->meta_info[i] = NULL;
    }
  }
#endif
  /* no need to memset again
  _x_extra_info_reset (&stream->ei[0]);
  _x_extra_info_reset (&stream->ei[1]);
  */

  /* create a reference counter */
  xine_refs_init (&s->refs, (void (*)(void *))xine_side_dispose_internal, &s->s);

  s->audio_decoder_extra_info = m->audio_decoder_extra_info;
  s->video_decoder_extra_info = m->video_decoder_extra_info;

  s->side_streams[0] = m;
  s->id_flag         = 1 << index;
  s->s.xine = m->s.xine;
  s->status = XINE_STATUS_IDLE;

  s->video_source.name   = "video source";
  s->video_source.type   = XINE_POST_DATA_VIDEO;
  s->video_source.data   = &m->s;
  s->video_source.rewire = stream_rewire_video;

  s->audio_source.name   = "audio source";
  s->audio_source.type   = XINE_POST_DATA_AUDIO;
  s->audio_source.data   = &m->s;
  s->audio_source.rewire = stream_rewire_audio;

  s->s.spu_decoder_streamtype   = -1;
  s->s.audio_out                = m->s.audio_out;
  s->audio_channel_user         = -1;
  s->s.audio_channel_auto       = -1;
  s->audio_decoder_streamtype   = -1;
  s->s.spu_channel_auto         = -1;
  s->s.spu_channel_letterbox    = -1;
  s->spu_channel_pan_scan       = -1;
  s->s.spu_channel_user         = -1;
  s->s.spu_channel              = -1;
  /* Do not flush output when opening/closing yet unused streams (eg subtitle). */
  s->finished_naturally         = 1;
  s->s.video_out                = m->s.video_out;
  s->s.video_driver             = m->s.video_driver;
  s->video_decoder_streamtype   = -1;
  /* initial master/slave */
  s->s.master                   = &s->s;

  s->event.queues = m->event.queues;

  /* these are not used in side streams.
  {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init (&attr);
    pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init (&s->frontend_lock, &attr);
    pthread_mutexattr_destroy (&attr);
  }
  xine_refs_init (&stream->current_extra_info_index, _xine_dummy_dest, stream);
  pthread_mutex_init (&s->demux.pair, NULL);
  pthread_mutex_init (&s->index.lock, NULL);
  pthread_mutex_init (&s->event.lock, NULL);
  pthread_mutex_init (&s->counter.lock, NULL);
  pthread_mutex_init (&s->first_frame.lock, NULL);
  pthread_cond_init  (&s->counter.changed, NULL);
  pthread_cond_init  (&s->first_frame.reached, NULL);
  xine_rwlock_init_default (&s->info_lock);
  xine_rwlock_init_default (&s->meta_lock);
  */
  /* init mutexes and conditions */
  pthread_mutex_init (&s->demux.lock, NULL);
  pthread_mutex_init (&s->demux.action_lock, NULL);
  pthread_cond_init  (&s->demux.resume, NULL);

  /* some user config */
  s->disable_decoder_flush_at_discontinuity = m->disable_decoder_flush_at_discontinuity;
  s->s.metronom = m->s.metronom;

  /* this will just link to master */
  s->s.video_fifo = m->s.video_fifo;
  s->s.audio_fifo = m->s.audio_fifo;

  /* osd */
  s->s.osd_renderer = m->s.osd_renderer;

  /* register stream */
  xine_refs_add (&m->refs, 1);
  xine_rwlock_wrlock (&m->info_lock);
  m->side_streams[index] = s;
  xine_rwlock_unlock (&m->info_lock);
  return &s->s;
}

void _x_mrl_unescape (char *mrl) {
  static const uint8_t tab_unhex[256] = {
#define nn 128
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,nn,nn,nn,nn,nn,nn,
    nn,10,11,12,13,14,15,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,10,11,12,13,14,15,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,
    nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn,nn
  };
  const uint8_t *p = (const uint8_t *)mrl;
  uint8_t *q;
  /* dont touch until first %xx */
  while (*p) {
    if (*p == '%') break;
    p++;
  }
  if (!*p)
    return;
  /* now really unescape */
  /* q = (uint8_t *)p ?? */
  q = (uint8_t *)mrl + (p - (const uint8_t *)mrl);
  while (1) {
    uint8_t z = *p++;
    if (z == '%') {
      uint8_t h = tab_unhex[*p];
      if (!(h & nn)) {
        z = h;
        p++;
        h = tab_unhex[*p];
        if (!(h & nn)) {
          z = (z << 4) | h;
          p++;
        }
      } else if (*p == '%') {
        p++;
      }
    }
    *q++ = z;
    if (!z) break;
  }
#undef nn
}

char *_x_mrl_remove_auth(const char *mrl_in)
{
  char *mrl = strdup(mrl_in);
  char *auth, *p, *at, *host_end;

  /* parse protocol */
  if (!(p = strchr(mrl, ':'))) {
    /* no protocol means plain filename */
    return mrl;
  }

  p++; /* skip ':' */
  if (*p == '/') p++;
  if (*p == '/') p++;

  /* authorization (user[:pass]@hostname) */
  auth = p;
  host_end = strchr(p, '/');
  while ((at = strchr(p, '@')) && at < host_end) {
    p = at + 1; /* skip '@' */
  }

  if (p != auth) {
    while (p[-1]) {
      *auth++ = *p++;
    }
  }

  return mrl;
}

/* 0x01 (end), 0x02 (alpha), 0x04 (alnum - + .), 0x08 (:), 0x10 (;), 0x20 (#)  */
static const uint8_t tab_parse[256] = {
   1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0,32, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 4, 0,
   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8,16, 0, 0, 0, 0,
   0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0, 0, 0,
   0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* aka "does path have a protocol prefix" */
static inline int _x_path_looks_like_mrl (const char *path) {
  const uint8_t *p = (const uint8_t *)path;
  if (!(tab_parse[*p++] & 0x02))
    return 0;
  while (tab_parse[*p++] & 0x04) ;
  return (p[-1] == ':') && (p[0] == '/');
}

static int open_internal (xine_stream_private_t *stream, const char *mrl, input_plugin_t *input) {

  static const uint8_t tab_tolower[256] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    ' ','!','"','#','$','%','&','\'','(',')','*','+',',','-','.','/',
    '0','1','2','3','4','5','6','7','8','9',':',';','<','=','>','?',
    '@','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    'p','q','r','s','t','u','v','w','x','y','z','[','\\',']','^','_',
    '`','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    'p','q','r','s','t','u','v','w','x','y','z','{','|','}','~',127,
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
  };

  uint8_t *buf, *name, *args;
  int no_cache = 0;

  if (!mrl) {
    xprintf (stream->s.xine, XINE_VERBOSITY_LOG, _("xine: error while parsing mrl\n"));
    stream->err = XINE_ERROR_MALFORMED_MRL;
    if (stream->status != XINE_STATUS_IDLE)
      stream->status = XINE_STATUS_STOP;
    return 0;
  }

  lprintf ("opening MRL '%s'...\n", mrl);

  /*
   * stop engine if necessary
   */

  close_internal (stream);

  lprintf ("engine should be stopped now\n");

  /*
   * look for a stream_setup in MRL and try finding an input plugin
   */
  buf = malloc (32 + strlen (mrl) + 32);
  if (!buf)
    return 0;
  name = buf + 32;
  args = NULL;
  {
    const uint8_t *p = (const uint8_t *)mrl;
    uint8_t *prot = NULL, *q = name, z;
    /* test protocol prefix */
    if (tab_parse[*p] & 0x02) {
      while (tab_parse[z = *p] & 0x04) p++, *q++ = z;
      if ((q > name) && (z == ':') && (p[1] == '/')) prot = name;
    }
    if (prot) {
      /* split off args at first hash */
      while (!(tab_parse[z = *p] & 0x21)) p++, *q++ = z;
      *q = 0;
      if (z == '#') {
        p++;
        args = ++q;
        while ((*q++ = *p++) != 0) ;
      }
    } else {
      /* raw filename, may contain any number of hashes */
      while (1) {
        struct stat s;
        while (!(tab_parse[z = *p] & 0x21)) p++, *q++ = z;
        *q = 0;
        /* no need to stat when no hashes found */
        if (!args && !z) break;
        if (!stat ((const char *)name, &s)) {
          args = NULL;
          /* no general break yet, beware "/foo/#bar.flv" */
        }
        if (!z) break;
        p++, *q++ = z;
        args = q;
      }
      if (args) args[-1] = 0;
    }
  }
    
  if (!input) {
    /*
     * find an input plugin
     */
    stream->s.input_plugin = _x_find_input_plugin (&stream->s, (const char *)name);
  } else {
    stream->s.input_plugin = input;
  }
  {
    if (stream->s.input_plugin) {
      int res;
      input_class_t *input_class = stream->s.input_plugin->input_class;

      xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: found input plugin  : %s\n"),
                dgettext(input_class->text_domain ? input_class->text_domain : XINE_TEXTDOMAIN,
                         input_class->description));
      if (stream->s.input_plugin->input_class->eject_media)
        stream->eject_class = stream->s.input_plugin->input_class;
      _x_meta_info_set_utf8 (&stream->s, XINE_META_INFO_INPUT_PLUGIN,
        stream->s.input_plugin->input_class->identifier);

      res = (stream->s.input_plugin->open) (stream->s.input_plugin);
      switch(res) {
      case 1: /* Open successfull */
	break;
      case -1: /* Open unsuccessfull, but correct plugin */
	stream->err = XINE_ERROR_INPUT_FAILED;
	_x_flush_events_queues (&stream->s);
        free (buf);
	return 0;
      default:
	xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: input plugin cannot open MRL [%s]\n"),mrl);
        _x_free_input_plugin (&stream->s, stream->s.input_plugin);
	stream->s.input_plugin = NULL;
	stream->err = XINE_ERROR_INPUT_FAILED;
      }
    }
  }

  if (!stream->s.input_plugin) {
    xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: cannot find input plugin for MRL [%s]\n"),mrl);
    stream->err = XINE_ERROR_NO_INPUT_PLUGIN;
    _x_flush_events_queues (&stream->s);
    free (buf);
    return 0;
  }

  if (args) {
    uint8_t *entry = NULL;

    while (*args) {
      /* Turn "WhatAGreat:Bullshit[;]" into key="whatagreat" entry="WhatAGreat" value="Bullshit". */
      uint8_t *key = buf, *value = NULL;
      if (entry)
        xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: no value for \"%s\", ignoring.\n"), entry);
      entry = args;
      {
        uint8_t *spos = args + 31, sval = *spos, *q = key, z;
        *spos = 0;
        while (!(tab_parse[z = *args] & 0x19)) *q++ = tab_tolower[z], args++;
        *q = 0;
        *spos = sval;
        while (!(tab_parse[z = *args] & 0x19)) args++;
        if (z == ':') {
          *args++ = 0;
          value = args;
          while (!(tab_parse[z = *args] & 0x11)) args++;
        }
        if (z == ';')
          *args++ = 0;
      }

      if (!memcmp (key, "demux", 6)) {
        if (value) {
          /* demuxer specified by name */
          char *demux_name = (char *)value;
          _x_mrl_unescape (demux_name);
	  if (!(stream->demux.plugin = _x_find_demux_plugin_by_name (&stream->s, demux_name, stream->s.input_plugin))) {
	    xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: specified demuxer %s failed to start\n"), demux_name);
	    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;
	    stream->status = XINE_STATUS_IDLE;
            free (buf);
	    return 0;
	  }

          _x_meta_info_set_utf8 (&stream->s, XINE_META_INFO_SYSTEMLAYER,
            stream->demux.plugin->demux_class->identifier);
          entry = NULL;
        }
	continue;
      }

      if (!memcmp (key, "save", 5)) {
        if (value) {
          /* filename to save */
          char *filename = (char *)value;
	  input_plugin_t *input_saver;

          _x_mrl_unescape (filename);

	  xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: join rip input plugin\n"));
          input_saver = _x_rip_plugin_get_instance (&stream->s, filename);

	  if( input_saver ) {
            stream->s.input_plugin = input_saver;
	  } else {
            xprintf (stream->s.xine, XINE_VERBOSITY_LOG, _("xine: error opening rip input plugin instance\n"));
	    stream->err = XINE_ERROR_MALFORMED_MRL;
	    stream->status = XINE_STATUS_IDLE;
            free (buf);
	    return 0;
	  }
          entry = NULL;
        }
	continue;
      }

      if (!memcmp (key, "lastdemuxprobe", 15)) {
        if (value) {
          /* all demuxers will be probed before the specified one */
          char *demux_name = (char *)value;
          _x_mrl_unescape (demux_name);
	  if (!(stream->demux.plugin = _x_find_demux_plugin_last_probe (&stream->s, demux_name, stream->s.input_plugin))) {
            xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: last_probed demuxer %s failed to start\n"), demux_name);
	    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;
	    stream->status = XINE_STATUS_IDLE;
            free (buf);
	    return 0;
	  }
	  lprintf ("demux and input plugin found\n");

          _x_meta_info_set_utf8 (&stream->s, XINE_META_INFO_SYSTEMLAYER,
            stream->demux.plugin->demux_class->identifier);
          entry = NULL;
        }
	continue;
      }

      if (!memcmp (key, "novideo", 8)) {
        _x_stream_info_set (&stream->s, XINE_STREAM_INFO_IGNORE_VIDEO, 1);
        xprintf (stream->s.xine, XINE_VERBOSITY_LOG, _("ignoring video\n"));
        entry = NULL;
	continue;
      }

      if (!memcmp (key, "noaudio", 8)) {
        _x_stream_info_set (&stream->s, XINE_STREAM_INFO_IGNORE_AUDIO, 1);
        xprintf (stream->s.xine, XINE_VERBOSITY_LOG, _("ignoring audio\n"));
        entry = NULL;
	continue;
      }

      if (!memcmp (key, "nospu", 6)) {
        _x_stream_info_set (&stream->s, XINE_STREAM_INFO_IGNORE_SPU, 1);
        xprintf (stream->s.xine, XINE_VERBOSITY_LOG, _("ignoring subpicture\n"));
        entry = NULL;
	continue;
      }

      if (!memcmp (key, "nocache", 8)) {
        no_cache = 1;
        xprintf (stream->s.xine, XINE_VERBOSITY_LOG, _("input cache plugin disabled\n"));
        entry = NULL;
	continue;
      }

      if (!memcmp (key, "volume", 7)) {
        if (value) {
          char *volume = (char *)value;
          _x_mrl_unescape (volume);
          xine_set_param (&stream->s, XINE_PARAM_AUDIO_VOLUME, atoi (volume));
          entry = NULL;
        }
	continue;
      }

      if (!memcmp (key, "compression", 12)) {
        if (value) {
          char *compression = (char *)value;
          _x_mrl_unescape (compression);
          xine_set_param (&stream->s, XINE_PARAM_AUDIO_COMPR_LEVEL, atoi (compression));
          entry = NULL;
        }
	continue;
      }

      if (!memcmp (key, "subtitle", 9)) {
        if (value) {
          char *subtitle_mrl = (char *)value;
	  /* unescape for xine_open() if the MRL looks like a raw pathname */
	  if (!_x_path_looks_like_mrl(subtitle_mrl))
	    _x_mrl_unescape(subtitle_mrl);
          stream->s.slave = xine_stream_new (stream->s.xine, NULL, stream->s.video_out);
	  stream->slave_affection = XINE_MASTER_SLAVE_PLAY | XINE_MASTER_SLAVE_STOP;
          if (xine_open (stream->s.slave, subtitle_mrl)) {
            xprintf (stream->s.xine, XINE_VERBOSITY_LOG, _("subtitle mrl opened '%s'\n"), subtitle_mrl);
            stream->s.slave->master = &stream->s;
	    stream->slave_is_subtitle = 1;
	  } else {
            xprintf (stream->s.xine, XINE_VERBOSITY_LOG, _("xine: error opening subtitle mrl\n"));
            xine_dispose (stream->s.slave);
            stream->s.slave = NULL;
	  }
          entry = NULL;
        }
	continue;
      }

      if (value) {
        /* when we got here, the stream setup parameter must be a config entry */
        int retval;
        value[-1] = ':';
        _x_mrl_unescape ((char *)entry);
        retval = _x_config_change_opt (stream->s.xine->config, (char *)entry);
	if (retval <= 0) {
          value[-1] = 0;
	  if (retval == 0) {
            /* the option not found */
            xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: unknown config option \"%s\", ignoring.\n"), entry);
	  } else {
            /* not permitted to change from MRL */
            xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: changing option '%s' from MRL isn't permitted\n"), entry);
	  }
	}
        entry = NULL;
      }
    }
    if (entry)
      xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: no value for \"%s\", ignoring.\n"), entry);
  }

  free (buf);

  /* Nasty xine-ui issue:
   * 1. User pauses playback, then opens a playlist.
   * 2. Xine-ui grabs a separate stream tied to our "none" output plugins.
   * 3. Xine-ui tries to open every playlist item in turn in order to query some info like duration.
   *    We dont discuss the performance and security implications thereof here.
   *    But we do find that any such open attempt will freeze inside _x_demux_control_headers_done ()
   *    below because the engine is still paused.
   * Workaround:
   *    If fifo's are (nearly) empty, try that live pause hack.
   *    If not, unpause for real. */
  if (_x_get_fine_speed (&stream->s) == XINE_SPEED_PAUSE) {
    if  (stream->s.audio_fifo && (stream->s.audio_fifo->fifo_size < 10)
      && stream->s.video_fifo && (stream->s.video_fifo->fifo_size < 10)) {
      xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
        "xine_open: switching to live pause mode to avoid freeze.\n");
      set_speed_internal (stream->side_streams[0], XINE_LIVE_PAUSE_ON);
    } else {
      xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
        "xine_open: unpauseing to avoid freeze.\n");
      set_speed_internal (stream->side_streams[0], XINE_FINE_SPEED_NORMAL);
    }
  }

  no_cache = no_cache || (stream->s.input_plugin->get_capabilities (stream->s.input_plugin) & INPUT_CAP_NO_CACHE);
  if( !no_cache )
    /* enable buffered input plugin (request optimizer) */
    stream->s.input_plugin = _x_cache_plugin_get_instance (&stream->s);

  /* Let the plugin request a specific demuxer (if the user hasn't).
   * This overrides find-by-content & find-by-extension.
   */
  if (!stream->demux.plugin)
  {
    char *default_demux = NULL;
    stream->s.input_plugin->get_optional_data (stream->s.input_plugin, &default_demux, INPUT_OPTIONAL_DATA_DEMUXER);
    if (default_demux)
    {
      stream->demux.plugin = _x_find_demux_plugin_by_name (&stream->s, default_demux, stream->s.input_plugin);
      if (stream->demux.plugin)
      {
        lprintf ("demux and input plugin found\n");
        _x_meta_info_set_utf8 (&stream->s, XINE_META_INFO_SYSTEMLAYER,
          stream->demux.plugin->demux_class->identifier);
      }
      else
        xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: couldn't load plugin-specified demux %s for >%s<\n"), default_demux, mrl);
    }
  }

  if (!stream->demux.plugin) {

    /*
     * find a demux plugin
     */
    if (!(stream->demux.plugin = _x_find_demux_plugin (&stream->s, stream->s.input_plugin))) {
      xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: couldn't find demux for >%s<\n"), mrl);
      stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;

      stream->status = XINE_STATUS_IDLE;

      /* force the engine to unregister fifo callbacks */
      _x_demux_control_nop (&stream->s, BUF_FLAG_END_STREAM);

      return 0;
    }
    lprintf ("demux and input plugin found\n");

    _x_meta_info_set_utf8 (&stream->s, XINE_META_INFO_SYSTEMLAYER,
      stream->demux.plugin->demux_class->identifier);
  }

  {
    demux_class_t *demux_class = stream->demux.plugin->demux_class;
    xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: found demuxer plugin: %s\n"),
              dgettext(demux_class->text_domain ? demux_class->text_domain : XINE_TEXTDOMAIN,
                       demux_class->description));
  }

  _x_extra_info_reset( stream->current_extra_info );
  _x_extra_info_reset( stream->video_decoder_extra_info );
  _x_extra_info_reset( stream->audio_decoder_extra_info );

  /* assume handled for now. we will only know for sure after trying
   * to init decoders (which should happen when headers are sent)
   */
  _x_stream_info_set (&stream->s, XINE_STREAM_INFO_VIDEO_HANDLED, 1);
  _x_stream_info_set (&stream->s, XINE_STREAM_INFO_AUDIO_HANDLED, 1);

  /*
   * send and decode headers
   */

  stream->demux.plugin->send_headers (stream->demux.plugin);

  if (stream->demux.plugin->get_status (stream->demux.plugin) != DEMUX_OK) {
    if (stream->demux.plugin->get_status (stream->demux.plugin) == DEMUX_FINISHED) {
      xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: demuxer is already done. that was fast!\n"));
    } else {
      xine_log (stream->s.xine, XINE_LOG_MSG, _("xine: demuxer failed to start\n"));
    }

    _x_free_demux_plugin (&stream->s, &stream->demux.plugin);

    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "demux disposed\n");

    _x_free_input_plugin (&stream->s, stream->s.input_plugin);
    stream->s.input_plugin = NULL;
    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;

    stream->status = XINE_STATUS_IDLE;

    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "return from\n");
    return 0;
  }

  _x_demux_control_headers_done (&stream->s);

  stream->status = XINE_STATUS_STOP;

  lprintf ("done\n");
  return 1;
}

int xine_open (xine_stream_t *s, const char *mrl) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  xine_private_t *xine = (xine_private_t *)stream->s.xine;
  pthread_mutex_t *frontend_lock = &stream->side_streams[0]->frontend_lock;
  int ret, sn;

  pthread_mutex_lock (frontend_lock);
  pthread_cleanup_push (mutex_cleanup, (void *) frontend_lock);

  lprintf ("open MRL:%s\n", mrl);

  ret = open_internal (stream, mrl, NULL);

  sn = 0;
  if (xine->join_av && mrl && (stream->side_streams[0] == stream)) do {
    char nbuf[1024];
    struct stat st;
    size_t nlen;
    xine_stream_private_t *side;
    const uint8_t *p, *n, *d;
    const char *orig = mrl;
    if (!strncasecmp (orig, "file:/", 6))
      orig += 6;
    n = d = p = (const uint8_t *)orig;
    while (1) {
      while (*p >= 0x30)
        p++;
      if ((*p == 0) || (*p == '#'))
        break;
      if (*p == '/') {
        p++;
        n = d = p;
      } else if (*p == '.') {
        d = p;
        p++;
      } else {
        p++;
      }
    }
    nlen = p - (const uint8_t *)orig;
    if (nlen > sizeof (nbuf) - 1)
      break;
    if ((n + 2 > d) || (d[-2] != '_') || (d[0] != '.'))
      break;
    if ((d[-1] != 'a') && (d[-1] != 'v'))
      break;
    if (stat (orig, &st))
      break;
    if (!S_ISREG (st.st_mode))
      break;
    memcpy (nbuf, orig, nlen);
    nbuf[nlen] = 0;
    nbuf[d - (const uint8_t *)orig - 1] = d[-1] == 'a' ? 'v' : 'a';
    if (stat (nbuf, &st))
      break;
    if (!S_ISREG (st.st_mode))
      break;
    side = (xine_stream_private_t *)xine_get_side_stream (&stream->s, 1);
    if (!side)
      break;
    xprintf (&xine->x, XINE_VERBOSITY_DEBUG,
      "xine_open: auto joining \"%s\" with \"%s\".\n", orig, nbuf);
    open_internal (side, nbuf, NULL);
    sn = 1;
  } while (0);

  if (!sn && mrl && (stream->side_streams[0] == stream)) do {
    input_plugin_t *main_input = stream->s.input_plugin;

    if (!main_input)
      break;
    for (sn = 1; sn < (int)(sizeof (stream->side_streams) / sizeof (stream->side_streams[0])); sn++) {
      xine_stream_private_t *side;
      union {
        int index;
        input_plugin_t *input;
      } si;

      si.index = sn;
      if (main_input->get_optional_data (main_input, &si, INPUT_OPTIONAL_DATA_SIDE) != INPUT_OPTIONAL_SUCCESS)
        break;
      side = (xine_stream_private_t *)xine_get_side_stream (&stream->s, sn);
      if (!side) {
        si.input->dispose (si.input);
        break;
      }
      xprintf (&xine->x, XINE_VERBOSITY_DEBUG,
        "xine_open: adding side stream #%d (%p).\n", sn, (void *)side);
      open_internal (side, mrl, si.input);
    }
    sn--;
  } while (0);

  pthread_cleanup_pop (0);
  pthread_mutex_unlock (frontend_lock);

  return ret;
}

static void wait_first_frame (xine_stream_private_t *stream) {
  if (stream->video_decoder_plugin || stream->audio_decoder_plugin) {
    pthread_mutex_lock (&stream->first_frame.lock);
    if (stream->first_frame.flag > 0) {
      struct timespec ts = {0, 0};
      xine_gettime (&ts);
      ts.tv_sec += 10;
      pthread_cond_timedwait(&stream->first_frame.reached, &stream->first_frame.lock, &ts);
    }
    pthread_mutex_unlock (&stream->first_frame.lock);
  }
}

static int play_internal (xine_stream_private_t *stream, int start_pos, int start_time) {
  xine_private_t *xine = (xine_private_t *)stream->s.xine;
  int        flush;
  int        first_frame_flag = 3;
  int        demux_status;
  uint32_t   input_is_seekable = 1;
  struct     timespec ts1 = {0, 0}, ts2 = {0, 0};
  struct {
    xine_stream_private_t *s;
    uint32_t flags;
  } sides[XINE_NUM_SIDE_STREAMS + 1], *sp, *sp2;

  if (stream->s.xine->verbosity >= XINE_VERBOSITY_DEBUG) {
    xine_gettime (&ts1);
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
      "play_internal (%d.%03d, %d)\n", start_time / 1000, start_time % 1000, start_pos);
  }

  /* list the streams (master and sides) with a demux. */
  {
    unsigned int u;
    sp = sides;
    xine_rwlock_rdlock (&stream->info_lock);
    for (u = 0; u < XINE_NUM_SIDE_STREAMS; u++) {
      xine_stream_private_t *side = stream->side_streams[u];
      if (side && side->demux.plugin) {
        sp->s = side;
        sp->flags = 0;
        sp++;
      }
    }
    xine_rwlock_unlock (&stream->info_lock);
    sp->s = NULL;
    sp->flags = 0;
  }

  if (!sides[0].s) {
    xine_log (stream->s.xine, XINE_LOG_MSG, _("xine_play: no demux available\n"));
    stream->err = XINE_ERROR_NO_DEMUX_PLUGIN;

    return 0;
  }

  if (start_pos || start_time) {
    stream->finished_naturally = 0;
    first_frame_flag = 2;
  }
  flush = (stream->s.master == &stream->s) && !stream->gapless_switch && !stream->finished_naturally;
  if (!flush)
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "play_internal: using gapless switch.\n");

  /* hint demuxer thread we want to interrupt it */
  sp = sides;
  do {
    pthread_mutex_lock (&sp->s->demux.action_lock);
    sp->s->demux.action_pending += 0x10001;
    if (!(sp->s->demux.input_caps & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE | INPUT_CAP_TIME_SEEKABLE)))
      input_is_seekable = 0;
    pthread_mutex_unlock (&sp->s->demux.action_lock);
    sp++;
  } while (sp->s);

  if (input_is_seekable != stream->seekable) {
    static const char * const fbc_mode[4] = {"off", "a", "v", "av"};
    int on;
    stream->seekable = input_is_seekable;
    on = (xine_fbc_set (stream->s.audio_fifo, input_is_seekable) ? 1 : 0)
       | (xine_fbc_set (stream->s.audio_fifo, input_is_seekable) ? 2 : 0);
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
        "file_buf_ctrl: %s.\n", fbc_mode[on]);
  }

  /* WTF??
   * TJ. OK these calls involve lock/unlock of the fifo mutexes.
   * Demux will do that again later by fifo->put ().
   * At least with x86 Linux, such a sequence forces a data cache sync from this thread
   * to the demux thread. This way, demux will see demux.action_pending > 0 early,
   * without the need to grab demux.action_lock for every iteration.
   * Reduces response delay by average 20ms. */
  (void)stream->s.video_fifo->size (stream->s.video_fifo);
  (void)stream->s.audio_fifo->size (stream->s.audio_fifo);

  /* ignore speed changes (net_buf_ctrl causes deadlocks while seeking ...) */
  lock_run (stream, 1);

  xine->port_ticket->acquire (xine->port_ticket, 1);

  /* only flush/discard output ports on master streams */
  if (flush) {
    /* discard audio/video buffers to get engine going and take the lock faster */
    if (stream->s.audio_out)
      stream->s.audio_out->set_property (stream->s.audio_out, AO_PROP_DISCARD_BUFFERS, 1);
    if (stream->s.video_out)
      stream->s.video_out->set_property (stream->s.video_out, VO_PROP_DISCARD_FRAMES, 1);
    /* freeze safety (discontinuity wait?) when there are multiple streams.
     * or, when all input is seekable, suspend faster this way. */
    if (sides[1].s || input_is_seekable) {
      stream->s.video_fifo->clear (stream->s.video_fifo);
      stream->s.audio_fifo->clear (stream->s.audio_fifo);
    }
  }

  sp = sides;
  do {
    pthread_mutex_lock (&sp->s->demux.lock);
    /* demux.lock taken. now demuxer is suspended. unblock io for seeking. */
    pthread_mutex_lock (&sp->s->demux.action_lock);
    sp->s->demux.action_pending -= 0x00001;
    pthread_mutex_unlock (&sp->s->demux.action_lock);
    sp++;
  } while (sp->s);

  if (stream->s.xine->verbosity >= XINE_VERBOSITY_DEBUG) {
    int diff;
    xine_gettime (&ts2);
    diff = (int)(ts2.tv_nsec - ts1.tv_nsec) / 1000000;
    diff += (ts2.tv_sec - ts1.tv_sec) * 1000;
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "play_internal: ...demux suspended after %dms.\n", diff);
  }

  /* set normal speed again (now that demuxer/input pair is suspended)
   * some input plugin may have changed speed by itself, we must ensure
   * the engine is not paused.
   */
  if (_x_get_fine_speed (&stream->s) != XINE_FINE_SPEED_NORMAL)
    set_speed_internal (stream, XINE_FINE_SPEED_NORMAL);

  /*
   * start/seek demux
   */

  pthread_mutex_lock (&stream->demux.pair);
  stream->demux.max_seek_bufs = sides[1].s ? 1 : 0xffffffff;
  pthread_mutex_unlock (&stream->demux.pair);

  /* seek to new position (no data is sent to decoders yet) */
  sp = sides;
  do {
    xine_stream_private_t *s;
    int r;
    int32_t vtime = 0;
    if (!sides[1].s)
      break;
    sp2 = sides;
    do {
      if (sp2->s->demux.plugin->get_capabilities (sp2->s->demux.plugin) & DEMUX_CAP_VIDEO_TIME)
        break;
      sp2++;
    } while (sp2->s);
    if (!sp2->s)
      break;
    s = sp2->s;
    sp2->s = sides[0].s;
    sides[0].s = s;
    sp++;
    r = sides[0].s->demux.plugin->seek (sides[0].s->demux.plugin,
      start_pos, start_time, sides[0].s->demux.thread_running);
    sides[0].flags = r == DEMUX_OK ? 1 : 0;
    if (r != DEMUX_OK)
      break;
    if (sides[0].s->demux.plugin->get_optional_data (sides[0].s->demux.plugin, &vtime,
        DEMUX_OPTIONAL_DATA_VIDEO_TIME) != DEMUX_OPTIONAL_SUCCESS)
      break;
    start_pos = 0;
    start_time = vtime;
  } while (0);
  do {
    int r = sp->s->demux.plugin->seek (sp->s->demux.plugin,
      start_pos, start_time, sp->s->demux.thread_running);
    sp->flags = r == DEMUX_OK ? 1 : 0;
    sp++;
  } while (sp->s);

  /* only flush/discard output ports on master streams */
  if (flush) {
    if (stream->s.audio_out)
      stream->s.audio_out->set_property (stream->s.audio_out, AO_PROP_DISCARD_BUFFERS, 0);
    if (stream->s.video_out)
      stream->s.video_out->set_property (stream->s.video_out, VO_PROP_DISCARD_FRAMES, 0);
  } else {
    stream->s.metronom->handle_audio_discontinuity (stream->s.metronom, DISC_GAPLESS, 0);
  }

  xine->port_ticket->release (xine->port_ticket, 1);
  unlock_run (stream);

  /* before resuming the demuxer, set first_frame_flag */
  pthread_mutex_lock (&stream->first_frame.lock);
  stream->first_frame.flag = first_frame_flag;
  pthread_mutex_unlock (&stream->first_frame.lock);

  /* before resuming the demuxer, reset current position information */
  xine_current_extra_info_reset (stream);

  /* now resume demuxer thread if it is running already */
  demux_status = 0;
  sp = sides;
  do {
    sp->flags |= sp->s->demux.thread_running ? 2 : 0;
    pthread_mutex_unlock (&sp->s->demux.lock);
    /* now that demux lock is released, resume demux. */
    pthread_mutex_lock (&sp->s->demux.action_lock);
    sp->s->demux.action_pending -= 0x10000;
    if (sp->s->demux.action_pending <= 0)
      pthread_cond_signal (&sp->s->demux.resume);
    pthread_mutex_unlock (&sp->s->demux.action_lock);
    /* seek OK but not running? try restart. */
    if (sp->flags == 1) {
      if (_x_demux_start_thread (&sp->s->s) >= 0)
        sp->flags = 3;
    }
    if (sp->flags == 3)
      demux_status++;
    sp++;
  } while (sp->s);

  if (!demux_status)
    goto demux_failed;

  stream->status = XINE_STATUS_PLAY;
  stream->finished_naturally = 0;


  /* Wait until the first frame produced is displayed
   * see video_out.c
   */
  wait_first_frame (stream);
  {
    extra_info_t info;
    int video_seek_count = xine_current_extra_info_get (stream, &info);
    if (info.seek_count != video_seek_count)
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "play_internal: warning: seek count still %d != %d.\n",
      info.seek_count, video_seek_count);
  }

  if (stream->s.xine->verbosity >= XINE_VERBOSITY_DEBUG) {
    int diff;
    xine_gettime (&ts2);
    diff = (int)(ts2.tv_nsec - ts1.tv_nsec) / 1000000;
    diff += (ts2.tv_sec - ts1.tv_sec) * 1000;
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "play_internal: ...done after %dms.\n", diff);
  }

  return 1;

 demux_failed:
  xine_log (stream->s.xine, XINE_LOG_MSG, _("xine_play: demux failed to start\n"));

  stream->err = XINE_ERROR_DEMUX_FAILED;
  pthread_mutex_lock (&stream->first_frame.lock);
  stream->first_frame.flag = 0;
  // no need to signal: wait is done only in this function.
  pthread_mutex_unlock (&stream->first_frame.lock);
  return 0;
}

int xine_play (xine_stream_t *s, int start_pos, int start_time) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s, *m;
  int ret;

  if (!stream)
    return 0;
  m = stream->side_streams[0];

  pthread_mutex_lock (&m->frontend_lock);
  pthread_cleanup_push (mutex_cleanup, (void *) &m->frontend_lock);

  m->delay_finish_event = 0;

  ret = play_internal (m, start_pos, start_time);
  if (m->s.slave && (m->slave_affection & XINE_MASTER_SLAVE_PLAY) )
    xine_play (m->s.slave, start_pos, start_time);

  m->gapless_switch = 0;

  pthread_cleanup_pop (0);
  pthread_mutex_unlock (&m->frontend_lock);

  return ret;
}

int xine_eject (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s, *m;
  int status;

  if (!stream)
    return 0;
  m = stream->side_streams[0];

  if (!m->eject_class)
    return 0;

  pthread_mutex_lock (&m->frontend_lock);
  pthread_cleanup_push (mutex_cleanup, (void *) &m->frontend_lock);

  status = 0;
  /* only eject, if we are stopped OR a different input plugin is playing */
  if (m->eject_class && m->eject_class->eject_media &&
      ((m->status == XINE_STATUS_STOP) ||
      m->eject_class != m->s.input_plugin->input_class)) {

    status = m->eject_class->eject_media (m->eject_class);
  }

  pthread_cleanup_pop (0);
  pthread_mutex_unlock (&m->frontend_lock);

  return status;
}

static void xine_dispose_internal (xine_stream_private_t *stream) {
  xine_t *xine = stream->s.xine;

  lprintf("stream: %p\n", (void*)stream);

  xine->config->unregister_callbacks (xine->config, NULL, NULL, stream, sizeof (*stream));

  pthread_mutex_lock (&xine->streams_lock);
  {
    xine_list_iterator_t ite = xine_list_find (xine->streams, stream);
    if (ite)
      xine_list_remove (xine->streams, ite);
  }
  /* keep xine instance open for this */
  stream->s.metronom->exit (stream->s.metronom);
  pthread_mutex_unlock (&xine->streams_lock);

  pthread_mutex_destroy (&stream->frontend_lock);
  pthread_mutex_destroy (&stream->index.lock);
  pthread_cond_destroy  (&stream->first_frame.reached);
  pthread_mutex_destroy (&stream->first_frame.lock);
  pthread_cond_destroy  (&stream->counter.changed);
  pthread_mutex_destroy (&stream->counter.lock);
  pthread_mutex_destroy (&stream->event.lock);
  pthread_cond_destroy  (&stream->demux.resume);
  pthread_mutex_destroy (&stream->demux.pair);
  pthread_mutex_destroy (&stream->demux.action_lock);
  pthread_mutex_destroy (&stream->demux.lock);
  xine_rwlock_destroy   (&stream->meta_lock);
  xine_rwlock_destroy   (&stream->info_lock);

  xine_refs_sub (&stream->current_extra_info_index, xine_refs_get (&stream->current_extra_info_index));

  xine_list_delete (stream->event.queues);

  free (stream->index.array);
  free (stream);
}

void xine_dispose (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  /* decrease the reference counter
   * if there is no more reference on this stream, the xine_dispose_internal
   * function is called
   */
  if (!stream)
    return;
  /* never dispose side streams directly. */
  if (stream->side_streams[0] != stream)
    return;

  xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "xine_dispose\n");
  stream->status = XINE_STATUS_QUIT;

  xine_close (&stream->s);

  if (stream->s.master != &stream->s) {
    stream->s.master->slave = NULL;
  }
  if (stream->s.slave && (stream->s.slave->master == &stream->s)) {
    stream->s.slave->master = NULL;
  }

  {
    unsigned int u;
    for (u = 1; u < XINE_NUM_SIDE_STREAMS; u++) {
      xine_stream_private_t *side = stream->side_streams[u];
      if (side)
        xine_refs_sub (&side->refs, 1);
    }
  }

  if(stream->broadcaster)
    _x_close_broadcaster(stream->broadcaster);

  /* Demuxers mpeg, mpeg_block and yuv_frames may send video pool buffers
   * to audio fifo. Fifos should be empty at this point. For safety,
   * shut down audio first anyway.
   */
  xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "shutdown audio\n");
  _x_audio_decoder_shutdown (&stream->s);

  xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG, "shutdown video\n");
  _x_video_decoder_shutdown (&stream->s);

  if (stream->s.osd_renderer)
    stream->s.osd_renderer->close (stream->s.osd_renderer);

  /* Remove the reference that the stream was created with. */
  xine_refs_sub (&stream->refs, 1);
}

#ifdef WIN32
static int xine_wsa_users = 0;
#endif

void xine_exit (xine_t *this_gen) {
  xine_private_t *this = (xine_private_t *)this_gen;
  if (this->x.streams) {
    int n = 10;
    /* XXX: streams kill themselves via their refs hook. */
    while (n--) {
      xine_stream_private_t *stream = NULL;
      xine_list_iterator_t ite;

      pthread_mutex_lock (&this->x.streams_lock);
      ite = NULL;
      while (1) {
        stream = xine_list_next_value (this->x.streams, &ite);
        if (!ite || (stream && (&stream->s != XINE_ANON_STREAM)))
          break;
      }
      if (!ite) {
        pthread_mutex_unlock (&this->x.streams_lock);
        break;
      }
      /* stream->refcounter->lock might be taken already */
      {
        int i = xine_refs_add (&stream->refs, 0);
        pthread_mutex_unlock (&this->x.streams_lock);
        xprintf (&this->x, XINE_VERBOSITY_LOG,
                 "xine_exit: BUG: stream %p still open (%d refs), waiting.\n",
                 (void*)stream, i);
      }
      if (n) {
        xine_usec_sleep (50000);
      } else {
#ifdef FORCE_STREAM_SHUTDOWN
        /* might raise even more heap damage, disabled for now */
        xprintf (&this->x, XINE_VERBOSITY_LOG,
                 "xine_exit: closing stream %p.\n",
                 (void*)stream);
        stream->refcounter->count = 1;
        xine_dispose (stream);
        n = 1;
#endif
      }
    }
    xine_list_delete (this->x.streams);
    pthread_mutex_destroy (&this->x.streams_lock);
  }

  if (this->x.config)
    this->x.config->unregister_callbacks (this->x.config, NULL, NULL, this, sizeof (*this));

  xprintf (&this->x, XINE_VERBOSITY_DEBUG, "xine_exit: bye!\n");

  _x_dispose_plugins (&this->x);

  if (this->x.clock)
    this->x.clock->exit (this->x.clock);

  if (this->x.config)
    this->x.config->dispose (this->x.config);

  if(this->port_ticket)
    this->port_ticket->dispose(this->port_ticket);

  pthread_cond_destroy (&this->speed_change_done);
  pthread_mutex_destroy (&this->speed_change_lock);

  {
    int i;
    for (i = 0; i < XINE_LOG_NUM; i++)
      if (this->x.log_buffers[i])
        this->x.log_buffers[i]->dispose (this->x.log_buffers[i]);
  }
  pthread_mutex_destroy(&this->log_lock);

#if defined(WIN32)
  if (xine_wsa_users) {
    if (--xine_wsa_users == 0)
      WSACleanup ();
  }
#endif

  xdgWipeHandle (&this->x.basedir_handle);

  free (this);
}

xine_t *xine_new (void) {
  xine_private_t *this;

  this = calloc (1, sizeof (*this));
  if (!this)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  this->x.plugin_catalog = NULL;
  this->x.save_path      = NULL;
  this->x.streams        = NULL;
  this->x.clock          = NULL;
  this->port_ticket      = NULL;
  this->speed_change_flags = 0;
#endif

  pthread_mutex_init (&this->speed_change_lock, NULL);
  pthread_cond_init (&this->speed_change_done, NULL);

#ifdef ENABLE_NLS
  /*
   * i18n
   */

  bindtextdomain(XINE_TEXTDOMAIN, XINE_LOCALEDIR);
#endif

  /*
   * config
   */

  this->x.config = _x_config_init ();
  if (!this->x.config) {
    free(this);
    return NULL;
  }

  /*
   * log buffers
   */
  memset (this->x.log_buffers, 0, sizeof (this->x.log_buffers));
  pthread_mutex_init (&this->log_lock, NULL);

#ifdef WIN32
  if (!xine_wsa_users) {
    /* WinSock Library Init. */
    WSADATA Data;
    int i_err = WSAStartup (MAKEWORD (1, 1), &Data);
    if (i_err) {
      fprintf (stderr, "error: can't initiate WinSocks, error %i\n", i_err);
    } else {
      xine_wsa_users++;
    }
  } else {
    xine_wsa_users++;
  }
#endif /* WIN32 */

  this->x.verbosity = XINE_VERBOSITY_NONE;

  return &this->x;
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

static void config_demux_strategy_cb (void *this_gen, xine_cfg_entry_t *entry) {
  xine_t *this = (xine_t *)this_gen;

  this->demux_strategy = entry->num_value;
}

static void config_save_cb (void *this_gen, xine_cfg_entry_t *entry) {
  xine_t *this = (xine_t *)this_gen;
  char homedir_trail_slash[strlen(xine_get_homedir()) + 2];

  sprintf(homedir_trail_slash, "%s/", xine_get_homedir());
  if (entry->str_value[0] &&
      (entry->str_value[0] != '/' || strstr(entry->str_value, "/.") ||
       strcmp(entry->str_value, xine_get_homedir()) == 0 ||
       strcmp(entry->str_value, homedir_trail_slash) == 0)) {
    xine_stream_t *stream;
    xine_list_iterator_t ite;

    xine_log(this, XINE_LOG_MSG,
	     _("xine: The specified save_dir \"%s\" might be a security risk.\n"), entry->str_value);

    pthread_mutex_lock(&this->streams_lock);
    ite = NULL;
    if ((stream = xine_list_next_value (this->streams, &ite))) {
      _x_message(stream, XINE_MSG_SECURITY, _("The specified save_dir might be a security risk."), NULL);
    }
    pthread_mutex_unlock(&this->streams_lock);
  }

  this->save_path = entry->str_value;
}

void xine_set_flags (xine_t *this_gen, int flags)
{
  xine_private_t *this = (xine_private_t *)this_gen;
  this->flags = flags;
}

int _x_query_network_timeout (xine_t *xine_gen) {
  xine_private_t *xine = (xine_private_t *)xine_gen;
  return xine ? xine->network_timeout : 30;
}

static void network_timeout_cb (void *this_gen, xine_cfg_entry_t *entry) {
  xine_private_t *this = (xine_private_t *)this_gen;
  this->network_timeout = entry->num_value;
}

#ifdef ENABLE_IPV6
static void ip_pref_cb (void *this_gen, xine_cfg_entry_t *entry) {
  xine_private_t *this = (xine_private_t *)this_gen;
  this->ip_pref = entry->num_value;
}
#endif

static void join_av_cb (void *this_gen, xine_cfg_entry_t *entry) {
  xine_private_t *this = (xine_private_t *)this_gen;
  this->join_av = entry->num_value;
}

void xine_init (xine_t *this_gen) {
  xine_private_t *this = (xine_private_t *)this_gen;

  /* First of all, initialise libxdg-basedir as it's used by plugins. */
  setenv ("HOME", xine_get_homedir (), 0); /* libxdg-basedir needs $HOME */
  xdgInitHandle (&this->x.basedir_handle);

  /* debug verbosity override */
  {
    int v = 0;
    const uint8_t *s = (const uint8_t *)getenv ("LIBXINE_VERBOSITY"), *t = s;
    uint8_t z;
    if (s) {
      while ((z = *s++ ^ '0') < 10)
        v = 10 * v + z;
      if (s > t + 1)
        this->x.verbosity = v;
    }
  }

  /*
   * locks
   */
  pthread_mutex_init (&this->x.streams_lock, NULL);

  /* initialize color conversion tables and functions */
  init_yuv_conversion();

  /* probe for optimized memcpy or config setting */
  xine_probe_fast_memcpy (&this->x);

  /*
   * plugins
   */
  XINE_PROFILE (_x_scan_plugins (&this->x));

#ifdef HAVE_SETLOCALE
  if (!setlocale(LC_CTYPE, ""))
    xprintf (&this->x, XINE_VERBOSITY_LOG, _("xine: locale not supported by C library\n"));
#endif

  /*
   * content detection strategy
   */
  {
    static const char *const demux_strategies[] = {"default", "reverse", "content", "extension", NULL};
    this->x.demux_strategy = this->x.config->register_enum (
      this->x.config, "engine.demux.strategy", 0,
      (char **)demux_strategies,
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
      20, config_demux_strategy_cb, this);
  }

  /*
   * save directory
   */
  this->x.save_path  = this->x.config->register_filename (
      this->x.config,
      "media.capture.save_dir", "", XINE_CONFIG_STRING_IS_DIRECTORY_NAME,
      _("directory for saving streams"),
      _("When using the stream save feature, files will be written only into this directory.\n"
	"This setting is security critical, because when changed to a different directory, xine "
	"can be used to fill files in it with arbitrary content. So you should be careful that "
	"the directory you specify is robust against any content in any file."),
      XINE_CONFIG_SECURITY, config_save_cb, this);

  /*
   * implicit configuration changes
   */
  this->x.config->register_bool (this->x.config,
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
   * timeout for network I/O to avoid freezes
   */
  this->network_timeout = this->x.config->register_num (this->x.config,
      "media.network.timeout", 30,
      _("Timeout for network stream reading (in seconds)"),
      _("Specifies the timeout when reading from network streams, in seconds. "
        "Too low values might stop streaming when the source is slow or the "
        "bandwidth is occupied, too high values will freeze the player if the "
        "connection is lost."),
      0, network_timeout_cb, this);

#ifdef ENABLE_IPV6
  /*
   * network ip version
   */
  {
    static const char *const ip_versions[] = {"auto", "IPv4", "IPv4, IPv6", "IPv6, IPv4", NULL};
    this->ip_pref = this->x.config->register_enum (
      this->x.config, "media.network.ip_version", 1,
      (char **)ip_versions,
      _("Internet Protocol version(s) to use"),
      _("\"auto\" just tries what the name query returned.\n"
        "Otherwise, IPv4 may offer more compatibility and privacy."),
      20, ip_pref_cb, this);
  }
#endif

  /*
   * auto join separate audio/video files (testing the side stream feature).
   */
  this->join_av = this->x.config->register_bool (this->x.config,
      "media.files.join_av", 0,
      _("Auto join separate audio/video files"),
      _("When opening an audio only file \"foo_a.ext\", assume that \"foo_v.ext\" "
        "contains the missing video track for it, and vice versa.\n"
        "This mainly serves as a test for engine side streams."),
      20, join_av_cb, this);

  /*
   * keep track of all opened streams
   */
  this->x.streams = xine_list_new ();

  /*
   * start metronom clock
   */

  this->x.clock = _x_metronom_clock_init (&this->x);

  this->x.clock->start_clock (this->x.clock, 0);

  /*
   * tickets
   */
  this->port_ticket = ticket_init();
}

void _x_select_spu_channel (xine_stream_t *s, int channel) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  xine_private_t *xine = (xine_private_t *)stream->s.xine;
  xine_stream_private_t *substream = NULL;

  stream = stream->side_streams[0];
  substream = (xine_stream_private_t *)stream->s.slave;

  pthread_mutex_lock (&stream->frontend_lock);
  stream->s.spu_channel_user = (channel >= -2 ? channel : -2);

  xine->port_ticket->acquire (xine->port_ticket, 1);

  switch (stream->s.spu_channel_user) {
  case -2:
    stream->s.spu_channel = -1;
    break;
  case -1:
    if (substream)
	stream->s.spu_channel = substream->s.spu_channel_auto;
    else
	stream->s.spu_channel = stream->s.spu_channel_auto;
    break;
  default:
    stream->s.spu_channel = stream->s.spu_channel_user;
  }
  lprintf ("set to %d\n", stream->s.spu_channel);

  xine->port_ticket->release (xine->port_ticket, 1);

  pthread_mutex_unlock (&stream->frontend_lock);

  if (substream)
  {
      pthread_mutex_lock (&substream->frontend_lock);
      substream->s.spu_channel = stream->s.spu_channel;
      substream->s.spu_channel_user = stream->s.spu_channel_user;
      pthread_mutex_unlock (&substream->frontend_lock);
  }
}

void _x_get_current_info (xine_stream_t *s, extra_info_t *extra_info, int size) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  stream = stream->side_streams[0];

  if (!extra_info || (size <= 0)) {
    return;
  } else if ((size_t)size < sizeof (*extra_info)) {
    extra_info_t info;
    xine_current_extra_info_get (stream, &info);
    memcpy (extra_info, &info, size);
  } else {
    xine_current_extra_info_get (stream, extra_info);
  }
}


int xine_get_status (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  /* phonon bug */
  if (!stream) {
    printf ("xine_get_status: BUG: stream = NULL.\n");
    return XINE_STATUS_QUIT;
  }
  stream = stream->side_streams[0];
  return stream->status;
}

/*
 * trick play
 */

void _x_set_fine_speed (xine_stream_t *s, int speed) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  xine_private_t *xine = (xine_private_t *)stream->s.xine;
  uint32_t speed_change_flags;

  stream = stream->side_streams[0];

  /* net_buf_ctrl may have 2 threads trying to pause with ticket held in parallel.
   * Avoid that freeze like this:
   * - Hold speed_change_lock for finite time only.
   * - While changing speed, remember latest speed wishes from elsewhere,
   *   and apply them after the current one. */
  pthread_mutex_lock (&xine->speed_change_lock);
  speed_change_flags = xine->speed_change_flags;
  if (speed_change_flags & SPEED_FLAG_IGNORE_CHANGE) {
    pthread_mutex_unlock (&xine->speed_change_lock);
    return;
  }
  if (speed_change_flags & SPEED_FLAG_CHANGING) {
    if ((speed == XINE_LIVE_PAUSE_ON) || (speed == XINE_LIVE_PAUSE_OFF)) {
      xine->speed_change_flags = speed_change_flags | SPEED_FLAG_WANT_LIVE;
      xine->speed_change_new_live = speed;
      pthread_mutex_unlock (&xine->speed_change_lock);
      return;
    }
    xine->speed_change_flags = speed_change_flags | SPEED_FLAG_WANT_NEW;
    xine->speed_change_new_speed = speed;
    pthread_mutex_unlock (&xine->speed_change_lock);
    return;
  }
  xine->speed_change_flags |= SPEED_FLAG_CHANGING;
  pthread_mutex_unlock (&xine->speed_change_lock);

  while (1) {
    if (speed <= XINE_SPEED_PAUSE)
      speed = XINE_SPEED_PAUSE;
    xprintf (&xine->x, XINE_VERBOSITY_DEBUG, "set_speed %d.\n", speed);
    set_speed_internal (stream, speed);
    if (stream->s.slave && (stream->slave_affection & XINE_MASTER_SLAVE_SPEED))
      set_speed_internal ((xine_stream_private_t *)stream->s.slave, speed);

    pthread_mutex_lock (&xine->speed_change_lock);
    speed_change_flags = xine->speed_change_flags;
    if (!(speed_change_flags & (SPEED_FLAG_WANT_LIVE | SPEED_FLAG_WANT_NEW))) {
      xine->speed_change_flags = speed_change_flags & ~(SPEED_FLAG_CHANGING | SPEED_FLAG_WANT_LIVE | SPEED_FLAG_WANT_NEW);
      if (speed_change_flags & SPEED_FLAG_IGNORE_CHANGE)
        pthread_cond_broadcast (&xine->speed_change_done);
      pthread_mutex_unlock (&xine->speed_change_lock);
      return;
    }
    if (speed_change_flags & SPEED_FLAG_WANT_LIVE) {
      xine->speed_change_flags = speed_change_flags & ~SPEED_FLAG_WANT_LIVE;
      speed = xine->speed_change_new_live;
    } else { /* speed_change_flags & SPEED_FLAG_WANT_NEW */
      xine->speed_change_flags = speed_change_flags & ~SPEED_FLAG_WANT_NEW;
      speed = xine->speed_change_new_speed;
    }
    pthread_mutex_unlock (&xine->speed_change_lock);
  }
}

int _x_get_fine_speed (xine_stream_t *stream) {
  return stream->xine->clock->speed;
}

void _x_set_speed (xine_stream_t *stream, int speed) {

  if (speed > XINE_SPEED_FAST_4)
    speed = XINE_SPEED_FAST_4;

  _x_set_fine_speed (stream, speed * XINE_FINE_SPEED_NORMAL / XINE_SPEED_NORMAL);
}

int _x_get_speed (xine_stream_t *stream) {
  int speed = _x_get_fine_speed (stream);

  /*
   * ensure compatibility with old API, only valid XINE_SPEED_xxx
   * constants are allowed. XINE_SPEED_NORMAL may only be returned
   * if speed is exactly XINE_FINE_SPEED_NORMAL.
   */

  if( speed <= XINE_SPEED_PAUSE )
    return XINE_SPEED_PAUSE;
  if( speed <= XINE_SPEED_SLOW_4 * XINE_FINE_SPEED_NORMAL / XINE_SPEED_NORMAL )
    return XINE_SPEED_SLOW_4;
  if( speed < XINE_FINE_SPEED_NORMAL )
    return XINE_SPEED_SLOW_2;
  if( speed == XINE_FINE_SPEED_NORMAL )
    return XINE_SPEED_NORMAL;
  if( speed <= XINE_SPEED_FAST_2 * XINE_FINE_SPEED_NORMAL / XINE_SPEED_NORMAL )
    return XINE_SPEED_FAST_2;
  return XINE_SPEED_FAST_4;
}


/*
 * time measurement / seek
 */

int xine_get_pos_length (xine_stream_t *s, int *pos_stream, int *pos_time, int *length_time) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  extra_info_t info;
  int normpos, timepos;

  stream = stream->side_streams[0];
  pthread_mutex_lock (&stream->frontend_lock);

  if (!stream->s.input_plugin) {
    lprintf ("no input source\n");
    pthread_mutex_unlock (&stream->frontend_lock);
    return 0;
  }

  if ((!stream->video_decoder_plugin && !stream->audio_decoder_plugin)) {
    /* rare case: no decoders available. */
    xine_rwlock_rdlock (&stream->info_lock);
    if (stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO]) {
      xine_rwlock_unlock (&stream->info_lock);
      xine_current_extra_info_set (stream, stream->video_decoder_extra_info);
    } else {
      xine_rwlock_unlock (&stream->info_lock);
      xine_current_extra_info_set (stream, stream->audio_decoder_extra_info);
    }
  }
  xine_current_extra_info_get (stream, &info);
  if (info.seek_count != stream->video_seek_count) {
    pthread_mutex_unlock (&stream->frontend_lock);
    return 0; /* position not yet known */
  }
  normpos = info.input_normpos;
  timepos = info.input_time;

  if (length_time) {
    int length = 0;
    /* frontend lock prevents demux unload. To be very precise, we would need to
     * suspend demux here as well, and trash performance :-/
     * Well. Demux either knows length from the start, and value is constant.
     * Or, it grows with current position. We can do that, too. */
    if (stream->demux.plugin)
      length = stream->demux.plugin->get_stream_length (stream->demux.plugin);
    pthread_mutex_unlock (&stream->frontend_lock);
    if ((length > 0) && (length < timepos))
      length = timepos;
    *length_time = length;
  } else {
    pthread_mutex_unlock (&stream->frontend_lock);
  }

  if (pos_stream)
    *pos_stream = normpos;
  if (pos_time)
    *pos_time = timepos;
  return 1;
}

static int _x_get_current_frame_data (xine_stream_t *stream,
				      xine_current_frame_data_t *data,
				      int flags, int img_size_unknown) {

  xine_private_t *xine;
  vo_frame_t *frame;
  size_t required_size = 0;

  {
    xine_stream_private_t *s = (xine_stream_private_t *)stream;
    stream = &s->side_streams[0]->s;
  }
  xine = (xine_private_t *)stream->xine;

  xine->port_ticket->acquire (xine->port_ticket, 1);
  frame = stream->video_out->get_last_frame (stream->video_out);
  xine->port_ticket->release (xine->port_ticket, 1);

  if (!frame) {
    data->img_size = 0;
    return 0;
  }

  data->width       = frame->width;
  data->height      = frame->height;
  data->crop_left   = frame->crop_left;
  data->crop_right  = frame->crop_right;
  data->crop_top    = frame->crop_top;
  data->crop_bottom = frame->crop_bottom;

  data->ratio_code = 10000.0 * frame->ratio;
  /* make ratio_code backward compatible */
#define RATIO_LIKE(a, b)  ((b) - 1 <= (a) && (a) <= 1 + (b))
  if (RATIO_LIKE(data->ratio_code, 10000))
    data->ratio_code = XINE_VO_ASPECT_SQUARE;
  else if (RATIO_LIKE(data->ratio_code, 13333))
    data->ratio_code = XINE_VO_ASPECT_4_3;
  else if (RATIO_LIKE(data->ratio_code, 17778))
    data->ratio_code = XINE_VO_ASPECT_ANAMORPHIC;
  else if (RATIO_LIKE(data->ratio_code, 21100))
    data->ratio_code = XINE_VO_ASPECT_DVB;

  data->format     = frame->format;
  data->interlaced = frame->progressive_frame ? 0 : (2 - frame->top_field_first);

  switch (frame->format) {

  default:
    if (frame->proc_provide_standard_frame_data) {
      uint8_t *img = data->img;
      size_t img_size = data->img_size;
      data->img = 0;
      data->img_size = 0;

      /* ask frame implementation for required img buffer size */
      frame->proc_provide_standard_frame_data(frame, data);
      required_size = data->img_size;

      data->img = img;
      data->img_size = img_size;
      break;
    }

    if (!data->img && !(flags & XINE_FRAME_DATA_ALLOCATE_IMG))
      break; /* not interested in image data */

    xprintf (stream->xine, XINE_VERBOSITY_DEBUG,
	     "xine: error, snapshot function not implemented for format 0x%x\n", frame->format);
    /* fall through and provide "green" YV12 image */
    data->format = XINE_IMGFMT_YV12;
    /* fall through */
  case XINE_IMGFMT_YV12:
    required_size = frame->width * frame->height
                  + ((frame->width + 1) / 2) * ((frame->height + 1) / 2)
                  + ((frame->width + 1) / 2) * ((frame->height + 1) / 2);
    break;

  case XINE_IMGFMT_YUY2:
    required_size = frame->width * frame->height
                  + ((frame->width + 1) / 2) * frame->height
                  + ((frame->width + 1) / 2) * frame->height;
    break;

  }

  if (flags & XINE_FRAME_DATA_ALLOCATE_IMG) {
    /* return allocated buffer size */
    data->img_size = required_size;
    /* allocate img or fail */
    if (!(data->img = calloc(1, required_size))) {
      frame->free(frame);
      return 0;
    }
  } else {
    /* fail if supplied buffer is to small */
    if (data->img && !img_size_unknown && data->img_size < (int)required_size) {
      data->img_size = required_size;
      frame->free(frame);
      return 0;
    }
    /* return used buffer size */
    data->img_size = required_size;
  }

  if (data->img) {
    switch (frame->format) {

    case XINE_IMGFMT_YV12:
      yv12_to_yv12(
       /* Y */
        frame->base[0], frame->pitches[0],
        data->img, frame->width,
       /* U */
        frame->base[1], frame->pitches[1],
        data->img+frame->width*frame->height, frame->width/2,
       /* V */
        frame->base[2], frame->pitches[2],
        data->img+frame->width*frame->height+frame->width*frame->height/4, frame->width/2,
       /* width x height */
        frame->width, frame->height);
      break;

    case XINE_IMGFMT_YUY2:
      yuy2_to_yuy2(
       /* src */
        frame->base[0], frame->pitches[0],
       /* dst */
        data->img, frame->width*2,
       /* width x height */
        frame->width, frame->height);
      break;

    default:
      if (frame->proc_provide_standard_frame_data)
        frame->proc_provide_standard_frame_data(frame, data);
      else if (!(flags & XINE_FRAME_DATA_ALLOCATE_IMG))
        memset(data->img, 0, data->img_size);
    }
  }

  frame->free(frame);
  return 1;
}

int xine_get_current_frame_data (xine_stream_t *stream,
				 xine_current_frame_data_t *data,
				 int flags) {

  return _x_get_current_frame_data(stream, data, flags, 0);
}

int xine_get_current_frame_alloc (xine_stream_t *stream, int *width, int *height,
				  int *ratio_code, int *format,
				  uint8_t **img, int *img_size) {

  int result;
  xine_current_frame_data_t data;

  memset(&data, 0, sizeof (data));

  result = _x_get_current_frame_data(stream, &data, img ? XINE_FRAME_DATA_ALLOCATE_IMG : 0, 0);
  if (width)      *width      = data.width;
  if (height)     *height     = data.height;
  if (ratio_code) *ratio_code = data.ratio_code;
  if (format)     *format     = data.format;
  if (img_size)   *img_size   = data.img_size;
  if (img)        *img        = data.img;
  return result;
}

int xine_get_current_frame_s (xine_stream_t *stream, int *width, int *height,
				int *ratio_code, int *format,
				uint8_t *img, int *img_size) {
  int result;
  xine_current_frame_data_t data;

  memset(&data, 0, sizeof (data));
  data.img = img;
  if (img_size)
    data.img_size = *img_size;

  result = _x_get_current_frame_data(stream, &data, 0, 0);
  if (width)      *width      = data.width;
  if (height)     *height     = data.height;
  if (ratio_code) *ratio_code = data.ratio_code;
  if (format)     *format     = data.format;
  if (img_size)   *img_size   = data.img_size;
  return result;
}

int xine_get_current_frame (xine_stream_t *stream, int *width, int *height,
			    int *ratio_code, int *format,
			    uint8_t *img) {
  int result;
  xine_current_frame_data_t data;

  memset(&data, 0, sizeof (data));
  data.img = img;

  result = _x_get_current_frame_data(stream, &data, 0, 1);
  if (width)      *width      = data.width;
  if (height)     *height     = data.height;
  if (ratio_code) *ratio_code = data.ratio_code;
  if (format)     *format     = data.format;
  return result;
}

xine_grab_video_frame_t* xine_new_grab_video_frame (xine_stream_t *stream) {
  xine_private_t *xine = (xine_private_t *)stream->xine;
  xine_grab_video_frame_t *frame;

  xine->port_ticket->acquire (xine->port_ticket, 1);

  {
    xine_stream_private_t *s = (xine_stream_private_t *)stream;
    stream = &s->side_streams[0]->s;
  }

  if (stream->video_out->driver->new_grab_video_frame)
    frame = stream->video_out->driver->new_grab_video_frame(stream->video_out->driver);
  else
    frame = stream->video_out->new_grab_video_frame(stream->video_out);

  xine->port_ticket->release (xine->port_ticket, 1);

  return frame;
}

static int _get_spu_lang (xine_stream_private_t *stream, int channel, char *lang) {
  if (!lang)
    return 0;

  /* Ask the demuxer first (e.g. TS extracts this information from
   * the stream)
   **/
  if (stream->demux.plugin) {
    if (stream->demux.plugin->get_capabilities (stream->demux.plugin) & DEMUX_CAP_SPULANG) {
      /* pass the channel number to the plugin in the data field */
      memcpy(lang, &channel, sizeof(channel));
      if (stream->demux.plugin->get_optional_data (stream->demux.plugin, lang,
	  DEMUX_OPTIONAL_DATA_SPULANG) == DEMUX_OPTIONAL_SUCCESS)
        return 1;
    }
  }

  /* No match, check with input plugin instead (e.g. DVD gets this
   * info from the IFO).
   **/
  if (stream->s.input_plugin) {
    if (stream->s.input_plugin->get_capabilities (stream->s.input_plugin) & INPUT_CAP_SPULANG) {
      /* pass the channel number to the plugin in the data field */
      memcpy(lang, &channel, sizeof(channel));
      if (stream->s.input_plugin->get_optional_data (stream->s.input_plugin, lang,
	  INPUT_OPTIONAL_DATA_SPULANG) == INPUT_OPTIONAL_SUCCESS)
        return 1;
    }
  }

  memcpy (lang, "und", 4);
  return 0;
}

int xine_get_spu_lang (xine_stream_t *s, int channel, char *lang) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  int ret;

  pthread_mutex_lock (&stream->frontend_lock);
  ret = _get_spu_lang(stream, channel, lang);
  pthread_mutex_unlock (&stream->frontend_lock);
  return ret;
}

static int _get_audio_lang (xine_stream_private_t *stream, int channel, char *lang) {
  if (!lang)
    return 0;

  if (stream->demux.plugin) {
    if (stream->demux.plugin->get_capabilities (stream->demux.plugin) & DEMUX_CAP_AUDIOLANG) {
      /* pass the channel number to the plugin in the data field */
      memcpy(lang, &channel, sizeof(channel));
      if (stream->demux.plugin->get_optional_data (stream->demux.plugin, lang,
	  DEMUX_OPTIONAL_DATA_AUDIOLANG) == DEMUX_OPTIONAL_SUCCESS)
        return 1;
    }
  }

  if (stream->s.input_plugin) {
    if (stream->s.input_plugin->get_capabilities (stream->s.input_plugin) & INPUT_CAP_AUDIOLANG) {
      /* pass the channel number to the plugin in the data field */
      memcpy(lang, &channel, sizeof(channel));
      if (stream->s.input_plugin->get_optional_data (stream->s.input_plugin, lang,
	  INPUT_OPTIONAL_DATA_AUDIOLANG) == INPUT_OPTIONAL_SUCCESS)
        return 1;
    }
  }

  memcpy (lang, "und", 4);
  return 0;
}

int xine_get_audio_lang (xine_stream_t *s, int channel, char *lang) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  int ret;

  pthread_mutex_lock (&stream->frontend_lock);
  ret = _get_audio_lang(stream, channel, lang);
  pthread_mutex_unlock (&stream->frontend_lock);
  return ret;
}

int _x_get_spu_channel (xine_stream_t *stream) {
  xine_stream_private_t *s = (xine_stream_private_t *)stream;
  stream = &s->side_streams[0]->s;
  return stream->spu_channel_user;
}

int _x_get_video_streamtype (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  return stream->video_decoder_streamtype;
}

/*
 * log functions
 */
int xine_get_log_section_count (xine_t *this) {
  (void)this;
  return XINE_LOG_NUM;
}

const char *const *xine_get_log_names (xine_t *this) {
  static const char *log_sections[XINE_LOG_NUM + 1];

  log_sections[XINE_LOG_MSG]      = _("messages");
  log_sections[XINE_LOG_PLUGIN]   = _("plugin");
  log_sections[XINE_LOG_TRACE]    = _("trace");
  log_sections[XINE_LOG_NUM]      = NULL;

  (void)this;
  return log_sections;
}

static inline void check_log_alloc (xine_private_t *this, int buf)
{
  if (this->x.log_buffers[buf])
    return;

  pthread_mutex_lock (&this->log_lock);

  if (!this->x.log_buffers[buf])
    this->x.log_buffers[buf] = _x_new_scratch_buffer(150);

  pthread_mutex_unlock (&this->log_lock);
}

void xine_log (xine_t *this_gen, int buf, const char *format, ...) {
  xine_private_t *this = (xine_private_t *)this_gen;
  va_list argp;
  char    buffer[SCRATCH_LINE_LEN_MAX];

  check_log_alloc (this, buf);

  va_start (argp, format);
  this->x.log_buffers[buf]->scratch_printf (this->x.log_buffers[buf], format, argp);
  va_end(argp);

  if (this->x.verbosity) {
    va_start(argp, format);
    vsnprintf(buffer, SCRATCH_LINE_LEN_MAX, format, argp);
    printf("%s", buffer);
    va_end (argp);
  }

  if (this->log_cb)
    this->log_cb (this->log_cb_user_data, buf);
}

void xine_vlog (xine_t *this_gen, int buf, const char *format,
                va_list args)
{
  xine_private_t *this = (xine_private_t *)this_gen;
  check_log_alloc (this, buf);

  this->x.log_buffers[buf]->scratch_printf (this->x.log_buffers[buf], format, args);

  if (this->log_cb)
    this->log_cb (this->log_cb_user_data, buf);
}

char *const *xine_get_log (xine_t *this, int buf) {

  if(buf >= XINE_LOG_NUM)
    return NULL;

  if ( this->log_buffers[buf] )
    return this->log_buffers[buf]->get_content (this->log_buffers[buf]);
  else
    return NULL;
}

void xine_register_log_cb (xine_t *this_gen, xine_log_cb_t cb, void *user_data) {
  xine_private_t *this = (xine_private_t *)this_gen;
  this->log_cb = cb;
  this->log_cb_user_data = user_data;
}

int xine_get_error (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  return stream->err;
}

int xine_stream_master_slave (xine_stream_t *m, xine_stream_t *slave, int affection) {
  xine_stream_private_t *master = (xine_stream_private_t *)m;
  master->s.slave = slave;
  master->slave_affection = affection;
  /* respect transitivity: if our designated master already has a master
   * of its own, we point to this master's master; if our master is a
   * standalone stream, its master pointer will point to itself */
  slave->master = master->s.master;

  _x_select_spu_channel (m, master->s.spu_channel_user);
  return 1;
}

int _x_query_buffer_usage(xine_stream_t *stream, int *num_video_buffers, int *num_audio_buffers, int *num_video_frames, int *num_audio_frames)
{
  xine_private_t *xine = (xine_private_t *)stream->xine;
  int ticket_acquired = -1;

  {
    xine_stream_private_t *s = (xine_stream_private_t *)stream;
    stream = &s->side_streams[0]->s;
  }

  if (num_video_buffers)
    *num_video_buffers = (stream->video_fifo ? stream->video_fifo->size(stream->video_fifo) : 0);

  if (num_audio_buffers)
    *num_audio_buffers = (stream->audio_fifo ? stream->audio_fifo->size(stream->audio_fifo) : 0);

  if ((num_video_frames && stream->video_out)
    || (num_audio_frames && stream->audio_out)) {

    ticket_acquired = xine->port_ticket->acquire_nonblocking (xine->port_ticket, 1);
  }

  if (num_video_frames)
    *num_video_frames = ((ticket_acquired && stream->video_out) ? stream->video_out->get_property(stream->video_out, VO_PROP_BUFS_IN_FIFO) : 0);

  if (num_audio_frames)
    *num_audio_frames = ((ticket_acquired && stream->audio_out) ? stream->audio_out->get_property(stream->audio_out, AO_PROP_BUFS_IN_FIFO) : 0);

  if (ticket_acquired > 0)
    xine->port_ticket->release_nonblocking (xine->port_ticket, 1);

  return ticket_acquired != 0;
}

static void _x_query_buffers_fix_data(xine_query_buffers_data_t *data)
{
  if (data->total < 0)
    data->total = 0;

  if (data->ready < 0)
    data->ready = 0;

  if (data->avail < 0)
    data->avail = 0;

  /* fix race condition of not filling data atomically */
  if (data->ready + data->avail > data->total)
    data->avail = data->total - data->ready;
}

int _x_query_buffers(xine_stream_t *stream, xine_query_buffers_t *query)
{
  xine_private_t *xine = (xine_private_t *)stream->xine;
  int ticket_acquired = -1;

  {
    xine_stream_private_t *s = (xine_stream_private_t *)stream;
    stream = &s->side_streams[0]->s;
  }

  memset(query, 0, sizeof (*query));

  if (stream->video_fifo)
  {
    query->vi.total = stream->video_fifo->buffer_pool_capacity;
    query->vi.ready = stream->video_fifo->size(stream->video_fifo);
    query->vi.avail = stream->video_fifo->num_free(stream->video_fifo);
    _x_query_buffers_fix_data(&query->vi);
  }

  if (stream->audio_fifo)
  {
    query->ai.total = stream->audio_fifo->buffer_pool_capacity;
    query->ai.ready = stream->audio_fifo->size(stream->audio_fifo);
    query->ai.avail = stream->audio_fifo->num_free(stream->audio_fifo);
    _x_query_buffers_fix_data(&query->ai);
  }

  if (stream->video_out || stream->audio_out)
    ticket_acquired = xine->port_ticket->acquire_nonblocking (xine->port_ticket, 1);

  if (ticket_acquired > 0)
  {
    if (stream->video_out)
    {
      query->vo.total = stream->video_out->get_property(stream->video_out, VO_PROP_BUFS_TOTAL);
      query->vo.ready = stream->video_out->get_property(stream->video_out, VO_PROP_BUFS_IN_FIFO);
      query->vo.avail = stream->video_out->get_property(stream->video_out, VO_PROP_BUFS_FREE);
    }

    if (stream->audio_out)
    {
      query->ao.total = stream->audio_out->get_property(stream->audio_out, AO_PROP_BUFS_TOTAL);
      query->ao.ready = stream->audio_out->get_property(stream->audio_out, AO_PROP_BUFS_IN_FIFO);
      query->ao.avail = stream->audio_out->get_property(stream->audio_out, AO_PROP_BUFS_FREE);
    }

    xine->port_ticket->release_nonblocking (xine->port_ticket, 1);
  }

  return ticket_acquired != 0;
}

int _x_lock_port_rewiring (xine_t *xine_gen, int ms_timeout)
{
  xine_private_t *xine = (xine_private_t *)xine_gen;
  return xine->port_ticket->lock_port_rewiring(xine->port_ticket, ms_timeout);
}

void _x_unlock_port_rewiring (xine_t *xine_gen)
{
  xine_private_t *xine = (xine_private_t *)xine_gen;
  xine->port_ticket->unlock_port_rewiring(xine->port_ticket);
}

int _x_lock_frontend (xine_stream_t *s, int ms_to_time_out) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  return lock_timeout (&stream->frontend_lock, ms_to_time_out);
}

void _x_unlock_frontend (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  pthread_mutex_unlock(&stream->frontend_lock);
}

int _x_query_unprocessed_osd_events(xine_stream_t *stream)
{
  xine_private_t *xine = (xine_private_t *)stream->xine;
  video_overlay_manager_t *ovl;
  int redraw_needed;

  {
    xine_stream_private_t *s = (xine_stream_private_t *)stream;
    stream = &s->side_streams[0]->s;
  }

  if (!xine->port_ticket->acquire_nonblocking (xine->port_ticket, 1))
    return -1;

  ovl = stream->video_out->get_overlay_manager(stream->video_out);
  redraw_needed = ovl->redraw_needed(ovl, 0);

  if (redraw_needed)
    stream->video_out->trigger_drawing(stream->video_out);

  xine->port_ticket->release_nonblocking (xine->port_ticket, 1);

  return redraw_needed;
}

int _x_continue_stream_processing (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  return stream->status != XINE_STATUS_STOP
    && stream->status != XINE_STATUS_QUIT;
}

void _x_trigger_relaxed_frame_drop_mode (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  pthread_mutex_lock (&stream->first_frame.lock);
  stream->first_frame.flag = 2;
  pthread_mutex_unlock (&stream->first_frame.lock);
}

void _x_reset_relaxed_frame_drop_mode (xine_stream_t *s) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  stream = stream->side_streams[0];
  pthread_mutex_lock (&stream->first_frame.lock);
  stream->first_frame.flag = 1;
  pthread_mutex_unlock (&stream->first_frame.lock);
}


#define KF_BITS 10
#define KF_SIZE (1 << KF_BITS)
#define KF_MASK (KF_SIZE - 1)

int xine_keyframes_find (xine_stream_t *s, xine_keyframes_entry_t *pos, int offs) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  if (!stream || (&stream->s == XINE_ANON_STREAM) || !pos)
    return 2;

  stream = stream->side_streams[0];
  pthread_mutex_lock (&stream->index.lock);
  if (!stream->index.array || !stream->index.used) {
    pthread_mutex_unlock (&stream->index.lock);
    return 2;
  }
  /* binary search the index */
  {
    xine_keyframes_entry_t *t = stream->index.array;
    int d, a = 0, e = stream->index.used, m = e >> 1, l;
    if ((pos->normpos > 0) && (pos->normpos < 0x10000)) {
      do {
        d = t[m].normpos - pos->normpos;
        if (d == 0)
          break;
        if (d > 0)
          e = m;
        else
          a = m;
        l = m;
        m = (a + e) >> 1;
      } while (m != l);
      if ((offs == 0) && (m + 1 < stream->index.used) &&
        (pos->normpos >= ((t[m].normpos + t[m + 1].normpos) >> 1)))
        m++;
    } else {
      do {
        d = t[m].msecs - pos->msecs;
        if (d == 0)
          break;
        if (d > 0)
          e = m;
        else
          a = m;
        l = m;
        m = (a + e) >> 1;
      } while (m != l);
      if ((offs == 0) && (m + 1 < stream->index.used) &&
        (pos->msecs >= ((t[m].msecs + t[m + 1].msecs) >> 1)))
        m++;
    }
    e = 0;
    if ((offs < 0) && (d != 0))
      offs++;
    m += offs;
    if (m < 0) {
      m = 0;
      e = 1;
    } else if (m >= stream->index.used) {
      m = stream->index.used - 1;
      e = 1;
    }
    *pos = t[m];
    pthread_mutex_unlock (&stream->index.lock);
    return e;
  }
}

int _x_keyframes_add (xine_stream_t *s, xine_keyframes_entry_t *pos) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  xine_keyframes_entry_t *t;
  stream = stream->side_streams[0];
  pthread_mutex_lock (&stream->index.lock);
  /* first ever entry */
  t = stream->index.array;
  if (!t) {
    t = calloc (KF_SIZE, sizeof (*t));
    if (!t) {
      pthread_mutex_unlock (&stream->index.lock);
      return -1;
    }
    t[0] = *pos;
    stream->index.array = t;
    stream->index.lastadd = 0;
    stream->index.used = 1;
    stream->index.size = KF_SIZE;
    pthread_mutex_unlock (&stream->index.lock);
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
      "keyframes: build index while playing.\n");
    return 0;
  }
  /* enlarge buf */
  if (stream->index.used + 1 >= stream->index.size) {
    t = realloc (stream->index.array, (stream->index.size + KF_SIZE) * sizeof (*t));
    if (!t) {
      pthread_mutex_unlock (&stream->index.lock);
      return -1;
    }
    stream->index.array = t;
    stream->index.size += KF_SIZE;
  }
  /* binary search seek target */
  {
    /* fast detect the most common "progressive" case */
    int d, a = 0, m = stream->index.lastadd, l, e = stream->index.used;
    if (m + 1 < e)
      m++;
    do {
      d = t[m].msecs - pos->msecs;
      if (abs (d) < 10) {
        t[m] = *pos; /* already known */
        pthread_mutex_unlock (&stream->index.lock);
        return m;
      }
      if (d > 0)
        e = m;
      else
        a = m;
      l = m;
      m = (a + e) >> 1;
    } while (m != l);
    if (d < 0)
      m++;
    if (m < stream->index.used) /* insert */
      memmove (&t[m + 1], &t[m], (stream->index.used - m) * sizeof (*t));
    stream->index.used++;
    stream->index.lastadd = m;
    t[m] = *pos;
    pthread_mutex_unlock (&stream->index.lock);
    return m;
  }
}

xine_keyframes_entry_t *xine_keyframes_get (xine_stream_t *s, int *size) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  xine_keyframes_entry_t *ret;
  if (!stream || (&stream->s == XINE_ANON_STREAM) || !size)
    return NULL;
  stream = stream->side_streams[0];
  pthread_mutex_lock (&stream->index.lock);
  if (stream->index.array && stream->index.used) {
    ret = malloc (stream->index.used * sizeof (xine_keyframes_entry_t));
    if (ret) {
      memcpy (ret, stream->index.array, stream->index.used * sizeof (xine_keyframes_entry_t));
      *size = stream->index.used;
    }
  } else {
    ret = NULL;
    *size = 0;
  }
  pthread_mutex_unlock (&stream->index.lock);
  return ret;
}

int _x_keyframes_set (xine_stream_t *s, xine_keyframes_entry_t *list, int size) {
  xine_stream_private_t *stream = (xine_stream_private_t *)s;
  int n = (size + KF_MASK) & ~KF_MASK;
  stream = stream->side_streams[0];
  pthread_mutex_lock (&stream->index.lock);
  if (stream->index.array) {
    xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
      "keyframes: deleting index.\n");
    free (stream->index.array);
  }
  stream->index.lastadd = 0;
  stream->index.array = (list && (n > 0)) ? malloc (n * sizeof (xine_keyframes_entry_t)) : NULL;
  if (!stream->index.array) {
    stream->index.used = 0;
    stream->index.size = 0;
    pthread_mutex_unlock (&stream->index.lock);
    return 1;
  }
  memcpy (stream->index.array, list, size * sizeof (xine_keyframes_entry_t));
  stream->index.used = size;
  stream->index.size = n;
  n -= size;
  if (n > 0)
    memset (stream->index.array + size, 0, n * sizeof (xine_keyframes_entry_t));
  pthread_mutex_unlock (&stream->index.lock);
  xprintf (stream->s.xine, XINE_VERBOSITY_DEBUG,
    "keyframes: got %d of them.\n", stream->index.used);
  return 0;
}
