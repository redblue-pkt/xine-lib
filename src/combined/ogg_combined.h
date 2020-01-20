/*
 * Copyright (C) 2013-2018 the xine project
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
 */

#include <xine/os_types.h>
#include <xine/attributes.h>

void *vorbis_init_plugin (xine_t *xine, const void *data);
void *speex_init_plugin (xine_t *xine, const void *data);
void *theora_init_plugin (xine_t *xine, const void *data);
void *anx_init_class (xine_t *xine, const void *data);
void *ogg_init_class (xine_t *xine, const void *data);
