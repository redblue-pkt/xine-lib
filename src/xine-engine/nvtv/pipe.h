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
 * $Id: pipe.h,v 1.1 2003/01/18 15:29:22 miguelfreitas Exp $
 *
 * Contents:
 *
 * Routine prototypes to access the named pipe for server/client 
 * communication, and communication protocol constants.
 *
 */

#ifndef _PIPE_H
#define _PIPE_H

#include <stdio.h>

#ifndef CONFIG_PIPE_PATH
#define CONFIG_PIPE_PATH "/var/run"
#endif

#define PIPE_IN  CONFIG_PIPE_PATH ## "/nvtv-in"
#define PIPE_OUT CONFIG_PIPE_PATH ## "/nvtv-out"

#define PIPE_VERSION	0x000403 /* 0.4.3 */

/* even numbered commands expect no return, odd numbered commands do */

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
  PCmd_SetMode          = 16, /* In: Regs                                   */
  PCmd_GetMode          = 17, /* In: None;                   Out: Regs      */
  PCmd_SetModeSettings  = 18, /* In: Regs, Settings;                        */
  PCmd_SetTestImage     = 20, /* In: EncRegs, Setttings;                    */
  PCmd_GetStatus        = 23, /* In: Index;                  Out: status    */
  PCmd_GetConnection    = 25, /* In: None;                   Out: connect   */
  PCmd_FindBySize       = 31, /* In: System, x, y, size;     Out: mode      */
  PCmd_FindByOverscan   = 33, /* In: System, x, y, hoc, voc; Out: mode      */
  PCmd_SetHeads         = 40, /* In: 3 heads                                */
  PCmd_GetHeads         = 41, /* In: None;                 Out: 3 heads     */
  PCmd_GetHeadDev       = 43, /* In: Head;                 Out: Dev Flags   */
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
