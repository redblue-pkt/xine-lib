/* 
 *    parse.c
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
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ac3.h"
#include "ac3_internal.h"

#include "bitstream.h"
#include "stats.h"
#include "debug.h"
#include "parse.h"
#include "bit_allocate.h"

static const uint8_t nfchans[8] = {2, 1, 2, 3, 3, 4, 4, 5};

int parse_syncinfo (uint8_t * buf, int * sample_rate, int * bit_rate)
{
    static int rate[] = { 32,  40,  48,  56,  64,  80,  96, 112,
			 128, 160, 192, 224, 256, 320, 384, 448,
			 512, 576, 640};
    int frmsizecod;
    int bitrate;

    if ((buf[0] != 0x0b) || (buf[1] != 0x77))	// syncword
	return 0;

    frmsizecod = buf[4] & 63;
    if (frmsizecod >= 38)
	return 0;
    *bit_rate = bitrate = rate [frmsizecod >> 1];

    switch (buf[4] & 0xc0) {
    case 0:	// 48 KHz
	*sample_rate = 48000;
	return 4 * bitrate;
    case 0x40:
	*sample_rate = 44100;
	return 2 * (320 * bitrate / 147 + (frmsizecod & 1));
    case 0x80:
	*sample_rate = 32000;
	return 6 * bitrate;
    default:
	return 0;
    }
}

int parse_bsi (ac3_state_t * state, uint8_t * buf)
{
    int chaninfo;

    state->fscod = buf[4] >> 6;

    if (buf[5] >= 0x48)		// bsid >= 9
	return 1;

    state->acmod = buf[6] >> 5;
    state->nfchans = nfchans[state->acmod];

    bitstream_set_ptr (buf + 6);
    bitstream_get (3);	// skip acmod we already parsed

    if ((state->acmod & 0x1) && (state->acmod != 0x1))
	state->cmixlev = bitstream_get (2);

    if (state->acmod & 0x4)
	state->surmixlev = bitstream_get (2);

    if (state->acmod == 0x2)
	bitstream_get (2);	// dsurmod

    state->lfeon = bitstream_get (1);

    chaninfo = (state->acmod) ? 0 : 1;
    do {
	bitstream_get (5);	// dialnorm
	if (bitstream_get (1))	// compre
	    bitstream_get (8);	// compr
	if (bitstream_get (1))	// langcode
	    bitstream_get (8);	// langcod
	if (bitstream_get (1))	// audprodie
	    bitstream_get (7);	// mixlevel + roomtyp
    } while (chaninfo--);

    bitstream_get (2);		// copyrightb + origbs

    if (bitstream_get (1))	// timecod1e
	bitstream_get (14);	// timecod1
    if (bitstream_get (1))	// timecod2e
	bitstream_get (14);	// timecod2

    if (bitstream_get (1)) {	// addbsie
	int addbsil;

	addbsil = bitstream_get (6);
	do {
	    bitstream_get (8);	// addbsi
	} while (addbsil--);
    }

    stats_print_bsi(state);
    return 0;
}

static int parse_exponents (int expstr, int ngrps, uint8_t exponent,
			    uint8_t * dest)
{
    int exps;
    int8_t exp_1, exp_2, exp_3;

    while (ngrps--) {
	exps = bitstream_get (7);
	if (exps >= 125)
	    return 1;

	exp_1 = exps / 25;
	exp_2 = (exps - (exp_1 * 25)) / 5;
	exp_3 = exps - (exp_1 * 25) - (exp_2 * 5) ;

	exponent += (exp_1 - 2);
	if (exponent > 24)
	    return 1;

	switch (expstr) {
	case EXP_D45:
	    *(dest++) = exponent;
	    *(dest++) = exponent;
	case EXP_D25:
	    *(dest++) = exponent;
	case EXP_D15:
	    *(dest++) = exponent;
	}

	exponent += (exp_2 - 2);
	if (exponent > 24)
	    return 1;

	switch (expstr) {
	case EXP_D45:
	    *(dest++) = exponent;
	    *(dest++) = exponent;
	case EXP_D25:
	    *(dest++) = exponent;
	case EXP_D15:
	    *(dest++) = exponent;
	}

	exponent += (exp_3 - 2);
	if (exponent > 24)
	    return 1;

	switch (expstr) {
	case EXP_D45:
	    *(dest++) = exponent;
	    *(dest++) = exponent;
	case EXP_D25:
	    *(dest++) = exponent;
	case EXP_D15:
	    *(dest++) = exponent;
	}
    }	

    return 0;
}

static int parse_deltba (int8_t * deltba)
{
    int deltnseg, deltlen, delta, j;

    memset (deltba, 0, 50);

    deltnseg = bitstream_get (3);
    j = 0;
    do {
	j += bitstream_get (5);
	deltlen = bitstream_get (4);
	delta = bitstream_get (3);
	delta -= (delta >= 4) ? 3 : 4;
	if (!deltlen)
	    continue;
	if (j + deltlen >= 50)
	    return 1;
	while (deltlen--)
	    deltba[j++] = delta;
    } while (deltnseg--);

    return 0;
}

static inline int zero_snr_offsets (ac3_state_t * state, audblk_t * audblk)
{
    int i;

    if ((audblk->csnroffst) ||
	(audblk->cplinu && audblk->cplba.fsnroffst) ||
	(state->lfeon && audblk->lfeba.fsnroffst))
	return 0;
    for (i = 0; i < state->nfchans; i++)
	if (audblk->ba[i].fsnroffst)
	    return 0;
    return 1;
}

int parse_audblk (ac3_state_t * state, audblk_t * audblk)
{
    int i, chaninfo;
    uint8_t cplexpstr, chexpstr[5], lfeexpstr, do_bit_alloc;

    for (i = 0; i < state->nfchans; i++)
	audblk->blksw[i] = bitstream_get (1);

    for (i = 0; i < state->nfchans; i++)
	audblk->dithflag[i] = bitstream_get (1);

    chaninfo = (state->acmod) ? 0 : 1;
    do {
	if (bitstream_get (1))	// dynrnge
	    bitstream_get (8);	// dynrng
    } while (chaninfo--);

    if (bitstream_get (1)) {	// cplstre
	audblk->cplinu = bitstream_get (1);
	if (audblk->cplinu) {
	    for (i = 0; i < state->nfchans; i++)
		audblk->chincpl[i] = bitstream_get (1);
	    if (state->acmod == 0x2)
		audblk->phsflginu = bitstream_get (1);
	    audblk->cplbegf = bitstream_get (4);
	    audblk->cplendf = bitstream_get (4);

	    audblk->cplstrtmant = audblk->cplbegf * 12 + 37;
	    audblk->cplendmant = audblk->cplendf * 12 + 73;
	    audblk->ncplsubnd = audblk->cplendf + 3 - audblk->cplbegf;
	    audblk->ncplbnd = audblk->ncplsubnd;

	    for(i = 1; i< audblk->ncplsubnd; i++) {
		audblk->cplbndstrc[i] = bitstream_get (1);
		audblk->ncplbnd -= audblk->cplbndstrc[i];
	    }
	}
    }

    if (audblk->cplinu) {
	int j, cplcoe;

	cplcoe = 0;
	for (i = 0; i < state->nfchans; i++)
	    if (audblk->chincpl[i])
		if (bitstream_get (1)) {	// cplcoe
		    cplcoe = 1;
		    audblk->mstrcplco[i] = bitstream_get (2);
		    for (j = 0; j < audblk->ncplbnd; j++) {
			audblk->cplcoexp[i][j] = bitstream_get (4);
			audblk->cplcomant[i][j] = bitstream_get (4);
		    }
		}
	if ((state->acmod == 0x2) && audblk->phsflginu && cplcoe)
	    for (j = 0; j < audblk->ncplbnd; j++)
		audblk->phsflg[j] = bitstream_get (1);
    }

    if ((state->acmod == 0x2) && (bitstream_get (1))) {	// rematstr
	if ((audblk->cplbegf > 2) || (audblk->cplinu == 0))
	    for (i = 0; i < 4; i++) 
		audblk->rematflg[i] = bitstream_get (1);
	else if ((audblk->cplbegf == 0) && audblk->cplinu)
	    for (i = 0; i < 2; i++)
		audblk->rematflg[i] = bitstream_get (1);
	else if ((audblk->cplbegf <= 2) && audblk->cplinu)
	    for(i = 0; i < 3; i++)
		audblk->rematflg[i] = bitstream_get (1);
    }

    cplexpstr = EXP_REUSE;
    lfeexpstr = EXP_REUSE;
    if (audblk->cplinu)
	cplexpstr = bitstream_get (2);
    for (i = 0; i < state->nfchans; i++)
	chexpstr[i] = bitstream_get (2);
    if (state->lfeon) 
	lfeexpstr = bitstream_get (1);

    for (i = 0; i < state->nfchans; i++)
	if (chexpstr[i] != EXP_REUSE) {
	    if (audblk->cplinu && audblk->chincpl[i])
		audblk->endmant[i] = audblk->cplstrtmant;
	    else {
		audblk->chbwcod[i] = bitstream_get (6);
		audblk->endmant[i] = audblk->chbwcod[i] * 3 + 73;
	    }
	}

    do_bit_alloc = 0;

    if (cplexpstr != EXP_REUSE) {
	int cplabsexp, ncplgrps;

	do_bit_alloc = 1;
	ncplgrps = ((audblk->cplendmant - audblk->cplstrtmant) /
		    (3 << (cplexpstr - 1)));
	cplabsexp = bitstream_get (4) << 1;
	if (parse_exponents (cplexpstr, ncplgrps, cplabsexp,
			     audblk->cpl_exp + audblk->cplstrtmant))
	    return 1;
    }
    for (i = 0; i < state->nfchans; i++)
	if (chexpstr[i] != EXP_REUSE) {
	    int grp_size, nchgrps;

	    do_bit_alloc = 1;
	    grp_size = 3 * (1 << (chexpstr[i] - 1));
	    nchgrps = (audblk->endmant[i] - 1 + (grp_size - 3)) / grp_size;
	    audblk->fbw_exp[i][0] = bitstream_get (4);
	    if (parse_exponents (chexpstr[i], nchgrps, audblk->fbw_exp[i][0],
				 audblk->fbw_exp[i] + 1))
		return 1;
	    bitstream_get (2);	// gainrng
	}
    if (lfeexpstr != EXP_REUSE) {
	do_bit_alloc = 1;
	audblk->lfe_exp[0] = bitstream_get (4);
	if (parse_exponents (lfeexpstr, 2, audblk->lfe_exp[0],
			     audblk->lfe_exp + 1))
	    return 1;
    }

    if (bitstream_get (1)) {	// baie
	do_bit_alloc = 1;
	audblk->sdcycod = bitstream_get (2);
	audblk->fdcycod = bitstream_get (2);
	audblk->sgaincod = bitstream_get (2);
	audblk->dbpbcod = bitstream_get (2);
	audblk->floorcod = bitstream_get (3);
    }
    if (bitstream_get (1)) {	//snroffste
	do_bit_alloc = 1;
	audblk->csnroffst = bitstream_get (6);
	if (audblk->cplinu) {
	    audblk->cplba.fsnroffst = bitstream_get (4);
	    audblk->cplba.fgaincod = bitstream_get (3);
	}
	for (i = 0; i < state->nfchans; i++) {
	    audblk->ba[i].fsnroffst = bitstream_get (4);
	    audblk->ba[i].fgaincod = bitstream_get (3);
	}
	if (state->lfeon) {
	    audblk->lfeba.fsnroffst = bitstream_get (4);
	    audblk->lfeba.fgaincod = bitstream_get (3);
	}
    }
    if ((audblk->cplinu) && (bitstream_get (1))) {	// cplleake
	do_bit_alloc = 1;
	audblk->cplfleak = bitstream_get (3);
	audblk->cplsleak = bitstream_get (3);
    }

    if (bitstream_get (1)) {	// deltbaie
	do_bit_alloc = 1;
	if (audblk->cplinu)
	    audblk->cplba.deltbae = bitstream_get (2);
	for (i = 0; i < state->nfchans; i++)
	    audblk->ba[i].deltbae = bitstream_get (2);
	if (audblk->cplinu && (audblk->cplba.deltbae == DELTA_BIT_NEW) &&
	    parse_deltba (audblk->cplba.deltba))
	    return 1;
	for (i = 0; i < state->nfchans; i++)
	    if ((audblk->ba[i].deltbae == DELTA_BIT_NEW) &&
		parse_deltba (audblk->ba[i].deltba))
		return 1;
    }

    if (do_bit_alloc) {
	if (zero_snr_offsets (state, audblk)) {
	    memset (audblk->cpl_bap, 0, sizeof (audblk->cpl_bap));
	    memset (audblk->fbw_bap, 0, sizeof (audblk->fbw_bap));
	    memset (audblk->lfe_bap, 0, sizeof (audblk->lfe_bap));
	} else {
	    static int bndtab[16] = {31, 35, 37, 39, 41, 42, 43, 44,
				     45, 45, 46, 46, 47, 47, 48, 48};

	    if (audblk->cplinu)
		bit_allocate (state->fscod, audblk, &audblk->cplba,
			      bndtab[audblk->cplbegf], audblk->cplstrtmant,
			      audblk->cplendmant,
			      2304 - (audblk->cplfleak << 8),
			      2304 - (audblk->cplsleak << 8),
			      audblk->cpl_exp, audblk->cpl_bap);
	    for (i = 0; i < state->nfchans; i++)
		bit_allocate (state->fscod, audblk, audblk->ba + i, 0, 0,
			      audblk->endmant[i], 0, 0,
			      audblk->fbw_exp[i], audblk->fbw_bap[i]);
	    if (state->lfeon) {
		audblk->lfeba.deltbae = DELTA_BIT_NONE;
		bit_allocate (state->fscod, audblk, &audblk->lfeba, 0, 0, 7,
			      0, 0, audblk->lfe_exp, audblk->lfe_bap);
	    }
	}
    }

    if (bitstream_get (1)) {	// skiple
	i = bitstream_get (9);	// skipl
	while (i--)
	    bitstream_get (8);
    }

    stats_print_audblk(state,audblk);
    return 0;
}
