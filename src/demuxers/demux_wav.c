/*
 * Copyright (C) 2001-2002 the xine project
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
 * MS WAV File Demuxer by Mike Melanson (melanson@pcisys.net)
 * based on WAV specs that are available far and wide
 *
 * $Id: demux_wav.c,v 1.9 2002/09/04 23:31:08 guenter Exp $
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
#include <ctype.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "buffer.h"
#include "bswap.h"

#define LE_16(x) (le2me_16(*(uint16_t *)(x)))
#define LE_32(x) (le2me_32(*(uint32_t *)(x)))

#define VALID_ENDS "wav"
#define WAV_SIGNATURE_SIZE 16
/* this is the hex value for 'data' */
#define data_TAG 0x61746164
#define PCM_BLOCK_ALIGN 1024

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_t              *xine;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;
  int                  send_end_buffers;
  int                  status;

  xine_waveformatex    *wave;
  int                  wave_size;
  unsigned int         audio_type;

  off_t                data_start;
  off_t                data_size;
  off_t                data_end;

  int                  seek_flag;  /* this is set when a seek just occurred */
} demux_wav_t;

static void *demux_wav_loop (void *this_gen) {

  demux_wav_t *this = (demux_wav_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int remaining_sample_bytes;
  off_t current_file_pos;
  int64_t current_pts;

  pthread_mutex_lock( &this->mutex );
  this->seek_flag = 1;

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      pthread_mutex_lock( &this->mutex );

      /* just load data chunks from wherever the stream happens to be
       * pointing; issue a DEMUX_FINISHED status is EOF is reached */
      remaining_sample_bytes = this->wave->nBlockAlign;
      current_file_pos = this->input->get_current_pos(this->input);

      current_pts = current_file_pos;
      current_pts -= this->data_start;
      current_pts *= 90000;
      current_pts /= this->wave->nAvgBytesPerSec;

      if (this->seek_flag) {
        xine_demux_control_newpts(this->xine, current_pts, 0);
        this->seek_flag = 0;
      }

      while (remaining_sample_bytes) {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = this->audio_type;
        buf->input_pos = current_file_pos;
        buf->input_length = this->data_end;
        buf->input_time = current_pts / 90000;
        buf->pts = current_pts;

        if (remaining_sample_bytes > buf->max_size)
          buf->size = buf->max_size;
        else
          buf->size = remaining_sample_bytes;
        remaining_sample_bytes -= buf->size;

        if (this->input->read(this->input, buf->content, buf->size) !=
          buf->size) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        if (!remaining_sample_bytes)
          buf->decoder_flags |= BUF_FLAG_FRAME_END;

        this->audio_fifo->put (this->audio_fifo, buf);
      }
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->audio_fifo->size(this->audio_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while (this->status == DEMUX_OK);

  printf ("demux_wav: demux loop finished (status: %d)\n",
          this->status);

  /* seek back to the beginning of the data in preparation for another
   * start */
  this->input->seek(this->input, this->data_start, SEEK_SET);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock(&this->mutex);
  return NULL;
}

static int demux_wav_open(demux_plugin_t *this_gen,
                         input_plugin_t *input, int stage) {

  demux_wav_t *this = (demux_wav_t *) this_gen;
  unsigned char signature[WAV_SIGNATURE_SIZE];

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, signature, WAV_SIGNATURE_SIZE) !=
      WAV_SIGNATURE_SIZE)
      return DEMUX_CANNOT_HANDLE;

    /* check the signature */
    if ((signature[0] == 'R') &&
        (signature[1] == 'I') &&
        (signature[2] == 'F') &&
        (signature[3] == 'F') &&
        (signature[8] == 'W') &&
        (signature[9] == 'A') &&
        (signature[10] == 'V') &&
        (signature[11] == 'E') &&
        (signature[12] == 'f') &&
        (signature[13] == 'm') &&
        (signature[14] == 't') &&
        (signature[15] == ' '))
      return DEMUX_CAN_HANDLE;

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
                                                            "mrl.ends_wav", VALID_ENDS,
                                                            _("valid mrls ending for wav demuxer"),
                                                            NULL, 20, NULL, NULL)));    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

      while(*m == ' ' || *m == '\t') m++;

      if(!strcasecmp((suffix + 1), m)) {
        this->input = input;
        return DEMUX_CAN_HANDLE;
      }
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

static int demux_wav_start (demux_plugin_t *this_gen,
                            fifo_buffer_t *video_fifo,
                            fifo_buffer_t *audio_fifo,
                            off_t start_pos, int start_time) {

  demux_wav_t *this = (demux_wav_t *) this_gen;
  buf_element_t *buf;
  int err;
  unsigned int chunk_tag;
  unsigned int chunk_size;
  unsigned char chunk_preamble[8];
  int status;

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {
    this->video_fifo = video_fifo;
    this->audio_fifo = audio_fifo;

    /* go straight for the format structure */
    this->input->seek(this->input, WAV_SIGNATURE_SIZE, SEEK_SET);
    if (this->input->read(this->input,
      (unsigned char *)&this->wave_size, 4) != 4) {
      status = this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return status;
    }
    this->wave_size = le2me_32(this->wave_size);
    this->wave = (xine_waveformatex *) malloc( this->wave_size );
    
    if (this->input->read(this->input, (void *)this->wave, this->wave_size) !=
      this->wave_size) {
      status = this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return status;
    }
    xine_waveformatex_le2me(this->wave);
    this->audio_type = formattag_to_buf_audio(this->wave->wFormatTag);
    if(!this->audio_type) {
      xine_report_codec( this->xine, XINE_CODEC_AUDIO, this->audio_type, 0, 0);
      status = this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return status;
    }

    /* traverse through the chunks to find the 'data' chunk */
    this->data_start = this->data_size = this->data_end = 0;
    while (this->data_start == 0) {

      if (this->input->read(this->input, chunk_preamble, 8) != 8) {
        status = this->status = DEMUX_FINISHED;
        pthread_mutex_unlock(&this->mutex);
        return status;
      }
      chunk_tag = LE_32(&chunk_preamble[0]);      
      chunk_size = LE_32(&chunk_preamble[4]);

      if (chunk_tag == data_TAG) {
        this->data_start = this->input->get_current_pos(this->input);
        this->data_size = chunk_size;
        this->data_end = this->data_start + chunk_size;
      } else {
        this->input->seek(this->input, chunk_size, SEEK_CUR);
      }
    }

    /* print vital stats */
    xine_log(this->xine, XINE_LOG_FORMAT,
      _("demux_wav: format 0x%X audio, %d Hz, %d bits/sample, %d %s\n"),
      this->wave->wFormatTag,
      this->wave->nSamplesPerSec,
      this->wave->wBitsPerSample,
      this->wave->nChannels,
      ngettext("channel", "channels", this->wave->nChannels));
    xine_log(this->xine, XINE_LOG_FORMAT,
      _("demux_wav: running time = %lld min, %lld sec\n"),
      this->data_size / this->wave->nAvgBytesPerSec / 60,
      this->data_size / this->wave->nAvgBytesPerSec % 60);
    xine_log(this->xine, XINE_LOG_FORMAT,
      _("demux_wav: average bytes/sec = %d, block alignment = %d\n"),
      this->wave->nAvgBytesPerSec,
      this->wave->nBlockAlign);

    /* special block alignment hack so that the demuxer doesn't send
     * packets with individual PCM samples */
    if ((this->wave->nAvgBytesPerSec / this->wave->nBlockAlign) ==
      this->wave->nSamplesPerSec)
      this->wave->nBlockAlign = PCM_BLOCK_ALIGN;

    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send init info to decoders */
    if (this->audio_fifo && this->audio_type) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->audio_type;
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = this->wave->nSamplesPerSec;
      buf->decoder_info[2] = this->wave->wBitsPerSample;
      buf->decoder_info[3] = this->wave->nChannels;
      buf->content = (void *)this->wave;
      buf->size = this->wave_size;
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_wav_loop, this)) != 0) {
      printf ("demux_wav: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_wav_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  demux_wav_t *this = (demux_wav_t *) this_gen;
  int status;
  off_t data_offset;

  pthread_mutex_lock(&this->mutex);

  /* check the boundary offsets */
  if (start_pos < this->data_start)
    this->input->seek(this->input, this->data_start, SEEK_SET);
  else if (start_pos >= this->data_end) {
    this->status = DEMUX_FINISHED;
    status = this->status;
    pthread_mutex_unlock(&this->mutex);
    return status;
  } else {
    /* This function must seek along the block alignment. Determine how
     * far into the data the requested offset lies, divide the diff
     * by the block alignment integer-wise, and multiply that by the
     * block alignment to get the new aligned offset. */
    data_offset = start_pos - this->data_start;
    data_offset /= this->wave->nBlockAlign;
    data_offset *= this->wave->nBlockAlign;
    data_offset += this->data_start;

    this->input->seek(this->input, data_offset, SEEK_SET);
  }

  this->seek_flag = 1;
  status = this->status = DEMUX_OK;
  xine_demux_flush_engine (this->xine);
  pthread_mutex_unlock(&this->mutex);

  return status;
}

static void demux_wav_stop (demux_plugin_t *this_gen) {

  demux_wav_t *this = (demux_wav_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );

  if (!this->thread_running) {
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  /* seek back to the beginning of the data in preparation for another
   * start */
  this->input->seek(this->input, this->data_start, SEEK_SET);

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_demux_flush_engine(this->xine);

  xine_demux_control_end(this->xine, BUF_FLAG_END_USER);
}

static void demux_wav_close (demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  pthread_mutex_destroy (&this->mutex);
  free(this);
}

static int demux_wav_get_status (demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  return this->status;
}

static char *demux_wav_get_id(void) {
  return "WAV";
}

static char *demux_wav_get_mimetypes(void) {
  return NULL;
}

/* return the approximate length in seconds */
static int demux_wav_get_stream_length (demux_plugin_t *this_gen) {

  demux_wav_t *this = (demux_wav_t *) this_gen;

  return (int)(this->data_size / this->wave->nAvgBytesPerSec);
}

demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {

  demux_wav_t *this;

  if (iface != 10) {
    printf (_("demux_wav: plugin doesn't support plugin API version %d.\n"
              "           this means there's a version mismatch between xine and this "
              "           demuxer plugin.\nInstalling current demux plugins should help.\n"),
            iface);
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_wav_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config,
                                        "mrl.ends_wav", VALID_ENDS,
                                        _("valid mrls ending for wav demuxer"),
                                        NULL, 20, NULL, NULL);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_wav_open;
  this->demux_plugin.start             = demux_wav_start;
  this->demux_plugin.seek              = demux_wav_seek;
  this->demux_plugin.stop              = demux_wav_stop;
  this->demux_plugin.close             = demux_wav_close;
  this->demux_plugin.get_status        = demux_wav_get_status;
  this->demux_plugin.get_identifier    = demux_wav_get_id;
  this->demux_plugin.get_stream_length = demux_wav_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_wav_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );

  return (demux_plugin_t *) this;
}
