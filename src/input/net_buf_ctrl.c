/*
 * Copyright (C) 2000-2003 the xine project
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
 * network buffering control
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>

#include "net_buf_ctrl.h"

#define DEFAULT_LOW_WATER_MARK     1
#define DEFAULT_HIGH_WATER_MARK 5000 /* in 1/1000 s */

#define WRAP_THRESHOLD       5*90000 /* from the asf demuxer */

#define FIFO_PUT                   0
#define FIFO_GET                   1
/*
#define LOG
*/

struct nbc_s {

  xine_stream_t   *stream;

  int              buffering;
  int              enabled;

  int              progress;
  fifo_buffer_t   *video_fifo;
  fifo_buffer_t   *audio_fifo;
  int              video_fifo_fill;
  int              audio_fifo_fill;
  int              video_fifo_free;
  int              audio_fifo_free;
  int64_t          video_fifo_length; /* in ms */
  int64_t          audio_fifo_length; /* in ms */

  int64_t          low_water_mark;
  int64_t          high_water_mark;
  /* bitrate */
  int64_t          video_last_pts;
  int64_t          audio_last_pts;
  int64_t          video_first_pts;
  int64_t          audio_first_pts;
  int64_t          video_fifo_size;
  int64_t          audio_fifo_size;
  int64_t          video_br;
  int64_t          audio_br;

  pthread_mutex_t  mutex;
};

static void report_progress (xine_stream_t *stream, int p) {

  xine_event_t             event;
  xine_progress_data_t     prg;

  prg.description = _("Buffering...");
  prg.percent = (p>100)?100:p;

  event.type = XINE_EVENT_PROGRESS;
  event.data = &prg;
  event.data_length = sizeof (xine_progress_data_t);

  xine_event_send (stream, &event);
}

static void nbc_set_speed_pause (xine_stream_t *stream) {
#ifdef LOG
      printf("\nnet_buf_ctrl: nbc_put_cb: set_speed_pause\n");
#endif
  stream->xine->clock->set_speed (stream->xine->clock, XINE_SPEED_PAUSE);
  stream->xine->clock->set_option (stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 0);
  if (stream->audio_out)
    stream->audio_out->set_property(stream->audio_out,AO_PROP_PAUSED,2);
}

static void nbc_set_speed_normal (xine_stream_t *stream) {
#ifdef LOG
      printf("\nnet_buf_ctrl: nbc_put_cb: set_speed_normal\n");
#endif
  stream->xine->clock->set_speed (stream->xine->clock, XINE_SPEED_NORMAL);
  stream->xine->clock->set_option (stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);
  if (stream->audio_out)
    stream->audio_out->set_property(stream->audio_out,AO_PROP_PAUSED,0);
}

void nbc_check_buffers (nbc_t *this) {
  /* Deprecated */
}

static void display_stats (nbc_t *this) {

  if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG) {
    printf("net_buf_ctrl: vff=%3d%% aff=%3d%% vf=%4.1fs af=%4.1fs vbr=%4lld abr=%4lld b=%1d e=%1d\r",
           this->video_fifo_fill, this->audio_fifo_fill,
           (float)(this->video_fifo_length / 1000),
           (float)(this->audio_fifo_length / 1000),
           this->video_br / 1000, this->audio_br / 1000,
           this->buffering, this->enabled
          );
    fflush(stdout);
  }
}

void nbc_close (nbc_t *this) {
  fifo_buffer_t *video_fifo = this->stream->video_fifo;
  fifo_buffer_t *audio_fifo = this->stream->audio_fifo;

#ifdef LOG
  printf("\nnet_buf_ctrl: nbc_close\n");
#endif

  video_fifo->register_put_cb(video_fifo, NULL, NULL);
  video_fifo->register_get_cb(video_fifo, NULL, NULL);

  if (audio_fifo) {
    audio_fifo->register_put_cb(audio_fifo, NULL, NULL);
    audio_fifo->register_get_cb(audio_fifo, NULL, NULL);
  }

  pthread_mutex_lock(&this->mutex);
  this->stream->xine->clock->set_option (this->stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);

  if (this->buffering) {
    this->buffering = 0;
    nbc_set_speed_normal(this->stream);
  }

  pthread_mutex_unlock(&this->mutex);

  free (this);
#ifdef LOG
  printf("\nnet_buf_ctrl: nbc_close: done\n");
#endif
}

/*  Try to compute the length of the fifo in 1/1000 s
 *  2 methods :
 *    if the bitrate is known
 *      use the size of the fifo
 *    else
 *      use the the first and the last pts of the fifo
 */
static void nbc_compute_fifo_length(nbc_t *this,
                                    fifo_buffer_t *fifo,
                                    buf_element_t *buf,
                                    int action) {
  int fifo_free, fifo_fill;
  int64_t video_br, audio_br;
  int has_video, has_audio;
  int64_t pts_diff = 0;

  has_video = this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO];
  has_audio = this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO];
  video_br  = this->stream->stream_info[XINE_STREAM_INFO_VIDEO_BITRATE];
  audio_br  = this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE];

  fifo_free = fifo->buffer_pool_num_free;
  fifo_fill = fifo->fifo_size;

  if (fifo == this->video_fifo) {
    this->video_fifo_free = fifo_free;
    this->video_fifo_fill = (100 * fifo_fill) / (fifo_fill + fifo_free - 1);
    this->video_fifo_size = fifo->fifo_data_size;
    if (video_br) {
      this->video_br = video_br;
      this->video_fifo_length = (8000 * this->video_fifo_size) / this->video_br;
    } else {
      if (buf->pts) {
        if (action == FIFO_PUT) {
          pts_diff = buf->pts - this->video_last_pts;
          if ((pts_diff >= 0) && (pts_diff < WRAP_THRESHOLD)) {
            this->video_last_pts = buf->pts;
          } else {
            /* discontinuity detected */
#ifdef LOG
            printf("\nnet_buf_ctrl: nbc_compute_fifo_length: video discontinuity: %lld\n", pts_diff);
#endif
            this->video_last_pts = buf->pts;
            /* smooth the discontinuity */
            this->video_first_pts = buf->pts - 90 * this->video_fifo_length;
          }
          if (this->video_first_pts == 0) {
            this->video_first_pts = buf->pts;
          }
        } else {
          this->video_first_pts = buf->pts;
        }
        this->video_fifo_length = (this->video_last_pts - this->video_first_pts) / 90;
        if (this->video_fifo_length)
          this->video_br = 8000 * (this->video_fifo_size / this->video_fifo_length);
        else
          this->video_br = 0;
      }
    }
  } else {
    this->audio_fifo_free = fifo_free;
    this->audio_fifo_fill = (100 * fifo_fill) / (fifo_fill + fifo_free - 1);
    this->audio_fifo_size = fifo->fifo_data_size;
    if (audio_br) {
      this->audio_br = audio_br;
      this->audio_fifo_length = (8000 * this->audio_fifo_size) / this->audio_br;
    } else {
      if (buf->pts) {
        if (action == FIFO_PUT) {
          pts_diff = buf->pts - this->audio_last_pts;
          if ((pts_diff >= 0) && (pts_diff < WRAP_THRESHOLD)) {
            this->audio_last_pts = buf->pts;
          } else {
            /* discontinuity detected */
#ifdef LOG
            printf("\nnet_buf_ctrl: nbc_compute_fifo_length: audio discontinuity: %lld\n", pts_diff);
#endif
            this->audio_last_pts = buf->pts;
            /* smooth the discontinuity */
            this->audio_first_pts = buf->pts  - 90 * this->audio_fifo_length;
          }
          if (!this->audio_first_pts) {
            this->audio_first_pts = buf->pts;
          }
        } else {
          this->audio_first_pts = buf->pts;
        }
        this->audio_fifo_length = (this->audio_last_pts - this->audio_first_pts) / 90;
        if (this->audio_fifo_length)
          this->audio_br = 8000 * (this->audio_fifo_size / this->audio_fifo_length);
        else
          this->audio_br = 0;
      }
    }
  }
}

/* Put callback
 * the fifo mutex is locked */
void nbc_put_cb (fifo_buffer_t *fifo, buf_element_t *buf, void *this_gen) {
  nbc_t *this = (nbc_t*)this_gen;
  int64_t progress = 0;
  int64_t video_p = 0;
  int64_t audio_p = 0;
  int has_video, has_audio;
  uint32_t buf_major_mask;

  pthread_mutex_lock(&this->mutex);

  /* stop buffering at the end of the stream */
  if ((buf->decoder_flags & BUF_FLAG_END_USER) ||
      (buf->decoder_flags & BUF_FLAG_END_STREAM)) {
    this->enabled = 0;
    if (this->buffering) {
      this->buffering = 0;
      nbc_set_speed_normal(this->stream);
    }
  }

  /* do nothing if we are at the end of the stream */
  if (!this->enabled) {

    buf_major_mask = buf->type & BUF_MAJOR_MASK;
    if ((buf_major_mask == BUF_VIDEO_BASE) || (buf_major_mask == BUF_AUDIO_BASE)) {
      /* a new stream starts */
      if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
        printf("\nnet_buf_ctrl: nbc_put_cb: starts buffering\n");
      this->enabled           = 1;
      this->buffering         = 1;
      this->video_first_pts   = 0;
      this->video_last_pts    = 0;
      this->audio_first_pts   = 0;
      this->audio_last_pts    = 0;
      this->video_fifo_length = 0;
      this->audio_fifo_length = 0;
      nbc_set_speed_pause(this->stream);
      this->progress = 0;
      report_progress (this->stream, progress);

    } else {
      display_stats(this);
      pthread_mutex_unlock(&this->mutex);
      return;
    }
  }

  nbc_compute_fifo_length(this, fifo, buf, FIFO_PUT);

  if (this->buffering) {

    has_video = this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO];
    has_audio = this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO];
    /* restart playing if :
     *   - one fifo is full (to avoid deadlock)
     *   - high_water_mark is reached by all fifos
     * do not restart if has_video and has_audio are false to avoid
     * a yoyo effect at the beginning of the stream when these values
     * are not yet known. */
    if ((fifo->buffer_pool_num_free <= 1) ||
        (((!has_video) || (this->video_fifo_length > this->high_water_mark)) &&
         ((!has_audio) || (this->audio_fifo_length > this->high_water_mark)) &&
         (has_video || has_audio))) {

      this->progress = 100;
      report_progress (this->stream, 100);
      this->buffering = 0;

      if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
        printf("\nnet_buf_ctrl: nbc_put_cb: stops buffering\n");

      nbc_set_speed_normal(this->stream);

    } else {
      /*  compute the buffering progress
       *    50%: video
       *    50%: audio */
      video_p = ((this->video_fifo_length * 50) / this->high_water_mark);
      if (video_p > 50) video_p = 50;
      audio_p = ((this->audio_fifo_length * 50) / this->high_water_mark);
      if (audio_p > 50) audio_p = 50;

      if ((has_video) && (has_audio)) {
        progress = video_p + audio_p;
      } else if (has_video) {
        progress = 2 * video_p;
      } else {
        progress = 2 * audio_p;
      }

      /* if the progress can't be computed using the fifo length,
         use the number of buffers */
      if (!progress) {
        video_p = this->video_fifo_fill;
        audio_p = this->audio_fifo_fill;
        progress = (video_p > audio_p) ? video_p : audio_p;
      }

      if (progress > this->progress) {
        report_progress (this->stream, progress);
        this->progress = progress;
      }
    }
  }
  display_stats(this);
  pthread_mutex_unlock(&this->mutex);
}

/* Get callback
 * the fifo mutex is locked */
void nbc_get_cb (fifo_buffer_t *fifo, buf_element_t *buf, void *this_gen) {
  nbc_t *this = (nbc_t*)this_gen;
  int other_fifo_free;
  pthread_mutex_lock(&this->mutex);

  nbc_compute_fifo_length(this, fifo, buf, FIFO_GET);

  if (!this->enabled) {
    display_stats(this);
    pthread_mutex_unlock(&this->mutex);
    return;
  }

  if (!this->buffering) {

    /* start buffering if one fifo is empty */
    if (fifo->fifo_size == 0) {
      if (fifo == this->video_fifo) {
        other_fifo_free = this->audio_fifo_free;
      } else {
        other_fifo_free = this->video_fifo_free;
      }

      /* Don't pause if the other fifo is full because the next
         put() will restart the engine */
      if (other_fifo_free > 1) {
        this->buffering = 1;
        this->progress  = 0;
        report_progress (this->stream, 0);

        if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
          printf("\nnet_buf_ctrl: nbc_put_cb: starts buffering\n");
        nbc_set_speed_pause(this->stream);
      }
    }
  } else {
    nbc_set_speed_pause(this->stream);
  }
  display_stats(this);
  pthread_mutex_unlock(&this->mutex);
}

nbc_t *nbc_init (xine_stream_t *stream) {

  nbc_t *this = (nbc_t *) malloc (sizeof (nbc_t));
  fifo_buffer_t *video_fifo = stream->video_fifo;
  fifo_buffer_t *audio_fifo = stream->audio_fifo;

#ifdef LOG
        printf("net_buf_ctrl: nbc_init\n");
#endif
  pthread_mutex_init (&this->mutex, NULL);

  this->stream              = stream;
  this->buffering           = 0;
  this->enabled             = 0;
  this->low_water_mark      = DEFAULT_LOW_WATER_MARK;
  this->high_water_mark     = DEFAULT_HIGH_WATER_MARK;
  this->progress            = 0;
  this->video_fifo          = video_fifo;
  this->audio_fifo          = audio_fifo;
  this->video_fifo_fill     = 0;
  this->audio_fifo_fill     = 0;
  this->video_fifo_free     = 0;
  this->audio_fifo_free     = 0;
  this->video_fifo_length   = 0;
  this->audio_fifo_length   = 0;
  this->video_last_pts      = 0;
  this->audio_last_pts      = 0;
  this->video_first_pts     = 0;
  this->audio_first_pts     = 0;
  this->video_fifo_size     = 0;
  this->audio_fifo_size     = 0;
  this->video_br            = 0;
  this->audio_br            = 0;

  video_fifo->register_put_cb(video_fifo, nbc_put_cb, this);
  video_fifo->register_get_cb(video_fifo, nbc_get_cb, this);

  if (audio_fifo) {
    audio_fifo->register_put_cb(audio_fifo, nbc_put_cb, this);
    audio_fifo->register_get_cb(audio_fifo, nbc_get_cb, this);
  }

  return this;
}


void nbc_set_high_water_mark(nbc_t *this, int value) {
/*
  Deprecated
  this->high_water_mark = value;
*/
}

void nbc_set_low_water_mark(nbc_t *this, int value) {
/*
  Deprecated
  this->low_water_mark = value;
*/
}
