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

#ifndef XINE_VAAPI_FRAME_H
#define XINE_VAAPI_FRAME_H

#include <xine/video_out.h>
#include "accel_vaapi.h"

#include "vaapi_util.h"
#include "../mem_frame.h"

typedef struct {
  mem_frame_t           mem_frame;
  vaapi_accel_t         vaapi_accel_data;
  vaapi_context_impl_t *ctx_impl;
} vaapi_frame_t;


vaapi_frame_t *_x_va_frame_alloc_frame (vaapi_context_impl_t *va, vo_driver_t *driver,
                                        int guarded_render);
void _x_va_frame_update_frame_format (vo_driver_t *this_gen,
                                      vo_frame_t *frame_gen,
                                      uint32_t width, uint32_t height,
                                      double ratio, int format, int flags);

void _x_va_frame_dispose (vo_frame_t *vo_img);
void _x_va_frame_provide_standard_frame_data (vo_frame_t *vo_frame, xine_current_frame_data_t *data);
void _x_va_frame_duplicate_frame_data (vo_frame_t *this_gen, vo_frame_t *original);

/*
 *
 */

static inline
vaapi_context_impl_t *_ctx_from_frame(vo_frame_t *vo_frame)
{
  vaapi_frame_t *frame = xine_container_of(vo_frame, vaapi_frame_t, mem_frame.vo_frame);
  return frame->ctx_impl;
}

/*
 * accel
 */

static inline
int _x_va_accel_profile_from_imgfmt(vo_frame_t *vo_frame, unsigned format)
{
  vaapi_context_impl_t *va = _ctx_from_frame(vo_frame);
  return _x_va_profile_from_imgfmt(va, format);
}

static inline
ff_vaapi_context_t *_x_va_accel_get_context(vo_frame_t *vo_frame)
{
  vaapi_context_impl_t *va = _ctx_from_frame(vo_frame);
  return &va->c;
}

static inline
int _x_va_accel_vaapi_init(vo_frame_t *vo_frame, int va_profile, int width, int height)
{
  vaapi_context_impl_t *va = _ctx_from_frame(vo_frame);
  return _x_va_init(va, va_profile, width, height);
}

static inline
int _x_va_accel_guarded_render(vo_frame_t *vo_frame)
{
  vaapi_accel_t *accel = vo_frame->accel_data;
  return accel->f->render_vaapi_surface != NULL;
}

/*
 * non-guarded mode
 */

static inline
int _x_va_accel_lock_decode_dummy(vo_frame_t *vo_frame)
{
  (void)vo_frame;
  return 0;
}

static inline
ff_vaapi_surface_t *_x_va_accel_get_vaapi_surface(vo_frame_t *vo_frame)
{
  vaapi_context_impl_t *va = _ctx_from_frame(vo_frame);
  vaapi_accel_t        *accel = vo_frame->accel_data;
  _x_assert(accel->index < RENDER_SURFACES); /* index is constant in this mode */
  return &va->c.va_render_surfaces[accel->index];
}

/*
 * guarded mode
 */

static inline
ff_vaapi_surface_t *_x_va_accel_alloc_vaapi_surface(vo_frame_t *vo_frame)
{
  vaapi_context_impl_t *va = _ctx_from_frame(vo_frame);
  return _x_va_alloc_surface(va);
}

static inline
void _x_va_accel_render_vaapi_surface(vo_frame_t *vo_frame, ff_vaapi_surface_t *va_surface)
{
  vaapi_context_impl_t *va = _ctx_from_frame(vo_frame);
  vaapi_accel_t        *accel = vo_frame->accel_data;

  accel->index = va_surface->index;
  _x_va_render_surface(va, va_surface);
}

static inline
void _x_va_accel_release_vaapi_surface(vo_frame_t *vo_frame, ff_vaapi_surface_t *va_surface)
{
  /* surface was not binded to this vo_frame */
  vaapi_context_impl_t *va = _ctx_from_frame(vo_frame);
  _x_va_release_surface(va, va_surface);
}

/*
 * video out
 */

static inline
void _x_va_frame_displayed(vo_frame_t *vo_frame)
{
  vaapi_context_impl_t *va_context = _ctx_from_frame(vo_frame);
  vaapi_accel_t *accel = vo_frame->accel_data;

  if (accel->index < RENDER_SURFACES) {
    ff_vaapi_surface_t *va_surface = &va_context->c.va_render_surfaces[accel->index];
    _x_va_surface_displayed(va_context, va_surface);
    accel->index = RENDER_SURFACES; /* invalid */
  }
}

#endif /* XINE_VAAPI_UTIL_H */
