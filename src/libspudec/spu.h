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
 * $Id: spu.h,v 1.3 2001/08/13 12:52:33 ehasenle Exp $
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

#ifndef CLUT_T
#define CLUT_T
typedef struct {                // CLUT == Color LookUp Table
        uint8_t:8;
        uint8_t y:8;
        uint8_t cr:8;
        uint8_t cb:8;
} __attribute__ ((packed)) clut_t;
#endif

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

  u_int PTS;        /* Base PTS of this sequence */
  int finished;     /* Has this sequence been finished? */
} spu_seq_t;

typedef struct {
  uint8_t *cmd_ptr;

  int field_offs[2];
  int b_top,    o_top;
  int b_bottom, o_bottom;
  int b_left,   o_left;
  int b_right,  o_right;

  u_int next_pts;   /* pts of next sub-sequence */
  int modified;     /* Was the sub-picture modified? */
  int visible;      /* Must the sub-picture be shown? */
  int menu;         /* This overlay is a menu */
  int b_show;       /* is a button shown? */

  uint32_t clut[16];
} spu_state_t;

int spuReassembly (spu_seq_t *seq, int start, uint8_t *pkt_data, u_int pkt_len);
int spuNextEvent (spu_state_t *state, spu_seq_t* seq, int pts);
void spuDoCommands (spu_state_t *state, spu_seq_t* seq, vo_overlay_t *ovl);
void spuDrawPicture (spu_state_t *state, spu_seq_t* seq, vo_overlay_t *ovl);
void spuUpdateMenu (spu_state_t *state, vo_overlay_t *ovl);

#endif
