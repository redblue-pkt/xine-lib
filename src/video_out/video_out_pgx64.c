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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * $Id: video_out_pgx64.c,v 1.28 2003/05/31 18:33:31 miguelfreitas Exp $
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

#include "xine_internal.h"
#include "alphablend.h"
#include "bswap.h"
#include "vo_scale.h"
#include "xineutils.h"

#define ADDRSPACE 8388608
#define REGBASE   8386560

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
#define SCALER_COLOUR_CNTL 0x054

#define SCALER_BUF0_OFFSET 0x00D
#define SCALER_BUF0_OFFSET_U 0x075
#define SCALER_BUF0_OFFSET_V 0x076
#define SCALER_BUF_PITCH 0x00F
#define VIDEO_FORMAT 0x012
#define VIDEO_FORMAT_YUV12 0x000A0000
#define VIDEO_FORMAT_YUY2 0x000B0000
#define CAPTURE_CONFIG 0x014

#define OVERLAY_X_Y_START 0x000
#define OVERLAY_X_Y_END 0x001
#define OVERLAY_X_Y_LOCK 0x80000000
#define OVERLAY_SCALE_INC 0x008
#define SCALER_HEIGHT_WIDTH 0x00A
#define OVERLAY_EXCLUSIVE_HORZ 0x016
#define OVERLAY_EXCLUSIVE_VERT 0x017
#define OVERLAY_EXCLUSIVE_EN 0x80000000
#define OVERLAY_SCALE_CNTL 0x009
#define OVERLAY_EN 0xC0000000

#define DEINTERLACE_ONEFIELD        0
#define DEINTERLACE_LINEARBLEND     1
#define DEINTERLACE_LINEARBLEND_VIS 2

static char *deinterlace_methods[] = {"one field",
                                      "linear blend",
#ifdef ENABLE_VIS
                                      "linear blend (VIS)",
#endif
                                      NULL};

typedef struct {
  video_driver_class_t vo_driver_class;

  xine_t *xine;
  config_values_t *config;

  pthread_mutex_t mutex;
  int instance_count;
} pgx64_driver_class_t;

typedef struct {
  vo_frame_t vo_frame;

  int lengths[3];  
  uint32_t buf_y, buf_u, buf_v;
  int width, height, pitch, ratio_code, format;
} pgx64_frame_t;

typedef struct {   
  vo_driver_t vo_driver;
  vo_scale_t vo_scale;
  pgx64_driver_class_t *class;
  pgx64_frame_t *current;
 
  int visual_type;
  Display *display;
  int screen;
  Drawable drawable;
  GC gc;

  int fbfd;
  uint8_t *fbbase;
  volatile uint32_t *fbregs;
  uint32_t top, fb_width, fb_height;

  int colour_key, depth_mask, brightness, saturation;
  int deinterlace, deinterlace_method, use_exclusive;
} pgx64_driver_t;

/*
 * Dispose of any allocated image data within a pgx64_frame_t
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
 * Read and write to the little endian framebuffer registers
 */

static inline uint32_t read_reg(pgx64_driver_t *this, int reg)
{
  return le2me_32(this->fbregs[reg]);
}

static inline void write_reg(pgx64_driver_t *this, int reg, uint32_t value)
{
  this->fbregs[reg] = le2me_32(value);
}

static inline void set_reg_bits(pgx64_driver_t *this, int reg, uint32_t mask)
{
  this->fbregs[reg] |= le2me_32(mask);
}

static inline void clear_reg_bits(pgx64_driver_t *this, int reg, uint32_t mask)
{
  this->fbregs[reg] &= le2me_32(~mask);
}

/*
 * Read and write to the graphics status register of VIS(TM) capable processors
 */

#ifdef ENABLE_VIS
static inline uint32_t read_gsr()
{
  uint32_t gsr;
  asm ("rd      %%gsr, %0" : "=r" (gsr));
  return gsr;
}

static inline void write_gsr(uint32_t gsr)
{
  asm ("wr      %0, %%g0, %%gsr" : : "r" (gsr));
}
#endif

/*
 * Paint the output area with the colour key and black borders
 */

static void repaint_output_area(pgx64_driver_t *this)
{
  switch (this->visual_type) {
    case XINE_VISUAL_TYPE_X11: {
#ifdef HAVE_X11
      XLockDisplay(this->display);
      XSetForeground(this->display, this->gc, BlackPixel(this->display, this->screen));
      XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.border[0].x, this->vo_scale.border[0].y, this->vo_scale.border[0].w, this->vo_scale.border[0].h);
      XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.border[1].x, this->vo_scale.border[1].y, this->vo_scale.border[1].w, this->vo_scale.border[1].h);
      XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.border[2].x, this->vo_scale.border[2].y, this->vo_scale.border[2].w, this->vo_scale.border[2].h);
      XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.border[3].x, this->vo_scale.border[3].y, this->vo_scale.border[3].w, this->vo_scale.border[3].h);
      XSetForeground(this->display, this->gc, this->colour_key);
      XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.output_xoffset, this->vo_scale.output_yoffset, this->vo_scale.output_width, this->vo_scale.output_height);
      XFlush(this->display);
      XUnlockDisplay(this->display);
#endif
    }
    break;

    case XINE_VISUAL_TYPE_FB: {
      /* FIXME: Do this properly */
      write_reg(this, OVERLAY_KEY_CNTL, 0x00000010);
    }
  }
}

/*
 * XINE VIDEO DRIVER FUNCTIONS
 */

static void pgx64fb_output_callback(pgx64_driver_t *this, int video_width, int video_height, double video_pixel_aspect, int *dest_x, int *dest_y, int *dest_width, int *dest_height, double *dest_pixel_aspect, int *win_x, int *win_y)
{
  *dest_x            = 0;
  *dest_y            = 0;
  *dest_width        = this->fb_width;
  *dest_height       = this->fb_height;
  *dest_pixel_aspect = 1.0;
  *win_x             = 0;
  *win_y             = 0;
}

static void pgx64_frame_field(pgx64_frame_t *frame, int which_field)
{
}

static void pgx64_frame_dispose(pgx64_frame_t *frame)
{
  dispose_frame_internals(frame);
  free(frame);
}

static uint32_t pgx64_get_capabilities(pgx64_driver_t *this)
{
  return VO_CAP_YV12 |
         VO_CAP_YUY2 |
         VO_CAP_COLORKEY |
         VO_CAP_SATURATION |
         VO_CAP_BRIGHTNESS;
}

static pgx64_frame_t* pgx64_alloc_frame(pgx64_driver_t *this)
{
  pgx64_frame_t *frame;

  frame = (pgx64_frame_t*)malloc(sizeof(pgx64_frame_t));
  if (!frame) {
    printf("video_out_pgx64: frame malloc failed\n");
    return NULL;
  }
  memset(frame, 0, sizeof(pgx64_frame_t));

  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  frame->vo_frame.field   = (void*)pgx64_frame_field; 
  frame->vo_frame.dispose = (void*)pgx64_frame_dispose;

  return frame;
}

static void pgx64_update_frame_format(pgx64_driver_t *this, pgx64_frame_t *frame, uint32_t width, uint32_t height, int ratio_code, int format, int flags)
{
  if ((width != frame->width) ||
      (height != frame->height) ||
      (ratio_code != frame->ratio_code) ||
      (format != frame->format)) {
    dispose_frame_internals(frame);

    frame->width = width;
    frame->height = height;
    frame->ratio_code = ratio_code;
    frame->format = format;
    frame->pitch = ((width + 7) / 8) * 8;

    switch (format) {
      case XINE_IMGFMT_YUY2:
        frame->vo_frame.pitches[0] = frame->pitch * 2;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->vo_frame.base[0] = (void*)memalign(8, frame->lengths[0]);
        frame->buf_y = (this->top - frame->lengths[0]) & ~0x07;
      break;

      case XINE_IMGFMT_YV12:
        frame->vo_frame.pitches[0] = frame->pitch;
        frame->vo_frame.pitches[1] = frame->pitch / 2;
        frame->vo_frame.pitches[2] = frame->pitch / 2;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->lengths[1] = frame->vo_frame.pitches[1] * (height / 2);
        frame->lengths[2] = frame->vo_frame.pitches[2] * (height / 2);
        frame->vo_frame.base[0] = (void*)memalign(8, frame->lengths[0]);
        frame->vo_frame.base[1] = (void*)memalign(8, frame->lengths[1]);
        frame->vo_frame.base[2] = (void*)memalign(8, frame->lengths[2]);
        frame->buf_y = (this->top - frame->lengths[0]) & ~0x07;
        frame->buf_u = (frame->buf_y - frame->lengths[1]) & ~0x07;
        frame->buf_v = (frame->buf_u - frame->lengths[2]) & ~0x07;
      break;
    }
  }
}

static void pgx64_display_frame(pgx64_driver_t *this, pgx64_frame_t *frame)
{
  if ((frame->width != this->vo_scale.delivered_width) ||
      (frame->height != this->vo_scale.delivered_height) ||
      (frame->ratio_code != this->vo_scale.delivered_ratio_code)) {

    this->vo_scale.delivered_width      = frame->width;
    this->vo_scale.delivered_height     = frame->height;
    this->vo_scale.delivered_ratio_code = frame->ratio_code;
    this->vo_scale.force_redraw         = 1;
    vo_scale_compute_ideal_size(&this->vo_scale);
  }

  if (vo_scale_redraw_needed(&this->vo_scale)) {  
    vo_scale_compute_output_size(&this->vo_scale);
    repaint_output_area(this);

    set_reg_bits(this, BUS_CNTL, BUS_EXT_REG_EN);
    write_reg(this, OVERLAY_SCALE_CNTL, 0x04000000);
    write_reg(this, SCALER_H_COEFF0, SCALER_H_COEFF0_DEFAULT);
    write_reg(this, SCALER_H_COEFF1, SCALER_H_COEFF1_DEFAULT);
    write_reg(this, SCALER_H_COEFF2, SCALER_H_COEFF2_DEFAULT);
    write_reg(this, SCALER_H_COEFF3, SCALER_H_COEFF3_DEFAULT);
    write_reg(this, SCALER_H_COEFF4, SCALER_H_COEFF4_DEFAULT);
    write_reg(this, CAPTURE_CONFIG, 0x00000000);
    write_reg(this, SCALER_COLOUR_CNTL, (this->saturation<<16) | (this->saturation<<8) | (this->brightness&0x7F));
    write_reg(this, OVERLAY_KEY_CNTL, 0x00000050);
    write_reg(this, OVERLAY_GRAPHICS_KEY_CLR, this->colour_key);
    write_reg(this, OVERLAY_GRAPHICS_KEY_MSK, this->depth_mask);

    write_reg(this, VIDEO_FORMAT, (frame->format == XINE_IMGFMT_YUY2) ? VIDEO_FORMAT_YUY2 : VIDEO_FORMAT_YUV12);
    write_reg(this, SCALER_BUF0_OFFSET, frame->buf_y);
    write_reg(this, SCALER_BUF0_OFFSET_U, frame->buf_u);
    write_reg(this, SCALER_BUF0_OFFSET_V, frame->buf_v);
    write_reg(this, SCALER_BUF_PITCH, frame->pitch);
    write_reg(this, OVERLAY_X_Y_START, ((this->vo_scale.gui_win_x + this->vo_scale.output_xoffset) << 16) | (this->vo_scale.gui_win_y + this->vo_scale.output_yoffset) | OVERLAY_X_Y_LOCK);
    write_reg(this, OVERLAY_X_Y_END, ((this->vo_scale.gui_win_x + this->vo_scale.output_xoffset + this->vo_scale.output_width) << 16) | (this->vo_scale.gui_win_y + this->vo_scale.output_yoffset + this->vo_scale.output_height - 1));
    write_reg(this, OVERLAY_SCALE_INC, (((frame->width << 12) / this->vo_scale.output_width) << 16) | (((this->deinterlace && (this->deinterlace_method == DEINTERLACE_ONEFIELD) ? frame->height/2 : frame->height) << 12) / this->vo_scale.output_height));
    write_reg(this, SCALER_HEIGHT_WIDTH, (frame->width << 16) | (this->deinterlace && (this->deinterlace_method == DEINTERLACE_ONEFIELD) ? frame->height/2 : frame->height));

    if (this->use_exclusive) {
      int horz_start = (this->vo_scale.gui_win_x + this->vo_scale.output_xoffset + 7) / 8;
      int horz_end = (this->vo_scale.gui_win_x + this->vo_scale.output_xoffset + this->vo_scale.output_width) / 8;

      write_reg(this, OVERLAY_EXCLUSIVE_VERT, (this->vo_scale.gui_win_y + this->vo_scale.output_yoffset) | ((this->vo_scale.gui_win_y + this->vo_scale.output_yoffset + this->vo_scale.output_height - 1) << 16));
      write_reg(this, OVERLAY_EXCLUSIVE_HORZ,  horz_start | (horz_end << 8) | ((this->fb_width/8 - horz_end) << 16) | OVERLAY_EXCLUSIVE_EN);
    }
    else {
      write_reg(this, OVERLAY_EXCLUSIVE_HORZ, 0);
    }

    set_reg_bits(this, OVERLAY_SCALE_CNTL, OVERLAY_EN);
  }

  if (frame->format == XINE_IMGFMT_YV12) {
    switch (this->deinterlace ? this->deinterlace_method : ~0) {
      case DEINTERLACE_ONEFIELD: {
        register uint8_t *y = frame->vo_frame.base[0];
        register uint8_t *ydest = this->fbbase+frame->buf_y;
        register uint8_t *u = frame->vo_frame.base[1];
        register uint8_t *v = frame->vo_frame.base[2];
        register uint8_t *udest = this->fbbase+frame->buf_u;
        register uint8_t *vdest = this->fbbase+frame->buf_v;
        int i = 0;

        for (i = 0; i < frame->height/2; i++, y += 2*frame->vo_frame.pitches[0], ydest += frame->vo_frame.pitches[0]) {
          memcpy(ydest, y, frame->vo_frame.pitches[0]);
        }

        for (i = 0; i < frame->height/4; i++, u += 2*frame->vo_frame.pitches[1], udest += frame->vo_frame.pitches[1], v += 2*frame->vo_frame.pitches[2], vdest += frame->vo_frame.pitches[2]) {
          memcpy(udest, u, frame->vo_frame.pitches[1]);
          memcpy(vdest, v, frame->vo_frame.pitches[2]);
        }
      }
      break;

      case DEINTERLACE_LINEARBLEND: {
        register uint8_t *first = frame->vo_frame.base[0];
        register uint8_t *second = frame->vo_frame.base[0]+frame->vo_frame.pitches[0];
        register uint8_t *third = frame->vo_frame.base[0]+(2*frame->vo_frame.pitches[0]);
        register uint8_t *last = frame->vo_frame.base[0]+(frame->vo_frame.pitches[0]*frame->height);
        register uint8_t *dest = this->fbbase+frame->buf_y;

        memcpy(dest, first, frame->vo_frame.pitches[0]);
        dest += frame->vo_frame.pitches[0];

        for (; third != last; first++, second++, third++, dest++) {
          *dest = (*first + (*second << 1) + *third) >> 2;
        }

        memcpy(dest, second, frame->vo_frame.pitches[0]);

        memcpy(this->fbbase+frame->buf_u, frame->vo_frame.base[1], frame->lengths[1]);
        memcpy(this->fbbase+frame->buf_v, frame->vo_frame.base[2], frame->lengths[2]);
      }
      break;

#ifdef ENABLE_VIS
      case DEINTERLACE_LINEARBLEND_VIS: {
        register uint32_t *first = (uint32_t *)(frame->vo_frame.base[0]);
        register uint32_t *second = (uint32_t *)(frame->vo_frame.base[0]+frame->vo_frame.pitches[0]);
        register uint32_t *third = (uint32_t *)(frame->vo_frame.base[0]+(2*frame->vo_frame.pitches[0]));
        register uint32_t *last = (uint32_t *)(frame->vo_frame.base[0]+(frame->vo_frame.pitches[0]*frame->height));
        register uint32_t *dest = (uint32_t *)(this->fbbase+frame->buf_y);

        write_gsr((read_gsr() & 0xffffff07) | 0x000000008);

        memcpy(dest, first, frame->vo_frame.pitches[0]);
        dest += frame->vo_frame.pitches[0]/4;

        for (; third != last; first++, second++, third++, dest++) {
          asm volatile("ld	[%0], %%f0\n\t"
                       "fexpand	%%f0, %%f2\n\t"
                       "ld	[%1], %%f4\n\t"
                       "fexpand %%f4, %%f6\n\t"
                       "ld	[%2], %%f8\n\t"
                       "fexpand %%f8, %%f10\n\t"
                       "fpadd16	%%f6, %%f6, %%f0\n\t"
                       "fpadd16 %%f2, %%f10, %%f4\n\t"
                       "fpadd16 %%f0, %%f4, %%f8\n\t"
                       "fpack16 %%f8, %%f6\n\t"
                       "st	%%f6, [%3]"
                       : : "r" (first), "r" (second), "r" (third), "r" (dest)
                       : "%f0", "%f1", "%f2", "%f3", "%f4", "%f5",
                         "%f6", "%f7", "%f8", "%f9", "%f10", "%f11");
        }

        memcpy(dest, second, frame->vo_frame.pitches[0]);

        memcpy(this->fbbase+frame->buf_u, frame->vo_frame.base[1], frame->lengths[1]);
        memcpy(this->fbbase+frame->buf_v, frame->vo_frame.base[2], frame->lengths[2]);
      }
      break;
#endif

      default: {
        memcpy(this->fbbase+frame->buf_y, frame->vo_frame.base[0], frame->lengths[0]);

        memcpy(this->fbbase+frame->buf_u, frame->vo_frame.base[1], frame->lengths[1]);
        memcpy(this->fbbase+frame->buf_v, frame->vo_frame.base[2], frame->lengths[2]);
      }
      break;
    }
  }
  else {
    memcpy(this->fbbase+frame->buf_y, frame->vo_frame.base[0], frame->lengths[0]);
  }

  if ((this->current != NULL) && (this->current != frame)) {
    frame->vo_frame.free(&this->current->vo_frame);
  }
  this->current = frame;
}

static void pgx64_overlay_blend(pgx64_driver_t *this, pgx64_frame_t *frame, vo_overlay_t *overlay)
{
  if (overlay->rle) {
    if (frame->format == XINE_IMGFMT_YV12) {
      blend_yuv(frame->vo_frame.base, overlay, frame->width, frame->height, frame->vo_frame.pitches);
    }
    else {
      blend_yuy2(frame->vo_frame.base[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0]);
    }
  }
}

static int pgx64_get_property(pgx64_driver_t *this, int property)
{
  switch (property) {
    case VO_PROP_INTERLACED:
      return this->deinterlace;
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

static int pgx64_set_property(pgx64_driver_t *this, int property, int value)
{
#ifdef LOG
  printf("video_out_pgx64: Propery %d was set to 0x%08x\n", property, value);
#endif

  switch (property) {
    case VO_PROP_INTERLACED: {
      this->deinterlace = value;
      this->vo_scale.force_redraw = 1;
    }
    break;

    case VO_PROP_ASPECT_RATIO: {
      if (value >= NUM_ASPECT_RATIOS) {
        value = ASPECT_AUTO;
      }
      this->vo_scale.user_ratio = value;
      this->vo_scale.force_redraw = 1;
      vo_scale_compute_ideal_size(&this->vo_scale);     
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

static void pgx64_get_property_min_max(pgx64_driver_t *this, int property, int *min, int *max)
{
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

    default:
      *min = 0;
      *max = 0;
    break;
  }
}

static int pgx64_gui_data_exchange(pgx64_driver_t *this, int data_type, void *data)
{
  switch (data_type) {
    case XINE_GUI_SEND_DRAWABLE_CHANGED: {
      if (this->visual_type == XINE_VISUAL_TYPE_X11) {
        this->drawable = (Drawable)data;
#ifdef HAVE_X11
        XLockDisplay(this->display);
	XFreeGC(this->display, this->gc);
        this->gc = XCreateGC(this->display, this->drawable, 0, NULL);
        XUnlockDisplay(this->display);
#endif
      }
    }
    break;

    case XINE_GUI_SEND_EXPOSE_EVENT: {
      repaint_output_area(this);
    }
    break;

    case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO: {
      x11_rectangle_t *rect = data;
      int x1, y1, x2, y2;

      vo_scale_translate_gui2video(&this->vo_scale, rect->x, rect->y, &x1, &y1);
      vo_scale_translate_gui2video(&this->vo_scale, rect->x + rect->w, rect->y + rect->h, &x2, &y2);

      rect->x = x1;
      rect->y = y1;
      rect->w = x2 - x1;
      rect->h = y2 - y1;
    }
    break;
  }

  return 0;
}

static int pgx64_redraw_needed(pgx64_driver_t *this)
{
  if (vo_scale_redraw_needed(&this->vo_scale)) {
    if (this->current != NULL) {
      this->vo_scale.force_redraw = 1;
      pgx64_display_frame(this, this->current);
    }

    return 1;
  }

  return 0;
}

static void pgx64_dispose(pgx64_driver_t *this)
{
  write_reg(this, OVERLAY_EXCLUSIVE_HORZ, 0);
  write_reg(this, OVERLAY_SCALE_CNTL, 0);
  clear_reg_bits(this, BUS_CNTL, BUS_EXT_REG_EN);
  munmap(this->fbbase, ADDRSPACE);
  close(this->fbfd);

  pthread_mutex_lock(&this->class->mutex);
  this->class->instance_count--;
  pthread_mutex_unlock(&this->class->mutex);

  free(this);
}

static void pgx64_config_changed(pgx64_driver_t *this, xine_cfg_entry_t *entry)
{
  if (strcmp(entry->key, "video.pgx64_colour_key") == 0) {
    pgx64_set_property(this, VO_PROP_COLORKEY, entry->num_value);
  } 
  else if (strcmp(entry->key, "video.pgx64_brightness") == 0) {
    pgx64_set_property(this, VO_PROP_BRIGHTNESS, entry->num_value);
  }
  else if (strcmp(entry->key, "video.pgx64_saturation") == 0) {
    pgx64_set_property(this, VO_PROP_SATURATION, entry->num_value);
  }
  else if (strcmp(entry->key, "video.pgx64_deinterlace_method") == 0) {
    this->deinterlace_method = entry->num_value;
    this->vo_scale.force_redraw = 1;
  }
  else if (strcmp(entry->key, "video.pgx64_use_exclusive") == 0) {
    this->use_exclusive = entry->num_value;
    this->vo_scale.force_redraw = 1;
  }
}

/*
 * XINE VIDEO DRIVER CLASS FUNCTIONS
 */

static pgx64_driver_t* init_driver(pgx64_driver_class_t *class)
{
  pgx64_driver_t *this;
  char *devname;
  int fbfd;
  uint8_t *baseaddr;
  struct fbgattr attr;

  pthread_mutex_lock(&class->mutex);
  if (class->instance_count > 0) {
    pthread_mutex_unlock(&class->mutex);
    return NULL;
  }
  class->instance_count++;
  pthread_mutex_unlock(&class->mutex);

  devname = class->config->register_string(class->config, "video.pgx64_device", "/dev/fb", "name of pgx64 device", NULL, 10, NULL, NULL);
  if ((fbfd = open(devname, O_RDWR)) < 0) {
    printf("video_out_pgx64: can't open framebuffer device '%s'\n", devname);
    return NULL;
  }

  if (ioctl(fbfd, FBIOGATTR, &attr) < 0) {
    printf("video_out_pgx64: ioctl failed, unable to determine framebuffer characteristics\n");
    close(fbfd);
    return NULL;
  }

  if (attr.real_type != 22) {
    printf("video_out_pgx64: '%s' is not a mach64 framebuffer device\n", devname);
    close(fbfd);
    return NULL;
  }

  if ((baseaddr = mmap(0, ADDRSPACE, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0)) == MAP_FAILED) {
    printf("video_out_pgx64: unable to memory map framebuffer\n");
    close(fbfd);
    return NULL;
  }

  this = (pgx64_driver_t*)malloc(sizeof(pgx64_driver_t));
  if (!this) {
    printf("video_out_pgx64: driver malloc failed\n");
    return NULL;
  }
  memset(this, 0, sizeof(pgx64_driver_t));

  this->class   = class;
  this->current = NULL;

  this->vo_driver.get_capabilities     = (void*)pgx64_get_capabilities;
  this->vo_driver.alloc_frame          = (void*)pgx64_alloc_frame;
  this->vo_driver.update_frame_format  = (void*)pgx64_update_frame_format;
  this->vo_driver.overlay_begin        = NULL; /* not used */
  this->vo_driver.overlay_blend        = (void*)pgx64_overlay_blend;
  this->vo_driver.overlay_end          = NULL; /* not used */
  this->vo_driver.display_frame        = (void*)pgx64_display_frame;
  this->vo_driver.get_property         = (void*)pgx64_get_property;
  this->vo_driver.set_property         = (void*)pgx64_set_property;
  this->vo_driver.get_property_min_max = (void*)pgx64_get_property_min_max;
  this->vo_driver.gui_data_exchange    = (void*)pgx64_gui_data_exchange;
  this->vo_driver.redraw_needed        = (void*)pgx64_redraw_needed;
  this->vo_driver.dispose              = (void*)pgx64_dispose;

  this->colour_key = this->class->config->register_num(this->class->config, "video.pgx64_colour_key", 1, "video overlay colour key", NULL, 10, (void*)pgx64_config_changed, this);
  this->brightness = this->class->config->register_range(this->class->config, "video.pgx64_brightness", 0, -64, 63, "video overlay brightness", NULL, 10, (void*)pgx64_config_changed, this);
  this->saturation = this->class->config->register_range(this->class->config, "video.pgx64_saturation", 16, 0, 31, "video overlay saturation", NULL, 10, (void*)pgx64_config_changed, this);
  this->deinterlace_method = this->class->config->register_enum(this->class->config, "video.pgx64_deinterlace_method", 0, deinterlace_methods, "video deinterlacing method", NULL, 10, (void*)pgx64_config_changed, this);
  this->use_exclusive = this->class->config->register_bool(this->class->config, "video.pgx64_use_exclusive", 0, "use exclusive video overlays", NULL, 10, (void*)pgx64_config_changed, this);

  this->fbfd = fbfd;
  this->top = attr.sattr.dev_specific[0];
  this->fbbase = baseaddr;
  this->fbregs = baseaddr + REGBASE;
  this->fb_width = attr.fbtype.fb_width;
  this->fb_height = attr.fbtype.fb_height;
  this->depth_mask = 0xffffffff >> (32 - attr.fbtype.fb_depth);

  vo_scale_init(&this->vo_scale, 0, 0, this->class->config);
  this->vo_scale.user_ratio = ASPECT_AUTO;

  return this;
}

static void pgx64_dispose_class(pgx64_driver_class_t *this)
{
  pthread_mutex_destroy(&this->mutex);

  free(this);
}

#ifdef HAVE_X11
static vo_info_t vo_info_pgx64 = {
  10,
  XINE_VISUAL_TYPE_X11
};

static pgx64_driver_t* pgx64_init_driver(pgx64_driver_class_t *class, void *visual_gen)
{  
  pgx64_driver_t *this = init_driver(class);

  if (this == NULL) {
    return NULL;
  }

  this->display  = ((x11_visual_t*)visual_gen)->display;
  this->screen   = ((x11_visual_t*)visual_gen)->screen;
  this->drawable = ((x11_visual_t*)visual_gen)->d;
  this->gc       = XCreateGC(this->display, this->drawable, 0, NULL);

  this->vo_scale.user_data       = ((x11_visual_t*)visual_gen)->user_data;
  this->vo_scale.frame_output_cb = ((x11_visual_t*)visual_gen)->frame_output_cb;
  this->vo_scale.dest_size_cb    = ((x11_visual_t*)visual_gen)->dest_size_cb;

  this->visual_type = XINE_VISUAL_TYPE_X11;

  return this;
}

static char* pgx64_get_identifier(pgx64_driver_class_t *this)
{
  return "pgx64";
}

static char* pgx64_get_description(pgx64_driver_class_t *this)
{
  return "xine video output plugin for Sun PGX64/PGX24 framebuffers";
}

static pgx64_driver_class_t* pgx64_init_class(xine_t *xine, void *visual_gen)
{
  pgx64_driver_class_t *this;

  if ((this = (pgx64_driver_class_t*)malloc(sizeof(pgx64_driver_class_t))) == NULL) {
    printf("video_out_pgx64: driver class malloc failed\n");
    return NULL;
  }
  memset(this, 0, sizeof(pgx64_driver_class_t));

  this->vo_driver_class.open_plugin     = (void*)pgx64_init_driver;
  this->vo_driver_class.get_identifier  = (void*)pgx64_get_identifier;
  this->vo_driver_class.get_description = (void*)pgx64_get_description;
  this->vo_driver_class.dispose         = (void*)pgx64_dispose_class;

  this->xine   = xine;
  this->config = xine->config;

  pthread_mutex_init(&this->mutex, NULL);

  return this;
}
#endif

static vo_info_t vo_info_pgx64fb = {
  10,
  XINE_VISUAL_TYPE_FB
};

static pgx64_driver_t* pgx64fb_init_driver(pgx64_driver_class_t *class, void *visual_gen)
{
  pgx64_driver_t *this = init_driver(class);

  if (this == NULL) {
    return NULL;
  }

  this->vo_scale.user_data       = this;
  this->vo_scale.frame_output_cb = (void*)pgx64fb_output_callback;

  this->visual_type = XINE_VISUAL_TYPE_FB;

  return this;
}

static char* pgx64fb_get_identifier(pgx64_driver_class_t *this)
{
  return "pgx64fb";
}

static char* pgx64fb_get_description(pgx64_driver_class_t *this)
{
  return "xine video output plugin for Sun PGX64/PGX24 framebuffers";
}

static pgx64_driver_class_t* pgx64fb_init_class(xine_t *xine, void *visual_gen)
{
  pgx64_driver_class_t *this;

  if ((this = (pgx64_driver_class_t*)malloc(sizeof(pgx64_driver_class_t))) == NULL) {
    printf("video_out_pgx64: driver class malloc failed\n");
    return NULL;
  }
  memset(this, 0, sizeof(pgx64_driver_class_t));

  this->vo_driver_class.open_plugin     = (void*)pgx64fb_init_driver;
  this->vo_driver_class.get_identifier  = (void*)pgx64fb_get_identifier;
  this->vo_driver_class.get_description = (void*)pgx64fb_get_description;
  this->vo_driver_class.dispose         = (void*)pgx64_dispose_class;

  this->xine   = xine;
  this->config = xine->config;

  pthread_mutex_init(&this->mutex, NULL);

  return this;
}

plugin_info_t xine_plugin_info[] = {
#ifdef HAVE_X11
  {PLUGIN_VIDEO_OUT, 15, "pgx64", XINE_VERSION_CODE, &vo_info_pgx64, (void*)pgx64_init_class},
#endif
  {PLUGIN_VIDEO_OUT, 15, "pgx64fb", XINE_VERSION_CODE, &vo_info_pgx64fb, (void*)pgx64fb_init_class},
  {PLUGIN_NONE, 0, "", 0, NULL, NULL}
};
