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
 * $Id: xine.c,v 1.159 2002/09/18 00:51:34 guenter Exp $
 *
 * top-level xine functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#if defined (__linux__)
#include <endian.h>
#elif defined (__FreeBSD__)
#include <machine/endian.h>
#endif

#include "xine_internal.h"
#include "plugin_catalog.h"
#include "audio_out.h"
#include "video_out.h"
#include "demuxers/demux.h"
#include "buffer.h"
#include "libspudec/spu_decoder_api.h"
/* TODO: who uses spu_decoder.h ? */
#include "spu_decoder.h"
#include "input/input_plugin.h"
#include "metronom.h"
#include "configfile.h"
#include "osd.h"

#include "xineutils.h"
#include "compat.h"

#define LOGO_DELAY 500000 /* usec */

static void play_logo_internal (xine_t *this) {
  pthread_mutex_lock (&this->logo_lock);
  this->playing_logo = 1;
  if( !xine_open_internal(this, this->logo_mrl) )
    this->playing_logo = 0;
  else {
    xine_play_internal (this, 0, 0);
    this->status = XINE_STATUS_LOGO;
  }
  pthread_mutex_unlock (&this->logo_lock);
}

/* config callback for logo mrl changing */
static void _logo_change_cb(void *data, xine_cfg_entry_t *cfg) {
  xine_t            *this = (xine_t *) data;
  
  pthread_mutex_lock (&this->logo_lock);
  this->logo_mrl = cfg->str_value;
  pthread_mutex_unlock (&this->logo_lock);
  
  /*
   * Start playback of new mrl only if 
   * current status is XINE_STATUS_STOP or XINE_STATUS_LOGO 
   */
  pthread_mutex_lock (&this->xine_lock);
  if(this->metronom && (this->status == XINE_STATUS_LOGO || this->status == XINE_STATUS_STOP)) {
    xine_stop_internal(this);  
    this->metronom->adjust_clock(this->metronom,
				 this->metronom->get_current_time(this->metronom) + 30 * 90000 );
    play_logo_internal(this);
  }
  pthread_mutex_unlock (&this->xine_lock);
}

void * xine_notify_stream_finished_thread (void * this_gen) {
  xine_t *this = this_gen;
  xine_event_t event;

  pthread_mutex_lock (&this->xine_lock);
  xine_stop_internal (this);
  pthread_mutex_unlock (&this->xine_lock);

  event.type = XINE_EVENT_PLAYBACK_FINISHED;
  xine_send_event (this, &event);

  xine_usec_sleep (LOGO_DELAY);
  
  pthread_mutex_lock (&this->xine_lock);
  if (this->status == XINE_STATUS_STOP) {
    play_logo_internal(this);
  }
  pthread_mutex_unlock (&this->xine_lock);

  return NULL;
}

void xine_notify_stream_finished (xine_t *this) {
  int err;

  if (this->status == XINE_STATUS_QUIT)
    return;

  if (this->finished_thread_running)
    pthread_join (this->finished_thread, NULL);

  this->finished_thread_running = 1;

  /* This thread will just execute xine_stop and (possibly) xine_play then die.
     It might look useless but i need to detach this code from the current
     thread to make sure that video_decoder and audio_decoder are running and
     freeing buffers. Free buffers might be needed by the main thread during
     a xine_play, for example.

     This is not a theorical situation: i was able to trigger it with simple
     user actions (play,seek,etc). [MF]
  */
  if ((err = pthread_create (&this->finished_thread,
			     NULL, xine_notify_stream_finished_thread, this)) != 0) {
    printf (_("xine_notify_stream_finished: can't create new thread (%s)\n"),
	    strerror(err));
    abort();
  }
}

void xine_report_codec( xine_t *this, int codec_type, uint32_t fourcc, uint32_t buf_type, int handled ) {

  if( this->report_codec_cb ) {
    if( codec_type == XINE_CODEC_VIDEO ) {
      if( !buf_type )
        buf_type = fourcc_to_buf_video( fourcc );

      this->report_codec_cb( this->report_codec_user_data,
                             codec_type, fourcc,
                             buf_video_name( buf_type ), handled );
    } else {
      if( !buf_type )
        buf_type = formattag_to_buf_audio( fourcc );
    
      this->report_codec_cb( this->report_codec_user_data,
                             codec_type, fourcc,
                             buf_audio_name( buf_type ), handled );
    }
  }
}

int xine_register_report_codec_cb(xine_p this_ro, 
				  xine_report_codec_cb_t report_codec,
				  void *user_data) {
  xine_t *this = (xine_t *)this_ro;
  
  this->report_codec_cb = report_codec;
  this->report_codec_user_data = user_data;
  return 1;
}

void xine_internal_osd (xine_t *this, char *str, int duration) {

  uint32_t seconds;
  char tstr[256];
  int64_t start_time;
  
  this->curtime_needed_for_osd = 0;
  start_time = this->metronom->get_current_time (this->metronom);
    
  if (this->osd_display) {
   
    this->osd_renderer->filled_rect (this->osd, 0, 0, 299, 99, 0);
    this->osd_renderer->render_text (this->osd, 0, 5, str, OSD_TEXT1);
  
    seconds = this->cur_input_time;
  
    sprintf (tstr, "%02d:%02d:%02d", 
             seconds / (60 * 60),
             (seconds % (60*60)) / 60,
             seconds % 60);
    
    this->osd_renderer->render_text (this->osd, 45, 5, tstr, OSD_TEXT1);
  
    this->osd_renderer->show (this->osd, start_time);
    this->osd_renderer->hide (this->osd, start_time+duration);
  }
}

static void update_osd_display(void *this_gen, xine_cfg_entry_t *entry)
{
  xine_t *this = (xine_t *) this_gen;
  
  this->osd_display = entry->num_value;
}


static void xine_set_speed_internal (xine_t *this, int speed) {

  this->metronom->set_speed (this->metronom, speed);

  /* see coment on audio_out loop about audio_paused */
  if( this->audio_out ) {
    this->audio_out->audio_paused = (speed != XINE_SPEED_NORMAL) + 
                                    (speed == XINE_SPEED_PAUSE);

    if (speed != XINE_SPEED_NORMAL && speed != XINE_SPEED_PAUSE)
	this->audio_out->control(this->audio_out, AO_CTRL_FLUSH_BUFFERS);

    this->audio_out->control(this->audio_out,
			     speed == XINE_SPEED_PAUSE ? AO_CTRL_PLAY_PAUSE : AO_CTRL_PLAY_RESUME);
  }

  this->speed = speed;
}


void xine_stop_internal (xine_t *this) {

  printf ("xine_stop\n");

  if (this->status == XINE_STATUS_STOP) {
    printf ("xine_stop ignored\n");
    return;
  }
  
  if (this->audio_out)
    this->audio_out->control(this->audio_out, AO_CTRL_FLUSH_BUFFERS);

  xine_set_speed_internal(this, XINE_SPEED_NORMAL);

  /* Don't change status if we're quitting */
  if(this->status != XINE_STATUS_QUIT)
    this->status = XINE_STATUS_STOP;
    
  printf ("xine_stop: stopping demuxer\n");
  if(this->cur_demuxer_plugin) {
    this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    this->cur_demuxer_plugin = NULL;
  }
  printf ("xine_stop: stopped demuxer\n");

  if(this->cur_input_plugin) {
    this->cur_input_plugin->close(this->cur_input_plugin);
    if (strcmp(this->cur_mrl, this->logo_mrl) != 0)
      /* remember the last input plugin for a possible eject */
      this->last_input_plugin = this->cur_input_plugin;
  }

  printf ("xine_stop: done\n");
}

void xine_stop (xine_p this_ro) {
  xine_t *this = (xine_t *)this_ro;
  pthread_mutex_lock (&this->xine_lock);
  xine_stop_internal(this);
  
  /*
     this will make output threads discard about everything
     am i abusing of xine architeture? :)
  */
  this->metronom->adjust_clock(this->metronom,
			       this->metronom->get_current_time(this->metronom) + 30 * 90000 );
  
  if(this->status == XINE_STATUS_STOP) {
    play_logo_internal(this);
  }

  pthread_mutex_unlock (&this->xine_lock);
}


/*
 * demuxer probing 
 */
static int probe_demux (xine_t *this, int stage1, int stage2) {

  int i;
  int stages[3];

  stages[0] = stage1;
  stages[1] = stage2;
  stages[2] = -1;

  if (stages[0] == -1) {
    printf ("xine: probe_demux stage1 = %d is not allowed \n", stage1);
    return 0;
  }

  i = 0;
  while (stages[i] != -1) {

    plugin_node_t *node;

    node = xine_list_first_content (this->plugin_catalog->demux);

    while (node) {
      demux_plugin_t *plugin;

      plugin = (demux_plugin_t *) node->plugin;

      if (plugin->open (plugin, 
			this->cur_input_plugin, 
			stages[i]) == DEMUX_CAN_HANDLE) {
	
	this->cur_demuxer_plugin = plugin;

	return 1;
      }
      node = xine_list_next_content (this->plugin_catalog->demux);
    }
    i++;
  }

  return 0;
}

/*
 * try to find a demuxer which handle current mrl.
 */
static int find_demuxer(xine_t *this) {

  this->cur_demuxer_plugin = NULL;

  switch (this->demux_strategy) {

  case DEMUX_DEFAULT_STRATEGY:
    if (probe_demux (this, STAGE_BY_CONTENT, STAGE_BY_EXTENSION))
      return 1;
    break;

  case DEMUX_REVERT_STRATEGY:
    if (probe_demux (this, STAGE_BY_EXTENSION, STAGE_BY_CONTENT))
      return 1;
    break;

  case DEMUX_CONTENT_STRATEGY:
    if (probe_demux (this, STAGE_BY_CONTENT, -1))
      return 1;
    break;

  case DEMUX_EXTENSION_STRATEGY:
    if (probe_demux (this, STAGE_BY_EXTENSION, -1))
      return 1;
    break;
  }
  
  return 0;
}

int xine_open_internal (xine_t *this, const char *mrl) {

  printf ("xine: open mrl '%s'\n", mrl);

  /* 
   * is this an 'opt:' mrlstyle ? 
   */ 
  if (xine_config_change_opt(this->config, mrl)) {
    xine_event_t event;
    
    this->status = XINE_STATUS_STOP;
    
    event.type = XINE_EVENT_PLAYBACK_FINISHED;
    pthread_mutex_unlock (&this->xine_lock);
    xine_send_event (this, &event);
    pthread_mutex_lock (&this->xine_lock);
    return 1;
  }

  /*
   * stop engine only for different mrl
   */

  if ((this->status == XINE_STATUS_PLAY && strcmp (mrl, this->cur_mrl)) 
      || (this->status == XINE_STATUS_LOGO)) {
    
    printf ("xine: stopping engine\n");

    if (this->speed != XINE_SPEED_NORMAL) 
      xine_set_speed_internal (this, XINE_SPEED_NORMAL);

    if(this->cur_demuxer_plugin) {
      this->playing_logo = 0;
      this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    }
    
    if(this->cur_input_plugin) {

      if (strcmp (mrl, this->cur_mrl)) 
        this->cur_input_plugin->close(this->cur_input_plugin);
      else
        this->cur_input_plugin->stop(this->cur_input_plugin);
    }

    if (this->audio_out)
      this->audio_out->control(this->audio_out, AO_CTRL_FLUSH_BUFFERS);

    this->status = XINE_STATUS_STOP;
  }

  if (this->status == XINE_STATUS_STOP) {

    plugin_node_t *node;
    int            i, header_count;

    /*
     * (1/3) reset metainfo 
     */
    
    for (i=0; i<XINE_STREAM_INFO_MAX; i++) {
      this->stream_info[i] = 0;
      if (this->meta_info[i]) {
	free (this->meta_info[i]);
	this->meta_info[i] = NULL;
      }
    }

    /* 
     * (2/3) start engine for new mrl'
     */

    printf ("xine: starting engine for new mrl\n");

    /*
     * find input plugin
     */
    this->cur_input_plugin = NULL;
    node = xine_list_first_content (this->plugin_catalog->input);
    while (node) {
      input_plugin_t *plugin;
      
      plugin = (input_plugin_t *) node->plugin;

      if (plugin->open (plugin, mrl)) {
        this->cur_input_plugin = plugin;
        break;
      }
      node = xine_list_next_content (this->plugin_catalog->input);
    }

    if (!this->cur_input_plugin) {
      xine_log (this, XINE_LOG_MSG, 
	        _("xine: cannot find input plugin for this MRL\n"));
      this->cur_demuxer_plugin = NULL;
      this->err = XINE_ERROR_NO_INPUT_PLUGIN;

      return 0;
    }
  
    this->meta_info[XINE_META_INFO_INPUT_PLUGIN] 
      = strdup (this->cur_input_plugin->get_identifier(this->cur_input_plugin));

    /*
     * find demuxer plugin
     */
    header_count = this->header_sent_counter+1;

    if (!find_demuxer(this)) {
      xine_log (this, XINE_LOG_MSG,
	        _("xine: couldn't find demuxer for >%s<\n"), mrl);
      this->cur_input_plugin->close(this->cur_input_plugin);
      this->err = XINE_ERROR_NO_DEMUXER_PLUGIN;
      return 0;
    }

    this->meta_info[XINE_META_INFO_SYSTEMLAYER] 
      = strdup (this->cur_demuxer_plugin->get_identifier());

    /* FIXME: ?? limited length ??? */
    strncpy (this->cur_mrl, mrl, 1024);

    printf ("xine: engine start successful - waiting for headers to be sent\n");

    /*
     * (3/3) wait for headers to be sent and decoded
     */

    while (header_count>this->header_sent_counter) {
      printf ("xine: waiting for headers.\n");
      xine_usec_sleep (20000);
    }

    printf ("xine: xine_open done.\n");

    return 1;
  }

  printf ("xine: xine_open ignored (same mrl, already playing)\n");
  return 0;
}

int xine_play_internal (xine_t *this, int start_pos, int start_time) {

  double     share ;
  off_t      pos, len;
  int        demux_status;

  printf ("xine: xine_play_internal\n");

  if (this->speed != XINE_SPEED_NORMAL) 
    xine_set_speed_internal (this, XINE_SPEED_NORMAL);

  /*
   * start/seek demuxer
   */
  if (start_pos) {
    /* FIXME: do we need to protect concurrent access to input plugin here? */
    len = this->cur_input_plugin->get_length (this->cur_input_plugin);
    share = (double) start_pos / 65535;
    pos = (off_t) (share * len) ;
  } else
    pos = 0;
  
  if (this->status == XINE_STATUS_STOP) {

    demux_status = this->cur_demuxer_plugin->start (this->cur_demuxer_plugin,
						    pos, start_time);
  } else {
    demux_status = this->cur_demuxer_plugin->seek (this->cur_demuxer_plugin,
						   pos, start_time);
  }

  if (demux_status != DEMUX_OK) {
    xine_log (this, XINE_LOG_MSG, 
	      _("xine_play: demuxer failed to start\n"));
    
    this->err = XINE_ERROR_DEMUXER_FAILED;
    
    if( this->status == XINE_STATUS_STOP )      
      this->cur_input_plugin->close(this->cur_input_plugin);
  
    return 0;
    
  } else {

    this->status = XINE_STATUS_PLAY;
    
    /* osd will be updated as soon as we know cur_input_time */
    if( !this->playing_logo )
      this->curtime_needed_for_osd = 5;
  }

  printf ("xine: xine_play_internal ...done\n");

  return 1;
}             

int xine_open (xine_p this_ro, const char *mrl) {
  xine_t *this = (xine_t *)this_ro;
  int ret;

  pthread_mutex_lock (&this->xine_lock);
  ret = xine_open_internal (this, mrl);
  pthread_mutex_unlock (&this->xine_lock);
  
  return ret;
}

int  xine_play (xine_p this_ro, int start_pos, int start_time) {
  xine_t *this = (xine_t *)this_ro;
  int ret;

  pthread_mutex_lock (&this->xine_lock);
  ret = xine_play_internal (this, start_pos, start_time);
  pthread_mutex_unlock (&this->xine_lock);
  
  return ret;
}


int xine_eject (xine_p this_ro) {
  xine_t *this = (xine_t *)this_ro;
  
  int status;

  if(this->last_input_plugin == NULL) 
    return 0;
  
  pthread_mutex_lock (&this->xine_lock);

  status = 0;
  if (((this->status == XINE_STATUS_STOP) || (this->status == XINE_STATUS_LOGO))
      && this->last_input_plugin && this->last_input_plugin->eject_media) {

    status = this->last_input_plugin->eject_media (this->last_input_plugin);
  }

  pthread_mutex_unlock (&this->xine_lock);
  return status;
}

void xine_exit (xine_p this_ro) {
  xine_t *this = (xine_t *)this_ro;

  int i;

  this->status = XINE_STATUS_QUIT;

  xine_stop(this);

  pthread_mutex_lock (&this->finished_lock);

  if (this->finished_thread_running)
    pthread_join (this->finished_thread, NULL);

  pthread_mutex_unlock (&this->finished_lock);

  printf ("xine_exit: shutdown audio\n");

  audio_decoder_shutdown (this);

  printf ("xine_exit: shutdown video\n");

  video_decoder_shutdown (this);

  this->osd_renderer->close( this->osd_renderer );
  this->video_out->exit (this->video_out);
  this->video_fifo->dispose (this->video_fifo);

  this->status = XINE_STATUS_QUIT;

  printf ("xine_exit: bye!\n");

  for (i = 0; i < XINE_LOG_NUM; i++)
    this->log_buffers[i]->dispose (this->log_buffers[i]);

  this->metronom->exit (this->metronom);

  dispose_plugins (this);
  xine_profiler_print_results ();
  this->config->dispose(this->config);

  pthread_mutex_destroy (&this->logo_lock);
  pthread_mutex_destroy (&this->xine_lock);
  pthread_mutex_destroy (&this->finished_lock);
  pthread_mutex_destroy (&this->osd_lock);

  free (this);

}

xine_p xine_new (void) {

  xine_t      *this;
  int          i;

  this = xine_xmalloc (sizeof (xine_t));
  if (!this) {
    printf ("xine: failed to malloc xine_t\n");
    abort();
  }
  

#ifdef ENABLE_NLS
  /*
   * i18n
   */

  bindtextdomain("xine-lib", XINE_LOCALEDIR);
#endif 

  /*
   * init locks
   */

  pthread_mutex_init (&this->xine_lock, NULL);

  pthread_mutex_init (&this->finished_lock, NULL);
  
  pthread_mutex_init (&this->osd_lock, NULL);

  this->finished_thread_running = 0;

  /*
   * config
   */

  this->config = xine_config_init ();

  /* 
   * log buffers 
   */

  for (i = 0; i < XINE_LOG_NUM; i++)
    this->log_buffers[i] = new_scratch_buffer (25);

  /*
   * defaults 
   */
  
  this->err                    = XINE_ERROR_NONE;
  this->spu_channel_auto       = -1;
  this->spu_channel_letterbox  = -1;
  this->spu_channel_pan_scan   = -1;
  this->spu_channel_user       = -1;
  this->cur_input_pos          = 0;
  this->cur_input_length       = 0;
  this->last_input_plugin      = NULL;
  this->num_event_listeners    = 0; /* initially there are none */
  this->cur_input_plugin       = NULL;
  this->cur_spu_decoder_plugin = NULL;
  this->report_codec_cb        = NULL; 
  this->header_sent_counter    = 0;

  /* 
   * meta info
   */

  for (i=0; i<XINE_STREAM_INFO_MAX; i++) {
    this->stream_info[i] = 0;
    this->meta_info  [i] = NULL;
  }

  /* 
   * plugins
   */
  
  scan_plugins(this);

  /*
   * logo 
   */

  pthread_mutex_init (&this->logo_lock, NULL);

  pthread_mutex_lock (&this->logo_lock);
  this->logo_mrl = this->config->register_string(this->config, 
						 "misc.logo_mrl", 
						 XINE_LOGO_FILE,
						 _("logo mrl, displayed in video output window"),
						 NULL, 0, _logo_change_cb, 
						 (void *) this);
  pthread_mutex_unlock (&this->logo_lock);

  return this;

}


void xine_init (xine_p this_ro,
		xine_ao_driver_p ao_ro, 
		xine_vo_driver_p vo_ro) {
  xine_t *this = (xine_t *)this_ro;
  xine_ao_driver_t *ao = (xine_ao_driver_t *)ao_ro;
  xine_vo_driver_t *vo = (xine_vo_driver_t *)vo_ro;

  static char *demux_strategies[] = {"default", "reverse", "content",
				     "extension", NULL};

  this->video_driver = vo;

  /* initialize color conversion tables and functions */
  init_yuv_conversion();

  
  /*
   * create a metronom
   */

  this->metronom = metronom_init ( (ao != NULL), this);
        
  /* probe for optimized memcpy or config setting */
  xine_probe_fast_memcpy (this->config);

  /*
   * content detection strategy
   */

  this->demux_strategy  = this->config->register_enum (this->config, 
						       "misc.demux_strategy", 
						       0,
						       demux_strategies, 
						       "media format detection strategy",
						       NULL, 10, NULL, NULL);

  /*
   * init and start decoder threads
   */

  this->video_out = vo_new_instance (vo, this);
  video_decoder_init (this);

  this->osd_renderer = osd_renderer_init (this->video_out->get_overlay_instance (this->video_out), this->config );
  
  this->osd = this->osd_renderer->new_object (this->osd_renderer, 300, 100);
  this->osd_renderer->set_font (this->osd, "cetus", 24);
  this->osd_renderer->set_text_palette (this->osd, TEXTPALETTE_WHITE_BLACK_TRANSPARENT, OSD_TEXT1 );
  this->osd_renderer->set_position (this->osd, 10,10);

  this->osd_display = this->config->register_bool (this->config, 
						   "misc.osd_display", 1,
						   "Show status on play, pause, ff, ...", 
						   NULL, 0,
						   update_osd_display, this );
  
  if (ao) 
    this->audio_out = ao_new_instance (ao, this);

  audio_decoder_init (this);

  /*
   * start metronom clock (needed for osd)
   */

  this->metronom->start_clock (this->metronom, 0);

  if (this->osd_display) {
   
    char tstr[30];

    this->osd_renderer->filled_rect (this->osd, 0, 0, 299, 99, 0);
    sprintf (tstr, "xine-lib v%01d.%01d.%01d", XINE_MAJOR, XINE_MINOR, XINE_SUB);

    this->osd_renderer->render_text (this->osd, 5, 5, tstr, OSD_TEXT1);
  
    this->osd_renderer->show (this->osd, 0);
    this->osd_renderer->hide (this->osd, 300000);
  }

  this->status = XINE_STATUS_STOP;

  play_logo_internal(this);
}

void xine_select_spu_channel (xine_t *this, int channel) {

  pthread_mutex_lock (&this->xine_lock);

  this->spu_channel_user = (channel >= -2 ? channel : -2);

  switch (this->spu_channel_user) {
  case -2:
    this->spu_channel = -1;
    this->video_out->enable_ovl (this->video_out, 0);
    break;
  case -1:
    this->spu_channel = this->spu_channel_auto;
    this->video_out->enable_ovl (this->video_out, 1);
    break;
  default:
    this->spu_channel = this->spu_channel_user;
    this->video_out->enable_ovl (this->video_out, 1);
  }

  pthread_mutex_unlock (&this->xine_lock);
}

static int xine_get_current_position (xine_t *this) {

  off_t len;
  double share;
  
  pthread_mutex_lock (&this->xine_lock);

  if (!this->cur_input_plugin) {
    printf ("xine: xine_get_current_position: no input source\n");
    pthread_mutex_unlock (&this->xine_lock);
    return 0;
  }
  
  /* pos = this->mCurInput->seek (0, SEEK_CUR); */
  len = this->cur_input_length;
  if (len == 0) len = this->cur_input_plugin->get_length (this->cur_input_plugin); 
  share = (double) this->cur_input_pos / (double) len * 65535;

  pthread_mutex_unlock (&this->xine_lock);

  return (int) share;
}

int xine_get_status(xine_p this_ro) {
  xine_t *this = (xine_t *)this_ro;
  int status;

  status = this->status;
  if( status == XINE_STATUS_LOGO )
    status = XINE_STATUS_STOP;
  return status;
}

/*
 * trick play 
 */

void xine_set_speed (xine_t *this, int speed) {

  pthread_mutex_lock (&this->xine_lock);

  if (speed <= XINE_SPEED_PAUSE) 
    speed = XINE_SPEED_PAUSE;
  else if (speed > XINE_SPEED_FAST_4) 
    speed = XINE_SPEED_FAST_4;

  /* osd */

  pthread_mutex_lock (&this->osd_lock);
  switch (speed) {
  case XINE_SPEED_PAUSE:
    xine_internal_osd (this, "<", 90000);
    break;
  case XINE_SPEED_SLOW_4:
    xine_internal_osd (this, "<>", 20000 * speed);
    break;
  case XINE_SPEED_SLOW_2:
    xine_internal_osd (this, "@>", 20000 * speed);
    break;
  case XINE_SPEED_NORMAL:
    xine_internal_osd (this, ">", 20000 * speed);
    break;
  case XINE_SPEED_FAST_2:
    xine_internal_osd (this, "$$", 20000 * speed);
    break;
  case XINE_SPEED_FAST_4:
    xine_internal_osd (this, "$$$", 20000 * speed);
    break;
  } 
  pthread_mutex_unlock (&this->osd_lock);
    
  printf ("xine: set_speed %d\n", speed);
  xine_set_speed_internal (this, speed);

  pthread_mutex_unlock (&this->xine_lock);
}


int xine_get_speed (xine_t *this) {
  return this->speed;
}

/*
 * time measurement / seek
 */

static int xine_get_stream_length (xine_t *this) {

  if(this->cur_demuxer_plugin)
    return this->cur_demuxer_plugin->get_stream_length (this->cur_demuxer_plugin);

  return 0;
}

int xine_get_pos_length (xine_p this_ro, int *pos_stream, 
			 int *pos_time, int *length_time) {
  xine_t *this = (xine_t *)this_ro;
  
  if (pos_stream)
    *pos_stream  = xine_get_current_position (this); 
  if (pos_time)
    *pos_time    = this->cur_input_time * 1000;
  if (length_time)
    *length_time = xine_get_stream_length (this) * 1000;

  return 1;
}

static int xine_get_audio_capabilities(xine_t *this) {

  if(this->audio_out)
    return (this->audio_out->get_capabilities(this->audio_out));

  return AO_CAP_NOCAP;
}

static int xine_get_audio_property(xine_t *this, int property) {
  
  if(this->audio_out)
    return(this->audio_out->get_property(this->audio_out, property));

  return 0;
}

static int xine_set_audio_property(xine_t *this, int property, int value) {

  if(this->audio_out)
    return(this->audio_out->set_property(this->audio_out, property, value));
  
  return ~value;
}

int xine_get_current_frame (xine_p this_ro, int *width, int *height,
			    int *ratio_code, int *format,
			    uint8_t *img) {
  xine_t *this = (xine_t *)this_ro;

  vo_frame_t *frame;

  frame = this->video_out->get_last_frame (this->video_out);

  if (!frame)
    return 0;

  *width = frame->width;
  *height = frame->height;

  *ratio_code = frame->ratio;
  *format = frame->format;

  switch (frame->format) {

  case XINE_IMGFMT_YV12:
    memcpy (img, frame->base[0], frame->width*frame->height);
    memcpy (img+frame->width*frame->height, frame->base[1], 
	    frame->width*frame->height/4);
    memcpy (img+frame->width*frame->height+frame->width*frame->height/4, 
	    frame->base[1], 
	    frame->width*frame->height/4);
    break;

  case XINE_IMGFMT_YUY2:
    memcpy (img, frame->base[0], frame->width * frame->height * 2);
    break;

  default:
    printf ("xine: error, snapshot function not implemented for format 0x%x\n",
	    frame->format);
    abort ();
  }

  return 1;
}

const char * xine_get_spu_lang (xine_p this_ro, int channel) {
  xine_t *this = (xine_t *)this_ro;

  if (this->cur_input_plugin) {
    if (this->cur_input_plugin->get_capabilities (this->cur_input_plugin) & INPUT_CAP_SPULANG) {
      this->cur_input_plugin->get_optional_data (this->cur_input_plugin, 
						 this->spu_lang, 
						 INPUT_OPTIONAL_DATA_SPULANG);
      return this->spu_lang;
    }
  } 

  return NULL;
}

const char* xine_get_audio_lang (xine_p this_ro, int channel) {
  xine_t *this = (xine_t *)this_ro;

  if (this->cur_input_plugin) {
    if (this->cur_input_plugin->get_capabilities (this->cur_input_plugin) & INPUT_CAP_AUDIOLANG) {
      this->cur_input_plugin->get_optional_data (this->cur_input_plugin, 
						 this->audio_lang, 
						 INPUT_OPTIONAL_DATA_AUDIOLANG);
      return this->audio_lang;
    }
  } 

  return NULL;
}

int xine_get_spu_channel (xine_t *this) {

  return this->spu_channel_user;
}

osd_renderer_t *xine_get_osd_renderer (xine_t *this) {

  return this->osd_renderer;
}

/*
 * log functions
 */
int xine_get_log_section_count (xine_p this_ro) {
  return XINE_LOG_NUM;
}

const char *const *xine_get_log_names (xine_p this_ro) {
  static const char *log_sections[XINE_LOG_NUM + 1];

  log_sections[XINE_LOG_MSG]      = _("messages");  
  log_sections[XINE_LOG_PLUGIN]   = _("plugin");
  log_sections[XINE_LOG_NUM]      = NULL;
  
  return log_sections;
}

void xine_log (xine_p this_ro, int buf, const char *format, ...) {
  xine_t *this = (xine_t *)this_ro;

  va_list argp;

  va_start (argp, format);

  this->log_buffers[buf]->scratch_printf (this->log_buffers[buf], format, argp);
  va_end (argp);

  va_start (argp, format);

  vprintf (format, argp);

  va_end (argp);
}

const char *const *xine_get_log (xine_p this_ro, int buf) {
  xine_t *this = (xine_t *)this_ro;
  
  if(buf >= XINE_LOG_NUM)
    return NULL;
  
  return this->log_buffers[buf]->get_content (this->log_buffers[buf]);
}

void xine_register_log_cb (xine_p this_ro, xine_log_cb_t cb, void *user_data) {

  printf ("xine: xine_register_log_cb: not implemented yet.\n");
  abort();
}


int xine_get_error (xine_p this_ro) {
  return this_ro->err;
}

int xine_trick_mode (xine_p this_ro, int mode, int value) {
  printf ("xine: xine_trick_mode not implemented yet.\n");
  abort ();
}

