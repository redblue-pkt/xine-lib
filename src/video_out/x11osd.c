/* 
 * Copyright (C) 2003 the xine project
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
 * $Id: x11osd.c,v 1.7 2004/03/23 09:29:25 esnel Exp $
 *
 * x11osd.c, use X11 Nonrectangular Window Shape Extension to draw xine OSD
 *
 * Nov 2003 - Miguel Freitas
 *
 * based on ideas and code of
 * xosd Copyright (c) 2000 Andre Renaud (andre@ignavus.net)
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <assert.h>

#include <netinet/in.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/Xatom.h>

#include "xine_internal.h"
#include "alphablend.h"
#include "x11osd.h"

struct x11osd
{
  Display *display;
  int screen;
  Window window;
  Window parent_window;
  unsigned int depth;
  Pixmap mask_bitmap;
  Pixmap bitmap;
  Visual *visual;
  Colormap cmap;

  GC gc;
  GC mask_gc;
  GC mask_gc_back;

  int width;
  int height;
  int x;
  int y;
  int clean;
  int mapped;
  xine_t *xine;
};


void
x11osd_expose (x11osd * osd)
{
  assert (osd);

  XShapeCombineMask (osd->display, osd->window, ShapeBounding, 0, 0,
                     osd->mask_bitmap, ShapeSet);

  if( !osd->clean ) {

    if( !osd->mapped )
      XMapRaised (osd->display, osd->window);
    osd->mapped = 1;
    
    XCopyArea (osd->display, osd->bitmap, osd->window, osd->gc, 0, 0,
               osd->width, osd->height, 0, 0);
  } else {
    if( osd->mapped )
      XUnmapWindow (osd->display, osd->window);
    osd->mapped = 0;
  }
}


void
x11osd_resize (x11osd * osd, int width, int height)
{
  assert (osd);
  osd->width = width;
  osd->height = height;

  XResizeWindow (osd->display, osd->window, osd->width, osd->height);
  XFreePixmap (osd->display, osd->mask_bitmap);
  osd->mask_bitmap =
    XCreatePixmap (osd->display, osd->window, osd->width, osd->height,
		   1);
  XFillRectangle (osd->display, osd->mask_bitmap, osd->mask_gc_back,
		  0, 0, osd->width, osd->height);

  XFreePixmap (osd->display, osd->bitmap);
  osd->bitmap =
    XCreatePixmap (osd->display, osd->window, osd->width,
		   osd->height, osd->depth);
}

void
x11osd_drawable_changed (x11osd * osd, Window window)
{
  assert (osd);

/*
  Do I need to recreate the GC's??

  XFreeGC (osd->display, osd->gc);
  XFreeGC (osd->display, osd->mask_gc);
  XFreeGC (osd->display, osd->mask_gc_back);
*/
  XFreePixmap (osd->display, osd->bitmap);
  XFreePixmap (osd->display, osd->mask_bitmap);
  XFreeColormap (osd->display, osd->cmap);
  XDestroyWindow (osd->display, osd->window);

  /* we need to call XSync(), because otherwise, calling XDestroyWindow()
     on the parent window could destroy our OSD window twice !! */
  XSync (osd->display, False);

  osd->parent_window = window;
  osd->window = XCreateSimpleWindow (osd->display, 
                                     osd->parent_window,
                                     0, 0,
                                     osd->width, osd->height, 1, 
                                     BlackPixel (osd->display, osd->screen),
                                     BlackPixel (osd->display, osd->screen));

  osd->mask_bitmap =
    XCreatePixmap (osd->display, osd->window, osd->width, osd->height,
		   1);
  XFillRectangle (osd->display, osd->mask_bitmap, osd->mask_gc_back,
		  0, 0, osd->width, osd->height);

  osd->bitmap =
    XCreatePixmap (osd->display, osd->window, osd->width,
		   osd->height, osd->depth);
  
  osd->cmap = XCreateColormap(osd->display, osd->window, 
                              osd->visual, AllocNone);

  XSelectInput (osd->display, osd->window, ExposureMask);
  
  osd->clean = 0;
  x11osd_clear(osd);
  osd->mapped = 0;
  x11osd_expose(osd);
}

static int x11_error = False ;

static int x11_error_handler(Display *dpy, XErrorEvent *error)
{
  x11_error = True;
  return 0;
}

x11osd *
x11osd_create (xine_t *xine, Display *display, int screen, Window window)
{
  x11osd *osd;
  int event_basep, error_basep;
  XErrorHandler   old_handler = NULL;
  XSetWindowAttributes  attr;

  osd = xine_xmalloc (sizeof (x11osd));
  if (!osd)
    return NULL;

  osd->xine = xine;
  osd->display = display;
  osd->screen = screen;
  osd->parent_window = window;

  if (!XShapeQueryExtension (osd->display, &event_basep, &error_basep)) {
    xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: XShape extension not available. unscaled overlay disabled.\n"));
    goto error2;
  }

  x11_error = False;
  old_handler = XSetErrorHandler(x11_error_handler);

  osd->visual = DefaultVisual (osd->display, osd->screen);
  osd->depth = DefaultDepth (osd->display, osd->screen);
  osd->width = XDisplayWidth (osd->display, osd->screen);
  osd->height = XDisplayHeight (osd->display, osd->screen);         

  attr.override_redirect = True;
  attr.background_pixel  = BlackPixel (osd->display, osd->screen);
  osd->window = XCreateWindow(osd->display, osd->parent_window,
                              0, 0, osd->width, osd->height, 0, 
                              CopyFromParent, CopyFromParent, CopyFromParent, 
                              CWBackPixel | CWOverrideRedirect, &attr);

  XSync(osd->display, False);
  if( x11_error ) {
    xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: error creating window. unscaled overlay disabled.\n"));
    goto error3;
  }

  osd->mask_bitmap =
    XCreatePixmap (osd->display, osd->window, osd->width, 
                   osd->height, 1);
  XSync(osd->display, False);
  if( x11_error ) {
    xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: error creating pixmap. unscaled overlay disabled.\n"));
    goto error4;
  }

  osd->bitmap =
    XCreatePixmap (osd->display, osd->window, osd->width, 
                   osd->height, osd->depth);
  XSync(osd->display, False);
  if( x11_error ) {
    xprintf(osd->xine, XINE_VERBOSITY_LOG, _("x11osd: error creating pixmap. unscaled overlay disabled.\n"));
    goto error5;
  }

  osd->gc = XCreateGC (osd->display, osd->window, 0, NULL);
  osd->mask_gc = XCreateGC (osd->display, osd->mask_bitmap, 0, NULL);
  osd->mask_gc_back = XCreateGC (osd->display, osd->mask_bitmap, 0, NULL);

  XSetForeground (osd->display, osd->mask_gc_back,
		  BlackPixel (osd->display, osd->screen));
  XSetBackground (osd->display, osd->mask_gc_back,
		  WhitePixel (osd->display, osd->screen));

  XSetForeground (osd->display, osd->mask_gc,
		  WhitePixel (osd->display, osd->screen));
  XSetBackground (osd->display, osd->mask_gc,
		  BlackPixel (osd->display, osd->screen));

  osd->cmap = XCreateColormap(osd->display, osd->window, 
                              osd->visual, AllocNone);

  XSelectInput (osd->display, osd->window, ExposureMask);

  osd->clean = 0;
  x11osd_clear(osd);
  osd->mapped = 0;
  x11osd_expose(osd);

  XSetErrorHandler(old_handler);

  return osd;

/*
  XFreeGC (osd->display, osd->gc);
  XFreeGC (osd->display, osd->mask_gc);
  XFreeGC (osd->display, osd->mask_gc_back);
*/

error5:
  XFreePixmap (osd->display, osd->bitmap);
error4:
  XFreePixmap (osd->display, osd->mask_bitmap);
error3:
  XDestroyWindow (osd->display, osd->window);
  XSetErrorHandler(old_handler);
error2:
  free (osd);
  return NULL;
}

void
x11osd_destroy (x11osd * osd)
{

  assert (osd);

  XFreeGC (osd->display, osd->gc);
  XFreeGC (osd->display, osd->mask_gc);
  XFreeGC (osd->display, osd->mask_gc_back);
  XFreePixmap (osd->display, osd->bitmap);
  XFreePixmap (osd->display, osd->mask_bitmap);
  XFreeColormap (osd->display, osd->cmap);
  XDestroyWindow (osd->display, osd->window);

  free (osd);
}

void x11osd_clear(x11osd *osd)
{
  if( !osd->clean )
    XFillRectangle (osd->display, osd->mask_bitmap, osd->mask_gc_back,
                    0, 0, osd->width, osd->height);
  osd->clean = 1;
}

#define TRANSPARENT 0xffffffff

#define saturate(n, l, u) ((n) < (l) ? (l) : ((n) > (u) ? (u) : (n)))

void x11osd_blend(x11osd *osd, vo_overlay_t *overlay)
{
  if (overlay->rle) {
    int i, x, y, len, width;
    int use_clip_palette, max_palette_colour[2];
    uint32_t palette[2][OVL_PALETTE_SIZE];

    max_palette_colour[0] = -1;
    max_palette_colour[1] = -1;

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
              if (1) {
                XColor xcolor;
                int y, u, v, r, g, b;

                y = saturate(src_clut[j].y, 16, 235);
                u = saturate(src_clut[j].cb, 16, 240);
                v = saturate(src_clut[j].cr, 16, 240);
                y = (9 * y) / 8;
                r = y + (25 * v) / 16 - 218;
                xcolor.red = (65536 * saturate(r, 0, 255)) / 256;
                g = y + (-13 * v) / 16 + (-25 * u) / 64 + 136;
                xcolor.green = (65536 * saturate(g, 0, 255)) / 256;
                b = y + 2 * u - 274;
                xcolor.blue = (65536 * saturate(b, 0, 255)) / 256;
                
                xcolor.flags = DoRed | DoBlue | DoGreen;

                XAllocColor(osd->display, osd->cmap, &xcolor);

                palette[use_clip_palette][j] = xcolor.pixel;
              }
              else {
                if (src_clut[j].y > 127) {
                  palette[use_clip_palette][j] = WhitePixel(osd->display, osd->screen);
                }
                else {
                  palette[use_clip_palette][j] = BlackPixel(osd->display, osd->screen);
                }
              }
            }
            else {
              palette[use_clip_palette][j] = TRANSPARENT;
            }
          }
          max_palette_colour[use_clip_palette] = overlay->rle[i].color;
        }

        if(palette[use_clip_palette][overlay->rle[i].color] != TRANSPARENT) {
          XSetForeground(osd->display, osd->gc, palette[use_clip_palette][overlay->rle[i].color]);
          XFillRectangle(osd->display, osd->bitmap, osd->gc, overlay->x + x, overlay->y + y, width, 1);
          XFillRectangle(osd->display, osd->mask_bitmap, osd->mask_gc, overlay->x + x, overlay->y + y, width, 1);
        }

        x += width;
        if (x == overlay->width) {
          x = 0;
          y++;
        }
      }
    }
  }
  osd->clean = 0;
}

