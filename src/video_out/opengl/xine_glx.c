/*
 * Copyright (C) 2012-2019 the xine project
 * Copyright (C) 2012 Christophe Thommeret <hftom@free.fr>
 * Copyright (C) 2012-2019 Petri Hintukainen <phintuka@users.sourceforge.net>
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
 *
 * video_out_opengl2.c, a video output plugin using opengl 2.0
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_gl_plugin.h"
#include "xine_gl.h"

#include <stdlib.h>

#include <xine.h> /* visual types */
#include <xine/xine_internal.h>

#include <X11/Xlib.h>
#include <GL/glx.h>

typedef struct {
  xine_gl_plugin_t p;

  Display    *display;
  Drawable    drawable;
  int         screen;
  GLXContext  context;

  int         lock1, lock2;

  /* DEBUG */
  int         is_current;
} xine_glx_t;

#define GLX(_gl) xine_container_of(_gl, xine_glx_t, p.gl)

static int _glx_make_current(xine_gl_t *gl)
{
  xine_glx_t *glx = GLX(gl);
  int result;

  _x_assert(!glx->is_current);

  /* User may change lock1 anytime, make sure we pair lock calls properly. */
  glx->lock2 = glx->lock1;

  XLockDisplay(glx->display);
  result = glXMakeCurrent(glx->display, glx->drawable, glx->context);

  if (!result) {
    XUnlockDisplay (glx->display);
    xprintf(glx->p.xine, XINE_VERBOSITY_LOG, "glx: display unavailable for rendering\n");
    return 0;
  }

  if (!glx->lock2)
    XUnlockDisplay (glx->display);

  glx->is_current = 1;
  return result;
}

static void _glx_release_current(xine_gl_t *gl)
{
  xine_glx_t *glx = GLX(gl);

  _x_assert(glx->is_current);

  if (!glx->lock2)
    XLockDisplay (glx->display);
  glXMakeCurrent(glx->display, None, NULL);
  XUnlockDisplay(glx->display);

  glx->is_current = 0;
}

static void _glx_swap_buffers(xine_gl_t *gl)
{
  xine_glx_t *glx = GLX(gl);

  XLockDisplay(glx->display);
  glXSwapBuffers(glx->display, glx->drawable);
  XUnlockDisplay(glx->display);
}

static void _glx_set_native_window(xine_gl_t *gl, void *drawable)
{
  xine_glx_t *glx = GLX(gl);

  _x_assert(!glx->is_current);

  XLockDisplay(glx->display);

  glx->drawable = (intptr_t)drawable;
  XUnlockDisplay(glx->display);
}

static void _glx_resize(xine_gl_t *gl, int w, int h)
{
  (void)gl;
  (void)w;
  (void)h;
}

static void *_glx_get_proc_address(xine_gl_t *gl, const char *procname)
{
  (void)gl;
#ifdef GLX_ARB_get_proc_address
  return glXGetProcAddressARB(procname);
#else
# warning GLX_ARB_get_proc_address extension missing
  (void)procname;
  return NULL;
#endif
}

static const char *_glx_query_extensions(xine_gl_t *gl)
{
#ifdef GLX_VERSION_1_1
  xine_glx_t *glx = GLX(gl);

  return glXQueryExtensionsString (glx->display, glx->screen);
#else
# warning GLX version 1.1 not detected !
  return NULL;
#endif
}


/*
 * xine module
 */

static void _module_dispose(xine_module_t *module)
{
  xine_glx_t *glx = (xine_glx_t *)module;

  lprintf("Destroying glx context %p\n", (void*)glx->context);

  glx->p.xine->config->unregister_callback (glx->p.xine->config, "video.output.lockdisplay");

  _x_assert(!glx->is_current);

  XLockDisplay(glx->display);
  if (glx->is_current) {
    glXMakeCurrent(glx->display, None, NULL);
  }
  glXDestroyContext(glx->display, glx->context);
  XUnlockDisplay(glx->display);

  free(glx);
}

static void _glx_set_lockdisplay (void *this_gen, xine_cfg_entry_t *entry) {
  xine_glx_t *glx = (xine_glx_t *)this_gen;
  glx->lock1 = entry->num_value;
  xprintf (glx->p.xine, XINE_VERBOSITY_DEBUG, "glx: lockdisplay=%d\n", glx->lock1);
}

static void _register_config(config_values_t *config, xine_glx_t *glx)
{
  int r = config->register_bool (config,
                                 "video.output.lockdisplay", 0,
                                 _("Lock X display during whole frame output."),
                                 _("This sometimes reduces system load and jitter.\n"),
                                 10,
                                 glx ? _glx_set_lockdisplay : NULL,
                                 glx);
  if (glx)
    glx->lock1 = r;
}

static xine_module_t *_glx_get_instance(xine_module_class_t *class_gen, const void *data)
{
  const gl_plugin_params_t *params = data;
  const x11_visual_t *vis = params->visual;
  Window              root;
  XVisualInfo        *visinfo;
  GLXContext          ctx;
  int                 is_direct = 0;
  xine_glx_t         *glx;

  int attribs[] = {
    GLX_RGBA,
    GLX_DOUBLEBUFFER,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_DEPTH_SIZE, 16,
    None,
  };

  (void)class_gen;

  if (!(params->flags & XINE_GL_API_OPENGL)) {
    return NULL;
  }

  _x_assert(vis);
  _x_assert(vis->display);

  XLockDisplay(vis->display);

  root = RootWindow(vis->display, vis->screen);
  if (!root) {
    goto fail_locked;
  }

  visinfo = glXChooseVisual(vis->display, vis->screen, attribs);
  if (!visinfo) {
    goto fail_locked;
  }

  ctx = glXCreateContext(vis->display, visinfo, NULL, GL_TRUE);
  XFree(visinfo);
  if (!ctx) {
    goto fail_locked;
  }

  if (!glXMakeCurrent(vis->display, vis->d, ctx)) {
    goto fail_created;
  }

  is_direct = glXIsDirect(vis->display, ctx);
  glXMakeCurrent(vis->display, None, NULL);
  if (!is_direct) {
    goto fail_created;
  }

  glx = calloc(1, sizeof(*glx));
  if (!glx) {
    goto fail_created;
  }

  XUnlockDisplay(vis->display);

  glx->p.module.dispose     = _module_dispose;

  glx->p.gl.make_current      = _glx_make_current;
  glx->p.gl.release_current   = _glx_release_current;
  glx->p.gl.swap_buffers      = _glx_swap_buffers;
  glx->p.gl.resize            = _glx_resize;
  glx->p.gl.set_native_window = _glx_set_native_window;
  glx->p.gl.dispose           = NULL;

  glx->p.gl.query_extensions  = _glx_query_extensions;
  glx->p.gl.get_proc_address  = _glx_get_proc_address;

  glx->p.xine   = params->xine;

  glx->context  = ctx;
  glx->display  = vis->display;
  glx->drawable = vis->d;
  glx->screen   = vis->screen;

  _register_config(glx->p.xine->config, glx);

  return &glx->p.module;

 fail_created:
  glXDestroyContext( vis->display, ctx );
 fail_locked:
  XUnlockDisplay (vis->display);
  return NULL;
}

/*
 * plugin
 */

static void *glx_init_class(xine_t *xine, const void *params)
{
  static const xine_module_class_t xine_glx_class = {
    .get_instance  = _glx_get_instance,
    .description   = N_("GL provider (GLX)"),
    .identifier    = "glx",
    .dispose       = NULL,
  };

  _register_config(xine->config, NULL);

  (void)xine;
  (void)params;

  return (void *)&xine_glx_class;
}

static const xine_module_info_t module_info_glx = {
  .priority  = 10,
  .type      = "gl_v1",
  .sub_type  = XINE_VISUAL_TYPE_X11,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_XINE_MODULE, 1, "glx", XINE_VERSION_CODE, &module_info_glx, glx_init_class },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL },
};
