/*
 * Copyright (C) 2004-2019 the xine project
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
 * $Id:
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xineutils.h>

static void _copy_plane(uint8_t *restrict dst, const uint8_t *restrict src,
                        int dst_pitch, int src_pitch,
                        int width, int height)
{
  if (src_pitch == dst_pitch) {
    xine_fast_memcpy(dst, src, src_pitch * height);
  } else {
    int y;

    for (y = 0; y < height; y++) {
      xine_fast_memcpy(dst, src, width);
      src += src_pitch;
      dst += dst_pitch;
    }
  }
}

void yv12_to_yv12
  (const unsigned char *y_src, int y_src_pitch, unsigned char *y_dst, int y_dst_pitch,
   const unsigned char *u_src, int u_src_pitch, unsigned char *u_dst, int u_dst_pitch,
   const unsigned char *v_src, int v_src_pitch, unsigned char *v_dst, int v_dst_pitch,
   int width, int height) {

  _copy_plane(y_dst, y_src, y_dst_pitch, y_src_pitch, width,   height);
  _copy_plane(u_dst, u_src, u_dst_pitch, u_src_pitch, width/2, height/2);
  _copy_plane(v_dst, v_src, v_dst_pitch, v_src_pitch, width/2, height/2);
}

void yuy2_to_yuy2
  (const unsigned char *src, int src_pitch,
   unsigned char *dst, int dst_pitch,
   int width, int height) {

  _copy_plane(dst, src, dst_pitch, src_pitch, width*2, height);
}

void _x_nv12_to_yv12(const uint8_t *restrict y_src,  int y_src_pitch,
                     const uint8_t *restrict uv_src, int uv_src_pitch,
                     uint8_t *restrict y_dst, int y_dst_pitch,
                     uint8_t *restrict u_dst, int u_dst_pitch,
                     uint8_t *restrict v_dst, int v_dst_pitch,
                     int width, int height) {

  int y, x;

  _copy_plane(y_dst, y_src, y_dst_pitch, y_src_pitch, width, height);

  for (y = 0; y < height / 2; y++) {
    for (x = 0; x < width / 2; x++) {
      u_dst[x] = uv_src[2*x];
      v_dst[x] = uv_src[2*x + 1];
    }
    uv_src += uv_src_pitch;
    u_dst += u_dst_pitch;
    v_dst += v_dst_pitch;
  }
}

void _x_yv12_to_nv12(const uint8_t *y_src, int y_src_pitch,
                     const uint8_t *u_src, int u_src_pitch,
                     const uint8_t *v_src, int v_src_pitch,
                     uint8_t *y_dst,  int y_dst_pitch,
                     uint8_t *uv_dst, int uv_dst_pitch,
                     int width, int height) {
  int y, x;

  _copy_plane(y_dst, y_src, y_dst_pitch, y_src_pitch, width, height);

  /* Combine uv line to temporary (cached) buffer.
     Avoids fetching destination plane to cache. */
  uint8_t *line = xine_malloc_aligned(width + 1);
  if (!line)
    return;

  for(y = 0; y < height / 2; y++) {
    for(x = 0; x < width / 2; x++) {
      line[2*x]     = *(u_src + x);
      line[2*x + 1] = *(v_src + x);
    }

    xine_fast_memcpy(uv_dst, line, width);

    uv_dst += uv_dst_pitch;
    u_src += u_src_pitch;
    v_src += v_src_pitch;
  }

  xine_free_aligned(line);
}

void _x_yuy2_to_nv12(const uint8_t *src_yuy2_map, int yuy2_pitch,
                     uint8_t *y_dst,  int y_dst_pitch,
                     uint8_t *uv_dst, int uv_dst_pitch,
                     int width, int height) {
  int y, x;

  const uint8_t *yuy2_map = src_yuy2_map;
  for(y = 0; y < height; y++) {
    uint8_t *y_dst_tmp = y_dst;
    const uint8_t *yuy2_src_tmp = yuy2_map;
    for(x = 0; x < width / 2; x++) {
      *(y_dst_tmp++   ) = *(yuy2_src_tmp++);
      yuy2_src_tmp++;
      *(y_dst_tmp++   ) = *(yuy2_src_tmp++);
      yuy2_src_tmp++;
    }
    y_dst += y_dst_pitch;
    yuy2_map += yuy2_pitch;
  }

  yuy2_map = src_yuy2_map;
  for(y = 0; y < height; y += 2) {
    for(x = 0; x < width; x += 2) {
      *(uv_dst + x )     = *(yuy2_map + x*2 + 1);
      *(uv_dst + x + 1 ) = *(yuy2_map + x*2 + 3);
    }
    uv_dst += uv_dst_pitch;
    yuy2_map += yuy2_pitch * 2;
  }
}
