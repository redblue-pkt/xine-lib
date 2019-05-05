/*
 * Copyright (C) 2018-2019 the xine project
 * Copyright (C) 2018-2019 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 * xine GL interface plugin:
 * interface between OpenGL and native windowing system (GLX/EGL/WGL/Wayland/...)
 *
 * GL plugin API, used in GL provider plugins
 */

#ifndef XINE_GL_PLUGIN_H_
#define XINE_GL_PLUGIN_H_

#include <sys/types.h>

#include <xine.h>
#include <xine/xine_module.h>

#include "xine_gl.h"

#define GL_PLUGIN_TYPE "gl_v1"

typedef struct xine_gl_plugin_s xine_gl_plugin_t;

struct xine_gl_plugin_s {
  xine_module_t module;
  xine_gl_t     gl;
  xine_t       *xine;
};

typedef struct {
  xine_t      *xine;
  unsigned     visual_type;
  const void  *visual;
  unsigned     flags;
} gl_plugin_params_t;

#endif /* XINE_GL_PLUGIN_H_ */
