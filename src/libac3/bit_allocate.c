/* 
 *  bit_allocate.c
 *
 *	Copyright (C) Aaron Holtzman - May 1999
 *
 *  This file is part of ac3dec, a free Dolby AC-3 stream decoder.
 *	
 *  ac3dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  ac3dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include <stdlib.h>
#include <string.h>
#include "ac3.h"
#include "ac3_internal.h"
#include "bit_allocate.h"



static int16_t calc_lowcomp(int16_t a,int16_t b0,int16_t b1,int16_t bin);
static inline uint16_t min(int16_t a,int16_t b);
static inline uint16_t max(int16_t a,int16_t b);

static void ba_compute_mask(int16_t start, int16_t end, uint16_t fscod,
			    uint16_t deltbae, uint16_t deltnseg,
			    uint16_t deltoffst[], uint16_t deltba[],
			    uint16_t deltlen[], int16_t excite[],
			    int16_t mask[]);
static void ba_compute_bap(int16_t start, int16_t end, int16_t snroffset,
			   int8_t exp[], int16_t mask[], int16_t bap[]);

/* Misc LUTs for bit allocation process */

static int16_t slowdec[]  = { 0x0f,  0x11,  0x13,  0x15  };
static int16_t fastdec[]  = { 0x3f,  0x53,  0x67,  0x7b  };
static int16_t slowgain[] = { 0x540, 0x4d8, 0x478, 0x410 };
static int16_t dbpbtab[]  = { 0x000, 0x700, 0x900, 0xb00 };

static uint16_t floortab[] = { 0x2f0, 0x2b0, 0x270, 0x230, 0x1f0, 0x170, 0x0f0, 0xf800 };
static int16_t fastgain[] = { 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380, 0x400  };


static int16_t bndtab[] = {  0,  1,  2,   3,   4,   5,   6,   7,   8,   9, 
			     10, 11, 12,  13,  14,  15,  16,  17,  18,  19,
			     20, 21, 22,  23,  24,  25,  26,  27,  28,  31,
			     34, 37, 40,  43,  46,  49,  55,  61,  67,  73,
			     79, 85, 97, 109, 121, 133, 157, 181, 205, 229 };

static int16_t bndsz[]  = { 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
			    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
			    1,  1,  1,  1,  1,  1,  1,  1,  3,  3,
			    3,  3,  3,  3,  3,  6,  6,  6,  6,  6,
			    6, 12, 12, 12, 12, 24, 24, 24, 24, 24 };

static int16_t masktab[] = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
			     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 28, 28, 29,
			     29, 29, 30, 30, 30, 31, 31, 31, 32, 32, 32, 33, 33, 33, 34, 34,
			     34, 35, 35, 35, 35, 35, 35, 36, 36, 36, 36, 36, 36, 37, 37, 37,
			     37, 37, 37, 38, 38, 38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 40,
			     40, 40, 40, 40, 40, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41,
			     41, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 43, 43, 43,
			     43, 43, 43, 43, 43, 43, 43, 43, 43, 44, 44, 44, 44, 44, 44, 44,
			     44, 44, 44, 44, 44, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
			     45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 45, 46, 46, 46,
			     46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 46,
			     46, 46, 46, 46, 46, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47,
			     47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 47, 48, 48, 48,
			     48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48,
			     48, 48, 48, 48, 48, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49,
			     49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49,  0,  0,  0 };


static int16_t latab[] = { 0x0040, 0x003f, 0x003e, 0x003d, 0x003c, 0x003b, 0x003a, 0x0039,
			   0x0038, 0x0037, 0x0036, 0x0035, 0x0034, 0x0034, 0x0033, 0x0032,
			   0x0031, 0x0030, 0x002f, 0x002f, 0x002e, 0x002d, 0x002c, 0x002c,
			   0x002b, 0x002a, 0x0029, 0x0029, 0x0028, 0x0027, 0x0026, 0x0026,
			   0x0025, 0x0024, 0x0024, 0x0023, 0x0023, 0x0022, 0x0021, 0x0021,
			   0x0020, 0x0020, 0x001f, 0x001e, 0x001e, 0x001d, 0x001d, 0x001c,
			   0x001c, 0x001b, 0x001b, 0x001a, 0x001a, 0x0019, 0x0019, 0x0018,
			   0x0018, 0x0017, 0x0017, 0x0016, 0x0016, 0x0015, 0x0015, 0x0015,
			   0x0014, 0x0014, 0x0013, 0x0013, 0x0013, 0x0012, 0x0012, 0x0012,
			   0x0011, 0x0011, 0x0011, 0x0010, 0x0010, 0x0010, 0x000f, 0x000f,
			   0x000f, 0x000e, 0x000e, 0x000e, 0x000d, 0x000d, 0x000d, 0x000d,
			   0x000c, 0x000c, 0x000c, 0x000c, 0x000b, 0x000b, 0x000b, 0x000b,
			   0x000a, 0x000a, 0x000a, 0x000a, 0x000a, 0x0009, 0x0009, 0x0009,
			   0x0009, 0x0009, 0x0008, 0x0008, 0x0008, 0x0008, 0x0008, 0x0008,
			   0x0007, 0x0007, 0x0007, 0x0007, 0x0007, 0x0007, 0x0006, 0x0006,
			   0x0006, 0x0006, 0x0006, 0x0006, 0x0006, 0x0006, 0x0005, 0x0005,
			   0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0005, 0x0004, 0x0004,
			   0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004,
			   0x0004, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003,
			   0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0002,
			   0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002,
			   0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002,
			   0x0002, 0x0002, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
			   0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
			   0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
			   0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
			   0x0001, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
			   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
			   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
			   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
			   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
			   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
			   0x0000, 0x0000, 0x0000, 0x0000};

static int16_t hth[][50] = {{ 0x04d0, 0x04d0, 0x0440, 0x0400, 0x03e0, 0x03c0, 0x03b0, 0x03b0,  
			      0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x0390, 0x0390, 0x0390,  
			      0x0380, 0x0380, 0x0370, 0x0370, 0x0360, 0x0360, 0x0350, 0x0350,  
			      0x0340, 0x0340, 0x0330, 0x0320, 0x0310, 0x0300, 0x02f0, 0x02f0,
			      0x02f0, 0x02f0, 0x0300, 0x0310, 0x0340, 0x0390, 0x03e0, 0x0420,
			      0x0460, 0x0490, 0x04a0, 0x0460, 0x0440, 0x0440, 0x0520, 0x0800,
			      0x0840, 0x0840 },
			    { 0x04f0, 0x04f0, 0x0460, 0x0410, 0x03e0, 0x03d0, 0x03c0, 0x03b0, 
			      0x03b0, 0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x0390, 0x0390, 
			      0x0390, 0x0380, 0x0380, 0x0380, 0x0370, 0x0370, 0x0360, 0x0360, 
			      0x0350, 0x0350, 0x0340, 0x0340, 0x0320, 0x0310, 0x0300, 0x02f0, 
			      0x02f0, 0x02f0, 0x02f0, 0x0300, 0x0320, 0x0350, 0x0390, 0x03e0, 
			      0x0420, 0x0450, 0x04a0, 0x0490, 0x0460, 0x0440, 0x0480, 0x0630, 
			      0x0840, 0x0840 },
			    { 0x0580, 0x0580, 0x04b0, 0x0450, 0x0420, 0x03f0, 0x03e0, 0x03d0, 
			      0x03c0, 0x03b0, 0x03b0, 0x03b0, 0x03a0, 0x03a0, 0x03a0, 0x03a0, 
			      0x03a0, 0x03a0, 0x03a0, 0x03a0, 0x0390, 0x0390, 0x0390, 0x0390, 
			      0x0380, 0x0380, 0x0380, 0x0370, 0x0360, 0x0350, 0x0340, 0x0330, 
			      0x0320, 0x0310, 0x0300, 0x02f0, 0x02f0, 0x02f0, 0x0300, 0x0310, 
			      0x0330, 0x0350, 0x03c0, 0x0410, 0x0470, 0x04a0, 0x0460, 0x0440, 
			      0x0450, 0x04e0 }};


static int16_t baptab[] = { 0,  1,  1,  1,  1,  1,  2,  2,  3,  3,  3,  4,  4,  5,  5,  6,
			    6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  8,  9,  9,  9,  9, 10, 
			    10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14,
			    14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15 };

static int16_t sdecay;
static int16_t fdecay;
static int16_t sgain;
static int16_t dbknee;
static int16_t floor;
static int16_t bndpsd[256];
static int16_t excite[256];
static int16_t mask[256];

static inline uint16_t max(int16_t a,int16_t b)
{
    return (a > b ? a : b);
}
	
static inline uint16_t min(int16_t a,int16_t b)
{
    return (a < b ? a : b);
}

void bit_allocate(int fscod, audblk_t * audblk, ac3_ba_t * ba, uint16_t start,
		  uint16_t end, int16_t fastleak, int16_t slowleak,
		  uint8_t * exp, uint16_t * bap, int is_lfe)
{
    int16_t fgain;
    uint16_t snroffset;
    int i, j;
    int bndstrt;
    int lowcomp;

    /* Do some setup before we do the bit alloc */
    sdecay = slowdec[audblk->sdcycod];
    fdecay = fastdec[audblk->fdcycod];
    sgain = slowgain[audblk->sgaincod];
    dbknee = dbpbtab[audblk->dbpbcod];
    floor = floortab[audblk->floorcod];

    fgain = fastgain[ba->fgaincod];
    snroffset = 64 * audblk->csnroffst + 4 * ba->fsnroffst - 960;

    i = start;
    j = bndstrt = masktab[start];
    do {
	int psd, lastbin;

	lastbin = min (bndtab[j] + bndsz[j], end);
	psd = 128 * exp[i++];
	while (i < lastbin) {
	    int next, delta;

	    next = 128 * exp[i++];
	    delta = next - psd;
	    switch (delta >> 9) {
	    case -6: case -5: case -4: case -3: case -2:
		psd = next;
		break;
	    case -1:
		psd = next - latab[(-delta) >> 1];
		break;
	    case 0:
		psd -= latab[delta >> 1];
		break;
	    }
	}
	bndpsd[j++] = 3072 - psd;
    } while (i < end);

    // j = bndend
    j--;

    i = bndstrt;
    lowcomp = 0;
    if (i == 0) {	// not the coupling channel
	do {
	    if (i < j) {
		if (bndpsd[i+1] == bndpsd[i] + 256)
		    lowcomp = 384;
		else if (lowcomp && (bndpsd[i+1] < bndpsd[i]))
		    lowcomp -= 64;
	    }
	    excite[i++] = bndpsd[i] - fgain - lowcomp;
	} while ((i < 3) || ((i < 7) && (bndpsd[i] < bndpsd[i-1])));
	fastleak = bndpsd[i-1] - fgain;
	slowleak = bndpsd[i-1] - sgain;

	while (i < 7) {
	    if (i < j) {
		if (bndpsd[i+1] == bndpsd[i] + 256)
		    lowcomp = 384;
		else if (lowcomp && (bndpsd[i+1] < bndpsd[i]))
		    lowcomp -= 64;
	    }
	    fastleak -= fdecay;
	    if (fastleak < bndpsd[i] - fgain)
		fastleak = bndpsd[i] - fgain;
	    slowleak -= sdecay;
	    if (slowleak < bndpsd[i] - sgain)
		slowleak = bndpsd[i] - sgain;
	    excite[i++] = ((fastleak - lowcomp > slowleak) ?
			   fastleak - lowcomp : slowleak);
	}
	if (i > j)	// lfe channel
	    goto done_excite;

	do {
	    if (bndpsd[i+1] == bndpsd[i] + 256)
		lowcomp = 320;
	    else if (lowcomp && (bndpsd[i+1] < bndpsd[i]))
		lowcomp -= 64;
	    fastleak -= fdecay;
	    if (fastleak < bndpsd[i] - fgain)
		fastleak = bndpsd[i] - fgain;
	    slowleak -= sdecay;
	    if (slowleak < bndpsd[i] - sgain)
		slowleak = bndpsd[i] - sgain;
	    excite[i++] = ((fastleak - lowcomp > slowleak) ?
			   fastleak - lowcomp : slowleak);
	} while (i < 20);

	while (lowcomp > 128) {		// two iterations maximum
	    lowcomp -= 128;
	    fastleak -= fdecay;
	    if (fastleak < bndpsd[i] - fgain)
		fastleak = bndpsd[i] - fgain;
	    slowleak -= sdecay;
	    if (slowleak < bndpsd[i] - sgain)
		slowleak = bndpsd[i] - sgain;
	    excite[i++] = ((fastleak - lowcomp > slowleak) ?
			   fastleak - lowcomp : slowleak);
	}
    }

    do {
	fastleak -= fdecay;
	if (fastleak < bndpsd[i] - fgain)
	    fastleak = bndpsd[i] - fgain;
	slowleak -= sdecay;
	if (slowleak < bndpsd[i] - sgain)
	    slowleak = bndpsd[i] - sgain;
	excite[i++] = (fastleak > slowleak) ? fastleak : slowleak;
    } while (i <= j);

done_excite:

    ba_compute_mask(start, end, fscod, ba->deltbae, ba->deltnseg,
		    ba->deltoffst, ba->deltba, ba->deltlen, excite, mask);
    ba_compute_bap(start, end, snroffset, exp, mask, bap);
}


static void ba_compute_mask(int16_t start, int16_t end, uint16_t fscod,
			    uint16_t deltbae, uint16_t deltnseg,
			    uint16_t deltoffst[], uint16_t deltba[],
			    uint16_t deltlen[], int16_t excite[],
			    int16_t mask[])
{
    int bin,k;
    int16_t bndstrt;
    int16_t bndend;
    int16_t delta;

    bndstrt = masktab[start]; 
    bndend = masktab[end - 1] + 1; 

    /* Compute the masking curve */

    for (bin = bndstrt; bin < bndend; bin++) { 
	if (bndpsd[bin] < dbknee) 
	    excite[bin] += ((dbknee - bndpsd[bin]) >> 2); 
	mask[bin] = max(excite[bin], hth[fscod][bin]);
    }
	
    /* Perform delta bit modulation if necessary */
    if ((deltbae == DELTA_BIT_REUSE) || (deltbae == DELTA_BIT_NEW)) { 
	int16_t band = 0; 
	int16_t seg = 0; 
		
	for (seg = 0; seg < deltnseg+1; seg++) { 
	    band += deltoffst[seg]; 
	    if (deltba[seg] >= 4) 
		delta = (deltba[seg] - 3) << 7;
	    else 
		delta = (deltba[seg] - 4) << 7;
			
	    for (k = 0; k < deltlen[seg]; k++) { 
		mask[band] += delta; 
		band++; 
	    } 
	} 
    }
}

static void ba_compute_bap(int16_t start, int16_t end, int16_t snroffset,
			   int8_t exps[], int16_t mask[], int16_t bap[])
{
    int i,j,k;
    int16_t lastbin = 0;
    int16_t address = 0;

    /* Compute the bit allocation pointer for each bin */
    i = start; 
    j = masktab[start]; 

    do { 
	lastbin = min(bndtab[j] + bndsz[j], end); 
	mask[j] -= snroffset; 
	mask[j] -= floor; 
		
	if (mask[j] < 0) 
	    mask[j] = 0; 

	mask[j] &= 0x1fe0;
	mask[j] += floor; 
	for (k = i; k < lastbin; k++) { 
	    address = (3072 - 128 * exps[i] - mask[j]) >> 5; 
	    address = min(63, max(0, address)); 
	    bap[i] = baptab[address]; 
	    i++; 
	} 
	j++; 
    } while (end > lastbin);
}

static int16_t calc_lowcomp(int16_t a,int16_t b0,int16_t b1,int16_t bin) 
{
    if (bin < 7) { 
	if ((b0 + 256) == b1)
	    a = 384; 
	else if (b0 > b1) 
	    a = max(0, a - 64); 
    } else if (bin < 20) { 
	if ((b0 + 256) == b1) 
	    a = 320; 
	else if (b0 > b1) 
	    a = max(0, a - 64) ; 
    } else  
	a = max(0, a - 128); 
	
    return(a);
}

