/* NVTV backend (header) -- Dirk Thierbach <dthierbach@gmx.de>
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
 * $Id: backend.h,v 1.3 2003/05/04 01:35:06 hadess Exp $
 *
 * Contents:
 *
 * Common header for all backends
 */

#ifndef _BACKEND_H
#define _BACKEND_H

#include "debug.h"
#include "tv_chip.h"

#ifdef DEBUG_PROBE
#include "tv_common.h"
#endif

#include "local.h" /* for Bool */

/* WARNING! The first entry of the two following data structures MUST be
   the 'next' field, and the second entry MUST be a string. If they are not,
   the pipe routines will crash. Other pointers besides those two are
   transfered through the pipe, but should be ignored, as there value is 
   invalid. */

/* List of chips accessible on one card. Most of the time, there will
   be only one tv chip, though in theory one could use two on dual-head
   cards. Other chips can be stored here as well. We duplicate the
   information in I2Chain and I2CDev to make the frontend independent
   of the back end. 
*/

typedef struct chip_info {
  struct chip_info *next; /* must be 1st entry! */
  char *name;    /* must be 2nd entry! (name including version and I2C addr) */
  TVChip chip;   /* chip type */
  int bus, addr; /* I2C bus and address */
  void *private; /* identify device, backend private */
} ChipInfo, *ChipPtr;

/* List of all NVidia cards available */

typedef struct card_info {
  struct card_info *next; /* must be 1st entry! */
  char *name; /* must be 2nd entry! (name including bus addr) */
  char *dev;  /* name of device for mmap (not via pipe) */
  char *arch; /* architecture (not via pipe) */
  CardType type;
  unsigned long reg_base;
  unsigned long pio_base;
  int addr_bus, addr_slot, addr_func;
  int pci_id;
  ChipPtr chips;  
} CardInfo, *CardPtr;

#define BACK_SERVICE_CURSOR		(1 << 0)
#define BACK_SERVICE_VIDEO		(1 << 1)
#define BACK_SERVICE_VIEW_CURSOR	(1 << 2)
#define BACK_SERVICE_VIEW_MAIN		(1 << 3)

typedef struct {
  void (*openCard) (CardPtr card);
  void (*closeCard) (void);
#ifdef DEBUG_PROBE
  void (*probeSystem) (CardPtr card_list);
#endif
} BackAccessRec, *BackAccessPtr;

/* 
 * listModes: Allocate a list of modes matching system, and return
 *   size of list. System may be TV_SYSTEM_NONE to match all systems.
 */

typedef struct {
  void (*openCard) (CardPtr card);
  void (*closeCard) (void);
  void (*probeChips) (void);
#ifdef DEBUG_PROBE
  void (*probeCard) (void);
  I2CChainPtr (*probeBus) (void);
#endif
  void (*setHeads) (int main, int tv, int video);
  void (*getHeads) (int *main, int *tv, int *video, int *max);
  void (*getHeadDev) (int head, int *devFlags);
  void (*setChip) (ChipPtr chip, Bool init);
  void (*setSettings) (TVSettings *set);
  void (*getSettings) (TVSettings *set);
  void (*setMode) (TVRegs *r);
  void (*getMode) (TVRegs *r);
  void (*setModeSettings) (TVRegs *r, TVSettings *set);
  void (*setTestImage) (TVEncoderRegs *tv, TVSettings *set);
  long (*getStatus) (int index);
  TVConnect (*getConnection) (void);
  int (*listModes) (TVSystem system, TVMode *(modes[]));
  Bool (*findBySize) (TVSystem system, int xres, int yres, char *size, 
    TVMode *mode);
  Bool (*findByOverscan) (TVSystem system, int xres, int yres, 
    double hoc, double voc, TVMode *mode);
  void (*initSharedView) (int *view_x, int *view_y);
  Bool (*getTwinView) (int *view_x, int *view_y);
  Bool (*adjustViewport) (int flags, int *view_x, int *view_y);
  Bool (*serviceViewportCursor) (int flags, int cursor_x, int cursor_y, 
    int *view_x, int *view_y);
} BackCardRec, *BackCardPtr;

/* The backend(s) use static information, so there can only be one
   backend active: */

extern BackAccessPtr back_access;
extern BackCardPtr back_card;

#endif /* _BACKEND_H */
