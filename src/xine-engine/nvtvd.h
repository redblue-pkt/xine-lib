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
 * $Id: nvtvd.h,v 1.3 2002/08/02 14:09:05 mshopf Exp $
 *
 * nvtvd - Routines for communication with nvtvd.
 *
 * This is the combination of several files from the nvtvd package
 * by Dirk Thierbach <dthierbach@gmx.de>. They have been inserted into
 * one single file in order not to clutter the source filespace too much.
 * The only change has been the removal of some '#include' statements.
 *
 * This file contains (in this order):
 * Xdefs.h miscstruct.h xfree.h nv_tvchip.h backend.h back_client.h
 */


/* $XFree86: xc/include/Xdefs.h,v 1.2 1999/08/22 06:21:20 dawes Exp $ */

/***********************************************************

Copyright (c) 1999  The XFree86 Project Inc.

All Rights Reserved.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The XFree86 Project
Inc. shall not be used in advertising or otherwise to promote the
sale, use or other dealings in this Software without prior written
authorization from The XFree86 Project Inc..

*/

/**
 ** Types definitions shared between server and clients 
 **/

#ifndef _XDEFS_H
#define _XDEFS_H

#ifdef _XSERVER64
#include <Xmd.h>
#endif 

#ifndef _XTYPEDEF_ATOM
#  define _XTYPEDEF_ATOM
#  ifndef _XSERVER64
typedef unsigned long Atom;
#  else
typedef CARD32 Atom;
#  endif
#endif

#ifndef Bool
#  ifndef _XTYPEDEF_BOOL
#   define _XTYPEDEF_BOOL
typedef int Bool;
#  endif
#endif

#ifndef _XTYPEDEF_POINTER
#  define _XTYPEDEF_POINTER
typedef void *pointer;
#endif

#ifndef _XTYPEDEF_CLIENTPTR
typedef struct _Client *ClientPtr;
#  define _XTYPEDEF_CLIENTPTR
#endif

#ifndef _XTYPEDEF_XID
#  define _XTYPEDEF_XID
#  ifndef _XSERVER64
typedef unsigned long XID;
#  else
typedef CARD32 XID;
#  endif
#endif

#ifndef _XTYPEDEF_MASK
#  define _XTYPEDEF_MASK
#  ifndef _XSERVER64
typedef unsigned long Mask;
#  else
typedef CARD32 Mask;
#  endif
#endif

#ifndef _XTYPEDEF_FONTPTR
#  define _XTYPEDEF_FONTPTR
typedef struct _Font *FontPtr; /* also in fonts/include/font.h */
#endif

#ifndef _XTYPEDEF_FONT
#  define _XTYPEDEF_FONT
typedef XID	Font;
#endif

#ifndef _XTYPEDEF_FSID
#  ifndef _XSERVER64
typedef unsigned long FSID;
#  else
typedef CARD32 FSID;
#  endif
#endif

typedef FSID AccContext;

/* OS independant time value 
   XXX Should probably go in Xos.h */
typedef struct timeval **OSTimePtr;


typedef void (* BlockHandlerProcPtr)(pointer /* blockData */,
				     OSTimePtr /* pTimeout */,
				     pointer /* pReadmask */);

#endif
/* Excerpt from: miscstruct.h and misc.h */

/* $TOG: miscstruct.h /main/11 1998/02/09 14:29:04 kaleb $ */
/***********************************************************

Copyright 1987, 1998  The Open Group

All Rights Reserved.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.


Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/
/* $XFree86: xc/programs/Xserver/include/miscstruct.h,v 3.0 1996/02/18 03:45:10 dawes Exp $ */

#ifndef MISCSTRUCT_H
#define MISCSTRUCT_H 1

/**** misc.h */

#ifndef TRUE
#define TRUE 1
#endif 

#ifndef FALSE
#define FALSE 0
#endif

/**** miscstruct.h */

typedef struct _Box {
    short x1, y1, x2, y2;
} BoxRec;

typedef union _DevUnion {
    pointer		ptr;
    long		val;
    unsigned long	uval;
    pointer		(*fptr)(void);
} DevUnion;

#endif /* MISCSTRUCT_H */

/* NVTV xfree -- Dirk Thierbach <dthierbach@gmx.de>
 *
 * Header: All definitions from xfree that are needed.
 *
 */

#ifndef _XFREE_H
#define _XFREE_H 1

#include <stdio.h>
#include <stdlib.h>

#include <X11/Xmd.h>

#ifdef __GNUC__
#ifndef DEBUG
#  define DEBUG(x) /*x*/
#endif
#define ErrorF(x...) fprintf(stderr,x)
#else
#ifndef DEBUG
#  define DEBUG(x) /*x*/
#endif
#define ErrorF(...) fprintf(stderr, __VA_ARGS__)
#endif

#define __inline__ inline

/**** libc_wrapper.c */

void xf86usleep(unsigned long usec);
void xf86getsecs(long * secs, long * usecs);

/**** include/os.h */

/* modified for stdlib */

#define xalloc(size) malloc(size)
#define xnfcalloc(_num, _size) calloc(_num, _size)
#define xcalloc(_num, _size) calloc(_num, _size)
#define xfree(ptr) free(ptr)

/**** common/compiler.h */

#define MMIO_IN8(base, offset) \
	*(volatile CARD8 *)(((CARD8*)(base)) + (offset))
#define MMIO_IN16(base, offset) \
	*(volatile CARD16 *)(void *)(((CARD8*)(base)) + (offset))
#define MMIO_IN32(base, offset) \
	*(volatile CARD32 *)(void *)(((CARD8*)(base)) + (offset))
#define MMIO_OUT8(base, offset, val) \
	*(volatile CARD8 *)(((CARD8*)(base)) + (offset)) = (val)
#define MMIO_OUT16(base, offset, val) \
	*(volatile CARD16 *)(void *)(((CARD8*)(base)) + (offset)) = (val)
#define MMIO_OUT32(base, offset, val) \
	*(volatile CARD32 *)(void *)(((CARD8*)(base)) + (offset)) = (val)
#define MMIO_ONB8(base, offset, val) MMIO_OUT8(base, offset, val) 
#define MMIO_ONB16(base, offset, val) MMIO_OUT16(base, offset, val) 
#define MMIO_ONB32(base, offset, val) MMIO_OUT32(base, offset, val) 

/* -------- vgahw/vgaHW.h -------- */

/* Standard VGA registers */
#define VGA_ATTR_INDEX		0x3C0
#define VGA_ATTR_DATA_W		0x3C0
#define VGA_ATTR_DATA_R		0x3C1
#define VGA_IN_STAT_0		0x3C2		/* read */
#define VGA_MISC_OUT_W		0x3C2		/* write */
#define VGA_ENABLE		0x3C3
#define VGA_SEQ_INDEX		0x3C4
#define VGA_SEQ_DATA		0x3C5
#define VGA_DAC_MASK		0x3C6
#define VGA_DAC_READ_ADDR	0x3C7
#define VGA_DAC_WRITE_ADDR	0x3C8
#define VGA_DAC_DATA		0x3C9
#define VGA_FEATURE_R		0x3CA		/* read */
#define VGA_MISC_OUT_R		0x3CC		/* read */
#define VGA_GRAPH_INDEX		0x3CE
#define VGA_GRAPH_DATA		0x3CF

#define VGA_IOBASE_MONO		0x3B0
#define VGA_IOBASE_COLOR	0x3D0

#define VGA_CRTC_INDEX_OFFSET	0x04
#define VGA_CRTC_DATA_OFFSET	0x05
#define VGA_IN_STAT_1_OFFSET	0x0A		/* read */
#define VGA_FEATURE_W_OFFSET	0x0A		/* write */


/**** common/xf86str.h */

/* Video mode flags */

typedef enum {
    V_PHSYNC	= 0x0001,
    V_NHSYNC	= 0x0002,
    V_PVSYNC	= 0x0004,
    V_NVSYNC	= 0x0008,
    V_INTERLACE	= 0x0010,
    V_DBLSCAN	= 0x0020,
    V_CSYNC	= 0x0040,
    V_PCSYNC	= 0x0080,
    V_NCSYNC	= 0x0100,
    V_HSKEW	= 0x0200,	/* hskew provided */
    V_BCAST	= 0x0400,
    V_PIXMUX	= 0x1000,
    V_DBLCLK	= 0x2000,
    V_CLKDIV2	= 0x4000
} ModeFlags;

typedef enum {
    INTERLACE_HALVE_V	= 0x0001	/* Halve V values for interlacing */
} CrtcAdjustFlags;

/* Flags passed to ChipValidMode() */
typedef enum {
    MODECHECK_INITIAL = 0,
    MODECHECK_FINAL   = 1
} ModeCheckFlags;

/* These are possible return values for xf86CheckMode() and ValidMode() */
typedef enum {
    MODE_OK	= 0,	/* Mode OK */
    MODE_HSYNC,		/* hsync out of range */
    MODE_VSYNC,		/* vsync out of range */
    MODE_H_ILLEGAL,	/* mode has illegal horizontal timings */
    MODE_V_ILLEGAL,	/* mode has illegal horizontal timings */
    MODE_BAD_WIDTH,	/* requires an unsupported linepitch */
    MODE_NOMODE,	/* no mode with a maching name */
    MODE_NO_INTERLACE,	/* interlaced mode not supported */
    MODE_NO_DBLESCAN,	/* doublescan mode not supported */
    MODE_NO_VSCAN,	/* multiscan mode not supported */
    MODE_MEM,		/* insufficient video memory */
    MODE_VIRTUAL_X,	/* mode width too large for specified virtual size */
    MODE_VIRTUAL_Y,	/* mode height too large for specified virtual size */
    MODE_MEM_VIRT,	/* insufficient video memory given virtual size */
    MODE_NOCLOCK,	/* no fixed clock available */
    MODE_CLOCK_HIGH,	/* clock required is too high */
    MODE_CLOCK_LOW,	/* clock required is too low */
    MODE_CLOCK_RANGE,	/* clock/mode isn't in a ClockRange */
    MODE_BAD_HVALUE,	/* horizontal timing was out of range */
    MODE_BAD_VVALUE,	/* vertical timing was out of range */
    MODE_BAD_VSCAN,	/* VScan value out of range */
    MODE_HSYNC_NARROW,	/* horizontal sync too narrow */
    MODE_HSYNC_WIDE,	/* horizontal sync too wide */
    MODE_HBLANK_NARROW,	/* horizontal blanking too narrow */
    MODE_HBLANK_WIDE,	/* horizontal blanking too wide */
    MODE_VSYNC_NARROW,	/* vertical sync too narrow */
    MODE_VSYNC_WIDE,	/* vertical sync too wide */
    MODE_VBLANK_NARROW,	/* vertical blanking too narrow */
    MODE_VBLANK_WIDE,	/* vertical blanking too wide */
    MODE_PANEL,         /* exceeds panel dimensions */
    MODE_INTERLACE_WIDTH, /* width too large for interlaced mode */
    MODE_ONE_WIDTH,    /* only one width is supported */
    MODE_ONE_HEIGHT,   /* only one height is supported */
    MODE_ONE_SIZE,     /* only one resolution is supported */
    MODE_BAD = -2,	/* unspecified reason */
    MODE_ERROR	= -1	/* error condition */
} ModeStatus;

# define M_T_BUILTIN 0x01        /* built-in mode */
# define M_T_CLOCK_C (0x02 | M_T_BUILTIN) /* built-in mode - configure clock */
# define M_T_CRTC_C  (0x04 | M_T_BUILTIN) /* built-in mode - configure CRTC  */
# define M_T_CLOCK_CRTC_C  (M_T_CLOCK_C | M_T_CRTC_C)
                               /* built-in mode - configure CRTC and clock */
# define M_T_DEFAULT 0x10	/* (VESA) default modes */

/* Video mode */
typedef struct _DisplayModeRec {
    struct _DisplayModeRec *	prev;
    struct _DisplayModeRec *	next;
    char *			name;		/* identifier for the mode */
    ModeStatus			status;
    int				type;
    
    /* These are the values that the user sees/provides */
    int				Clock;		/* pixel clock freq */
    int				HDisplay;	/* horizontal timing */
    int				HSyncStart;
    int				HSyncEnd;
    int				HTotal;
    int				HSkew;
    int				VDisplay;	/* vertical timing */
    int				VSyncStart;
    int				VSyncEnd;
    int				VTotal;
    int				VScan;
    int				Flags;

  /* These are the values the hardware uses */
    int				ClockIndex;
    int				SynthClock;	/* Actual clock freq to
					  	 * be programmed */
    int				CrtcHDisplay;
    int				CrtcHBlankStart;
    int				CrtcHSyncStart;
    int				CrtcHSyncEnd;
    int				CrtcHBlankEnd;
    int				CrtcHTotal;
    int				CrtcHSkew;
    int				CrtcVDisplay;
    int				CrtcVBlankStart;
    int				CrtcVSyncStart;
    int				CrtcVSyncEnd;
    int				CrtcVBlankEnd;
    int				CrtcVTotal;
    Bool			CrtcHAdjusted;
    Bool			CrtcVAdjusted;
    int				PrivSize;
    INT32 *			Private;
    int				PrivFlags;

    float			HSync, VRefresh;
} DisplayModeRec, *DisplayModePtr;

/*
 * memType is of the size of the addressable memory (machine size)
 * usually unsigned long.
 */
typedef pointer (*funcPointer)(void);

typedef struct _ScrnInfoRec {
    int			scrnIndex;		/* Number of this screen */
/* ... */
    DisplayModePtr	currentMode;		/* current mode */
/* ... */
    pointer		driverPrivate;		/* Driver private area */
    DevUnion *		privates;		/* Other privates can hook in
						 * here */
} ScrnInfoRec;

typedef struct _ScrnInfoRec *ScrnInfoPtr;

/**** common/xf86.h */

extern ScrnInfoPtr *xf86Screens;	/* List of pointers to ScrnInfoRecs */

#ifdef __GNUC__
#define xf86Msg(type,format,args...) /* fprintf(stderr,format,args) */
#define xf86DrvMsg(scrnIndex,type,format, args...) /* fprintf(stderr,format,args) */
#else
#define xf86Msg(type,format,...) /* fprintf(stderr,format,__VA_ARGS__) */
#define xf86DrvMsg(scrnIndex,type,format, ...) /* fprintf(stderr,format,__VA_ARGS_) */
#endif

/* ---------------- nv driver files ---------------- */

/**** nv_local.h */

/*
 * Typedefs to force certain sized values.
 */
typedef unsigned char  U008;
typedef unsigned short U016;
typedef unsigned int   U032;

/* these assume memory-mapped I/O, and not normal I/O space */
#define NV_WR08(p,i,d)  MMIO_OUT8((volatile pointer)(p), (i), (d))
#define NV_RD08(p,i)    MMIO_IN8((volatile pointer)(p), (i))
#define NV_WR16(p,i,d)  MMIO_OUT16((volatile pointer)(p), (i), (d))
#define NV_RD16(p,i)    MMIO_IN16((volatile pointer)(p), (i))
#define NV_WR32(p,i,d)  MMIO_OUT32((volatile pointer)(p), (i), (d))
#define NV_RD32(p,i)    MMIO_IN32((volatile pointer)(p), (i))

#define VGA_WR08(p,i,d) NV_WR08(p,i,d)
#define VGA_RD08(p,i)   NV_RD08(p,i)

/**** nv_dac.c */

#define DDC_SDA_READ_MASK  (1 << 3)
#define DDC_SCL_READ_MASK  (1 << 2)
#define DDC_SDA_WRITE_MASK (1 << 4)
#define DDC_SCL_WRITE_MASK (1 << 5)

/**** riva_hw.h */

typedef struct _riva_hw_inst
{
    U032 Architecture;
    U032 Version;
    U032 CrystalFreqKHz;
    U032 RamAmountKBytes;
    U032 MaxVClockFreqKHz;
    U032 RamBandwidthKBytesPerSec;
    U032 EnableIRQ;
    U032 IO;
#if 1 /* from a different riva_hw ??? */
    U032 VBlankBit;
    U032 FifoFreeCount;
    U032 FifoEmptyCount;
#endif
    /*
     * Non-FIFO registers.
     */
    volatile U032 *PCRTC;
    volatile U032 *PRAMDAC;
    volatile U032 *PFB;
    volatile U032 *PFIFO;
    volatile U032 *PGRAPH;
    volatile U032 *PEXTDEV;
    volatile U032 *PTIMER;
    volatile U032 *PMC;
    volatile U032 *PRAMIN;
    volatile U032 *FIFO;
    volatile U032 *CURSOR;
    volatile U032 *CURSORPOS;
    volatile U032 *VBLANKENABLE;
    volatile U032 *VBLANK;
    volatile U008 *PCIO;
    volatile U008 *PVIO;
    volatile U008 *PDIO;
#if 1
    volatile U032 *PVIDEO; /* extra */
#endif
  /* Remaining entries cut */
#if 0
    /*
     * Common chip functions.
     */
    int  (*Busy)(struct _riva_hw_inst *);
    void (*CalcStateExt)(struct _riva_hw_inst *,struct _riva_hw_state *,int,int,int,int,int,int,int,int,int,int,int,int,int);
    void (*LoadStateExt)(struct _riva_hw_inst *,struct _riva_hw_state *);
    void (*UnloadStateExt)(struct _riva_hw_inst *,struct _riva_hw_state *);
    void (*SetStartAddress)(struct _riva_hw_inst *,U032);
    void (*SetSurfaces2D)(struct _riva_hw_inst *,U032,U032);
    void (*SetSurfaces3D)(struct _riva_hw_inst *,U032,U032);
    int  (*ShowHideCursor)(struct _riva_hw_inst *,int);
    void (*LockUnlock)(struct _riva_hw_inst *, int);
    /*
     * Current extended mode settings.
     */
    struct _riva_hw_state *CurrentState;
    /*
     * FIFO registers.
     */
    RivaRop                 *Rop;
    RivaPattern             *Patt;
    RivaClip                *Clip;
    RivaPixmap              *Pixmap;
    RivaScreenBlt           *Blt;
    RivaBitmap              *Bitmap;
    RivaLine                *Line;
    RivaTexturedTriangle03  *Tri03;
    RivaTexturedTriangle05  *Tri05;
#endif
} RIVA_HW_INST;

#endif
/* NVTV tv_chip header -- Dirk Thierbach <dthierbach@gmx.de>
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
 * $Id: nvtvd.h,v 1.3 2002/08/02 14:09:05 mshopf Exp $
 *
 * Contents:
 *
 * Header: Structures and defines for the Brooktree, the Chrontel and the 
 * Philips chip. This part could eventually become a part of the XFree 
 * NV-Driver.
 * 
 */

#ifndef _TV_CHIP_H
#define _TV_CHIP_H

typedef enum {
  TYPE_NONE  = 0,
  TYPE_INT   = 1,
  TYPE_ULONG = 2,
} VarType;

typedef enum {
  CARD_NONE   = 0,
  CARD_NVIDIA = 1,
  CARD_TDFX   = 2,
  CARD_I810   = 3,
} CardType;

typedef enum {
  TV_SYSTEM_NONE    = -1,
  TV_SYSTEM_NTSC    = 0,
  TV_SYSTEM_NTSC_J  = 1, /* same as NTSC_60 */
  TV_SYSTEM_PAL     = 2, 
  TV_SYSTEM_PAL_60  = 3,
  TV_SYSTEM_PAL_N   = 4,
  TV_SYSTEM_PAL_NC  = 5,
  TV_SYSTEM_PAL_M   = 6,
  TV_SYSTEM_PAL_M60 = 7,
  TV_SYSTEM_PAL_X   = 8, /* Fake PAL System to correct color carriers,
                            useful at least in Sweden PAL-B */
  TV_SYSTEM_SECAM   = 9, /* only on Conexant chips */
} TVSystem;

typedef enum {
  TV_NO_CHIP   = 0,
  TV_CHRONTEL  = 1,
  TV_BROOKTREE = 2,
  TV_CONEXANT  = 3,
  TV_PHILIPS   = 4,
} TVChip;

/* -------- Host interface flags, all chips -------- */

#define HOST_SYNC_DIR		(1 << 0)
#define HOST_SYNC_IN		0       
#define HOST_SYNC_OUT		(1 << 0)
				        
#define HOST_VSYNC_ACTIVE	(1 << 1)
#define HOST_VSYNC_LOW		0       
#define HOST_VSYNC_HIGH		(1 << 1)
				        
#define HOST_HSYNC_ACTIVE	(1 << 2)
#define HOST_HSYNC_LOW		0       
#define HOST_HSYNC_HIGH		(1 << 2)
				        
#define HOST_BLANK_ACTIVE	(1 << 3)
#define HOST_BLANK_LOW		0       
#define HOST_BLANK_HIGH		(1 << 3)
				        
#define HOST_BLANK_DIR		(1 << 4)
#define HOST_BLANK_IN		0       
#define HOST_BLANK_OUT		(1 << 4)
				        
#define HOST_BLANK_MODE		(1 << 5)
#define HOST_BLANK_AREA		0       
#define HOST_BLANK_DOT		(1 << 5)
				
#define HOST_MODE_MASK		(1 << 6)
#define HOST_MODE_MASTER	0
#define HOST_MODE_SLAVE		(1 << 6)

#define HOST_PCLK_ACTIVE	(1 << 7)
#define HOST_PCLK_LOW		0
#define HOST_PCLK_HIGH		(1 << 7)

#define HOST_PCLK_VDD		(1 << 8)
#define HOST_VDD_LOW		0
#define HOST_VDD_HIGH		(1 << 8)

#define HOST_NVIDIA		(HOST_VSYNC_HIGH | HOST_HSYNC_HIGH | \
				 HOST_SYNC_OUT   | HOST_PCLK_HIGH  | \
				 HOST_BLANK_LOW  | HOST_BLANK_OUT  | \
				 HOST_BLANK_AREA | HOST_MODE_MASTER)

#define HOST_TDFX		(HOST_VSYNC_HIGH | HOST_HSYNC_HIGH | \
				 HOST_BLANK_LOW  | HOST_BLANK_IN   | \
				 HOST_BLANK_DOT  | HOST_MODE_MASTER)

#define HOST_I810		(HOST_VSYNC_LOW  | HOST_HSYNC_LOW | \
				 HOST_SYNC_IN    | HOST_PCLK_LOW  | \
				 HOST_BLANK_LOW  | HOST_BLANK_IN  | \
				 HOST_BLANK_AREA | HOST_MODE_MASTER)

/* -------- Brooktree -------- */

#define BT_FLAG1_NI_OUT		(1 << 0)
#define BT_FLAG1_SETUP		(1 << 1)
#define BT_FLAG1_625LINE	(1 << 2)
#define BT_FLAG1_VSYNC_DUR	(1 << 3)
#define BT_FLAG1_DIS_SCRESET	(1 << 4)
#define BT_FLAG1_PAL_MD		(1 << 5)
#define BT_FLAG1_ECLIP		(1 << 6)
#define BT_FLAG1_EN_ASYNC	(1 << 8)

#define BT_FLAG1_NTSC		(BT_FLAG1_VSYNC_DUR | BT_FLAG1_SETUP)
#define BT_FLAG1_NTSC_JAPAN	(BT_FLAG1_VSYNC_DUR)
#define BT_FLAG1_PAL_BDGHI	(BT_FLAG1_PAL_MD | BT_FLAG1_625LINE)
#define BT_FLAG1_PAL_N		(BT_FLAG1_VSYNC_DUR | BT_FLAG1_SETUP | \
				 BT_FLAG1_PAL_MD | BT_FLAG1_625LINE)
#define BT_FLAG1_PAL_M		(BT_FLAG1_VSYNC_DUR | BT_FLAG1_SETUP | \
				 BT_FLAG1_PAL_MD)
#define BT_FLAG1_PAL_BDGHI	(BT_FLAG1_PAL_MD | BT_FLAG1_625LINE)
#define BT_FLAG1_PAL_60		(BT_FLAG1_PAL_MD | BT_FLAG1_VSYNC_DUR)

#define BT_FLAG2_DIS_FFILT	(1 << 0)
#define BT_FLAG2_DIS_YFLPF	(1 << 1)
#define BT_FLAG2_DIS_GMSHY	(1 << 2)
#define BT_FLAG2_DIS_GMUSHY	(1 << 3)
#define BT_FLAG2_DIS_GMSHC	(1 << 4)
#define BT_FLAG2_DIS_GMUSHC	(1 << 5)
#define BT_FLAG2_DIS_CHROMA	(1 << 6)

#define BT_FLAG2_DIS_GM 	(BT_FLAG2_DIS_GMSHY | BT_FLAG2_DIS_GMUSHY \
			       | BT_FLAG2_DIS_GMSHC | BT_FLAG2_DIS_GMUSHC)

#define BT_FLAG3_DACDISA	(1 << 0)
#define BT_FLAG3_DACDISB	(1 << 1)
#define BT_FLAG3_DACDISC	(1 << 2)
#define BT_FLAG3_DACDISD	(1 << 3)
#define BT_FLAG3_DAC		(BT_FLAG3_DACDISA | BT_FLAG3_DACDISB \
			       | BT_FLAG3_DACDISC | BT_FLAG3_DACDISD)

#define BT_FLAG3_FBAS		(BT_FLAG3_DACDISB | BT_FLAG3_DACDISC)
#define BT_FLAG3_SVHS		(BT_FLAG3_DACDISA)
#define BT_FLAG3_CONVERT	(BT_FLAG3_DACDISA)
#define BT_FLAG3_BOTH		0

/* DACDISD is available only for the Conexant chip, and is reserved for
   the Brooktree chip. It is reset by default */

/* FIXME URGENT: Conexant doc says no more than 1 DAC should be disabled */

typedef struct {
  int hsynoffset;    /* time */
  int vsynoffset;    /* time */
  int hsynwidth;     /* time */ /* don't confuse with hsync_width ! */
  int vsynwidth;     /* time */
  int h_clko;        /* time */
  int h_active;      /* time */
  int hsync_width;   /* time(system) */
  int hburst_begin;  /* time(system) */
  int hburst_end;    /* time(system) */
  int h_blanko;      /* time */
  int v_blanko;      /* time */
  int v_activeo;     /* time */
  int h_fract;       /* time */
  int h_clki;        /* time */
  int h_blanki;      /* time */
  int v_linesi;      /* time */
  int v_blanki;      /* time */
  int v_activei;     /* time */
  int v_scale;       /* time */
  int pll_fract;     /* time */
  int pll_int;       /* time */
  int sync_amp;      /* level(system) */
  int bst_amp;       /* level(system) */
  int mcr;           /* level(system) */
  int mcb;           /* level(system) */
  int my;            /* level(system) */
  unsigned long msc; /* time */
  int flags1;        /* time */
  int flags2;        /* func */
  int flags3;        /* func */
  int f_selc;        /* func */
  int f_sely;        /* func */
  int ycoring;       /* func */
  int ccoring;       /* func */
  int yattenuate;    /* func */
  int cattenuate;    /* func */
  int ylpf;          /* func */
  int clpf;          /* func */
  int out_muxa;      /* func */
  int out_muxb;      /* func */
  int out_muxc;      /* func */
  int out_muxd;      /* func */
  int phase_off;     /* time(?) */
  int macro;         /* time(all) */
} TVBtRegs;

/* -------- Conexant -------- */

#define CX_FLAG1_FM		(1 << 7)
#define CX_FLAG1_EXT		(1 << 9)  /* Is extension, don't init */

#define CX_FLAG4_PROG_SC	(1 << 0)
#define CX_FLAG4_SC_PATTERN	(1 << 1)
#define CX_FLAG4_FIELD_ID	(1 << 3)
#define CX_FLAG4_BY_YCCR	(1 << 6)
#define CX_FLAG4_CHROMA_BW	(1 << 7)

#define CX_FLAG4_MASK		(CX_FLAG4_PROG_SC  | CX_FLAG4_SC_PATTERN | \
				 CX_FLAG4_FIELD_ID | CX_FLAG4_BY_YCCR | \
				 CX_FLAG4_CHROMA_BW)

#define CX_FLAG5_ADPT_FF	(1 << 0)
#define CX_FLAG5_FFRTN		(1 << 1)
#define CX_FLAG5_YSELECT	(1 << 2)
#define CX_FLAG5_DIV2		(1 << 4)
#define CX_FLAG5_PLL_32CLK	(1 << 5)
#define CX_FLAG5_PIX_DOUBLE	(1 << 6)
#define CX_FLAG5_EWSSF1		(1 << 8)
#define CX_FLAG5_EWSSF2		(1 << 9)

typedef struct {
  TVBtRegs bt;
  unsigned long msc_db; /* time */
  int dr_limitp;        /* time */
  int dr_limitn;        /* time */
  int db_limitp;        /* time */
  int db_limitn;        /* time */
  int filfsconv;        /* ??   */
  int filincr;          /* ??   */ /* fil4286incr */
  int flags4;           /* time & func */
  int flags5;           /* time & func */
  int mcompy;           /* level */
  int mcompu;           /* level */
  int mcompv;           /* level */
  int y_off;            /* level */
  int hue_adj;          /* time(?) */
  long wsdat;           /* time */
  int wssinc;           /* time */
  int c_altff;          /* func */
  int y_altff;          /* func */
  int c_thresh;         /* func */
  int y_thresh;         /* func */
  int pkfil_sel;        /* func */
} TVCxRegs;

/* -------- Chrontel -------- */

#define CH_FLAG_DAC		1:0
#define CH_FLAG_DAC_MASK	3
#define CH_FLAG_FBAS		2
#define CH_FLAG_SVHS		0
#define CH_FLAG_BOTH		3

typedef struct {
  int dmr_ir;    /* time */
  int dmr_vs;    /* time */
  int dmr_sr;    /* time */
  int ffr_fc;    /* func */
  int ffr_fy;    /* func */
  int ffr_ft;    /* func */
  int vbw_flff;  /* func */    	      /* flag */
  int vbw_cvbw;  /* func */    	      /* flag */
  int vbw_cbw;   /* func */    	      
  int vbw_ypeak; /* func */    	      /* flag */
  int vbw_ysv;   /* func */    	      
  int vbw_ycv;   /* func */    	      /* flag */
  int dacg;      /* level(system) */  /* flag */
  int aciv;      /* time */           /* flag */
  int civh;      /* time */
  int sav;       /* time */
  int blr;       /* level(system) */
  int hpr;       /* func/time */
  int vpr;       /* func/time */
  int ce;        /* func */
  int pll_m;     /* time */
  int pll_n;     /* time */
  int pllcap;    /* time */  /* flag */
  unsigned long fsci;
#if 0 /* Test register; no documentation */
  int ylm; /* y multiplier ? */
  int clm; /* c multiplier ? */
#endif
  int flags;     /* func */
  int mode;      /* for macrovision table */
  int macro;
} TVChRegs;

/* -------- Philips -------- */

#define PH_FLAG1_FISE		(1 << 0)
#define PH_FLAG1_PAL		(1 << 1)
#define PH_FLAG1_SCBW		(1 << 2)
#define PH_FLAG1_YGS		(1 << 4)
#define PH_FLAG1_YFIL		(1 << 8)

#define PH_FLAG1_MASK		(PH_FLAG1_FISE | PH_FLAG1_PAL | \
				 PH_FLAG1_SCBW | PH_FLAG1_YGS)

#define PH_FLAG2_CEN		(1 << 4)
#define PH_FLAG2_CVBSEN0	(1 << 5)
#define PH_FLAG2_CVBSEN1	(1 << 6)
#define PH_FLAG2_VBSEN		(1 << 7)

#define PH_FLAG2_NORMAL		(PH_FLAG2_CEN | PH_FLAG2_CVBSEN0 | \
				 PH_FLAG2_VBSEN)
#define PH_FLAG2_CONVERT	(PH_FLAG2_CEN | PH_FLAG2_CVBSEN0 | \
				 PH_FLAG2_CVBSEN1)
#define PH_FLAG2_FBAS		PH_FLAG2_NORMAL
#define PH_FLAG2_SVHS		PH_FLAG2_NORMAL
#define PH_FLAG2_BOTH		PH_FLAG2_NORMAL

typedef struct {
  int adwhs;  /* time */
  int adwhe;  /* time */
  int xofs;   /* time */
  int xpix;   /* time */
  int xinc;   /* time */
  int hlen;   /* time */
  int fal;    /* time */
  int lal;    /* time */
  int yinc;   /* time */
  int yskip;  /* time */
  int yofso;  /* time */
  int yofse;  /* time */
  int ypix;   /* time */
  int yiwgto; /* time */
  int yiwgte; /* time */
  long pcl;   /* time */
  long fsc;   /* time */
  int idel;   /* init */
  int bs;     /* time(system) */
  int be;     /* time(system) */
  int bsta;   /* level(system) */
  int blckl;  /* level(system) */
  int blnnl;  /* level(system) */
  int chps;   /* time(phase) */
  int gy;     /* level */
  int gcd;    /* level */
  int bcy;    /* func */
  int bcu;    /* func */
  int bcv;    /* func */
  int ccrs;   /* func */
  int gainu;  /* func */
  int gainv;  /* func */
  int flc;    /* time */
  int phres;  /* func(phase) */
  int flags1;  
  int flags2;  
  int macro;   
} TVPhRegs;

/* -------- CRT -------- */

#ifndef V_INTERLACE
#define V_INTERLACE 0x0010
#endif

#ifndef V_DBLSCAN
#define V_DBLSCAN 0x0020
#endif

#ifndef V_HSKEW
#define V_HSKEW 0x0200 
#endif

/* Flags that describe the mode (in TVCrtRegs.PrivFlags) */

#define TV_MODE_TVMODE		(1 << 0)  /* Is TV Mode */
#define TV_MODE_DUALVIEW	(1 << 2)  /* Default dualview */
#define TV_MODE_MACROVISION	(1 << 3)  /* Default macrovision */
#define TV_MODE_NONINTERLACED	(1 << 4)  /* Default noninterlace */
#define TV_MODE_MONOCHROME	(1 << 5)  /* Default monochrome */
#define TV_MODE_CARRIER_LOCK	(1 << 6)  /* Default carrier lock */

#define TV_CAN_DUALVIEW		(1 << 10) /* Has dualview */
#define TV_CAN_MACROVISION	(1 << 11) /* Has macrovision choice */
#define TV_CAN_NONINTERLACED	(1 << 12) /* Has noninterlace */
#define TV_CAN_MONOCHROME	(1 << 13) /* Has monochrome */
#define TV_CAN_CARRIER_LOCK	(1 << 14)  /* Default carrier lock */

#define TV_CAN_BIT		8         /* Bit shift for HAS -> MODE */

#define TV_CAN_MASK             (TV_CAN_DUALVIEW | TV_CAN_MACROVISION | \
				 TV_CAN_MONOCHROME | TV_CAN_NONINTERLACED)

#define TV_DEF_DUALVIEW		(TV_CAN_DUALVIEW | TV_MODE_DUALVIEW)

/* Flags that affect the mode (in DisplayModeRec.PrivFlags) */

#define TV_PRIV_TVMODE		(1 << 0)  /* TV or monitor mode? */
#define	TV_PRIV_BYPASS		(1 << 1)  /* Bypass settings */
#define TV_PRIV_DUALVIEW	(1 << 2)  /* Set single/dualview */

/* We use our own crt structure in the frontend and database, and convert 
 * to DisplayModeRec, XF86VidModeModeLine, or XF86VidModeModeInfo as
 * required. The direct backend uses DisplayModeRec. The 'private'
 * structure is the same in all cases, and defined in nv_type.h. 
 * FIXME: Change this.
 */

typedef struct {
  long Clock;   /* Pixel clock in kHz, 0 = ignore */
  int HDisplay;	/* horizontal timing */
  int HSyncStart;
  int HSyncEnd;
  int HTotal;
  int VDisplay;	/* vertical timing */
  int VSyncStart;
  int VSyncEnd;
  int VTotal;
  int Flags;
  int PrivFlags;
} TVCrtRegs;

/* -------- TV Regs Diff -------- */

typedef struct {
  int ofs;
  VarType type;
  unsigned long ulval;
  int ival;
} TVDiff;

#define TV_FIELD_LONG(r,f,v)	ofs:(char *)(&r.f) - (char *)(&r), \
				type:TYPE_ULONG, ulval:(v)
#define TV_FIELD_INT(r,f,v)	ofs:(char *)(&r.f) - (char *)(&r), \
				type:TYPE_INT, ival:(v)
#define TV_FIELD_END		ofs:-1, type:TYPE_NONE

/* -------- Common -------- */

typedef union {
  TVBtRegs bt;
  TVCxRegs cx;
  TVChRegs ch;
  TVPhRegs ph;
} TVRegs;

typedef enum {
  TV_UNKNOWN = 0,
  TV_OFF     = 1,
  TV_BARS    = 2,
  TV_ON      = 3
} TVState;

typedef enum {
  CONNECT_AUTO    = -2,
  CONNECT_NONE    = -1,
  CONNECT_FBAS    = 0,
  CONNECT_SVHS    = 1,
  CONNECT_BOTH    = 2,
  CONNECT_CONVERT = 3, /* FBAS on both S-VHS lines, for converter */
} TVConnect;

typedef struct {
  int tv_hoffset, tv_voffset;
  int mon_hoffset, mon_voffset;
  int brightness_sig;   /*  -50 -  50 % */
  int contrast;         /* -100 - 100 % */
  int contrast_sig;     /*  -50 -  50 % */
  int saturation;       /* -100 - 100 % */
  int saturation_sig;   /*  -50 -  50 % */
  int phase;            /*  -90 -  90 deg */
  int hue;              /*  -90 -  90 deg */
  int flicker;          /*    0 - 100 % */
  int flicker_adapt;    /*    0 - 100 % */
  int luma_bandwidth;   /*    0 - 100 % */
  int chroma_bandwidth; /*    0 - 100 % */
  int sharpness;        /*    0 - 100 % */
  int cross_color;      /*    0 - 100 % */
  int flags;
  TVConnect connector; 
} TVSettings;

/* FIXME: defines for TVSettings flags */

/* FIXME: Get rid of NVPrivate */

typedef struct {
  char magic[4];
  TVChip chip;
  TVRegs tv;
} NVPrivate;

typedef struct {
  TVSystem	system;
  int		res_x;
  int		res_y;
  char		*size;
  char		*aspect;
  double	hoc;
  double	voc;
  TVCrtRegs	*crt;   
  TVRegs	*tv; 
  TVDiff	*diff;
} TVMode;

#endif /* _TV_CHIP */
/* NVTV backend -- Dirk Thierbach <dthierbach@gmx.de>
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
 * $Id: nvtvd.h,v 1.3 2002/08/02 14:09:05 mshopf Exp $
 *
 * Contents:
 *
 * Common header for all backends
 */

#ifndef _BACKEND_H
#define _BACKEND_H

#ifdef DEBUG_PROBE
#include "tv_common.h"
#endif

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
  TVChip chip; /* chip type */
  void *private; /* identify device, backend private */
} ChipInfo, *ChipPtr;

/* List of all NVidia cards available */

typedef struct card_info {
  struct card_info *next; /* must be 1st entry! */
  char *name; /* must be 2nd entry! (name including bus addr) */
  char *dev;  /* name of device for mmap */
  char *arch; /* architecture */
  CardType type;
  unsigned long reg_base;
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
  void (*setChip) (ChipPtr chip, Bool init);
  void (*setSettings) (TVSettings *set);
  void (*getSettings) (TVSettings *set);
  void (*setMode) (int ModeFlags, TVCrtRegs *crt, TVRegs *tv);
  void (*getMode) (TVCrtRegs *crt, TVRegs *tv);
  void (*setModeSettings) (int ModeFlags, TVCrtRegs *crt, TVRegs *tv,
			   TVSettings *set);
  void (*setTestImage) (int ModeFlags, TVRegs *tv, TVSettings *set);
  long (*getStatus) (int index);
  TVConnect (*getConnection) (void);
  Bool (*findBySize) (TVSystem system, int xres, int yres, char *size, 
    TVMode *mode, TVCrtRegs *crt, TVRegs *tv);
  Bool (*findByOverscan) (TVSystem system, int xres, int yres, 
    double hoc, double voc, TVMode *mode, TVCrtRegs *crt, TVRegs *tv);
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
 * $Id: nvtvd.h,v 1.3 2002/08/02 14:09:05 mshopf Exp $
 *
 * Contents:
 *
 * Header for client backend
 */

#ifndef _BACK_CLIENT_H
#define _BACK_CLIENT_H

Bool back_client_avail (void);
CardPtr back_client_init (void);

/* client backend methods */

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
void bcl_setMode (int ModeFlags, TVCrtRegs *crt, TVRegs *tv);
void bcl_getMode (TVCrtRegs *crt, TVRegs *tv);
void bcl_setModeSettings (int ModeFlags, TVCrtRegs *crt, 
			  TVRegs *tv, TVSettings *set);
void bcl_setTestImage (int ModeFlags, TVRegs *tv, TVSettings *set);
long bcl_getStatus (int index);
TVConnect bcl_getConnection (void);
Bool bcl_findBySize (TVSystem system, int xres, int yres, char *size, 
    TVMode *mode, TVCrtRegs *crt, TVRegs *tv);
Bool bcl_findByOverscan (TVSystem system, int xres, int yres, 
    double hoc, double voc, TVMode *mode, TVCrtRegs *crt, TVRegs *tv);

void bcl_initSharedView (int *view_x, int *view_y);
Bool bcl_getTwinView (int *view_x, int *view_y);
Bool bcl_adjustViewportVideo (int flags, int *view_x, int *view_y);
Bool bcl_serviceViewportVideoCursor (int flags, int cursor_x, int cursor_y, 
  int *view_x, int *view_y);

#endif /* _BACK_CLIENT_H */

