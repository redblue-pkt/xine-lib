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
 * $Id: dxr3_decoder.c,v 1.16 2001/10/14 14:49:54 ehasenle Exp $
 *
 * dxr3 video and spu decoder plugin. Accepts the video and spu data
 * from XINE and sends it directly to the corresponding dxr3 devices.
 * Takes precedence over the libmpeg2 and libspudec due to a higher
 * priority.
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

#define LOOKUP_DEV "dxr3_devname"
#define DEFAULT_DEV "/dev/em8300"
static char *devname;

typedef struct dxr3_decoder_s {
	video_decoder_t video_decoder;
	vo_instance_t *video_out;

	int fd_video;
	int last_pts;
	scr_plugin_t *scr;
	int scr_prio;
	int width;
	int height;
	int aspect;
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
		fprintf(stderr, "dxr3: not detected (%s: %s)\n",
			devname, strerror(errno));
		return;
	}
	if (ioctl(fd, EM8300_IOCTL_GET_AUDIOMODE, &val)<0) {
		fprintf(stderr, "dxr3: ioctl failed (%s)\n", strerror(errno));
		return;
	}
	close(fd);
	dxr3_ok = 1;
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

static int dxr3scr_set_speed (scr_plugin_t *scr, int speed) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	uint32_t em_speed;

	switch(speed){
	case SPEED_PAUSE:
		em_speed = 0;
		break;
	case SPEED_SLOW_4:
		em_speed = 0x900/4;
		break;
	case SPEED_SLOW_2:
		em_speed = 0x900/2;
		break;
	case SPEED_NORMAL:
		em_speed = 0x900;
		break;
	case SPEED_FAST_2:
		em_speed = 0x900*2;
		break;
	case SPEED_FAST_4:
		em_speed = 0x900*4;
		break;
	default:
		em_speed = 0;
	}
	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_SETSPEED, &em_speed))
		fprintf(stderr, "dxr3scr: failed to set speed (%s)\n", strerror(errno));

	return speed;
}

static void dxr3scr_adjust (scr_plugin_t *scr, uint32_t vpts) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	vpts >>= 1;

	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_SET, &vpts))
		fprintf(stderr, "dxr3scr: adjust failed (%s)\n", strerror(errno));
}

static void dxr3scr_start (scr_plugin_t *scr, uint32_t start_vpts) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	start_vpts >>= 1;

	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_SET, &start_vpts))
		fprintf(stderr, "dxr3scr: start failed (%s)\n", strerror(errno));
	/* mis-use start_vpts */
	start_vpts = 0x900;
	ioctl(self->fd_control, EM8300_IOCTL_SCR_SETSPEED, &start_vpts);
}

static uint32_t dxr3scr_get_current (scr_plugin_t *scr) {
	dxr3scr_t *self = (dxr3scr_t*) scr;
	uint32_t pts;

	if (ioctl(self->fd_control, EM8300_IOCTL_SCR_GET, &pts))
		fprintf(stderr, "dxr3scr: get current failed (%s)\n", strerror(errno));

	return pts << 1;
}

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
		fprintf(stderr, "dxr3scr: Failed to open control device %s (%s)\n",
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
		fprintf(stderr, "dxr3: Failed to open video device %s (%s)\n",
		 tmpstr, strerror(errno));
		return;
	}

	video_out->open(video_out);
	this->video_out = video_out;

	this->last_pts = 0;
	
	this->scr = dxr3scr_init(this);
	this->video_decoder.metronom->register_scr(
	 this->video_decoder.metronom, this->scr);

	/* dxr3_init is called while the master scr already runs.
	   therefore the scr must be started here */
	this->scr->start(this->scr, 0);
}

#define HEADER_OFFSET 4
static void find_aspect(dxr3_decoder_t *this, uint8_t * buffer)
{
	/* only carry on if we have a legitimate mpeg header... */
	if (buffer[1]==0 && buffer[0]==0 && buffer[2]==1 && buffer[3]==0xb3) {
		int old_h = this->height;
		int old_w = this->width;
		int old_a = this->aspect;

		/* grab video resolution and aspect ratio from the stream */
		this->height = (buffer[HEADER_OFFSET+0] << 16) |
		               (buffer[HEADER_OFFSET+1] <<  8) |
					    buffer[HEADER_OFFSET+2];
		this->width  = ((this->height >> 12) + 15) & ~15;
		this->height = ((this->height & 0xfff) + 15) & ~15;
		this->aspect = buffer[HEADER_OFFSET+3] >> 4;

		/* and ship the data if different ... appeasing any other vo plugins
		that are active ... */
		if (old_h!=this->height || old_w!=this->width || old_a!=this->aspect)
			this->video_out->get_frame(this->video_out,
			 this->width,this->height,this->aspect,
			 IMGFMT_YV12, 1, VO_BOTH_FIELDS);
	}
}

static void dxr3_decode_data (video_decoder_t *this_gen, buf_element_t *buf)
{
	dxr3_decoder_t *this = (dxr3_decoder_t *) this_gen;
	ssize_t written;

	/* Ignore videofill packets */
	if (buf->type == BUF_VIDEO_FILL) return;

	/* The dxr3 does not need the preview-data */
	if (buf->decoder_info[0] == 0) return;

	if (buf->PTS) {
		int vpts;
		vpts = this->video_decoder.metronom->got_video_frame(
		 this->video_decoder.metronom, buf->PTS);

		if (this->last_pts < vpts)
		{
			this->last_pts = vpts;

			if (ioctl(this->fd_video, EM8300_IOCTL_VIDEO_SETPTS, &vpts))
				fprintf(stderr, "dxr3: set video pts failed (%s)\n",
				 strerror(errno));
		}
	}

	written = write(this->fd_video, buf->content, buf->size);
	if (written < 0) {
		fprintf(stderr, "dxr3: video device write failed (%s)\n",
		 strerror(errno));
		return;
	}
	if (written != buf->size)
		fprintf(stderr, "dxr3: Could only write %d of %d video bytes.\n",
		 written, buf->size);

	/* run the header parser here... otherwise the dxr3 tends to block... */
	find_aspect(this, buf->content);	
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

	if (iface_version != 2) {
		printf( "dxr3: plugin doesn't support plugin API version %d.\n"
		 "dxr3: this means there's a version mismatch between xine and this\n"
		 "dxr3: decoder plugin. Installing current plugins should help.\n",
		 iface_version);
		return NULL;
	}

	devname = cfg->lookup_str (cfg, LOOKUP_DEV, DEFAULT_DEV);
	dxr3_presence_test ();
	if (!dxr3_ok) return NULL;

	this = (dxr3_decoder_t *) malloc (sizeof (dxr3_decoder_t));

	this->video_decoder.interface_version   = 2;
	this->video_decoder.can_handle          = dxr3_can_handle;
	this->video_decoder.init                = dxr3_init;
	this->video_decoder.decode_data         = dxr3_decode_data;
	this->video_decoder.close               = dxr3_close;
	this->video_decoder.get_identifier      = dxr3_get_id;
	this->video_decoder.priority            = 10;

	this->scr_prio = cfg->lookup_int(cfg, "dxr3_scr_prio", 10);

	return (video_decoder_t *) this;
}

/*
 * Second part of the dxr3 plugin: subpicture decoder
 */

typedef struct spudec_decoder_s {
	spu_decoder_t    spu_decoder;

	vo_instance_t   *vo_out;
	int fd_spu;
} spudec_decoder_t;

static int spudec_can_handle (spu_decoder_t *this_gen, int buf_type)
{
	int type = buf_type & 0xFFFF0000;
	return (type == BUF_SPU_PACKAGE || type == BUF_SPU_CLUT);
}

static void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out)
{
	spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
	char tmpstr[100];

	this->vo_out = vo_out;

	/* open spu device */
	snprintf (tmpstr, sizeof(tmpstr), "%s_sp", devname);
	if ((this->fd_spu = open (tmpstr, O_WRONLY)) < 0) {
		fprintf(stderr, "dxr3: Failed to open spu device %s (%s)\n",
		 tmpstr, strerror(errno));
		return;
	}

}

static void swab_clut(int* clut)
{
	int i;
	for (i=0; i<16; i++)
		clut[i] = bswap_32(clut[i]);
}

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf)
{
	spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
	ssize_t written;

	if (buf->type == BUF_SPU_CLUT) {
		if (buf->content[0] == 0)  /* cheap endianess detection */
			swab_clut((int*)buf->content);
		if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPALETTE, buf->content))
			fprintf(stderr, "dxr3: failed to set CLUT (%s)\n", strerror(errno));
		return;
	}

	/* Is this also needed for subpicture? */
	if (buf->decoder_info[0] == 0) return;

	if (buf->PTS) {
		int vpts;
		vpts = this->spu_decoder.metronom->got_spu_packet
		 (this->spu_decoder.metronom, buf->PTS, 0);

		if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPTS, &vpts))
			fprintf(stderr, "dxr3: spu setpts failed (%s)\n", strerror(errno));
	}

	written = write(this->fd_spu, buf->content, buf->size);
	if (written < 0) {
		fprintf(stderr, "dxr3: spu device write failed (%s)\n",
		 strerror(errno));
		return;
	}
	if (written != buf->size)
		fprintf(stderr, "dxr3: Could only write %d of %d spu bytes.\n",
		 written, buf->size);
}

static void spudec_close (spu_decoder_t *this_gen)
{
	spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
	
	close(this->fd_spu);
}

static void spudec_event (spu_decoder_t *this_gen, spu_event_t *event) {
	spudec_decoder_t *this = (spudec_decoder_t*) this_gen;
	switch (event->sub_type) {
	case SPU_EVENT_BUTTON:
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
				fprintf(stderr, "dxr3: failed to set spu button (%s)\n",
				 strerror(errno));
		}
		break;
	case SPU_EVENT_CLUT:
		{
			spu_cltbl_t *clut = event->data;
#ifdef WORDS_BIGENDIAN
			swab_clut(clut->clut);
#endif

			if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPALETTE, clut->clut))
				fprintf(stderr, "dxr3: failed to set CLUT (%s)\n",
				 strerror(errno));
		}
		break;
	}
}

static char *spudec_get_id(void)
{
	return "dxr3-spudec";
}

spu_decoder_t *init_spu_decoder_plugin (int iface_version,
 config_values_t *cfg)
{
  spudec_decoder_t *this;

  if (iface_version != 3) {
    printf( "dxr3: plugin doesn't support plugin API version %d.\n"
	    "dxr3: this means there's a version mismatch between xine and this "
	    "dxr3: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  devname = cfg->lookup_str (cfg, LOOKUP_DEV, DEFAULT_DEV);
  dxr3_presence_test ();
  if (!dxr3_ok) return NULL;

  this = (spudec_decoder_t *) malloc (sizeof (spudec_decoder_t));

  this->spu_decoder.interface_version   = 3;
  this->spu_decoder.can_handle          = spudec_can_handle;
  this->spu_decoder.init                = spudec_init;
  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.event               = spudec_event;
  this->spu_decoder.close               = spudec_close;
  this->spu_decoder.get_identifier      = spudec_get_id;
  this->spu_decoder.priority            = 10;
  
  return (spu_decoder_t *) this;
}

