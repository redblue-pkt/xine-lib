/*
 * Copyright (C) 2003-2021 the xine project
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
 */

/* NOTE: This will bend the xine engine into a certain direction (just to avoid the
 * term "misuse"). Demux keeps running all the time. Its the vdr server that
 * performs seeks, stream switches, still frames, trick play frames etc.
 * It then muxes the result down the line sequentially. For the demuxer, most stuff
 * looks like ordinary absolute discontinuities. We need to watch the control
 * messages coming through a side channel, and inject apropriate xine engine calls
 * manually. In reverse, we listen to xine events, and send back vdr keys.
 * "Trick play" is turned on and off by server. When on, xine shall just play all
 * frames as if they had perfectly consecutive time stamps. We still need to register
 * first discontinuity early because server will wait for it, and video decoder may
 * delay it -> freeze.
 * Issue #2: xine engine now uses a more efficient buffering scheme. Audio fifo
 * default now is 700*2k with soft start vs 230*8k fixed. This is needed to support
 * modern fragment streaming protocols. It also helps live DVB radio, and it speeds
 * up seeking. However, vdr seems to freeze from it sometimes. We work around it
 * by using fifo->buffer_pool_size_alloc (fifo, need), and by registering a dummy
 * alloc callback that disables file_buf_ctrl soft start.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <errno.h>
#include <pthread.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <resolv.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef WIN32
#include <winsock.h>
#endif

#define LOG_MODULE "input_vdr"
#define LOG_VERBOSE
/*
#define LOG
*/
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>

#include <xine/vdr.h>
#include "combined_vdr.h"

#define VDR_DISC_START (('V' << 24) | ('D' << 16) | ('R' << 8) | 1)
#define VDR_DISC_STOP  (('V' << 24) | ('D' << 16) | ('R' << 8) | 0)

#define VDR_MAX_NUM_WINDOWS 16
#define VDR_ABS_FIFO_DIR "/tmp/vdr-xine"

#define BUF_SIZE 1024

#define LOG_OSD(x)
/*
#define LOG_OSD(x) x
*/


typedef struct vdr_input_plugin_s vdr_input_plugin_t;

  /* This is our relay metronom, built on top of the engine one.
   * src/xine-engine/metronom.c uses a much more complex algorithm now.
   * One goal is to avoid unnecessary waiting. Thus lets not wait
   * ourselves here, and detect complete discontinuity pairs instead. */
typedef struct {
  metronom_t          metronom;
  metronom_t         *stream_metronom;
  vdr_input_plugin_t *input;
  pthread_mutex_t     mutex;
  struct {
    int               disc_num;
    int               seek;
    int               on;
  } audio, video;
  /* -1 = unset, 0 = off, 1 = on, 2 = first disc sent. */
  int                 trick_new_mode, trick_mode;
}
vdr_metronom_t;


typedef struct vdr_osd_s
{
  xine_osd_t *window;
  uint8_t    *argb_buffer[ 2 ];
  int         width;
  int         height;
}
vdr_osd_t;

/* This struct shall provide:
 * - backwards translation current vpts -> stream pts, and
 * - the information whether there are jumps that are not yet reached. */
typedef struct {
  int64_t offset; /* vpts - pts */
  int64_t vpts;   /* the vpts time that offset shall take effect from */
} vdr_vpts_offset_t;

struct vdr_input_plugin_s
{
  input_plugin_t      input_plugin;

  xine_stream_t      *stream;
  xine_stream_t      *stream_external;

  int                 is_netvdr;
  int                 fh;
  int                 fh_control;
  int                 fh_result;
  int                 fh_event;

  char               *mrl;

  off_t               curpos;

  enum funcs          cur_func;
  off_t               cur_size;
  off_t               cur_done;

  vdr_osd_t           osd[ VDR_MAX_NUM_WINDOWS ];
  uint8_t            *osd_buffer;
  uint32_t            osd_buffer_size;
  uint8_t             osd_unscaled_blending;
  uint8_t             osd_supports_custom_extent;
  uint8_t             osd_supports_argb_layer;

  uint8_t             audio_channels;
  uint8_t             mute_mode;
  uint8_t             volume_mode;
  int                 last_volume;
  vdr_frame_size_changed_data_t frame_size;

  pthread_t           rpc_thread;
  int                 rpc_thread_created;
  int                 rpc_thread_shutdown;
  pthread_mutex_t     rpc_thread_shutdown_lock;
  pthread_cond_t      rpc_thread_shutdown_cond;
  int                 startup_phase;

  xine_event_queue_t *event_queue;
  xine_event_queue_t *event_queue_external;

  pthread_mutex_t     adjust_zoom_lock;
  uint16_t            image4_3_zoom_x;
  uint16_t            image4_3_zoom_y;
  uint16_t            image16_9_zoom_x;
  uint16_t            image16_9_zoom_y;

  uint8_t             find_sync_point;
  pthread_mutex_t     find_sync_point_lock;

  vdr_metronom_t      metronom;
  int                 last_disc_type;

#define OFFS_RING_LD 7
#define OFFS_RING_NUM (1 << OFFS_RING_LD)
#define OFFS_RING_MASK (OFFS_RING_NUM - 1)
  struct {
    vdr_vpts_offset_t items[OFFS_RING_NUM];
    int               read;
    int               write;
    pthread_mutex_t   lock;
    pthread_cond_t    changed;
  } vpts_offs_queue;

  int                         video_window_active;
  vdr_set_video_window_data_t video_window_event_data;

  uint8_t             seek_buf[BUF_SIZE];
};

static void input_vdr_dummy (fifo_buffer_t *fifo, void *data) {
  (void)fifo;
  (void)data;
}

static void trick_speed_send_event (vdr_input_plugin_t *this, int mode) {
  xine_event_t event;

  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_vdr: trick play mode now %d.\n", mode);
  _x_demux_seek (this->stream, 0, 0, 0);
  event.type = XINE_EVENT_VDR_TRICKSPEEDMODE;
  event.data = NULL;
  event.data_length = mode;
  xine_event_send (this->stream, &event);
}

static int vdr_write(int f, void *b, int n)
{
  int t = 0, r;

  while (t < n)
  {
    /*
     * System calls are not a thread cancellation point in Linux
     * pthreads.  However, the RT signal sent to cancel the thread
     * will cause recv() to return with EINTR, and we can manually
     * check cancellation.
     */
    pthread_testcancel();
    r = write(f, ((char *)b) + t, n - t);
    pthread_testcancel();

    if (r < 0
        && (errno == EINTR
          || errno == EAGAIN))
    {
      continue;
    }

    if (r < 0)
      return r;

    t += r;
  }

  return t;
}



static int internal_write_event_play_external(vdr_input_plugin_t *this, uint32_t key);

static void event_handler_external(void *user_data, const xine_event_t *event)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)user_data;
  uint32_t key = key_none;
/*
  printf("event_handler_external(): event->type: %d\n", event->type);
*/
  switch (event->type)
  {
  case XINE_EVENT_UI_PLAYBACK_FINISHED:
    break;

  default:
    return;
  }

  if (0 != internal_write_event_play_external(this, key))
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            _("%s: input event write: %s.\n"), LOG_MODULE, strerror(errno));
}

static void external_stream_stop(vdr_input_plugin_t *this)
{
  if (this->stream_external)
  {
    xine_stop(this->stream_external);
    xine_close(this->stream_external);

    if (this->event_queue_external)
    {
      xine_event_dispose_queue(this->event_queue_external);
      this->event_queue_external = NULL;
    }

    _x_demux_flush_engine(this->stream_external);

    xine_dispose(this->stream_external);
    this->stream_external = NULL;
  }
}

static void external_stream_play(vdr_input_plugin_t *this, char *file_name)
{
  external_stream_stop(this);

  this->stream_external = xine_stream_new(this->stream->xine, this->stream->audio_out, this->stream->video_out);

  this->event_queue_external = xine_event_new_queue(this->stream_external);

  xine_event_create_listener_thread(this->event_queue_external, event_handler_external, this);

  if (!xine_open(this->stream_external, file_name)
      || !xine_play(this->stream_external, 0, 0))
  {
    uint32_t key = key_none;

    if ( 0 != internal_write_event_play_external(this, key))
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("%s: input event write: %s.\n"), LOG_MODULE, strerror(errno));
  }
}

static ssize_t vdr_read_abort (xine_stream_t *stream, int fd, uint8_t *buf, size_t todo) {
  ssize_t ret;

  while (1)
  {
    /*
     * System calls are not a thread cancellation point in Linux
     * pthreads.  However, the RT signal sent to cancel the thread
     * will cause recv() to return with EINTR, and we can manually
     * check cancellation.
     */
    pthread_testcancel();
    ret = _x_read_abort (stream, fd, (char *)buf, todo);
    pthread_testcancel();

    if (ret < 0
        && (errno == EINTR
          || errno == EAGAIN))
    {
      continue;
    }

    break;
  }

  return ret;
}

#define READ_DATA_OR_FAIL(kind, log) \
  data_##kind##_t *data = &data_union.kind; \
  { \
    log; \
    n = vdr_read_abort (this->stream, this->fh_control, (uint8_t *)data + sizeof (data->header), sizeof (*data) - sizeof (data->header)); \
    if (n != sizeof (*data) - sizeof (data->header)) \
      return -1; \
    \
    this->cur_size -= n; \
  }

static double _now()
{
  struct timeval tv;

  gettimeofday(&tv, 0);

  return (tv.tv_sec * 1000000.0 + tv.tv_usec) / 1000.0;
}

static void adjust_zoom(vdr_input_plugin_t *this)
{
  pthread_mutex_lock(&this->adjust_zoom_lock);

  if (this->image4_3_zoom_x && this->image4_3_zoom_y
    && this->image16_9_zoom_x && this->image16_9_zoom_y)
  {
    int ratio = (int)(10000 * this->frame_size.r + 0.5);
    int matches4_3 = abs(ratio - 13333);
    int matches16_9 = abs(ratio - 17778);

    /* fprintf(stderr, "ratio: %d\n", ratio); */
    if (matches4_3 < matches16_9)
    {
      xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_X, this->image4_3_zoom_x);
      xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_Y, this->image4_3_zoom_y);
    }
    else
    {
      xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_X, this->image16_9_zoom_x);
      xine_set_param(this->stream, XINE_PARAM_VO_ZOOM_Y, this->image16_9_zoom_y);
    }
  }

  pthread_mutex_unlock(&this->adjust_zoom_lock);
}


static void vdr_vpts_offset_queue_init (vdr_input_plugin_t *this) {
  pthread_mutex_init (&this->vpts_offs_queue.lock, NULL);
  pthread_cond_init (&this->vpts_offs_queue.changed, NULL);
  this->metronom.stream_metronom = this->stream->metronom;
  this->vpts_offs_queue.read = 0;
  this->vpts_offs_queue.write = 1;
  this->vpts_offs_queue.items[0].offset = this->metronom.stream_metronom->get_option (this->metronom.stream_metronom,
    METRONOM_VPTS_OFFSET);
  this->vpts_offs_queue.items[0].vpts = xine_get_current_vpts (this->stream);
}

static void vdr_vpts_offset_queue_deinit (vdr_input_plugin_t *this) {
  pthread_cond_destroy (&this->vpts_offs_queue.changed);
  pthread_mutex_destroy (&this->vpts_offs_queue.lock);
}

static void vdr_vpts_offset_queue_process (vdr_input_plugin_t *this, int64_t vpts) {
  int i = this->vpts_offs_queue.read;
  while (1) {
    int j = (i + 1) & OFFS_RING_MASK;
    if (j == this->vpts_offs_queue.write)
      break;
    if (this->vpts_offs_queue.items[j].vpts > vpts)
      break;
    i = j;
  }
  this->vpts_offs_queue.read = i;
}

static void vdr_vpts_offset_queue_add_int (vdr_input_plugin_t *this, int64_t pts) {
  int64_t offset = this->metronom.stream_metronom->get_option (this->metronom.stream_metronom, METRONOM_VPTS_OFFSET);
  int64_t vpts = pts + offset;
  this->vpts_offs_queue.items[this->vpts_offs_queue.write].offset = offset;
  this->vpts_offs_queue.items[this->vpts_offs_queue.write].vpts = vpts;
  this->vpts_offs_queue.write = (this->vpts_offs_queue.write + 1) & OFFS_RING_MASK;
  /* queue full, make some room */
  if (this->vpts_offs_queue.write == this->vpts_offs_queue.read)
    vdr_vpts_offset_queue_process (this, xine_get_current_vpts (this->stream));
}

static int vdr_vpts_offset_queue_ask (vdr_input_plugin_t *this, int64_t *pts) {
  int64_t vpts = xine_get_current_vpts (this->stream);
  vdr_vpts_offset_queue_process (this, vpts);
  *pts = vpts - this->vpts_offs_queue.items[this->vpts_offs_queue.read].offset;
  return ((this->vpts_offs_queue.write - this->vpts_offs_queue.read) & OFFS_RING_MASK) > 1;
}

static void vdr_vpts_offset_queue_purge (vdr_input_plugin_t *this) {
  this->vpts_offs_queue.read = (this->vpts_offs_queue.write - 1) & OFFS_RING_MASK;
}


static void vdr_start_buffers (vdr_input_plugin_t *this) {
  /* Make sure this sends DISC_STREAMSTART. */
  int gs = xine_get_param (this->stream, XINE_PARAM_GAPLESS_SWITCH);
  if (gs) {
    xine_set_param (this->stream, XINE_PARAM_GAPLESS_SWITCH, 0);
    _x_demux_control_start (this->stream);
    xine_set_param (this->stream, XINE_PARAM_GAPLESS_SWITCH, gs);
  } else {
    _x_demux_control_start (this->stream);
  }
}


static ssize_t vdr_execute_rpc_command (vdr_input_plugin_t *this) {
  data_union_t data_union;
  ssize_t n;

  n = vdr_read_abort (this->stream, this->fh_control, (uint8_t *)&data_union, sizeof (data_union.header));
  if (n != sizeof (data_union.header))
    return -1;

  this->cur_func = data_union.header.func;
  this->cur_size = data_union.header.len - sizeof (data_union.header);
  this->cur_done = 0;

  switch (this->cur_func)
  {
  case func_nop:
    {
      READ_DATA_OR_FAIL(nop, lprintf("got NOP\n"));
    }
    break;

  case func_osd_new:
    {
      READ_DATA_OR_FAIL(osd_new, LOG_OSD(lprintf("got OSDNEW\n")));
/*
      LOG_OSD(lprintf("... (%d,%d)-(%d,%d)@(%d,%d)\n", data->x, data->y, data->width, data->height, data->w_ref, data->h_ref));

      fprintf(stderr, "vdr: osdnew %d\n", data->window);
*/
      if (data->window >= VDR_MAX_NUM_WINDOWS)
        return -1;

      if (NULL != this->osd[ data->window ].window)
        return -1;

      this->osd[ data->window ].window = xine_osd_new(this->stream
                                                     , data->x
                                                     , data->y
                                                     , data->width
                                                     , data->height);

      this->osd[ data->window ].width  = data->width;
      this->osd[ data->window ].height = data->height;

      if (NULL == this->osd[ data->window ].window)
        return -1;

      if (this->osd_supports_custom_extent && data->w_ref > 0 && data->h_ref > 0)
        xine_osd_set_extent(this->osd[ data->window ].window, data->w_ref, data->h_ref);
    }
    break;

  case func_osd_free:
    {
      int i;
      READ_DATA_OR_FAIL(osd_free, LOG_OSD(lprintf("got OSDFREE\n")));
/*
      fprintf(stderr, "vdr: osdfree %d\n", data->window);
*/
      if (data->window >= VDR_MAX_NUM_WINDOWS)
        return -1;

      if (NULL != this->osd[ data->window ].window)
        xine_osd_free(this->osd[ data->window ].window);

      this->osd[ data->window ].window = NULL;

      for (i = 0; i < 2; i++)
      {
        free(this->osd[ data->window ].argb_buffer[ i ]);
        this->osd[ data->window ].argb_buffer[ i ] = NULL;
      }
    }
    break;

  case func_osd_show:
    {
      READ_DATA_OR_FAIL(osd_show, LOG_OSD(lprintf("got OSDSHOW\n")));
/*
      fprintf(stderr, "vdr: osdshow %d\n", data->window);
*/
      if (data->window >= VDR_MAX_NUM_WINDOWS)
        return -1;

      if (NULL != this->osd[ data->window ].window)
      {
#ifdef XINE_OSD_CAP_VIDEO_WINDOW
        xine_osd_set_video_window(this->osd[ data->window ].window
          , this->video_window_active ? this->video_window_event_data.x : 0
          , this->video_window_active ? this->video_window_event_data.y : 0
          , this->video_window_active ? this->video_window_event_data.w : 0
          , this->video_window_active ? this->video_window_event_data.h : 0);
#endif
        if (this->osd_unscaled_blending)
          xine_osd_show_unscaled(this->osd[ data->window ].window, 0);
        else
          xine_osd_show(this->osd[ data->window ].window, 0);
      }
    }
    break;

  case func_osd_hide:
    {
      READ_DATA_OR_FAIL(osd_hide, LOG_OSD(lprintf("got OSDHIDE\n")));
/*
      fprintf(stderr, "vdr: osdhide %d\n", data->window);
*/
      if (data->window >= VDR_MAX_NUM_WINDOWS)
        return -1;

      if (NULL != this->osd[ data->window ].window)
        xine_osd_hide(this->osd[ data->window ].window, 0);
    }
    break;

  case func_osd_flush:
    {
      double _t1/*, _t2*/;
      int _n = 0;
      /*int _to = 0;*/
      int r = 0;

      READ_DATA_OR_FAIL(osd_flush, LOG_OSD(lprintf("got OSDFLUSH\n")));
/*
      fprintf(stderr, "vdr: osdflush +\n");
*/
      _t1 = _now();

      while ((r = _x_query_unprocessed_osd_events(this->stream)))
      {
break;
        if ((_now() - _t1) > 200)
        {
          /*_to = 1;*/
          break;
        }
/*
        fprintf(stderr, "redraw_needed: 1\n");
*/
/*        sched_yield(); */
        xine_usec_sleep(5000);
        _n++;
      }
/*
      _t2 = _now();

      fprintf(stderr, "vdr: osdflush: n: %d, %.1lf, timeout: %d, result: %d\n", _n, _t2 - _t1, _to, r);
*/
/*
      fprintf(stderr, "redraw_needed: 0\n");

      fprintf(stderr, "vdr: osdflush -\n");
*/
    }
    break;

  case func_osd_set_position:
    {
      READ_DATA_OR_FAIL(osd_set_position, LOG_OSD(lprintf("got OSDSETPOSITION\n")));
/*
      fprintf(stderr, "vdr: osdsetposition %d\n", data->window);
*/
      if (data->window >= VDR_MAX_NUM_WINDOWS)
        return -1;

      if (NULL != this->osd[ data->window ].window)
        xine_osd_set_position(this->osd[ data->window ].window, data->x, data->y);
    }
    break;

  case func_osd_draw_bitmap:
    {
      READ_DATA_OR_FAIL(osd_draw_bitmap, LOG_OSD(lprintf("got OSDDRAWBITMAP\n")));
/*
      fprintf(stderr, "vdr: osddrawbitmap %d, %d, %d, %d, %d, %d\n", data->window, data->x, data->y, data->width, data->height, data->argb);
*/
      if (this->osd_buffer_size < this->cur_size)
      {
        free(this->osd_buffer);
        this->osd_buffer_size = 0;

        this->osd_buffer = calloc(1, this->cur_size);
        if (!this->osd_buffer)
          return -1;

        this->osd_buffer_size = this->cur_size;
      }

      n = vdr_read_abort (this->stream, this->fh_control, this->osd_buffer, this->cur_size);
      if (n != this->cur_size)
        return -1;

      this->cur_size -= n;

      if (data->window >= VDR_MAX_NUM_WINDOWS)
        return -1;

      if (NULL != this->osd[ data->window ].window)
      {
        vdr_osd_t *osd = &this->osd[ data->window ];

        if (data->argb)
        {
          int i;
          for (i = 0; i < 2; i++)
          {
            if (!osd->argb_buffer[ i ])
              osd->argb_buffer[ i ] = calloc(4 * osd->width, osd->height);

            {
              int src_stride = 4 * data->width;
              int dst_stride = 4 * osd->width;

              uint8_t *src = this->osd_buffer;
              uint8_t *dst = osd->argb_buffer[ i ] + data->y * dst_stride + data->x * 4;
              int y;

              if (src_stride == dst_stride)
                xine_fast_memcpy(dst, src, src_stride * (size_t)data->height);
              else
              {
                for (y = 0; y < data->height; y++)
                {
                  xine_fast_memcpy(dst, src, src_stride);
                  dst += dst_stride;
                  src += src_stride;
                }
              }
            }

            if (i == 0)
              xine_osd_set_argb_buffer(osd->window, (uint32_t *)osd->argb_buffer[ i ], data->x, data->y, data->width, data->height);
          }
          /* flip render and display buffer */
          {
            uint8_t *argb_buffer = osd->argb_buffer[ 0 ];
            osd->argb_buffer[ 0 ] = osd->argb_buffer[ 1 ];
            osd->argb_buffer[ 1 ] = argb_buffer;
          }
        }
        else
          xine_osd_draw_bitmap(osd->window, this->osd_buffer, data->x, data->y, data->width, data->height, 0);
      }
    }
    break;

  case func_set_color:
    {
      uint32_t vdr_color[ 256 ];

      READ_DATA_OR_FAIL(set_color, lprintf("got SETCOLOR\n"));

      if (((data->num + 1) * sizeof (uint32_t)) != (unsigned int)this->cur_size)
        return -1;

      n = vdr_read_abort (this->stream, this->fh_control, (uint8_t *)&vdr_color[ data->index ], this->cur_size);
      if (n != this->cur_size)
        return -1;

      this->cur_size -= n;

      if (data->window >= VDR_MAX_NUM_WINDOWS)
        return -1;

      if (NULL != this->osd[ data->window ].window)
      {
        uint32_t color[ 256 ];
        uint8_t trans[ 256 ];

        xine_osd_get_palette(this->osd[ data->window ].window, color, trans);

        {
          int i;

          for (i = data->index; i <= (data->index + data->num); i++)
          {
            int a = (vdr_color[ i ] & 0xff000000) >> 0x18;
            int r = (vdr_color[ i ] & 0x00ff0000) >> 0x10;
            int g = (vdr_color[ i ] & 0x0000ff00) >> 0x08;
            int b = (vdr_color[ i ] & 0x000000ff) >> 0x00;

            int y  = (( 66 * r + 129 * g +  25 * b + 128) >> 8) +  16;
            int cr = ((112 * r -  94 * g -  18 * b + 128) >> 8) + 128;
            int cb = ((-38 * r -  74 * g + 112 * b + 128) >> 8) + 128;

            uint8_t *dst = (uint8_t *)&color[ i ];
            *dst++ = cb;
            *dst++ = cr;
            *dst++ = y;
            *dst++ = 0;

            trans[ i ] = a >> 4;
          }
        }

        xine_osd_set_palette(this->osd[ data->window ].window, color, trans);
      }
    }
    break;

  case func_play_external:
    {
      char file_name[ 1024 ];
      int file_name_len = 0;

      READ_DATA_OR_FAIL(play_external, lprintf("got PLAYEXTERNAL\n"));

      file_name_len = this->cur_size;

      if (0 != file_name_len)
      {
        if (file_name_len <= 1
            || file_name_len > (int)sizeof (file_name))
        {
          return -1;
        }

        n = vdr_read_abort (this->stream, this->fh_control, (uint8_t *)file_name, file_name_len);
        if (n != file_name_len)
          return -1;

        if (file_name[ file_name_len - 1 ] != '\0')
          return -1;

        this->cur_size -= n;
      }

      lprintf((file_name_len > 0) ? "----------- play external: %s\n" : "---------- stop external\n", file_name);

      if (file_name_len > 0)
        external_stream_play(this, file_name);
      else
        external_stream_stop(this);
    }
    break;

  case func_clear:
    {
      READ_DATA_OR_FAIL(clear, lprintf("got CLEAR\n"));
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_vdr: clear (%d, %d, %u)\n", (int)data->n, (int)data->s, (unsigned int)data->i);

      {
        /* make sure engine is not paused. */
        int orig_speed = xine_get_param(this->stream, XINE_PARAM_FINE_SPEED);
        if (orig_speed <= XINE_FINE_SPEED_NORMAL / 3)
          xine_set_param (this->stream, XINE_PARAM_FINE_SPEED, XINE_FINE_SPEED_NORMAL);

        /* server seems to always send this 2 times: with s == 0, then again with s == 1.
         * lets try and ignore the latter. */
        if (!data->s) {
          /* let vdr_plugin_read () seek (skip) to server generated pes padding of 6 + 0xff00 + i bytes:
           * 00 00 01 be ff <i> ...
           * this flushes the main pipe, and tells demux about this. */
          pthread_mutex_lock (&this->find_sync_point_lock);
          this->find_sync_point = data->i;
          pthread_mutex_unlock (&this->find_sync_point_lock);
/*
          if (!this->dont_change_xine_volume)
            xine_set_param(this->stream, XINE_PARAM_AUDIO_VOLUME, 0);
*/
          /* start buffers are needed at least to reset audio track map.
           * demux_pes does pass pes audio id as track verbatim, and a
           * switch from a52 to mp2 or back would add a new track instead
           * of replacing the old one.
           * start bufs will force metronom DISC_STREAMSTART wait later.
           * we need to make sure vdr does not pause inbetween, and freeze.
           * workaround: send start first, then flush. flush will keep the
           * start bufs, and wait until both decoders have seen all this. */
/* fprintf(stderr, "=== CLEAR(%d.1)\n", data->n); */
          vdr_start_buffers (this);
          _x_demux_flush_engine (this->stream);
/* fprintf(stderr, "=== CLEAR(%d.2)\n", data->n); */
          /* _x_demux_seek (this->stream, 0, 0, 0); */
/* fprintf(stderr, "=== CLEAR(%d.3)\n", data->n); */

          /* XXX: why is this needed? */
          pthread_mutex_lock (&this->metronom.mutex);
          this->metronom.audio.seek = 1;
          this->metronom.video.seek = 1;
          pthread_mutex_unlock (&this->metronom.mutex);
          pthread_mutex_lock (&this->vpts_offs_queue.lock);
          this->last_disc_type = DISC_STREAMSTART;
          pthread_mutex_unlock (&this->vpts_offs_queue.lock);

          _x_stream_info_reset(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE);
/* fprintf(stderr, "=== CLEAR(%d.4)\n", data->n); */
          _x_meta_info_reset (this->stream, XINE_META_INFO_AUDIOCODEC);
/* fprintf(stderr, "=== CLEAR(%d.5)\n", data->n); */

          _x_trigger_relaxed_frame_drop_mode(this->stream);
/*        _x_reset_relaxed_frame_drop_mode(this->stream); */
/*
          if (!this->dont_change_xine_volume)
            xine_set_param(this->stream, XINE_PARAM_AUDIO_VOLUME, this->last_volume);
*/
        }
/* fprintf(stderr, "--- CLEAR(%d%c)\n", data->n, data->s ? 'b' : 'a'); */
        if (orig_speed <= XINE_FINE_SPEED_NORMAL / 3) {
          xine_set_param (this->stream, XINE_PARAM_FINE_SPEED, orig_speed);
          if (orig_speed == 0)
            /* make sure decoders are responsive. */
            xine_set_param (this->stream, XINE_PARAM_FINE_SPEED, XINE_LIVE_PAUSE_ON);
        }
      }
    }
    break;

  case func_first_frame:
    {
      READ_DATA_OR_FAIL(first_frame, lprintf("got FIRST FRAME\n"));
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_vdr: first_frame ()\n");

      _x_trigger_relaxed_frame_drop_mode(this->stream);
/*      _x_reset_relaxed_frame_drop_mode(this->stream); */
    }
    break;

  case func_still_frame:
    {
      READ_DATA_OR_FAIL(still_frame, lprintf("got STILL FRAME\n"));
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_vdr: still_frame ()\n");

      _x_reset_relaxed_frame_drop_mode(this->stream);
    }
    break;

  case func_set_video_window:
    {
      READ_DATA_OR_FAIL(set_video_window, lprintf("got SET VIDEO WINDOW\n"));
/*
      fprintf(stderr, "svw: (%d, %d)x(%d, %d), (%d, %d)\n", data->x, data->y, data->w, data->h, data->wRef, data->hRef);
*/
      {
        xine_event_t event;

        this->video_window_active = (data->x != 0
          || data->y != 0
          || data->w != data->w_ref
          || data->h != data->h_ref);

        this->video_window_event_data.x = data->x;
        this->video_window_event_data.y = data->y;
        this->video_window_event_data.w = data->w;
        this->video_window_event_data.h = data->h;
        this->video_window_event_data.w_ref = data->w_ref;
        this->video_window_event_data.h_ref = data->h_ref;

        event.type = XINE_EVENT_VDR_SETVIDEOWINDOW;
        event.data = &this->video_window_event_data;
        event.data_length = sizeof (this->video_window_event_data);

        xine_event_send(this->stream, &event);
      }
    }
    break;

  case func_select_audio:
    {
      READ_DATA_OR_FAIL(select_audio, lprintf("got SELECT AUDIO\n"));
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_vdr: select_audio (%d)\n", data->channels);

      this->audio_channels = data->channels;

      {
        xine_event_t event;
        vdr_select_audio_data_t event_data;

        event_data.channels = this->audio_channels;

        event.type = XINE_EVENT_VDR_SELECTAUDIO;
        event.data = &event_data;
        event.data_length = sizeof (event_data);

        xine_event_send(this->stream, &event);
      }
    }
    break;

  case func_trick_speed_mode:
    {
      READ_DATA_OR_FAIL(trick_speed_mode, lprintf("got TRICK SPEED MODE\n"));
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_vdr: trick_speed_mode (%d)\n", (int)data->on);

      pthread_mutex_lock (&this->metronom.mutex);
      if (this->metronom.audio.disc_num != this->metronom.video.disc_num) {
        this->metronom.trick_new_mode = data->on;
        pthread_mutex_unlock (&this->metronom.mutex);
      } else {
        this->metronom.trick_mode = data->on;
        this->metronom.trick_new_mode = -1;
        pthread_mutex_unlock (&this->metronom.mutex);
        trick_speed_send_event (this, this->metronom.trick_mode);
      }
    }
    break;

  case func_flush:
    {
      READ_DATA_OR_FAIL(flush, lprintf("got FLUSH\n"));

      if (!data->just_wait)
      {
        if (this->stream->video_fifo)
        {
          buf_element_t *buf = this->stream->video_fifo->buffer_pool_alloc(this->stream->video_fifo);
          if (!buf)
          {
            xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("%s: buffer_pool_alloc() failed!\n"), LOG_MODULE);
            return -1;
          }

          buf->type = BUF_CONTROL_FLUSH_DECODER;

          this->stream->video_fifo->put(this->stream->video_fifo, buf);
        }
      }

      {
        /*double _t1, _t2*/;
        int _n = 0;

        int vb = -1, ab = -1, vf = -1, af = -1;

        uint8_t timed_out = 0;

        struct timeval now, then;

        if (data->ms_timeout >= 0)
        {
          gettimeofday(&now, 0);

          then = now;
          then.tv_usec += (data->ms_timeout % 1000) * 1000;
          then.tv_sec  += (data->ms_timeout / 1000);

          if (then.tv_usec >= 1000000)
          {
            then.tv_usec -= 1000000;
            then.tv_sec  += 1;
          }
        }
        else
        {
          then.tv_usec = 0;
          then.tv_sec  = 0;
        }

        /*_t1 = _now();*/

        while (1)
        {
          _x_query_buffer_usage(this->stream, &vb, &ab, &vf, &af);

          if (vb <= 0 && ab <= 0 && vf <= 0 && af <= 0)
            break;

          if (data->ms_timeout >= 0
              && timercmp(&now, &then, >=))
          {
            timed_out++;
            break;
          }

/*          sched_yield(); */
          xine_usec_sleep(5000);
          _n++;

          if (data->ms_timeout >= 0)
            gettimeofday(&now, 0);
        }

        /*_t2 = _now();*/
        /* fprintf(stderr, "vdr: flush: n: %d, %.1lf\n", _n, _t2 - _t1); */

        xprintf(this->stream->xine
                , XINE_VERBOSITY_LOG
                , _("%s: flush buffers (vb: %d, ab: %d, vf: %d, af: %d) %s.\n")
                , LOG_MODULE, vb, ab, vf, af
                , (timed_out ? "timed out" : "done"));

        {
          result_flush_t result_flush;
          result_flush.header.func = data->header.func;
          result_flush.header.len = sizeof (result_flush);

          result_flush.timed_out = timed_out;

          if (sizeof (result_flush) != vdr_write(this->fh_result, &result_flush, sizeof (result_flush)))
            return -1;
        }
      }
    }
    break;

  case func_mute:
    {
      READ_DATA_OR_FAIL(mute, lprintf("got MUTE\n"));

      {
        int param_mute = (this->volume_mode == XINE_VDR_VOLUME_CHANGE_SW) ? XINE_PARAM_AUDIO_AMP_MUTE : XINE_PARAM_AUDIO_MUTE;
        xine_set_param(this->stream, param_mute, data->mute);
      }
    }
    break;

  case func_set_volume:
    {
/*double t3, t2, t1, t0;*/
      READ_DATA_OR_FAIL(set_volume, lprintf("got SETVOLUME\n"));
/*t0 = _now();*/
      {
        int change_volume = (this->volume_mode != XINE_VDR_VOLUME_IGNORE);
        int do_mute   = (this->last_volume != 0 && 0 == data->volume);
        int do_unmute = (this->last_volume <= 0 && 0 != data->volume);
        int report_change = 0;

        int param_mute   = (this->volume_mode == XINE_VDR_VOLUME_CHANGE_SW) ? XINE_PARAM_AUDIO_AMP_MUTE  : XINE_PARAM_AUDIO_MUTE;
        int param_volume = (this->volume_mode == XINE_VDR_VOLUME_CHANGE_SW) ? XINE_PARAM_AUDIO_AMP_LEVEL : XINE_PARAM_AUDIO_VOLUME;

        this->last_volume = data->volume;

        if (do_mute || do_unmute)
        {
          switch (this->mute_mode)
          {
          case XINE_VDR_MUTE_EXECUTE:
            report_change = 1;
            xine_set_param(this->stream, param_mute, do_mute);
            /* fall through */

          case XINE_VDR_MUTE_IGNORE:
            if (do_mute)
              change_volume = 0;
            break;

          case XINE_VDR_MUTE_SIMULATE:
            change_volume = 1;
            break;

          default:
            return -1;
          };
        }
/*t1 = _now();*/

        if (change_volume)
        {
          report_change = 1;
          xine_set_param(this->stream, param_volume, this->last_volume);
        }
/*t2 = _now();*/

        if (report_change && this->volume_mode != XINE_VDR_VOLUME_CHANGE_SW)
        {
          xine_event_t            event;
          xine_audio_level_data_t data;

          data.left
            = data.right
            = xine_get_param(this->stream, param_volume);
          data.mute
            = xine_get_param(this->stream, param_mute);
/*t3 = _now();*/

          event.type        = XINE_EVENT_AUDIO_LEVEL;
          event.data        = &data;
          event.data_length = sizeof (data);

          xine_event_send(this->stream, &event);
        }
      }
/* fprintf(stderr, "volume: %6.3lf ms, %6.3lf ms, %6.3lf ms\n", t1 - t0, t2 - t1, t3 - t2); */
    }
    break;

  case func_set_speed:
    {
      READ_DATA_OR_FAIL(set_speed, lprintf("got SETSPEED\n"));

      lprintf("... got SETSPEED %d\n", data->speed);

      if (data->speed != xine_get_param (this->stream, XINE_PARAM_FINE_SPEED)) {
        xine_set_param (this->stream, XINE_PARAM_FINE_SPEED, data->speed);
        if (data->speed == 0)
          /* make sure decoders are responsive. */
          xine_set_param (this->stream, XINE_PARAM_FINE_SPEED, XINE_LIVE_PAUSE_ON);
      }
    }
    break;

  case func_set_prebuffer:
    {
      READ_DATA_OR_FAIL(set_prebuffer, lprintf("got SETPREBUFFER\n"));
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_vdr: set_prebuffer (%d)\n", (int)data->prebuffer);

      xine_set_param (this->stream, XINE_PARAM_METRONOM_PREBUFFER, data->prebuffer);
    }
    break;

  case func_metronom:
    {
      READ_DATA_OR_FAIL(metronom, lprintf("got METRONOM\n"));
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_vdr: newpts (%"PRId64", 0x%08x)\n", data->pts, (unsigned int)data->flags);

      _x_demux_control_newpts(this->stream, data->pts, data->flags);
    }
    break;

  case func_start:
    {
      READ_DATA_OR_FAIL(start, lprintf("got START\n"));
      xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "input_vdr: start ()\n");

      vdr_start_buffers (this);
      _x_demux_seek(this->stream, 0, 0, 0);
    }
    break;

  case func_wait:
    {
      READ_DATA_OR_FAIL(wait, lprintf("got WAIT\n"));

      {
        result_wait_t result_wait;
        result_wait.header.func = data->header.func;
        result_wait.header.len = sizeof (result_wait);

        if (sizeof (result_wait) != vdr_write(this->fh_result, &result_wait, sizeof (result_wait)))
          return -1;

        if (data->id == 1)
          this->startup_phase = 0;
      }
    }
    break;

  case func_setup:
    {
      READ_DATA_OR_FAIL(setup, lprintf("got SETUP\n"));

      this->osd_unscaled_blending = data->osd_unscaled_blending;
      this->volume_mode           = data->volume_mode;
      this->mute_mode             = data->mute_mode;
      this->image4_3_zoom_x       = data->image4_3_zoom_x;
      this->image4_3_zoom_y       = data->image4_3_zoom_y;
      this->image16_9_zoom_x      = data->image16_9_zoom_x;
      this->image16_9_zoom_y      = data->image16_9_zoom_y;

      adjust_zoom(this);
    }
    break;

  case func_grab_image:
    {
      READ_DATA_OR_FAIL(grab_image, lprintf("got GRABIMAGE\n"));

      {
        off_t ret_val   = -1;

        xine_current_frame_data_t frame_data;
        memset(&frame_data, 0, sizeof (frame_data));

        if (xine_get_current_frame_data(this->stream, &frame_data, XINE_FRAME_DATA_ALLOCATE_IMG))
        {
          if (frame_data.ratio_code == XINE_VO_ASPECT_SQUARE)
            frame_data.ratio_code = 10000;
          else if (frame_data.ratio_code == XINE_VO_ASPECT_4_3)
            frame_data.ratio_code = 13333;
          else if (frame_data.ratio_code == XINE_VO_ASPECT_ANAMORPHIC)
            frame_data.ratio_code = 17778;
          else if (frame_data.ratio_code == XINE_VO_ASPECT_DVB)
            frame_data.ratio_code = 21100;
        }

        if (!frame_data.img)
          memset(&frame_data, 0, sizeof (frame_data));

        {
          result_grab_image_t result_grab_image;
          result_grab_image.header.func = data->header.func;
          result_grab_image.header.len = sizeof (result_grab_image) + frame_data.img_size;

          result_grab_image.width       = frame_data.width;
          result_grab_image.height      = frame_data.height;
          result_grab_image.ratio       = frame_data.ratio_code;
          result_grab_image.format      = frame_data.format;
          result_grab_image.interlaced  = frame_data.interlaced;
          result_grab_image.crop_left   = frame_data.crop_left;
          result_grab_image.crop_right  = frame_data.crop_right;
          result_grab_image.crop_top    = frame_data.crop_top;
          result_grab_image.crop_bottom = frame_data.crop_bottom;

          if (sizeof (result_grab_image) == vdr_write(this->fh_result, &result_grab_image, sizeof (result_grab_image)))
          {
            if (!frame_data.img_size || (frame_data.img_size == vdr_write(this->fh_result, frame_data.img, frame_data.img_size)))
              ret_val = 0;
          }
        }

        free(frame_data.img);

        if (ret_val != 0)
          return ret_val;
      }
    }
    break;

  case func_get_pts:
    {
      READ_DATA_OR_FAIL(get_pts, lprintf("got GETPTS\n"));

      {
        result_get_pts_t result_get_pts;
        result_get_pts.header.func = data->header.func;
        result_get_pts.header.len = sizeof (result_get_pts);

        pthread_mutex_lock(&this->vpts_offs_queue.lock);

        if (this->last_disc_type == DISC_STREAMSTART
          && data->ms_timeout > 0)
        {
          struct timespec abstime;
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
            "input_vdr: get_pts (%d ms)\n", (int)data->ms_timeout);
          {
            struct timeval now;
            gettimeofday(&now, 0);

            abstime.tv_sec = now.tv_sec + data->ms_timeout / 1000;
            abstime.tv_nsec = now.tv_usec * 1000 + (data->ms_timeout % 1000) * 1e6;

            if (abstime.tv_nsec > 1e9)
            {
              abstime.tv_nsec -= 1e9;
              abstime.tv_sec++;
            }
          }

          while (this->last_disc_type == DISC_STREAMSTART) {
            if (0 != pthread_cond_timedwait (&this->vpts_offs_queue.changed, &this->vpts_offs_queue.lock, &abstime))
              break;
          }
        }

        if (this->last_disc_type == DISC_STREAMSTART) {
          result_get_pts.pts    = -1;
          result_get_pts.queued = 0;
        }
        else
        {
          int64_t pts;
          result_get_pts.queued = vdr_vpts_offset_queue_ask (this, &pts);
          result_get_pts.pts = pts & ((1ll << 33) - 1);
/* fprintf(stderr, "vpts: %12ld, stc: %12ld, offset: %12ld\n", vpts, result_get_pts.pts, offset); */
        }

        pthread_mutex_unlock(&this->vpts_offs_queue.lock);

        if (sizeof (result_get_pts) != vdr_write(this->fh_result, &result_get_pts, sizeof (result_get_pts)))
          return -1;
      }
    }
    break;

  case func_get_version:
    {
      READ_DATA_OR_FAIL(get_version, lprintf("got GETVERSION\n"));

      {
        result_get_version_t result_get_version;
        result_get_version.header.func = data->header.func;
        result_get_version.header.len = sizeof (result_get_version);

        result_get_version.version = XINE_VDR_VERSION;

        if (sizeof (result_get_version) != vdr_write(this->fh_result, &result_get_version, sizeof (result_get_version)))
          return -1;
      }
    }
    break;

  case func_video_size:
    {
      READ_DATA_OR_FAIL(video_size, lprintf("got VIDEO SIZE\n"));

      {
        int format, width, height, ratio;

        result_video_size_t result_video_size;
        result_video_size.header.func = data->header.func;
        result_video_size.header.len = sizeof (result_video_size);

        result_video_size.top    = -1;
        result_video_size.left   = -1;
        result_video_size.width  = -1;
        result_video_size.height = -1;
        result_video_size.ratio  = 0;

        xine_get_current_frame_s(this->stream, &width, &height, &ratio, &format, NULL, NULL);
        result_video_size.width = width;
        result_video_size.height = height;
        result_video_size.ratio = ratio;

        if (ratio == XINE_VO_ASPECT_SQUARE)
          result_video_size.ratio = 10000;
        else if (ratio == XINE_VO_ASPECT_4_3)
          result_video_size.ratio = 13333;
        else if (ratio == XINE_VO_ASPECT_ANAMORPHIC)
          result_video_size.ratio = 17778;
        else if (ratio == XINE_VO_ASPECT_DVB)
          result_video_size.ratio = 21100;

        if (0 != this->frame_size.x
            || 0 != this->frame_size.y
            || 0 != this->frame_size.w
            || 0 != this->frame_size.h)
        {
          result_video_size.left   = this->frame_size.x;
          result_video_size.top    = this->frame_size.y;
          result_video_size.width  = this->frame_size.w;
          result_video_size.height = this->frame_size.h;
        }
/* fprintf(stderr, "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE\n"); */
        result_video_size.zoom_x = xine_get_param(this->stream, XINE_PARAM_VO_ZOOM_X);
        result_video_size.zoom_y = xine_get_param(this->stream, XINE_PARAM_VO_ZOOM_Y);
/* fprintf(stderr, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF\n"); */
        if (sizeof (result_video_size) != vdr_write(this->fh_result, &result_video_size, sizeof (result_video_size)))
          return -1;
/* fprintf(stderr, "GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG\n"); */
      }
    }
    break;

  case func_reset_audio:
    {
      /*double _t1, _t2;*/
      int _n = 0;

      READ_DATA_OR_FAIL(reset_audio, lprintf("got RESET AUDIO\n"));

      if (this->stream->audio_fifo)
      {
        xine_set_param(this->stream, XINE_PARAM_IGNORE_AUDIO, 1);
        xine_set_param(this->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, -2);

        /*_t1 = _now();*/

        while (1)
        {
          int n = xine_get_stream_info(this->stream, XINE_STREAM_INFO_MAX_AUDIO_CHANNEL);
          if (n <= 0)
            break;

          /* keep the decoder running */
          if (this->stream->audio_fifo)
          {
            buf_element_t *buf = this->stream->audio_fifo->buffer_pool_alloc(this->stream->audio_fifo);
            if (!buf)
            {
              xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("%s: buffer_pool_alloc() failed!\n"), LOG_MODULE);
              return -1;
            }

            buf->type = BUF_CONTROL_RESET_TRACK_MAP;

            this->stream->audio_fifo->put(this->stream->audio_fifo, buf);
          }

/*          sched_yield(); */
          xine_usec_sleep(5000);
          _n++;
        }

        /*_t2 = _now();*/
        /* fprintf(stderr, "vdr: reset_audio: n: %d, %.1lf\n", _n, _t2 - _t1); */

        xine_set_param(this->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, -1);

        _x_stream_info_reset(this->stream, XINE_STREAM_INFO_AUDIO_BITRATE);
        _x_meta_info_reset(this->stream, XINE_META_INFO_AUDIOCODEC);

        xine_set_param(this->stream, XINE_PARAM_IGNORE_AUDIO, 0);
      }
    }
    break;

  case func_query_capabilities:
    {
      READ_DATA_OR_FAIL(query_capabilities, lprintf("got QUERYCAPABILITIES\n"));

      {
        result_query_capabilities_t result_query_capabilities;
        result_query_capabilities.header.func = data->header.func;
        result_query_capabilities.header.len = sizeof (result_query_capabilities);

        result_query_capabilities.osd_max_num_windows = MAX_SHOWING;
        result_query_capabilities.osd_palette_max_depth = 8;
        result_query_capabilities.osd_palette_is_shared = 0;
        result_query_capabilities.osd_supports_argb_layer = this->osd_supports_argb_layer;
        result_query_capabilities.osd_supports_custom_extent = this->osd_supports_custom_extent;

        if (sizeof (result_query_capabilities) != vdr_write(this->fh_result, &result_query_capabilities, sizeof (result_query_capabilities)))
          return -1;
      }
    }
    break;

  default:
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
      "input_vdr: unknown function #%d\n", (int)this->cur_func);
  }

  if (this->cur_size != this->cur_done)
  {
    off_t skip = this->cur_size - this->cur_done;

    lprintf("func: %d, skipping: %" PRId64 "\n", this->cur_func, (int64_t)skip);

    while (skip > BUF_SIZE)
    {
      n = vdr_read_abort(this->stream, this->fh_control, this->seek_buf, BUF_SIZE);
      if (n != BUF_SIZE)
        return -1;

      skip -= BUF_SIZE;
    }

    n = vdr_read_abort(this->stream, this->fh_control, this->seek_buf, skip);
    if (n != skip)
      return -1;

    this->cur_done = this->cur_size;

    return -1;
  }

  return 0;
}

static void *vdr_rpc_thread_loop(void *arg)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)arg;
  int frontend_lock_failures = 0;
  int failed = 0;
  int was_startup_phase = this->startup_phase;

  while (!failed
    && !this->rpc_thread_shutdown
    && was_startup_phase == this->startup_phase)
  {
    struct timeval timeout;
    fd_set rset;

    FD_ZERO(&rset);
    FD_SET(this->fh_control, &rset);

    timeout.tv_sec  = 0;
    timeout.tv_usec = 50000;

    if (select(this->fh_control + 1, &rset, NULL, NULL, &timeout) > 0)
    {
      if (!_x_lock_frontend(this->stream, 100))
      {
        if (++frontend_lock_failures > 50)
        {
          failed = 1;
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  LOG_MODULE ": locking frontend for rpc command execution failed, exiting ...\n");
        }
      }
      else
      {
        frontend_lock_failures = 0;

        if (_x_lock_port_rewiring(this->stream->xine, 100))
        {
          if (vdr_execute_rpc_command(this) < 0)
          {
            failed = 1;
            xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                    LOG_MODULE ": execution of rpc command %d (%s) failed, exiting ...\n", this->cur_func, "");
          }

          _x_unlock_port_rewiring(this->stream->xine);
        }

        _x_unlock_frontend(this->stream);
      }
    }
  }

  if (!failed && was_startup_phase)
    return (void *)1;

  /* close control and result channel here to have vdr-xine initiate a disconnect for the above error case ... */
  close(this->fh_control);
  this->fh_control = -1;

  close(this->fh_result);
  this->fh_result = -1;

  xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
          LOG_MODULE ": rpc thread done.\n");

  pthread_mutex_lock(&this->rpc_thread_shutdown_lock);
  this->rpc_thread_shutdown = -1;
  pthread_cond_broadcast(&this->rpc_thread_shutdown_cond);
  pthread_mutex_unlock(&this->rpc_thread_shutdown_lock);

  return 0;
}

static int internal_write_event_key(vdr_input_plugin_t *this, uint32_t key)
{
  event_key_t event;
  event.header.func = func_key;
  event.header.len = sizeof (event);

  event.key = key;

  if (sizeof (event) != vdr_write(this->fh_event, &event, sizeof (event)))
    return -1;

  return 0;
}

static int internal_write_event_frame_size(vdr_input_plugin_t *this)
{
  event_frame_size_t event;
  event.header.func = func_frame_size;
  event.header.len = sizeof (event);

  event.left   = this->frame_size.x;
  event.top    = this->frame_size.y;
  event.width  = this->frame_size.w,
  event.height = this->frame_size.h;
  event.zoom_x = xine_get_param(this->stream, XINE_PARAM_VO_ZOOM_X);
  event.zoom_y = xine_get_param(this->stream, XINE_PARAM_VO_ZOOM_Y);

  if (sizeof (event) != vdr_write(this->fh_event, &event, sizeof (event)))
    return -1;

  return 0;
}

static int internal_write_event_play_external(vdr_input_plugin_t *this, uint32_t key)
{
  event_play_external_t event;
  event.header.func = func_play_external;
  event.header.len = sizeof (event);

  event.key = key;

  if (sizeof (event) != vdr_write(this->fh_event, &event, sizeof (event)))
    return -1;

  return 0;
}

static int internal_write_event_discontinuity(vdr_input_plugin_t *this, int32_t type)
{
  event_discontinuity_t event;
  event.header.func = func_discontinuity;
  event.header.len = sizeof (event);

  event.type = type;

  if (sizeof (event) != vdr_write(this->fh_event, &event, sizeof (event)))
    return -1;

  return 0;
}

static ssize_t vdr_main_read (vdr_input_plugin_t *this, uint8_t *buf, ssize_t len) {
  ssize_t have = 0, n = 0;

  if (this->is_netvdr) {

    n = _x_io_tcp_read (this->stream, this->fh, buf + have, len - have);
    if (n >= 0) {
      this->curpos += n;
      have += n;
#ifdef LOG_READ
      lprintf ("got %d bytes (%d/%d bytes read)\n", (int)n, (int)have, (int)len);
#endif
    }

  } else {

    int retries = 0;
    while (have < len) {
      n = read (this->fh, buf + have, len - have);
      if (n > 0) {
        retries = 0;
        this->curpos += n;
        have += n;
#ifdef LOG_READ
        lprintf ("got %d bytes (%d/%d bytes read)\n", (int)n, (int)have, (int)len);
#endif
        continue;
      }
      if (n < 0) {
        int e = errno;
        if (e == EINTR)
          continue;
        if (e != EAGAIN)
          break;
        do {
          fd_set rset;
          struct timeval timeout;
          if (_x_action_pending (this->stream) || this->stream_external || !_x_continue_stream_processing (this->stream)) {
            errno = EINTR;
            break;
          }
          FD_ZERO (&rset);
          FD_SET (this->fh, &rset);
          timeout.tv_sec  = 0;
          timeout.tv_usec = 50000;
          e = select (this->fh + 1, &rset, NULL, NULL, &timeout);
        } while (e == 0);
        if (e < 0)
          break;
      } else { /* n == 0 (pipe not yet open for writing) */
        struct timeval timeout;
        if (_x_action_pending (this->stream) || this->stream_external || !_x_continue_stream_processing (this->stream)) {
          errno = EINTR;
          break;
        }
        if (++retries >= 200) { /* 200 * 50ms */
          errno = ETIMEDOUT;
          break;
        }
        lprintf ("read 0, retries: %d\n", retries);
        timeout.tv_sec  = 0;
        timeout.tv_usec = 50000;
        select (0, NULL, NULL, NULL, &timeout);
      }
    }

  }
  if ((n < 0) && (errno != EINTR))
    _x_message (this->stream, XINE_MSG_READ_ERROR, NULL);
  return have;
}

static off_t vdr_plugin_read (input_plugin_t *this_gen, void *buf_gen, off_t len) {
  vdr_input_plugin_t  *this = (vdr_input_plugin_t *) this_gen;
  uint8_t *buf = (uint8_t *)buf_gen;
  ssize_t have;
#ifdef LOG_READ
  lprintf ("reading %d bytes...\n", (int)len);
#endif

  have = vdr_main_read (this, buf, len);

  /* HACK: demux_pes always reads 6 byte pes heads. */
  if (have == 6) {
    pthread_mutex_lock (&this->find_sync_point_lock);

    while (this->find_sync_point && (have == 6) && (buf[0] == 0x00) && (buf[1] == 0x00) && (buf[2] == 0x01)) {
      int l;
      /* sync point? */
      if ((buf[3] == 0xbe) && (buf[4] == 0xff)) {
        xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
          "input_vdr: found sync point %d.\n", (int)buf[5]);
        if (buf[5] == this->find_sync_point) {
          this->find_sync_point = 0;
          break;
        }
      }
      /* unknown packet type? */
      if (((buf[3] & 0xf0) != 0xe0) && ((buf[3] & 0xe0) != 0xc0) && (buf[3] != 0xbd) && (buf[3] != 0xbe))
        break;
      /* payload size */
      l = ((uint32_t)buf[4] << 8) + buf[5];
      if (l <= 0)
         break;
      /* skip this */
      while (l >= (int)sizeof (this->seek_buf)) {
        int n = vdr_main_read (this, this->seek_buf, sizeof (this->seek_buf));
        if (n <= 0)
          break;
        l -= n;
      }
      if (l >= (int)sizeof (this->seek_buf))
        break;
      if (l > 0) {
        int n = vdr_main_read (this, this->seek_buf, l);
        if (n < l)
          break;
      }
      /* get next head */
      have = vdr_main_read (this, buf, 6);
    }

    pthread_mutex_unlock(&this->find_sync_point_lock);
  }

  return have;
}

static buf_element_t *vdr_plugin_read_block(input_plugin_t *this_gen, fifo_buffer_t *fifo,
                                            off_t todo)
{
  off_t          total_bytes;
  buf_element_t *buf;

  if (todo < 0)
    return NULL;

  buf = fifo->buffer_pool_size_alloc (fifo, todo);

  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  if (todo > buf->max_size)
    todo = buf->max_size;

  total_bytes = vdr_plugin_read(this_gen, (char *)buf->content, todo);

  if (total_bytes != todo)
  {
    buf->free_buffer(buf);
    return NULL;
  }

  buf->size = total_bytes;

  return buf;
}

/* forward reference */
static off_t vdr_plugin_get_current_pos(input_plugin_t *this_gen);

static off_t vdr_plugin_seek(input_plugin_t *this_gen, off_t offset, int origin)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)this_gen;

  lprintf("seek %" PRId64 " offset, %d origin...\n",
          (int64_t)offset, origin);

  if (origin == SEEK_SET) {
    if (offset < this->curpos) {
      lprintf ("cannot seek back! (%" PRId64 " > %" PRId64 ")\n",
        (int64_t)this->curpos, (int64_t)offset);
      return this->curpos;
    }
    offset -= this->curpos;
    origin = SEEK_CUR;
  }

  if (origin == SEEK_CUR) {
    while (offset > 0) {
      int part = offset > BUF_SIZE ? BUF_SIZE : offset;
      part = this_gen->read (this_gen, this->seek_buf, part);
      if (part <= 0)
        break;
      this->curpos += part;
      offset -= part;
    }
  }

  return this->curpos;
}

static off_t vdr_plugin_get_length(input_plugin_t *this_gen)
{
  (void)this_gen;
  return 0;
}

static uint32_t vdr_plugin_get_capabilities(input_plugin_t *this_gen)
{
  (void)this_gen;
  return INPUT_CAP_PREVIEW | INPUT_CAP_NO_CACHE;
}

static uint32_t vdr_plugin_get_blocksize(input_plugin_t *this_gen)
{
  (void)this_gen;
  return 0;
}

static off_t vdr_plugin_get_current_pos(input_plugin_t *this_gen)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)this_gen;

  return this->curpos;
}

static const char *vdr_plugin_get_mrl(input_plugin_t *this_gen)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)this_gen;

  return this->mrl;
}

static void vdr_plugin_dispose(input_plugin_t *this_gen)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)this_gen;
  int i;

  external_stream_stop(this);

  if (this->event_queue)
    xine_event_dispose_queue(this->event_queue);

  if (this->rpc_thread_created)
  {
    struct timespec abstime;
    int ms_to_time_out = 10000;

    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("%s: shutting down rpc thread (timeout: %d ms) ...\n"), LOG_MODULE, ms_to_time_out);

    pthread_mutex_lock(&this->rpc_thread_shutdown_lock);

    if (this->rpc_thread_shutdown > -1)
    {
      this->rpc_thread_shutdown = 1;

      {
        struct timeval now;
        gettimeofday(&now, 0);

        abstime.tv_sec = now.tv_sec + ms_to_time_out / 1000;
        abstime.tv_nsec = now.tv_usec * 1000 + (ms_to_time_out % 1000) * 1e6;

        if (abstime.tv_nsec > 1e9)
        {
          abstime.tv_nsec -= 1e9;
          abstime.tv_sec++;
        }
      }

      if (0 != pthread_cond_timedwait(&this->rpc_thread_shutdown_cond, &this->rpc_thread_shutdown_lock, &abstime))
      {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("%s: cancelling rpc thread in function %d...\n"), LOG_MODULE, this->cur_func);
        pthread_cancel(this->rpc_thread);
      }
    }

    pthread_mutex_unlock(&this->rpc_thread_shutdown_lock);

    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("%s: joining rpc thread ...\n"), LOG_MODULE);
    pthread_join(this->rpc_thread, 0);
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, _("%s: rpc thread joined.\n"), LOG_MODULE);
  }

  pthread_cond_destroy(&this->rpc_thread_shutdown_cond);
  pthread_mutex_destroy(&this->rpc_thread_shutdown_lock);

  pthread_mutex_destroy(&this->find_sync_point_lock);
  pthread_mutex_destroy(&this->adjust_zoom_lock);

  if (this->fh_result != -1)
    close(this->fh_result);

  if (this->fh_control != -1)
    close(this->fh_control);

  if (this->fh_event != -1)
    close(this->fh_event);

  for (i = 0; i < VDR_MAX_NUM_WINDOWS; i++)
  {
    int k;

    if (NULL == this->osd[ i ].window)
      continue;

    xine_osd_hide(this->osd[ i ].window, 0);
    xine_osd_free(this->osd[ i ].window);

    for (k = 0; k < 2; k++)
      free(this->osd[ i ].argb_buffer[ k ]);
  }

  if (this->osd_buffer)
    free(this->osd_buffer);

  if ((this->fh != STDIN_FILENO) && (this->fh != -1))
    close(this->fh);

  free(this->mrl);

  /* unset metronom */
  this->stream->metronom = this->metronom.stream_metronom;
  this->metronom.stream_metronom = NULL;

  vdr_vpts_offset_queue_purge (this);
  vdr_vpts_offset_queue_deinit (this);

  pthread_mutex_destroy (&this->metronom.mutex);

  /* see comment above */
  if (this->stream->audio_fifo)
    this->stream->audio_fifo->unregister_alloc_cb (this->stream->audio_fifo, input_vdr_dummy);
  if (this->stream->video_fifo)
    this->stream->video_fifo->unregister_alloc_cb (this->stream->video_fifo, input_vdr_dummy);

  free(this);
}

static int vdr_plugin_get_optional_data(input_plugin_t *this_gen,
                                        void *data, int data_type)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)this_gen;
  (void)this;
  switch (data_type)
  {
  case INPUT_OPTIONAL_DATA_PREVIEW:
    /* just fake what mpeg_pes demuxer expects */
    memcpy (data, "\x00\x00\x01\xe0\x00\x03\x80\x00\x00", 9);
    return 9;
  }
  return INPUT_OPTIONAL_UNSUPPORTED;
}

static inline const char *mrl_to_fifo (const char *mrl)
{
  /* vdr://foo -> /foo */
  return mrl + 3 + strspn (mrl + 4, "/");
}

static inline const char *mrl_to_host (const char *mrl)
{
  /* netvdr://host:port -> host:port */
  return strrchr (mrl, '/') + 1;
}

static int vdr_plugin_open_fifo_mrl(input_plugin_t *this_gen)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)this_gen;
  const char *fifoname = mrl_to_fifo (this->mrl);

  if(!strcmp(fifoname, "/")) {
    fifoname = VDR_ABS_FIFO_DIR "/stream";
  }

  char *filename = strdup(fifoname);

  _x_mrl_unescape (filename);
  this->fh = xine_open_cloexec(filename, O_RDONLY | O_NONBLOCK);

  lprintf("filename '%s'\n", filename);

  if (this->fh == -1)
  {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            _("%s: failed to open '%s' (%s)\n"), LOG_MODULE,
            filename,
            strerror(errno));
    free (filename);
    return 0;
  }

  {
    struct pollfd poll_fh = { this->fh, POLLIN, 0 };

    int r = poll(&poll_fh, 1, 300);
    if (1 != r)
    {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("%s: failed to open '%s' (%s)\n"), LOG_MODULE,
              filename,
              _("timeout expired during setup phase"));
      free (filename);
      return 0;
    }
  }

  fcntl(this->fh, F_SETFL, ~O_NONBLOCK & fcntl(this->fh, F_GETFL, 0));

  /* eat initial handshake byte */
  {
    char b;
    if (1 != read(this->fh, &b, 1)) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("%s: failed to read '%s' (%s)\n"), LOG_MODULE,
	      filename,
	      strerror(errno));
    }
  }

  {
    char *filename_control = NULL;
    filename_control = _x_asprintf("%s.control", filename);

    this->fh_control = xine_open_cloexec(filename_control, O_RDONLY);

    if (this->fh_control == -1) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("%s: failed to open '%s' (%s)\n"), LOG_MODULE,
              filename_control,
              strerror(errno));

      free(filename_control);
      free (filename);
      return 0;
    }

    free(filename_control);
  }

  {
    char *filename_result = NULL;
    filename_result = _x_asprintf("%s.result", filename);

    this->fh_result = xine_open_cloexec(filename_result, O_WRONLY);

    if (this->fh_result == -1) {
      perror("failed");

      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("%s: failed to open '%s' (%s)\n"), LOG_MODULE,
              filename_result,
              strerror(errno));

      free(filename_result);
      free (filename);
      return 0;
    }

    free(filename_result);
  }

  {
    char *filename_event = NULL;
    filename_event = _x_asprintf("%s.event", filename);

    this->fh_event = xine_open_cloexec(filename_event, O_WRONLY);

    if (this->fh_event == -1) {
      perror("failed");

      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("%s: failed to open '%s' (%s)\n"), LOG_MODULE,
              filename_event,
              strerror(errno));

      free(filename_event);
      free (filename);
      return 0;
    }

    free(filename_event);
  }

  free (filename);
  return 1;
}

static int vdr_plugin_open_socket(vdr_input_plugin_t *this, struct hostent *host, unsigned short port)
{
  int fd;
  struct sockaddr_in sain;
  struct in_addr iaddr;

  if ((fd = xine_socket_cloexec(PF_INET, SOCK_STREAM, 0)) == -1)
  {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            _("%s: failed to create socket for port %d (%s)\n"), LOG_MODULE,
            port, strerror(errno));
    return -1;
  }

  iaddr.s_addr = *((unsigned int *)host->h_addr_list[0]);

  sain.sin_port = htons(port);
  sain.sin_family = AF_INET;
  sain.sin_addr = iaddr;

  if (connect(fd, (struct sockaddr *)&sain, sizeof (sain)) < 0)
  {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            _("%s: failed to connect to port %d (%s)\n"), LOG_MODULE, port,
            strerror(errno));
    close(fd);

    return -1;
  }

  xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
          _("%s: socket opening (port %d) successful, fd = %d\n"), LOG_MODULE, port, fd);

  return fd;
}

static int vdr_plugin_open_sockets(vdr_input_plugin_t *this)
{
  struct hostent *host;
  char *mrl_host = strdup (mrl_to_host (this->mrl));
  char *mrl_port;
  int port = 18701;

  mrl_port = strchr(mrl_host, '#');
  if (mrl_port)
    *mrl_port = 0; /* strip off things like '#demux:mpeg_pes' */

  _x_mrl_unescape (mrl_host);

  mrl_port = strchr(mrl_host, ':');
  if (mrl_port)
  {
    port = atoi(mrl_port + 1);
    *mrl_port = 0;
  }

  host = gethostbyname(mrl_host);

  xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
          _("%s: connecting to vdr.\n"), LOG_MODULE);

  if (!host)
  {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            _("%s: failed to resolve hostname '%s' (%s)\n"), LOG_MODULE,
            mrl_host,
            strerror(errno));
    free (mrl_host);
    return 0;
  }
  free (mrl_host);

  if ((this->fh = vdr_plugin_open_socket(this, host, port + 0)) == -1)
    return 0;

  fcntl(this->fh, F_SETFL, ~O_NONBLOCK & fcntl(this->fh, F_GETFL, 0));

  if ((this->fh_control = vdr_plugin_open_socket(this, host, port + 1)) == -1)
    return 0;

  if ((this->fh_result = vdr_plugin_open_socket(this, host, port + 2)) == -1)
    return 0;

  if ((this->fh_event = vdr_plugin_open_socket(this, host, port + 3)) == -1)
    return 0;

  xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
          _("%s: connecting to all sockets (port %d .. %d) was successful.\n"), LOG_MODULE, port, port + 3);

  return 1;
}

static int vdr_plugin_open_socket_mrl(input_plugin_t *this_gen)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)this_gen;

  lprintf("input_vdr: connecting to vdr-xine-server...\n");

  if (!vdr_plugin_open_sockets(this))
    return 0;

  return 1;
}

static void vdr_vpts_offset_queue_add (vdr_input_plugin_t *this, int type, int64_t disc_off) {
  pthread_mutex_lock (&this->vpts_offs_queue.lock);
  if ((type == DISC_ABSOLUTE) || (type == DISC_STREAMSTART))
    vdr_vpts_offset_queue_add_int (this, disc_off);
  else
    vdr_vpts_offset_queue_purge(this);
  this->last_disc_type = type;
  if (type != DISC_STREAMSTART)
    pthread_cond_broadcast (&this->vpts_offs_queue.changed);
  pthread_mutex_unlock (&this->vpts_offs_queue.lock);

  if (!this->metronom.trick_mode)
  {
    xine_event_t event;

    event.type = XINE_EVENT_VDR_DISCONTINUITY;
    event.data = NULL;
    event.data_length = type;

    xine_event_send(this->stream, &event);
  }
}

static int vdr_plugin_open(input_plugin_t *this_gen)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)this_gen;

  lprintf("trying to open '%s'...\n", this->mrl);

  if (this->fh == -1)
  {
    int err = 0;

    if (!strncasecmp (&this->mrl[0], "vdr:/", 5)) {
      this->is_netvdr = 0;
      if (!vdr_plugin_open_fifo_mrl (this_gen))
        return 0;
    } else if (!strncasecmp (&this->mrl[0], "netvdr:/", 8)) {
      this->is_netvdr = 1;
      if (!vdr_plugin_open_socket_mrl (this_gen))
        return 0;
    } else {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG,
        _("%s: MRL (%s) invalid! MRL should start with vdr://path/to/fifo/stream or netvdr://host:port where ':port' is optional.\n"),
        LOG_MODULE, strerror (err));
      return 0;
    }

    this->rpc_thread_shutdown = 0;

    /* let this thread handle rpc commands in startup phase */
    this->startup_phase = 1;
    if (0 == vdr_rpc_thread_loop(this))
      return 0;
/* fprintf(stderr, "####################################################\n"); */
    if ((err = pthread_create(&this->rpc_thread, NULL,
                              vdr_rpc_thread_loop, (void *)this)) != 0)
    {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("%s: can't create new thread (%s)\n"), LOG_MODULE,
              strerror(err));

      return 0;
    }
    this->rpc_thread_created = 1;
  }

  /*
   * mrl accepted and opened successfully at this point
   *
   * => create plugin instance
   */

  this->curpos       = 0;

  return 1;
}

static void event_handler(void *user_data, const xine_event_t *event)
{
  vdr_input_plugin_t *this = (vdr_input_plugin_t *)user_data;
  uint32_t key = key_none;

  lprintf("eventHandler(): event->type: %d\n", event->type);

  if (XINE_EVENT_VDR_FRAMESIZECHANGED == event->type)
  {
    memcpy(&this->frame_size, event->data, event->data_length);

    if (0 != internal_write_event_frame_size(this))
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("%s: input event write: %s.\n"), LOG_MODULE, strerror(errno));

    adjust_zoom(this);
    return;
  }

  if (XINE_EVENT_VDR_DISCONTINUITY == event->type)
  {
    if (0 != internal_write_event_discontinuity(this, event->data_length))
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
              _("%s: input event write: %s.\n"), LOG_MODULE, strerror(errno));

    return;
  }

  if (XINE_EVENT_VDR_PLUGINSTARTED == event->type)
  {
    if (0 == event->data_length) /* vdr_video */
    {
      xine_event_t event;

      event.type = XINE_EVENT_VDR_TRICKSPEEDMODE;
      event.data = NULL;
      event.data_length = 0; /* this->trick_speed_mode; */

      xine_event_send(this->stream, &event);
    }
    else if (1 == event->data_length) /* vdr_audio */
    {
      xine_event_t event;
      vdr_select_audio_data_t event_data;

      event_data.channels = this->audio_channels;

      event.type = XINE_EVENT_VDR_SELECTAUDIO;
      event.data = &event_data;
      event.data_length = sizeof (event_data);

      xine_event_send(this->stream, &event);
    }
    else
    {
      fprintf(stderr, "input_vdr: illegal XINE_EVENT_VDR_PLUGINSTARTED: %d\n", event->data_length);
    }

    return;
  }

  if ((event->type >= 101) && (event->type < 130)) {
    static const uint8_t input_keys[130 - 101] = {
      [XINE_EVENT_INPUT_UP - 101]       = key_up,
      [XINE_EVENT_INPUT_DOWN - 101]     = key_down,
      [XINE_EVENT_INPUT_LEFT - 101]     = key_left,
      [XINE_EVENT_INPUT_RIGHT - 101]    = key_right,
      [XINE_EVENT_INPUT_SELECT - 101]   = key_ok,
      [XINE_EVENT_INPUT_MENU1 - 101]    = key_menu,
      [XINE_EVENT_INPUT_NUMBER_0 - 101] = key_0,
      [XINE_EVENT_INPUT_NUMBER_1 - 101] = key_1,
      [XINE_EVENT_INPUT_NUMBER_2 - 101] = key_2,
      [XINE_EVENT_INPUT_NUMBER_3 - 101] = key_3,
      [XINE_EVENT_INPUT_NUMBER_4 - 101] = key_4,
      [XINE_EVENT_INPUT_NUMBER_5 - 101] = key_5,
      [XINE_EVENT_INPUT_NUMBER_6 - 101] = key_6,
      [XINE_EVENT_INPUT_NUMBER_7 - 101] = key_7,
      [XINE_EVENT_INPUT_NUMBER_8 - 101] = key_8,
      [XINE_EVENT_INPUT_NUMBER_9 - 101] = key_9,
      [XINE_EVENT_INPUT_NEXT - 101]     = key_next,
      [XINE_EVENT_INPUT_PREVIOUS - 101] = key_previous
    };
    key = input_keys[event->type - 101];
    if (key == 0)
      return;
  } else if ((event->type >= 300) && (event->type < 337)) {
    static const uint8_t vdr_keys[337 - 300] = {
      [XINE_EVENT_VDR_BACK - 300]              = key_back,
      [XINE_EVENT_VDR_CHANNELPLUS - 300]       = key_channel_plus,
      [XINE_EVENT_VDR_CHANNELMINUS - 300]      = key_channel_minus,
      [XINE_EVENT_VDR_RED - 300]               = key_red,
      [XINE_EVENT_VDR_GREEN - 300]             = key_green,
      [XINE_EVENT_VDR_YELLOW - 300]            = key_yellow,
      [XINE_EVENT_VDR_BLUE - 300]              = key_blue,
      [XINE_EVENT_VDR_PLAY - 300]              = key_play,
      [XINE_EVENT_VDR_PAUSE - 300]             = key_pause,
      [XINE_EVENT_VDR_STOP - 300]              = key_stop,
      [XINE_EVENT_VDR_RECORD - 300]            = key_record,
      [XINE_EVENT_VDR_FASTFWD - 300]           = key_fast_fwd,
      [XINE_EVENT_VDR_FASTREW - 300]           = key_fast_rew,
      [XINE_EVENT_VDR_POWER - 300]             = key_power,
      [XINE_EVENT_VDR_SCHEDULE - 300]          = key_schedule,
      [XINE_EVENT_VDR_CHANNELS - 300]          = key_channels,
      [XINE_EVENT_VDR_TIMERS - 300]            = key_timers,
      [XINE_EVENT_VDR_RECORDINGS - 300]        = key_recordings,
      [XINE_EVENT_VDR_SETUP - 300]             = key_setup,
      [XINE_EVENT_VDR_COMMANDS - 300]          = key_commands,
      [XINE_EVENT_VDR_USER0 - 300]             = key_user0,
      [XINE_EVENT_VDR_USER1 - 300]             = key_user1,
      [XINE_EVENT_VDR_USER2 - 300]             = key_user2,
      [XINE_EVENT_VDR_USER3 - 300]             = key_user3,
      [XINE_EVENT_VDR_USER4 - 300]             = key_user4,
      [XINE_EVENT_VDR_USER5 - 300]             = key_user5,
      [XINE_EVENT_VDR_USER6 - 300]             = key_user6,
      [XINE_EVENT_VDR_USER7 - 300]             = key_user7,
      [XINE_EVENT_VDR_USER8 - 300]             = key_user8,
      [XINE_EVENT_VDR_USER9 - 300]             = key_user9,
      [XINE_EVENT_VDR_VOLPLUS - 300]           = key_volume_plus,
      [XINE_EVENT_VDR_VOLMINUS - 300]          = key_volume_minus,
      [XINE_EVENT_VDR_MUTE - 300]              = key_mute,
      [XINE_EVENT_VDR_AUDIO - 300]             = key_audio,
      [XINE_EVENT_VDR_INFO - 300]              = key_info,
      [XINE_EVENT_VDR_CHANNELPREVIOUS - 300]   = key_channel_previous,
      [XINE_EVENT_VDR_SUBTITLES - 300]         = key_subtitles
    };
    key = vdr_keys[event->type - 300];
    if (key == 0)
      return;
  } else {
    return;
  }

  if (0 != internal_write_event_key(this, key))
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            _("%s: input event write: %s.\n"), LOG_MODULE, strerror(errno));
}


static void vdr_metronom_handle_audio_discontinuity (metronom_t *self, int type, int64_t disc_off) {
  vdr_metronom_t *metr = (vdr_metronom_t *)self;
  int diff, num, trick_mode, trick_new_mode, add, relay_type = type;

  /* just 1 finite time lock session. */
  pthread_mutex_lock (&metr->mutex);
  /* just relay all we dont need (in particular, DISC_GAPLESS). */
  if ((type != DISC_STREAMSTART) &&
      (type != DISC_RELATIVE) &&
      (type != DISC_ABSOLUTE) &&
      (type != DISC_STREAMSEEK)) {
    pthread_mutex_unlock (&metr->mutex);
    metr->stream_metronom->handle_audio_discontinuity (metr->stream_metronom, type, disc_off);
    return;
  }
  /* make sure we dont respond too early. */
  if (!metr->audio.on) {
    if ((type != DISC_STREAMSEEK) || (disc_off != VDR_DISC_START)) {
      pthread_mutex_unlock (&metr->mutex);
      metr->stream_metronom->handle_audio_discontinuity (metr->stream_metronom, type, disc_off);
      return;
    }
    metr->audio.on = 1;
    pthread_mutex_unlock (&metr->mutex);
    xprintf (metr->input->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_vdr: audio discontinuity handling now on.\n");
    return;
  }
  /* Demux knows nothing about vdr seek. It just sees a pts jump, and sends a plain absolute
   * discontinuity. That will make metronom try a seamless append, leaving a 1..2s gap there.
   * Thus, after a seek, manually upgrade first "absolute" to "streamseek" here. */
  if (type == DISC_STREAMSTART) {
    metr->audio.seek = 1;
  } else if ((type == DISC_ABSOLUTE) && metr->audio.seek) {
    metr->audio.seek = 0;
    relay_type = DISC_STREAMSEEK;
  }

  trick_mode = metr->trick_mode;
  trick_new_mode = metr->trick_new_mode;
  metr->audio.disc_num += 1;
  num = metr->audio.disc_num;
  add = diff = metr->audio.disc_num - metr->video.disc_num;

  if (trick_mode && (type == DISC_ABSOLUTE) && (diff <= 0)) {
    if (trick_mode == 1)
      metr->trick_mode = 2;
    else
      add = 1;
  }

  if ((diff == 0) && (trick_new_mode >= 0)) {
    metr->trick_mode = trick_new_mode;
    metr->trick_new_mode = -1;
  } else {
    trick_new_mode = -1;
  }
  pthread_mutex_unlock (&metr->mutex);

  /* report */
  xprintf (metr->input->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_vdr: %s audio discontinuity #%d, type is %d, disc off %" PRId64 ".\n",
    trick_mode ? "trick play" : "", num, type, disc_off);
  /* relay */
  if (!trick_mode)
    metr->stream_metronom->handle_audio_discontinuity (metr->stream_metronom, relay_type, disc_off);
  /* if we are behind: complete this pair. */
  if (add <= 0)
    vdr_vpts_offset_queue_add (metr->input, type, disc_off);
  /* new trick mode */
  if (trick_new_mode >= 0)
    trick_speed_send_event (metr->input, trick_new_mode);
}

static void vdr_metronom_handle_video_discontinuity (metronom_t *self, int type, int64_t disc_off) {
  vdr_metronom_t *metr = (vdr_metronom_t *)self;
  int diff, num, trick_mode, trick_new_mode, add, relay_type = type;

  /* just 1 finite time lock session. */
  pthread_mutex_lock (&metr->mutex);
  /* just relay all we dont need (in particular, DISC_GAPLESS). */
  if ((type != DISC_STREAMSTART) &&
      (type != DISC_RELATIVE) &&
      (type != DISC_ABSOLUTE) &&
      (type != DISC_STREAMSEEK)) {
    pthread_mutex_unlock (&metr->mutex);
    metr->stream_metronom->handle_video_discontinuity (metr->stream_metronom, type, disc_off);
    return;
  }
  /* make sure we dont respond too early. */
  if (!metr->video.on) {
    if ((type != DISC_STREAMSEEK) || (disc_off != VDR_DISC_START)) {
      pthread_mutex_unlock (&metr->mutex);
      metr->stream_metronom->handle_video_discontinuity (metr->stream_metronom, type, disc_off);
      return;
    }
    metr->video.on = 1;
    pthread_mutex_unlock (&metr->mutex);
    xprintf (metr->input->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_vdr: video discontinuity handling now on.\n");
    return;
  }
  /* Demux knows nothing about vdr seek. It just sees a pts jump, and sends a plain absolute
   * discontinuity. That will make metronom try a seamless append, leaving a 1..2s gap there.
   * Thus, after a seek, manually upgrade first "absolute" to "streamseek" here. */
  if (type == DISC_STREAMSTART) {
    metr->video.seek = 1;
  } else if ((type == DISC_ABSOLUTE) && metr->video.seek) {
    metr->video.seek = 0;
    relay_type = DISC_STREAMSEEK;
  }

  trick_mode = metr->trick_mode;
  trick_new_mode = metr->trick_new_mode;
  metr->video.disc_num += 1;
  num = metr->video.disc_num;
  add = diff = metr->video.disc_num - metr->audio.disc_num;

  if (trick_mode && (type == DISC_ABSOLUTE) && (diff <= 0)) {
    if (trick_mode == 1)
      metr->trick_mode = 2;
    else
      add = 1;
  }

  if ((diff == 0) && (trick_new_mode >= 0)) {
    metr->trick_mode = trick_new_mode;
    metr->trick_new_mode = -1;
  } else {
    trick_new_mode = -1;
  }
  pthread_mutex_unlock (&metr->mutex);

  /* report */
  xprintf (metr->input->stream->xine, XINE_VERBOSITY_DEBUG,
    "input_vdr: %s video discontinuity #%d, type is %d, disc off %" PRId64 ".\n",
    trick_mode ? "trick play" : "", num, type, disc_off);
  /* relay */
  if (!trick_mode)
    metr->stream_metronom->handle_video_discontinuity (metr->stream_metronom, relay_type, disc_off);
  /* if we are behind: complete this pair. */
  if (add <= 0)
    vdr_vpts_offset_queue_add (metr->input, type, disc_off);
  /* new trick mode */
  if (trick_new_mode >= 0)
    trick_speed_send_event (metr->input, trick_new_mode);
}

static void vdr_metronom_got_video_frame(metronom_t *self, vo_frame_t *frame)
{
  vdr_metronom_t *metr = (vdr_metronom_t *)self;

  if (frame->pts) {

    pthread_mutex_lock (&metr->mutex);
    if (!metr->trick_mode) {

      pthread_mutex_unlock (&metr->mutex);
      metr->stream_metronom->got_video_frame (metr->stream_metronom, frame);

    } else {

      frame->progressive_frame = -1; /* force progressive */

      metr->stream_metronom->set_option (metr->stream_metronom, METRONOM_VDR_TRICK_PTS, frame->pts);

      metr->stream_metronom->got_video_frame (metr->stream_metronom, frame);
      vdr_vpts_offset_queue_add (metr->input, DISC_ABSOLUTE, frame->pts);

      pthread_mutex_unlock (&metr->mutex);

      /* fprintf (stderr, "vpts: %12ld, pts: %12ld, offset: %12ld\n",
        frame->vpts, frame->pts, metr->stream_metronom->get_option (metr->stream_metronom, METRONOM_VPTS_OFFSET)); */
    }

  } else {

    metr->stream_metronom->got_video_frame (metr->stream_metronom, frame);

  }
}

static int64_t vdr_metronom_got_audio_samples(metronom_t *self, int64_t pts, int nsamples)
{
  vdr_metronom_t *this = (vdr_metronom_t *)self;
  return this->stream_metronom->got_audio_samples(this->stream_metronom, pts, nsamples);
}

static int64_t vdr_metronom_got_spu_packet(metronom_t *self, int64_t pts)
{
  vdr_metronom_t *this = (vdr_metronom_t *)self;
  return this->stream_metronom->got_spu_packet(this->stream_metronom, pts);
}

static void vdr_metronom_set_audio_rate(metronom_t *self, int64_t pts_per_smpls)
{
  vdr_metronom_t *this = (vdr_metronom_t *)self;
  this->stream_metronom->set_audio_rate(this->stream_metronom, pts_per_smpls);
}

static void vdr_metronom_set_option(metronom_t *self, int option, int64_t value)
{
  vdr_metronom_t *this = (vdr_metronom_t *)self;
  this->stream_metronom->set_option(this->stream_metronom, option, value);
}

static int64_t vdr_metronom_get_option(metronom_t *self, int option)
{
  vdr_metronom_t *this = (vdr_metronom_t *)self;
  return this->stream_metronom->get_option(this->stream_metronom, option);
}

static void vdr_metronom_set_master(metronom_t *self, metronom_t *master)
{
  vdr_metronom_t *this = (vdr_metronom_t *)self;
  this->stream_metronom->set_master(this->stream_metronom, master);
}

static void vdr_metronom_exit(metronom_t *self)
{
  (void)self;
  int this_shall_never_be_called = 1;
  _x_assert (this_shall_never_be_called == 0);
}


static input_plugin_t *vdr_class_get_instance(input_class_t *cls_gen, xine_stream_t *stream,
                                               const char *data)
{
  vdr_input_plugin_t *this;
  char               *mrl = strdup(data);

  if (!strncasecmp(mrl, "vdr:/", 5))
    lprintf("filename '%s'\n", mrl_to_fifo (mrl));
  else if (!strncasecmp(mrl, "netvdr:/", 5))
    lprintf("host '%s'\n", mrl_to_host (mrl));
  else
  {
    free(mrl);
    return NULL;
  }

  /*
   * mrl accepted and opened successfully at this point
   *
   * => create plugin instance
   */

  this = calloc(1, sizeof (vdr_input_plugin_t));
  if (!this) {
    free(mrl);
    return NULL;
  }

#ifndef HAVE_ZERO_SAFE_MEM
  this->curpos                   = 0;
  this->cur_size                 = 0;
  this->cur_done                 = 0;
  this->osd_buffer               = NULL;
  this->osd_buffer_size          = 0;
  this->osd_unscaled_blending    = 0;
  this->audio_channels           = 0;
  this->frame_size.x             = 0;
  this->frame_size.y             = 0;
  this->frame_size.w             = 0;
  this->frame_size.h             = 0;
  this->frame_size.r             = 0;
  this->stream_external          = NULL;
  this->event_queue_external     = NULL;
  this->image4_3_zoom_x          = 0;
  this->image4_3_zoom_y          = 0;
  this->image16_9_zoom_x         = 0;
  this->image16_9_zoom_y         = 0;
  this->metronom.audio.on        = 0;
  this->metronom.audio.seek      = 0;
  this->metronom.audio.disc_num  = 0;
  this->metronom.video.on        = 0;
  this->metronom.video.seek      = 0;
  this->metronom.video.disc_num  = 0;
  this->metronom.trick_mode      = 0;
#endif

  this->stream     = stream;

  this->mrl        = mrl;
  this->fh         = -1;
  this->fh_control = -1;
  this->fh_result  = -1;
  this->fh_event   = -1;

  this->metronom.trick_new_mode  = -1;

  this->input_plugin.open              = vdr_plugin_open;
  this->input_plugin.get_capabilities  = vdr_plugin_get_capabilities;
  this->input_plugin.read              = vdr_plugin_read;
  this->input_plugin.read_block        = vdr_plugin_read_block;
  this->input_plugin.seek              = vdr_plugin_seek;
  this->input_plugin.get_current_pos   = vdr_plugin_get_current_pos;
  this->input_plugin.get_length        = vdr_plugin_get_length;
  this->input_plugin.get_blocksize     = vdr_plugin_get_blocksize;
  this->input_plugin.get_mrl           = vdr_plugin_get_mrl;
  this->input_plugin.dispose           = vdr_plugin_dispose;
  this->input_plugin.get_optional_data = vdr_plugin_get_optional_data;
  this->input_plugin.input_class       = cls_gen;

  this->cur_func = func_unknown;

  memset(this->osd, 0, sizeof (this->osd));

  {
    xine_osd_t *osd = xine_osd_new(this->stream, 0, 0, 16, 16);
    uint32_t caps = xine_osd_get_capabilities(osd);
    xine_osd_free(osd);

    this->osd_supports_argb_layer    = !!(caps & XINE_OSD_CAP_ARGB_LAYER);
    this->osd_supports_custom_extent = !!(caps & XINE_OSD_CAP_CUSTOM_EXTENT);
  }

  this->mute_mode               = XINE_VDR_MUTE_SIMULATE;
  this->volume_mode             = XINE_VDR_VOLUME_CHANGE_HW;
  this->last_volume             = -1;

  pthread_mutex_init (&this->rpc_thread_shutdown_lock, NULL);
  pthread_cond_init (&this->rpc_thread_shutdown_cond, NULL);

  pthread_mutex_init (&this->find_sync_point_lock, NULL);
  pthread_mutex_init (&this->adjust_zoom_lock, NULL);

  vdr_vpts_offset_queue_init (this);

  this->event_queue = xine_event_new_queue(this->stream);
  if (this->event_queue)
    xine_event_create_listener_thread(this->event_queue, event_handler, this);

  /* see comment above */
  if (this->stream->audio_fifo)
    this->stream->audio_fifo->register_alloc_cb (this->stream->audio_fifo, input_vdr_dummy, this);
  if (this->stream->video_fifo)
    this->stream->video_fifo->register_alloc_cb (this->stream->video_fifo, input_vdr_dummy, this);

  /* init metronom */
  this->metronom.input = this;
  this->metronom.metronom.set_audio_rate             = vdr_metronom_set_audio_rate;
  this->metronom.metronom.got_video_frame            = vdr_metronom_got_video_frame;
  this->metronom.metronom.got_audio_samples          = vdr_metronom_got_audio_samples;
  this->metronom.metronom.got_spu_packet             = vdr_metronom_got_spu_packet;
  this->metronom.metronom.handle_audio_discontinuity = vdr_metronom_handle_audio_discontinuity;
  this->metronom.metronom.handle_video_discontinuity = vdr_metronom_handle_video_discontinuity;
  this->metronom.metronom.set_option                 = vdr_metronom_set_option;
  this->metronom.metronom.get_option                 = vdr_metronom_get_option;
  this->metronom.metronom.set_master                 = vdr_metronom_set_master;
  this->metronom.metronom.exit                       = vdr_metronom_exit;
  pthread_mutex_init (&this->metronom.mutex, NULL);
  /* set metronom */
  stream->metronom = &this->metronom.metronom;
  /* send start discontinuities */
  _x_demux_control_newpts (stream, VDR_DISC_START, BUF_FLAG_SEEK);

  return &this->input_plugin;
}

/*
 * vdr input plugin class stuff
 */
static const char * const *vdr_class_get_autoplay_list(input_class_t *this_gen,
                                          int *num_files)
{
  static const char * const mrls[] = {"vdr:/" VDR_ABS_FIFO_DIR "/stream#demux:mpeg_pes", NULL};

  (void)this_gen;
  *num_files = 1;
  return mrls;
}

void *vdr_input_init_plugin(xine_t *xine, const void *data)
{
  lprintf("init_class\n");
  static const input_class_t this = {
    .get_instance      = vdr_class_get_instance,
    .identifier        = "VDR",
    .description       = N_("VDR display device plugin"),
    .get_dir           = NULL,
    .get_autoplay_list = vdr_class_get_autoplay_list,
    .dispose           = NULL,
    .eject_media       = NULL
  };
  (void)xine;
  (void)data;
  return (input_class_t *)&this;
}
