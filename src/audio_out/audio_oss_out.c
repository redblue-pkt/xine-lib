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
 * $Id: audio_oss_out.c,v 1.31 2001/08/22 10:51:05 jcdutton Exp $
 *
 * 20-8-2001 First implementation of Audio sync and Audio driver separation.
 * Copyright (C) 2001 James Courtier-Dutton James@superbug.demon.co.uk
 * 
 * General Programming Guidelines: -
 * New concept of an "audio_frame".
 * An audio_frame consists of all the samples required to fill every audio channel to a full amount of bits.
 * So, it does not mater how many bits per sample, or how many audio channels are being used, the number of audio_frames is the same.
 * E.g.  16 bit stereo is 4 bytes, but one frame.
 *       16 bit 5.1 surround is 12 bytes, but one frame.
 * The purpose of this is to make the audio_sync code a lot more readable, rather than having to multiply by the amount of channels all the time
 * when dealing with audio_bytes instead of audio_frames.
 *
 * The number of samples passed to/from the audio driver is also sent in units of audio_frames.
 *              `
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

#define AO_OUT_OSS_IFACE_VERSION 2

#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

/* bufsize must be a multiple of 3 and 5 for 5.0 and 5.1 channel playback! */
#define ZERO_BUF_SIZE        15360

#define GAP_TOLERANCE         5000
#define MAX_GAP              90000

#ifdef CONFIG_DEVFS_FS
#define DSP_TEMPLATE "/dev/sound/dsp%d"
#else
#define DSP_TEMPLATE "/dev/dsp%d"
#endif

static int checked_getoptr = 0;

typedef struct oss_driver_s {

  ao_driver_t ao_driver;
  char           audio_dev[20];
  int            audio_fd;
  int            capabilities;
  int            mode;

  int32_t        output_sample_rate, input_sample_rate;
  double         sample_rate_factor;
  uint32_t       num_channels;
  uint32_t	 bits_per_sample;
  uint32_t	 bytes_per_frame;
  uint32_t       bytes_in_buffer;      /* number of bytes writen to audio hardware   */
  
  int            audio_started;
  int            audio_has_realtime;   /* OSS driver supports real-time              */
} oss_driver_t;

/*
 * open the audio device for writing to
 */
static int ao_oss_open(ao_driver_t *self_gen,
		   uint32_t bits, uint32_t rate, int mode)
{
  oss_driver_t *self = (oss_driver_t *) self_gen;
  int tmp;

  printf ("audio_oss_out: ao_open rate=%d, mode=%d\n", rate, mode);

  if ( (mode & self->capabilities) == 0 ) {
    printf ("audio_oss_out: unsupported mode %08x\n", mode);
    return -1;
  }

  if (self->audio_fd > -1) {

    if ( (mode == self->mode) && (rate == self->input_sample_rate) ) {
      return 1;
    }

    close (self->audio_fd);
  }
  
  self->mode                   = mode;
  self->input_sample_rate      = rate;
  self->bits_per_sample        = bits;
  self->bytes_in_buffer        = 0;
  self->audio_started          = 0;

  /*
   * open audio device
   */

  self->audio_fd=open(self->audio_dev,O_WRONLY|O_NDELAY);
  if(self->audio_fd < 0) {
    printf("audio_oss_out: Opening audio device %s: %s\n",
	   self->audio_dev, strerror(errno));
    return -1;
  }
  
  /* We wanted non blocking open but now put it back to normal */
  fcntl(self->audio_fd, F_SETFL, fcntl(self->audio_fd, F_GETFL)&~FNDELAY);

  /*
   * configure audio device
   * In AC3 mode, skip all other SNDCTL commands
   */
  if(!(mode & AO_CAP_MODE_AC3)) {
    tmp = (mode & AO_CAP_MODE_STEREO) ? 1 : 0;
    ioctl(self->audio_fd,SNDCTL_DSP_STEREO,&tmp);

    tmp = bits;
    ioctl(self->audio_fd,SNDCTL_DSP_SAMPLESIZE,&tmp);

    tmp = self->input_sample_rate;
#ifdef FORCE_44K_MAX
    if(tmp > 44100)
       tmp = 44100;
#endif
    if (ioctl(self->audio_fd,SNDCTL_DSP_SPEED, &tmp) == -1) {

      printf ("audio_oss_out: warning: sampling rate %d Hz not supported, trying 44100 Hz\n", self->input_sample_rate);

      tmp = 44100;
      if (ioctl(self->audio_fd,SNDCTL_DSP_SPEED, &tmp) == -1) {
        printf ("audio_oss_out: error: 44100 Hz sampling rate not supported\n");
        return -1;
      }
    }
    self->output_sample_rate = tmp;
    xprintf (VERBOSE|AUDIO, "audio_oss_out: audio rate : %d requested, %d provided by device/sec\n",
	   self->input_sample_rate, self->output_sample_rate);
  }
  /*
   * set number of channels / ac3 throughput
   */

  switch (mode) {
  case AO_CAP_MODE_MONO:
    tmp = 1;
    ioctl(self->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    self->num_channels = tmp;
    break;
  case AO_CAP_MODE_STEREO:
    tmp = 2;
    ioctl(self->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    self->num_channels = tmp;
    break;
  case AO_CAP_MODE_4CHANNEL:
    tmp = 4;
    ioctl(self->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    self->num_channels = tmp;
    break;
  case AO_CAP_MODE_5CHANNEL:
    tmp = 5;
    ioctl(self->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    self->num_channels = tmp;
    break;
  case AO_CAP_MODE_5_1CHANNEL:
    tmp = 6;
    ioctl(self->audio_fd, SNDCTL_DSP_CHANNELS, &tmp);
    self->num_channels = tmp;
    break;
  case AO_CAP_MODE_AC3:
    tmp = AFMT_AC3;
    if (ioctl(self->audio_fd, SNDCTL_DSP_SETFMT, &tmp) < 0 || tmp != AFMT_AC3) {
        printf("audio_oss_out: AC3 SNDCTL_DSP_SETFMT failed. %d\n",tmp);
	return -1;
    }
    self->num_channels = 2; /* FIXME: is this correct ? */
    self->output_sample_rate = self->input_sample_rate;
    printf ("audio_oss_out : AO_CAP_MODE_AC3\n");
    break;
  }

  printf ("audio_oss_out : %d channels output\n",self->num_channels);
  self->bytes_per_frame=(self->bits_per_sample*self->num_channels)/8;
  
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

  ioctl(self->audio_fd,SNDCTL_DSP_SETFRAGMENT,&tmp); 
  */

  /*
   * Final check of realtime capability, make sure GETOPTR
   * doesn't return an error.
   */
  if ( self->audio_has_realtime && !checked_getoptr ) {
    count_info info;
    int ret = ioctl(self->audio_fd, SNDCTL_DSP_GETOPTR, &info);
    if ( ret == -1 && errno == EINVAL ) {
      self->audio_has_realtime = 0;
      printf("audio_oss_out: Audio driver SNDCTL_DSP_GETOPTR reports %s,"
		" disabling realtime sync...\n", strerror(errno) );
      printf("audio_oss_out: ...Will use video master clock for soft-sync instead\n");
      printf("audio_oss_out: ...There may be audio/video synchronization issues\n");
    }
    checked_getoptr = 1;
  }

  return 1;
}

static int ao_oss_num_channels(ao_driver_t *self_gen) 
{
  oss_driver_t *self = (oss_driver_t *) self_gen;
  return self->num_channels;
}

static int ao_oss_bytes_per_frame(ao_driver_t *self_gen)
{
  oss_driver_t *self = (oss_driver_t *) self_gen;
  return self->bytes_per_frame;
}

static int ao_oss_delay(ao_driver_t *self_gen)
{
  count_info info;
  oss_driver_t *self = (oss_driver_t *) self_gen;
  ioctl (self->audio_fd, SNDCTL_DSP_GETOPTR, &info);
  return info.bytes / self->bytes_per_frame;
}

 /* Write audio samples
  * num_frames is the number of audio frames present
  * audio frames are equivalent one sample on each channel.
  * I.E. Stereo 16 bits audio frames are 4 bytes.
  */
static int ao_oss_write(ao_driver_t *self_gen,
                               int16_t* frame_buffer, uint32_t num_frames)
{
  oss_driver_t *self = (oss_driver_t *) self_gen;
      return write(self->audio_fd, frame_buffer, num_frames * self->bytes_per_frame); 

}

static void ao_oss_close(ao_driver_t *self_gen)
{
  oss_driver_t *self = (oss_driver_t *) self_gen;
  close(self->audio_fd);
  self->audio_fd = -1;
}

static uint32_t ao_oss_get_capabilities (ao_driver_t *self_gen) {
  oss_driver_t *self = (oss_driver_t *) self_gen;
  return self->capabilities;
}

static void ao_oss_exit(ao_driver_t *self_gen)
{
  oss_driver_t *self = (oss_driver_t *) self_gen;
  
  if (self->audio_fd != -1)
    close(self->audio_fd);

  free (self);
}

/*
 *
 */
static int ao_oss_get_property (ao_driver_t *self_gen, int property) {
  oss_driver_t *self = (oss_driver_t *) self;

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
static int ao_oss_set_property (ao_driver_t *self_gen, int property, int value) {
  oss_driver_t *self = (oss_driver_t *) self;

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

  oss_driver_t *self;
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

  self = (oss_driver_t *) malloc (sizeof (oss_driver_t));

  /*
   * find best device driver/channel
   */

  printf ("audio_oss_out: Opening audio device...\n");
  xprintf (VERBOSE|AUDIO, "audio_oss_out: Opening audio device...");

  best_rate = 0;
  devnum = config->lookup_int (config, "oss_device_num", -1);

  if (devnum >= 0) {
    sprintf (self->audio_dev, DSP_TEMPLATE, devnum);
    devnum = 30; /* skip while loop */
  } else {
    devnum = 0;
    sprintf (self->audio_dev, "/dev/dsp");
  }

  while (devnum<16) {

    audio_fd=open(devname,O_WRONLY|O_NDELAY);

    if (audio_fd>0) {

      /* test bitrate capability */
      
      rate = 48000;
      ioctl(audio_fd,SNDCTL_DSP_SPEED, &rate);
      if (rate>best_rate) {
	strncpy (self->audio_dev, devname, 19);
	best_rate = rate;
      }
      
      close (audio_fd);
    } /*else
      printf("audio_oss_out: opening audio device %s failed:\n%s\n",
	     self->audio_dev, strerror(errno));
	     */

    sprintf(devname, DSP_TEMPLATE, devnum);
    devnum++;
  }

  /*
   * open that device
   */

  audio_fd=open(self->audio_dev, O_WRONLY|O_NDELAY);

  if(audio_fd < 0) 
  {
    printf("audio_oss_out: opening audio device %s failed:\n%s\n",
	   self->audio_dev, strerror(errno));

    free (self);
    return NULL;

  } else
    xprintf (VERBOSE|AUDIO, " %s\n", self->audio_dev);

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
    self->audio_has_realtime = 1;
  } else {
    printf ("audio_oss_out : realtime check: *FAILED* :-(((((\n");
    self->audio_has_realtime = 0;
  }

  if( !self->audio_has_realtime ) {
    printf("audio_oss_out: Audio driver realtime sync disabled...\n");
    printf("audio_oss_out: ...Will use video master clock for soft-sync instead\n");
    printf("audio_oss_out: ...There may be audio/video synchronization issues\n");
  }

  self->capabilities = 0;

  printf ("audio_oss_out : supported modes are ");
  num_channels = 1; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==1) ) {
    self->capabilities |= AO_CAP_MODE_MONO;
    printf ("mono ");
  }
  num_channels = 2; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==2) ) {
    self->capabilities |= AO_CAP_MODE_STEREO;
    printf ("stereo ");
  }
  num_channels = 4; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==4) ) {
    if (config->lookup_int (config, "four_channel", 0)) {
      self->capabilities |= AO_CAP_MODE_4CHANNEL;
      printf ("4-channel ");
    } else
      printf ("(4-channel not enabled in .xinerc) " );
  }
  num_channels = 5; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==5) ) {
    if (config->lookup_int (config, "five_channel", 0)) {
      self->capabilities |= AO_CAP_MODE_5CHANNEL;
      printf ("5-channel ");
    } else
      printf ("(5-channel not enabled in .xinerc) " );
  }
  num_channels = 6; 
  status = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &num_channels); 
  if ( (status != -1) && (num_channels==6) ) {
    if (config->lookup_int (config, "five_lfe_channel", 0)) {
      self->capabilities |= AO_CAP_MODE_5_1CHANNEL;
      printf ("5.1-channel ");
    } else
      printf ("(5.1-channel not enabled in .xinerc) " );
  }

  ioctl(audio_fd,SNDCTL_DSP_GETFMTS,&caps);
  if (caps & AFMT_AC3) {
    if (config->lookup_int (config, "ac3_pass_through", 0)) {
      self->capabilities |= AO_CAP_MODE_AC3;
      printf ("ac3-pass-through ");
    } else 
      printf ("(ac3-pass-through not enabled in .xinerc)");
  }    

  printf ("\n");

  close (audio_fd);

  self->output_sample_rate = 0;
  self->audio_fd = -1;

  self->ao_driver.get_capabilities    = ao_oss_get_capabilities;
  self->ao_driver.get_property        = ao_oss_get_property;
  self->ao_driver.set_property        = ao_oss_set_property;
  self->ao_driver.open                = ao_oss_open;
  self->ao_driver.num_channels        = ao_oss_num_channels;
  self->ao_driver.bytes_per_frame     = ao_oss_bytes_per_frame;
  self->ao_driver.delay               = ao_oss_delay;
  self->ao_driver.write		      = ao_oss_write;
  self->ao_driver.close               = ao_oss_close;
  self->ao_driver.exit                = ao_oss_exit;

  return &self->ao_driver;
}

static ao_info_t ao_info_oss = {
  AUDIO_OUT_IFACE_VERSION,
  "oss",
  "xine audio output plugin using oss-compliant audio devices/drivers",
  10
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_oss;
}

