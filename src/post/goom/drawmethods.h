#ifndef _DRAWMETHODS_H
#define _DRAWMETHODS_H

#include "goom_config.h"

#define DRAWMETHOD_NORMAL(adr,col) {*(adr) = (col);}

/* #ifdef MMX */
#if 0
#define DRAWMETHOD_PLUS(_out,_backbuf,_col) \
{\
__asm__		("movd %%eax,%%mm0\n movd %%edx,%%mm1\n paddusb %%mm1,%%mm0\n movd %%mm0,%%eax"\
:"=eax"(_out):"eax"(_backbuf),"edx"(_col));\
}
#else
#define DRAWMETHOD_PLUS(_out,_backbuf,_col) \
{\
      int tra=0,i=0;\
      unsigned char *bra = (unsigned char*)&(_backbuf);\
      unsigned char *dra = (unsigned char*)&(_out);\
      unsigned char *cra = (unsigned char*)&(_col);\
      for (;i<4;i++) {\
				tra = *cra;\
				tra += *bra;\
				if (tra>255) tra=255;\
				*dra = tra;\
				++dra;++cra;++bra;\
			}\
}
#endif

#define DRAWMETHOD_OR(adr,col) {*(adr)|=(col);}

/* #ifdef MMX */
#if 0
#define DRAWMETHOD_DONE() {__asm__ __volatile__ ("emms");}
#else
#define DRAWMETHOD_DONE() {}
#endif

#endif
