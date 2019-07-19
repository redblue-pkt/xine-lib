/*
 * a52_internal.h
 * Copyright (C) 2000-2002 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of a52dec, a free ATSC A-52 stream decoder.
 * See http://liba52.sourceforge.net/ for updates.
 *
 * a52dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * a52dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

typedef struct complex_s {
    sample_t real;
    sample_t imag;
} complex_t;

typedef struct {
    uint8_t bai;		/* fine SNR offset, fast gain */
    uint8_t deltbae;		/* delta bit allocation exists */
    int8_t deltba[50];		/* per-band delta bit allocation */
} ba_t;

typedef struct {
    uint8_t exp[256];		/* decoded channel exponents */
    int8_t bap[256];		/* derived channel bit allocation */
} expbap_t;

struct a52_state_s {
    uint8_t fscod;		/* sample rate */
    uint8_t halfrate;		/* halfrate factor */
    uint8_t acmod;		/* coded channels */
    uint8_t lfeon;		/* coded lfe channel */
    sample_t clev;		/* centre channel mix level */
    sample_t slev;		/* surround channels mix level */

    int output;			/* type of output */
    sample_t level;		/* output level */
    sample_t bias;		/* output bias */

    int dynrnge;		/* apply dynamic range */
    sample_t dynrng;		/* dynamic range */
    void * dynrngdata;		/* dynamic range callback funtion and data */
    sample_t (* dynrngcall) (sample_t range, void * dynrngdata);

    uint8_t chincpl;		/* channel coupled */
    uint8_t phsflginu;		/* phase flags in use (stereo only) */
    uint8_t cplstrtmant;	/* coupling channel start mantissa */
    uint8_t cplendmant;		/* coupling channel end mantissa */
    uint32_t cplbndstrc;	/* coupling band structure */
    sample_t cplco[5][18];	/* coupling coordinates */

    /* derived information */
    uint8_t cplstrtbnd;		/* coupling start band (for bit allocation) */
    uint8_t ncplbnd;		/* number of coupling bands */

    uint8_t rematflg;		/* stereo rematrixing */

    uint8_t endmant[5];		/* channel end mantissa */

    uint16_t bai;		/* bit allocation information */

    uint32_t * buffer_start;
    uint16_t lfsr_state;	/* dither state */
    uint32_t bits_left;
    uint32_t current_word;

    uint8_t csnroffst;		/* coarse SNR offset */
    ba_t cplba;			/* coupling bit allocation parameters */
    ba_t ba[5];			/* channel bit allocation parameters */
    ba_t lfeba;			/* lfe bit allocation parameters */

    uint8_t cplfleak;		/* coupling fast leak init */
    uint8_t cplsleak;		/* coupling slow leak init */

    expbap_t cpl_expbap;
    expbap_t fbw_expbap[5];
    expbap_t lfe_expbap;

    sample_t * samples;
    void * samples_base;
    int downmixed;

    /* these used to be writable static data in imdct.c. */
    /* Root values for IFFT */
    sample_t roots16[3];
    sample_t roots32[7];
    sample_t roots64[15];
    sample_t roots128[31];
    /* Twiddle factors for IMDCT */
    complex_t pre1[128];
    complex_t post1[64];
    complex_t pre2[64];
    complex_t post2[32];
    /* */
    sample_t a52_imdct_window[256];
    /* */
    void (* ifft128) (a52_state_t *a52, complex_t * buf);
    void (* ifft64) (a52_state_t *a52, complex_t * buf);
};

#define LEVEL_PLUS6DB 2.0
#define LEVEL_PLUS3DB 1.4142135623730951
#define LEVEL_3DB 0.7071067811865476
#define LEVEL_45DB 0.5946035575013605
#define LEVEL_6DB 0.5

#define EXP_REUSE (0)
#define EXP_D15   (1)
#define EXP_D25   (2)
#define EXP_D45   (3)

#define DELTA_BIT_REUSE (0)
#define DELTA_BIT_NEW (1)
#define DELTA_BIT_NONE (2)
#define DELTA_BIT_RESERVED (3)

void a52_bit_allocate (a52_state_t * state, ba_t * ba, int bndstart,
		       int start, int end, int fastleak, int slowleak,
		       expbap_t * expbap);

int a52_downmix_init (int input, int flags, sample_t * level,
		      sample_t clev, sample_t slev);
int a52_downmix_coeff (sample_t * coeff, int acmod, int output, sample_t level,
		       sample_t clev, sample_t slev);
void a52_downmix (sample_t * samples, int acmod, int output, sample_t bias,
		  sample_t clev, sample_t slev);
void a52_upmix (sample_t * samples, int acmod, int output);

void a52_imdct_init (a52_state_t *a52, uint32_t mm_accel);
void a52_imdct_256 (a52_state_t *a52, sample_t * data, sample_t * delay, sample_t bias);
void a52_imdct_512 (a52_state_t *a52, sample_t * data, sample_t * delay, sample_t bias);

#define ROUND(x) ((int)((x) + ((x) > 0 ? 0.5 : -0.5)))


#ifndef LIBA52_FIXED

typedef sample_t quantizer_t;
#  define SAMPLE(x) (x)
#  define LEVEL(x) (x)
#  define MUL(a,b) ((a) * (b))
#  define MUL_L(a,b) ((a) * (b))
#  define MUL_C(a,b) ((a) * (b))
#  define DIV(a,b) ((a) / (b))

static inline sample_t minus3db (sample_t v) {
  return LEVEL_3DB * v;
}

static inline sample_t minus6db (sample_t v) {
  return LEVEL_6DB * v;
}

static inline sample_t plus6db (sample_t v) {
  return LEVEL_PLUS6DB * v;
}

#else /* LIBA52_FIXED */

typedef int16_t quantizer_t;
#  define SAMPLE(x) (sample_t)((x) * (1 << 30))
#  define LEVEL(x) (sample_t)((x) * (1 << 26))
#  if 0
#    define MUL(a,b) ((int)(((int64_t)(a) * (b) + (1 << 29)) >> 30))
#    define MUL_L(a,b) ((int)(((int64_t)(a) * (b) + (1 << 25)) >> 26))
#  elif 1
#    define MUL(a,b) ({ \
        int32_t _ta=(a), _tb=(b), _tc; \
        _tc = (_ta & 0xffff) * (_tb >> 16) + (_ta >> 16) * (_tb & 0xffff); \
        (int32_t)(((_tc >> 14)) + (((_ta >> 16) * (_tb >> 16)) << 2 )); \
     })
#    define MUL_L(a,b) ({ \
        int32_t _ta=(a), _tb=(b), _tc; \
        _tc = (_ta & 0xffff) * (_tb >> 16) + (_ta >> 16) * (_tb & 0xffff); \
        (int32_t)((_tc >> 10) + (((_ta >> 16) * (_tb >> 16)) << 6)); \
     })
#  else
#    define MUL(a,b) (((a) >> 15) * ((b) >> 15))
#    define MUL_L(a,b) (((a) >> 13) * ((b) >> 13))
#  endif
#  define MUL_C(a,b) MUL_L (a, LEVEL (b))
#  define DIV(a,b) ((((int64_t)LEVEL (a)) << 26) / (b))

static inline sample_t minus3db (sample_t v) {
  return v - (v >> 8) * 0x4b;
}

static inline sample_t minus6db (sample_t v) {
  return v >> 1;
}

static inline sample_t plus6db (sample_t v) {
  return v << 1;
}

#endif
