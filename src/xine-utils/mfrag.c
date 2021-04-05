/*
 * Copyright (C) 2021 the xine project
 * Copyright (C) 2021 Torsten Jager <t.jager@gmx.de>
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
 * Xine media fragment list utility.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define LOG_MODULE "mfrag"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/mfrag.h>

#define MFRAG_STEP 256

typedef struct {
  int64_t t, p;  /** << start time, byte pos */
  uint32_t d, l; /** << duration or 0, byte length or 0 if unknoen */
} xine_mfrag_frag_t;

struct xine_mfrag_list_s {
  xine_mfrag_frag_t *frags;
  uint32_t refs;
  uint32_t have, used, known_nd, known_nl; /** << fragment counts */
  uint32_t dirty_from; /** << rebuild start times/byte pos from here when needed */
  uint32_t avg_d, avg_l;
  uint64_t known_d; /** << time sum */
  uint64_t known_l; /** << byte length sum */
};

static void _xine_mfrag_fix (xine_mfrag_list_t *list, uint32_t last) {
  /** rebuild on demand only, for speed, and forward only, to avoid mass confusion. */
  xine_mfrag_frag_t *frag, *end;
  uint64_t t, p;
  list->avg_d = list->known_nd ? list->known_d / list->known_nd : 0;
  list->avg_l = list->known_nl ? list->known_l / list->known_nl : 0;
  frag = list->frags + list->dirty_from;
  end = list->frags + last;
  if (frag == list->frags) {
    frag[0].t = frag[1].t = 0;
    frag[0].p = 0;
    frag[1].p = frag[0].l;
    frag += 1;
  }
  t = frag[0].t;
  p = frag[0].p;
  while (frag < end) {
    t += frag[0].d ? frag[0].d : list->avg_d;
    frag[1].t = t;
    p += frag[0].l ? frag[0].l : list->avg_l;
    frag[1].p = p;
    frag += 1;
  }
  list->dirty_from = last;
}

static void _xine_mfrag_test (xine_mfrag_list_t *list, uint32_t last) {
  if (list->dirty_from < last)
    _xine_mfrag_fix (list, last);
}

void xine_mfrag_list_open (xine_mfrag_list_t **plist) {
  xine_mfrag_list_t *list;
  xine_mfrag_frag_t *frag;

  if (!plist)
    return;
  list = *plist;
  if (list) {
    list->refs++;
    return;
  }
  list = malloc (sizeof (*list));
  if (!list)
    return;

  list->refs = 1;
  list->have = MFRAG_STEP - 2;
  list->used = list->known_nd = list->known_nl = 0;
  list->dirty_from = 0;
  list->avg_d = list->avg_l = 0;
  list->known_d = list->known_l = 0;

  frag = malloc ((list->have + 2) * sizeof (*frag));
  if (!frag) {
    free (list);
    return;
  }
  list->frags = frag;
  frag[0].d = 1;
  frag[0].l = 0;
  frag[0].t = frag[0].p = 0;
  frag[1].d = frag[1].l = 0;

  *plist = list;
}

int xine_mfrag_set_index_frag (xine_mfrag_list_t *list, xine_mfrag_index_t index, int64_t dur, off_t len) {
  xine_mfrag_frag_t *frag;
  uint32_t idx;

  if (!list)
    return 0;
  if (index < 0)
    return 0;

  if (index == 0) {
    if (dur >= 0)
      list->frags[0].d = dur;
    if ((len >= 0) && ((uint32_t)len != list->frags[0].l)) {
      list->frags[0].l = len;
      list->dirty_from = 0;
    }
    return 1;
  }

  idx = index;
  if (idx > list->used + 1)
    return 0;

  if (idx == list->used + 1) {
    if (list->used >= list->have) {
      frag = realloc (list->frags, (list->have + 2 + MFRAG_STEP) * sizeof (*frag));
      if (!frag)
        return 0;
      list->frags = frag;
      list->have += MFRAG_STEP;
    }
    list->used += 1;
    if (idx < list->dirty_from)
      list->dirty_from = idx;
    frag = list->frags + idx;
    if (dur >= 0) {
      frag[0].d = dur;
      list->known_d += dur;
      list->known_nd += 1;
    } else {
      frag[0].d = 0;
    }
    if (len >= 0) {
      frag[0].l = len;
      list->known_l += len;
      list->known_nl += 1;
    } else {
      frag[0].l = 0;
    }
    frag[1].d = frag[1].l = 0;
    return 1;
  }

  frag = list->frags + idx;
  if ((dur >= 0) && ((uint32_t)dur != frag[0].d)) {
    if (!frag[0].d) {
      list->known_nd += 1;
      list->known_d += dur;
    } else if ((uint32_t)dur == 0) {
      list->known_nd -= 1;
      list->known_d -= frag[0].d;
    } else {
      list->known_d -= frag[0].d;
      list->known_d += dur;
    }
    frag[0].d = dur;
    if (idx < list->dirty_from)
      list->dirty_from = idx;
  }
  if ((len >= 0) && ((uint32_t)len != frag[0].l)) {
    if (!frag[0].l) {
      list->known_nl += 1;
      list->known_l += len;
    } else if ((uint32_t)len == 0) {
      list->known_nl -= 1;
      list->known_l -= frag[0].l;
    } else {
      list->known_l -= frag[0].l;
      list->known_l += len;
    }
    frag[0].l = len;
    if (idx < list->dirty_from)
      list->dirty_from = idx;
  }
  return 1;
}

int32_t xine_mfrag_get_frag_count (xine_mfrag_list_t *list) {
  if (!list)
    return 0;
  return list->used;
}

xine_mfrag_index_t xine_mfrag_find_time (xine_mfrag_list_t *list, int64_t timepos) {
  uint32_t b, m, l, e;

  if (!list)
    return -1;

  _xine_mfrag_test (list, list->used + 1);
  b = l = 1;
  e = list->used + 2;
  m = (b + e) >> 1;
  while (m != l) {
    int64_t d = timepos - list->frags[m].t;
    if (d < 0)
      e = m;
    else
      b = m;
    l = m;
    m = (b + e) >> 1;
  }
  return b;
}

xine_mfrag_index_t xine_mfrag_find_pos (xine_mfrag_list_t *list, off_t offs) {
  uint32_t b, l, m, e;

  if (!list)
    return -1;

  _xine_mfrag_test (list, list->used + 1);
  b = l = list->frags[0].l ? 0 : 1;
  e = list->used + 2;
  m = (b + e) >> 1;
  while (m != l) {
    int64_t d = offs - list->frags[m].p;
    if (d < 0)
      e = m;
    else
      b = m;
    l = m;
    m = (b + e) >> 1;
  }
  return b;
}

int xine_mfrag_get_index_frag (xine_mfrag_list_t *list, xine_mfrag_index_t index, int64_t *dur, off_t *len) {
  xine_mfrag_frag_t *frag;
  uint32_t idx;

  if (!list)
    return 0;
  if (index < 0)
    return 0;
  idx = index;
  if (idx > list->have)
    return 0;
  frag = list->frags + idx;
  if (dur)
    *dur = frag[0].d;
  if (len)
    *len = frag[0].l;
  return 1;
}

int xine_mfrag_get_index_start (xine_mfrag_list_t *list, xine_mfrag_index_t index, int64_t *timepos, off_t *offs) {
  xine_mfrag_frag_t *frag;
  uint32_t idx;

  if (!list)
    return 0;
  if (!list->frags || (index < 0))
    return 0;
  idx = index;
  if (idx > list->have + 1)
    return 0;
  _xine_mfrag_test (list, idx);
  frag = list->frags + idx;
  if (timepos)
    *timepos = frag[0].t;
  if (offs)
    *offs = frag[0].p;
  return 1;
}
  
void xine_mfrag_list_close (xine_mfrag_list_t **plist) {
  if (plist && *plist) {
    xine_mfrag_list_t *list = *plist;
    if (--list->refs == 0) {
      free (list->frags);
      list->frags = NULL;
      free (list);
      *plist = NULL;
    }
  }
}

