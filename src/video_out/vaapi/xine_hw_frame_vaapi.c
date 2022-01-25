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
 * VAAPI HW frame plugin
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_hw_frame_plugin.h"

#include <stdlib.h>

#include <xine.h> /* visual types */
#include <xine/xine_internal.h>

#include "vaapi_util.h"
#include "vaapi_frame.h"
#include "vaapi_egl.h"

typedef struct {
  xine_hw_frame_plugin_t  plugin;
  vaapi_context_impl_t   *va_context;
  int                     guarded_render;
} va_hw_frame_plugin_t;

/*
 * xine module
 */

static void _module_dispose(xine_module_t *module)
{
  va_hw_frame_plugin_t *p = xine_container_of(module, va_hw_frame_plugin_t, plugin.module);
  _x_va_free(&p->va_context);
  free(p);
}

static mem_frame_t *_alloc_frame(xine_hwdec_t *api)
{
  va_hw_frame_plugin_t *p = xine_container_of(api, va_hw_frame_plugin_t, plugin.api);
  vaapi_frame_t *frame = _x_va_frame_alloc_frame(p->va_context, p->va_context->c.driver,
                                                 p->guarded_render);
  return &frame->mem_frame;
}

static xine_glconv_t *_opengl_interop(xine_hwdec_t *api, struct xine_gl *gl)
{
  va_hw_frame_plugin_t *p = xine_container_of(api, va_hw_frame_plugin_t, plugin.api);

  /* - may need plugins if ex. need to link against X11 */
  return _glconv_vaegl_init(p->plugin.xine, gl, p->va_context->va_display_plugin);
}

static xine_module_t *_get_instance(xine_module_class_t *class_gen, const void *data)
{
  const hw_frame_plugin_params_t *params = data;
  config_values_t      *config = params->xine->config;
  vaapi_context_impl_t *va_context;
  va_hw_frame_plugin_t *p;
  int enable, guarded_render;

  (void)class_gen;

  enable = config->register_bool(config, "video.output.enable_vaapi", 0,
    _("Enable VAAPI"),
    _("Enable VAAPI video decoding with any video output driver. "
      "When disabled, only vaapi video output driver uses VAAPI accelerated decoding. "
      "Currently only opengl2 video output driver supports this."),
    10, NULL, NULL);

  guarded_render = config->register_num(config, "video.output.vaapi_guarded_render", 1,
        _("vaapi: set vaapi_guarded_render to 0 ( no ) 1 ( yes )"),
        _("vaapi: set vaapi_guarded_render to 0 ( no ) 1 ( yes )"),
        10, NULL, NULL);

  if (!enable)
    return NULL;

  /* initialize vaapi */
  va_context = _x_va_new(params->xine, params->visual_type, params->visual, 0);
  if (!va_context)
    return NULL;
  va_context->c.driver = params->vo_driver;

  /* test it */
  if (_x_va_init(va_context, -1, 1920, 1080)) {
    _x_va_free(&va_context);
    return NULL;
  }
  _x_va_close(va_context);

  p = calloc(1, sizeof(*p));
  if (!p) {
    _x_va_free(&va_context);
    return NULL;
  }

  p->plugin.module.dispose          = _module_dispose;

  p->plugin.api.frame_format        = XINE_IMGFMT_VAAPI;
  p->plugin.api.driver_capabilities = VO_CAP_VAAPI;

  p->plugin.api.alloc_frame         = _alloc_frame;
  p->plugin.api.update_frame_format = _x_va_frame_update_frame_format;
  p->plugin.api.opengl_interop      = _opengl_interop;
  p->plugin.api.destroy             = NULL;  /* filled by hw_frame loader */

  p->va_context     = va_context;
  p->guarded_render = guarded_render;

  return &p->plugin.module;
}

/*
 * plugin
 */

static void *_init_class(xine_t *xine, const void *params)
{
  static const xine_module_class_t xine_class = {
    .get_instance  = _get_instance,
    .description   = N_("VAAPI frame provider"),
    .identifier    = "hw_frame_vaapi",
    .dispose       = NULL,
  };

  (void)xine;
  (void)params;

  return (void *)&xine_class;
}

static const xine_module_info_t module_info = {
  .priority  = 9,
  .type      = "hw_frame_v1",
  .sub_type  = 0,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_XINE_MODULE, 1, "hwframe_vaapi", XINE_VERSION_CODE, &module_info, _init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL },
};
