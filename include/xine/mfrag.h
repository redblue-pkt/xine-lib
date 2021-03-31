/*
 * Copyright (C) 2021 the xine project
 * Copyright (C) 2021 Torsten Jager <t.jager@gmx.de>
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
 * Xine media fragment list utility.
 */

#ifndef HAVE_XINE_MFRAG_H
#define HAVE_XINE_MFRAG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <xine/attributes.h>
#include <xine/compat.h>

/** -1: error.
 *  0: the stream head.
 *    dur = timebase.
 *    len = byte offset of first media fragment or 0.
 *  1..n: a media fragment.
 *    dur = duration in timebase units or -1 (no change).
 *    len = length in bytes or -1 (unknown/no change).
 *  n + 1: append this one (set) or the stream total (get/find). */
typedef int32_t xine_mfrag_index_t;

typedef struct xine_mfrag_list_s xine_mfrag_list_t;

/** *plist may be NULL. */
void xine_mfrag_list_open (xine_mfrag_list_t **plist) XINE_PROTECTED;

int xine_mfrag_set_index_frag (xine_mfrag_list_t *list, xine_mfrag_index_t index, int64_t dur, off_t len) XINE_PROTECTED;

/** returns the "n" above. */
int32_t xine_mfrag_get_frag_count (xine_mfrag_list_t *list) XINE_PROTECTED;

xine_mfrag_index_t xine_mfrag_find_time (xine_mfrag_list_t *list, int64_t timepos) XINE_PROTECTED;
xine_mfrag_index_t xine_mfrag_find_pos (xine_mfrag_list_t *list, off_t offs) XINE_PROTECTED;

int xine_mfrag_get_index_frag (xine_mfrag_list_t *list, xine_mfrag_index_t index, int64_t *dur, off_t *len) XINE_PROTECTED;
int xine_mfrag_get_index_start (xine_mfrag_list_t *list, xine_mfrag_index_t index, int64_t *timepos, off_t *offs) XINE_PROTECTED;

void xine_mfrag_list_close (xine_mfrag_list_t **plist) XINE_PROTECTED;

#ifdef __cplusplus
}
#endif

#endif
