/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * This file is part of xine, a unix video player.
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
 * $Id: utils.h,v 1.1 2001/10/22 00:52:10 guenter Exp $
 *
 */
#ifndef HAVE_UTILS_H
#define HAVE_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

void *xmalloc(size_t size);

void *xmalloc_aligned(size_t alignment, size_t size);

const char *get_homedir(void);

/*
 * Clean a string (remove spaces and '=' at the begin,
 * and '\n', '\r' and spaces at the end.
 */

char *chomp (char *str);

/*
 * A thread-safe usecond sleep
 */
void xine_usec_sleep(unsigned usec);

#ifdef __cplusplus
}
#endif

#endif
