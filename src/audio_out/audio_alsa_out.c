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
 * Credits go 
 * - for the SPDIF A/52 sync part
 * - frame size calculation added (16-08-2001)
 * (c) 2001 Andy Lo A Foe <andy@alsaplayer.org>
 * for initial ALSA 0.9.x support.
 *     adding MONO/STEREO/4CHANNEL/5CHANNEL/5.1CHANNEL analogue support.
 * (c) 2001 James Courtier-Dutton <James@superbug.demon.co.uk>
 *
 * 
 * $Id: audio_alsa_out.c,v 1.46 2002/02/08 13:13:47 f1rmb Exp $
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
#include <sys/asoundlib.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <pthread.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "audio_out.h"

#ifndef AFMT_S16_NE
# if defined(sparc) || defined(__sparc__) || defined(PPC)
/* Big endian machines */
#  define AFMT_S16_NE AFMT_S16_BE
# else
#  define AFMT_S16_NE AFMT_S16_LE
# endif
#endif

#define AO_OUT_ALSA_IFACE_VERSION 3

#define GAP_TOLERANCE             5000

#define MIXER_MASK_LEFT           (1 << 0)
#define MIXER_MASK_RIGHT          (1 << 1)
#define MIXER_MASK_STEREO         (MIXER_MASK_LEFT|MIXER_MASK_RIGHT)

typedef struct alsa_driver_s {

  ao_driver_t   ao_driver;

  config_values_t *config;

  snd_pcm_t    *audio_fd;
  int           capabilities;
  int           open_mode;

  int32_t       output_sample_rate, input_sample_rate;
  double        sample_rate_factor;
  uint32_t      num_channels;
  uint32_t      bits_per_sample;
  uint32_t      bytes_per_frame;
  uint32_t      bytes_in_buffer;      /* number of bytes writen to audio hardware   */

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
  config_values_t *config = this->config;
  char                 *pcm_device;
  snd_pcm_stream_t      direction = SND_PCM_STREAM_PLAYBACK; 
  snd_pcm_hw_params_t  *params;
  snd_pcm_sw_params_t  *swparams;
  snd_pcm_sframes_t     buffer_size;
  snd_pcm_sframes_t     period_size,tmp;
  /*
  snd_aes_iec958_t      spdif;
  snd_ctl_elem_value_t *ctl;
  snd_ctl_t            *ctl_handle;
  snd_pcm_info_t       *info;
  char                  ctl_name[12];
  int                   ctl_card;
  */
  int                   err, step;
 // int                 open_mode=1; //NONBLOCK
  int                   open_mode=0; //BLOCK

  snd_pcm_hw_params_alloca(&params);
  snd_pcm_sw_params_alloca(&swparams);
  err = snd_output_stdio_attach(&jcd_out, stderr, 0);

  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    pcm_device = config->register_string(config,
                                         "audio.alsa_default_device",
                                         "default",
                                         "device used for mono output",
                                         NULL,
                                         NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    pcm_device = config->register_string(config,
                                         "audio.alsa_front_device",
                                         "default",
                                         "device used for stereo output",
                                         NULL,
                                         NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_4CHANNEL:
    this->num_channels = 4;
    pcm_device = config->register_string(config,
                                         "audio.alsa_surround40_device",
                                         "surround40",
                                         "device used for 4-channel output",
                                         NULL,
                                         NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_5CHANNEL:
    this->num_channels = 5;
    pcm_device = config->register_string(config,
                                         "audio.alsa_surround50_device",
                                         "surround51",
                                         "device used for 5-channel output",
                                         NULL,
                                         NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_5_1CHANNEL:
    this->num_channels = 6;
    pcm_device = config->register_string(config,
                                         "audio.alsa_surround51_device",
                                         "surround51",
                                         "device used for 5.1-channel output",
                                         NULL,
                                         NULL,
                                         NULL);
    break;
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
    this->num_channels = 2;
    pcm_device = config->register_string(config,
                                         "audio.alsa_a52_device",
                                         "iec958:AES0=0x6,AES1=0x82,AES2=0x0,AES3=0x2",
                                         "device used for 5.1-channel output",
                                         NULL,
                                         NULL,
                                         NULL);
    break;
  default:
    printf ("audio_alsa_out: ALSA Driver does not support the requested mode: 0x%X\n",mode);
    return 0;
  } 

  printf("audio_alsa_out: Audio Device name = %s\n",pcm_device);
  printf("audio_alsa_out: Number of channels = %d\n",this->num_channels);

  if (this->audio_fd != NULL) {
    xlerror ("Already open...WHY!");
    snd_pcm_close (this->audio_fd);
  }

  this->open_mode              = mode;
  this->input_sample_rate      = rate;
  this->bits_per_sample        = bits;
  this->bytes_in_buffer        = 0;
  /* FIXME: Can use an ALSA function here */
  this->bytes_per_frame=(this->bits_per_sample*this->num_channels)/8;
  /*
   * open audio device
   */

  err=snd_pcm_open(&this->audio_fd, pcm_device, direction, open_mode);      
  if(err <0 ) {                                                           
    printf ("audio_alsa_out: snd_pcm_open() failed: %s\n", snd_strerror(err));               
    printf ("audio_alsa_out: >>> check if another program don't already use PCM <<<\n");     
    return 0;
  }
/*
 * This is all not needed in the new ALSA API
 * Beginning of old alsa section.
 * 
  if ((mode & AO_CAP_MODE_A52) || (mode & AO_CAP_MODE_AC5)) {
    snd_pcm_info_alloca(&info);

    if ((err = snd_pcm_info(this->audio_fd, info)) < 0) {
      fprintf(stderr, "info: %s\n", snd_strerror(err));
      goto __close;
    }
    printf ("audio_alsa_out: device: %d, subdevice: %d\n", 
	    snd_pcm_info_get_device(info),
	    snd_pcm_info_get_subdevice(info));

    spdif.status[0] = IEC958_AES0_NONAUDIO |
                      IEC958_AES0_CON_EMPHASIS_NONE;
    spdif.status[1] = IEC958_AES1_CON_ORIGINAL |
                      IEC958_AES1_CON_PCM_CODER;
    spdif.status[2] = 0;
    spdif.status[3] = IEC958_AES3_CON_FS_48000;

    snd_ctl_elem_value_alloca(&ctl);
    snd_ctl_elem_value_set_interface(ctl, SND_CTL_ELEM_IFACE_PCM);
    snd_ctl_elem_value_set_device(ctl,snd_pcm_info_get_device(info));
    snd_ctl_elem_value_set_subdevice(ctl, snd_pcm_info_get_subdevice(info));
    snd_ctl_elem_value_set_name(ctl, SND_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM));
    snd_ctl_elem_value_set_iec958(ctl, &spdif);
    ctl_card = snd_pcm_info_get_card(info);
    if (ctl_card < 0) {
      printf ("audio_alsa_out: unable to setup the IEC958 (S/PDIF) interface - PCM has no assigned card");
      goto __close;
    }
    sprintf(ctl_name, "hw:%d", ctl_card);
    printf("hw:%d\n", ctl_card);
    if ((err = snd_ctl_open(&ctl_handle, ctl_name, 0)) < 0) {
      printf ("audio_alsa_out: unable to open the control interface '%s':%s", 
	      ctl_name, snd_strerror(err));
      goto __close;
    }
    if ((err = snd_ctl_elem_write(ctl_handle, ctl)) < 0) {
      printf ("audio_alsa_out: unable to update the IEC958 control: %s", 
	      snd_strerror(err));
      goto __close;
    }
    snd_ctl_close(ctl_handle);
  }
 *
 * End of old alsa section.
 */
  /* We wanted non blocking open but now put it back to normal */
  snd_pcm_nonblock(this->audio_fd, 0);
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
  err = snd_pcm_hw_params_set_format(this->audio_fd, params, bits == 16 ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_U8);
  if (err < 0) {
    printf ("audio_alsa_out: sample format non available\n");
    goto __close;
  }
  err = snd_pcm_hw_params_set_channels(this->audio_fd, params, this->num_channels);
  if (err < 0) {
    printf ("audio_alsa_out: channels count non available\n");
    goto __close;
  }
  err = snd_pcm_hw_params_set_rate_near(this->audio_fd, params, rate, 0);
    if (err < 0) {
      printf ("audio_alsa_out: rate not available\n");
      goto __close;
    }
  buffer_size = snd_pcm_hw_params_set_buffer_size_near(this->audio_fd, params,
                                                       500000);
  if (buffer_size < 0) {
    printf ("audio_alsa_out: buffer time not available\n");
    goto __close;
  }
  step = 2;
  period_size = 128;
  do {
    period_size *= 2;
    tmp = snd_pcm_hw_params_set_period_size_near(this->audio_fd, params,
                                                 period_size, 0);
    printf("audio_alsa_out:open:period_size=%ld tmp=%ld\n",period_size,tmp);
    
    if (period_size < 0) {
      printf ("audio_alsa_out: period size not available");
      goto __close;
    }
  } while (period_size <= (buffer_size/2) && (period_size != tmp));
  if (buffer_size == period_size) {
    printf ("audio_alsa_out: buffer time and period time match, could not use\n");
    goto __close;
  }
  if ((err = snd_pcm_hw_params(this->audio_fd, params)) < 0) {
    printf ("audio_alsa_out: pcm hw_params failed: %s\n", snd_strerror(err));
    goto __close;
  }
  this->output_sample_rate = this->input_sample_rate;
  this->sample_rate_factor = (double) this->output_sample_rate / (double) this->input_sample_rate;
  /*
   * audio buffer size handling
   */
  /* Copy current parameters into swparams */
  snd_pcm_sw_params_current(this->audio_fd, swparams);
  tmp=snd_pcm_sw_params_set_xfer_align(this->audio_fd, swparams, period_size);
  tmp=snd_pcm_sw_params_set_avail_min(this->audio_fd, swparams, 1);
  tmp=snd_pcm_sw_params_set_start_threshold(this->audio_fd, swparams, period_size);

  /* Install swparams into current parameters */
  snd_pcm_sw_params(this->audio_fd, swparams);
  snd_pcm_dump_setup(this->audio_fd, jcd_out); 
  snd_pcm_sw_params_dump(swparams, jcd_out);
  return this->output_sample_rate;
__close:
  snd_pcm_close (this->audio_fd);
  this->audio_fd=NULL;
  return 0;
}

static int ao_alsa_num_channels(ao_driver_t *this_gen)
{
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_alsa_bytes_per_frame(ao_driver_t *this_gen)
{
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_alsa_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

static int ao_alsa_delay (ao_driver_t *this_gen) 
{
  snd_pcm_status_t  *pcm_stat;
  snd_pcm_sframes_t delay;

  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  snd_pcm_status_alloca(&pcm_stat);
  snd_pcm_status(this->audio_fd, pcm_stat);
  /* Dump ALSA info to stderr */
  /* snd_pcm_status_dump(pcm_stat, jcd_out);  */
  delay=snd_pcm_status_get_delay( pcm_stat );
  /* printf("audio_alsa_out:delay:delay=%ld\n",delay); */
  return delay;
}

static void xrun(alsa_driver_t *this) 
{
  snd_pcm_status_t *status;
  int res;

  snd_pcm_status_alloca(&status);
  if ((res = snd_pcm_status(this->audio_fd, status))<0) {
    printf ("audio_alsa_out: status error: %s\n", snd_strerror(res));
    return;
  }
  if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
    struct timeval now, diff, tstamp;
    gettimeofday(&now, 0);
    snd_pcm_status_get_trigger_tstamp(status, &tstamp);
    timersub(&now, &tstamp, &diff);
    printf ("audio_alsa_out: xrun!!! (at least %.3f ms long)\n", diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
    if ((res = snd_pcm_prepare(this->audio_fd))<0) {
      printf ("audio_alsa_out: xrun: prepare error: %s", snd_strerror(res));
      return;
    }
    return;         /* ok, data should be accepted again */
  }
}

static int ao_alsa_write(ao_driver_t *this_gen,int16_t *data, uint32_t count)
{
  snd_pcm_sframes_t result;
  uint8_t *buffer=(uint8_t *)data;
  snd_pcm_uframes_t number_of_frames = (snd_pcm_uframes_t) count;
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  	
  while( number_of_frames > 0) {
    result = snd_pcm_writei(this->audio_fd, buffer, number_of_frames);
    /* printf("audio_alsa_out:write:result=%ld\n",result); */
    if (result == -EAGAIN || (result >=0 && result < number_of_frames)) {
      /* printf("audio_alsa_out:write:result=%ld\n",result); */
      snd_pcm_wait(this->audio_fd, 1000);
    } else if (result == -EPIPE) {
      xrun(this);
    }
    if (result > 0) {
      number_of_frames -= result;
      /* FIXME: maybe not *2 as int16 */
      buffer += result * this->bytes_per_frame;
    }
  }
  /* FIXME: What should this really be? */
  return 1;
}

static void ao_alsa_close(ao_driver_t *this_gen)
{
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  if(this->audio_fd) snd_pcm_close(this->audio_fd);
  this->audio_fd = NULL;
}

static uint32_t ao_alsa_get_capabilities (ao_driver_t *this_gen) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_alsa_exit(ao_driver_t *this_gen)
{
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  void          *p;

  config_values_t *config = this->config;

  config->update_num (config, "audio.alsa_mixer_volume", 
		   (((ao_alsa_get_percent_from_volume(this->mixer.left_vol, 
						      this->mixer.min, this->mixer.max)) + 
		     (ao_alsa_get_percent_from_volume(this->mixer.right_vol, 
						      this->mixer.min, this->mixer.max))) /2));
  config->save(config);

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
 *
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

static void ao_alsa_mixer_init(ao_driver_t *this_gen) {
  alsa_driver_t        *this = (alsa_driver_t *) this_gen;
  config_values_t      *config = this->config;
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
				       "device used for mono output",
				       NULL,
				       NULL,
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
  
  config->save(config);
  
  this->mixer.name = config->register_string(config,
                                             "audio.alsa_mixer_name",
                                             "PCM",
                                             "alsa mixer device",
                                             NULL,
                                             NULL,
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

ao_driver_t *init_audio_out_plugin (config_values_t *config) {

  alsa_driver_t *this;
  int              err;
  char             *pcm_device;
  snd_pcm_hw_params_t *params;

  this = (alsa_driver_t *) malloc (sizeof (alsa_driver_t));
  snd_pcm_hw_params_alloca(&params);
  /* Fill the .xinerc file with options */ 
  pcm_device = config->register_string(config,
                                         "audio.alsa_default_device",
                                         "default",
                                         "device used for mono output",
                                         NULL,
                                         NULL,
                                         NULL);
  pcm_device = config->register_string(config,
                                         "audio.alsa_front_device",
                                         "default",
                                         "device used for stereo output",
                                         NULL,
                                         NULL,
                                         NULL);
  pcm_device = config->register_string(config,
                                         "audio.alsa_surround40_device",
                                         "surround40",
                                         "device used for 4-channel output",
                                         NULL,
                                         NULL,
                                         NULL);
  pcm_device = config->register_string(config,
                                         "audio.alsa_surround50_device",
                                         "surround51",
                                         "device used for 5-channel output",
                                         NULL,
                                         NULL,
                                         NULL);
  pcm_device = config->register_string(config,
                                         "audio.alsa_surround51_device",
                                         "surround51",
                                         "device used for 5.1-channel output",
                                         NULL,
                                         NULL,
                                         NULL);
  pcm_device = config->register_string(config,
                                         "audio.alsa_a52_device",
                                         "iec958:AES0=0x6,AES1=0x82,AES2=0x0,AES3=0x2",
                                         "device used for 5.1-channel output",
                                         NULL,
                                         NULL,
                                         NULL);

  /* Use the default device to open first */
  pcm_device = config->register_string(config,
                                         "audio.alsa_default_device",
                                         "default",
                                         "device used for mono output",
                                         NULL,
                                         NULL,
                                         NULL);
 
  /*
   * find best device driver/channel
   */
  /*
   * open that device
   */
  
  err=snd_pcm_open(&this->audio_fd, pcm_device, SND_PCM_STREAM_PLAYBACK, 0);
  if(err <0 ) {
    xlerror("snd_pcm_open() failed: %d", err); 
    xlerror(">>> Check if another program don't already use PCM <<<"); 
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
                               "used to inform xine about what the sound card can do",
                               NULL,
                               NULL,
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
                               "used to inform xine about what the sound card can do",
                               NULL,
                               NULL,
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
                               "used to inform xine about what the sound card can do",
                               NULL,
                               NULL,
                               NULL) ) {
    this->capabilities |= AO_CAP_MODE_5_1CHANNEL;
    printf ("5.1-channel ");
    } else {
    printf ("(5.1-channel not enabled in xine config) " );
  }
 
  snd_pcm_close (this->audio_fd);
  this->audio_fd=NULL;
  this->output_sample_rate = 0;
  if (config->register_bool (config,
                               "audio.a52_pass_through",
                               0,
                               "used to inform xine about what the sound card can do",
                               NULL,
                               NULL,
                               NULL) ) {
    this->capabilities |= AO_CAP_MODE_A52;
    this->capabilities |= AO_CAP_MODE_AC5;
    printf ("a/52 and DTS pass-through ");
  } else {
    printf ("(a/52 and DTS pass-through not enabled in xine config)");
  }
  printf ("\n");

  /* printf("audio_alsa_out: capabilities 0x%X\n",this->capabilities); */

  this->config                        = config;

  this->mixer.name = config->register_string(config,
                                             "audio.alsa_mixer_name",
                                             "PCM",
                                             "alsa mixer device",
                                             NULL,
                                             NULL,
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

  return &this->ao_driver;
}

static ao_info_t ao_info_alsa9 = {
  AO_OUT_ALSA_IFACE_VERSION,
  "alsa09",
  "xine audio output plugin using alsa-compliant audio devices/drivers",
  11
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_alsa9;
}

