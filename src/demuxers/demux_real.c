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
 * Real Media File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Real file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_real.c,v 1.4 2002/10/12 17:11:59 jkeil Exp $
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

#define RMF_TAG   FOURCC_TAG('.', 'R', 'M', 'F')
#define PROP_TAG  FOURCC_TAG('P', 'R', 'O', 'P')
#define MDPR_TAG  FOURCC_TAG('M', 'D', 'P', 'R')
#define CONT_TAG  FOURCC_TAG('C', 'O', 'N', 'T')
#define DATA_TAG  FOURCC_TAG('D', 'A', 'T', 'A')
#define INDX_TAG  FOURCC_TAG('I', 'N', 'D', 'X')

#define PREAMBLE_SIZE 8
#define REAL_SIGNATURE_SIZE 4
#define DATA_CHUNK_HEADER_SIZE 10
#define DATA_PACKET_HEADER_SIZE 12

#define PN_KEYFRAME_FLAG 0x0002

#define VALID_ENDS   "rm,ra"

typedef struct {
  int stream;
  int64_t offset;
  unsigned int size;
  int64_t pts;
  int keyframe;
} real_packet;

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
  unsigned int         duration;

  unsigned int         video_type;
  unsigned int         audio_type;

  unsigned int         video_width;
  unsigned int         video_height;
  unsigned int         audio_channels;
  unsigned int         audio_sample_rate;
  unsigned int         audio_bits;

  unsigned int         packet_count;
  unsigned int         current_packet;
  real_packet         *packets;

  unsigned int         current_data_chunk_packet_count;
  unsigned int         next_data_chunk_offset;

} demux_real_t ;

static void *demux_real_loop (void *this_gen) {

  demux_real_t *this = (demux_real_t *) this_gen;
  buf_element_t *buf = NULL;
  char preamble[PREAMBLE_SIZE];
  unsigned char data_chunk_header[DATA_CHUNK_HEADER_SIZE];
  char header[DATA_PACKET_HEADER_SIZE];

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

      /* load a header from wherever the stream happens to be pointing */
      if (this->input->read(this->input, header, DATA_PACKET_HEADER_SIZE) !=
        DATA_PACKET_HEADER_SIZE) {
        this->status = DEMUX_FINISHED;
        break;
      }

      /* log the packet information */
      this->packets[this->current_packet].stream = BE_16(&header[4]);
      this->packets[this->current_packet].offset = 
        this->input->get_current_pos(this->input);
      this->packets[this->current_packet].size = 
        BE_16(&header[2]) - DATA_PACKET_HEADER_SIZE;
      this->packets[this->current_packet].pts = 
        BE_32(&header[6]);
      this->packets[this->current_packet].pts *= 90;
      this->packets[this->current_packet].keyframe =
        (header[11] & PN_KEYFRAME_FLAG);

printf ("packet %d: stream %d, 0x%X bytes @ %llX, pts = %lld%s\n",
  this->current_packet,
  this->packets[this->current_packet].stream,
  this->packets[this->current_packet].size,
  this->packets[this->current_packet].offset,
  this->packets[this->current_packet].pts,
  (this->packets[this->current_packet].keyframe) ? ", keyframe" : "");



this->input->seek(this->input, this->packets[this->current_packet].size,
  SEEK_CUR);

      this->current_packet++;
      this->current_data_chunk_packet_count--;

      /* check if it's time to reload */
      if (!this->current_data_chunk_packet_count && 
        this->next_data_chunk_offset) {

        /* seek to the next DATA chunk offset */
        this->input->seek(this->input, this->next_data_chunk_offset, SEEK_SET);

        /* load the DATA chunk preamble */
        if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
          PREAMBLE_SIZE) {
          this->status = DEMUX_FINISHED;
          break;
        }

        /* load the rest of the DATA chunk header */
        if (this->input->read(this->input, data_chunk_header, 
          DATA_CHUNK_HEADER_SIZE) != DATA_CHUNK_HEADER_SIZE) {
          this->status = DEMUX_FINISHED;
          break;
        }
printf ("**** found next DATA tag\n");
        this->current_data_chunk_packet_count = BE_32(&data_chunk_header[2]);
        this->next_data_chunk_offset = BE_32(&data_chunk_header[6]);
      }

if (!this->current_data_chunk_packet_count) {
  this->status = DEMUX_FINISHED;
  break;
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

  printf ("demux_real: demux loop finished (status: %d)\n",
          this->status);

  /* seek back to the beginning of the data in preparation for another
   * start */
  this->input->seek(this->input, this->data_start, SEEK_SET);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;

  pthread_mutex_unlock( &this->mutex );

  return NULL;
}

static int load_real_and_send_headers(demux_real_t *this) {

  char preamble[PREAMBLE_SIZE];
  unsigned int chunk_type = 0;
  unsigned int chunk_size;
  unsigned char *chunk_buffer;
  int field_size;
  int stream_ptr;
  unsigned char data_chunk_header[DATA_CHUNK_HEADER_SIZE];

  pthread_mutex_lock( &this->mutex );

  this->video_fifo = this->xine->video_fifo;
  this->audio_fifo = this->xine->audio_fifo;

  this->status = DEMUX_OK;
  this->data_start = 0;
  this->data_size = 0;
  this->packets = NULL;
  this->current_packet = 0;

  /* skip to the start of the first chunk (the first chunk is 0x12 bytes
   * long) and start traversing */
  this->input->seek(this->input, 0x12, SEEK_SET);

  /* iterate through chunks and gather information until the first DATA
   * chunk is located */
  while (chunk_type != DATA_TAG) {

    if (this->input->read(this->input, preamble, PREAMBLE_SIZE) != 
      PREAMBLE_SIZE) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_CANNOT_HANDLE;
    }
    chunk_type = BE_32(&preamble[0]);
    chunk_size = BE_32(&preamble[4]);

    switch (chunk_type) {

    case PROP_TAG:
    case MDPR_TAG:
    case CONT_TAG:

      chunk_size -= PREAMBLE_SIZE;
      chunk_buffer = xine_xmalloc(chunk_size);
      if (this->input->read(this->input, chunk_buffer, chunk_size) != 
        chunk_size) {
        free(chunk_buffer);
        this->status = DEMUX_FINISHED;
        pthread_mutex_unlock(&this->mutex);
        return DEMUX_CANNOT_HANDLE;
      }

      if (chunk_type == PROP_TAG) {

        this->packet_count = BE_32(&chunk_buffer[18]);
        this->duration = BE_32(&chunk_buffer[22]);
        this->data_start = BE_32(&chunk_buffer[34]);

      } else if (chunk_type == MDPR_TAG) {


      } else if (chunk_type == CONT_TAG) {

        stream_ptr = 2;

        /* load the title string */
        field_size = BE_16(&chunk_buffer[stream_ptr]);
        stream_ptr += 2;
        this->xine->meta_info[XINE_META_INFO_TITLE] =
          xine_xmalloc(field_size + 1);
        strncpy(this->xine->meta_info[XINE_META_INFO_TITLE],
          &chunk_buffer[stream_ptr], field_size);
        this->xine->meta_info[XINE_META_INFO_TITLE][field_size] = '\0';
        stream_ptr += field_size;

        /* load the author string */
        field_size = BE_16(&chunk_buffer[stream_ptr]);
        stream_ptr += 2;
        this->xine->meta_info[XINE_META_INFO_ARTIST] =
          xine_xmalloc(field_size + 1);
        strncpy(this->xine->meta_info[XINE_META_INFO_ARTIST],
          &chunk_buffer[stream_ptr], field_size);
        this->xine->meta_info[XINE_META_INFO_ARTIST][field_size] = '\0';
        stream_ptr += field_size;

        /* load the copyright string as the year */
        field_size = BE_16(&chunk_buffer[stream_ptr]);
        stream_ptr += 2;
        this->xine->meta_info[XINE_META_INFO_YEAR] =
          xine_xmalloc(field_size + 1);
        strncpy(this->xine->meta_info[XINE_META_INFO_YEAR],
          &chunk_buffer[stream_ptr], field_size);
        this->xine->meta_info[XINE_META_INFO_YEAR][field_size] = '\0';
        stream_ptr += field_size;

        /* load the comment string */
        field_size = BE_16(&chunk_buffer[stream_ptr]);
        stream_ptr += 2;
        this->xine->meta_info[XINE_META_INFO_COMMENT] =
          xine_xmalloc(field_size + 1);
        strncpy(this->xine->meta_info[XINE_META_INFO_COMMENT],
          &chunk_buffer[stream_ptr], field_size);
        this->xine->meta_info[XINE_META_INFO_COMMENT][field_size] = '\0';
        stream_ptr += field_size;
      }

      free(chunk_buffer);
      break;

    case DATA_TAG:
      if (this->input->read(this->input, data_chunk_header, 
        DATA_CHUNK_HEADER_SIZE) != DATA_CHUNK_HEADER_SIZE) {
        this->status = DEMUX_FINISHED;
        pthread_mutex_unlock(&this->mutex);
        return DEMUX_CANNOT_HANDLE;
      }
      this->current_data_chunk_packet_count = BE_32(&data_chunk_header[2]);
      this->next_data_chunk_offset = BE_32(&data_chunk_header[6]);
      break;

    default:
      /* this should not occur, but in case it does, skip the chunk */
      this->input->seek(this->input, chunk_size - PREAMBLE_SIZE, SEEK_CUR);
      break;

    }

  }

  /* allocate space for the packet list */
  this->packets = xine_xmalloc(this->packet_count * sizeof(real_packet));

  xine_demux_control_headers_done (this->xine);

  pthread_mutex_unlock( &this->mutex );

  return DEMUX_CAN_HANDLE;
}

static int demux_real_open(demux_plugin_t *this_gen, input_plugin_t *input,
                           int stage) {
  demux_real_t *this = (demux_real_t *) this_gen;
  unsigned char signature[REAL_SIGNATURE_SIZE];

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, signature, REAL_SIGNATURE_SIZE) !=
      REAL_SIGNATURE_SIZE)
      return DEMUX_CANNOT_HANDLE;

    if (BE_32(signature) == RMF_TAG)
      return load_real_and_send_headers(this);

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
                                                            "mrl.ends_real", VALID_ENDS,
                                                            _("valid mrls ending for real demuxer"),
                                                            NULL, 20, NULL, NULL)));
    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

      while(*m == ' ' || *m == '\t') m++;

      if(!strcasecmp((suffix + 1), m))
        return load_real_and_send_headers(this);
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

static int demux_real_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time);

static int demux_real_start (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {

  demux_real_t *this = (demux_real_t *) this_gen;
  buf_element_t *buf;
  int err;

  pthread_mutex_lock(&this->mutex);

  demux_real_seek(this_gen, start_pos, start_time);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {

    /* send vital stats */
    xine_log (this->xine, XINE_LOG_MSG,
      _("demux_real: Real media file, running time: %d min, %d sec\n"),
      this->duration / 1000 / 60,
      this->duration / 1000 % 60);



    /* send start buffers */
//    xine_demux_control_start(this->xine);

    /* send new pts */
//    xine_demux_control_newpts(this->xine, 0, 0);

    /* send init info to decoders */


    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_real_loop, this)) != 0) {
      printf ("demux_real: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_real_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {

  return 0;
}

static void demux_real_stop (demux_plugin_t *this_gen) {

  demux_real_t *this = (demux_real_t *) this_gen;
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

static void demux_real_dispose (demux_plugin_t *this_gen) {

  demux_real_t *this = (demux_real_t *) this_gen;

  free(this->packets);
  free(this);
}

static int demux_real_get_status (demux_plugin_t *this_gen) {
  demux_real_t *this = (demux_real_t *) this_gen;

  return this->status;
}

static char *demux_real_get_id(void) {
  return "RealMedia";
}

static int demux_real_get_stream_length (demux_plugin_t *this_gen) {

  demux_real_t *this = (demux_real_t *) this_gen;

  /* duration is stored in the file as milliseconds */
  return this->duration / 1000;
}

static char *demux_real_get_mimetypes(void) {
  return NULL;
}


static void *init_demuxer_plugin(xine_t *xine, void *data) {

  demux_real_t *this;

  this         = xine_xmalloc (sizeof (demux_real_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config,
                                        "mrl.ends_real", VALID_ENDS,
                                        _("valid mrls ending for real demuxer"),
                                        NULL, 20, NULL, NULL);

  this->demux_plugin.open              = demux_real_open;
  this->demux_plugin.start             = demux_real_start;
  this->demux_plugin.seek              = demux_real_seek;
  this->demux_plugin.stop              = demux_real_stop;
  this->demux_plugin.dispose           = demux_real_dispose;
  this->demux_plugin.get_status        = demux_real_get_status;
  this->demux_plugin.get_identifier    = demux_real_get_id;
  this->demux_plugin.get_stream_length = demux_real_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_real_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init(&this->mutex, NULL);

  return &this->demux_plugin;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 11, "real", XINE_VERSION_CODE, NULL, init_demuxer_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
