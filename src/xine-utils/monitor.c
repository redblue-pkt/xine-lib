/*
 * Copyright (C) 2000-2018 the xine project
 *
 * This file is part of xine, a free video player.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * debug print and profiling functions - implementation
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <sys/time.h>
#include <xine/xineutils.h>

#define MAX_ID 10

#ifndef NDEBUG

typedef struct {
    uint64_t    p_times;
    uint64_t    p_start;
    long        p_calls;
    const char *p_label;
} xine_profiler_t;

static xine_profiler_t profiler[MAX_ID];
static pthread_mutex_t profiler_lock = PTHREAD_MUTEX_INITIALIZER;

void xine_profiler_init () {
  pthread_mutex_lock(&profiler_lock);
  memset(profiler, 0, sizeof(profiler));
  pthread_mutex_unlock(&profiler_lock);
}

int xine_profiler_allocate_slot (const char *label) {
  int id;

  pthread_mutex_lock(&profiler_lock);

  for (id = 0; id < MAX_ID && profiler[id].p_label != NULL; id++)
    ;

  if (id >= MAX_ID) {
    pthread_mutex_unlock(&profiler_lock);
    return -1;
  }

  profiler[id].p_label = label;

  pthread_mutex_unlock(&profiler_lock);

  return id;
}


#if defined(ARCH_X86_32)
static __inline__ uint64_t rdtsc(void)
{
  unsigned long long int x;
  __asm__ volatile ("rdtsc\n\t" : "=A" (x));
  return x;
}
#elif defined(ARCH_X86_X32) || defined(ARCH_X86_64)
static __inline__ uint64_t rdtsc(void)
{
  unsigned long long int a, d;
  __asm__ volatile ("rdtsc\n\t" : "=a" (a), "=d" (d));
  return (d << 32) | (a & 0xffffffff);
}
#endif

void xine_profiler_start_count (int id) {
  if ( id >= MAX_ID || id < 0 ) return;

#if defined(ARCH_X86)
  profiler[id].p_start = rdtsc();
#endif
}

void xine_profiler_stop_count (int id) {
  if ( id >= MAX_ID || id < 0 ) return;

#if defined(ARCH_X86)
  profiler[id].p_times += rdtsc() - profiler[id].p_start;
#endif
  profiler[id].p_calls++;
}

void xine_profiler_print_results (void) {
  int i;

#if defined(ARCH_X86)
  static uint64_t cpu_speed;	/* cpu cyles/usec */

  if (!cpu_speed) {
    uint64_t tsc_start, tsc_end;
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
    if (profiler[i].p_label) {
      printf ("%2d: %-24.24s %12" PRIu64 " %9ld",
	      i, profiler[i].p_label, profiler[i].p_times, profiler[i].p_calls);
      if (profiler[i].p_calls) {
          printf(" %12" PRIu64, profiler[i].p_times / (uint64_t)profiler[i].p_calls);
#if defined(ARCH_X86)
	  printf(" %9" PRIu64, profiler[i].p_times / ((uint64_t)cpu_speed * profiler[i].p_calls));
#endif
      }
      printf ("\n");
    }
  }
}


#else /* DEBUG */

#define NO_PROFILER_MSG  {printf("xine's profiler is unavailable.\n");}

/* Dummies */
void xine_profiler_init (void) {
  NO_PROFILER_MSG
}
int xine_profiler_allocate_slot (const char *label) {
  (void)label;
  return -1;
}
void xine_profiler_start_count (int id) {
  (void)id;
}
void xine_profiler_stop_count (int id) {
  (void)id;
}
void xine_profiler_print_results (void) {
  NO_PROFILER_MSG
}

#endif


