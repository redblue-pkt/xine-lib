/*
 *
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
 *
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
 * $Id: spu.c,v 1.20 2001/10/26 11:21:08 jcdutton Exp $
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
#include "video_out/alphablend.h"
#include "monitor.h"

#define LOG_DEBUG 1

#ifdef DEBUG

# ifdef	__GNUC__
#  define LOG(lvl, fmt...)	fprintf (stderr, fmt);
# else
#  define LOG(lvl, ...)		fprintf (stderr, __VA_ARGS__);
# endif

#else /* !DEBUG */

# ifdef __GNUC__
#  define LOG(lvl, fmt...)
# else
#  define LOG(lvl, ...)
# endif

#endif /* !DEBUG */


/* Return value: reassembly complete = 1 */
int spu_reassembly (spu_seq_t *seq, int start, uint8_t *pkt_data, u_int pkt_len)
{
  xprintf (VERBOSE|SPU, "pkt_len: %d\n", pkt_len);
  xprintf (VERBOSE|SPU, "Reassembly: start=%d seq=%p\n", start,seq);

  if (start) {
    seq->seq_len = (((u_int)pkt_data[0])<<8) | pkt_data[1];
    seq->cmd_offs = (((u_int)pkt_data[2])<<8) | pkt_data[3];

    if (seq->buf_len < seq->seq_len) {
      if (seq->buf) {
        xprintf (VERBOSE|SPU, "FREE1: seq->buf %p\n", seq->buf);
        free(seq->buf);
        seq->buf = NULL;
        xprintf (VERBOSE|SPU, "FREE2: seq->buf %p\n", seq->buf);
      }

      seq->buf_len = seq->seq_len;
      seq->buf = malloc(seq->buf_len);
      xprintf (VERBOSE|SPU, "MALLOC: seq->buf %p, len=%d\n", seq->buf,seq->buf_len);

    }
    seq->ra_offs = 0;
    
    xprintf (VERBOSE|SPU, "buf_len: %d\n", seq->buf_len);
    xprintf (VERBOSE|SPU, "cmd_off: %d\n", seq->cmd_offs);
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

int spu_next_event(spu_state_t *state, spu_seq_t* seq, int pts)
{
  uint8_t *buf = state->cmd_ptr;

  if (state->next_pts == -1) { /* timestamp valid? */
    state->next_pts = seq->PTS + ((buf[0] << 8) + buf[1]) * 1024;
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
#define CMD_SPU_WIPE		0x07  /* Not currently implemented */
#define CMD_SPU_EOF		0xff

void spu_do_commands(spu_state_t *state, spu_seq_t* seq, vo_overlay_t *ovl)
{
  uint8_t *buf = state->cmd_ptr;
  uint8_t *next_seq;

  xprintf (VERBOSE|SPU, "SPU EVENT\n");
  
  state->delay = (buf[0] << 8) + buf[1];
  xprintf (VERBOSE|SPU, "\tdelay=%d\n",state->delay);
  next_seq = seq->buf + (buf[2] << 8) + buf[3];
  buf += 4;

/* if next equals current, this is the last one
 */
  if (state->cmd_ptr >= next_seq)
    next_seq = seq->buf + seq->seq_len; /* allow to run until end */

  state->cmd_ptr = next_seq;
	      
  while (buf < next_seq && *buf != CMD_SPU_EOF) {
    switch (*buf) {
    case CMD_SPU_SHOW:		/* show subpicture */
      xprintf (VERBOSE|SPU, "\tshow subpicture\n");
      state->visible = 1;
      buf++;
      break;
      
    case CMD_SPU_HIDE:		/* hide subpicture */
      xprintf (VERBOSE|SPU, "\thide subpicture\n");
      state->visible = 2;
      buf++;
      break;
      
    case CMD_SPU_SET_PALETTE: {	/* CLUT */
      spu_clut_t *clut = (spu_clut_t *) (buf+1);
      
      state->cur_colors[3] = clut->entry0;
      state->cur_colors[2] = clut->entry1;
      state->cur_colors[1] = clut->entry2;
      state->cur_colors[0] = clut->entry3;
 
/* This is a bit out of context for now */
      ovl->color[3] = state->clut[clut->entry0]; 
      ovl->color[2] = state->clut[clut->entry1];
      ovl->color[1] = state->clut[clut->entry2];
      ovl->color[0] = state->clut[clut->entry3];
      xprintf (VERBOSE|SPU, "\tclut [%x %x %x %x]\n",
	   ovl->color[0], ovl->color[1], ovl->color[2], ovl->color[3]);
      state->modified = 1;
      buf += 3;
      break;
    }	
    case CMD_SPU_SET_ALPHA:	{	/* transparency palette */
      spu_clut_t *trans = (spu_clut_t *) (buf+1);
/* This should go into state for now */
      
      ovl->trans[3] = trans->entry0;
      ovl->trans[2] = trans->entry1;
      ovl->trans[1] = trans->entry2;
      ovl->trans[0] = trans->entry3;
      xprintf (VERBOSE|SPU, "\ttrans [%d %d %d %d]\n",
	   ovl->trans[0], ovl->trans[1], ovl->trans[2], ovl->trans[3]);
      state->modified = 1;
      buf += 3;
      break;
    }
    
    case CMD_SPU_SET_SIZE:		/* image coordinates */
/*    state->o_left  = (buf[1] << 4) | (buf[2] >> 4);
      state->o_right = (((buf[2] & 0x0f) << 8) | buf[3]);

      state->o_top    = (buf[4]  << 4) | (buf[5] >> 4);
      state->o_bottom = (((buf[5] & 0x0f) << 8) | buf[6]);
 */
      ovl->x      = (buf[1] << 4) | (buf[2] >> 4);
      ovl->y      = (buf[4]  << 4) | (buf[5] >> 4);
      ovl->width  = (((buf[2] & 0x0f) << 8) | buf[3]) - ovl->x + 1; 
      ovl->height = (((buf[5] & 0x0f) << 8) | buf[6]) - ovl->y + 1;
      ovl->clip_top    = 0;
      ovl->clip_bottom = ovl->height - 1;
      ovl->clip_left   = 0;
      ovl->clip_right  = ovl->width - 1;

      xprintf (VERBOSE|SPU, "\tx = %d y = %d width = %d height = %d\n",
	   ovl->x, ovl->y, ovl->width, ovl->height );
      state->modified = 1;
      buf += 7;
      break;
      
    case CMD_SPU_SET_PXD_OFFSET:	/* image top[0] field / image bottom[1] field*/
      state->field_offs[0] = (((u_int)buf[1]) << 8) | buf[2];
      state->field_offs[1] = (((u_int)buf[3]) << 8) | buf[4];
      xprintf (VERBOSE|SPU, "\toffset[0] = %d offset[1] = %d\n",
	   state->field_offs[0], state->field_offs[1]);
      state->modified = 1;
      buf += 5;
      break;
      
    case CMD_SPU_MENU:
      xprintf (VERBOSE|SPU, "\tForce Display/Menu\n");
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


  state->cmd_ptr = next_seq;


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

static int spu_next_line (vo_overlay_t *spu)
{
  get_bits (0); // byte align rle data
	
  put_x = 0;
  put_y++;
  field ^= 1; // Toggle fields
	
  if (put_y >= spu->height) {
    xprintf (VERBOSE|SPU, "put_y >= spu->height\n");
    return -1;
  }
  return 0;
}

void spu_draw_picture (spu_state_t *state, spu_seq_t* seq, vo_overlay_t *ovl)
{
  rle_elem_t *rle;
  field = 0;
  bit_ptr[0] = seq->buf + state->field_offs[0];
  bit_ptr[1] = seq->buf + state->field_offs[1];
  put_x = put_y = 0;
  get_bits (0);	/* Reset/init bit code */

/*  ovl->x      = state->o_left;
 *  ovl->y      = state->o_top;
 *  ovl->width  = state->o_right - state->o_left + 1;
 *  ovl->height = state->o_bottom - state->o_top + 1;

 *  ovl->clip_top    = 0;
 *  ovl->clip_bottom = ovl->height - 1;
 *  ovl->clip_left   = 0;
 *  ovl->clip_right  = ovl->width - 1;
 */

/*  spu_update_menu(state, ovl); FIXME: What is this for? */

  /* buffer is believed to be sufficiently large
   * with cmd_offs * 2 * sizeof(rle_elem_t), is that true? */
//  if (seq->cmd_offs * 2 * sizeof(rle_elem_t) > ovl->data_size) {
//    if (ovl->rle)
//      free(ovl->rle);
    ovl->data_size = seq->cmd_offs * 2 * sizeof(rle_elem_t);
    ovl->rle = malloc(ovl->data_size);
    xprintf (VERBOSE|SPU, "MALLOC: ovl->rle %p, len=%d\n", ovl->rle,ovl->data_size);
//  }

  state->modified = 0; /* mark as already processed */
  rle = ovl->rle;
  xprintf (VERBOSE|SPU, "Draw RLE=%p\n",rle);

  while (bit_ptr[1] < seq->buf + seq->cmd_offs) {
    u_int len;
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

    len   = vlc >> 2;

    /* if len == 0 -> end sequence - fill to end of line */
    if (len == 0)
      len = ovl->width - put_x;

    rle->len = len;
    rle->color = vlc & 0x03;
    rle++;
    put_x += len;

    if (put_x >= ovl->width) {
      if (spu_next_line (ovl) < 0)
        break;
    }
  }

  ovl->num_rle = rle - ovl->rle;
  ovl->rgb_clut = 0;
  xprintf (VERBOSE|SPU, "Num RLE=%d\n",ovl->num_rle);
  xprintf (VERBOSE|SPU, "Date size=%d\n",ovl->data_size);
  xprintf (VERBOSE|SPU, "sizeof RLE=%d\n",sizeof(rle_elem_t));
}

/* Heuristic to discover the colors used by the subtitles
   and assign a "readable" pallete to them.
   Currently looks for sequence of border-fg-border or
   border1-border2-fg-border2-border1.
   MINFOUND is the number of ocurrences threshold.
*/
#define MINFOUND 20
void spu_discover_clut(spu_state_t *state, vo_overlay_t *ovl)
{
  int bg,c;
  int seqcolor[10];
  int n,i;
  rle_elem_t *rle;

  int found[2][16];

  static clut_t text_clut[] = {
  CLUT_Y_CR_CB_INIT(0x80, 0x90, 0x80),
  CLUT_Y_CR_CB_INIT(0x00, 0x90, 0x00),
  CLUT_Y_CR_CB_INIT(0xff, 0x90, 0x00)
  };

  memset(found,0,sizeof(found));
  rle = ovl->rle;

  /* suppose the first and last pixels are bg */
  if( rle[0].color != rle[ovl->num_rle-1].color )
    return;

  bg = rle[0].color;

  i = 0;
  for( n = 0; n < ovl->num_rle; n++ )
  {
    c = rle[n].color;

    if( c == bg )
    {
      if( i == 3 && seqcolor[1] == seqcolor[3] )
      {
        found[0][seqcolor[2]]++;
        if( found[0][seqcolor[2]] > MINFOUND )
        {
           memcpy(&state->clut[state->cur_colors[seqcolor[1]]], &text_clut[1],
             sizeof(clut_t));
           memcpy(&state->clut[state->cur_colors[seqcolor[2]]], &text_clut[2],
             sizeof(clut_t));
           ovl->color[seqcolor[1]] = state->clut[state->cur_colors[seqcolor[1]]];
           ovl->color[seqcolor[2]] = state->clut[state->cur_colors[seqcolor[2]]];
           state->need_clut = 0;
           break;
        }
      }
      if( i == 5 && seqcolor[1] == seqcolor[5]
             && seqcolor[2] == seqcolor[4] )
      {
        found[1][seqcolor[3]]++;
        if( found[1][seqcolor[3]] > MINFOUND )
        {
           memcpy(&state->clut[state->cur_colors[seqcolor[1]]], &text_clut[0],
             sizeof(clut_t));
           memcpy(&state->clut[state->cur_colors[seqcolor[2]]], &text_clut[1],
             sizeof(clut_t));
           memcpy(&state->clut[state->cur_colors[seqcolor[3]]], &text_clut[2],
             sizeof(clut_t));
           ovl->color[seqcolor[1]] = state->clut[state->cur_colors[seqcolor[1]]];
           ovl->color[seqcolor[2]] = state->clut[state->cur_colors[seqcolor[2]]];
           ovl->color[seqcolor[3]] = state->clut[state->cur_colors[seqcolor[3]]];
           state->need_clut = 0;
           break;
        }
      }
      i = 0;
      seqcolor[i] = c;
    }
    else if ( i < 6 )
    {
      i++;
      seqcolor[i] = c;
    }
  }
}


void spu_update_menu (spu_state_t *state, vo_overlay_t *ovl) {

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
