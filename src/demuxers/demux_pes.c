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
 * $Id: demux_pes.c,v 1.42 2002/11/09 23:22:32 guenter Exp $
 *
 * demultiplexer for mpeg 2 PES (Packetized Elementary Streams)
 * reads streams of variable blocksizes
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>

#include "xine_internal.h"
#include "demux.h"
#include "xineutils.h"

#define NUM_PREVIEW_BUFFERS 400

#define VALID_MRLS          "fifo,stdin"
#define VALID_ENDS          "vdr"

typedef struct demux_pes_s {

  demux_plugin_t       demux_plugin;

  xine_t              *xine;

  config_values_t     *config;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;

  unsigned char        dummy_space[100000];

  int                  status;
  int                  preview_mode;

  int                  send_end_buffers;
  
  int64_t              last_scr;
  int                  send_newpts;
} demux_pes_t ;

static uint32_t read_bytes (demux_pes_t *this, int n) {
  
  uint32_t res;
  uint32_t i;
  unsigned char buf[6];

  buf[4]=0;


  i = this->input->read (this->input, buf, n);

  if (i != n) {
    
    this->status = DEMUX_FINISHED;

  }
  
  switch (n)  {
  case 1:
    res = buf[0];
    break;
  case 2:
    res = (buf[0]<<8) | buf[1];
    break;
  case 3:
    res = (buf[0]<<16) | (buf[1]<<8) | buf[2];
    break;
  case 4:
    res = (buf[2]<<8) | buf[3] | (buf[1]<<16) | (buf[0] << 24);
    break;
  default:
    printf ("demux_pes: how how - something wrong in wonderland demux:read_bytes (%d)\n", n);
    abort();
  }

  return res;
}

static void check_newpts( demux_pes_t *this, int64_t pts )
{
  if( this->send_newpts && !this->preview_mode && pts ) {
    
    xine_demux_control_newpts(this->xine, pts, 0);
    this->send_newpts = 0;
  }
}


static void parse_mpeg2_packet (demux_pes_t *this, int nID) {

  int            nLen, i;
  uint32_t       w, flags, header_len;
  int64_t        pts;
  buf_element_t *buf = NULL;

  nLen = read_bytes(this, 2);


  if (nID==0xbd) {

    int track;

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    nLen -= header_len + 3;

    pts=0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
      
      check_newpts( this, pts );
    }

    /* read rest of header */
//    i = this->input->read (this->input, this->dummy_space, header_len+4);

//    track = this->dummy_space[0] & 0x0F ;
    track = 0;

    /* contents */

    if(this->audio_fifo)
      buf = this->input->read_block (this->input, this->audio_fifo, nLen);
    else {
      this->input->read (this->input, this->dummy_space, nLen);
      return;
    }
    
    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type      = BUF_AUDIO_A52 + track;
    buf->pts       = pts;
    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;

    buf->input_pos = this->input->get_current_pos (this->input);
    
    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((nID & 0xe0) == 0xc0) {
    int track = nID & 0x1f;

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    nLen -= header_len + 3;

    pts = 0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
      
      check_newpts( this, pts );
    }

    /* read rest of header */
    i = this->input->read (this->input, this->dummy_space, header_len);

    if(this->audio_fifo)
      buf = this->input->read_block (this->input, this->audio_fifo, nLen);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type      = BUF_AUDIO_MPEG + track;
    buf->pts       = pts;
    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    
    buf->input_pos = this->input->get_current_pos(this->input);

    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((nID >= 0xbc) && ((nID & 0xf0) == 0xe0)) {

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    nLen -= header_len + 3;

    pts = 0;

    if ((flags & 0x80) == 0x80) {

      w = read_bytes(this, 1);
      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) << 14;
      w = read_bytes(this, 2); 
      pts |= (w & 0xFFFE) >> 1;

      header_len -= 5 ;
      
      check_newpts( this, pts );
    }

    /* read rest of header */
    i = this->input->read (this->input, this->dummy_space, header_len);

    /* contents */

    if(this->audio_fifo)
      buf = this->input->read_block (this->input, this->audio_fifo, nLen);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type = BUF_VIDEO_MPEG;
    buf->pts  = pts;
    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    
    buf->input_pos = this->input->get_current_pos(this->input);

    this->video_fifo->put (this->video_fifo, buf);

  } else {

    i = this->input->read (this->input, this->dummy_space, nLen); 
    /* (*this->input->seek) (nLen,SEEK_CUR); */
  }

}


static uint32_t parse_pack(demux_pes_t *this)
{
  uint32_t buf ;

  buf = read_bytes (this, 1) ;

  if (this->status != DEMUX_OK)
      return buf;

  parse_mpeg2_packet (this, buf & 0xFF);

  buf = read_bytes (this, 3);
  
  return buf;

}

static void demux_pes_resync (demux_pes_t *this, uint32_t buf) {

  while ((buf !=0x000001) && (this->status == DEMUX_OK)) {
    buf = (buf << 8) | read_bytes (this, 1);
  }
}

static void *demux_pes_loop (void *this_gen) {

  demux_pes_t *this = (demux_pes_t *) this_gen;
  uint32_t w=0;

  /* do-while needed to seek after demux finished */
  do {

    /* main demuxer loop */
    while(this->status == DEMUX_OK) {
      w = parse_pack (this);

      if (w != 0x000001)
        demux_pes_resync (this, w);

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      /* give demux_*_stop a chance to interrupt us */
      sched_yield();
      pthread_mutex_lock( &this->mutex );
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->video_fifo->size(this->video_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while( this->status == DEMUX_OK );
  
  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  printf ("demux_pes: demux loop finished (status: %d, buf:%x)\n",
	  this->status, w);

  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );
  pthread_exit(NULL);

  return NULL;
}

static void demux_pes_stop (demux_plugin_t *this_gen) {

  demux_pes_t *this = (demux_pes_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );
  
  if (!this->thread_running) {
    printf ("demux_pes: stop...ignored\n");
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

static int demux_pes_get_status (demux_plugin_t *this_gen) {
  demux_pes_t *this = (demux_pes_t *) this_gen;

  return this->status;
}

static int demux_pes_start (demux_plugin_t *this_gen,
			     fifo_buffer_t *video_fifo,
			     fifo_buffer_t *audio_fifo,
			     off_t start_pos, int start_time) {

  demux_pes_t *this = (demux_pes_t *) this_gen;
  int err;
  int status;

  pthread_mutex_lock( &this->mutex );

  if( !this->thread_running ) {
    this->video_fifo  = video_fifo;
    this->audio_fifo  = audio_fifo;

    /* 
     * send start buffer
     */

    xine_demux_control_start(this->xine);

    if ((this->input->get_capabilities (this->input) & INPUT_CAP_PREVIEW) != 0 ) {

      uint32_t w;
      int num_buffers = NUM_PREVIEW_BUFFERS;

      this->preview_mode = 1;

      this->input->seek (this->input, 3, SEEK_SET);

      this->status = DEMUX_OK ;

      do {
        w = parse_pack (this);
      
        if (w != 0x000001)
          demux_pes_resync (this, w);
      
        num_buffers --;

      } while ( (this->status == DEMUX_OK) && (num_buffers>0)) ;
      this->status = DEMUX_FINISHED;
    }
  }
  
  if ((this->input->get_capabilities (this->input) & INPUT_CAP_SEEKABLE) != 0 ) {
    
    /* FIXME: implement time seek */
    this->input->seek (this->input, start_pos+3, SEEK_SET);

    if( start_pos )
      demux_pes_resync (this, read_bytes(this, 3) );

  } else
    read_bytes(this, 3);

  this->send_newpts = 1;
    
  if( !this->thread_running ) {
    this->preview_mode = 0;
    this->send_end_buffers = 1;
    this->status = DEMUX_OK ;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread,
			     NULL, demux_pes_loop, this)) != 0) {
      printf ("demux_pes: can't create new thread (%s)\n",
	      strerror(err));
      abort();
    }
  }
  else {
    xine_demux_flush_engine(this->xine);
  }

  /* this->status is saved because we can be interrupted between
   * pthread_mutex_unlock and return
   */
  status = this->status;
  pthread_mutex_unlock( &this->mutex );
  return status;
}

static int demux_pes_seek (demux_plugin_t *this_gen,
			     off_t start_pos, int start_time) {
  demux_pes_t *this = (demux_pes_t *) this_gen;

	return demux_pes_start (this_gen, this->video_fifo, this->audio_fifo,
			 start_pos, start_time);
}

static int demux_pes_open(demux_plugin_t *this_gen, 
			   input_plugin_t *input, int stage) {

  demux_pes_t *this = (demux_pes_t *) this_gen;

  this->input = input;

  switch(stage) {
    
  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];
    
    if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      input->seek(input, 0, SEEK_SET);
      
      if(input->get_blocksize(input))
	return DEMUX_CANNOT_HANDLE;
      
      if(input->read(input, buf, 6)) {
	
	if(buf[0] || buf[1] || (buf[2] != 0x01))
	  return DEMUX_CANNOT_HANDLE;
	
	switch(buf[3]) {
	  
	case 0xe0:
	case 0xc0:
	case 0xc1:
	case 0xbd:
	    
	  return DEMUX_CAN_HANDLE;
	  break;
	  
	}
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;

  case STAGE_BY_EXTENSION: {
    char *media;
    char *ending;
    char *m, *valid_mrls, *valid_ends;
    char *MRL = input->get_mrl(input);
    
    xine_strdupa(valid_mrls, (this->config->register_string(this->config,
							    "mrl.mrls_pes", VALID_MRLS,
							    _("valid mrls for pes demuxer"),
							    NULL, 20, NULL, NULL)));
    
    media = strstr(MRL, ":/");
    if(media) {
      while((m = xine_strsep(&valid_mrls, ",")) != NULL) { 
	
	while(*m == ' ' || *m == '\t') m++;
	
	if(!strncmp(MRL, m, strlen(m))) {
	  
	  if(!strncmp((media + 3), "pes", 3)) {
	    this->input = input;
	    return DEMUX_CAN_HANDLE;
	  }
	  return DEMUX_CANNOT_HANDLE;
	}
	else if(strncasecmp(MRL, "file", 4)) {
	  return DEMUX_CANNOT_HANDLE;
	}
      }
    }

    ending = strrchr(MRL, '.');
    
    if(!ending)
      return DEMUX_CANNOT_HANDLE;
    
    xine_strdupa(valid_ends, (this->config->register_string(this->config,
							    "mrl.ends_pes", VALID_ENDS,
							    _("valid mrls ending for pes demuxer"),
							    NULL, 20, NULL, NULL)));
    while((m = xine_strsep(&valid_ends, ",")) != NULL) { 
      
      while(*m == ' ' || *m == '\t') m++;
      
      if(!strcasecmp((ending + 1), m)) {
	this->input = input;
	return DEMUX_CAN_HANDLE;
      }
    }
  }
  break;

  case METHOD_EXPLICIT:
  break;

  default:
    return DEMUX_CANNOT_HANDLE;
    break;
  }

  return DEMUX_CANNOT_HANDLE;
}

static char *demux_pes_get_id(void) {
  return "MPEG_PES";
}

static char *demux_pes_get_mimetypes(void) {
  return "";
}

static void demux_pes_close (demux_plugin_t *this) {
  free (this);
}

static int demux_pes_get_stream_length (demux_plugin_t *this_gen) {

  /* demux_pes_t *this = (demux_pes_t *) this_gen; */

  return 0; /* FIXME: implement */
}

static void *init_demuxer_plugin(xine_t *xine, void *data) {

  demux_pes_t     *this;

  this         = xine_xmalloc (sizeof (demux_pes_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config, "mrl.mrls_pes", VALID_MRLS,
					_("valid mrls for pes demuxer"),
					NULL, 20, NULL, NULL);
  (void*) this->config->register_string(this->config,
					"mrl.ends_pes", VALID_ENDS,
					_("valid mrls ending for pes demuxer"),
					NULL, 20, NULL, NULL);    
  
  this->demux_plugin.open              = demux_pes_open;
  this->demux_plugin.start             = demux_pes_start;
  this->demux_plugin.seek              = demux_pes_seek;
  this->demux_plugin.stop              = demux_pes_stop;
  this->demux_plugin.close             = demux_pes_close;
  this->demux_plugin.get_status        = demux_pes_get_status;
  this->demux_plugin.get_identifier    = demux_pes_get_id;
  this->demux_plugin.get_stream_length = demux_pes_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_mimetypes     = demux_pes_get_mimetypes;
  
  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );
  
  return (demux_plugin_t *) this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 10, "mpeg-pes", XINE_VERSION_CODE, NULL, init_demuxer_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
