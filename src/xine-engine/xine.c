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
 * $Id: xine.c,v 1.136 2002/06/07 04:15:46 miguelfreitas Exp $
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

/* config callback for logo mrl changing */
static void _logo_change_cb(void *data, cfg_entry_t *cfg) {
  xine_t            *this = (xine_t *) data;
  
  pthread_mutex_lock (&this->logo_lock);
  this->logo_mrl = cfg->str_value;
  pthread_mutex_unlock (&this->logo_lock);
  
  /*
   * Start playback of new mrl only if 
   * current status is XINE_STOP or XINE_LOGO 
   */
  pthread_mutex_lock (&this->xine_lock);
  if((this->status == XINE_LOGO) || (this->status == XINE_STOP)) {
    xine_stop_internal(this);  
    this->metronom->adjust_clock(this->metronom,
				 this->metronom->get_current_time(this->metronom) + 30 * 90000 );
    pthread_mutex_lock (&this->logo_lock);
    xine_play_internal(this, this->logo_mrl, 0, 0);
    this->status = XINE_LOGO;
    pthread_mutex_unlock (&this->logo_lock);
  }
  pthread_mutex_unlock (&this->xine_lock);
}

void * xine_notify_stream_finished_thread (void * this_gen) {
  xine_t *this = this_gen;
  xine_event_t event;

  pthread_mutex_lock (&this->xine_lock);
  xine_stop_internal (this);
  pthread_mutex_unlock (&this->xine_lock);

  pthread_mutex_lock (&this->logo_lock);
  if (strcmp(this->cur_mrl, this->logo_mrl)) {

    event.type = XINE_EVENT_PLAYBACK_FINISHED;
    xine_send_event (this, &event);

    xine_usec_sleep (LOGO_DELAY);
  
    pthread_mutex_lock (&this->xine_lock);
    if (this->status == XINE_STOP) {
      xine_play_internal(this, this->logo_mrl, 0, 0);
      this->status = XINE_LOGO; 
    }
    pthread_mutex_unlock (&this->xine_lock);
  }
  pthread_mutex_unlock (&this->logo_lock);

  return NULL;
}

void xine_notify_stream_finished (xine_t *this) {
  int err;

  if (this->status == XINE_QUIT)
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

int xine_register_report_codec_cb(xine_t *this, xine_report_codec_t report_codec,
				 void *user_data) {
  
  this->report_codec_cb = report_codec;
  this->report_codec_user_data = user_data;
  return 0;
}

static void xine_internal_osd (xine_t *this, char *str, 
			       uint32_t start_time, uint32_t duration) {

  uint32_t seconds;
  char tstr[256];

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

static void update_osd_display(void *this_gen, cfg_entry_t *entry)
{
  xine_t *this = (xine_t *) this_gen;
  
  this->osd_display = entry->num_value;
}


static void xine_set_speed_internal (xine_t *this, int speed) {

  this->metronom->set_speed (this->metronom, speed);

  /* see coment on audio_out loop about audio_paused */
  if( this->audio_out ) {
    this->audio_out->audio_paused = (speed != SPEED_NORMAL) + 
                                    (speed == SPEED_PAUSE);

    if (speed != SPEED_NORMAL && speed != SPEED_PAUSE)
	this->audio_out->control(this->audio_out, AO_CTRL_FLUSH_BUFFERS);

    this->audio_out->control(this->audio_out,
			     speed == SPEED_PAUSE ? AO_CTRL_PLAY_PAUSE : AO_CTRL_PLAY_RESUME);
  }

  this->speed = speed;
}


void xine_stop_internal (xine_t *this) {

  printf ("xine_stop\n");

  /* xine_internal_osd (this, "}", this->metronom->get_current_time (this->metronom), 30000); never works */
  
  if (this->status == XINE_STOP) {
    printf ("xine_stop ignored\n");
    return;
  }
  
  if (this->audio_out)
    this->audio_out->control(this->audio_out, AO_CTRL_FLUSH_BUFFERS);

  xine_set_speed_internal(this, SPEED_NORMAL);

  /* Don't change status if we're quitting */
  if(this->status != XINE_QUIT)
    this->status = XINE_STOP;
    
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

void xine_stop (xine_t *this) {
  pthread_mutex_lock (&this->xine_lock);
  xine_stop_internal(this);
  
  /*
     this will make output threads discard about everything
     am i abusing of xine architeture? :)
  */
  this->metronom->adjust_clock(this->metronom,
			       this->metronom->get_current_time(this->metronom) + 30 * 90000 );
  
  if(this->status == XINE_STOP) {
    pthread_mutex_lock (&this->logo_lock);
    xine_play_internal(this, this->logo_mrl,0,0);
    this->status = XINE_LOGO;
    pthread_mutex_unlock (&this->logo_lock);
  }
  pthread_mutex_unlock (&this->xine_lock);
}


/*
 * *****
 * Demuxers probing stuff
 */
static int try_demux_with_stages(xine_t *this, const char *MRL, 
				 int stage1, int stage2) {
  int s = 0, i;
  int stages[3];

  stages[0] = stage1;
  stages[1] = stage2;
  stages[2] = -1;

  if(stages[0] == -1) {
    printf (_("%s(%d) wrong first stage = %d !!\n"), 
	    __XINE_FUNCTION__, __LINE__, stage1);
    return 0;
  }

  while(stages[s] != -1) {
    for(i = 0; i < this->num_demuxer_plugins; i++) {
      /* printf ("trying demuxer %s\n", this->demuxer_plugins[i]->get_identifier()); */
      if(this->demuxer_plugins[i]->open(this->demuxer_plugins[i], 
					this->cur_input_plugin, 
					stages[s]) == DEMUX_CAN_HANDLE) {
	
	this->cur_demuxer_plugin = this->demuxer_plugins[i];

	return 1;
      }
    }
    s++;
  }

  return 0;
}
/*
 * Try to find a demuxer which handle the MRL stream
 */
static int find_demuxer(xine_t *this, const char *MRL) {

  this->cur_demuxer_plugin = NULL;

  switch(this->demux_strategy) {

  case DEMUX_DEFAULT_STRATEGY:
    if(try_demux_with_stages(this, MRL, STAGE_BY_CONTENT, STAGE_BY_EXTENSION))
      return 1;
    break;

  case DEMUX_REVERT_STRATEGY:
    if(try_demux_with_stages(this, MRL, STAGE_BY_EXTENSION, STAGE_BY_CONTENT))
      return 1;
    break;

  case DEMUX_CONTENT_STRATEGY:
    if(try_demux_with_stages(this, MRL, STAGE_BY_CONTENT, -1))
      return 1;
    break;

  case DEMUX_EXTENSION_STRATEGY:
    if(try_demux_with_stages(this, MRL, STAGE_BY_EXTENSION, -1))
      return 1;
    break;

  }
  
  return 0;
}

int xine_play_internal (xine_t *this, char *mrl, 
	       int start_pos, int start_time) {

  double     share ;
  off_t      pos, len;
  int        i;
  int        demux_status;

  printf ("xine_play: xine open %s, start pos = %d, start time = %d (sec)\n", 
	  mrl, start_pos, start_time);

  xine_set_speed_internal (this, SPEED_NORMAL);
  
  /*
   * stop engine only for different mrl
   */

  if ((this->status == XINE_PLAY && strcmp (mrl, this->cur_mrl)) || (this->status == XINE_LOGO)) {
    
    if(this->cur_demuxer_plugin) {
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

    this->status = XINE_STOP;
  }

  if (this->status == XINE_STOP ) {
    /*
     * find input plugin
     */
    this->cur_input_plugin = NULL;

    for (i = 0; i < this->num_input_plugins; i++) {
      if (this->input_plugins[i]->open(this->input_plugins[i], mrl)) {
        this->cur_input_plugin = this->input_plugins[i];
        break;
      }
    }

    if (!this->cur_input_plugin) {
      xine_log (this, XINE_LOG_FORMAT,
	        _("xine: cannot find input plugin for this MRL\n"));
      this->cur_demuxer_plugin = NULL;
      this->err = XINE_ERROR_NO_INPUT_PLUGIN;

      return 0;
    }
  
    printf ("xine: using input plugin >%s< for this MRL (%s).\n", 
	    this->cur_input_plugin->get_identifier(this->cur_input_plugin), mrl);

    xine_log (this, XINE_LOG_FORMAT,
	      _("using input plugin '%s' for MRL '%s'\n"),
	      this->cur_input_plugin->get_identifier(this->cur_input_plugin), 
	      mrl);

    /*
     * find demuxer plugin
     */

    if (!find_demuxer(this, mrl)) {
      xine_log (this, XINE_LOG_FORMAT, 
	        _("xine: couldn't find demuxer for >%s<\n"), mrl);
      this->cur_input_plugin->close(this->cur_input_plugin);
      this->err = XINE_ERROR_NO_DEMUXER_PLUGIN;
      return 0;
    }

    xine_log (this, XINE_LOG_FORMAT,
	      _("system layer format '%s' detected.\n"),
	      this->cur_demuxer_plugin->get_identifier());
  }
    
  /*
   * start demuxer
   */

  if (start_pos) {
    /* FIXME: do we need to protect concurrent access to input plugin here? */
    len = this->cur_input_plugin->get_length (this->cur_input_plugin);
    share = (double) start_pos / 65535;
    pos = (off_t) (share * len) ;
  } else
    pos = 0;
  
  if( this->status == XINE_STOP ) {
    demux_status = this->cur_demuxer_plugin->start (this->cur_demuxer_plugin,
						    this->video_fifo,
						    this->audio_fifo, 
						    pos, start_time);
  }
  else {
    demux_status = this->cur_demuxer_plugin->seek (this->cur_demuxer_plugin,
						   pos, start_time);
  }
  if (demux_status != DEMUX_OK) {
    xine_log (this, XINE_LOG_MSG, 
	      _("xine_play: demuxer failed to start\n"));
    
    this->err = XINE_ERROR_DEMUXER_FAILED;
    
    if( this->status == XINE_STOP )      
      this->cur_input_plugin->close(this->cur_input_plugin);
  
    return 0;
    
  } else {

    this->status = XINE_PLAY;
    strncpy (this->cur_mrl, mrl, 1024);
    
    /* osd */
    xine_usec_sleep(100000); /* FIXME: how do we assure an updated cur_input_time? */
    xine_internal_osd (this, ">", this->metronom->get_current_time (this->metronom), 300000);

  }

  return 1;
}             

int xine_play (xine_t *this, char *mrl, 
	       int start_pos, int start_time) {
int ret;

  pthread_mutex_lock (&this->xine_lock);
  ret = xine_play_internal (this, mrl, start_pos, start_time);
  pthread_mutex_unlock (&this->xine_lock);
  
  return ret;
}

int xine_eject (xine_t *this) {
  
  int status;

  if(this->last_input_plugin == NULL) 
    return 0;
  
  pthread_mutex_lock (&this->xine_lock);

  status = 0;
  if (((this->status == XINE_STOP) || (this->status == XINE_LOGO))
      && this->last_input_plugin && this->last_input_plugin->eject_media) {

    status = this->last_input_plugin->eject_media (this->last_input_plugin);
  }

  pthread_mutex_unlock (&this->xine_lock);
  return status;
}

void xine_exit (xine_t *this) {

  int i;

  this->status = XINE_QUIT;

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

  this->status = XINE_QUIT;

  printf ("xine_exit: bye!\n");

  for (i = 0; i < XINE_LOG_NUM; i++)
    this->log_buffers[i]->dispose (this->log_buffers[i]);

  this->metronom->exit (this->metronom);

  for (i = 0; i < this->num_demuxer_plugins; i++)
    this->demuxer_plugins[i]->close (this->demuxer_plugins[i]);

  for (i = 0; i < this->num_input_plugins; i++)
    this->input_plugins[i]->dispose (this->input_plugins[i]);

  for (i = 0; i < this->num_audio_decoders_loaded; i++) 
    this->audio_decoders_loaded[i]->dispose (this->audio_decoders_loaded[i]);

  for (i = 0; i < this->num_video_decoders_loaded; i++) 
    this->video_decoders_loaded[i]->dispose (this->video_decoders_loaded[i]);

  for (i = 0; i < this->num_spu_decoders_loaded; i++) 
    this->spu_decoders_loaded[i]->dispose (this->spu_decoders_loaded[i]);

  xine_profiler_print_results ();

  pthread_mutex_destroy (&this->logo_lock);
  pthread_mutex_destroy (&this->xine_lock);
  pthread_mutex_destroy (&this->finished_lock);

  free (this);

}

xine_t *xine_init (vo_driver_t *vo, 
		   ao_driver_t *ao,
		   config_values_t *config) {

  xine_t      *this = xine_xmalloc (sizeof (xine_t));
  static char *demux_strategies[] = {"default", "reverse", "content",
				     "extension", NULL};
  int          i;

  /* setting default logo mrl */
  pthread_mutex_init (&this->logo_lock, NULL);

  pthread_mutex_lock (&this->logo_lock);
  this->logo_mrl = config->register_string(config, "misc.logo_mrl", XINE_LOGO_FILE,
					   "logo mrl, displayed in video output window",
					   NULL, _logo_change_cb, (void *) this);
  pthread_mutex_unlock (&this->logo_lock);

  this->video_driver = vo;

  /* init log buffers */
  for (i = 0; i < XINE_LOG_NUM; i++)
    this->log_buffers[i] = new_scratch_buffer (25);
  
#ifdef ENABLE_NLS
  bindtextdomain("xine-lib", XINE_LOCALEDIR);
#endif 

  printf ("xine: xine_init entered\n");
  
  this->err     = XINE_ERROR_NONE;
  this->config  = config;

  /* probe for optimized memcpy or config setting */
  xine_probe_fast_memcpy(config);
  
  /*
   * init locks
   */

  pthread_mutex_init (&this->xine_lock, NULL);

  pthread_mutex_init (&this->finished_lock, NULL);

  this->finished_thread_running = 0;

  /*
   * init event listeners
   */
  this->num_event_listeners = 0; /* Initially there are none */
  this->cur_input_plugin = NULL; /* In case the input plugin event handlers
                                  * are called too early. */
  this->cur_spu_decoder_plugin = NULL;
  this->report_codec_cb = NULL; 
  
  /*
   * create a metronom
   */

  this->metronom = metronom_init (ao != NULL, (void *)this);

  /*
   * load input and demuxer plugins
   */

  load_input_plugins (this, config);

  this->demux_strategy  = config->register_enum (config, "misc.demux_strategy", 0,
						 demux_strategies, "demuxer selection strategy",
						 NULL, NULL, NULL);

  load_demux_plugins(this, config);

  this->spu_channel_auto   = -1;
  this->spu_channel_user   = -1;
  this->cur_input_pos      = 0;
  this->cur_input_length   = 0;
  this->last_input_plugin  = NULL;

  /*
   * init and start decoder threads
   */

  load_decoder_plugins (this, config);

  this->video_out = vo_new_instance (vo, this);
  video_decoder_init (this);

  this->osd_renderer = osd_renderer_init (this->video_out->get_overlay_instance (this->video_out), config );
  
  this->osd = this->osd_renderer->new_object (this->osd_renderer, 300, 100);
  this->osd_renderer->set_font (this->osd, "cetus", 24);
  this->osd_renderer->set_text_palette (this->osd, TEXTPALETTE_WHITE_BLACK_TRANSPARENT, OSD_TEXT1 );
  this->osd_renderer->set_position (this->osd, 10,10);

  this->osd_display = config->register_bool(config, "misc.osd_display", 1,
                                            "Show status on play, pause, ff, ...", NULL,
                                            update_osd_display, this );
  
  if(ao) 
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

  pthread_mutex_lock (&this->logo_lock);
  xine_play(this, this->logo_mrl,0,0);
  this->status = XINE_LOGO;
  pthread_mutex_unlock (&this->logo_lock);

  return this;
}

int xine_get_spu_channel (xine_t *this) {

  return this->spu_channel_user;
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

int xine_get_current_position (xine_t *this) {

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

int xine_get_status(xine_t *this) {

  return this->status;
}

/* ***
 * Version information/check
 */

/*
 * Return version in string, like "0.5.0"
 */
char *xine_get_str_version(void) {
  return VERSION;
}

/*
 * Return major version
 */
int xine_get_major_version(void) {
  return XINE_MAJOR;
}

/*
 * Return minor version
 */
int xine_get_minor_version(void) {
  return XINE_MINOR;
}

/*
 * Return sub version
 */
int xine_get_sub_version(void) {
  return XINE_SUB;
}

/*
 * Check if xine version is <= to specifier version.
 */
int xine_check_version(int major, int minor, int sub) {
  
  if((XINE_MAJOR > major) || 
     ((XINE_MAJOR == major) && (XINE_MINOR > minor)) || 
     ((XINE_MAJOR == major) && (XINE_MINOR == minor) && (XINE_SUB >= sub)))
    return 1;
  
  return 0;
}

/*
 * manually adjust a/v sync 
 */

void xine_set_av_offset (xine_t *this, int offset_pts) {
  this->metronom->set_option (this->metronom, METRONOM_AV_OFFSET, offset_pts);
}

int xine_get_av_offset (xine_t *this) {
  return this->metronom->get_option (this->metronom, METRONOM_AV_OFFSET);
}

/*
 * trick play 
 */

void xine_set_speed (xine_t *this, int speed) {

  pthread_mutex_lock (&this->xine_lock);

  if (speed <= SPEED_PAUSE) 
    speed = SPEED_PAUSE;
  else if (speed > SPEED_FAST_4) 
    speed = SPEED_FAST_4;

  /* osd */

  switch (speed) {
  case SPEED_PAUSE:
    xine_internal_osd (this, "<", this->metronom->get_current_time (this->metronom), 10000);
    break;
  case SPEED_SLOW_4:
    xine_internal_osd (this, "<>", this->metronom->get_current_time (this->metronom), 20000 * speed);
    break;
  case SPEED_SLOW_2:
    xine_internal_osd (this, "@>", this->metronom->get_current_time (this->metronom), 20000 * speed);
    break;
  case SPEED_NORMAL:
    xine_internal_osd (this, ">", this->metronom->get_current_time (this->metronom), 20000 * speed);
    break;
  case SPEED_FAST_2:
    xine_internal_osd (this, "$$", this->metronom->get_current_time (this->metronom), 20000 * speed);
    break;
  case SPEED_FAST_4:
    xine_internal_osd (this, "$$$", this->metronom->get_current_time (this->metronom), 20000 * speed);
    break;
  } 

  /* make sure osd can be displayed */
  xine_usec_sleep(100000);

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

int xine_get_current_time (xine_t *this) {
  return this->cur_input_time;
}

int xine_get_stream_length (xine_t *this) {

  if(this->cur_demuxer_plugin)
    return this->cur_demuxer_plugin->get_stream_length (this->cur_demuxer_plugin);

  return 0;
}

int xine_get_audio_capabilities(xine_t *this) {

  if(this->audio_out)
    return (this->audio_out->get_capabilities(this->audio_out));

  return AO_CAP_NOCAP;
}

int xine_get_audio_property(xine_t *this, int property) {
  
  if(this->audio_out)
    return(this->audio_out->get_property(this->audio_out, property));

  return 0;
}

int xine_set_audio_property(xine_t *this, int property, int value) {

  if(this->audio_out)
    return(this->audio_out->set_property(this->audio_out, property, value));
  
  return ~value;
}

int xine_get_current_frame (xine_t *this, int *width, int *height,
			    int *ratio_code, int *format,
			    uint8_t **y, uint8_t **u, uint8_t **v) {

  vo_frame_t *frame;

  frame = this->video_out->get_last_frame (this->video_out);

  if (!frame)
    return 0;

  *width = frame->width;
  *height = frame->height;

  *ratio_code = frame->ratio;
  *format = frame->format;

  *y = frame->base[0];
  *u = frame->base[1];
  *v = frame->base[2];

  return 1;
}

void xine_get_spu_lang (xine_t *this, char *str) {

  switch (this->spu_channel_user) {
  case -2:
    sprintf (str, "off");
    break;
  case -1:
    if (this->cur_input_plugin) {
      if (this->cur_input_plugin->get_capabilities (this->cur_input_plugin) & INPUT_CAP_SPULANG) {
	this->cur_input_plugin->get_optional_data (this->cur_input_plugin, this->str, 
						   INPUT_OPTIONAL_DATA_SPULANG);
	sprintf (str, "*(%s)", this->str);
	return;
      }
    } 
    if (this->spu_channel_auto == -1)
      sprintf (str, "*(off)");
    else
      sprintf (str, "*(%3d)", this->spu_channel_auto);
    break;
  default:
    sprintf (str, "%3d", this->spu_channel_user);
  }

}

void xine_get_audio_lang (xine_t *this, char *str) {

  switch (this->audio_channel_user) {
  case -2:
    sprintf (str, "off");
    break;
  case -1:
    if (this->cur_input_plugin) {
      if (this->cur_input_plugin->get_capabilities (this->cur_input_plugin) & INPUT_CAP_AUDIOLANG) {
	this->cur_input_plugin->get_optional_data (this->cur_input_plugin, this->str, 
						   INPUT_OPTIONAL_DATA_AUDIOLANG);

	sprintf (str, "*(%s)", this->str);

	return;
      }
    } 
    if (this->audio_channel_auto == -1)
      sprintf (str, "*(off)");
    else
      sprintf (str, "*(%3d)", this->audio_channel_auto);
    break;
  default:
    sprintf (str, "%3d", this->audio_channel_user);
  }
}

int xine_is_stream_seekable (xine_t *this) {

  if (this->cur_input_plugin)
    return this->cur_input_plugin->get_capabilities (this->cur_input_plugin) & INPUT_CAP_SEEKABLE;

  return -1;
}

osd_renderer_t *xine_get_osd_renderer (xine_t *this) {

  return this->osd_renderer;
}

/*
 * log functions
 */
int xine_get_log_section_count (xine_t *this) {
  return XINE_LOG_NUM;
}

char **xine_get_log_names (xine_t *this) {
  static char *log_sections[XINE_LOG_NUM + 1];

  log_sections[XINE_LOG_FORMAT]   = _("stream format");
  log_sections[XINE_LOG_MSG]      = _("messages");  
  log_sections[XINE_LOG_PLUGIN]   = _("plugin");
  log_sections[XINE_LOG_NUM]      = NULL;
  
  return log_sections;
}

void xine_log (xine_t *this, int buf, const char *format, ...) {

  va_list argp;

  va_start (argp, format);

  this->log_buffers[buf]->scratch_printf (this->log_buffers[buf], format, argp);
  va_end (argp);

  va_start (argp, format);

  vprintf (format, argp);

  va_end (argp);
}

char **xine_get_log (xine_t *this, int buf) {
  
  if(buf >= XINE_LOG_NUM)
    return NULL;
  
  return this->log_buffers[buf]->get_content (this->log_buffers[buf]);
}

int xine_get_error (xine_t *this) {
  return this->err;
}
