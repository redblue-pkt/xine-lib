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
 * OSD stuff (text and graphic primitives)
 */

#define __OSD_C__
 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>
#include <sys/types.h>
#include <dirent.h>

#include "xine_internal.h"
#include "video_out/alphablend.h"
#include "xine-engine/bswap.h"
#include "xineutils.h"
#include "video_out.h"
#include "osd.h"

/*
#define LOG_DEBUG 1
*/

#ifdef MAX
#undef MAX
#endif
#define MAX(a,b) ( (a) > (b) ) ? (a) : (b)

#ifdef MIN
#undef MIN
#endif
#define MIN(a,b) ( (a) < (b) ) ? (a) : (b)

typedef struct osd_fontchar_s {
  uint16_t code;
  uint16_t width;
  uint16_t height;
  uint8_t *bmp;
} osd_fontchar_t;

struct osd_font_s {
  char             name[40];
  uint16_t         version;
  uint16_t         size;
  uint16_t         num_fontchars;
  osd_fontchar_t  *fontchar;
  osd_font_t      *next;
}; 

/*
 * open a new osd object. this will allocated an empty (all zero) drawing
 * area where graphic primitives may be used.
 * It is ok to specify big width and height values. The render will keep
 * track of the smallest changed area to not generate too big overlays.
 * A default palette is initialized (i sugest keeping color 0 as transparent
 * for the sake of simplicity)
 */

static osd_object_t *osd_new_object (osd_renderer_t *this, int width, int height) {
     
  osd_object_t *osd;
  
  pthread_mutex_lock (&this->osd_mutex);  
  
  osd = xine_xmalloc( sizeof(osd_object_t) );
  osd->renderer = this;
  osd->next = this->osds;
  this->osds = osd;
  
  osd->width = width;
  osd->height = height;
  osd->area = xine_xmalloc( width * height );
  
  osd->x1 = width;
  osd->y1 = height;
  osd->x2 = 0;
  osd->y2 = 0;

  memcpy(osd->color, textpalettes_color[0], sizeof(textpalettes_color[0])); 
  memcpy(osd->trans, textpalettes_trans[0], sizeof(textpalettes_trans[0])); 

  osd->handle = -1;
    
  pthread_mutex_unlock (&this->osd_mutex);  

#ifdef LOG_DEBUG  
  printf("osd_open %p [%dx%d]\n", osd, width, height);
#endif
  
  return osd;
}



/*
 * send the osd to be displayed at given pts (0=now)
 * the object is not changed. there may be subsequent drawing  on it.
 */
static int osd_show (osd_object_t *osd, int64_t vpts ) {
     
  osd_renderer_t *this = osd->renderer;
  rle_elem_t rle, *rle_p=0;
  int x, y, spare;
  uint8_t *c;

#ifdef LOG_DEBUG  
  printf("osd_show %p vpts=%lld\n", osd, vpts);
#endif
      
  if( osd->handle < 0 ) {
    if( (osd->handle = this->video_overlay->get_handle(this->video_overlay,0)) == -1 ) {
      return 0;
    }
  }
  
  pthread_mutex_lock (&this->osd_mutex);  
  
  /* check if osd is valid (something drawn on it) */
  if( osd->x2 >= osd->x1 ) {
 
    this->event.object.handle = osd->handle;

    memset( this->event.object.overlay, 0, sizeof(*this->event.object.overlay) );
    this->event.object.overlay->x = osd->display_x + osd->x1;
    this->event.object.overlay->y = osd->display_y + osd->y1;
    this->event.object.overlay->width = osd->x2 - osd->x1 + 1;
    this->event.object.overlay->height = osd->y2 - osd->y1 + 1;
 
    this->event.object.overlay->clip_top    = -1;
    this->event.object.overlay->clip_bottom = this->event.object.overlay->height +
                                              osd->display_x;
    this->event.object.overlay->clip_left   = 0;
    this->event.object.overlay->clip_right  = this->event.object.overlay->width +
                                              osd->display_y;
   
    spare = osd->y2 - osd->y1;
    this->event.object.overlay->num_rle = 0;
    this->event.object.overlay->data_size = 1024;
    rle_p = this->event.object.overlay->rle = 
       malloc(this->event.object.overlay->data_size * sizeof(rle_elem_t) );
    
    for( y = osd->y1; y <= osd->y2; y++ ) {
      rle.len = 0;
      c = osd->area + y * osd->width + osd->x1;                                       
      for( x = osd->x1; x <= osd->x2; x++, c++ ) {
        if( rle.color != *c ) {
          if( rle.len ) {
            if( (this->event.object.overlay->num_rle + spare) > 
                this->event.object.overlay->data_size ) {
                this->event.object.overlay->data_size += 1024;
                rle_p = this->event.object.overlay->rle = 
                  realloc( this->event.object.overlay->rle,
                           this->event.object.overlay->data_size * sizeof(rle_elem_t) );
                rle_p += this->event.object.overlay->num_rle;
            }
            *rle_p++ = rle;
            this->event.object.overlay->num_rle++;            
          }
          rle.color = *c;
          rle.len = 1;
        } else {
          rle.len++;
        }  
      }
      *rle_p++ = rle;
      this->event.object.overlay->num_rle++;            
    }
  
#ifdef LOG_DEBUG  
    printf("osd_show num_rle = %d\n", this->event.object.overlay->num_rle);
#endif
  
    memcpy(this->event.object.overlay->clip_color, osd->color, sizeof(osd->color)); 
    memcpy(this->event.object.overlay->clip_trans, osd->trans, sizeof(osd->trans)); 
  
    this->event.event_type = OVERLAY_EVENT_SHOW;
    this->event.vpts = vpts;
    this->video_overlay->add_event(this->video_overlay,(void *)&this->event);
  }
  pthread_mutex_unlock (&this->osd_mutex);  
  
  return 1;
}

/*
 * send event to hide osd at given pts (0=now)
 * the object is not changed. there may be subsequent drawing  on it.
 */
static int osd_hide (osd_object_t *osd, int64_t vpts) {     

  osd_renderer_t *this = osd->renderer;
  
#ifdef LOG_DEBUG  
  printf("osd_hide %p vpts=%lld\n",osd, vpts);
#endif
    
  if( osd->handle < 0 )
    return 0;
      
  pthread_mutex_lock (&this->osd_mutex);  
  
  this->event.object.handle = osd->handle;
  
  /* not really needed this, but good pratice to clean it up */
  memset( this->event.object.overlay, 0, sizeof(this->event.object.overlay) );
   
  this->event.event_type = OVERLAY_EVENT_HIDE;
  this->event.vpts = vpts;
  this->video_overlay->add_event(this->video_overlay,(void *)&this->event);

  pthread_mutex_unlock (&this->osd_mutex);  
  
  return 1;
}


/*
 * clear an osd object, so that it can be used for rendering a new image
 */

static void osd_clear (osd_object_t *osd) {
#ifdef LOG_DEBUG
  printf("osd_clear\n");
#endif

  memset(osd->area, 0, osd->width * osd->height);
  osd->x1 = osd->width;
  osd->y1 = osd->height;
  osd->x2 = 0;
  osd->y2 = 0;
}


/*
 * Bresenham line implementation on osd object
 */

static void osd_line (osd_object_t *osd,
		      int x1, int y1, int x2, int y2, int color) {
     
  uint8_t *c;
  int dx, dy, t, inc, d, inc1, inc2;

#ifdef LOG_DEBUG  
  printf("osd_line %p (%d,%d)-(%d,%d)\n",osd, x1,y1, x2,y2 );
#endif
     
  /* update clipping area */
  t = MIN( x1, x2 );
  osd->x1 = MIN( osd->x1, t );
  t = MAX( x1, x2 );
  osd->x2 = MAX( osd->x2, t );
  t = MIN( y1, y2 );
  osd->y1 = MIN( osd->y1, t );
  t = MAX( y1, y2 );
  osd->y2 = MAX( osd->y2, t );

  dx = abs(x1-x2);
  dy = abs(y1-y2);

  if( dx>=dy ) {
    if( x1>x2 )
    {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }
  
    if( y2 > y1 ) inc = 1; else inc = -1;

    inc1 = 2*dy;
    d = inc1 - dx;
    inc2 = 2*(dy-dx);

    c = osd->area + y1 * osd->width + x1;
    
    while(x1<x2)
    {
      *c++ = color;
      x1++;
      if( d<0 ) {
        d+=inc1;
      } else {
        y1+=inc;
        d+=inc2;
        c = osd->area + y1 * osd->width + x1;
      }
    }
  } else {
    if( y1>y2 ) {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }

    if( x2 > x1 ) inc = 1; else inc = -1;

    inc1 = 2*dx;
    d = inc1-dy;
    inc2 = 2*(dx-dy);

    c = osd->area + y1 * osd->width + x1;

    while(y1<y2) {
      *c = color;
      c += osd->width;
      y1++;
      if( d<0 ) {
        d+=inc1;
      } else {
        x1+=inc;
        d+=inc2;
        c = osd->area + y1 * osd->width + x1;
      }
    }
  }
}


/*
 * filled retangle
 */

static void osd_filled_rect (osd_object_t *osd,
			     int x1, int y1, int x2, int y2, int color) {

  int x, y, dx, dy;

#ifdef LOG_DEBUG  
  printf("osd_filled_rect %p (%d,%d)-(%d,%d)\n",osd, x1,y1, x2,y2 );
#endif

  /* update clipping area */
  x = MIN( x1, x2 );
  osd->x1 = MIN( osd->x1, x );
  dx = MAX( x1, x2 );
  osd->x2 = MAX( osd->x2, dx );
  y = MIN( y1, y2 );
  osd->y1 = MIN( osd->y1, y );
  dy = MAX( y1, y2 );
  osd->y2 = MAX( osd->y2, dy );

  dx -= x;
  dy -= y;

  for( ; dy--; y++ ) {
    memset(osd->area + y * osd->width + x,color,dx);
  }
}

/*
 * set palette (color and transparency)
 */

static void osd_set_palette(osd_object_t *osd, const uint32_t *color, const uint8_t *trans ) {

  memcpy(osd->color, color, sizeof(osd->color));
  memcpy(osd->trans, trans, sizeof(osd->trans));
}

/*
 * set on existing text palette 
 * (-1 to set user specified palette)
 */

static void osd_set_text_palette(osd_object_t *osd, int palette_number,
				 int color_base) {

  if( palette_number < 0 )
    palette_number = osd->renderer->textpalette;

  /* some sanity checks for the color indices */
  if( color_base < 0 )
    color_base = 0;
  else if( color_base > OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE )
    color_base = OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE;

  memcpy(&osd->color[color_base], textpalettes_color[palette_number],
	 sizeof(textpalettes_color[palette_number]));
  memcpy(&osd->trans[color_base], textpalettes_trans[palette_number],
	 sizeof(textpalettes_trans[palette_number]));    
}


/*
 * get palette (color and transparency)
 */

static void osd_get_palette (osd_object_t *osd, uint32_t *color, uint8_t *trans) {

  memcpy(color, osd->color, sizeof(osd->color));
  memcpy(trans, osd->trans, sizeof(osd->trans));
}

/*
 * set position were overlay will be blended
 */

static void osd_set_position (osd_object_t *osd, int x, int y) {

  osd->display_x = x;
  osd->display_y = y;
}

static uint16_t gzread_i16(gzFile *fp) {
  uint16_t ret;
  ret = gzgetc(fp);
  ret |= (gzgetc(fp)<<8);
  return ret;
}

/*
   load bitmap font into osd engine 
*/

static void osd_renderer_load_font(osd_renderer_t *this, char *filename) {

  gzFile      *fp;
  osd_font_t  *font = NULL;
  int          i, ret = 0;
  
#ifdef LOG_DEBUG  
  printf("osd: renderer_load_font %p name=%s\n", this, filename );
#endif

  pthread_mutex_lock (&this->osd_mutex);

  /* load quick & dirt font format */
  /* fixme: check read errors... */
  if( (fp = gzopen(filename,"rb")) != NULL ) {

    font = xine_xmalloc( sizeof(osd_font_t) );

    gzread(fp, font->name, sizeof(font->name) );
    font->version = gzread_i16(fp);
    font->size = gzread_i16(fp);
    font->num_fontchars = gzread_i16(fp);

    font->fontchar = malloc( sizeof(osd_fontchar_t) * font->num_fontchars );

#ifdef LOG_DEBUG  
    printf("osd: font %s %d\n",font->name, font->num_fontchars);
#endif
    for( i = 0; i < font->num_fontchars; i++ ) {
      font->fontchar[i].code = gzread_i16(fp);
      font->fontchar[i].width = gzread_i16(fp);
      font->fontchar[i].height = gzread_i16(fp);
      font->fontchar[i].bmp = malloc(font->fontchar[i].width*font->fontchar[i].height);
      if( gzread(fp,font->fontchar[i].bmp, 
            font->fontchar[i].width*font->fontchar[i].height) <= 0 )
        break;
    }
    
    if( i == font->num_fontchars ) {
      ret = 1;

#ifdef LOG_DEBUG  
    printf("osd: font %s loading ok\n",font->name);
#endif

      font->next = this->fonts;
      this->fonts = font;
    } else {

#ifdef LOG_DEBUG  
      printf("osd: font %s loading failed (%d < %d)\n",font->name,
	     i, font->num_fontchars);
#endif

      while( --i >= 0 ) {
        free(font->fontchar[i].bmp);
      }
      free(font->fontchar);
      free(font);
    }

    gzclose(fp);
  }

  pthread_mutex_unlock (&this->osd_mutex);
}

/*
 * unload font
 */
static int osd_renderer_unload_font(osd_renderer_t *this, char *fontname ) {

  osd_font_t *font, *last;
  osd_object_t *osd;
  int i, ret = 0;
  
#ifdef LOG_DEBUG  
  printf("osd_renderer_unload_font %p name=%s\n", this, fontname);
#endif

  pthread_mutex_lock (&this->osd_mutex);

  osd = this->osds;
  while( osd ) {  
    if( !strcmp(osd->font->name, fontname) )
      osd->font = NULL;
    osd = osd->next;
  }

  last = NULL;
  font = this->fonts;
  while( font ) {
    if ( !strcmp(font->name,fontname) ) {

      for( i = 0; i < font->num_fontchars; i++ ) {
        free( font->fontchar[i].bmp );
      }
      free( font->fontchar );

      if( last )
        last->next = font->next;
      else
        this->fonts = font->next;
      free( font );
      break;
    }
    last = font;
    font = font->next;
  }

  pthread_mutex_unlock (&this->osd_mutex);
  return ret;
}


/*
  set the font of osd object
*/

static int osd_set_font( osd_object_t *osd, const char *fontname, int size) { 

  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int best = 0;
  int ret = 0;

#ifdef LOG_DEBUG  
  printf("osd_set_font %p name=%s\n", osd, fontname);
#endif
 
  pthread_mutex_lock (&this->osd_mutex);

  osd->font = NULL;

  font = this->fonts;
  while( font ) {

    if( !strcmp(font->name, fontname) && (size>=font->size) 
	&& (best<font->size)) {
      ret = 1;
      osd->font = font;
      best = font->size;
#ifdef LOG_DEBUG  
      printf ("osd_set_font: font->name=%s, size=%d\n", font->name, font->size);
#endif

    }
    font = font->next;
  }

  pthread_mutex_unlock (&this->osd_mutex);
  return ret;
}


/*
 * render text on x,y position (8 bits version)
 *  no \n yet
 */
static int osd_render_text (osd_object_t *osd, int x1, int y1,
	                    const char *text, int color_base) {

  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int i, y;
  uint8_t *dst, *src;
  int c;

#ifdef LOG_DEBUG  
  printf("osd_render_text %p (%d,%d) \"%s\"\n", osd, x1, y1, text);
#endif
 
  /* some sanity checks for the color indices */
  if( color_base < 0 )
    color_base = 0;
  else if( color_base > OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE )
    color_base = OVL_PALETTE_SIZE - TEXT_PALETTE_SIZE;

  pthread_mutex_lock (&this->osd_mutex);
  
  font = osd->font;

  if( x1 < osd->x1 ) osd->x1 = x1;
  if( y1 < osd->y1 ) osd->y1 = y1;

  while( font && *text ) {
    c = *text & 0xff;
    
    for( i = 0; i < font->num_fontchars; i++ ) {
      if( font->fontchar[i].code == c )
        break;
    }

#ifdef LOG_DEBUG  
    printf("font %s [%c:%d] %dx%d -> %d,%d\n",font->name, c, font->fontchar[i].code, 
    font->fontchar[i].width, font->fontchar[i].height,
    x1,y1);
#endif

    if ( i != font->num_fontchars ) {
      dst = osd->area + y1 * osd->width + x1;
      src = font->fontchar[i].bmp;
      
      for( y = 0; y < font->fontchar[i].height; y++ ) {
	int width = font->fontchar[i].width;
	uint8_t *s = src, *d = dst;
	while (s < src + width)
	  *d++ = *s++ + (uint8_t) color_base;
        src += font->fontchar[i].width;
        dst += osd->width;
      }
      x1 += font->fontchar[i].width;
    
      if( x1 > osd->x2 ) osd->x2 = x1;
      if( y1 + font->fontchar[i].height > osd->y2 ) 
        osd->y2 = y1 + font->fontchar[i].height;
    }
    text++;
  }
  
  pthread_mutex_unlock (&this->osd_mutex);

  return 1;
}

/*
  get width and height of how text will be renderized
*/
static int osd_get_text_size(osd_object_t *osd, const char *text, int *width, int *height) {

  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int i, c;

#ifdef LOG_DEBUG  
  printf("osd_get_text_size %p \"%s\"\n", osd, text);
#endif
  
  pthread_mutex_lock (&this->osd_mutex);

  font = osd->font;
  
  *width = 0;
  *height = 0;  
  
  while( font && *text ) {
    c = *text & 0xff;
  
    for( i = 0; i < font->num_fontchars; i++ ) {
      if( font->fontchar[i].code == c )
        break;
    }

    if ( i != font->num_fontchars ) {
      if( font->fontchar[i].height > *height )
        *height = font->fontchar[i].height;
      *width += font->fontchar[i].width;
    }
    text++;
  }

  pthread_mutex_unlock (&this->osd_mutex);

  return 1;
}

static void osd_load_fonts (osd_renderer_t *this, char *path) {
  DIR *dir;
  char pathname [1024];

#ifdef LOG_DEBUG
  printf ("osd: load_fonts, path=%s\n", path);
#endif

  dir = opendir (path) ;

  if (dir) {

    struct dirent *entry;

#ifdef LOG_DEBUG
    printf ("osd: load_fonts, %s opened\n", path);
#endif

    while ((entry = readdir (dir)) != NULL) {
      int len;

      len = strlen (entry->d_name);

      if ( (len>12) && !strncmp (&entry->d_name[len-12], ".xinefont.gz", 12)) {
	
#ifdef LOG_DEBUG
	printf ("osd: trying to load font >%s< (ending >%s<)\n",
		entry->d_name,&entry->d_name[len-12]);
#endif

	sprintf (pathname, "%s/%s", path, entry->d_name);
	
	osd_renderer_load_font (this, pathname);

      }
    }

    /*
     * for a reason that is still unknown this closedir breaks
     * ac3 passthrough (at least for oss).
     * Needs to be investigted further...
     * -- Heiko
     */
    /* closedir (dir); */

  }
}

/*
 * free osd object
 */

static void osd_free_object (osd_object_t *osd_to_close) {
     
  osd_renderer_t *this = osd_to_close->renderer;
  osd_object_t *osd, *last;

  if( osd_to_close->handle >= 0 ) {
    osd_hide(osd_to_close,0);
    
    this->event.object.handle = osd_to_close->handle;
  
    /* not really needed this, but good pratice to clean it up */
    memset( this->event.object.overlay, 0, sizeof(this->event.object.overlay) );
    this->event.event_type = OVERLAY_EVENT_FREE_HANDLE;
    this->event.vpts = 0;
    this->video_overlay->add_event(this->video_overlay,(void *)&this->event);
  
    osd_to_close->handle = -1; /* handle will be freed */
  }
  
  pthread_mutex_lock (&this->osd_mutex);  

  last = NULL;
  osd = this->osds;
  while( osd ) {
    if ( osd == osd_to_close ) {
      free( osd->area );
      
      if( last )
        last->next = osd->next;
      else
        this->osds = osd->next;
      free( osd );
      break;
    }
    last = osd;
    osd = osd->next;
  }
  pthread_mutex_unlock (&this->osd_mutex);  
}

static void osd_renderer_close (osd_renderer_t *this) {

  while( this->osds )
    osd_free_object ( this->osds );
  
  while( this->fonts )
    osd_renderer_unload_font( this, this->fonts->name );

  pthread_mutex_destroy (&this->osd_mutex);

  free(this->event.object.overlay);
  free(this);
}


static void update_text_palette(void *this_gen, xine_cfg_entry_t *entry)
{
  osd_renderer_t *this = (osd_renderer_t *)this_gen;

  this->textpalette = entry->num_value;
  printf("osd: text palette will be %s\n", textpalettes_str[this->textpalette] );
}


/*
 * initialize the osd rendering engine
 */

osd_renderer_t *osd_renderer_init( video_overlay_instance_t *video_overlay, config_values_t *config ) {

  osd_renderer_t *this;
  char str[1024];

  this = xine_xmalloc(sizeof(osd_renderer_t)); 
  this->video_overlay = video_overlay;
  this->config = config;
  this->event.object.overlay = xine_xmalloc( sizeof(vo_overlay_t) );

  pthread_mutex_init (&this->osd_mutex, NULL);

#ifdef LOG_DEBUG  
  printf("osd: osd_renderer_init %p\n", this);
#endif
  
  /*
   * load available fonts
   */

  osd_load_fonts (this, XINE_FONTDIR);

  sprintf (str, "%s/.xine/fonts", xine_get_homedir ());

  osd_load_fonts (this, str);

  this->textpalette = config->register_enum (config, "misc.osd_text_palette", 0,
                                             textpalettes_str, 
                                             _("Palette (foreground-border-background) to use on subtitles"),
                                             NULL, 10, update_text_palette, this);
  
  /*
   * set up function pointer
   */

  this->new_object         = osd_new_object;
  this->free_object        = osd_free_object;
  this->show               = osd_show;
  this->hide               = osd_hide;
  this->set_palette        = osd_set_palette;
  this->set_text_palette   = osd_set_text_palette;
  this->get_palette        = osd_get_palette;
  this->set_position       = osd_set_position;
  this->set_font           = osd_set_font;
  this->clear              = osd_clear;
  this->line               = osd_line;
  this->filled_rect        = osd_filled_rect;
  this->render_text        = osd_render_text;
  this->get_text_size      = osd_get_text_size;
  this->close              = osd_renderer_close;

  return this;
}
