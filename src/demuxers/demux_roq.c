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
 * RoQ File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the RoQ file format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 *
 * $Id: demux_roq.c,v 1.5 2002/06/12 12:22:33 f1rmb Exp $
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

#define RoQ_MAGIC_NUMBER 0x1084
#define RoQ_CHUNK_PREAMBLE_SIZE 8
#define RoQ_AUDIO_SAMPLE_RATE 22050

#define RoQ_INFO           0x1001
#define RoQ_QUAD_CODEBOOK  0x1002
#define RoQ_QUAD_VQ        0x1011
#define RoQ_SOUND_MONO     0x1020
#define RoQ_SOUND_STEREO   0x1021

#define VALID_ENDS   "roq"

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

  unsigned int         width;
  unsigned int         height;
  unsigned int         audio_channels;
} demux_roq_t ;

static void *demux_roq_loop (void *this_gen) {

  demux_roq_t *this = (demux_roq_t *) this_gen;
  buf_element_t *buf = NULL;
  char preamble[RoQ_CHUNK_PREAMBLE_SIZE];
  unsigned int chunk_type;
  unsigned int chunk_size;
  int64_t video_pts_counter = 0;
  int64_t audio_pts;
  unsigned int audio_byte_count = 0;
  off_t current_file_pos;

  pthread_mutex_lock( &this->mutex );

  /* start after the signature chunk */
  this->input->seek(this->input, RoQ_CHUNK_PREAMBLE_SIZE, SEEK_SET);

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      /* fetch the next preamble */
      if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != 
        RoQ_CHUNK_PREAMBLE_SIZE) {
        this->status = DEMUX_FINISHED;
        break;
      }
      chunk_type = LE_16(&preamble[0]);
      chunk_size = LE_32(&preamble[2]);
//printf ("loading chunk type %X, %X bytes\n", chunk_type, chunk_size);

      /* if the chunk is an audio chunk, route it to the audio fifo */
      if ((chunk_type == RoQ_SOUND_MONO) || (chunk_type == RoQ_SOUND_STEREO)) {
        /* rewind over the preamble */
        this->input->seek(this->input, -RoQ_CHUNK_PREAMBLE_SIZE, SEEK_CUR);

        /* adjust the chunk size */
        chunk_size += RoQ_CHUNK_PREAMBLE_SIZE;


        if( this->audio_fifo ) {
        
          /* do this calculation carefully because I can't trust the
           * 64-bit numerical manipulation */
          audio_pts = audio_byte_count;
          audio_pts *= 90000;
          audio_pts /= (RoQ_AUDIO_SAMPLE_RATE * this->audio_channels);
          audio_byte_count += chunk_size - 8;  /* do not count the preamble */
         
          current_file_pos = this->input->get_current_pos(this->input);

          /* packetize the audio */
          while (chunk_size) {
            buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
            buf->content = buf->mem;
            buf->type = BUF_AUDIO_ROQ;
            buf->input_pos = current_file_pos;
            buf->decoder_flags = 0;
            buf->pts = audio_pts;

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
            this->audio_fifo->put(this->audio_fifo, buf);
//printf ("audio packet pts = %lld\n", buf->pts);
          }
        } else {
          /* no audio -> skip chunk */
          this->input->seek(this->input, chunk_size, SEEK_CUR);
        }
      } else if (chunk_type == RoQ_INFO) {
        /* skip 8 bytes */
        this->input->seek(this->input, 8, SEEK_CUR);
      } else if ((chunk_type == RoQ_QUAD_CODEBOOK) ||
        (chunk_type == RoQ_QUAD_VQ)) {

        current_file_pos = this->input->get_current_pos(this->input) -
          RoQ_CHUNK_PREAMBLE_SIZE;

        /* if the chunk is video, check if it is a codebook */
        if (chunk_type == RoQ_QUAD_CODEBOOK) {
          /* if it is, figure in the size of the next VQ chunk, too */
          this->input->seek(this->input, chunk_size, SEEK_CUR);
          if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != 
            RoQ_CHUNK_PREAMBLE_SIZE) {
            this->status = DEMUX_FINISHED;
            break;
          }

          /* rewind to the start of the codebook chunk */
          this->input->seek(this->input, current_file_pos, SEEK_SET);

          /* figure out the total video chunk size */
          chunk_size += (2 * RoQ_CHUNK_PREAMBLE_SIZE) + LE_32(&preamble[2]);
        }

        /* packetize the video chunk and route it to the video fifo */
//printf ("total video chunk size = %X\n", chunk_size);
        while (chunk_size) {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->content = buf->mem;
          buf->type = BUF_VIDEO_ROQ;
          buf->input_pos = current_file_pos;
          buf->decoder_flags = 0;
          buf->pts = video_pts_counter;

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
//printf ("loaded video packet with %X bytes, pts = %lld\n",
//  buf->size, buf->pts);
        }
        video_pts_counter += this->frame_pts_inc;
      } else {
        printf ("demux_roq: encountered bad chunk type: %d\n", chunk_type);
      }

      /* someone may want to interrupt us */
      pthread_mutex_unlock(&this->mutex);
      pthread_mutex_lock(&this->mutex);
    }
    
    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->video_fifo->size(this->video_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }
  } while (this->status == DEMUX_OK);

  printf ("demux_roq: demux loop finished (status: %d)\n",
          this->status);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );

  return NULL;
}

static int demux_roq_open(demux_plugin_t *this_gen, input_plugin_t *input,
                          int stage) {
  demux_roq_t *this = (demux_roq_t *) this_gen;
  char preamble[RoQ_CHUNK_PREAMBLE_SIZE];

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != 
      RoQ_CHUNK_PREAMBLE_SIZE)
      return DEMUX_CANNOT_HANDLE;

    /* check for the RoQ magic numbers */
    if ((LE_16(&preamble[0]) == RoQ_MAGIC_NUMBER) &&
        (LE_32(&preamble[2]) == 0xFFFFFFFF))
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
							    "mrl.ends_roq", VALID_ENDS,
							    _("valid mrls ending for roq demuxer"),
							    NULL, NULL, NULL)));
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

static int demux_roq_start (demux_plugin_t *this_gen,
                             fifo_buffer_t *video_fifo,
                             fifo_buffer_t *audio_fifo,
                             off_t start_pos, int start_time) {

  demux_roq_t *this = (demux_roq_t *) this_gen;
  buf_element_t *buf;
  int err;
  char preamble[RoQ_CHUNK_PREAMBLE_SIZE];
  int i;
  unsigned int chunk_type;
  unsigned int chunk_size;

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {
    this->video_fifo = video_fifo;
    this->audio_fifo = audio_fifo;

    this->input->seek(this->input, 0, SEEK_SET);
    if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != 
      RoQ_CHUNK_PREAMBLE_SIZE) {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    this->width = this->height = 0;
    this->audio_channels = 0;  /* assume no audio at first */

    /*
     * RoQ files enjoy a constant framerate; pts calculation:
     *
     *   xine pts     frame #
     *   --------  =  -------  =>  xine pts = 90000 * frame # / fps
     *    90000         fps
     *
     * therefore, the frame pts increment is 90000 / fps
     */
    this->fps = LE_16(&preamble[6]);
    this->frame_pts_inc = 90000 / this->fps;

    /* iterate through the first 2 seconds worth of chunks searching for
     * the RoQ_INFO chunk and an audio chunk */
    i = this->fps * 2;
    while (i-- > 0) {
      /* if this read fails, then maybe it's just a really small RoQ file
       * (even less than 2 seconds) */
      if (this->input->read(this->input, preamble, RoQ_CHUNK_PREAMBLE_SIZE) != 
        RoQ_CHUNK_PREAMBLE_SIZE)
        break;
      chunk_type = LE_16(&preamble[0]);
      chunk_size = LE_32(&preamble[2]);

      if (chunk_type == RoQ_INFO) {
        /* fetch the width and height; reuse the preamble bytes */
        if (this->input->read(this->input, preamble, 8) != 8)
          break;

        this->width = LE_16(&preamble[0]);
        this->height = LE_16(&preamble[2]);

        /* if an audio chunk was already found, search is done */
        if (this->audio_channels)
          break;

        /* prep the size for a seek */
        chunk_size -= 8;
      } else {
        /* if it was an audio chunk and the info chunk has already been
         * found (as indicated by width and height) then break */
        if (chunk_type == RoQ_SOUND_MONO) {
          this->audio_channels = 1;
          if (this->width && this->height)
            break;
        } else if (chunk_type == RoQ_SOUND_STEREO) {
          this->audio_channels = 2;
          if (this->width && this->height)
            break;
        }
      }

      /* skip the rest of the chunk */
      this->input->seek(this->input, chunk_size, SEEK_CUR);
    }

    /* after all is said and done, if there is a width and a height,
     * regard it as being a valid file and reset to the first chunk */
    if (this->width && this->height) {
      this->input->seek(this->input, 8, SEEK_SET);
    } else {
      this->status = DEMUX_FINISHED;
      return this->status;
    }

    /* print vital stats */
    xine_log (this->xine, XINE_LOG_FORMAT,
      _("demux_roq: RoQ file, video is %dx%d, %d frames/sec\n"),
      this->width, this->height, this->fps);
    if (this->audio_channels)
      xine_log (this->xine, XINE_LOG_FORMAT,
        _("demux_roq: 16-bit, 22050 Hz %s RoQ DPCM audio\n"),
        (this->audio_channels == 1) ? "monaural" : "stereo");

    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send new pts */
    xine_demux_control_newpts(this->xine, 0, 0);

    /* send init info to decoders */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->content = buf->mem;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->frame_pts_inc;  /* initial video_step */
    /* really be a rebel: No structure at all, just put the video width
     * and height straight into the buffer, BE_16 format */
    buf->content[0] = (this->width >> 8) & 0xFF;
    buf->content[1] = (this->width >> 0) & 0xFF;
    buf->content[2] = (this->height >> 8) & 0xFF;
    buf->content[3] = (this->height >> 0) & 0xFF;
    buf->size = 4;
    buf->type = BUF_VIDEO_ROQ;
    this->video_fifo->put (this->video_fifo, buf);

    if (this->audio_fifo && this->audio_channels) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->content = buf->mem;
      buf->type = BUF_AUDIO_ROQ;
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = RoQ_AUDIO_SAMPLE_RATE;
      buf->decoder_info[2] = 16;
      buf->decoder_info[3] = this->audio_channels;
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_roq_loop, this)) != 0) {
      printf ("demux_roq: can't create new thread (%s)\n", strerror(err));
      abort();
    }

    this->status = DEMUX_OK;
  }

  pthread_mutex_unlock(&this->mutex);

  return this->status;
}

static int demux_roq_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {

  /* RoQ files are not meant to be seekable; don't even bother */

  return 0;
}

static void demux_roq_stop (demux_plugin_t *this_gen) {

  demux_roq_t *this = (demux_roq_t *) this_gen;
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

static void demux_roq_close (demux_plugin_t *this) {

}

static int demux_roq_get_status (demux_plugin_t *this_gen) {
  demux_roq_t *this = (demux_roq_t *) this_gen;

  return this->status;
}

static char *demux_roq_get_id(void) {
  return "RoQ";
}

static int demux_roq_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static char *demux_roq_get_mimetypes(void) {
  return NULL;
}


demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {
  demux_roq_t *this;

  if (iface != 9) {
    printf (_("demux_roq: plugin doesn't support plugin API version %d.\n"
	      "           this means there's a version mismatch between xine and this "
	      "           demuxer plugin.\nInstalling current demux plugins should help.\n"),
            iface);
    return NULL;
  }

  this         = (demux_roq_t *) xine_xmalloc(sizeof(demux_roq_t));
  this->config = xine->config;
  this->xine   = xine;

  (void *) this->config->register_string(this->config,
					 "mrl.ends_roq", VALID_ENDS,
					 _("valid mrls ending for roq demuxer"),
					 NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_roq_open;
  this->demux_plugin.start             = demux_roq_start;
  this->demux_plugin.seek              = demux_roq_seek;
  this->demux_plugin.stop              = demux_roq_stop;
  this->demux_plugin.close             = demux_roq_close;
  this->demux_plugin.get_status        = demux_roq_get_status;
  this->demux_plugin.get_identifier    = demux_roq_get_id;
  this->demux_plugin.get_stream_length = demux_roq_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_roq_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init(&this->mutex, NULL);

  return &this->demux_plugin;
}

