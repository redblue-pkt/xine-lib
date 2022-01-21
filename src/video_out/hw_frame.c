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
 * hw_frame.c, HW decoder loader
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>

#include <xine/video_out.h>
#include <xine/xine_internal.h>

#include "xine_hw_frame_plugin.h"

#define HW_FRAME_PLUGIN(_api) xine_container_of(_api, xine_hw_frame_plugin_t, api)

/* technically, we can't unload dynamic module from the module itself.
 * it would work now (nothing is unloaded), but there may be problems in
 * the future if plugin loader unloads the library in _x_free_module().
 */
static void default_hwdec_destroy(xine_hwdec_t **api)
{
  if (*api) {
    xine_hw_frame_plugin_t *plugin = HW_FRAME_PLUGIN(*api);
    xine_module_t *module = &plugin->module;
    *api = NULL;
    _x_free_module(plugin->xine, &module);
  }
}

/*
 * loader wrapper
 */

xine_hwdec_t *_x_hwdec_new(xine_t *xine, vo_driver_t *vo_driver,
                           unsigned visual_type, const void *visual,
                           unsigned flags)
{
  const hw_frame_plugin_params_t params = {
    .xine        = xine,
    .visual_type = visual_type,
    .visual      = visual,
    .flags       = flags,
    .vo_driver   = vo_driver,
  };
  xine_hw_frame_plugin_t *p;

  p = (xine_hw_frame_plugin_t *)_x_find_module(xine, HW_FRAME_PLUGIN_TYPE, NULL, 0, &params);
  if (p) {
    p->xine = xine;
    p->api.destroy = default_hwdec_destroy;
    return &p->api;
  }

  return NULL;
}
