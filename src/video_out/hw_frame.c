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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/video_out.h>
#include <xine/xine_internal.h>

#include "hw_frame.h"

xine_hwdec_t *_x_hwdec_new(xine_t *xine, vo_driver_t *vo_driver,
                           unsigned visual_type, const void *visual,
                           unsigned flags)
{
  (void)xine;
  (void)vo_driver;
  (void)visual_type;
  (void)visual;
  (void)flags;
  return NULL;
}
