/*
 *  lines.c
 *  iTunesXPlugIn
 *
 *  Created by guillaum on Tue Aug 14 2001.
 *  Copyright (c) 2001 __CompanyName__. All rights reserved.
 *
 */

#include "lines.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "goom_tools.h"
#include "drawmethods.h"

extern unsigned int resolx, c_resoly;

#define DRAWMETHOD DRAWMETHOD_PLUS(*p,*p,col)

static void
draw_line (int *data, int x1, int y1, int x2, int y2, int col, int screenx,
					 int screeny)
{
	int     x, y, dx, dy, yy, xx;
	int    *p;

/*   DATA32 *p; */
/*   DATA8 aaa, nr, ng, nb, rr, gg, bb, aa, na; */

	/* clip to top edge */
	if ((y1 < 0) && (y2 < 0))
		return;
	if (y1 < 0) {
		x1 += (y1 * (x1 - x2)) / (y2 - y1);
		y1 = 0;
	}
	if (y2 < 0) {
		x2 += (y2 * (x1 - x2)) / (y2 - y1);
		y2 = 0;
	}
	/* clip to bottom edge */
	if ((y1 >= screeny) && (y2 >= screeny))
		return;
	if (y1 >= screeny) {
		x1 -= ((screeny - y1) * (x1 - x2)) / (y2 - y1);
		y1 = screeny - 1;
	}
	if (y2 >= screeny) {
		x2 -= ((screeny - y2) * (x1 - x2)) / (y2 - y1);
		y2 = screeny - 1;
	}
	/* clip to left edge */
	if ((x1 < 0) && (x2 < 0))
		return;
	if (x1 < 0) {
		y1 += (x1 * (y1 - y2)) / (x2 - x1);
		x1 = 0;
	}
	if (x2 < 0) {
		y2 += (x2 * (y1 - y2)) / (x2 - x1);
		x2 = 0;
	}
	/* clip to right edge */
	if ((x1 >= screenx) && (x2 >= screenx))
		return;
	if (x1 >= screenx) {
		y1 -= ((screenx - x1) * (y1 - y2)) / (x2 - x1);
		x1 = screenx - 1;
	}
	if (x2 >= screenx) {
		y2 -= ((screenx - x2) * (y1 - y2)) / (x2 - x1);
		x2 = screenx - 1;
	}
	dx = x2 - x1;
	dy = y2 - y1;
	if (x1 > x2) {
		int     tmp;

		tmp = x1;
		x1 = x2;
		x2 = tmp;
		tmp = y1;
		y1 = y2;
		y2 = tmp;
		dx = x2 - x1;
		dy = y2 - y1;
	}

	/* vertical line */
	if (dx == 0) {
		if (y1 < y2) {
			p = &(data[(screenx * y1) + x1]);
			for (y = y1; y <= y2; y++) {
				DRAWMETHOD;
				p += screenx;
			}
		}
		else {
			p = &(data[(screenx * y2) + x1]);
			for (y = y2; y <= y1; y++) {
				DRAWMETHOD;
				p += screenx;
			}
		}
		return;
	}
	/* horizontal line */
	if (dy == 0) {
		if (x1 < x2) {
			p = &(data[(screenx * y1) + x1]);
			for (x = x1; x <= x2; x++) {
				DRAWMETHOD;
				p++;
			}
			return;
		}
		else {
			p = &(data[(screenx * y1) + x2]);
			for (x = x2; x <= x1; x++) {
				DRAWMETHOD;
				p++;
			}
			return;
		}
	}
	/* 1    */
	/* \   */
	/* \  */
	/* 2 */
	if (y2 > y1) {
		/* steep */
		if (dy > dx) {
			dx = ((dx << 16) / dy);
			x = x1 << 16;
			for (y = y1; y <= y2; y++) {
				xx = x >> 16;
				p = &(data[(screenx * y) + xx]);
				DRAWMETHOD;
				if (xx < (screenx - 1)) {
					p++;
/*                                 DRAWMETHOD; */
				}
				x += dx;
			}
			return;
		}
		/* shallow */
		else {
			dy = ((dy << 16) / dx);
			y = y1 << 16;
			for (x = x1; x <= x2; x++) {
				yy = y >> 16;
				p = &(data[(screenx * yy) + x]);
				DRAWMETHOD;
				if (yy < (screeny - 1)) {
					p += screeny;
/*                                       DRAWMETHOD; */
				}
				y += dy;
			}
		}
	}
	/* 2 */
	/* /  */
	/* /   */
	/* 1    */
	else {
		/* steep */
		if (-dy > dx) {
			dx = ((dx << 16) / -dy);
			x = (x1 + 1) << 16;
			for (y = y1; y >= y2; y--) {
				xx = x >> 16;
				p = &(data[(screenx * y) + xx]);
				DRAWMETHOD;
				if (xx < (screenx - 1)) {
					p--;
/*                                 DRAWMETHOD; */
				}
				x += dx;
			}
			return;
		}
		/* shallow */
		else {
			dy = ((dy << 16) / dx);
			y = y1 << 16;
			for (x = x1; x <= x2; x++) {
				yy = y >> 16;
				p = &(data[(screenx * yy) + x]);
				DRAWMETHOD;
				if (yy < (screeny - 1)) {
					p += screeny;
/*                                 DRAWMETHOD; */
				}
				y += dy;
			}
			return;
		}
	}
}

void
genline (int id, float param, GMUnitPointer * l, int rx, int ry)
{
	int     i;

	switch (id) {
	case GML_HLINE:
		for (i = 0; i < 512; i++) {
			l[i].x = ((float) i * rx) / 512.0f;
			l[i].y = param;
			l[i].angle = M_PI / 2.0f;
		}
		return;
	case GML_VLINE:
		for (i = 0; i < 512; i++) {
			l[i].y = ((float) i * ry) / 512.0f;
			l[i].x = param;
			l[i].angle = 0.0f;
		}
		return;
	case GML_CIRCLE:
		for (i = 0; i < 512; i++) {
			float   cosa, sina;

			l[i].angle = 2.0f * M_PI * (float) i / 512.0f;
			cosa = param * cos (l[i].angle);
			sina = param * sin (l[i].angle);
			l[i].x = ((float) rx / 2.0f) + cosa;
			l[i].y = (float) ry / 2.0f + sina;
		}
		return;
	}
}

guint32 getcouleur (int mode)
{
	switch (mode) {
	case GML_RED:
		return (230 << (ROUGE * 8)) | (120 << (VERT * 8));
	case GML_ORANGE_J:
		return (120 << (VERT * 8)) | (252 << (ROUGE * 8));
	case GML_ORANGE_V:
		return (160 << (VERT * 8)) | (236 << (ROUGE * 8)) | (40 << (BLEU * 8));
	case GML_BLEUBLANC:
		return (40 << (BLEU * 8)) | (220 << (ROUGE * 8)) | (140 << (VERT * 8));
	case GML_VERT:
		return (200 << (VERT * 8)) | (80 << (ROUGE * 8));
	case GML_BLEU:
		return (250 << (BLEU * 8)) | (30 << (VERT * 8)) | (80 << (ROUGE * 8));
	case GML_BLACK:
		return 0x10 << (BLEU * 8);
	}
	return 0;
}

void
goom_lines_set_res (GMLine * gml, int rx, int ry)
{
	if (gml != NULL) {
		gml->screenX = rx;
		gml->screenY = ry;

		genline (gml->IDdest, gml->param, gml->points2, rx, ry);
	}
}


void
goom_lines_move (GMLine * l)
{
	int     i;
	unsigned char *c1, *c2;

	for (i = 0; i < 512; i++) {
		l->points[i].x = (l->points2[i].x + 39.0f * l->points[i].x) / 40.0f;
		l->points[i].y = (l->points2[i].y + 39.0f * l->points[i].y) / 40.0f;
		l->points[i].angle =
			(l->points2[i].angle + 39.0f * l->points[i].angle) / 40.0f;
	}

	c1 = (unsigned char *) &l->color;
	c2 = (unsigned char *) &l->color2;
	for (i = 0; i < 4; i++) {
		int     cc1, cc2;

		cc1 = *c1;
		cc2 = *c2;
		*c1 = (unsigned char) ((cc1 * 63 + cc2) >> 6);
		++c1;
		++c2;
	}

	l->power += l->powinc;
	if (l->power < -2.6f) {
		l->power = -2.6f;
		l->powinc = (float) (iRAND (20) + 10) / 600.0f;
	}
	if (l->power > 0.6f) {
		l->power = 0.6f;
		l->powinc = -(float) (iRAND (20) + 10) / 600.0f;
	}
}

void
goom_lines_switch_to (GMLine * gml, int IDdest, float param, int col)
{
	genline (IDdest, param, gml->points2, gml->screenX, gml->screenY);
	gml->IDdest = IDdest;
	gml->param = param;
	gml->color2 = getcouleur (col);
/*  printf ("couleur %d : %x\n",col,gml->color2); */
}

inline unsigned char
lighten (unsigned char value, float power)
{
	int     val = value;
	float   t = exp ((float) val / 32.0f) + power;

	if (t > 0) {
		val = (int) (32.0f * log (t));
		if (val > 255)
			val = 255;
		if (val < 0)
			val = 0;
		return val;
	}
	else {
		return 0;
	}
}

void
lightencolor (int *col, float power)
{
	unsigned char *color;

	color = (unsigned char *) col;
	*color = lighten (*color, power);
	color++;
	*color = lighten (*color, power);
	color++;
	*color = lighten (*color, power);
	color++;
	*color = lighten (*color, power);
}

GMLine *
goom_lines_init (int rx, int ry,
								 int IDsrc, float paramS, int coulS,
								 int IDdest, float paramD, int coulD)
{
	GMLine *l = (GMLine *) malloc (sizeof (GMLine));

	l->points = (GMUnitPointer *) malloc (512 * sizeof (GMUnitPointer));
	l->points2 = (GMUnitPointer *) malloc (512 * sizeof (GMUnitPointer));
	l->nbPoints = 512;

	l->IDdest = IDdest;
	l->param = paramD;

	genline (IDsrc, paramS, l->points, rx, ry);
	genline (IDdest, paramD, l->points2, rx, ry);

	l->color = getcouleur (coulS);
	l->color2 = getcouleur (coulD);

	l->screenX = rx;
	l->screenY = ry;

	l->power = 0.0f;
	l->powinc = 0.01f;

	goom_lines_switch_to (l, IDdest, paramD, coulD);

	return l;
}

void
goom_lines_free (GMLine ** l)
{
	free ((*l)->points);
	free (*l);
	l = NULL;
}

void
goom_lines_draw (GMLine * line, gint16 data[512], unsigned int *p)
{
	if (line != NULL) {
		int     i, x1, y1;
		guint32 color = line->color;
		GMUnitPointer *pt = &(line->points[0]);

		float   cosa = cos (pt->angle) / 1000.0f;
		float   sina = sin (pt->angle) / 1000.0f;

		lightencolor (&color, line->power);

		x1 = (int) (pt->x + cosa * data[0]);
		y1 = (int) (pt->y + sina * data[0]);

		for (i = 1; i < 512; i++) {
			int     x2, y2;
			GMUnitPointer *pt = &(line->points[i]);

			float   cosa = cos (pt->angle) / 1000.0f;
			float   sina = sin (pt->angle) / 1000.0f;

			x2 = (int) (pt->x + cosa * data[i]);
			y2 = (int) (pt->y + sina * data[i]);

			draw_line (p, x1, y1, x2, y2, color, line->screenX, line->screenY);
			DRAWMETHOD_DONE ();
			x1 = x2;
			y1 = y2;
		}
		goom_lines_move (line);
	}
}
