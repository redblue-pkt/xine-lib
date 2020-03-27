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
 * along with self program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */

/**
 * @file
 * @brief xine-lib audio output implementation
 *
 * @date 2001-08-20 First implementation of Audio sync and Audio driver separation.
 *       (c) 2001 James Courtier-Dutton <james@superbug.demon.co.uk>
 * @date 2001-08-22 James imported some useful AC3 sections from the previous
 *       ALSA driver. (c) 2001 Andy Lo A Foe <andy@alsaplayer.org>
 *
 *
 * General Programming Guidelines: -
 * New concept of an "audio_frame".
 * An audio_frame consists of all the samples required to fill every
 * audio channel to a full amount of bits.
 * So, it does not mater how many bits per sample, or how many audio channels
 * are being used, the number of audio_frames is the same.
 * E.g.  16 bit stereo is 4 bytes, but one frame.
 *       16 bit 5.1 surround is 12 bytes, but one frame.
 * The purpose of this is to make the audio_sync code a lot more readable,
 * rather than having to multiply by the amount of channels all the time
 * when dealing with audio_bytes instead of audio_frames.
 *
 * The number of samples passed to/from the audio driver is also sent
 * in units of audio_frames.
 *
 * Currently, James has tested with OSS: Standard stereo out, SPDIF PCM, SPDIF AC3
 *                                 ALSA: Standard stereo out
 * No testing has been done of ALSA SPDIF AC3 or any 4,5,5.1 channel output.
 * Currently, I don't think resampling functions, as I cannot test it.
 *
 * equalizer based on
 *
 *   PCM time-domain equalizer
 *
 *   Copyright (C) 2002  Felipe Rivera <liebremx at users sourceforge net>
 *
 * heavily modified by guenter bartsch 2003 for use in libxine
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <inttypes.h>
#include <math.h>
#include <sys/time.h>

#define XINE_ENABLE_EXPERIMENTAL_FEATURES

#define LOG_MODULE "audio_out"
#define LOG_VERBOSE
/*
#define LOG
*/

#define LOG_RESAMPLE_SYNC 0

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/audio_out.h>
#include <xine/resample.h>
#include <xine/metronom.h>

#include "xine_private.h"


#define NUM_AUDIO_BUFFERS       32
#define AUDIO_BUF_SIZE       32768

/* By adding gap errors (difference between reported and expected
 * sound card clock) into metronom's vpts_offset we can use its
 * smoothing algorithms to correct sound card clock drifts.
 * obs: previously this error was added to xine scr.
 *
 * audio buf ---> metronom --> audio fifo --> (buf->vpts - hw_vpts)
 *           (vpts_offset + error)                     gap
 *                    <---------- control --------------|
 *
 * Unfortunately audio fifo adds a large delay to our closed loop.
 *
 * The defines below are designed to avoid updating the metronom too fast.
 * - it will only be updated 1 time per second (so it has a chance of
 *   distributing the error for several frames).
 * - it will only be updated 2 times for the whole audio fifo size
 *   length (so the control will wait to see the feedback effect)
 * - each update will be of gap/SYNC_GAP_RATE.
 *
 * Sound card clock correction can only provide smooth playback for
 * errors < 1% nominal rate. For bigger errors (bad streams) audio
 * buffers may be dropped or gaps filled with silence.
 */
#define SYNC_TIME_INTERVAL  (1 * 90000)
#define SYNC_BUF_INTERVAL   NUM_AUDIO_BUFFERS / 2
#define SYNC_GAP_RATE_LOG2  2

/* Alternative for metronom feedback: fix sound card clock drift
 * by resampling all audio data, so that the sound card keeps in
 * sync with the system clock. This may help, if one uses a DXR3/H+
 * decoder board. Those have their own clock (which serves as xine's
 * master clock) and can only operate at fixed frame rates (if you
 * want smooth playback). Resampling then avoids A/V sync problems,
 * gaps filled with 0-frames and jerky video playback due to different
 * clock speeds of the sound card and DXR3/H+.
 */
#define RESAMPLE_SYNC_WINDOW 50
#define RESAMPLE_MAX_GAP_DIFF 150
#define RESAMPLE_REDUCE_GAP_THRESHOLD 200



typedef struct {
  double   last_factor;
  int      window;
  int      reduce_gap;
  uint64_t window_duration, last_vpts;
  int64_t  recent_gap[8], last_avg_gap;
  int      valid;
} resample_sync_t;

/*
 * equalizer stuff
 */

#define EQ_BANDS    10
#define EQ_CHANNELS  8

#define FP_FRBITS 28

#define EQ_REAL(x) ((int)((x) * (1 << FP_FRBITS)))

typedef struct  {
  int beta;
  int alpha;
  int gamma;
} sIIRCoefficients;

static const sIIRCoefficients iir_cf[] = {
  /* 31 Hz*/
  { EQ_REAL(9.9691562441e-01), EQ_REAL(1.5421877947e-03), EQ_REAL(1.9968961468e+00) },
  /* 62 Hz*/
  { EQ_REAL(9.9384077546e-01), EQ_REAL(3.0796122698e-03), EQ_REAL(1.9937629855e+00) },
  /* 125 Hz*/
  { EQ_REAL(9.8774277725e-01), EQ_REAL(6.1286113769e-03), EQ_REAL(1.9874275518e+00) },
  /* 250 Hz*/
  { EQ_REAL(9.7522112569e-01), EQ_REAL(1.2389437156e-02), EQ_REAL(1.9739682661e+00) },
  /* 500 Hz*/
  { EQ_REAL(9.5105628526e-01), EQ_REAL(2.4471857368e-02), EQ_REAL(1.9461077269e+00) },
  /* 1k Hz*/
  { EQ_REAL(9.0450844499e-01), EQ_REAL(4.7745777504e-02), EQ_REAL(1.8852109613e+00) },
  /* 2k Hz*/
  { EQ_REAL(8.1778971701e-01), EQ_REAL(9.1105141497e-02), EQ_REAL(1.7444877599e+00) },
  /* 4k Hz*/
  { EQ_REAL(6.6857185264e-01), EQ_REAL(1.6571407368e-01), EQ_REAL(1.4048592171e+00) },
  /* 8k Hz*/
  { EQ_REAL(4.4861333678e-01), EQ_REAL(2.7569333161e-01), EQ_REAL(6.0518718075e-01) },
  /* 16k Hz*/
  { EQ_REAL(2.4201241845e-01), EQ_REAL(3.7899379077e-01), EQ_REAL(-8.0847117831e-01) },
};

/* XXX: Apart from the typedef in include/xine/audio_out.h, this is used nowhere in xine. */
struct audio_fifo_s {
  audio_buffer_t    *first;
  audio_buffer_t   **add;

  pthread_mutex_t    mutex;
  pthread_cond_t     not_empty;
  pthread_cond_t     empty;

  int                num_buffers;
  int                num_buffers_max;
  int                num_waiters;
};

typedef struct {

  xine_audio_port_t    ao; /* public part */

  /* private stuff */
  struct {
    pthread_mutex_t    mutex;
    pthread_mutex_t    intr_mutex;         /* protects num_driver_actions */
    pthread_cond_t     intr_wake;          /* informs about num_driver_actions-- */
    ao_driver_t       *d;
    uint32_t           open;
    int                intr_num;           /* number of threads, that wish to call
                                            * functions needing driver_lock */
    int                dreqs_all;          /* statistics */
    int                dreqs_wait;
    uint32_t           speed;
    int                trick;              /* play audio even on slow/fast speeds */
  } driver;

  uint32_t             audio_loop_running:1;
  uint32_t             grab_only:1; /* => do not start thread, frontend will consume samples */
  uint32_t             do_resample:1;
  uint32_t             do_compress:1;
  uint32_t             do_amp:1;
  uint32_t             amp_mute:1;
  uint32_t             do_equ:1;


  metronom_clock_t    *clock;
  xine_private_t      *xine;

#define STREAMS_DEFAULT_SIZE 32
  int                  num_null_streams;
  int                  num_anon_streams;
  int                  num_streams;
  int                  streams_size;
  xine_stream_private_t **streams, *streams_default[STREAMS_DEFAULT_SIZE];
  xine_rwlock_t        streams_lock;

  pthread_t       audio_thread;

  uint32_t        audio_step;           /* pts per 32 768 samples (sample = #bytes/2) */
  uint32_t        in_channels;
  uint32_t        out_pts_per_kframe;   /* pts per 1024 frames                        */
  uint32_t        out_frames_per_kpts;  /* frames per 1024/90000 sec                  */
  uint32_t        out_channels;

  int             av_sync_method_conf;
  resample_sync_t resample_sync_info;
  double          resample_sync_factor; /* correct buffer length by this factor
                                         * to sync audio hardware to (dxr3) clock */
  int             resample_sync_method; /* fix sound card clock drift by resampling */

  int             gap_tolerance;
  int             small_gap;            /* gap_tolerance * sqrt (speed) / sqrt (XINE_FINE_SPEED_NORMAL),
                                         * to avoid nervous metronom syncing in trick mode. */

  ao_format_t     input, output;        /* format conversion done at audio_out.c */
  double          frame_rate_factor;
  double          output_frame_excess;  /* used to keep track of 'half' frames */

  int             resample_conf;
  uint32_t        force_rate;           /* force audio output rate to this value if non-zero */

  struct {
    pthread_mutex_t  mutex;
    pthread_cond_t   not_empty;
    audio_buffer_t  *first;
    audio_buffer_t **add;
    int              num_buffers;
    int              num_buffers_max;
    int              num_waiters;
  } free_fifo;

  struct {
    pthread_mutex_t  mutex;
    pthread_cond_t   not_empty;
    pthread_cond_t   empty;
    audio_buffer_t  *first;
    audio_buffer_t **add;
    int              num_buffers;
    int              num_waiters;
    uint32_t         pts_fill;
    int              seek_count1;
    struct timespec  wake_time;            /* time to drop next buf in trick play mode without sound */
    int              use_wake_time;        /* 0 (play), 1 (wake_time or event), 2 (event) */
    int              wake_now;             /* immediate response requested (speed change, shutdown) */
    int              discard_buffers;
    xine_stream_private_t *buf_streams[NUM_AUDIO_BUFFERS];
  } out_fifo;

  struct {
    uint32_t         speed;
    int              trick;
    int64_t          last_flush_vpts;
    int              seek_count2;
    int              seek_count3;
    int              seek_count_n;
    /* extra info ring buffer, compensating for driver delay (not fifo size).
     * beware of good old audio cd who used to send impressive 75 bufs per second. */
#define EI_RING_SIZE 128 /* 2^n please */
    int              ei_write;
    int              ei_read;
  } rp;

  int64_t         last_audio_vpts;

  int16_t	  last_sample[RESAMPLE_MAX_CHANNELS];
  audio_buffer_t *frame_buf[2];         /* two buffers for "stackable" conversions */
  int16_t        *zero_space;

  int             passthrough_offset, ptoffs;

  int             dropped;
  int             step;
  pthread_mutex_t step_mutex;
  pthread_cond_t  done_stepping;

  /* some built-in audio filters */

  double          compression_factor;   /* current compression */
  double          compression_factor_max; /* user limit on compression */
  double          amp_factor;

  /* 10-band equalizer */

  int             eq_settings[EQ_BANDS];
  int             eq_gain[EQ_BANDS];
  /* Coefficient history for the IIR filter */
  int             eq_data_history[EQ_CHANNELS][EQ_BANDS][4];

  int             last_gap;
  int             last_sgap;

  /* If driver cannot pause while keeping its own buffers alive,
   * resend some frames at unpause time instead of filling a big gap with silence. */
  struct {
#define RESEND_BUF_SIZE (1 << 20)
    int           driver_caps;
    int64_t       vpts;
    uint32_t      speed;
    uint32_t      rate;
    uint32_t      bits;
    int           mode;
    int           frame_size;
    int           write;
    int           wrap;
    int           max;
    uint8_t      *buf;
  } resend;

  uint8_t        *base_samp;

  audio_buffer_t  base_buf[NUM_AUDIO_BUFFERS + 2];
  extra_info_t    base_ei[EI_RING_SIZE + NUM_AUDIO_BUFFERS + 2];
} aos_t;

static void ao_driver_lock (aos_t *this) {
  if (pthread_mutex_trylock (&this->driver.mutex)) {
    pthread_mutex_lock (&this->driver.intr_mutex);
    this->driver.intr_num++;
    pthread_mutex_unlock (&this->driver.intr_mutex);

    pthread_mutex_lock (&this->driver.mutex);
    this->driver.dreqs_wait++;

    pthread_mutex_lock (&this->driver.intr_mutex);
    this->driver.intr_num--;
    /* indicate the change to ao_loop() */
    pthread_cond_broadcast (&this->driver.intr_wake);
    pthread_mutex_unlock (&this->driver.intr_mutex);
  }
  this->driver.dreqs_all++;
}

static int ao_driver_lock_2 (aos_t *this) {
  pthread_mutex_lock (&this->driver.mutex);
  if ((this->driver.speed == this->rp.speed) && (this->driver.trick == this->rp.trick))
    return 1;
  pthread_mutex_unlock (&this->driver.mutex);
  return 0;
}

static void ao_driver_unlock (aos_t *this) {
  pthread_mutex_unlock (&this->driver.mutex);
}

static void ao_flush_driver (aos_t *this) {
  if (this->rp.speed == XINE_SPEED_PAUSE)
    return;
  ao_driver_lock (this);
  if (this->driver.open) {
    if (this->driver.d->delay (this->driver.d) > 0) {
      this->driver.d->control (this->driver.d, AO_CTRL_FLUSH_BUFFERS, NULL);
      ao_driver_unlock (this);
      xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: flushed driver.\n");
      return;
    }
  }
  ao_driver_unlock (this);
}

static void ao_driver_test_intr (aos_t *this) {
  /* Give other threads a chance to use functions which require this->driver.mutex to
   * be available. This is needed when using NPTL on Linux (and probably PThreads
   * on Solaris as well). */
  if (this->driver.intr_num > 0) {
    /* calling sched_yield() is not sufficient on multicore systems */
    /* sched_yield(); */
    /* instead wait for the other thread to acquire this->driver.mutex */
    pthread_mutex_lock (&this->driver.intr_mutex);
    if (this->driver.intr_num > 0)
      pthread_cond_wait (&this->driver.intr_wake, &this->driver.intr_mutex);
    pthread_mutex_unlock (&this->driver.intr_mutex);
  }
}

static int ao_driver_test_intr_2 (aos_t *this) {
  int intr;
  pthread_mutex_lock (&this->driver.intr_mutex);
  intr = this->driver.intr_num;
  pthread_mutex_unlock (&this->driver.intr_mutex);
  return intr;
}

static int ao_set_property (xine_audio_port_t *this_gen, int property, int value);

/********************************************************************
 * streams register.                                                *
 * Reading is way more speed relevant here.                         *
 *******************************************************************/

static void ao_streams_open (aos_t *this) {
#ifndef HAVE_ZERO_SAFE_MEM
  this->num_null_streams   = 0;
  this->num_anon_streams   = 0;
  this->num_streams        = 0;
  this->streams_default[0] = NULL;
#endif
  this->streams_size = STREAMS_DEFAULT_SIZE;
  this->streams      = &this->streams_default[0];
  xine_rwlock_init_default (&this->streams_lock);
}

static void ao_streams_close (aos_t *this) {
  xine_rwlock_destroy (&this->streams_lock);
  if (this->streams != &this->streams_default[0])
    _x_freep (&this->streams);
#if 0 /* not yet needed */
  this->num_null_streams = 0;
  this->num_anon_streams = 0;
  this->num_streams      = 0;
  this->streams_size     = 0;
#endif
}

static void ao_streams_register (aos_t *this, xine_stream_private_t *s) {
  xine_rwlock_wrlock (&this->streams_lock);
  if (!s) {
    this->num_null_streams++;
  } else if (&s->s == XINE_ANON_STREAM) {
    this->num_anon_streams++;
  } else do {
    xine_stream_private_t **a = this->streams;
    if (this->num_streams + 2 > this->streams_size) {
      xine_stream_private_t **n = malloc ((this->streams_size + 32) * sizeof (void *));
      if (!n)
        break;
      memcpy (n, a, this->streams_size * sizeof (void *));
      this->streams = n;
      if (a != &this->streams_default[0])
        free (a);
      a = n;
      this->streams_size += 32;
    }
    a[this->num_streams++] = s;
    a[this->num_streams] = NULL;
  } while (0);
  xine_rwlock_unlock (&this->streams_lock);
}

static int ao_streams_unregister (aos_t *this, xine_stream_private_t *s) {
  int n;
  xine_rwlock_wrlock (&this->streams_lock);
  if (!s) {
    this->num_null_streams--;
  } else if (&s->s == XINE_ANON_STREAM) {
    this->num_anon_streams--;
  } else {
    xine_stream_private_t **a = this->streams;
    while (*a && (*a != s))
      a++;
    if (*a) {
      do {
        a[0] = a[1];
        a++;
      } while (*a);
      this->num_streams--;
    }
  }
  n = this->num_null_streams + this->num_anon_streams + this->num_streams;
  xine_rwlock_unlock (&this->streams_lock);
  return n;
}


/********************************************************************
 * reuse buffer stream refs.                                        *
 * be the current owner of buf when calling this.                   *
 *******************************************************************/

static void ao_unref_obsolete (aos_t *this) {
  audio_buffer_t *buf;
  xine_stream_private_t *d[128], **a = d;

  xine_rwlock_rdlock (&this->streams_lock);
  /* same order as elsewhere (eg ao_out_fifo_get ()) !! */
  pthread_mutex_lock (&this->out_fifo.mutex);
  pthread_mutex_lock (&this->free_fifo.mutex);

  for (buf = this->free_fifo.first; buf; buf = buf->next) {
    /* Paranoia? */
    if (PTR_IN_RANGE (buf, this->base_buf, NUM_AUDIO_BUFFERS * sizeof (*buf))) {
      int i = buf - this->base_buf;
      xine_stream_private_t *buf_stream = this->out_fifo.buf_streams[i], **open_stream;
      if (!buf_stream)
        continue;
      for (open_stream = this->streams; *open_stream; open_stream++) {
        if (buf_stream == *open_stream)
          break;
      }
      if (*open_stream)
        continue;
      *a++ = this->out_fifo.buf_streams[i];
      this->out_fifo.buf_streams[i] = NULL;
      if (a > d + sizeof (d) / sizeof (d[0]) - 2)
        break;
    }
  }
  *a = NULL;

  pthread_mutex_unlock (&this->free_fifo.mutex);
  pthread_mutex_unlock (&this->out_fifo.mutex);
  xine_rwlock_unlock (&this->streams_lock);

  if (a > d) {
    for (a = d; *a; a++)
      xine_refs_sub (&(*a)->refs, 1);
    xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
      "audio_out: freed %d obsolete stream refs.\n", (int)(a - d));
  }
}

static void ao_force_unref_all (aos_t *this, int lock) {
  xine_stream_private_t *d[NUM_AUDIO_BUFFERS + 1], **a = d;
  int i;

  if (lock)
    pthread_mutex_lock (&this->out_fifo.mutex);
  for (i = 0; i < NUM_AUDIO_BUFFERS; i++) {
    if (this->out_fifo.buf_streams[i]) {
      *a++ = this->out_fifo.buf_streams[i];
      this->out_fifo.buf_streams[i] = NULL;
    }
  }
  if (lock)
    pthread_mutex_unlock (&this->out_fifo.mutex);
  *a = NULL;

  if (a > d) {
    for (a = d; *a; a++)
      xine_refs_sub (&(*a)->refs, 1);
    xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
      "audio_out: freed %d obsolete stream refs.\n", (int)(a - d));
  }
}

/********************************************************************
 * frame queue (fifo)                                               *
 *******************************************************************/

static void ao_free_fifo_open (aos_t *this) {
#ifndef HAVE_ZERO_SAFE_MEM
  this->free_fifo.first           = NULL;
  this->free_fifo.num_buffers     = 0;
  this->free_fifo.num_buffers_max = 0;
  this->free_fifo.num_waiters     = 0;
#endif
  this->free_fifo.add             = &this->free_fifo.first;
  pthread_mutex_init (&this->free_fifo.mutex, NULL);
  pthread_cond_init  (&this->free_fifo.not_empty, NULL);
}

static void ao_out_fifo_open (aos_t *this) {
#ifndef HAVE_ZERO_SAFE_MEM
  int i;
  for (i = 0; i < NUM_AUDIO_BUFFERS; i++)
    this->out_fifo.buf_streams[i]  = NULL;
  this->out_fifo.first             = NULL;
  this->out_fifo.num_buffers       = 0;
  this->out_fifo.num_waiters       = 0;
  this->out_fifo.pts_fill          = 0;
  this->out_fifo.wake_time.tv_sec  = 0;
  this->out_fifo.wake_time.tv_nsec = 0;
  this->out_fifo.use_wake_time     = 0;
  this->out_fifo.wake_now          = 0;
  this->out_fifo.discard_buffers   = 0;
#endif
  this->out_fifo.add         = &this->out_fifo.first;
  this->out_fifo.seek_count1 = -1;
  pthread_mutex_init (&this->out_fifo.mutex, NULL);
  pthread_cond_init  (&this->out_fifo.not_empty, NULL);
  pthread_cond_init  (&this->out_fifo.empty, NULL);
}

static void ao_free_fifo_close (aos_t *this) {
#if 0 /* not yet needed */
  this->free_fifo.first           = NULL;
  this->free_fifo.add             = &this->free_fifo.first;
  this->free_fifo.num_buffers     = 0;
  this->free_fifo.num_buffers_max = 0;
  this->free_fifo.num_waiters     = 0;
#endif
  pthread_mutex_destroy (&this->free_fifo.mutex);
  pthread_cond_destroy (&this->free_fifo.not_empty);
}

static void ao_out_fifo_close (aos_t *this) {
#if 0 /* not yet needed */
  int i;
  for (i = 0; i < NUM_AUDIO_BUFFERS; i++)
    this->out_fifo.buf_streams[i]  = NULL;
  this->out_fifo.first             = NULL;
  this->out_fifo.add               = &this->out_fifo.first;
  this->out_fifo.num_buffers       = 0;
  this->out_fifo.num_waiters       = 0;
  this->out_fifo.pts_fill          = 0;
  this->out_fifo.wake_time.rv_sec  = 0;
  this->out_fifo.wake_time.tv_nsec = 0;
  this->out_fifo.use_wake_time     = 0;
  this->out_fifo.wake_now          = 0;
  this->out_fifo.seek_count1       = -1;
#endif
  pthread_mutex_destroy (&this->out_fifo.mutex);
  pthread_cond_destroy (&this->out_fifo.not_empty);
  pthread_cond_destroy (&this->out_fifo.empty);
}

static void ao_out_fifo_reref_append (aos_t *this, audio_buffer_t *buf, int is_first) {
  xine_stream_private_t **s, *olds, *news;

  _x_assert (!buf->next);
  buf->next = NULL;

  /* Paranoia? */
  s = PTR_IN_RANGE (buf, this->base_buf, NUM_AUDIO_BUFFERS * sizeof (*buf))
    ? this->out_fifo.buf_streams + (buf - this->base_buf) : &news;
  news = (xine_stream_private_t *)buf->stream;
  pthread_mutex_lock (&this->out_fifo.mutex);
  olds = *s;
  if (olds != news) {

    *s = news;
    if (news)
      xine_refs_add (&news->refs, 1); /* this is fast. */
    this->out_fifo.num_buffers = (this->out_fifo.first ? this->out_fifo.num_buffers : 0) + 1;
    *(this->out_fifo.add)      = buf;
    this->out_fifo.add         = &buf->next;
    if (buf->format.rate)
      this->out_fifo.pts_fill += (uint32_t)90000 * (uint32_t)buf->num_frames / buf->format.rate;
    if (this->out_fifo.num_waiters && (this->out_fifo.first == buf))
      pthread_cond_signal (&this->out_fifo.not_empty);
    if (is_first)
      this->out_fifo.seek_count1 = buf->extra_info->seek_count;
    pthread_mutex_unlock (&this->out_fifo.mutex);
    if (olds)
      xine_refs_sub (&olds->refs, 1); /* this may involve stream dispose. */
  
  } else {

    this->out_fifo.num_buffers = (this->out_fifo.first ? this->out_fifo.num_buffers : 0) + 1;
    *(this->out_fifo.add)      = buf;
    this->out_fifo.add         = &buf->next;
    if (buf->format.rate)
      this->out_fifo.pts_fill += (uint32_t)90000 * (uint32_t)buf->num_frames / buf->format.rate;
    if (this->out_fifo.num_waiters && (this->out_fifo.first == buf))
      pthread_cond_signal (&this->out_fifo.not_empty);
    if (is_first)
      this->out_fifo.seek_count1 = buf->extra_info->seek_count;
    pthread_mutex_unlock (&this->out_fifo.mutex);

  }
}

static void ao_free_fifo_append (aos_t *this, audio_buffer_t *buf) {
  _x_assert (!buf->next);
  buf->next = NULL;

  pthread_mutex_lock (&this->free_fifo.mutex);
  this->free_fifo.num_buffers = (this->free_fifo.first ? this->free_fifo.num_buffers : 0) + 1;
  *(this->free_fifo.add)      = buf;
  this->free_fifo.add         = &buf->next;
  if (this->free_fifo.num_waiters)
    pthread_cond_signal (&this->free_fifo.not_empty);
  pthread_mutex_unlock (&this->free_fifo.mutex);
}

static audio_buffer_t *ao_out_fifo_pop_int (aos_t *this) {
  audio_buffer_t *buf;
  buf                  = this->out_fifo.first;
  this->out_fifo.first = buf->next;
  buf->next            = NULL;
  this->out_fifo.num_buffers--;
  if (!this->out_fifo.first) {
    this->out_fifo.add         = &this->out_fifo.first;
    this->out_fifo.num_buffers = 0;
  }
  return buf;
}

static void ao_out_fifo_signal (aos_t *this) {
  pthread_mutex_lock (&this->out_fifo.mutex);
  this->out_fifo.wake_now = 1;
  if (this->out_fifo.num_waiters)
    pthread_cond_signal (&this->out_fifo.not_empty);
  pthread_mutex_unlock (&this->out_fifo.mutex);
}

static audio_buffer_t *ao_out_fifo_get (aos_t *this, audio_buffer_t *buf) {
  int dry = 0;

  pthread_mutex_lock (&this->out_fifo.mutex);
  while (1) {

    if (this->out_fifo.seek_count1 >= 0) {
      xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: seek_count %d step 2.\n", this->out_fifo.seek_count1);
      this->rp.seek_count2 =
      this->rp.seek_count3 = this->out_fifo.seek_count1;
      this->out_fifo.seek_count1 = -1;
    }

    if (this->out_fifo.discard_buffers) {
      audio_buffer_t *list, **add;
      int n;

      this->rp.last_flush_vpts = this->clock->get_current_time (this->clock);
      this->rp.ei_read = this->rp.ei_write = 0;

      list = NULL;
      add = &list;
      n = 0;
      if (buf) {
        *add = buf;
        add = &buf->next;
        n++;
      }
      if (this->out_fifo.first) {
        n += this->out_fifo.num_buffers;
        *add = this->out_fifo.first;
        add = this->out_fifo.add;
        this->out_fifo.first = NULL;
        this->out_fifo.add = &this->out_fifo.first;
        this->out_fifo.num_buffers = 0;
        this->out_fifo.pts_fill = 0;
      }
      if (n) {
        if ((this->rp.seek_count3 >= 0) && list->stream) {
          xine_stream_private_t *s = (xine_stream_private_t *)list->stream;
          s = s->side_streams[0];
          pthread_mutex_lock (&s->first_frame.lock);
          s->first_frame.flag = 0;
          pthread_cond_broadcast (&s->first_frame.reached);
          pthread_mutex_unlock (&s->first_frame.lock);
        }
        pthread_mutex_lock (&this->free_fifo.mutex);
        this->free_fifo.num_buffers = n + (this->free_fifo.first ? this->free_fifo.num_buffers : 0);
        *(this->free_fifo.add) = list;
        this->free_fifo.add = add;
        if (this->free_fifo.num_waiters)
          pthread_cond_broadcast (&this->free_fifo.not_empty);
        pthread_mutex_unlock (&this->free_fifo.mutex);
      }
      pthread_cond_broadcast (&this->out_fifo.empty);
      buf = NULL;
      this->resend.write = 0;
      this->resend.wrap  = 0;
      xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: flushed out %d buffers.\n", n);
    }

    if (!buf) {
      buf = this->out_fifo.first;
      if (buf) {
        this->out_fifo.first = buf->next;
        buf->next            = NULL;
        if (this->out_fifo.first) {
          this->out_fifo.num_buffers--;
        } else {
          this->out_fifo.add = &this->out_fifo.first;
          this->out_fifo.num_buffers = 0;
          dry = 1;
        }
        if (buf->format.rate)
          this->out_fifo.pts_fill -= (uint32_t)90000 * (uint32_t)buf->num_frames / buf->format.rate;
      }
    }
    if (buf && !this->out_fifo.use_wake_time)
      break;

    if (this->out_fifo.wake_now || !this->audio_loop_running)
      break;

    /* no more bufs for now... */
    if (!this->out_fifo.use_wake_time && this->driver.open) {
      int n;
      xine_rwlock_rdlock (&this->streams_lock);
      n = this->num_null_streams + this->num_anon_streams + this->num_streams;
      xine_rwlock_unlock (&this->streams_lock);
      if (!n) {
        /* ...and no users. When driver runs idle as well, close it. */
        pthread_mutex_lock (&this->driver.mutex);
        n = this->driver.d->delay (this->driver.d);
        pthread_mutex_unlock (&this->driver.mutex);
        n = (n > 0) && this->output.rate ? (uint32_t)n * 1000u / this->output.rate : 0;
        if (n > 0) {
          struct timespec ts = {0, 0};
          xine_gettime (&ts);
          ts.tv_nsec += (n % 1000) * 1000000;
          ts.tv_sec  +=  n / 1000;
          if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
          }
          this->out_fifo.num_waiters++;
          n = pthread_cond_timedwait (&this->out_fifo.not_empty, &this->out_fifo.mutex, &ts);
          this->out_fifo.num_waiters--;
        } else {
          n = ETIMEDOUT;
        }
        if (n == ETIMEDOUT) {
          xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: driver idle, closing.\n");
          pthread_mutex_lock (&this->driver.mutex);
          this->driver.d->close (this->driver.d);
          this->driver.open = 0;
          pthread_mutex_unlock (&this->driver.mutex);
          /* unref streams as well. */
          ao_force_unref_all (this, 0);
        }
        continue;
      }
    }

    this->out_fifo.num_waiters++;
    if (this->out_fifo.use_wake_time == 1) {
      pthread_cond_timedwait (&this->out_fifo.not_empty, &this->out_fifo.mutex, &this->out_fifo.wake_time);
    } else {
      pthread_cond_wait (&this->out_fifo.not_empty, &this->out_fifo.mutex);
    }
    this->out_fifo.use_wake_time = 0;
    this->out_fifo.num_waiters--;
  }

  this->out_fifo.wake_now = 0;
  pthread_mutex_unlock (&this->out_fifo.mutex);

  if (dry)
    xine_nbc_event ((xine_stream_private_t *)buf->stream, XINE_NBC_EVENT_AUDIO_DRY);

  return buf;
}

static void ao_ticket_revoked (void *user_data, int flags) {
  aos_t *this = (aos_t *)user_data;
  const char *s1 = (flags & XINE_TICKET_FLAG_ATOMIC) ? " atomic" : "";
  const char *s2 = (flags & XINE_TICKET_FLAG_REWIRE) ? " port_rewire" : "";
  pthread_cond_signal (&this->free_fifo.not_empty);
  xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: port ticket revoked%s%s.\n", s1, s2);
}

static audio_buffer_t *ao_free_fifo_get (aos_t *this) {
  audio_buffer_t *buf;

  pthread_mutex_lock (&this->free_fifo.mutex);
  while (!(buf = this->free_fifo.first)) {
    if (this->xine->port_ticket->ticket_revoked) {
      pthread_mutex_unlock (&this->free_fifo.mutex);
      this->xine->port_ticket->renew (this->xine->port_ticket, 1);
      if (!(this->xine->port_ticket->ticket_revoked & XINE_TICKET_FLAG_REWIRE)) {
        pthread_mutex_lock (&this->free_fifo.mutex);
        continue;
      }
      /* O dear. Port rewiring ahead. Try unblock. */
      if (this->clock->speed == XINE_SPEED_PAUSE) {
        pthread_mutex_lock (&this->out_fifo.mutex);
        if (this->out_fifo.first) {
          buf = ao_out_fifo_pop_int (this);
          pthread_mutex_unlock (&this->out_fifo.mutex);
          xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: try unblocking decoder.\n");
          return buf;
        }
        pthread_mutex_unlock (&this->out_fifo.mutex);
      }
      pthread_mutex_lock (&this->free_fifo.mutex);
    }
    {
      struct timespec ts = {0, 0};
      xine_gettime (&ts);
      ts.tv_sec += 1;
      this->free_fifo.num_waiters++;
      pthread_cond_timedwait (&this->free_fifo.not_empty, &this->free_fifo.mutex, &ts);
      this->free_fifo.num_waiters--;
    }
  }

  this->free_fifo.first = buf->next;
  buf->next = NULL;
  this->free_fifo.num_buffers--;
  if (!this->free_fifo.first) {
    this->free_fifo.add = &this->free_fifo.first;
    this->free_fifo.num_buffers = 0;
  }
  pthread_mutex_unlock (&this->free_fifo.mutex);
  return buf;
}

/* This function is currently not needed */
#if 0
static int ao_fifo_num_buffers (audio_fifo_t *fifo) {

  int ret;

  pthread_mutex_lock (&fifo->mutex);
  ret = fifo->num_buffers;
  pthread_mutex_unlock (&fifo->mutex);

  return ret;
}
#endif

/* have this->out_fifo.mutex locked */
static void ao_out_fifo_manual_flush (aos_t *this) {
  if (this->out_fifo.first) {
    audio_buffer_t *list = NULL, **add = &list;
    int n = this->out_fifo.num_buffers;
    *add = this->out_fifo.first;
    add = this->out_fifo.add;
    this->out_fifo.first = NULL;
    this->out_fifo.add = &this->out_fifo.first;
    this->out_fifo.num_buffers = 0;
    this->out_fifo.pts_fill = 0;
    pthread_mutex_lock (&this->free_fifo.mutex);
    this->free_fifo.num_buffers = n + (this->free_fifo.first ? this->free_fifo.num_buffers : 0);
    *(this->free_fifo.add) = list;
    this->free_fifo.add = add;
    pthread_mutex_unlock (&this->free_fifo.mutex);
  }
  if (this->free_fifo.first && this->free_fifo.num_waiters)
    pthread_cond_broadcast (&this->free_fifo.not_empty);
}

static void ao_out_fifo_loop_flush (aos_t *this) {
  int n;
  pthread_mutex_lock (&this->out_fifo.mutex);
  this->out_fifo.discard_buffers++;
  while (this->out_fifo.first) {
    /* i think it's strange to send not_empty signal here (beside the enqueue
     * function), but it should do no harm. [MF]
     * TJ. and its needed now that ao loop no longer polls. */
    if (this->out_fifo.num_waiters)
      pthread_cond_signal (&this->out_fifo.not_empty);
    pthread_cond_wait (&this->out_fifo.empty, &this->out_fifo.mutex);
  }
  this->out_fifo.discard_buffers--;
  n = this->out_fifo.discard_buffers;
  pthread_mutex_unlock (&this->out_fifo.mutex);
  if (n == 0)
    ao_flush_driver (this);
}


static void ao_resend_init (aos_t *this) {
  do {
    this->resend.driver_caps = this->driver.d->get_capabilities (this->driver.d);
    if (!(this->resend.driver_caps & AO_CAP_NO_UNPAUSE))
      break;
    if ((this->output.mode == AO_CAP_MODE_A52) || (this->output.mode == AO_CAP_MODE_AC5))
      break;
    if (!this->resend.buf)
      this->resend.buf = malloc (RESEND_BUF_SIZE);
    if (!this->resend.buf)
      break;
    if (this->rp.speed == XINE_SPEED_PAUSE)
      return;
    if ((this->resend.speed == this->rp.speed) &&
        (this->resend.rate  == this->output.rate)   &&
        (this->resend.mode  == this->output.mode)   &&
        (this->resend.bits  == this->output.bits))
      return;
    this->resend.speed = this->rp.speed;
    this->resend.rate  = this->output.rate;
    this->resend.mode  = this->output.mode;
    this->resend.bits  = this->output.bits;
    this->resend.frame_size = (this->output.bits >> 3) * this->out_channels;
    if (!this->resend.frame_size)
      break;
    this->resend.write = 0;
    this->resend.wrap  = 0;
    this->resend.max = RESEND_BUF_SIZE / this->resend.frame_size;
    xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
      "audio_out: using unpause resend buffer for %d frames / %d pts.\n",
      this->resend.max, (this->resend.max * this->out_pts_per_kframe) >> 10);
    return;
  } while (0);
  this->resend.driver_caps &= ~AO_CAP_NO_UNPAUSE;
  this->resend.write = 0;
  this->resend.wrap  = 0;
}

static void ao_resend_store (aos_t *this, audio_buffer_t *buf) {
  uint8_t *q = this->resend.buf + this->resend.write * this->resend.frame_size;
  int n1 = buf->num_frames;
  int n2 = this->resend.write + n1;
  if (n2 > this->resend.max) {
    n2 -= this->resend.max;
    n1 -= n2;
    this->resend.write = n2;
    this->resend.wrap = 1;
    n1 *= this->resend.frame_size;
    xine_fast_memcpy (q, buf->mem, n1);
    xine_fast_memcpy (this->resend.buf, buf->mem + n1, n2 * this->resend.frame_size);
  } else if (n2 == this->resend.max) {
    this->resend.wrap = 1;
    this->resend.write = 0;
    n1 *= this->resend.frame_size;
    xine_fast_memcpy (q, buf->mem, n1);
  } else {
    this->resend.write = n2;
    n1 *= this->resend.frame_size;
    xine_fast_memcpy (q, buf->mem, n1);
  }
  this->resend.vpts = (((uint32_t)buf->num_frames * this->out_pts_per_kframe) >> 10) + buf->vpts;
}


static int ao_fill_gap (aos_t *this, int64_t pts_len) {
  static const uint16_t a52_pause_head[4] = {
    0xf872,
    0x4e1f,
    /* Audio ES Channel empty, wait for DD Decoder or pause */
    0x0003,
    0x0020
  };
  int64_t num_frames = (pts_len * this->out_frames_per_kpts) >> 10;

  xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
           "audio_out: inserting %" PRId64 " 0-frames to fill a gap of %" PRId64 " pts\n", num_frames, pts_len);

  if ((this->output.mode == AO_CAP_MODE_A52) || (this->output.mode == AO_CAP_MODE_AC5)) {

    memcpy (this->zero_space, a52_pause_head, sizeof (a52_pause_head));
    while (num_frames > 1536) {
      if (ao_driver_test_intr_2 (this))
        return 1;
      if (this->driver.open)
        this->driver.d->write (this->driver.d, this->zero_space, 1536);
      num_frames -= 1536;
    }

  } else {

    int max_frames = this->out_channels * (this->output.bits >> 3);
    max_frames = max_frames ? AUDIO_BUF_SIZE / max_frames : 4096;
    memset (this->zero_space, 0, sizeof (a52_pause_head));
    while ((num_frames >= max_frames) && !this->out_fifo.discard_buffers) {
      if (ao_driver_test_intr_2 (this))
        return 1;
      if (this->driver.open)
        this->driver.d->write (this->driver.d, this->zero_space, max_frames);
      num_frames -= max_frames;
    }
    if (num_frames && !this->out_fifo.discard_buffers) {
      if (ao_driver_test_intr_2 (this))
        return 1;
      if (this->driver.open)
        this->driver.d->write (this->driver.d, this->zero_space, num_frames);
    }

  }
  return 0;
}

static int ao_resend_fill (aos_t *this, int64_t pts_len, int64_t end_time) {
  int resend_have = this->resend.wrap ? this->resend.max : this->resend.write;
  int64_t resend_start = this->resend.vpts - ((resend_have * this->out_pts_per_kframe) >> 10);
  int64_t fill_start = end_time - pts_len;
  int64_t fill_len;

  fill_len = resend_start - fill_start;
  if (fill_len > 0) {
    pts_len -= fill_len;
    if (pts_len < 0) {
      fill_len += pts_len;
      pts_len = 0;
    }
    if (ao_fill_gap (this, fill_len))
      return 1;
    fill_start += fill_len;
  }
  if (pts_len == 0)
    return 0;

  fill_len = this->resend.vpts - fill_start;
  if (fill_len > 0) {
    int start_frame, fill_frames1, fill_frames2;
    start_frame = (fill_len * this->out_frames_per_kpts) >> 10;
    pts_len -= fill_len;
    if (pts_len < 0) {
      fill_len += pts_len;
      pts_len = 0;
    }
    fill_frames1 = (fill_len * this->out_frames_per_kpts) >> 10;
    xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
        "audio_out: resending %d frames / %" PRId64 " pts.\n", fill_frames1, fill_len);
    start_frame = this->resend.write - start_frame;
    if (start_frame < 0)
      start_frame += this->resend.max;
    fill_frames2 = this->resend.max - start_frame;
    if (fill_frames2 < fill_frames1) {
      fill_frames1 -= fill_frames2;
      if (!this->out_fifo.discard_buffers) {
        if (ao_driver_test_intr_2 (this))
          return 1;
        if (this->driver.open)
          this->driver.d->write (this->driver.d, (int16_t *)(this->resend.buf + start_frame * this->resend.frame_size), fill_frames2);
      }
      if (!this->out_fifo.discard_buffers) {
        if (ao_driver_test_intr_2 (this))
          return 1;
        if (this->driver.open)
          this->driver.d->write (this->driver.d, (int16_t *)this->resend.buf, fill_frames1);
      }
    } else {
      if (!this->out_fifo.discard_buffers) {
        if (ao_driver_test_intr_2 (this))
          return 1;
        if (this->driver.open)
          this->driver.d->write (this->driver.d, (int16_t *)(this->resend.buf + start_frame * this->resend.frame_size), fill_frames1);
      }
    }
  }

  if (pts_len > 0)
    return ao_fill_gap (this, pts_len);
  return 0;
}


static void ensure_buffer_size (audio_buffer_t *buf, int bytes_per_frame,
                                int frames)
{
  int size = bytes_per_frame * frames;

  if (buf->mem_size < size) {
    buf->mem = realloc( buf->mem, size );
    buf->mem_size = size;
  }
  buf->num_frames = frames;
}

static audio_buffer_t * swap_frame_buffers ( aos_t *this ) {
  audio_buffer_t *tmp;

  tmp = this->frame_buf[1];
  this->frame_buf[1] = this->frame_buf[0];
  this->frame_buf[0] = tmp;
  return this->frame_buf[0];
}

int _x_ao_mode2channels( int mode ) {
  switch( mode ) {
  case AO_CAP_MODE_MONO:
    return 1;
  case AO_CAP_MODE_STEREO:
    return 2;
  case AO_CAP_MODE_4CHANNEL:
    return 4;
  case AO_CAP_MODE_4_1CHANNEL:
  case AO_CAP_MODE_5CHANNEL:
  case AO_CAP_MODE_5_1CHANNEL:
    return 6;
  }
  return 0;
}

int _x_ao_channels2mode (int channels) {
  static const int modes[9] = {
    AO_CAP_NOCAP,
    AO_CAP_MODE_MONO,
    AO_CAP_MODE_STEREO,
    AO_CAP_MODE_4CHANNEL,
    AO_CAP_MODE_4CHANNEL,
    AO_CAP_MODE_5CHANNEL,
    AO_CAP_MODE_5_1CHANNEL,
    AO_CAP_NOCAP,
    AO_CAP_NOCAP
  };
  return modes[(channels >= 0) && (channels < 9) ? channels : 0];
}

static void audio_filter_compress (aos_t *this, int16_t *mem, int num_frames) {

  int    i, maxs;
  double f_max;
  int    num_channels;

  num_channels = this->in_channels;
  if (!num_channels)
    return;

  maxs = 0;

  /* measure */

  for (i=0; i<num_frames*num_channels; i++) {
    int16_t sample = abs(mem[i]);
    if (sample>maxs)
      maxs = sample;
  }

  /* calc maximum possible & allowed factor */

  if (maxs>0) {
    f_max = 32767.0 / maxs;
    this->compression_factor = this->compression_factor * 0.999 + f_max * 0.001;
    if (this->compression_factor > f_max)
      this->compression_factor = f_max;

    if (this->compression_factor > this->compression_factor_max)
      this->compression_factor = this->compression_factor_max;
  } else
    f_max = 1.0;

  lprintf ("max=%d f_max=%f compression_factor=%f\n", maxs, f_max, this->compression_factor);

  /* apply it */

  for (i=0; i<num_frames*num_channels; i++) {
    /* 0.98 to avoid overflow */
    mem[i] = mem[i] * 0.98 * this->compression_factor * this->amp_factor;
  }
}

static void audio_filter_amp (aos_t *this, void *buf, int num_frames) {
  double amp_factor;
  int    i;
  const int total_frames = num_frames * this->in_channels;

  if (!total_frames)
    return;

  amp_factor=this->amp_factor;
  if (this->amp_mute || amp_factor == 0) {
    memset (buf, 0, total_frames * (this->input.bits / 8));
    return;
  }

  if (this->input.bits == 8) {
    int16_t test;
    int8_t *mem = (int8_t *) buf;

    for (i=0; i<total_frames; i++) {
      test = mem[i] * amp_factor;
      /* Force limit on amp_factor to prevent clipping */
      if (test < INT8_MIN) {
        this->amp_factor = amp_factor = amp_factor * INT8_MIN / test;
	test=INT8_MIN;
      }
      if (test > INT8_MAX) {
        this->amp_factor = amp_factor = amp_factor * INT8_MIN / test;
	test=INT8_MAX;
      }
      mem[i] = test;
    }
  } else if (this->input.bits == 16) {
    int32_t test;
    int16_t *mem = (int16_t *) buf;

    for (i=0; i<total_frames; i++) {
      test = mem[i] * amp_factor;
      /* Force limit on amp_factor to prevent clipping */
      if (test < INT16_MIN) {
        this->amp_factor = amp_factor = amp_factor * INT16_MIN / test;
	test=INT16_MIN;
      }
      if (test > INT16_MAX) {
        this->amp_factor = amp_factor = amp_factor * INT16_MIN / test;
	test=INT16_MAX;
      }
      mem[i] = test;
    }
  }
}

static void ao_eq_update (aos_t *this) {
  /* TJ. gxine assumes a setting range of 0..100, with 100 being the default.
     Lets try to fix that very broken api like this:
     1. If all settings are the same, disable eq.
     2. A setting step of 1 means 0.5 dB relative.
     3. The highest setting refers to 0 dB absolute. */
  int smin, smax, i;
  smin = smax = this->eq_settings[0];
  for (i = 1; i < EQ_BANDS; i++) {
    if (this->eq_settings[i] < smin)
      smin = this->eq_settings[i];
    else if (this->eq_settings[i] > smax)
      smax = this->eq_settings[i];
  }
  if (smin == smax) {
    this->do_equ = 0;
  } else {
    for (i = 0; i < EQ_BANDS; i++) {
      uint32_t setting = smax - this->eq_settings[i];
      if (setting > 99) {
        this->eq_gain[i] = EQ_REAL (0.0);
      } else {
        static const int mant[12] = {
          EQ_REAL (1.0),        EQ_REAL (0.94387431), EQ_REAL (0.89089872),
          EQ_REAL (0.84089642), EQ_REAL (0.79370053), EQ_REAL (0.74915354),
          EQ_REAL (0.70710678), EQ_REAL (0.66741993), EQ_REAL (0.62996052),
          EQ_REAL (0.59460355), EQ_REAL (0.56123102), EQ_REAL (0.52973155)
        };
        uint32_t exp = setting / 12;
        setting = setting % 12;
        this->eq_gain[i] = mant[setting] >> exp;
      }
    }
    /* Not very precise but better than nothing... */
    if (this->input.rate < 15000) {
      for (i = EQ_BANDS - 1; i > 1; i--)
        this->eq_gain[i] = this->eq_gain[i - 2];
      this->eq_gain[1] = this->eq_gain[0] = EQ_REAL (1.0);
    } else if (this->input.rate < 30000) {
      for (i = EQ_BANDS - 1; i > 0; i--)
        this->eq_gain[i] = this->eq_gain[i - 1];
      this->eq_gain[0] = EQ_REAL (1.0);
    } else if (this->input.rate > 60000) {
      for (i = 0; i < EQ_BANDS - 1; i++)
        this->eq_gain[i] = this->eq_gain[i + 1];
      this->eq_gain[EQ_BANDS - 1] = EQ_REAL (1.0);
    }
    this->do_equ = 1;
  }
}

#define sat16(v) (((v + 0x8000) & ~0xffff) ? ((v) >> 31) ^ 0x7fff : (v))

static void audio_filter_equalize (aos_t *this,
				   int16_t *data, int num_frames) {
  int       index, band, channel;
  int       length;
  int       num_channels;

  num_channels = this->in_channels;
  if (!num_channels)
    return;

  length = num_frames * num_channels;

  for (index = 0; index < length; index += num_channels) {

    for (channel = 0; channel < num_channels; channel++) {

      /* Convert the PCM sample to a fixed fraction */
      int scaledpcm = ((int)data[index + channel]) << (FP_FRBITS - 16);
      int out = 0;
      /*  For each band */
      for (band = 0; band < EQ_BANDS; band++) {
        int64_t l;
        int v;
        int *p = &this->eq_data_history[channel][band][0];
        l = (int64_t)iir_cf[band].alpha * (scaledpcm - p[1])
          + (int64_t)iir_cf[band].gamma * p[2]
          - (int64_t)iir_cf[band].beta  * p[3];
        p[1] = p[0]; p[0] = scaledpcm;
        p[3] = p[2]; p[2] = v = (int)(l >> FP_FRBITS);
        l = (int64_t)v * this->eq_gain[band];
        out += (int)(l >> FP_FRBITS);
      }
      /* Adjust the fixed point fraction value to a PCM sample */
      /* Scale back to a 16bit signed int */
      out >>= (FP_FRBITS - 16);
      /* Limit the output */
      data[index+channel] = sat16 (out);
    }
  }

}

static audio_buffer_t* prepare_samples( aos_t *this, audio_buffer_t *buf) {
  double          acc_output_frames;
  int             num_output_frames ;

  /*
   * volume / compressor / equalizer filter
   */

  if (this->amp_factor == 0) {
    if (this->do_amp)
      audio_filter_amp (this, buf->mem, buf->num_frames);
  } else if (this->input.bits == 16) {
    if (this->do_equ)
      audio_filter_equalize (this, buf->mem, buf->num_frames);
    if (this->do_compress)
      audio_filter_compress (this, buf->mem, buf->num_frames);
    if (this->do_amp)
      audio_filter_amp (this, buf->mem, buf->num_frames);
  } else if (this->input.bits == 8) {
    if (this->do_amp)
      audio_filter_amp (this, buf->mem, buf->num_frames);
  }


  /*
   * resample and output audio data
   */

  /* calculate number of output frames (after resampling) */
  acc_output_frames = (double) buf->num_frames * this->frame_rate_factor
    * this->resample_sync_factor + this->output_frame_excess;

  /* Truncate to an integer */
  num_output_frames = acc_output_frames;

  /* Keep track of the amount truncated */
  this->output_frame_excess = acc_output_frames - (double) num_output_frames;
  if ( this->output_frame_excess != 0 &&
       !this->do_resample && !this->resample_sync_method)
    this->output_frame_excess = 0;

  lprintf ("outputting %d frames\n", num_output_frames);

  /* convert 8 bit samples as needed */
  if ( this->input.bits == 8 &&
       (this->resample_sync_method || this->do_resample ||
        this->output.bits != 8 || this->input.mode != this->output.mode) ) {
    int channels = this->in_channels;
    ensure_buffer_size(this->frame_buf[1], 2*channels, buf->num_frames );
    _x_audio_out_resample_8to16((int8_t *)buf->mem, this->frame_buf[1]->mem,
                                channels * buf->num_frames );
    buf = swap_frame_buffers(this);
  }

  /* check if resampling may be skipped */
  if ( (this->resample_sync_method || this->do_resample) &&
       buf->num_frames != num_output_frames ) {
    switch (this->input.mode) {
    case AO_CAP_MODE_MONO:
      ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3), num_output_frames);
      _x_audio_out_resample_mono (this->last_sample, buf->mem, buf->num_frames,
			       this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_STEREO:
      ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3)*2, num_output_frames);
      _x_audio_out_resample_stereo (this->last_sample, buf->mem, buf->num_frames,
				 this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_4CHANNEL:
      ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3)*4, num_output_frames);
      _x_audio_out_resample_4channel (this->last_sample, buf->mem, buf->num_frames,
				   this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_4_1CHANNEL:
    case AO_CAP_MODE_5CHANNEL:
    case AO_CAP_MODE_5_1CHANNEL:
      ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3)*6, num_output_frames);
      _x_audio_out_resample_6channel (this->last_sample, buf->mem, buf->num_frames,
				   this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_A52:
    case AO_CAP_MODE_AC5:
      /* pass-through modes: no resampling */
      break;
    }
  } else {
    /* maintain last_sample in case we need it */
    switch (this->input.mode) {
    case AO_CAP_MODE_MONO:
      memcpy (this->last_sample, &buf->mem[buf->num_frames - 1], sizeof (this->last_sample[0]));
      break;
    case AO_CAP_MODE_STEREO:
      memcpy (this->last_sample, &buf->mem[(buf->num_frames - 1) * 2], 2 * sizeof (this->last_sample[0]));
      break;
    case AO_CAP_MODE_4CHANNEL:
      memcpy (this->last_sample, &buf->mem[(buf->num_frames - 1) * 4], 4 * sizeof (this->last_sample[0]));
      break;
    case AO_CAP_MODE_4_1CHANNEL:
    case AO_CAP_MODE_5CHANNEL:
    case AO_CAP_MODE_5_1CHANNEL:
      memcpy (this->last_sample, &buf->mem[(buf->num_frames - 1) * 6], 6 * sizeof (this->last_sample[0]));
      break;
    default:;
    }
  }

  /* mode conversion */
  if ( this->input.mode != this->output.mode ) {
    switch (this->input.mode) {
    case AO_CAP_MODE_MONO:
      if( this->output.mode == AO_CAP_MODE_STEREO ) {
	ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3)*2, buf->num_frames );
	_x_audio_out_resample_monotostereo(buf->mem, this->frame_buf[1]->mem,
					   buf->num_frames );
	buf = swap_frame_buffers(this);
      }
      break;
    case AO_CAP_MODE_STEREO:
      if( this->output.mode == AO_CAP_MODE_MONO ) {
	ensure_buffer_size(this->frame_buf[1], (this->output.bits>>3), buf->num_frames );
	_x_audio_out_resample_stereotomono(buf->mem, this->frame_buf[1]->mem,
					   buf->num_frames );
	buf = swap_frame_buffers(this);
      }
      break;
    case AO_CAP_MODE_4CHANNEL:
      break;
    case AO_CAP_MODE_5CHANNEL:
      break;
    case AO_CAP_MODE_5_1CHANNEL:
      break;
    case AO_CAP_MODE_A52:
    case AO_CAP_MODE_AC5:
      break;
    }
  }

  /* convert back to 8 bits after resampling */
  if( this->output.bits == 8 &&
        (this->resample_sync_method || this->do_resample ||
         this->input.mode != this->output.mode) ) {
    int channels = this->out_channels;
    ensure_buffer_size(this->frame_buf[1], channels, buf->num_frames );
    _x_audio_out_resample_16to8(buf->mem, (int8_t *)this->frame_buf[1]->mem,
                                channels * buf->num_frames );
    buf = swap_frame_buffers(this);
  }
  return buf;
}


static int resample_rate_adjust(aos_t *this, int64_t gap, audio_buffer_t *buf) {

  /* Calculates the drift factor used to resample the audio data to
   * keep in sync with system (or dxr3) clock.
   *
   * To compensate the sound card drift it is necessary to know, how many audio
   * frames need to be added (or removed) via resampling. This function waits for
   * RESAMPLE_SYNC_WINDOW audio buffers to be sent to the card and keeps track
   * of their total duration in vpts. With the measured gap difference between
   * the reported gap values at the beginning and at the end of this window the
   * required resampling factor is calculated:
   *
   * resample_factor = (duration + gap_difference) / duration
   *
   * This factor is then used in prepare_samples() to resample the audio
   * buffers as needed so we keep in sync with the system (or dxr3) clock.
   */

  resample_sync_t *info = &this->resample_sync_info;
  int64_t avg_gap = 0;
  double factor;
  double diff;
  double duration;
  int i;

  if (llabs(gap) > AO_MAX_GAP) {
    /* drop buffers or insert 0-frames in audio out loop */
    info->valid = 0;
    return -1;
  }

  if ( ! info->valid) {
    this->resample_sync_factor = 1.0;
    info->window = 0;
    info->reduce_gap = 0;
    info->last_avg_gap = gap;
    info->last_factor = 0;
    info->window_duration = info->last_vpts = 0;
    info->valid = 1;
  }

  /* calc average gap (to compensate small errors during measurement) */
  for (i = 0; i < 7; i++) info->recent_gap[i] = info->recent_gap[i + 1];
  info->recent_gap[i] = gap;
  for (i = 0; i < 8; i++) avg_gap += info->recent_gap[i];
  avg_gap /= 8;


  /* gap too big? Change sample rate so that gap converges towards 0. */

  if (llabs(avg_gap) > RESAMPLE_REDUCE_GAP_THRESHOLD && !info->reduce_gap) {
    info->reduce_gap = 1;
    this->resample_sync_factor = (avg_gap < 0) ? 0.995 : 1.005;

    llprintf (LOG_RESAMPLE_SYNC,
              "sample rate adjusted to reduce gap: gap=%" PRId64 "\n", avg_gap);
    return 0;

  } else if (info->reduce_gap && llabs(avg_gap) < 50) {
    info->reduce_gap = 0;
    info->valid = 0;
    llprintf (LOG_RESAMPLE_SYNC, "gap successfully reduced\n");
    return 0;

  } else if (info->reduce_gap) {
    /* re-check, because the gap might suddenly change its sign,
     * also slow down, when getting close to zero (-300<gap<300) */
    if (llabs(avg_gap) > 300)
      this->resample_sync_factor = (avg_gap < 0) ? 0.995 : 1.005;
    else
      this->resample_sync_factor = (avg_gap < 0) ? 0.998 : 1.002;
    return 0;
  }


  if (info->window > RESAMPLE_SYNC_WINDOW) {

    /* adjust drift correction */

    int64_t gap_diff = avg_gap - info->last_avg_gap;

    if (gap_diff < RESAMPLE_MAX_GAP_DIFF) {
#if LOG_RESAMPLE_SYNC
      int num_frames;

      /* if we are already resampling to a different output rate, consider
       * this during calculation */
      num_frames = (this->do_resample) ? (buf->num_frames * this->frame_rate_factor)
        : buf->num_frames;
      printf("audio_out: gap=%5" PRId64 ";  gap_diff=%5" PRId64 ";  frame_diff=%3.0f;  drift_factor=%f\n",
             avg_gap, gap_diff, num_frames * info->window * info->last_factor,
             this->resample_sync_factor);
#endif
      /* we want to add factor * num_frames to each buffer */
      diff = gap_diff;
#if _MSCVER <= 1200
      /* ugly hack needed by old Visual C++ 6.0 */
      duration = (int64_t)info->window_duration;
#else
      duration = info->window_duration;
#endif
      factor = diff / duration + info->last_factor;

      info->last_factor = factor;
      this->resample_sync_factor = 1.0 + factor;

      info->last_avg_gap = avg_gap;
      info->window_duration = 0;
      info->window = 0;
    } else
      info->valid = 0;

  } else {

    /* collect data for next adjustment */
    if (info->window > 0)
      info->window_duration += buf->vpts - info->last_vpts;
    info->last_vpts = buf->vpts;
    info->window++;
  }

  return 0;
}

static int ao_change_settings(aos_t *this, xine_stream_t *stream, uint32_t bits, uint32_t rate, int mode);
static int ao_update_resample_factor (aos_t *this);

/* Audio output loop: -
 * 1) Check for pause.
 * 2) Make sure audio hardware is in RUNNING state.
 * 3) Get delay
 * 4) Do drop, 0-fill or output samples.
 * 5) Go round loop again.
 */
static void *ao_loop (void *this_gen) {

  aos_t *this = (aos_t *) this_gen;
  audio_buffer_t *in_buf = NULL;
  int64_t         cur_time = -1;
  int64_t         next_sync_time = SYNC_TIME_INTERVAL;
  int             bufs_since_sync = 0;

  pthread_mutex_lock (&this->driver.mutex);
  this->rp.speed = this->driver.speed;
  this->rp.trick = this->driver.trick;
  pthread_mutex_unlock (&this->driver.mutex);

  while (this->audio_loop_running) {

    xine_stream_private_t *stream;
    int64_t         gap;
    int             delay;
    int             drop = 0;

    /* handle buf */
    do {
      /* get buffer to process for this loop iteration */
      {
        audio_buffer_t *last = in_buf;
        lprintf ("loop: get buf from fifo\n");
        in_buf = ao_out_fifo_get (this, in_buf);
        if (!in_buf)
          break;
        if (in_buf->num_frames <= 0) {
          /* drop empty buf */
          drop = 1;
          break;
        }
        stream = (xine_stream_private_t *)in_buf->stream;
        if (!last) {
          bufs_since_sync++;
          lprintf ("got a buffer\n");
          /* If there is no video stream to update extra info, queue this */
          if (stream) {
            if (!stream->video_decoder_plugin && !in_buf->extra_info->invalid) {
              int i = this->rp.ei_write;
              this->base_ei[i] = in_buf->extra_info[0];
              this->rp.ei_write = (i + 1) & (EI_RING_SIZE - 1);
            }
          }
        }
      }
      if (!this->audio_loop_running)
        break;

      /* wait until user unpauses stream.
       * if we are playing at a different speed (without speed.trick flag)
       * we must process/free buffers otherwise the entire engine will stop.
       * next 2 vars are updated via this->driver.mutex and/or out_fifo.mutex. */
      this->rp.trick = this->driver.trick;
      if (this->rp.speed != this->driver.speed) {
        this->rp.speed = this->driver.speed;
        ao_update_resample_factor (this);
      }

      if ((this->rp.speed == XINE_SPEED_PAUSE) ||
         ((this->rp.speed != XINE_FINE_SPEED_NORMAL) && !this->rp.trick)) {

        cur_time = this->clock->get_current_time (this->clock);

        if ((this->rp.speed != XINE_SPEED_PAUSE) || this->step) {
          if (in_buf->vpts < cur_time) {
            this->dropped++;
            drop = 1;
            break;
          }
          if (this->step) {
            pthread_mutex_lock (&this->step_mutex);
            this->step = 0;
            pthread_cond_broadcast (&this->done_stepping);
            pthread_mutex_unlock (&this->step_mutex);
            if (this->dropped)
              xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
                "audio_out: SINGLE_STEP: dropped %d buffers.\n", this->dropped);
          }
          this->dropped = 0;
          if ((in_buf->vpts - cur_time) > 2 * 90000)
            xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
              "audio_out: vpts/clock error, in_buf->vpts=%" PRId64 " cur_time=%" PRId64 "\n", in_buf->vpts, cur_time);
        }

        {
          extra_info_t *found = NULL;
          while (this->rp.ei_read != this->rp.ei_write) {
            extra_info_t *ei = &this->base_ei[this->rp.ei_read];
            if (ei->vpts > cur_time)
              break;
            found = ei;
            this->rp.ei_read = (this->rp.ei_read + 1) & (EI_RING_SIZE - 1);
          }
          if (found && stream) {
            xine_stream_private_t *m = stream->side_streams[0];
            xine_current_extra_info_set (m, found);
            if (found->seek_count == this->rp.seek_count3) {
              xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: seek_count %d step 3.\n", found->seek_count);
              this->rp.seek_count3 = -1;
              pthread_mutex_lock (&m->first_frame.lock);
              m->first_frame.flag = 0;
              pthread_cond_broadcast (&m->first_frame.reached);
              pthread_mutex_unlock (&m->first_frame.lock);
            }
          }
        }

        if (this->rp.speed != XINE_SPEED_PAUSE) {
          int wait = (in_buf->vpts - cur_time) * XINE_FINE_SPEED_NORMAL / this->rp.speed;
          wait /= 90;
          xine_gettime (&this->out_fifo.wake_time);
          this->out_fifo.use_wake_time = 1;
          this->out_fifo.wake_time.tv_sec  +=  wait / 1000;
          this->out_fifo.wake_time.tv_nsec += (wait % 1000) * 1000000;
          if (this->out_fifo.wake_time.tv_nsec >= 1000000000) {
            this->out_fifo.wake_time.tv_nsec -= 1000000000;
            this->out_fifo.wake_time.tv_sec  += 1;
          }
        } else {
          this->out_fifo.use_wake_time = 2;
        }
        continue;
      }
      /* end of pause mode */

      /* change driver's settings as needed */
      {
        int changed = in_buf->format.bits != this->input.bits
                   || in_buf->format.rate != this->input.rate
                   || in_buf->format.mode != this->input.mode;
        if (!ao_driver_lock_2 (this))
          continue;
        if (!this->driver.open || changed) {
          lprintf ("audio format has changed\n");
          if (!stream || !stream->emergency_brake)
            ao_change_settings (this, &stream->s, in_buf->format.bits, in_buf->format.rate, in_buf->format.mode);
        }
        if (!this->driver.open) {
          xine_stream_private_t **s;
          pthread_mutex_unlock (&this->driver.mutex);
          xprintf (&this->xine->x, XINE_VERBOSITY_LOG,
            _("audio_out: delay calculation impossible with an unavailable audio device\n"));
          xine_rwlock_rdlock (&this->streams_lock);
          for (s = this->streams; *s; s++) {
            if (!(*s)->emergency_brake) {
              (*s)->emergency_brake = 1;
              _x_message (&(*s)->s, XINE_MSG_AUDIO_OUT_UNAVAILABLE, NULL);
            }
          }
          xine_rwlock_unlock (&this->streams_lock);
          drop = 1;
          break;
        }
      }

      /* buf timing pt 1 */
      if (!this->audio_loop_running)
        break;
      delay = this->driver.d->delay (this->driver.d);
      if (delay < 0) {
        int start_pts = 0;
        /* Get the audio card into RUNNING state. */
        delay = 0;
        while (this->audio_loop_running) {
          delay = this->driver.d->delay (this->driver.d);
          if (delay >= 0)
            break;
          if (ao_fill_gap (this, 10000)) { /* FIXME, this PTS of 1000 should == period size */
            delay = -33333;
            break;
          }
          start_pts += 10000;
        }
        if (!this->audio_loop_running)
          break;
        if (delay == -33333) {
          pthread_mutex_unlock (&this->driver.mutex);
          pthread_mutex_lock (&this->driver.intr_mutex);
          if (this->driver.intr_num)
            pthread_cond_wait (&this->driver.intr_wake, &this->driver.intr_mutex);
          pthread_mutex_unlock (&this->driver.intr_mutex);
          continue;
        }
        cur_time = this->clock->get_current_time (this->clock);
        pthread_mutex_unlock (&this->driver.mutex);
        xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
          "audio_out: got driver running with %d pts of silence.\n", start_pts);
      } else {
        cur_time = this->clock->get_current_time (this->clock);
        pthread_mutex_unlock (&this->driver.mutex);
      }
      if (!this->audio_loop_running)
        break;

      /* current_extra_info not set by video stream or getting too much out of date */
      {
        extra_info_t *found = NULL;
        while (this->rp.ei_read != this->rp.ei_write) {
          extra_info_t *ei = &this->base_ei[this->rp.ei_read];
          if (ei->vpts > cur_time)
            break;
          found = ei;
          this->rp.ei_read = (this->rp.ei_read + 1) & (EI_RING_SIZE - 1);
        }
        if (stream) {
          xine_stream_private_t *m = stream->side_streams[0];
          if (!found) {
            /* O dear. This extra info ring will unblock xine_play () when the first
             * frame after seek is actually heared. That is at least one metronom
             * prebuffer delay (default 14400 pts) later -- too long for fluent seek.
             * Video out tricks around this by showing the first frame earlier.
             * We could double a portion of audio here if we have an agile driver,
             * and if we like to annoy the user sooner or later.
             * Until there is a better way, just limit the delay to 3000 pts. */
            if ((this->rp.ei_read != this->rp.ei_write) && ((cur_time - this->rp.last_flush_vpts) > 3000))
              found = &this->base_ei[this->rp.ei_read];
          }
          if (found) {
            xine_current_extra_info_set (m, found);
            if (found->seek_count == this->rp.seek_count3) {
              xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: seek_count %d step 3.\n", found->seek_count);
              this->rp.seek_count3 = -1;
              pthread_mutex_lock (&m->first_frame.lock);
              m->first_frame.flag = 0;
              pthread_cond_broadcast (&m->first_frame.reached);
              pthread_mutex_unlock (&m->first_frame.lock);
            }
          }
        }
      }

      /* buf timing pt 2: where, in the timeline is the "end" of the hardware audio buffer at the moment? */
      lprintf ("current delay is %d, current time is %" PRId64 "\n", delay, cur_time);
      /* no sound card should delay more than 23.301s ;-) */
      delay = ((uint32_t)delay * this->out_pts_per_kframe) >> 10;
      /* External A52 decoder delay correction (in pts) */
      delay += this->ptoffs;
      /* calculate gap: */
      gap = in_buf->vpts - cur_time - delay;
      this->last_gap = gap;
      lprintf ("now=%" PRId64 ", buffer_vpts=%" PRId64 ", gap=%" PRId64 "\n", cur_time, in_buf->vpts, gap);

      if (this->resample_sync_method) {
        /* Correct sound card drift via resampling. If gap is too big to
         * be corrected this way, we use the fallback: drop/insert frames.
         * This function only calculates the drift correction factor. The
         * actual resampling is done by prepare_samples().
         */
        resample_rate_adjust (this, gap, in_buf);
      } else {
        this->resample_sync_factor = 1.0;
      }

      /* output audio data synced to master clock */
      if (gap < (-1 * AO_MAX_GAP)) {

        /* drop late buf */
        this->last_sgap = 0;
        this->dropped++;
        drop = 1;

      } else if (gap > AO_MAX_GAP) {

        /* for big gaps output silence */
        this->last_sgap = 0;
        pthread_mutex_lock (&this->driver.mutex);
        if (!(this->resend.driver_caps & AO_CAP_NO_UNPAUSE))
          ao_fill_gap (this, gap);
        else
          ao_resend_fill (this, gap, in_buf->vpts);
        pthread_mutex_unlock (&this->driver.mutex);
      }
#if 0
      /* silence out even small stream start gaps (avoid metronom shift).
       * disabled because it also kills nice seek with sound :-/ */
      else if ((this->rp.seek_count_n != in_buf->extra_info->seek_count) && (gap > 0)) {
        this->rp.seek_count_n = in_buf->extra_info->seek_count;
        this->last_sgap = 0;
        pthread_mutex_lock (&this->driver.mutex);
        ao_fill_gap (this, gap);
        pthread_mutex_unlock (&this->driver.mutex);
      }
#endif
      else if ((abs ((int)gap) > this->small_gap) && (cur_time > next_sync_time) &&
               (bufs_since_sync >= SYNC_BUF_INTERVAL) &&
               !this->resample_sync_method) {

        /* for small gaps ( tolerance < abs(gap) < AO_MAX_GAP )
         * feedback them into metronom's vpts_offset (when using
         * metronom feedback for A/V sync)
         */
        xine_stream_private_t **s;
        int sgap = (int)gap >> SYNC_GAP_RATE_LOG2;
        /* avoid asymptote trap of bringing down step with remaining gap */
        if (sgap < 0) {
          sgap = sgap <= this->last_sgap ? sgap
               : this->last_sgap < (int)gap ? (int)gap : this->last_sgap;
        } else {
          sgap = sgap >= this->last_sgap ? sgap
               : this->last_sgap > (int)gap ? (int)gap : this->last_sgap;
        }
        this->last_sgap = sgap != (int)gap ? sgap : 0;
        sgap = -sgap;
        lprintf ("audio_loop: ADJ_VPTS\n");
        xine_rwlock_rdlock (&this->streams_lock);
        for (s = this->streams; *s; s++)
          (*s)->s.metronom->set_option ((*s)->s.metronom, METRONOM_ADJ_VPTS_OFFSET, sgap);
        xine_rwlock_unlock (&this->streams_lock);
        next_sync_time = cur_time + SYNC_TIME_INTERVAL;
        bufs_since_sync = 0;

      } else {

        audio_buffer_t *out_buf;
        int result;

        if (this->dropped) {
          xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
            "audio_out: dropped %d late buffers.\n", this->dropped);
          this->dropped = 0;
        }
#if 0
        {
          int count;
          printf ("Audio data\n");
          for (count = 0; count < 10; count++)
            printf ("%x ", buf->mem[count]);
          printf ("\n");
        }
#endif
        out_buf = prepare_samples (this, in_buf);
#if 0
        {
          int count;
          printf ("Audio data2\n");
          for (count = 0; count < 10; count++)
            printf ("%x ", out_buf->mem[count]);
          printf ("\n");
        }
#endif
        if (this->resend.driver_caps & AO_CAP_NO_UNPAUSE) {
          out_buf->vpts = in_buf->vpts;
          ao_resend_store (this, out_buf);
        }

        lprintf ("loop: writing %d samples to sound device\n", out_buf->num_frames);
        result = 0;
        if (this->driver.open) {
          if (!ao_driver_lock_2 (this))
            continue;
          if (this->driver.open) {
            result = this->driver.d->write (this->driver.d, out_buf->mem, out_buf->num_frames);
          }
          pthread_mutex_unlock (&this->driver.mutex);
        }

        if (result < 0) {
          /* device unplugged. */
          xprintf (&this->xine->x, XINE_VERBOSITY_LOG, _("write to sound card failed. Assuming the device was unplugged.\n"));
          if (stream)
            _x_message (&stream->s, XINE_MSG_AUDIO_OUT_UNAVAILABLE, NULL);
          pthread_mutex_lock (&this->driver.mutex);
          if (this->driver.open)
            this->driver.d->close (this->driver.d);
          this->driver.open = 0;
          _x_free_audio_driver (&this->xine->x, &this->driver.d);
          this->driver.d = _x_load_audio_output_plugin (&this->xine->x, "none");
          if (this->driver.d && (!stream || !stream->emergency_brake)) {
            if (ao_change_settings (this, &stream->s, in_buf->format.bits, in_buf->format.rate, in_buf->format.mode) == 0) {
              if (stream)
                stream->emergency_brake = 1;
            }
            if (stream)
              _x_message (&stream->s, XINE_MSG_AUDIO_OUT_UNAVAILABLE, NULL);
          }
          pthread_mutex_unlock (&this->driver.mutex);
          /* closing the driver will result in XINE_MSG_AUDIO_OUT_UNAVAILABLE to be emitted */
        }
        drop = 1;
      }
    } while (0);

    if (drop) {
      lprintf ("loop: next buf from fifo\n");
      ao_free_fifo_append (this, in_buf);
      in_buf = NULL;
    }

    ao_driver_test_intr (this);
  }

  if (in_buf)
    ao_free_fifo_append (this, in_buf);

  if (this->step) {
    pthread_mutex_lock (&this->step_mutex);
    this->step = 0;
    pthread_cond_broadcast (&this->done_stepping);
    pthread_mutex_unlock (&this->step_mutex);
  }

  return NULL;
}

/*
 * public a/v processing interface
 */

int xine_get_next_audio_frame (xine_audio_port_t *this_gen,
			       xine_audio_frame_t *frame) {

  aos_t          *this = (aos_t *) this_gen;
  audio_buffer_t *in_buf = NULL, *out_buf;
  struct timespec now = {0, 0};

  now.tv_nsec = 990000000;

  pthread_mutex_lock (&this->out_fifo.mutex);

  lprintf ("get_next_audio_frame\n");

  while (!this->out_fifo.first) {
    {
      xine_stream_private_t *stream = this->streams[0];
      if (stream && (stream->s.audio_fifo->fifo_size == 0)
        && (stream->demux.plugin->get_status (stream->demux.plugin) != DEMUX_OK)) {
        /* no further data can be expected here */
        pthread_mutex_unlock (&this->out_fifo.mutex);
        return 0;
      }
    }

    now.tv_nsec += 20000000;
    if (now.tv_nsec >= 1000000000) {
      xine_gettime (&now);
      now.tv_nsec += 20000000;
      if (now.tv_nsec >= 1000000000) {
        now.tv_sec++;
        now.tv_nsec -= 1000000000;
      }
    }
    {
      struct timespec ts = now;
      this->out_fifo.num_waiters++;
      pthread_cond_timedwait (&this->out_fifo.not_empty, &this->out_fifo.mutex, &ts);
      this->out_fifo.num_waiters--;
    }

  }

  in_buf = ao_out_fifo_pop_int (this);
  pthread_mutex_unlock(&this->out_fifo.mutex);

  if  ((in_buf->format.bits != this->input.bits)
    || (in_buf->format.rate != this->input.rate)
    || (in_buf->format.mode != this->input.mode)) {
    xine_stream_private_t *s = (xine_stream_private_t *)in_buf->stream;
    pthread_mutex_lock (&this->driver.mutex);
    lprintf ("audio format has changed\n");
    if (!(s && s->emergency_brake))
      ao_change_settings (this, &s->s, in_buf->format.bits, in_buf->format.rate, in_buf->format.mode);
    pthread_mutex_unlock (&this->driver.mutex);
  }

  out_buf = prepare_samples (this, in_buf);

  if (out_buf != in_buf) {
    ao_free_fifo_append (this, in_buf);
    frame->xine_frame = NULL;
  } else
    frame->xine_frame    = out_buf;

  frame->vpts            = out_buf->vpts;
  frame->num_samples     = out_buf->num_frames;
  frame->sample_rate     = this->input.rate;
  frame->num_channels    = this->in_channels;
  frame->bits_per_sample = this->input.bits;
  frame->pos_stream      = out_buf->extra_info->input_normpos;
  frame->pos_time        = out_buf->extra_info->input_time;
  frame->data            = (uint8_t *) out_buf->mem;

  return 1;
}

void xine_free_audio_frame (xine_audio_port_t *this_gen, xine_audio_frame_t *frame) {

  aos_t          *this = (aos_t *) this_gen;
  audio_buffer_t *buf;

  buf = (audio_buffer_t *) frame->xine_frame;

  if (buf)
    ao_free_fifo_append (this, buf);
}

static uint32_t uint_sqrt (uint32_t v) {
  uint32_t b = 0, e = 0xffff;
  do {
    uint32_t m = (b + e) >> 1;
    if (m * m >= v)
      e = m;
    else
      b = m + 1;
  } while (b < e);
  return b;
}

static int ao_update_resample_factor(aos_t *this) {
  unsigned int eff_input_rate;

  if( !this->driver.open )
    return 0;

  eff_input_rate = this->input.rate;
  switch (this->resample_conf) {
  case 1: /* force off */
    this->do_resample = 0;
    break;
  case 2: /* force on */
    this->do_resample = 1;
    break;
  default: /* AUTO */
    /* Always set up trick play mode here. If turned off by user, it simply has no effect right now,
     * but it can be turned on any time later. */
    if ((this->rp.speed != XINE_FINE_SPEED_NORMAL) && (this->rp.speed != XINE_SPEED_PAUSE))
      eff_input_rate = xine_uint_mul_div (eff_input_rate, this->rp.speed, XINE_FINE_SPEED_NORMAL);
    this->do_resample = eff_input_rate != this->output.rate;
  }

  if (this->do_resample)
    xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
      "audio_out: will resample audio from %u to %d.\n", eff_input_rate, this->output.rate);

  this->small_gap = this->gap_tolerance;
  this->frame_rate_factor = ((double)(this->output.rate)) / ((double)(this->input.rate));
  if (this->rp.speed != XINE_SPEED_PAUSE) {
    this->small_gap = this->gap_tolerance * uint_sqrt (this->rp.speed) / uint_sqrt (XINE_FINE_SPEED_NORMAL);
    this->frame_rate_factor *= (double)XINE_FINE_SPEED_NORMAL / (double)this->rp.speed;
  }

  /* XINE_FINE_SPEED_NORMAL == 1000000; 1024 * 1000000 / 90000 == 1024 * 100 / 9; */
  this->out_frames_per_kpts = this->rp.speed > 0
                            ? xine_uint_mul_div (this->output.rate, 1024 * 100, this->rp.speed * 9)
                            : (this->output.rate * 1024 + 45000) / 90000;
  /* XINE_FINE_SPEED_NORMAL == 1000000; 90000 * 1024 / 1000000 == 9 * 256 / 25; */
  this->out_pts_per_kframe  = xine_uint_mul_div (9 * 256, this->rp.speed, this->output.rate * 25);
  this->out_channels        = _x_ao_mode2channels (this->output.mode);
  this->in_channels         = _x_ao_mode2channels (this->input.mode);

  ao_resend_init (this);
  ao_eq_update (this);

  lprintf ("audio_step %" PRIu32 " pts per 32768 frames\n", this->audio_step);
  return this->output.rate;
}

static int ao_change_settings (aos_t *this, xine_stream_t *stream, uint32_t bits, uint32_t rate, int mode) {
  int output_sample_rate;

  if (this->driver.open && !this->grab_only)
    this->driver.d->close (this->driver.d);
  this->driver.open = 0;

  this->input.mode = mode;
  this->input.rate = rate;
  this->input.bits = bits;

  if (!this->grab_only) {
    int caps = this->driver.d->get_capabilities (this->driver.d);
    /* not all drivers/cards support 8 bits */
    if ((this->input.bits == 8) && !(caps & AO_CAP_8BITS)) {
      bits = 16;
      xprintf (&this->xine->x, XINE_VERBOSITY_LOG,
               _("8 bits not supported by driver, converting to 16 bits.\n"));
    }
    /* provide mono->stereo and stereo->mono conversions */
    if ((this->input.mode == AO_CAP_MODE_MONO) && !(caps & AO_CAP_MODE_MONO)) {
      mode = AO_CAP_MODE_STEREO;
      xprintf (&this->xine->x, XINE_VERBOSITY_LOG,
               _("mono not supported by driver, converting to stereo.\n"));
    }
    if ((this->input.mode == AO_CAP_MODE_STEREO) && !(caps & AO_CAP_MODE_STEREO)) {
      mode = AO_CAP_MODE_MONO;
      xprintf (&this->xine->x, XINE_VERBOSITY_LOG,
               _("stereo not supported by driver, converting to mono.\n"));
    }
    output_sample_rate = this->driver.d->open (this->driver.d, bits, this->force_rate ? this->force_rate : rate, mode);
  } else
    output_sample_rate = this->input.rate;

  if (output_sample_rate == 0) {
    xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: open failed!\n");
    return 0;
  }

  this->driver.open = 1;
  xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: output sample rate %d.\n", output_sample_rate);

  this->last_audio_vpts = 0;
  this->output.mode     = mode;
  this->output.rate     = output_sample_rate;
  this->output.bits     = bits;

  this->ptoffs = (mode == AO_CAP_MODE_A52) || (mode == AO_CAP_MODE_AC5) ? this->passthrough_offset : 0;

  if (this->input.rate) {
    this->audio_step = ((uint32_t)90000 * (uint32_t)32768) / this->input.rate;
    if (stream)
      stream->metronom->set_audio_rate (stream->metronom, this->audio_step);
  }

  return ao_update_resample_factor (this);
}


/*
 * open the audio device for writing to
 */

static int ao_open (xine_audio_port_t *this_gen, xine_stream_t *s,
		   uint32_t bits, uint32_t rate, int mode) {

  aos_t *this = (aos_t *) this_gen;
  int channels;
  xine_stream_private_t *stream = (xine_stream_private_t *)s;

  if (stream)
    stream = stream->side_streams[0];

  xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: ao_open (%p)\n", (void*)stream);

  /* Defer _changing_ settings of an open driver to final output stage, following buf queue status. */
  if (!this->driver.open) {
    int ret;

    if (this->audio_loop_running) {
      /* make sure there are no more buffers on queue */
      ao_out_fifo_loop_flush (this);
    }

    if (stream && !stream->emergency_brake) {
      pthread_mutex_lock( &this->driver.mutex );
      ret = ao_change_settings (this, &stream->s, bits, rate, mode);
      pthread_mutex_unlock( &this->driver.mutex );

      if( !ret ) {
        stream->emergency_brake = 1;
        _x_message (&stream->s, XINE_MSG_AUDIO_OUT_UNAVAILABLE, NULL);
        return 0;
      }
    } else {
      return 0;
    }
  }

  /*
   * set metainfo
   */
  if (stream) {
    channels = _x_ao_mode2channels( mode );
    if( channels == 0 )
      channels = 255; /* unknown */

    /* faster than 4x _x_stream_info_set () */
    xine_rwlock_wrlock (&stream->info_lock);
    stream->stream_info[XINE_STREAM_INFO_AUDIO_MODE]       = mode;
    stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS]   = channels;
    stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS]       = bits;
    stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] = rate;
    xine_rwlock_unlock (&stream->info_lock);

  }

  ao_streams_register (this, stream);
  ao_unref_obsolete (this);

  return this->output.rate;
}

static audio_buffer_t *ao_get_buffer (xine_audio_port_t *this_gen) {

  aos_t *this = (aos_t *) this_gen;
  audio_buffer_t *buf;

  buf = ao_free_fifo_get (this);

  _x_extra_info_reset( buf->extra_info );
  buf->stream = NULL;

  return buf;
}

static void ao_put_buffer (xine_audio_port_t *this_gen,
                           audio_buffer_t *buf, xine_stream_t *stream) {

  aos_t *this = (aos_t *) this_gen;
  int64_t pts;
  int is_first = 0;

  if (this->out_fifo.discard_buffers || (buf->num_frames <= 0)) {
    ao_free_fifo_append (this, buf);
    return;
  }

  this->last_audio_vpts = pts = buf->vpts;

  /* handle anonymous streams like NULL for easy checking */
  if (stream == XINE_ANON_STREAM)
    stream = NULL;
  if (stream) {
    xine_stream_private_t *s = (xine_stream_private_t *)stream;
    s = s->side_streams[0];
    /* faster than 3x _x_stream_info_get () */
    xine_rwlock_rdlock (&s->info_lock);
    buf->format.bits = s->stream_info[XINE_STREAM_INFO_AUDIO_BITS];
    buf->format.rate = s->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE];
    buf->format.mode = s->stream_info[XINE_STREAM_INFO_AUDIO_MODE];
    xine_rwlock_unlock (&s->info_lock);
    _x_extra_info_merge (buf->extra_info, s->audio_decoder_extra_info);
    buf->vpts = s->s.metronom->got_audio_samples (s->s.metronom, pts, buf->num_frames);
    if ((s->first_frame.flag >= 2) && !s->video_decoder_plugin) {
      pthread_mutex_lock (&s->first_frame.lock);
      if (s->first_frame.flag >= 2) {
        xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
          "audio_out: seek_count %d step 1.\n", buf->extra_info->seek_count);
        if (s->first_frame.flag == 3) {
          xine_current_extra_info_set (s, buf->extra_info);
          s->first_frame.flag = 0;
          pthread_cond_broadcast (&s->first_frame.reached);
        } else {
          s->first_frame.flag = 1;
        }
        is_first = 1;
      }
      pthread_mutex_unlock (&s->first_frame.lock);
    }
  }
  buf->extra_info->vpts = buf->vpts;

  lprintf ("ao_put_buffer, pts=%" PRId64 ", vpts=%" PRId64 "\n", pts, buf->vpts);

  buf->stream = stream;
  ao_out_fifo_reref_append (this, buf, is_first);

  lprintf ("ao_put_buffer done\n");
}

static void ao_close(xine_audio_port_t *this_gen, xine_stream_t *stream) {

  aos_t *this = (aos_t *) this_gen;
  int n;

  xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: ao_close (%p)\n", (void*)stream);

  /* unregister stream */
  n = ao_streams_unregister (this, (xine_stream_private_t *)stream);

  /* ao_close () simply means that decoder is finished. The remaining buffered frames
   * will still be played, unless engine flushes them explicitely.
   * trigger possible idle driver close. */
  if (!n && !this->grab_only) {
    pthread_mutex_lock (&this->out_fifo.mutex);
    pthread_cond_signal (&this->out_fifo.not_empty);
    pthread_mutex_unlock (&this->out_fifo.mutex);
  }
}

static void ao_speed_change_cb (void *this_gen, int new_speed) {
  aos_t *this = (aos_t *)this_gen;

  ao_driver_lock (this);
  /* something to do? */
  if ((int)this->driver.speed == new_speed) {
    ao_driver_unlock (this);
    return;
  }
  this->driver.speed = new_speed;
  xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: new speed %d.\n", new_speed);

  if (!this->grab_only) {
    if (new_speed == XINE_SPEED_PAUSE) {
      /* speed_lock is here to make sure the ao_loop will pause in a safe place.
       * that is, we cannot pause writing to device, filling gaps etc. */
      if (this->driver.open) {
        /* Some simple drivers misunderstand AO_CTRL_PLAY_PAUSE as "wait for running dry first".
         * Lets try this flush HACK. See also unpause resend buffer. */
        if (this->driver.d->get_capabilities (this->driver.d) & AO_CAP_NO_UNPAUSE)
          this->driver.d->control (this->driver.d, AO_CTRL_FLUSH_BUFFERS, NULL);
        this->driver.d->control (this->driver.d, AO_CTRL_PLAY_PAUSE, NULL);
      }
    } else {
      if (this->driver.open) {
        /* slow motion / fast forward does not play sound, drop buffered
         * samples from the sound driver (check speed.trick flag) */
        if ((new_speed != XINE_FINE_SPEED_NORMAL) && !this->driver.trick)
          this->driver.d->control (this->driver.d, AO_CTRL_FLUSH_BUFFERS, NULL);
        this->driver.d->control (this->driver.d, AO_CTRL_PLAY_RESUME, NULL);
      }
    }
    ao_driver_unlock (this);
  } else {
    this->rp.speed = this->driver.speed;
    ao_driver_unlock (this);
    ao_update_resample_factor (this);
  }
  ao_out_fifo_signal (this);
}

static void ao_exit(xine_audio_port_t *this_gen) {
  aos_t *this = (aos_t *) this_gen;

  _x_freep (&this->resend.buf);

  this->xine->x.clock->unregister_speed_change_callback (this->xine->x.clock, ao_speed_change_cb, this);
  this->xine->port_ticket->revoke_cb_unregister (this->xine->port_ticket, ao_ticket_revoked, this);

  if (this->audio_loop_running) {
    void *p;

    this->audio_loop_running = 0;
    pthread_mutex_lock (&this->out_fifo.mutex);
    pthread_cond_signal (&this->out_fifo.not_empty);
    pthread_mutex_unlock (&this->out_fifo.mutex);

    pthread_join (this->audio_thread, &p);
  }

  if (!this->grab_only) {
    ao_driver_t *driver;
    int vol = 0, prop, caps;

    pthread_mutex_lock (&this->driver.mutex);
    driver = this->driver.d;

    caps = driver->get_capabilities (driver);
    prop = (caps & AO_CAP_MIXER_VOL) ? AO_PROP_MIXER_VOL
         : (caps & AO_CAP_PCM_VOL) ? AO_PROP_PCM_VOL : 0;
    if (prop)
      vol = driver->get_property (driver, prop);

    if (this->driver.open) {
      driver->close (driver);
      this->driver.open = 0;
    }
    this->driver.d = NULL;
    pthread_mutex_unlock (&this->driver.mutex);

    if (prop)
      this->xine->x.config->update_num (this->xine->x.config, "audio.volume.mixer_volume", vol);

    _x_free_audio_driver (&this->xine->x, &driver);
  }

  if (this->driver.dreqs_wait)
    xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
      "audio_out: waited %d of %d external driver requests.\n", this->driver.dreqs_wait, this->driver.dreqs_all);

  /* We are about to free "this". No callback shall refer to it anymore, even if not our own. */
  this->xine->x.config->unregister_callbacks (this->xine->x.config, NULL, NULL, this, sizeof (*this));

  pthread_mutex_destroy (&this->driver.mutex);
  pthread_cond_destroy (&this->driver.intr_wake);
  pthread_mutex_destroy (&this->driver.intr_mutex);

  ao_streams_close (this);

  pthread_mutex_destroy (&this->step_mutex);
  pthread_cond_destroy  (&this->done_stepping);

  ao_force_unref_all (this, 1);
  ao_free_fifo_close (this);
  ao_out_fifo_close (this);

  _x_freep (&this->frame_buf[0]->mem);
  _x_freep (&this->frame_buf[1]->mem);
  xine_freep_aligned (&this->base_samp);

  free (this);
}

static uint32_t ao_get_capabilities (xine_audio_port_t *this_gen) {
  aos_t *this = (aos_t *) this_gen;
  uint32_t result;

  if (this->grab_only) {

    return AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO ;
    /* FIXME: make configurable
      | AO_CAP_MODE_4CHANNEL | AO_CAP_MODE_5CHANNEL
      | AO_CAP_MODE_5_1CHANNEL | AO_CAP_8BITS;
    */
  } else {
    ao_driver_lock (this);
    result = this->driver.d->get_capabilities (this->driver.d);
    ao_driver_unlock (this);
  }
  return result;
}

static int ao_get_property (xine_audio_port_t *this_gen, int property) {
  aos_t *this = (aos_t *) this_gen;
  int ret;

  switch (property) {
  case XINE_PARAM_VO_SINGLE_STEP:
    ret = 0;
    break;

  case AO_PROP_COMPRESSOR:
    ret = this->compression_factor_max*100;
    break;

  case AO_PROP_BUFS_IN_FIFO:
    ret = this->audio_loop_running ? this->out_fifo.num_buffers : -1;
    break;

  case AO_PROP_BUFS_FREE:
    ret = this->audio_loop_running ? this->free_fifo.num_buffers : -1;
    break;

  case AO_PROP_BUFS_TOTAL:
    ret = this->audio_loop_running ? this->free_fifo.num_buffers_max : -1;
    break;

  case AO_PROP_NUM_STREAMS:
    xine_rwlock_rdlock (&this->streams_lock);
    ret = this->num_anon_streams + this->num_streams;
    xine_rwlock_unlock (&this->streams_lock);
    break;

  case AO_PROP_AMP:
    ret = this->amp_factor*100;
    break;

  case AO_PROP_AMP_MUTE:
    ret = this->amp_mute;
    break;

  case AO_PROP_EQ_30HZ:
  case AO_PROP_EQ_60HZ:
  case AO_PROP_EQ_125HZ:
  case AO_PROP_EQ_250HZ:
  case AO_PROP_EQ_500HZ:
  case AO_PROP_EQ_1000HZ:
  case AO_PROP_EQ_2000HZ:
  case AO_PROP_EQ_4000HZ:
  case AO_PROP_EQ_8000HZ:
  case AO_PROP_EQ_16000HZ:
    ret = this->eq_settings[property - AO_PROP_EQ_30HZ];
    break;

  case AO_PROP_DISCARD_BUFFERS:
    ret = this->out_fifo.discard_buffers;
    break;

  case AO_PROP_CLOCK_SPEED:
    ret = this->rp.speed;
    break;

  case AO_PROP_DRIVER_DELAY:
    ret = this->last_gap;
    break;

  case AO_PROP_PTS_IN_FIFO:
    pthread_mutex_lock (&this->out_fifo.mutex);
    ret = this->out_fifo.pts_fill;
    pthread_mutex_unlock (&this->out_fifo.mutex);
    break;

  default:
    ao_driver_lock (this);
    ret = this->driver.d->get_property(this->driver.d, property);
    ao_driver_unlock (this);
  }
  return ret;
}

static int ao_set_property (xine_audio_port_t *this_gen, int property, int value) {
  aos_t *this = (aos_t *) this_gen;
  int ret = 0;

  switch (property) {
  /* not a typo :-) */
  case XINE_PARAM_VO_SINGLE_STEP:
    ret = !!value;
    if (this->grab_only)
      break;
    pthread_mutex_lock (&this->step_mutex);
    this->step = ret;
    if (ret) {
      struct timespec ts = {0, 0};
      ao_out_fifo_signal (this);
      xine_gettime (&ts);
      ts.tv_nsec += 500000000;
      if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
      }
      if (pthread_cond_timedwait (&this->done_stepping, &this->step_mutex, &ts))
        ret = 0;
    }
    pthread_mutex_unlock (&this->step_mutex);
    break;

  case AO_PROP_COMPRESSOR:

    this->compression_factor_max = (double) value / 100.0;

    this->do_compress = (this->compression_factor_max >1.0);

    ret = this->compression_factor_max*100;
    break;

  case AO_PROP_AMP:

    this->amp_factor = (double) value / 100.0;

    this->do_amp = (this->amp_factor != 1.0 || this->amp_mute);

    ret = this->amp_factor*100;
    break;

  case AO_PROP_AMP_MUTE:
    ret = this->amp_mute = value;

    this->do_amp = (this->amp_factor != 1.0 || this->amp_mute);
    break;

  case AO_PROP_EQ_30HZ:
  case AO_PROP_EQ_60HZ:
  case AO_PROP_EQ_125HZ:
  case AO_PROP_EQ_250HZ:
  case AO_PROP_EQ_500HZ:
  case AO_PROP_EQ_1000HZ:
  case AO_PROP_EQ_2000HZ:
  case AO_PROP_EQ_4000HZ:
  case AO_PROP_EQ_8000HZ:
  case AO_PROP_EQ_16000HZ:
    this->eq_settings[property - AO_PROP_EQ_30HZ] = value;
    ao_eq_update (this);
    ret = value;
    break;

  case AO_PROP_DISCARD_BUFFERS:
    /* recursive discard buffers setting */
    if (value) {
      pthread_mutex_lock (&this->out_fifo.mutex);
      this->out_fifo.discard_buffers++;
      ret = this->out_fifo.discard_buffers;
      pthread_cond_signal (&this->out_fifo.not_empty);
      if (this->grab_only) {
        /* discard buffers here because we have no output thread. */
        ao_out_fifo_manual_flush (this);
      }
      pthread_mutex_unlock (&this->out_fifo.mutex);
    } else {
      pthread_mutex_lock (&this->out_fifo.mutex);
      if (this->out_fifo.discard_buffers) {
        if (this->out_fifo.discard_buffers == 1) {
          if (!this->grab_only) {
            if (this->audio_loop_running && this->out_fifo.first) {
              /* Usually, output thread already did that in the meantime.
               * If not, do it here and avoid extra context switches. */
              ao_out_fifo_manual_flush (this);
            }
            this->out_fifo.discard_buffers = ret = 0;
            pthread_mutex_unlock (&this->out_fifo.mutex);
            /* flush driver at last lift, so user can hear this seek segment longer. */
            ao_flush_driver (this);
            break;
          }
        }
        this->out_fifo.discard_buffers--;
        ret = this->out_fifo.discard_buffers;
        pthread_mutex_unlock (&this->out_fifo.mutex);
      } else {
        pthread_mutex_unlock (&this->out_fifo.mutex);
        ret = 0;
        xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
          "audio_out: ao_set_property: discard_buffers is already zero\n");
      }
    }
    break;

  case AO_PROP_CLOSE_DEVICE:
    ao_driver_lock (this);
    if (this->driver.open)
      this->driver.d->close (this->driver.d);
    this->driver.open = 0;
    ao_driver_unlock (this);
    break;

  case AO_PROP_CLOCK_SPEED:
    /* We no longer need this and that speed paranoia hack, and latch on that clock directly instead. */
    ret = value;
    break;

  case AO_PROP_PTS_IN_FIFO:
    pthread_mutex_lock (&this->out_fifo.mutex);
    ret = this->out_fifo.pts_fill;
    pthread_mutex_unlock (&this->out_fifo.mutex);
    break;

  default:
    if (!this->grab_only) {
      /* Let the sound driver lock it's own mixer */
      ret =  this->driver.d->set_property(this->driver.d, property, value);
    }
  }

  return ret;
}

static int ao_control (xine_audio_port_t *this_gen, int cmd, ...) {

  aos_t *this = (aos_t *) this_gen;
  va_list args;
  void *arg;
  int rval = 0;

  if (this->grab_only)
    return 0;

  ao_driver_lock (this);
  if(this->driver.open) {
    va_start(args, cmd);
    arg = va_arg(args, void*);
    rval = this->driver.d->control(this->driver.d, cmd, arg);
    va_end(args);
  }
  ao_driver_unlock (this);

  return rval;
}

static void ao_flush (xine_audio_port_t *this_gen) {
  aos_t *this = (aos_t *) this_gen;

  xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG,
           "audio_out: ao_flush (loop running: %d)\n", this->audio_loop_running);

  if (this->audio_loop_running)
    ao_out_fifo_loop_flush (this);
}

static int ao_status (xine_audio_port_t *this_gen, xine_stream_t *stream,
	       uint32_t *bits, uint32_t *rate, int *mode) {
  aos_t *this = (aos_t *) this_gen;
  xine_stream_private_t **s;
  int ret = 0;

  if (!stream || (stream == XINE_ANON_STREAM)) {
    *bits = this->input.bits;
    *rate = this->input.rate;
    *mode = this->input.mode;
    return 0;
  }

  xine_rwlock_rdlock (&this->streams_lock);
  for (s = this->streams; *s; s++) {
    if (&(*s)->s == stream) {
      *bits = this->input.bits;
      *rate = this->input.rate;
      *mode = this->input.mode;
      ret = 1;
      break;
    }
  }
  xine_rwlock_unlock (&this->streams_lock);

  return ret;
}

static void ao_update_av_sync_method(void *this_gen, xine_cfg_entry_t *entry) {
  aos_t *this = (aos_t *) this_gen;

  lprintf ("av_sync_method = %d\n", entry->num_value);

  this->av_sync_method_conf = entry->num_value;

  switch (this->av_sync_method_conf) {
  case 0:
    this->resample_sync_method = 0;
    break;
  case 1:
    this->resample_sync_method = 1;
    break;
  default:
    this->resample_sync_method = 0;
    break;
  }
  this->resample_sync_info.valid = 0;
}

static void ao_update_ptoffs (void *this_gen, xine_cfg_entry_t *entry) {
  aos_t *this = (aos_t *)this_gen;
  this->passthrough_offset = entry->num_value;
  this->ptoffs = (this->output.mode == AO_CAP_MODE_A52) || (this->output.mode == AO_CAP_MODE_AC5) ? this->passthrough_offset : 0;
}

static void ao_update_slow_fast (void *this_gen, xine_cfg_entry_t *entry) {
  aos_t *this = (aos_t *)this_gen;
  this->driver.trick = entry->num_value;
  ao_out_fifo_signal (this);
}

xine_audio_port_t *_x_ao_new_port (xine_t *xine, ao_driver_t *driver,
				int grab_only) {

  config_values_t *config = xine->config;
  aos_t           *this;
  uint8_t         *vsbuf0, *vsbuf1;
  static const char *const resample_modes[] = {"auto", "off", "on", NULL};
  static const char *const av_sync_methods[] = {"metronom feedback", "resample", NULL};

  this = calloc(1, sizeof(aos_t)) ;
  if (!this)
    return NULL;
#ifndef HAVE_ZERO_SAFE_MEM
  /* Do these first, when compiler still knows "this" is all zeroed.
   * Let it optimize away this on most systems where clear mem
   * interpretes as 0, 0f or NULL safely.
   */
  this->driver.intr_num        = 0;
  this->driver.dreqs_all       = 0;
  this->driver.dreqs_wait      = 0;
  this->audio_loop_running     = 0;
  this->step                   = 0;
  this->last_gap               = 0;
  this->last_sgap              = 0;
  this->compression_factor_max = 0.0;
  this->do_compress            = 0;
  this->do_amp                 = 0;
  this->amp_mute               = 0;
  this->do_equ                 = 0;
  this->eq_settings[0]         = 0;
  this->eq_settings[1]         = 0;
  this->eq_settings[2]         = 0;
  this->eq_settings[3]         = 0;
  this->eq_settings[4]         = 0;
  this->eq_settings[5]         = 0;
  this->eq_settings[6]         = 0;
  this->eq_settings[7]         = 0;
  this->eq_settings[8]         = 0;
  this->eq_settings[9]         = 0;
  this->rp.last_flush_vpts     = 0;
  this->resend.vpts            = 0;
  this->resend.driver_caps     = 0;
  this->resend.speed           = 0;
  this->resend.rate            = 0;
  this->resend.mode            = 0;
  this->resend.bits            = 0;
  this->resend.write           = 0;
  this->resend.wrap            = 0;
  this->resend.max             = 0;
  this->resend.frame_size      = 0;
  this->resend.buf             = NULL;
#endif

  this->rp.seek_count2  = -1;
  this->rp.seek_count3  = -1;
  this->rp.seek_count_n = -1;

  this->driver.d     = driver;
  this->xine         = (xine_private_t *)xine;
  this->clock        = xine->clock;
  this->driver.speed =
  this->rp.speed     = xine->clock->speed;

  this->base_samp = xine_mallocz_aligned ((NUM_AUDIO_BUFFERS + 1) * AUDIO_BUF_SIZE);
  vsbuf0          = calloc (1, 4 * AUDIO_BUF_SIZE);
  vsbuf1          = calloc (1, 4 * AUDIO_BUF_SIZE);
  if (!this->base_samp || !vsbuf0 || !vsbuf1) {
    xine_free_aligned (this->base_samp);
    free (vsbuf0);
    free (vsbuf1);
    free (this);
    return NULL;
  }

  ao_streams_open (this);
  
  pthread_mutex_init        (&this->driver.mutex, NULL);

  pthread_mutex_init        (&this->driver.intr_mutex, NULL);
  pthread_cond_init         (&this->driver.intr_wake, NULL);

  this->ao.open             = ao_open;
  this->ao.get_buffer       = ao_get_buffer;
  this->ao.put_buffer       = ao_put_buffer;
  this->ao.close            = ao_close;
  this->ao.exit             = ao_exit;
  this->ao.get_capabilities = ao_get_capabilities;
  this->ao.get_property     = ao_get_property;
  this->ao.set_property     = ao_set_property;
  this->ao.control          = ao_control;
  this->ao.flush            = ao_flush;
  this->ao.status           = ao_status;

  this->grab_only           = grab_only;

  pthread_mutex_init (&this->step_mutex, NULL);
  pthread_cond_init  (&this->done_stepping, NULL);

  if (!grab_only)
    this->gap_tolerance = driver->get_gap_tolerance (driver);

  this->av_sync_method_conf = config->register_enum (
    config, "audio.synchronization.av_sync_method", 0, (char **)av_sync_methods,
    _("method to sync audio and video"),
    _("When playing audio and video, there are at least two clocks involved: "
      "The system clock, to which video frames are synchronized and the clock "
      "in your sound hardware, which determines the speed of the audio playback. "
      "These clocks are never ticking at the same speed except for some rare "
      "cases where they are physically identical. In general, the two clocks "
      "will run drift after some time, for which xine offers two ways to keep "
      "audio and video synchronized:\n\n"
      "metronom feedback\n"
      "This is the standard method, which applies a countereffecting video drift, "
      "as soon as the audio drift has accumulated over a threshold.\n\n"
      "resample\n"
      "For some video hardware, which is limited to a fixed frame rate (like the "
      "DXR3 or other decoder cards) the above does not work, because the video "
      "cannot drift. Therefore we resample the audio stream to make it longer "
      "or shorter to compensate the audio drift error. This does not work for "
      "digital passthrough, where audio data is passed to an external decoder in "
      "digital form."),
    20, ao_update_av_sync_method, this);
  this->resample_sync_method = this->av_sync_method_conf == 1 ? 1 : 0;
  this->resample_sync_info.valid = 0;

  this->resample_conf = config->register_enum (
    config, "audio.synchronization.resample_mode", 0, (char **)resample_modes,
    _("enable resampling"),
    _("When the sample rate of the decoded audio does not match the capabilities "
      "of your sound hardware, an adaptation called \"resampling\" is required. "
      "Here you can select, whether resampling is enabled, disabled or used "
      "automatically when necessary."),
    20, NULL, NULL);

  this->force_rate = config->register_num (
    config, "audio.synchronization.force_rate", 0,
    _("always resample to this rate (0 to disable)"),
    _("Some audio drivers do not correctly announce the capabilities of the audio "
      "hardware. By setting a value other than zero here, you can force the audio "
      "stream to be resampled to the given rate."),
    20, NULL, NULL);

  this->passthrough_offset = config->register_num (
    config, "audio.synchronization.passthrough_offset", 0,
    _("offset for digital passthrough"),
    _("If you use an external surround decoder and audio is ahead or behind video, "
      "you can enter a fixed offset here to compensate.\n"
      "The unit of the value is one PTS tick, which is the 90000th part of a second."),
    10, ao_update_ptoffs, this);

  this->driver.trick = this->rp.trick = config->register_bool (
    config, "audio.synchronization.slow_fast_audio", 0,
    _("play audio even on slow/fast speeds"),
    _("If you enable this option, the audio will be heard even when playback speed is "
      "different than 1X. Of course, it will sound distorted (lower/higher pitch). "
      "If want to experiment preserving the pitch you may try the 'stretch' audio post plugin instead."),
    10, ao_update_slow_fast, this);

  this->compression_factor = 2.0;
  this->amp_factor         = 1.0;

  /*
   * pre-allocate memory for samples
   */

  ao_free_fifo_open (this);
  ao_out_fifo_open (this);

  {
    audio_buffer_t *buf = this->base_buf, *list = NULL, **add = &list;
    extra_info_t    *ei = this->base_ei + EI_RING_SIZE;
    uint8_t        *mem = this->base_samp;
    int             i;

    for (i = 0; i < NUM_AUDIO_BUFFERS; i++) {
      buf->mem        = (int16_t *)mem;
      buf->mem_size   = AUDIO_BUF_SIZE;
      buf->extra_info = ei;
      *add            = buf;
      add             = &buf->next;
      buf++;
      ei++;
      mem += AUDIO_BUF_SIZE;
    }
    *add = NULL;
    this->free_fifo.first = list;
    this->free_fifo.add   = add;
    this->free_fifo.num_buffers     =
    this->free_fifo.num_buffers_max = i;

    this->zero_space = (int16_t *)mem;

    /* buffers used for audio conversions. need to be resizable */
    buf->mem        = (int16_t *)vsbuf0;
    buf->mem_size   = 4 *AUDIO_BUF_SIZE;
    buf->extra_info = ei;
    this->frame_buf[0] = buf;
    buf++;
    ei++;
    buf->mem        = (int16_t *)vsbuf1;
    buf->mem_size   = 4 *AUDIO_BUF_SIZE;
    buf->extra_info = ei;
    this->frame_buf[1] = buf;
  }

  this->out_fifo.seek_count1 = -1;

  this->xine->port_ticket->revoke_cb_register (this->xine->port_ticket, ao_ticket_revoked, this);

  /*
   * Set audio volume to latest used one ?
   */
  if (this->driver.d) {
    int vol;

    vol = config->register_range (config, "audio.volume.mixer_volume", 50, 0, 100,
      _("startup audio volume"),
      _("The overall audio volume set at xine startup."),
      10, NULL, NULL);

    if (config->register_bool (config, "audio.volume.remember_volume", 0,
      _("restore volume level at startup"),
      _("If disabled, xine will not modify any mixer settings at startup."),
      10, NULL, NULL)) {
      int caps = this->driver.d->get_capabilities (this->driver.d);
      if (caps & AO_CAP_MIXER_VOL)
        this->driver.d->set_property (this->driver.d, AO_PROP_MIXER_VOL, vol);
      else if (caps & AO_CAP_PCM_VOL)
        this->driver.d->set_property (this->driver.d, AO_PROP_PCM_VOL, vol);
    }
  }

  if (!this->grab_only) {
    pthread_attr_t pth_attrs;
    int err;
    /*
     * start output thread
     */

    this->audio_loop_running = 1;

    pthread_attr_init(&pth_attrs);
#if defined(_POSIX_THREAD_PRIORITY_SCHEDULING) && (_POSIX_THREAD_PRIORITY_SCHEDULING > 0)
    pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
#endif

    err = pthread_create (&this->audio_thread, &pth_attrs, ao_loop, this);
    pthread_attr_destroy(&pth_attrs);

    if (err != 0) {
      xprintf (&this->xine->x, XINE_VERBOSITY_NONE,
	       "audio_out: can't create thread (%s)\n", strerror(err));
      xprintf (&this->xine->x, XINE_VERBOSITY_LOG,
	       _("audio_out: sorry, this should not happen. please restart xine.\n"));

      this->audio_loop_running = 0;
      /* no need to inline ao_exit () here. */
      this->ao.exit (&this->ao);
      return NULL;
    }

    xprintf (&this->xine->x, XINE_VERBOSITY_DEBUG, "audio_out: thread created\n");
  }

  this->xine->x.clock->register_speed_change_callback (this->xine->x.clock, ao_speed_change_cb, this);
  return &this->ao;
}
