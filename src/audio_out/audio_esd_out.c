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
 * $Id: audio_esd_out.c,v 1.5 2001/06/25 08:46:55 guenter Exp $
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
#include "resample.h"
#include "metronom.h"
#include "utils.h"

#define AO_OUT_ESD_IFACE_VERSION 1


#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

#define GAP_TOLERANCE        15000
#define MAX_GAP              90000

typedef struct esd_functions_s {

  ao_functions_t ao_functions;

  metronom_t    *metronom;

  int            audio_fd;
  int            capabilities;
  int            mode;

  int32_t        output_sample_rate, input_sample_rate;
  double         sample_rate_factor;
  uint32_t       num_channels;

  uint32_t       bytes_in_buffer;      /* number of bytes writen to audio hardware   */

  int            audio_step;           /* pts per 32 768 samples (sample = #bytes/2) */
  int32_t        bytes_per_kpts;       /* bytes per 1024/90000 sec                   */

  uint16_t      *sample_buffer;
  int16_t       *zero_space;
  
  int            audio_started;
  uint32_t       last_audio_vpts;

  uint32_t       latency;

} esd_functions_t;


/*
 * connect to esd 
 */
static int ao_open(ao_functions_t *this_gen,
		   uint32_t bits, uint32_t rate, int mode)
{
  esd_functions_t *this = (esd_functions_t *) this_gen;
  esd_format_t     format;

  printf ("audio_esd_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    printf ("audio_esd_out: unsupported mode %08x\n", mode);
    return -1;
  }

  if (this->audio_fd>=0) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) )
      return 1;

    close (this->audio_fd);
  }
  
  this->mode                   = mode;
  this->input_sample_rate      = rate;
  this->bytes_in_buffer        = 0;
  this->audio_started          = 0;
  this->last_audio_vpts        = 0;
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

  if (this->output_sample_rate > 44100)
    this->output_sample_rate = 44100;


  this->audio_fd=esd_play_stream(format, this->output_sample_rate, NULL, NULL);
  if (this->audio_fd < 0) {
    printf("audio_esd_out: connecting to ESD server %s: %s\n",
	   getenv("ESPEAKER"), strerror(errno));
    return -1;
  }

  this->sample_rate_factor = (double) this->output_sample_rate / (double) this->input_sample_rate;
  this->audio_step         = (uint32_t) 90000 * (uint32_t) 32768 
                                 / this->input_sample_rate;
  this->bytes_per_kpts     = this->output_sample_rate * this->num_channels * 2 * 1024 / 90000;

  printf ("audio_out : audio_step %d pts per 32768 samples\n", this->audio_step);

  return 1;
}

static void ao_fill_gap (esd_functions_t *this, uint32_t pts_len) {

  int num_bytes ;

  if (pts_len > MAX_GAP)
    pts_len = MAX_GAP;
  num_bytes = pts_len * this->bytes_per_kpts / 1024;
  num_bytes = (num_bytes / (2*this->num_channels)) * (2*this->num_channels);

  printf ("audio_esd_out: inserting %d 0-bytes to fill a gap of %d pts\n",num_bytes, pts_len);
  
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

  esd_functions_t *this = (esd_functions_t *) this_gen;
  uint32_t         vpts, buffer_vpts;
  int32_t          gap;
  int              bDropPackage;

  if (this->audio_fd<0)
    return 1;

  vpts = this->metronom->got_audio_samples (this->metronom, pts_, num_samples);

  xprintf (VERBOSE|AUDIO, "audio_esd_out: got %d samples, vpts=%d\n",
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

  buffer_vpts += 3000; /* FIXME - esd doesn't have sync capabilities */

  /*
  printf ("audio_esd_out: got audio package vpts = %d, buffer_vpts = %d\n",
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

  if (!bDropPackage) {
    int num_output_samples = num_samples * (this->output_sample_rate) / this->input_sample_rate;

    switch (this->mode) {
    case AO_CAP_MODE_MONO:
      audio_out_resample_mono (output_samples, num_samples,
			       this->sample_buffer, num_output_samples);
      write(this->audio_fd, this->sample_buffer, num_output_samples * 2);
      break;
    case AO_CAP_MODE_STEREO:
      audio_out_resample_stereo (output_samples, num_samples,
				 this->sample_buffer, num_output_samples);
      write(this->audio_fd, this->sample_buffer, num_output_samples * 4);
      break;
    }

    xprintf (AUDIO|VERBOSE, "audio_esd_out :audio package written\n");
    
    /*
     * step values
     */
    
    this->bytes_in_buffer += num_output_samples * 2 * this->num_channels;
    this->audio_started    = 1;
  } else {
    printf ("audio_esd_out: audio package (vpts = %d) dropped\n", vpts);
  }

  return 1;

}

static void ao_close(ao_functions_t *this_gen)
{
  esd_functions_t *this = (esd_functions_t *) this_gen;
  esd_close(this->audio_fd);
  this->audio_fd = -1;
}

static uint32_t ao_get_capabilities (ao_functions_t *this_gen) {
  esd_functions_t *this = (esd_functions_t *) this_gen;
  return this->capabilities;
}

static void ao_connect (ao_functions_t *this_gen, metronom_t *metronom) {
  esd_functions_t *this = (esd_functions_t *) this_gen;
  
  this->metronom = metronom;
}

static void ao_exit(ao_functions_t *this_gen)
{
  esd_functions_t *this = (esd_functions_t *) this_gen;
  
  if (this->audio_fd != -1)
    esd_close(this->audio_fd);

  free (this->sample_buffer);
  free (this->zero_space);
  free (this);
}

static int ao_get_property (ao_functions_t *this, int property) {

  /* FIXME: implement some properties
  */
  return 0;
}

static int ao_set_property (ao_functions_t *this, int property, int value) {

  /* FIXME: Implement property support.
  */

  return ~value;
}

ao_functions_t *init_audio_out_plugin (config_values_t *config) {

  esd_functions_t *this;
  int              audio_fd;

  /*
   * open stream to ESD server
   */

  xprintf(VERBOSE|AUDIO, "Connecting to ESD server...");
  audio_fd = esd_open_sound(NULL);

  if(audio_fd < 0) 
  {
    char *server = getenv("ESPEAKER");

    // print a message so the user knows why ESD failed
    printf("audio_esd_out: can't connect to %s ESD server: %s\n",
	   server ? server : "local", strerror(errno));

    return NULL;
  } 
  
  esd_close(audio_fd);


  this = (esd_functions_t *) malloc (sizeof (esd_functions_t));
  this->output_sample_rate = 0;
  this->audio_fd = -1;
  this->capabilities = AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO;

  this->sample_buffer = malloc (40000);
  memset (this->sample_buffer, 0, 40000);
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

static ao_info_t ao_info_esd = {
  AUDIO_OUT_IFACE_VERSION,
  "esd",
  "xine audio output plugin using esd",
  1
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_esd;
}

