/*****
*
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
 *
 * This file is part of xine
 * This file was originally part of the OMS program.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by 
* the Free Software Foundation; either version 2, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; see the file COPYING.  If not, write to
* the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*
* $Id: spu.c,v 1.6 2001/08/14 17:13:33 ehasenle Exp $
*
*****/

/*
 * spu.c - converts DVD subtitles to an XPM image
 *
 * Mostly based on hard work by:
 *
 * Copyright (C) 2000   Samuel Hocevar <sam@via.ecp.fr>
 *                       and Michel Lespinasse <walken@via.ecp.fr>
 *
 * Lots of rearranging by:
 *	Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *	Thomas Mirlacher <dent@cosy.sbg.ac.at>
 *		implemented reassembling
 *		cleaner implementation of SPU are saving
 *		overlaying (proof of concept for now)
 *		... and yes, it works now with oms
 *		added tranparency (provided by the SPU hdr) 
 *		changed structures for easy porting to MGAs DVD mode
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *                                                     
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <malloc.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "spu.h"

#define LOG_DEBUG 1

#ifdef DEBUG
#define LOG(lvl, fmt...)	fprintf (stderr, fmt);
#else
#define LOG(lvl, fmt...)
#endif

/* Return value: reassembly complete = 1 */
int spuReassembly (spu_seq_t *seq, int start, uint8_t *pkt_data, u_int pkt_len)
{
  LOG (LOG_DEBUG, "pkt_len: %d", pkt_len);

  if (start) {
    seq->seq_len = (((u_int)pkt_data[0])<<8) | pkt_data[1];
    seq->cmd_offs = (((u_int)pkt_data[2])<<8) | pkt_data[3];

    if (seq->buf_len < seq->seq_len) {
      if (seq->buf)
        free(seq->buf);

      seq->buf_len = seq->seq_len;
      seq->buf = malloc(seq->buf_len);
    }
    seq->ra_offs = 0;
    
    LOG (LOG_DEBUG, "buf_len: %d", seq->buf_len);
    LOG (LOG_DEBUG, "cmd_off: %d", seq->cmd_offs);
  }

  if (seq->ra_offs < seq->buf_len) {
    if (seq->ra_offs + pkt_len > seq->seq_len)
      pkt_len = seq->seq_len - seq->ra_offs;
      
    memcpy (seq->buf + seq->ra_offs, pkt_data, pkt_len);
    seq->ra_offs += pkt_len;
  }

  if (seq->ra_offs == seq->seq_len) {
    seq->finished = 0;
    return 1; /* sequence ready */
  }

  return 0;	
}

int spuNextEvent(spu_state_t *state, spu_seq_t* seq, int pts)
{
  uint8_t *buf = state->cmd_ptr;

  if (state->next_pts == -1) { /* timestamp valid? */
    state->next_pts = seq->PTS + ((buf[0] << 8) + buf[1]) * 1100;
    buf += 2;
    state->cmd_ptr = buf;
  }

  return state->next_pts <= pts;
}

#define CMD_SPU_MENU		0x00
#define CMD_SPU_SHOW		0x01
#define CMD_SPU_HIDE		0x02
#define CMD_SPU_SET_PALETTE	0x03
#define CMD_SPU_SET_ALPHA	0x04
#define CMD_SPU_SET_SIZE	0x05
#define CMD_SPU_SET_PXD_OFFSET	0x06
#define CMD_SPU_EOF		0xff

void spuDoCommands(spu_state_t *state, spu_seq_t* seq, vo_overlay_t *ovl)
{
  uint8_t *buf = state->cmd_ptr;
  uint8_t *next_seq;

  next_seq = seq->buf + (buf[0] << 8) + buf[1];
  buf += 2;

  if (state->cmd_ptr >= next_seq)
    next_seq = seq->buf + seq->seq_len; /* allow to run until end */

  state->cmd_ptr = next_seq;
	      
  while (buf < next_seq && *buf != CMD_SPU_EOF) {
    switch (*buf) {
    case CMD_SPU_SHOW:		/* show subpicture */
      LOG (LOG_DEBUG, "\tshow subpicture");
      state->visible = 1;
      buf++;
      break;
      
    case CMD_SPU_HIDE:		/* hide subpicture */
      LOG (LOG_DEBUG, "\thide subpicture");
      state->visible = 0;
      buf++;
      break;
      
    case CMD_SPU_SET_PALETTE: {	/* CLUT */
      spu_clut_t *clut = (spu_clut_t *) (buf+1);
      
      ovl->color[3] = state->clut[clut->entry0];
      ovl->color[2] = state->clut[clut->entry1];
      ovl->color[1] = state->clut[clut->entry2];
      ovl->color[0] = state->clut[clut->entry3];
      LOG (LOG_DEBUG, "\tclut [%x %x %x %x]",
	   ovl->color[0], ovl->color[1], ovl->color[2], ovl->color[3]);
      state->modified = 1;
      buf += 3;
      break;
    }	
    case CMD_SPU_SET_ALPHA:	{	/* transparency palette */
      spu_clut_t *trans = (spu_clut_t *) (buf+1);
      
      /* TODO: bswap32? */
      ovl->trans[3] = trans->entry0;
      ovl->trans[2] = trans->entry1;
      ovl->trans[1] = trans->entry2;
      ovl->trans[0] = trans->entry3;
      LOG (LOG_DEBUG, "\ttrans [%d %d %d %d]\n",
	   ovl->trans[0], ovl->trans[1], ovl->trans[2], ovl->trans[3]);
      state->modified = 1;
      buf += 3;
      break;
    }
    
    case CMD_SPU_SET_SIZE:		/* image coordinates */
      state->o_left  = (buf[1] << 4) | (buf[2] >> 4);
      state->o_right = (((buf[2] & 0x0f) << 8) | buf[3]);
						 
      state->o_top    = (buf[4]  << 4) | (buf[5] >> 4);
      state->o_bottom = (((buf[5] & 0x0f) << 8) | buf[6]);
						    
      LOG (LOG_DEBUG, "\ttop = %d bottom = %d left = %d right = %d",
	   state->o_left, state->o_right, state->o_top, state->o_bottom);
      state->modified = 1;
      buf += 7;
      break;
      
    case CMD_SPU_SET_PXD_OFFSET:	/* image 1 / image 2 offsets */
      state->field_offs[0] = (((u_int)buf[1]) << 8) | buf[2];
      state->field_offs[1] = (((u_int)buf[3]) << 8) | buf[4];
      LOG (LOG_DEBUG, "\toffset[0] = %d offset[1] = %d",
	   state->field_offs[0], state->field_offs[1]);
      state->modified = 1;
      buf += 5;
      break;
      
    case CMD_SPU_MENU:
      state->menu = 1;
      buf++;
      break;
      
    default:
      fprintf(stderr, "libspudec: unknown seqence command (%02x)\n", buf[0]);
      buf++;
      break;
    }
  }
  if (next_seq >= seq->buf + seq->seq_len)
    seq->finished = 1;       /* last sub-sequence */
  state->next_pts = -1;      /* invalidate timestamp */
}

static uint8_t *bit_ptr[2];
static int field;		// which field we are currently decoding
static int put_x, put_y;

static u_int get_bits (u_int bits)
{
  static u_int data;
  static u_int bits_left;
  u_int ret = 0;

  if (!bits) {	/* for realignment to next byte */
    bits_left = 0;
  }

  while (bits) {
    if (bits > bits_left) {
      ret |= data << (bits - bits_left);
      bits -= bits_left;

      data = *bit_ptr[field]++;
      bits_left = 8;
    } else {
      bits_left -= bits;
      ret |= data >> (bits_left);
      data &= (1 << bits_left) - 1;
      bits = 0;
    }
  }

  return ret;	
}

static inline void spu_put_pixel (vo_overlay_t *spu, int len, uint8_t colorid)
{
  memset (spu->data + put_x + put_y * spu->width, colorid, len);
  put_x += len;
}

static int spu_next_line (vo_overlay_t *spu)
{
  get_bits (0); // byte align rle data
	
  put_x = 0;
  put_y++;
  field ^= 1; // Toggle fields
	
  if (put_y >= spu->height) {
    LOG (LOG_DEBUG, ".");
    return -1;
  }
  return 0;
}

void spuDrawPicture (spu_state_t *state, spu_seq_t* seq, vo_overlay_t *ovl)
{
  field = 0;
  bit_ptr[0] = seq->buf + state->field_offs[0];
  bit_ptr[1] = seq->buf + state->field_offs[1];
  put_x = put_y = 0;
  get_bits (0);	/* Reset/init bit code */

  ovl->x      = state->o_left;
  ovl->y      = state->o_top;
  ovl->width  = state->o_right - state->o_left + 1;
  ovl->height = state->o_bottom - state->o_top + 1;

  ovl->clip_top    = 0;
  ovl->clip_bottom = ovl->height - 1;
  ovl->clip_left   = 0;
  ovl->clip_right  = ovl->width - 1;

  spuUpdateMenu(state, ovl);

  if (ovl->width * ovl->height > ovl->data_size) {
    if (ovl->data)
      free(ovl->data);
    ovl->data_size = ovl->width * ovl->height;
    ovl->data = malloc(ovl->data_size);
  }

  state->modified = 0; /* mark as already processed */

  while (bit_ptr[1] < seq->buf + seq->cmd_offs) {
    u_int len;
    u_int color;
    u_int vlc;
    
    vlc = get_bits (4);
    if (vlc < 0x0004) {
      vlc = (vlc << 4) | get_bits (4);
      if (vlc < 0x0010) {
	vlc = (vlc << 4) | get_bits (4);
	if (vlc < 0x0040) {
	  vlc = (vlc << 4) | get_bits (4);
	}
      }
    }
    
    color = vlc & 0x03;
    len   = vlc >> 2;
    
    /* if len == 0 -> end sequence - fill to end of line */
    if (!len)
      len = ovl->width - put_x;
    
    spu_put_pixel (ovl, len, color);
    
    if (put_x >= ovl->width)
      if (spu_next_line (ovl) < 0)
        return;
  }
  
  /* Like the eof-line escape, fill the rest of the sp. with background */
  do {
    spu_put_pixel (ovl, ovl->width, 0);
  } while (!spu_next_line (ovl));
}

void spuUpdateMenu (spu_state_t *state, vo_overlay_t *ovl) {

  if (!state->menu)
    return;

  if (state->b_show) {
  
    int left   = state->b_left;
    int right  = state->b_right;
    int top    = state->b_top;
    int bottom = state->b_bottom;

    if (left   < state->o_left)   left   = state->o_left;
    if (right  > state->o_right)  right  = state->o_right;
    if (top    < state->o_top)    top    = state->o_top;
    if (bottom > state->o_bottom) bottom = state->o_bottom;
    
    ovl->clip_top    = top    - state->o_top;
    ovl->clip_bottom = bottom - state->o_top;
    ovl->clip_left   = left   - state->o_left;
    ovl->clip_right  = right  - state->o_left;

    state->visible = 1;

  } else {
    state->visible = 0;
  }
}
