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
 * $Id: demux_fli.c,v 1.14 2002/10/06 02:35:08 tmmm Exp $
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

#define LE_16(x) (le2me_16(*(unsigned short *)(x)))
#define LE_32(x) (le2me_32(*(unsigned int *)(x)))

#define VALID_ENDS   "fli,flc"

#define FLI_HEADER_SIZE 128
#define FLI_FILE_MAGIC_1 0xAF11
#define FLI_FILE_MAGIC_2 0xAF12
#define FLI_CHUNK_MAGIC_1 0xF1FA
#define FLI_CHUNK_MAGIC_2 0xF5FA

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_t              *xine;

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
} demux_fli_t;

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
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );

  return NULL;
}

static int load_fli_and_send_headers(demux_fli_t *this) {

  pthread_mutex_lock(&this->mutex);

  this->video_fifo  = this->xine->video_fifo;
  /* video-only format, don't worry about audio_fifo */

  this->status = DEMUX_OK;

  /* read the whole header */
  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, this->fli_header, FLI_HEADER_SIZE) !=
    FLI_HEADER_SIZE) {
    this->status = DEMUX_FINISHED;
    pthread_mutex_unlock (&this->mutex);
    return DEMUX_CANNOT_HANDLE;
  }

  this->magic_number = LE_16(&this->fli_header[4]);
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

  /* load stream information */
  this->xine->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH] = this->width;
  this->xine->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->height;

  xine_demux_control_headers_done (this->xine);

  pthread_mutex_unlock (&this->mutex);

  return DEMUX_CAN_HANDLE;
}

static int demux_fli_open(demux_plugin_t *this_gen, input_plugin_t *input,
                          int stage) {

  demux_fli_t *this = (demux_fli_t *) this_gen;
  char sig[2];
  unsigned int magic_number;

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 4, SEEK_SET);
    if (input->read(input, sig, 2) != 2) {
      return DEMUX_CANNOT_HANDLE;
    }
    magic_number = LE_16(&sig[0]);
    if ((magic_number == FLI_FILE_MAGIC_1) || 
        (magic_number == FLI_FILE_MAGIC_2))
      return load_fli_and_send_headers(this);

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
                 "mrl.ends_fli", VALID_ENDS,
                 "valid mrls ending for fli demuxer",
		 NULL, 20, NULL, NULL)));
    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

      while(*m == ' ' || *m == '\t') m++;

      if(!strcasecmp((suffix + 1), m))
        return load_fli_and_send_headers(this);
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

static int demux_fli_start (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {

  demux_fli_t *this = (demux_fli_t *) this_gen;
  buf_element_t *buf;
  int err;

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {

    /* print vital stats */
    xine_log (this->xine, XINE_LOG_MSG,
      _("demux_fli: FLI type: %04X, speed: %d/%d\n"),
      this->magic_number, this->speed,
      (this->magic_number == FLI_FILE_MAGIC_1) ? 70 : 1000);
    xine_log (this->xine, XINE_LOG_MSG,
      _("demux_fli: %d frames, %dx%d\n"), 
      this->frame_count, this->width, this->height);
    xine_log (this->xine, XINE_LOG_MSG,
      _("demux_fli: running time: %d min, %d sec\n"), 
      this->frame_count * this->frame_pts_inc / 90000 / 60,
      this->frame_count * this->frame_pts_inc / 90000 % 60);

    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send new pts */
    xine_demux_control_newpts(this->xine, 0, 0);

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
  demux_fli_t *this = (demux_fli_t *) this_gen;

  /* FLI files are not meant to be seekable */

  return this->status;
}

static void demux_fli_stop (demux_plugin_t *this_gen) {

  demux_fli_t *this = (demux_fli_t *) this_gen;
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

static void demux_fli_dispose (demux_plugin_t *this) {
  free(this);
}

static int demux_fli_get_status (demux_plugin_t *this_gen) {
  demux_fli_t *this = (demux_fli_t *) this_gen;

  return this->status;
}

static char *demux_fli_get_id(void) {
  return "FLI";
}

static int demux_fli_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static char *demux_fli_get_mimetypes(void) {
  return NULL;
}

static void *init_demuxer_plugin(xine_t *xine, void *data) {
  demux_fli_t *this;

  this         = (demux_fli_t *) xine_xmalloc(sizeof(demux_fli_t));
  this->config = xine->config;
  this->xine   = xine;

  (void *) this->config->register_string(this->config,
                                         "mrl.ends_fli", VALID_ENDS,
                                         "valid mrls ending for fli demuxer",
                                         NULL, 10, NULL, NULL);

  this->demux_plugin.open              = demux_fli_open;
  this->demux_plugin.start             = demux_fli_start;
  this->demux_plugin.seek              = demux_fli_seek;
  this->demux_plugin.stop              = demux_fli_stop;
  this->demux_plugin.dispose           = demux_fli_dispose;
  this->demux_plugin.get_status        = demux_fli_get_status;
  this->demux_plugin.get_identifier    = demux_fli_get_id;
  this->demux_plugin.get_stream_length = demux_fli_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_fli_get_mimetypes;

  return &this->demux_plugin;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 11, "fli", XINE_VERSION_CODE, NULL, init_demuxer_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
