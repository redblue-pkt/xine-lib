/* NVTV client backend header -- Dirk Thierbach <dthierbach@gmx.de>
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
 * $Id: back_client.h,v 1.2 2003/02/05 00:14:02 miguelfreitas Exp $
 *
 * Contents:
 *
 * Header for client backend
 */

#ifndef _BACK_CLIENT_H
#define _BACK_CLIENT_H

#include "debug.h"
#include "backend.h"

Bool back_client_avail (void);
CardPtr back_client_init (void);

/* client backend methods */

/* Attention! The 'size' and 'aspect' strings returned by the find
   operations in mode.spec are allocated, and should be freed when not
   needed anymore. For the moment, this creates a memory leak, as this
   behaviour is different from the other backends.  
*/

void bcl_openCard (CardPtr card);
void bcl_closeCard (void);
#ifdef DEBUG_PROBE
void bcl_probeCards (CardPtr card_list);
#endif
void bcl_setHeads (int main, int tv, int video);
void bcl_getHeads (int *main, int *tv, int *video, int *max);
void bcl_probeChips (void);
void bcl_setChip (ChipPtr chip, Bool init);
void bcl_setSettings (TVSettings *set);
void bcl_getSettings (TVSettings *set);
void bcl_setMode (TVRegs *r);
void bcl_getMode (TVRegs *r);
void bcl_setModeSettings (TVRegs *r, TVSettings *set);
void bcl_setTestImage (TVEncoderRegs *tv, TVSettings *set);
long bcl_getStatus (int index);
TVConnect bcl_getConnection (void);
Bool bcl_findBySize (TVSystem system, int xres, int yres, char *size, 
    TVMode *mode);
Bool bcl_findByOverscan (TVSystem system, int xres, int yres, 
    double hoc, double voc, TVMode *mode);

void bcl_initSharedView (int *view_x, int *view_y);
Bool bcl_getTwinView (int *view_x, int *view_y);
Bool bcl_adjustViewportVideo (int flags, int *view_x, int *view_y);
Bool bcl_serviceViewportVideoCursor (int flags, int cursor_x, int cursor_y, 
  int *view_x, int *view_y);

#endif /* _BACK_CLIENT_H */

