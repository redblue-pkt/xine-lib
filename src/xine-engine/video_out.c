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
 * $Id: video_out.c,v 1.41 2001/09/06 14:09:37 jkeil Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "video_out.h"
#include "utils.h"
#include "monitor.h"

#define NUM_FRAME_BUFFERS     15

struct img_buf_fifo_s {
  vo_frame_t        *first;
  vo_frame_t        *last;
  int                num_buffers;

  pthread_mutex_t    mutex;
  pthread_cond_t     bNotEmpty;
} ;


static img_buf_fifo_t *vo_new_img_buf_queue () {

  img_buf_fifo_t *queue;

  queue = (img_buf_fifo_t *) xmalloc (sizeof (img_buf_fifo_t));
  if( queue ) {
    queue->first       = NULL;
    queue->last        = NULL;
    queue->num_buffers = 0;
    pthread_mutex_init (&queue->mutex, NULL);
    pthread_cond_init  (&queue->bNotEmpty, NULL);
  }
  return queue;
}

static void vo_append_to_img_buf_queue (img_buf_fifo_t *queue, 
					vo_frame_t *img) {

  pthread_mutex_lock (&queue->mutex);

  img->next = NULL;

  if (!queue->first) {
    queue->first = img;
    queue->last  = img;
    queue->num_buffers = 0;
  } 
  else if (queue->last) {
    queue->last->next = img;
    queue->last  = img;
  }

  queue->num_buffers++;

  pthread_cond_signal (&queue->bNotEmpty);
  pthread_mutex_unlock (&queue->mutex);
}

static vo_frame_t *vo_remove_from_img_buf_queue (img_buf_fifo_t *queue) {
  vo_frame_t *img;

  pthread_mutex_lock (&queue->mutex);

  while (!queue->first) {
    /* printf ("video_out: queue %d empty...\n", queue); */
    pthread_cond_wait (&queue->bNotEmpty, &queue->mutex);
  }

  img = queue->first;

  if (img) {
    queue->first = img->next;
    img->next = NULL;
    if (!queue->first) {
      queue->last = NULL;
      queue->num_buffers = 0;
      pthread_cond_init  (&queue->bNotEmpty, NULL);
    }
    else {
      queue->num_buffers--;
    }
  }
    
  pthread_mutex_unlock (&queue->mutex);

  return img;
}

static void vo_set_timer (uint32_t video_step) {
  struct itimerval tval;

  tval.it_interval.tv_sec  = 0;
  tval.it_interval.tv_usec = video_step*100000/90000;
  tval.it_value.tv_sec     = 0;
  tval.it_value.tv_usec    = video_step*100000/90000;

  if (setitimer(ITIMER_REAL, &tval, NULL)) {
    printf ("vo_set_timer: setitimer failed :");
  }
}

void video_timer_handler (int hubba) {
#if	!HAVE_SIGACTION
  signal (SIGALRM, video_timer_handler);
#endif
}

static void *video_out_loop (void *this_gen) {

  uint32_t           cur_pts;
  int                pts_absdiff, diff, absdiff, pts=0;
  vo_frame_t        *img;
  uint32_t           video_step, video_step_new;
  vo_instance_t     *this = (vo_instance_t *) this_gen;
  sigset_t           vo_mask;
  /*
  int                dummysignum;
  */

  /* printf ("%d video_out start\n", getpid());  */
  /*
  sigemptyset(&vo_mask);
  sigaddset(&vo_mask, SIGALRM);
  pthread_sigmask(SIG_UNBLOCK, &vo_mask, NULL);
  */

  
  sigemptyset(&vo_mask);
  sigaddset(&vo_mask, SIGALRM);
  if (sigprocmask (SIG_UNBLOCK,  &vo_mask, NULL)) {
    printf ("video_out: sigprocmask failed.\n");
  }
#if	HAVE_SIGACTION
  {
    struct sigaction   sig_act;
    memset (&sig_act, 0, sizeof(sig_act));
    sig_act.sa_handler = video_timer_handler;
    sigaction (SIGALRM, &sig_act, NULL);
  }
#else
  signal (SIGALRM, video_timer_handler);
#endif

  video_step = this->metronom->get_video_rate (this->metronom);
  vo_set_timer (video_step); 


  while (this->video_loop_running) {

    /* sigwait(&vo_mask, &dummysignum); */ /* wait for next timer tick */
    pause (); 

    profiler_start_count (2);

    video_step_new = this->metronom->get_video_rate (this->metronom);
    if (video_step_new != video_step) {
      video_step = video_step_new;
      vo_set_timer (video_step); 
    }
    pts_absdiff = 1000000;

    cur_pts = this->metronom->get_current_time (this->metronom);
    
    xprintf (VERBOSE|VIDEO, "video_out : video loop iteration at audio pts %d\n", cur_pts);
    /*printf ("video_out : video loop iteration at audio pts %d\n", cur_pts);
    fflush (stdout); */
    
    img = this->display_img_buf_queue->first;
    
    if (!img) {
      profiler_stop_count (2);
      continue;
    }
    
    /*
     * throw away expired frames
     */
    
    do {
      pts = img->PTS;
      diff = cur_pts - pts;
      absdiff = abs(diff);
      
      if (diff >this->pts_per_half_frame) {
    
	xprintf (VERBOSE|VIDEO, "video_out : throwing away image with pts %d because "
		 "it's too old (diff : %d > %d).\n",pts,diff,
		 this->pts_per_half_frame);
	
	/*
	fprintf (stderr,
		 "video_out : throwing away image with pts %d because "
		 "it's too old (diff : %d > %d).\n",pts,diff,
		 this->pts_per_half_frame);
		 */

	this->num_frames_discarded++;

	img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);
	pthread_mutex_lock (&img->mutex);

	img->bDisplayLock = 0;

	if (!img->bDecoderLock) 
	  vo_append_to_img_buf_queue (this->free_img_buf_queue, img);

	pthread_mutex_unlock (&img->mutex);

	img = this->display_img_buf_queue->first;

	if (!img)
	  diff = -1;
      }
    } while (diff >this->pts_per_half_frame); 

    /*
     * time to display frame 0 ?
     */

    /*
    printf ("video_out: diff %d\n", diff);
    fflush(stdout);
    */

    if (diff<0) {
      profiler_stop_count (2);
      continue;
    }


    /*
     * remove frame from display queue and show it
     */
    
    xprintf (VERBOSE|VIDEO, "video_out : displaying image with pts = %d (diff=%d)\n", pts, diff);
    
    img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);

    if (!img) {
      profiler_stop_count (2);
      continue;
    }

    pthread_mutex_lock (&img->mutex);
    img->bDriverLock = 1;
    if (!img->bDisplayLock)
      xprintf (VERBOSE|VIDEO, "video_out: ALERT! frame was not locked for display queue\n");
    img->bDisplayLock = 0;
    pthread_mutex_unlock (&img->mutex);

    xprintf (VERBOSE|VIDEO, "video_out : passing to video driver, image with pts = %d\n", pts);

    if (this->overlay_source) {
      /* This is the only way for the spu decoder to get pts values
       * for flushing it's buffers. So don't remove it! */
      vo_overlay_t *ovl;
      
      profiler_start_count (4);

      ovl = this->overlay_source->get_overlay (this->overlay_source, img->PTS);
      if (ovl && this->driver->overlay_blend)
	this->driver->overlay_blend (this->driver, img, ovl); 

      profiler_stop_count (4);
    }
    
    this->driver->display_frame (this->driver, img); 

    profiler_stop_count (2);
  }

  /*
   * throw away undisplayed frames
   */
  
  img = this->display_img_buf_queue->first;
  while (img) {
    
    img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);
    pthread_mutex_lock (&img->mutex);

    if (!img->bDecoderLock) 
      vo_append_to_img_buf_queue (this->free_img_buf_queue, img);

    img->bDisplayLock = 0;
    pthread_mutex_unlock (&img->mutex);

    img = this->display_img_buf_queue->first;
  }

  pthread_exit(NULL);
}

static uint32_t vo_get_capabilities (vo_instance_t *this) {
  return this->driver->get_capabilities (this->driver);
}

static void vo_open (vo_instance_t *this) {

  if (!this->video_loop_running) {
    this->video_loop_running = 1;

    pthread_create (&this->video_thread, NULL, video_out_loop, this) ; 
    printf ("video_out: thread created\n");
  } else
    printf ("video_out: vo_open : warning! video thread already running\n");

}

static vo_frame_t *vo_get_frame (vo_instance_t *this,
				 uint32_t width, uint32_t height,
				 int ratio, int format, uint32_t duration,
				 int flags) {

  vo_frame_t *img;

  /*
  printf ("video_out: get_frame %d x %d from queue %d\n", 
	  width, height, this->free_img_buf_queue);
  fflush(stdout);
  */

  if (this->pts_per_frame != duration) {
    this->pts_per_frame = duration;
    this->pts_per_half_frame = duration / 2;
    this->metronom->set_video_rate (this->metronom, duration);
  }

  img = vo_remove_from_img_buf_queue (this->free_img_buf_queue);

  pthread_mutex_lock (&img->mutex);
  img->bDisplayLock = 0;
  img->bDecoderLock = 1;
  img->bDriverLock  = 0;

  /* let driver ensure this image has the right format */

  this->driver->update_frame_format (this->driver, img, width, height, ratio, format, flags);

  pthread_mutex_unlock (&img->mutex);
  
  return img;
}

static void vo_close (vo_instance_t *this) {

  if (this->video_loop_running) {
    void *p;

    this->video_loop_running = 0;
    /*kill (0, SIGALRM);*/
    pthread_join (this->video_thread, &p);
  }
}

static void vo_free_img_buffers (vo_instance_t *this) {
  vo_frame_t *img; 

  while (this->free_img_buf_queue->first) {
    img = vo_remove_from_img_buf_queue (this->free_img_buf_queue);
    img->dispose (img);
  }

  while (this->display_img_buf_queue->first) {
    img = vo_remove_from_img_buf_queue (this->display_img_buf_queue) ;
    img->dispose (img);
  }
}

static void vo_exit (vo_instance_t *this) {

  vo_free_img_buffers (this);

  this->driver->exit (this->driver);
}

static void vo_frame_displayed (vo_frame_t *img) {

  pthread_mutex_lock (&img->mutex);

  img->bDriverLock = 0;

  if (!img->bDecoderLock) {    
    vo_append_to_img_buf_queue (img->instance->free_img_buf_queue, img);
  }

  pthread_mutex_unlock (&img->mutex);
}

static void vo_frame_free (vo_frame_t *img) {

  pthread_mutex_lock (&img->mutex);
  img->bDecoderLock = 0; 

  if (!img->bDisplayLock && !img->bDriverLock ) {
    vo_append_to_img_buf_queue (img->instance->free_img_buf_queue, img);
  }

  pthread_mutex_unlock (&img->mutex);
}

static int vo_frame_draw (vo_frame_t *img) {

  vo_instance_t *this = img->instance;
  int32_t        diff;
  uint32_t       cur_vpts;
  uint32_t       pic_vpts ;
  int            frames_to_skip;

  pic_vpts = this->metronom->got_video_frame (this->metronom, img->PTS);

  /*
  printf ("video_out: got image %d. vpts for picture is %d (pts was %d)\n", 
	  img, pic_vpts, img->PTS);
  */
  img->PTS = pic_vpts;
  this->num_frames_delivered++;

  xprintf (VERBOSE|VIDEO,"video_out: got image. vpts for picture is %d\n", pic_vpts);
  
  cur_vpts = this->metronom->get_current_time(this->metronom);

  diff = pic_vpts - cur_vpts;
  frames_to_skip = ((-1 * diff) / this->pts_per_frame + 3) * 2;

  xprintf (VERBOSE|VIDEO,"video_out:: delivery diff : %d\n",diff);

  if (cur_vpts>0) {

    if (diff<(-1 * this->pts_per_half_frame)) {

      this->num_frames_discarded++;
      xprintf (VERBOSE|VIDEO, "vo_frame_draw: rejected, %d frames to skip\n", frames_to_skip);

      pthread_mutex_lock (&img->mutex);
      img->bDisplayLock = 0;
      pthread_mutex_unlock (&img->mutex);

      vo_frame_displayed (img);

      return frames_to_skip;

    } 
  } /* else: we are probably in precaching mode */

  if (!img->bFrameBad) {
    /*
     * put frame into FIFO-Buffer
     */

    xprintf (VERBOSE|VIDEO, "frame is ok => appending to display buffer\n");

    pthread_mutex_lock (&img->mutex);
    img->bDisplayLock = 1;
    pthread_mutex_unlock (&img->mutex);
    
    vo_append_to_img_buf_queue (this->display_img_buf_queue, img);

  } else {
    this->num_frames_skipped++;

    pthread_mutex_lock (&img->mutex);
    img->bDisplayLock = 0;
    pthread_mutex_unlock (&img->mutex);
    
    vo_frame_displayed (img);
  }

  /*
   * performance measurement
   */
  
  if (this->num_frames_delivered>199) {
    fprintf (stderr, 
	     "%d frames delivered, %d frames skipped, %d frames discarded\n", 
            this->num_frames_delivered, this->num_frames_skipped, this->num_frames_discarded);

    this->num_frames_delivered = 0;
    this->num_frames_discarded = 0;
    this->num_frames_skipped   = 0;
  }
  
  return frames_to_skip;
}

static void vo_register_ovl_src (vo_instance_t *this, ovl_src_t *ovl_src)
{
  this->overlay_source = ovl_src;
  ovl_src->metronom = this->metronom;
}

static void vo_unregister_ovl_src (vo_instance_t *this, ovl_src_t *ovl_src)
{
  /* only remove the source if it is the same as registered */
  if (this->overlay_source == ovl_src)
    this->overlay_source = NULL;
}

vo_instance_t *vo_new_instance (vo_driver_t *driver, metronom_t *metronom) {

  vo_instance_t *this;
  int            i;

  this = xmalloc (sizeof (vo_instance_t)) ;
  this->driver                = driver;
  this->metronom              = metronom;

  this->open                  = vo_open;
  this->get_frame             = vo_get_frame;
  this->close                 = vo_close;
  this->exit                  = vo_exit;
  this->get_capabilities      = vo_get_capabilities;
  this->register_ovl_src      = vo_register_ovl_src;
  this->unregister_ovl_src    = vo_unregister_ovl_src;

  this->num_frames_delivered  = 0;
  this->num_frames_skipped    = 0;
  this->num_frames_discarded  = 0;
  this->free_img_buf_queue    = vo_new_img_buf_queue ();
  this->display_img_buf_queue = vo_new_img_buf_queue ();
  this->video_loop_running    = 0;
  this->pts_per_frame         = 0;
  this->pts_per_half_frame    = 0;

  for (i=0; i<NUM_FRAME_BUFFERS; i++) {
    vo_frame_t *img;

    img = driver->alloc_frame (driver) ;
    
    img->instance  = this;
    img->free      = vo_frame_free ;
    img->displayed = vo_frame_displayed;
    img->draw      = vo_frame_draw;

    vo_append_to_img_buf_queue (this->free_img_buf_queue,
				img);
  }

  return this;
}

