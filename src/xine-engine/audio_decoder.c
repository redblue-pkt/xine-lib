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
 * $Id: audio_decoder.c,v 1.1 2001/04/18 22:36:01 f1rmb Exp $
 *
 *
 * functions that implement audio decoding
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "audio_decoder.h"

#define MAX_NUM_DECODERS 10

typedef struct ad_globals_s {

  pthread_t                  mAudioThread;

  fifo_buffer_t             *mBufAudio;

  audio_decoder_t           *mDecoders[MAX_NUM_DECODERS];
  audio_decoder_t           *mCurDecoder;

  uint32_t                   mnCurPos;

  ao_instance_t             *mAudioOut

  gui_status_callback_func_t gui_status_callback;

  int                        mbStreamFinished;

  pthread_mutex_t            mXineLock;

} ad_globals_t;

static ad_globals_t gAD;


void *audio_decoder_loop (void *dummy) {

  buf_element_t *pBuf;
  int bRunning = 1;

  while (bRunning) {

    pBuf = gAD.mBufAudio->fifo_buffer_get (gAD.mBufAudio);

    if (gAD.mAudioOut) {

      gAD.mnCurPos = pBuf->nInputPos;

      /*
      if (gXine.mnStatus == XINE_PLAY)
	gXine.mStatusCallback (gXine.mnStatus);
	*/

      switch (pBuf->nType) {

      case BUF_STREAMSTART:
	if (gAD.mCurDecoder) {
	  gAD.mCurDecoder->close ();
	  gAD.mCurDecoder = NULL;
	}

	pthread_mutex_lock (&gAD.mXineLock);
	gAD.mbStreamFinished = 0;
	pthread_mutex_unlock (&gAD.mXineLock);

      break;

      case BUF_AC3AUDIO:
      case BUF_MPEGAUDIO:
      case BUF_MSAUDIO:
      case BUF_LINEARPCM:
      
	decoder = gAD.mDecoders [pBuf->nType];

	if (decoder) {
	  if (gAD.mCurDecoder != decoder) {

	    if (gAD.mCurDecoder) 
	      gAD.mCurDecoder->close ();

	    gAD.mCurDecoder = decoder;
	    gAD.mCurDecoder->init (gAD.mVideoOut);

	  }
	
	  decoder->decode_data (pBuf);
	}

	break;

      case BUF_STREAMEND:
	if (gAD.mCurDecoder) {
	  gAD.mCurDecoder->close ();
	  gAD.mCurDecoder = NULL;
	}

	gAD.mbStreamFinished = 1;

	pthread_mutex_lock (&gAD.mXineLock);

	gVD.mbStreamFinished = 1;
      
	if (video_decoder_is_stream_finished ()) {
	  pthread_mutex_unlock (&gAD.mXineLock);
	  xine_notify_stream_finished ();
	} else
	  pthread_mutex_unlock (&gAD.mXineLock);

	break;

      case BUF_QUIT:
	if (gAD.mCurDecoder) {
	  gAD.mCurDecoder->close ();
	  gAD.mCurDecoder = NULL;
	}
	bRunning = 0;
	break;

      }
    }
    pBuf->free_buffer (pBuf);
  }

  return NULL;
}

int audio_decoder_is_stream_finished () {
  return gAD.mbStreamFinished ;
}

uint32_t audio_decoder_get_pos () {
  return gAD.mnCurPos;
}

fifo_buffer_t *audio_decoder_init (ao_instance_t *audio_out,
				   pthread_mutex_t xine_lock) {

  gAD.mAudioOut = audio_out;
  gAD.mXineLock = xine_lock;

  gAD.mCurDecoder = NULL;
  for (i=0; i<MAX_NUM_DECODERS; i++)
    gAD.mDecoders[i] = NULL;

  gAD.mDecoders[BUF_AC3AUDIO]  = init_audio_decoder_ac3dec ();
  gAD.mDecoders[BUF_MPEGAUDIO] = init_audio_decoder_mpg123 ();
  gAD.mDecoders[BUF_MSAUDIO]   = init_audio_decoder_msaudio ();
  gAD.mDecoders[BUF_LINEARPCM] = init_audio_decoder_linearpcm ();

  gAD.mBufAudio = fifo_buffer_new ();

  pthread_create (&gAD.mAudioThread, NULL, audio_decoder_loop, NULL) ;

  printf ("audio_decoder_init: audio thread created\n");

  return gAD.mBufAudio;
}

void audio_decoder_shutdown () {

  buf_element_t *pBuf;

  gAD.mBufAudio->fifo_buffer_clear(gAD.mBufAudio);

  pBuf = gAD.mBufAudio->buffer_pool_alloc ();
  pBuf->nType = BUF_QUIT;
  gAD.mBufAudio->fifo_buffer_put (gAD.mBufAudio, pBuf);

  pthread_join (gAD.mAudioThread, &p);
}


