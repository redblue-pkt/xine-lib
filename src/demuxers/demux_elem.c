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
 * $Id: demux_elem.c,v 1.2 2001/04/19 09:46:57 f1rmb Exp $
 *
 * demultiplexer for elementary mpeg streams
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

#include "xine_internal.h"
#include "monitor.h"
#include "demux.h"

static uint32_t xine_debug;

typedef struct _demux_mpeg_elem_globals {
  fifo_buffer_t       *mBufVideo;
  fifo_buffer_t       *mBufAudio;

  input_plugin_t      *mInput;
  pthread_t            mThread;
  int                  mnBlocksize;

  int                  mnStatus;
} demux_mpeg_elem_globals_t ;

static demux_mpeg_elem_globals_t gDemuxMpegElem;
static fifobuf_functions_t *Ffb;

/*
 *
 */
static int demux_mpeg_elem_next (void) {

  buf_element_t *pBuf;

  pBuf = Ffb->buffer_pool_alloc ();

  pBuf->pContent  = pBuf->pMem;
  pBuf->nDTS      = 0;
  pBuf->nPTS      = 0;
  pBuf->nSize     = gDemuxMpegElem.mInput->read(pBuf->pMem, 
						gDemuxMpegElem.mnBlocksize);
  pBuf->nType     = BUF_MPEGELEMENT;
  pBuf->nInputPos = gDemuxMpegElem.mInput->seek (0, SEEK_CUR);

  Ffb->fifo_buffer_put (gDemuxMpegElem.mBufVideo, pBuf);

  return (pBuf->nSize==gDemuxMpegElem.mnBlocksize);
}

/*
 *
 */
static void *demux_mpeg_elem_loop (void *dummy) {
  buf_element_t *pBuf;

  do {

    if (!demux_mpeg_elem_next())
      gDemuxMpegElem.mnStatus = DEMUX_FINISHED;

  } while (gDemuxMpegElem.mnStatus == DEMUX_OK) ;

  xprintf (VERBOSE|DEMUX, "demux loop finished (status: %d)\n",
	   gDemuxMpegElem.mnStatus);

  pBuf = Ffb->buffer_pool_alloc ();
  pBuf->nType    = BUF_STREAMEND;
  Ffb->fifo_buffer_put (gDemuxMpegElem.mBufVideo, pBuf);

  pBuf = Ffb->buffer_pool_alloc ();
  pBuf->nType    = BUF_STREAMEND;
  Ffb->fifo_buffer_put (gDemuxMpegElem.mBufAudio, pBuf);

  return NULL;
}

/*
 *
 */
static void demux_mpeg_elem_stop (void) {
  void *p;

  gDemuxMpegElem.mnStatus = DEMUX_FINISHED;

  Ffb->fifo_buffer_clear(gDemuxMpegElem.mBufVideo);
  Ffb->fifo_buffer_clear(gDemuxMpegElem.mBufAudio);

  pthread_join (gDemuxMpegElem.mThread, &p);
}

/*
 *
 */
static int demux_mpeg_elem_get_status (void) {
  return gDemuxMpegElem.mnStatus;
}

/*
 *
 */
static void demux_mpeg_elem_start (input_plugin_t *input_plugin,
				    fifo_buffer_t *bufVideo, 
				    fifo_buffer_t *bufAudio,
				    fifo_buffer_t *bufSPU,
				    off_t pos) 
{
  buf_element_t *pBuf;

  gDemuxMpegElem.mInput       = input_plugin;
  gDemuxMpegElem.mBufVideo    = bufVideo;
  gDemuxMpegElem.mBufAudio    = bufAudio;

  gDemuxMpegElem.mnStatus     = DEMUX_OK;
  /*  
  if ((gDemuxMpegElem.mInput->get_capabilities() & INPUT_CAP_SEEKABLE) != 0 ) {
    xprintf (VERBOSE|DEMUX, "=>seek to %Ld\n",pos);
    
      gDemuxMpegElem.mInput->seek (pos, SEEK_SET);
  }
  else { */
  if((gDemuxMpegElem.mInput->get_capabilities() & INPUT_CAP_SEEKABLE) != 0)
    gDemuxMpegElem.mInput->seek (pos, SEEK_SET);
/*    } */
  
  gDemuxMpegElem.mnBlocksize = 2048;
  //  pos /= (off_t) gDemuxMpegElem.mnBlocksize;
  //  pos *= (off_t) gDemuxMpegElem.mnBlocksize;
  //  xprintf (VERBOSE|DEMUX, "=>seek to %Ld\n",pos);

  //  gDemuxMpegElem.mInput->seek (pos, SEEK_SET);

  /* 
   * send reset buffer
   */

  pBuf = Ffb->buffer_pool_alloc ();
  pBuf->nType    = BUF_RESET;
  Ffb->fifo_buffer_put (gDemuxMpegElem.mBufVideo, pBuf);

  pBuf = Ffb->buffer_pool_alloc ();
  pBuf->nType    = BUF_RESET;
  Ffb->fifo_buffer_put (gDemuxMpegElem.mBufAudio, pBuf);

  /*
   * now start demuxing
   */

  pthread_create (&gDemuxMpegElem.mThread, NULL, demux_mpeg_elem_loop, NULL) ;
}

/*
 *
 */
static void demux_mpeg_elem_select_audio_channel (int nChannel) {
}

/*
 *
 */
static void demux_mpeg_elem_select_spu_channel (int nChannel) {
}

/*
 *
 */
static int demux_mpeg_elem_open(input_plugin_t *ip, 
				const char *MRL, int stage) {

  switch(stage) {
    
  case STAGE_BY_CONTENT: {
    uint8_t buf[4096];
    int bs = 0;
    
    if(!ip)
      return DEMUX_CANNOT_HANDLE;
  
    if((ip->get_capabilities() & INPUT_CAP_SEEKABLE) != 0) {
      ip->seek(0, SEEK_SET);

      if(ip->get_blocksize)
	bs = ip->get_blocksize();
      
      bs = (bs > 4) ? bs : 4;

      if(ip->read(buf, bs)) {
	
	if(buf[0] || buf[1] || (buf[2] != 0x01))
	  return DEMUX_CANNOT_HANDLE;
	
	switch(buf[3]) {
	case 0xb3:
	  return DEMUX_CAN_HANDLE;
	  break;
	}
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;
  
  case STAGE_BY_EXTENSION: {
    char *suffix;
    
    suffix = strrchr(MRL, '.');
    xprintf(VERBOSE|DEMUX, "demux_pure_can_handle: suffix %s of %s\n", 
	    suffix, MRL);
    
    if(suffix) {
      if(!strcasecmp(suffix, ".mpv"))
	return DEMUX_CAN_HANDLE;
    }

    return DEMUX_CANNOT_HANDLE;
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
static char *demux_mpeg_elem_get_id(void) {
  return "MPEG_ELEM";
}

/*
 *
 */
static demux_functions_t demux_mpeg_elem_functions = {
  NULL,
  NULL,
  demux_mpeg_elem_open,
  demux_mpeg_elem_start,
  demux_mpeg_elem_stop,
  demux_mpeg_elem_get_status,
  demux_mpeg_elem_select_audio_channel,
  demux_mpeg_elem_select_spu_channel,
  demux_mpeg_elem_get_id
};

/*
 *
 */
demux_functions_t *init_demux_mpeg_elem(fifobuf_functions_t *f, uint32_t xd) {

  Ffb = f;
  xine_debug = xd;
  return &demux_mpeg_elem_functions;
}
