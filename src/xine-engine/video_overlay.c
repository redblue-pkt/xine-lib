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
 * $Id: video_overlay.c,v 1.3 2001/11/30 16:19:58 jcdutton Exp $
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "buffer.h"
#include "events.h"
#include "xine_internal.h"
#include "video_out/alphablend.h"
#include "xine-engine/bswap.h"
#include "xineutils.h"
#include "video_overlay.h"

/*
#define LOG_DEBUG 1
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
  
  pthread_mutex_t           video_overlay_events_mutex;  
  video_overlay_events_t    video_overlay_events[MAX_EVENTS];
  pthread_mutex_t           video_overlay_objects_mutex;  
  video_overlay_object_t    video_overlay_objects[MAX_OBJECTS];
  pthread_mutex_t           video_overlay_showing_mutex;
  video_overlay_showing_t   video_overlay_showing[MAX_SHOWING];  

} video_overlay_t;


static void add_showing_handle( video_overlay_t *this, int32_t handle )
{
  int i;
  
  pthread_mutex_lock( &this->video_overlay_showing_mutex );

  for( i = 0; i < MAX_SHOWING; i++ )
    if( this->video_overlay_showing[i].handle == handle )
      break; /* already showing */
   
  if( i == MAX_SHOWING ) {
    for( i = 0; i < MAX_SHOWING && this->video_overlay_showing[i].handle >= 0; i++ )
      ;
        
    if( i != MAX_SHOWING )
      this->video_overlay_showing[i].handle = handle;
    else
      fprintf(stderr,"video_overlay: error: no showing slots available\n");
  }
  
  pthread_mutex_unlock( &this->video_overlay_showing_mutex );
}

static void remove_showing_handle( video_overlay_t *this, int32_t handle )
{
  int i;

  pthread_mutex_lock( &this->video_overlay_showing_mutex );
  
  for( i = 0; i < MAX_SHOWING; i++ ) {
    if( this->video_overlay_showing[i].handle == handle ) {
      this->video_overlay_showing[i].handle = -1;
    }
  }
  
  pthread_mutex_unlock( &this->video_overlay_showing_mutex );
}

static void remove_events_handle( video_overlay_t *this, int32_t handle )
{
  uint32_t   last_event,this_event;

  pthread_mutex_lock( &this->video_overlay_events_mutex );
  
  this_event=0;
  do {
    last_event=this_event;
    this_event=this->video_overlay_events[last_event].next_event;
  
    while( this_event && 
        this->video_overlay_events[this_event].event->object.handle == handle ) {
      /* remove event from pts list */
      this->video_overlay_events[last_event].next_event=
        this->video_overlay_events[this_event].next_event;

      /* free its overlay */ 
      if( this->video_overlay_events[this_event].event->object.overlay ) {   
        if( this->video_overlay_events[this_event].event->object.overlay->rle )
          free( this->video_overlay_events[this_event].event->object.overlay->rle );
        free(this->video_overlay_events[this_event].event->object.overlay);
        this->video_overlay_events[this_event].event->object.overlay = NULL;
      }
      
      /* mark as free */
      this->video_overlay_events[this_event].next_event = 0;
      this->video_overlay_events[this_event].event->event_type = EVENT_NULL;
      
      this_event=this->video_overlay_events[last_event].next_event;
    }
  } while ( this_event );
 
  pthread_mutex_unlock( &this->video_overlay_events_mutex );
}


/*
  allocate a handle from the object pool (exported function)
 */
static int32_t video_overlay_get_handle(video_overlay_instance_t *this_gen, int object_type ) {
  video_overlay_t *this = (video_overlay_t *) this_gen;
  int n;
  
  pthread_mutex_lock( &this->video_overlay_objects_mutex );
  
  for( n=0; n < MAX_OBJECTS && this->video_overlay_objects[n].handle > -1; n++ )
    ;
  
  if (n == MAX_OBJECTS) {
    n = -1;
  } else {
    this->video_overlay_objects[n].handle = n;
    this->video_overlay_objects[n].object_type = object_type;
  }
  
  pthread_mutex_unlock( &this->video_overlay_objects_mutex );
  return n;
}

/* 
  free a handle from the object pool (internal function)
 */
static void internal_video_overlay_free_handle(video_overlay_t *this, int32_t handle) {
    
  pthread_mutex_lock( &this->video_overlay_objects_mutex );

  if( this->video_overlay_objects[handle].overlay ) {
    if( this->video_overlay_objects[handle].overlay->rle )
      free( this->video_overlay_objects[handle].overlay->rle );
    free( this->video_overlay_objects[handle].overlay );
    this->video_overlay_objects[handle].overlay = NULL; 
  }
  this->video_overlay_objects[handle].handle = -1;

  pthread_mutex_unlock( &this->video_overlay_objects_mutex );
}

/*
   exported free handle function. must take care of removing the object
   from showing and events lists.
*/
static void video_overlay_free_handle(video_overlay_instance_t *this_gen, int32_t handle) {
  video_overlay_t *this = (video_overlay_t *) this_gen;

  remove_showing_handle(this,handle);
  remove_events_handle(this,handle);
  internal_video_overlay_free_handle(this,handle);
}



static void video_overlay_reset (video_overlay_t *this) {
  int i;
  
  pthread_mutex_lock (&this->video_overlay_events_mutex);
  for (i=0; i < MAX_EVENTS; i++) {
    if (this->video_overlay_events[i].event == NULL) {
      this->video_overlay_events[i].event = xine_xmalloc (sizeof(video_overlay_event_t));
#ifdef LOG_DEBUG
      printf ("video_overlay: MALLOC2: this->video_overlay_events[%d].event %p, len=%d\n",
	      i,
	      this->video_overlay_events[i].event,
	      sizeof(video_overlay_event_t));
#endif
    }
    this->video_overlay_events[i].event->event_type = 0;  /* Empty slot */
    this->video_overlay_events[i].next_event = 0;    
  }
  pthread_mutex_unlock (&this->video_overlay_events_mutex);
  
  for (i=0; i < MAX_OBJECTS; i++) {
    internal_video_overlay_free_handle(this, i);
  }
   
  for( i = 0; i < MAX_SHOWING; i++ )
    this->video_overlay_showing[i].handle = -1;
}


static void video_overlay_init (video_overlay_instance_t *this_gen) {

  video_overlay_t *this = (video_overlay_t *) this_gen;

  pthread_mutex_init (&this->video_overlay_events_mutex,NULL);
  pthread_mutex_init (&this->video_overlay_objects_mutex,NULL);
  pthread_mutex_init (&this->video_overlay_showing_mutex,NULL);
  
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
 * note2: handle will be freed on HIDE events
 */
static int32_t video_overlay_add_event(video_overlay_instance_t *this_gen,  void *event_gen ) {
  video_overlay_event_t *event = (video_overlay_event_t *) event_gen;
  video_overlay_t *this = (video_overlay_t *) this_gen;
  uint32_t   last_event,this_event,new_event;

  pthread_mutex_lock (&this->video_overlay_events_mutex);
  
  /* We skip the 0 entry because that is used as a pointer to the first event.*/
  /* Find a free event slot */
  for( new_event = 1; new_event<MAX_EVENTS && 
       this->video_overlay_events[new_event].event->event_type > 0; new_event++ )
    ;
  
  if (new_event < MAX_EVENTS) {
    /* Find position in event queue to be added. */
    this_event=0;
    /* Find where in the current queue to insert the event. I.E. Sort it. */
    do {
      last_event=this_event;
      this_event=this->video_overlay_events[last_event].next_event;
    } while ( this_event && this->video_overlay_events[this_event].event->vpts <= event->vpts );

    this->video_overlay_events[last_event].next_event=new_event;
    this->video_overlay_events[new_event].next_event=this_event;
    
    /* memcpy everything except the actual image */
    if ( this->video_overlay_events[new_event].event == NULL ) {
      fprintf(stderr,"video_overlay: error: event slot is NULL!\n");
    }
    this->video_overlay_events[new_event].event->event_type=event->event_type;
    this->video_overlay_events[new_event].event->vpts=event->vpts;
    this->video_overlay_events[new_event].event->object.handle=event->object.handle;

    if ( this->video_overlay_events[new_event].event->object.overlay ) {
      fprintf(stderr,"video_overlay: error: event->object.overlay was not freed!\n");
    }
    
    if( event->object.overlay ) {
      this->video_overlay_events[new_event].event->object.overlay = xine_xmalloc (sizeof(vo_overlay_t));
      xine_fast_memcpy(this->video_overlay_events[new_event].event->object.overlay, 
           event->object.overlay, sizeof(vo_overlay_t));
    
      /* We took the callers rle and data, therefore it will be our job to free it */
      /* clear callers overlay so it will not be freed twice */
      memset(event->object.overlay,0,sizeof(vo_overlay_t));
    } else {
      this->video_overlay_events[new_event].event->object.overlay = NULL;
    }
  } else {
    fprintf(stderr, "No spare subtitle event slots\n");
    new_event = -1;
  }
  
  pthread_mutex_unlock (&this->video_overlay_events_mutex);
   
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
#endif
  return;
} 

/*
   process overlay events
   if vpts == 0 will process everything now (used in flush)
*/
static void video_overlay_event( video_overlay_t *this, int vpts ) {
  int32_t      handle;
  uint32_t     this_event;
  
  pthread_mutex_lock (&this->video_overlay_events_mutex);
  
  this_event=this->video_overlay_events[0].next_event;
  while ( this_event && (vpts > this->video_overlay_events[this_event].event->vpts ||
          vpts == 0) ) {
    handle=this->video_overlay_events[this_event].event->object.handle;
    switch( this->video_overlay_events[this_event].event->event_type ) {
      case EVENT_SHOW_SPU:
#ifdef LOG_DEBUG
        printf ("video_overlay: SHOW SPU NOW\n");
#endif
        if (this->video_overlay_events[this_event].event->object.overlay != NULL) {
          internal_video_overlay_free_handle( this, handle );
          
          this->video_overlay_objects[handle].handle = handle;
          if( this->video_overlay_objects[handle].overlay ) {
            fprintf(stderr,"video_overlay: error: object->overlay was not freed!\n");
          }
          this->video_overlay_objects[handle].overlay = 
             this->video_overlay_events[this_event].event->object.overlay;
          this->video_overlay_events[this_event].event->object.overlay = NULL;
        
          add_showing_handle( this, handle );
        }
        break;

      /* implementation for HIDE_SPU and HIDE_MENU is the same.
         i will keep them separated in case we need something special...
      */
      case EVENT_HIDE_SPU:
#ifdef LOG_DEBUG
        printf ("video_overlay: HIDE SPU NOW\n");
#endif
        free(this->video_overlay_events[this_event].event->object.overlay);
          this->video_overlay_events[this_event].event->object.overlay = NULL; 
        remove_showing_handle( this, handle );
        internal_video_overlay_free_handle( this, handle );
        break;
  
      case EVENT_HIDE_MENU:
#ifdef LOG_DEBUG
        printf ("video_overlay: HIDE MENU NOW %d\n",handle);
#endif
        free(this->video_overlay_events[this_event].event->object.overlay);
          this->video_overlay_events[this_event].event->object.overlay = NULL; 
        remove_showing_handle( this, handle );
        internal_video_overlay_free_handle( this, handle );
        break;
  
      case EVENT_MENU_SPU:
        /* mixes palette and copy rle */
#ifdef LOG_DEBUG
        printf ("MENU SPU NOW\n");
#endif
        if (this->video_overlay_events[this_event].event->object.overlay != NULL) {
          vo_overlay_t *event_overlay = this->video_overlay_events[this_event].event->object.overlay;
          vo_overlay_t *overlay;

          /* we need to allocate overlay on first EVENT_MENU_SPU */          
          if( !this->video_overlay_objects[handle].overlay ) {
            this->video_overlay_objects[handle].overlay
               = xine_xmalloc( sizeof(vo_overlay_t) );
          }
          overlay = this->video_overlay_objects[handle].overlay;
          
          this->video_overlay_objects[handle].handle = handle;
          
          /* If rle is not empty, free it first */
          if(overlay->rle) {
            free (overlay->rle);
          }
          overlay->rle = event_overlay->rle;
          
          overlay->data_size = event_overlay->data_size;
          overlay->num_rle = event_overlay->num_rle;
          overlay->x = event_overlay->x;
          overlay->y = event_overlay->y;
          overlay->width = event_overlay->width;
          overlay->height = event_overlay->height;
          overlay->rgb_clut = event_overlay->rgb_clut;
          if((event_overlay->color[0] +
              event_overlay->color[1] +
              event_overlay->color[2] +
              event_overlay->color[3]) > 0 ) {
            overlay->color[0] = event_overlay->color[0];
            overlay->color[1] = event_overlay->color[1];
            overlay->color[2] = event_overlay->color[2];
            overlay->color[3] = event_overlay->color[3];
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
          add_showing_handle( this, handle );
          
          /* The null test was done at the start of this case statement */
          free (this->video_overlay_events[this_event].event->object.overlay);
          this->video_overlay_events[this_event].event->object.overlay = NULL;
        }
        break;
  
      case EVENT_MENU_BUTTON:
        /* mixes palette and copy clip coords */
#ifdef LOG_DEBUG
        printf ("MENU BUTTON NOW\n");
#endif
        if (this->video_overlay_events[this_event].event->object.overlay != NULL) {
          vo_overlay_t *overlay = this->video_overlay_objects[handle].overlay;
          vo_overlay_t *event_overlay = this->video_overlay_events[this_event].event->object.overlay;
          
          if( !this->video_overlay_objects[handle].overlay ) {
            fprintf(stderr,"video_overlay: error: button event received and no overlay allocated.\n");
          }
               
          this->video_overlay_objects[handle].handle = handle;
          overlay->clip_top = event_overlay->clip_top;
          overlay->clip_bottom = event_overlay->clip_bottom;
          overlay->clip_left = event_overlay->clip_left;
          overlay->clip_right = event_overlay->clip_right;
          
          if((event_overlay->color[0] +
              event_overlay->color[1] +
              event_overlay->color[2] +
              event_overlay->color[3]) > 0 ) {
            overlay->color[0] = event_overlay->color[0];
            overlay->color[1] = event_overlay->color[1];
            overlay->color[2] = event_overlay->color[2];
            overlay->color[3] = event_overlay->color[3];
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
          add_showing_handle( this, handle );

          if( this->video_overlay_events[this_event].event->object.overlay->rle ) {
            printf ("video_overlay: warning EVENT_MENU_BUTTON with rle data\n");
            free( this->video_overlay_events[this_event].event->object.overlay->rle );
          }
            
          /* The null test was done at the start of this case statement */
          free (this->video_overlay_events[this_event].event->object.overlay);
          this->video_overlay_events[this_event].event->object.overlay = NULL;
        }
        break;
  
      default:
        printf ("video_overlay: unhandled event type\n");
        break;
    }
    
    this->video_overlay_events[0].next_event = this->video_overlay_events[this_event].next_event;    
    this->video_overlay_events[this_event].next_event = 0;
    this->video_overlay_events[this_event].event->event_type = 0;
  
    this_event=this->video_overlay_events[0].next_event;
  }
  
  pthread_mutex_unlock (&this->video_overlay_events_mutex);
}
  
/* This is called from video_out.c 
 * must call output->overlay_blend for each active overlay.
 */
static void video_overlay_multiple_overlay_blend(video_overlay_instance_t *this_gen, int vpts, 
                                         vo_driver_t *output, vo_frame_t *vo_img, int enabled) {
  video_overlay_t *this = (video_overlay_t *) this_gen;
  int i;
  int32_t  handle;

  /* Look at next events, if current video vpts > first event on queue, process the event 
   * else just continue 
   */
  video_overlay_event( this, vpts );
  
  /* Scan through 5 entries and display any present. 
   */
  pthread_mutex_lock( &this->video_overlay_showing_mutex );
  for( i = 0; enabled && output->overlay_blend && i < MAX_SHOWING; i++ ) {
    handle=this->video_overlay_showing[i].handle; 
    if (handle >= 0 ) {
      output->overlay_blend(output, vo_img, this->video_overlay_objects[handle].overlay);
    }
  }
  pthread_mutex_unlock( &this->video_overlay_showing_mutex );
}


/* this should be called on stream end or stop to make sure every 
   hide event is processed.
*/
static void video_overlay_flush_events(video_overlay_instance_t *this_gen )
{
  video_overlay_t *this = (video_overlay_t *) this_gen;
  
  video_overlay_event( this, 0 );
}


video_overlay_instance_t *video_overlay_new_instance () {

  video_overlay_t *this;

  this = (video_overlay_t *) xine_xmalloc (sizeof (video_overlay_t));

  this->video_overlay.init                = video_overlay_init;
  this->video_overlay.get_handle          = video_overlay_get_handle;
  this->video_overlay.free_handle         = video_overlay_free_handle;
  this->video_overlay.add_event           = video_overlay_add_event;
  this->video_overlay.flush_events        = video_overlay_flush_events;
  this->video_overlay.multiple_overlay_blend = video_overlay_multiple_overlay_blend;

  return (video_overlay_instance_t *) &this->video_overlay;
}

