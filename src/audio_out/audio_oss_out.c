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
 * $Id: audio_oss_out.c,v 1.4 2001/05/07 02:25:00 f1rmb Exp $
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
#if defined(__OpenBSD__)
#include <soundcard.h>
#elif defined(__FreeBSD__)
#include <machine/soundcard.h>
#else
#if defined(__linux__)
#include <linux/config.h> /* Check for DEVFS */
#endif
#include <sys/soundcard.h>
#endif
#include <sys/ioctl.h>
#include <inttypes.h>

#include "xine_internal.h"
#include "monitor.h"
#include "audio_out.h"
#include "resample.h"
#include "metronom.h"
#include "utils.h"

#define AO_OUT_OSS_IFACE_VERSION 1

#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

#define GAP_TOLERANCE        15000
#define MAX_MASTER_CLOCK_DIV  5000

#ifdef CONFIG_DEVFS_FS
#define DSP_TEMPLATE "/dev/sound/dsp%d"
#else
#define DSP_TEMPLATE "/dev/dsp%d"
#endif

typedef struct oss_functions_s {

  ao_functions_t ao_functions;

  metronom_t    *metronom;

  char           audio_dev[20];
  int            audio_fd;

  int32_t        output_sample_rate, input_sample_rate;
  int32_t        output_rate_correction;
  double         sample_rate_factor;
  uint32_t       num_channels;

  uint32_t       bytes_in_buffer;      /* number of bytes writen to audio hardware   */
  uint32_t       last_vpts;            /* vpts at which last written package ends    */

  uint32_t       sync_vpts;            /* this syncpoint is used as a starting point */
  uint32_t       sync_bytes_in_buffer; /* for vpts <-> samplecount assoc             */

  int            audio_step;           /* pts per 32 768 samples (sample = #bytes/2) */
  int32_t        bytes_per_kpts;       /* bytes per 1024/90000 sec                   */

  int16_t       *zero_space;
  
  int            audio_started;

} oss_functions_t;

/*
 * open the audio device for writing to
 */
static int ao_open(ao_functions_t *this_gen,
		   uint32_t bits, uint32_t rate, int mode)
{
  oss_functions_t *this = (oss_functions_t *) this_gen;
  int tmp;
  int fsize;

  printf ("audio_oss_out: ao_open rate=%d, mode=%d\n", rate, mode);

  if ((mode != AO_MODE_STEREO) && (mode != AO_MODE_MONO)) {
    printf ("OSS Driver only supports mono/stereo output modes at the moment\n");
    return -1;
  }

  if (this->audio_fd > -1) {

    if (rate == this->input_sample_rate)
      return 1;

    close (this->audio_fd);
  }

  this->input_sample_rate      = rate;
  this->bytes_in_buffer        = 0;
  this->last_vpts              = 0;
  this->output_rate_correction = 0;
  this->sync_vpts              = 0;
  this->sync_bytes_in_buffer   = 0;
  this->audio_started          = 0;

  /*
   * open audio device
   */

  this->audio_fd=open(this->audio_dev,O_WRONLY|O_NDELAY);
  if(this->audio_fd < 0) {
    printf("audio_oss_out: Opening audio device %s: %s\n",
	   this->audio_dev, strerror(errno));
    return -1;
  }
  
  /* We wanted non blocking open but now put it back to normal */
  fcntl(this->audio_fd, F_SETFL, fcntl(this->audio_fd, F_GETFL)&~FNDELAY);

  /*
   * configure audio device
   */

  tmp = (mode == AO_MODE_STEREO) ? 1 : 0;
  ioctl(this->audio_fd,SNDCTL_DSP_STEREO,&tmp);

  this->num_channels = tmp+1;
  xprintf (VERBOSE|AUDIO, "audio_oss_out: %d channels\n",this->num_channels);

  tmp = bits;
  ioctl(this->audio_fd,SNDCTL_DSP_SAMPLESIZE,&tmp);

  tmp = this->input_sample_rate;
  ioctl(this->audio_fd,SNDCTL_DSP_SPEED, &tmp);
  this->output_sample_rate = tmp;

  xprintf (VERBOSE|AUDIO, "audio_oss_out: audio rate : %d requested, %d provided by device/sec\n",
	   this->input_sample_rate, this->output_sample_rate);

  this->sample_rate_factor = (double) this->output_sample_rate / (double) this->input_sample_rate;
  this->audio_step         = (uint32_t) 90000 * (uint32_t) 32768 
                                 / this->input_sample_rate;
  this->bytes_per_kpts     = this->output_sample_rate * this->num_channels * 2 * 1024 / 90000;

  xprintf (VERBOSE|AUDIO, "audio_out : audio_step %d pts per 32768 samples\n", this->audio_step);

  this->metronom->set_audio_rate(this->metronom, this->audio_step);

  /*
   * audio buffer size handling
   */

  tmp=0 ;
  fsize = AUDIO_FRAGMENT_SIZE;
  while (fsize>0) {
    fsize /=2;
    tmp++;
  }
  tmp--;

  tmp = (AUDIO_NUM_FRAGMENTS << 16) | tmp ;

  xprintf (VERBOSE|AUDIO, "Audio buffer fragment info : %x\n",tmp);

  ioctl(this->audio_fd,SNDCTL_DSP_SETFRAGMENT,&tmp); 


  return 1;
}

static uint32_t ao_get_current_vpts (oss_functions_t *this) {

  int      pos ;
  int32_t  diff ;
  uint32_t vpts ;
  
  count_info info;
  
  if (this->audio_started) {
    ioctl (this->audio_fd, SNDCTL_DSP_GETOPTR, &info);
  
    pos = info.bytes;

  } else
    pos = 0;

  diff = this->sync_bytes_in_buffer - pos;
  
  vpts = this->sync_vpts - diff * 1024 / this->bytes_per_kpts;

  xprintf (AUDIO|VERBOSE,"audio_oss_out: get_current_vpts pos=%d diff=%d vpts=%d sync_vpts=%d\n",
	   pos, diff, vpts, this->sync_vpts);

  return vpts;
}

static void ao_fill_gap (oss_functions_t *this, uint32_t pts_len) {

  int num_bytes = pts_len * this->bytes_per_kpts / 1024;
  
  num_bytes = (num_bytes / 4) * 4;

  printf ("audio_oss_out: inserting %d 0-bytes to fill a gap of %d pts\n",num_bytes, pts_len);
  
  this->bytes_in_buffer += num_bytes;
  
  while (num_bytes>0) {
    if (num_bytes>8192) {
      write(this->audio_fd, this->zero_space, 8192);
      num_bytes -= 8192;
    } else {
      write(this->audio_fd, this->zero_space, num_bytes);
      num_bytes = 0;
    }
  }
  
  this->last_vpts += pts_len;
}

static void ao_write_audio_data(ao_functions_t *this_gen,
				int16_t* output_samples, uint32_t num_samples, 
				uint32_t pts_)
{

  oss_functions_t *this = (oss_functions_t *) this_gen;
  uint32_t vpts,
           audio_vpts,
           master_vpts;
  int32_t  diff, gap;
  int      bDropPackage;
  uint16_t sample_buffer[8192];

  
  if (this->audio_fd<0)
    return;

  vpts        = this->metronom->got_audio_samples (this->metronom, pts_, num_samples);

  xprintf (VERBOSE|AUDIO, "audio_oss_out: got %d samples, vpts=%d, last_vpts=%d\n",
	   num_samples, vpts, this->last_vpts);

  /*
   * check if these samples "fit" in the audio output buffer
   * or do we have an audio "gap" here?
   */
  
  gap = vpts - this->last_vpts ;
  
  /*
    printf ("audio_oss_out: gap = %d - %d + %d = %d\n",
    vpts, this->last_vpts, diff, gap);
  */

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

  xprintf (AUDIO|VERBOSE, "audio_oss_out: syncing on master clock: audio_vpts=%d master_vpts=%d\n",
	   audio_vpts, master_vpts);
  /*
  printf ("audio_oss_out: audio_vpts=%d <=> master_vpts=%d (diff=%d)\n",
	  audio_vpts, master_vpts, diff);
  */
  /*
   * method 1 : resampling
   */

  /*
  if (abs(diff)>5000) {

    if (diff>5000) {
      ao_fill_gap (diff);
    } else if (diff<-5000) {
      bDropPackage = 1;
    }

  } else if (abs(diff)>1000) {
    this->output_rate_correction = diff/10 ; 
    
    printf ("audio_oss_out: diff = %d => rate correction : %d\n", diff, this->output_rate_correction);  
    
    if ( this->output_rate_correction < -500)
      this->output_rate_correction = -500;
    else if ( this->output_rate_correction > 500)
      this->output_rate_correction = 500;
  }
  */

  /*
   * method 2: adjust master clock
   */
  
  
  if (abs(diff)>MAX_MASTER_CLOCK_DIV) {
    printf ("master clock adjust time %d -> %d (diff: %d)\n", master_vpts, audio_vpts, diff); 
    this->metronom->adjust_clock (this->metronom, audio_vpts); 
  }
  

  /*
   * resample and output samples
   */

  if (!bDropPackage) {
    int num_output_samples = num_samples * (this->output_sample_rate + this->output_rate_correction) / this->input_sample_rate;


    audio_out_resample_stereo (output_samples, num_samples,
			       sample_buffer, num_output_samples);
    
    write(this->audio_fd, sample_buffer, num_output_samples * 2 * this->num_channels);

    xprintf (AUDIO|VERBOSE, "audio_oss_out :audio package written\n");
    
    /*
     * remember vpts
     */
    
    this->sync_vpts            = vpts;
    this->sync_bytes_in_buffer = this->bytes_in_buffer;

    /*
     * step values
     */
    
    this->bytes_in_buffer += num_output_samples * 2 * this->num_channels;
    this->audio_started    = 1;
  } else {
    printf ("audio_oss_out: audio package (vpts = %d) dropped\n", vpts);
    this->sync_vpts            = vpts;
  }
  
  this->last_vpts        = vpts + num_samples * 90000 / this->input_sample_rate ; 
}


static void ao_close(ao_functions_t *this_gen)
{
  oss_functions_t *this = (oss_functions_t *) this_gen;
  close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_get_supported_modes (ao_functions_t *this) {
  return AO_MODE_STEREO | AO_MODE_MONO;
}

static void ao_connect (ao_functions_t *this_gen, metronom_t *metronom) {
  oss_functions_t *this = (oss_functions_t *) this_gen;
  
  this->metronom = metronom;
}

static void ao_exit(ao_functions_t *this_gen)
{
  oss_functions_t *this = (oss_functions_t *) this_gen;
  
  if (this->audio_fd != -1)
    close(this->audio_fd);

  free (this->zero_space);
  free (this);
}

ao_functions_t *init_audio_out_plugin (config_values_t *config) {

  oss_functions_t *this;
  int              caps;
#ifdef CONFIG_DEVFS_FS
  char             devname[] = "/dev/sound/dsp\0\0\0";
#else
  char             devname[] = "/dev/dsp\0\0\0";
#endif
  int              best_rate;
  int              rate ;
  int              devnum;
  int              audio_fd;

  this = (oss_functions_t *) malloc (sizeof (oss_functions_t));

  /*
   * find best device driver/channel
   */

  xprintf (VERBOSE|AUDIO, "audio_oss_out: Opening audio device...");
  devnum = 0;
  best_rate = 0;
  while (devnum<16) {
    audio_fd=open(devname,O_WRONLY|O_NDELAY);

    if (audio_fd>0) {

      /* test bitrate capability */
      
      rate = 48000;
      ioctl(audio_fd,SNDCTL_DSP_SPEED, &rate);
      if (rate>best_rate) {
	strncpy (this->audio_dev, devname, 19);
	best_rate = rate;
      }
      
      close (audio_fd);
    }

    sprintf(devname, DSP_TEMPLATE, devnum);
    devnum++;
  }

  /*
   * open that device
   */

  audio_fd=open(this->audio_dev, O_WRONLY|O_NDELAY);

  if(audio_fd < 0) 
  {
    xprintf(VERBOSE|AUDIO, "audio_oss_out: %s: Opening audio device %s\n",
	   strerror(errno), this->audio_dev);

    free (this);
    return NULL;

  } else
    xprintf (VERBOSE|AUDIO, " %s\n", this->audio_dev);

  ioctl (audio_fd, SNDCTL_DSP_GETCAPS, &caps);

  if ((caps & DSP_CAP_REALTIME) > 0) {
    xprintf (VERBOSE|AUDIO, "audio_oss_out : realtime check: passed :-)\n");
  } else {
    xprintf (VERBOSE|AUDIO, "audio_oss_out : realtime check: *FAILED* :-(((((\n\n");
  }

  /*
  if ((caps & DSP_CAP_TRIGGER) > 0) {
    xprintf (VERBOSE|AUDIO, "audio_out   : trigger check : passed :-)\n");
  } else {
    xprintf (VERBOSE|AUDIO, "audio_out   : trigger check : *FAILED* :-(((((\n");
  }
  */

  close (audio_fd);

  this->output_sample_rate = 0;

  this->zero_space = malloc (8192);
  memset (this->zero_space, 0, 8192);

  this->ao_functions.get_supported_modes = ao_get_supported_modes;
  this->ao_functions.connect             = ao_connect;
  this->ao_functions.open                = ao_open;
  this->ao_functions.write_audio_data    = ao_write_audio_data;
  this->ao_functions.close               = ao_close;
  this->ao_functions.exit                = ao_exit;

  return &this->ao_functions;
}

static ao_info_t ao_info_oss = {
  AUDIO_OUT_IFACE_VERSION,
  "oss",
  "xine audio output plugin using oss-compliant audio devices/drivers",
  5
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_oss;
}

