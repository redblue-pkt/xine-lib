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
 * $Id: video_out_pgx64.c,v 1.37 2003/10/04 20:29:26 komadori Exp $
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
#include "yuv2rgb.h"
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

#define BUF_MODE_SINGLE 0
#define BUF_MODE_DOUBLE 1
#define BUF_MODE_MULTI  2

#define OVL_MODE_ALPHA_BLEND 0
#define OVL_MODE_EXCLUSIVE   1
#define OVL_MODE_CHROMA_KEY  2

static const char *overlay_modes[] = {
  "alpha blend to normal overlay",
  "alpha blend to exclusive overlay",
  "chroma key graphics",
  NULL
};

#define MAX_ALLOCED_FRAMES  15
#define MAX_DETAINED_FRAMES 10

typedef struct {
  video_driver_class_t vo_driver_class;

  xine_t *xine;
  config_values_t *config;

  pthread_mutex_t mutex;
  int instance_count;
} pgx64_driver_class_t;

typedef struct {
  vo_frame_t vo_frame;

  void *this;
  int lengths[3], stripe_lengths[3], stripe_offsets[3];
  int width, height, pitch, format, native_format, planes, buffer;
  double ratio;
} pgx64_frame_t;

typedef struct {   
  vo_driver_t vo_driver;
  vo_scale_t vo_scale;

  pgx64_driver_class_t *class;
  pgx64_frame_t *current, *previous, *detained[MAX_DETAINED_FRAMES];

  Display *display;
  int screen, depth;
  Drawable drawable;
  GC gc;
  Visual *visual;

  int fbfd, free_top, free_bottom, free_mark, fb_width, fb_height, fb_depth;
  int buf_mode, buffers[MAX_ALLOCED_FRAMES][3], dblbuf_select, num_frames;
  int detained_frames;
  uint8_t *vram, *fb_base, *buffer_ptrs[MAX_ALLOCED_FRAMES][3];
  volatile uint32_t *vregs;

  int ovl_mode, key_width, key_height, key_changed;
  XImage *key;
  pthread_mutex_t key_mutex;
  yuv2rgb_factory_t *yuv2rgb_factory;
  yuv2rgb_t *yuv2rgb;

  int delivered_format, colour_key, brightness, saturation, deinterlace_en;
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
 * Paint the output area with black borders and colour key (or chroma key image)
 */

static void repaint_output_area(pgx64_driver_t *this)
{
  int i;

  XLockDisplay(this->display);
  XSetForeground(this->display, this->gc, BlackPixel(this->display, this->screen));
  for (i=0; i<4; i++) {
    XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.border[i].x, this->vo_scale.border[i].y, this->vo_scale.border[i].w, this->vo_scale.border[i].h);
  }

  pthread_mutex_lock(&this->key_mutex);
  if (!this->key) {
    XSetForeground(this->display, this->gc, this->colour_key);
    XFillRectangle(this->display, this->drawable, this->gc, this->vo_scale.output_xoffset, this->vo_scale.output_yoffset, this->vo_scale.output_width, this->vo_scale.output_height);
  }
  else {
    XPutImage(this->display, this->drawable, this->gc, this->key, 0, 0, this->vo_scale.output_xoffset, this->vo_scale.output_yoffset, this->key_width, this->key_width);
  }
  pthread_mutex_unlock(&this->key_mutex);

  XFlush(this->display);
  XUnlockDisplay(this->display);
}

static void clear_key(pgx64_driver_t *this)
{
  int i;

  for (i=0; i<(this->key_width*this->key_height); i++) {
    ((uint32_t*)(void*)this->key->data)[i] = this->colour_key;
  }
}

/*
 * Allocate a portion of video memory
 */

static void vram_free_all(pgx64_driver_t* this)
{
  this->free_mark = this->free_top;
}

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
 * Release detained frames
 */

static void release_detained_frames(pgx64_driver_t* this)
{
  int i;

  if (this->detained_frames > 0) {
    printf("video_out_pgx64: Notice: %d detained frame(s) were released\n", this->detained_frames);
  }
  for (i=0; i<this->detained_frames; i++) {
    this->detained[i]->vo_frame.free(&this->detained[i]->vo_frame);
  }
  this->detained_frames = 0;
}

/*
 * Switch scaler buffers on next vertical sync
 */

static void switch_buffers(pgx64_driver_t* this)
{
  this->vregs[CAPTURE_CONFIG] = this->dblbuf_select ? le2me_32(CAPTURE_CONFIG_BUF1) : le2me_32(CAPTURE_CONFIG_BUF0);
  this->dblbuf_select = 1 - this->dblbuf_select;
}

/*
 * XINE VIDEO DRIVER FUNCTIONS
 */

static void pgx64_frame_copy(pgx64_frame_t *frame, uint8_t **src)
{
  pgx64_driver_t *this = (pgx64_driver_t*)frame->this;
  int i, len;

  frame->vo_frame.copy_called = 1;

  if ((this->buf_mode == BUF_MODE_MULTI) && (frame->buffer >= 0)) {
    for (i=0; i<frame->planes; i++) {
      len = (frame->lengths[i] - frame->stripe_offsets[i] < frame->stripe_lengths[i]) ? frame->lengths[i] - frame->stripe_offsets[i] : frame->stripe_lengths[i];
      memcpy(this->buffer_ptrs[frame->buffer][i]+frame->stripe_offsets[i], src[i], len);
      frame->stripe_offsets[i] += len;
    }
  }
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
  return VO_CAP_COPIES_IMAGE |
         VO_CAP_YV12 |
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
    printf("video_out_pgx64: Error: frame malloc failed\n");
    return NULL;
  }
  memset(frame, 0, sizeof(pgx64_frame_t));

  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  frame->vo_frame.copy    = NULL; 
  frame->vo_frame.field   = (void*)pgx64_frame_field; 
  frame->vo_frame.dispose = (void*)pgx64_frame_dispose;

  frame->this = (void*)this;

  return frame;
}

static void pgx64_update_frame_format(pgx64_driver_t *this, pgx64_frame_t *frame, uint32_t width, uint32_t height, double ratio, int format, int flags)
{
  if ((width != frame->width) ||
      (height != frame->height) ||
      (ratio != frame->ratio) ||
      (format != frame->format)) {
    dispose_frame_internals(frame);

    frame->width = width;
    frame->height = height;
    frame->ratio = ratio;
    frame->format = format;
    frame->pitch = ((width + 7) / 8) * 8;

    switch (format) {
      case XINE_IMGFMT_YUY2:
        frame->native_format = VIDEO_FORMAT_YUY2;
        frame->planes = 1;
        frame->buffer = -1;
        frame->vo_frame.pitches[0] = frame->pitch * 2;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->stripe_lengths[0] = frame->vo_frame.pitches[0] * 16;
        frame->vo_frame.base[0] = (void*)malloc(frame->lengths[0]);
      break;

      case XINE_IMGFMT_YV12:
        frame->native_format = VIDEO_FORMAT_YUV12;
        frame->planes = 3;
        frame->buffer = -1;
        frame->vo_frame.pitches[0] = frame->pitch;
        frame->vo_frame.pitches[1] = frame->pitch / 2;
        frame->vo_frame.pitches[2] = frame->pitch / 2;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->lengths[1] = frame->vo_frame.pitches[1] * (height / 2);
        frame->lengths[2] = frame->vo_frame.pitches[2] * (height / 2);
        frame->stripe_lengths[0] = frame->vo_frame.pitches[0] * 16;
        frame->stripe_lengths[1] = frame->vo_frame.pitches[1] * 8;
        frame->stripe_lengths[2] = frame->vo_frame.pitches[2] * 8;
        frame->vo_frame.base[0] = (void*)malloc(frame->lengths[0]);
        frame->vo_frame.base[1] = (void*)malloc(frame->lengths[1]);
        frame->vo_frame.base[2] = (void*)malloc(frame->lengths[2]);
      break;
    }
  }

  frame->stripe_offsets[0] = 0;
  frame->stripe_offsets[1] = 0;
  frame->stripe_offsets[2] = 0;
}

static void pgx64_display_frame(pgx64_driver_t *this, pgx64_frame_t *frame)
{
  if ((frame->width != this->vo_scale.delivered_width) ||
      (frame->height != this->vo_scale.delivered_height) ||
      (frame->ratio != this->vo_scale.delivered_ratio) ||
      (frame->format != this->delivered_format)) {
    this->vo_scale.delivered_width  = frame->width;
    this->vo_scale.delivered_height = frame->height;
    this->vo_scale.delivered_ratio  = frame->ratio;
    this->delivered_format          = frame->format;

    this->vo_scale.force_redraw = 1;
    vo_scale_compute_ideal_size(&this->vo_scale);

    vram_free_all(this);
    release_detained_frames(this);
    this->num_frames = 0;
    this->buf_mode = BUF_MODE_MULTI;  
  }

  if (vo_scale_redraw_needed(&this->vo_scale)) {  
    vo_scale_compute_output_size(&this->vo_scale);

    pthread_mutex_lock(&this->key_mutex);
    if ((this->ovl_mode == OVL_MODE_CHROMA_KEY) &&
        ((this->vo_scale.output_width != this->key_width) ||
         (this->vo_scale.output_height != this->key_height))) {
      this->key_width  = this->vo_scale.output_width;
      this->key_height = this->vo_scale.output_height;

      if (this->key) {
        XDestroyImage(this->key);
      }
      this->key = XCreateImage(this->display, this->visual, this->depth, ZPixmap, 0, NULL, this->key_width, this->key_height, 8, 0);
      if (this->key) {
        this->key->data = malloc(this->key_width * this->key_height * 4);
        if (!this->key->data) {
          XDestroyImage(this->key);
          this->key = NULL;
          pthread_mutex_unlock(&this->key_mutex);
          printf("video_out_pgx64: Error: key image malloc failed\n");
          return;
        }
      }
      else {
        pthread_mutex_unlock(&this->key_mutex);
        printf("video_out_pgx64: Error: key image malloc failed\n");
        return;
      }
      clear_key(this);
    }
    pthread_mutex_unlock(&this->key_mutex);

    repaint_output_area(this);

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

    if (frame->buffer >= 0) {
      for (i=0; i<frame->planes; i++) {
        this->vregs[scaler_regs_table[this->dblbuf_select][i]] = le2me_32(this->buffers[frame->buffer][i]);
      }
    }
    else {
      for (i=0; i<frame->planes; i++) {
        if ((this->buffers[this->num_frames][i] = vram_alloc(this, frame->lengths[i])) < 0) {
          if (this->detained_frames < MAX_DETAINED_FRAMES) {
            int buffer;

            if (this->num_frames < 1) {
              printf("video_out_pgx64: Error: insuffucient video memory for single-buffering\n");
              return;
            }
            buffer = (this->num_frames > 1) ? this->num_frames-2 : 0;
            for (i=0; i<frame->planes; i++) {
              this->vregs[scaler_regs_table[this->dblbuf_select][i]] = le2me_32(this->buffers[buffer][i]);
              memcpy(this->buffer_ptrs[buffer][i], frame->vo_frame.base[i], frame->lengths[i]);
            }
            printf("video_out_pgx64: Notice: a frame was detained owing to insuffucient video memory\n");
            this->detained[this->detained_frames++] = frame;
            switch_buffers(this);
            return;
          }
          else {
            printf("video_out_pgx64: Warning: insuffucient video memory for multi-buffering\n");
            release_detained_frames(this);
            if (this->num_frames > 1) {
              this->buf_mode = BUF_MODE_DOUBLE;
            }
            else {
              printf("video_out_pgx64: Warning: insuffucient video memory for double-buffering\n");
              this->buf_mode = BUF_MODE_SINGLE;
              this->dblbuf_select = 0;
            }
            for (i=0; i<frame->planes; i++) {
              this->vregs[scaler_regs_table[this->dblbuf_select][i]] = le2me_32(this->buffers[this->dblbuf_select][i]);
              memcpy(this->buffer_ptrs[this->dblbuf_select][i], frame->vo_frame.base[i], frame->lengths[i]);
            }
            frame->vo_frame.copy = NULL;
            if (this->buf_mode == BUF_MODE_DOUBLE) {
              switch_buffers(this);
            }
            return;
          }
        }
        else {
          this->buffer_ptrs[this->num_frames][i] = this->vram + this->buffers[this->num_frames][i];
        }
      }

      frame->buffer = this->num_frames++;
      for (i=0; i<frame->planes; i++) {
        this->vregs[scaler_regs_table[this->dblbuf_select][i]] = le2me_32(this->buffers[frame->buffer][i]);
        memcpy(this->buffer_ptrs[frame->buffer][i], frame->vo_frame.base[i], frame->lengths[i]);
      }
      frame->vo_frame.copy = (void*)pgx64_frame_copy;
    }
  }
  else {
    int i;

    for (i=0; i<frame->planes; i++) {
      memcpy(this->buffer_ptrs[this->dblbuf_select][i], frame->vo_frame.base[i], frame->lengths[i]);
    }
    frame->vo_frame.copy = NULL;
  }

  if (this->buf_mode != BUF_MODE_SINGLE) {
    switch_buffers(this);
  }

  if (this->previous != NULL) {
    this->previous->vo_frame.free(&this->previous->vo_frame);
  }
  this->previous = this->current;
  this->current = frame;
}

static void pgx64_overlay_blend(pgx64_driver_t *this, pgx64_frame_t *frame, vo_overlay_t *overlay)
{
  if (overlay->rle) {
    if ((this->buf_mode == BUF_MODE_MULTI) && (frame->buffer >= 0)) {
      /* FIXME: Implement out of place alphablending functions for better performance */
      switch (frame->format) {
        case XINE_IMGFMT_YV12: {
          blend_yuv(this->buffer_ptrs[frame->buffer], overlay, frame->width, frame->height, frame->vo_frame.pitches);
        }
        break;

        case XINE_IMGFMT_YUY2: {
          blend_yuy2(this->buffer_ptrs[frame->buffer][0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0]);
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

static void pgx64_overlay_key_begin(pgx64_driver_t *this, pgx64_frame_t *frame, int changed)
{
  if (changed) {
    pthread_mutex_lock(&this->key_mutex);
    if (!this->key) {
      pthread_mutex_unlock(&this->key_mutex);
      return;
    }
    clear_key(this);
    this->key_changed = 1;
  }
}

static void pgx64_overlay_key_blend(pgx64_driver_t *this, pgx64_frame_t *frame, vo_overlay_t *overlay)
{
  if (overlay->rle) {
    if (this->key_changed) {
      if (!overlay->rgb_clut) {
        int i;
        clut_t *clut;

        clut = (clut_t*)overlay->color;
        for (i=0; i<OVL_PALETTE_SIZE; i++) {
          overlay->color[i] = this->yuv2rgb->yuv2rgb_single_pixel_fun(this->yuv2rgb, clut[i].y, clut[i].cb, clut[i].cr);
        }
        overlay->rgb_clut = 1;
      }
      if (!overlay->clip_rgb_clut) {
        int i;
        clut_t *clut;

        clut = (clut_t*)overlay->clip_color;
        for (i=0; i<OVL_PALETTE_SIZE; i++) {
          overlay->clip_color[i] = this->yuv2rgb->yuv2rgb_single_pixel_fun(this->yuv2rgb, clut[i].y, clut[i].cb, clut[i].cr);
        }
        overlay->clip_rgb_clut = 1;
      }
      blend_rgb32((uint8_t *)this->key->data, overlay, this->key_width, this->key_height, frame->width, frame->height);
    }
  }
}

static void pgx64_overlay_key_end(pgx64_driver_t *this, pgx64_frame_t *frame)
{
  if (this->key_changed) {
    this->key_changed = 0;
    pthread_mutex_unlock(&this->key_mutex);
    repaint_output_area(this);
  }
}

static int pgx64_get_property(pgx64_driver_t *this, int property)
{
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

static int pgx64_set_property(pgx64_driver_t *this, int property, int value)
{
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

static int pgx64_gui_data_exchange(pgx64_driver_t *this, int data_type, void *data)
{
  switch (data_type) {
    case XINE_GUI_SEND_DRAWABLE_CHANGED: {
      this->drawable = (Drawable)data;
      XLockDisplay(this->display);
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
    this->vo_scale.force_redraw = 1;
    return 1;
  }

  return 0;
}

static void pgx64_dispose(pgx64_driver_t *this)
{
  this->yuv2rgb->dispose(this->yuv2rgb);
  this->yuv2rgb_factory->dispose(this->yuv2rgb_factory);
  if (this->key != NULL) {
    XDestroyImage(this->key);
  }
  this->vregs[OVERLAY_EXCLUSIVE_HORZ] = 0;
  this->vregs[OVERLAY_SCALE_CNTL] = 0;
  this->vregs[BUS_CNTL] &= le2me_32(~BUS_EXT_REG_EN);
  munmap(this->vram, FB_ADDRSPACE);
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
  else if (strcmp(entry->key, "video.pgx64_overlay_mode") == 0) {
    pthread_mutex_lock(&this->key_mutex);
    this->ovl_mode = entry->num_value;
    if ((entry->num_value == OVL_MODE_CHROMA_KEY) && (this->visual->class != TrueColor)) {
      printf("video_out_pgx64: Error: chroma keyed graphics require a true colour visual, reverting to alpha blending\n");
      this->ovl_mode = OVL_MODE_ALPHA_BLEND;
    }
    else {
      this->ovl_mode = entry->num_value;
    }
    if (this->ovl_mode == OVL_MODE_CHROMA_KEY) {
      this->vo_driver.overlay_begin = (void*)pgx64_overlay_key_begin;
      this->vo_driver.overlay_blend = (void*)pgx64_overlay_key_blend;
      this->vo_driver.overlay_end   = (void*)pgx64_overlay_key_end;
    }
    else {
      if (this->key != NULL) {
        XDestroyImage(this->key);
        this->key = NULL;
        this->key_width = 0;
        this->key_height = 0;
      }
      this->vo_driver.overlay_begin = NULL;
      this->vo_driver.overlay_blend = (void*)pgx64_overlay_blend;
      this->vo_driver.overlay_end   = NULL;
    }
    pthread_mutex_unlock(&this->key_mutex);
    this->vo_scale.force_redraw = 1;
  }
}

/*
 * XINE VIDEO DRIVER CLASS FUNCTIONS
 */

static void pgx64_dispose_class(pgx64_driver_class_t *this)
{
  pthread_mutex_destroy(&this->mutex);

  free(this);
}

static vo_info_t vo_info_pgx64 = {
  10,
  XINE_VISUAL_TYPE_X11
};

static pgx64_driver_t* pgx64_init_driver(pgx64_driver_class_t *class, void *visual_gen)
{  
  pgx64_driver_t *this;
  char *devname;
  int fbfd;
  void *baseaddr;
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
    printf("video_out_pgx64: Error: can't open framebuffer device '%s'\n", devname);
    return NULL;
  }

  if (ioctl(fbfd, FBIOGATTR, &attr) < 0) {
    printf("video_out_pgx64: Error: ioctl failed, unable to determine framebuffer characteristics\n");
    close(fbfd);
    return NULL;
  }

  if (attr.real_type != 22) {
    printf("video_out_pgx64: Error: '%s' is not a mach64 framebuffer device\n", devname);
    close(fbfd);
    return NULL;
  }

  if ((baseaddr = mmap(0, FB_ADDRSPACE, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0)) == MAP_FAILED) {
    printf("video_out_pgx64: Error: unable to memory map framebuffer\n");
    close(fbfd);
    return NULL;
  }

  this = (pgx64_driver_t*)malloc(sizeof(pgx64_driver_t));
  if (!this) {
    printf("video_out_pgx64: Error: driver malloc failed\n");
    return NULL;
  }
  memset(this, 0, sizeof(pgx64_driver_t));

  this->vo_driver.get_capabilities     = (void*)pgx64_get_capabilities;
  this->vo_driver.alloc_frame          = (void*)pgx64_alloc_frame;
  this->vo_driver.update_frame_format  = (void*)pgx64_update_frame_format;
  this->vo_driver.overlay_begin        = NULL;
  this->vo_driver.overlay_blend        = (void*)pgx64_overlay_blend;
  this->vo_driver.overlay_end          = NULL;
  this->vo_driver.display_frame        = (void*)pgx64_display_frame;
  this->vo_driver.get_property         = (void*)pgx64_get_property;
  this->vo_driver.set_property         = (void*)pgx64_set_property;
  this->vo_driver.get_property_min_max = (void*)pgx64_get_property_min_max;
  this->vo_driver.gui_data_exchange    = (void*)pgx64_gui_data_exchange;
  this->vo_driver.redraw_needed        = (void*)pgx64_redraw_needed;
  this->vo_driver.dispose              = (void*)pgx64_dispose;
  
  vo_scale_init(&this->vo_scale, 0, 0, class->config);
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

  this->fbfd = fbfd;
  this->vram = (uint8_t*)baseaddr;
  this->vregs = (uint32_t*)baseaddr + FB_REGSBASE;
  this->free_top = attr.sattr.dev_specific[0];
  this->free_bottom = attr.sattr.dev_specific[5] + attr.fbtype.fb_size;
  this->fb_base = this->vram + attr.sattr.dev_specific[5];
  this->fb_width = attr.fbtype.fb_width;
  this->fb_height = attr.fbtype.fb_height;
  this->fb_depth = attr.fbtype.fb_depth;

  this->colour_key = class->config->register_num(this->class->config, "video.pgx64_colour_key", 1, "video overlay colour key", NULL, 10, (void*)pgx64_config_changed, this);
  this->brightness = class->config->register_range(this->class->config, "video.pgx64_brightness", 0, -64, 63, "video overlay brightness", NULL, 10, (void*)pgx64_config_changed, this);
  this->saturation = class->config->register_range(this->class->config, "video.pgx64_saturation", 16, 0, 31, "video overlay saturation", NULL, 10, (void*)pgx64_config_changed, this);
  this->ovl_mode = class->config->register_enum(this->class->config, "video.pgx64_overlay_mode", 0, (char**)overlay_modes, "video overlay mode", NULL, 10, (void*)pgx64_config_changed, this);

  pthread_mutex_init(&this->key_mutex, NULL);
  if ((this->ovl_mode == OVL_MODE_CHROMA_KEY) && (this->visual->class != TrueColor)) {
    printf("video_out_pgx64: Error: chroma keyed graphics require a true colour visual, reverting to alpha blending\n");
    this->ovl_mode = OVL_MODE_ALPHA_BLEND;
  }
  else if (this->ovl_mode == OVL_MODE_CHROMA_KEY) {
    this->vo_driver.overlay_begin = (void*)pgx64_overlay_key_begin;
    this->vo_driver.overlay_blend = (void*)pgx64_overlay_key_blend;
    this->vo_driver.overlay_end   = (void*)pgx64_overlay_key_end;
  }
  if (((this->yuv2rgb_factory = yuv2rgb_factory_init(MODE_32_RGB, 0, NULL)) == NULL) ||
      ((this->yuv2rgb = this->yuv2rgb_factory->create_converter(this->yuv2rgb_factory)) == NULL)) {
    printf("video_out_pgx64: Error: yuv2rgb malloc failed\n");
    free(this);
    return NULL;
  }

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
  pgx64_driver_class_t *class;

  class = (pgx64_driver_class_t*)malloc(sizeof(pgx64_driver_class_t));
  if (!class) {
    printf("video_out_pgx64: Error: driver class malloc failed\n");
    return NULL;
  }
  memset(class, 0, sizeof(pgx64_driver_class_t));

  class->vo_driver_class.open_plugin     = (void*)pgx64_init_driver;
  class->vo_driver_class.get_identifier  = (void*)pgx64_get_identifier;
  class->vo_driver_class.get_description = (void*)pgx64_get_description;
  class->vo_driver_class.dispose         = (void*)pgx64_dispose_class;

  class->xine   = xine;
  class->config = xine->config;

  pthread_mutex_init(&class->mutex, NULL);

  return class;
}

plugin_info_t xine_plugin_info[] = {
  {PLUGIN_VIDEO_OUT, 16, "pgx64", XINE_VERSION_CODE, &vo_info_pgx64, (void*)pgx64_init_class},
  {PLUGIN_NONE, 0, "", 0, NULL, NULL}
};
