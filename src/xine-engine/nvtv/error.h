/* NVTV error header -- Dirk Thierbach <dthierbach@gmx.de>
 *
 * This file is part of nvtv, a tool for tv-output on NVidia cards.
 * 
 * nvtv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * nvtv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: error.h,v 1.1 2003/02/05 00:14:03 miguelfreitas Exp $
 *
 * Contents:
 *
 * Error handling.
 *
 */

#ifndef _ERROR_H
#define _ERROR_H

#include "debug.h"

/*
 * This header file must do the following:
 *
 * 1) Define error macros for standalone usage
 * 2) Map error macros to XFree error macros if compiled under X
 * 3) Map error macros and XFree macros function names for Borland C++, 
 *    as the compiler does not support macros with a variable number
 *    of arguments.
 */


#ifdef __BORLANDC__

#include <stdarg.h>

int dprintf (char *format, ...);
int errorf (char *format, ...);
int errorf1 (int arg1, char *format, ...);
int errorf2 (int arg1, int arg2, char *format, ...);

#define ERROR errorf
#define FPRINTF errorf
#define DPRINTF dprintf
#define xf86Msg errorf1
#define xf86DrvMsg errorf2

#else /* __BORLANDC__ */

/* -------- */

#define ERROR(X...) fprintf(stderr, X)

/* Fake output */
#define FPRINTF(X...) fprintf(stderr, X)

#ifdef NVTV_DEBUG
#define DPRINTF(X...) fprintf(stderr, X)
#else
#define DPRINTF(X...) /* */
#endif

/* -------- */

#define xf86Msg(type,format,args...) /* fprintf(stderr,format,args) */
#define xf86DrvMsg(scrnIndex,type,format, args...) /* fprintf(stderr,format,args) */

#endif /* __BORLANDC__ */

#define DEBUG(x) /*x*/
#define ErrorF ERROR

#endif /* _ERROR_H */

