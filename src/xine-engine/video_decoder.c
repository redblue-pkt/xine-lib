/* 
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: video_decoder.c,v 1.2 2001/04/19 09:46:57 f1rmb Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xine_internal.h"
#include "video_out/video_out.h"
#include "video_decoder.h"

#define MAX_NUM_DECODERS 10

typedef struct vd_globals_s {

  pthread_t                  mVideoThread;

  fifo_buffer_t             *mBufVideo;

  video_decoder_t           *mDecoders[MAX_NUM_DECODERS];
  video_decoder_t           *mCurDecoder;

  uint32_t                   mnCurInputPos;

  vo_instance_t             *mVideoOut;

  gui_status_callback_func_t gui_status_callback;

  int                        mbStreamFinished;

  pthread_mutex_t            mXineLock;

} vd_globals_t;

static vd_globals_t gVD;

void *video_decoder_loop () {

  buf_element_t *pBuf;
  int bRunning = 1;

  while (bRunning) {

    pBuf = gVD.mBufVideo->fifo_buffer_get (gVD.mBufVideo);

    gVD.mnCurInputPos = pBuf->nInputPos;

    switch (pBuf->nType) {
    case BUF_STREAMSTART:
      if (gVD.mCurDecoder) {
	gVD.mCurDecoder->close ();
	gVD.mCurDecoder = NULL;
      }

      pthread_mutex_lock (&gVD.mXineLock);
      gVD.mbStreamFinished = 0;
      pthread_mutex_unlock (&gVD.mXineLock);

      break;

    case BUF_MPEGVIDEO:
    case BUF_AVIVIDEO:
      
      decoder = gVD.mDecoders [pBuf->nType];

      if (decoder) {
	if (gVD.mCurDecoder != decoder) {

	  if (gVD.mCurDecoder) 
	    gVD.mCurDecoder->close ();

	  gVD.mCurDecoder = decoder;
	  gVD.mCurDecoder->init (gVD.mVideoOut);

	}
	
	decoder->decode_data (pBuf);
      }

      break;

    case BUF_STREAMEND:
      if (gVD.mCurDecoder) {
	gVD.mCurDecoder->close ();
	gVD.mCurDecoder = NULL;
      }

      gVD.mbStreamFinished = 1;

      pthread_mutex_lock (&gVD.mXineLock);

      gVD.mbVideoFinished = 1;
      
      if (audio_decoder_is_stream_finished ()) {
	pthread_mutex_unlock (&gVD.mXineLock);
	xine_notify_stream_finished ();
      } else
	pthread_mutex_unlock (&gVD.mXineLock);

      break;

    case BUF_QUIT:
      if (gVD.mCurDecoder) {
	gVD.mCurDecoder->close ();
	gVD.mCurDecoder = NULL;
      }
      bRunning = 0;
      break;

    }

    pBuf->free_buffer (pBuf);
  }

  return NULL;
}

int video_decoder_is_stream_finished () {
  return gVD.mbStreamFinished ;
}

uint32_t video_decoder_get_pos () {
  return gVD.mnCurPos;
}

fifo_buffer_t *video_decoder_init (vo_instance_t *video_out,
				   pthread_mutex_t xine_lock) {

  gVD.mVideoOut = video_out;
  gVD.mXineLock = xine_lock;

  gVD.mCurDecoder = NULL;
  for (i=0; i<MAX_NUM_DECODERS; i++)
    gVD.mDecoders[i] = NULL;

  gVD.mDecoders[BUF_MPEGVIDEO] = init_video_decoder_mpeg2dec ();
  gVD.mDecoders[BUF_AVIVIDEO]  = init_video_decoder_avi ();

  gVD.mBufVideo = fifo_buffer_new ();

  pthread_create (&gVD.mVideoThread, NULL, video_decoder_loop, NULL) ;

  printf ("video_decoder_init: video thread created\n");

  return gVD.mBufVideo;
}

void video_decoder_shutdown () {

  buf_element_t *pBuf;

  gVD.mBufVideo->fifo_buffer_clear(gVD.mBufVideo);

  pBuf = gVD.mBufVideo->buffer_pool_alloc ();
  pBuf->nType = BUF_QUIT;
  gVD.mBufVideo->fifo_buffer_put (gVD.mBufVideo, pBuf);

  pthread_join (gVD.mVideoThread, &p);
}
