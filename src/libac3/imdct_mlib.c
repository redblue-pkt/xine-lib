/* 
 *  imdct_mlib.c
 *  Copyright (C) 2001 Håkan Hjort <d95hjort@dtek.chalmers.se>
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
 */


#include "config.h"

#ifdef LIBAC3_MLIB

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <mlib_types.h>
#include <mlib_status.h>
#include <mlib_signal.h>


extern float imdct_window[];

void
imdct_do_512_mlib(float data[], float delay[])
{
	float *buf_real;
	float *buf_imag;
	float *data_ptr;
	float *delay_ptr;
	float *window_ptr;
	float tmp[256] __attribute__ ((__aligned__ (16)));
	int i;
	
	memcpy(tmp, data, 256 * sizeof(float));
	if(mlib_SignalIMDCT_F32(tmp) != MLIB_SUCCESS) {
	  fprintf(stderr, "mediaLib failure\n");
	  exit(-1);
	}
  
	buf_real = tmp;
	buf_imag = tmp + 128;
	data_ptr = data;
	delay_ptr = delay;
	window_ptr = imdct_window;

	/* Window and convert to real valued signal */
	for(i=0; i< 64; i++) 
	{ 
		*data_ptr++ = 2.0f * (-buf_imag[64+i]   * *window_ptr++ + *delay_ptr++); 
		*data_ptr++ = 2.0f * ( buf_real[64-i-1] * *window_ptr++ + *delay_ptr++); 
	}

	for(i=0; i< 64; i++) 
	{ 
		*data_ptr++ = 2.0f * (-buf_real[i]       * *window_ptr++ + *delay_ptr++); 
		*data_ptr++ = 2.0f * ( buf_imag[128-i-1] * *window_ptr++ + *delay_ptr++); 
	}
	
	/* The trailing edge of the window goes into the delay line */
	delay_ptr = delay;

	for(i=0; i< 64; i++) 
	{ 
		*delay_ptr++ = -buf_real[64+i]   * *--window_ptr; 
		*delay_ptr++ =  buf_imag[64-i-1] * *--window_ptr; 
	}

	for(i=0; i<64; i++) 
	{
		*delay_ptr++ =  buf_imag[i]       * *--window_ptr; 
		*delay_ptr++ = -buf_real[128-i-1] * *--window_ptr; 
	}
}

void
imdct_do_256_mlib(float data[], float delay[])
{
	float *buf1_real, *buf1_imag;
	float *buf2_real, *buf2_imag;
	float *data_ptr;
	float *delay_ptr;
	float *window_ptr;
	float tmp[256] __attribute__ ((__aligned__ (16)));
	int i;
	
	memcpy(tmp, data, 256 * sizeof(float));
	if(mlib_SignalIMDCT_F32(tmp) != MLIB_SUCCESS) {
	  fprintf(stderr, "mediaLib failure\n");
	  exit(-1);
	}
  
	buf1_real = tmp;
	buf1_imag = tmp + 64;
	buf2_real = tmp + 128;
	buf2_imag = tmp + 128 + 64;
	data_ptr = data;
	delay_ptr = delay;
	window_ptr = imdct_window;

	/* Window and convert to real valued signal */
	for(i=0; i< 64; i++) 
	{
		*data_ptr++ = 2.0f * (-buf1_imag[i]      * *window_ptr++ + *delay_ptr++);
		*data_ptr++ = 2.0f * ( buf1_real[64-i-1] * *window_ptr++ + *delay_ptr++);
	}

	for(i=0; i< 64; i++) 
	{
		*data_ptr++ = 2.0f * (-buf1_real[i]      * *window_ptr++ + *delay_ptr++);
		*data_ptr++ = 2.0f * ( buf1_imag[64-i-1] * *window_ptr++ + *delay_ptr++);
	}
	
	delay_ptr = delay;

	for(i=0; i< 64; i++) 
	{
		*delay_ptr++ = -buf2_real[i]      * *--window_ptr;
		*delay_ptr++ =  buf2_imag[64-i-1] * *--window_ptr;
	}

	for(i=0; i< 64; i++) 
	{
		*delay_ptr++ =  buf2_imag[i]      * *--window_ptr;
		*delay_ptr++ = -buf2_real[64-i-1] * *--window_ptr;
	}
}

#endif
