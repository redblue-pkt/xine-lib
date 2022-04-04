/*
 * Copyright (C) 2000-2022 the xine project
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

/**
 * @file
 * @brief Declaration of internal, private functions for xine-lib.
 *
 * @internal These functions should not be used by neither plugins nor
 * frontends.
 */

#ifndef XINE_PRIVATE_H__
#define XINE_PRIVATE_H__

#ifndef XINE_LIBRARY_COMPILE
# error xine_private.h is for libxine private use only!
#endif
#if defined(HAVE_CONFIG_H) && !defined(__XINE_LIB_CONFIG_H__)
#  error config.h not included
#endif

#include <xine/xine_internal.h>

#if SUPPORT_ATTRIBUTE_VISIBILITY_INTERNAL
# define INTERNAL __attribute__((visibility("internal")))
#elif SUPPORT_ATTRIBUTE_VISIBILITY_DEFAULT
# define INTERNAL __attribute__((__visibility__("default")))
#else
# define INTERNAL
#endif

#if defined (__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6 ))
#    define XINE_DISABLE_DEPRECATION_WARNINGS _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#    define XINE_ENABLE_DEPRECATION_WARNINGS  _Pragma("GCC diagnostic warning \"-Wdeprecated-declarations\"")
#else
#    define XINE_DISABLE_DEPRECATION_WARNINGS
#    define XINE_ENABLE_DEPRECATION_WARNINGS
#endif

#ifdef __cplusplus
# define EXTERN_C_START extern "C" {
# define EXTERN_C_STOP  }
#else
# define EXTERN_C_START
# define EXTERN_C_STOP
#endif

EXTERN_C_START

/* HAVE_ATOMIC_VARS: 0 = none, 1 = stdatomic.h, 2 = __atomic_*, 3 = __sync_* */
#if (HAVE_ATOMIC_VARS > 0)
#  if (HAVE_ATOMIC_VARS == 1)
#    include <stdatomic.h>
#    define XINE_ATINT_T atomic_int
#    define XINE_ATINIT(xatfa_refs,xatfa_n) atomic_init (&(xatfa_refs), (xatfa_n))
#    define XINE_ATFA(xatfa_refs,xatfa_n) atomic_fetch_add_explicit (&(xatfa_refs), (xatfa_n), memory_order_acq_rel)
#    define XINE_ATGET(xatfa_refs) atomic_load_explicit (&(xatfa_refs), memory_order_acquire)
#  elif (HAVE_ATOMIC_VARS == 2)
#    define XINE_ATINT_T int
#    define XINE_ATINIT(xatfa_refs,xatfa_n) __atomic_store_n (&(xatfa_refs), (xatfa_n), __ATOMIC_RELAXED)
#    define XINE_ATFA(xatfa_refs,xatfa_n) __atomic_fetch_add (&(xatfa_refs), (xatfa_n), __ATOMIC_ACQ_REL)
#    define XINE_ATGET(xatfa_refs) __atomic_load_n (&(xatfa_refs), __ATOMIC_ACQUIRE)
#  else /* HAVE_ATOMIC_VARS == 3 */
#    define XINE_ATINT_T volatile int
#    define XINE_ATINIT(xatfa_refs,xatfa_n) xatfa_refs = xatfa_n
#    define XINE_ATFA(xatfa_refs,xatfa_n) __sync_fetch_and_add (&(xatfa_refs), (xatfa_n))
#    if defined (ARCH_X86)
#      define XINE_ATGET(xatfa_refs) (xatfa_refs)
#    else
#      define XINE_ATGET(xatfa_refs) __sync_fetch_and_add (&(xatfa_refs), 0)
#    endif
#  endif

typedef struct {
  XINE_ATINT_T refs;
  void (*destructor) (void *object);
  void *object;
} xine_refs_t;

static inline void xine_refs_init (xine_refs_t *refs,
  void (*destructor) (void *object), void *object) {
  refs->destructor = destructor;
  refs->object = object;
  XINE_ATINIT (refs->refs, 1);
}

static inline int xine_refs_add (xine_refs_t *refs, int n) {
  return XINE_ATFA (refs->refs, n) + n;
}

static inline int xine_refs_sub (xine_refs_t *refs, int n) {
  int v = XINE_ATFA (refs->refs, -n) - n;
  if (v == 0)
    refs->destructor (refs->object);
  return v;
}

static inline int xine_refs_get (xine_refs_t *refs) {
  return XINE_ATGET (refs->refs);
}

#else

typedef struct {
  pthread_mutex_t mutex;
  int refs;
  void (*destructor) (void *object);
  void *object;
} xine_refs_t;

static inline void xine_refs_init (xine_refs_t *refs,
  void (*destructor) (void *object), void *object) {
  refs->destructor = destructor;
  refs->object = object;
  refs->refs = 1;
  pthread_mutex_init (&refs->mutex, NULL);
}

static inline int xine_refs_add (xine_refs_t *refs, int n) {
  int v;
  pthread_mutex_lock (&refs->mutex);
  refs->refs += n;
  v = refs->refs;
  pthread_mutex_unlock (&refs->mutex);
  return v;
}

static inline int xine_refs_sub (xine_refs_t *refs, int n) {
  int v;
  pthread_mutex_lock (&refs->mutex);
  refs->refs -= n;
  v = refs->refs;
  pthread_mutex_unlock (&refs->mutex);
  if (v == 0) {
    pthread_mutex_destroy (&refs->mutex);
    refs->destructor (refs->object);
  }
  return v;
}

static inline int xine_refs_get (xine_refs_t *refs) {
  int v;
  pthread_mutex_lock (&refs->mutex);
  v = refs->refs;
  pthread_mutex_unlock (&refs->mutex);
  return v;
}

#endif

/**
 * @defgroup load_plugins Plugins loading
 * @brief Functions related with plugins loading.
 */

/**
 * @ingroup load_plugins
 * @brief Load plugins into catalog
 * @param this xine instance
 *
 * All input and demux plugins will be fully loaded and initialized.
 * Decoder plugins are loaded on demand. Video/audio output plugins
 * have special load/probe functions
 */
int _x_scan_plugins (xine_t *this) INTERNAL;

/**
 * @ingroup load_plugins
 * @brief Dispose (shutdown) all currently loaded plugins
 * @param this xine instance
 */
void _x_dispose_plugins (xine_t *this) INTERNAL;

void _x_free_video_driver (xine_t *xine, vo_driver_t **driver) INTERNAL;
void _x_free_audio_driver (xine_t *xine, ao_driver_t **driver) INTERNAL;

///@{
/**
 * @defgroup
 * @brief find and instantiate input and demux plugins
 */
demux_plugin_t *_x_find_demux_plugin_last_probe(xine_stream_t *stream, const char *last_demux_name, input_plugin_t *input) INTERNAL;
input_plugin_t *_x_rip_plugin_get_instance (xine_stream_t *stream, const char *filename) INTERNAL;
input_plugin_t *_x_cache_plugin_get_instance (xine_stream_t *stream) INTERNAL;
///@}

///@{
/**
 * @defgroup
 * @brief  create decoder fifos and threads
*/

int _x_video_decoder_init           (xine_stream_t *stream) INTERNAL;
void _x_video_decoder_shutdown      (xine_stream_t *stream) INTERNAL;

int _x_audio_decoder_init           (xine_stream_t *stream) INTERNAL;
void _x_audio_decoder_shutdown      (xine_stream_t *stream) INTERNAL;
///@}

/**
 * @brief Benchmark available memcpy methods
 */
void xine_probe_fast_memcpy(xine_t *xine) INTERNAL;

/**
 * @brief Make file descriptors and sockets uninheritable
 */
int _x_set_file_close_on_exec(int fd) INTERNAL;

int _x_set_socket_close_on_exec(int s) INTERNAL;


#if defined(HAVE_PTHREAD_RWLOCK)
#  define xine_rwlock_t                pthread_rwlock_t
#  define xine_rwlock_init_default(l)  pthread_rwlock_init (l, NULL)
#  define xine_rwlock_rdlock(l)        pthread_rwlock_rdlock (l)
#  define xine_rwlock_tryrdlock(l)     pthread_rwlock_tryrdlock (l)
#  define xine_rwlock_timedrdlock(l,t) pthread_rwlock_timedrdlock (l, t)
#  define xine_rwlock_wrlock(l)        pthread_rwlock_wrlock (l)
#  define xine_rwlock_trywrlock(l)     pthread_rwlock_trywrlock (l)
#  define xine_rwlock_timedwrlock(l,t) pthread_rwlock_timedwrlock (l, t)
#  define xine_rwlock_unlock(l)        pthread_rwlock_unlock (l)
#  define xine_rwlock_destroy(l)       pthread_rwlock_destroy (l)
#else
#  define xine_rwlock_t                pthread_mutex_t
#  define xine_rwlock_init_default(l)  pthread_mutex_init (l, NULL)
#  define xine_rwlock_rdlock(l)        pthread_mutex_lock (l)
#  define xine_rwlock_tryrdlock(l)     pthread_mutex_trylock (l)
#  define xine_rwlock_timedrdlock(l,t) pthread_mutex_timedlock (l, t)
#  define xine_rwlock_wrlock(l)        pthread_mutex_lock (l)
#  define xine_rwlock_trywrlock(l)     pthread_mutex_trylock (l)
#  define xine_rwlock_timedwrlock(l,t) pthread_mutex_timedlock (l, t)
#  define xine_rwlock_unlock(l)        pthread_mutex_unlock (l)
#  define xine_rwlock_destroy(l)       pthread_mutex_destroy (l)
#endif

#ifdef HAVE_POSIX_TIMERS
#  define xine_gettime(t) clock_gettime (CLOCK_REALTIME, t)
#else
static inline int xine_gettime (struct timespec *ts) {
  struct timeval tv;
  int r;
  r = gettimeofday (&tv, NULL);
  if (!r) {
    ts->tv_sec  = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
  }
  return r;
}
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(ARCH_X86)
static inline uint32_t xine_uint_mul_div (uint32_t num, uint32_t mul, uint32_t den) {
  register uint32_t eax = num, edx;
  /* if result > 0xffffffff, return 0xffffffff without math exception. */
  __asm__ __volatile__ (
      "mull\t%2\n"
    "\tmovl\t%3, %2\n"
    "\tshrl\t%2\n"
    "\taddl\t%2, %0\n"
    "\tadcl\t$0, %1\n"
    "\tcmpl\t%1, %3\n"
    "\tjbe\t1f\n"
    "\tdivl\t%3\n"
    "\tjmp\t2f\n"
    "1:\n"
    "\txorl\t%0, %0\n"
    "\tnotl\t%0\n"
    "2:\n"
    : "=a" (eax), "=d" (edx), "=r" (mul), "=g" (den)
    : "0"  (eax),             "2"  (mul), "3"  (den)
    : "cc"
  );
  (void)mul;
  (void)den;
  (void)edx;
  return eax;
}
#else
static inline uint32_t xine_uint_mul_div (uint32_t num, uint32_t mul, uint32_t den) {
  return ((uint64_t)num * mul + (den >> 1)) / den;
}
#endif

static inline int32_t xine_str2int32 (const char **s) {
  const uint8_t *p = (const uint8_t *)*s;
  uint8_t z;
  int32_t v;
  do {
    z = *p;
    if (!z) {
      *s = (const char *)p;
      return 0;
    }
    p++;
    z ^= '0';
  } while ((z > 9) && (z != ('-' ^ '0')));
  if (z == ('-' ^ '0')) {
    v = 0;
    while (1) {
      z = *p++ ^ '0';
      if (z > 9)
        break;
      v = 10 * v - z;
    }
  } else {
    v = 0;
    do {
      v = 10 * v + z;
      z = *p++ ^ '0';
    } while (z <= 9);
  }
  *s = (const char *)(p - 1);
  return v;
}

static inline uint32_t xine_str2uint32 (const char **s) {
  const uint8_t *p = (const uint8_t *)*s;
  uint8_t z;
  uint32_t v;
  do {
    z = *p;
    if (!z) {
      *s = (const char *)p;
      return 0;
    }
    p++;
    z ^= '0';
  } while (z > 9);
  v = 0;
  do {
    v = 10u * v + z;
    z = *p++ ^ '0';
  } while (z <= 9);
  *s = (const char *)(p - 1);
  return v;
}

static inline uint64_t xine_str2uint64 (const char **s) {
  const uint8_t *p = (const uint8_t *)*s;
  uint8_t z;
  uint64_t v;
#if defined(__WORDSIZE) && (__WORDSIZE == 32)
  uint32_t u;
#endif
  do {
    z = *p;
    if (!z) {
      *s = (const char *)p;
      return 0;
    }
    p++;
    z ^= '0';
  } while (z > 9);
#if defined(__WORDSIZE) && (__WORDSIZE == 32)
  u = 0;
  do {
    u = 10u * u + z;
    z = *p++ ^ '0';
    if (z > 9) {
      *s = (const char *)(p - 1);
      return u;
    }
  } while (!(u & 0xf0000000));
  v = u;
#else
  v = 0;
#endif
  do {
    v = (v << 3) + (v << 1) + z;
    z = *p++ ^ '0';
  } while (z <= 9);
  *s = (const char *)(p - 1);
  return v;
}

#define XINE_MAX_INT32_STR 13
static inline void xine_int32_2str (char **s, int32_t v) {
  uint8_t b[24], *t = b + 11, *q = (uint8_t *)*s;
  uint32_t u;
  if (v < 0) {
    *q++ = '-';
    u = -v;
  } else {
    u = v;
  }
  *t = 0;
  do {
    *--t = u % 10u + '0';
    u /= 10u;
  } while (u);
  memcpy (q, t, 12);
  *s = (char *)(q + (b + 11 - t));
}

static inline void xine_uint32_2str (char **s, uint32_t v) {
  uint8_t b[24], *t = b + 11, *q = (uint8_t *)*s;
  *t = 0;
  do {
    *--t = v % 10u + '0';
    v /= 10u;
  } while (v);
  memcpy (q, t, 12);
  *s = (char *)(q + (b + 11 - t));
}

#define XINE_MAX_INT64_STR 21
static inline void xine_uint64_2str (char **s, uint64_t v) {
  uint8_t b[44], *t = b + 21, *q = (uint8_t *)*s;
  *t = 0;
  do {
    *--t = v % 10u + '0';
    v /= 10u;
  } while (v);
  memcpy (q, t, 21);
  *s = (char *)(q + (b + 21 - t));
}

/* A little helper for integers whose size is not obvious, like off_t and time_t. */
#define xine_uint2str(s,v) do { \
  if (sizeof (v) == 8) \
    xine_uint64_2str (s, v); \
  else \
    xine_uint32_2str (s, v); \
} while (0)

#if 1 /* XXX: Is this safe everywhere? */
#  define PTR_IN_RANGE(_ptr,_start,_size) \
    ((uintptr_t)((uint8_t *)(_ptr) - (uint8_t *)(_start)) < (uintptr_t)(_size))
#else
#  define PTR_IN_RANGE(_ptr,_start,_size) \
    ((uint8_t *)(_ptr) >= (uint8_t *)(_start) && ((uint8_t *)(_ptr) < (uint8_t *)(_start) + (_size)))
#endif

typedef struct {
  xine_t                     x;

  xine_ticket_t             *port_ticket;
  pthread_mutex_t            log_lock;

  xine_log_cb_t              log_cb;
  void                      *log_cb_user_data;

  int                        flags;
  int                        network_timeout;
  enum {
    XINE_IP_PREF_AUTO = 0,
    XINE_IP_PREF_4,
    XINE_IP_PREF_4_6,
    XINE_IP_PREF_6_4
  }                          ip_pref;

  uint32_t                   join_av:1;

  /* lock controlling speed change access.
   * if we should ever introduce per stream clock and ticket,
   * move this to xine_stream_private_t below. */
#define SPEED_FLAG_IGNORE_CHANGE  1
#define SPEED_FLAG_CHANGING       2
#define SPEED_FLAG_WANT_LIVE      4
#define SPEED_FLAG_WANT_NEW       8
  uint32_t                   speed_change_flags;
  int                        speed_change_new_live;
  int                        speed_change_new_speed;
  pthread_mutex_t            speed_change_lock;
  pthread_cond_t             speed_change_done;
  /* set when pauseing with port ticket granted, for XINE_PARAM_VO_SINGLE_STEP. */
  /* special values for set_speed_internal (). now defined in xine/xine_internal.h. */
  /* # define XINE_LIVE_PAUSE_ON 0x7ffffffd */
  /* # define XINE_LIVE_PAUSE_OFF 0x7ffffffc */
} xine_private_t;
  
typedef struct xine_stream_private_st {
  xine_stream_t              s;

  int                        status;

  uint32_t                   video_thread_created:1;
  uint32_t                   audio_thread_created:1;
  uint32_t                   slave_is_subtitle:1;    /*< ... and will be automaticaly disposed */
  uint32_t                   emergency_brake:1;      /*< something went really wrong and this stream must be
                                                      *  stopped. usually due some fatal error on output
                                                      *  layers as they cannot call xine_stop. */
  uint32_t                   early_finish_event:1;   /*< do not wait fifos get empty before sending event */
  uint32_t                   gapless_switch:1;       /*< next stream switch will be gapless */
  uint32_t                   keep_ao_driver_open:1;
  uint32_t                   finished_naturally:1;

  input_class_t             *eject_class;

/*  vo_driver_t               *video_driver;*/
  pthread_t                  video_thread;
  video_decoder_t           *video_decoder_plugin;
  extra_info_t              *video_decoder_extra_info;
  int                        video_decoder_streamtype;
  int                        video_channel;

  int                        audio_track_map_entries;

  int                        audio_decoder_streamtype;
  pthread_t                  audio_thread;
  audio_decoder_t           *audio_decoder_plugin;
  extra_info_t              *audio_decoder_extra_info;

  uint32_t                   audio_type;
  /* *_user: -2 => off
             -1 => auto (use *_auto value)
	    >=0 => respect the user's choice
  */
  int                        audio_channel_user;
/*  int                        audio_channel_auto; */

/*  spu_decoder_t             *spu_decoder_plugin; */
/*  int                        spu_decoder_streamtype; */
  int                        spu_track_map_entries;
/*  int                        spu_channel_user; */
/*  int                        spu_channel_auto; */
/*  int                        spu_channel_letterbox; */
  int                        spu_channel_pan_scan;
/*  int                        spu_channel; */

  /* lock for public xine player functions */
  pthread_mutex_t            frontend_lock;

#define XINE_NUM_SIDE_STREAMS 4
  /* HACK: protected by info_lock below.
   * side_streams[0] always points to the master, which is the stream itself if not a side stream.
   * It is set by init, and does not change until dispose.
   * In other words: it may safely be read without lock. */
  struct xine_stream_private_st *side_streams[XINE_NUM_SIDE_STREAMS];
  /* 1 << side_stream_index (1, 2, 4, 8) */
  uint32_t                   id_flag;

  /* a id3v2 tag of this many bytes has been parserd, or -1. */
  int                        id3v2_tag_size;

  /* stream meta information */
  /* Grab lock, or use helpers (see info_helper.c). */
  xine_rwlock_t              info_lock;
  int                        stream_info[XINE_STREAM_INFO_MAX];
  /* Broken API: _x_meta_info_get_public () returns const char *, with no go away safety.
   * For now, we copy info to info_public when a new value is requested :-/ */
  xine_rwlock_t              meta_lock;
  char                      *meta_info_public[XINE_STREAM_INFO_MAX];
  char                      *meta_info[XINE_STREAM_INFO_MAX];

  /* seeking slowdown */
  struct {
    pthread_mutex_t          lock;
    pthread_cond_t           reached;
    /* 3: wait for first frame to decode (stream start).
     * 2: wait for first frame to display (stream seek).
     * 1: after 2, first frame is decoded but not yet displayed.
     * 0: waiting done.
     */
    uint32_t                 flag:2;
  } first_frame;

  /* wait for headers sent / stream decoding finished */
  struct {
    pthread_mutex_t          lock;
    pthread_cond_t           changed;
    int                      headers_audio;
    int                      headers_video;
    int                      finisheds_audio;
    int                      finisheds_video;
    int                      demuxers_running;
    /* network buffering control. */
    int                      nbc_refs;
    xine_nbc_t              *nbc;
  } counter;

  /* event mechanism */
  struct {
    pthread_mutex_t          lock;
    xine_list_t             *queues;
  } event;

  /* demux thread stuff */
  struct {
    demux_plugin_t          *plugin;
    pthread_t                thread;
    pthread_mutex_t          lock;
    pthread_mutex_t          action_lock;
    pthread_cond_t           resume;
    /* used in _x_demux_... functions to synchronize order of pairwise A/V buffer operations */
    pthread_mutex_t          pair;
    /* next 2 protected by action_lock */
    uint32_t                 action_pending;
    uint32_t                 input_caps;
    uint32_t                 thread_created:1;
    uint32_t                 thread_running:1;
    /* filter out duplicate seek discontinuities from side streams */
    uint32_t                 max_seek_bufs;
    /* set of id_flag values */
    uint32_t                 start_buffers_sent;
  } demux;

#define XINE_NUM_CURR_EXTRA_INFOS 2
  xine_refs_t                current_extra_info_index;
  extra_info_t               current_extra_info[XINE_NUM_CURR_EXTRA_INFOS];
  int                        video_seek_count;

  int                        delay_finish_event; /* delay event in 1/10 sec units. 0=>no delay, -1=>forever */

  int                        slave_affection;   /* what operations need to be propagated down to the slave? */

  int                        err;

  xine_post_out_t            video_source;
  xine_post_out_t            audio_source;

  broadcaster_t             *broadcaster;

  xine_refs_t                refs;

  struct {
    pthread_mutex_t          lock;
    xine_keyframes_entry_t  *array;
    int                      size, used, lastadd;
  } index;

  uint32_t                   disable_decoder_flush_at_discontinuity;

  /* all input is... */
  uint32_t                   seekable;

  /* _x_find_input_plugin () recursion protection */
  input_class_t             *query_input_plugins[2];

  extra_info_t               ei[2];
} xine_stream_private_t;

void xine_current_extra_info_set (xine_stream_private_t *stream, const extra_info_t *info) INTERNAL;

/* Nasty net_buf_ctrl helper: inform about something outside its regular callbacks. */
#define XINE_NBC_EVENT_AUDIO_DRY 1
void xine_nbc_event (xine_stream_private_t *stream, uint32_t type) INTERNAL;

/* Enable file_buf_ctrl optimizations when there is no net_buf_ctrl.
 * This is a kludge to detect less compatible plugins like vdr and vdr-xineliboutput.
 * Return actual state. */
int xine_fbc_set (fifo_buffer_t *fifo, int on) INTERNAL;

/** The fast text feature. */
typedef struct xine_fast_text_s xine_fast_text_t;
/** load fast text from file. */
xine_fast_text_t *xine_fast_text_load (const char *filename, size_t max_size) INTERNAL;
/** get next line. you may modify return[0] ... return[filesize].
 *  it all stays valid until xine_fast_text_unload (). */
char *xine_fast_text_line (xine_fast_text_t *xft, size_t *linesize) INTERNAL;
/** free the text. */
void xine_fast_text_unload (xine_fast_text_t **xft) INTERNAL;

EXTERN_C_STOP

#endif
