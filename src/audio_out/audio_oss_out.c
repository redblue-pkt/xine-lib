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
 * $Id: audio_oss_out.c,v 1.17 2001/06/24 03:36:30 guenter Exp $
 */

/* required for swab() */
#define _XOPEN_SOURCE 500
/* required for FNDELAY decl */
#define _BSD_SOURCE 1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
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

#ifndef AFMT_S16_NE
# if defined(sparc) || defined(__sparc__) || defined(PPC)
/* Big endian machines */
#  define AFMT_S16_NE AFMT_S16_BE
# else
#  define AFMT_S16_NE AFMT_S16_LE
# endif
#endif

#ifndef AFMT_AC3
#       define AFMT_AC3         0x00000400      /* Dolby Digital AC3 */
#endif

#define AO_OUT_OSS_IFACE_VERSION 1

#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

#define GAP_TOLERANCE         5000
#define MAX_GAP              90000

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
  int            capabilities;
  int            mode;

  int32_t        output_sample_rate, input_sample_rate;
  double         sample_rate_factor;
  uint32_t       num_channels;

  uint32_t       bytes_in_buffer;      /* number of bytes writen to audio hardware   */

  int            audio_step;           /* pts per 32 768 samples (sample = #bytes/2) */
  int32_t        bytes_per_kpts;       /* bytes per 1024/90000 sec                   */

  int16_t       *zero_space;
  
  int            audio_started;
  uint32_t       last_audio_vpts;
} oss_functions_t;

/*
 * open the audio device for writing to
 */
static int ao_open(ao_functions_t *this_gen,
		   uint32_t bits, uint32_t rate, int mode)
{
  oss_functions_t *this = (oss_functions_t *) this_gen;
  int tmp;

  printf ("audio_oss_out: ao_open rate=%d, mode=%d\n", rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    printf ("audio_oss_out: unsupported mode %08x\n", mode);
    return -1;
  }

  if (this->audio_fd > -1) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) ) {
      return 1;
    }

    close (this->audio_fd);
  }
  
  this->mode                   = mode;
  this->input_sample_rate      = rate;
  this->bytes_in_buffer        = 0;
  this->audio_started          = 0;
  this->last_audio_vpts        = 0;

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

  tmp = (mode & AO_CAP_MODE_STEREO) ? 1 : 0;
  ioctl(this->audio_fd,SNDCTL_DSP_STEREO,&tmp);


  tmp = bits;
  ioctl(this->audio_fd,SNDCTL_DSP_SAMPLESIZE,&tmp);

  tmp = this->input_sample_rate;
  ioctl(this->audio_fd,SNDCTL_DSP_SPEED, &tmp);
  this->output_sample_rate = tmp;

  xprintf (VERBOSE|AUDIO, "audio_oss_out: audio rate : %d requested, %d provided by device/sec\n",
	   this->input_sample_rate, this->output_sample_rate);

  /*
   * set number of channels / ac3 throughput
   */

  switch (mode) {
  case AO_CAP_MODE_MONO:
    tmp = 1;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_STEREO:
    tmp = 2;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_4CHANNEL:
    tmp = 4;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_5CHANNEL:
    tmp = 5;
    ioctl(this->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    this->num_channels = tmp;
    break;
  case AO_CAP_MODE_AC3:
    tmp = AFMT_AC3;
    ioctl(this->audio_fd,SNDCTL_DSP_SETFMT,&tmp);
    this->num_channels = 2; /* FIXME: is this correct ? */
    printf ("audio_oss_out : AO_CAP_MODE_AC3\n");
    break;
  }

  printf ("audio_oss_out : %d channels output\n",this->num_channels);

  this->sample_rate_factor = (double) this->output_sample_rate / (double) this->input_sample_rate;
  this->audio_step         = (uint32_t) 90000 * (uint32_t) 32768 
                                 / this->input_sample_rate;
  this->bytes_per_kpts     = this->output_sample_rate * this->num_channels * 2 * 1024 / 90000;

  xprintf (VERBOSE|AUDIO, "audio_out : audio_step %d pts per 32768 samples\n", this->audio_step);
  printf ("audio_out : audio_step %d pts per 32768 samples\n", this->audio_step);

  this->metronom->set_audio_rate(this->metronom, this->audio_step); 
  
  /*
   * audio buffer size handling
   */

  /* WARNING: let's hope for good defaults here...
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
  */

  return 1;
}

static void ao_fill_gap (oss_functions_t *this, uint32_t pts_len) {

  int num_bytes ;

  if (pts_len > MAX_GAP)
    pts_len = MAX_GAP;
  num_bytes = pts_len * this->bytes_per_kpts / 1024;
  num_bytes = (num_bytes / (2*this->num_channels)) * (2*this->num_channels);

  if(this->mode == AO_CAP_MODE_AC3) return; /* FIXME */

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
}

static int ao_write_audio_data(ao_functions_t *this_gen,
			       int16_t* output_samples, uint32_t num_samples, 
			       uint32_t pts_)
{

  oss_functions_t *this = (oss_functions_t *) this_gen;
  uint32_t         vpts, buffer_vpts;
  int32_t          gap;
  int              bDropPackage;
  uint16_t         sample_buffer[10000];
  count_info       info;
  int              pos;

  if (this->audio_fd<0)
    return;

  vpts = this->metronom->got_audio_samples (this->metronom, pts_, num_samples);

  xprintf (VERBOSE|AUDIO, "audio_oss_out: got %d samples, vpts=%d\n",
	   num_samples, vpts);

  if (vpts<this->last_audio_vpts) {
    /* reject this */

    return 1;
  }

  this->last_audio_vpts = vpts;

  /*
   * where, in the timeline is the "end" of the audio buffer at the moment?
   */

  buffer_vpts = this->metronom->get_current_time (this->metronom);

  if (this->audio_started) {
    ioctl (this->audio_fd, SNDCTL_DSP_GETOPTR, &info);
    pos = info.bytes;
  } else
    pos = 0;

  if (pos>this->bytes_in_buffer) /* buffer ran dry */ 
    this->bytes_in_buffer = pos;

  buffer_vpts += (this->bytes_in_buffer - pos) * 1024 / this->bytes_per_kpts;

  /*
  printf ("audio_oss_out: got audio package vpts = %d, buffer_vpts = %d\n",
	  vpts, buffer_vpts);
  */

  /*
   * calculate gap:
   */

  gap = vpts - buffer_vpts;

  bDropPackage = 0;
  
  if (gap>GAP_TOLERANCE) {
    ao_fill_gap (this, gap);

    /* keep xine responsive */

    if (gap>MAX_GAP)
      return 0;

  } else if (gap<-GAP_TOLERANCE) {
    bDropPackage = 1;
  }

  /*
   * resample and output samples
   */
  if(this->mode == AO_CAP_MODE_AC3) bDropPackage=0;

  if (!bDropPackage) {
    int num_output_samples = num_samples * (this->output_sample_rate) / this->input_sample_rate;

    switch (this->mode) {
    case AO_CAP_MODE_MONO:
      audio_out_resample_mono (output_samples, num_samples,
			       sample_buffer, num_output_samples);
      write(this->audio_fd, sample_buffer, num_output_samples * 2);
      break;
    case AO_CAP_MODE_STEREO:
      audio_out_resample_stereo (output_samples, num_samples,
				 sample_buffer, num_output_samples);
      write(this->audio_fd, sample_buffer, num_output_samples * 4);
      break;
    case AO_CAP_MODE_4CHANNEL:
      audio_out_resample_4channel (output_samples, num_samples,
				   sample_buffer, num_output_samples);
      write(this->audio_fd, sample_buffer, num_output_samples * 8);
      break;
    case AO_CAP_MODE_5CHANNEL:
      audio_out_resample_5channel (output_samples, num_samples,
				   sample_buffer, num_output_samples);
      write(this->audio_fd, sample_buffer, num_output_samples * 10);
      break;
    case AO_CAP_MODE_AC3:
      num_output_samples = num_samples+8;
      sample_buffer[0] = 0xf872;  //spdif syncword
      sample_buffer[1] = 0x4e1f;  // .............
      sample_buffer[2] = 0x0001;  // AC3 data
      sample_buffer[3] = num_samples * 8;
//      sample_buffer[4] = 0x0b77;  // AC3 syncwork already in output_samples

      // ac3 seems to be swabbed data
      swab(output_samples,sample_buffer+4,  num_samples  );
      write(this->audio_fd, sample_buffer, num_output_samples);
      write(this->audio_fd, this->zero_space, 6144-num_output_samples);
      num_output_samples=num_output_samples/4;
      break;
    }

    xprintf (AUDIO|VERBOSE, "audio_oss_out :audio package written\n");
    
    /*
     * step values
     */
    
    this->bytes_in_buffer += num_output_samples * 2 * this->num_channels;
    this->audio_started    = 1;
  } else {
    printf ("audio_oss_out: audio package (vpts = %d) dropped\n", vpts);
  }

  return 1;

}


static void ao_close(ao_functions_t *this_gen)
{
  oss_functions_t *this = (oss_functions_t *) this_gen;
  close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_get_capabilities (ao_functions_t *this_gen) {
  oss_functions_t *this = (oss_functions_t *) this_gen;
  return this->capabilities;
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
  int              num_channels, status, arg;

  this = (oss_functions_t *) malloc (sizeof (oss_functions_t));

  /*
   * find best device driver/channel
   */

  printf ("audio_oss_out: Opening audio device...\n");
  xprintf (VERBOSE|AUDIO, "audio_oss_out: Opening audio device...");
  devnum = 0;
  best_rate = 0;
  sprintf (this->audio_dev, "/dev/dsp");
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
    } /*else
      printf("audio_oss_out: opening audio device %s failed:\n%s\n",
	     this->audio_dev, strerror(errno));
	     */

    sprintf(devname, DSP_TEMPLATE, devnum);
    devnum++;
  }

  /*
   * open that device
   */

  audio_fd=open(this->audio_dev, O_WRONLY|O_NDELAY);

  if(audio_fd < 0) 
  {
    printf("audio_oss_out: opening audio device %s failed:\n%s\n",
	   this->audio_dev, strerror(errno));

    free (this);
    return NULL;

  } else
    xprintf (VERBOSE|AUDIO, " %s\n", this->audio_dev);

  /*
   * set up driver to reasonable values for capabilities tests
   */

  arg = AFMT_S16_NE; 
  status = ioctl(audio_fd, SOUND_PCM_SETFMT, &arg);
  arg = 44100;
  status = ioctl(audio_fd, SOUND_PCM_WRITE_RATE, &arg);

  /*
   * get capabilities
   */

  ioctl (audio_fd, SNDCTL_DSP_GETCAPS, &caps);

  if ((caps & DSP_CAP_REALTIME) > 0) {
    xprintf (VERBOSE|AUDIO, "audio_oss_out : realtime check: passed :-)\n");
  } else {
    xprintf (VERBOSE|AUDIO, "audio_oss_out : realtime check: *FAILED* :-(((((\n\n");
  }

  this->capabilities = 0;

  printf ("audio_oss_out : supported modes are ");
  num_channels = 1; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==1) ) {
    this->capabilities |= AO_CAP_MODE_MONO;
    printf ("mono ");
  }
  num_channels = 2; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==2) ) {
    this->capabilities |= AO_CAP_MODE_STEREO;
    printf ("stereo ");
  }
  num_channels = 4; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==4) ) {
    if (config->lookup_int (config, "four_channel", 0)) {
      this->capabilities |= AO_CAP_MODE_4CHANNEL;
      printf ("4-channel ");
    } else
      printf ("(4-channel not enabled in .xinerc) " );
  }
  num_channels = 5; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==5) ) {
    if (config->lookup_int (config, "five_channel", 0)) {
      this->capabilities |= AO_CAP_MODE_5CHANNEL;
      printf ("5-channel ");
    } else
      printf ("(5-channel not enabled in .xinerc) " );
  }

  ioctl(audio_fd,SNDCTL_DSP_GETFMTS,&caps);
  if (caps & AFMT_AC3) {
    if (config->lookup_int (config, "ac3_pass_through", 0)) {
      this->capabilities |= AO_CAP_MODE_AC3;
      printf ("ac3-pass-through ");
    } else 
      printf ("(ac3-pass-through not enabled in .xinerc)");
  }    

  printf ("\n");

  close (audio_fd);

  this->output_sample_rate = 0;
  this->audio_fd = -1;

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

static ao_info_t ao_info_oss = {
  AUDIO_OUT_IFACE_VERSION,
  "oss",
  "xine audio output plugin using oss-compliant audio devices/drivers",
  5
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_oss;
}

