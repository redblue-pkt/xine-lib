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
 * WIN32 PORT,
 * by Matthew Grooms <elon@altavista.com>
 *
 * inttypes.h - Standard integer definitions.
 *
 */

#ifndef _SYS_INTTYPES_H_
#define _SYS_INTTYPES_H_

#define int8_t			signed char
#define int16_t			signed short
#define int32_t			signed long
#define int64_t			signed hyper

#define uint8_t			unsigned char
#define uint16_t		unsigned short
#define uint32_t		unsigned long
#define uint64_t		unsigned hyper

#define intptr_t		signed int *
#define uintptr_t		unsigned int *

#define __int8_t        int8_t
#define __int16_t       int16_t
#define __int32_t       int32_t
#define __int64_t       int64_t

#define __uint8_t       uint8_t
#define __uint16_t      uint16_t
#define __uint32_t      uint32_t
#define __uint64_t      uint64_t

#define __intptr_t      intptr_t
#define __uintptr_t     uintptr_t

typedef __int64         ulonglong;
typedef __int64         longlong;

#endif
