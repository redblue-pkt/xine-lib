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
 * CIN File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Id CIN file format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 *
 * $Id: demux_idcin.c,v 1.3 2002/08/01 03:56:31 tmmm Exp $
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

#define VALID_ENDS   "cin"
#define IDCIN_HEADER_SIZE 20
#define HUFFMAN_TABLE_SIZE 65536
#define IDCIN_FRAME_PTS_INC  (90000 / 14)

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

  off_t                start;
  int                  status;

  unsigned int         video_width;
  unsigned int         video_height;
  unsigned int         audio_sample_rate;
  unsigned int         audio_bytes_per_sample;
  unsigned int         audio_channels;

  unsigned char        huffman_table[HUFFMAN_TABLE_SIZE];

} demux_idcin_t;

static void *demux_idcin_loop (void *this_gen) {

  demux_idcin_t *this = (demux_idcin_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int command;
  off_t current_file_pos;

  pthread_mutex_lock( &this->mutex );

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      current_file_pos = this->input->get_current_pos(this->input);

      /* figure out what the next data is */
      if (this->input->read(this->input, (unsigned char *)&command, 4) != 4) {
        this->status = DEMUX_FINISHED;
        pthread_mutex_unlock(&this->mutex);
        break;
      }

      command = le2me_32(command);
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->video_fifo->size(this->video_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while (this->status == DEMUX_OK);

  printf ("demux_idcin: demux loop finished (status: %d)\n",
          this->status);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock(&this->mutex);
  return NULL;
}

static int demux_idcin_open(demux_plugin_t *this_gen,
                         input_plugin_t *input, int stage) {

  demux_idcin_t *this = (demux_idcin_t *) this_gen;
  char header[IDCIN_HEADER_SIZE];
  unsigned int current_value;

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, header, IDCIN_HEADER_SIZE) != IDCIN_HEADER_SIZE)
      return DEMUX_CANNOT_HANDLE;

    /*
     * This is what you could call a "probabilistic" file check: Id CIN
     * files don't have a definite file signature. In lieu of such a marker,
     * perform sanity checks on the 5 header fields:
     *  width, height: greater than 0, less than or equal to 1024
     * audio sample rate: greater than or equal to 8000, less than or
     *  equal to 48000, or 0 for no audio
     * audio sample width (bytes/sample): 0 for no audio, or 1 or 2
     * audio channels: 0 for no audio, or 1 or 2
     */

    /* check the width */
    current_value = LE_32(&header[0]);
    if ((current_value == 0) || (current_value > 1024))
      return DEMUX_CANNOT_HANDLE;

    /* check the height */
    current_value = LE_32(&header[4]);
    if ((current_value == 0) || (current_value > 1024))
      return DEMUX_CANNOT_HANDLE;

    /* check the audio sample rate */
    current_value = LE_32(&header[8]);
    if ((current_value < 8000) || (current_value > 48000))
      return DEMUX_CANNOT_HANDLE;

    /* check the audio bytes/sample */
    current_value = LE_32(&header[12]);
    if (current_value > 2)
      return DEMUX_CANNOT_HANDLE;

    /* check the audio channels */
    current_value = LE_32(&header[16]);
    if (current_value > 2)
      return DEMUX_CANNOT_HANDLE;

    /* if execution got this far, qualify it as a valid Id CIN file */
    return DEMUX_CAN_HANDLE;
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
                                                            "mrl.ends_idcin", VALID_ENDS,
                                                            _("valid mrls ending for idcin demuxer"),
                                                            NULL, NULL, NULL)));    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

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

static int demux_idcin_start (demux_plugin_t *this_gen,
                              fifo_buffer_t *video_fifo,
                              fifo_buffer_t *audio_fifo,
                              off_t start_pos, int start_time) {

  demux_idcin_t *this = (demux_idcin_t *) this_gen;
  buf_element_t *buf;
  int err;
  char header[IDCIN_HEADER_SIZE];

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {
    this->video_fifo = video_fifo;
    this->audio_fifo = audio_fifo;

    this->input->seek(this->input, 0, SEEK_SET);
    if (this->input->read(this->input, header, IDCIN_HEADER_SIZE) !=
      IDCIN_HEADER_SIZE) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
    }

    this->video_width = LE_32(&header[0]);
    this->video_height = LE_32(&header[4]);
    this->audio_sample_rate = LE_32(&header[8]);
    this->audio_bytes_per_sample = LE_32(&header[12]);
    this->audio_channels = LE_32(&header[16]);

    /* read the Huffman table */
    if (this->input->read(this->input, this->huffman_table,
      HUFFMAN_TABLE_SIZE) != HUFFMAN_TABLE_SIZE) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
    }

    /* print vital stats */
    xine_log (this->xine, XINE_LOG_FORMAT,
      _("demux_idcin: Id CIN file, video is %dx%d, 14 frames/sec\n"),
      this->video_width,
      this->video_height);
    if (this->audio_channels)
      xine_log (this->xine, XINE_LOG_FORMAT,
        _("demux_idcin: %d-bit, %d Hz %s PCM audio\n"),
        this->audio_bytes_per_sample * 8,
        this->audio_sample_rate,
        (this->audio_channels == 1) ? "monaural" : "stereo");


pthread_mutex_unlock(&this->mutex);
return DEMUX_FINISHED;


    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send new pts */
    xine_demux_control_newpts(this->xine, 0, 0);

    /* send init info to decoders */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = IDCIN_FRAME_PTS_INC;  /* initial video_step */
    /* really be a rebel: No structure at all, just put the video width
     * and height straight into the buffer, BE_16 format */
    buf->content[0] = (this->video_width >> 8) & 0xFF;
    buf->content[1] = (this->video_width >> 0) & 0xFF;
    buf->content[2] = (this->video_height >> 8) & 0xFF;
    buf->content[3] = (this->video_height >> 0) & 0xFF;
    buf->size = 4;
    buf->type = BUF_VIDEO_IDCIN;
    this->video_fifo->put (this->video_fifo, buf);

    /* send the Huffman table */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_SPECIAL;
    buf->decoder_info[1] = BUF_SPECIAL_IDCIN_HUFFMAN_TABLE;
    buf->decoder_info[2] = (unsigned int)&this->huffman_table;
    buf->size = 0;
    buf->type = BUF_VIDEO_IDCIN;
    this->video_fifo->put (this->video_fifo, buf);

    if (this->audio_fifo && this->audio_channels) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_LPCM_LE;
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = this->audio_sample_rate;
      buf->decoder_info[2] = this->audio_bytes_per_sample * 8;
      buf->decoder_info[3] = this->audio_channels;
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_idcin_loop, this)) != 0) {
      printf ("demux_idcin: can't create new thread (%s)\n", strerror(err));
      abort();
    }

    this->status = DEMUX_OK;
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_idcin_seek (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {

  /* Id CIN files are not meant to be seekable; don't even bother */

  return 0;
}

static void demux_idcin_stop (demux_plugin_t *this_gen) {

  demux_idcin_t *this = (demux_idcin_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );

  if (!this->thread_running) {
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  /* seek to the start of the data in case there's another start command */
  this->input->seek(this->input, IDCIN_HEADER_SIZE + HUFFMAN_TABLE_SIZE,
    SEEK_SET);

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_demux_flush_engine(this->xine);

  xine_demux_control_end(this->xine, BUF_FLAG_END_USER);
}

static void demux_idcin_close (demux_plugin_t *this) {
  free(this);
}

static int demux_idcin_get_status (demux_plugin_t *this_gen) {
  demux_idcin_t *this = (demux_idcin_t *) this_gen;

  return this->status;
}

static char *demux_idcin_get_id(void) {
  return "Id CIN";
}

static int demux_idcin_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static char *demux_idcin_get_mimetypes(void) {
  return NULL;
}

demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {
  demux_idcin_t *this;

  if (iface != 10) {
    printf (_("demux_idcin: plugin doesn't support plugin API version %d.\n"
              "             this means there's a version mismatch between xine and this "
              "             demuxer plugin.\nInstalling current demux plugins should help.\n"),
            iface);
    return NULL;
  }

  this         = (demux_idcin_t *) xine_xmalloc(sizeof(demux_idcin_t));
  this->config = xine->config;
  this->xine   = xine;

  (void *) this->config->register_string(this->config,
                                         "mrl.ends_idcin", VALID_ENDS,
                                         _("valid mrls ending for idcin demuxer"),                                         NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_idcin_open;
  this->demux_plugin.start             = demux_idcin_start;
  this->demux_plugin.seek              = demux_idcin_seek;
  this->demux_plugin.stop              = demux_idcin_stop;
  this->demux_plugin.close             = demux_idcin_close;
  this->demux_plugin.get_status        = demux_idcin_get_status;
  this->demux_plugin.get_identifier    = demux_idcin_get_id;
  this->demux_plugin.get_stream_length = demux_idcin_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_idcin_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init(&this->mutex, NULL);

  return &this->demux_plugin;
}
