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
 *
 * rewritten by Torsten Jager <t.jager@gmx.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <xine/attributes.h>
#include <xine/sorted_array.h>

#define MIN_CHUNK_SIZE 64

/* Array internal struct */
struct xine_sarray_s {
  void                    **chunk;
  size_t                    chunk_size;
  size_t                    size;
  xine_sarray_comparator_t  comparator;
  int                     (*find) (xine_sarray_t *sarray, void *key);
  unsigned int              mode;
  unsigned int              last_add[2];
  unsigned int              first_test;
  unsigned int              same_dir;
  void                     *default_chunk[1];
};

static int _xine_sarray_find_default (xine_sarray_t *sarray, void *key) {
  unsigned int b = 0, e = sarray->size, m = sarray->first_test;

  do {
    int d = sarray->comparator (key, sarray->chunk[m]);
    if (d == 0)
      return m; /* found */
    if (d < 0)
      e = m;
    else
      b = m + 1;
    m = (b + e) >> 1;
  } while (b != e);
  return ~m; /* not found */
}

static int _xine_sarray_find_first (xine_sarray_t *sarray, void *key) {
  unsigned int b = 0, e = sarray->size, m = sarray->first_test;
  int found = 0;

  do {
    int d = sarray->comparator (key, sarray->chunk[m]);
    if (d == 0) {
      found = 1;
      e = m;
    } else if (d < 0) {
      e = m;
    } else {
      b = m + 1;
    }
    m = (b + e) >> 1;
  } while (b != e);
  return found ? m : ~m;
}

static int _xine_sarray_find_last (xine_sarray_t *sarray, void *key) {
  unsigned int b = 0, e = sarray->size, m = sarray->first_test;
  int found = 0;

  do {
    int d = sarray->comparator (key, sarray->chunk[m]);
    if (d == 0) {
      found = 1;
      b = m + 1;
    } else if (d < 0) {
      e = m;
    } else {
      b = m + 1;
    }
    m = (b + e) >> 1;
  } while (b != e);
  return found ? m : ~m;
}

int xine_sarray_binary_search (xine_sarray_t *sarray, void *key) {
  if (!sarray)
    return ~0; /* not found */
  if (sarray->size == 0)
    return ~0; /* not found */
  sarray->first_test = sarray->size >> 1;
  return sarray->find (sarray, key);
}

/* Constructor */
xine_sarray_t *xine_sarray_new (size_t initial_size, xine_sarray_comparator_t comparator) {
  xine_sarray_t *new_sarray;

  if (initial_size == 0)
    initial_size = MIN_CHUNK_SIZE;
  new_sarray = malloc (sizeof (*new_sarray) + (initial_size - 1) * sizeof (void *));
  if (!new_sarray)
    return NULL;
  new_sarray->chunk_size = initial_size;
  new_sarray->comparator = comparator;
  new_sarray->find       = _xine_sarray_find_default;
  new_sarray->chunk      = &new_sarray->default_chunk[0];
  new_sarray->size       = 0;
  new_sarray->mode       = XINE_SARRAY_MODE_DEFAULT;
  new_sarray->last_add[0]= 0;
  new_sarray->last_add[1]= 0;
  new_sarray->same_dir   = 0;
  return new_sarray;
}

/* Destructor */
void xine_sarray_delete (xine_sarray_t *sarray) {
  if (sarray) {
    if (sarray->chunk != &sarray->default_chunk[0])
      free (sarray->chunk);
    free (sarray);
  }
}

size_t xine_sarray_size (const xine_sarray_t *sarray) {
  return sarray ? sarray->size : 0;
}

void xine_sarray_set_mode (xine_sarray_t *sarray, unsigned int mode) {
  if (sarray) {
    sarray->mode = mode;
    sarray->find = (mode & XINE_SARRAY_MODE_FIRST) ? _xine_sarray_find_first
                 : (mode & XINE_SARRAY_MODE_LAST)  ? _xine_sarray_find_last
                 : _xine_sarray_find_default;
  }
}

void *xine_sarray_get (xine_sarray_t *sarray, unsigned int position) {
  if (sarray) {
    if (position < sarray->size)
      return sarray->chunk[position];
  }
  return NULL;
}

void xine_sarray_clear (xine_sarray_t *sarray) {
  if (sarray) {
    sarray->size = 0;
    sarray->last_add[0] = 0;
    sarray->last_add[1] = 0;
    sarray->same_dir = 0;
  }
}

void xine_sarray_remove (xine_sarray_t *sarray, unsigned int position) {
  if (sarray) {
    if (position < sarray->size) {
      void **here = sarray->chunk + position;
      unsigned int u = sarray->size - position - 1;
      while (u--) {
        here[0] = here[1];
        here++;
      }
      sarray->size--;
      sarray->last_add[0] = 0;
      sarray->last_add[1] = 0;
      sarray->same_dir = 0;
    }
  }
}

int xine_sarray_remove_ptr (xine_sarray_t *sarray, void *ptr) {
  if (sarray) {
    int ret;
    void **here = sarray->chunk, **end = here + sarray->size;
    while (here < end) {
      if (*here == ptr)
        break;
      here++;
    }
    if (here >= end)
      return ~0;
    ret = here - sarray->chunk;
    end--;
    while (here < end) {
      here[0] = here[1];
      here++;
    }
    sarray->size--;
    sarray->last_add[0] = 0;
    sarray->last_add[1] = 0;
    sarray->same_dir = 0;
    return ret;
  }
  return ~0;
}

static void _xine_sarray_insert (xine_sarray_t *sarray, unsigned int pos, void *value) {
  if (sarray->size + 1 > sarray->chunk_size) {
    size_t new_size;
    void **new_chunk;

    new_size = 2 * (sarray->size + 1);
    if (new_size < MIN_CHUNK_SIZE)
      new_size = MIN_CHUNK_SIZE;
    if (sarray->chunk == &sarray->default_chunk[0]) {
      new_chunk = malloc (new_size * sizeof (void *));
      if (!new_chunk)
        return;
      memcpy (new_chunk, sarray->chunk, sarray->size * sizeof (void *));
    } else {
      new_chunk = realloc (sarray->chunk, new_size * sizeof (void *));
      if (!new_chunk)
        return;
    }
    sarray->chunk = new_chunk;
    sarray->chunk_size = new_size;
  }

  /* this database is often built from already sorted items. optimize for that. */
  {
    int d = ((int)sarray->last_add[1] - (int)sarray->last_add[0]) ^ ((int)sarray->last_add[0] - (int)pos);
    sarray->same_dir = d < 0 ? 0 : sarray->same_dir + 1;
  }
  sarray->last_add[1] = sarray->last_add[0];
  sarray->last_add[0] = pos;

  {
    unsigned int u = sarray->size - pos;
    if (!u) {
      sarray->chunk[sarray->size++] = value;
    } else {
      void **here = sarray->chunk + sarray->size;
      do {
        here[0] = here[-1];
        here--;
      } while (--u);
      here[0] = value;
      sarray->size++;
    }
  }
}

int xine_sarray_add (xine_sarray_t *sarray, void *value) {
  if (sarray) {
    unsigned int pos2;

    if (sarray->size == 0) {
      pos2 = 0;
    } else {
      int pos1;

      sarray->first_test = sarray->same_dir >= 2 ? sarray->last_add[0] : (sarray->size >> 1);
      pos1 = sarray->find (sarray, value);
      if (pos1 >= 0) {
        if (sarray->mode & XINE_SARRAY_MODE_UNIQUE)
          return ~pos1;
        pos2 = pos1;
      } else {
        pos2 = ~pos1;
      }
    }
    _xine_sarray_insert (sarray, pos2, value);
    return pos2;
  }
  return 0;
}
