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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vaapi_egl.h"

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <va/va.h>
#ifdef HAVE_VA_VA_DRMCOMMON_H
# include <va/va_drmcommon.h>
#endif

#define LOG_MODULE "vaapi_egl"
#include <xine/xineutils.h>
#include <xine/video_out.h>

//#include "vaapi_util.h"
#include "accel_vaapi.h"
#include "xine_va_display.h"

/*
 * NOTE: this module is linked against VAAPI only.
 * OpenGL and EGL is used via xine_gl.
 */
#include "../opengl/xine_gl.h"

#include "../hw_frame.h"

/* Required: VAAPI surface DRM export */
#ifdef VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2

/*
 * OpenGL(ES) compat
 */

#if 1||defined(HAVE_GLES2_GL_H)
# include <GLES2/gl2.h>
#elif defined(HAVE_GL_GL_H)
# include <GL/gl.h>
#else
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned char GLubyte;
typedef int GLsizei;
#endif

#ifndef GL_EXTENSIONS
# define GL_EXTENSIONS 0x1F03
#endif
#ifndef APIENTRY
# define APIENTRY
#endif
#ifndef APIENTRYP
# define APIENTRYP APIENTRY *
#endif
#ifndef GL_TEXTURE_2D
# define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0    0x84C0
#endif

/*
 * EGL compat
 */

#ifdef HAVE_EGL_EGL_H
# include <EGL/egl.h>
#else
# define EGL_HEIGHT                        0x3056
# define EGL_NONE                          0x3038
# define EGL_WIDTH                         0x3057
typedef int32_t EGLint;
#endif

#ifdef HAVE_EGL_EGLEXT_H
# include <EGL/eglext.h>
#endif

#ifndef EGL_EXT_image_dma_buf_import
# define EGL_LINUX_DMA_BUF_EXT             0x3270
# define EGL_LINUX_DRM_FOURCC_EXT          0x3271
# define EGL_DMA_BUF_PLANE0_FD_EXT         0x3272
# define EGL_DMA_BUF_PLANE0_OFFSET_EXT     0x3273
# define EGL_DMA_BUF_PLANE0_PITCH_EXT      0x3274
#endif

#ifndef EGL_EXT_image_dma_buf_import_modifiers
# define EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT 0x3443
# define EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT 0x3444
#endif

#ifndef GL_OES_EGL_image
#define GL_OES_EGL_image 1
typedef void *GLeglImageOES;
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum target, GLeglImageOES image);
#endif

/*
 * VAAPI -> OpenGL texture import
 */

typedef struct glconv_vaapi_egl_t glconv_vaapi_egl_t;

struct glconv_vaapi_egl_t {
  xine_glconv_t api;

  xine_t    *xine;
  xine_gl_t *gl;

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
  void   (APIENTRYP glBindTexture) (GLenum target, GLuint texture);
  GLenum (APIENTRYP glGetError)    (void);

  void    *egl_image[3];
};

static inline int _va_check_status(glconv_vaapi_egl_t *this, VAStatus vaStatus, const char *msg)
{
  if (vaStatus != VA_STATUS_SUCCESS) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": "
            "Error : %s: %s [0x%04x]\n", msg, vaErrorStr(vaStatus), vaStatus);
    return 0;
  }
  return 1;
}

static void _release_images(glconv_vaapi_egl_t *c)
{
  unsigned i;
  for (i = 0; i < sizeof(c->egl_image) / sizeof(c->egl_image[0]); i++)
    if (c->egl_image[i])
      c->gl->eglDestroyImageKHR(c->gl, c->egl_image[i]);
}

static void *_create_egl_image(glconv_vaapi_egl_t *c, unsigned width, unsigned height,
                               unsigned drm_format, int fd, unsigned offset, unsigned pitch,
                               uint64_t modifier)
{
  EGLint attribs[] = {
    EGL_WIDTH, width,
    EGL_HEIGHT, height,
    EGL_LINUX_DRM_FOURCC_EXT, drm_format,
    EGL_DMA_BUF_PLANE0_FD_EXT, fd,
    EGL_DMA_BUF_PLANE0_OFFSET_EXT, offset,
    EGL_DMA_BUF_PLANE0_PITCH_EXT, pitch,
    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, modifier & 0xffffffff,
    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, modifier >> 32,
    EGL_NONE
  };
  void *egl_image;

  egl_image = c->gl->eglCreateImageKHR(c->gl, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
  if (!egl_image)
    xprintf(c->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": eglCreateImageKHR() failed\n");

  return egl_image;
}

static int _glconv_vaegl_get_textures(xine_glconv_t *glconv, vo_frame_t *vo_frame,
                                      unsigned target,
                                      unsigned *texture, unsigned *_num_texture, unsigned *_sw_format)
{
  glconv_vaapi_egl_t   *c     = xine_container_of(glconv, glconv_vaapi_egl_t, api);
  vaapi_accel_t        *accel = vo_frame->accel_data;
  ff_vaapi_context_t   *va_context;
  ff_vaapi_surface_t   *va_surface;
  unsigned layer, sw_format, num_texture, plane_width[3], plane_height[3];
  int vaapi_locked, result = -1;

  VADRMPRIMESurfaceDescriptor desc;
  VAStatus vaStatus;
#if 0
  vaapi_context_impl_t *va;
#endif

  _x_assert(vo_frame->format == XINE_IMGFMT_VAAPI);
  _x_assert(vo_frame->accel_data != NULL);

  *_sw_format = *_num_texture = 0;

  va_context = accel->f->get_context (vo_frame);
  if (!va_context) {
    xprintf(c->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": Invalid VA context\n");
    return -1;
  }

  /* driver lock */
  vaapi_locked = accel->f->lock_vaapi(vo_frame);

#if 0
  /* no context change, please */
  va = xine_container_of(va_context, vaapi_context_impl_t, c);
  pthread_mutex_lock(&va->ctx_lock);

  if (accel->index >= RENDER_SURFACES) {
    xprintf(c->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": Invalid VA surface index\n");
    goto fail;
  }
#endif
  va_surface = &va_context->va_render_surfaces[accel->index];
  if (va_surface->va_surface_id == VA_INVALID_SURFACE) {
    xprintf(c->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": Invalid VA surface\n");
    goto fail;
  }

  _release_images(c);

  vaStatus = vaSyncSurface(va_context->va_display, va_surface->va_surface_id);
  if (!_va_check_status(c, vaStatus, "vaSyncSurface()"))
    goto fail;

  vaStatus = vaExportSurfaceHandle (va_context->va_display, va_surface->va_surface_id,
                                    VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                    VA_EXPORT_SURFACE_SEPARATE_LAYERS | VA_EXPORT_SURFACE_READ_ONLY,
                                    &desc);
  if (!_va_check_status(c, vaStatus, "vaExportSurfaceHandle()"))
    goto fail;

  if (desc.num_layers > sizeof(c->egl_image) / sizeof(c->egl_image[0])) {
    xprintf(c->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": Too many layers (%d)\n", desc.num_layers);
    goto fail_fds;
  }

  switch (desc.fourcc) {
    case VA_FOURCC_P010:
      /* 'R16 ', 'GR32' */
    case VA_FOURCC_NV12:
      /* 'R8  ', 'GR88' */
      num_texture = 2;
      for (layer = 0; layer < 2; layer++) {
        plane_width[layer]  = va_context->width >> layer;
        plane_height[layer] = va_context->height >> layer;
      }
      sw_format = XINE_IMGFMT_NV12;
      break;
    case VA_FOURCC_YV12:
    case VA_FOURCC_I420:
      num_texture = 3;
      for (layer = 0; layer < 3; layer++) {
        plane_width[layer]  = va_context->width >> (!!layer);
        plane_height[layer] = va_context->height >> (!!layer);
      }
      sw_format = XINE_IMGFMT_YV12;
      break;
    default:
      xprintf(c->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": VA DRM fourcc %4.4s not supported\n",
              (const char *)&desc.fourcc);
      goto fail_fds;
  }

  for (layer = 0; layer < desc.num_layers; layer++) {
    unsigned obj_idx  = desc.layers[layer].object_index[0];
    GLenum   err;
    if (desc.layers[layer].num_planes > 1) {
      xprintf(c->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": DRM composed layers not supported\n");
      goto fail_fds;
    }

    lprintf("    layer %d: fd %d, pitch %d, VAAPI image fourcc %4.4s, DRM plane fourcc %4.4s\n",
            layer, desc.objects[obj_idx].fd, desc.layers[layer].pitch[0],
            (const char *)&desc.fourcc, (const char *)&desc.layers[layer].drm_format);

    c->egl_image[layer] = _create_egl_image(c, plane_width[layer], plane_height[layer],
                                            desc.layers[layer].drm_format, desc.objects[obj_idx].fd,
                                            desc.layers[layer].offset[0], desc.layers[layer].pitch[0],
                                            desc.objects[obj_idx].drm_format_modifier);
    if (!c->egl_image[layer]) {
      xprintf(c->xine, XINE_VERBOSITY_LOG, LOG_MODULE ": eglCreateImageKHR() failed\n");
      goto fail_fds;
    }

    c->glBindTexture (target, texture[layer]);
    c->glEGLImageTargetTexture2DOES (target, c->egl_image[layer]);
    if ((err = c->glGetError())) {
      xprintf(c->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": Texture import failed: 0x%04x\n", err);
      goto fail_fds;
    }
  }

  *_num_texture = num_texture;
  *_sw_format   = sw_format;
  result = 0;

 fail_fds:
  for (layer = 0; layer < desc.num_objects; layer++)
    close (desc.objects[layer].fd);

 fail:
#if 0
  pthread_mutex_unlock(&va->ctx_lock);
#endif
  if (vaapi_locked)
    accel->f->unlock_vaapi(vo_frame);

  return result;
}

static void _glconv_vaegl_destroy(xine_glconv_t **p)
{
  if (*p) {
    glconv_vaapi_egl_t *c = xine_container_of(*p, glconv_vaapi_egl_t, api);
    _release_images(c);
    _x_freep(p);
  }
}

/* test conversion */
static int _test(glconv_vaapi_egl_t *c, VADisplay *va_display)
{
  VASurfaceID va_surface_id;
  VAStatus    vaStatus;
  int         result = -1;

  if (!c->gl->make_current (c->gl))
    return -1;

  void (*glGenTextures) (GLsizei n, GLuint *textures) = c->gl->get_proc_address(c->gl, "glGenTextures");
  void (*glDeleteTextures) (GLsizei n, const GLuint *textures) = c->gl->get_proc_address(c->gl, "glDeleteTextures");
  void (*glActiveTexture) (GLenum texture) = c->gl->get_proc_address(c->gl, "glActiveTexture");
  void (*glEnable) (GLenum cap) = c->gl->get_proc_address(c->gl, "glEnable");
  void (*glDisable) (GLenum cap) = c->gl->get_proc_address(c->gl, "glDisable");

  if (!glGenTextures || !glDeleteTextures || !glActiveTexture || !glEnable || !glDisable) {
    c->gl->release_current (c->gl);
    return -1;
  }

  vaStatus = vaCreateSurfaces(va_display, VA_RT_FORMAT_YUV420,
                              1920, 1080, &va_surface_id, 1, NULL, 0);
  if (_va_check_status(c, vaStatus, "vaCreateSurfaces()")) {
    VAImage va_image;
    vaStatus = vaDeriveImage(va_display, va_surface_id, &va_image);
    if (_va_check_status(c, vaStatus, "vaDeriveImage()")) {
      vaStatus = vaSyncSurface(va_display, va_surface_id);
      if (_va_check_status(c, vaStatus, "vaSyncSurface()")) {
        VADRMPRIMESurfaceDescriptor desc;
        vaStatus = vaExportSurfaceHandle (va_display, va_surface_id, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                          VA_EXPORT_SURFACE_SEPARATE_LAYERS | VA_EXPORT_SURFACE_READ_ONLY,
                                          &desc);
        if (_va_check_status(c, vaStatus, "vaExportSurfaceHandle()")) {
          unsigned layer;

          result = 0;

          for (layer = 0; layer < desc.num_layers; layer++) {
            unsigned obj_idx  = desc.layers[layer].object_index[0];
            if (desc.layers[layer].num_planes > 1) {
              xprintf(c->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": DRM composed layers not supported\n");
              result = -1;
            } else {
              void *egl_image = _create_egl_image(c, va_image.width >> (!!layer), va_image.height >> (!!layer),
                                                  desc.layers[layer].drm_format, desc.objects[obj_idx].fd,
                                                  desc.layers[layer].offset[0], desc.layers[layer].pitch[0],
                                                  desc.objects[obj_idx].drm_format_modifier);
              if (!egl_image) {
                result = -1;
              } else {
                GLuint tex, err;
                glEnable(GL_TEXTURE_2D);
                glGenTextures(1, &tex);
                glActiveTexture (GL_TEXTURE0);
                c->glBindTexture (GL_TEXTURE_2D, tex);
                c->glEGLImageTargetTexture2DOES (GL_TEXTURE_2D, egl_image);
                if ((err = c->glGetError())) {
                  xprintf(c->xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": Texture import failed: 0x%x\n", err);
                  result = -1;
                }
                c->gl->eglDestroyImageKHR(c->gl, egl_image);
                glDeleteTextures(1, &tex);
                glDisable(GL_TEXTURE_2D);
              }
            }
          }
          for (layer = 0; layer < desc.num_objects; layer++)
            close(desc.objects[layer].fd);
        }
      }
    }
    vaStatus = vaSyncSurface(va_display, va_surface_id);
    _va_check_status(c, vaStatus, "vaSyncSurface()");
    vaStatus = vaDestroySurfaces(va_display, &va_surface_id, 1);
    _va_check_status(c, vaStatus, "vaDestroySurfaces()");
  }

  c->gl->release_current (c->gl);
  return result;
}

xine_glconv_t *_glconv_vaegl_init(xine_t *xine, xine_gl_t *gl,
                                  xine_va_display_t *va_display)
{
  glconv_vaapi_egl_t *c = NULL;
  int has_egl_image = 0;

  const GLubyte *(APIENTRYP glGetString) (GLenum name);

  if (!gl || !gl->get_proc_address || !gl->query_extensions)
    return NULL;
  if (!gl->eglCreateImageKHR) {
    xprintf(xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
            "No eglCreateImageKHR() detected\n");
    return NULL;
  }

  if (!_x_gl_has_extension(gl->query_extensions(gl), "EGL_EXT_image_dma_buf_import")) {
    xprintf(xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
            "EGL extension EGL_EXT_image_dma_buf_import not available\n");
    goto fail;
  }

  if (!gl->make_current (gl))
    return NULL;

  glGetString = gl->get_proc_address(gl, "glGetString");
  if (glGetString)
    has_egl_image = _x_gl_has_extension(glGetString(GL_EXTENSIONS), "GL_OES_EGL_image");

  gl->release_current (gl);

  if (!has_egl_image) {
    xprintf(xine, XINE_VERBOSITY_DEBUG, LOG_MODULE ": "
            "OpenGL extension GL_OES_EGL_image not available\n");
    goto fail;
  }

  c = calloc(1, sizeof(*c));
  if (!c)
    return NULL;

  c->glGetError = gl->get_proc_address(gl, "glGetError");
  c->glBindTexture = gl->get_proc_address(gl, "glBindTexture");
  c->glEGLImageTargetTexture2DOES = gl->get_proc_address(gl, "glEGLImageTargetTexture2DOES");
  if (!c->glGetError || !c->glBindTexture || !c->glEGLImageTargetTexture2DOES)
    goto fail;

  c->api.get_textures   = _glconv_vaegl_get_textures;
  c->api.destroy        = _glconv_vaegl_destroy;

  c->xine = xine;
  c->gl   = gl;

  if (_test(c, va_display->va_display) < 0)
    goto fail;

  xprintf(xine, XINE_VERBOSITY_LOG, LOG_MODULE ": VAAPI EGL interop enabled\n");
  return &c->api;

 fail:

  free(c);
  xprintf(xine, XINE_VERBOSITY_LOG, LOG_MODULE ": VAAPI EGL interop disabled\n");
  return NULL;
}

#else /* VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 */

# warning VAAPI OpenGL/EGL interop disabled (old libva ?)

xine_glconv_t *_glconv_vaegl_init(xine_t *xine, xine_gl_t *gl, xine_va_display_t *va_display)
{
  (void)gl;
  (void)va_display;
  xprintf(xine, XINE_VERBOSITY_LOG, LOG_MODULE " VAAPI EGL interop not compiled in\n");
  return NULL;
}

#endif /* VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 */

