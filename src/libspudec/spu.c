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

//#include <oms/plugin/output_video.h>	// for clut_t
#include "spu.h"

static u_int field;		// which field we are currently decoding

#define DISPLAY_INIT

#define REASSEMBLY_START	0
#define REASSEMBLY_MID		1
#define REASSEMBLY_UNNEEDED	2

static u_int reassembly_flag = REASSEMBLY_START;

struct reassembly_s {
  uint8_t *buf;
  uint8_t *buf_ptr;	// actual pointer to still empty buffer
  u_int buf_len;
  u_int cmd_offset;
} reassembly;
	
#define LOG_DEBUG 1

#ifdef DEBUG
#define LOG(lvl, fmt...)	fprintf (stderr, fmt);
#else
#define LOG(lvl, fmt...)
#endif


static u_int _get_bits (u_int bits, vo_overlay_t *spu)
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

      data = reassembly.buf[spu->offset[field]++];
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


void spuInit (void)
{
}	


static inline void _spu_put_pixel (vo_overlay_t *spu, u_int len, uint8_t colorid)
{
  uint8_t *spu_data_ptr = &spu->data[spu->_x + spu->_y * spu->width];
	
  spu->_x += len;

  memset (spu_data_ptr, spu->trans[colorid]<<4 | colorid, len);
}


static int _spu_next_line (vo_overlay_t *spu)
{
  _get_bits (0, spu); // byte align rle data
	
			   spu->_x = 0;
  spu->_y++;
  field = (field+1) & 0x01; // Toggle fields
	
				 if (spu->_y >= spu->height) {
				   LOG (LOG_DEBUG, ".");
				   return -1;
				 }
  return 0;
}


// DENT: we need a mechanism here, when having non-linearities (like jumps, ff)
     //	like pass NULL pkt_data to reset reassembly

static struct reassembly_s *_reassembly (uint8_t *pkt_data, u_int pkt_len)
{
  LOG (LOG_DEBUG, "pkt_len: %d", pkt_len);

  if (reassembly_flag == REASSEMBLY_UNNEEDED)
    reassembly_flag = REASSEMBLY_START;

  if (reassembly_flag == REASSEMBLY_START) {
    reassembly.buf_len = (((u_int)pkt_data[0])<<8) | pkt_data[1];
    reassembly.cmd_offset = (((u_int)pkt_data[2])<<8) | pkt_data[3];

    LOG (LOG_DEBUG, "buf_len: %d", reassembly.buf_len);
    LOG (LOG_DEBUG, "cmd_off: %d", reassembly.cmd_offset);

    // the whole spu fits into the supplied packet
	 if (pkt_len >= reassembly.buf_len) {
	   reassembly.buf = pkt_data;
	   reassembly_flag = REASSEMBLY_UNNEEDED;
	   return &reassembly;
	 } else {
	   if (!(reassembly.buf = malloc (reassembly.buf_len + 1))) {
	     LOG (LOG_DEBUG, "unable to alloc buffer");
	     return NULL;
	   }
	   reassembly.buf_ptr = reassembly.buf;

	   memcpy (reassembly.buf_ptr, pkt_data, pkt_len);
	   reassembly.buf_ptr += pkt_len;
	   reassembly_flag = REASSEMBLY_MID;
	 }
  } else {
    if ((reassembly.buf_ptr+pkt_len) > (reassembly.buf+reassembly.buf_len))
      pkt_len = reassembly.buf_len - (reassembly.buf_ptr - reassembly.buf);


    memcpy (reassembly.buf_ptr, pkt_data, pkt_len);
    reassembly.buf_ptr += pkt_len;

    if (reassembly.buf_ptr >= (reassembly.buf+reassembly.buf_len)) {
      reassembly_flag = REASSEMBLY_START;
      return &reassembly;
    }
  }
	
  return NULL;	
}


#define CMD_SPU_MENU		0x00
#define CMD_SPU_SHOW		0x01
#define CMD_SPU_HIDE		0x02
#define CMD_SPU_SET_PALETTE	0x03
#define CMD_SPU_SET_ALPHA	0x04
#define CMD_SPU_SET_SIZE	0x05
#define CMD_SPU_SET_PXD_OFFSET	0x06
#define CMD_SPU_EOF		0xff

/* The time is given as an offset from the presentation time stamp
   and it is measured in number of fields. If we play a NTSC movie
   the time for each field is 1/(2*29.97) seconds. */
#define TIME_UNIT 1000*1.0/(2*29.97)

int spuParseHdr (vo_overlay_t *spu, uint8_t *pkt_data, u_int pkt_len)
{
  struct reassembly_s *reassembly;
  uint8_t *buf; 
  u_int DCSQ_offset, prev_DCSQ_offset = -1;

  if (!(reassembly = _reassembly (pkt_data, pkt_len)))
    return -1;

  buf = reassembly->buf;
  DCSQ_offset = reassembly->cmd_offset;
	
  while (DCSQ_offset != prev_DCSQ_offset) { /* Display Control Sequences */
    u_int i = DCSQ_offset;
		
    spu->duration = /* Frames + */ ((buf[i] << 8) + buf[i+1]) /* * TIME_UNIT */ ;
    LOG (LOG_DEBUG, "duration = %d frames", spu->duration);
    i += 2;
		
    prev_DCSQ_offset = DCSQ_offset;
    DCSQ_offset = (buf[i] << 8) + buf[i+1];
    i += 2;
		
    while (buf[i] != CMD_SPU_EOF) {		/* Command Sequence */
      switch (buf[i]) {
      case CMD_SPU_SHOW:		/* show subpicture */
	LOG (LOG_DEBUG, "\tshow subpicture");
	i++;
	break;
	
      case CMD_SPU_HIDE:		/* hide subpicture */
	LOG (LOG_DEBUG, "\thide subpicture");
	i++;
	break;
	
      case CMD_SPU_SET_PALETTE: {	/* CLUT */
	spu_clut_t *clut = (spu_clut_t *) &buf[i+1];
	
	spu->clut[3] = clut->entry0;
	spu->clut[2] = clut->entry1;
	spu->clut[1] = clut->entry2;
	spu->clut[0] = clut->entry3;
	LOG (LOG_DEBUG, "\tclut [%d %d %d %d]",
	     spu->clut[0], spu->clut[1], spu->clut[2], spu->clut[3]);
	i += 3;
	break;
      }	
      case CMD_SPU_SET_ALPHA:	{	/* transparency palette */
	spu_clut_t *trans = (spu_clut_t *) &buf[i+1];
	
	spu->trans[3] = trans->entry0;
	spu->trans[2] = trans->entry1;
	spu->trans[1] = trans->entry2;
	spu->trans[0] = trans->entry3;
	LOG (LOG_DEBUG, "\ttrans [%d %d %d %d]\n",
	     spu->trans[0], spu->trans[1], spu->trans[2], spu->trans[3]);
	i += 3;
	break;
      }
      
      case CMD_SPU_SET_SIZE:		/* image coordinates */
	spu->x = (buf[i+1] << 4) |
	  (buf[i+2] >> 4);
	spu->width = (((buf[i+2] & 0x0f) << 8) |
		      buf[i+3]) - spu->x + 1; /* 1-720 */
						   
	spu->y = (buf[i+4]  << 4) |
	  (buf[i+5] >> 4);
	spu->height = (((buf[i+5] & 0x0f) << 8)
		       | buf[i+6]) - spu->y + 1; /* 1-576 */
						      
	if (spu->data) spu->data = (uint8_t *) realloc (spu->data,spu->width * spu->height * sizeof (uint8_t));
	else spu->data = (uint8_t *) malloc (spu->width * spu->height * sizeof (uint8_t));

				/* Private stuff */
	spu->_x = spu->_y = 0;
	LOG (LOG_DEBUG, "\tx = %d y = %d width = %d height = %d",
	     spu->x, spu->y, spu->width, spu->height);
	i += 7;
	break;
	
      case CMD_SPU_SET_PXD_OFFSET:	/* image 1 / image 2 offsets */
	spu->offset[0] = (((u_int)buf[i+1]) << 8) | buf[i+2];
	spu->offset[1] = (((u_int)buf[i+3]) << 8) | buf[i+4];
	LOG (LOG_DEBUG, "\toffset[0] = %d offset[1] = %d",
	     spu->offset[0], spu->offset[1]);
	i += 5;
	break;
	
      case CMD_SPU_MENU:
	/*
	 * hardcoded menu clut, uncomment this and comment CMD_SPU_SET_PALETTE and
	 * CMD_SPU_SET_ALPHA to see the menu buttons
	 */
	i++;
	break;
	
      default:
	LOG (LOG_DEBUG, "invalid sequence in control header (%.2x)", buf[i]);
	i++;
	break;
      }
    }
    i++; /* lose the CMD_SPU_EOF code (no need to, really) */
	      
    /* Until we change the interface we parse all 'Command Sequence's 
       but just overwrite the data in spu. Should be a list instead. */
  }
  
  /* Here we should have a linked list of display commands ready to 
     be decoded/executed by later calling some spu???() */

  return 0;
}


void spuParseData (vo_overlay_t *spu)
{
  field = 0;
  _get_bits (0, spu);	/* Reset/init bit code */
	
  while ((spu->offset[1] < reassembly.cmd_offset)) {
    u_int len;
    u_int color;
    u_int vlc;
    
    vlc = _get_bits (4, spu);
    if (vlc < 0x0004) {
      vlc = (vlc << 4) | _get_bits (4, spu);
      if (vlc < 0x0010) {
	vlc = (vlc << 4) | _get_bits (4, spu);
	if (vlc < 0x0040) {
	  vlc = (vlc << 4) | _get_bits (4, spu);
	}
      }
    }
    
    color = vlc & 0x03;
    len = vlc>>2;
    
    /* if len == 0 -> end sequence - fill to end of line */
    len = len ? : spu->width - spu->_x;
    
    _spu_put_pixel (spu, len, color);
    
    if (spu->_x >= spu->width)
      if (_spu_next_line (spu) < 0)
	goto clean_up;
  }
  
  /* Like the eof-line escape, fill the rest of the sp. with background */
  _spu_put_pixel (spu, spu->width - spu->_x, 0);
  while (!_spu_next_line (spu)) {
    _spu_put_pixel (spu, spu->width - spu->_x, 0);
  }

 clean_up:
  if (reassembly_flag != REASSEMBLY_UNNEEDED) {
    LOG (LOG_DEBUG, "freeing reassembly.buf");
    free (reassembly.buf);
  }

  reassembly_flag = REASSEMBLY_START;
}
