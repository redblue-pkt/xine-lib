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
 * FILM (CPK) File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information on the FILM file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_film.c,v 1.19 2002/07/17 18:17:48 miguelfreitas Exp $
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

#define FOURCC_TAG( ch0, ch1, ch2, ch3 )                                \
        ( (long)(unsigned char)(ch3) | ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | ( (long)(unsigned char)(ch0) << 24 ) )

#define FILM_TAG FOURCC_TAG('F', 'I', 'L', 'M')
#define FDSC_TAG FOURCC_TAG('F', 'D', 'S', 'C')
#define STAB_TAG FOURCC_TAG('S', 'T', 'A', 'B')
#define CVID_TAG FOURCC_TAG('c', 'v', 'i', 'd')

#define VALID_ENDS   "cpk,cak,film"

typedef struct {
  off_t sample_offset;
  unsigned int sample_size;
  unsigned int syncinfo1;
  unsigned int syncinfo2;
  int64_t pts;
} film_sample_t;

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
  off_t                data_end;
  int                  status;

  /* when this flag is set, demuxer only dispatches audio samples until it
   * encounters a video keyframe, then it starts sending every frame again */
  int                  waiting_for_keyframe;

  char                 version[4];

  /* video information */
  unsigned int         video_codec;
  unsigned int         video_type;
  xine_bmiheader       bih;

  /* audio information */
  unsigned int         audio_type;
  unsigned int         sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;

  /* playback information */
  unsigned int         frequency;
  unsigned int         sample_count;
  film_sample_t       *sample_table;
  unsigned int         current_sample;
  unsigned int         last_sample;
  int                  total_time;
} demux_film_t ;

/* returns 1 if FILM file was opened successfully */
static int open_film_file(demux_film_t *film)
{
  unsigned char *film_header;
  unsigned int film_header_size;
  unsigned char scratch[16];
  unsigned int chunk_type;
  unsigned int chunk_size;
  unsigned int i, j;
  unsigned int audio_byte_count = 0;
  int64_t largest_pts = 0;

  /* initialize structure fields */
  film->bih.biWidth = 0;
  film->bih.biHeight = 0;
  film->video_codec = 0;
  film->sample_rate = 0;
  film->audio_bits = 0;
  film->audio_channels = 0;
  film->waiting_for_keyframe = 0;

  /* get the ending offset */
  film->data_end = film->input->get_length(film->input);

  /* reset the file */
  film->input->seek(film->input, 0, SEEK_SET);

  /* get the signature, header length and file version */
  if (film->input->read(film->input, scratch, 16) != 16) {
    return 0;
  }
  if (BE_32(&scratch[0]) != FILM_TAG) {
    xine_log(film->xine, XINE_LOG_FORMAT,
      _("demux_film: This is not a FILM file (why was it sent to this demuxer?\n"));
    return 0;
  }

  /* header size = header size - 16-byte FILM signature */
  film_header_size = BE_32(&scratch[4]) - 16;
  film_header = xine_xmalloc(film_header_size);
  if (!film_header)
    return 0;
  strncpy(film->version, &scratch[8], 4);

  /* load the rest of the FILM header */
  if (film->input->read(film->input, film_header, film_header_size) != 
    film_header_size) {
    return 0;
  }

  /* get the starting offset */
  film->data_start = film->input->get_current_pos(film->input);

  /* traverse the FILM header */
  i = 0;
  while (i < film_header_size) {
    chunk_type = BE_32(&film_header[i]);
    chunk_size = BE_32(&film_header[i + 4]);

    /* sanity check the chunk size */
    if (i + chunk_size > film_header_size) {
      xine_log(film->xine, XINE_LOG_FORMAT,
        _("invalid FILM chunk size\n"));
      return 0;
    }

    switch(chunk_type) {
    case FDSC_TAG:
      /* always fetch the video information */
      film->bih.biWidth = BE_32(&film_header[i + 16]);
      film->bih.biHeight = BE_32(&film_header[i + 12]);
      film->video_codec = BE_32(&film_header[i + 8]);
      if (film->video_codec == CVID_TAG)
        film->video_type = BUF_VIDEO_CINEPAK;
      else
        film->video_type = 0;

      /* fetch the audio information if the chunk size checks out */
      if (chunk_size == 32) {
        film->audio_channels = film_header[21];
        film->audio_bits = film_header[22];
        film->sample_rate = BE_16(&film_header[24]);
      } else {
        /* otherwise, make a few assumptions about the audio parms */
        film->audio_channels = 1;
        film->audio_bits = 8;
        film->sample_rate = 22050;
      }
      if (film->sample_rate)
        film->audio_type = BUF_AUDIO_LPCM_BE;
      else
        film->audio_type = 0;
      break;

    case STAB_TAG:
      /* load the sample table */
      if (film->sample_table)
        free(film->sample_table);
      film->frequency = BE_32(&film_header[i + 8]);
      film->sample_count = BE_32(&film_header[i + 12]);
      film->sample_table =
        xine_xmalloc(film->sample_count * sizeof(film_sample_t));
      for (j = 0; j < film->sample_count; j++) {
        film->sample_table[j].sample_offset = 
          BE_32(&film_header[(i + 16) + j * 16 + 0])
          + film_header_size + 16;
        film->sample_table[j].sample_size = 
          BE_32(&film_header[(i + 16) + j * 16 + 4]);
        film->sample_table[j].syncinfo1 = 
          BE_32(&film_header[(i + 16) + j * 16 + 8]);
        film->sample_table[j].syncinfo2 = 
          BE_32(&film_header[(i + 16) + j * 16 + 12]);

        /* figure out the pts */
        if (film->sample_table[j].syncinfo1 == 0xFFFFFFFF) {
          film->sample_table[j].pts = audio_byte_count;
          film->sample_table[j].pts *= 90000;
          film->sample_table[j].pts /= 
            (film->sample_rate * film->audio_channels * (film->audio_bits / 8));
          audio_byte_count += film->sample_table[j].sample_size;
        }
        else
          film->sample_table[j].pts = 
            (90000 * (film->sample_table[j].syncinfo1 & 0x7FFFFFFF)) /
            film->frequency;

        if (film->sample_table[j].pts > largest_pts)
          largest_pts = film->sample_table[j].pts;
      }

      /*
       * in some files, this chunk length does not account for the 16-byte
       * chunk preamble; watch for it
       */
      if (chunk_size == film->sample_count * 16)
        i += 16;
      break;

    default:
      xine_log(film->xine, XINE_LOG_FORMAT,
        _("unrecognized FILM chunk\n"));
      return 0;
    }

    i += chunk_size;
  }

  film->total_time = largest_pts / 90000;

  return 1;
}

static void *demux_film_loop (void *this_gen) {

  demux_film_t *this = (demux_film_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int cvid_chunk_size;
  unsigned int i, j;
  int fixed_cvid_header;
  unsigned int remaining_sample_bytes;
  int64_t last_frame_pts = 0;

  pthread_mutex_lock( &this->mutex );

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      pthread_mutex_lock( &this->mutex );

      i = this->current_sample;

      /* if there is an incongruency between last and current sample, it
       * must be time to send a new pts */
      if (this->last_sample + 1 != this->current_sample) {
        xine_demux_flush_engine(this->xine);

        /* send new pts */
        xine_demux_control_newpts(this->xine, this->sample_table[i].pts, 0);
        
        /* reset last_frame_pts on seek */
        last_frame_pts = 0;
      }

      this->last_sample = this->current_sample;
      this->current_sample++;

      /* check if all the samples have been sent */
      if (i >= this->sample_count) {
        this->status = DEMUX_FINISHED;
        break;
      }

      /* check if we're only sending audio samples until the next keyframe */
      if ((this->waiting_for_keyframe) && 
          (this->sample_table[i].syncinfo1 != 0xFFFFFFFF)) {
        if ((this->sample_table[i].syncinfo1 & 0x80000000) == 0) {
          this->waiting_for_keyframe = 0;
        } else {
          /* move on to the next sample */
          continue;
        }
      }

      if ((this->sample_table[i].syncinfo1 != 0xFFFFFFFF) &&
        (this->video_type == BUF_VIDEO_CINEPAK)) {
        /* do a special song and dance when loading CVID data */
        if (this->version[0])
          cvid_chunk_size = this->sample_table[i].sample_size - 2;
        else
          cvid_chunk_size = this->sample_table[i].sample_size - 6;

        /* reset flag */
        fixed_cvid_header = 0;

        remaining_sample_bytes = cvid_chunk_size;
        this->input->seek(this->input, this->sample_table[i].sample_offset,
          SEEK_SET);

        while (remaining_sample_bytes) {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->type = this->video_type;
          buf->input_pos = this->sample_table[i].sample_offset;
          buf->input_length = this->data_end;
          buf->input_time = this->sample_table[i].pts / 90000;
          buf->pts = this->sample_table[i].pts;

          if (last_frame_pts) {
            buf->decoder_flags |= BUF_FLAG_FRAMERATE;
            buf->decoder_info[0] = buf->pts - last_frame_pts;
          }
            
          if (remaining_sample_bytes > buf->max_size)
            buf->size = buf->max_size;
          else
            buf->size = remaining_sample_bytes;
          remaining_sample_bytes -= buf->size;

          if (!fixed_cvid_header) {
            if (this->input->read(this->input, buf->content, 10) != 10) {
              this->status = DEMUX_FINISHED;
              break;
            }

            /* skip over the extra non-spec CVID bytes */
            this->input->seek(this->input, 
              this->sample_table[i].sample_size - cvid_chunk_size, SEEK_CUR);

            /* load the rest of the chunk */
            if (this->input->read(this->input, buf->content + 10, 
              buf->size - 10) != buf->size - 10) {
              this->status = DEMUX_FINISHED;
              break;
            }

            /* adjust the length in the CVID data chunk */
            buf->content[1] = (cvid_chunk_size >> 16) & 0xFF;
            buf->content[2] = (cvid_chunk_size >>  8) & 0xFF;
            buf->content[3] = (cvid_chunk_size >>  0) & 0xFF;

            fixed_cvid_header = 1;
          } else {
            if (this->input->read(this->input, buf->content, buf->size) !=
              buf->size) {
              this->status = DEMUX_FINISHED;
              break;
            }
          }

          if ((this->sample_table[i].syncinfo1 & 0x80000000) == 0)
            buf->decoder_flags |= BUF_FLAG_KEYFRAME;
          if (!remaining_sample_bytes)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;
          this->video_fifo->put(this->video_fifo, buf);
        }
        last_frame_pts = buf->pts;
      } else if (this->sample_table[i].syncinfo1 != 0xFFFFFFFF) {
        /* FILM files always appear to use Cinepak video, but pretend that
           sometimes they don't and add a provision to load another kind of
           video chunk */
        remaining_sample_bytes = this->sample_table[i].sample_size;
        this->input->seek(this->input, this->sample_table[i].sample_offset,
          SEEK_SET);

        while (remaining_sample_bytes) {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->type = this->video_type;
          buf->input_pos = this->sample_table[i].sample_offset;
          buf->input_length = this->data_end;
          buf->input_time = this->sample_table[i].pts / 90000;
          buf->pts = this->sample_table[i].pts;

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

          if ((this->sample_table[i].syncinfo1 & 0x80000000) == 0)
            buf->decoder_flags |= BUF_FLAG_KEYFRAME;
          if (!remaining_sample_bytes)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;
          this->video_fifo->put(this->video_fifo, buf);
        }
      } else if( this->audio_fifo ) {
        /* load an audio sample and packetize it */
        remaining_sample_bytes = this->sample_table[i].sample_size;
        this->input->seek(this->input, this->sample_table[i].sample_offset,
          SEEK_SET);

        while (remaining_sample_bytes) {
          buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
          buf->type = this->audio_type;
          buf->input_pos = this->sample_table[i].sample_offset;
          buf->input_length = this->data_end;
          buf->input_time = this->sample_table[i].pts / 90000;
          buf->pts = this->sample_table[i].pts;

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

          /* convert 8-bit data from signed -> unsigned */
          if (this->audio_bits == 8)
            for (j = 0; j < buf->size; j++)
              buf->content[j] += 0x80;

          if (!remaining_sample_bytes)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;

          this->audio_fifo->put(this->audio_fifo, buf);
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

  printf ("demux_film: demux loop finished (status: %d)\n",
          this->status);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }
      
  this->thread_running = 0;
  pthread_mutex_unlock( &this->mutex );
  return NULL;
}

static int demux_film_open(demux_plugin_t *this_gen, input_plugin_t *input,
                          int stage) {

  demux_film_t *this = (demux_film_t *) this_gen;
  char sig[4];

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, sig, 4) != 4) {
      return DEMUX_CANNOT_HANDLE;
    }
    if (strncmp(sig, "FILM", 4) == 0)
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

    xine_strdupa(valid_ends,
		 (this->config->register_string(this->config,
						"mrl.ends_film", VALID_ENDS,
						_("valid mrls ending for film demuxer"),
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

static int demux_film_start (demux_plugin_t *this_gen,
                             fifo_buffer_t *video_fifo,
                             fifo_buffer_t *audio_fifo,
                             off_t start_pos, int start_time) {

  demux_film_t *this = (demux_film_t *) this_gen;
  buf_element_t *buf;
  int err;
  int status;

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {
    this->video_fifo = video_fifo;
    this->audio_fifo = audio_fifo;

    /* open the FILM file */
    if (!open_film_file(this)) {
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
    }

    /* print vital stats */
    xine_log (this->xine, XINE_LOG_FORMAT,
      _("demux_film: FILM version %c%c%c%c, running time: %d min, %d sec\n"),
      this->version[0],
      this->version[1],
      this->version[2],
      this->version[3],
      this->total_time / 60,
      this->total_time % 60);
    if (this->video_type)
      xine_log (this->xine, XINE_LOG_FORMAT,
        _("demux_film: %c%c%c%c video @ %dx%d, %d Hz playback clock\n"),
        (this->video_codec >> 24) & 0xFF,
        (this->video_codec >> 16) & 0xFF,
        (this->video_codec >>  8) & 0xFF,
        (this->video_codec >>  0) & 0xFF,
        this->bih.biWidth,
        this->bih.biHeight,
        this->frequency);
    else {
      xine_log (this->xine, XINE_LOG_FORMAT,
        _("demux_film: unknown video codec %c%c%c%c\n"),
        (this->video_codec >> 24) & 0xFF,
        (this->video_codec >> 16) & 0xFF,
        (this->video_codec >>  8) & 0xFF,
        (this->video_codec >>  0) & 0xFF );
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
    }
    if (this->audio_type)
      xine_log (this->xine, XINE_LOG_FORMAT,
        _("demux_film: %d Hz, %d-bit %s%s PCM audio\n"),
        this->sample_rate,
        this->audio_bits,
        (this->audio_bits == 16) ? "big-endian " : "",
        (this->audio_channels == 1) ? "monaural" : "stereo");

    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send init info to decoders */
    if (this->video_fifo && this->video_type) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = 3000;  /* initial video_step */
      memcpy(buf->content, &this->bih, sizeof(this->bih));
      buf->size = sizeof(this->bih);
      if (this->video_codec == CVID_TAG)
        buf->type = BUF_VIDEO_CINEPAK;
      else
        buf->type = 0;
      this->video_fifo->put (this->video_fifo, buf);
    }

    if (this->audio_fifo && this->audio_type) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_LPCM_BE;
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = this->sample_rate;
      buf->decoder_info[2] = this->audio_bits;
      buf->decoder_info[3] = this->audio_channels;
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    this->current_sample = 0;
    this->last_sample = 0;

    if ((err = pthread_create (&this->thread, NULL, demux_film_loop, this)) != 0) {
      printf ("demux_film: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }

  status = this->status;
  pthread_mutex_unlock(&this->mutex);
  
  return status;
}

static int demux_film_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {
  demux_film_t *this = (demux_film_t *) this_gen;
  int best_index;
  int left, middle, right;
  int found;
  int64_t keyframe_pts;

  pthread_mutex_lock( &this->mutex );

  if (!this->thread_running) {
    pthread_mutex_unlock( &this->mutex );
    return this->status;
  }

  /* perform a binary search on the sample table, testing the offset 
   * boundaries first */
  if (start_pos <= this->data_start)
    best_index = 0;
  else if (start_pos >= this->data_end) {
    this->status = DEMUX_FINISHED;
    pthread_mutex_unlock( &this->mutex );
    return this->status;
  } else {
    left = 0;
    right = this->sample_count - 1;
    found = 0;

    while (!found) {
      middle = (left + right) / 2;
      if ((start_pos >= this->sample_table[middle].sample_offset) &&
          (start_pos <= this->sample_table[middle].sample_offset + 
           this->sample_table[middle].sample_size)) {
        found = 1;
      } else if (start_pos < this->sample_table[middle].sample_offset) {
        right = middle;
      } else {
        left = middle;
      }
    }

    best_index = middle;
  }

  /* search back in the table for the nearest keyframe */
  while (best_index--) {
    if ((this->sample_table[best_index].syncinfo1 & 0x80000000) == 0) {
      break;
    }
  }

  /* not done yet; now that the nearest keyframe has been found, seek
   * back to the first audio frame that has a pts less than or equal to
   * that of the keyframe */
  this->waiting_for_keyframe = 1;
  keyframe_pts = this->sample_table[best_index].pts;
  while (best_index--) {
    if ((this->sample_table[best_index].syncinfo1 == 0xFFFFFFFF) &&
        (this->sample_table[best_index].pts < keyframe_pts)) {
      break;
    }
  }

  this->current_sample = best_index;
  this->status = DEMUX_OK;
  pthread_mutex_unlock( &this->mutex );

  return this->status;
}

static void demux_film_stop (demux_plugin_t *this_gen) {

  demux_film_t *this = (demux_film_t *) this_gen;
  void *p;

  pthread_mutex_lock( &this->mutex );

  if (!this->thread_running) {
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  this->current_sample = this->last_sample = 0;

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_demux_flush_engine(this->xine);

  xine_demux_control_end(this->xine, BUF_FLAG_END_USER);
}

static void demux_film_close (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  pthread_mutex_destroy (&this->mutex);

  if (this->sample_table)
    free(this->sample_table);
  free(this);
}

static int demux_film_get_status (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  return this->status;
}

static char *demux_film_get_id(void) {
  return "FILM (CPK)";
}

static int demux_film_get_stream_length (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  return this->total_time;
}

static char *demux_film_get_mimetypes(void) {
  return NULL;
}


demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {
  demux_film_t *this;

  if (iface != 10) {
    printf (_("demux_film: plugin doesn't support plugin API version %d.\n"
	      "            this means there's a version mismatch between xine and this "
	      "            demuxer plugin. Installing current demux plugins should help.\n"),
            iface);
    return NULL;
  }

  this         = (demux_film_t *) xine_xmalloc(sizeof(demux_film_t));
  this->config = xine->config;
  this->xine   = xine;
  
  (void *) this->config->register_string(this->config,
					 "mrl.ends_film", VALID_ENDS,
					 _("valid mrls ending for film demuxer"),
					 NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_film_open;
  this->demux_plugin.start             = demux_film_start;
  this->demux_plugin.seek              = demux_film_seek;
  this->demux_plugin.stop              = demux_film_stop;
  this->demux_plugin.close             = demux_film_close;
  this->demux_plugin.get_status        = demux_film_get_status;
  this->demux_plugin.get_identifier    = demux_film_get_id;
  this->demux_plugin.get_stream_length = demux_film_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_film_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init(&this->mutex, NULL);

  return &this->demux_plugin;
}
