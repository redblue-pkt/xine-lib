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

/**
 * This file defines types for many events which can be sent in Xine.
 */

/**
 * Generic Event type.
 */
typedef struct {
  uint32_t type;  /* The event type (determines remainder of struct) */

  /* Event dependent data goes after this. */
} event_t;

/**
 * Mouse event.
 */
#define XINE_MOUSE_EVENT 0x0001
typedef struct {
  event_t event;
  uint8_t button; /* Generally 1 = left, 2 = mid, 3 = right */
  uint16_t x,y;   /* In Image space */
} mouse_event_t;

/**
 * Overlay event - used for plugins/UIs to request that a specific overlay be
 * displayed.
 */
#define XINE_OVERLAY_EVENT 0x0002
typedef struct overlay_event_s {
  event_t event;
  vo_overlay_t overlay;
} overlay_event_t;

/**
 * SPU event - send control events to the spu decoder
 */
#define XINE_SPU_EVENT 0x0003
typedef struct spu_event_s {
  event_t event;
  int sub_type;
  void *data;
} spu_event_t;

/**
 * UI event - send information to/from UI.
 */
#define XINE_UI_EVENT 0x0004
typedef struct ui_event_s {
  event_t event;
  int sub_type;
  void *data;
  uint32_t data_len;
  int handled;
} ui_event_t;

/* UI sub-types */

/* Warn Xine UI that spu/audio stream has changed and to 
 * update accordingly, data is unused. */
#define XINE_UI_UPDATE_CHANNEL  0x0001
/* UI asks for conversion of spu stream number into language.
 * if the listener can do it, it sets handled to 1 and writes
 * the string into data. data_len is how big this buffer is*/
#define XINE_UI_GET_SPU_LANG 0x0002
/* As above but for audio streams */
#define XINE_UI_GET_AUDIO_LANG 0x0003
/* Change the title label to the contents of the NULL-terminated
 * array of chars pointed to by data.
 */
#define XINE_UI_SET_TITLE 0x0004

/* EOF UI sub-types */

/**
 * MENU events
 */
#define XINE_MENU1_EVENT 0x0005
#define XINE_MENU2_EVENT 0x0006
#define XINE_MENU3_EVENT 0x0007

#ifdef __cplusplus
}
#endif

#endif /* HAVE_EVENTS_H */ 

