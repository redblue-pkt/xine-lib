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
 * $Id: xine_decoder.c,v 1.2 2002/01/07 19:30:09 cvogler Exp $
 *
 * closed caption spu decoder. receive data by events. 
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "events.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "cc_decoder.h"


#define LOG_DEBUG 1


typedef struct spucc_decoder_s {
  spu_decoder_t      spu_decoder;

  xine_t            *xine;
  
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
static void spucc_do_init (spucc_decoder_t *this, vo_instance_t *vo_out)
{
  if (! this->cc_open) {
#ifdef LOG_DEBUG
    printf("spucc: init\n");
#endif
    /* initialize caption renderer */
    this->cc_cfg.renderer = cc_renderer_open(this->xine->osd_renderer,
					     this->xine->metronom,
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

static void spucc_cfg_enable_change(void *this_gen, cfg_entry_t *value)
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


static void spucc_font_change(void *this_gen, cfg_entry_t *value)
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


static void spucc_num_change(void *this_gen, cfg_entry_t *value)
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
						"Enable closed captions in MPEG-2 streams",
						NULL, spucc_cfg_enable_change,
						this);

  copy_str(cc_vars->font, 
	   xine_cfg->register_string(xine_cfg, "misc.cc_font", "cc",
				     "Standard closed captioning font",
				     NULL, spucc_font_change, this),
	   CC_FONT_MAX);

  copy_str(cc_vars->italic_font,
	   xine_cfg->register_string(xine_cfg, "misc.cc_italic_font", "cci",
				     "Italic closed captioning font",
				     NULL, spucc_font_change, this),
	   CC_FONT_MAX);

  cc_vars->font_size = xine_cfg->register_num(xine_cfg, "misc.cc_font_size",
					      24,
					      "Closed captioning font size",
					      NULL, spucc_num_change,
					      this);

  cc_vars->center = xine_cfg->register_bool(xine_cfg, "misc.cc_center", 1,
					    "Center-adjust closed captions",
					    NULL, spucc_num_change,
					    this);
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

static int spudec_can_handle (spu_decoder_t *this_gen, int buf_type) {
  /*int type = buf_type & 0xFFFF0000;
  return (type == BUF_SPU_TEXT);  */
  return 0;
}


static void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out) {

  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;

  pthread_mutex_lock(&this->cc_mutex);
  spucc_do_init(this, vo_out);
  pthread_mutex_unlock(&this->cc_mutex);
}


static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  
  if (buf->decoder_info[0] == 0) {
  } else {
    pthread_mutex_lock(&this->cc_mutex);
    if (this->cc_cfg.cc_enabled) {
      if( !this->cc_open )
	spucc_do_init (this, NULL);
      
      if(this->cc_cfg.can_cc) {
	decode_cc(this->ccdec, buf->content, buf->size,
		  buf->PTS, buf->SCR);
      }
    }
    pthread_mutex_unlock(&this->cc_mutex);
  }
}  


static void spudec_close (spu_decoder_t *this_gen) {
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;

  pthread_mutex_lock(&this->cc_mutex);
  spucc_do_close(this);
  pthread_mutex_unlock(&this->cc_mutex);
}

static void spudec_event_listener(void *this_gen, xine_event_t *event_gen) {
  spucc_decoder_t *this  = (spucc_decoder_t *) this_gen;
  
  if((!this) || (!event_gen)) {
    return;
  }

  switch (event_gen->type) {
    case XINE_EVENT_FRAME_CHANGE:
    {
      xine_frame_change_event_t *frame_change = 
        (xine_frame_change_event_t *)event_gen;
      
      spucc_notify_frame_change(this, frame_change->width,
				frame_change->height);
    }
    break;
    
    case XINE_EVENT_CLOSED_CAPTION:
    {
      xine_closed_caption_event_t *closed_caption = 
        (xine_closed_caption_event_t *)event_gen;
      
      pthread_mutex_lock(&this->cc_mutex);
      if (this->cc_cfg.cc_enabled) {
	if (!this->cc_open)
	  spucc_do_init (this, NULL);
	if (this->cc_cfg.can_cc) {
	  decode_cc(this->ccdec, closed_caption->buffer,
		    closed_caption->buf_len, closed_caption->pts,
		    closed_caption->scr);
	}
      }
      pthread_mutex_unlock(&this->cc_mutex);
    }
    break;
  }
}


static char *spudec_get_id(void) {
  return "spucc";
}


spu_decoder_t *init_spu_decoder_plugin (int iface_version, xine_t *xine) {

  spucc_decoder_t *this ;

  if (iface_version != 4) {
    printf("libspucc: doesn't support plugin api version %d.\n"
	   "libspucc: This means there is a version mismatch between xine and\n"
	   "libspucc: this plugin.\n", iface_version);
    return NULL;
  }

  this = (spucc_decoder_t *) xine_xmalloc (sizeof (spucc_decoder_t));

  this->spu_decoder.interface_version   = iface_version;
  this->spu_decoder.can_handle          = spudec_can_handle;
  this->spu_decoder.init                = spudec_init;
  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.close               = spudec_close;
  this->spu_decoder.get_identifier      = spudec_get_id;
  this->spu_decoder.priority            = 1;

  this->xine                            = xine;
  this->cc_open = 0;

  pthread_mutex_init(&this->cc_mutex, NULL);
  spucc_register_cfg_vars(this, xine->config);
  cc_decoder_init();
   
  xine_register_event_listener(xine, spudec_event_listener, this);
   
  return (spu_decoder_t *) this;
}

