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
 * network buffering control
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>

#include "net_buf_ctrl.h"

#define DEFAULT_LOW_WATER_MARK  2
#define DEFAULT_HIGH_WATER_MARK 5
/*
#define LOG
*/
struct nbc_s {

  xine_t          *xine;

  int              buffering;
  int              low_water_mark;
  int              high_water_mark;

};

void nbc_check_buffers (nbc_t *this) {

  int fifo_fill;

  fifo_fill = this->xine->video_fifo->size(this->xine->video_fifo);
  if (this->xine->audio_fifo) {
    fifo_fill += 8*this->xine->audio_fifo->size(this->xine->audio_fifo);
  }
#ifdef LOG
  if (this->buffering) {
    xine_log (this->xine, XINE_LOG_MSG, 
	      "net_buf_ctl: buffering (%d/%d)...\n", fifo_fill, this->high_water_mark);
  }
#endif
  if (fifo_fill<this->low_water_mark) {
    
    if (!this->buffering) {

      this->xine->osd_renderer->filled_rect (this->xine->osd, 0, 0, 299, 99, 0);
      this->xine->osd_renderer->render_text (this->xine->osd, 5, 30, "buffering...", OSD_TEXT1);
      this->xine->osd_renderer->show (this->xine->osd, 0);

      /* give video_out time to display osd before pause */
      sleep (1);

      if (this->high_water_mark<150) {

	/* increase marks to adapt to stream/network needs */

	this->high_water_mark += 10;
	/* this->low_water_mark = this->high_water_mark/4; */
      }
    }

    this->xine->metronom->set_speed (this->xine->metronom, SPEED_PAUSE);
    this->xine->metronom->set_option (this->xine->metronom, METRONOM_SCR_ADJUSTABLE, 0);
    this->xine->audio_out->audio_paused = 2;
    this->buffering = 1;

  } else if ( (fifo_fill>this->high_water_mark) && (this->buffering)) {
    this->xine->metronom->set_speed (this->xine->metronom, SPEED_NORMAL);
    this->xine->metronom->set_option (this->xine->metronom, METRONOM_SCR_ADJUSTABLE, 1);
    this->xine->audio_out->audio_paused = 0;
    this->buffering = 0;

    this->xine->osd_renderer->hide (this->xine->osd, 0);
  }

}

void nbc_close (nbc_t *this) {
  this->xine->metronom->set_option (this->xine->metronom, METRONOM_SCR_ADJUSTABLE, 1);
  free (this);
}

nbc_t *nbc_init (xine_t *xine) {

  nbc_t *this = (nbc_t *) malloc (sizeof (nbc_t));

  this->xine            = xine;
  this->buffering       = 0;
  this->low_water_mark  = DEFAULT_LOW_WATER_MARK;
  this->high_water_mark = DEFAULT_HIGH_WATER_MARK;

  return this;
}

