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
 * $Id: monitor.c,v 1.5 2001/09/10 13:36:56 jkeil Exp $
 *
 * debug print and profiling functions - implementation
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <sys/time.h>
#include "utils.h"
#include "monitor.h"

#define MAX_ID 10

#ifdef DEBUG

long long int profiler_times[MAX_ID] ;
long long int profiler_start[MAX_ID] ;
long profiler_calls[MAX_ID] ;
char * profiler_label[MAX_ID] ;

void profiler_init () {
  int i;
  for (i=0; i<MAX_ID; i++) {
    profiler_times[i] = 0;
    profiler_start[i] = 0;
    profiler_calls[i] = 0;
    profiler_label[i] = NULL;
  }
}

int profiler_allocate_slot (char *label) {
  int id;

  for (id = 0; id < MAX_ID && profiler_label[id] != NULL; id++)
    ;

  if (id >= MAX_ID)
    return -1;

  profiler_label[id] = label;
  return id;
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

  if ((unsigned)id >= MAX_ID) return;

#ifdef ARCH_X86
  profiler_start[id] = rdtsc();
#endif
}

void profiler_stop_count (int id) {

  if ((unsigned)id >= MAX_ID) return;

#ifdef ARCH_X86
  profiler_times[id] += rdtsc() - profiler_start[id];
#endif
  profiler_calls[id]++;
}

void profiler_print_results () {
  int i;

#ifdef ARCH_X86
  static long long int cpu_speed;	/* cpu cyles/usec */
  if (!cpu_speed) {
    long long int tsc_start, tsc_end;
    struct timeval tv_start, tv_end;

    tsc_start = rdtsc();
    gettimeofday(&tv_start, NULL);

    xine_usec_sleep(100000);

    tsc_end = rdtsc();
    gettimeofday(&tv_end, NULL);

    cpu_speed = (tsc_end - tsc_start) /
	((tv_end.tv_sec - tv_start.tv_sec) * 1e6 +
	 tv_end.tv_usec - tv_start.tv_usec);
  }
#endif

  printf ("\n\nPerformance analysis:\n\n"
	  "%-3s %-24.24s %12s %9s %12s %9s\n"
	  "----------------------------------------------------------------------------\n",
	  "ID", "name", "cpu cycles", "calls", "cycles/call", "usec/call");
  for (i=0; i<MAX_ID; i++) {
    if (profiler_label[i]) {
      printf ("%2d: %-24.24s %12lld %9ld",
	      i, profiler_label[i], profiler_times[i], profiler_calls[i]);
      if (profiler_calls[i]) {
	  printf(" %12lld", profiler_times[i] / profiler_calls[i]);
#ifdef ARCH_X86
	  printf(" %9lld", profiler_times[i] / (cpu_speed * profiler_calls[i]));
#endif
      }
      printf ("\n");
    }
  }
}

#endif
