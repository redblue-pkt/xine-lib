/* NVTV xfree -- Dirk Thierbach <dthierbach@gmx.de>
 *
 * Header: All definitions from xfree that are needed.
 *
 */

#ifndef _XFREE_H
#define _XFREE_H 1

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_X
#include <X11/Xmd.h>
#endif

#include "miscstruct.h"

#define __inline__ inline

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

/* Flags for driver messages */
typedef enum {
    X_PROBED,			/* Value was probed */
    X_CONFIG,			/* Value was given in the config file */
    X_DEFAULT,			/* Value is a default */
    X_CMDLINE,			/* Value was given on the command line */
    X_NOTICE,			/* Notice */
    X_ERROR,			/* Error message */
    X_WARNING,			/* Warning message */
    X_INFO,			/* Informational message */
    X_NONE,			/* No prefix */
    X_NOT_IMPLEMENTED		/* Not implemented */
} MessageType;

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
