/* 
 * Copyright (C) 2000-2003 the xine project
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * $Id: video_out_pgx64.c,v 1.49 2003/12/18 00:30:19 komadori Exp $
 *
 * video_out_pgx64.c, Sun PGX64/PGX24 output plugin for xine
 *
 * written and currently maintained by Robin Kay <komadori@myrealbox.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/fbio.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "xine_internal.h"
#include "alphablend.h"
#include "bswap.h"
#include "vo_scale.h"
#include "xineutils.h"

#define FB_ADDRSPACE 0x800000
#define FB_REGSBASE  0x1ffe00

#define BUS_CNTL 0x128
#define BUS_EXT_REG_EN 0x08000000

#define SCALER_H_COEFF0 0x055
#define SCALER_H_COEFF0_DEFAULT 0x00002000
#define SCALER_H_COEFF1 0x056
#define SCALER_H_COEFF1_DEFAULT 0x0D06200D
#define SCALER_H_COEFF2 0x057
#define SCALER_H_COEFF2_DEFAULT 0x0D0A1C0D
#define SCALER_H_COEFF3 0x058
#define SCALER_H_COEFF3_DEFAULT 0x0C0E1A0C
#define SCALER_H_COEFF4 0x059
#define SCALER_H_COEFF4_DEFAULT 0x0C14140C

#define OVERLAY_GRAPHICS_KEY_CLR 0x004
#define OVERLAY_GRAPHICS_KEY_MSK 0x005
#define OVERLAY_KEY_CNTL 0x006
#define OVERLAY_KEY_EN 0x00000050
#define SCALER_COLOUR_CNTL 0x054

#define SCALER_BUF0_OFFSET 0x00D
#define SCALER_BUF0_OFFSET_U 0x075
#define SCALER_BUF0_OFFSET_V 0x076
#define SCALER_BUF1_OFFSET 0x00E
#define SCALER_BUF1_OFFSET_U 0x077
#define SCALER_BUF1_OFFSET_V 0x078
#define SCALER_BUF_PITCH 0x00F
#define VIDEO_FORMAT 0x012
#define VIDEO_FORMAT_YUV12 0x000A0000
#define VIDEO_FORMAT_YUY2 0x000B0000
#define CAPTURE_CONFIG 0x014
#define CAPTURE_CONFIG_BUF0 0x00000000
#define CAPTURE_CONFIG_BUF1 0x20000000

#define OVERLAY_X_Y_START 0x000
#define OVERLAY_X_Y_END 0x001
#define OVERLAY_X_Y_LOCK 0x80000000
#define OVERLAY_SCALE_INC 0x008
#define SCALER_HEIGHT_WIDTH 0x00A
#define OVERLAY_EXCLUSIVE_HORZ 0x016
#define OVERLAY_EXCLUSIVE_VERT 0x017
#define OVERLAY_EXCLUSIVE_EN 0x80000000
#define OVERLAY_SCALE_CNTL 0x009
#define OVERLAY_SCALE_EN 0xC0000000

static const int scaler_regs_table[2][3] = {
  {SCALER_BUF0_OFFSET, SCALER_BUF0_OFFSET_U, SCALER_BUF0_OFFSET_V},
  {SCALER_BUF1_OFFSET, SCALER_BUF1_OFFSET_U, SCALER_BUF1_OFFSET_V}
};

#define BUF_MODE_MULTI  0
#define BUF_MODE_NONE   1
#define BUF_MODE_SINGLE 2
#define BUF_MODE_DOUBLE 3

#define OVL_MODE_ALPHA_BLEND 0
#define OVL_MODE_EXCLUSIVE   1
#define OVL_MODE_CHROMA_KEY  2

static const char *overlay_modes[] = {
  "alpha blend to normal overlay",
  "alpha blend to exclusive overlay",
  "chroma key graphics",
  NULL
};

#define MAX_DETAINED_FRAMES 10

struct pgx64_overlay_s {
  int x, y, width, height;
  Pixmap p;
  struct pgx64_overlay_s *next;
};
typedef struct pgx64_overlay_s pgx64_overlay_t;

typedef struct {
  video_driver_class_t vo_driver_class;

  xine_t *xine;
  config_values_t *config;

  pthread_mutex_t mutex;
  int instance_count;
} pgx64_driver_class_t;

typedef struct {
  vo_frame_t vo_frame;

  int lengths[3], stripe_lengths[3], stripe_offsets[3], buffers[3];
  int width, height, pitch, format, native_format, planes;
  double ratio;
  uint8_t *buffer_ptrs[3];
} pgx64_frame_t;

typedef struct {   
  vo_driver_t vo_driver;
  vo_scale_t vo_scale;

  pgx64_driver_class_t *class;
  pgx64_frame_t *current, *detained[MAX_DETAINED_FRAMES];

  Display *display;
  int screen, depth;
  Drawable drawable;
  GC gc;
  Visual *visual;
  Colormap cmap;

  int fbfd, free_top, free_bottom, free_mark, fb_width, fb_height, fb_depth;
  int buf_mode, dblbuf_select, detained_frames, buffers[2][3];
  uint8_t *vram, *buffer_ptrs[2][3];
  volatile uint32_t *vregs;

  int ovl_mode, ovl_changed, ovl_regen_needed;
  pthread_mutex_t ovl_mutex;
  pgx64_overlay_t *first_overlay;


  int delivered_format, colour_key, brightness, saturation;
  int deinterlace_en, multibuf_en;
} pgx64_driver_t;

/*
 * Dispose of any free_mark key data within a pgx64_frame_t
 */

static void dispose_frame_internals(pgx64_frame_t *frame)
{
  if (frame->vo_frame.base[0]) {
    free(frame->vo_frame.base[0]);
    frame->vo_frame.base[0] = NULL;
  }
  if (frame->vo_frame.base[1]) {
    free(frame->vo_frame.base[1]);
    frame->vo_frame.base[1] = NULL;
  }
  if (frame->vo_frame.base[2]) {
    free(frame->vo_frame.base[2]);
    frame->vo_frame.base[2] = NULL;
  }
}

/*
 * Paint the output area with black borders, colour key, and any chorma keyed
 * overlays
 */

static void draw_overlays(pgx64_driver_t *this)
{
  pgx64_overlay_t *ovl;

  ovl = this->first_overlay;
  XLockDisplay(this->display);
  while (ovl != NULL) {
    XCopyArea(this->display, ovl->p, this->drawable, this->gc, 0, 0, ovl->width, ovl->height, this->vo_scale.output_xoffset + ovl->x, this->vo_scale.output_yoffset + ovl->y);
    ovl = ovl->next;
  }
  XFlush(this->display);
  XUnlockDisplay(this->display);
}

static void repaint_output_area(pgx64_driver_t *this)
{
  int i;

  XLockDisplay(this->display);
  XSetForeground(this->display, this->gc, BlackPixel(this->display, this->screen));
  for (i=0; i<4; i++) {
    XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.border[i].x, this->vo_scale.border[i].y, this->vo_scale.border[i].w, this->vo_scale.border[i].h);
  }

  XSetForeground(this->display, this->gc, this->colour_key);
  XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.output_xoffset, this->vo_scale.output_yoffset, this->vo_scale.output_width, this->vo_scale.output_height);
  XFlush(this->display);
  XUnlockDisplay(this->display);

  pthread_mutex_lock(&this->ovl_mutex);
  if (this->ovl_mode == OVL_MODE_CHROMA_KEY) {
    draw_overlays(this);
  }
  pthread_mutex_unlock(&this->ovl_mutex);
}

/*
 * Reset video memory allocator and release detained frames
 */

static void vram_reset(pgx64_driver_t* this)
{
  int i;

  this->free_mark = this->free_top;

  for (i=0; i<this->detained_frames; i++) {
    this->detained[i]->vo_frame.free(&this->detained[i]->vo_frame);
  }
  this->detained_frames = 0;
}

/*
 * Allocate a portion of video memory
 */

static int vram_alloc(pgx64_driver_t* this, int size)
{
  if (this->free_mark - size < this->free_bottom) {
    return -1;
  }
  else {
    this->free_mark -= size;
    return this->free_mark;
  }
}

/*
 * XINE VIDEO DRIVER FUNCTIONS
 */

static void pgx64_frame_proc_frame(vo_frame_t *frame_gen)
{
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;
  int i;

  frame->vo_frame.proc_called = 1;

  for (i=0; i<frame->planes; i++) {
    memcpy(frame->buffer_ptrs[i], frame->vo_frame.base[i], frame->lengths[i]);
  }
}

static void pgx64_frame_proc_slice(vo_frame_t *frame_gen, uint8_t **src)
{
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;
  int i, len;

  frame->vo_frame.proc_called = 1;

  for (i=0; i<frame->planes; i++) {
    len = (frame->lengths[i] - frame->stripe_offsets[i] < frame->stripe_lengths[i]) ? frame->lengths[i] - frame->stripe_offsets[i] : frame->stripe_lengths[i];
    memcpy(frame->buffer_ptrs[i]+frame->stripe_offsets[i], src[i], len);
    frame->stripe_offsets[i] += len;
  }
}

static void pgx64_frame_field(vo_frame_t *frame_gen, int which_field)
{
  /*pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;*/
}

static void pgx64_frame_dispose(vo_frame_t *frame_gen)
{
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

  dispose_frame_internals(frame);
  free(frame);
}

static uint32_t pgx64_get_capabilities(vo_driver_t *this_gen)
{
  /*pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;*/

  return VO_CAP_YV12 |
         VO_CAP_YUY2;
}

static vo_frame_t* pgx64_alloc_frame(vo_driver_t *this_gen)
{
  /*pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;*/
  pgx64_frame_t *frame;

  frame = (pgx64_frame_t *) xine_xmalloc(sizeof(pgx64_frame_t));
  if (!frame)
    return NULL;

  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.field      = pgx64_frame_field;
  frame->vo_frame.dispose    = pgx64_frame_dispose;

  return (vo_frame_t *)frame;
}

static void pgx64_update_frame_format(vo_driver_t *this_gen, vo_frame_t *frame_gen, uint32_t width, uint32_t height, double ratio, int format, int flags)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

  if ((width != frame->width) ||
      (height != frame->height) ||
      (ratio != frame->ratio) ||
      (format != frame->format)) {
    int i;

    dispose_frame_internals(frame);

    frame->width = width;
    frame->height = height;
    frame->ratio = ratio;
    frame->format = format;
    frame->pitch = ((width + 7) / 8) * 8;

    frame->vo_frame.proc_frame = NULL; 
    frame->vo_frame.proc_slice = NULL; 

    switch (format) {
      case XINE_IMGFMT_YUY2:
        frame->native_format = VIDEO_FORMAT_YUY2;
        frame->planes = 1;
        frame->vo_frame.pitches[0] = frame->pitch * 2;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->stripe_lengths[0] = frame->vo_frame.pitches[0] * 16;
        frame->vo_frame.base[0] = memalign(8, frame->lengths[0]);
      break;

      case XINE_IMGFMT_YV12:
        frame->native_format = VIDEO_FORMAT_YUV12;
        frame->planes = 3;
        frame->vo_frame.pitches[0] = frame->pitch;
        frame->vo_frame.pitches[1] = ((width + 15) / 16) * 8;
        frame->vo_frame.pitches[2] = ((width + 15) / 16) * 8;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->lengths[1] = frame->vo_frame.pitches[1] * ((height + 1) / 2);
        frame->lengths[2] = frame->vo_frame.pitches[2] * ((height + 1) / 2);
        frame->stripe_lengths[0] = frame->vo_frame.pitches[0] * 16;
        frame->stripe_lengths[1] = frame->vo_frame.pitches[1] * 8;
        frame->stripe_lengths[2] = frame->vo_frame.pitches[2] * 8;
        frame->vo_frame.base[0] = memalign(8, frame->lengths[0]);
        frame->vo_frame.base[1] = memalign(8, frame->lengths[1]);
        frame->vo_frame.base[2] = memalign(8, frame->lengths[2]);
      break;
    }

    for (i=0; i<frame->planes; i++) {
      if (!frame->vo_frame.base[i]) {
        xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: frame plane malloc failed\n");
        abort();
      }
    }
  }

  frame->stripe_offsets[0] = 0;
  frame->stripe_offsets[1] = 0;
  frame->stripe_offsets[2] = 0;
}

static void pgx64_display_frame(vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

  if ((frame->width != this->vo_scale.delivered_width) ||
      (frame->height != this->vo_scale.delivered_height) ||
      (frame->ratio != this->vo_scale.delivered_ratio) ||
      (frame->format != this->delivered_format)) {
    this->vo_scale.delivered_width  = frame->width;
    this->vo_scale.delivered_height = frame->height;
    this->vo_scale.delivered_ratio  = frame->ratio;
    this->delivered_format          = frame->format;

    this->vo_scale.force_redraw = 1;
    _x_vo_scale_compute_ideal_size(&this->vo_scale);

    vram_reset(this);
    if (this->multibuf_en) {
      this->buf_mode = BUF_MODE_MULTI;
    }
    else {
      this->buf_mode = BUF_MODE_NONE;
    }
  }

  if (_x_vo_scale_redraw_needed(&this->vo_scale)) {  
    _x_vo_scale_compute_output_size(&this->vo_scale);
    repaint_output_area(this);
    this->ovl_regen_needed = 1;

    this->vregs[BUS_CNTL] |= le2me_32(BUS_EXT_REG_EN);
    this->vregs[OVERLAY_SCALE_CNTL] = 0;
    this->vregs[SCALER_H_COEFF0] = le2me_32(SCALER_H_COEFF0_DEFAULT);
    this->vregs[SCALER_H_COEFF1] = le2me_32(SCALER_H_COEFF1_DEFAULT);
    this->vregs[SCALER_H_COEFF2] = le2me_32(SCALER_H_COEFF2_DEFAULT);
    this->vregs[SCALER_H_COEFF3] = le2me_32(SCALER_H_COEFF3_DEFAULT);
    this->vregs[SCALER_H_COEFF4] = le2me_32(SCALER_H_COEFF4_DEFAULT);
    this->vregs[CAPTURE_CONFIG] = le2me_32(CAPTURE_CONFIG_BUF0);
    this->vregs[SCALER_COLOUR_CNTL] = le2me_32((this->saturation<<16) | (this->saturation<<8) | (this->brightness&0x7F));
    this->vregs[OVERLAY_KEY_CNTL] = le2me_32(OVERLAY_KEY_EN);
    this->vregs[OVERLAY_GRAPHICS_KEY_CLR] = le2me_32(this->colour_key);
    this->vregs[OVERLAY_GRAPHICS_KEY_MSK] = le2me_32(0xffffffff >> (32 - this->fb_depth));

    this->vregs[VIDEO_FORMAT] = le2me_32(frame->native_format);
    this->vregs[SCALER_BUF_PITCH] = le2me_32(this->deinterlace_en ? frame->pitch*2 : frame->pitch);
    this->vregs[OVERLAY_X_Y_START] = le2me_32(((this->vo_scale.gui_win_x + this->vo_scale.output_xoffset) << 16) | (this->vo_scale.gui_win_y + this->vo_scale.output_yoffset) | OVERLAY_X_Y_LOCK);
    this->vregs[OVERLAY_X_Y_END] = le2me_32(((this->vo_scale.gui_win_x + this->vo_scale.output_xoffset + this->vo_scale.output_width - 1) << 16) | (this->vo_scale.gui_win_y + this->vo_scale.output_yoffset + this->vo_scale.output_height - 1));
    this->vregs[OVERLAY_SCALE_INC] = le2me_32((((frame->width << 12) / this->vo_scale.output_width) << 16) | (((this->deinterlace_en ? frame->height/2 : frame->height) << 12) / this->vo_scale.output_height));
    this->vregs[SCALER_HEIGHT_WIDTH] = le2me_32((frame->width << 16) | (this->deinterlace_en ? frame->height/2 : frame->height));

    if (this->ovl_mode == OVL_MODE_EXCLUSIVE) {
      int horz_start = (this->vo_scale.gui_win_x + this->vo_scale.output_xoffset + 7) / 8;
      int horz_end = (this->vo_scale.gui_win_x + this->vo_scale.output_xoffset + this->vo_scale.output_width) / 8;

      this->vregs[OVERLAY_EXCLUSIVE_VERT] = le2me_32((this->vo_scale.gui_win_y + this->vo_scale.output_yoffset - 1) | ((this->vo_scale.gui_win_y + this->vo_scale.output_yoffset + this->vo_scale.output_height - 1) << 16));
      this->vregs[OVERLAY_EXCLUSIVE_HORZ] = le2me_32(horz_start | (horz_end << 8) | ((this->fb_width/8 - horz_end) << 16) | OVERLAY_EXCLUSIVE_EN);
    }
    else {
      this->vregs[OVERLAY_EXCLUSIVE_HORZ] = 0;
    }

    this->vregs[OVERLAY_SCALE_CNTL] = le2me_32(OVERLAY_SCALE_EN);
  }

  if (this->buf_mode == BUF_MODE_MULTI) {
    int i;

    if (frame->vo_frame.proc_slice != pgx64_frame_proc_slice) {
      for (i=0; i<frame->planes; i++) {
        if ((frame->buffers[i] = vram_alloc(this, frame->lengths[i])) < 0) {
          if (this->detained_frames < MAX_DETAINED_FRAMES) {
            this->detained[this->detained_frames++] = frame;
            return;
          }
          else {
            xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, 
		    "video_out_pgx64: Warning: low video memory, multi-buffering disabled\n");
            vram_reset(this);
            this->buf_mode = BUF_MODE_NONE;
            break;
          }
        }
        else {
          frame->buffer_ptrs[i] = this->vram + frame->buffers[i];
          memcpy(frame->buffer_ptrs[i], frame->vo_frame.base[i], frame->lengths[i]);
        }
      }

      frame->vo_frame.proc_frame = pgx64_frame_proc_frame;
      frame->vo_frame.proc_slice = pgx64_frame_proc_slice;
    }

    for (i=0; i<frame->planes; i++) {
      this->vregs[scaler_regs_table[this->dblbuf_select][i]] = le2me_32(frame->buffers[i]);
    }
  }

  if (this->buf_mode != BUF_MODE_MULTI) {
    int i, j;

    if (this->buf_mode == BUF_MODE_NONE) {
      for (i=0; i<frame->planes; i++) {
        if ((this->buffers[0][i] = vram_alloc(this, frame->lengths[i])) < 0) {
          xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Error: insuffucient video memory\n");
          return;
        }
        else {
          this->buffer_ptrs[0][i] = this->vram + this->buffers[0][i];
        }
      }

      this->buf_mode = BUF_MODE_DOUBLE;
      for (i=0; i<frame->planes; i++) {
        if ((this->buffers[1][i] = vram_alloc(this, frame->lengths[i])) < 0) {
           this->buf_mode = BUF_MODE_SINGLE;
        }
        else {
          this->buffer_ptrs[1][i] = this->vram + this->buffers[1][i];
        }
      }

      if (this->buf_mode == BUF_MODE_SINGLE) {
        xprintf(this->class->xine, XINE_VERBOSITY_LOG, 
		_("video_out_pgx64: Warning: low video memory, double-buffering disabled\n"));
        for (i=0; i<frame->planes; i++) {
          this->buffers[1][i] = this->buffers[0][i];
          this->buffer_ptrs[1][i] = this->vram + this->buffers[1][i];
        }
      }

      for (i=0; i<2; i++) {
        for (j=0; j<frame->planes; j++) {
          this->vregs[scaler_regs_table[i][j]] = le2me_32(this->buffers[i][j]);
        }
      }
    }

    for (i=0; i<frame->planes; i++) {
      memcpy(this->buffer_ptrs[this->dblbuf_select][i], frame->vo_frame.base[i], frame->lengths[i]);
    }

    frame->vo_frame.proc_slice = NULL;
    frame->vo_frame.proc_frame = NULL;
  }

  this->vregs[CAPTURE_CONFIG] = this->dblbuf_select ? le2me_32(CAPTURE_CONFIG_BUF1) : le2me_32(CAPTURE_CONFIG_BUF0);
  this->dblbuf_select = 1 - this->dblbuf_select;
  ioctl(this->fbfd, FBIOVERTICAL);

  if (this->current != NULL) {
    this->current->vo_frame.free(&this->current->vo_frame);
  }
  this->current = frame;
}

static void pgx64_overlay_blend(vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay)
{
  /*pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;*/
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

  if (overlay->rle) {
    if (frame->vo_frame.proc_slice == pgx64_frame_proc_slice) {
      /* FIXME: Implement out of place alphablending functions for better performance */
      switch (frame->format) {
        case XINE_IMGFMT_YV12: {
          blend_yuv(frame->buffer_ptrs, overlay, frame->width, frame->height, frame->vo_frame.pitches);
        }
        break;

        case XINE_IMGFMT_YUY2: {
          blend_yuy2(frame->buffer_ptrs[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0]);
        }
        break;
      }
    }
    else {
      switch (frame->format) {
        case XINE_IMGFMT_YV12: {
          blend_yuv(frame->vo_frame.base, overlay, frame->width, frame->height, frame->vo_frame.pitches);
        }
        break;

        case XINE_IMGFMT_YUY2: {
          blend_yuy2(frame->vo_frame.base[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0]);
        }
        break;
      }
    }
  }
}

static void pgx64_overlay_key_begin(vo_driver_t *this_gen, vo_frame_t *frame_gen, int changed)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  /*pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;*/

  if (changed || this->ovl_regen_needed) {
    pgx64_overlay_t *ovl, *next_ovl;

    this->ovl_regen_needed = 0;
    this->ovl_changed = 1;
    pthread_mutex_lock(&this->ovl_mutex);

    XLockDisplay(this->display);
    XSetForeground(this->display, this->gc, this->colour_key);
    ovl = this->first_overlay;
    while (ovl != NULL) {
      next_ovl = ovl->next;
      XFreePixmap(this->display, ovl->p);
      XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.output_xoffset + ovl->x, this->vo_scale.output_yoffset + ovl->y, ovl->width, ovl->height);
      free(ovl);
      ovl = next_ovl;
    }
    this->first_overlay = NULL;
    XUnlockDisplay(this->display);
  }
}

#define scale_up(n)       ((n) << 16)
#define scale_down(n)     ((n) >> 16)
#define saturate(n, l, u) ((n) < (l) ? (l) : ((n) > (u) ? (u) : (n)))

static void pgx64_overlay_key_blend(vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;

  if (this->ovl_changed && overlay->rle) {
    pgx64_overlay_t *ovl, **ovl_ptr;
    int x_scale, y_scale, i, x, y, len, width;
    int use_clip_palette, max_palette_colour[2];
    unsigned long palette[2][OVL_PALETTE_SIZE];

    x_scale = scale_up(this->vo_scale.output_width) / frame->width;
    y_scale = scale_up(this->vo_scale.output_height) / frame->height;

    max_palette_colour[0] = -1;
    max_palette_colour[1] = -1;

    ovl = (pgx64_overlay_t *)malloc(sizeof(pgx64_overlay_t));
    if (!ovl) {
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: overlay malloc failed\n");
      return;
    }
    ovl->x = scale_down(overlay->x * x_scale);
    ovl->y = scale_down(overlay->y * y_scale);
    ovl->width = scale_down(overlay->width * x_scale);
    ovl->height = scale_down(overlay->height * y_scale);
    ovl->next = NULL;

    XLockDisplay(this->display);
    ovl->p = XCreatePixmap(this->display, this->drawable, ovl->width, ovl->height, this->depth);
    for (i=0, x=0, y=0; i<overlay->num_rle; i++) {
      len = overlay->rle[i].len;

      while (len > 0) {
        use_clip_palette = 0;
        if (len > overlay->width) {
          width = overlay->width;
          len -= overlay->width;
        }
        else {
          width = len;
          len = 0;
        }
        if ((y >= overlay->clip_top) && (y <= overlay->clip_bottom) && (x <= overlay->clip_right)) {
          if ((x < overlay->clip_left) && (x + width - 1 >= overlay->clip_left)) {
            width -= overlay->clip_left - x;
            len += overlay->clip_left - x;
          }
          else if (x > overlay->clip_left)  {
            use_clip_palette = 1;
            if (x + width - 1 > overlay->clip_right) {
              width -= overlay->clip_right - x;
              len += overlay->clip_right - x;
            } 
          }
        }

        if (overlay->rle[i].color > max_palette_colour[use_clip_palette]) {
          int j;
          clut_t *src_clut;
          uint8_t *src_trans;
          
          if (use_clip_palette) {
            src_clut = (clut_t *)&overlay->clip_color;
            src_trans = (uint8_t *)&overlay->clip_trans;
          }
          else {
            src_clut = (clut_t *)&overlay->color;
            src_trans = (uint8_t *)&overlay->trans;
          }
          for (j=max_palette_colour[use_clip_palette]+1; j<=overlay->rle[i].color; j++) {
            if (src_trans[j]) {
              XColor col;
              int y, u, v, r, g, b;

              y = saturate(src_clut[j].y, 16, 235);
              u = saturate(src_clut[j].cb, 16, 240);
              v = saturate(src_clut[j].cr, 16, 240);
              y = (9 * y) / 8;
              r = y + (25 * v) / 16 - 218;
              g = y + (-13 * v) / 16 + (-25 * u) / 64 + 136;
              b = y + 2 * u - 274;

              col.red = (r & 0xff) << 8;
              col.green = (g & 0xff) << 8;
              col.blue = (b & 0xff) << 8;
              if (XAllocColor(this->display, this->cmap, &col)) {
                palette[use_clip_palette][j] = col.pixel;
              }
              else {
                if (src_clut[j].y > 127) {
                  palette[use_clip_palette][j] = WhitePixel(this->display, this->screen);
                }
                else {
                  palette[use_clip_palette][j] = BlackPixel(this->display, this->screen);
                }
              } 
            }
            else {
              palette[use_clip_palette][j] = this->colour_key;
            }
          }
          max_palette_colour[use_clip_palette] = overlay->rle[i].color;
        }
        XSetForeground(this->display, this->gc, palette[use_clip_palette][overlay->rle[i].color]);
        XFillRectangle(this->display, ovl->p, this->gc, scale_down(x * x_scale), scale_down(y * y_scale), scale_down((x + width) * x_scale) - scale_down(x * x_scale), scale_down((y + 1) * y_scale) - scale_down(y * y_scale));
        x += width;
        if (x == overlay->width) {
          x = 0;
          y++;
        }
      }
    }
    if (y < overlay->height) {
      xprintf(this->class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Notice: RLE data doesn't extend to height of overlay\n");
      XFillRectangle(this->display, ovl->p, this->gc, scale_down(x * x_scale), scale_down(y * y_scale), ovl->width, scale_down(overlay->height * y_scale) - scale_down(y * y_scale));
    }
    XUnlockDisplay(this->display);

    ovl_ptr = &this->first_overlay;
    while (*ovl_ptr != NULL) {
      ovl_ptr = &(*ovl_ptr)->next;
    }
    *ovl_ptr = ovl;
  }
}

static void pgx64_overlay_key_end(vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;
  /*pgx64_frame_t *frame = (pgx64_frame_t *)frame_gen;*/

  if (this->ovl_changed) {
    draw_overlays(this);
    pthread_mutex_unlock(&this->ovl_mutex);
    this->ovl_changed = 0;
  }
}

static int pgx64_get_property(vo_driver_t *this_gen, int property)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

  switch (property) {
    case VO_PROP_INTERLACED:
      return this->deinterlace_en;
    break;

    case VO_PROP_ASPECT_RATIO:
      return this->vo_scale.user_ratio;
    break;

    case VO_PROP_SATURATION:
      return this->saturation;
    break;

    case VO_PROP_BRIGHTNESS:
      return this->brightness;
    break;

    case VO_PROP_COLORKEY:
      return this->colour_key;
    break;

    default:
      return 0;
    break;
  }
}

static int pgx64_set_property(vo_driver_t *this_gen, int property, int value)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

  switch (property) {
    case VO_PROP_INTERLACED: {
      this->deinterlace_en = value;
      this->vo_scale.force_redraw = 1;
    }
    break;

    case VO_PROP_ASPECT_RATIO: {
      if (value >= XINE_VO_ASPECT_NUM_RATIOS) {
        value = XINE_VO_ASPECT_AUTO;
      }
      this->vo_scale.user_ratio = value;
      this->vo_scale.force_redraw = 1;
      _x_vo_scale_compute_ideal_size(&this->vo_scale);     
    }
    break;

    case VO_PROP_SATURATION: {
      this->saturation = value;
      this->vo_scale.force_redraw = 1;
    }
    break;

    case VO_PROP_BRIGHTNESS: {
      this->brightness = value;
      this->vo_scale.force_redraw = 1;
    }
    break;

    case VO_PROP_COLORKEY: {
      this->colour_key = value;
      this->vo_scale.force_redraw = 1;
    }
    break;
  }
  return value;
}

static void pgx64_get_property_min_max(vo_driver_t *this_gen, int property, int *min, int *max)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

  switch (property) {
    case VO_PROP_SATURATION: {
      *min = 0;
      *max = 31;
    }
    break;

    case VO_PROP_BRIGHTNESS: {
      *min = -64;
      *max = 63;
    }
    break;

    case VO_PROP_COLORKEY: {
      *min = 0;
      *max = 0xffffffff >> (32 - this->fb_depth);
    }

    default:
      *min = 0;
      *max = 0;
    break;
  }
}

static int pgx64_gui_data_exchange(vo_driver_t *this_gen, int data_type, void *data)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

  switch (data_type) {
    case XINE_GUI_SEND_DRAWABLE_CHANGED: {
      XWindowAttributes win_attrs;

      XLockDisplay(this->display);
      this->drawable = (Drawable)data;
      XGetWindowAttributes(this->display, this->drawable, &win_attrs);
      this->depth  = win_attrs.depth;
      this->visual = win_attrs.visual;
      XFreeColormap(this->display, this->cmap);
      this->cmap = XCreateColormap(this->display, this->drawable, this->visual, AllocNone);
      XFreeGC(this->display, this->gc);
      this->gc = XCreateGC(this->display, this->drawable, 0, NULL);
      XUnlockDisplay(this->display);
    }
    break;

    case XINE_GUI_SEND_EXPOSE_EVENT: {
      repaint_output_area(this);
    }
    break;

    case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO: {
      x11_rectangle_t *rect = data;
      int x1, y1, x2, y2;

      _x_vo_scale_translate_gui2video(&this->vo_scale, rect->x, rect->y, &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->vo_scale, rect->x + rect->w, rect->y + rect->h, &x2, &y2);

      rect->x = x1;
      rect->y = y1;
      rect->w = x2 - x1;
      rect->h = y2 - y1;
    }
    break;
  }

  return 0;
}

static int pgx64_redraw_needed(vo_driver_t *this_gen)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

  if (_x_vo_scale_redraw_needed(&this->vo_scale)) {  
    this->vo_scale.force_redraw = 1;
    this->ovl_regen_needed = 1;
    return 1;
  }

  return 0;
}

static void pgx64_dispose(vo_driver_t *this_gen)
{
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)this_gen;

  this->vregs[OVERLAY_EXCLUSIVE_HORZ] = 0;
  this->vregs[OVERLAY_SCALE_CNTL] = 0;

  XLockDisplay (this->display);
  XFreeGC(this->display, this->gc);
  XUnlockDisplay (this->display);

  munmap(this->vram, FB_ADDRSPACE);
  close(this->fbfd);

  pthread_mutex_lock(&this->class->mutex);
  this->class->instance_count--;
  pthread_mutex_unlock(&this->class->mutex);
  
  free(this);
}

static void set_overlay_mode(pgx64_driver_t* this, int ovl_mode)
{
  pthread_mutex_lock(&this->ovl_mutex);
  if (ovl_mode == OVL_MODE_CHROMA_KEY) {
    this->vo_driver.overlay_begin = pgx64_overlay_key_begin;
    this->vo_driver.overlay_blend = pgx64_overlay_key_blend;
    this->vo_driver.overlay_end   = pgx64_overlay_key_end;
  }
  else {
    this->vo_driver.overlay_begin = NULL;
    this->vo_driver.overlay_blend = pgx64_overlay_blend;
    this->vo_driver.overlay_end   = NULL;
  }

  this->ovl_mode = ovl_mode;
  pthread_mutex_unlock(&this->ovl_mutex);
}

static void pgx64_config_changed(void *user_data, xine_cfg_entry_t *entry)
{
  vo_driver_t *this_gen = (vo_driver_t *)user_data;
  pgx64_driver_t *this = (pgx64_driver_t *)(void *)user_data;

  if (strcmp(entry->key, "video.pgx64_colour_key") == 0) {
    pgx64_set_property(this_gen, VO_PROP_COLORKEY, entry->num_value);
  } 
  else if (strcmp(entry->key, "video.pgx64_brightness") == 0) {
    pgx64_set_property(this_gen, VO_PROP_BRIGHTNESS, entry->num_value);
  }
  else if (strcmp(entry->key, "video.pgx64_saturation") == 0) {
    pgx64_set_property(this_gen, VO_PROP_SATURATION, entry->num_value);
  }
  else if (strcmp(entry->key, "video.pgx64_overlay_mode") == 0) {
    set_overlay_mode(this, entry->num_value);
  }
  else if (strcmp(entry->key, "video.pgx64_multibuf_en") == 0) {
    this->multibuf_en = entry->num_value;
  }
}

/*
 * XINE VIDEO DRIVER CLASS FUNCTIONS
 */

static void pgx64_dispose_class(video_driver_class_t *class_gen)
{
  pgx64_driver_class_t *class = (pgx64_driver_class_t *)(void *)class_gen;

  pthread_mutex_destroy(&class->mutex);
  free(class);
}

static vo_info_t vo_info_pgx64 = {
  10,
  XINE_VISUAL_TYPE_X11
};

static vo_driver_t* pgx64_init_driver(video_driver_class_t *class_gen, const void *visual_gen)
{
  pgx64_driver_class_t *class = (pgx64_driver_class_t *)(void *)class_gen;
  char *devname;
  int fbfd;
  void *baseaddr;
  pgx64_driver_t *this;
  struct fbgattr attr;
  XWindowAttributes win_attrs;

  pthread_mutex_lock(&class->mutex);
  if (class->instance_count > 0) {
    pthread_mutex_unlock(&class->mutex);
    return NULL;
  }
  class->instance_count++;
  pthread_mutex_unlock(&class->mutex);

  devname = class->config->register_string(class->config, "video.pgx64_device", "/dev/fb", "name of pgx64 device", NULL, 10, NULL, NULL);
  if ((fbfd = open(devname, O_RDWR)) < 0) {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Error: can't open framebuffer device '%s'\n", devname);
    return NULL;
  }

  if (ioctl(fbfd, FBIOGATTR, &attr) < 0) {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Error: ioctl failed, unable to determine framebuffer characteristics\n");
    close(fbfd);
    return NULL;
  }

  if (attr.real_type != 22) {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Error: '%s' is not a mach64 framebuffer device\n", devname);
    close(fbfd);
    return NULL;
  }

  if ((baseaddr = mmap(0, FB_ADDRSPACE, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0)) == MAP_FAILED) {
    xprintf(class->xine, XINE_VERBOSITY_DEBUG, "video_out_pgx64: Error: unable to memory map framebuffer\n");
    close(fbfd);
    return NULL;
  }

  this = (pgx64_driver_t*)xine_xmalloc(sizeof(pgx64_driver_t));
  if (!this) {
    return NULL;
  }

  this->vo_driver.get_capabilities     = pgx64_get_capabilities;
  this->vo_driver.alloc_frame          = pgx64_alloc_frame;
  this->vo_driver.update_frame_format  = pgx64_update_frame_format;
  this->vo_driver.overlay_begin        = NULL;
  this->vo_driver.overlay_blend        = pgx64_overlay_blend;
  this->vo_driver.overlay_end          = NULL;
  this->vo_driver.display_frame        = pgx64_display_frame;
  this->vo_driver.get_property         = pgx64_get_property;
  this->vo_driver.set_property         = pgx64_set_property;
  this->vo_driver.get_property_min_max = pgx64_get_property_min_max;
  this->vo_driver.gui_data_exchange    = pgx64_gui_data_exchange;
  this->vo_driver.redraw_needed        = pgx64_redraw_needed;
  this->vo_driver.dispose              = pgx64_dispose;

  _x_vo_scale_init(&this->vo_scale, 0, 0, class->config);
  this->vo_scale.user_ratio = XINE_VO_ASPECT_AUTO;
  this->vo_scale.user_data       = ((x11_visual_t*)visual_gen)->user_data;
  this->vo_scale.frame_output_cb = ((x11_visual_t*)visual_gen)->frame_output_cb;
  this->vo_scale.dest_size_cb    = ((x11_visual_t*)visual_gen)->dest_size_cb;

  this->class = class;
  this->current = NULL;

  this->display  = ((x11_visual_t*)visual_gen)->display;
  this->screen   = ((x11_visual_t*)visual_gen)->screen;
  this->drawable = ((x11_visual_t*)visual_gen)->d;
  this->gc       = XCreateGC(this->display, this->drawable, 0, NULL);

  XGetWindowAttributes(this->display, this->drawable, &win_attrs);
  this->depth  = win_attrs.depth;
  this->visual = win_attrs.visual;
  this->cmap   = XCreateColormap(this->display, this->drawable, this->visual, AllocNone);

  this->fbfd = fbfd;
  this->vram = (uint8_t*)baseaddr;
  this->vregs = (uint32_t*)baseaddr + FB_REGSBASE;
  this->free_top = attr.sattr.dev_specific[0];
  this->free_bottom = attr.sattr.dev_specific[5] + attr.fbtype.fb_size;
  this->fb_width = attr.fbtype.fb_width;
  this->fb_height = attr.fbtype.fb_height;
  this->fb_depth = attr.fbtype.fb_depth;

  this->colour_key  = class->config->register_num(this->class->config, "video.pgx64_colour_key", 1, "video overlay colour key", NULL, 10, pgx64_config_changed, this);
  this->brightness  = class->config->register_range(this->class->config, "video.pgx64_brightness", 0, -64, 63, "video overlay brightness", NULL, 10, pgx64_config_changed, this);
  this->saturation  = class->config->register_range(this->class->config, "video.pgx64_saturation", 16, 0, 31, "video overlay saturation", NULL, 10, pgx64_config_changed, this);
  this->ovl_mode    = class->config->register_enum(this->class->config, "video.pgx64_overlay_mode", 0, (char**)overlay_modes, "video overlay mode", NULL, 10, pgx64_config_changed, this);
  this->multibuf_en = class->config->register_bool(this->class->config, "video.pgx64_multibuf_en", 1, "enable multi-buffering", NULL, 10, pgx64_config_changed, this);

  pthread_mutex_init(&this->ovl_mutex, NULL);
  set_overlay_mode(this, this->ovl_mode);

  return (vo_driver_t *)this;
}

static char* pgx64_get_identifier(video_driver_class_t *class_gen)
{
  return "pgx64";
}

static char* pgx64_get_description(video_driver_class_t *class_gen)
{
  return "xine video output plugin for Sun PGX64/PGX24 framebuffers";
}

static void* pgx64_init_class(xine_t *xine, void *visual_gen)
{
  pgx64_driver_class_t *class;

  class = (pgx64_driver_class_t*)xine_xmalloc(sizeof(pgx64_driver_class_t));
  if (!class)
    return NULL;

  class->vo_driver_class.open_plugin     = pgx64_init_driver;
  class->vo_driver_class.get_identifier  = pgx64_get_identifier;
  class->vo_driver_class.get_description = pgx64_get_description;
  class->vo_driver_class.dispose         = pgx64_dispose_class;

  class->xine   = xine;
  class->config = xine->config;

  pthread_mutex_init(&class->mutex, NULL);

  return class;
}

plugin_info_t xine_plugin_info[] = {
  {PLUGIN_VIDEO_OUT, 19, "pgx64", XINE_VERSION_CODE, &vo_info_pgx64, pgx64_init_class},
  {PLUGIN_NONE, 0, "", 0, NULL, NULL}
};
