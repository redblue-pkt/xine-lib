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
 * $Id: local.h,v 1.1 2003/01/18 15:29:22 miguelfreitas Exp $
 *
 * Contents:
 *
 * Header: Local declarations.
 *
 * - Defines for GTK2 vs. GTK.
 * - Define for Bool (must be included after xfree.h for this reason)
 *
 */

#ifndef _LOCAL_H
#define _LOCAL_H

#ifdef HAVE_GTK

#if GTK_MAJOR_VERSION >= 2

#define gdk_screen gdk_x11_get_default_screen()
#define gdk_root_window gdk_x11_get_default_root_xwindow()
#define gtk_spin_button_set_shadow_type(x, y) 

#endif
#endif /* HAVE_GTK */

#ifndef _XDEFS_H
typedef int Bool;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#endif /* _XDEFS_H */

#endif /* _LOCAL_H */
