/*
** FAAD - Freeware Advanced Audio Decoder
** Copyright (C) 2002 M. Bakker
**  
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software 
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: cfft.h,v 1.4 2003/04/12 14:58:46 miguelfreitas Exp $
**/

#ifndef __CFFT_H__
#define __CFFT_H__

#ifdef __cplusplus
extern "C" {
#endif


void cfftf(cfft_info *cfft, complex_t *c);
void cfftb(cfft_info *cfft, complex_t *c);
cfft_info *cffti(uint16_t n);
void cfftu(cfft_info *cfft);


/* static declarations moved to avoid compiler warnings [MF] */


#ifdef __cplusplus
}
#endif
#endif
