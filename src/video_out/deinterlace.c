 /*
 * Copyright (C) 2001 the xine project
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
 * Deinterlace routines by Miguel Freitas
 * based of DScaler project sources (deinterlace.sourceforge.net)
 *
 * Currently only available for Xv driver and MMX extensions
 *
 */

#include <stdio.h>
#include <string.h>
#include "xine_internal.h"
#include "cpu_accel.h"
#include "deinterlace.h"

/*
   DeinterlaceFieldBob algorithm
   Based on Virtual Dub plugin by Gunnar Thalin
   MMX asm version from dscaler project (deinterlace.sourceforge.net)
   Linux version for Xine player by Miguel Freitas
   Todo: use a MMX optimized memcpy
*/
static void deinterlace_bob_yuv_mmx( uint8_t *pdst, uint8_t *psrc,
    int width, int height )
{
#ifdef ARCH_X86

  int Line;
  long long* YVal1;
  long long* YVal2;
  long long* YVal3;
  long long* Dest;
  uint8_t* pEvenLines = psrc;
  uint8_t* pOddLines = psrc+width;
  int LineLength = width;
  int Pitch = width * 2;
  int IsOdd = 1;
  long EdgeDetect = 625;
  long JaggieThreshold = 73;

  int n;

  unsigned long long qwEdgeDetect;
  unsigned long long qwThreshold;
  const unsigned long long Mask = 0xfefefefefefefefe;
  const unsigned long long YMask = 0x00ff00ff00ff00ff;

  qwEdgeDetect = EdgeDetect;
  qwEdgeDetect += (qwEdgeDetect << 48) + (qwEdgeDetect << 32) + (qwEdgeDetect << 16);
  qwThreshold = JaggieThreshold;
  qwThreshold += (qwThreshold << 48) + (qwThreshold << 32) + (qwThreshold << 16);


  // copy first even line no matter what, and the first odd line if we're
  // processing an odd field.
  memcpy(pdst, pEvenLines, LineLength);
  if (IsOdd)
    memcpy(pdst + LineLength, pOddLines, LineLength);

  height = height / 2;
  for (Line = 0; Line < height - 1; ++Line)
  {
    if (IsOdd)
    {
      YVal1 = (long long *)(pOddLines + Line * Pitch);
      YVal2 = (long long *)(pEvenLines + (Line + 1) * Pitch);
      YVal3 = (long long *)(pOddLines + (Line + 1) * Pitch);
      Dest = (long long *)(pdst + (Line * 2 + 2) * LineLength);
    }
    else
    {
      YVal1 = (long long *)(pEvenLines + Line * Pitch);
      YVal2 = (long long *)(pOddLines + Line * Pitch);
      YVal3 = (long long *)(pEvenLines + (Line + 1) * Pitch);
      Dest = (long long *)(pdst + (Line * 2 + 1) * LineLength);
    }

    // For ease of reading, the comments below assume that we're operating on an odd
    // field (i.e., that bIsOdd is true).  The exact same processing is done when we
    // operate on an even field, but the roles of the odd and even fields are reversed.
    // It's just too cumbersome to explain the algorithm in terms of "the next odd
    // line if we're doing an odd field, or the next even line if we're doing an
    // even field" etc.  So wherever you see "odd" or "even" below, keep in mind that
    // half the time this function is called, those words' meanings will invert.

    // Copy the odd line to the overlay verbatim.
    memcpy((char *)Dest + LineLength, YVal3, LineLength);

    n = LineLength >> 3;
    while( n-- )
    {
      movq_m2r (*YVal1++, mm0);
      movq_m2r (*YVal2++, mm1);
      movq_m2r (*YVal3++, mm2);

      // get intensities in mm3 - 4
      movq_r2r ( mm0, mm3 );
      movq_r2r ( mm1, mm4 );
      movq_r2r ( mm2, mm5 );

      pand_m2r ( *&YMask, mm3 );
      pand_m2r ( *&YMask, mm4 );
      pand_m2r ( *&YMask, mm5 );

      // get average in mm0
      pand_m2r ( *&Mask, mm0 );
      pand_m2r ( *&Mask, mm2 );
      psrlw_i2r ( 01, mm0 );
      psrlw_i2r ( 01, mm2 );
      paddw_r2r ( mm2, mm0 );

      // work out (O1 - E) * (O2 - E) / 2 - EdgeDetect * (O1 - O2) ^ 2 >> 12
      // result will be in mm6

      psrlw_i2r ( 01, mm3 );
      psrlw_i2r ( 01, mm4 );
      psrlw_i2r ( 01, mm5 );

      movq_r2r ( mm3, mm6 );
      psubw_r2r ( mm4, mm6 );	//mm6 = O1 - E

      movq_r2r ( mm5, mm7 );
      psubw_r2r ( mm4, mm7 );	//mm7 = O2 - E

      pmullw_r2r ( mm7, mm6 );		// mm6 = (O1 - E) * (O2 - E)

      movq_r2r ( mm3, mm7 );
      psubw_r2r ( mm5, mm7 );		// mm7 = (O1 - O2)
      pmullw_r2r ( mm7, mm7 );	// mm7 = (O1 - O2) ^ 2
      psrlw_i2r ( 12, mm7 );		// mm7 = (O1 - O2) ^ 2 >> 12
      pmullw_m2r ( *&qwEdgeDetect, mm7 );// mm7  = EdgeDetect * (O1 - O2) ^ 2 >> 12

      psubw_r2r ( mm7, mm6 );      // mm6 is what we want

      pcmpgtw_m2r ( *&qwThreshold, mm6 );

      movq_r2r ( mm6, mm7 );

      pand_r2r ( mm6, mm0 );

      pandn_r2r ( mm1, mm7 );

      por_r2r ( mm0, mm7 );

      movq_r2m ( mm7, *Dest++ );
    }
  }

  // Copy last odd line if we're processing an even field.
  if (! IsOdd)
  {
    memcpy(pdst + (height * 2 - 1) * LineLength,
                      pOddLines + (height - 1) * Pitch,
                      LineLength);
  }

  // clear out the MMX registers ready for doing floating point
  // again
  emms();
#endif
}


static int check_for_mmx(void)
{
#ifdef ARCH_X86
static int config_flags = -1;

  if ( config_flags == -1 )
    config_flags = mm_accel();
  if (config_flags & MM_ACCEL_X86_MMX)
    return 1;
  return 0;
#elif
  return 0;
#endif
}

static void abort_mmx_missing(void)
{
  printf("deinterlace: Fatal error, MMX instruction set needed!\n");
  /* FIXME: is it possible to call some "nicer" xine exit function? */
  exit(1);
}

void deinterlace_yuv( uint8_t *pdst, uint8_t *psrc,
    int width, int height, int method )
{
  switch( method ) {
    case DEINTERLACE_NONE:
      memcpy(pdst,psrc,width*height);
      break;
    case DEINTERLACE_BOB:
      if( check_for_mmx() )
          deinterlace_bob_yuv_mmx(pdst,psrc,width,height);
      else /* FIXME: provide an alternative? */
          abort_mmx_missing();
      break;
  }
}
