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
 * $Id: xine_decoder.c,v 1.39 2001/11/30 16:19:58 jcdutton Exp $
 *
 * stuff needed to turn libspu into a xine decoder plugin
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "buffer.h"
#include "events.h"
#include "xine_internal.h"
#include "video_out/alphablend.h"
#include "xine-engine/bswap.h"
#include "xineutils.h"
#include "spu.h"

#define LOG_DEBUG 1

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
  return (type == BUF_SPU_PACKAGE || type == BUF_SPU_CLUT || type == BUF_SPU_SUBP_CONTROL) ;
}

/* FIXME: This function needs checking */
static void spudec_reset (spudec_decoder_t *this) {
  int i;
  
  this->ovl_pts = 0;
  this->buf_pts = 0;

  this->state.visible = 0;

//  this->seq_list[0].finished = 1;   /* mark as cur_seq */
//  for (i = 1; i < NUM_SEQ_BUFFERS; i++) {
//    this->seq_list[i].finished = 2; /* free for reassembly */
//  }
  for (i=0; i < MAX_STREAMS; i++) {
    this->spu_stream_state[i].stream_filter = 1; /* So it works with non-navdvd plugins */
    this->spu_stream_state[i].ra_complete = 1;
    this->spu_stream_state[i].overlay_handle = -1;
  }

/* I don't think I need this.
  this->cur_seq = this->ra_seq = this->seq_list;
 */
}


static void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out) {

  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;

  this->vo_out      = vo_out;
  this->ovl_caps    = vo_out->get_capabilities(vo_out);
  this->output_open = 0;

  spudec_reset(this);
/* FIXME:Do we really need a default clut? */
  memcpy(this->state.clut, __default_clut, sizeof(this->state.clut));
  this->state.need_clut = 1;
}


static void spu_process (spudec_decoder_t *this, uint32_t stream_id) {
//  spu_overlay_event_t   *event;
//  spu_object_t  *object;
//  vo_overlay_t  *overlay;
  int pending = 1;
  this->cur_seq = &this->spu_stream_state[stream_id].ra_seq;

/* FIXME:Get Handle after we have found if "Forced display" is set or not. 
 */
    
#ifdef LOG_DEBUG
  printf ("spu: Found SPU from stream %d pts=%d vpts=%d\n",stream_id, 
          this->spu_stream_state[stream_id].pts,
          this->spu_stream_state[stream_id].vpts); 
#endif
  this->state.cmd_ptr = this->cur_seq->buf + this->cur_seq->cmd_offs;
  this->state.next_pts = -1; /* invalidate timestamp */
  this->state.modified = 1; /* Only draw picture if = 1 on first event of SPU */
  this->state.visible = 0; /* 0 - No value, 1 - Show, 2 - Hide. */
  this->state.menu = 0; /* 0 - No value, 1 - Forced Display. */
  this->state.delay = 0;
  this->cur_seq->finished=0;
  
  do {
    if (!this->spu_stream_state[stream_id].ra_seq.finished) {
      
      //spudec_nextseq(this);
/* Get do commands to build the event. */
      spu_do_commands(&this->state, this->cur_seq, &this->overlay);
      /* FIXME: Check for Forced-display or subtitle stream
       *        For subtitles, open event.
       *        For menus, store it for later.
       */

      if ((this->xine->spu_channel != stream_id) &&
           (this->state.menu == 0) ) {
#ifdef LOG_DEBUG
        printf ("spu: Dropping SPU channel %d\n", stream_id);
#endif
        return;
      }
      if ((this->state.modified) ) { 
        spu_draw_picture(&this->state, this->cur_seq, &this->overlay);
      }
      
      if (this->state.need_clut)
        spu_discover_clut(&this->state, &this->overlay);
      
      if (this->state.menu == 0) {
        /* Subtitle */
        if( this->spu_stream_state[stream_id].overlay_handle < 0 ) {
          this->spu_stream_state[stream_id].overlay_handle = 
               this->vo_out->overlay_source->get_handle(this->vo_out->overlay_source, 0);
        }
        
        if( this->spu_stream_state[stream_id].overlay_handle < 0 ) {
          printf("libspudec: No video_overlay handles left for SPU\n");
          return;
        }
        
        this->event.object.handle = this->spu_stream_state[stream_id].overlay_handle;
       
        memcpy(this->event.object.overlay, 
               &this->overlay,
               sizeof(vo_overlay_t));
        this->overlay.rle=NULL;
        
        this->event.event_type = this->state.visible;
        
        /* event hide event must free the handle after use */
        if( this->event.event_type == EVENT_HIDE_SPU ) {
          this->spu_stream_state[stream_id].overlay_handle = -1;
        }
                  
        this->event.vpts = this->spu_stream_state[stream_id].vpts+(this->state.delay*1000); 
        
        /*
        printf("spu event %d handle: %d vpts: %d\n", this->event.event_type,
           this->event.object.handle, this->event.vpts ); 
        */
      } else {
        /* Menu */
        if( this->menu_handle < 0 )
          this->menu_handle = this->vo_out->overlay_source->get_handle(this->vo_out->overlay_source,1);
        
        if( this->menu_handle < 0 ) {
          printf("libspudec: No video_overlay handles left for menu\n");
          return;
        }
        this->event.object.handle = this->menu_handle;
        
        memcpy(this->event.object.overlay, 
               &this->overlay,
               sizeof(vo_overlay_t));
        this->overlay.rle=NULL;
        
        this->event.event_type = EVENT_MENU_SPU;
        this->event.vpts = this->spu_stream_state[stream_id].vpts+(this->state.delay*1000); 
      }
      this->vo_out->overlay_source->add_event(this->vo_out->overlay_source, (void *)&this->event);
    } else {
      pending = 0;
    }
  } while (pending);

}

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {
  uint32_t stream_id;
  spu_seq_t       *cur_seq;
  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
  stream_id = buf->type & 0x1f ;
  cur_seq = &this->spu_stream_state[stream_id].ra_seq;

  if (buf->type == BUF_SPU_CLUT) {
    if (buf->content[0]) { /* cheap endianess detection */
      memcpy(this->state.clut, buf->content, sizeof(uint32_t)*16);
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
    int i;
    uint32_t *subp_control = (uint32_t*) buf->content;
    for (i = 0; i < 32; i++) {
      this->spu_stream_state[i].stream_filter = subp_control[i]; 
    }
    return;
  }


  if (buf->decoder_info[0] == 0)  /* skip preview data */
    return;

  if ( this->spu_stream_state[stream_id].stream_filter == 0) 
    return;

  if (buf->PTS) {
    metronom_t *metronom = this->xine->metronom;
    uint32_t vpts = metronom->got_spu_packet(metronom, buf->PTS, 0, buf->SCR);
    if (vpts < this->buf_pts) {
      /* FIXME: Don't do this yet, 
         because it will cause all sorts of 
         problems with malloc. 
       */
      /* spudec_reset(this); */
    }

    this->spu_stream_state[stream_id].vpts = vpts; /* Show timer */
    this->spu_stream_state[stream_id].pts = buf->PTS; /* Required to match up with NAV packets */
  }

/*  if (this->ra_complete) {
    spu_seq_t *tmp_seq = this->ra_seq + 1;
    if (tmp_seq >= this->seq_list + NUM_SEQ_BUFFERS)
      tmp_seq = this->seq_list;
    if (tmp_seq->finished > 1) {
      this->ra_seq = tmp_seq;
      this->ra_seq->PTS = this->buf_pts; 
    }
  }
 */
  stream_id = buf->type & 0x1f ;
  this->spu_stream_state[stream_id].ra_complete = 
     spu_reassembly(&this->spu_stream_state[stream_id].ra_seq,
                     this->spu_stream_state[stream_id].ra_complete,
                     buf->content,
                     buf->size);
  if(this->spu_stream_state[stream_id].ra_complete == 1) { 
    spu_process(this,stream_id);
  }
}

static void spudec_close (spu_decoder_t *this_gen) {
  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
  int i;
  
  if( this->menu_handle >= 0 )
    this->vo_out->overlay_source->free_handle(this->vo_out->overlay_source,
                                              this->menu_handle);
  this->menu_handle = -1;


  for (i=0; i < MAX_STREAMS; i++) {
    if( this->spu_stream_state[i].overlay_handle >= 0 )
      this->vo_out->overlay_source->free_handle(this->vo_out->overlay_source,
                                                this->spu_stream_state[i].overlay_handle);
    this->spu_stream_state[i].overlay_handle = -1;
  }
}

/* This function is probably not needed now */
static void spudec_nextseq(spudec_decoder_t* this) {
  spu_seq_t *tmp_seq = this->cur_seq + 1;
/*  if (tmp_seq >= this->seq_list + NUM_SEQ_BUFFERS)
    tmp_seq = this->seq_list;
 */
 
  if (!tmp_seq->finished) { /* is the next seq ready for process? */
    this->cur_seq->finished = 2; /* ready for reassembly */
    this->cur_seq = tmp_seq;
    this->state.cmd_ptr = this->cur_seq->buf + this->cur_seq->cmd_offs;
    this->state.next_pts = -1; /* invalidate timestamp */
    this->state.modified = 1;
    this->state.visible = 0;
    this->state.menu = 0;
  }
}

static void spudec_print_overlay( vo_overlay_t *ovl ) {
#ifdef LOG_DEBUG
  printf ("spu: OVERLAY to show\n");
  printf ("spu: \tx = %d y = %d width = %d height = %d\n",
	  ovl->x, ovl->y, ovl->width, ovl->height );
  printf ("spu: \tclut [%x %x %x %x]\n",
	  ovl->color[0], ovl->color[1], ovl->color[2], ovl->color[3]);
  printf ("spu: \ttrans [%d %d %d %d]\n",
	  ovl->trans[0], ovl->trans[1], ovl->trans[2], ovl->trans[3]);
  printf ("spu: \tclip top=%d bottom=%d left=%d right=%d\n",
	  ovl->clip_top, ovl->clip_bottom, ovl->clip_left, ovl->clip_right);
#endif
  return;
} 

static void spudec_event_listener(void *this_gen, xine_event_t *event_gen) {
  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;
  xine_spu_event_t *event = (xine_spu_event_t *) event_gen;

  if((!this) || (!event)) {
    return;
  }

  switch (event->event.type) {
  case XINE_EVENT_SPU_BUTTON:
    {
      video_overlay_event_t *overlay_event = NULL;
      vo_overlay_t        *overlay = NULL;
      spu_button_t        *but = event->data;

#ifdef LOG_DEBUG
      printf ("MALLOC1: overlay_event %p, len=%d\n",
	      overlay_event,
	      sizeof(video_overlay_event_t));
#endif

      overlay_event = xine_xmalloc (sizeof(video_overlay_event_t));

#ifdef LOG_DEBUG
      printf("MALLOC2: overlay_event %p, len=%d\n",
	     overlay_event,
	     sizeof(video_overlay_event_t));
#endif

#ifdef LOG_DEBUG
      printf ("MALLOC1: overlay %p, len=%d\n",
	      overlay,
	      sizeof(vo_overlay_t));
#endif

      overlay = xine_xmalloc (sizeof(vo_overlay_t));

#ifdef LOG_DEBUG
      printf ("MALLOC2: overlay %p, len=%d\n",
	      overlay,
	      sizeof(vo_overlay_t));
#endif

#ifdef LOG_DEBUG
      printf ("BUTTON\n");
      printf ("\tshow=%d\n",but->show);
      printf ("\tclut [%x %x %x %x]\n",
	   but->color[0], but->color[1], but->color[2], but->color[3]);
      printf ("\ttrans [%d %d %d %d]\n",
	   but->trans[0], but->trans[1], but->trans[2], but->trans[3]);
      printf ("\tleft = %u right = %u top = %u bottom = %u\n",
	   but->left, but->right, but->top, but->bottom );
#endif
      if (!this->state.menu) return;

#ifdef LOG_DEBUG
      printf ("libspudec:xine_decoder.c:spudec_event_listener:this->menu_handle=%u\n",this->menu_handle);
#endif
      
      if (but->show) {
        overlay_event->object.handle = this->menu_handle;
        overlay_event->object.overlay=overlay;
        overlay_event->event_type = EVENT_MENU_BUTTON;
        overlay_event->vpts = 0; /* Activate it NOW */
        overlay->clip_top = but->top;
        overlay->clip_bottom = but->bottom;
        overlay->clip_left = but->left;
        overlay->clip_right = but->right;
        overlay->color[0] = this->state.clut[but->color[0]];
        overlay->color[1] = this->state.clut[but->color[1]];
        overlay->color[2] = this->state.clut[but->color[2]];
        overlay->color[3] = this->state.clut[but->color[3]];
        overlay->trans[0] = but->trans[0];
        overlay->trans[1] = but->trans[1];
        overlay->trans[2] = but->trans[2];
        overlay->trans[3] = but->trans[3];
      } else {
        overlay_event->object.handle = this->menu_handle;
        overlay_event->event_type = EVENT_HIDE_MENU;
        overlay_event->vpts = 0; /* Activate it NOW */
      }
      this->vo_out->overlay_source->add_event(this->vo_out->overlay_source, (void *)overlay_event);
    }
    break;
  case XINE_EVENT_SPU_CLUT:
    {
    /* FIXME: This function will need checking before it works. */
      spu_cltbl_t *clut = event->data;
      if (clut) {
        memcpy(this->state.clut, clut->clut, sizeof(int32_t)*16);
        this->state.need_clut = 0;
      }
    }
    break;
  }
}

static char *spudec_get_id(void) {
  return "spudec";
}

spu_decoder_t *init_spu_decoder_plugin (int iface_version, xine_t *xine) {

  spudec_decoder_t *this ;

  if (iface_version != 4) {
    printf("libspudec: Doesn't support plugin API version %d.\n"
	   "libspudec: This means there is a version mismatch between XINE and\n"
	   "libspudec: this plugin.\n", iface_version);
    return NULL;
  }

  this = (spudec_decoder_t *) xine_xmalloc (sizeof (spudec_decoder_t));
/* xine_xmalloc does memset */
/*  memset (this, 0, sizeof(*this)); */

  this->spu_decoder.interface_version   = 4;
  this->spu_decoder.can_handle          = spudec_can_handle;
  this->spu_decoder.init                = spudec_init;
  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.close               = spudec_close;
  this->spu_decoder.get_identifier      = spudec_get_id;
  this->spu_decoder.priority            = 1;

  this->xine                            = xine;
  
  this->menu_handle = -1;
  this->event.object.overlay = malloc(sizeof(vo_overlay_t));
 
  xine_register_event_listener(xine, spudec_event_listener, this);

  return (spu_decoder_t *) this;
}

