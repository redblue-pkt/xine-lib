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
 * OSD stuff (text and graphic primitives)
 */

#define __OSD_C__

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

#include "events.h"
#include "video_overlay.h"
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

/*
   initialize the osd rendering engine
*/

osd_renderer_t *osd_renderer_init( video_overlay_instance_t *video_overlay )
{
  osd_renderer_t *this;

  this = xine_xmalloc(sizeof(osd_renderer_t)); 
  this->video_overlay = video_overlay;
  this->event.object.overlay = xine_xmalloc( sizeof(vo_overlay_t) );

  pthread_mutex_init (&this->osd_mutex, NULL);

#ifdef LOG_DEBUG  
  printf("osd_renderer_init %p\n", this);
#endif
  
  return this;
}

void osd_renderer_exit( osd_renderer_t *this )
{

  while( this->osds )
    osd_close( this->osds );

  while( this->fonts )
    osd_renderer_unload_font( this, this->fonts->name );

  free(this);
}


/*
   open a new osd object. this will allocated an empty (all zero) drawing
   area where graphic primitives may be used.
   It is ok to specify big width and height values. The render will keep
   track of the smallest changed area to not generate too big overlays.
   A default palette is initialized (i sugest keeping color 0 as transparent
   for the sake of simplicity)
*/

osd_object_t *osd_open(osd_renderer_t *this, int width, int height)
{     
  osd_object_t *osd;
  
  static clut_t default_color[] = {
  CLUT_Y_CR_CB_INIT(0x00, 0x00, 0x00),
  CLUT_Y_CR_CB_INIT(0x80, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xff, 0x90, 0x00),
  CLUT_Y_CR_CB_INIT(0xff, 0x80, 0x80)
  };
  static uint8_t default_trans[] = {0, 7, 15, 15};

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

  memcpy(osd->color, default_color, sizeof(default_color)); 
  memcpy(osd->trans, default_trans, sizeof(default_trans)); 

  osd->handle = -1;
    
  pthread_mutex_unlock (&this->osd_mutex);  

#ifdef LOG_DEBUG  
  printf("osd_open %p [%dx%d]\n", osd, width, height);
#endif
  
  return osd;
}


/*
   free osd object
*/

void osd_close(osd_object_t *osd_to_close)
{     
  osd_renderer_t *this = osd_to_close->renderer;
  osd_object_t *osd, *last;

  if( osd_to_close->handle >= 0 )
    osd_hide(osd_to_close,0);
  
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


/*
   send the osd to be displayed at given pts (0=now)
   the object is not changed. there may be subsequent drawing  on it.
*/
int osd_show(osd_object_t *osd, uint32_t vpts )
{     
  osd_renderer_t *this = osd->renderer;
  rle_elem_t rle, *rle_p=0;
  int x, y, spare;
  uint8_t *c;

#ifdef LOG_DEBUG  
  printf("osd_show %p vpts=%d\n", osd, vpts);
#endif
      
  if( osd->handle >= 0 )
    return 0;   /* must hide first */
      
  if( (osd->handle = this->video_overlay->get_handle(this->video_overlay,0)) == -1 )
    return 0;
  
  pthread_mutex_lock (&this->osd_mutex);  
  
  /* check if osd is valid (something draw on it) */
  if( osd->x2 >= osd->x1 ) {
 
    this->event.object.handle = osd->handle;
    /* not really needed this, but good pratice to clean it up */
    memset( this->event.object.overlay, 0, sizeof(this->event.object.overlay) );
    this->event.object.overlay->x = osd->display_x + osd->x1;
    this->event.object.overlay->y = osd->display_y + osd->y1;
    this->event.object.overlay->width = osd->x2 - osd->x1 + 1;
    this->event.object.overlay->height = osd->y2 - osd->y1 + 1;
 
    this->event.object.overlay->clip_top    = 0;
    this->event.object.overlay->clip_bottom = this->event.object.overlay->height - 1;
    this->event.object.overlay->clip_left   = 0;
    this->event.object.overlay->clip_right  = this->event.object.overlay->width - 1;
    
    spare = osd->y2 - osd->y1;
    
    this->event.object.overlay->data_size = 1024;
    rle_p = this->event.object.overlay->rle = 
       malloc(this->event.object.overlay->data_size * sizeof(rle_elem_t) );
    
    for( y = osd->y1; y <= osd->y2; y++ ) {
      rle.len = 0;
      c = osd->area + y * osd->width + osd->x1;
      for( x = osd->x1; x <= osd->x2; x++, c++ ) {
        if( rle.color != *c ) {
          if( rle.len ) {
            if( (this->event.object.overlay->num_rle + spare) * sizeof(rle_elem_t) > 
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
  
    memcpy(this->event.object.overlay->color, osd->color, sizeof(osd->color)); 
    memcpy(this->event.object.overlay->trans, osd->trans, sizeof(osd->trans)); 
  
    this->event.event_type = EVENT_SHOW_SPU;
    this->event.vpts = vpts;
    this->video_overlay->add_event(this->video_overlay,(void *)&this->event);
  }
  pthread_mutex_unlock (&this->osd_mutex);  
  
  return 1;
}

/*
   send event to hide osd at given pts (0=now)
   the object is not changed. there may be subsequent drawing  on it.
*/
int osd_hide(osd_object_t *osd, uint32_t vpts )
{     
  osd_renderer_t *this = osd->renderer;
  
#ifdef LOG_DEBUG  
  printf("osd_hide %p vpts=%d\n",osd, vpts);
#endif
    
  if( osd->handle < 0 )
    return 0;
      
  pthread_mutex_lock (&this->osd_mutex);  
  
  this->event.object.handle = osd->handle;
  osd->handle = -1; /* handle will be freed after hide */
  
  /* not really needed this, but good pratice to clean it up */
  memset( this->event.object.overlay, 0, sizeof(this->event.object.overlay) );
   
  this->event.event_type = EVENT_HIDE_SPU;
  this->event.vpts = vpts;
  this->video_overlay->add_event(this->video_overlay,(void *)&this->event);

  pthread_mutex_unlock (&this->osd_mutex);  
  
  return 1;
}

/*
   Bresenham line implementation on osd object
*/

void osd_line(osd_object_t *osd,
                int x1, int y1, int x2, int y2, int color )
{     
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
   filled retangle
*/

void osd_filled_rect(osd_object_t *osd,
                int x1, int y1, int x2, int y2, int color )
{
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
   set palette (color and transparency)
*/

void osd_set_palette(osd_object_t *osd, uint32_t *color, uint8_t *trans )
{
  memcpy(osd->color, color, sizeof(osd->color));
  memcpy(osd->trans, trans, sizeof(osd->trans));
}

/*
   get palette (color and transparency)
*/

void osd_get_palette(osd_object_t *osd, uint32_t *color, uint8_t *trans )
{
  memcpy(color, osd->color, sizeof(osd->color));
  memcpy(trans, osd->trans, sizeof(osd->trans));
}

/*
   set position were overlay will be blended
*/

void osd_set_position(osd_object_t *osd, int x, int y )
{
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
   returns the internal font name to be used with other functions
   FIXME: check if font is already loaded!
*/

char * osd_renderer_load_font(osd_renderer_t *this, char *name)
{
  gzFile *fp;
  osd_font_t *font = NULL;
  char *filename;
  int i, ret = 0;
  
#ifdef LOG_DEBUG  
  printf("osd_renderer_load_font %p name=%s\n", this, name );
#endif

  filename = malloc(strlen(name)+200);
  sprintf(filename,"%s/%s.xinefont.gz",XINE_SKINDIR, name);
  
  pthread_mutex_lock (&this->osd_mutex);

  /* load quick & dirt font format */
  /* fixme: check read errors... */
  if( (fp = gzopen(filename,"rb")) != NULL ) {

    font = xine_xmalloc( sizeof(osd_font_t) );

    gzread(fp, font->name, sizeof(font->name) );
    font->version = gzread_i16(fp);
    font->num_fontchars = gzread_i16(fp);

    font->fontchar = malloc( sizeof(osd_fontchar_t) * font->num_fontchars );

#ifdef LOG_DEBUG  
    printf("font %s %d\n",font->name, font->num_fontchars);
#endif
    for( i = 0; i < font->num_fontchars; i++ ) {
      font->fontchar[i].code = gzread_i16(fp);
      font->fontchar[i].width = gzread_i16(fp);
      font->fontchar[i].height = gzread_i16(fp);
      font->fontchar[i].bmp = malloc(font->fontchar[i].width*font->fontchar[i].height);
      if( gzread(fp,font->fontchar[i].bmp, 
            font->fontchar[i].width*font->fontchar[i].height) <= 0 )
        break;
#ifdef LOG_DEBUG  
      printf("char[%d] %dx%d\n",font->fontchar[i].code,font->fontchar[i].width,font->fontchar[i].height);
#endif
    }
    
    if( i == font->num_fontchars ) {
      ret = 1;

      font->next = this->fonts;
      this->fonts = font;
    } else {
      while( --i >= 0 ) {
        free(font->fontchar[i].bmp);
      }
      free(font->fontchar);
      free(font);
    }

    gzclose(fp);
  }

  pthread_mutex_unlock (&this->osd_mutex);
  free(filename);
  
  if( ret )
    return font->name;
  else
    return NULL;
}

/*
   unload font
*/
int osd_renderer_unload_font(osd_renderer_t *this, char *fontname )
{
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

int osd_set_font( osd_object_t *osd, char *fontname )
{
  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int ret = 0;

#ifdef LOG_DEBUG  
  printf("osd_set_font %p name=%s\n", osd, fontname);
#endif
 
  pthread_mutex_lock (&this->osd_mutex);

  osd->font = NULL;

  font = this->fonts;
  while( font ) {
    if( !strcmp(font->name, fontname) ) {
      ret = 1;
      osd->font = font;
    }
    font = font->next;
  }

  pthread_mutex_unlock (&this->osd_mutex);
  return ret;
}


/*
  render text on x,y position (8 bits version)
  no \n yet
*/
int osd_render_text( osd_object_t *osd, int x1, int y1, char *text )
{
  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int i, y;
  uint8_t *dst, *src;

#ifdef LOG_DEBUG  
  printf("osd_render_text %p (%d,%d) \"%s\"\n", osd, x1, y1, text);
#endif
  
  pthread_mutex_lock (&this->osd_mutex);
  
  font = osd->font;

  if( x1 < osd->x1 ) osd->x1 = x1;
  if( y1 < osd->y1 ) osd->y1 = y1;

  while( font && *text ) {

    for( i = 0; i < font->num_fontchars; i++ ) {
      if( font->fontchar[i].code == (*text & 0xff) )
        break;
    }

#ifdef LOG_DEBUG  
    printf("font %s [%d] %dx%d -> %d,%d\n",font->name, *text, 
    font->fontchar[i].width, font->fontchar[i].height,
    x1,y1);
#endif

    if ( i != font->num_fontchars ) {
      dst = osd->area + y1 * osd->width + x1;
      src = font->fontchar[i].bmp;
      
      for( y = 0; y < font->fontchar[i].height; y++ ) {
        memcpy( dst, src, font->fontchar[i].width );
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
int osd_get_text_size( osd_object_t *osd, char *text, int *width, int *height )
{
  osd_renderer_t *this = osd->renderer;
  osd_font_t *font;
  int i;

#ifdef LOG_DEBUG  
  printf("osd_get_text_size %p \"%s\"\n", osd, text);
#endif
  
  pthread_mutex_lock (&this->osd_mutex);

  font = osd->font;
  
  *width = 0;
  *height = 0;  
  
  while( *text ) {

    for( i = 0; i < font->num_fontchars; i++ ) {
      if( font->fontchar[i].code == *text )
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

