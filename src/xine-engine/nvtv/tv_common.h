/* NVTV TV types header -- Dirk Thierbach <dthierbach@gmx.de>
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
 * $Id: tv_common.h,v 1.3 2003/05/04 01:35:06 hadess Exp $
 *
 * Contents:
 *
 * Header: Common tv-related data types and defines.
 *
 */

#ifndef _TV_COMMON_H
#define _TV_COMMON_H

#include "xf86i2c.h"
#include "tv_chip.h"

typedef struct _I2CChainRec *I2CChainPtr;
typedef struct _I2CChainRec {
  char          *name;
  I2CDevPtr	dev;
  I2CChainPtr	next;
  TVChip        chip;
} I2CChainRec;

typedef struct _TvEncoderObj TVEncoderObj;

struct _TvEncoderObj {
  TVChip chip;
  I2CDevPtr dev;
  long minClock, maxClock; /* in kHz */
  void (*Create) (TVEncoderObj *this, TVChip chip, I2CDevPtr dev);
  void (*InitRegs) (TVEncoderObj *this, int port);
  void (*SetRegs) (TVEncoderObj *this, TVEncoderRegs *r, TVState state);
  void (*GetRegs) (TVEncoderObj *this, TVEncoderRegs *r);
  void (*SetPort) (TVEncoderObj *this, int port);
  void (*GetPort) (TVEncoderObj *this, int *port);
  void (*SetState) (TVEncoderObj *this, TVEncoderRegs *r, TVState state);
  TVConnect (*GetConnect) (TVEncoderObj *this);
  long (*GetStatus) (TVEncoderObj *this, int index);
  int hwconfig;
  int hwstate;
};

/* I2C Id of device for use in (s)printf */
#define I2C_ID(dev) dev->pI2CBus->BusName+2,dev->SlaveAddr

I2CChainPtr TVFindDevice (I2CChainPtr root, TVChip chip);
I2CChainPtr TVCreateChain (I2CBusPtr busses[], int nbus, Bool all);

void TVDestroyChain (I2CChainPtr root);
void TVDestroyDevices (I2CBusPtr busses[], int nbus);
void TVDestroyBusses (I2CBusPtr busses[], int nbus);

void TVProbeDevice (I2CBusPtr bus, I2CSlaveAddr addr, char *format, ...);
void TVProbeCreateKnown (I2CBusPtr busses[], int nbus, I2CChainPtr *chain);
void TVProbeCreateAll (I2CBusPtr busses[], int nbus, I2CChainPtr *chain);

void TVSetTvEncoder (TVEncoderObj *encoder, I2CChainPtr chain);

TVChip TVDetectDeviceA (I2CDevPtr dev); /* for 0x88/0x8A */
TVChip TVDetectDeviceB (I2CDevPtr dev); /* for 0xEA/0xEC */

extern TVState tvState;

#endif /* _TV_COMMON_H */
