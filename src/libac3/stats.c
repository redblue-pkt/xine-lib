/* 
 *  stats.c
 *
 *	Copyright (C) Aaron Holtzman - May 1999
 *
 *  This file is part of ac3dec, a free Dolby AC-3 stream decoder.
 *	
 *  ac3dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  ac3dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "config.h"
#include "ac3.h"
#include "ac3_internal.h"


#include "decode.h"
#include "stats.h"
#include "debug.h"


struct mixlev_s
{
	float clev;
	char *desc;
};

static const struct mixlev_s cmixlev_tbl[4] =  
{
	{0.707, "(-3.0 dB)"}, {0.595, "(-4.5 dB)"},
	{0.500, "(-6.0 dB)"}, {1.0,  "Invalid"}
};

static const struct mixlev_s smixlev_tbl[4] =  
{
	{0.707, "(-3.0 dB)"}, {0.500, "(-6.0 dB)"},
	{  0.0,   "off    "}, {  1.0, "Invalid"}
};

void stats_print_banner(ac3_state_t * state)
{
	fprintf(stdout,"%d.%d Mode\n",state->nfchans,state->lfeon);
}

void stats_print_syncinfo(ac3_state_t * state)
{
	dprintf("(syncinfo) ");
	
	switch (state->fscod)
	{
		case 2:
			dprintf("32 KHz   ");
			break;
		case 1:
			dprintf("44.1 KHz ");
			break;
		case 0:
			dprintf("48 KHz   ");
			break;
		default:
			dprintf("Invalid sampling rate ");
			break;
	}
}
	
void stats_print_bsi(ac3_state_t * state)
{
	dprintf("(bsi) ");
	dprintf(" %d.%d Mode ",state->nfchans,state->lfeon);
	if ((state->acmod & 0x1) && (state->acmod != 0x1))
		dprintf(" Centre Mix Level %s ",cmixlev_tbl[state->cmixlev].desc);
	if (state->acmod & 0x4)
		dprintf(" Sur Mix Level %s ",smixlev_tbl[state->cmixlev].desc);
	dprintf("\n");

}

char *exp_strat_tbl[4] = {"R   ","D15 ","D25 ","D45 "};

void stats_print_audblk(ac3_state_t * state,audblk_t *audblk)
{
	uint32_t i;

	dprintf("(audblk) ");
	dprintf("%s ",audblk->cplinu ? "cpl on " : "cpl off");
	dprintf("%s ",audblk->phsflginu? "phsflg " : "       ");
	dprintf("[");
	for(i=0;i<state->nfchans;i++)
		dprintf("%1d",audblk->blksw[i]);
	dprintf("]");

	dprintf("\n");
}
