/*
 * Copyright (C) 2000-2002 the xine project
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
 * Color Conversion Utility Functions
 * 
 * Overview: xine's video output modules only accept YUV images from
 * video decoder modules. A video decoder can either send a planar (YV12)
 * image or a packed (YUY2) image to a video output module. However, many
 * older video codecs are RGB-based. Either each pixel is an index
 * to an RGB value in a palette table, or each pixel is encoded with
 * red, green, and blue values. In the latter case, typically either
 * 15, 16, 24, or 32 bits are used to represent a single pixel.
 *
 * If you want to use these facilities in your decoder, include the
 * xineutils.h header file. Then declare a yuv_planes_t structure. This
 * structure represents 3 non-subsampled YUV planes. "Non-subsampled"
 * means that there is a Y, U, and V sample for each pixel in the RGB
 * image, whereas YUV formats are usually subsampled so that the U and
 * V samples correspond to more than 1 pixel in the output image. When
 * you need to convert RGB values to Y, U, and V, values, use the
 * COMPUTE_Y(r, g, b), COMPUTE_U(r, g, b), COMPUTE_V(r, g, b) macros found
 * in xineutils.h
 *
 * The yuv_planes_t structure has 3 other fields: row_width and row_count,
 * which are equivalent to the frame width and height, respectively, and
 * row_stride, which is 2 bytes longer than the row_width. This is because
 * each row in each plane is actually 2 bytes longer than the width. For
 * example, if the row_width is 8 then the row_stride is 10 and each
 * plane's byte map is laid out as follows:
 *
 *   byte  0: p0  p1  p2  p3  p4  p5  p6  p7  p7  p6
 *   byte 10: p8  p9 p10 p11 p12 p13 p14 p15 p15 p14 
 *   byte 20: ...
 *
 * The extra 2 samples are necessary for the final conversion. The extra
 * 2 samples are simply mirrored from the last 2 samples on the line.
 *
 * When an image has been fully decoded into the yuv_planes_t structure,
 * call yuv444_to_yuy2() with the structure and the final (pre-allocated)
 * YUY2 buffer. xine will have already chosen the best conversion
 * function to use based on the CPU type. The YUY2 buffer will then be
 * ready to pass to the video output module.
 *
 * If your decoder is rendering an image based on an RGB palette, a good
 * strategy is to maintain a YUV palette rather than an RGB palette and
 * render the image directly in YUV.
 *
 * $Id: color.c,v 1.1 2002/07/14 01:27:03 tmmm Exp $
 */

#include "xine_internal.h"
#include "xineutils.h"

/*
 * In search of the perfect colorspace conversion formulae...
 * These are the conversion equations that xine currently uses:
 *
 *      Y  =  0.29900 * R + 0.58700 * G + 0.11400 * B
 *      U  = -0.16874 * R - 0.33126 * G + 0.50000 * B + 128
 *      V  =  0.50000 * R - 0.41869 * G - 0.08131 * B + 128
 *
 * Feel free to experiment with different coefficients by altering the
 * next 9 defines.
 */

#if 1

#define Y_R (SCALEFACTOR *  0.29900)
#define Y_G (SCALEFACTOR *  0.58700)
#define Y_B (SCALEFACTOR *  0.11400)

#define U_R (SCALEFACTOR * -0.16874)
#define U_G (SCALEFACTOR * -0.33126)
#define U_B (SCALEFACTOR *  0.50000)

#define V_R (SCALEFACTOR *  0.50000)
#define V_G (SCALEFACTOR * -0.41869)
#define V_B (SCALEFACTOR * -0.08131)

#else

/*
 * Here is another promising set of coefficients. If you use these, you
 * must also add 16 to the Y calculation in the COMPUTE_Y macro found
 * in xineutils.h.
 */

#define Y_R (SCALEFACTOR *  0.257)
#define Y_G (SCALEFACTOR *  0.504)
#define Y_B (SCALEFACTOR *  0.098)

#define U_R (SCALEFACTOR * -0.148)
#define U_G (SCALEFACTOR * -0.291)
#define U_B (SCALEFACTOR *  0.439)

#define V_R (SCALEFACTOR *  0.439)
#define V_G (SCALEFACTOR * -0.368)
#define V_B (SCALEFACTOR * -0.071)

#endif

/*
 * Precalculate all of the YUV tables since it requires fewer than
 * 10 kilobytes to store them.
 */
int y_r_table[256];
int y_g_table[256];
int y_b_table[256];

int u_r_table[256];
int u_g_table[256];
int u_b_table[256];

int v_r_table[256];
int v_g_table[256];
int v_b_table[256];

void (*yuv444_to_yuy2) (yuv_planes_t *yuv_planes, unsigned char *yuy2_map);

/*
 * init_yuv_planes
 *
 * This function initializes a yuv_planes_t structure based on the width
 * and height passed to it. The width must be divisible by 4 or the
 * final conversion function will not work.
 */
void init_yuv_planes(yuv_planes_t *yuv_planes, int width, int height) {

  int plane_size;

  yuv_planes->row_width = width;
  yuv_planes->row_stride = width + 2;
  yuv_planes->row_count = height;
  plane_size = yuv_planes->row_stride * yuv_planes->row_count;

  yuv_planes->y = xine_xmalloc(plane_size);
  yuv_planes->u = xine_xmalloc(plane_size);
  yuv_planes->v = xine_xmalloc(plane_size);
}

/*
 * free_yuv_planes
 *
 * This frees the memory used by the YUV planes.
 */
void free_yuv_planes(yuv_planes_t *yuv_planes) {
  free(yuv_planes->y);
  free(yuv_planes->u);
  free(yuv_planes->v);
}

/* 
 * yuv444_to_yuy2_c
 *
 * This is the simple, portable C version of the yuv444_to_yuy2() function.
 * It is not especially accurate in its method. But it is fast.
 *
 * yuv_planes contains the 3 non-subsampled planes that represent Y, U,
 * and V samples for every pixel in the image. For each pair of pixels,
 * use both Y samples but use the first pixel's U value and the second
 * pixel's V value.
 *
 *    Y plane: Y0 Y1 Y2 Y3 ...
 *    U plane: U0 U1 U2 U3 ...
 *    V plane: V0 V1 V2 V3 ...
 *
 *   YUY2 map: Y0 U0 Y1 V1  Y2 U2 Y3 V3
 */
void yuv444_to_yuy2_c(yuv_planes_t *yuv_planes, unsigned char *yuy2_map) {

  int row_ptr, pixel_ptr;
  int yuy2_index;

  /* copy the Y samples */
  yuy2_index = 0;
  for (row_ptr = 0; row_ptr < yuv_planes->row_stride * yuv_planes->row_count;
    row_ptr += yuv_planes->row_stride) {
    for (pixel_ptr = 0; pixel_ptr <  yuv_planes->row_stride - 2;
      pixel_ptr++, yuy2_index += 2)
      yuy2_map[yuy2_index] = yuv_planes->y[row_ptr + pixel_ptr];
  }

  /* copy the C samples */
  yuy2_index = 1;
  for (row_ptr = 0; row_ptr < yuv_planes->row_stride * yuv_planes->row_count;
    row_ptr += yuv_planes->row_stride) {

    for (pixel_ptr = 0; pixel_ptr <  yuv_planes->row_stride - 2;) {
      yuy2_map[yuy2_index] = yuv_planes->u[row_ptr + pixel_ptr];
      pixel_ptr++;
      yuy2_index += 2;
      yuy2_map[yuy2_index] = yuv_planes->v[row_ptr + pixel_ptr];
      pixel_ptr++;
      yuy2_index += 2;
    }
  }
}

/* 
 * yuv444_to_yuy2_mmx
 *
 * This is the proper, filtering version of the yuv444_to_yuy2() function
 * optimized with basic Intel MMX instructions.
 * 
 * yuv_planes contains the 3 non-subsampled planes that represent Y, U,
 * and V samples for every pixel in the image. The goal is to convert the
 * 3 planes to a single packed YUY2 byte stream. Dealing with the Y
 * samples is easy because every Y sample is used in the final image.
 * This can still be sped up using MMX instructions. Initialize mm0 to 0.
 * Then load blocks of 8 Y samples into mm1:
 *
 *    in memory: Y0 Y1 Y2 Y3 Y4 Y5 Y6 Y7
 *    in mm1:    Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0
 *
 * Use the punpck*bw instructions to interleave the Y samples with zeros.
 * For example, executing punpcklbw_r2r(mm0, mm1) will result in:
 *
 *          mm1: 00 Y3 00 Y2 00 Y1 00 Y0
 *
 * which will be written back to memory (in the YUY2 map) as:
 *
 *    in memory: Y0 00 Y1 00 Y2 00 Y3 00
 *
 * Do the same with the top 4 samples and soon all of the Y samples are
 * split apart and ready to have the U and V values interleaved.
 *
 * The C planes (U and V) must be filtered. The filter looks like this:
 *
 *   (1 * C1 + 3 * C2 + 3 * C3 + 1 * C4) / 8
 *
 * This filter slides across each row of each color plane. In the end, all
 * of the samples are filtered and the converter only uses every other
 * one. Since half of the filtered samples will not be used, their
 * calculations can safely be skipped.
 *
 * This implementation of the converter uses the MMX pmaddwd instruction
 * which performs 4 16x16 multiplications and 2 additions in parallel.
 *
 * First, initialize mm0 to 0 and mm7 to the filter coefficients:
 *    mm0 = 0
 *    mm7 = 0001 0003 0003 0001
 *
 * For each C plane, init the YUY2 map pointer to either 1 (for the U
 * plane) or 3 (for the V plane). For each set of 8 C samples, compute
 * 3 final C samples: 1 for [C0..C3], 1 for [C2..C5], and 1 for [C4..C7].
 * Load 8 samples:
 *    mm1 = C7 C6 .. C1 C0 (opposite order than in memory)
 *
 * Interleave zeros with the first 4 C samples:
 *    mm2 = 00 C3 00 C2 00 C1 00 C0
 *
 * Use pmaddwd to multiply and add:
 *    mm2 = [C0 * 1 + C1 * 3] [C2 * 3 + C3 * 1]
 *
 * Copy mm2 to mm3, shift the high 32 bits in mm3 down, do the final
 * accumulation, and then divide by 8 (shift right by 3):
 *    mm3 = mm2
 *    mm3 >>= 32
 *    mm2 += mm3
 *    mm2 >>= 3
 *
 * At this point, the lower 8 bits of mm2 contain a filtered C sample.
 * Move it out the YUY2 map and advance the map pointer by 4. Toss out
 * 2 of the samples in mm1 (C0 and C1) and loop twice more, once for
 * [C2..C5] and once for [C4..C7]. After computing 3 filtered samples,
 * increment the plane pointer by 6 and repeat the whole process.
 *
 * There is a special case when the row width is not evenly divisible by
 * 6. In the special case, the plane pointer must be backed up by a few
 * samples so that the filter can be computed 1 or 2 more times in order to
 * pad out the line.    
 *
 */
void yuv444_to_yuy2_mmx(yuv_planes_t *yuv_planes, unsigned char *yuy2_map) {

  int i, j, k;
  unsigned char *source_plane;
  unsigned char *dest_plane;
  unsigned char vector[8];
  unsigned char filter[] = {
    0x01, 0x00,
    0x03, 0x00,
    0x03, 0x00,
    0x01, 0x00
  };

  /* special case work variables */
  int width_mod;
  int secondary_samples;
  int rewind_bytes;
  int toss_out_shift;

  width_mod = yuv_planes->row_width % 6;
  secondary_samples = width_mod / 2;
  rewind_bytes = 6 - width_mod;
  toss_out_shift = rewind_bytes * 8;

  /* set up some MMX registers: mm0 = 0, mm7 = color filter */
  pxor_r2r(mm0, mm0);
  movq_m2r(*filter, mm7);

  /* copy the Y samples */
  source_plane = yuv_planes->y;
  dest_plane = yuy2_map;
  for (i = 0; i < yuv_planes->row_count; i++) {
    /* iterate through blocks of 8 samples, disregarding extra 2 samples */
    for (j = 0; j < yuv_planes->row_width / 8; j++) {

      movq_m2r(*source_plane, mm1);  /* load 8 Y samples */
      source_plane += 8;

      movq_r2r(mm1, mm2);  /* mm2 = mm1 */

      punpcklbw_r2r(mm0, mm1); /* interleave lower 4 samples with zeros */
      movq_r2m(mm1, *dest_plane);
      dest_plane += 8;

      punpckhbw_r2r(mm0, mm2); /* interleave upper 4 samples with zeros */
      movq_r2m(mm2, *dest_plane);
      dest_plane += 8;
    }

    /* account for extra 2 samples */
    source_plane += 2;
  }

  /* figure out the U samples */
  source_plane = yuv_planes->u;
  dest_plane = yuy2_map + 1;
  for (i = 0; i < yuv_planes->row_count; i++) {

    /* iterate through blocks of 6 samples */
    for (j = 0; j < yuv_planes->row_width / 6; j++) {

      movq_m2r(*source_plane, mm1); /* load 8 U samples */
      source_plane += 6;

      for (k = 0; k < 3; k++)
      {
        movq_r2r(mm1, mm2);      /* make a copy */

        punpcklbw_r2r(mm0, mm2); /* interleave lower 4 samples with zeros */
        pmaddwd_r2r(mm7, mm2);   /* apply the filter */
        movq_r2r(mm2, mm3);      /* copy result to mm3 */
        psrlq_i2r(32, mm3);      /* move the upper sum down */
        paddd_r2r(mm3, mm2);     /* mm2 += mm3 */
        psrlq_i2r(3, mm2);       /* final phase of the filter */

        movq_r2m(mm2, *vector);
        dest_plane[0] = vector[0];
        dest_plane += 4;

        psrlq_i2r(16, mm1);      /* toss out 2 U samples and loop again */
      }

    }

    /* special case time: secondary samples */
    if (width_mod) {
      source_plane -= rewind_bytes;
      movq_m2r(*source_plane, mm1); /* load 8 U samples */
      source_plane += 8;

      /* toss out 2 U samples before starting */
      psrlq_i2r(toss_out_shift, mm1);

      for (k = 0; k < secondary_samples; k++)
      {
        movq_r2r(mm1, mm2);      /* make a copy */

        punpcklbw_r2r(mm0, mm2); /* interleave lower 4 samples with zeros */
        pmaddwd_r2r(mm7, mm2);   /* apply the filter */
        movq_r2r(mm2, mm3);      /* copy result to mm3 */
        psrlq_i2r(32, mm3);      /* move the upper sum down */
        paddd_r2r(mm3, mm2);     /* mm2 += mm3 */
        psrlq_i2r(3, mm2);       /* final phase of the filter */

        movq_r2m(mm2, *vector);
        dest_plane[0] = vector[0];
        dest_plane += 4;

        psrlq_i2r(16, mm1);      /* toss out 2 U samples and loop again */
      }
    } else
      source_plane += 2;
  }

  /* figure out the V samples */
  source_plane = yuv_planes->v;
  dest_plane = yuy2_map + 3;
  for (i = 0; i < yuv_planes->row_count; i++) {

    /* iterate through blocks of 6 samples */
    for (j = 0; j < yuv_planes->row_width / 6; j++) {

      movq_m2r(*source_plane, mm1); /* load 8 U samples */
      source_plane += 6;

      for (k = 0; k < 3; k++)
      {
        movq_r2r(mm1, mm2);      /* make a copy */

        punpcklbw_r2r(mm0, mm2); /* interleave lower 4 samples with zeros */
        pmaddwd_r2r(mm7, mm2);   /* apply the filter */
        movq_r2r(mm2, mm3);      /* copy result to mm3 */
        psrlq_i2r(32, mm3);      /* move the upper sum down */
        paddd_r2r(mm3, mm2);     /* mm2 += mm3 */
        psrlq_i2r(3, mm2);       /* final phase of the filter */

        movq_r2m(mm2, *vector);
        dest_plane[0] = vector[0];
        dest_plane += 4;

        psrlq_i2r(16, mm1);      /* toss out 2 V samples and loop again */
      }
    }

    /* special case time: secondary samples */
    if (width_mod) {
      source_plane -= rewind_bytes;
      movq_m2r(*source_plane, mm1); /* load 8 V samples */
      source_plane += 8;

      /* toss out 2 V samples before starting */
      psrlq_i2r(toss_out_shift, mm1);

      for (k = 0; k < secondary_samples; k++)
      {
        movq_r2r(mm1, mm2);      /* make a copy */

        punpcklbw_r2r(mm0, mm2); /* interleave lower 4 samples with zeros */
        pmaddwd_r2r(mm7, mm2);   /* apply the filter */
        movq_r2r(mm2, mm3);      /* copy result to mm3 */
        psrlq_i2r(32, mm3);      /* move the upper sum down */
        paddd_r2r(mm3, mm2);     /* mm2 += mm3 */
        psrlq_i2r(3, mm2);       /* final phase of the filter */

        movq_r2m(mm2, *vector);
        dest_plane[0] = vector[0];
        dest_plane += 4;

        psrlq_i2r(16, mm1);      /* toss out 2 V samples and loop again */
      }
    } else
      source_plane += 2;
  }

  /* be a good MMX citizen and empty MMX state */
  emms();
}

/*
 * init_yuv_conversion
 *
 * This function precalculates all of the tables used for converted RGB
 * values to YUV values. This function also decides which conversion
 * functions to use.
 */
void init_yuv_conversion(void) {

  int i;

  for (i = 0; i < 256; i++) {

    y_r_table[i] = Y_R * i;
    y_g_table[i] = Y_G * i;
    y_b_table[i] = Y_B * i;

    u_r_table[i] = U_R * i;
    u_g_table[i] = U_G * i;
    u_b_table[i] = U_B * i;

    v_r_table[i] = V_R * i;
    v_g_table[i] = V_G * i;
    v_b_table[i] = V_B * i;
  }

  if (xine_mm_accel() & MM_ACCEL_X86_MMX)
    yuv444_to_yuy2 = yuv444_to_yuy2_mmx;
  else
    yuv444_to_yuy2 = yuv444_to_yuy2_c;
}
