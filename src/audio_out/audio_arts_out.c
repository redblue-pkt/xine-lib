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
 * $Id: audio_arts_out.c,v 1.4 2001/08/24 01:05:30 guenter Exp $
 */

/* required for swab() */
#define _XOPEN_SOURCE 500

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
#include <inttypes.h>
#include <artsc.h>

#include "xine_internal.h"
#include "monitor.h"
#include "audio_out.h"
#include "utils.h"

#define AO_OUT_ARTS_IFACE_VERSION 2

#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

/*#define GAP_TOLERANCE         5000 */
#define GAP_TOLERANCE        15000
#define MAX_GAP              90000

typedef struct arts_driver_s {

  ao_driver_t    ao_driver;

  arts_stream_t  audio_stream;
  int            capabilities;
  int            mode;

  int32_t        sample_rate;
  uint32_t       num_channels;
  uint32_t       bits_per_sample;
  uint32_t       bytes_per_frame;

  uint32_t       latency;

} arts_driver_t;

/*
 * open the audio device for writing to
 */
static int ao_arts_open(ao_driver_t *this_gen,
		   uint32_t bits, uint32_t rate, int mode)
{
  arts_driver_t *this = (arts_driver_t *) this_gen;
  int rc;

  printf ("audio_arts_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    printf ("audio_arts_out: unsupported mode %08x\n", mode);
    return 0;
  }

  if (this->audio_stream) {

    if ( (mode == this->mode) && (rate == this->sample_rate) )
      return this->sample_rate;

    arts_close_stream(this->audio_stream);
  }
  
  this->mode                   = mode;
  this->sample_rate            = rate;
  this->bits_per_sample        = bits;

  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    break;
  }

  this->bytes_per_frame=(this->bits_per_sample*this->num_channels)/8;

  printf ("audio_arts_out: %d channels output\n",this->num_channels);

  if( (rc=arts_init()) != 0 )
  {
    printf("arts_init error: %s\n",arts_error_text(rc));
    return 0;
  }

  this->audio_stream=arts_play_stream(this->sample_rate, bits, this->num_channels, "xine");

  this->latency = arts_stream_get (this->audio_stream, ARTS_P_TOTAL_LATENCY);

  printf ("audio_arts_out : latency %d ms\n", this->latency);

  return this->sample_rate;
}


static int ao_arts_num_channels(ao_driver_t *this_gen)
{
  arts_driver_t *this = (arts_driver_t *) this_gen;
    return this->num_channels;
}

static int ao_arts_bytes_per_frame(ao_driver_t *this_gen)
{
  arts_driver_t *this = (arts_driver_t *) this_gen;
  return this->bytes_per_frame;
}

static int ao_arts_get_gap_tolerance (ao_driver_t *this_gen)
{
  return GAP_TOLERANCE;
}

static int ao_arts_write(ao_driver_t *this_gen, int16_t *data,
                         uint32_t num_frames)
{
  arts_driver_t *this = (arts_driver_t *) this_gen;

  arts_write(this->audio_stream, data, num_frames * this->bytes_per_frame );

  return 1;
}


static int ao_arts_delay (ao_driver_t *this_gen)
{
  arts_driver_t *this = (arts_driver_t *) this_gen;

  /* Just convert latency (ms) to frame units.
     please note that there is no function in aRts C API to
     get the current buffer utilization. This is, at best,
     a very roughly aproximation.
  */

  return arts_stream_get (this->audio_stream, ARTS_P_TOTAL_LATENCY) * this->sample_rate / 1000;

/*  return this->latency * this->sample_rate / 1000; */
}

static void ao_arts_close(ao_driver_t *this_gen)
{
  arts_driver_t *this = (arts_driver_t *) this_gen;

  if (this->audio_stream) {
  arts_close_stream(this->audio_stream);
  arts_free();
  this->audio_stream = NULL;
  }
}

static uint32_t ao_arts_get_capabilities (ao_driver_t *this_gen) {
  arts_driver_t *this = (arts_driver_t *) this_gen;
  return this->capabilities;
}

static void ao_arts_exit(ao_driver_t *this_gen)
{
  arts_driver_t *this = (arts_driver_t *) this_gen;
  
  ao_arts_close(this_gen);

  free (this);
}

/*
 *
 */
static int ao_arts_get_property (ao_driver_t *this, int property) {

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
static int ao_arts_set_property (ao_driver_t *this, int property, int value) {

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

  arts_driver_t *this;
  int		   rc;

  this = (arts_driver_t *) malloc (sizeof (arts_driver_t));

  rc = arts_init();
  if(rc < 0) {
	  fprintf(stderr,"audio_arts_out: arts_init failed: %s\n",arts_error_text(rc));
	  return NULL;
  }
  arts_free();
  
  /*
   * set capabilities
   */
  this->capabilities = 0;
  printf ("audio_arts_out : supported modes are ");
  this->capabilities |= AO_CAP_MODE_MONO;
  printf ("mono ");
  this->capabilities |= AO_CAP_MODE_STEREO;
  printf ("stereo ");
  printf ("\n");

  this->sample_rate = 0;
  this->audio_stream = NULL;

  this->ao_driver.get_capabilities    = ao_arts_get_capabilities;
  this->ao_driver.get_property        = ao_arts_get_property;
  this->ao_driver.set_property        = ao_arts_set_property;
  this->ao_driver.open                = ao_arts_open;
  this->ao_driver.num_channels        = ao_arts_num_channels;
  this->ao_driver.bytes_per_frame     = ao_arts_bytes_per_frame;
  this->ao_driver.delay               = ao_arts_delay;
  this->ao_driver.write               = ao_arts_write;
  this->ao_driver.close               = ao_arts_close;
  this->ao_driver.exit                = ao_arts_exit;
  this->ao_driver.get_gap_tolerance   = ao_arts_get_gap_tolerance;
  
  return &this->ao_driver;
}

static ao_info_t ao_info_arts = {
  AUDIO_OUT_IFACE_VERSION,
  "arts",
  "xine audio output plugin using arts-compliant audio devices/drivers",
  5
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_arts;
}

