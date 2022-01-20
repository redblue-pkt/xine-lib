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
 * hw_frame.h, Interface between video output and HW decoders
 *
 */

#ifndef XINE_HW_FRAME_H_
#define XINE_HW_FRAME_H_

#include <xine/video_out.h>
#include <xine/xine_internal.h>

struct mem_frame_t;
struct xine_gl;

typedef struct xine_glconv_t xine_glconv_t;
struct xine_glconv_t {
  int  (*get_textures)(xine_glconv_t *, vo_frame_t *, unsigned target,
                       unsigned *textures, unsigned *textures_count, unsigned *sw_format);
  void (*destroy)     (xine_glconv_t **);
};

typedef struct xine_hwdec_t xine_hwdec_t;

struct xine_hwdec_t {
  int            frame_format;         /* Frame format (XINE_IMGFMT_*) */
  uint32_t       driver_capabilities;  /* VO_CAP_*      */

  struct
  mem_frame_t   *(*alloc_frame)         (xine_hwdec_t *);
  void           (*update_frame_format) (vo_driver_t *vo_driver,
                                         vo_frame_t *vo_frame,
                                         uint32_t width, uint32_t height,
                                         double ratio, int format, int flags);
  void           (*destroy)             (xine_hwdec_t **);


  xine_glconv_t *(*opengl_interop)      (xine_hwdec_t *, struct xine_gl *);
};

xine_hwdec_t *_x_hwdec_new(xine_t *xine, vo_driver_t *vo_driver,
                           unsigned visual_type, const void *visual,
                           unsigned flags);

#endif /* XINE_HW_FRAME_H_ */
