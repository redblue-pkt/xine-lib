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
 * $Id: xine_decoder.c,v 1.9 2001/08/15 09:07:16 ehasenle Exp $
 *
 * stuff needed to turn libspu into a xine decoder plugin
 */

/*
 * FIXME: libspu uses global variables (that are written to)
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "spu.h"
#include "buffer.h"
#include "events.h"
#include "xine_internal.h"

static clut_t __default_clut[] = {
  {y: 0x00, cr: 0x80, cb:0x80},
  {y: 0xbf, cr: 0x80, cb:0x80},
  {y: 0x10, cr: 0x80, cb:0x80},
  {y: 0x28, cr: 0x6d, cb:0xef},
  {y: 0x51, cr: 0xef, cb:0x5a},
  {y: 0xbf, cr: 0x80, cb:0x80},
  {y: 0x36, cr: 0x80, cb:0x80},
  {y: 0x28, cr: 0x6d, cb:0xef},
  {y: 0xbf, cr: 0x80, cb:0x80},
  {y: 0x51, cr: 0x80, cb:0x80},
  {y: 0xbf, cr: 0x80, cb:0x80},
  {y: 0x10, cr: 0x80, cb:0x80},
  {y: 0x28, cr: 0x6d, cb:0xef},
  {y: 0x5c, cr: 0x80, cb:0x80},
  {y: 0xbf, cr: 0x80, cb:0x80},
  {y: 0x1c, cr: 0x80, cb:0x80},
  {y: 0x28, cr: 0x6d, cb:0xef}
};

#define NUM_SEQ_BUFFERS 5

typedef struct spudec_decoder_s {
  spu_decoder_t    spu_decoder;
  ovl_src_t        ovl_src;

  spu_seq_t	   seq_list[NUM_SEQ_BUFFERS];
  spu_seq_t       *cur_seq;
  spu_seq_t       *ra_seq;
  int              ra_complete;

  uint32_t         ovl_pts;
  uint32_t         buf_pts;
  spu_state_t      state;

  vo_instance_t   *vo_out;
  vo_overlay_t     ovl;
  int              ovl_caps;
  int              output_open;

} spudec_decoder_t;

int spudec_can_handle (spu_decoder_t *this_gen, int buf_type) {
  int type = buf_type & 0xFFFF0000;
  return (type == BUF_SPU_PACKAGE || type == BUF_SPU_CLUT) ;
}

static void spudec_reset (spudec_decoder_t *this) {
  int i;

  this->ovl_pts = 0;
  this->buf_pts = 0;

  this->state.visible = 0;

  this->seq_list[0].finished = 1;   /* mark as cur_seq */
  for (i = 1; i < NUM_SEQ_BUFFERS; i++) {
    this->seq_list[i].finished = 2; /* free for reassembly */
  }
  this->ra_complete = 1;
  this->cur_seq = this->ra_seq = this->seq_list;
}


void spudec_init (spu_decoder_t *this_gen, vo_instance_t *vo_out) {

  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;

  this->vo_out      = vo_out;
  this->ovl_caps    = vo_out->get_capabilities(vo_out);
  this->output_open = 0;

  spudec_reset(this);
  memcpy(this->state.clut, __default_clut, sizeof(this->state.clut));
  vo_out->register_ovl_src(vo_out, &this->ovl_src);
}

void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {

  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;

  if (buf->type == BUF_SPU_CLUT) {
    memcpy(this->state.clut, buf->content, sizeof(int32_t)*16);
    return;
  }

  if (buf->PTS) {
    metronom_t *metronom = this->ovl_src.metronom;
    uint32_t pts = metronom->got_spu_packet(metronom, buf->PTS, 0);
    if (pts < this->buf_pts)
      spudec_reset(this);

    this->buf_pts = pts;
  }

  if (this->ra_complete) {
    spu_seq_t *tmp_seq = this->ra_seq + 1;
    if (tmp_seq >= this->seq_list + NUM_SEQ_BUFFERS)
      tmp_seq = this->seq_list;
    
    if (tmp_seq->finished > 1) {
      this->ra_seq = tmp_seq;
      this->ra_seq->PTS = this->buf_pts;
    }
  }
  this->ra_complete = 
   spuReassembly(this->ra_seq, this->ra_complete, buf->content, buf->size);
}

void spudec_close (spu_decoder_t *this_gen) {
  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
  
  this->vo_out->unregister_ovl_src(this->vo_out, &this->ovl_src);
}

static void spudec_nextseq(spudec_decoder_t* this) {
  spu_seq_t *tmp_seq = this->cur_seq + 1;
  if (tmp_seq >= this->seq_list + NUM_SEQ_BUFFERS)
    tmp_seq = this->seq_list;
  
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

static vo_overlay_t* spudec_get_overlay(ovl_src_t *ovl_src, int pts) {
  spudec_decoder_t *this = (spudec_decoder_t*) ovl_src->src_gen;
  int pending = 0;

  if (this->ovl_pts > pts)
    spudec_reset(this);

  this->ovl_pts = pts;

  do {
    if (this->cur_seq->finished)
      spudec_nextseq(this);

    if (!this->cur_seq->finished) {
      pending = spuNextEvent(&this->state, this->cur_seq, pts);

      if (pending)
        spuDoCommands(&this->state, this->cur_seq, &this->ovl);
    } else
      pending = 0;

  } while (pending);

  if (this->state.visible) {
    if (this->state.modified) {
      spuDrawPicture(&this->state, this->cur_seq, &this->ovl);
    }

    return &this->ovl;
  }

  return NULL;
}

static void spudec_event(spu_decoder_t *this_gen, spu_event_t *event) {
  spudec_decoder_t *this = (spudec_decoder_t*) this_gen;

  switch (event->sub_type) {
  case SPU_EVENT_BUTTON:
    {
      spu_button_t *but = event->data;
      if (!this->state.menu) return;
      
      if (but->show) {
	int i;
	for (i = 0; i < 4; i++) {
	  this->ovl.color[i] = this->state.clut[but->color[i]];
	  this->ovl.trans[i] = but->trans[i];
	}
	
	this->state.b_left   = but->left;
	this->state.b_right  = but->right;
	this->state.b_top    = but->top;
	this->state.b_bottom = but->bottom;
      }

      this->state.b_show = but->show;
      spuUpdateMenu(&this->state, &this->ovl);
    }
    break;
  case SPU_EVENT_CLUT:
    {
      spu_cltbl_t *clut = event->data;
      memcpy(this->state.clut, clut->clut, sizeof(int32_t)*16);
    }
    break;
  }
}

static char *spudec_get_id(void) {
  return "spudec";
}

spu_decoder_t *init_spu_decoder_plugin (int iface_version, config_values_t *cfg) {

  spudec_decoder_t *this ;

  if (iface_version != 3) {
    fprintf(stderr,
     "libspudec: Doesn't support plugin API version %d.\n"
     "libspudec: This means there is a version mismatch between XINE and\n"
     "libspudec: this plugin.\n", iface_version);
    return NULL;
  }

  this = (spudec_decoder_t *) malloc (sizeof (spudec_decoder_t));
  memset (this, 0, sizeof(*this));

  this->spu_decoder.interface_version   = 3;
  this->spu_decoder.can_handle          = spudec_can_handle;
  this->spu_decoder.init                = spudec_init;
  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.event               = spudec_event;
  this->spu_decoder.close               = spudec_close;
  this->spu_decoder.get_identifier      = spudec_get_id;
  this->spu_decoder.priority            = 1;

  this->ovl_src.src_gen                 = this;
  this->ovl_src.get_overlay             = spudec_get_overlay;
  
  return (spu_decoder_t *) this;
}

