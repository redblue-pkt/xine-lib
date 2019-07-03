/*
 * Copyright (C) 2000-2019 the xine project
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
 * network buffering control
 */

#ifndef HAVE_NET_BUF_CTRL_H
#define HAVE_NET_BUF_CTRL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xine/input_plugin.h>

#define nbc_t xine_nbc_t
#define nbc_init(s) xine_nbc_init (s)
#define nbc_close(nbc) xine_nbc_close (nbc)

#ifdef __cplusplus
}
#endif

#endif /* HAVE_NET_BUF_CTRL_H */
