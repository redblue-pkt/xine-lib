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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: metronom.c,v 1.109 2003/01/11 03:47:01 miguelfreitas Exp $
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
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "xine_internal.h"
#include "metronom.h"
#include "xineutils.h"
#include "video_out.h"

#define MAX_AUDIO_DELTA        1600
#define AUDIO_SAMPLE_NUM      32768
#define WRAP_THRESHOLD       120000 
#define MAX_NUM_WRAP_DIFF        10
#define MAX_SCR_PROVIDERS        10
#define PREBUFFER_PTS_OFFSET  30000
#define VIDEO_DRIFT_TOLERANCE 45000
#define AUDIO_DRIFT_TOLERANCE 45000

/*#define OLD_DRIFT_CORRECTION  1*/

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

/*
#define LOG
*/

/*
 * ****************************************
 *   primary SCR plugin: 
 *    unix System Clock Reference
 * ****************************************
 */

typedef struct unixscr_s {
  scr_plugin_t     scr;

  struct timeval   cur_time;
  int64_t         cur_pts;
  double           speed_factor;

  pthread_mutex_t  lock;

} unixscr_t;

static int unixscr_get_priority (scr_plugin_t *scr) {
  return 5; /* low priority */
}

/* Only call this when already mutex locked */
static void unixscr_set_pivot (unixscr_t *this) {

  struct   timeval tv;
  int64_t pts;
  double   pts_calc; 

  gettimeofday(&tv, NULL);
  pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
  pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;
  pts = this->cur_pts + pts_calc;

/* This next part introduces a one off inaccuracy 
 * to the scr due to rounding tv to pts. 
 */
  this->cur_time.tv_sec=tv.tv_sec;
  this->cur_time.tv_usec=tv.tv_usec;
  this->cur_pts=pts; 

  return ;
}

static int unixscr_set_speed (scr_plugin_t *scr, int speed) {
  unixscr_t *this = (unixscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  unixscr_set_pivot( this );
  this->speed_factor = (double) speed * 90000.0 / 4.0;

  pthread_mutex_unlock (&this->lock);

  return speed;
}

static void unixscr_adjust (scr_plugin_t *scr, int64_t vpts) {
  unixscr_t *this = (unixscr_t*) scr;
  struct   timeval tv;

  pthread_mutex_lock (&this->lock);

  gettimeofday(&tv, NULL);
  this->cur_time.tv_sec=tv.tv_sec;
  this->cur_time.tv_usec=tv.tv_usec;
  this->cur_pts = vpts;

  pthread_mutex_unlock (&this->lock);
}

static void unixscr_start (scr_plugin_t *scr, int64_t start_vpts) {
  unixscr_t *this = (unixscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  gettimeofday(&this->cur_time, NULL);
  this->cur_pts = start_vpts;

  pthread_mutex_unlock (&this->lock);
  
  unixscr_set_speed (&this->scr, XINE_SPEED_NORMAL);
}

static int64_t unixscr_get_current (scr_plugin_t *scr) {
  unixscr_t *this = (unixscr_t*) scr;

  struct   timeval tv;
  int64_t pts;
  double   pts_calc; 
  pthread_mutex_lock (&this->lock);

  gettimeofday(&tv, NULL);
  
  pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
  pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;

  pts = this->cur_pts + pts_calc;
  
  pthread_mutex_unlock (&this->lock);

  return pts;
}

static void unixscr_exit (scr_plugin_t *scr) {
  unixscr_t *this = (unixscr_t*) scr;

  pthread_mutex_destroy (&this->lock);
  free(this);
}

static scr_plugin_t* unixscr_init () {
  unixscr_t *this;

  this = malloc(sizeof(*this));
  memset(this, 0, sizeof(*this));
  
  this->scr.interface_version = 2;
  this->scr.get_priority      = unixscr_get_priority;
  this->scr.set_speed         = unixscr_set_speed;
  this->scr.adjust            = unixscr_adjust;
  this->scr.start             = unixscr_start;
  this->scr.get_current       = unixscr_get_current;
  this->scr.exit              = unixscr_exit;
  
  pthread_mutex_init (&this->lock, NULL);
  
  unixscr_set_speed (&this->scr, XINE_SPEED_PAUSE);
#ifdef LOG
  printf("xine-scr_init: complete\n");
#endif

  return &this->scr;
}
 

/*
 * ****************************************
 *       master clock feature
 * ****************************************
 */


static void metronom_start_clock (metronom_clock_t *this, int64_t pts) {
  scr_plugin_t** scr;

#ifdef LOG
  printf ("metronom: start_clock (at %lld)\n", pts);
#endif

  for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
    if (*scr) (*scr)->start(*scr, pts);
  
  this->speed = XINE_SPEED_NORMAL;
}


static int64_t metronom_get_current_time (metronom_clock_t *this) {
  return this->scr_master->get_current(this->scr_master);
}


static void metronom_stop_clock(metronom_clock_t *this) {
  scr_plugin_t** scr;
  for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
    if (*scr) (*scr)->set_speed(*scr, XINE_SPEED_PAUSE);
}

static void metronom_resume_clock(metronom_clock_t *this) {
  scr_plugin_t** scr;
  for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
    if (*scr) (*scr)->set_speed(*scr, XINE_SPEED_NORMAL);
}



static void metronom_adjust_clock(metronom_clock_t *this, int64_t desired_pts) {
  if (this->scr_adjustable)
    this->scr_master->adjust(this->scr_master, desired_pts);
}

static int metronom_set_speed (metronom_clock_t *this, int speed) {

  scr_plugin_t **scr;
  int            true_speed;

  true_speed = this->scr_master->set_speed (this->scr_master, speed);
  
  this->speed = true_speed;

  for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
    if (*scr) (*scr)->set_speed(*scr, true_speed);

  return true_speed;
}


static void metronom_set_audio_rate (metronom_t *this, int64_t pts_per_smpls) {
  pthread_mutex_lock (&this->lock);

  this->pts_per_smpls = pts_per_smpls;

  pthread_mutex_unlock (&this->lock);

#ifdef LOG
  printf ("metronom: %lld pts per %d samples\n", pts_per_smpls, AUDIO_SAMPLE_NUM);
#endif

}

static int64_t metronom_got_spu_packet (metronom_t *this, int64_t pts) {
  int64_t vpts;

  pthread_mutex_lock (&this->lock);

  vpts = pts + this->vpts_offset;
  
  /* no vpts going backwards please */
  if( vpts < this->spu_vpts )
    vpts = this->spu_vpts;
  
  this->spu_vpts = vpts;
  
  pthread_mutex_unlock (&this->lock);
  return vpts;
}

static void metronom_handle_video_discontinuity (metronom_t *this, int type,
						 int64_t disc_off) {
  pthread_mutex_lock (&this->lock);

  this->video_discontinuity_count++;
  pthread_cond_signal (&this->video_discontinuity_reached);
  
  printf ("metronom: video discontinuity #%d, type is %d, disc_off is %lld\n",
	  this->video_discontinuity_count, type, disc_off);
  
  if (this->have_audio) {
    while (this->audio_discontinuity_count <
	   this->video_discontinuity_count) {

      printf ("metronom: waiting for audio discontinuity #%d\n",
	      this->video_discontinuity_count);

      pthread_cond_wait (&this->audio_discontinuity_reached, &this->lock);
    }
  }
  
  if ( this->video_vpts < this->clock->get_current_time(this->clock) ||
       type == DISC_STREAMSTART || type == DISC_STREAMSEEK ) {
    this->video_vpts = PREBUFFER_PTS_OFFSET + this->clock->get_current_time(this->clock);
    printf ("metronom: video vpts adjusted with prebuffer to %lld\n", this->video_vpts);
  }
  if ( this->audio_vpts < this->clock->get_current_time(this->clock) ||
       type == DISC_STREAMSTART || type == DISC_STREAMSEEK ) {
    this->audio_vpts = PREBUFFER_PTS_OFFSET + this->clock->get_current_time(this->clock);
    printf ("metronom: audio vpts adjusted with prebuffer to %lld\n", this->audio_vpts);
  }
  
#ifdef LOG
  printf ("metronom: video_vpts: %lld, audio_vpts: %lld\n", this->video_vpts, this->audio_vpts);
#endif

  switch (type) {
  case DISC_STREAMSTART:
#ifdef LOG
    printf ("metronom: DISC_STREAMSTART\n");
#endif
    if (this->video_vpts > this->audio_vpts)
      this->vpts_offset = this->audio_vpts = this->video_vpts;
    else
      this->vpts_offset = this->video_vpts = this->audio_vpts;
    this->force_audio_jump        = 1;
    this->force_video_jump        = 1;
    this->video_drift             = 0;
    break;
  case DISC_ABSOLUTE:
#ifdef LOG
    printf ("metronom: DISC_ABSOLUTE\n");
#endif
    this->vpts_offset             = this->video_vpts - disc_off;
    this->force_audio_jump        = 0;
    this->force_video_jump        = 0;
    break;
  case DISC_RELATIVE:
#ifdef LOG
    printf ("metronom: DISC_RELATIVE\n");
#endif
    this->vpts_offset             = this->vpts_offset - disc_off;
    this->force_audio_jump        = 0;
    this->force_video_jump        = 0;
    break;
  case DISC_STREAMSEEK:
#ifdef LOG
    printf ("metronom: DISC_STREAMSEEK\n");
#endif
    this->vpts_offset             = this->video_vpts - disc_off;
    this->force_audio_jump        = 1;
    this->force_video_jump        = 1;
    this->video_drift             = 0;
    break;
  }
  
  this->last_video_pts = 0;
  this->discontinuity_handled_count++;
  pthread_cond_signal (&this->video_discontinuity_reached);

  pthread_mutex_unlock (&this->lock);
}

static void metronom_got_video_frame (metronom_t *this, vo_frame_t *img) {

  int64_t vpts;
  int64_t pts = img->pts;
  int64_t diff;
  
  pthread_mutex_lock (&this->lock);

#ifdef LOG
  printf("metronom: got_video_frame pts = %lld\n", pts );
#endif

  this->img_cpt++;

  if (pts) {

    /*
     * Compute img duration if it's not provided by the decoder
     * example: mpeg streams with an invalid frame rate
     */
    if (!img->duration) {
      if (this->last_video_pts && this->img_cpt) {
        this->img_duration = (pts - this->last_video_pts) / this->img_cpt;
#ifdef LOG
        printf("metronom: computed frame_duration = %lld\n", this->img_duration );
#endif
      }
      this->img_cpt = 0;
      this->last_video_pts = pts;
      img->duration = this->img_duration;
    } else {
      this->img_duration = img->duration;  
    }
  
  
    /*
     * compare predicted (this->video_vpts) and given (pts+vpts_offset)
     * pts values - hopefully they will be the same
     * if not, for small diffs try to interpolate
     *         for big diffs: jump
     */
    
    vpts = pts + this->vpts_offset;

    diff = this->video_vpts - vpts;

#ifdef LOG
    printf ("metronom: video diff is %lld (predicted %lld, given %lld)\n",
	    diff, this->video_vpts, vpts);
#endif

    if ((abs (diff) > VIDEO_DRIFT_TOLERANCE) || (this->force_video_jump)) {
      this->force_video_jump = 0;
      this->video_vpts       = vpts;
      this->video_drift      = 0;
      
      printf ("metronom: video jump\n");

    } else {

      this->video_drift = diff;
      this->video_drift_step = diff / 30;
      /* this will fix video drift with a constant compensation each
	 frame for about 1 second of video.  */

#ifdef LOG
      if (diff)
        printf ("metronom: video drift, drift is %lld\n", this->video_drift);
#endif
    }
  } else {
    if (!img->duration) {
      img->duration = this->img_duration;
    } else {
      this->img_duration = img->duration;  
    }
  }

  
  img->vpts = this->video_vpts + this->av_offset;

#ifdef LOG
  printf ("metronom: video vpts for %10lld : %10lld (duration:%d drift:%lld step:%lld)\n", 
	  pts, this->video_vpts, img->duration, this->video_drift, this->video_drift_step );
#endif
  
  if( this->video_drift * this->video_drift_step > 0 )
  {
    img->duration -= this->video_drift_step;

    this->video_drift -= this->video_drift_step;
  }
  
  this->video_vpts += img->duration;

  pthread_mutex_unlock (&this->lock);
}

static void metronom_handle_audio_discontinuity (metronom_t *this, int type,
						 int64_t disc_off) {

  pthread_mutex_lock (&this->lock);
    
  this->audio_discontinuity_count++;
  pthread_cond_signal (&this->audio_discontinuity_reached);
  
  printf ("metronom: audio discontinuity #%d, type is %d, disc_off %lld\n",
	  this->audio_discontinuity_count, type, disc_off);
  
  /* next_vpts_offset, in_discontinuity is handled in expect_video_discontinuity */
  while ( this->audio_discontinuity_count >
	  this->discontinuity_handled_count ) {

    printf ("metronom: waiting for in_discontinuity update #%d\n", 
	    this->audio_discontinuity_count);

    pthread_cond_wait (&this->video_discontinuity_reached, &this->lock);
  }

  this->audio_samples = 0;
  this->audio_drift_step = 0;
  
  pthread_mutex_unlock (&this->lock);
}

static int64_t metronom_got_audio_samples (metronom_t *this, int64_t pts, 
					   int nsamples) {

  int64_t vpts;
  int64_t diff;

#ifdef LOG  
  printf ("metronom: got %d audio samples, pts is %lld\n", nsamples, pts);
#endif

  pthread_mutex_lock (&this->lock);

  if (pts) {
    vpts = pts + this->vpts_offset;
    diff = this->audio_vpts - vpts;

    /* compare predicted and given vpts */
    if((abs(diff) > AUDIO_DRIFT_TOLERANCE) || (this->force_audio_jump)) {
      this->force_audio_jump = 0;
      this->audio_vpts       = vpts;
      this->audio_drift_step = 0;
      printf("metronom: audio jump, diff=%lld\n", diff);
    }
    else {
      if( this->audio_samples ) {
        /* calculate drift_step to recover vpts errors */
#ifdef LOG  
        printf("metronom: audio diff = %lld \n", diff );
#endif
        diff *= AUDIO_SAMPLE_NUM;
        diff /= this->audio_samples * 4;
        
        /* drift_step is not allowed to change rate by more than 25% */
        if( diff > this->pts_per_smpls/4 )
          diff = this->pts_per_smpls/4;    
        if( diff < -this->pts_per_smpls/4 )
          diff = -this->pts_per_smpls/4;
        
        this->audio_drift_step = diff;
                
#ifdef LOG  
        printf("metronom: audio_drift = %lld, pts_per_smpls = %lld\n", diff,
                this->pts_per_smpls );
#endif
      }
    }
    this->audio_samples = 0;
  }
  vpts = this->audio_vpts;

  /* drift here is caused by streams where nominal sample rate differs from 
   * the rate of which pts increments. fixing the audio_vpts won't do us any
   * good because sound card won't play it faster or slower just because
   * we want. however, adding the error to the vpts_offset will force video
   * to change it's frame rate to keep in sync with us.
   */
#if OLD_DRIFT_CORRECTION
  this->audio_vpts += nsamples * (this->pts_per_smpls-this->audio_drift_step)
                      / AUDIO_SAMPLE_NUM;
  this->audio_samples += nsamples;
#else
  this->audio_vpts += nsamples * this->pts_per_smpls / AUDIO_SAMPLE_NUM;
  this->audio_samples += nsamples;
  this->vpts_offset += nsamples * this->audio_drift_step / AUDIO_SAMPLE_NUM;
#endif                 
                        
#ifdef LOG
  printf ("metronom: audio vpts for %10lld : %10lld\n", pts, vpts);
#endif

  pthread_mutex_unlock (&this->lock);

  return vpts;
}

static void metronom_set_option (metronom_t *this, int option, int64_t value) {

  pthread_mutex_lock (&this->lock);

  switch (option) {
  case METRONOM_AV_OFFSET:
    this->av_offset = value;
    printf ("metronom: av_offset=%lld pts\n", this->av_offset);
    break;
  case METRONOM_ADJ_VPTS_OFFSET:
#if OLD_DRIFT_CORRECTION
    this->vpts_offset += value;
#else
    this->audio_vpts += value;
#endif

/*#ifdef LOG*/
    /* that message should be rare, please report otherwise.
     * when xine is in some sort of "steady state" hearing it
     * once in a while means a small sound card drift (or system
     * clock drift -- who knows?). nothing to worry about.
     */
    printf ("metronom: fixing sound card drift by %lld pts\n", value );
/*#endif*/
    break;
  default:
    printf ("metronom: unknown option in set_option: %d\n",
	    option);
  }

  pthread_mutex_unlock (&this->lock);
}

static void metronom_clock_set_option (metronom_clock_t *this,
					int option, int64_t value) {

  pthread_mutex_lock (&this->lock);

  switch (option) {
  case CLOCK_SCR_ADJUSTABLE:
    this->scr_adjustable = value;
    break;
  default:
    printf ("metronom: unknown option in set_option: %d\n",
	    option);
  }

  pthread_mutex_unlock (&this->lock);
}

static int64_t metronom_get_option (metronom_t *this, int option) {
  switch (option) {
  case METRONOM_AV_OFFSET:
    return this->av_offset;
  case METRONOM_FRAME_DURATION:
    return this->img_duration;
  }
  printf ("metronom: unknown option in get_option: %d\n",
	  option);
  return 0;
}

static int64_t metronom_clock_get_option (metronom_clock_t *this, int option) {
  switch (option) {
  case CLOCK_SCR_ADJUSTABLE:
    return this->scr_adjustable;
  }
  printf ("metronom: unknown option in get_option: %d\n",
	  option);
  return 0;
}

static scr_plugin_t* get_master_scr(metronom_clock_t *this) {
  int select = -1, maxprio = 0, i;

  /* find the SCR provider with the highest priority */
  for (i=0; i<MAX_SCR_PROVIDERS; i++) if (this->scr_list[i]) {
    scr_plugin_t *scr = this->scr_list[i];
    
    if (maxprio < scr->get_priority(scr)) {
      select = i;
      maxprio = scr->get_priority(scr);
    }
  }
  if (select < 0) {
    printf ("metronom: panic - no scr provider found!\n");
    return NULL;
  }
  return this->scr_list[select];
}

static int metronom_register_scr (metronom_clock_t *this, scr_plugin_t *scr) {
  int i;

  if (scr->interface_version != 2) return -1;

  for (i=0; i<MAX_SCR_PROVIDERS; i++)
    if (this->scr_list[i] == NULL) break;
  if (i >= MAX_SCR_PROVIDERS)
    return -1; /* No free slot available */

  scr->clock = this;
  this->scr_list[i] = scr;
  this->scr_master = get_master_scr(this);
  return 0;
}

static void metronom_unregister_scr (metronom_clock_t *this, scr_plugin_t *scr) {
  int i;
  int64_t time;

  /* never unregister scr_list[0]! */
  for (i=1; i<MAX_SCR_PROVIDERS; i++)
    if (this->scr_list[i] == scr) 
      break;

  if (i >= MAX_SCR_PROVIDERS)
    return; /* Not found */
    
  this->scr_list[i] = NULL;
  time = this->get_current_time(this);
    
  /* master could have been adjusted, others must follow now */
  for (i=0; i<MAX_SCR_PROVIDERS; i++)
    if (this->scr_list[i]) this->scr_list[i]->adjust(this->scr_list[i], time);
  
  this->scr_master = get_master_scr(this);
}

static int metronom_sync_loop (metronom_clock_t *this) {

  struct timeval tv;
  struct timespec ts;
  scr_plugin_t** scr;
  int64_t        pts;
  
  while (this->thread_running) {
    /* synchronise every 5 seconds */
    pthread_mutex_lock (&this->lock);

    pts = this->scr_master->get_current(this->scr_master);
    
    for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
      if (*scr && *scr != this->scr_master) (*scr)->adjust(*scr, pts);

    gettimeofday(&tv, NULL);
    ts.tv_sec  = tv.tv_sec + 5;
    ts.tv_nsec = tv.tv_usec * 1000;
    pthread_cond_timedwait (&this->cancel, &this->lock, &ts);

    pthread_mutex_unlock (&this->lock);
  }
  return 0;
}

static void metronom_exit (metronom_t *this) {

  pthread_mutex_destroy (&this->lock);
  pthread_cond_destroy (&this->video_discontinuity_reached);
  pthread_cond_destroy (&this->audio_discontinuity_reached);

  free (this);
}

static void metronom_clock_exit (metronom_clock_t *this) {

  scr_plugin_t** scr;

  this->thread_running = 0;
  
  pthread_mutex_lock (&this->lock);
  pthread_cond_signal (&this->cancel);
  pthread_mutex_unlock (&this->lock);

  pthread_join (this->sync_thread, NULL);

  pthread_mutex_destroy (&this->lock);
  pthread_cond_destroy (&this->cancel);

  for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
    if (*scr) (*scr)->exit(*scr);

  free (this->scr_list);
  free (this);
}


metronom_t * metronom_init (int have_audio, xine_stream_t *stream) {

  metronom_t *this = xine_xmalloc (sizeof (metronom_t));

  this->stream               = stream;
  this->clock                = stream->xine->clock;
  this->set_audio_rate       = metronom_set_audio_rate;
  this->got_video_frame      = metronom_got_video_frame;
  this->got_audio_samples    = metronom_got_audio_samples;
  this->got_spu_packet       = metronom_got_spu_packet;
  this->handle_audio_discontinuity = metronom_handle_audio_discontinuity;
  this->handle_video_discontinuity = metronom_handle_video_discontinuity;
  this->set_option           = metronom_set_option;
  this->get_option           = metronom_get_option;
  this->exit                 = metronom_exit;

  pthread_mutex_init (&this->lock, NULL);

  this->av_offset                   = 0;
  this->vpts_offset                 = 0;

  /* initialize video stuff */

  this->video_vpts                  = PREBUFFER_PTS_OFFSET;
  this->video_drift                 = 0;
  this->video_drift_step            = 0;
  this->video_discontinuity_count   = 0;
  this->discontinuity_handled_count = 0;
  pthread_cond_init (&this->video_discontinuity_reached, NULL);
  this->img_duration              = 3000;
  this->img_cpt                   = 0;
  this->last_video_pts            = 0;
  
  
  /* initialize audio stuff */

  this->have_audio                  = have_audio;
  this->audio_vpts                  = PREBUFFER_PTS_OFFSET;
  this->audio_discontinuity_count   = 0;
  pthread_cond_init (&this->audio_discontinuity_reached, NULL);
    

  return this;
}


metronom_clock_t *metronom_clock_init(void)
{
  metronom_clock_t *this = (metronom_clock_t *)malloc(sizeof(metronom_clock_t));
  int err;
  
  this->set_option           = metronom_clock_set_option;
  this->get_option           = metronom_clock_get_option;
  this->start_clock          = metronom_start_clock;
  this->stop_clock           = metronom_stop_clock;
  this->resume_clock         = metronom_resume_clock;
  this->get_current_time     = metronom_get_current_time;
  this->adjust_clock         = metronom_adjust_clock;
  this->set_speed            = metronom_set_speed;
  this->register_scr         = metronom_register_scr;
  this->unregister_scr       = metronom_unregister_scr;
  this->exit                 = metronom_clock_exit;
  
  this->scr_adjustable = 1;
  this->scr_list = calloc(MAX_SCR_PROVIDERS, sizeof(void*));
  this->register_scr(this, unixscr_init());
  
  pthread_mutex_init (&this->lock, NULL);
  pthread_cond_init (&this->cancel, NULL);
  
  this->thread_running       = 1;

  if ((err = pthread_create(&this->sync_thread, NULL,
      			    (void*(*)(void*)) metronom_sync_loop, this)) != 0)
    printf ("metronom: cannot create sync thread (%s)\n",
	    strerror(err));

  return this;
}
