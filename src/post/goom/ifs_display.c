#include <stdlib.h>
#include "ifs.h"

#define DRAWMETHOD_NORMAL data[pos] = couleur

#define DRAWMETHOD_PLUS(x) \
{\
  int tra=0,i=0;\
  unsigned char *bra = (unsigned char*)&back[pos];\
  unsigned char *dra = (unsigned char*)&data[pos];\
  unsigned char *cra = (unsigned char*)&couleur;\
  for (;i<4;i++) {\
	tra = *cra >> x;\
	tra += *bra;\
	if (tra>255) tra=255;\
	*dra = tra;\
	++dra;++cra;++bra;\
  }\
}

#define DRAWMETHOD_OR data[pos]|=couleur

#define DRAWMETHOD_DEMI data[pos]=((back[pos]&0xfefefefe) + (couleur & 0xfefefefe)) >> 1

#define DRAWMETHOD(x) {DRAWMETHOD_DEMI;DRAWMETHOD_PLUS(x);}

void ifs_update (guint32 *data, guint32 *back, int width, int height, int increment)
{
  static int couleur = 0xc0c0c0c0;
  static int v[4] = {2,4,3,2};
  static int col[4] = {2,4,3,2};
#define MOD_MER 0
#define MOD_FEU 1
#define MOD_MERVER 2
  static int mode = MOD_MERVER;
  static int justChanged = 0;
  static int cycle = 0;
  int cycle10;
  
  int nbpt;
  IFSPoint *points;
  int i;

  points = draw_ifs (&nbpt);

  cycle ++;
  if (cycle < 40) {
    cycle10 = cycle / 10;
    for (i = 0; i < nbpt; i+=increment) {
      int x = (int)points[i].x & 0x7fffffff;
      int y = (int)points[i].y & 0x7fffffff;
      if ((x < width) && (y < height)) {
	int pos = x + (int)(y * width);
	DRAWMETHOD(cycle10);
      }
    }
  }
  else {
    cycle10 = 7 - cycle / 10;
    for (i = 0; i < nbpt; i+=increment) {
      int x = (int)points[i].x & 0x7fffffff;
      int y = (int)points[i].y & 0x7fffffff;
      if ((x < width) && (y < height)) {
	int pos = x + (int)(y * width);
	DRAWMETHOD(cycle10);
      }
    }
    if (cycle >= 79)
      cycle = 0;
  }
  

  justChanged --;
  
  col[ALPHA] = couleur >> (ALPHA*8) & 0xff;
  col[BLEU] = couleur >> (BLEU*8) & 0xff;
  col[VERT] = couleur >> (VERT*8) & 0xff;
  col[ROUGE] = couleur >> (ROUGE*8) & 0xff;

  if (mode == MOD_MER) {
	col[BLEU] += v[BLEU];
	if (col[BLEU]>255) {
	  col[BLEU]=255; v[BLEU] = - (rand () % 4) - 1;
	}
	if (col[BLEU]<32) {
	  col[BLEU]=32; v[BLEU] = (rand () % 4) + 1;
	}
	
	col[VERT] += v[VERT];
	if (col[VERT]>200) {
	  col[VERT]=200; v[VERT] = - (rand () % 3) - 2;
	}
	if (col[VERT]>col[BLEU]) {
	  col[VERT]=col[BLEU]; v[VERT] = v[BLEU];
	}
	if (col[VERT]<32) {
	  col[VERT]=32; v[VERT] = (rand () % 3) + 2;
	}
	
	col[ROUGE] += v[ROUGE];
	if (col[ROUGE]>64) {
	  col[ROUGE]=64; v[ROUGE] = - (rand () % 4) - 1;
	}
	if (col[ROUGE]<0) {
	  col[ROUGE]=0; v[ROUGE] = (rand () % 4) + 1;
	}

  col[ALPHA] += v[ALPHA];
  if (col[ALPHA]>0) {
	col[ALPHA]=0; v[ALPHA] = - (rand () % 4) - 1;
  }
  if (col[ALPHA]<0) {
	col[ALPHA]=0; v[ALPHA] = (rand () % 4) + 1;
  }
  
	if (((col [VERT] > 32) && (col[ROUGE]<col[VERT] + 40) && (col[VERT]<col[ROUGE] + 20) && (col [BLEU] < 64)
		 && (rand () % 20 == 0)) && (justChanged < 0)) {
	  mode = rand()%3?MOD_FEU:MOD_MERVER;
	  justChanged = 250;
	}
  }
  else if (mode == MOD_MERVER) {
	col[BLEU] += v[BLEU];
	if (col[BLEU]>128) {
	  col[BLEU]=128; v[BLEU] = - (rand () % 4) - 1;
	}
	if (col[BLEU]<16) {
	  col[BLEU]=16; v[BLEU] = (rand () % 4) + 1;
	}
	
	col[VERT] += v[VERT];
	if (col[VERT]>200) {
	  col[VERT]=200; v[VERT] = - (rand () % 3) - 2;
	}
	if (col[VERT]>col[ALPHA]) {
	  col[VERT]=col[ALPHA]; v[VERT] = v[ALPHA];
	}
	if (col[VERT]<32) {
	  col[VERT]=32; v[VERT] = (rand () % 3) + 2;
	}
	
	col[ROUGE] += v[ROUGE];
	if (col[ROUGE]>128) {
	  col[ROUGE]=128; v[ROUGE] = - (rand () % 4) - 1;
	}
	if (col[ROUGE]<0) {
	  col[ROUGE]=0; v[ROUGE] = (rand () % 4) + 1;
	}

	col[ALPHA] += v[ALPHA];
	if (col[ALPHA]>255) {
	  col[ALPHA]=255; v[ALPHA] = - (rand () % 4) - 1;
	}
	if (col[ALPHA]<0) {
	  col[ALPHA]=0; v[ALPHA] = (rand () % 4) + 1;
	}	

	if (((col [VERT] > 32) && (col[ROUGE]<col[VERT] + 40) && (col[VERT]<col[ROUGE] + 20) && (col [BLEU] < 64)
		 && (rand () % 20 == 0)) && (justChanged < 0)) {
	  mode = rand()%3?MOD_FEU:MOD_MER;
	  justChanged = 250;
	}
  }
  else if (mode == MOD_FEU) {

	col[BLEU] += v[BLEU];
	if (col[BLEU]>64) {
	  col[BLEU]=64; v[BLEU] = - (rand () % 4) - 1;
	}
	if (col[BLEU]<0) {
	  col[BLEU]=0; v[BLEU] = (rand () % 4) + 1;
	}
	
	col[VERT] += v[VERT];
	if (col[VERT]>200) {
	  col[VERT]=200; v[VERT] = - (rand () % 3) - 2;
	}
	if (col[VERT]>col[ROUGE] + 20) {
	  col[VERT]=col[ROUGE] + 20; v[VERT] = - (rand () % 3) - 2;
	  v[ROUGE] = (rand () % 4) + 1; v[BLEU] = (rand () % 4) + 1;
	}
	if (col[VERT]<0) {
	  col[VERT]=0; v[VERT] = (rand () % 3) + 2;
	}
	
	col[ROUGE] += v[ROUGE];
	if (col[ROUGE]>255) {
	  col[ROUGE]=255; v[ROUGE] = - (rand () % 4) - 1;
	}
	if (col[ROUGE]>col[VERT]+40) {
	  col[ROUGE]=col[VERT]+40; v[ROUGE] = - (rand () % 4) - 1;
	}
	if (col[ROUGE]<0) {
	  col[ROUGE]=0; v[ROUGE] = (rand () % 4) + 1;
	}

  col[ALPHA] += v[ALPHA];
  if (col[ALPHA]>0) {
	col[ALPHA]=0; v[ALPHA] = - (rand () % 4) - 1;
  }
  if (col[ALPHA]<0) {
	col[ALPHA]=0; v[ALPHA] = (rand () % 4) + 1;
  }
  
	if (((col [ROUGE] < 64) && (col [VERT] > 32) && (col [VERT] < col [BLEU]) && (col [BLEU] > 32)
		&& (rand () % 20 == 0)) && (justChanged < 0)) {
	  mode = rand () % 2 ? MOD_MER : MOD_MERVER;
	  justChanged = 250;
	}
  }

  couleur = (col[ALPHA]<<(ALPHA*8))
	|(col[BLEU]<<(BLEU*8))
	|(col[VERT]<<(VERT*8))
	|(col[ROUGE]<<(ROUGE*8));
}
