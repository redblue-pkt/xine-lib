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
 * $Id: audio_sun_out.c,v 1.20 2002/06/12 12:22:28 f1rmb Exp $
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
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#ifdef	__svr4__
#include <stropts.h>
#endif

#include "xine_internal.h"
#include "xineutils.h"
#include "audio_out.h"

#define	CS4231_WORKAROUND	1	/* enable workaround for audiocs play.samples bug */
#define	SW_SAMPLE_COUNT		1


#ifndef	AUDIO_CHANNELS_MONO
#define	AUDIO_CHANNELS_MONO	1
#define	AUDIO_CHANNELS_STEREO	2
#endif
#ifndef	AUDIO_PRECISION_8
#define	AUDIO_PRECISION_8	8
#define	AUDIO_PRECISION_16	16
#endif

#define AO_SUN_IFACE_VERSION 4

#define GAP_TOLERANCE         5000
#define GAP_NONRT_TOLERANCE   AO_MAX_GAP
#define	NOT_REAL_TIME		-1


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

  int		 convert_u8_s8;	       /* Builtin conversion 8-bit UNSIGNED->SIGNED */

#if	CS4231_WORKAROUND
  /*
   * Sun's audiocs driver has problems counting samples when we send
   * sound data chunks with a length that is not a multiple of 1024.
   * As a workaround for this problem, we re-block the audio stream,
   * so that we always send buffers of samples to the driver that have
   * a size of N*1024 bytes;
   */
#define	MIN_WRITE_SIZE	1024

  char		 buffer[MIN_WRITE_SIZE];
  unsigned	 buf_len;
#endif

#if	SW_SAMPLE_COUNT
  struct timeval tv0;
  uint_t	 sample0;
#endif

  uint_t	 last_samplecnt;
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
    fprintf(stderr, "rtsc: SETINFO failed\n");
    goto error;
  }
    
  if (write(fd, silence, len) != len) {
    fprintf(stderr, "rtsc: write failed\n");
    goto error;
  }

  if (ioctl(fd, AUDIO_GETINFO, &info)) {
    fprintf(stderr, "rtsc: GETINFO1, %s\n", strerror(errno));
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
	fprintf(stderr, "rtsc: GETINFO2 failed, %s\n", strerror(errno));
	goto error;
    }
    if (info.play.samples < last_samplecnt) {
	fprintf(stderr, "rtsc: %u > %u?\n", last_samplecnt, info.play.samples);
	goto error;
    }

    if ((increment = info.play.samples - last_samplecnt) > 0) {
	/* printf("audio_sun_out: sample counter increment: %d\n", increment); */
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

  /*
  printf("audio_sun_out: minimum sample counter increment per 10msec interval: %d\n"
  	 "\t%susing sample counter based timing code\n",
	 min_increment, rtsc_ok == RTSC_ENABLED ? "" : "not ");
  */
    

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
 *
 * Implicit assumptions about audio format (bits/rate/mode):
 *
 * bits == 16: We always get 16-bit samples in native endian format,
 * 	using signed linear encoding
 *
 * bits ==  8: 8-bit samples use unsigned linear encoding,
 *	other 8-bit formats (uLaw, aLaw, etc) are currently not supported
 *	by xine
 */
static int ao_sun_open(ao_driver_t *this_gen,
		       uint32_t bits, uint32_t rate, int mode)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t info;
  int ok;

  printf ("audio_sun_out: ao_sun_open rate=%d, mode=%d\n", rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    printf ("audio_sun_out: unsupported mode %08x\n", mode);
    return 0;
  }

  if (this->audio_fd >= 0) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) )
      return this->output_sample_rate;

    close (this->audio_fd);
  }
  
  this->mode			= mode;
  this->input_sample_rate	= rate;
  this->frames_in_buffer	= 0;

  /*
   * open audio device
   */

  this->audio_fd=open(this->audio_dev,O_WRONLY|O_NONBLOCK);
  if(this->audio_fd < 0) {
    printf("audio_sun_out: Opening audio device %s: %s\n",
	   this->audio_dev, strerror(errno));
    return 0;
  }
  
  /* We wanted non blocking open but now put it back to normal */
  fcntl(this->audio_fd, F_SETFL, fcntl(this->audio_fd, F_GETFL)&~O_NONBLOCK);

  /*
   * configure audio device
   */

  AUDIO_INITINFO(&info);
  info.play.channels = (mode & AO_CAP_MODE_STEREO)
      ? AUDIO_CHANNELS_STEREO
      : AUDIO_CHANNELS_MONO;
  info.play.precision = bits;
  info.play.encoding = bits == 8
      ? AUDIO_ENCODING_LINEAR8
      : AUDIO_ENCODING_LINEAR;
  info.play.sample_rate = this->input_sample_rate;
  info.play.eof = 0;
  info.play.samples = 0;

  this->convert_u8_s8 = 0;
  ok = ioctl(this->audio_fd, AUDIO_SETINFO, &info) >= 0;
  if (!ok && info.play.encoding == AUDIO_ENCODING_LINEAR8) {
      /*
       * Unsigned AUDIO_ENCODING_LINEAR8 not supported.
       * Maybe signed AUDIO_ENCODING_LINEAR works?
       */
      info.play.encoding = AUDIO_ENCODING_LINEAR;
      ok = ioctl(this->audio_fd, AUDIO_SETINFO, &info) >= 0;
      if (ok) this->convert_u8_s8 = 1;
  }

  if (!ok) {
      printf("audio_sun_out: Cannot configure audio device for "
	     "%dhz, %d channel, %d bits\n",
	     info.play.sample_rate, info.play.channels,
	     info.play.precision);
      close(this->audio_fd);
      this->audio_fd = -1;
      return 0;
  }

  this->last_samplecnt = 0;

  this->output_sample_rate = info.play.sample_rate;
  this->num_channels = info.play.channels;

  this->bytes_per_frame = 1;
  if (info.play.channels == AUDIO_CHANNELS_STEREO)
    this->bytes_per_frame *= 2;
  if (info.play.precision == 16)
    this->bytes_per_frame *= 2;

#if	CS4231_WORKAROUND
  this->buf_len = 0;
#endif

  /*
  printf ("audio_sun_out: audio rate : %d requested, %d provided by device/sec\n",
	   this->input_sample_rate, this->output_sample_rate);
  */

  printf ("audio_sun_out: %d channels output\n",this->num_channels);
  return this->output_sample_rate;
}

static int ao_sun_num_channels(ao_driver_t *this_gen) 
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_sun_bytes_per_frame(ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_sun_delay(ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t info;

  if (ioctl(this->audio_fd, AUDIO_GETINFO, &info) == 0 && info.play.samples) {

    if (info.play.samples < this->last_samplecnt) {
	printf("*** broken sound driver, sample counter runs backwards, cur %u < prev %u\n",
	       info.play.samples, this->last_samplecnt);
    }
    this->last_samplecnt = info.play.samples;

    if (this->use_rtsc == RTSC_ENABLED)
      return this->frames_in_buffer - info.play.samples;

#if	SW_SAMPLE_COUNT
    /* compute "current sample" based on real time */
    {
      struct timeval tv1;
      uint_t cur_sample;
      uint_t msec;

      gettimeofday(&tv1, NULL);

      msec = (tv1.tv_sec  - this->tv0.tv_sec)  * 1000
	  +  (tv1.tv_usec - this->tv0.tv_usec) / 1000;

      cur_sample = this->sample0 + this->output_sample_rate * msec / 1000;

      if (info.play.error) {
	AUDIO_INITINFO(&info);
	info.play.error = 0;
	ioctl(this->audio_fd, AUDIO_SETINFO, &info);
      }

      /*
       * more than 0.5 seconds difference between HW sample counter and
       * computed sample counter?  -> re-initialize
       */
      if (abs(cur_sample - info.play.samples) > this->output_sample_rate/2) {
	this->tv0 = tv1;
	this->sample0 = cur_sample = info.play.samples;
      }

      return this->frames_in_buffer - cur_sample;
    }
#endif
  }
  return NOT_REAL_TIME;
}

static int ao_sun_get_gap_tolerance (ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;

  if (this->use_rtsc == RTSC_ENABLED)
    return GAP_TOLERANCE;
  else
    return GAP_NONRT_TOLERANCE;
}


#if	CS4231_WORKAROUND
/*
 * Sun's audiocs driver has problems counting samples when we send
 * sound data chunks with a length that is not a multiple of 1024.
 * As a workaround for this problem, we re-block the audio stream,
 * so that we always send buffers of samples to the driver that have
 * a size of N*1024 bytes;
 */
static int sun_audio_write(sun_driver_t *this, char *buf, unsigned nbytes)
{
  unsigned total_bytes, remainder;
  int num_written;
  unsigned orig_nbytes = nbytes;

  total_bytes = this->buf_len + nbytes;
  remainder = total_bytes % MIN_WRITE_SIZE;
  if ((total_bytes -= remainder) > 0) {
    struct iovec iov[2];
    int iovcnt = 0;

    if (this->buf_len > 0) {
      iov[iovcnt].iov_base = this->buffer;
      iov[iovcnt].iov_len = this->buf_len;
      iovcnt++;
    }
    iov[iovcnt].iov_base = buf;
    iov[iovcnt].iov_len = total_bytes - this->buf_len;

    this->buf_len = 0;
    buf += iov[iovcnt].iov_len;
    nbytes -= iov[iovcnt].iov_len;

    num_written = writev(this->audio_fd, iov, iovcnt+1);
    if (num_written != total_bytes)
      return -1;
  }

  if (nbytes > 0) {
    memcpy(this->buffer + this->buf_len, buf, nbytes);
    this->buf_len += nbytes;
  }

  return orig_nbytes;
}


static void sun_audio_flush(sun_driver_t *this)
{
  if (this->buf_len > 0) {
    write(this->audio_fd, this->buffer, this->buf_len);
    this->buf_len = 0;
  }
}

#else
static int sun_audio_write(sun_driver_t *this, char *buf, unsigned nbytes)
{
  return write(this->audio_fd, buf, nbytes);
}

static void sun_audio_flush(sun_driver_t *this)
{
}
#endif


 /* Write audio samples
  * num_frames is the number of audio frames present
  * audio frames are equivalent one sample on each channel.
  * I.E. Stereo 16 bits audio frames are 4 bytes.
  */
static int ao_sun_write(ao_driver_t *this_gen,
                               int16_t* frame_buffer, uint32_t num_frames)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  int num_written;

  if (this->convert_u8_s8) {
      /* 
       * Audio hardware does not support 8-bit unsigned format,
       * only 8-bit signed.  Convert to 8-bit unsigned before sending
       * the data to the audio device.
       */
      uint8_t *p = (void *)frame_buffer;
      int i;

      for (i = num_frames * this->bytes_per_frame; --i >= 0; p++) 
	  *p ^= 0x80;
  }
  num_written = sun_audio_write(this, frame_buffer, num_frames * this->bytes_per_frame);
  if (num_written > 0)
    this->frames_in_buffer += num_written / this->bytes_per_frame;

  return num_written;
}

static void ao_sun_close(ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  sun_audio_flush(this);
  close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_sun_get_capabilities (ao_driver_t *this_gen) {
  sun_driver_t *this = (sun_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_sun_exit(ao_driver_t *this_gen)
{
  sun_driver_t *this = (sun_driver_t *) this_gen;
  
  if (this->audio_fd >= 0)
    close(this->audio_fd);

  free (this);
}

/*
 * Get a property of audio driver.
 * return 1 in success, 0 on failure. (and the property value?)
 */
static int ao_sun_get_property (ao_driver_t *this_gen, int property) {
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t	info;

  switch(property) {
  case AO_PROP_MIXER_VOL:
    break;
  case AO_PROP_PCM_VOL:
    if (ioctl(this->audio_fd, AUDIO_GETINFO, &info) < 0)
      return 0;
    return info.play.gain * 100 / AUDIO_MAX_GAIN;
#if !defined(__NetBSD__)    /* audio_info.output_muted is missing on NetBSD */
  case AO_PROP_MUTE_VOL:
    if (ioctl(this->audio_fd, AUDIO_GETINFO, &info) < 0)
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
static int ao_sun_set_property (ao_driver_t *this_gen, int property, int value) {
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t	info;

  AUDIO_INITINFO(&info);

  switch(property) {
  case AO_PROP_MIXER_VOL:
    break;
  case AO_PROP_PCM_VOL:
    info.play.gain = value * AUDIO_MAX_GAIN / 100;
    if (ioctl(this->audio_fd, AUDIO_SETINFO, &info) < 0)
      return ~value;
    return value;
#if !defined(__NetBSD__)    /* audio_info.output_muted is missing on NetBSD */
  case AO_PROP_MUTE_VOL:
    info.output_muted = value != 0;
    if (ioctl(this->audio_fd, AUDIO_SETINFO, &info) < 0)
      return ~value;
    return value;
#endif
  }

  return ~value;
}

static int ao_sun_ctrl(ao_driver_t *this_gen, int cmd, ...) {
  sun_driver_t *this = (sun_driver_t *) this_gen;
  audio_info_t	info;

  switch (cmd) {

  case AO_CTRL_PLAY_PAUSE:
    AUDIO_INITINFO(&info);
    info.play.pause = 1;
    ioctl(this->audio_fd, AUDIO_SETINFO, &info);
    break;

  case AO_CTRL_PLAY_RESUME:
    AUDIO_INITINFO(&info);
    info.play.pause = 0;
    ioctl(this->audio_fd, AUDIO_SETINFO, &info);
    break;

  case AO_CTRL_FLUSH_BUFFERS:
#ifdef	__svr4__
    /* flush buffered STEAMS data first */
    ioctl(this->audio_fd, I_FLUSH, FLUSHW);

    /* 
     * the flush above discarded an unknown amount of data from the
     * audio device.  To get the "*_delay" computation in sync again,
     * reset the audio device's sample counter to 0, after waiting
     * that all samples still active playing on the sound hardware
     * have finished playing.
     */
    AUDIO_INITINFO(&info);
    info.play.pause = 0;
    ioctl(this->audio_fd, AUDIO_SETINFO, &info);

    ioctl(this->audio_fd, AUDIO_DRAIN);

    AUDIO_INITINFO(&info);
    info.play.samples = 0;
    ioctl(this->audio_fd, AUDIO_SETINFO, &info);

    this->frames_in_buffer = 0;
    this->last_samplecnt = 0;
#endif
    break;
  }

  return 0;
}

ao_driver_t *init_audio_out_plugin (config_values_t *config) {

  sun_driver_t	  *this;
  char            *devname;
  int              audio_fd;
  int              status;
  audio_info_t	   info;

  this = (sun_driver_t *) malloc (sizeof (sun_driver_t));

  /* Fill the .xinerc file with options */ 
  devname = config->register_string(config,
				    "audio.sun_audio_device",
				    "/dev/audio",
				    _("device used for audio output with the 'Sun' audio plugin"),
				    NULL,
				    NULL,
				    NULL);

  /*
   * find best device driver/channel
   */

  printf ("audio_sun_out: Opening audio device %s...\n", devname);

  /*
   * open the device
   */

  audio_fd=open(this->audio_dev = devname, O_WRONLY|O_NONBLOCK);

  if(audio_fd < 0) 
  {
    fprintf(stderr, "audio_sun_out: opening audio device %s failed:\n%s\n",
	   devname, strerror(errno));

    free (this);
    return NULL;

  }

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

  this->capabilities = 0;

  printf ("audio_sun_out: supported modes are ");

  this->capabilities |= AO_CAP_MODE_MONO;
  printf ("mono ");

  this->capabilities |= AO_CAP_MODE_STEREO;
  printf ("stereo ");

  this->capabilities |= AO_CAP_PCM_VOL | AO_CAP_MUTE_VOL;
  printf ("\n");

  close (audio_fd);

  this->audio_fd = -1;
  this->use_rtsc = realtime_samplecounter_available(this->audio_dev);
  this->output_sample_rate = 0;

  this->ao_driver.get_capabilities	= ao_sun_get_capabilities;
  this->ao_driver.get_property		= ao_sun_get_property;
  this->ao_driver.set_property		= ao_sun_set_property;
  this->ao_driver.open			= ao_sun_open;
  this->ao_driver.num_channels		= ao_sun_num_channels;
  this->ao_driver.bytes_per_frame	= ao_sun_bytes_per_frame;
  this->ao_driver.delay			= ao_sun_delay;
  this->ao_driver.write			= ao_sun_write;
  this->ao_driver.close			= ao_sun_close;
  this->ao_driver.exit			= ao_sun_exit;
  this->ao_driver.get_gap_tolerance     = ao_sun_get_gap_tolerance;
  this->ao_driver.control		= ao_sun_ctrl;

  return &this->ao_driver;
}

static ao_info_t ao_info_sun = {
  AO_SUN_IFACE_VERSION,
  "sun",
  NULL,
  10
};

ao_info_t *get_audio_out_plugin_info() {
  ao_info_sun.description = _("xine audio output plugin using sun-compliant audio devices/drivers"); 
  return &ao_info_sun;
}

