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
 * $Id: dxr3_video_out.c,v 1.5 2002/01/09 22:33:04 jcdutton Exp $
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
 ****** Update 16/12/2001 by Harm
 *
 * Merged dxr3 and dxr3enc video out drivers. Now there's only one!
 * dxr3_vo_standard.c and dxr3_vo_encoder.c are no more, everything
 * is in dxr3_video_out.c.
 *
 * renamed most config variables dxr3enc.XXX to dxr3.XXX
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dxr3_video_out.h"

#ifdef HAVE_LIBRTE
int dxr3_rte_init(dxr3_driver_t *);
#endif
#ifdef HAVE_LIBFAME
int dxr3_fame_init(dxr3_driver_t *);
#endif

#define MV_COMMAND 0

vo_info_t *get_video_out_plugin_info();

/* some helper stuff so that the decoder plugin can test for the
 * presence of the dxr3 vo driver */
/* to be called by dxr3 video out init and exit handlers */
static void dxr3_set_vo(dxr3_driver_t* this, int active)
{
	cfg_entry_t *entry;
	config_values_t *config = this->config;

	entry = config->lookup_entry(config, "dxr3.active");
	if (! entry) {
		/* register first */
		config->register_num(config, "dxr3.active", active, 
			"state of dxr3 video out", 
			"(internal variable; do not edit)",
			NULL, NULL);
	}
	else {
		entry->num_value = active;
	}
	printf("dxr3: %s dxr3 video out.\n", (active ? "enabled" : "disabled"));
}

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
  free(frame);
}

static void dxr3_frame_copy(vo_frame_t *frame_gen, uint8_t **src);

static vo_frame_t *dxr3_alloc_frame (vo_driver_t *this_gen)
{
  dxr3_frame_t   *frame;
  dxr3_driver_t *this = (dxr3_driver_t *)this_gen;
  
  frame = (dxr3_frame_t *) malloc (sizeof (dxr3_frame_t));
  memset (frame, 0, sizeof(dxr3_frame_t));

  if (this->enc && this->enc->on_frame_copy)
    frame->vo_frame.copy = dxr3_frame_copy;
  else
    frame->vo_frame.copy = 0;
  frame->vo_frame.field   = dummy_frame_field; 
  frame->vo_frame.dispose = dxr3_frame_dispose;

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
  int image_size, oheight; 

  /* reset the copy calls counter (number of calls to dxr3_frame_copy) */	
  frame->copy_calls = 0;
  frame->vo_instance = this;

  aspect = this->aspectratio;
  oheight = this->oheight;

  if (flags == DXR3_VO_UPDATE_FLAG) { /* talking to dxr3 decoder */
	this->mpeg_source = 1;
	/* a bit of a hack. we must release the em8300_mv fd for
	 * the dxr3 decoder plugin */
	if (this->fd_video >= 0) {
		close(this->fd_video);
		this->fd_video = -1;
	}
  }
  else {
	/* FIXME: Disable reset of mpeg_source 
	 * video_out.c can call us without the DXR3_VO_UPDATE_FLAG in
	 * the still frames code. Needs a better fix... */
	/* this->mpeg_source = 0; */
  }

  /* for mpeg source, we don't have to do much. */
  if (this->mpeg_source) {
	int aspect;
	this->video_width  = width;
	this->video_height = height;
	this->video_aspect = ratio_code;
	/* remember, there are no buffers malloc'ed for this frame!
 	 * the dxr3 decoder plugin is cool about this */
	frame->width  = width;
	frame->height = height;
	frame->oheight = oheight;
	frame->format = format;
	if (frame->mem) {
		free (frame->mem);
		frame->mem = NULL;
		frame->real_base[0] = frame->real_base[1] 
			= frame->real_base[2] = NULL;
		frame_gen->base[0] = frame_gen->base[1] 
			= frame_gen->base[2] = NULL;
	}
	if (ratio_code < 3 || ratio_code>4)
	  aspect = ASPECT_FULL;
	else
	  aspect = ASPECT_ANAMORPHIC;
	if(this->aspectratio!=aspect)
	  dxr3_set_property ((vo_driver_t*)this, VO_PROP_ASPECT_RATIO, aspect);
	return;
  }

  /* the following is for the mpeg encoding part only */

  if (this->add_bars == 0) {
	/* don't add black bars; assume source is in 4:3 */
	ratio_code = XINE_ASPECT_RATIO_4_3;
  }

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
      printf("dxr3: adding %d black lines to get %s a.r.\n", 
              oheight-height, aspect == ASPECT_FULL ? "4:3" : "16:9");
    this->video_width  = width;
    this->video_iheight = height;
    this->video_height = oheight;
    this->video_aspect = ratio_code;
    this->fps = 90000.0/frame->vo_frame.duration;
    this->format = format;

    if (! this->enc) {
      /* no encoder plugin! Let's bug the user! */
      printf(
	"dxr3: ********************************************************\n"
	"dxr3: *                                                      *\n"
	"dxr3: * need an mpeg encoder to play non-mpeg videos on dxr3 *\n"
	"dxr3: * read the README.dxr3 for details.                    *\n"
	"dxr3: * (if you get this message while trying to play an     *\n"
	"dxr3: * mpeg video, there is something wrong with the dxr3   *\n"
	"dxr3: * decoder plugin. check if it is set up correctly)     *\n"
	"dxr3: *                                                      *\n"
	"dxr3: ********************************************************\n"
	);
    }

    if (this->mpeg_source == 0 && this->enc && this->enc->on_update_format)
      this->enc->on_update_format(this);
  }


  /* if dimensions changed, we need to re-allocate frame memory */
  if ((frame->width != width) || (frame->height != height) || 
	(frame->oheight != oheight)) {
    if (frame->mem) {
      free (frame->mem);
      frame->mem = NULL;
    }
    /* make top black bar multiple of 16, 
     * so old and new macroblocks overlap */ 
    this->top_bar = ((oheight - height) / 32) * 16; 
    if (format == IMGFMT_YUY2) {
      image_size = width * oheight; /* includes black bars */
      /* planar format, only base[0] */
      /* add one extra line for field swap stuff */
      frame->real_base[0] = malloc_aligned(16, (image_size+width)*2, 
		(void**)&frame->mem);
      /* don't use first line */
      frame->real_base[0] += width * 2;
      frame->real_base[1] = frame->real_base[2] = 0;

      /* fix offset, so the decoder does not see the top black bar */
      frame->vo_frame.base[0] = frame->real_base[0] + width * 2 * this->top_bar; 
      frame->vo_frame.base[1] = frame->vo_frame.base[2] = 0;

      /* fill with black (yuy2 16,128,16,128,...) */
      memset(frame->real_base[0], 128, 2*image_size); /* U and V */
      for (i=0; i<2*image_size; i+=2)
	*(frame->real_base[0]+i) = 16; /* Y */
    }
    else { /* IMGFMT_YV12 */
      image_size = width * oheight; /* includes black bars */
      /* add one extra line for field swap stuff */
      frame->real_base[0] = malloc_aligned(16, (image_size + width) * 3/2, 
                                             (void**) &frame->mem);
      /* don't use first line */
      frame->real_base[0] += width;
      frame->real_base[1] = frame->real_base[0] + image_size; 
      frame->real_base[2] = frame->real_base[1] + image_size/4; 

      /* fill with black (yuv 16,128,128) */
      memset(frame->real_base[0], 16, image_size);
      memset(frame->real_base[1], 128, image_size/4);
      memset(frame->real_base[2], 128, image_size/4);

      /* fix offsets, so the decoder does not see the top black bar */
      frame->vo_frame.base[0] = frame->real_base[0] + width * this->top_bar;
      frame->vo_frame.base[1] = frame->real_base[1] + width * this->top_bar/4;
      frame->vo_frame.base[2] = frame->real_base[2] + width * this->top_bar/4;
    }
  }
     
  if (this->swap_fields != frame->swap_fields) {
	if (format == IMGFMT_YUY2) {
		if (this->swap_fields) 
			frame->vo_frame.base[0] -= width *2;
		else  
			frame->vo_frame.base[0] += width *2;
	}
	else {
		if (this->swap_fields) 
			frame->vo_frame.base[0] -= width;
		else  
			frame->vo_frame.base[0] += width;
	}
  }
 
  frame->width  = width;
  frame->height = height;
  frame->oheight = oheight;
  frame->format = format;
  frame->swap_fields = this->swap_fields;
  if(this->aspectratio!=aspect)
    dxr3_set_property (this_gen,VO_PROP_ASPECT_RATIO, aspect);

}

static void dxr3_frame_copy(vo_frame_t *frame_gen, uint8_t **src)
{
	dxr3_frame_t *frame = (dxr3_frame_t *) frame_gen;
	dxr3_driver_t *this = frame->vo_instance;
	if (this->mpeg_source == 0 && this->enc && this->enc->on_frame_copy)
		this->enc->on_frame_copy(this, frame, src);
}

static void dxr3_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  dxr3_driver_t *this = (dxr3_driver_t*)this_gen;
  dxr3_frame_t *frame = (dxr3_frame_t*)frame_gen;

  if (this->mpeg_source == 0 && this->enc && this->enc->on_display_frame)
	this->enc->on_display_frame(this, frame);
  if (this->mpeg_source)
	frame_gen->displayed(frame_gen);
  /* for non-mpeg, the encoder plugin is responsible for calling 
   * frame_gen->displayed(frame_gen) ! */
}

static void dxr3_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen,
 vo_overlay_t *overlay)
{
	dxr3_driver_t *this = (dxr3_driver_t*)this_gen;
	if ( this->mpeg_source == 0 )
	{
		/* we have regular YUV frames, so in principle we can blend
		 * it just like the Xv driver does. Problem is that the
		 * alphablend.c code file is not nearby */
	}
	else {
		/* we're using hardware mpeg decoding and have no YUV frames
		 * we need something else to blend... */
	}
}

void dxr3_exit (vo_driver_t *this_gen)
{
	dxr3_driver_t *this = (dxr3_driver_t *) this_gen;

	if (this->enc && this->enc->on_close)
		this->enc->on_close(this);
	printf("dxr3: vo exit called\n");
	this->mpeg_source = 0;

	if(this->overlay_enabled)
		dxr3_overlay_set_mode(&this->overlay, EM8300_OVERLAY_MODE_OFF);
	dxr3_set_vo(this, 0);
}

void dxr3_update_add_bars(void *data, cfg_entry_t* entry)
{
	dxr3_driver_t* this = (dxr3_driver_t*)data;
	this->add_bars = entry->num_value;
	printf("dxr3: add bars to correct a.r. is %s\n", 
		(this->add_bars ? "on" : "off"));
}

void dxr3_update_swap_fields(void *data, cfg_entry_t* entry)
{
	dxr3_driver_t* this = (dxr3_driver_t*)data;
	this->swap_fields = entry->num_value;
	printf("dxr3: set swap field to %s\n", 
		(this->swap_fields ? "on" : "off"));
}

static void dxr3_update_enhanced_mode(void *this_gen, cfg_entry_t *entry)
{
	((dxr3_driver_t*)this_gen)->enhanced_mode = entry->num_value;
	printf("dxr3: encode: set enhanced mode to %s\n", 
		(entry->num_value ? "on" : "off"));
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
printf("dxr3_video_out:init_plugin\n");

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
	this->vo_driver.get_info             = get_video_out_plugin_info;
	this->config=config;
	this->mpeg_source = 0; /* set by update_frame, by checking the flag */

	this->swap_fields = config->register_bool(config, "dxr3.enc_swap_fields", 0, "swap odd and even lines", NULL, dxr3_update_swap_fields, this);

	this->add_bars = config->register_bool(config, "dxr3.enc_add_bars", 1, "Add black bars to correct aspect ratio", "If disabled, will assume source has 4:3 a.r.", dxr3_update_add_bars, this);
	
	this->enhanced_mode = config->register_bool(config,"dxr3.enc_alt_play_mode", 1, "dxr3: use alternate play mode for mpeg encoder playback","Enabling this option will utilise a slightly different play mode",dxr3_update_enhanced_mode,this);
	/* open control device */
	this->devname = config->register_string (config, LOOKUP_DEV, DEFAULT_DEV,NULL,NULL,NULL,NULL);

	printf("dxr3: Entering video init, devname=%s.\n",this->devname);
	if ((this->fd_control = open(this->devname, O_WRONLY)) < 0) {
		printf("dxr3: Failed to open control device %s (%s)\n",
			this->devname, strerror(errno));
		return 0;
	}
        /* output mpeg to file instead of dxr3? */
        this->file_out = config->register_string(config, "dxr3.outputfile", "<none>", "dxr3: output file of encoded mpeg video for debugging",NULL,NULL,NULL);

        if (this->file_out && strcmp(this->file_out, "<none>")) {
		this->fd_video = open(this->file_out, O_WRONLY | O_CREAT);
		if (this->fd_video < 0) {
			perror("dxr3: failed to open output file");
			return 0;
		}
	}
	else {        
	 	/* open video device */
		snprintf (tmpstr, sizeof(tmpstr), "%s_mv", this->devname);
		if ((this->fd_video = open (tmpstr, O_WRONLY | O_SYNC )) < 0) {
			printf("dxr3: failed to open video device %s (%s)\n",
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
	printf("dxr3: %s\n", available_encoders);
	this->enc = 0;
	if (default_encoder) { 
		encoder = config->register_string(config, "dxr3.encoder", 
			default_encoder, available_encoders, NULL, NULL, NULL);
#ifdef HAVE_LIBRTE
		if (! strcmp(encoder, "rte"))
			if ( dxr3_rte_init(this) )
				return 0;
#endif
#ifdef HAVE_LIBFAME
		if (! strcmp(encoder, "fame"))
			if ( dxr3_fame_init(this) )
				return 0;
#endif
		if (this->enc == 0) {
			printf(
"dxr3: mpeg encoder \"%s\" not compiled in or not supported.\n"
"dxr3: valid options are %s\n", encoder, available_encoders);
			return 0;
		}
	}
	else {
		printf(
"dxr3: no mpeg encoder compiled in.\n"
"dxr3: that's ok, you don't need if for mpeg video like DVDs, but\n"
"dxr3: you will not be able to play non-mpeg content using this video out\n"
"dxr3: driver. See the README.dxr3 for details on configuring an encoder.\n" 
		);
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

	dxr3_set_vo(this, 1);
	return &this->vo_driver;
}

static vo_info_t vo_info_dxr3 = {
  3, /* api version */
  "dxr3",
  "xine video output plugin for dxr3 cards",
  VISUAL_TYPE_X11,
  10  /* priority */
};

vo_info_t *get_video_out_plugin_info()
{
	return &vo_info_dxr3;
}


