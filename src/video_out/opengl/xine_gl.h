/*
 * Copyright (C) 2018-2022 the xine project
 * Copyright (C) 2018-2021 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * xine_gl.h, Interface between OpenGL and native windowing system
 *
 * GL provider API, used in vo drivers
 *
 */

#ifndef XINE_GL_H_
#define XINE_GL_H_

#include <stdlib.h>
#include <string.h>

#include <xine.h>
#include <xine/sorted_array.h>

typedef struct xine_gl xine_gl_t;

struct xine_gl {
  int  (*make_current)     (xine_gl_t *);
  void (*release_current)  (xine_gl_t *);
  void (*swap_buffers)     (xine_gl_t *);

  /* resize is needed only with WAYLAND visual */
  void (*resize)           (xine_gl_t *, int width, int height);
  /* set_native_window is used only with X11 */
  void (*set_native_window)(xine_gl_t *, void *);

  void (*dispose)          (xine_gl_t **);

  void *(*get_proc_address)(xine_gl_t *, const char *);
  const char * (*query_extensions)(xine_gl_t *);

  /* EGL */
  void       * (*eglCreateImageKHR) (xine_gl_t *,
                                     unsigned /* EGLenum target */,
                                     void * /* EGLClientBuffer buffer */,
                                     const int32_t * /*const EGLint * attrib_list */);
  int          (*eglDestroyImageKHR) (xine_gl_t *, void *);
};

xine_gl_t *_x_load_gl(xine_t *xine, unsigned visual_type, const void *visual, unsigned flags);


static inline int _x_gl_has_extension(const char *extensions, const char * const ext) {
  if (extensions)
    while (*extensions) {
      const char *p = ext;
      while (*extensions == ' ') extensions++;
      while (*p && *p == *extensions) p++, extensions++;
      if (*p == 0 && (*extensions == 0 || *extensions == ' '))
        return 1;
      while (*extensions && *extensions != ' ')
        extensions++;
    }
  return 0;
}

/* flags */
#define XINE_GL_API_OPENGL     0x0001
#define XINE_GL_API_OPENGLES   0x0002

typedef struct {
  xine_sarray_t *list;
  unsigned char *buf;
} xine_gl_extensions_t;

static inline void xine_gl_extensions_unload (xine_gl_extensions_t *e) {
  xine_sarray_delete (e->list);
  free (e->buf);
  e->list = NULL;
  e->buf  = NULL;
}

static inline void xine_gl_extensions_load (xine_gl_extensions_t *e, const char *list) {
  size_t llen;
  unsigned char *p;

  e->list = NULL;
  e->buf  = NULL;
  if (!list)
    return;

  llen = strlen (list) + 1;
  e->buf = malloc (llen);
  /* TJ. I got 298 strings here :-) */
  e->list = xine_sarray_new (1024, (xine_sarray_comparator_t)strcmp);
  if (!e->list || !e->buf) {
    xine_gl_extensions_unload (e);
    return;
  }

  p = e->buf;
  memcpy (p, list, llen);
  while (*p) {
    unsigned char *q = p;
    while (*p > ' ')
      p++;
    if (*p)
      *p++ = 0;
    if (*q)
      xine_sarray_add (e->list, q);
  }
}

static inline int xine_gl_extensions_test (xine_gl_extensions_t *e, const char *name) {
  return xine_sarray_binary_search (e->list, (void *)name) >= 0; /** << will not be written to */
}

#endif /* XINE_GL_H_ */
