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
 * SMJPEG File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information on the SMJPEG file format, visit:
 *   http://www.lokigames.com/development/smjpeg.php3
 *
 * $Id: demux_smjpeg.c,v 1.20 2002/10/26 23:23:15 tmmm Exp $
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

#define FOURCC_TAG( ch0, ch1, ch2, ch3 )                                \
        ( (long)(unsigned char)(ch3) | ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | ( (long)(unsigned char)(ch0) << 24 ) )

#define _TXT_TAG FOURCC_TAG('_', 'T', 'X', 'T')
#define _SND_TAG FOURCC_TAG('_', 'S', 'N', 'D')
#define _VID_TAG FOURCC_TAG('_', 'V', 'I', 'D')
#define HEND_TAG FOURCC_TAG('H', 'E', 'N', 'D')
#define sndD_TAG FOURCC_TAG('s', 'n', 'd', 'D')
#define vidD_TAG FOURCC_TAG('v', 'i', 'd', 'D')
#define APCM_TAG FOURCC_TAG('A', 'P', 'C', 'M')

#define VALID_ENDS   "mjpg"

#define SMJPEG_SIGNATURE_SIZE 8
/* 16 is the max size of a header chunk (the video header) */
#define SMJPEG_VIDEO_HEADER_SIZE 16
#define SMJPEG_AUDIO_HEADER_SIZE 12
#define SMJPEG_HEADER_CHUNK_MAX_SIZE SMJPEG_VIDEO_HEADER_SIZE
#define SMJPEG_CHUNK_PREAMBLE_SIZE 12

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;
  int                  send_end_buffers;

  off_t                input_length;
  int                  status;

  /* when this flag is set, demuxer only dispatches audio samples until it
   * encounters a video keyframe, then it starts sending every frame again */
  int                  waiting_for_keyframe;

  /* video information */
  unsigned int         video_type;
  xine_bmiheader       bih;

  /* audio information */
  unsigned int         audio_codec;
  unsigned int         audio_type;
  unsigned int         audio_sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;

  /* playback information */
  unsigned int         duration;  /* duration in milliseconds */

  char                 last_mrl[1024];
} demux_smjpeg_t;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_smjpeg_class_t;

/* returns 1 if the SMJPEG file was opened successfully, 0 otherwise */
static int open_smjpeg_file(demux_smjpeg_t *this) {

  unsigned int chunk_tag;
  unsigned char signature[8];
  unsigned char header_chunk[SMJPEG_HEADER_CHUNK_MAX_SIZE];

  /* initial state: no video and no audio (until headers found) */
  this->video_type = this->audio_type = 0;
  this->input_length = this->input->get_length (this->input);

  this->input->seek(this->input, 0, SEEK_SET);
  if (this->input->read(this->input, signature, SMJPEG_SIGNATURE_SIZE) !=
    SMJPEG_SIGNATURE_SIZE)
    return 0;

  /* check for the SMJPEG signature */
  if ((signature[0] != 0x00) ||
      (signature[1] != 0x0A) ||
      (signature[2] != 'S') ||
      (signature[3] != 'M') ||
      (signature[4] != 'J') ||
      (signature[5] != 'P') ||
      (signature[6] != 'E') ||
      (signature[7] != 'G'))
    return 0;

  /* jump over the version to the duration */
  this->input->seek(this->input, 4, SEEK_CUR);
  if (this->input->read(this->input, header_chunk, 4) != 4)
    return 0;
  this->duration = BE_32(&header_chunk[0]);

  /* traverse the header chunks until the HEND tag is found */
  chunk_tag = 0;
  while (chunk_tag != HEND_TAG) {

    if (this->input->read(this->input, header_chunk, 4) != 4)
      return 0;
    chunk_tag = BE_32(&header_chunk[0]);

    switch(chunk_tag) {

    case HEND_TAG:
      /* this indicates the end of the header; do nothing and fall
       * out of the loop on the next iteration */
      break;

    case _VID_TAG:
      if (this->input->read(this->input, header_chunk, 
        SMJPEG_VIDEO_HEADER_SIZE) != SMJPEG_VIDEO_HEADER_SIZE)
        return 0;

      this->bih.biWidth = BE_16(&header_chunk[8]);
      this->bih.biHeight = BE_16(&header_chunk[10]);
      this->bih.biCompression = *(uint32_t *)&header_chunk[12];
      this->video_type = fourcc_to_buf_video(this->bih.biCompression);
      break;

    case _SND_TAG:
      if (this->input->read(this->input, header_chunk, 
        SMJPEG_AUDIO_HEADER_SIZE) != SMJPEG_AUDIO_HEADER_SIZE)
        return 0;

      this->audio_sample_rate = BE_16(&header_chunk[4]);
      this->audio_bits = header_chunk[6];
      this->audio_channels = header_chunk[7];
      /* ADPCM in these files is ID'd by 'APCM' which is used in other
       * files to denote a slightly different format; thus, use the
       * following special case */
      if (BE_32(&header_chunk[8]) == APCM_TAG) {
        this->audio_codec = be2me_32(APCM_TAG);
        this->audio_type = BUF_AUDIO_SMJPEG_IMA;
      } else {
        this->audio_codec = *(uint32_t *)&header_chunk[8];
        this->audio_type = formattag_to_buf_audio(this->audio_codec);
      }
      break;

    default:
      /* for all other chunk types, read the length and skip the rest
       * of the chunk */
      if (this->input->read(this->input, header_chunk, 4) != 4)
        return 0;
      this->input->seek(this->input, BE_32(&header_chunk[0]), SEEK_CUR);
      break;
    }
  }

  if(!this->video_type)
    xine_report_codec(this->stream, XINE_CODEC_VIDEO,
      this->bih.biCompression, 0, 0);

  if(!this->audio_type && this->audio_codec)
    xine_report_codec(this->stream, XINE_CODEC_AUDIO, 
    this->audio_codec, 0, 0);

  return 1;
}

static void *demux_smjpeg_loop (void *this_gen) {

  demux_smjpeg_t *this = (demux_smjpeg_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int chunk_tag;
  int64_t pts;
  unsigned int remaining_sample_bytes;
  unsigned char preamble[SMJPEG_CHUNK_PREAMBLE_SIZE];
  off_t current_file_pos;
  int64_t last_frame_pts = 0;
  unsigned int audio_frame_count = 0;

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

      /* load the next sample */
      current_file_pos = this->input->get_current_pos(this->input);
      if (this->input->read(this->input, preamble, 
        SMJPEG_CHUNK_PREAMBLE_SIZE) != SMJPEG_CHUNK_PREAMBLE_SIZE) {
        this->status = DEMUX_FINISHED;
        continue;  /* skip to next while() iteration to bail out */
      }

      chunk_tag = BE_32(&preamble[0]);
      remaining_sample_bytes = BE_32(&preamble[8]);

      /*
       * Each sample has an absolute timestamp in millisecond units:
       *
       *    xine pts     timestamp (ms)
       *    --------  =  --------------
       *      90000           1000
       *
       * therefore, xine pts = timestamp * 90000 / 1000 => timestamp * 90
       *
       * However, millisecond timestamps are not completely accurate
       * for the audio samples. These audio chunks usually have 256 bytes,
       * or 512 nibbles, which corresponds to 512 samples.
       *
       *   512 samples * (1 sec / 22050 samples) * (1000 ms / 1 sec)
       *     = 23.2 ms
       *
       * where the audio samples claim that each chunk is 23 ms long.
       * Therefore, manually compute the pts values for the audio samples.
       */
      if (chunk_tag == sndD_TAG) {
        pts = audio_frame_count;
        pts *= 90000;
        pts /= (this->audio_sample_rate * this->audio_channels);
        audio_frame_count += ((remaining_sample_bytes - 4) * 2);
      } else {
        pts = BE_32(&preamble[4]);
        pts *= 90;
      }

      /* break up the data into packets and dispatch them */
      if (((chunk_tag == sndD_TAG) && this->audio_fifo && this->audio_type) ||
        (chunk_tag == vidD_TAG)) {

        while (remaining_sample_bytes) {
          if (chunk_tag == sndD_TAG) {
            buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
            buf->type = this->audio_type;
          } else {
            buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
            buf->type = this->video_type;
          }

          buf->input_pos = current_file_pos;
          buf->input_length = this->input_length;
          buf->input_time = pts / 90000;
          buf->pts = pts;

          if (last_frame_pts) {
            buf->decoder_flags |= BUF_FLAG_FRAMERATE;
            buf->decoder_info[0] = buf->pts - last_frame_pts;
          }

          if (remaining_sample_bytes > buf->max_size)
            buf->size = buf->max_size;
          else
            buf->size = remaining_sample_bytes;
          remaining_sample_bytes -= buf->size;

          if (this->input->read(this->input, buf->content, buf->size) !=
            buf->size) {
            this->status = DEMUX_FINISHED;
            break;
          }

          /* every frame is a keyframe */
          buf->decoder_flags |= BUF_FLAG_KEYFRAME;
          if (!remaining_sample_bytes)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;

          if (chunk_tag == sndD_TAG)
            this->audio_fifo->put(this->audio_fifo, buf);
          else
            this->video_fifo->put(this->video_fifo, buf);
        }

      } else {

        /* skip the chunk if it can't be handled */
        this->input->seek(this->input, remaining_sample_bytes, SEEK_CUR);

      }

      if (chunk_tag == vidD_TAG)
        last_frame_pts = buf->pts;
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->video_fifo->size(this->video_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while (this->status == DEMUX_OK);

  printf ("demux_smjpeg: demux loop finished (status: %d)\n",
          this->status);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->stream, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock(&this->mutex);
  return NULL;
}

static void demux_smjpeg_send_headers(demux_plugin_t *this_gen) {

  demux_smjpeg_t *this = (demux_smjpeg_t *) this_gen;
  buf_element_t *buf;

  pthread_mutex_lock(&this->mutex);

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]  = this->bih.biWidth;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->bih.biHeight;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->audio_sample_rate;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->audio_bits;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->decoder_flags = BUF_FLAG_HEADER;
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = 3000;  /* initial video_step */
  memcpy(buf->content, &this->bih, sizeof(this->bih));
  buf->size = sizeof(this->bih);
  buf->type = this->video_type;
  this->video_fifo->put (this->video_fifo, buf);

  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->audio_sample_rate;
    buf->decoder_info[2] = this->audio_bits;
    buf->decoder_info[3] = this->audio_channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }

  xine_demux_control_headers_done (this->stream);

  pthread_mutex_unlock (&this->mutex);
}

static int demux_smjpeg_start (demux_plugin_t *this_gen,
                               off_t start_pos, int start_time) {

  demux_smjpeg_t *this = (demux_smjpeg_t *) this_gen;
  int err;

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_smjpeg_loop, this)) != 0) {
      printf ("demux_smjpeg: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_smjpeg_seek (demux_plugin_t *this_gen,
                              off_t start_pos, int start_time) {

  /* SMJPEG files consist of a series of keyframes, but there is no
   * master index; in order to effectively seek, an index would have to be
   * built by traversing the file in advance. Therefore, don't bother
   * implementing the seek function. */

  return 0;
}

static void demux_smjpeg_stop (demux_plugin_t *this_gen) {

  demux_smjpeg_t *this = (demux_smjpeg_t *) this_gen;
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

  xine_demux_flush_engine(this->stream);

  xine_demux_control_end(this->stream, BUF_FLAG_END_USER);
}

static void demux_smjpeg_dispose (demux_plugin_t *this_gen) {
  demux_smjpeg_t *this = (demux_smjpeg_t *) this_gen;

  demux_smjpeg_stop(this_gen);

  pthread_mutex_destroy (&this->mutex);
  free(this);
}

static int demux_smjpeg_get_status (demux_plugin_t *this_gen) {
  demux_smjpeg_t *this = (demux_smjpeg_t *) this_gen;

  return this->status;
}

static int demux_smjpeg_get_stream_length (demux_plugin_t *this_gen) {
  demux_smjpeg_t *this = (demux_smjpeg_t *) this_gen;

  /* return total running time in seconds */
  return this->duration / 1000;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_smjpeg_t *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_smjpeg.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_smjpeg_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_smjpeg_send_headers;
  this->demux_plugin.start             = demux_smjpeg_start;
  this->demux_plugin.seek              = demux_smjpeg_seek;
  this->demux_plugin.stop              = demux_smjpeg_stop;
  this->demux_plugin.dispose           = demux_smjpeg_dispose;
  this->demux_plugin.get_status        = demux_smjpeg_get_status;
  this->demux_plugin.get_stream_length = demux_smjpeg_get_stream_length;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init (&this->mutex, NULL);

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:

    if (!open_smjpeg_file(this)) {
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

    if (strncasecmp (ending, ".mjpg", 5)) {
      free (this);
      return NULL;
    }

    if (!open_smjpeg_file(this)) {
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
    _("demux_smjpeg: SMJPEG file, running time: %d min, %d sec\n"),
    this->duration / 1000 / 60,
    this->duration / 1000 % 60);
  if (this->video_type)
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("demux_smjpeg: '%c%c%c%c' video @ %dx%d\n"),
      *((char *)&this->bih.biCompression + 0),
      *((char *)&this->bih.biCompression + 1),
      *((char *)&this->bih.biCompression + 2),
      *((char *)&this->bih.biCompression + 3),
      this->bih.biWidth,
      this->bih.biHeight);
  if (this->audio_type)
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("demux_smjpeg: '%c%c%c%c' audio @ %d Hz, %d bits, %d %s\n"),
      *((char *)&this->audio_codec + 0),
      *((char *)&this->audio_codec + 1),
      *((char *)&this->audio_codec + 2),
      *((char *)&this->audio_codec + 3),
      this->audio_sample_rate,
      this->audio_bits,
      this->audio_channels,
      ngettext("channel", "channels", this->audio_channels));

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "SMJPEG file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "SMJPEG";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "mjpg";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_smjpeg_class_t *this = (demux_smjpeg_class_t *) this_gen;

  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  demux_smjpeg_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_smjpeg_class_t));
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
  { PLUGIN_DEMUX, 14, "smjpeg", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
