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
static void deinterlace_bob_yuv_mmx( uint8_t *pdst, uint8_t *psrc[],
    int width, int height )
{
#ifdef ARCH_X86

  int Line;
  uint64_t *YVal1;
  uint64_t *YVal2;
  uint64_t *YVal3;
  uint64_t *Dest;
  uint8_t* pEvenLines = psrc[0];
  uint8_t* pOddLines = psrc[0]+width;
  int LineLength = width;
  int SourcePitch = width * 2;
  int IsOdd = 1;
  long EdgeDetect = 625;
  long JaggieThreshold = 73;

  int n;

  uint64_t qwEdgeDetect;
  uint64_t qwThreshold;
  const uint64_t Mask = 0xfefefefefefefefe;
  const uint64_t YMask = 0x00ff00ff00ff00ff;

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
      YVal1 = (uint64_t *)(pOddLines + Line * SourcePitch);
      YVal2 = (uint64_t *)(pEvenLines + (Line + 1) * SourcePitch);
      YVal3 = (uint64_t *)(pOddLines + (Line + 1) * SourcePitch);
      Dest = (uint64_t *)(pdst + (Line * 2 + 2) * LineLength);
    }
    else
    {
      YVal1 = (uint64_t *)(pEvenLines + Line * SourcePitch);
      YVal2 = (uint64_t *)(pOddLines + Line * SourcePitch);
      YVal3 = (uint64_t *)(pEvenLines + (Line + 1) * SourcePitch);
      Dest = (uint64_t *)(pdst + (Line * 2 + 1) * LineLength);
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
                      pOddLines + (height - 1) * SourcePitch,
                      LineLength);
  }

  // clear out the MMX registers ready for doing floating point
  // again
  emms();
#endif
}

/* Deinterlace the latest field, with a tendency to weave rather than bob.
   Good for high detail on low-movement scenes.
   NOT FINISHED! WEIRD OUTPUT!!!
*/
static int deinterlace_weave_yuv_mmx( uint8_t *pdst, uint8_t *psrc[],
    int width, int height )
{
#ifdef ARCH_X86

  int Line;
  uint64_t *YVal1;
  uint64_t *YVal2;
  uint64_t *YVal3;
  uint64_t *YVal4;
  uint64_t *Dest;
  uint8_t* pEvenLines = psrc[0];
  uint8_t* pOddLines = psrc[0]+width;
  uint8_t* pPrevLines;

  int LineLength = width;
  int SourcePitch = width * 2;
  int IsOdd = 1;

  long TemporalTolerance = 300;
  long SpatialTolerance = 600;
  long SimilarityThreshold = 25;

  const uint64_t YMask    = 0x00ff00ff00ff00ff;

  int n;

  uint64_t qwSpatialTolerance;
  uint64_t qwTemporalTolerance;
  uint64_t qwThreshold;
  const uint64_t Mask = 0xfefefefefefefefe;


  // Make sure we have all the data we need.
  if ( psrc[0] == NULL || psrc[1] == NULL )
    return 0;

  if (IsOdd)
    pPrevLines = psrc[1] + width;
  else
    pPrevLines = psrc[1];

  // Since the code uses MMX to process 4 pixels at a time, we need our constants
  // to be represented 4 times per quadword.
  qwSpatialTolerance = SpatialTolerance;
  qwSpatialTolerance += (qwSpatialTolerance << 48) + (qwSpatialTolerance << 32) + (qwSpatialTolerance << 16);
  qwTemporalTolerance = TemporalTolerance;
  qwTemporalTolerance += (qwTemporalTolerance << 48) + (qwTemporalTolerance << 32) + (qwTemporalTolerance << 16);
  qwThreshold = SimilarityThreshold;
  qwThreshold += (qwThreshold << 48) + (qwThreshold << 32) + (qwThreshold << 16);

  // copy first even line no matter what, and the first odd line if we're
  // processing an even field.
  memcpy(pdst, pEvenLines, LineLength);
  if (!IsOdd)
    memcpy(pdst + LineLength, pOddLines, LineLength);

  height = height / 2;
  for (Line = 0; Line < height - 1; ++Line)
  {
    if (IsOdd)
    {
      YVal1 = (uint64_t *)(pEvenLines + Line * SourcePitch);
      YVal2 = (uint64_t *)(pOddLines + Line * SourcePitch);
      YVal3 = (uint64_t *)(pEvenLines + (Line + 1) * SourcePitch);
      YVal4 = (uint64_t *)(pPrevLines + Line * SourcePitch);
      Dest = (uint64_t *)(pdst + (Line * 2 + 1) * LineLength);
    }
    else
    {
      YVal1 = (uint64_t *)(pOddLines + Line * SourcePitch);
      YVal2 = (uint64_t *)(pEvenLines + (Line + 1) * SourcePitch);
      YVal3 = (uint64_t *)(pOddLines + (Line + 1) * SourcePitch);
      YVal4 = (uint64_t *)(pPrevLines + (Line + 1) * SourcePitch);
      Dest = (uint64_t *)(pdst + (Line * 2 + 2) * LineLength);
    }

    // For ease of reading, the comments below assume that we're operating on an odd
    // field (i.e., that bIsOdd is true).  The exact same processing is done when we
    // operate on an even field, but the roles of the odd and even fields are reversed.
    // It's just too cumbersome to explain the algorithm in terms of "the next odd
    // line if we're doing an odd field, or the next even line if we're doing an
    // even field" etc.  So wherever you see "odd" or "even" below, keep in mind that
    // half the time this function is called, those words' meanings will invert.

    // Copy the even scanline below this one to the overlay buffer, since we'll be
    // adapting the current scanline to the even lines surrounding it.  The scanline
    // above has already been copied by the previous pass through the loop.
    memcpy((char *)Dest + LineLength, YVal3, LineLength);

    n = LineLength >> 3;
    while( n-- )
    {
      movq_m2r ( *YVal1++, mm0 );    // mm0 = E1
      movq_m2r ( *YVal2++, mm1 );    // mm1 = O
      movq_m2r ( *YVal3++, mm2 );    // mm2 = E2

      movq_r2r ( mm0, mm3 );       // mm3 = intensity(E1)
      movq_r2r ( mm1, mm4 );       // mm4 = intensity(O)
      movq_r2r ( mm2, mm6 );       // mm6 = intensity(E2)

      pand_m2r ( *&YMask, mm3 );
      pand_m2r ( *&YMask, mm4 );
      pand_m2r ( *&YMask, mm6 );

      // Average E1 and E2 for interpolated bobbing.
      // leave result in mm0
      pand_m2r ( *&Mask, mm0 ); // mm0 = E1 with lower chroma bit stripped off
      pand_m2r ( *&Mask, mm2 ); // mm2 = E2 with lower chroma bit stripped off
      psrlw_i2r ( 01, mm0 );    // mm0 = E1 / 2
      psrlw_i2r ( 01, mm2 );    // mm2 = E2 / 2
      paddb_r2r ( mm2, mm0 );

      // The meat of the work is done here.  We want to see whether this pixel is
      // close in luminosity to ANY of: its top neighbor, its bottom neighbor,
      // or its predecessor.  To do this without branching, we use MMX's
      // saturation feature, which gives us Z(x) = x if x>=0, or 0 if x<0.
      //
      // The formula we're computing here is
      //		Z(ST - (E1 - O) ^ 2) + Z(ST - (E2 - O) ^ 2) + Z(TT - (Oold - O) ^ 2)
      // where ST is spatial tolerance and TT is temporal tolerance.  The idea
      // is that if a pixel is similar to none of its neighbors, the resulting
      // value will be pretty low, probably zero.  A high value therefore indicates
      // that the pixel had a similar neighbor.  The pixel in the same position
      // in the field before last (Oold) is considered a neighbor since we want
      // to be able to display 1-pixel-high horizontal lines.

      movq_m2r ( *&qwSpatialTolerance, mm7 );
      movq_r2r ( mm3, mm5 );     // mm5 = E1
      psubsw_r2r ( mm4, mm5 );   // mm5 = E1 - O
      psraw_i2r ( 1, mm5 );
      pmullw_r2r ( mm5, mm5 );   // mm5 = (E1 - O) ^ 2
      psubusw_r2r ( mm5, mm7 );  // mm7 = ST - (E1 - O) ^ 2, or 0 if that's negative

      movq_m2r ( *&qwSpatialTolerance, mm3 );
      movq_r2r ( mm6, mm5 );    // mm5 = E2
      psubsw_r2r ( mm4, mm5 );  // mm5 = E2 - O
      psraw_i2r ( 1, mm5 );
      pmullw_r2r ( mm5, mm5 );  // mm5 = (E2 - O) ^ 2
      psubusw_r2r ( mm5, mm3 ); // mm0 = ST - (E2 - O) ^ 2, or 0 if that's negative
      paddusw_r2r ( mm3, mm7 ); // mm7 = (ST - (E1 - O) ^ 2) + (ST - (E2 - O) ^ 2)

      movq_m2r ( *&qwTemporalTolerance, mm3 );
      movq_m2r ( *YVal4++, mm5 ); // mm5 = Oold
      pand_m2r ( *&YMask, mm5 );
      psubsw_r2r ( mm4, mm5 );  // mm5 = Oold - O
      psraw_i2r ( 1, mm5 ); // XXX
      pmullw_r2r ( mm5, mm5 );  // mm5 = (Oold - O) ^ 2
      psubusw_r2r ( mm5, mm3 ); // mm0 = TT - (Oold - O) ^ 2, or 0 if that's negative
      paddusw_r2r ( mm3, mm7 ); // mm7 = our magic number

      // Now compare the similarity totals against our threshold.  The pcmpgtw
      // instruction will populate the target register with a bunch of mask bits,
      // filling words where the comparison is true with 1s and ones where it's
      // false with 0s.  A few ANDs and NOTs and an OR later, we have bobbed
      // values for pixels under the similarity threshold and weaved ones for
      // pixels over the threshold.

      pcmpgtw_m2r( *&qwThreshold, mm7 ); // mm7 = 0xffff where we're greater than the threshold, 0 elsewhere
      movq_r2r ( mm7, mm6 );  // mm6 = 0xffff where we're greater than the threshold, 0 elsewhere
      pand_r2r ( mm1, mm7 );  // mm7 = weaved data where we're greater than the threshold, 0 elsewhere
      pandn_r2r ( mm0, mm6 ); // mm6 = bobbed data where we're not greater than the threshold, 0 elsewhere
      por_r2r ( mm6, mm7 );   // mm7 = bobbed and weaved data

      movq_r2m ( mm7, *Dest++ );
    }
  }

  // Copy last odd line if we're processing an odd field.
  if (IsOdd)
  {
    memcpy(pdst + (height * 2 - 1) * LineLength,
                      pOddLines + (height - 1) * SourcePitch,
                      LineLength);
  }

  // clear out the MMX registers ready for doing floating point
  // again
  emms();

  return 1;
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

/* generic YUV deinterlacer
   pdst -> pointer to destination bitmap
   psrc -> array of pointers to source bitmaps ([0] = most recent)
   width,height -> dimension for bitmaps
   method -> DEINTERLACE_xxx
*/

void deinterlace_yuv( uint8_t *pdst, uint8_t *psrc[],
    int width, int height, int method )
{
  switch( method ) {
    case DEINTERLACE_NONE:
      memcpy(pdst,psrc[0],width*height);
      break;
    case DEINTERLACE_BOB:
      if( check_for_mmx() )
        deinterlace_bob_yuv_mmx(pdst,psrc,width,height);
      else /* FIXME: provide an alternative? */
        abort_mmx_missing();
      break;
    case DEINTERLACE_WEAVE:
      if( check_for_mmx() )
      {
        if( !deinterlace_weave_yuv_mmx(pdst,psrc,width,height) )
          memcpy(pdst,psrc[0],width*height);
      }
      else /* FIXME: provide an alternative? */
        abort_mmx_missing();
      break;
  }
}
