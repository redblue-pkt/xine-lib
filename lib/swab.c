/*
 * Copyright (C) 2018 the xine project
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
 */

#include "config.h"

#include <byteswap.h>
#include <sys/types.h>
#include <inttypes.h>

void xine_private_swab(const void *from, void *to, ssize_t n)
{
  const int16_t *in  = (int16_t*)from;
  int16_t       *out = (int16_t*)to;
  ssize_t        i;

  n /= 2;
  for (i = 0 ; i < n; i++) {
    out[i] = bswap_16(in[i]);
  }
}
