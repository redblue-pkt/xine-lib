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
 * $Id: video_out_dxr3.c,v 1.30 2002/06/24 17:40:44 pmhahn Exp $
 */
 
/* mpeg1 encoding video out plugin for the dxr3.  
 *
 * modifications to the original dxr3 video out plugin by 
 * Mike Lampard <mike at web2u.com.au>
 * this first standalone version by 
 * Harm van der Heijden <hrm at users.sourceforge.net>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef HAVE_XINERAMA
#  include <X11/extensions/Xinerama.h>
#endif

#include "xine_internal.h"
#include "xineutils.h"
#include "video_out.h"
#include "../video_out/video_out_x11.h"
#include "alphablend.h"
#include "dxr3.h"
#include "video_out_dxr3.h"

#define LOG_VID 1
#define LOG_OVR 0


/* plugin initialization functions */
vo_info_t         *get_video_out_plugin_info();
vo_driver_t       *init_video_out_plugin(config_values_t *config, void *visual_gen);

/* functions required by xine api */
static uint32_t    dxr3_get_capabilities(vo_driver_t *this_gen);
static vo_frame_t *dxr3_alloc_frame(vo_driver_t *this_gen);
static void        dxr3_frame_copy(vo_frame_t *frame_gen, uint8_t **src);
static void        dxr3_frame_field(vo_frame_t *vo_img, int which_field);
static void        dxr3_frame_dispose(vo_frame_t *frame_gen);
static void        dxr3_update_frame_format(vo_driver_t *this_gen, vo_frame_t *frame_gen,
                                            uint32_t width, uint32_t height,
                                            int ratio_code, int format, int flags);
static void        dxr3_overlay_blend(vo_driver_t *this_gen, vo_frame_t *frame_gen,
                                      vo_overlay_t *overlay);
static void        dxr3_display_frame(vo_driver_t *this_gen, vo_frame_t *frame_gen);
static int         dxr3_redraw_needed(vo_driver_t *this_gen);
static int         dxr3_get_property(vo_driver_t *this_gen, int property);
static int         dxr3_set_property(vo_driver_t *this_gen, int property, int value);
static void        dxr3_get_property_min_max(vo_driver_t *this_gen, int property,
                                              int *min, int *max);
static int         dxr3_gui_data_exchange(vo_driver_t *this_gen,
                                          int data_type, void *data);
static void        dxr3_exit(vo_driver_t *this_gen);

/* overlay helper functions only called once during plugin init */
static void        gather_screen_vars(dxr3_driver_t *this, x11_visual_t *vis);
static int         dxr3_overlay_read_state(dxr3_overlay_t *this);
static int         dxr3_overlay_set_keycolor(dxr3_overlay_t *this);
static int         dxr3_overlay_set_attributes(dxr3_overlay_t *this);

/* overlay helper functions */
static void        dxr3_overlay_update(dxr3_driver_t *this);
static void        dxr3_zoomTV(dxr3_driver_t *this);
static void        dxr3_translate_gui2video(dxr3_driver_t *this, int x, int y,
                                            int *vid_x, int *vid_y);
static int         is_fullscreen(dxr3_driver_t *this);

/* config callbacks */
static void        dxr3_update_add_bars(void *data, cfg_entry_t *entry);
static void        dxr3_update_swap_fields(void *data, cfg_entry_t *entry);
static void        dxr3_update_enhanced_mode(void *this_gen, cfg_entry_t *entry);


vo_info_t *get_video_out_plugin_info()
{
  static vo_info_t vo_info_dxr3 = {
    5, /* api version */
    "dxr3",
    NULL,
    VISUAL_TYPE_X11,
    10  /* priority */
  };

  vo_info_dxr3.description = _("xine video output plugin for dxr3 cards");
  return &vo_info_dxr3;
}

vo_driver_t *init_video_out_plugin(config_values_t *config, void *visual_gen)
{
  dxr3_driver_t *this;
  char tmpstr[100];
  const char *confstr;
  int dashpos, encoder, confnum;
  static char *available_encoders[SUPPORTED_ENCODER_COUNT + 1];
  static char *videoout_modes[] = { "tv", "overlay", NULL };
  static char *tv_modes[] = { "ntsc", "pal", "pal60" , "default", NULL };

  this = (dxr3_driver_t *)malloc(sizeof(dxr3_driver_t));
  if (!this) return NULL;
  memset(this, 0, sizeof(dxr3_driver_t));

  this->vo_driver.get_capabilities     = dxr3_get_capabilities;
  this->vo_driver.alloc_frame          = dxr3_alloc_frame;
  this->vo_driver.update_frame_format  = dxr3_update_frame_format;
  this->vo_driver.overlay_blend        = dxr3_overlay_blend;
  this->vo_driver.display_frame        = dxr3_display_frame;
  this->vo_driver.redraw_needed        = dxr3_redraw_needed;
  this->vo_driver.get_property         = dxr3_get_property;
  this->vo_driver.set_property         = dxr3_set_property;
  this->vo_driver.get_property_min_max = dxr3_get_property_min_max;
  this->vo_driver.gui_data_exchange    = dxr3_gui_data_exchange;
  this->vo_driver.exit                 = dxr3_exit;
  
  this->config                         = config;
  this->aspect                         = ASPECT_FULL;
  this->overlay_enabled                = 0;
  this->swap_fields                    = config->register_bool(config,
    "dxr3.enc_swap_fields", 0, _("swap odd and even lines"), 
    NULL, dxr3_update_swap_fields, this);
  this->add_bars                       = config->register_bool(config,
    "dxr3.enc_add_bars", 1, _("Add black bars to correct aspect ratio"),
    _("If disabled, will assume source has 4:3 aspect ratio."), dxr3_update_add_bars, this);
  this->enhanced_mode                  = config->register_bool(config,
    "dxr3.enc_alt_play_mode", 1,
    _("dxr3: use alternate play mode for mpeg encoder playback"),
    _("Enabling this option will utilise a smoother play mode."), dxr3_update_enhanced_mode, this);
  
  confstr = config->register_string(config, CONF_LOOKUP, CONF_DEFAULT, CONF_NAME, CONF_HELP, NULL, NULL);
  strncpy(this->devname, confstr, 128);
  this->devname[127] = '\0';
  dashpos = strlen(this->devname) - 2; /* the dash in the new device naming scheme would be here */
  if (this->devname[dashpos] == '-') {
    /* use new device naming scheme with trailing number */
    strncpy(this->devnum, &this->devname[dashpos], 3);
    this->devname[dashpos] = '\0';
  } else {
    /* use old device naming scheme without trailing number */
    /* FIXME: remove this when everyone uses em8300 >=0.12.0 */
    this->devnum[0] = '\0';
  }

  snprintf(tmpstr, sizeof(tmpstr), "%s%s", this->devname, this->devnum);
#if LOG_VID
  printf("video_out_dxr3: Entering video init, devname = %s.\n", tmpstr);
#endif
  if ((this->fd_control = open(tmpstr, O_WRONLY)) < 0) {
    printf("video_out_dxr3: Failed to open control device %s (%s)\n",
      tmpstr, strerror(errno));
    return 0;
  }
  
  snprintf (tmpstr, sizeof(tmpstr), "%s_mv%s", this->devname, this->devnum);
  if ((this->fd_video = open (tmpstr, O_WRONLY | O_SYNC )) < 0) {
    printf("video_out_dxr3: Failed to open video device %s (%s)\n",
      tmpstr, strerror(errno));
    return 0;
  }
  /* close now and and let the decoder/encoder reopen if they want */
  close(this->fd_video);
  this->fd_video = CLOSED_FOR_DECODER;

  /* which encoder to use? Whadda we got? */
  encoder = 0;
#if LOG_VID
  printf("video_out_dxr3: Supported mpeg encoders: ");
#endif
#ifdef HAVE_LIBFAME
  available_encoders[encoder++] = "fame";
#if LOG_VID
  printf("fame, ");
#endif
#endif
#ifdef HAVE_LIBRTE
  available_encoders[encoder++] = "rte";
#if LOG_VID
  printf("rte, ");
#endif
#endif
  available_encoders[encoder] = _("none");
  available_encoders[encoder + 1] = NULL;
#if LOG_VID
  printf("none\n");
#endif
  if (encoder) {
    encoder = config->register_enum(config, "dxr3.encoder", 
      0, available_encoders, _("the encoder for non mpeg content"),
      _("Content other than mpeg has to pass an additional reencoding stage, "
      "because the dxr3 handles mpeg only."), NULL, NULL);
#ifdef HAVE_LIBRTE
    if ((strcmp(available_encoders[encoder], "rte") == 0) && !dxr3_rte_init(this)) {
      printf("video_out_dxr3: Mpeg encoder rte failed to init.\n");
      return 0;
    }
#endif
#ifdef HAVE_LIBFAME
    if ((strcmp(available_encoders[encoder], "fame") == 0) && !dxr3_fame_init(this)) {
      printf("video_out_dxr3: Mpeg encoder fame failed to init.\n");
      return 0;
    }
#endif
    if (strcmp(available_encoders[encoder], _("none")) == 0)
      printf("video_out_dxr3: Mpeg encoding disabled.\n"
             "video_out_dxr3: that's ok, you don't need it for mpeg video like DVDs, but\n"
             "video_out_dxr3: you will not be able to play non-mpeg content using this video out\n"
             "video_out_dxr3: driver. See the README.dxr3 for details on configuring an encoder.\n");
  } else
    printf("video_out_dxr3: No mpeg encoder compiled in.\n"
           "video_out_dxr3: that's ok, you don't need it for mpeg video like DVDs, but\n"
           "video_out_dxr3: you will not be able to play non-mpeg content using this video out\n"
           "video_out_dxr3: driver. See the README.dxr3 for details on configuring an encoder.\n");
  
  /* init bcs */
  if (ioctl(this->fd_control, EM8300_IOCTL_GETBCS, &this->bcs))
    printf("video_out_dxr3: cannot read bcs values (%s)\n",
      strerror(errno));
  this->bcs.contrast   = config->register_range(config, "dxr3.contrast",
    this->bcs.contrast, 100, 900, _("Dxr3: contrast control"), NULL, NULL, NULL);
  this->bcs.saturation = config->register_range(config, "dxr3.saturation",
    this->bcs.saturation, 100, 900, _("Dxr3: saturation control"), NULL, NULL, NULL);
  this->bcs.brightness = config->register_range(config, "dxr3.brightness",
    this->bcs.brightness, 100, 900, _("Dxr3: brightness control"), NULL, NULL, NULL);
  
  /* overlay or tvout? */
  confnum = config->register_enum(config, "dxr3.videoout_mode", 0, videoout_modes,
    _("Dxr3: videoout mode (tv or overlay)"), NULL, NULL, NULL);
#if LOG_VID
  printf("video_out_dxr3: overlaymode = %s\n", videoout_modes[confnum]);
#endif
  switch (confnum) {
  case 0: /* tv mode */
    this->overlay_enabled = 0;
    this->tv_switchable = 0;  /* don't allow on-the-fly switching */
    break;
  case 1: /* overlay mode */
#if LOG_VID
    printf("video_out_dxr3: setting up overlay mode\n");
#endif
    gather_screen_vars(this, visual_gen);
    if (dxr3_overlay_read_state(&this->overlay) == 0) {
      this->overlay_enabled = 1;
      this->tv_switchable = 1;
      confstr = config->register_string(config, "dxr3.keycolor", "0x80a040",
        _("Dxr3: overlay colorkey value"), NULL, NULL, NULL);
      sscanf(confstr, "%x", &this->overlay.colorkey);
      confstr = config->register_string(config, "dxr3.color_interval", "50.0",
        _("Dxr3: overlay colorkey range"),
        _("A greater value widens the tolerance for the overlay keycolor"), NULL, NULL);
      sscanf(confstr, "%f", &this->overlay.color_interval);
    } else {
      printf("video_out_dxr3: please run autocal, overlay disabled\n");
      this->overlay_enabled = 0;
      this->tv_switchable = 0;
    }
  }
  
  /* init tvmode */
  confnum = config->register_enum(config, "dxr3.preferred_tvmode", 3, tv_modes,
    _("dxr3 preferred tv mode"), NULL, NULL, NULL);
  switch (confnum) {
  case 0: /* ntsc */
    this->tv_mode = EM8300_VIDEOMODE_NTSC;
#if LOG_VID
    printf("video_out_dxr3: setting tv_mode to NTSC\n");
#endif
    break;
  case 1: /* pal */
    this->tv_mode = EM8300_VIDEOMODE_PAL;
#if LOG_VID
    printf("video_out_dxr3: setting tv_mode to PAL 50Hz\n");
#endif
    break;
  case 2: /* pal60 */
    this->tv_mode = EM8300_VIDEOMODE_PAL60;
#if LOG_VID
    printf("video_out_dxr3: setting tv_mode to PAL 60Hz\n");
#endif
    break;
  default:
    this->tv_mode = EM8300_VIDEOMODE_DEFAULT;
  }
  if (this->tv_mode != EM8300_VIDEOMODE_DEFAULT)
    if (ioctl(this->fd_control, EM8300_IOCTL_SET_VIDEOMODE, &this->tv_mode))
      printf("video_out_dxr3: setting video mode failed.\n");
  
  /* initialize overlay */
  if (this->overlay_enabled) {
    em8300_overlay_screen_t scr;
    int value;
    
    this->overlay.fd_control = this->fd_control;
    
    /* allocate keycolor */
    this->color.red   = ((this->overlay.colorkey >> 16) & 0xff) * 256;
    this->color.green = ((this->overlay.colorkey >>  8) & 0xff) * 256;
    this->color.blue  = ((this->overlay.colorkey      ) & 0xff) * 256;
    XAllocColor(this->display, DefaultColormap(this->display, 0), &this->color);
    
    /* set the screen */
    scr.xsize = this->overlay.screen_xres;
    scr.ysize = this->overlay.screen_yres;
    if (ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETSCREEN, &scr))
      printf("video_out_dxr3: setting the overlay screen failed.\n");
    
    if (dxr3_overlay_set_keycolor(&this->overlay) != 0)
      printf("video_out_dxr3: setting the overlay keycolor failed.\n");
    if (dxr3_overlay_set_attributes(&this->overlay) != 0)
      printf("video_out_dxr3: setting an overlay attribute failed.\n");
      
    /* finally switch to overlay mode */
    value = EM8300_OVERLAY_MODE_OVERLAY;
    if (ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETMODE, &value) != 0)
      printf("video_out_dxr3: switching to overlay mode failed.\n");
  }
  
  return &this->vo_driver;
}

static uint32_t dxr3_get_capabilities(vo_driver_t *this_gen)
{
  return VO_CAP_YV12 | VO_CAP_YUY2 |
    VO_CAP_SATURATION | VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST;
}

static vo_frame_t *dxr3_alloc_frame(vo_driver_t *this_gen)
{
  dxr3_frame_t *frame;
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  
  frame = (dxr3_frame_t *)malloc(sizeof(dxr3_frame_t));
  memset(frame, 0, sizeof(dxr3_frame_t));
  
  pthread_mutex_init(&frame->vo_frame.mutex, NULL);

  if (this->enc && this->enc->on_frame_copy)
    frame->vo_frame.copy = dxr3_frame_copy;
  else
    frame->vo_frame.copy = 0;
  frame->vo_frame.field   = dxr3_frame_field; 
  frame->vo_frame.dispose = dxr3_frame_dispose;
  frame->vo_frame.driver  = this_gen;

  return &frame->vo_frame;
}

static void dxr3_frame_copy(vo_frame_t *frame_gen, uint8_t **src)
{
  dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen;
  dxr3_driver_t *this = (dxr3_driver_t *)frame_gen->driver;
  
  if (frame_gen->format != IMGFMT_MPEG && this->enc && this->enc->on_frame_copy)
    this->enc->on_frame_copy(this, frame, src);
}

static void dxr3_frame_field(vo_frame_t *vo_img, int which_field)
{
  /* dummy function */
}

static void dxr3_frame_dispose(vo_frame_t *frame_gen)
{
  dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen;
  
  if (frame->mem) free(frame->mem);
  pthread_mutex_destroy(&frame_gen->mutex);
  free(frame);
}

static void dxr3_update_frame_format(vo_driver_t *this_gen, vo_frame_t *frame_gen,
  uint32_t width, uint32_t height, int ratio_code, int format, int flags)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen; 
  dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen; 
  int i;
  int aspect, oheight;

  /* update the overlay window co-ords if required */
  dxr3_overlay_update(this);

  if (format == IMGFMT_MPEG) { /* talking to dxr3 decoder */
    /* a bit of a hack. we must release the em8300_mv fd for
     * the dxr3 decoder plugin */
    if (this->fd_video >= 0) {
      close(this->fd_video);
      this->fd_video = CLOSED_FOR_DECODER;
    }
    
    /* for mpeg source, we don't have to do much. */
    this->video_width   = width;
    this->video_iheight = height;
    this->video_oheight = height;
    this->top_bar       = 0;
    this->video_aspect  = ratio_code;
    
    frame->width   = width;
    frame->iheight = height;
    frame->oheight = height;
    
    if (frame->mem) {
      free(frame->mem);
      frame->mem = NULL;
      frame->real_base[0] = frame->real_base[1] = frame->real_base[2] = NULL;
      frame_gen->base[0] = frame_gen->base[1] = frame_gen->base[2] = NULL;
    }
    
    aspect = this->aspect;
    if (ratio_code == 1 || ratio_code == 2) aspect = ASPECT_FULL;
    if (ratio_code == 3 || ratio_code == 4) aspect = ASPECT_ANAMORPHIC;
    
    if(this->aspect != aspect)
      dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, aspect);
      
    return;
  }

  /* the following is for the mpeg encoding part only */
  
  aspect = this->aspect;
  oheight = this->video_oheight;

  if (this->fd_video == CLOSED_FOR_DECODER) { /* decoder should have released it */
    this->fd_video = CLOSED_FOR_ENCODER; /* allow encoder to reopen it */
    this->need_redraw = 1;
  }

  if (this->add_bars == 0) {
    /* don't add black bars; assume source is in 4:3 */
    ratio_code = XINE_ASPECT_RATIO_4_3;
  }

  if ((this->video_width != width) || (this->video_iheight != height) ||
      (this->video_aspect != ratio_code)) {
    /* check aspect ratio, see if we need to add black borders */
    switch (ratio_code) {
    case XINE_ASPECT_RATIO_4_3:
      aspect = ASPECT_FULL;
      oheight = height;
      break;
    case XINE_ASPECT_RATIO_ANAMORPHIC:
      aspect = ASPECT_ANAMORPHIC;
      oheight = height;
      break;
    default: /* assume square pixel */
      aspect = ASPECT_ANAMORPHIC;
      oheight = (int)(width * 9./16.);
      if (oheight < height) { /* frame too high, try 4:3 */
        aspect = ASPECT_FULL;
        oheight = (int)(width * 3./4.);
      }
    }
    
    /* find closest multiple of 16 */
    oheight = 16*(int)(oheight / 16. + 0.5);
    if (oheight < height) oheight = height;

    /* Tell the viewers about the aspect ratio stuff. */
    if (oheight - height > 0)
      printf("video_out_dxr3: adding %d black lines to get %s aspect ratio.\n",
        oheight - height, aspect == ASPECT_FULL ? "4:3" : "16:9");

    this->video_width   = width;
    this->video_iheight = height;
    this->video_oheight = oheight;
    this->video_aspect  = ratio_code;
    this->format        = format;
    this->need_redraw   = 1;
    this->need_update   = 1;

    if (!this->enc) {
      /* no encoder plugin! Let's bug the user! */
      printf("video_out_dxr3: Need an mpeg encoder to play non-mpeg videos on dxr3\n"
             "video_out_dxr3: Read the README.dxr3 for details.\n");
    }
  }

  /* if dimensions changed, we need to re-allocate frame memory */
  if ((frame->width != width) || (frame->iheight != height) || 
      (frame->oheight != oheight)) {
    if (frame->mem) {
      free (frame->mem);
      frame->mem = NULL;
    }
    
    /* make top black bar multiple of 16, 
     * so old and new macroblocks overlap */ 
    this->top_bar = ((oheight - height) / 32) * 16;
    if (format == IMGFMT_YUY2) {
      int image_size = width * oheight; /* includes black bars */
      
      /* planar format, only base[0] */
      /* add one extra line for field swap stuff */
      frame->real_base[0] = xine_xmalloc_aligned(16, (image_size + width) * 2,
        (void**)&frame->mem);

      /* don't use first line */
      frame->real_base[0] += width * 2;
      frame->real_base[1] = frame->real_base[2] = 0;

      /* fix offset, so the decoder does not see the top black bar */
      frame->vo_frame.base[0] = frame->real_base[0] + width * 2 * this->top_bar;
      frame->vo_frame.base[1] = frame->vo_frame.base[2] = 0;

      /* fill with black (yuy2 16,128,16,128,...) */
      memset(frame->real_base[0], 128, 2 * image_size); /* U and V */
      for (i = 0; i < 2 * image_size; i += 2) /* Y */
        *(frame->real_base[0] + i) = 16;

    } else { /* IMGFMT_YV12 */
      int image_size = width * oheight; /* includes black bars */
      
      /* add one extra line for field swap stuff */
      frame->real_base[0] = xine_xmalloc_aligned(16, (image_size + width) * 3/2,
        (void**)&frame->mem);

      /* don't use first line */
      frame->real_base[0] += width;
      frame->real_base[1] = frame->real_base[0] + image_size;
      frame->real_base[2] = frame->real_base[1] + image_size/4;

      /* fix offsets, so the decoder does not see the top black bar */
      frame->vo_frame.base[0] = frame->real_base[0] + width * this->top_bar;
      frame->vo_frame.base[1] = frame->real_base[1] + width * this->top_bar/4;
      frame->vo_frame.base[2] = frame->real_base[2] + width * this->top_bar/4;
      
      /* fill with black (yuv 16,128,128) */
      memset(frame->real_base[0], 16, image_size);
      memset(frame->real_base[1], 128, image_size/4);
      memset(frame->real_base[2], 128, image_size/4);
    }
  }

  if (this->swap_fields != frame->swap_fields) {
    if (format == IMGFMT_YUY2) {
      if (this->swap_fields) 
        frame->vo_frame.base[0] -= width *2;
      else  
        frame->vo_frame.base[0] += width *2;
    } else {
      if (this->swap_fields) 
        frame->vo_frame.base[0] -= width;
      else  
        frame->vo_frame.base[0] += width;
    }
  }
 
  frame->width       = width;
  frame->iheight     = height;
  frame->oheight     = oheight;
  frame->swap_fields = this->swap_fields;

  if(this->aspect != aspect)
    dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, aspect);
}

static void dxr3_overlay_blend(vo_driver_t *this_gen, vo_frame_t *frame_gen,
  vo_overlay_t *overlay)
{
  /* FIXME: We only blend non-mpeg frames here.
     Is there any way to provide overlays for mpeg content? Subpictures? */
  if (frame_gen->format != IMGFMT_MPEG) {
    dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen;
    
    if (overlay->rle) {
      if (frame_gen->format == IMGFMT_YV12)
        blend_yuv(frame->vo_frame.base, overlay, frame->width, frame->iheight);
      else
        blend_yuy2(frame->vo_frame.base[0], overlay, frame->width, frame->iheight);
    }
  }
}

static void dxr3_display_frame(vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  dxr3_frame_t *frame = (dxr3_frame_t *)frame_gen;

  if (frame_gen->format != IMGFMT_MPEG && this->enc && this->enc->on_display_frame) {
    if (this->need_update) {
      /* we cannot do this earlier, because vo_frame.duration is only valid here */
      if (this->enc && this->enc->on_update_format)
        this->enc->on_update_format(this, frame);
      this->need_update = 0;
    }
    /* for non-mpeg, the encoder plugin is responsible for calling 
     * frame_gen->displayed(frame_gen) ! */
    this->enc->on_display_frame(this, frame);
  } else {
    frame_gen->displayed(frame_gen);
  }
}

static int dxr3_redraw_needed(vo_driver_t *this_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  
  dxr3_overlay_update(this);
  return 0;
}

static int dxr3_get_property(vo_driver_t *this_gen, int property)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;

  switch (property) {
  case VO_PROP_SATURATION:
    return this->bcs.saturation;
  case VO_PROP_CONTRAST:
    return this->bcs.contrast;
  case VO_PROP_BRIGHTNESS:
    return this->bcs.brightness;
  case VO_PROP_ASPECT_RATIO:
    return this->aspect;
  case VO_PROP_COLORKEY:
    return this->overlay.colorkey;
  case VO_PROP_ZOOM_FACTOR:
  case VO_PROP_TVMODE:
    return 0;
  case VO_PROP_VO_TYPE:
    return VO_TYPE_DXR3;
  }
  printf("video_out_dxr3: property %d not implemented.\n", property);
  return 0;
}

static int dxr3_set_property(vo_driver_t *this_gen, int property, int value)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  int val, bcs_changed = 0;
  int fullscreen;

  switch (property) {
  case VO_PROP_SATURATION:
    this->bcs.saturation = value;
    bcs_changed = 1;
    break;
  case VO_PROP_CONTRAST:
    this->bcs.contrast = value;
    bcs_changed = 1;
    break;
  case VO_PROP_BRIGHTNESS:
    this->bcs.brightness = value;
    bcs_changed = 1;
    break;
  case VO_PROP_ASPECT_RATIO:
    /* xitk-ui increments the value, so we make
     * just a two value "loop" */
    if (value > ASPECT_FULL) value = ASPECT_ANAMORPHIC;
    this->aspect = value;
    fullscreen = this->overlay_enabled ? is_fullscreen(this) : 0;
    
    if (value == ASPECT_ANAMORPHIC) {
#if LOG_VID
      printf("video_out_dxr3: setting aspect ratio to anamorphic\n");
#endif
      if (!this->overlay_enabled || fullscreen)
        val = EM8300_ASPECTRATIO_16_9;
      else /* The overlay window can adapt to the ratio */
        val = EM8300_ASPECTRATIO_4_3;
    } else {
#if LOG_VID
      printf("video_out_dxr3: setting aspect ratio to full\n");
#endif
      val = EM8300_ASPECTRATIO_4_3;
    }

    if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &val))
      printf("video_out_dxr3: failed to set aspect ratio (%s)\n", strerror(errno));
    
    dxr3_overlay_update(this);
    break;
  case VO_PROP_COLORKEY:
    printf("video_out_dxr3: VO_PROP_COLORKEY not implemented!");
    this->overlay.colorkey = value;
    break;
  case VO_PROP_ZOOM_FACTOR:
    if(!this->overlay_enabled) {  /* TV-out only */
      if (value == 1) {
#if LOG_VID
        printf("video_out_dxr3: enabling 16:9 zoom\n");
#endif
        val = EM8300_ASPECTRATIO_4_3;
        if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &val))
          printf("video_out_dxr3: failed to set aspect ratio (%s)\n", strerror(errno));
        dxr3_zoomTV(this);
      } else if (value == -1) {
#if LOG_VID
        printf("video_out_dxr3: disabling 16:9 zoom\n");
#endif
        if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &this->aspect))
          printf("video_out_dxr3: failed to set aspect ratio (%s)\n", strerror(errno));
      }
    }
    break;
  case VO_PROP_TVMODE:
    if (++this->tv_mode > EM8300_VIDEOMODE_LAST) this->tv_mode = EM8300_VIDEOMODE_PAL;
#if LOG_VID
    printf("video_out_dxr3: Changing TVMode to ");
#endif
    if (this->tv_mode == EM8300_VIDEOMODE_PAL)   printf("PAL\n");
    if (this->tv_mode == EM8300_VIDEOMODE_PAL60) printf("PAL60\n");
    if (this->tv_mode == EM8300_VIDEOMODE_NTSC)  printf("NTSC\n");
    if (ioctl(this->fd_control, EM8300_IOCTL_SET_VIDEOMODE, &this->tv_mode))
      printf("video_out_dxr3: setting video mode failed (%s)\n", strerror(errno));
    break;
  }

  if (bcs_changed) {
    if (ioctl(this->fd_control, EM8300_IOCTL_SETBCS, &this->bcs))
      printf("video_out_dxr3: bcs set failed (%s)\n", strerror(errno));
    this->config->update_num(this->config, "dxr3.contrast",   this->bcs.contrast);
    this->config->update_num(this->config, "dxr3.saturation", this->bcs.saturation);
    this->config->update_num(this->config, "dxr3.brightness", this->bcs.brightness);
  }
    
  return value;
}

static void dxr3_get_property_min_max(vo_driver_t *this_gen, int property,
  int *min, int *max)
{
  switch (property) {
  case VO_PROP_SATURATION:
  case VO_PROP_CONTRAST:
  case VO_PROP_BRIGHTNESS:
    *min = 0;
    *max = 1000;
    break;
  default:
    *min = 0;
    *max = 0;
  }
}

static int dxr3_gui_data_exchange(vo_driver_t *this_gen, int data_type, void *data)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;

  if (!this->overlay_enabled && !this->tv_switchable) return 0;

  switch (data_type) {
  case GUI_DATA_EX_EXPOSE_EVENT:
    this->need_redraw = 1;
    dxr3_overlay_update(this);
    break;
  case GUI_DATA_EX_DRAWABLE_CHANGED:
    this->win = (Drawable)data;
    XFreeGC(this->display, this->gc);
    this->gc = XCreateGC(this->display, this->win, 0, NULL);
    dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, this->aspect);
    break;
  case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO:
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;
      dxr3_translate_gui2video(this, rect->x, rect->y, &x1, &y1);
      dxr3_translate_gui2video(this, rect->w, rect->h, &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2 - x1;
      rect->h = y2 - y1;
    }
    break;
  case GUI_DATA_EX_VIDEOWIN_VISIBLE:
    {
      int window_showing = (int)data;
      int val;
      if (!window_showing) {
#if LOG_VID
        printf("video_out_dxr3: Hiding video window and diverting video to TV\n");
#endif
        val = EM8300_OVERLAY_MODE_OFF;
        this->overlay_enabled = 0;
      } else {
#if LOG_VID
        printf("video_out_dxr3: Using video window for overlaying video\n");
#endif
        val = EM8300_OVERLAY_MODE_OVERLAY;
        this->overlay_enabled = 1;
        this->need_redraw = 1;
      }
      ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETMODE, &val);
      dxr3_set_property(this_gen, VO_PROP_ASPECT_RATIO, this->aspect);
    }
    break;
  default:
    return -1;
  }
  return 0;
}

static void dxr3_exit(vo_driver_t *this_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  int val = EM8300_OVERLAY_MODE_OFF;

#if LOG_VID
  printf("video_out_dxr3: vo exit called\n");
#endif
  if (this->enc && this->enc->on_close)
    this->enc->on_close(this);
  if(this->overlay_enabled)
    ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETMODE, &val);
    
  free(this);
}


static void gather_screen_vars(dxr3_driver_t *this, x11_visual_t *vis)
{
  int scrn;
#ifdef HAVE_XINERAMA
  int screens;
  int dummy_a, dummy_b;
  XineramaScreenInfo *screeninfo = NULL;
#endif

  this->win       = vis->d;
  this->display   = vis->display;
  this->user_data = vis->user_data;
  this->gc        = XCreateGC(this->display, this->win, 0, NULL);
  scrn            = DefaultScreen(this->display);

#ifdef HAVE_XINERAMA
  if (XineramaQueryExtension(this->display, &dummy_a, &dummy_b) &&
      (screeninfo = XineramaQueryScreens(this->display, &screens)) &&
      XineramaIsActive(this->display)) {
    this->overlay.screen_xres = screeninfo[0].width;
    this->overlay.screen_yres = screeninfo[0].height;
  } else
#endif
  {
    this->overlay.screen_xres = DisplayWidth(this->display, scrn);
    this->overlay.screen_yres = DisplayHeight(this->display, scrn);
  }

  this->overlay.screen_depth = DisplayPlanes(this->display, scrn);
  this->frame_output_cb = (void *)vis->frame_output_cb;
  
#if LOG_OVR
  printf("video_out_dxr3: xres: %d, yres: %d, depth: %d\n",
    this->overlay.screen_xres, this->overlay.screen_yres, this->overlay.screen_depth);
#endif
}

/* dxr3_overlay_read_state helper structure */
#define TYPE_INT 1
#define TYPE_XINT 2
#define TYPE_COEFF 3
#define TYPE_FLOAT 4

struct lut_entry {
    char *name;   
    int type;     
    void *ptr;    
};

/* dxr3_overlay_read_state helper function */
static int lookup_parameter(struct lut_entry *lut, char *name,
  void **ptr, int *type)
{
  int i;
  
  for (i = 0; lut[i].name; i++)
    if (strcmp(name, lut[i].name) == 0) {
      *ptr  = lut[i].ptr;
      *type = lut[i].type;
#if LOG_OVR
      printf("video_out_dxr3: found parameter \"%s\"\n", name);
#endif
      return 1;
    }
#if LOG_OVR
  printf("video_out_dxr3: WARNING: unknown parameter \"%s\"\n", name);
#endif
  return 0;
}

static int dxr3_overlay_read_state(dxr3_overlay_t *this)
{
  char *loc;
  char fname[256], tmp[128], line[256];
  FILE *fp;
  struct lut_entry lut[] = {
    {"xoffset",        TYPE_INT,   &this->xoffset},
    {"yoffset",        TYPE_INT,   &this->yoffset},
    {"xcorr",          TYPE_INT,   &this->xcorr},
    {"jitter",         TYPE_INT,   &this->jitter},
    {"stability",      TYPE_INT,   &this->stability},
    {"keycolor",       TYPE_XINT,  &this->colorkey},
    {"colcal_upper",   TYPE_COEFF, &this->colcal_upper[0]},
    {"colcal_lower",   TYPE_COEFF, &this->colcal_lower[0]},
    {"color_interval", TYPE_FLOAT, &this->color_interval},
    {0,0,0}
  };
  char *tok;
  void *ptr;
  int type;
  int j;

  /* store previous locale */
  loc = setlocale(LC_NUMERIC, NULL);
  /* set C locale for floating point values
   * (used by .overlay/res file) */
  setlocale(LC_NUMERIC, "C");

  snprintf(tmp, sizeof(tmp), "/res_%dx%dx%d",
    this->screen_xres, this->screen_yres, this->screen_depth);
  strncpy(fname, getenv("HOME"), sizeof(fname) - strlen(tmp) - sizeof("/.overlay"));
  fname[sizeof(fname) - strlen(tmp) - sizeof("/.overlay")] = '\0';
  strcat(fname, "/.overlay");
  strcat(fname, tmp);
#if LOG_OVR
  printf("video_out_dxr3: attempting to open %s\n", fname);
#endif
  if (!(fp = fopen(fname, "r"))) {
    printf("video_out_dxr3: ERROR Reading overlay init file. Run autocal!\n");
    return -1;
  }

  while (!feof(fp)) {
    if (!fgets(line, 256, fp))
      break;
    tok = strtok(line, " ");
    if (lookup_parameter(lut, tok, &ptr, &type)) {
      tok = strtok(NULL, " \n");
      switch(type) {
      case TYPE_INT:
        sscanf(tok, "%d", (int *)ptr);
#if LOG_OVR
        printf("video_out_dxr3: value \"%s\" = %d\n", tok, *(int *)ptr);
#endif
        break;
      case TYPE_XINT:
        sscanf(tok, "%x", (int *)ptr);
#if LOG_OVR
        printf("video_out_dxr3: value \"%s\" = %d\n", tok, *(int *)ptr);
#endif
        break;
      case TYPE_FLOAT:
        sscanf(tok, "%f", (float *)ptr);
#if LOG_OVR
        printf("video_out_dxr3: value \"%s\" = %f\n", tok, *(float *)ptr);
#endif
        break;
      case TYPE_COEFF:
        for(j = 0; j < 3; j++) {
          sscanf(tok, "%f", &((struct coeff *)ptr)[j].k);
#if LOG_OVR
          printf("video_out_dxr3: value (%d,k) \"%s\" = %f\n", j, tok, ((struct coeff *)ptr)[j].k);
#endif
          tok = strtok(NULL, " \n");
          sscanf(tok, "%f", &((struct coeff *)ptr)[j].m);
#if LOG_OVR
          printf("video_out_dxr3: value (%d,m) \"%s\" = %f\n", j, tok, ((struct coeff *)ptr)[j].m);
#endif
          tok = strtok(NULL, " \n");
        }
        break;
      }
    }
  }
  
  fclose(fp);
  /* restore original locale */
  setlocale(LC_NUMERIC, loc);
  
  return 0;
}

/* dxr3_overlay_set_keycolor helper function */
static int col_interp(float x, struct coeff c)
{
  int y;
  y = rint(x * c.k + c.m);
  if (y > 255) y = 255;
  if (y <   0) y =   0;
  return y;
}

static int dxr3_overlay_set_keycolor(dxr3_overlay_t *this)
{
  em8300_attribute_t attr;
  float r = (this->colorkey & 0xff0000) >> 16;
  float g = (this->colorkey & 0x00ff00) >>  8;
  float b = (this->colorkey & 0x0000ff);
  float interval = this->color_interval;
  int32_t overlay_limit;
  int ret;

#if LOG_OVR
  printf("video_out_dxr3: set_keycolor: r = %f, g = %f, b = %f, interval = %f\n",
    r, g, b, interval);
#endif

  overlay_limit =  /* lower limit */
    col_interp(r - interval, this->colcal_lower[0]) << 16 |
    col_interp(g - interval, this->colcal_lower[1]) <<  8 |
    col_interp(b - interval, this->colcal_lower[2]);
#if LOG_OVR
  printf("video_out_dxr3: lower overlay_limit = %d\n", overlay_limit);
#endif
  attr.attribute = EM9010_ATTRIBUTE_KEYCOLOR_LOWER;
  attr.value     = overlay_limit;
  if ((ret = ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr)) < 0) {
    printf("video_out_dxr3: WARNING: error setting overlay lower limit attribute\n");
    return ret;
  }

  overlay_limit =  /* upper limit */
    col_interp(r + interval, this->colcal_upper[0]) << 16 |
    col_interp(g + interval, this->colcal_upper[1]) <<  8 |
    col_interp(b + interval, this->colcal_upper[2]);
#if LOG_OVR
  printf("video_out_dxr3: upper overlay_limit = %d\n", overlay_limit);
#endif
  attr.attribute = EM9010_ATTRIBUTE_KEYCOLOR_UPPER;
  attr.value     = overlay_limit;
  if ((ret = ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr)) < 0)
    printf("video_out_dxr3: WARNING: error setting overlay upper limit attribute\n");
  return ret;
}

static int dxr3_overlay_set_attributes(dxr3_overlay_t *this)
{
  em8300_attribute_t attr;
  
  attr.attribute = EM9010_ATTRIBUTE_XOFFSET;
  attr.value     = this->xoffset;
  if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
    return -1;
  attr.attribute = EM9010_ATTRIBUTE_YOFFSET;
  attr.value     = this->yoffset;
  if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
    return -1;
  attr.attribute = EM9010_ATTRIBUTE_XCORR;
  attr.value     = this->xcorr;
  if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
    return -1;
  attr.attribute = EM9010_ATTRIBUTE_STABILITY;
  attr.value     = this->stability;
  if(ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr) == -1)
    return -1;
  attr.attribute = EM9010_ATTRIBUTE_JITTER;
  attr.value     = this->jitter;
  return ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SET_ATTRIBUTE, &attr);
}


static void dxr3_overlay_update(dxr3_driver_t *this)
{
  if (this->overlay_enabled) {
    int width;
    int gui_win_x, gui_win_y, win_off_x, win_off_y, gui_width, gui_height;
    
    switch (this->aspect) {
    case ASPECT_FULL:
      width = this->video_oheight * 4 / 3;
      break;
    case ASPECT_ANAMORPHIC:
      width = this->video_oheight * 16 / 9;
      break;
    default:
      width = this->video_width;
    }
    
    this->frame_output_cb(this->user_data,
      width, this->video_oheight,
      &win_off_x, &win_off_y, &gui_width, &gui_height, &gui_win_x, &gui_win_y);
      
    if (this->xpos != (gui_win_x + win_off_x) ||
        this->ypos != (gui_win_y + win_off_y) ||
        this->width != gui_width ||
        this->height != gui_height ||
        this->need_redraw) {
      em8300_overlay_window_t win;
      
      this->xpos   = gui_win_x + win_off_x;
      this->ypos   = gui_win_y + win_off_y;
      this->width  = gui_width;
      this->height = gui_height;
      
      /* fill video window with keycolor */
      XLockDisplay(this->display);
      XSetForeground(this->display, this->gc, this->color.pixel);
      XFillRectangle(this->display, this->win, this->gc,
        win_off_x, win_off_y, this->width, this->height);
      XUnlockDisplay(this->display);
      this->need_redraw = 0;
      
      /* is some part of the picture visible? */
      if (this->xpos + this->width  < 0) return;
      if (this->ypos + this->height < 0) return;
      if (this->xpos > this->overlay.screen_xres) return;
      if (this->ypos > this->overlay.screen_yres) return;
  
      win.xpos   = this->xpos;
      win.ypos   = this->ypos;
      win.width  = this->width;
      win.height = this->height;
      
      ioctl(this->fd_control, EM8300_IOCTL_OVERLAY_SETWINDOW, &win);
    }
  }
}

static void dxr3_zoomTV(dxr3_driver_t *this)
{
  em8300_register_t frame, visible, update;
  
  /* change left bound */
  frame.microcode_register   = 1;
  frame.reg                  = 93;   // dicom frame left
  frame.val                  = 0x10;
  
  visible.microcode_register = 1;
  visible.reg                = 97;   // dicom visible left
  visible.val                = 0x10;
  
  update.microcode_register  = 1;
  update.reg                 = 65;   // dicom_update
  update.val                 = 1;
      
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &frame);
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &visible);
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &update);

  /* change right bound */
  frame.microcode_register   = 1;
  frame.reg                  = 94;   // dicom frame right
  frame.val                  = 0x10;
  
  visible.microcode_register = 1;
  visible.reg                = 98;   // dicom visible right
  visible.val                = 968;
  
  update.microcode_register  = 1;
  update.reg                 = 65;   // dicom_update
  update.val                 = 1;
      
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &frame);
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &visible);
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &update);
}

static void dxr3_translate_gui2video(dxr3_driver_t *this, int x, int y,
  int *vid_x, int *vid_y)
{
  *vid_x = x * this->video_width   / this->width;
  *vid_y = y * this->video_oheight / this->height - this->top_bar;
}

static int is_fullscreen(dxr3_driver_t *this)
{
  XWindowAttributes a;

  XGetWindowAttributes(this->display, this->win, &a);
  
  return 
    a.x == 0 &&
    a.y == 0 &&
    a.width  == this->overlay.screen_xres &&
    a.height == this->overlay.screen_yres;
}


static void dxr3_update_add_bars(void *data, cfg_entry_t *entry)
{
  dxr3_driver_t *this = (dxr3_driver_t *)data;
  this->add_bars = entry->num_value;
  printf("video_out_dxr3: setting add_bars to correct aspect ratio to %s\n", 
    (this->add_bars ? "on" : "off"));
}

static void dxr3_update_swap_fields(void *data, cfg_entry_t *entry)
{
  dxr3_driver_t *this = (dxr3_driver_t *)data;
  this->swap_fields = entry->num_value;
  printf("video_out_dxr3: setting swap fields to %s\n",
    (this->swap_fields ? "on" : "off"));
}

static void dxr3_update_enhanced_mode(void *data, cfg_entry_t *entry)
{
  dxr3_driver_t *this = (dxr3_driver_t *)data;
  this->enhanced_mode = entry->num_value;
  printf("video_out_dxr3: setting enhanced encoding playback to %s\n", 
    (this->enhanced_mode ? "on" : "off"));
}
