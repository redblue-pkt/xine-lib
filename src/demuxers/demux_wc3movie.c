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
 * File Demuxer for Wing Commander III MVE movie files
 *   by Mike Melanson (melanson@pcisys.net)
 * For more information on the MVE file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_wc3movie.c,v 1.1 2002/08/10 20:58:44 tmmm Exp $
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
#define MOVE_TAG FOURCC_TAG('M', 'O', 'V', 'E')
#define PC_TAG   FOURCC_TAG('_', 'P', 'C', '_')
#define SOND_TAG FOURCC_TAG('S', 'O', 'N', 'D')
#define PALT_TAG FOURCC_TAG('P', 'A', 'L', 'T')
#define BRCH_TAG FOURCC_TAG('B', 'R', 'C', 'H')
#define SHOT_TAG FOURCC_TAG('S', 'H', 'O', 'T')
#define VGA_TAG  FOURCC_TAG('V', 'G', 'A', ' ')
#define AUDI_TAG FOURCC_TAG('A', 'U', 'D', 'I')

#define PREAMBLE_SIZE 8

#define VALID_ENDS   "mve"

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

  unsigned int         fps;
  unsigned int         frame_pts_inc;

  xine_waveformatex    wave;

} demux_mve_t;

static void *demux_mve_loop (void *this_gen) {

  demux_mve_t *this = (demux_mve_t *) this_gen;
  buf_element_t *buf = NULL;
  int64_t current_pts = 0;
  unsigned char preamble[PREAMBLE_SIZE];
  unsigned int chunk_tag;
  unsigned int chunk_size;
  unsigned int audio_frames;
  uint64_t total_frames = 0;

  pthread_mutex_lock( &this->mutex );

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      pthread_mutex_lock( &this->mutex );

      /* traverse through the chunks just past the header */
      this->input->seek(this->input, 12, SEEK_SET);

      while (this->status == DEMUX_OK) {
        if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
          PREAMBLE_SIZE)
          this->status = DEMUX_FINISHED;
        else {
          chunk_tag = BE_32(&preamble[0]);
          /* round up to the nearest even size */
          chunk_size = (BE_32(&preamble[4]) + 1) & (~1);

/*
          printf ("  %c%c%c%c chunk containing 0x%X bytes @ %llX\n",
            preamble[0],
            preamble[1],
            preamble[2],
            preamble[3],
            chunk_size,
            this->input->get_current_pos(this->input) - 8);
*/

          /* skip it */
          if (chunk_tag != AUDI_TAG)
            this->input->seek(this->input, chunk_size, SEEK_CUR);
          else {
            audio_frames = 
              chunk_size * 8 / this->wave.wBitsPerSample / 
              this->wave.nChannels;
            total_frames += audio_frames;
            current_pts = total_frames;
            current_pts *= 90000;
            current_pts /= this->wave.nSamplesPerSec;
/*
printf ("  sending %d bytes, %d frames, %lld samples, pts = %lld\n",
  chunk_size,
  audio_frames,
  total_frames,
  current_pts);
*/
            while (chunk_size) {
              buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
              buf->type = BUF_AUDIO_LPCM_LE;
//              buf->input_pos = current_file_pos;
//              buf->input_length = this->data_end;
              buf->input_time = current_pts / 90000;
              buf->pts = current_pts;

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
          }
        }
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

  printf ("demux_wc3mve: demux loop finished (status: %d)\n",
          this->status);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock(&this->mutex);
  return NULL;
}

static int demux_mve_open(demux_plugin_t *this_gen, input_plugin_t *input,
                          int stage) {
  demux_mve_t *this = (demux_mve_t *) this_gen;
  char header[16];

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, header, 16) != 16)
      return DEMUX_CANNOT_HANDLE;

    if ((BE_32(&header[0]) == FORM_TAG) &&
        (BE_32(&header[8]) == MOVE_TAG) &&
        (BE_32(&header[12]) == PC_TAG))
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
                                                            "mrl.ends_mve", VALID_ENDS,
                                                            _("valid mrls ending for mve demuxer"),
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

static int demux_mve_start (demux_plugin_t *this_gen,
                            fifo_buffer_t *video_fifo,
                            fifo_buffer_t *audio_fifo,
                            off_t start_pos, int start_time) {

  demux_mve_t *this = (demux_mve_t *) this_gen;
  buf_element_t *buf;
  int err;

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {
    this->video_fifo = video_fifo;
    this->audio_fifo = audio_fifo;

    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send new pts */
    xine_demux_control_newpts(this->xine, 0, 0);

    /* send init info to decoders */
    if (this->audio_fifo) {
      this->wave.wFormatTag = 1;
      this->wave.nChannels = 1;
      this->wave.nSamplesPerSec = 22050;
      this->wave.wBitsPerSample = 16;
      this->wave.nBlockAlign = (this->wave.wBitsPerSample / 8) * this->wave.nChannels;
      this->wave.nAvgBytesPerSec = this->wave.nBlockAlign * this->wave.nSamplesPerSec;

      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_LPCM_LE;
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = this->wave.nSamplesPerSec;
      buf->decoder_info[2] = this->wave.wBitsPerSample;
      buf->decoder_info[3] = this->wave.nChannels;
      buf->content = (void *)&this->wave;
      buf->size = sizeof(this->wave);
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_mve_loop, this)) != 0) {
      printf ("demux_wc3mve: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_mve_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  /* MVE files are not meant to be seekable; don't even bother */

  return 0;
}

static void demux_mve_stop (demux_plugin_t *this_gen) {

  demux_mve_t *this = (demux_mve_t *) this_gen;
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

static void demux_mve_close (demux_plugin_t *this) {
  free(this);
}

static int demux_mve_get_status (demux_plugin_t *this_gen) {
  demux_mve_t *this = (demux_mve_t *) this_gen;

  return this->status;
}

static char *demux_mve_get_id(void) {
  return "WC3 MOVIE";
}

static int demux_mve_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static char *demux_mve_get_mimetypes(void) {
  return NULL;
}


demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {
  demux_mve_t *this;

  if (iface != 10) {
    printf (_("demux_wc3movie: plugin doesn't support plugin API version %d.\n"
              "                this means there's a version mismatch between xine and this "
              "                demuxer plugin.\nInstalling current demux plugins should help.\n"),
            iface);
    return NULL;
  }

  this         = (demux_mve_t *) xine_xmalloc(sizeof(demux_mve_t));
  this->config = xine->config;
  this->xine   = xine;

  (void *) this->config->register_string(this->config,
                                         "mrl.ends_mve", VALID_ENDS,
                                         _("valid mrls ending for mve demuxer"),                                         NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_mve_open;
  this->demux_plugin.start             = demux_mve_start;
  this->demux_plugin.seek              = demux_mve_seek;
  this->demux_plugin.stop              = demux_mve_stop;
  this->demux_plugin.close             = demux_mve_close;
  this->demux_plugin.get_status        = demux_mve_get_status;
  this->demux_plugin.get_identifier    = demux_mve_get_id;
  this->demux_plugin.get_stream_length = demux_mve_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_mve_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init(&this->mutex, NULL);

  return &this->demux_plugin;
}
