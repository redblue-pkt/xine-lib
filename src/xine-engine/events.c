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
 * $Id:
 *
 * Event handling functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"

int xine_register_event_listener (xine_p this_ro, 
				  xine_event_listener_cb_t listener,
				  void *user_data) {
  xine_t *this = (xine_t *)this_ro;
  /* Ensure the listener is non-NULL */
  if(listener == NULL) {
    return 0;
  }

  pthread_mutex_lock(&this->event_lock);
  /* Check we hava a slot free */
  if(this->num_event_listeners < XINE_MAX_EVENT_LISTENERS) {
    
    this->event_listeners[this->num_event_listeners] = listener;
    this->event_listener_user_data[this->num_event_listeners] = user_data;

    this->num_event_listeners++;

    pthread_mutex_unlock(&this->event_lock);
    return 1;
  } 

  pthread_mutex_unlock(&this->event_lock);
  return 0;
}

void xine_send_event(xine_p this, xine_event_t *event) {
  uint16_t i;
  
  pthread_mutex_lock(&this->event_lock);
  while (this->event_pending[event->type])
    /* there is already one event of that type around */
    pthread_cond_wait(&this->event_handled, &this->event_lock);
  this->event_pending[event->type] = 1;
  pthread_mutex_unlock(&this->event_lock);
  
  /* Iterate through all event handlers */
  for(i=0; i < this->num_event_listeners; i++) {
    (this->event_listeners[i]) ((void *)this->event_listener_user_data[i], event);
  }
  
  this->event_pending[event->type] = 0;
  pthread_cond_signal(&this->event_handled);
}

int xine_remove_event_listener(xine_p this_ro, 
			       xine_event_listener_cb_t listener) {
  xine_t *this = (xine_t *)this_ro;
  uint16_t i, found, pending;

  found = 1;

  pthread_mutex_lock(&this->event_lock);
  /* wait for any pending events */
  do {
    pending = 0;
    for (i = 0; i < XINE_MAX_EVENT_TYPES; i++)
      pending += this->event_pending[i];
    if (pending)
      pthread_cond_wait(&this->event_handled, &this->event_lock);
  } while (pending);
  
  /* Attempt to find the listener */
  while((found == 1) && (i < this->num_event_listeners)) {
    if(this->event_listeners[i] == listener) {
      /* Set found flag */
      found = 0;

      this->event_listeners[i] = NULL;

      /* If possible, move the last listener to the hole thats left */
      if(this->num_event_listeners > 1) {
	this->event_listeners[i] = this->event_listeners[this->num_event_listeners - 1];
	this->event_listener_user_data[i] = this->event_listener_user_data[this->num_event_listeners - 1];
	this->event_listeners[this->num_event_listeners - 1] = NULL;
      }
      
      this->num_event_listeners --;
    }

    i++;
  }
  pthread_mutex_unlock(&this->event_lock);

  return found;
}
