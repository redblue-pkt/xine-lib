/*
 * Copyright (C) 2002-2003 the xine project
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
 * libmmsh public header
 */

#ifndef HAVE_MMSH_H
#define HAVE_MMSH_H

#include <inttypes.h>
#include <xine/xine_internal.h>

typedef struct mmsh_s mmsh_t;

char*    mmsh_connect_common(int *s ,int *port, char *url, char **host, char **path, char **file);
mmsh_t*  mmsh_connect (xine_stream_t *stream, const char *url_, int bandwidth);

int      mmsh_read (mmsh_t *this, char *data, int len);
uint32_t mmsh_get_length (mmsh_t *this);
void     mmsh_close (mmsh_t *this);

size_t   mmsh_peek_header (mmsh_t *this, char *data, size_t maxsize);

off_t    mmsh_get_current_pos (mmsh_t *this);

void     mmsh_set_start_time (mmsh_t *this, int time_offset);

#endif
