/* 
 * Copyright (C) 2002 the xine project
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
 * $Id: strict_scr.c,v 1.2 2002/02/09 07:13:23 guenter Exp $
 *
 * scr plugin that may not allow others to adjust it (used for streaming)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "xine_internal.h"
#include "strict_scr.h"

static int strictscr_get_priority (scr_plugin_t *scr) {
  return 100; /* very high priority */
}

/* Only call this when already mutex locked */
static void strictscr_set_pivot (strictscr_t *this) {

  struct   timeval tv;
  uint32_t pts;
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

static int strictscr_set_speed (scr_plugin_t *scr, int speed) {
  strictscr_t *this = (strictscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  strictscr_set_pivot( this );
  this->speed_factor = (double) speed * 90000.0 / 4.0;

  pthread_mutex_unlock (&this->lock);

  return speed;
}

static void strictscr_adjust (scr_plugin_t *scr, int64_t vpts) {

  strictscr_t *this = (strictscr_t*) scr;
  struct   timeval tv;

  if (this->adjustable) {

    pthread_mutex_lock (&this->lock);
    
    gettimeofday(&tv, NULL);
    this->cur_time.tv_sec=tv.tv_sec;
    this->cur_time.tv_usec=tv.tv_usec;
    this->cur_pts = vpts;
    
    pthread_mutex_unlock (&this->lock);
  }
}

static void strictscr_start (scr_plugin_t *scr, int64_t start_vpts) {
  strictscr_t *this = (strictscr_t*) scr;

  pthread_mutex_lock (&this->lock);

  gettimeofday(&this->cur_time, NULL);
  this->cur_pts = start_vpts;

  pthread_mutex_unlock (&this->lock);
}

static int64_t strictscr_get_current (scr_plugin_t *scr) {
  strictscr_t *this = (strictscr_t*) scr;

  struct   timeval tv;
  int64_t  pts;
  double   pts_calc; 
  pthread_mutex_lock (&this->lock);

  gettimeofday(&tv, NULL);
  
  pts_calc = (tv.tv_sec  - this->cur_time.tv_sec) * this->speed_factor;
  pts_calc += (tv.tv_usec - this->cur_time.tv_usec) * this->speed_factor / 1e6;

  pts = this->cur_pts + pts_calc;
  
  pthread_mutex_unlock (&this->lock);

  return pts;
}

strictscr_t* strictscr_init () {
  strictscr_t *this;

  this = malloc(sizeof(*this));
  memset(this, 0, sizeof(*this));
  
  this->scr.interface_version = 2;
  this->scr.get_priority      = strictscr_get_priority;
  this->scr.set_speed         = strictscr_set_speed;
  this->scr.adjust            = strictscr_adjust;
  this->scr.start             = strictscr_start;
  this->scr.get_current       = strictscr_get_current;
  strictscr_set_speed (&this->scr, SPEED_NORMAL);

  this->adjustable            = 0;

  pthread_mutex_init (&this->lock, NULL);

  return this;
}

