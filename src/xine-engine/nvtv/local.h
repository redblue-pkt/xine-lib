/* NVTV Local header -- Dirk Thierbach <dthierbach@gmx.de>
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
 * $Id: local.h,v 1.3 2003/05/04 01:35:06 hadess Exp $
 *
 * Contents:
 *
 * Header: Local declarations.
 *
 * - Defines for GTK2 vs. GTK.
 * - Define for Bool (must be included after xfree.h for this reason)
 *
 * Defines for all basic types, for 
 *   a) without X, b) with xfree.h, c) under windows.
 * Map allocations a) X to normal  b) normal to X
 */

#ifndef _LOCAL_H
#define _LOCAL_H

#include "config.h"
#include "debug.h"
#include "error.h"

/* -------- GTK -------- */

#ifdef HAVE_GTK

#if HAVE_GTK_VERSION == 1
#define my_gdk_screen gdk_screen
#define my_gdk_root_window gdk_root_window
#define my_gtk_spin_button_set_shadow_type(x, y) gtk_spin_button_set_shadow_type (x,y)
#endif

#if HAVE_GTK_VERSION == 2
#define my_gdk_screen gdk_x11_get_default_screen()
#define my_gdk_root_window gdk_x11_get_default_root_xwindow()
#define my_gtk_spin_button_set_shadow_type(x, y) 
#endif

#endif /* HAVE_GTK */

/* -------- Allocation layer -------- */

/* Simulate X via stdlib. nf means 'no failure' */

#define xalloc(_size) malloc(_size)
#define xnfcalloc(_num, _size) calloc(_num, _size)
#define xcalloc(_num, _size) calloc(_num, _size)
#define xfree(_ptr) free(_ptr)
#define xrealloc(_ptr, _size) realloc(_ptr, _size)

/* -------- Basic types -------- */

#ifndef _XDEFS_H

#define Bool int

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#endif /* _XDEFS_H */

#endif /* _LOCAL_H */
