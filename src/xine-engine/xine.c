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
 * $Id: xine.c,v 1.21 2001/06/03 18:08:56 guenter Exp $
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
#include "libac3/ac3.h"
#include "libmpg123/mpg123.h"
#include "libmpg123/mpglib.h"
#include "libmpeg2/mpeg2.h"
#ifdef ARCH_X86
#include "libw32dll/w32codec.h"
#endif
#include "libspudec/spudec.h"
#include "input/input_plugin.h"
#include "metronom.h"
#include "configfile.h"
#include "monitor.h"
#include "utils.h"

/* debugging purposes only */
uint32_t   xine_debug;

void xine_notify_stream_finished (xine_t *this) {
  
  xine_stop (this);

  this->status_callback (this->status);
  
}

/*
 *
 */
void xine_stop (xine_t *this) {

  if (!this->cur_input_plugin) 
    return;

  pthread_mutex_lock (&this->xine_lock);
  
  this->status = XINE_STOP;
  
  if(this->cur_demuxer_plugin) {
    this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    this->cur_demuxer_plugin = NULL;
  }

  if(this->cur_input_plugin) {
    this->cur_input_plugin->close(this->cur_input_plugin);
    this->cur_input_plugin = NULL;
  }

  video_decoder_stop (this);
  audio_decoder_stop (this);

  this->spu_fifo->clear(this->spu_fifo);

  this->metronom->stop_clock (this->metronom);
  this->metronom->reset(this->metronom);

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

/*
 *
 */
static void xine_play_internal (xine_t *this, char *MRL, 
				int spos, off_t pos) {

  double     share ;
  off_t      len;
  int        i;

  xprintf (VERBOSE|LOOP, "xine open %s, start pos = %d\n",MRL, spos);

  if (this->status == XINE_PAUSE) {
    xine_pause(this);
    return;
  }

  if (this->status != XINE_STOP) {
    xine_stop (this);
  }

  /*
   * find input plugin
   */

  printf ("xine: looking for an input plugin...\n");
   
  this->cur_input_plugin = NULL;

  for (i = 0; i < this->num_input_plugins; i++) {
    if (this->input_plugins[i]->open(this->input_plugins[i], MRL)) {
      this->cur_input_plugin = this->input_plugins[i];
      break;
    }
  }

  if (!this->cur_input_plugin) {
    perror ("open input source");
    this->cur_demuxer_plugin = NULL;
    return;
  }
  
  printf ("xine: using input plugin >%s< for this MRL.\n", 
	  this->cur_input_plugin->get_identifier(this->cur_input_plugin));

  /*
   * find demuxer plugin
   */

  printf ("xine: looking for a demuxer plugin...\n");

  if(!find_demuxer(this, MRL)) {
    printf ("error: couldn't find demuxer for >%s<\n", MRL);
    return;
  }

  printf ("xine: using demuxer plugin >%s< for this MRL.\n", 
	  this->cur_demuxer_plugin->get_identifier());
  
  /*
   * Init SPU decoder with colour lookup table. 
   */

  /* FIXME
  if(this->cur_input_plugin->get_clut) 
    spudec_init(this->cur_input_plugin->get_clut());
  else 
    spudec_init(NULL);
  */

  /*
   * metronom
   */

  this->metronom->reset(this->metronom);

  /*
   * start demuxer
   */

  printf ("xine: starting demuxer ...\n");
  
  if (spos) {
    len = this->cur_input_plugin->get_length (this->cur_input_plugin);
    share = (double) spos / 65535;
    pos = (off_t) (share * len) ;
  }

  this->cur_demuxer_plugin->start (this->cur_demuxer_plugin,
				   this->video_fifo,
				   this->audio_fifo, 
				   this->spu_fifo, pos);
  
  this->status = XINE_PLAY;
  this->cur_input_pos = pos;

  /*
   * start clock
   */

  printf ("xine: starting clock ...\n");
  this->metronom->start_clock (this->metronom, 0);

  printf ("xine: play_internal done.\n");
}

/*
 *
 */
void xine_play (xine_t *this, char *MRL, int spos) {

  pthread_mutex_lock (&this->xine_lock);

  if (this->status != XINE_PLAY)    
    xine_play_internal (this, MRL, spos, (off_t) 0);

  pthread_mutex_unlock (&this->xine_lock);
}

/*
 *
 */
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

/*
 *
 */
void xine_exit (xine_t *this) {

  /*
   * stop decoder threads
   */

  if(this->cur_demuxer_plugin) {
    this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    this->cur_demuxer_plugin = NULL;
  }

  if(this->cur_input_plugin) {
    this->cur_input_plugin->close(this->cur_input_plugin);
    this->cur_input_plugin = NULL;
  }

  audio_decoder_shutdown (this);
  video_decoder_shutdown (this);

  this->status = XINE_QUIT;

  this->config->save (this->config);
}

/*
 *
 */
void xine_pause (xine_t *this) {

  pthread_mutex_lock (&this->xine_lock);

  if (this->status == XINE_PAUSE) {

    xprintf (VERBOSE, "xine play %s from %Ld\n", 
	     this->cur_mrl, this->cur_input_pos);

    this->status = XINE_STOP;

    xine_play_internal (this, this->cur_mrl, 0, this->cur_input_pos);
    /* this->mnPausePos = 0; */

  } else if (this->status == XINE_PLAY) {

    if (!this->cur_input_plugin) {
      pthread_mutex_unlock (&this->xine_lock);
      return;
    }

    this->status = XINE_PAUSE;

    this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    this->cur_demuxer_plugin = NULL;

    this->video_fifo->clear(this->video_fifo);
    this->audio_fifo->clear(this->audio_fifo);
    this->spu_fifo->clear(this->spu_fifo);
    
    this->metronom->reset(this->metronom);
    this->metronom->stop_clock (this->metronom);

    this->cur_input_plugin->close(this->cur_input_plugin);
  }

  pthread_mutex_unlock (&this->xine_lock);
}

/*
 *
 */
xine_t *xine_init (vo_driver_t *vo, 
		   ao_functions_t *ao,
		   gui_status_callback_func_t gui_status_callback,
		   config_values_t *config) {

  xine_t *this = xmalloc (sizeof (xine_t));

  this->status_callback = gui_status_callback;
  this->config          = config;
  xine_debug            = config->lookup_int (config, "xine_debug", 0);

  /*
   * init lock
   */

  pthread_mutex_init (&this->xine_lock, NULL);
  printf ("xine_init: lock created\n");

  /*
   * create a metronom
   */

  this->metronom = metronom_init ();
  printf ("xine_init: metronom created\n");

  /*
   * load input and demuxer plugins
   */
  
  load_input_plugins (this, config, INPUT_PLUGIN_IFACE_VERSION);
  
  printf ("xine_init: input plugins loaded\n");

  this->demux_strategy  = config->lookup_int (config, "demux_strategy", 0);

  load_demux_plugins(this, config, DEMUXER_PLUGIN_IFACE_VERSION);

  this->audio_channel = 0;
  this->spu_channel   = -1;
  this->cur_input_pos = 0;

  printf ("xine_init: demuxer plugins loaded\n");

  /*
   * init and start decoder threads
   */

  load_decoder_plugins (this, config, DECODER_PLUGIN_IFACE_VERSION);

  this->video_out = vo_new_instance (vo, this->metronom);
  video_decoder_init (this);

  if(ao) {
    this->audio_out = ao;
    this->audio_out->connect (this->audio_out, this->metronom);
  }
  audio_decoder_init (this);

  /*
   * init SPU decoder
   */

  
  this->spu_fifo   = fifo_buffer_new (1000, 4096);
  /* FIXME spudec_init(NULL);  */

  return this;
}

/*
 *
 */
int xine_get_audio_channel (xine_t *this) {

  return this->audio_channel;
}

/*
 *
 */
void xine_select_audio_channel (xine_t *this, int channel) {

  pthread_mutex_lock (&this->xine_lock);

  this->audio_channel = channel;

  pthread_mutex_unlock (&this->xine_lock);
}

/*
 *
 */
int xine_get_spu_channel (xine_t *this) {

  return this->spu_channel;
}

/*
 *
 */
void xine_select_spu_channel (xine_t *this, int channel) {

  pthread_mutex_lock (&this->xine_lock);

  this->spu_channel = (channel >= -1 ? channel : -1);

  pthread_mutex_unlock (&this->xine_lock);
}

/*
 *
 */
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

/*
 *
 */
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
