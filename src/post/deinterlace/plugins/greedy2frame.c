/**
 * Copyright (c) 2000 John Adcock, Tom Barry, Steve Grimm  All rights reserved.
 * port copyright (c) 2003 Miguel Freitas
 *
 * This code is ported from DScaler: http://deinterlace.sf.net/
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
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#if defined (__SVR4) && defined (__sun)
# include <sys/int_types.h>
#else
# include <stdint.h>
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "attributes.h"
#include "xineutils.h"
#include "deinterlace.h"
#include "speedtools.h"
#include "speedy.h"


// debugging feature
// output the value of mm4 at this point which is pink where we will weave
// and green were we are going to bob
// uncomment next line to see this
//#define CHECK_BOBWEAVE

static int GreedyTwoFrameThreshold = 4;
static int GreedyTwoFrameThreshold2 = 8;

#define IS_SSE 1
#include "greedy2frame_template.c"
#undef IS_SSE

static deinterlace_setting_t settings[] =
{
    {
        "Greedy 2 Frame Luma Threshold",
        SETTING_SLIDER,
        &GreedyTwoFrameThreshold,
        4, 0, 128, 1,
        0
    },
    {
        "Greedy 2 Frame Chroma Threshold",
        SETTING_SLIDER,
        &GreedyTwoFrameThreshold2,
        8, 0, 128, 1,
        0
    }
};

static deinterlace_method_t greedymethod =
{
    DEINTERLACE_PLUGIN_API_VERSION,
    "Greedy - 2-frame (DScaler)",
    "Greedy2Frame",
    4,
    MM_ACCEL_X86_MMXEXT,
    0,
    2,
    settings,
    0,
    0,
    0,
    DeinterlaceGreedy2Frame_SSE
};

#ifdef BUILD_TVTIME_PLUGINS
void deinterlace_plugin_init( void )
#else
void greedy2frame_plugin_init( void )
#endif
{
    register_deinterlace_method( &greedymethod );
}

