/* 
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: monitor.h,v 1.3 2001/07/18 21:38:17 f1rmb Exp $
 *
 * debug print and profiling functions
 *
 */
 
#ifndef HAVE_MONITOR_H
#define HAVE_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

extern uint32_t xine_debug;

#define VERBOSE        (xine_debug & 0x8000>>1)   // 16384
#define METRONOM       (xine_debug & 0x8000>>2)   //  8192
#define AUDIO          (xine_debug & 0x8000>>3)   //  4096
#define DEMUX          (xine_debug & 0x8000>>4)   //  2048
#define INPUT          (xine_debug & 0x8000>>5)   //  1024
#define VIDEO          (xine_debug & 0x8000>>6)   //   512
#define VPTS           (xine_debug & 0x8000>>7)   //   256
#define MPEG           (xine_debug & 0x8000>>8)   //   128
#define VAVI           (xine_debug & 0x8000>>9)   //    64
#define AC3            (xine_debug & 0x8000>>10)  //    32
#define LOOP           (xine_debug & 0x8000>>11)  //    16
#define GUI            (xine_debug & 0x8000>>12)  //     8

#define perr(FMT,ARGS...) {fprintf(stderr, FMT, ##ARGS);fflush(stderr);}

#ifdef DEBUG

/*
 * Debug stuff
 */

//#define perr(FMT,ARGS...) {fprintf(stderr, FMT, ##ARGS);fflush(stderr);}

#define xprintf(LVL, FMT, ARGS...) {                                          \
                                     if(LVL) {                                \
                                       printf(FMT, ##ARGS);          \
                                     }                                        \
                                   }
/*
 * profiling
 */

void profiler_init ();

void profiler_set_label (int id, char *label);

void profiler_start_count (int id);

void profiler_stop_count (int id);

void profiler_print_results ();

#else /* no DEBUG, release version */

//#define perr(FMT,ARGS...) 

#define xprintf(LVL, FMT, ARGS...) 

#define profiler_init()
#define profiler_set_label(id, label)
#define profiler_start_count(id)
#define profiler_stop_count(id)
#define profiler_print_results()

#endif /* DEBUG*/

#ifdef __cplusplus
}
#endif

#endif /* HAVE_MONITOR_H */
