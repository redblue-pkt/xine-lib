/*
 * Copyright (C) 2000-2003 the xine project
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
 * Demuxer helper functions
 * hide some xine engine details from demuxers and reduce code duplication
 *
 * $id$ 
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "xine_internal.h"
#include "demuxers/demux.h"
#include "buffer.h"

/*
#define LOG
*/

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
      
  if (stream->audio_out) {
    stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 1);
  }
  
  /* on seeking we must wait decoder fifos to process before doing flush. 
   * otherwise we flush too early (before the old data has left decoders)
   */
  xine_demux_control_headers_done (stream);

  if (stream->video_out) {
    stream->video_out->flush(stream->video_out);
  }

  if (stream->audio_out) {
    stream->audio_out->flush(stream->audio_out);
    stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 0);
  }
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

/* sync with decoder fifos, making sure everything gets processed */
void xine_demux_control_headers_done (xine_stream_t *stream) {

  int header_count_audio;
  int header_count_video;
  buf_element_t *buf;

  pthread_mutex_lock (&stream->counter_lock);
  if (stream->audio_fifo)
    header_count_audio = stream->header_count_audio + 1;
  else
    header_count_audio = 0;

  header_count_video = stream->header_count_video + 1;
  
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_HEADERS_DONE;
  stream->video_fifo->put (stream->video_fifo, buf);

  if (stream->audio_fifo) {
    buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
    buf->type = BUF_CONTROL_HEADERS_DONE;
    stream->audio_fifo->put (stream->audio_fifo, buf);
  }

  while ((stream->header_count_audio<header_count_audio) || 
	 (stream->header_count_video<header_count_video)) {
    struct timeval tv;
    struct timespec ts;
#ifdef LOG
    printf ("xine: waiting for headers.\n");
#endif
    gettimeofday(&tv, NULL);
    ts.tv_sec  = tv.tv_sec + 1;
    ts.tv_nsec = tv.tv_usec * 1000;
    /* use timedwait to workaround buggy pthread broadcast implementations */
    pthread_cond_timedwait (&stream->counter_changed, &stream->counter_lock, &ts);
  }
#ifdef LOG
  printf ("xine: headers processed.\n");
#endif
  pthread_mutex_unlock (&stream->counter_lock);
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

void xine_demux_control_nop( xine_stream_t *stream, uint32_t flags ) {

  buf_element_t *buf;

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_NOP;
  buf->decoder_flags = flags;
  stream->video_fifo->put (stream->video_fifo, buf);

  if (stream->audio_fifo) {
    buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
    buf->type = BUF_CONTROL_NOP;
    buf->decoder_flags = flags;
    stream->audio_fifo->put (stream->audio_fifo, buf);
  }
}

static void *demux_loop (void *stream_gen) {

  xine_stream_t *stream = (xine_stream_t *)stream_gen;
  int status;

#ifdef LOG
  printf ("demux: loop starting...\n");
#endif

  pthread_mutex_lock( &stream->demux_lock );

  /* do-while needed to seek after demux finished */
  do {

    /* main demuxer loop */
    status = stream->demux_plugin->get_status(stream->demux_plugin);
    while(status == DEMUX_OK && stream->demux_thread_running) {

      status = stream->demux_plugin->send_chunk(stream->demux_plugin);

      /* someone may want to interrupt us */
      if( stream->demux_action_pending ) {
        pthread_mutex_unlock( &stream->demux_lock );
#ifdef LOG
        printf ("demux: sched_yield\n");
#endif
        sched_yield();
        pthread_mutex_lock( &stream->demux_lock );
      }
    }

#ifdef LOG
    printf ("demux: main demuxer loop finished (status: %d)\n", status);
#endif

    /* wait before sending end buffers: user might want to do a new seek */
    xine_demux_control_nop(stream, BUF_FLAG_END_STREAM);
    while(stream->demux_thread_running &&
          ((!stream->video_fifo || stream->video_fifo->size(stream->video_fifo)) ||
           (stream->audio_out
	    && (!stream->audio_fifo || stream->audio_fifo->size(stream->audio_fifo)))) &&
          status == DEMUX_FINISHED ){
      pthread_mutex_unlock( &stream->demux_lock );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &stream->demux_lock );
      status = stream->demux_plugin->get_status(stream->demux_plugin);
    }

  } while( status == DEMUX_OK && stream->demux_thread_running );

#ifdef LOG
  printf ("demux: loop finished (status: %d)\n", status);
#endif

  /* demux_thread_running is zero if demux loop has being stopped by user */
  if (stream->demux_thread_running) {
    xine_demux_control_end(stream, BUF_FLAG_END_STREAM);
  } else {
    xine_demux_control_end(stream, BUF_FLAG_END_USER);
  }

  stream->demux_thread_running = 0;

  pthread_mutex_unlock( &stream->demux_lock );

  return NULL;
}

int xine_demux_start_thread (xine_stream_t *stream) {

  int err;
  
#ifdef LOG
  printf ("demux: start thread called\n");
#endif
  
  stream->demux_action_pending = 1;
  pthread_mutex_lock( &stream->demux_lock );
  stream->demux_action_pending = 0;
  
  if( !stream->demux_thread_running ) {
    
    stream->demux_thread_running = 1;
    if ((err = pthread_create (&stream->demux_thread,
			       NULL, demux_loop, (void *)stream)) != 0) {
      printf ("demux: can't create new thread (%s)\n",
	      strerror(err));
      abort();
    }
  }
  
  pthread_mutex_unlock( &stream->demux_lock );
  return 0;
}

int xine_demux_stop_thread (xine_stream_t *stream) {
  
  void *p;
  
#ifdef LOG
  printf ("demux: stop thread called\n");
#endif
  
  stream->demux_action_pending = 1;
  pthread_mutex_lock( &stream->demux_lock );
  stream->demux_thread_running = 0;
  stream->demux_action_pending = 0;
  pthread_mutex_unlock( &stream->demux_lock );

#ifdef LOG
  printf ("demux: joining thread %ld\n", stream->demux_thread );
#endif
  
  /* FIXME: counter_lock isn't meant to protect demux_thread update.
     however we can't use demux_lock here. should we create a new lock? */
  pthread_mutex_lock (&stream->counter_lock);
  
  /* <join; demux_thread = 0;> must be atomic */
  if( stream->demux_thread )
    pthread_join (stream->demux_thread, &p);
  stream->demux_thread = 0;
  
  pthread_mutex_unlock (&stream->counter_lock);

  /*
   * Wake up xine_play if it's waiting for a frame
   */
  pthread_mutex_lock (&stream->first_frame_lock);
  if (stream->first_frame_flag) {
    stream->first_frame_flag = 0;
    pthread_cond_broadcast(&stream->first_frame_reached);
  }
  pthread_mutex_unlock (&stream->first_frame_lock);

  return 0;
}


/*
 * read from socket/file descriptor checking demux_action_pending
 *
 * network input plugins should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if no data is available and demux_action_pending is set
 */
off_t xine_read_abort (xine_stream_t *stream, int fd, char *buf, off_t todo) {

  off_t ret, total;

  total = 0;

  while (total < todo) {

    fd_set rset;
    struct timeval timeout;

    while(1) {

      FD_ZERO (&rset);
      FD_SET  (fd, &rset);

      timeout.tv_sec  = 0;
      timeout.tv_usec = 50000;

      if( select (fd+1, &rset, NULL, NULL, &timeout) <= 0 ) {
        /* aborts current read if action pending. otherwise xine
         * cannot be stopped when no more data is available.
         */
        if( stream->demux_action_pending )
          return 0;
      } else {
        break;
      }
    }

    ret = read (fd, &buf[total], todo - total);

    /* check EOF */
    if (!ret)
      break;

    /* check errors */
    if(ret < 0) {
      if(errno == EAGAIN)
        continue;

      perror("xine_read_abort");
      return ret;
    }

    total += ret;
  }

  return total;
}

