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
 * $Id: xine_decoder.c,v 1.1 2002/01/05 21:41:18 miguelfreitas Exp $
 *
 * closed caption spu decoder. receive data by events. 
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include "buffer.h"
#include "events.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "cc_decoder.h"

/*
#define LOG 1
*/


typedef struct spucc_decoder_s {
  spu_decoder_t      spu_decoder;

  xine_t            *xine;
  
  /* closed captioning decoder state */
  cc_decoder_t *ccdec;

  /* closed captioning decoder configuration */
  cc_config_t cc_cfg;

  int cc_open;
  
} spucc_decoder_t;


static int spudec_can_handle (spu_decoder_t *this_gen, int buf_type) {
  /*int type = buf_type & 0xFFFF0000;
  return (type == BUF_SPU_TEXT);  */
  return 0;
}


static void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out) {

  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  
  /* initialize CC decoder */
  this->ccdec = cc_decoder_open(this->xine->osd_renderer, this->xine->metronom,
                                this->xine->config, &this->cc_cfg);
  this->cc_open = 1;
}

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  int do_decode;
  
  if (buf->decoder_info[0] == 0) {
  } else {
    if( !this->cc_open )
      spudec_init (this_gen, NULL);
      
    pthread_mutex_lock(&this->cc_cfg.cc_mutex);
    do_decode = this->cc_cfg.vars.cc_enabled && this->cc_cfg.vars.can_cc;
    pthread_mutex_unlock(&this->cc_cfg.cc_mutex);
      
    if( do_decode ) {
      decode_cc(this->ccdec, buf->content, buf->size,
                buf->PTS, buf->SCR);
    }
  }
}  


static void spudec_close (spu_decoder_t *this_gen) {
  spucc_decoder_t *this = (spucc_decoder_t *) this_gen;
  
  cc_decoder_close(this->ccdec);
  this->cc_open = 0;
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
      
      if( !this->cc_open )
        spudec_init (this_gen, NULL);
        
      cc_notify_frame_change( this->ccdec, frame_change->width,
                              frame_change->height);
    }
    break;
    
    case XINE_EVENT_CLOSED_CAPTION:
    {
      xine_closed_caption_event_t *closed_caption = 
        (xine_closed_caption_event_t *)event_gen;
      int do_decode;
      
      if( !this->cc_open )
        spudec_init (this_gen, NULL);
      
      pthread_mutex_lock(&this->cc_cfg.cc_mutex);
      do_decode = this->cc_cfg.vars.cc_enabled && this->cc_cfg.vars.can_cc;
      pthread_mutex_unlock(&this->cc_cfg.cc_mutex);
      
      if( do_decode ) {
        decode_cc(this->ccdec, closed_caption->buffer, closed_caption->buf_len,
                  closed_caption->pts, closed_caption->scr);
      }
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
  
  cc_decoder_init(xine->config, &this->cc_cfg);
   
  xine_register_event_listener(xine, spudec_event_listener, this);
   
  return (spu_decoder_t *) this;
}

