/* 
 * Copyright (C) 2000, 2001 the xine project
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
 *
 * Plugin for ALSA Version 0.5.x
 *
 * Credits go
 * for the SPDIF A52 sync part
 * (c) 2000 Andy Lo A Foe <andy@alsaplayer.org>
 *
 * $Id: audio_alsa05_out.c,v 1.12 2001/11/19 13:55:03 hrm Exp $
 */

/* required for swab() */
#define _XOPEN_SOURCE 500

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/asoundlib.h>

#include "xine_internal.h"
#include "audio_out.h"
#include "metronom.h"
#include "resample.h"
#include "xineutils.h"

#define AO_ALSA_IFACE_VERSION   2

#define AUDIO_NUM_FRAGMENTS     6
#define AUDIO_FRAGMENT_SIZE  1536

#define GAP_TOLERANCE        5000

#define MAX_GAP              90000

extern uint32_t xine_debug;


typedef struct alsa_driver_s {

  ao_driver_t ao_driver;

  snd_pcm_t  *front_handle;

  int32_t     output_sample_rate, input_sample_rate;
  uint32_t    num_channels;

  uint32_t    bytes_in_buffer;  /* number of bytes written to audio hardware  */
  uint32_t    bytes_per_frame;

  int         pcm_default_card;
  int         pcm_default_device;

  int         direction;
  int         mode;
  int         start_mode;
  int         stop_mode;
  int         format;
  int         rate;
  int         voices;
  int         interleave;
  int         frag_size;
  int         frag_count;
  int         pcm_len;
  int         ao_mode;

  int         capabilities;

} alsa_driver_t;


static void alsa_set_frag(alsa_driver_t *this, int fragment_size, int fragment_count) {
  snd_pcm_channel_params_t  params;
  snd_pcm_channel_setup_t   setup;
  snd_pcm_format_t          format;
  int err;

  memset(&params, 0, sizeof(params));
  
  params.mode = this->mode;
  params.channel = this->direction;
  params.start_mode = this->start_mode;
  params.stop_mode = this->stop_mode;
  params.buf.block.frag_size = fragment_size;
  params.buf.block.frags_max = fragment_count;
  params.buf.block.frags_min = 1;
  
  memset(&format, 0, sizeof(format));
  format.format =  this->format;
  format.rate = this->rate;
  format.voices = this->voices;
  format.interleave = this->interleave;
  memcpy(&params.format, &format, sizeof(format));
  
  snd_pcm_playback_flush(this->front_handle);
  
  if((err = snd_pcm_channel_params(this->front_handle, &params)) < 0) {
    perr("snd_pcm_channel_params() failed: %s\n", snd_strerror(err));
    return;
  }
  if((err = snd_pcm_playback_prepare(this->front_handle)) < 0) {
    perr("snd_pcm_channel_prepare() failed: %s\n", snd_strerror(err));
    return;
  }

  memset(&setup, 0, sizeof(setup));
  setup.mode = this->mode;
  setup.channel = this->direction;
  if((err = snd_pcm_channel_setup(this->front_handle, &setup)) < 0) {
    perr("snd_pcm_channel_setup() failed: %s\n", snd_strerror(err));
    return;
  }      
  
  this->frag_size = fragment_size;
  this->frag_count = fragment_count;
  
  /* this->pcm_len = fragment_size * 
    (snd_pcm_format_width(this->format) / 8) * 
    this->voices;

    perr("PCM len = %d\n", this->pcm_len);
   if(this->zero_space)
    free(this->zero_space);
  
   this->zero_space = (int16_t *) malloc(this->frag_size);
    memset(this->zero_space, 
  	 (int16_t) snd_pcm_format_silence(this->format), 
  	 this->frag_size);
  */
}

/*
 * open the audio device for writing to
 */
static int ao_alsa_open(ao_driver_t *this_gen,uint32_t bits, uint32_t rate, int ao_mode) {

  int                       channels;
  int                       subdevice = 0;
  int                       direction = SND_PCM_OPEN_PLAYBACK;  
  snd_pcm_format_t          pcm_format;
  snd_pcm_channel_setup_t   pcm_chan_setup;
  snd_pcm_channel_params_t  pcm_chan_params;
  snd_pcm_channel_info_t    pcm_chan_info;
  int                       err;
  int                       mode;
  alsa_driver_t            *this = (alsa_driver_t *) this_gen;

  switch (ao_mode) {

  case AO_CAP_MODE_STEREO:
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
    channels = 2;
    break;

  case AO_CAP_MODE_MONO:
    channels = 1;
    break;

  default:
    return 0;
    break;
  }

#ifdef LOG_DEBUG  
  xprintf (VERBOSE|AUDIO, "bits = %d, rate = %d, channels = %d\n", 
	   bits, rate, channels);
#endif
 
  if(!rate)
    return 0;
 
  if(this->front_handle != NULL) {

    if(rate == this->input_sample_rate)
      return this->output_sample_rate;
    
    snd_pcm_close(this->front_handle);
  }

  this->input_sample_rate      = rate;
  this->bytes_in_buffer        = 0;
  this->direction              = SND_PCM_CHANNEL_PLAYBACK;

  if ((ao_mode == AO_CAP_MODE_A52) || (mode == AO_CAP_MODE_AC5)) {
    this->pcm_default_device = 2;
    mode = SND_PCM_MODE_BLOCK;
  }
  else {
    mode = SND_PCM_MODE_BLOCK;
  }

  this->mode = mode;
  
  if((err = snd_pcm_open_subdevice(&this->front_handle, 
				   this->pcm_default_card, 
				   this->pcm_default_device, 
				   subdevice, direction
				   /*				   | SND_PCM_OPEN_NONBLOCK)) < 0) { */
				   )) < 0) {
    perr("snd_pcm_open_subdevice() failed: %s\n", snd_strerror(err));
    return 0;
  }

  memset(&pcm_chan_info, 0, sizeof(snd_pcm_channel_info_t));
  if((err = snd_pcm_channel_info(this->front_handle, 
				 &pcm_chan_info)) < 0) {
    perr("snd_pcm_channel_info() failed: %s\n", snd_strerror(err));
    return 0;
  }	
  
  memset(&pcm_chan_params, 0, sizeof(snd_pcm_channel_params_t));
  memset(&pcm_format, 0, sizeof(snd_pcm_format_t));
  /* set sample size */
  switch(bits) {
  case 8:
    pcm_format.format = SND_PCM_SFMT_S8;
    break;
    
  case 16:
    pcm_format.format = SND_PCM_SFMT_S16;
    break;
    
  case 24:
    pcm_format.format = SND_PCM_SFMT_S24;
    break;

  case 32:
    pcm_format.format = SND_PCM_SFMT_S32;
    break;

  default:
    perr("sample format %d unsupported\n", bits);
    break;
  }
  this->format = pcm_format.format;

#ifdef LOG_DEBUG
  xprintf (VERBOSE|AUDIO, "format name = '%s'\n", 
       snd_pcm_get_format_name(pcm_format.format));
#endif
  
  pcm_format.voices = this->voices = channels;
  pcm_format.rate = this->rate = rate;
  pcm_format.interleave = this->interleave = 1;

  this->num_channels    = channels;
  this->bytes_per_frame = (bits*this->num_channels)/8;

#ifdef LOG_DEBUG
  xprintf (VERBOSE|AUDIO, "audio channels = %d ao_mode = %d\n", 
	   this->num_channels,ao_mode);
#endif
  
  if(rate > pcm_chan_info.max_rate)
    this->output_sample_rate = pcm_chan_info.max_rate;
  else
    this->output_sample_rate = this->input_sample_rate;


  memcpy(&pcm_chan_params.format, &pcm_format, sizeof(snd_pcm_format_t));

  pcm_chan_params.mode           = mode;
  pcm_chan_params.channel        = this->direction;

  pcm_chan_params.start_mode     = SND_PCM_START_FULL;
  /*
    pcm_chan_params.start_mode    = SND_PCM_START_DATA;
    pcm_chan_params.stop_mode      = SND_PCM_STOP_STOP;
  */
  pcm_chan_params.stop_mode      = SND_PCM_STOP_ROLLOVER;
  
  this->start_mode = pcm_chan_params.start_mode;
  this->stop_mode = pcm_chan_params.stop_mode;
  this->ao_mode = ao_mode;

  if ((ao_mode == AO_CAP_MODE_A52) || (mode == AO_CAP_MODE_AC5)) {
        pcm_chan_params.digital.dig_valid      = 1;
        pcm_chan_params.digital.dig_status[0]  = SND_PCM_DIG0_NONAUDIO;
        pcm_chan_params.digital.dig_status[0] |= SND_PCM_DIG0_PROFESSIONAL;
        pcm_chan_params.digital.dig_status[0] |= SND_PCM_DIG0_PRO_FS_48000;
        pcm_chan_params.digital.dig_status[3]  = SND_PCM_DIG3_CON_FS_48000;
  }

  snd_pcm_playback_flush(this->front_handle);
  if((err = snd_pcm_channel_params(this->front_handle, 
				   &pcm_chan_params)) < 0) {
    perr("snd_pcm_channel_params() failed: %s\n", snd_strerror(err));
    return 0;
  } 
  if((err = snd_pcm_playback_prepare(this->front_handle)) < 0) {
    perr("snd_pcm_channel_prepare() failed: %s\n", snd_strerror(err));
    return 0;
  }

  pcm_chan_setup.mode    = mode;
  pcm_chan_setup.channel = this->direction;

  if((err = snd_pcm_channel_setup(this->front_handle, 
				  &pcm_chan_setup)) < 0) {
    perr("snd_pcm_channel_setup() failed: %s\n", snd_strerror(err));
    return 0;
  }

  printf ("actual rate: %d\n", pcm_chan_setup.format.rate);

  alsa_set_frag(this, 1536, 6);
  alsa_set_frag(this,AUDIO_FRAGMENT_SIZE, AUDIO_NUM_FRAGMENTS);

  this->bytes_in_buffer = 0;

  return this->output_sample_rate;
}

static int ao_alsa_num_channels(ao_driver_t *this_gen)  {

  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->num_channels;

}

static int ao_alsa_bytes_per_frame(ao_driver_t *this_gen) {

  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->bytes_per_frame;

}

static int ao_alsa_get_gap_tolerance (ao_driver_t *this_gen) {
  return GAP_TOLERANCE;
}

static int ao_alsa_delay(ao_driver_t *this_gen) {

  alsa_driver_t            *this = (alsa_driver_t *) this_gen;
  int                       bytes_left;
  snd_pcm_channel_status_t  pcm_stat;
  int                       err;
  
     
  memset(&pcm_stat, 0, sizeof(snd_pcm_channel_status_t));
  pcm_stat.channel = SND_PCM_CHANNEL_PLAYBACK;
  if((err = snd_pcm_channel_status(this->front_handle, 
                                     &pcm_stat)) < 0) {
    /* Hide error report */
    perr("snd_pcm_channel_status() failed: %s\n", snd_strerror(err));
    return 0;
  }

  /* calc delay */

  bytes_left = this->bytes_in_buffer - pcm_stat.scount;

  if (bytes_left<=0) { /* buffer ran dry */
    bytes_left = 0;
    this->bytes_in_buffer = pcm_stat.scount;
  }

  return bytes_left / this->bytes_per_frame;
}


static int ao_alsa_write (ao_driver_t *this_gen,
			  int16_t* frame_buffer, uint32_t num_frames) {


  alsa_driver_t *this = (alsa_driver_t *) this_gen;

  this->bytes_in_buffer += num_frames * this->bytes_per_frame;

  snd_pcm_write(this->front_handle, frame_buffer,
		num_frames * this->bytes_per_frame);
  return 1;
}

static void ao_alsa_close(ao_driver_t *this_gen) {
  int err;
  alsa_driver_t *this = (alsa_driver_t *) this_gen;

  if(this->front_handle) {
    if((err = snd_pcm_playback_flush(this->front_handle)) < 0) {
      perr("snd_pcm_channel_flush() failed: %s\n", snd_strerror(err));
    }
    
    if((err = snd_pcm_close(this->front_handle)) < 0) {
      perr("snd_pcm_close() failed: %s\n", snd_strerror(err));
    }
    
    this->front_handle = NULL;
  }
}

static int ao_alsa_get_property (ao_driver_t *this_gen, int property) {
  /* alsa_driver_t *this = (alsa_driver_t *) this_gen; */

  /* FIXME: implement some properties 
  switch(property) {
  case AO_PROP_MIXER_VOL:
    break;
  case AO_PROP_PCM_VOL:
    break;
  case AO_PROP_MUTE_VOL:
    break;
  }
  */
  return 0;
}

static int ao_alsa_set_property (ao_driver_t *this_gen, int property, int value) {
  /* alsa_driver_t *this = (alsa_driver_t *) this_gen; */

  /* FIXME: Implement property support.
  switch(property) {
  case AO_PROP_MIXER_VOL:
    break;
  case AO_PROP_PCM_VOL:
    break;
  case AO_PROP_MUTE_VOL:
    break;
  }
  */

  return ~value;
}

static uint32_t ao_alsa_get_capabilities (ao_driver_t *this_gen) {
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  return this->capabilities;
}


static void ao_alsa_exit(ao_driver_t *this_gen) {
  /* alsa_driver_t *this = (alsa_driver_t *) this_gen; */
}


static ao_info_t ao_info_alsa = {
  AO_ALSA_IFACE_VERSION,
  "alsa05",
  "xine audio output plugin using alsa-compliant audio devices/drivers",
  10
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_alsa;
}

static void sighandler(int signum) {
}

ao_driver_t *init_audio_out_plugin(config_values_t *config) {
  alsa_driver_t           *this;
  int                      best_rate;
  int                      devnum;
  int                      err;
  int                      direction = SND_PCM_OPEN_PLAYBACK;
  int                      snd_default_card;
  int                      snd_default_mixer_card;
  int                      snd_default_mixer_device;
  snd_pcm_info_t           pcm_info;
  snd_pcm_channel_info_t   pcm_chan_info;
  struct sigaction         action;

  this = (alsa_driver_t *) malloc (sizeof (alsa_driver_t));

  /* Check if, at least, one card is installed */
  if((devnum = snd_cards()) == 0) {
    return NULL;
  }
  else {
    snd_default_card = snd_defaults_card();
    if((err = snd_card_load(snd_default_card)) < 0) {
      perr("snd_card_load() failed: %s\n", snd_strerror(err));
    }
#ifdef LOG_DEBUG
    xprintf (VERBOSE|AUDIO, "%d card(s) installed. Default = %d\n",
	     devnum, snd_default_card);
#endif
    
    if((snd_default_mixer_card = snd_defaults_mixer_card()) < 0) {
      perr("snd_defaults_mixer_card() failed: %s\n", 
	   snd_strerror(snd_default_mixer_card));
    }
#ifdef LOG_DEBUG
    xprintf (VERBOSE|AUDIO, "default mixer card = %d\n", 
	     snd_default_mixer_card);
#endif

    if((snd_default_mixer_device = snd_defaults_mixer_device()) < 0) {
      perr("snd_defaults_mixer_device() failed: %s\n", 
	   snd_strerror(snd_default_mixer_device));
    }
#ifdef LOG_DEBUG
    xprintf (VERBOSE|AUDIO, "default mixer device = %d\n", 
	     snd_default_mixer_device);
#endif
  }
  
#ifdef LOG_DEBUG
  xprintf (VERBOSE|AUDIO, "Opening audio device...");
#endif

  if((this->pcm_default_card = snd_defaults_pcm_card()) < 0) {
    perr("There is no default pcm card.\n");
    exit(1);
  }
#ifdef LOG_DEBUG
  xprintf (VERBOSE|AUDIO, "snd_defaults_pcm_card() return %d\n", 
	   this->pcm_default_card);
#endif

  if((this->pcm_default_device = snd_defaults_pcm_device()) < 0) {
    perr("There is no default pcm device.\n");
    exit(1);
  }
#ifdef LOG_DEBUG
  xprintf (VERBOSE|AUDIO, "snd_defaults_pcm_device() return %d\n", 
	   this->pcm_default_device);
#endif

  this->capabilities = AO_CAP_MODE_STEREO;
  if (config->register_bool (config,
                             "audio.a52_pass_through",
                             0,
                             "used to inform xine about what the sound card can do",
                             NULL,
                             NULL,
                             NULL) ) {
    this->capabilities |= AO_CAP_MODE_A52;
    this->capabilities |= AO_CAP_MODE_AC5;
  }

  this->ao_driver.get_capabilities    = ao_alsa_get_capabilities;
  this->ao_driver.get_property        = ao_alsa_get_property;
  this->ao_driver.set_property        = ao_alsa_set_property;
  this->ao_driver.open                = ao_alsa_open;
  this->ao_driver.num_channels        = ao_alsa_num_channels;
  this->ao_driver.bytes_per_frame     = ao_alsa_bytes_per_frame;
  this->ao_driver.delay               = ao_alsa_delay;
  this->ao_driver.write		      = ao_alsa_write;
  this->ao_driver.close               = ao_alsa_close;
  this->ao_driver.exit                = ao_alsa_exit;
  this->ao_driver.get_gap_tolerance   = ao_alsa_get_gap_tolerance;


  action.sa_handler = sighandler;
  sigemptyset(&(action.sa_mask));
  action.sa_flags = 0;
  if(sigaction(SIGALRM, &action, NULL) != 0) {
    perr("sigaction(SIGALRM) failed: %s\n", strerror(errno));
  }
  alarm(2);

  if((err = snd_pcm_open(&this->front_handle, this->pcm_default_card,
			 this->pcm_default_device, direction)) < 0) {
    perr("snd_pcm_open() failed: %s\n", snd_strerror(err));
    perr(">>> Check if another program don't already use PCM <<<\n");
    return NULL;
  }
  
  memset(&pcm_info, 0, sizeof(snd_pcm_info_t));
  if((err = snd_pcm_info(this->front_handle, &pcm_info)) < 0) {
    perr("snd_pcm_info() failed: %s\n", snd_strerror(err));
    exit(1);
  }
  
#ifdef LOG_DEBUG
  xprintf (VERBOSE|AUDIO, "snd_pcm_info():\n");
  xprintf (VERBOSE|AUDIO, "---------------\n");
  xprintf (VERBOSE|AUDIO, "type      = 0x%x\n", pcm_info.type); 
  xprintf (VERBOSE|AUDIO, "flags     = 0x%x\n", pcm_info.flags);
  xprintf (VERBOSE|AUDIO, "id        = '%s'\n", pcm_info.id);  
  xprintf (VERBOSE|AUDIO, "name      = '%s'\n", pcm_info.name); 
  xprintf (VERBOSE|AUDIO, "playback  = %d\n", pcm_info.playback);
  xprintf (VERBOSE|AUDIO, "capture   = %d\n", pcm_info.capture); 
#endif
  
  memset(&pcm_chan_info, 0, sizeof(snd_pcm_channel_info_t));
  pcm_chan_info.channel = SND_PCM_CHANNEL_PLAYBACK;
  if((err = snd_pcm_channel_info(this->front_handle, 
				 &pcm_chan_info)) < 0) {
    perr("snd_pcm_channel_info() failed: %s\n", snd_strerror(err));
    exit(1);
  }						       
  
  best_rate = pcm_chan_info.rates;

#ifdef LOG_DEBUG
  xprintf (VERBOSE|AUDIO, "best_rate = %d\n", best_rate);
  xprintf (VERBOSE|AUDIO, "snd_pcm_channel_info(PLAYBACK):\n");
  xprintf (VERBOSE|AUDIO, "-------------------------------\n");
  xprintf (VERBOSE|AUDIO, "subdevice           = %d\n", 
	   pcm_chan_info.subdevice);
  xprintf (VERBOSE|AUDIO, "subname             = %s\n", 
	   pcm_chan_info.subname);
  xprintf (VERBOSE|AUDIO, "channel             = %d\n", 
	   pcm_chan_info.channel);
  xprintf (VERBOSE|AUDIO, "mode                = %d\n", 
	   pcm_chan_info.mode);
  xprintf (VERBOSE|AUDIO, "flags               = 0x%x\n", 
	   pcm_chan_info.flags);
  xprintf (VERBOSE|AUDIO, "formats             = %d\n", 
	   pcm_chan_info.formats);
  xprintf (VERBOSE|AUDIO, "rates               = %d\n", 
	   pcm_chan_info.rates);  
  xprintf (VERBOSE|AUDIO, "min_rate            = %d\n", 
	   pcm_chan_info.min_rate);
  xprintf (VERBOSE|AUDIO, "max_rate            = %d\n", 
	   pcm_chan_info.max_rate);
  xprintf (VERBOSE|AUDIO, "min_voices          = %d\n", 
	   pcm_chan_info.min_voices);
  xprintf (VERBOSE|AUDIO, "max_voices          = %d\n",
	   pcm_chan_info.max_voices);
  xprintf (VERBOSE|AUDIO, "buffer_size         = %d\n",
	   pcm_chan_info.buffer_size);
  xprintf (VERBOSE|AUDIO, "min_fragment_size   = %d\n", 
	   pcm_chan_info.min_fragment_size);
  xprintf (VERBOSE|AUDIO, "max_fragment_size   = %d\n", 
	   pcm_chan_info.max_fragment_size);
  xprintf (VERBOSE|AUDIO, "fragment_align      = %d\n", 
	   pcm_chan_info.fragment_align);
  xprintf (VERBOSE|AUDIO, "fifo_size           = %d\n", 
	   pcm_chan_info.fifo_size);
  xprintf (VERBOSE|AUDIO, "transfer_block_size = %d\n", 
	   pcm_chan_info.transfer_block_size);
  xprintf (VERBOSE|AUDIO, "mmap_size           = %ld\n", 
	   pcm_chan_info.mmap_size); 
  xprintf (VERBOSE|AUDIO, "mixer_device        = %d\n",
	   pcm_chan_info.mixer_device);
#endif  
  snd_pcm_close (this->front_handle);
  this->front_handle = NULL;

  return &this->ao_driver; 
}
