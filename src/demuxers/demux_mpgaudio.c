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
 * $Id: demux_mpgaudio.c,v 1.2 2001/04/19 09:46:57 f1rmb Exp $
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

/* The following variable indicates the kind of error */

static uint32_t xine_debug;

typedef struct _demux_mpgaudio_globals {
  fifo_buffer_t       *mBufAudio;
  fifo_buffer_t       *mBufVideo;

  input_plugin_t      *mInput;

  pthread_t            mThread;

  int                  mnStatus;
} demux_mpgaudio_globals_t ;

static demux_mpgaudio_globals_t gDemuxMpgAudio;
static fifobuf_functions_t *Ffb;

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
  
  if (fr->mpeg25)		/* allow Bitrate change for 2.5 ... */
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
    mpg123_init_layer2();	/* inits also shared tables with layer1 */
    fr->framesize = (long) tabsel_123[fr->lsf][0][fr->bitrate_index] * 12000;
    fr->framesize /= mpg123_freqs[fr->sampling_frequency];
    fr->framesize = ((fr->framesize + fr->padding) << 2) - 4;
    break;
  case 2:
    mpg123_init_layer2();	/* inits also shared tables with layer1 */
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

int demux_mpgaudio_next (void) {

  buf_element_t *pBuf;

  pBuf = Ffb->buffer_pool_alloc ();

  pBuf->pContent  = pBuf->pMem;
  pBuf->nDTS      = 0 ; /* FIXME ? */
  pBuf->nPTS      = 0 ; /* FIXME ? */
  pBuf->nSize     = gDemuxMpgAudio.mInput->read (pBuf->pMem, 2048) ;
  pBuf->nType     = BUF_MPEGAUDIO; /* FIXME */
  pBuf->nInputPos = gDemuxMpgAudio.mInput->seek (0, SEEK_CUR);

  Ffb->fifo_buffer_put (gDemuxMpgAudio.mBufAudio, pBuf);

  return (pBuf->nSize==2048);
}

static void *demux_mpgaudio_loop (void *dummy) {

  buf_element_t *pBuf;

  do {
    if (!demux_mpgaudio_next())
      gDemuxMpgAudio.mnStatus = DEMUX_FINISHED;

  } while (gDemuxMpgAudio.mnStatus == DEMUX_OK) ;

  xprintf (VERBOSE|DEMUX, "mpgaudio demux loop finished (status: %d)\n",
	  gDemuxMpgAudio.mnStatus);

  pBuf = Ffb->buffer_pool_alloc ();
  pBuf->nType    = BUF_STREAMEND;
  Ffb->fifo_buffer_put (gDemuxMpgAudio.mBufVideo, pBuf);
  pBuf = Ffb->buffer_pool_alloc ();
  pBuf->nType    = BUF_STREAMEND;
  Ffb->fifo_buffer_put (gDemuxMpgAudio.mBufAudio, pBuf);

  return NULL;
}

static void demux_mpgaudio_stop (void) {
  void *p;

  gDemuxMpgAudio.mnStatus = DEMUX_FINISHED;
  
  Ffb->fifo_buffer_clear(gDemuxMpgAudio.mBufVideo);
  Ffb->fifo_buffer_clear(gDemuxMpgAudio.mBufAudio);

  pthread_join (gDemuxMpgAudio.mThread, &p);
}

static int demux_mpgaudio_get_status (void) {
  return gDemuxMpgAudio.mnStatus;
}

static void demux_mpgaudio_start (input_plugin_t *input_plugin,
				  fifo_buffer_t *bufVideo, 
				  fifo_buffer_t *bufAudio,
				  fifo_buffer_t *bufSPU, off_t pos) 
{
  buf_element_t *pBuf;

  gDemuxMpgAudio.mInput       = input_plugin;
  gDemuxMpgAudio.mBufVideo    = bufVideo;
  gDemuxMpgAudio.mBufAudio    = bufAudio;

  gDemuxMpgAudio.mnStatus     = DEMUX_OK;

  if((gDemuxMpgAudio.mInput->get_capabilities() & INPUT_CAP_SEEKABLE) != 0)
    gDemuxMpgAudio.mInput->seek (pos, SEEK_SET);

  pBuf = Ffb->buffer_pool_alloc ();
  pBuf->nType    = BUF_RESET;
  Ffb->fifo_buffer_put (gDemuxMpgAudio.mBufVideo, pBuf);
  pBuf = Ffb->buffer_pool_alloc ();
  pBuf->nType    = BUF_RESET;
  Ffb->fifo_buffer_put (gDemuxMpgAudio.mBufAudio, pBuf);

  pthread_create (&gDemuxMpgAudio.mThread, NULL, demux_mpgaudio_loop, NULL) ;
}

static void demux_mpgaudio_select_audio_channel (int nChannel) {
}

static void demux_mpgaudio_select_spu_channel (int nChannel) {
}

static int demux_mpgaudio_open(input_plugin_t *ip, 
			       const char *MRL, int stage) {
  
  switch(stage) {
    
  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];
    uint8_t *pbuf;
    struct frame fr;
    uint32_t head;
    int in_buf, i;
    int bs = 0;
    
    if(!ip)
      return DEMUX_CANNOT_HANDLE;

    if((ip->get_capabilities() & INPUT_CAP_SEEKABLE) != 0) {
      ip->seek(0, SEEK_SET);
      
      if(ip->get_blocksize)
	bs = ip->get_blocksize();
      
      if(bs > 4) 
	return DEMUX_CANNOT_HANDLE;

      if(!bs) 
	bs = 4;

      if(ip->read(buf, bs)) {

	/* Not an AVI ?? */
	if(buf[0] || buf[1] || (buf[2] != 0x01) || (buf[3] != 0x46)) {

	  pbuf = (uint8_t *) malloc(1024);
	  head = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
	  
	  while(!mpg123_head_check(head)) {
	    
	    in_buf = ip->read(pbuf, 1024);
	    
	    if(in_buf == 0) {
	      free(pbuf);
	      return DEMUX_CANNOT_HANDLE;
	    }
	    
	    for(i = 0; i < in_buf; i++) {
	      head <<= 8;
	      head |= pbuf[i];
	      
	      if(mpg123_head_check(head)) {
		ip->seek(i+1-in_buf, SEEK_CUR);
		break;
	      }
	    }
	  }
	  free(pbuf);
	  
	  if(decode_header(&fr, head)) {
	    
	    if((ip->seek(fr.framesize, SEEK_CUR)) <= 0)
	      return DEMUX_CANNOT_HANDLE;
	    
	    if((ip->read(buf, 4)) != 4)
	      return DEMUX_CANNOT_HANDLE;
	  }
	  
	  head = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
	  
	  if(mpg123_head_check(head) && 
	     (((head >> 8) & 0x1) == 0x0) && (((head >> 6) & 0x3) == 0x1))
	    return DEMUX_CAN_HANDLE;
	}
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;
  
  case STAGE_BY_EXTENSION: {
    char *suffix;
    
    suffix = strrchr(MRL, '.');
    xprintf(VERBOSE|DEMUX, "demux_mpgaudio_can_handle: suffix %s of %s\n", 
	    suffix, MRL);
    
    if(!suffix)
      return DEMUX_CANNOT_HANDLE;
    
    if(!strcasecmp(suffix, ".mp3") 
       || (!strcasecmp(suffix, ".mp2"))) {
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

static char *demux_mpgaudio_get_id(void) {
  return "MPEGAUDIO";
}

static demux_functions_t demux_mpgaudio_functions = {
  NULL,
  NULL,
  demux_mpgaudio_open,
  demux_mpgaudio_start,
  demux_mpgaudio_stop,
  demux_mpgaudio_get_status,
  demux_mpgaudio_select_audio_channel,
  demux_mpgaudio_select_spu_channel,
  demux_mpgaudio_get_id
};

demux_functions_t *init_demux_mpeg_audio(fifobuf_functions_t *f, uint32_t xd) {
  
  Ffb = f;
  xine_debug = xd;
  return &demux_mpgaudio_functions;
}
