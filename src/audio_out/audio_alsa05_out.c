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
 * for the SPDIF AC3 sync part
 * (c) 2000 Andy Lo A Foe <andy@alsaplayer.org>
 *
 * $Id: audio_alsa05_out.c,v 1.7 2001/08/25 04:33:33 guenter Exp $
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
#include "monitor.h"
#include "audio_out.h"
#include "metronom.h"
#include "resample.h"
#include "utils.h"

#define AUDIO_NUM_FRAGMENTS     6
#define AUDIO_FRAGMENT_SIZE  1536

#define ZERO_BUF_SIZE        15360 /* has to be a multiplier of 3 and 5 (??)*/

#define GAP_TOLERANCE        5000

#define MAX_GAP              90000

extern uint32_t xine_debug;


typedef struct _audio_alsa_globals {

  ao_functions_t ao_functions;

  snd_pcm_t *front_handle;

  int32_t    output_sample_rate, input_sample_rate;
  uint32_t   num_channels;

  uint32_t   bytes_in_buffer;  /* number of bytes written to audio hardware  */
  uint32_t   last_vpts;        /* vpts at which last written package ends    */

  uint32_t   sync_vpts;        /* this syncpoint is used as a starting point */
  uint32_t   sync_bytes_in_buffer; /* for vpts <-> samplecount assoc         */

  int        audio_step;       /* pts per 32 768 samples (sample = #bytes/2) */
  int32_t    bytes_per_kpts;   /* bytes per 1024/90000 sec                   */

  uint32_t       last_audio_vpts;

  int16_t   *zero_space;

  int        do_resample;      /* resampling if output and input sample rate are different */
  int        audio_started;
  int        pcm_default_card;
  int        pcm_default_device;

  int        direction;
  int        mode;
  int        start_mode;
  int        stop_mode;
  int        format;
  int        rate;
  int        voices;
  int        interleave;
  int        frag_size;
  int        frag_count;
  int        pcm_len;
  int        ao_mode;
  metronom_t *metronom;
  int        capabilities;
  uint16_t   *sample_buffer;     


} audio_alsa_globals_t;


/* ------------------------------------------------------------------------- */
/*
 *
 */
static void alsa_set_frag(audio_alsa_globals_t *this, int fragment_size, int fragment_count) {
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

/* ------------------------------------------------------------------------- */
/*
 * open the audio device for writing to
 */
static int ao_open(ao_functions_t *this_gen,uint32_t bits, uint32_t rate, int ao_mode) {
  int                       channels;
  int                       subdevice = 0;
  int                       direction = SND_PCM_OPEN_PLAYBACK;  
  snd_pcm_format_t          pcm_format;
  snd_pcm_channel_setup_t   pcm_chan_setup;
  snd_pcm_channel_params_t  pcm_chan_params;
  snd_pcm_channel_info_t    pcm_chan_info;
  int                       err;
  int                       mode;
  audio_alsa_globals_t *this = (audio_alsa_globals_t *) this_gen;

  switch (ao_mode) {

  case AO_CAP_MODE_STEREO:
  case AO_CAP_MODE_AC3:
    channels = 2;
    break;

  case AO_CAP_MODE_MONO:
    channels = 1;
    break;

  default:
    return 0;
    break;
  }
  
  xprintf (VERBOSE|AUDIO, "bits = %d, rate = %d, channels = %d\n", 
	   bits, rate, channels);

 
  if(!rate)
    return 0;
 
  if(this->front_handle != NULL) {

    if(rate == this->input_sample_rate)
      return 1;
    
    snd_pcm_close(this->front_handle);
  }

  this->input_sample_rate      = rate;
  this->bytes_in_buffer        = 0;
  this->last_vpts              = 0;
  this->sync_vpts              = 0;
  this->sync_bytes_in_buffer   = 0;
  this->audio_started          = 0;
  this->direction              = SND_PCM_CHANNEL_PLAYBACK;
  this->last_audio_vpts        = 0;

  if (ao_mode == AO_CAP_MODE_AC3) {
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

  xprintf (VERBOSE|AUDIO, "format name = '%s'\n", 
       snd_pcm_get_format_name(pcm_format.format));

  
  pcm_format.voices = this->voices = channels;
  pcm_format.rate = this->rate = rate;
  pcm_format.interleave = this->interleave = 1;

  this->num_channels = channels;

  xprintf (VERBOSE|AUDIO, "audio channels = %d ao_mode = %d\n", 
	   this->num_channels,ao_mode);
  
  if(rate > pcm_chan_info.max_rate)
    this->output_sample_rate = pcm_chan_info.max_rate;
  else
    this->output_sample_rate = this->input_sample_rate;

  // is there a need to resample?
  if ((this->input_sample_rate) != (this->output_sample_rate)) {
    this->do_resample=1;
    printf("audio_alsa05_out: resampling from %d to %d.\n",
	   this->input_sample_rate,this->output_sample_rate);
  }
  else 
    this->do_resample=0;
  
  this->audio_step           = (uint32_t) 90000 
    * (uint32_t) 32768 / this->input_sample_rate;

  this->bytes_per_kpts       = this->output_sample_rate 
    * this->num_channels * 2 * 1024 / 90000;


  xprintf (VERBOSE|AUDIO, "%d input samples/sec %d output samples/sec\n", 
	   rate, this->output_sample_rate);
  xprintf (VERBOSE|AUDIO, "audio_out : audio_step %d pts per 32768 samples\n",
	   this->audio_step);

  this->metronom->set_audio_rate (this->metronom,this->audio_step);

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

  if (ao_mode == AO_CAP_MODE_AC3) {
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

  return 1;
}

/* ------------------------------------------------------------------------- */
/*
 *
 */
static void ao_fill_gap (audio_alsa_globals_t *this, uint32_t pts_len) {
  int num_bytes;

  if (pts_len > MAX_GAP)
    pts_len = MAX_GAP;

  num_bytes = pts_len * this->bytes_per_kpts / 1024;
  num_bytes = (num_bytes / (2*this->num_channels)) * (2*this->num_channels);
  
  this->bytes_in_buffer += num_bytes;
  
  printf ("audio_alsa05_out: inserting %d 0-bytes to fill a gap of %d pts\n",
	  num_bytes, pts_len);

  while (num_bytes>0) {
    if (num_bytes> ZERO_BUF_SIZE) {
      snd_pcm_write(this->front_handle, this->zero_space, 
     		    ZERO_BUF_SIZE);
      num_bytes -= ZERO_BUF_SIZE;
    } else {
      snd_pcm_write(this->front_handle, this->zero_space, num_bytes);
      num_bytes = 0;
    }
  } 
}

/* ------------------------------------------------------------------------- */
/*
 *
 */
static uint32_t ao_get_current_pos (audio_alsa_globals_t *this) {
  uint32_t                  pos;
  snd_pcm_channel_status_t  pcm_stat;
  int                       err;
  
     
  if (this->audio_started) { 
    memset(&pcm_stat, 0, sizeof(snd_pcm_channel_status_t));
    pcm_stat.channel = SND_PCM_CHANNEL_PLAYBACK;
    if((err = snd_pcm_channel_status(this->front_handle, 
				     &pcm_stat)) < 0) {
      //Hide error report
      perr("snd_pcm_channel_status() failed: %s\n", snd_strerror(err));
      return 0;
    }
    pos = pcm_stat.scount; 
  }
  else {
    pos = this->bytes_in_buffer;
  }

  return pos;
}



/* ------------------------------------------------------------------------- */
/*
 *
 */
static int ao_write_audio_data(ao_functions_t *this_gen,
			       int16_t* output_samples, 
			       uint32_t num_samples, uint32_t pts_) {
  uint32_t                  vpts, buffer_vpts;
  int32_t                   gap;
  int                       bDropPackage;
  uint32_t                    pos;
  audio_alsa_globals_t *this = (audio_alsa_globals_t *) this_gen;

  
  xprintf(VERBOSE|AUDIO, "Audio : play %d samples at pts %d pos %d \n",
	  num_samples, pts_, this->bytes_in_buffer);   

  if (this->front_handle == NULL) // no output device
    return 1;
   
  // what's the last delivered sample?
  vpts = this->metronom->got_audio_samples (this->metronom,pts_, num_samples);

  if (vpts<this->last_audio_vpts) {
    /* reject this */

    return 1;
  }

  this->last_audio_vpts = vpts;
  bDropPackage = 0;

  // get the sample that should be played NOW
  buffer_vpts = this->metronom->get_current_time (this->metronom);
    
  if (this->audio_started) {
    pos  = ao_get_current_pos (this); 
  }
  else pos=0;
  

  if (pos>this->bytes_in_buffer) { /* buffer ran dry */ 
    printf("Buffer ran dry.\n");
    this->bytes_in_buffer = pos;
  }

  buffer_vpts += (this->bytes_in_buffer - pos) * 1024 / this->bytes_per_kpts;
  // alternative to this command:
  // vpts = vpts - (this->bytes_in_buffer - pos) * 1024 / this->bytes_per_kpts;
  
  gap = vpts - buffer_vpts;

  // activate this for debugging output
  /* 
     printf("pos: %d -- buffer_vpts: %d -- vpts: %d -- gap: %d -- bib: %d\n"
            ,pos,buffer_vpts, vpts, gap,this->bytes_in_buffer);
  */

  xprintf (VERBOSE|AUDIO, "audio_alsa05_out: got %d samples, vpts=%d, "
	   "last_vpts=%d\n", num_samples, vpts, this->last_vpts);

  if (gap > GAP_TOLERANCE) {

    ao_fill_gap (this, gap);
       
    /* keep xine responsive */
    
    if (gap>MAX_GAP)
      return 0;
  } 
  else if (gap < -GAP_TOLERANCE) {
    bDropPackage = 1;
  }
  
  if (!bDropPackage) {
    int num_output_samples = num_samples * (this->output_sample_rate) / this->input_sample_rate;

    if (!this->do_resample) {
      snd_pcm_write(this->front_handle, output_samples,
            num_output_samples * this->num_channels * 2);
    } else 
      switch (this->ao_mode) {
      case AO_CAP_MODE_MONO:
	audio_out_resample_mono (output_samples, num_samples,
	                        this->sample_buffer, num_output_samples);
	snd_pcm_write(this->front_handle, output_samples, num_output_samples * 2);
	break;
      case AO_CAP_MODE_STEREO:
	audio_out_resample_stereo (output_samples, num_samples,
				   this->sample_buffer, num_output_samples);
	snd_pcm_write(this->front_handle, this->sample_buffer, num_output_samples * 4);
	break;
      case AO_CAP_MODE_AC3:
	// I hope this works. I cannot test because I don't have a SPDIF out
	num_output_samples = num_samples+8;
	this->sample_buffer[0] = 0xf872;  //spdif syncword
	this->sample_buffer[1] = 0x4e1f;  // .............
	this->sample_buffer[2] = 0x0001;  // AC3 data
	this->sample_buffer[3] = num_samples * 8;
	//      this->sample_buffer[4] = 0x0b77;  // AC3 syncwork already in output_samples
	
	// ac3 seems to be swabbed data
	swab(output_samples,this->sample_buffer+4,  num_samples  );
	snd_pcm_write(this->front_handle, output_samples, num_output_samples);
	snd_pcm_write(this->front_handle, output_samples, 6144-num_output_samples);
	num_output_samples=num_output_samples/4;
	break;
      }
    
    // step values
    this->bytes_in_buffer += num_output_samples * this->num_channels * 2;
    this->audio_started    = 1;
  }
  return 1;
}





/* ------------------------------------------------------------------------- */
/*
 *
 */
static void ao_close(ao_functions_t *this_gen) {
  int err;
  audio_alsa_globals_t *this = (audio_alsa_globals_t *) this_gen;

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

static int ao_get_property (ao_functions_t *this_gen, int property) {
  // audio_alsa_globals_t *this = (audio_alsa_globals_t *) this_gen;

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

/*
 *
 */
static int ao_set_property (ao_functions_t *this_gen, int property, int value) {
  // audio_alsa_globals_t *this = (audio_alsa_globals_t *) this_gen;

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

static void ao_connect (ao_functions_t *this_gen, metronom_t *metronom) {
  audio_alsa_globals_t *this = (audio_alsa_globals_t *) this_gen;
  this->metronom = metronom;
}

static uint32_t ao_get_capabilities (ao_functions_t *this_gen) {
  audio_alsa_globals_t *this = (audio_alsa_globals_t *) this_gen;
  return this->capabilities;
}


/* ------------------------------------------------------------------------- */
/*
 *
 */
static int ao_is_mode_supported (int mode) {

  switch (mode) {

  case AO_CAP_MODE_STEREO:
  case AO_CAP_MODE_AC3:
    /*case AO_MODE_MONO: FIXME */
    return 1;

  }

  return 0;
}

static void ao_exit(ao_functions_t *this_gen)
{
  audio_alsa_globals_t *this = (audio_alsa_globals_t *) this_gen;
  free(this->sample_buffer);
  free(this->zero_space);
}




/* ------------------------------------------------------------------------- */
/*
 *
 */

static ao_info_t ao_info_alsa = {
  AUDIO_OUT_IFACE_VERSION,
  "alsa05",
  "xine audio output plugin using alsa-compliant audio devices/drivers",
  10
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_alsa;
}



/* ------------------------------------------------------------------------- */
/*
 *
 */
static void sighandler(int signum) {
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
ao_functions_t *init_audio_out_plugin(config_values_t *config) {
  audio_alsa_globals_t     *this;
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

  this = (audio_alsa_globals_t *) malloc (sizeof (audio_alsa_globals_t));

  /* Check if, at least, one card is installed */
  if((devnum = snd_cards()) == 0) {
    return NULL;
  }
  else {
    snd_default_card = snd_defaults_card();
    if((err = snd_card_load(snd_default_card)) < 0) {
      perr("snd_card_load() failed: %s\n", snd_strerror(err));
    }
    xprintf (VERBOSE|AUDIO, "%d card(s) installed. Default = %d\n",
	     devnum, snd_default_card);
    
    if((snd_default_mixer_card = snd_defaults_mixer_card()) < 0) {
      perr("snd_defaults_mixer_card() failed: %s\n", 
	   snd_strerror(snd_default_mixer_card));
    }
    xprintf (VERBOSE|AUDIO, "default mixer card = %d\n", 
	     snd_default_mixer_card);

    if((snd_default_mixer_device = snd_defaults_mixer_device()) < 0) {
      perr("snd_defaults_mixer_device() failed: %s\n", 
	   snd_strerror(snd_default_mixer_device));
    }
    xprintf (VERBOSE|AUDIO, "default mixer device = %d\n", 
	     snd_default_mixer_device);
  }
  
  xprintf (VERBOSE|AUDIO, "Opening audio device...");

  if((this->pcm_default_card = snd_defaults_pcm_card()) < 0) {
    perr("There is no default pcm card.\n");
    exit(1);
  }
  xprintf (VERBOSE|AUDIO, "snd_defaults_pcm_card() return %d\n", 
	   this->pcm_default_card);

  if((this->pcm_default_device = snd_defaults_pcm_device()) < 0) {
    perr("There is no default pcm device.\n");
    exit(1);
  }
  xprintf (VERBOSE|AUDIO, "snd_defaults_pcm_device() return %d\n", 
	   this->pcm_default_device);

  this->capabilities = AO_CAP_MODE_STEREO;
  if (config->lookup_int (config, "ac3_pass_through", 0)) 
    this->capabilities |= AO_CAP_MODE_AC3;



  this->ao_functions.get_capabilities = ao_get_capabilities;
  this->ao_functions.get_property     = ao_get_property;
  this->ao_functions.set_property     = ao_set_property;
  this->ao_functions.connect          = ao_connect;
  this->ao_functions.open             = ao_open;
  this->ao_functions.write_audio_data = ao_write_audio_data;
  this->ao_functions.close            = ao_close;
  this->ao_functions.exit             = ao_exit;



  this->sample_buffer = malloc (40000);
  memset (this->sample_buffer, 0, 40000);
  this->zero_space = malloc (ZERO_BUF_SIZE);
  memset (this->zero_space, 0, ZERO_BUF_SIZE);

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
  
  xprintf (VERBOSE|AUDIO, "snd_pcm_info():\n");
  xprintf (VERBOSE|AUDIO, "---------------\n");
  xprintf (VERBOSE|AUDIO, "type      = 0x%x\n", pcm_info.type); 
  xprintf (VERBOSE|AUDIO, "flags     = 0x%x\n", pcm_info.flags);
  xprintf (VERBOSE|AUDIO, "id        = '%s'\n", pcm_info.id);  
  xprintf (VERBOSE|AUDIO, "name      = '%s'\n", pcm_info.name); 
  xprintf (VERBOSE|AUDIO, "playback  = %d\n", pcm_info.playback);
  xprintf (VERBOSE|AUDIO, "capture   = %d\n", pcm_info.capture); 
  
  memset(&pcm_chan_info, 0, sizeof(snd_pcm_channel_info_t));
  pcm_chan_info.channel = SND_PCM_CHANNEL_PLAYBACK;
  if((err = snd_pcm_channel_info(this->front_handle, 
				 &pcm_chan_info)) < 0) {
    perr("snd_pcm_channel_info() failed: %s\n", snd_strerror(err));
    exit(1);
  }						       
  
  best_rate = pcm_chan_info.rates;

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
  
  snd_pcm_close (this->front_handle);
  this->front_handle = NULL;

  return &this->ao_functions; 
}
