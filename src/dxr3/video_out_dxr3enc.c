/* 
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: video_out_dxr3enc.c,v 1.7 2001/10/31 21:57:46 hrm Exp $
 *
 * mpeg1 encoding video out plugin for the dxr3.  
 *
 * modifications to the original dxr3 video out plugin by 
 * Mike Lampard <mike at web2u.com.au>
 * this first standalone version by 
 * Harm van der Heijden <hrm at users.sourceforge.net>
 *
 * Changes are mostly in dxr3_update_frame_format() (init stuff),
 * dxr3_frame_copy() (encoding), and dxr3_display_frame() (send stream
 * to device). The driver and frame structs are changed too.
 *
 * What it does
 * - automatically insert black borders to correct a.r. to 16:9 of 4:3
 *   if needed (these are the only ones that dxr3 supports).
 * - detect framerate from frame's duration value and set it as mpeg1's
 *   framerate. We are hampered a little by the fact that mpeg1 supports
 *   a limited number of frame rates. Most notably 23.976 (NTSC-FILM) 
 *   is missing
 * - (ab)uses the vo_frame_t->copy() function to encode mpeg1 as soon as
 *   the frame is available.
 * - full support for YUY2 output; automatic conversion to YV12
 *
 * TODO:
 * - try ffmpeg encoder instead of libfame
 * - jerkiness issues with mpeg1 output (possibly fixed, see below)
 * - sync issues (possibly fixed, see below)
 * - split off code that is shared with original dxr3 decoder, for
 *   maintainability of the whole thing.
 * - init; sometimes (usually first time after boot) there's no output
 *   to tv. The second attempt usually works.
 * - test with overlay (haven't figured out yet how to get it working
 *   on my system, not even with standard dxr3 driver -- harm)
 *
 ***** Update 28/10/2001 by Harm
 *
 * I've implemented a method for buffering the mpeg data
 * (basically copying it to the frame) for display (read: write to mpeg
 * device) when xine requests it via dxr3_frame_display. It helps sync,
 * but playback is still not smooth.
 *
 * buffering enabled by default, see USE_MPEG_BUFFER define.
 *
 * Moved the mpeg device (/dev/em8300_fd) file descriptor to vo_driver_t;
 * very weird: to be able to use it in frame_display, I must reopen it 
 * there! Is that a thread thing or something? Normally you'd open it in
 * the driver's init function. 
 *
 ***** Update 29/10/2001 by Harm
 *
 * Mike Lampard figured out a solution to the jerky playback problem that
 * seems to work well; write the value 6 to the MV_COMMAND register!
 * I'm guessing this puts the dxr3 playback in some sort of scan mode
 * where it plays frames as soon as it can. This combines well with our
 * method because we deliver them when they need displaying.
 * 
 * This fix is turned on/off by setting USE_MAGIC_REGISTER to 1/0. 
 *
 * Note, we write to the register at every frame; possibly overkill, but
 * there seems to be no noticeable overhead and better safe than sorry...
 *
 * If you still get occasional jerky playback, try lowering the 
 * mpeg1 encoding qualtiy (.xinerc var dxr3enc_quality) first. On my
 * system, there are occasional scenes with high entropy that libfame
 * can't encode at hi quality and 25 fps. Remember, the time it takes
 * to encode a frame is not fixed but depends on the complexity!
 *
 * You wanna hear a funny thing? With the register fix in place, it no
 * longer seems to matter whether USE_MPEG_BUFFER is on or off; in both
 * cases A/V sync seems fine! Weird... I'm leaving it on for the moment,
 * to be safe. It should give the encoder the option to spend more than
 * 1/fps on occasional frames.
 *
 * Other changes:
 * - .xinerc: renamed dxr3_enc_quality to dxr3enc_quality
 * - .xinerc: added dxr3_file for output of mpeg stream to file, for 
 * debugging. set to <none> or delete the entry to send stream to dxr3.
 *
 ***** Update 29/10/2001 (later) by Harm
 *
 * Added support for encoding using libavcodec from ffmpeg. See the defines
 * USE_LIBFAME and USE_FFMPEG (mutually exclusive)
 * These defines are getting quite messy; there's three of them now.
 * Need to make some decisions soon :-)
 * 
 * If using ffmpeg, do not link against libavcodec from xine sources!
 * There's something wrong with that one, you'll get rubbish output.
 * Get the ffmpeg cvs, compile, then copy libavcodec.a to /usr/local/lib
 * or something.
 *
 * At the moment libffmpeg's encoder output is pretty crappy, with weird
 * ghost effects left and right of objects. At the moment using a fixed 
 * quantizer value. Somewhat more cpu intensive than libfame.
 */
 
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/em8300.h>
#include "video_out.h"
#include "xine_internal.h"
#include "dxr3_overlay.h"
/* for fast_memcpy: */
#include "memcpy.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include "../video_out/video_out_x11.h"

#define LOOKUP_DEV "dxr3_devname"
#define DEFAULT_DEV "/dev/em8300"
static char *devname;

#include <math.h>

/* buffer size for encoded mpeg1 stream; will hold one intra frame 
 * at 640x480 typical sizes are <50 kB. 512 kB should be plenty */
#define DEFAULT_BUFFER_SIZE 512*1024

/* 1: enable to buffer the mpeg1 stream; 
 * 0: write to mpeg device immediately;
 * with 1 sync is better, but playback still not smooth (but see below) */
#define USE_MPEG_BUFFER 1 

/* 1: write 6 to MV_COMMAND register. This seems to fix playback problems!
 * 0: don't write to register */
#define USE_MAGIC_REGISTER 1

/* 1: use libfame for encoding
 * 0: use libavcodec from ffmpeg for encoding */
#define USE_LIBFAME 1

#if USE_LIBFAME
# define USE_FFMPEG 0
#else
# define USE_FFMPEG 1
#endif

#if USE_LIBFAME
#include <fame.h>
/* some global stuff for libfame, could use some cleanup :-) */
fame_parameters_t fp = FAME_PARAMETERS_INITIALIZER;
fame_object_t *object;
fame_yuv_t yuv;
fame_context_t *fc; /* needed for fame calls */
#endif

#if USE_FFMPEG
/* use libffmpeg */
#include <ffmpeg/avcodec.h>
AVCodecContext *avc;
AVPicture avp;
AVCodec *avcodec;
#endif

/* mpeg1 buffer, used by both encoders */
unsigned char *buffer;

typedef struct dxr3_driver_s {
	vo_driver_t      vo_driver;
	config_values_t *config;
	int fd_control;
        int fd_video;
	int aspectratio;
	int tv_mode;
	em8300_bcs_t bcs;
        uint8_t *out[3]; /* aligned buffer for YV12 data, copied from frames */
        uint8_t *buf[3]; /* unaligned YV12 buffer */
        int oheight; /* height after adding black bars to correct a.r. */
	/* for overlay */
	dxr3_overlay_t overlay;
	Display *display;
	Drawable win;
	GC gc;     
	XColor color;
	int xpos, ypos;
	int width, height; 
	int overlay_enabled;
	float desired_ratio;

	int video_width;
	int video_iheight; // input height (before adding black bars)
	int video_height; // output height (after adding bars)
	int video_aspect;
	void (*request_dest_size) (char *userdata, int video_width, int video_height, int *dest_x,
        int *dest_y, int *dest_height, int *dest_width);
} dxr3_driver_t;

typedef struct dxr3_frame_s {
  vo_frame_t    vo_frame;
  int           width, height;
  uint8_t       *mem[3]; /* allocated for YV12 or YUY2 buffers */
  uint8_t       *real_base[3]; /* same buffers alligned on 16 bytes */
  int           format;
  dxr3_driver_t *vo_instance; /* points to self, for use in dxr3_frame_copy */
  int           copy_calls; /* counts calls to dxr3_frame_copy function */
#if USE_MPEG_BUFFER
  unsigned char *mpeg; // encoded mpeg data
  unsigned int  mpeg_size; // length of data
#endif
}dxr3_frame_t;

static void *malloc_aligned (size_t alignment, size_t size, void **mem) {
  char *aligned;
   
  aligned = malloc (size+alignment);
  *mem = aligned;
        
  while ((int) aligned % alignment)
    aligned++;
               
  return aligned;
}

static int dxr3_set_property (vo_driver_t *this_gen, int property, int value);

static void dxr3_overlay_adapt_area(dxr3_driver_t *this,
 int dest_x, int dest_y, int dest_width, int dest_height)
{
	XWindowAttributes a;
	Window junkwin;
	int rx, ry;

	XLockDisplay(this->display);

	XSetForeground(this->display, this->gc, this->color.pixel);
	XGetWindowAttributes(this->display, this->win, &a);
    XTranslateCoordinates (this->display, this->win, a.root,
	 dest_x, dest_y, &rx, &ry, &junkwin);

	XUnlockDisplay(this->display);
	
	this->xpos = rx; this->ypos = ry;
	this->width = dest_width; this->height = dest_height;

	dxr3_overlay_set_window(&this->overlay, this->xpos, this->ypos,
	 this->width, this->height);
}

static void dxr3_get_keycolor(dxr3_driver_t *this)
{
	this->color.red   = ((this->overlay.colorkey >> 16) & 0xff) * 256;
	this->color.green = ((this->overlay.colorkey >>  8) & 0xff) * 256;
	this->color.blue  = ((this->overlay.colorkey      ) & 0xff) * 256;

	XAllocColor(this->display, DefaultColormap(this->display,0), &this->color);
}
	
void dxr3_read_config(dxr3_driver_t *this, config_values_t *config)
{
	char* str;

	if (ioctl(this->fd_control, EM8300_IOCTL_GETBCS, &this->bcs))
		fprintf(stderr, "dxr3_vo: cannot read bcs values (%s)\n",
		 strerror(errno));
	this->vo_driver.set_property(&this->vo_driver,
	 VO_PROP_ASPECT_RATIO, ASPECT_FULL);

	str = config->lookup_str(config, "dxr3_tvmode", "default");
	if (!strcmp(str, "ntsc")) {
		this->tv_mode = EM8300_VIDEOMODE_NTSC;
		fprintf(stderr, "dxr3_vo: setting tv_mode to NTSC\n");
	} else if (!strcmp(str, "pal")) {
		this->tv_mode = EM8300_VIDEOMODE_PAL;
		fprintf(stderr, "dxr3_vo: setting tv_mode to PAL 50Hz\n");
	} else if (!strcmp(str, "pal60")) {
		this->tv_mode = EM8300_VIDEOMODE_PAL60;
		fprintf(stderr, "dxr3_vo: setting tv_mode to PAL 60Hz\n");
	} else if (!strcmp(str, "overlay")) {
		this->tv_mode = EM8300_VIDEOMODE_DEFAULT;
		fprintf(stderr, "dxr3_vo: setting up overlay mode\n");
		if (dxr3_overlay_read_state(&this->overlay) == 0) {
			this->overlay_enabled = 1;
			
			str = config->lookup_str(config, "dxr3_keycolor", "0x80a040");
			sscanf(str, "%x", &this->overlay.colorkey);

			str = config->lookup_str(config, "dxr3_color_interval", "50.0");
			sscanf(str, "%f", &this->overlay.color_interval);
		} else {
			fprintf(stderr, "dxr3_vo: please run autocal, overlay disabled\n");
		}
	} else {
		this->tv_mode = EM8300_VIDEOMODE_DEFAULT;
	}
	if (this->tv_mode != EM8300_VIDEOMODE_DEFAULT)
		if (ioctl(this->fd_control, EM8300_IOCTL_SET_VIDEOMODE, &this->tv_mode))
			fprintf(stderr, "dxr3_vo: setting video mode failed.");
}

static uint32_t dxr3_get_capabilities (vo_driver_t *this_gen)
{
	/* Since we have no vo format, we return dummy values here */
	return VO_CAP_YV12 | VO_CAP_YUY2 |
		VO_CAP_SATURATION | VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST;
}

/* This are dummy functions to fill in the frame object */

static void dummy_frame_field (vo_frame_t *vo_img, int which_field)
{
	fprintf(stderr, "dxr3_vo: dummy_frame_field called!\n");
}

static void dxr3_frame_dispose (vo_frame_t *frame_gen)
{
  dxr3_frame_t  *frame = (dxr3_frame_t *) frame_gen; 
  if (frame->mem[0])
    free (frame->mem[0]);
  if (frame->mem[1])
    free (frame->mem[1]);
  if (frame->mem[2])
    free (frame->mem[2]);
#if USE_MPEG_BUFFER
  free(frame->mpeg);
#endif
  free(frame);
}

static void dxr3_frame_copy(vo_frame_t *frame_gen, uint8_t **src);

static vo_frame_t *dxr3_alloc_frame (vo_driver_t *this_gen)
{
  /* dxr3_driver_t  *this = (dxr3_driver_t *) this_gen; */
  dxr3_frame_t   *frame;

  frame = (dxr3_frame_t *) malloc (sizeof (dxr3_frame_t));
  memset (frame, 0, sizeof(dxr3_frame_t));

  frame->vo_frame.copy    = dxr3_frame_copy; 
  frame->vo_frame.field   = dummy_frame_field; 
  frame->vo_frame.dispose = dxr3_frame_dispose;

#if USE_MPEG_BUFFER
  frame->mpeg = (unsigned char *) malloc (DEFAULT_BUFFER_SIZE);
#endif

  return (vo_frame_t*) frame;
}

static void dxr3_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags)
{
  dxr3_driver_t  *this = (dxr3_driver_t *) this_gen; 
  int aspect;  
  dxr3_frame_t  *frame = (dxr3_frame_t *) frame_gen; 
  int image_size, oheight;
  float fps;

  /* reset the copy calls counter (number of calls to dxr3_frame_copy) */	
  frame->copy_calls = 0;
  frame->vo_instance = this;

  aspect = this->aspectratio;
  oheight = this->oheight;

  /* check aspect ratio, see if we need to add black borders */
  if ((this->video_width != width) || (this->video_iheight != height) ||
      (this->video_aspect != ratio_code)) {
    switch (ratio_code) {
    case XINE_ASPECT_RATIO_4_3: /* 4:3 */
      aspect = ASPECT_FULL;
      oheight = height; 
      break;
    case XINE_ASPECT_RATIO_ANAMORPHIC:
      aspect = ASPECT_ANAMORPHIC;
      oheight = height; 
      break;
    default: /* assume square pixel */
      aspect = ASPECT_ANAMORPHIC;
      oheight = (int)(width * 9./16.);
      if (oheight < height) { /* frame too high, try 4:3 */
        aspect = ASPECT_FULL;
        oheight = (int)(width * 3./4.);
      }
    }  
    /* find closest multiple of 16 */
    oheight = 16*(int)(oheight / 16. + 0.5);
    if (oheight < height)
      oheight = height;/* no good, need horizontal bars (not yet) */

    this->oheight = oheight;

    /* Tell the viewers about the aspect ratio stuff. */
    if (oheight - height > 0) 
      printf("dxr3enc: adding %d black lines to get %s a.r.\n", 
              oheight-height, aspect == ASPECT_FULL ? "4:3" : "16:9");

    /* if YUY2 and dimensions changed, we need to re-allocate the
     * internal YV12 buffer */
    if (format == IMGFMT_YUY2) {
      if (this->buf[0]) free(this->buf[0]);  
      if (this->buf[1]) free(this->buf[1]);  
      if (this->buf[2]) free(this->buf[2]);

      image_size = width * oheight;

      this->out[0] = malloc_aligned(16, image_size, (void*)&this->buf[0]);
      this->out[1] = malloc_aligned(16, image_size/4, (void*)&this->buf[1]);
      this->out[2] = malloc_aligned(16, image_size/4, (void*)&this->buf[2]);

      /* fill with black (yuv 16,128,128) */
      memset(this->out[0], 16, image_size);
      memset(this->out[1], 128, image_size/4);
      memset(this->out[2], 128, image_size/4);

      printf("dxr3enc: Using YUY2->YV12 conversion\n");  
    }
  }

  /* if dimensions changed, we need to re-allocate frame memory */
  if ((frame->width != width) || (frame->height != height)) {
    if (frame->mem[0]) {
      free (frame->mem[0]);
      frame->mem[0] = NULL;
    }
    if (frame->mem[1]) {
      free (frame->mem[1]);
      frame->mem[1] = NULL;
    }
    if (frame->mem[2]) {
      free (frame->mem[2]);
      frame->mem[2] = NULL;
    }

    if (format == IMGFMT_YUY2) {
      image_size = width * height; /* does not include black bars */
      /* planar format, only base[0] */
      frame->vo_frame.base[0] = malloc_aligned(16, image_size*2, (void**)&frame->mem[0]);
      frame->vo_frame.base[1] = frame->vo_frame.base[2] = 0;
      frame->real_base[0] = frame->real_base[1] = frame->real_base[2] = 0;
      /* we do the black bar stuff while converting to YV12 */
    }
    else { /* IMGFMT_YV12 */
      image_size = width * oheight; /* includes black bars */
      frame->real_base[0] = malloc_aligned(16,image_size, 
                                             (void**) &frame->mem[0]);
      frame->real_base[1] = malloc_aligned(16,image_size/4, 
                                             (void**) &frame->mem[1]);
      frame->real_base[2] = malloc_aligned(16,image_size/4, 
                                             (void**) &frame->mem[2]);
      /* fill with black (yuv 16,128,128) */
      memset(frame->real_base[0], 16, image_size);
      memset(frame->real_base[1], 128, image_size/4);
      memset(frame->real_base[2], 128, image_size/4);

      /* fix offsets, so the decoder does not see the top black bar */
      frame->vo_frame.base[0] = frame->real_base[0] + width * (oheight - height)/2;
      frame->vo_frame.base[1] = frame->real_base[1] + width/2 * (oheight - height)/4;
      frame->vo_frame.base[2] = frame->real_base[2] + width/2 * (oheight - height)/4;
    }
  }
      
  frame->width  = width;
  frame->height = height;
  frame->format = format;
  this->video_width  = width;
  this->video_iheight = height;
  this->video_height = oheight;
  this->video_aspect = ratio_code;

  /* init encoder if needed */
#if USE_LIBFAME
  if (!fc)
  {
    if (!(fc = fame_open ()))
      puts ("Couldn't start the FAME library");
   
    buffer = (unsigned char *) malloc (DEFAULT_BUFFER_SIZE);
    fp.quality=this->config->lookup_int(this->config,"dxr3enc_quality",90);
    fp.width = width;
    fp.height = oheight;
    fp.profile = "mpeg1";
    fp.coding = "I";
    fp.verbose = 0;   /* we don't need any more info.. thanks :) */ 

    /* start guessing the framerate */
    fps = 90000.0/frame->vo_frame.duration;
    if (fabs(fps - 25) < 0.01) { /* PAL */
      printf("dxr3enc: setting mpeg output framerate to PAL (25 Hz)\n");
      fp.frame_rate_num = 25; fp.frame_rate_den = 1; 
    }  
    else if (fabs(fps - 23.976) < 0.01) { /* NTSC film */
      /* 23.976 not supported by mpeg 1 */
      printf("dxr3enc: setting mpeg output framerate to 24 Hz (should be 23.976)\n");
      fp.frame_rate_num = 24; fp.frame_rate_den = 1; 
    }
    else if (fabs(fps - 29.97) < 0.01) { /* NTSC */
      printf("dxr3enc: setting mpeg output framerate to NTSC (29.97 Hz)\n");
      fp.frame_rate_num = 30000; fp.frame_rate_den = 1001;
    }
    else { /* try 1/fps, but libfame will probably go to PAL */
      fp.frame_rate_num = (int)(fps + 0.5); fp.frame_rate_den = 1;
      printf("dxr3enc: trying to set mpeg output framerate to %d Hz\n",
             fp.frame_rate_num);
    }
    object = fame_get_object (fc, "profile/mpeg1");
    fame_init (fc, &fp, buffer, DEFAULT_BUFFER_SIZE);
    
  }
#endif
#if USE_FFMPEG
  if (!avc)
  {
    avc = malloc(sizeof(AVCodecContext));
    memset(avc, 0, sizeof(AVCodecContext));
    buffer = (unsigned char *) malloc (DEFAULT_BUFFER_SIZE);

    avc->bit_rate = 0; /* using fixed quantizer */
    avc->width = width;
    avc->height = oheight;
    avc->gop_size = 0; /* only intra */
    avc->pix_fmt = PIX_FMT_YUV420P;
    avc->flags = CODEC_FLAG_QSCALE; /* fix qscale = quality */
    avc->quality = 2; /* 1-31 highest-lowest quality */
    /* start guessing the framerate */
    fps = 90000.0/frame->vo_frame.duration;
    avc->frame_rate = (int)(fps*FRAME_RATE_BASE + 0.5);

    if (avcodec_open(avc, avcodec)) {
      printf("dxr3enc: error opening avcodec. Aborting...\n");
      exit(1); /* can't think of a cleaner way at this point */
    }
  }
#endif

  if(this->aspectratio!=aspect)
    dxr3_set_property (this_gen,VO_PROP_ASPECT_RATIO, aspect);

#if USE_MPEG_BUFFER
  /* safeguard */
  frame->mpeg_size = 0;
#endif
}

static void dxr3_frame_copy(vo_frame_t *frame_gen, uint8_t **src)
{
  int size, i, j, hoffset, w2;
  dxr3_frame_t *frame = (dxr3_frame_t *) frame_gen;
  dxr3_driver_t *this = frame->vo_instance;
  uint8_t *y, *u, *v, *yuy2;

  if (frame_gen->bad_frame)
    return;

  if (frame->copy_calls == frame->height/16) {
    /* shouldn't happen */
    printf("dxr3enc: Internal error. Too many calls to dxr3_frame_copy (%d)\n", 
           frame->copy_calls);
    return; 
  }

  if (frame_gen->format == IMGFMT_YV12) {
    y = frame->real_base[0];
    u = frame->real_base[1];
    v = frame->real_base[2];
  }
  else { /* must be YUY2 */
    if (! (this->out[0] && this->out[1] && this->out[2]) ) {
      printf("dxr3enc: Internal error. Internal YV12 buffer not created.\n");
      return;
    }
    /* need conversion */
    hoffset = (this->oheight - frame->height)/2; 
    y = this->out[0] + frame->width*(hoffset + frame->copy_calls*16);
    u = this->out[1] + frame->width/2*(hoffset/2 + frame->copy_calls*8);
    v = this->out[2] + frame->width/2*(hoffset/2 + frame->copy_calls*8);
    yuy2 = src[0];
    w2 = frame->width/2;
    /* we get 16 lines each time */
    for (i=0; i<16; i+=2) {
      for (j=0; j<w2; j++) {
        /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
        *(y++) = *(yuy2++);
        *(u++) = *(yuy2++);
        *(y++) = *(yuy2++);
        *(v++) = *(yuy2++);
      }
      /* down sampling */
      for (j=0; j<w2; j++) {
        /* skip every second line for U and V */
        *(y++) = *(yuy2++);
        yuy2++;
        *(y++) = *(yuy2++);
        yuy2++;
      }
    }
    /* reset for encoder */
    y = this->out[0];
    u = this->out[1];
    v = this->out[2];
  }

  frame->copy_calls++;

  /* frame complete yet? */
  if (frame->copy_calls == frame->height/16) {
#if USE_LIBFAME
    yuv.y=y;
    yuv.u=u;
    yuv.v=v;
    size = fame_encode_frame(fc, &yuv, NULL);
    /* not sure whether libfame does bounds checking, but if we're
     * this close to the limit there's bound to be trouble */
    if (size >= DEFAULT_BUFFER_SIZE) {
      printf("dxr3enc: warning, mpeg buffer too small!\n");
      size = DEFAULT_BUFFER_SIZE;
    }
# if USE_MPEG_BUFFER
    /* copy mpeg data to frame */
    fast_memcpy(frame->mpeg, buffer, size);
    frame->mpeg_size = size;
# endif
#endif
#if USE_FFMPEG
    avp.data[0] = y;
    avp.data[1] = u;
    avp.data[2] = v;
    avp.linesize[0] = frame->width;
    avp.linesize[1] = avp.linesize[2] = frame->width/2;
# if USE_MPEG_BUFFER
    size = avcodec_encode_video(avc, frame->mpeg, DEFAULT_BUFFER_SIZE, &avp);
    frame->mpeg_size = size;
# else
    size = avcodec_encode_video(avc, buffer, DEFAULT_BUFFER_SIZE, &avp);
# endif    
#endif
#if ! USE_MPEG_BUFFER
    /* write to device now */
    if (write(this->fd_video, buffer, size) < 0)
      perror("dxr3enc: writing to video device");
#endif
  }
}

#define MV_COMMAND 0

static void dxr3_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  char tmpstr[256]; 
  dxr3_driver_t *this = (dxr3_driver_t*)this_gen;
  dxr3_frame_t *frame = (dxr3_frame_t*)frame_gen;
#if USE_MAGIC_REGISTER
  em8300_register_t regs; 

  regs.microcode_register=1; 	/* Yes, this is a MC Reg */
  regs.reg = MV_COMMAND;
  regs.val=6; /* Mike's mystery number :-) */
  ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &regs);
#endif
#if USE_MPEG_BUFFER
  if (this->fd_video < 0) {
    snprintf (tmpstr, sizeof(tmpstr), "%s_mv", devname);
    this->fd_video = open(tmpstr, O_WRONLY);
  }
  if (write(this->fd_video, frame->mpeg, frame->mpeg_size) < 0)
    perror("dxr3enc: writing to video device");
#endif
  frame_gen->displayed (frame_gen); 
}

static void dxr3_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
 vo_overlay_t *overlay)
{
	/* dxr3_driver_t *this = (dxr3_driver_t *) this_gen; */
	fprintf(stderr, "dxr3_vo: dummy function dxr3_overlay_blend called!\n");
}

static int dxr3_get_property (vo_driver_t *this_gen, int property)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;
	int val;

	switch (property) {
	case VO_PROP_SATURATION:
		val = this->bcs.saturation;
		break;
		
	case VO_PROP_CONTRAST:
		val = this->bcs.contrast;
		break;
		
	case VO_PROP_BRIGHTNESS:
		val = this->bcs.brightness;
		break;

	case VO_PROP_ASPECT_RATIO:
		val = this->aspectratio;
		break;
	
	case VO_PROP_COLORKEY:
		val = this->overlay.colorkey;
		break;
	default:
		val = 0;
		fprintf(stderr, "dxr3_vo: property %d not implemented!\n", property);
	}

	return val;
}

static int is_fullscreen(dxr3_driver_t *this)
{
	XWindowAttributes a;

	XGetWindowAttributes(this->display, this->win, &a);
	/* this is a good place for gathering the with and height
	 * although it is a mis-use for is_fullscreen */
	this->width  = a.width;
	this->height = a.height;
	
	return a.x==0 && a.y==0 &&
	 a.width  == this->overlay.screen_xres &&
	 a.height == this->overlay.screen_yres;
}

static int dxr3_set_property (vo_driver_t *this_gen, 
			      int property, int value)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;
	int val, bcs_changed = 0;
	int fullscreen;

	switch (property) {
	case VO_PROP_SATURATION:
		this->bcs.saturation = value;
		bcs_changed = 1;
		break;
	case VO_PROP_CONTRAST:
		this->bcs.contrast = value;
		bcs_changed = 1;
		break;
	case VO_PROP_BRIGHTNESS:
		this->bcs.brightness = value;
		bcs_changed = 1;
		break;
	case VO_PROP_ASPECT_RATIO:
		/* xitk-ui just increments the value, so we make
		 * just a two value "loop"
		 */
		if (value > ASPECT_FULL)
			value = ASPECT_ANAMORPHIC;
		this->aspectratio = value;
		fullscreen = is_fullscreen(this);

		if (value == ASPECT_ANAMORPHIC) {
			fprintf(stderr, "dxr3_vo: setting aspect ratio to anamorphic\n");
			if (!this->overlay_enabled || fullscreen)
				val = EM8300_ASPECTRATIO_16_9;
			else /* The overlay window can adapt to the ratio */
				val = EM8300_ASPECTRATIO_4_3;
			this->desired_ratio = 16.0/9.0;
		} else {
			fprintf(stderr, "dxr3_vo: setting aspect ratio to full\n");
			val = EM8300_ASPECTRATIO_4_3;
			this->desired_ratio = 4.0/3.0;
		}

		if (ioctl(this->fd_control, EM8300_IOCTL_SET_ASPECTRATIO, &val))
			fprintf(stderr, "dxr3_vo: failed to set aspect ratio (%s)\n",
			 strerror(errno));
		if (this->overlay_enabled && !fullscreen){
			int foo;
			char *foo2=0;
			this->request_dest_size(foo2,this->width,
			 this->width/this->desired_ratio, &foo, &foo, &foo, &foo);
		}
		break;
	case VO_PROP_COLORKEY:
		fprintf(stderr, "dxr3_vo: VO_PROP_COLORKEY not implemented!");
		this->overlay.colorkey = val;
		break;
	}

	if (bcs_changed)
		if (ioctl(this->fd_control, EM8300_IOCTL_SETBCS, &this->bcs))
			fprintf(stderr, "dxr3_vo: bcs set failed (%s)\n",
			 strerror(errno));
	return value;
}

static void dxr3_get_property_min_max (vo_driver_t *this_gen, 
 int property, int *min, int *max)
{
	/* dxr3_driver_t *this = (dxr3_driver_t *) this_gen;  */

	switch (property) {
	case VO_PROP_SATURATION:
	case VO_PROP_CONTRAST:
	case VO_PROP_BRIGHTNESS:
		*min = 0;
		*max = 1000;
		break;

	default:
		*min = 0;
		*max = 0;
	}
}

static void dxr3_translate_gui2video(dxr3_driver_t *this,
				     int x, int y,
				     int *vid_x, int *vid_y)
{
	x = x * this->video_width / this->width;
	y = y * this->video_height / this->height;
	*vid_x = x;
	*vid_y = y;
}

static int dxr3_gui_data_exchange (vo_driver_t *this_gen, 
				 int data_type, void *data)
{
	dxr3_driver_t *this = (dxr3_driver_t*) this_gen;
	x11_rectangle_t *area;
	XWindowAttributes a;

	if (!this->overlay_enabled) return 0;

	switch (data_type) {
	case GUI_DATA_EX_DEST_POS_SIZE_CHANGED:
		area = (x11_rectangle_t*) data;
		dxr3_overlay_adapt_area(this, area->x, area->y, area->w, area->h);
		break;
	case GUI_DATA_EX_EXPOSE_EVENT:
		XLockDisplay(this->display);
		XFillRectangle(this->display, this->win,
		 this->gc, 0, 0, this->width, this->height);
		XUnlockDisplay(this->display);
		break;
	case GUI_DATA_EX_DRAWABLE_CHANGED:
		this->win = (Drawable) data;
		this->gc = XCreateGC(this->display, this->win, 0, NULL);
		XGetWindowAttributes(this->display, this->win, &a);
		dxr3_set_property((vo_driver_t*) this,
		 VO_PROP_ASPECT_RATIO, this->aspectratio);
		break;
	case GUI_DATA_EX_TRANSLATE_GUI_TO_VIDEO:
		{
			int x1, y1, x2, y2;
			x11_rectangle_t *rect = data;

			dxr3_translate_gui2video(this, rect->x, rect->y,
						 &x1, &y1);
			dxr3_translate_gui2video(this, rect->w, rect->h,
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

static void dxr3_exit (vo_driver_t *this_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t *) this_gen;
#if USE_LIBFAME
  if(fc){
    fame_close(fc);
    fc = 0;
    free(buffer);
  }
#endif
#if USE_FFMPEG
  if (avc) {
    avcodec_close(avc);
    free(avc);
    free(buffer);
  }
#endif
  if (this->buf[0]) free(this->buf[0]);  
  if (this->buf[1]) free(this->buf[1]);  
  if (this->buf[2]) free(this->buf[2]);
  if(this->overlay_enabled)
    dxr3_overlay_set_mode(&this->overlay, EM8300_OVERLAY_MODE_OFF );
  close(this->fd_control);
  close(this->fd_video);
}

static void gather_screen_vars(dxr3_driver_t *this, x11_visual_t *vis)
{
	int scrn;
#ifdef HAVE_XINERAMA
	int screens;
	int dummy_a, dummy_b;
	XineramaScreenInfo *screeninfo = NULL;
#endif

	this->win = vis->d;
	this->display = vis->display;
	this->gc = XCreateGC(this->display, this->win, 0, NULL);
	scrn = DefaultScreen(this->display);

	/* Borrowed from xine-ui in order to detect fullscreen */
#ifdef HAVE_XINERAMA
	if (XineramaQueryExtension(this->display, &dummy_a, &dummy_b) &&
	 (screeninfo = XineramaQueryScreens(this->display, &screens)) &&
	 XineramaIsActive(this->display))
	{
		this->overlay.screen_xres = screeninfo[0].width;
		this->overlay.screen_yres = screeninfo[0].height;
	} else
#endif
	{
		this->overlay.screen_xres = DisplayWidth(this->display, scrn);
		this->overlay.screen_yres = DisplayHeight(this->display, scrn);
	}

	this->overlay.screen_depth = DisplayPlanes(this->display, scrn);
	this->request_dest_size = vis->request_dest_size;
	printf("xres %d yres %d depth %d\n", this->overlay.screen_xres, this->overlay.screen_yres, this->overlay.screen_depth);
}

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen)
{
	dxr3_driver_t *this;
	char tmpstr[100];
	char *file_out;

	/*
	* allocate plugin struct
	*/

	this = malloc (sizeof (dxr3_driver_t));

	if (!this) {
		printf ("video_out_dxr3: malloc failed\n");
		return NULL;
	}

	memset (this, 0, sizeof(dxr3_driver_t));

	this->vo_driver.get_capabilities     = dxr3_get_capabilities;
	this->vo_driver.alloc_frame          = dxr3_alloc_frame;
	this->vo_driver.update_frame_format  = dxr3_update_frame_format;
	this->vo_driver.display_frame        = dxr3_display_frame;
	this->vo_driver.overlay_blend        = dxr3_overlay_blend;
	this->vo_driver.get_property         = dxr3_get_property;
	this->vo_driver.set_property         = dxr3_set_property;
	this->vo_driver.get_property_min_max = dxr3_get_property_min_max;
	this->vo_driver.gui_data_exchange    = dxr3_gui_data_exchange;
	this->vo_driver.exit                 = dxr3_exit;
	this->config=config;
	
	/* open control device */
	devname = config->lookup_str (config, LOOKUP_DEV, DEFAULT_DEV);
	printf("dxr3enc: Entering video init, devname=%s.\n",devname);
	if ((this->fd_control = open(devname, O_WRONLY)) < 0) {
		printf("dxr3enc: Failed to open control device %s (%s)\n",
			devname, strerror(errno));
		return 0;
	}
        /* output mpeg to file instead of dxr3? */
        file_out = config->lookup_str(config, "dxr3enc_file", "<none>");
        if (file_out && strcmp(file_out, "<none>")) {
		this->fd_video = open(file_out, O_WRONLY | O_CREAT);
		if (this->fd_video < 0) {
			perror("dxr3enc: failed to open output file");
			return 0;
		}
	}
	else {        
	 	/* open video device */
		snprintf (tmpstr, sizeof(tmpstr), "%s_mv", devname);
		if ((this->fd_video = open (tmpstr, O_WRONLY | O_SYNC )) < 0) {
			printf("dxr3enc: failed to open video device %s (%s)\n",
				tmpstr, strerror(errno));
			return 0;
		}
#if USE_MPEG_BUFFER
	        /* we have to close now and open the first time we get 
		 * to display_frame. weird... */
	        close(this->fd_video);
        	this->fd_video = -1;
#endif
	}
	gather_screen_vars(this, visual_gen);
	
	/* default values */
	this->overlay_enabled = 0;
	this->aspectratio = ASPECT_FULL;

	dxr3_read_config(this, config);
	
	if (this->overlay_enabled) {
		dxr3_get_keycolor(this);
		dxr3_overlay_buggy_preinit(&this->overlay, this->fd_control);
	}

#if USE_LIBFAME
	/* fame context */
	fc = 0;
#endif
#if USE_FFMPEG
	avc = 0;
	avcodec_init();
	/* this register_all() is not really needed, but it gives us a good
         * way to sniff out bad libavcodecs */
	avcodec_register_all();
	avcodec = avcodec_find_encoder(CODEC_ID_MPEG1VIDEO);
	if (! avcodec) {
		/* tell tale sign of bad libavcodec */
        	printf("dxr3enc: can't find mpeg1 encoder! libavcodec is rotten!\n");
		return 0;
	}
#endif
	return &this->vo_driver;
}

static vo_info_t vo_info_dxr3 = {
  2, /* api version */
  "dxr3enc",
  "xine mpeg1 encoding video output plugin for dxr3 cards",
  VISUAL_TYPE_X11,
  10  /* priority */
};

vo_info_t *get_video_out_plugin_info()
{
	return &vo_info_dxr3;
}

