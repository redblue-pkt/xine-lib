/*
 * Copyright (C) 2018-2021 the xine project
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
 * xine_egl.c, EGL bindings for OpenGL video output
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_gl.h"

#include <stdlib.h>

#include <xine.h> /* visual types */
#include <xine/xine_internal.h>

#include "xine_gl_plugin.h"

#if defined(XINE_EGL_USE_X11)
#elif defined(XINE_EGL_USE_WAYLAND)
#  include <wayland-egl.h>
#else
#  error EGL platform undefined
#endif

#include <EGL/egl.h>
#include <EGL/eglext.h>

#define EGL(_gl) xine_container_of(_gl, xine_egl_t, p.gl)

typedef struct {
  xine_gl_plugin_t p;

  EGLDisplay display;
  EGLContext context;
  EGLSurface surface;
  EGLConfig  config;

#if defined(XINE_EGL_USE_WAYLAND)
  struct wl_egl_window *window;
  int width, height;
#endif

#ifdef EGL_KHR_image
  PFNEGLCREATEIMAGEKHRPROC    eglCreateImageKHR;
  PFNEGLDESTROYIMAGEKHRPROC   eglDestroyImageKHR;
#endif

  /* DEBUG */
  int         is_current;
} xine_egl_t;

static const char *_egl_error_str(EGLint error)
{
  switch (error) {
    case EGL_SUCCESS:             return "No error";
    case EGL_NOT_INITIALIZED:     return "EGL not initialized or failed to initialize";
    case EGL_BAD_ACCESS:          return "Resource inaccessible";
    case EGL_BAD_ALLOC:           return "Cannot allocate resources";
    case EGL_BAD_ATTRIBUTE:       return "Unrecognized attribute or attribute value";
    case EGL_BAD_CONTEXT:         return "Invalid EGL context";
    case EGL_BAD_CONFIG:          return "Invalid EGL frame buffer configuration";
    case EGL_BAD_CURRENT_SURFACE: return "Current surface is no longer valid";
    case EGL_BAD_DISPLAY:         return "Invalid EGL display";
    case EGL_BAD_SURFACE:         return "Invalid surface";
    case EGL_BAD_MATCH:           return "Inconsistent arguments";
    case EGL_BAD_PARAMETER:       return "Invalid argument";
    case EGL_BAD_NATIVE_PIXMAP:   return "Invalid native pixmap";
    case EGL_BAD_NATIVE_WINDOW:   return "Invalid native window";
    case EGL_CONTEXT_LOST:        return "Context lost";
  }
  return "Unknown error ";
}

static inline void _egl_log_error(xine_t *xine, const char *msg)
{
  EGLint error = eglGetError();
  xprintf(xine, XINE_VERBOSITY_LOG, "egl: %s : %s (%d)\n",
          msg, _egl_error_str(error), error);
}

static int _egl_make_current(xine_gl_t *gl)
{
  xine_egl_t *egl = EGL(gl);
  int result;

  _x_assert(!egl->is_current);

  result = eglMakeCurrent(egl->display, egl->surface, egl->surface, egl->context);
  if (!result) {
    _egl_log_error(egl->p.xine, "eglMakeCurrent() failed");
    return 0;
  }

  egl->is_current = 1;
  return result;
}

static void _egl_release_current(xine_gl_t *gl)
{
  xine_egl_t *egl = EGL(gl);

  _x_assert(egl->is_current);

  eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  egl->is_current = 0;
}

static void _egl_swap_buffers(xine_gl_t *gl)
{
  xine_egl_t *egl = EGL(gl);

  eglSwapBuffers(egl->display, egl->surface);
}

static void _egl_set_native_window(xine_gl_t *gl, void *drawable)
{
  xine_egl_t *egl = EGL(gl);
  EGLNativeWindowType window;

  _x_assert(!egl->is_current);

  eglDestroySurface (egl->display, egl->surface);
#if defined(XINE_EGL_USE_X11)
  window = (intptr_t)drawable;
#elif defined(XINE_EGL_USE_WAYLAND)
  wl_egl_window_destroy(egl->window);
  window = egl->window = wl_egl_window_create(drawable, egl->width, egl->height);
#endif
  egl->surface = eglCreateWindowSurface(egl->display, egl->config, window, NULL);

  if (egl->surface == EGL_NO_SURFACE) {
    _egl_log_error(egl->p.xine, "eglCreateWindowSurface() failed");
  }
}

static void _egl_resize(xine_gl_t *gl, int w, int h)
{
#if defined(XINE_EGL_USE_WAYLAND)
  xine_egl_t *egl = EGL(gl);
  wl_egl_window_resize(egl->window, w, h, 0, 0);
  egl->width = w;
  egl->height = h;
#else
  (void)gl;
  (void)w;
  (void)h;
#endif
}

static const char *_egl_query_string(xine_gl_t *gl, int type)
{
  xine_egl_t *egl = EGL(gl);

  return eglQueryString(egl->display, type);
}

static const char *_egl_query_extensions(xine_gl_t *gl)
{
  return _egl_query_string(gl, EGL_EXTENSIONS);
}

static void *_egl_get_proc_address(xine_gl_t *gl, const char *procname)
{
  (void)gl;
  return (void *)eglGetProcAddress(procname);
}

#ifdef EGL_KHR_image
static void *_egl_create_image_khr(xine_gl_t *gl, unsigned target, void *buffer, const int32_t *attrib_list)
{
  xine_egl_t *egl = EGL(gl);
  void *img;

  img = egl->eglCreateImageKHR(egl->display, EGL_NO_CONTEXT, target, buffer, attrib_list);

  if (!img) {
    _egl_log_error(egl->p.xine, "eglCreateImageKHR");
  }

  return img;
}

static int _egl_destroy_image_khr(xine_gl_t *gl, void *image)
{
  xine_egl_t *egl = EGL(gl);

  return egl->eglDestroyImageKHR(egl->display, image);
}
#endif /* EGL_KHR_image */

static int _egl_init(xine_egl_t *egl, EGLNativeDisplayType native_display, int api)
{
  static const EGLint attributes[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_DEPTH_SIZE, 16,
    EGL_NONE
  };
  static const EGLint context_attribs[] = {
    //EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };
  EGLint num_config;

  egl->display = eglGetDisplay(native_display);
  if (egl->display == EGL_NO_DISPLAY) {
    _egl_log_error(egl->p.xine, "eglGetDisplay() failed");
    return 0;
  }

  if (!eglInitialize (egl->display, NULL, NULL)) {
    _egl_log_error(egl->p.xine, "eglInitialize() failed");
    goto fail;
  }

  eglChooseConfig(egl->display, attributes, &egl->config, 1, &num_config);

  if (!eglBindAPI (api)) {
    _egl_log_error(egl->p.xine, "OpenGL API unavailable");
    goto fail;
  }

  egl->context = eglCreateContext(egl->display, egl->config, EGL_NO_CONTEXT, context_attribs);
  if (egl->context == EGL_NO_CONTEXT) {
    _egl_log_error(egl->p.xine, "eglCreateContext() failed");
    goto fail;
  }

  return 1;

 fail:
  eglTerminate(egl->display);
  return 0;
}

static void _egl_dispose(xine_gl_t *gl)
{
  xine_egl_t *egl = EGL(gl);

  lprintf("Destroying egl context %p\n", (void*)egl->context);

  _x_assert(!egl->is_current);

  if (egl->is_current) {
    eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  }

  eglDestroySurface (egl->display, egl->surface);
#if defined(XINE_EGL_USE_WAYLAND)
  wl_egl_window_destroy(egl->window);
#endif
  eglDestroyContext(egl->display, egl->context);
  eglTerminate(egl->display);
  free(egl);
}

/*
 * xine module
 */

static void _module_dispose(xine_module_t *module)
{
  xine_egl_t *egl = xine_container_of(module, xine_egl_t, p.module);

  _egl_dispose(&egl->p.gl);
}

static xine_module_t *_egl_get_instance(xine_module_class_t *class_gen, const void *data)
{
  const gl_plugin_params_t *params = data;
  EGLNativeWindowType native_window;
  xine_egl_t *egl;

#if defined(XINE_EGL_USE_X11)
  const x11_visual_t *vis = params->visual;
  _x_assert(params->visual_type == XINE_VISUAL_TYPE_X11 ||
            params->visual_type == XINE_VISUAL_TYPE_X11_2);
#elif defined(XINE_EGL_USE_WAYLAND)
  const xine_wayland_visual_t *vis = params->visual;
  _x_assert (params->visual_type == XINE_VISUAL_TYPE_WAYLAND);
#endif

  (void)class_gen;

  if (!(params->flags & (XINE_GL_API_OPENGL | XINE_GL_API_OPENGLES))) {
    return NULL;
  }
  _x_assert(params->visual);
  _x_assert(vis->display);

  egl = calloc(1, sizeof(*egl));
  if (!egl) {
    return NULL;
  }

  egl->p.module.dispose       = _module_dispose;

  egl->p.gl.make_current      = _egl_make_current;
  egl->p.gl.release_current   = _egl_release_current;
  egl->p.gl.swap_buffers      = _egl_swap_buffers;
  egl->p.gl.resize            = _egl_resize;
  egl->p.gl.set_native_window = _egl_set_native_window;
  egl->p.gl.dispose           = NULL;

  egl->p.gl.query_extensions  = _egl_query_extensions;
  egl->p.gl.get_proc_address  = _egl_get_proc_address;

#ifdef EGL_KHR_image
  egl->eglCreateImageKHR  = (void *)eglGetProcAddress("eglCreateImageKHR");
  egl->eglDestroyImageKHR = (void *)eglGetProcAddress("eglDestroyImageKHR");
  if (egl->eglCreateImageKHR && egl->eglDestroyImageKHR) {
    egl->p.gl.eglCreateImageKHR  = _egl_create_image_khr;
    egl->p.gl.eglDestroyImageKHR = _egl_destroy_image_khr;
  }
#endif /* EGL_KHR_image */

  egl->p.xine = params->xine;

  do {
    if (params->flags & XINE_GL_API_OPENGL)
      if (_egl_init(egl, vis->display, EGL_OPENGL_API))
        break;

    if (params->flags & XINE_GL_API_OPENGLES)
      if (_egl_init(egl, vis->display, EGL_OPENGL_ES_API))
        break;

    free(egl);
    return NULL;
  } while (0);

#if defined(XINE_EGL_USE_X11)
  native_window       = vis->d;
#elif defined(XINE_EGL_USE_WAYLAND)
  egl->width = 720;
  egl->height = 576;
  native_window = wl_egl_window_create(vis->surface, egl->width, egl->height);
  egl->window = native_window;
  egl->surface = vis->surface;
#endif

  egl->surface = eglCreateWindowSurface(egl->display, egl->config, native_window, NULL);
  if (egl->surface == EGL_NO_SURFACE) {
    _egl_log_error(egl->p.xine, "eglCreateWindowSurface() failed");
    goto fail;
  }

  return &egl->p.module;

 fail:
  eglDestroyContext(egl->display, egl->context);
  eglTerminate(egl->display);
  free(egl);
  return NULL;
}

/*
 * plugin
 */

static void *egl_init_class(xine_t *xine, const void *params)
{
  static const xine_module_class_t xine_egl_class = {
    .get_instance  = _egl_get_instance,
#if defined(XINE_EGL_USE_X11)
    .description   = "GL provider (EGL/X11)",
#elif defined(XINE_EGL_USE_WAYLAND)
    .description   = "GL provider (EGL/Wayland)",
#endif
    .identifier    = "egl",
    .dispose       = NULL,
  };

  (void)xine;
  (void)params;

  return (void *)&xine_egl_class;
}

#if defined(XINE_EGL_USE_X11)
static const xine_module_info_t module_info_egl = {
  .priority  = 9,
  .type      = "gl_v1",
  .sub_type  = XINE_VISUAL_TYPE_X11,
};
#elif defined(XINE_EGL_USE_WAYLAND)
static const xine_module_info_t module_info_egl = {
  .priority  = 10,
  .type      = "gl_v1",
  .sub_type  = XINE_VISUAL_TYPE_WAYLAND,
};
#endif

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_XINE_MODULE, 1, "egl", XINE_VERSION_CODE, &module_info_egl, egl_init_class },
  { PLUGIN_NONE, 0, NULL,  0, NULL, NULL },
};
