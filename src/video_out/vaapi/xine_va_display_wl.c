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
 * VAAPI display for Wayland
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_va_display_plugin.h"
#include "xine_vaapi.h"

#include <stdlib.h>

#include <xine.h> /* visual types */
#include <xine/xine_internal.h>

#include <va/va.h>
#include <va/va_wayland.h>

/*
 * xine module
 */

static void _module_dispose(xine_module_t *module)
{
  xine_va_display_plugin_t *p = (xine_va_display_plugin_t *)module;
  vaTerminate(p->display.va_display);
  free(p);
}

static xine_module_t *_get_instance(xine_module_class_t *class_gen, const void *data)
{
  const va_display_plugin_params_t *params = data;
  const xine_wayland_visual_t *vis_wl = params->visual;
  xine_va_display_plugin_t *p;
  VADisplay dpy = NULL;

  (void)class_gen;

  if (params->visual_type != XINE_VISUAL_TYPE_WAYLAND)
    return NULL;

  dpy = vaGetDisplayWl(vis_wl->display);
  if (!vaDisplayIsValid(dpy))
    return NULL;

  p = calloc(1, sizeof(*p));
  if (!p) {
    vaTerminate(dpy);
    return NULL;
  }

  p->xine               = params->xine;
  p->display.va_display = dpy;
  p->module.dispose     = _module_dispose;

  return &p->module;
}

/*
 * plugin
 */

static void *_init_class(xine_t *xine, const void *params)
{
  static const xine_module_class_t xine_class = {
    .get_instance  = _get_instance,
    .description   = N_("VA display provider (WAYLAND)"),
    .identifier    = "va_display_wayland",
    .dispose       = NULL,
  };

  (void)xine;
  (void)params;

  return (void *)&xine_class;
}

static const xine_module_info_t module_info = {
  .priority  = 10,
  .type      = "va_display_v1",
  .sub_type  = XINE_VISUAL_TYPE_WAYLAND,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_XINE_MODULE, 1, "va_display_wayland", XINE_VERSION_CODE, &module_info, _init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL },
};
