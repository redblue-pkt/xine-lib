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
 * $Id: dxr3_scr.h,v 1.1 2002/05/06 11:26:37 jcdutton Exp $
 */

/* plugin structure */
typedef struct dxr3_scr_s {
  scr_plugin_t    scr_plugin;
  pthread_mutex_t mutex;
  
  int             fd_control; /* to access the dxr3 control device */
  
  int             priority;
  int64_t         offset;     /* difference between real scr and internal dxr3 clock */
  uint32_t        last_pts;   /* last known value of internal dxr3 clock to detect wrap around */
  int             scanning;   /* are we in a scanning mode */
} dxr3_scr_t;

/* plugin initialization function */
dxr3_scr_t *dxr3_scr_init(xine_t *xine);
