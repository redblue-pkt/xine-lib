/* 
 * Copyright (C) 2000 the xine project
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
 *
 * Plugin for ALSA Version 0.5.x
 *
 * Credits go
 * for the SPDIF AC3 sync part
 * (c) 2000 Andy Lo A Foe <andy@alsaplayer.org>
 *
 * $Id: audio_alsa05_out.c,v 1.3 2001/06/23 19:45:47 guenter Exp $
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

#if (SND_LIB_MAJOR == 0) && (SND_LIB_MINOR <= 5)

#include "xine_internal.h"
#include "monitor.h"
#include "audio_out.h"
#include "metronom.h"
#include "resample.h"
#include "utils.h"

#define AUDIO_NUM_FRAGMENTS    15
#define AUDIO_FRAGMENT_SIZE  8192

#define GAP_TOLERANCE        15000
#define MAX_MASTER_CLOCK_DIV  5000
#define MAX_GAP              90000

extern uint32_t xine_debug;


typedef struct _audio_alsa_globals {

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

} audio_alsa_globals_t;

/* FIXME : global variables are not allowed in plugins */

static audio_alsa_globals_t gAudioALSA;


/* ------------------------------------------------------------------------- */
/*
 *
 */
static void alsa_set_frag(int fragment_size, int fragment_count) {
  snd_pcm_channel_params_t  params;
  snd_pcm_channel_setup_t   setup;
  snd_pcm_format_t          format;
  int err;

  memset(&params, 0, sizeof(params));
  
  params.mode = gAudioALSA.mode;
  params.channel = gAudioALSA.direction;
  params.start_mode = gAudioALSA.start_mode;
  params.stop_mode = gAudioALSA.stop_mode;
  params.buf.block.frag_size = fragment_size;
  params.buf.block.frags_max = fragment_count;
  params.buf.block.frags_min = 1;
  
  memset(&format, 0, sizeof(format));
  format.format =  gAudioALSA.format;
  format.rate = gAudioALSA.rate;
  format.voices = gAudioALSA.voices;
  format.interleave = gAudioALSA.interleave;
  memcpy(&params.format, &format, sizeof(format));
  
  snd_pcm_playback_flush(gAudioALSA.front_handle);
  
  if((err = snd_pcm_channel_params(gAudioALSA.front_handle, &params)) < 0) {
    perr("snd_pcm_channel_params() failed: %s\n", snd_strerror(err));
    return;
  }
  if((err = snd_pcm_playback_prepare(gAudioALSA.front_handle)) < 0) {
    perr("snd_pcm_channel_prepare() failed: %s\n", snd_strerror(err));
    return;
  }

  memset(&setup, 0, sizeof(setup));
  setup.mode = gAudioALSA.mode;
  setup.channel = gAudioALSA.direction;
  if((err = snd_pcm_channel_setup(gAudioALSA.front_handle, &setup)) < 0) {
    perr("snd_pcm_channel_setup() failed: %s\n", snd_strerror(err));
    return;
  }      
  
  gAudioALSA.frag_size = fragment_size;
  gAudioALSA.frag_count = fragment_count;
  
  gAudioALSA.pcm_len = fragment_size * 
    (snd_pcm_format_width(gAudioALSA.format) / 8) * 
    gAudioALSA.voices;

  //  perr("PCM len = %d\n", gAudioALSA.pcm_len);
  if(gAudioALSA.zero_space)
    free(gAudioALSA.zero_space);
  
  gAudioALSA.zero_space = (int16_t *) malloc(gAudioALSA.frag_size);
  memset(gAudioALSA.zero_space, 
	 (int16_t) snd_pcm_format_silence(gAudioALSA.format), 
	 gAudioALSA.frag_size);
}
/* ------------------------------------------------------------------------- */
/*
 * open the audio device for writing to
 */
static int ao_open(ao_functions_t *this,uint32_t bits, uint32_t rate, int ao_mode) {
  int                       channels;
  int                       subdevice = 0;
  int                       direction = SND_PCM_OPEN_PLAYBACK;  
  snd_pcm_format_t          pcm_format;
  snd_pcm_channel_setup_t   pcm_chan_setup;
  snd_pcm_channel_params_t  pcm_chan_params;
  snd_pcm_channel_info_t    pcm_chan_info;
  int                       err;
  int                       mode;


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

#warning "FIXME in libAC3"
  if(!rate)
    return 0;
 
  if(gAudioALSA.front_handle != NULL) {

    if(rate == gAudioALSA.input_sample_rate)
      return 1;
    
    snd_pcm_close(gAudioALSA.front_handle);
  }

  gAudioALSA.input_sample_rate      = rate;
  gAudioALSA.bytes_in_buffer        = 0;
  gAudioALSA.last_vpts              = 0;
  gAudioALSA.sync_vpts              = 0;
  gAudioALSA.sync_bytes_in_buffer   = 0;
  gAudioALSA.audio_started          = 0;
  gAudioALSA.direction              = SND_PCM_CHANNEL_PLAYBACK;
  gAudioALSA.last_audio_vpts        = 0;

  if (ao_mode == AO_CAP_MODE_AC3) {
    gAudioALSA.pcm_default_device = 2;
    mode = SND_PCM_MODE_BLOCK;
  }
  else {
    mode = SND_PCM_MODE_BLOCK;
  }

  gAudioALSA.mode = mode;
  
  if((err = snd_pcm_open_subdevice(&gAudioALSA.front_handle, 
				   gAudioALSA.pcm_default_card, 
				   gAudioALSA.pcm_default_device, 
				   subdevice, direction
				   /*				   | SND_PCM_OPEN_NONBLOCK)) < 0) { */
				   )) < 0) {
    perr("snd_pcm_open_subdevice() failed: %s\n", snd_strerror(err));
    return 0;
  }

  memset(&pcm_chan_info, 0, sizeof(snd_pcm_channel_info_t));
  if((err = snd_pcm_channel_info(gAudioALSA.front_handle, 
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
  gAudioALSA.format = pcm_format.format;

  xprintf (VERBOSE|AUDIO, "format name = '%s'\n", 
       snd_pcm_get_format_name(pcm_format.format));

  
  pcm_format.voices = gAudioALSA.voices = channels;
  pcm_format.rate = gAudioALSA.rate = rate;
  pcm_format.interleave = gAudioALSA.interleave = 1;

  gAudioALSA.num_channels = channels;

  xprintf (VERBOSE|AUDIO, "audio channels = %d ao_mode = %d\n", 
	   gAudioALSA.num_channels,ao_mode);
  
  if(rate > pcm_chan_info.max_rate)
    gAudioALSA.output_sample_rate = pcm_chan_info.max_rate;
  else
    gAudioALSA.output_sample_rate = gAudioALSA.input_sample_rate;

  gAudioALSA.audio_step           = (uint32_t) 90000 
    * (uint32_t) 32768 / gAudioALSA.input_sample_rate;

  gAudioALSA.bytes_per_kpts       = gAudioALSA.output_sample_rate 
    * gAudioALSA.num_channels * 2 * 1024 / 90000;


  xprintf (VERBOSE|AUDIO, "%d input samples/sec %d output samples/sec\n", 
	   rate, gAudioALSA.output_sample_rate);
  xprintf (VERBOSE|AUDIO, "audio_out : audio_step %d pts per 32768 samples\n",
	   gAudioALSA.audio_step);

  gAudioALSA.metronom->set_audio_rate (gAudioALSA.metronom,gAudioALSA.audio_step);

  memcpy(&pcm_chan_params.format, &pcm_format, sizeof(snd_pcm_format_t));

  pcm_chan_params.mode           = mode;
  pcm_chan_params.channel        = gAudioALSA.direction;

  pcm_chan_params.start_mode     = SND_PCM_START_FULL;
  /*
    pcm_chan_params.start_mode    = SND_PCM_START_DATA;
    pcm_chan_params.stop_mode      = SND_PCM_STOP_STOP;
  */
  pcm_chan_params.stop_mode      = SND_PCM_STOP_ROLLOVER;
  
  gAudioALSA.start_mode = pcm_chan_params.start_mode;
  gAudioALSA.stop_mode = pcm_chan_params.stop_mode;
  gAudioALSA.ao_mode = ao_mode;

  if (ao_mode == AO_CAP_MODE_AC3) {
        pcm_chan_params.digital.dig_valid      = 1;
        pcm_chan_params.digital.dig_status[0]  = SND_PCM_DIG0_NONAUDIO;
        pcm_chan_params.digital.dig_status[0] |= SND_PCM_DIG0_PROFESSIONAL;
        pcm_chan_params.digital.dig_status[0] |= SND_PCM_DIG0_PRO_FS_48000;
        pcm_chan_params.digital.dig_status[3]  = SND_PCM_DIG3_CON_FS_48000;
  }

  snd_pcm_playback_flush(gAudioALSA.front_handle);
  if((err = snd_pcm_channel_params(gAudioALSA.front_handle, 
				   &pcm_chan_params)) < 0) {
    perr("snd_pcm_channel_params() failed: %s\n", snd_strerror(err));
    return 0;
  } 
  if((err = snd_pcm_playback_prepare(gAudioALSA.front_handle)) < 0) {
    perr("snd_pcm_channel_prepare() failed: %s\n", snd_strerror(err));
    return 0;
  }

  pcm_chan_setup.mode    = mode;
  pcm_chan_setup.channel = gAudioALSA.direction;

  if((err = snd_pcm_channel_setup(gAudioALSA.front_handle, 
				  &pcm_chan_setup)) < 0) {
    perr("snd_pcm_channel_setup() failed: %s\n", snd_strerror(err));
    return 0;
  }

  printf ("actual rate: %d\n", pcm_chan_setup.format.rate);

  alsa_set_frag(1536, 6);

  gAudioALSA.bytes_in_buffer = 0;

  return 1;
}

/* ------------------------------------------------------------------------- */
/*
 *
 */
static void ao_fill_gap (uint32_t pts_len) {
  int num_bytes;

  if (pts_len > MAX_GAP)
    pts_len = MAX_GAP;

  num_bytes = pts_len * gAudioALSA.bytes_per_kpts / 1024;
  num_bytes = (num_bytes / 4) * 4;
  
  gAudioALSA.bytes_in_buffer += num_bytes;
  
  printf ("audio_alsa_out: inserting %d 0-bytes to fill a gap of %d pts\n",
	  num_bytes, pts_len);

  while (num_bytes>0) {
    if (num_bytes>gAudioALSA.frag_size) {
      snd_pcm_write(gAudioALSA.front_handle, gAudioALSA.zero_space, 
     		    gAudioALSA.frag_size);
      num_bytes -= gAudioALSA.frag_size;
    } else {
      int old_frag_size = gAudioALSA.frag_size;

      alsa_set_frag(num_bytes, 6);

      snd_pcm_write(gAudioALSA.front_handle, gAudioALSA.zero_space, num_bytes);

      alsa_set_frag(old_frag_size, 6);

      num_bytes = 0;
    }
  } 

  gAudioALSA.last_vpts += pts_len ;
}

/* ------------------------------------------------------------------------- */
/*
 *
 */
static uint32_t ao_get_current_vpts (void) {
  int                       pos;
  snd_pcm_channel_status_t  pcm_stat;
  int                       err;
  int32_t                   diff;
  uint32_t                  vpts;

     
  if (gAudioALSA.audio_started) { 
    memset(&pcm_stat, 0, sizeof(snd_pcm_channel_status_t));
    pcm_stat.channel = SND_PCM_CHANNEL_PLAYBACK;
    if((err = snd_pcm_channel_status(gAudioALSA.front_handle, 
				     &pcm_stat)) < 0) {
      //Hide error report
      perr("snd_pcm_channel_status() failed: %s\n", snd_strerror(err));
      return 0;
    }
    pos = pcm_stat.scount;
  }
  else {
    pos = 0;
  }


  diff = gAudioALSA.sync_bytes_in_buffer - pos;
  vpts = gAudioALSA.sync_vpts - diff * 1024 / gAudioALSA.bytes_per_kpts;
  
  xprintf (AUDIO|VERBOSE,"audio_alsa_out: get_current_vpts pos=%d diff=%d "
	   "vpts=%d sync_vpts=%d sync_bytes_in_buffer %d\n", pos, diff, 
	   vpts, gAudioALSA.sync_vpts,gAudioALSA.sync_bytes_in_buffer);

  return vpts;
}

void audio_put_bytes(uint8_t *samples, uint32_t len)
{
static int old_bytes = 0;
static char buffer[8000];
int i;
if (old_bytes) {
  if (old_bytes + len < gAudioALSA.frag_size) {
    memcpy(&buffer[old_bytes],samples,len);
    old_bytes += len;
    return;
  }
  i = gAudioALSA.frag_size-old_bytes;
  memcpy(&buffer[old_bytes],samples,i);
  snd_pcm_write(gAudioALSA.front_handle, (void*)buffer,gAudioALSA.frag_size);
  len -= i;
  samples += i;
  old_bytes = 0;
}
while(len >= gAudioALSA.frag_size) {
  snd_pcm_write(gAudioALSA.front_handle, (void*)samples,gAudioALSA.frag_size);
  len -= gAudioALSA.frag_size;
  samples += gAudioALSA.frag_size;
}
if (len) {
  memcpy(buffer,samples,len);
  old_bytes = len;
}
return;

}


/* ------------------------------------------------------------------------- */
/*
 *
 */
static int ao_put_samples(ao_functions_t *this,int16_t* output_samples, 
			  uint32_t num_samples, uint32_t pts_) {
  uint32_t                  vpts;
  uint32_t                  audio_vpts;
  uint32_t                  master_vpts;
  int32_t                   diff, gap;
  int                       bDropPackage = 0;
  snd_pcm_channel_status_t  status_front;
  int                       err;
  uint16_t                  sample_buffer[gAudioALSA.frag_size*gAudioALSA.frag_count+6];

  
  xprintf(VERBOSE|AUDIO, "Audio : play %d samples at pts %d pos %d \n",
	  num_samples, pts_, gAudioALSA.bytes_in_buffer);   

  if (gAudioALSA.front_handle == NULL)
    return 1;

  //  if(gAudioALSA.frag_size != num_samples) {
  //    alsa_set_frag(num_samples, 6);
  //  }
  
  vpts = gAudioALSA.metronom->got_audio_samples (gAudioALSA.metronom,pts_, num_samples);

  if (vpts<gAudioALSA.last_audio_vpts) {
    /* reject this */

    return 1;
  }

  gAudioALSA.last_audio_vpts = vpts;

  /*
   * check if these samples "fit" in the audio output buffer
   * or do we have an audio "gap" here?
   */

  gap = vpts - gAudioALSA.last_vpts;

  xprintf (VERBOSE|AUDIO, "audio_alsa_out: got %d samples, vpts=%d, "
	   "last_vpts=%d\n", num_samples, vpts, gAudioALSA.last_vpts);

  if (gap > GAP_TOLERANCE) {

    /* FIXME : sync wont work without this
       ao_fill_gap (gap);
       */
    /* keep xine responsive */
    /*
    if (gap>MAX_GAP)
      return 0;
      */

  } 
  else if (gap < -GAP_TOLERANCE) {
    bDropPackage = 1;
  }
  
  /*
   * sync on master clock
   */

  audio_vpts  = ao_get_current_vpts () ;
  master_vpts = gAudioALSA.metronom->get_current_time (gAudioALSA.metronom);
  diff        = audio_vpts - master_vpts;
//printf("diff %d\n",diff);
  if (abs(diff) > 5000) {  /* this is a big jump, so check again */
    audio_vpts  = ao_get_current_vpts () ;
    diff        = audio_vpts - master_vpts;
    printf("double check is %d\n",abs(diff));
  }

  xprintf (VERBOSE|AUDIO,"audio_alsa_out: syncing on master clock: "
	   "audio_vpts=%d master_vpts=%d\n", audio_vpts, master_vpts);

  /*
   * method 1 : resampling
   */

  /*
  */

  /*
   * method 2: adjust master clock
   */
   if (gAudioALSA.ao_mode == AO_CAP_MODE_AC3) {   /* additional corrections for SPDIF speed */
     if (diff > 1)
       num_samples++;
     if (diff < -1)
       num_samples--;
   }
   if (gAudioALSA.ao_mode == AO_CAP_MODE_STEREO) {  /* fine tuning  */
     if (diff > 1) {
       memcpy(&output_samples[num_samples*4],&output_samples[num_samples*4]-4,4); /* duplicate last sample */
       num_samples++;
     }
     if (diff < -1)
       num_samples--;
   }

  if (abs(diff) > MAX_MASTER_CLOCK_DIV) {
    printf ("master clock adjust time %d -> %d\n", master_vpts, audio_vpts);
    gAudioALSA.metronom->adjust_clock (gAudioALSA.metronom,audio_vpts);
  }

  /*
   * resample and output samples
   */
  if (!bDropPackage) {
    int num_output_samples = 
      num_samples 
      * gAudioALSA.output_sample_rate
      / gAudioALSA.input_sample_rate;
	
//    if(num_output_samples != gAudioALSA.frag_size)
//      alsa_set_frag(num_output_samples, 6);

    if (gAudioALSA.ao_mode & AO_CAP_MODE_AC3) { 
       num_output_samples = num_samples;
       sample_buffer[0] = 0xf872;  //spdif syncword
       sample_buffer[1] = 0x4e1f;  // .............
       sample_buffer[2] = 0x0001;  // AC3 data
       sample_buffer[3] = 1536 * 16;
       sample_buffer[4] = 0x0b77;  // AC3 syncwork

       // ac3 seems to be swabbed data
       swab(&output_samples[1],&sample_buffer[5],  1536 * 2  );
       audio_put_bytes((uint8_t*) sample_buffer,
                    num_samples * 2 * gAudioALSA.num_channels);


    } else if (num_output_samples != num_samples ) {
      audio_out_resample_stereo (output_samples, num_samples,
				 sample_buffer, num_output_samples);
      audio_put_bytes((uint8_t*)sample_buffer, 
		    num_output_samples * 2 * gAudioALSA.num_channels);
    } else {
//    snd_pcm_write(gAudioALSA.front_handle, (void*)output_samples, 
//		    num_samples * 2 * gAudioALSA.num_channels);
      audio_put_bytes((uint8_t*)output_samples, 
		    num_samples * 2 * gAudioALSA.num_channels);
    }

    memset(&status_front, 0, sizeof(snd_pcm_channel_status_t));
    if((err = snd_pcm_channel_status(gAudioALSA.front_handle, 
				     &status_front)) < 0) {
      perr("snd_pcm_channel_status() failed: %s\n", snd_strerror(err));
    }
	
    /* Hummm, this seems made mistakes (flushing isnt good here). */
    /*
      if(status_front.underrun) {
      perr("underrun, resetting front channel\n");
      snd_pcm_channel_flush(gAudioALSA.front_handle, channel);
      snd_pcm_playback_prepare(gAudioALSA.front_handle);
      snd_pcm_write(gAudioALSA.front_handle, output_samples, num_samples<<1);
      if((err = snd_pcm_channel_status(gAudioALSA.front_handle, 
                                       &status_front)) < 0) {
      perr("snd_pcm_channel_status() failed: %s", snd_strerror(err));
      }
      if(status_front.underrun) {
      perr("front write error, giving up\n");
      }
      }
    */
    
    /*
     * remember vpts
     */
    
    gAudioALSA.sync_vpts            = vpts;
    gAudioALSA.sync_bytes_in_buffer = gAudioALSA.bytes_in_buffer;
    
    /*
     * step values
     */
    gAudioALSA.bytes_in_buffer += 
      num_output_samples * 2 * gAudioALSA.num_channels;

    gAudioALSA.audio_started = 1;
  } 
  else {
    printf ("audio_alsa_out: audio package (vpts = %d) dropped\n", vpts);
    gAudioALSA.sync_vpts = vpts;
  }
  
  gAudioALSA.last_vpts = 
    vpts + num_samples * 90000 / gAudioALSA.input_sample_rate ;


  return 1;
}

/* ------------------------------------------------------------------------- */
/*
 *
 */
static void ao_close(ao_functions_t *this) {
  int err;

  if(gAudioALSA.front_handle) {
    if((err = snd_pcm_playback_flush(gAudioALSA.front_handle)) < 0) {
      perr("snd_pcm_channel_flush() failed: %s\n", snd_strerror(err));
    }
    
  if((err = snd_pcm_close(gAudioALSA.front_handle)) < 0) {
    perr("snd_pcm_close() failed: %s\n", snd_strerror(err));
  }
  
  gAudioALSA.front_handle = NULL;
  }
}

static int ao_get_property (ao_functions_t *this, int property) {

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
static int ao_set_property (ao_functions_t *this, int property, int value) {

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
  gAudioALSA.metronom = metronom;
}

static uint32_t ao_get_capabilities (ao_functions_t *this_gen) {
  return gAudioALSA.capabilities;
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
}




/* ------------------------------------------------------------------------- */
/*
 *
 */

static ao_functions_t audio_alsaout;


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

  if((gAudioALSA.pcm_default_card = snd_defaults_pcm_card()) < 0) {
    perr("There is no default pcm card.\n");
    exit(1);
  }
  xprintf (VERBOSE|AUDIO, "snd_defaults_pcm_card() return %d\n", 
	   gAudioALSA.pcm_default_card);

  if((gAudioALSA.pcm_default_device = snd_defaults_pcm_device()) < 0) {
    perr("There is no default pcm device.\n");
    exit(1);
  }
  xprintf (VERBOSE|AUDIO, "snd_defaults_pcm_device() return %d\n", 
	   gAudioALSA.pcm_default_device);

  gAudioALSA.capabilities = AO_CAP_MODE_STEREO;
  if (config->lookup_int (config, "ac3_pass_through", 0)) 
    gAudioALSA.capabilities |= AO_CAP_MODE_AC3;



  audio_alsaout.get_capabilities = ao_get_capabilities;
  audio_alsaout.get_property     = ao_get_property;
  audio_alsaout.set_property     = ao_set_property;
  audio_alsaout.connect          = ao_connect;
  audio_alsaout.open             = ao_open;
  audio_alsaout.write_audio_data = ao_put_samples;
  audio_alsaout.close            = ao_close;
  audio_alsaout.exit             = ao_exit;




  
  action.sa_handler = sighandler;
  sigemptyset(&(action.sa_mask));
  action.sa_flags = 0;
  if(sigaction(SIGALRM, &action, NULL) != 0) {
    perr("sigaction(SIGALRM) failed: %s\n", strerror(errno));
  }
  alarm(2);

  if((err = snd_pcm_open(&gAudioALSA.front_handle, gAudioALSA.pcm_default_card,
			 gAudioALSA.pcm_default_device, direction)) < 0) {
    perr("snd_pcm_open() failed: %s\n", snd_strerror(err));
    perr(">>> Check if another program don't already use PCM <<<\n");
    return NULL;
  }
  
  memset(&pcm_info, 0, sizeof(snd_pcm_info_t));
  if((err = snd_pcm_info(gAudioALSA.front_handle, &pcm_info)) < 0) {
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
  if((err = snd_pcm_channel_info(gAudioALSA.front_handle, 
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
  
  snd_pcm_close (gAudioALSA.front_handle);
  gAudioALSA.front_handle = NULL;

  return &audio_alsaout;
}

#endif
