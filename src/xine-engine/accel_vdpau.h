/*
 * Copyright (C) 2008 the xine project
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
 *
 * Common acceleration definitions for vdpau
 *
 *
 */

#ifndef HAVE_XINE_ACCEL_VDPAU_H
#define HAVE_XINE_ACCEL_VDPAU_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <vdpau/vdpau.h>


typedef struct {

  VdpDevice vdp_device;

  VdpGetErrorString *vdp_get_error_string;
  VdpVideoSurfaceCreate *vdp_video_surface_create;
  VdpDecoderCreate *vdp_decoder_create;
  VdpDecoderDestroy *vdp_decoder_destroy;
  VdpDecoderRender *vdp_decoder_render;

  VdpVideoSurface surface;

} vdpau_accel_t;

#ifdef __cplusplus
}
#endif

#endif

