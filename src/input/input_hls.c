/*
 * Copyright (C) 2020-2021 the xine project
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
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define LOG_MODULE "input_hls"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/compat.h>
#include <xine/input_plugin.h>
#include <xine/mfrag.h>

#include "http_helper.h"
#include "input_helper.h"
#include "group_network.h"
#include "multirate_pref.c"

typedef struct {
  input_class_t     input_class;
  xine_t           *xine;
  multirate_pref_t  pref;
} hls_input_class_t;

typedef struct {
  input_plugin_t    input_plugin;
  xine_stream_t    *stream;
  xine_nbc_t       *nbc;
  input_plugin_t   *in1;
  uint32_t          caps1;
  struct {
    xine_mfrag_list_t *list;
    uint64_t          *input_offs; /** << bytes + 1, or 0 if unset */
    uint32_t          *mrl_offs; /** << offs into list_buf */
    off_t              pos;
    off_t              size;
    int64_t            pts;
    uint32_t           num;
    uint32_t           current; /** << 1..n or 0 (none) */
  }                 frag;
  off_t             pos;
  char             *list_buf;
  uint32_t          list_bsize;
  enum {
    LIST_VOD,
    LIST_LIVE_BUMP,
    LIST_LIVE_REGET
  }                 list_type;
  uint32_t          list_seq;
  uint32_t          items_num;
  uint32_t          prev_size1; /** << the actual preview bytes, for INPUT_OPTIONAL_DATA_[SIZED]_PREVIEW. */
  uint32_t          prev_size2; /** << for read (), 0 after leaving that range. */
  struct timespec   frag_dur;   /** << != 0 if fixed duration live frags */
  struct timespec   next_stop;  /** << live timeline emulation */
  int               rewind;     /** << seconds */
  const char       *items_mrl[20];
  multirate_pref_t  items[20];
  const char       *list_strtype;
  const char       *list_strseq;
#define HLS_MAX_MRL 4096
  char              list_mrl[HLS_MAX_MRL];
  char              item_mrl[HLS_MAX_MRL];
  char              prev_item_mrl[HLS_MAX_MRL];
  size_t            bump_pos;
  size_t            bump_size;
  uint32_t          bump_seq;
  char              pad1[4];
  char              bump1[HLS_MAX_MRL];
  char              pad2[4];
  char              bump2[HLS_MAX_MRL];
  char              preview[32 << 10];
} hls_input_plugin_t;

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


static int hls_get_duration (hls_input_plugin_t *this) {
  int64_t d = 0;
  if (!xine_mfrag_get_index_start (this->frag.list, this->frag.num + 1, &d, NULL))
    return 0;
  return d / 1000;
}

static off_t hls_get_size (hls_input_plugin_t *this) {
  int64_t s = 0;
  xine_mfrag_get_index_start (this->frag.list, this->frag.num + 1, NULL, &s);
  if (this->pos > s)
    s = this->pos;
  return s;
}

static uint32_t hls_frag_start (hls_input_plugin_t *this) {
  int64_t s1, s2;
  this->frag.pos = this->pos;
  /* known size */
  xine_mfrag_get_index_frag (this->frag.list, this->frag.current, NULL, &s1);
  /* seen size */
  s2 = this->in1->get_length (this->in1);
  /* subfragment? */
  if (this->frag.input_offs[this->frag.current - 1]) {
    this->frag.size = s1;
    if (s1 > 0)
      return s1;
    s2 -= this->frag.input_offs[this->frag.current - 1] - 1;
  }
  /* update size */
  this->frag.size = s2;
  if (s2 > 0) {
    if ((s1 > 0) && (s1 != s2)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_hls: WTF: fragment #%u changed size from %" PRId64 " to %" PRId64 " bytes!!\n",
        (unsigned int)this->frag.current, s1, s2);
    }
    xine_mfrag_set_index_frag (this->frag.list, this->frag.current, -1, s2);
    return s2;
  }
  return s2;
}

static void hls_frag_end (hls_input_plugin_t *this) {
  int64_t s;
  s = this->pos - this->frag.pos;
  xine_mfrag_set_index_frag (this->frag.list, this->frag.current, -1, s);
}

static int hls_bump_find (hls_input_plugin_t *this, const char *item1, const char *seq) {
  size_t len1, len2;
  uint8_t *p1, *ps, s;
  const uint8_t *p2;

  if (!seq)
    return 0;
  p2 = (const uint8_t *)seq;
  {
    uint32_t v = 0;
    uint8_t z;
    while ((z = *p2++ ^ '0') <= 9)
      v = 10u * v + z;
    this->bump_seq = v;
  }
  len2 = p2 - (const uint8_t *)seq;
  if (len2 == 0)
    return 0;

  len1 = strlcpy (this->bump1, item1, sizeof (this->bump1));
  p1 = (uint8_t *)this->bump1 + len1;
  if (len1 < len2)
    return 0;

  ps = p1 - len1 - 1;
  s = ps[0];
  ps[0] = p1[-1];
  while (1) {
    while (*--p1 != p2[-1]) ;
    p1++;
    if ((const uint8_t *)p1 <= ps)
      break;
    if (!memcmp (p1 - len1, seq, len1)) {
      this->bump_pos = p1 - (uint8_t *)this->bump1;
      ps[0] = s;
      return 1;
    }
  }
  return 0;
}
    
static int hls_bump_guess (hls_input_plugin_t *this, const char *item1, const char *item2) {
  size_t len1 = strlcpy (this->bump1, item1, sizeof (this->bump1));
  uint8_t *p1 = (uint8_t *)this->bump1 + len1;
  size_t len2 = strlcpy (this->bump2, item2, sizeof (this->bump2));
  uint8_t *p2 = (uint8_t *)this->bump2 + len2;
  this->bump_size = len1;
  while (1) {
    uint8_t *e1;
    /* find end of num string */
    this->pad1[3] = '0';
    this->pad2[3] = '0';
    while (((*--p1) ^ '0') > 9) ;
    e1 = p1;
    if (p1 < (uint8_t *)this->bump1)
      return 0;
    while (((*--p2) ^ '0') > 9) ;
    if (p2 < (uint8_t *)this->bump2)
      return 0;
    /* find start there of */
    this->pad1[3] = ' ';
    this->pad2[3] = ' ';
    while (((*--p1) ^ '0') <= 9) ;
    p1++;
    while (((*--p2) ^ '0') <= 9) ;
    p2++;
    /* evaluate nums */
    {
      uint32_t v1, v2;
      const uint8_t *q;
      uint8_t z;
      v1 = 0, q = p1;
      while ((z = *q++ ^ '0') <= 9)
        v1 = 10u * v1 + z;
      v2 = 0, q = p2;
      while ((z = *q++ ^ '0') <= 9)
        v2 = 10u * v2 + z;
      if (v2 == v1 + 1) {
        this->bump_seq = v1;
        this->bump_pos = e1 - (uint8_t *)this->bump1 + 1;
        return 1;
      }
    }
  }
  return 0;
}

static void hls_bump_inc (hls_input_plugin_t *this) {
  uint8_t *p1 = (uint8_t *)this->bump1 + this->bump_pos;
  this->pad1[3] = ' ';
  while (1) {
    uint8_t z = *--p1 ^ '0';
    if (z < 9)
      break;
    if (z > 9) {
      size_t l;
      l = this->bump_pos + 1;
      if (l > sizeof (this->bump1) - 1)
        l = sizeof (this->bump1) - 1;
      this->bump_pos = l;
      l = this->bump_size + 1;
      if (l > sizeof (this->bump1) - 1)
        l = sizeof (this->bump1) - 1;
      this->bump_size = l;
      p1++;
      l -= p1 - (uint8_t *)this->bump1;
      memmove (p1 + 1, p1, l);
      *p1 = '0';
      break;
    }
    *p1 = '0';
  }
  *p1 += 1;
  this->bump_seq += 1;
}

static int hls_input_switch_mrl (hls_input_plugin_t *this) {
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_hls: %s.\n", this->item_mrl);
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

static int hls_input_open_bump (hls_input_plugin_t *this) {
  /* bump mode */
  _x_merge_mrl (this->item_mrl, HLS_MAX_MRL, this->list_mrl, this->bump1);
  if (!hls_input_switch_mrl (this))
    return 0;
  this->caps1 = this->in1->get_capabilities (this->in1);
  hls_frag_start (this);
  return 1;
}

static int hls_input_open_item (hls_input_plugin_t *this, uint32_t n) {
  /* valid index ? */
  if ((n < 1) || (n > this->frag.num))
    return 0;
  strcpy (this->prev_item_mrl, this->item_mrl);
  /* get fragment mrl */
  _x_merge_mrl (this->item_mrl, HLS_MAX_MRL, this->list_mrl, this->list_buf + this->frag.mrl_offs[n - 1]);
  /* get input */
  if (strcmp (this->prev_item_mrl, this->item_mrl)) {
    this->caps1 = 0;
    if (!hls_input_switch_mrl (this))
      return 0;
  } else {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_hls: reuse %s for fragment #%u.\n", (const char *)this->item_mrl, (unsigned int)n);
  }
  this->caps1 = this->in1->get_capabilities (this->in1);
  /* input offset */
  do {
    int64_t old_pos = this->in1->get_current_pos (this->in1), new_pos;
    if (old_pos < 0)
      break;
    if (!this->frag.input_offs[n - 1])
      break;
    new_pos = this->frag.input_offs[n - 1] - 1;
    if (old_pos == new_pos)
      break;
    if (this->caps1 & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE)) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_hls: seek into fragment @ %" PRId64 ".\n", new_pos);
      old_pos = this->in1->seek (this->in1, new_pos, SEEK_SET);
      if (old_pos == new_pos)
        break;
    }
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
      "input_hls: sub input seek failed.\n");
  } while (0);
  this->frag.current = n;
  /* update size info */
  hls_frag_start (this);
  this->bump_seq = this->list_seq + n - 1;
  return 1;
}

static int hls_input_get_mrl_ext (const char *mrl, const char **ext) {
  const char *p1, *p2;
  for (p2 = mrl; *p2 && (*p2 != '?'); p2++) ;
  for (p1 = p2; (p1 > mrl) && (p1[-1] != '.'); p1--) ;
  *ext = p1;
  return p2 - p1;
}

static int hls_input_is_hls (const char *mrl) {
  const char *ext;
  int n = hls_input_get_mrl_ext (mrl, &ext);
  if ((n == 2) && !strncasecmp (ext, "ts", 2))
    return 1;
  if ((n == 3) && !strncasecmp (ext, "m2t", 3))
    return 1;
  if ((n == 4) && !strncasecmp (ext, "m3u8", 4))
    return 2;
  if ((n == 3) && !strncasecmp (ext, "hls", 3))
    return 2;
  return 0;
}

static const uint8_t hls_tab_char[256] = {
  ['\t'] = 1,
  [' ']  = 1,
  ['\r'] = 2,
  ['\n'] = 2,
  [0]    = 128
};

static void hls_skip_spc (char **s) {
  uint8_t *p = (uint8_t *)*s;
  while (hls_tab_char[*p] & 1)
    p++;
  *s = (char *)p;
}

static void hls_skip_newline_spc (char **s) {
  uint8_t *p = (uint8_t *)*s;
  while (hls_tab_char[*p] & (1 | 2))
    p++;
  *s = (char *)p;
}

static void hls_skip_line (char **s) {
  uint8_t *p = (uint8_t *)*s;
  while (!(hls_tab_char[*p] & (2 | 128)))
    p++;
  *s = (char *)p;
}

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

static uint64_t str2uint64 (char **s) {
  uint8_t *p = (uint8_t *)*s;
  uint64_t v = 0;
  uint8_t z;
  while ((z = *p ^ '0') < 10) {
    v = (v << 3) + (v << 1) + z;
    p++;
  }
  *s = (char *)p;
  return v;
}

static uint32_t str2usec (char **s) {
  uint8_t *p = (uint8_t *)*s;
  uint32_t v = 0;
  uint8_t z;
  while ((z = *p ^ '0') < 10) {
    v = v * 10u + z;
    p++;
  }
  v *= 1000000;
  do {
    if (z != ('.' ^ '0'))
      break;
    p++;
    if ((z = *p ^ '0') >= 10)
      break;
    v += 100000u * z;
    p++;
    if ((z = *p ^ '0') >= 10)
      break;
    v += 10000u * z;
    p++;
    if ((z = *p ^ '0') >= 10)
      break;
    v += 1000u * z;
    p++;
    if ((z = *p ^ '0') >= 10)
      break;
    v += 100u * z;
    p++;
    if ((z = *p ^ '0') >= 10)
      break;
    v += 10u * z;
    p++;
    if ((z = *p ^ '0') >= 10)
      break;
    v += z;
    p++;
  } while (0);
  *s = (char *)p;
  return v;
}

static int hls_input_load_list (hls_input_plugin_t *this) {
  ssize_t size;
  char *line, *lend;
  uint32_t frag_duration, fixed_duration;

  this->frag.mrl_offs = NULL;
  _x_freep (&this->frag.input_offs);
  xine_mfrag_list_close (&this->frag.list);
  this->frag.num = 0;
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

  this->list_seq = 1;
  this->list_strseq = "";
  this->list_strtype = "";

  fixed_duration = 0;
  frag_duration = 0;
  lend = this->list_buf + 4;

  if (strstr (lend, "#EXTINF:")) {
    uint32_t fragsize = ~0u;
    /* fragment list */
    {
      uint8_t *mem;
      uint32_t n = 0;
      while (1) {
        hls_skip_newline_spc (&lend);
        if (lend[0] == 0)
          break;
        if (lend[0] != '#')
          n++;
        hls_skip_line (&lend);
      }
      if (n == 0)
        return 0;
      mem = malloc ((n + 2) * (sizeof (*this->frag.input_offs) + sizeof (this->frag.mrl_offs)));
      if (!mem)
        return 0;
      this->frag.input_offs = (uint64_t *)mem;
      mem += (n + 2) * sizeof (*this->frag.input_offs);
      this->frag.mrl_offs = (uint32_t *)mem;
    }
    this->frag.mrl_offs[0] = 0;
    this->frag.input_offs[0]= 0;
    xine_mfrag_list_open (&this->frag.list);
    xine_mfrag_set_index_frag (this->frag.list, 0, 1000000, 0);
    lend = this->list_buf + 4;
    while (1) {
      size_t llen;
      /* find next line */
      hls_skip_newline_spc (&lend);
      if (lend[0] == 0)
        break;
      line = lend;
      hls_skip_line (&lend);
      llen = lend - line;
      if (lend[0] != 0)
        *lend++ = 0;
      if ((llen >=4) && !strncasecmp (line, "#EXT", 4)) {
        /* control tag */
        if ((llen > 8) && !strncasecmp (line + 4, "INF:", 4)) {
          line += 8;
          frag_duration = str2usec (&line);
          if (fixed_duration == 0)
            fixed_duration = frag_duration;
          else if (fixed_duration != frag_duration)
            fixed_duration = ~0u;
        } else if ((llen > 17) && !strncasecmp (line + 4, "-X-BYTERANGE:", 13)) {
          line += 17;
          hls_skip_spc (&line);
          fragsize = str2uint32 (&line);
          hls_skip_spc (&line);
          if (*line == '@') {
            line++;
            hls_skip_spc (&line);
            this->frag.input_offs[this->frag.num] = str2uint64 (&line) + 1;
          } else {
            this->frag.input_offs[this->frag.num] = 1;
          }
        } else if ((llen > 22) && !strncasecmp (line + 4, "-X-MEDIA-SEQUENCE:", 18)) {
          line += 22;
          hls_skip_spc (&line);
          this->list_strseq = line;
          if (line[0])
            this->list_seq = str2uint32 (&line);
        } else if ((llen > 21) && !strncasecmp (line + 4, "-X-PLAYLIST-TYPE:", 17)) {
          line += 21;
          hls_skip_spc (&line);
          this->list_strtype = line;
        }
      } else if ((llen >= 1) && (line[0] != '#')) {
        /* mrl */
        this->frag.mrl_offs[this->frag.num] = line - this->list_buf;
        this->frag.num += 1;
        this->frag.mrl_offs[this->frag.num] = 0;
        this->frag.input_offs[this->frag.num] = 0;
        xine_mfrag_set_index_frag (this->frag.list, this->frag.num, frag_duration, fragsize != ~0u ? (int64_t)fragsize : -1);
      }
    }
    if ((fixed_duration != 0) && (fixed_duration != ~0u)) {
      this->frag_dur.tv_sec = fixed_duration / 1000000;
      this->frag_dur.tv_nsec = (fixed_duration % 1000000) * 1000;
    }
    return 1;
  }

  if (strstr (lend, "#EXT-X-STREAM-INF:")) {
    uint32_t n = 0;
    /* item list */
    while (1) {
      size_t llen;
      /* find next line */
      while (((lend[0] & 0xe0) == 0) && (lend[0] != 0))
        lend++;
      if (lend[0] == 0)
        break;
      line = lend;
      while ((lend[0] & 0xe0) != 0)
        lend++;
      llen = lend - line;
      *lend++ = 0;
      if ((llen >=4) && !strncasecmp (line, "#EXT", 4)) {
        /* control tag */
        if ((llen > 8) && !strncasecmp (line + 4, "-X-STREAM-INF:", 14)) {
          if (n < sizeof (this->items_mrl) / sizeof (this->items_mrl[0])) {
            line += 18;
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
              "input_hls: item #%u: %s.\n", (unsigned int)n, line);
            this->items[n].bitrate = 0;
            this->items[n].video_width = 0;
            this->items[n].video_height = 0;
            this->items[n].lang[0] = 0;
            while (*line != 0) {
              char *tagend;
              lend[-1] = ',';
              tagend = line;
              while (*tagend != ',')
                tagend++;
              lend[-1] = 0;
              if (!strncasecmp (line, "BANDWIDTH=", 10)) {
                line += 10;
                this->items[n].bitrate = str2uint32 (&line);
              }
              if (!strncasecmp (line, "RESOLUTION=", 11)) {
                line += 11;
                this->items[n].video_width = str2uint32 (&line);
                if ((*line & 0xdf) == 'X') {
                  line += 1;
                  this->items[n].video_height = str2uint32 (&line);
                }
              }
              line = tagend;
              if (*line)
                line++;
            }
          }
        }
      } else if ((llen >= 1) && (line[0] != '#')) {
        /* mrl */
        if (n < sizeof (this->items_mrl) / sizeof (this->items_mrl[0]))
          this->items_mrl[n++] = line;
      }
    }
    this->items_num = n;
    return 2;
  }

  return 0;
}

static uint32_t hls_input_get_capabilities (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  uint32_t flags;
  if (!this)
    return 0;
  if (this->list_type == LIST_VOD) {
    if (this->in1)
      this->caps1 = this->in1->get_capabilities (this->in1);
    flags = (this->caps1 & INPUT_CAP_SEEKABLE)
          | INPUT_CAP_TIME_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE | INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW;
  } else {
    flags = INPUT_CAP_LIVE | INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW;
  }
  return flags;
}

static void hls_live_start (hls_input_plugin_t *this) {
  if (!this->in1 || (this->list_type == LIST_VOD))
    return;
  xine_gettime (&this->next_stop);
  this->frag.pts = xine_nbc_get_pos_pts (this->nbc);
}

static int hls_live_wait (hls_input_plugin_t *this) {
  struct timespec now = {0, 0};
  int64_t pts;
  int d;

  if (!this->in1 || (this->frag_dur.tv_sec == 0))
    return 1;

  pts = xine_nbc_get_pos_pts (this->nbc);

  if (this->next_stop.tv_sec == 0) {
    /* paranoia */
    xine_gettime (&this->next_stop);
    this->next_stop.tv_sec -= 2;
    this->frag.pts = pts;
  }

  d = pts - this->frag.pts;
  if ((d > 0) && (d < 100 * 900000)) {
    this->frag_dur.tv_sec = d / 90000;
    this->frag_dur.tv_nsec = (d % 90000) * (1000000000 / 90000);
  }
  this->frag.pts = pts;

  this->next_stop.tv_sec += this->frag_dur.tv_sec;
  this->next_stop.tv_nsec += this->frag_dur.tv_nsec;
  if (this->next_stop.tv_nsec >= 1000000000) {
    this->next_stop.tv_nsec -= 1000000000;
    this->next_stop.tv_sec += 1;
  }
  xine_gettime (&now);
  d = (this->next_stop.tv_sec - now.tv_sec) * 1000;
  d += ((int)this->next_stop.tv_nsec - (int)now.tv_nsec) / 1000000;
  if ((d <= 0) || (d >= 100000))
    return 1;
  this->caps1 = this->in1->get_capabilities (this->in1);
  if (this->caps1 & INPUT_CAP_NEW_MRL) {
    this->item_mrl[0] = 0;
    this->in1->get_optional_data (this->in1, this->item_mrl, INPUT_OPTIONAL_DATA_NEW_MRL);
  }
  if (_x_io_select (this->stream, -1, 0, d) != XIO_TIMEOUT)
    return 0;
  return 1;
}

static off_t hls_input_read (input_plugin_t *this_gen, void *buf, off_t len) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  uint8_t *b = (uint8_t *)buf;
  size_t left;

  if (!this || !b)
    return 0;
  if (len < 0)
    return 0;
  left = len;

  if (this->pos <= (off_t)this->prev_size2) {
    size_t l = this->prev_size2 - this->pos;
    if (l > 0) {
      if (l > left)
        l = left;
      memcpy (b, this->preview + this->pos, l);
      b += l;
      this->pos += l;
      left -= l;
    }
    if (left > 0)
      this->prev_size2 = 0;
  }

  while (left > 0) {
    int reget = 0;
    /* read, safe with unknown size. */
    if (this->frag.current == 0)
      break;
    {
      ssize_t r = 0;
      size_t fragleft = left;
      if (this->frag.size > 0) {
        int64_t fl2 = this->frag.pos + this->frag.size - this->pos;
        if (fl2 <= 0) {
          fragleft = 0;
        } else if ((int64_t)fragleft > fl2) {
          fragleft = fl2;
        }
      }
      left -= fragleft;
      while (fragleft > 0) {
        r = this->in1->read (this->in1, (void *)b, fragleft);
        if (r <= 0)
          break;
        this->pos += r;
        b += r;
        fragleft -= r;
      }
      if (fragleft <= 0)
        r = 0;
      left += fragleft;
      if (r <= 0) {
        if (r == 0) {
          /* EOF */
          hls_frag_end (this);
        } else {
          return -1;
        }
      }
    }
    if (left == 0)
      break;
    /* bump */
    if (this->list_type != LIST_LIVE_BUMP) {
      uint32_t n = this->frag.current + 1;
      if (n > this->frag.num + 1) {
        if (this->list_type != LIST_LIVE_REGET)
          break;
        reget = 1;
      } else {
        if (!hls_input_open_item (this, n))
          break;
      }
    } else {
      hls_bump_inc (this);
      this->frag.current += 1;
      if (!hls_live_wait (this))
        break;
      if (!hls_input_open_bump (this)) {
        this->list_type = LIST_LIVE_REGET;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "input_hls: LIVE bump error, falling back to reget mode.\n");
        reget = 1;
      }
    }
    if (reget) {
      int32_t n;
      strcpy (this->item_mrl, this->list_mrl);
      if (!hls_input_switch_mrl (this))
        break;
      if (hls_input_load_list (this) != 1)
        break;
      this->bump_seq += 1;
      n = this->bump_seq - this->list_seq;
      if ((n < 0) || (n >= (int32_t)this->frag.num)) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "input_hls: LIVE seq discontinuity %u -> %u.\n", (unsigned int)this->bump_seq, (unsigned int)this->list_seq);
        this->bump_seq = this->list_seq;
        n = 0;
      }
      if (!hls_input_open_item (this, n + 1))
        break;
    }
  }

  return b - (uint8_t *)buf;
}

static buf_element_t *hls_input_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {
  (void)this_gen;
  (void)fifo;
  (void)todo;
  return NULL;
}

static off_t hls_input_time_seek (input_plugin_t *this_gen, int time_offs, int origin) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  uint32_t new_time, d;
  int idx;

  if (!this)
    return 0;
  if (this->list_type != LIST_VOD)
    return this->pos;
  if (!this->frag.list)
    return this->pos;

  d = hls_get_duration (this);
  switch (origin) {
    case SEEK_SET:
      new_time = 0;
      break;
    case SEEK_CUR:
      {
        int64_t t1, t2, p1, p2;
        xine_mfrag_get_index_start (this->frag.list, this->frag.current, &t1, &p1);
        xine_mfrag_get_index_start (this->frag.list, this->frag.current + 1, &t2, &p2);
        t1 /= 1000;
        t2 /= 1000;
        new_time = p2 - p1;
        new_time = new_time
                 ? (uint32_t)t1 + (uint64_t)(t2 - t1) * (uint32_t)(this->pos - this->frag.pos) / new_time
                 : (uint32_t)t1;
      }
      break;
    case SEEK_END:
      new_time = d;
      break;
    default:
      errno = EINVAL;
      return (off_t)-1;
  }
  new_time += time_offs;
  if (new_time > d) {
    errno = EINVAL;
    return (off_t)-1;
  }

  idx = xine_mfrag_find_time (this->frag.list, new_time * (uint64_t)1000);
  if (idx < 1)
    return (off_t)-1;
  {
    int64_t p;
    xine_mfrag_get_index_start (this->frag.list, idx, NULL, &p);
    if ((idx == 1) && (this->frag.current == 1) && (this->pos <= (off_t)this->prev_size2) && (p <= (int64_t)this->prev_size2)) {
      this->pos = p;
    } else {
      this->frag.current = idx;
      this->pos = p;
      this->prev_size2 = 0;
      if (!hls_input_open_item (this, idx))
        return (off_t)-1;
    }
  }

  return this->pos;
}

static off_t hls_input_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  off_t new_offs, l;
  int idx;

  if (!this)
    return 0;

  l = hls_get_size (this);
  switch (origin) {
    case SEEK_SET:
      new_offs = 0;
      break;
    case SEEK_CUR:
      new_offs = this->pos;
      break;
    case SEEK_END:
      new_offs = l;
      break;
    default:
      errno = EINVAL;
      return (off_t)-1;
  }
  new_offs += offset;
  if (new_offs < 0) {
    errno = EINVAL;
    return (off_t)-1;
  }

  /* always seek within preview. */
  if ((new_offs <= (off_t)this->prev_size2) && (this->pos <= (off_t)this->prev_size2)) {
    this->pos = new_offs;
    return this->pos;
  }
  this->prev_size2 = 0;

  if (this->list_type != LIST_VOD)
    return this->pos;

  if (new_offs > l) {
    errno = EINVAL;
    return (off_t)-1;
  }

  idx = xine_mfrag_find_pos (this->frag.list, new_offs);
  if (idx < 1) {
    errno = EINVAL;
    return (off_t)-1;
  }

  if (((uint32_t)idx != this->frag.current) || (new_offs < this->pos)) {
    int64_t p1, p2;
    /* HACK: offsets around this fragment may be guessed ones,
     * and the fragment itself may turn out to be smaller than expected.
     * however, demux expects a seek to land at the exact byte offs.
     * lets try to meet that, even if it is still wrong. */
    xine_mfrag_get_index_start (this->frag.list, idx, NULL, &p1);
    this->pos = p1;
    if (!hls_input_open_item (this, idx))
      return (off_t)-1;
    xine_mfrag_get_index_start (this->frag.list, idx + 1, NULL, &p2);
    while (new_offs >= p2) {
      p1 = p2;
      this->pos = p1;
      idx += 1;
      if (!hls_input_open_item (this, idx))
        return (off_t)-1;
      xine_mfrag_get_index_start (this->frag.list, idx + 1, NULL, &p2);
    }
  }

  new_offs -= this->frag.pos;
  if (new_offs > 0) {
    off_t subpos = new_offs;
    if (this->frag.input_offs[this->frag.current - 1])
      subpos += this->frag.input_offs[this->frag.current - 1] - 1;
    if (this->in1->seek (this->in1, subpos, SEEK_SET) != subpos) {
      this->in1->seek (this->in1, 0, SEEK_SET);
    } else {
      this->pos += new_offs;
    }
  }

  return this->pos;
}

static off_t hls_input_get_current_pos (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  if (!this)
    return 0;
  return this->pos;
}

static off_t hls_input_get_length (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  if (!this)
    return 0;
  return hls_get_size (this);
}

static const char *hls_input_get_mrl (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  if (!this)
    return NULL;
  return this->list_mrl;
}

static void hls_input_dispose (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  if (!this)
    return;
  if (this->in1) {
    _x_free_input_plugin (this->stream, this->in1);
    this->in1 = NULL;
  }
  if (this->nbc) {
    xine_nbc_close (this->nbc);
    this->nbc = NULL;
  }
  xine_mfrag_list_close (&this->frag.list);
  _x_freep (&this->list_buf);
  this->frag.mrl_offs = NULL;
  _x_freep (&this->frag.input_offs);
  free (this);
}

static int hls_input_open (input_plugin_t *this_gen) {
  static const char * const type_names[3] = {
    [LIST_VOD] = "seekable VOD",
    [LIST_LIVE_BUMP] = "non seekable LIVE bump",
    [LIST_LIVE_REGET] = "non seekable LIVE reget"
  };
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  hls_input_class_t  *cls  = (hls_input_class_t *)this->input_plugin.input_class;
  int try;

  for (try = 8; try > 0; try--) {
    int n;

    n = hls_input_load_list (this);
    if (n == 1)
      break;
    if (n != 2)
      return 0;

    n = multirate_autoselect (&cls->pref, this->items, this->items_num);
    if (n < 0) {
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_hls: no auto selected item.\n");
      return 0;
    }
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_hls: auto selected item #%d.\n", n);
    _x_merge_mrl (this->item_mrl, HLS_MAX_MRL, this->list_mrl, this->items_mrl[n]);
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_hls: trying %s.\n", this->item_mrl);
    if (!hls_input_switch_mrl (this))
      return 0;
    strcpy (this->list_mrl, this->item_mrl);
  }

  if (try <= 0) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_hls: too many redirections, giving up.\n");
    return 0;
  }

  {
    unsigned int d = hls_get_duration (this);
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_hls: got %u fragments for %u.%03u seconds.\n", (unsigned int)this->frag.num, d / 1000u, d % 1000u);
  }

  if (!strncasecmp (this->list_strtype, "VOD", 3) || ((this->frag.num >= 8) && (this->list_seq == 1))) {
    this->list_type = LIST_VOD;
  } else {
    if ((this->frag.num > 1)
      && hls_bump_guess (this, this->list_buf + this->frag.mrl_offs[0], this->list_buf + this->frag.mrl_offs[1])) {
      this->list_type = LIST_LIVE_BUMP;
    } else if ((this->frag.num > 0)
      && hls_bump_find (this, this->list_buf + this->frag.mrl_offs[0], this->list_strseq)) {
      this->list_type = LIST_LIVE_BUMP;
    } else {
      this->list_type = LIST_LIVE_REGET;
    }
  }
  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_hls: %s mode @ seq %s.\n", type_names[this->list_type], this->list_strseq);

  if (this->list_type == LIST_LIVE_BUMP) {
    this->frag.current = 1;
    if (!hls_input_open_bump (this))
      return 0;
  } else {
    if (!hls_input_open_item (this, 1))
      return 0;
  }

  hls_live_start (this);
  try = hls_input_read (&this->input_plugin, this->preview, sizeof (this->preview));
  if (try > 0) {
    this->prev_size1 = this->prev_size2 = try;
    hls_input_seek (&this->input_plugin, 0, SEEK_SET);
  }

  return 1;
}

static int hls_input_get_optional_data (input_plugin_t *this_gen, void *data, int data_type) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;

  if (!this)
    return INPUT_OPTIONAL_UNSUPPORTED;

  switch (data_type) {

    case INPUT_OPTIONAL_DATA_PREVIEW:
      if (!data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      {
        uint32_t s = this->prev_size1;
        if (s > MAX_PREVIEW_SIZE)
          s = MAX_PREVIEW_SIZE;
        if (!s) {
          if (this->in1)
            return this->in1->get_optional_data (this->in1, data, data_type);
          else
            return INPUT_OPTIONAL_UNSUPPORTED;
        }
        memcpy (data, this->preview, s);
        return s;
      }

    case INPUT_OPTIONAL_DATA_SIZED_PREVIEW:
      if (!data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      {
        int s;
        memcpy (&s, data, sizeof (s));
        if (s < 0)
          return INPUT_OPTIONAL_UNSUPPORTED;
        if (s > (int)this->prev_size1)
          s = this->prev_size1;
        if (!s) {
          if (this->in1)
            return this->in1->get_optional_data (this->in1, data, data_type);
          else
            return INPUT_OPTIONAL_UNSUPPORTED;
        }
        memcpy (data, this->preview, s);
        return s;
      }

    case INPUT_OPTIONAL_DATA_DURATION:
      if (!data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      {
        int d = hls_get_duration (this);
        memcpy (data, &d, sizeof (d));
        return INPUT_OPTIONAL_SUCCESS;
      }

    case INPUT_OPTIONAL_DATA_FRAGLIST:
      if (!data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      {
        xine_mfrag_list_t *n = NULL;
        memcpy (data, this->list_type == LIST_VOD ? &this->frag.list : &n, sizeof (this->frag.list));
      }
      return INPUT_OPTIONAL_SUCCESS;

    default:
      return INPUT_OPTIONAL_UNSUPPORTED;
  }
}

static input_plugin_t *hls_input_get_instance (input_class_t *cls_gen, xine_stream_t *stream, const char *mrl) {

  hls_input_class_t  *cls = (hls_input_class_t *)cls_gen;
  hls_input_plugin_t *this;
  input_plugin_t     *in1;
  char                hbuf[8];
  int                 n;

  lprintf("hls_input_get_instance\n");

  do {
    n = 0;
    in1 = NULL;
    if (!strncasecmp (mrl, "hls:/", 5)) {
      n = 5;
      in1 = _x_find_input_plugin (stream, mrl + 5);
    } else if (hls_input_is_hls (mrl) == 2)
      in1 = _x_find_input_plugin (stream, mrl);
    if (in1) {
      if (in1->open (in1) > 0) {
        if (_x_demux_read_header (in1, hbuf, 8) == 8) {
          if (!strncmp (hbuf, "#EXTM3U", 7)) {
            this = calloc (1, sizeof (*this));
            if (this)
              break;
          }
        }
      }
      _x_free_input_plugin (stream, in1);
    }
    return NULL;
  } while (0);

#ifndef HAVE_ZERO_SAFE_MEM
  this->caps1        = 0;
  this->frag.list    = NULL;
  this->frag.input_offs = NULL;
  this->frag.size    = 0;
  this->frag.mrl_offs = NULL;
  this->frag.num     = 0;
  this->frag.current = 0;
  this->list_buf     = NULL;
  this->list_bsize   = 0;
  this->items_num    = 0;
  this->prev_size1   = 0;
  this->prev_size2   = 0;
  this->frag_dur.tv_sec  = 0;
  this->frag_dur.tv_nsec = 0;
  this->next_stop.tv_sec  = 0;
  this->next_stop.tv_nsec = 0;
  this->rewind       = 0;
  this->prev_item_mrl[0] = 0;
#endif

  this->stream = stream;
  this->in1    = in1;

  /* TJ. yes input_http already does this, but i want to test offline
   * with a file based service. */
  this->nbc    = xine_nbc_init (this->stream);

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_hls: %s.\n", mrl + n);

  strlcpy (this->list_mrl, mrl + n, HLS_MAX_MRL);

  this->input_plugin.open               = hls_input_open;
  this->input_plugin.get_capabilities   = hls_input_get_capabilities;
  this->input_plugin.read               = hls_input_read;
  this->input_plugin.read_block         = hls_input_read_block;
  this->input_plugin.seek               = hls_input_seek;
  this->input_plugin.seek_time          = hls_input_time_seek;
  this->input_plugin.get_current_pos    = hls_input_get_current_pos;
  this->input_plugin.get_length         = hls_input_get_length;
  this->input_plugin.get_blocksize      = _x_input_default_get_blocksize;
  this->input_plugin.get_mrl            = hls_input_get_mrl;
  this->input_plugin.get_optional_data  = hls_input_get_optional_data;
  this->input_plugin.dispose            = hls_input_dispose;
  this->input_plugin.input_class        = &cls->input_class;

  return &this->input_plugin;
}


/*
 * plugin class functions
 */

static void hls_input_class_dispose (input_class_t *this_gen) {
  hls_input_class_t *this = (hls_input_class_t *)this_gen;
  config_values_t   *config = this->xine->config;

  config->unregister_callbacks (config, NULL, NULL, this, sizeof (*this));

  free (this);
}

void *input_hls_init_class (xine_t *xine, const void *data) {

  hls_input_class_t *this;

  (void)data;
  this = calloc (1, sizeof (*this));
  if (!this)
    return NULL;

  this->xine = xine;
  multirate_pref_get (xine->config, &this->pref);

  this->input_class.get_instance       = hls_input_get_instance;
  this->input_class.identifier         = "hls";
  this->input_class.description        = N_("HTTP live streaming input plugin");
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = NULL;
  this->input_class.dispose            = hls_input_class_dispose;
  this->input_class.eject_media        = NULL;

  return this;
}
