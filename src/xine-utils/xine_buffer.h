/* 
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: xine_buffer.h,v 1.2 2002/12/24 00:59:36 holstsn Exp $
 *
 *
 * generic dynamic buffer functions. The goals
 * of these functions are (in fact many of these points
 * are todos):
 * - dynamic allocation and reallocation depending
 *   on the size of data written to it.
 * - fast and transparent access to the data.
 *   The user sees only the raw data chunk as it is
 *   returned by the well-known malloc function.
 *   This is necessary since not all data-accessing
 *   functions can be wrapped here.
 * - some additional health checks are made during
 *   development (eg boundary checks after direct
 *   access to a buffer). This can be turned off in
 *   production state for higher performance.
 * - A lot of convenient string and memory manipulation
 *   functions are implemented here, where the user
 *   do not have to care about memory chunk sizes.
 * - Some garbage collention could be implemented as well;
 *   i think of a global structure containing infos
 *   about all allocated chunks. This must be implemented
 *   in a thread-save way...
 *
 * Here are some drawbacks (aka policies):
 * - The user must not pass indexed buffers to xine_buffer_*
 *   functions.
 * - The pointers passed to xine_buffer_* functions may change
 *   (eg during reallocation). The user must respect that.
 */

#ifndef HAVE_XINE_BUFFER_H
#define HAVE_XINE_BUFFER_H

#include <inttypes.h>

/*
 * returns an initialized pointer to a buffer.
 * The buffer will be allocated in blocks of
 * chunk_size bytes. This will prevent permanent
 * reallocation on slow growing buffers.
 */
void *xine_buffer_init(int chunk_size);

/*
 * frees a buffer, the macro ensures, that a freed
 * buffer pointer is set to NULL
 */
#define xine_buffer_free(buf) buf=_xine_buffer_free(buf)
void *_xine_buffer_free(void *buf);

/*
 * duplicates a buffer
 */
void *xine_buffer_dup(void *buf);

/*
 * will copy len bytes of data into buf at position index.
 */
#define xine_buffer_copyin(buf,i,data,len) \
  buf=_xine_buffer_copyin(buf,i,data,len)
void *_xine_buffer_copyin(void *buf, int index, const void *data, int len);

/*
 * will copy len bytes out of buf+index into data.
 * no checks are made in data. It is treated as an ordinary
 * user-malloced data chunk.
 */
void xine_buffer_copyout(void *buf, int index, void *data, int len);

/*
 * set len bytes in buf+index to b.
 */
#define xine_buffer_set(buf,i,b,len) \
  buf=_xine_buffer_set(buf,i,b,len)
void *_xine_buffer_set(void *buf, int index, uint8_t b, int len);

/*
 * concatnates given buf (which schould contain a null terminated string)
 * with another string.
 */
#define xine_buffer_strcat(buf,data) \
  buf=_xine_buffer_strcat(buf,data)
void *_xine_buffer_strcat(void *buf, char *data);

/*
 * copies given string to buf+index
 */
#define xine_buffer_strcpy(buf,index,data) \
  buf=_xine_buffer_strcpy(buf,index,data)
void *_xine_buffer_strcpy(void *buf, int index, char *data);

/*
 * returns a pointer to the first occurence of ch.
 * note, that the returned pointer cannot be used
 * in any other xine_buffer_* functions.
 */
char *xine_buffer_strchr(void *buf, int ch);

/*
 * get allocated memory size
 */
int xine_buffer_get_size(void *buf);

/*
 * ensures a specified buffer size if the user want to
 * write directly to the buffer. Normally the special
 * access functions defined here should be used.
 */
#define xine_buffer_ensure_size(buf,data) \
  buf=_xine_buffer_ensure_size(buf,data)
void *_xine_buffer_ensure_size(void *buf, int size);

#endif
