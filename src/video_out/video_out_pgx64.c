/* 
 * Copyright (C) 2000-2002 the xine project
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * $Id: video_out_pgx64.c,v 1.3 2002/09/05 20:44:42 mroi Exp $
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

#include "video_out.h"
#include "video_out_x11.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "alphablend.h"

#define ADDRSPACE 8388608
#define REGBASE   8386560

#define BUS_CNTL 0x128
#define BUS_EXT_REG_EN 0x08000000
#define FIFO_STAT 0x1C4

#define OVERLAY_SCALE_INC 0x008
#define SCALER_HEIGHT_WIDTH 0x00A
#define OVERLAY_GRAPHICS_KEY_CLR 0x004
#define OVERLAY_GRAPHICS_KEY_MSK 0x005
#define OVERLAY_KEY_CNTL 0x006
#define SCALER_COLOUR_CNTL 0x054

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
#define OVERLAY_SCALE_CNTL 0x009
#define OVERLAY_EN 0xC0000000

#define DEINTERLACE_ONEFIELD         0
#define DEINTERLACE_LINEARBLEND      1
#define DEINTERLACE_LINEARBLEND_VIS  2

static char *deinterlace_methods[] = {"one field",
                                      "linear blend",
#ifdef ENABLE_VIS
                                      "linear blend (VIS)",
#endif
                                      NULL};

typedef struct {   
  vo_driver_t vo_driver;
  config_values_t *config;
  Display *display;
  int screen;
  Window root;
  Drawable drawable;
  GC gc;
  void *user_data;
  void (*frame_output_cb) (void *user_data, int video_width, int video_height, int *dest_x, int *dest_y, int *dest_width, int *dest_height, int *win_x, int *win_y);

  int fbfd;
  void *fbbase;
  volatile uint32_t *fbregs;
  uint32_t top, buf_y, buf_u, buf_v;

  int colour_key, brightness, saturation, ratio_property;
  int force_update, deinterlace, deinterlace_method;
  double frame_ratio, image_ratio, unscale_x, unscale_y;
  int dest_x, dest_y, old_dest_x, old_dest_y;
  int dest_width, dest_height, old_dest_width, old_dest_height;
  int win_x, win_y, old_win_x, old_win_y;
  int display_width, display_height, display_xoffset, display_yoffset;
  int correct_width, correct_height;
} pgx64_driver_t;

typedef struct {
  vo_frame_t vo_frame;

  int lengths[3];  
  int width, height, ratio_code, format;
} pgx64_frame_t;

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
 * Swap the byte order of a 32 bit word
 */

static inline uint32_t swap_uint32(uint32_t value)
{
#ifdef WORDS_BIGENDIAN
  return ((((value) & 0xff000000) >> 24) |
          (((value) & 0x00ff0000) >>  8) |
          (((value) & 0x0000ff00) <<  8) |
          (((value) & 0x000000ff) << 24));
#else
  return value;
#endif
}

/*
 * Read and write to the little endian framebuffer registers
 */

static inline uint32_t read_reg(pgx64_driver_t *this, int reg)
{
  return swap_uint32(this->fbregs[reg]);
}

static inline void write_reg(pgx64_driver_t *this, int reg, uint32_t value)
{
  this->fbregs[reg] = swap_uint32(value);
}

static inline void set_reg_bits(pgx64_driver_t *this, int reg, uint32_t mask)
{
  this->fbregs[reg] |= swap_uint32(mask);
}

static inline void clear_reg_bits(pgx64_driver_t *this, int reg, uint32_t mask)
{
  this->fbregs[reg] &= swap_uint32(~mask);
}

/*
 * Read and write to the graphics status register of VIS(TM) capable processors
 */

#ifdef ENABLE_VIS
static inline uint32_t read_gsr()
{
  uint32_t gsr;
  asm ("rd	%%gsr, %0" : "=r" (gsr));
  return gsr;
}

static inline void write_gsr(uint32_t gsr)
{
  asm ("wr	%0, %%g0, %%gsr" : : "r" (gsr));
}
#endif

/*
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 * All the following functions are defined by the xine video_out API
 * !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT! !IMPORTANT!
 */

static void pgx64_config_changed(pgx64_driver_t *this, cfg_entry_t *entry)
{
  if (strcmp(entry->key, "video.pgx64_colour_key") == 0) {
    this->colour_key = entry->num_value;
    this->force_update = -1;
  } else if (strcmp(entry->key, "video.pgx64_brightness") == 0) {
    this->brightness = entry->num_value;
    write_reg(this, SCALER_COLOUR_CNTL, (this->saturation<<16) | (this->saturation<<8) | (this->brightness&0x7F));
  } else if (strcmp(entry->key, "video.pgx64_saturation") == 0) {
    this->saturation = entry->num_value;
    write_reg(this, SCALER_COLOUR_CNTL, (this->saturation<<16) | (this->saturation<<8) | (this->brightness&0x7F));
  } else if (strcmp(entry->key, "video.pgx64_deinterlace_method") == 0) {
    this->deinterlace_method = entry->num_value;
    this->force_update = -1;
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

    switch (format) {
      case IMGFMT_YUY2:
        frame->vo_frame.pitches[0] = width * 2;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->vo_frame.base[0] = (void*)memalign(8, frame->lengths[0]);
        this->buf_y = (this->top - frame->lengths[0]) & ~0x07;
      break;

      case IMGFMT_YV12:
        frame->vo_frame.pitches[0] = width;
        frame->vo_frame.pitches[1] = width / 2;
        frame->vo_frame.pitches[2] = width / 2;
        frame->lengths[0] = frame->vo_frame.pitches[0] * height;
        frame->lengths[1] = frame->vo_frame.pitches[1] * height;
        frame->lengths[2] = frame->vo_frame.pitches[2] * height;
        frame->vo_frame.base[0] = (void*)memalign(8, frame->lengths[0]);
        frame->vo_frame.base[1] = (void*)memalign(8, frame->lengths[1]);
        frame->vo_frame.base[2] = (void*)memalign(8, frame->lengths[2]);
        this->buf_y = (this->top - frame->lengths[0]) & ~0x07;
        this->buf_u = (this->buf_y - frame->lengths[1]) & ~0x07;
        this->buf_v = (this->buf_u - frame->lengths[2]) & ~0x07;
      break;
    }

    this->image_ratio = (double)width / (double)height;
    switch (ratio_code) {
      default:
      case XINE_ASPECT_RATIO_DONT_TOUCH:
      case XINE_ASPECT_RATIO_SQUARE:
        this->frame_ratio = this->image_ratio;
      break;
      case XINE_ASPECT_RATIO_4_3:
        this->frame_ratio = 4.0 / 3.0;
      break;
      case XINE_ASPECT_RATIO_ANAMORPHIC:
        this->frame_ratio = 16.0 / 9.0;
      break;
      case XINE_ASPECT_RATIO_211_1:
        this->frame_ratio = 2.11;
      break;
    }

    frame->width = width;
    frame->height = height;
    frame->ratio_code = ratio_code;
    frame->format = format;    
    this->force_update = -1;
  }
}

static void pgx64_display_frame(pgx64_driver_t *this, pgx64_frame_t *frame)
{
  if (this->force_update) {
    double ratio;

    switch (this->ratio_property) {
      default:
      case ASPECT_AUTO:
        ratio = this->frame_ratio;
      break;
      case ASPECT_ANAMORPHIC:
        ratio = 16.0 / 9.0;
      break;
      case ASPECT_FULL:
        ratio = 4.0 / 3.0;
      break;
      case ASPECT_DVB:
        ratio = 2.0;
      break;
      case ASPECT_SQUARE:
        ratio = this->image_ratio;
      break;
    }

    ratio /= this->image_ratio;
    if (ratio >= 1.0) {
      this->correct_width = frame->width * ratio;
      this->correct_height = frame->height;
    }
    else {
      this->correct_width = frame->width;
      this->correct_height = frame->height / ratio;
    }
  }

  this->frame_output_cb(this->user_data, this->correct_width, this->correct_height, &this->dest_x, &this->dest_y, &this->dest_width, &this->dest_height, &this->win_x, &this->win_y);

  if ((this->win_x == 0) ||
      (this->win_y == 0)) {
    Window temp_window;

    XLockDisplay(this->display);
    XTranslateCoordinates(this->display, this->drawable, this->root, 0, 0, &this->win_x, &this->win_y, &temp_window);
    XUnlockDisplay(this->display);
  }

  if ((this->force_update) ||
      (this->old_dest_x != this->dest_x) ||
      (this->old_dest_y != this->dest_y) ||
      (this->old_dest_width != this->dest_width) ||
      (this->old_dest_height != this->dest_height) ||
      (this->old_win_x != this->win_x) ||
      (this->old_win_y != this->win_y)) {
    double scale_fitw, scale_fith;

    this->display_width = this->correct_width;
    this->display_height = this->correct_height;
    scale_fitw = (double)this->dest_width / (double)this->display_width;
    scale_fith = (double)this->dest_height / (double)this->display_height;
    if (scale_fitw < scale_fith) {
      this->display_width *= scale_fitw;
      this->display_height *= scale_fitw;
    }
    else {
      this->display_width *= scale_fith;
      this->display_height *= scale_fith;
    }
    this->display_xoffset = (this->dest_width - this->display_width) / 2 + this->dest_x;
    this->display_yoffset = (this->dest_height - this->display_height) / 2 + this->dest_y;

    XLockDisplay(this->display);
    XSetForeground(this->display, this->gc, BlackPixel(this->display, this->screen));
    XFillRectangle(this->display, this->drawable, this->gc, this->dest_x, this->dest_y, this->display_width, this->display_yoffset - this->dest_y);
    XFillRectangle(this->display, this->drawable, this->gc, this->dest_x, this->display_yoffset + this->display_height, this->dest_width, this->dest_height - (this->display_yoffset + this->display_height));
    XFillRectangle(this->display, this->drawable, this->gc, this->dest_x, this->dest_y, this->display_xoffset - this->dest_y, this->display_height);
    XFillRectangle(this->display, this->drawable, this->gc, this->display_xoffset + this->display_width, this->dest_y, this->dest_width - (this->display_xoffset + this->display_width), this->dest_height);
    XSetForeground(this->display, this->gc, this->colour_key);
    XFillRectangle(this->display, this->drawable, this->gc, this->display_xoffset, this->display_yoffset, this->display_width, this->display_height);
    XFlush(this->display);
    XUnlockDisplay(this->display);

    write_reg(this, VIDEO_FORMAT, (frame->format == IMGFMT_YUY2) ? VIDEO_FORMAT_YUY2 : VIDEO_FORMAT_YUV12);
    write_reg(this, SCALER_BUF0_OFFSET, this->buf_y);
    write_reg(this, SCALER_BUF0_OFFSET_U, this->buf_u);
    write_reg(this, SCALER_BUF0_OFFSET_V, this->buf_v);
    write_reg(this, SCALER_BUF_PITCH, this->deinterlace && (this->deinterlace_method == DEINTERLACE_ONEFIELD) ? frame->width*2 : frame->width);
    write_reg(this, OVERLAY_X_Y_START, ((this->win_x + this->display_xoffset) << 16) | (this->win_y + this->display_yoffset) | OVERLAY_X_Y_LOCK);
    write_reg(this, OVERLAY_X_Y_END, ((this->win_x + this->display_xoffset + this->display_width) << 16) | (this->win_y + this->display_yoffset + this->display_height));
    write_reg(this, OVERLAY_GRAPHICS_KEY_CLR, this->colour_key);
    write_reg(this, OVERLAY_SCALE_INC, (((frame->width << 12) / this->display_width) << 16) | (((this->deinterlace && (this->deinterlace_method == DEINTERLACE_ONEFIELD) ? frame->height/2 : frame->height) << 12) / this->display_height));
    write_reg(this, SCALER_HEIGHT_WIDTH, (frame->width << 16) | (this->deinterlace && (this->deinterlace_method == DEINTERLACE_ONEFIELD) ? frame->height/2 : frame->height));
    set_reg_bits(this, OVERLAY_SCALE_CNTL, OVERLAY_EN);

    this->unscale_x = (double)frame->width / (double)this->display_width;
    this->unscale_y = (double)frame->height / (double)this->display_height;

    this->force_update = 0;
    this->old_dest_x = this->dest_x;
    this->old_dest_y = this->dest_y;
    this->old_dest_width = this->dest_width;
    this->old_dest_height = this->dest_height;
    this->old_win_x = this->win_x;
    this->old_win_y = this->win_y;
  }

  if (this->deinterlace && (frame->format == IMGFMT_YV12)) {
    switch (this->deinterlace_method) {
      case DEINTERLACE_LINEARBLEND: {
        register uint8_t *p = frame->vo_frame.base[0];
        register uint8_t *endp = p+(frame->width*(frame->height-2));

        for (;p != endp;p++) {
          p[0] = (p[0] + p[frame->width]*2 + p[2*frame->width]) >> 2;
        }

        break;
      }

#ifdef ENABLE_VIS
      case DEINTERLACE_LINEARBLEND_VIS: {
        register uint32_t *p = (uint32_t*)frame->vo_frame.base[0];
        register uint32_t *endp = p+((frame->width>>2)*(frame->height-2));
        register uint32_t width = frame->width;

        write_gsr((read_gsr() & 0xffffff07) | 0x000000008);

        for (;p != endp;p++) {
          asm volatile("ld	[%0], %%f0\n\t"
                       "add	%0, %1, %%l0\n\t"
                       "fexpand	%%f0, %%f6\n\t"
                       "ld	[%%l0], %%f2\n\t"
                       "add	%%l0, %1, %%l1\n\t"
                       "fexpand	%%f2, %%f8\n\t"
                       "ld	[%%l1], %%f4\n\t"
                       "fpadd16	%%f6, %%f8, %%f0\n\t"
                       "fexpand	%%f4, %%f10\n\t"
                       "fpadd16	%%f0, %%f8, %%f2\n\t"
                       "fpadd16	%%f2, %%f10, %%f4\n\t"
                       "fpack16	%%f4, %%f0\n\t"
                       "st	%%f0, [%0]"
                       : : "r" (p), "r" (width)
                       : "%f0", "%f1", "%f2", "%f3", "%f4", "%f5",
                         "%f6", "%f7", "%f8", "%f9", "%f10", "%f11",
                         "%l0", "%l1");
        }

        break;
      }
#endif

    }
  }

  memcpy(this->fbbase+this->buf_y, frame->vo_frame.base[0], frame->lengths[0]);
  if (frame->format == IMGFMT_YV12) {
    memcpy(this->fbbase+this->buf_u, frame->vo_frame.base[1], frame->lengths[1]);
    memcpy(this->fbbase+this->buf_v, frame->vo_frame.base[2], frame->lengths[2]);
  }

  frame->vo_frame.displayed(&frame->vo_frame);
}

static void pgx64_overlay_blend(pgx64_driver_t *this, pgx64_frame_t *frame, vo_overlay_t *overlay)
{
  if (overlay->rle) {
    if (frame->format == IMGFMT_YV12) {
      blend_yuv(frame->vo_frame.base, overlay, frame->width, frame->height);
    }
    else {
      blend_yuy2(frame->vo_frame.base[0], overlay, frame->width, frame->height);
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
      return this->ratio_property;
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
  printf("video_out_pgx64: Propery %d was set to 0x%08x\n", property, value);

  switch (property) {
    case VO_PROP_INTERLACED: {
      this->deinterlace = value;
      this->force_update = -1;
    }
    break;

    case VO_PROP_ASPECT_RATIO: {
      if (value >= NUM_ASPECT_RATIOS) {
        value = ASPECT_AUTO;
      }
      this->ratio_property = value;
      this->force_update = -1;    }
    break;

    case VO_PROP_SATURATION: {
      this->saturation = value;
      write_reg(this, SCALER_COLOUR_CNTL, (this->saturation<<16) | (this->saturation<<8) | (this->brightness&0x7F));
    }
    break;

    case VO_PROP_BRIGHTNESS: {
      this->brightness = value;
      write_reg(this, SCALER_COLOUR_CNTL, (this->saturation<<16) | (this->saturation<<8) | (this->brightness&0x7F));
    }
    break;

    case VO_PROP_COLORKEY: {
      this->colour_key = value;
      this->force_update = -1;
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
    case GUI_DATA_EX_DRAWABLE_CHANGED: {
      this->drawable = (Drawable)data;
      XLockDisplay(this->display);
      this->gc = XCreateGC(this->display, this->drawable, 0, NULL);
      XUnlockDisplay(this->display);
    }
    break;

    case GUI_DATA_EX_EXPOSE_EVENT: {
      this->force_update = -1;
    }
    break;

    case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO: {
      x11_rectangle_t *rect = data;

      rect->x = (rect->x - this->display_xoffset) * this->unscale_x;
      rect->y = (rect->y - this->display_yoffset) * this->unscale_x;
      rect->w = (rect->w - this->display_xoffset) * this->unscale_y;
      rect->h = (rect->h - this->display_yoffset) * this->unscale_y;
    }
    break;

    case GUI_DATA_EX_VIDEOWIN_VISIBLE: {
      if (!((int *)data)) {
        clear_reg_bits(this, OVERLAY_SCALE_CNTL, OVERLAY_EN);
      }
      else {
        set_reg_bits(this, OVERLAY_SCALE_CNTL, OVERLAY_EN);
      }
    }
    break;
  }

  return 0;
}

static int pgx64_redraw_needed(pgx64_driver_t *this)
{
  return -1;
}

static void pgx64_exit(pgx64_driver_t *this)
{
  write_reg(this, OVERLAY_SCALE_CNTL, 0);
  clear_reg_bits(this, BUS_CNTL, BUS_EXT_REG_EN);
  munmap(this->fbbase, ADDRSPACE);
  close(this->fbfd);
}

static void* init_video_out_plugin(config_values_t *config, void *visual_gen) {
  pgx64_driver_t *this;
  char *devname;
  int fbfd;
  void *baseaddr;
  struct fbgattr attr;

  printf("video_out_pgx64: PGX64 video output plugin - By Robin Kay\n");

  devname = config->register_string (config, "video.pgx64_device", "/dev/m640", "name of pgx64 device", NULL, NULL, NULL);
  if ((fbfd = open(devname, O_RDWR)) < 0) {
    printf("video_out_pgx64: can't open framebuffer device (%s)\n", devname);
    return NULL;
  }

  if (ioctl(fbfd, FBIOGATTR, &attr) < 0) {
    printf("video_out_pgx64: unable to determine amount of available video memory\n");
    close(fbfd);
    return NULL;
  }

  if ((baseaddr = mmap(baseaddr, ADDRSPACE, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0)) == MAP_FAILED) {
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

  this->vo_driver.get_capabilities     = (void*)pgx64_get_capabilities;
  this->vo_driver.alloc_frame          = (void*)pgx64_alloc_frame;
  this->vo_driver.update_frame_format  = (void*)pgx64_update_frame_format;
  this->vo_driver.overlay_blend        = (void*)pgx64_overlay_blend;
  this->vo_driver.display_frame        = (void*)pgx64_display_frame;
  this->vo_driver.get_property         = (void*)pgx64_get_property;
  this->vo_driver.set_property         = (void*)pgx64_set_property;
  this->vo_driver.get_property_min_max = (void*)pgx64_get_property_min_max;
  this->vo_driver.gui_data_exchange    = (void*)pgx64_gui_data_exchange;
  this->vo_driver.redraw_needed        = (void*)pgx64_redraw_needed;
  this->vo_driver.exit                 = (void*)pgx64_exit;

  this->config = config;

  this->display = ((x11_visual_t*)visual_gen)->display;
  this->screen = ((x11_visual_t*)visual_gen)->screen;
  this->root = RootWindow(this->display, this->screen);
  this->drawable = ((x11_visual_t*)visual_gen)->d;
  this->gc = XCreateGC(this->display, this->drawable, 0, NULL);
  this->user_data = ((x11_visual_t*)visual_gen)->user_data;
  this->frame_output_cb = ((x11_visual_t*)visual_gen)->frame_output_cb;

  this->colour_key = config->register_num(config, "video.pgx64_colour_key", 1, "video overlay colour key", NULL, (void*)pgx64_config_changed, this);
  this->brightness = config->register_range(config, "video.pgx64_brightness", 0, -64, 63, "video overlay brightness", NULL, (void*)pgx64_config_changed, this);
  this->saturation = config->register_range(config, "video.pgx64_saturation", 16, 0, 31, "video overlay saturation", NULL, (void*)pgx64_config_changed, this);
  this->deinterlace_method = config->register_enum(config, "video.pgx64_deinterlace_method", 0, deinterlace_methods, "video deinterlacing method", NULL, (void*)pgx64_config_changed, this);

  this->fbfd = fbfd;
  this->top = attr.sattr.dev_specific[0];
  this->fbbase = baseaddr;
  this->fbregs = baseaddr + REGBASE;

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
  write_reg(this, OVERLAY_GRAPHICS_KEY_MSK, (1 << DefaultDepth(this->display, this->screen)) - 1);

  return this;
}

static vo_info_t vo_info_pgx64 = {
  6,
  "PGX64",
  "xine video output plugin for Sun PGX6/PGX24 framebuffers",
  VISUAL_TYPE_X11,
  10
};

vo_info_t* get_video_out_plugin_info()
{
  return &vo_info_pgx64;
}
