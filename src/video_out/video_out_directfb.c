/*
 * Copyright (C) 2000-2005 the xine project and Claudio Ciccani
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
 *
 * DirectFB based output plugin by Claudio Ciccani <klan@directfb.org>
 * 
 * Based on video_out_xv.c and video_out_vidix.c.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_X11
# include <X11/Xlib.h>
#endif

#ifdef HAVE_FB
# include <fcntl.h>
# include <unistd.h>
# include <sys/ioctl.h>
# include <linux/fb.h>
#endif

#define LOG_MODULE "video_out_directfb"
#define LOG_VERBOSE

#include "xine.h"
#include "xine_internal.h"
#include "video_out.h"
#include "xineutils.h"
#include "vo_scale.h"

#include <directfb.h>
#include <directfb_version.h>


typedef struct directfb_frame_s {
  vo_frame_t                   vo_frame;

  int                          width;
  int                          height;
  DFBSurfacePixelFormat        format;
  double                       ratio;

  IDirectFBSurface            *surface;
  int                          locked;
} directfb_frame_t;

typedef struct directfb_driver_s {
  vo_driver_t                  vo_driver;
  
  int                          visual_type;

  xine_t                      *xine;
  
  directfb_frame_t            *recent_frames[VO_NUM_RECENT_FRAMES];

  /* DirectFB related stuff */
  IDirectFB                   *dfb;
  IDirectFBDisplayLayer       *layer;
  IDirectFBSurface            *surface;
  DFBDisplayLayerCapabilities  caps;
  DFBDisplayLayerConfig        config;
  DFBColorAdjustment           default_cadj;
  DFBColorAdjustment           cadj;
  
  /* for hardware scaling */
  IDirectFBSurface            *temp;
  int                          temp_frame_width;
  int                          temp_frame_height;
  DFBSurfacePixelFormat        temp_frame_format;
  
  /* wheter card supports stretchblit with deinterlacing */
  int                          hw_deinterlace;
  
  /* wheter to enable deinterlacing */
  int                          deinterlace;
  
  /* configurable options */
  int                          buffermode;
  int                          vsync;
  int                          colorkeying;
  uint32_t                     colorkey;
  int                          flicker_filtering;
  int                          field_parity;
  
#ifdef HAVE_X11
  /* X11 related stuff */
  Display                     *display;
  int                          screen;
  Drawable                     drawable;
  GC                           gc;
  int                          depth;
#endif

  /* screen size */
  int                          screen_width;
  int                          screen_height;
  
  /* size / aspect ratio calculations */
  vo_scale_t                   sc;

  /* gui callbacks */
  alphablend_t                 alphablend_extra_data;
} directfb_driver_t;

typedef struct {
  video_driver_class_t         driver_class;
  xine_t                      *xine;
} directfb_class_t;


#define DEFAULT_COLORKEY  0x202040


#ifndef MAX
# define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

/*** driver functions ***/

static uint32_t directfb_get_capabilities (vo_driver_t *this_gen) {
  return VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_CROP;
}

static void directfb_frame_field (vo_frame_t *vo_img, int which_field) {
  /* not needed */
}

static void directfb_frame_dispose (vo_frame_t *vo_img) {
  directfb_frame_t *frame = (directfb_frame_t *) vo_img;

  if (frame) {
    if (frame->surface) {
      if (frame->locked) 
        frame->surface->Unlock (frame->surface);
      frame->surface->Release (frame->surface);
    }
    
    free (frame);
  }
}

static vo_frame_t *directfb_alloc_frame (vo_driver_t *this_gen) {
  directfb_driver_t *this = (directfb_driver_t *) this_gen;
  directfb_frame_t  *frame;

  frame = (directfb_frame_t *) xine_xmalloc (sizeof (directfb_frame_t));
  if (!frame) {
    xprintf (this->xine, XINE_VERBOSITY_DEBUG, 
             "video_out_directfb: directfb_alloc_frame: out of memory\n");
    return NULL;
  }

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);
  
  frame->vo_frame.proc_slice = NULL; 
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = directfb_frame_field; 
  frame->vo_frame.dispose    = directfb_frame_dispose;
  frame->vo_frame.driver     = this_gen;
  
  return &frame->vo_frame;
}

static void directfb_update_frame_format (vo_driver_t *this_gen,
                                          vo_frame_t *frame_gen,
                                          uint32_t width, uint32_t height,
                                          double ratio, int fmt, int flags) {
  directfb_driver_t     *this   = (directfb_driver_t *) this_gen;
  directfb_frame_t      *frame  = (directfb_frame_t *) frame_gen;
  DFBSurfacePixelFormat  format = (fmt == XINE_IMGFMT_YUY2) ? DSPF_YUY2 : DSPF_YV12;
  
  if (frame->surface == NULL   ||
      frame->width   != width  ||
      frame->height  != height ||
      frame->format  != format) 
  {
    DFBSurfaceDescription dsc;
    DFBResult             ret;
    
    lprintf ("frame %p: format changed to %dx%d %s.\n",
             frame, width, height, (format == DSPF_YUY2) ? "YUY2" : "YV12");
    
    if (frame->surface) {
      if (frame->locked)
        frame->surface->Unlock (frame->surface);
      frame->surface->Release (frame->surface);
      frame->surface = NULL;
      frame->locked  = 0;
    }
    
    dsc.flags       = DSDESC_CAPS   | DSDESC_WIDTH |
                      DSDESC_HEIGHT | DSDESC_PIXELFORMAT;
    dsc.caps        = DSCAPS_SYSTEMONLY;
    dsc.width       = (width  + 7) & ~7;
    dsc.height      = (height + 1) & ~1;
    dsc.pixelformat = format;
    
    ret = this->dfb->CreateSurface (this->dfb, &dsc, &frame->surface);
    if (ret != DFB_OK) {
      DirectFBError ("IDirectFB::CreateSurface()", ret);
      return;
    }
    
    frame->width  = width;
    frame->height = height;
    frame->format = format;
    
    ret = frame->surface->Lock (frame->surface, 
                                DSLF_READ | DSLF_WRITE,   
                                (void**)&frame->vo_frame.base[0],
                                (int  *)&frame->vo_frame.pitches[0]);
    if (ret != DFB_OK) {
      DirectFBError ("IDirectFBSurface::Lock()", ret);
      return;
    }
    frame->locked = 1;
    
    if (frame->format == DSPF_YV12) {
      frame->vo_frame.pitches[1] = frame->vo_frame.pitches[0]/2;
      frame->vo_frame.pitches[2] = frame->vo_frame.pitches[0]/2;
      frame->vo_frame.base[2]    = frame->vo_frame.base[0] + 
                                   dsc.height   * frame->vo_frame.pitches[0];
      frame->vo_frame.base[1]    = frame->vo_frame.base[2] +
                                   dsc.height/2 * frame->vo_frame.pitches[2];
    }
  }
  
  frame->ratio = ratio;
}

static void directfb_clean_output_area (directfb_driver_t *this) {
  if (this->visual_type == XINE_VISUAL_TYPE_X11) {
#ifdef HAVE_X11
    if (this->config.options & DLOP_DST_COLORKEY) {
      uint32_t pixel;
      int      i;
    
      switch (this->depth) {
        case 8:
          pixel = ((this->colorkey & 0xe00000) >> 16) |
                  ((this->colorkey & 0x00e000) >> 11) |
                  ((this->colorkey & 0x0000c0) >>  6);
          break;
        case 15:
          pixel = ((this->colorkey & 0xf80000) >>  9) |
                  ((this->colorkey & 0x00f800) >>  6) |
                  ((this->colorkey & 0x0000f8) >>  3);
          break;
        case 16:
          pixel = ((this->colorkey & 0xf80000) >>  8) |
                  ((this->colorkey & 0x00fc00) >>  5) |
                  ((this->colorkey & 0x0000f8) >>  3);
          break;
        default:
          pixel = this->colorkey;
          break;
      }
        
      XLockDisplay (this->display);    
      
      XSetForeground (this->display, this->gc, BlackPixel(this->display, this->screen));
    
      for (i = 0; i < 4; i++) {
        if (this->sc.border[i].w && this->sc.border[i].h) {
          XFillRectangle (this->display, this->drawable, this->gc,
                          this->sc.border[i].x, this->sc.border[i].y,
                          this->sc.border[i].w, this->sc.border[i].h);
        }
      }
    
      XSetForeground (this->display, this->gc, pixel);
      XFillRectangle (this->display, this->drawable, this->gc, 
                      this->sc.output_xoffset, this->sc.output_yoffset,
                      this->sc.output_width, this->sc.output_height);
    
      XFlush (this->display);

      XUnlockDisplay (this->display);
    }
#endif
  }
  else if (!(this->caps & DLCAPS_SCREEN_LOCATION)) {
    DFBRectangle rect[4];
    int          i; 

    for (i = 0; i < 4; i++) {
      rect[i].x = MAX(this->sc.border[i].x, 0);
      rect[i].y = MAX(this->sc.border[i].y, 0);
      rect[i].w = MAX(this->sc.border[i].w, 0);
      rect[i].h = MAX(this->sc.border[i].h, 0);
    }
      
    this->surface->SetColor (this->surface, 0x00, 0x00, 0x00, 0xff);
    this->surface->FillRectangles (this->surface, &rect[0], 4);
  }
}

static void directfb_overlay_blend (vo_driver_t *this_gen,
                                    vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  directfb_driver_t *this  = (directfb_driver_t *) this_gen;
  directfb_frame_t  *frame = (directfb_frame_t *) frame_gen;
  
  this->alphablend_extra_data.offset_x = frame_gen->overlay_offset_x;
  this->alphablend_extra_data.offset_y = frame_gen->overlay_offset_y;
  
  if (frame->format == DSPF_YUY2) {
    _x_blend_yuy2 (frame->vo_frame.base[0], overlay,
                frame->width, frame->height,
                frame->vo_frame.pitches[0],
                &this->alphablend_extra_data);
  }
  else {
    _x_blend_yuv (frame->vo_frame.base, overlay, 
               frame->width, frame->height,
               frame->vo_frame.pitches,
               &this->alphablend_extra_data);
  }
}

static int directfb_redraw_needed (vo_driver_t *this_gen) {
  directfb_driver_t *this  = (directfb_driver_t *) this_gen;
  
  if (_x_vo_scale_redraw_needed (&this->sc)) {
    _x_vo_scale_compute_output_size (&this->sc);
    
    if (this->caps & DLCAPS_SCREEN_LOCATION) {  
      this->layer->SetSourceRectangle (this->layer,
                                       this->sc.displayed_xoffset,
                                       this->sc.displayed_yoffset,
                                       this->sc.displayed_width, 
                                       this->sc.displayed_height );
      this->layer->SetScreenRectangle (this->layer,
                                       this->sc.gui_win_x+this->sc.output_xoffset, 
                                       this->sc.gui_win_y+this->sc.output_yoffset,
                                       this->sc.output_width,
                                       this->sc.output_height);
    }
      
    directfb_clean_output_area (this);
  }
  
  return 0;
}

static void directfb_add_recent_frame (directfb_driver_t *this, directfb_frame_t *frame) {
  int i;

  i = VO_NUM_RECENT_FRAMES-1;
  if (this->recent_frames[i])
    this->recent_frames[i]->vo_frame.free (&this->recent_frames[i]->vo_frame);

  for (; i; i--)
    this->recent_frames[i] = this->recent_frames[i-1];

  this->recent_frames[0] = frame;
}

/* directfb_display_frame(): output to overlay */
static void directfb_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  directfb_driver_t *this  = (directfb_driver_t *) this_gen;
  directfb_frame_t  *frame = (directfb_frame_t *) frame_gen;
  DFBResult          ret;
  
  directfb_add_recent_frame (this, frame);
  
  this->config.flags = DLCONF_NONE;
  
  if (frame->width != this->config.width) {
    this->config.flags |= DLCONF_WIDTH;
    this->config.width  = frame->width;
  }
   
  if (frame->height != this->config.height) {
    this->config.flags  |= DLCONF_HEIGHT;
    this->config.height  = frame->height;
  }
      
  if (frame->format != this->config.pixelformat) {
    this->config.flags      |= DLCONF_PIXELFORMAT;
    this->config.pixelformat = frame->format;
  }
  
  if (this->caps & DLCAPS_DEINTERLACING) {
    if (this->deinterlace &&
      !(this->config.options & DLOP_DEINTERLACING)) {
      this->config.flags   |= DLCONF_OPTIONS;
      this->config.options |= DLOP_DEINTERLACING;
    }
    else if (!this->deinterlace &&
            (this->config.options & DLOP_DEINTERLACING)) {
      this->config.flags   |= DLCONF_OPTIONS;
      this->config.options &= ~DLOP_DEINTERLACING;
    }
  }
  
  if (this->config.flags) {
    lprintf ("changing layer configuration to %dx%d %s.\n",
             this->config.width, this->config.height,
             (this->config.pixelformat == DSPF_YUY2) ? "YUY2" : "YV12");
    
    ret = this->layer->SetConfiguration (this->layer, &this->config);
    if (ret != DFB_OK)
      DirectFBError( "IDirectFBDisplayLayer::SetConfiguration()", ret );      
    this->layer->GetConfiguration (this->layer, &this->config);
  }
  
  if (this->sc.delivered_width  != frame->width  ||
      this->sc.delivered_height != frame->height ||
      this->sc.delivered_ratio  != frame->ratio)
  {
    this->sc.delivered_width  = frame->width; 
    this->sc.delivered_height = frame->height; 
    this->sc.delivered_ratio  = frame->ratio;  
    this->sc.crop_left        = frame->vo_frame.crop_left;
    this->sc.crop_right       = frame->vo_frame.crop_right;
    this->sc.crop_top         = frame->vo_frame.crop_top;
    this->sc.crop_bottom      = frame->vo_frame.crop_bottom;
    
    _x_vo_scale_compute_ideal_size (&this->sc);
    this->sc.force_redraw = 1;
  }
  
  directfb_redraw_needed (&this->vo_driver);
 
  this->layer->SetOpacity (this->layer, 0xff);

  if (frame->locked) {
    frame->surface->Unlock (frame->surface);
    frame->locked = 0;
  }
  
  if (this->deinterlace) {
    if (!(this->config.options & DLOP_DEINTERLACING))
      this->surface->SetBlittingFlags (this->surface, DSBLIT_DEINTERLACE);
  } else
    this->surface->SetBlittingFlags (this->surface, DSBLIT_NOFX);
  
  this->surface->Blit (this->surface, frame->surface, NULL, 0, 0);
  this->surface->Flip (this->surface, NULL,
                      (this->vsync) ? DSFLIP_WAITFORSYNC : DSFLIP_ONSYNC);
  
  frame->surface->Lock (frame->surface, 
                        DSLF_READ | DSLF_WRITE, 
                        (void**)&frame->vo_frame.base[0],
                        (int  *)&frame->vo_frame.pitches[0]);
  frame->locked = 1;
}

/* directfb_display_frame2(): output to a generic (fixed) layer */
static void directfb_display_frame2 (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  directfb_driver_t       *this   = (directfb_driver_t *) this_gen;
  directfb_frame_t        *frame  = (directfb_frame_t *) frame_gen;
  DFBSurfaceBlittingFlags  flags;
  DFBRectangle             s, d;
  DFBResult                ret;
  
  directfb_add_recent_frame (this, frame);
   
  /* TODO: try to change video mode when frame size changes */
   
  if (this->temp) {
    /* try to reduce video memory fragmentation */
    if (this->temp_frame_width  <  frame->width  ||
        this->temp_frame_height <  frame->height ||
        this->temp_frame_format != frame->format)
    {
      DFBSurfaceDescription dsc;

      lprintf ("reallocating temporary surface.\n");
      
      this->temp->Release (this->temp);
      this->temp = NULL;
      
      dsc.flags       = DSDESC_CAPS   | DSDESC_WIDTH      |
                        DSDESC_HEIGHT | DSDESC_PIXELFORMAT;
      dsc.caps        = DSCAPS_VIDEOONLY;
      dsc.width       = frame->width;
      dsc.height      = frame->height;
      dsc.pixelformat = frame->format;
      
      ret = this->dfb->CreateSurface (this->dfb, &dsc, &this->temp);
      if (ret != DFB_OK)
        DirectFBError ("IDirectFB::CreateSurface()", ret);
     
      this->temp_frame_width  = frame->width;
      this->temp_frame_height = frame->height;
      this->temp_frame_format = frame->format;
    }
  }
  
  if (this->sc.delivered_width  != frame->width  ||
      this->sc.delivered_height != frame->height ||
      this->sc.delivered_ratio  != frame->ratio)
  {
    this->sc.delivered_width  = frame->width; 
    this->sc.delivered_height = frame->height; 
    this->sc.delivered_ratio  = frame->ratio;  
    this->sc.crop_left        = frame->vo_frame.crop_left;
    this->sc.crop_right       = frame->vo_frame.crop_right;
    this->sc.crop_top         = frame->vo_frame.crop_top;
    this->sc.crop_bottom      = frame->vo_frame.crop_bottom;
    
    _x_vo_scale_compute_ideal_size (&this->sc);
    this->sc.force_redraw = 1;
  }
  
  directfb_redraw_needed (&this->vo_driver);

  this->layer->SetOpacity (this->layer, 0xff);

  if (frame->locked) {
    frame->surface->Unlock (frame->surface);
    frame->locked = 0;
  }
  
  /* source rectangle */
  s.x = this->sc.displayed_xoffset;
  s.y = this->sc.displayed_yoffset;
  s.w = this->sc.displayed_width;
  s.h = this->sc.displayed_height;
  
  /* destination rectangle */
  d.x = this->sc.gui_win_x+this->sc.output_xoffset;
  d.y = this->sc.gui_win_y+this->sc.output_yoffset;
  d.w = this->sc.output_width;
  d.h = this->sc.output_height;
  
  flags = (this->deinterlace) ? DSBLIT_DEINTERLACE : DSBLIT_NOFX;
    
  if (this->temp) {
    if (this->hw_deinterlace)
      this->surface->SetBlittingFlags (this->surface, flags);
    else
      this->temp->SetBlittingFlags (this->temp, flags);      
      
    this->temp->Blit (this->temp, frame->surface, &s, s.x, s.y);
    this->surface->StretchBlit (this->surface, this->temp, &s, &d);
  } 
  else {
    this->surface->SetBlittingFlags (this->surface, flags);
    this->surface->StretchBlit (this->surface, frame->surface, &s, &d);
  }
  
  this->surface->Flip (this->surface, NULL,
                      (this->vsync) ? DSFLIP_WAITFORSYNC : DSFLIP_ONSYNC);
  
  frame->surface->Lock (frame->surface, 
                        DSLF_READ | DSLF_WRITE, 
                        (void**)&frame->vo_frame.base[0],
                        (int  *)&frame->vo_frame.pitches[0]);
  frame->locked = 1;
}

static int directfb_get_property (vo_driver_t *this_gen, int property) {
  directfb_driver_t *this = (directfb_driver_t *) this_gen;
  
  switch (property) {
    case VO_PROP_INTERLACED:
      return this->deinterlace;
      
    case VO_PROP_ASPECT_RATIO:
      return this->sc.user_ratio;
    
    case VO_PROP_HUE:
      if (this->caps & DLCAPS_HUE)
        return this->cadj.hue;
      break;
    
    case VO_PROP_SATURATION:
      if (this->caps & DLCAPS_SATURATION)
        return this->cadj.saturation;
      break;
    
    case VO_PROP_CONTRAST:
      if (this->caps & DLCAPS_CONTRAST)
        return this->cadj.contrast;
      break;
    
    case VO_PROP_BRIGHTNESS:
      if (this->caps & DLCAPS_BRIGHTNESS)
        return this->cadj.brightness;
      break;
    
    case VO_PROP_COLORKEY:
      if (this->caps & DLCAPS_DST_COLORKEY)
        return this->colorkey;
      break;
    
    case VO_PROP_ZOOM_X:
      return this->sc.zoom_factor_x * XINE_VO_ZOOM_STEP;
    
    case VO_PROP_ZOOM_Y:
      return this->sc.zoom_factor_y * XINE_VO_ZOOM_STEP;
    
    case VO_PROP_WINDOW_WIDTH:
      return this->sc.gui_width;
    
    case VO_PROP_WINDOW_HEIGHT:
      return this->sc.gui_height;
    
    default:
      break;
  }
  
  return 0;
}

static int directfb_set_property (vo_driver_t *this_gen, 
                                  int property, int value) {
  directfb_driver_t *this = (directfb_driver_t *) this_gen;
  
  switch (property) {
    case VO_PROP_INTERLACED:
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "video_out_directfb: deinterlacing set to %d.\n", value);
      this->deinterlace = value;
      break;
      
    case VO_PROP_ASPECT_RATIO:
      if (value >= XINE_VO_ASPECT_NUM_RATIOS)
        value = XINE_VO_ASPECT_NUM_RATIOS-1;
      xprintf (this->xine, XINE_VERBOSITY_DEBUG,
               "video_out_directfb: aspect ratio changed to %s.\n",
               _x_vo_scale_aspect_ratio_name (value));
      this->sc.user_ratio = value;
      _x_vo_scale_compute_ideal_size (&this->sc);
      this->sc.force_redraw = 1;
      break;
    
    case VO_PROP_HUE:
      if (this->caps & DLCAPS_HUE) {
        if (value > 0xffff)
          value = 0xffff;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
                 "video_out_directfb: setting hue to %d.\n", value);
        this->cadj.flags = DCAF_HUE;
        this->cadj.hue   = value;
        this->layer->SetColorAdjustment (this->layer, &this->cadj);
      }
      break;
    
    case VO_PROP_SATURATION:
      if (this->caps & DLCAPS_SATURATION) {
        if (value > 0xffff)
          value = 0xffff;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
                 "video_out_directfb: setting saturation to %d.\n", value);
        this->cadj.flags      = DCAF_SATURATION;
        this->cadj.saturation = value;
        this->layer->SetColorAdjustment (this->layer, &this->cadj);
      }
      break;
    
    case VO_PROP_CONTRAST:
      if (this->caps & DLCAPS_CONTRAST) {
        if (value > 0xffff)
          value = 0xffff;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
                 "video_out_directfb: setting contrast to %d.\n", value);
        this->cadj.flags    = DCAF_CONTRAST;
        this->cadj.contrast = value;
        this->layer->SetColorAdjustment (this->layer, &this->cadj);
      }
      break;
    
    case VO_PROP_BRIGHTNESS:
      if (this->caps & DLCAPS_BRIGHTNESS) {
        if (value > 0xffff)
          value = 0xffff;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
                 "video_out_directfb: setting brightness to %d.\n", value);
        this->cadj.flags      = DCAF_BRIGHTNESS;
        this->cadj.brightness = value;
        this->layer->SetColorAdjustment (this->layer, &this->cadj);
      }
      break;
    
    case VO_PROP_COLORKEY:
      if (this->caps & DLCAPS_DST_COLORKEY) {
        if (value > 0xffffff)
          value = 0xffffff;
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
                 "video_out_directfb: setting colorkey to 0x%06x.\n", value);
        this->colorkey = value;
        this->layer->SetDstColorKey (this->layer, (value & 0xff0000) >> 16,
                                                  (value & 0x00ff00) >>  8,
                                                  (value & 0x0000ff) >>  0);
        directfb_clean_output_area (this);
      }
      break;
    
    case VO_PROP_ZOOM_X:
      if (value >= XINE_VO_ZOOM_MIN && value <= XINE_VO_ZOOM_MAX) {
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
                 "video_out_directfb: Zoom X by factor %d.\n", value);
        this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size (&this->sc);
        this->sc.force_redraw = 1;
      }
      break;
    
    case VO_PROP_ZOOM_Y:
      if (value >= XINE_VO_ZOOM_MIN && value <= XINE_VO_ZOOM_MAX) {
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
                 "video_out_directfb: Zoom Y by factor %d.\n", value);
        this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size (&this->sc);
        this->sc.force_redraw = 1;
      }
      break;
    
    default:
      break;
  } 
      
  return value;
}

static void directfb_get_property_min_max (vo_driver_t *this_gen,
				                                   int property, int *min, int *max) {
  directfb_driver_t *this = (directfb_driver_t *) this_gen;
  
  switch (property) {
    case VO_PROP_INTERLACED:
      *min = 0;
      *max = 1;
      return;
      
    case VO_PROP_ASPECT_RATIO:
      *min = 0; 
      *max = XINE_VO_ASPECT_NUM_RATIOS-1;
      return;
    
    case VO_PROP_HUE:
      if (this->caps & DLCAPS_HUE) {
        *min = 0x0000;
        *max = 0xffff;
        return;
      }
      break;
    
    case VO_PROP_SATURATION:
      if (this->caps & DLCAPS_SATURATION) {
        *min = 0x0000;
        *max = 0xffff;
        return;
      }
      break;
    
    case VO_PROP_CONTRAST:
      if (this->caps & DLCAPS_CONTRAST) {
        *min = 0x0000;
        *max = 0xffff;
        return;
      }
      break;
    
    case VO_PROP_BRIGHTNESS:
      if (this->caps & DLCAPS_BRIGHTNESS) {
        *min = 0x0000;
        *max = 0xffff;
        return;
      }
      break;
    
    case VO_PROP_COLORKEY:
      if (this->caps & DLCAPS_DST_COLORKEY) {
        *min = 0x000000;
        *max = 0xffffff;
        return;
      }
      break;
    
    case VO_PROP_ZOOM_X:
    case VO_PROP_ZOOM_Y:
      *min = XINE_VO_ZOOM_MIN;
      *max = XINE_VO_ZOOM_MAX;
      return;
    
    default:
      break;
  }
               
  *min = 0;
  *max = 0;
}

static int directfb_gui_data_exchange (vo_driver_t *this_gen, 
                                       int data_type, void *data) {
  directfb_driver_t *this = (directfb_driver_t *) this_gen;
  
  switch (data_type) {
    case XINE_GUI_SEND_DRAWABLE_CHANGED:
#ifdef HAVE_X11
      if (this->visual_type == XINE_VISUAL_TYPE_X11) {
        this->drawable = (Drawable) data;
        XLockDisplay (this->display);
        XFreeGC (this->display, this->gc);
        this->gc = XCreateGC (this->display, this->drawable, 0, NULL);
        XUnlockDisplay (this->display);
      }
#endif
      return 0;
      
    case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO: {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;
      
      _x_vo_scale_translate_gui2video (&this->sc,
                                       rect->x, rect->y, 
                                       &x1, &y1);
      _x_vo_scale_translate_gui2video (&this->sc,
                                       rect->x + rect->w, rect->y + rect->h,
			                                 &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
    } return 0;
    
    default:
      break;
  }
  
  return -1;
}

static void directfb_dispose (vo_driver_t *this_gen) {
  directfb_driver_t *this = (directfb_driver_t *) this_gen;
 
  if (this) {
    int i;
    
    for (i = 0; i < VO_NUM_RECENT_FRAMES; i++) {
      directfb_frame_t *frame = this->recent_frames[i];
      if (frame)
        frame->vo_frame.free (&frame->vo_frame);
    }
        
#ifdef HAVE_X11
    if (this->visual_type == XINE_VISUAL_TYPE_X11) {
      XLockDisplay (this->display);
      XFreeGC (this->display, this->gc);
      XUnlockDisplay (this->display);
    }
#endif

    if (this->temp)
      this->temp->Release (this->temp);
    
    if (this->surface)
      this->surface->Release (this->surface);
    
    if (this->layer) {
      this->layer->SetColorAdjustment (this->layer, &this->default_cadj);
      this->layer->Release (this->layer);
    }
    
    if (this->dfb)
      this->dfb->Release (this->dfb);

    _x_alphablend_free (&this->alphablend_extra_data);

    free (this);
  }
}

/*** misc functions ****/

static DFBEnumerationResult find_overlay (DFBDisplayLayerID id,
                                          DFBDisplayLayerDescription dsc, void *ctx) {
  DFBDisplayLayerID *ret_id = (DFBDisplayLayerID *) ctx;
  
  if (dsc.caps & DLCAPS_SURFACE &&
      dsc.caps & DLCAPS_SCREEN_LOCATION) {
    *ret_id = id;
    return DFENUM_CANCEL;
  }
  
  return DFENUM_OK;
}

static DFBResult probe_device (directfb_driver_t *this, DFBDisplayLayerID id) {
  IDirectFBDisplayLayer      *layer;
  DFBDisplayLayerDescription  dsc;
  DFBDisplayLayerConfig       config;
  DFBResult                   ret;
    
  ret = this->dfb->GetDisplayLayer (this->dfb, id, &layer);
  if (ret != DFB_OK) {
    DirectFBError( "IDirectFB::GetDisplayLayer()", ret );
    return ret;
  }
 
  layer->SetCooperativeLevel (layer, DLSCL_EXCLUSIVE);
    
  /* hide */
  layer->SetOpacity (layer, 0x00);
    
  /* check if YUY2 is supported */
  config.flags       = DLCONF_PIXELFORMAT;
  config.pixelformat = DSPF_YUY2;
    
  ret = layer->TestConfiguration (layer, &config, NULL);
  if (ret != DFB_OK) {
    xprintf (this->xine, XINE_VERBOSITY_LOG, 
             "video_out_directfb: Display Layer #%d doesn't support YUY2.\n", id);
    layer->Release (layer);
    return DFB_UNSUPPORTED;
  }
    
  /* check if YV12 is supported */
  config.flags       = DLCONF_PIXELFORMAT;
  config.pixelformat = DSPF_YV12;
    
  ret = layer->TestConfiguration (layer, &config, NULL);
  if (ret != DFB_OK) {
    xprintf (this->xine, XINE_VERBOSITY_LOG, 
             "video_out_directfb: Display Layer #%d doesn't support YV12.\n", id);
    layer->Release (layer);
    return DFB_UNSUPPORTED;
  }
  
  layer->GetDescription (layer, &dsc);
  
  this->layer = layer;
  this->caps  = dsc.caps;

  xprintf (this->xine, XINE_VERBOSITY_LOG, 
           "video_out_directfb: using Display Layer #%d.\n", id);
           
  return DFB_OK;
}

static void update_config_cb (void *data, xine_cfg_entry_t *entry) {
  directfb_driver_t *this = (directfb_driver_t *) data;
  
  if (strcmp (entry->key, "video.device.directfb_buffermode") == 0) {
    DFBDisplayLayerConfig config = { .flags = DLCONF_BUFFERMODE };
    
    switch (entry->num_value) {
      case 0:
        config.buffermode = DLBM_FRONTONLY;
        break;
      case 2:
        config.buffermode = DLBM_TRIPLE;
        break;
      default:
        config.buffermode = DLBM_BACKVIDEO;
        break;
    }
    
    if (config.buffermode != this->config.buffermode) {      
      if (this->layer->SetConfiguration (this->layer, &config) != DFB_OK) {
        xprintf (this->xine, XINE_VERBOSITY_NONE,
                 "video_out_directfb: failed to set buffermode to %d!\n", 
                 entry->num_value);
        return;
      }
      
      this->layer->GetConfiguration (this->layer, &this->config);
    }
  }
  else if (strcmp (entry->key, "video.device.directfb_colorkeying") == 0) {
    DFBDisplayLayerConfig config = { .flags = DLCONF_OPTIONS };
       
    if (entry->num_value)
      config.options = this->config.options | DLOP_DST_COLORKEY;
    else
      config.options = this->config.options & ~DLOP_DST_COLORKEY;
    
    if (config.options != this->config.options) {  
      if (this->layer->SetConfiguration (this->layer, &config) != DFB_OK) {
          xprintf (this->xine, XINE_VERBOSITY_NONE,
                   "video_out_directfb: failed to set colorkeying to %d!\n", 
                   entry->num_value);
          return;
      }
    
      this->layer->GetConfiguration (this->layer, &this->config);
      directfb_clean_output_area (this);
    }
  }  
  else if (strcmp (entry->key, "video.device.directfb_colorkey") == 0) {
    this->colorkey = entry->num_value;
    this->layer->SetDstColorKey (this->layer, (this->colorkey & 0xff0000) >> 16,
                                              (this->colorkey & 0x00ff00) >>  8,
                                              (this->colorkey & 0x0000ff) >>  0);
    directfb_clean_output_area (this);
  }
  else if (strcmp (entry->key, "video.device.directfb_flicker_filtering") == 0) {
    DFBDisplayLayerConfig config = { .flags = DLCONF_OPTIONS };
    
    if (entry->num_value)
      config.options = this->config.options | DLOP_FLICKER_FILTERING;
    else
      config.options = this->config.options & ~DLOP_FLICKER_FILTERING;
      
    if (config.options != this->config.options) {
      if (this->layer->SetConfiguration (this->layer, &config) != DFB_OK) {
          xprintf (this->xine, XINE_VERBOSITY_NONE,
                   "video_out_directfb: failed to set flicker_filtering to %d!\n", 
                   entry->num_value);
          return;
      }
    
      this->layer->GetConfiguration (this->layer, &this->config);
    }
  }
  else if (strcmp (entry->key, "video.device.directfb_field_parity") == 0) {
    DFBDisplayLayerConfig config = { .flags = DLCONF_OPTIONS };
    
    if (entry->num_value)
      config.options = this->config.options | DLOP_FIELD_PARITY;
    else
      config.options = this->config.options & ~DLOP_FIELD_PARITY;
      
    if (config.options != this->config.options) {
      if (this->layer->SetConfiguration (this->layer, &config) != DFB_OK) {
          xprintf (this->xine, XINE_VERBOSITY_NONE,
                   "video_out_directfb: failed to set field_parity to %d!\n", 
                   entry->num_value);
          return;
      }
    
      this->layer->GetConfiguration (this->layer, &this->config);
    } 
  }
  else if (strcmp (entry->key, "video.device.directfb_vsync") == 0) {
    this->vsync = entry->num_value;
  }
}
   
static void init_config (directfb_driver_t *this) {
  config_values_t   *config             = this->xine->config;
  static const char *buffermode_enum[]  = {"single", "double", "triple", 0};
  static const char *fieldparity_enum[] = {"none", "top", "bottom", 0};
  
  this->buffermode = config->register_enum (config,
      "video.device.directfb_buffermode", this->buffermode, (char**)buffermode_enum,
      _("video layer buffering mode"),
      _("Select the buffering mode of the output layer. "
        "Double or triple buffering give a smoother playback, "
        "but consume more video memory."),
      10, update_config_cb, (void *)this);
  
  this->vsync      = config->register_bool (config,
      "video.device.directfb_vsync", this->vsync,
      _("wait for vertical retrace"),
      _("Enable synchronizing the update of the video image to the "
		    "repainting of the entire screen (\"vertical retrace\")."),
		  10, update_config_cb, (void *)this);
  
  if (this->caps & DLCAPS_DST_COLORKEY) {
    this->colorkeying = config->register_bool  (config,
          "video.device.directfb_colorkeying", this->colorkeying,
          _("enable video color key"),
          _("Enable using a color key to tell the graphics card "
            "where to overlay the video image."),
          20, update_config_cb, (void *)this);
    
    this->colorkey    = config->register_range (config,
          "video.device.directfb_colorkey", this->colorkey, 0, 0xffffff,
          _("video color key"),
          _("The color key is used to tell the graphics card "
            "where to overlay the video image. Try different values, "
            "if you experience windows becoming transparent."),
          10, update_config_cb, (void  *)this);
  }
  
  if (this->caps & DLCAPS_FLICKER_FILTERING) {
    this->flicker_filtering = config->register_bool( config,
          "video.device.directfb_flicker_filtering", this->flicker_filtering,
          _("flicker filtering"),
          _("Enable Flicker Filetring for a smooth output on an interlaced display."),
          10, update_config_cb, (void *)this);
  }
  
  if (this->caps & DLCAPS_FIELD_PARITY) {
    this->field_parity  = config->register_enum( config,
          "video.device.directfb_field_parity", this->field_parity, (char**)fieldparity_enum,
          _("field parity"),
          _("For an interlaced display, enable controlling "
            "the field parity (\"none\"=disabled)."),
          10, update_config_cb, (void *)this);
  }
} 

static DFBResult init_device (directfb_driver_t *this) {
  IDirectFBSurface           *surface;
  DFBDisplayLayerConfig       config;
  DFBDisplayLayerConfigFlags  failed = 0;
  DFBResult                   ret;
   
  /* get current color cadjustment */
  this->layer->GetColorAdjustment (this->layer, &this->default_cadj);
  this->cadj = this->default_cadj;
  
  /* set layer configuration */
  config.flags       = DLCONF_BUFFERMODE | DLCONF_PIXELFORMAT | DLCONF_OPTIONS;
  config.pixelformat = DSPF_YV12;
  config.options     = DLOP_NONE;
  
  switch (this->buffermode) {
    case 0:
      config.buffermode = DLBM_FRONTONLY;
      break;
    case 2:
      config.buffermode = DLBM_TRIPLE;
      break;
    default:
      config.buffermode = DLBM_BACKVIDEO;
      break;
  }
  
  if (this->colorkeying)
    config.options |= DLOP_DST_COLORKEY;
    
  if (this->flicker_filtering)
    config.options |= DLOP_FLICKER_FILTERING;
    
  if (this->field_parity)
    config.options |= DLOP_FIELD_PARITY;
  
  /* test current configuration */
  ret = this->layer->TestConfiguration (this->layer, &config, &failed);
  if (failed & DLCONF_BUFFERMODE) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
             "video_out_directfb: layer doesn't support buffermode %d!\n",
             this->buffermode);   
    config.flags &= ~DLCONF_BUFFERMODE;
  }
  if (failed & DLCONF_OPTIONS) {
    xprintf (this->xine, XINE_VERBOSITY_LOG,
             "video_out_directfb: layer doesn't support options 0x%08x!\n",
             config.options);
    config.flags &= ~DLCONF_OPTIONS;
  }
  
  ret = this->layer->SetConfiguration (this->layer, &config);
  /* this should never happen */
  if (ret != DFB_OK) {
    DirectFBError ("IDirectFBDisplayLayer::SetConfiguration()", ret);
    return ret;
  }

  this->layer->GetConfiguration (this->layer, &this->config); 
  
  if (this->caps & DLCAPS_DST_COLORKEY) {
    this->layer->SetDstColorKey (this->layer, (this->colorkey & 0xff0000) >> 16,
                                              (this->colorkey & 0x00ff00) >>  8,
                                              (this->colorkey & 0x0000ff) >>  0);
  }
  
  if (this->field_parity)
    this->layer->SetFieldParity (this->layer, this->field_parity-1);
  
  /* retrieve layer's surface */
  ret = this->layer->GetSurface (this->layer, &surface);
  if (ret != DFB_OK) {
    DirectFBError ("IDirectFBDisplayLayer::GetSurface()", ret);
    return ret;
  }
  
  /* clear surface buffers */
  surface->Clear (surface, 0x00, 0x00, 0x00, 0xff);
  surface->Flip  (surface, NULL, 0);
  surface->Clear (surface, 0x00, 0x00, 0x00, 0xff);
  surface->Flip  (surface, NULL, 0);
  surface->Clear (surface, 0x00, 0x00, 0x00, 0xff);
  surface->Flip  (surface, NULL, 0);
  
  this->surface = surface;
  
  /* check if stretchblit is hardware accelerated */
  if (!(this->caps & DLCAPS_SCREEN_LOCATION)) {
    IDirectFBSurface      *temp;
    DFBSurfaceDescription  dsc;
    DFBAccelerationMask    mask = DFXL_NONE;
    
    dsc.flags       = DSDESC_CAPS   | DSDESC_WIDTH      |
                      DSDESC_HEIGHT | DSDESC_PIXELFORMAT;
    dsc.caps        = DSCAPS_VIDEOONLY;
    dsc.width       = 352;
    dsc.height      = 240;
    dsc.pixelformat = DSPF_YV12;
    
    if (this->dfb->CreateSurface (this->dfb, &dsc, &temp) == DFB_OK) {
      this->surface->GetAccelerationMask (this->surface, temp, &mask);
      
      if (mask & DFXL_STRETCHBLIT) {
        xprintf (this->xine, XINE_VERBOSITY_LOG,
                 _("video_out_directfb: "
                   "using hardware accelerated image scaling.\n"));
        this->temp = temp;
        
        /* check if stretchblit with deinterlacing is supported */
        this->surface->SetBlittingFlags (this->surface, DSBLIT_DEINTERLACE);
        this->surface->GetAccelerationMask (this->surface, temp, &mask);
        this->surface->SetBlittingFlags (this->surface, DSBLIT_NOFX);
        
        this->hw_deinterlace = (mask & DFXL_STRETCHBLIT) ? 1 : 0;
        if (this->hw_deinterlace) {
          xprintf (this->xine, XINE_VERBOSITY_LOG,
                  _("video_out_directfb: "
                    "image scaling with deinterlacing is hardware accelerated.\n"));
        }
        
        /* used to decide reallocation */
        this->temp_frame_width  = 352;
        this->temp_frame_height = 240;
        this->temp_frame_format = DSPF_YV12;
      } 
      else
        temp->Release (temp);
    }
  }
  
  return DFB_OK;
}    

static void get_screen_size (directfb_driver_t *this, int *ret_w, int *ret_h) {
  int w = -640, h = -480;
#ifdef HAVE_FB
  int fd;
  struct fb_var_screeninfo var;
  
  fd = open (getenv ("FRAMEBUFFER") ? : "/dev/fb0", O_RDONLY);
  if (fd > 0) {
    if (ioctl (fd, FBIOGET_VSCREENINFO, &var) == 0) {
      w = var.xres;
      h = var.yres;
    }
    close (fd);
  }
#endif
  if (w < 1 || h < 1) {
    IDirectFBDisplayLayer *primary;
    DFBDisplayLayerConfig  config;
    DFBResult              ret;
  
    ret = this->dfb->GetDisplayLayer (this->dfb, DLID_PRIMARY, &primary);
    if (ret == DFB_OK) {
      if (primary->GetConfiguration (primary, &config) == DFB_OK) {
        w = config.width;
        h = config.height;
      }
      primary->Release (primary);
    } 
    else
      DirectFBError( "IDirectFB::GetDisplayLayer( DLID_PRIMARY )", ret );
  }

  xprintf (this->xine, XINE_VERBOSITY_DEBUG,
           "video_out_directfb: screen size is %dx%d.\n", abs(w), abs(h));
  *ret_w = abs(w); *ret_h = abs(h);
}

static void directfb_frame_output_cb (void *user_data, int video_width, int video_height,
                                      double video_pixel_aspect, int *dest_x, int *dest_y,
                                      int *dest_width, int *dest_height,
                                      double *dest_pixel_aspect, int *win_x, int *win_y) {
  directfb_driver_t *this = (directfb_driver_t *) user_data;

  *dest_x            = 0;
  *dest_y            = 0;
  *dest_width        = this->screen_width;
  *dest_height       = this->screen_height;
  *dest_pixel_aspect = video_pixel_aspect ? : 1.0;
  *win_x             = 0;
  *win_y             = 0;
}

/*** DirectFB plugin functions ***/ 

static vo_driver_t *open_plugin_fb (video_driver_class_t *class_gen, const void *visual_gen) {
  directfb_class_t  *class  = (directfb_class_t *) class_gen;
  directfb_driver_t *this;
  fb_visual_t       *visual = (fb_visual_t *) visual_gen;
  config_values_t   *config = class->xine->config;
  IDirectFBScreen   *screen;
  DFBDisplayLayerID  id     = DLID_PRIMARY;
  DFBResult          ret;

  this = xine_xmalloc (sizeof (directfb_driver_t));
  if (!this)
    return NULL;
  
  this->visual_type = XINE_VISUAL_TYPE_FB;
  this->xine        = class->xine;
  
  /* initialize DirectFB */ 
  ret = DirectFBInit (NULL, NULL);
  if (ret != DFB_OK) {
    DirectFBError ("DirectFBInit()", ret);
    free (this);
    return NULL;
  }
  
  DirectFBSetOption ("bg-none", NULL);
  DirectFBSetOption ("no-vt"  , NULL);
  /* linux_input blocks input from console, disable it */
  DirectFBSetOption ("disable-module", "linux_input");
  
  /* create the main interface or retrieve an already existing one */
  ret = DirectFBCreate (&this->dfb);
  if (ret != DFB_OK) {
    DirectFBError ("DirectFBCreate()", ret);
    free (this);
    return NULL;
  }
  
  /* retrieve an interface to the current screen */
  ret = this->dfb->GetScreen (this->dfb, DSCID_PRIMARY, &screen);
  if (ret != DFB_OK) {
    DirectFBError ("IDirectFB::GetScreen( DSCID_PRIMARY )", ret);
    this->dfb->Release (this->dfb);
    free (this);
    return NULL;
  }
  
  /* find an overlay layer on the current screen */
  ret = screen->EnumDisplayLayers (screen, find_overlay, (void*)&id);
  screen->Release (screen);
  if (ret != DFB_OK) {
    DirectFBError( "IDirectFBScreen::EnumDisplayLayers()", ret);
    this->dfb->Release (this->dfb);
    free (this);
    return NULL;
  }
 
  /* allow user/application to select a different layer */
  id = config->register_num (config,
                             "video.device.directfb_layer_id", id,
                             _("video layer id"),
                             _("Select the video output layer by its id."),
                             20, NULL, 0);
                             
  if (probe_device (this, id) != DFB_OK) {
    xprintf (this->xine, XINE_VERBOSITY_NONE,
             _("video_out_directfb: no usable output layer was found!\n"));
    this->dfb->Release (this->dfb);
    free (this);
    return NULL;
  }

  /* debugging option */
  if (this->caps & DLCAPS_SCREEN_LOCATION) {
    if (config->register_bool (config, "video.device.directfb_fixed_overlay", 0,
                _("make the overlay behave like a fixed layer (for debugging)"),
                _("make the overlay behave like a fixed layer (for debugging)"),
                                                                  100, NULL, 0)) {
      this->caps &= ~DLCAPS_SCREEN_LOCATION;
    }
  }
  
  /* set default configuration */
  this->buffermode        = 1; // double
  this->vsync             = 0;
  this->colorkeying       = 0;
  this->colorkey          = DEFAULT_COLORKEY;
  this->flicker_filtering = 0;
  this->field_parity      = 0;
  
  /* get user configuration */
  init_config (this);

  /* set layer configuration */
  if (init_device (this) != DFB_OK) {
    this->layer->Release (this->layer);
    this->dfb->Release (this->dfb);
    free (this);
    return NULL;
  }
  
  if (!(this->caps & DLCAPS_SCREEN_LOCATION)) {
    this->screen_width  = this->config.width;
    this->screen_height = this->config.height;
  } else
    get_screen_size (this, &this->screen_width, &this->screen_height );

  _x_alphablend_init (&this->alphablend_extra_data, this->xine);
  
  _x_vo_scale_init (&this->sc, 1, 0, this->xine->config);
  this->sc.user_ratio = XINE_VO_ASPECT_AUTO;
  this->sc.gui_width  = this->screen_width;
  this->sc.gui_height = this->screen_height; 
  
  if (visual) {
    this->sc.frame_output_cb = visual->frame_output_cb;
    this->sc.user_data       = visual->user_data;
  } else {
    this->sc.frame_output_cb = directfb_frame_output_cb;
    this->sc.user_data       = (void *) this;
  }

  this->vo_driver.get_capabilities     = directfb_get_capabilities;
  this->vo_driver.alloc_frame          = directfb_alloc_frame;
  this->vo_driver.update_frame_format  = directfb_update_frame_format;
  this->vo_driver.overlay_begin        = NULL; /* not used */
  this->vo_driver.overlay_blend        = directfb_overlay_blend;
  this->vo_driver.overlay_end          = NULL; /* not used */
  this->vo_driver.display_frame        = (this->caps & DLCAPS_SCREEN_LOCATION)
                                         ? directfb_display_frame
                                         : directfb_display_frame2;
  this->vo_driver.get_property         = directfb_get_property;
  this->vo_driver.set_property         = directfb_set_property;
  this->vo_driver.get_property_min_max = directfb_get_property_min_max;
  this->vo_driver.gui_data_exchange    = directfb_gui_data_exchange;
  this->vo_driver.redraw_needed        = directfb_redraw_needed;
  this->vo_driver.dispose              = directfb_dispose;

  return &this->vo_driver;
}

static char* get_identifier_fb (video_driver_class_t *this_gen) {
  return "DirectFB";
}

static char* get_description_fb (video_driver_class_t *this_gen) {
  return _("xine video output plugin using DirectFB.");
}

static void dispose_class_fb (video_driver_class_t *this_gen) {
  directfb_class_t *this = (directfb_class_t *) this_gen;

  free (this);
}

static void *init_class_fb (xine_t *xine, void *visual_gen) {
  directfb_class_t *this;
  const char       *error;
  
  /* check DirectFB version */
  error = DirectFBCheckVersion( DIRECTFB_MAJOR_VERSION,
                                DIRECTFB_MINOR_VERSION,
                                DIRECTFB_MICRO_VERSION );
  if (error) {
    xprintf (xine, XINE_VERBOSITY_NONE,
             "video_out_directfb: %s!\n", error);
    return NULL;
  }

  this = (directfb_class_t *) xine_xmalloc (sizeof (directfb_class_t));
  this->driver_class.open_plugin     = open_plugin_fb;
  this->driver_class.get_identifier  = get_identifier_fb;
  this->driver_class.get_description = get_description_fb;
  this->driver_class.dispose         = dispose_class_fb;

  this->xine = xine;

  return this;
}

static vo_info_t vo_info_directfb_fb = {
  8,                   /* priority    */
  XINE_VISUAL_TYPE_FB  /* visual type */
};

/*** XDirectFB plugin functions ****/

#ifdef HAVE_X11
static vo_driver_t *open_plugin_x11 (video_driver_class_t *class_gen, const void *visual_gen) {
  directfb_class_t  *class  = (directfb_class_t *) class_gen;
  directfb_driver_t *this;
  x11_visual_t      *visual = (x11_visual_t *) visual_gen;
  XWindowAttributes  attrs;
  IDirectFBScreen   *screen;
  DFBDisplayLayerID  id     = (DFBDisplayLayerID) -1;
  DFBResult          ret;

  this = xine_xmalloc (sizeof (directfb_driver_t));
  if (!this)
    return NULL;
  
  this->visual_type = XINE_VISUAL_TYPE_X11;
  this->xine        = class->xine;
  
  /* initialize DirectFB */ 
  ret = DirectFBInit (NULL, NULL);
  if (ret != DFB_OK) {
    DirectFBError ("DirectFBInit()", ret);
    free (this);
    return NULL;
  }
  
  /* create the main interface or retrieve an already existing one */
  ret = DirectFBCreate (&this->dfb);
  if (ret != DFB_OK) {
    DirectFBError ("DirectFBCreate()", ret);
    free (this);
    return NULL;
  }
  
  /* retrieve an interface to the current screen */
  ret = this->dfb->GetScreen (this->dfb, DSCID_PRIMARY, &screen);
  if (ret != DFB_OK) {
    DirectFBError ("IDirectFB::GetScreen( DSCID_PRIMARY )", ret);
    this->dfb->Release (this->dfb);
    free (this);
    return NULL;
  }
  
  /* find an overlay layer on the current screen */
  ret = screen->EnumDisplayLayers (screen, find_overlay, (void*)&id);
  screen->Release (screen);
  if (ret != DFB_OK) {
    DirectFBError( "IDirectFBScreen::EnumDisplayLayers()", ret);
    this->dfb->Release (this->dfb);
    free (this);
    return NULL;
  }
                             
  if (probe_device (this, id) != DFB_OK) {
    xprintf (this->xine, XINE_VERBOSITY_NONE,
             _("video_out_directfb: no usable overlay layer was found!\n"));
    this->dfb->Release (this->dfb);
    free (this);
    return NULL;
  }
  
  /* set default configuration */
  this->buffermode        = 1; // double
  this->vsync             = 0;
  this->colorkeying       = (this->caps & DLCAPS_DST_COLORKEY) ? 1 : 0;
  this->colorkey          = DEFAULT_COLORKEY;
  this->flicker_filtering = 0;
  this->field_parity      = 0;
  
  /* get user configuration */
  init_config (this);

  /* set layer configuration */
  if (init_device (this) != DFB_OK) {
    this->layer->Release (this->layer);
    this->dfb->Release (this->dfb);
    free (this);
    return NULL;
  }
  
  this->display  = visual->display;
  this->screen   = visual->screen;
  this->drawable = visual->d;
  this->gc       = XCreateGC (this->display, this->drawable, 0, NULL);
  
  XGetWindowAttributes (this->display, this->drawable, &attrs);
  this->depth = attrs.depth;

  _x_alphablend_init (&this->alphablend_extra_data, this->xine);
  
  _x_vo_scale_init (&this->sc, 0, 0, this->xine->config);
  this->sc.user_ratio      = XINE_VO_ASPECT_AUTO;
  this->sc.gui_width       = attrs.width;
  this->sc.gui_height      = attrs.height;
  this->sc.frame_output_cb = visual->frame_output_cb;
  this->sc.user_data       = visual->user_data;

  this->vo_driver.get_capabilities     = directfb_get_capabilities;
  this->vo_driver.alloc_frame          = directfb_alloc_frame;
  this->vo_driver.update_frame_format  = directfb_update_frame_format;
  this->vo_driver.overlay_begin        = NULL; /* not used */
  this->vo_driver.overlay_blend        = directfb_overlay_blend;
  this->vo_driver.overlay_end          = NULL; /* not used */
  this->vo_driver.display_frame        = directfb_display_frame;
  this->vo_driver.get_property         = directfb_get_property;
  this->vo_driver.set_property         = directfb_set_property;
  this->vo_driver.get_property_min_max = directfb_get_property_min_max;
  this->vo_driver.gui_data_exchange    = directfb_gui_data_exchange;
  this->vo_driver.redraw_needed        = directfb_redraw_needed;
  this->vo_driver.dispose              = directfb_dispose;

  return &this->vo_driver;
}

static char* get_identifier_x11 (video_driver_class_t *this_gen) {
  return "XDirectFB";
}

static char* get_description_x11 (video_driver_class_t *this_gen) {
  return _("xine video output plugin using DirectFB under XDirectFB.");
}

static void dispose_class_x11 (video_driver_class_t *this_gen) {
  directfb_class_t *this = (directfb_class_t *) this_gen;

  free (this);
}

static void *init_class_x11 (xine_t *xine, void *visual_gen) {
  directfb_class_t *this;
  x11_visual_t     *visual = (x11_visual_t *) visual_gen;
  const char       *error;
  
  /* check DirectFB version */
  error = DirectFBCheckVersion( DIRECTFB_MAJOR_VERSION,
                                DIRECTFB_MINOR_VERSION,
                                DIRECTFB_MICRO_VERSION );
  if (error) {
    xprintf (xine, XINE_VERBOSITY_NONE,
             "video_out_directfb: %s!\n", error);
    return NULL;
  }
  
  if (!visual) {
    xprintf (xine, XINE_VERBOSITY_DEBUG,
             "video_out_directfb: x11 visual is required!\n");
    return NULL;
  }
  
  /* check if we are running under XDirectFB */
  if (strcmp (XServerVendor (visual->display), "Denis Oliver Kropp"))
    return NULL;

  this = (directfb_class_t *) xine_xmalloc (sizeof (directfb_class_t));
  this->driver_class.open_plugin     = open_plugin_x11;
  this->driver_class.get_identifier  = get_identifier_x11;
  this->driver_class.get_description = get_description_x11;
  this->driver_class.dispose         = dispose_class_x11;

  this->xine = xine;

  return this;
}

static vo_info_t vo_info_directfb_x11 = {
  8,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};
#endif /* HAVE_X11 */

/*********/

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, VIDEO_OUT_DRIVER_IFACE_VERSION, "DirectFB",
    XINE_VERSION_CODE, &vo_info_directfb_fb, init_class_fb },
#ifdef HAVE_X11
  { PLUGIN_VIDEO_OUT, VIDEO_OUT_DRIVER_IFACE_VERSION, "XDirectFB",
    XINE_VERSION_CODE, &vo_info_directfb_x11, init_class_x11 },
#endif
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
