#include "goom_config.h"
#ifdef MMX
#define BUFFPOINTNB 16
#define BUFFPOINTMASK 0xffff
#define BUFFINCR 0xff

#define sqrtperte 16
/* faire : a % sqrtperte <=> a & pertemask */
#define PERTEMASK 0xf
/* faire : a / sqrtperte <=> a >> PERTEDEC */
#define PERTEDEC 4

void zoom_filter_mmx (int prevX, int prevY,
					  unsigned int *expix1, unsigned int *expix2,
					  int *brutS, int *brutD, int buffratio,
					  int precalCoef[16][16])
{
  unsigned int ax = (prevX-1)<<PERTEDEC, ay = (prevY-1)<<PERTEDEC;
  
  int bufsize = prevX * prevY;
  int loop;

  __asm__ ("pxor %mm7,%mm7");
  
  for (loop=0; loop<bufsize; loop++)
	{
	  int couleur;
	  int px,py;
	  int pos;
	  int coeffs;

	  int myPos = loop << 1,
		myPos2 = myPos + 1;
	  int brutSmypos = brutS[myPos];
	  
	  px = brutSmypos + (((brutD[myPos] - brutSmypos)*buffratio) >> BUFFPOINTNB);
	  brutSmypos = brutS[myPos2];
	  py = brutSmypos + (((brutD[myPos2] - brutSmypos)*buffratio) >> BUFFPOINTNB);
	  
	  if ((py>=ay) || (px>=ax)) {
		pos=coeffs=0;
	  }
	  else {
		pos = ((px >> PERTEDEC) + prevX * (py >> PERTEDEC));
		/* coef en modulo 15 */
		coeffs = precalCoef [px & PERTEMASK][py & PERTEMASK];
	  }

	  __asm__ __volatile__ ("
               movd %%eax,%%mm6
			   ;/* recuperation des deux premiers pixels dans mm0 et mm1 */
	   	       movq (%%edx,%%ebx,4), %%mm0		/* b1-v1-r1-a1-b2-v2-r2-a2 */
			   movq %%mm0, %%mm1				/* b1-v1-r1-a1-b2-v2-r2-a2 */
			   
			   ;/* depackage du premier pixel */
			   punpcklbw %%mm7, %%mm0	/* 00-b2-00-v2-00-r2-00-a2 */
			   
			   movq %%mm6, %%mm5			/* ??-??-??-??-c4-c3-c2-c1 */
			   ;/* depackage du 2ieme pixel */
			   punpckhbw %%mm7, %%mm1	/* 00-b1-00-v1-00-r1-00-a1 */
							   
			   ;/* extraction des coefficients... */
			   punpcklbw %%mm5, %%mm6	/* c4-c4-c3-c3-c2-c2-c1-c1 */
			   movq %%mm6, %%mm4			/* c4-c4-c3-c3-c2-c2-c1-c1 */
			   movq %%mm6, %%mm5			/* c4-c4-c3-c3-c2-c2-c1-c1 */
											   
			   punpcklbw %%mm5, %%mm6	/* c2-c2-c2-c2-c1-c1-c1-c1 */
			   punpckhbw %%mm5, %%mm4	/* c4-c4-c4-c4-c3-c3-c3-c3 */

			   movq %%mm6, %%mm3			/* c2-c2-c2-c2-c1-c1-c1-c1 */
			   punpcklbw %%mm7, %%mm6	/* 00-c1-00-c1-00-c1-00-c1 */
			   punpckhbw %%mm7, %%mm3	/* 00-c2-00-c2-00-c2-00-c2 */
	
			   ;/* multiplication des pixels par les coefficients */
			   pmullw %%mm6, %%mm0		/* c1*b2-c1*v2-c1*r2-c1*a2 */
			   pmullw %%mm3, %%mm1		/* c2*b1-c2*v1-c2*r1-c2*a1 */
			   paddw %%mm1, %%mm0
			   
			   ;/* ...extraction des 2 derniers coefficients */
			   movq %%mm4, %%mm5			/* c4-c4-c4-c4-c3-c3-c3-c3 */
			   punpcklbw %%mm7, %%mm4	/* 00-c3-00-c3-00-c3-00-c3 */
			   punpckhbw %%mm7, %%mm5	/* 00-c4-00-c4-00-c4-00-c4 */

			   /* ajouter la longueur de ligne a esi */
			   addl 8(%%ebp),%%ebx
	   
			   ;/* recuperation des 2 derniers pixels */
			   movq (%%edx,%%ebx,4), %%mm1
			   movq %%mm1, %%mm2
			
			   ;/* depackage des pixels */
			   punpcklbw %%mm7, %%mm1
			   punpckhbw %%mm7, %%mm2
			
			   ;/* multiplication pas les coeffs */
			   pmullw %%mm4, %%mm1
			   pmullw %%mm5, %%mm2
			   
			   ;/* ajout des valeurs obtenues à la valeur finale */
			   paddw %%mm1, %%mm0
			   paddw %%mm2, %%mm0
			   
			   ;/* division par 256 = 16+16+16+16, puis repackage du pixel final */
			   psrlw $8, %%mm0
			   packuswb %%mm7, %%mm0

               movd %%mm0,%%eax
			   "
							:"=eax"(expix2[loop])
							:"ebx"(pos),"eax"(coeffs),"edx"(expix1)
							
				);
	  
/*	  expix2[loop] = couleur; */
	  
	  __asm__ __volatile__ ("emms");
	}
}
#endif
