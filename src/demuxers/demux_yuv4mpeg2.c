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
 * YUV4MPEG2 File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the YUV4MPEG2 file format and associated
 * tools, visit:
 *   http://mjpeg.sourceforge.net/
 *
 * $Id: demux_yuv4mpeg2.c,v 1.2 2002/10/05 17:48:25 tmmm Exp $
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

#define Y4M_SIGNATURE_SIZE 9
#define Y4M_SIGNATURE "YUV4MPEG2"
#define Y4M_FRAME_SIGNATURE_SIZE 6
#define Y4M_FRAME_SIGNATURE "FRAME\x0A"
/* number of header bytes is completely arbitrary */
#define Y4M_HEADER_BYTES 100


#define VALID_ENDS "y4m"

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

  off_t                data_start;
  off_t                data_size;
  int                  status;

  xine_bmiheader       bih;

  unsigned int         fps;
  unsigned int         frame_pts_inc;
  unsigned int         frame_size;
} demux_yuv4mpeg2_t;

static void *demux_yuv4mpeg2_loop (void *this_gen) {

  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned char preamble[Y4M_FRAME_SIGNATURE_SIZE];
  int bytes_remaining;
  off_t current_file_pos;
  int64_t pts;

  pthread_mutex_lock( &this->mutex );

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      pthread_mutex_lock( &this->mutex );

      /* validate that this is an actual frame boundary */
      if (this->input->read(this->input, preamble, Y4M_FRAME_SIGNATURE_SIZE) !=
        Y4M_FRAME_SIGNATURE_SIZE) {
        this->status = DEMUX_FINISHED;
        break;
      }
      if (memcmp(preamble, Y4M_FRAME_SIGNATURE, Y4M_FRAME_SIGNATURE_SIZE) !=
        0) {
        this->status = DEMUX_FINISHED;
        break;
      }

      /* load and dispatch the raw frame */
      bytes_remaining = this->frame_size;
      current_file_pos = 
        this->input->get_current_pos(this->input) - this->data_start;
      pts = current_file_pos;
      pts /= (this->frame_size + Y4M_FRAME_SIGNATURE_SIZE);
      pts *= this->frame_pts_inc;
      while(bytes_remaining) {
        buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
        buf->type = BUF_VIDEO_YV12;
        buf->input_pos = current_file_pos;
        buf->input_length = this->data_size;
        buf->pts = pts;

        if (bytes_remaining > buf->max_size)
          buf->size = buf->max_size;
        else
          buf->size = bytes_remaining;
        bytes_remaining -= buf->size;

        if (this->input->read(this->input, buf->content, buf->size) !=
          buf->size) {
          this->status = DEMUX_FINISHED;
          break;
        }

        if (!bytes_remaining)
          buf->decoder_flags |= BUF_FLAG_FRAME_END;
        this->video_fifo->put(this->video_fifo, buf);
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

  printf ("demux_yuv4mpeg2: demux loop finished (status: %d)\n",
          this->status);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock(&this->mutex);

  return NULL;
}

static int load_yuv4mpeg2_and_send_headers(demux_yuv4mpeg2_t *this) {

  unsigned char header[Y4M_HEADER_BYTES];
  int i;

  pthread_mutex_lock(&this->mutex);

  this->video_fifo  = this->xine->video_fifo;
  this->audio_fifo  = this->xine->audio_fifo;

  this->status = DEMUX_OK;

  this->bih.biWidth = this->bih.biHeight = this->fps = this->data_start = 0;

  /* back to the start */
  this->input->seek(this->input, 0, SEEK_SET);

  /* read a chunk of bytes that should contain all the header info */
  if (this->input->read(this->input, header, Y4M_HEADER_BYTES) !=
    Y4M_HEADER_BYTES) {
    this->status = DEMUX_FINISHED;
    pthread_mutex_unlock(&this->mutex);
    return DEMUX_CANNOT_HANDLE;
  }

  /* first, seek for the width (starts with " W") */
  i = 0;
  while ((header[i] != ' ') || (header[i + 1] != 'W'))
    if (i < Y4M_HEADER_BYTES - 2)
      i++;
    else
      break;
  i += 2;
  this->bih.biWidth = atoi(&header[i]);

  /* go after the height next (starts with " H") */
  while ((header[i] != ' ') || (header[i + 1] != 'H'))
    if (i < Y4M_HEADER_BYTES - 2)
      i++;
    else
      break;
  i += 2;
  this->bih.biHeight = atoi(&header[i]);

  /* compute the size of an individual frame */
  this->frame_size = this->bih.biWidth * this->bih.biHeight * 3 / 2;

  /* find the frames/sec (starts with " F") */
  while ((header[i] != ' ') || (header[i + 1] != 'F'))
    if (i < Y4M_HEADER_BYTES - 2)
      i++;
    else
      break;
  i += 2;
  this->fps = atoi(&header[i]);
  this->frame_pts_inc = 90000 / this->fps;

  /* finally, look for the first frame */
  while ((header[i + 0] != 'F') ||
         (header[i + 1] != 'R') ||
         (header[i + 2] != 'A') ||
         (header[i + 3] != 'M') ||
         (header[i + 4] != 'E'))
    if (i < Y4M_HEADER_BYTES - 5)
      i++;
    else
      break;
  this->data_start = i;
  this->data_size = this->input->get_length(this->input) - 
    this->data_start;

  /* make sure all the data was found */
  if (!this->bih.biWidth || !this->bih.biHeight ||
      !this->fps || !this->data_start) {
    this->status = DEMUX_FINISHED;
    pthread_mutex_unlock(&this->mutex);
    return DEMUX_CANNOT_HANDLE;
  }

  /* seek to first frame */
  this->input->seek(this->input, this->data_start, SEEK_SET);

  /* load stream information */
  this->xine->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = this->bih.biWidth;
  this->xine->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->bih.biHeight;

  xine_demux_control_headers_done (this->xine);

  pthread_mutex_unlock (&this->mutex);

  return DEMUX_CAN_HANDLE;
}

static int demux_yuv4mpeg2_open(demux_plugin_t *this_gen, input_plugin_t *input,
                                int stage) {
  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;
  char signature[Y4M_SIGNATURE_SIZE];

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, signature, Y4M_SIGNATURE_SIZE) != Y4M_SIGNATURE_SIZE)
      return DEMUX_CANNOT_HANDLE;

    /* check for the Y4M signature */
    if (memcmp(signature, Y4M_SIGNATURE, Y4M_SIGNATURE_SIZE) == 0)
      return load_yuv4mpeg2_and_send_headers(this);

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
                                                            "mrl.ends_yuv4mpeg2", VALID_ENDS,
                                                            _("valid mrls ending for yuv4mpeg2 demuxer"),
                                                            NULL, 10, NULL, NULL)));
    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

      while(*m == ' ' || *m == '\t') m++;

      if(!strcasecmp((suffix + 1), m))
        return load_yuv4mpeg2_and_send_headers(this);
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

static int demux_yuv4mpeg2_seek (demux_plugin_t *this_gen,
                                 off_t start_pos, int start_time);

static int demux_yuv4mpeg2_start (demux_plugin_t *this_gen,
                                  off_t start_pos, int start_time) {

  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;
  buf_element_t *buf;
  int err;

  demux_yuv4mpeg2_seek(this_gen, start_pos, start_time);

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {

    /* print vital stats */
    xine_log (this->xine, XINE_LOG_MSG,
      _("demux_yuv4mpeg2: raw YV12 video @ %d x %x\n"),
      this->bih.biWidth,
      this->bih.biHeight);

    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send new pts */
    xine_demux_control_newpts(this->xine, 0, 0);

    /* send init info to decoders */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->frame_pts_inc;  /* initial video_step */
    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = BUF_VIDEO_YV12;
    this->video_fifo->put (this->video_fifo, buf);

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_yuv4mpeg2_loop, this)) != 0) {
      printf ("demux_yuv4mpeg2: can't create new thread (%s)\n", strerror(err));
      abort();
    }

    this->status = DEMUX_OK;
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_yuv4mpeg2_seek (demux_plugin_t *this_gen,
                                 off_t start_pos, int start_time) {

  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;
  int status;

   /* YUV4MPEG2 files are essentially constant bit-rate video. Seek along
    * the calculated frame boundaries. Divide the requested seek offset
    * by the frame size integer-wise to obtain the desired frame number 
    * and then multiply the frame number by the frame size to get the
    * starting offset. Add the data_start offset to obtain the final
    * offset. */

  start_pos /= (this->frame_size + Y4M_FRAME_SIGNATURE_SIZE);
  start_pos *= (this->frame_size + Y4M_FRAME_SIGNATURE_SIZE);
  start_pos += this->data_start;

  this->input->seek(this->input, start_pos, SEEK_SET);
  status = this->status = DEMUX_OK;
  xine_demux_flush_engine (this->xine);
  pthread_mutex_unlock(&this->mutex);

  return status;
}

static void demux_yuv4mpeg2_stop (demux_plugin_t *this_gen) {

  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;
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

static void demux_yuv4mpeg2_dispose (demux_plugin_t *this_gen) {

  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;

  free(this);
}

static int demux_yuv4mpeg2_get_status (demux_plugin_t *this_gen) {
  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;

  return this->status;
}

static char *demux_yuv4mpeg2_get_id(void) {
  return "YUV4MPEG2";
}

static int demux_yuv4mpeg2_get_stream_length (demux_plugin_t *this_gen) {

  demux_yuv4mpeg2_t *this = (demux_yuv4mpeg2_t *) this_gen;

  return (this->data_size / (this->frame_size + Y4M_FRAME_SIGNATURE_SIZE) /
    this->fps);
}

static char *demux_yuv4mpeg2_get_mimetypes(void) {
  return NULL;
}


static void *init_demuxer_plugin(xine_t *xine, void *data) {
  demux_yuv4mpeg2_t *this;

  this         = (demux_yuv4mpeg2_t *) xine_xmalloc(sizeof(demux_yuv4mpeg2_t));
  this->config = xine->config;
  this->xine   = xine;

  (void *) this->config->register_string(this->config,
                                         "mrl.ends_yuv4mpeg2", VALID_ENDS,
                                         _("valid mrls ending for yuv4mpeg2 demuxer"),
                                         NULL, 10, NULL, NULL);

  this->demux_plugin.open              = demux_yuv4mpeg2_open;
  this->demux_plugin.start             = demux_yuv4mpeg2_start;
  this->demux_plugin.seek              = demux_yuv4mpeg2_seek;
  this->demux_plugin.stop              = demux_yuv4mpeg2_stop;
  this->demux_plugin.dispose           = demux_yuv4mpeg2_dispose;
  this->demux_plugin.get_status        = demux_yuv4mpeg2_get_status;
  this->demux_plugin.get_identifier    = demux_yuv4mpeg2_get_id;
  this->demux_plugin.get_stream_length = demux_yuv4mpeg2_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_yuv4mpeg2_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init(&this->mutex, NULL);

  return &this->demux_plugin;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 11, "yuv4mpeg2", XINE_VERSION_CODE, NULL, init_demuxer_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

