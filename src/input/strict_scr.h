/* 
 * Copyright (C) 2002 the xine project
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
 * scr plugin that may not allow others to adjust it (used for streaming)
 */

#ifndef HAVE_STRICT_SCR_H
#define HAVE_STRICT_SCR_H

#include "metronom.h"

typedef struct strictscr_s {
  scr_plugin_t     scr;

  struct timeval   cur_time;
  uint32_t         cur_pts;
  double           speed_factor;

  int              adjustable;

  pthread_mutex_t  lock;

} strictscr_t;

strictscr_t* strictscr_init () ;

#endif

