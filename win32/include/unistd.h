/* 
 * Copyright (C) 2000-2004 the xine project
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
 * unistd.h - This is mostly a catch all header that maps standard unix
 *            libc calls to the equivelent win32 functions. 
 *
 */

#include <windows.h>
#include <malloc.h>
#include <errno.h>
#include <direct.h>

#include "config.h"

#ifndef _SYS_UNISTD_H_
#define _SYS_UNISTD_H_

#define mkdir( A, B )	_mkdir( A )

#ifndef S_ISDIR
# define S_ISDIR(m) ((m) & _S_IFDIR)
#endif

#ifndef S_ISREG
#  define S_ISREG(m) ((m) & _S_IFREG)
#endif

#ifndef S_ISBLK
#  define S_ISBLK(m) 0
#endif

#ifndef S_ISCHR
#  define S_ISCHR(m) 0
#endif

#ifndef S_IXUSR
#  define S_IXUSR S_IEXEC
#endif

#ifndef S_IXGRP
#  define S_IXGRP S_IEXEC
#endif

#ifndef S_IXOTH
#  define S_IXOTH S_IEXEC
#endif

#define  M_PI			3.14159265358979323846  /* pi */

#define bzero( A, B ) memset( A, 0, B )

#define readlink(PATH, BUF, BUFSIZ) 0

#endif
