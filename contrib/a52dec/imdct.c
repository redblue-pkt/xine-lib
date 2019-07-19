/*
 * imdct.c
 * Copyright (C) 2000-2002 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * The ifft algorithms in this file have been largely inspired by Dan
 * Bernstein's work, djbfft, available at http://cr.yp.to/djbfft.html
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

#include "config.h"

#include <math.h>
#include <stdio.h>
#ifdef LIBA52_DJBFFT
#include <fftc4.h>
#endif
#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795029
#endif
#include <inttypes.h>

#include "a52.h"
#include "a52_internal.h"
#include <xine/xineutils.h>

static const uint8_t fftorder[] = {
      0,128, 64,192, 32,160,224, 96, 16,144, 80,208,240,112, 48,176,
      8,136, 72,200, 40,168,232,104,248,120, 56,184, 24,152,216, 88,
      4,132, 68,196, 36,164,228,100, 20,148, 84,212,244,116, 52,180,
    252,124, 60,188, 28,156,220, 92, 12,140, 76,204,236,108, 44,172,
      2,130, 66,194, 34,162,226, 98, 18,146, 82,210,242,114, 50,178,
     10,138, 74,202, 42,170,234,106,250,122, 58,186, 26,154,218, 90,
    254,126, 62,190, 30,158,222, 94, 14,142, 78,206,238,110, 46,174,
      6,134, 70,198, 38,166,230,102,246,118, 54,182, 22,150,214, 86
};

static inline void ifft2 (complex_t * buf)
{
    sample_t r, i;

    r = buf[0].real;
    i = buf[0].imag;
    buf[0].real += buf[1].real;
    buf[0].imag += buf[1].imag;
    buf[1].real = r - buf[1].real;
    buf[1].imag = i - buf[1].imag;
}

static inline void ifft4 (complex_t * buf)
{
    sample_t tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;

    tmp1 = buf[0].real + buf[1].real;
    tmp2 = buf[3].real + buf[2].real;
    tmp3 = buf[0].imag + buf[1].imag;
    tmp4 = buf[2].imag + buf[3].imag;
    tmp5 = buf[0].real - buf[1].real;
    tmp6 = buf[0].imag - buf[1].imag;
    tmp7 = buf[2].imag - buf[3].imag;
    tmp8 = buf[3].real - buf[2].real;

    buf[0].real = tmp1 + tmp2;
    buf[0].imag = tmp3 + tmp4;
    buf[2].real = tmp1 - tmp2;
    buf[2].imag = tmp3 - tmp4;
    buf[1].real = tmp5 + tmp7;
    buf[1].imag = tmp6 + tmp8;
    buf[3].real = tmp5 - tmp7;
    buf[3].imag = tmp6 - tmp8;
}

/* basic radix-2 ifft butterfly */
#define BUTTERFLY_0(t0,t1,W0,W1,d0,d1) do { \
    t0 = MUL (W1, d1) + MUL (W0, d0); \
    t1 = MUL (W0, d1) - MUL (W1, d0); \
} while (0)
#ifdef LIBA52_FIXED
#  define BUTTERFLY_BIAS(t0,t1,W0,W1,d0,d1,bias) do { \
    t0 = MUL (d1, W1) + MUL (d0, W0); \
    t1 = MUL (d1, W0) - MUL (d0, W1); \
    (void)bias; \
   } while (0)
#else
#  define BUTTERFLY_BIAS(t0,t1,W0,W1,d0,d1,bias) do { \
    t0 = MUL (d1, W1) + MUL (d0, W0) + bias; \
    t1 = MUL (d1, W0) - MUL (d0, W1) + bias; \
   } while (0)
#endif

/* the basic split-radix ifft butterfly */
#define BUTTERFLY(a0,a1,a2,a3,wr,wi) do {	\
    BUTTERFLY_0 (tmp5, tmp6, wr, wi, a2.real, a2.imag); \
    BUTTERFLY_0 (tmp8, tmp7, wr, wi, a3.imag, a3.real); \
    tmp1 = tmp5 + tmp7;				\
    tmp2 = tmp6 + tmp8;				\
    tmp3 = tmp6 - tmp8;				\
    tmp4 = tmp7 - tmp5;				\
    a2.real = a0.real - tmp1;			\
    a2.imag = a0.imag - tmp2;			\
    a3.real = a1.real - tmp3;			\
    a3.imag = a1.imag - tmp4;			\
    a0.real += tmp1;				\
    a0.imag += tmp2;				\
    a1.real += tmp3;				\
    a1.imag += tmp4;				\
} while (0)

/* split-radix ifft butterfly, specialized for wr=1 wi=0 */

#define BUTTERFLY_ZERO(a0,a1,a2,a3) do {	\
    tmp1 = a2.real + a3.real;			\
    tmp2 = a2.imag + a3.imag;			\
    tmp3 = a2.imag - a3.imag;			\
    tmp4 = a3.real - a2.real;			\
    a2.real = a0.real - tmp1;			\
    a2.imag = a0.imag - tmp2;			\
    a3.real = a1.real - tmp3;			\
    a3.imag = a1.imag - tmp4;			\
    a0.real += tmp1;				\
    a0.imag += tmp2;				\
    a1.real += tmp3;				\
    a1.imag += tmp4;				\
} while (0)

/* split-radix ifft butterfly, specialized for wr=wi */

#define BUTTERFLY_HALF(a0,a1,a2,a3,w) do {	\
    tmp5 = MUL (a2.real + a2.imag, w);		\
    tmp6 = MUL (a2.imag - a2.real, w);		\
    tmp7 = MUL (a3.real - a3.imag, w);		\
    tmp8 = MUL (a3.imag + a3.real, w);		\
    tmp1 = tmp5 + tmp7;				\
    tmp2 = tmp6 + tmp8;				\
    tmp3 = tmp6 - tmp8;				\
    tmp4 = tmp7 - tmp5;				\
    a2.real = a0.real - tmp1;			\
    a2.imag = a0.imag - tmp2;			\
    a3.real = a1.real - tmp3;			\
    a3.imag = a1.imag - tmp4;			\
    a0.real += tmp1;				\
    a0.imag += tmp2;				\
    a1.real += tmp3;				\
    a1.imag += tmp4;				\
} while (0)

static inline void ifft8 (a52_state_t *a52, complex_t * buf)
{
    sample_t tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;

    ifft4 (buf);
    ifft2 (buf + 4);
    ifft2 (buf + 6);
    BUTTERFLY_ZERO (buf[0], buf[2], buf[4], buf[6]);
    BUTTERFLY_HALF (buf[1], buf[3], buf[5], buf[7], a52->roots16[1]);
}

static void ifft_pass (complex_t * buf, sample_t * weight, int n)
{
    complex_t * buf1;
    complex_t * buf2;
    complex_t * buf3;
    sample_t  * wi;
    sample_t tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;
    int i;

    buf++;
    buf1 = buf + n;
    buf2 = buf + 2 * n;
    buf3 = buf + 3 * n;

    BUTTERFLY_ZERO (buf[-1], buf1[-1], buf2[-1], buf3[-1]);

    i = n - 1;
    wi = weight + n - 2;

    do {
	BUTTERFLY (buf[0], buf1[0], buf2[0], buf3[0], weight[0], wi[0]);
	buf++;
	buf1++;
	buf2++;
	buf3++;
	weight++;
	wi--;
    } while (--i);
}

static void ifft16 (a52_state_t *a52, complex_t * buf)
{
    ifft8 (a52, buf);
    ifft4 (buf + 8);
    ifft4 (buf + 12);
    ifft_pass (buf, a52->roots16, 4);
}

static void ifft32 (a52_state_t *a52, complex_t * buf)
{
    ifft16 (a52, buf);
    ifft8 (a52, buf + 16);
    ifft8 (a52, buf + 24);
    ifft_pass (buf, a52->roots32, 8);
}

#ifdef LIBA52_DJBFFT
static void ifft64_djbfft (a52_state_t *a52, complex_t * buf) {
  (void)a52;
#  ifndef LIBA52_DOUBLE
  fftc4_un64 (buf);
#  else
  fftc8_un64 (buf);
#  endif
}

static void ifft128_djbfft (a52_state_t *a52, complex_t * buf) {
  (void)a52;
#  ifndef LIBA52_DOUBLE
  fftc4_un128 (buf);
#  else
  fftc8_un128 (buf);
#  endif
}
#endif

static void ifft64_c (a52_state_t *a52, complex_t * buf)
{
    ifft32 (a52, buf);
    ifft16 (a52, buf + 32);
    ifft16 (a52, buf + 48);
    ifft_pass (buf, a52->roots64, 16);
}

static void ifft128_c (a52_state_t *a52, complex_t * buf)
{
    ifft32 (a52, buf);
    ifft16 (a52, buf + 32);
    ifft16 (a52, buf + 48);
    ifft_pass (buf, a52->roots64, 16);

    ifft32 (a52, buf + 64);
    ifft32 (a52, buf + 96);
    ifft_pass (buf, a52->roots128, 32);
}

void a52_imdct_512 (a52_state_t *a52, sample_t * data, sample_t * delay, sample_t bias)
{
    int i, k;
    sample_t t_r, t_i, a_r, a_i, b_r, b_i, w_1, w_2;
    const sample_t * window = a52->a52_imdct_window;
    complex_t buf[128];
	
    for (i = 0; i < 128; i++) {
	k = fftorder[i];
        t_r = a52->pre1[i].real;
        t_i = a52->pre1[i].imag;
        BUTTERFLY_0 (buf[i].real, buf[i].imag, t_r, t_i, data[k], data[255-k]);
    }

    a52->ifft128 (a52, buf);

    /* Post IFFT complex multiply plus IFFT complex conjugate*/
    /* Window and convert to real valued signal */
    for (i = 0; i < 64; i++) {
	/* y[n] = z[n] * (xcos1[n] + j * xsin1[n]) ; */
        t_r = a52->post1[i].real;
        t_i = a52->post1[i].imag;

	BUTTERFLY_0 (a_r, a_i, t_i, t_r, buf[i].imag, buf[i].real);
	BUTTERFLY_0 (b_r, b_i, t_r, t_i, buf[127-i].imag, buf[127-i].real);

	w_1 = window[2*i];
	w_2 = window[255-2*i];
        BUTTERFLY_BIAS (data[255-2*i], data[2*i], w_2, w_1, a_r, delay[2*i], bias);
	delay[2*i] = a_i;

	w_1 = window[2*i+1];
	w_2 = window[254-2*i];
        BUTTERFLY_BIAS (data[2*i+1], data[254-2*i], w_1, w_2, b_r, delay[2*i+1], bias);
	delay[2*i+1] = b_i;
    }
}

void a52_imdct_256 (a52_state_t *a52, sample_t * data, sample_t * delay, sample_t bias)
{
    int i, k;
    sample_t t_r, t_i, a_r, a_i, b_r, b_i, c_r, c_i, d_r, d_i, w_1, w_2;
    const sample_t * window = a52->a52_imdct_window;
    complex_t buf1[64], buf2[64];

    /* Pre IFFT complex multiply plus IFFT cmplx conjugate */
    for (i = 0; i < 64; i++) {
	k = fftorder[i];
        t_r = a52->pre2[i].real;
        t_i = a52->pre2[i].imag;
        BUTTERFLY_0 (buf1[i].real, buf1[i].imag, t_r, t_i, data[k], data[254-k]);
        BUTTERFLY_0 (buf2[i].real, buf2[i].imag, t_r, t_i, data[k+1], data[255-k]);
    }

    a52->ifft64 (a52, buf1);
    a52->ifft64 (a52, buf2);

    /* Post IFFT complex multiply */
    /* Window and convert to real valued signal */
    for (i = 0; i < 32; i++) {
	/* y1[n] = z1[n] * (xcos2[n] + j * xs in2[n]) ; */ 
        t_r = a52->post2[i].real;
        t_i = a52->post2[i].imag;

        BUTTERFLY_0 (a_r, a_i, t_i, t_r, buf1[i].imag, buf1[i].real);
        BUTTERFLY_0 (b_r, b_i, t_r, t_i, buf1[63-i].imag, buf1[63-i].real);
        BUTTERFLY_0 (c_r, c_i, t_i, t_r, buf2[i].imag, buf2[i].real);
        BUTTERFLY_0 (d_r, d_i, t_r, t_i, buf2[63-i].imag, buf2[63-i].real);

	w_1 = window[2*i];
	w_2 = window[255-2*i];
        BUTTERFLY_BIAS (data[255-2*i], data[2*i], w_2, w_1, a_r, delay[2*i], bias);
	delay[2*i] = c_i;

	w_1 = window[128+2*i];
	w_2 = window[127-2*i];
        BUTTERFLY_BIAS (data[128+2*i], data[127-2*i], w_1, w_2, a_i, delay[127-2*i], bias);
	delay[127-2*i] = c_r;

	w_1 = window[2*i+1];
	w_2 = window[254-2*i];
        BUTTERFLY_BIAS (data[254-2*i], data[2*i+1], w_2, w_1, b_i, delay[2*i+1], bias);
	delay[2*i+1] = d_r;

	w_1 = window[129+2*i];
	w_2 = window[126-2*i];
        BUTTERFLY_BIAS (data[129+2*i], data[126-2*i], w_1, w_2, b_r, delay[126-2*i], bias);
	delay[126-2*i] = d_i;
    }
}

static double besselI0 (double x)
{
    double bessel = 1;
    int i = 100;

    do
	bessel = bessel * x / (i * i) + 1;
    while (--i);
    return bessel;
}

void a52_imdct_init (a52_state_t *a52, uint32_t mm_accel)
{
    int i, k;
    double sum;

    /* compute imdct window - kaiser-bessel derived window, alpha = 5.0 */
    sum = 0;
    for (i = 0; i < 256; i++) {
	sum += besselI0 (i * (256 - i) * (5 * M_PI / 256) * (5 * M_PI / 256));
        a52->a52_imdct_window[i] = sum;
    }
    sum++;
    for (i = 0; i < 256; i++)
        a52->a52_imdct_window[i] = SAMPLE (sqrt (a52->a52_imdct_window[i] / sum));

    for (i = 0; i < 3; i++)
        a52->roots16[i] = SAMPLE (cos ((M_PI / 8) * (i + 1)));

    for (i = 0; i < 7; i++)
        a52->roots32[i] = SAMPLE (cos ((M_PI / 16) * (i + 1)));

    for (i = 0; i < 15; i++)
        a52->roots64[i] = SAMPLE (cos ((M_PI / 32) * (i + 1)));

    for (i = 0; i < 31; i++)
        a52->roots128[i] = SAMPLE (cos ((M_PI / 64) * (i + 1)));

    for (i = 0; i < 64; i++) {
	k = fftorder[i] / 2 + 64;
        a52->pre1[i].real = SAMPLE (cos ((M_PI / 256) * (k - 0.25)));
        a52->pre1[i].imag = SAMPLE (sin ((M_PI / 256) * (k - 0.25)));
    }

    for (i = 64; i < 128; i++) {
	k = fftorder[i] / 2 + 64;
        a52->pre1[i].real = SAMPLE (-cos ((M_PI / 256) * (k - 0.25)));
        a52->pre1[i].imag = SAMPLE (-sin ((M_PI / 256) * (k - 0.25)));
    }

    for (i = 0; i < 64; i++) {
        a52->post1[i].real = SAMPLE (cos ((M_PI / 256) * (i + 0.5)));
        a52->post1[i].imag = SAMPLE (sin ((M_PI / 256) * (i + 0.5)));
    }

    for (i = 0; i < 64; i++) {
	k = fftorder[i] / 4;
        a52->pre2[i].real = SAMPLE (cos ((M_PI / 128) * (k - 0.25)));
        a52->pre2[i].imag = SAMPLE (sin ((M_PI / 128) * (k - 0.25)));
    }

    for (i = 0; i < 32; i++) {
        a52->post2[i].real = SAMPLE (cos ((M_PI / 128) * (i + 0.5)));
        a52->post2[i].imag = SAMPLE (sin ((M_PI / 128) * (i + 0.5)));
    }

#ifndef LIBA52_DJBFFT
    (void)mm_accel;
#else
    if (mm_accel & MM_ACCEL_DJBFFT) {
	fprintf (stderr, "liba52:Using djbfft for IMDCT transform\n");
        a52->ifft128 = ifft128_djbfft;
        a52->ifft64 = ifft64_djbfft;
    } else
#endif
    {
        a52->ifft128 = ifft128_c;
        a52->ifft64 = ifft64_c;
    }
}
