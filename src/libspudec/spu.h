/*
 * Copyright (C) 2000-2001 the xine project
 *
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
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
 * $Id: spu.h,v 1.10 2002/03/25 13:57:25 jcdutton Exp $
 *
 * This file was originally part of the OMS program.
 *
 */

#ifndef __SPU_H__
#define __SPU_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <inttypes.h>
#include "video_out.h"
#include "video_overlay.h"
#include "nav_types.h"

#define NUM_SEQ_BUFFERS 50
#define MAX_STREAMS 32

typedef struct spu_clut_struct {
#ifdef WORDS_BIGENDIAN
	uint8_t	entry0	: 4;
	uint8_t	entry1	: 4;
	uint8_t	entry2	: 4;
	uint8_t	entry3	: 4;
#else
	uint8_t	entry1	: 4;
	uint8_t	entry0	: 4;
	uint8_t	entry3	: 4;
	uint8_t	entry2	: 4;
#endif
} spu_clut_t;

typedef struct {
  uint8_t *buf;
  u_int    ra_offs; /* reassembly offset */
  u_int    seq_len;
  u_int    buf_len;

  u_int    cmd_offs;

  int64_t pts;        /* Base PTS of this sequence */
  int finished;     /* Has this sequence been finished? */
} spu_seq_t;

typedef struct {
  uint8_t *cmd_ptr;

  int field_offs[2];
  int b_top,    o_top;
  int b_bottom, o_bottom;
  int b_left,   o_left;
  int b_right,  o_right;

  int64_t next_pts;   /* pts of next sub-sequence */
  int modified;     /* Was the sub-picture modified? */
  int visible;      /* Must the sub-picture be shown? */
  int menu;         /* This overlay is a menu */
  int delay;        /* Delay in 90Khz / 1000 */
  int b_show;       /* is a button shown? */
  int need_clut;    /* doesn't have the right clut yet */
  int cur_colors[4];/* current 4 colors been used */

  uint32_t clut[16];
} spu_state_t;

typedef struct spudec_stream_state_s {
  spu_seq_t        ra_seq;
  uint32_t         ra_complete;
  uint32_t         stream_filter;
  spu_state_t      state;
  int64_t          vpts;
  int64_t          pts;
  int32_t          overlay_handle;
} spudec_stream_state_t;

typedef struct spudec_decoder_s {
  spu_decoder_t    spu_decoder;

  xine_t          *xine;
  spudec_stream_state_t spu_stream_state[MAX_STREAMS];
  
  video_overlay_event_t      event;
  video_overlay_object_t     object;  
  int32_t          menu_handle;
  
  int64_t          ovl_pts;
  int64_t          buf_pts;
  spu_state_t      state;

  vo_instance_t   *vo_out;
  vo_overlay_t     overlay;
  int              ovl_caps;
  int              output_open;
  pci_t            pci;
  uint32_t         buttonN;  /* Current button number for highlights */
} spudec_decoder_t;

int spu_reassembly (spu_seq_t *seq, int start, uint8_t *pkt_data, u_int pkt_len);
int spu_next_event (spu_state_t *state, spu_seq_t* seq, int64_t pts);
void spu_do_commands (spu_state_t *state, spu_seq_t* seq, vo_overlay_t *ovl);
void spu_draw_picture (spu_state_t *state, spu_seq_t* seq, vo_overlay_t *ovl);
void spu_discover_clut (spu_state_t *state, vo_overlay_t *ovl);
void spu_update_menu (spu_state_t *state, vo_overlay_t *ovl);
void spudec_reset (spudec_decoder_t *this);
void spudec_print_overlay( vo_overlay_t *overlay );
void spudec_copy_nav_to_spu( spudec_decoder_t *this );
void spu_process( spudec_decoder_t *this, uint32_t stream_id);
void spudec_decode_nav( spudec_decoder_t *this, buf_element_t *buf);

#endif
