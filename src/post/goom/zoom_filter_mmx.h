#ifndef ZOOM_FILTER_MMX_H
#define ZOOM_FILTER_MMX_H

void zoom_filter_mmx (int prevX, int prevY,
		      unsigned int *expix1, unsigned int *expix2,
		      int *brutS, int *brutD, int buffratio,
		      int precalCoef[16][16]);

#endif
