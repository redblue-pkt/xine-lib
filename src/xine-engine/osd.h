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
 * OSD stuff (text and graphic primitives)
 */

#ifndef HAVE_OSD_H
#define HAVE_OSD_H

#include "video_overlay.h"

typedef struct osd_object_s osd_object_t;

typedef struct osd_renderer_s osd_renderer_t;
typedef struct osd_font_s osd_font_t;

struct osd_renderer_s {

  /*
   * open a new osd object. this will allocated an empty (all zero) drawing
   * area where graphic primitives may be used.
   * It is ok to specify big width and height values. The render will keep
   * track of the smallest changed area to not generate too big overlays.
   * A default palette is initialized (i sugest keeping color 0 as transparent
   * for the sake of simplicity)
   */
  osd_object_t* (*new_object) (osd_renderer_t *this, int width, int height);

  /*
   * free osd object
   */
  void (*free_object) (osd_object_t *osd_to_close);


  /*
   * send the osd to be displayed at given pts (0=now)
   * the object is not changed. there may be subsequent drawing  on it.
   */
  int (*show) (osd_object_t *osd, uint32_t vpts );

  /*
   * send event to hide osd at given pts (0=now)
   * the object is not changed. there may be subsequent drawing  on it.
   */
  int (*hide) (osd_object_t *osd, uint32_t vpts );

  /*
   * Bresenham line implementation on osd object
   */
  void (*line) (osd_object_t *osd,
		int x1, int y1, int x2, int y2, int color );
  
  /*
   * filled retangle
   */
  void (*filled_rect) (osd_object_t *osd,
		       int x1, int y1, int x2, int y2, int color );

  /*
   * set palette (color and transparency)
   */
  void (*set_palette) (osd_object_t *osd, uint32_t *color, uint8_t *trans );

  /*
   * get palette (color and transparency)
   */
  void (*get_palette) (osd_object_t *osd, uint32_t *color, 
		       uint8_t *trans);

  /*
   * set position were overlay will be blended
   */
  void (*set_position) (osd_object_t *osd, int x, int y);

  /*
   * set the font of osd object
   */

  int (*set_font) (osd_object_t *osd, char *fontname, int size);


  /*
   * render text on x,y position (8 bits version)
   * no \n yet
   */
  int (*render_text) (osd_object_t *osd, int x1, int y1, 
		      char *text);

  /*
   * get width and height of how text will be renderized
   */
  int (*get_text_size) (osd_object_t *osd, char *text, 
			int *width, int *height);

  /* 
   * close osd rendering engine
   * loaded fonts are unloaded
   * osd objects are closed
   */
  void (*close) (osd_renderer_t *this);
  
  /* private stuff */

  pthread_mutex_t             osd_mutex;
  video_overlay_instance_t   *video_overlay;
  video_overlay_event_t       event;
  osd_object_t               *osds;
  osd_font_t                 *fonts;
};

/*
 *   initialize the osd rendering engine
 */
osd_renderer_t *osd_renderer_init( video_overlay_instance_t *video_overlay );

#endif

