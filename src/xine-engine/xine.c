/*
 * Copyright (C) 2000-2001 the xine project
 * 
 * This file is part of xine, a unix video player.
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
 * $Id: xine.c,v 1.103 2002/02/09 07:13:24 guenter Exp $
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

#ifdef __GNUC__
#define LOG_MSG_STDERR(xine, message, args...) {                     \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                   \
    fprintf(stderr, message, ##args);                                \
  }
#define LOG_MSG(xine, message, args...) {                            \
    xine_log(xine, XINE_LOG_MSG, message, ##args);                   \
    printf(message, ##args);                                         \
  }
#else
#define LOG_MSG_STDERR(xine, ...) {                                  \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                       \
    fprintf(stderr, __VA_ARGS__);                                    \
  }
#define LOG_MSG(xine, ...) {                                         \
    xine_log(xine, XINE_LOG_MSG, __VA_ARGS__);                       \
    printf(__VA_ARGS__);                                             \
  }
#endif

void * xine_notify_stream_finished_thread (void * this_gen) {
  xine_t *this = this_gen;
  xine_event_t event;

  xine_stop_internal (this);

  event.type = XINE_EVENT_PLAYBACK_FINISHED;

  xine_send_event (this, &event);

  return NULL;
}

void xine_notify_stream_finished (xine_t *this) {
  pthread_t finished_thread;
  int err;

  /* This thread will just execute xine_stop and (possibly) xine_play then die.
     It might look useless but i need to detach this code from the current
     thread to make sure that video_decoder and audio_decoder are running and
     freeing buffers. Free buffers might be needed by the main thread during
     a xine_play, for example.

     This is not a theorical situation: i was able to trigger it with simple
     user actions (play,seek,etc). [MF]
  */
  if ((err = pthread_create (&finished_thread,
			     NULL, xine_notify_stream_finished_thread, this)) != 0) {
    LOG_MSG_STDERR(this, _("xine_notify_stream_finished: can't create new thread (%s)\n"),
		   strerror(err));
    exit (1);
  }
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
    
    this->osd_renderer->render_text (this->osd, 70, 5, tstr, OSD_TEXT1);
  
    this->osd_renderer->show (this->osd, start_time);
    this->osd_renderer->hide (this->osd, start_time+duration);
  }
}

static void update_osd_display(void *this_gen, cfg_entry_t *entry)
{
  xine_t *this = (xine_t *) this_gen;
  
  this->osd_display = entry->num_value;
}

void xine_stop_internal (xine_t *this) {

  pthread_mutex_lock (&this->xine_lock);

  LOG_MSG(this, _("xine_stop\n"));

  /* xine_internal_osd (this, "}", this->metronom->get_current_time (this->metronom), 30000); never works */

  if (this->status == XINE_STOP) {
    LOG_MSG(this, _("xine_stop ignored\n"));
    pthread_mutex_unlock (&this->xine_lock);
    return;
  }


  this->metronom->set_speed (this->metronom, SPEED_NORMAL);
  this->speed      = SPEED_NORMAL;

  if( this->audio_out )
    this->audio_out->audio_paused = 0;

  this->status = XINE_STOP;
  LOG_MSG(this, _("xine_stop: stopping demuxer\n"));

  if(this->cur_demuxer_plugin) {
    this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    this->cur_demuxer_plugin = NULL;
  }

  if(this->cur_input_plugin) {
    this->cur_input_plugin->close(this->cur_input_plugin);
    /*
     * If we set it to NULL, xine_eject() will not work after
     * a xine_stop() call.
     *
     * this->cur_input_plugin = NULL;
     */
  }

  LOG_MSG(this, _("xine_stop: done\n"));

  pthread_mutex_unlock (&this->xine_lock);
}

void xine_stop (xine_t *this) {
  xine_stop_internal(this);
  
  /*
     this will make output threads discard about everything
     am i abusing of xine architeture? :)
  */
  this->metronom->adjust_clock(this->metronom,
			       this->metronom->get_current_time(this->metronom) + 30 * 90000 );
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
    LOG_MSG_STDERR(this, _("%s(%d) wrong first stage = %d !!\n"), 
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

int xine_play (xine_t *this, char *mrl, 
		int start_pos, int start_time) {

  double     share ;
  off_t      pos, len;
  int        i;

  LOG_MSG(this, _("xine_play: xine open %s, start pos = %d, start time = %d (sec)\n"), 
	  mrl, start_pos, start_time);

  pthread_mutex_lock (&this->xine_lock);

  /*
   * stop engine?
   */

  if (this->status == XINE_PLAY) {
    
    if(this->cur_demuxer_plugin) {
      this->cur_demuxer_plugin->stop (this->cur_demuxer_plugin);
    }
    
    if(this->cur_input_plugin) {

      if (strcmp (mrl, this->cur_mrl)) 
	this->cur_input_plugin->close(this->cur_input_plugin);
      else
	this->cur_input_plugin->stop(this->cur_input_plugin);
    }

    this->status = XINE_STOP;
  }

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
    LOG_MSG(this, _("xine: cannot find input plugin for this MRL\n"));
    this->cur_demuxer_plugin = NULL;
    this->err = XINE_ERROR_NO_INPUT_PLUGIN;
    pthread_mutex_unlock (&this->xine_lock);

    return 0;
  }
  
  LOG_MSG(this, _("xine: using input plugin >%s< for this MRL (%s).\n"), 
	  this->cur_input_plugin->get_identifier(this->cur_input_plugin), mrl);

  /*
   * find demuxer plugin
   */

  if (!find_demuxer(this, mrl)) {
    LOG_MSG(this, _("xine: couldn't find demuxer for >%s<\n"), mrl);
    this->err = XINE_ERROR_NO_DEMUXER_PLUGIN;
    pthread_mutex_unlock (&this->xine_lock);
    return 0;
  }

  LOG_MSG(this, _("xine: using demuxer plugin >%s< for this MRL.\n"),
	  this->cur_demuxer_plugin->get_identifier());
  
  /*
   * start demuxer
   */

  if (start_pos) {
    len = this->cur_input_plugin->get_length (this->cur_input_plugin);
    share = (double) start_pos / 65535;
    pos = (off_t) (share * len) ;
  } else
    pos = 0;

  this->cur_demuxer_plugin->start (this->cur_demuxer_plugin,
				   this->video_fifo,
				   this->audio_fifo, 
				   pos, start_time);
  
  if (this->cur_demuxer_plugin->get_status(this->cur_demuxer_plugin) != DEMUX_OK) {
    LOG_MSG(this, _("xine_play: demuxer failed to start\n"));
    
    this->cur_input_plugin->close(this->cur_input_plugin);

    this->status = XINE_STOP;
  } else {

    this->status = XINE_PLAY;
    strncpy (this->cur_mrl, mrl, 1024);
    
    this->metronom->set_speed (this->metronom, SPEED_NORMAL);

    if( this->audio_out )
      this->audio_out->audio_paused = 0;
    this->speed = SPEED_NORMAL;

    /* osd */
    xine_internal_osd (this, ">", 0, 300000);

  }

  pthread_mutex_unlock (&this->xine_lock);

  return 1;
}             

int xine_eject (xine_t *this) {
  
  if(this->cur_input_plugin == NULL) 
    return 0;
  
  pthread_mutex_lock (&this->xine_lock);

  if ((this->status == XINE_STOP)
      && this->cur_input_plugin && this->cur_input_plugin->eject_media) {

    pthread_mutex_unlock (&this->xine_lock);

    return this->cur_input_plugin->eject_media (this->cur_input_plugin);
  }

  pthread_mutex_unlock (&this->xine_lock);
  return 0;
}

void xine_exit (xine_t *this) {

  xine_stop(this);
    
  LOG_MSG(this, _("xine_exit: shutdown audio\n"));

  audio_decoder_shutdown (this);

  LOG_MSG(this, _("xine_exit: shutdown video\n"));

  video_decoder_shutdown (this);

  this->status = XINE_QUIT;

  LOG_MSG(this, _("xine_exit: bye!\n"));

  xine_profiler_print_results ();

}

xine_t *xine_init (vo_driver_t *vo, 
		   ao_driver_t *ao,
		   config_values_t *config) {

  xine_t      *this = xine_xmalloc (sizeof (xine_t));
  static char *demux_strategies[] = {"default", "reverse", "content",
				     "extension", NULL};
  int          i;

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
  
  /* initialize aligned mem allocator */
  xine_init_mem_aligned();

  /*
   * init locks
   */

  pthread_mutex_init (&this->xine_lock, NULL);

  pthread_mutex_init (&this->finished_lock, NULL);

  /*
   * init event listeners
   */
  this->num_event_listeners = 0; /* Initially there are none */
  this->cur_input_plugin = NULL; /* In case the input plugin event handlers
                                  * are called too early. */
  this->cur_spu_decoder_plugin = NULL; 
  
  /*
   * create a metronom
   */

  this->metronom = metronom_init (ao != NULL, (void *)this);

  /*
   * load input and demuxer plugins
   */

  load_input_plugins (this, config, INPUT_PLUGIN_IFACE_VERSION);

  this->demux_strategy  = config->register_enum (config, "misc.demux_strategy", 0,
						 demux_strategies, "demuxer selection strategy",
						 NULL, NULL, NULL);

  load_demux_plugins(this, config, DEMUXER_PLUGIN_IFACE_VERSION);

  this->spu_channel_auto   = -1;
  this->spu_channel_user   = -1;
  this->cur_input_pos      = 0;

  /*
   * init and start decoder threads
   */

  load_decoder_plugins (this, config, DECODER_PLUGIN_IFACE_VERSION);

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
    this->audio_out = ao_new_instance (ao, this->metronom, config);

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
    LOG_MSG(this, _("xine: xine_get_current_position: no input source\n"));
    pthread_mutex_unlock (&this->xine_lock);
    return 0;
  }
  
  /* pos = this->mCurInput->seek (0, SEEK_CUR); */
  len = this->cur_input_plugin->get_length (this->cur_input_plugin);

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
  this->metronom->set_av_offset (this->metronom, offset_pts);
}

int xine_get_av_offset (xine_t *this) {
  return this->metronom->get_av_offset (this->metronom);
}

/*
 * trick play 
 */

void xine_set_speed (xine_t *this, int speed) {

  struct timespec tenth_sec;

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
  tenth_sec.tv_sec = 0;
  tenth_sec.tv_nsec = 100000000;

  nanosleep (&tenth_sec, NULL);

  LOG_MSG(this, _("xine: set_speed %d\n"), speed);

  this->metronom->set_speed (this->metronom, speed);

  /* see coment on audio_out loop about audio_paused */
  if( this->audio_out )
    this->audio_out->audio_paused = (speed != SPEED_NORMAL) + 
                                    (speed == SPEED_PAUSE);

  this->speed      = speed;

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
unsigned int xine_get_log_section_count(void) {
  return XINE_LOG_NUM;
}

const char **xine_get_log_names(void) {
  static const char *log_sections[XINE_LOG_NUM + 1];

  log_sections[XINE_LOG_MSG]      = _("messages");  /* XINE_LOG_MSG      */
  log_sections[XINE_LOG_INPUT]    = _("inputs");    /* XINE_LOG_INPUT    */
  log_sections[XINE_LOG_DEMUX]    = _("demuxers");  /* XINE_LOG_DEMUX    */
  log_sections[XINE_LOG_CODEC]    = _("codecs");    /* XINE_LOG_CODEC    */
  log_sections[XINE_LOG_VIDEO]    = _("video");     /* XINE_LOG_VIDEO    */
  log_sections[XINE_LOG_METRONOM] = _("metronom");  /* XINE_LOG_METRONOM */
  log_sections[XINE_LOG_PLUGIN]   = _("plugin");    /* XINE_LOG_PLUGIN   */
  log_sections[XINE_LOG_NUM]      = NULL;
  
  return log_sections;
}

void xine_log (xine_t *this, int buf, const char *format, ...) {

  va_list argp;

  va_start (argp, format);

  this->log_buffers[buf]->scratch_printf (this->log_buffers[buf], format, argp);

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
