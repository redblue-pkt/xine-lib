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
 * $Id: tv_chip.h,v 1.2 2003/02/05 00:14:03 miguelfreitas Exp $
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
  CARD_FIRST  = 1,
  CARD_NVIDIA = 1,
  CARD_TDFX   = 2,
  CARD_I810   = 3,
  CARD_XBOX   = 4,
  CARD_LAST   = 4,
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
  TV_CHIP_BY_ADDR   = -1,
  TV_NO_CHIP        = 0,
  TV_CHRONTEL       = 0x1000,
  TV_BROOKTREE      = 0x2000,
  TV_CONEXANT       = 0x2100,
  TV_PHILIPS        = 0x3000,
  TV_PHILIPS_7102   = 0x3010, 
  TV_PHILIPS_7103   = 0x3011,
  TV_PHILIPS_7108   = 0x3012, 
  TV_PHILIPS_7109   = 0x3013,
  TV_PHILIPS_7104   = 0x3020,
  TV_PHILIPS_7105   = 0x3021,
  TV_PHILIPS_7108A  = 0x3022, 
  TV_PHILIPS_7109A  = 0x3023,
} TVChip;

#define TV_PHILIPS_MODEL	0x00f0
#define TV_PHILIPS_MODEL1	0x0010
#define TV_PHILIPS_MODEL2	0x0020

#define TV_ENCODER 0xff00  /* mask for principal encoder type */

/* -------- Host interface flags, all chips -------- */

/* These constants are used both for the host port and the encoder port.
   Direction is seen in both cases from the encoder, so "in" means
   "to encoder", "out" means "to host" */

#define PORT_SYNC_DIR		(1 << 0)
#define PORT_SYNC_OUT		(0 << 0)
#define PORT_SYNC_IN		(1 << 0)

#define PORT_SYNC_MASTER PORT_SYNC_OUT
#define PORT_SYNC_SLAVE  PORT_SYNC_IN
				        
#define PORT_BLANK_MODE		(1 << 1)
#define PORT_BLANK_REGION	(0 << 1)
#define PORT_BLANK_DOTCLK	(1 << 1)

#define PORT_BLANK_DIR		(1 << 2)
#define PORT_BLANK_OUT		(0 << 2)
#define PORT_BLANK_IN		(1 << 2)
				        
#define PORT_PCLK_MODE		(1 << 3)
#define PORT_PCLK_MASTER	(0 << 3)
#define PORT_PCLK_SLAVE		(1 << 3)

#define PORT_VSYNC_POLARITY	(1 << 4)
#define PORT_VSYNC_LOW		(0 << 4)
#define PORT_VSYNC_HIGH		(1 << 4)
				        
#define PORT_HSYNC_POLARITY	(1 << 5)
#define PORT_HSYNC_LOW		(0 << 5)
#define PORT_HSYNC_HIGH		(1 << 5)
				        
#define PORT_BLANK_POLARITY	(1 << 6)
#define PORT_BLANK_LOW		(0 << 6)
#define PORT_BLANK_HIGH		(1 << 6)
				        
#define PORT_PCLK_POLARITY	(1 << 7)
#define PORT_PCLK_LOW		(0 << 7)
#define PORT_PCLK_HIGH		(1 << 7)

#define PORT_FORMAT_MASK	(3 << 8)
#define PORT_FORMAT_MASK_COLOR	(1 << 8)
#define PORT_FORMAT_MASK_ALT	(2 << 8)
#define PORT_FORMAT_RGB		(0 << 8)
#define PORT_FORMAT_YCRCB	(1 << 8)
#define PORT_FORMAT_ALT_RGB	(2 << 8)
#define PORT_FORMAT_ALT_YCRCB	(3 << 8)

#define PORT_XBOX \
  (PORT_VSYNC_HIGH | PORT_HSYNC_HIGH | PORT_SYNC_IN | \
   PORT_BLANK_LOW  | PORT_BLANK_OUT  | PORT_PCLK_HIGH | PORT_PCLK_MASTER | \
   PORT_FORMAT_ALT_YCRCB)

#define PORT_NVIDIA \
  (PORT_VSYNC_HIGH | PORT_HSYNC_HIGH | PORT_SYNC_OUT | \
   PORT_BLANK_LOW  | PORT_BLANK_OUT  | PORT_PCLK_HIGH | PORT_PCLK_MASTER | \
   PORT_FORMAT_RGB)

#define PORT_NVIDIA_SYNC_SLAVE \
  (PORT_VSYNC_HIGH | PORT_HSYNC_HIGH | PORT_SYNC_IN | \
   PORT_BLANK_LOW  | PORT_BLANK_OUT  | PORT_PCLK_HIGH | PORT_PCLK_MASTER | \
   PORT_FORMAT_RGB)

#define PORT_NVIDIA_PCLK_SLAVE \
  (PORT_VSYNC_HIGH | PORT_HSYNC_HIGH | PORT_SYNC_OUT | \
   PORT_BLANK_LOW  | PORT_BLANK_OUT  | PORT_PCLK_HIGH | PORT_PCLK_SLAVE | \
   PORT_FORMAT_RGB)

#define PORT_TDFX \
  (PORT_VSYNC_HIGH | PORT_HSYNC_HIGH  | PORT_SYNC_OUT | \
   PORT_BLANK_LOW  | PORT_BLANK_IN    | PORT_BLANK_REGION | \
   PORT_PCLK_HIGH  | PORT_PCLK_MASTER | PORT_FORMAT_RGB)

#define PORT_I810 \
  (PORT_VSYNC_LOW  | PORT_HSYNC_LOW | PORT_SYNC_IN | \
   PORT_BLANK_LOW  | PORT_BLANK_OUT | PORT_PCLK_HIGH | PORT_PCLK_MASTER | \
   PORT_FORMAT_RGB)

/* -------- Brooktree -------- */

#define BT_FLAG1_NI_OUT		(1 << 0)
#define BT_FLAG1_SETUP		(1 << 1)
#define BT_FLAG1_625LINE	(1 << 2)
#define BT_FLAG1_VSYNC_DUR	(1 << 3)
#define BT_FLAG1_DIS_SCRESET	(1 << 4)
#define BT_FLAG1_PAL_MD		(1 << 5)
#define BT_FLAG1_ECLIP		(1 << 6)
#define BT_FLAG1_EN_ASYNC	(1 << 8)

#define BT_FLAG1_SYSTEM		(BT_FLAG1_VSYNC_DUR | BT_FLAG1_SETUP | \
				 BT_FLAG1_PAL_MD | BT_FLAG1_625LINE)
#define BT_FLAG1_NTSC		(BT_FLAG1_VSYNC_DUR | BT_FLAG1_SETUP)
#define BT_FLAG1_NTSC_J		(BT_FLAG1_VSYNC_DUR)
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

#define CH_FLAG_ACIV		(1 << 4)
#define CH_FLAG_CFRB		(1 << 5)
#define CH_FLAG_CVBW		(1 << 6)
#define CH_FLAG_SCART		(1 << 7)

typedef struct {
  int dmr_ir;    /* time */
  int dmr_vs;    /* time */
  int dmr_sr;    /* time */
  int ffr_fc;    /* func */
  int ffr_fy;    /* func */
  int ffr_ft;    /* func */
  int vbw_flff;  /* func */    	      /* flag */
  int vbw_cbw;   /* func */    	      
  int vbw_ypeak; /* func */    	      /* flag */
  int vbw_ysv;   /* func */    	      
  int vbw_ycv;   /* func */    	      /* flag */
  int dacg;      /* level(system) */  /* flag */
  int civh;      /* func */
  int sav;       /* time */
  int blr;       /* level(system) */
  int hpr;       /* func/time */
  int vpr;       /* func/time */
  int ce;        /* func */
  int te;        /* func, CH7009 only */
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
#define PH_FLAG1_EDGE		(1 << 9)

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

#define PH_FLAG3_DOUBLE		(1 << 0)

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
  int flags3;
  int macro;   
} TVPhRegs;

/* -------- CRT -------- */

/* Flags for devices */

#define DEV_MONITOR		(1 << 0)
#define DEV_TELEVISION		(1 << 1)
#define DEV_FLATPANEL		(1 << 2)
#define DEV_OVERLAY		(1 << 3)

/* Flags that describe the mode, capabilities and defaults 
   (in TVMode.descFlags). TODO: Get rid of Dualview, etc. */

#define TV_DESC_DUALVIEW	(1 << 2)  /* Default dualview */
#define TV_DESC_MACROVISION	(1 << 3)  /* Default macrovision */
#define TV_DESC_NONINTERLACED	(1 << 4)  /* Default noninterlace */
#define TV_DESC_MONOCHROME	(1 << 5)  /* Default monochrome */
#define TV_DESC_CARRIER_LOCK	(1 << 6)  /* Default carrier lock */
#define TV_DESC_COLORFIX	(1 << 7)  /* Default color fix */

#define TV_CAP_DUALVIEW		(1 << 10) /* Has dualview */
#define TV_CAP_MACROVISION	(1 << 11) /* Has macrovision choice */
#define TV_CAP_NONINTERLACED	(1 << 12) /* Has noninterlace */
#define TV_CAP_MONOCHROME	(1 << 13) /* Has monochrome */
#define TV_CAP_CARRIER_LOCK	(1 << 14) /* Has carrier lock */
#define TV_CAP_COLORFIX		(1 << 15) /* Has color fix */

#define TV_CAP_OVERLAY		(1 << 24) /* Is overlay mode */

#define TV_CAP_BIT		8         /* Bit shift for DESC -> CAP */

#define TV_CAP_MASK             (TV_CAP_DUALVIEW | TV_CAP_MACROVISION | \
				 TV_CAP_MONOCHROME | TV_CAP_NONINTERLACED | \
				 TV_CAP_COLORFIX)

#define TV_DEF_DUALVIEW		(TV_CAP_DUALVIEW | TV_DESC_DUALVIEW)

#define NV_FLAG_DOUBLE_SCAN	(1 << 0)
#define NV_FLAG_DOUBLE_PIX	(1 << 1)

typedef struct {
  int HSyncStart;
  int HSyncEnd;
  int HTotal;
  int VSyncStart;
  int VSyncEnd;
  int VTotal;
  int Unknown;
} TVNvSlaveRegs;

typedef struct {
  int HDisplay;    
  int HSyncStart;  
  int HSyncEnd;    
  int HTotal;      
  int HValidStart; 
  int HValidEnd;   
  int HCrtc;       
  int VDisplay;    
  int VSyncStart;  
  int VSyncEnd;    
  int VTotal;      
  int VValidStart; 
  int VValidEnd;   
  int VCrtc;       
} TVNvFpRegs;

typedef struct {
  long clock;    /* Pixel clock in kHz, 0 = ignore */
  int HDisplay;	 /* horizontal timing */
  int HBlankStart;
  int HSyncStart;
  int HSyncEnd;
  int HBlankEnd;
  int HTotal;
  int VDisplay;	 /* vertical timing */
  int VBlankStart;
  int VSyncStart;
  int VSyncEnd;
  int VBlankEnd;
  int VTotal;
  int latency;   /* internal TV clock delay */
  int flags;
  TVNvSlaveRegs slave;
  TVNvFpRegs fp;
} TVNvRegs;

typedef struct {
  int tvHDisplay;	 /* horizontal timing */
  int tvHBlankStart;
  int tvHSyncStart;
  int tvHSyncEnd;
  int tvHBlankEnd;
  int tvHTotal;
  int tvVDisplay;	 /* vertical timing */
  int tvVBlankStart;
  int tvVSyncStart;
  int tvVSyncEnd;
  int tvVBlankEnd;
  int tvVTotal;
  int borderRed;
  int borderGreen;
  int borderBlue;
} TVI810Regs;

#define TDFX_FLAG_CLOCK2X	(1 << 0)
#define TDFX_FLAG_DOUBLE_PIX	(1 << 1)
#define TDFX_FLAG_HALF_MODE	(1 << 2)

typedef struct {
  long clock;    /* Pixel clock in kHz, 0 = ignore */
  int HDisplay;	 /* horizontal timing */
  int HBlankStart;
  int HSyncStart;
  int HSyncEnd;
  int HBlankEnd;
  int HTotal;
  int VDisplay;	 /* vertical timing */
  int VBlankStart;
  int VSyncStart;
  int VSyncEnd;
  int VBlankEnd;
  int VTotal;
  int HScreenSize;
  int VScreenSize;
  int tvHBlankStart;
  int tvHBlankEnd;
  int tvVBlankStart;
  int tvVBlankEnd;
  int tvBlankDelay;
  int tvSyncDelay;
  int tvLatency;     /* internal TV clock delay */
  int flags;
} TVTdfxRegs;

/* -------- Common -------- */

typedef union {
  TVBtRegs bt;
  TVCxRegs cx;
  TVChRegs ch;
  TVPhRegs ph;
} TVEncoderRegs;

typedef union {
  TVNvRegs nv;
  TVI810Regs i810;
  TVTdfxRegs tdfx;
} TVCrtcRegs;

typedef struct {
  int devFlags;    /* device(s) used for this mode */
  TVCrtcRegs crtc;
  TVEncoderRegs enc;
  int portHost;
  int portEnc;
} TVRegs;

/* External mode specification. TODO: Change this into x, y, and list
   of attributes */

typedef struct {
  TVSystem	system;
  int		res_x;
  int		res_y;
  char		*size;
  char		*aspect;
  double	hoc;
  double	voc;
} TVModeSpec;

typedef struct {
  TVModeSpec spec;
  TVRegs regs;
  int descFlags; /* capabilities and defaults */
} TVMode;

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

#endif /* _TV_CHIP */
