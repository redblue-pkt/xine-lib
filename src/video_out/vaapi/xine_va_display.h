/*
 * Copyright (C) 2022 the xine project
 * Copyright (C) 2022 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 * xine_va_display.h: VAAPI display (plugin loader)
 *
 */

#ifndef XINE_VAAPI_H_
#define XINE_VAAPI_H_

#include <xine.h>

typedef struct xine_va_display_t xine_va_display_t;

struct xine_va_display_t {
  void *va_display;

  void (*dispose)          (xine_va_display_t **);
};

/* flags */
#define XINE_VA_DISPLAY_GLX    0x0001

xine_va_display_t *_x_va_display_open(xine_t *xine, unsigned visual_type, const void *visual, unsigned flags);

#endif /* XINE_VAAPI_H_ */
