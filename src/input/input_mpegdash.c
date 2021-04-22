/*
 * Copyright (C) 2021 the xine project
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#define LOG_MODULE "input_mpegdash"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/input_plugin.h>
#include <xine/stree.h>
#include <xine/mfrag.h>

#include <xine/io_helper.h>

#include "http_helper.h"
#include "input_helper.h"
#include "group_network.h"
#include "multirate_pref.c"
#include "net_buf_ctrl.h"

typedef struct {
  input_class_t     input_class;
  xine_t           *xine;
  multirate_pref_t  pref;
} mpd_input_class_t;

typedef struct {
  uint32_t mime, sfile, init, media, id; /** << offs into stree buf */
#define MPD_TYPE_AUDIO 1
#define MPD_TYPE_VIDEO 2
#define MPD_TYPE_SUBT  4
  uint32_t type;
  uint32_t index_p, index_as, index_r;   /** << xine_stree_t units */
  uint32_t timebase;                     /** << units/second */
  uint32_t bitrate;                      /** << bits/second */
  uint32_t samplerate;                   /** << audio_samples/second */
  uint32_t w, h;                         /** << video pixels */
  uint32_t frag_start;                   /** << frag number offset */
  uint32_t frag_duration;                /** << timebase units */
  uint32_t frag_count;                   /** << 0 in live mode */
} mpd_stream_info_t;

typedef enum {
  MPD_LIVE = 0,
  MPD_SINGLE_LIVE,
  MPD_INIT_LIVE,
  MPD_VOD,
  MPD_SINGLE_VOD,
  MPD_INIT_VOD,
  MPD_MODE_LAST
} mpd_mode_t;

#define MPD_IS_LIVE(this) ((this->mode == MPD_LIVE) || (this->mode == MPD_SINGLE_LIVE) || (this->mode == MPD_INIT_LIVE))

static const char *mpd_mode_names[MPD_MODE_LAST] = {
  [MPD_LIVE]        = "non seekable live mode",
  [MPD_SINGLE_LIVE] = "non seekable single file live mode",
  [MPD_INIT_LIVE]   = "non seekable live mode with init fragment",
  [MPD_VOD]         = "seekable VOD mode",
  [MPD_SINGLE_VOD]  = "seekable single file VOD mode",
  [MPD_INIT_VOD]    = "seekable VOD mode with init fragment"
};

typedef struct mpd_input_plugin_s {
  input_plugin_t    input_plugin;
  xine_stream_t    *stream;
  xine_nbc_t       *nbc;

  struct mpd_input_plugin_s *main_input;

  input_plugin_t   *in1;
  uint32_t          caps1;

  uint32_t          side_index; /** << 0..3 */
  uint32_t          num_sides;

  struct {
    pthread_mutex_t mutex;
    time_t          avail_start, play_start; /** << seconds since 1970 */
    struct timespec play_systime;
    int             lag;  /** pts */
    uint32_t        type;
    int             init;
    int             refs;
  }                 sync;  /** set by main input, used by sides */

  int               lag;  /** pts */

  xine_stree_t     *tree;
  char             *list_buf;
  xine_stree_mode_t tmode;

  uint32_t          base_url, seg_base_url, time_url; /** << offs into stree buf */
  int64_t           frag_num;      /** << derived from manifest */
  uint32_t          frag_index;    /** << 0 (init), 1...n (real frags) */
  uint32_t          frag_mrl_1;    /** << [foo/bar_]12345.mp4 */
  uint32_t          frag_mrl_2;    /** << foo/bar_[12345].mp4 */
  uint32_t          frag_mrl_3;    /** << foo/ber_12345[.mp4] */

#define MPD_MAX_SIDES 4
#define MPD_MAX_REPR 16
  uint8_t           side_have_streams[MPD_MAX_SIDES][MPD_MAX_REPR];

#define MPD_MAX_STREAMS 32
  uint32_t          num_streams, used_stream;
  mpd_stream_info_t info;
  mpd_stream_info_t streams[MPD_MAX_STREAMS];
  multirate_pref_t  items[MPD_MAX_STREAMS];

  xine_mfrag_list_t *fraglist;
  off_t             pos, frag_pos, all_size;
  uint32_t          frag_size;
  uint32_t          prev_size1; /** << the actual preview bytes, for INPUT_OPTIONAL_DATA_[SIZED]_PREVIEW. */
  uint32_t          prev_size2; /** << for read (), 0 after leaving that range. */

  uint32_t          list_bsize;
  uint32_t          duration;
  mpd_mode_t        mode;
#define MPD_MAX_MRL 4096
  char              manifest_mrl[MPD_MAX_MRL];
  char              list_mrl[MPD_MAX_MRL];
  char              item_mrl[MPD_MAX_MRL];
#define MPD_PREVIEW_SIZE (32 << 10)
  char              preview[MPD_PREVIEW_SIZE];
} mpd_input_plugin_t;

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

static uint32_t str2uint32 (char **s) {
  uint8_t *p = (uint8_t *)*s;
  uint32_t v = 0;
  uint8_t z;
  while ((z = *p ^ '0') < 10) {
    v = v * 10u + z;
    p++;
  }
  *s = (char *)p;
  return v;
}

static time_t mpd_str2time (char *s) {
  char buf[256], *tz;
  struct tm tm;
  time_t ret;
  /* Sigh. try to parse something like "1969-12-31T23:59:44Z" or "PT5H30M55S". */
  if (!s)
    return (time_t)-1;

  if (((s[0] | 0x20) == 'p') && ((s[1] | 0x20) == 't')) {
    ret = 0;
    s += 2;
    while (1) {
      uint32_t v = str2uint32 (&s);
      uint32_t z = (*s | 0x20);
      if (z == 'h')
        ret += 3600u * v;
      else if (z == 'm')
        ret += 60u * v;
      else if (z == 's')
        ret += v;
      else
        break;
      s++;
    }
    return ret;
  }

  tm.tm_year = (int)str2uint32 (&s) - 1900;
  if (*s++ != '-')
    return (time_t)-1;
  tm.tm_mon = (int)str2uint32 (&s) - 1;
  if (*s++ != '-')
    return (time_t)-1;
  tm.tm_mday= str2uint32 (&s);
  if ((*s++ | 0x20) != 't')
    return (time_t)-1;
  tm.tm_hour = str2uint32 (&s);
  if (*s++ != ':')
    return (time_t)-1;
  tm.tm_min = str2uint32 (&s);
  if (*s++ != ':')
    return (time_t)-1;
  tm.tm_sec = str2uint32 (&s);
  tm.tm_wday = 0;
  tm.tm_yday = 0;
  tm.tm_isdst = 0;

  tz = getenv ("TZ");
  strlcpy (buf, tz ? tz : "", sizeof (buf));
  setenv ("TZ", "", 1);
  tzset ();
  ret = mktime (&tm);
  if (buf[0])
    setenv ("TZ", buf, 1);
  else
    unsetenv ("TZ");
  tzset ();

  return ret;
}

static char *mpd_strcasestr (const char *haystack, const char *needle) {
  const char *n;
  size_t ln;
  char z;

  if (!haystack)
    return NULL;
  if (!needle)
    return (char *)haystack;
  if (!needle[0])
    return (char *)haystack;

  n = haystack;
  needle++;
  ln = strlen (needle);
  z = needle[-1] | 0x20;
  if ((z >= 'a') && (z <= 'z')) {
    while ((n = strchr (n, z))) {
      if (!strncasecmp (n + 1, needle, ln))
        return (char *)n;
      n++;
    }
    z &= 0xdf;
    n = haystack;
  } else {
    z = needle[-1];
  }
  while ((n = strchr (n, z))) {
    if (!strncasecmp (n + 1, needle, ln))
      return (char *)n;
    n++;
  }
  return NULL;
}

static int mpd_build_mrl (mpd_input_plugin_t *this, const char *name) {
  const char *p, *b;
  char *q, *e;

  _x_merge_mrl (this->item_mrl, MPD_MAX_MRL, this->list_buf + this->base_url, name);

  q = this->list_mrl;
  e = q + MPD_MAX_MRL;
  p = this->item_mrl;
  while ((b = mpd_strcasestr (p, "$RepresentationId$"))) {
    size_t l = b - p;
    if (l >= (size_t)(e - q))
      return 0;
    if (l) {
      memcpy (q, p, l);
      q += l;
    }
    p = b + strlen ("$RepresentationId$");
    q += strlcpy (q, this->list_buf + this->info.id, e - q);
    if (q >= e)
      return 0;
  }
  q += strlcpy (q, p, e - q);
  if (q >= e)
    return 0;

  _x_merge_mrl (this->item_mrl, MPD_MAX_MRL, this->manifest_mrl, this->list_mrl);
  return 1;
}

static void mpd_prepare_fragnum (mpd_input_plugin_t *this) {
  const char *b = mpd_strcasestr (this->item_mrl, "$Number$");

  if (b) {
    this->frag_mrl_1 = b - (const char *)this->item_mrl;
    this->frag_mrl_2 = strlen ("$Number$");
    this->frag_mrl_3 = strlen (b + this->frag_mrl_2);
  } else {
    this->frag_mrl_1 = strlen (this->item_mrl);
    this->frag_mrl_2 = 0;
    this->frag_mrl_3 = 0;
  }
}

static void mpd_apply_fragnum (mpd_input_plugin_t *this) {
  if (this->frag_mrl_2) {
    char buf[32];
    uint32_t l_2 = sprintf (buf, "%" PRId64, this->frag_num);

    if (l_2 != this->frag_mrl_2) {
      memmove (this->item_mrl + this->frag_mrl_1 + l_2,
        this->item_mrl + this->frag_mrl_1 + this->frag_mrl_2, this->frag_mrl_3 + 1);
      this->frag_mrl_2 = l_2;
    }
    memcpy (this->item_mrl + this->frag_mrl_1, buf, l_2);
  }
}

static int mpd_input_switch_mrl (mpd_input_plugin_t *this) {
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_mpegdash.%d: %s.\n", (int)this->side_index, this->item_mrl);
  if (this->in1) {
    if (this->in1->get_capabilities (this->in1) & INPUT_CAP_NEW_MRL) {
      if (this->in1->get_optional_data (this->in1, this->item_mrl,
        INPUT_OPTIONAL_DATA_NEW_MRL) == INPUT_OPTIONAL_SUCCESS) {
        if (this->in1->open (this->in1) > 0)
          return 1;
      }
    }
    _x_free_input_plugin (this->stream, this->in1);
  }
  this->in1 = _x_find_input_plugin (this->stream, this->item_mrl);
  if (!this->in1)
    return 0;
  if (this->in1->open (this->in1) <= 0)
    return 0;
  return 1;
}

static int mpd_set_start_time (mpd_input_plugin_t *this) {
  if (!MPD_IS_LIVE (this)) {
    if (!mpd_build_mrl (this, this->list_buf + this->info.media))
      return 0;
    this->frag_index = 1;
    this->frag_num = this->info.frag_start;
    mpd_prepare_fragnum (this);
    return 2;
  }

  if (!this->side_index) { /* main */
    char buf[256];
    time_t play_start;
    struct timespec ts;
    int l;

    if (this->sync.avail_start == (time_t)-1)
      return 0;
    if (!this->info.timebase || !this->info.frag_duration)
      return 0;
    if (!mpd_build_mrl (this, this->list_buf + this->time_url))
      return 0;
    if (!mpd_input_switch_mrl (this))
      return 0;
    l = this->in1->read (this->in1, buf, sizeof (buf) - 1);
    if (l <= 0)
      return 0;
    buf[l] = 0;
    play_start = mpd_str2time (buf);
    if (play_start == (time_t)-1)
      return 0;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    xine_gettime (&ts);
    this->frag_index = 1;
    /* heavy magic ;-) */
    {
      int64_t d = play_start - this->sync.avail_start;
      d *= this->info.timebase;
      this->frag_num = d / this->info.frag_duration + this->info.frag_start;
      this->lag = (d % this->info.frag_duration) * 90000 / this->info.timebase;
    }
    if (this->sync.init) {
      pthread_mutex_lock (&this->sync.mutex);
      this->sync.play_start = play_start;
      this->sync.play_systime = ts;
      this->sync.lag = this->lag;
      this->sync.type = this->info.type;
      pthread_mutex_unlock (&this->sync.mutex);
    } else {
      this->sync.play_start = play_start;
      this->sync.play_systime = ts;
      this->sync.lag = this->lag;
      this->sync.type = this->info.type;
    }
  } else {
    mpd_input_plugin_t *main_input = this->main_input;

    if (!this->info.timebase || !this->info.frag_duration)
      return 0;
    if (main_input->sync.init) {
      pthread_mutex_lock (&this->sync.mutex);
      this->sync.avail_start = main_input->sync.avail_start;
      this->sync.play_start = main_input->sync.play_start;
      this->sync.play_systime = main_input->sync.play_systime;
      this->sync.lag = main_input->sync.lag;
      this->sync.type = main_input->sync.type;
      pthread_mutex_unlock (&this->sync.mutex);
    } else {
      this->sync.avail_start = main_input->sync.avail_start;
      this->sync.play_start = main_input->sync.play_start;
      this->sync.play_systime = main_input->sync.play_systime;
      this->sync.lag = main_input->sync.lag;
      this->sync.type = main_input->sync.type;
    }
    if (this->sync.avail_start == (time_t)-1)
      return 0;
    this->frag_index = 1;
    /* heavy magic ;-) */
    {
      int64_t d = this->sync.play_start - this->sync.avail_start;
      d *= this->info.timebase;
      this->frag_num = d / this->info.frag_duration + this->info.frag_start;
      this->lag = (d % this->info.frag_duration) * 90000 / this->info.timebase;
    }
  }
    
  if (!mpd_build_mrl (this, this->list_buf + this->info.media))
    return 0;
  mpd_prepare_fragnum (this);
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_mpegdash.%d: live start @ fragment #%" PRId64 ", lag %d pts.\n",
    (int)this->side_index, this->frag_num, this->lag);
  return 1;
}

static int mpd_set_frag_index (mpd_input_plugin_t *this, uint32_t index, int wait) {
  if (!MPD_IS_LIVE (this)) {
    this->frag_num = this->info.frag_start + index - 1;
    this->frag_index = index;
    mpd_apply_fragnum (this);
  } else {
    int32_t d = index - this->frag_index;
    this->frag_num += d;
    this->frag_index = index;
    mpd_apply_fragnum (this);
    if (!wait) 
      return 2;
    if (d > 0) {
      int32_t ms;
      struct timespec ts = {0, 0};
      xine_gettime (&ts);
      ms = (ts.tv_sec - this->sync.play_systime.tv_sec) * 1000;
      ms += (ts.tv_nsec - this->sync.play_systime.tv_nsec) / 1000000;
      ms = (int64_t)(index - 1) * 1000 * this->info.frag_duration / this->info.timebase - ms;
      if ((ms > 0) && (ms < 100000)) {
        /* save server load and hang up before wait. */
        if (this->in1) {
          if (this->in1->get_capabilities (this->in1) & INPUT_CAP_NEW_MRL) {
            char no_mrl[] = "";
            this->in1->get_optional_data (this->in1, no_mrl, INPUT_OPTIONAL_DATA_NEW_MRL);
          }
        }
        if (_x_io_select (this->stream, -1, 0, ms) != XIO_TIMEOUT)
          return 0;
      }
    }
  }
  return mpd_input_switch_mrl (this);
}

static void mpd_frag_seen (mpd_input_plugin_t *this) {
  this->frag_pos = this->pos;
  if (this->in1) {
    int64_t l = this->in1->get_length (this->in1);
    if (l > 0) {
      this->frag_size = l;
      xine_mfrag_set_index_frag (this->fraglist, this->frag_index,
        (this->frag_index && this->info.frag_duration) ? (int64_t)this->info.frag_duration : -1, l);
    } else if (xine_mfrag_get_index_frag (this->fraglist, this->frag_index, NULL, &l) && (l > 0)) {
      this->frag_size = l;
    } else {
      this->frag_size = 0;
    }
  } else {
    this->frag_size = 0;
  }
}

static void mpd_frag_end (mpd_input_plugin_t *this) {
  int64_t l = this->pos - this->frag_pos;
  if (l > (int64_t)this->frag_size) {
    this->frag_size = l;
    xine_mfrag_set_index_frag (this->fraglist, this->frag_index, -1, l);
  }
}

static ssize_t mpd_read_int (mpd_input_plugin_t *this, void *buf, size_t len, int wait) {
  char *q = (char *)buf;

  if (len == 0)
    return 0;

  if (this->pos <= (off_t)this->prev_size2) {
    size_t n = this->prev_size2 - this->pos;
    if (n > 0) {
      if (n > len)
        n = len;
      memcpy (q, this->preview + this->pos, n);
      q += n;
      this->pos += n;
      len -= n;
    }
    if (len > 0)
      this->prev_size2 = 0;
  }
  if (len == 0)
    return q - (char *)buf;

  if (this->frag_index == 0) {
    if (this->list_buf[this->info.init]) {
      int r;
      if (this->pos == 0) {
        if (!mpd_build_mrl (this, this->list_buf + this->info.init))
          return -1;
        if (!mpd_input_switch_mrl (this))
          return -1;
        mpd_frag_seen (this);
      }
      r = this->in1->read (this->in1, q, len);
      if (r < 0)
        return -1;
      q += r;
      this->pos += r;
      len -= r;
      if (len == 0)
        return q - (char *)buf;
    }
    mpd_frag_end (this);
    if (!mpd_set_start_time (this))
      return q - (char *)buf;
    mpd_apply_fragnum (this);
    if (!mpd_input_switch_mrl (this))
      return q - (char *)buf;
    mpd_frag_seen (this);
  }

  while (len > 0) {
    int r = this->in1->read (this->in1, q, len);
    if (r < 0)
      return q > (char *)buf ? q - (char *)buf : -1;
    q += r;
    this->pos += r;
    len -= r;
    if (len == 0)
      break;
    if (r == 0) {
      if ((this->mode == MPD_SINGLE_LIVE) || (this->mode == MPD_SINGLE_VOD))
        break;
      mpd_frag_end (this);
      if (mpd_set_frag_index (this, this->frag_index + 1, wait) != 1)
        break;
      mpd_frag_seen (this);
    }
  }

  return q - (char *)buf;
}

static int mpd_input_get_mrl_ext (const char *mrl, const char **ext) {
  const char *p1, *p2;
  for (p2 = mrl; *p2 && (*p2 != '?'); p2++) ;
  for (p1 = p2; (p1 > mrl) && (p1[-1] != '.'); p1--) ;
  *ext = p1;
  return p2 - p1;
}

static int mpd_input_is_mpd (const char *mrl) {
  const char *ext;
  int n = mpd_input_get_mrl_ext (mrl, &ext);
  if ((n == 3) && !strncasecmp (ext, "mpd", 3))
    return 1;
  return 0;
}

/*
static uint32_t str2msec (char **s) {
  uint8_t *p = (uint8_t *)*s;
  uint32_t v = 0;
  uint8_t z;
  while ((z = *p ^ '0') < 10) {
    v = v * 10u + z;
    p++;
  }
  v *= 1000u;
  if (z == ('.' ^ '0')) {
    p++;
    if ((z = *p ^ '0') < 10) {
      v += 100u * z;
      p++;
      if ((z = *p ^ '0') < 10) {
        v += 10u * z;
        p++;
        if ((z = *p ^ '0') < 10) {
          v += z;
          p++;
        }
      }
    }
  }
  *s = (char *)p;
  return v;
}
*/

static uint32_t mpd_stree_find (mpd_input_plugin_t *this, const char *path, uint32_t base) {
  return 4 + this->tree[xine_stree_find (this->tree, this->list_buf + 4, path, base, 0)].value;
}

static int mpd_input_load_manifest (mpd_input_plugin_t *this) {
  ssize_t size;
  uint32_t tree_mpd;

  {
    off_t s = this->in1->get_length (this->in1);
    if (s > (32 << 20))
      return 0;
    size = s;
  }
  if (size > 0) {
    /* size known, get at once. */
    if (this->in1->seek (this->in1, 0, SEEK_SET) != 0)
      return 0;
    if ((uint32_t)size + 8 > this->list_bsize) {
      char *nbuf = realloc (this->list_buf, (uint32_t)size * 5 / 4 + 8);
      if (!nbuf)
        return 0;
      this->list_buf = nbuf;
      this->list_bsize = (uint32_t)size * 5 / 4 + 8;
    }
    if (this->in1->read (this->in1, this->list_buf + 4, size) != size)
      return 0;
  } else {
    /* chunked/deflated */
    uint32_t have;
    if (!this->list_buf) {
      this->list_buf = malloc (32 << 10);
      if (!this->list_buf)
          return 0;
      this->list_bsize = 32 << 10;
    }
    have = 0;
    while (1) {
      int32_t r = this->in1->read (this->in1, this->list_buf + 4 + have, this->list_bsize - have - 8);
      if (r <= 0)
        break;
      have += r;
      if (have == this->list_bsize - 8) {
        char *nbuf;
        if (this->list_bsize >= (32 << 20))
          break;
        nbuf = realloc (this->list_buf, this->list_bsize + (32 << 10));
        if (!nbuf)
          return 0;
        this->list_buf = nbuf;
        this->list_bsize += 32 << 10;
      }
    }
    size = have;
  }
  memset (this->list_buf, 0, 4);
  memset (this->list_buf + 4 + size, 0, 4);


  this->tmode = XINE_STREE_AUTO;
  this->tree = xine_stree_load (this->list_buf + 4, &this->tmode);
  if (!this->tree) {
    this->list_bsize = 0;
    _x_freep (&this->list_buf);
    return 0;
  }

  tree_mpd = xine_stree_find (this->tree, this->list_buf + 4, "mpd", 0, 0);
  if (!tree_mpd) {
    tree_mpd = xine_stree_find (this->tree, this->list_buf + 4, "?xml.mpd", 0, 0);
    if (!tree_mpd) {
      xine_stree_delete (&this->tree);
      this->list_bsize = 0;
      _x_freep (&this->list_buf);
      return 0;
    }
  }

  this->base_url = mpd_stree_find (this, "BaseURL", tree_mpd);
  this->seg_base_url = mpd_stree_find (this, "SegmentBase", tree_mpd);
  if (!this->list_buf[this->seg_base_url])
    this->seg_base_url = this->base_url;
  this->time_url = mpd_stree_find (this, "UTCTiming.value", tree_mpd);
  {
    char *s = this->list_buf + mpd_stree_find (this, "availabilityStartTime", tree_mpd);
    this->sync.avail_start = mpd_str2time (s);
  }

  {
    char path_p[] = "Period[  ]";
    uint32_t period, index_p;

    this->num_streams = 0;
    for (period = 0; this->num_streams < MPD_MAX_STREAMS; period++) {
      char path_as[] = "AdaptationSet[  ]";
      uint32_t adaptationset, index_as, max_as;

      path_p[7] = period < 10 ? ' ' : '0' + period / 10u;
      path_p[8] = '0' + period % 10u;
      index_p = xine_stree_find (this->tree, this->list_buf + 4, path_p, tree_mpd, 0);
      if (!index_p)
        break;

      max_as = MPD_MAX_STREAMS - this->num_streams;
      if (max_as > MPD_MAX_SIDES)
        max_as = MPD_MAX_SIDES;
      for (adaptationset = 0; adaptationset < max_as; adaptationset++) {
        char path_r[] = "Representation[  ]";
        uint32_t representation, max_r;
        mpd_stream_info_t *info;
        char *s;

        path_as[14] = adaptationset < 10 ? ' ' : '0' + adaptationset / 10;
        path_as[15] = '0' + adaptationset % 10;
        index_as = xine_stree_find (this->tree, this->list_buf + 4, path_as, index_p, 0);
        if (!index_as)
          break;
        info = this->streams + this->num_streams;
        s = this->list_buf + mpd_stree_find (this, "contentType", index_as);
        info->type = 0;
        if (strcasestr (s, "audio"))
          info->type |= MPD_TYPE_AUDIO;
        if (strcasestr (s, "video"))
          info->type |= MPD_TYPE_VIDEO;
        if (strcasestr (s, "subtitle"))
          info->type |= MPD_TYPE_SUBT;
        info->mime = mpd_stree_find (this, "mimeType", index_as);
        info->index_p = index_p;
        info->index_as = index_as;
        info->index_r = 0;
        s = this->list_buf + mpd_stree_find (this, "SegmentTemplate.timescale", index_as);
        info->timebase = str2uint32 (&s);
        s = this->list_buf + mpd_stree_find (this, "audioSamplingRate", index_as);
        info->samplerate = str2uint32 (&s);
        info->init = mpd_stree_find (this, "SegmentTemplate.initialization", index_as);
        info->media = mpd_stree_find (this, "SegmentTemplate.media", index_as);
        s = this->list_buf + mpd_stree_find (this, "width", index_as);
        info->w = str2uint32 (&s);
        s = this->list_buf + mpd_stree_find (this, "height", index_as);
        info->h = str2uint32 (&s);
        /* this seems to default to 1. */
        s = this->list_buf + mpd_stree_find (this, "SegmentTemplate.startNumber", index_as);
        info->frag_start = s[0] ? str2uint32 (&s) : 1;
        s = this->list_buf + mpd_stree_find (this, "SegmentTemplate.duration", index_as);
        info->frag_duration = str2uint32 (&s);
        info->id = 0;
        info->bitrate = 0;

        max_r = MPD_MAX_STREAMS - this->num_streams;
        if (max_r > MPD_MAX_REPR - 1)
          max_r = MPD_MAX_REPR - 1;
        for (representation = 0; representation < max_r; representation++) {
          uint32_t index_r;

          path_r[15] = representation < 10 ? ' ' : '0' + representation / 10;
          path_r[16] = '0' + representation % 10;
          index_r = xine_stree_find (this->tree, this->list_buf + 4, path_r, index_as, 0);
          if (!index_r)
            break;
          if (representation)
            info[0] = info[-1];
          info->index_r = index_r;
          info->id = mpd_stree_find (this, "id", index_r);
          info->sfile = mpd_stree_find (this, "BaseURL", index_r);
          s = this->list_buf + mpd_stree_find (this, "SegmentBase.timescale", index_r);
          if (s[0])
            info->timebase = str2uint32 (&s);
          s = this->list_buf + mpd_stree_find (this, "audioSamplingRate", index_r);
          if (s[0])
            info->samplerate = str2uint32 (&s);
          s = this->list_buf + mpd_stree_find (this, "bandwidth", index_r);
          info->bitrate = str2uint32 (&s);
          s = this->list_buf + mpd_stree_find (this, "width", index_r);
          info->w = str2uint32 (&s);
          s = this->list_buf + mpd_stree_find (this, "height", index_r);
          info->h = str2uint32 (&s);
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_mpegdash.%d: stream[%2u]: %s %ux%u %uHz %ubps.\n",
            (int)this->side_index,
            (unsigned int)this->num_streams, this->list_buf + info->mime, (unsigned int)info->w, (unsigned int)info->h,
            (unsigned int)info->samplerate, (unsigned int)info->bitrate);
          info++;
          this->side_have_streams[adaptationset][representation] = this->num_streams;
          this->num_streams += 1;
        }

        if (!representation) {
          /* FIXME: empty adaptationset?? */
          info->sfile = 0;
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_mpegdash.%d: stream[%2u]: %s %ux%u %uHz %ubps.\n",
            (int)this->side_index,
            (unsigned int)this->num_streams, this->list_buf + info->mime, (unsigned int)info->w, (unsigned int)info->h,
            (unsigned int)info->samplerate, (unsigned int)info->bitrate);
          this->side_have_streams[adaptationset][representation] = this->num_streams;
          this->num_streams += 1;
        }
        this->side_have_streams[adaptationset][representation] = 255;
      }
      this->num_sides = adaptationset;
    }
  }

  {
    uint32_t u;

    for (u = 0; u < this->num_streams; u++) {
      mpd_stream_info_t *info = this->streams + u;

      if (!this->list_buf[info->media])
        info->media = info->sfile;
    }
  }

  return 1;
}

static uint32_t mpd_input_get_capabilities (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  if (!this)
    return 0;
  if (MPD_IS_LIVE (this))
    return INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW | INPUT_CAP_LIVE;
  if (this->fraglist)
    return INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW | INPUT_CAP_SLOW_SEEKABLE | INPUT_CAP_TIME_SEEKABLE;
  if (this->in1) {
    this->caps1 = this->in1->get_capabilities (this->in1);
    return INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW | (this->caps1 & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE));
  }
  return INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW;
}

static off_t mpd_input_read (input_plugin_t *this_gen, void *buf, off_t len) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  if (!this)
    return 0;
  return mpd_read_int (this, buf, len, 1);
}

static buf_element_t *mpd_input_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {
  (void)this_gen;
  (void)fifo;
  (void)todo;
  return NULL;
}

static off_t mpd_input_time_seek (input_plugin_t *this_gen, int time_offs, int origin) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;

  if (!this)
    return 0;

  do {
    xine_mfrag_index_t idx;
    int64_t frag_time1, frag_time2;
    uint32_t new_time;

    if (!this->fraglist)
      return this->pos;

    switch (origin) {
      case SEEK_SET:
        new_time = 0;
        break;
      case SEEK_CUR:
        if (xine_mfrag_get_index_start (this->fraglist, this->frag_index, &frag_time1, NULL)
          && xine_mfrag_get_index_start (this->fraglist, this->frag_index + 1, &frag_time2, NULL)) {
          new_time = frag_time1 * 1000 / this->info.timebase;
          if (this->frag_size)
            new_time += ((frag_time2 - frag_time1) * 1000 / this->info.timebase) * (this->pos - this->frag_pos) / this->frag_size;
        } else {
          new_time = 0;
        }
        break;
      case SEEK_END:
        if (xine_mfrag_get_index_start (this->fraglist, xine_mfrag_get_frag_count (this->fraglist) + 1, &frag_time1, NULL)) {
          new_time = frag_time1 * 1000 / this->info.timebase;
        } else {
          new_time = 0;
        }
        break;
      default:
        errno = EINVAL;
        return (off_t)-1;
    }
    new_time += time_offs;

    frag_time1 = (int64_t)new_time * this->info.timebase / 1000;
    idx = xine_mfrag_find_time (this->fraglist, frag_time1);
    if (idx < 1)
      break;
    if (!xine_mfrag_get_index_start (this->fraglist, idx, NULL, &frag_time1))
      break;
    if ((uint32_t)idx != this->frag_index) {
      if (!mpd_set_frag_index (this, idx, 1))
        break;
    }
    this->prev_size2 = 0;
    this->pos = frag_time1;
    mpd_frag_seen (this);
    return this->pos;
  } while (0);

  errno = EINVAL;
  return (off_t)-1;
}

static off_t mpd_input_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  off_t new_offs;

  if (!this)
    return 0;

  switch (origin) {
    case SEEK_SET:
      new_offs = offset;
      break;
    case SEEK_CUR:
      new_offs = this->pos + offset;
      break;
    case SEEK_END:
      if (MPD_IS_LIVE (this))
        return this->pos;
      if (this->fraglist) {
        int n;
        int64_t l;
        n = xine_mfrag_get_frag_count (this->fraglist);
        if (n < 1)
          return this->pos;
        this->info.frag_count = n;
        if (!xine_mfrag_get_index_start (this->fraglist, n + 1, NULL, &l))
          return this->pos;
        if (l <= 0)
          return this->pos;
        this->all_size = l;
        new_offs = l + offset;
        break;
      }
      if (this->in1) {
        off_t l = this->in1->get_length (this->in1);
        if (l > 0) {
          this->all_size = l;
          new_offs = l + offset;
          break;
        }
      }
      /* fall through */
    default:
      return this->pos;
  }

  /* always seek within the preview. */
  if ((this->pos <= (int)this->prev_size2) && (new_offs >= 0) && (new_offs <= (int)this->prev_size2)) {
    this->pos = new_offs;
    return this->pos;
  }
  this->prev_size2 = 0;

  if (this->fraglist) {
    int64_t frag_pos;
    xine_mfrag_index_t idx = xine_mfrag_find_pos (this->fraglist, new_offs);
    if (idx < 1)
      return this->pos;
    /* HACK: offsets around this fragment may be guessed ones,
     * and the fragment itself may turn out to be smaller than expected.
     * however, demux expects a seek to land at the exact byte offs.
     * lets try to meet that, even if it is still wrong. */
    idx -= 1;
    do {
      idx += 1;
      if (!xine_mfrag_get_index_start (this->fraglist, idx, NULL, &frag_pos))
        return this->pos;
      if ((uint32_t)idx != this->frag_index) {
        mpd_frag_end (this);
        if (!mpd_set_frag_index (this, idx, 1))
          return this->pos;
        this->pos = frag_pos;
        mpd_frag_seen (this);
      }
    } while (new_offs >= this->pos + this->frag_size);
  }

  this->caps1 = this->in1->get_capabilities (this->in1);
  if (this->caps1 & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE)) {
    off_t r = this->in1->seek (this->in1, new_offs - this->frag_pos, SEEK_SET);

    if (r >= 0)
      this->pos = this->frag_pos + r;
    return this->pos;
  }

  new_offs -= this->pos;
  if (new_offs < 0)
    return this->pos;
  {
    while (new_offs > 0) {
      char buf[2048];
      size_t l = new_offs > (int)sizeof (buf) ? sizeof (buf) : (size_t)new_offs;
      ssize_t r = mpd_read_int (this, buf, l, 1);

      if (r <= 0)
        break;
      new_offs -= r;
    }
  }
  return this->pos;
}

static off_t mpd_input_get_current_pos (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  if (!this)
    return 0;
  return this->pos;
}

static off_t mpd_input_get_length (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  if (!this)
    return 0;
  if (MPD_IS_LIVE (this)) {
    if (this->pos > this->all_size)
      this->all_size = this->pos;
  } else if (this->fraglist) {
    int n;
    int64_t l;
    n = xine_mfrag_get_frag_count (this->fraglist);
    if (n >= 1) {
      this->info.frag_count = n;
      if (xine_mfrag_get_index_start (this->fraglist, n + 1, NULL, &l) && (l > 0))
        this->all_size = l;
    }
  } else if (this->in1) {
    off_t l = this->in1->get_length (this->in1);
    if (l > 0)
      this->all_size = l;
  }
  return this->all_size;
}

static const char *mpd_input_get_mrl (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  if (!this)
    return NULL;
  return this->manifest_mrl;
}

static void mpd_input_dispose (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;

  if (!this)
    return;

  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }
  if (this->in1) {
    _x_free_input_plugin (this->stream, this->in1);
    this->in1 = NULL;
  }
  xine_mfrag_list_close (&this->fraglist);
  xine_stree_delete (&this->tree);
  _x_freep (&this->list_buf);

  if (this->side_index) {
    mpd_input_plugin_t *main_input = this->main_input;
    this->sync.refs = 0;
    free (this);
    this = main_input;
  }
  if (this->sync.init) {
    pthread_mutex_lock (&this->sync.mutex);
    if (--this->sync.refs == 0) {
      pthread_mutex_unlock (&this->sync.mutex);
      pthread_mutex_destroy (&this->sync.mutex);
      free (this);
    } else {
      pthread_mutex_unlock (&this->sync.mutex);
    }
  } else {
    if (--this->sync.refs == 0)
      free (this);
  }
}

static int mpd_input_open (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  mpd_input_class_t  *cls  = (mpd_input_class_t *)this->input_plugin.input_class;
  int n;

  if (!this)
    return 0;

  if (!this->side_index) {
    if (!mpd_input_load_manifest (this))
      return 0;
    if ((this->num_sides > 1) && !this->sync.init) {
      pthread_mutex_init (&this->sync.mutex, NULL);
      this->sync.init = 1;
    }
  }

  {
    multirate_pref_t *item = this->items;
    uint32_t u, i;
    for (u = 0; (i = this->side_have_streams[this->side_index][u]) != 255; u++) {
      mpd_stream_info_t *info = this->streams + i;
      item->video_width = info->w;
      item->video_height = info->h;
      item->bitrate = info->bitrate;
      item->lang[0] = 0;
      item++;
    }
    n = multirate_autoselect (&cls->pref, this->items, u);
  }
  if (n < 0) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_mpegdash.%d: no auto selected item.\n", (int)this->side_index);
    return 0;
  }
  this->used_stream = n = this->side_have_streams[this->side_index][n];
  this->info = this->streams[n];
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_mpegdash.%d: auto selected stream #%d.\n", (int)this->side_index, n);
  this->mode = this->list_buf[this->time_url]
             ? (this->list_buf[this->info.init]
                 ? MPD_INIT_LIVE
                 : mpd_strcasestr (this->list_buf + this->info.media, "$Number$") ? MPD_LIVE : MPD_SINGLE_LIVE)
             : (this->list_buf[this->info.init]
                 ? MPD_INIT_VOD
                 : mpd_strcasestr (this->list_buf + this->info.media, "$Number$") ? MPD_VOD : MPD_SINGLE_VOD);
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_mpegdash.%d: %s.\n", (int)this->side_index, mpd_mode_names[this->mode]);

  if ((this->mode == MPD_INIT_VOD) || (this->mode == MPD_VOD)) {
    xine_mfrag_list_open (&this->fraglist);
    xine_mfrag_set_index_frag (this->fraglist, 0, this->info.timebase, 0);
  }

  this->frag_index = 0;
  this->prev_size1 = 0;
  this->prev_size2 = 0;
  this->pos = 0;
  n = mpd_read_int (this, this->preview, sizeof (this->preview), 0);
  if (n <= 0) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
      "input_mpegdash.%d: failed to read preview.\n", (int)this->side_index);
    return 0;
  }
  this->prev_size1 = n;
  this->prev_size2 = n;
  this->pos = 0;
  /*
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_mpegdash.%d: got %u fragments for %u.%03u seconds.\n",
    (int)this->side_index, (unsigned int)this->frag_have,
    (unsigned int)(this->frags[this->frag_have].start_msec / 1000u),
    (unsigned int)(this->frags[this->frag_have].start_msec % 1000u));
  */

  return 1;
}

static input_plugin_t *mpd_get_side (mpd_input_plugin_t *this, int side_index) {
  mpd_input_plugin_t *side_input;

  if (this->side_index)
    return NULL;
  if ((side_index < 1) || (side_index >= (int)this->num_sides))
    return NULL;
  side_input = malloc (sizeof (*side_input));
  if (!side_input)
    return NULL;

  /* clone everything */
  *side_input = *this;

  /* sync */
  if (this->sync.init) {
    pthread_mutex_lock (&this->sync.mutex);
    this->sync.refs++;
    pthread_mutex_unlock (&this->sync.mutex);
  } else {
    this->sync.refs++;
  }
  memset (&side_input->sync.mutex, 0, sizeof (side_input->sync.mutex));
  side_input->sync.init = 0;
  side_input->sync.refs = 1;

  /* detach */
  side_input->side_index = side_index;
  side_input->in1 = NULL;
  side_input->caps1 = 0;
  side_input->tree = NULL;
  side_input->fraglist = NULL;

  side_input->list_buf = malloc (this->list_bsize);
  if (!side_input->list_buf) {
    free (side_input);
    return NULL;
  }
  memcpy (side_input->list_buf, this->list_buf, this->list_bsize);

  side_input->stream = xine_get_side_stream (this->stream, side_index);
  if (!side_input->stream) {
    free (side_input->list_buf);
    free (side_input);
    return NULL;
  }
  side_input->nbc = nbc_init (side_input->stream);

  return &side_input->input_plugin;
}
  
static int mpd_input_get_optional_data (input_plugin_t *this_gen, void *data, int data_type) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;

  if (!this)
    return INPUT_OPTIONAL_UNSUPPORTED;

  switch (data_type) {

    case INPUT_OPTIONAL_DATA_PREVIEW:
      if (!data || (this->prev_size1 <= 0))
        return INPUT_OPTIONAL_UNSUPPORTED;
      {
        uint32_t l = this->prev_size1 > MAX_PREVIEW_SIZE ? MAX_PREVIEW_SIZE : this->prev_size1;
        memcpy (data, this->preview, l);
        return l;
      }

    case INPUT_OPTIONAL_DATA_SIZED_PREVIEW:
      if (!data || (this->prev_size1 <= 0))
        return INPUT_OPTIONAL_UNSUPPORTED;
      {
        int want;
        memcpy (&want, data, sizeof (want));
        want = want < 0 ? 0
             : want > (int)this->prev_size1 ? (int)this->prev_size1
             : want;
        memcpy (data, this->preview, want);
        return want;
      }

    case INPUT_OPTIONAL_DATA_DURATION:
      if (!data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      if (this->fraglist) {
        int64_t d;
        int n = xine_mfrag_get_frag_count (this->fraglist);
        if (n > 0) {
          if (xine_mfrag_get_index_start (this->fraglist, n + 1, &d, NULL))
            this->duration = d * 1000 / this->info.timebase;
        }
      } else {
        this->duration = (int64_t)this->info.frag_count * this->info.frag_duration * 1000 / this->info.timebase;
      }
      memcpy (data, &this->duration, sizeof (this->duration));
      return INPUT_OPTIONAL_SUCCESS;

    case INPUT_OPTIONAL_DATA_FRAGLIST:
      if (!data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      memcpy (data, &this->fraglist, sizeof (this->fraglist));
      return INPUT_OPTIONAL_SUCCESS;

    case INPUT_OPTIONAL_DATA_SIDE:
      if (!data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      {
        int side_index;
        input_plugin_t *side_input;
        memcpy (&side_index, data, sizeof (side_index));
        side_input = mpd_get_side (this, side_index);
        if (!side_input)
          return INPUT_OPTIONAL_UNSUPPORTED;
        memcpy (data, &side_input, sizeof (side_input));
        return INPUT_OPTIONAL_SUCCESS;
      }

    default:
      return INPUT_OPTIONAL_UNSUPPORTED;
  }
}

static input_plugin_t *mpd_input_get_instance (input_class_t *cls_gen, xine_stream_t *stream, const char *mrl) {

  mpd_input_class_t  *cls = (mpd_input_class_t *)cls_gen;
  mpd_input_plugin_t *this;
  input_plugin_t     *in1;
  char                hbuf[2048];
  int                 n;

  if (!cls || !mrl)
    return NULL;
  lprintf("mpd_input_get_instance\n");

  do {
    n = !strncasecmp (mrl, "mpegdash:/", 10) ? 10 : 0;
    in1 = _x_find_input_plugin (stream, mrl + n);
    if (in1) {
      if (in1->open (in1) > 0) {
        int l;
        if (mpd_input_is_mpd (mrl))
          break;
        l = _x_demux_read_header (in1, hbuf, sizeof (hbuf) - 1);
        if (l > 5) {
          char *p = hbuf;
          p[l] = 0;
          while ((p = strchr (p, '<'))) {
            p++;
            if (!strncasecmp (p, "mpd ", 4))
              break;
          }
        }
      }
      _x_free_input_plugin (stream, in1);
    }
    return NULL;
  } while (0);

  this = calloc (1, sizeof (*this));
  if (!this)
    return NULL;

#ifndef HAVE_ZERO_SAFE_MEM
  this->side_index   = 0;
  this->caps1        = 0;
  this->fraglist     = NULL;
  this->list_buf     = NULL;
  this->list_bsize   = 0;
  this->duration     = 0;
  this->all_size     = 0;
  this->sync.lag     = 0;
  this->sync.type    = 0;
  this->sync.init    = 0;
  this->lag          = 0;
#endif

  this->main_input = this;
  this->stream = stream;
  this->in1    = in1;
  this->num_sides = 1;
  this->sync.avail_start =
  this->sync.play_start  = (time_t)-1;
  this->sync.refs = 1;

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_mpegdash.%d: %s.\n", (int)this->side_index, mrl + n);

  strlcpy (this->manifest_mrl, mrl + n, MPD_MAX_MRL);

  this->input_plugin.open               = mpd_input_open;
  this->input_plugin.get_capabilities   = mpd_input_get_capabilities;
  this->input_plugin.read               = mpd_input_read;
  this->input_plugin.read_block         = mpd_input_read_block;
  this->input_plugin.seek               = mpd_input_seek;
  this->input_plugin.seek_time          = mpd_input_time_seek;
  this->input_plugin.get_current_pos    = mpd_input_get_current_pos;
  this->input_plugin.get_length         = mpd_input_get_length;
  this->input_plugin.get_blocksize      = _x_input_default_get_blocksize;
  this->input_plugin.get_mrl            = mpd_input_get_mrl;
  this->input_plugin.get_optional_data  = mpd_input_get_optional_data;
  this->input_plugin.dispose            = mpd_input_dispose;
  this->input_plugin.input_class        = &cls->input_class;

  this->nbc = stream ? nbc_init (stream) : NULL;

  return &this->input_plugin;
}


/*
 * plugin class functions
 */

static void mpd_input_class_dispose (input_class_t *this_gen) {
  mpd_input_class_t *this = (mpd_input_class_t *)this_gen;
  config_values_t   *config = this->xine->config;

  config->unregister_callbacks (config, NULL, NULL, this, sizeof (*this));

  free (this);
}

void *input_mpegdash_init_class (xine_t *xine, const void *data) {

  mpd_input_class_t *this;

  (void)data;
  this = calloc (1, sizeof (*this));
  if (!this)
    return NULL;

  this->xine = xine;
  multirate_pref_get (xine->config, &this->pref);

  this->input_class.get_instance       = mpd_input_get_instance;
  this->input_class.identifier         = "mpegdash";
  this->input_class.description        = N_("MPEG Dynamic Adaptive Streaming over Http input plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = mpd_input_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}
