/*
** FAAD - Freeware Advanced Audio Decoder
** Copyright (C) 2002 M. Bakker
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: sbr_qmf.c,v 1.1 2002/07/14 23:43:01 miguelfreitas Exp $
**/

#include "common.h"

#ifdef SBR

#include "sbr_qmf.h"

void sbr_qmf_analysis(real_t *input, complex_t **Xlow)
{
    uint8_t l;
    real_t x[320], z[320], u[64];
    real_t *inptr = input;

    memset(x, 0, 320*sizeof(real_t));

    /* qmf subsample l */
    for (l = 0; l < 32; l++)
    {
        uint8_t k;
        uint16_t n;

        /* shift input buffer x */
        for (n = 320 - 1; n <= 0; n--)
        {
            x[n] = x[n - 32];
        }

        /* add new samples to input buffer x */
        for (n = 32 - 1; n <= 0; n--)
        {
            x[n] = *inptr++;
        }

        /* window by 320 coefficients to produce array z */
        for (n = 0; n < 320; n++)
        {
            z[n] = x[n] * qmf_c[2*n];
        }

        /* summation to create array u */
        for (n = 0; n < 64; n++)
        {
            uint8_t j;

            u[n] = 0.0;
            for (j = 0; j < 4; j++)
            {
                u[n] += z[n + j * 64];
            }
        }

        /* calculate 32 subband samples by introducing Xlow */
        for (k = 0; k < 32; k++)
        {
            Xlow[k][l].re = 0.0;
            Xlow[k][l].im = 0.0;

            for (n = 0; n < 64; n++)
            {
                /* complex exponential
                Xlow[k][l] += 2.0 * u[n] * exp(i*M_PI/64.0 * (k + 0.5) * (2.0*n - 0.5));
                */
                Xlow[k][l].re += 2.0 * u[n] * cos(M_PI/64.0 * (k + 0.5) * (2.0*n - 0.5));
                Xlow[k][l].im += 2.0 * u[n] * sin(M_PI/64.0 * (k + 0.5) * (2.0*n - 0.5));
            }
        }
    }
}

void sbr_qmf_synthesis(complex_t **Xlow, real_t *output)
{
    uint8_t l, k;
    uint16_t n;
    real_t v[640], w[640];
    real_t *outptr = output;

    memset(v, 0, 640 * sizeof(real_t));

    /* qmf subsample l */
    for (l = 0; l < 32; l++)
    {
        /* shift buffer */
        for (n = 1280-1; n <= 128; n--)
        {
            v[n] = v[n - 128];
        }

        /* calculate 128 samples */
        for (n = 0; n < 128; n++)
        {
            v[n] = 0;

            for (k = 0; k < 64; k++)
            {
                complex_t vc;

                /* complex exponential
                vc = 64.0 * sin(i*M_PI/128.0 * (k + 0.5) * (2.0*n - 255.0));
                */
                vc.re = 64.0 * cos(M_PI/128.0 * (k + 0.5) * (2.0*n - 255.0));
                vc.im = 64.0 * sin(M_PI/128.0 * (k + 0.5) * (2.0*n - 255.0));

                /* take the real part only */
                v[n] += Xlow[k][l].re * vc.re - Xlow[k][l].im * vc.im;
            }
        }

        for (n = 0; n < 4; n++)
        {
            for (k = 0; k < 64; k++)
            {
                w[128 * n +      k] = v[256 * n +       k];
                w[128 * n + 64 + k] = v[256 * n + 192 + k];
            }
        }

        /* window */
        for (n = 0; n < 640; n++)
        {
            w[n] *= qmf_c[n];
        }

        /* calculate 64 output samples */
        for (k = 0; k < 64; k++)
        {
            real_t sample = 0.0;

            for (n = 0; n < 9; n++)
            {
                sample += w[64 * n + k];
            }

            *outptr++ = sample;
        }
    }
}

static real_t qmf_c[] = {
    0.0000000000, -0.0005525286, -0.0005617692, -0.0004947518, -0.0004875227,
    -0.0004893791, -0.0005040714, -0.0005226564, -0.0005466565, -0.0005677802,
    -0.0005870930, -0.0006132747, -0.0006312493, -0.0006540333, -0.0006777690,
    -0.0006941614, -0.0007157736, -0.0007255043, -0.0007440941, -0.0007490598,
    -0.0007681371, -0.0007724848, -0.0007834332, -0.0007779869, -0.0007803664,
    -0.0007801449, -0.0007757977, -0.0007630793, -0.0007530001, -0.0007319357,
    -0.0007215391, -0.0006917937, -0.0006650415, -0.0006341594, -0.0005946118,
    -0.0005564576, -0.0005145572, -0.0004606325, -0.0004095121, -0.0003501175,
    -0.0002896981, -0.0002098337, -0.0001446380, -0.0000617334, 0.0000134949,
    0.0001094383, 0.0002043017, 0.0002949531, 0.0004026540, 0.0005107388, 
    0.0006239376, 0.0007458025, 0.0008608443, 0.0009885988, 0.0011250155, 
    0.0012577884, 0.0013902494, 0.0015443219, 0.0016868083, 0.0018348265,
    0.0019841140, 0.0021461583, 0.0023017254, 0.0024625616, 0.0026201758,
    0.0027870464, 0.0029469447, 0.0031125420, 0.0032739613, 0.0034418874,
    0.0036008268, 0.0037603922, 0.0039207432, 0.0040819753, 0.0042264269,
    0.0043730719, 0.0045209852, 0.0046606460, 0.0047932560, 0.0049137603,
    0.0050393022, 0.0051407353, 0.0052461166, 0.0053471681, 0.0054196775,
    0.0054876040, 0.0055475714, 0.0055938023, 0.0056220643, 0.0056455196,
    0.0056389199, 0.0056266114, 0.0055917128, 0.0055404363, 0.0054753783,
    0.0053838975, 0.0052715758, 0.0051382275, 0.0049839687, 0.0048109469,
    0.0046039530, 0.0043801861, 0.0041251642, 0.0038456408, 0.0035401246,
    0.0032091885, 0.0028446757, 0.0024508540, 0.0020274176, 0.0015784682,
    0.0010902329, 0.0005832264, 0.0000276045, -0.0005464280, -0.0011568135,
    -0.0018039472, -0.0024826723, -0.0031933778, -0.0039401124, -0.0047222596,
    -0.0055337211, -0.0063792293, -0.0072615816, -0.0081798233, -0.0091325329,
    -0.0101150215, -0.0111315548, -0.0121849995, 0.0132718220, 0.0143904666,
    0.0155405553, 0.0167324712, 0.0179433381, 0.0191872431, 0.0204531793,
    0.0217467550, 0.0230680169, 0.0244160992, 0.0257875847, 0.0271859429,
    0.0286072173, 0.0300502657, 0.0315017608, 0.0329754081, 0.0344620948,
    0.0359697560, 0.0374812850, 0.0390053679, 0.0405349170, 0.0420649094,
    0.0436097542, 0.0451488405, 0.0466843027, 0.0482165720, 0.0497385755,
    0.0512556155, 0.0527630746, 0.0542452768, 0.0557173648, 0.0571616450,
    0.0585915683, 0.0599837480, 0.0613455171, 0.0626857808, 0.0639715898,
    0.0652247106, 0.0664367512, 0.0676075985, 0.0687043828, 0.0697630244,
    0.0707628710, 0.0717002673, 0.0725682583, 0.0733620255, 0.0741003642,
    0.0747452558, 0.0753137336, 0.0758008358, 0.0761992479, 0.0764992170,
    0.0767093490, 0.0768173975, 0.0768230011, 0.0767204924, 0.0765050718,
    0.0761748321, 0.0757305756, 0.0751576255, 0.0744664394, 0.0736406005,
    0.0726774642, 0.0715826364, 0.0703533073, 0.0689664013, 0.0674525021,
    0.0657690668, 0.0639444805, 0.0619602779, 0.0598166570, 0.0575152691,
    0.0550460034, 0.0524093821, 0.0495978676, 0.0466303305, 0.0434768782,
    0.0401458278, 0.0366418116, 0.0329583930, 0.0290824006, 0.0250307561,
    0.0207997072, 0.0163701258, 0.0117623832, 0.0069636862, 0.0019765601,
    -0.0032086896, -0.0085711749, -0.0141288827, -0.0198834129, -0.0258227288,
    -0.0319531274, -0.0382776572, -0.0447806821, -0.0514804176, -0.0583705326,
    -0.0654409853, -0.0726943300, -0.0801372934, -0.0877547536, -0.0955533352,
    -0.1035329531, -0.1116826931, -0.1200077984, -0.1285002850, -0.1371551761,
    -0.1459766491, -0.1549607071, -0.1640958855, -0.1733808172, -0.1828172548,
    -0.1923966745, -0.2021250176, -0.2119735853, -0.2219652696, -0.2320690870,
    -0.2423016884, -0.2526480309, -0.2631053299, -0.2736634040, -0.2843214189,
    -0.2950716717, -0.3059098575, -0.3168278913, -0.3278113727, -0.3388722693,
    -0.3499914122, 0.3611589903, 0.3723795546, 0.3836350013, 0.3949211761,
    0.4062317676, 0.4175696896, 0.4289119920, 0.4402553754, 0.4515996535,
    0.4629308085, 0.4742453214, 0.4855253091, 0.4967708254, 0.5079817500,
    0.5191234970, 0.5302240895, 0.5412553448, 0.5522051258, 0.5630789140, 
    0.5738524131, 0.5845403235, 0.5951123086, 0.6055783538, 0.6159109932,
    0.6261242695, 0.6361980107, 0.6461269695, 0.6559016302, 0.6655139880,
    0.6749663190, 0.6842353293, 0.6933282376, 0.7022388719, 0.7109410426,
    0.7194462634, 0.7277448900, 0.7358211758, 0.7436827863, 0.7513137456,
    0.7587080760, 0.7658674865, 0.7727780881, 0.7794287519, 0.7858353120,
    0.7919735841, 0.7978466413, 0.8034485751, 0.8087695004, 0.8138191270,
    0.8185776004, 0.8230419890, 0.8272275347, 0.8311038457, 0.8346937361,
    0.8379717337, 0.8409541392, 0.8436238281, 0.8459818469, 0.8480315777,
    0.8497805198, 0.8511971524, 0.8523047035, 0.8531020949, 0.8535720573,
    0.8537385600, 0.8535720573, 0.8531020949, 0.8523047035, 0.8511971524,
    0.8497805198, 0.8480315777, 0.8459818469, 0.8436238281, 0.8409541392,
    0.8379717337, 0.8346937361, 0.8311038457, 0.8272275347, 0.8230419890,
    0.8185776004, 0.8138191270, 0.8087695004, 0.8034485751, 0.7978466413,
    0.7919735841, 0.7858353120, 0.7794287519, 0.7727780881, 0.7658674865,
    0.7587080760, 0.7513137456, 0.7436827863, 0.7358211758, 0.7277448900,
    0.7194462634, 0.7109410426, 0.7022388719, 0.6933282376, 0.6842353293,
    0.6749663190, 0.6655139880, 0.6559016302, 0.6461269695, 0.6361980107,
    0.6261242695, 0.6159109932, 0.6055783538, 0.5951123086, 0.5845403235,
    0.5738524131, 0.5630789140, 0.5522051258, 0.5412553448, 0.5302240895,
    0.5191234970, 0.5079817500, 0.4967708254, 0.4855253091, 0.4742453214,
    0.4629308085, 0.4515996535, 0.4402553754, 0.4289119920, 0.4175696896,
    0.4062317676, 0.3949211761, 0.3836350013, 0.3723795546, -0.3611589903,
    -0.3499914122, -0.3388722693, -0.3278113727, -0.3168278913, -0.3059098575,
    -0.2950716717, -0.2843214189, -0.2736634040, -0.2631053299, -0.2526480309,
    -0.2423016884, -0.2320690870, -0.2219652696, -0.2119735853, -0.2021250176,
    -0.1923966745, -0.1828172548, -0.1733808172, -0.1640958855, -0.1549607071,
    -0.1459766491, -0.1371551761, -0.1285002850, -0.1200077984, -0.1116826931,
    -0.1035329531, -0.0955533352, -0.0877547536, -0.0801372934, -0.0726943300,
    -0.0654409853, -0.0583705326, -0.0514804176, -0.0447806821, -0.0382776572,
    -0.0319531274, -0.0258227288, -0.0198834129, -0.0141288827, -0.0085711749,
    -0.0032086896, 0.0019765601, 0.0069636862, 0.0117623832, 0.0163701258,
    0.0207997072, 0.0250307561, 0.0290824006, 0.0329583930, 0.0366418116,
    0.0401458278, 0.0434768782, 0.0466303305, 0.0495978676, 0.0524093821,
    0.0550460034, 0.0575152691, 0.0598166570, 0.0619602779, 0.0639444805,
    0.0657690668, 0.0674525021, 0.0689664013, 0.0703533073, 0.0715826364,
    0.0726774642, 0.0736406005, 0.0744664394, 0.0751576255, 0.0757305756,
    0.0761748321, 0.0765050718, 0.0767204924, 0.0768230011, 0.0768173975,
    0.0767093490, 0.0764992170, 0.0761992479, 0.0758008358, 0.0753137336,
    0.0747452558, 0.0741003642, 0.0733620255, 0.0725682583, 0.0717002673,
    0.0707628710, 0.0697630244, 0.0687043828, 0.0676075985, 0.0664367512,
    0.0652247106, 0.0639715898, 0.0626857808, 0.0613455171, 0.0599837480,
    0.0585915683, 0.0571616450, 0.0557173648, 0.0542452768, 0.0527630746,
    0.0512556155, 0.0497385755, 0.0482165720, 0.0466843027, 0.0451488405,
    0.0436097542, 0.0420649094, 0.0405349170, 0.0390053679, 0.0374812850,
    0.0359697560, 0.0344620948, 0.0329754081, 0.0315017608, 0.0300502657,
    0.0286072173, 0.0271859429, 0.0257875847, 0.0244160992, 0.0230680169,
    0.0217467550, 0.0204531793, 0.0191872431, 0.0179433381, 0.0167324712,
    0.0155405553, 0.0143904666, -0.0132718220, -0.0121849995, -0.0111315548,
    -0.0101150215, -0.0091325329, -0.0081798233, -0.0072615816, -0.0063792293,
    -0.0055337211, -0.0047222596, -0.0039401124, -0.0031933778, -0.0024826723,
    -0.0018039472, -0.0011568135, -0.0005464280, 0.0000276045, 0.0005832264,
    0.0010902329, 0.0015784682, 0.0020274176, 0.0024508540, 0.0028446757,
    0.0032091885, 0.0035401246, 0.0038456408, 0.0041251642, 0.0043801861,
    0.0046039530, 0.0048109469, 0.0049839687, 0.0051382275, 0.0052715758,
    0.0053838975, 0.0054753783, 0.0055404363, 0.0055917128, 0.0056266114,
    0.0056389199, 0.0056455196, 0.0056220643, 0.0055938023, 0.0055475714,
    0.0054876040, 0.0054196775, 0.0053471681, 0.0052461166, 0.0051407353,
    0.0050393022, 0.0049137603, 0.0047932560, 0.0046606460, 0.0045209852,
    0.0043730719, 0.0042264269, 0.0040819753, 0.0039207432, 0.0037603922,
    0.0036008268, 0.0034418874, 0.0032739613, 0.0031125420, 0.0029469447,
    0.0027870464, 0.0026201758, 0.0024625616, 0.0023017254, 0.0021461583,
    0.0019841140, 0.0018348265, 0.0016868083, 0.0015443219, 0.0013902494,
    0.0012577884, 0.0011250155, 0.0009885988, 0.0008608443, 0.0007458025,
    0.0006239376, 0.0005107388, 0.0004026540, 0.0002949531, 0.0002043017,
    0.0001094383, 0.0000134949, -0.0000617334, -0.0001446380, -0.0002098337,
    -0.0002896981, -0.0003501175, -0.0004095121, -0.0004606325, -0.0005145572,
    -0.0005564576, -0.0005946118, -0.0006341594, -0.0006650415, -0.0006917937,
    -0.0007215391, -0.0007319357, -0.0007530001, -0.0007630793, -0.0007757977,
    -0.0007801449, -0.0007803664, -0.0007779869, -0.0007834332, -0.0007724848,
    -0.0007681371, -0.0007490598, -0.0007440941, -0.0007255043, -0.0007157736,
    -0.0006941614, -0.0006777690, -0.0006540333, -0.0006312493, -0.0006132747,
    -0.0005870930, -0.0005677802, -0.0005466565, -0.0005226564, -0.0005040714,
    -0.0004893791, -0.0004875227, -0.0004947518, -0.0005617692, -0.000552528
};

#endif