/*
 * Copyright (C) 2002 the xine project
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
 * xine-logoconv.c  
 *
 * converts png (or any other file types imlib understands) to xine logos
 *
 * xine logo file format:
 *
 * int16_t    width
 * int16_t    height
 * uint8_t    zlib_compressed (yuy2_data[width*height*2])
 *
 * compile:
 *   gcc -o xine-logoconv xine-logoconv.c -L/usr/X11R6/lib -lX11 -lXext -ljpeg -lpng -ltiff -lz -lImlib
 *
 * usage:
 *   xine-logoconv logo.png
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <inttypes.h>

#include <Imlib.h>

#define LUMARED   0.299
#define LUMAGREEN 0.587
#define LUMABLUE  0.114

static void wr16 (gzFile *fp, int i) {

  uint8_t c;

  c = i >> 8;
  gzputc (fp, c);
  c = i & 0xff;
  gzputc (fp, c);
}

static void save_image (char *oldname, ImlibImage *img) {

  gzFile  *fp;
  int32_t  px, py, w, h;
  char     filename[1024];
  char    *extension;

  w = img->rgb_width;
  h = img->rgb_height;

  extension = strrchr (oldname, '.');
  if (extension)
    *extension = 0;

  snprintf (filename, 1023, "%s.zyuy2", oldname);

  if (!(fp = gzopen (filename ,"w"))) {
    printf ("failed to create file '%s'\n", filename);
    return;
  }

  printf ("saving %d x %d logo image to %s\n",
	  w, h, filename);

  wr16 (fp, w);
  wr16 (fp, h);

  /*
   * convert yuv to yuy2 
   */

  for (py=0; py<h; py++) {
    printf (".");
    for (px=0; px<w; px++) {

      double r, g, b;
      double y, u, v;
      unsigned char cy,cu,cv;

#ifdef WORDS_BIGENDIAN
      r = img->rgb_data[(px+py*w)*3];
      g = img->rgb_data[(px+py*w)*3+1];
      b = img->rgb_data[(px+py*w)*3+2];
#else
      r = img->rgb_data[(px+py*w)*3+2];
      g = img->rgb_data[(px+py*w)*3+1];
      b = img->rgb_data[(px+py*w)*3];
#endif

      y = (LUMARED*r) + (LUMAGREEN*g) + (LUMABLUE*b);
      //      u = (b-y) / (2 - 2*LUMABLUE);
      //      v = (r-y) / (2 - 2*LUMABLUE);
      u = (b-y) / (2 - 2*LUMABLUE);
      v = (r-y) / (2 - 2*LUMABLUE);

      cy = y;
      cu = u + 128.0;
      cv = v + 128.0;

      gzwrite (fp, &cy, 1);
      if ((px-1) % 2)
	gzwrite (fp, &cv, 1);
      else
	gzwrite (fp, &cu, 1);

      /*
      printf ("%f %f %f => %f %f %f\n",r,g,b,y,u,v);
      */

    }
  }

  printf ("\ndone.\n");
  gzclose(fp);
}

int main(int argc, char *argv[]) {

  Display           *display;
  ImlibData         *imlib_data;
  ImlibImage        *img;

  if (argc != 2) {
    printf ("usage: %s foo.png\n", argv[0]);
    exit (1);
  }

  if (!(display = XOpenDisplay (NULL))) {
    printf ("failed to open X11 display\n");
    exit (1);
  }
  
  if (!(imlib_data = Imlib_init(display))) {
    printf ("failed to initialize imlib\n");
    exit(1);
  }
  
  if (!(img = Imlib_load_image(imlib_data, argv[1]))) {
    printf ("failed to load '%s'\n", argv[1]);
    exit(1);
  }

  Imlib_render(imlib_data, img,
	       img->rgb_width,
	       img->rgb_height);

  save_image (argv[1], img);

}
