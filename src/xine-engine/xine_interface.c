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
 * $Id: xine_interface.c,v 1.50 2003/04/06 23:58:18 miguelfreitas Exp $
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
#include "post.h"

/* 
 * version information / checking
 */

const char *xine_get_version_string(void) {
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

const char* xine_config_register_string (xine_t *self,
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
  
int xine_config_register_range (xine_t *self,
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
  

int xine_config_register_enum (xine_t *self,
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
  

int xine_config_register_num (xine_t *self,
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


int xine_config_register_bool (xine_t *self,
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

static int xine_config_get_current_entry (xine_t *this, 
					  xine_cfg_entry_t *entry) {

  config_values_t *config = this->config;

  if (!config->cur)
    return 0;

  entry->key            = config->cur->key;
  entry->type           = config->cur->type;
  entry->str_value      = config->cur->str_value;
  entry->str_default    = config->cur->str_default;
  entry->str_sticky     = config->cur->str_sticky;
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
int  xine_config_get_first_entry (xine_t *this, xine_cfg_entry_t *entry) {
  int result;
  config_values_t *config = this->config;

  pthread_mutex_lock(&config->config_lock);
  config->cur = config->first;

  /* do not hand out unclaimed entries */
  while (config->cur && config->cur->type == CONFIG_TYPE_UNKNOWN)
    config->cur = config->cur->next;
  result = xine_config_get_current_entry (this, entry);
  pthread_mutex_unlock(&config->config_lock);

  return result;
}
  

/*
 * get next config item (iterate through the items)
 * this will return NULL when called after returning the last item
 */     
int xine_config_get_next_entry (xine_t *this, xine_cfg_entry_t *entry) {
  int result;
  config_values_t *config = this->config;

  pthread_mutex_lock(&config->config_lock);

  if (!config->cur) {
    pthread_mutex_unlock(&config->config_lock);
    return (xine_config_get_first_entry(this, entry));
  }
  
  /* do not hand out unclaimed entries */
  do {
    config->cur = config->cur->next;
  } while (config->cur && config->cur->type == CONFIG_TYPE_UNKNOWN);
  result = xine_config_get_current_entry (this, entry);
  pthread_mutex_unlock(&config->config_lock);

  return result;
} 
  

/*
 * search for a config entry by key 
 */

int xine_config_lookup_entry (xine_t *this, const char *key,
			      xine_cfg_entry_t *entry) {
  int result;
  config_values_t *config = this->config;

  pthread_mutex_lock(&config->config_lock);
  config->cur = config->lookup_entry (config, key);
  /* do not hand out unclaimed entries */
  if (config->cur && config->cur->type == CONFIG_TYPE_UNKNOWN)
    config->cur = NULL;
  result = xine_config_get_current_entry (this, entry);
  pthread_mutex_unlock(&config->config_lock);

  return result;
}
  

/*
 * update a config entry (which was returned from lookup_entry() )
 */
void xine_config_update_entry (xine_t *this, const xine_cfg_entry_t *entry) {

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
  

void xine_config_reset (xine_t *this) {

  config_values_t *config = this->config;
  cfg_entry_t *entry;

  pthread_mutex_lock(&config->config_lock);
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
  pthread_mutex_unlock(&config->config_lock);
}
  
int xine_gui_send_vo_data (xine_stream_t *stream,
			   int type, void *data) {

  return stream->video_driver->gui_data_exchange (stream->video_driver, 
						  type, data);
}

int xine_port_send_gui_data (xine_video_port_t *vo,
			   int type, void *data) {

  return vo->driver->gui_data_exchange (vo->driver, 
						  type, data);
}

void xine_set_param (xine_stream_t *stream, int param, int value) {

  switch (param) {
  case XINE_PARAM_SPEED:
    xine_set_speed (stream, value);
    break;

  case XINE_PARAM_AV_OFFSET:
    stream->metronom->set_option (stream->metronom, METRONOM_AV_OFFSET, value);
    break;
  
  case XINE_PARAM_SPU_OFFSET:
    stream->metronom->set_option (stream->metronom, METRONOM_SPU_OFFSET, value);
    break;

  case XINE_PARAM_AUDIO_CHANNEL_LOGICAL:
    pthread_mutex_lock (&stream->frontend_lock);
    if (value < -2)
      value = -2;
    stream->audio_channel_user = value;
    pthread_mutex_unlock (&stream->frontend_lock);
    break;

  case XINE_PARAM_SPU_CHANNEL:
    xine_select_spu_channel (stream, value);
    break;

  case XINE_PARAM_VIDEO_CHANNEL:
    pthread_mutex_lock (&stream->frontend_lock);
    if (value<0)
      value = 0;
    stream->video_channel = value;
    pthread_mutex_unlock (&stream->frontend_lock);
    break;

  case XINE_PARAM_AUDIO_VOLUME:
    if (stream->audio_out)
      stream->audio_out->set_property (stream->audio_out, AO_PROP_MIXER_VOL, value);
    break;

  case XINE_PARAM_AUDIO_MUTE:
    if (stream->audio_out)
      stream->audio_out->set_property (stream->audio_out, AO_PROP_MUTE_VOL, value);
    break;
    
  case XINE_PARAM_AUDIO_COMPR_LEVEL:
    if (stream->audio_out)
      stream->audio_out->set_property (stream->audio_out, AO_PROP_COMPRESSOR, value);
    break;
    
  case XINE_PARAM_AUDIO_AMP_LEVEL:
    if (stream->audio_out)
      stream->audio_out->set_property (stream->audio_out, AO_PROP_AMP, value);
    break;
    
  case XINE_PARAM_VERBOSITY:
    stream->xine->verbosity = value;
    
  case XINE_PARAM_VO_HUE:
  case XINE_PARAM_VO_SATURATION:
  case XINE_PARAM_VO_CONTRAST:
  case XINE_PARAM_VO_BRIGHTNESS:
  case XINE_PARAM_VO_DEINTERLACE:
  case XINE_PARAM_VO_ASPECT_RATIO:
  case XINE_PARAM_VO_ZOOM_X:
  case XINE_PARAM_VO_ZOOM_Y:
  case XINE_PARAM_VO_PAN_SCAN:
  case XINE_PARAM_VO_TVMODE:
    stream->video_out->set_property(stream->video_out, param, value);
    break;

  case XINE_PARAM_IGNORE_VIDEO:
    stream->stream_info[XINE_STREAM_INFO_IGNORE_VIDEO] = value;
    break;
    
  case XINE_PARAM_IGNORE_AUDIO:
    stream->stream_info[XINE_STREAM_INFO_IGNORE_AUDIO] = value;
    break;
  
  case XINE_PARAM_IGNORE_SPU:
    stream->stream_info[XINE_STREAM_INFO_IGNORE_SPU] = value;
    break;
  
  case XINE_PARAM_METRONOM_PREBUFFER:
    stream->metronom_prebuffer = value;
    break;

  default:
    printf ("xine_interface: unknown param %d\n", param);
  }
}

int  xine_get_param (xine_stream_t *stream, int param) {

  switch (param) {
  case XINE_PARAM_SPEED:
    return stream->xine->clock->speed;

  case XINE_PARAM_AV_OFFSET:
    return stream->metronom->get_option (stream->metronom, METRONOM_AV_OFFSET);
  
  case XINE_PARAM_SPU_OFFSET:
    return stream->metronom->get_option (stream->metronom, METRONOM_SPU_OFFSET);

  case XINE_PARAM_AUDIO_CHANNEL_LOGICAL:
    return stream->audio_channel_user;

  case XINE_PARAM_SPU_CHANNEL:
    return stream->spu_channel_user;

  case XINE_PARAM_VIDEO_CHANNEL:
    return stream->video_channel;

  case XINE_PARAM_AUDIO_VOLUME:
    if (!stream->audio_out)
      return -1;
    return stream->audio_out->get_property (stream->audio_out, AO_PROP_MIXER_VOL);

  case XINE_PARAM_AUDIO_MUTE:
    if (!stream->audio_out)
      return -1;
    return stream->audio_out->get_property (stream->audio_out, AO_PROP_MUTE_VOL);

  case XINE_PARAM_AUDIO_COMPR_LEVEL:
    if (!stream->audio_out)
      return -1;
    return stream->audio_out->get_property (stream->audio_out, AO_PROP_COMPRESSOR);

  case XINE_PARAM_AUDIO_AMP_LEVEL:
    if (!stream->audio_out)
      return -1;
    return stream->audio_out->get_property (stream->audio_out, AO_PROP_AMP);

  case XINE_PARAM_VERBOSITY:
    return stream->xine->verbosity;
    
  case XINE_PARAM_VO_HUE:
  case XINE_PARAM_VO_SATURATION:
  case XINE_PARAM_VO_CONTRAST:
  case XINE_PARAM_VO_BRIGHTNESS:
  case XINE_PARAM_VO_DEINTERLACE:
  case XINE_PARAM_VO_ASPECT_RATIO:
  case XINE_PARAM_VO_ZOOM_X:
  case XINE_PARAM_VO_ZOOM_Y:
  case XINE_PARAM_VO_PAN_SCAN:
  case XINE_PARAM_VO_TVMODE:
    return stream->video_out->get_property(stream->video_out, param);
  
  case XINE_PARAM_IGNORE_VIDEO:
    return stream->stream_info[XINE_STREAM_INFO_IGNORE_VIDEO];
    
  case XINE_PARAM_IGNORE_AUDIO:
    return stream->stream_info[XINE_STREAM_INFO_IGNORE_AUDIO];
  
  case XINE_PARAM_IGNORE_SPU:
    return stream->stream_info[XINE_STREAM_INFO_IGNORE_SPU];

  case XINE_PARAM_METRONOM_PREBUFFER:
    return stream->metronom_prebuffer;

  default:
    printf ("xine_interface: unknown param %d\n", param);
  }

  return 0;
}

uint32_t xine_get_stream_info (xine_stream_t *stream, int info) {

  switch (info) {

  case XINE_STREAM_INFO_SEEKABLE:
    if (stream->input_plugin)
      return stream->input_plugin->get_capabilities (stream->input_plugin) & INPUT_CAP_SEEKABLE;
    return 0;

  case XINE_STREAM_INFO_HAS_CHAPTERS:
    if (stream->input_plugin)
      return stream->input_plugin->get_capabilities (stream->input_plugin) & INPUT_CAP_CHAPTERS;
    return 0;

  case XINE_STREAM_INFO_BITRATE:
  case XINE_STREAM_INFO_VIDEO_WIDTH:
  case XINE_STREAM_INFO_VIDEO_HEIGHT:
  case XINE_STREAM_INFO_VIDEO_RATIO:
  case XINE_STREAM_INFO_VIDEO_CHANNELS:
  case XINE_STREAM_INFO_VIDEO_STREAMS:
  case XINE_STREAM_INFO_VIDEO_BITRATE:
  case XINE_STREAM_INFO_VIDEO_FOURCC:
  case XINE_STREAM_INFO_VIDEO_HANDLED:
  case XINE_STREAM_INFO_FRAME_DURATION:
  case XINE_STREAM_INFO_AUDIO_CHANNELS:
  case XINE_STREAM_INFO_AUDIO_BITS:
  case XINE_STREAM_INFO_AUDIO_SAMPLERATE:
  case XINE_STREAM_INFO_AUDIO_BITRATE:
  case XINE_STREAM_INFO_AUDIO_FOURCC:
  case XINE_STREAM_INFO_AUDIO_HANDLED:
  case XINE_STREAM_INFO_HAS_AUDIO:
  case XINE_STREAM_INFO_HAS_VIDEO:
  case XINE_STREAM_INFO_IGNORE_VIDEO:
  case XINE_STREAM_INFO_IGNORE_AUDIO:
  case XINE_STREAM_INFO_IGNORE_SPU:
  case XINE_STREAM_INFO_VIDEO_HAS_STILL:
    return stream->stream_info[info];

  case XINE_STREAM_INFO_MAX_AUDIO_CHANNEL:
    return stream->audio_track_map_entries;

  case XINE_STREAM_INFO_MAX_SPU_CHANNEL:
    return stream->spu_track_map_entries;
  
  default:
    printf ("xine_interface: error, unknown stream info (%d) requested\n",
	    info);
  }
  return 0;
}

const char *xine_get_meta_info (xine_stream_t *stream, int info) {

  return stream->meta_info[info];
}

xine_osd_t *xine_osd_new(xine_stream_t *stream, int x, int y, int width, int height) {
  xine_osd_t *this = (xine_osd_t *)stream->osd_renderer->new_object(stream->osd_renderer, width, height);
  this->osd.renderer->set_position(&this->osd, x, y);
  return this;
}
  
void xine_osd_draw_point(xine_osd_t *this, int x, int y, int color) {
  this->osd.renderer->point(&this->osd, x, y, color);
} 

void xine_osd_draw_line(xine_osd_t *this, int x1, int y1, int x2, int y2, int color) {
  this->osd.renderer->line(&this->osd, x1, y1, x2, y2, color);
}

void xine_osd_draw_rect(xine_osd_t *this, int x1, int y1, int x2, int y2, int color, int filled) {
  if (filled) {
    this->osd.renderer->filled_rect(&this->osd, x1, y1, x2, y2, color);
  } else {
    this->osd.renderer->line(&this->osd, x1, y1, x2, y1, color);
    this->osd.renderer->line(&this->osd, x2, y1, x2, y2, color);
    this->osd.renderer->line(&this->osd, x2, y2, x1, y2, color);
    this->osd.renderer->line(&this->osd, x1, y2, x1, y1, color);
  }
}

void xine_osd_draw_text(xine_osd_t *this, int x1, int y1, const char *text, int color_base) {
  this->osd.renderer->render_text(&this->osd, x1, y1, text, NULL, color_base);
}

void xine_osd_get_text_size(xine_osd_t *this, const char *text, int *width, int *height) {
  this->osd.renderer->get_text_size(&this->osd, text, width, height);
}

void xine_osd_set_font(xine_osd_t *this, const char *fontname, int size) {
  this->osd.renderer->set_font(&this->osd, fontname, size);
}

void xine_osd_set_position(xine_osd_t *this, int x, int y) {
  this->osd.renderer->set_position(&this->osd, x, y);
}

void xine_osd_show(xine_osd_t *this, int64_t vpts) {
  this->osd.renderer->show(&this->osd, vpts);
}

void xine_osd_hide(xine_osd_t *this, int64_t vpts) {
  this->osd.renderer->hide(&this->osd, vpts);
}

void xine_osd_clear(xine_osd_t *this) {
  this->osd.renderer->clear(&this->osd);
}

void xine_osd_free(xine_osd_t *this) {
  this->osd.renderer->free_object(&this->osd);
}

void xine_osd_set_palette(xine_osd_t *this, const uint32_t *const color, const uint8_t *const trans) {
  this->osd.renderer->set_palette(&this->osd, color, trans);
}

void xine_osd_set_text_palette(xine_osd_t *this, int palette_number, int color_base) {
  this->osd.renderer->set_text_palette(&this->osd, palette_number, color_base);
}

void xine_osd_get_palette(xine_osd_t *this, uint32_t *color, uint8_t *trans) {
  this->osd.renderer->get_palette(&this->osd, color, trans);
}

void xine_osd_draw_bitmap(xine_osd_t *this, uint8_t *bitmap,
			    int x1, int y1, int width, int height,
			    uint8_t *palette_map) {
  this->osd.renderer->draw_bitmap(&this->osd, bitmap, x1, y1, width, height, palette_map);
}

const char *const *xine_post_list_inputs(xine_post_t *this_gen) {
  post_plugin_t *this = (post_plugin_t *)this_gen;
  return this->input_ids;
}

const char *const *xine_post_list_outputs(xine_post_t *this_gen) {
  post_plugin_t *this = (post_plugin_t *)this_gen;
  return this->output_ids;
}

const xine_post_in_t *xine_post_input(xine_post_t *this_gen, char *name) {
  post_plugin_t  *this = (post_plugin_t *)this_gen;
  xine_post_in_t *input;
  
  input = xine_list_first_content(this->input);
  while (input) {
    if (strcmp(input->name, name) == 0)
      return input;
    input = xine_list_next_content(this->input);
  }
  return NULL;
}

const xine_post_out_t *xine_post_output(xine_post_t *this_gen, char *name) {
  post_plugin_t   *this = (post_plugin_t *)this_gen;
  xine_post_out_t *output;
  
  output = xine_list_first_content(this->output);
  while (output) {
    if (strcmp(output->name, name) == 0)
      return output;
    output = xine_list_next_content(this->output);
  }
  return NULL;
}

int xine_post_wire(xine_post_out_t *source, xine_post_in_t *target) {
  if (source && source->rewire) {
    if (target) {
      if (source->type == target->type)
        return source->rewire(source, target->data);
      else
        return 0;
    } else
      return source->rewire(source, NULL);
  }
  return 0;
}

int xine_post_wire_video_port(xine_post_out_t *source, xine_video_port_t *vo) {
  if (source && source->rewire) {
    if (vo) {
      if (source->type == XINE_POST_DATA_VIDEO)
        return source->rewire(source, vo);
      else
        return 0;
    } else
      return source->rewire(source, NULL);
  }
  return 0;
}

int xine_post_wire_audio_port(xine_post_out_t *source, xine_audio_port_t *ao) {
  if (source && source->rewire) {
    if (ao) {
      if (source->type == XINE_POST_DATA_AUDIO)
        return source->rewire(source, ao);
      else
        return 0;
    } else
      return source->rewire(source, NULL);
  }
  return 0;
}

xine_post_out_t * xine_get_video_source(xine_stream_t *stream) {
  return &stream->video_source;  
}

xine_post_out_t * xine_get_audio_source(xine_stream_t *stream) {
  return &stream->audio_source;
}

/* report error/message to UI. may be provided with several
 * string parameters. last parameter must be NULL.
 */
int xine_message(xine_stream_t *stream, int type, ...) {
  xine_ui_message_data_t *data;
  xine_event_t            event;
  char                   *explanation;
  int                     size;
  int                     n;
  va_list                 ap;
  char                   *s, *params;

  static char *std_explanation[] = {
    "",
    "Warning",
    "Unknown host:",
    "Unknown device:",
    "Network unreachable",
    "Connection refused:",
    "File not found:",
    "Read error from:",
    "Error loading library:"
  };

  if( type >= 0 && type <= sizeof(std_explanation)/
                           sizeof(std_explanation[0]) ) {
    explanation = std_explanation[type];
    size = strlen(explanation)+1;
  } else {
    explanation = NULL;
    size = 0;
  }

  n = 0;
  va_start(ap, type);
  while( (s = va_arg(ap, char *)) != NULL ) {
    size += strlen(s) + 1;
    n++;
  }
  va_end(ap);

  size += sizeof(xine_ui_message_data_t) + 1;
  data = xine_xmalloc( size );
  strcpy(data->compatibility.str, 
         "Upgrade your frontend to see the error messages");
  data->type = type;
  data->num_parameters = n;
  
  if( explanation ) {
    strcpy (data->messages, explanation);
    data->explanation = data->messages - (char *)data;
    params = data->messages + strlen(explanation) + 1;
  } else {
    data->explanation = 0;
    params = data->messages;
  }
  data->parameters = params - (char *)data;

  params[0] = '\0';
  va_start(ap, type);
  while( (s = va_arg(ap, char *)) != NULL ) {
    strcpy(params, s);
    params += strlen(s) + 1;
  }
  va_end(ap);

  params[0] = '\0';

  event.type = XINE_EVENT_UI_MESSAGE;
  event.stream = stream;
  event.data_length = size;
  event.data = data;
  xine_event_send(stream, &event);

  free(data);

  return 1;
}
