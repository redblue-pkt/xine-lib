/*
 * Copyright (C) 2018-2022 the xine project
 * Copyright (C) 2018-2022 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 * xine_vaapi.c, VAAPI loader
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_vaapi.h"
#include "xine_va_display_plugin.h"

#include <stddef.h>

#include <xine/xine_internal.h>

#define DISPLAY_PLUGIN(_dpy) xine_container_of(_dpy, xine_va_display_plugin_t, display)

/* technically, we can't unload dynamic module from the module itself.
 * it would work now (nothing is unloaded), but there may be problems in
 * the future if plugin loader unloads the library in _x_free_module().
 */
static void default_va_display_dispose(xine_va_display_t **dpy)
{
  if (*dpy) {
    xine_va_display_plugin_t *plugin = DISPLAY_PLUGIN(*dpy);
    xine_module_t *module = &plugin->module;
    *dpy = NULL;
    _x_free_module(plugin->xine, &module);
  }
}


/*
 * loader wrapper
 */

xine_va_display_t *_x_va_display_open(xine_t *xine, unsigned visual_type, const void *visual, unsigned flags)
{
  const va_display_plugin_params_t params = {
    .xine        = xine,
    .visual_type = visual_type,
    .visual      = visual,
    .flags       = flags,
  };
  xine_va_display_plugin_t *p;

  p = (xine_va_display_plugin_t*)_x_find_module(xine, VA_DISPLAY_PLUGIN_TYPE, NULL, visual_type, &params);
  if (p) {
    p->display.dispose = default_va_display_dispose;
    return &p->display;
  }
  return NULL;
}


