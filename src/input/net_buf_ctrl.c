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

#define LOG

struct nbc_s {

  xine_stream_t   *stream;

  int              buffering;
  int              low_water_mark;
  int              high_water_mark;

};

void nbc_check_buffers (nbc_t *this) {

  int fifo_fill;

  fifo_fill = this->stream->video_fifo->size(this->stream->video_fifo);
  if (this->stream->audio_fifo) {
    fifo_fill += 8*this->stream->audio_fifo->size(this->stream->audio_fifo);
  }
#ifdef LOG
  if (this->buffering) {
    printf ("net_buf_ctl: buffering (%d/%d)...\n", 
	    fifo_fill, this->high_water_mark);
  }
#endif
  if (fifo_fill<this->low_water_mark) {
    
    if (!this->buffering) {

      /* FIXME: send progress events about buffering
      xine_event_t             event;
      xine_idx_progress_data_t idx;
       */

      if (this->high_water_mark<150) {

	/* increase marks to adapt to stream/network needs */

	this->high_water_mark += 10;
	/* this->low_water_mark = this->high_water_mark/4; */
      }
    }

    this->stream->metronom->set_speed (this->stream->metronom, XINE_SPEED_PAUSE);
    this->stream->metronom->set_option (this->stream->metronom, METRONOM_SCR_ADJUSTABLE, 0);
    if (this->stream->audio_out)
      this->stream->audio_out->audio_paused = 2;
    this->buffering = 1;

  } else if ( (fifo_fill>this->high_water_mark) && (this->buffering)) {
    this->stream->metronom->set_speed (this->stream->metronom, XINE_SPEED_NORMAL);
    this->stream->metronom->set_option (this->stream->metronom, METRONOM_SCR_ADJUSTABLE, 1);
    if (this->stream->audio_out)
      this->stream->audio_out->audio_paused = 0;
    this->buffering = 0;

  }
}

void nbc_close (nbc_t *this) {
  this->stream->metronom->set_option (this->stream->metronom, METRONOM_SCR_ADJUSTABLE, 1);
  free (this);
}

nbc_t *nbc_init (xine_stream_t *stream) {

  nbc_t *this = (nbc_t *) malloc (sizeof (nbc_t));

  this->stream          = stream;
  this->buffering       = 0;
  this->low_water_mark  = DEFAULT_LOW_WATER_MARK;
  this->high_water_mark = DEFAULT_HIGH_WATER_MARK;

  return this;
}

