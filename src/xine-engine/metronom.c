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
 * $Id: metronom.c,v 1.76 2002/03/26 01:47:17 miguelfreitas Exp $
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
  
  unixscr_set_speed (&this->scr, SPEED_NORMAL);
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
  
  unixscr_set_speed (&this->scr, SPEED_PAUSE);
  printf("xine-scr_init: complete\n");

  return &this->scr;
}
 

/*
 * ****************************************
 *       master clock feature
 * ****************************************
 */


static void metronom_start_clock (metronom_t *this, int64_t pts) {
  scr_plugin_t** scr;

  printf ("metronom: start_clock (at %lld)\n", pts);

  for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
    if (*scr) (*scr)->start(*scr, pts);
}


static int64_t metronom_get_current_time (metronom_t *this) {
  return this->scr_master->get_current(this->scr_master);
}


static void metronom_stop_clock(metronom_t *this) {
  scr_plugin_t** scr;
  for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
    if (*scr) (*scr)->set_speed(*scr, SPEED_PAUSE);
}

static void metronom_resume_clock(metronom_t *this) {
  scr_plugin_t** scr;
  for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
    if (*scr) (*scr)->set_speed(*scr, SPEED_NORMAL);
}



static void metronom_adjust_clock(metronom_t *this, int64_t desired_pts) {
  if (this->scr_adjustable)
    this->scr_master->adjust(this->scr_master, desired_pts);
}

static int metronom_set_speed (metronom_t *this, int speed) {

  scr_plugin_t **scr;
  int            true_speed;

  true_speed = this->scr_master->set_speed (this->scr_master, speed);

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

static int64_t metronom_got_spu_packet (metronom_t *this, int64_t pts,
					int64_t duration) {
  int64_t vpts;
  
  pthread_mutex_lock (&this->lock);
  
  if (pts) {
    this->spu_vpts=pts;
  } else {
    pts=this->spu_vpts;
  }

  if ( !this->in_discontinuity ) {
    vpts = pts + this->vpts_offset;
  } else {
    vpts = 0;      
  }
  
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
  
    if ( this->video_vpts < metronom_get_current_time(this) ) {
      this->video_vpts = PREBUFFER_PTS_OFFSET + metronom_get_current_time(this);
      printf ("metronom: video vpts adjusted with prebuffer to %lld\n", this->video_vpts);
    }
    
    if (this->video_vpts < this->audio_vpts) {
      this->video_vpts = this->audio_vpts;
      printf ("metronom: video vpts adjusted to %lld\n", this->video_vpts);
    }
  }

  if (this->in_discontinuity) 
    this->vpts_offset = this->next_vpts_offset;

  switch (type) {
  case DISC_STREAMSTART:
    this->vpts_offset      = this->video_vpts;
    this->in_discontinuity = 0;
    break;
  case DISC_ABSOLUTE:
    this->next_vpts_offset = this->video_vpts - disc_off;
    this->in_discontinuity = 30;
    break;
  case DISC_RELATIVE:
    this->next_vpts_offset = this->vpts_offset - disc_off;
    this->in_discontinuity = 30;
    break;
  }
  pthread_mutex_unlock (&this->lock);
}

static void metronom_got_video_frame (metronom_t *this, vo_frame_t *img) {

  int64_t vpts;
  int64_t pts = img->pts;
  
  pthread_mutex_lock (&this->lock);

  if (this->in_discontinuity) {
    this->in_discontinuity--;

    if (!this->in_discontinuity)
      this->vpts_offset = this->next_vpts_offset;
    else
      pts = 0; /* ignore pts during discontinuities */
  }
  
  if (pts) {
    int64_t diff;

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

    if (abs (diff) > VIDEO_DRIFT_TOLERANCE) {

      this->video_vpts  = vpts;
      this->video_drift = 0;

#ifdef LOG
      printf ("metronom: video jump\n");
#endif
	
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
  
  while ( this->audio_discontinuity_count >
	  this->video_discontinuity_count ) {

    printf ("metronom: waiting for video_discontinuity #%d\n", 
	    this->audio_discontinuity_count);

    pthread_cond_wait (&this->video_discontinuity_reached, &this->lock);
  }
  
  if ( this->audio_vpts < metronom_get_current_time(this) ) {
    this->audio_vpts = PREBUFFER_PTS_OFFSET + metronom_get_current_time(this);
    printf ("metronom: audio vpts adjusted with prebuffer to %lld\n", this->audio_vpts);
  }
  
  if ( this->audio_vpts < this->video_vpts ) {
    this->audio_vpts = this->video_vpts;
    printf ("metronom: audio vpts adjusted to %lld\n", this->audio_vpts);
  }
    
  /* next_vpts_offset, in_discontinuity is handled in expect_video_discontinuity */

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

  if (this->in_discontinuity) 
    pts = 0; /* ignore pts during discontinuities */
  
  if (pts) {
    vpts = pts + this->vpts_offset;
    diff = this->audio_vpts - vpts;
    
    /* compare predicted and given vpts */
    if( abs(diff) > AUDIO_DRIFT_TOLERANCE ) {
      this->audio_vpts = vpts;
      this->audio_drift_step = 0;
      printf("metronom: audio jump\n");
    }
    else {
      if( this->audio_samples ) {
        /* calculate drift_step to recover vpts errors */
#ifdef LOG  
        printf("audio diff = %lld ", diff );
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
        printf("audio_drift = %lld, pts_per_smpls = %lld\n", diff,
                this->pts_per_smpls );
#endif
      }
    }
    this->audio_samples = 0;
  }
  vpts = this->audio_vpts;

  this->audio_vpts += nsamples * (this->pts_per_smpls-this->audio_drift_step)
                      / AUDIO_SAMPLE_NUM;
  this->audio_samples += nsamples;
  
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
  case METRONOM_SCR_ADJUSTABLE:
    this->scr_adjustable = value;
    break;
  case METRONOM_ADJ_VPTS_OFFSET:
    this->vpts_offset += value;
/*#ifdef LOG*/
    printf ("metronom: adjusting vpts_offset by %lld\n", value );
/*#endif*/
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
  case METRONOM_SCR_ADJUSTABLE:
    return this->scr_adjustable;
  }
  printf ("metronom: unknown option in get_option: %d\n",
	  option);
  return 0;
}

static scr_plugin_t* get_master_scr(metronom_t *this) {
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

static int metronom_register_scr (metronom_t *this, scr_plugin_t *scr) {
  int i;

  if (scr->interface_version != 2) return -1;

  for (i=0; i<MAX_SCR_PROVIDERS; i++)
    if (this->scr_list[i] == NULL) break;
  if (i >= MAX_SCR_PROVIDERS)
    return -1; /* No free slot available */

  scr->metronom = this;
  this->scr_list[i] = scr;
  this->scr_master = get_master_scr(this);
  return 0;
}

static void metronom_unregister_scr (metronom_t *this, scr_plugin_t *scr) {
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

static int metronom_sync_loop (metronom_t *this) {

  struct timeval tv;
  struct timespec ts;
  scr_plugin_t** scr;
  int64_t        pts;
  
  while (((xine_t*)this->xine)->status != XINE_QUIT) {
    pts = this->scr_master->get_current(this->scr_master);
    
    for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
      if (*scr && *scr != this->scr_master) (*scr)->adjust(*scr, pts);

    /* synchronise every 5 seconds */
    pthread_mutex_lock (&this->lock);

    gettimeofday(&tv, NULL);
    ts.tv_sec  = tv.tv_sec + 5;
    ts.tv_nsec = tv.tv_usec * 1000;
    pthread_cond_timedwait (&this->cancel, &this->lock, &ts);

    pthread_mutex_unlock (&this->lock);
  }
  return 0;
}

static void metronom_exit (metronom_t *this) {

  scr_plugin_t** scr;

  pthread_mutex_lock (&this->lock);
  pthread_cond_signal (&this->cancel);
  pthread_mutex_unlock (&this->lock);

  pthread_join (this->sync_thread, NULL);

  pthread_mutex_destroy (&this->lock);
  pthread_cond_destroy (&this->video_discontinuity_reached);
  pthread_cond_destroy (&this->audio_discontinuity_reached);
  pthread_cond_destroy (&this->cancel);

  for (scr = this->scr_list; scr < this->scr_list+MAX_SCR_PROVIDERS; scr++)
    if (*scr) (*scr)->exit(*scr);

  free (this->scr_list);
  free (this);
}


metronom_t * metronom_init (int have_audio, void *xine) {

  metronom_t *this = xine_xmalloc (sizeof (metronom_t));
  int         err;

  this->xine                 = xine;
  this->set_audio_rate       = metronom_set_audio_rate;
  this->got_video_frame      = metronom_got_video_frame;
  this->got_audio_samples    = metronom_got_audio_samples;
  this->got_spu_packet       = metronom_got_spu_packet;
  this->handle_audio_discontinuity = metronom_handle_audio_discontinuity;
  this->handle_video_discontinuity = metronom_handle_video_discontinuity;
  this->set_option           = metronom_set_option;
  this->get_option           = metronom_get_option;
  this->start_clock          = metronom_start_clock;
  this->stop_clock           = metronom_stop_clock;
  this->resume_clock         = metronom_resume_clock;
  this->get_current_time     = metronom_get_current_time;
  this->adjust_clock         = metronom_adjust_clock;
  this->register_scr         = metronom_register_scr;
  this->unregister_scr       = metronom_unregister_scr;
  this->set_speed            = metronom_set_speed;
  this->exit                 = metronom_exit;

  this->scr_adjustable = 1;
  this->scr_list = calloc(MAX_SCR_PROVIDERS, sizeof(void*));
  this->register_scr(this, unixscr_init());

  pthread_mutex_init (&this->lock, NULL);
  pthread_cond_init (&this->cancel, NULL);

  if ((err = pthread_create(&this->sync_thread, NULL,
      			    (void*(*)(void*)) metronom_sync_loop, this)) != 0)
    printf ("metronom: cannot create sync thread (%s)\n",
	    strerror(err));

  this->av_offset                 = 0;
  this->in_discontinuity          = 0;
  this->vpts_offset               = 0;
  this->next_vpts_offset          = 0;

  /* initialize video stuff */

  this->video_vpts                = PREBUFFER_PTS_OFFSET;
  this->video_drift               = 0;
  this->video_drift_step          = 0;
  this->video_discontinuity_count = 0;
  pthread_cond_init (&this->video_discontinuity_reached, NULL);

  /* initialize audio stuff */

  this->have_audio                = have_audio;
  this->audio_vpts                = PREBUFFER_PTS_OFFSET;
  this->audio_discontinuity_count = 0;
  pthread_cond_init (&this->audio_discontinuity_reached, NULL);
    

  return this;
}

