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
 * $Id: demux_mpgaudio.c,v 1.33 2002/01/13 21:15:48 jcdutton Exp $
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

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_DEMUX, message, ##args);                 \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_DEMUX, message, ##args);                 \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_DEMUX, __VA_ARGS__);                     \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_DEMUX, __VA_ARGS__);                     \
    printf(__VA_ARGS__);                                             \
  }
#endif

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_t              *xine;

  config_values_t     *config;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  pthread_t            thread;

  int                  status;

  int                  send_end_buffers;

  int                  stream_length;
} demux_mpgaudio_t ;


int tabsel_123[2][3][16] = {
   { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,},
     {0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384,},
     {0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,} },

   { {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,} }
};

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
  double tpf, bitrate;

  if( newhead & (1<<20) ) {
    lsf = (newhead & (1<<19)) ? 0x0 : 0x1;
    mpeg25 = 0;
  }
  else {
    lsf = 1;
    mpeg25 = 1;
  }

  lay = 4-((newhead>>17)&3);

  if(mpeg25) {
    sampling_frequency = 6 + ((newhead>>10)&0x3);
  }
  else {
    sampling_frequency = ((newhead>>10)&0x3) + (lsf*3);
  }

  bitrate_index = ((newhead>>12)&0xf);
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

  bitrate = (double) framesize / tpf;
  LOG_MSG(this->xine, _("mpgaudio: bitrate = %.2fkbps\n"), bitrate/1024.0*8.0 );
  this->stream_length = (int)(this->input->get_length(this->input) / bitrate);
}

static int demux_mpgaudio_next (demux_mpgaudio_t *this) {

  buf_element_t *buf = NULL;
  uint32_t head;
  
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

  buf->PTS             = 0;
  buf->SCR             = 0;
  buf->input_pos       = this->input->get_current_pos(this->input);
  buf->input_time      = buf->input_pos * this->stream_length /
                         this->input->get_length(this->input);
  buf->type            = BUF_AUDIO_MPEG;
  buf->decoder_info[0] = 1;

  if(this->audio_fifo)
    this->audio_fifo->put(this->audio_fifo, buf);

  return (buf->size == 2048);
}

static void *demux_mpgaudio_loop (void *this_gen) {
  buf_element_t *buf;
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  this->send_end_buffers = 1;

  do {

    if (!demux_mpgaudio_next(this))
      this->status = DEMUX_FINISHED;

  } while (this->status == DEMUX_OK) ;

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_info[0] = 0; /* stream finished */
    this->video_fifo->put (this->video_fifo, buf);
    
    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type            = BUF_CONTROL_END;
      buf->decoder_info[0] = 0; /* stream finished */
      this->audio_fifo->put (this->audio_fifo, buf);
    }
  }

  pthread_exit(NULL);

  return NULL;
}

static void demux_mpgaudio_stop (demux_plugin_t *this_gen) {

  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;
  buf_element_t *buf;
  void *p;

  if (this->status != DEMUX_OK) {
    LOG_MSG(this->xine, _("demux_mpgaudio_block: stop...ignored\n"));
    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_cancel (this->thread);
  pthread_join (this->thread, &p);

  this->video_fifo->clear(this->video_fifo);
  if (this->audio_fifo)
    this->audio_fifo->clear(this->audio_fifo);

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type            = BUF_CONTROL_END;
  buf->decoder_info[0] = 1; /* forced */
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_info[0] = 1; /* forced */
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_mpgaudio_get_status (demux_plugin_t *this_gen) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  return this->status;
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

  this->video_fifo  = video_fifo;
  this->audio_fifo  = audio_fifo;
  
  this->status = DEMUX_OK;
  this->stream_length = 0;
  
  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    uint32_t head;

    head = demux_mpgaudio_read_head(this->input);

    if (mpg123_head_check(head))
       mpg123_decode_header(this,head);

    if (!start_pos && start_time && this->stream_length > 0)
         start_pos = start_time * this->input->get_length(this->input) /
                     this->stream_length;


    this->input->seek (this->input, start_pos, SEEK_SET);
  }
  
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

  if ((err = pthread_create (&this->thread,
			     NULL, demux_mpgaudio_loop, this)) != 0) {
    LOG_MSG_STDERR(this->xine, _("demux_mpgaudio: can't create new thread (%s)\n"),
		   strerror(err));
    exit (1);
  }
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
  /* nothing */
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

  if (iface != 6) {
    LOG_MSG(xine,
	    _("demux_mpeg: plugin doesn't support plugin API version %d.\n"
	      "            this means there's a version mismatch between xine and this "
	      "            demuxer plugin.\nInstalling current demux plugins should help.\n"),
	    iface);
    return NULL;
  }
  
  this         = malloc (sizeof (demux_mpgaudio_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config,
					"mrl.ends_mgaudio", VALID_ENDS,
					"valid mrls ending for mpeg audio demuxer",
					NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUX_MPGAUDIO_IFACE_VERSION;
  this->demux_plugin.open              = demux_mpgaudio_open;
  this->demux_plugin.start             = demux_mpgaudio_start;
  this->demux_plugin.stop              = demux_mpgaudio_stop;
  this->demux_plugin.close             = demux_mpgaudio_close;
  this->demux_plugin.get_status        = demux_mpgaudio_get_status;
  this->demux_plugin.get_identifier    = demux_mpgaudio_get_id;
  this->demux_plugin.get_stream_length = demux_mpgaudio_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_mpgaudio_get_mimetypes;
  
  return &this->demux_plugin;
}
