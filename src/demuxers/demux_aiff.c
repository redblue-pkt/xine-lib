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
 * AIFF File Demuxer by Mike Melanson (melanson@pcisys.net)
 *
 * $Id: demux_aiff.c,v 1.8 2002/10/05 14:39:24 komadori Exp $
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

#define BE_16(x) (be2me_16((uint32_t)(x)))
#define BE_32(x) (be2me_32((uint32_t)(x)))

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch3) | \
        ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | \
        ( (long)(unsigned char)(ch0) << 24 ) )

#define FORM_TAG FOURCC_TAG('F', 'O', 'R', 'M')
#define AIFF_TAG FOURCC_TAG('A', 'I', 'F', 'F')
#define COMM_TAG FOURCC_TAG('C', 'O', 'M', 'M')
#define SSND_TAG FOURCC_TAG('S', 'S', 'N', 'D')
#define APCM_TAG FOURCC_TAG('A', 'P', 'C', 'M')

#define PREAMBLE_SIZE 8
#define AIFF_SIGNATURE_SIZE 12
#define PCM_BLOCK_ALIGN 1024

#define VALID_ENDS "aif,aiff"

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

  unsigned int         audio_type;
  unsigned int         audio_frames;
  unsigned int         audio_sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;
  unsigned int         audio_block_align;
  unsigned int         audio_bytes_per_second;

  unsigned int         running_time;

  off_t                data_start;
  off_t                data_size;

  int                  seek_flag;  /* this is set when a seek just occurred */
} demux_aiff_t;

static void *demux_aiff_loop (void *this_gen) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;
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
       * pointing; issue a DEMUX_FINISHED status if EOF is reached */
      remaining_sample_bytes = this->audio_block_align;
      current_file_pos = 
        this->input->get_current_pos(this->input) - this->data_start;

      current_pts = current_file_pos;
      current_pts *= 90000;
      current_pts /= this->audio_bytes_per_second;

      if (this->seek_flag) {
        xine_demux_control_newpts(this->xine, current_pts, 0);
        this->seek_flag = 0;
      }

      while (remaining_sample_bytes) {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = this->audio_type;
        buf->input_pos = current_file_pos;
        buf->input_length = this->data_size;
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

  printf ("demux_aiff: demux loop finished (status: %d)\n",
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

static int load_aiff_and_send_headers(demux_aiff_t *this) {

  unsigned char preamble[PREAMBLE_SIZE];
  unsigned int chunk_type;
  unsigned int chunk_size;
  unsigned char buffer[100];

  pthread_mutex_lock(&this->mutex);

  this->video_fifo  = this->xine->video_fifo;
  this->audio_fifo  = this->xine->audio_fifo;

  this->status = DEMUX_OK;

  /* audio type is PCM unless proven otherwise */
  this->audio_type = BUF_AUDIO_LPCM_BE;
  this->audio_frames = 0;
  this->audio_channels = 0;
  this->audio_bits = 0;
  this->audio_sample_rate = 0;
  this->audio_bytes_per_second = 0;

  /* skip past the file header and traverse the chunks */
  this->input->seek(this->input, 12, SEEK_SET);
  while (1) {
    if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
      PREAMBLE_SIZE) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_lock(&this->mutex);
      return DEMUX_CANNOT_HANDLE;
    }
    chunk_type = BE_32(preamble[0]);
    chunk_size = BE_32(preamble[4]);

    if (chunk_type == COMM_TAG) {
      if (this->input->read(this->input, buffer, chunk_size) !=
        chunk_size) {
        this->status = DEMUX_FINISHED;
        pthread_mutex_lock(&this->mutex);
        return DEMUX_CANNOT_HANDLE;
      }

      this->audio_channels = BE_16(buffer[0]);
      this->audio_frames = BE_32(buffer[2]);
      this->audio_bits = BE_16(buffer[6]);
      this->audio_sample_rate = BE_16(buffer[0x0A]);
      this->audio_bytes_per_second = this->audio_channels *
        (this->audio_bits / 8) * this->audio_sample_rate;

    } else if ((chunk_type == SSND_TAG) || 
               (chunk_type == APCM_TAG)) {

      /* audio data has been located; proceed to demux loop after
       * skipping 8 more bytes (2 more 4-byte numbers) */
      this->input->seek(this->input, 8, SEEK_CUR);
      this->data_start = this->input->get_current_pos(this->input);
      this->data_size = this->audio_frames * this->audio_channels *
        (this->audio_bits / 8);
      this->running_time = this->audio_frames / this->audio_sample_rate;

      this->audio_block_align = PCM_BLOCK_ALIGN;

      break;

    } else {
      /* unrecognized; skip it */
      this->input->seek(this->input, chunk_size, SEEK_CUR);
    }
  }

  /* the audio parameters should have been set at this point */
  if (!this->audio_channels) {
    this->status = DEMUX_FINISHED;
    pthread_mutex_lock(&this->mutex);
    return DEMUX_CANNOT_HANDLE;
  }

  /* load stream information */
  this->xine->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->xine->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->audio_sample_rate;
  this->xine->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->audio_bits;

  xine_demux_control_headers_done (this->xine);

  pthread_mutex_unlock (&this->mutex);

  return DEMUX_CAN_HANDLE;
}

static int demux_aiff_open(demux_plugin_t *this_gen,
                           input_plugin_t *input, int stage) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;
  unsigned char signature[AIFF_SIGNATURE_SIZE];

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, signature, AIFF_SIGNATURE_SIZE) !=
      AIFF_SIGNATURE_SIZE)
      return DEMUX_CANNOT_HANDLE;

    /* check the signature */
    if ((BE_32(signature[0]) == FORM_TAG) &&
        (BE_32(signature[8]) == AIFF_TAG))
      return load_aiff_and_send_headers(this);

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
                                                            "mrl.ends_aiff", VALID_ENDS,
                                                            _("valid mrls ending for aiff demuxer"),
                                                            NULL, 10, NULL, NULL)));
    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

      while(*m == ' ' || *m == '\t') m++;

      if(!strcasecmp((suffix + 1), m))
        return load_aiff_and_send_headers(this);
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

static int demux_aiff_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time);

static int demux_aiff_start (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;
  buf_element_t *buf;
  int err;

  demux_aiff_seek(this_gen, start_pos, start_time);

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {

    /* print vital stats */
    xine_log(this->xine, XINE_LOG_MSG,
      _("demux_aiff: %d Hz, %d channels, %d bits, %d frames\n"),
      this->audio_sample_rate,
      this->audio_channels,
      this->audio_bits,
      this->audio_frames);
    xine_log(this->xine, XINE_LOG_MSG,
      _("demux_aiff: running time: %d min, %d sec\n"),
      this->running_time / 60,
      this->running_time % 60);

    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send init info to decoders */
    if (this->audio_fifo && this->audio_type) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->audio_type;
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = this->audio_sample_rate;
      buf->decoder_info[2] = this->audio_bits;
      buf->decoder_info[3] = this->audio_channels;
      buf->size = 0;
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_aiff_loop, this)) != 0) {
      printf ("demux_aiff: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_aiff_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;
  int status;

  pthread_mutex_lock(&this->mutex);

  /* check the boundary offsets */
  if (start_pos < 0)
    this->input->seek(this->input, this->data_start, SEEK_SET);
  else if (start_pos >= this->data_size) {
    this->status = DEMUX_FINISHED;
    status = this->status;
    pthread_mutex_unlock(&this->mutex);
    return status;
  } else {
    /* This function must seek along the block alignment. The start_pos
     * is in reference to the start of the data. Divide the start_pos by
     * the block alignment integer-wise, and multiply the quotient by the
     * block alignment to get the new aligned offset. Add the data start
     * offset and seek to the new position. */
    start_pos /= this->audio_block_align;
    start_pos *= this->audio_block_align;
    start_pos += this->data_start;

    this->input->seek(this->input, start_pos, SEEK_SET);
  }

  this->seek_flag = 1;
  status = this->status = DEMUX_OK;
  xine_demux_flush_engine (this->xine);
  pthread_mutex_unlock(&this->mutex);

  return status;
}

static void demux_aiff_stop (demux_plugin_t *this_gen) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;
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

static void demux_aiff_dispose (demux_plugin_t *this_gen) {
  demux_aiff_t *this = (demux_aiff_t *) this_gen;

  pthread_mutex_destroy (&this->mutex);
  free(this);
}

static int demux_aiff_get_status (demux_plugin_t *this_gen) {
  demux_aiff_t *this = (demux_aiff_t *) this_gen;

  return this->status;
}

static char *demux_aiff_get_id(void) {
  return "AIFF";
}

static char *demux_aiff_get_mimetypes(void) {
  return NULL;
}

/* return the approximate length in seconds */
static int demux_aiff_get_stream_length (demux_plugin_t *this_gen) {

  demux_aiff_t *this = (demux_aiff_t *) this_gen;

  return this->running_time;
}

static void *init_demuxer_plugin(xine_t *xine, void *data) {

  demux_aiff_t *this;

  this         = xine_xmalloc (sizeof (demux_aiff_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config,
                                        "mrl.ends_aiff", VALID_ENDS,
                                        _("valid mrls ending for aiff demuxer"),
                                        NULL, 10, NULL, NULL);

  this->demux_plugin.open              = demux_aiff_open;
  this->demux_plugin.start             = demux_aiff_start;
  this->demux_plugin.seek              = demux_aiff_seek;
  this->demux_plugin.stop              = demux_aiff_stop;
  this->demux_plugin.dispose           = demux_aiff_dispose;
  this->demux_plugin.get_status        = demux_aiff_get_status;
  this->demux_plugin.get_identifier    = demux_aiff_get_id;
  this->demux_plugin.get_stream_length = demux_aiff_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_aiff_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );

  return (demux_plugin_t *) this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 11, "aiff", XINE_VERSION_CODE, NULL, init_demuxer_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
