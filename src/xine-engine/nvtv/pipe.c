/* NVTV pipe -- Dirk Thierbach <dthierbach@gmx.de>
 *
 * This file is part of nvtv, a tool for tv-output on NVidia cards.
 * 
 * nvtv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * nvtv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: pipe.c,v 1.2 2003/02/05 00:14:03 miguelfreitas Exp $
 *
 * Contents:
 *
 * Routines to access the named pipe for server/client communication
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "debug.h"
#include "error.h"
#include "pipe.h"

/* 
 *  Read a command from a pipe
 */

PipeCmd pipeReadCmd (FILE *pipe)
{
  PipeCmd cmd;

  DPRINTF ("pipe read cmd\n");
  fread (&cmd, sizeof(PipeCmd), 1, pipe);
  DPRINTF ("pipe read cmd, got %i\n", cmd);
  return cmd;
}

/* 
 *  Write a command to a pipe
 */

void pipeWriteCmd (FILE *pipe, PipeCmd cmd)
{
  DPRINTF ("pipe write cmd %i\n", cmd);
  fwrite (&cmd, sizeof(PipeCmd), 1, pipe);
  fflush (pipe);
  DPRINTF ("pipe write cmd done\n");
}

/* 
 *  Write arguments to a pipe. Ellipsis paramaters are:
 *    size1, pointer1, size2, pointer2, ..., 0
 *  Ignore zero size of null pointer arguments.
 */

void pipeWriteArgs (FILE *pipe, int n, ...)
{
  va_list ap;
  int i, s;
  void *p;

  DPRINTF ("pipe write args\n");
  fwrite (&n, sizeof(n), 1, pipe);
  va_start(ap, n);
  for (i = 0; i < n; i++) {
    s = va_arg(ap, int);
    p = va_arg(ap, void *);
    if (!p) s = 0;
    fwrite (&s, sizeof(s), 1, pipe);
    if (s != 0) {
      fwrite (p, s, 1, pipe);
    }
  }
  fflush (pipe);
  va_end(ap);
}

/*
 * Implements the three following read routines. Allocate elements
 * with zero size, and optionally set results.
 */

int pipeReadArgsMulti (FILE *pipe, int res, int n, va_list ap)
{
  int i, j;
  int m, s, t, r;
  void *p;
  void **q;
  int ok;

  r = 0;
  fread (&m, sizeof(m), 1, pipe);
  ok = (m == n);
  for (i = 0; i < m; i++) {
    fread (&t, sizeof(t), 1, pipe);
    s = va_arg(ap, int);
    p = va_arg(ap, void *);
    if (ok) {
      if (s != 0 && t != 0 && s != t) ok = 0;
    }
    if (ok) {
      r++;
    } else {
      p = NULL;
    }
    if (t == 0) p = NULL;
    if (s == 0 && p) { /* alloc zero size */
      q = (void **) p;
      p = malloc (t);
      *q = p;
    }
    if (res) { /* store result pointer */
      q = va_arg(ap, void **);
      if (q) *q = p;
    }
    if (p) {
      fread (p, t, 1, pipe);
    } else {
      for (j = 0; j < t; j++) fgetc (pipe);
    }
  }
  return r;
}

/* 
 *  Read arguments from a pipe. Ellipsis parameters are:
 *    size_1, pointer_1, size_2, pointer_2, ..., size_n, pointer_n
 *  Ignore null pointer arguments, and allocate arguments with zero size. 
 *  Return number of initial arguments that matched input stream.
 */

int pipeReadArgs (FILE *pipe, int n, ...)
{
  va_list ap;
  int r;

  DPRINTF ("pipe read args\n");
  va_start(ap, n);
  r = pipeReadArgsMulti (pipe, 0, n, ap);
  va_end(ap);
  return r;
}

/* 
 *  Read optional arguments from a pipe. Ellipsis parameters are:
 *    size_1, pointer_1, result_1, ..., size_n, pointer_n, result_n
 *  Ignore zero size or null pointer data. Return number of initial
 *  arguments that matched input stream, and set each result either to
 *  NULL or to pointer.  
 */

int pipeReadArgsOpt (FILE *pipe, int n, ...)
{
  va_list ap;
  int r;

  DPRINTF ("pipe read args opt\n");
  va_start(ap, n);
  r = pipeReadArgsMulti (pipe, 1, n, ap);
  va_end(ap);
  return r;
}

/* 
 *  Read list from a pipe, alloc elements, and return pointer to first 
 *  element. The elements MUST contain the next field as first entry,
 *  and a string as second entry.
 */

void* pipeReadList (FILE *pipe, int size)
{
  void *root, *p, *q;
  int i, j, n, t;
  char *s;

  DPRINTF ("pipe read list\n");
  root = p = NULL;
  fread (&n, sizeof(n), 1, pipe);
  n /= 2;
  for (i = 0; i < n; i++) {
    fread (&t, sizeof(t), 1, pipe);
    if (t == size) {
      q = malloc (t);
      fread (q, t, 1, pipe);
      if (p) {
	* (void **) p = q;
      } else {
	root = q;
      }
      p = q;
    } else {
      q = NULL;
      for (j = 0; j < t; j++) fgetc (pipe);
    }
    fread (&t, sizeof(t), 1, pipe);
    if (q) {
      s = NULL;
      if (t != 0) {
	s = malloc (t);
	fread (s, sizeof(char), t, pipe);
      }
      * (((char **) q) + 1) = s;
    } else {
      for (j = 0; j < t; j++) fgetc (pipe);
    }
  }
  return root;
}

/* 
 *  Write list to a pipe. The elements MUST contain the next field as 
 *  first entry, and a string as second entry.
 */

void pipeWriteList (FILE *pipe, int size, void *list)
{
  void *l;
  char *s;
  int n, k;

  DPRINTF ("pipe write list\n");
  n = 0;
  for (l = list; l; l = * (void **) l) n++;
  n *= 2;
  fwrite (&n, sizeof(n), 1, pipe);
  for (l = list; l; l = * (void **) l) {
    fwrite (&size, sizeof(size), 1, pipe);
    fwrite (l, size, 1, pipe);
    s = * (((char **) l) + 1);
    if (s) {
      k = strlen(s) + 1;
      fwrite (&k, sizeof(k), 1, pipe);
      fwrite (s, sizeof(char), k, pipe);
    } else {
      k = 0;
      fwrite (&k, sizeof(k), 1, pipe);
    }
  }
  fflush (pipe);
}

