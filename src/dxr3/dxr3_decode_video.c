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
 * $Id: dxr3_decode_video.c,v 1.1 2002/05/02 14:33:30 jcdutton Exp $
 *
 * dxr3 video and spu decoder plugin. Accepts the video and spu data
 * from XINE and sends it directly to the corresponding dxr3 devices.
 * Takes precedence over the libmpeg2 and libspudec due to a higher
 * priority.
 * also incorporates an scr plugin for metronom
 *
 * update 7/1/2002 by jcdutton:
 *   Updated to work better with the changes done to dvdnav.
 *   Subtitles display properly now.
 *   TODO: Process NAV packets so that the first
 *         menu button appears, and also so that
 *         menu buttons disappear when one starts playing the movie.
 *         Processing NAV packets will also make "The White Rabbit"
 *         work on DXR3 as I currently works on XV.
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
 *
 * update 21/12/01 by Harm
 * many revisions, but I've been too lazy to document them here.
 * read the cvs log, that's what it's for anyways.
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
#include <signal.h>

#include <linux/soundcard.h>
#include <linux/em8300.h>
#include "video_out.h"
#include "xine_internal.h"
#include "buffer.h"
#include "xine-engine/bswap.h"
#include "nav_types.h"
#include "nav_read.h"

/* for DXR3_VO_UPDATE_FLAG */
#include "dxr3_video_out.h"

#define LOOKUP_DEV "dxr3.devicename"
#define DEFAULT_DEV "/dev/em8300"
static char devname[128];
static char devnum[3];

/* lots of poohaa about pts things */
#define LOG_PTS 0
#define LOG_SPU 0 

#define MV_COMMAND 0

/* the number of frames to pass after an out-of-sync situation
   before locking the stream again */
#define RESYNC_WINDOW_SIZE 50


typedef struct dxr3_decoder_s {
	video_decoder_t video_decoder;
	vo_instance_t *video_out;
	config_values_t *config;
	metronom_t *metronom;
	
	int fd_control;
	int fd_video;
	int64_t last_pts;
	scr_plugin_t *scr;
	int scr_prio;
	int width;
	int height;
	int aspect;
	int frame_rate_code;
	int repeat_first_field;
	/* try to sync pts every frame. will be disabled if non-progessive
	   video is detected via repeat first field */
	int sync_every_frame;
	/* if disabled by repeat first field, retry after 500 frames */
	int sync_retry; 
	int enhanced_mode;
	int resync_window;
	int have_header_info;
	int in_buffer_fill;
	pthread_t decoder_thread; /* reference to self */
} dxr3_decoder_t;

static int dxr3_presence_test( xine_t* xine)
{
        int info;
        if (xine && xine->video_driver ) {
          info = xine->video_driver->get_property( xine->video_driver, VO_PROP_VO_TYPE);
          printf("dxr3_presence_test:info=%d\n",info);
          if (info != VO_TYPE_DXR3) {
            return 0;
          }
        }
        return 1;
}


/* *** dxr3_mvcommand ***
   Changes the dxr3 playmode.  Possible playmodes (currently) are
   0 	- Stop
   2 	- Pause
   3 	- Start playback
   4 	- Play intra frames only (for FFWD/FBackward)
   6 	- Alternate playmode - not much is known about this mode
     	  other than it buffers frames, possibly re-organising them
     	  on-the-fly to match scr vs pts values
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
	int64_t offset;     /* difference between real scr and internal dxr3 clock */
	uint32_t last_pts;  /* last known value of internal dxr3 clock to detect wrap around */
	pthread_mutex_t mutex;
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
		playmode=MVCOMMAND_SYNC;
		break;
	case SPEED_FAST_2:
		em_speed = 0x900*2;
		playmode=MVCOMMAND_START;
		break;
	case SPEED_FAST_4:
		em_speed = 0x900*4;
		playmode=MVCOMMAND_START;
		break;
	default:
		em_speed = 0;
		playmode = MVCOMMAND_PAUSE;
	}
	
	if (dxr3_mvcommand(self->fd_control, playmode))
		printf("dxr3scr: failed to playmode (%s)\n", strerror(errno));
		
	if(em_speed>0x900)
		scanning_mode=1;
	else
		scanning_mode=0;

	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_SETSPEED, &em_speed))
		printf("dxr3scr: failed to set speed (%s)\n", strerror(errno));

	return speed;
}


/* *** dxr3scr_adjust ***
   Adjusts the scr value of the card to match that given.
   This function is only called if the dxr3 scr plugin is
   _NOT_ master...
   Harm: wish that were so. It's called by audio_out
   (those adjusting master clock x->y messages)
*/
static void dxr3scr_adjust (scr_plugin_t *scr, int64_t vpts) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	uint32_t cpts32;
	int32_t offset32;
	
	pthread_mutex_lock(&self->mutex);
	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_GET, &cpts32))
		printf("dxr3scr: adjust get failed (%s)\n", strerror(errno));
	self->last_pts = cpts32;
	self->offset = vpts - ((int64_t)cpts32 << 1);
	offset32 = self->offset / 4;
	/* kernel driver ignores diffs < 7200, so abs(offset32) must be > 7200 / 4 */
	if (offset32 < -7200/4 || offset32 > 7200/4) {
		uint32_t vpts32 = vpts >> 1;
		if (ioctl(self->fd_control, EM8300_IOCTL_SCR_SET, &vpts32))
			printf("dxr3scr: adjust set failed (%s)\n", strerror(errno));
		self->last_pts = vpts32;
		self->offset = vpts - ((int64_t)vpts32 << 1);
	}
	pthread_mutex_unlock(&self->mutex);
}

/* *** dxr3scr_start ***
   sets the dxr3 onboard system reference clock to match that handed to
   it in start_vpts.  also sets the speed of the clock to 0x900 - which
   is normal speed.
*/
static void dxr3scr_start (scr_plugin_t *scr, int64_t start_vpts) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	uint32_t vpts32 = start_vpts >> 1;

	pthread_mutex_lock(&self->mutex);
	self->last_pts = vpts32;
	self->offset = start_vpts - ((int64_t)vpts32 << 1);
	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_SET, &vpts32))
		printf("dxr3scr: start failed (%s)\n", strerror(errno));
	/* mis-use vpts32 */
	vpts32 = 0x900;
	ioctl(self->fd_control, EM8300_IOCTL_SCR_SETSPEED, &vpts32);
	pthread_mutex_unlock(&self->mutex);
}


/* *** dxr3_get_current ***
   returns the current SCR value as indicated by the hardware clock 
   on the dxr3 - apparently only called when the dxr3_scr plugin is
   master..
*/
static int64_t dxr3scr_get_current (scr_plugin_t *scr) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	uint32_t pts;
	int64_t current;

	pthread_mutex_lock(&self->mutex);
        if (ioctl(self->fd_control, EM8300_IOCTL_SCR_GET, &pts))
		printf("dxr3scr: get current failed (%s)\n", strerror(errno));
	/* FIXME: The pts value read from the card sometimes drops to zero for 
	   unknown reasons. We catch this here, but we should better find out, why. */
	if (pts == 0) pts = self->last_pts;
	if (pts < self->last_pts) /* wrap around detected, compensate with offset */
		self->offset += (int64_t)1 << 33;
	self->last_pts = pts;
	current = ((int64_t)pts << 1) + self->offset;
        pthread_mutex_unlock(&self->mutex);

	return current;
}
 /* *** dxr3scr_exit ***
    clean up for exit
 */
static void dxr3scr_exit (scr_plugin_t *scr) {
       dxr3scr_t *self = (dxr3scr_t*) scr;

       pthread_mutex_destroy(&self->mutex);
       free (self);
}


/* *** dxr3scr_init ***
   initialise the SCR plugin
*/
static scr_plugin_t* dxr3scr_init (dxr3_decoder_t *dxr3) {
	dxr3scr_t *self;
	char tmpstr[128];

	self = malloc(sizeof(*self));
	memset(self, 0, sizeof(*self));

	self->scr.interface_version = 2;
	self->scr.get_priority      = dxr3scr_get_priority;
	self->scr.set_speed         = dxr3scr_set_speed;
	self->scr.adjust            = dxr3scr_adjust;
	self->scr.start             = dxr3scr_start;
	self->scr.get_current       = dxr3scr_get_current;
	self->scr.exit              = dxr3scr_exit;

	self->offset = 0;
	self->last_pts = 0;
	pthread_mutex_init(&self->mutex, NULL);

	snprintf (tmpstr, sizeof(tmpstr), "%s%s", devname, devnum);
	if ((self->fd_control = open (tmpstr, O_WRONLY)) < 0) {
		printf("dxr3scr: Failed to open control device %s (%s)\n",
		 tmpstr, strerror(errno));
		return NULL;
	}

	self->priority = dxr3->scr_prio;

	printf("dxr3scr_init: init complete\n");
	return &self->scr;
}

static int dxr3_can_handle (video_decoder_t *this_gen, int buf_type)
{
	buf_type &= 0xFFFF0000;
	return (buf_type == BUF_VIDEO_MPEG);
}

static void dxr3_init (video_decoder_t *this_gen, vo_instance_t *video_out)
{
	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen;
	metronom_t *metronom = this->metronom;
	char tmpstr[128];
	int64_t cur_offset;

	snprintf (tmpstr, sizeof(tmpstr), "%s%s", devname, devnum);
	printf("dxr3: Entering video init, devname=%s.\n",tmpstr);
   
	this->fd_video = -1; /* open later */

	if ((this->fd_control = open (tmpstr, O_WRONLY)) < 0) {
		printf("dxr3: Failed to open control device %s (%s)\n",
		 tmpstr, strerror(errno));
		return;
	}

	video_out->open(video_out);
	this->video_out = video_out;

	this->last_pts = metronom->get_current_time(metronom);
	this->decoder_thread = pthread_self();
	
	this->resync_window = 0;
	
	cur_offset = metronom->get_option(metronom, METRONOM_AV_OFFSET);
	metronom->set_option(metronom, METRONOM_AV_OFFSET, cur_offset - 21600);
	/* a note on the 21600: this seems to be a necessary offset,
	   maybe the dxr3 needs some internal time, however it improves sync a lot */
}


/* *** parse_mpeg_header ***
   Does a partial parse of the mpeg buffer, extracting information such as
   frame width & height, aspect ratio, and framerate, and sends it to the 
   video_out plugin via get_frame 
*/
#define HEADER_OFFSET 0
static void parse_mpeg_header(dxr3_decoder_t *this, uint8_t * buffer)
{
	/* framerate code... needed for metronom */
	this->frame_rate_code = buffer[HEADER_OFFSET+3] & 15;
	
	/* grab video resolution and aspect ratio from the stream */
	this->height = (buffer[HEADER_OFFSET+0] << 16) |
	               (buffer[HEADER_OFFSET+1] <<  8) |
			    buffer[HEADER_OFFSET+2];
	this->width  = ((this->height >> 12) + 15) & ~15;
	this->height = ((this->height & 0xfff) + 15) & ~15;
	this->aspect = buffer[HEADER_OFFSET+3] >> 4;
	
	this->have_header_info = 1;
}

static int get_duration(int framecode, int repeat_first_field)
{
	int duration;
	switch (framecode){
	case 1: /* 23.976 */
		duration=3913;
		break;
	case 2: /* 24.000 */
		duration=3750;
		break;
	case 3: /* 25.000 */
		duration=repeat_first_field ? 5400 : 3600;
		/*duration=3600;*/
		break;
	case 4: /* 29.970 */
		duration=repeat_first_field ? 4505 : 3003;
		/*duration=3003;*/
		break;
	case 5: /* 30.000 */
		duration=3000;
		break;
	case 6: /* 50.000 */
		duration=1800;
		break;
	case 7: /* 59.940 */
		duration=1525;
		break;
	case 8: /* 60.000 */
		duration=1509;
		break;
	default:
		printf("dxr3: warning: unknown frame rate code %d: using PAL\n", framecode);
		duration=3600;  /* PAL 25fps */
		break;
	}
	return duration;
}

static void dxr3_reset(video_decoder_t *this_gen)
{
/*	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen; */
}

/* *** dxr3_flush ***
   flush the dxr3's onboard buffers - but I'm not sure that this is 
   doing that - more testing is required.
*/
static void dxr3_flush (video_decoder_t *this_gen) 
{
	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen;
	/* Don't flush, causes still images to disappear. We don't seem
	 * to need it anyway... */
	printf("dxr3_decoder: flush requested, disabled for the moment.\n");
	/* dxr3_mvcommand(this->fd_control, 0x11);
	this->have_header_info = 0;*/
	if (this->fd_video >= 0)
		fsync(this->fd_video);
}


static void dxr3_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen;
	ssize_t written;
	int64_t vpts;
	int i, duration, skip;
        vo_frame_t *img;
	uint8_t *buffer, byte;
	uint32_t shift;

	if (buf->decoder_flags & BUF_FLAG_PREVIEW) return;

	vpts = 0;

	/* count the number of frame codes in this block of data 
	 * this code borrowed from libmpeg2. 
	 * Note: this uses the 'naive' approach of constant durations,
	 * not the real NTSC-like durations that vary dep on repeat first
	 * field flags and stuff. */
	buffer = buf->content;
	shift = 0xffffff00;
	for (i=0; i<buf->size; i++) {
		byte = *buffer++;
		if (shift != 0x00000100) {
			shift = (shift | byte) << 8;
			continue;
		}
		/* header code of some kind found */
		/* printf("dxr3: have header %x\n", byte); */
		shift = 0xffffff00;
		if (byte == 0xb3) {
			/* sequence data, also borrowed from libmpeg2 */
			/* will enable have_header_info */
			parse_mpeg_header(this, buffer);
			continue;
		}
		if (byte == 0xb5) {
			/* extension data */
			if ((buffer[0] & 0xf0) == 0x80) {
				/* picture coding extension */
    				this->repeat_first_field = (buffer[3] >> 1) & 1;
			}
			/* check if we can keep syncing */
			if (this->repeat_first_field && this->sync_every_frame) {
				/* metronom can't handle variable duration */
				printf("dxr3: non-progressive video detected. "
					"disabling sync_every_frame.\n");
				this->sync_every_frame = 0;
				this->sync_retry = 500; /* see you later */
			}
			if (this->repeat_first_field && this->sync_retry) {
				/* reset counter */
				this->sync_retry = 500;	
			}
			continue;
		}
		if (byte != 0x00) {
			/* Don't care what it is. It's not a new frame */
			continue;
		}
		/* we have a code for a new frame */
		if (! this->have_header_info) {
			/* this->width et al may still be undefined */
			continue;
		}
		duration = get_duration(this->frame_rate_code, 
					this->repeat_first_field);
		/* pretend like we have decoded a frame */
		img = this->video_out->get_frame (this->video_out,
                             this->width,
                             this->height,
                             this->aspect,
                             IMGFMT_MPEG,
                             VO_BOTH_FIELDS);
		img->pts=buf->pts;
		img->bad_frame = 0;
		img->duration = duration;
		/* draw calls metronom->got_video_frame with img pts and scr
		   and stores the return value in img->vpts
		   Calling draw with buf->pts==0 is okay; metronome will
		   extrapolate a value. */
		skip = img->draw(img);
	        if (skip <= 0) { /* don't skip */
			vpts = img->vpts; /* copy so we can free img */
			
			if (this->resync_window == 0 && this->scr && this->enhanced_mode && !scanning_mode) {
				/* we are in sync, so we can lock the stream now */
				printf("dxr3: in sync, stream locked\n");
				dxr3_mvcommand(this->fd_control, MVCOMMAND_SYNC);
				this->resync_window = -RESYNC_WINDOW_SIZE;
			}
			if (this->resync_window != 0 && this->resync_window > -RESYNC_WINDOW_SIZE)
				this->resync_window--;
		}
		else { /* metronom says skip, so don't set vpts */
			printf("dxr3: skip = %d\n", skip);
			vpts = 0;
			
			if (scanning_mode) this->resync_window = 0;
			if (this->resync_window == 0 && this->scr && this->enhanced_mode && !scanning_mode) {
				/* switch off sync mode in the card to allow resyncing */
				printf("dxr3: out of sync, allowing stream resync\n");
				dxr3_mvcommand(this->fd_control, MVCOMMAND_START);
				this->resync_window = RESYNC_WINDOW_SIZE;
			}
			if (this->resync_window != 0 && this->resync_window < RESYNC_WINDOW_SIZE)
				this->resync_window++;
		}
		img->free(img);
		this->last_pts += duration; /* predict vpts */
		
		/* if sync_every_frame was disabled, decrease the counter
		 * for a retry 
		 * (it might be due to crappy studio logos and stuff
		 * so we should give the main movie a chance) */
		if (this->sync_retry) {
			this->sync_retry--;
			if (!this->sync_retry) {
				printf("dxr3: retrying sync_every_frame");
				this->sync_every_frame = 1;
			}
		}
	}


	/* ensure video device is open 
	 * (we open it late because on occasion the dxr3 video out driver
	 * wants to open it) */
	if (this->fd_video < 0) {	
		char tmpstr[128];
		snprintf (tmpstr, sizeof(tmpstr), "%s_mv%s", devname, devnum);
		if ((this->fd_video = open (tmpstr, O_WRONLY | O_NONBLOCK)) < 0) {
			printf("dxr3: Failed to open video device %s (%s)\n",
				tmpstr, strerror(errno)); 
			return;
		}
	}

	/* We may want to issue a SETPTS, so make sure the scr plugin
	   is running and registered. Unfortuantely wa cannot do this
	   earlier, because the dxr3's internal scr gets confused
	   when started with a closed video device. Maybe this is a
	   driver bug and gets fixed somewhen. FIXME: We might then
	   want to move this code to dxr3_init. */
	if (!this->scr) {
		int64_t time;
		
		time = this->metronom->get_current_time(
			this->metronom);
		
		this->scr = dxr3scr_init(this);
		this->scr->start(this->scr, time);
		this->metronom->register_scr(
			this->metronom, this->scr);
		if (this->metronom->scr_master == this->scr) {
			printf("dxr3: dxr3scr plugin is master\n");
		} else {
			printf("dxr3: dxr3scr plugin is NOT master\n");
		}
	}

	/* From time to time, update the pts value 
	 * FIXME: the exact conditions here are a bit uncertain... */
	if (vpts)
	{
		int64_t delay;
		
		delay = vpts - this->metronom->get_current_time(
			this->metronom);
#if LOG_PTS
		printf("dxr3: SETPTS got %lld expected = %lld (delta %lld) delay = %lld\n", 
			vpts, this->last_pts, vpts-this->last_pts, delay);
#endif
		this->last_pts = vpts;
		
		/* SETPTS only if less then one second in the future and
		 * either buffer has pts or sync_every_frame is set */
		if ((delay > 0) && (delay < 90000) &&
		    (this->sync_every_frame || buf->pts)) {
			uint32_t vpts32 = vpts;
			/* update the dxr3's current pts value */
			if (ioctl(this->fd_video, EM8300_IOCTL_VIDEO_SETPTS, &vpts32))
				printf("dxr3: set video pts failed (%s)\n",
					 strerror(errno));
		}
		if (delay >= 90000) {
			/* frame more than 1 sec ahead */
			printf("dxr3: WARNING: vpts %lld is %.02f seconds ahead of time!\n",
				vpts, delay/90000.0); 
		}
		if (delay < 0) {
			printf("dxr3: WARNING: overdue frame.\n");
		}
	}
#if LOG_PTS
	else if (buf->pts) {
		printf("dxr3: skip buf->pts = %lld (no vpts) last_vpts = %lld\n", 
			buf->pts, this->last_pts);
	}
#endif

	/* now write the content to the dxr3 mpeg device and, in a dramatic
	   break with open source tradition, check the return value */
	written = write(this->fd_video, buf->content, buf->size);
	if (written < 0) {
		if (errno == EAGAIN) {
			printf("dxr3: write to device would block. flushing\n");
			dxr3_flush(this_gen);
		}
		else {
			printf("dxr3: video device write failed (%s)\n",
			 	strerror(errno));
		}
		return;
	}
	if (written != buf->size)
		printf("dxr3: Could only write %d of %d video bytes.\n",
		 written, buf->size);

}

static void dxr3_close (video_decoder_t *this_gen)
{
	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen;
	metronom_t *metronom = this->metronom;
	int64_t cur_offset;

	if (this->scr) {
		metronom->unregister_scr(metronom, this->scr);
		this->scr->exit(this->scr);
		this->scr = 0;
	}
	
	/* revert av_offset change done in dxr3_init */
	cur_offset = metronom->get_option(metronom, METRONOM_AV_OFFSET);
	metronom->set_option(metronom, METRONOM_AV_OFFSET, cur_offset + 21600);
	
	if (this->fd_video >= 0)
		close(this->fd_video);
	this->fd_video = -1;

	this->video_out->close(this->video_out);
	this->have_header_info = 0;
}

static char *dxr3_get_id(void) {
  return "dxr3-mpeg2";
}

static void dxr3_update_enhanced_mode(void *this_gen, cfg_entry_t *entry)
{
	((dxr3_decoder_t*)this_gen)->enhanced_mode = entry->num_value;
	printf("dxr3: mpeg playback: set enhanced mode to %s\n", 
		(entry->num_value ? "on" : "off"));
}

static void dxr3_update_sync_mode(void *this_gen, cfg_entry_t *entry)
{
	((dxr3_decoder_t*)this_gen)->sync_every_frame = entry->num_value;
	printf("dxr3: set sync_every_frame to %s\n", 
		(entry->num_value ? "on" : "off"));
}

static void dxr3_flush_decoder(void *this_gen, cfg_entry_t *entry)
{
	/* dxr3_decoder_t *this = (dxr3_decoder_t*)this_gen; */
	printf("dxr3: flush requested\n");
/*
	pthread_kill(this->decoder_thread, SIGINT);
	if (this->fd_video >= 0) {
		close(this->fd_video);
		this->fd_video = -1;
	}
*/
	dxr3_flush(this_gen);
	/* reset to false, so it'll look like a button in the gui :-) */
	entry->num_value = 0;	
}

static void dxr3_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

video_decoder_t *init_video_decoder_plugin (int iface_version,
 xine_t *xine)
{
	dxr3_decoder_t *this ;
	config_values_t *cfg;
	char *tmpstr;
	int dashpos;
        int result;

	if (iface_version != 8) {
		printf( "dxr3_decode_video: plugin doesn't support plugin API version %d.\n"
		 "dxr3_decode_video: this means there's a version mismatch between xine and this\n"
		 "dxr3_decode_video: decoder plugin. Installing current plugins should help.\n",
		 iface_version);
		return NULL;
	}
 
	cfg = xine->config;
	tmpstr = cfg->register_string (cfg, LOOKUP_DEV, DEFAULT_DEV, "Dxr3: Device Name",NULL,NULL,NULL);
	strncpy(devname, tmpstr, 128);
	devname[127] = '\0';
	dashpos = strlen(devname) - 2; /* the dash in the new device naming scheme would be here */
	if (devname[dashpos] == '-') {
		/* use new device naming scheme with trailing number */
		strncpy(devnum, &devname[dashpos], 3);
		devname[dashpos] = '\0';
	} else {
		/* use old device naming scheme without trailing number */
		/* FIXME: remove this when everyone uses em8300 >=0.12.0 */
		devnum[0] = '\0';
	}

	result = dxr3_presence_test ( xine );
	if (!result) return NULL;

	this = (dxr3_decoder_t *) malloc (sizeof (dxr3_decoder_t));

	this->video_decoder.interface_version   = iface_version;
	this->video_decoder.can_handle          = dxr3_can_handle;
	this->video_decoder.init                = dxr3_init;
	this->video_decoder.decode_data         = dxr3_decode_data;
	this->video_decoder.close               = dxr3_close;
	this->video_decoder.get_identifier      = dxr3_get_id;
	this->video_decoder.flush		= dxr3_flush;
	this->video_decoder.reset		= dxr3_reset;
	this->video_decoder.dispose		= dxr3_dispose;
	this->video_decoder.priority            = cfg->register_num(cfg,
		"dxr3.decoder_priority", 10, "Dxr3: video decoder priority", NULL, NULL, NULL);
	
	this->config			        = cfg;
        this->metronom                          = xine->metronom;
	this->frame_rate_code = 0;
	this->repeat_first_field = 0;
	this->sync_every_frame = 1;

	this->scr = NULL;
	this->scr_prio = cfg->register_num(cfg, "dxr3.scr_priority", 10, "Dxr3: SCR plugin priority",NULL,NULL,NULL); 
        
	this->sync_every_frame = cfg->register_bool(cfg,
		"dxr3.sync_every_frame", 
		0, 
		"Try to sync video every frame",
		"This is relevant for progressive video only (most PAL films)",
		dxr3_update_sync_mode, this);

	this->sync_retry = 0;

	this->enhanced_mode = cfg->register_bool(cfg,
		"dxr3.alt_play_mode", 
		1, 
		"Use alternate Play mode",
		"Enabling this option will utilise a slightly different play mode",
		dxr3_update_enhanced_mode, this);

	/* a boolean that's really a button; request a decoder flush */
	cfg->register_bool(cfg, "dxr3.flush", 0, "Flush decoder now", 
		"Flushing the decoder might unfreeze playback or restore sync", 
		dxr3_flush_decoder, this);
		
	if(this->enhanced_mode)
	  printf("dxr3: Using Mode 6 for playback\n");
	this->have_header_info = 0;
	this->in_buffer_fill = 0;
	return (video_decoder_t *) this;
}

