/* 
 * Copyright (C) 2000-2002 the xine project, 
 *                         Rich Wareham <richwareham@users.sourceforge.net>
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
 * $Id: input_dvd.c,v 1.120 2002/11/23 11:09:29 f1rmb Exp $
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
#include <stddef.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>

#include <sys/mount.h>
#include <sys/wait.h>

#include <sys/poll.h>
#include <sys/ioctl.h>

#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD__)
#include <sys/dvdio.h>
#include <sys/cdio.h> /* CDIOCALLOW etc... */
#elif defined(HAVE_LINUX_CDROM_H)
#include <linux/cdrom.h>
#elif defined(HAVE_SYS_CDIO_H)
#include <sys/cdio.h>
#else
#warning "This might not compile due to missing cdrom ioctls"
#endif

/* DVDNAV includes */
#ifdef HAVE_DVDNAV
#  include <dvdnav/dvdnav.h>
#else
#  include "dvdnav.h"
#endif

/* libdvdread includes */
#include "nav_read.h"

/* Xine includes */
#include "xineutils.h"
#include "buffer.h"
#include "xine_internal.h"

/* Print debug messages? */
/* #define INPUT_DEBUG */

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
#define RDVD_PATH ""
#else
#define DVD_PATH "/dev/dvd"
#define RDVD_PATH "/dev/rdvd"
#endif 

/* Some misc. defines */
#define DVD_BLOCK_SIZE 2048
#ifndef BUF_DEMUX_BLOCK
#define BUF_DEMUX_BLOCK 0x05000000
#endif
#define VIDEO_FILL_THROTTLE 5

/* Debugging macros */
#ifdef __GNUC__
# ifdef INPUT_DEBUG_TRACE
#  define trace_print(s, args...) printf("input_dvd: " __func__ ": " s, ##args);
# else
#  define trace_print(s, args...) /* Nothing */
# endif
#else
# ifdef INPUT_DEBUG_TRACE
#  define trace_print(s, ...) printf("input_dvd: " __func__ ": " s, __VA_ARGS_);
# else
#  define trace_print(s, ...) /* Nothing */
# endif
#endif

/* Globals */
extern int errno;

/* Array to hold MRLs returned by get_autoplay_list */
#define MAX_DIR_ENTRIES 1250
#define MAX_STR_LEN     255  

typedef struct {
  input_plugin_t    input_plugin; /* Parent input plugin type        */

  xine_stream_t    *stream;
  xine_event_queue_t *event_queue;
  int               pause_timer;  /* Cell still-time timer            */
  int               pause_counter;
  time_t	    pause_end_time;
  int32_t           buttonN;
  int               typed_buttonN;/* for XINE_EVENT_INPUT_NUMBER_* */

  /* Flags */
  int               opened;       /* 1 if the DVD device is already open */
  int               seekable;     /* are we seekable? */
  
  /* Xine specific variables */
  char             *current_dvd_device; /* DVD device currently open */
  char             *mrl;          /* Current MRL                     */
  int               mode;
  dvdnav_t         *dvdnav;       /* Handle for libdvdnav            */
  char              dvd_name[128];
  size_t            dvd_name_length;                   
/*
  xine_mrl_t           **mrls;
  int               num_mrls;
*/
  char              filelist[MAX_DIR_ENTRIES][MAX_STR_LEN];
  char              ui_title[MAX_STR_LEN + 1];
  
  /* special buffer handling for libdvdnav caching */
  pthread_mutex_t   buf_mutex;
  void             *source;
  void            (*free_buffer)(buf_element_t *);
  int               mem_stack;
  unsigned char    *mem[1024];
} dvd_input_plugin_t;

typedef struct {

  input_class_t       input_class;

  xine_t             *xine;
  config_values_t    *config;       /* Pointer to XineRC config file   */  

  int                 mrls_allocated_entries;
  xine_mrl_t        **mrls;
  char		     *dvd_device;	  /* Default DVD device		     */

  dvd_input_plugin_t *ip;

  int32_t             read_ahead_flag;
  int32_t             language;
  int32_t             region;

  char               *filelist2[MAX_DIR_ENTRIES];

} dvd_input_class_t;

static void dvd_handle_events(dvd_input_plugin_t *this);
static void flush_buffers(dvd_input_plugin_t *this);
static void xine_dvd_send_button_update(dvd_input_plugin_t *this, int mode);

/* Callback on device name change */
static void device_change_cb(void *data, xine_cfg_entry_t *cfg) {
  dvd_input_class_t *class = (dvd_input_class_t *) data;
  
  class->dvd_device = cfg->str_value;
  printf("input_dvd.c:device_change_cb:dvd_device=%s\n",class->dvd_device); 
}

static uint32_t dvd_plugin_get_capabilities (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
  
  trace_print("Called\n");

  return INPUT_CAP_BLOCK |
#if CAN_SEEK
    (this->seekable ? INPUT_CAP_SEEKABLE : 0) |
#endif
    INPUT_CAP_AUDIOLANG | INPUT_CAP_SPULANG | INPUT_CAP_CHAPTERS; 
}

void read_ahead_cb(void *this_gen, xine_cfg_entry_t *entry) {
  dvd_input_class_t *class = (dvd_input_class_t*)this_gen;

  if(!class)
   return;

  class->read_ahead_flag = entry->num_value;

  if(class->ip) {
    dvd_input_plugin_t *this = class->ip;

    dvdnav_set_readahead_flag(this->dvdnav, entry->num_value);
  }
}
 
void region_changed_cb (void *this_gen, xine_cfg_entry_t *entry) {
  dvd_input_class_t *class = (dvd_input_class_t*)this_gen;

  if(!class)
   return;

  class->region = entry->num_value;

  if(class->ip && ((entry->num_value >= 1) && (entry->num_value <= 8))) {
    dvd_input_plugin_t *this = class->ip;

    dvdnav_set_region_mask(this->dvdnav, 1<<(entry->num_value-1));
  }
}

void language_changed_cb(void *this_gen, xine_cfg_entry_t *entry) {
  dvd_input_class_t *class = (dvd_input_class_t*)this_gen;

  if(!class)
   return;

  class->language = entry->str_value[0] << 8 | entry->str_value[1];
  
  if(class->ip) {
    dvd_input_plugin_t *this = class->ip;
    
    dvdnav_menu_language_select(this->dvdnav, entry->str_value);
    dvdnav_audio_language_select(this->dvdnav, entry->str_value);
    dvdnav_spu_language_select(this->dvdnav, entry->str_value);
  }
}
 
void update_title_display(dvd_input_plugin_t *this) {
  xine_event_t uevent;
  xine_ui_data_t data;
  int tt=-1, pr=-1;
  size_t ui_str_length=0;

  if(!this || !(this->stream)) 
   return;
  
  /* Set title/chapter display */

  dvdnav_current_title_info(this->dvdnav, &tt, &pr);
  
  if(tt != -1) { 
    int num_angle = 0, cur_angle = 0;
    /* no menu here */    
    /* Reflect angle info if appropriate */
    dvdnav_get_angle_info(this->dvdnav, &cur_angle, &num_angle);
    if(num_angle > 1) {
      snprintf(this->ui_title, MAX_STR_LEN,
               "Title %i, Chapter %i, Angle %i of %i",
               tt,pr,cur_angle, num_angle); 
    } else {
      snprintf(this->ui_title, MAX_STR_LEN, 
	       "Title %i, Chapter %i",
	       tt,pr);
    }
  } else {
    strcpy(this->ui_title, "DVD Navigator: Menu");
  }
  ui_str_length = strlen(this->ui_title);
  
  if (this->dvd_name[0] != 0 && (ui_str_length + this->dvd_name_length < MAX_STR_LEN)) {
      snprintf(this->ui_title+ui_str_length, MAX_STR_LEN - ui_str_length, 
	       ", %s",
	       &this->dvd_name[0]);
  }
#ifdef INPUT_DEBUG
  printf("input_dvd: Changing title to read '%s'\n", this->ui_title);
#endif
  uevent.type = XINE_EVENT_UI_SET_TITLE;
  uevent.stream = this->stream;
  uevent.data = &data;
  uevent.data_length = sizeof(data);;
  memcpy(data.str, this->ui_title, strlen(this->ui_title) + 1);
  data.str_len = strlen(this->ui_title) + 1;
  xine_event_send(this->stream, &uevent);
}

static void dvd_plugin_stop (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*) this_gen;
  if (this->dvdnav) {
    dvdnav_still_skip(this->dvdnav);
  }
}

static void dvd_plugin_dispose (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
  
  trace_print("Called\n");
  
  xine_event_dispose_queue (this->event_queue);
  if(this->opened || this->dvdnav) 
    dvdnav_close(this->dvdnav);
  this->dvdnav = NULL;
  this->opened = 0;
  this->dvd_name[0]            = 0;
  this->dvd_name_length        = 0;
}


/* Align pointer |p| to alignment |align| */
#define	PTR_ALIGN(p, align)	((void*) (((long)(p) + (align) - 1) & ~((align)-1)) )


static void dvd_build_mrl_list(dvd_input_plugin_t *this) {
  int num_titles, *num_parts;
/* FIXME */
  return;
#if 0
  /* skip DVD if already open */
  if (this->opened) return;
  if (this->class->mrls) {
    free(this->class->mrls);
    this->class->mrls = NULL;
    this->class->num_mrls = 0;
  }

  if (dvdnav_open(&(this->dvdnav), 
		  this->dvd_device) == DVDNAV_STATUS_ERR) {
    return;
  }
  
  this->current_dvd_device = this->dvd_device;
  this->opened = 1;

  dvdnav_get_number_of_titles(this->dvdnav, &num_titles);
  if ((num_parts = (int *) calloc(num_titles, sizeof(int)))) {
    struct xine_mrl_align_s {
      char dummy;
      xine_mrl_t mrl;
    };
    int xine_mrl_alignment = offsetof(struct xine_mrl_align_s, mrl);
    int num_mrls = 1, i;
    /* for each title, count the number of parts */
    for (i = 1; i <= num_titles; i++) {
      num_parts[i-1] = 0;
      /* dvdnav_title_play(this->dvdnav, i); */
      dvdnav_get_number_of_parts(this->dvdnav, i, &num_parts[i-1]);
      num_mrls += num_parts[i-1]; /* num_mrls = total number of programs */
    }

    /* allocate enough memory for:
     * - a list of pointers to mrls       sizeof(xine_mrl_t *)     * (num_mrls+1)
     * - possible alignment of the mrl array 
     * - an array of mrl structures       sizeof(xine_mrl_t)       * num_mrls
     * - enough chars for every filename  sizeof(char)*25     * num_mrls
     *   - "dvd:/000000.000000\0" = 25 chars
     */
    if ((this->mrls = (xine_mrl_t **) malloc(sizeof(xine_mrl_t *) + num_mrls *
	(sizeof(xine_mrl_t*) + sizeof(xine_mrl_t) + 25*sizeof(char)) +
	xine_mrl_alignment))) {
    
      /* the first mrl struct comes after the pointer list */
      xine_mrl_t *mrl = PTR_ALIGN(&this->mrls[num_mrls+1], xine_mrl_alignment);

      /* the chars for filenames come after the mrl structs */
      char *name = (char *) &mrl[num_mrls];
      int pos = 0, j;
      this->num_mrls = num_mrls;

      for (i = 1; i <= num_titles; i++) {
	for (j = (i == 1 ? 0 : 1); j <= num_parts[i-1]; j++) {
	  this->class->mrls[pos++] = mrl;
	  mrl->origin = NULL;
	  mrl->mrl = name;
	  mrl->link = NULL;
	  mrl->type = mrl_dvd;
	  mrl->size = 0;
	  snprintf(name, 25, (j == 0) ? "dvd:/" :
		             (j == 1) ? "dvd:/%d" :
		                        "dvd:/%d.%d", i, j);
	  name = &name[25];
	  mrl++;
	}
      }
      this->class->mrls[pos] = NULL; /* terminate list */
    }
    free(num_parts);
  }
#endif

  /* Reset the VM so that we don't break anything */
/* FIXME: Is this really needed */
  dvdnav_reset(this->dvdnav);
}

static void dvd_plugin_free_buffer(buf_element_t *buf) {
  dvd_input_plugin_t *this = buf->source;
  
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

static buf_element_t *dvd_plugin_read_block (input_plugin_t *this_gen, 
						fifo_buffer_t *fifo, off_t nlen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
  buf_element_t      *buf;
  dvdnav_status_t     result;
  int                 event, len;
  int                 finished = 0;
  unsigned char      *block;

  if(fifo == NULL) {
    printf("input_dvd: values of \\beta will give rise to dom!\n");
    return NULL;
  }

  /* Read buffer */
  buf = fifo->buffer_pool_alloc (fifo);
  block = buf->mem;

  while(!finished) {
    dvd_handle_events(this);
  
    if (block != buf->mem) {
      /* if we already have a dvdnav cache block, give it back first */
      dvdnav_free_cache_block(this->dvdnav, block);
      block = buf->mem;
    }
    result = dvdnav_get_next_cache_block (this->dvdnav, &block, &event, &len);
    if(result == DVDNAV_STATUS_ERR) {
      printf("input_dvd: Error getting next block from DVD (%s)\n",
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
	
	/* we got a block, so we might be seekable here */
	this->seekable = 1;
	
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
       
       /* stills are not seekable */
       this->seekable = 0;
	
       /* Xine's method of doing still-frames */
       if (this->pause_timer == 0) {
#ifdef INPUT_DEBUG
         printf("input_dvd: Stillframe! (pause time = 0x%02x)\n",
                 still_event->length);
#endif
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
#ifdef INPUT_DEBUG
         printf("input_dvd: Stillframe! (pause_timer = 0x%02x) counter=%d\n",
                 still_event->length, this->pause_counter);
#endif
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
#ifdef INPUT_DEBUG
	printf("input_dvd: SPU stream wide %d, letterbox %d, pan&scan %d\n",
	  stream_event->physical_wide,
	  stream_event->physical_letterbox,
	  stream_event->physical_pan_scan);
#endif
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
#ifdef INPUT_DEBUG
	printf("input_dvd: AUDIO stream %d\n", stream_event->physical);
#endif
	finished = 1;
       }
      break;
     case DVDNAV_HIGHLIGHT:
       {
        xine_dvd_send_button_update(this, 0);
       }
      break;
     case DVDNAV_VTS_CHANGE:
       {
	int aspect, permission;
#ifdef INPUT_DEBUG
	printf("input_dvd: VTS change\n");
#endif
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
         xine_event_t event;

	 /* Tell Xine to update the UI */
	 event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
	 event.stream = this->stream;
	 event.data = NULL;
	 event.data_length = 0;
	 xine_event_send(this->stream, &event);
	 
	 update_title_display(this);
       }
      break;
     case DVDNAV_SEEK_DONE:
       {
#ifdef INPUT_DEBUG
	printf("input_dvd: Seek done\n");
#endif
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
	buf->type = BUF_SPU_DVD;
	buf->decoder_flags |= BUF_FLAG_SPECIAL;
	buf->decoder_info[1] = BUF_SPECIAL_SPU_DVD_SUBTYPE;
	buf->decoder_info[2] = SPU_DVD_SUBTYPE_CLUT;
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
      printf("input_dvd: FIXME: Unknown event (%i)\n", event);
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
      buf->free_buffer = dvd_plugin_free_buffer;
      buf->source = this;
    } else {
      /* the stack for storing the memory chunks from xine is full, we cannot
       * modify the buffer, because we would not be able to reconstruct it.
       * Therefore we copy the data and give the buffer back. */
      printf("input_dvd: too many buffers issued, memory stack exceeded\n");
      memcpy(buf->mem, block, 2048);
      dvdnav_free_cache_block(this->dvdnav, block);
      buf->content = buf->mem;
    }
    pthread_mutex_unlock(&this->buf_mutex);
  }
  return buf;
}

static off_t dvd_plugin_read (input_plugin_t *this_gen, char *ch_buf, off_t len) {
/*  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen; */

  /* FIXME: Tricking the demux_mpeg_block plugin */
  ch_buf[0] = 0;
  ch_buf[1] = 0;
  ch_buf[2] = 0x01;
  ch_buf[3] = 0xba;
  return 1;
}
  
static off_t dvd_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
 
  trace_print("Called\n");

  if(!this || !this->dvdnav) {
    return -1;
  }
 
  return dvdnav_sector_search(this->dvdnav, offset / DVD_BLOCK_SIZE , origin) * DVD_BLOCK_SIZE;

  return -1;
}

static off_t dvd_plugin_get_current_pos (input_plugin_t *this_gen){
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
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

static off_t dvd_plugin_get_length (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
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

static uint32_t dvd_plugin_get_blocksize (input_plugin_t *this_gen) {
  trace_print("Called\n");

  return DVD_BLOCK_SIZE;
}

static int dvd_umount_media(char *device)
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


static char* dvd_plugin_get_mrl (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
  
  trace_print("Called\n");

  return this->mrl;
}

static void flush_buffers(dvd_input_plugin_t *this) {
  /*
   * This code comes from xine_demux_flush_engine
   */
  xine_stream_t *stream = this->stream;  
  buf_element_t *buf;
  
  stream->video_fifo->clear(stream->video_fifo);

  if( stream->audio_fifo )
    stream->audio_fifo->clear(stream->audio_fifo);
  
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type            = BUF_CONTROL_RESET_DECODER;
  stream->video_fifo->put (stream->video_fifo, buf);

  if(stream->audio_fifo) {
    buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
    buf->type            = BUF_CONTROL_RESET_DECODER;
    stream->audio_fifo->put (stream->audio_fifo, buf);
  }

  if (stream->video_out) {
    stream->video_out->flush(stream->video_out);
  }

  if (stream->audio_out) {
    stream->audio_out->flush(stream->audio_out);
  }
}

static void xine_dvd_send_button_update(dvd_input_plugin_t *this, int mode) {
  int32_t button;
  int32_t show;

  if (!this || !(this->stream) || !(this->stream->spu_decoder_plugin) ) {
    return;
  }
  dvdnav_get_current_highlight(this->dvdnav, &button);
  if (button == this->buttonN && (mode ==0) ) return;
  this->buttonN = button; /* Avoid duplicate sending of button info */
#ifdef INPUT_DEBUG
  printf("input_dvd: sending_button_update button=%d mode=%d\n", button, mode);
#endif
  /* Do we want to show or hide the button? */
  /* libspudec will control hiding */
  show = mode + 1; /* mode=0 select, 1 activate. */
  this->stream->spu_decoder_plugin->set_button (this->stream->spu_decoder_plugin, button, mode + 1);
}

static void dvd_handle_events(dvd_input_plugin_t *this) {

  dvd_input_class_t  *class = (dvd_input_class_t*)this->input_plugin.input_class;
  config_values_t  *config = class->config;       /* Pointer to XineRC config file   */  
  xine_event_t *event;

  while ((event = xine_event_get(this->event_queue))) {
  
    if(!this->dvdnav) {
      xine_event_free(event);
      return;
    }

    switch(event->type) {
    case XINE_EVENT_INPUT_MENU2:
      printf("input_dvd: MENU2 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Title);
      break;
    case XINE_EVENT_INPUT_MENU1:
    case XINE_EVENT_INPUT_MENU3:
      printf("input_dvd: MENU3 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Root);
      break;
    case XINE_EVENT_INPUT_MENU4:
      printf("input_dvd: MENU4 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Subpicture);
      break;
    case XINE_EVENT_INPUT_MENU5:
      printf("input_dvd: MENU5 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Audio);
      break;
    case XINE_EVENT_INPUT_MENU6:
      printf("input_dvd: MENU6 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Angle);
      break;
    case XINE_EVENT_INPUT_MENU7:
      printf("input_dvd: MENU7 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Part);
      break;
    case XINE_EVENT_INPUT_NEXT:
      {
        cfg_entry_t* entry = config->lookup_entry(config, "input.dvd_skip_behaviour");
	int title = 0, part = 0;
	switch (entry->num_value) {
	case 0: /* skip by program */
	  dvdnav_next_pg_search(this->dvdnav);
	  break;
	case 1: /* skip by part */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part))
	    dvdnav_part_play(this->dvdnav, title, ++part);
	  break;
	case 2: /* skip by title */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part))
	    dvdnav_part_play(this->dvdnav, ++title, 1);
	  break;
	}
      }
      break;
    case XINE_EVENT_INPUT_PREVIOUS:
      {
        cfg_entry_t *entry = config->lookup_entry(config, "input.dvd_skip_behaviour");
	int title = 0, part = 0;
	switch (entry->num_value) {
	case 0: /* skip by program */
	  dvdnav_prev_pg_search(this->dvdnav);
	  break;
	case 1: /* skip by part */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part))
	    dvdnav_part_play(this->dvdnav, title, --part);
	  break;
	case 2: /* skip by title */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part))
	    dvdnav_part_play(this->dvdnav, --title, 1);
	  break;
	}
      }
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
#ifdef INPUT_DEBUG
        printf("input_dvd: Changing to angle %i\n", current);
#endif
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
#ifdef INPUT_DEBUG
        printf("input_dvd: Changing to angle %i\n", current);
#endif
        update_title_display(this);
      }
      break;
    case XINE_EVENT_INPUT_SELECT:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
          xine_dvd_send_button_update(this, 1);
          dvdnav_button_activate(this->dvdnav, &nav_pci);
        }
      }
      break;
    case XINE_EVENT_INPUT_MOUSE_BUTTON: 
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
	  xine_input_data_t *input = event->data;
          xine_dvd_send_button_update(this, 1);
          dvdnav_mouse_activate(this->dvdnav, &nav_pci, input->x, input->y);
        }
      }
      break;
    case XINE_EVENT_INPUT_BUTTON_FORCE:  /* For libspudec to feedback forced button select from NAV PCI packets. */
      {
        spu_button_t *but = event->data;
#ifdef INPUT_DEBUG
        printf("input_dvd: BUTTON_FORCE %d\n", but->buttonN);
#endif
        dvdnav_button_select(this->dvdnav, &but->nav_pci, but->buttonN);
      }
      break;
    case XINE_EVENT_INPUT_MOUSE_MOVE: 
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
	  xine_input_data_t *input = event->data;
	  /* printf("input_dvd: Mouse move (x,y) = (%i,%i)\n", input->x, input->y); */
	  dvdnav_mouse_select(this->dvdnav, &nav_pci, input->x, input->y);
          xine_dvd_send_button_update(this, 0);
        }
      }
      break;
    case XINE_EVENT_INPUT_UP:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
          dvdnav_upper_button_select(this->dvdnav, &nav_pci);
          xine_dvd_send_button_update(this, 0);
        }
        break;
      }
    case XINE_EVENT_INPUT_DOWN:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
          dvdnav_lower_button_select(this->dvdnav, &nav_pci);
          xine_dvd_send_button_update(this, 0);
        }
        break;
      }
    case XINE_EVENT_INPUT_LEFT:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
          dvdnav_left_button_select(this->dvdnav, &nav_pci);
          xine_dvd_send_button_update(this, 0);
        }
        break;
      }
    case XINE_EVENT_INPUT_RIGHT:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
          dvdnav_right_button_select(this->dvdnav, &nav_pci);
          xine_dvd_send_button_update(this, 0);
        }
        break;
      }
    case XINE_EVENT_INPUT_NUMBER_9:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_8:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_7:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_6:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_5:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_4:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_3:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_2:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_1:
      this->typed_buttonN++;
    case XINE_EVENT_INPUT_NUMBER_0:
      { 
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin) {
          return;
        }
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
          dvdnav_button_select(this->dvdnav, &nav_pci, this->typed_buttonN);
          xine_dvd_send_button_update(this, 1);
          dvdnav_button_activate(this->dvdnav, &nav_pci);
          this->typed_buttonN = 0;
        }
        break;
      }
    case XINE_EVENT_INPUT_NUMBER_10_ADD:
      this->typed_buttonN += 10;
    }
    
    xine_event_free(event);
  }
  return;
}

static int dvd_plugin_get_optional_data (input_plugin_t *this_gen, 
					    void *data, int data_type) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t *) this_gen; 
  
  switch(data_type) {

  case INPUT_OPTIONAL_DATA_AUDIOLANG: {
    uint16_t lang;
    int8_t   channel;
    
    /* Be paranoid */
    if(this && this->stream && this->dvdnav) {

      if(!(dvdnav_is_domain_vts(this->dvdnav))) {
	sprintf(data, "%s", "menu");
	goto __audio_success;
      }
      
      channel = (int8_t) xine_get_audio_channel(this->stream);
      /*  printf("input_dvd: ********* AUDIO CHANNEL = %d\n", channel); */
      channel = dvdnav_get_audio_logical_stream(this->dvdnav, channel);
      if(channel != -1) {
	lang = dvdnav_audio_stream_to_lang(this->dvdnav, channel);
	
	if(lang != 0xffff) {
	  sprintf(data, " %c%c", lang >> 8, lang & 0xff);
	} 
	else {
	  sprintf(data, "%3i", xine_get_audio_channel(this->stream));
	}
      } 
      else {
	channel = xine_get_audio_channel(this->stream);
	sprintf(data, "%3i", channel);
      }
      
    __audio_success:
      /*  printf("input_dvd: ********** RETURNING '%s'\n", (char *)data); */
      return INPUT_OPTIONAL_SUCCESS;
    } 
    return INPUT_OPTIONAL_UNSUPPORTED;
  }
  break;


  case INPUT_OPTIONAL_DATA_SPULANG: {
    uint16_t lang;
    int8_t channel;
    
    /* Be paranoid */
    if(this && this->stream && this->dvdnav) {

      if(!(dvdnav_is_domain_vts(this->dvdnav))) {
	sprintf(data, "%s", "menu");
	goto __spu_success;
      }

      channel = (int8_t) xine_get_spu_channel(this->stream);
      /*  printf("input_dvd: ********* SPU CHANNEL = %i\n", channel); */
      if(channel == -1)
	channel = dvdnav_get_spu_logical_stream(this->dvdnav, this->stream->spu_channel);
      else
	channel = dvdnav_get_spu_logical_stream(this->dvdnav, channel);
      
      if(channel != -1) {
	lang = dvdnav_spu_stream_to_lang(this->dvdnav, channel);

	if(lang != 0xffff) {
	  sprintf(data, " %c%c", lang >> 8, lang & 0xff);
	} 
	else {
	  sprintf(data, "%3i", xine_get_spu_channel(this->stream));
	}
      } 
      else {
	channel = xine_get_spu_channel(this->stream);
	if(channel == -1)
	  sprintf(data, "%s", "none");
	else
	  sprintf(data, "%3i", channel);
      }
      
    __spu_success:
      /*  printf("input_dvd: ********** RETURNING '%s'\n", (char *)data); */
      return INPUT_OPTIONAL_SUCCESS;
    }
    return INPUT_OPTIONAL_UNSUPPORTED;
  }
  break;
  
  }
  
  return INPUT_OPTIONAL_UNSUPPORTED;
}

#ifdef	__sun
/* 
 * Check the environment, if we're running under sun's
 * vold/rmmount control.
 */
static void
check_solaris_vold_device(dvd_input_class_t *this)
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

/* dvdnav CLASS functions */

/*
 * Opens the DVD plugin. The MRL takes the following form:
 *
 * dvd:/[vts[/program]]
 *
 * e.g.
 *   dvd:/                    - Play (navigate)
 *   dvd:/1                   - Play Title 1
 *   dvd:/1.3                 - Play Title 1, program 3
 */
static input_plugin_t *open_plugin (input_class_t *class_gen, xine_stream_t *stream, const char *data) {
  dvd_input_plugin_t    *this;
  dvd_input_class_t     *class = (dvd_input_class_t*)class_gen;
  char                  *locator;
  int                    dot_point;
  dvdnav_status_t        ret;
  char                  *intended_dvd_device;
  xine_cfg_entry_t      region_entry, lang_entry, cache_entry;
  config_values_t       *config = stream->xine->config;

  printf("input_dvd.c: open_plugin called.\n");

  this = (dvd_input_plugin_t *) xine_xmalloc (sizeof (dvd_input_plugin_t));
  if (this == NULL) {
    printf("input_dvd.c: xine_xmalloc failed!!!! You have run out of memory\n");
    assert(0);
  }

  this->input_plugin.get_capabilities   = dvd_plugin_get_capabilities;
  this->input_plugin.read               = dvd_plugin_read;
  this->input_plugin.read_block         = dvd_plugin_read_block;
  this->input_plugin.seek               = dvd_plugin_seek;
  this->input_plugin.get_current_pos    = dvd_plugin_get_current_pos;
  this->input_plugin.get_length         = dvd_plugin_get_length;
  this->input_plugin.get_blocksize      = dvd_plugin_get_blocksize;
  this->input_plugin.get_mrl            = dvd_plugin_get_mrl;
  this->input_plugin.get_optional_data  = dvd_plugin_get_optional_data;
  this->input_plugin.dispose            = dvd_plugin_dispose;
  this->input_plugin.input_class        = class_gen;

  this->stream = stream;
  this->dvdnav                 = NULL;
  this->opened                 = 0;
  this->seekable               = 0;
  this->buttonN                = 0;
  this->typed_buttonN          = 0;
  this->dvd_name[0]            = 0;
  this->dvd_name_length        = 0;
  this->mrl                    = NULL;
/*
  this->mrls                   = NULL;
  this->num_mrls               = 0;
    if (raw_device) xine_setenv("DVDCSS_RAW_DEVICE", raw_device, 0);
*/

  pthread_mutex_init(&this->buf_mutex, NULL);
  this->mem_stack              = 0;
  trace_print("Called\n");
  this->event_queue = xine_event_new_queue (this->stream);
  /* printf("input_dvd: open1: dvdnav=%p opened=%d\n",this->dvdnav, this->opened); */
  printf("data=%p\n",data);
  if (data) printf("data=%s\n",data); 
  this->mrl = strdup(data);
  this->pause_timer            = 0;
  this->dvd_name[0]            = 0;
  this->dvd_name_length        = 0;

  /* Check we can handle this MRL */
  if (!strncasecmp (this->mrl, "dvd:/",5)) {
    locator = &this->mrl[5];
    while (*locator == '/') locator++;
  } else {
    return 0;
  }

  /* Attempt to parse MRL */
  dot_point=0;
  while((locator[dot_point] != '\0') && (locator[dot_point] != '.')) {
    dot_point++;
  }

  if(locator[dot_point] == '.') {
    this->mode = MODE_TITLE; 
  } else {
    this->mode = MODE_NAVIGATE;
  }

  printf("input_dvd.c:open_plugin:dvd_device=%s\n",class->dvd_device); 
  intended_dvd_device=class->dvd_device;
  
  if(this->opened) {
    if ( intended_dvd_device==this->current_dvd_device ) {
      /* Already open, so skip opening */
    } else {
      /* Changing DVD device */
      dvdnav_close(this->dvdnav);
      this->dvdnav = NULL;
      this->opened = 0; 
      ret = dvdnav_open(&this->dvdnav, intended_dvd_device);
      if(ret == DVDNAV_STATUS_ERR) {
        printf("input_dvd: Error opening DVD device\n");
        return 0;
      }
      this->opened=1;
      this->current_dvd_device=intended_dvd_device;
    }
  } else {
    ret = dvdnav_open(&this->dvdnav, intended_dvd_device);
    if(ret == DVDNAV_STATUS_ERR) {
      printf("input_dvd: Error opening DVD device\n");
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
          printf("input_dvd: DVD Title: ");
          for(i=25; i < 73; i++ ) {
            if((data[i] == 0)) break;
            if((data[i] > 32) && (data[i] < 127)) {
              printf("%c", data[i]);
            } else {
              printf(" ");
            }
          }
          strncpy(&this->dvd_name[0], &data[25], 48);
          /* printf("input_dvd: TITLE:%s\n",&this->dvd_name[0]); */
          this->dvd_name[48]=0;
          this->dvd_name_length=strlen(&this->dvd_name[0]);
          printf("\ninput_dvd: DVD Serial Number: ");
          for(i=73; i < 89; i++ ) {
            if((data[i] == 0)) break;
            if((data[i] > 32) && (data[i] < 127)) {
              printf("%c", data[i]);
            } else {
              printf(" ");
            } 
          }
          printf("\ninput_dvd: DVD Title (Alternative): ");
          for(i=89; i < 128; i++ ) {
            if((data[i] == 0)) break;
            if((data[i] > 32) && (data[i] < 127)) {
              printf("%c", data[i]);
            } else {
              printf(" ");
            }
          }
          printf("\n");
        } else {
          printf("input_dvd: Can't read name block. Probably not a DVD-ROM device.\n");
        }
      } else {
        printf("input_dvd: Can't seek to block %u\n", 32 );
      }
    } else {
      printf("input_dvd: NAME OPEN FAILED\n");
    }
  }

  /* config callbacks may react now */
  class->ip = this;

  /* Set region code */
  if (xine_config_lookup_entry (this->stream->xine, "input.dvd_region", 
				&region_entry)) 
    region_changed_cb (class, &region_entry);
  
  /* Set languages */
  if (xine_config_lookup_entry (this->stream->xine, "input.dvd_language",
				&lang_entry)) 
    language_changed_cb (class, &lang_entry);
  
  /* Set cache usage */
  if (xine_config_lookup_entry(this->stream->xine, "input.dvd_use_readahead",
			       &cache_entry))
    read_ahead_cb(class, &cache_entry);
  
  if(this->mode == MODE_TITLE) {
    int tt, i, pr, found;
    int titles;
    
    /* A program and/or VTS was specified */
    locator += dot_point + 1;

    if(locator[0] == '\0') {
      /* Empty specifier */
      printf("input_dvd: Incorrect MRL format.\n");
      dvdnav_close(this->dvdnav);
      this->dvdnav = NULL;
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
      printf("input_dvd: Title %i is out of range (1 to %i).\n", tt,
	      titles);
      dvdnav_close(this->dvdnav);
      this->dvdnav = NULL;
      return 0;
    }

    /* If there was a program specified, get that too. */
    pr = -1;
    if(found != -1) {
      pr = strtol(locator+found+1, NULL,10);
    }
#ifdef INPUT_DEBUG
    printf("input_dvd: Jumping to VTS >%i<, prog >%i<\n", tt, pr);
#endif
    if(pr != -1) {
      dvdnav_part_play(this->dvdnav, tt, pr);
    } else {
      dvdnav_title_play(this->dvdnav, tt);
    }
  }
#ifdef INPUT_DEBUG
  printf("input_dvd: DVD device successfully opened.\n");
#endif
  return &this->input_plugin;
}

static char *dvd_class_get_description (input_class_t *this_gen) {
  trace_print("Called\n");

  return "DVD Navigator";
}

static char *dvd_class_get_identifier (input_class_t *this_gen) {
  trace_print("Called\n");

  return "DVD";
}

/* FIXME: adapt to new api. */
#if 0
static xine_mrl_t **dvd_class_get_dir (input_class_t *this_gen, 
						       const char *filename, int *nFiles) {
  dvd_input_class_t *this = (dvd_input_class_t*)this_gen;

  trace_print("Called\n");
  if (filename) { *nFiles = 0; return NULL; }

/*
  dvd_build_mrl_list(this);
  *nFiles = this->num_mrls;
  return this->mrls;
*/
  *nFiles = 0;
   return NULL;
}
#endif

static char ** dvd_class_get_autoplay_list (input_class_t *this_gen, 
					    int *num_files) {

  dvd_input_class_t *this = (dvd_input_class_t *) this_gen;
  int i;
  trace_print("get_autoplay_list entered\n"); 

#if 0
  /* rebuild thie MRL browser list */
  dvd_build_mrl_list(this);
  *nFiles = this->num_mrls;

  i = 0;
  for(i=0;(i<this->num_mrls) && (i<MAX_DIR_ENTRIES);i++) {
    snprintf (&(this->filelist[i][0]), MAX_STR_LEN, this->mrls[i]->mrl);
    this->filelist2[i] = &(this->filelist[i][0]);
  }
  this->filelist2[*nFiles] = NULL;

#endif

  this->filelist2[0] = "dvd:/";
  this->filelist2[1] = NULL;
  *num_files = 1;

  return this->filelist2;
}

void dvd_class_dispose(input_class_t *this_gen) {
  dvd_input_class_t *this = (dvd_input_class_t*)this_gen;
/* FIXME: get mutex working again
  pthread_mutex_destroy(&this->buf_mutex);
  free(this->mrl);  this->mrl  = NULL;
*/
  free(this->mrls); this->mrls = NULL;
  free(this);
}

static int dvd_class_eject_media (input_class_t *this_gen) {
  dvd_input_class_t *this = (dvd_input_class_t *) this_gen;
  int   ret, status;
  int   fd;

  /* printf("input_dvd: Eject Device %s current device %s opened=%d handle=%p trying...\n",this->dvd_device, this->current_dvd_device, this->opened, this->dvdnav); */
  ret=dvd_umount_media(this->dvd_device);
  /**********
        printf("ipnut_dvd: umount result: %s\n", 
                  strerror(errno));  
   ***********/
  if ((fd = open (this->dvd_device, O_RDONLY|O_NONBLOCK)) > -1) {

#if defined (__linux__)
    if((status = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT)) > 0) {
      switch(status) {
      case CDS_TRAY_OPEN:
        if((ret = ioctl(fd, CDROMCLOSETRAY)) != 0) {
#ifdef LOG_DVD_EJECT
          printf("input_dvd: CDROMCLOSETRAY failed: %s\n", 
                  strerror(errno));  
#endif
        }
        break;
      case CDS_DISC_OK:
        if((ret = ioctl(fd, CDROMEJECT)) != 0) {
#ifdef LOG_DVD_EJECT
          printf("input_dvd: CDROMEJECT failed: %s\n", strerror(errno));  
#endif
        }
        break;
      }
    }
    else {
#ifdef LOG_DVD_EJECT
      printf("input_dvd: CDROM_DRIVE_STATUS failed: %s\n", 
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
    printf("input_dvd: Device %s failed to open during eject calls\n",this->dvd_device);
  }
  return 1;
}

static void *init_class (xine_t *xine, void *data) {
  dvd_input_class_t   *this;
  config_values_t     *config = xine->config;
  void                *dvdcss;
  static char         *skip_modes[] = {"skip program", "skip part", "skip title", NULL};

  trace_print("Called\n");
#ifdef INPUT_DEBUG
  printf("input_dvd.c: init_class called.\n");
  printf("input_dvd.c: config = %p\n", config);
#endif

  this = (dvd_input_class_t *) malloc (sizeof (dvd_input_class_t));
  
  this->input_class.open_plugin        = open_plugin;
  this->input_class.get_identifier     = dvd_class_get_identifier;
  this->input_class.get_description    = dvd_class_get_description;
/*
  this->input_class.get_dir            = dvd_class_get_dir;
*/
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = dvd_class_get_autoplay_list;
  this->input_class.dispose            = dvd_class_dispose;
  this->input_class.eject_media        = dvd_class_eject_media;
  
  this->config                         = config;
  this->mrls                           = NULL;

  this->ip                             = NULL;

/*  this->num_mrls               = 0; */
  
/*  pthread_mutex_init(&this->buf_mutex, NULL);
  this->mem_stack              = 0;
*/
  
  this->dvd_device = config->register_string(config,
					     "input.dvd_device",
					     DVD_PATH,
					     "device used for dvd drive",
					     NULL,
					     0, device_change_cb, (void *)this);
  
  if ((dvdcss = dlopen("libdvdcss.so.2", RTLD_LAZY)) != NULL) {
    /* we have found libdvdcss, enable the specific config options */
#ifndef HAVE_DVDNAV
    char *raw_device;
#endif
    static char *decrypt_modes[] = { "key", "disc", "title", NULL };
    int mode;
    
#ifndef HAVE_DVDNAV
    /* only our local copy of libdvdread supports raw device reads,
     * so we don't provide this option, when we are using a shared version
     * of libdvdnav/libdvdread */
    raw_device = config->register_string(config, "input.dvd_raw_device",
					 RDVD_PATH, "raw device set up for dvd access",
					 NULL, 10, NULL, NULL);
    if (raw_device) xine_setenv("DVDCSS_RAW_DEVICE", raw_device, 0);
#endif
    
    mode = config->register_enum(config, "input.css_decryption_method", 0,
				 decrypt_modes, "the css decryption method libdvdcss should use",
				 NULL, 10, NULL, NULL);
    xine_setenv("DVDCSS_METHOD", decrypt_modes[mode], 0);
    
    dlclose(dvdcss);
  }
  
  config->register_num(config, "input.dvd_region",
		       1,
		       "Region that DVD player claims "
		       "to be (1 -> 8)",
		       "This only needs to be changed "
		       "if your DVD jumps to a screen "
		       "complaining about region code ",
		       0, region_changed_cb, this);
  config->register_string(config, "input.dvd_language",
			  "en",
			  "The default language for dvd",
			  "The dvdnav plugin tries to use this "
			  "language as a default. This must be a"
			  "two character ISO country code.",
			  0, language_changed_cb, this);
  config->register_bool(config, "input.dvd_use_readahead",
			1,
			"Do we use read-ahead caching?",
			"This "
			"may lead to jerky playback on low-end "
			"machines.",
			10, read_ahead_cb, this);
  config->register_enum(config, "input.dvd_skip_behaviour", 0,
			skip_modes,
			"Skipping will work on this basis.",
			NULL, 10, NULL, NULL);

#ifdef __sun
  check_solaris_vold_device(this);
#endif
#ifdef INPUT_DEBUG
  printf("input_dvd.c: init_class finished.\n");
#endif
  return this;
}


/*
 * $Log: input_dvd.c,v $
 * Revision 1.120  2002/11/23 11:09:29  f1rmb
 * registering config entries at init_class time
 *
 * Revision 1.119  2002/11/22 16:23:58  mroi
 * do not play with the clock any more, we have dedicated flush functions for that now
 * (This should fix Daniels MP3 problems, since the end of one stream would
 * have adjusted the global clock thus affecting all other streams.)
 *
 * Revision 1.118  2002/11/20 11:57:42  mroi
 * engine modifications to allow post plugin layer:
 * * new public output interface xine_{audio,video}_port_t instead of
 *   xine_{ao,vo}_driver_t, old names kept as aliases for compatibility
 * * modified the engine to allow multiple streams per output
 * * renaming of some internal structures according to public changes
 * * moving SCR out of per-stream-metronom into a global metronom_clock_t
 *   residing in xine_t and therefore easily available to the output layer
 * * adapting all available plugins
 *   (note to external projects: the compiler will help you a lot, if a plugin
 *   compiles, it is adapted, because all changes add new parameters to some
 *   functions)
 * * bump up all interface versions because of xine_t and xine_stream_t changes
 *
 * Revision 1.117  2002/11/18 11:48:35  mroi
 * DVD input should now be initially unseekable
 *
 * Revision 1.116  2002/11/18 11:33:59  mroi
 * getting rid of obviously unused INPUT_CAP_VARIABLE_BITRATE
 * fix ejecting (works now)
 *
 * Revision 1.115  2002/11/17 16:23:38  mroi
 * cleanup: bring config entries back to life
 * introduce a seekable flag
 *
 * Revision 1.114  2002/11/15 00:20:32  miguelfreitas
 * cleaning up spu types. now avi subtitles may be enabled again.
 * (+ missed ffmpeg/dv patch)
 *
 * Revision 1.113  2002/11/03 23:03:31  siggi
 * some more release-related fixes...
 *
 * Revision 1.112  2002/11/02 15:13:01  mroi
 * don't display crap in UI panel, xine-ui expects a xine_ui_data_t and
 * I think this is right, so we provide one
 *
 * Revision 1.111  2002/11/02 03:13:44  f1rmb
 * Less verbosity.
 *
 * Revision 1.110  2002/11/01 17:51:57  mroi
 * be less strict with MRL syntax, people are used to ://
 *
 * Revision 1.109  2002/11/01 11:48:59  tmattern
 * Time for fast navigation now !
 *
 * Revision 1.108  2002/10/31 17:00:45  mroi
 * adapt input plugins to new MRL syntax
 * (mostly turning :// into :/)
 *
 * Revision 1.107  2002/10/27 20:07:39  mroi
 * less noise and register skip_behaviour (chapter skip keys work again)
 *
 * Revision 1.106  2002/10/26 22:50:52  guenter
 * timeouts for mms, send progress report events, introduce verbosity engine parameter (not implemented yet), document new plugin loader in changelog
 *
 * Revision 1.105  2002/10/26 20:15:21  mroi
 * first step in getting dvd events back
 *
 * Revision 1.104  2002/10/26 02:12:27  jcdutton
 * Remove assert(0), left over from testing.
 * dispose of event queue.
 *
 * Revision 1.103  2002/10/25 15:36:19  mroi
 * remove obviously obsolete INPUT_CAP_CLUT and INPUT_OPTIONAL_DATA_CLUT
 *
 * Revision 1.102  2002/10/24 15:06:55  jkeil
 * C99 version of macro definition with variable number of arguments added
 *
 * Revision 1.101  2002/10/24 13:52:57  jcdutton
 * Fix some log messages in audio_alsa_out.c
 * Fix input_dvd.c for new config file loading before init_class().
 *
 * Revision 1.100  2002/10/24 11:30:38  jcdutton
 * Further changes to DVD code.
 *
 * Revision 1.99  2002/10/23 20:26:34  guenter
 * final c++ -> c coding style fixes, libxine compiles now
 *
 * Revision 1.98  2002/10/23 11:59:52  jcdutton
 * Oops...will compile now.
 *
 * Revision 1.97  2002/10/23 11:44:31  jcdutton
 * input_dvd.c now listens for keyboard events from xine-ui.
 *
 * Revision 1.96  2002/10/23 10:14:08  jkeil
 * "dvd_device" device name moved from dvd_input_plugin_t -> dvd_input_class_t,
 * adapt the check_solaris_vold_device() function.
 *
 * Revision 1.95  2002/10/22 17:16:57  jkeil
 * Fix bad comment, and disable some piece of code to enable compilation on solaris
 *
 * Revision 1.94  2002/10/22 07:36:05  jcdutton
 * Update input_dvd.c to new api.
 * Plays DVDs now, but not menu buttons work yet.
 *
 * Revision 1.93  2002/10/14 15:47:16  guenter
 * introduction of xine_stream_t and async xine events - all still in developement
 *
 * Revision 1.92  2002/10/06 15:48:02  jkeil
 * Proper alignment is needed for the array of "xine_mrl_t" structures on SPARC.
 *
 * Revision 1.91  2002/10/02 15:56:51  mroi
 * - kill global variables
 * - remove some code that could never be reached (after return)
 *
 * Revision 1.90  2002/09/28 11:10:04  mroi
 * configurable skipping behaviour
 *
 * Revision 1.89  2002/09/22 14:29:40  mroi
 * API review part I
 * - bring our beloved xine_t * back (no more const there)
 * - remove const on some input plugin functions
 *   where the data changes with media (dvd, ...) changes
 *   and is therefore not const
 *
 * Revision 1.86  2002/09/18 10:03:07  jcdutton
 * Fix a seg fault.
 *
 * Revision 1.85  2002/09/18 06:42:23  jcdutton
 * Try to get xine-lib to compile.
 *
 * Revision 1.84  2002/09/18 04:20:09  jcdutton
 * Updating the DVD menu code to use better nav_pci information.
 * libspudec parses nav_pci info correctly.
 * libdvdnav does not parse nav_pci info at all.
 *
 * Revision 1.83  2002/09/17 07:53:59  jcdutton
 * Make input_dvd.c mrl playlist work again.
 *
 * Revision 1.82  2002/09/16 16:55:35  jcdutton
 * Start to get mrl working for DVD button.
 *
 * Revision 1.81  2002/09/16 16:13:56  jcdutton
 * Prevent a segfault when accessing the config.
 *
 * Revision 1.80  2002/09/15 14:05:37  mroi
 * be more distinct with UI info texts for
 * "no subtitles because user switched it off"
 * and
 * "no subtitles because none are available"
 *
 * Revision 1.79  2002/09/14 19:04:07  guenter
 * latest xine_config api changes as proposed by james
 *
 * Revision 1.78  2002/09/13 17:18:42  mroi
 * dvd playback should work again
 *
 * Revision 1.77  2002/09/06 18:13:10  mroi
 * introduce "const"
 * fix some input plugins that would not copy the mrl on open
 *
 * Revision 1.76  2002/09/05 22:18:54  mroi
 * remove plugin's private priority and interface members
 * adapt some more decoders
 *
 * Revision 1.75  2002/09/05 20:44:39  mroi
 * make all the plugin init functions static
 * (geez this was a job)
 *
 * Revision 1.74  2002/09/05 20:19:48  guenter
 * use xine_mrl_t instead of mrl_t in input plugins, implement more configfile functions
 *
 * Revision 1.73  2002/09/05 05:51:14  jcdutton
 * XV Video out at least loads now and we see the xine logo again.
 * The DVD plugin now loads, but audio and spu info is lost.
 * What happened to xine_get_spu_channel and xine_get_audio_channel?
 *
 * Revision 1.72  2002/09/04 23:31:08  guenter
 * merging in the new_api branch ... unfortunately video_out / vo_scale is broken now ... matthias/miguel: please fix it :-)
 *
 * Revision 1.71  2002/09/04 10:48:36  mroi
 * - handle numeric events for button selection (maybe this makes some
 *   dvd's easter eggs accesible)
 * - workaround current breakage in libdvdnav concerning mrl list building
 *
 * Revision 1.70  2002/09/03 07:51:34  jcdutton
 * Improve chapter selection functions.
 *
 * Revision 1.69  2002/09/02 12:25:49  jcdutton
 * This might slow things down a bit, but I need to do it to test a problem with DVD menus
 * not appearing.
 * I think the reason they are not appearing is that they are getting flushed too early.
 *
 * Revision 1.68  2002/09/02 03:21:38  jcdutton
 * Implement proper prev/next chapter.
 *
 * Revision 1.67  2002/08/31 02:48:13  jcdutton
 * Add a printf so we can tell if a user is using xine's libdvdnav or the one from
 * dvd.sf.net.
 * Add some "this->dvdnav = NULL;" after dvd_close()
 *
 * Revision 1.66  2002/08/30 11:14:44  mroi
 * make menu key output conform xine guidelines, improve compatibility with
 * older xine-ui versions by handling XINE_EVENT_INPUT_MENU1
 *
 * Revision 1.65  2002/08/29 04:32:12  jcdutton
 * Use more Fkeys to jump to different DVD menus.
 * We can now jump directly to Title, Root, Sub-Picture, Audio, Angle, PTT (Chapter) menus.
 *
 * Revision 1.64  2002/08/26 11:50:47  mroi
 * adapt to xine coding guidelines
 *
 * Revision 1.63  2002/08/21 23:38:48  komadori
 * fix portability problems
 *
 * Revision 1.62  2002/08/21 15:10:09  mroi
 * use raw devices only with our patched local copy of libdvdread
 *
 * Revision 1.61  2002/08/19 17:27:11  mroi
 * add config entries for raw device and css decryption method
 *
 * Revision 1.60  2002/08/13 16:04:27  jkeil
 * Solaris uses <sys/cdio.h> for CDROM/DVD-ROM ioctl, too.  Try to use autoconf
 * HAVE_headerfile macros...  (The xxxBSD part nees a bit work)
 *
 * Revision 1.59  2002/08/13 15:55:23  mroi
 * change error to warning
 *
 * Revision 1.58  2002/08/09 22:33:10  mroi
 * sorry, my raw device patch was not meant to be committed
 * It only works with a patched version of libdvdcss
 *
 * Revision 1.57  2002/08/09 22:13:08  mroi
 * make developers life easier: add possibility to use an existing  shared
 * version of libdvdnav
 *
 * Revision 1.56  2002/08/09 15:38:13  mroi
 * fix mrl parsing
 *
 * Revision 1.55  2002/08/09 13:50:17  heikos
 * seems to compile better this way :)
 *
 * Revision 1.54  2002/08/09 07:34:47  richwareham
 * More include fixes
 *
 * Revision 1.53  2002/08/08 17:49:21  richwareham
 * First stage of DVD plugin -> dvdnav conversion
 *
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_INPUT, 10, "DVD", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
