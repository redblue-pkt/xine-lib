/* 
 * Copyright (C) 2000, 2001 the xine project, 
 *                          Rich Wareham <richwareham@users.sourceforge.net>
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
 * $Id: input_dvd.c,v 1.53 2002/08/08 17:49:21 richwareham Exp $
 *
 */

/* This file was origninally part of the xine-dvdnav project
 * at http://dvd.sf.net/. 
 */

/* TODO:
 *
 *  - Proper internationalisation of strings.
 *  - Failure dialogue.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Standard includes */
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#include <sys/mount.h>
#include <sys/wait.h>

#include <sys/poll.h>
#include <sys/ioctl.h>

#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD__)
#include <sys/dvdio.h>
#include <sys/cdio.h> /* CDIOCALLOW etc... */
#elif defined(__linux__)
#include <linux/cdrom.h>
#else
#error "Need the DVD ioctls"
#endif

/* Xine includes */
#include <xine/xineutils.h>
#include <xine/buffer.h>
#include <xine/input_plugin.h>
#include <xine/video_out.h>
#include <xine/events.h>
#include <xine/metronom.h>
#include <xine/spu_decoder_api.h>
#include <xine/xine_internal.h>

/* DVDNAV includes */
#include <dvdnav.h>
 
/* libdvdread includes */
#include <dvdread/nav_read.h>

/* Print debug messages? */
/* #define INPUT_DEBUG 1 */

/* Print trace messages? */
/* #define INPUT_DEBUG_TRACE */

/* Print debug of eject */
/* #define LOG_DVD_EJECT */

/* Current play mode (title only or menus?) */
#define MODE_NAVIGATE 0
#define MODE_TITLE 1

/* Is seeking enabled? 1 - Yes, 0 - No */
#define CAN_SEEK 1

/* The default DVD device on Solaris is not /dev/dvd */
#if defined(__sun)
#define DVD_PATH "/vol/dev/aliases/cdrom0"
#else
#define DVD_PATH "/dev/dvd"
#endif 

/* Some misc. defines */
#define DVD_BLOCK_SIZE 2048
#ifndef BUF_DEMUX_BLOCK
#define BUF_DEMUX_BLOCK 0x05000000
#endif
#define VIDEO_FILL_THROTTLE 5

/* Debugging macros */
#if INPUT_DEBUG
#define dprint(s, args...) fprintf(stderr, __FUNCTION__ ": " s, ##args);
#else
#define dprint(s, args...) /* Nowt */
#endif

#if INPUT_DEBUG_TRACE
#define trace_print(s, args...) fprintf(stdout, __FUNCTION__ ": " s, ##args);
#else
#define trace_print(s, args...) /* Nothing */
#endif

/* Globals */
extern int errno;

/* Array to hold MRLs returned by get_autoplay_list */
#define MAX_DIR_ENTRIES 250
#define MAX_STR_LEN     255  
char    filelist[MAX_DIR_ENTRIES][MAX_STR_LEN];
char   *filelist2[MAX_DIR_ENTRIES];

/* A Temporary string (FIXME: May cause problems if multiple
 * dvdnavs in multiple threads). */
char    temp_str[256];
#define TEMP_STR_LEN 255

typedef struct {
  input_plugin_t    input_plugin; /* Parent input plugin type        */

  int               pause_timer;  /* Cell stil-time timer            */
  int               pause_counter;
  time_t	    pause_end_time;
  int32_t           buttonN;

  /* Flags */
  int               opened;       /* 1 if the DVD device is already open */
  
  /* Xine specific variables */
  config_values_t  *config;       /* Pointer to XineRC config file   */  
  char		   *dvd_device;	  /* Default DVD device		     */
  char             *current_dvd_device; /* DVD device currently open */
  char             *mrl;          /* Current MRL                     */
  int               mode;
  dvdnav_t         *dvdnav;       /* Handle for libdvdnav            */
  xine_t           *xine;
  char              dvd_name[128];
  size_t            dvd_name_length;                   
  mrl_t           **mrls;
  int               num_mrls;
  
  /* special buffer handling for libdvdnav caching */
  pthread_mutex_t   buf_mutex;
  void             *source;
  void            (*free_buffer)(buf_element_t *);
  int               mem_stack;
  unsigned char    *mem[1024];
} dvdnav_input_plugin_t;

static void flush_buffers(dvdnav_input_plugin_t *this);
static void xine_dvdnav_send_button_update(dvdnav_input_plugin_t *this, int mode);

/* Callback on device name change */
static void device_change_cb(void *data, cfg_entry_t *cfg) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t *) data;
  
  this->dvd_device = cfg->str_value;
}

static uint32_t dvdnav_plugin_get_capabilities (input_plugin_t *this_gen) {
  trace_print("Called\n");

  return INPUT_CAP_AUTOPLAY | INPUT_CAP_BLOCK | INPUT_CAP_CLUT |
#if CAN_SEEK
         INPUT_CAP_SEEKABLE | INPUT_CAP_VARIABLE_BITRATE | 
#endif
    INPUT_CAP_AUDIOLANG | INPUT_CAP_SPULANG | INPUT_CAP_GET_DIR | INPUT_CAP_CHAPTERS; 
}

void read_ahead_cb(void *this_gen, cfg_entry_t *entry) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;

  if(!this)
   return;

  if(!this->dvdnav)
   return;

  dvdnav_set_readahead_flag(this->dvdnav, entry->num_value);
}
 
void region_changed_cb(void *this_gen, cfg_entry_t *entry) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;

  if(!this)
   return;

  if(!this->dvdnav)
   return;

  if((entry->num_value >= 1) && (entry->num_value <= 8)) {
    /* FIXME: Remove debug message */
    dprint("Setting region code to %i (0x%x)\n",
	   entry->num_value, 1<<(entry->num_value-1));
    dvdnav_set_region_mask(this->dvdnav, 1<<(entry->num_value-1));
  }
}

void language_changed_cb(void *this_gen, cfg_entry_t *entry) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;

  if(!this)
   return;

  if(!this->dvdnav)
   return;

  dvdnav_menu_language_select(this->dvdnav, entry->str_value);
  dvdnav_audio_language_select(this->dvdnav, entry->str_value);
  dvdnav_spu_language_select(this->dvdnav, entry->str_value);
}
 
void update_title_display(dvdnav_input_plugin_t *this) {
  xine_ui_event_t uevent;
  int tt=-1, pr=-1;
  size_t temp_str_length=0;

  if(!this || !(this->xine)) 
   return;
  
  /* Set title/chapter display */
  uevent.event.type = XINE_EVENT_UI_SET_TITLE;
  uevent.data = temp_str;

  dvdnav_current_title_info(this->dvdnav, &tt, &pr);
  
  if(tt != -1) { 
    int num_angle = 0, cur_angle = 0;
    /* no menu here */    
    /* Reflect angle info if appropriate */
    dvdnav_get_angle_info(this->dvdnav, &cur_angle, &num_angle);
    if(num_angle > 1) {
      snprintf(temp_str, TEMP_STR_LEN,
               "Title %i, Chapter %i, Angle %i of %i",
               tt,pr,cur_angle, num_angle); 
    } else {
      snprintf(temp_str, TEMP_STR_LEN, 
	       "Title %i, Chapter %i",
	       tt,pr);
    }
  } else {
    strcpy(temp_str, "DVD Navigator: Menu");
  }
  temp_str_length = strlen(temp_str);
  
  if (this->dvd_name[0] != 0 && (temp_str_length + this->dvd_name_length < TEMP_STR_LEN)) {
      snprintf(temp_str+temp_str_length, TEMP_STR_LEN - temp_str_length, 
	       ", %s",
	       &this->dvd_name[0]);
  }
   
  dprint("Changing title to read '%s'\n", temp_str);
  xine_send_event(this->xine, &uevent.event);
}

static void dvdnav_plugin_stop (input_plugin_t *this_gen) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*) this_gen;
  fprintf(stderr, "dvdnav_plugin_stop called.\n");
  if (this->dvdnav) {
    fprintf(stderr, "Now get out of still.\n");
    dvdnav_still_skip(this->dvdnav);
  }
}

static void dvdnav_plugin_close (input_plugin_t *this_gen) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;
  
  trace_print("Called\n");
  
  if(this->opened) 
    dvdnav_close(this->dvdnav);
  this->dvdnav = NULL;
  this->opened = 0;
  this->dvd_name[0]            = 0;
  this->dvd_name_length        = 0;
}

static void dvdnav_build_mrl_list(dvdnav_input_plugin_t *this) {
  int num_titles, *num_parts;

  /* skip DVD if already open */
  if (this->opened) return;
  if (this->mrls) {
    free(this->mrls);
    this->mrls = NULL;
    this->num_mrls = 0;
  }

  if (dvdnav_open(&(this->dvdnav), 
		  this->current_dvd_device) == DVDNAV_STATUS_ERR) 
   return; 

  dvdnav_get_number_of_titles(this->dvdnav, &num_titles);
  if ((num_parts = (int *) calloc(num_titles, sizeof(int)))) {
    int num_mrls = 1, i;
    /* for each title, count the number of programs */
    for (i = 1; i <= num_titles; i++) {
      num_parts[i-1] = 0;
      dvdnav_title_play(this->dvdnav, i);
      dvdnav_get_number_of_programs(this->dvdnav, &num_parts[i-1]);
      num_mrls += num_parts[i-1]; /* num_mrls = total number of programs */
    }

    /* allocate enough memory for:
     * - a list of pointers to mrls       sizeof(mrl_t *)     * num_mrls + 1
     * - an array of mrl structures       sizeof(mrl_t)       * num_mrls
     * - enough chars for every filename  sizeof(char)*25     * num_mrls
     *   - "dvd://:000000.000000\0" = 25 chars
     */
    if ((this->mrls = (mrl_t **) malloc(sizeof(mrl_t *) + num_mrls *
	(sizeof(mrl_t*) + sizeof(mrl_t) + 25*sizeof(char))))) {
    
      /* the first mrl struct comes after the pointer list */
      mrl_t *mrl = (mrl_t *) &this->mrls[num_mrls+1];
      /* the chars for filenames come after the mrl structs */
      char *name = (char *) &mrl[num_mrls];
      int pos = 0, j;
      this->num_mrls = num_mrls;

      for (i = 1; i <= num_titles; i++) {
	for (j = (i == 1 ? 0 : 1); j <= num_parts[i-1]; j++) {
	  this->mrls[pos++] = mrl;
	  mrl->origin = NULL;
	  mrl->mrl = name;
	  mrl->link = NULL;
	  mrl->type = mrl_dvd;
	  mrl->size = 0;
	  snprintf(name, 25, (j == 0) ? "dvd://" :
		             (j == 1) ? "dvd://:%d" :
		                        "dvd://:%d.%d", i, j);
	  name = &name[25];
	  mrl++;
	}
      }
      this->mrls[pos] = NULL; /* terminate list */
    }
    free(num_parts);
  }

  /* Reset the VM so that we don't break anything */
  /* dvdnav_reset(this->dvdnav); */

  dvdnav_close(this->dvdnav);
}

/*
 * Opens the DVD plugin. The MRL takes the following form:
 *
 * dvd://[dvd_path][:vts[.program]]
 *
 * e.g.
 *   dvd://                    - Play (navigate) /dev/dvd
 *   dvd:///dev/dvd2           - Play (navigate) /dev/dvd2
 *   dvd:///dev/dvd2:1         - Play Title 1 from /dev/dvd2
 *   dvd://:1.3                - Play Title 1, program 3 from /dev/dvd
 */
static int dvdnav_plugin_open (input_plugin_t *this_gen, char *mrl) {
  char                  *locator;
  int                    colon_point;
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t *) this_gen;
  dvdnav_status_t        ret;
  char                  *intended_dvd_device;
  cfg_entry_t           *region_entry, *lang_entry, *cache_entry;
    
  trace_print("Called\n");
  /* printf("open1: dvdnav=%p opened=%d\n",this->dvdnav, this->opened); */
 
  this->mrl                    = mrl;
  this->pause_timer            = 0;
  this->dvd_name[0]            = 0;
  this->dvd_name_length        = 0;

  /* Check we can handle this MRL */
  if (!strncasecmp (mrl, "dvd://",9))
    locator = &mrl[9];
  else {
    return 0;
  }

  /* Attempt to parse MRL */
  colon_point=0;
  while((locator[colon_point] != '\0') && (locator[colon_point] != ':')) {
    colon_point++;
  }

  if(locator[colon_point] == ':') {
    this->mode = MODE_TITLE; 
  } else {
    this->mode = MODE_NAVIGATE;
  }

  locator[colon_point] = '\0';
  ret = DVDNAV_STATUS_OK;
  if(colon_point == 0) {
    /* Use default device */
    intended_dvd_device=this->dvd_device;
  } else {
    /* Use specified device */
    intended_dvd_device=locator;
  }
  
  if(this->opened) {
    if ( intended_dvd_device==this->current_dvd_device ) {
      /* Already open, so skip opening */
    } else {
      /* Changing DVD device */
      dvdnav_close(this->dvdnav);
      this->dvdnav=NULL;
      this->opened=0; 
      ret = dvdnav_open(&this->dvdnav, intended_dvd_device);
      if(ret == DVDNAV_STATUS_ERR) {
        fprintf(stderr, "Error opening DVD device\n");
        return 0;
      }
      this->opened=1;
      this->current_dvd_device=intended_dvd_device;
    }
  } else {
    ret = dvdnav_open(&this->dvdnav, intended_dvd_device);
    if(ret == DVDNAV_STATUS_ERR) {
      fprintf(stderr, "Error opening DVD device\n");
      return 0;
    }
    this->opened=1;
    this->current_dvd_device=intended_dvd_device;
  }
  if (1) {
    int fd, i;
    off64_t off;
    uint8_t data[DVD_VIDEO_LB_LEN];

    /* Read DVD name */
    fd=open(intended_dvd_device, O_RDONLY);
    if (fd > 0) { 
      off = lseek64( fd, 32 * (int64_t) DVD_VIDEO_LB_LEN, SEEK_SET );
      if( off == ( 32 * (int64_t) DVD_VIDEO_LB_LEN ) ) {
        off = read( fd, data, DVD_VIDEO_LB_LEN ); 
        close(fd);
        if (off == ( (int64_t) DVD_VIDEO_LB_LEN )) {
          fprintf( stderr, "DVD Title: ");
          for(i=25; i < 73; i++ ) {
            if((data[i] == 0)) break;
            if((data[i] > 32) && (data[i] < 127)) {
              fprintf(stderr, "%c", data[i]);
            } else {
              fprintf(stderr, " ");
            }
          }
          strncpy(&this->dvd_name[0], &data[25], 48);
          /* fprintf(stderr, "TITLE:%s\n",&this->dvd_name[0]); */
          this->dvd_name[48]=0;
          this->dvd_name_length=strlen(&this->dvd_name[0]);
          fprintf( stderr, "\nDVD Serial Number: ");
          for(i=73; i < 89; i++ ) {
            if((data[i] == 0)) break;
            if((data[i] > 32) && (data[i] < 127)) {
              fprintf(stderr, "%c", data[i]);
            } else {
              fprintf(stderr, " ");
            } 
          }
          fprintf( stderr, "\nDVD Title (Alternative): ");
          for(i=89; i < 128; i++ ) {
            if((data[i] == 0)) break;
            if((data[i] > 32) && (data[i] < 127)) {
              fprintf(stderr, "%c", data[i]);
            } else {
              fprintf(stderr, " ");
            }
          }
          fprintf( stderr, "\n");
        } else {
          fprintf( stderr, "libdvdread: Can't read name block. Probably not a DVD-ROM device.\n");
        }
      } else {
        fprintf( stderr, "libdvdread: Can't seek to block %u\n", 32 );
      }
    } else {
    fprintf(stderr,"NAME OPEN FAILED\n");
    }
  }

     
  /* Set region code */
  region_entry = this->config->lookup_entry(this->config,
					    "input.dvd_region");
  if(region_entry) {
    region_changed_cb(this, region_entry);
  }
  
  /* Set languages */
  lang_entry = this->config->lookup_entry(this->config,
					  "input.dvdnav_language");
  if(lang_entry) {
    language_changed_cb(this, lang_entry);
  }
  
  /* Set cache usage */
  cache_entry = this->config->lookup_entry(this->config,
					    "input.dvdnav_use_readahead");
  if(cache_entry) {
    read_ahead_cb(this, cache_entry);
  }
   
  if(this->mode == MODE_TITLE) {
    int tt, i, pr, found;
    int titles;
    
    /* A program and/or VTS was specified */
    locator += colon_point + 1;

    if(locator[0] == '\0') {
      /* Empty specifier */
      fprintf(stderr, "Incorrect MRL format.\n");
      dvdnav_close(this->dvdnav);
      return 0;
    }

    /* See if there is a period. */
    found = -1;
    for(i=0; i<strlen(locator); i++) {
      if(locator[i] == '.') {
	found = i;
	locator[i] = '\0';
      }
    }
    tt = strtol(locator, NULL,10);

    dvdnav_get_number_of_titles(this->dvdnav, &titles);
    if((tt <= 0) || (tt > titles)) {
      fprintf(stderr, "Title %i is out of range (1 to %i).\n", tt,
	      titles);
      dvdnav_close(this->dvdnav);
      return 0;
    }

    /* If there was a program specified, get that too. */
    pr = -1;
    if(found != -1) {
      pr = strtol(locator+found+1, NULL,10);
    }

    dprint("Jumping to VTS >%i<, prog >%i<\n", tt, pr);
    if(pr != -1) {
      dvdnav_part_play(this->dvdnav, tt, pr);
    } else {
      dvdnav_title_play(this->dvdnav, tt);
    }
  }
  
  dprint("DVD device successfully opened.\n");
  
  return 1;
}

static void dvdnav_plugin_free_buffer(buf_element_t *buf) {
  dvdnav_input_plugin_t *this = buf->source;
  
  pthread_mutex_lock(&this->buf_mutex);
  /* give this buffer back to libdvdnav */
  dvdnav_free_cache_block(this->dvdnav, buf->mem);
  /* reconstruct the original xine buffer */
  buf->free_buffer = this->free_buffer;
  buf->source = this->source;
  buf->mem = this->mem[--this->mem_stack];
  pthread_mutex_unlock(&this->buf_mutex);
  /* give this buffer back to xine's pool */
  buf->free_buffer(buf);
}

static buf_element_t *dvdnav_plugin_read_block (input_plugin_t *this_gen, 
						fifo_buffer_t *fifo, off_t nlen) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;
  buf_element_t      *buf;
  dvdnav_status_t     result;
  int                 event, len;
  int                 finished = 0;
  unsigned char      *block;

  if(fifo == NULL) {
    dprint("values of \\beta will give rise to dom!\n");
    return NULL;
  }

  /* Read buffer */
  buf = fifo->buffer_pool_alloc (fifo);
  block = buf->mem;

  while(!finished) {
    if (block != buf->mem) {
      /* if we already have a dvdnav cache block, give it back first */
      dvdnav_free_cache_block(this->dvdnav, block);
      block = buf->mem;
    }
    result = dvdnav_get_next_cache_block (this->dvdnav, &block, &event, &len);
    if(result == DVDNAV_STATUS_ERR) {
      fprintf(stderr, "Error getting next block from DVD (%s)\n",
	      dvdnav_err_to_string(this->dvdnav));
      if (block != buf->mem) dvdnav_free_cache_block(this->dvdnav, block);
      buf->free_buffer(buf);
      return NULL;
    }

    switch(event) {
     case DVDNAV_BLOCK_OK: 
       {
	buf->content = block;
	buf->type = BUF_DEMUX_BLOCK;

	/* Make sure we don't think we are still paused */
	this->pause_timer = 0;
	
	finished = 1;
       }
      break;
     case DVDNAV_NOP:
       {
	/* Nothing */
       }
      break;
     case DVDNAV_STILL_FRAME:
     {

       /* OK, So xine no-longer accepts BUF_VIDEO_FILLs, find out
        * how else we provide the hint
        */
       dvdnav_still_event_t *still_event =
         (dvdnav_still_event_t*)(block);
       buf->type = BUF_CONTROL_NOP;
       finished = 1;
	
       /* Xine's method of doing still-frames */
       if (this->pause_timer == 0) {
         dprint("dvd:input_dvdnav.c:Stillframe! (pause time = 0x%02x)\n",
                 still_event->length);
         this->pause_timer = still_event->length;
         this->pause_end_time = time(NULL) + this->pause_timer;
         this->pause_counter = 0;
         break;
       }

       if(this->pause_timer == 0xff) {
         this->pause_counter++;
         xine_usec_sleep(100000);
         break;
       }
       if ( (this->pause_timer != 0xFF) && 
           (time(NULL) >= this->pause_end_time) ){
         this->pause_timer = 0;
         this->pause_end_time = 0;
         dvdnav_still_skip(this->dvdnav);
         break;
       }
       if(this->pause_timer) {
         this->pause_counter++;
         dprint("dvd:input_dvdnav.c:Stillframe! (pause_timer = 0x%02x) counter=%d\n",
                 still_event->length, this->pause_counter);
         xine_usec_sleep(100000);
         break;
       }
     }
     break;
     case DVDNAV_SPU_STREAM_CHANGE:
       {
	dvdnav_spu_stream_change_event_t *stream_event = 
	  (dvdnav_spu_stream_change_event_t*) (block);
        buf->content = block;
        buf->type = BUF_CONTROL_SPU_CHANNEL;
        buf->decoder_info[0] = stream_event->physical_wide;
	buf->decoder_info[1] = stream_event->physical_letterbox;
	buf->decoder_info[2] = stream_event->physical_pan_scan;
	dprint("SPU stream wide %d, letterbox %d, pan&scan %d\n",
	  stream_event->physical_wide,
	  stream_event->physical_letterbox,
	  stream_event->physical_pan_scan);
	finished = 1;
       }
      break;
     case DVDNAV_AUDIO_STREAM_CHANGE:
       {
	dvdnav_audio_stream_change_event_t *stream_event = 
	 (dvdnav_audio_stream_change_event_t*) (block);
        buf->content = block;
        buf->type = BUF_CONTROL_AUDIO_CHANNEL;
        buf->decoder_info[0] = stream_event->physical;
	dprint("AUDIO stream %d\n", stream_event->physical);
	finished = 1;
       }
      break;
     case DVDNAV_HIGHLIGHT:
       {
        xine_dvdnav_send_button_update(this, 0);
       }
      break;
     case DVDNAV_VTS_CHANGE:
       {
	int aspect, permission;
	
	dprint("VTS change\n");

	/* Check for video aspect change and scaling permissions */
	aspect = dvdnav_get_video_aspect(this->dvdnav);
	permission = dvdnav_get_video_scale_permission(this->dvdnav);

	buf->type = BUF_VIDEO_MPEG;
	buf->decoder_flags = BUF_FLAG_SPECIAL;
	buf->decoder_info[1] = BUF_SPECIAL_ASPECT;
	buf->decoder_info[2] = aspect;
	buf->decoder_info[3] = permission;
	finished = 1;
       }
      break;
     case DVDNAV_CELL_CHANGE:
       {
	 xine_ui_event_t uevent;

	 /* Tell Xine to update the UI */
	 uevent.event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
	 uevent.data = NULL;
	 xine_send_event(this->xine, &uevent.event);
	 
	 update_title_display(this);
       }
      break;
     case DVDNAV_SEEK_DONE:
       {
	dprint("Seek done\n");
        /* FIXME: This should send a message to clear all currently displaying subtitle. */
       }
      break;
     case DVDNAV_HOP_CHANNEL:
       {
       flush_buffers(this);
       break;
       }
     case DVDNAV_NAV_PACKET:
       {
	buf->content = block;
	buf->type = BUF_DEMUX_BLOCK;
	finished = 1;
       }
      break;
     case DVDNAV_SPU_CLUT_CHANGE:
       {
	buf->content = block;
	buf->type = BUF_SPU_CLUT;
	finished = 1;
       }
      break;
     case DVDNAV_STOP:
       {
	if (buf->mem != block) dvdnav_free_cache_block(this->dvdnav, block);
	buf->free_buffer(buf);
	/* return NULL to indicate end of stream */
	return NULL;
       }
     default:
      dprint("FIXME: Unknown event (%i)\n", event);
      break;
    }
  }
 
  if (block != buf->mem) {
    /* we have received a buffer from the libdvdnav cache, store all
     * necessary values to reconstruct xine's buffer and modify it according to
     * our needs. */
    pthread_mutex_lock(&this->buf_mutex);
    if (this->mem_stack < 1024) {
      this->mem[this->mem_stack++] = buf->mem;
      this->free_buffer = buf->free_buffer;
      this->source = buf->source;
      buf->mem = block;
      buf->free_buffer = dvdnav_plugin_free_buffer;
      buf->source = this;
    } else {
      /* the stack for storing the memory chunks from xine is full, we cannot
       * modify the buffer, because we would not be able to reconstruct it.
       * Therefore we copy the data and give the buffer back. */
      dprint("too many buffers issued, memory stack exceeded\n");
      memcpy(buf->mem, block, 2048);
      dvdnav_free_cache_block(this->dvdnav, block);
      buf->content = buf->mem;
    }
    pthread_mutex_unlock(&this->buf_mutex);
  }
  return buf;
}

static off_t dvdnav_plugin_read (input_plugin_t *this_gen, char *ch_buf, off_t len) {
/*  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen; */

  /* FIXME: Implement somehow */

  return 0;
}
  
static off_t dvdnav_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;
 
  trace_print("Called\n");

  if(!this || !this->dvdnav) {
    return -1;
  }
 
  return dvdnav_sector_search(this->dvdnav, offset / DVD_BLOCK_SIZE , origin) * DVD_BLOCK_SIZE;

  return -1;
}

static off_t dvdnav_plugin_get_current_pos (input_plugin_t *this_gen){
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;
  uint32_t pos=0;
  uint32_t length=1;
  dvdnav_status_t result;
  trace_print("Called\n");

  if(!this || !this->dvdnav) {
    return 0;
  }
  result = dvdnav_get_position(this->dvdnav, &pos, &length);
  return (off_t)pos * (off_t)2048;
}

static off_t dvdnav_plugin_get_length (input_plugin_t *this_gen) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;
  uint32_t pos=0;
  uint32_t length=1;
  dvdnav_status_t result;
 
  trace_print("Called\n");

  if(!this || !this->dvdnav) {
    return 0;
  }

  result = dvdnav_get_position(this->dvdnav, &pos, &length);
  return (off_t)length * (off_t)2048;
}

static uint32_t dvdnav_plugin_get_blocksize (input_plugin_t *this_gen) {
  trace_print("Called\n");

  return DVD_BLOCK_SIZE;
}

static mrl_t **dvdnav_plugin_get_dir (input_plugin_t *this_gen, 
				    char *filename, int *nFiles) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;

  trace_print("Called\n");
  if (filename) { *nFiles = 0; return NULL; }

  dvdnav_build_mrl_list((dvdnav_input_plugin_t *) this_gen);
  *nFiles = this->num_mrls;
  return this->mrls;
}

static int dvdnav_umount_media(char *device)
{
  char *argv[10];
  int i;
  pid_t pid;
  int status;
  argv[0]="umount";
  argv[1]=device;
  argv[2]=0;
  pid=fork();
  if (pid == 0) {
    i= execv("/bin/umount", argv);
    exit(127);
  }
  do {
    if(waitpid(pid, &status, 0) == -1) {
      if (errno != EINTR)
	return -1;
    } 
    else {
      return WEXITSTATUS(status);
    }
  } while(1);
  
  return -1;
} 


static int dvdnav_plugin_eject_media (input_plugin_t *this_gen) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t *) this_gen;
  int   ret, status;
  int   fd;

  /* printf("dvd:Eject Device %s current device %s opened=%d handle=%p trying...\n",this->dvd_device, this->current_dvd_device, this->opened, this->dvdnav); */
  dvdnav_plugin_close (this_gen) ;
  ret=dvdnav_umount_media(this->current_dvd_device);
  /**********
        printf ("umount result: %s\n", 
                  strerror(errno));  
   ***********/
  if ((fd = open (this->current_dvd_device, O_RDONLY|O_NONBLOCK)) > -1) {

#if defined (__linux__)
    if((status = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
        if((ret = ioctl(fd, CDROMCLOSETRAY)) != 0) {
#ifdef LOG_DVD_EJECT
          dprint ("CDROMCLOSETRAY failed: %s\n", 
                  strerror(errno));  
#endif
        }
        break;
      case CDS_DISC_OK:
        if((ret = ioctl(fd, CDROMEJECT)) != 0) {
#ifdef LOG_DVD_EJECT
          dprint ("CDROMEJECT failed: %s\n", strerror(errno));  
#endif
        }
        break;
      }
    }
    else {
#ifdef LOG_DVD_EJECT
      dprint ("CDROM_DRIVE_STATUS failed: %s\n", 
              strerror(errno));
#endif
      close(fd);
      return 0;
    }
#elif defined (__NetBSD__) || defined (__OpenBSD__) || defined (__FreeBSD__)

    if (ioctl(fd, CDIOCALLOW) == -1) {
      perror("ioctl(cdromallow)");
    } else {
      if (ioctl(fd, CDIOCEJECT) == -1) {
        perror("ioctl(cdromeject)");
      }
    }

#endif

    close(fd);
  } else {
    dprint("Device %s failed to open during eject calls\n",this->current_dvd_device);
  }
  return 1;
}

static char* dvdnav_plugin_get_mrl (input_plugin_t *this_gen) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;
  
  trace_print("Called\n");

  return this->mrl;
}

static char *dvdnav_plugin_get_description (input_plugin_t *this_gen) {
  trace_print("Called\n");

  return "DVD Navigator";
}

static char *dvdnav_plugin_get_identifier (input_plugin_t *this_gen) {
  trace_print("Called\n");

  return "DVD";
}

static void flush_buffers(dvdnav_input_plugin_t *this) {
  /* Small hack for still menus with audio. Thanks to
   * the Captain for doing this in d5d. The changes are necessary to
   * stop some audio problems (esp. with R2 'Dalekmania').
   */

   if (this->xine->audio_fifo)
    this->xine->audio_fifo->clear (this->xine->audio_fifo); 
   
   if (this->xine->video_fifo)
    this->xine->video_fifo->clear (this->xine->video_fifo); 


   if (this->xine->cur_audio_decoder_plugin)
    this->xine->cur_audio_decoder_plugin->reset(this->xine->cur_audio_decoder_plugin);
   if (this->xine->cur_video_decoder_plugin)
    this->xine->cur_video_decoder_plugin->flush(this->xine->cur_video_decoder_plugin);
}

static void xine_dvdnav_send_button_update(dvdnav_input_plugin_t *this, int mode) {
  int button;
  spu_button_t spu_button;
  xine_spu_event_t spu_event;
  dvdnav_get_current_highlight(this->dvdnav, &button);
  if (button == this->buttonN && (mode ==0) ) return;
  this->buttonN = button; /* Avoid duplicate sending of button info */
  dprint("sending_button_update button=%d mode=%d\n", button, mode);
  /* Do we want to show or hide the button? */
  /* libspudec will control hiding */
  spu_event.event.type = XINE_EVENT_SPU_BUTTON;
  spu_event.data = &spu_button;
  spu_button.show = mode + 1; /* mode=0 select, 1 activate. */
  spu_button.buttonN  = button;
  xine_send_event(this->xine, &spu_event.event);
}

static void dvdnav_event_listener (void *this_gen, xine_event_t *event) {

  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t *) this_gen; 

  if(!this->dvdnav) {
    return;
  }

  switch(event->type) {
   case XINE_EVENT_INPUT_MENU1:
    dvdnav_menu_call(this->dvdnav, DVD_MENU_Root);
    break;
   case XINE_EVENT_INPUT_MENU2:
    dvdnav_menu_call(this->dvdnav, DVD_MENU_Title);
    break;
   case XINE_EVENT_INPUT_MENU3:
    dvdnav_menu_call(this->dvdnav, DVD_MENU_Audio);
    break;
   case XINE_EVENT_INPUT_NEXT:
    dvdnav_next_pg_search(this->dvdnav);
    break;
   case XINE_EVENT_INPUT_PREVIOUS:
    dvdnav_prev_pg_search(this->dvdnav);
    break;
  case XINE_EVENT_INPUT_ANGLE_NEXT: 
    {
     int num = 0, current = 0;
     dvdnav_get_angle_info(this->dvdnav, &current, &num);

     if(num != 0) {
       current ++;
       if(current > num)
	current = 1;
     }
     dvdnav_angle_change(this->dvdnav, current);
     dprint("Changing to angle %i\n", current);

     update_title_display(this);
    }			
   break;
  case XINE_EVENT_INPUT_ANGLE_PREVIOUS: 
    {
     int num = 0, current = 0;
     dvdnav_get_angle_info(this->dvdnav, &current, &num);

     if(num != 0) {
       current --;
       if(current <= 0)
	current = num;
     }
     dvdnav_angle_change(this->dvdnav, current);
     dprint("Changing to angle %i\n", current);

     update_title_display(this);
    }			
   break;
   case XINE_EVENT_INPUT_SELECT:
   {
      xine_dvdnav_send_button_update(this, 1);
      dvdnav_button_activate(this->dvdnav);
   }
   break;
   case XINE_EVENT_MOUSE_BUTTON: 
   {
     xine_input_event_t *input_event = (xine_input_event_t*) event;      
     xine_dvdnav_send_button_update(this, 1);
     dvdnav_mouse_activate(this->dvdnav, input_event->x, 
			    input_event->y);
   }
    break;
   case XINE_EVENT_INPUT_BUTTON_FORCE:  /* For libspudec to feedback forced button select from NAV PCI packets. */
   {
     xine_spu_event_t    *spu_event = (xine_spu_event_t *) event;
     spu_button_t        *but = spu_event->data;
     fprintf(stderr, "xine_dvd:BUTTON_FORCE %d\n", but->buttonN); 
     dvdnav_button_select(this->dvdnav, but->buttonN);
   }
    break;
   case XINE_EVENT_MOUSE_MOVE: 
     {
      xine_input_event_t *input_event = (xine_input_event_t*) event;      
      /* printf("Mouse move (x,y) = (%i,%i)\n", input_event->x,
	     input_event->y); */
      dvdnav_mouse_select(this->dvdnav, input_event->x, input_event->y);
      xine_dvdnav_send_button_update(this, 0);
     }
    break;
   case XINE_EVENT_INPUT_UP:
    dvdnav_upper_button_select(this->dvdnav);
    xine_dvdnav_send_button_update(this, 0);
    break;
   case XINE_EVENT_INPUT_DOWN:
    dvdnav_lower_button_select(this->dvdnav);
    xine_dvdnav_send_button_update(this, 0);
    break;
   case XINE_EVENT_INPUT_LEFT:
    dvdnav_left_button_select(this->dvdnav);
    xine_dvdnav_send_button_update(this, 0);
    break;
   case XINE_EVENT_INPUT_RIGHT:
    dvdnav_right_button_select(this->dvdnav);
    xine_dvdnav_send_button_update(this, 0);
    break;
  }
   
  return;
}

static int dvdnav_plugin_get_optional_data (input_plugin_t *this_gen, 
					    void *data, int data_type) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t *) this_gen; 
  
  switch(data_type) {

  case INPUT_OPTIONAL_DATA_AUDIOLANG: {
    uint16_t lang;
    int8_t   channel;
    
    /* Be paranoid */
    if(this && this->xine && this->dvdnav) {

      if(!(dvdnav_is_domain_vts(this->dvdnav))) {
	sprintf(data, "%s", "nav");
	goto __audio_success;
      }
      
      channel = (int8_t) xine_get_audio_channel(this->xine);
      /*  printf("********* AUDIO CHANNEL = %d\n", channel); */
      channel = dvdnav_get_audio_logical_stream(this->dvdnav, channel);
      if(channel != -1) {
	lang = dvdnav_audio_stream_to_lang(this->dvdnav, channel);
	
	if(lang != 0xffff) {
	  sprintf(data, " %c%c", lang >> 8, lang & 0xff);
	} 
	else {
	  sprintf(data, "%3i", xine_get_audio_channel(this->xine));
	}
      } 
      else {
	channel = xine_get_audio_channel(this->xine);
	sprintf(data, "%3i", channel);
      }
      
    __audio_success:
      /*  printf("********** RETURNING '%s'\n", (char *)data); */
      return INPUT_OPTIONAL_SUCCESS;
    } 
    return INPUT_OPTIONAL_UNSUPPORTED;
  }
  break;


  case INPUT_OPTIONAL_DATA_SPULANG: {
    uint16_t lang;
    int8_t channel;
    
    /* Be paranoid */
    if(this && this->xine && this->dvdnav) {

      if(!(dvdnav_is_domain_vts(this->dvdnav))) {
	sprintf(data, "%3s", "off");
	goto __spu_success;
      }

      channel = (int8_t) xine_get_spu_channel(this->xine);
      /*  printf("********* SPU CHANNEL = %i\n", channel); */
      if(channel == -1)
	channel = dvdnav_get_spu_logical_stream(this->dvdnav, this->xine->spu_channel);
      else
	channel = dvdnav_get_spu_logical_stream(this->dvdnav, channel);
      
      if(channel != -1) {
	lang = dvdnav_spu_stream_to_lang(this->dvdnav, channel);

	if(lang != 0xffff) {
	  sprintf(data, " %c%c", lang >> 8, lang & 0xff);
	} 
	else {
	  sprintf(data, "%3i", xine_get_spu_channel(this->xine));
	}
      } 
      else {
	channel = xine_get_spu_channel(this->xine);
	if(channel == -1)
	  sprintf(data, "%3s", "off");
	else
	  sprintf(data, "%3i", channel);
      }
      
    __spu_success:
      /*  printf("********** RETURNING '%s'\n", (char *)data); */
      return INPUT_OPTIONAL_SUCCESS;
    }
    return INPUT_OPTIONAL_UNSUPPORTED;
  }
  break;
  
  }
  
  return INPUT_OPTIONAL_UNSUPPORTED;
}

static char **dvdnav_plugin_get_autoplay_list (input_plugin_t *this_gen, 
					       int *nFiles) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t *) this_gen;
  dvdnav_status_t res;
  int titles, i;
  trace_print("get_autoplay_list entered\n"); 
  /* Close the plugin is opened */
  if(this->opened) {
    dvdnav_close(this->dvdnav);
    this->opened = 0;
  }

  /* rebuild thie MRL browser list */
  dvdnav_build_mrl_list(this);

  /* Use default path */
  res = dvdnav_open(&(this->dvdnav), this->current_dvd_device);
  if(res == DVDNAV_STATUS_ERR) {
    return NULL;
  }

  this->opened = 1;
   
  /* Return a list of all titles */
  snprintf (&(filelist[0][0]), MAX_STR_LEN, "dvd://");
  filelist2[0] = &(filelist[0][0]);

  dvdnav_get_number_of_titles(this->dvdnav, &titles);
  for(i=1; i<=titles; i++) {
    snprintf (&(filelist[i][0]), MAX_STR_LEN, "dvd://:%i", i);
    filelist2[i] = &(filelist[i][0]);
  }
  *nFiles=titles+1;
  filelist2[*nFiles] = NULL;
  dprint("get_autoplay_list exiting opened=%d dvdnav=%p\n",this->opened, this->dvdnav); 

  return filelist2;
}

void dvdnav_plugin_dispose(input_plugin_t *this_gen) {
  dvdnav_input_plugin_t *this = (dvdnav_input_plugin_t*)this_gen;
  pthread_mutex_destroy(&this->buf_mutex);
  free(this->mrls); this->mrls = NULL;
}

#ifdef	__sun
/* 
 * Check the environment, if we're running under sun's
 * vold/rmmount control.
 */
static void
check_solaris_vold_device(dvdnav_input_plugin_t *this)
{
  char *volume_device;
  char *volume_name;
  char *volume_action;
  char *device;
  struct stat stb;

  if ((volume_device = getenv("VOLUME_DEVICE")) != NULL &&
      (volume_name   = getenv("VOLUME_NAME"))   != NULL &&
      (volume_action = getenv("VOLUME_ACTION")) != NULL &&
      strcmp(volume_action, "insert") == 0) {

    device = malloc(strlen(volume_device) + strlen(volume_name) + 2);
    if (device == NULL)
      return;
    sprintf(device, "%s/%s", volume_device, volume_name);
    if (stat(device, &stb) != 0 || !S_ISCHR(stb.st_mode)) {
      free(device);
      return;
    }
    this->dvd_device = device;
  }
}
#endif

input_plugin_t *init_input_plugin (int iface, xine_t *xine) {
  dvdnav_input_plugin_t *this;
  config_values_t *config = xine->config;

  trace_print("Called\n");

  switch (iface) {
  case 8:
    this = (dvdnav_input_plugin_t *) malloc (sizeof (dvdnav_input_plugin_t));

    this->input_plugin.interface_version  = INPUT_PLUGIN_IFACE_VERSION;
    this->input_plugin.get_capabilities   = dvdnav_plugin_get_capabilities;
    this->input_plugin.open               = dvdnav_plugin_open;
    this->input_plugin.read               = dvdnav_plugin_read;
    this->input_plugin.read_block         = dvdnav_plugin_read_block;
    this->input_plugin.seek               = dvdnav_plugin_seek;
    this->input_plugin.get_current_pos    = dvdnav_plugin_get_current_pos;
    this->input_plugin.get_length         = dvdnav_plugin_get_length;
    this->input_plugin.get_blocksize      = dvdnav_plugin_get_blocksize;
    this->input_plugin.get_dir            = dvdnav_plugin_get_dir;
    this->input_plugin.eject_media        = dvdnav_plugin_eject_media;
    this->input_plugin.get_mrl            = dvdnav_plugin_get_mrl;
    this->input_plugin.stop               = dvdnav_plugin_stop;
    this->input_plugin.close              = dvdnav_plugin_close;
    this->input_plugin.get_description    = dvdnav_plugin_get_description;
    this->input_plugin.get_identifier     = dvdnav_plugin_get_identifier;
    this->input_plugin.get_autoplay_list  = dvdnav_plugin_get_autoplay_list;
    this->input_plugin.get_optional_data  = dvdnav_plugin_get_optional_data;
    this->input_plugin.is_branch_possible = NULL;
    this->input_plugin.dispose		  = dvdnav_plugin_dispose;

    this->config                 = config;
    this->xine                   = xine;
    this->dvdnav                 = NULL;
    this->opened                 = 0;
    this->buttonN                = 0;
    this->dvd_name[0]            = 0;
    this->dvd_name_length        = 0;
    this->mrls                   = NULL;
    this->num_mrls               = 0;
    
    pthread_mutex_init(&this->buf_mutex, NULL);
    this->mem_stack              = 0;
    
    xine_register_event_listener(this->xine, dvdnav_event_listener, this);
    this->dvd_device = config->register_string(config,
					       "input.dvd_device",
					       DVD_PATH,
					       "device used for dvd drive",
					       NULL,
					       device_change_cb, (void *)this);
    this->current_dvd_device = this->dvd_device;

    config->register_num(config, "input.dvd_region",
		      	 1,
			 "Region that DVD player claims "
			 "to be (1 -> 8)",
			 "This only needs to be changed "
			 "if your DVD jumps to a screen "
			 "complaining about region code ",
			 region_changed_cb,
			 this);
    config->register_string(config, "input.dvdnav_language",
			    "en",
			    "The default language for dvd",
			    "The dvdnav plugin tries to use this "
			    "language as a default. This must be a"
			    "two character ISO country code.",
			    language_changed_cb, this);
    config->register_bool(config, "input.dvdnav_use_readahead",
			  1,
			  "Do we use read-ahead caching?",
			  "This "
			  "may lead to jerky playback on low-end "
			  "machines.",
			  read_ahead_cb, this);
    
#ifdef __sun
    check_solaris_vold_device(this);
#endif

    return (input_plugin_t *) this;
    break;
  default:
    fprintf(stderr,
	    "DVD Navigator input plugin doesn't support plugin API version %d.\n"
	    "PLUGIN DISABLED.\n"
	    "This means there's a version mismatch between xine and this input"
	    "plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }
}

/*
 * $Log: input_dvd.c,v $
 * Revision 1.53  2002/08/08 17:49:21  richwareham
 * First stage of DVD plugin -> dvdnav conversion
 *
 */
