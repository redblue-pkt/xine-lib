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
 * $Id: audio_arts_out.c,v 1.1 2001/06/24 07:17:37 guenter Exp $
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
#include "resample.h"
#include "metronom.h"
#include "utils.h"

#define AO_OUT_ARTS_IFACE_VERSION 1

#define AUDIO_NUM_FRAGMENTS     15
#define AUDIO_FRAGMENT_SIZE   8192

/*#define GAP_TOLERANCE         5000 */
#define GAP_TOLERANCE        15000
#define MAX_GAP              90000

typedef struct arts_functions_s {

  ao_functions_t ao_functions;

  metronom_t    *metronom;

  arts_stream_t  audio_stream;
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

  uint32_t       latency;

} arts_functions_t;

/*
 * open the audio device for writing to
 */
static int ao_open(ao_functions_t *this_gen,
		   uint32_t bits, uint32_t rate, int mode)
{
  arts_functions_t *this = (arts_functions_t *) this_gen;

  printf ("audio_arts_out: ao_open bits=%d rate=%d, mode=%d\n", bits, rate, mode);

  if ( (mode & this->capabilities) == 0 ) {
    printf ("audio_arts_out: unsupported mode %08x\n", mode);
    return -1;
  }

  if (this->audio_stream) {

    if ( (mode == this->mode) && (rate == this->input_sample_rate) )
      return 1;

    arts_close_stream(this->audio_stream);
  }
  
  this->mode                   = mode;
  this->input_sample_rate      = rate;
  this->bytes_in_buffer        = 0;
  this->audio_started          = 0;
  this->last_audio_vpts        = 0;
  
  this->output_sample_rate = rate;

  switch (mode) {
  case AO_CAP_MODE_MONO:
    this->num_channels = 1;
    break;
  case AO_CAP_MODE_STEREO:
    this->num_channels = 2;
    break;
  }
  printf ("audio_arts_out: %d channels output\n",this->num_channels);

  /* XXX: Handle errors */
  arts_init();

  this->audio_stream=arts_play_stream(this->output_sample_rate, bits, this->num_channels, "xine");

  this->sample_rate_factor = (double) this->output_sample_rate / (double) this->input_sample_rate;
  this->audio_step         = (uint32_t) 90000 * (uint32_t) 32768 
                                 / this->input_sample_rate;
  this->bytes_per_kpts     = this->output_sample_rate * this->num_channels * 2 * 1024 / 90000;

  printf ("audio_out : audio_step %d pts per 32768 samples\n", this->audio_step);

  this->latency = arts_stream_get (this->audio_stream, ARTS_P_TOTAL_LATENCY);

  printf ("audio_out : latency %d ms\n", this->latency);

  this->metronom->set_audio_rate(this->metronom, this->audio_step);

  return 1;
}

static void ao_fill_gap (arts_functions_t *this, uint32_t pts_len) {

  int num_bytes;

  if (pts_len > MAX_GAP)
    pts_len = MAX_GAP;

  num_bytes= pts_len * this->bytes_per_kpts / 1024;
  num_bytes = (num_bytes / (2*this->num_channels)) * (2*this->num_channels);
  if(this->mode == AO_CAP_MODE_AC3) return;
  printf ("audio_arts_out: inserting %d 0-bytes to fill a gap of %d pts\n",num_bytes, pts_len);
  
  this->bytes_in_buffer += num_bytes;
  
  while (num_bytes>0) {
    if (num_bytes>8192) {
      arts_write(this->audio_stream, this->zero_space, 8192);
      num_bytes -= 8192;
    } else {
      arts_write(this->audio_stream, this->zero_space, num_bytes);
      num_bytes = 0;
    }
  }
}

static int ao_write_audio_data(ao_functions_t *this_gen,
			       int16_t* output_samples, uint32_t num_samples, 
			       uint32_t pts_)
{

  arts_functions_t *this = (arts_functions_t *) this_gen;
  uint32_t         vpts, buffer_vpts;
  int32_t          gap;
  int              bDropPackage;
  uint16_t         sample_buffer[10000];
/*  count_info       info; */
/*  int              pos; */
  
  if (this->audio_stream<0)
    return 1;

  xprintf (VERBOSE|AUDIO, "audio_arts_out: got %d samples, vpts=%d\n",
	   num_samples, vpts);

  vpts = this->metronom->got_audio_samples (this->metronom, pts_, num_samples);

  if (vpts<this->last_audio_vpts) {
    /* reject this */

    return 1;
  }

  this->last_audio_vpts = vpts;

  /*
   * where, in the timeline is the "end" of the audio buffer at the moment?
   */

  buffer_vpts = this->metronom->get_current_time (this->metronom);

  buffer_vpts += this->latency * 90;

/*
  if (this->audio_started) {
    ioctl (this->audio_fd, SNDCTL_DSP_GETOPTR, &info);
    pos = info.bytes;
  } else

    pos = 0;
*/

//  if (pos>this->bytes_in_buffer) /* buffer ran dry */ 
//    this->bytes_in_buffer = pos;

//  buffer_vpts += (this->bytes_in_buffer - pos) * 1024 / this->bytes_per_kpts;


  /*
  printf ("audio_arts_out: got audio package vpts = %d, buffer_vpts = %d\n",
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
      arts_write(this->audio_stream, sample_buffer, num_output_samples * 2);
      break;
    case AO_CAP_MODE_STEREO:
      audio_out_resample_stereo (output_samples, num_samples,
    				 sample_buffer, num_output_samples);
      arts_write(this->audio_stream, sample_buffer, num_output_samples * 4);
      break;
    case AO_CAP_MODE_4CHANNEL:
      audio_out_resample_4channel (output_samples, num_samples,
				   sample_buffer, num_output_samples);
      arts_write(this->audio_stream, sample_buffer, num_output_samples * 8);
      break;
    case AO_CAP_MODE_5CHANNEL:
      audio_out_resample_5channel (output_samples, num_samples,
				   sample_buffer, num_output_samples);
      arts_write(this->audio_stream, sample_buffer, num_output_samples * 10);
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
      arts_write(this->audio_stream, sample_buffer, num_output_samples);
      arts_write(this->audio_stream, this->zero_space, 6144-num_output_samples);
      num_output_samples=num_output_samples/4;
      break;
    }

    xprintf (AUDIO|VERBOSE, "audio_arts_out :audio package written\n");
    
    /*
     * step values
     */
    
    this->bytes_in_buffer += num_output_samples * 2 * this->num_channels;
    this->audio_started    = 1;
  } else {
    printf ("audio_arts_out: audio package (vpts = %d) dropped\n", vpts);
  }

  return 1;
}


static void ao_close(ao_functions_t *this_gen)
{
  arts_functions_t *this = (arts_functions_t *) this_gen;
  arts_close_stream(this->audio_stream);
  arts_free();
  this->audio_stream = NULL;
}

static uint32_t ao_get_capabilities (ao_functions_t *this_gen) {
  arts_functions_t *this = (arts_functions_t *) this_gen;
  return this->capabilities;
}

static void ao_connect (ao_functions_t *this_gen, metronom_t *metronom) {
  arts_functions_t *this = (arts_functions_t *) this_gen;
  
  this->metronom = metronom;
}

static void ao_exit(ao_functions_t *this_gen)
{
  arts_functions_t *this = (arts_functions_t *) this_gen;
  
  if (this->audio_stream) {
    arts_close_stream(this->audio_stream);
    arts_free();
  }

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

  arts_functions_t *this;
  int		   rc;

  this = (arts_functions_t *) malloc (sizeof (arts_functions_t));

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

static ao_info_t ao_info_arts = {
  AUDIO_OUT_IFACE_VERSION,
  "arts",
  "xine audio output plugin using arts-compliant audio devices/drivers",
  5
};

ao_info_t *get_audio_out_plugin_info() {
  return &ao_info_arts;
}

