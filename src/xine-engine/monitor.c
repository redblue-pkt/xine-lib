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
 * $Id: monitor.c,v 1.1 2001/04/18 22:36:04 f1rmb Exp $
 *
 * debug print and profiling functions - implementation
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "monitor.h"
#include <stdio.h>

#define MAX_ID 5

#ifdef DEBUG

long long int profiler_times[MAX_ID+1] ;
long long int profiler_start[MAX_ID+1] ;
char * profiler_label[MAX_ID+1] ;

void profiler_init () {
  int i;
  for (i=0; i<MAX_ID; i++) {
    profiler_times[i] = 0;
    profiler_start[i] = 0;
    profiler_label[i] = "??";
  }
}

void profiler_set_label (int id, char *label) {
  profiler_label[id] = label;
}

#ifdef ARCH_X86
__inline__ unsigned long long int rdtsc()
{
  unsigned long long int x;
  __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));     
  return x;
}
#endif

void profiler_start_count (int id) {
#ifdef ARCH_X86
  profiler_start[id] = rdtsc();
#endif
}

void profiler_stop_count (int id) {
#ifdef ARCH_X86
  profiler_times[id] += rdtsc() - profiler_start[id];
#endif
}

void profiler_print_results () {
  int i;

  printf ("\n\nPerformance analysis (cpu cycles):\n\n");
  for (i=0; i<MAX_ID; i++) {
    printf ("%d:\t%s\t%12lld\n", i, profiler_label[i], profiler_times[i]);
  }
}

#endif
