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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>

#define LOG_MODULE "metronom"
#define LOG_VERBOSE
/*
#define LOG
#define LOG_AUDIO
*/
#define METRONOM_CLOCK_INTERNAL

#include <xine/xine_internal.h>
#include <xine/metronom.h>
#include <xine/xineutils.h>
/* xine_rwlock_* */
#include "xine_private.h"

#define MAX_AUDIO_DELTA        1600
#define AUDIO_SAMPLE_LD          15
#define AUDIO_SAMPLE_NUM  (1 << AUDIO_SAMPLE_LD)
#define AUDIO_SAMPLE_MASK (AUDIO_SAMPLE_NUM - 1)
#define WRAP_THRESHOLD       120000
#define MAX_NUM_WRAP_DIFF        10
#define MAX_SCR_PROVIDERS        10
#define MAX_SPEED_CHANGE_CALLBACKS 16
#define VIDEO_DRIFT_TOLERANCE 45000
#define AUDIO_DRIFT_TOLERANCE 45000

/* metronom video modes */
#define VIDEO_PREDICTION_MODE     0      /* use pts + frame duration */
#define VIDEO_PTS_MODE            1      /* use only pts */

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

/*
 * ****************************************
 *   primary SCR plugin:
 *    unix System Clock Reference
 * ****************************************
 */

typedef struct unixscr_s {
  scr_plugin_t    scr;
  void           *mem_to_free;
  /* Time of last speed change. */
  struct timeval  cur_time;
  int64_t         cur_pts;
  /* speed * 90000 / XINE_FINE_SPEED_NORMAL */
  double          speed_factor_1;
  /* speed_factor_1 / 1000000 */
  double          speed_factor_2;

  xine_rwlock_t   lock;
} unixscr_t;

static int unixscr_get_priority (scr_plugin_t *scr) {
  (void)scr;
  return 5; /* low priority */
}

/* Only call this when already mutex locked */
static void unixscr_set_pivot (unixscr_t *this) {
  struct timeval tv;
  double pts_calc;

  xine_monotonic_clock (&tv, NULL);
  pts_calc = (tv.tv_sec - this->cur_time.tv_sec) * this->speed_factor_1;
  /* Make sure this diff is signed. */
  pts_calc += ((int32_t)tv.tv_usec - (int32_t)this->cur_time.tv_usec) * this->speed_factor_2;
  /* This next part introduces a one off inaccuracy to the scr due to rounding tv to pts. */
  this->cur_pts  = this->cur_pts + pts_calc;
  this->cur_time = tv;
}

static int unixscr_set_speed (scr_plugin_t *scr, int speed) {
  unixscr_t *this = (unixscr_t*) scr;

  xine_rwlock_wrlock (&this->lock);

  unixscr_set_pivot( this );
  this->speed_factor_1 = (double)speed * 90000.0 / XINE_FINE_SPEED_NORMAL;
  this->speed_factor_2 = this->speed_factor_1 * 1e-6;

  xine_rwlock_unlock (&this->lock);

  return speed;
}

static void unixscr_adjust (scr_plugin_t *scr, int64_t vpts) {
  unixscr_t *this = (unixscr_t*) scr;

  xine_rwlock_wrlock (&this->lock);

  this->cur_pts = vpts;
  xine_monotonic_clock (&this->cur_time, NULL);

  xine_rwlock_unlock (&this->lock);
}

static void unixscr_start (scr_plugin_t *scr, int64_t start_vpts) {
  unixscr_t *this = (unixscr_t*) scr;

  xine_rwlock_wrlock (&this->lock);

  this->cur_pts = start_vpts;
  xine_monotonic_clock (&this->cur_time, NULL);
  /* XINE_FINE_SPEED_NORMAL */
  this->speed_factor_1 = 90000.0;
  this->speed_factor_2 = 0.09;

  xine_rwlock_unlock (&this->lock);
}

static int64_t unixscr_get_current (scr_plugin_t *scr) {
  unixscr_t *this = (unixscr_t*) scr;

  struct   timeval tv;
  int64_t pts;
  double   pts_calc;
  xine_rwlock_rdlock (&this->lock);

  xine_monotonic_clock(&tv, NULL);
  pts_calc  = (tv.tv_sec - this->cur_time.tv_sec) * this->speed_factor_1;
  pts_calc += ((int32_t)tv.tv_usec - (int32_t)this->cur_time.tv_usec) * this->speed_factor_2;
  pts = this->cur_pts + pts_calc;

  xine_rwlock_unlock (&this->lock);

  return pts;
}

static void unixscr_exit (scr_plugin_t *scr) {
  unixscr_t *this = (unixscr_t*) scr;

  xine_rwlock_destroy (&this->lock);
  free (this->mem_to_free);
}

static scr_plugin_t *unixscr_init (void *this_gen) {
  unixscr_t *this = (unixscr_t *)this_gen;

  if (this) {
    this->mem_to_free = NULL;
  } else {
    this = calloc (1, sizeof (unixscr_t));
    if (!this)
      return NULL;
    this->mem_to_free = this;
  }

  this->scr.interface_version = 3;
  this->scr.get_priority      = unixscr_get_priority;
  this->scr.set_fine_speed    = unixscr_set_speed;
  this->scr.adjust            = unixscr_adjust;
  this->scr.start             = unixscr_start;
  this->scr.get_current       = unixscr_get_current;
  this->scr.exit              = unixscr_exit;

  xine_rwlock_init_default (&this->lock);

  this->cur_time.tv_sec  = 0;
  this->cur_time.tv_usec = 0;
  this->cur_pts          = 0;
  /* XINE_SPEED_PAUSE */
  this->speed_factor_1   = 0;
  this->speed_factor_2   = 0;
  lprintf("xine-scr_init complete\n");

  return &this->scr;
}


/************************************************************************
* The master clock feature. It shall handle these basic cases:          *
* 1. A single system clock controls all timing.                         *
* 2. Some plugin is hard wired to use its own non-adjustable clock.     *
*    That clock is slightly faster or slower than system clock.         *
*    It will drift away over time, and wants xine to follow that drift. *
*    Such clock registers as high priority (> 5),                       *
*    and thus becomes the new master.                                   *
* 3. Some plugin uses its own drifting clock, but it is adjustable,     *
*    and wants xine to fix that drift.                                  *
*    Such clock registers as low priority (< 5).                        *
* In cases 2 and 3, we "sync" the masters time to all other clocks      *
* roughly every 5 seconds.                                              *
************************************************************************/

/* #$@! dont break existing API */
typedef struct {
  metronom_clock_t mct;
  unixscr_t        uscr;
  int              next_sync_pts; /* sync by API calls, STOP_PTS to disable */
  enum {
    SYNC_THREAD_NONE,             /* thread disabled by user, see above */
    SYNC_THREAD_OFF,              /* no clock to sync, or thread unavailable and -"- */
    SYNC_THREAD_RUNNING           /* self explaining */
  }                sync_thread_state;
  scr_plugin_t    *providers[MAX_SCR_PROVIDERS + 1];
  int                     speed_change_used;
  xine_speed_change_cb_t *speed_change_callbacks[MAX_SPEED_CHANGE_CALLBACKS + 1];
  void                   *speed_change_data[MAX_SPEED_CHANGE_CALLBACKS + 1];
} metronom_clock_private_t;

static void metronom_register_speed_change_callback (metronom_clock_t *this,
  xine_speed_change_cb_t *callback, void *user_data) {
  if (callback) {
    metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
    pthread_mutex_lock (&this_priv->mct.lock);
    if (this_priv->speed_change_used < MAX_SPEED_CHANGE_CALLBACKS) {
      this_priv->speed_change_callbacks[this_priv->speed_change_used] = callback;
      this_priv->speed_change_data[this_priv->speed_change_used]      = user_data;
      this_priv->speed_change_used++;
      this_priv->speed_change_callbacks[this_priv->speed_change_used] = NULL;
    }
    pthread_mutex_unlock (&this_priv->mct.lock);
  }
}

static void metronom_unregister_speed_change_callback (metronom_clock_t *this,
  xine_speed_change_cb_t *callback, void *user_data) {
  if (callback) {
    metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
    int i;
    pthread_mutex_lock (&this_priv->mct.lock);
    for (i = 0; this_priv->speed_change_callbacks[i]; i++) {
      if ((this_priv->speed_change_callbacks[i] == callback) &&
          (this_priv->speed_change_data[i]      == user_data)) {
        this_priv->speed_change_used--;
        if (i != this_priv->speed_change_used) {
          this_priv->speed_change_callbacks[i] = this_priv->speed_change_callbacks[this_priv->speed_change_used];
          this_priv->speed_change_data[i]      = this_priv->speed_change_data[this_priv->speed_change_used];
        }
        this_priv->speed_change_callbacks[this_priv->speed_change_used] = NULL;
        break;
      }
    }
    pthread_mutex_unlock (&this_priv->mct.lock);
  }
}

#define START_PTS 0
#define STOP_PTS ~0
#define MASK_PTS (1 << 19) /* 5.825 s */

static void metronom_start_clock (metronom_clock_t *this, int64_t pts) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
  scr_plugin_t **r;

  lprintf("start_clock (at %" PRId64 ")\n", pts);

  if (this_priv->next_sync_pts != STOP_PTS)
    this_priv->next_sync_pts = (int)pts & MASK_PTS;

  pthread_mutex_lock (&this_priv->mct.lock);
  for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++)
    (*r)->start (*r, pts);
  pthread_mutex_unlock (&this_priv->mct.lock);

  this_priv->mct.speed = XINE_FINE_SPEED_NORMAL;
}

static int64_t metronom_get_current_time (metronom_clock_t *this) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
  int64_t pts = this_priv->mct.scr_master->get_current (this_priv->mct.scr_master);
  scr_plugin_t **r;

  /* sync not needed or done by separate thread */
  if (((int)pts & MASK_PTS) != this_priv->next_sync_pts)
    return pts;

  this_priv->next_sync_pts ^= MASK_PTS;

  pthread_mutex_lock (&this_priv->mct.lock);
  for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++)
    if (*r != this_priv->mct.scr_master)
      (*r)->adjust (*r, pts);
  pthread_mutex_unlock (&this_priv->mct.lock);

  return pts;
}

static void metronom_stop_clock(metronom_clock_t *this) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
  scr_plugin_t **r;
  int i;

  pthread_mutex_lock (&this_priv->mct.lock);
  for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++)
    (*r)->set_fine_speed (*r, XINE_SPEED_PAUSE);
  for (i = 0; this_priv->speed_change_callbacks[i]; i++)
    this_priv->speed_change_callbacks[i] (this_priv->speed_change_data[i], XINE_SPEED_PAUSE);
  pthread_mutex_unlock (&this_priv->mct.lock);
}

static void metronom_resume_clock(metronom_clock_t *this) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
  scr_plugin_t **r;
  int i;

  pthread_mutex_lock (&this_priv->mct.lock);
  for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++)
    (*r)->set_fine_speed (*r, XINE_FINE_SPEED_NORMAL);
  for (i = 0; this_priv->speed_change_callbacks[i]; i++)
    this_priv->speed_change_callbacks[i] (this_priv->speed_change_data[i], XINE_FINE_SPEED_NORMAL);
  pthread_mutex_unlock (&this_priv->mct.lock);
}

static void metronom_adjust_clock(metronom_clock_t *this, int64_t desired_pts) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;

  if (this_priv->mct.scr_adjustable)
    this_priv->mct.scr_master->adjust (this_priv->mct.scr_master, desired_pts);
  if (this_priv->next_sync_pts != STOP_PTS)
    this_priv->next_sync_pts = (int)desired_pts & MASK_PTS;
}

static int metronom_set_speed (metronom_clock_t *this, int speed) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
  scr_plugin_t **r;
  int            true_speed, i;

  true_speed = this_priv->mct.scr_master->set_fine_speed (this_priv->mct.scr_master, speed);

  this_priv->mct.speed = true_speed;

  pthread_mutex_lock (&this_priv->mct.lock);
  for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++)
    (*r)->set_fine_speed (*r, true_speed);
  for (i = 0; this_priv->speed_change_callbacks[i]; i++)
    this_priv->speed_change_callbacks[i] (this_priv->speed_change_data[i], true_speed);
  pthread_mutex_unlock (&this_priv->mct.lock);

  return true_speed;
}

/*
 * metronom
 */

typedef struct {

  metronom_t metronom;

  /*
   * metronom internal stuff
   */
  /* general */
  xine_t         *xine;
  metronom_t     *master;
  pthread_mutex_t lock;
  int64_t         vpts_offset;
  int64_t         prebuffer;

  /* audio */
  struct {
    int64_t       pts_per_smpls;
    int64_t       last_pts;
    int64_t       vpts;
    int           vpts_rmndr;  /* the remainder for integer division */
    int           drift_step;
    int           samples;
    int           seek;
    int           force_jump;
    int           vdr_hack;
  } audio;

  /* video */
  struct {
    int64_t       last_pts;
    int64_t       vpts;
    int64_t       av_offset;
    int           drift;
    int           drift_step;
    int           base_av_offset;
    int           force_jump;
    int           img_duration;
    int           img_cpt;
    int           mode;
  } video;

  /* subtitle */
  struct {
    int64_t       vpts;
    int64_t       offset;
  } spu;

  /* bounce hack */
  struct {
    int64_t       diff;
    int64_t       vpts_offs;
    int           left_audio;
    int           left_video;
    int           jumped;
  } bounce;

  /* discontinuity handling */
  struct {
    int             have_video;
    int             have_audio;
    int64_t         last_offs;
    int             last_type;
    int             video_count;
    int             audio_count;
    int             handled_count;
    int             num_video_waiters;
    int             num_audio_waiters;
    pthread_cond_t  video_reached;
    pthread_cond_t  audio_reached;
  } disc;

} metronom_impl_t;

/* detect vdr_xineliboutput from this sequence:
   metronom_handle_*_discontinuity (this, DISC_STREAMSEEK, 0);
   metronom_set_option (this, METRONOM_PREBUFFER, 2000);
   metronom_set_option (this, METRONOM_PREBUFFER, 14400);
   apply audio jump fix after
   metronom_handle_*_discontinuity (this, DISC_STREAMSEEK, != 0);
   */

static void metronom_vdr_hack_disc (metronom_impl_t *this, int64_t pts_offs) {
  if (pts_offs == 0) {
    this->audio.vdr_hack = 0;
  } else {
    this->audio.seek = (this->audio.vdr_hack == 2);
  }
}

static void metronom_vdr_hack_prebuffer (metronom_impl_t *this, int64_t pts) {
  if (pts == 2000) {
    this->audio.vdr_hack = (this->audio.vdr_hack == 0) ? 1 : 0;
  } else if (pts == 14400) {
    this->audio.vdr_hack = (this->audio.vdr_hack == 1) || (this->audio.vdr_hack == 2) ? 2 : 0;
  }
}

static void metronom_set_audio_rate (metronom_t *this_gen, int64_t pts_per_smpls) {
  metronom_impl_t *this = (metronom_impl_t *)this_gen;

  pthread_mutex_lock (&this->lock);

  this->audio.pts_per_smpls = pts_per_smpls;

  pthread_mutex_unlock (&this->lock);

  lprintf("%" PRId64 " pts per %d samples\n", pts_per_smpls, AUDIO_SAMPLE_NUM);
}

static int64_t metronom_got_spu_packet (metronom_t *this_gen, int64_t pts) {
  metronom_impl_t *this = (metronom_impl_t *)this_gen;
  int64_t vpts;

  pthread_mutex_lock (&this->lock);

  if (this->master) {
    this->master->set_option(this->master, METRONOM_LOCK, 1);

    this->vpts_offset = this->master->get_option(this->master, METRONOM_VPTS_OFFSET | METRONOM_NO_LOCK);
    this->spu.offset  = this->master->get_option(this->master, METRONOM_SPU_OFFSET | METRONOM_NO_LOCK);
  }

  vpts = pts + this->vpts_offset + this->spu.offset;

  /* no vpts going backwards please */
  if( vpts < this->spu.vpts )
    vpts = this->spu.vpts;

  this->spu.vpts = vpts;

  if (this->master) {
    this->master->set_option(this->master, METRONOM_LOCK, 0);
  }

  pthread_mutex_unlock (&this->lock);
  return vpts;
}

/* There are 3 stages of discontinuity handling:
 * 1. At demux time (entering fifo):
 *   If pts jumps more than 2 seconds, insert a discontinuity control buf.
 * 2. At metronom_handle_foo_discontinuity () time (leaving fifo):
 *   Wait for both fifos to reach this, then bump vpts offset.
 * 3. At metronom_got_bar () time (after decoding):
 *   Apply vpts to output buf.
 *
 * Issue: Not all decoders work straight forward. Many video decoders
 * delay and reorder their output. We could defer discontinuity handling
 * to stage 3, but decoder may swallow frames due to errors.
 *
 * Issue: mpeg style cotainers may jump during reorder, and pts will
 * bounce some 20 times. Lets detect these, and try to stay calm.
 * Workaround: at 2, remember previous setting, and swap with it when
 * appropriate. This reduces timeline drift. At 3, do accept a few
 * frames from both settings.
 *
 * Issue: Audio frames are often small (< 500 bytes). Putting 1 such
 * frame into mpeg-ts packets (188 bytes) will waste a lot of space.
 * Thats why they usually take 0.5 .. 1 seconds at once, and all video
 * frames for the same time need to be sent before. That normal
 * discontinuity wait will then outdate some audio, and yield a gap of
 * silence.
 * Workaround: Use the bounce hack above to handle most absolute
 * discontinuities without wait. */
#define BOUNCE_MAX 220000

static int metronom_handle_discontinuity (metronom_impl_t *this,
  int type, int try, int64_t disc_off) {
  int64_t cur_time;

  /* video.vpts and audio.vpts adjustements */
  cur_time = this->xine->clock->get_current_time(this->xine->clock);

  switch (type) {
    /* When switching streams gaplessly, a paradox situation may happen:
     * Engine was very fast and filled output buffers with more than
     * this->prebuffer of yet to be played frames from the end of previous
     * stream. The DISC_STREAMSTART code below will then set back vpts
     * a few frames, and the engine will drop them later.
     * We could try to fix this by increasing this->prebuffer, but that
     * would cumulate over large playlists, and finally blow out queue
     * sizes.
     * Instead, we wait here a bit.
     */
    case DISC_GAPLESS:
      {
        int64_t t;
        int speed = this->xine->clock->speed;
        if (speed <= 0)
          return 0;
        pthread_mutex_lock (&this->lock);
        t = this->video.vpts > this->audio.vpts ? this->video.vpts : this->audio.vpts;
        t -= this->prebuffer + cur_time;
        pthread_mutex_unlock (&this->lock);
        if ((t <= 0) || (t > 135000))
          return 0;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "metronom: gapless switch: wait %u pts.\n", (unsigned int)t);
        /* XINE_FINE_SPEED_NORMAL == 1000000; 1000000 * 1000 / 90 == 100000000 / 9; */
        xine_usec_sleep (xine_uint_mul_div (t, 100000000, speed * 9));
      }
      return 0;

    case DISC_STREAMSTART:
      lprintf ("DISC_STREAMSTART\n");
      disc_off = 0;
      /* fall through */
    case DISC_STREAMSEEK:
      lprintf ("DISC_STREAMSEEK\n");
      this->video.vpts       = this->prebuffer + cur_time;
      this->audio.vpts       = this->video.vpts;
      this->vpts_offset      = this->video.vpts - disc_off;
      this->bounce.left_audio = -1;
      this->bounce.left_video = -1;
      this->bounce.jumped    = 0;
      this->audio.vpts_rmndr = 0;
      this->audio.force_jump = 1;
      this->video.force_jump = 1;
      this->video.drift      = 0;
      this->video.last_pts   = 0;
      this->audio.last_pts   = 0;
      metronom_vdr_hack_disc (this, disc_off);
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
        "metronom: vpts adjusted with prebuffer to %" PRId64 ".\n", this->video.vpts);
      lprintf("video.vpts: %" PRId64 ", audio.vpts: %" PRId64 "\n", this->video.vpts, this->audio.vpts);
      return 0;

    case DISC_ABSOLUTE: {
      int64_t d, video_vpts, vpts_offset;
      int mode;
      lprintf ("DISC_ABSOLUTE\n");
      this->audio.seek = 0;
      /* calculate but dont set yet */
      mode = ((this->video.vpts < cur_time) << 1) | (this->audio.vpts < cur_time);
      video_vpts = (mode == 3) ? this->prebuffer + cur_time
                 : (mode == 2) ? this->audio.vpts
                 : this->video.vpts;
      vpts_offset = video_vpts - disc_off;
      /* where are we? */
      d = vpts_offset - this->vpts_offset;
      if (d < 0)
        d = -d;
      if (d < BOUNCE_MAX) {
        /* small step, keep old previous. */
        ;
      } else {
        /* big step. */
        d = vpts_offset - this->bounce.vpts_offs;
        if (d < 0)
          d = -d;
        if (d < BOUNCE_MAX) {
          /* near old previous, swap with it. */
          d = this->vpts_offset;
          this->vpts_offset = this->bounce.vpts_offs;
          this->bounce.vpts_offs = d;
          d -= this->vpts_offset;
          this->bounce.diff = d;
          this->bounce.left_audio = BOUNCE_MAX;
          this->bounce.left_video = BOUNCE_MAX;
          this->audio.last_pts = 0;
          this->video.last_pts = 0;
          xprintf (this->xine, XINE_VERBOSITY_DEBUG, "metronom: pts bounce by %" PRId64 ".\n", d);
          return 0;
        }
        if (try && (this->bounce.left_audio >= 0))
          return 1;
        /* remember current as prev, and set new. */
        this->bounce.vpts_offs = this->vpts_offset;
      }
      this->vpts_offset = vpts_offset;
      this->bounce.diff = this->bounce.vpts_offs - vpts_offset;
      this->video.vpts = video_vpts;
      this->bounce.left_audio = BOUNCE_MAX;
      this->bounce.left_video = BOUNCE_MAX;
      this->bounce.jumped = 1;
      if (mode == 2) {
        /* still frame with audio */
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "metronom: video vpts adjusted to audio vpts %" PRId64 ".\n", this->video.vpts);
      } else if (mode == 3) {
        /* still frame, no audio */
        this->audio.vpts = video_vpts;
        this->audio.vpts_rmndr = 0;
        this->video.force_jump = 1;
        this->audio.force_jump = 1;
        this->video.drift = 0;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "metronom: vpts adjusted with prebuffer to %" PRId64 ".\n", this->video.vpts);
      } else if (mode == 1) {
        /* video, no sound */
        this->audio.vpts = video_vpts;
        this->audio.vpts_rmndr = 0;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "metronom: audio vpts adjusted to video vpts %" PRId64 ".\n", this->video.vpts);
      }
    }
    this->video.last_pts = 0;
    this->audio.last_pts = 0;
    lprintf ("video.vpts: %" PRId64 ", audio.vpts: %" PRId64 "\n", this->video.vpts, this->audio.vpts);
    return 0;

    case DISC_RELATIVE:
      lprintf ("DISC_RELATIVE\n");
      if (this->video.vpts < cur_time) {
        /* still frame */
        if (this->audio.vpts > cur_time) {
          /* still frame with audio */
          this->video.vpts = this->audio.vpts;
          xprintf (this->xine, XINE_VERBOSITY_DEBUG,
            "metronom: video vpts adjusted to audio vpts %" PRId64 ".\n", this->video.vpts);
        } else {
          /* still frame, no audio */
          this->video.vpts = this->prebuffer + cur_time;
          this->audio.vpts = this->video.vpts;
          this->audio.vpts_rmndr = 0;
          this->video.force_jump = 1;
          this->audio.force_jump = 1;
          this->video.drift = 0;
          xprintf (this->xine, XINE_VERBOSITY_DEBUG,
            "metronom: vpts adjusted with prebuffer to %" PRId64 ".\n",
	    this->video.vpts);
        }
      } else {
        /* video */
        if (this->audio.vpts < cur_time) {
          /* video, no sound */
          this->audio.vpts = this->video.vpts;
          this->audio.vpts_rmndr = 0;
          xprintf (this->xine, XINE_VERBOSITY_DEBUG,
            "metronom: audio vpts adjusted to video vpts %" PRId64 ".\n", this->video.vpts);
        } else {
          /* video + audio */
        }
      }
      this->vpts_offset = this->vpts_offset - disc_off;
      this->video.last_pts = 0;
      this->audio.last_pts = 0;
      lprintf ("video.vpts: %" PRId64 ", audio.vpts: %" PRId64 "\n", this->video.vpts, this->audio.vpts);
      return 0;

    default:
      return 0;
  }
}

static void metronom_handle_vdr_trick_pts (metronom_impl_t *this, int64_t pts) {
  int64_t cur_time = this->xine->clock->get_current_time (this->xine->clock);
  if (this->video.vpts < cur_time) {
    if (this->audio.vpts >= cur_time) {
      /* still frame with audio */
      this->video.vpts = this->audio.vpts;
    } else {
      /* still frame, no audio */
      this->audio.vpts =
      this->video.vpts = this->prebuffer + cur_time;
      this->audio.vpts_rmndr = 0;
      this->video.force_jump = 1;
      this->audio.force_jump = 1;
      this->video.drift = 0;
    }
  } else {
    if (this->audio.vpts < cur_time) {
      /* video, no sound */
      this->audio.vpts = this->video.vpts;
      this->audio.vpts_rmndr = 0;
    }
  }
  this->vpts_offset = this->video.vpts - pts;
  this->bounce.diff = this->bounce.vpts_offs - this->vpts_offset;
  this->bounce.left_audio = -1;
  this->bounce.left_video = -1;
  this->bounce.jumped = 0;
  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
    "metronom: vdr trick pts %" PRId64 ", vpts %" PRId64 ".\n", pts, this->video.vpts);
}

static void metronom_handle_video_discontinuity (metronom_t *this_gen, int type,
                                                 int64_t disc_off) {
  metronom_impl_t *this = (metronom_impl_t *)this_gen;
  int waited;

  if (type == DISC_GAPLESS) {
    /* this would cause deadlock in metronom_handle_discontinuity()
       because of double pthread_mutex_lock(&this->lock) */
    _x_assert(type != DISC_GAPLESS);
    return;
  }

  pthread_mutex_lock (&this->lock);

  if (this->master) {
    /* slaves are currently not allowed to set discontinuities */
    pthread_mutex_unlock(&this->lock);
    return;
  }

  this->disc.video_count++;
  if (this->disc.num_video_waiters && (this->disc.audio_count <= this->disc.video_count))
    pthread_cond_signal (&this->disc.video_reached);

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
    "metronom: video discontinuity #%d, type is %d, disc_off %" PRId64 ".\n",
    this->disc.video_count, type, disc_off);

  if (this->disc.video_count <= this->disc.handled_count) {
    pthread_mutex_unlock (&this->lock);
    return;
  }

  if (type == DISC_ABSOLUTE) {
    if (!metronom_handle_discontinuity (this, type, 1, disc_off)) {
      this->disc.handled_count = this->disc.video_count;
      pthread_mutex_unlock (&this->lock);
      return;
    }
  }

  /* If both audio and video are there, the video side shall take
   * effect. Previous code did this by letting audio wait even if
   * video came first. Lets drop that unnecessary wait, and pass
   * over params instead. */
  this->disc.last_type = type;
  this->disc.last_offs = disc_off;

  waited = 0;
  if (this->disc.have_audio) {
    while (this->disc.audio_count <
	   this->disc.video_count) {

      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
        "metronom: waiting for audio discontinuity #%d...\n",
        this->disc.video_count);

      this->disc.num_audio_waiters++;
      pthread_cond_wait (&this->disc.audio_reached, &this->lock);
      this->disc.num_audio_waiters--;
      waited = 1;
    }
  }

  if (!waited) {
    metronom_handle_discontinuity (this, type, 0, disc_off);
    this->disc.handled_count++;
  }

  pthread_mutex_unlock (&this->lock);
}

static void metronom_got_video_frame (metronom_t *this_gen, vo_frame_t *img) {

  metronom_impl_t *this = (metronom_impl_t *)this_gen;
  int64_t pts = img->pts;

  pthread_mutex_lock (&this->lock);

  if (this->master) {
    this->master->set_option(this->master, METRONOM_LOCK, 1);

    if (!this->disc.handled_count) {
      /* we are not initialized yet */

      this->video.vpts = this->audio.vpts = this->master->get_option(this->master, METRONOM_VPTS | METRONOM_NO_LOCK);

      /* when being attached to the first master, do not drift into
       * his vpts values but adopt at once */
      this->audio.force_jump = 1;
      this->video.force_jump = 1;
      this->disc.handled_count++;
    }

    this->vpts_offset = this->master->get_option(this->master, METRONOM_VPTS_OFFSET | METRONOM_NO_LOCK);
    this->video.av_offset   = this->master->get_option(this->master, METRONOM_AV_OFFSET | METRONOM_NO_LOCK);
  }

  lprintf("got_video_frame pts = %" PRId64 ", duration = %d\n", pts, img->duration);

  this->video.img_cpt++;

  /* 1000 fps usually means unknown or variable frame rate */
  if (img->duration > 90) {
    this->video.mode = VIDEO_PREDICTION_MODE;
    this->video.img_duration = img->duration;
  } else {
    /* will skip the whole predicted vpts stuff */
    this->video.mode = VIDEO_PTS_MODE;
  }

  /* goom likes to deliver all zero pts sometimes. Give a chance to follow
     at least sound card drift */
  if (!pts && img->duration && !(this->video.img_cpt & 0x7f))
    pts = this->video.last_pts + this->video.img_cpt * img->duration;

  if (pts && pts != this->video.last_pts) {

    if (!img->duration) {
      /* Compute the duration of previous frames using this formula:
       * duration = (curent_pts - last_pts) / (frame count between the 2 pts)
       * This duration will be used to predict the next frame vpts.
       */
      if (this->video.last_pts && this->video.img_cpt) {
        this->video.img_duration = (pts - this->video.last_pts) / this->video.img_cpt;
        lprintf("computed frame_duration = %d\n", this->video.img_duration );
      }
    }
    this->video.img_cpt = 0;
    this->video.last_pts = pts;


    /*
     * compare predicted (this->video.vpts) and given (pts+vpts_offset)
     * pts values - hopefully they will be the same
     * if not, for small diffs try to interpolate
     *         for big diffs: jump
     */

    pts += this->vpts_offset;

    if (this->bounce.left_video >= 0) {
      int64_t diff = this->video.vpts - pts;
      if ((abs (diff) > BOUNCE_MAX) && (abs (diff - this->bounce.diff) < BOUNCE_MAX)) {
        pts += this->bounce.diff;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG, "metronom: bounced video frame with pts %" PRId64 ".\n", img->pts);
      }
      this->bounce.left_video -= img->duration;
      if (this->bounce.left_video < 0) {
        this->bounce.left_audio = -1;
        this->bounce.left_video = -1;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "metronom: leaving bounce area at pts %" PRId64 ".\n", img->pts);
      }
    }

    if (this->video.mode == VIDEO_PREDICTION_MODE) {

      int64_t diff = this->video.vpts - pts;

      lprintf("video diff is %" PRId64 " (predicted %" PRId64 ", given %" PRId64 ")\n", diff, this->video.vpts, pts);

      if ((abs (diff) > VIDEO_DRIFT_TOLERANCE) || (this->video.force_jump)) {


        xprintf (this->xine, XINE_VERBOSITY_DEBUG, "metronom: video jump by %"PRId64" pts.\n", -diff);
        this->video.force_jump = 0;
        this->video.vpts       = pts;
        this->video.drift      = 0;
        this->video.drift_step = 0;

      } else {
        /* TJ. Drift into new value over the next 32 frames.
         * Dont fall into the asymptote trap of bringing down step with remaining drift.
         * BTW. video.drift* merely uses 17 bits.
         */
        this->video.drift = diff;
        if (diff < 0) {
          int step = ((int)diff - 31) >> 5;
          if (this->video.drift_step > step)
            this->video.drift_step = step;
          else if (this->video.drift_step < (int)diff)
            this->video.drift_step = diff;
        } else {
          int step = ((int)diff + 31) >> 5;
          if (this->video.drift_step < step)
            this->video.drift_step = step;
          else if (this->video.drift_step > (int)diff)
            this->video.drift_step = diff;
        }
      }
    } else {
      /* VIDEO_PTS_MODE: do not use the predicted value */
      this->video.vpts       = pts;
      this->video.drift      = 0;
      this->video.drift_step = 0;
    }
  }

  img->vpts = this->video.vpts + this->video.av_offset + this->video.base_av_offset;

  /* We need to update this->video.vpts is both modes.
   * this->video.vpts is used as the next frame vpts if next frame pts=0
   */
  this->video.vpts += this->video.img_duration - this->video.drift_step;

  if (this->video.mode == VIDEO_PREDICTION_MODE) {
    lprintf("video vpts for %10"PRId64" : %10"PRId64" (duration:%d drift:%d step:%d)\n",
      img->pts, this->video.vpts, img->duration, this->video.drift, this->video.drift_step );

    /* reset drift compensation if work is done after this frame */
    if (this->video.drift_step < 0) {
      this->video.drift -= this->video.drift_step;
      if (this->video.drift >= 0) {
        this->video.drift      = 0;
        this->video.drift_step = 0;
      }
    } else if (this->video.drift_step > 0) {
      this->video.drift -= this->video.drift_step;
      if (this->video.drift <= 0) {
        this->video.drift      = 0;
        this->video.drift_step = 0;
      }
    }
  }


  if (this->master) {
    this->master->set_option(this->master, METRONOM_LOCK, 0);
  }

  pthread_mutex_unlock (&this->lock);
  if (this->xine->verbosity == XINE_VERBOSITY_DEBUG + 1)
    xprintf (this->xine, XINE_VERBOSITY_DEBUG + 1, "metronom: video pts: %"PRId64":%04d ->  %"PRId64".\n",
      img->pts, (int)img->duration, img->vpts);
}

static void metronom_handle_audio_discontinuity (metronom_t *this_gen, int type,
                                                 int64_t disc_off) {
  metronom_impl_t *this = (metronom_impl_t *)this_gen;
  int waited;

  if (type == DISC_GAPLESS) {
    metronom_handle_discontinuity (this, type, 0, disc_off);
    return;
  }

  pthread_mutex_lock (&this->lock);

  if (this->master) {
    /* slaves are currently not allowed to set discontinuities */
    pthread_mutex_unlock(&this->lock);
    return;
  }

  this->disc.audio_count++;
  if (this->disc.num_audio_waiters && (this->disc.audio_count >= this->disc.video_count))
    pthread_cond_signal (&this->disc.audio_reached);

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
    "metronom: audio discontinuity #%d, type is %d, disc_off %" PRId64 ".\n",
    this->disc.audio_count, type, disc_off);

  if (this->disc.audio_count <= this->disc.handled_count) {
    pthread_mutex_unlock (&this->lock);
    return;
  }

  if (type == DISC_ABSOLUTE) {
    if (!metronom_handle_discontinuity (this, type, 1, disc_off)) {
      this->disc.handled_count = this->disc.audio_count;
      pthread_mutex_unlock (&this->lock);
      return;
    }
  }

  waited = 0;
  if (this->disc.have_video) {
    while ( this->disc.audio_count >
            this->disc.video_count ) {

      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
        "metronom: waiting for video discontinuity #%d...\n",
        this->disc.audio_count);

      this->disc.num_video_waiters++;
      pthread_cond_wait (&this->disc.video_reached, &this->lock);
      this->disc.num_video_waiters--;
      waited = 1;
    }
  } else {
    this->disc.last_type = type;
    this->disc.last_offs = disc_off;
  }

  if (!waited) {
    metronom_handle_discontinuity (this, this->disc.last_type, 0, this->disc.last_offs);
    this->disc.handled_count++;
  }
  this->audio.samples = 0;
  this->audio.drift_step = 0;

  pthread_mutex_unlock (&this->lock);
}

static int64_t metronom_got_audio_samples (metronom_t *this_gen, int64_t pts,
                                           int nsamples) {

  metronom_impl_t *this = (metronom_impl_t *)this_gen;
  int64_t vpts;

  lprintf("got %d audio samples, pts is %" PRId64 ", last pts = %" PRId64 "\n", nsamples, pts, this->audio.last_pts);
  lprintf("AUDIO pts from last= %" PRId64 "\n", pts-this->audio.last_pts);

  pthread_mutex_lock (&this->lock);

  if (this->master) {
    this->master->set_option(this->master, METRONOM_LOCK, 1);

    if (!this->disc.handled_count) {
      /* we are not initialized yet */

      this->video.vpts = this->audio.vpts = this->master->get_option(this->master, METRONOM_VPTS | METRONOM_NO_LOCK);

      this->audio.vpts_rmndr = 0;
      /* when being attached to the first master, do not drift into
       * his vpts values but adopt at once */
      this->audio.force_jump = 1;
      this->video.force_jump = 1;
      this->disc.handled_count++;
    }

    this->vpts_offset = this->master->get_option(this->master, METRONOM_VPTS_OFFSET | METRONOM_NO_LOCK);
  }

  if (pts && pts != this->audio.last_pts) {
    int64_t diff;
    this->audio.last_pts = pts;
    vpts = pts + this->vpts_offset;
    diff = this->audio.vpts - vpts;

    /* Attempt to fix that mpeg-ts "video ahead of audio" issue with vdr-libxineoutput. */
    if (this->audio.seek) {
      this->audio.seek = 0;
      if ((diff > 0) && (diff < 220000)) {
        vpts += diff;
        this->vpts_offset += diff;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "metronom: fixing seek jump by %" PRId64 " pts.\n", diff);
        diff = 0;
      }
    }

    if (this->bounce.left_audio >= 0) {
      if ((abs (diff) > BOUNCE_MAX) && (abs (diff - this->bounce.diff) < BOUNCE_MAX)) {
        vpts += this->bounce.diff;
        diff = this->audio.vpts - vpts;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG, "metronom: bounced audio buffer with pts %" PRId64 ".\n", pts);
      }
      this->bounce.left_audio -= (nsamples * this->audio.pts_per_smpls) >> AUDIO_SAMPLE_LD;
      if (this->bounce.left_audio < 0) {
        this->bounce.left_audio = -1;
        this->bounce.left_video = -1;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "metronom: leaving bounce area at pts %" PRId64 ".\n", pts);
      }
      if (this->bounce.jumped) {
        if ((diff > 0) /* && (diff < BOUNCE_MAX) */) {
          vpts += diff;
          this->vpts_offset += diff;
          this->bounce.diff = this->bounce.vpts_offs - this->vpts_offset;
          xprintf (this->xine, XINE_VERBOSITY_DEBUG,
            "metronom: fixing discontinuity jump by %" PRId64 " pts.\n", diff);
          diff = 0;
        }
      }
    }

    /* compare predicted and given vpts */
    if((abs(diff) > AUDIO_DRIFT_TOLERANCE) || (this->audio.force_jump)) {
      xprintf (this->xine, XINE_VERBOSITY_DEBUG, "metronom: audio jump by %" PRId64 " pts.\n", -diff);
      this->audio.force_jump = 0;
      this->audio.vpts       = vpts;
      this->audio.vpts_rmndr = 0;
      this->audio.drift_step = 0;
    } else {
      if( this->audio.samples ) {
        /* calculate drift_step to recover vpts errors */
        int d = diff, m;
        lprintf ("audio diff = %d\n", d);

        d *= AUDIO_SAMPLE_NUM / 4;
        d /= (int)this->audio.samples;
        /* drift_step is not allowed to change rate by more than 25% */
        m = (int)this->audio.pts_per_smpls >> 2;
        if (d > m) d = m;
        else if (d < -m) d = -m;
        this->audio.drift_step = d;

        lprintf ("audio_drift = %d, audio.pts_per_smpls = %" PRId64 "\n", d, this->audio.pts_per_smpls);
      }
    }
    this->audio.samples = 0;
  }
  vpts = this->audio.vpts;

  /* drift here is caused by streams where nominal sample rate differs from
   * the rate of which pts increments. fixing the audio.vpts won't do us any
   * good because sound card won't play it faster or slower just because
   * we want. however, adding the error to the vpts_offset will force video
   * to change it's frame rate to keep in sync with us.
   *
   * Since we are using integer division below, it can happen that we lose
   * precision for the calculated duration in vpts for each audio buffer
   * (< 1 PTS, e.g. 0.25 PTS during playback of most DVDs with LPCM audio).
   * This would lead to a situation where the sound card actually needs
   * more time to play back the buffers, than the audio buffer's vpts field
   * indicates. This makes audio_out loop think we are in sync with the
   * soundcard, while we actually are not. So that's why there is the extra
   * modulo calculation, to keep track of the truncated, fractional part.
   * However, this is but a nice try after all because
   *  1. "pts per 2^15 samples" itself has a similar size rounding error.
   *  2. System and sound card clocks are unprecise independently.
   *  3. System clock is not an exact multiple of sample rate.
   * So let audio out help us fixing this through occasional feedback there :-)
   */
  this->audio.vpts_rmndr += (nsamples * this->audio.pts_per_smpls) & AUDIO_SAMPLE_MASK;
  this->audio.vpts       += (nsamples * this->audio.pts_per_smpls) >> AUDIO_SAMPLE_LD;
  if (this->audio.vpts_rmndr >= AUDIO_SAMPLE_NUM) {
    this->audio.vpts       += 1;
    this->audio.vpts_rmndr -= AUDIO_SAMPLE_NUM;
  }
  this->audio.samples += nsamples;
  this->vpts_offset += (nsamples * this->audio.drift_step) >> AUDIO_SAMPLE_LD;

  if (this->master) {
    this->master->set_option(this->master, METRONOM_LOCK, 0);
  }
  pthread_mutex_unlock (&this->lock);

  lprintf ("audio vpts for %10"PRId64" : %10"PRId64"\n", pts, vpts);
  return vpts;
}

static void metronom_set_option (metronom_t *this_gen, int option, int64_t value) {

  metronom_impl_t *this = (metronom_impl_t *)this_gen;

  if (option == METRONOM_LOCK) {
    if (value) {
      pthread_mutex_lock (&this->lock);
      if (this->master)
        this->master->set_option(this->master, option, value);
    } else {
      if (this->master)
        this->master->set_option(this->master, option, value);
      pthread_mutex_unlock (&this->lock);
    }
    return;
  }

  pthread_mutex_lock (&this->lock);

  if (this->master) {
    /* pass the option on to the master */
    this->master->set_option(this->master, option, value);
    pthread_mutex_unlock(&this->lock);
    return;
  }

  switch (option) {
  case METRONOM_AV_OFFSET:
    this->video.av_offset = value;
    xprintf (this->xine, XINE_VERBOSITY_LOG,
      "metronom: video.av_offset=%" PRId64 " pts.\n", this->video.av_offset);
    break;
  case METRONOM_SPU_OFFSET:
    this->spu.offset = value;
    xprintf (this->xine, XINE_VERBOSITY_LOG,
      "metronom: spu.offset=%" PRId64 " pts.\n", this->spu.offset);
    break;
  case METRONOM_ADJ_VPTS_OFFSET:
    this->audio.vpts      += value;
    this->audio.vpts_rmndr = 0;

    /* that message should be rare, please report otherwise.
     * when xine is in some sort of "steady state" hearing it
     * once in a while means a small sound card drift (or system
     * clock drift -- who knows?). nothing to worry about.
     */
    xprintf (this->xine, XINE_VERBOSITY_LOG,
      "metronom: fixing sound card drift by %" PRId64 " pts.\n", value);
    break;
  case METRONOM_PREBUFFER:
    this->prebuffer = value;
    metronom_vdr_hack_prebuffer (this, value);
    xprintf (this->xine, XINE_VERBOSITY_LOG,
      "metronom: prebuffer=%" PRId64 " pts.\n", this->prebuffer);
    break;
  case METRONOM_VDR_TRICK_PTS:
    metronom_handle_vdr_trick_pts (this, value);
    break;
  default:
    xprintf(this->xine, XINE_VERBOSITY_NONE,
      "metronom: unknown option in set_option: %d.\n", option);
  }

  pthread_mutex_unlock (&this->lock);
}

static void metronom_clock_set_option (metronom_clock_t *this,
					int option, int64_t value) {

  pthread_mutex_lock (&this->lock);

  switch (option) {
  case CLOCK_SCR_ADJUSTABLE:
    this->scr_adjustable = value;
    break;
  default:
    xprintf (this->xine, XINE_VERBOSITY_NONE,
      "metronom: unknown option in set_option: %d.\n", option);
  }

  pthread_mutex_unlock (&this->lock);
}

static int64_t metronom_get_option (metronom_t *this_gen, int option) {

  metronom_impl_t *this = (metronom_impl_t *)this_gen;
  int64_t result;
  int mutex_locked;

  if (option & METRONOM_NO_LOCK) {
    mutex_locked = 0;
  } else {
    pthread_mutex_lock (&this->lock);
    mutex_locked = 1;
  }

  if (this->master) {
    result = this->master->get_option(this->master, option);
    if (mutex_locked)
      pthread_mutex_unlock (&this->lock);
    return result;
  }

  option &= ~METRONOM_NO_LOCK;

  switch (option) {
  case METRONOM_AV_OFFSET:
    result = this->video.av_offset;
    break;
  case METRONOM_SPU_OFFSET:
    result = this->spu.offset;
    break;
  case METRONOM_FRAME_DURATION:
    result = this->video.img_duration;
    break;
  case METRONOM_VPTS_OFFSET:
    result = this->vpts_offset;
    break;
  case METRONOM_PREBUFFER:
    result = this->prebuffer;
    break;
  case METRONOM_VPTS:
      if (this->video.vpts > this->audio.vpts)
        result = this->video.vpts;
      else
        result = this->audio.vpts;
      break;
  case METRONOM_WAITING:
    result = (this->disc.num_audio_waiters ? 1 : 0) | (this->disc.num_video_waiters ? 2 : 0);
    break;
  case METRONOM_VDR_TRICK_PTS:
    result = this->video.vpts;
    break;
  default:
    result = 0;
    xprintf (this->xine, XINE_VERBOSITY_NONE,
      "metronom: unknown option in get_option: %d.\n", option);
    break;
  }

  if (mutex_locked) {
    pthread_mutex_unlock (&this->lock);
  }

  return result;
}

static int64_t metronom_clock_get_option (metronom_clock_t *this, int option) {
  switch (option) {
  case CLOCK_SCR_ADJUSTABLE:
    return this->scr_adjustable;
  }
  xprintf (this->xine, XINE_VERBOSITY_NONE,
    "metronom: unknown option in get_option: %d.\n", option);
  return 0;
}

static void metronom_set_master(metronom_t *this_gen, metronom_t *master) {
  metronom_impl_t *this = (metronom_impl_t *)this_gen;
  metronom_t *old_master = this->master;

  pthread_mutex_lock(&this->lock);
  /* someone might currently be copying values from the old master,
   * so we need his lock too */
  if (old_master)
    old_master->set_option(old_master, METRONOM_LOCK, 1);

  this->master = master;
  /* new master -> we have to reinit */
  this->disc.handled_count = 0;

  if (old_master)
    old_master->set_option(old_master, METRONOM_LOCK, 0);
  pthread_mutex_unlock(&this->lock);
}

static scr_plugin_t* get_master_scr(metronom_clock_t *this) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
  scr_plugin_t *found = NULL, **r;
  int maxprio = 0;

  /* find the SCR provider with the highest priority */
  for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++) {
    int p = (*r)->get_priority (*r);
    if (maxprio < p) {
      found = *r;
      maxprio = p;
    }
  }
  if (!found) {
    xprintf (this_priv->mct.xine, XINE_VERBOSITY_NONE,
      "metronom: panic - no scr provider found!\n");
    return NULL;
  }
  return found;
}

static void *metronom_sync_loop (void *const this_gen) {
  metronom_clock_private_t *const this_priv = (metronom_clock_private_t *const)this_gen;

  struct timespec ts = {0, 0};
  scr_plugin_t **r;
  int64_t        pts;

  while (this_priv->mct.thread_running) {
    /* synchronise every 5 seconds */
    pthread_mutex_lock (&this_priv->mct.lock);

    pts = this_priv->mct.scr_master->get_current (this_priv->mct.scr_master);

    for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++)
      if (*r != this_priv->mct.scr_master) (*r)->adjust (*r, pts);

    xine_gettime (&ts);
    ts.tv_sec += 5;
    pthread_cond_timedwait (&this_priv->mct.cancel, &this_priv->mct.lock, &ts);

    pthread_mutex_unlock (&this_priv->mct.lock);
  }
  return NULL;
}

static void metronom_start_sync_thread (metronom_clock_private_t *this_priv) {
  int err;

  if (this_priv->sync_thread_state == SYNC_THREAD_NONE) {
    this_priv->next_sync_pts = START_PTS;
    return;
  }

  if (this_priv->sync_thread_state != SYNC_THREAD_OFF)
    return;

  pthread_cond_init  (&this_priv->mct.cancel, NULL);

  this_priv->mct.thread_running = 1;

  err = pthread_create (&this_priv->mct.sync_thread, NULL, metronom_sync_loop, &this_priv->mct);

  if (err) {
    xprintf (this_priv->mct.xine, XINE_VERBOSITY_NONE,
      "metronom: cannot create sync thread (%s).\n", strerror (err));
    this_priv->next_sync_pts = START_PTS;
  } else {
    this_priv->sync_thread_state = SYNC_THREAD_RUNNING;
    this_priv->next_sync_pts = STOP_PTS;
  }
}

static void metronom_stop_sync_thread (metronom_clock_private_t *this_priv) {

  if (this_priv->sync_thread_state == SYNC_THREAD_NONE) {
    this_priv->next_sync_pts = STOP_PTS;
    return;
  }

  if (this_priv->sync_thread_state != SYNC_THREAD_RUNNING)
    return;

  this_priv->mct.thread_running = 0;

  pthread_mutex_lock (&this_priv->mct.lock);
  pthread_cond_signal (&this_priv->mct.cancel);
  pthread_mutex_unlock (&this_priv->mct.lock);

  pthread_join (this_priv->mct.sync_thread, NULL);

  pthread_cond_destroy (&this_priv->mct.cancel);

  this_priv->sync_thread_state = SYNC_THREAD_OFF;
  this_priv->next_sync_pts = STOP_PTS;
}

static int metronom_register_scr (metronom_clock_t *this, scr_plugin_t *scr) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
  scr_plugin_t **r;

  if (scr->interface_version != 3) {
    xprintf (this->xine, XINE_VERBOSITY_NONE,
      "metronom: wrong interface version for scr provider!\n");
    return -1;
  }

  if (this_priv->providers[0] && !this_priv->providers[1])
    metronom_start_sync_thread (this_priv);

  pthread_mutex_lock (&this_priv->mct.lock);
  for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++) ;
  if (r >= this_priv->providers + MAX_SCR_PROVIDERS) {
    pthread_mutex_unlock (&this_priv->mct.lock);
    return -1; /* No free slot available */
  }

  scr->clock = &this_priv->mct;
  *r = scr;
  this_priv->mct.scr_master = get_master_scr (&this_priv->mct);
  pthread_mutex_unlock (&this_priv->mct.lock);
  return 0;
}

static void metronom_unregister_scr (metronom_clock_t *this, scr_plugin_t *scr) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
  scr_plugin_t **found = NULL, **r;
  int64_t now;

  if (!scr)
    return;

  pthread_mutex_lock (&this_priv->mct.lock);
  /* never unregister scr_list[0]! */
  for (r = this_priv->providers + 1; *r && r < this_priv->providers + MAX_SCR_PROVIDERS; r++) {
    if (*r == scr)
      found = r;
  }
  if (!found) {
    pthread_mutex_unlock (&this_priv->mct.lock);
    return; /* Not found */
  }
  /* avoid holes in list */
  found[0] = r[-1];
  r[-1] = NULL;

  now = this_priv->mct.scr_master->get_current (this_priv->mct.scr_master);
  /* master could have been adjusted, others must follow now */
  for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++)
    if (*r != this_priv->mct.scr_master)
      (*r)->adjust (*r, now);

  this_priv->mct.scr_master = get_master_scr (&this_priv->mct);
  pthread_mutex_unlock (&this_priv->mct.lock);

  if (this_priv->providers[0] && !this_priv->providers[1])
    metronom_stop_sync_thread (this_priv);
}

static void metronom_exit (metronom_t *this_gen) {

  metronom_impl_t *this = (metronom_impl_t *)this_gen;

  this->xine->config->unregister_callbacks (this->xine->config, NULL, NULL, this, sizeof (*this));

  pthread_mutex_destroy (&this->lock);
  pthread_cond_destroy (&this->disc.video_reached);
  pthread_cond_destroy (&this->disc.audio_reached);

  free (this);
}

static void metronom_clock_exit (metronom_clock_t *this) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this;
  scr_plugin_t **r;

  this_priv->mct.xine->config->unregister_callbacks (this_priv->mct.xine->config, NULL, NULL, this_priv, sizeof (*this_priv));

  metronom_stop_sync_thread (this_priv);

  pthread_mutex_lock (&this_priv->mct.lock);
  for (r = this_priv->providers; *r && (r < this_priv->providers + MAX_SCR_PROVIDERS); r++)
    (*r)->exit (*r);
  pthread_mutex_unlock (&this_priv->mct.lock);

  pthread_mutex_destroy (&this_priv->mct.lock);
  free (this_priv);
}


static void metronom_base_av_offs_hook (void *this_gen, xine_cfg_entry_t *entry) {
  metronom_impl_t *this = (metronom_impl_t *)this_gen;
  this->video.base_av_offset = entry->num_value;
}

metronom_t * _x_metronom_init (int have_video, int have_audio, xine_t *xine) {

  metronom_impl_t *this = calloc(1, sizeof (metronom_impl_t));
  if (!this)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  /* Do these first, when compiler still knows "this" is all zeroed.
   * Let it optimize away this on most systems where clear mem
   * interpretes as 0, 0f or NULL safely.
   */
  this->master                 = NULL;
  this->vpts_offset            = 0;
  this->audio.pts_per_smpls    = 0;
  this->audio.last_pts         = 0;
  this->audio.vpts_rmndr       = 0;
  this->audio.vdr_hack         = 0;
  this->audio.seek             = 0;
  this->audio.samples          = 0;
  this->audio.drift_step       = 0;
  this->audio.force_jump       = 0;
  this->video.last_pts         = 0;
  this->video.av_offset        = 0;
  this->video.drift            = 0;
  this->video.drift_step       = 0;
  this->video.img_cpt          = 0;
  this->video.force_jump       = 0;
  this->video.img_duration     = 0;
  this->video.mode             = 0;
  this->spu.vpts               = 0;
  this->spu.offset             = 0;
  this->bounce.diff            = 0;
  this->bounce.vpts_offs       = 0;
  this->bounce.jumped          = 0;
  this->disc.video_count       = 0;
  this->disc.handled_count     = 0;
  this->disc.audio_count       = 0;
  this->disc.num_audio_waiters = 0;
  this->disc.num_video_waiters = 0;
  this->disc.last_offs         = 0;
  this->disc.last_type         = 0;
#endif
  this->bounce.left_audio      = -1;
  this->bounce.left_video      = -1;
  this->metronom.set_audio_rate             = metronom_set_audio_rate;
  this->metronom.got_video_frame            = metronom_got_video_frame;
  this->metronom.got_audio_samples          = metronom_got_audio_samples;
  this->metronom.got_spu_packet             = metronom_got_spu_packet;
  this->metronom.handle_audio_discontinuity = metronom_handle_audio_discontinuity;
  this->metronom.handle_video_discontinuity = metronom_handle_video_discontinuity;
  this->metronom.set_option                 = metronom_set_option;
  this->metronom.get_option                 = metronom_get_option;
  this->metronom.set_master                 = metronom_set_master;
  this->metronom.exit                       = metronom_exit;

  this->xine                       = xine;

  pthread_mutex_init (&this->lock, NULL);

  this->prebuffer = PREBUFFER_PTS_OFFSET;

  /* initialize video stuff */

  this->disc.have_video   = have_video;
  this->video.vpts   = this->prebuffer;
  pthread_cond_init (&this->disc.video_reached, NULL);
  this->video.img_duration = 3000;

  /* initialize audio stuff */

  this->disc.have_audio = have_audio;
  this->audio.vpts = this->prebuffer;
  pthread_cond_init (&this->disc.audio_reached, NULL);

  this->video.base_av_offset = xine->config->register_num (xine->config, "video.output.base_delay", 0,
    _("basic video to audio delay in pts"),
    _("Getting in sync picture and sound is a complex story.\n"
      "Xine will compensate for any delays it knows about.\n"
      "However, external hardware like flatscreens, sound systems, or simply\n"
      "the distance between you and the speakers may add in more.\n"
      "Here you can adjust video timing in steps of 1/90000 seconds manually."),
    10, metronom_base_av_offs_hook, this);

  return &this->metronom;
}


static void metronom_sync_hook (void *this_gen, xine_cfg_entry_t *entry) {
  metronom_clock_private_t *this_priv = (metronom_clock_private_t *)this_gen;

  if (entry->num_value) {
    if (this_priv->sync_thread_state != SYNC_THREAD_NONE)
      return;
    this_priv->sync_thread_state = SYNC_THREAD_OFF;
    this_priv->next_sync_pts = STOP_PTS;
    if (this_priv->providers[1])
      metronom_start_sync_thread (this_priv);
  } else {
    if (this_priv->sync_thread_state == SYNC_THREAD_NONE)
      return;
    metronom_stop_sync_thread (this_priv);
    this_priv->sync_thread_state = SYNC_THREAD_NONE;
    if (this_priv->providers[1])
      this_priv->next_sync_pts = START_PTS;
  }
}

metronom_clock_t *_x_metronom_clock_init(xine_t *xine)
{
  metronom_clock_private_t *this_priv = calloc (1, sizeof (metronom_clock_private_t));

  if (!this_priv)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  this_priv->speed_change_used = 0;
  this_priv->speed_change_callbacks[0] = NULL;
#endif

  this_priv->mct.set_option       = metronom_clock_set_option;
  this_priv->mct.get_option       = metronom_clock_get_option;
  this_priv->mct.start_clock      = metronom_start_clock;
  this_priv->mct.stop_clock       = metronom_stop_clock;
  this_priv->mct.resume_clock     = metronom_resume_clock;
  this_priv->mct.get_current_time = metronom_get_current_time;
  this_priv->mct.adjust_clock     = metronom_adjust_clock;
  this_priv->mct.set_fine_speed   = metronom_set_speed;
  this_priv->mct.register_scr     = metronom_register_scr;
  this_priv->mct.unregister_scr   = metronom_unregister_scr;
  this_priv->mct.exit             = metronom_clock_exit;

  this_priv->mct.register_speed_change_callback   = metronom_register_speed_change_callback;
  this_priv->mct.unregister_speed_change_callback = metronom_unregister_speed_change_callback;

  this_priv->mct.xine             = xine;
  this_priv->mct.scr_adjustable   = 1;
  this_priv->mct.scr_list         = this_priv->providers;

  pthread_mutex_init (&this_priv->mct.lock, NULL);
  this_priv->mct.register_scr (&this_priv->mct, unixscr_init (&this_priv->uscr));

  this_priv->mct.thread_running   = 0;

  this_priv->next_sync_pts = STOP_PTS;

  if (this_priv->mct.xine->config->register_bool (this_priv->mct.xine->config,
    "engine.use_metronom_sync_thread", 0,
    _("Sync multiple clocks in a separate thread"),
    _("Enable this when there are problems with multiple (eg application supplied) clocks."),
    20, metronom_sync_hook, this_priv)) {
    this_priv->sync_thread_state  = SYNC_THREAD_OFF;
  } else {
    this_priv->sync_thread_state  = SYNC_THREAD_NONE;
  }

  return &this_priv->mct;
}

