/* 
 * Copyright (C) 2000 the xine project
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
 * $Id: demux_mpgaudio.c,v 1.8 2001/05/30 02:09:24 f1rmb Exp $
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
#include "monitor.h"
#include "demux.h"

#include "libmpg123/mpg123.h"
#include "libmpg123/mpglib.h"

#define DEMUX_MPGAUDIO_IFACE_VERSION 1

typedef struct {

  demux_plugin_t       demux_plugin;

  fifo_buffer_t       *audio_fifo;
  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  pthread_t            thread;

  int                  status;
} demux_mpgaudio_t ;

static uint32_t xine_debug;

/* 
 * *********************************************************************** 
 * Adds some (very slightly hacked) parts of libmpg123 here: 
 *    I don't want to link the lib to this demuxer.
 */
static int ssize;
static int grp_3tab[32 * 3]   = {0,};
static int grp_5tab[128 * 3]  = {0,};
static int grp_9tab[1024 * 3] = {0,};
static real mpg123_muls[27][64];
static int tabsel_123[2][3][16] = {
  {
    {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,}, 
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,},
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,}}, 
  
  { 
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256,},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,}}
};
static long mpg123_freqs[9] = {
  44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000
};
/*
 *
 */
static void mpg123_init_layer2(void) {
  static double mulmul[27] = {
    0.0, -2.0 / 3.0, 2.0 / 3.0,
    2.0 / 7.0, 2.0 / 15.0, 2.0 / 31.0, 2.0 / 63.0, 2.0 / 127.0, 2.0 / 255.0,
    2.0 / 511.0, 2.0 / 1023.0, 2.0 / 2047.0, 2.0 / 4095.0, 2.0 / 8191.0,
    2.0 / 16383.0, 2.0 / 32767.0, 2.0 / 65535.0,
    -4.0 / 5.0, -2.0 / 5.0, 2.0 / 5.0, 4.0 / 5.0,
    -8.0 / 9.0, -4.0 / 9.0, -2.0 / 9.0, 2.0 / 9.0, 4.0 / 9.0, 8.0 / 9.0
  };
  static int base[3][9] = {
    {1, 0, 2,},
    {17, 18, 0, 19, 20,},
    {21, 1, 22, 23, 0, 24, 25, 2, 26}
  };
  int i, j, k, l, len;
  real *table;
  static int tablen[3] = { 3, 5, 9 };
  static int *itable, *tables[3] = { grp_3tab, grp_5tab, grp_9tab };
  
  for (i = 0; i < 3; i++) {
    itable = tables[i];
    len = tablen[i];
    for (j = 0; j < len; j++)
      for (k = 0; k < len; k++)
	for (l = 0; l < len; l++) {
	  *itable++ = base[i][l];
	  *itable++ = base[i][k];
	  *itable++ = base[i][j];
	}
  }
  
  for (k = 0; k < 27; k++) {
    double m = mulmul[k];
    
    table = mpg123_muls[k];
    for (j = 3, i = 0; i < 63; i++, j--)
      *table++ = m * pow(2.0, (double) j / 3.0);
    *table++ = 0.0;
  }
}
/*
 *
 */
static int mpg123_decode_header(struct frame *fr, unsigned long newhead) {
  if (newhead & (1 << 20)) {
    fr->lsf = (newhead & (1 << 19)) ? 0x0 : 0x1;
    fr->mpeg25 = 0;
  }
  else {
    fr->lsf = 1;
    fr->mpeg25 = 1;
  }
  fr->lay = 4 - ((newhead >> 17) & 3);
  if (fr->mpeg25) {
    fr->sampling_frequency = 6 + ((newhead >> 10) & 0x3);
  }
  else
    fr->sampling_frequency = ((newhead >> 10) & 0x3) + (fr->lsf * 3);

  fr->error_protection = ((newhead >> 16) & 0x1) ^ 0x1;
  
  if (fr->mpeg25)
    fr->bitrate_index = ((newhead >> 12) & 0xf);
  
  fr->bitrate_index = ((newhead >> 12) & 0xf);
  fr->padding = ((newhead >> 9) & 0x1);
  fr->extension = ((newhead >> 8) & 0x1);
  fr->mode = ((newhead >> 6) & 0x3);
  fr->mode_ext = ((newhead >> 4) & 0x3);
  fr->copyright = ((newhead >> 3) & 0x1);
  fr->original = ((newhead >> 2) & 0x1);
  fr->emphasis = newhead & 0x3;
  
  fr->stereo = (fr->mode == MPG_MD_MONO) ? 1 : 2;
  
  ssize = 0;
  
  if (!fr->bitrate_index)
    return (0);
  
  switch (fr->lay) {
  case 1:
    mpg123_init_layer2();
    fr->framesize = (long) tabsel_123[fr->lsf][0][fr->bitrate_index] * 12000;
    fr->framesize /= mpg123_freqs[fr->sampling_frequency];
    fr->framesize = ((fr->framesize + fr->padding) << 2) - 4;
    break;
  case 2:
    mpg123_init_layer2();
    fr->framesize = (long) tabsel_123[fr->lsf][1][fr->bitrate_index] * 144000;
    fr->framesize /= mpg123_freqs[fr->sampling_frequency];
    fr->framesize += fr->padding - 4;
    break;
  case 3:
    if (fr->lsf)
      ssize = (fr->stereo == 1) ? 9 : 17;
    else
      ssize = (fr->stereo == 1) ? 17 : 32;
    if (fr->error_protection)
      ssize += 2;
    fr->framesize = (long) tabsel_123[fr->lsf][2][fr->bitrate_index] * 144000;
    fr->framesize /= mpg123_freqs[fr->sampling_frequency] << (fr->lsf);
    fr->framesize = fr->framesize + fr->padding - 4;
    break;
  default:
    return (0);
  }
  if(fr->framesize > MAXFRAMESIZE)
    return 0;
  return 1;
}
/*
 *
 */
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
/* 
 * End of libmpg123 adds.
 ************************************************************************
 */

static int demux_mpgaudio_next (demux_mpgaudio_t *this) {

  buf_element_t *buf = NULL;
  
  if(this->audio_fifo)
    buf = this->input->read_block(this->input, 
				  this->audio_fifo, 2048);

  if (buf == NULL) {
    this->status = DEMUX_FINISHED;
    return 0;
  }

  buf->DTS       = 0;
  buf->PTS       = 0;
  buf->input_pos = this->input->get_current_pos(this->input);
  buf->type      = BUF_AUDIO_AVI;

  if(this->audio_fifo)
    this->audio_fifo->put(this->audio_fifo, buf);

  return (buf->size == 2048);
}

static void *demux_mpgaudio_loop (void *this_gen) {
  buf_element_t *buf;
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  do {

    if (!demux_mpgaudio_next(this))
      this->status = DEMUX_FINISHED;

  } while (this->status == DEMUX_OK) ;

  xprintf (VERBOSE|DEMUX, "demux loop finished (status: %d)\n", this->status);

  this->status = DEMUX_FINISHED;

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type    = BUF_CONTROL_END;
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type    = BUF_CONTROL_END;
    this->audio_fifo->put (this->audio_fifo, buf);
  }

  pthread_exit(NULL);
}

static void demux_mpgaudio_stop (demux_plugin_t *this_gen) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;
  void *p;

  this->status = DEMUX_FINISHED;

  pthread_join (this->thread, &p);
}

static int demux_mpgaudio_get_status (demux_plugin_t *this_gen) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  return this->status;
}

static void demux_mpgaudio_start (demux_plugin_t *this_gen,
				 fifo_buffer_t *video_fifo, 
				 fifo_buffer_t *audio_fifo,
				 fifo_buffer_t *spu_fifo,
				 off_t pos) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = video_fifo;
  this->audio_fifo  = audio_fifo;
  
  this->status = DEMUX_OK;
  
  if((this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) != 0) {
    xprintf (VERBOSE|DEMUX, "=>seek to %Ld\n",pos);
    this->input->seek (this->input, pos, SEEK_SET);
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

  pthread_create (&this->thread, NULL, demux_mpgaudio_loop, this) ;
}

static int demux_mpgaudio_open(demux_plugin_t *this_gen,
			       input_plugin_t *input, int stage) {
  demux_mpgaudio_t *this = (demux_mpgaudio_t *) this_gen;

  switch(stage) {
    
  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];
    uint8_t *pbuf;
    struct frame fr;
    uint32_t head;
    int in_buf, i;
    int bs = 0;
    
    if(!input)
      return DEMUX_CANNOT_HANDLE;

    if((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0) {
      input->seek(input, 0, SEEK_SET);

      if(input->get_blocksize)
	bs = input->get_blocksize(input);
      
      if(bs > 4) 
	return DEMUX_CANNOT_HANDLE;

      if(!bs) 
	bs = 4;

      if(input->read(input, buf, bs)) {

	/* Not an AVI ?? */
	if(buf[0] || buf[1] || (buf[2] != 0x01) || (buf[3] != 0x46)) {

	  pbuf = (uint8_t *) malloc(1024);
	  head = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
	  
	  while(!mpg123_head_check(head)) {
	    
	    in_buf = input->read(input, pbuf, 1024);
	    
	    if(in_buf == 0) {
	      free(pbuf);
	      return DEMUX_CANNOT_HANDLE;
	    }
	    
	    for(i = 0; i < in_buf; i++) {
	      head <<= 8;
	      head |= pbuf[i];
	      
	      if(mpg123_head_check(head)) {
		input->seek(input, i+1-in_buf, SEEK_CUR);
		break;
	      }
	    }
	  }
	  free(pbuf);
	  
	  if(mpg123_decode_header(&fr, head)) {
	    
	    if((input->seek(input, fr.framesize, SEEK_CUR)) <= 0)
	      return DEMUX_CANNOT_HANDLE;
	    
	    if((input->read(input, buf, 4)) != 4)
	      return DEMUX_CANNOT_HANDLE;
	  }
	  
	  head = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
	  
	  if(mpg123_head_check(head) && 
	     (((head >> 8) & 0x1) == 0x0) && (((head >> 6) & 0x3) == 0x1)) {
	    this->input = input;
	    return DEMUX_CAN_HANDLE;
	  }
	}
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;
  
  case STAGE_BY_EXTENSION: {
    char *suffix;
    char *MRL;
    
    MRL = input->get_mrl (input);
    
    suffix = strrchr(MRL, '.');
    xprintf(VERBOSE|DEMUX, "%s: suffix %s of %s\n", __FUNCTION__, suffix, MRL);
    
    if(!suffix)
      return DEMUX_CANNOT_HANDLE;
    
    if(!strcasecmp(suffix, ".mp3") 
       || (!strcasecmp(suffix, ".mp2"))) {
      this->input = input;
      return DEMUX_CAN_HANDLE;
    }
  }
  break;
  
  default:
    return DEMUX_CANNOT_HANDLE;
    break;
  }
  
  return DEMUX_CANNOT_HANDLE;
}

/*
 *
 */
static char *demux_mpgaudio_get_id(void) {
  return "MPGAUDIO";
}

static void demux_mpgaudio_close (demux_plugin_t *this) {
  /* nothing */
}

demux_plugin_t *init_demuxer_plugin(int iface, config_values_t *config) {
  demux_mpgaudio_t *this = malloc (sizeof (demux_mpgaudio_t));

  xine_debug  = config->lookup_int (config, "xine_debug", 0);

  switch (iface) {

  case 1:

    this->demux_plugin.interface_version = DEMUX_MPGAUDIO_IFACE_VERSION;
    this->demux_plugin.open              = demux_mpgaudio_open;
    this->demux_plugin.start             = demux_mpgaudio_start;
    this->demux_plugin.stop              = demux_mpgaudio_stop;
    this->demux_plugin.close             = demux_mpgaudio_close;
    this->demux_plugin.get_status        = demux_mpgaudio_get_status;
    this->demux_plugin.get_identifier    = demux_mpgaudio_get_id;
    
    return &this->demux_plugin;
    break;

  default:
    fprintf(stderr,
	    "Demuxer plugin doesn't support plugin API version %d.\n"
	    "PLUGIN DISABLED.\n"
	    "This means there's a version mismatch between xine and this "
	    "demuxer plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }
}
