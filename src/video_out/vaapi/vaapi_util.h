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
 * vaapi_util.c, VAAPI video extension interface for xine
 *
 */

#ifndef XINE_VAAPI_UTIL_H
#define XINE_VAAPI_UTIL_H

#include <xine/video_out.h>
#include "accel_vaapi.h"

#include <sys/types.h>

#include <va/va.h>

/*
 *
 */

const char *_x_va_profile_to_string(VAProfile profile);
const char *_x_va_entrypoint_to_string(VAEntrypoint entrypoint);

void _x_va_reset_va_context(ff_vaapi_context_t *va_context);

VAStatus _x_va_initialize(ff_vaapi_context_t *va_context, int visual_type, const void *visual, int opengl_render);
VAStatus _x_va_terminate(ff_vaapi_context_t *va_context);

/*
 *
 */

typedef struct vaapi_context_impl vaapi_context_impl_t;

#define  RENDER_SURFACES  50

struct vaapi_context_impl {
  ff_vaapi_context_t  c;

  xine_t *xine;

  int query_va_status;

  pthread_mutex_t     surfaces_lock;
  ff_vaapi_surface_t  va_render_surfaces_storage[RENDER_SURFACES + 1];
  VASurfaceID         va_surface_ids_storage[RENDER_SURFACES + 1];
};

vaapi_context_impl_t *_x_va_new(xine_t *xine, int visual_type, const void *visual, int opengl_render);
void _x_va_free(vaapi_context_impl_t **va_context);

int _x_va_check_status(vaapi_context_impl_t *va_context, VAStatus vaStatus, const char *msg);

void _x_va_destroy_image(vaapi_context_impl_t *va_context, VAImage *va_image);
VAStatus _x_va_create_image(vaapi_context_impl_t *va_context, VASurfaceID va_surface_id, VAImage *va_image, int width, int height, int clear, int *is_bound);

int _x_va_profile_from_imgfmt(vaapi_context_impl_t *va_context, unsigned format);

/*
 * surface pool
 */

void _x_va_close(vaapi_context_impl_t *va_context);
VAStatus _x_va_init(vaapi_context_impl_t *va_context, int va_profile, int width, int height);

ff_vaapi_surface_t *_x_va_alloc_surface(vaapi_context_impl_t *va_context);
void _x_va_render_surface(vaapi_context_impl_t *va_context, ff_vaapi_surface_t *va_surface);
void _x_va_release_surface(vaapi_context_impl_t *va_context, ff_vaapi_surface_t *va_surface);
void _x_va_surface_displayed(vaapi_context_impl_t *va_context, ff_vaapi_surface_t *va_surface);

#endif /* XINE_VAAPI_UTIL_H */
