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
 * $Id: demux.c,v 1.44 2003/12/23 21:22:40 miguelfreitas Exp $ 
 */


#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define XINE_ENGINE_INTERNAL

#define LOG_MODULE "demux"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine_internal.h"
#include "demuxers/demux.h"
#include "buffer.h"

#ifdef WIN32
#include <winsock.h>
#endif

#ifdef MIN
#undef MIN
#endif
#define MIN(a,b) ( (a) < (b) ) ? (a) : (b)

/* 
 *  Flush audio and video buffers. It is called from demuxers on
 *  seek/stop, and may be useful when user input changes a stream and
 *  xine-lib has cached buffers that have yet to be played.
 *
 * warning: after clearing decoders fifos an absolute discontinuity
 *          indication must be sent. relative discontinuities are likely
 *          to cause "jumps" on metronom.
 */
void _x_demux_flush_engine (xine_stream_t *stream) {

  buf_element_t *buf;

  if (stream->video_out) {
    stream->video_out->set_property(stream->video_out, VO_PROP_DISCARD_FRAMES, 1);
  }
  if (stream->audio_out) {
    stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 1);
  }
  
  stream->video_fifo->clear(stream->video_fifo);
  stream->audio_fifo->clear(stream->audio_fifo);
  
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_RESET_DECODER;
  stream->video_fifo->put (stream->video_fifo, buf);
  
  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_RESET_DECODER;
  stream->audio_fifo->put (stream->audio_fifo, buf);
  
  /* on seeking we must wait decoder fifos to process before doing flush. 
   * otherwise we flush too early (before the old data has left decoders)
   */
  _x_demux_control_headers_done (stream);

  if (stream->video_out) {
    stream->video_out->flush(stream->video_out);
    stream->video_out->set_property(stream->video_out, VO_PROP_DISCARD_FRAMES, 0);
  }

  if (stream->audio_out) {
    stream->audio_out->flush(stream->audio_out);
    stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 0);
  }
}


void _x_demux_control_newpts( xine_stream_t *stream, int64_t pts, uint32_t flags ) {

  buf_element_t *buf;
      
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_NEWPTS;
  buf->decoder_flags = flags;
  buf->disc_off = pts;
  stream->video_fifo->put (stream->video_fifo, buf);

  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_NEWPTS;
  buf->decoder_flags = flags;
  buf->disc_off = pts;
  stream->audio_fifo->put (stream->audio_fifo, buf);
}

/* sync with decoder fifos, making sure everything gets processed */
void _x_demux_control_headers_done (xine_stream_t *stream) {

  int header_count_audio;
  int header_count_video;
  buf_element_t *buf;

  pthread_mutex_lock (&stream->counter_lock);

  if (stream->video_thread) {
    header_count_video = stream->header_count_video + 1;
  } else {
    header_count_video = 0;
  }

  if (stream->audio_thread) {
    header_count_audio = stream->header_count_audio + 1;
  } else {
    header_count_audio = 0;
  }
  
  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_HEADERS_DONE;
  stream->video_fifo->put (stream->video_fifo, buf);

  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_HEADERS_DONE;
  stream->audio_fifo->put (stream->audio_fifo, buf);

  while ((stream->header_count_audio<header_count_audio) || 
	 (stream->header_count_video<header_count_video)) {
    struct timeval tv;
    struct timespec ts;

    lprintf ("waiting for headers. v:%d %d   a:%d %d\n",
	     stream->header_count_video, header_count_video,
	     stream->header_count_audio, header_count_audio); 

    gettimeofday(&tv, NULL);
    ts.tv_sec  = tv.tv_sec + 1;
    ts.tv_nsec = tv.tv_usec * 1000;
    /* use timedwait to workaround buggy pthread broadcast implementations */
    pthread_cond_timedwait (&stream->counter_changed, &stream->counter_lock, &ts);
  }

  lprintf ("headers processed.\n");

  pthread_mutex_unlock (&stream->counter_lock);
}

void _x_demux_control_start( xine_stream_t *stream ) {

  buf_element_t *buf;

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_START;
  stream->video_fifo->put (stream->video_fifo, buf);

  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_START;
  stream->audio_fifo->put (stream->audio_fifo, buf);
}

void _x_demux_control_end( xine_stream_t *stream, uint32_t flags ) {

  buf_element_t *buf;

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_END;
  buf->decoder_flags = flags;
  stream->video_fifo->put (stream->video_fifo, buf);

  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_END;
  buf->decoder_flags = flags;
  stream->audio_fifo->put (stream->audio_fifo, buf);
}

void _x_demux_control_nop( xine_stream_t *stream, uint32_t flags ) {

  buf_element_t *buf;

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_NOP;
  buf->decoder_flags = flags;
  stream->video_fifo->put (stream->video_fifo, buf);
  
  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_NOP;
  buf->decoder_flags = flags;
  stream->audio_fifo->put (stream->audio_fifo, buf);
}

static void *demux_loop (void *stream_gen) {

  xine_stream_t *stream = (xine_stream_t *)stream_gen;
  int status;

  lprintf ("loop starting...\n");

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

        lprintf ("sched_yield\n");

        sched_yield();
        pthread_mutex_lock( &stream->demux_lock );
      }
    }

    lprintf ("main demuxer loop finished (status: %d)\n", status);

    /* tell to the net_buf_ctrl that we are at the end of the stream
     * then the net_buf_ctrl will not pause
     */
    _x_demux_control_nop(stream, BUF_FLAG_END_STREAM);

    /* wait before sending end buffers: user might want to do a new seek */
    while(stream->demux_thread_running &&
          ((stream->video_fifo->size(stream->video_fifo)) ||
           (stream->audio_fifo->size(stream->audio_fifo))) &&
          status == DEMUX_FINISHED ){
      pthread_mutex_unlock( &stream->demux_lock );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &stream->demux_lock );
      status = stream->demux_plugin->get_status(stream->demux_plugin);
    }

  } while( status == DEMUX_OK && stream->demux_thread_running );

  lprintf ("loop finished (status: %d)\n", status);

  /* demux_thread_running is zero if demux loop has being stopped by user */
  if (stream->demux_thread_running) {
    _x_demux_control_end(stream, BUF_FLAG_END_STREAM);
  } else {
    _x_demux_control_end(stream, BUF_FLAG_END_USER);
  }

  lprintf ("loop finished, end buffer sent\n");

  stream->demux_thread_running = 0;

  pthread_mutex_unlock( &stream->demux_lock );

  return NULL;
}

int _x_demux_start_thread (xine_stream_t *stream) {

  int err;
  
  lprintf ("start thread called\n");
  
  stream->demux_action_pending = 1;
  pthread_mutex_lock( &stream->demux_lock );
  stream->demux_action_pending = 0;
  
  if( !stream->demux_thread_running ) {
    
    stream->demux_thread_running = 1;
    if ((err = pthread_create (&stream->demux_thread,
			       NULL, demux_loop, (void *)stream)) != 0) {
      printf ("demux: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }
  
  pthread_mutex_unlock( &stream->demux_lock );
  return 0;
}

int _x_demux_stop_thread (xine_stream_t *stream) {
  
  void *p;
  
  lprintf ("stop thread called\n");
  
  stream->demux_action_pending = 1;
  pthread_mutex_lock( &stream->demux_lock );
  stream->demux_thread_running = 0;
  stream->demux_action_pending = 0;
  pthread_mutex_unlock( &stream->demux_lock );

  lprintf ("joining thread %ld\n", stream->demux_thread );
  
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

int _x_demux_read_header( input_plugin_t *input, unsigned char *buffer, off_t size){
  int read_size;
  unsigned char *buf;

  if (!input || !size || size > MAX_PREVIEW_SIZE)
    return 0;

  if (input->get_capabilities(input) & INPUT_CAP_SEEKABLE) {
    input->seek(input, 0, SEEK_SET);
    read_size = input->read(input, buffer, size);
    input->seek(input, 0, SEEK_SET);
  } else if (input->get_capabilities(input) & INPUT_CAP_PREVIEW) {
    buf = xine_xmalloc(MAX_PREVIEW_SIZE);
    read_size = input->get_optional_data(input, buf, INPUT_OPTIONAL_DATA_PREVIEW);
    read_size = MIN (read_size, size);
    memcpy(buffer, buf, read_size);
    free(buf);
  } else {
    return 0;
  }
  return read_size;
}

int _x_demux_check_extension (char *mrl, char *extensions){
  char *last_dot, *e, *ext_copy, *ext_work;

  ext_copy = strdup(extensions);
  ext_work = ext_copy;

  last_dot = strrchr (mrl, '.');
  if (last_dot) {
    last_dot++;
    while ( ( e = xine_strsep(&ext_work, " ")) != NULL ) {
      if (strcasecmp (last_dot, e) == 0) {
        free(ext_copy);
        return 1;
      }
    }
  }
  free(ext_copy);
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
off_t _x_read_abort (xine_stream_t *stream, int fd, char *buf, off_t todo) {

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

#ifndef WIN32
    ret = read (fd, &buf[total], todo - total);

    /* check EOF */
    if (!ret)
      break;

    /* check errors */
    if(ret < 0) {
      if(errno == EAGAIN)
        continue;

      perror("_x_read_abort");
      return ret;
    }
#else
    ret = recv (fd, &buf[total], todo - total, 0);
    if (ret <= 0)
	{
      perror("_x_read_abort");
	  return ret;
	}
#endif

    total += ret;
  }

  return total;
}

int _x_action_pending (xine_stream_t *stream) {
  return stream->demux_action_pending;
}

/*
 * demuxer helper function to send data to fifo, breaking into smaller
 * pieces (bufs) as needed.
 *
 * it has quite some parameters, but only the first 6 are needed.
 *
 * the other ones help enforcing that demuxers provide the information
 * they have about the stream, so things like the GUI slider bar can
 * work as expected. 
 */
void _x_demux_send_data(fifo_buffer_t *fifo, uint8_t *data, int size,
                        int64_t pts, uint32_t type, uint32_t decoder_flags,
                        off_t input_pos, off_t input_length,
                        int input_time, int total_time,
                        uint32_t frame_number) {
  buf_element_t *buf;

  decoder_flags |= BUF_FLAG_FRAME_START;

  while (fifo && size) {

    buf = fifo->buffer_pool_alloc (fifo);

    if ( size > buf->max_size ) {
      buf->size          = buf->max_size;
      buf->decoder_flags = decoder_flags;
    } else {
      buf->size          = size;
      buf->decoder_flags = BUF_FLAG_FRAME_END | decoder_flags;
    }
    decoder_flags &= ~BUF_FLAG_FRAME_START;

    xine_fast_memcpy (buf->content, data, buf->size);
    data += buf->size;
    size -= buf->size;

    buf->pts = pts;
    pts = 0;

    buf->extra_info->input_pos     = input_pos;
    buf->extra_info->input_length  = input_length;
    buf->extra_info->input_time    = input_time;
    buf->extra_info->total_time    = total_time;
    buf->extra_info->frame_number  = frame_number;

    buf->type                      = type;

    fifo->put (fifo, buf);
  }
}
