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

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

/*
 * This file defines types for many events which can be sent in xine.
 */

/* event types */

#define XINE_EVENT_MOUSE_BUTTON          1
#define XINE_EVENT_MOUSE_MOVE            2
#define XINE_EVENT_SPU_BUTTON            3
#define XINE_EVENT_SPU_CLUT              4
#define XINE_EVENT_UI_CHANNELS_CHANGED   5 /* inform ui that new channel info is available */
#define XINE_EVENT_UI_SET_TITLE          6 /* request title display change in ui */
#define XINE_EVENT_INPUT_MENU1           7
#define XINE_EVENT_INPUT_MENU2           8
#define XINE_EVENT_INPUT_MENU3           9
#define XINE_EVENT_INPUT_UP             10
#define XINE_EVENT_INPUT_DOWN           11
#define XINE_EVENT_INPUT_LEFT           12
#define XINE_EVENT_INPUT_RIGHT          13
#define XINE_EVENT_INPUT_SELECT         14
#define XINE_EVENT_PLAYBACK_FINISHED    15
#define XINE_EVENT_BRANCHED             16
#define XINE_EVENT_NEED_NEXT_MRL        17 
#define XINE_EVENT_INPUT_NEXT           18
#define XINE_EVENT_INPUT_PREVIOUS       19
#define XINE_EVENT_INPUT_ANGLE_NEXT     20
#define XINE_EVENT_INPUT_ANGLE_PREVIOUS 21


/*
 * generic event type.
 */
typedef struct {
  uint32_t type;  /* The event type (determines remainder of struct) */

  /* Event dependent data goes after this. */
} xine_event_t;

/*
 * input events
 */
typedef struct {
  xine_event_t     event;
  uint8_t          button; /* Generally 1 = left, 2 = mid, 3 = right */
  uint16_t         x,y;    /* In Image space */
} xine_input_event_t;

/*
 * SPU event - send control events to the spu decoder
 */
typedef struct {
  xine_event_t     event;
  void            *data;
} xine_spu_event_t;

/*
 * UI event - send information to/from UI.
 */

typedef struct {
  xine_event_t     event;
  void            *data;
  uint32_t         data_len;
  int              handled;
} xine_ui_event_t;

/*
 * next_mrl
 */
typedef struct {
  xine_event_t     event;
  char            *mrl;
  int              handled;
} xine_next_mrl_event_t;


#ifdef __cplusplus
}
#endif

#endif /* HAVE_EVENTS_H */ 

