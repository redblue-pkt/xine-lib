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
 * $Id: video_out_x11.h,v 1.5 2001/05/28 13:42:02 guenter Exp $
 *
 * structs and defines specific to all x11 related output plugins
 * (any x11 base xine ui should include this)
 */

#ifndef HAVE_VIDEO_OUT_X11_H
#define HAVE_VIDEO_OUT_X11_H

typedef struct {

  /* area of that drawable to be used by video */
  int      x,y,w,h;

} x11_rectangle_t;

/*
 * this is the visual data struct any x11 gui should supply
 * (pass this to init_video_out_plugin or the xine_load_video_output_plugin
 * utility function)
 */

typedef struct {

  /* some information about the display */
  Display *display;
  int      screen;
  double   display_ratio;

  /* drawable to display the video in/on */
  Drawable d;
  
  /*
   * calc dest size
   *
   * this will be called by the video driver to find out
   * how big the video output area size will be for a
   * given video size. The ui should _not_ adjust it's
   * video out area, just do some calculations and return 
   * the size
   */
  void (*calc_dest_size) (int video_width, int video_height, 
			  int *dest_width, int *dest_height);

  /*
   * request dest size
   *
   * this will be called by the video driver to request
   * the video output area to be resized to fit the video.
   * note: the ui doesn't have to adjust itself to this
   * size, this is just to be taken as a hint.
   * ui must return the actual size of the video output
   * area and the video output driver will do it's best
   * to adjust the video frames to that size (while
   * preserving aspect ration and stuff). 
   */

  void (*request_dest_size) (int video_width, int video_height,
			     int *dest_x, int *dest_y, 
			     int *dest_width, int *dest_height);

} x11_visual_t;

/*
 * constants for gui_data_exhange's data_type parameter
 */

/* x11_rectangle_t *data */
#define GUI_DATA_EX_DEST_POS_SIZE_CHANGED 0
/* xevent *data */
#define GUI_DATA_EX_COMPLETION_EVENT      1
/* Drawable has changed */
#define GUI_DATA_EX_DRAWABLE_CHANGED      2
/* xevent *data */
#define GUI_DATA_EX_EXPOSE_EVENT          3

#endif
