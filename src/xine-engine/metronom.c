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
 * $Id: metronom.c,v 1.8 2001/06/04 17:13:36 guenter Exp $
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
#define AUDIO_SAMPLE_NUM   32768
#define MAX_WRAP_TOLERANCE 90000

static void metronom_reset (metronom_t *this) {

  pthread_mutex_lock (&this->lock);

  this->pts_per_frame             = 3000;

  this->video_vpts                = 0;
  this->audio_vpts                = 0;

  this->video_pts_delta           = 0;
  this->audio_pts_delta           = 0;

  this->last_video_pts            = 0;
  this->num_video_vpts_guessed    = 1;
  this->num_audio_samples_guessed = 1;
  this->last_audio_pts            = 0;

  this->sync_pts                  = 0;
  this->sync_vpts                 = 0;

  this->video_wrap_offset         = 0;
  this->audio_wrap_offset         = 0;

  this->av_offset                 = 0;

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

static uint32_t metronom_got_spu_packet (metronom_t *this, uint32_t pts) {
  /* FIXME: Nasty hack */

  return this->sync_pts;
}

static uint32_t metronom_got_video_frame (metronom_t *this, uint32_t pts) {

  uint32_t vpts;

  pthread_mutex_lock (&this->lock);

  /* pts = 0; */

  if (pts) {

    /*
     * did a wrap-around occur?
     */
    if ( (pts+this->video_wrap_offset+MAX_WRAP_TOLERANCE)<this->last_video_pts) {

      this->video_wrap_offset = this->last_video_pts - pts 
	+ this->num_video_vpts_guessed *(this->pts_per_frame + this->video_pts_delta);

      printf ("metronom: video pts wraparound detected, wrap_offset = %d\n",
	      this->video_wrap_offset);

    }

    pts += this->video_wrap_offset;

    /*
     * calc delta to compensate wrong framerates 
     */
      
    if (this->last_video_vpts && (pts>this->last_video_pts)) {
      int32_t  vpts_diff;
      uint32_t synced_vpts ;
      int32_t  diff;

      diff        = pts - this->last_video_pts;
      synced_vpts = this->last_video_vpts + diff;
      vpts_diff   = synced_vpts - this->video_vpts;

      this->video_pts_delta += vpts_diff / (this->num_video_vpts_guessed);
      
      if (abs(this->video_pts_delta) >= MAX_VIDEO_DELTA) 
	this->video_pts_delta = 0;

      this->num_video_vpts_guessed = 0;
    }

    /* 
     * sync if necessary and possible
     */

    if (this->sync_vpts && (pts>this->sync_pts)) {

      int32_t  vpts_diff;
      uint32_t synced_vpts ;
      int32_t  diff;

      diff        = pts - this->sync_pts;
      synced_vpts = this->sync_vpts + diff;
      vpts_diff   = synced_vpts - this->video_vpts;

      xprintf (METRONOM | VERBOSE, "metronom: video calced vpts : %d <=> synced vpts : %d (diff: %d, delta: %d)\n",
	      this->video_vpts, synced_vpts, vpts_diff, this->video_pts_delta);
      
      if (abs(vpts_diff)>MAX_PTS_TOLERANCE) {
	if ( synced_vpts>this->video_vpts ) {
	  this->video_vpts = synced_vpts;
	}
      } else {
	xprintf (METRONOM | VERBOSE, "metronom: video tolerating diff\n");
      }

    } else
      xprintf (METRONOM | VERBOSE, "metronom: video not synced on this one\n");

    this->sync_pts        = pts;
    this->sync_vpts       = this->video_vpts;
    this->last_video_vpts = this->video_vpts;
    this->last_video_pts  = pts;
  }
  
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
    int32_t diff;

    /*
     * did a wrap-around occur?
     */

    if ((pts+this->audio_wrap_offset+MAX_WRAP_TOLERANCE)<this->last_audio_pts) {

      this->audio_wrap_offset = this->last_audio_pts - pts
	+ this->num_audio_samples_guessed *(this->audio_pts_delta + this->pts_per_smpls) / AUDIO_SAMPLE_NUM ;

      printf ("metronom: audio pts wraparound detected, wrap_offset = %d\n",
	      this->audio_wrap_offset);

    }

    pts += this->audio_wrap_offset;

    diff = pts - this->sync_pts;

    if (this->sync_vpts && (pts>this->sync_pts)) {

      int32_t vpts_diff;
      uint32_t synced_vpts = this->sync_vpts + diff;

      vpts_diff = synced_vpts - this->audio_vpts;

      xprintf (METRONOM | VERBOSE, "metronom: audio calced vpts : %d <=> synced vpts : %d (diff: %d, delta: %d)\n",
	      this->audio_vpts, synced_vpts, vpts_diff, this->audio_pts_delta);
      if (abs(vpts_diff)>5000) {

	/* calc delta for wrong samplerates */

	this->audio_pts_delta += vpts_diff*AUDIO_SAMPLE_NUM / (this->num_audio_samples_guessed);
	
	if (abs(this->audio_pts_delta) >= 10000) 
	  this->audio_pts_delta = 0;
      
	if (synced_vpts>this->audio_vpts)
	  this->audio_vpts = synced_vpts;
      
      } else
	xprintf (METRONOM | VERBOSE, "metronom: audio tolerating diff\n");

    } else
      xprintf (METRONOM | VERBOSE, "metronom: audio not synced on this one\n");

    this->sync_pts = pts;
    this->sync_vpts = this->audio_vpts;
    this->num_audio_samples_guessed = 0;
    this->last_audio_pts = pts;
  }
  
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

metronom_t * metronom_init () {

  metronom_t *this = xmalloc (sizeof (metronom_t));

  this->reset             = metronom_reset;
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
    
  this->reset (this);

  return this;
}

