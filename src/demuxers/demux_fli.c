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
 * FLI File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For information on the FLI format, as well as various traps to
 * avoid while programming a FLI decoder, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_fli.c,v 1.19 2002/10/24 22:48:25 guenter Exp $
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

#define VALID_ENDS   "fli,flc"

#define FLI_HEADER_SIZE 128
#define FLI_FILE_MAGIC_1 0xAF11
#define FLI_FILE_MAGIC_2 0xAF12
#define FLI_CHUNK_MAGIC_1 0xF1FA
#define FLI_CHUNK_MAGIC_2 0xF5FA

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;

  input_plugin_t      *input;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;
  int                  send_end_buffers;

  off_t                start;
  int                  status;

  /* video information */
  unsigned int         width;
  unsigned int         height;
  unsigned char        fli_header[FLI_HEADER_SIZE];

  /* playback info */
  unsigned int         magic_number;
  unsigned int         speed;
  unsigned int         frame_pts_inc;
  unsigned int         frame_count;

  char                 last_mrl[1024];
} demux_fli_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_fli_class_t;

/* returns 1 if the CIN file was opened successfully, 0 otherwise */
static int open_fli_file(demux_fli_t *this) {

  /* read the whole header */
  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, this->fli_header, FLI_HEADER_SIZE) !=
    FLI_HEADER_SIZE)
    return 0;

  /* validate the file */
  this->magic_number = LE_16(&this->fli_header[4]);
  if ((this->magic_number != FLI_FILE_MAGIC_1) &&
      (this->magic_number != FLI_FILE_MAGIC_2))
    return 0;

  this->frame_count = LE_16(&this->fli_header[6]);
  this->width = LE_16(&this->fli_header[8]);
  this->height = LE_16(&this->fli_header[10]);
  this->speed = LE_32(&this->fli_header[16]);
  if (this->magic_number == 0xAF11) {
    /* 
     * in this case, the speed (n) is number of 1/70s ticks between frames:
     *
     *  xine pts     n * frame #
     *  --------  =  -----------  => xine pts = n * (90000/70) * frame #
     *   90000           70
     *
     *  therefore, the frame pts increment = n * 1285.7
     */
     this->frame_pts_inc = this->speed * 1285.7;
  } else {
    /* 
     * in this case, the speed (n) is number of milliseconds between frames:
     *
     *  xine pts     n * frame #
     *  --------  =  -----------  => xine pts = n * 90 * frame #
     *   90000          1000
     *
     *  therefore, the frame pts increment = n * 90
     */
     this->frame_pts_inc = this->speed * 90;
  }

  return 1;
}

static void *demux_fli_loop (void *this_gen) {

  demux_fli_t *this = (demux_fli_t *) this_gen;
  buf_element_t *buf = NULL;
  int i = 0;
  unsigned char fli_buf[6];
  unsigned int chunk_size;
  unsigned int chunk_magic;
  int64_t pts_counter = 0;
  off_t current_file_pos;
  off_t stream_len;

  /* make sure to start just after the header */
  this->input->seek(this->input, FLI_HEADER_SIZE, SEEK_SET);

  stream_len = this->input->get_length(this->input);

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      /* check if all the frames have been sent */
      if (i >= this->frame_count) {
        this->status = DEMUX_FINISHED;
        break;
      }

      current_file_pos = this->input->get_current_pos(this->input);

      /* get the chunk size nd magic number */
      if (this->input->read(this->input, fli_buf, 6) != 6) {
        this->status = DEMUX_FINISHED;
        break;
      }
      chunk_size = LE_32(&fli_buf[0]);
      chunk_magic = LE_16(&fli_buf[4]);

      /* rewind over the size and packetize the chunk */
      this->input->seek(this->input, -6, SEEK_CUR);

      if ((chunk_magic == FLI_CHUNK_MAGIC_1) || 
          (chunk_magic == FLI_CHUNK_MAGIC_2)) {
        while (chunk_size) {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->type = BUF_VIDEO_FLI;
          buf->input_pos = current_file_pos;
          buf->input_time = pts_counter / 90000;
          buf->input_length = stream_len;
          buf->pts = pts_counter;

          if (chunk_size > buf->max_size)
            buf->size = buf->max_size;
          else
            buf->size = chunk_size;
          chunk_size -= buf->size;

          if (this->input->read(this->input, buf->content, buf->size) !=
            buf->size) {
            this->status = DEMUX_FINISHED;
            break;
          }

          if (!chunk_size)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;
          this->video_fifo->put(this->video_fifo, buf);
        }
        pts_counter += this->frame_pts_inc;
      } else
        this->input->seek(this->input, chunk_size, SEEK_CUR);

      i++;

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      /* give demux_*_stop a chance to interrupt us */
      sched_yield();
      pthread_mutex_lock( &this->mutex );
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->video_fifo->size(this->video_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }
  } while (this->status == DEMUX_OK);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->stream, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );

  return NULL;
}

static void demux_fli_send_headers(demux_plugin_t *this_gen) {

  demux_fli_t *this = (demux_fli_t *) this_gen;
  buf_element_t *buf;

  pthread_mutex_lock(&this->mutex);

  this->video_fifo  = this->stream->video_fifo;
  /* video-only format, don't worry about audio_fifo */

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = this->width;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->height;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to FLI decoder */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = this->frame_pts_inc;  /* initial video_step */
  /* be a rebel and send the FLI header instead of the bih */
  memcpy(buf->content, this->fli_header, FLI_HEADER_SIZE);
  buf->size = FLI_HEADER_SIZE;
  buf->type = BUF_VIDEO_FLI;
  this->video_fifo->put (this->video_fifo, buf);

  xine_demux_control_headers_done (this->stream);

  pthread_mutex_unlock (&this->mutex);
}

static int demux_fli_start (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {

  demux_fli_t *this = (demux_fli_t *) this_gen;
  int err;

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {

    /* send new pts */
    xine_demux_control_newpts(this->stream, 0, 0);

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    /* kick off the demux thread */
    if ((err = pthread_create (&this->thread, NULL, demux_fli_loop, this)) != 0) {
      printf ("demux_fli: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_fli_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  /* FLI files are not meant to be seekable; don't even bother */

  return 0;
}

static void demux_fli_stop (demux_plugin_t *this_gen) {

  demux_fli_t *this = (demux_fli_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );

  if (!this->thread_running) {
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  /* seek to the start of the data in case there's another start command */
  this->input->seek(this->input, FLI_HEADER_SIZE, SEEK_SET);

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_demux_flush_engine(this->stream);

  xine_demux_control_end(this->stream, BUF_FLAG_END_USER);
}

static void demux_fli_dispose (demux_plugin_t *this) {

  demux_fli_stop(this);

  free(this);
}

static int demux_fli_get_status (demux_plugin_t *this_gen) {
  demux_fli_t *this = (demux_fli_t *) this_gen;

  return (this->thread_running?DEMUX_OK:DEMUX_FINISHED);
}

static int demux_fli_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_fli_t    *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_fli.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_fli_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_fli_send_headers;
  this->demux_plugin.start             = demux_fli_start;
  this->demux_plugin.seek              = demux_fli_seek;
  this->demux_plugin.stop              = demux_fli_stop;
  this->demux_plugin.dispose           = demux_fli_dispose;
  this->demux_plugin.get_status        = demux_fli_get_status;
  this->demux_plugin.get_stream_length = demux_fli_get_stream_length;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init (&this->mutex, NULL);

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:

    if (!open_fli_file(this)) {
      free (this);
      return NULL;
    }

  break;

  case METHOD_BY_EXTENSION: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');

    if (!ending) {
      free (this);
      return NULL;
    }

    if (strncasecmp (ending, ".fli", 4) &&
        strncasecmp (ending, ".flc", 4)) {
      free (this);
      return NULL;
    }

    if (!open_fli_file(this)) {
      free (this);
      return NULL;
    }

  }

  break;

  default:
    free (this);
    return NULL;
  }

  strncpy (this->last_mrl, input->get_mrl (input), 1024);

  /* print vital stats */
  xine_log (this->stream->xine, XINE_LOG_MSG,
    _("demux_fli: FLI type: %04X, speed: %d/%d\n"),
    this->magic_number, this->speed,
    (this->magic_number == FLI_FILE_MAGIC_1) ? 70 : 1000);
  xine_log (this->stream->xine, XINE_LOG_MSG,
    _("demux_fli: %d frames, %dx%d\n"), 
    this->frame_count, this->width, this->height);
  xine_log (this->stream->xine, XINE_LOG_MSG,
    _("demux_fli: running time: %d min, %d sec\n"), 
    this->frame_count * this->frame_pts_inc / 90000 / 60,
    this->frame_count * this->frame_pts_inc / 90000 % 60);

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "Autodesk Animator FLI/FLC demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "FLI/FLC";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "fli flc";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_fli_class_t *this = (demux_fli_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_fli_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_fli_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 14, "fli", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
