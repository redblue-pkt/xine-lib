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
 * $Id: video_out_x11.h,v 1.15 2002/03/21 18:29:51 miguelfreitas Exp $
 *
 * structs and defines specific to all x11 related output plugins
 * (any x11 base xine ui should include this)
 */

#ifndef HAVE_VIDEO_OUT_X11_H
#define HAVE_VIDEO_OUT_X11_H

#ifdef __cplusplus
extern "C" {
#endif

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

  void    *user_data;
  
  /*
   * dest size callback
   *
   * this will be called by the video driver to find out
   * how big the video output area size will be for a
   * given video size. The ui should _not_ adjust it's
   * video out area, just do some calculations and return 
   * the size. This will be called for every frame, ui 
   * implementation should be fast.
   */
  void (*dest_size_cb) (void *user_data,
			int video_width, int video_height, 
			int *dest_width, int *dest_height);

  /*
   * frame output callback
   *
   * this will be called by the video driver for every frame 
   * it's about to draw. ui can adapt it's size if necessary 
   * here.
   * note: the ui doesn't have to adjust itself to this
   * size, this is just to be taken as a hint.
   * ui must return the actual size of the video output
   * area and the video output driver will do it's best
   * to adjust the video frames to that size (while
   * preserving aspect ration and stuff). 
   *    dest_x, dest_y: offset inside window
   *    dest_width, dest_height: available drawing space
   *    win_x, win_y: window absolute screen position
   */

  void (*frame_output_cb) (void *user_data,
			   int video_width, int video_height,
			   int *dest_x, int *dest_y, 
			   int *dest_width, int *dest_height,
			   int *win_x, int *win_y);

} x11_visual_t;

/*
 * constants for gui_data_exchange's data_type parameter
 */

/* xevent *data */
#define GUI_DATA_EX_COMPLETION_EVENT       1
/* Drawable has changed */
#define GUI_DATA_EX_DRAWABLE_CHANGED       2
/* xevent *data */
#define GUI_DATA_EX_EXPOSE_EVENT           3
/* x11_rectangle_t *data */
#define GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO 4
/* int *data */
#define GUI_DATA_EX_VIDEOWIN_VISIBLE	   5

/* *data contains chosen visual, select a new one or change it to NULL
 * to indicate the visual to use or that no visual will work */
/* XVisualInfo **data */
#define GUI_SELECT_VISUAL                  8

#ifdef __cplusplus
}
#endif

#endif
