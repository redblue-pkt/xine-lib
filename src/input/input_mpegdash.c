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
  uint32_t mrl_offs;
  uint32_t start_msec;
  off_t    byte_size;
  off_t    start_offs;
} mpd_frag_info_t;

typedef struct {
  const char     *mime, *init, *media, *id;   /** << ptr into stree buf */
  uint32_t        index_p, index_as, index_r; /** << xine_stree_t units */
  uint32_t        timebase;                   /** << units/second */
  uint32_t        bitrate;                    /** << bits/second */
  uint32_t        samplerate;                 /** << audio_samples/second */
  uint32_t        w, h;                       /** << video pixels */
  uint32_t        frag_start;                 /** << frag number offset */
  uint32_t        frag_duration;              /** << timebase units */
  uint32_t        frag_count;                 /** << 0 in live mode */
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
  [MPD_INIT_VOD]    = "(yet) non seekable VOD mode with init fragment"
};

typedef struct {
  input_plugin_t    input_plugin;
  xine_stream_t    *stream;
  xine_nbc_t       *nbc;

  input_plugin_t   *in1;
  off_t             size1;
  uint32_t          caps1;

  mpd_frag_info_t  *frags, *current_frag;

  xine_stree_t     *tree;
  char             *list_buf;
  xine_stree_mode_t tmode;

  const char       *base_url, *seg_base_url, *time_url; /** << ptr into stree buf */
  time_t            avail_start, play_start; /** << seconds since 1970 */
  int64_t           frag_num;      /** << derived from manifest */
  uint32_t          frag_index;    /** << 0 (init), 1...n (real frags) */
  uint32_t          frag_mrl_1;    /** << [foo/bar_]12345.mp4 */
  uint32_t          frag_mrl_2;    /** << foo/bar_[12345].mp4 */
  uint32_t          frag_mrl_3;    /** << foo/ber_12345[.mp4] */
  struct timespec   play_systime;

#define MPD_MAX_STREAMS 32
  uint32_t          num_streams, used_stream;
  mpd_stream_info_t info;
  mpd_stream_info_t streams[MPD_MAX_STREAMS];
  multirate_pref_t  items[MPD_MAX_STREAMS];

  off_t             pos;
  uint32_t          prev_size;

  uint32_t          list_bsize;
  uint32_t          frag_have;
  uint32_t          frag_max;
  off_t             est_size;
  off_t             seen_size;
  off_t             live_pos;
  uint32_t          seen_num;
  uint32_t          seen_avg;
  uint32_t          duration;
  uint32_t          pos_in_frag;
  mpd_mode_t        mode;
  uint32_t          list_seq;
  uint32_t          items_num;
  const char       *items_mrl[20];
  const char       *list_strtype;
  const char       *list_strseq;
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
  /* Sigh. try to parse something like "1969-12-31T23:59:44Z". */
  if (!s)
    return (time_t)-1;

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

  _x_merge_mrl (this->item_mrl, MPD_MAX_MRL, this->base_url, name);

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
    q += strlcpy (q, this->info.id, e - q);
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
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_mpegdash: %s.\n", this->item_mrl);
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
  char buf[256];
  int l;

  if (!MPD_IS_LIVE (this)) {
    if (!mpd_build_mrl (this, this->info.media))
      return 0;
    mpd_prepare_fragnum (this);
    return 2;
  }

  if (this->avail_start == (time_t)-1)
    return 0;
  if (!this->info.timebase || !this->info.frag_duration)
    return 0;
  if (!mpd_build_mrl (this, this->time_url))
    return 0;
  if (!mpd_input_switch_mrl (this))
    return 0;
  l = this->in1->read (this->in1, buf, sizeof (buf) - 1);
  if (l <= 0)
    return 0;
  buf[l] = 0;
  this->play_start = mpd_str2time (buf);
  if (this->play_start == (time_t)-1)
    return 0;
  this->play_systime.tv_sec = 0;
  this->play_systime.tv_nsec = 0;
  xine_gettime (&this->play_systime);
  this->frag_index = 1;
  /* heavy magic ;-) */
  this->frag_num = (int64_t)(this->play_start - this->avail_start)
    * this->info.timebase / this->info.frag_duration + this->info.frag_start;
  if (!mpd_build_mrl (this, this->info.media))
    return 0;
  mpd_prepare_fragnum (this);
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
      ms = (ts.tv_sec - this->play_systime.tv_sec) * 1000;
      ms += (ts.tv_nsec - this->play_systime.tv_nsec) / 1000000;
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

static ssize_t mpd_read_int (mpd_input_plugin_t *this, void *buf, size_t len, int wait) {
  char *q = (char *)buf;

  if (len == 0)
    return 0;

  if (this->pos < (int)this->prev_size) {
    size_t n = this->prev_size - this->pos;

    if (n > len)
      n = len;
    memcpy (q, this->preview + this->pos, n);
    q += n;
    this->pos += n;
    len -= n;
  }
  if (len == 0)
    return q - (char *)buf;

  if (this->frag_index == 0) {
    if (this->info.init[0]) {
      int r;
      if (this->pos == 0) {
        if (!mpd_build_mrl (this, this->info.init))
          return -1;
        if (!mpd_input_switch_mrl (this))
          return -1;
      }
      r = this->in1->read (this->in1, q, len);
      if (r <= 0)
        return -1;
      q += r;
      this->pos += r;
      len -= r;
      if (len == 0)
        return q - (char *)buf;
    }
    if (!mpd_set_start_time (this))
      return q - (char *)buf;
    mpd_apply_fragnum (this);
    if (!mpd_input_switch_mrl (this))
      return q - (char *)buf;
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
      if (mpd_set_frag_index (this, this->frag_index + 1, wait) != 1)
        break;
    }
  }

  return q - (char *)buf;
}

static int mpd_input_open_item (mpd_input_plugin_t *this, uint32_t n) {
  mpd_frag_info_t *frag;
  /* valid index ? */
  if (n >= this->frag_have)
    return 0;
  /* get fragment mrl */
  _x_merge_mrl (this->item_mrl, MPD_MAX_MRL, this->list_mrl, this->list_buf + this->frags[n].mrl_offs);
  /* get input */
  this->caps1 = 0;
  if (!mpd_input_switch_mrl (this))
    return 0;
  this->caps1 = this->in1->get_capabilities (this->in1);
  /* query fragment */
  this->size1 = this->in1->get_length (this->in1);
  if (this->size1 <= 0)
    return 0;
  /* update size info */
  this->pos_in_frag = 0;
  frag = this->frags + n;
  this->current_frag = frag;
  if (frag->byte_size == 0) {
    this->seen_num += 1;
    this->seen_size += this->size1;
  } else if (frag->byte_size != this->size1) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_mpegdash: WTF: fragment #%u changed size from %" PRId64 " to %" PRId64 " bytes!!\n",
      (unsigned int)n, (int64_t)frag->byte_size, (int64_t)this->size1);
    this->seen_size += this->size1 - frag->byte_size;
  } else {
    n = ~0u;
  }
  if (n != ~0u) {
    uint32_t u;
    off_t pos;
    frag->byte_size = this->size1;
    this->seen_avg = this->seen_size / this->seen_num;
    pos = frag->start_offs;
    /* dont shift current pos as this would confuse demuxers too much. */
    for (u = this->frag_have - n; u; u--) {
      frag->start_offs = pos;
      pos += frag->byte_size == 0 ? this->seen_avg : frag->byte_size;
      frag++;
    }
    frag->start_offs = pos;
    this->est_size = pos;
  }
  return 1;
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

static char *mpd_stree_find (mpd_input_plugin_t *this, const char *path, uint32_t base) {
  return this->list_buf + 4 + this->tree[xine_stree_find (this->tree, this->list_buf + 4, path, base, 0)].value;
}

static int mpd_input_load_manifest (mpd_input_plugin_t *this) {
  ssize_t size;
  uint32_t tree_mpd;

  _x_freep (&this->frags);
  this->frag_have = 0;
  this->frag_max  = 0;
  this->est_size  = 0;
  this->seen_size = 0;
  this->seen_num  = 0;
  this->seen_avg  = 0;
  this->items_num = 0;

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

  this->frags = malloc (256 * sizeof (this->frags[0]));
  if (!this->frags) {
    this->list_bsize = 0;
    _x_freep (&this->list_buf);
    return 0;
  }
  this->frag_max = 256;

  this->list_seq = 1;
  this->list_strseq = "";
  this->list_strtype = "";

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
  if (!this->seg_base_url[0])
    this->seg_base_url = this->base_url;
  this->time_url = mpd_stree_find (this, "UTCTiming.value", tree_mpd);
  {
    char *s = mpd_stree_find (this, "availabilityStartTime", tree_mpd);
    this->avail_start = mpd_str2time (s);
  }

  {
    char path_p[] = "Period[  ]";
    uint32_t period, index_p;

    this->num_streams = 0;
    for (period = 0; this->num_streams < MPD_MAX_STREAMS; period++) {
      char path_as[] = "AdaptationSet[  ]";
      uint32_t adaptationset, index_as;

      path_p[7] = period < 10 ? ' ' : '0' + period / 10u;
      path_p[8] = '0' + period % 10u;
      index_p = xine_stree_find (this->tree, this->list_buf + 4, path_p, tree_mpd, 0);
      if (!index_p)
        break;

      for (adaptationset = 0; this->num_streams < MPD_MAX_STREAMS; adaptationset++) {
        char path_r[] = "Representation[  ]";
        uint32_t representation;
        mpd_stream_info_t *info;
        multirate_pref_t *item;
        char *s;

        path_as[14] = adaptationset < 10 ? ' ' : '0' + adaptationset / 10;
        path_as[15] = '0' + adaptationset % 10;
        index_as = xine_stree_find (this->tree, this->list_buf + 4, path_as, index_p, 0);
        if (!index_as)
          break;
        item = this->items + this->num_streams;
        info = this->streams + this->num_streams;
        info->mime = mpd_stree_find (this, "mimeType", index_as);
        info->index_p = index_p;
        info->index_as = index_as;
        info->index_r = 0;
        s = mpd_stree_find (this, "SegmentTemplate.timescale", index_as);
        info->timebase = str2uint32 (&s);
        s = mpd_stree_find (this, "audioSamplingRate", index_as);
        info->samplerate = str2uint32 (&s);
        info->init = mpd_stree_find (this, "SegmentTemplate.initialization", index_as);
        info->media = mpd_stree_find (this, "SegmentTemplate.media", index_as);
        s = mpd_stree_find (this, "width", index_as);
        item->video_width = info->w = str2uint32 (&s);
        s = mpd_stree_find (this, "height", index_as);
        item->video_height = info->h = str2uint32 (&s);
        s = mpd_stree_find (this, "SegmentTemplate.startNumber", index_as);
        info->frag_start = str2uint32 (&s);
        s = mpd_stree_find (this, "SegmentTemplate.duration", index_as);
        info->frag_duration = str2uint32 (&s);
        info->id = "";
        info->bitrate = 0;
        item->bitrate = 0;
        item->lang[0] = 0;

        for (representation = 0; this->num_streams < MPD_MAX_STREAMS; representation++) {
          uint32_t index_r;

          path_r[15] = representation < 10 ? ' ' : '0' + representation / 10;
          path_r[16] = '0' + representation % 10;
          index_r = xine_stree_find (this->tree, this->list_buf + 4, path_r, index_as, 0);
          if (!index_r)
            break;
          if (representation) {
            info[representation] = *info;
            item[representation] = *item;
          }
          info[representation].index_r = index_r;
          info[representation].id = mpd_stree_find (this, "id", index_r);
          s = mpd_stree_find (this, "bandwidth", index_r);
          item[representation].bitrate = info[representation].bitrate = str2uint32 (&s);
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_mpegdash: stream[%2u]: %s %ux%u %uHz %ubps.\n",
            (unsigned int)this->num_streams, info->mime,
            (unsigned int)info[representation].w, (unsigned int)info[representation].h,
            (unsigned int)info[representation].samplerate, (unsigned int)info[representation].bitrate);
          this->num_streams += 1;
        }

        if (!representation) {
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_mpegdash: stream[%2u]: %s %ux%u %uHz %ubps.\n",
            (unsigned int)this->num_streams, info->mime, (unsigned int)info->w, (unsigned int)info->h,
            (unsigned int)info->samplerate, (unsigned int)info->bitrate);
          this->num_streams += 1;
        }
      }
    }
  }
  return 1;
}

static uint32_t mpd_input_get_capabilities (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  if (MPD_IS_LIVE (this))
    return INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW | INPUT_CAP_LIVE;
  return INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW;
}

static off_t mpd_input_read (input_plugin_t *this_gen, void *buf, off_t len) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
#if 1
  return mpd_read_int (this, buf, len, 1);
#else
  uint8_t *b = (uint8_t *)buf;
  size_t left;
  mpd_frag_info_t *frag = this->current_frag;

  if (!b)
    return 0;
  if (len < 0)
    return 0;
  left = len;

  while (left > 0) {
    int reget = 0;
    {
      if (!frag)
        break;
      {
        ssize_t r;
        size_t fragleft = frag->byte_size - this->pos_in_frag;
        if (left < fragleft) {
          r = this->in1->read (this->in1, (void *)b, left);
          if (r > 0) {
            this->pos_in_frag += r;
            b += r;
          }
          break;
        }
        r = this->in1->read (this->in1, (void *)b, fragleft);
        if (r > 0) {
          this->pos_in_frag += r;
          left -= r;
          b += r;
        }
        if (r < (ssize_t)fragleft)
          break;
      }  
      {
        uint32_t n = frag - this->frags + 1;
        if (n >= this->frag_have) {
          if (!MPD_IS_LIVE (this))
            break;
          reget = 1;
        } else {
          if (!mpd_input_open_item (this, n))
            break;
          frag = this->current_frag;
        }
      }
    }
    if (reget) {
      uint32_t n;
      strcpy (this->item_mrl, this->list_mrl);
      if (!mpd_input_switch_mrl (this))
        break;
      if (mpd_input_load_manifest (this) != 1)
        break;
      if (!mpd_input_open_item (this, n))
        break;
      frag = this->current_frag;
    }
  }

  left = b - (uint8_t *)buf;
  this->live_pos += left;
  return left;
#endif
}

static buf_element_t *mpd_input_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {
  (void)this_gen;
  (void)fifo;
  (void)todo;
  return NULL;
}

static void mpd_input_frag_seek (mpd_input_plugin_t *this, uint32_t new_pos_in_frag) {
  if (this->caps1 & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE)) {
    int32_t newpos = this->in1->seek (this->in1, new_pos_in_frag, SEEK_SET);
    if (newpos < 0)
      newpos = this->in1->get_current_pos (this->in1);
    if (newpos >= 0)
      this->pos_in_frag = newpos;
  }
}

static off_t mpd_input_time_seek (input_plugin_t *this_gen, int time_offs, int origin) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  uint32_t new_time;
  mpd_frag_info_t *frag;

  if (MPD_IS_LIVE (this))
    return this->pos;

  frag = this->current_frag;
  if (!frag)
    return 0;

  switch (origin) {
    case SEEK_SET:
      new_time = 0;
      break;
    case SEEK_CUR:
      new_time = frag->start_msec + (frag[1].start_msec - frag[0].start_msec) * this->pos_in_frag / frag->byte_size;
      break;
    case SEEK_END:
      new_time = this->duration;
      break;
    default:
      errno = EINVAL;
      return (off_t)-1;
  }
  new_time += time_offs;
  if (new_time > this->duration) {
    errno = EINVAL;
    return (off_t)-1;
  }

  {
    /* find nearest fragment at or before requested time. */
    int32_t b = 0, e = this->frag_have, m;
    do {
      uint32_t t;
      m = (b + e) >> 1;
      t = this->frags[m].start_msec;
      if (new_time < t)
        e = m--;
      else
        b = m + 1;
    } while (b != e);
    if (m < 0)
      m = 0;
    if (this->frags + m == frag) {
      mpd_input_frag_seek (this, 0);
    } else {
      if (!mpd_input_open_item (this, m))
        return (off_t)-1;
      frag = this->current_frag;
    }
  }

  return frag->start_offs + this->pos_in_frag;
}

static off_t mpd_input_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  off_t new_offs;
  uint32_t new_pos_in_frag;
  mpd_frag_info_t *frag;

  if (MPD_IS_LIVE (this)) {
    char buf[2048];

    switch (origin) {
      case SEEK_SET:
        new_offs = offset;
        break;
      case SEEK_CUR:
        new_offs = this->pos + offset;
        break;
      default:
        return this->pos;
    }
    if ((this->pos <= (int)this->prev_size) && (new_offs >= 0) && (new_offs <= (int)this->prev_size)) {
      this->pos = new_offs;
      return this->pos;
    }
    new_offs -= this->pos;
    if (new_offs < 0)
      return this->pos;
    while (new_offs > 0) {
      size_t l = new_offs > (int)sizeof (buf) ? sizeof (buf) : (size_t)new_offs;
      ssize_t r = mpd_read_int (this, buf, l, 1);

      if (r <= 0)
        break;
      new_offs -= r;
    }
    return this->pos;
  }

  frag = this->current_frag;
  if (!frag)
    return 0;

  switch (origin) {
    case SEEK_SET:
      new_offs = 0;
      break;
    case SEEK_CUR:
      new_offs = frag->start_offs + this->pos_in_frag;
      break;
    case SEEK_END:
      new_offs = this->est_size;
      break;
    default:
      errno = EINVAL;
      return (off_t)-1;
  }
  new_offs += offset;
  if ((new_offs < 0) || (new_offs > this->est_size)) {
    errno = EINVAL;
    return (off_t)-1;
  }

  if ((new_offs < frag->start_offs) || (new_offs >= frag->start_offs + frag->byte_size)) {
    /* find nearest fragment at or before requested offs. */
    int32_t b = 0, e = this->frag_have, m;
    do {
      off_t t;
      m = (b + e) >> 1;
      t = this->frags[m].start_offs;
      if (new_offs < t)
        e = m--;
      else
        b = m + 1;
    } while (b != e);
    if (m < 0)
      m = 0;
    /* HACK: offsets around this fragment may be guessed ones,
     * and the fragment itself may turn out to be smaller than expected.
     * however, demux expects a seek to land at the exact byte offs.
     * lets try to meet that, even if it is still wrong. */
    do {
      if (!mpd_input_open_item (this, m))
        return (off_t)-1;
      m++;
      frag = this->current_frag;
      new_pos_in_frag = new_offs - frag->start_offs;
    } while (new_pos_in_frag >= (uint32_t)frag->byte_size);
  } else {
    new_pos_in_frag = new_offs - frag->start_offs;
  }

  mpd_input_frag_seek (this, new_pos_in_frag);

  return frag->start_offs + this->pos_in_frag;
}

static off_t mpd_input_get_current_pos (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  return this->pos;
}

static off_t mpd_input_get_length (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  return this->est_size;
}

static const char *mpd_input_get_mrl (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  return this->manifest_mrl;
}

static void mpd_input_dispose (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  if (this->nbc) {
    nbc_close (this->nbc);
    this->nbc = NULL;
  }
  if (this->in1) {
    _x_free_input_plugin (this->stream, this->in1);
    this->in1 = NULL;
  }
  xine_stree_delete (&this->tree);
  _x_freep (&this->list_buf);
  _x_freep (&this->frags);
  free (this);
}

static int mpd_input_open (input_plugin_t *this_gen) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;
  mpd_input_class_t  *cls  = (mpd_input_class_t *)this->input_plugin.input_class;
  int n;

  if (!mpd_input_load_manifest (this))
    return 0;

  n = multirate_autoselect (&cls->pref, this->items, this->num_streams);
  if (n < 0) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_mpegdash: no auto selected item.\n");
    return 0;
  }
  this->used_stream = n;
  this->info = this->streams[n];
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_mpegdash: auto selected stream #%d.\n", n);
  this->mode = this->time_url[0]
             ? (this->info.init[0] ? MPD_INIT_LIVE : mpd_strcasestr (this->info.media, "$Number$") ? MPD_LIVE : MPD_SINGLE_LIVE)
             : (this->info.init[0] ? MPD_INIT_VOD : mpd_strcasestr (this->info.media, "$Number$") ? MPD_VOD : MPD_SINGLE_VOD);
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_mpegdash: %s.\n", mpd_mode_names[this->mode]);

  this->frag_index = 0;
  this->prev_size = 0;
  this->pos = 0;
  n = mpd_read_int (this, this->preview, sizeof (this->preview), 0);
  if (n <= 0) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG, "input_mpegdash: failed to read preview.\n");
    return 0;
  }
  this->prev_size = n;
  this->pos = 0;
  /*
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_mpegdash: got %u fragments for %u.%03u seconds.\n", (unsigned int)this->frag_have,
    (unsigned int)(this->frags[this->frag_have].start_msec / 1000u),
    (unsigned int)(this->frags[this->frag_have].start_msec % 1000u));
  */

  return 1;
}

static int mpd_input_get_optional_data (input_plugin_t *this_gen, void *data, int data_type) {
  mpd_input_plugin_t *this = (mpd_input_plugin_t *)this_gen;

  if (!this)
    return INPUT_OPTIONAL_UNSUPPORTED;

  switch (data_type) {
    case INPUT_OPTIONAL_DATA_PREVIEW:
      if (!data || (this->prev_size <= 0))
        return INPUT_OPTIONAL_UNSUPPORTED;
      {
        uint32_t l = this->prev_size > MAX_PREVIEW_SIZE ? MAX_PREVIEW_SIZE : this->prev_size;
        memcpy (data, this->preview, l);
        return l;
      }
    case INPUT_OPTIONAL_DATA_SIZED_PREVIEW:
      if (!data || (this->prev_size <= 0))
        return INPUT_OPTIONAL_UNSUPPORTED;
      {
        int want;
        memcpy (&want, data, sizeof (want));
        want = want < 0 ? 0
             : want > (int)this->prev_size ? (int)this->prev_size
             : want;
        memcpy (data, this->preview, want);
        return want;
      }
    case INPUT_OPTIONAL_DATA_DURATION:
      if (!data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      memcpy (data, &this->duration, sizeof (this->duration));
      return INPUT_OPTIONAL_SUCCESS;
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
  this->size1        = 0;
  this->caps1        = 0;
  this->frags        = NULL;
  this->current_frag = NULL;
  this->list_buf     = NULL;
  this->list_bsize   = 0;
  this->live_pos     = 0;
  this->est_size     = 0;
  this->seen_size    = 0;
  this->seen_num     = 0;
  this->seen_avg     = 0;
  this->duration     = 0;
  this->items_num    = 0;
#endif

  this->stream = stream;
  this->in1    = in1;

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_mpegdash: %s.\n", mrl + n);

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

