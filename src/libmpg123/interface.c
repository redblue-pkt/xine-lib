/* 
 * Copyright (C) 2000 the xine project
 * 
 * This file is part of xine, a unix video player.
 * The code is heavily based on libmpeg from mpg123
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
 * $Id: interface.c,v 1.4 2001/05/27 23:48:12 guenter Exp $
 */

#include <stdlib.h>
#include <stdio.h>

#include "mpg123.h"
#include "mpglib.h"

void mpg_audio_reset (mpgaudio_t *mp) {

  mp->framesize             = 0;
  mp->framesize_old         = -1;
  mp->bsize                 =  0;
  mp->fr.single             = -1;
  mp->bsnum                 =  0;
  mp->synth_bo              =  1;
  mp->is_output_initialized =  0;
  mp->sample_rate_device    =  0;
  mp->header                =  0;
}

mpgaudio_t *mpg_audio_init (ao_functions_t *ao_output) 
{
  mpgaudio_t *mp;

  mp = malloc (sizeof(struct mpstr));
  memset(mp, 0, sizeof(struct mpstr));

  make_decode_tables(32767);
  init_layer2();
  init_layer3(SBLIMIT);

  mp->ao_output = ao_output;

  mpg_audio_reset (mp);

  return mp;
}

void mpg_audio_close (mpgaudio_t *mpg) {

  free (mpg);

}

int head_check(struct mpstr *mp)
{
  if( (mp->header & 0xffe00000) != 0xffe00000)
    return 0;
  if(!((mp->header>>17)&3))
    return 0;
  if( ((mp->header>>12)&0xf) == 0xf)
    return 0;
  if( ((mp->header>>10)&0x3) == 0x3 )
    return 0;
  return 1;
}

void mpg_audio_decode_data (mpgaudio_t *mp, uint8_t *data, uint8_t *data_end,
			    uint32_t pts) 
{

  while (1) {
    /* sync */
    if(mp->framesize == 0) {
      
      /* printf ("mpg123: looking for header\n"); */

      while (!head_check (mp)) {
	    
	if (data == data_end) 
	  return;
	    
	mp->header = (mp->header << 8) | *data;
	data++;
      }

      /* decode header */

      decode_header(&mp->fr,mp->header);

      mp->framesize = mp->fr.framesize;
      mp->bsize = 0;
      mpg123_wordpointer = mp->bsspace[mp->bsnum] + 512;
      mp->bsnum = (mp->bsnum + 1) & 0x1;
      mpg123_bitindex = 0;
      mp->pts = pts;
      pts = 0;
    }
  
    /* copy data to bsspace */
    while (mp->bsize<mp->framesize) {

      if (data == data_end)
	return;

      *(mpg123_wordpointer + mp->bsize) = *data;
      data++;
      mp->bsize++;
    }

    if(mp->fr.error_protection)
      getbits(16);
    
    switch(mp->fr.lay) {
    case 1:
      do_layer1(mp);
      break;
    case 2:
      do_layer2(mp);
      break;
    case 3:
      do_layer3(mp);
      break;
    }

    mp->framesize_old = mp->framesize;
    mp->framesize = 0;
    mp->header = 0;
  }
}

int set_pointer(mpgaudio_t *mp, long backstep)
{
  unsigned char *bsbufold;
  if(mp->framesize_old < 0 && backstep > 0) {
    fprintf(stderr,"Can't step back %ld!\n",backstep);
    return 0;
  }
  bsbufold = mp->bsspace[mp->bsnum] + 512;
  mpg123_wordpointer -= backstep;
  if (backstep)
    memcpy(mpg123_wordpointer,bsbufold+mp->framesize_old-backstep,backstep);
  mpg123_bitindex = 0;
  return 1;
}
