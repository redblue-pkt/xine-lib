/*
 * Copyright (C) 2000-2002 the xine project
 *
 * This file is part of xine, a free video player.
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
 * $Id: video_out_xvmc.c,v 1.3 2003/10/22 20:38:10 komadori Exp $
 * 
 * video_out_xvmc.c, X11 video motion compensation extension interface for xine
 *
 * based on mpeg2dec code from
 * Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * XvMC image support by Jack Kelliher
 *
 * TODO:
 *  - support non-XvMC output, probably falling back to Xv.
 *  - support XvMC overlays for spu/osd
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_XVMC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#if defined(__FreeBSD__)
#include <machine/param.h>
#endif
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include <X11/extensions/XvMClib.h>
#include <X11/extensions/XvMC.h>

#include "xine.h"
#include "video_out.h"
#include "xine_internal.h"

// TODO - delete these?
#include "alphablend.h"
#include "deinterlace.h"

#include "xineutils.h"
#include "vo_scale.h"

/*
#define LOG
*/
//#define LOG1
//#define LOG
//#define DLOG

//#define PRINTDATA
//#define PRINTFRAME

#define MAX_NUM_FRAMES 8

typedef struct xvmc_macroblock_s {
  xine_macroblocks_t   xine_mc;
  XvMCBlockArray      *blocks;    // pointer to memory for dct block array
  int                  num_blocks;
  XvMCMacroBlock      *macroblockptr;     // pointer to current macro block
  XvMCMacroBlock      *macroblockbaseptr; // pointer to base MacroBlock in MB array
  XvMCMacroBlockArray *macro_blocks;  // pointer to memory for macroblock array
  int                  slices;
} xvmc_macroblocks_t;  

typedef struct cxid_s cxid_t;
struct cxid_s {
  void *xid;
};

typedef struct xvmc_driver_s xvmc_driver_t;

typedef struct {
  int                value;
  int                min;
  int                max;
  Atom               atom;

  cfg_entry_t       *entry;

  xvmc_driver_t       *this;
} xvmc_property_t;


typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, format;
  double             ratio;

  XvMCSurface        surface;

  // temporary Xv only storage
  XvImage           *image;
  XShmSegmentInfo    shminfo;

} xvmc_frame_t;


struct xvmc_driver_s {

  vo_driver_t        vo_driver;

  config_values_t   *config;

  /* X11 / XvMC related stuff */
  Display           *display;
  int                screen;
  Drawable           drawable;
  unsigned int       xvmc_format_yv12;
  unsigned int       xvmc_format_yuy2;
  XVisualInfo        vinfo;
  GC                 gc;
  XvPortID           xv_port;
  XvMCContext        context;
  xvmc_frame_t      *frames[MAX_NUM_FRAMES];

  int                surface_type_id;
  int                max_surface_width;
  int                max_surface_height;
  int                num_frame_buffers;

  int                surface_width;   
  int                surface_height;     
  int                surface_ratio;
  int                surface_format;
  int                surface_flags;
  short              acceleration;

  cxid_t             context_id;
  xvmc_macroblocks_t macroblocks;

  /* all scaling information goes here */
  vo_scale_t         sc;


  XColor             black;
  int                expecting_event; /* completion event handling */

  /* display anatomy */
  double             display_ratio;        /* given by visual parameter
					      from init function            */

  xvmc_property_t    props[VO_NUM_PROPERTIES];
  uint32_t           capabilities;


  xvmc_frame_t      *recent_frames[VO_NUM_RECENT_FRAMES];
  xvmc_frame_t      *cur_frame;
  vo_overlay_t      *overlay;

  /* TODO CLEAN THIS UP all unused vars sizes moved to vo_scale */

  /* size / aspect ratio calculations */

  /* 
   * "delivered" size:
   * frame dimension / aspect as delivered by the decoder
   * used (among other things) to detect frame size changes
   */

  int                delivered_duration;

  /* 
   * "ideal" size :
   * displayed width/height corrected by aspect ratio
   */

  double             ratio_factor;         /* output frame must fullfill:
					      height = width * ratio_factor  */

  
  xvmc_frame_t       deinterlace_frame;
  int                deinterlace_method;
  int                deinterlace_enabled;

  void              *user_data;

  /* gui callback */

  void (*frame_output_cb) (void *user_data,
			   int video_width, int video_height,
			   int *dest_x, int *dest_y, 
			   int *dest_height, int *dest_width,
			   int *win_x, int *win_y);

  char               scratch[256];

  int                use_colorkey;
  uint32_t           colorkey;
};


typedef struct {
  video_driver_class_t driver_class;

  config_values_t     *config;
  XvPortID             xv_port;
  XvAdaptorInfo       *adaptor_info;
  unsigned int         adaptor_num;

  int                  surface_type_id;
  unsigned int         max_surface_width;
  unsigned int         max_surface_height;
  short                acceleration;
} xvmc_class_t;

int gX11Fail;

static void xvmc_render_macro_blocks(vo_frame_t *current_image,
			      vo_frame_t *backward_ref_image,
			      vo_frame_t *forward_ref_image,
			      int picture_structure,
			      int second_field,
			      xvmc_macroblocks_t *macroblocks);


/*********************** XVMC specific routines *********************/

/**************************************************************************/

static void calc_DMV(DMV,dmvector,mvx,mvy,picture_structure,top_field_first)
int DMV[][2];
int *dmvector; /* differential motion vector */
int mvx, mvy;  /* decoded mv components (always in field format) */
int picture_structure;
int top_field_first;
{
  if (picture_structure==VO_BOTH_FIELDS)
  {
    if (top_field_first)
    {
      /* vector for prediction of top field from bottom field */
      DMV[0][0] = ((mvx  +(mvx>0))>>1) + dmvector[0];
      DMV[0][1] = ((mvy  +(mvy>0))>>1) + dmvector[1] - 1;

      /* vector for prediction of bottom field from top field */
      DMV[1][0] = ((3*mvx+(mvx>0))>>1) + dmvector[0];
      DMV[1][1] = ((3*mvy+(mvy>0))>>1) + dmvector[1] + 1;
    }
    else
    {
      /* vector for prediction of top field from bottom field */
      DMV[0][0] = ((3*mvx+(mvx>0))>>1) + dmvector[0];
      DMV[0][1] = ((3*mvy+(mvy>0))>>1) + dmvector[1] - 1;

      /* vector for prediction of bottom field from top field */
      DMV[1][0] = ((mvx  +(mvx>0))>>1) + dmvector[0];
      DMV[1][1] = ((mvy  +(mvy>0))>>1) + dmvector[1] + 1;
    }
  }
  else
  {
    /* vector for prediction from field of opposite 'parity' */
    DMV[0][0] = ((mvx+(mvx>0))>>1) + dmvector[0];
    DMV[0][1] = ((mvy+(mvy>0))>>1) + dmvector[1];

    /* correct for vertical field shift */
    if (picture_structure==VO_TOP_FIELD)
      DMV[0][1]--;
    else
      DMV[0][1]++;
  }
}

static void xvmc_proc_macro_block(
   int x,
   int y,
   int mb_type,
   int motion_type,
   //   int (*PMV)[2][2],
   int (*mv_field_sel)[2],
   int *dmvector,
   int cbp,
   int dct_type,
   vo_frame_t *current_frame,
   vo_frame_t *forward_ref_frame,
   vo_frame_t *backward_ref_frame,
   int picture_structure,
   int second_field,
   int (*f_mot_pmv)[2],
   int (*b_mot_pmv)[2])
{
  xvmc_driver_t *this = (xvmc_driver_t *) current_frame->driver;
  xvmc_macroblocks_t * mbs = &this->macroblocks;
  int top_field_first=current_frame->top_field_first;
  int picture_coding_type = current_frame->picture_coding_type;
        
  mbs->macroblockptr->x = x;
  mbs->macroblockptr->y = y;

  if(mb_type & XINE_MACROBLOCK_INTRA) {
    mbs->macroblockptr->macroblock_type = XVMC_MB_TYPE_INTRA;
  } else {
    mbs->macroblockptr->macroblock_type = 0;
    /* XvMC doesn't support skips */
    if(!(mb_type & (XINE_MACROBLOCK_MOTION_BACKWARD | XINE_MACROBLOCK_MOTION_FORWARD))) {
      mb_type |= XINE_MACROBLOCK_MOTION_FORWARD;
      motion_type = (picture_structure==VO_BOTH_FIELDS) ? XINE_MC_FRAME : XINE_MC_FIELD;
      mbs->macroblockptr->PMV[0][0][0] = 0;
      mbs->macroblockptr->PMV[0][0][1] = 0;
    } else {
      if(mb_type & XINE_MACROBLOCK_MOTION_BACKWARD) {
	mbs->macroblockptr->macroblock_type |= XVMC_MB_TYPE_MOTION_BACKWARD;
	mbs->macroblockptr->PMV[0][1][0] = 
	  b_mot_pmv[0][0];
	mbs->macroblockptr->PMV[0][1][1] = 
	  b_mot_pmv[0][1];
	mbs->macroblockptr->PMV[1][1][0] = 
	  b_mot_pmv[1][0];
	mbs->macroblockptr->PMV[1][1][1] = 
	  b_mot_pmv[1][1];

      }
      if(mb_type & XINE_MACROBLOCK_MOTION_FORWARD) {
	mbs->macroblockptr->macroblock_type |= XVMC_MB_TYPE_MOTION_FORWARD;
	mbs->macroblockptr->PMV[0][0][0] = 
	  f_mot_pmv[0][0];
	mbs->macroblockptr->PMV[0][0][1] = 
	  f_mot_pmv[0][1];
	mbs->macroblockptr->PMV[1][0][0] = 
	  f_mot_pmv[1][0];
	mbs->macroblockptr->PMV[1][0][1] = 
	  f_mot_pmv[1][1];
      }
    }
    if((mb_type & XINE_MACROBLOCK_PATTERN) && cbp)
      mbs->macroblockptr->macroblock_type |= XVMC_MB_TYPE_PATTERN;

    mbs->macroblockptr->motion_type = motion_type;

    if(motion_type == XINE_MC_DMV) {
      int DMV[2][2];

      if(picture_structure==VO_BOTH_FIELDS) {
	calc_DMV(DMV,dmvector, f_mot_pmv[0][0],
		 f_mot_pmv[0][1]>>1, picture_structure,
		 top_field_first);

	mbs->macroblockptr->PMV[1][0][0] = DMV[0][0];
	mbs->macroblockptr->PMV[1][0][1] = DMV[0][1];
	mbs->macroblockptr->PMV[1][1][0] = DMV[1][0];
	mbs->macroblockptr->PMV[1][1][1] = DMV[1][1];
      } else {
	calc_DMV(DMV,dmvector, f_mot_pmv[0][0],
		 f_mot_pmv[0][1]>>1, picture_structure,
		 top_field_first);

	mbs->macroblockptr->PMV[0][1][0] = DMV[0][0];
	mbs->macroblockptr->PMV[0][1][1] = DMV[0][1];
      }
    }

    if((motion_type == XINE_MC_FIELD) || (motion_type == XINE_MC_16X8)) {
      mbs->macroblockptr->motion_vertical_field_select = 0;
      if(mv_field_sel[0][0])
	mbs->macroblockptr->motion_vertical_field_select |= 1;
      if(mv_field_sel[0][1])
	mbs->macroblockptr->motion_vertical_field_select |= 2;
      if(mv_field_sel[1][0])
	mbs->macroblockptr->motion_vertical_field_select |= 4;
      if(mv_field_sel[1][1])
	mbs->macroblockptr->motion_vertical_field_select |= 8;
    }
  } // else of if(mb_type & XINE_MACROBLOCK_INTRA)

  mbs->macroblockptr->index = ((unsigned long)mbs->xine_mc.blockptr -
			     (unsigned long)mbs->xine_mc.blockbaseptr) >> 7;

  mbs->macroblockptr->dct_type = dct_type;
  mbs->macroblockptr->coded_block_pattern = cbp;

  cbp &= 0x3F;
  mbs->macroblockptr->coded_block_pattern = cbp;
  while(cbp) { 
    if(cbp & 1) mbs->macroblockptr->index--;
    cbp >>= 1;
  }

#ifdef PRINTDATA
  printf("\n");
  printf("-- %04d %04d %02x %02x %02x %02x",mbs->macroblockptr->x,mbs->macroblockptr->y,mbs->macroblockptr->macroblock_type,
	 mbs->macroblockptr->motion_type,mbs->macroblockptr->motion_vertical_field_select,mbs->macroblockptr->dct_type);
  printf(" [%04d %04d %04d %04d %04d %04d %04d %04d] ",
	 mbs->macroblockptr->PMV[0][0][0],mbs->macroblockptr->PMV[0][0][1],mbs->macroblockptr->PMV[0][1][0],mbs->macroblockptr->PMV[0][1][1],
	 mbs->macroblockptr->PMV[1][0][0],mbs->macroblockptr->PMV[1][0][1],mbs->macroblockptr->PMV[1][1][0],mbs->macroblockptr->PMV[1][1][1]);

  printf(" %04d %04x\n",mbs->macroblockptr->index,mbs->macroblockptr->coded_block_pattern);

#endif

  mbs->num_blocks++;
  mbs->macroblockptr++;

  if(mbs->num_blocks == mbs->slices) {
#ifdef PRINTDATA
    printf("macroblockptr %lx",  mbs->macroblockptr);
    printf("** RenderSurface %04d %04x\n",picture_structure,
	   second_field ? XVMC_SECOND_FIELD : 0);
    fflush(stdout);
#endif
#ifdef PRINTFRAME
    printf("  target %08x past %08x future %08x\n",
	   current_frame,
	   forward_ref_frame,
	   backward_ref_frame);
#endif
#ifdef PRINTFRAME
    if (picture_coding_type == XINE_PICT_P_TYPE)
      printf(" coding type P_TYPE\n");
    if (picture_coding_type == XINE_PICT_I_TYPE)
      printf(" coding type I_TYPE\n");
    if (picture_coding_type == XINE_PICT_B_TYPE)
      printf(" coding type B_TYPE\n");
    if (picture_coding_type == XINE_PICT_D_TYPE)
      printf(" coding type D_TYPE\n");
    fflush(stdout);
#endif

    if (picture_coding_type == XINE_PICT_B_TYPE)
      xvmc_render_macro_blocks(
			  current_frame,
			  backward_ref_frame,
			  forward_ref_frame,
			  picture_structure,
			  second_field ? XVMC_SECOND_FIELD : 0,
			  mbs);
    if (picture_coding_type == XINE_PICT_P_TYPE)
      xvmc_render_macro_blocks(
			  current_frame,
			  NULL,
			  forward_ref_frame,
			  picture_structure,
			  second_field ? XVMC_SECOND_FIELD : 0,
			  mbs);
    if (picture_coding_type == XINE_PICT_I_TYPE)
      xvmc_render_macro_blocks(
			  current_frame,
			  NULL,
			  NULL,
			  picture_structure,
			  second_field ? XVMC_SECOND_FIELD : 0,
			  mbs);
       
    mbs->num_blocks = 0;
    mbs->macroblockptr = mbs->macroblockbaseptr;
    mbs->xine_mc.blockptr = mbs->xine_mc.blockbaseptr;
  }
}

static uint32_t xvmc_get_capabilities (vo_driver_t *this_gen) {

  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_get_capabilities\n");
#endif

  return this->capabilities;
}

static void xvmc_frame_field (vo_frame_t *vo_img, int which_field) {
  xvmc_driver_t *this = (xvmc_driver_t *) vo_img->driver;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_frame_field\n");
#endif
  this->macroblocks.num_blocks = 0;
  this->macroblocks.macroblockptr = this->macroblocks.macroblockbaseptr;
  this->macroblocks.xine_mc.blockptr = this->macroblocks.xine_mc.blockbaseptr;
}

static void xvmc_frame_dispose (vo_frame_t *vo_img) {

  xvmc_frame_t  *frame = (xvmc_frame_t *) vo_img ;
  xvmc_driver_t *this = (xvmc_driver_t *) vo_img->driver;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_frame_dispose\n");
#endif

  // TODO - clean up of images/surfaces and frames
  //  Note this function is not really needed  
  // set_context does the work

  if (frame->image) {
      XLockDisplay (this->display);
      XFree (frame->image);
      XUnlockDisplay (this->display);
  }

  free (frame);
}

static void xvmc_render_macro_blocks(vo_frame_t *current_image,
				     vo_frame_t *backward_ref_image,
				     vo_frame_t *forward_ref_image,
				     int picture_structure,
				     int second_field,
				     xvmc_macroblocks_t *macroblocks) {

  xvmc_driver_t *this           = (xvmc_driver_t *) current_image->driver;
  xvmc_frame_t  *current_frame  = (xvmc_frame_t *)  current_image;
  xvmc_frame_t  *forward_frame  = (xvmc_frame_t *)  forward_ref_image;
  xvmc_frame_t  *backward_frame = (xvmc_frame_t *)  backward_ref_image;



#ifdef LOG
  printf ("video_out_xvmc: xvmc_render_macro_blocks\n");
  printf ("                slices %d 0x%08lx 0x%08lx 0x%08lx\n",
	  macroblocks->slices,
  	  (long) current_frame, (long) backward_frame,
  	  (long) forward_frame);

  //  printf ("                        slices %d 0x%08lx 0x%08lx 0x%08lx\n",macroblocks->slices,
  //  	  (long) current_frame->surface, (long) backward_frame->surface,
  //  	  (long) forward_frame->surface);
  fflush(stdout);
#endif

  /* XvMCSyncSurface(this->display,&current_frame->surface); */
  if(forward_frame) {
    if(backward_frame) {
      XvMCRenderSurface(this->display, &this->context, picture_structure,
			&current_frame->surface,
			&forward_frame->surface,
			&backward_frame->surface,
			second_field,
			macroblocks->slices, 0, macroblocks->macro_blocks,
			macroblocks->blocks);
    } else {
      XvMCRenderSurface(this->display, &this->context, picture_structure,
			&current_frame->surface,
			&forward_frame->surface,
			NULL,
			second_field,
			macroblocks->slices, 0, macroblocks->macro_blocks,
			macroblocks->blocks);
    }
  } else {
    if(backward_frame) {
      XvMCRenderSurface(this->display, &this->context, picture_structure,
			&current_frame->surface,
			NULL,
			&backward_frame->surface,
			second_field,
			macroblocks->slices, 0, macroblocks->macro_blocks,
			macroblocks->blocks);
    } else {
      XvMCRenderSurface(this->display, &this->context, picture_structure,
			&current_frame->surface,
			NULL,
			NULL,
			second_field,
			macroblocks->slices, 0, macroblocks->macro_blocks,
			macroblocks->blocks);
    }
  }

  //   XvMCFlushSurface(this->display, &current_frame->surface);

#ifdef LOG
  printf ("video_out_xvmc: xvmc_render_macro_blocks done\n");
  fflush(stdout);
#endif
}

static vo_frame_t *xvmc_alloc_frame (vo_driver_t *this_gen) {

  xvmc_frame_t     *frame ;
  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_alloc_frame\n");
#endif

  frame = (xvmc_frame_t *) malloc (sizeof (xvmc_frame_t));
  memset (frame, 0, sizeof(xvmc_frame_t));

  if (frame == NULL) {
    printf ("xvmc_alloc_frame: out of memory\n");
    return (NULL);
  }

  // keep track of frames and how many frames alocated.
  this->frames[this->num_frame_buffers++] = frame;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */

  frame->vo_frame.proc_slice = NULL;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = xvmc_frame_field;
  frame->vo_frame.dispose    = xvmc_frame_dispose;
  frame->vo_frame.proc_macro_block = xvmc_proc_macro_block;

  frame->vo_frame.driver  = this_gen;

    return (vo_frame_t *) frame;
}

static cxid_t *xvmc_set_context (xvmc_driver_t *this,
				 uint32_t width, uint32_t height,
				 double ratio, int format, int flags,
				 xine_macroblocks_t *macro_blocks) {

  int           result = 0;
  int           i;
  int           slices = 1;
  xvmc_macroblocks_t *macroblocks = (xvmc_macroblocks_t *) macro_blocks;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_set_context %dx%d %04x\n",width,height,format);
#endif

  //initialize block & macro block pointers first time
  if(macroblocks->blocks == NULL ||  macroblocks->macro_blocks == NULL) {
    macroblocks->blocks = malloc(sizeof(XvMCBlockArray));
    macroblocks->macro_blocks = malloc(sizeof(XvMCMacroBlockArray));
    memset (macroblocks->blocks, 0, sizeof(XvMCBlockArray));
    memset (macroblocks->macro_blocks, 0, sizeof(XvMCMacroBlockArray));
#ifdef LOG
    printf("	macroblocks->blocks %lx ->macro_blocks %lx\n",macroblocks->blocks,macroblocks->macro_blocks);
#endif
  }

  if((this->context_id.xid != NULL)   &&
     (width  == this->surface_width)  &&
     (height == this->surface_height) &&
     (format == this->surface_format) &&
     (flags  == this->surface_flags)) {

    // don't need to change  context
#ifdef LOG
    printf ("video_out_xvmc: didn't change context\n");
#endif
    return(&this->context_id);

  } else { 
    if(this->context_id.xid != NULL) {

      // flush any drawing and wait till we are done with the old stuff
      // blow away the old stuff
#ifdef LOG
      printf ("video_out_xvmc: freeing previous context\n");
      fflush(stdout);
#endif

      XvMCDestroyBlocks(this->display, macroblocks->blocks);
      XvMCDestroyMacroBlocks(this->display, macroblocks->macro_blocks);

      for(i = 0; i < this->num_frame_buffers; i++) {
	XvMCFlushSurface(this->display, &this->frames[i]->surface);
	XvMCSyncSurface(this->display, &this->frames[i]->surface);

	XvMCDestroySurface(this->display, &this->frames[i]->surface);
      }
      XvMCDestroyContext(this->display, &this->context);
      this->context_id.xid = NULL;
    }

#ifdef DLOG
     printf("CreateContext  w %d h %d id %x portNum %x\n",width,height,this->surface_type_id,(int)this->xv_port);
#endif

    // now create a new context
    result = XvMCCreateContext(this->display, this->xv_port, 
			       this->surface_type_id, 
			       width, height, XVMC_DIRECT, &this->context);

    if(result != Success) {
      fprintf(stderr, "set_context: couldn't create XvMCContext\n");
      macroblocks->xine_mc.xvmc_accel=0;
      abort();
    }

    this->context_id.xid = (void *)this->context.context_id;

    for(i = 0; i < this->num_frame_buffers; i++) {
      result = XvMCCreateSurface(this->display, &this->context,
				 &this->frames[i]->surface);
      if(result != Success) {
	XvMCDestroyContext(this->display, &this->context);
	fprintf(stderr, "set_context: couldn't create XvMCSurfaces\n");
	this->context_id.xid = NULL;
	macroblocks->xine_mc.xvmc_accel=0;
	abort();
      }
#ifdef LOG
      printf ("  CreatedSurface %d 0x%lx\n",i,(long)&this->frames[i]->surface);
#endif 

    }

    slices = (slices * width/16);

#ifdef DLOG
   printf("CreateBlocks  slices %d\n",slices);
#endif

    result = XvMCCreateBlocks(this->display, &this->context, slices * 6,
		     macroblocks->blocks);
    if(result != Success) {
      fprintf(stderr, "set_context: ERROR XvMCCreateBlocks failed\n");
      macroblocks->xine_mc.xvmc_accel=0;
      abort();
    }
    result =XvMCCreateMacroBlocks(this->display, &this->context, slices,
			  macroblocks->macro_blocks);
    if(result != Success) {
      fprintf(stderr, "set_context: ERROR XvMCCreateMacroBlocks failed\n");
      macroblocks->xine_mc.xvmc_accel=0;
      abort();
    }

#ifdef LOG
      printf ("  Created bock and macro block arrays\n");
#endif

    macroblocks->xine_mc.blockbaseptr = macroblocks->blocks->blocks;
    macroblocks->xine_mc.blockptr = macroblocks->xine_mc.blockbaseptr;
    macroblocks->num_blocks = 0;
    macroblocks->macroblockbaseptr = macroblocks->macro_blocks->macro_blocks;
    macroblocks->macroblockptr = macroblocks->macroblockbaseptr;
    macroblocks->slices=slices;
    macroblocks->xine_mc.xvmc_accel=this->acceleration;

    return(&this->context_id);
  }
}

int HandleXError (Display *display, XErrorEvent *xevent) {

  char str [1024];

  XGetErrorText (display, xevent->error_code, str, 1024);

  printf ("received X error event: %s\n", str);

  gX11Fail = 1;
  return 0;

}

static void x11_InstallXErrorHandler (xvmc_driver_t *this)
{
  XSetErrorHandler (HandleXError);
  XFlush (this->display);
}

static void x11_DeInstallXErrorHandler (xvmc_driver_t *this)
{
  XSetErrorHandler (NULL);
  XFlush (this->display);
}

static XvImage *create_ximage (xvmc_driver_t *this, XShmSegmentInfo *shminfo,
			       int width, int height, int format) {

  unsigned int  xvmc_format;
  XvImage      *image=NULL;

#ifdef LOG
  printf ("video_out_xvmc: create_ximage\n");
#endif

  switch (format) {
  case XINE_IMGFMT_YV12:
  case XINE_IMGFMT_XVMC:
    xvmc_format = this->xvmc_format_yv12;
    break;
  case XINE_IMGFMT_YUY2:
    xvmc_format = this->xvmc_format_yuy2;
    break;
  default:
    fprintf (stderr, "create_ximage: unknown format %08x\n",format);
    abort();
  }

  /*
   *  plain Xv
   */

  if (1) {

    char *data;

    switch (format) {
    case XINE_IMGFMT_YV12:
    case XINE_IMGFMT_XVMC:
      data = malloc (width * height * 3/2);
      break;
    case XINE_IMGFMT_YUY2:
      data = malloc (width * height * 2);
      break;
    default:
      fprintf (stderr, "create_ximage: unknown format %08x\n",format);
      abort();
    }

    image = XvCreateImage (this->display, this->xv_port,
			   xvmc_format, data, width, height);
  }
  return image;
}

static void dispose_ximage (xvmc_driver_t *this,
			    XShmSegmentInfo *shminfo,
			    XvImage *myimage) {

#ifdef LOG
  printf ("video_out_xvmc: dispose_ximage\n");
#endif

    XFree (myimage);
}

static void xvmc_update_frame_format (vo_driver_t *this_gen,
				    vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height,
				    double ratio, int format, int flags) {

  xvmc_driver_t  *this = (xvmc_driver_t *) this_gen;
  xvmc_frame_t   *frame = (xvmc_frame_t *) frame_gen;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_update_frame_format\n");
#endif

  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

#ifdef LOG
    printf ("video_out_xvmc: updating frame to %d x %d (ratio=%f, format=%08x)\n",width,height,ratio,format);
#endif
    XLockDisplay (this->display);

    /*
     * (re-) allocate xvimage
     */

    if (frame->image) {
      dispose_ximage (this, &frame->shminfo, frame->image);
      frame->image = NULL;
    }

    frame->image = create_ximage (this, &frame->shminfo, width, height, format);

    frame->vo_frame.pitches[0] = frame->image->pitches[0];
    frame->vo_frame.pitches[1] = frame->image->pitches[2];
    frame->vo_frame.pitches[2] = frame->image->pitches[1];
    frame->vo_frame.base[0] = frame->image->data + frame->image->offsets[0];
    frame->vo_frame.base[1] = frame->image->data + frame->image->offsets[2];
    frame->vo_frame.base[2] = frame->image->data + frame->image->offsets[1];
    
    frame->width  = width;
    frame->height = height;
    frame->format = format;

    XUnlockDisplay (this->display);
  }

  frame->ratio = ratio;

  frame->vo_frame.macroblocks = (xine_macroblocks_t *)&this->macroblocks;
  if( flags & VO_NEW_SEQUENCE_FLAG ) {
    xvmc_set_context (this, width, height, ratio, format, flags,
                      frame->vo_frame.macroblocks);
  }
  this->macroblocks.num_blocks = 0;
  this->macroblocks.macroblockptr = this->macroblocks.macroblockbaseptr;
  this->macroblocks.xine_mc.blockptr = this->macroblocks.xine_mc.blockbaseptr;
}

static void xvmc_clean_output_area (xvmc_driver_t *this) {

#ifdef LOG
  printf ("video_out_xvmc: xvmc_clean_output_area\n");
#endif

  XLockDisplay (this->display);

  XSetForeground (this->display, this->gc, this->black.pixel);

  XFillRectangle(this->display, this->drawable, this->gc,
		 this->sc.gui_x, this->sc.gui_y, this->sc.gui_width, this->sc.gui_height);

  if (this->use_colorkey) {
    XSetForeground (this->display, this->gc, this->colorkey);
    XFillRectangle (this->display, this->drawable, this->gc,
		    this->sc.output_xoffset, this->sc.output_yoffset, 
		    this->sc.output_width, this->sc.output_height);
  }
  
  XUnlockDisplay (this->display);
}

/*
 * convert delivered height/width to ideal width/height
 * taking into account aspect ratio and zoom factor
 */

static void xvmc_compute_ideal_size (xvmc_driver_t *this) {

  vo_scale_compute_ideal_size( &this->sc );
}


/*
 * make ideal width/height "fit" into the gui
 */

static void xvmc_compute_output_size (xvmc_driver_t *this) {

 vo_scale_compute_output_size( &this->sc );

}

static void xvmc_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay) {

  xvmc_frame_t   *frame = (xvmc_frame_t *) frame_gen;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_overlay_blend\n");
#endif

  /* Alpha Blend here
   * As XV drivers improve to support Hardware overlay, we will change this function.
   */
  
  if (overlay->rle) {
    if (frame->format == XINE_IMGFMT_YV12)
      blend_yuv(frame->vo_frame.base, overlay, frame->width, frame->height, frame->vo_frame.pitches);
    else
      blend_yuy2(frame->vo_frame.base[0], overlay, frame->width, frame->height, frame->vo_frame.pitches[0]);
  }

}

static void xvmc_add_recent_frame (xvmc_driver_t *this, xvmc_frame_t *frame) {
  int i;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_add_recent_frame\n");
#endif

  i = VO_NUM_RECENT_FRAMES-1;
  if( this->recent_frames[i] )
    this->recent_frames[i]->vo_frame.free
       (&this->recent_frames[i]->vo_frame);

  for( ; i ; i-- )
    this->recent_frames[i] = this->recent_frames[i-1];

  this->recent_frames[0] = frame;
}

/* currently not used - we could have a method to call this from video loop */
#if 0
static void xvmc_flush_recent_frames (xvmc_driver_t *this) {

  int i;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_flush_recent_frames\n");
#endif

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ )
  {
    if( this->recent_frames[i] )
      this->recent_frames[i]->vo_frame.free
         (&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }
}
#endif

static int xvmc_redraw_needed (vo_driver_t *this_gen) {
  xvmc_driver_t  *this = (xvmc_driver_t *) this_gen;
  int ret = 0;

  if( this->cur_frame ) {

    this->sc.delivered_height   = this->cur_frame->height;
    this->sc.delivered_width    = this->cur_frame->width;
    this->sc.delivered_ratio    = this->cur_frame->ratio;

    xvmc_compute_ideal_size(this);

    if( vo_scale_redraw_needed( &this->sc ) ) {

      xvmc_compute_output_size (this);

      xvmc_clean_output_area (this);

      ret = 1;
    }
  }
  else
    ret = 1;

  return ret;
}

static void xvmc_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {

  xvmc_driver_t  *this = (xvmc_driver_t *) this_gen;
  xvmc_frame_t   *frame = (xvmc_frame_t *) frame_gen;
  int            status;

#ifdef LOG1
  printf ("video_out_xvmc: xvmc_display_frame %d %x\n",frame_gen->id,frame_gen);
#endif

  if (this->expecting_event) {

    frame->vo_frame.free (&frame->vo_frame);
    this->expecting_event--;
#ifdef LOG
    printf ("video_out_xvmc: xvmc_display_frame... not displayed, waiting for completion event\n");
#endif
  } else {

    /* 
     * queue frames (deinterlacing)
     * free old frames
     */

    xvmc_add_recent_frame (this, frame); /* deinterlacing */

    this->cur_frame = frame;

    /*
     * let's see if this frame is different in size / aspect
     * ratio from the previous one
     */

    if ( (frame->width != this->sc.delivered_width)
	 || (frame->height != this->sc.delivered_height)
	 || (frame->ratio != this->sc.delivered_ratio) ) {
#ifdef LOG
      printf("video_out_xvmc: frame format changed\n");
#endif

      //      this->delivered_width      = frame->width;
      //      this->delivered_height     = frame->height;
      //      this->delivered_ratio      = frame->ratio;
      //      this->delivered_duration   = frame->vo_frame.duration;

      //xvmc_compute_ideal_size (this);
      
      //this->gui_width = 0; /* trigger re-calc of output size */
      this->sc.force_redraw = 1;    /* trigger re-calc of output size */
    }

    /* 
     * tell gui that we are about to display a frame,
     * ask for offset and output size
     */
    xvmc_redraw_needed (this_gen);

    XLockDisplay (this->display);

    XvMCGetSurfaceStatus(this->display, &this->cur_frame->surface, &status);

    if(status & XVMC_RENDERING) {
      printf("--------- current frame is still being rendered %x --------\n",status);
      fflush(stdout);
      XvMCSyncSurface(this->display, &this->cur_frame->surface);
    }

    if (this->deinterlace_enabled &&
	(this->deinterlace_method == DEINTERLACE_ONEFIELD)) {
      XvMCPutSurface(this->display, &this->cur_frame->surface,
		     this->drawable,
		     this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		     this->sc.displayed_width, this->sc.displayed_height,
		     this->sc.output_xoffset, this->sc.output_yoffset,
		     this->sc.output_width, this->sc.output_height,
		     XVMC_TOP_FIELD);
    } else { // WEAVE
      XvMCPutSurface(this->display, &this->cur_frame->surface,
		     this->drawable,
		     this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		     this->sc.displayed_width, this->sc.displayed_height,
		     this->sc.output_xoffset, this->sc.output_yoffset,
		     this->sc.output_width, this->sc.output_height,
		     XVMC_FRAME_PICTURE);
    }

    //    XFlush(this->display);

    XUnlockDisplay (this->display);
    
  }
  /*
  printf ("video_out_xvmc: xvmc_display_frame... done\n");
  */
}

static int xvmc_get_property (vo_driver_t *this_gen, int property) {

  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_get_property\n");
#endif
  
  return this->props[property].value;
}

static void xvmc_property_callback (void *property_gen, xine_cfg_entry_t *entry) {

  xvmc_property_t *property = (xvmc_property_t *) property_gen;
  xvmc_driver_t   *this = property->this;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_property_callback\n");
#endif
  
  XvSetPortAttribute (this->display, this->xv_port,
		      property->atom, entry->num_value);

}

static int xvmc_set_property (vo_driver_t *this_gen,
			    int property, int value) {

  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_set_property %d value %d\n",property,value);
#endif
  
  if (this->props[property].atom != None) {
    /* value is out of bound */
    if((value < this->props[property].min) || (value > this->props[property].max))
      value = (this->props[property].min + this->props[property].max) >> 1;

    XvSetPortAttribute (this->display, this->xv_port,
			this->props[property].atom, value);
    XvGetPortAttribute (this->display, this->xv_port,
			this->props[property].atom,
			&this->props[property].value);

    if (this->props[property].entry)
      this->props[property].entry->num_value = this->props[property].value;

    return this->props[property].value;
  } else {
    switch (property) {
    case VO_PROP_INTERLACED:

      this->props[property].value = value;
      printf("video_out_xvmc: VO_PROP_INTERLACED(%d)\n",
	     this->props[property].value);
      this->deinterlace_enabled = value;
      if (this->deinterlace_method == DEINTERLACE_ONEFIELDXV) {
         xvmc_compute_ideal_size (this);
      }
      break;
    case VO_PROP_ASPECT_RATIO:

      if (value>=XINE_VO_ASPECT_NUM_RATIOS)
	value = XINE_VO_ASPECT_AUTO;

      this->props[property].value = value;
      printf("video_out_xvmc: VO_PROP_ASPECT_RATIO(%d)\n",
	     this->props[property].value);

      xvmc_compute_ideal_size (this);
      xvmc_compute_output_size (this);
      xvmc_clean_output_area (this);

      break;
    case VO_PROP_ZOOM_X:

      if ((value >= VO_ZOOM_MIN) && (value <= VO_ZOOM_MAX)) {
        this->props[property].value = value;
        printf ("video_out_xv: VO_PROP_ZOOM_X = %d\n",
		this->props[property].value);

	this->sc.zoom_factor_x = (double)value / (double)VO_ZOOM_STEP;

	xvmc_compute_ideal_size (this);

	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    case VO_PROP_ZOOM_Y:

      if ((value >= VO_ZOOM_MIN) && (value <= VO_ZOOM_MAX)) {
        this->props[property].value = value;
        printf ("video_out_xv: VO_PROP_ZOOM_Y = %d\n",
		this->props[property].value);

	this->sc.zoom_factor_y = (double)value / (double)VO_ZOOM_STEP;

	xvmc_compute_ideal_size (this);

	this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    } 
  }

  return value;
}

static void xvmc_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {

  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_get_property_min_max\n");
#endif

  *min = this->props[property].min;
  *max = this->props[property].max;
}

static int xvmc_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {

  xvmc_driver_t     *this = (xvmc_driver_t *) this_gen;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_gui_data_exchange\n");
#endif
  
  switch (data_type) {
  case XINE_GUI_SEND_COMPLETION_EVENT: {
   
    XShmCompletionEvent *cev = (XShmCompletionEvent *) data;

    if (cev->drawable == this->drawable) {
      this->expecting_event = 0;

    }

  }
  break;

  case XINE_GUI_SEND_EXPOSE_EVENT: {

    /* XExposeEvent * xev = (XExposeEvent *) data; */
    
    /* FIXME : take care of completion events */

    //    printf ("video_out_xvmc: XINE_GUI_SEND_EXPOSE_EVENT\n");
    
    if (this->cur_frame) {
      int i;

      XLockDisplay (this->display);

      //      XvPutImage(this->display, this->xv_port,
      //		 this->drawable, this->gc, this->cur_frame->image,
      //		 this->sc.displayed_xoffset, this->sc.displayed_yoffset,
      //		 this->sc.displayed_width, this->sc.displayed_height,
      //		 this->sc.output_xoffset, this->sc.output_yoffset,
      //		 this->sc.output_width, this->sc.output_height);

      XSetForeground (this->display, this->gc, this->black.pixel);

      for( i = 0; i < 4; i++ ) {
        if( this->sc.border[i].w && this->sc.border[i].h )
          XFillRectangle(this->display, this->drawable, this->gc,
                         this->sc.border[i].x, this->sc.border[i].y,
                         this->sc.border[i].w, this->sc.border[i].h);
      }
      
      if (this->use_colorkey) {
	XSetForeground (this->display, this->gc, this->colorkey);
	XFillRectangle (this->display, this->drawable, this->gc,
			this->sc.output_xoffset, this->sc.output_yoffset, 
			this->sc.output_width, this->sc.output_height);
      }

      XvMCPutSurface(this->display, &this->cur_frame->surface,
		   this->drawable,
		   this->sc.displayed_xoffset, this->sc.displayed_yoffset,
		   this->sc.displayed_width, this->sc.displayed_height,
		   this->sc.output_xoffset, this->sc.output_yoffset,
		   this->sc.output_width, this->sc.output_height,
		   XVMC_FRAME_PICTURE);
      
      XFlush(this->display);
      
      XUnlockDisplay (this->display);
    }
  }
  break;

  case XINE_GUI_SEND_DRAWABLE_CHANGED:
    this->drawable = (Drawable) data;
    this->gc       = XCreateGC (this->display, this->drawable, 0, NULL);
    break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO:
    {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

//      xvmc_translate_gui2video(this, rect->x, rect->y,
//			     &x1, &y1);
//      xvmc_translate_gui2video(this, rect->x + rect->w, rect->y + rect->h,
//			     &x2, &y2);

      vo_scale_translate_gui2video(&this->sc, rect->x, rect->y,
			     &x1, &y1);
      vo_scale_translate_gui2video(&this->sc, rect->x + rect->w, rect->y + rect->h,
			     &x2, &y2);

      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
    }
    break;

  default:
    return -1;
  }

  return 0;
}

static void xvmc_dispose (vo_driver_t *this_gen) {

  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;
  int i;

#ifdef LOG
  printf ("video_out_xvmc: xvmc_dispose\n");
#endif

  if (this->deinterlace_frame.image) {
    dispose_ximage (this, &this->deinterlace_frame.shminfo,
		    this->deinterlace_frame.image);
    this->deinterlace_frame.image = NULL;
  }

  if(this->context_id.xid) {
    for(i = 0; i < this->num_frame_buffers; i++) {
      //      if(useOverlay) /* only one is displaying but I don't want to keep track*/
      XvMCHideSurface(this->display, &this->frames[i]->surface);
      XvMCDestroySurface(this->display, &this->frames[i]->surface);
    }
    //    XvMCDestroyBlocks(this->display, &macroblocks->blocks);
    //    XvMCDestroyMacroBlocks(this->display, &macroblocks->macro_blocks);
    XvMCDestroyContext(this->display, &this->context);    
  }

  XLockDisplay (this->display);
  if(XvUngrabPort (this->display, this->xv_port, CurrentTime) != Success) {
    printf ("video_out_xvmc: xvmc_dispose: XvUngrabPort() failed.\n");
  }
  XUnlockDisplay (this->display);

  for( i=0; i < VO_NUM_RECENT_FRAMES; i++ )
  {
    if( this->recent_frames[i] )
      this->recent_frames[i]->vo_frame.dispose
         (&this->recent_frames[i]->vo_frame);
    this->recent_frames[i] = NULL;
  }

  free (this);
}

static void xvmc_check_capability (xvmc_driver_t *this,
				 uint32_t capability,
				 int property, XvAttribute attr,
				 int base_id, char *str_prop,
				 char *config_name,
				 char *config_desc) {

  int         int_default;
  cfg_entry_t *entry;

  this->capabilities |= capability;

  /*
   * some Xv drivers (Gatos ATI) report some ~0 as max values, this is confusing.
   */
  if (VO_CAP_COLORKEY && (attr.max_value == ~0))
    attr.max_value = 2147483615;

  this->props[property].min  = attr.min_value;
  this->props[property].max  = attr.max_value;
  this->props[property].atom = XInternAtom (this->display, str_prop, False);

  XvGetPortAttribute (this->display, this->xv_port,
		      this->props[property].atom, &int_default);

  printf ("video_out_xvmc: port attribute %s (%d) value is %d\n",
	  str_prop, property, int_default);

  if (config_name) {
    /* is this a boolean property ? */
    if ((attr.min_value == 0) && (attr.max_value == 1)) {
      this->config->register_bool (this->config, config_name, int_default,
				   config_desc,
				   NULL, 10, xvmc_property_callback, &this->props[property]);

    } else {
      this->config->register_range (this->config, config_name, int_default,
				    this->props[property].min, this->props[property].max,
				    config_desc,
				    NULL, 10, xvmc_property_callback, &this->props[property]);
    }

    entry = this->config->lookup_entry (this->config, config_name);

    this->props[property].entry = entry;

    xvmc_set_property (&this->vo_driver, property, entry->num_value);

    if (capability == VO_CAP_COLORKEY) {
      this->use_colorkey = 1;
      this->colorkey = entry->num_value;
    }
  } else
    this->props[property].value  = int_default;
}

static void xvmc_update_deinterlace(void *this_gen, xine_cfg_entry_t *entry)
{
  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;

  //#ifdef LOG
  printf ("video_out_xvmc: xvmc_update_deinterlace method = %d\n",entry->num_value);
  //#endif
  
  this->deinterlace_method = entry->num_value;
}

static void xvmc_update_XV_DOUBLE_BUFFER(void *this_gen, xine_cfg_entry_t *entry)
{
  xvmc_driver_t *this = (xvmc_driver_t *) this_gen;
  Atom atom;
  int xvmc_double_buffer;
  
  xvmc_double_buffer = entry->num_value;
  
  atom = XInternAtom (this->display, "XV_DOUBLE_BUFFER", False);

  XvSetPortAttribute (this->display, this->xv_port, atom, xvmc_double_buffer);
  printf("video_out_xvmc: double buffering mode = %d\n",xvmc_double_buffer);
}

static vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {

  xvmc_class_t         *class = (xvmc_class_t *) class_gen;
  config_values_t      *config = class->config;
  xvmc_driver_t        *this = NULL;
  Display              *display = NULL;
  unsigned int          i, formats;
  XvPortID              xv_port = class->xv_port;
  XvAttribute          *attr;
  XvImageFormatValues  *fo;
  int                   nattr;
  x11_visual_t         *visual = (x11_visual_t *) visual_gen;
  XColor                dummy;
  //  XvImage              *myimage;

#ifdef LOG
  printf ("video_out_xvmc: open_plugin\n");
#endif

  display = visual->display;

  // TODO ??? 
  this = malloc (sizeof (xvmc_driver_t));

  if (!this) {
    printf ("video_out_xvmc: malloc failed\n");
    return NULL;
  }
 
  memset (this, 0, sizeof(xvmc_driver_t));

  this->display            = visual->display;
  this->overlay            = NULL;
  this->screen             = visual->screen;
  this->xv_port            = class->xv_port;
  this->config             = config;

  vo_scale_init (&this->sc, 1, 0, config );

  this->sc.frame_output_cb   = visual->frame_output_cb;
  this->sc.user_data         = visual->user_data;

  this->drawable           = visual->d;
  this->gc                 = XCreateGC(this->display, this->drawable, 0, NULL);
  this->capabilities       = VO_CAP_XVMC_MOCOMP;
  this->expecting_event    = 0;

  this->surface_type_id    = class->surface_type_id;
  this->max_surface_width  = class->max_surface_width;
  this->max_surface_height = class->max_surface_height;
  this->context_id.xid     = NULL;
  this->num_frame_buffers  = 0;
  this->acceleration       = class->acceleration;

  // TODO CLEAN UP THIS
  this->user_data          = visual->user_data;

  this->deinterlace_method = 0;
  this->deinterlace_frame.image = NULL;
  this->use_colorkey       = 0;
  this->colorkey           = 0;

  XAllocNamedColor(this->display,
		   DefaultColormap(this->display, this->screen),
		   "black", &this->black, &dummy);

  this->vo_driver.get_capabilities     = xvmc_get_capabilities;
  this->vo_driver.alloc_frame          = xvmc_alloc_frame;
  this->vo_driver.update_frame_format  = xvmc_update_frame_format;
  this->vo_driver.overlay_blend        = xvmc_overlay_blend;
  this->vo_driver.display_frame        = xvmc_display_frame;
  this->vo_driver.get_property         = xvmc_get_property;
  this->vo_driver.set_property         = xvmc_set_property;
  this->vo_driver.get_property_min_max = xvmc_get_property_min_max;
  this->vo_driver.gui_data_exchange    = xvmc_gui_data_exchange;
  this->vo_driver.dispose              = xvmc_dispose;
  this->vo_driver.redraw_needed        = xvmc_redraw_needed;

  /*
   * init properties
   */

  for (i=0; i<VO_NUM_PROPERTIES; i++) {
    this->props[i].value = 0;
    this->props[i].min   = 0;
    this->props[i].max   = 0;
    this->props[i].atom  = None;
    this->props[i].entry = NULL;
    this->props[i].this  = this;
  }

  this->props[VO_PROP_INTERLACED].value     = 0;
  this->props[VO_PROP_ASPECT_RATIO].value   = XINE_VO_ASPECT_AUTO;
  this->props[VO_PROP_ZOOM_X].value    = 100;
  this->props[VO_PROP_ZOOM_Y].value    = 100;
  this->props[VO_PROP_MAX_NUM_FRAMES].value = MAX_NUM_FRAMES;

  /*
   * check this adaptor's capabilities
   */

  if(this->acceleration&XINE_VO_IDCT_ACCEL) 
    this->capabilities |= VO_CAP_XVMC_IDCT;

  attr = XvQueryPortAttributes(display, xv_port, &nattr);
  if(attr && nattr) {
    int k;

    for(k = 0; k < nattr; k++) {
      if((attr[k].flags & XvSettable) && (attr[k].flags & XvGettable)) {
	if(!strcmp(attr[k].name, "XV_HUE")) {
	  xvmc_check_capability (this, VO_CAP_HUE,
				 VO_PROP_HUE, attr[k],
				 class->adaptor_info[class->adaptor_num].base_id, "XV_HUE",
				 NULL, NULL);

	} else if(!strcmp(attr[k].name, "XV_SATURATION")) {
	  xvmc_check_capability (this, VO_CAP_SATURATION,
				 VO_PROP_SATURATION, attr[k],
				 class->adaptor_info[class->adaptor_num].base_id, "XV_SATURATION",
				 NULL, NULL);

	} else if(!strcmp(attr[k].name, "XV_BRIGHTNESS")) {
	  xvmc_check_capability (this, VO_CAP_BRIGHTNESS,
				 VO_PROP_BRIGHTNESS, attr[k],
				 class->adaptor_info[class->adaptor_num].base_id, "XV_BRIGHTNESS",
				 NULL, NULL);

	} else if(!strcmp(attr[k].name, "XV_CONTRAST")) {
	  xvmc_check_capability (this, VO_CAP_CONTRAST,
				 VO_PROP_CONTRAST, attr[k],
				 class->adaptor_info[class->adaptor_num].base_id, "XV_CONTRAST",
				 NULL, NULL);

	} else if(!strcmp(attr[k].name, "XV_COLORKEY")) {
	  xvmc_check_capability (this, VO_CAP_COLORKEY,
				 VO_PROP_COLORKEY, attr[k],
				 class->adaptor_info[class->adaptor_num].base_id, "XV_COLORKEY",
				 "video.xv_colorkey",
				 _("Colorkey used for Xv video overlay"));

	} else if(!strcmp(attr[k].name, "XV_AUTOPAINT_COLORKEY")) {
	  xvmc_check_capability (this, VO_CAP_AUTOPAINT_COLORKEY,
				 VO_PROP_AUTOPAINT_COLORKEY, attr[k],
				 class->adaptor_info[class->adaptor_num].base_id, "XV_AUTOPAINT_COLORKEY",
				 NULL, NULL);

	} else if(!strcmp(attr[k].name, "XV_DOUBLE_BUFFER")) {
	  int xvmc_double_buffer;
	  xvmc_double_buffer = config->register_bool (config, "video.XV_DOUBLE_BUFFER", 1,
						      _("double buffer to sync video to the retrace"),
						      NULL, 10, xvmc_update_XV_DOUBLE_BUFFER, this);
	  config->update_num(config,"video.XV_DOUBLE_BUFFER",xvmc_double_buffer);
	}
      }
    }
    XFree(attr);
  } else {
    printf("video_out_xvmc: no port attributes defined.\n");
  }


  /*
   * check supported image formats
   */

  fo = XvListImageFormats(display, this->xv_port, (int*)&formats);

  this->xvmc_format_yv12 = 0;
  this->xvmc_format_yuy2 = 0;
  
  for(i = 0; i < formats; i++) {
#ifdef LOG
    printf ("video_out_xvmc: XvMC image format: 0x%x (%4.4s) %s\n",
	    fo[i].id, (char*)&fo[i].id,
	    (fo[i].format == XvPacked) ? "packed" : "planar");
#endif
    if (fo[i].id == XINE_IMGFMT_YV12)  {
      this->xvmc_format_yv12 = fo[i].id;
      this->capabilities |= VO_CAP_YV12;
      printf("video_out_xvmc: this adaptor supports the yv12 format.\n");
    } else if (fo[i].id == XINE_IMGFMT_YUY2) {
      this->xvmc_format_yuy2 = fo[i].id;
      this->capabilities |= VO_CAP_YUY2;
      printf("video_out_xvmc: this adaptor supports the yuy2 format.\n");
    }
  }

  /*
   * try to create a shared image
   * to find out if MIT shm really works, using supported format
   */
  //  myimage = create_ximage (this, &myshminfo, 100, 100, 
  //			   (this->xvmc_format_yv12 != 0) ? XINE_IMGFMT_YV12 : IMGFMT_YUY2);
  //  dispose_ximage (this, &myshminfo, myimage);

  this->deinterlace_method = config->register_enum (config, "video.deinterlace_method", 4,
						    deinterlace_methods, 
						    _("Software deinterlace method (Key I toggles deinterlacer on/off)"),
						    NULL, 10, xvmc_update_deinterlace, this);
  this->deinterlace_enabled = 1;  // default is enabled
  printf("video_out_xvmc: deinterlace_methods %d ",this->deinterlace_method);
  switch(this->deinterlace_method) {
  case DEINTERLACE_NONE: printf("NONE\n"); break;
  case DEINTERLACE_BOB: printf("BOB\n"); break;
  case DEINTERLACE_WEAVE: printf("WEAVE\n"); break;
  case DEINTERLACE_GREEDY: printf("GREEDY\n"); break;
  case DEINTERLACE_ONEFIELD: printf("ONEFIELD\n"); break;
  case DEINTERLACE_ONEFIELDXV: printf("ONEFIELDXV\n"); break;
  case DEINTERLACE_LINEARBLEND: printf("LINEARBLEND\n"); break;
  }
  printf("video_out_xvmc: initialization of plugin successful\n");

  return &this->vo_driver;
}

/*
 * class functions
 */

static char* get_identifier (video_driver_class_t *this_gen) {
  return "XvMC";
}

static char* get_description (video_driver_class_t *this_gen) {
  return _("xine video output plugin using the XvMC X video extension");
}

static void dispose_class (video_driver_class_t *this_gen) {

  xvmc_class_t        *this = (xvmc_class_t *) this_gen;

  XvFreeAdaptorInfo (this->adaptor_info);

  free (this);
}

static void *init_class (xine_t *xine, void *visual_gen) {

  x11_visual_t      *visual = (x11_visual_t *) visual_gen;
  xvmc_class_t      *this;
  Display           *display = NULL;
  unsigned int       adaptors, j = 0;
  unsigned int       ver,rel,req,ev,err;
  XvPortID           xv_port;
  XvAdaptorInfo     *adaptor_info;
  unsigned int       adaptor_num;

  /* XvMC */
  int                IDCTaccel = 0;
  int                useOverlay = 0;
  int                unsignedIntra = 0;
  unsigned int       surface_num, types;
  unsigned int       max_width=0, max_height=0;
  XvMCSurfaceInfo   *surfaceInfo;
  int                surface_type = 0;


  display = visual->display;

  /*
   * check for Xv and  XvMC video support
   */

  printf ("video_out_xvmc: XvMC init_class\n");

  if (Success != XvQueryExtension(display,&ver,&rel,&req,&ev,&err)) {
    printf ("video_out_xvmc: Xv extension not present.\n");
    return NULL;
  }

  if(!XvMCQueryExtension(display, &ev, &err)) {
    printf ("video_out_xvmc: XvMC extension not present.\n");
    return 0;
  }

  /*
   * check adaptors, search for one that supports (at least) yuv12
   */

  if(Success != XvQueryAdaptors(display,DefaultRootWindow(display),
				&adaptors,&adaptor_info))  {
    printf ("video_out_xvmc: XvQueryAdaptors failed.\n");
    return 0;
  }

  xv_port = 0;

  for ( adaptor_num = 0; (adaptor_num < adaptors) && !xv_port; adaptor_num++ ) {
    printf ("video_out_xvmc: checking adaptor %d\n",adaptor_num);
    if (adaptor_info[adaptor_num].type & XvImageMask) {
      surfaceInfo = XvMCListSurfaceTypes(display, adaptor_info[adaptor_num].base_id,
					 &types);
      if(surfaceInfo) {
	for(surface_num  = 0; surface_num < types; surface_num++) {
	  if((surfaceInfo[surface_num].chroma_format == XVMC_CHROMA_FORMAT_420) &&
	      (surfaceInfo[surface_num].mc_type == (XVMC_IDCT | XVMC_MPEG_2))) {
	    max_width = surfaceInfo[surface_num].max_width;
	    max_height = surfaceInfo[surface_num].max_height;
	    for(j = 0; j < adaptor_info[adaptor_num].num_ports; j++) {
	      /* try to grab a port */
	      if(Success == XvGrabPort(display, adaptor_info[adaptor_num].base_id + j,
				       CurrentTime))
		{   
		  xv_port = adaptor_info[adaptor_num].base_id + j;
		  surface_type = surfaceInfo[j].surface_type_id;
		  break;
		}
	    }
	    if(xv_port) break;
	  }
	}
	if(!xv_port) { // try for just XVMC_MOCOMP 
	  printf ("video_out_xvmc: didn't find XVMC_IDCT acceleration trying for MC\n");
	  for(surface_num  = 0; surface_num < types; surface_num++) {
	    if((surfaceInfo[surface_num].chroma_format == XVMC_CHROMA_FORMAT_420) &&
	       ((surfaceInfo[surface_num].mc_type == (XVMC_MOCOMP | XVMC_MPEG_2)))) {
	      printf ("video_out_xvmc: Found XVMC_MOCOMP\n");
	      max_width = surfaceInfo[surface_num].max_width;
	      max_height = surfaceInfo[surface_num].max_height;
	      for(j = 0; j < adaptor_info[adaptor_num].num_ports; j++) {
		/* try to grab a port */
		if(Success == XvGrabPort(display, adaptor_info[adaptor_num].base_id + j,
					 CurrentTime))
		  {   
		    xv_port = adaptor_info[adaptor_num].base_id + j;
		    surface_type = surfaceInfo[j].surface_type_id;
		    break;
		  }
	      }
	      if(xv_port) break;
	    }
	  }
	}
	if(xv_port) {
	  printf ("video_out_xvmc: port %ld surface %d\n",xv_port,j);
	  if(surfaceInfo[j].flags & XVMC_OVERLAID_SURFACE)
	    useOverlay = 1;
	  if(surfaceInfo[j].flags & XVMC_INTRA_UNSIGNED)
	    unsignedIntra = 1;
	  if(surfaceInfo[j].mc_type == (XVMC_IDCT | XVMC_MPEG_2))
	    IDCTaccel = XINE_VO_IDCT_ACCEL + XINE_VO_MOTION_ACCEL;
	  else if(surfaceInfo[j].mc_type == (XVMC_MOCOMP | XVMC_MPEG_2)) {
	    IDCTaccel = XINE_VO_MOTION_ACCEL;
	    if(!unsignedIntra)
	      IDCTaccel |= XINE_VO_SIGNED_INTRA;
	  }
	  else 
	    IDCTaccel = 0;
	  printf ("video_out_xvmc: IDCTaccel %02x\n",IDCTaccel);
	  break;
	}
	XFree(surfaceInfo);
      }
    }
  } // outer for adaptor_num loop
  if (!xv_port) {
    printf ("video_out_xvmc: Xv extension is present but "
	    "I couldn't find a usable yuv12 port.\n");
    printf ("              Looks like your graphics hardware "
	    "driver doesn't support Xv?!\n");
    /* XvFreeAdaptorInfo (adaptor_info); this crashed on me (gb)*/
    return NULL;
  } else {
    printf ("video_out_xvmc: using Xv port %ld from adaptor %s\n"
	    "                for hardware colorspace conversion and scaling\n", xv_port,
            adaptor_info[adaptor_num].name);

    if(IDCTaccel&XINE_VO_IDCT_ACCEL)
      printf ("                idct and motion compensation acceleration \n");
    else if (IDCTaccel&XINE_VO_MOTION_ACCEL)
      printf ("                motion compensation acceleration only\n");
    else
      printf ("                no XvMC support \n");
    printf ("                With Overlay = %d; UnsignedIntra = %d.\n", useOverlay,
	    unsignedIntra);
  }

  this = (xvmc_class_t *) malloc (sizeof (xvmc_class_t));

  if (!this) {
    printf ("video_out_xvmc: malloc failed\n");
    return NULL;
  }

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.get_identifier  = get_identifier;
  this->driver_class.get_description = get_description;
  this->driver_class.dispose         = dispose_class;

  this->config            = xine->config;
  this->xv_port           = xv_port;
  this->adaptor_info      = adaptor_info;
  this->adaptor_num       = adaptor_num;
  this->surface_type_id   = surface_type;
  this->max_surface_width  = max_width;
  this->max_surface_height = max_height;
  this->acceleration      = IDCTaccel;

  printf("video_out_xvmc: init_class done\n");
  return this;
}

static vo_info_t vo_info_xvmc = {
  /* priority must be low until it supports displaying non-accelerated stuff */
  0,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 18, "xvmc", XINE_VERSION_CODE, &vo_info_xvmc, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

#endif
