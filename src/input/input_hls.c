/*
 * Copyright (C) 2019 the xine project
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

#include "input_helper.h"
#include "group_network.h"
#include "multirate_pref.c"

typedef struct {
  input_class_t     input_class;
  xine_t           *xine;
  multirate_pref_t  pref;
} hls_input_class_t;

/* https://host:port/path1/path2/item.ext?foo1=bar1&foo2=bar2#display_info
   ^                ^           ^     ^  ^                   ^            ^
   0                p           l     e  a                   i            s */
typedef struct {
  char    *mrl;
  uint32_t path;
  uint32_t last;
  uint32_t ext;
  uint32_t args;
  uint32_t info;
  uint32_t stop;
} hls_mrl_t;

static void hls_mrl_split (hls_mrl_t *mrl) {
#define st 0x01 /* stop */
#define co 0x02 /* colon : */
#define sl 0x04 /* slash / */
#define dt 0x08 /* dot . */
#define qu 0x10 /* question mark ? */
#define ha 0x20 /* hash # */
  static const uint8_t flags[256] = {
    st,st,st,st,st,st,st,st,st,st,st,st,st,st,st,st,
    st,st,st,st,st,st,st,st,st,st,st,st,st,st,st,st,
     0, 0, 0,ha, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,dt,sl,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0,co, 0, 0, 0, 0,qu,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };
  const uint8_t *p = (const uint8_t *)mrl->mrl;
  uint8_t mask;
  uint32_t f;
  int32_t skip_host;

  skip_host = -1;
  mrl->path = mrl->last = 0;
  mrl->ext = mrl->args = mrl->info = ~0u;
  mask = st | co | sl | dt | qu | ha;
  while (1) {
    while (((f = flags[*p]) & mask) == 0)
      p++;
    if (f == st)
      break;
    if (f == co) {
      if (p[1] == '/')
        p++;
      mrl->path = p + 1 - (const uint8_t *)mrl->mrl;
      if (strncasecmp ((const char *)mrl->mrl, "file:", 5))
        skip_host = 1;
      mask = st | sl | dt | qu | ha;
    } else if (f == sl) {
      mrl->ext = ~0u;
      mrl->last = p - (const uint8_t *)mrl->mrl;
      if (skip_host >= 0) {
        skip_host--;
        if (skip_host < 0)
          mrl->path = mrl->last;
      }
    } else if (f == dt) {
      mrl->ext = p + 1 - (const uint8_t *)mrl->mrl;
    } else if (f == qu) {
      mrl->args = p - (const uint8_t *)mrl->mrl;
      mask = st | ha;
    } else { /* f == ha */
      mrl->info = p - (const uint8_t *)mrl->mrl;
      mask = st;
    }
    p++;
  }
  mrl->stop = p - (const uint8_t *)mrl->mrl;
  if (mrl->info == ~0u)
    mrl->info = mrl->stop;
  if (mrl->args == ~0u)
    mrl->args = mrl->info;
  if (mrl->ext == ~0u)
    mrl->ext = mrl->args;
#undef st
#undef co
#undef sl
#undef dt
#undef qu
#undef ha
}

static void hls_mrl_join (hls_mrl_t *base, hls_mrl_t *link, hls_mrl_t *res) {
  if (link->path > 0) {
    /* full link, just copy */
    *res = *link;
    return;
  }
  res->path = base->path;
  if (link->mrl[0] == '/') {
    /* same source, new absolute path */
    res->last = base->path + link->last;
    res->ext  = base->path + link->ext;
    res->args = base->path + link->args;
    res->info = base->path + link->info;
    if (res->info >= res->stop) {
      res->info = res->stop - 1;
      if (res->args >= res->stop) {
        res->args = res->stop - 1;
        if (res->ext >= res->stop) {
          res->info = res->stop - 1;
          if (res->last >= res->stop)
            res->last = res->stop - 1;
        }
      }
    }
    res->stop = res->info;
    memcpy (res->mrl, base->mrl, res->path);
    if (res->info > res->path)
      memcpy (res->mrl + res->path, link->mrl, res->info - res->path);
  } else {
    /* same source, relative path */
    uint32_t last;
    res->last = base->last + 1 + link->last;
    res->ext  = base->last + 1 + link->ext;
    res->args = base->last + 1 + link->args;
    res->info = base->last + 1 + link->info;
    if (res->info >= res->stop) {
      res->info = res->stop - 1;
      if (res->args >= res->stop) {
        res->args = res->stop - 1;
        if (res->ext >= res->stop) {
          res->info = res->stop - 1;
          if (res->last >= res->stop)
            res->last = res->stop - 1;
        }
      }
    }
    last = base->last >= res->stop ? res->stop - 1 : base->last;
    res->stop = res->info;
    memcpy (res->mrl, base->mrl, last);
    res->mrl[last] = '/';
    if (res->info > last + 1)
      memcpy (res->mrl + last + 1, link->mrl, res->info - last - 1);
  }
  res->mrl[res->info] = 0;
}

typedef struct {
  uint32_t mrl_offs;
  uint32_t start_msec;
  off_t    byte_size;
  off_t    start_offs;
} hls_frag_info_t;

typedef struct {
  input_plugin_t    input_plugin;
  xine_stream_t    *stream;
  hls_mrl_t         list_mrl, item_mrl;
  input_plugin_t   *in1;
  off_t             size1;
  hls_frag_info_t  *frags, *current_frag;
  char             *list_buf;
  uint32_t          frag_have;
  uint32_t          frag_max;
  off_t             est_size;
  off_t             seen_size;
  uint32_t          seen_num;
  uint32_t          seen_avg;
  uint32_t          duration;
  uint32_t          pos_in_frag;
  uint32_t          items_num;
  char             *items_mrl[20];
  multirate_pref_t  items[20];
#define HLS_MAX_MRL 4096
  char              item_mrl_buf[HLS_MAX_MRL];
} hls_input_plugin_t;

static int hls_input_switch_mrl (hls_input_plugin_t *this) {
  if (this->in1) {
    if (this->in1->get_capabilities (this->in1) & INPUT_CAP_NEW_MRL) {
      if (this->in1->get_optional_data (this->in1, this->item_mrl.mrl,
        INPUT_OPTIONAL_DATA_NEW_MRL) == INPUT_OPTIONAL_SUCCESS) {
        if (this->in1->open (this->in1) > 0)
          return 1;
      }
    }
    _x_free_input_plugin (this->stream, this->in1);
  }
  this->in1 = _x_find_input_plugin (this->stream, this->item_mrl.mrl);
  if (!this->in1)
    return 0;
  if (this->in1->open (this->in1) <= 0)
    return 0;
  return 1;
}

static int hls_input_open_item (hls_input_plugin_t *this, uint32_t n) {
  hls_mrl_t link;
  hls_frag_info_t *frag;
  /* valid index ? */
  if (n >= this->frag_have)
    return 0;
  /* get fragment mrl */
  link.mrl = this->list_buf + this->frags[n].mrl_offs;
  hls_mrl_split (&link);
  this->item_mrl.mrl = this->item_mrl_buf;
  this->item_mrl.stop = HLS_MAX_MRL;
  hls_mrl_join (&this->list_mrl, &link, &this->item_mrl);
  /* get input */
  if (!hls_input_switch_mrl (this))
    return 0;
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
  off_t size;
  char *line, *lend;
  uint32_t frag_start, frag_duration;

  _x_freep (&this->list_buf);
  _x_freep (&this->frags);
  this->frag_have = 0;
  this->frag_max  = 0;
  this->est_size  = 0;
  this->seen_size = 0;
  this->seen_num  = 0;
  this->seen_avg  = 0;
  this->items_num = 0;

  size = this->in1->get_length (this->in1);
  if (size > (32 << 20))
    return 0;
  if (size > 0) {
    /* size known, get at once. */
    if (this->in1->seek (this->in1, 0, SEEK_SET) != 0)
      return 0;
    this->list_buf = malloc (4 + size + 4);
    if (!this->list_buf)
      return 0;
    if (this->in1->read (this->in1, this->list_buf + 4, size) != size) {
      _x_freep (&this->list_buf);
      return 0;
    }
  } else {
    /* chunked/deflated */
    int32_t max = (32 << 10) - 4 - 4, have = 0;
    this->list_buf = malloc (4 + max + 4);
    if (!this->list_buf)
      return 0;
    while (1) {
      int32_t r = this->in1->read (this->in1, this->list_buf + 4 + have, max - have);
      if (r <= 0)
        break;
      if (have >= max) {
        char *n = NULL;
        if (max < (32 << 20))
          n = realloc (this->list_buf, 4 + max + (32 << 10) + 4);
        if (!n) {
          _x_freep (&this->list_buf);
          return 0;
        }
        this->list_buf = n;
        max += 32 << 10;
      }
      have += r;
    }
    size = have;
  }
  memset (this->list_buf, 0, 4);
  memset (this->list_buf + 4 + size, 0, 4);

  this->frags = malloc (256 * sizeof (this->frags[0]));
  if (!this->frags) {
    _x_freep (&this->list_buf);
    return 0;
  }
  this->frag_max = 256;

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
  uint32_t flags = 0;
  if (this->in1)
    flags = this->in1->get_capabilities (this->in1) 
          & (INPUT_CAP_SEEKABLE | INPUT_CAP_SLOW_SEEKABLE | INPUT_CAP_PREVIEW | INPUT_CAP_SIZED_PREVIEW);
  return INPUT_CAP_TIME_SEEKABLE | flags;
}

static off_t hls_input_read (input_plugin_t *this_gen, void *buf, off_t len) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  uint8_t *b = (uint8_t *)buf;
  hls_frag_info_t *frag = this->current_frag;

  if (!frag || !buf)
    return 0;

  while (len > 0) {
    uint32_t n;
    off_t l = frag->byte_size - this->pos_in_frag, d;

    if (len < l) {
      d = this->in1->read (this->in1, (void *)b, len);
      if (d > 0) {
        this->pos_in_frag += d;
        b += d;
      }
      break;
    }

    d = this->in1->read (this->in1, (void *)b, l);
    if (d > 0) {
      this->pos_in_frag += d;
      len -= d;
      b += d;
    }
    if (d < l)
      break;

    n = frag - this->frags + 1;
    if (n >= this->frag_have)
      break;
    if (!hls_input_open_item (this, n))
      break;
    frag = this->current_frag;
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
  uint32_t new_time;
  hls_frag_info_t *frag = this->current_frag;

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
      this->in1->seek (this->in1, 0, SEEK_SET);
      this->pos_in_frag = 0;
    } else {
      if (!hls_input_open_item (this, m))
        return (off_t)-1;
      frag = this->current_frag;
    }
  }

  return frag->start_offs;
}

static off_t hls_input_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  off_t new_offs;
  hls_frag_info_t *frag = this->current_frag;

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
      this->pos_in_frag = new_offs - frag->start_offs;
    } while (this->pos_in_frag >= (uint32_t)frag->byte_size);
  }

  this->in1->seek (this->in1, this->pos_in_frag, SEEK_SET);
  return new_offs;
}

static off_t hls_input_get_current_pos (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
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
  return this->list_mrl.mrl;
}

static void hls_input_dispose (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  if (this->in1) {
    _x_free_input_plugin (this->stream, this->in1);
    this->in1 = NULL;
  }
  _x_freep (&this->list_buf);
  _x_freep (&this->list_mrl.mrl);
  free (this);
}

static int hls_input_open (input_plugin_t *this_gen) {
  hls_input_plugin_t *this = (hls_input_plugin_t *)this_gen;
  hls_input_class_t  *cls  = (hls_input_class_t *)this->input_plugin.input_class;
  hls_mrl_t link;
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
    link.mrl = this->items_mrl[n];
    hls_mrl_split (&link);
    this->item_mrl.mrl = this->item_mrl_buf;
    this->item_mrl.stop = HLS_MAX_MRL;
    hls_mrl_join (&this->list_mrl, &link, &this->item_mrl);
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_hls: trying %s.\n", this->item_mrl.mrl);
    if (!hls_input_switch_mrl (this))
      return 0;
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
      if (in1->open (in1)) {
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
  this->frags        = NULL;
  this->current_frag = NULL;
  this->list_buf     = NULL;
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

  this->list_mrl.mrl = strdup (mrl + n);
  hls_mrl_split (&this->list_mrl);
  this->item_mrl.mrl = this->item_mrl_buf;
  this->item_mrl.stop = HLS_MAX_MRL;

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
