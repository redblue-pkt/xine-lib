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
 * $Id: video_decoder.c,v 1.111 2002/11/29 17:25:26 mroi Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xine_internal.h"
#include "xineutils.h"
#include <sched.h>

/*
#define LOG
*/

static void update_spu_decoder (xine_stream_t *stream, int type) {

  int streamtype = (type>>16) & 0xFF;
  
  if( stream->spu_decoder_streamtype != streamtype ||
      !stream->spu_decoder_plugin ) {
    
    if (stream->spu_decoder_plugin)
      free_spu_decoder (stream, stream->spu_decoder_plugin);
          
    stream->spu_decoder_streamtype = streamtype;
    stream->spu_decoder_plugin = get_spu_decoder (stream, streamtype);

  }
  return ;
}

void *video_decoder_loop (void *stream_gen) {

  buf_element_t   *buf;
  xine_stream_t   *stream = (xine_stream_t *) stream_gen;
  int              running = 1;
  int              streamtype;
  static int	   prof_video_decode = -1;
  static int	   prof_spu_decode = -1;
  static uint32_t  buftype_unknown = 0;
  
  if (prof_video_decode == -1)
    prof_video_decode = xine_profiler_allocate_slot ("video decoder");
  if (prof_spu_decode == -1)
    prof_spu_decode = xine_profiler_allocate_slot ("spu decoder");

  while (running) {

#ifdef LOG
    printf ("video_decoder: getting buffer...\n");  
#endif

    buf = stream->video_fifo->get (stream->video_fifo);

    if (buf->input_pos)
      stream->input_pos = buf->input_pos;
    if (buf->input_length)
      stream->input_length = buf->input_length;
    if (buf->input_time) {
      stream->input_time = buf->input_time;
    }
    
#ifdef LOG
    printf ("video_decoder: got buffer 0x%08x\n", buf->type);      
#endif

    switch (buf->type & 0xffff0000) {
    case BUF_CONTROL_HEADERS_DONE:
      pthread_mutex_lock (&stream->counter_lock);
      stream->header_count_video++;
      pthread_cond_broadcast (&stream->counter_changed);
      pthread_mutex_unlock (&stream->counter_lock);
      break;

    case BUF_CONTROL_START:
      
      if (stream->video_decoder_plugin) {
	free_video_decoder (stream, stream->video_decoder_plugin);
	stream->video_decoder_plugin = NULL;
      }
      
      if (stream->spu_decoder_plugin) {
        free_spu_decoder (stream, stream->spu_decoder_plugin);
        stream->spu_decoder_plugin = NULL;
      }
      
      stream->metronom->handle_video_discontinuity (stream->metronom, 
						    DISC_STREAMSTART, 0);
      break;

    case BUF_CONTROL_SPU_CHANNEL:
      {
	xine_event_t  ui_event;
	
	/* We use widescreen spu as the auto selection, because widescreen
	 * display is common. SPU decoders can choose differently if it suits
	 * them. */
	stream->spu_channel_auto = buf->decoder_info[0];
	stream->spu_channel_letterbox = buf->decoder_info[1];
	stream->spu_channel_pan_scan = buf->decoder_info[2];
	if (stream->spu_channel_user == -1)
	  stream->spu_channel = stream->spu_channel_auto;
	
	/* Inform UI of SPU channel changes */
	ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
	ui_event.data_length = 0;

        xine_event_send (stream, &ui_event);
      }
      break;

    case BUF_CONTROL_END:

      /* wait for audio to reach this marker, if necessary */

      pthread_mutex_lock (&stream->counter_lock);

      stream->finished_count_video++;

#ifdef LOG
      printf ("video_decoder: reached end marker # %d\n", 
	      stream->finished_count_video);
#endif

      pthread_cond_broadcast (&stream->counter_changed);

      if (stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO]
	  && stream->audio_fifo) {

	while (stream->finished_count_video > stream->finished_count_audio) {
	  pthread_cond_wait (&stream->counter_changed, &stream->counter_lock);
	}
      }
          
      pthread_mutex_unlock (&stream->counter_lock);

      if (stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO]) {
	/* set engine status, send frontend notification event */
	xine_handle_stream_end (stream, 
				buf->decoder_flags & BUF_FLAG_END_STREAM);
      }

      /* Wake up xine_play if it's waiting for a frame */
      pthread_mutex_lock (&stream->first_frame_lock);
      if (stream->first_frame_flag) {
        stream->first_frame_flag = 0;
        pthread_cond_broadcast(&stream->first_frame_reached);
      }
      pthread_mutex_unlock (&stream->first_frame_lock);

      break;

    case BUF_CONTROL_QUIT:
      if (stream->video_decoder_plugin) {
	free_video_decoder (stream, stream->video_decoder_plugin);
	stream->video_decoder_plugin = NULL;
      }
      if (stream->spu_decoder_plugin) {
        free_spu_decoder (stream, stream->spu_decoder_plugin);
        stream->spu_decoder_plugin = NULL;
      }

      running = 0;
      break;

    case BUF_CONTROL_RESET_DECODER:
      if (stream->video_decoder_plugin) {
        stream->video_decoder_plugin->reset (stream->video_decoder_plugin);
      }
      if (stream->spu_decoder_plugin) {
        stream->spu_decoder_plugin->reset (stream->spu_decoder_plugin);
      }
      break;
    
    case BUF_CONTROL_DISCONTINUITY:
#ifdef LOG
      printf ("video_decoder: discontinuity ahead\n");
#endif
      if (stream->video_decoder_plugin) {
        stream->video_decoder_plugin->discontinuity (stream->video_decoder_plugin);
      }
      
      stream->video_in_discontinuity = 1;

      stream->metronom->handle_video_discontinuity (stream->metronom, DISC_RELATIVE, buf->disc_off);
      
      stream->video_in_discontinuity = 0;
      
      break;
    
    case BUF_CONTROL_NEWPTS:
#ifdef LOG
      printf ("video_decoder: new pts %lld\n", buf->disc_off);
#endif
      if (stream->video_decoder_plugin) {
        stream->video_decoder_plugin->discontinuity (stream->video_decoder_plugin);
      }
      
      stream->video_in_discontinuity = 1;
      
      if (buf->decoder_flags & BUF_FLAG_SEEK) {
	stream->metronom->handle_video_discontinuity (stream->metronom, DISC_STREAMSEEK, buf->disc_off);
      } else {
	stream->metronom->handle_video_discontinuity (stream->metronom, DISC_ABSOLUTE, buf->disc_off);
      }
      stream->video_in_discontinuity = 0;
     
      break;
      
    case BUF_CONTROL_AUDIO_CHANNEL:
      {
	xine_event_t  ui_event;
	/* Inform UI of AUDIO channel changes */
	ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
	ui_event.data_length = 0;
	xine_event_send (stream, &ui_event);
      }
      break;

    case BUF_CONTROL_NOP:
      break;
      
    default:

      if ( (buf->type & 0xFF000000) == BUF_VIDEO_BASE ) {

        if (stream->stream_info[XINE_STREAM_INFO_IGNORE_VIDEO])
          break;

        xine_profiler_start_count (prof_video_decode);
      
	/*
	  printf ("video_decoder: got package %d, decoder_info[0]:%d\n", 
	  buf, buf->decoder_info[0]);
	*/      
	
	streamtype = (buf->type>>16) & 0xFF;
 
        if( stream->video_decoder_streamtype != streamtype ||
            !stream->video_decoder_plugin ) {
          
          if (stream->video_decoder_plugin) {
            free_video_decoder (stream, stream->video_decoder_plugin);
          }
          
          stream->video_decoder_streamtype = streamtype;
          stream->video_decoder_plugin = get_video_decoder (stream, streamtype);
        }
	
	if (stream->video_decoder_plugin) {

	  stream->video_decoder_plugin->decode_data (stream->video_decoder_plugin, buf);  

	} else if (buf->type != buftype_unknown) {
	  xine_log (stream->xine, XINE_LOG_MSG, 
		    "video_decoder: no plugin available to handle '%s'\n",
		    buf_video_name( buf->type ) );
	  buftype_unknown = buf->type;
        }

        xine_profiler_stop_count (prof_video_decode);

      } else if ( (buf->type & 0xFF000000) == BUF_SPU_BASE ) {

        if (stream->stream_info[XINE_STREAM_INFO_IGNORE_SPU])
          break;

        xine_profiler_start_count (prof_spu_decode);

        update_spu_decoder(stream, buf->type);

        if (stream->spu_decoder_plugin) {
          stream->spu_decoder_plugin->decode_data (stream->spu_decoder_plugin, buf);
        }

        xine_profiler_stop_count (prof_spu_decode);
        break;

      } else if (buf->type != buftype_unknown) {
	xine_log (stream->xine, XINE_LOG_MSG, 
		  "video_decoder: unknown buffer type: %08x\n",
		  buf->type );
	buftype_unknown = buf->type;
      }

      break;

    }

    buf->free_buffer (buf);
  }

  pthread_exit(NULL);
}

void video_decoder_init (xine_stream_t *stream) {
  
  pthread_attr_t       pth_attrs;
  struct sched_param   pth_params;
  int		       err;

  /* The fifo size is based on dvd playback where buffers are filled
   * with 2k of data. With 500 buffers and a typical video data rate
   * of 4 Mbit/s, the fifo can hold about 2 seconds of video, wich
   * should be enough to compensate for drive delays.
   * We provide buffers of 8k size instead of 2k for demuxers sending
   * larger chunks.
   */
  stream->video_fifo = fifo_buffer_new (500, 8192);

  pthread_attr_init(&pth_attrs);
  pthread_attr_getschedparam(&pth_attrs, &pth_params);
  pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
  pthread_attr_setschedparam(&pth_attrs, &pth_params);
  pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
  
  if ((err = pthread_create (&stream->video_thread,
			     &pth_attrs, video_decoder_loop, stream)) != 0) {
    fprintf (stderr, "video_decoder: can't create new thread (%s)\n",
	     strerror(err));
    abort();
  }

  stream->video_in_discontinuity = 0;
}

void video_decoder_shutdown (xine_stream_t *stream) {

  buf_element_t *buf;
  void          *p;

  printf ("video_decoder: shutdown...\n");

  /* stream->video_fifo->clear(stream->video_fifo); */

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  printf ("video_decoder: shutdown...2\n");
  buf->type = BUF_CONTROL_QUIT;
  stream->video_fifo->put (stream->video_fifo, buf);
  printf ("video_decoder: shutdown...3\n");

  pthread_join (stream->video_thread, &p);
  printf ("video_decoder: shutdown...4\n");
}

