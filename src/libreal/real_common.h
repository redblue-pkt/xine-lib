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
 * $Id: real_common.h,v 1.2 2007/03/16 20:21:40 dgp85 Exp $
 *
 * Common function for the thin layer to use Real binary-only codecs in xine
 */

#ifndef __REAL_COMMON_H__
#define __REAL_COMMON_H__

#include "xine_internal.h"

/*
 * some fake functions to make real codecs happy 
 * These are, on current date (20070316) needed only for Alpha
 * codecs.
 * As they are far from being proper replacements, define them only there
 * until new codecs are available there too.
 */
#ifdef __alpha__

void *__builtin_new(size_t size);
void __builtin_delete (void *foo);
void *__builtin_vec_new(size_t size) EXPORTED;
void __builtin_vec_delete(void *mem) EXPORTED;
void __pure_virtual(void) EXPORTED;

#endif

#ifdef __FreeBSD__
 #ifdef SUPPORT_ATTRIBUTE_ALIAS
char **__environ __attribute__((weak, alias("environ")));
FILE *stderr __attribute__((weak, alias("__stderrp")));

 #endif
void ___brk_addr(void) EXPORTED;
void __ctype_b(void) EXPORTED;
#endif

#endif
