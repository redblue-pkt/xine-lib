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
 * $Id: dxr3_vo_encoder.c,v 1.15 2001/12/13 03:41:31 hrm Exp $
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
 *
 ***** Update 1/12/2001 by Harm
 * some support for mp1e encoder. Needs the raw-input patch for mp1e to
 * be functional. I'm sending that patch to the mp1e guys at zapping.sf.net,
 * it might be in the next version...
 *
 * (later) A/V sync should now be good; MP1E_DISPLAY_FRAME==1 works.
 * needs major code cleanup, but that's for later.
 *
 * looks like it'll work with mp1e rte API as well, provided it's
 * stable and all threads don't become a tangled mess.
 *
 ****** Update 2/12/2001 by Harm
 * 
 * Switched to librte instead of that weird mp1e-fifo concoction.
 * Not terribly impressed by the speed gain, if any, but certainly
 * cleaner. To use it, install librte-0.4 (get it at zapping.sf.net, the
 * API is still under development so don't expect newer versions to work
 * right away), define USE_MP1E 1 in the header below and change -lfame
 * to -lrte in Makefile.am
 *
 ****** Update 11/12/2001 by Harm
 *
 * Much much needed clean up.
 * Dropped ffmpeg support for the moment. Moved almost all libfame and 
 * librte stuff in separate sections, defined general encoder API
 * in encoder_data_t.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_LIBRTE
#  define _GNU_SOURCE
#  include <unistd.h>
#  include <rte.h>
#endif
#ifdef HAVE_LIBFAME
#  include <fame.h>
#endif

#include "dxr3_video_out.h"

/* buffer size for encoded mpeg1 stream; will hold one intra frame 
 * at 640x480 typical sizes are <50 kB. 512 kB should be plenty */
#define DEFAULT_BUFFER_SIZE 512*1024

#ifdef HAVE_LIBFAME
typedef struct {
	encoder_data_t encoder_data;
	fame_context_t *fc; /* needed for fame calls */
	fame_parameters_t fp;
	fame_yuv_t yuv;
	/* temporary buffer for mpeg data */
	char		*buffer;
	/* temporary buffer for YUY2->YV12 conversion */
        uint8_t 	*out[3]; /* aligned buffer for YV12 data */
        uint8_t 	*buf; /* unaligned YV12 buffer */
} fame_data_t;

int dxr3enc_fame_init(dxr3_driver_t *);
#endif

#ifdef HAVE_LIBRTE
typedef struct {
	encoder_data_t encoder_data;
	rte_context* context; /* handle for encoding */
	void* rte_ptr; /* buffer maintened by librte */
	double rte_time; /* frame time (s) */
	double rte_time_step; /* time per frame (s) */
	double rte_bitrate; /* mpeg out bitrate, default 2.3e6 bits/s */
} rte_data_t;

int dxr3enc_rte_init(dxr3_driver_t *);
#endif


#define MV_COMMAND 0

static uint32_t dxr3_get_capabilities (vo_driver_t *this_gen)
{
	return VO_CAP_YV12 | VO_CAP_YUY2 |
		VO_CAP_SATURATION | VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST;
}

static void dummy_frame_field (vo_frame_t *vo_img, int which_field)
{
	/* dummy function */
}

static void dxr3_frame_dispose (vo_frame_t *frame_gen)
{
  dxr3_frame_t  *frame = (dxr3_frame_t *) frame_gen; 
  if (frame->mem)
    free (frame->mem);
  if (frame->mpeg)
    free(frame->mpeg);
  free(frame);
}

static void dxr3_frame_copy(vo_frame_t *frame_gen, uint8_t **src);

static vo_frame_t *dxr3_alloc_frame (vo_driver_t *this_gen)
{
  dxr3_frame_t   *frame;

  frame = (dxr3_frame_t *) malloc (sizeof (dxr3_frame_t));
  memset (frame, 0, sizeof(dxr3_frame_t));

  frame->vo_frame.copy    = dxr3_frame_copy; 
  frame->vo_frame.field   = dummy_frame_field; 
  frame->vo_frame.dispose = dxr3_frame_dispose;

  frame->mpeg = (unsigned char *) malloc (DEFAULT_BUFFER_SIZE);

  return (vo_frame_t*) frame;
}

static void dxr3_update_frame_format (vo_driver_t *this_gen,
				      vo_frame_t *frame_gen,
				      uint32_t width, uint32_t height,
				      int ratio_code, int format, int flags)
{
  dxr3_driver_t  *this = (dxr3_driver_t *) this_gen; 
  int aspect,i;  
  dxr3_frame_t  *frame = (dxr3_frame_t *) frame_gen; 
  int image_size, oheight, top_bar;

  /* reset the copy calls counter (number of calls to dxr3_frame_copy) */	
  frame->copy_calls = 0;
  frame->vo_instance = this;

  aspect = this->aspectratio;
  oheight = this->oheight;

  frame->mpeg_size = 0;

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
    this->video_width  = width;
    this->video_iheight = height;
    this->video_height = oheight;
    this->video_aspect = ratio_code;
    this->fps = 90000.0/frame->vo_frame.duration;
    this->format = format;

    if (this->enc && this->enc->on_update_format)
      this->enc->on_update_format(this);
  }


  /* if dimensions changed, we need to re-allocate frame memory */
  if ((frame->width != width) || (frame->height != height)) {
    if (frame->mem) {
      free (frame->mem);
      frame->mem = NULL;
    }
    /* make top black bar multiple of 16, 
     * so old and new macroblocks overlap */ 
    top_bar = ((oheight - height) / 32) * 16; 

    if (format == IMGFMT_YUY2) {
      image_size = width * oheight; /* includes black bars */
      /* planar format, only base[0] */
      frame->real_base[0] = malloc_aligned(16, image_size*2, (void**)&frame->mem);
      frame->real_base[1] = frame->real_base[2] = 0;

      /* fix offset, so the decoder does not see the top black bar */
      frame->vo_frame.base[0] = frame->real_base[0] + width * 2 * top_bar; 
      frame->vo_frame.base[1] = frame->vo_frame.base[2] = 0;

      /* fill with black (yuy2 16,128,16,128,...) */
      memset(frame->real_base[0], 128, 2*image_size); /* U and V */
      for (i=0; i<2*image_size; i+=2)
	*(frame->real_base[0]+i) = 16; /* Y */
    }
    else { /* IMGFMT_YV12 */
      image_size = width * oheight; /* includes black bars */
      frame->real_base[0] = malloc_aligned(16, image_size * 3/2, 
                                             (void**) &frame->mem);
      frame->real_base[1] = frame->real_base[0] + image_size; 
      frame->real_base[2] = frame->real_base[1] + image_size/4; 

      /* fill with black (yuv 16,128,128) */
      memset(frame->real_base[0], 16, image_size);
      memset(frame->real_base[1], 128, image_size/4);
      memset(frame->real_base[2], 128, image_size/4);

      /* fix offsets, so the decoder does not see the top black bar */
      frame->vo_frame.base[0] = frame->real_base[0] + width * top_bar;
      frame->vo_frame.base[1] = frame->real_base[1] + width * top_bar/4;
      frame->vo_frame.base[2] = frame->real_base[2] + width * top_bar/4;
    }
  }
      
  frame->width  = width;
  frame->height = height;
  frame->oheight = oheight;
  frame->format = format;
  if(this->aspectratio!=aspect)
    dxr3_set_property (this_gen,VO_PROP_ASPECT_RATIO, aspect);

}

static void dxr3_frame_copy(vo_frame_t *frame_gen, uint8_t **src)
{
	dxr3_frame_t *frame = (dxr3_frame_t *) frame_gen;
	dxr3_driver_t *this = frame->vo_instance;
	if (this->enc && this->enc->on_frame_copy)
		this->enc->on_frame_copy(this, frame, src);
}

static void dxr3_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t*)this_gen;
  dxr3_frame_t *frame = (dxr3_frame_t*)frame_gen;

  if (this->enc)
	this->enc->on_display_frame(this, frame);

  /* plugin is responsible for calling vo_frame->displayed(frame) ! */
}

static void dxr3_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
 vo_overlay_t *overlay)
{
	/* dummy function */
}

void dxr3_exit (vo_driver_t *this_gen)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;

	if (this->enc && this->enc->on_close)
		this->enc->on_close(this);

	if(this->overlay_enabled)
		dxr3_overlay_set_mode(&this->overlay, EM8300_OVERLAY_MODE_OFF);
}

vo_driver_t *init_video_out_plugin (config_values_t *config, void *visual_gen)
{
	dxr3_driver_t *this;
	char tmpstr[100];
	const char *encoder; 
	char *available_encoders,*default_encoder;

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
	
	this->enhanced_mode = config->register_bool(config,"dxr3enc.alt_play_mode", 1, "Dxr3enc: use alternate Play mode","Enabling this option will utilise a slightly different play mode",NULL,NULL);
	/* open control device */
	this->devname = config->register_string (config, LOOKUP_DEV, DEFAULT_DEV,NULL,NULL,NULL,NULL);

	printf("dxr3enc: Entering video init, devname=%s.\n",this->devname);
	if ((this->fd_control = open(this->devname, O_WRONLY)) < 0) {
		printf("dxr3enc: Failed to open control device %s (%s)\n",
			this->devname, strerror(errno));
		return 0;
	}
        /* output mpeg to file instead of dxr3? */
        this->file_out = config->register_string(config, "dxr3enc.outputfile", "<none>", "Dxr3enc: output file for debugging",NULL,NULL,NULL);

        if (this->file_out && strcmp(this->file_out, "<none>")) {
		this->fd_video = open(this->file_out, O_WRONLY | O_CREAT);
		if (this->fd_video < 0) {
			perror("dxr3enc: failed to open output file");
			return 0;
		}
	}
	else {        
	 	/* open video device */
		snprintf (tmpstr, sizeof(tmpstr), "%s_mv", this->devname);
		if ((this->fd_video = open (tmpstr, O_WRONLY | O_SYNC )) < 0) {
			printf("dxr3enc: failed to open video device %s (%s)\n",
				tmpstr, strerror(errno));
			return 0;
		}
	        /* close now and and let the encoders reopen if they want */
	        close(this->fd_video);
        	this->fd_video = -1;
	}

	/* which encoder to use? Whadda we got? */
	default_encoder = 0;
	/* memory leak... but config doesn't copy our help string :-( */
	available_encoders = malloc(256);
	strcpy(available_encoders, "Mpeg1 encoder. Options: ");
#ifdef HAVE_LIBFAME
	default_encoder = "fame";
	strcat(available_encoders, "\"fame\" (very fast, good quality) ");
#endif
#ifdef HAVE_LIBRTE
	default_encoder = "rte"; 
	strcat(available_encoders, "\"rte\" (fast, high quality) ");
#endif
	printf("dxr3enc: %s\n", available_encoders);
	if (! default_encoder) { /* silly person; no encoder compiled in! */
		printf("dxr3enc: no mpeg1 encoder compiled in.\n");
		return 0;
	}
	encoder = config->register_string(config, "dxr3enc.encoder", 
		default_encoder, available_encoders, NULL, NULL, NULL);
	this->enc = 0;
#ifdef HAVE_LIBRTE
	if (! strcmp(encoder, "rte"))
		if ( dxr3enc_rte_init(this) )
			return 0;
#endif
#ifdef HAVE_LIBFAME
	if (! strcmp(encoder, "fame"))
		if ( dxr3enc_fame_init(this) )
			return 0;
#endif
	if (! this->enc) {
		printf("dxr3enc: encoder \"%s\" not supported.\n", encoder);
		return 0;
	}

	gather_screen_vars(this, visual_gen);
	
	/* default values */
	this->overlay_enabled = 0;
	this->aspectratio = ASPECT_FULL;

	dxr3_read_config(this);
	
	if (this->overlay_enabled) {
		dxr3_get_keycolor(this);
		dxr3_overlay_buggy_preinit(&this->overlay, this->fd_control);
	}

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

#ifdef HAVE_LIBRTE

static void mp1e_callback(rte_context *context, void *data, ssize_t size, 
	void* user_data)
{
  dxr3_driver_t *this = (dxr3_driver_t*)user_data;
  em8300_register_t regs; 
  char tmpstr[128];

  if (this->enhanced_mode)
  {
    regs.microcode_register=1; 	/* Yes, this is a MC Reg */
    regs.reg = MV_COMMAND;
    regs.val=6; /* Mike's mystery number :-) */
    ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &regs);
  }
  if (this->fd_video < 0) {
    snprintf (tmpstr, sizeof(tmpstr), "%s_mv", this->devname);
    this->fd_video = open(tmpstr, O_WRONLY);
  }
  if (write(this->fd_video, data, size) < 0)
    perror("dxr3enc: writing to video device");
}

static int rte_on_update_format(dxr3_driver_t *drv) 
{
	rte_data_t *this = (rte_data_t*)drv->enc;
	rte_context* context; 
	rte_codec *codec;
	int width, height;
	char tmpstr[128];

	width = drv->video_width;
	height = drv->video_height;

	if (this->context != 0) /* already done */
		return 0;

	this->context = rte_context_new (width, height, "mp1e", drv);
	if (! this->context) {
		printf("failed to get rte context.\n");
		return 1;
	}
	context = this->context; /* shortcut */
	rte_set_verbosity(context, 2);
	/* get mpeg codec handle */
	codec = rte_codec_set(context, RTE_STREAM_VIDEO, 0, "mpeg1_video");
	if (! codec) {
		printf("dxr3enc: could not create codec.\n");
		rte_context_destroy(context);
		context = 0;
		return 1;
	}
	this->rte_bitrate=drv->config->register_range(drv->config,"dxr3enc.rte_bitrate",10000, 1000,20000, "Dxr3enc: rte mpeg output bitrate (kbit/s)",NULL,NULL,NULL);
	this->rte_bitrate *= 1000; /* config in kbit/s, rte wants bit/s */
	/* FIXME: this needs to be replaced with a codec option call. 
	 * However, there seems to be none for the colour format!
	 * So we'll use the deprecated set_video_parameters instead. 
	 * Alternative is to manually set context->video_format (RTE_YU... )
	 * and context->video_bytes (= width * height * bytes/pixel) */
	rte_set_video_parameters(context, 
		(drv->format == IMGFMT_YV12 ? RTE_YUV420 : RTE_YUYV),
		context->width, context->height, 
		context->video_rate, context->output_video_bits, 
		context->gop_sequence);
	/* Now set a whole bunch of codec options */
	/* If I understand correctly, virtual_frame_rate is the frame rate
	 * of the source (can be anything), while coded_frame_rate must be
	 * one of the mpeg1 alloweds */
	if (!rte_option_set(codec, "virtual_frame_rate", drv->fps))
		printf("dxr3enc: WARNING: rte_option_set failed; virtual_frame_rate=%g.\n",drv->fps);
	if (!rte_option_set(codec, "coded_frame_rate", drv->fps))
		printf("dxr3enc: WARNING: rte_option_set failed; coded_frame_rate=%g.\n",drv->fps);
	if (!rte_option_set(codec, "bit_rate", (int)this->rte_bitrate))
		printf("dxr3enc: WARNING: rte_option_set failed; bit_rate = %d.\n", 
			(int)this->rte_bitrate);
	if (!rte_option_set(codec, "gop_sequence", "I"))
		printf("dxr3enc: WARNING: rte_option_set failed; gop_sequence = \"I\".\n");
	/* just to be sure, disable motion comp (not needed in I frames) */
	if (!rte_option_set(codec, "motion_compensation", 0))
		printf("dxr3enc: WARNING: rte_option_set failed; motion_compensation = 0.\n");
	rte_set_input(context, RTE_VIDEO, RTE_PUSH, FALSE, NULL, NULL, NULL);
	snprintf (tmpstr, sizeof(tmpstr), "%s_mv", drv->devname);
	rte_set_output(context, mp1e_callback, NULL, NULL);
	if (!rte_init_context(context)) {
		printf("dxr3enc: cannot init the context: %s\n",
			context->error);
		rte_context_delete(context);
		context = 0;
		return 1;
	}
	/* do the sync'ing and start encoding */
	if (!rte_start_encoding(context)) {
		printf("dxr3enc: cannot start encoding: %s\n",
			context->error);
		rte_context_delete(context);
		context = 0;
		return 1;
	}
	this->rte_ptr = rte_push_video_data(context, NULL, 0); 
	if (! this->rte_ptr) {
		printf("dxr3enc: failed to get encoder buffer pointer.\n");
		return 1;
	}
	this->rte_time = 0.0;
	this->rte_time_step = 1.0/drv->fps;
	
	return 0;
}

static int rte_on_display_frame( dxr3_driver_t* drv, dxr3_frame_t* frame )
{
	int size;
	rte_data_t* this = (rte_data_t*)drv->enc;
	size = frame->width * frame->oheight;
	if (frame->format == IMGFMT_YV12)
		xine_fast_memcpy(this->rte_ptr, frame->real_base[0], size*3/2);
	else
		xine_fast_memcpy(this->rte_ptr, frame->real_base[0], size*2);
	this->rte_time += this->rte_time_step;
	this->rte_ptr = rte_push_video_data(this->context, this->rte_ptr, 
					this->rte_time);
  	frame->vo_frame.displayed(&frame->vo_frame); 
	return 0;
}

static int rte_on_close( dxr3_driver_t *drv )
{
	rte_data_t *this = (rte_data_t*)drv->enc;
	if (this->context) {
		rte_stop(this->context);
		rte_context_delete(this->context);
		this->context = 0;
	}
	free(this);
	drv->enc = 0;
	return 0;
}

int dxr3enc_rte_init( dxr3_driver_t *drv )
{
	rte_data_t* this;
	if (! rte_init() ) {
		printf("dxr3enc: failed to init librte\n");
		return 1;
	} 
	this = malloc(sizeof(rte_data_t));
	if (!this)
		return 1;
	memset(this, 0, sizeof(rte_data_t));
	this->encoder_data.type = ENC_RTE;
	this->context = 0;
	this->encoder_data.on_update_format = rte_on_update_format;
	this->encoder_data.on_frame_copy = 0;
	this->encoder_data.on_display_frame = rte_on_display_frame;
	this->encoder_data.on_close = rte_on_close;
	drv->enc = (encoder_data_t*)this;
	return 0;
}

#endif

#ifdef HAVE_LIBFAME

static fame_parameters_t dummy_init_fp = FAME_PARAMETERS_INITIALIZER;

static int fame_on_update_format(dxr3_driver_t *drv) 
{
	fame_data_t *this = (fame_data_t*)drv->enc;
 	double fps;
 
	/* if YUY2 and dimensions changed, we need to re-allocate the
	 * internal YV12 buffer */
	if (this->buf) free(this->buf);  
	this->buf = 0;
	this->out[0] = this->out[1] = this->out[2] = 0;
	if (drv->format == IMGFMT_YUY2) {
		int image_size = drv->video_width * drv->video_height;

		this->out[0] = malloc_aligned(16, image_size * 3/2, 
			(void*)&this->buf);
		this->out[1] = this->out[0] + image_size; 
		this->out[2] = this->out[1] + image_size/4; 

		/* fill with black (yuv 16,128,128) */
		memset(this->out[0], 16, image_size);
		memset(this->out[1], 128, image_size/4);
		memset(this->out[2], 128, image_size/4);

		printf("dxr3enc: Using YUY2->YV12 conversion\n");  
	}

	if (!this->fc)
		this->fc = fame_open();
	if (!this->fc) {
	      	printf("Couldn't start the FAME library\n");
		return 1;
	}
	
	if (!this->buffer)
		this->buffer = (unsigned char *) malloc (DEFAULT_BUFFER_SIZE);
	if (!this->buffer) {
		printf("Couldn't allocate temp buffer for mpeg data\n");
		return 1;
	}

	this->fp = dummy_init_fp; 
	this->fp.quality=drv->config->register_range(drv->config,"dxr3enc.fame_quality",90, 10,100, "Dxr3enc: fame mpeg encoding quality",NULL,NULL,NULL);
	/* the really interesting bit is the quantizer scale. The formula
	 * below is copied from libfame's sources (could be changed in the
	 * future) */
	printf("dxr3enc: quality %d -> quant scale = %d\n", this->fp.quality,
        	1 + (30*(100-this->fp.quality)+50)/100);
	this->fp.width = drv->video_width;
	this->fp.height = drv->video_height;
	this->fp.profile = "mpeg1";
	this->fp.coding = "I";
	this->fp.verbose = 1;   /* we don't need any more info.. thanks :) */ 

	/* start guessing the framerate */
	fps = drv->fps;
	if (fabs(fps - 25) < 0.01) { /* PAL */
		printf("dxr3enc: setting mpeg output framerate to PAL (25 Hz)\n");
		this->fp.frame_rate_num = 25; this->fp.frame_rate_den = 1; 
	}  
	else if (fabs(fps - 24) < 0.01) { /* FILM */
      		printf("dxr3enc: setting mpeg output framerate to FILM (24 Hz))\n");
		this->fp.frame_rate_num = 24; this->fp.frame_rate_den = 1; 
	}
	else if (fabs(fps - 23.976) < 0.01) { /* NTSC-FILM */
		printf("dxr3enc: setting mpeg output framerate to NTSC-FILM (23.976 Hz))\n");
		this->fp.frame_rate_num = 24000; this->fp.frame_rate_den = 1001; 
	}
	else if (fabs(fps - 29.97) < 0.01) { /* NTSC */
		printf("dxr3enc: setting mpeg output framerate to NTSC (29.97 Hz)\n");
		this->fp.frame_rate_num = 30000; this->fp.frame_rate_den = 1001;
	}
	else { /* try 1/fps, if not legal, libfame will go to PAL */
		this->fp.frame_rate_num = (int)(fps + 0.5); this->fp.frame_rate_den = 1;
		printf("dxr3enc: trying to set mpeg output framerate to %d Hz\n",
			this->fp.frame_rate_num);
	}
	fame_init (this->fc, &this->fp, this->buffer, DEFAULT_BUFFER_SIZE);
	
	return 0;
}

static int fame_on_frame_copy(dxr3_driver_t *drv, dxr3_frame_t *frame, 
				uint8_t **src)
{
	int size, i, j, hoffset, w2;
	uint8_t *y, *u, *v, *yuy2;
	fame_data_t *this = (fame_data_t*)drv->enc;

	if (frame->vo_frame.bad_frame)
		return 0;

	if (frame->copy_calls == frame->height/16) {
		/* shouldn't happen */
		printf("dxr3enc: Internal error. Too many calls to dxr3_frame_copy (%d)\n", 
		frame->copy_calls);
		return 1; 
	}

	if (frame->vo_frame.format == IMGFMT_YUY2) {
		/* need YUY2->YV12 conversion */
		if (! (this->out[0] && this->out[1] && this->out[2]) ) {
			printf("dxr3enc: Internal error. Internal YV12 buffer not created.\n");
			return 1;
		}
		/* need conversion */
		hoffset = ((frame->oheight - frame->height)/32)*16; 
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
	else { /* YV12 */
		y = frame->real_base[0];
		u = frame->real_base[1];
		v = frame->real_base[2];
	}

	frame->copy_calls++;

	/* frame complete yet? */
	if (frame->copy_calls != frame->height/16)
		return 0;
	/* frame is complete: encode */
	this->yuv.y=y;
	this->yuv.u=u;
	this->yuv.v=v;
	size = fame_encode_frame(this->fc, &this->yuv, NULL);
    	if (size >= DEFAULT_BUFFER_SIZE) {
		printf("dxr3enc: warning, mpeg buffer too small!\n");
		size = DEFAULT_BUFFER_SIZE;
	}
	/* copy mpeg data to frame */
	xine_fast_memcpy(frame->mpeg, this->buffer, size);
	frame->mpeg_size = size;
	return 0;
}


static int fame_on_display_frame( dxr3_driver_t* drv, dxr3_frame_t* frame)
{
	char tmpstr[128];
	em8300_register_t regs; 
	if (drv->enhanced_mode)
	{
		regs.microcode_register=1; /* Yes, this is a MC Reg */
		regs.reg = MV_COMMAND;
		regs.val=6; /* Mike's mystery number :-) */
		ioctl(drv->fd_control, EM8300_IOCTL_WRITEREG, &regs);
	}

	if (drv->fd_video < 0) {
		snprintf (tmpstr, sizeof(tmpstr), "%s_mv", drv->devname);
		drv->fd_video = open(tmpstr, O_WRONLY);
	}
	if (write(drv->fd_video, frame->mpeg, frame->mpeg_size) < 0) 
		perror("dxr3enc: writing to video device");
  	frame->vo_frame.displayed(&frame->vo_frame); 
	return 0;
}

static int fame_on_close( dxr3_driver_t *drv )
{
	fame_data_t *this = (fame_data_t*)drv->enc;
	if (this->fc) {
		fame_close(this->fc);
	}
	free(this);
	drv->enc = 0;
	return 0;
}

int dxr3enc_fame_init( dxr3_driver_t *drv )
{
	fame_data_t* this;
	this = malloc(sizeof(fame_data_t));
	if (! this)
		return 1;
	memset(this, 0, sizeof(fame_data_t));
	this->encoder_data.type = ENC_FAME;
	/* fame context */
	this->fc = 0;
	this->encoder_data.on_update_format = fame_on_update_format;
	this->encoder_data.on_frame_copy = fame_on_frame_copy;
	this->encoder_data.on_display_frame = fame_on_display_frame;
	this->encoder_data.on_close = fame_on_close;
	drv->enc = (encoder_data_t*)this;
	return 0;
}

#endif

