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
 * - for the SPDIF AC3 sync part
 * - frame size calculation added (16-08-2001)
 * (c) 2001 Andy Lo A Foe <andy@alsaplayer.org>
 * for initial ALSA 0.9.x support.
 * (c) 2001 James Courtier-Dutton <James@superbug.demon.co.uk>
 *
 * 
 * $Id: audio_alsa_out.c,v 1.17 2001/08/24 01:05:30 guenter Exp $
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

#define AO_OUT_ALSA_IFACE_VERSION 2

#define GAP_TOLERANCE         5000
#define MAX_MASTER_CLOCK_DIV  5000
#define MAX_GAP              90000

typedef struct alsa_driver_s {

  ao_driver_t ao_driver;
  char                audio_dev[20];
  snd_pcm_t *         audio_fd;
  int                 capabilities;
  int                 open_mode;

  int32_t             output_sample_rate, input_sample_rate;
  int32_t             output_rate_correction;
  double              sample_rate_factor;
  uint32_t            num_channels;
  uint32_t       bits_per_sample;
  uint32_t       bytes_per_frame;
  uint32_t            bytes_in_buffer;      /* number of bytes writen to audio hardware   */


  int                audio_started;

} alsa_driver_t;

  static snd_output_t *jcd_out;
/*
 * open the audio device for writing to
 */
static int ao_alsa_open(ao_driver_t *this_gen, uint32_t bits, uint32_t rate, int mode)
{
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
  snd_pcm_stream_t    direction = SND_PCM_STREAM_PLAYBACK; 
  snd_pcm_hw_params_t *params;
  snd_pcm_sw_params_t *swparams;
  snd_pcm_sframes_t   buffer_time;
  snd_pcm_sframes_t   period_time,tmp;
  snd_aes_iec958_t    spdif;
  snd_ctl_elem_value_t *ctl;
  snd_ctl_t            *ctl_handle;
  snd_pcm_info_t        *info;

  char                 ctl_name[12];
  int                  ctl_card;


  int                 err, step;
 // int                 open_mode=1; //NONBLOCK
  int                 open_mode=0; //BLOCK
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

  this->open_mode              = mode;
  this->input_sample_rate      = rate;
  this->bits_per_sample        = bits;
  this->bytes_in_buffer        = 0;
  this->output_rate_correction = 0;
  this->audio_started          = 0;
  /* FIXME: Can use an ALSA function here */
  this->bytes_per_frame=(this->bits_per_sample*this->num_channels)/8;
  /*
   * open audio device
   */

  err=snd_pcm_open(&this->audio_fd, this->audio_dev, direction, open_mode);      
  if(err <0 ) {                                                           
    error("snd_pcm_open() failed: %s", snd_strerror(err));               
    error(">>> Check if another program don't already use PCM <<<");     
    return -1;                                                          
  }

       if (mode & AO_CAP_MODE_AC3) {

           snd_pcm_info_alloca(&info);

           if ((err = snd_pcm_info(this->audio_fd, info)) < 0) {
             fprintf(stderr, "info: %s\n", snd_strerror(err));
             goto __close;
           }
           printf("device: %d, subdevice: %d\n", snd_pcm_info_get_device(info),
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
                fprintf(stderr, "Unable to setup the IEC958 (S/PDIF) interface - PCM has no assigned card");
                   goto __close;

           }
           sprintf(ctl_name, "hw:%d", ctl_card);
           printf("hw:%d\n", ctl_card);
           if ((err = snd_ctl_open(&ctl_handle, ctl_name, 0)) < 0) {
              fprintf(stderr, "Unable to open the control interface '%s':
                                            %s", ctl_name, snd_strerror(err));
              goto __close;
           }
           if ((err = snd_ctl_elem_write(ctl_handle, ctl)) < 0) {
              fprintf(stderr, "Unable to update the IEC958 control: %s", snd_strerror(err));

              goto __close;
           }
           snd_ctl_close(ctl_handle);
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
  /*
   * audio buffer size handling
   */
  /* Copy current parameters into swparams */
  snd_pcm_sw_params_current(this->audio_fd, swparams);
  tmp=snd_pcm_sw_params_set_xfer_align(this->audio_fd, swparams, 4);
  tmp=snd_pcm_sw_params_set_avail_min(this->audio_fd, swparams, 1);
  tmp=snd_pcm_sw_params_set_start_threshold(this->audio_fd, swparams, 1);

  /* Install swparams into current parameters */
  snd_pcm_sw_params(this->audio_fd, swparams);

    snd_pcm_dump_setup(this->audio_fd, jcd_out); 
    snd_pcm_sw_params_dump(swparams, jcd_out);
      

  //  write_pause_burst(this,0);


  return 1;
__close:
  snd_pcm_close (this->audio_fd);
  this->audio_fd=NULL;
  return -1;
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
  snd_pcm_sframes_t pos ;
  snd_pcm_status_t  *pcm_stat;
  snd_pcm_sframes_t delay;
  int err;
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
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
  }
  return delay;
}

void xrun(alsa_driver_t *this)
{
        snd_pcm_status_t *status;
        int res;

        snd_pcm_status_alloca(&status);
        if ((res = snd_pcm_status(this->audio_fd, status))<0) {
            printf("status error: %s", snd_strerror(res));
           return;
         }
         if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
            struct timeval now, diff, tstamp;
            gettimeofday(&now, 0);
            snd_pcm_status_get_trigger_tstamp(status, &tstamp);
            timersub(&now, &tstamp, &diff);
            fprintf(stderr, "xrun!!! (at least %.3f ms long)\n", diff.tv_sec * 1000 + diff.tv_usec / 1000.0);

         if ((res = snd_pcm_prepare(this->audio_fd))<0) {
                printf("xrun: prepare error: %s", snd_strerror(res));
                return;
            }
            return;         /* ok, data should be accepted again */
         }
}

static int ao_alsa_write(ao_driver_t *this_gen,int16_t *data, uint32_t count)
{
    ssize_t r;
  alsa_driver_t *this = (alsa_driver_t *) this_gen;
	
   while( count > 0) {
      r = snd_pcm_writei(this->audio_fd, data, count);
      if (r == -EAGAIN || (r >=0 && r < count)) {
        snd_pcm_wait(this->audio_fd, 1000);
      } else if (r == -EPIPE) {
        xrun(this);
      }
      if (r > 0) {
        count -= r;
	/* FIXME: maybe not *2 as int16 */
        data += r * 2 * this->num_channels;
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
  if (this->audio_fd) snd_pcm_close(this->audio_fd);
  free (this);
}

/*
 *
 */
static int ao_alsa_get_property (ao_driver_t *this, int property) {

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
static int ao_alsa_set_property (ao_driver_t *this, int property, int value) {

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

ao_driver_t *init_audio_out_plugin (config_values_t *config) {

  alsa_driver_t *this;
  int              card;
  int              dev;
  int              err;
  char             *pcm_device;
  char             *ac3_device;

  this = (alsa_driver_t *) malloc (sizeof (alsa_driver_t));
 
  pcm_device = config->lookup_str(config,"alsa_pcm_device", "hw:0,0");
  ac3_device = config->lookup_str(config,"alsa_ac3_device", "hw:0,2");

 
  strcpy(this->audio_dev,pcm_device);
  
  /*
   * find best device driver/channel
   */
  /*
   * open that device
   */
  
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
    strcpy(this->audio_dev,ac3_device);
    printf("AC3 pass through activated\n");
  }
   
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
  AUDIO_OUT_IFACE_VERSION,
  "alsa09",
  "xine audio output plugin using alsa-compliant audio devices/drivers",
  10
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_alsa9;
}
