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
 * $Id: audio_sun_out.c,v 1.4 2001/06/26 18:47:13 jkeil Exp $
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

#include "xine_internal.h"
#include "monitor.h"
#include "audio_out.h"
#include "resample.h"
#include "metronom.h"
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


typedef struct sun_functions_s {

  ao_functions_t ao_functions;

  metronom_t    *metronom;

  char		*audio_dev;
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
} sun_functions_t;

/*
 * open the audio device for writing to
 */
static int ao_open(ao_functions_t *this_gen,
		   uint32_t bits, uint32_t rate, int mode)
{
  sun_functions_t *this = (sun_functions_t *) this_gen;
  audio_info_t info;

  printf ("audio_sun_out: ao_open rate=%d, mode=%d\n", rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    printf ("audio_sun_out: unsupported mode %08x\n", mode);
    return -1;
  }

  if (this->audio_fd > -1) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) )
      return 1;

    close (this->audio_fd);
  }
  
  this->mode                   = mode;
  this->input_sample_rate      = rate;
  this->bytes_in_buffer        = 0;
  this->audio_started          = 0;
  this->last_audio_vpts	       = 0;

  /*
   * open audio device
   */

  this->audio_fd=open(this->audio_dev,O_WRONLY|O_NDELAY);
  if(this->audio_fd < 0) {
    printf("audio_sun_out: Opening audio device %s: %s\n",
	   this->audio_dev, strerror(errno));
    return -1;
  }
  
  /* We wanted non blocking open but now put it back to normal */
  fcntl(this->audio_fd, F_SETFL, fcntl(this->audio_fd, F_GETFL)&~O_NDELAY);

  /*
   * configure audio device
   */

  AUDIO_INITINFO(&info);
  info.play.channels = (mode & AO_CAP_MODE_STEREO)
      ? AUDIO_CHANNELS_STEREO
      : AUDIO_CHANNELS_MONO;
  info.play.precision = bits;
  info.play.encoding = AUDIO_ENCODING_LINEAR;
  info.play.sample_rate = this->input_sample_rate;
  info.play.eof = 0;
  info.play.samples = 0;

  ioctl(this->audio_fd, AUDIO_SETINFO, &info);

  this->output_sample_rate = info.play.sample_rate;
  this->num_channels = info.play.channels;

  xprintf (VERBOSE|AUDIO, "audio_sun_out: audio rate : %d requested, %d provided by device/sec\n",
	   this->input_sample_rate, this->output_sample_rate);

  printf ("audio_sun_out : %d channels output\n",this->num_channels);

  this->sample_rate_factor = (double) this->output_sample_rate / (double) this->input_sample_rate;
  this->audio_step         = (uint32_t) 90000 * (uint32_t) 32768 
                                 / this->input_sample_rate;
  this->bytes_per_kpts     = this->output_sample_rate * this->num_channels * 2 * 1024 / 90000;

  xprintf (VERBOSE|AUDIO, "audio_out : audio_step %d pts per 32768 samples\n", this->audio_step);
  printf ("audio_out : audio_step %d pts per 32768 samples\n", this->audio_step);

  this->metronom->set_audio_rate(this->metronom, this->audio_step);

  return 1;
}

static void ao_fill_gap (sun_functions_t *this, uint32_t pts_len) {

  int num_bytes;

  if (pts_len > MAX_GAP)
    pts_len = MAX_GAP;

  num_bytes = pts_len * this->bytes_per_kpts / 1024;
  num_bytes = (num_bytes / (2*this->num_channels)) * (2*this->num_channels);
  if(this->mode == AO_CAP_MODE_AC3) return;
  printf ("audio_sun_out: inserting %d 0-bytes to fill a gap of %d pts\n",num_bytes, pts_len);
  
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

  sun_functions_t *this = (sun_functions_t *) this_gen;
  uint32_t         vpts, buffer_vpts;
  int32_t          gap;
  int              bDropPackage;
  uint16_t         sample_buffer[10000];
  int              pos;
  audio_info_t	   info;

  if (this->audio_fd<0)
    return 1;

  vpts = this->metronom->got_audio_samples (this->metronom, pts_, num_samples);

  if (vpts<this->last_audio_vpts) {
    /* reject this */

    return 1;
  }

  this->last_audio_vpts = vpts;

  xprintf (VERBOSE|AUDIO, "audio_sun_out: got %d samples, vpts=%d\n",
	   num_samples, vpts);

  /*
   * where, in the timeline is the "end" of the audio buffer at the moment?
   */

  buffer_vpts = this->metronom->get_current_time (this->metronom);

  if (this->audio_started) {
    ioctl (this->audio_fd, AUDIO_GETINFO, &info);
    pos = info.play.samples * 2 * this->num_channels;
  } else
    pos = 0;

  if (pos>this->bytes_in_buffer) /* buffer ran dry */ 
    this->bytes_in_buffer = pos;

  buffer_vpts += (this->bytes_in_buffer - pos) * 1024 / this->bytes_per_kpts;

  /*
  printf ("audio_sun_out: got audio package vpts = %d, buffer_vpts = %d\n",
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

  /*
  if(this->mode == AO_CAP_MODE_AC3) bDropPackage=0;
  */

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
    /*
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
    */
    default:
      fprintf(stderr, "audio_sun_out: unsupported audio mode %d\n", this->mode);
      break;
    }

    xprintf (AUDIO|VERBOSE, "audio_sun_out :audio package written\n");
    
    /*
     * step values
     */
    
    this->bytes_in_buffer += num_output_samples * 2 * this->num_channels;
    this->audio_started    = 1;
  } else {
    printf ("audio_sun_out: audio package (vpts = %d) dropped\n", vpts);
  }

  return 1;
}


static void ao_close(ao_functions_t *this_gen)
{
  sun_functions_t *this = (sun_functions_t *) this_gen;
  close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_get_capabilities (ao_functions_t *this_gen) {
  sun_functions_t *this = (sun_functions_t *) this_gen;
  return this->capabilities;
}

static void ao_connect (ao_functions_t *this_gen, metronom_t *metronom) {
  sun_functions_t *this = (sun_functions_t *) this_gen;
  
  this->metronom = metronom;
}

static void ao_exit(ao_functions_t *this_gen)
{
  sun_functions_t *this = (sun_functions_t *) this_gen;
  
  if (this->audio_fd != -1)
    close(this->audio_fd);

  free (this->zero_space);
  free (this);
}

/*
 * Get a property of audio driver.
 * return 1 in success, 0 on failure. (and the property value?)
 */
static int ao_get_property (ao_functions_t *this_gen, int property) {
  sun_functions_t *this = (sun_functions_t *) this_gen;
  audio_info_t	info;

  switch(property) {
  case AO_PROP_MIXER_VOL:
    break;
  case AO_PROP_PCM_VOL:
    if (ioctl(this->audio_fd, AUDIO_GETINFO, &info) < 0)
      return 0;
    return info.play.gain;
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
static int ao_set_property (ao_functions_t *this_gen, int property, int value) {
  sun_functions_t *this = (sun_functions_t *) this_gen;
  audio_info_t	info;

  AUDIO_INITINFO(&info);

  switch(property) {
  case AO_PROP_MIXER_VOL:
    break;
  case AO_PROP_PCM_VOL:
    info.play.gain = value;
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

ao_functions_t *init_audio_out_plugin (config_values_t *config) {

  sun_functions_t *this;
  char            *devname = "/dev/audio";
  int              audio_fd;
  int              status;
  audio_info_t	   info;

  this = (sun_functions_t *) malloc (sizeof (sun_functions_t));

  /*
   * find best device driver/channel
   */

  printf ("audio_sun_out: Opening audio device...\n");
  xprintf (VERBOSE|AUDIO, "audio_sun_out: Opening audio device...");

  /*
   * open the device
   */

  audio_fd=open(this->audio_dev = devname, O_WRONLY|O_NDELAY);

  if(audio_fd < 0) 
  {
    printf("audio_sun_out: opening audio device %s failed:\n%s\n",
	   devname, strerror(errno));

    free (this);
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

  this->capabilities = 0;

  printf ("audio_sun_out : supported modes are ");

  this->capabilities |= AO_CAP_MODE_MONO;
  printf ("mono ");

  this->capabilities |= AO_CAP_MODE_STEREO;
  printf ("stereo ");

  this->capabilities |= AO_CAP_PCM_VOL | AO_CAP_MUTE_VOL;
  printf ("\n");

  close (audio_fd);

  this->output_sample_rate = 0;

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

static ao_info_t ao_info_sun = {
  AUDIO_OUT_IFACE_VERSION,
  "sun",
  "xine audio output plugin using sun-compliant audio devices/drivers",
  5
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_sun;
}

