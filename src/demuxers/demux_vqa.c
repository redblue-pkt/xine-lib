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
 * VQA File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the VQA file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * Quick technical note: VQA files begin with a header that includes a
 * frame index. This ought to be useful for seeking within a VQA file.
 * However, seeking is infeasible due to the audio encoding: Each audio 
 * block needs information from the previous audio block in order to be
 * decoded, thus making random seeking difficult.
 *
 * $Id: demux_vqa.c,v 1.13 2002/10/12 17:11:59 jkeil Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "demux.h"
#include "bswap.h"

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch3) | \
        ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | \
        ( (long)(unsigned char)(ch0) << 24 ) )

#define FORM_TAG FOURCC_TAG('F', 'O', 'R', 'M')
#define WVQA_TAG FOURCC_TAG('W', 'Q', 'V', 'A')
#define VQHD_TAG FOURCC_TAG('V', 'Q', 'H', 'D')
#define FINF_TAG FOURCC_TAG('F', 'I', 'N', 'F')
#define SND0_TAG FOURCC_TAG('S', 'N', 'D', '0')
#define SND2_TAG FOURCC_TAG('S', 'N', 'D', '2')
#define VQFR_TAG FOURCC_TAG('V', 'Q', 'F', 'R')

#define VQA_HEADER_SIZE 0x2A
#define VQA_FRAMERATE 15
#define VQA_PTS_INC (90000 / VQA_FRAMERATE)
#define VQA_PREAMBLE_SIZE 8

#define VALID_ENDS "vqa"

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

  off_t                filesize;
  int                  status;

  unsigned int         video_width;
  unsigned int         video_height;
  unsigned int         vector_width;
  unsigned int         vector_height;

  unsigned int         audio_sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;
} demux_vqa_t ;

static void *demux_vqa_loop (void *this_gen) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int i = 0;
  unsigned char preamble[VQA_PREAMBLE_SIZE];
  unsigned int chunk_size;
  off_t current_file_pos;
  int skip_byte;
  int64_t video_pts = 0;
  int64_t audio_pts = 0;
  unsigned int audio_frames = 0;

  pthread_mutex_lock( &this->mutex );

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      /* give demux_*_stop a chance to interrupt us */
      sched_yield();
      pthread_mutex_lock( &this->mutex );

      /* load and dispatch the audio portion of the frame */
      if (this->input->read(this->input, preamble, VQA_PREAMBLE_SIZE) !=
        VQA_PREAMBLE_SIZE) {
        this->status = DEMUX_FINISHED;
        break;
      }

      current_file_pos = this->input->get_current_pos(this->input);
      chunk_size = BE_32(&preamble[4]);
      skip_byte = chunk_size & 0x1;
      audio_pts = audio_frames;
      audio_pts *= 90000;
      audio_pts /= this->audio_sample_rate;
      audio_frames += (chunk_size * 2 / this->audio_channels);
      while (chunk_size) {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = BUF_AUDIO_VQA_IMA;
        buf->input_pos = current_file_pos;
        buf->input_length = this->filesize;
        buf->input_time = audio_pts / 90000;
        buf->pts = audio_pts;

        if (chunk_size > buf->max_size)
          buf->size = buf->max_size;
        else
          buf->size = chunk_size;
        chunk_size -= buf->size;

        if (this->input->read(this->input, buf->content, buf->size) !=
          buf->size) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        if (!chunk_size)
          buf->decoder_flags |= BUF_FLAG_FRAME_END;

        this->audio_fifo->put (this->audio_fifo, buf);
      }
      /* stay on 16-bit alignment */
      if (skip_byte)
        this->input->seek(this->input, 1, SEEK_CUR);

      /* load and dispatch the video portion of the frame but only if this
       * is not frame #0 */
      if (i > 0) {
        if (this->input->read(this->input, preamble, VQA_PREAMBLE_SIZE) !=
          VQA_PREAMBLE_SIZE) {
          this->status = DEMUX_FINISHED;
          break;
        }

        current_file_pos = this->input->get_current_pos(this->input);
        chunk_size = BE_32(&preamble[4]);
        while (chunk_size) {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->type = BUF_VIDEO_VQA;
          buf->input_pos = current_file_pos;
          buf->input_length = this->filesize;
          buf->input_time = video_pts / 90000;
          buf->pts = video_pts;

          if (chunk_size > buf->max_size)
            buf->size = buf->max_size;
          else
            buf->size = chunk_size;
          chunk_size -= buf->size;

          if (this->input->read(this->input, buf->content, buf->size) !=
            buf->size) {
            buf->free_buffer(buf);
            this->status = DEMUX_FINISHED;
            break;
          }

          if (!chunk_size)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;

          this->video_fifo->put (this->video_fifo, buf);
        }
        video_pts += VQA_PTS_INC;
      }

      i++;
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->video_fifo->size(this->video_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while (this->status == DEMUX_OK);

  printf ("demux_vqa: demux loop finished (status: %d)\n",
          this->status);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock(&this->mutex);

  return NULL;
}

static int load_vqa_and_send_headers(demux_vqa_t *this) {

  unsigned char header[VQA_HEADER_SIZE];
  off_t last_offset;
  unsigned char preamble[VQA_PREAMBLE_SIZE];
  unsigned int chunk_size;

  pthread_mutex_lock(&this->mutex);

  this->video_fifo  = this->xine->video_fifo;
  this->audio_fifo  = this->xine->audio_fifo;

  this->status = DEMUX_OK;

  /* get the file size (a.k.a., last offset) as reported by the file */
  this->input->seek(this->input, 4, SEEK_SET);
  if (this->input->read(this->input, header, 4) != 4) {
    this->status = DEMUX_FINISHED;
    pthread_mutex_unlock(&this->mutex);
    return DEMUX_CANNOT_HANDLE;
  }
  last_offset = BE_32(&header[0]);

  /* get the actual filesize */
  this->filesize = this->input->get_length(this->input);

  /* skip to the VQA header */
  this->input->seek(this->input, 20, SEEK_SET);
  if (this->input->read(this->input, header, VQA_HEADER_SIZE)
    != VQA_HEADER_SIZE) {
    this->status = DEMUX_FINISHED;
    pthread_mutex_unlock(&this->mutex);
    return DEMUX_CANNOT_HANDLE;
  }

  /* fetch the interesting information */
  this->video_width = LE_16(&header[6]);
  this->video_height = LE_16(&header[8]);
  this->vector_width = header[10];
  this->vector_height = header[11];
  this->audio_sample_rate = LE_16(&header[24]);
  this->audio_channels = header[26];

  /* skip the FINF chunk */
  if (this->input->read(this->input, preamble, VQA_PREAMBLE_SIZE) !=
    VQA_PREAMBLE_SIZE) {
    this->status = DEMUX_FINISHED;
    pthread_mutex_unlock(&this->mutex);
    return DEMUX_CANNOT_HANDLE;
  }
  chunk_size = BE_32(&preamble[4]);
  this->input->seek(this->input, chunk_size, SEEK_CUR);

  /* load stream information */
  this->xine->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = this->video_width;
  this->xine->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->video_height;
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

static int demux_vqa_open(demux_plugin_t *this_gen, input_plugin_t *input,
                          int stage) {
  demux_vqa_t *this = (demux_vqa_t *) this_gen;
  char header[12];

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, header, 12) != 12)
      return DEMUX_CANNOT_HANDLE;

    /* check for the VQA signatures */
    if ((BE_32(&header[0]) == FORM_TAG) &&
        (BE_32(&header[8]) == WVQA_TAG))
      return load_vqa_and_send_headers(this);

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
                                                            "mrl.ends_vqa", VALID_ENDS,
                                                            _("valid mrls ending for vqa demuxer"),
                                                            NULL, 10, NULL, NULL)));    
    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

      while(*m == ' ' || *m == '\t') m++;

      if(!strcasecmp((suffix + 1), m))
        return load_vqa_and_send_headers(this);
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

static int demux_vqa_start (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;
  buf_element_t *buf;
  int err;

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {

    /* print vital stats */
    xine_log (this->xine, XINE_LOG_MSG,
      _("demux_vqa: %dx%d VQA video; %d-channel %d Hz IMA ADPCM audio\n"),
      this->video_width,
      this->video_height,
      this->audio_channels,
      this->audio_sample_rate);

    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send init info to decoders */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = VQA_PTS_INC;  /* initial video_step */
    /* really be a rebel: No structure at all, just put the video width
     * and height straight into the buffer, BE_16 format */
    buf->content[0] = (this->video_width >> 8) & 0xFF;
    buf->content[1] = (this->video_width >> 0) & 0xFF;
    buf->content[2] = (this->video_height >> 8) & 0xFF;
    buf->content[3] = (this->video_height >> 0) & 0xFF;
    buf->size = 4;
    buf->type = BUF_VIDEO_VQA;
    this->video_fifo->put (this->video_fifo, buf);

    /* send the vector size to the video decoder */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_SPECIAL;
    buf->decoder_info[1] = BUF_SPECIAL_VQA_VECTOR_SIZE;
    buf->decoder_info[2] = this->vector_width;
    buf->decoder_info[3] = this->vector_height;
    buf->size = 0;
    buf->type = BUF_VIDEO_VQA;
    this->video_fifo->put (this->video_fifo, buf);

    if (this->audio_fifo && this->audio_channels) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_VQA_IMA;
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = this->audio_sample_rate;
      buf->decoder_info[2] = 16;  /* bits/samples */
      buf->decoder_info[3] = 1;   /* channels */
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_vqa_loop, this)) != 0) {
      printf ("demux_vqa: can't create new thread (%s)\n", strerror(err));
      abort();
    }

    this->status = DEMUX_OK;
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_vqa_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {

  /* despite the presence of a frame index in the header, VQA files are 
   * not built for seeking; don't even bother */

  return 0;
}

static void demux_vqa_stop (demux_plugin_t *this_gen) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );

  if (!this->thread_running) {
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_demux_flush_engine(this->xine);

  xine_demux_control_end(this->xine, BUF_FLAG_END_USER);
}

static void demux_vqa_dispose (demux_plugin_t *this_gen) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;

  free(this);
}

static int demux_vqa_get_status (demux_plugin_t *this_gen) {
  demux_vqa_t *this = (demux_vqa_t *) this_gen;

  return this->status;
}

static char *demux_vqa_get_id(void) {
  return "VQA";
}

static int demux_vqa_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static char *demux_vqa_get_mimetypes(void) {
  return NULL;
}


static void *init_demuxer_plugin(xine_t *xine, void *data) {
  demux_vqa_t *this;

  this         = (demux_vqa_t *) xine_xmalloc(sizeof(demux_vqa_t));
  this->config = xine->config;
  this->xine   = xine;

  (void *) this->config->register_string(this->config,
                                         "mrl.ends_vqa", VALID_ENDS,
                                         _("valid mrls ending for vqa demuxer"),
					 NULL, 10, NULL, NULL);
  
  this->demux_plugin.open              = demux_vqa_open;
  this->demux_plugin.start             = demux_vqa_start;
  this->demux_plugin.seek              = demux_vqa_seek;
  this->demux_plugin.stop              = demux_vqa_stop;
  this->demux_plugin.dispose           = demux_vqa_dispose;
  this->demux_plugin.get_status        = demux_vqa_get_status;
  this->demux_plugin.get_identifier    = demux_vqa_get_id;
  this->demux_plugin.get_stream_length = demux_vqa_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_vqa_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init(&this->mutex, NULL);

  return &this->demux_plugin;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 11, "vqa", XINE_VERSION_CODE, NULL, init_demuxer_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
