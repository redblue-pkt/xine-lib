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
 * $Id: video_out.c,v 1.127 2002/12/27 19:14:41 mroi Exp $
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
#include <pthread.h>
#include <assert.h>

#include "xine_internal.h"
#include "video_out.h"
#include "metronom.h"
#include "xineutils.h"

/*
#define LOG
*/

#define NUM_FRAME_BUFFERS     15

typedef struct {
  
  xine_video_port_t         vo; /* public part */

  vo_driver_t              *driver;
  xine_t                   *xine;
  metronom_clock_t         *clock;
  xine_list_t              *streams;
  pthread_mutex_t           streams_lock;
  
  img_buf_fifo_t           *free_img_buf_queue;
  img_buf_fifo_t           *display_img_buf_queue;

  vo_frame_t               *last_frame;
  vo_frame_t               *img_backup;
  int                       redraw_needed;
  int                       flush_frames;
  
  int                       video_loop_running;
  int                       video_opened;
  pthread_t                 video_thread;

  int                       num_frames_delivered;
  int                       num_frames_skipped;
  int                       num_frames_discarded;

  /* pts value when decoder delivered last video frame */
  int64_t                   last_delivery_pts; 


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

  int                locked_for_read;
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
    queue->locked_for_read = 0;
    pthread_mutex_init (&queue->mutex, NULL);
    pthread_cond_init  (&queue->not_empty, NULL);
  }
  return queue;
}

static void vo_append_to_img_buf_queue_int (img_buf_fifo_t *queue,
					vo_frame_t *img) {

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
}

static void vo_append_to_img_buf_queue (img_buf_fifo_t *queue,
					vo_frame_t *img) {
  pthread_mutex_lock (&queue->mutex);
  vo_append_to_img_buf_queue_int (queue, img);
  pthread_mutex_unlock (&queue->mutex);
}

static vo_frame_t *vo_remove_from_img_buf_queue_int (img_buf_fifo_t *queue) {
  vo_frame_t *img;

  while (!queue->first || queue->locked_for_read) {
    pthread_cond_wait (&queue->not_empty, &queue->mutex);
  }

  img = queue->first;

  if (img) {
    queue->first = img->next;
    img->next = NULL;
    if (!queue->first) {
      queue->last = NULL;
      queue->num_buffers = 0;
    }
    else {
      queue->num_buffers--;
    }
  }
    
  return img;
}

static vo_frame_t *vo_remove_from_img_buf_queue (img_buf_fifo_t *queue) {
  vo_frame_t *img;

  pthread_mutex_lock (&queue->mutex);
  img = vo_remove_from_img_buf_queue_int(queue);
  pthread_mutex_unlock (&queue->mutex);

  return img;
}

/*
 * functions to maintain lock_counter
 */
static void vo_frame_inc_lock (vo_frame_t *img) {
  
  pthread_mutex_lock (&img->mutex);

  img->lock_counter++;

  pthread_mutex_unlock (&img->mutex);
}

static void vo_frame_dec_lock (vo_frame_t *img) {
  
  pthread_mutex_lock (&img->mutex);

  img->lock_counter--;
  if (!img->lock_counter) {    
    vos_t *this = (vos_t *) img->port;
    vo_append_to_img_buf_queue (this->free_img_buf_queue, img);
  }

  pthread_mutex_unlock (&img->mutex);
}

/* call vo_driver->copy method for the entire frame */
static void vo_frame_driver_copy(vo_frame_t *img)
{ 
  if (img->format == XINE_IMGFMT_YV12) {
    if (img->copy) {
      int height = img->height;
      uint8_t* src[3];
  
      src[0] = img->base[0];
      src[1] = img->base[1];
      src[2] = img->base[2];
      while ((height -= 16) >= 0) {
        img->copy(img, src);
        src[0] += 16 * img->pitches[0];
        src[1] +=  8 * img->pitches[1];
        src[2] +=  8 * img->pitches[2];
      }
    }
  } else {
    if (img->copy) {
      int height = img->height;
      uint8_t* src[3];
      
      src[0] = img->base[0];
      
      while ((height -= 16) >= 0) {
        img->copy(img, src);
        src[0] += 16 * img->pitches[0];
      }
    }
  }
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

static vo_frame_t *vo_get_frame (xine_video_port_t *this_gen,
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
  img->lock_counter   = 1;
  img->width          = width;
  img->height         = height;
  img->ratio          = ratio;
  img->format         = format;
  img->copy_called    = 0;
  extra_info_reset ( img->extra_info );
  
  /* let driver ensure this image has the right format */

  this->driver->update_frame_format (this->driver, img, width, height, 
				     ratio, format, flags);

  pthread_mutex_unlock (&img->mutex);
  
#ifdef LOG
  printf ("video_out: get_frame (%d x %d) done\n", width, height);
#endif

  return img;
}

static int vo_frame_draw (vo_frame_t *img, xine_stream_t *stream) {

  vos_t         *this = (vos_t *) img->port;
  int64_t        diff;
  int64_t        cur_vpts;
  int64_t        pic_vpts ;
  int            frames_to_skip;

  img->stream = stream;
  extra_info_merge( img->extra_info, stream->video_decoder_extra_info );
  
  stream->metronom->got_video_frame (stream->metronom, img);

  pic_vpts = img->vpts;

  cur_vpts = this->clock->get_current_time(this->clock);
  this->last_delivery_pts = cur_vpts;

#ifdef LOG
  printf ("video_out: got image at master vpts %lld. vpts for picture is %lld (pts was %lld)\n",
	  cur_vpts, pic_vpts, img->pts);
#endif

  this->num_frames_delivered++;

  diff = pic_vpts - cur_vpts;
  /* avoid division by zero */
  if( img->duration <= 0 )
    img->duration = 3000;
  frames_to_skip = ((-1 * diff) / img->duration + 3) * 2;

  if (frames_to_skip<0)
    frames_to_skip = 0;

#ifdef LOG
  printf ("video_out: delivery diff : %lld, current vpts is %lld, %d frames to skip\n",
	  diff, cur_vpts, frames_to_skip);
#endif

  if (img->lock_counter > 1) {
    printf ("video_out: ALERT! frame is already locked for displaying\n");
    return frames_to_skip;
  }

  if (!img->bad_frame) {

    /* do not call copy() for frames that will be dropped */
    if( !frames_to_skip && img->copy && !img->copy_called )
      vo_frame_driver_copy(img);
    
    /*
     * put frame into FIFO-Buffer
     */

#ifdef LOG
    printf ("video_out: frame is ok => appending to display buffer\n");
#endif

    vo_frame_inc_lock( img );
    vo_append_to_img_buf_queue (this->display_img_buf_queue, img);

  } else {
#ifdef LOG
    printf ("video_out: bad_frame\n");
#endif
    extra_info_merge( stream->current_extra_info, img->extra_info );

    this->num_frames_skipped++;
  }

  /*
   * performance measurement
   */

  if ((this->num_frames_delivered % 200) == 0
  	&& (this->num_frames_skipped || this->num_frames_discarded)) {
    xine_log(this->xine, XINE_LOG_MSG,
	     _("%d frames delivered, %d frames skipped, %d frames discarded\n"), 
	     this->num_frames_delivered, 
	     this->num_frames_skipped, this->num_frames_discarded);

    this->num_frames_delivered = 0;
    this->num_frames_discarded = 0;
    this->num_frames_skipped   = 0;
  }
  
  return frames_to_skip;
}

/*
 *
 * video out loop related functions
 *
 */

/* duplicate_frame(): this function is used to keep playing frames 
 * while video is still or player paused. 
 * 
 * frame allocation inside vo loop is dangerous:
 * we must never wait for a free frame -> deadlock condition.
 * to avoid deadlocks we don't use vo_remove_from_img_buf_queue()
 * and reimplement a slightly modified version here.
 * free_img_buf_queue->mutex must be grabbed prior entering it.
 * (must assure that free frames won't be exhausted by decoder thread).
 */
static vo_frame_t * duplicate_frame( vos_t *this, vo_frame_t *img ) {

  vo_frame_t *dupl;
  int         image_size;

  if( !this->free_img_buf_queue->first)
    return NULL;

  dupl = this->free_img_buf_queue->first;
  this->free_img_buf_queue->first = dupl->next;
  dupl->next = NULL;
  if (!this->free_img_buf_queue->first) {
    this->free_img_buf_queue->last = NULL;
    this->free_img_buf_queue->num_buffers = 0;
  }
  else {
    this->free_img_buf_queue->num_buffers--;
  }
      
  pthread_mutex_lock (&dupl->mutex);
  dupl->lock_counter   = 1;
  dupl->width          = img->width;
  dupl->height         = img->height;
  dupl->ratio          = img->ratio;
  dupl->format         = img->format;
  
  this->driver->update_frame_format (this->driver, dupl, dupl->width, dupl->height, 
				     dupl->ratio, dupl->format, VO_BOTH_FIELDS);

  pthread_mutex_unlock (&dupl->mutex);
  
  image_size = img->pitches[0] * img->height;

  if (img->format == XINE_IMGFMT_YV12) {
    if (img->base[0])
      xine_fast_memcpy(dupl->base[0], img->base[0], image_size);
    if (img->base[1])
      xine_fast_memcpy(dupl->base[1], img->base[1], img->pitches[1] * ((img->height+1)/2));
    if (img->base[2])
      xine_fast_memcpy(dupl->base[2], img->base[2], img->pitches[2] * ((img->height+1)/2));
  } else {
    if (img->base[0])
      xine_fast_memcpy(dupl->base[0], img->base[0], image_size);
  }  
  
  dupl->bad_frame = 0;
  dupl->pts       = 0;
  dupl->vpts      = 0;
  dupl->copy_called    = 0;

  dupl->duration  = img->duration;

  dupl->stream    = img->stream;
  memcpy( dupl->extra_info, img->extra_info, sizeof(extra_info_t) );
  
  /* delay frame copying for now, we might not even need it (eg. frame will be discarded) */
  /* vo_frame_driver_copy(dupl); */
  
  return dupl;
}


static void expire_frames (vos_t *this, int64_t cur_vpts) {

  int64_t       pts;
  int64_t       diff;
  vo_frame_t   *img;

  pthread_mutex_lock(&this->display_img_buf_queue->mutex);
  
  img = this->display_img_buf_queue->first;

  /*
   * throw away expired frames
   */

  diff = 1000000; /* always enter the while-loop */

  while (img && (diff > img->duration || this->flush_frames)) {
    pts = img->vpts;
    diff = cur_vpts - pts;
      
    if (diff > img->duration || this->flush_frames) {
  
      if( !this->flush_frames ) {
        xine_log(this->xine, XINE_LOG_MSG,
	         _("video_out: throwing away image with pts %lld because "
		   "it's too old (diff : %lld).\n"), pts, diff);

        this->num_frames_discarded++;
      }
      
      img = vo_remove_from_img_buf_queue_int (this->display_img_buf_queue);
  
      extra_info_merge( img->stream->current_extra_info, img->extra_info );

      /* when flushing frames, keep the first one as backup */
      if( this->flush_frames ) {
        
        if (!this->img_backup) {
	  this->img_backup = img;
	
	  this->redraw_needed = 1;
        } else {
	  vo_frame_dec_lock( img );
        }     
           
      } else {
        /*
         * last frame? back it up for 
         * still frame creation
         */
        
        if (!this->display_img_buf_queue->first) {
	  
	  if (this->img_backup) {
#ifdef LOG
	    printf("video_out: overwriting frame backup\n");
#endif
	    vo_frame_dec_lock( this->img_backup );
	  }
#ifdef LOG
	  printf("video_out: possible still frame (old)\n");
#endif

	  this->img_backup = img;
	
	  /* wait 4 frames before drawing this one. 
	     this allow slower systems to recover. */
	  this->redraw_needed = 4; 
        } else {
	  vo_frame_dec_lock( img );
        }
      }
      img = this->display_img_buf_queue->first;
    }
  }
  this->flush_frames = 0;
  
  pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
}

static vo_frame_t *get_next_frame (vos_t *this, int64_t cur_vpts) {
  
  vo_frame_t   *img;

  pthread_mutex_lock(&this->display_img_buf_queue->mutex);
  
  img = this->display_img_buf_queue->first;

  /* 
   * still frame detection:
   */

  /* no frame? => still frame detection */

  if (!img) {

    pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
#ifdef LOG
    printf ("video_out: no frame\n");
#endif

    if (this->img_backup && (this->redraw_needed==1)) {

#ifdef LOG
      printf("video_out: generating still frame (cur_vpts = %lld) \n",
	     cur_vpts);
#endif

      /* keep playing still frames */
      pthread_mutex_lock( &this->free_img_buf_queue->mutex );
      img = duplicate_frame (this, this->img_backup );
      pthread_mutex_unlock( &this->free_img_buf_queue->mutex );
      if( img )
        img->vpts = cur_vpts;
        
      return img;

    } else {
    
      if( this->redraw_needed )
        this->redraw_needed--;
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
      pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
      return NULL;
    }

    if (this->img_backup) {
#ifdef LOG
      printf("video_out: freeing frame backup\n");
#endif
      vo_frame_dec_lock( this->img_backup );
      this->img_backup = NULL;
    }
      
    /* 
     * last frame? make backup for possible still image 
     */
    pthread_mutex_lock( &this->free_img_buf_queue->mutex );
    if (img && !img->next) {
      
      if (img->stream->stream_info[XINE_STREAM_INFO_VIDEO_HAS_STILL] ||
          img->stream->video_fifo->size(img->stream->video_fifo) < 10) {

#ifdef LOG
        printf ("video_out: possible still frame\n");
#endif
        this->img_backup = duplicate_frame (this, img);
      }
    }
    pthread_mutex_unlock( &this->free_img_buf_queue->mutex );

    /*
     * remove frame from display queue and show it
     */
    
    img = vo_remove_from_img_buf_queue_int (this->display_img_buf_queue);
    pthread_mutex_unlock(&this->display_img_buf_queue->mutex);

    return img;
  }
}

static void overlay_and_display_frame (vos_t *this, 
				       vo_frame_t *img, int64_t vpts) {
  xine_stream_t *stream;

#ifdef LOG
  printf ("video_out: displaying image with vpts = %lld\n", 
	  img->vpts);
#endif

  /* no, this is not were copy() is usually called.
   * it's just to catch special cases like late or duplicated frames.
   */
  if( img->copy && !img->copy_called )
    vo_frame_driver_copy(img);
  
  extra_info_merge( img->stream->current_extra_info, img->extra_info );

  if (this->overlay_source) {
    this->overlay_source->multiple_overlay_blend (this->overlay_source, 
						  vpts, 
						  this->driver, img,
						  this->video_loop_running && this->overlay_enabled);
  }

  /* hold current frame for snapshot feature */
  if( this->last_frame ) {
    vo_frame_dec_lock( this->last_frame );
  }
  vo_frame_inc_lock( img );
  this->last_frame = img;

  this->driver->display_frame (this->driver, img);
  
  /*
   * Wake up xine_play if it's waiting for a frame
   */
  pthread_mutex_lock(&this->streams_lock);
  for (stream = xine_list_first_content(this->streams); stream;
       stream = xine_list_next_content(this->streams)) {
    pthread_mutex_lock (&stream->first_frame_lock);
    if (stream->first_frame_flag) {
      stream->first_frame_flag = 0;
      pthread_cond_broadcast(&stream->first_frame_reached);
    }
    pthread_mutex_unlock (&stream->first_frame_lock);
  }
  pthread_mutex_unlock(&this->streams_lock);

  this->redraw_needed = 0; 
}

static void check_redraw_needed (vos_t *this, int64_t vpts) {

  if (this->overlay_source) {
    if( this->overlay_source->redraw_needed (this->overlay_source, vpts) )
      this->redraw_needed = 1; 
  }
  
  if( this->driver->redraw_needed (this->driver) )
    this->redraw_needed = 1;
}

/* special loop for paused mode
 * needed to update screen due overlay changes, resize, window
 * movement, brightness adjusting etc.
 */                   
static void paused_loop( vos_t *this, int64_t vpts )
{
  vo_frame_t   *img;
  
  pthread_mutex_lock( &this->free_img_buf_queue->mutex );
  /* prevent decoder thread from allocating new frames */
  this->free_img_buf_queue->locked_for_read = 1;
  
  while (this->clock->speed == XINE_SPEED_PAUSE) {
  
    /* we need at least one free frame to keep going */
    if( this->display_img_buf_queue->first &&
       !this->free_img_buf_queue->first ) {
    
      img = vo_remove_from_img_buf_queue (this->display_img_buf_queue);
      img->next = NULL;
      this->free_img_buf_queue->first = img;
      this->free_img_buf_queue->last  = img;
      this->free_img_buf_queue->num_buffers = 1;
    }
    
    /* set img_backup to play the same frame several times */
    if( this->display_img_buf_queue->first && !this->img_backup ) {
      this->img_backup = vo_remove_from_img_buf_queue (this->display_img_buf_queue);
      this->redraw_needed = 1;
    }
    
    check_redraw_needed( this, vpts );
    
    if( this->redraw_needed && this->img_backup ) {
      img = duplicate_frame (this, this->img_backup );
      if( img ) {
        pthread_mutex_unlock( &this->free_img_buf_queue->mutex );
        overlay_and_display_frame (this, img, vpts);
        pthread_mutex_lock( &this->free_img_buf_queue->mutex );
      }  
    }
    
    xine_usec_sleep (20000);
  } 
  
  this->free_img_buf_queue->locked_for_read = 0;
   
  if( this->free_img_buf_queue->first )
    pthread_cond_signal (&this->free_img_buf_queue->not_empty);
  pthread_mutex_unlock( &this->free_img_buf_queue->mutex );
}

static void *video_out_loop (void *this_gen) {

  int64_t            vpts, diff;
  vo_frame_t        *img;
  vos_t             *this = (vos_t *) this_gen;
  int64_t            frame_duration, next_frame_vpts;
  int64_t            usec_to_sleep;
 
  /* nice(-2); */
    
  /*
   * here it is - the heart of xine (or rather: one of the hearts
   * of xine) : the video output loop
   */
   
  frame_duration = 1500; /* default */
  next_frame_vpts = this->clock->get_current_time (this->clock);

#ifdef LOG
    printf ("video_out: loop starting...\n");
#endif

  while ( this->video_loop_running ) {

    /*
     * get current time and find frame to display
     */

    vpts = this->clock->get_current_time (this->clock);
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
      overlay_and_display_frame (this, img, vpts);
      }
    else
    {
      check_redraw_needed( this, vpts );
    }

    /*
     * if we haven't heared from the decoder for some time
     * flush it
     * test display fifo empty to protect from deadlocks
     */

    diff = vpts - this->last_delivery_pts;
    if (diff > 30000 && !this->display_img_buf_queue->first) {
      xine_stream_t *stream;
      
      pthread_mutex_lock(&this->streams_lock);
      for (stream = xine_list_first_content(this->streams); stream;
           stream = xine_list_next_content(this->streams)) {
        if (stream->video_decoder_plugin && stream->video_fifo) {
          buf_element_t *buf;
          
#ifdef LOG
	  printf ("video_out: flushing current video decoder plugin\n");
#endif
          
          buf = stream->video_fifo->buffer_pool_try_alloc (stream->video_fifo);
          if( buf ) {
            buf->type = BUF_CONTROL_FLUSH_DECODER;
            stream->video_fifo->put(stream->video_fifo, buf);
          }
        }
      }
      pthread_mutex_unlock(&this->streams_lock);

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
      vpts = this->clock->get_current_time (this->clock);
  
      if (this->clock->speed == XINE_SPEED_PAUSE)
        paused_loop (this, vpts);

      usec_to_sleep = (next_frame_vpts - vpts) * 100 / 9;

#ifdef LOG
      printf ("video_out: %lld usec to sleep at master vpts %lld\n", 
	      usec_to_sleep, vpts);
#endif
      
      if ( (next_frame_vpts - vpts) > 2*90000 )
        printf("video_out: vpts/clock error, next_vpts=%lld cur_vpts=%lld\n",
               next_frame_vpts,vpts);
               
      if (usec_to_sleep>0) 
	xine_usec_sleep (usec_to_sleep);

    } while ( (usec_to_sleep > 0) && this->video_loop_running);
  }


  /*
   * throw away undisplayed frames
   */
  
  pthread_mutex_lock(&this->display_img_buf_queue->mutex);
  img = this->display_img_buf_queue->first;
  while (img) {

    img = vo_remove_from_img_buf_queue_int (this->display_img_buf_queue);
    vo_frame_dec_lock( img );

    img = this->display_img_buf_queue->first;
  }
  pthread_mutex_unlock(&this->display_img_buf_queue->mutex);

  if (this->img_backup) {
    vo_frame_dec_lock( this->img_backup );
    this->img_backup = NULL;
  }
  if (this->last_frame) {
    vo_frame_dec_lock( this->last_frame );
    this->last_frame = NULL;
  }

  pthread_exit(NULL);
}

static uint32_t vo_get_capabilities (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  return this->driver->get_capabilities (this->driver);
}

static void vo_open (xine_video_port_t *this_gen, xine_stream_t *stream) {

  vos_t      *this = (vos_t *) this_gen;

  this->video_opened = 1;
  this->flush_frames = 0;
  this->last_delivery_pts = 0;
  if (!this->overlay_enabled && stream->spu_channel_user > -2)
    /* enable overlays if our new stream might want to show some */
    this->overlay_enabled = 1;
  pthread_mutex_lock(&this->streams_lock);
  xine_list_append_content(this->streams, stream);
  pthread_mutex_unlock(&this->streams_lock);
}

static void vo_close (xine_video_port_t *this_gen, xine_stream_t *stream) {

  vos_t      *this = (vos_t *) this_gen;
  xine_stream_t *cur;

  /* this will make sure all hide events were processed */
  if (this->overlay_source)
    this->overlay_source->flush_events (this->overlay_source);

  this->video_opened = 0;
  
  /* unregister stream */
  pthread_mutex_lock(&this->streams_lock);
  for (cur = xine_list_first_content(this->streams); cur;
       cur = xine_list_next_content(this->streams))
    if (cur == stream) {
      xine_list_delete_current(this->streams);
      break;
    }
  pthread_mutex_unlock(&this->streams_lock);
}

static int vo_status (xine_video_port_t *this_gen, xine_stream_t *stream) {

  vos_t      *this = (vos_t *) this_gen;
  xine_stream_t *cur;
  int ret = 0;

  pthread_mutex_lock(&this->streams_lock);
  for (cur = xine_list_first_content(this->streams); cur;
       cur = xine_list_next_content(this->streams))
    if (cur == stream) {
      ret = 1;
      break;
    }
  pthread_mutex_unlock(&this->streams_lock);
  
  return ret;
}


static void vo_free_img_buffers (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  vo_frame_t *img;

  while (this->free_img_buf_queue->first) {
    img = vo_remove_from_img_buf_queue (this->free_img_buf_queue);
    free(img->extra_info);
    img->dispose (img);
  }

  while (this->display_img_buf_queue->first) {
    img = vo_remove_from_img_buf_queue (this->display_img_buf_queue) ;
    free(img->extra_info);
    img->dispose (img);
  }
}

static void vo_exit (xine_video_port_t *this_gen) {

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

  this->driver->dispose (this->driver);

#ifdef LOG
  printf ("video_out: vo_exit... done\n");
#endif

  if (this->overlay_source) {
    this->overlay_source->dispose (this->overlay_source);
  }
  
  xine_list_free(this->streams);
  pthread_mutex_destroy(&this->streams_lock);

  free (this->free_img_buf_queue);
  free (this->display_img_buf_queue);

  free (this);
}

static vo_frame_t *vo_get_last_frame (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  return this->last_frame;
}

/*
 * overlay stuff 
 */

static video_overlay_instance_t *vo_get_overlay_instance (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;
  return this->overlay_source;
}

static void vo_enable_overlay (xine_video_port_t *this_gen, int overlay_enabled) {
  vos_t      *this = (vos_t *) this_gen;
  
  if (overlay_enabled) {
    /* we always ENable ... */
    this->overlay_enabled = 1;
  } else {
    /* ... but we only actually DISable, if all associated streams have SPU off */
    xine_stream_t *stream;
    pthread_mutex_lock(&this->streams_lock);
    for (stream = xine_list_first_content(this->streams) ; stream ;
         stream = xine_list_next_content(this->streams)) {
      if (stream->spu_channel_user > -2) {
	pthread_mutex_unlock(&this->streams_lock);
	return;
      }
    }
    pthread_mutex_unlock(&this->streams_lock);
    this->overlay_enabled = 0;
  }
}

/*
 * Flush video_out fifo
 */
static void vo_flush (xine_video_port_t *this_gen) {
  vos_t      *this = (vos_t *) this_gen;

  if( this->video_loop_running ) {
    pthread_mutex_lock(&this->display_img_buf_queue->mutex);
    this->flush_frames = 1;
    pthread_mutex_unlock(&this->display_img_buf_queue->mutex);
   
    /* do not try this in paused mode */
    while(this->flush_frames)
      xine_usec_sleep (20000); /* pthread_cond_t could be used here */
  }
}


xine_video_port_t *vo_new_port (xine_t *xine, vo_driver_t *driver) {

  vos_t            *this;
  int               i;
  pthread_attr_t    pth_attrs;
  int		    err;
  int               num_frame_buffers;


  this = xine_xmalloc (sizeof (vos_t)) ;

  this->xine                  = xine;
  this->clock                 = xine->clock;
  this->driver                = driver;
  this->streams               = xine_list_new();
  
  pthread_mutex_init(&this->streams_lock, NULL);

  this->vo.open                  = vo_open;
  this->vo.get_frame             = vo_get_frame;
  this->vo.get_last_frame        = vo_get_last_frame;
  this->vo.close                 = vo_close;
  this->vo.exit                  = vo_exit;
  this->vo.get_capabilities      = vo_get_capabilities;
  this->vo.enable_ovl            = vo_enable_overlay;
  this->vo.get_overlay_instance  = vo_get_overlay_instance;
  this->vo.flush                 = vo_flush;
  this->vo.status                = vo_status;
  this->vo.driver                = driver;

  this->num_frames_delivered  = 0;
  this->num_frames_skipped    = 0;
  this->num_frames_discarded  = 0;
  this->free_img_buf_queue    = vo_new_img_buf_queue ();
  this->display_img_buf_queue = vo_new_img_buf_queue ();
  this->video_loop_running    = 0;

  this->last_frame            = NULL;
  this->img_backup            = NULL;
  
  this->overlay_source        = video_overlay_new_instance();
  this->overlay_source->init (this->overlay_source);
  this->overlay_enabled       = 1;

  num_frame_buffers = driver->get_property (driver, VO_PROP_MAX_NUM_FRAMES);

  if (!num_frame_buffers)
    num_frame_buffers = NUM_FRAME_BUFFERS; /* default */
  else if (num_frame_buffers<5) 
    num_frame_buffers = 5;

  for (i=0; i<num_frame_buffers; i++) {
    vo_frame_t *img;

    img = driver->alloc_frame (driver) ;

    img->id        = i;
    
    img->port      = &this->vo;
    img->free      = vo_frame_dec_lock;
    img->displayed = vo_frame_dec_lock;
    img->draw      = vo_frame_draw;

    img->extra_info = malloc(sizeof(extra_info_t));
    
    vo_append_to_img_buf_queue (this->free_img_buf_queue,
				img);
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
    abort();
  } else
    printf ("video_out: thread created\n");

  return &this->vo;
}
