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
 * Credits go 
 * - for the SPDIF A/52 sync part
 * - frame size calculation added (16-08-2001)
 * (c) 2001 Andy Lo A Foe <andy@alsaplayer.org>
 * for initial ALSA 0.9.x support.
 *     adding MONO/STEREO/4CHANNEL/5CHANNEL/5.1CHANNEL analogue support.
 * (c) 2001 James Courtier-Dutton <James@superbug.demon.co.uk>
 *
 * 
 * $Id: audio_alsa_out.c,v 1.88 2003/03/15 13:50:58 jstembridge Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <alloca.h>

#ifdef HAVE_ALSA_ASOUNDLIB_H
#include <alsa/asoundlib.h>
#elif HAVE_SYS_ASOUNDLIB_H
#include <sys/asoundlib.h>
#else
#error "required asoundlib.h neither in sys/ nor alsa/ - unable to compile."
#endif

#include <sys/ioctl.h>
#include <inttypes.h>
#include <pthread.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "audio_out.h"


#define ALSA_LOG
/*
#define LOG_DEBUG
*/

#define AO_OUT_ALSA_IFACE_VERSION 7

#define BUFFER_TIME               1000*1000
#define PERIOD_TIME               100*1000
#define GAP_TOLERANCE             5000

#define MIXER_MASK_LEFT           (1 << 0)
#define MIXER_MASK_RIGHT          (1 << 1)
#define MIXER_MASK_STEREO         (MIXER_MASK_LEFT|MIXER_MASK_RIGHT)

typedef struct {
  audio_driver_class_t driver_class;

  xine_t          *xine;
} alsa_class_t;

typedef struct alsa_driver_s {

  ao_driver_t   ao_driver;

  alsa_class_t *class;

  snd_pcm_t    *audio_fd;
  int           capabilities;
  int           open_mode;
  int		has_pause_resume;

  int32_t       output_sample_rate, input_sample_rate;
  double        sample_rate_factor;
  uint32_t      num_channels;
  uint32_t      bits_per_sample;
  uint32_t      bytes_per_frame;
  uint32_t      bytes_in_buffer;      /* number of bytes writen to audio hardware   */
  snd_pcm_sframes_t  buffer_size;

  struct {
    pthread_t          thread;
    pthread_mutex_t    mutex;
    char              *name;
    snd_mixer_t       *handle;
    snd_mixer_elem_t  *elem;
    long               min;
    long               max;
    long               left_vol;
    long               right_vol;
    int                mute;
  } mixer;
} alsa_driver_t;

static snd_output_t *jcd_out;

/*
 * Wait (blocking) till a mixer event happen
 */
static void *ao_alsa_handle_event_thread(void *data) {
  alsa_driver_t  *this = (alsa_driver_t *) data;

  do {
    snd_mixer_wait(this->mixer.handle, -1);
    pthread_mutex_lock(&this->mixer.mutex);
    snd_mixer_handle_events(this->mixer.handle);
    pthread_mutex_unlock(&this->mixer.mutex);
  } while(1);
  
  pthread_exit(NULL);
}

/*
 * Get and convert volume to percent value
 */
static int ao_alsa_get_percent_from_volume(long val, long min, long max)
{
  int range = max - min;
  int tmp;
  
  if (range == 0)
    return 0;
  val -= min;
  tmp = rint((double)val / (double)range * 100);
  return tmp;
}

/*
 * Convert percent value to volume and set
 */
static long ao_alsa_get_volume_from_percent(int val, long min, long max)
{
  int range = max - min;
  long tmp;
  
  if (range == 0)
    return 0;
  val -= min;
  tmp = (long) ((range * val) / 100);
  return tmp;
}


/*
 * open the audio device for writing to
 */
static int ao_alsa_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode)
{
  alsa_driver_t        *this = (alsa_driver_t *) this_gen;
  config_values_t *config = this->class->xine->config;
  char                 *pcm_device;
  snd_pcm_stream_t      direction = SND_PCM_STREAM_PLAYBACK; 
  snd_pcm_hw_params_t  *params;
  snd_pcm_sw_params_t  *swparams;
  snd_pcm_sframes_t     buffer_size;
  snd_pcm_sframes_t     period_size;
  int                   err, dir;
 // int                 open_mode=1; //NONBLOCK
  int                   open_mode=0; //BLOCK

  snd_pcm_hw_params_alloca(&params);
  snd_pcm_sw_params_alloca(&swparams);
  err = snd_output_stdio_attach(&jcd_out, stdout, 0);

  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    pcm_device = config->register_string(config,
                                         "audio.alsa_default_device",
                                         "default",
                                         _("device used for mono output"),
                                         NULL,
                                         10, NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    pcm_device = config->register_string(config,
                                         "audio.alsa_front_device",
                                         "default",
                                         _("device used for stereo output"),
                                         NULL,
                                         10, NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_4CHANNEL:
    this->num_channels = 4;
    pcm_device = config->register_string(config,
                                         "audio.alsa_surround40_device",
                                         "surround40",
                                         _("device used for 4-channel output"),
                                         NULL,
                                         10, NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_5CHANNEL:
    this->num_channels = 5;
    pcm_device = config->register_string(config,
                                         "audio.alsa_surround50_device",
                                         "surround51",
                                         _("device used for 5-channel output"),
                                         NULL,
                                         10, NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_5_1CHANNEL:
    this->num_channels = 6;
    pcm_device = config->register_string(config,
                                         "audio.alsa_surround51_device",
                                         "surround51",
                                         _("device used for 5.1-channel output"),
                                         NULL,
                                         10, NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
    this->num_channels = 2;
    pcm_device = config->register_string(config,
                                         "audio.alsa_a52_device",
                                         "iec958:AES0=0x6,AES1=0x82,AES2=0x0,AES3=0x2",
                                         _("device used for 5.1-channel output"),
                                         NULL,
                                         10, NULL,
                                         NULL);
    break;
  default:
    printf ("audio_alsa_out: ALSA Driver does not support the requested mode: 0x%X\n",mode);
    return 0;
  } 

#ifdef ALSA_LOG
  printf("audio_alsa_out: Audio Device name = %s\n",pcm_device);
  printf("audio_alsa_out: Number of channels = %d\n",this->num_channels);
#endif

  if (this->audio_fd != NULL) {
    xine_log (this->class->xine, XINE_LOG_MSG,
              "audio_alsa_out:Already open...WHY!");
    snd_pcm_close (this->audio_fd);
  }

  this->open_mode              = mode;
  this->input_sample_rate      = rate;
  this->bits_per_sample        = bits;
  this->bytes_in_buffer        = 0;
  /*
   * open audio device
   */
  err=snd_pcm_open(&this->audio_fd, pcm_device, direction, open_mode);      
  if(err <0 ) {                                                           
    printf ("audio_alsa_out: snd_pcm_open() failed: %s\n", snd_strerror(err));               
    printf ("audio_alsa_out: >>> check if another program don't already use PCM <<<\n");     
    return 0;
  }
  /* We wanted non blocking open but now put it back to normal */
  //snd_pcm_nonblock(this->audio_fd, 0);
  snd_pcm_nonblock(this->audio_fd, 1);
  /*
   * configure audio device
   */
  err = snd_pcm_hw_params_any(this->audio_fd, params);
  if (err < 0) {
    printf ("audio_alsa_out: broken configuration for this PCM: no configurations available\n");
    goto __close;
  }
  /* set interleaved access */
  err = snd_pcm_hw_params_set_access(this->audio_fd, params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err < 0) {
    printf ("audio_alsa_out: access type not available\n");
    goto __close;
  }
  /* set the sample format ([SU]{8,16{LE,BE}})*/
  err = snd_pcm_hw_params_set_format(this->audio_fd, params, bits == 16 ?
#ifdef WORDS_BIGENDIAN
		  SND_PCM_FORMAT_S16_BE
#else
		  SND_PCM_FORMAT_S16_LE
#endif
		  : SND_PCM_FORMAT_U8);
  if (err < 0) {
    printf ("audio_alsa_out: sample format non available\n");
    goto __close;
  }
  /* set the count of channels */
  err = snd_pcm_hw_params_set_channels(this->audio_fd, params, this->num_channels);
  if (err < 0) {
    printf ("audio_alsa_out: channels count non available\n");
    goto __close;
  }
  /* set the stream rate [Hz] */
  dir=0;
  err = snd_pcm_hw_params_set_rate_near(this->audio_fd, params, rate, &dir);
  if (err < 0) {
    printf ("audio_alsa_out: rate not available\n");
    goto __close;
  }
  this->output_sample_rate = (uint32_t)err;
  if (this->input_sample_rate != this->output_sample_rate) {
    printf ("audio_alsa_out: audio rate : %d requested, %d provided by device/sec\n",
	    this->input_sample_rate, this->output_sample_rate);
  }
  /* set the ring-buffer time [us] (large enough for x us|y samples ...) */
  dir=0;
  err = snd_pcm_hw_params_set_buffer_time_near(this->audio_fd, params, BUFFER_TIME, &dir);
  if (err < 0) {
    printf ("audio_alsa_out: buffer time not available\n");
    goto __close;
  }
  this->buffer_size = buffer_size = snd_pcm_hw_params_get_buffer_size(params);
  /* set the period time [us] (interrupt every x us|y samples ...) */
  dir=0;
  err = snd_pcm_hw_params_set_period_size_near(this->audio_fd, params, buffer_size/8, &dir);
  /*err = snd_pcm_hw_params_set_period_time_near(this->audio_fd, params, PERIOD_TIME, &dir); */
  if (err < 0) {
    printf ("audio_alsa_out: period time not available");
    goto __close;
  }
  period_size = snd_pcm_hw_params_get_period_size(params, NULL);
  if (2*period_size > buffer_size) {
    printf ("audio_alsa_out: buffer to small, could not use\n");
    goto __close;
  }
  
  /* write the parameters to device */
  err = snd_pcm_hw_params(this->audio_fd, params);
  if (err < 0) {
    printf ("audio_alsa_out: pcm hw_params failed: %s\n", snd_strerror(err));
    goto __close;
  }
  /* Check for pause/resume support */
  this->has_pause_resume = ( snd_pcm_hw_params_can_pause (params)
			    && snd_pcm_hw_params_can_resume (params) );
  printf ("audio_alsa_out:open pause_resume=%d\n", this->has_pause_resume);
  this->sample_rate_factor = (double) this->output_sample_rate / (double) this->input_sample_rate;
  this->bytes_per_frame = snd_pcm_frames_to_bytes (this->audio_fd, 1);
  /*
   * audio buffer size handling
   */
  /* Copy current parameters into swparams */
  err = snd_pcm_sw_params_current(this->audio_fd, swparams);
  if (err < 0) {
    printf ("audio_alsa_out: Unable to determine current swparams: %s\n", snd_strerror(err));
    goto __close;
  }
  /* align all transfers to 1 sample */
  err = snd_pcm_sw_params_set_xfer_align(this->audio_fd, swparams, 1);
  if (err < 0) {
    printf ("audio_alsa_out: Unable to set transfer alignment: %s\n", snd_strerror(err));
    goto __close;
  }
  /* allow the transfer when at least period_size samples can be processed */
  err = snd_pcm_sw_params_set_avail_min(this->audio_fd, swparams, period_size);
  if (err < 0) {
    printf ("audio_alsa_out: Unable to set available min: %s\n", snd_strerror(err));
    goto __close;
  }
  /* start the transfer when the buffer contains at least period_size samples */
  err = snd_pcm_sw_params_set_start_threshold(this->audio_fd, swparams, period_size);
  if (err < 0) {
    printf ("audio_alsa_out: Unable to set start threshold: %s\n", snd_strerror(err));
    goto __close;
  }

  /* never stop the transfer, even on xruns */
  err = snd_pcm_sw_params_set_stop_threshold(this->audio_fd, swparams, buffer_size);
  if (err < 0) {
    printf ("audio_alsa_out: Unable to set stop threshold: %s\n", snd_strerror(err));
    goto __close;
  }

  /* Install swparams into current parameters */
  err = snd_pcm_sw_params(this->audio_fd, swparams);
  if (err < 0) {
    printf ("audio_alsa_out: Unable to set swparams: %s\n", snd_strerror(err));
    goto __close;
  }
#ifdef ALSA_LOG
  snd_pcm_dump_setup(this->audio_fd, jcd_out); 
  snd_pcm_sw_params_dump(swparams, jcd_out);
#endif
  return this->output_sample_rate;
__close:
  snd_pcm_close (this->audio_fd);
  this->audio_fd=NULL;
  return 0;
}

/*
 * Return the number of audio channels
 */
static int ao_alsa_num_channels(ao_driver_t *this_gen)
{
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->num_channels;
}

/*
 * Return the number of bytes per frame
 */
static int ao_alsa_bytes_per_frame(ao_driver_t *this_gen)
{
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->bytes_per_frame;
}

/*
 * Return gap tolerance (in pts)
 */
static int ao_alsa_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

/*
 * Return the delay. is frames measured by looking at pending samples
 */
/* FIXME: delay returns invalid data if status is not RUNNING. 
 * e.g When there is an XRUN or we are in PREPARED mode.
 */
static int ao_alsa_delay (ao_driver_t *this_gen) 
{
  snd_pcm_sframes_t delay = 0;
  int err = 0;
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
#ifdef LOG_DEBUG
  struct timeval now;
  printf("audio_alsa_out:delay:ENTERED\n");
#endif
  err=snd_pcm_delay( this->audio_fd, &delay );
#ifdef LOG_DEBUG
  printf("audio_alsa_out:delay:delay all=%ld err=%d\n",delay, err);
  gettimeofday(&now, 0);
  printf("audio_alsa_out:delay: Time = %ld.%ld\n", now.tv_sec, now.tv_usec);
  printf("audio_alsa_out:delay:FINISHED\n");
#endif
  return delay;

}
/*
 * Handle over/under-run
 */
static void xrun(alsa_driver_t *this) 
{
  //snd_pcm_status_t *status;
  int res;

  //snd_pcm_status_alloca(&status);
  //if ((res = snd_pcm_status(this->audio_fd, status))<0) {
  //  printf ("audio_alsa_out: status error: %s\n", snd_strerror(res));
  //  return;
  //}
  //snd_pcm_status_dump(status, jcd_out);
  if (snd_pcm_state(this->audio_fd) == SND_PCM_STATE_XRUN) {
    //struct timeval now, diff, tstamp;
    //gettimeofday(&now, 0);
    //snd_pcm_status_get_trigger_tstamp(status, &tstamp);
    //timersub(&now, &tstamp, &diff);
    //printf ("audio_alsa_out: xrun!!! (at least %.3f ms long)\n", diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
    printf ("audio_alsa_out: XRUN!!!\n");
    if ((res = snd_pcm_prepare(this->audio_fd))<0) {
      printf ("audio_alsa_out: xrun: prepare error: %s", snd_strerror(res));
      return;
    }
    return;         /* ok, data should be accepted again */
  }
}

/*
 * Write audio data to output buffer (blocking using snd_pcm_wait)
 */
static int ao_alsa_write(ao_driver_t *this_gen,int16_t *data, uint32_t count)
{
  snd_pcm_sframes_t result;
  snd_pcm_status_t *pcm_stat;
  snd_pcm_state_t    state;
#ifdef LOG_DEBUG
  struct timeval now;
#endif
  int wait_result;
  int res;
  uint8_t *buffer=(uint8_t *)data;
  snd_pcm_uframes_t number_of_frames = (snd_pcm_uframes_t) count;
  alsa_driver_t *this = (alsa_driver_t *) this_gen;

#ifdef LOG_DEBUG
  printf("audio_alsa_out:write:ENTERED\n");
  gettimeofday(&now, 0);
  printf("audio_alsa_out:write: Time = %ld.%ld\n", now.tv_sec, now.tv_usec);
  printf("audio_alsa_out:write:count=%u\n",count);
#endif
  snd_pcm_status_alloca(&pcm_stat);
  state = snd_pcm_state(this->audio_fd);
  if (state == SND_PCM_STATE_XRUN) {
#ifdef LOG_DEBUG
    printf("audio_alsa_out:write:XRUN before\n");
    snd_pcm_status(this->audio_fd, pcm_stat);
    snd_pcm_status_dump(pcm_stat, jcd_out); 
#endif
    if ((res = snd_pcm_prepare(this->audio_fd))<0) {
      XINE_ASSERT (0,"audio_alsa_out: xrun: prepare error: %s", snd_strerror(res));
    }
    state = snd_pcm_state(this->audio_fd);
#ifdef LOG_DEBUG
    printf("audio_alsa_out:write:XRUN after\n");
#endif
  } 
  if ( (state != SND_PCM_STATE_PREPARED) &&
       (state != SND_PCM_STATE_RUNNING) &&
       (state != SND_PCM_STATE_DRAINING) ) {
         printf("audio_alsa_out:write:BAD STATE, state = %d\n",state);
  }
        
  while( number_of_frames > 0) {
    if ( (state == SND_PCM_STATE_RUNNING) ) {
#ifdef LOG_DEBUG
      printf("audio_alsa_out:write:loop:waiting for Godot\n");
#endif
      wait_result = snd_pcm_wait(this->audio_fd, 1000000);
#ifdef LOG_DEBUG
      printf("audio_alsa_out:write:loop:wait_result=%d\n",wait_result);
#endif
    }
    result = snd_pcm_writei(this->audio_fd, buffer, number_of_frames);
    if (result < 0) {
#ifdef LOG_DEBUG
      printf("audio_alsa_out:write:result=%ld:%s\n",result, snd_strerror(result));
#endif
      state = snd_pcm_state(this->audio_fd);
      if ( (state != SND_PCM_STATE_PREPARED) &&
           (state != SND_PCM_STATE_RUNNING) &&
           (state != SND_PCM_STATE_DRAINING) ) {
        printf("audio_alsa_out:write:BAD STATE2, state = %d, going to try XRUN\n",state);
        if ((res = snd_pcm_prepare(this->audio_fd))<0) {
          XINE_ASSERT(0, "audio_alsa_out: xrun: prepare error: %s", snd_strerror(res));
        }
      }
    }
    if (result > 0) {
      number_of_frames -= result;
      buffer += result * this->bytes_per_frame;
    }
  }
  if ( (state == SND_PCM_STATE_RUNNING) ) {
#ifdef LOG_DEBUG
    printf("audio_alsa_out:write:loop:waiting for Godot2\n");
#endif
    wait_result = snd_pcm_wait(this->audio_fd, 1000000);
#ifdef LOG_DEBUG
    printf("audio_alsa_out:write:loop:wait_result=%d\n",wait_result);
#endif
  }
#ifdef LOG_DEBUG
  gettimeofday(&now, 0);
  printf("audio_alsa_out:write: Time = %ld.%ld\n", now.tv_sec, now.tv_usec);
  printf("audio_alsa_out:write:FINISHED\n");
#endif
  return 1; /* audio samples were processed ok */
}

/*
 * This is called when the decoder no longer uses the audio
 */
static void ao_alsa_close(ao_driver_t *this_gen)
{
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  if(this->audio_fd) snd_pcm_close(this->audio_fd);
  this->audio_fd = NULL;
  this->has_pause_resume = 0; /* This is set at open time */
}

/*
 * Find out what output modes + capatilities are supported
 */
static uint32_t ao_alsa_get_capabilities (ao_driver_t *this_gen) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->capabilities;
}

/*
 * Shut down audio output driver plugin and free all resources allocated
 */
static void ao_alsa_exit(ao_driver_t *this_gen)
{
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  void          *p;

  pthread_mutex_destroy(&this->mixer.mutex);
  /*
   * Destroy the mixer thread and cleanup the mixer, so that
   * any child processes (such as xscreensaver) cannot inherit
   * the mixer's handle and keep it open.
   * By rejoining the mixer thread, we remove a race condition
   * between closing the handle and spawning the child process
   * (i.e. xscreensaver).
   */
  if(this->mixer.handle) {
    pthread_cancel(this->mixer.thread);
    pthread_join(this->mixer.thread, &p);
    snd_mixer_close(this->mixer.handle);
  }

  if (this->audio_fd) snd_pcm_close(this->audio_fd);
  free (this);
}

/*
 * Get a property of audio driver
 */
static int ao_alsa_get_property (ao_driver_t *this_gen, int property) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  int err;

  switch(property) {
  case AO_PROP_MIXER_VOL:
  case AO_PROP_PCM_VOL:
    if(this->mixer.elem) {

      pthread_mutex_lock(&this->mixer.mutex);

      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT,
						    &this->mixer.left_vol)) < 0) {
	printf("audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	goto __done;
      }
      
      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT,
						    &this->mixer.right_vol)) < 0) {
	printf("audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	goto __done;
      }
      
    __done:
      pthread_mutex_unlock(&this->mixer.mutex);

      return (((ao_alsa_get_percent_from_volume(this->mixer.left_vol, 
						this->mixer.min, this->mixer.max)) +
	       (ao_alsa_get_percent_from_volume(this->mixer.right_vol, 
						this->mixer.min, this->mixer.max))) /2);
    }
    break;
    
  case AO_PROP_MUTE_VOL:
    return (this->mixer.mute) ? 1 : 0;
    break;
  }
  
  return 0;
}

/*
 * Set a property of audio driver
 */
static int ao_alsa_set_property (ao_driver_t *this_gen, int property, int value) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  int err;

  switch(property) {
  case AO_PROP_MIXER_VOL:
  case AO_PROP_PCM_VOL:
    if(this->mixer.elem) {

      pthread_mutex_lock(&this->mixer.mutex);

      this->mixer.left_vol = this->mixer.right_vol = ao_alsa_get_volume_from_percent(value, this->mixer.min, this->mixer.max);
      
      if((err = snd_mixer_selem_set_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT,
						    this->mixer.left_vol)) < 0) {
	printf("audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	pthread_mutex_unlock(&this->mixer.mutex);
	return ~value;
      }
      
      if((err = snd_mixer_selem_set_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT,
						    this->mixer.right_vol)) < 0) {
	printf("audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	pthread_mutex_unlock(&this->mixer.mutex);
	return ~value;
      }
      pthread_mutex_unlock(&this->mixer.mutex);
      return value;
    }
    break;

  case AO_PROP_MUTE_VOL:
    if(this->mixer.elem) {
      int sw;
      int old_mute = this->mixer.mute;
      
      pthread_mutex_lock(&this->mixer.mutex);

      this->mixer.mute = (value) ? MIXER_MASK_STEREO : 0;
      
      if ((this->mixer.mute != old_mute) 
	  && snd_mixer_selem_has_playback_switch(this->mixer.elem)) {
	if (snd_mixer_selem_has_playback_switch_joined(this->mixer.elem)) {
	  snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &sw);
	  snd_mixer_selem_set_playback_switch_all(this->mixer.elem, !sw);
	} else {
	  if (this->mixer.mute & MIXER_MASK_LEFT) {
	    snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &sw);
	    snd_mixer_selem_set_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, !sw);
	  }
	  if (SND_MIXER_SCHN_FRONT_RIGHT != SND_MIXER_SCHN_UNKNOWN && 
	      (this->mixer.mute & MIXER_MASK_RIGHT)) {
	    snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT, &sw);
	    snd_mixer_selem_set_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT, !sw);
	  }
	}
      }
      
      pthread_mutex_unlock(&this->mixer.mutex);
      return value;
    }
    return ~value;
    break;
  }

  return ~value;
}

/*
 * Misc control operations
 */
static int ao_alsa_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  int result;

  /* Alsa 0.9.x pause and resume is not stable enough at the moment.
   * Use snd_pcm_drop and restart instead.
   */
  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
    if (this->audio_fd > 0) {
      if (this->has_pause_resume) {
        if ((result=snd_pcm_pause(this->audio_fd, 1)) < 0) {
          printf("audio_alsa_out: Pause call failed err=%d\n", result);
          this->has_pause_resume = 0;
          ao_alsa_ctrl(this_gen, AO_CTRL_PLAY_PAUSE);
        }
      } else {
        if ((result=snd_pcm_reset(this->audio_fd)) < 0) {
          printf("audio_alsa_out: Reset call failed err=%d\n",result);
        }
        if ((result=snd_pcm_drain(this->audio_fd)) < 0) {
          printf("audio_alsa_out: Drain call failed err=%d\n",result);
        }
        if ((result=snd_pcm_prepare(this->audio_fd)) < 0) {
          printf("audio_alsa_out: Prepare call failed err=%d\n",result);
        }
      }
    }
    break;

  case AO_CTRL_PLAY_RESUME:
    if (this->audio_fd > 0) {
      if (this->has_pause_resume) {
        if ((result=snd_pcm_pause(this->audio_fd, 0)) < 0) {
          printf("audio_alsa_out: Resume call failed err=%d\n",result);
          this->has_pause_resume = 0;
          ao_alsa_ctrl(this_gen, AO_CTRL_PLAY_RESUME);
        }
      }
    }
    break;

  case AO_CTRL_FLUSH_BUFFERS:
    if (this->audio_fd > 0) {
      if ((result=snd_pcm_drop(this->audio_fd)) < 0) {
        printf("audio_alsa_out: Drop call failed err=%d\n",result);
      }
      if ((result=snd_pcm_prepare(this->audio_fd)) < 0) {
        printf("audio_alsa_out: Prepare call failed err=%d\n",result);
      }
     }
    break;
  }

  return 0;
}

/*
 * Initialize mixer
 */
static void ao_alsa_mixer_init(ao_driver_t *this_gen) {
  alsa_driver_t        *this = (alsa_driver_t *) this_gen;
  config_values_t      *config = this->class->xine->config;
  char                 *pcm_device;
  snd_ctl_card_info_t  *hw_info;
  snd_ctl_t            *ctl_handle;
  int                   err;
  void                 *mixer_sid;
  snd_mixer_elem_t     *elem;
  int                   mixer_n_selems = 0;
  snd_mixer_selem_id_t *sid;
  int                   loop = 0;
  int                   found;
  int                   sw;

  snd_ctl_card_info_alloca(&hw_info);
  pcm_device = config->register_string(config,
				       "audio.alsa_default_device",
				       "default",
				       _("device used for mono output"),
				       NULL,
				       10, NULL,
				       NULL);
  
  if ((err = snd_ctl_open (&ctl_handle, pcm_device, 0)) < 0) {
    printf ("audio_alsa_out: snd_ctl_open(): %s\n", snd_strerror(err));
    return;
  }
  
  if ((err = snd_ctl_card_info (ctl_handle, hw_info)) < 0) {
    printf ("audio_alsa_out: snd_ctl_card_info(): %s\n", snd_strerror(err));
    snd_ctl_close(ctl_handle);
    return;
  }
  
  snd_ctl_close (ctl_handle);

  /* 
   * Open mixer device
   */
  if ((err = snd_mixer_open (&this->mixer.handle, 0)) < 0) {
    printf ("audio_alsa_out: snd_mixer_open(): %s\n", snd_strerror(err));
    return;
  }
  
  if ((err = snd_mixer_attach (this->mixer.handle, pcm_device)) < 0) {
    printf ("audio_alsa_out: snd_mixer_attach(): %s\n", snd_strerror(err));
    snd_mixer_close(this->mixer.handle);
    return;
  }
  
  if ((err = snd_mixer_selem_register (this->mixer.handle, NULL, NULL)) < 0) {
    printf ("audio_alsa_out: snd_mixer_selem_register(): %s\n", snd_strerror(err));
    snd_mixer_close(this->mixer.handle);
    return;
  }

  if ((err = snd_mixer_load (this->mixer.handle)) < 0) {
    printf ("audio_alsa_out: snd_mixer_load(): %s\n", snd_strerror(err));
    snd_mixer_close(this->mixer.handle);
    return;
  }
  
  mixer_sid = alloca(snd_mixer_selem_id_sizeof() * snd_mixer_get_count(this->mixer.handle));
  if (mixer_sid == NULL) {
    printf ("audio_alsa_out: alloca() failed: %s\n", strerror(errno));
    snd_mixer_close(this->mixer.handle);
    return;
  }
  
 __again:

  found = 0;
  mixer_n_selems = 0;
  for (elem = snd_mixer_first_elem(this->mixer.handle); elem; elem = snd_mixer_elem_next(elem)) {
    sid = (snd_mixer_selem_id_t *)(((char *)mixer_sid) + snd_mixer_selem_id_sizeof() * mixer_n_selems);
    
    if (!snd_mixer_selem_is_active(elem))
      continue;
    
    snd_mixer_selem_get_id(elem, sid);
    mixer_n_selems++;
    
    if(!strcmp((snd_mixer_selem_get_name(elem)), this->mixer.name)) {
      
      //      printf("found %s\n", snd_mixer_selem_get_name(elem));
      
      this->mixer.elem = elem;
      
      snd_mixer_selem_get_playback_volume_range(this->mixer.elem, 
						&this->mixer.min, &this->mixer.max);
      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT,
						    &this->mixer.left_vol)) < 0) {
	printf("audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	snd_mixer_close(this->mixer.handle);
	return;
      }
      
      if((err = snd_mixer_selem_get_playback_volume(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT,
						    &this->mixer.right_vol)) < 0) {
	printf ("audio_alsa_out: snd_mixer_selem_get_playback_volume(): %s\n",  snd_strerror(err));
	snd_mixer_close(this->mixer.handle);
	return;
      }
      
      /* Channels mute */
      this->mixer.mute = 0;
      if(snd_mixer_selem_has_playback_switch(this->mixer.elem)) {

	if (snd_mixer_selem_has_playback_switch_joined(this->mixer.elem)) {
	  snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &sw);
	  if(!sw)
	    this->mixer.mute = MIXER_MASK_STEREO;
	} 
	else {
	  if (this->mixer.mute & MIXER_MASK_LEFT) {
	    snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_LEFT, &sw);
	    if(!sw)
	      this->mixer.mute |= MIXER_MASK_LEFT;
	  }
	  if (SND_MIXER_SCHN_FRONT_RIGHT != SND_MIXER_SCHN_UNKNOWN && 
	      (this->mixer.mute & MIXER_MASK_RIGHT)) {
	    snd_mixer_selem_get_playback_switch(this->mixer.elem, SND_MIXER_SCHN_FRONT_RIGHT, &sw);
	    if(!sw)
	      this->mixer.mute |= MIXER_MASK_RIGHT;
	  }
	}

	this->capabilities |= AO_CAP_MUTE_VOL;
      }

      found++;

      goto __mixer_found;
    }
  }
  
  if(loop)
    goto __mixer_found; /* Yes, untrue but... ;-) */
  
  if(!strcmp(this->mixer.name, "PCM")) {
    config->update_string(config, "audio.alsa_mixer_name", "Master");
    loop++;
  }
  else {
    config->update_string(config, "audio.alsa_mixer_name", "PCM");
  }
  
  this->mixer.name = config->register_string(config,
                                             "audio.alsa_mixer_name",
                                             "PCM",
                                             _("alsa mixer device"),
                                             NULL,
                                             10, NULL,
                                             NULL);
  
  goto __again;

 __mixer_found:
  
  /* 
   * Ugly: yes[*]  no[ ]
   */
  if(found) {
    if(!strcmp(this->mixer.name, "Master"))
      this->capabilities |= AO_CAP_MIXER_VOL;
    else
      this->capabilities |= AO_CAP_PCM_VOL;
  }

  /* Create a thread which wait/handle mixer events */
  {
    pthread_attr_t       pth_attrs;
    struct sched_param   pth_params;
    
    pthread_attr_init(&pth_attrs);
    
    pthread_attr_getschedparam(&pth_attrs, &pth_params);
    pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
    pthread_attr_setschedparam(&pth_attrs, &pth_params);
    
    pthread_create(&this->mixer.thread, &pth_attrs, ao_alsa_handle_event_thread, (void *) this);
  }

}

/*
 * Initialize plugin
 */
static ao_driver_t *open_plugin (audio_driver_class_t *class_gen, const void *data) {

  alsa_class_t        *class = (alsa_class_t *) class_gen;
  config_values_t     *config = class->xine->config;
  alsa_driver_t       *this;
  int                  err;
  char                *pcm_device;
  snd_pcm_hw_params_t *params;

  this = (alsa_driver_t *) malloc (sizeof (alsa_driver_t));
  this->class = class;
  snd_pcm_hw_params_alloca(&params);
  /* Fill the .xinerc file with options */ 
  pcm_device = config->register_string(config,
				       "audio.alsa_default_device",
				       "default",
				       _("device used for mono output"),
				       NULL,
				       10, NULL,
				       NULL);
  pcm_device = config->register_string(config,
				       "audio.alsa_front_device",
				       "default",
				       _("device used for stereo output"),
				       NULL,
				       10, NULL,
				       NULL);
  pcm_device = config->register_string(config,
				       "audio.alsa_surround40_device",
				       "surround40",
				       _("device used for 4-channel output"),
				       NULL,
				       10, NULL,
				       NULL);
  pcm_device = config->register_string(config,
				       "audio.alsa_surround50_device",
				       "surround51",
				       _("device used for 5-channel output"),
				       NULL,
                                       10, NULL,
				       NULL);
  pcm_device = config->register_string(config,
				       "audio.alsa_surround51_device",
				       "surround51",
				       _("device used for 5.1-channel output"),
				       NULL,
                                       10,  NULL,
				       NULL);
  pcm_device = config->register_string(config,
				       "audio.alsa_a52_device",
				       "iec958:AES0=0x6,AES1=0x82,AES2=0x0,AES3=0x2",
				       _("device used for 5.1-channel output"),
				       NULL,
				       10, NULL,
				       NULL);

  /* Use the default device to open first */
  pcm_device = config->register_string(config,
				       "audio.alsa_default_device",
				       "default",
				       _("device used for mono output"),
				       NULL,
				       10, NULL,
				       NULL);
 
  /*
   * find best device driver/channel
   */
  /*
   * open that device
   */
  err=snd_pcm_open(&this->audio_fd, pcm_device, SND_PCM_STREAM_PLAYBACK, 0);
  if(err <0 ) {
    xine_log (this->class->xine, XINE_LOG_MSG,
          "snd_pcm_open() failed: %d", err); 
    xine_log (this->class->xine, XINE_LOG_MSG,
          ">>> Check if another program don't already use PCM <<<"); 
    return NULL; 
  }

  /*
   * configure audio device
   */
  err = snd_pcm_hw_params_any(this->audio_fd, params);
  if (err < 0) {
    printf ("audio_alsa_out: broken configuration for this PCM: no configurations available\n");
    return NULL;
  }
  err = snd_pcm_hw_params_set_access(this->audio_fd, params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err < 0) {
    printf ("audio_alsa_out: access type not available");
    return NULL;
  }

  this->capabilities = 0;

  printf ("audio_alsa_out : supported modes are ");
  if (!(snd_pcm_hw_params_test_format(this->audio_fd, params, SND_PCM_FORMAT_U8))) {
    this->capabilities |= AO_CAP_8BITS;
    printf ("8bit ");
  }
  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 1))) {
    this->capabilities |= AO_CAP_MODE_MONO;
    printf ("mono ");
  }
  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 2))) {
    this->capabilities |= AO_CAP_MODE_STEREO;
    printf ("stereo ");
  }
  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 4)) &&
        config->register_bool (config,
                               "audio.four_channel",
                               0,
                               _("used to inform xine about what the sound card can do"),
                               NULL,
                               0, NULL,
                               NULL) ) {
    this->capabilities |= AO_CAP_MODE_4CHANNEL;
    printf ("4-channel ");
  } else {
    printf ("(4-channel not enabled in xine config) " );
  }
  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 5)) && 
        config->register_bool (config,
                               "audio.five_channel",
                               0,
                               _("used to inform xine about what the sound card can do"),
                               NULL,
                               0, NULL,
                               NULL) ) {
    this->capabilities |= AO_CAP_MODE_5CHANNEL;
    printf ("5-channel ");
  } else {
    printf ("(5-channel not enabled in xine config) " );
  }
  if (!(snd_pcm_hw_params_test_channels(this->audio_fd, params, 6)) && 
        config->register_bool (config,
                               "audio.five_lfe_channel",
                               0,
                               _("used to inform xine about what the sound card can do"),
                               NULL,
                               0, NULL,
                               NULL) ) {
    this->capabilities |= AO_CAP_MODE_5_1CHANNEL;
    printf ("5.1-channel ");
    } else {
    printf ("(5.1-channel not enabled in xine config) " );
  }
 
  this->has_pause_resume = 0; /* This is checked at open time instead */

  snd_pcm_close (this->audio_fd);
  this->audio_fd=NULL;
  this->output_sample_rate = 0;
  if (config->register_bool (config,
                               "audio.a52_pass_through",
                               0,
                               _("used to inform xine about what the sound card can do"),
                               NULL,
                               0, NULL,
                               NULL) ) {
    this->capabilities |= AO_CAP_MODE_A52;
    this->capabilities |= AO_CAP_MODE_AC5;
    printf ("a/52 and DTS pass-through ");
  } else {
    printf ("(a/52 and DTS pass-through not enabled in xine config)");
  }
  printf ("\n");

  /* printf("audio_alsa_out: capabilities 0x%X\n",this->capabilities); */

  this->mixer.name = config->register_string(config,
                                             "audio.alsa_mixer_name",
                                             "PCM",
                                             _("alsa mixer device"),
                                             NULL,
                                             10, NULL,
                                             NULL);

  pthread_mutex_init(&this->mixer.mutex, NULL);
  ao_alsa_mixer_init(&this->ao_driver);

  this->ao_driver.get_capabilities    = ao_alsa_get_capabilities;
  this->ao_driver.get_property        = ao_alsa_get_property;
  this->ao_driver.set_property        = ao_alsa_set_property;
  this->ao_driver.open                = ao_alsa_open;
  this->ao_driver.num_channels        = ao_alsa_num_channels;
  this->ao_driver.bytes_per_frame     = ao_alsa_bytes_per_frame;
  this->ao_driver.delay               = ao_alsa_delay;
  this->ao_driver.write	 	      = ao_alsa_write;
  this->ao_driver.close               = ao_alsa_close;
  this->ao_driver.exit                = ao_alsa_exit;
  this->ao_driver.get_gap_tolerance   = ao_alsa_get_gap_tolerance;
  this->ao_driver.control	      = ao_alsa_ctrl;

  return &this->ao_driver;
}

/*
 * class functions
 */

static char* get_identifier (audio_driver_class_t *this_gen) {
  return "alsa";
}

static char* get_description (audio_driver_class_t *this_gen) {
  return _("xine audio output plugin using alsa-compliant audio devices/drivers");
}

static void dispose_class (audio_driver_class_t *this_gen) {

  alsa_class_t *this = (alsa_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {

  alsa_class_t        *this;

  this = (alsa_class_t *) malloc (sizeof (alsa_class_t));

  this->driver_class.open_plugin     = open_plugin;
  this->driver_class.get_identifier  = get_identifier;
  this->driver_class.get_description = get_description;
  this->driver_class.dispose         = dispose_class;

/*  this->config = xine->config; */
  this->xine = xine;
   return this;
 }

static ao_info_t ao_info_alsa = {
  10
};

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_OUT, AO_OUT_ALSA_IFACE_VERSION, "alsa", XINE_VERSION_CODE, &ao_info_alsa, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
