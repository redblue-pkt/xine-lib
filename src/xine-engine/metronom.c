/* 
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: metronom.c,v 1.16 2001/07/10 22:16:58 guenter Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "monitor.h"
#include "xine_internal.h"
#include "metronom.h"
#include "utils.h"

#define MAX_PTS_TOLERANCE  5000
#define MAX_VIDEO_DELTA    1600
#define MAX_AUDIO_DELTA    1600
#define AUDIO_SAMPLE_NUM   32768
#define WRAP_START_TIME    100000
#define WRAP_TRESHOLD      30000 
#define MAX_NUM_WRAP_DIFF  100


/*
 * ****************************************
 *       master clock feature
 * ****************************************
 */


static void metronom_start_clock (metronom_t *this, uint32_t pts) {

  pthread_mutex_lock (&this->lock);

  gettimeofday(&this->start_time, NULL);
  this->last_pts = this->start_pts = pts;
  this->stopped  = 0;

  pthread_mutex_unlock (&this->lock);

}


static uint32_t metronom_get_current_time (metronom_t *this) {

  uint32_t pts;
  struct timeval tv;

  pthread_mutex_lock (&this->lock);

  gettimeofday(&tv, NULL);
  pts  = (tv.tv_sec  - this->start_time.tv_sec) * 90000;
  pts += (tv.tv_usec - this->start_time.tv_usec) / 10 * 9 / 10;
  pts += this->start_pts;
  
  if (this->stopped || (this->last_pts > pts)) {
    /* printf("metronom: get_current_time(): timer STOPPED!\n"); */
    pts = this->last_pts;
  }

  pthread_mutex_unlock (&this->lock);

  return pts;
}


static void metronom_stop_clock(metronom_t *this) {

  uint32_t current_time = this->get_current_time(this);

  pthread_mutex_lock (&this->lock);

  this->stopped = 1;
  this->last_pts = current_time;

  pthread_mutex_unlock (&this->lock);

}


static void metronom_resume_clock(metronom_t *this) {
  this->start_clock(this, this->last_pts);
}



static void metronom_adjust_clock(metronom_t *this, uint32_t desired_pts)
{
  int      delta;
  uint32_t current_time = this->get_current_time(this);

  pthread_mutex_lock (&this->lock);

  /* FIXME: this should be softer than a brute force warp... */
  delta  = desired_pts;
  delta -= current_time;
  this->start_pts += delta;
  printf("adjusting start_pts to %d\n", this->start_pts);  

  pthread_mutex_unlock (&this->lock);
}

/*
 * virtual pts calculation
*/

static void metronom_video_stream_start (metronom_t *this) {

  pthread_mutex_lock (&this->lock);

  printf ("metronom: video stream start...\n");

  if (this->video_stream_running) {
    printf ("metronom: video stream start ignored\n");
    pthread_mutex_unlock (&this->lock);
    return;
  }

  this->pts_per_frame             = 3000;

  this->video_vpts                = 0;

  this->video_pts_delta           = 0;

  this->last_video_pts            = 0;
  this->num_video_vpts_guessed    = 1;

  this->video_wrap_offset         = 0;
  this->wrap_diff_counter         = 0;

  this->video_stream_running      = 1;
  this->video_stream_starting     = 1;

  if (this->have_audio) {
    while (!this->audio_stream_running) {
      printf ("metronom: waiting for audio to start...\n");
      pthread_cond_wait (&this->audio_started, &this->lock);
    }
  }
  pthread_cond_signal (&this->video_started);

  pthread_mutex_unlock (&this->lock);

  printf ("metronom: video stream start...done\n");

  metronom_start_clock (this, 0);
}


static void metronom_video_stream_end (metronom_t *this) {
  
  pthread_mutex_lock (&this->lock);

  printf ("metronom: video stream end\n");

  if (!this->video_stream_running) {
    printf ("metronom: video stream end ignored\n");
    pthread_mutex_unlock (&this->lock);
    return;
  }

  this->video_stream_running = 0;

  if (this->have_audio) {
    while (this->audio_stream_running) {
      printf ("metronom: waiting for audio to end...\n");
      pthread_cond_wait (&this->audio_ended, &this->lock);
    }
  }
  pthread_cond_signal (&this->video_ended);


  pthread_mutex_unlock (&this->lock);
}

static void metronom_audio_stream_start (metronom_t *this) {

  pthread_mutex_lock (&this->lock);

  printf ("metronom: audio stream start...\n");

  if (this->audio_stream_running) {
    printf ("metronom: audio stream start ignored\n");
    pthread_mutex_unlock (&this->lock);
    return;
  }

  this->audio_vpts                = 0;

  this->audio_pts_delta           = 0;

  this->num_audio_samples_guessed = 1;
  this->last_audio_pts            = 0;

  this->audio_wrap_offset         = 0;
  this->wrap_diff_counter         = 0;

  this->audio_stream_running      = 1;
  this->audio_stream_starting     = 1;

  while (!this->video_stream_running) {
    printf ("metronom: waiting for video to start...\n");
    pthread_cond_wait (&this->video_started, &this->lock);
  }

  pthread_cond_signal (&this->audio_started);

  pthread_mutex_unlock (&this->lock);

  printf ("metronom: audio stream start...done\n");

  metronom_start_clock (this, 0);
}

static void metronom_audio_stream_end (metronom_t *this) {
  
  pthread_mutex_lock (&this->lock);

  printf ("metronom: audio stream end\n");
  if (!this->audio_stream_running) {
    printf ("metronom: audio stream end ignored\n");
    pthread_mutex_unlock (&this->lock);
    return;
  }

  this->audio_stream_running = 0;
  while (this->video_stream_running) {
    
    printf ("waiting for video to end...\n");
    pthread_cond_wait (&this->video_ended, &this->lock);
  }

  pthread_cond_signal (&this->audio_ended);
  pthread_mutex_unlock (&this->lock);
}

static void metronom_set_video_rate (metronom_t *this, uint32_t pts_per_frame) {
  pthread_mutex_lock (&this->lock);

  this->pts_per_frame = pts_per_frame;

  pthread_mutex_unlock (&this->lock);
}

static uint32_t metronom_get_video_rate (metronom_t *this) {
  return this->pts_per_frame + this->video_pts_delta;
}

static void metronom_set_audio_rate (metronom_t *this, uint32_t pts_per_smpls) {
  pthread_mutex_lock (&this->lock);

  this->pts_per_smpls = pts_per_smpls;

  pthread_mutex_unlock (&this->lock);

  xprintf (METRONOM | VERBOSE, "metronom: %d pts per %d samples\n", pts_per_smpls, AUDIO_SAMPLE_NUM);

}

static uint32_t metronom_got_spu_packet (metronom_t *this, uint32_t pts,uint32_t duration) {
  if (pts) {
    this->spu_vpts=pts;
  } else {
    pts=this->spu_vpts;
    this->spu_vpts=this->spu_vpts;
  }
  return pts + this->video_wrap_offset;
}

static uint32_t metronom_got_video_frame (metronom_t *this, uint32_t pts) {

  uint32_t vpts;

  pthread_mutex_lock (&this->lock);

  if (pts) {

    /*
     * first video pts ?
     */
    if (this->video_stream_starting) {
      this->video_stream_starting = 0;
      
      this->video_wrap_offset = -1 * pts;
  
      if (this->audio_wrap_offset) {
	if (this->audio_wrap_offset>this->video_wrap_offset) 
	  this->video_wrap_offset = this->audio_wrap_offset;
	else
	  this->audio_wrap_offset = this->video_wrap_offset;
      }

      printf ("metronom: first video pts => offset = %d\n", this->video_wrap_offset); 

    }

    /*
     * did a wrap-around occur?
     */

    if ( ( (pts + WRAP_TRESHOLD) <this->last_video_pts) 
	 && (pts<WRAP_START_TIME) ) {
      
      this->video_wrap_offset += this->last_video_pts - pts 
	+ this->num_video_vpts_guessed *(this->pts_per_frame + this->video_pts_delta);
      
      printf ("metronom: video pts wraparound detected, wrap_offset = %d\n",
	      this->video_wrap_offset);
    }

    /*
     * audio and video wrap are not allowed to differ
     * for too long
     */

    if ( !this->audio_stream_starting && this->have_audio
	 && (this->video_wrap_offset != this->audio_wrap_offset)) {
      this->wrap_diff_counter++;

      if (this->wrap_diff_counter > MAX_NUM_WRAP_DIFF) {

	printf ("metronom: forcing video_wrap (%d) and audio wrap (%d)",
		this->video_wrap_offset, this->audio_wrap_offset);

	if (this->video_wrap_offset > this->audio_wrap_offset)
	  this->audio_wrap_offset = this->video_wrap_offset;
	else
	  this->video_wrap_offset = this->audio_wrap_offset;

	printf ("to %d\n", this->video_wrap_offset);

	this->wrap_diff_counter = 0;
      }
    }

    vpts = pts + this->video_wrap_offset;

    /*
     * calc delta to compensate wrong framerates 
     */
      
    if (this->last_video_pts && (pts>this->last_video_pts)) {
      int32_t  vpts_diff;

      vpts_diff   = vpts - this->video_vpts;

      this->video_pts_delta += vpts_diff / (this->num_video_vpts_guessed);
      
      if (abs(this->video_pts_delta) >= MAX_VIDEO_DELTA) 
	this->video_pts_delta = 0;
    }

    this->num_video_vpts_guessed = 0;
    this->last_video_pts  = pts;
    this->video_vpts      = vpts;
  } else
    vpts = this->video_vpts;

  this->video_vpts += this->pts_per_frame + this->video_pts_delta;
  this->num_video_vpts_guessed++ ;

  xprintf (METRONOM | VERBOSE, "metronom: video vpts for %10d : %10d\n", pts, vpts);

  pthread_mutex_unlock (&this->lock);

  return vpts + this->av_offset;
}


static uint32_t metronom_got_audio_samples (metronom_t *this, uint32_t pts, uint32_t nsamples) {

  uint32_t vpts;
  
  xprintf (METRONOM | VERBOSE, "metronom: got %d audio samples (pts=%d)\n",
	   nsamples,pts);

  pthread_mutex_lock (&this->lock);

  if (pts) {

    /*
     * first audio pts ?
     */
    if (this->audio_stream_starting) {
      this->audio_stream_starting = 0;
      
      this->audio_wrap_offset = -1 * pts;

      if (this->video_wrap_offset) {
	if (this->audio_wrap_offset>this->video_wrap_offset) 
	  this->video_wrap_offset = this->audio_wrap_offset;
	else
	  this->audio_wrap_offset = this->video_wrap_offset;
      }

      printf ("metronom: first audio pts => offset = %d\n", this->audio_wrap_offset); 
    }

    /*
     * did a wrap-around occur?
     */
    if ( ( (pts + WRAP_TRESHOLD) < this->last_audio_pts )
	 && (pts<WRAP_START_TIME) ) {
      
      this->audio_wrap_offset += this->last_audio_pts - pts
	+ this->num_audio_samples_guessed *(this->audio_pts_delta + this->pts_per_smpls) / AUDIO_SAMPLE_NUM ;
      
      printf ("metronom: audio pts wraparound detected, wrap_offset = %d\n",
	      this->audio_wrap_offset);
    }

    /*
     * audio and video wrap are not allowed to differ
     * for too long
     */

    if ( !this->video_stream_starting
	 && this->video_wrap_offset != this->audio_wrap_offset) {
      this->wrap_diff_counter++;

      if (this->wrap_diff_counter > MAX_NUM_WRAP_DIFF) {

	printf ("metronom: forcing video_wrap (%d) and audio wrap (%d)",
		this->video_wrap_offset, this->audio_wrap_offset);

	if (this->video_wrap_offset > this->audio_wrap_offset)
	  this->audio_wrap_offset = this->video_wrap_offset;
	else
	  this->video_wrap_offset = this->audio_wrap_offset;

	printf ("to %d\n", this->video_wrap_offset);

	this->wrap_diff_counter = 0;
      }
    }

    vpts = pts + this->audio_wrap_offset;

    /*
     * calc delta to compensate wrong samplerates 
     */

    if (this->last_audio_pts && (pts>this->last_audio_pts)) {
      int32_t  vpts_diff;

      vpts_diff   = vpts - this->audio_vpts;

      this->audio_pts_delta += vpts_diff*AUDIO_SAMPLE_NUM / (this->num_audio_samples_guessed);
	
      if (abs(this->audio_pts_delta) >= MAX_AUDIO_DELTA) 
	this->audio_pts_delta = 0;
    }      

    this->num_audio_samples_guessed = 0;
    this->last_audio_pts = pts;
    this->audio_vpts     = vpts;
  } else
    vpts = this->audio_vpts;

  this->audio_vpts += nsamples * (this->audio_pts_delta + this->pts_per_smpls) / AUDIO_SAMPLE_NUM;
  this->num_audio_samples_guessed += nsamples;

  xprintf (METRONOM | VERBOSE, "metronom: audio vpts for %10d : %10d\n", pts, vpts);

  pthread_mutex_unlock (&this->lock);

  return vpts;
}

static void metronom_set_av_offset (metronom_t *this, int32_t pts) {

  pthread_mutex_lock (&this->lock);

  this->av_offset = pts;

  pthread_mutex_unlock (&this->lock);

  printf ("metronom: av_offset=%d pts\n", pts);
}

static int32_t metronom_get_av_offset (metronom_t *this) {
  return this->av_offset;
}



metronom_t * metronom_init (int have_audio) {

  metronom_t *this = xmalloc (sizeof (metronom_t));

  this->audio_stream_start= metronom_audio_stream_start;
  this->audio_stream_end  = metronom_audio_stream_end  ;
  this->video_stream_start= metronom_video_stream_start;
  this->video_stream_end  = metronom_video_stream_end  ;
  this->set_video_rate    = metronom_set_video_rate;
  this->get_video_rate    = metronom_get_video_rate;
  this->set_audio_rate    = metronom_set_audio_rate;
  this->got_video_frame   = metronom_got_video_frame;
  this->got_audio_samples = metronom_got_audio_samples;
  this->got_spu_packet    = metronom_got_spu_packet;
  this->set_av_offset     = metronom_set_av_offset;
  this->get_av_offset     = metronom_get_av_offset;
  this->start_clock       = metronom_start_clock;
  this->stop_clock        = metronom_stop_clock;
  this->resume_clock      = metronom_resume_clock;
  this->get_current_time  = metronom_get_current_time;
  this->adjust_clock      = metronom_adjust_clock;

  pthread_mutex_init (&this->lock, NULL);
  pthread_cond_init (&this->video_started, NULL);
  pthread_cond_init (&this->audio_started, NULL);
  pthread_cond_init (&this->video_ended, NULL);
  pthread_cond_init (&this->audio_ended, NULL);
    
  this->av_offset   = 0;
  this->have_audio  = have_audio;

  return this;
}

