/* 
 * Copyright (C) 2000, 2001 the xine project
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
 * $Id: audio_esd_out.c,v 1.16 2001/12/18 22:38:38 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <esd.h>
#include <signal.h>
#include <sys/time.h>
#include <inttypes.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "audio_out.h"
#include "metronom.h"

#define AO_OUT_ESD_IFACE_VERSION 3

#define GAP_TOLERANCE         5000

typedef struct esd_driver_s {

  ao_driver_t      ao_driver;

  int              audio_fd;
  int              capabilities;
  int              mode;

  char            *pname; /* Player name id for esd daemon */

  int32_t          output_sample_rate, input_sample_rate;
  int32_t          output_sample_k_rate;
  double           sample_rate_factor;
  uint32_t         num_channels;
  uint32_t	   bytes_per_frame;
  uint32_t         bytes_in_buffer;      /* number of bytes writen to esd */

  int              gap_tolerance, latency;

  struct timeval   start_time;

  struct {
    char          *name;
    int            source_id;
    int            volume;
    int            mute;
  } mixer;

} esd_driver_t;


/*
 * connect to esd 
 */
static int ao_esd_open(ao_driver_t *this_gen,
		       uint32_t bits, uint32_t rate, int mode)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  esd_format_t     format;

  printf ("audio_esd_out: ao_open bits=%d rate=%d, mode=%d\n", 
	  bits, rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    printf ("audio_esd_out: unsupported mode %08x\n", mode);
    return 0;
  }

  if (this->audio_fd>=0) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) )
      return this->output_sample_rate;

    close (this->audio_fd);
  }
  
  this->mode                   = mode;
  this->input_sample_rate      = rate;
  this->output_sample_rate     = rate;
  this->bytes_in_buffer        = 0;

  /*
   * open stream to ESD server
   */

  format = ESD_STREAM | ESD_PLAY | ESD_BITS16;
  switch (mode) {
  case AO_CAP_MODE_MONO:
    format |= ESD_MONO;
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    format |= ESD_STEREO;
    this->num_channels = 2;
    break;
  }
  printf ("audio_esd_out: %d channels output\n",this->num_channels);

  this->bytes_per_frame=(bits*this->num_channels)/8;

  if (this->output_sample_rate > 44100)
    this->output_sample_rate = 44100;

  this->output_sample_k_rate   = this->output_sample_rate / 1000;

  this->audio_fd = esd_play_stream(format, this->output_sample_rate, NULL, this->pname);
  if (this->audio_fd < 0) {
    printf("audio_esd_out: connecting to ESD server %s: %s\n",
	   getenv("ESPEAKER"), strerror(errno));
    return 0;
  }

  gettimeofday(&this->start_time, NULL);

  return this->output_sample_rate;
}

static int ao_esd_num_channels(ao_driver_t *this_gen) 
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_esd_bytes_per_frame(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->bytes_per_frame;
}


static int ao_esd_delay(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  int           bytes_left;
  int           frames;
  struct        timeval tv;

  gettimeofday(&tv, NULL);

  frames  = (tv.tv_usec + 1000000 - this->start_time.tv_usec)
    * this->output_sample_k_rate / 1000;
  frames += (tv.tv_sec - this->start_time.tv_sec)
    * this->output_sample_rate;

  frames -= this->latency; 
  
  /* calc delay */
  
  bytes_left = this->bytes_in_buffer - frames * this->bytes_per_frame;
  
  if (bytes_left<=0) /* buffer ran dry */
    bytes_left = 0;

  return bytes_left / this->bytes_per_frame;
}

static int ao_esd_write(ao_driver_t *this_gen,
			int16_t* frame_buffer, uint32_t num_frames)
{

  esd_driver_t  *this = (esd_driver_t *) this_gen;
  int            simulated_bytes_in_buffer, frames ;
  struct timeval tv;

  if (this->audio_fd<0)
    return 1;

  /* check if simulated buffer ran dry */

  gettimeofday(&tv, NULL);
  
  frames  = (tv.tv_usec + 1000000 - this->start_time.tv_usec)
    * this->output_sample_k_rate / 1000;
  frames += (tv.tv_sec - this->start_time.tv_sec)
    * this->output_sample_rate;
  
  frames -= this->latency; 

  /* calc delay */
  
  simulated_bytes_in_buffer = frames * this->bytes_per_frame;
  
  if (this->bytes_in_buffer < simulated_bytes_in_buffer)
    this->bytes_in_buffer = simulated_bytes_in_buffer;

  this->bytes_in_buffer += num_frames * this->bytes_per_frame;

  write(this->audio_fd, frame_buffer, num_frames * this->bytes_per_frame);

  return 1;
}

static void ao_esd_close(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  esd_close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_esd_get_capabilities (ao_driver_t *this_gen) {
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->capabilities;
}

static int ao_esd_get_gap_tolerance (ao_driver_t *this_gen) {
  /* esd_driver_t *this = (esd_driver_t *) this_gen; */
  return GAP_TOLERANCE;
}

static void ao_esd_exit(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  
  if (this->audio_fd != -1)
    esd_close(this->audio_fd);

  free(this->pname);

  free (this);
}

static int ao_esd_get_property (ao_driver_t *this_gen, int property) {
  esd_driver_t      *this = (esd_driver_t *) this_gen;
  int                mixer_fd;
  esd_player_info_t *esd_pi;
  esd_info_t        *esd_i;
  
  switch(property) {
  case AO_PROP_MIXER_VOL:
    
    if((mixer_fd = esd_open_sound(NULL)) >= 0) {
      if((esd_i = esd_get_all_info(mixer_fd)) != NULL) {
	for(esd_pi = esd_i->player_list; esd_pi != NULL; esd_pi = esd_pi->next) {
	  if(!strcmp(this->pname, esd_pi->name)) {

	    this->mixer.source_id = esd_pi->source_id;
	    
	    if(!this->mixer.mute)
	      this->mixer.volume  = (((esd_pi->left_vol_scale * 100)  / 256) + 
				     ((esd_pi->right_vol_scale * 100) / 256)) >> 1;

	  }
	}
	esd_free_all_info(esd_i);
      }
      esd_close(mixer_fd);
    }
    
    return this->mixer.volume;
    break;

  case AO_PROP_MUTE_VOL:
    return this->mixer.mute;
    break;
  }

  return 0;
}

static int ao_esd_set_property (ao_driver_t *this_gen, int property, int value) {
  esd_driver_t *this = (esd_driver_t *) this_gen;
  int           mixer_fd;

  switch(property) {
  case AO_PROP_MIXER_VOL:
      
    if(!this->mixer.mute) {
      
      /* need this to get source_id */
      (void) ao_esd_get_property(&this->ao_driver, AO_PROP_MIXER_VOL);

      if((mixer_fd = esd_open_sound(NULL)) >= 0) {
	int v = (value * 256) / 100;
	
	esd_set_stream_pan(mixer_fd, this->mixer.source_id, v, v);
	
	if(!this->mixer.mute)
	  this->mixer.volume = value;
	
	esd_close(mixer_fd);
      }
    }
    else
      this->mixer.volume = value;
    
    return this->mixer.volume;
    break;
    
  case AO_PROP_MUTE_VOL: {
    int mute = (value) ? 1 : 0;
    
    /* need this to get source_id */
    (void) ao_esd_get_property(&this->ao_driver, AO_PROP_MIXER_VOL);
    
    if(mute) {
      if((mixer_fd = esd_open_sound(NULL)) >= 0) {
	int v = 0;
	
	esd_set_stream_pan(mixer_fd, this->mixer.source_id, v, v);
	esd_close(mixer_fd);
      }
    }
    else {
      if((mixer_fd = esd_open_sound(NULL)) >= 0) {
	int v = (this->mixer.volume * 256) / 100;
	
	esd_set_stream_pan(mixer_fd, this->mixer.source_id, v, v);
	esd_close(mixer_fd);
      }
    }
    
    this->mixer.mute = mute;
    
    return value;
  }
  break;
  }

  return ~value;
}

ao_driver_t *init_audio_out_plugin (config_values_t *config) {

  esd_driver_t *this;
  int           audio_fd;
  sigset_t	vo_mask, vo_mask_orig;

  /*
   * open stream to ESD server
   *
   * esd_open_sound needs a working SIGALRM for detecting a failed
   * attempt to autostart the esd daemon;  esd notifies the process that
   * attempts the esd daemon autostart with a SIGALRM (SIGUSR1) signal
   * about a failure to open the audio device (successful daemin startup).
   *
   * Temporarily release the blocked SIGALRM, while esd_open_sound is active.
   * (Otherwise xine hangs in esd_open_sound on a machine without sound)
   */

  sigemptyset(&vo_mask);
  sigaddset(&vo_mask, SIGALRM);
  if (sigprocmask(SIG_UNBLOCK, &vo_mask, &vo_mask_orig)) 
    printf("audio_esd_out: cannot unblock SIGALRM: %s\n", strerror(errno));

  printf("audio_esd_out: connecting to esd server...\n");
  audio_fd = esd_open_sound(NULL);
  
  if (sigprocmask(SIG_SETMASK, &vo_mask_orig, NULL))
    printf("audio_esd_out: cannot block SIGALRM: %s\n", strerror(errno));

  if(audio_fd < 0) {
    char *server = getenv("ESPEAKER");

    /* print a message so the user knows why ESD failed */
    printf("audio_esd_out: can't connect to %s ESD server: %s\n",
	   server ? server : "local", strerror(errno));

    return NULL;
  }
  
  esd_close(audio_fd);


  this                     = (esd_driver_t *) xine_xmalloc (sizeof (esd_driver_t));
  this->pname              = strdup("xine esd audio output plugin");
  this->output_sample_rate = 0;
  this->audio_fd           = -1;
  this->capabilities       = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO | AO_CAP_MIXER_VOL | AO_CAP_MUTE_VOL;
  this->latency            = config->register_range (config, "audio.esd_latency", 30000,
						     -30000, 90000,
						     "esd audio output latency (adjust a/v sync)",
						     NULL, NULL, NULL);

  this->ao_driver.get_capabilities    = ao_esd_get_capabilities;
  this->ao_driver.get_property        = ao_esd_get_property;
  this->ao_driver.set_property        = ao_esd_set_property;
  this->ao_driver.open                = ao_esd_open;
  this->ao_driver.num_channels        = ao_esd_num_channels;
  this->ao_driver.bytes_per_frame     = ao_esd_bytes_per_frame;
  this->ao_driver.get_gap_tolerance   = ao_esd_get_gap_tolerance;
  this->ao_driver.delay               = ao_esd_delay;
  this->ao_driver.write		      = ao_esd_write;
  this->ao_driver.close               = ao_esd_close;
  this->ao_driver.exit                = ao_esd_exit;

  return &this->ao_driver;
}

static ao_info_t ao_info_esd = {
  AO_OUT_ESD_IFACE_VERSION,
  "esd",
  "xine audio output plugin using esd",
  5
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_esd;
}

