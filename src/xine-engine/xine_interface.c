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
 * $Id: xine_interface.c,v 1.13 2002/09/16 15:09:36 jcdutton Exp $
 *
 * convenience/abstraction layer, functions to implement
 * libxine's public interface
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

/* 
 * version information / checking
 */

char *xine_get_version_string(void) {
  return VERSION;
}

void xine_get_version (int *major, int *minor, int *sub) {
  *major = XINE_MAJOR;
  *minor = XINE_MINOR;
  *sub   = XINE_SUB;
}

int xine_check_version(int major, int minor, int sub) {
  
  if((XINE_MAJOR > major) || 
     ((XINE_MAJOR == major) && (XINE_MINOR > minor)) || 
     ((XINE_MAJOR == major) && (XINE_MINOR == minor) && (XINE_SUB >= sub)))
    return 1;
  
  return 0;
}

/*
 * public config object access functions
 */

const char* xine_config_register_string (xine_p self,
					 const char *key,
					 const char *def_value,
					 const char *description,
					 const char *help,
					 int   exp_level,
					 xine_config_cb_t changed_cb,
					 void *cb_data) {
  
  return self->config->register_string (self->config,
					key,
					def_value,
					description,
					help,
					exp_level,
					changed_cb,
					cb_data);

}
  
int xine_config_register_range (xine_p self,
				const char *key,
				int def_value,
				int min, int max,
				const char *description,
				const char *help,
				int   exp_level,
				xine_config_cb_t changed_cb,
				void *cb_data) {
  return self->config->register_range (self->config,
				       key, def_value, min, max,
				       description, help, exp_level,
				       changed_cb, cb_data);
}
  

int xine_config_register_enum (xine_p self,
			       const char *key,
			       int def_value,
			       char **values,
			       const char *description,
			       const char *help,
			       int   exp_level,
			       xine_config_cb_t changed_cb,
			       void *cb_data) {
  return self->config->register_enum (self->config,
				      key, def_value, values,
				      description, help, exp_level,
				      changed_cb, cb_data);
}
  

int xine_config_register_num (xine_p self,
			      const char *key,
			      int def_value,
			      const char *description,
			      const char *help,
			      int   exp_level,
			      xine_config_cb_t changed_cb,
			      void *cb_data) {
  return self->config->register_num (self->config,
				     key, def_value, 
				     description, help, exp_level,
				     changed_cb, cb_data);
}


int xine_config_register_bool (xine_p self,
			       const char *key,
			       int def_value,
			       const char *description,
			       const char *help,
			       int   exp_level,
			       xine_config_cb_t changed_cb,
			       void *cb_data) {
  return self->config->register_bool (self->config,
				      key, def_value, 
				      description, help, exp_level,
				      changed_cb, cb_data);
}
  

/*
 * helper function:
 *
 * copy current config entry data to user-provided memory
 * and return status
 */

static int xine_config_get_current_entry (xine_p this, 
					  xine_cfg_entry_t *entry) {

  config_values_t *config = this->config;

  if (!config->cur)
    return 0;
/* Don't do strdup on const key, help, description */
  entry->key            = config->cur->key;
  entry->type           = config->cur->type;
  if(entry->unknown_value) {
    free(entry->unknown_value);
    entry->unknown_value=NULL;
  }
  if(config->cur->unknown_value)
    entry->unknown_value            = strdup(config->cur->unknown_value);

  if(entry->str_value) {
    free(entry->str_value);
    entry->str_value=NULL;
  }
  if(config->cur->str_value)
    entry->str_value            = strdup(config->cur->str_value);
  if(entry->str_default) {
    free(entry->str_default);
    entry->str_default=NULL;
  }
  if(config->cur->str_default)
    entry->str_default            = strdup(config->cur->str_default);
  if(entry->str_sticky) {
    free(entry->str_sticky);
    entry->str_sticky=NULL;
  }
  if(config->cur->str_sticky)
    entry->str_sticky            = strdup(config->cur->str_sticky);
  entry->num_value      = config->cur->num_value;
  entry->num_default    = config->cur->num_default;
  entry->range_min      = config->cur->range_min;
  entry->range_max      = config->cur->range_max;
  entry->enum_values    = config->cur->enum_values;

  entry->description    = config->cur->description;
  entry->help           = config->cur->help;
  entry->callback       = config->cur->callback;
  entry->callback_data  = config->cur->callback_data;
  entry->exp_level      = config->cur->exp_level;

  return 1;
}

/*
 * get first config item 
 */
int  xine_config_get_first_entry (xine_p this, xine_cfg_entry_t *entry) {

  config_values_t *config = this->config;

  config->cur = config->first;

  return xine_config_get_current_entry (this, entry);
}
  

/*
 * get next config item (iterate through the items)
 * this will return NULL when called after returning the last item
 */     
int xine_config_get_next_entry (xine_p this, xine_cfg_entry_t *entry) {

  config_values_t *config = this->config;

  config->cur = config->cur->next;

  return xine_config_get_current_entry (this, entry);
} 
  

/*
 * search for a config entry by key 
 */

int xine_config_lookup_entry (xine_p this, const char *key,
			      xine_cfg_entry_t *entry) {

  config_values_t *config = this->config;

  config->cur = config->lookup_entry (config, key);
  
  return xine_config_get_current_entry (this, entry);
}
  

/*
 * update a config entry (which was returned from lookup_entry() )
 */
void xine_config_update_entry (xine_p this, xine_cfg_entry_t *entry) {

  switch (entry->type) {
  case XINE_CONFIG_TYPE_RANGE:
  case XINE_CONFIG_TYPE_ENUM:
  case XINE_CONFIG_TYPE_NUM:
  case XINE_CONFIG_TYPE_BOOL:
    this->config->update_num (this->config, entry->key, entry->num_value);
    break;

  case XINE_CONFIG_TYPE_STRING:
    this->config->update_string (this->config, entry->key, entry->str_value);
    break;

  default:
    printf ("xine_interface: error, unknown config entry type %d\n",
	    entry->type);
    abort();
  }
}
  

void xine_reset_config (xine_p this) {

  config_values_t *config = this->config;
  cfg_entry_t *entry;

  config->cur = NULL;

  entry = config->first;
  while (entry) {
    cfg_entry_t *next;
    next = entry->next;
    free (entry);
    entry = next;
  }

  config->first = NULL;
  config->last = NULL;
}
  
int xine_gui_send_vo_data (xine_p this,
			   int type, void *data) {

  return this->video_driver->gui_data_exchange (this->video_driver, type, data);
}

void xine_set_param (xine_p this_ro, int param, int value) {
  xine_t *this = (xine_t *)this_ro;

  switch (param) {
  case XINE_PARAM_SPEED:
    xine_set_speed (this, value);
    break;

  case XINE_PARAM_AV_OFFSET:
    this->metronom->set_option (this->metronom, METRONOM_AV_OFFSET, value);
    break;

  case XINE_PARAM_AUDIO_CHANNEL_LOGICAL:
    pthread_mutex_lock (&this->xine_lock);
    if (value < -2)
      value = -2;
    this->audio_channel_user = value;
    pthread_mutex_unlock (&this->xine_lock);
    break;

  case XINE_PARAM_SPU_CHANNEL:
    xine_select_spu_channel (this, value);
    break;

  case XINE_PARAM_VIDEO_CHANNEL:
    pthread_mutex_lock (&this->xine_lock);
    if (value<0)
      value = 0;
    this->video_channel = value;
    pthread_mutex_unlock (&this->xine_lock);
    break;

  case XINE_PARAM_AUDIO_VOLUME:
    break; /* FIXME: implement */

  case XINE_PARAM_AUDIO_MUTE:
    break; /* FIXME: implement */
    
  case XINE_PARAM_VO_DEINTERLACE:
  case XINE_PARAM_VO_ASPECT_RATIO:
  case XINE_PARAM_VO_HUE:
  case XINE_PARAM_VO_SATURATION:
  case XINE_PARAM_VO_CONTRAST:
  case XINE_PARAM_VO_BRIGHTNESS:
  case XINE_PARAM_VO_ZOOM_X:
  case XINE_PARAM_VO_ZOOM_Y:
  case XINE_PARAM_VO_PAN_SCAN:
  case XINE_PARAM_VO_TVMODE:
    this->video_driver->set_property(this->video_driver, param & 0xffffff, value);
    break;
    
  default:
    printf ("xine_interface: unknown param %d\n", param);
  }
}

int  xine_get_param (xine_p this, int param) {

  switch (param) {
  case XINE_PARAM_SPEED:
    return this->speed;

  case XINE_PARAM_AV_OFFSET:
    return this->metronom->get_option (this->metronom, METRONOM_AV_OFFSET);

  case XINE_PARAM_AUDIO_CHANNEL_LOGICAL:
    return this->audio_channel_user;

  case XINE_PARAM_SPU_CHANNEL:
    return this->spu_channel_user;

  case XINE_PARAM_VIDEO_CHANNEL:
    return this->video_channel;

  case XINE_PARAM_AUDIO_VOLUME:
    return -1; /* FIXME: implement */

  case XINE_PARAM_AUDIO_MUTE:
    return -1; /* FIXME: implement */

  case XINE_PARAM_VO_DEINTERLACE:
  case XINE_PARAM_VO_ASPECT_RATIO:
  case XINE_PARAM_VO_HUE:
  case XINE_PARAM_VO_SATURATION:
  case XINE_PARAM_VO_CONTRAST:
  case XINE_PARAM_VO_BRIGHTNESS:
  case XINE_PARAM_VO_ZOOM_X:
  case XINE_PARAM_VO_ZOOM_Y:
  case XINE_PARAM_VO_PAN_SCAN:
  case XINE_PARAM_VO_TVMODE:
    return this->video_driver->get_property(this->video_driver, param & 0xffffff);
    break;
    
  default:
    printf ("xine_interface: unknown param %d\n", param);
  }

  return 0;
}

uint32_t xine_get_stream_info (xine_p this, int info) {

  switch (info) {

  case XINE_STREAM_INFO_SEEKABLE:
    if (this->cur_input_plugin)
      return this->cur_input_plugin->get_capabilities (this->cur_input_plugin) & INPUT_CAP_SEEKABLE;
    return 0;

  case XINE_STREAM_INFO_HAS_CHAPTERS:
    if (this->cur_input_plugin)
      return this->cur_input_plugin->get_capabilities (this->cur_input_plugin) & INPUT_CAP_CHAPTERS;
    return 0;

  case XINE_STREAM_INFO_WIDTH:
  case XINE_STREAM_INFO_HEIGHT:
  case XINE_STREAM_INFO_VIDEO_FOURCC:
  case XINE_STREAM_INFO_VIDEO_CHANNELS:
  case XINE_STREAM_INFO_VIDEO_STREAMS:
  case XINE_STREAM_INFO_AUDIO_FOURCC:
  case XINE_STREAM_INFO_AUDIO_CHANNELS:
  case XINE_STREAM_INFO_AUDIO_BITS:
  case XINE_STREAM_INFO_AUDIO_SAMPLERATE:
    return this->stream_info[info];

  default:
    printf ("xine_interface: error, unknown stream info (%d) requested\n",
	    info);
  }
  return 0;
}

const char *xine_get_meta_info (xine_p this, int info) {

  return this->meta_info[info];
}

