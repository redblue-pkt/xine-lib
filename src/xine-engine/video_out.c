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
 * $Id: video_out.c,v 1.62 2002/01/02 18:16:08 jkeil Exp $
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
#include "xine_internal.h"
#include "xineutils.h"

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_VIDEO, message, ##args);                 \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_VIDEO, message, ##args);                 \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_VIDEO, __VA_ARGS__);                     \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_VIDEO, __VA_ARGS__);                     \
    printf(__VA_ARGS__);                                             \
  }
#endif

/*
#define VIDEO_OUT_LOG
*/

#define NUM_FRAME_BUFFERS     15

struct img_buf_fifo_s {
  vo_frame_t        *first;
  vo_frame_t        *last;
  int                num_buffers;

  pthread_mutex_t    mutex;
  pthread_cond_t     not_empty;
} ;

static img_buf_fifo_t *vo_new_img_buf_queue () {

  img_buf_fifo_t *queue;

  queue = (img_buf_fifo_t *) xine_xmalloc (sizeof (img_buf_fifo_t));
  if( queue ) {
    queue->first       = NULL;
    queue->last        = NULL;
    queue->num_buffers = 0;
    pthread_mutex_init (&queue->mutex, NULL);
    pthread_cond_init  (&queue->not_empty, NULL);
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

  pthread_cond_signal (&queue->not_empty);
  pthread_mutex_unlock (&queue->mutex);
}

static vo_frame_t *vo_remove_from_img_buf_queue (img_buf_fifo_t *queue) {
  vo_frame_t *img;

  pthread_mutex_lock (&queue->mutex);

  while (!queue->first) {
    pthread_cond_wait (&queue->not_empty, &queue->mutex);
  }

  img = queue->first;

  if (img) {
    queue->first = img->next;
    img->next = NULL;
    if (!queue->first) {
      queue->last = NULL;
      queue->num_buffers = 0;
      pthread_cond_init  (&queue->not_empty, NULL);
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

/* send a buf to force video_decoder->flush */
static void video_out_send_decoder_flush( fifo_buffer_t *video_fifo ) {
  buf_element_t   *buf;

  if( !video_fifo )
    return;
    
  buf = video_fifo->buffer_pool_alloc (video_fifo);
  
  buf->type = BUF_CONTROL_FLUSH ;
  buf->PTS  = 0;
  buf->SCR  = 0;
  buf->input_pos = 0;
  buf->input_time = 0;

  video_fifo->put (video_fifo, buf);      

}



static void *video_out_loop (void *this_gen) {

  uint32_t           cur_pts;
  int                diff, absdiff, pts=0;
  vo_frame_t        *img, *img_backup;
  uint32_t           video_step, video_step_new;
  vo_instance_t     *this = (vo_instance_t *) this_gen;
  static int	     prof_video_out = -1;
  static int	     prof_spu_blend = -1;
  sigset_t           vo_mask;

  /* printf ("%d video_out start\n", getpid());  */

  if (prof_video_out == -1)
    prof_video_out = xine_profiler_allocate_slot ("video output");
  if (prof_spu_blend == -1)
    prof_spu_blend = xine_profiler_allocate_slot ("spu blend");

  img_backup    = NULL;
  this->still_counter = 0;

  /*
   * set up timer signal
   */
  
  sigemptyset(&vo_mask);
  sigaddset(&vo_mask, SIGALRM);
  if (sigprocmask (SIG_UNBLOCK,  &vo_mask, NULL)) {
    LOG_MSG(this->xine, _("video_out: sigprocmask failed.\n"));
  }
#if HAVE_SIGACTION
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

  /*
   * here it is - the big video output loop
   */

  while ((this->video_loop_running) ||
	 (!this->video_loop_running && this->display_img_buf_queue->first)) {

    /*
     * wait until it's time to display a frame
     */
 
    pause (); 

    video_step_new = this->metronom->get_video_rate (this->metronom);
    if (video_step_new != video_step) {
      video_step = video_step_new;
      vo_set_timer (video_step);
    }
    
    /*
     * now, look at the frame queue and decide which frame to display
     * or generate still frames if no frames are available
     */

    xine_profiler_start_count (prof_video_out);

    cur_pts = this->metronom->get_current_time (this->metronom);

#ifdef VIDEO_OUT_LOG
    printf ("video_out : video loop iteration at audio pts %d\n", cur_pts);
#endif
    
    img = this->display_img_buf_queue->first;

    /*
     * throw away expired frames
     */

    diff = 1000000;

    while (img && (diff >this->pts_per_half_frame)) {
      pts = img->PTS;
      diff = cur_pts - pts;
      absdiff = abs(diff);
      
      if (diff >this->pts_per_half_frame) {
	LOG_MSG(this->xine, _("video_out : throwing away image with pts %d because "
			      "it's too old (diff : %d > %d).\n"), 
		pts, diff, this->pts_per_half_frame);

	this->num_frames_discarded++;

	img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);
	pthread_mutex_lock (&img->mutex);

	img->display_locked = 0;

	/*
	 * last frame? back it up for 
	 * still frame creation
	 */

	if (img && !img->next) {
	  
	  if (img_backup) {
#ifdef VIDEO_OUT_LOG
	    printf("video_out : overwriting frame backup\n");
#endif
	    vo_append_to_img_buf_queue (this->free_img_buf_queue, img_backup);
	  }
	  
	  img_backup = img;
	} else {
	  if (!img->decoder_locked) 
	    vo_append_to_img_buf_queue (this->free_img_buf_queue, img);
	}

	pthread_mutex_unlock (&img->mutex);

	img = this->display_img_buf_queue->first;

	if (!img)
	  diff = -1;
      }
    } 

    /* 
     * still frame detection:
     */

    /* no frame? => still frame detection */

    if (!img) {

#ifdef VIDEO_OUT_LOG
      printf ("video_out : no frame\n");
#endif

      if (!this->xine->video_fifo->first || this->xine->video_in_discontinuity) {

	this->still_counter++;

	if (this->still_counter%8 == 0) {
#ifdef VIDEO_OUT_LOG
	  printf("video_out : sending decoder flush due to inactivity\n");
#endif
	  video_out_send_decoder_flush( this->xine->video_fifo );
	} 

	if (this->still_counter<8) {
#ifdef VIDEO_OUT_LOG
	  printf("video_out : no frame - waiting %d/8 frames\n", this->still_counter);
#endif
	  continue;
	}

	if (img_backup) {

#ifdef VIDEO_OUT_LOG
	  printf("video_out : generating still frame \n");
#endif

	  /* keep playing still frames */
	  img = this->duplicate_frame( this, img_backup );
	  img->display_locked = 1;
	  do {
	    img->PTS = this->metronom->got_video_frame(this->metronom, 0, 0);
	    pts = img->PTS;
	    diff = cur_pts - pts;
	    
	  } while (diff >this->pts_per_half_frame) ;

	  /*
	   * wait until it's time to display this still frame
	   */
	  
	  while (pts > cur_pts) {
	    xine_usec_sleep ( 10000 );
	    cur_pts = this->metronom->get_current_time (this->metronom);

#ifdef VIDEO_OUT_LOG
	    printf ("video_out: waiting until it's time to display this still frame\n");
#endif
	  }


        } else {
#ifdef VIDEO_OUT_LOG
	  printf ("video_out : no frame, but no backup frame\n");
#endif
	  continue;
	}


      } else {
#ifdef VIDEO_OUT_LOG
	printf ("video_out : no frame, but video_fifo size is %d and not in discontinuity\n",
		this->xine->video_fifo->size(this->xine->video_fifo));
#endif
	continue;
      }

    } else {

      this->still_counter = 0;

      /*
       * time to display frame >img< ?
       */

#ifdef VIDEO_OUT_LOG
      printf ("video_out : diff %d\n", diff);
#endif

      if (diff<0) {
	xine_profiler_stop_count (prof_video_out);
	continue;
      }

      /* 
       * last frame? make backup for possible still image 
       */
      if (img && !img->next) {
	
	if (img_backup) {
	  LOG_MSG(this->xine, _("video_out : overwriting frame backup\n"));
	  vo_append_to_img_buf_queue (this->free_img_buf_queue, img_backup);
	}
        
	img_backup = this->duplicate_frame(this, img);
      }

      /*
       * remove frame from display queue and show it
       */
    
      img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);
      
      if (!img) {
	xine_profiler_stop_count (prof_video_out);
	continue;
      }
    }

    /*
     * from this point on, img must be a valid frame for
     * overlay and output
     */

#ifdef VIDEO_OUT_LOG
    printf ("video_out : displaying image with pts = %d (diff=%d)\n", pts, diff);
#endif

    pthread_mutex_lock (&img->mutex);
    img->driver_locked = 1;

#ifdef VIDEO_OUT_LOG
    if (!img->display_locked)
      printf ("video_out : ALERT! frame was not locked for display queue\n");
#endif

    img->display_locked = 0;
    pthread_mutex_unlock (&img->mutex);

#ifdef VIDEO_OUT_LOG
    printf ("video_out : passing to video driver, image with pts = %d\n", pts);
#endif

    if (this->overlay_source) {
      /* This is the only way for the overlay manager to get pts values
       * for flushing it's buffers. So don't remove it! */
      xine_profiler_start_count (prof_spu_blend);

      this->overlay_source->multiple_overlay_blend (this->overlay_source, img->PTS, 
                                                    this->driver, img,
                                                    this->video_loop_running && this->overlay_enabled);
      xine_profiler_stop_count (prof_spu_blend);
    }
    
    this->driver->display_frame (this->driver, img); 

    xine_profiler_stop_count (prof_video_out);
  }

  /*
   * throw away undisplayed frames
   */
  
  img = this->display_img_buf_queue->first;
  while (img) {

    img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);
    pthread_mutex_lock (&img->mutex);

    if (!img->decoder_locked) 
      vo_append_to_img_buf_queue (this->free_img_buf_queue, img);

    img->display_locked = 0;
    pthread_mutex_unlock (&img->mutex);

    img = this->display_img_buf_queue->first;
  }

  if( img_backup ) {
     vo_append_to_img_buf_queue (this->free_img_buf_queue, img_backup);
  }
 
  pthread_exit(NULL);
}

static uint32_t vo_get_capabilities (vo_instance_t *this) {
  return this->driver->get_capabilities (this->driver);
}

static void vo_open (vo_instance_t *this) {

  pthread_attr_t       pth_attrs;
  int		       err;

  if (!this->video_loop_running) {
    this->video_loop_running = 1;
    this->decoder_started_flag = 0;

    pthread_attr_init(&pth_attrs);
    pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);

    if((err = pthread_create (&this->video_thread,
			      &pth_attrs, video_out_loop, this)) != 0) {

      LOG_MSG(this->xine, _("video_out : can't create thread (%s)\n"), strerror(err));
      /* FIXME: how does this happen ? */
      LOG_MSG(this->xine, _("video_out : sorry, this should not happen. please restart xine.\n"));
      exit(1);
    }
    else
      LOG_MSG(this->xine, _("video_out : thread created\n"));
  } else
    LOG_MSG(this->xine, _("video_out : vo_open : warning! video thread already running\n"));

}

static vo_frame_t *vo_get_frame (vo_instance_t *this,
				 uint32_t width, uint32_t height,
				 int ratio, int format, uint32_t duration,
				 int flags) {

  vo_frame_t *img;

  /*
  printf ("video_out : get_frame %d x %d from queue %d\n",
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
  img->display_locked = 0;
  img->decoder_locked = 1;
  img->driver_locked  = 0;
  img->width        = width;
  img->height       = height;
  img->ratio        = ratio;
  img->format       = format;
  img->duration     = duration;

  /* let driver ensure this image has the right format */

  this->driver->update_frame_format (this->driver, img, width, height, ratio, format, flags);

  pthread_mutex_unlock (&img->mutex);
  
  return img;
}

static vo_frame_t * vo_duplicate_frame( vo_instance_t *this, vo_frame_t *img ) {
  vo_frame_t *dupl;
  int image_size;
    
  pthread_mutex_unlock (&img->mutex);
  
  dupl = vo_get_frame( this, img->width, img->height, img->ratio,
		       img->format, img->duration, VO_BOTH_FIELDS );
 
  pthread_mutex_lock (&dupl->mutex);
  
  dupl->display_locked = 0;
  dupl->decoder_locked = 0;
  dupl->driver_locked  = 0;
  
  image_size = img->width * img->height;

  if (img->format == IMGFMT_YV12) {
    /* The dxr3 video out plugin does not allocate memory for the dxr3
     * decoder, so we must check for NULL */
    if (img->base[0])
      xine_fast_memcpy(dupl->base[0], img->base[0], image_size);
    if (img->base[1])
      xine_fast_memcpy(dupl->base[1], img->base[1], image_size >> 2);
    if (img->base[2])
      xine_fast_memcpy(dupl->base[2], img->base[2], image_size >> 2);
  } else {
    if (img->base[0])
      xine_fast_memcpy(dupl->base[0], img->base[0], image_size * 2);
  }  
  
  dupl->bad_frame = 0;
  dupl->PTS = dupl->SCR = 0;

  /* Support copy; Dangerous, since some decoders may use a source that's
   * not dupl->base. It's up to the copy implementation to check for NULL */ 
  if (img->copy) {
    int height = img->height;
    int stride = img->width;
    uint8_t* src[3];
  
    src[0] = dupl->base[0];
    src[1] = dupl->base[1];
    src[2] = dupl->base[2];
    while ((height -= 16) >= 0) {
      dupl->copy(dupl, src);
      src[0] += 16 * stride;
      src[1] +=  4 * stride;
      src[2] +=  4 * stride;
    }
  }
  
  pthread_mutex_unlock (&dupl->mutex);
  
  pthread_mutex_unlock (&img->mutex);

  return dupl;
}

static void vo_close (vo_instance_t *this) {
    
  /* this will make sure all hide events were processed */
  if (this->overlay_source)
    this->overlay_source->flush_events (this->overlay_source);
  
  if (this->video_loop_running) {
    void *p;

    this->video_loop_running = 0;
    this->video_paused = 0;
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

  img->driver_locked = 0;

  if (!img->decoder_locked) {    
    vo_append_to_img_buf_queue (img->instance->free_img_buf_queue, img);
  }

  pthread_mutex_unlock (&img->mutex);
}

static void vo_frame_free (vo_frame_t *img) {

  pthread_mutex_lock (&img->mutex);
  img->decoder_locked = 0;

  if (!img->display_locked && !img->driver_locked ) {
    vo_append_to_img_buf_queue (img->instance->free_img_buf_queue, img);
  }

  pthread_mutex_unlock (&img->mutex);
}

static vo_frame_t *vo_get_last_frame (vo_instance_t *this) {
  return this->last_frame;
}

static int vo_frame_draw (vo_frame_t *img) {

  vo_instance_t *this = img->instance;
  int32_t        diff;
  uint32_t       cur_vpts;
  uint32_t       pic_vpts ;
  int            frames_to_skip;

  pic_vpts = this->metronom->got_video_frame (this->metronom, img->PTS, img->SCR);

#ifdef VIDEO_OUT_LOG
  printf ("video_out : got image %d. vpts for picture is %d (pts was %d)\n",
	  img, pic_vpts, img->PTS);
#endif

  img->PTS = pic_vpts;
  this->num_frames_delivered++;

  cur_vpts = this->metronom->get_current_time(this->metronom);
  
  diff = pic_vpts - cur_vpts;
  frames_to_skip = ((-1 * diff) / this->pts_per_frame + 3) * 2;

#ifdef VIDEO_OUT_LOG
  printf ("video_out : delivery diff : %d\n",diff);
#endif

  if (img->display_locked) {
    LOG_MSG(this->xine, _("video_out : ALERT! frame is already locked for displaying\n"));
    return frames_to_skip;
  }

  if (cur_vpts>0) {

    if (diff<(-1 * this->pts_per_half_frame) && img->drawn != 2 ) {

      this->num_frames_discarded++;
#ifdef VIDEO_OUT_LOG
      printf ("video_out : frame rejected, %d frames to skip\n", frames_to_skip);
#endif

      LOG_MSG(this->xine, _("vo_frame_draw: rejected, %d frames to skip\n"), frames_to_skip);

      pthread_mutex_lock (&img->mutex);
      img->display_locked = 0;
      pthread_mutex_unlock (&img->mutex);

      vo_frame_displayed (img);

      this->last_frame = img;

      return frames_to_skip;

    }
  } /* else: we are probably in precaching mode */

  if (!img->bad_frame) {
    /*
     * put frame into FIFO-Buffer
     */

#ifdef VIDEO_OUT_LOG
    printf ("video_out : frame is ok => appending to display buffer\n");
#endif

    this->last_frame = img;

    pthread_mutex_lock (&img->mutex);
    img->display_locked = 1;
    pthread_mutex_unlock (&img->mutex);

    vo_append_to_img_buf_queue (this->display_img_buf_queue, img);

  } else {
    this->num_frames_skipped++;

    pthread_mutex_lock (&img->mutex);
    img->display_locked = 0;
    pthread_mutex_unlock (&img->mutex);

    vo_frame_displayed (img);
  }

  /*
   * performance measurement
   */

  if (this->num_frames_delivered>199) {
    LOG_MSG_STDERR(this->xine,
		   _("%d frames delivered, %d frames skipped, %d frames discarded\n"), 
		   this->num_frames_delivered, this->num_frames_skipped, this->num_frames_discarded);

    this->num_frames_delivered = 0;
    this->num_frames_discarded = 0;
    this->num_frames_skipped   = 0;
  }
  
  return frames_to_skip;
}

static void vo_enable_overlay (vo_instance_t *this, int overlay_enabled) {
  this->overlay_enabled = overlay_enabled;
}

static void vo_decoder_started (vo_instance_t *this) {
  this->decoder_started_flag = 1;
}

vo_instance_t *vo_new_instance (vo_driver_t *driver, xine_t *xine) {

  vo_instance_t *this;
  int            i;

  this = xine_xmalloc (sizeof (vo_instance_t)) ;
  this->driver                = driver;
  this->xine                  = xine;
  this->metronom              = xine->metronom;

  this->open                  = vo_open;
  this->get_frame             = vo_get_frame;
  this->duplicate_frame       = vo_duplicate_frame;
  this->get_last_frame        = vo_get_last_frame;
  this->close                 = vo_close;
  this->exit                  = vo_exit;
  this->get_capabilities      = vo_get_capabilities;
  this->enable_ovl            = vo_enable_overlay;
  this->decoder_started       = vo_decoder_started;

  this->num_frames_delivered  = 0;
  this->num_frames_skipped    = 0;
  this->num_frames_discarded  = 0;
  this->free_img_buf_queue    = vo_new_img_buf_queue ();
  this->display_img_buf_queue = vo_new_img_buf_queue ();
  this->video_loop_running    = 0;
  this->video_paused          = 0;          
  this->pts_per_frame         = 0;
  this->pts_per_half_frame    = 0;
  
  this->overlay_source        = video_overlay_new_instance();
  this->overlay_source->init (this->overlay_source);
  this->overlay_enabled       = 1;

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

