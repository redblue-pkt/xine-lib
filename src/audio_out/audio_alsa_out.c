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
 * $Id: audio_alsa_out.c,v 1.6 2001/06/01 07:25:26 f1rmb Exp $
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
#include <sys/asoundlib.h>
#include <sys/ioctl.h>
#include <inttypes.h>

#if (SND_LIB_MAJOR >= 0) && (SND_LIB_MINOR >= 9)

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)
#define error(...) do {\
        fprintf(stderr, "XINE lib %s:%d:(%s) ", __FILE__, __LINE__, __FUNCTION__); \
        fprintf(stderr, __VA_ARGS__); \
        putc('\n', stderr); \
} while (0)
#else
#define error(args...) do {\
        fprintf(stderr, "XINE lib %s:%d:(%s) ", __FILE__, __LINE__, __FUNCTION__); \
        fprintf(stderr, ##args); \
        putc('\n', stderr); \
} while (0)
#endif


#include "xine_internal.h"
#include "monitor.h"
#include "audio_out.h"
#include "resample.h"
#include "metronom.h"
#include "utils.h"

#ifndef AFMT_S16_NE
# if defined(sparc) || defined(__sparc__) || defined(PPC)
/* Big endian machines */
#  define AFMT_S16_NE AFMT_S16_BE
# else
#  define AFMT_S16_NE AFMT_S16_LE
# endif
#endif

#define AO_OUT_ALSA_IFACE_VERSION 1

#define GAP_TOLERANCE        15000
#define MAX_MASTER_CLOCK_DIV  5000

typedef struct alsa_functions_s {

  ao_functions_t ao_functions;

  metronom_t         *metronom;

  char                audio_dev[20];
  snd_pcm_t *         audio_fd;
  int                 open_mode;

  int32_t             output_sample_rate, input_sample_rate;
  int32_t             output_rate_correction;
  double              sample_rate_factor;
  uint32_t            num_channels;
/* The ALSA drivers handle "frames" instead of bytes.
 * So for a Stereo 16 Bit Sample, each frame would equil 4 bytes.
 * For this plugin, we will use frames instead of bytes for everything.
 * The term sample is also equil to frames
  */
  snd_pcm_sframes_t   frames_in_buffer;      /* number of frames writen to audio hardware   */
  uint32_t            last_vpts;            /* vpts at which last written package ends    */

  uint32_t            sync_vpts;            /* this syncpoint is used as a starting point */
  snd_pcm_sframes_t   sync_frames_in_buffer; /* for vpts <-> samplecount assoc             */

  int                 audio_step;           /* pts per 32 768 frames (frame = #bytes/2(16 bits)/channels) */
/* frames = pts * rate / pts_per_second */
/* pts    = frame * pts_per_second / rate  */

  snd_pcm_sframes_t   pts_per_second;       /* pts per second                 */
                                            
  int16_t            *zero_space;
  
  int                audio_started;

  int                capabilities;

} alsa_functions_t;

  static snd_output_t *jcd_out;
/*
 * open the audio device for writing to
 */
static int ao_open(ao_functions_t *this_gen, uint32_t bits, uint32_t rate, int mode)
{
  alsa_functions_t *this = (alsa_functions_t *) this_gen;
  snd_pcm_stream_t    direction = SND_PCM_STREAM_PLAYBACK; 
  snd_pcm_hw_params_t *params;
  snd_pcm_sw_params_t *swparams;
  snd_pcm_sframes_t   buffer_time;
  snd_pcm_sframes_t   period_time,tmp;
  int                 err, step;
  int                 open_mode=1; //NONBLOCK
  //int                 open_mode=0; //BLOCK
  snd_pcm_hw_params_alloca(&params);
  snd_pcm_sw_params_alloca(&swparams);
  
  err = snd_output_stdio_attach(&jcd_out, stderr, 0);
  if (((mode & AO_CAP_MODE_STEREO) == 0) && ((mode & AO_CAP_MODE_AC3) == 0)) {
    error ("ALSA Driver only supports AC3/stereo output modes at the moment");
    return -1;
  } else {
    this->num_channels = 2;
  }
  if (this->audio_fd != NULL) {
    error ("Already open...WHY!");
    snd_pcm_close (this->audio_fd);
  }
  this->input_sample_rate      = rate;
  this->frames_in_buffer       = 0;
  this->last_vpts              = 0;
  this->output_rate_correction = 0;
  this->sync_vpts              = 0;
  this->sync_frames_in_buffer  = 0;
  this->audio_started          = 0;
  this->open_mode              = mode;
  /*
   * open audio device
   */

  err=snd_pcm_open(&this->audio_fd, this->audio_dev, direction, open_mode);      
  if(err <0 ) {                                                           
    error("snd_pcm_open() failed: %s", snd_strerror(err));               
    error(">>> Check if another program don't already use PCM <<<");     
    return -1;                                                          
  }
  /* We wanted non blocking open but now put it back to normal */
  snd_pcm_nonblock(this->audio_fd, 0);
  /*
   * configure audio device
   */
        err = snd_pcm_hw_params_any(this->audio_fd, params);
        if (err < 0) {
                error("Broken configuration for this PCM: no configurations available");
                goto __close;
        }
        /* set interleaved access */
        err = snd_pcm_hw_params_set_access(this->audio_fd, params,
                                           SND_PCM_ACCESS_RW_INTERLEAVED);
        if (err < 0) {
                error("Access type not available");
                goto __close;
        }
        err = snd_pcm_hw_params_set_format(this->audio_fd, params, bits == 16 ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_U8);
        if (err < 0) {
                error("Sample format non available");
                goto __close;
        }
        err = snd_pcm_hw_params_set_channels(this->audio_fd, params, this->num_channels);
        if (err < 0) {
                error("Channels count non available");
                goto __close;
        }
        err = snd_pcm_hw_params_set_rate_near(this->audio_fd, params, rate, 0);
        if (err < 0) {
                error("Rate not available");
                goto __close;
        }
        buffer_time = snd_pcm_hw_params_set_buffer_time_near(this->audio_fd, params,
                                                             500000, 0);
        if (buffer_time < 0) {
                error("Buffer time not available");
                goto __close;
        }
               step = 2;
        period_time = 10000 * 2;
        do {
                period_time /= 2;
                tmp = snd_pcm_hw_params_set_period_time_near(this->audio_fd, params,
                                                             period_time, 0);
                if (tmp == period_time) {
                        period_time /= 3;
                        tmp = snd_pcm_hw_params_set_period_time_near(this->audio_fd, params,
                                                                     period_time, 0);
                        if (tmp == period_time)
                                period_time = 10000 * 2;
                }
                if (period_time < 0) {
                        fprintf(stderr, "Period time not available");
                        goto __close;
                }
        } while (buffer_time == period_time && period_time > 10000);
        if (buffer_time == period_time) {
                error("Buffer time and period time match, could not use");
                goto __close;
        }
        if ((err = snd_pcm_hw_params(this->audio_fd, params)) < 0) {
                error("PCM hw_params failed: %s", snd_strerror(err));
                goto __close;
        }
this->output_sample_rate = this->input_sample_rate;
  this->sample_rate_factor = (double) this->output_sample_rate / (double) this->input_sample_rate;
  this->audio_step         = (double) 90000 * (double) 32768 
                                 / this->input_sample_rate;
  this->pts_per_second     = 90000;
  this->metronom->set_audio_rate(this->metronom, this->audio_step);
  /*
   * audio buffer size handling
   */
  /* Copy current parameters into swparams */
  snd_pcm_sw_params_current(this->audio_fd, swparams);
  tmp=snd_pcm_sw_params_set_xfer_align(this->audio_fd, swparams, 4);
  /* Install swparams into current parameters */
  snd_pcm_sw_params(this->audio_fd, swparams);

  //  snd_pcm_dump_setup(this->audio_fd, jcd_out); 
  return 1;
__close:
  snd_pcm_close (this->audio_fd);
  this->audio_fd=NULL;
  return -1;
}

static uint32_t ao_get_current_vpts (alsa_functions_t *this) 
{
  snd_pcm_sframes_t pos ;
  snd_pcm_status_t  *pcm_stat;
  snd_pcm_sframes_t delay;
  int err;
  uint32_t vpts ;
  snd_pcm_status_alloca(&pcm_stat);
  snd_pcm_status(this->audio_fd, pcm_stat);
  /* Dump ALSA info to stderr */
  /* snd_pcm_status_dump(pcm_stat, jcd_out);  */
  if (this->audio_started) {
    err=snd_pcm_delay( this->audio_fd, &delay);
    if(err < 0) {
      //Hide error report
      error("snd_pcm_delay() failed");
      return 0;
    }
    /* Correction factor, bigger -, sound earlier
     *                    bigger +, sound later
     * current setting for SB Live
     */    
    pos = this->frames_in_buffer - delay + 1500;  
  } else {
    pos=0;
  }
  vpts =  ((double)pos * (double)this->pts_per_second / (double)this->input_sample_rate);
  return vpts;
}

static void ao_fill_gap (alsa_functions_t *this, uint32_t pts_len)
{
  snd_pcm_sframes_t res; 
  int num_frames = (double)pts_len * (double)this->input_sample_rate / (double)this->pts_per_second;
  num_frames = (num_frames / 4) * 4;
  this->frames_in_buffer += num_frames;
  while (num_frames>0) {
    if (num_frames>2048) {
      res=snd_pcm_writei(this->audio_fd, this->zero_space, 2048 );
      num_frames -= 2048;
    } else {
      res=snd_pcm_writei(this->audio_fd, this->zero_space, num_frames );
      num_frames = 0;
    }
  }
  this->last_vpts += pts_len;
}

static void ao_write_audio_data(ao_functions_t *this_gen,
				int16_t* output_samples, uint32_t num_samples, 
				uint32_t pts_)
{

  alsa_functions_t *this = (alsa_functions_t *) this_gen;
  uint32_t vpts,
           audio_vpts,
           master_vpts;
  int32_t  diff, gap;
  int      bDropPackage;
  uint16_t sample_buffer[8192];
  int      num_output_samples;
  snd_pcm_sframes_t res = 0;                                                                

  if (this->audio_fd == NULL) {
    error("Nothing open");
    return;
  }

  vpts        = this->metronom->got_audio_samples (this->metronom, pts_, num_samples);
  /*
   * check if these samples "fit" in the audio output buffer
   * or do we have an audio "gap" here?
   */
  gap = vpts - this->last_vpts ;
  bDropPackage = 0;
  
  if (gap>GAP_TOLERANCE) {
    ao_fill_gap (this, gap);
  } else if (gap<-GAP_TOLERANCE) {
    bDropPackage = 1;
  }

  /*
   * sync on master clock
   */
  audio_vpts  = ao_get_current_vpts (this) ;
  master_vpts = this->metronom->get_current_time (this->metronom);
  diff        = audio_vpts - master_vpts;
  /*
   * method 1 : resampling
   */
  if (abs(diff)>5000) {
    if (diff>5000) {
      error("Fill Gap");
      ao_fill_gap (this,diff);
    } else if (diff<-5000) {
      error("Drop");
      bDropPackage = 1;
    }
  } else if (abs(diff)>1000) {
    this->output_rate_correction = diff/10 ; 
    error("diff = %d => rate correction : %d", diff, this->output_rate_correction);  
    if ( this->output_rate_correction < -500)
      this->output_rate_correction = -500;
    else if ( this->output_rate_correction > 500)
      this->output_rate_correction = 500;
  }
  /*
   * method 2: adjust master clock
   */
  if (abs(diff)>MAX_MASTER_CLOCK_DIV) {
    error ("master clock adjust time %d -> %d (diff: %d)", master_vpts, audio_vpts, diff); 
    this->metronom->adjust_clock (this->metronom, audio_vpts); 
  }
  /*
   * resample and output samples
   */
  if (!bDropPackage) {
    if ((this->open_mode & AO_CAP_MODE_AC3) == 0) {
      /* Multiples of xfer_align eg:- 4 */
      num_output_samples = ((num_samples * (this->output_sample_rate + this->output_rate_correction) / this->input_sample_rate / 4) * 4)+4; 
      audio_out_resample_stereo (output_samples, num_samples,
			       sample_buffer, num_output_samples);
    } else {
       num_output_samples = num_samples;
       sample_buffer[0] = 0xf872;  //spdif syncword
       sample_buffer[1] = 0x4e1f;  // .............
       sample_buffer[2] = 0x0001;  // AC3 data
       sample_buffer[3] = num_samples * 16;
       sample_buffer[4] = 0x0b77;  // AC3 syncwork

       // ac3 seems to be swabbed data
       swab(output_samples,&sample_buffer[5],  num_samples * 2 );
    }

    do {
         res=snd_pcm_avail_update(this->audio_fd);
         usleep(3200);
    } while (res<num_output_samples+512);

    /* Special note, the new ALSA outputs in counts of frames.
     * A Frame is one sample for all channels, so here a Stereo 16 bits frame is 4 bytes.
     */
    res=snd_pcm_writei(this->audio_fd, sample_buffer, num_output_samples);    


    if(res != num_output_samples) error("BUFFER MAYBE FULL!!!!!!!!!!!!");
    if (res < 0)                                                   
             error("writei returned error: %s", snd_strerror(res));                    
    /*
     * remember vpts
     */
    this->sync_vpts            = vpts;
    this->sync_frames_in_buffer = this->frames_in_buffer;
    /*
     * step values
     */
    this->frames_in_buffer += num_samples ;
    this->audio_started    = 1;
  } else {
    this->sync_vpts            = vpts;
  }
  
  this->last_vpts        = vpts + num_samples * this->pts_per_second / this->input_sample_rate ; 
}


static void ao_close(ao_functions_t *this_gen)
{
  alsa_functions_t *this = (alsa_functions_t *) this_gen;
  if(this->audio_fd) snd_pcm_close(this->audio_fd);
  this->audio_fd = NULL;
}

static uint32_t ao_get_capabilities (ao_functions_t *this_gen) {
  alsa_functions_t *this = (alsa_functions_t *) this_gen;
  return this->capabilities;
}

static void ao_connect (ao_functions_t *this_gen, metronom_t *metronom) {
  alsa_functions_t *this = (alsa_functions_t *) this_gen;
  this->metronom = metronom;
}

static void ao_exit(ao_functions_t *this_gen)
{
  alsa_functions_t *this = (alsa_functions_t *) this_gen;
  if (this->audio_fd) snd_pcm_close(this->audio_fd);
  free (this->zero_space);
  free (this);
}

/*
 *
 */
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

ao_functions_t *init_audio_out_plugin (config_values_t *config) {

  alsa_functions_t *this;
  int              card;
  int              dev;
  int              err;

  this = (alsa_functions_t *) malloc (sizeof (alsa_functions_t));
  
  strcpy(this->audio_dev,"plug:0,0");

  /*
   * find best device driver/channel
   */
  /*
   * open that device
   */
                card = snd_defaults_pcm_card();
                dev = snd_defaults_pcm_device();
                if (card < 0 || dev < 0) {
                        fprintf(stderr, "defaults are not set");
                        return NULL;
                }
  
  err=snd_pcm_open(&this->audio_fd, this->audio_dev, SND_PCM_STREAM_PLAYBACK, 0);         
  if(err <0 ) {                                                                       
    error("snd_pcm_open() failed: %d", err);                           
    error(">>> Check if another program don't already use PCM <<<");                 
    return NULL;                                                                      
  }
  snd_pcm_close (this->audio_fd);
  this->audio_fd=NULL;
  this->output_sample_rate = 0;
  this->capabilities       = AO_CAP_MODE_STEREO;
  if (config->lookup_int (config, "ac3_pass_through", 0)) {
    this->capabilities |= AO_CAP_MODE_AC3;
    strcpy(this->audio_dev,"plug:0,2");
    printf("AC3 pass through activated\n");
  }
   

  this->zero_space = malloc (8192);
  memset (this->zero_space, 0, 8192);

  this->ao_functions.get_capabilities    = ao_get_capabilities;
  this->ao_functions.get_property        = ao_get_property;
  this->ao_functions.set_property        = ao_set_property;
  this->ao_functions.connect             = ao_connect;
  this->ao_functions.open                = ao_open;
  this->ao_functions.write_audio_data    = ao_write_audio_data;
  this->ao_functions.close               = ao_close;
  this->ao_functions.exit                = ao_exit;

  return &this->ao_functions;
}

static ao_info_t ao_info_alsa = {
  AUDIO_OUT_IFACE_VERSION,
  "alsa09",
  "xine audio output plugin using alsa-compliant audio devices/drivers",
  10
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_alsa;
}

#endif /*(SND_LIB_MAJOR >= 0) && (SND_LIB_MINOR >= 9) */
