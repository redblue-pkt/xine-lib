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

#include "xine/xine.h"
#include "xine/monitor.h"
#include "xine/audio_out.h"
#include "resample.h"
#include "xine/metronom.h"
#include "xine/ac3.h"
#include "xine/utils.h"

#define GAP_TOLERANCE        15000
#define MAX_MASTER_CLOCK_DIV  5000

extern uint32_t xine_debug;

typedef struct _audio_esd_globals {

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

} audio_esd_globals_t;

static audio_esd_globals_t gAudioESD;

/*
 * open the audio device for writing to
 */
static int ao_open(uint32_t bits, uint32_t rate, int mode)
{
  esd_format_t format;

  printf ("audio_esd_out: ao_open rate=%d, mode=%d\n", rate, mode);

  if ((mode != AO_MODE_STEREO) && (mode != AO_MODE_MONO)) {
    printf ("ESD Driver only supports mono/stereo output modes at the moment\n");
    return -1;
  }

  if (gAudioESD.audio_fd > -1) {

    if (rate == gAudioESD.input_sample_rate)
      return 1;

    close (gAudioESD.audio_fd);
  }

  gAudioESD.input_sample_rate      = rate;
  gAudioESD.bytes_in_buffer        = 0;
  gAudioESD.last_vpts              = 0;
  gAudioESD.output_rate_correction = 0;
  gAudioESD.sync_vpts              = 0;
  gAudioESD.sync_bytes_in_buffer   = 0;
  gAudioESD.audio_started          = 0;

  /*
   * open stream to ESD server
   */

  format = ESD_STREAM | ESD_PLAY | ESD_BITS16;
  if (mode == AO_MODE_STEREO) {
    format |= ESD_STEREO;
    gAudioESD.num_channels = 2;
  } else {
    format |= ESD_MONO;
    gAudioESD.num_channels = 1;
  }
  gAudioESD.output_sample_rate = gAudioESD.input_sample_rate;
  if (gAudioESD.output_sample_rate > 44100)
    gAudioESD.output_sample_rate = 44100;

  gAudioESD.audio_fd=esd_play_stream(format, gAudioESD.output_sample_rate, NULL, NULL);
  if(gAudioESD.audio_fd < 0) {
    printf("audio_esd_out: Connecting to ESD server %s: %s\n",
	   getenv("ESPEAKER"), strerror(errno));
    return -1;
  }

  xprintf (VERBOSE|AUDIO, "audio_esd_out: %d channels\n",gAudioESD.num_channels);
  
  gAudioESD.sample_rate_factor = (double) gAudioESD.output_sample_rate / (double) gAudioESD.input_sample_rate;
  gAudioESD.audio_step         = (uint32_t) 90000 * (uint32_t) 32768 
                                 / gAudioESD.input_sample_rate;
  gAudioESD.bytes_per_kpts     = gAudioESD.output_sample_rate * gAudioESD.num_channels * 2 * 1024 / 90000;

  xprintf (VERBOSE|AUDIO, "audio_out : audio_step %d pts per 32768 samples\n", gAudioESD.audio_step);

  metronom_set_audio_rate (gAudioESD.audio_step);

  return 1;
}

static uint32_t ao_get_current_vpts (void) {

  int32_t  diff ;
  uint32_t vpts ;

  if (gAudioESD.audio_started)
    diff = 0;
  else
    diff = gAudioESD.sync_bytes_in_buffer;
  
  vpts = gAudioESD.sync_vpts - diff * 1024 / gAudioESD.bytes_per_kpts;

//  xprintf (AUDIO|VERBOSE,"audio_esd_out: get_current_vpts pos=%d diff=%d vpts=%d sync_vpts=%d\n",
//	   pos, diff, vpts, gAudioESD.sync_vpts);

  return vpts;
}

static void ao_fill_gap (uint32_t pts_len) {

  int num_bytes = pts_len * gAudioESD.bytes_per_kpts / 1024;
  
  num_bytes = (num_bytes / 4) * 4;

  printf ("audio_esd_out: inserting %d 0-bytes to fill a gap of %d pts\n",num_bytes, pts_len);
  
  gAudioESD.bytes_in_buffer += num_bytes;
  
  while (num_bytes>0) {
    if (num_bytes>8192) {
      write(gAudioESD.audio_fd, gAudioESD.zero_space, 8192);
      num_bytes -= 8192;
    } else {
      write(gAudioESD.audio_fd, gAudioESD.zero_space, num_bytes);
      num_bytes = 0;
    }
  }
  
  gAudioESD.last_vpts += pts_len;
}

static void ao_write_audio_data(int16_t* output_samples, uint32_t num_samples, 
				uint32_t pts_)
{

  uint32_t vpts,
           audio_vpts,
           master_vpts;
  int32_t  diff, gap;
  int      bDropPackage;
  uint16_t sample_buffer[8192];

  
  if (gAudioESD.audio_fd<0)
    return;

  vpts        = metronom_got_audio_samples (pts_, num_samples);

  xprintf (VERBOSE|AUDIO, "audio_esd_out: got %d samples, vpts=%d, last_vpts=%d\n",
	   num_samples, vpts, gAudioESD.last_vpts);

  /*
   * check if these samples "fit" in the audio output buffer
   * or do we have an audio "gap" here?
   */
  
  gap = vpts - gAudioESD.last_vpts ;
  
  /*
    printf ("audio_esd_out: gap = %d - %d + %d = %d\n",
    vpts, gAudioESD.last_vpts, diff, gap);
  */

  bDropPackage = 0;
  
  if (gap>GAP_TOLERANCE) {
    ao_fill_gap (gap);
  } else if (gap<-GAP_TOLERANCE) {
    bDropPackage = 1;
  }

  /*
   * sync on master clock
   */

  audio_vpts  = ao_get_current_vpts () ;
  master_vpts = metronom_get_current_time ();
  diff        = audio_vpts - master_vpts;

  xprintf (AUDIO|VERBOSE, "audio_esd_out: syncing on master clock: audio_vpts=%d master_vpts=%d\n",
	   audio_vpts, master_vpts);
  /*
  printf ("audio_esd_out: audio_vpts=%d <=> master_vpts=%d (diff=%d)\n",
	  audio_vpts, master_vpts, diff);
  */

  /*
   * adjust master clock
   */

  if (abs(diff)>MAX_MASTER_CLOCK_DIV) {
    printf ("master clock adjust time %d -> %d (diff: %d)\n", master_vpts, audio_vpts, diff); 
    metronom_adjust_clock (audio_vpts); 
  }

  /*
   * resample and output samples
   */

  if (!bDropPackage) {
    int num_output_samples = num_samples * (gAudioESD.output_sample_rate + gAudioESD.output_rate_correction) / gAudioESD.input_sample_rate;


    audio_out_resample_stereo (output_samples, num_samples,
			       sample_buffer, num_output_samples);
    
    write(gAudioESD.audio_fd, sample_buffer, num_output_samples * 2 * gAudioESD.num_channels);

    xprintf (AUDIO|VERBOSE, "audio_esd_out :audio package written\n");
    
    /*
     * remember vpts
     */
    
    gAudioESD.sync_vpts            = vpts;
    gAudioESD.sync_bytes_in_buffer = gAudioESD.bytes_in_buffer;

    /*
     * step values
     */
    
    gAudioESD.bytes_in_buffer += num_output_samples * 2 * gAudioESD.num_channels;
    gAudioESD.audio_started    = 1;
  } else {
    printf ("audio_esd_out: audio package (vpts = %d) dropped\n", vpts);
    gAudioESD.sync_vpts            = vpts;
  }
  
  gAudioESD.last_vpts        = vpts + num_samples * 90000 / gAudioESD.input_sample_rate ; 
}


static void ao_close(void)
{
  close(gAudioESD.audio_fd);
  gAudioESD.audio_fd = -1;
}

static int ao_is_mode_supported (int mode) {
  return ((mode == AO_MODE_STEREO) || (mode == AO_MODE_MONO));
}

static ao_functions_t audio_esdout = {
  ao_is_mode_supported,
  ao_open,
  ao_write_audio_data,
  ao_close,
};


ao_functions_t *audio_esdout_init (void) 
{
  int audio_fd;

  /*
   * open stream to ESD server
   */

  xprintf(VERBOSE|AUDIO, "Connecting to ESD server...");
  audio_fd = esd_open_sound(NULL);

  if(audio_fd < 0) 
  {
    char *server = getenv("ESPEAKER");

    // print a message so the user knows why ESD failed
    printf("Can't connect to %s ESD server: %s\n",
	   server ? server : "local", strerror(errno));

    return NULL;
  } // else
//    xprintf(VERBOSE|AUDIO, " %s\n", gAudioESD.audio_dev);

  close(audio_fd);

  gAudioESD.output_sample_rate = 0;
  gAudioESD.zero_space = xmalloc (8192);

  return &audio_esdout;
}
