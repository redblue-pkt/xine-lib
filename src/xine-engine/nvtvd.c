/* 
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: nvtvd.c,v 1.1 2002/06/10 21:42:45 mshopf Exp $
 *
 * nvtvd - Routines for communication with nvtvd.
 *
 * This is the combination of several files from the nvtvd package
 * by Dirk Thierbach <dthierbach@gmx.de>. They have been inserted into
 * one single file in order not to clutter the source filespace too much.
 * The only change has been the removal of some '#include' statements,
 * and only a small fraction from debug.h has been included.
 *
 * This file contains (in this order):
 * pipe.h debug.h back_client.c pipe.c
 */

#include "nvtvd.h"


/* NVTV pipe header -- Dirk Thierbach <dthierbach@gmx.de>
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
 * $Id: nvtvd.c,v 1.1 2002/06/10 21:42:45 mshopf Exp $
 *
 * Contents:
 *
 * Routine prototypes to access the named pipe for server/client 
 * communication, and communication protocol constants.
 *
 */

#ifndef _PIPE_H
#define _PIPE_H

#define PIPE_IN  "/tmp/.nvtv-in"
#define PIPE_OUT "/tmp/.nvtv-out"

#define PIPE_VERSION	2 /* 0.3.1 */

/* even commands expect no return, odd commands do */

typedef enum {
  PCmd_None             =  0, /* In: None                                   */
  PCmd_Init             =  1, /* In: None;                   Out: card list */
  PCmd_Kill             =  2, /* In: None                                   */
  PCmd_Version          =  3, /* In: None;                   Out: version   */
  PCmd_CloseCard        = 10, /* In: None                                   */
  PCmd_OpenCard         = 11, /* In: Card index;             Out: chip list */
  PCmd_SetChip          = 12, /* In: Chip index, init;                      */
  PCmd_ProbeChips       = 13, /* In: None;                   Out: chip list */
  PCmd_SetSettings      = 14, /* In: Settings;                              */
  PCmd_GetSettings      = 15, /* In: None;                   Out: Settings  */
  PCmd_SetMode          = 16, /* In: Flags, crt, tv;                        */
  PCmd_GetMode          = 17, /* In: None;                   Out: Crt, tv   */
  PCmd_SetModeSettings  = 18, /* In: Flags, crt, tv, set;                   */
  PCmd_SetTestImage     = 20, /* In: Flags, tv, set;                        */
  PCmd_GetStatus        = 23, /* In: Index;                  Out: status    */
  PCmd_GetConnection    = 25, /* In: None;                   Out: connect   */
  PCmd_FindBySize       = 31, /* In: System, x, y, size;     Out: mode      */
  PCmd_FindByOverscan   = 33, /* In: System, x, y, hoc, voc; Out: mode      */
  PCmd_SetHeads         = 40, /* In: 3 heads                                */
  PCmd_GetHeads         = 41, /* In: None;                 Out: 3 heads     */
  PCmd_InitSharedView   = 51, /* In: None;                 Out: 4 int       */
  PCmd_GetTwinView      = 53, /* In: None;                 Out: 2 int, bool */
  PCmd_AdjustView       = 55, /* In: 3 int;                Out: 2 int, bool */
  PCmd_ServiceVC        = 57, /* In: 5 int;                Out: 2 int, bool */
} PipeCmd;

PipeCmd pipeReadCmd (FILE *pipe);
void    pipeWriteCmd (FILE *pipe, PipeCmd cmd);

int  pipeReadArgs (FILE *pipe, int n, ...);
int  pipeReadArgsOpt (FILE *pipe, int n, ...);
void pipeWriteArgs (FILE *pipe, int n, ...);

void* pipeReadList (FILE *pipe, int size);
void  pipeWriteList (FILE *pipe, int size, void *list);

#endif /* _PIPE_H */

/* excerpt from debug.h */
#define ERROR(X...) fprintf(stderr, X)

/* Fake output */
#define FPRINTF(X...) fprintf(stderr, X)

#ifdef NVTV_DEBUG
#define DPRINTF(X...) fprintf(stderr, X)
#define NO_TIMEOUT
#else
#define DPRINTF(X...) /* */
#endif

/* NVTV client backend -- Dirk Thierbach <dthierbach@gmx.de>
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
 * $Id: nvtvd.c,v 1.1 2002/06/10 21:42:45 mshopf Exp $
 *
 * Contents:
 *
 * Client backend for accessing the server
 *
 */

#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* -------- State -------- */

static FILE *pipe_in = NULL;
static FILE *pipe_out = NULL;

static CardPtr bcl_root = NULL;
static CardPtr bcl_card = NULL;

/* -------- Driver routines -------- */

void bcl_openPipes (void)
{
  /* IMPORTANT: Open out pipe first, otherwise deadlock */
  pipe_out = fopen (PIPE_OUT, "w");
  pipe_in  = fopen (PIPE_IN, "r");
}

void bcl_closePipes (void)
{
  fclose (pipe_in );
  fclose (pipe_out);
}

void bcl_openCard (CardPtr card)
{
  CardPtr c;
  int i, index;

  DPRINTF ("bcl_open\n");
  bcl_card = card;
  bcl_openPipes ();
  /* convert card to index */
  i = index = 0;
  for (c = bcl_root; c; c = c->next) {
    i++;
    if (c == card) index = i;
  }
  pipeWriteCmd (pipe_out, PCmd_OpenCard);
  pipeWriteArgs (pipe_out, 1, sizeof(index), &index);
  pipeReadCmd (pipe_in);
  bcl_card->chips = pipeReadList (pipe_in, sizeof (ChipInfo));
}

void bcl_closeCard (void)
{
  DPRINTF ("bcl_close\n");
  pipeWriteCmd (pipe_out, PCmd_CloseCard);
  pipeWriteArgs (pipe_out, 0);
  bcl_closePipes ();
}

void bcl_setHeads (int main, int tv, int video)
{
  DPRINTF ("bcl_setHeads %i %i %i\n", main, tv, video);
  pipeWriteCmd (pipe_out, PCmd_SetHeads);
  pipeWriteArgs (pipe_out, 3, sizeof(int), &main, sizeof(int), &tv, 
		 sizeof(int), &video);
}

void bcl_getHeads (int *main, int *tv, int *video, int *max) 
{
  DPRINTF ("bcl_getHeads\n");
  pipeWriteCmd (pipe_out, PCmd_GetHeads);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 4, sizeof(int), main, sizeof(int), tv, 
		sizeof(int), video, sizeof(int), max);
}

void bcl_probeChips (void)
{
  ChipPtr chip, del;

  DPRINTF ("bcl_probe\n");
  chip = bcl_card->chips; 
  while (chip) {
    del = chip;
    chip = chip->next;
    free (del->name);
    free (del);
  }
  pipeWriteCmd (pipe_out, PCmd_ProbeChips);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  bcl_card->chips = pipeReadList (pipe_in, sizeof (ChipInfo));
}

void bcl_setChip (ChipPtr chip, Bool init)
{
  ChipPtr c;
  int i, index;

  DPRINTF ("bcl_setChip %s %i\n", chip->name, init);
  /* convert chip to index */
  i = index = 0;
  for (c = bcl_card->chips; c; c = c->next) {
    i++;
    if (c == chip) index = i;
  }
  pipeWriteCmd (pipe_out, PCmd_SetChip);
  pipeWriteArgs (pipe_out, 2, sizeof(index), &index, sizeof(init), &init);
}

void bcl_setSettings (TVSettings *set)
{
  DPRINTF ("bcl_setSettings\n");
  pipeWriteCmd (pipe_out, PCmd_SetSettings);
  pipeWriteArgs (pipe_out, 1, sizeof(TVSettings), set);
}

void bcl_getSettings (TVSettings *set)
{
  DPRINTF ("bcl_getSettings\n");
  pipeWriteCmd (pipe_out, PCmd_GetSettings);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 1, sizeof(TVSettings), set);
}

void bcl_setMode (int ModeFlags, TVCrtRegs *crt, TVRegs *tv)
{
  DPRINTF ("bcl_setMode\n");
  pipeWriteCmd (pipe_out, PCmd_SetMode);
  pipeWriteArgs (pipe_out, 3, sizeof(ModeFlags), &ModeFlags,
		 sizeof(TVCrtRegs), crt, sizeof(TVRegs), tv);
}

void bcl_getMode (TVCrtRegs *crt, TVRegs *tv)
{
  DPRINTF ("bcl_getMode\n");
  pipeWriteCmd (pipe_out, PCmd_GetMode);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 2, sizeof(TVCrtRegs), crt, sizeof(TVRegs), tv);
}

void bcl_setModeSettings (int ModeFlags, TVCrtRegs *crt, 
			   TVRegs *tv, TVSettings *set)
{
  DPRINTF ("bcl_setModeSettings\n");
  pipeWriteCmd (pipe_out, PCmd_SetModeSettings);
  pipeWriteArgs (pipe_out, 4, sizeof(ModeFlags), &ModeFlags,
		 sizeof(TVCrtRegs), crt, sizeof(TVRegs), tv,
		 sizeof(TVSettings), set);
}

void bcl_setTestImage (int ModeFlags, TVRegs *tv, TVSettings *set)
{
  DPRINTF ("bcl_setTestImage\n");
  pipeWriteCmd (pipe_out, PCmd_SetTestImage);
  pipeWriteArgs (pipe_out, 3, sizeof(int), &ModeFlags, 
		 sizeof(TVRegs), tv, sizeof(TVSettings), set);
}

long bcl_getStatus (int index)
{
  long l;

  DPRINTF ("bcl_getStatus\n");
  pipeWriteCmd (pipe_out, PCmd_GetStatus);
  pipeWriteArgs (pipe_out, 1, sizeof(index), &index);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 1, sizeof(l), &l);
  return l;
}

TVConnect bcl_getConnection (void)
{
  TVConnect c;

  DPRINTF ("bcl_getConnection\n");
  pipeWriteCmd (pipe_out, PCmd_GetConnection);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 1, sizeof(c), &c);
  DPRINTF ("bcl_getConnection got %i\n", c);
  return c;
}

Bool bcl_findBySize (TVSystem system, int xres, int yres, char *size, 
    TVMode *mode, TVCrtRegs *crt, TVRegs *tv)
{
  int n;

  DPRINTF ("bcl_findBySize %i %i,%i %s\n", system, xres, yres, size);
  pipeWriteCmd (pipe_out, PCmd_FindBySize);
  pipeWriteArgs (pipe_out, 4, sizeof(system), &system, 
		 sizeof(xres), &xres, sizeof(yres), &yres,
		 strlen(size)+1, size);
  pipeReadCmd (pipe_in);
  n = pipeReadArgs (pipe_in, 3, sizeof(TVMode), mode, 
    sizeof(TVCrtRegs), crt, sizeof(TVRegs), tv);
  if (mode) {
    mode->crt = crt;
    mode->tv  = tv;
  }
  return (n >= 1);
}

Bool bcl_findByOverscan (TVSystem system, int xres, int yres, 
    double hoc, double voc, TVMode *mode, TVCrtRegs *crt, TVRegs *tv)
{
  int n;
 
  DPRINTF ("bcl_findByOC %i %i,%i\n", system, xres, yres);
  pipeWriteCmd (pipe_out, PCmd_FindByOverscan);
  pipeWriteArgs (pipe_out, 5, sizeof(system), &system, 
		 sizeof(xres), &xres, sizeof(yres), &yres,
		 sizeof(hoc), &hoc, sizeof(voc), &voc);
  pipeReadCmd (pipe_in);
  n = pipeReadArgs (pipe_in, 3, sizeof(TVMode), mode, 
    sizeof(TVCrtRegs), crt, sizeof(TVRegs), tv);
  if (mode) {
    mode->crt = crt;
    mode->tv  = tv;
  }
  return (n >= 1);
}

void bcl_initSharedView (int *view_x, int *view_y)
{
  DPRINTF ("bcl_initSharedView\n");
  pipeWriteCmd (pipe_out, PCmd_InitSharedView);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 2, sizeof(int), view_x, sizeof(int), view_y);
}

Bool bcl_getTwinView (int *view_x, int *view_y)
{
  Bool result;

  DPRINTF ("bcl_getTwinVie\n");
  pipeWriteCmd (pipe_out, PCmd_GetTwinView);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 3, sizeof(Bool), &result,
		sizeof(int), view_x, sizeof(int), view_y);
  return result;
}

Bool bcl_adjustViewport (int flags, int *view_x, int *view_y)
{
  Bool result;

  DPRINTF ("bcl_adjustViewport\n");
  pipeWriteCmd (pipe_out, PCmd_AdjustView);
  pipeWriteArgs (pipe_out, 3, sizeof(int), &flags, 
		 sizeof(int), view_x, sizeof(int), view_y);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_out, 3, sizeof(int), &result, 
		sizeof(int), view_x, sizeof(int), view_y);
  return result;
}

Bool bcl_serviceViewportCursor (int flags, int cursor_x, int cursor_y, 
  int *view_x, int *view_y)
{
  Bool result;

  DPRINTF ("bcl_serviceViewportCursor\n");
  pipeWriteCmd (pipe_out, PCmd_ServiceVC);
  pipeWriteArgs (pipe_out, 5, sizeof(int), &flags, 
		 sizeof(int), &cursor_x, sizeof(int), &cursor_y, 
		 sizeof(int), view_x, sizeof(int), view_y);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_out, 3, sizeof(int), &result, 
		sizeof(int), view_x, sizeof(int), view_y);
  return result;
}

BackAccessRec bcl_access_func = {
  openCard:                   bcl_openCard,
  closeCard:                  bcl_closeCard,
#ifdef DEBUG_PROBE
  probeSystem:                bnull_probeSystem,
#endif
};

BackCardRec bcl_card_func = {
  openCard:              bcl_openCard,
  closeCard:             bcl_closeCard,
#ifdef DEBUG_PROBE
  probeCard:             bnull_probeCard,
  probeBus:              bnull_probeBus,
#endif
  setHeads:              bcl_setHeads,
  getHeads:              bcl_getHeads,
  probeChips:            bcl_probeChips,
  setChip:               bcl_setChip,
  setSettings:           bcl_setSettings,
  getSettings:           bcl_getSettings,
  setMode:               bcl_setMode,
  getMode:               bcl_getMode,
  setModeSettings:       bcl_setModeSettings,
  setTestImage:          bcl_setTestImage, 
  getStatus:             bcl_getStatus,    
  getConnection:         bcl_getConnection,
  findBySize:            bcl_findBySize, 
  findByOverscan:        bcl_findByOverscan,
  initSharedView:        bcl_initSharedView,
  getTwinView:           bcl_getTwinView,
  adjustViewport:        bcl_adjustViewport,
  serviceViewportCursor: bcl_serviceViewportCursor,
};

/* -------- Init -------- */

Bool back_client_avail (void)
{
  int version;
  int fd_out, fd_in;

  /* IMPORTANT: Open out pipe first, otherwise deadlock */
  fd_out = open (PIPE_OUT, O_WRONLY | O_NONBLOCK);
  if (fd_out < 0) return FALSE;
  fd_in  = open (PIPE_IN, O_RDONLY | O_NONBLOCK);
  if (fd_in < 0) { 
    close (fd_out);
    return FALSE;
  }
  close (fd_in);
  close (fd_out);
  bcl_openPipes ();
  if (!pipe_in || !pipe_out) {
    /* FIXME: error message */
    bcl_closePipes ();
    return FALSE;
  }
  pipeWriteCmd (pipe_out, PCmd_Version);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 1, sizeof(version), &version);
  bcl_closePipes ();
  return (version == PIPE_VERSION);
}


CardPtr back_client_init (void)
{
  CardPtr card;

  back_access = &bcl_access_func;
  back_card   = &bcl_card_func;
  bcl_openPipes ();
  pipeWriteCmd (pipe_out, PCmd_Init);
  pipeWriteArgs (pipe_out, 0);
  bcl_root = pipeReadList (pipe_in, sizeof (CardInfo));
  bcl_card = bcl_root;
  bcl_closePipes ();
  for (card = bcl_card; card; card = card->next) {
    card->chips = NULL;
    card->dev = NULL;
  }
  return bcl_root;
}

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
 * $Id: nvtvd.c,v 1.1 2002/06/10 21:42:45 mshopf Exp $
 *
 * Contents:
 *
 * Routines to access the named pipe for server/client communication
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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
 *  Ignore zero size or null pointer arguments. Return number of 
 *  initial arguments that matched input stream, and set each result 
 *  either to NULL or to pointer.
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

