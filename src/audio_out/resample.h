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
 * $Id: resample.h,v 1.1 2001/04/24 20:53:00 f1rmb Exp $
 *
 * utilitiy functions for audio drivers
 *
 * FIXME: not all of them are implemented yet
 */

#ifndef HAVE_RESAMPLE_H
#define HAVE_RESAMPLE_H

void audio_out_resample_stereo(int16_t* input_samples, uint32_t in_samples, 
			       int16_t* output_samples, uint32_t out_samples);

void audio_out_resample_mono(int16_t* input_samples, uint32_t in_samples, 
			     int16_t* output_samples, uint32_t out_samples);

void audio_out_resample_4channel(int16_t* input_samples, uint32_t in_samples, 
				 int16_t* output_samples, uint32_t out_samples);

void audio_out_resample_5channel(int16_t* input_samples, uint32_t in_samples, 
				 int16_t* output_samples, uint32_t out_samples);

#endif
