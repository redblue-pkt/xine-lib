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
 * $Id: demux_ogg.c,v 1.28 2002/06/07 02:40:47 miguelfreitas Exp $
 *
 * demultiplexer for ogg streams
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

#include <ogg/ogg.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

/*
#define LOG
*/

#define CHUNKSIZE 8500

#define MAX_STREAMS 16

#define VALID_ENDS  "ogg"

typedef struct dsogg_video_header_s {
  int32_t width;
  int32_t height;
} dsogg_video_header_t;

typedef struct dsogg_audio_header_s {
  int16_t channels;
  int16_t blockalign;
  int32_t avgbytespersec;
} dsogg_audio_header_t;

typedef struct {

  char streamtype[8];
  char subtype[4];

  int32_t size; /* size of the structure */

  int64_t time_unit; /* in reference time */
  int64_t samples_per_unit;
  int32_t default_len; /* in media time */

  int32_t buffersize;
  int16_t bits_per_sample;

  union {
    /* video specific */
    struct dsogg_video_header_s video;
    /* audio specific */
    struct dsogg_audio_header_s audio;
  } hubba;
} dsogg_header_t;
 
typedef struct demux_ogg_s {
  demux_plugin_t        demux_plugin;

  xine_t               *xine;
  
  config_values_t      *config;

  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;

  input_plugin_t       *input;

  pthread_t             thread;
  int                   thread_running;
  pthread_mutex_t       mutex;

  int                   status;
  
  int                   send_end_buffers;

  int                   frame_duration;

  ogg_sync_state        oy;
  ogg_stream_state      os;
  ogg_page              og;

  ogg_stream_state      oss[MAX_STREAMS];
  uint32_t              buf_types[MAX_STREAMS];
  int                   num_streams;

  int                   pkg_count;

} demux_ogg_t ;


static void hex_dump (uint8_t *p, int length) {

  int i;

  for (i=0; i<length; i++) {
    unsigned char c = p[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\n");

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");

  for (i=0; i<length; i++) {
    unsigned char c = p[i];
    if ( (c>=20) && (c<128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");
  

}

static void demux_ogg_send_package (demux_ogg_t *this) {

  int i;
  int stream_num = -1;
  int cur_serno;
  
  char *buffer;
  long bytes;

  ogg_packet op;
  
  int ret = ogg_sync_pageout(&this->oy,&this->og);
  
#ifdef LOG
  printf ("demux_ogg: send package...\n");
#endif

  if (ret == 0) {
    buffer = ogg_sync_buffer(&this->oy, CHUNKSIZE);
    bytes  = this->input->read(this->input, buffer, CHUNKSIZE);
    
    if (bytes < CHUNKSIZE) {
      this->status = DEMUX_FINISHED;
#ifdef LOG
      printf ("demux_ogg: EOF\n");
#endif
      return;
    }
    
    ogg_sync_wrote(&this->oy, bytes);
  } else if (ret > 0) {
    /* now we've got at least one new page */
    
    cur_serno = ogg_page_serialno (&this->og);
    
    if (ogg_page_bos(&this->og)) {
      printf ("demux_ogg: beginning of stream\ndemux_ogg: serial number %d\n",
	      ogg_page_serialno (&this->og));
    }
    
    for (i = 0; i<this->num_streams; i++) {
      if (this->oss[i].serialno == cur_serno) {
	stream_num = i;
	break;
      }
    }
    
    if (stream_num < 0) {
      ogg_stream_init(&this->oss[this->num_streams], cur_serno);
      stream_num = this->num_streams;
      this->buf_types[stream_num] = 0;
      
      printf ("demux_ogg: found a new stream, serialnumber %d\n", cur_serno);
      
      this->num_streams++;
    }
    
    ogg_stream_pagein(&this->oss[stream_num], &this->og);
    
    while (ogg_stream_packetout(&this->oss[stream_num], &op) == 1) {
      /* printf("demux_ogg: packet: %.8s\n", op.packet); */
      /* printf("demux_ogg:   got a packet\n"); */
      
      if (!this->buf_types[stream_num]) {
	/* detect buftype */
	if (!strncmp (&op.packet[1], "vorbis", 6)) {
	  this->buf_types[stream_num] = BUF_AUDIO_VORBIS;

	  xine_log (this->xine, XINE_LOG_FORMAT,
		    _("ogg: vorbis audio stream detected\n"));

	} else if (!strncmp (&op.packet[1], "video", 5)) {

	  dsogg_header_t   *oggh;
	  buf_element_t    *buf;
	  xine_bmiheader    bih;

#ifdef LOG
	  printf ("demux_ogg: direct show filter created stream detected, hexdump:\n");
	  hex_dump (op.packet, op.bytes);
#endif

	  oggh = (dsogg_header_t *) &op.packet[1];

	  this->buf_types[stream_num] = fourcc_to_buf_video (*(uint32_t *)oggh->subtype);

#ifdef LOG
	  printf ("demux_ogg: subtype          %.4s\n", oggh->subtype);
	  printf ("demux_ogg: time_unit        %lld\n", oggh->time_unit);
	  printf ("demux_ogg: samples_per_unit %lld\n", oggh->samples_per_unit);
	  printf ("demux_ogg: default_len      %d\n", oggh->default_len); 
	  printf ("demux_ogg: buffersize       %d\n", oggh->buffersize); 
	  printf ("demux_ogg: bits_per_sample  %d\n", oggh->bits_per_sample); 
	  printf ("demux_ogg: width            %d\n", oggh->hubba.video.width); 
	  printf ("demux_ogg: height           %d\n", oggh->hubba.video.height); 
	  printf ("demux_ogg: buf_type         %08x\n",this->buf_types[stream_num]);  
#endif

	  bih.biSize=sizeof(xine_bmiheader);
	  bih.biWidth = oggh->hubba.video.width;
	  bih.biHeight= oggh->hubba.video.height;
	  bih.biPlanes= 0;
	  memcpy(&bih.biCompression, oggh->subtype, 4);
	  bih.biBitCount= 0;
	  bih.biSizeImage=oggh->hubba.video.width*oggh->hubba.video.height;
	  bih.biXPelsPerMeter=1;
	  bih.biYPelsPerMeter=1;
	  bih.biClrUsed=0;
	  bih.biClrImportant=0;

	  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
	  buf->content = buf->mem;
	  buf->decoder_flags = BUF_FLAG_HEADER;
	  this->frame_duration = oggh->time_unit * 9 / 1000;
	  buf->decoder_info[1] = this->frame_duration;
	  memcpy (buf->content, &bih, sizeof (xine_bmiheader));
	  buf->size = sizeof (xine_bmiheader);	  
	  buf->type = this->buf_types[stream_num];
	  this->video_fifo->put (this->video_fifo, buf);

	} else {
	  printf ("demux_ogg: unknown streamtype, signature: >%.8s<\n",
		  op.packet);

	  xine_log (this->xine, XINE_LOG_FORMAT,
		    _("ogg: unknown stream type (signature >%.8s<)\n"),
		    op.packet);

	  this->buf_types[stream_num] = BUF_CONTROL_NOP;
	}
      }

      this->pkg_count++;
      
      if ( this->audio_fifo 
	   && (this->buf_types[stream_num] & 0xFF000000) == BUF_AUDIO_BASE) {
	buf_element_t *buf;
	
	buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
	
	buf->content = buf->mem;
	
	{
	  int op_size = sizeof(op);
	  ogg_packet *og_ghost;
	  op_size += (4 - (op_size % 4));
	  
	  /* nasty hack to pack op as well as (vorbis) content
	     in one xine buffer */
	  memcpy (buf->content + op_size, op.packet, op.bytes);
	  memcpy (buf->content, &op, sizeof(op));
	  og_ghost = (ogg_packet *) buf->content;
	  og_ghost->packet = buf->content + op_size;
	  
	}
	
	buf->pts    = 0; /* FIXME */
	buf->size   = op.bytes;
	
	buf->decoder_flags = 0;

	buf->input_pos  = this->input->get_current_pos (this->input);
	buf->input_time = 0;
	
	buf->type = this->buf_types[stream_num];
	
	this->audio_fifo->put (this->audio_fifo, buf);
      } else if ((this->buf_types[stream_num] & 0xFF000000) == BUF_VIDEO_BASE) {
	buf_element_t *buf;
	int todo, done;

#ifdef LOG
	printf ("demux_ogg: video buffer, type=%08x\n", this->buf_types[stream_num]);
#endif

	todo = op.bytes;
	done = 0;
	while (done<todo) {

	  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
	
	  buf->content = buf->mem;

	  if ( (todo-done)>(buf->max_size-1)) {
	    buf->size  = buf->max_size-1;
	    buf->decoder_flags = 0;
	  } else {
	    buf->size = todo-done;
	    buf->decoder_flags = BUF_FLAG_FRAME_END;
	  }
	  
	  /*
	    printf ("demux_ogg: done %d todo %d doing %d\n", done, todo, buf->size);
	  */
	  memcpy (buf->content, op.packet+done, buf->size);

	  if (op.granulepos>0)
	    buf->pts  = op.granulepos * this->frame_duration;  
	  else
	    buf->pts  = 0;
#ifdef LOG
	  printf ("demux_ogg: granulepos %d\n", op.granulepos);
#endif
	  /* buf->pts = 0; */
	  
	  buf->input_pos  = this->input->get_current_pos (this->input);
	  buf->input_time = 0;
	
	  buf->type = this->buf_types[stream_num];
	
	  done += buf->size;

	  this->video_fifo->put (this->video_fifo, buf);

	}
      }
    }
  }
}

static void *demux_ogg_loop (void *this_gen) {
  
  demux_ogg_t *this = (demux_ogg_t *) this_gen;
#ifdef LOG
  printf ("demux_ogg: demux loop starting...\n"); 
#endif

  pthread_mutex_lock( &this->mutex );
  /* do-while needed to seek after demux finished */
  do {

    /* main demuxer loop */
    while(this->status == DEMUX_OK) {

      demux_ogg_send_package (this);

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      pthread_mutex_lock( &this->mutex );
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->audio_fifo->size(this->audio_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while( this->status == DEMUX_OK );
    
#ifdef LOG
  printf ("demux_ogg: demux loop finished (status: %d)\n",
	  this->status);
#endif

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );
  pthread_exit(NULL);

  return NULL;
}

static void demux_ogg_close (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  free (this);
  
}

static void demux_ogg_stop (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );

  printf ("demux_ogg: demux_ogg_stop\n");
  
  if (!this->thread_running) {
    printf ("demux_ogg: stop...ignored\n");
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;
  
  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_demux_flush_engine(this->xine);

  xine_demux_control_end(this->xine, BUF_FLAG_END_USER);
}

static int demux_ogg_get_status (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  return (this->thread_running?DEMUX_OK:DEMUX_FINISHED);
}

static int demux_ogg_start (demux_plugin_t *this_gen,
			     fifo_buffer_t *video_fifo, 
			     fifo_buffer_t *audio_fifo,
			     off_t start_pos, int start_time) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  int err, i;

  pthread_mutex_lock( &this->mutex );
  err = 1;

  if( !this->thread_running ) {
    this->video_fifo  = video_fifo;
    this->audio_fifo  = audio_fifo;

    /* 
     * send start buffer
     */

    xine_demux_control_start(this->xine);

    /*
     * initialize ogg engine
     */
    ogg_sync_init(&this->oy);

    this->num_streams = 0;

    this->input->seek (this->input, 0, SEEK_SET);

    /* send header */
    this->pkg_count = 0;
    for (i=0; i<5; i++) 
      demux_ogg_send_package (this);
  }

  /*
   * seek to start position
   */
  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {

    off_t cur_pos = this->input->get_current_pos (this->input);

/*
      if ( (!start_pos) && (start_time)) {
        start_pos = start_time * this->rate;
      }
*/
/*
    if (start_pos<cur_pos)
      start_pos = cur_pos;
*/

    this->input->seek (this->input, start_pos, SEEK_SET);

    /* send a new pts */
    if( this->thread_running ) {
      xine_demux_control_newpts(this->xine, start_pos / 90, 0);
    }
  }

  if( !this->thread_running ) {
    /*
     * now start demuxing
     */

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;
    if ((err = pthread_create (&this->thread,
			       NULL, demux_ogg_loop, this)) != 0) {
      printf ("demux_ogg: can't create new thread (%s)\n",
	      strerror(err));
      abort();
    }
  }
  else {
    xine_demux_flush_engine(this->xine);
    err = 0;
  }
  
  pthread_mutex_unlock( &this->mutex );

  return err ? DEMUX_FINISHED : DEMUX_OK;
}

static int demux_ogg_seek (demux_plugin_t *this_gen,
			     off_t start_pos, int start_time) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  return demux_ogg_start (this_gen, this->video_fifo, this->audio_fifo,
			  start_pos, start_time);
}

static int demux_ogg_open(demux_plugin_t *this_gen,
			  input_plugin_t *input, int stage) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  switch(stage) {

  case STAGE_BY_CONTENT:
    {
      uint8_t buf[4096];

      if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {

	input->seek(input, 0, SEEK_SET);

	if (input->read(input, buf, 4)) {

	  if ((buf[0] == 'O')
	      && (buf[1] == 'g')
	      && (buf[2] == 'g')
	      && (buf[3] == 'S')) {
	    this->input = input;
	    return DEMUX_CAN_HANDLE;
	  }
	}
      }

      if (input->get_optional_data (input, buf, INPUT_OPTIONAL_DATA_PREVIEW)) {
	if ((buf[0] == 'O')
	    && (buf[1] == 'g')
	    && (buf[2] == 'g')
	    && (buf[3] == 'S')) {
	  this->input = input;
	  return DEMUX_CAN_HANDLE;
	}
      }
    }
    return DEMUX_CANNOT_HANDLE;
    break;

  case STAGE_BY_EXTENSION: {
    char *ending;
    char *MRL;
    char *m, *valid_ends;
    
    MRL = input->get_mrl (input);
    
    /*
     * check ending
     */
    
    ending = strrchr(MRL, '.');
    
    if(!ending)
      return DEMUX_CANNOT_HANDLE;
    
    xine_strdupa(valid_ends, (this->config->register_string(this->config,
							    "mrl.ends_ogg", VALID_ENDS,
							    "valid mrls ending for ogg demuxer",
							    NULL, NULL, NULL)));
    while((m = xine_strsep(&valid_ends, ",")) != NULL) { 
      
      while(*m == ' ' || *m == '\t') m++;
      
      if(!strcasecmp((ending + 1), m)) {
	this->input = input;
	return DEMUX_CAN_HANDLE;
      }
    }
  }
  break;
  }

  return DEMUX_CANNOT_HANDLE;
}

static char *demux_ogg_get_id(void) {
  return "OGG";
}

static char *demux_ogg_get_mimetypes(void) {
  return "audio/x-ogg: ogg: OggVorbis Audio;";
}

static int demux_ogg_get_stream_length (demux_plugin_t *this_gen) {

  /* demux_ogg_t *this = (demux_ogg_t *) this_gen; */

  return 0;
}

demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {

  demux_ogg_t     *this;

  if (iface != 9) {
    printf( _("demux_ogg: plugin doesn't support plugin API version %d.\n"
	      "           this means there's a version mismatch between xine and this "
	      "           demuxer plugin.\nInstalling current demux plugins should help.\n"),
	    iface);
    return NULL;
  }
  
  this         = xine_xmalloc (sizeof (demux_ogg_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config,
					"mrl.ends_ogg", VALID_ENDS,
					"valid mrls ending for ogg demuxer",
					NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_ogg_open;
  this->demux_plugin.start             = demux_ogg_start;
  this->demux_plugin.seek              = demux_ogg_seek;
  this->demux_plugin.stop              = demux_ogg_stop;
  this->demux_plugin.close             = demux_ogg_close;
  this->demux_plugin.get_status        = demux_ogg_get_status;
  this->demux_plugin.get_identifier    = demux_ogg_get_id;
  this->demux_plugin.get_stream_length = demux_ogg_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_ogg_get_mimetypes;
  
  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );
  
  return (demux_plugin_t *) this;
}
