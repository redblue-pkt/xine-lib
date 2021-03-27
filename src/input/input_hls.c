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
  uint32_t mrl_offs;
  uint32_t start_msec;
  off_t    byte_size;
  off_t    start_offs;
} hls_frag_info_t;

typedef struct {
  input_plugin_t    input_plugin;
  xine_stream_t    *stream;
  input_plugin_t   *in1;
  off_t             size1;
  uint32_t          caps1;
  hls_frag_info_t  *frags, *current_frag;
  char             *list_buf;
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
  enum {
    LIST_VOD,
    LIST_LIVE_BUMP,
    LIST_LIVE_REGET
  }                 list_type;
  uint32_t          list_seq;
  uint32_t          items_num;
  const char       *items_mrl[20];
  multirate_pref_t  items[20];
  const char       *list_strtype;
  const char       *list_strseq;
#define HLS_MAX_MRL 4096
  char              list_mrl[HLS_MAX_MRL];
  char              item_mrl[HLS_MAX_MRL];
  size_t            bump_pos;
  size_t            bump_size;
  uint32_t          bump_seq;
  char              pad1[4];
  char              bump1[HLS_MAX_MRL];
  char              pad2[4];
  char              bump2[HLS_MAX_MRL];
} hls_input_plugin_t;

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
  return 1;
}

static int hls_input_open_item (hls_input_plugin_t *this, uint32_t n) {
  hls_frag_info_t *frag;
  /* valid index ? */
  if (n >= this->frag_have)
    return 0;
  /* get fragment mrl */
  _x_merge_mrl (this->item_mrl, HLS_MAX_MRL, this->list_mrl, this->list_buf + this->frags[n].mrl_offs);
  /* get input */
  this->caps1 = 0;
  if (!hls_input_switch_mrl (this))
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
      "input_hls: WTF: fragment #%u changed size from %" PRId64 " to %" PRId64 " bytes!!\n",
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
  this->bump_seq = this->list_seq + n;
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

static int hls_input_load_list (hls_input_plugin_t *this) {
  ssize_t size;
  char *line, *lend;
  uint32_t frag_start, frag_duration;

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

  frag_start    = 0;
  frag_duration = 0;
  lend = this->list_buf + 4;

  if (strstr (lend, "#EXTINF:")) {
    /* fragment list */
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
        if ((llen > 8) && !strncasecmp (line + 4, "INF:", 4)) {
          line += 8;
          frag_duration = str2msec (&line);
        } else if ((llen > 22) && !strncasecmp (line + 4, "-X-MEDIA-SEQUENCE:", 18)) {
          for (line += 22; *line == ' '; line++) ;
          this->list_strseq = line;
          if (line[0])
            this->list_seq = str2uint32 (&line);
        } else if ((llen > 21) && !strncasecmp (line + 4, "-X-PLAYLIST-TYPE:", 17)) {
          for (line += 21; *line == ' '; line++) ;
          this->list_strtype = line;
        }
      } else if ((llen >= 1) && (line[0] != '#')) {
        hls_frag_info_t *frag;
        /* mrl */
        if (this->frag_have + 1 >= this->frag_max) {
          uint32_t nsize = this->frag_max + 256;
          frag = realloc (this->frags, nsize * sizeof (this->frags[0]));
          if (!frag)
            break;
          this->frags = frag;
          this->frag_max = nsize;
        }
        frag = this->frags + this->frag_have;
        frag->mrl_offs = line - this->list_buf;
        frag->start_msec = frag_start;
        frag_start += frag_duration;
        frag->byte_size = 0;
        frag->start_offs = 0;
        this->frag_have++;
      }
    }
    {
      hls_frag_info_t *frag = this->frags + this->frag_have;
      frag->start_msec = frag_start;
      frag->mrl_offs = 0;
      frag->byte_size = 0;
      frag->start_offs = 0;
    }
    this->duration = frag_start;
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
  uint32_t flags = this->caps1
    & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE | INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW);
  if (this->list_type == LIST_VOD) {
    flags |= INPUT_CAP_TIME_SEEKABLE;
  } else {
    flags &= ~(INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE);
    flags |= INPUT_CAP_LIVE;
  }
  return flags;
}

static off_t hls_input_read (input_plugin_t *this_gen, void *buf, off_t len) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  uint8_t *b = (uint8_t *)buf;
  size_t left;
  hls_frag_info_t *frag = this->current_frag;

  if (!b)
    return 0;
  if (len < 0)
    return 0;
  left = len;

  while (left > 0) {
    int reget = 0;
    if (this->list_type != LIST_LIVE_BUMP) {
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
          if (this->list_type != LIST_LIVE_REGET)
            break;
          reget = 1;
        } else {
          if (!hls_input_open_item (this, n))
            break;
          frag = this->current_frag;
        }
      }
    } else {
      ssize_t r = this->in1->read (this->in1, (void *)b, left);
      if (r < 0)
        break;
      left -= r;
      b += r;
      if (left > 0) {
        hls_bump_inc (this);
        if (!hls_input_open_bump (this)) {
          this->list_type = LIST_LIVE_REGET;
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
            "input_hls: LIVE bump error, falling back to reget mode.\n");
          reget = 1;
        }
      }
    }
    if (reget) {
      uint32_t n;
      strcpy (this->item_mrl, this->list_mrl);
      if (!hls_input_switch_mrl (this))
        break;
      if (hls_input_load_list (this) != 1)
        break;
      this->bump_seq += 1;
      if ((this->bump_seq >= this->list_seq) && (this->bump_seq < this->list_seq + this->frag_have)) {
        n = this->bump_seq - this->list_seq;
      } else {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "input_hls: LIVE seq discontinuity %u -> %u.\n", (unsigned int)this->bump_seq, (unsigned int)this->list_seq);
        this->bump_seq = this->list_seq;
        n = 0;
      }
      if (!hls_input_open_item (this, n))
        break;
      frag = this->current_frag;
    }
  }

  left = b - (uint8_t *)buf;
  this->live_pos += left;
  return left;
}

static buf_element_t *hls_input_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {
  (void)this_gen;
  (void)fifo;
  (void)todo;
  return NULL;
}

static void hls_input_frag_seek (hls_input_plugin_t *this, uint32_t new_pos_in_frag) {
  if (this->caps1 & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE)) {
    int32_t newpos = this->in1->seek (this->in1, new_pos_in_frag, SEEK_SET);
    if (newpos < 0)
      newpos = this->in1->get_current_pos (this->in1);
    if (newpos >= 0)
      this->pos_in_frag = newpos;
  }
}

static off_t hls_input_time_seek (input_plugin_t *this_gen, int time_offs, int origin) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  uint32_t new_time;
  hls_frag_info_t *frag;

  if (this->list_type != LIST_VOD)
    return this->live_pos;

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
      hls_input_frag_seek (this, 0);
    } else {
      if (!hls_input_open_item (this, m))
        return (off_t)-1;
      frag = this->current_frag;
    }
  }

  return frag->start_offs + this->pos_in_frag;
}

static off_t hls_input_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  off_t new_offs;
  uint32_t new_pos_in_frag;
  hls_frag_info_t *frag;

  if (this->list_type != LIST_VOD)
    return this->live_pos;

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
      if (!hls_input_open_item (this, m))
        return (off_t)-1;
      m++;
      frag = this->current_frag;
      new_pos_in_frag = new_offs - frag->start_offs;
    } while (new_pos_in_frag >= (uint32_t)frag->byte_size);
  } else {
    new_pos_in_frag = new_offs - frag->start_offs;
  }

  hls_input_frag_seek (this, new_pos_in_frag);

  return frag->start_offs + this->pos_in_frag;
}

static off_t hls_input_get_current_pos (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  if (this->list_type != LIST_VOD)
    return this->live_pos;
  if (!this->current_frag)
    return 0;
  return this->current_frag->start_offs + this->pos_in_frag;
}

static off_t hls_input_get_length (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  return this->est_size;
}

static const char *hls_input_get_mrl (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  return this->list_mrl;
}

static void hls_input_dispose (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  if (this->in1) {
    _x_free_input_plugin (this->stream, this->in1);
    this->in1 = NULL;
  }
  _x_freep (&this->list_buf);
  _x_freep (&this->frags);
  free (this);
}

static int hls_input_open (input_plugin_t *this_gen) {
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

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_hls: got %u fragments for %u.%03u seconds.\n", (unsigned int)this->frag_have,
    (unsigned int)(this->frags[this->frag_have].start_msec / 1000u),
    (unsigned int)(this->frags[this->frag_have].start_msec % 1000u));

  if (!strncasecmp (this->list_strtype, "VOD", 3) || ((this->frag_have >= 8) && (this->list_seq == 1))) {
    this->list_type = LIST_VOD;
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_hls: seekable VOD mode @ seq %s.\n", this->list_strseq);
  } else {
    if ((this->frag_have > 1)
      && hls_bump_guess (this, this->list_buf + this->frags[0].mrl_offs, this->list_buf + this->frags[1].mrl_offs)) {
      this->list_type = LIST_LIVE_BUMP;
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_hls: non seekable LIVE bump mode @ seq %s.\n", this->list_strseq);
    } else if ((this->frag_have > 0)
      && hls_bump_find (this, this->list_buf + this->frags[0].mrl_offs, this->list_strseq)) {
      this->list_type = LIST_LIVE_BUMP;
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_hls: non seekable LIVE bump mode @ seq %s.\n", this->list_strseq);
    } else {
      this->list_type = LIST_LIVE_REGET;
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_hls: non seekable LIVE reget mode @ seq %s.\n", this->list_strseq);
    }
  }

  if (this->list_type == LIST_LIVE_BUMP)
    return hls_input_open_bump (this);

  return hls_input_open_item (this, 0);
}

static int hls_input_get_optional_data (input_plugin_t *this_gen, void *data, int data_type) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;

  if (!this)
    return INPUT_OPTIONAL_UNSUPPORTED;

  switch (data_type) {
    case INPUT_OPTIONAL_DATA_PREVIEW:
    case INPUT_OPTIONAL_DATA_SIZED_PREVIEW:
      if (!this->in1)
        return INPUT_OPTIONAL_UNSUPPORTED;
      return this->in1->get_optional_data (this->in1, data, data_type);
    case INPUT_OPTIONAL_DATA_DURATION:
      if (!data)
        return INPUT_OPTIONAL_UNSUPPORTED;
      memcpy (data, &this->duration, sizeof (this->duration));
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

