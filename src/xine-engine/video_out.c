/*
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: video_out.c,v 1.76 2002/02/18 13:33:19 guenter Exp $
 *
 * frame allocation / queuing / scheduling / output functions
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
#include <zlib.h>
#include <assert.h>

#include "video_out.h"
#include "metronom.h"
#include "xine_internal.h"
#include "xineutils.h"

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                 \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                 \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                     \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                     \
    printf(__VA_ARGS__);                                             \
  }
#endif

/*
#define LOG
*/

#define NUM_FRAME_BUFFERS     15

typedef struct {
  
  vo_instance_t             vo; /* public part */

  vo_driver_t              *driver;
  metronom_t               *metronom;
  xine_t                   *xine;
  
  img_buf_fifo_t           *free_img_buf_queue;
  img_buf_fifo_t           *display_img_buf_queue;

  vo_frame_t               *last_frame;
  vo_frame_t               *img_backup;
  int                       backup_is_logo;

  int                       video_loop_running;
  int                       video_opened;
  pthread_t                 video_thread;

  int                       num_frames_delivered;
  int                       num_frames_skipped;
  int                       num_frames_discarded;

  /* pts value when decoder delivered last video frame */
  int64_t                   last_delivery_pts; 

  int                       logo_w, logo_h;
  uint8_t                  *logo_yuy2;

  video_overlay_instance_t *overlay_source;
  int                       overlay_enabled;
} vos_t;

/*
 * frame queue (fifo) util functions
 */

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

  /* img already enqueue? (serious leak) */
  assert (img->next==NULL);

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

/*
 * function called by video output driver
 */

static void vo_frame_displayed (vo_frame_t *img) {

  pthread_mutex_lock (&img->mutex);

  img->driver_locked = 0;

  if (!img->decoder_locked) {    
    vos_t *this = (vos_t *) img->instance;
    vo_append_to_img_buf_queue (this->free_img_buf_queue, img);
  }

  pthread_mutex_unlock (&img->mutex);
}

/*
 * 
 * functions called by video decoder:
 *
 * get_frame => alloc frame for rendering
 *
 * frame_draw=> queue finished frame for display
 *
 * frame_free=> frame no longer used as reference frame by decoder
 *
 */

static vo_frame_t *vo_get_frame (vo_instance_t *this_gen,
				 uint32_t width, uint32_t height,
				 int ratio, int format,
				 int flags) {

  vo_frame_t *img;
  vos_t      *this = (vos_t *) this_gen;

#ifdef LOG
  printf ("video_out: get_frame (%d x %d)\n", width, height);
#endif

  img = vo_remove_from_img_buf_queue (this->free_img_buf_queue);

#ifdef LOG
  printf ("video_out: got a frame -> pthread_mutex_lock (&img->mutex)\n");
#endif

  pthread_mutex_lock (&img->mutex);
  img->display_locked = 0;
  img->decoder_locked = 1;
  img->driver_locked  = 0;
  img->width          = width;
  img->height         = height;
  img->ratio          = ratio;
  img->format         = format;
  
  /* let driver ensure this image has the right format */

  this->driver->update_frame_format (this->driver, img, width, height, 
				     ratio, format, flags);

  pthread_mutex_unlock (&img->mutex);
  
#ifdef LOG
  printf ("video_out: get_frame (%d x %d) done\n", width, height);
#endif

  return img;
}

static int vo_frame_draw (vo_frame_t *img) {

  vos_t         *this = (vos_t *) img->instance;
  int64_t        diff;
  int64_t        cur_vpts;
  int64_t        pic_vpts ;
  int            frames_to_skip;

  this->metronom->got_video_frame (this->metronom, img);

  pic_vpts = img->vpts;

  cur_vpts = this->metronom->get_current_time(this->metronom);
  this->last_delivery_pts = cur_vpts;

#ifdef LOG
  printf ("video_out: got image at master vpts %lld. vpts for picture is %lld (pts was %lld)\n",
	  cur_vpts, pic_vpts, img->pts);
#endif

  this->num_frames_delivered++;

  diff = pic_vpts - cur_vpts;
  frames_to_skip = ((-1 * diff) / img->duration + 3) * 2;

#ifdef LOG
  printf ("video_out: delivery diff : %lld, current vpts is %lld\n",
	  diff, cur_vpts);
#endif

  if (img->display_locked) {
    printf ("video_out: ALERT! frame is already locked for displaying\n");
    return frames_to_skip;
  }

  if (!img->bad_frame) {
    /*
     * put frame into FIFO-Buffer
     */

#ifdef LOG
    printf ("video_out: frame is ok => appending to display buffer\n");
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
		   this->num_frames_delivered, 
		   this->num_frames_skipped, this->num_frames_discarded);

    this->num_frames_delivered = 0;
    this->num_frames_discarded = 0;
    this->num_frames_skipped   = 0;
  }
  
  return frames_to_skip;
}

static void vo_frame_free (vo_frame_t *img) {

  pthread_mutex_lock (&img->mutex);
  img->decoder_locked = 0;

  if (!img->display_locked && !img->driver_locked ) {
    vos_t *this = (vos_t *) img->instance;
    vo_append_to_img_buf_queue (this->free_img_buf_queue, img);
  }

  pthread_mutex_unlock (&img->mutex);
}



/*
 *
 * video out loop related functions
 *
 */

static void expire_frames (vos_t *this, int64_t cur_vpts) {

  int64_t       pts;
  int64_t       diff;
  vo_frame_t   *img;

  img = this->display_img_buf_queue->first;

  /*
   * throw away expired frames
   */

  diff = 1000000; /* always enter the while-loop */

  while (img && (diff > img->duration)) {
    pts = img->vpts;
    diff = cur_vpts - pts;
      
    if (diff > img->duration) {
      LOG_MSG(this->xine,
	      _("video_out: throwing away image with pts %lld because "
		"it's too old (diff : %lld).\n"), pts, diff);

      this->num_frames_discarded++;

      img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);

      /*
       * last frame? back it up for 
       * still frame creation
       */

      if (!this->display_img_buf_queue->first) {
	  
	if (this->img_backup) {
	  pthread_mutex_lock (&this->img_backup->mutex);
#ifdef LOG
	  printf("video_out: overwriting frame backup\n");
#endif
	  this->img_backup->display_locked = 0;
	  if (!img->decoder_locked) 
	    vo_append_to_img_buf_queue (this->free_img_buf_queue,
					this->img_backup);

	  pthread_mutex_unlock (&this->img_backup->mutex);
	}
	printf("video_out: possible still frame (old)\n");

	/* we must not clear display_locked from img_backup.
	   without it decoder may try to free our backup.  */
	this->img_backup = img;
	this->backup_is_logo = 0;
      } else {
	pthread_mutex_lock (&img->mutex);
	  
	img->display_locked = 0;
	if (!img->decoder_locked) 
	  vo_append_to_img_buf_queue (this->free_img_buf_queue, img);
	  
	pthread_mutex_unlock (&img->mutex);
      }
	
      img = this->display_img_buf_queue->first;
    }
  }

}

static vo_frame_t *get_next_frame (vos_t *this, int64_t cur_vpts) {
  
  vo_frame_t   *img;

  img = this->display_img_buf_queue->first;

  /* 
   * still frame detection:
   */

  /* no frame? => still frame detection */

  if (!img) {

#ifdef LOG
    printf ("video_out: no frame\n");
#endif

    /*
     * display logo ?
     */
    if (!this->video_opened && (!this->img_backup || !this->backup_is_logo)) {

      if (this->img_backup) {
	pthread_mutex_lock (&this->img_backup->mutex);
#ifdef LOG
	printf("video_out: overwriting frame backup\n");
#endif
	this->img_backup->display_locked = 0;
	if (!this->img_backup->decoder_locked) 
	  vo_append_to_img_buf_queue (this->free_img_buf_queue,
				      this->img_backup);

	pthread_mutex_unlock (&this->img_backup->mutex);
      }

      printf("video_out: copying logo image\n");
	
      this->img_backup = vo_get_frame (&this->vo, this->logo_w, this->logo_h,
				       42, IMGFMT_YUY2, VO_BOTH_FIELDS);

      this->img_backup->decoder_locked = 0;
      this->img_backup->display_locked = 1;
      this->img_backup->driver_locked  = 0;
      this->img_backup->duration       = 10000;

      xine_fast_memcpy(this->img_backup->base[0], this->logo_yuy2,
		       this->logo_w*this->logo_h*2);

      this->backup_is_logo = 1;
    }

    if (this->img_backup) {

#ifdef LOG
      printf("video_out: generating still frame (cur_vpts = %lld) \n",
	     cur_vpts);
#endif
	
      /* keep playing still frames */
      img = this->vo.duplicate_frame (&this->vo, this->img_backup );
      img->display_locked = 1;
  
      do {
	this->metronom->got_video_frame(this->metronom, img);
      } while (img->vpts < (cur_vpts - img->duration/2) );

      return img;

    } else {

#ifdef LOG
      printf ("video_out: no frame, but no backup frame\n");
#endif

      return NULL;
    }
  } else {

    int64_t diff;

    diff = cur_vpts - img->vpts;

    /*
     * time to display frame "img" ?
     */

#ifdef LOG
    printf ("video_out: diff %lld\n", diff);
#endif

    if (diff < 0) {
      return NULL;
    }

    if (this->img_backup) {
      pthread_mutex_lock (&this->img_backup->mutex);
      printf("video_out: freeing frame backup\n");
	
      this->img_backup->display_locked = 0;
      if( !this->img_backup->decoder_locked )
	vo_append_to_img_buf_queue (this->free_img_buf_queue,
				    this->img_backup);
      pthread_mutex_unlock (&this->img_backup->mutex);
      this->img_backup = NULL;
    }
      
    /* 
     * last frame? make backup for possible still image 
     */
    if (img && !img->next &&
	(this->xine->video_fifo->size(this->xine->video_fifo) < 10 
	 || this->xine->video_in_discontinuity) ) {
	
      printf ("video_out: possible still frame (fifosize = %d)\n",
	      this->xine->video_fifo->size(this->xine->video_fifo));
        
      this->img_backup = this->vo.duplicate_frame (&this->vo, img);
      this->backup_is_logo = 0;
    }

    /*
     * remove frame from display queue and show it
     */
    
    img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);

    return img;
  }
}

static void overlay_and_display_frame (vos_t *this, 
				       vo_frame_t *img) {

#ifdef LOG
  printf ("video_out: displaying image with vpts = %lld\n", 
	  img->vpts);
#endif
  
  pthread_mutex_lock (&img->mutex);
  img->driver_locked = 1;
  
#ifdef LOG
  if (!img->display_locked)
    printf ("video_out: ALERT! frame was not locked for display queue\n");
#endif
  
  img->display_locked = 0;
  pthread_mutex_unlock (&img->mutex);
  
#ifdef LOG
  printf ("video_out: passing to video driver image with pts = %lld\n", 
	  img->vpts);
#endif

  if (this->overlay_source) {
    /* This is the only way for the overlay manager to get pts values
     * for flushing its buffers. So don't remove it! */
    
    this->overlay_source->multiple_overlay_blend (this->overlay_source, 
						  img->vpts, 
						  this->driver, img,
						  this->video_loop_running && this->overlay_enabled);
  }
  
  this->driver->display_frame (this->driver, img); 
}

static void *video_out_loop (void *this_gen) {

  int64_t            vpts, diff;
  vo_frame_t        *img;
  vos_t             *this = (vos_t *) this_gen;
  int64_t            frame_duration, next_frame_vpts;
  int64_t            usec_to_sleep;

  /*
   * here it is - the heart of xine (or rather: one of the hearts
   * of xine) : the video output loop
   */

  frame_duration = 1500; /* default */
  next_frame_vpts = this->metronom->get_current_time (this->metronom);

#ifdef LOG
    printf ("video_out: loop starting...\n");
#endif

  while ( this->video_loop_running ) {

    /*
     * get current time and find frame to display
     */

    vpts = this->metronom->get_current_time (this->metronom);
#ifdef LOG
    printf ("video_out: loop iteration at %lld\n", vpts);
#endif
    expire_frames (this, vpts);
    img = get_next_frame (this, vpts);

    /*
     * if we have found a frame, display it
     */

    if (img) {
#ifdef LOG
      printf ("video_out: displaying frame (id=%d)\n", img->id);
#endif
      overlay_and_display_frame (this, img);
    }

    /*
     * if we haven't heared from the decoder for some time
     * flush it
     * test display fifo empty to protect from deadlocks
     */

    diff = vpts - this->last_delivery_pts;
    if (diff > 30000 && !this->display_img_buf_queue->first) {
      if (this->xine->cur_video_decoder_plugin) {
	this->xine->cur_video_decoder_plugin->flush(this->xine->cur_video_decoder_plugin);

#ifdef LOG
	printf ("video_out: flushing current video decoder plugin\n");
#endif
      }
      this->last_delivery_pts = vpts;
    }

    /*
     * wait until it's time to display next frame
     */

    if (img) {
      frame_duration = img->duration;
      next_frame_vpts = img->vpts + img->duration;
    } else {
      next_frame_vpts += frame_duration;
    }
    
#ifdef LOG
    printf ("video_out: next_frame_vpts is %lld\n", next_frame_vpts);
#endif
 
    do {
      vpts = this->metronom->get_current_time (this->metronom);

      usec_to_sleep = (next_frame_vpts - vpts) * 100 / 9;

#ifdef LOG
      printf ("video_out: %lld usec to sleep at master vpts %lld\n", 
	      usec_to_sleep, vpts);
#endif
      
      /*
      if( usec_to_sleep > 1000000 )
      {
        printf ("video_out: master clock changed\n"); 
        next_frame_vpts = vpts;
        usec_to_sleep = 0;
      }
      */

      if (usec_to_sleep>0) 
	xine_usec_sleep (usec_to_sleep);

    } while (usec_to_sleep > 0);
  }


  /*
   * throw away undisplayed frames
   */
  
  img = this->display_img_buf_queue->first;
  while (img) {

    img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);
    pthread_mutex_lock (&img->mutex);

    img->display_locked = 0;
    if (!img->decoder_locked) 
      vo_append_to_img_buf_queue (this->free_img_buf_queue, img);

    pthread_mutex_unlock (&img->mutex);

    img = this->display_img_buf_queue->first;
  }

  if (this->img_backup) {
    pthread_mutex_lock (&this->img_backup->mutex);
    
    this->img_backup->display_locked = 0;
    if (!this->img_backup->decoder_locked)
      vo_append_to_img_buf_queue (this->free_img_buf_queue, this->img_backup);
    
    pthread_mutex_unlock (&this->img_backup->mutex);
  }
 
  pthread_exit(NULL);
}

static uint32_t vo_get_capabilities (vo_instance_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  return this->driver->get_capabilities (this->driver);
}

static vo_frame_t * vo_duplicate_frame( vo_instance_t *this_gen, vo_frame_t *img ) {

  vo_frame_t *dupl;
  /* vos_t      *this = (vos_t *) this_gen; */
  int         image_size;
    
  dupl = vo_get_frame (this_gen, img->width, img->height, img->ratio,
		       img->format, VO_BOTH_FIELDS );
 
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
  dupl->pts       = 0;
  dupl->vpts      = 0;
  dupl->scr       = 0;
  dupl->duration  = img->duration;

  /* Support copy; Dangerous, since some decoders may use a source that's
   * not dupl->base. It's up to the copy implementation to check for NULL */ 
  if (img->format == IMGFMT_YV12) {
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
  } else {
    if (img->copy) {
      int height = img->height;
      int stride = img->width;
      uint8_t* src[3];
      
      src[0] = dupl->base[0];
      
      while ((height -= 16) >= 0) {
        dupl->copy(dupl, src);
        src[0] += 32 * stride;
      }
    }
  }
  
  pthread_mutex_unlock (&dupl->mutex);
  
  return dupl;
}

static void vo_open (vo_instance_t *this_gen) {

  vos_t      *this = (vos_t *) this_gen;

  this->video_opened = 1;
  this->last_delivery_pts = 0;
}

static void vo_close (vo_instance_t *this_gen) {

  vos_t      *this = (vos_t *) this_gen;    

  /* this will make sure all hide events were processed */
  if (this->overlay_source)
    this->overlay_source->flush_events (this->overlay_source);

  this->video_opened = 0;
}

static void vo_free_img_buffers (vo_instance_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
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

static void vo_exit (vo_instance_t *this_gen) {

  vos_t      *this = (vos_t *) this_gen;

#ifdef LOG
  printf ("video_out: vo_exit...\n");
#endif

  if (this->video_loop_running) {
    void *p;

    this->video_loop_running = 0;

    pthread_join (this->video_thread, &p);
  }

  vo_free_img_buffers (this_gen);

  this->driver->exit (this->driver);

#ifdef LOG
  printf ("video_out: vo_exit... done\n");
#endif
}

static vo_frame_t *vo_get_last_frame (vo_instance_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  return this->last_frame;
}

/*
 * overlay stuff 
 */

static video_overlay_instance_t *vo_get_overlay_instance (vo_instance_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  return this->overlay_source;
}

static void vo_enable_overlay (vo_instance_t *this_gen, int overlay_enabled) {
  vos_t      *this = (vos_t *) this_gen;
  this->overlay_enabled = overlay_enabled;
}

static uint16_t gzread_i16(gzFile *fp) {
  uint16_t ret;
  ret = gzgetc(fp) << 8 ;
  ret |= gzgetc(fp);
  return ret;
}

#define LOGO_PATH_MAX 1025

vo_instance_t *vo_new_instance (vo_driver_t *driver, xine_t *xine) {

  vos_t         *this;
  int            i;
  char           pathname[LOGO_PATH_MAX]; 
  pthread_attr_t pth_attrs;
  int		 err;
  gzFile        *fp;

  this = xine_xmalloc (sizeof (vos_t)) ;

  this->driver                = driver;
  this->xine                  = xine;
  this->metronom              = xine->metronom;

  this->vo.open                  = vo_open;
  this->vo.get_frame             = vo_get_frame;
  this->vo.duplicate_frame       = vo_duplicate_frame;
  this->vo.get_last_frame        = vo_get_last_frame;
  this->vo.close                 = vo_close;
  this->vo.exit                  = vo_exit;
  this->vo.get_capabilities      = vo_get_capabilities;
  this->vo.enable_ovl            = vo_enable_overlay;
  this->vo.get_overlay_instance  = vo_get_overlay_instance;

  this->num_frames_delivered  = 0;
  this->num_frames_skipped    = 0;
  this->num_frames_discarded  = 0;
  this->free_img_buf_queue    = vo_new_img_buf_queue ();
  this->display_img_buf_queue = vo_new_img_buf_queue ();
  this->video_loop_running    = 0;

  this->img_backup            = NULL;
  this->backup_is_logo        = 0;
  
  this->overlay_source        = video_overlay_new_instance();
  this->overlay_source->init (this->overlay_source);
  this->overlay_enabled       = 1;

  for (i=0; i<NUM_FRAME_BUFFERS; i++) {
    vo_frame_t *img;

    img = driver->alloc_frame (driver) ;

    img->id        = i;
    
    img->instance  = &this->vo;
    img->free      = vo_frame_free ;
    img->displayed = vo_frame_displayed;
    img->draw      = vo_frame_draw;

    vo_append_to_img_buf_queue (this->free_img_buf_queue,
				img);
  }

  /* 
   * load xine logo
   */
  
  snprintf (pathname, LOGO_PATH_MAX, "%s/xine_logo.zyuy2", XINE_SKINDIR);
    
  if ((fp = gzopen (pathname, "rb")) != NULL) {
    
    this->logo_w = gzread_i16 (fp);
    this->logo_h = gzread_i16 (fp);
    
    printf ("video_out: loading logo %d x %d pixels, yuy2\n",
	    this->logo_w, this->logo_h);
    
    this->logo_yuy2 = malloc (this->logo_w * this->logo_h *2);
    
    gzread (fp, this->logo_yuy2, this->logo_w * this->logo_h *2);
    
    gzclose (fp);
  }

  /*
   * start video output thread
   *
   * this thread will alwys be running, displaying the
   * logo when "idle" thus making it possible to have
   * osd when not playing a stream
   */

  this->video_loop_running   = 1;
  this->video_opened         = 0;

  pthread_attr_init(&pth_attrs);
  pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);

  if ((err = pthread_create (&this->video_thread,
			     &pth_attrs, video_out_loop, this)) != 0) {

    printf (_("video_out: can't create thread (%s)\n"), 
	    strerror(err));
    /* FIXME: how does this happen ? */
    printf (_("video_out: sorry, this should not happen. please restart xine.\n"));
    exit(1);
  } else
    LOG_MSG(this->xine, _("video_out: thread created\n"));

  return &this->vo;
}
