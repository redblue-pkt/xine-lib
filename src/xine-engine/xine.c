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
 * $Id: xine.c,v 1.56 2001/09/02 22:26:54 guenter Exp $
 *
 * top-level xine functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#if defined (__linux__)
#include <endian.h>
#elif defined (__FreeBSD__)
#include <machine/endian.h>
#endif

#include "xine_internal.h"
#include "audio_out.h"
#include "video_out.h"
#include "demuxers/demux.h"
#include "buffer.h"
#include "libspudec/spu_decoder_api.h"
/* TODO: who uses spu_decoder.h ? */
#include "spu_decoder.h"
#include "input/input_plugin.h"
#include "metronom.h"
#include "configfile.h"
#include "monitor.h"
#include "utils.h"

/* debugging purposes only */
uint32_t   xine_debug;

void xine_notify_stream_finished (xine_t *this) {
  
  xine_stop (this);

  if (this->stream_end_cb)
    this->stream_end_cb (this->status);
  
}

void xine_stop (xine_t *this) {

  pthread_mutex_lock (&this->xine_lock);

  printf ("xine_stop\n");

  if (this->status == XINE_STOP) {
    printf ("xine_stop ignored\n");
    pthread_mutex_unlock (&this->xine_lock);
    return;
  }


  this->metronom->set_speed (this->metronom, SPEED_NORMAL);
  this->speed      = SPEED_NORMAL;
  this->audio_mute = 0;

  this->status = XINE_STOP;
  printf ("xine_stop: stopping demuxer\n");
  
  if(this->cur_demuxer_plugin) {
    this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    this->cur_demuxer_plugin = NULL;
  }

  if(this->cur_input_plugin) {
    this->cur_input_plugin->close(this->cur_input_plugin);
    /*
     * If we set it to NULL, xine_eject() will not work after
     * a xine_stop() call.
     * 
     * this->cur_input_plugin = NULL;
     */
  }

  printf ("xine_stop: done\n");
  
  pthread_mutex_unlock (&this->xine_lock);
}

/*
 * *****
 * Demuxers probing stuff
 */
static int try_demux_with_stages(xine_t *this, const char *MRL, 
				 int stage1, int stage2) {
  int s = 0, i;
  int stages[3] = {
    stage1, stage2, -1
  };

  if(stages[0] == -1) {
    fprintf(stderr, "%s(%d) wrong first stage = %d !!\n", 
	    __FUNCTION__, __LINE__, stage1);
    return 0;
  }

  while(stages[s] != -1) {
    for(i = 0; i < this->num_demuxer_plugins; i++) {
      printf ("trying demuxer %s\n", this->demuxer_plugins[i]->get_identifier());
      if(this->demuxer_plugins[i]->open(this->demuxer_plugins[i], 
					this->cur_input_plugin, 
					stages[s]) == DEMUX_CAN_HANDLE) {
	
	this->cur_demuxer_plugin = this->demuxer_plugins[i];
	
	return 1;
      }
    }
    s++;
  }

  return 0;
}
/*
 * Try to find a demuxer which handle the MRL stream
 */
static int find_demuxer(xine_t *this, const char *MRL) {

  this->cur_demuxer_plugin = NULL;

  switch(this->demux_strategy) {

  case DEMUX_DEFAULT_STRATEGY:
    if(try_demux_with_stages(this, MRL, STAGE_BY_CONTENT, STAGE_BY_EXTENSION))
      return 1;
    break;

  case DEMUX_REVERT_STRATEGY:
    if(try_demux_with_stages(this, MRL, STAGE_BY_EXTENSION, STAGE_BY_CONTENT))
      return 1;
    break;

  case DEMUX_CONTENT_STRATEGY:
    if(try_demux_with_stages(this, MRL, STAGE_BY_CONTENT, -1))
      return 1;
    break;

  case DEMUX_EXTENSION_STRATEGY:
    if(try_demux_with_stages(this, MRL, STAGE_BY_EXTENSION, -1))
      return 1;
    break;

  }
  
  return 0;
}

void xine_play (xine_t *this, char *mrl, 
		int start_pos, int start_time) {

  double     share ;
  off_t      pos, len;
  int        i;

  xprintf (VERBOSE|LOOP, "xine open %s, start pos = %d\n", mrl, start_pos);

  pthread_mutex_lock (&this->xine_lock);

  printf ("xine_play: open %s, start pos = %d\n", mrl, start_pos);

  /*
   * stop engine?
   */

  if (this->status == XINE_PLAY) {
    
    if(this->cur_demuxer_plugin) {
      this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    }
    
    if(this->cur_input_plugin) {

      if (strcmp (mrl, this->cur_mrl)) 
	this->cur_input_plugin->close(this->cur_input_plugin);
      else
	this->cur_input_plugin->stop(this->cur_input_plugin);
    }

    this->status = XINE_STOP;
  }

  /*
   * find input plugin
   */

  this->cur_input_plugin = NULL;

  for (i = 0; i < this->num_input_plugins; i++) {
    if (this->input_plugins[i]->open(this->input_plugins[i], mrl)) {
      this->cur_input_plugin = this->input_plugins[i];
      break;
    }
  }

  if (!this->cur_input_plugin) {
    printf ("xine: cannot find input plugin for this MRL\n");
    this->cur_demuxer_plugin = NULL;
    pthread_mutex_unlock (&this->xine_lock);
    return;
  }
  
  printf ("xine: using input plugin >%s< for this MRL.\n", 
	  this->cur_input_plugin->get_identifier(this->cur_input_plugin));

  /* FIXME: This is almost certainly the WRONG way to do this but it is
   * only temporary until a better way if found for plugins to send events.
   */
  this->cur_input_plugin->get_optional_data(this->cur_input_plugin,
					    (void*)this, 0x1010);

  /*
   * find demuxer plugin
   */

  if(!find_demuxer(this, mrl)) {
    printf ("xine: couldn't find demuxer for >%s<\n", mrl);
    pthread_mutex_unlock (&this->xine_lock);
    return;
  }

  printf ("xine: using demuxer plugin >%s< for this MRL.\n", 
	  this->cur_demuxer_plugin->get_identifier());
  
  /*
   * start demuxer
   */

  if (start_pos) {
    len = this->cur_input_plugin->get_length (this->cur_input_plugin);
    share = (double) start_pos / 65535;
    pos = (off_t) (share * len) ;
  } else
    pos = 0;

  this->cur_demuxer_plugin->start (this->cur_demuxer_plugin,
				   this->video_fifo,
				   this->audio_fifo, 
				   pos, start_time,
				   this->get_next_mrl_cb,
				   this->branched_cb);
  
  if (this->cur_demuxer_plugin->get_status(this->cur_demuxer_plugin) != DEMUX_OK) {
    printf("xine_play: demuxer failed to start\n");
    
    this->cur_input_plugin->close(this->cur_input_plugin);

    this->status = XINE_STOP;
  } else {

    this->status = XINE_PLAY;
    strncpy (this->cur_mrl, mrl, 1024);
    
    this->metronom->set_speed (this->metronom, SPEED_NORMAL);
    this->audio_mute = 0;
    this->speed = SPEED_NORMAL;
    
    printf ("xine_play: demuxer started\n");
  }    

  pthread_mutex_unlock (&this->xine_lock);
}

int xine_eject (xine_t *this) {
  
  if(this->cur_input_plugin == NULL) 
    return 0;
  
  pthread_mutex_lock (&this->xine_lock);

  if ((this->status == XINE_STOP)
      && this->cur_input_plugin && this->cur_input_plugin->eject_media) {

    pthread_mutex_unlock (&this->xine_lock);

    return this->cur_input_plugin->eject_media (this->cur_input_plugin);
  }

  pthread_mutex_unlock (&this->xine_lock);
  return 0;
}

void xine_exit (xine_t *this) {

  printf ("xine_exit: try to get lock...\n");

  pthread_mutex_lock (&this->xine_lock);
  
  this->metronom->set_speed (this->metronom, SPEED_NORMAL);
  this->speed  = SPEED_NORMAL;
  this->status = XINE_STOP;
  
  /*
   * stop decoder threads
   */

  if(this->cur_demuxer_plugin) {
    printf ("xine_exit: closing demuxer\n");

    this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    this->cur_demuxer_plugin = NULL;
  }

  if(this->cur_input_plugin) {
    printf ("xine_exit: closing input plugin\n");

    this->cur_input_plugin->stop (this->cur_input_plugin);
    this->cur_input_plugin->close(this->cur_input_plugin);
    this->cur_input_plugin = NULL;
  }

  pthread_mutex_unlock (&this->xine_lock);

  printf ("xine_exit: shutdown audio\n");

  this->audio_mute = 1;
  audio_decoder_shutdown (this);

  printf ("xine_exit: shutdown video\n");

  video_decoder_shutdown (this);

  this->status = XINE_QUIT;

  printf ("xine_exit: bye!\n");

  profiler_print_results ();

}

static void event_handler(xine_t *xine, event_t *event, void *data) {
  /* Check Xine handle/current input plugin is not NULL */
  if((xine == NULL) || (xine->cur_input_plugin == NULL)) {
    return;
  }
  
  switch(event->type) {
  case XINE_MOUSE_EVENT: 
    {
      mouse_event_t *mevent = (mouse_event_t*)event;
      
      /* Send event to imput plugin if appropriate. */
      if(xine->cur_input_plugin->handle_input_event != NULL) {
	if(mevent->button != 0) {
	  /* Click event. */
  	  xine->cur_input_plugin->handle_input_event(xine->cur_input_plugin,
						     INPUT_EVENT_MOUSEBUTTON,
						     0, mevent->x, mevent->y);
	} else {
	  /* Motion event */
	  xine->cur_input_plugin->handle_input_event(xine->cur_input_plugin,
						     INPUT_EVENT_MOUSEMOVE,
						     0, mevent->x, mevent->y);
	}
      }
    }
    break;
  case XINE_SPU_EVENT:
    if (xine->cur_spu_decoder_plugin)
      xine->cur_spu_decoder_plugin->event(xine->cur_spu_decoder_plugin,
		(spu_event_t*) event);
    break;
  }
}

xine_t *xine_init (vo_driver_t *vo, 
		   ao_driver_t *ao,
		   config_values_t *config,
		   gui_stream_end_cb_t stream_end_cb,
		   gui_get_next_mrl_cb_t get_next_mrl_cb,
		   gui_branched_cb_t branched_cb) {

  xine_t *this = xmalloc (sizeof (xine_t));
  printf("xine_init entered\n");

  this->stream_end_cb   = stream_end_cb;
  this->get_next_mrl_cb = get_next_mrl_cb;
  this->branched_cb     = branched_cb;  
  this->config          = config;
  xine_debug            = config->lookup_int (config, "xine_debug", 0);

  /*
   * set up profiler
   */
  profiler_init();
  profiler_set_label (0, "video decoder         ");
  profiler_set_label (1, "audio decoder/output  ");
  profiler_set_label (2, "video output          ");

  /*
   * init lock
   */

  pthread_mutex_init (&this->xine_lock, NULL);

  /*
   * create a metronom
   */

  this->metronom = metronom_init (ao != NULL);

  /*
   * load input and demuxer plugins
   */
  
  load_input_plugins (this, config, INPUT_PLUGIN_IFACE_VERSION);
  
  this->demux_strategy  = config->lookup_int (config, "demux_strategy", 0);

  load_demux_plugins(this, config, DEMUXER_PLUGIN_IFACE_VERSION);

  this->audio_channel = 0;
  this->spu_channel   = -1;
  this->cur_input_pos = 0;

  /*
   * init and start decoder threads
   */

  load_decoder_plugins (this, config, DECODER_PLUGIN_IFACE_VERSION);

  this->video_out = vo_new_instance (vo, this->metronom);
  video_decoder_init (this);

  if(ao) 
    this->audio_out = ao_new_instance (ao, this->metronom, config);

  audio_decoder_init (this);
  printf("xine_init returning\n");

  /*
   * init event listeners
   */
  this->num_event_listeners = 0; /* Initially there are none */

  if((xine_register_event_listener(this, event_handler)) < 1) {
    fprintf(stderr, "xine_register_event_listener() failed.\n");
  }

  return this;
}

int xine_get_audio_channel (xine_t *this) {

  return this->audio_channel;
}

void xine_select_audio_channel (xine_t *this, int channel) {

  pthread_mutex_lock (&this->xine_lock);

  this->audio_channel = channel;

  pthread_mutex_unlock (&this->xine_lock);
}

int xine_get_spu_channel (xine_t *this) {

  return this->spu_channel;
}

void xine_select_spu_channel (xine_t *this, int channel) {

  pthread_mutex_lock (&this->xine_lock);

  this->spu_channel = (channel >= -1 ? channel : -1);

  pthread_mutex_unlock (&this->xine_lock);
}

int xine_get_current_position (xine_t *this) {

  off_t len;
  double share;
  
  pthread_mutex_lock (&this->xine_lock);

  if (!this->cur_input_plugin) {
    xprintf (VERBOSE|INPUT, "xine_get_current_position: no input source\n");
    pthread_mutex_unlock (&this->xine_lock);
    return 0;
  }
  
  /* pos = this->mCurInput->seek (0, SEEK_CUR); */
  len = this->cur_input_plugin->get_length (this->cur_input_plugin);

  share = (double) this->cur_input_pos / (double) len * 65535;

  pthread_mutex_unlock (&this->xine_lock);

  return (int) share;
}

int xine_get_status(xine_t *this) {

  return this->status;
}

/* ***
 * Version information/check
 */

/*
 * Return version in string, like "0.5.0"
 */
char *xine_get_str_version(void) {
  return VERSION;
}

/*
 * Return major version
 */
int xine_get_major_version(void) {
  return XINE_MAJOR;
}

/*
 * Return minor version
 */
int xine_get_minor_version(void) {
  return XINE_MINOR;
}

/*
 * Return sub version
 */
int xine_get_sub_version(void) {
  return XINE_SUB;
}

/*
 * Check if xine version is <= to specifier version.
 */
int xine_check_version(int major, int minor, int sub) {
  
  if((XINE_MAJOR > major) || 
     ((XINE_MAJOR == major) && (XINE_MINOR > minor)) || 
     ((XINE_MAJOR == major) && (XINE_MINOR == minor) && (XINE_SUB >= sub)))
    return 1;
  
  return 0;
}

/*
 * manually adjust a/v sync 
 */

void xine_set_av_offset (xine_t *this, int offset_pts) {
  this->metronom->set_av_offset (this->metronom, offset_pts);
}

int xine_get_av_offset (xine_t *this) {
  return this->metronom->get_av_offset (this->metronom);
}

/*
 * trick play 
 */

void xine_set_speed (xine_t *this, int speed) {
  
  pthread_mutex_lock (&this->xine_lock);

  if (speed < SPEED_PAUSE)
    speed = SPEED_PAUSE;
  if (speed > SPEED_FAST_4)
    speed = SPEED_FAST_4;

  printf ("xine_set_speed %d\n", speed);

  this->metronom->set_speed (this->metronom, speed);

  if (speed == SPEED_PAUSE) 
    this->audio_mute = 2;
  else
    this->audio_mute = speed != SPEED_NORMAL;

  this->speed      = speed;

  pthread_mutex_unlock (&this->xine_lock);
}


int xine_get_speed (xine_t *this) {
  return this->speed;
}

/*
 * time measurement / seek
 */

int xine_get_current_time (xine_t *this) {
  return this->cur_input_time;
}

int xine_get_stream_length (xine_t *this) {

  if(this->cur_demuxer_plugin) 
    return this->cur_demuxer_plugin->get_stream_length (this->cur_demuxer_plugin);

  return 0;
}


