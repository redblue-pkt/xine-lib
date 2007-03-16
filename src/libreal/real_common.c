/*
 * Copyright (C) 2000-2007 the xine project
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: real_common.c,v 1.2 2007/03/16 20:21:40 dgp85 Exp $
 *
 * Common function for the thin layer to use Real binary-only codecs in xine
 */

#define LOG_MODULE "real_common"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "real_common.h"

#ifdef __alpha__

void *__builtin_new(size_t size) {
  return malloc(size);
}

void __builtin_delete (void *foo) {
  /* printf ("libareal: __builtin_delete called\n"); */
  free (foo);
}

void *__builtin_vec_new(size_t size) {
  return malloc(size);
}

void __builtin_vec_delete(void *mem) {
  free(mem);
}

void __pure_virtual(void) {
  lprintf("libreal: FATAL: __pure_virtual() called!\n");
  /*      exit(1); */
}

#endif

#ifdef __FreeBSD__ /* TODO: alias them if at all possible */
void ___brk_addr(void) { exit(0); }
void __ctype_b(void) { exit(0); }
#endif
