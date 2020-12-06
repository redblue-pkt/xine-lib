/**
 * Copyright (c) 2001, 2002, 2003 Billy Biggs <vektor@dumbterm.net>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>

#if HAVE_INTTYPES_H
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#include "speedy.h"
#include "deinterlace.h"
#include "pulldown.h"
#include "tvtime.h"

/**
 * This is how many predictions have to be incorrect before we fall back to
 * video mode.  Right now, if we mess up, we jump to video mode immediately.
 */
#define PULLDOWN_ERROR_THRESHOLD 2


/**
 * Explination of the loop:
 *
 * We want to build frames so that they look like this:
 *  Top field:      Bot field:
 *     Copy            Double
 *     Interp          Copy
 *     Copy            Interp
 *     Interp          Copy
 *     Copy            --
 *     --              --
 *     --              --
 *     Copy            Interp
 *     Interp          Copy
 *     Copy            Interp
 *     Double          Copy
 *
 *  So, say a frame is n high.
 *  For the bottom field, the first scanline is blank (special case).
 *  For the top field, the final scanline is blank (special case).
 *  For the rest of the scanlines, we alternate between Copy then Interpolate.
 *
 *  To do the loop, I go 'Interp then Copy', and handle the first copy
 *  outside the loop for both top and bottom.
 *  The top field therefore handles n-2 scanlines in the loop.
 *  The bot field handles n-2 scanlines in the loop.
 *
 * What we pass to the deinterlacing routines:
 *
 * Each deinterlacing routine can require data from up to four fields.
 * The current field is being output is Field 4:
 *
 * | Field 3 | Field 2 | Field 1 | Field 0 |
 * |         |   T2    |         |   T0    |
 * |   M3    |         |    M1   |         |
 * |         |   B2    |         |   B0    |
 * |  NX3    |         |   NX1   |         |
 *
 * So, since we currently get frames not individual fields from V4L, there
 * are two possibilities for where these come from:
 *
 * CASE 1: Deinterlacing the top field:
 * | Field 4 | Field 3 | Field 2 | Field 1 | Field 0 |
 * |   T4    |         |   T2    |         |   T0    |
 * |         |   M3    |         |    M1   |         |
 * |   B4    |         |   B2    |         |   B0    |
 *  [--  secondlast --] [--  lastframe  --] [--  curframe   --]
 *
 * CASE 2: Deinterlacing the bottom field:
 * | Field 4 | Field 3 | Field 2 | Field 1 | Field 0 |
 * |   T4    |         |   T2    |         |   T0    |
 * |         |   M3    |         |    M1   |         |
 * |   B4    |         |   B2    |         |   B0    |
 * ndlast --] [--  lastframe  --] [--  curframe   --]
 *
 * So, in case 1, we need the previous 2 frames as well as the current
 * frame, and in case 2, we only need the previous frame, since the
 * current frame contains both Field 3 and Field 4.
 */
static void pulldown_merge_fields( uint8_t *output,
                                   uint8_t *topfield,
                                   uint8_t *botfield,
                                   int width,
                                   int frame_height,
                                   int fieldstride,
                                   int outstride )
{
    int i;

    for( i = 0; i < frame_height; i++ ) {
        uint8_t *curoutput = output + (i * outstride);

        if( i & 1 ) {
            blit_packed422_scanline( curoutput, botfield + ((i / 2) * fieldstride), width );
        } else {
            blit_packed422_scanline( curoutput, topfield + ((i / 2) * fieldstride), width );
        }
    }
}

static void calculate_pulldown_score_vektor( tvtime_t *tvtime,
                                             uint8_t *curframe,
                                             uint8_t *lastframe,
                                             int instride,
                                             int frame_height,
                                             int width )
{
    int i;

    tvtime->last_topdiff = 0;
    tvtime->last_botdiff = 0;

    for( i = 0; i < frame_height; i++ ) {

        if( i > 40 && (i & 3) == 0 && i < frame_height - 40 ) {
            tvtime->last_topdiff += diff_factor_packed422_scanline( curframe + (i*instride),
                                                                    lastframe + (i*instride), width );
            tvtime->last_botdiff += diff_factor_packed422_scanline( curframe + (i*instride) + instride,
                                                                    lastframe + (i*instride) + instride,
                                                                    width );
        }
    }
}


int tvtime_build_deinterlaced_frame( tvtime_t *tvtime, uint8_t *output,
                                             uint8_t *curframe,
                                             uint8_t *lastframe,
                                             uint8_t *secondlastframe,
                                             int bottom_field, int second_field,
                                             int width,
                                             int frame_height,
                                             int instride,
                                             int outstride )
{

    if( tvtime->pulldown_alg != PULLDOWN_VEKTOR ) {
        /* If we leave vektor pulldown mode, lose our state. */
        tvtime->filmmode = 0;
    }

    if( tvtime->pulldown_alg == PULLDOWN_VEKTOR ) {
        /* Make pulldown phase decisions every top field. */
        if( !bottom_field ) {
            int predicted;

            predicted = tvtime->pdoffset << 1;
            if( predicted > PULLDOWN_SEQ_DD ) predicted = PULLDOWN_SEQ_AA;

            calculate_pulldown_score_vektor( tvtime, curframe, lastframe,
                                             instride, frame_height, width );
            tvtime->pdoffset = determine_pulldown_offset_short_history_new( tvtime->last_topdiff,
                                                                            tvtime->last_botdiff,
                                                                            1, predicted );

            /* 3:2 pulldown state machine. */
            if( !tvtime->pdoffset ) {
                /* No pulldown offset applies, drop out of pulldown immediately. */
                tvtime->pdlastbusted = 0;
                tvtime->pderror = tvtime->pulldown_error_wait;
            } else if( tvtime->pdoffset != predicted ) {
                if( tvtime->pdlastbusted ) {
                    tvtime->pdlastbusted--;
                    tvtime->pdoffset = predicted;
                } else {
                    tvtime->pderror = tvtime->pulldown_error_wait;
                }
            } else {
                if( tvtime->pderror ) {
                    tvtime->pderror--;
                }

                if( !tvtime->pderror ) {
                    tvtime->pdlastbusted = PULLDOWN_ERROR_THRESHOLD;
                }
            }


            if( !tvtime->pderror ) {
                /* We're in pulldown, reverse it. */
                if( !tvtime->filmmode ) {
                    printf( "Film mode enabled.\n" );
                    tvtime->filmmode = 1;
                }

                if( pulldown_drop( tvtime->pdoffset, 0 ) )
                  return 0;

                if( pulldown_source( tvtime->pdoffset, 0 ) ) {
                    pulldown_merge_fields( output, lastframe, lastframe + instride,
                                           width, frame_height, instride*2, outstride );
                } else {
                    pulldown_merge_fields( output, curframe, lastframe + instride,
                                           width, frame_height, instride*2, outstride );
                }

                return 1;
            } else {
                if( tvtime->filmmode ) {
                    printf( "Film mode disabled.\n" );
                    tvtime->filmmode = 0;
                }
            }
        } else if( !tvtime->pderror ) {
            if( pulldown_drop( tvtime->pdoffset, 1 ) )
                return 0;

            if( pulldown_source( tvtime->pdoffset, 1 ) ) {
                pulldown_merge_fields( output, curframe, lastframe + instride,
                                       width, frame_height, instride*2, outstride );
            } else {
                pulldown_merge_fields( output, curframe, curframe + instride,
                                       width, frame_height, instride*2, outstride );
            }

            return 1;
        }
    }

    if( !tvtime->curmethod->scanlinemode ) {
        deinterlace_frame_data_t data;

        data.f0 = curframe;
        data.f1 = lastframe;
        data.f2 = secondlastframe;

        tvtime->curmethod->deinterlace_frame( output, outstride, &data, bottom_field, second_field,
                                      width, frame_height );

    } else {
        int loop_size, bytes_left;
        uint8_t *f3, *f4;

        if (frame_height < 8) {
            /* should not happen */
            while (frame_height-- > 0) {
                blit_packed422_scanline (output, curframe, width);
                curframe += instride;
                output += outstride;
            }
        } else {

            if (bottom_field) {
                /* Advance frame pointers to the next input line. */
                curframe += instride;
                lastframe += instride;
                secondlastframe += instride;
                /* Double the top scanline a scanline. */
                blit_packed422_scanline (output, curframe, width);
                output += outstride;
            }

            /* Copy a scanline. */
            blit_packed422_scanline (output, curframe, width);
            output += outstride;

            if (second_field) {
                f3 = curframe;
                f4 = lastframe;
            } else {
                f3 = lastframe;
                f4 = secondlastframe;
            }

            /* Something is wrong here. -Billy */
            loop_size = ((frame_height - 6) / 2);
            bytes_left = (frame_height - 5) * instride;

            {
                deinterlace_scanline_data_t data;

                data.bottom_field = bottom_field;
                data.bytes_left = bytes_left;
                data.t0  = curframe;
                data.b0  = curframe + instride * 2;
                data.tt1 =
                data.m1  = f3 + instride;
                data.bb1 = f3 + instride * 3;
                data.t2  = lastframe;
                data.b2  = lastframe + instride * 2;
                data.tt3 =
                data.m3  = f4 + instride;
                data.bb3 = f4 + instride * 3;
                tvtime->curmethod->interpolate_scanline (output, &data, width);
                output += outstride;

                data.tt0 = curframe;
                data.m0  = curframe + instride * 2;
                data.bb0 = curframe + instride * 4;
                data.t1  = f3 + instride;
                data.b1  = f3 + instride * 3;
                data.tt2 = lastframe;
                data.t2  = f4 + instride;
                data.m2  = lastframe + instride * 2;
                data.b2  = f4 + instride * 3;
                data.bb2 = lastframe + instride * 4;
                tvtime->curmethod->copy_scanline (output, &data, width);
                curframe += instride * 2;
                lastframe += instride * 2;
                f3 += instride * 2;
                f4 += instride * 2;
                bytes_left -= instride * 2;
                output += outstride;
            }

            while (loop_size-- > 0) {
                deinterlace_scanline_data_t data;

                data.bottom_field = bottom_field;
                data.bytes_left = bytes_left;
                data.t0  = curframe;
                data.b0  = curframe + instride * 2;
                data.tt1 = f3 - instride;
                data.m1  = f3 + instride;
                data.bb1 = f3 + instride * 3;
                data.t2  = lastframe;
                data.b2  = lastframe + instride * 2;
                data.tt3 = f4 - instride;
                data.m3  = f4 + instride;
                data.bb3 = f4 + instride * 3;
                tvtime->curmethod->interpolate_scanline (output, &data, width);
                output += outstride;

                data.tt0 = curframe;
                data.m0  = curframe + instride * 2;
                data.bb0 = curframe + instride * 4;
                data.t1  = f3 + instride;
                data.b1  = f3 + instride * 3;
                data.tt2 = lastframe;
                data.t2  = f4 + instride;
                data.m2  = lastframe + instride * 2;
                data.b2  = f4 + instride * 3;
                data.bb2 = lastframe + instride * 4;
                tvtime->curmethod->copy_scanline (output, &data, width);
                curframe += instride * 2;
                lastframe += instride * 2;
                f3 += instride * 2;
                f4 += instride * 2;
                bytes_left -= instride * 2;
                output += outstride;
            }

            {
                deinterlace_scanline_data_t data;

                data.bottom_field = bottom_field;
                data.bytes_left = bytes_left;
                data.t0  = curframe;
                data.b0  = curframe + instride * 2;
                data.tt1 = f3 - instride;
                data.m1  =
                data.bb1 = f3 + instride;
                data.t2  = lastframe;
                data.b2  = lastframe + instride * 2;
                data.tt3 = f4 - instride;
                data.m3  =
                data.bb3 = f4 + instride;
                tvtime->curmethod->interpolate_scanline (output, &data, width);
                output += outstride;

                data.tt0 = curframe;
                data.m0  =
                data.bb0 = curframe + instride * 2;
                data.t1  =
                data.b1  = f3 + instride;
                data.tt2 = lastframe;
                data.t2  =
                data.b2  = f4 + instride;
                data.m2  =
                data.bb2 = lastframe + instride * 2;
                tvtime->curmethod->copy_scanline (output, &data, width);
            }

            if (!bottom_field) {
                curframe += instride * 2;
                output += outstride;
                /* Double the bottom scanline. */
                blit_packed422_scanline (output, curframe, width);
            }
        }
    }

    return 1;
}


int tvtime_build_copied_field( tvtime_t *tvtime, uint8_t *output,
                                       uint8_t *curframe,
                                       int bottom_field,
                                       int width,
                                       int frame_height,
                                       int instride,
                                       int outstride )
{
    int scanline = 0;
    int i;

    (void)tvtime;

    if( bottom_field ) {
        /* Advance frame pointers to the next input line. */
        curframe += instride;
    }

    /* Copy a scanline. */
    // blit_packed422_scanline( output, curframe, width );
    quarter_blit_vertical_packed422_scanline( output, curframe + (instride*2), curframe, width );

    curframe += instride * 2;
    output += outstride;
    scanline += 2;

    for( i = ((frame_height - 2) / 2); i; --i ) {
        /* Copy/interpolate a scanline. */
        if( bottom_field ) {
            // interpolate_packed422_scanline( output, curframe, curframe - (instride*2), width );
            quarter_blit_vertical_packed422_scanline( output, curframe - (instride*2), curframe, width );
        } else {
            // blit_packed422_scanline( output, curframe, width );
            if( i > 1 ) {
                quarter_blit_vertical_packed422_scanline( output, curframe + (instride*2), curframe, width );
            } else {
                blit_packed422_scanline( output, curframe, width );
            }
        }
        curframe += instride * 2;

        output += outstride;
        scanline += 2;
    }

    return 1;
}

tvtime_t *tvtime_new_context(void)
{
  tvtime_t *tvtime;

  tvtime = calloc(1, sizeof(tvtime_t));
  if (!tvtime)
    return NULL;

  tvtime->pulldown_alg = PULLDOWN_NONE;

  tvtime->curmethod = NULL;

  tvtime_reset_context(tvtime);

  return tvtime;
}

void tvtime_reset_context( tvtime_t *tvtime )
{
  tvtime->last_topdiff = 0;
  tvtime->last_botdiff = 0;

  tvtime->pdoffset = PULLDOWN_SEQ_AA;
  tvtime->pderror = tvtime->pulldown_error_wait;
  tvtime->pdlastbusted = 0;
  tvtime->filmmode = 0;
}
