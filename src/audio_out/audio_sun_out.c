/* 
 * Copyright (C) 2001 the xine project
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
 * $Id: audio_sun_out.c,v 1.6 2001/08/23 15:21:07 jkeil Exp $
 */

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
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#ifdef	__svr4__
#include <stropts.h>
#endif

#include "xine_internal.h"
#include "monitor.h"
#include "audio_out.h"
#include "utils.h"


#ifndef	AUDIO_CHANNELS_MONO
#define	AUDIO_CHANNELS_MONO	1
#define	AUDIO_CHANNELS_STEREO	2
#endif
#ifndef	AUDIO_PRECISION_8
#define	AUDIO_PRECISION_8	8
#define	AUDIO_PRECISION_16	16
#endif

#define GAP_TOLERANCE         5000
#define MAX_GAP              90000


typedef struct sun_driver_s {

  ao_driver_t	 ao_driver;

  char		*audio_dev;
  int            audio_fd;
  int            capabilities;
  int            mode;

  int32_t        output_sample_rate, input_sample_rate;
  double         sample_rate_factor;
  uint32_t       num_channels;
  int		 bytes_per_frame;

  uint32_t       frames_in_buffer;     /* number of frames writen to audio hardware   */

  enum {
      RTSC_UNKNOWN = 0,
      RTSC_ENABLED,
      RTSC_DISABLED
  }		 use_rtsc;
} sun_driver_t;


/*
 * try to figure out, if the soundcard driver provides usable (precise)
 * sample counter information
 */
static int realtime_samplecounter_available(char *dev)
{
  int fd = -1;
  audio_info_t info;
  int rtsc_ok = RTSC_DISABLED;
  int len;
  void *silence = NULL;
  struct timeval start, end;
  struct timespec delay;
  int usec_delay;
  unsigned last_samplecnt;
  unsigned increment;
  unsigned min_increment;

  len = 44100 * 4 / 4;    /* amount of data for 0.25sec of 44.1khz, stereo,
			   * 16bit.  44kbyte can be sent to all supported
			   * sun audio devices without blocking in the
			   * "write" below.
			   */
  silence = calloc(1, len);
  if (silence == NULL)
    goto error;
    
  if ((fd = open(dev, O_WRONLY)) < 0)
    goto error;

  AUDIO_INITINFO(&info);
  info.play.sample_rate = 44100;
  info.play.channels = AUDIO_CHANNELS_STEREO;
  info.play.precision = AUDIO_PRECISION_16;
  info.play.encoding = AUDIO_ENCODING_LINEAR;
  info.play.samples = 0;
  if (ioctl(fd, AUDIO_SETINFO, &info)) {
    xprintf(VERBOSE|AUDIO, "rtsc: SETINFO failed\n");
    goto error;
  }
    
  if (write(fd, silence, len) != len) {
    xprintf(VERBOSE|AUDIO, "rtsc: write failed\n");
    goto error;
  }

  if (ioctl(fd, AUDIO_GETINFO, &info)) {
    xprintf(VERBOSE|AUDIO, "rtsc: GETINFO1, %s\n", strerror(errno));
    goto error;
  }

  last_samplecnt = info.play.samples;
  min_increment = ~0;

  gettimeofday(&start, NULL);
  for (;;) {
    delay.tv_sec = 0;
    delay.tv_nsec = 10000000;
    nanosleep(&delay, NULL);
    gettimeofday(&end, NULL);
    usec_delay = (end.tv_sec - start.tv_sec) * 1000000
	+ end.tv_usec - start.tv_usec;

    /* stop monitoring sample counter after 0.2 seconds */
    if (usec_delay > 200000)
      break;

    if (ioctl(fd, AUDIO_GETINFO, &info)) {
	xprintf(VERBOSE|AUDIO, "rtsc: GETINFO2 failed, %s\n", strerror(errno));
	goto error;
    }
    if (info.play.samples < last_samplecnt) {
	xprintf(VERBOSE|AUDIO, "rtsc: %d > %d?\n", last_samplecnt, info.play.samples);
	goto error;
    }

    if ((increment = info.play.samples - last_samplecnt) > 0) {
	xprintf(VERBOSE|AUDIO, "ao_sun: sample counter increment: %d\n", increment);
	if (increment < min_increment) {
	  min_increment = increment;
	  if (min_increment < 2000)
	    break;	/* looks good */
	}
    }
    last_samplecnt = info.play.samples;
  }

  /*
   * For 44.1kkz, stereo, 16-bit format we would send sound data in 16kbytes
   * chunks (== 4096 samples) to the audio device.  If we see a minimum
   * sample counter increment from the soundcard driver of less than
   * 2000 samples,  we assume that the driver provides a useable realtime
   * sample counter in the AUDIO_INFO play.samples field.  Timing based
   * on sample counts should be much more accurate than counting whole 
   * 16kbyte chunks.
   */
  if (min_increment < 2000)
    rtsc_ok = RTSC_ENABLED;

  xprintf(VERBOSE|AUDIO,
	  "ao_sun: minimum sample counter increment per 10msec interval: %d\n"
	  "\t%susing sample counter based timing code\n",
	  min_increment, rtsc_ok == RTSC_ENABLED ? "" : "not ");
    

error:
  if (silence != NULL) free(silence);
  if (fd >= 0) {
#ifdef	__svr4__
    /*
     * remove the 0 bytes from the above measurement from the
     * audio driver's STREAMS queue
     */
    ioctl(fd, I_FLUSH, FLUSHW);
#endif
    close(fd);
  }

  return rtsc_ok;
}


/*
 * open the audio device for writing to
 */
static int ao_sun_open(ao_driver_t *self_gen,
		       uint32_t bits, uint32_t rate, int mode)
{
  sun_driver_t *self = (sun_driver_t *) self_gen;
  audio_info_t info;

  printf ("audio_sun_out: ao_sun_open rate=%d, mode=%d\n", rate, mode);

  if ( (mode & self->capabilities) == 0 ) {
    printf ("audio_sun_out: unsupported mode %08x\n", mode);
    return -1;
  }

  if (self->audio_fd > -1) {

    if ( (mode == self->mode) && (rate == self->input_sample_rate) )
      return 1;

    close (self->audio_fd);
  }
  
  self->mode			= mode;
  self->input_sample_rate	= rate;
  self->frames_in_buffer	= 0;

  /*
   * open audio device
   */

  self->audio_fd=open(self->audio_dev,O_WRONLY|O_NDELAY);
  if(self->audio_fd < 0) {
    printf("audio_sun_out: Opening audio device %s: %s\n",
	   self->audio_dev, strerror(errno));
    return -1;
  }
  
  /* We wanted non blocking open but now put it back to normal */
  fcntl(self->audio_fd, F_SETFL, fcntl(self->audio_fd, F_GETFL)&~O_NDELAY);

  /*
   * configure audio device
   */

  AUDIO_INITINFO(&info);
  info.play.channels = (mode & AO_CAP_MODE_STEREO)
      ? AUDIO_CHANNELS_STEREO
      : AUDIO_CHANNELS_MONO;
  info.play.precision = bits;
  info.play.encoding = AUDIO_ENCODING_LINEAR;
  info.play.sample_rate = self->input_sample_rate;
  info.play.eof = 0;
  info.play.samples = 0;

  ioctl(self->audio_fd, AUDIO_SETINFO, &info);

  self->output_sample_rate = info.play.sample_rate;
  self->num_channels = info.play.channels;

  self->bytes_per_frame = 1;
  if (info.play.channels == AUDIO_CHANNELS_STEREO)
    self->bytes_per_frame *= 2;
  if (info.play.precision == 16)
    self->bytes_per_frame *= 2;

  xprintf (VERBOSE|AUDIO, "audio_sun_out: audio rate : %d requested, %d provided by device/sec\n",
	   self->input_sample_rate, self->output_sample_rate);

  printf ("audio_sun_out : %d channels output\n",self->num_channels);

  return 1;
}

static int ao_sun_num_channels(ao_driver_t *self_gen) 
{
  sun_driver_t *self = (sun_driver_t *) self_gen;
  return self->num_channels;
}

static int ao_sun_bytes_per_frame(ao_driver_t *self_gen)
{
  sun_driver_t *self = (sun_driver_t *) self_gen;
  return self->bytes_per_frame;
}

static int ao_sun_delay(ao_driver_t *self_gen)
{
  sun_driver_t *self = (sun_driver_t *) self_gen;
  audio_info_t info;

  if (ioctl(self->audio_fd, AUDIO_GETINFO, &info) == 0
      && info.play.samples
      && self->use_rtsc == RTSC_ENABLED) {
    return self->frames_in_buffer - info.play.samples;
  }
  return 0;
}

 /* Write audio samples
  * num_frames is the number of audio frames present
  * audio frames are equivalent one sample on each channel.
  * I.E. Stereo 16 bits audio frames are 4 bytes.
  */
static int ao_sun_write(ao_driver_t *self_gen,
                               int16_t* frame_buffer, uint32_t num_frames)
{
  sun_driver_t *self = (sun_driver_t *) self_gen;
  int num_written;
  num_written = write(self->audio_fd, frame_buffer, num_frames * self->bytes_per_frame);
  if (num_written > 0) {
    self->frames_in_buffer += num_written / self->bytes_per_frame;
  }
  return num_written;
}

static void ao_sun_close(ao_driver_t *self_gen)
{
  sun_driver_t *self = (sun_driver_t *) self_gen;
  close(self->audio_fd);
  self->audio_fd = -1;
}

static uint32_t ao_sun_get_capabilities (ao_driver_t *self_gen) {
  sun_driver_t *self = (sun_driver_t *) self_gen;
  return self->capabilities;
}

static void ao_sun_exit(ao_driver_t *self_gen)
{
  sun_driver_t *self = (sun_driver_t *) self_gen;
  
  if (self->audio_fd != -1)
    close(self->audio_fd);

  free (self);
}

/*
 * Get a property of audio driver.
 * return 1 in success, 0 on failure. (and the property value?)
 */
static int ao_sun_get_property (ao_driver_t *self_gen, int property) {
  sun_driver_t *self = (sun_driver_t *) self_gen;
  audio_info_t	info;

  switch(property) {
  case AO_PROP_MIXER_VOL:
    break;
  case AO_PROP_PCM_VOL:
    if (ioctl(self->audio_fd, AUDIO_GETINFO, &info) < 0)
      return 0;
    return info.play.gain;
#if !defined(__NetBSD__)    /* audio_info.output_muted is missing on NetBSD */
  case AO_PROP_MUTE_VOL:
    if (ioctl(self->audio_fd, AUDIO_GETINFO, &info) < 0)
      return 0;
    return info.output_muted;
#endif
  }

  return 0;
}

/*
 * Set a property of audio driver.
 * return value on success, ~value on failure
 */
static int ao_sun_set_property (ao_driver_t *self_gen, int property, int value) {
  sun_driver_t *self = (sun_driver_t *) self_gen;
  audio_info_t	info;

  AUDIO_INITINFO(&info);

  switch(property) {
  case AO_PROP_MIXER_VOL:
    break;
  case AO_PROP_PCM_VOL:
    info.play.gain = value;
    if (ioctl(self->audio_fd, AUDIO_SETINFO, &info) < 0)
      return ~value;
    return value;
#if !defined(__NetBSD__)    /* audio_info.output_muted is missing on NetBSD */
  case AO_PROP_MUTE_VOL:
    info.output_muted = value != 0;
    if (ioctl(self->audio_fd, AUDIO_SETINFO, &info) < 0)
      return ~value;
    return value;
#endif
  }

  return ~value;
}

ao_driver_t *init_audio_out_plugin (config_values_t *config) {

  sun_driver_t	  *self;
  char            *devname = "/dev/audio";
  int              audio_fd;
  int              status;
  audio_info_t	   info;

  self = (sun_driver_t *) malloc (sizeof (sun_driver_t));

  /*
   * find best device driver/channel
   */

  printf ("audio_sun_out: Opening audio device...\n");
  xprintf (VERBOSE|AUDIO, "audio_sun_out: Opening audio device...");

  /*
   * open the device
   */

  audio_fd=open(self->audio_dev = devname, O_WRONLY|O_NDELAY);

  if(audio_fd < 0) 
  {
    printf("audio_sun_out: opening audio device %s failed:\n%s\n",
	   devname, strerror(errno));

    free (self);
    return NULL;

  } else
    xprintf (VERBOSE|AUDIO, " %s\n", devname);

  /*
   * set up driver to reasonable values for capabilities tests
   */

  AUDIO_INITINFO(&info);
  info.play.encoding = AUDIO_ENCODING_LINEAR;
  info.play.precision = AUDIO_PRECISION_16;
  info.play.sample_rate = 44100;
  status = ioctl(audio_fd, AUDIO_SETINFO, &info);

  /*
   * get capabilities
   */

  self->capabilities = 0;

  printf ("audio_sun_out : supported modes are ");

  self->capabilities |= AO_CAP_MODE_MONO;
  printf ("mono ");

  self->capabilities |= AO_CAP_MODE_STEREO;
  printf ("stereo ");

  self->capabilities |= AO_CAP_PCM_VOL | AO_CAP_MUTE_VOL;
  printf ("\n");

  close (audio_fd);

  self->use_rtsc = realtime_samplecounter_available(self->audio_dev);
  self->output_sample_rate = 0;

  self->ao_driver.get_capabilities	= ao_sun_get_capabilities;
  self->ao_driver.get_property		= ao_sun_get_property;
  self->ao_driver.set_property		= ao_sun_set_property;
  self->ao_driver.open			= ao_sun_open;
  self->ao_driver.num_channels		= ao_sun_num_channels;
  self->ao_driver.bytes_per_frame	= ao_sun_bytes_per_frame;
  self->ao_driver.delay			= ao_sun_delay;
  self->ao_driver.write			= ao_sun_write;
  self->ao_driver.close			= ao_sun_close;
  self->ao_driver.exit			= ao_sun_exit;

  return &self->ao_driver;
}

static ao_info_t ao_info_sun = {
  AUDIO_OUT_IFACE_VERSION,
  "sun",
  "xine audio output plugin using sun-compliant audio devices/drivers",
  10
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_sun;
}

