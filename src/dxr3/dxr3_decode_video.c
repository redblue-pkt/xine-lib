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
 * $Id: dxr3_decode_video.c,v 1.28 2003/01/13 18:09:58 mroi Exp $
 */
 
/* dxr3 video decoder plugin.
 * Accepts the video data from xine and sends it directly to the
 * corresponding dxr3 device. Takes precedence over the libmpeg2
 * due to a higher priority.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#if defined(__sun)
#include <sys/ioccom.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "xine_internal.h"
#include "buffer.h"
#include "dxr3_scr.h"
#include "video_out_dxr3.h"
#include "dxr3.h"

#define LOG_VID 1
#define LOG_PTS 0

/* the number of frames to pass after an out-of-sync situation
   before locking the stream again */
#define RESYNC_WINDOW_SIZE 50

/* we adjust vpts_offset in metronom, when skip_count reaches this value */
#define SKIP_TOLERANCE 200

/* the number of frames to pass before we stop duration correction */
#define FORCE_DURATION_WINDOW_SIZE 100

/* offset for mpeg header parsing */
#define HEADER_OFFSET 0


/* plugin class initialization function */
static void     *dxr3_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
static uint32_t supported_types[] = { BUF_VIDEO_MPEG, 0 };

static decoder_info_t dxr3_video_decoder_info = {
  supported_types,     /* supported types */
  10                   /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 14, "dxr3-mpeg2", XINE_VERSION_CODE, &dxr3_video_decoder_info, &dxr3_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


/* plugin class functions */
static video_decoder_t *dxr3_open_plugin(video_decoder_class_t *class_gen, xine_stream_t *stream);
static char            *dxr3_get_identifier(video_decoder_class_t *class_gen);
static char            *dxr3_get_description(video_decoder_class_t *class_gen);
static void             dxr3_class_dispose(video_decoder_class_t *class_gen);

/* plugin instance functions */
static void dxr3_decode_data(video_decoder_t *this_gen, buf_element_t *buf);
static void dxr3_reset(video_decoder_t *this_gen);
static void dxr3_discontinuity(video_decoder_t *this_gen);
static void dxr3_flush(video_decoder_t *this_gen);
static void dxr3_dispose(video_decoder_t *this_gen);

/* plugin structures */
typedef struct dxr3_decoder_class_s {
  video_decoder_class_t  video_decoder_class;
  
  int                    instance;             /* we allow only one instance of this plugin */
  
  metronom_clock_t      *clock;                /* used for syncing */
} dxr3_decoder_class_t;

typedef struct dxr3_decoder_s {
  video_decoder_t        video_decoder;
  dxr3_decoder_class_t  *class;
  xine_stream_t         *stream;
  dxr3_scr_t            *scr;
  dxr3_driver_t         *dxr3_vo;              /* we need to talk to the dxr3 video out */
  
  char                   devname[128];
  char                   devnum[3];
  int                    fd_control;
  int                    fd_video;             /* to access the dxr3 devices */
  
  int                    have_header_info;
  int                    sequence_open;
  int                    width;
  int                    height;
  int                    aspect;
  int                    frame_rate_code;
  int                    repeat_first_field;   /* mpeg stream header data */
  
  int                    force_aspect;         /* when input plugin has better info, we are forced
                                                * to use a certain aspect */
  int                    last_width;
  int                    last_height;
  int                    last_aspect;          /* used to detect changes for event sending */
  
  int                    sync_every_frame;
  int                    sync_retry;
  int                    enhanced_mode;
  int                    resync_window;
  int                    skip_count;           /* syncing parameters */
  
  int                    correct_durations;
  int64_t                last_vpts;
  int                    force_duration_window;
  int                    avg_duration;         /* logic to correct broken frame rates */
} dxr3_decoder_t;

/* helper functions */
static int       dxr3_present(xine_stream_t *stream);
static int       dxr3_mvcommand(int fd_control, int command);
static void      parse_mpeg_header(dxr3_decoder_t *this, uint8_t *buffer);
static int       get_duration(dxr3_decoder_t *this);

/* config callbacks */
static void      dxr3_update_sync_mode(void *this_gen, xine_cfg_entry_t *entry);
static void      dxr3_update_enhanced_mode(void *this_gen, xine_cfg_entry_t *entry);
static void      dxr3_update_correct_durations(void *this_gen, xine_cfg_entry_t *entry);


static void *dxr3_init_plugin(xine_t *xine, void *data)
{
  dxr3_decoder_class_t *this;
  
  this = (dxr3_decoder_class_t *)malloc(sizeof (dxr3_decoder_class_t));
  if (!this) return NULL;
  
  this->video_decoder_class.open_plugin     = dxr3_open_plugin;
  this->video_decoder_class.get_identifier  = dxr3_get_identifier;
  this->video_decoder_class.get_description = dxr3_get_description;
  this->video_decoder_class.dispose         = dxr3_class_dispose;
  
  this->instance                            = 0;
  
  this->clock                               = xine->clock;
  
  return &this->video_decoder_class;
}


static video_decoder_t *dxr3_open_plugin(video_decoder_class_t *class_gen, xine_stream_t *stream)
{
  dxr3_decoder_t *this;
  dxr3_decoder_class_t *class = (dxr3_decoder_class_t *)class_gen;
  config_values_t *cfg;
  const char *confstr;
  int dashpos;
  char tmpstr[128];
  
  if (class->instance) return NULL;
  if (!dxr3_present(stream)) return NULL;
  
  this = (dxr3_decoder_t *)malloc(sizeof (dxr3_decoder_t));
  if (!this) return NULL;
  
  cfg = stream->xine->config;
  
  this->video_decoder.decode_data   = dxr3_decode_data;
  this->video_decoder.reset         = dxr3_reset;
  this->video_decoder.discontinuity = dxr3_discontinuity;
  this->video_decoder.flush         = dxr3_flush;
  this->video_decoder.dispose       = dxr3_dispose;
  
  this->class                       = class;
  this->stream                      = stream;
  this->scr                         = NULL;
  this->dxr3_vo                     = (dxr3_driver_t *)stream->video_driver;
  
  confstr = cfg->register_string(cfg, CONF_LOOKUP, CONF_DEFAULT, CONF_NAME, CONF_HELP, 0, NULL, NULL);
  strncpy(this->devname, confstr, 128);
  this->devname[127] = '\0';
  dashpos = strlen(this->devname) - 2; /* the dash in the new device naming scheme would be here */
  if (this->devname[dashpos] == '-') {
    /* use new device naming scheme with trailing number */
    strncpy(this->devnum, &this->devname[dashpos], 3);
    this->devname[dashpos] = '\0';
  } else {
    /* use old device naming scheme without trailing number */
    /* FIXME: remove this when everyone uses em8300 >=0.12.0 */
    this->devnum[0] = '\0';
  }
  
  snprintf(tmpstr, sizeof(tmpstr), "%s%s", this->devname, this->devnum);
#if LOG_VID
  printf("dxr3_decode_video: Entering video init, devname=%s.\n",tmpstr);
#endif
  
  /* open later, because dxr3_video_out might have it open until we request a frame */
  this->fd_video = -1;
  
  if ((this->fd_control = open(tmpstr, O_WRONLY)) < 0) {
    printf("dxr3_decode_video: Failed to open control device %s (%s)\n",
      tmpstr, strerror(errno));
    free(this);
    return NULL;
  }
  
  this->have_header_info      = 0;
  this->sequence_open         = 0;
  this->repeat_first_field    = 0;
  
  this->force_aspect          = 0;
  
  this->last_width            = 0;
  this->last_height           = 0;
  this->last_aspect           = 0;
  
  this->sync_retry            = 0;
  this->resync_window         = 0;
  this->skip_count            = 0;
  
  this->force_duration_window = -FORCE_DURATION_WINDOW_SIZE;
  this->last_vpts             = this->class->clock->get_current_time(this->class->clock);
  this->avg_duration          = 0;
  
  this->sync_every_frame      = cfg->register_bool(cfg,
    "dxr3.sync_every_frame", 0, _("Try to sync video every frame"),
    _("This is relevant for progressive video only (most PAL films)."), 20,
    dxr3_update_sync_mode, this);
  this->enhanced_mode         = cfg->register_bool(cfg,
    "dxr3.alt_play_mode", 1, _("Use alternate Play mode"),
    _("Enabling this option will utilise a smoother play mode."), 10,
    dxr3_update_enhanced_mode, this);
  this->correct_durations     = cfg->register_bool(cfg,
    "dxr3.correct_durations", 0, _("Correct frame durations in broken streams"),
    _("Enable this for streams with wrong frame durations."), 10,
    dxr3_update_correct_durations, this);
  
  if (!this->dxr3_vo->overlay_enabled)
    /* set a/v offset to compensate dxr3 internal delay */
    this->stream->metronom->set_option(this->stream->metronom, METRONOM_AV_OFFSET, -21600);
  
  stream->video_out->open(stream->video_out, stream);
  
  class->instance = 1;
  
  return &this->video_decoder;
}

static char *dxr3_get_identifier(video_decoder_class_t *class_gen)
{
  return "dxr3-mpeg2";
}

static char *dxr3_get_description(video_decoder_class_t *class_gen)
{
  return "MPEGI/II decoder plugin using the hardware decoding capabilities of a DXR3 decoder card.";
}

static void dxr3_class_dispose(video_decoder_class_t *class_gen)
{
  free(class_gen);
}
 

static void dxr3_decode_data(video_decoder_t *this_gen, buf_element_t *buf)
{
  dxr3_decoder_t *this = (dxr3_decoder_t *)this_gen;
  ssize_t written;
  int64_t vpts;
  int i, skip;
  vo_frame_t *img;
  uint8_t *buffer, byte;
  uint32_t shift;
    
  vpts = 0;
  
  /* handle aspect hints from xine-dvdnav */
  if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
    if (buf->decoder_info[1] == BUF_SPECIAL_ASPECT) {
      this->aspect = this->force_aspect = buf->decoder_info[2];
      if (buf->decoder_info[3] == 0x1 && this->force_aspect == XINE_VO_ASPECT_ANAMORPHIC)
        /* letterboxing is denied, we have to do pan&scan */
        this->aspect = this->force_aspect = XINE_VO_ASPECT_PAN_SCAN;
      /* when aspect changed, we have to send an event for dxr3 spu decoder */
      if (!this->last_aspect || this->last_aspect != this->aspect) {
        xine_event_t event;
	xine_format_change_data_t data;
        event.type        = XINE_EVENT_FRAME_FORMAT_CHANGE;
	event.stream      = this->stream;
	event.data        = &data;
	event.data_length = sizeof(data);
        data.width        = this->last_width;
        data.height       = this->last_height;
        data.aspect       = this->aspect;
        xine_event_send(this->stream, &event);
        this->last_aspect = this->aspect;
      }
    }
    return;
  }
  
  /* parse frames in the buffer handed in, evaluate headers,
   * send frames to video_out and handle some syncing
   */
  buffer = buf->content;
  shift = 0xffffff00;
  for (i = 0; i < buf->size; i++) {
    byte = *buffer++;
    if (shift != 0x00000100) {
      shift = (shift | byte) << 8;
      continue;
    }
    /* header code of some kind found */
    shift = 0xffffff00;
    if (byte == 0xb3) {
      /* sequence data */
      parse_mpeg_header(this, buffer);
      this->sequence_open = 1;
      continue;
    }
    if (byte == 0xb5) {
      /* extension data */
      if ((buffer[0] & 0xf0) == 0x80)
        this->repeat_first_field = (buffer[3] >> 1) & 1;
      /* check if we can keep syncing */
      if (this->repeat_first_field && this->sync_retry)  /* reset counter */
        this->sync_retry = 500;
      if (this->repeat_first_field && this->sync_every_frame) {
#if LOG_VID
        printf("dxr3: non-progressive video detected. "
          "disabling sync_every_frame.\n");
#endif
        this->sync_every_frame = 0;
        this->sync_retry = 500; /* see you later */
      }
      continue;
    }
    if (byte == 0xb7)
      /* sequence end */
      this->sequence_open = 0;
    if (byte != 0x00)  /* Don't care what it is. It's not a new frame */
      continue;
    /* we have a code for a new frame */
    if (!this->have_header_info)  /* this->width et al may still be undefined */
      continue;
    if (buf->decoder_flags & BUF_FLAG_PREVIEW)
      continue;
    
    /* pretend like we have decoded a frame */
    img = this->stream->video_out->get_frame(this->stream->video_out,
      this->width, this->height, this->aspect,
      XINE_IMGFMT_DXR3, VO_BOTH_FIELDS);
    img->pts       = buf->pts;
    img->bad_frame = 0;
    img->duration  = get_duration(this);
    
    skip = img->draw(img, this->stream);
    
    if (skip <= 0) { /* don't skip */
      vpts = img->vpts; /* copy so we can free img */
      
      if (this->correct_durations) {
        /* calculate an average frame duration from metronom's vpts values */
        this->avg_duration = this->avg_duration * 0.9 + (vpts - this->last_vpts) * 0.1;
#if LOG_PTS
        printf("dxr3_decode_video: average frame duration %d\n", this->avg_duration);
#endif
      }
      
      if (this->skip_count) this->skip_count--;
      
      if (this->resync_window == 0 && this->scr && this->enhanced_mode &&
        !this->scr->scanning) {
        /* we are in sync, so we can lock the stream now */
#if LOG_VID
        printf("dxr3_decode_video: in sync, stream locked\n");
#endif
        dxr3_mvcommand(this->fd_control, MVCOMMAND_SYNC);
        this->resync_window = -RESYNC_WINDOW_SIZE;
      }
      if (this->resync_window != 0 && this->resync_window > -RESYNC_WINDOW_SIZE)
        this->resync_window--;
    } else { /* metronom says skip, so don't set vpts */
#if LOG_VID
      printf("dxr3_decode_video: %d frames to skip\n", skip);
#endif
      vpts = 0;
      this->avg_duration = 0;
      
      /* handle frame skip conditions */
      if (this->scr && !this->scr->scanning) this->skip_count += skip;
      if (this->skip_count > SKIP_TOLERANCE) {
        /* we have had enough skipping messages now, let's react */
        int64_t vpts_adjust = skip * (int64_t)img->duration / 2;
        if (vpts_adjust > 90000) vpts_adjust = 90000;
        this->stream->metronom->set_option(this->stream->metronom,
          METRONOM_ADJ_VPTS_OFFSET, vpts_adjust);
        this->skip_count = 0;
        this->resync_window = 0;
      }
      
      if (this->scr && this->scr->scanning) this->resync_window = 0;
      if (this->resync_window == 0 && this->scr && this->enhanced_mode &&
        !this->scr->scanning) {
        /* switch off sync mode in the card to allow resyncing */
#if LOG_VID
        printf("dxr3_decode_video: out of sync, allowing stream resync\n");
#endif
        dxr3_mvcommand(this->fd_control, MVCOMMAND_START);
        this->resync_window = RESYNC_WINDOW_SIZE;
      }
      if (this->resync_window != 0 && this->resync_window < RESYNC_WINDOW_SIZE)
        this->resync_window++;
    }
    this->last_vpts = img->vpts;
    img->free(img);

    /* if sync_every_frame was disabled, decrease the counter
     * for a retry 
     * (it might be due to crappy studio logos and stuff
     * so we should give the main movie a chance)
     */
    if (this->sync_retry) {
      if (!--this->sync_retry) {
#if LOG_VID
        printf("dxr3_decode_video: retrying sync_every_frame");
#endif
        this->sync_every_frame = 1;
      }
    }
  }
  if (buf->decoder_flags & BUF_FLAG_PREVIEW) return;
  
  /* ensure video device is open 
   * (we open it late because on occasion the dxr3 video out driver
   * wants to open it)
   */
  if (this->fd_video < 0) {
    char tmpstr[128];
    snprintf (tmpstr, sizeof(tmpstr), "%s_mv%s", this->devname, this->devnum);
    if ((this->fd_video = open(tmpstr, O_WRONLY | O_NONBLOCK)) < 0) {
      printf("dxr3_decode_video: Failed to open video device %s (%s)\n",
        tmpstr, strerror(errno)); 
      return;
    }
  }
  
  /* We may want to issue a SETPTS, so make sure the scr plugin
   * is running and registered. Unfortuantely wa cannot do this
   * earlier, because the dxr3's internal scr gets confused
   * when started with a closed video device. Maybe this is a
   * driver bug and gets fixed somewhen. FIXME: We might then
   * want to move this code to dxr3_init.
   */
  if (!this->scr) {
    int64_t time;
    
    time = this->class->clock->get_current_time(this->class->clock);
    
    this->scr = dxr3_scr_init(this->stream);
    this->scr->scr_plugin.start(&this->scr->scr_plugin, time);
    this->class->clock->register_scr(
      this->class->clock, &this->scr->scr_plugin);
    if (this->class->clock->scr_master == &this->scr->scr_plugin)
#if LOG_VID
      printf("dxr3_decode_video: dxr3_scr plugin is master\n");
#endif
    else
#if LOG_VID
      printf("dxr3_decode_video: dxr3scr plugin is NOT master\n");
#endif
  }
  
  /* update the pts timestamp in the card, which tags the data we write to it */
  if (vpts) {
    int64_t delay;
    
    delay = vpts - this->class->clock->get_current_time(
      this->class->clock);
#if LOG_PTS
    printf("dxr3_decode_video: SETPTS got %lld\n", vpts);
#endif
    /* SETPTS only if less then one second in the future and
     * either buffer has pts or sync_every_frame is set */
    if ((delay > 0) && (delay < 90000) &&
      (this->sync_every_frame || buf->pts)) {
      uint32_t vpts32 = vpts;
      /* update the dxr3's current pts value */
      if (ioctl(this->fd_video, EM8300_IOCTL_VIDEO_SETPTS, &vpts32))
        printf("dxr3_decode_video: set video pts failed (%s)\n",
          strerror(errno));
    }
    if (delay >= 90000)   /* frame more than 1 sec ahead */
      printf("dxr3_decode_video: WARNING: vpts %lld is %.02f seconds ahead of time!\n",
        vpts, delay/90000.0); 
    if (delay < 0)
      printf("dxr3_decode_video: WARNING: overdue frame.\n");
  }
#if LOG_PTS
  else if (buf->pts) {
    printf("dxr3_decode_video: skip buf->pts = %lld (no vpts)\n", buf->pts);
  }
#endif
  
  /* now write the content to the dxr3 mpeg device and, in a dramatic
   * break with open source tradition, check the return value
   */
  written = write(this->fd_video, buf->content, buf->size);
  if (written < 0) {
    if (errno == EAGAIN) {
      printf("dxr3_decode_video: write to device would block. flushing\n");
      dxr3_flush(this_gen);
    } else {
      printf("dxr3_decode_video: video device write failed (%s)\n",
        strerror(errno));
    }
    return;
  }
  if (written != buf->size)
    printf("dxr3_decode_video: Could only write %d of %d video bytes.\n",
      written, buf->size);
}

static void dxr3_reset(video_decoder_t *this_gen)
{
  dxr3_decoder_t *this = (dxr3_decoder_t *)this_gen;
  
  this->sequence_open = 0;
}

static void dxr3_discontinuity(video_decoder_t *this_gen)
{
}

static void dxr3_flush(video_decoder_t *this_gen) 
{
  dxr3_decoder_t *this = (dxr3_decoder_t *)this_gen;
  
  if (this->sequence_open &&
      this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HAS_STILL]) {
    /* The dxr3 needs a sequence end code for still menus to work correctly
     * (the highlights won't move without), but some dvds have stills
     * with no sequence end code. Since it is very likely that flush() is called
     * in still situations, we send one here. */
    static uint8_t end_buffer[4] = { 0x00, 0x00, 0x01, 0xb7 };
    write(this->fd_video, &end_buffer, 4);
    this->sequence_open = 0;
    printf("dxr3_decode_video: WARNING: added missing end sequence\n");
  }
}

static void dxr3_dispose(video_decoder_t *this_gen)
{
  dxr3_decoder_t *this = (dxr3_decoder_t *)this_gen;
  metronom_clock_t *clock = this->class->clock;
  
  if (this->scr) {
    clock->unregister_scr(clock, &this->scr->scr_plugin);
    this->scr->scr_plugin.exit(&this->scr->scr_plugin);
  }
  
  dxr3_mvcommand(this->fd_control, MVCOMMAND_FLUSHBUF);
  
  if (this->fd_video >= 0) close(this->fd_video);
  close(this->fd_control);
  
  this->stream->video_out->close(this->stream->video_out, this->stream);
  this->class->instance  = 0;
  
  free(this);
}


static int dxr3_present(xine_stream_t *stream)
{
  plugin_node_t *node;
  video_driver_class_t *vo_class;
  int present = 0;
  
  if (stream->video_driver && stream->video_driver->node) {
    node = (plugin_node_t *)stream->video_driver->node;
    if (node->plugin_class) {
      vo_class = (video_driver_class_t *)node->plugin_class;
      if (vo_class->get_identifier)
        present = (strcmp(vo_class->get_identifier(vo_class), DXR3_VO_ID) == 0);
    }
  }
#ifdef LOG_VID
  printf("dxr3_decode_video: dxr3 %s\n", present ? "present" : "not present");
#endif
  return present;
}

static int dxr3_mvcommand(int fd_control, int command)
{
  em8300_register_t regs;
  
  regs.microcode_register = 1;
  regs.reg = 0;
  regs.val = command;
  
  return ioctl(fd_control, EM8300_IOCTL_WRITEREG, &regs);
}

static void parse_mpeg_header(dxr3_decoder_t *this, uint8_t * buffer)
{
  this->frame_rate_code = buffer[HEADER_OFFSET+3] & 15;
  this->height          = (buffer[HEADER_OFFSET+0] << 16) |
                          (buffer[HEADER_OFFSET+1] <<  8) |
                          buffer[HEADER_OFFSET+2];
  this->width           = ((this->height >> 12) + 15) & ~15;
  this->height          = ((this->height & 0xfff) + 15) & ~15;
  this->aspect          = buffer[HEADER_OFFSET+3] >> 4;
  
  this->have_header_info = 1;
  
  if (this->force_aspect) this->aspect = this->force_aspect;
  
  /* when width, height or aspect changes,
   * we have to send an event for dxr3 spu decoder */
  if (!this->last_width || !this->last_height || !this->last_aspect ||
      (this->last_width != this->width) ||
      (this->last_height != this->height) ||
      (this->last_aspect != this->aspect)) {
    xine_event_t event;
    xine_format_change_data_t data;
    event.type        = XINE_EVENT_FRAME_FORMAT_CHANGE;
    event.stream      = this->stream;
    event.data        = &data;
    event.data_length = sizeof(data);
    data.width        = this->width;
    data.height       = this->height;
    data.aspect       = this->aspect;
    xine_event_send(this->stream, &event);
    this->last_width = this->width;
    this->last_height = this->height;
    this->last_aspect = this->aspect;
    
    /* update stream metadata */
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]  = this->width;
    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->height;
    switch (this->aspect) {
    case XINE_VO_ASPECT_SQUARE:
      this->stream->stream_info[XINE_STREAM_INFO_VIDEO_RATIO] = 10000;
      break;
    case XINE_VO_ASPECT_4_3:
      this->stream->stream_info[XINE_STREAM_INFO_VIDEO_RATIO] = 10000 * 4.0 / 3.0;
      break;
    case XINE_VO_ASPECT_PAN_SCAN:
    case XINE_VO_ASPECT_ANAMORPHIC:
      this->stream->stream_info[XINE_STREAM_INFO_VIDEO_RATIO] = 10000 * 16.0 / 9.0;
      break;
    case XINE_VO_ASPECT_DVB:
      this->stream->stream_info[XINE_STREAM_INFO_VIDEO_RATIO] = 10000 * 2.11;
      break;
    }
  }
}

static int get_duration(dxr3_decoder_t *this)
{
  int duration;
  
  switch (this->frame_rate_code) {
  case 1: /* 23.976 */
    duration = 3913;
    break;
  case 2: /* 24.000 */
    duration = 3750;
    break;
  case 3: /* 25.000 */
    duration = this->repeat_first_field ? 5400 : 3600;
    break;
  case 4: /* 29.970 */
    duration = this->repeat_first_field ? 4505 : 3003;
    break;
  case 5: /* 30.000 */
    duration = 3000;
    break;
  case 6: /* 50.000 */
    duration = 1800;
    break;
  case 7: /* 59.940 */
    duration = 1525;
    break;
  case 8: /* 60.000 */
    duration = 1509;
    break;
  default:
    printf("dxr3_decode_video: WARNING: unknown frame rate code %d: using PAL\n",
      this->frame_rate_code);
    duration = 3600;  /* PAL 25fps */
    break;
  }
  
  /* update stream metadata */
  this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION] = duration;
  
  if (this->correct_durations) {
    /* we set an initial average frame duration here */
    if (!this->avg_duration) this->avg_duration = duration;
  
    /* Apply a correction to the framerate-code if metronom
     * insists on a different frame duration.
     * The code below is for NTCS streams labeled as PAL streams.
     * (I have seen such things even on dvds!)
     */
    if (this->avg_duration && this->avg_duration < 3300 && duration == 3600) {
      if (this->force_duration_window > 0) {
        /* we are already in a force_duration window, so we force duration */
        this->force_duration_window = FORCE_DURATION_WINDOW_SIZE;
        return 3000;
      }
      if (this->force_duration_window <= 0 && (this->force_duration_window += 10) > 0) {
        /* we just entered a force_duration window, so we start the correction */
        metronom_t *metronom = this->stream->metronom;
        int64_t cur_offset;
        printf("dxr3_decode_video: WARNING: correcting frame rate code from PAL to NTSC\n");
        /* those weird streams need an offset, too */
        cur_offset = metronom->get_option(metronom, METRONOM_AV_OFFSET);
        metronom->set_option(metronom, METRONOM_AV_OFFSET, cur_offset - 28800);
        this->force_duration_window = FORCE_DURATION_WINDOW_SIZE;
        return 3000;
      }
    }
    
    if (this->force_duration_window == -FORCE_DURATION_WINDOW_SIZE)
      /* we are far from a force_duration window */
      return duration;
    if (--this->force_duration_window == 0) {
      /* we have just left a force_duration window */
      metronom_t *metronom = this->stream->metronom;
      int64_t cur_offset;
      cur_offset = metronom->get_option(metronom, METRONOM_AV_OFFSET);
      metronom->set_option(metronom, METRONOM_AV_OFFSET, cur_offset + 28800);
      this->force_duration_window = -FORCE_DURATION_WINDOW_SIZE;
    }
  }
  
  return duration;
}

static void dxr3_update_sync_mode(void *this_gen, xine_cfg_entry_t *entry)
{
  ((dxr3_decoder_t *)this_gen)->sync_every_frame = entry->num_value;
  printf("dxr3_decode_video: setting sync_every_frame to %s\n", 
    (entry->num_value ? "on" : "off"));
}

static void dxr3_update_enhanced_mode(void *this_gen, xine_cfg_entry_t *entry)
{
  ((dxr3_decoder_t *)this_gen)->enhanced_mode = entry->num_value;
  printf("dxr3_decode_video: setting enhanced mode to %s\n", 
    (entry->num_value ? "on" : "off"));
}

static void dxr3_update_correct_durations(void *this_gen, xine_cfg_entry_t *entry)
{
  ((dxr3_decoder_t *)this_gen)->correct_durations = entry->num_value;
  printf("dxr3_decode_video: setting correct_durations mode to %s\n", 
    (entry->num_value ? "on" : "off"));
}
