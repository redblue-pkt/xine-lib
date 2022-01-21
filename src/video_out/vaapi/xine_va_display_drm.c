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
 * VAAPI display for DRM
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_va_display_plugin.h"
#include "xine_vaapi.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <xine.h> /* visual types */
#include <xine/xine_internal.h>

#include <va/va.h>
#include <va/va_drm.h>

typedef struct {
  xine_va_display_plugin_t p;

  int drm_fd;

} xine_va_display_impl_t;

/*
 * xine module
 */

static void _module_dispose(xine_module_t *module)
{
  xine_va_display_impl_t *impl = xine_container_of(module, xine_va_display_impl_t, p.module);

  vaTerminate(impl->p.display.va_display);
  close(impl->drm_fd);
  impl->drm_fd = -1;
  free(impl);
}

static xine_module_t *_get_instance(xine_module_class_t *class_gen, const void *data)
{
  const va_display_plugin_params_t *params = data;
  xine_va_display_impl_t *impl;
  VADisplay dpy = NULL;
  static const char *default_drm_device[] = {
    "/dev/dri/renderD128",
    "/dev/dri/card0",
    "/dev/dri/renderD129",
    "/dev/dri/card1",
  };
  int drm_fd = -1;
  size_t i;

  (void)class_gen;

#if 0
  /* bail out if GLX requested */
  if (params->visual_type == XINE_VISUAL_TYPE_X11) {
    if (params->flags & XINE_VA_DISPLAY_GLX) {
      return NULL;
    }
  }
#endif

  /* accept all other visuals */

  for (i = 0; i < sizeof(default_drm_device)/sizeof(default_drm_device[0]); i++) {
    drm_fd = open(default_drm_device[i], O_RDWR);
    if (drm_fd >= 0) {
      dpy = vaGetDisplayDRM(drm_fd);
      if (vaDisplayIsValid(dpy))
        break;
      close(drm_fd);
      drm_fd = -1;
    }
  }

  if (!vaDisplayIsValid(dpy))
    return NULL;

  impl = calloc(1, sizeof(*impl));
  if (!impl) {
    vaTerminate(dpy);
    close(drm_fd);
  }

  impl->drm_fd               = drm_fd;
  impl->p.xine               = params->xine;
  impl->p.module.dispose     = _module_dispose;
  impl->p.display.va_display = dpy;
  return &impl->p.module;
}

/*
 * plugin
 */

static void *_init_class(xine_t *xine, const void *params)
{
  static const xine_module_class_t xine_class = {
    .get_instance  = _get_instance,
    .description   = N_("VA display provider (DRM)"),
    .identifier    = "va_display_drm",
    .dispose       = NULL,
  };

  (void)xine;
  (void)params;

  return (void *)&xine_class;
}

# if 0
/* Currently there's no no DRM support in video_out_vaapi.c */
static const xine_module_info_t module_info_x11 = {
  .priority  = 1,
  .type      = "va_display_v1",
  .sub_type  = XINE_VISUAL_TYPE_X11,
};
# endif

static const xine_module_info_t module_info_wl = {
  .priority  = 1,
  .type      = "va_display_v1",
  .sub_type  = XINE_VISUAL_TYPE_WAYLAND,
};
static const xine_module_info_t module_info_none = {
  .priority  = 1,
  .type      = "va_display_v1",
  .sub_type  = XINE_VISUAL_TYPE_NONE,
};
static const xine_module_info_t module_info_fb = {
  .priority  = 1,
  .type      = "va_display_v1",
  .sub_type  = XINE_VISUAL_TYPE_FB,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  //{ PLUGIN_XINE_MODULE, 1, "va_display_drm", XINE_VERSION_CODE, &module_info_x11, _init_class },
  { PLUGIN_XINE_MODULE, 1, "va_display_drm", XINE_VERSION_CODE, &module_info_wl,   _init_class },
  { PLUGIN_XINE_MODULE, 1, "va_display_drm", XINE_VERSION_CODE, &module_info_fb,   _init_class },
  { PLUGIN_XINE_MODULE, 1, "va_display_drm", XINE_VERSION_CODE, &module_info_none, _init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL },
};
