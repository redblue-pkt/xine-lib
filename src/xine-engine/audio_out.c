/* 
 * Copyright (C) 2000-2003 the xine project
 * 
 * This file is part of xine, a free video player.
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
 * along with self program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: audio_out.c,v 1.130 2003/06/17 18:53:14 tmattern Exp $
 * 
 * 22-8-2001 James imported some useful AC3 sections from the previous alsa driver.
 *   (c) 2001 Andy Lo A Foe <andy@alsaplayer.org>
 * 20-8-2001 First implementation of Audio sync and Audio driver separation.
 *   (c) 2001 James Courtier-Dutton James@superbug.demon.co.uk
 * 
 * General Programming Guidelines: -
 * New concept of an "audio_frame".
 * An audio_frame consists of all the samples required to fill every 
 * audio channel to a full amount of bits.
 * So, it does not mater how many bits per sample, or how many audio channels 
 * are being used, the number of audio_frames is the same.
 * E.g.  16 bit stereo is 4 bytes, but one frame.
 *       16 bit 5.1 surround is 12 bytes, but one frame.
 * The purpose of this is to make the audio_sync code a lot more readable, 
 * rather than having to multiply by the amount of channels all the time
 * when dealing with audio_bytes instead of audio_frames.
 *
 * The number of samples passed to/from the audio driver is also sent 
 * in units of audio_frames.
 * 
 * Currently, James has tested with OSS: Standard stereo out, SPDIF PCM, SPDIF AC3
 *                                 ALSA: Standard stereo out
 * No testing has been done of ALSA SPDIF AC3 or any 4,5,5.1 channel output.
 * Currently, I don't think resampling functions, as I cannot test it.
 *
 * equalizer based on
 *
 *   PCM time-domain equalizer
 *
 *   Copyright (C) 2002  Felipe Rivera <liebremx at users sourceforge net>
 *
 * heavily modified by guenter bartsch 2003 for use in libxine
 *
 */

#ifndef	__sun
/* required for swab() */
#define _XOPEN_SOURCE 500
#endif
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
#include <inttypes.h>
#include <math.h>

#define XINE_ENABLE_EXPERIMENTAL_FEATURES

#include "xine_internal.h"
#include "xineutils.h"
#include "audio_out.h"
#include "resample.h"
#include "metronom.h"

/*
#define LOG

#define LOG_RESAMPLE_SYNC
*/

#define NUM_AUDIO_BUFFERS       32
#define AUDIO_BUF_SIZE       32768

#define ZERO_BUF_SIZE         5000

/* By adding gap errors (difference between reported and expected
 * sound card clock) into metronom's vpts_offset we can use its 
 * smoothing algorithms to correct sound card clock drifts.
 * obs: previously this error was added to xine scr.
 *
 * audio buf ---> metronom --> audio fifo --> (buf->vpts - hw_vpts)
 *           (vpts_offset + error)                     gap
 *                    <---------- control --------------|
 *
 * Unfortunately audio fifo adds a large delay to our closed loop.
 *
 * The defines below are designed to avoid updating the metronom too fast.
 * - it will only be updated 1 time per second (so it has a chance of
 *   distributing the error for several frames).
 * - it will only be updated 2 times for the whole audio fifo size
 *   length (so the control will wait to see the feedback effect)
 * - each update will be of gap/SYNC_GAP_RATE.
 *
 * Sound card clock correction can only provide smooth playback for
 * errors < 1% nominal rate. For bigger errors (bad streams) audio
 * buffers may be dropped or gaps filled with silence.
 */
#define SYNC_TIME_INVERVAL  (1 * 90000)
#define SYNC_BUF_INTERVAL   NUM_AUDIO_BUFFERS / 2
#define SYNC_GAP_RATE       4

/* Alternative for metronom feedback: fix sound card clock drift
 * by resampling all audio data, so that the sound card keeps in
 * sync with the system clock. This may help, if one uses a DXR3/H+
 * decoder board. Those have their own clock (which serves as xine's
 * master clock) and can only operate at fixed frame rates (if you
 * want smooth playback). Resampling then avoids A/V sync problems,
 * gaps filled with 0-frames and jerky video playback due to different
 * clock speeds of the sound card and DXR3/H+.
 */
#define RESAMPLE_SYNC_WINDOW 50
#define RESAMPLE_MAX_GAP_DIFF 150
#define RESAMPLE_REDUCE_GAP_THRESHOLD 200



typedef struct {
  double   last_factor;
  int      window;
  int      reduce_gap;
  uint64_t window_duration, last_vpts;
  int64_t  recent_gap[8], last_avg_gap;
  int      valid;
} resample_sync_t;

/*
 * equalizer stuff
 */

#define EQ_BANDS    10
#define EQ_CHANNELS  8

#define FP_FRBITS 28	

#define EQ_REAL(x) ((int)((x) * (1 << FP_FRBITS)))

typedef struct  {
  int beta;
  int alpha; 
  int gamma;
} sIIRCoefficients;

/* Coefficient history for the IIR filter */
typedef struct {
  int x[3]; /* x[n], x[n-1], x[n-2] */
  int y[3]; /* y[n], y[n-1], y[n-2] */
}sXYData;


static sIIRCoefficients iir_cf[] = {
  /* 31 Hz*/
  { EQ_REAL(9.9691562441e-01), EQ_REAL(1.5421877947e-03), EQ_REAL(1.9968961468e+00) },
  /* 62 Hz*/
  { EQ_REAL(9.9384077546e-01), EQ_REAL(3.0796122698e-03), EQ_REAL(1.9937629855e+00) },
  /* 125 Hz*/
  { EQ_REAL(9.8774277725e-01), EQ_REAL(6.1286113769e-03), EQ_REAL(1.9874275518e+00) },
  /* 250 Hz*/
  { EQ_REAL(9.7522112569e-01), EQ_REAL(1.2389437156e-02), EQ_REAL(1.9739682661e+00) },
  /* 500 Hz*/
  { EQ_REAL(9.5105628526e-01), EQ_REAL(2.4471857368e-02), EQ_REAL(1.9461077269e+00) },
  /* 1k Hz*/
  { EQ_REAL(9.0450844499e-01), EQ_REAL(4.7745777504e-02), EQ_REAL(1.8852109613e+00) },
  /* 2k Hz*/
  { EQ_REAL(8.1778971701e-01), EQ_REAL(9.1105141497e-02), EQ_REAL(1.7444877599e+00) },
  /* 4k Hz*/
  { EQ_REAL(6.6857185264e-01), EQ_REAL(1.6571407368e-01), EQ_REAL(1.4048592171e+00) },
  /* 8k Hz*/
  { EQ_REAL(4.4861333678e-01), EQ_REAL(2.7569333161e-01), EQ_REAL(6.0518718075e-01) },
  /* 16k Hz*/
  { EQ_REAL(2.4201241845e-01), EQ_REAL(3.7899379077e-01), EQ_REAL(-8.0847117831e-01) },
};
                                   
typedef struct {
 
  xine_audio_port_t    ao; /* public part */

  /* private stuff */
  ao_driver_t         *driver;
  pthread_mutex_t      driver_lock;
  int                  driver_open;
  metronom_clock_t    *clock;
  xine_t              *xine;
  xine_list_t         *streams;
  pthread_mutex_t      streams_lock;

  int             audio_loop_running;
  int             grab_only; /* => do not start thread, frontend will consume samples */
  int             audio_paused;
  pthread_t       audio_thread;

  int64_t         audio_step;           /* pts per 32 768 samples (sample = #bytes/2) */
  int32_t         frames_per_kpts;      /* frames per 1024/90000 sec                  */
  
  ao_format_t     input, output;        /* format conversion done at audio_out.c */
  double          frame_rate_factor;
  double          output_frame_excess;  /* used to keep track of 'half' frames */

  int             av_sync_method_conf;
  resample_sync_t resample_sync_info;
  int             resample_sync_method; /* fix sound card clock drift by resampling */
  double          resample_sync_factor; /* correct buffer length by this factor
                                         * to sync audio hardware to (dxr3) clock */
  int             resample_conf;
  uint32_t        force_rate;           /* force audio output rate to this value if non-zero */
  int             do_resample;
  int             gap_tolerance;
  audio_fifo_t   *free_fifo;
  audio_fifo_t   *out_fifo;
  int64_t         last_audio_vpts;
  
  audio_buffer_t *frame_buf[2];         /* two buffers for "stackable" conversions */
  int16_t        *zero_space;

  int64_t         passthrough_offset;
  int             flush_audio_driver;
  pthread_mutex_t flush_audio_driver_lock;
  pthread_cond_t  flush_audio_driver_reached;
  int             discard_buffers;

  /* some built-in audio filters */

  int             do_compress;
  double          compression_factor;   /* current compression */
  double          compression_factor_max; /* user limit on compression */
  int             do_amp;
  double          amp_factor;

  /* 10-band equalizer */

  int             do_equ;
  int             eq_gain[EQ_BANDS];
  int             eq_preamp;
  int             eq_i;
  int             eq_j;
  int             eq_k;

  sXYData         eq_data_history[EQ_BANDS][EQ_CHANNELS];
  
} aos_t;

struct audio_fifo_s {
  audio_buffer_t    *first;
  audio_buffer_t    *last;
  int                num_buffers;

  pthread_mutex_t    mutex;
  pthread_cond_t     not_empty;
  pthread_cond_t     empty;
};


static audio_fifo_t *fifo_new (void) {

  audio_fifo_t *fifo;

  fifo = (audio_fifo_t *) xine_xmalloc (sizeof (audio_fifo_t));

  if (!fifo) {
    printf ("audio_out: out of memory!\n");
    return NULL;
  }

  fifo->first       = NULL;
  fifo->last        = NULL;
  fifo->num_buffers = 0;
  pthread_mutex_init (&fifo->mutex, NULL);
  pthread_cond_init  (&fifo->not_empty, NULL);
  pthread_cond_init  (&fifo->empty, NULL);

  return fifo;
}

static void fifo_append_int (audio_fifo_t *fifo,
			     audio_buffer_t *buf) {

  /* buf->next = NULL; */

  XINE_ASSERT(!buf->next, "Next audio buffer is not NULL.");

  if (!fifo->first) {
    fifo->first       = buf;
    fifo->last        = buf;
    fifo->num_buffers = 1;

  } else {

    fifo->last->next = buf;
    fifo->last       = buf;
    fifo->num_buffers++;

  }
  pthread_cond_signal (&fifo->not_empty);
}

static void fifo_append (audio_fifo_t *fifo,
			 audio_buffer_t *buf) {

  pthread_mutex_lock (&fifo->mutex);
  fifo_append_int (fifo, buf);
  pthread_mutex_unlock (&fifo->mutex);
}

static audio_buffer_t *fifo_remove_int (audio_fifo_t *fifo) {
  audio_buffer_t *buf;

  while (!fifo->first) {
    pthread_cond_signal (&fifo->empty);
    pthread_cond_wait (&fifo->not_empty, &fifo->mutex);
  }

  buf = fifo->first;

  if (buf) {
    fifo->first = buf->next;

    if (!fifo->first) {

      fifo->last = NULL;
      fifo->num_buffers = 0;

    } else 
      fifo->num_buffers--;

  }

  buf->next = NULL;
    
  return buf;
}

static audio_buffer_t *fifo_remove (audio_fifo_t *fifo) {

  audio_buffer_t *buf;

  pthread_mutex_lock (&fifo->mutex);
  buf = fifo_remove_int(fifo);
  pthread_mutex_unlock (&fifo->mutex);

  return buf;
}

/* This function is currently not needed */
#if 0
static int fifo_num_buffers (audio_fifo_t *fifo) {

  int ret;

  pthread_mutex_lock (&fifo->mutex);
  ret = fifo->num_buffers;
  pthread_mutex_unlock (&fifo->mutex);

  return ret;
}
#endif

static void fifo_wait_empty (audio_fifo_t *fifo) {

  pthread_mutex_lock (&fifo->mutex);
  pthread_cond_signal (&fifo->not_empty);
  pthread_cond_wait (&fifo->empty, &fifo->mutex);
  pthread_mutex_unlock (&fifo->mutex);
}


static void write_pause_burst(aos_t *this, uint32_t num_frames) { 
  
  int error = 0;
  unsigned char buf[8192];
  unsigned short *sbuf = (unsigned short *)&buf[0];
  
  sbuf[0] = 0xf872;
  sbuf[1] = 0x4e1f;
  
  if (error == 0)
    /* Audio ES Channel empty, wait for DD Decoder or pause */
    sbuf[2] = 0x0003;
  else
    /* user stop, skip or error */
    sbuf[2] = 0x0103;

  sbuf[3] = 0x0020;
  sbuf[4] = 0x0000;
  sbuf[5] = 0x0000;

  memset(&sbuf[6], 0, 6144 - 96);
  while (num_frames > 1536) {
    pthread_mutex_lock( &this->driver_lock );
    this->driver->write(this->driver, sbuf, 1536);
    pthread_mutex_unlock( &this->driver_lock );
    num_frames -= 1536;
  }

}


static void ao_fill_gap (aos_t *this, int64_t pts_len) {

  int num_frames ;

  num_frames = pts_len * this->frames_per_kpts / 1024;

  if (this->xine->verbosity >= XINE_VERBOSITY_LOG)

    printf ("audio_out: inserting %d 0-frames to fill a gap of %" PRId64 " pts\n",
	    num_frames, pts_len);

  if ((this->output.mode == AO_CAP_MODE_A52) || (this->output.mode == AO_CAP_MODE_AC5)) {
    write_pause_burst(this,num_frames);
    return; 
  }

  while (num_frames > 0 && !this->discard_buffers) {
    if (num_frames > ZERO_BUF_SIZE) {
      pthread_mutex_lock( &this->driver_lock );
      this->driver->write(this->driver, this->zero_space, ZERO_BUF_SIZE);
      pthread_mutex_unlock( &this->driver_lock );
      num_frames -= ZERO_BUF_SIZE;
    } else {
      pthread_mutex_lock( &this->driver_lock );
      this->driver->write(this->driver, this->zero_space, num_frames);
      pthread_mutex_unlock( &this->driver_lock );
      num_frames = 0;
    }
  }
}

static void ensure_buffer_size (audio_buffer_t *buf, int bytes_per_frame,
                                int frames)
{
  int size = bytes_per_frame * frames;
  
  if (buf->mem_size < size) {
    buf->mem = realloc( buf->mem, size );
    buf->mem_size = size;
  }
  buf->num_frames = frames;
}

static audio_buffer_t * swap_frame_buffers ( aos_t *this ) {
  audio_buffer_t *tmp;

  tmp = this->frame_buf[1];
  this->frame_buf[1] = this->frame_buf[0];
  this->frame_buf[0] = tmp;
  return this->frame_buf[0];
}

static int mode_channels( int mode ) {
  switch( mode ) {
  case AO_CAP_MODE_MONO:
    return 1;
  case AO_CAP_MODE_STEREO:
    return 2;
  case AO_CAP_MODE_4CHANNEL:
    return 4;
  case AO_CAP_MODE_5CHANNEL:
    return 5;
  case AO_CAP_MODE_5_1CHANNEL:
    return 6;
  }
  return 0;
} 

static void audio_filter_compress (aos_t *this, int16_t *mem, int num_frames) {

  int    i, maxs;
  double f_max;
  int    num_channels;

  num_channels = mode_channels (this->input.mode);
  if (!num_channels)
    return;

  maxs = 0;
    
  /* measure */

  for (i=0; i<num_frames*num_channels; i++) {
    int16_t sample = abs(mem[i]);
    if (sample>maxs)
      maxs = sample;
  }

  /* calc maximum possible & allowed factor */

  if (maxs>0) {
    f_max = 32767.0 / maxs;
    this->compression_factor = this->compression_factor * 0.999 + f_max * 0.001;
    if (this->compression_factor > f_max)
      this->compression_factor = f_max;
    
    if (this->compression_factor > this->compression_factor_max)
      this->compression_factor = this->compression_factor_max;
  } else
    f_max = 1.0;
  
#ifdef LOG
  printf ("audio_out: max=%d f_max=%f compression_factor=%f\n", 
	  maxs, f_max, this->compression_factor);
#endif
    
  /* apply it */

  for (i=0; i<num_frames*num_channels; i++) {

    /* 0.98 to avoid overflow */

    mem[i] = mem[i] * 0.98 * this->compression_factor * this->amp_factor;
  }
}

static void audio_filter_amp (aos_t *this, int16_t *mem, int num_frames) {

  int    i;
  int    num_channels;

  num_channels = mode_channels (this->input.mode);
  if (!num_channels)
    return;

  for (i=0; i<num_frames*num_channels; i++) {

    mem[i] = mem[i] * this->amp_factor;
  }
}

static void audio_filter_equalize (aos_t *this, 
				   int16_t *data, int num_frames) {
  int       index, band, channel;
  int       halflength, length;
  int       out[EQ_CHANNELS], scaledpcm[EQ_CHANNELS];
  int64_t l;
  int       num_channels;

  num_channels = mode_channels (this->input.mode);
  if (!num_channels)
    return;

  halflength = num_frames * 2;
  length = num_frames * 4;

  for (index = 0; index < halflength; index+=2) {

    for (channel = 0; channel < num_channels; channel++) {
      
      /* Convert the PCM sample to a fixed fraction */
      scaledpcm[channel] = ((int)data[index+channel]) << (FP_FRBITS-16-1);
      
      out[channel] = 0;
      /*  For each band */
      for (band = 0; band < EQ_BANDS; band++) {

	this->eq_data_history[band][channel].x[this->eq_i] = scaledpcm[channel];
	l = (int64_t)iir_cf[band].alpha * (int64_t)(this->eq_data_history[band][channel].x[this->eq_i] - this->eq_data_history[band][channel].x[this->eq_k])
	  + (int64_t)iir_cf[band].gamma * (int64_t)this->eq_data_history[band][channel].y[this->eq_j]
	  - (int64_t)iir_cf[band].beta * (int64_t)this->eq_data_history[band][channel].y[this->eq_k]; 
	this->eq_data_history[band][channel].y[this->eq_i] = (int)(l >> FP_FRBITS);
	l = (int64_t)this->eq_data_history[band][channel].y[this->eq_i] * (int64_t)this->eq_gain[band];
	out[channel] +=	(int)(l >> FP_FRBITS);
      } 

      /*  Volume scaling adjustment by 2^-2 */
      out[channel] += (scaledpcm[channel] >> 2);
      
      /* Adjust the fixed point fraction value to a PCM sample */
      /* Scale back to a 16bit signed int */
      out[channel] >>= (FP_FRBITS-16);

      /* Limit the output */
      if (out[channel] < -32768)
	data[index+channel] = -32768;
      else if (out[channel] > 32767)
	data[index+channel] = 32767;
      else
	data[index+channel] = out[channel];
    } 
		
    this->eq_i++; this->eq_j++; this->eq_k++;
    if (this->eq_i == 3) this->eq_i = 0;
    else if (this->eq_j == 3) this->eq_j = 0;
    else this->eq_k = 0;
  }

}

static audio_buffer_t* prepare_samples( aos_t *this, audio_buffer_t *buf) {
  double          acc_output_frames;
  int             num_output_frames ;

  /*
   * volume / compressor / equalizer filter
   */

  if (this->input.bits == 16) {

    if (this->do_equ) 
      audio_filter_equalize (this, buf->mem, buf->num_frames);
    if (this->do_compress)
      audio_filter_compress (this, buf->mem, buf->num_frames);
    if (this->do_amp) 
      audio_filter_amp (this, buf->mem, buf->num_frames);
  }


  /*
   * resample and output audio data
   */

  /* calculate number of output frames (after resampling) */
  acc_output_frames = (double) buf->num_frames * this->frame_rate_factor
    * this->resample_sync_factor + this->output_frame_excess;

  /* Truncate to an integer */
  num_output_frames = acc_output_frames;
    
  /* Keep track of the amount truncated */
  this->output_frame_excess = acc_output_frames - (double) num_output_frames;
  if ( this->output_frame_excess != 0 &&
       !this->do_resample && !this->resample_sync_method)
    this->output_frame_excess = 0;
      
#ifdef LOG
  printf ("audio_out: outputting %d frames\n", num_output_frames);
#endif

  /* convert 8 bit samples as needed */
  if ( this->input.bits == 8 &&
       (this->resample_sync_method || this->do_resample || 
        this->output.bits != 8 || this->input.mode != this->output.mode) ) {
    ensure_buffer_size(this->frame_buf[1], 2*mode_channels(this->input.mode),
		       buf->num_frames );
    audio_out_resample_8to16((int8_t *)buf->mem, this->frame_buf[1]->mem,
			     mode_channels(this->input.mode) * buf->num_frames );
    buf = swap_frame_buffers(this);
  }

  /* check if resampling may be skipped */
  if ( (this->resample_sync_method || this->do_resample) &&  
       buf->num_frames != num_output_frames ) {
    switch (this->input.mode) {
    case AO_CAP_MODE_MONO:
      ensure_buffer_size(this->frame_buf[1], 2, num_output_frames);
      audio_out_resample_mono (buf->mem, buf->num_frames,
			       this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_STEREO:
      ensure_buffer_size(this->frame_buf[1], 4, num_output_frames);
      audio_out_resample_stereo (buf->mem, buf->num_frames,
				 this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_4CHANNEL:
      ensure_buffer_size(this->frame_buf[1], 8, num_output_frames);
      audio_out_resample_4channel (buf->mem, buf->num_frames,
				   this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_5CHANNEL:
      ensure_buffer_size(this->frame_buf[1], 10, num_output_frames);
      audio_out_resample_5channel (buf->mem, buf->num_frames,
				   this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_5_1CHANNEL:
      ensure_buffer_size(this->frame_buf[1], 12, num_output_frames);
      audio_out_resample_6channel (buf->mem, buf->num_frames,
				   this->frame_buf[1]->mem, num_output_frames);
      buf = swap_frame_buffers(this);
      break;
    case AO_CAP_MODE_A52:
    case AO_CAP_MODE_AC5:
      /* pass-through modes: no resampling */
      break;
    }
  }
    
  /* mode conversion */
  if ( this->input.mode != this->output.mode ) {
    switch (this->input.mode) {
    case AO_CAP_MODE_MONO:
      if( this->output.mode == AO_CAP_MODE_STEREO ) {
	ensure_buffer_size(this->frame_buf[1], 4, buf->num_frames );
	audio_out_resample_monotostereo(buf->mem, this->frame_buf[1]->mem,
					buf->num_frames );
	buf = swap_frame_buffers(this);
      }
      break;
    case AO_CAP_MODE_STEREO:
      if( this->output.mode == AO_CAP_MODE_MONO ) {
	ensure_buffer_size(this->frame_buf[1], 2, buf->num_frames );
	audio_out_resample_stereotomono(buf->mem, this->frame_buf[1]->mem,
					buf->num_frames );
	buf = swap_frame_buffers(this);
      }
      break;
    case AO_CAP_MODE_4CHANNEL:
      break;
    case AO_CAP_MODE_5CHANNEL:
      break;
    case AO_CAP_MODE_5_1CHANNEL:
      break;
    case AO_CAP_MODE_A52:
    case AO_CAP_MODE_AC5:
      break;
    }
  }

  /* convert back to 8 bits after resampling */
  if( this->output.bits == 8 && (this->resample_sync_method || this->do_resample ||
				 this->input.mode != this->output.mode) ) {
    ensure_buffer_size(this->frame_buf[1], 1*mode_channels(this->output.mode),
		       buf->num_frames );
    audio_out_resample_16to8(buf->mem, (int8_t *)this->frame_buf[1]->mem,
			     mode_channels(this->output.mode) * buf->num_frames );
    buf = swap_frame_buffers(this);
  }
  return buf;
}


static int resample_rate_adjust(aos_t *this, int64_t gap, audio_buffer_t *buf) {

  /* Calculates the drift factor used to resample the audio data to
   * keep in sync with system (or dxr3) clock.
   *
   * To compensate the sound card drift it is necessary to know, how many audio
   * frames need to be added (or removed) via resampling. This function waits for
   * RESAMPLE_SYNC_WINDOW audio buffers to be sent to the card and keeps track
   * of their total duration in vpts. With the measured gap difference between
   * the reported gap values at the beginning and at the end of this window the
   * required resampling factor is calculated:
   *
   * resample_factor = (duration + gap_difference) / duration
   *
   * This factor is then used in prepare_samples() to resample the audio
   * buffers as needed so we keep in sync with the system (or dxr3) clock.
   */

  resample_sync_t *info = &this->resample_sync_info;
  int64_t avg_gap = 0;
  double factor;
  double diff;
  double duration;
  int i;

  if (abs(gap) > AO_MAX_GAP) {
    /* drop buffers or insert 0-frames in audio out loop */
    info->valid = 0;
    return -1;
  }

  if ( ! info->valid) {
    this->resample_sync_factor = 1.0;
    info->window = 0;
    info->reduce_gap = 0;
    info->last_avg_gap = gap;
    info->last_factor = 0;
    info->window_duration = info->last_vpts = 0;
    info->valid = 1;
  }

  /* calc average gap (to compensate small errors during measurement) */
  for (i = 0; i < 7; i++) info->recent_gap[i] = info->recent_gap[i + 1];
  info->recent_gap[i] = gap;
  for (i = 0; i < 8; i++) avg_gap += info->recent_gap[i];
  avg_gap /= 8;


  /* gap too big? Change sample rate so that gap converges towards 0. */

  if (abs(avg_gap) > RESAMPLE_REDUCE_GAP_THRESHOLD && !info->reduce_gap) {
    info->reduce_gap = 1;
    this->resample_sync_factor = (avg_gap < 0) ? 0.995 : 1.005;

#ifdef LOG_RESAMPLE_SYNC
    printf("audio_out: sample rate adjusted to reduce gap: gap=%" PRId64 "\n", avg_gap);
#endif
    return 0;

  } else if (info->reduce_gap && abs(avg_gap) < 50) {
    info->reduce_gap = 0;
    info->valid = 0;
#ifdef LOG_RESAMPLE_SYNC
    printf("audio_out: gap successfully reduced\n");
#endif
    return 0;

  } else if (info->reduce_gap) {
    /* re-check, because the gap might suddenly change its sign,
     * also slow down, when getting close to zero (-300<gap<300) */
    if (abs(avg_gap) > 300)
      this->resample_sync_factor = (avg_gap < 0) ? 0.995 : 1.005;
    else
      this->resample_sync_factor = (avg_gap < 0) ? 0.998 : 1.002;
    return 0;
  }


  if (info->window > RESAMPLE_SYNC_WINDOW) {

    /* adjust drift correction */

    int64_t gap_diff = avg_gap - info->last_avg_gap;

    if (gap_diff < RESAMPLE_MAX_GAP_DIFF) {
#ifdef LOG_RESAMPLE_SYNC
      int num_frames;
      
      /* if we are already resampling to a different output rate, consider
       * this during calculation */
      num_frames = (this->do_resample) ? (buf->num_frames * this->frame_rate_factor)
        : buf->num_frames;
      printf("audio_out: gap=%5" PRId64 ";  gap_diff=%5" PRId64 ";  frame_diff=%3.0f;  drift_factor=%f\n",
             avg_gap, gap_diff, num_frames * info->window * info->last_factor,
             this->resample_sync_factor);
#endif
      /* we want to add factor * num_frames to each buffer */
      diff = gap_diff;
      duration = (int64_t)info->window_duration + (int64_t)info->last_factor;
      factor = diff / duration;

      info->last_factor = factor;
      this->resample_sync_factor = 1.0 + factor;

      info->last_avg_gap = avg_gap;
      info->window_duration = 0;
      info->window = 0;
    } else
      info->valid = 0;

  } else {

    /* collect data for next adjustment */
    if (info->window > 0)
      info->window_duration += buf->vpts - info->last_vpts;
    info->last_vpts = buf->vpts;
    info->window++;
  } 

  return 0;
}


/* Audio output loop: -
 * 1) Check for pause. 
 * 2) Make sure audio hardware is in RUNNING state.
 * 3) Get delay
 * 4) Do drop, 0-fill or output samples.
 * 5) Go round loop again.
 */
static void *ao_loop (void *this_gen) {

  aos_t *this = (aos_t *) this_gen;
  int64_t         hw_vpts;
  audio_buffer_t *in_buf, *out_buf;
  int64_t         gap;
  int64_t         delay;
  int64_t         cur_time;
  int64_t         last_sync_time;
  int             bufs_since_sync;

  last_sync_time = bufs_since_sync = 0;
  in_buf = NULL;

  while ((this->audio_loop_running) ||
	 (!this->audio_loop_running && this->out_fifo->first)) {

    /*
     * get buffer to process for this loop iteration
     */
    
    if (!in_buf) {

#ifdef LOG
      printf ("audio_out:loop: get buf from fifo\n");
#endif
      in_buf = fifo_remove (this->out_fifo);
      bufs_since_sync++;
#ifdef LOG
      printf ("audio_out: got a buffer\n");
#endif
    }

    pthread_mutex_lock(&this->flush_audio_driver_lock);
    if (this->flush_audio_driver) {
      this->ao.control(&this->ao, AO_CTRL_FLUSH_BUFFERS);
      this->flush_audio_driver = 0;
      pthread_cond_signal(&this->flush_audio_driver_reached);
    }

    if (this->discard_buffers) {
      fifo_append (this->free_fifo, in_buf);
      in_buf = NULL;
      pthread_mutex_unlock(&this->flush_audio_driver_lock);
      continue;
    }
    pthread_mutex_unlock(&this->flush_audio_driver_lock);
    

    /* 
     * wait until user unpauses stream
     * audio_paused == 1 means we are playing at a different speed
     * them we must process buffers otherwise the entire engine will stop.
     */
    
    if ( this->audio_paused && this->audio_loop_running )  {

      if (this->audio_paused == 1) {

	cur_time = this->clock->get_current_time (this->clock);
	if (in_buf->vpts < cur_time ) {
#ifdef LOG
	  printf ("audio_out:loop: next fifo\n");
#endif
	  fifo_append (this->free_fifo, in_buf);
	  in_buf = NULL;
	  continue;
	}
      }

#ifdef LOG
      printf ("audio_out:loop:pause: I feel sleepy (%d buffers).\n", this->out_fifo->num_buffers);
#endif
      xine_usec_sleep (10000);
#ifdef LOG
      printf ("audio_out:loop:pause: I wake up.\n");
#endif
      continue;
    }


    pthread_mutex_lock( &this->driver_lock );
    if(this->driver_open) {
      delay = this->driver->delay(this->driver);
      while (delay < 0 && this->audio_loop_running) {
        /* Get the audio card into RUNNING state. */
        pthread_mutex_unlock( &this->driver_lock ); 
        ao_fill_gap (this, 10000); /* FIXME, this PTS of 1000 should == period size */
        pthread_mutex_lock( &this->driver_lock ); 
        delay = this->driver->delay(this->driver);
      }
    } else 
      delay = 0;
    pthread_mutex_unlock( &this->driver_lock ); 

    cur_time = this->clock->get_current_time (this->clock);  
    
    /* we update current_extra_info if either there is no video stream that could do that
     * or if the current_extra_info is getting too much out of date */
    if( in_buf && in_buf->stream && (!in_buf->stream->video_decoder_plugin ||
        (cur_time - in_buf->stream->current_extra_info->vpts) > 30000 )) {

      pthread_mutex_lock( &in_buf->stream->current_extra_info_lock );
      extra_info_merge( in_buf->stream->current_extra_info, in_buf->extra_info );
      pthread_mutex_unlock( &in_buf->stream->current_extra_info_lock );
    }
    
    /*
     * where, in the timeline is the "end" of the 
     * hardware audio buffer at the moment?
     */

    hw_vpts = cur_time;
  
#ifdef LOG
    printf ("audio_out: current delay is %" PRId64 ", current time is %" PRId64 "\n",
	      delay, cur_time);
#endif
    /* External A52 decoder delay correction */
    if ((this->output.mode==AO_CAP_MODE_A52) || (this->output.mode==AO_CAP_MODE_AC5)) 
      delay += this->passthrough_offset;

    if(this->frames_per_kpts)
      hw_vpts += (delay * 1024) / this->frames_per_kpts;
  
    /*
     * calculate gap:
     */
    gap = in_buf->vpts - hw_vpts;
#ifdef LOG
    printf ("audio_out: hw_vpts : %" PRId64 " buffer_vpts : %" PRId64 " gap : %" PRId64 "\n",
	    hw_vpts, in_buf->vpts, gap);
#endif

    if (this->resample_sync_method) {
      /* Correct sound card drift via resampling. If gap is too big to
       * be corrected this way, we use the fallback: drop/insert frames.
       * This function only calculates the drift correction factor. The
       * actual resampling is done by prepare_samples().
       */
      resample_rate_adjust(this, gap, in_buf);
    } else {
      this->resample_sync_factor = 1.0;
    }

    /*
     * output audio data synced to master clock
     */
    /* pthread_mutex_lock( &this->driver_lock ); */

    if (gap < (-1 * AO_MAX_GAP) || !in_buf->num_frames ) {

      /* drop package */
#ifdef LOG
      printf ("audio_out:loop: drop package, next fifo\n");
#endif
      fifo_append (this->free_fifo, in_buf);

#ifdef LOG
      printf ("audio_out: audio package (vpts = %" PRId64 ", gap = %" PRId64 ") dropped\n", 
	      in_buf->vpts, gap);
#endif
      in_buf = NULL;

      
      /* for small gaps ( tolerance < abs(gap) < AO_MAX_GAP ) 
       * feedback them into metronom's vpts_offset (when using
       * metronom feedback for A/V sync)
       */
    } else if ( abs(gap) < AO_MAX_GAP && abs(gap) > this->gap_tolerance &&
                cur_time > (last_sync_time + SYNC_TIME_INVERVAL) && 
                bufs_since_sync >= SYNC_BUF_INTERVAL &&
                !this->resample_sync_method ) {
	xine_stream_t *stream;
#ifdef LOG
        printf ("audio_out: audio_loop: ADJ_VPTS\n"); 
#endif
	pthread_mutex_lock(&this->streams_lock);
	for (stream = xine_list_first_content(this->streams); stream;
	     stream = xine_list_next_content(this->streams)) {
	  stream->metronom->set_option(stream->metronom, METRONOM_ADJ_VPTS_OFFSET,
                                       -gap/SYNC_GAP_RATE );
          last_sync_time = cur_time;
          bufs_since_sync = 0;
	}
	pthread_mutex_unlock(&this->streams_lock);

    } else if ( gap > AO_MAX_GAP ) {
      /* for big gaps output silence */
      ao_fill_gap (this, gap);
    } else {
#if 0
      {
        int count;
        printf("Audio data\n");
        for (count=0;count < 10;count++) {
          printf("%x ",buf->mem[count]);
        }
        printf("\n");
      }
#endif
      out_buf = prepare_samples (this, in_buf);
#if 0
      {
        int count;
        printf("Audio data2\n");
        for (count=0;count < 10;count++) {
          printf("%x ",out_buf->mem[count]);
        }
        printf("\n");
      }
#endif

#ifdef LOG
      printf ("audio_out: loop: writing %d samples to sound device\n", 
	      out_buf->num_frames);
#endif

      pthread_mutex_lock( &this->driver_lock );
      this->driver->write (this->driver, out_buf->mem, out_buf->num_frames );
      pthread_mutex_unlock( &this->driver_lock ); 

#ifdef LOG
      printf ("audio_out:loop: next buf from fifo\n");
#endif
      fifo_append (this->free_fifo, in_buf);
      in_buf = NULL;
    }
    /* pthread_mutex_unlock( &this->driver_lock ); */
  }

  if (in_buf)
    fifo_append (this->free_fifo, in_buf);

  return NULL;
}

/*
 * public a/v processing interface
 */

int xine_get_next_audio_frame (xine_audio_port_t *this_gen,
			       xine_audio_frame_t *frame) {

  aos_t          *this = (aos_t *) this_gen;
  audio_buffer_t *in_buf, *out_buf;
  xine_stream_t  *stream;

#ifdef LOG
  printf ("audio_audio: get_next_audio_frame\n");
#endif

  do {
    stream = xine_list_first_content(this->streams);
    if (!stream)
      xine_usec_sleep (1000);
  } while( !stream );
  
  pthread_mutex_lock (&this->out_fifo->mutex);

  in_buf = this->out_fifo->first;

  /* FIXME: ugly, use conditions and locks instead */

  while (!in_buf
	 && (stream->demux_plugin->get_status (stream->demux_plugin)==DEMUX_OK)) {

    pthread_mutex_unlock(&this->out_fifo->mutex);
    xine_usec_sleep (1000);
    pthread_mutex_lock(&this->out_fifo->mutex);

    in_buf = this->out_fifo->first;
  }

  if (!in_buf) {
    pthread_mutex_unlock(&this->out_fifo->mutex);
    printf ("audio_audio: EOS\n");
    return 0;
  }

  in_buf = fifo_remove_int (this->out_fifo);
  pthread_mutex_unlock(&this->out_fifo->mutex);

  out_buf = prepare_samples (this, in_buf);

  if (out_buf != in_buf) {
    fifo_append (this->free_fifo, in_buf);
    frame->xine_frame = NULL;
  } else
    frame->xine_frame    = out_buf;

  frame->vpts            = out_buf->vpts;
  frame->num_samples     = out_buf->num_frames;
  frame->sample_rate     = this->input.rate;
  frame->num_channels    = mode_channels (this->input.mode);
  frame->bits_per_sample = this->input.bits;
  frame->pos_stream      = out_buf->extra_info->input_pos;
  frame->pos_time        = out_buf->extra_info->input_time;
  frame->data            = (uint8_t *) out_buf->mem;

  return 1;
}

void xine_free_audio_frame (xine_audio_port_t *this_gen, xine_audio_frame_t *frame) {

  aos_t          *this = (aos_t *) this_gen;
  audio_buffer_t *buf;

  buf = (audio_buffer_t *) frame->xine_frame;

  if (buf)
    fifo_append (this->free_fifo, buf);
}


static int ao_change_settings(aos_t *this, uint32_t bits, uint32_t rate, int mode) {
  int output_sample_rate;

  if (this->audio_loop_running) {
    /* make sure there are no more buffers on queue */
    fifo_wait_empty(this->out_fifo);
  }
  
  pthread_mutex_lock( &this->driver_lock );
  if(this->driver_open)
    this->driver->close(this->driver);  
  this->driver_open = 0;
  pthread_mutex_unlock( &this->driver_lock );
 
  this->input.mode            = mode;
  this->input.rate            = rate;
  this->input.bits            = bits;

  if (!this->grab_only) {
    /* not all drivers/cards support 8 bits */
    if( this->input.bits == 8 && 
	!(this->driver->get_capabilities(this->driver) & AO_CAP_8BITS) ) {
      bits = 16;
      if (this->xine->verbosity >= XINE_VERBOSITY_LOG)
        printf("audio_out: 8 bits not supported by driver, converting to 16 bits.\n");
    }
    
    /* provide mono->stereo and stereo->mono conversions */
    if( this->input.mode == AO_CAP_MODE_MONO && 
	!(this->driver->get_capabilities(this->driver) & AO_CAP_MODE_MONO) ) {
      mode = AO_CAP_MODE_STEREO;
      if (this->xine->verbosity >= XINE_VERBOSITY_LOG)
        printf("audio_out: mono not supported by driver, converting to stereo.\n");
    }
    if( this->input.mode == AO_CAP_MODE_STEREO && 
	!(this->driver->get_capabilities(this->driver) & AO_CAP_MODE_STEREO) ) {
      mode = AO_CAP_MODE_MONO;
      if (this->xine->verbosity >= XINE_VERBOSITY_LOG)
        printf("audio_out: stereo not supported by driver, converting to mono.\n");
    }
 
    pthread_mutex_lock( &this->driver_lock );
    output_sample_rate=this->driver->open(this->driver,bits,(this->force_rate ? this->force_rate : rate),mode);
    this->driver_open = 1;
    pthread_mutex_unlock( &this->driver_lock );
  } else
    output_sample_rate = this->input.rate;

  if ( output_sample_rate == 0) {
    if (this->xine->verbosity >= XINE_VERBOSITY_LOG)
      printf("audio_out: open failed!\n");
    return 0;
  }; 

  if (this->xine->verbosity >= XINE_VERBOSITY_LOG)
    printf("audio_out: output sample rate %d\n", output_sample_rate);

  this->last_audio_vpts       = 0;
  this->output.mode           = mode;
  this->output.rate           = output_sample_rate;
  this->output.bits           = bits;

  switch (this->resample_conf) {
  case 1: /* force off */
    this->do_resample = 0;
    break;
  case 2: /* force on */
    this->do_resample = 1;
    break;
  default: /* AUTO */
    this->do_resample = this->output.rate != this->input.rate;
  }

  if (this->do_resample && this->xine->verbosity >= XINE_VERBOSITY_DEBUG)
    printf("audio_out: will resample audio from %d to %d\n",
	   this->input.rate, this->output.rate);

  this->frame_rate_factor = ((double)(this->output.rate)) / ((double)(this->input.rate));
  /* FIXME: If this->frames_per_kpts line goes after this->audio_step line, xine crashes with FPE, when compiled with gcc 3.0.1!!! Why? */ 
  this->frames_per_kpts   = (this->output.rate * 1024) / 90000;
  this->audio_step        = ((int64_t)90000 * (int64_t)32768) / (int64_t)this->input.rate;
#ifdef LOG
  printf ("audio_out : audio_step %" PRId64 " pts per 32768 frames\n", this->audio_step);
#endif

  return this->output.rate;
}

/*
 * open the audio device for writing to
 */

static int ao_open(xine_audio_port_t *this_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {
 
  aos_t *this = (aos_t *) this_gen;

  if( !this->driver_open || bits != this->input.bits || rate != this->input.rate || mode != this->input.mode )
    if( !ao_change_settings(this, bits, rate, mode) )
      return 0;
  
  pthread_mutex_lock(&this->streams_lock);
  xine_list_append_content(this->streams, stream);
  pthread_mutex_unlock(&this->streams_lock);
  
  /* 
   * set metainfo
   */
  stream->stream_info[XINE_STREAM_INFO_AUDIO_MODE] = mode;
  switch (mode) {
  case AO_CAP_MODE_MONO:
    stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = 1;
    break;
  case AO_CAP_MODE_STEREO:
    stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = 2;
    break;
  case AO_CAP_MODE_4CHANNEL:
    stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = 4;
    break;
  case AO_CAP_MODE_5CHANNEL:
    stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = 5;
    break;
  case AO_CAP_MODE_5_1CHANNEL:
    stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = 6;
    break;
  case AO_CAP_MODE_A52:
  case AO_CAP_MODE_AC5:
  default:
    stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] = 255; /* unknown */
  }
  
  stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS]       = bits;
  stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] = rate;

  stream->metronom->set_audio_rate(stream->metronom, this->audio_step);

  return this->output.rate;
}

static audio_buffer_t *ao_get_buffer (xine_audio_port_t *this_gen) {

  aos_t *this = (aos_t *) this_gen;
  audio_buffer_t *buf;
   
  buf = fifo_remove (this->free_fifo);
  extra_info_reset( buf->extra_info );
  
  return buf;
}

static void ao_put_buffer (xine_audio_port_t *this_gen, 
                           audio_buffer_t *buf, xine_stream_t *stream) {

  aos_t *this = (aos_t *) this_gen;
  int64_t pts;

  if (buf->num_frames == 0) {
    fifo_append (this->free_fifo, buf);
    return;
  }

  /* change driver's settings if needed */
  if( stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] != this->input.bits ||
      stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] != this->input.rate ||
      stream->stream_info[XINE_STREAM_INFO_AUDIO_MODE] != this->input.mode ) {
    printf("audio_out: audio format have changed\n");
    ao_change_settings(this, 
                       stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS],
                       stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE],
                       stream->stream_info[XINE_STREAM_INFO_AUDIO_MODE]);
  }
  
  buf->stream = stream;
  extra_info_merge( buf->extra_info, stream->audio_decoder_extra_info );
  
  pts = buf->vpts;

  buf->vpts = stream->metronom->got_audio_samples (stream->metronom, pts, 
						   buf->num_frames);
  buf->extra_info->vpts = buf->vpts;
         
#ifdef LOG
  printf ("audio_out: ao_put_buffer, pts=%" PRId64 ", vpts=%" PRId64 ", flushmode=%d\n",
	  pts, buf->vpts, this->discard_buffers);
#endif
 
  if (!this->discard_buffers) 
    fifo_append (this->out_fifo, buf);
  else
    fifo_append (this->free_fifo, buf);
  
  this->last_audio_vpts = buf->vpts;

#ifdef LOG
  printf ("audio_out: ao_put_buffer done\n");
#endif
}

static void ao_close(xine_audio_port_t *this_gen, xine_stream_t *stream) {

  aos_t *this = (aos_t *) this_gen;
  xine_stream_t *cur;

  if (this->xine->verbosity >= XINE_VERBOSITY_DEBUG)
    printf ("audio_out: ao_close \n");

  /* unregister stream */
  pthread_mutex_lock(&this->streams_lock);
  for (cur = xine_list_first_content(this->streams); cur;
       cur = xine_list_next_content(this->streams))
    if (cur == stream) {
      xine_list_delete_current(this->streams);
      break;
    }
  cur = xine_list_first_content(this->streams);    
  pthread_mutex_unlock(&this->streams_lock);

  /* close driver if no streams left */
  if (!cur && !this->grab_only) {
    if (this->xine->verbosity >= XINE_VERBOSITY_DEBUG)
      printf("audio_out: no streams left, closing driver\n");

    if (this->audio_loop_running) {
      /* make sure there are no more buffers on queue */
      fifo_wait_empty(this->out_fifo);
    }

    pthread_mutex_lock( &this->driver_lock );
    if(this->driver_open)
      this->driver->close(this->driver);  
    this->driver_open = 0;
    pthread_mutex_unlock( &this->driver_lock );
  }
}

static void ao_exit(xine_audio_port_t *this_gen) {
  aos_t *this = (aos_t *) this_gen;
  int vol;
  int prop = 0;
  
  audio_buffer_t *buf, *next;

  if (this->audio_loop_running) {
    void *p;

    this->audio_loop_running = 0;
    this->audio_paused = 0;

    buf = fifo_remove(this->free_fifo);
    buf->num_frames = 0;
    buf->stream = NULL;
    fifo_append (this->out_fifo, buf);

    pthread_join (this->audio_thread, &p);
    this->audio_thread = 0;
  }
      
  if (!this->grab_only) {
    pthread_mutex_lock( &this->driver_lock );
    
    if((this->driver->get_capabilities(this->driver)) & AO_CAP_MIXER_VOL)
      prop = AO_PROP_MIXER_VOL;
    else if((this->driver->get_capabilities(this->driver)) & AO_CAP_PCM_VOL)
      prop = AO_PROP_PCM_VOL;
    
    vol = this->driver->get_property(this->driver, prop);
    this->xine->config->update_num(this->xine->config, "audio.mixer_volume", vol);
    if(this->driver_open)
      this->driver->close(this->driver);
    this->driver->exit(this->driver);
    pthread_mutex_unlock( &this->driver_lock );
  }

  pthread_mutex_destroy(&this->driver_lock);
  pthread_mutex_destroy(&this->streams_lock);
  xine_list_free(this->streams);

  free (this->frame_buf[0]->mem);
  free (this->frame_buf[0]->extra_info);
  free (this->frame_buf[0]);
  free (this->frame_buf[1]->mem);
  free (this->frame_buf[1]->extra_info);
  free (this->frame_buf[1]);
  free (this->zero_space);

  buf = this->free_fifo->first;

  while (buf != NULL) {

    next = buf->next;

    free (buf->mem);
    free (buf->extra_info);
    free (buf);

    buf = next;
  }

  buf = this->out_fifo->first;

  while (buf != NULL) {

    next = buf->next;

    free (buf->mem);
    free (buf->extra_info);
    free (buf);

    buf = next;
  }

  free (this->free_fifo);
  free (this->out_fifo);
  free (this);
}

static uint32_t ao_get_capabilities (xine_audio_port_t *this_gen) {
  aos_t *this = (aos_t *) this_gen;
  uint32_t result;
  
  if (this->grab_only) {

    return AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO ;
    /* FIXME: make configurable
      | AO_CAP_MODE_4CHANNEL | AO_CAP_MODE_5CHANNEL
      | AO_CAP_MODE_5_1CHANNEL | AO_CAP_8BITS;
    */
  } else {
    pthread_mutex_lock( &this->driver_lock );
    result=this->driver->get_capabilities(this->driver);  
    pthread_mutex_unlock( &this->driver_lock );
  }
  return result;
}

static int ao_get_property (xine_audio_port_t *this_gen, int property) {
  aos_t *this = (aos_t *) this_gen;
  int ret;

  switch (property) {
  case AO_PROP_COMPRESSOR:
    ret = this->compression_factor_max*100;
    break;
  
  case AO_PROP_AMP:
    ret = this->amp_factor*100;
    break;
  
  case AO_PROP_EQ_30HZ:
  case AO_PROP_EQ_60HZ:
  case AO_PROP_EQ_125HZ:
  case AO_PROP_EQ_250HZ:
  case AO_PROP_EQ_500HZ:
  case AO_PROP_EQ_1000HZ:
  case AO_PROP_EQ_2000HZ:
  case AO_PROP_EQ_4000HZ:
  case AO_PROP_EQ_8000HZ:
  case AO_PROP_EQ_16000HZ: 
    ret = (100 * this->eq_gain[property - AO_PROP_EQ_30HZ]) / (1 << FP_FRBITS) ;
    break;

  case AO_PROP_DISCARD_BUFFERS:
    ret = this->discard_buffers;
    break;

  case AO_PROP_PAUSED:
    ret = this->audio_paused;
    break;

  default:
    pthread_mutex_lock( &this->driver_lock );
    ret = this->driver->get_property(this->driver, property);
    pthread_mutex_unlock( &this->driver_lock );
  }
  return ret;
}

static int ao_set_property (xine_audio_port_t *this_gen, int property, int value) {
  aos_t *this = (aos_t *) this_gen;
  int ret;

  switch (property) {
  case AO_PROP_COMPRESSOR:

    this->compression_factor_max = (double) value / 100.0;

    this->do_compress = (this->compression_factor_max >1.0);

    ret = this->compression_factor_max*100;
    break;
  
  case AO_PROP_AMP:

    this->amp_factor = (double) value / 100.0;

    this->do_amp = (this->amp_factor != 1.0);

    ret = this->amp_factor*100;
    break;

  case AO_PROP_EQ_30HZ:
  case AO_PROP_EQ_60HZ:
  case AO_PROP_EQ_125HZ:
  case AO_PROP_EQ_250HZ:
  case AO_PROP_EQ_500HZ:
  case AO_PROP_EQ_1000HZ:
  case AO_PROP_EQ_2000HZ:
  case AO_PROP_EQ_4000HZ:
  case AO_PROP_EQ_8000HZ:
  case AO_PROP_EQ_16000HZ: 
    {

      int min_gain, max_gain, i;

      this->eq_gain[property - AO_PROP_EQ_30HZ] = EQ_REAL(((float)value / 100.0)) ;
      
      /* calc pregain, find out if any gain != 0.0 - enable eq if that is the case */
      min_gain = EQ_REAL(0.0);
      max_gain = EQ_REAL(0.0);
      for (i=0; i<EQ_BANDS; i++) {
	if (this->eq_gain[i] < min_gain)
	  min_gain = this->eq_gain[i];
	if (this->eq_gain[i] > max_gain)
	  max_gain = this->eq_gain[i];
      }
      
      printf ("audio_out: eq min_gain=%d, max_gain=%d\n", min_gain, max_gain);

      this->do_equ = ((min_gain != EQ_REAL(0.0)) || (max_gain != EQ_REAL(0.0)));

      ret = value;
    }
    break;
  
  case AO_PROP_DISCARD_BUFFERS:
    /* recursive discard buffers setting */
    if(value)
      this->discard_buffers++;
    else
      this->discard_buffers--;
    ret = this->discard_buffers;
    
    /* discard buffers here because we have no output thread */
    if (this->grab_only && this->discard_buffers) {
      audio_buffer_t *buf;
      
      pthread_mutex_lock(&this->out_fifo->mutex);
  
      while ((buf = this->out_fifo->first)) {
  
#ifdef LOG
        printf ("audio_out: flushing out frame\n");
#endif
  
        buf = fifo_remove_int (this->out_fifo);
  
        fifo_append (this->free_fifo, buf);
      }
      pthread_mutex_unlock (&this->out_fifo->mutex);
    }
    break;

  case AO_PROP_PAUSED:
    this->audio_paused = value;
    ret = this->audio_paused;
    break;

  default:
    if (!this->grab_only) {
      pthread_mutex_lock( &this->driver_lock );
      ret =  this->driver->set_property(this->driver, property, value);
      pthread_mutex_unlock( &this->driver_lock );
    } else
      ret = 0;
  }

  return ret;
}

static int ao_control (xine_audio_port_t *this_gen, int cmd, ...) {

  aos_t *this = (aos_t *) this_gen;
  va_list args;
  void *arg;
  int rval;

  if (this->grab_only)
    return 0;

  pthread_mutex_lock( &this->driver_lock );
  va_start(args, cmd);
  arg = va_arg(args, void*);
  rval = this->driver->control(this->driver, cmd, arg);
  va_end(args);
  pthread_mutex_unlock( &this->driver_lock );

  return rval;
}

static void ao_flush (xine_audio_port_t *this_gen) {
  aos_t *this = (aos_t *) this_gen;
  audio_buffer_t *buf;

  if (this->xine->verbosity >= XINE_VERBOSITY_DEBUG)
    printf ("audio_out: ao_flush (loop running: %d)\n", this->audio_loop_running);

  if( this->audio_loop_running ) {
    pthread_mutex_lock(&this->flush_audio_driver_lock);
    this->discard_buffers++;
    this->flush_audio_driver = 1;
    
    /* do not try this in paused mode */
    while( this->flush_audio_driver ) {
      buf = fifo_remove (this->free_fifo);
      buf->num_frames = 0;
      buf->stream = NULL;
      fifo_append (this->out_fifo, buf);
      pthread_cond_wait(&this->flush_audio_driver_reached, &this->flush_audio_driver_lock);
    }
    this->discard_buffers--;

    pthread_mutex_unlock(&this->flush_audio_driver_lock);
    fifo_wait_empty(this->out_fifo);
  }
}

static int ao_status (xine_audio_port_t *this_gen, xine_stream_t *stream,
	       uint32_t *bits, uint32_t *rate, int *mode) {
  aos_t *this = (aos_t *) this_gen;
  xine_stream_t *cur;
  int ret = 0;
            
  pthread_mutex_lock(&this->streams_lock);
  for (cur = xine_list_first_content(this->streams); cur;
       cur = xine_list_next_content(this->streams))
    if (cur == stream || !stream) {
      *bits = this->input.bits;
      *rate = this->input.rate;
      *mode = this->input.mode;
      ret = 1;
      break;
    }
  pthread_mutex_unlock(&this->streams_lock);
  
  return ret;        
}

static void ao_update_av_sync_method(void *this_gen, xine_cfg_entry_t *entry) {
  aos_t *this = (aos_t *) this_gen;

#ifdef LOG
  printf ("audio_out: av_sync_method = %d\n", entry->num_value);
#endif

  this->av_sync_method_conf = entry->num_value;
  
  switch (this->av_sync_method_conf) {
  case 0:
    this->resample_sync_method = 0;
    break;
  case 1:
    this->resample_sync_method = 1;
    break;
  default:
    this->resample_sync_method = 0;
    break;
  }
  this->resample_sync_info.valid = 0;
}

xine_audio_port_t *ao_new_port (xine_t *xine, ao_driver_t *driver,
				int grab_only) {
 
  config_values_t *config = xine->config;
  aos_t           *this;
  int              i, err;
  pthread_attr_t   pth_attrs;
  static     char *resample_modes[] = {"auto", "off", "on", NULL};
  static     char *av_sync_methods[] = {"metronom_feedback", "resample", NULL};

  this = xine_xmalloc (sizeof (aos_t)) ;

  this->driver                = driver;
  this->xine                  = xine;
  this->clock                 = xine->clock;
  this->streams               = xine_list_new();
    
  pthread_mutex_init( &this->streams_lock, NULL );
  pthread_mutex_init( &this->driver_lock, NULL );

  this->ao.open                   = ao_open;
  this->ao.get_buffer             = ao_get_buffer;
  this->ao.put_buffer             = ao_put_buffer;
  this->ao.close                  = ao_close;
  this->ao.exit                   = ao_exit;
  this->ao.get_capabilities       = ao_get_capabilities;
  this->ao.get_property           = ao_get_property;
  this->ao.set_property           = ao_set_property;
  this->ao.control                = ao_control;
  this->ao.flush                  = ao_flush;
  this->ao.status                 = ao_status;
  
  this->audio_loop_running     = 0;
  this->grab_only              = grab_only;
  this->audio_paused           = 0;
  this->flush_audio_driver     = 0;
  this->discard_buffers        = 0;
  this->zero_space             = xine_xmalloc (ZERO_BUF_SIZE * 2 * 6);
  
  pthread_mutex_init( &this->flush_audio_driver_lock, NULL );
  pthread_cond_init( &this->flush_audio_driver_reached, NULL );

  if (!grab_only)
    this->gap_tolerance          = driver->get_gap_tolerance (this->driver);

  this->av_sync_method_conf = config->register_enum(config, "audio.av_sync_method", 0,
                                                    av_sync_methods,
                                                    _("choose method to sync audio and video"),
                                                    _("'resample' might be better if you use a "
                                                      "DXR3/H+ card and (analog) audio is "
                                                      "processed by your sound card"),
                                                    30, ao_update_av_sync_method, this);
  config->update_num(config,"audio.av_sync_method",this->av_sync_method_conf);
  
  this->resample_conf = config->register_enum (config, "audio.resample_mode", 0,
					       resample_modes,
					       _("adjust whether resampling is done or not"),
					       NULL, 20, NULL, NULL);
  this->force_rate    = config->register_num (config, "audio.force_rate", 0,
					      _("if !=0 always resample to given rate"),
					      NULL, 20, NULL, NULL);

  this->passthrough_offset = config->register_num (config,
						   "audio.passthrough_offset",
						   0,
						   _("adjust if audio is offsync"),
						   NULL, 10, NULL, NULL);

  this->compression_factor     = 2.0;
  this->compression_factor_max = 0.0;
  this->do_compress            = 0;
  this->amp_factor             = 1.0;
  this->do_amp                 = 0;

  this->do_equ                 = 0;
  this->eq_gain[0]             = 0;
  this->eq_gain[1]             = 0;
  this->eq_gain[2]             = 0;
  this->eq_gain[3]             = 0;
  this->eq_gain[4]             = 0;
  this->eq_gain[5]             = 0;
  this->eq_gain[6]             = 0;
  this->eq_gain[7]             = 0;
  this->eq_gain[8]             = 0;
  this->eq_gain[9]             = 0;
  this->eq_preamp              = EQ_REAL(1.0);
  this->eq_i                   = 0;
  this->eq_j                   = 2;
  this->eq_k                   = 1;

  bzero (this->eq_data_history, sizeof(sXYData) * EQ_BANDS * EQ_CHANNELS);

  /*
   * pre-allocate memory for samples
   */

  this->free_fifo        = fifo_new ();
  this->out_fifo         = fifo_new ();

  for (i=0; i<NUM_AUDIO_BUFFERS; i++) {

    audio_buffer_t *buf;

    buf = (audio_buffer_t *) xine_xmalloc (sizeof (audio_buffer_t));
    buf->mem = xine_xmalloc (AUDIO_BUF_SIZE);
    buf->mem_size = AUDIO_BUF_SIZE;
    buf->extra_info = malloc(sizeof(extra_info_t));
    
    fifo_append (this->free_fifo, buf);
  }
  
  /* buffers used for audio conversions */
  for (i=0; i<2; i++) {

    audio_buffer_t *buf;

    buf = (audio_buffer_t *) xine_xmalloc (sizeof (audio_buffer_t));
    buf->mem = xine_xmalloc (4*AUDIO_BUF_SIZE);
    buf->mem_size = 4*AUDIO_BUF_SIZE;
    buf->extra_info = malloc(sizeof(extra_info_t));

    this->frame_buf[i] = buf;
  }

  /*
   * Set audio volume to latest used one ?
   */
  if(this->driver){
    int vol;
    
    vol = config->register_range (config, "audio.mixer_volume", 
				  50, 0, 100, _("Audio volume"), 
				  NULL, 0, NULL, NULL);
    
    if(config->register_bool (config, "audio.remember_volume", 0,
			      _("restore volume level at startup"), 
			      _("if this not set, xine will not touch any mixer settings at startup"),
			      0, NULL, NULL)) {
      int prop = 0;

      if((ao_get_capabilities(&this->ao)) & AO_CAP_MIXER_VOL)
	prop = AO_PROP_MIXER_VOL;
      else if((ao_get_capabilities(&this->ao)) & AO_CAP_PCM_VOL)
	prop = AO_PROP_PCM_VOL;
      
      ao_set_property(&this->ao, prop, vol);
    }
  }              
  
  if (!this->grab_only) {
    /*
     * start output thread
     */

    this->audio_loop_running = 1;  
    
    pthread_attr_init(&pth_attrs);
    pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
    
    if ((err = pthread_create (&this->audio_thread,
			       &pth_attrs, ao_loop, this)) != 0) {
      
      printf ("audio_out: can't create thread (%s)\n", strerror(err));
      printf ("audio_out: sorry, this should not happen. please restart xine.\n");
      abort();
      
    } else
      if (this->xine->verbosity >= XINE_VERBOSITY_DEBUG)
        printf ("audio_out: thread created\n");
  }

  return &this->ao;
}
