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
 * $Id: xine_decoder.c,v 1.22 2002/12/09 21:56:29 mroi Exp $
 *
 * closed caption spu decoder. receive data by events. 
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "cc_decoder.h"

/*
#define LOG_DEBUG 1
*/

typedef struct spucc_decoder_s {
  spu_decoder_t      spu_decoder;

  xine_stream_t     *stream;
  
  /* closed captioning decoder state */
  cc_decoder_t *ccdec;
  /* true if ccdec has been initialized */
  int cc_open;

  /* closed captioning decoder configuration and intrinsics */
  cc_config_t cc_cfg;

  /* video dimensions captured in frame change events */
  int video_width;
  int video_height;

  /* big lock regulating access to the CC decoder, CC renderer, and
     configuration changes. For CC decoding, fine-grained locking is not
     necessary. Using just  single lock for everything makes the code
     for configuraton changes *a lot* simpler, and *much* easier to
     debug and maintain.
  */
  pthread_mutex_t cc_mutex;
  
  /* events will be sent here */
  xine_event_queue_t *queue;
  
} spucc_decoder_t;


/*------------------- general utility functions ----------------------------*/

static void copy_str(char *d, const char *s, size_t maxbytes)
{
  strncpy(d, s, maxbytes - 1);
  d[maxbytes - 1] = '\0';
}


/*------------------- private methods --------------------------------------*/

/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void spucc_update_intrinsics(spucc_decoder_t *this)
{
#ifdef LOG_DEBUG
  printf("spucc: update_intrinsics\n");
#endif

  if (this->cc_open)
    cc_renderer_update_cfg(this->cc_cfg.renderer, this->video_width,
			   this->video_height);
}


/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void spucc_do_close(spucc_decoder_t *this)
{
  if (this->cc_open) {
#ifdef LOG_DEBUG
    printf("spucc: close\n");
#endif
    cc_decoder_close(this->ccdec);
    cc_renderer_close(this->cc_cfg.renderer);
    this->cc_open = 0;
  }
}


/* CAUTION: THIS FUNCTION ASSUMES THAT THE MUTEX IS ALREADY LOCKED! */
static void spucc_do_init (spucc_decoder_t *this, xine_video_port_t *vo_out)
{
  if (! this->cc_open) {
#ifdef LOG_DEBUG
    printf("spucc: init\n");
#endif
    /* initialize caption renderer */
    this->cc_cfg.renderer = cc_renderer_open(this->stream->osd_renderer,
					     this->stream->metronom,
					     &this->cc_cfg,
					     this->video_width,
					     this->video_height);
    spucc_update_intrinsics(this);
    /* initialize CC decoder */
    this->ccdec = cc_decoder_open(&this->cc_cfg);
    this->cc_open = 1;
  }
}


/*----------------- configuration listeners --------------------------------*/

static void spucc_cfg_enable_change(void *this_gen, xine_cfg_entry_t *value)
{
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  cc_config_t *cc_cfg = &this->cc_cfg;

  pthread_mutex_lock(&this->cc_mutex);

  cc_cfg->cc_enabled = value->num_value;
  if (! cc_cfg->cc_enabled) {
    /* captions were just disabled? */
    spucc_do_close(this);
  }
  /* caption decoder is initialized on demand, so do nothing on open */

#ifdef LOG_DEBUG
  printf("spucc: closed captions are now %s.\n", cc_cfg->cc_enabled?
	 "enabled" : "disabled");
#endif

  pthread_mutex_unlock(&this->cc_mutex);
}


static void spucc_cfg_scheme_change(void *this_gen, xine_cfg_entry_t *value)
{
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  cc_config_t *cc_cfg = &this->cc_cfg;

  pthread_mutex_lock(&this->cc_mutex);

  cc_cfg->cc_scheme = value->num_value;
#ifdef LOG_DEBUG
  printf("spucc: closed captioning scheme is now %s.\n",
	 cc_schemes[cc_cfg->cc_scheme]);
#endif
  spucc_update_intrinsics(this);
  pthread_mutex_unlock(&this->cc_mutex);
}


static void spucc_font_change(void *this_gen, xine_cfg_entry_t *value)
{
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  cc_config_t *cc_cfg = &this->cc_cfg;
  char *font;
  
  if (strcmp(value->key, "misc.cc_font") == 0)
    font = cc_cfg->font;
  else
    font = cc_cfg->italic_font;

  pthread_mutex_lock(&this->cc_mutex);

  copy_str(font, value->str_value, CC_FONT_MAX);
  spucc_update_intrinsics(this);
#ifdef LOG_DEBUG
  printf("spucc: changing %s to font %s\n", value->key, font);
#endif

  pthread_mutex_unlock(&this->cc_mutex);
}


static void spucc_num_change(void *this_gen, xine_cfg_entry_t *value)
{
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  cc_config_t *cc_cfg = &this->cc_cfg;
  int *num;
  
  if (strcmp(value->key, "misc.cc_font_size") == 0)
    num = &cc_cfg->font_size;
  else
    num = &cc_cfg->center;

  pthread_mutex_lock(&this->cc_mutex);

  *num = value->num_value;
  spucc_update_intrinsics(this);
#ifdef LOG_DEBUG
  printf("spucc: changing %s to %d\n", value->key, *num);
#endif

  pthread_mutex_unlock(&this->cc_mutex);
}


static void spucc_register_cfg_vars(spucc_decoder_t *this,
				    config_values_t *xine_cfg) {
  cc_config_t *cc_vars = &this->cc_cfg;

  cc_vars->cc_enabled = xine_cfg->register_bool(xine_cfg, 
						"misc.cc_enabled", 0,
						_("Enable closed captions in MPEG-2 streams"),
						NULL, 0, spucc_cfg_enable_change,
						this);
  
  cc_vars->cc_scheme = xine_cfg->register_enum(xine_cfg,
					       "misc.cc_scheme", 0,
					       cc_schemes,
					       _("Closed-captioning foreground/background scheme"),
					       NULL, 10, spucc_cfg_scheme_change,
					       this);
  
  copy_str(cc_vars->font, 
	   xine_cfg->register_string(xine_cfg, "misc.cc_font", "cc",
				     _("Standard closed captioning font"),
				     NULL, 10, spucc_font_change, this),
	   CC_FONT_MAX);
  
  copy_str(cc_vars->italic_font,
	   xine_cfg->register_string(xine_cfg, "misc.cc_italic_font", "cci",
				     _("Italic closed captioning font"),
				     NULL, 10, spucc_font_change, this),
	   CC_FONT_MAX);
  
  cc_vars->font_size = xine_cfg->register_num(xine_cfg, "misc.cc_font_size",
					      24,
					      _("Closed captioning font size"),
					      NULL, 10, spucc_num_change,
					      this);
  
  cc_vars->center = xine_cfg->register_bool(xine_cfg, "misc.cc_center", 1,
					    _("Center-adjust closed captions"),
					    NULL, 10, spucc_num_change,
					    this);
}


static void spucc_unregister_cfg_callbacks(config_values_t *xine_cfg) {
  xine_cfg->unregister_callback(xine_cfg, "misc.cc_enabled");
  xine_cfg->unregister_callback(xine_cfg, "misc.cc_scheme");
  xine_cfg->unregister_callback(xine_cfg, "misc.cc_font");
  xine_cfg->unregister_callback(xine_cfg, "misc.cc_italic_font");
  xine_cfg->unregister_callback(xine_cfg, "misc.cc_font_size");
  xine_cfg->unregister_callback(xine_cfg, "misc.cc_center");
}


/* called when the video frame size changes */
void spucc_notify_frame_change(spucc_decoder_t *this, int width, int height)
{
#ifdef LOG_DEBUG
  printf("spucc: new frame size: %dx%d\n", width, height);
#endif

  pthread_mutex_lock(&this->cc_mutex);
  this->video_width = width;
  this->video_height = height;
  spucc_update_intrinsics(this);
  pthread_mutex_unlock(&this->cc_mutex);
}


/*------------------- implementation of spudec interface -------------------*/

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  xine_event_t *event;
  
  if ((event = xine_event_get(this->queue))) {
    switch (event->type) {
    case XINE_EVENT_FRAME_FORMAT_CHANGE:
      {
        xine_format_change_data_t *frame_change = 
          (xine_format_change_data_t *)event->data;
        
        spucc_notify_frame_change(this, frame_change->width,
                                 frame_change->height);
      }
      break;
    }
  }
  
  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
  } else {
    pthread_mutex_lock(&this->cc_mutex);
    if (this->cc_cfg.cc_enabled) {
      if( !this->cc_open )
	spucc_do_init (this, NULL);
      
      if(this->cc_cfg.can_cc) {
	decode_cc(this->ccdec, buf->content, buf->size,
		  buf->pts);
      }
    }
    pthread_mutex_unlock(&this->cc_mutex);
  }
}  

static void spudec_reset (spu_decoder_t *this_gen) {
}

static void spudec_discontinuity (spu_decoder_t *this_gen) {
}

static void spudec_dispose (spu_decoder_t *this_gen) {
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;

  pthread_mutex_lock(&this->cc_mutex);
  spucc_do_close(this);
  pthread_mutex_unlock(&this->cc_mutex);

  spucc_unregister_cfg_callbacks(this->stream->xine->config);
  xine_event_dispose_queue(this->queue);
  pthread_mutex_destroy (&this->cc_mutex);
  free (this);
}


static spu_decoder_t *spudec_open_plugin (spu_decoder_class_t *class, xine_stream_t *stream) {

  spucc_decoder_t *this ;

  this = (spucc_decoder_t *) xine_xmalloc (sizeof (spucc_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.discontinuity       = spudec_discontinuity;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.get_nav_pci         = NULL;
  this->spu_decoder.set_button          = NULL;

  this->stream                          = stream;
  this->queue                           = xine_event_new_queue(stream);
  this->cc_open = 0;

  pthread_mutex_init(&this->cc_mutex, NULL);
  spucc_register_cfg_vars(this, stream->xine->config);
  cc_decoder_init();
   
  return &this->spu_decoder;
}

static char *spudec_get_identifier(spu_decoder_class_t *class) {
  return "spucc";
}

static char *spudec_get_description(spu_decoder_class_t *class) {
  return "closed caption decoder plugin";
}

static void spudec_class_dispose(spu_decoder_class_t *class) {
  free(class);
}


static void *init_spu_decoder_plugin (xine_t *xine, void *data) {

  spu_decoder_class_t *this ;

  this = (spu_decoder_class_t *) xine_xmalloc (sizeof (spu_decoder_class_t));

  this->open_plugin      = spudec_open_plugin;
  this->get_identifier   = spudec_get_identifier;
  this->get_description  = spudec_get_description;
  this->dispose          = spudec_class_dispose;

  return this;
}

/* plugin catalog information */
static uint32_t supported_types[] = { BUF_SPU_CC, 0 };

static decoder_info_t spudec_info = {
  supported_types,     /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_SPU_DECODER, 12, "spucc", XINE_VERSION_CODE, &spudec_info, &init_spu_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
