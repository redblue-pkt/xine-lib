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
 * $Id: spu.h,v 1.2 2001/07/04 20:32:29 uid32519 Exp $
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


void  spuInit (void);
void  decode_spu (u_char *data_start, u_char *data_end);
u_int buffer_spupack (u_int *length, u_char **start, u_char *end);
int   spuParseHdr (vo_overlay_t *spu, u_char *pkt_data, u_int pkt_len);
void  spuParseData (vo_overlay_t *spu);

#endif
