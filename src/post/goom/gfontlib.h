#ifndef _GFONTLIB_H
#define _GFONTLIB_H

void gfont_load ();
void goom_draw_text (guint32 * buf,int resolx,int resoly,
										 int x, int y,
										 const char *str, float chspace, int center);

#endif
