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
 * Demuxer helper functions
 * hide some xine engine details from demuxers and reduce code duplication
 *
 * $id$ 
 */

#include "xine_internal.h"
#include "demuxers/demux.h"
#include "buffer.h"

/* internal use only - called from demuxers on seek/stop
 * warning: after clearing decoders fifos an absolute discontinuity
 *          indication must be sent. relative discontinuities are likely
 *          to cause "jumps" on metronom.
 */
void xine_demux_flush_engine (xine_t *this) {

  buf_element_t *buf;

  this->video_fifo->clear(this->video_fifo);

  if( this->audio_fifo )
    this->audio_fifo->clear(this->audio_fifo);
  
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type            = BUF_CONTROL_RESET_DECODER;
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type            = BUF_CONTROL_RESET_DECODER;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
  
  this->metronom->adjust_clock(this->metronom,
                               this->metronom->get_current_time(this->metronom) + 30 * 90000 );
}


void xine_demux_control_newpts( xine_t *this, int64_t pts, uint32_t flags ) {

  buf_element_t *buf;
      
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type = BUF_CONTROL_NEWPTS;
  buf->decoder_flags = flags;
  buf->disc_off = pts;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_CONTROL_NEWPTS;
    buf->decoder_flags = flags;
    buf->disc_off = pts;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

void xine_demux_control_headers_done (xine_t *this) {

  buf_element_t *buf;
      
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type = BUF_CONTROL_HEADERS_DONE;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_CONTROL_HEADERS_DONE;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

void xine_demux_control_start( xine_t *this ) {

  buf_element_t *buf;
      
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type = BUF_CONTROL_START;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_CONTROL_START;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

void xine_demux_control_end( xine_t *this, uint32_t flags ) {

  buf_element_t *buf;
      
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type = BUF_CONTROL_END;
  buf->decoder_flags = flags;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_CONTROL_END;
    buf->decoder_flags = flags;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}
