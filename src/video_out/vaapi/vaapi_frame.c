/*
 * Copyright (C) 2012 Edgar Hucek <gimli|@dark-green.com>
 * Copyright (C) 2012-2022 xine developers
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * vaapi_frame.c, VAAPI video extension interface for xine
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <pthread.h>

#include <va/va.h>

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/video_out.h>

#include "vaapi_frame.h"

#include "accel_vaapi.h"

#define FRAME_FORMAT_STR(f) \
  ( (f) == XINE_IMGFMT_VAAPI ? "XINE_IMGFMT_VAAPI" : \
    (f) == XINE_IMGFMT_YV12  ? "XINE_IMGFMT_YV12"  : \
    (f) == XINE_IMGFMT_YUY2  ? "XINE_IMGFMT_YUY2"  : \
    (f) == XINE_IMGFMT_NV12  ? "XINE_IMGFMT_NV12"  : \
    "UNKNOWN")

void _x_va_frame_provide_standard_frame_data (vo_frame_t *vo_frame, xine_current_frame_data_t *data)
{
  vaapi_context_impl_t *va = _ctx_from_frame(vo_frame);
  vaapi_accel_t        *accel = vo_frame->accel_data;
  ff_vaapi_surface_t   *va_surface;

  VAStatus  vaStatus;
  uint32_t  pitches[3];
  uint8_t  *base[3];
  int       width, height;

  if (vo_frame->format != XINE_IMGFMT_VAAPI) {
    xprintf(va->xine, XINE_VERBOSITY_LOG, LOG_MODULE " "
            "vaapi_provide_standard_frame_data: unexpected frame format 0x%08x!\n",
            vo_frame->format);
    return;
  }

  if (accel->index >= RENDER_SURFACES /* invalid */) {
    xprintf(va->xine, XINE_VERBOSITY_LOG, LOG_MODULE " "
            "vaapi_provide_standard_frame_data: invalid surface\n");
    return;
  }

  pthread_mutex_lock(&va->ctx_lock);

  va_surface = &va->c.va_render_surfaces[accel->index];
  if (va_surface->va_surface_id == VA_INVALID_SURFACE)
    goto error;

  lprintf("vaapi_provide_standard_frame_data 0x%08x width %d height %d\n",
          va_surface->va_surface_id, vo_frame->width, vo_frame->height);

  width  = va->c.width;
  height = va->c.height;

  data->format = XINE_IMGFMT_YV12;
  data->img_size = width * height
                   + ((width + 1) / 2) * ((height + 1) / 2)
                   + ((width + 1) / 2) * ((height + 1) / 2);
  if (!data->img)
    goto error;

  pitches[0] = width;
  pitches[2] = width / 2;
  pitches[1] = width / 2;
  base[0] = data->img;
  base[2] = data->img + width * height;
  base[1] = data->img + width * height + width * vo_frame->height / 4;

  VAImage   va_image;
  void     *p_base;
  int       is_bound;
  VASurfaceStatus surf_status = 0;

  vaStatus = vaSyncSurface(va->c.va_display, va_surface->va_surface_id);
  _x_va_check_status(va, vaStatus, "vaSyncSurface()");

  if (va->query_va_status) {
    vaStatus = vaQuerySurfaceStatus(va->c.va_display, va_surface->va_surface_id, &surf_status);
    _x_va_check_status(va, vaStatus, "vaQuerySurfaceStatus()");
  } else {
    surf_status = VASurfaceReady;
  }

  if (surf_status != VASurfaceReady)
    goto error;

  vaStatus = _x_va_create_image(va, va_surface->va_surface_id, &va_image, width, height, 0, &is_bound);
  if (!_x_va_check_status(va, vaStatus, "_x_va_create_image()"))
    goto error;

  lprintf("vaapi_provide_standard_frame_data accel->va_surface_id 0x%08x va_image.image_id 0x%08x "
          "va_context->width %d va_context->height %d va_image.width %d va_image.height %d "
          "width %d height %d size1 %d size2 %d %d %d %d status %d num_planes %d\n",
          va_surface->va_surface_id, va_image.image_id, va->c.width, va->c.height,
          va_image.width, va_image.height, width, height, va_image.data_size, data->img_size,
          va_image.pitches[0], va_image.pitches[1], va_image.pitches[2], surf_status, va_image.num_planes);

  if (va_image.image_id == VA_INVALID_ID)
    goto error;

  if (!is_bound) {
    vaStatus = vaGetImage(va->c.va_display, va_surface->va_surface_id, 0, 0,
                          va_image.width, va_image.height, va_image.image_id);
    if (!_x_va_check_status(va, vaStatus, "vaGetImage()"))
      goto error;
  }

  vaStatus = vaMapBuffer( va->c.va_display, va_image.buf, &p_base ) ;
  if (!_x_va_check_status(va, vaStatus, "vaMapBuffer()"))
    goto error_image;

  /*
        uint8_t *src[3] = { NULL, };
        src[0] = (uint8_t *)p_base + va_image.offsets[0];
        src[1] = (uint8_t *)p_base + va_image.offsets[1];
        src[2] = (uint8_t *)p_base + va_image.offsets[2];
  */

  if (va_image.format.fourcc == VA_FOURCC( 'Y', 'V', '1', '2' ) ||
      va_image.format.fourcc == VA_FOURCC( 'I', '4', '2', '0' )) {
    lprintf("VAAPI YV12 image\n");

    yv12_to_yv12((uint8_t*)p_base + va_image.offsets[0], va_image.pitches[0],
                 base[0], pitches[0],
                 (uint8_t*)p_base + va_image.offsets[1], va_image.pitches[1],
                 base[1], pitches[1],
                 (uint8_t*)p_base + va_image.offsets[2], va_image.pitches[2],
                 base[2], pitches[2],
                 va_image.width, va_image.height);

  } else if (va_image.format.fourcc == VA_FOURCC( 'N', 'V', '1', '2' )) {
    lprintf("VAAPI NV12 image\n");

    lprintf("va_image.offsets[0] %d va_image.offsets[1] %d va_image.offsets[2] %d size %d size %d size %d width %d height %d width %d height %d\n",
            va_image.offsets[0], va_image.offsets[1], va_image.offsets[2], va_image.data_size, va_image.width * va_image.height,
            data->img_size, width, height, va_image.width, va_image.height);

    base[0] = data->img;
    base[1] = data->img + width * height;
    base[2] = data->img + width * height + width * height / 4;
    _x_nv12_to_yv12((uint8_t *)p_base + va_image.offsets[0], va_image.pitches[0],
                    (uint8_t *)p_base + va_image.offsets[1], va_image.pitches[1],
                    base[0], pitches[0],
                    base[1], pitches[1],
                    base[2], pitches[2],
                    va_image.width  > width  ? width  : va_image.width,
                    va_image.height > height ? height : va_image.height);

  } else {
    printf("vaapi_provide_standard_frame_data unsupported image format\n");
  }

  vaStatus = vaUnmapBuffer(va->c.va_display, va_image.buf);
  _x_va_check_status(va, vaStatus, "vaUnmapBuffer()");

 error_image:
  _x_va_destroy_image(va, &va_image);

error:
  pthread_mutex_unlock(&va->ctx_lock);
}

void _x_va_frame_duplicate_frame_data (vo_frame_t *this_gen, vo_frame_t *original)
{
  vaapi_context_impl_t *va = _ctx_from_frame(this_gen);

  mem_frame_t *this = xine_container_of(this_gen, mem_frame_t, vo_frame);
  mem_frame_t *orig = xine_container_of(original, mem_frame_t, vo_frame);

  vaapi_accel_t *accel_this = this_gen->accel_data;
  vaapi_accel_t *accel_orig = original->accel_data;

  ff_vaapi_surface_t *va_surface_this = NULL;
  ff_vaapi_surface_t *va_surface_orig;

  VAImage   va_image_orig;
  VAImage   va_image_this;
  VAStatus  vaStatus;
  void     *p_base_orig = NULL;
  void     *p_base_this = NULL;
  int       this_is_bound, orig_is_bound;

  if (orig->vo_frame.format != XINE_IMGFMT_VAAPI) {
    xprintf(va->xine, XINE_VERBOSITY_LOG, LOG_MODULE " "
            "vaapi_duplicate_frame_data: unexpected frame format 0x%08x!\n", orig->format);
    return;
  }

  if (this->vo_frame.format != XINE_IMGFMT_VAAPI) {
    xprintf(va->xine, XINE_VERBOSITY_LOG, LOG_MODULE " "
            "vaapi_duplicate_frame_data: unexpected frame format 0x%08x!\n", this->format);
    return;
  }

  va_image_this.image_id = VA_INVALID_ID;
  va_image_orig.image_id = VA_INVALID_ID;

  pthread_mutex_lock(&va->ctx_lock);

  if (_x_va_accel_guarded_render(this_gen)) {
    if (accel_orig->index >= RENDER_SURFACES) {
      xprintf(va->xine, XINE_VERBOSITY_LOG, LOG_MODULE " "
              "vaapi_duplicate_frame_data: invalid source surface\n");
      goto error;
    }
    va_surface_orig = &va->c.va_render_surfaces[accel_orig->index];

    va_surface_this = _x_va_accel_alloc_vaapi_surface(this_gen);
    if (!va_surface_this) {
      xprintf(va->xine, XINE_VERBOSITY_LOG, LOG_MODULE " "
              "vaapi_duplicate_frame_data: surface allocation failed\n");
      goto error;
    }
  } else {
    _x_assert (accel_this->index < RENDER_SURFACES); /* "fixed" in this mode */
    _x_assert (accel_orig->index < RENDER_SURFACES); /* "fixed" in this mode */
    va_surface_this = &va->c.va_render_surfaces[accel_this->index];
    va_surface_orig = &va->c.va_render_surfaces[accel_orig->index];
  }

  lprintf("vaapi_duplicate_frame_data  0x%08x <- 0x%08x\n",
          va_surface_this->va_surface_id, va_surface_orig->va_surface_id);

  vaStatus = vaSyncSurface(va->c.va_display, va_surface_orig->va_surface_id);
  _x_va_check_status(va, vaStatus, "vaSyncSurface()");

  vaStatus = _x_va_create_image(va, va_surface_orig->va_surface_id, &va_image_orig,
                                va->c.width, va->c.height, 0, &orig_is_bound);
  if (!_x_va_check_status(va, vaStatus, "_x_va_create_image()")) {
    va_image_orig.image_id = VA_INVALID_ID;
    goto error;
  }

  vaStatus = _x_va_create_image(va, va_surface_this->va_surface_id, &va_image_this,
                                va->c.width, va->c.height, 0, &this_is_bound);
  if (!_x_va_check_status(va, vaStatus, "_x_va_create_image()")) {
    va_image_this.image_id = VA_INVALID_ID;
    goto error;
  }

  if(va_image_orig.image_id == VA_INVALID_ID || va_image_this.image_id == VA_INVALID_ID) {
    printf("vaapi_duplicate_frame_data invalid image\n");
    goto error;
  }

  lprintf("vaapi_duplicate_frame_data va_image_orig.image_id 0x%08x "
          "va_image_orig.width %d va_image_orig.height %d width %d height %d "
          "size %d %d %d %d\n",
          va_image_orig.image_id, va_image_orig.width, va_image_orig.height,
          this->width, this->height, va_image_orig.data_size,
          va_image_orig.pitches[0], va_image_orig.pitches[1], va_image_orig.pitches[2]);

  if (!orig_is_bound) {
    vaStatus = vaGetImage(va->c.va_display, va_surface_orig->va_surface_id, 0, 0,
                          va_image_orig.width, va_image_orig.height, va_image_orig.image_id);
    if (!_x_va_check_status(va, vaStatus, "vaGetImage()"))
      goto error;
  }

  if (!this_is_bound) {
    vaStatus = vaPutImage(va->c.va_display, va_surface_this->va_surface_id, va_image_orig.image_id,
                          0, 0, va_image_orig.width, va_image_orig.height,
                          0, 0, va_image_this.width, va_image_this.height);
    _x_va_check_status(va, vaStatus, "vaPutImage()");
  } else {
    vaStatus = vaMapBuffer(va->c.va_display, va_image_orig.buf, &p_base_orig);
    if (!_x_va_check_status(va, vaStatus, "vaMapBuffer()"))
      goto error;

    vaStatus = vaMapBuffer(va->c.va_display, va_image_this.buf, &p_base_this);
    if (!_x_va_check_status(va, vaStatus, "vaMapBuffer()"))
      goto error;

    int size = (va_image_orig.data_size > va_image_this.data_size) ? va_image_this.data_size : va_image_orig.data_size;
    xine_fast_memcpy((uint8_t *) p_base_this, (uint8_t *) p_base_orig, size);
  }

  if (_x_va_accel_guarded_render(this_gen)) {
    accel_this->index = va_surface_this->index;
    va_surface_this->status = SURFACE_RENDER_RELEASE;
  }
  va_surface_this = NULL; /* do not release */

error:
  if(p_base_orig) {
    vaStatus = vaUnmapBuffer(va->c.va_display, va_image_orig.buf);
    _x_va_check_status(va, vaStatus, "vaUnmapBuffer()");
  }
  if(p_base_this) {
    vaStatus = vaUnmapBuffer(va->c.va_display, va_image_this.buf);
    _x_va_check_status(va, vaStatus, "vaUnmapBuffer()");
  }

  if (va_image_orig.image_id != VA_INVALID_ID)
    _x_va_destroy_image(va, &va_image_orig);
  if (va_image_this.image_id != VA_INVALID_ID)
    _x_va_destroy_image(va, &va_image_this);

  if (va_surface_this) {
    /* --> failed, need to release allocated surface */
    if (_x_va_accel_guarded_render(this_gen)) {
      _x_va_surface_displayed(va, va_surface_this);
      accel_this->index = RENDER_SURFACES; /* invalid */
    }
  }

  pthread_mutex_unlock(&va->ctx_lock);
}

void _x_va_frame_update_frame_format (vo_driver_t *this_gen,
                                      vo_frame_t *vo_frame,
                                      uint32_t width, uint32_t height,
                                      double ratio, int format, int flags) {
  mem_frame_t *frame = xine_container_of(vo_frame, mem_frame_t, vo_frame);

  if (frame->format == XINE_IMGFMT_VAAPI /* check _old_ format */) {
    if (_x_va_accel_guarded_render(vo_frame)) {
      /* This code handles frames that were dropped (used in decoder, but not drawn). */
      vaapi_context_impl_t *va = _ctx_from_frame(vo_frame);
      pthread_mutex_lock(&va->ctx_lock);
      _x_va_frame_displayed(vo_frame);
      pthread_mutex_unlock(&va->ctx_lock);
    }
  }

  lprintf("vaapi_update_frame_format %s -> %s width %d height %d\n",
          FRAME_FORMAT_STR(frame->format), FRAME_FORMAT_STR(format),
          width, height);

  mem_frame_update_frame_format(this_gen, vo_frame, width, height, ratio, format, flags);

  if (format == XINE_IMGFMT_VAAPI) {
    frame->width = width; /* mem_frame freed frame->base */
    frame->vo_frame.width = width;
    frame->vo_frame.proc_duplicate_frame_data = _x_va_frame_duplicate_frame_data;
    frame->vo_frame.proc_provide_standard_frame_data = _x_va_frame_provide_standard_frame_data;
    lprintf("XINE_IMGFMT_VAAPI width %d height %d\n", width, height);
  } else {
    frame->vo_frame.proc_duplicate_frame_data = NULL;
    frame->vo_frame.proc_provide_standard_frame_data = NULL;
  }

#ifdef DEBUG
  if (_x_va_accel_guarded_render(vo_frame)) {
    vaapi_accel_t *accel = vo_frame->accel_data;
    _x_assert(accel->index == RENDER_SURFACES);
  }
#endif
}

void _x_va_frame_dispose (vo_frame_t *vo_frame)
{
  vaapi_frame_t  *frame = xine_container_of(vo_frame, vaapi_frame_t, mem_frame.vo_frame);
  vaapi_accel_t  *accel = &frame->vaapi_accel_data;
  vaapi_context_impl_t *va = frame->ctx_impl;

  if (accel->index < RENDER_SURFACES) {
    if (_x_va_accel_guarded_render(vo_frame)) {
      ff_vaapi_surface_t *va_surface = &va->c.va_render_surfaces[accel->index];
      pthread_mutex_lock(&va->surfaces_lock);
      va_surface->status = SURFACE_FREE;
      pthread_mutex_unlock(&va->surfaces_lock);
    }
  }

  _mem_frame_dispose(vo_frame);
}

vaapi_frame_t *_x_va_frame_alloc_frame (vaapi_context_impl_t *va, vo_driver_t *driver, int guarded_render)
{
  static const struct vaapi_accel_funcs_s accel_funcs = {
    .vaapi_init                = _x_va_accel_vaapi_init,
    .profile_from_imgfmt       = _x_va_accel_profile_from_imgfmt,
    .get_context               = _x_va_accel_get_context,
    .lock_vaapi                = _x_va_accel_lock_decode_dummy,
    .unlock_vaapi              = NULL,

    .get_vaapi_surface         = _x_va_accel_get_vaapi_surface,
    .render_vaapi_surface      = NULL,
    .release_vaapi_surface     = NULL,
    .guarded_render            = _x_va_accel_guarded_render,
  };
  static const struct vaapi_accel_funcs_s accel_funcs_guarded = {
    .vaapi_init                = _x_va_accel_vaapi_init,
    .profile_from_imgfmt       = _x_va_accel_profile_from_imgfmt,
    .get_context               = _x_va_accel_get_context,
    .lock_vaapi                = _x_va_accel_lock_decode_dummy,
    .unlock_vaapi              = NULL,

    .get_vaapi_surface         = _x_va_accel_alloc_vaapi_surface,
    .render_vaapi_surface      = _x_va_accel_render_vaapi_surface,
    .release_vaapi_surface     = _x_va_accel_release_vaapi_surface,
    .guarded_render            = _x_va_accel_guarded_render,
  };
  vaapi_frame_t   *frame;

  if (va->num_frames >= sizeof(va->frames) / sizeof(va->frames[0])) {
    xprintf(va->xine, XINE_VERBOSITY_LOG, LOG_MODULE " alloc_frame: "
            "frame limit (%u) exceeded\n", va->num_frames);
    return NULL;
  }

  frame = (vaapi_frame_t *)_mem_frame_alloc_frame(driver, sizeof(vaapi_frame_t));

  if (!frame)
    return NULL;

  frame->mem_frame.vo_frame.dispose = _x_va_frame_dispose;
  frame->mem_frame.vo_frame.accel_data = &frame->vaapi_accel_data;
  frame->ctx_impl = va;

  frame->vaapi_accel_data.f = guarded_render ? &accel_funcs_guarded : &accel_funcs;

  frame->vaapi_accel_data.index = guarded_render ? RENDER_SURFACES : va->num_frames;

  va->frames[va->num_frames] = &frame->mem_frame.vo_frame;
  va->num_frames++;

  return frame;
}
