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
void xine_demux_flush_engine (xine_stream_t *stream) {

  buf_element_t *buf;

  stream->video_fifo->clear(stream->video_fifo);

  if( stream->audio_fifo )
    stream->audio_fifo->clear(stream->audio_fifo);
  
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type            = BUF_CONTROL_RESET_DECODER;
  stream->video_fifo->put (stream->video_fifo, buf);

  if(stream->audio_fifo) {
    buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
    buf->type            = BUF_CONTROL_RESET_DECODER;
    stream->audio_fifo->put (stream->audio_fifo, buf);
  }
  
  stream->metronom->adjust_clock(stream->metronom,
                               stream->metronom->get_current_time(stream->metronom) + 30 * 90000 );
}


void xine_demux_control_newpts( xine_stream_t *stream, int64_t pts, uint32_t flags ) {

  buf_element_t *buf;
      
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_NEWPTS;
  buf->decoder_flags = flags;
  buf->disc_off = pts;
  stream->video_fifo->put (stream->video_fifo, buf);

  if (stream->audio_fifo) {
    buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
    buf->type = BUF_CONTROL_NEWPTS;
    buf->decoder_flags = flags;
    buf->disc_off = pts;
    stream->audio_fifo->put (stream->audio_fifo, buf);
  }
}

void xine_demux_control_headers_done (xine_stream_t *stream) {

  buf_element_t *buf;
      
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_HEADERS_DONE;
  stream->video_fifo->put (stream->video_fifo, buf);

  if (stream->audio_fifo) {
    buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
    buf->type = BUF_CONTROL_HEADERS_DONE;
    stream->audio_fifo->put (stream->audio_fifo, buf);
  }
}

void xine_demux_control_start( xine_stream_t *stream ) {

  buf_element_t *buf;
      
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_START;
  stream->video_fifo->put (stream->video_fifo, buf);

  if (stream->audio_fifo) {
    buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
    buf->type = BUF_CONTROL_START;
    stream->audio_fifo->put (stream->audio_fifo, buf);
  }
}

void xine_demux_control_end( xine_stream_t *stream, uint32_t flags ) {

  buf_element_t *buf;
      
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_END;
  buf->decoder_flags = flags;
  stream->video_fifo->put (stream->video_fifo, buf);

  if (stream->audio_fifo) {
    buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
    buf->type = BUF_CONTROL_END;
    buf->decoder_flags = flags;
    stream->audio_fifo->put (stream->audio_fifo, buf);
  }
}
