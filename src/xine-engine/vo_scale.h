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
 * $Id: vo_scale.h,v 1.1 2002/08/15 03:12:27 miguelfreitas Exp $
 * 
 * vo_scale.h
 *
 * keeps video scaling information
 */

#ifndef HAVE_VO_SCALE_H
#define HAVE_VO_SCALE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

typedef struct {
  int x, y;
  int w, h;
} vo_scale_rect_t;

struct vo_scale_s {

  /* display anatomy */
  double             display_ratio;        /* given by visual parameter
					      from init function              */

  /* true if driver supports frame zooming */
  int                support_zoom;
  
  /* forces direct mapping between frame pixels and screen pixels */
  int                scaling_disabled;
  
  /* size / aspect ratio calculations */

  /* 
   * "delivered" size:
   * frame dimension / aspect as delivered by the decoder
   * used (among other things) to detect frame size changes
   * units: frame pixels
   */
  int                delivered_width;   
  int                delivered_height;     
  int                delivered_ratio_code;

  /* 
   * displayed part of delivered images,
   * taking zoom into account
   * units: frame pixels
   */
  int                displayed_xoffset;
  int                displayed_yoffset;
  int                displayed_width;
  int                displayed_height;
  double             zoom_factor_x, zoom_factor_y;

  /* 
   * "ideal" size :
   * delivered width/height corrected by aspect ratio and display_ratio
   * units: screen pixels
   */
  int                ideal_width, ideal_height;
  int                user_ratio;

  /*
   * "gui" size / offset:
   * what gui told us about where to display the video
   * units: screen pixels
   */
  int                gui_x, gui_y;
  int                gui_width, gui_height;
  int                gui_win_x, gui_win_y;
  
  /*
   * "output" size:
   *
   * this is finally the ideal size "fitted" into the
   * gui size while maintaining the aspect ratio
   * units: screen pixels
   */
  int                output_width;
  int                output_height;
  int                output_xoffset;
  int                output_yoffset;

  /* */
  int                force_redraw;
  

  /* gui callbacks */

  void              *user_data;
  void (*frame_output_cb) (void *user_data,
			   int video_width, int video_height,
			   int *dest_x, int *dest_y, 
			   int *dest_height, int *dest_width,
			   int *win_x, int *win_y);
  
  void (*dest_size_cb) (void *user_data,
			int video_width, int video_height, 
			int *dest_width, int *dest_height);

  /* borders */
  vo_scale_rect_t     border[4];
};

typedef struct vo_scale_s vo_scale_t; 


/*
 * convert delivered height/width to ideal width/height
 * taking into account aspect ratio and zoom factor
 */

void vo_scale_compute_ideal_size (vo_scale_t *this);


/*
 * make ideal width/height "fit" into the gui
 */

void vo_scale_compute_output_size (vo_scale_t *this);

/*
 * return true if a redraw is needed due resizing, zooming,
 * aspect ratio changing, etc.
 */

int vo_scale_redraw_needed (vo_scale_t *this);

/*
 *
 */
 
void vo_scale_translate_gui2video(vo_scale_t *this,
				 int x, int y,
				 int *vid_x, int *vid_y);

/*
 * Returns description of a given ratio code
 */

char *vo_scale_aspect_ratio_name(int a);

/* 
 * initialize rescaling struct
 */
 
void vo_scale_init(vo_scale_t *this, double display_ratio,
                  int support_zoom, int scaling_disabled );

#ifdef __cplusplus
}
#endif

#endif

