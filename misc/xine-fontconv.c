/*
 * Copyright (C) 2001 the xine project
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
 *
 * xine-fontconv.c  
 *
 * converts ttf fonts to xine osd fonts
 *
 * compile:
 *   gcc -o xine-fontconv xine-fontconv.c -lfreetype -lz -I/usr/include/freetype2
 *
 * usage:
 *   xine-fontconv font.ttf fontname
 *
 * begin                : Sat Dec 1 2001
 * copyright            : (C) 2001 by miguel
 * email                : miguel@mf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifndef OLD_FREETYPE2
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#else                   /* freetype 2.0.1 */
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#endif

#define f266ToInt(x)            (((x)+32)>>6)   /* round fractional fixed point */
                                                /* coordinates are in 26.6 pixels (i.e. 1/64th of pixels)*/
#define f266CeilToInt(x)        (((x)+63)>>6)   /* ceiling */
#define f266FloorToInt(x)       ((x)>>6)        /* floor */

/* xine stuff */
typedef struct osd_fontchar_s osd_fontchar_t;
typedef struct osd_font_s     osd_font_t;
typedef unsigned short        uint16_t;
typedef unsigned char         uint8_t;

struct osd_fontchar_s {
  uint16_t code;
  uint16_t width;
  uint16_t height;
  uint8_t *bmp;
};

struct osd_font_s {
  char             name[40];
  uint16_t         version;
  uint16_t         size;
  uint16_t         num_fontchars;
  osd_fontchar_t  *fontchar;
  osd_font_t      *next;
}; 

osd_fontchar_t fontchar;
osd_font_t     font;

void print_bitmap (FT_Bitmap *bitmap) {

  int x,y;
  
  for( y = 0; y < bitmap->rows; y++ ) {
    for( x = 0; x < bitmap->width; x++ ) {
      if( bitmap->buffer[y*bitmap->width+x] )
        printf("%02x ", bitmap->buffer[y*bitmap->width+x] );
      else
        printf("   ");
    }
    printf("\n");
  }
}

FT_Bitmap *create_bitmap (int width, int height) {
  FT_Bitmap * bitmap;
  bitmap = malloc( sizeof( FT_Bitmap ) );
  bitmap->rows = height;
  bitmap->width = width;
  bitmap->buffer = malloc(width*height);
  memset( bitmap->buffer, 0, width*height );
  
  return bitmap;
}

void destroy_bitmap (FT_Bitmap * bitmap) {
  free(bitmap->buffer);
  free(bitmap);
}
 

void add_final_bitmap (FT_Bitmap *dst, FT_Bitmap *src, int left, int top) {

  int x,y;
  
  for (y = 0; y < src->rows; y++) {
    for ( x = 0; x < src->width; x++) {

      int x1, y1;
      int dstpos, srcpos;
      int x2, y2;


      x1 = x + left;
      if (x1 < 0 || x1 >= dst->width)
        continue;
        
      y1 = y + top;
      if (y1 < 0 || y1 >= dst->rows)
        continue;

      dstpos = y1 * dst->width + x1;

      x2 = x; y2 = y;

      srcpos = y2 * src->width + x2;
      if (src->buffer[srcpos])
        dst->buffer[dstpos] = 3;
      else {
	int count;
	int i,j;
	count = 0;
	
	for (i=-1; i<2; i++) {
	  for (j=-1; j<2; j++) {

	    x2 = x+i; y2 = y+j;
	
	    if ( (x2 >0 ) && (y2 > 0) 
		 &&(x2 < src->width) 
		 && (y2 < src->rows)) {
	      srcpos = y2 * src->width + x2;
	      if (src->buffer[srcpos])
		count++;
	    }
	  }
	}
	if (count)
	  dst->buffer[dstpos] = 2;
	  
      }
    }
  }


  
  for (y = 0; y < dst->rows; y++) {
    for (x = 0; x < dst->width; x++) {
      int dstpos = y * dst->width + x;
      if (dst->buffer[dstpos]<2)
	dst->buffer[dstpos]=1;
    }
  }
  
  
}

void render_font (FT_Face face, char *fontname, int size) {

  char                filename[1024];
  FT_Bitmap          *out_bitmap;
  gzFile             *fp;
  int                 error;
  int                 glyph_index;
  FT_Glyph            glyph;
  FT_BitmapGlyph      glyph_bitmap;
  FT_Vector           origin;
  int                 max_bearing_y = 0;        
  int                 c;
  int                 thickness;

  /* 
   * generate filename, open file
   */
   
  sprintf (filename, "%s-%d.xinefont.gz", fontname, size);

  fp = gzopen(filename,"w");
 
  if (!fp) {
    printf ("error opening output file %s\n", filename);
    return;
  }

  /* 
   * set up font
   */

  strcpy(font.name, fontname);
  font.version       = 1;
  font.num_fontchars = 0;
  font.size          = size;

  error = FT_Set_Pixel_Sizes( face,     /* handle to face object */
                              0,        /* pixel_width           */
                              size );   /* pixel_height          */
                              
  if (error) {
    printf("error setting size\n");
    return;
  }

  thickness = 64;

  /* 
   * calc max bearing 
   */

  for (c = 32; c < 256; c++) {
    glyph_index = FT_Get_Char_Index( face, c );
  
    if (!glyph_index)
      continue;
      
    error = FT_Load_Glyph (face,               /* handle to face object */
                           glyph_index,        /* glyph index           */
                           FT_LOAD_DEFAULT );  /* load flags            */
                         
    if (error) {
      continue;
    }

    if( (face->glyph->metrics.horiBearingY >> 6) > max_bearing_y )
      max_bearing_y = (face->glyph->metrics.horiBearingY >> 6);
    font.num_fontchars++;
  }

  printf("max_bearing_y: %d\n", max_bearing_y + f266CeilToInt(thickness));

  gzwrite (fp, &font, 40+6);
 
  for (c = 32; c < 256; c++) {

    glyph_index = FT_Get_Char_Index( face, c );
    
    if (glyph_index) {
      
      error = FT_Load_Glyph( face,          /* handle to face object */
			     glyph_index,   /* glyph index           */
			     FT_LOAD_DEFAULT );  /* load flags */
      
      if (!error) {
	
	error = FT_Get_Glyph( face->glyph, &glyph );

	
	out_bitmap = create_bitmap( f266CeilToInt(face->glyph->metrics.horiAdvance + thickness),
				    f266CeilToInt((max_bearing_y<<6) - face->glyph->metrics.horiBearingY + 
						  face->glyph->metrics.height + thickness) );
	
	origin.x = thickness;
	origin.y = thickness;
	
	error = FT_Glyph_To_Bitmap( &glyph, ft_render_mode_normal, &origin, 1 );  
	if (error) {
	  printf("error generating bitmap [%d]\n",c);
	  return;
	}
	
	glyph_bitmap = (FT_BitmapGlyph)glyph;
	
	
	add_final_bitmap (out_bitmap, &glyph_bitmap->bitmap, glyph_bitmap->left,
			  max_bearing_y - glyph_bitmap->top);
	
	FT_Done_Glyph( glyph );
      }
    }
    
    printf("[%d] bitmap width: %d height: %d\n", c, out_bitmap->width, out_bitmap->rows );
  
    fontchar.code = c;
    fontchar.width = out_bitmap->width;
    fontchar.height = out_bitmap->rows;
    gzwrite (fp, &fontchar,6);
    gzwrite (fp, out_bitmap->buffer, out_bitmap->width*out_bitmap->rows);
  }
  gzclose(fp);

  printf ("generated %s\n", filename);
}  

int main(int argc, char *argv[]) {

  int                 error;
  int                 len;
  FT_Library          library;
  FT_Face             face;

  /*
   * command line parsing
   */

  if (argc!=3) {
    printf ("usage:%s font.ttf fontname\n", argv[0]);
    exit (1);
  }
  
  len = strlen (argv[1]);

  if (strncasecmp (&argv[1][len-4],".ttf",3)) {
    printf ("font name must have .ttf suffix (is %s)\n", &argv[1][len-4]);
    exit (1);
  }

  error = FT_Init_FreeType( &library );
  if( error ) {
    printf("error initializing freetype\n");
  }
  
  error = FT_New_Face( library, 
                       argv[1],
                       0,
                       &face );
  if (error) {
    printf("error loading font\n");
    return 1;
  }

  render_font (face, argv[2], 16);
  render_font (face, argv[2], 20);
  render_font (face, argv[2], 24);
  render_font (face, argv[2], 32);

  /*
   * some rgb -> yuv conversion,
   * can be used to calc new palettes
   */
  
  { 
    float f;
    for (f=1.0; f<6.0; f+=1.0) {

      float R=f*40.0;
      float G=f*40.0;
      float B=f*42.0;
      float Y, Cb, Cr;

      Y  =  0.29900 * R + 0.58700 * G + 0.11400 * B ;
      Cb = -0.16874 * R - 0.33126 * G + 0.50000 * B  + 128.0;
      Cr =  0.50000 * R - 0.41869 * G - 0.08131 * B  + 128.0;
      
      printf ("CLUT_Y_CR_CB_INIT(0x%x, 0x%x, 0x%x),\n", (int) Y, (int) Cr, (int) Cb);
    }
  }

  return 0;
}


