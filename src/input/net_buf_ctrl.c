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
 * network buffering control
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>

#include "net_buf_ctrl.h"

#define DEFAULT_LOW_WATER_MARK     1
#define DEFAULT_HIGH_WATER_MARK 5000 /* in millisecond */

/*
#define LOG
*/

struct nbc_s {

  xine_stream_t   *stream;

  int              buffering;
  int              low_water_mark;
  int              high_water_mark;
  int              fifo_full;
  int              progress;

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



void nbc_check_buffers (nbc_t *this) {

  int fifo_fill, video_fifo_fill, audio_fifo_fill;        /* number of buffers */
  int video_fifo_free, audio_fifo_free;                   /* number of free buffers */
  int data_length, video_data_length, audio_data_length;  /* fifo length in second */
  uint32_t video_data_size, audio_data_size;              /* fifo size in bytes */
  int video_bitrate, audio_bitrate;
  int progress;
  int video_fifo_progress, audio_fifo_progress;

  video_fifo_fill = this->stream->video_fifo->size(this->stream->video_fifo);
  if (this->stream->audio_fifo)
    audio_fifo_fill = this->stream->audio_fifo->size(this->stream->audio_fifo);
  else
    audio_fifo_fill = 0;

  fifo_fill = audio_fifo_fill + video_fifo_fill;

  /* start buffering if fifos are empty */
  if (fifo_fill == 0) {
    if (!this->buffering) {

      /* increase/decrease marks to adapt to stream/network needs */
      if (!this->fifo_full) {
        this->high_water_mark += this->high_water_mark / 4;
        /* this->low_water_mark = this->high_water_mark/4; */
      } else {
        this->high_water_mark -= this->high_water_mark / 8;
       }
      this->buffering = 1;
      this->progress  = 0;
      report_progress (this->stream, 0);

    }
    /* pause */
     this->stream->xine->clock->set_speed (this->stream->xine->clock, XINE_SPEED_PAUSE);
     this->stream->xine->clock->set_option (this->stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 0);
     if (this->stream->audio_out)
       this->stream->audio_out->set_property(this->stream->audio_out,AO_PROP_PAUSED,2);

  } else {

    if (this->buffering) {

      /* compute data length in fifos */
      video_bitrate = this->stream->stream_info[XINE_STREAM_INFO_VIDEO_BITRATE];
      audio_bitrate = this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE];

      if (video_bitrate) {
        video_data_size = this->stream->video_fifo->data_size(this->stream->video_fifo);
        video_data_length = (8000 * video_data_size) / video_bitrate;
      } else {
        video_data_length = 0;
      }
      video_fifo_free = this->stream->video_fifo->num_free(this->stream->video_fifo);

      if (this->stream->audio_fifo) {
        if (audio_bitrate) {
          audio_data_size = this->stream->audio_fifo->data_size(this->stream->audio_fifo);
          audio_data_length = (8000 * audio_data_size) / audio_bitrate;
        } else {
          audio_data_length = 0;
        }
        audio_fifo_free = this->stream->audio_fifo->num_free(this->stream->audio_fifo);
      } else {
        audio_data_length = 0;
        audio_fifo_free = 0;
      }

      data_length = (video_data_length > audio_data_length) ? video_data_length : audio_data_length;

#ifdef LOG
      printf("net_buf_ctrl: vb=%d, ab=%d, vf=%d, af=%d, vdl=%d, adl=%d, dl=%d\n",
             video_fifo_fill, audio_fifo_fill,
             video_fifo_free, audio_fifo_free,
             video_data_length, audio_data_length, data_length);
#endif
      /* stop buffering because:
       *    - fifos are filled enough
       *    - fifos are full (1 buffer is keeped for emergency stuffs)
       */
      if ((data_length >= this->high_water_mark) ||
          (video_fifo_free == 1) ||
          (audio_fifo_free == 1) ) {
        /* unpause */

        this->stream->xine->clock->set_speed (this->stream->xine->clock, XINE_SPEED_NORMAL);
        this->stream->xine->clock->set_option (this->stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);
        if (this->stream->audio_out)
          this->stream->audio_out->set_property(this->stream->audio_out,AO_PROP_PAUSED,0);

        report_progress (this->stream, 100);
        this->buffering = 0;
        this->fifo_full = (data_length < this->high_water_mark);
      } else {
        progress = (data_length * 100) / this->high_water_mark;

        if (!progress) {
          /* bitrate is not known */
          if ((video_fifo_fill + video_fifo_free) > 0)
            video_fifo_progress = (100 * video_fifo_fill) / (video_fifo_fill + video_fifo_free);
          else
            video_fifo_progress = 0;
          if ((audio_fifo_fill + audio_fifo_free) > 0)
            audio_fifo_progress = (100 * audio_fifo_fill) / (audio_fifo_fill + audio_fifo_free);
          else
            audio_fifo_progress = 0;
          progress = (video_fifo_progress > audio_fifo_progress)?
            video_fifo_progress : audio_fifo_progress;
        }

        if (progress != this->progress) {
          report_progress (this->stream, progress);
          this->progress = progress;
        }
      }

    } else {
      /* fifos are ok */
    }

  }
}

void nbc_close (nbc_t *this) {
#ifdef LOG
  printf("net_buf_ctrl: nbc_close\n");
#endif
  this->stream->xine->clock->set_option (this->stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);
  free (this);
}

nbc_t *nbc_init (xine_stream_t *stream) {

  nbc_t *this = (nbc_t *) malloc (sizeof (nbc_t));

  this->stream          = stream;
  this->buffering       = 0;
  this->low_water_mark  = DEFAULT_LOW_WATER_MARK;
  this->high_water_mark = DEFAULT_HIGH_WATER_MARK;
  this->progress        = 0;

  return this;
}


void nbc_set_high_water_mark(nbc_t *this, int value) {
/*
  this->high_water_mark = value;
*/
}

void nbc_set_low_water_mark(nbc_t *this, int value) {
/*
  this->low_water_mark = value;
*/
}
