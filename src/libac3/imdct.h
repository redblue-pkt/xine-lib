/* 
 *  imdct.h
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
 *
 */

#include "cmplx.h"

void imdct_init(void);

void imdct (bsi_t *bsi,audblk_t *audblk, stream_samples_t samples, 
	    int16_t *s16_samples, dm_par_t *dm_par);

void fft_64p (complex_t *);
void imdct_do_512 (float data[],float delay[]);
void imdct_do_512_nol (float data[], float delay[]);
void imdct_do_256 (float data[],float delay[]);

