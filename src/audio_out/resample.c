/* 
 * Copyright (C) 2000 the xine project
 * 
 * This file is part of xine, a unix video player.
 * 
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: resample.c,v 1.1 2001/04/24 20:53:00 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>

/* contributed by paul flinders */

void audio_out_resample_stereo(int16_t* input_samples, uint32_t in_samples, 
			       int16_t* output_samples, uint32_t out_samples)
{
  int osample;
  /* 16+16 fixed point math */
  uint32_t isample = 0;
  uint32_t istep = (in_samples << 16)/out_samples;

#ifdef VERBOSE
  printf ("Audio : resample %d samples to %d\n",
          in_samples, out_samples);
#endif

  for (osample = 0; osample < out_samples - 1; osample++) {
    int  s1;
    int  s2;
    int16_t  os;
    uint32_t t = isample&0xffff;
    
    /* don't "optimize" the (isample >> 16)*2 to (isample >> 15) */
    s1 = input_samples[(isample >> 16)*2];
    s2 = input_samples[(isample >> 16)*2+2];
    
    os = (s1 * (0x10000-t)+ s2 * t) >> 16;
    output_samples[osample * 2] = os;

    s1 = input_samples[(isample >> 16)*2+1];
    s2 = input_samples[(isample >> 16)*2+3];
    
    os = (s1 * (0x10000-t)+ s2 * t) >> 16;
    output_samples[(osample * 2 )+1] = os;
    isample += istep;
  }
  output_samples[out_samples*2-2] = input_samples[in_samples*2-2];
  output_samples[out_samples*2-1] = input_samples[in_samples*2-1];
}

