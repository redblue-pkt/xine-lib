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
 * $Id: video_overlay.c,v 1.24 2002/09/04 23:31:13 guenter Exp $
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "buffer.h"
#include "xine_internal.h"
#include "video_out/alphablend.h"
#include "xine-engine/bswap.h"
#include "xineutils.h"
#include "video_overlay.h"

/*
#define LOG_DEBUG
*/

typedef struct video_overlay_events_s {
  video_overlay_event_t  *event;
  uint32_t	next_event;
} video_overlay_events_t;

typedef struct video_overlay_showing_s {
  int32_t	handle; /* -1 means not allocated */
} video_overlay_showing_t;


typedef struct video_overlay_s {
  video_overlay_instance_t  video_overlay;
  
  pthread_mutex_t           events_mutex;  
  video_overlay_events_t    events[MAX_EVENTS];
  pthread_mutex_t           objects_mutex;  
  video_overlay_object_t    objects[MAX_OBJECTS];
  pthread_mutex_t           showing_mutex;
  video_overlay_showing_t   showing[MAX_SHOWING];  
  int                       showing_changed;
} video_overlay_t;


static void add_showing_handle( video_overlay_t *this, int32_t handle )
{
  int i;
  
  pthread_mutex_lock( &this->showing_mutex );
  this->showing_changed++;
  
  for( i = 0; i < MAX_SHOWING; i++ )
    if( this->showing[i].handle == handle )
      break; /* already showing */
   
  if( i == MAX_SHOWING ) {
    for( i = 0; i < MAX_SHOWING && this->showing[i].handle >= 0; i++ )
      ;
        
    if( i != MAX_SHOWING )
      this->showing[i].handle = handle;
    else
      printf("video_overlay: error: no showing slots available\n");
  }
  
  pthread_mutex_unlock( &this->showing_mutex );
}

static void remove_showing_handle( video_overlay_t *this, int32_t handle )
{
  int i;

  pthread_mutex_lock( &this->showing_mutex );
  this->showing_changed++;
  
  for( i = 0; i < MAX_SHOWING; i++ ) {
    if( this->showing[i].handle == handle ) {
      this->showing[i].handle = -1;
    }
  }
  
  pthread_mutex_unlock( &this->showing_mutex );
}

static void remove_events_handle( video_overlay_t *this, int32_t handle, int lock )
{
  uint32_t   last_event,this_event;

  if( lock )
    pthread_mutex_lock( &this->events_mutex );
  
  this_event=0;
  do {
    last_event=this_event;
    this_event=this->events[last_event].next_event;
  
    while( this_event && 
        this->events[this_event].event->object.handle == handle ) {
      /* remove event from pts list */
      this->events[last_event].next_event=
        this->events[this_event].next_event;

      /* free its overlay */ 
      if( this->events[this_event].event->object.overlay ) {   
        if( this->events[this_event].event->object.overlay->rle )
          free( this->events[this_event].event->object.overlay->rle );
        free(this->events[this_event].event->object.overlay);
        this->events[this_event].event->object.overlay = NULL;
      }
      
      /* mark as free */
      this->events[this_event].next_event = 0;
      this->events[this_event].event->event_type = EVENT_NULL;
      
      this_event=this->events[last_event].next_event;
    }
  } while ( this_event );
 
  if( lock )
    pthread_mutex_unlock( &this->events_mutex );
}


/*
  allocate a handle from the object pool (exported function)
 */
static int32_t video_overlay_get_handle(video_overlay_instance_t *this_gen, int object_type ) {
  video_overlay_t *this = (video_overlay_t *) this_gen;
  int n;
  
  pthread_mutex_lock( &this->objects_mutex );
  
  for( n=0; n < MAX_OBJECTS && this->objects[n].handle > -1; n++ )
    ;
  
  if (n == MAX_OBJECTS) {
    n = -1;
  } else {
    this->objects[n].handle = n;
    this->objects[n].object_type = object_type;
  }
  
  pthread_mutex_unlock( &this->objects_mutex );
  return n;
}

/* 
  free a handle from the object pool (internal function)
 */
static void internal_video_overlay_free_handle(video_overlay_t *this, int32_t handle) {
    
  pthread_mutex_lock( &this->objects_mutex );

  if( this->objects[handle].overlay ) {
    if( this->objects[handle].overlay->rle )
      free( this->objects[handle].overlay->rle );
    free( this->objects[handle].overlay );
    this->objects[handle].overlay = NULL; 
  }
  this->objects[handle].handle = -1;

  pthread_mutex_unlock( &this->objects_mutex );
}

/*
   exported free handle function. must take care of removing the object
   from showing and events lists.
*/
static void video_overlay_free_handle(video_overlay_instance_t *this_gen, int32_t handle) {
  video_overlay_t *this = (video_overlay_t *) this_gen;

  remove_showing_handle(this,handle);
  remove_events_handle(this,handle,1);
  internal_video_overlay_free_handle(this,handle);
}


static void video_overlay_reset (video_overlay_t *this) {
  int i;
  
  pthread_mutex_lock (&this->events_mutex);
  for (i=0; i < MAX_EVENTS; i++) {
    if (this->events[i].event == NULL) {
      this->events[i].event = xine_xmalloc (sizeof(video_overlay_event_t));
#ifdef LOG_DEBUG
      printf ("video_overlay: MALLOC2: this->events[%d].event %p, len=%d\n",
	      i,
	      this->events[i].event,
	      sizeof(video_overlay_event_t));
#endif
    }
    this->events[i].event->event_type = 0;  /* Empty slot */
    this->events[i].next_event = 0;    
  }
  pthread_mutex_unlock (&this->events_mutex);
  
  for (i=0; i < MAX_OBJECTS; i++) {
    internal_video_overlay_free_handle(this, i);
  }
   
  for( i = 0; i < MAX_SHOWING; i++ )
    this->showing[i].handle = -1;
    
  this->showing_changed = 0;
}


static void video_overlay_init (video_overlay_instance_t *this_gen) {

  video_overlay_t *this = (video_overlay_t *) this_gen;

  pthread_mutex_init (&this->events_mutex,NULL);
  pthread_mutex_init (&this->objects_mutex,NULL);
  pthread_mutex_init (&this->showing_mutex,NULL);
  
  video_overlay_reset(this);
}


/* add an event to the events queue, sort the queue based on vpts.
 * This can be the API entry point for DVD subtitles.
 * One calls this function with an event, the event contains an overlay
 * and a vpts when to action/process it. vpts of 0 means action the event now.
 * One also has a handle, so one can match show and hide events.
 *
 * note: on success event->object.overlay is "taken" (caller will not have access
 *       to overlay data including rle).
 * note2: handle will not be freed on HIDE events
 *        the handle is removed from the currently showing list.
 */
static int32_t video_overlay_add_event(video_overlay_instance_t *this_gen,  void *event_gen ) {
  video_overlay_event_t *event = (video_overlay_event_t *) event_gen;
  video_overlay_t *this = (video_overlay_t *) this_gen;
  uint32_t   last_event,this_event,new_event;

  pthread_mutex_lock (&this->events_mutex);
  
  /* We skip the 0 entry because that is used as a pointer to the first event.*/
  /* Find a free event slot */
  for( new_event = 1; new_event<MAX_EVENTS && 
       this->events[new_event].event->event_type > 0; new_event++ )
    ;
  
  if (new_event < MAX_EVENTS) {
    /* Find position in event queue to be added. */
    this_event=0;
    /* Find where in the current queue to insert the event. I.E. Sort it. */
    do {
      last_event=this_event;
      this_event=this->events[last_event].next_event;
    } while ( this_event && this->events[this_event].event->vpts <= event->vpts );

    this->events[last_event].next_event=new_event;
    this->events[new_event].next_event=this_event;
    
    /* memcpy everything except the actual image */
    if ( this->events[new_event].event == NULL ) {
      printf("video_overlay: error: event slot is NULL!\n");
    }
    this->events[new_event].event->event_type=event->event_type;
    this->events[new_event].event->vpts=event->vpts;
    this->events[new_event].event->object.handle=event->object.handle;
    this->events[new_event].event->object.pts=event->object.pts;

    if ( this->events[new_event].event->object.overlay ) {
      printf("video_overlay: add_event: event->object.overlay was not freed!\n");
    }
    
    if( event->object.overlay ) {
      this->events[new_event].event->object.overlay = xine_xmalloc (sizeof(vo_overlay_t));
      xine_fast_memcpy(this->events[new_event].event->object.overlay, 
           event->object.overlay, sizeof(vo_overlay_t));
    
      /* We took the callers rle and data, therefore it will be our job to free it */
      /* clear callers overlay so it will not be freed twice */
      memset(event->object.overlay,0,sizeof(vo_overlay_t));
    } else {
      this->events[new_event].event->object.overlay = NULL;
    }
  } else {
    printf("video_overlay:No spare subtitle event slots\n");
    new_event = -1;
  }
  
  pthread_mutex_unlock (&this->events_mutex);
   
  return new_event;
}


/* not currently used. James might need this for debugging menu stuff */
static void video_overlay_print_overlay( vo_overlay_t *ovl ) {
#ifdef LOG_DEBUG
  printf ("video_overlay: OVERLAY to show\n");
  printf ("video_overlay: \tx = %d y = %d width = %d height = %d\n",
	  ovl->x, ovl->y, ovl->width, ovl->height );
  printf ("video_overlay: \tclut [%x %x %x %x]\n",
	  ovl->color[0], ovl->color[1], ovl->color[2], ovl->color[3]);
  printf ("video_overlay: \ttrans [%d %d %d %d]\n",
	  ovl->trans[0], ovl->trans[1], ovl->trans[2], ovl->trans[3]);
  printf ("video_overlay: \tclip top=%d bottom=%d left=%d right=%d\n",
	  ovl->clip_top, ovl->clip_bottom, ovl->clip_left, ovl->clip_right);
  printf ("video_overlay: \tclip_clut [%x %x %x %x]\n",
	  ovl->clip_color[0], ovl->clip_color[1], ovl->clip_color[2], ovl->clip_color[3]);
  printf ("video_overlay: \tclip_trans [%d %d %d %d]\n",
	  ovl->clip_trans[0], ovl->clip_trans[1], ovl->clip_trans[2], ovl->clip_trans[3]);
#endif
  return;
} 

/*
   process overlay events
   if vpts == 0 will process everything now (used in flush)
   return true if something has been processed
*/
static int video_overlay_event( video_overlay_t *this, int64_t vpts ) {
  int32_t      handle;
  uint32_t     this_event;
  int          processed = 0;
  
  pthread_mutex_lock (&this->events_mutex);
  
  this_event=this->events[0].next_event;
  while ( this_event && (vpts > this->events[this_event].event->vpts ||
          vpts == 0) ) {
    processed++;
    handle=this->events[this_event].event->object.handle;
    switch( this->events[this_event].event->event_type ) {
      case EVENT_SHOW_SPU:
#ifdef LOG_DEBUG
        printf ("video_overlay: SHOW SPU NOW\n");
#endif
        if (this->events[this_event].event->object.overlay != NULL) {
#ifdef LOG_DEBUG
          video_overlay_print_overlay( this->events[this_event].event->object.overlay ) ;
#endif
          /* this->objects[handle].overlay is about to be
           * overwritten by this event data. make sure we free it if needed.
           */
          if( this->objects[handle].overlay ) {
            if( this->objects[handle].overlay->rle )
              free( this->objects[handle].overlay->rle );
            free( this->objects[handle].overlay );
            this->objects[handle].overlay = NULL; 
          }
          
          this->objects[handle].handle = handle;
          if( this->objects[handle].overlay ) {
            printf("video_overlay: error: object->overlay was not freed!\n");
          }
          this->objects[handle].overlay = 
             this->events[this_event].event->object.overlay;
          this->objects[handle].pts = 
             this->events[this_event].event->object.pts;
          this->events[this_event].event->object.overlay = NULL;
        
          add_showing_handle( this, handle );
        }
        break;

      case EVENT_FREE_HANDLE:
#ifdef LOG_DEBUG
        printf ("video_overlay: FREE SPU NOW\n");
#endif
        /* free any overlay associated with this event */
        if (this->events[this_event].event->object.overlay != NULL) {
          free(this->events[this_event].event->object.overlay);
          this->events[this_event].event->object.overlay = NULL; 
        }
        /* this avoid removing this_event from the queue
         * (it will be removed at the end of this loop) */
        this->events[this_event].event->object.handle = -1;
        remove_showing_handle(this,handle);
        remove_events_handle(this,handle,0);
        internal_video_overlay_free_handle( this, handle );
        break;

      /* implementation for HIDE_SPU and HIDE_MENU is the same.
         i will keep them separated in case we need something special...
      */
      case EVENT_HIDE_SPU:
#ifdef LOG_DEBUG
        printf ("video_overlay: HIDE SPU NOW\n");
#endif
        /* free any overlay associated with this event */
        if (this->events[this_event].event->object.overlay != NULL) {
          free(this->events[this_event].event->object.overlay);
          this->events[this_event].event->object.overlay = NULL; 
        }
        remove_showing_handle( this, handle );
        break;
  
      case EVENT_HIDE_MENU:
#ifdef LOG_DEBUG
        printf ("video_overlay: HIDE MENU NOW %d\n",handle);
#endif
        if (this->events[this_event].event->object.overlay != NULL) {
          free(this->events[this_event].event->object.overlay);
          this->events[this_event].event->object.overlay = NULL; 
        }
        remove_showing_handle( this, handle );
        break;
  
      case EVENT_SHOW_MENU:
#ifdef LOG_DEBUG
        printf ("video_overlay: SHOW MENU NOW\n");
#endif
        if (this->events[this_event].event->object.overlay != NULL) {
#ifdef LOG_DEBUG
          video_overlay_print_overlay( this->events[this_event].event->object.overlay ) ;
#endif
          /* this->objects[handle].overlay is about to be
           * overwritten by this event data. make sure we free it if needed.
           */
          if( this->objects[handle].overlay ) {
            if( this->objects[handle].overlay->rle )
              free( this->objects[handle].overlay->rle );
            free( this->objects[handle].overlay );
            this->objects[handle].overlay = NULL; 
          }
          
          this->objects[handle].handle = handle;
          if( this->objects[handle].overlay ) {
            printf("video_overlay: error: object->overlay was not freed!\n");
          }
          this->objects[handle].overlay = 
             this->events[this_event].event->object.overlay;
          this->objects[handle].pts = 
             this->events[this_event].event->object.pts;
          this->events[this_event].event->object.overlay = NULL;
        
          add_showing_handle( this, handle );
        }
        break;
  
      case EVENT_MENU_BUTTON:
        /* mixes palette and copy clip coords */
#ifdef LOG_DEBUG
        printf ("video_overlay:MENU BUTTON NOW\n");
#endif
        if ( (this->events[this_event].event->object.overlay != NULL) &&
             (this->objects[handle].overlay) &&
             (this->events[this_event].event->object.pts == 
                this->objects[handle].pts) ) {
          vo_overlay_t *overlay = this->objects[handle].overlay;
          vo_overlay_t *event_overlay = this->events[this_event].event->object.overlay;
          printf ("video_overlay:overlay present\n");
          this->objects[handle].handle = handle;
          overlay->clip_top = event_overlay->clip_top;
          overlay->clip_bottom = event_overlay->clip_bottom;
          overlay->clip_left = event_overlay->clip_left;
          overlay->clip_right = event_overlay->clip_right;
          overlay->clip_color[0] = event_overlay->clip_color[0];
          overlay->clip_color[1] = event_overlay->clip_color[1];
          overlay->clip_color[2] = event_overlay->clip_color[2];
          overlay->clip_color[3] = event_overlay->clip_color[3];
          overlay->clip_trans[0] = event_overlay->clip_trans[0];
          overlay->clip_trans[1] = event_overlay->clip_trans[1];
          overlay->clip_trans[2] = event_overlay->clip_trans[2];
          overlay->clip_trans[3] = event_overlay->clip_trans[3];
          overlay->clip_rgb_clut = event_overlay->clip_rgb_clut;
/***********************************
          if((event_overlay->color[0] +
              event_overlay->color[1] +
              event_overlay->color[2] +
              event_overlay->color[3]) > 0 ) {
            overlay->color[0] = event_overlay->color[0];
            overlay->color[1] = event_overlay->color[1];
            overlay->color[2] = event_overlay->color[2];
            overlay->color[3] = event_overlay->color[3];
            
            overlay->rgb_clut = event_overlay->rgb_clut;
          }
          if((event_overlay->trans[0] +
              event_overlay->trans[1] +
              event_overlay->trans[2] +
              event_overlay->trans[3]) > 0 ) {
            overlay->trans[0] = event_overlay->trans[0];
            overlay->trans[1] = event_overlay->trans[1];
            overlay->trans[2] = event_overlay->trans[2];
            overlay->trans[3] = event_overlay->trans[3];
          }
***********************************/
#ifdef LOG_DEBUG
          video_overlay_print_overlay( this->events[this_event].event->object.overlay ) ;
#endif
          add_showing_handle( this, handle );
        } else {
          printf ("video_overlay:overlay not present\n");
        }
        if ( (this->events[this_event].event->object.pts != 
                this->objects[handle].pts) ) {
          printf ("video_overlay:MENU BUTTON DROPPED menu pts=%lld spu pts=%lld\n",      
            this->events[this_event].event->object.pts,
            this->objects[handle].pts);
        }

        if( this->events[this_event].event->object.overlay->rle ) {
          printf ("video_overlay: warning EVENT_MENU_BUTTON with rle data\n");
          free( this->events[this_event].event->object.overlay->rle );
          this->events[this_event].event->object.overlay->rle = NULL;
        }
            
        if (this->events[this_event].event->object.overlay != NULL) {
          free (this->events[this_event].event->object.overlay);
          this->events[this_event].event->object.overlay = NULL;
        }
        break;
  
      default:
        printf ("video_overlay: unhandled event type\n");
        break;
    }
    
    this->events[0].next_event = this->events[this_event].next_event;    
    this->events[this_event].next_event = 0;
    this->events[this_event].event->event_type = 0;
  
    this_event=this->events[0].next_event;
  }
  
  pthread_mutex_unlock (&this->events_mutex);

  return processed;
}
  
/* This is called from video_out.c 
 * must call output->overlay_blend for each active overlay.
 */
static void video_overlay_multiple_overlay_blend (video_overlay_instance_t *this_gen, int64_t vpts, 
						  xine_vo_driver_t *output, vo_frame_t *vo_img, int enabled) {
  video_overlay_t *this = (video_overlay_t *) this_gen;
  int i;
  int32_t  handle;

  /* Look at next events, if current video vpts > first event on queue, process the event 
   * else just continue 
   */
  video_overlay_event( this, vpts );
  
  /* Scan through 5 entries and display any present. 
   */
  pthread_mutex_lock( &this->showing_mutex );

  if( output->overlay_begin )
    output->overlay_begin(output, vo_img, this->showing_changed);
  
  for( i = 0; enabled && output->overlay_blend && i < MAX_SHOWING; i++ ) {
    handle=this->showing[i].handle; 
    if (handle >= 0 ) {
      output->overlay_blend(output, vo_img, this->objects[handle].overlay);
    }
  }
  
  if( output->overlay_end )
    output->overlay_end(output, vo_img);
  
  this->showing_changed = 0;
  
  pthread_mutex_unlock( &this->showing_mutex );
}


/* this should be called on stream end or stop to make sure every 
   hide event is processed.
*/
static void video_overlay_flush_events(video_overlay_instance_t *this_gen )
{
  video_overlay_t *this = (video_overlay_t *) this_gen;
  
  video_overlay_event( this, 0 );
}

/* this is called from video_out.c on still frames to check 
   if a redraw is needed.
*/
static int video_overlay_redraw_needed(video_overlay_instance_t *this_gen, int64_t vpts )
{
  video_overlay_t *this = (video_overlay_t *) this_gen;
  
  video_overlay_event( this, vpts );
  return this->showing_changed;
}


static void video_overlay_dispose(video_overlay_instance_t *this_gen) {

  video_overlay_t *this = (video_overlay_t *) this_gen;
  int i;

  for (i=0; i < MAX_EVENTS; i++) {
    if (this->events[i].event != NULL) {
      if (this->events[i].event->object.overlay != NULL) {
        if (this->events[i].event->object.overlay->rle)
          free (this->events[i].event->object.overlay->rle);
        free (this->events[i].event->object.overlay);
      }
      free (this->events[i].event);
    }
  }

  for (i=0; i < MAX_OBJECTS; i++) {
    if (this->objects[i].overlay != NULL) {
      if (this->objects[i].overlay->rle)
        free (this->objects[i].overlay->rle);
      free (this->objects[i].overlay);
    }
  }

  free (this);
}


video_overlay_instance_t *video_overlay_new_instance () {

  video_overlay_t *this;

  this = (video_overlay_t *) xine_xmalloc (sizeof (video_overlay_t));

  this->video_overlay.init                = video_overlay_init;
  this->video_overlay.dispose             = video_overlay_dispose;
  this->video_overlay.get_handle          = video_overlay_get_handle;
  this->video_overlay.free_handle         = video_overlay_free_handle;
  this->video_overlay.add_event           = video_overlay_add_event;
  this->video_overlay.flush_events        = video_overlay_flush_events;
  this->video_overlay.redraw_needed       = video_overlay_redraw_needed;
  this->video_overlay.multiple_overlay_blend = video_overlay_multiple_overlay_blend;

  return (video_overlay_instance_t *) &this->video_overlay;
}
