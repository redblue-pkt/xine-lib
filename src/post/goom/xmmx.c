/* a definir pour avoir exactement le meme resultat que la fonction C
 * (un chouillat plus lent)
 */

#ifdef HAVE_MMX

#define STRICT_COMPAT

//#define HAVE_ATHLON

#define BUFFPOINTNB 16
#define BUFFPOINTMASK 0xffff
#define BUFFINCR 0xff

#define sqrtperte 16
/* faire : a % sqrtperte <=> a & pertemask*/
#define PERTEMASK 0xf
/* faire : a / sqrtperte <=> a >> PERTEDEC*/
#define PERTEDEC 4


/*#define MMX_TRACE*/
#include "mmx.h"
/*#include "xmmx.h"*/
#include "goom_graphic.h"

int xmmx_supported (void) {
	return (mm_support()&0x8)>>3;
}

void zoom_filter_xmmx (int prevX, int prevY,
                       Pixel *expix1, Pixel *expix2,
                       int *lbruS, int *lbruD, int buffratio,
                       int precalCoef[16][16])
{
	int bufsize = prevX * prevY; /* taille du buffer */
	volatile int loop;                    /* variable de boucle */

	mmx_t *brutS = (mmx_t*)lbruS; /* buffer de transformation source */
	mmx_t *brutD = (mmx_t*)lbruD; /* buffer de transformation dest */

	volatile mmx_t prevXY;
	volatile mmx_t ratiox;
	/*	volatile mmx_t interpix; */

	expix1[0].val=expix1[prevX-1].val=expix1[prevX*prevY-1].val=expix1[prevX*prevY-prevX].val=0;

	prevXY.ud[0] = (prevX-1)<<PERTEDEC;
	prevXY.ud[1] = (prevY-1)<<PERTEDEC;

	ratiox.d[0] = buffratio;
	ratiox.d[1] = buffratio;
	movq_m2r (ratiox, mm6);
	pslld_i2r (16,mm6);

	pxor_r2r (mm7,mm7); /* mise a zero de mm7 */

	loop=0;

	/*
	 * NOTE : mm6 et mm7 ne sont pas modifies dans la boucle.
	 */
	while (loop < bufsize)
	{
		/*
		 * pre : mm6 = [buffratio<<16|buffratio<<16]
		 * post : mm0 = S + ((D-S)*buffratio)>>16 format [X|Y]
		 * modified = mm0,mm1,mm2
		 */

		__asm__ __volatile__ (
		                      "movq %0,%%mm0\n"
		                      "movq %1,%%mm1\n"
		                      : :"X"(brutS[loop]),"X"(brutD[loop])
		                     );               /* mm0 = S */

		psubd_r2r (mm0,mm1);           /* mm1 = D - S */
		movq_r2r (mm1, mm2);           /* mm2 = D - S */

		pslld_i2r (16,mm1);
		mmx_r2r (pmulhuw, mm6, mm1);   /* mm1 = ?? */
		pmullw_r2r (mm6, mm2);

		paddd_r2r (mm2, mm1);     /* mm1 = (D - S) * buffratio >> 16 */
		pslld_i2r (16,mm0);

		paddd_r2r (mm1, mm0);     /* mm0 = S + mm1 */
		psrld_i2r (16, mm0);

		/*
		 * pre : mm0 : position vector on screen
		 *       prevXY : coordinate of the lower-right point on screen
		 * post : clipped mm0
		 * modified : mm0,mm1,mm2
		 */
		movq_m2r (prevXY,mm1);
		pcmpgtd_r2r (mm0, mm1); /* mm0 en X contient :
		                            1111 si prevXY > px
		                            0000 si prevXY <= px
		                           (idem pour y) */
#ifdef STRICT_COMPAT
		movq_r2r (mm1,mm2);
		punpckhdq_r2r (mm2,mm2);
		punpckldq_r2r (mm1,mm1);
		pand_r2r (mm2, mm0);
#endif
		pand_r2r (mm1, mm0);    /* on met a zero la partie qui deborde */

		/*
		 * pre : mm0 : clipped position on screen
		 *
		 * post : mm3 & mm4 : coefs for this position
		 *              mm1 : X vector [0|X]
		 *
		 * modif : eax,ebx
		 */
		__asm__ __volatile__ (
			"movd %%mm0,%%ebx\n"
			"movq %%mm0,%%mm1\n"

			"andl $15,%%ebx\n"
			"psrlq $32,%%mm1\n"

			"shll $6,%%ebx\n"
			"movd %%mm1,%%eax\n"

			"addl %0,%%ebx\n"
			"andl $15,%%eax\n"

			"movd (%%ebx,%%eax,4),%%mm3\n"
			::"g"(precalCoef):"eax","ebx");

		/*
		 * extraction des coefficients...
		 *
		 * pre : coef dans mm3
		 *
		 * post : coef extraits dans mm3 (c1 & c2)
		 *                        et mm4 (c3 & c4)
		 *
		 * modif : mm5
		 */

		/* entrelace avec portion d'apres (cf les '^')
			movq_r2r (mm3, mm5);            / * ??-??-??-??-c4-c3-c2-c1 * /
			punpcklbw_r2r (mm5, mm3);       / * c4-c4-c3-c3-c2-c2-c1-c1 * /
			movq_r2r (mm3, mm4);            / * c4-c4-c3-c3-c2-c2-c1-c1 * /
			movq_r2r (mm3, mm5);            / * c4-c4-c3-c3-c2-c2-c1-c1 * /

			punpcklbw_r2r (mm5, mm3);       / * c2-c2-c2-c2-c1-c1-c1-c1 * /
			punpckhbw_r2r (mm5, mm4);       / * c4-c4-c4-c4-c3-c3-c3-c3 * /
		*/

		/*
		 * pre : mm0 : Y pos [*|Y]
		 *       mm1 : X pos [*|X]
		 *
		 * post : mm0 : expix1[position]
		 *        mm2 : expix1[position+largeur]
		 *
		 * modif : eax,ebx
		 */
		psrld_i2r (PERTEDEC,mm0);
		psrld_i2r (PERTEDEC,mm1);
		__asm__ __volatile__ (
			"movd %%mm1,%%eax\n"
			/*^*/ "movq %%mm3,%%mm5\n"       /*^*/

			"mull %1\n"
			"movd %%mm0,%%ebx\n"
			/*^*/ "punpcklbw %%mm5, %%mm3\n" /*^*/

			"addl %%ebx,%%eax\n"
			/*^*/ "movq %%mm3,%%mm4\n"       /*^*/
			/*^*/ "movq %%mm3,%%mm5\n"       /*^*/

			"movl %0,%%ebx\n"
			/*^*/ "punpcklbw %%mm5,%%mm3\n"  /*^*/

			"movq (%%ebx,%%eax,4),%%mm0\n"
			/*^*/ "punpckhbw %%mm5,%%mm4\n"  /*^*/

			"addl %1,%%eax\n"
			"movq (%%ebx,%%eax,4),%%mm2\n"

			: : "X"(expix1), "X"(prevX):"eax","ebx"
		);

		/*
		 * pre :       mm0 : expix1[position]
		 *             mm2 : expix1[position+largeur]
		 *       mm3 & mm4 : coefs
		 */

		/* recopie des deux premiers pixels dans mm0 et mm1 */
		movq_r2r (mm0, mm1);            /* b1-v1-r1-a1-b2-v2-r2-a2 */

		/* depackage du premier pixel */
		punpcklbw_r2r (mm7, mm0);       /* 00-b2-00-v2-00-r2-00-a2 */

		/* extraction des coefficients... */

		movq_r2r (mm3, mm5);      /* c2-c2-c2-c2-c1-c1-c1-c1 */

		/*^en parrallele^*/ /* depackage du 2ieme pixel */
		/*^*/ punpckhbw_r2r (mm7, mm1); /* 00-b1-00-v1-00-r1-00-a1 */

		punpcklbw_r2r (mm7, mm5);	/* 00-c1-00-c1-00-c1-00-c1 */
		punpckhbw_r2r (mm7, mm3);	/* 00-c2-00-c2-00-c2-00-c2 */

		/* multiplication des pixels par les coefficients */
		pmullw_r2r (mm5, mm0);		/* c1*b2-c1*v2-c1*r2-c1*a2 */
		pmullw_r2r (mm3, mm1);		/* c2*b1-c2*v1-c2*r1-c2*a1 */
		paddw_r2r (mm1, mm0);

		/* ...extraction des 2 derniers coefficients */
		movq_r2r (mm4, mm5);			/* c4-c4-c4-c4-c3-c3-c3-c3 */
		punpcklbw_r2r (mm7, mm4);	/* 00-c3-00-c3-00-c3-00-c3 */
		punpckhbw_r2r (mm7, mm5);	/* 00-c4-00-c4-00-c4-00-c4 */

		/* recuperation des 2 derniers pixels */
		movq_r2r (mm2, mm1);

		/* depackage des pixels */
		punpcklbw_r2r (mm7, mm1);
		punpckhbw_r2r (mm7, mm2);

		/* multiplication pas les coeffs */
		pmullw_r2r (mm4, mm1);
		pmullw_r2r (mm5, mm2);

		/* ajout des valeurs obtenues à la valeur finale */
		paddw_r2r (mm1, mm0);
		paddw_r2r (mm2, mm0);

		/* division par 256 = 16+16+16+16, puis repackage du pixel final */
		psrlw_i2r (8, mm0);
		packuswb_r2r (mm7, mm0);

		movd_r2m (mm0,expix2[loop]);

		++loop;
	}
/*#ifdef HAVE_ATHLON*/
	__asm__ __volatile__ ("femms\n");
/*#else
	emms();
#endif*/
}

#define DRAWMETHOD_PLUS_XMMX(_out,_backbuf,_col) \
{ \
	movd_m2r(_backbuf, mm0); \
	paddusb_m2r(_col, mm0); \
	movd_r2m(mm0, _out); \
}

#define DRAWMETHOD DRAWMETHOD_PLUS_XMMX(*p,*p,col)

void draw_line_xmmx (Pixel *data, int x1, int y1, int x2, int y2, int col, int screenx, int screeny)
{
	int x, y, dx, dy, yy, xx;
	Pixel *p;

	if ((y1 < 0) || (y2 < 0) || (x1 < 0) || (x2 < 0) || (y1 >= screeny) || (y2 >= screeny) || (x1 >= screenx) || (x2 >= screenx))
		goto end_of_line;

	dx = x2 - x1;
	dy = y2 - y1;
	if (x1 >= x2) {
		int tmp;

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
		goto end_of_line;
	}
	/* horizontal line */
	if (dy == 0) {
		if (x1 < x2) {
			p = &(data[(screenx * y1) + x1]);
			for (x = x1; x <= x2; x++) {
				DRAWMETHOD;
				p++;
			}
			goto end_of_line;
		}
		else {
			p = &(data[(screenx * y1) + x2]);
			for (x = x2; x <= x1; x++) {
				DRAWMETHOD;
				p++;
			}
			goto end_of_line;
		}
	}
	/* 1    */
	/*  \   */
	/*   \  */
	/*    2 */
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
					/* DRAWMETHOD; */
				}
				x += dx;
			}
			goto end_of_line;
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
					/* DRAWMETHOD; */
				}
				y += dy;
			}
		}
	}
	/*    2 */
	/*   /  */
	/*  /   */
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
					/* DRAWMETHOD; */
				}
				x += dx;
			}
			goto end_of_line;
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
					/* DRAWMETHOD; */
				}
				y += dy;
			}
			goto end_of_line;
		}
	}
end_of_line:
	__asm__ __volatile__ ("femms\n"); 
}

#endif
