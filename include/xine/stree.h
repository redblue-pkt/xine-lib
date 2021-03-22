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
 * Xine string tree library.
 */

#ifndef HAVE_XINE_STREE_H
#define HAVE_XINE_STREE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <xine/attributes.h>
#include <xine/compat.h>

typedef enum {
  XINE_STREE_AUTO = 0,
  XINE_STREE_XML,  /** << eXtensible Markup Language */
  XINE_STREE_JSON, /** << Java script Serial Object Notation */
  XINE_STREE_URL,  /** << Uniform Resource Location encoding */
  XINE_STREE_LAST
} xine_stree_mode_t;

typedef struct {
  uint32_t next, prev;                      /** << xine_stree_t index */
  uint32_t first_child, last_child, parent; /** << xine_stree_t index */
  uint32_t num_children, level, index;      /** << int */
  uint32_t key, value;                      /** << offset into buf */
} xine_stree_t;

/** buf will be reused (modified) to hold the strings referenced by xine_stree_t.
 *  XINE_STREE_AUTO will update mode. */
xine_stree_t *xine_stree_load (char *buf, xine_stree_mode_t *mode) XINE_PROTECTED;

/** base is an index into the tree, where to start. */
void xine_stree_dump (const xine_stree_t *tree, const char *buf, uint32_t base) XINE_PROTECTED;

/** path is a dot separated list of parts.
 *  part is a name, a zero based index number in square brackets, or both. */
uint32_t xine_stree_find (const xine_stree_t *tree, const char *buf, const char *path, uint32_t base, int case_sens) XINE_PROTECTED;

void xine_stree_delete (xine_stree_t **tree) XINE_PROTECTED;

/** "%3a" -> ":" etc. return new strlen (). */
size_t xine_string_unpercent (char *s) XINE_PROTECTED;
/** "\n"     -> <newline>
 *  "\070"   -> "8"
 *  "\x37"   -> "7"
 *  "\ud575" -> "핵"
 *  "\cG"    -> <bell> (^G) */
size_t xine_string_unbackslash (char *s) XINE_PROTECTED;

#ifdef __cplusplus
}
#endif

#endif
