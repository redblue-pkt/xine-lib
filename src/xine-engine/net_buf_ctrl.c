/*
 * Copyright (C) 2000-2020 the xine project
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * network buffering control
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

/********** logging **********/
#define LOG_MODULE "net_buf_ctrl"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine_private.h"

#ifndef XINE_LIVE_PAUSE_ON
# define XINE_LIVE_PAUSE_ON 0x7ffffffd
#endif

#define DEFAULT_HIGH_WATER_MARK 5000 /* in 1/1000 s */

#define FULL_FIFO_MARK             5 /* buffers free */

#define FIFO_PUT                   0
#define FIFO_GET                   1

typedef struct {
  /* pointer */
  fifo_buffer_t   *fifo;
  /* buffers */
  int              fifo_fill;
  int              fifo_free;
  /* ms */
  uint32_t         fifo_length;     /* in ms */
  uint32_t         fifo_length_int; /* in ms */
  /* bitrate */
  int64_t          last_pts;
  int64_t          first_pts;
  uint32_t         br;
  uint32_t         stream_br;
  /* bytes */
  uint32_t         fifo_size;
  /* flags */
  int              in_disc;
} xine_nbc_fifo_info_t;

struct xine_nbc_st {

  xine_stream_t   *stream;

  int              buffering;
  int              enabled;

  int              has_audio;
  int              has_video;

  int              progress;

  xine_nbc_fifo_info_t audio;
  xine_nbc_fifo_info_t video;

  uint32_t         high_water_mark;

  pthread_mutex_t  mutex;

  /* follow live dvb delivery speed.
     0 = fix disabled
     1 = play at normal speed
     2 = play 0.5% slower to fill video fifo
     3 = play 0.5% faster to empty video fifo
     4..6 = same as 1..3 but watch audio fifo instead
     7 = pause */
  int dvbspeed;
  int dvbs_center, dvbs_width, dvbs_audio_fill, dvbs_video_fill;
  int64_t dvbs_audio_in, dvbs_audio_out;
  int64_t dvbs_video_in, dvbs_video_out;
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

static void nbc_set_speed_pause (xine_nbc_t *this) {
  xine_stream_t *stream = this->stream;

  xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: nbc_set_speed_pause\n");
  _x_set_speed (stream, XINE_SPEED_PAUSE);
  /* allow decoding while paused */
  _x_set_fine_speed (stream, XINE_LIVE_PAUSE_ON);
  stream->xine->clock->set_option (stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 0);
}

static void nbc_set_speed_normal (xine_nbc_t *this) {
  xine_stream_t *stream = this->stream;

  xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: nbc_set_speed_normal\n");
  _x_set_speed (stream, XINE_SPEED_NORMAL);
  stream->xine->clock->set_option (stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);
}

static void dvbspeed_init (xine_nbc_t *this) {
  int use_dvbs = 0;
  if (this->stream->input_plugin) {
    if (this->stream->input_plugin->get_capabilities (this->stream->input_plugin) & INPUT_CAP_LIVE) {
      use_dvbs = 1;
    } else {
      const char *mrl = this->stream->input_plugin->get_mrl (this->stream->input_plugin);
      if (mrl) {
        /* detect Kaffeine: fifo://~/.kde4/share/apps/kaffeine/dvbpipe.m2t */
        if ((strcasestr (mrl, "/dvbpipe.")) ||
            ((!strncasecmp (mrl, "dvb", 3)) && ((mrl[3] == ':') || (mrl[3] && (mrl[4] == ':')))))
          use_dvbs = 1;
      }
    }
  }
  if (use_dvbs) {
    this->dvbs_center = 2 * 90000;
    this->dvbs_width = 90000;
    this->dvbs_audio_in = this->dvbs_audio_out = this->dvbs_audio_fill = 0;
    this->dvbs_video_in = this->dvbs_video_out = this->dvbs_video_fill = 0;
    this->dvbspeed = 7;
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: dvbspeed mode\n");
#if 1
    {
      /* somewhat rude but saves user a lot of frustration */
      xine_t *xine = this->stream->xine;
      config_values_t *config = xine->config;
      xine_cfg_entry_t entry;
      if (xine_config_lookup_entry (xine, "audio.synchronization.slow_fast_audio",
        &entry) && (entry.num_value == 0)) {
        config->update_num (config, "audio.synchronization.slow_fast_audio", 1);
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: slow/fast audio playback enabled\n");
      }
      if (xine_config_lookup_entry (xine, "engine.buffers.video_num_buffers",
        &entry) && (entry.num_value < 800)) {
        config->update_num (config, "engine.buffers.video_num_buffers", 800);
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: enlarged video fifo to 800 buffers\n");
      }
    }
#endif
  }
}

static void dvbspeed_close (xine_nbc_t *this) {
  if ((0xec >> this->dvbspeed) & 1)
    _x_set_fine_speed (this->stream, XINE_FINE_SPEED_NORMAL);
  if (this->dvbspeed)
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: dvbspeed OFF\n");
  this->dvbspeed = 0;
}

static void dvbspeed_put (xine_nbc_t *this, fifo_buffer_t * fifo, buf_element_t *b) {
  int all_fill, used, mode;
  const char *name;
  /* select vars */
  mode = b->type & BUF_MAJOR_MASK;
  if (mode == BUF_VIDEO_BASE) {
    /* update fifo fill time */
    if (b->pts) {
      if (this->dvbs_video_in) {
        int64_t diff = b->pts - this->dvbs_video_in;
        if ((diff > -220000) && (diff < 220000))
          this->dvbs_video_fill += diff;
      }
      this->dvbs_video_in = b->pts;
    }
    if ((0x71 >> this->dvbspeed) & 1)
      return;
    name = "video";
    all_fill = this->dvbs_video_fill;
  } else if (mode == BUF_AUDIO_BASE) {
    /* update fifo fill time */
    if (b->pts) {
      if (this->dvbs_audio_in) {
        int64_t diff = b->pts - this->dvbs_audio_in;
        if ((diff > -220000) && (diff < 220000))
          this->dvbs_audio_fill += diff;
      }
      this->dvbs_audio_in = b->pts;
    }
    if ((0x0f >> this->dvbspeed) & 1)
      return;
    name = "audio";
    all_fill = this->dvbs_audio_fill;
    if (_x_lock_port_rewiring (this->stream->xine, 0)) {
      all_fill += this->stream->audio_out->get_property (this->stream->audio_out, AO_PROP_PTS_IN_FIFO);
      _x_unlock_port_rewiring (this->stream->xine);
    }
  } else
    return;
  /* take actions */
  used = fifo->fifo_size;
  switch (this->dvbspeed) {
    case 1:
    case 4:
      if ((all_fill > this->dvbs_center + this->dvbs_width) ||
        (100 * used > 98 * fifo->buffer_pool_capacity)) {
        _x_set_fine_speed (this->stream, XINE_FINE_SPEED_NORMAL * 201 / 200);
        this->dvbspeed += 2;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "net_buf_ctrl: dvbspeed 100.5%% @ %s %d ms %d buffers\n", name, all_fill / 90, used);
      }
      break;
    case 7:
      if (_x_get_fine_speed (this->stream)) {
        /* Pause on first a/v buffer. Decoder headers went through at this time
           already, and xine_play is done waiting for that */
        _x_set_fine_speed (this->stream, 0);
        /* allow decoding while paused */
        _x_set_fine_speed (this->stream, XINE_LIVE_PAUSE_ON);
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: prebuffering...\n");
        break;
      }
      /* DVB streams usually mux video > 0.5 seconds earlier than audio
         to give slow TVs time to decode and present in sync. Take care
         of unusual high delays of some DVB-T streams */
      if (this->dvbs_audio_in && this->dvbs_video_in) {
        int64_t d = this->dvbs_video_in - this->dvbs_audio_in + 110000;
        if ((d < 3 * 90000) && (d > this->dvbs_center)) this->dvbs_center = d;
      }
      /* fall through */
    case 2:
    case 5:
      if ((all_fill > this->dvbs_center) || (100 * used > 73 * fifo->buffer_pool_capacity)) {
        _x_set_fine_speed (this->stream, XINE_FINE_SPEED_NORMAL);
        this->dvbspeed = (mode == BUF_VIDEO_BASE) ? 1 : 4;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "net_buf_ctrl: dvbspeed 100%% @ %s %d ms %d buffers\n", name, all_fill / 90, used);
        /* dont let low bitrate radio switch speed too often */
        if (used < 30) this->dvbs_width = 135000;
      }
    break;
  }
}

static int dvbspeed_get (xine_nbc_t *this, fifo_buffer_t * fifo, buf_element_t *b) {
  int all_fill, used, mode, pause = 0;
  const char *name;
  /* select vars */
  mode = b->type & BUF_MAJOR_MASK;
  if (mode == BUF_VIDEO_BASE) {
    /* update fifo fill time */
    if (b->pts) {
      if (this->dvbs_video_out) {
        int64_t diff = b->pts - this->dvbs_video_out;
        if ((diff > -220000) && (diff < 220000))
          this->dvbs_video_fill -= diff;
      }
      this->dvbs_video_out = b->pts;
    }
    if ((0x71 >> this->dvbspeed) & 1)
      return 0;
    name = "video";
    all_fill = this->dvbs_video_fill;
  } else if (mode == BUF_AUDIO_BASE) {
    /* update fifo fill time */
    if (b->pts) {
      if (this->dvbs_audio_out) {
        int64_t diff = b->pts - this->dvbs_audio_out;
        if ((diff > -220000) && (diff < 220000))
          this->dvbs_audio_fill -= diff;
      }
      this->dvbs_audio_out = b->pts;
    }
    if ((0x0f >> this->dvbspeed) & 1)
      return 0;
    name = "audio";
    all_fill = this->dvbs_audio_fill;
  } else
    return 0;
  /* take actions */
  used = fifo->fifo_size;
  switch (this->dvbspeed) {
    case 4:
      /* The usual 48kHz stereo mp2 can fill audio out fifo with > 7 seconds!! */
      if (_x_lock_port_rewiring (this->stream->xine, 0)) {
        all_fill += this->stream->audio_out->get_property (this->stream->audio_out, AO_PROP_PTS_IN_FIFO);
        _x_unlock_port_rewiring (this->stream->xine);
      }
      /* fall through */
    case 1:
      if (all_fill && (all_fill < this->dvbs_center - this->dvbs_width) &&
        (100 * used < 38 * fifo->buffer_pool_capacity)) {
        _x_set_fine_speed (this->stream, XINE_FINE_SPEED_NORMAL * 199 / 200);
        this->dvbspeed += 1;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "net_buf_ctrl: dvbspeed 99.5%% @ %s %d ms %d buffers\n", name, all_fill / 90, used);
      }
    break;
    case 2:
    case 5:
      if (used <= 1) {
        this->dvbspeed = 7;
        pause = 1;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: signal lost\n");
      }
    break;
    case 6:
      if (_x_lock_port_rewiring (this->stream->xine, 0)) {
        all_fill += this->stream->audio_out->get_property (this->stream->audio_out, AO_PROP_PTS_IN_FIFO);
        _x_unlock_port_rewiring (this->stream->xine);
      }
      /* fall through */
    case 3:
      if (all_fill && (all_fill < this->dvbs_center) && (100 * used < 73 * fifo->buffer_pool_capacity)) {
        _x_set_fine_speed (this->stream, XINE_FINE_SPEED_NORMAL);
        this->dvbspeed -= 2;
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "net_buf_ctrl: dvbspeed 100%% @ %s %d ms %d buffers\n", name, all_fill / 90, used);
      }
    break;
  }
  return pause;
}

void xine_nbc_event (xine_stream_private_t *stream, uint32_t type) {
  if (stream && (type == XINE_NBC_EVENT_AUDIO_DRY)) {
    stream = stream->side_streams[0];
    pthread_mutex_lock (&stream->counter.lock);
    if (stream->counter.nbc_refs <= 0) {
      pthread_mutex_unlock (&stream->counter.lock);
    } else {
      xine_nbc_t *this = stream->counter.nbc;
      stream->counter.nbc_refs += 1;
      pthread_mutex_unlock (&stream->counter.lock);
      pthread_mutex_lock (&this->mutex);
      switch (this->dvbspeed) {
        case 4:
        case 5:
        case 6:
          this->dvbspeed = 7;
          pthread_mutex_unlock (&this->mutex);
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: signal lost\n");
          _x_set_fine_speed (this->stream, 0);
          _x_set_fine_speed (this->stream, XINE_LIVE_PAUSE_ON);
          break;
        default:
          pthread_mutex_unlock (&this->mutex);
      }
      xine_nbc_close (this);
    }
  }
}

static void display_stats (xine_nbc_t *this) {
  static const char buffering[2][4] = {"   ", "buf"};
  static const char enabled[2][4]   = {"off", "on "};

  printf("net_buf_ctrl: vid %3d%% %4.1fs %4ukbps %1d, "\
	 "aud %3d%% %4.1fs %4ukbps %1d, %s %s%c",
	 this->video.fifo_fill,
	 (float)(this->video.fifo_length / 1000),
	 (unsigned int)this->video.br / 1000,
	 this->video.in_disc,
	 this->audio.fifo_fill,
	 (float)(this->audio.fifo_length / 1000),
	 (unsigned int)this->audio.br / 1000,
	 this->audio.in_disc,
	 buffering[this->buffering],
	 enabled[this->enabled],
	 isatty (STDOUT_FILENO) ? '\r' : '\n'
	 );
  fflush(stdout);
}

static void report_stats (xine_nbc_t *this, int type) {
  xine_event_t             event;
  xine_nbc_stats_data_t    bs;

  bs.v_percent = this->video.fifo_fill;
  bs.v_remaining = this->video.fifo_length;
  bs.v_bitrate = this->video.br;
  bs.v_in_disc = this->video.in_disc;
  bs.a_percent = this->audio.fifo_fill;
  bs.a_remaining = this->audio.fifo_length;
  bs.a_bitrate = this->audio.br;
  bs.a_in_disc = this->audio.in_disc;
  bs.buffering = this->buffering;
  bs.enabled = this->enabled;
  bs.type = type;

  event.type = XINE_EVENT_NBC_STATS;
  event.data = &bs;
  event.data_length = sizeof (xine_nbc_stats_data_t);

  xine_event_send (this->stream, &event);
}

/*  Try to compute the length of the fifo in 1/1000 s
 *  2 methods :
 *    if the bitrate is known
 *      use the size of the fifo
 *    else
 *      use the the first and the last pts of the fifo
 */
static void nbc_compute_fifo_length(xine_nbc_t *this,
                                    fifo_buffer_t *fifo,
                                    buf_element_t *buf,
                                    int action) {
  int64_t diff;

  /* faster than 4x _x_stream_info_get () */
  {
    xine_stream_private_t *s = (xine_stream_private_t *)this->stream;
    xine_rwlock_rdlock (&s->info_lock);
    this->has_video = s->stream_info[XINE_STREAM_INFO_HAS_VIDEO];
    this->has_audio = s->stream_info[XINE_STREAM_INFO_HAS_AUDIO];
    this->video.stream_br = s->stream_info[XINE_STREAM_INFO_VIDEO_BITRATE];
    this->audio.stream_br = s->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE];
    xine_rwlock_unlock (&s->info_lock);
  }

  {
    xine_nbc_fifo_info_t *info = (fifo == this->video.fifo) ? &this->video : &this->audio;
    int have_pts;

    info->fifo_size = fifo->fifo_data_size;
    info->fifo_free = fifo->buffer_pool_num_free;
    {
      int fifo_div = fifo->buffer_pool_num_free + fifo->fifo_size - 1;
      if (fifo_div == 0)
        fifo_div = 1; /* avoid a possible divide-by-zero */
      info->fifo_fill = 100 *  fifo->fifo_size / fifo_div;
    }

    have_pts = 0;
    if (buf->pts && (info->in_disc == 0)) {
      have_pts = 1;
      if (action == FIFO_PUT) {
        info->last_pts = buf->pts;
        if (info->first_pts == 0) {
          info->first_pts = buf->pts;
        }
      } else {
        /* GET */
        info->first_pts = buf->pts;
      }
    }

    if (info->stream_br) {
      info->br = info->stream_br;
      info->fifo_length_int = (uint64_t)8000 * info->fifo_size / info->br;
    } else {
      if (have_pts) {
        info->fifo_length_int = (info->last_pts - info->first_pts) / 90;
        if (info->fifo_length)
          info->br = (uint64_t)8000 * info->fifo_size / info->fifo_length;
        else
          info->br = 0;
      } else {
        if (info->br)
          info->fifo_length_int = (uint64_t)8000 * info->fifo_size / info->br;
      }
    }
  }

  /* decoder buffer compensation */
  if (this->has_audio && this->has_video) {
    diff = this->video.first_pts - this->audio.first_pts;
  } else {
    diff = 0;
  }
  if (diff > 0) {
    this->video.fifo_length = this->video.fifo_length_int + diff / 90;
    this->audio.fifo_length = this->audio.fifo_length_int;
  } else {
    this->video.fifo_length = this->video.fifo_length_int;
    this->audio.fifo_length = this->audio.fifo_length_int - diff / 90;
  }
}

/* Alloc callback */
static void nbc_alloc_cb (fifo_buffer_t *fifo, void *this_gen) {
  xine_nbc_t *this = this_gen;

  lprintf("enter nbc_alloc_cb\n");
  /* restart playing if one fifo is full (to avoid deadlock).
   * fifo is locked already, test this one first. */
  if (fifo->buffer_pool_num_free <= FULL_FIFO_MARK) {
    pthread_mutex_lock (&this->mutex);
    if (this->buffering && this->enabled) {
      this->progress = 100;
      this->buffering = 0;
      pthread_mutex_unlock (&this->mutex);
      nbc_set_speed_normal (this);
      report_progress (this->stream, 100);
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: nbc_alloc_cb: stops buffering\n");
    } else {
      pthread_mutex_unlock (&this->mutex);
    }
  }
  lprintf("exit nbc_alloc_cb\n");
}

/* Put callback
 * the fifo mutex is locked */
static void nbc_put_cb (fifo_buffer_t *fifo,
                        buf_element_t *buf, void *this_gen) {
  xine_nbc_t *this = this_gen;
  int pause = 0;

  lprintf("enter nbc_put_cb\n");
  pthread_mutex_lock(&this->mutex);

  if ((buf->type & BUF_MAJOR_MASK) != BUF_CONTROL_BASE) {

    if (this->enabled) {

      if (this->dvbspeed)
        dvbspeed_put (this, fifo, buf);
      else {
        nbc_compute_fifo_length(this, fifo, buf, FIFO_PUT);

        if (this->buffering) {
          /* restart playing if high_water_mark is reached by all fifos
           * do not restart if has_video and has_audio are false to avoid
           * a yoyo effect at the beginning of the stream when these values
           * are not yet known.
           *
           * be sure that the next buffer_pool_alloc() call will not deadlock,
           * we need at least 2 buffers (see buffer.c)
           */
          int progress;
          if (this->has_video) {
            if (this->has_audio) {
              if ((this->video.fifo_length > this->high_water_mark) &&
                  (this->audio.fifo_length > this->high_water_mark)) {
                progress = 100;
                this->buffering = 0;
              } else {
                /*  compute the buffering progress, 50%: video, 50%: audio */
                progress = (this->video.fifo_length + this->audio.fifo_length) * 50 / this->high_water_mark;
              }
            } else {
              if (this->video.fifo_length > this->high_water_mark) {
                progress = 100;
                this->buffering = 0;
              } else {
                progress = this->video.fifo_length * 100 / this->high_water_mark;
              }
            }
          } else {
            if (this->has_audio) {
              if (this->audio.fifo_length > this->high_water_mark) {
                progress = 100;
                this->buffering = 0;
              } else {
                progress = this->audio.fifo_length * 100 / this->high_water_mark;
              }
            } else {
              progress = 0;
            }
          }
          if (!this->buffering) {
            this->progress = 100;
            nbc_set_speed_normal (this);
            report_progress (this->stream, 100);
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: nbc_put_cb: stops buffering\n");
#if 0 /* WTF... */
            this->high_water_mark += this->high_water_mark / 2;
#endif
          } else {
            if (!progress) {
              /* if the progress can't be computed using the fifo length, use the number of buffers */
              progress = this->video.fifo_fill > this->audio.fifo_fill ? this->video.fifo_fill : this->audio.fifo_fill;
            }
            if (progress > this->progress) {
              this->progress = progress;
              report_progress (this->stream, progress);
            }
          }
          report_stats (this, 0);
          if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
            display_stats (this);
        }

      }
    }
  } else {

    switch (buf->type) {
      case BUF_CONTROL_START:
        lprintf("BUF_CONTROL_START\n");
        if (!this->enabled) {
          /* a new stream starts */
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: nbc_put_cb: starts buffering\n");
          this->enabled           = 1;
          this->buffering         = 1;
          this->video.first_pts   = 0;
          this->video.last_pts    = 0;
          this->audio.first_pts   = 0;
          this->audio.last_pts    = 0;
          this->video.fifo_length = 0;
          this->audio.fifo_length = 0;
          dvbspeed_init (this);
          if (!this->dvbspeed) pause = 1;
          this->progress = 0;
          report_progress (this->stream, 0);
        }
        break;
      case BUF_CONTROL_NOP:
        if (!(buf->decoder_flags & (BUF_FLAG_END_USER | BUF_FLAG_END_STREAM)))
          break;
        /* fall through */
      case BUF_CONTROL_END:
      case BUF_CONTROL_QUIT:
        lprintf("BUF_CONTROL_END\n");
        dvbspeed_close (this);
        if (this->enabled) {
          /* end of stream :
           *   - disable the nbc
           *   - unpause the engine if buffering
           */
          this->enabled = 0;

          lprintf("DISABLE netbuf\n");

          if (this->buffering) {
            this->buffering = 0;
            this->progress = 100;
            report_progress (this->stream, this->progress);

            xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: nbc_put_cb: stops buffering\n");

            nbc_set_speed_normal(this);
          }
        }
        break;

      case BUF_CONTROL_NEWPTS:
        /* discontinuity management */
        if (fifo == this->video.fifo) {
          this->video.in_disc++;
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		  "\nnet_buf_ctrl: nbc_put_cb video disc %d\n", this->video.in_disc);
        } else {
          this->audio.in_disc++;
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		  "\nnet_buf_ctrl: nbc_put_cb audio disc %d\n", this->audio.in_disc);
        }
        break;
    }

    if (fifo == this->video.fifo) {
      this->video.fifo_free = fifo->buffer_pool_num_free;
      this->video.fifo_size = fifo->fifo_data_size;
    } else {
      this->audio.fifo_free = fifo->buffer_pool_num_free;
      this->audio.fifo_size = fifo->fifo_data_size;
    }
  }
  pthread_mutex_unlock(&this->mutex);
  if (pause)
    nbc_set_speed_pause (this);
  lprintf("exit nbc_put_cb\n");
}

/* Get callback
 * the fifo mutex is locked */
static void nbc_get_cb (fifo_buffer_t *fifo,
			buf_element_t *buf, void *this_gen) {
  xine_nbc_t *this = this_gen;
  int pause = 0;

  lprintf("enter nbc_get_cb\n");
  pthread_mutex_lock(&this->mutex);

  if ((buf->type & BUF_MAJOR_MASK) != BUF_CONTROL_BASE) {

    if (this->enabled) {

      if (this->dvbspeed)
        pause = dvbspeed_get (this, fifo, buf);
      else {
        nbc_compute_fifo_length(this, fifo, buf, FIFO_GET);

        if (!this->buffering) {
          /* start buffering if one fifo is empty
           */
          if (((this->video.fifo_length == 0) && this->has_video) ||
              ((this->audio.fifo_length == 0) && this->has_audio)) {
            /* do not pause if a fifo is full to avoid yoyo (play-pause-play-pause) */
            if ((this->video.fifo_free > FULL_FIFO_MARK) &&
                (this->audio.fifo_free > FULL_FIFO_MARK)) {
              this->buffering = 1;
              this->progress  = 0;
              report_progress (this->stream, 0);

              xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                      "\nnet_buf_ctrl: nbc_get_cb: starts buffering, vid: %d, aud: %d\n",
                      this->video.fifo_fill, this->audio.fifo_fill);
              pause = 1;
            }
          }
          report_stats (this, 1);
          if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
            display_stats (this);
        } else {
          pause = 1;
        }
      }
    }
  } else {
    /* discontinuity management */
    if (buf->type == BUF_CONTROL_NEWPTS) {
      if (fifo == this->video.fifo) {
        this->video.in_disc--;
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"\nnet_buf_ctrl: nbc_get_cb video disc %d\n", this->video.in_disc);
      } else {
        this->audio.in_disc--;
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		"\nnet_buf_ctrl: nbc_get_cb audio disc %d\n", this->audio.in_disc);
      }
    }

    if (fifo == this->video.fifo) {
      this->video.fifo_free = fifo->buffer_pool_num_free;
      this->video.fifo_size = fifo->fifo_data_size;
    } else {
      this->audio.fifo_free = fifo->buffer_pool_num_free;
      this->audio.fifo_size = fifo->fifo_data_size;
    }
  }

  pthread_mutex_unlock(&this->mutex);
  if (pause)
    nbc_set_speed_pause (this);
  lprintf("exit nbc_get_cb\n");
}

xine_nbc_t *xine_nbc_init (xine_stream_t *stream) {
  xine_nbc_t *this;
  fifo_buffer_t *video_fifo, *audio_fifo;
  double video_fifo_factor, audio_fifo_factor;
  cfg_entry_t *entry;

  if (!stream)
    return NULL;

  {
    xine_stream_private_t *s = (xine_stream_private_t *)stream;
    s = s->side_streams[0];
    pthread_mutex_lock (&s->counter.lock);
    if (s->counter.nbc_refs > 0) {
      int refs;
      s->counter.nbc_refs += 1;
      refs = s->counter.nbc_refs;
      this = s->counter.nbc;
      pthread_mutex_unlock (&s->counter.lock);
      xprintf (s->s.xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: add to stream %p (%d refs).\n", (void *)s, refs);
      return this;
    }
    this = calloc (1, sizeof (*this));
    if (!this) {
      pthread_mutex_unlock (&s->counter.lock);
      return this;
    }
    s->counter.nbc_refs = 1;
    s->counter.nbc = this;
    pthread_mutex_unlock (&s->counter.lock);
    xine_refs_add (&s->refs, 1);
    xprintf (s->s.xine, XINE_VERBOSITY_DEBUG, "net_buf_ctrl: add to stream %p (1 refs).\n", (void *)s);
    stream = &s->s;
  }

  video_fifo = stream->video_fifo;
  audio_fifo = stream->audio_fifo;

  lprintf("nbc_init\n");
  pthread_mutex_init (&this->mutex, NULL);

  this->stream              = stream;
  this->video.fifo          = video_fifo;
  this->audio.fifo          = audio_fifo;

  /* when the FIFO sizes are increased compared to the default configuration,
   * apply a factor to the high water mark */
  entry = stream->xine->config->lookup_entry(stream->xine->config, "engine.buffers.video_num_buffers");
  /* No entry when no video output */
  if (entry)
    video_fifo_factor = (double)video_fifo->buffer_pool_capacity / (double)entry->num_default;
  else
    video_fifo_factor = 1.0;
  entry = stream->xine->config->lookup_entry(stream->xine->config, "engine.buffers.audio_num_buffers");
  /* When there's no audio output, there's no entry */
  if (entry)
    audio_fifo_factor = (double)audio_fifo->buffer_pool_capacity / (double)entry->num_default;
  else
    audio_fifo_factor = 1.0;
  /* use the smaller factor */
  if (video_fifo_factor < audio_fifo_factor)
    this->high_water_mark = (double)DEFAULT_HIGH_WATER_MARK * video_fifo_factor;
  else
    this->high_water_mark = (double)DEFAULT_HIGH_WATER_MARK * audio_fifo_factor;

  video_fifo->register_alloc_cb(video_fifo, nbc_alloc_cb, this);
  video_fifo->register_put_cb(video_fifo, nbc_put_cb, this);
  video_fifo->register_get_cb(video_fifo, nbc_get_cb, this);

  audio_fifo->register_alloc_cb(audio_fifo, nbc_alloc_cb, this);
  audio_fifo->register_put_cb(audio_fifo, nbc_put_cb, this);
  audio_fifo->register_get_cb(audio_fifo, nbc_get_cb, this);

  return this;
}

void xine_nbc_close (xine_nbc_t *this) {
  fifo_buffer_t *video_fifo, *audio_fifo;
  xine_t        *xine;

  if (!this)
    return;
  xine = this->stream->xine;
  {
    xine_stream_private_t *s = (xine_stream_private_t *)this->stream;
    int refs;
    pthread_mutex_lock (&s->counter.lock);
    s->counter.nbc_refs -= 1;
    refs = s->counter.nbc_refs;
    if (refs > 0) {
      pthread_mutex_unlock (&s->counter.lock);
      xprintf (xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: remove from stream %p (%d refs).\n", (void *)s, refs);
      return;
    }
    s->counter.nbc_refs = 0;
    s->counter.nbc = NULL;
    pthread_mutex_unlock (&s->counter.lock);
  }

  xprintf (xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: remove from stream %p (0 refs).\n", (void *)this->stream);
  video_fifo = this->stream->video_fifo;
  audio_fifo = this->stream->audio_fifo;

  /* unregister all fifo callbacks */
  /* do not lock the mutex to avoid deadlocks if a decoder calls fifo->get() */
  video_fifo->unregister_alloc_cb(video_fifo, nbc_alloc_cb);
  video_fifo->unregister_put_cb(video_fifo, nbc_put_cb);
  video_fifo->unregister_get_cb(video_fifo, nbc_get_cb);

  audio_fifo->unregister_alloc_cb(audio_fifo, nbc_alloc_cb);
  audio_fifo->unregister_put_cb(audio_fifo, nbc_put_cb);
  audio_fifo->unregister_get_cb(audio_fifo, nbc_get_cb);

  /* now we are sure that nobody will call a callback */
  this->stream->xine->clock->set_option (this->stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);

  pthread_mutex_destroy(&this->mutex);
  xprintf(xine, XINE_VERBOSITY_DEBUG, "\nnet_buf_ctrl: nbc_close: done\n");

  {
    xine_stream_private_t *s = (xine_stream_private_t *)this->stream;
    free (this);
    xine_refs_sub (&s->refs, 1);
  }
}

