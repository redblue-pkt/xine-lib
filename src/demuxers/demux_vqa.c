/*
 * Copyright (C) 2000-2002 the xine project
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
 * VQA File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the VQA file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_vqa.c,v 1.3 2002/09/04 23:31:08 guenter Exp $
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
#include "bswap.h"

#define BE_16(x) (be2me_16(*(unsigned short *)(x)))
#define BE_32(x) (be2me_32(*(unsigned int *)(x)))
#define LE_16(x) (le2me_16(*(unsigned short *)(x)))
#define LE_32(x) (le2me_32(*(unsigned int *)(x)))

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
  off_t         frame_offset;
  unsigned int  frame_size;
  int64_t       audio_pts;
  int64_t       video_pts;
} vqa_frame_t;

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

  unsigned int         total_frames;
  vqa_frame_t         *frame_table;
  unsigned int         current_frame;
  unsigned int         last_frame;
  int                  total_time;

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
  unsigned int i;
  unsigned char preamble[VQA_PREAMBLE_SIZE];
  unsigned int chunk_size;
  off_t current_file_pos;
  int skip_byte;

  pthread_mutex_lock( &this->mutex );

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      pthread_mutex_lock( &this->mutex );

      current_file_pos = this->input->get_current_pos(this->input);

      i = this->current_frame;
      /* if there is an incongruency between last and current sample, it
       * must be time to send a new pts */
      if (this->last_frame + 1 != this->current_frame)
        xine_demux_control_newpts(this->xine, 
          this->frame_table[i].video_pts, BUF_FLAG_SEEK);

      this->last_frame = this->current_frame;
      this->current_frame++;

      /* check if all the samples have been sent */
      if (i >= this->total_frames) {
        this->status = DEMUX_FINISHED;
        break;
      }

      /* make sure to position at the current frame */
      this->input->seek(this->input, this->frame_table[i].frame_offset,
        SEEK_SET);

      /* load and dispatch the audio portion of the frame */
      if (this->input->read(this->input, preamble, VQA_PREAMBLE_SIZE) !=
        VQA_PREAMBLE_SIZE) {
        this->status = DEMUX_FINISHED;
        break;
      }

      chunk_size = BE_32(&preamble[4]);
      skip_byte = chunk_size & 0x1;
      while (chunk_size) {
        buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
        buf->type = BUF_AUDIO_VQA_IMA;
        buf->input_pos = current_file_pos;
        buf->input_length = this->filesize;
        buf->input_time = this->frame_table[i].audio_pts / 90000;
        buf->pts = this->frame_table[i].audio_pts;

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

        chunk_size = BE_32(&preamble[4]);
        while (chunk_size) {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->type = BUF_VIDEO_VQA;
          buf->input_pos = current_file_pos;
          buf->input_length = this->filesize;
          buf->input_time = this->frame_table[i].video_pts / 90000;
          buf->pts = this->frame_table[i].video_pts;

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
      }
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
                                                            "mrl.ends_vqa", VALID_ENDS,
                                                            _("valid mrls ending for vqa demuxer"),
                                                            NULL, 10, NULL, NULL)));    
    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

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

static int demux_vqa_start (demux_plugin_t *this_gen,
                             fifo_buffer_t *video_fifo,
                             fifo_buffer_t *audio_fifo,
                             off_t start_pos, int start_time) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;
  buf_element_t *buf;
  int err;
  unsigned char header[VQA_HEADER_SIZE];
  unsigned char *finf_chunk;
  int i;
  off_t last_offset;
  uint64_t audio_pts_counter = 0;
  uint64_t video_pts_counter = 0;

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {
    this->video_fifo = video_fifo;
    this->audio_fifo = audio_fifo;

    /* get the file size (a.k.a., last offset) as reported by the file */
    this->input->seek(this->input, 4, SEEK_SET);
    if (this->input->read(this->input, header, 4) != 4) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
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
      return DEMUX_FINISHED;
    }

    /* fetch the interesting information */
    this->total_frames = LE_16(&header[4]);
    this->video_width = LE_16(&header[6]);
    this->video_height = LE_16(&header[8]);
    this->vector_width = header[10];
    this->vector_height = header[11];
    this->audio_sample_rate = LE_16(&header[24]);
    this->audio_channels = header[26];

    /* fetch the chunk table */
    this->input->seek(this->input, 8, SEEK_CUR);  /* skip FINF and length */
    finf_chunk = xine_xmalloc(this->total_frames * 4);
    this->frame_table = xine_xmalloc(this->total_frames * sizeof(vqa_frame_t));
    if (this->input->read(this->input, finf_chunk, this->total_frames * 4) !=
      this->total_frames * 4) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
    }
    for (i = 0; i < this->total_frames; i++) {
      this->frame_table[i].frame_offset = (LE_32(&finf_chunk[i * 4]) * 2) &
        0x03FFFFFF;
      if (i < this->total_frames - 1)
        this->frame_table[i].frame_size = LE_32(&finf_chunk[(i + 1) * 4]) * 2 - 
          this->frame_table[i].frame_offset;
      else
        this->frame_table[i].frame_size = last_offset - 
          this->frame_table[i].frame_offset;

      /*
       * VQA files play at a constant rate of 15 frames/second. The file data
       * begins with 1/15 sec of compressed audio followed by 1 video frame
       * that will be displayed for 1/15 sec.
       *
       *   xine pts     frame #
       *   --------  =  -------  =>  xine pts = 90000 * frame # / 15
       *    90000         15
       *
       * Thus, each frame has a duration of 90000 / 15 (VQA_PTS_INC, in
       * this code).
       *
       * If this is the first frame in the file, it contains 1/2 sec
       * of audio and no video. Each successive frame represents 1/15 sec.
       */
      if (i == 0) {
        this->frame_table[i].audio_pts = 0;
        this->frame_table[i].video_pts = 0;
        audio_pts_counter += (90000 / 2);
      } else {
        this->frame_table[i].audio_pts = audio_pts_counter;
        this->frame_table[i].video_pts = video_pts_counter;
        audio_pts_counter += VQA_PTS_INC;
        video_pts_counter += VQA_PTS_INC;
      }
    }

    this->total_time = this->frame_table[this->total_frames - 1].video_pts /
      90000;

    /* print vital stats */
    xine_log (this->xine, XINE_LOG_FORMAT,
      _("demux_vqa: running time: %d min, %d sec\n"),
      this->total_time / 60,
      this->total_time % 60);
    xine_log (this->xine, XINE_LOG_FORMAT,
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

    this->current_frame = 0;
    this->last_frame = 0;

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

static void demux_vqa_close (demux_plugin_t *this_gen) {

  demux_vqa_t *this = (demux_vqa_t *) this_gen;

  free(this->frame_table);
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


demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {
  demux_vqa_t *this;

  if (iface != 10) {
    printf (_("demux_vqa: plugin doesn't support plugin API version %d.\n"
              "           this means there's a version mismatch between xine and this "
              "           demuxer plugin.\nInstalling current demux plugins should help.\n"),
            iface);
    return NULL;
  }

  this         = (demux_vqa_t *) xine_xmalloc(sizeof(demux_vqa_t));
  this->config = xine->config;
  this->xine   = xine;

  (void *) this->config->register_string(this->config,
                                         "mrl.ends_vqa", VALID_ENDS,
                                         _("valid mrls ending for vqa demuxer"),
					 NULL, 10, NULL, NULL);
  
  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_vqa_open;
  this->demux_plugin.start             = demux_vqa_start;
  this->demux_plugin.seek              = demux_vqa_seek;
  this->demux_plugin.stop              = demux_vqa_stop;
  this->demux_plugin.close             = demux_vqa_close;
  this->demux_plugin.get_status        = demux_vqa_get_status;
  this->demux_plugin.get_identifier    = demux_vqa_get_id;
  this->demux_plugin.get_stream_length = demux_vqa_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_vqa_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init(&this->mutex, NULL);

  return &this->demux_plugin;
}

