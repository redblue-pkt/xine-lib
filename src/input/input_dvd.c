/* 
 * Copyright (C) 2000-2003 the xine project, 
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
 * $Id: input_dvd.c,v 1.153 2003/04/22 23:30:29 tchamp Exp $
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

#ifndef _MSC_VER
#include <dirent.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif /* _MSC_VER */

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>

#ifndef _MSC_VER
#include <sys/mount.h>
#include <sys/wait.h>

#include <sys/poll.h>
#include <sys/ioctl.h>
#endif /* _MSC_VER */


#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__FreeBSD__)
#include <sys/dvdio.h>
#include <sys/cdio.h> /* CDIOCALLOW etc... */
#elif defined(HAVE_LINUX_CDROM_H)
#include <linux/cdrom.h>
#elif defined(HAVE_SYS_CDIO_H)
#include <sys/cdio.h>
#else

#ifdef WIN32
#include <io.h>                                                 /* read() */
#else
#warning "This might not compile due to missing cdrom ioctls"
#endif /* WIN32 */

#endif

/* DVDNAV includes */
#ifdef HAVE_DVDNAV
#ifndef _MSC_VER
#  include <dvdnav/dvdnav.h>
#else
#  include "dvdnav.h"
#endif /* _MSC_VER */
#else
#  define DVDNAV_COMPILE
#  include "dvdnav.h"
#endif

/* libdvdread includes */
#include "nav_read.h"

/* Xine includes */
#include "xineutils.h"
#include "buffer.h"
#include "xine_internal.h"
#include "media_helper.h"

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
#ifdef DVD_VIDEO_LB_LEN
#  define DVD_BLOCK_SIZE DVD_VIDEO_LB_LEN
#else
#  define DVD_BLOCK_SIZE 2048
#endif

/* Debugging macros */
#ifdef __GNUC__
# ifdef INPUT_DEBUG_TRACE
#  define trace_print(s, args...) printf("input_dvd: " __func__ ": " s, ##args);
# else
#  define trace_print(s, args...) /* Nothing */
# endif
#else
#  ifndef _MSC_VER
#    define trace_print(s, ...) /* Nothing */
#  else
#    define trace_print printf
#  endif /* _MSC_VER */
#endif

/* Array to hold MRLs returned by get_autoplay_list */
#define MAX_DIR_ENTRIES 1250
#define MAX_STR_LEN     255  

#if defined (__FreeBSD__)
# define off64_t off_t
# define lseek64 lseek
#endif

static const char *dvdnav_menu_table[] = {
  NULL,
  NULL,
  "Title",
  "Root",
  "Subpicture",
  "Audio",
  "Angle",
  "Part"
};

typedef struct {
  input_plugin_t    input_plugin; /* Parent input plugin type        */

  xine_stream_t    *stream;
  xine_event_queue_t *event_queue;
  
  int               pause_timer;  /* Cell still-time timer            */
  int               pause_counter;
  time_t	    pause_end_time;
  int64_t           pg_length;
  int64_t           pgc_length;
  int64_t           cell_start;
  int64_t           pg_start;
  int32_t           buttonN;
  int               typed_buttonN;/* for XINE_EVENT_INPUT_NUMBER_* */

  /* Flags */
  int               opened;       /* 1 if the DVD device is already open */
  int               seekable;     /* are we seekable? */
  
  /* xine specific variables */
  char             *current_dvd_device; /* DVD device currently open */
  char             *mrl;          /* Current MRL                     */
  int               mode;
  dvdnav_t         *dvdnav;       /* Handle for libdvdnav            */
  const char       *dvd_name;
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
  int               freeing;
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
  int32_t             seek_mode;
  int32_t             language;
  int32_t             region;

  char               *filelist2[MAX_DIR_ENTRIES];

} dvd_input_class_t;

static void dvd_handle_events(dvd_input_plugin_t *this);
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
 
void seek_mode_cb(void *this_gen, xine_cfg_entry_t *entry) {
  dvd_input_class_t *class = (dvd_input_class_t*)this_gen;

  if(!class)
   return;

  class->seek_mode = entry->num_value;

  if(class->ip) {
    dvd_input_plugin_t *this = class->ip;

    dvdnav_set_PGC_positioning_flag(this->dvdnav, !entry->num_value);
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
 
  if(tt >= 1) { 
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
  } else if (tt == 0 && dvdnav_menu_table[pr]) {
    snprintf(this->ui_title, MAX_STR_LEN,
             "DVD %s Menu",
             dvdnav_menu_table[pr]);
  } else {
    strcpy(this->ui_title, "DVD Menu");
  }
  ui_str_length = strlen(this->ui_title);
  
  if (this->dvd_name && this->dvd_name[0] &&
      (ui_str_length + strlen(this->dvd_name) < MAX_STR_LEN)) {
    snprintf(this->ui_title+ui_str_length, MAX_STR_LEN - ui_str_length, 
	     ", %s", this->dvd_name);
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
  
  if (this->event_queue)
    xine_event_dispose_queue (this->event_queue);
   
  if (this->dvdnav) {
    dvdnav_close(this->dvdnav);
    /* raise the freeing flag, so that the plugin will be freed as soon
     * as all buffers have returned to the libdvdnav read ahead cache */
    this->freeing = 1;
  } else {
    pthread_mutex_destroy(&this->buf_mutex);
    free(this);
  }
}


/* Align pointer |p| to alignment |align| */
#define	PTR_ALIGN(p, align)	((void*) (((long)(p) + (align) - 1) & ~((align)-1)) )


static void dvd_build_mrl_list(dvd_input_plugin_t *this) {
/* FIXME */
#if 0
  int num_titles, *num_parts;

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
#else
  return;
#endif
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
  if (this->freeing && !this->mem_stack) {
    /* all buffers returned, we can free the plugin now */
    pthread_mutex_destroy(&this->buf_mutex);
    free(this);
  }
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
      xine_message(this->stream, XINE_MSG_READ_ERROR,
                   dvdnav_err_to_string(this->dvdnav), NULL);
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
      break;
    case DVDNAV_STILL_FRAME:
      {
        dvdnav_still_event_t *still_event =
          (dvdnav_still_event_t*)block;
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
          xine_usec_sleep(50000);
          break;
        }
        if ((this->pause_timer != 0xff) && 
            (time(NULL) >= this->pause_end_time)) {
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
          xine_usec_sleep(50000);
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
      xine_dvd_send_button_update(this, 0);
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
	dvdnav_cell_change_event_t *cell_event = 
	 (dvdnav_cell_change_event_t*) (block);
        xine_event_t event;

	/* Tell xine to update the UI */
	event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
	event.stream = this->stream;
	event.data = NULL;
	event.data_length = 0;
	xine_event_send(this->stream, &event);
	
	update_title_display(this);
	
	this->pg_length  = cell_event->pg_length;
	this->pgc_length = cell_event->pgc_length;
	this->cell_start = cell_event->cell_start;
	this->pg_start   = cell_event->pg_start;
      }
      break;
    case DVDNAV_HOP_CHANNEL:
      xine_demux_flush_engine(this->stream);
      break;
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
    case DVDNAV_WAIT:
      {
	int buffers = this->stream->video_fifo->size(this->stream->video_fifo);
	if (this->stream->audio_fifo)
	  buffers += this->stream->audio_fifo->size(this->stream->audio_fifo);
	/* we wait until the fifos are empty, ... well, we allow one remaining buffer,
	 * because a flush might be in progress. */
	if (buffers <= 1)
	  dvdnav_wait_skip(this->dvdnav);
	else
	  xine_usec_sleep(50000);
      }
      break;
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
      memcpy(buf->mem, block, DVD_BLOCK_SIZE);
      dvdnav_free_cache_block(this->dvdnav, block);
      buf->content = buf->mem;
    }
    pthread_mutex_unlock(&this->buf_mutex);
  }
  
  if (this->pg_length && this->pgc_length) {
    int pos, length;
    dvdnav_get_position(this->dvdnav, &pos, &length);
    buf->extra_info->input_pos = pos * (off_t)DVD_BLOCK_SIZE;
    buf->extra_info->input_length = length * (off_t)DVD_BLOCK_SIZE;
    switch (((dvd_input_class_t *)this->input_plugin.input_class)->seek_mode) {
    case 0: /* PGC based seeking */
      buf->extra_info->total_time = this->pgc_length / 90;
      buf->extra_info->input_time = this->cell_start / 90;
      break;
    case 1: /* PG based seeking */
      buf->extra_info->total_time = this->pg_length  / 90;
      buf->extra_info->input_time = (this->cell_start - this->pg_start) / 90;
      break;
    }
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
  return (off_t)pos * (off_t)DVD_BLOCK_SIZE;
}

static off_t dvd_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
 
  trace_print("Called\n");

  if(!this || !this->dvdnav) {
    return -1;
  }
 
  dvdnav_sector_search(this->dvdnav, offset / DVD_BLOCK_SIZE , origin);
  return dvd_plugin_get_current_pos(this_gen);
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
  return (off_t)length * (off_t)DVD_BLOCK_SIZE;
}

static uint32_t dvd_plugin_get_blocksize (input_plugin_t *this_gen) {
  trace_print("Called\n");

  return DVD_BLOCK_SIZE;
}

static char* dvd_plugin_get_mrl (input_plugin_t *this_gen) {
  dvd_input_plugin_t *this = (dvd_input_plugin_t*)this_gen;
  
  trace_print("Called\n");

  return this->mrl;
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
    case XINE_EVENT_INPUT_MENU1:
      printf("input_dvd: MENU1 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Escape);
      break;
    case XINE_EVENT_INPUT_MENU2:
      printf("input_dvd: MENU2 key hit.\n");
      dvdnav_menu_call(this->dvdnav, DVD_MENU_Title);
      break;
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
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part) && title > 0)
	    dvdnav_part_play(this->dvdnav, title, ++part);
	  break;
	case 2: /* skip by title */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part) && title > 0)
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
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part) && title > 0)
	    dvdnav_part_play(this->dvdnav, title, --part);
	  break;
	case 2: /* skip by title */
	  if (dvdnav_current_title_info(this->dvdnav, &title, &part) && title > 0)
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
          if (dvdnav_button_activate(this->dvdnav, &nav_pci) == DVDNAV_STATUS_OK)
            xine_dvd_send_button_update(this, 1);
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
          if (dvdnav_mouse_activate(this->dvdnav, &nav_pci, input->x, input->y) == DVDNAV_STATUS_OK)
            xine_dvd_send_button_update(this, 1);
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
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
	  xine_input_data_t *input = event->data;
	  /* printf("input_dvd: Mouse move (x,y) = (%i,%i)\n", input->x, input->y); */
	  dvdnav_mouse_select(this->dvdnav, &nav_pci, input->x, input->y);
        }
      }
      break;
    case XINE_EVENT_INPUT_UP:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) )
          dvdnav_upper_button_select(this->dvdnav, &nav_pci);
        break;
      }
    case XINE_EVENT_INPUT_DOWN:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) )
          dvdnav_lower_button_select(this->dvdnav, &nav_pci);
        break;
      }
    case XINE_EVENT_INPUT_LEFT:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) )
          dvdnav_left_button_select(this->dvdnav, &nav_pci);
        break;
      }
    case XINE_EVENT_INPUT_RIGHT:
      {
        pci_t nav_pci;
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) )
          dvdnav_right_button_select(this->dvdnav, &nav_pci);
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
        if(!this->stream || !this->stream->spu_decoder_plugin)
          return;
        if (this->stream->spu_decoder_plugin->get_nav_pci(this->stream->spu_decoder_plugin, &nav_pci) ) {
	  if (dvdnav_button_select_and_activate(this->dvdnav, &nav_pci, this->typed_buttonN) == DVDNAV_STATUS_OK)
            xine_dvd_send_button_update(this, 1);
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
    int      channel = *((int *)data);
    int8_t   dvd_channel;
    
    /* Be paranoid */
    if(this && this->stream && this->dvdnav) {

      if(!(dvdnav_is_domain_vts(this->dvdnav))) {
	sprintf(data, "%s", "menu");
	if (channel <= 0)
	  return INPUT_OPTIONAL_SUCCESS;
	else
	  return INPUT_OPTIONAL_UNSUPPORTED;
      }
      
      if (channel == -1)
        dvd_channel = dvdnav_get_audio_logical_stream(this->dvdnav, this->stream->audio_channel_auto);
      else
        dvd_channel = dvdnav_get_audio_logical_stream(this->dvdnav, channel);

      if(dvd_channel != -1) {
	lang = dvdnav_audio_stream_to_lang(this->dvdnav, dvd_channel);
	
	if(lang != 0xffff)
	  sprintf(data, " %c%c", lang >> 8, lang & 0xff);
	else
	  sprintf(data, " %c%c", '?', '?');
	return INPUT_OPTIONAL_SUCCESS;
      } else {
        if (channel == -1) {
	  sprintf(data, "%s", "none");
	  return INPUT_OPTIONAL_SUCCESS;
	}
      }
    } 
    return INPUT_OPTIONAL_UNSUPPORTED;
  }
  break;


  case INPUT_OPTIONAL_DATA_SPULANG: {
    uint16_t lang;
    int      channel = *((int *)data);
    int8_t   dvd_channel;
    
    /* Be paranoid */
    if(this && this->stream && this->dvdnav) {

      if(!(dvdnav_is_domain_vts(this->dvdnav))) {
	sprintf(data, "%s", "menu");
	if (channel <= 0)
	  return INPUT_OPTIONAL_SUCCESS;
	else
	  return INPUT_OPTIONAL_UNSUPPORTED;
      }

      if(channel == -1)
	dvd_channel = dvdnav_get_spu_logical_stream(this->dvdnav, this->stream->spu_channel_auto);
      else
	dvd_channel = dvdnav_get_spu_logical_stream(this->dvdnav, channel);

      if(dvd_channel != -1) {
	lang = dvdnav_spu_stream_to_lang(this->dvdnav, dvd_channel);

	if(lang != 0xffff)
	  sprintf(data, " %c%c", lang >> 8, lang & 0xff);
	else
	  sprintf(data, " %c%c", '?', '?');
	return INPUT_OPTIONAL_SUCCESS;
      } else {
	if(channel == -1) {
	  sprintf(data, "%s", "none");
	  return INPUT_OPTIONAL_SUCCESS;
	}
      }
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

static int dvd_plugin_open (input_plugin_t *this_gen) {
  dvd_input_plugin_t    *this = (dvd_input_plugin_t*)this_gen;
  dvd_input_class_t     *class = (dvd_input_class_t*)this_gen->input_class;
  
  char                  *locator;
  int                    last_slash = 0;
  dvdnav_status_t        ret;
  char                  *intended_dvd_device;
  xine_event_t           event;
  static char           *handled_mrl = "dvd:/";
  xine_cfg_entry_t       region_entry, lang_entry, cache_entry;
  
  trace_print("Called\n");

  /* we already checked the "dvd:/" MRL above */
  locator = &this->mrl[strlen(handled_mrl)];
  while (*locator == '/') locator++;
  /* we skipped at least one slash, get it back */
  locator--;

  /* Attempt to parse MRL */
  last_slash = strlen(locator);
  while(last_slash && locator[last_slash] != '/') last_slash--;

  if(last_slash) {
    /* we have an alternative dvd_path */
    intended_dvd_device = locator;
    intended_dvd_device[last_slash] = '\0';
    locator += last_slash;
  }else{
    intended_dvd_device=class->dvd_device;
  }
  locator++;

  if(locator[0]) {
    this->mode = MODE_TITLE; 
  } else {
    this->mode = MODE_NAVIGATE;
  }

  if(this->opened) {
    if ( intended_dvd_device==this->current_dvd_device ) {
      /* Already open, so skip opening */
      dvdnav_reset(this->dvdnav);
    } else {
      /* Changing DVD device */
      dvdnav_close(this->dvdnav);
      this->dvdnav = NULL;
      this->opened = 0; 
      ret = dvdnav_open(&this->dvdnav, intended_dvd_device);
      if(ret == DVDNAV_STATUS_ERR) {
	if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG) 
	  printf("input_dvd: Error opening DVD device\n");
	xine_message (this->stream, XINE_MSG_READ_ERROR,
		      intended_dvd_device, NULL);
        return 0;
      }
      this->opened=1;
      this->current_dvd_device=intended_dvd_device;
    }
  } else {
    ret = dvdnav_open(&this->dvdnav, intended_dvd_device);
    if(ret == DVDNAV_STATUS_ERR) {
      if (this->stream->xine->verbosity >= XINE_VERBOSITY_LOG) 
	printf("input_dvd: Error opening DVD device\n");
      xine_message (this->stream, XINE_MSG_READ_ERROR,
		    intended_dvd_device, NULL);
      return 0;
    }
    this->opened=1;
    this->current_dvd_device=intended_dvd_device;
  }
  
  dvdnav_get_title_string(this->dvdnav, &this->dvd_name);
  
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
  
  /* Set seek mode */
  if (xine_config_lookup_entry(this->stream->xine, "input.dvd_seek_behaviour",
			       &cache_entry))
    seek_mode_cb(class, &cache_entry);
  

  if(this->mode == MODE_TITLE) {
    int tt, i, pr, found;
    int titles, parts;
    
    /* A program and/or VTS was specified */

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

    /* If there was a part specified, get that too. */
    pr = -1;
    if(found != -1) {
      pr = strtol(locator+found+1, NULL,10);
    }
    dvdnav_get_number_of_parts(this->dvdnav, tt, &parts);
    if ((pr == 0) || (pr > parts)) {
      printf("input_dvd: Part %i is out of range (1 to %i).\n", pr,
	      parts);
      dvdnav_close(this->dvdnav);
      this->dvdnav = NULL;
      return 0;
    }
#ifdef INPUT_DEBUG
    printf("input_dvd: Jumping to TT >%i<, PTT >%i<\n", tt, pr);
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

  /* Tell Xine to update the UI */
  event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
  event.stream = this->stream;
  event.data = NULL;
  event.data_length = 0;
  xine_event_send(this->stream, &event);

  update_title_display(this);
  
  return 1;
}

/* dvdnav CLASS functions */

/*
 * Opens the DVD plugin. The MRL takes the following form:
 *
 * dvd:[dvd_path]/[vts[.program]]
 *
 * e.g.
 *   dvd:/                    - Play (navigate)
 *   dvd:/1                   - Play Title 1
 *   dvd:/1.3                 - Play Title 1, program 3
 *   dvd:/dev/dvd2/           - Play (navigate) from /dev/dvd2
 *   dvd:/dev/dvd2/1.3        - Play Title 1, program 3 from /dev/dvd2
 */
static input_plugin_t *dvd_class_get_instance (input_class_t *class_gen, xine_stream_t *stream, const char *data) {
  dvd_input_plugin_t    *this;
  dvd_input_class_t     *class = (dvd_input_class_t*)class_gen;
  static char *handled_mrl = "dvd:/";

  trace_print("Called\n");
  
  /* Check we can handle this MRL */
  if (strncasecmp (data, handled_mrl, strlen(handled_mrl) ) != 0)
    return NULL;

  this = (dvd_input_plugin_t *) xine_xmalloc (sizeof (dvd_input_plugin_t));
  if (this == NULL) {
    XINE_ASSERT(0, "input_dvd.c: xine_xmalloc failed!!!! You have run out of memory\n");
  }

  this->input_plugin.open               = dvd_plugin_open;
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
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HAS_STILL] = 1;

  this->dvdnav                 = NULL;
  this->opened                 = 0;
  this->seekable               = 0;
  this->buttonN                = 0;
  this->typed_buttonN          = 0;
  this->pause_timer            = 0;
  this->pg_length              = 0;
  this->pgc_length             = 0;
  this->dvd_name               = NULL;
  this->mrl                    = strdup(data);
/*
  this->mrls                   = NULL;
  this->num_mrls               = 0;
*/

printf("dvd_class_get_instance2\n");
  pthread_mutex_init(&this->buf_mutex, NULL);
  this->mem_stack              = 0;
  this->freeing                = 0;
  
printf("dvd_class_get_instance21\n");
  this->event_queue = xine_event_new_queue (this->stream);
printf("dvd_class_get_instance22\n");
  
  /* config callbacks may react now */
  class->ip = this;

printf("dvd_class_get_instance3\n");
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

static char **dvd_class_get_autoplay_list (input_class_t *this_gen, 
					    int *num_files) {

  dvd_input_class_t *this = (dvd_input_class_t *) this_gen;
  trace_print("get_autoplay_list entered\n"); 

  this->filelist2[0] = "dvd:/";
  this->filelist2[1] = NULL;
  *num_files = 1;

  return this->filelist2;
}

void dvd_class_dispose(input_class_t *this_gen) {
  dvd_input_class_t *this = (dvd_input_class_t*)this_gen;
  
  free(this->mrls); this->mrls = NULL;
  free(this);
}

static int dvd_class_eject_media (input_class_t *this_gen) {
  dvd_input_class_t *this = (dvd_input_class_t*)this_gen;

  return media_eject_media (this->dvd_device);
}

static void *init_class (xine_t *xine, void *data) {
  dvd_input_class_t   *this;
  config_values_t     *config = xine->config;
  void                *dvdcss;
  static char         *skip_modes[] = {"skip program", "skip part", "skip title", NULL};
  static char         *seek_modes[] = {"seek in program chain", "seek in program", NULL};

  trace_print("Called\n");
#ifdef INPUT_DEBUG
  printf("input_dvd.c: init_class called.\n");
  printf("input_dvd.c: config = %p\n", config);
#endif

  this = (dvd_input_class_t *) malloc (sizeof (dvd_input_class_t));
  
  this->input_class.get_instance       = dvd_class_get_instance;
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
    char *css_cache_default, *css_cache;
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
    
    css_cache_default = (char *)malloc(strlen(xine_get_homedir()) + 10);
    sprintf(css_cache_default, "%s/.dvdcss/", xine_get_homedir());
    css_cache = config->register_string(config, "input.css_cache_path", css_cache_default,
					"path to the libdvdcss title key cache",
					NULL, 10, NULL, NULL);
    if (strlen(css_cache) > 0)
      xine_setenv("DVDCSS_CACHE", css_cache, 0);
    free(css_cache_default);
    
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
  config->register_enum(config, "input.dvd_seek_behaviour", 0,
			seek_modes,
			"Seeking will work on this basis.",
			NULL, 10, seek_mode_cb, this);

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
 * Revision 1.153  2003/04/22 23:30:29  tchamp
 * Additional changes for win32/msvc port; This is my first real commit so please be gentle with me; Everything builds except for the win32 ui
 *
 * Revision 1.152  2003/04/13 16:02:53  tmattern
 * Input plugin api change:
 * old open() function replaced by :
 *   *_class_get_instance() : return an instance if the plugin handles the mrl
 *   *_plugin_open() : open the stream
 *
 * Revision 1.151  2003/04/08 17:51:58  guenter
 * beta10
 *
 * Revision 1.150  2003/04/08 13:58:11  mroi
 * fix compilation problems
 *
 * Revision 1.149  2003/04/07 18:13:19  mroi
 * support the new menu resume feature
 *
 * Revision 1.148  2003/04/07 16:51:29  mroi
 * output beautification
 *
 * Revision 1.147  2003/04/06 23:44:59  guenter
 * some more dvd error reporting
 *
 * Revision 1.146  2003/04/06 13:19:59  mroi
 * * fix input_time reporting for PG based seeking
 *   (with more than one cell per PG, only the first cell starts at 0; for the others,
 *   we need pg_start)
 * * check for title sanity
 * * fix tsble -> table typo
 *
 * Revision 1.145  2003/04/06 13:06:03  jcdutton
 * Enable display of DVD Menu types.
 * Currently needs libdvdnav cvs, but does not break xine's own libdvdnav version.
 *
 * Revision 1.144  2003/04/06 12:11:10  mroi
 * reset the VM when it is already open
 *
 * Revision 1.143  2003/04/06 00:51:29  hadess
 * - shared eject implementation taken from the DVD input, eject doesn't work if the CD/DVD isn't mounted, which definitely breaks the CDDA plugin... better than nothing
 *
 * Revision 1.142  2003/04/05 12:28:16  miguelfreitas
 * "perfect" time display for dvds
 * (see thread on xine-devel for details)
 *
 * Revision 1.141  2003/04/04 19:20:48  miguelfreitas
 * add initial async error/general message reporting to frontend
 * obs: more messages should be added
 *
 * Revision 1.140  2003/04/03 13:04:52  mroi
 * not so much noise in cvs
 *
 * Revision 1.139  2003/04/01 11:45:32  jcdutton
 * Fix race condition, where spudec_reset is called and then a button update arrives from input_dvd.c before we have our this->menu_handle back.
 *
 * Revision 1.138  2003/03/30 10:57:48  mroi
 * additional sanity check on the part number
 *
 * Revision 1.137  2003/03/29 13:19:08  mroi
 * sync to libdvdnav cvs once again
 *  * some changes to mutual header inclusion to make it compile warning-less
 *    when tracing is enabled
 *  * title/part jumping should work much more reliable now
 *
 * Revision 1.136  2003/03/27 13:48:03  mroi
 * use timing information provided by libdvdnav to get more accurate position
 *
 * Revision 1.135  2003/03/25 13:20:31  mroi
 * new config option to switch between PG ("per chapter") and PGC ("per movie")
 * based seeking,
 * although this differs from the behaviour up to now, PGC based seeking is now the
 * default, since this is what people usually expect, what hardware players do and it
 * is needed for separate subtitles to work with DVDs.
 *
 * Revision 1.134  2003/03/13 22:09:51  mroi
 * turn these around so that dvd_get_current_position is defined before used
 *
 * Revision 1.133  2003/03/12 13:28:12  mroi
 * fix wrong return value of seek function, kindly reported by Nick Kurshev
 *
 * Revision 1.132  2003/03/04 10:30:28  mroi
 * fix compiler warnings at least in xine's native code
 *
 * Revision 1.131  2003/02/28 02:51:48  storri
 * Xine assert() replacement:
 *
 * All assert() function calls, with exceptions of libdvdread and libdvdnav, have been
 * replaced with XINE_ASSERT. Functionally XINE_ASSERT behaves just likes its predecesor but its
 * adding the ability to print out a stack trace at the point where the assertion fails.
 * So here are a few examples.
 *
 * assert (0);
 *
 * This use of assert was found in a couple locations most favorably being the default case of a switch
 * statement. This was the only thing there. So if the switch statement was unable to find a match
 * it would have defaulted to this and the user and the developers would be stuck wonder who died and where.
 *
 * So it has been replaced with
 *
 * XINE_ASSERT(0, "We have reach this point and don't have a default case");
 *
 * It may seem a bit none descriptive but there is more going on behind the scene.
 *
 * In addition to checking a condition is true/false, in this case '0', the XINE_ASSERT
 * prints out:
 *
 * <filename>:<function name>:<line number> - assertion '<assertion expression>' failed. <description>
 *
 * An example of this might be:
 *
 * input_dvd.c:open_plugin:1178 - assertion '0' failed. xine_malloc failed!!! You have run out of memory
 *
 * XINE_ASSERT and its helper function, print_trace, are found in src/xine-utils/xineutils.h
 *
 * Revision 1.130  2003/02/26 20:45:18  mroi
 * adjust input_dvd to handle DVDNAV_WAIT events properly
 * (that is: wait for the fifos to become empty)
 *
 * Revision 1.129  2003/02/20 16:01:57  mroi
 * syncing to libdvdnav 0.1.5 and modifying input plugin accordingly
 * quoting the ChangeLog:
 *   * some bugfixes
 *   * code cleanup
 *   * build process polishing
 *   * more sensible event order in get_next_block to ensure useful event delivery
 *   * VOBU level resume
 *   * fixed: seeking in a multiangle feature briefly showed the wrong angle
 *
 * Revision 1.128  2003/02/14 18:00:38  heikos
 * FreeBSD compile fixes
 *
 * Revision 1.127  2003/02/13 16:24:27  mroi
 * use the requested channel number when querying for the language
 * (the _cool_ menu in xine-ui displays the correct languages now)
 *
 * Revision 1.126  2003/02/11 15:17:10  mroi
 * enable libdvdcss title key cache
 *
 * Revision 1.125  2002/12/27 16:47:10  miguelfreitas
 * man errno: "must not be  explicitly  declared; errno  may  be a macro"
 * (thanks Chris Rankin for noticing)
 *
 * Revision 1.124  2002/12/22 23:35:42  miguelfreitas
 * it doesn't make sense to reimplement flush here.
 * (this is why xine_demux_flush_engine was created, to avoid redundant code)
 *
 * Revision 1.123  2002/12/21 12:56:47  miguelfreitas
 * - add buf->decoder_info_ptr: portability for systems where pointer has
 *   different sizeof than integer.
 * - add extra_info structure to pass informations from input/demuxers down
 *   to the output frame. this can be used, for example, to pass the frame
 *   number of a frame (when known by decoder). also, immediate benefict is
 *   that we now have a slider which really shows the current position of
 *   the playing stream. new fields can be added to extra_info keeping
 *   binary compatibility
 * - bumpy everybody's api versions
 *
 * Revision 1.122  2002/12/06 18:44:40  miguelfreitas
 * - add still frame hint (untested - i don't have dvd here)
 * - check mrl before allocating plugin context, so it doesn't get initialized for
 * non-dvd streams
 *
 * Revision 1.121  2002/11/23 12:41:04  mroi
 * DVD input fixes and cleanup:
 * * revert my removing of the clock adjustment; although this is bad, it seems
 *   to be the best solution for now (menu transitions have choppy audio without)
 * * add patch from Marco Zühlke enabling dvd device specification by MRL
 * * update GUI title and language display once immediately after plugin open
 *
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
  { PLUGIN_INPUT, 12, "DVD", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
