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
 * $Id: xine_interface.c,v 1.2 2002/09/04 23:31:13 guenter Exp $
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

char* xine_config_register_string (xine_t *self,
				   char *key,
				   char *def_value,
				   char *description,
				   char *help,
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
  
int xine_config_register_range (xine_t *self,
				char *key,
				int def_value,
				int min, int max,
				char *description,
				char *help,
				int   exp_level,
				xine_config_cb_t changed_cb,
				void *cb_data) {
  return self->config->register_range (self->config,
				       key, def_value, min, max,
				       description, help, exp_level,
				       changed_cb, cb_data);
}
  

int xine_config_register_enum (xine_t *self,
			       char *key,
			       int def_value,
			       char **values,
			       char *description,
			       char *help,
			       int   exp_level,
			       xine_config_cb_t changed_cb,
			       void *cb_data) {
  return self->config->register_enum (self->config,
				      key, def_value, values,
				      description, help, exp_level,
				      changed_cb, cb_data);
}
  

int xine_config_register_num (xine_t *self,
			      char *key,
			      int def_value,
			      char *description,
			      char *help,
			      int   exp_level,
			      xine_config_cb_t changed_cb,
			      void *cb_data) {
  return self->config->register_num (self->config,
				     key, def_value, 
				     description, help, exp_level,
				     changed_cb, cb_data);
}
  

int xine_config_register_bool (xine_t *self,
			       char *key,
			       int def_value,
			       char *description,
			       char *help,
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
 * copy current config entry data to public struct
 * and return it
 */

xine_cfg_entry_t *xine_config_get_current_entry (xine_t *this) {

  config_values_t *config = this->config;

  if (!config->cur)
    return NULL;

  config->public_entry.key            = config->cur->key;
  config->public_entry.type           = config->cur->type;

  config->public_entry.unknown_value  = config->cur->unknown_value;
  config->public_entry.str_value      = config->cur->str_value;
  config->public_entry.str_default    = config->cur->str_default;
  config->public_entry.str_sticky     = config->cur->str_sticky;
  config->public_entry.num_value      = config->cur->num_value;
  config->public_entry.num_default    = config->cur->num_default;
  config->public_entry.range_min      = config->cur->range_min;
  config->public_entry.range_max      = config->cur->range_max;
  config->public_entry.enum_values    = config->cur->enum_values;

  config->public_entry.description    = config->cur->description;
  config->public_entry.help           = config->cur->help;
  config->public_entry.callback       = config->cur->callback;
  config->public_entry.callback_data  = config->cur->callback_data;
  config->public_entry.exp_level      = config->cur->exp_level;

  return &config->public_entry;
}

/*
 * get first config item 
 */
xine_cfg_entry_t *xine_config_get_first_entry (xine_t *this) {

  config_values_t *config = this->config;

  config->cur = config->first;

  return xine_config_get_current_entry (this);
}
  

/*
 * get next config item (iterate through the items)
 * this will return NULL when called after returning the last item
 */     
xine_cfg_entry_t *xine_config_get_next_entry (xine_t *this) {

  config_values_t *config = this->config;

  config->cur = config->cur->next;

  return xine_config_get_current_entry (this);
} 
  

/*
 * search for a config entry by key 
 */

xine_cfg_entry_t *xine_config_lookup_entry (xine_t *this, char *key) {

  config_values_t *config = this->config;

  config->cur = config->lookup_entry (config, key);
  
  return xine_config_get_current_entry (this);
}
  

/*
 * update a config entry (which was returned from lookup_entry() )
 */
void xine_config_update_entry (xine_t *this, xine_cfg_entry_t *entry){
  printf ("xine_interface: xine_config_update_entry: not implemented\n");
  abort();
}
  

void xine_reset_config (xine_t *this){
  printf ("xine_interface: xine_reset_config: not implemented\n");
  abort();
}
  
int xine_gui_send_vo_data (xine_t *this,
			   int type, void *data) {

  return this->video_driver->gui_data_exchange (this->video_driver, type, data);
}

void xine_set_param (xine_t *this, int param, int value) {

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
  }
}

int  xine_get_param (xine_t *this, int param) {

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

  default:
    printf ("xine_interface: unknown param %d\n", param);
    abort ();
  }

  return 0;
}

uint32_t xine_get_stream_info (xine_t *self, int info) {
  printf ("xine_interface: xine_get_stream_info: not implemented\n");
  abort();
}
