/*
 * Copyright (C) 2018-2022 the xine project
 * Copyright (C) 2021-2022 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 * vaapi_egl.c, VAAPI -> OpenGL/EGL interop
 *
 */

#ifndef XINE_VAAPI_EGL_H_
#define XINE_VAAPI_EGL_H_

#include <xine.h>

struct xine_gl;
struct xine_glconv_t;
struct xine_va_display_t;

struct xine_glconv_t *_glconv_vaegl_init(xine_t *xine, struct xine_gl *gl, struct xine_va_display_t *);

#endif /* XINE_VAAPI_EGL_H_ */
