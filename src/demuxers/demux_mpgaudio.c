/* 
 * Copyright (C) 2000, 2001 the xine project
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
 * $Id: demux_mpgaudio.c,v 1.44 2002/05/19 16:21:04 tmattern Exp $
 *
 * demultiplexer for mpeg audio (i.e. mp3) streams
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

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "demux.h"

#define DEMUX_MPGAUDIO_IFACE_VERSION 3

#define VALID_ENDS                   "mp3,mp2,mpa,mpega"
#define WRAP_THRESHOLD       120000


typedef struct {

  demux_plugin_t       demux_plugin;

  xine_t              *xine;

  config_values_t     *config;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;

  int                  status;

  int                  send_end_buffers;

  int                  stream_length;
  long                 bitrate;
  int64_t              last_pts;
  int                  send_newpts;

} demux_mpgaudio_t ;


/* bitrate table tabsel_123[mpeg version][layer][bitrate index] */
int tabsel_123[2][3][16] = {
   { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,},
     {0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384,},
     {0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,} },

   { {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,} }
};

/* sampling rate frequency table */
long freqs[9] = { 44100, 48000, 32000,
                  22050, 24000, 16000 ,
                  11025 , 12000 , 8000 };

static int mpg123_head_check(unsigned long head) {
  if ((head & 0xffe00000) != 0xffe00000)
    return 0;
  if (!((head >> 17) & 3))
    return 0;
  if (((head >> 12) & 0xf) == 0xf)
    return 0;
  if (!((head >> 12) & 0xf))
    return 0;
  if (((head >> 10) & 0x3) == 0x3)
    return 0;
  if (((head >> 19) & 1) == 1 
      && ((head >> 17) & 3) == 3 
      && ((head >> 16) & 1) == 1)
    return 0;
  if ((head & 0xffff0000) == 0xfffe0000)
    return 0;
  
  return 1;
}

static void mpg123_decode_header(demux_mpgaudio_t *this,unsigned long newhead)
{
  int lsf, mpeg25;
  int lay, sampling_frequency, bitrate_index, padding;
  long framesize = 1;
  static int bs[4] = {0, 384, 1152, 1152};
  double tpf;
  char * ver;

  /*
   * lsf==0 && mpeg25==0 : MPEG Version 1 (ISO/IEC 11172-3)
   * lsf==1 && mpeg25==0 : MPEG Version 2 (ISO/IEC 13818-3)
   * lsf==1 && mpeg25==1 : MPEG Version 2.5 (later extension of MPEG 2)
   */
  if( newhead & (1<<20) ) {
    lsf = (newhead & (1<<19)) ? 0x0 : 0x1;
    if (lsf) {
      ver = "2";
    } else {
      ver = "1";
    }
    mpeg25 = 0;
  } else {
    lsf = 1;
    mpeg25 = 1;
    ver = "2.5";
  }

  /* Layer I, II, III */
  lay = 4-((newhead>>17)&3);

/*
  if(mpeg25) {
    sampling_frequency = 6 + ((newhead>>10)&0x3);
  }
  else {
    sampling_frequency = ((newhead>>10)&0x3) + (lsf*3);
  }
*/

  bitrate_index = ((newhead>>12)&0xf);
/*
  padding   = ((newhead>>9)&0x1);

  switch(lay)
  {
    case 1:
      framesize  = (long) tabsel_123[lsf][0][bitrate_index] * 12000;
      framesize /= freqs[sampling_frequency];
      framesize  = ((framesize+padding)<<2)-4;
      break;
    case 2:
      framesize = (long) tabsel_123[lsf][1][bitrate_index] * 144000;
      framesize /= freqs[sampling_frequency];
      framesize += padding - 4;
      break;
    case 3:
      framesize  = (long) tabsel_123[lsf][2][bitrate_index] * 144000;
      framesize /= freqs[sampling_frequency]<<(lsf);
      framesize = framesize + padding - 4;
      break;
  }

  tpf = (double) bs[lay];
  tpf /= freqs[sampling_frequency] << lsf;

  this->bitrate = (double) framesize / tpf;
*/
  this->bitrate = tabsel_123[lsf][lay - 1][bitrate_index];
  xine_log (this->xine, XINE_LOG_FORMAT, 
	    _("demux_mpgaudio: MPEG %s  Layer %d  %ldkbps\n"), ver, lay, this->bitrate );
  this->stream_length = (int)(this->input->get_length(this->input) / (this->bitrate * 1000 / 8));
}

static void check_newpts( demux_mpgaudio_t *this, int64_t pts )
{
  int64_t diff;

  diff = pts - this->last_pts;

  if( pts &&
      (this->send_newpts || (this->last_pts && abs(diff)>WRAP_THRESHOLD) ) ) {

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

  if( pts )
    this->last_pts = pts;
}

static int demux_mpgaudio_next (demux_mpgaudio_t *this) {

  buf_element_t *buf = NULL;
  uint32_t head;
  off_t buffer_pos;
  uint64_t pts;

  buffer_pos = this->input->get_current_pos(this->input);
  if(this->audio_fifo)
    buf = this->input->read_block(this->input, 
				  this->audio_fifo, 2048);

  if (buf == NULL) {
    this->status = DEMUX_FINISHED;
    return 0;
  }

  if( this->stream_length == 0 )
  {
    int i;
    for( i = 0; i < buf->size-4; i++ )
    {
      head = (buf->mem[i+0] << 24) + (buf->mem[i+1] << 16) +
             (buf->mem[i+2] << 8) + buf->mem[i+3];

      if (mpg123_head_check(head))
      {
         mpg123_decode_header(this,head);
         break;
      }
    }
  }

  pts = (90000 * buffer_pos) / (this->bitrate * 1000 / 8);
  check_newpts(this, pts);

  /*buf->pts             = 0;*/
  /*buf->scr             = 0;*/
  buf->input_pos       = this->input->get_current_pos(this->input);
  buf->input_time      = buf->input_pos * this->stream_length /
                         this->input->get_length(this->input);
  buf->pts             = pts;
  buf->type            = BUF_AUDIO_MPEG;
  buf->decoder_info[0] = 1;

  if(this->audio_fifo)
    this->audio_fifo->put(this->audio_fifo, buf);

  return (buf->size == 2048);
}

static void *demux_mpgaudio_loop (void *this_gen) {
  buf_element_t *buf;
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  pthread_mutex_lock( &this->mutex );
  /* do-while needed to seek after demux finished */
  do {

    /* main demuxer loop */
    while(this->status == DEMUX_OK) {

      if (!demux_mpgaudio_next(this))
        this->status = DEMUX_FINISHED;

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
  
  this->status = DEMUX_FINISHED;

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
  printf ("demux_mpgaudio: demux loop finished.\n");

  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );
  pthread_exit(NULL);

  return NULL;
}

static void demux_mpgaudio_stop (demux_plugin_t *this_gen) {

  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;
  buf_element_t *buf;
  void *p;

  pthread_mutex_lock( &this->mutex );
  
  if (!this->thread_running) {
    printf ("demux_mpgaudio_block: stop...ignored\n");
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
  buf->decoder_flags   = BUF_FLAG_END_USER;
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_flags   = BUF_FLAG_END_USER;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_mpgaudio_get_status (demux_plugin_t *this_gen) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  return (this->thread_running?DEMUX_OK:DEMUX_FINISHED);
}

static uint32_t demux_mpgaudio_read_head(input_plugin_t *input)
{
  uint8_t buf[4096];
  uint32_t head=0;
  int bs = 0;

  if(!input)
    return 0;

  if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
    input->seek(input, 0, SEEK_SET);

    if (input->get_capabilities (input) & INPUT_CAP_BLOCK)
      bs = input->get_blocksize(input);

    if(!bs)
      bs = 4;

    if(input->read(input, buf, bs))
      head = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
  }
  return head;
}

static void demux_mpgaudio_start (demux_plugin_t *this_gen,
				  fifo_buffer_t *video_fifo, 
				  fifo_buffer_t *audio_fifo,
				  off_t start_pos, int start_time) {

  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;
  buf_element_t *buf;
  int err;
  
  pthread_mutex_lock( &this->mutex );

  if( !this->thread_running ) {
    this->video_fifo  = video_fifo;
    this->audio_fifo  = audio_fifo;
  
    this->stream_length = 0;
    this->last_pts      = 0;
  }
  
  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    uint32_t head;

    if( !this->thread_running ) {
      head = demux_mpgaudio_read_head(this->input);

      if (mpg123_head_check(head))
         mpg123_decode_header(this,head);
    }
    
    if (!start_pos && start_time && this->stream_length > 0)
         start_pos = start_time * this->input->get_length(this->input) /
                     this->stream_length;

    this->input->seek (this->input, start_pos, SEEK_SET);

  }

  this->status = DEMUX_OK;
  this->send_newpts = 1;

  if( !this->thread_running ) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type    = BUF_CONTROL_START;
    this->video_fifo->put (this->video_fifo, buf);

    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type    = BUF_CONTROL_START;
      this->audio_fifo->put (this->audio_fifo, buf);
    }
  
    /*
     * now start demuxing
     */

    this->send_end_buffers = 1;
    this->thread_running = 1;
    if ((err = pthread_create (&this->thread,
			     NULL, demux_mpgaudio_loop, this)) != 0) {
      printf ("demux_mpgaudio: can't create new thread (%s)\n",
	      strerror(err));
      abort();
    }
  }
  else {
    xine_flush_engine(this->xine);
  }
  pthread_mutex_unlock( &this->mutex );
}

static void demux_mpgaudio_seek (demux_plugin_t *this_gen,
			     off_t start_pos, int start_time) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

	demux_mpgaudio_start (this_gen, this->video_fifo, this->audio_fifo,
			 start_pos, start_time);
}

static int demux_mpgaudio_open(demux_plugin_t *this_gen,
			       input_plugin_t *input, int stage) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  switch(stage) {
    
  case STAGE_BY_CONTENT: {
    uint32_t head;
    
    if(!input)
      return DEMUX_CANNOT_HANDLE;

    head = demux_mpgaudio_read_head(input);

	if (mpg123_head_check(head)) {
	  this->input = input;
	  return DEMUX_CAN_HANDLE;
	}
    return DEMUX_CANNOT_HANDLE;
  }
  break;
  
  case STAGE_BY_EXTENSION: {
    char *suffix;
    char *MRL;
    char *m, *valid_ends;

    MRL = input->get_mrl (input);
    
    suffix = strrchr(MRL, '.');
    
    if(!suffix)
      return DEMUX_CANNOT_HANDLE;
    
    xine_strdupa(valid_ends, (this->config->register_string(this->config,
							    "mrl.ends_mgaudio", VALID_ENDS,
							    "valid mrls ending for mpeg audio demuxer",
							    NULL, NULL, NULL)));
    while((m = xine_strsep(&valid_ends, ",")) != NULL) { 
      
      while(*m == ' ' || *m == '\t') m++;
      
      if(!strcasecmp((suffix + 1), m)) {
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

static char *demux_mpgaudio_get_id(void) {
  return "MPGAUDIO";
}

static char *demux_mpgaudio_get_mimetypes(void) {
  return "audio/mpeg2: mp2: MPEG audio;"
         "audio/x-mpeg2: mp2: MPEG audio;"
         "audio/mpeg3: mp3: MPEG audio;"
         "audio/x-mpeg3: mp3: MPEG audio;"
         "audio/mpeg: mpa,abs,mpega: MPEG audio;"
         "audio/x-mpeg: mpa,abs,mpega: MPEG audio;";
}

static void demux_mpgaudio_close (demux_plugin_t *this) {
  free (this);
}

static int demux_mpgaudio_get_stream_length (demux_plugin_t *this_gen) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  if( this->stream_length > 0 )
  {
    return this->stream_length;
  }
  else
  return 0;
}


demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {

  demux_mpgaudio_t *this;

  if (iface != 7) {
    printf ("demux_mpeg: plugin doesn't support plugin API version %d.\n"
	    "            this means there's a version mismatch between xine and this "
	    "            demuxer plugin.\nInstalling current demux plugins should help.\n",
	    iface);
    return NULL;
  }
  
  this         = xine_xmalloc (sizeof (demux_mpgaudio_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config,
					"mrl.ends_mgaudio", VALID_ENDS,
					"valid mrls ending for mpeg audio demuxer",
					NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUX_MPGAUDIO_IFACE_VERSION;
  this->demux_plugin.open              = demux_mpgaudio_open;
  this->demux_plugin.start             = demux_mpgaudio_start;
  this->demux_plugin.seek              = demux_mpgaudio_seek;
  this->demux_plugin.stop              = demux_mpgaudio_stop;
  this->demux_plugin.close             = demux_mpgaudio_close;
  this->demux_plugin.get_status        = demux_mpgaudio_get_status;
  this->demux_plugin.get_identifier    = demux_mpgaudio_get_id;
  this->demux_plugin.get_stream_length = demux_mpgaudio_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_mpgaudio_get_mimetypes;
  
  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );
  
  return &this->demux_plugin;
}
