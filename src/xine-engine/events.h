/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * Copyright (C) Rich Wareham <richwareham@users.sourceforge.net> - July 2001
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
 */

#ifndef HAVE_EVENTS_H
#define HAVE_EVENTS_H

#include <inttypes.h>

/**
 * This file defines types for many events which can be sent in Xine.
 */

/**
 * Generic Event type.
 */
typedef struct event_s {
  uint32_t type;  /* The event type (determines remainder of struct) */

  /* Event dependent data goes after this. */
} event_t;

/**
 * Mouse event.
 */
#define XINE_MOUSE_EVENT 0x0001
typedef struct mouse_event_s {
  event_t event;
  uint8_t button; /* Generally 1 = left, 2 = mid, 3 = right */
  uint16_t x,y;   /* In Image space */
} mouse_event_t;

#endif /* HAVE_EVENTS_H */ 
