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
 * $Id: dxr3_decode_spu.c,v 1.1 2002/05/02 14:33:30 jcdutton Exp $
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

int spudec_copy_nav_to_btn(pci_t* nav_pci, int32_t button, int32_t mode, em8300_button_t* btn );

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
	pci_t			pci;
	uint32_t		buttonN;  /* currently highlighted button */
	int64_t			button_vpts; /* time to show the menu enter button */
	xine_t			*xine;
} spudec_decoder_t;

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

static int spudec_can_handle (spu_decoder_t *this_gen, int buf_type)
{
	int type = buf_type & 0xFFFF0000;
	return (type == BUF_SPU_PACKAGE || type == BUF_SPU_CLUT || 
		type == BUF_SPU_NAV || type == BUF_SPU_SUBP_CONTROL);
}

static void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out)
{
	spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
	char tmpstr[100];
	int i;
	
	this->vo_out = vo_out;

	/* open spu device */
	snprintf (tmpstr, sizeof(tmpstr), "%s_sp%s", devname, devnum);
	if ((this->fd_spu = open (tmpstr, O_WRONLY)) < 0) {
		printf("dxr3: Failed to open spu device %s (%s)\n",
		 tmpstr, strerror(errno));
		return;
	}
#if LOG_SPU
        printf ("dxr3_spu: init: SPU_FD = %i\n",this->fd_spu);
#endif

        for (i=0; i < MAX_SPU_STREAMS; i++) /* reset the spu filter for non-dvdnav */
                 this->spu_stream_state[i].stream_filter = 1;
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
	uint32_t stream_id = buf->type & 0x1f ;
	
	if (this->button_vpts &&
		this->xine->metronom->get_current_time(this->xine->metronom) > this->button_vpts) {
		/* we have a scheduled menu enter button and it's time to show it now */
		em8300_button_t button;
		int buttonNr;
		btni_t *button_ptr;
		
		buttonNr = this->buttonN;
		
		if (this->pci.hli.hl_gi.fosl_btnn > 0)
			buttonNr = this->pci.hli.hl_gi.fosl_btnn ;
		
		if ((buttonNr <= 0) || (buttonNr > this->pci.hli.hl_gi.btn_ns)) {
			printf("dxr3_spu: Unable to select button number %i as it doesn't exist. Forcing button 1\n", buttonNr);
			buttonNr = 1;
		}
		
		button_ptr = &this->pci.hli.btnit[buttonNr-1];
		button.left = button_ptr->x_start;
		button.top  = button_ptr->y_start;
		button.right = button_ptr->x_end;
		button.bottom = button_ptr->y_end;
		button.color = this->pci.hli.btn_colit.btn_coli[button_ptr->btn_coln-1][0] >> 16;
		button.contrast = this->pci.hli.btn_colit.btn_coli[button_ptr->btn_coln-1][0] & 0xffff;
		
#if LOG_SPU
		printf("dxr3_spu: now showing menu enter button %i.\n", buttonNr);
#endif
		ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, &button);
		this->button_vpts = 0;
		this->pci.hli.hl_gi.hli_s_ptm = 0;
	}
	
	if (buf->type == BUF_SPU_CLUT) {
#if LOG_SPU
        printf ("dxr3_spu: BUF_SPU_CLUT\n" );
#endif
#if LOG_SPU
        printf ("dxr3_spu: buf clut: SPU_FD = %i\n",this->fd_spu);
#endif
		if (buf->content[0] == 0)  /* cheap endianess detection */
			swab_clut((int*)buf->content);
		if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPALETTE, buf->content))
			printf("dxr3: failed to set CLUT (%s)\n", strerror(errno));
		return;
	}
        if(buf->type == BUF_SPU_SUBP_CONTROL){
/***************
		int i;
                uint32_t *subp_control = (uint32_t*) buf->content;
                for (i = 0; i < 32; i++) {
	        	this->spu_stream_state[i].stream_filter = subp_control[i];
                 }
***************/
     		return;
	}
        if(buf->type == BUF_SPU_NAV){
#if LOG_SPU
		printf("dxr3_spu: Got NAV packet\n");
#endif
		uint8_t *p = buf->content;
		
		/* just watch out for menus */
		if (p[3] == 0xbf && p[6] == 0x00) { /* Private stream 2 */
			pci_t pci;
			
			nav_read_pci(&pci, p + 7);
			
			if (pci.hli.hl_gi.hli_ss == 1) {
                                em8300_button_t btn;
				/* menu ahead, remember pci for later evaluation */
				xine_fast_memcpy(&this->pci, &pci, sizeof(pci_t));
                                if ( (spudec_copy_nav_to_btn(&this->pci, this->buttonN, 0, &btn ) > 0)) {
                                  if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, &btn)) {
  	                            printf("dxr3: failed to set spu button (%s)\n",
		                    strerror(errno));
                                  }
                                }
                        }
			
			if ((pci.hli.hl_gi.hli_ss == 0) && (this->pci.hli.hl_gi.hli_ss == 1)) {
				/* leaving menu */
				xine_fast_memcpy(&this->pci, &pci, sizeof(pci_t));
				ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, NULL);
			}
		}
		return;
	}
	/* Is this also needed for subpicture? */
	if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
#if LOG_SPU
        printf ("dxr3_spu: Dropping SPU channel %d. Preview data\n", stream_id);
#endif
          return;
        }

	if ( this->spu_stream_state[stream_id].stream_filter == 0) {
#if LOG_SPU
        printf ("dxr3_spu: Dropping SPU channel %d. Stream filtered\n", stream_id);
#endif
          return;
        }
/* spu_channel is now set based on whether we are in the menu or not. */
/* Bit 7 is set if only forced display SPUs should be shown */
        if ( (this->xine->spu_channel & 0x1f) != stream_id  ) { 
#if LOG_SPU
          printf ("dxr3_spu: Dropping SPU channel %d. Not selected stream_id\n", stream_id);
#endif
          return;
        }
	if ( (this->menu == 0) && (this->xine->spu_channel & 0x80) ) { 
#if LOG_SPU
	  printf ("dxr3_spu: Dropping SPU channel %d. Only allow forced display SPUs\n", stream_id);
#endif
	  return;
	}
//        if (this->xine->spu_channel != stream_id && this->menu!=1 ) return; 
        /* Hide any previous button highlights */
//	ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, NULL);
	if (buf->pts) {
		int64_t vpts;
		uint32_t vpts32;
		
		vpts = this->xine->metronom->got_spu_packet
		 (this->xine->metronom, buf->pts);
#if LOG_SPU
                printf ("dxr3_spu: pts=%lld vpts=%lld\n", buf->pts, vpts);
#endif
		vpts32 = vpts;
		if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_SETPTS, &vpts32))
			printf("dxr3: spu setpts failed (%s)\n", strerror(errno));
			
		if (buf->pts == this->pci.hli.hl_gi.hli_s_ptm)
			/* schedule the menu enter button for current packet's vpts, so
			that the card has decoded this SPU when we try to show the button */
			this->button_vpts = vpts;
	}


#if LOG_SPU
        printf ("dxr3_spu: write: SPU_FD = %i\n",this->fd_spu);
#endif
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

static void spudec_reset (spu_decoder_t *this_gen)
{
}

static void spudec_close (spu_decoder_t *this_gen)
{
	spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
#if LOG_SPU
        printf ("dxr3_spu: close: SPU_FD = %i\n",this->fd_spu);
#endif
	
	close(this->fd_spu);
        this->fd_spu				= 0;
}

int spudec_copy_nav_to_btn(pci_t* nav_pci, int32_t button, int32_t mode, em8300_button_t* btn ) {
  btni_t *button_ptr;

  /* FIXME: Need to communicate with dvdnav vm to get/set
    "self->vm->state.HL_BTNN_REG" info.
    now done via button events from dvdnav.
   *
   * if ( this->pci.hli.hl_gi.fosl_btnn > 0) {
   *   button = this->pci.hli.hl_gi.fosl_btnn ;
   * }
   */
  if((button <= 0) || (button > nav_pci->hli.hl_gi.btn_ns)) {
    printf("dxr3_decoder:Unable to select button number %i as it doesn't exist. Forcing button 1\n",
              button);
    button = 1;
  }
  /* There is no point in highlighting an area it the area's colours are no different from the general overlay colours. */
  button_ptr = &nav_pci->hli.btnit[button-1];
  if(button_ptr->btn_coln != 0) {
#ifdef LOG_BUTTON
    fprintf(stderr, "libspudec: normal button clut\n");
#endif
    btn->color = (nav_pci->hli.btn_colit.btn_coli[button_ptr->btn_coln-1][mode] >> 16 );
    btn->contrast = (nav_pci->hli.btn_colit.btn_coli[button_ptr->btn_coln-1][mode] );
    /* FIXME:Only the first grouping of buttons are used at the moment */
    btn->left = button_ptr->x_start;
    btn->top  = button_ptr->y_start;
    btn->right = button_ptr->x_end;
    btn->bottom = button_ptr->y_end;
    return 1;
  } 
  return -1;
}

static void spudec_event_listener (void *this_gen, xine_event_t *event_gen) {

  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;
  xine_spu_event_t *event = (xine_spu_event_t *) event_gen;
#if LOG_SPU
        printf ("dxr3_spu: event: SPU_FD = %i\n",this->fd_spu);
#endif
  
  switch (event->event.type) {
  case XINE_EVENT_SPU_BUTTON:
    {

      spu_button_t *but = event->data;
      em8300_button_t btn;
#if LOG_SPU
        printf ("dxr3_spu: SPU_BUTTON\n");
#endif
      
//      if (!but->show) {
//	ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, NULL);
//	break;
//      }
      
      this->buttonN = but->buttonN;
      if ( (but->show > 0) && (spudec_copy_nav_to_btn(&this->pci, this->buttonN, but->show - 1, &btn ) > 0)) {
        if (ioctl(this->fd_spu, EM8300_IOCTL_SPU_BUTTON, &btn)) {
  	  printf("dxr3: failed to set spu button (%s)\n",
		strerror(errno));
        }
      }
#if LOG_SPU
        printf ("dxr3_spu: buttonN = %u\n",but->buttonN);
#endif
    }
    break;

  case XINE_EVENT_SPU_CLUT:
    {
      spudec_clut_table_t *clut = event->data;
#if LOG_SPU
        printf ("dxr3_spu: SPU_CLUT\n");
#endif
#if LOG_SPU
        printf ("dxr3_spu: clut: SPU_FD = %i\n",this->fd_spu);
#endif
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
  default:
    {
    }
        	 
  }  
}

static char *spudec_get_id(void)
{
	return "dxr3-spudec";
}

static void spudec_dispose (spu_decoder_t *this_gen) {
  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;

  xine_remove_event_listener (this->xine, spudec_event_listener);

  free (this);
}

spu_decoder_t *init_spu_decoder_plugin (int iface_version, xine_t *xine)
{
  spudec_decoder_t *this;
  config_values_t  *cfg;
  char *tmpstr;
  int dashpos;
  int result;

  if (iface_version != 7) {
    printf( "dxr3_decode_spu: plugin doesn't support plugin API version %d.\n"
	    "dxr3_decode_spu: this means there's a version mismatch between xine and this "
	    "dxr3_decode_spu: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  cfg = xine->config;
  tmpstr = cfg->register_string (cfg, LOOKUP_DEV, DEFAULT_DEV, NULL,NULL,NULL,NULL);
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
  if (!result) {
    return NULL;
  }

  this = (spudec_decoder_t *) malloc (sizeof (spudec_decoder_t));

  this->spu_decoder.interface_version   = iface_version;
  this->spu_decoder.can_handle          = spudec_can_handle;
  this->spu_decoder.init                = spudec_init;
  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.close               = spudec_close;
  this->spu_decoder.get_identifier      = spudec_get_id;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.priority            = 10;
  this->xine				= xine;
  this->menu				= 0;
  this->fd_spu				= 0;
  this->pci.hli.hl_gi.hli_ss		= 0;
  this->pci.hli.hl_gi.hli_s_ptm		= 0;
  this->buttonN				= 1;
  this->button_vpts			= 0;
  
  xine_register_event_listener(xine, spudec_event_listener, this);
  
  return (spu_decoder_t *) this;
}
