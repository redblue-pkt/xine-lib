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
 * $Id: demux_mpeg.c,v 1.54 2002/04/11 22:27:11 jcdutton Exp $
 *
 * demultiplexer for mpeg 1/2 program streams
 * reads streams of variable blocksizes
 *
 * currently only used for mpeg-1-files
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
#include <string.h>
#include <unistd.h>

#include "xine_internal.h"
#include "demux.h"
#include "xineutils.h"

#define VALID_MRLS          "stdin,fifo"
#define VALID_ENDS          "mpg,mpeg,mpe"

#define NUM_PREVIEW_BUFFERS 150

typedef struct demux_mpeg_s {

  demux_plugin_t       demux_plugin;
  
  xine_t              *xine;

  config_values_t     *config;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  pthread_t            thread;
  pthread_mutex_t      mutex;

  unsigned char        dummy_space[100000];

  int                  status;
  int                  preview_mode;

  int                  rate;

  int                  send_end_buffers;

  int64_t              last_scr;
  int                  send_newpts;
 
} demux_mpeg_t;

static uint32_t read_bytes (demux_mpeg_t *this, int n) {
  
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
    printf ("demux_mpeg: how how - something wrong in wonderland demux:read_bytes (%d)\n", n);
    exit (1);
  }

  return res;
}


static void check_newpts( demux_mpeg_t *this, int64_t pts )
{
  if( this->send_newpts && !this->preview_mode && pts ) {
    
    buf_element_t *buf;
  
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type = BUF_CONTROL_NEWPTS;
    buf->disc_off = pts;
    this->video_fifo->put (this->video_fifo, buf);

    if (this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_CONTROL_NEWPTS;
      buf->disc_off = pts;
      this->audio_fifo->put (this->audio_fifo, buf);
    }
    this->send_newpts = 0;
  }
}

static void parse_mpeg2_packet (demux_mpeg_t *this, int stream_id, int64_t scr) {

  int            len, i;
  uint32_t       w, flags, header_len;
  int64_t        pts;
  buf_element_t *buf = NULL;

  len = read_bytes(this, 2);

  if (stream_id==0xbd) {

    int track;

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    len -= header_len + 3;

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
    i = this->input->read (this->input, this->dummy_space, header_len+4);

    track = this->dummy_space[0] & 0x0F ;

    /* contents */

    if(this->audio_fifo)
      buf = this->input->read_block (this->input, this->audio_fifo, len-4);
    else {
      this->input->read (this->input, this->dummy_space, len-4);
      return;
    }

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type      = BUF_AUDIO_A52 + track;
    buf->pts       = pts;
    /* buf->scr       = scr;*/
    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    else
      buf->decoder_flags = 0;

    buf->input_pos = this->input->get_current_pos (this->input);

    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((stream_id & 0xe0) == 0xc0) {
    int track = stream_id & 0x1f;

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    len -= header_len + 3;

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
      buf = this->input->read_block (this->input, this->audio_fifo, len);
    else {
      this->input->read (this->input, this->dummy_space, len);
      return;
    }

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type      = BUF_AUDIO_MPEG + track;
    buf->pts       = pts;
    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    else
      buf->decoder_flags = 0;
    buf->input_pos = this->input->get_current_pos(this->input);

    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((stream_id >= 0xbc) && ((stream_id & 0xf0) == 0xe0)) {

    w = read_bytes(this, 1);
    flags = read_bytes(this, 1);
    header_len = read_bytes(this, 1);

    len -= header_len + 3;

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

    buf = this->input->read_block (this->input, this->video_fifo, len);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type = BUF_VIDEO_MPEG;
    buf->pts  = pts;
    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    else
      buf->decoder_flags = 0;
    buf->input_pos = this->input->get_current_pos(this->input);

    this->video_fifo->put (this->video_fifo, buf);

  } else {

    i = this->input->read (this->input, this->dummy_space, len);
    /* (*this->input->seek) (len,SEEK_CUR); */
  }

}

static void parse_mpeg1_packet (demux_mpeg_t *this, int stream_id, int64_t scr) {

  int             len;
  uint32_t        w;
  int             i;
  int64_t         pts;
  buf_element_t  *buf = NULL;

  len = read_bytes(this, 2);

  pts=0;

  if (stream_id != 0xbf) {

    w = read_bytes(this, 1); len--;

    while ((w & 0x80) == 0x80)   {

      if (this->status != DEMUX_OK)
	return;

      /* stuffing bytes */
      w = read_bytes(this, 1); len--;
    }

    if ((w & 0xC0) == 0x40) {

      if (this->status != DEMUX_OK)
	return;

      /* buffer_scale, buffer size */
      w = read_bytes(this, 1); len--;
      w = read_bytes(this, 1); len--;
    }

    if ((w & 0xF0) == 0x20) {

      if (this->status != DEMUX_OK)
	return;

      pts = (w & 0xe) << 29 ;
      w = read_bytes(this, 2); len -= 2;

      pts |= (w & 0xFFFE) << 14;

      w = read_bytes(this, 2); len -= 2;
      pts |= (w & 0xFFFE) >> 1;

      /* pts = 0; */

    } else if ((w & 0xF0) == 0x30) {

      if (this->status != DEMUX_OK)
	return;

      pts = (w & 0x0e) << 29 ;
      w = read_bytes(this, 2); len -= 2;

      pts |= (w & 0xFFFE) << 14;

      w = read_bytes(this, 2); len -= 2;

      pts |= (w & 0xFFFE) >> 1;

/*       printf ("pts2=%d\n",pts); */

      /* Decoding Time Stamp */
      w = read_bytes(this, 3); len -= 3;
      w = read_bytes(this, 2); len -= 2;
    } else {

      /*
      if (w != 0x0f)
	xprintf (VERBOSE|DEMUX, " ERROR w (%02x) != 0x0F ",w);
      */
    }

  }
  check_newpts( this, pts );

  if ((stream_id & 0xe0) == 0xc0) {
    int track = stream_id & 0x1f;

    if(this->audio_fifo) {
      buf = this->input->read_block (this->input, this->audio_fifo, len);
    } else {
      this->input->read (this->input, this->dummy_space, len);
      return;
    }

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type      = BUF_AUDIO_MPEG + track ;
    buf->pts       = pts;
    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    else
      buf->decoder_flags = 0;
    buf->input_pos = this->input->get_current_pos(this->input);
    if (this->rate)
      buf->input_time = buf->input_pos / (this->rate * 50);

    if(this->audio_fifo)
      this->audio_fifo->put (this->audio_fifo, buf);

  } else if ((stream_id & 0xf0) == 0xe0) {

    buf = this->input->read_block (this->input, this->video_fifo, len);

    if (buf == NULL) {
      this->status = DEMUX_FINISHED;
      return ;
    }
    buf->type = BUF_VIDEO_MPEG;
    buf->pts  = pts;
    if (this->preview_mode)
      buf->decoder_flags = BUF_FLAG_PREVIEW;
    else
      buf->decoder_flags = 0;
    buf->input_pos = this->input->get_current_pos(this->input);
    if (this->rate)
      buf->input_time = buf->input_pos / (this->rate * 50);

    this->video_fifo->put (this->video_fifo, buf);

  } else if (stream_id == 0xbd) {

    i = this->input->read (this->input, this->dummy_space, len);
  } else {

    this->input->read (this->input, this->dummy_space, len);
  }

}

static uint32_t parse_pack(demux_mpeg_t *this) {

  uint32_t  buf ;
  int       mpeg_version;
  int64_t   scr;


  buf = read_bytes (this, 1);

  if ((buf>>4) == 4) {

    int stuffing, i;

    mpeg_version = 2;
    
    /* system_clock_reference */
    
    scr  = (buf & 0x08) << 27;
    scr  = (buf & 0x03) << 28;
    buf  = read_bytes (this, 1);
    scr |= buf << 20;
    buf  = read_bytes (this, 1);
    scr |= (buf & 0xF8) << 12 ;
    scr |= (buf & 0x03) << 13 ;
    buf  = read_bytes (this, 1);
    scr |= buf << 5;
    buf  = read_bytes (this, 1);
    scr |= (buf & 0xF8) >> 3;
    buf  = read_bytes (this, 1); /* extension */
    
    /* mux_rate */
    
    buf = read_bytes(this,3);
    if (!this->rate) {
      this->rate = (buf & 0xFFFFFC) >> 2;
    }     
    
    /* stuffing bytes */
    buf = read_bytes(this,1);
    stuffing = buf &0x03;
    for (i=0; i<stuffing; i++)
      read_bytes (this, 1);

  } else {

     mpeg_version = 1;

     /* system_clock_reference */

     scr = (buf & 0x2) << 30;
     buf = read_bytes (this, 2);
     scr |= (buf & 0xFFFE) << 14;
     buf = read_bytes (this, 2);
     scr |= (buf & 0xFFFE) >>1;

     /* mux_rate */
     
     if (!this->rate) {
       buf = read_bytes (this,1);
       this->rate = (buf & 0x7F) << 15;
       buf = read_bytes (this,1);
       this->rate |= (buf << 7);
       buf = read_bytes (this,1);
       this->rate |= (buf >> 1);
       
       /* printf ("demux_mpeg: mux_rate = %d\n",this->rate);  */
       
     } else
       buf = read_bytes (this, 3) ;
  }

  /* discontinuity ? */
  if( scr && !this->preview_mode )
  {  
    int64_t scr_diff = scr - this->last_scr;
    
    if (abs(scr_diff) > 60000 && !this->send_newpts) {
     
      buf_element_t *buf;

      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = BUF_CONTROL_DISCONTINUITY;
      buf->disc_off = scr_diff;
      this->video_fifo->put (this->video_fifo, buf);

      if (this->audio_fifo) {
	buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
	buf->type = BUF_CONTROL_DISCONTINUITY;
	buf->disc_off = scr_diff;
	this->audio_fifo->put (this->audio_fifo, buf);
      }
    }
    this->last_scr = scr;
  }


  /* system header */

  buf = read_bytes (this, 4) ;

  /* printf ("  code = %08x\n",buf);*/

  if (buf == 0x000001bb) {
    buf = read_bytes (this, 2);

    this->input->read (this->input, this->dummy_space, buf);

    buf = read_bytes (this, 4) ;
  }

  /* printf ("  code = %08x\n",buf); */

  while ( ((buf & 0xFFFFFF00) == 0x00000100)
	  && ((buf & 0xff) != 0xba) ) {

    if (this->status != DEMUX_OK)
      return buf;

    if (mpeg_version == 1)
      parse_mpeg1_packet (this, buf & 0xFF, scr);
    else
      parse_mpeg2_packet (this, buf & 0xFF, scr);

    buf = read_bytes (this, 4);

  }

  return buf;

}

static uint32_t parse_pack_preview (demux_mpeg_t *this, int *num_buffers)
{
  uint32_t buf ;
  int mpeg_version;

  /* system_clock_reference */
  buf = read_bytes (this, 1);

  if ((buf>>4) == 4) {
     buf = read_bytes(this, 2);
     mpeg_version = 2;
  } else {
     mpeg_version = 1;
  }

  buf = read_bytes (this, 4);

  /* mux_rate */

  if (!this->rate) {
    buf = read_bytes (this,1);
    this->rate = (buf & 0x7F) << 15;
    buf = read_bytes (this,1);
    this->rate |= (buf << 7);
    buf = read_bytes (this,1);
    this->rate |= (buf >> 1);

    /* printf ("demux_mpeg: mux_rate = %d\n",this->rate); */

  } else
    buf = read_bytes (this, 3) ;

  /* system header */

  buf = read_bytes (this, 4) ;

  if (buf == 0x000001bb) {
    buf = read_bytes (this, 2);
    this->input->read (this->input, this->dummy_space, buf);
    buf = read_bytes (this, 4) ;
  }
  
  while ( ((buf & 0xFFFFFF00) == 0x00000100)
	  && ((buf & 0xff) != 0xba) 
	  && (*num_buffers > 0)) {

    if (this->status != DEMUX_OK)
      return buf;

    if (mpeg_version == 1)
      parse_mpeg1_packet (this, buf & 0xFF, 0);
    else
      parse_mpeg2_packet (this, buf & 0xFF, 0);

    buf = read_bytes (this, 4);
    *num_buffers = *num_buffers - 1;
  }
  
  return buf;

}

static void demux_mpeg_resync (demux_mpeg_t *this, uint32_t buf) {

  while ((buf !=0x000001ba) && (this->status == DEMUX_OK)) {

    buf = (buf << 8) | read_bytes (this, 1);
  }
}

static void *demux_mpeg_loop (void *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  buf_element_t *buf;
  uint32_t w=0;

  while(1) {
    
    pthread_mutex_lock( &this->mutex );
    
    if( this->status != DEMUX_OK)
      break;
    
    w = parse_pack (this);

    if (w != 0x000001ba)
      demux_mpeg_resync (this, w);
    
    pthread_mutex_unlock( &this->mutex );
  
  }
  
  if (this->send_end_buffers) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_flags   = BUF_FLAG_END_STREAM;
    this->video_fifo->put (this->video_fifo, buf);

    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type            = BUF_CONTROL_END;
      buf->decoder_flags   = BUF_FLAG_END_STREAM;
      this->audio_fifo->put (this->audio_fifo, buf);
    }
  }

  printf ("demux_mpeg: demux loop finished (status: %d, buf:%x)\n",
	  this->status, w);

  pthread_mutex_unlock( &this->mutex );
  
  pthread_exit(NULL);

  return NULL;
}

static void demux_mpeg_stop (demux_plugin_t *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  buf_element_t *buf;
  void *p;

  pthread_mutex_lock( &this->mutex );
  
  if (this->status != DEMUX_OK) {
    printf ("demux_mpeg: stop...ignored\n");
    pthread_mutex_unlock( &this->mutex );
    return;
  }
  
  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;
  
  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_flush_engine(this->xine);

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type            = BUF_CONTROL_END;
  buf->decoder_flags   = BUF_FLAG_END_USER; /* user finished */
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_flags   = BUF_FLAG_END_USER; /* user finished */
    this->audio_fifo->put (this->audio_fifo, buf);
  }

}

static int demux_mpeg_get_status (demux_plugin_t *this_gen) {
  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  return this->status;
}

static void demux_mpeg_start (demux_plugin_t *this_gen,
			      fifo_buffer_t *video_fifo,
			      fifo_buffer_t *audio_fifo,
			      off_t start_pos, int start_time) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;
  buf_element_t *buf;
  int err;

  pthread_mutex_lock( &this->mutex );

  if( this->status != DEMUX_OK ) {
  
    this->video_fifo  = video_fifo;
    this->audio_fifo  = audio_fifo;
  
    this->rate          = 0; /* fixme */
    this->last_scr      = 0;

    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type    = BUF_CONTROL_START;
    this->video_fifo->put (this->video_fifo, buf);

    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type    = BUF_CONTROL_START;
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    if ((this->input->get_capabilities (this->input) & INPUT_CAP_PREVIEW) != 0 ) {

      uint32_t w;
      int num_buffers = NUM_PREVIEW_BUFFERS;

      this->preview_mode = 1;

      this->input->seek (this->input, 4, SEEK_SET);

      this->status = DEMUX_OK ;

      do {

        w = parse_pack_preview (this, &num_buffers);
      
        if (w != 0x000001ba)
	  demux_mpeg_resync (this, w);
      
        num_buffers --;

      } while ( (this->status == DEMUX_OK) && (num_buffers>0)) ;

      /* printf ("demux_mpeg: rate %d\n", this->rate); */
      this->status = DEMUX_FINISHED;
    }
  }  
  
  if ((this->input->get_capabilities (this->input) & INPUT_CAP_SEEKABLE) != 0 ) {
    
    if ( (!start_pos) && (start_time))
      start_pos = start_time * this->rate * 50;

    this->input->seek (this->input, start_pos+4, SEEK_SET);

    if( start_pos )
      demux_mpeg_resync (this, read_bytes(this, 4) );
  
  } else
    read_bytes(this, 4);
  
  this->send_newpts = 1;
    
  if( this->status != DEMUX_OK ) {
    this->preview_mode = 0;
    this->send_end_buffers = 1;
    this->status = DEMUX_OK ;

    if ((err = pthread_create (&this->thread,
			     NULL, demux_mpeg_loop, this)) != 0) {
      printf ("demux_mpeg: can't create new thread (%s)\n",
	      strerror(err));
      exit (1);
    }
  }
  else {
    xine_flush_engine(this->xine);
  }
  pthread_mutex_unlock( &this->mutex );
  
}

static void demux_mpeg_seek (demux_plugin_t *this_gen,
			     off_t start_pos, int start_time) {
  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;

	demux_mpeg_start (this_gen, this->video_fifo, this->audio_fifo,
			 start_pos, start_time);
}

static int demux_mpeg_open(demux_plugin_t *this_gen, 
			   input_plugin_t *input, int stage) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;

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
	  
	case 0xba:
	  if((buf[4] & 0xf0) == 0x20) {
	    uint32_t pckbuf ;
	    
	    pckbuf = read_bytes (this, 1);
	    if ((pckbuf>>4) != 4) {
	      this->input = input;
	      return DEMUX_CAN_HANDLE;
	    }
	  }
	  break;
#if 0	  
	case 0xe0:
	  if((buf[6] & 0xc0) != 0x80) {
	    uint32_t pckbuf ;
	    
	    pckbuf = read_bytes (this, 1);
	    if ((pckbuf>>4) != 4) {
	      this->input = input;
	      return DEMUX_CAN_HANDLE;
	    }
	  }
	  break;
#endif	  
	}
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;

  case STAGE_BY_EXTENSION: {
    char *media;
    char *ending;
    char *MRL = input->get_mrl(input);
    char *m, *valid_mrls, *valid_ends;

    xine_strdupa(valid_mrls, (this->config->register_string(this->config,
							    "mrl.mrls_mpeg", VALID_MRLS,
							    "valid mrls for mpeg demuxer",
							    NULL, NULL, NULL)));
    
    media = strstr(MRL, "://");
    if (media) {
      while((m = xine_strsep(&valid_mrls, ",")) != NULL) { 
	
	while(*m == ' ' || *m == '\t') m++;
	
	if(!strncmp(MRL, m, strlen(m))) {
	  
	  if(!strncmp((media + 3), "mpeg1", 5)) {
	    this->input = input;
	    return DEMUX_CAN_HANDLE;
	  }
	  else if(!strncasecmp((media + 3), "mpeg2", 5)) {
	    return DEMUX_CANNOT_HANDLE;
	  }
	  
	  xine_log (this->xine, XINE_LOG_MSG, _("demux_mpeg: please specify mpeg(mpeg1/mpeg2) stream type.\n"));
	  return DEMUX_CANNOT_HANDLE;
	}
      }
    }

    ending = strrchr(MRL, '.');
    
    if(!ending)
      return DEMUX_CANNOT_HANDLE;
    
    xine_strdupa(valid_ends, (this->config->register_string(this->config,
							    "mrl.ends_mpeg", VALID_ENDS,
							    "valid mrls ending for mpeg demuxer",
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
  
  default:
    return DEMUX_CANNOT_HANDLE;
    break;
  }

  return DEMUX_CANNOT_HANDLE;
}

static char *demux_mpeg_get_id(void) {
  return "MPEG";
}

static char *demux_mpeg_get_mimetypes(void) {
  return "video/mpeg: mpeg, mpg, mpe: MPEG animation;"
         "video/x-mpeg: mpeg, mpg, mpe: MPEG animation;";
}

static void demux_mpeg_close (demux_plugin_t *this) {
  /* nothing */
}

static int demux_mpeg_get_stream_length (demux_plugin_t *this_gen) {

  demux_mpeg_t *this = (demux_mpeg_t *) this_gen;

  if (this->rate) 
    return this->input->get_length (this->input) / (this->rate * 50);
  else
    return 0;

}

demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {

  demux_mpeg_t    *this;

  if (iface != 7) {
    printf ("demux_mpeg: plugin doesn't support plugin API version %d.\n"
	    "            this means there's a version mismatch between xine and this "
	    "            demuxer plugin.\nInstalling current demux plugins should help.\n",
	    iface);
    return NULL;
  }
  
  this         = xine_xmalloc (sizeof (demux_mpeg_t));
  this->config = xine->config;
  this->xine   = xine;

  /* Calling register_string() configure valid mrls in configfile */
  (void*) this->config->register_string(this->config, "mrl.mrls_mpeg", VALID_MRLS,
					"valid mrls for mpeg demuxer",
					NULL, NULL, NULL);
  (void*) this->config->register_string(this->config,
					"mrl.ends_mpeg", VALID_ENDS,
					"valid mrls ending for mpeg demuxer",
					NULL, NULL, NULL);    
    
  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_mpeg_open;
  this->demux_plugin.start             = demux_mpeg_start;
  this->demux_plugin.seek              = demux_mpeg_seek;
  this->demux_plugin.stop              = demux_mpeg_stop;
  this->demux_plugin.close             = demux_mpeg_close;
  this->demux_plugin.get_status        = demux_mpeg_get_status;
  this->demux_plugin.get_identifier    = demux_mpeg_get_id;
  this->demux_plugin.get_stream_length = demux_mpeg_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_mpeg_get_mimetypes;
  
  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );
  
  return (demux_plugin_t *) this;
}

