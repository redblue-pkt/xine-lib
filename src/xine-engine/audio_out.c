/* 
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: audio_out.c,v 1.86 2002/12/01 01:32:47 guenter Exp $
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

#include "xine_internal.h"
#include "xineutils.h"
#include "audio_out.h"
#include "resample.h"
#include "metronom.h"

/*
#define LOG
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
 * These are designed to avoid updating the metronom too fast.
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

struct audio_fifo_s {
  audio_buffer_t    *first;
  audio_buffer_t    *last;
  int                num_buffers;

  pthread_mutex_t    mutex;
  pthread_cond_t     not_empty;
};


static audio_fifo_t *fifo_new () {

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

  return fifo;
}

static void fifo_append_int (audio_fifo_t *fifo,
			     audio_buffer_t *buf) {

  /* buf->next = NULL; */

  assert (!buf->next);

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
    pthread_cond_wait (&fifo->not_empty, &fifo->mutex);
  }

  buf = fifo->first;

  if (buf) {
    fifo->first = buf->next;

    if (!fifo->first) {

      fifo->last = NULL;
      fifo->num_buffers = 0;
      pthread_cond_init  (&fifo->not_empty, NULL);

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


void write_pause_burst(xine_audio_port_t *this, uint32_t num_frames) { 
 
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
    if(num_frames > 1536) {
      pthread_mutex_lock( &this->driver_lock );
      this->driver->write(this->driver, sbuf, 1536);
      pthread_mutex_unlock( &this->driver_lock );
      num_frames -= 1536;
    } else {
      pthread_mutex_lock( &this->driver_lock );
      this->driver->write(this->driver, sbuf, num_frames);
      pthread_mutex_unlock( &this->driver_lock );
      num_frames = 0;
    }
  }

}


static void ao_fill_gap (xine_audio_port_t *this, int64_t pts_len) {

  int num_frames ;

  num_frames = pts_len * this->frames_per_kpts / 1024;

  printf ("audio_out: inserting %d 0-frames to fill a gap of %lld pts\n",
	  num_frames, pts_len);

  if ((this->output.mode == AO_CAP_MODE_A52) || (this->output.mode == AO_CAP_MODE_AC5)) {
    write_pause_burst(this,num_frames);
    return; 
  }

  while (num_frames > 0) {
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

static audio_buffer_t * swap_frame_buffers ( xine_audio_port_t *this ) {
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

static void audio_filter_compress (xine_audio_port_t *this, int16_t *mem, int num_frames) {

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

    mem[i] = mem[i] * 0.98 * this->compression_factor;
  }
}

static audio_buffer_t* prepare_samples( xine_audio_port_t  *this, audio_buffer_t *buf) {
  double          acc_output_frames, output_frame_excess = 0;
  int             num_output_frames ;

  /*
   * volume / compressor filter
   */

  if ( this->do_compress && (this->input.bits == 16))
    audio_filter_compress (this, buf->mem, buf->num_frames);

  /*
   * resample and output audio data
   */

  /* calculate number of output frames (after resampling) */
  acc_output_frames = (double) buf->num_frames * this->frame_rate_factor
    + output_frame_excess;

  /* Truncate to an integer */
  num_output_frames = acc_output_frames;

  /* Keep track of the amount truncated */
  output_frame_excess = acc_output_frames - (double) num_output_frames;
      
#ifdef LOG
  printf ("audio_out: outputting %d frames\n", num_output_frames);
#endif
      
  /* convert 8 bit samples as needed */
  if( this->input.bits == 8 &&
      (this->do_resample || this->output.bits != 8 ||
       this->input.mode != this->output.mode ) ) {
    ensure_buffer_size(this->frame_buf[1], 2*mode_channels(this->input.mode),
		       buf->num_frames );
    audio_out_resample_8to16((int8_t *)buf->mem, this->frame_buf[1]->mem,
			     mode_channels(this->input.mode) * buf->num_frames );
    buf = swap_frame_buffers(this);
  }

  /* check if resampling may be skipped */
  if ( this->do_resample &&  
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
  if( this->output.bits == 8 && (this->do_resample || 
				 this->input.mode != this->output.mode) ) {
    ensure_buffer_size(this->frame_buf[1], 1*mode_channels(this->output.mode),
		       buf->num_frames );
    audio_out_resample_16to8(buf->mem, (int8_t *)this->frame_buf[1]->mem,
			     mode_channels(this->output.mode) * buf->num_frames );
    buf = swap_frame_buffers(this);
  }
  return buf;
}


/* Audio output loop: -
 * 1) Check for pause. 
 * 2) Make sure audio hardware is in RUNNING state.
 * 3) Get delay
 * 4) Do drop, 0-fill or output samples.
 * 5) Go round loop again.
 */
static void *ao_loop (void *this_gen) {

  xine_audio_port_t *this = (xine_audio_port_t *) this_gen;
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

    if (this->flush_audio_driver) {
#ifdef LOG
      printf ("audio_out: flush audio driver\n");
#endif
      this->control(this, AO_CTRL_FLUSH_BUFFERS);
      this->flush_audio_driver = 0;
    }

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
	}
      }

#ifdef LOG
      printf ("audio_out:loop:pause: I feel sleepy.\n");
#endif
      xine_usec_sleep (10000);
#ifdef LOG
      printf ("audio_out:loop:pause: I wake up.\n");
#endif
      continue;
    }


    pthread_mutex_lock( &this->driver_lock );
    delay = this->driver->delay(this->driver);
    while (delay < 0 && this->audio_loop_running) {
      /* Get the audio card into RUNNING state. */
      ao_fill_gap (this, 10000); /* FIXME, this PTS of 1000 should == period size */
      delay = this->driver->delay(this->driver);
    }
    pthread_mutex_unlock( &this->driver_lock ); 

    /*
     * where, in the timeline is the "end" of the 
     * hardware audio buffer at the moment?
     */

    cur_time = this->clock->get_current_time (this->clock);
    hw_vpts = cur_time;
  
#ifdef LOG
    printf ("audio_out: current delay is %lld, current time is %lld\n",
	      delay, cur_time);
#endif
    /* External A52 decoder delay correction */
    if ((this->output.mode==AO_CAP_MODE_A52) || (this->output.mode==AO_CAP_MODE_AC5)) 
      delay += this->passthrough_offset;

    hw_vpts += delay * 1024 / this->frames_per_kpts;
  
    /*
     * calculate gap:
     */
    gap = in_buf->vpts - hw_vpts;
#ifdef LOG
    printf ("audio_out: hw_vpts : %lld   buffer_vpts : %lld   gap : %lld\n",
	    hw_vpts, buf->vpts, gap);
#endif

    /*
     * output audio data synced to master clock
     */
    /* pthread_mutex_lock( &this->driver_lock ); */
    
    if (gap < (-1 * AO_MAX_GAP) || !in_buf->num_frames ) {

      /* drop package */
#ifdef LOG
      printf ("audio_out:loop: next fifo\n");
#endif
      fifo_append (this->free_fifo, in_buf);
      in_buf = NULL;

#ifdef LOG
      printf ("audio_out: audio package (vpts = %lld, gap = %lld) dropped\n", 
	      buf->vpts, gap);
#endif

      
      /* for small gaps ( tolerance < abs(gap) < AO_MAX_GAP ) 
       * feedback them into metronom's vpts_offset. 
       */
    } else if ( abs(gap) < AO_MAX_GAP && abs(gap) > this->gap_tolerance &&
           cur_time > (last_sync_time + SYNC_TIME_INVERVAL) && 
           bufs_since_sync >= SYNC_BUF_INTERVAL ) {
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

  pthread_exit(NULL);
  return NULL;
}

/*
 * open the audio device for writing to, start audio output thread
 */

static int ao_open(xine_audio_port_t *this, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {
 
  int output_sample_rate, err;
  pthread_attr_t pth_attrs;

  /* 
   * set metainfo
   */
  pthread_mutex_lock(&this->streams_lock);
  xine_list_append_content(this->streams, stream);
  for (stream = xine_list_first_content(this->streams); stream;
       stream = xine_list_next_content(this->streams)) {
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
  }
  pthread_mutex_unlock(&this->streams_lock);

  this->input.mode            = mode;
  this->input.rate            = rate;
  this->input.bits            = bits;
  
  /* not all drivers/cards support 8 bits */
  if( this->input.bits == 8 && 
      !(this->driver->get_capabilities(this->driver) & AO_CAP_8BITS) ) {
    bits = 16;      
    printf("audio_out: 8 bits not supported by driver, converting to 16 bits.\n");
  }
    
  /* provide mono->stereo and stereo->mono conversions */
  if( this->input.mode == AO_CAP_MODE_MONO && 
      !(this->driver->get_capabilities(this->driver) & AO_CAP_MODE_MONO) ) {
    mode = AO_CAP_MODE_STEREO;
    printf("audio_out: mono not supported by driver, converting to stereo.\n");
  }
  if( this->input.mode == AO_CAP_MODE_STEREO && 
      !(this->driver->get_capabilities(this->driver) & AO_CAP_MODE_STEREO) ) {
    mode = AO_CAP_MODE_MONO;
    printf("audio_out: stereo not supported by driver, converting to mono.\n");
  }
 
  pthread_mutex_lock( &this->driver_lock );
  output_sample_rate=this->driver->open(this->driver,bits,(this->force_rate ? this->force_rate : rate),mode);
  pthread_mutex_unlock( &this->driver_lock );
  
  if ( output_sample_rate == 0) {
    printf("audio_out: open failed!\n");
    return 0;
  }; 

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

  if (this->do_resample) 
    printf("audio_out: will resample audio from %d to %d\n",
	   this->input.rate, this->output.rate);

  this->frame_rate_factor = ((double)(this->output.rate)) / ((double)(this->input.rate));
  /* FIXME: If this->frames_per_kpts line goes after this->audio_step line, xine crashes with FPE, when compiled with gcc 3.0.1!!! Why? */ 
  this->frames_per_kpts   = this->output.rate * 1024 / 90000;
  this->audio_step        = ( (uint32_t)(90000) * (uint32_t)(32768) )/ this->input.rate;
#ifdef LOG
  printf ("audio_out : audio_step %d pts per 32768 frames\n", this->audio_step);
#endif

  pthread_mutex_lock(&this->streams_lock);
  for (stream = xine_list_first_content(this->streams); stream;
       stream = xine_list_next_content(this->streams))
    stream->metronom->set_audio_rate(stream->metronom, this->audio_step);
  pthread_mutex_unlock(&this->streams_lock);

  /*
   * start output thread
   */

  if( this->audio_thread ) {
    printf("audio_out: pthread already running!\n");
  }
  
  this->audio_loop_running = 1;  

  pthread_attr_init(&pth_attrs);
  pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);

  if ((err = pthread_create (&this->audio_thread,
			     &pth_attrs, ao_loop, this)) != 0) {

    /* FIXME: how does this happen ? */

    printf ("audio_out: can't create thread (%s)\n", strerror(err));
    printf ("audio_out: sorry, this should not happen. please restart xine.\n");
    abort();

  } else
    printf ("audio_out: thread created\n");

  return this->output.rate;
}

static audio_buffer_t *ao_get_buffer (xine_audio_port_t *this) {
  return fifo_remove (this->free_fifo);
}

static void ao_put_buffer (xine_audio_port_t *this, audio_buffer_t *buf, xine_stream_t *stream) {

  int64_t pts;

  if (buf->num_frames == 0) {
    fifo_append (this->free_fifo, buf);
    return;
  }

  pts = buf->vpts;

  buf->vpts = stream->metronom->got_audio_samples (stream->metronom, pts, 
						   buf->num_frames);

#ifdef LOG
  printf ("audio_out: ao_put_buffer, pts=%lld, vpts=%lld\n",
	  pts, buf->vpts);
#endif

  if ( buf->vpts + AO_MAX_GAP < this->last_audio_vpts) {

    /* reject buffer */
    printf ("audio_out: rejected buffer vpts=%lld, last_audio_vpts=%lld\n", 
	    buf->vpts, this->last_audio_vpts);

    fifo_append (this->free_fifo, buf);

  } else {

    fifo_append (this->out_fifo, buf);
    this->last_audio_vpts = buf->vpts;

  }

#ifdef LOG
  printf ("audio_out: ao_put_buffer done\n");
#endif
}

static void ao_close(xine_audio_port_t *this, xine_stream_t *stream) {

  audio_buffer_t *audio_buffer;
  xine_stream_t *cur;

  if (this->audio_loop_running) {
    void *p;

    this->audio_loop_running = 0;
    this->audio_paused = 0;

    audio_buffer = fifo_remove(this->free_fifo);
    audio_buffer->num_frames = 0;
    fifo_append (this->out_fifo, audio_buffer);

    pthread_join (this->audio_thread, &p);
    this->audio_thread = 0;
  }
  
  /* unregister stream */
  pthread_mutex_lock(&this->streams_lock);
  for (cur = xine_list_first_content(this->streams); cur;
       cur = xine_list_next_content(this->streams))
    if (cur == stream) {
      xine_list_delete_current(this->streams);
      break;
    }
  pthread_mutex_unlock(&this->streams_lock);

  pthread_mutex_lock( &this->driver_lock );
  this->driver->close(this->driver);  
  pthread_mutex_unlock( &this->driver_lock );
}

static void ao_exit(xine_audio_port_t *this) {
  int vol;
  int prop = 0;
  
  audio_buffer_t *buf, *next;
  
  pthread_mutex_lock( &this->driver_lock );
  
  if((this->driver->get_capabilities(this->driver)) & AO_CAP_MIXER_VOL)
    prop = AO_PROP_MIXER_VOL;
  else if((this->driver->get_capabilities(this->driver)) & AO_CAP_PCM_VOL)
    prop = AO_PROP_PCM_VOL;
  
  vol = this->driver->get_property(this->driver, prop);
  this->xine->config->update_num(this->xine->config, "audio.mixer_volume", vol);
  this->driver->exit(this->driver);
  pthread_mutex_unlock( &this->driver_lock );
  
  pthread_mutex_destroy(&this->driver_lock);
  pthread_mutex_destroy(&this->streams_lock);
  xine_list_free(this->streams);

  free (this->frame_buf[0]->mem);
  free (this->frame_buf[0]);
  free (this->frame_buf[1]->mem);
  free (this->frame_buf[1]);
  free (this->zero_space);

  buf = this->free_fifo->first;

  while (buf != NULL) {

    next = buf->next;

    free (buf->mem);
    free (buf);

    buf = next;
  }

  buf = this->out_fifo->first;

  while (buf != NULL) {

    next = buf->next;

    free (buf->mem);
    free (buf);

    buf = next;
  }

  free (this->free_fifo);
  free (this->out_fifo);
  free (this);
}

static uint32_t ao_get_capabilities (xine_audio_port_t *this) {
  uint32_t result;
  
  pthread_mutex_lock( &this->driver_lock );
  result=this->driver->get_capabilities(this->driver);  
  pthread_mutex_unlock( &this->driver_lock );
  
  return result;
}

static int ao_get_property (xine_audio_port_t *this, int property) {
  int ret;

  switch (property) {
  case AO_PROP_COMPRESSOR:
    ret = this->compression_factor_max*100;
    break;

  default:
    pthread_mutex_lock( &this->driver_lock );
    ret = this->driver->get_property(this->driver, property);
    pthread_mutex_unlock( &this->driver_lock );
  }
  return ret;
}

static int ao_set_property (xine_audio_port_t *this, int property, int value) {
  int ret;

  switch (property) {
  case AO_PROP_COMPRESSOR:

    this->compression_factor_max = (double) value / 100.0;
    this->do_compress =  (this->compression_factor_max >1.0);

    ret = this->compression_factor_max*100;
    break;

  default:
    pthread_mutex_lock( &this->driver_lock );
    ret =  this->driver->set_property(this->driver, property, value);
    pthread_mutex_unlock( &this->driver_lock );
  }

  return ret;
}

static int ao_control (xine_audio_port_t *this, int cmd, ...) {

  va_list args;
  void *arg;
  int rval;

  va_start(args, cmd);
  arg = va_arg(args, void*);
  pthread_mutex_lock( &this->driver_lock );
  rval = this->driver->control(this->driver, cmd, arg);
  pthread_mutex_unlock( &this->driver_lock );
  va_end(args);

  return rval;
}

static void ao_flush (xine_audio_port_t *this) {
  audio_buffer_t *buf;
  int            i, num_buffers;

  pthread_mutex_lock (&this->out_fifo->mutex);
  pthread_mutex_lock (&this->free_fifo->mutex);

  num_buffers = this->out_fifo->num_buffers;

  printf ("audio_out: flush fifo (%d buffers)\n", num_buffers);

  for (i = 0; i < num_buffers; i++) {
    buf = fifo_remove_int (this->out_fifo);
    fifo_append_int (this->free_fifo, buf);
  }

  /* 
   * make sure ao_loop can savely quit
   */

  buf = fifo_remove_int (this->free_fifo);
  buf->num_frames = 0;
  fifo_append_int (this->out_fifo, buf);

  this->flush_audio_driver = 1;
  pthread_mutex_unlock (&this->free_fifo->mutex);
  pthread_mutex_unlock (&this->out_fifo->mutex);
}

xine_audio_port_t *ao_new_port (xine_t *xine, ao_driver_t *driver) {
 
  config_values_t *config = xine->config;
  xine_audio_port_t *this;
  int              i;
  static     char *resample_modes[] = {"auto", "off", "on", NULL};

  this = xine_xmalloc (sizeof (xine_audio_port_t)) ;

  this->driver                = driver;
  this->xine                  = xine;
  this->clock                 = xine->clock;
  this->streams               = xine_list_new();
    
  pthread_mutex_init( &this->streams_lock, NULL );
  pthread_mutex_init( &this->driver_lock, NULL );

  this->open                   = ao_open;
  this->get_buffer             = ao_get_buffer;
  this->put_buffer             = ao_put_buffer;
  this->close                  = ao_close;
  this->exit                   = ao_exit;
  this->get_capabilities       = ao_get_capabilities;
  this->get_property           = ao_get_property;
  this->set_property           = ao_set_property;
  this->control                = ao_control;
  this->flush                  = ao_flush;
  this->audio_loop_running     = 0;
  this->audio_paused           = 0;
  this->flush_audio_driver     = 0;
  this->zero_space             = xine_xmalloc (ZERO_BUF_SIZE * 2 * 6);
  this->gap_tolerance          = driver->get_gap_tolerance (this->driver);

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

  this->compression_factor     = 1.0;
  this->compression_factor_max = 4.0;
  this->do_compress            = 0;

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

    fifo_append (this->free_fifo, buf);
  }
  
  /* buffers used for audio conversions */
  for (i=0; i<2; i++) {

    audio_buffer_t *buf;

    buf = (audio_buffer_t *) xine_xmalloc (sizeof (audio_buffer_t));
    buf->mem = xine_xmalloc (4*AUDIO_BUF_SIZE);
    buf->mem_size = 4*AUDIO_BUF_SIZE;

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

      if((ao_get_capabilities(this)) & AO_CAP_MIXER_VOL)
	prop = AO_PROP_MIXER_VOL;
      else if((ao_get_capabilities(this)) & AO_CAP_PCM_VOL)
	prop = AO_PROP_PCM_VOL;
      
      ao_set_property(this, prop, vol);
    }
  }

  return this;
}
