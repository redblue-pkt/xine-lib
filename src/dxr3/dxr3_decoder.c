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
 * $Id: dxr3_decoder.c,v 1.40 2001/11/25 21:13:15 hrm Exp $
 *
 * dxr3 video and spu decoder plugin. Accepts the video and spu data
 * from XINE and sends it directly to the corresponding dxr3 devices.
 * Takes precedence over the libmpeg2 and libspudec due to a higher
 * priority.
 * also incorporates an scr plugin for metronom
 *
 * update 25/11/01 by Harm:
 * Major retooling; so much so that I've decided to cvs-tag the dxr3 sources
 * as DXR3_095 before commiting.
 * - major retooling of dxr3_decode_data; Mike Lampard's comments indicate
 * that dxr3_decode_data is called once every 12 frames or so. This seems
 * no longer true; we're in fact called on average more than once per frame.
 * This gives us some interesting possibilities to lead metronom up the garden
 * path (and administer it a healthy beating behind the toolshed ;-).
 * Read the comments for details, but the short version is that we take a
 * look at the scr clock to guestimate when we should call get_frame/draw/free.
 * - renamed update_aspect to parse_mpeg_header.
 * - replaced printing to stderr by stdout, following xine practice and
 * to make it easier to write messages to a log.
 * - replaced 6667 flag with proper define in dxr3_video_out.h
 *
 * The upshot of all this is that sync is a lot better now; I get good
 * a/v sync within a few seconds of playing after start/seek. I also
 * get a lot of "throwing away frame..." messages, especially just
 * after start/seek, but those are relatively harmless. (I guess we
 * call img->draw a tad too often).
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

#include <linux/soundcard.h>
#include <linux/em8300.h>
#include "video_out.h"
#include "xine_internal.h"
#include "buffer.h"
#include "xine-engine/bswap.h"

/* for DXR3_VO_UPDATE_FLAG */
#include "dxr3_video_out.h"

#define LOOKUP_DEV "dxr3.devicename"
#define DEFAULT_DEV "/dev/em8300"
static char *devname;

#define MV_COMMAND 0
#define MV_STATUS  1
#ifndef MVCOMMAND_SCAN
 #define MVCOMMAND_SCAN 4
#endif

typedef struct dxr3_decoder_s {
	video_decoder_t video_decoder;
	vo_instance_t *video_out;
	
	int fd_control;
	int fd_video;
	int last_pts;
	int last_scr;
	scr_plugin_t *scr;
	int scr_prio;
	int width;
	int height;
	int aspect;
	int duration;
	int enhanced_mode;
} dxr3_decoder_t;

static int dxr3_tested = 0;
static int dxr3_ok;

static void dxr3_presence_test()
{
	int fd, val;

	if (dxr3_tested)
		return;

	dxr3_tested = 1;
	dxr3_ok = 0;
	
	if ((fd = open(devname, O_WRONLY))<0) {
		printf("dxr3: not detected (%s: %s)\n",
			devname, strerror(errno));
		return;
	}
	if (ioctl(fd, EM8300_IOCTL_GET_AUDIOMODE, &val)<0) {
		printf("dxr3: ioctl failed (%s)\n", strerror(errno));
		return;
	}
	close(fd);
	dxr3_ok = 1;
}


/* *** dxr3_mvcommand ***
   Changes the dxr3 playmode.  Possible playmodes (currently) are
   0 	- Stop
   2 	- Pause
   3 	- Start playback
   4 	- Play intra frames only (for FFWD/FBackward)
   6 	- Alternate playmode - not much is known about this mode
     	  other than it buffers frames, possibly re-organising them
     	  on-the-fly to match SCR vs PTS values
   0x11 - Flush the onboard buffer???????
   0x10 - as above??? 
*/
static int dxr3_mvcommand(int fd_control, int command) {
       	em8300_register_t regs; 
       	regs.microcode_register=1; 	/* Yes, this is a MC Reg */
       	regs.reg = MV_COMMAND;
	regs.val=command;
	
	return (ioctl(fd_control, EM8300_IOCTL_WRITEREG, &regs));
}

typedef struct dxr3scr_s {
	scr_plugin_t scr;
	int fd_control;
	int priority;
} dxr3scr_t;

static int dxr3scr_get_priority (scr_plugin_t *scr) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	return self->priority;
}


/* *** dxr3scr_set_speed ***
   sets the speed and playmode of the dxr3.  if FFWD is requested
   the function changes the speed of the onboard clock, and sets
   the playmode to SCAN (mv_command 4).
*/
int scanning_mode=0;
static int dxr3scr_set_speed (scr_plugin_t *scr, int speed) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	uint32_t em_speed;
	int playmode;
	
	switch(speed){
	case SPEED_PAUSE:
		em_speed = 0;
		playmode=MVCOMMAND_PAUSE;
		break;
	case SPEED_SLOW_4:
		em_speed = 0x900/4;
		playmode=MVCOMMAND_START;
		break;
	case SPEED_SLOW_2:
		em_speed = 0x900/2;
		playmode=MVCOMMAND_START;		
		break;
	case SPEED_NORMAL:
		em_speed = 0x900;
		playmode=MVCOMMAND_START;
		break;
	case SPEED_FAST_2:
		em_speed = 0x900*2;
		playmode=MVCOMMAND_SCAN;
		break;
	case SPEED_FAST_4:
		em_speed = 0x900*4;
		playmode=MVCOMMAND_SCAN;
		break;
	default:
		em_speed = 0;
		playmode = MVCOMMAND_PAUSE;
	}
	if(em_speed>0x900)
		scanning_mode=1;
	else
		scanning_mode=0;

	if(dxr3_mvcommand(self->fd_control,playmode))
		printf("dxr3scr: failed to playmode (%s)\n", strerror(errno));
		
	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_SETSPEED, &em_speed))
		printf("dxr3scr: failed to set speed (%s)\n", strerror(errno));

	return speed;
}


/* *** dxr3scr_adjust ***
   Adjusts the SCR value of the card to match that given.
   This function is only called if the dxr3 SCR plugin is
   _NOT_ master...
*/
static void dxr3scr_adjust (scr_plugin_t *scr, uint32_t vpts) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	vpts >>= 1;

	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_SET, &vpts))
		printf("dxr3scr: adjust failed (%s)\n", strerror(errno));

}

/* *** dxr3scr_start ***
   sets the dxr3 onboard system reference clock to match that handed to
   it in start_vpts.  also sets the speed of the clock to 0x900 - which
   is normal speed.
*/
static void dxr3scr_start (scr_plugin_t *scr, uint32_t start_vpts) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	start_vpts >>= 1;

	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_SET, &start_vpts))
		printf("dxr3scr: start failed (%s)\n", strerror(errno));
	/* mis-use start_vpts */
	start_vpts = 0x900;
	ioctl(self->fd_control, EM8300_IOCTL_SCR_SETSPEED, &start_vpts);
}


/* *** dxr3_get_current ***
   returns the current SCR value as indicated by the hardware clock 
   on the dxr3 - apparently only called when the dxr3_scr plugin is
   master..
*/
static uint32_t dxr3scr_get_current (scr_plugin_t *scr) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	uint32_t pts;

	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_GET, &pts))
		printf("dxr3scr: get current failed (%s)\n", strerror(errno));

	return pts << 1;
}


/* *** dxr3scr_init ***
   initialise the SCR plugin
*/
static scr_plugin_t* dxr3scr_init (dxr3_decoder_t *dxr3) {
	dxr3scr_t *self;

	self = malloc(sizeof(*self));
	memset(self, 0, sizeof(*self));

	self->scr.interface_version = 2;
	self->scr.get_priority      = dxr3scr_get_priority;
	self->scr.set_speed         = dxr3scr_set_speed;
	self->scr.adjust            = dxr3scr_adjust;
	self->scr.start             = dxr3scr_start;
	self->scr.get_current       = dxr3scr_get_current;

	if ((self->fd_control = open (devname, O_WRONLY)) < 0) {
		printf("dxr3scr: Failed to open control device %s (%s)\n",
		 devname, strerror(errno));
		return NULL;
	}

	self->priority = dxr3->scr_prio;

	printf("dxr3scr: init complete\n");
	return &self->scr;
}

static int dxr3_can_handle (video_decoder_t *this_gen, int buf_type)
{
	buf_type &= 0xFFFF0000;
	return (buf_type == BUF_VIDEO_MPEG) || (buf_type == BUF_VIDEO_FILL);
}

static void dxr3_init (video_decoder_t *this_gen, vo_instance_t *video_out)
{
	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen;
	char tmpstr[100];

	printf("dxr3: Entering video init, devname=%s.\n",devname);
    
	/* open video device */
	snprintf (tmpstr, sizeof(tmpstr), "%s_mv", devname);
	if ((this->fd_video = open (tmpstr, O_WRONLY)) < 0) {
		printf("dxr3: Failed to open video device %s (%s)\n",
		 tmpstr, strerror(errno));
		return;
	}

	if ((this->fd_control = open (devname, O_WRONLY)) < 0) {
		printf("dxr3: Failed to open control device %s (%s)\n",
		 devname, strerror(errno));
		return;
	}

	video_out->open(video_out);
	this->video_out = video_out;

	this->last_pts = 0;
	
	this->scr = dxr3scr_init(this);
	this->video_decoder.metronom->register_scr(
	 this->video_decoder.metronom, this->scr);

	if (this->video_decoder.metronom->scr_master == this->scr) {
		printf("dxr3: dxr3scr plugin is master\n");
	}
	else {
		printf("dxr3: dxr3scr plugin is NOT master\n");
	}
	/* dxr3_init is called while the master scr already runs.
	   therefore the scr must be started here */
	this->scr->start(this->scr, 0);
}


/* *** parse_mpeg_header ***
   Does a partial parse of the mpeg buffer, extracting information such as
   frame width & height, aspect ratio, and framerate, and sends it to the 
   video_out plugin via get_frame 
*/
#define HEADER_OFFSET 4
static void parse_mpeg_header(dxr3_decoder_t *this, uint8_t * buffer)
{
	/* only carry on if we have a legitimate mpeg header... */
	if (buffer[1]==0 && buffer[0]==0 && buffer[2]==1 && buffer[3]==0xb3) {
		int old_h = this->height;
		int old_w = this->width;
		int old_a = this->aspect;

		/* framerate code... needed for metronom */
		int framecode = buffer[HEADER_OFFSET+3] & 15;
		
		/* grab video resolution and aspect ratio from the stream */
		this->height = (buffer[HEADER_OFFSET+0] << 16) |
		               (buffer[HEADER_OFFSET+1] <<  8) |
					    buffer[HEADER_OFFSET+2];
		this->width  = ((this->height >> 12) + 15) & ~15;
		this->height = ((this->height & 0xfff) + 15) & ~15;
		this->aspect = buffer[HEADER_OFFSET+3] >> 4;

		switch (framecode){
		case 1: /* 23.976 */
			this->duration=3913;
			break;
		case 2: /* 24.000 */
			this->duration=3750;
			break;
		case 3: /* 25.000 */
			this->duration=3600;
			break;
		case 4: /* 29.970 */
			this->duration=3003;
			break;
		case 5: /* 30.000 */
			this->duration=3000;
			break;
		case 6: /* 50.000 */
			this->duration=1800;
			break;
		case 7: /* 59.940 */
			this->duration=1525;
			break;
		case 8: /* 60.000 */
			this->duration=1509;
			break;
		default:
			/* only print this warning once */
			if (this->duration != 3600) {
				printf("dxr3: warning: unknown frame rate code %d: using PAL\n", framecode);
			}
			this->duration=3600;  /* PAL 25fps */
			break;
		}
				
		/* and ship the data if different ... appeasing any other vo plugins
		that are active ... */
		if (old_h!=this->height || old_w!=this->width || old_a!=this->aspect)
		{
			vo_frame_t *img;
			/* call with flags=DXR3_VO_UPDATE_FLAG, so that the 
			   dxr3 vo driver will update sizes and aspect ratio */
			img = this->video_out->get_frame(this->video_out,
				 this->width,this->height,this->aspect,
				 IMGFMT_YV12, this->duration, 
				 DXR3_VO_UPDATE_FLAG); 
			img->free(img);				 
		}
	}
}


/* *** dxr3_flush ***
   flush the dxr3's onboard buffers - but I'm not sure that this is 
   doing that - more testing is required.
*/
static void dxr3_flush (video_decoder_t *this_gen) 
{
	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen;
	printf("dxr3_decoder: flushing\n");
	dxr3_mvcommand(this->fd_control, 0x11); 
}


static void dxr3_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen;
	ssize_t written;
	int vpts;
        vo_frame_t *img;
	
	/* The dxr3 does not need the preview-data */
	if (buf->decoder_info[0] == 0) {
		return;
	}

	/* examine mpeg header, if this buffer's contents has one, and
	   send an update message to the dxr3 vo driver if needed */
	parse_mpeg_header(this, buf->content);	

	/* not sure if this is supposed to ever happen, but 
	   checking is cheap enough... */	
	if (buf->SCR < this->last_scr) { /* wrapped ? */
		this->last_scr = buf->SCR; 
	}

	/* Now try to make metronom happy by calling get_frame,draw,free.
	   There are two conditions when we need to do this:
	   (1) Normal mpeg decoding; we want to make the calls for each
	   frame, but the problem is that dxr3_decode_data is called more 
	   frequently than once per frame (not sure why). We'd have to analyse
	   the mpeg data to find out whether or not a new frame should be
	   announced. That defeats the point of hardware mpeg decoding
	   somewhat, so we'll just look at the clock value; if the time
	   elapsed since the previous image get_frame/draw/free trio is
	   more than the frame's duration, we draw. 
	   (2) Still pictures; A buffer type of BUF_VIDEO_FILL is used 
	   when still frames are required (after an initial frame is sent 
	   for display, BUF_VIDEO_FILL grabs and re-displays the last frame) 
	   the dxr3 doesn't require this functionality (just do nothing and
	   the last frame will stick), but for interoperability purposes
	   this plugin must implement it in order to override xine's 
	   builtin version - so we just pretend to be outputting the same
	   old frame at the correct frame rate.  
	*/
	vpts = 0;
	if ( buf->SCR >= this->last_scr + this->duration || /* time to draw */
	     buf->type==BUF_VIDEO_FILL) /* static picture; always draw */
	{ 
    		img = this->video_out->get_frame (this->video_out,
                             this->width,
                             this->height,
                             this->aspect,
                             IMGFMT_YV12,
                             this->duration,
                             VO_BOTH_FIELDS);
		/* copy PTS and SCR from buffer to img, img->draw uses them */
		img->PTS=buf->PTS;
		img->SCR=buf->SCR;
		/* draw calls metronom->got_video_frame with img pts and scr
		   and stores the return value back in img->PTS
		   Calling draw with buf->PTS==0 is okay; metronome will
		   extrapolate a value. */
	        img->draw(img);
		vpts = img->PTS; /* copy so we can free img */
		/* store at what time we called draw last */
		this->last_scr = img->SCR;
		img->free(img);
	}

	/* for stills we're done now */
	if(buf->type == BUF_VIDEO_FILL) {
		return;
	}

	/* Every once in a while a buffer has a PTS value associated with it.  
	   From my testing, once around every 12-13 frames, the 
	   buf->PTS is non-zero, which is around every .5 seconds or so...
	   If vpts is non-zero, we already called img->draw which in 
	   turn has called got_video_frame, so we have vpts already */
	if (buf->PTS && vpts == 0) {
		/* receive an updated pts value from metronom... */
		vpts = this->video_decoder.metronom->got_video_frame(
		 this->video_decoder.metronom, buf->PTS, buf->SCR );
	}
	/* Now that we have a PTS value from the stream, not a metronom
	   interpolated one, it's a good time to make sure the dxr3 pts
           is in sync. Checking every 0.5 seconds should be enough... */
	if (buf->PTS && this->last_pts < vpts)
	{
		this->last_pts = vpts;
		/* update the dxr3's current pts value */	
		if (ioctl(this->fd_video, EM8300_IOCTL_VIDEO_SETPTS, &vpts)) {
				printf("dxr3: set video pts failed (%s)\n",
				 strerror(errno));
		}
	}

	/* if the dxr3_alt_play option is used, change the dxr3 playmode */
	if(this->enhanced_mode && !scanning_mode)
		dxr3_mvcommand(this->fd_control, 6);
	
	/* now write the content to the dxr3 mpeg device and, in a dramatic
	   break with open source tradition, check the return value */
	written = write(this->fd_video, buf->content, buf->size);
	if (written < 0) {
		printf("dxr3: video device write failed (%s)\n",
		 strerror(errno));
		return;
	}
	if (written != buf->size)
		printf("dxr3: Could only write %d of %d video bytes.\n",
		 written, buf->size);

}

static void dxr3_close (video_decoder_t *this_gen)
{
	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen;

	this->video_decoder.metronom->unregister_scr(
	 this->video_decoder.metronom, this->scr);

	close(this->fd_video);
	this->fd_video = 0;

	this->video_out->close(this->video_out);
}

static char *dxr3_get_id(void) {
  return "dxr3-mpeg2";
}

video_decoder_t *init_video_decoder_plugin (int iface_version,
 config_values_t *cfg)
{
	dxr3_decoder_t *this ;

	if (iface_version != 3) {
		printf( "dxr3: plugin doesn't support plugin API version %d.\n"
		 "dxr3: this means there's a version mismatch between xine and this\n"
		 "dxr3: decoder plugin. Installing current plugins should help.\n",
		 iface_version);
		return NULL;
	}

	devname = cfg->register_string (cfg, LOOKUP_DEV, DEFAULT_DEV, "Dxr3: Device Name",NULL,NULL,NULL);

	dxr3_presence_test ();
	if (!dxr3_ok) return NULL;

	this = (dxr3_decoder_t *) malloc (sizeof (dxr3_decoder_t));

	this->video_decoder.interface_version   = 3;
	this->video_decoder.can_handle          = dxr3_can_handle;
	this->video_decoder.init                = dxr3_init;
	this->video_decoder.decode_data         = dxr3_decode_data;
	this->video_decoder.close               = dxr3_close;
	this->video_decoder.get_identifier      = dxr3_get_id;
	this->video_decoder.flush		= dxr3_flush;
	this->video_decoder.priority            = 10;

	this->scr_prio = cfg->register_num(cfg, "dxr3.scr_priority", 10, "Dxr3: SCR plugin priority",NULL,NULL,NULL); 
        
	this->enhanced_mode = cfg->register_bool(cfg,"dxr3.alt_play_mode", 0, "Dxr3: use alternate Play mode","Enabling this option will utilise a slightly different play mode",NULL,NULL);

	if(this->enhanced_mode)
	  printf("Dxr3: Using Mode 6 for playback\n");
	return (video_decoder_t *) this;
}

/*
 * Second part of the dxr3 plugin: subpicture decoder
 */
#define MAX_SPU_STREAMS 32

typedef struct spudec_stream_state_s {
  	uint32_t         stream_filter;
} spudec_stream_state_t;
  
typedef struct spudec_decoder_s {
	spu_decoder_t    	spu_decoder;
 	spudec_stream_state_t   spu_stream_state[MAX_SPU_STREAMS];

	vo_instance_t   	*vo_out;
	int 			fd_spu;
	int			menu; /* are we in a menu? */
	xine_t			*xine;
} spudec_decoder_t;

static int spudec_can_handle (spu_decoder_t *this_gen, int buf_type)
{
	int type = buf_type & 0xFFFF0000;
	return (type == BUF_SPU_PACKAGE || type == BUF_SPU_CLUT ||
		type == BUF_SPU_SUBP_CONTROL);
}

static void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out)
{
	spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
	char tmpstr[100];
	int i;
	
	this->vo_out = vo_out;

	/* open spu device */
	snprintf (tmpstr, sizeof(tmpstr), "%s_sp", devname);
	if ((this->fd_spu = open (tmpstr, O_WRONLY)) < 0) {
		printf("dxr3: Failed to open spu device %s (%s)\n",
		 tmpstr, strerror(errno));
		return;
	}

        for (i=0; i < MAX_SPU_STREAMS; i++) /* reset the spu filter for non-dvdnav */
                 this->spu_stream_state[i].stream_filter = 1;
}

static void swab_clut(int* clut)
{
	int i;
	for (i=0; i<16; i++)
		clut[i] = bswap_32(clut[i]);
}

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf, uint32_t scr)
{
	spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
	ssize_t written;
	uint32_t stream_id = buf->type & 0x1f ;
	
	if (buf->type == BUF_SPU_CLUT) {
		if (buf->content[0] == 0)  /* cheap endianess detection */
			swab_clut((int*)buf->content);
		if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPALETTE, buf->content))
			printf("dxr3: failed to set CLUT (%s)\n", strerror(errno));
		return;
	}

        if(buf->type == BUF_SPU_SUBP_CONTROL){
		int i;
                uint32_t *subp_control = (uint32_t*) buf->content;
                for (i = 0; i < 32; i++) {
	        	this->spu_stream_state[i].stream_filter = subp_control[i];
                 }
     		return;
	}

	/* Is this also needed for subpicture? */
	if (buf->decoder_info[0] == 0) return;

	if ( this->spu_stream_state[stream_id].stream_filter == 0) return;

	if (buf->PTS) {
		int vpts;
		vpts = this->spu_decoder.metronom->got_spu_packet
		 (this->spu_decoder.metronom, buf->PTS, 0, buf->SCR);

		if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPTS, &vpts))
			printf("dxr3: spu setpts failed (%s)\n", strerror(errno));
	}

        if (this->xine->spu_channel != stream_id && this->menu!=1 ) return; 

	written = write(this->fd_spu, buf->content, buf->size);
	if (written < 0) {
		printf("dxr3: spu device write failed (%s)\n",
		 strerror(errno));
		return;
	}
	if (written != buf->size)
		printf("dxr3: Could only write %d of %d spu bytes.\n",
		 written, buf->size);
}

static void spudec_close (spu_decoder_t *this_gen)
{
	spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
	
	close(this->fd_spu);
}

static void spudec_event_listener (void *this_gen, xine_event_t *event_gen) {

  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;
  xine_spu_event_t *event = (xine_spu_event_t *) event_gen;
  
  switch (event->event.type) {
  case XINE_EVENT_SPU_BUTTON:
    {

      spu_button_t *but = event->data;
      em8300_button_t btn;
      int i;
      
      if (!but->show) {
	ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, NULL);
	break;
      }
      btn.color = btn.contrast = 0;
      
      for (i = 0; i < 4; i++) {
	btn.color    |= (but->color[i] & 0xf) << (4*i);
	btn.contrast |= (but->trans[i] & 0xf) << (4*i);
      }
      
      btn.left   = but->left;
      btn.right  = but->right;
      btn.top    = but->top;
      btn.bottom = but->bottom;
      
      if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, &btn))
	printf("dxr3: failed to set spu button (%s)\n",
		strerror(errno));
    }
    break;

  case XINE_EVENT_SPU_CLUT:
    {
      spu_cltbl_t *clut = event->data;
#ifdef WORDS_BIGENDIAN
      swab_clut(clut->clut);
#endif
      
      if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPALETTE, clut->clut))
	printf("dxr3: failed to set CLUT (%s)\n",
		strerror(errno));
    }
    break;
	/* Temporarily use the stream title to find out if we have a menu... 
	   obsoleted by XINE_EVENT_SPU_FORCEDISPLAY, but we'll keep it 'til
	   the next version of dvdnav */
  case XINE_EVENT_UI_SET_TITLE:
    {
      if(strstr(event->data,"Menu"))
	this->menu=1;
      else
	this->menu=0;
    }
    break;
  case XINE_EVENT_SPU_FORCEDISPLAY:
    {
    	(int*)this->menu=event->data;
    }
    break;    	 
  }  
}

static char *spudec_get_id(void)
{
	return "dxr3-spudec";
}

spu_decoder_t *init_spu_decoder_plugin (int iface_version, xine_t *xine)
{
  spudec_decoder_t *this;
  config_values_t  *cfg;

  if (iface_version != 4) {
    printf( "dxr3: plugin doesn't support plugin API version %d.\n"
	    "dxr3: this means there's a version mismatch between xine and this "
	    "dxr3: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  cfg = xine->config;
  devname = cfg->register_string (cfg, LOOKUP_DEV, DEFAULT_DEV, NULL,NULL,NULL,NULL);

  dxr3_presence_test ();
  if (!dxr3_ok) return NULL;

  this = (spudec_decoder_t *) malloc (sizeof (spudec_decoder_t));

  this->spu_decoder.interface_version   = 4;
  this->spu_decoder.can_handle          = spudec_can_handle;
  this->spu_decoder.init                = spudec_init;
  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.close               = spudec_close;
  this->spu_decoder.get_identifier      = spudec_get_id;
  this->spu_decoder.priority            = 10;
  this->xine				= xine;
  this->menu				= 0;
  
  xine_register_event_listener(xine, spudec_event_listener, this);
  
  return (spu_decoder_t *) this;
}

