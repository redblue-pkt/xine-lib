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
 * $Id: back_client.c,v 1.1 2003/01/18 15:29:22 miguelfreitas Exp $
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

#include "debug.h"
#include "backend.h"
#include "back_client.h"
#include "pipe.h"

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

void bcl_getHeadDev (int head, int *devFlags) 
{
  DPRINTF ("bcl_getHeadDev\n");
  pipeWriteCmd (pipe_out, PCmd_GetHeadDev);
  pipeWriteArgs (pipe_out, 1, sizeof (head), &head);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 1, sizeof(int), devFlags);
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

void bcl_setMode (TVRegs *regs)
{
  DPRINTF ("bcl_setMode\n");
  pipeWriteCmd (pipe_out, PCmd_SetMode);
  pipeWriteArgs (pipe_out, 1, sizeof(TVRegs), regs);
}

void bcl_getMode (TVRegs *regs)
{
  DPRINTF ("bcl_getMode\n");
  pipeWriteCmd (pipe_out, PCmd_GetMode);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 1, sizeof(TVRegs), regs);
}

void bcl_setModeSettings (TVRegs *regs, TVSettings *set)
{
  DPRINTF ("bcl_setModeSettings\n");
  pipeWriteCmd (pipe_out, PCmd_SetModeSettings);
  pipeWriteArgs (pipe_out, 2, sizeof(TVRegs), regs, sizeof(TVSettings), set);
}

void bcl_setTestImage (TVEncoderRegs *tv, TVSettings *set)
{
  DPRINTF ("bcl_setTestImage\n");
  pipeWriteCmd (pipe_out, PCmd_SetTestImage);
  pipeWriteArgs (pipe_out, 2, sizeof(TVEncoderRegs), tv, 
		 sizeof(TVSettings), set);
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
    TVMode *mode)
{
  int n;

  DPRINTF ("bcl_findBySize %i %i,%i %s\n", system, xres, yres, size);
  pipeWriteCmd (pipe_out, PCmd_FindBySize);
  pipeWriteArgs (pipe_out, 4, sizeof(system), &system, 
		 sizeof(xres), &xres, sizeof(yres), &yres,
		 strlen(size)+1, size);
  pipeReadCmd (pipe_in);
  n = pipeReadArgs (pipe_in, 1, sizeof(TVMode), mode); 
  return (n >= 1);
}

Bool bcl_findByOverscan (TVSystem system, int xres, int yres, 
    double hoc, double voc, TVMode *mode)
{
  int n;
 
  DPRINTF ("bcl_findByOC %i %i,%i\n", system, xres, yres);
  pipeWriteCmd (pipe_out, PCmd_FindByOverscan);
  pipeWriteArgs (pipe_out, 5, sizeof(system), &system, 
		 sizeof(xres), &xres, sizeof(yres), &yres,
		 sizeof(hoc), &hoc, sizeof(voc), &voc);
  pipeReadCmd (pipe_in);
  n = pipeReadArgs (pipe_in, 1, sizeof(TVMode), mode);
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

/* We cannot reuse the bnull functions here, because that would require
 * the client library to include a lot more files.
 */

#ifdef DEBUG_PROBE
void bcl_probeSystem (CardPtr card_list)
{
  DPRINTF ("bcl_probeSystem\n");
}

void bcl_probeCard (void)
{
  DPRINTF ("bcl_probeCard\n");
}

I2CChainPtr bcl_probeBus (void)
{
  DPRINTF ("bcl_probeBus\n");
  return NULL;
}
#endif

BackAccessRec bcl_access_func = {
  openCard:                   bcl_openCard,
  closeCard:                  bcl_closeCard,
#ifdef DEBUG_PROBE
  probeSystem:                bcl_probeSystem,
#endif
};

BackCardRec bcl_card_func = {
  openCard:              bcl_openCard,
  closeCard:             bcl_closeCard,
#ifdef DEBUG_PROBE
  probeCard:             bcl_probeCard,
  probeBus:              bcl_probeBus,
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
    /* FIXME TODO: error message */
    bcl_closePipes ();
    return FALSE;
  }
  pipeWriteCmd (pipe_out, PCmd_Version);
  pipeWriteArgs (pipe_out, 0);
  pipeReadCmd (pipe_in);
  pipeReadArgs (pipe_in, 1, sizeof(version), &version);
  bcl_closePipes ();
  if (version != PIPE_VERSION)
    ERROR ("Server protocol version mismatch\n");
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
    card->dev = "";
    card->arch = "";
    card->chips = NULL;
    card->dev = NULL;
  }
  return bcl_root;
}

