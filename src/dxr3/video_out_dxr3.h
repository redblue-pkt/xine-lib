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
 * $Id: video_out_dxr3.h,v 1.3 2002/06/30 10:47:06 mroi Exp $
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <X11/Xlib.h>

#include "xine_internal.h"
#include "dxr3.h"

/* values for fd_video indicating why it is closed */
#define CLOSED_FOR_DECODER -1
#define CLOSED_FOR_ENCODER -2

/* the number of supported encoders */
#define SUPPORTED_ENCODER_COUNT 2


/* plugin structures */
typedef struct encoder_data_s encoder_data_t;

typedef enum { ENC_FAME, ENC_RTE } encoder_type;


struct coeff {
  float            k,m;
};

typedef struct dxr3_overlay_s {
  int              fd_control;

  int              xoffset;
  int              yoffset;
  int              xcorr;
  int              jitter;
  int              stability;
  int              colorkey;
  float            color_interval;
  int              screen_xres;
  int              screen_yres;
  int              screen_depth;

  struct coeff     colcal_upper[3];
  struct coeff     colcal_lower[3];
} dxr3_overlay_t;

typedef struct dxr3_driver_s {
  vo_driver_t      vo_driver;
  config_values_t *config;

  char             devname[128];
  char             devnum[3];
  int              fd_control;
  int              fd_video;      /* to access the relevant dxr3 devices */
  
  int              enhanced_mode;
  int              swap_fields;   /* swap fields */
  int              add_bars;      /* add black bars to correct a.r. */
  
  int              aspect;
  int              tv_mode;
  int              pan_scan;
  int              overlay_enabled;
  int              tv_switchable; /* can switch from overlay<->tvout */
  em8300_bcs_t     bcs;

  encoder_data_t  *enc;           /* encoder data */
  int              format;        /* color format */
  int              video_iheight; /* input height (before adding black bars) */
  int              video_oheight; /* output height (after adding bars) */
  int              video_width;
  int              video_aspect;
  int              top_bar;       /* the height of the upper black bar */
  int              need_redraw;   /* the image on screen needs redrawing */
  int              need_update;   /* the mpeg encoder needs to be updated */

  dxr3_overlay_t   overlay;
  Display         *display;
  Drawable         win;
  GC               gc;
  XColor           color;
  int              xpos, ypos;
  int              width, height; 

  char            *user_data;
  void           (*frame_output_cb)(void *user_data,
                     int video_width, int video_height,
                     int *dest_x, int *dest_y,
                     int *dest_height, int *dest_width,
                     int *win_x, int *win_y);
} dxr3_driver_t;

typedef struct dxr3_frame_s {
  vo_frame_t       vo_frame;
  int              width, iheight, oheight;
  uint8_t         *mem;           /* allocated for YV12 or YUY2 buffers */
  uint8_t         *real_base[3];  /* yuv/yuy2 buffers in mem aligned on 16 */
  int              swap_fields;   /* shifts Y buffer one line to exchange odd/even lines */
} dxr3_frame_t;

struct encoder_data_s {
  encoder_type     type;
  int            (*on_update_format)(dxr3_driver_t *, dxr3_frame_t *);
  int            (*on_frame_copy)(dxr3_driver_t *, dxr3_frame_t *, uint8_t **src);
  int            (*on_display_frame)(dxr3_driver_t *, dxr3_frame_t *);
  int            (*on_close)(dxr3_driver_t *);
}; 

/* encoder plugins initialization functions */
#ifdef HAVE_LIBRTE
int dxr3_rte_init(dxr3_driver_t *);
#endif
#ifdef HAVE_LIBFAME
int dxr3_fame_init(dxr3_driver_t *);
#endif
