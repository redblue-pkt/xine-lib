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
 * $Id: audio_esd_out.c,v 1.9 2001/09/04 20:30:55 guenter Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <esd.h>
#include <sys/time.h>
#include <inttypes.h>

#include "xine_internal.h"
#include "monitor.h"
#include "audio_out.h"
#include "metronom.h"
#include "utils.h"

#define AO_OUT_ESD_IFACE_VERSION 1


typedef struct esd_driver_s {

  ao_driver_t    ao_driver;

  int            audio_fd;
  int            capabilities;
  int            mode;

  int32_t        output_sample_rate, input_sample_rate;
  double         sample_rate_factor;
  uint32_t       num_channels;
  uint32_t	 bytes_per_frame;

  int            latency, gap_tolerance;

} esd_driver_t;


/*
 * connect to esd 
 */
static int ao_esd_open(ao_driver_t *this_gen,
		       uint32_t bits, uint32_t rate, int mode)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  esd_format_t     format;

  printf ("audio_esd_out: ao_open bits=%d rate=%d, mode=%d\n", 
	  bits, rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    printf ("audio_esd_out: unsupported mode %08x\n", mode);
    return 0;
  }

  if (this->audio_fd>=0) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) )
      return this->output_sample_rate;

    close (this->audio_fd);
  }
  
  this->mode                   = mode;
  this->input_sample_rate      = rate;
  this->output_sample_rate     = rate;

  /*
   * open stream to ESD server
   */

  format = ESD_STREAM | ESD_PLAY | ESD_BITS16;
  switch (mode) {
  case AO_CAP_MODE_MONO:
    format |= ESD_MONO;
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    format |= ESD_STEREO;
    this->num_channels = 2;
    break;
  }
  printf ("audio_esd_out: %d channels output\n",this->num_channels);

  this->bytes_per_frame=(bits*this->num_channels)/8;

  if (this->output_sample_rate > 44100)
    this->output_sample_rate = 44100;


  this->audio_fd=esd_play_stream(format, this->output_sample_rate, NULL, NULL);
  if (this->audio_fd < 0) {
    printf("audio_esd_out: connecting to ESD server %s: %s\n",
	   getenv("ESPEAKER"), strerror(errno));
    return 0;
  }

  return this->output_sample_rate;
}

static int ao_esd_num_channels(ao_driver_t *this_gen) 
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->num_channels;
}

static int ao_esd_bytes_per_frame(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->bytes_per_frame;
}


static int ao_esd_delay(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;

  return this->latency;
}

static int ao_esd_write(ao_driver_t *this_gen,
			int16_t* frame_buffer, uint32_t num_frames)
{

  esd_driver_t *this = (esd_driver_t *) this_gen;

  if (this->audio_fd<0)
    return 1;

  write(this->audio_fd, frame_buffer, num_frames * this->bytes_per_frame);

  return 1;
}

static void ao_esd_close(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  esd_close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_esd_get_capabilities (ao_driver_t *this_gen) {
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->capabilities;
}

static int ao_esd_get_gap_tolerance (ao_driver_t *this_gen) {
  esd_driver_t *this = (esd_driver_t *) this_gen;
  return this->gap_tolerance ;
}

static void ao_esd_exit(ao_driver_t *this_gen)
{
  esd_driver_t *this = (esd_driver_t *) this_gen;
  
  if (this->audio_fd != -1)
    esd_close(this->audio_fd);

  free (this);
}

static int ao_esd_get_property (ao_driver_t *this, int property) {

  /* FIXME: implement some properties
  */
  return 0;
}

static int ao_esd_set_property (ao_driver_t *this, int property, int value) {

  /* FIXME: Implement property support.
  */

  return ~value;
}

ao_driver_t *init_audio_out_plugin (config_values_t *config) {

  esd_driver_t *this;
  int           audio_fd;

  /*
   * open stream to ESD server
   */

  printf("audio_esd_out: connecting to esd server...\n");
  audio_fd = esd_open_sound(NULL);

  if(audio_fd < 0) {
    char *server = getenv("ESPEAKER");

    /* print a message so the user knows why ESD failed */
    printf("audio_esd_out: can't connect to %s ESD server: %s\n",
	   server ? server : "local", strerror(errno));

    return NULL;
  } 
  
  esd_close(audio_fd);


  this = (esd_driver_t *) malloc (sizeof (esd_driver_t));
  this->output_sample_rate = 0;
  this->audio_fd           = -1;
  this->capabilities       = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO;
  this->latency            = config->lookup_int (config, "esd_latency", 30000);
  this->gap_tolerance      = config->lookup_int (config, "esd_gap_tolerance", 15000);

  this->ao_driver.get_capabilities    = ao_esd_get_capabilities;
  this->ao_driver.get_property        = ao_esd_get_property;
  this->ao_driver.set_property        = ao_esd_set_property;
  this->ao_driver.open                = ao_esd_open;
  this->ao_driver.num_channels        = ao_esd_num_channels;
  this->ao_driver.bytes_per_frame     = ao_esd_bytes_per_frame;
  this->ao_driver.get_gap_tolerance   = ao_esd_get_gap_tolerance;
  this->ao_driver.delay               = ao_esd_delay;
  this->ao_driver.write		      = ao_esd_write;
  this->ao_driver.close               = ao_esd_close;
  this->ao_driver.exit                = ao_esd_exit;

  return &this->ao_driver;
}

static ao_info_t ao_info_esd = {
  AO_OUT_ESD_IFACE_VERSION,
  "esd",
  "xine audio output plugin using esd",
  5
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_esd;
}

