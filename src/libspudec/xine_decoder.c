/*
 * Copyright (C) 2000-2001 the xine project
 * 
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
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
 * $Id: xine_decoder.c,v 1.66 2002/04/24 20:26:07 jcdutton Exp $
 *
 * stuff needed to turn libspu into a xine decoder plugin
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "buffer.h"
#include "events.h"
#include "xine_internal.h"
#include "video_out/alphablend.h" /* For clut_t */
#include "xine-engine/bswap.h"
#include "xineutils.h"
#include "spu.h"
#include "nav_types.h"
#include "nav_read.h"
#include "nav_print.h"

/*
#define LOG_DEBUG 1
#define LOG_BUTTON 1
*/

static clut_t __default_clut[] = {
  CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x10, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef),
  CLUT_Y_CR_CB_INIT(0x51, 0xef, 0x5a),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x36, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x51, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x10, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef),
  CLUT_Y_CR_CB_INIT(0x5c, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x1c, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef)
};

static int spudec_can_handle (spu_decoder_t *this_gen, int buf_type) {
  int type = buf_type & 0xFFFF0000;
  return (type == BUF_SPU_PACKAGE || type == BUF_SPU_CLUT || type == BUF_SPU_NAV || type == BUF_SPU_SUBP_CONTROL) ;
}

static void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out) {

  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
  int i;

  pthread_mutex_init(&this->nav_pci_lock, NULL);

  this->vo_out      = vo_out;
  this->ovl_caps    = vo_out->get_capabilities(vo_out);
  this->output_open = 0;
  this->last_event_vpts = 0;
  for (i=0; i < MAX_STREAMS; i++) {
    this->spudec_stream_state[i].stream_filter = 1; /* So it works with non-navdvd plugins */
    this->spudec_stream_state[i].ra_seq.complete = 1;
    this->spudec_stream_state[i].overlay_handle = -1;
  }

/* FIXME:Do we really need a default clut? */
  xine_fast_memcpy(this->state.clut, __default_clut, sizeof(this->state.clut));
  this->state.need_clut = 1;
}

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {
  uint32_t stream_id;
  spudec_seq_t       *cur_seq;
  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
  stream_id = buf->type & 0x1f ;
  cur_seq = &this->spudec_stream_state[stream_id].ra_seq;

  if (buf->type == BUF_SPU_CLUT) {
    printf("libspudec: SPU CLUT\n");
    if (buf->content[0]) { /* cheap endianess detection */
      xine_fast_memcpy(this->state.clut, buf->content, sizeof(uint32_t)*16);
    } else {
      int i;
      uint32_t *clut = (uint32_t*) buf->content;
      for (i = 0; i < 16; i++)
        this->state.clut[i] = bswap_32(clut[i]);
    }
    this->state.need_clut = 0;
    return;
  }
 
  if (buf->type == BUF_SPU_SUBP_CONTROL) {
    /* FIXME: I don't think SUBP_CONTROL is used any more */
    int i;
    uint32_t *subp_control = (uint32_t*) buf->content;
    for (i = 0; i < 32; i++) {
      this->spudec_stream_state[i].stream_filter = subp_control[i]; 
    }
    return;
  }
  if (buf->type == BUF_SPU_NAV) {
#ifdef LOG_DEBUG
    printf("libspudec:got nav packet 1\n");
#endif
    spudec_decode_nav(this,buf);
    return;
  }

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)  /* skip preview data */
    return;

  if ( this->spudec_stream_state[stream_id].stream_filter == 0) 
    return;

  if (buf->pts) {
    metronom_t *metronom = this->xine->metronom;
    int64_t vpts = metronom->got_spu_packet(metronom, buf->pts);
    
    this->spudec_stream_state[stream_id].vpts = vpts; /* Show timer */
    this->spudec_stream_state[stream_id].pts = buf->pts; /* Required to match up with NAV packets */
  }

  spudec_reassembly(&this->spudec_stream_state[stream_id].ra_seq,
                     buf->content,
                     buf->size);
  if(this->spudec_stream_state[stream_id].ra_seq.complete == 1) { 
    spudec_process(this,stream_id);
  }
}

static void spudec_reset (spu_decoder_t *this_gen) {
}

static void spudec_close (spu_decoder_t *this_gen) {
  spudec_decoder_t         *this = (spudec_decoder_t *) this_gen;
  int                       i;
  video_overlay_instance_t *ovl_instance = this->vo_out->get_overlay_instance (this->vo_out);
  
  if( this->menu_handle >= 0 )
    ovl_instance->free_handle(ovl_instance,
			      this->menu_handle);
  this->menu_handle = -1;


  for (i=0; i < MAX_STREAMS; i++) {
    if( this->spudec_stream_state[i].overlay_handle >= 0 )
      ovl_instance->free_handle(ovl_instance,
				this->spudec_stream_state[i].overlay_handle);
    this->spudec_stream_state[i].overlay_handle = -1;
  }
  pthread_mutex_destroy(&this->nav_pci_lock);
}

static void spudec_event_listener(void *this_gen, xine_event_t *event_gen) {
  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;
  xine_spu_event_t *event = (xine_spu_event_t *) event_gen;
  video_overlay_instance_t *ovl_instance;

  if((!this) || (!event)) {
    return;
  }

  switch (event->event.type) {
  case XINE_EVENT_SPU_BUTTON:
    {
      /* This function will move to video_overlay 
       * when video_overlay does menus */

      video_overlay_event_t *overlay_event = NULL;
      vo_overlay_t        *overlay = NULL;
      spu_button_t        *but = event->data;
      overlay_event = xine_xmalloc (sizeof(video_overlay_event_t));

      overlay = xine_xmalloc (sizeof(vo_overlay_t));

#ifdef LOG_DEBUG
      printf ("BUTTON\n");
      printf ("\tshow=%d\n",but->show);
      printf ("\tclut [%x %x %x %x]\n",
	   but->color[0], but->color[1], but->color[2], but->color[3]);
      printf ("\ttrans [%d %d %d %d]\n",
	   but->trans[0], but->trans[1], but->trans[2], but->trans[3]);
      printf ("\tleft = %u right = %u top = %u bottom = %u\n",
	   but->left, but->right, but->top, but->bottom );
      printf ("\tpts = %u\n",
	   but->pts );
#endif
      /* FIXME: Watch out for threads. We should really put a lock on this 
       * because events is a different thread than decode_data */
      //if (!this->state.forced_display) return;

#ifdef LOG_DEBUG
      printf ("libspudec:xine_decoder.c:spudec_event_listener:this->menu_handle=%u\n",this->menu_handle);
#endif
      
      if (but->show > 0) {
#ifdef LOG_NAV
        fprintf (stderr,"libspudec:xine_decoder.c:spudec_event_listener:buttonN = %u show=%d\n",
          but->buttonN,
          but->show);
#endif
        this->buttonN = but->buttonN;
        if (this->button_filter != 1) {
#ifdef LOG_NAV
          fprintf (stdout,"libspudec:xine_decoder.c:spudec_event_listener:buttonN updates not allowed\n");
#endif
          /* Only update highlight is the menu will let us */
          free(overlay_event);
          free(overlay);
          break;
        }
        if (but->show == 2) {
          this->button_filter = 2;
        }
        pthread_mutex_lock(&this->nav_pci_lock);
        overlay_event->object.handle = this->menu_handle;
        overlay_event->object.pts = this->pci.hli.hl_gi.hli_s_ptm;
        overlay_event->object.overlay=overlay;
        overlay_event->event_type = EVENT_MENU_BUTTON;
#ifdef LOG_NAV
        fprintf(stderr, "libspudec:Button Overlay\n");
#endif
        spudec_copy_nav_to_overlay(&this->pci, this->state.clut, this->buttonN, but->show-1, overlay );
        pthread_mutex_unlock(&this->nav_pci_lock);
      } else {
        fprintf (stderr,"libspudec:xine_decoder.c:spudec_event_listener:HIDE ????\n");
        assert(0);
        overlay_event->object.handle = this->menu_handle;
        overlay_event->event_type = EVENT_HIDE_MENU;
      }
      overlay_event->vpts = 0; 
      if (this->vo_out) {
        ovl_instance = this->vo_out->get_overlay_instance (this->vo_out);
#ifdef LOG_NAV
        fprintf(stderr, "libspudec: add_event type=%d : current time=%lld, spu vpts=%lld\n",
          overlay_event->event_type,
          this->xine->metronom->get_current_time(this->xine->metronom),
          overlay_event->vpts);
#endif
        ovl_instance->add_event (ovl_instance, (void *)overlay_event);
      } else {
        free(overlay_event);
        free(overlay);
      }
    }
    break;
  case XINE_EVENT_SPU_CLUT:
    {
    /* FIXME: This function will need checking before it works. */
      spudec_clut_table_t *clut = event->data;
      if (clut) {
        xine_fast_memcpy(this->state.clut, clut->clut, sizeof(uint32_t)*16);
        this->state.need_clut = 0;
      }
    }
    break;
  }
}

static char *spudec_get_id(void) {
  return "spudec";
}

static void spudec_dispose (spu_decoder_t *this_gen) {
  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;

  xine_remove_event_listener (this->xine, spudec_event_listener);

  free (this->event.object.overlay);
  free (this);
}

spu_decoder_t *init_spu_decoder_plugin (int iface_version, xine_t *xine) {

  spudec_decoder_t *this ;

  if (iface_version != 6) {
    printf("libspudec: Doesn't support plugin API version %d.\n"
	   "libspudec: This means there is a version mismatch between XINE and\n"
	   "libspudec: this plugin.\n", iface_version);
    return NULL;
  }

  this = (spudec_decoder_t *) xine_xmalloc (sizeof (spudec_decoder_t));
/* xine_xmalloc does memset */
/*  memset (this, 0, sizeof(*this)); */

  this->spu_decoder.interface_version   = iface_version;
  this->spu_decoder.can_handle          = spudec_can_handle;
  this->spu_decoder.init                = spudec_init;
  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.close               = spudec_close;
  this->spu_decoder.get_identifier      = spudec_get_id;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.priority            = 1;

  this->xine                            = xine;
  
  this->menu_handle = -1;
  this->buttonN = 1;
  this->event.object.overlay = malloc(sizeof(vo_overlay_t));
 
  xine_register_event_listener(xine, spudec_event_listener, this);

  return (spu_decoder_t *) this;
}

