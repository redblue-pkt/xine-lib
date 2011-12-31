/*
 * Copyright (C) 2000-2003 the xine project
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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
 * The facilities in this file are designed to ease the pain of converting
 * RGB -> YUV.
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
 * The yuv_planes_t structure has 2 other fields: row_width and row_count
 * which are equivalent to the frame width and height, respectively.
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
 * Some utility macros that you may find useful in your decoder are
 * UNPACK_RGB15, UNPACK_RGB16, UNPACK_BGR15, and UNPACK_BGR16. All are
 * located in xineutils.h. All of them take a packed pixel, either in
 * RGB or BGR format depending on the macro, and unpack them into the
 * component red, green, and blue bytes. If a CPU has special instructions
 * to facilitate these operations (such as the PPC AltiVec pixel-unpacking
 * instructions), these macros will automatically map to those special
 * instructions.
 */

#include <xine/xine_internal.h>
#include "xine_mmx.h"

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

void (*yuv444_to_yuy2) (const yuv_planes_t *yuv_planes, unsigned char *yuy2_map, int pitch);
void (*yuv9_to_yv12)
  (const unsigned char *y_src, int y_src_pitch, unsigned char *y_dest, int y_dest_pitch,
   const unsigned char *u_src, int u_src_pitch, unsigned char *u_dest, int u_dest_pitch,
   const unsigned char *v_src, int v_src_pitch, unsigned char *v_dest, int v_dest_pitch,
   int width, int height);
void (*yuv411_to_yv12)
  (const unsigned char *y_src, int y_src_pitch, unsigned char *y_dest, int y_dest_pitch,
   const unsigned char *u_src, int u_src_pitch, unsigned char *u_dest, int u_dest_pitch,
   const unsigned char *v_src, int v_src_pitch, unsigned char *v_dest, int v_dest_pitch,
   int width, int height);
void (*yv12_to_yuy2)
  (const unsigned char *y_src, int y_src_pitch,
   const unsigned char *u_src, int u_src_pitch,
   const unsigned char *v_src, int v_src_pitch,
   unsigned char *yuy2_map, int yuy2_pitch,
   int width, int height, int progressive);
void (*yuy2_to_yv12)
  (const unsigned char *yuy2_map, int yuy2_pitch,
   unsigned char *y_dst, int y_dst_pitch,
   unsigned char *u_dst, int u_dst_pitch,
   unsigned char *v_dst, int v_dst_pitch,
   int width, int height);

/*
 * init_yuv_planes
 *
 * This function initializes a yuv_planes_t structure based on the width
 * and height passed to it. The width must be divisible by 2.
 */
void init_yuv_planes(yuv_planes_t *yuv_planes, int width, int height) {
  memset (yuv_planes, 0, sizeof (yuv_planes));

  yuv_planes->row_width = width;
  yuv_planes->row_count = height;

  yuv_planes->y = calloc(width, height);
  yuv_planes->u = calloc(width, height);
  yuv_planes->v = calloc(width, height);
}

/*
 * free_yuv_planes
 *
 * This frees the memory used by the YUV planes.
 */
void free_yuv_planes(yuv_planes_t *yuv_planes) {
  if (yuv_planes->y)
    free(yuv_planes->y);
  if (yuv_planes->u)
    free(yuv_planes->u);
  if (yuv_planes->v)
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
static void yuv444_to_yuy2_c(const yuv_planes_t *yuv_planes, unsigned char *yuy2_map,
  int pitch) {

  unsigned int row_ptr, pixel_ptr;
  int yuy2_index;

  /* copy the Y samples */
  yuy2_index = 0;
  for (row_ptr = 0; row_ptr < yuv_planes->row_width * yuv_planes->row_count;
    row_ptr += yuv_planes->row_width) {
    for (pixel_ptr = 0; pixel_ptr <  yuv_planes->row_width;
      pixel_ptr++, yuy2_index += 2)
      yuy2_map[yuy2_index] = yuv_planes->y[row_ptr + pixel_ptr];

    yuy2_index += (pitch - 2*yuv_planes->row_width);
  }

  /* copy the C samples */
  yuy2_index = 1;
  for (row_ptr = 0; row_ptr < yuv_planes->row_width * yuv_planes->row_count;
    row_ptr += yuv_planes->row_width) {

    for (pixel_ptr = 0; pixel_ptr <  yuv_planes->row_width;) {
      yuy2_map[yuy2_index] = yuv_planes->u[row_ptr + pixel_ptr];
      pixel_ptr++;
      yuy2_index += 2;
      yuy2_map[yuy2_index] = yuv_planes->v[row_ptr + pixel_ptr];
      pixel_ptr++;
      yuy2_index += 2;
    }

    yuy2_index += (pitch - 2*yuv_planes->row_width);
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
 * Move it out to the YUY2 map and advance the map pointer by 4. Toss out
 * 2 of the samples in mm1 (C0 and C1) and loop twice more, once for
 * [C2..C5] and once for [C4..C7]. After computing 3 filtered samples,
 * increment the plane pointer by 6 and repeat the whole process.
 *
 * There is a special case when the filter hits the end of the line since
 * it is always necessary to rely on phantom samples beyond the end of the
 * line in order to compute the final 1-3 C samples of a line. This function
 * rewinds the C sample stream by a few bytes and reuses a few samples in
 * order to compute the final samples. This is not strictly correct; a
 * better approach would be to mirror the final samples before computing
 * the filter. But this reuse method is fast and apparently accurate
 * enough.
 *
 */
static void yuv444_to_yuy2_mmx(const yuv_planes_t *yuv_planes, unsigned char *yuy2_map,
  int pitch) {
#if defined(ARCH_X86) || defined(ARCH_X86_64)
  unsigned int i;
  int h, j, k;
  int width_div_8 = yuv_planes->row_width / 8;
  int width_mod_8 = yuv_planes->row_width % 8;
  unsigned char *source_plane;
  unsigned char *dest_plane;
  unsigned char filter[] = {
    0x01, 0x00,
    0x03, 0x00,
    0x03, 0x00,
    0x01, 0x00
  };
  unsigned char shifter[] = {0, 0, 0, 0, 0, 0, 0, 0};
  unsigned char vector[8];
  int block_loops = yuv_planes->row_width / 6;
  int filter_loops;
  int residual_filter_loops;
  int row_inc = (pitch - 2 * yuv_planes->row_width);

  residual_filter_loops = (yuv_planes->row_width % 6) / 2;
  shifter[0] = residual_filter_loops * 8;
  /* if the width is divisible by 6, apply 3 residual filters and perform
   * one less primary loop */
  if (!residual_filter_loops) {
    residual_filter_loops = 3;
    block_loops--;
  }

  /* set up some MMX registers:
   * mm0 = 0, mm7 = color filter */
  pxor_r2r(mm0, mm0);
  movq_m2r(*filter, mm7);

  /* copy the Y samples */
  source_plane = yuv_planes->y;
  dest_plane = yuy2_map;
  for (i = 0; i < yuv_planes->row_count; i++) {
    /* iterate through blocks of 8 Y samples */
    for (j = 0; j < width_div_8; j++) {

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

    /* iterate through residual samples in row if row is not divisible by 8 */
    for (j = 0; j < width_mod_8; j++) {

      *dest_plane = *source_plane;
      dest_plane += 2;
      source_plane++;
    }

    dest_plane += row_inc;
  }

  /* figure out the C samples */
  for (h = 0; h < 2; h++) {

    /* select the color plane for this iteration */
    if (h == 0) {
      source_plane = yuv_planes->u;
      dest_plane = yuy2_map + 1;
    } else {
      source_plane = yuv_planes->v;
      dest_plane = yuy2_map + 3;
    }

    for (i = 0; i < yuv_planes->row_count; i++) {

      filter_loops = 3;

      /* iterate through blocks of 6 samples */
      for (j = 0; j <= block_loops; j++) {

        if (j == block_loops) {

          /* special case for end-of-line residual */
          filter_loops = residual_filter_loops;
          source_plane -= (8 - residual_filter_loops * 2);
          movq_m2r(*source_plane, mm1); /* load 8 C samples */
          source_plane += 8;
          psrlq_m2r(*shifter, mm1);  /* toss out samples before starting */

        } else {

          /* normal case */
          movq_m2r(*source_plane, mm1); /* load 8 C samples */
          source_plane += 6;
        }

        for (k = 0; k < filter_loops; k++) {
          movq_r2r(mm1, mm2);      /* make a copy */

          punpcklbw_r2r(mm0, mm2); /* interleave lower 4 samples with zeros */
          pmaddwd_r2r(mm7, mm2);   /* apply the filter */
          movq_r2r(mm2, mm3);      /* copy result to mm3 */
          psrlq_i2r(32, mm3);      /* move the upper sum down */
          paddd_r2r(mm3, mm2);     /* mm2 += mm3 */
          psrlq_i2r(3, mm2);       /* divide by 8 */

#if 0
          /* load the destination address into ebx */
          __asm__ __volatile__ ("mov %0, %%ebx"
                              : /* nothing */
                              : "X" (dest_plane)
                              : "ebx" /* clobber list */);

          /* move the lower 32 bits of mm2 into eax */
          __asm__ __volatile__ ("movd %%mm2, %%eax"
                                : /* nothing */
                                : /* nothing */
                                : "eax" /* clobber list */ );

          /* move al (the final filtered sample) to its spot it memory */
          __asm__ __volatile__ ("mov %%al, (%%ebx)"
                                : /* nothing */
                                : /* nothing */ );

#else
          movq_r2m(mm2, *vector);
          dest_plane[0] = vector[0];
#endif

          dest_plane += 4;

          psrlq_i2r(16, mm1);      /* toss out 2 C samples and loop again */
        }
      }
    }
  }

  /* be a good MMX citizen and empty MMX state */
  emms();
#endif
}

static void hscale_chroma_line (unsigned char *dst, const unsigned char *src,
  int width) {

  unsigned int n1, n2;
  int       x;

  n1       = *src;
  *(dst++) = n1;

  for (x=0; x < (width - 1); x++) {
    n2       = *(++src);
    *(dst++) = (3*n1 + n2 + 2) >> 2;
    *(dst++) = (n1 + 3*n2 + 2) >> 2;
    n1       = n2;
  }

  *dst = n1;
}

static void vscale_chroma_line (unsigned char *dst, int pitch,
  const unsigned char *src1, const unsigned char *src2, int width) {

  unsigned int t1, t2;
  unsigned int n1, n2, n3, n4;
  unsigned int *dst1, *dst2;
  int       x;

  dst1 = (unsigned int *) dst;
  dst2 = (unsigned int *) (dst + pitch);

  /* process blocks of 4 pixels */
  for (x=0; x < (width / 4); x++) {
    n1  = *((unsigned int *) src1); src1 = (const unsigned char *)(((const unsigned int *) src1) + 1);
    n2  = *((unsigned int *) src2); src2 = (const unsigned char *)(((const unsigned int *) src2) + 1);
    n3  = (n1 & 0xFF00FF00) >> 8;
    n4  = (n2 & 0xFF00FF00) >> 8;
    n1 &= 0x00FF00FF;
    n2 &= 0x00FF00FF;

    t1 = (2*n1 + 2*n2 + 0x20002);
    t2 = (n1 - n2);
    n1 = (t1 + t2);
    n2 = (t1 - t2);
    t1 = (2*n3 + 2*n4 + 0x20002);
    t2 = (n3 - n4);
    n3 = (t1 + t2);
    n4 = (t1 - t2);

    *(dst1++) = ((n1 >> 2) & 0x00FF00FF) | ((n3 << 6) & 0xFF00FF00);
    *(dst2++) = ((n2 >> 2) & 0x00FF00FF) | ((n4 << 6) & 0xFF00FF00);
  }

  /* process remaining pixels */
  for (x=(width & ~0x3); x < width; x++) {
    n1 = src1[x];
    n2 = src2[x];

    dst[x]       = (3*n1 + n2 + 2) >> 2;
    dst[x+pitch] = (n1 + 3*n2 + 2) >> 2;
  }
}

static void upsample_c_plane_c(const unsigned char *src, int src_width,
  int src_height, unsigned char *dest,
  unsigned int src_pitch, unsigned int dest_pitch) {

  unsigned char *cr1;
  unsigned char *cr2;
  unsigned char *tmp;
  int y;

  cr1 = &dest[dest_pitch * (src_height * 2 - 2)];
  cr2 = &dest[dest_pitch * (src_height * 2 - 3)];

  /* horizontally upscale first line */
  hscale_chroma_line (cr1, src, src_width);
  src += src_pitch;

  /* store first line */
  memcpy (dest, cr1, src_width * 2);
  dest += dest_pitch;

  for (y = 0; y < (src_height - 1); y++) {

    hscale_chroma_line (cr2, src, src_width);
    src += src_pitch;

    /* interpolate and store two lines */
    vscale_chroma_line (dest, dest_pitch, cr1, cr2, src_width * 2);
    dest += 2 * dest_pitch;

    /* swap buffers */
    tmp = cr2;
    cr2 = cr1;
    cr1 = tmp;
  }

  /* horizontally upscale and store last line */
  src -= src_pitch;
  hscale_chroma_line (dest, src, src_width);
}

/*
 * yuv9_to_yv12_c
 *
 */
static void yuv9_to_yv12_c
  (const unsigned char *y_src, int y_src_pitch, unsigned char *y_dest, int y_dest_pitch,
   const unsigned char *u_src, int u_src_pitch, unsigned char *u_dest, int u_dest_pitch,
   const unsigned char *v_src, int v_src_pitch, unsigned char *v_dest, int v_dest_pitch,
   int width, int height) {

  int y;

  /* Y plane */
  for (y=0; y < height; y++) {
    xine_fast_memcpy (y_dest, y_src, width);
    y_src += y_src_pitch;
    y_dest += y_dest_pitch;
  }

  /* U plane */
  upsample_c_plane_c(u_src, width / 4, height / 4, u_dest,
    u_src_pitch, u_dest_pitch);

  /* V plane */
  upsample_c_plane_c(v_src, width / 4, height / 4, v_dest,
    v_src_pitch, v_dest_pitch);

}

/*
 * yuv411_to_yv12_c
 *
 */
static void yuv411_to_yv12_c
  (const unsigned char *y_src, int y_src_pitch, unsigned char *y_dest, int y_dest_pitch,
   const unsigned char *u_src, int u_src_pitch, unsigned char *u_dest, int u_dest_pitch,
   const unsigned char *v_src, int v_src_pitch, unsigned char *v_dest, int v_dest_pitch,
   int width, int height) {

  int y;
  int c_src_row, c_src_pixel;
  int c_dest_row, c_dest_pixel;
  unsigned char c_sample;

  /* Y plane */
  for (y=0; y < height; y++) {
    xine_fast_memcpy (y_dest, y_src, width);
    y_src += y_src_pitch;
    y_dest += y_dest_pitch;
  }

  /* naive approach: downsample vertically, upsample horizontally */

  /* U plane */
  for (c_src_row = 0, c_dest_row = 0;
       c_src_row < u_src_pitch * height;
       c_src_row += u_src_pitch * 2, c_dest_row += u_dest_pitch) {

    for (c_src_pixel = c_src_row, c_dest_pixel = c_dest_row;
         c_dest_pixel < c_dest_row + u_dest_pitch;
         c_src_pixel++) {

      /* downsample by averaging the samples from 2 rows */
      c_sample =
        (u_src[c_src_pixel] + u_src[c_src_pixel + u_src_pitch] + 1) / 2;
      /* upsample by outputting the sample twice on the YV12 row */
      u_dest[c_dest_pixel++] = c_sample;
      u_dest[c_dest_pixel++] = c_sample;

    }
  }

  /* V plane */
  for (c_src_row = 0, c_dest_row = 0;
       c_src_row < v_src_pitch * height;
       c_src_row += v_src_pitch * 2, c_dest_row += v_dest_pitch) {

    for (c_src_pixel = c_src_row, c_dest_pixel = c_dest_row;
         c_dest_pixel < c_dest_row + v_dest_pitch;
         c_src_pixel++) {

      /* downsample by averaging the samples from 2 rows */
      c_sample =
        (v_src[c_src_pixel] + v_src[c_src_pixel + v_src_pitch] + 1 ) / 2;
      /* upsample by outputting the sample twice on the YV12 row */
      v_dest[c_dest_pixel++] = c_sample;
      v_dest[c_dest_pixel++] = c_sample;

    }
  }

}

#define C_YUV420_YUYV_PROGRESSIVE( )                                          \
    utmp = 3 * *p_u++;                                                        \
    vtmp = 3 * *p_v++;                                                        \
    *p_line1++ = *p_y1++;                *p_line2++ = *p_y2++;                \
    *p_line1++ = (*p_ut++ + utmp) >> 2;  *p_line2++ = (utmp + *p_ub++) >> 2;  \
    *p_line1++ = *p_y1++;                *p_line2++ = *p_y2++;                \
    *p_line1++ = (*p_vt++ + vtmp) >> 2;  *p_line2++ = (vtmp + *p_vb++) >> 2;  \

#define C_YUV420_YUYV_INTERLACED_ODD( )                                                   \
    *p_line1++ = *p_y1++;                   *p_line2++ = *p_y2++;                         \
    *p_line1++ = (*p_ut++ + *p_u * 7) >> 3; *p_line2++ = (*p_u++ * 5 + *p_ub++ * 3) >> 3; \
    *p_line1++ = *p_y1++;                   *p_line2++ = *p_y2++;                         \
    *p_line1++ = (*p_vt++ + *p_v * 7) >> 3; *p_line2++ = (*p_v++ * 5 + *p_vb++ * 3) >> 3; \

#define C_YUV420_YUYV_INTERLACED_EVEN( )                                                  \
    *p_line1++ = *p_y1++;                       *p_line2++ = *p_y2++;                     \
    *p_line1++ = (*p_ut++ * 3 + *p_u * 5) >> 3; *p_line2++ = (*p_u++ * 7 + *p_ub++) >> 3; \
    *p_line1++ = *p_y1++;                       *p_line2++ = *p_y2++;                     \
    *p_line1++ = (*p_vt++ * 3 + *p_v * 5) >> 3; *p_line2++ = (*p_v++ * 7 + *p_vb++) >> 3; \

/*****************************************************************************
 * I420_YUY2: planar YUV 4:2:0 to packed YUYV 4:2:2
 * original conversion routine from Videolan project
 * changed to support interlaced frames and do correct chroma upsampling with
 * correct weighting factors and no chroma shift.
 *****************************************************************************/
static void yv12_to_yuy2_c
  (const unsigned char *y_src, int y_src_pitch,
   const unsigned char *u_src, int u_src_pitch,
   const unsigned char *v_src, int v_src_pitch,
   unsigned char *yuy2_map, int yuy2_pitch,
   int width, int height, int progressive) {

    uint8_t *p_line1, *p_line2 = yuy2_map;
    const uint8_t *p_y1, *p_y2 = y_src;
    const uint8_t *p_u = u_src;
    const uint8_t *p_v = v_src;
    const uint8_t *p_ub, *p_vb;
    const uint8_t *p_ut = u_src;
    const uint8_t *p_vt = v_src;

    int i_x, i_y;
    int utmp, vtmp;

    const int i_source_margin = y_src_pitch - width;
    const int i_source_u_margin = u_src_pitch - width/2;
    const int i_source_v_margin = v_src_pitch - width/2;
    const int i_dest_margin = yuy2_pitch - width*2;


    if( progressive ) {

      for( i_y = height / 2 ; i_y-- ; )
      {
          p_line1 = p_line2;
          p_line2 += yuy2_pitch;

          p_y1 = p_y2;
          p_y2 += y_src_pitch;

          if( i_y > 1 ) {
            p_ub = p_u + u_src_pitch;
            p_vb = p_v + v_src_pitch;
          } else {
            p_ub = p_u;
            p_vb = p_v;
          }

          for( i_x = width / 2 ; i_x-- ; )
          {
              C_YUV420_YUYV_PROGRESSIVE( );
          }

          p_y2 += i_source_margin;
          p_u += i_source_u_margin;
          p_v += i_source_v_margin;
          p_ut = p_u - u_src_pitch;
          p_vt = p_v - v_src_pitch;
          p_line2 += i_dest_margin;
      }

    } else {

      for( i_y = height / 4 ; i_y-- ; )
      {
          p_line1 = p_line2;
          p_line2 += 2 * yuy2_pitch;

          p_y1 = p_y2;
          p_y2 += 2 * y_src_pitch;

          if( i_y > 1 ) {
            p_ub = p_u + 2 * u_src_pitch;
            p_vb = p_v + 2 * v_src_pitch;
          } else {
            p_ub = p_u;
            p_vb = p_v;
          }

          for( i_x = width / 2 ; i_x-- ; )
          {
              C_YUV420_YUYV_INTERLACED_ODD( );
          }

          p_y2 += i_source_margin + y_src_pitch;
          p_u += i_source_u_margin + u_src_pitch;
          p_v += i_source_v_margin + v_src_pitch;
          p_ut = p_u - 2 * u_src_pitch;
          p_vt = p_v - 2 * v_src_pitch;
          p_line2 += i_dest_margin + yuy2_pitch;
      }

      p_line2 = yuy2_map + yuy2_pitch;
      p_y2 = y_src + y_src_pitch;
      p_u = u_src + u_src_pitch;
      p_v = v_src + v_src_pitch;
      p_ut = p_u;
      p_vt = p_v;

      for( i_y = height / 4 ; i_y-- ; )
      {
          p_line1 = p_line2;
          p_line2 += 2 * yuy2_pitch;

          p_y1 = p_y2;
          p_y2 += 2 * y_src_pitch;

          if( i_y > 1 ) {
            p_ub = p_u + 2 * u_src_pitch;
            p_vb = p_v + 2 * v_src_pitch;
          } else {
            p_ub = p_u;
            p_vb = p_v;
          }

          for( i_x = width / 2 ; i_x-- ; )
          {
              C_YUV420_YUYV_INTERLACED_EVEN( );
          }

          p_y2 += i_source_margin + y_src_pitch;
          p_u += i_source_u_margin + u_src_pitch;
          p_v += i_source_v_margin + v_src_pitch;
          p_ut = p_u - 2 * u_src_pitch;
          p_vt = p_v - 2 * v_src_pitch;
          p_line2 += i_dest_margin + yuy2_pitch;
      }

    }
}


#if defined(ARCH_X86) || defined(ARCH_X86_64)

#define MMXEXT_YUV420_YUYV_PROGRESSIVE( )                                          \
do {                                                                               \
   __asm__ __volatile__(".align 8 \n\t"                                            \
    "movq       (%0), %%mm0 \n\t"  /* Load 8 Y          y7 y6 y5 y4 y3 y2 y1 y0 */ \
    "movd       (%1), %%mm1 \n\t"  /* Load 4 Cb         00 00 00 00 u3 u2 u1 u0 */ \
    "movd       (%2), %%mm2 \n\t"  /* Load 4 Cr         00 00 00 00 v3 v2 v1 v0 */ \
    "pxor      %%mm7, %%mm7 \n\t"  /*                   00 00 00 00 00 00 00 00 */ \
    "punpcklbw %%mm7, %%mm1 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "punpcklbw %%mm7, %%mm2 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "movq      %%mm1, %%mm3 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "movq      %%mm2, %%mm4 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "psllw        $1, %%mm3 \n\t"  /* Cb * 2                                    */ \
    "psllw        $1, %%mm4 \n\t"  /* Cr * 2                                    */ \
    "paddw     %%mm3, %%mm1 \n\t"  /* Cb * 3                                    */ \
    "paddw     %%mm4, %%mm2 \n\t"  /* Cr * 3                                    */ \
    :                                                                              \
    : "r" (p_y1), "r" (p_u), "r" (p_v) );                                          \
   __asm__ __volatile__(                                                           \
    "movd       (%0), %%mm3 \n\t"  /* Load 4 Cbt        00 00 00 00 u3 u2 u1 u0 */ \
    "movd       (%1), %%mm4 \n\t"  /* Load 4 Crt        00 00 00 00 v3 v2 v1 v0 */ \
    "movd       (%2), %%mm5 \n\t"  /* Load 4 Cbb        00 00 00 00 u3 u2 u1 u0 */ \
    "movd       (%3), %%mm6 \n\t"  /* Load 4 Crb        00 00 00 00 v3 v2 v1 v0 */ \
    "punpcklbw %%mm7, %%mm3 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "punpcklbw %%mm7, %%mm4 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "punpcklbw %%mm7, %%mm5 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "punpcklbw %%mm7, %%mm6 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "paddw     %%mm1, %%mm3 \n\t"  /* Cb1 = Cbt + 3*Cb                          */ \
    "paddw     %%mm2, %%mm4 \n\t"  /* Cr1 = Crt + 3*Cr                          */ \
    "paddw     %%mm5, %%mm1 \n\t"  /* Cb2 = Cbb + 3*Cb                          */ \
    "paddw     %%mm6, %%mm2 \n\t"  /* Cr2 = Crb + 3*Cr                          */ \
    "psrlw        $2, %%mm3 \n\t"  /* Cb1 = (Cbt + 3*Cb) / 4                    */ \
    /* either the shifts by 2 and 8 or mask off bits and shift by 6             */ \
    "psrlw        $2, %%mm4 \n\t"  /* Cr1 = (Crt + 3*Cr) / 4                    */ \
    "psllw        $8, %%mm4 \n\t"                                                  \
    "por       %%mm4, %%mm3 \n\t"  /* Cr1 Cb1 interl    v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "psrlw        $2, %%mm1 \n\t"  /* Cb2 = (Cbb + 3*Cb) / 4                    */ \
    "psrlw        $2, %%mm2 \n\t"  /* Cr2 = (Cbb + 3*Cb) / 4                    */ \
    "psllw        $8, %%mm2 \n\t"                                                  \
    "por       %%mm1, %%mm2 \n\t"  /* Cr2 Cb2 interl    v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "movq      %%mm0, %%mm1 \n\t"  /*                   y7 y6 y5 y4 y3 y2 y1 y0 */ \
    "punpcklbw %%mm3, %%mm1 \n\t"  /*                   v1 y3 u1 y2 v0 y1 u0 y0 */ \
    :                                                                              \
    : "r" (p_ut), "r" (p_vt), "r" (p_ub), "r" (p_vb) );                            \
   __asm__ __volatile__(                                                           \
    "movntq    %%mm1, (%0)  \n\t"  /* Store low YUYV1                           */ \
    "punpckhbw %%mm3, %%mm0 \n\t"  /*                   v3 y7 u3 y6 v2 y5 u2 y4 */ \
    "movntq    %%mm0, 8(%0) \n\t"  /* Store high YUYV1                          */ \
    "movq       (%2), %%mm0 \n\t"  /* Load 8 Y          Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */ \
    "movq      %%mm0, %%mm1 \n\t"  /*                   Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */ \
    "punpcklbw %%mm2, %%mm1 \n\t"  /*                   v1 Y3 u1 Y2 v0 Y1 u0 Y0 */ \
    "movntq    %%mm1, (%1)  \n\t"  /* Store low YUYV2                           */ \
    "punpckhbw %%mm2, %%mm0 \n\t"  /*                   v3 Y7 u3 Y6 v2 Y5 u2 Y4 */ \
    "movntq    %%mm0, 8(%1) \n\t"  /* Store high YUYV2                          */ \
    :                                                                              \
    : "r" (p_line1),  "r" (p_line2),  "r" (p_y2) );                                \
  p_line1 += 16; p_line2 += 16; p_y1 += 8; p_y2 += 8; p_u += 4; p_v += 4;          \
  p_ub += 4; p_vb += 4; p_ut += 4; p_vt += 4;                                      \
} while(0)

#define MMXEXT_YUV420_YUYV_INTERLACED_ODD( )                                       \
do {                                                                               \
   __asm__ __volatile__(".align 8 \n\t"                                            \
    "movd       (%0), %%mm1 \n\t"  /* Load 4 Cb         00 00 00 00 u3 u2 u1 u0 */ \
    "movd       (%1), %%mm2 \n\t"  /* Load 4 Cr         00 00 00 00 v3 v2 v1 v0 */ \
    "pxor      %%mm7, %%mm7 \n\t"  /*                   00 00 00 00 00 00 00 00 */ \
    "punpcklbw %%mm7, %%mm1 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "punpcklbw %%mm7, %%mm2 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "movq      %%mm1, %%mm3 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "movq      %%mm2, %%mm4 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "psllw        $2, %%mm3 \n\t"  /* Cb * 4                                    */ \
    "psllw        $2, %%mm4 \n\t"  /* Cr * 4                                    */ \
    "paddw     %%mm3, %%mm1 \n\t"  /* Cb * 5                                    */ \
    "paddw     %%mm4, %%mm2 \n\t"  /* Cr * 5                                    */ \
    "psrlw        $1, %%mm3 \n\t"  /* Cb * 2                                    */ \
    "psrlw        $1, %%mm4 \n\t"  /* Cr * 2                                    */ \
    "paddw     %%mm1, %%mm3 \n\t"  /* Cb * 7                                    */ \
    "paddw     %%mm2, %%mm4 \n\t"  /* Cr * 7                                    */ \
    :                                                                              \
    : "r" (p_u), "r" (p_v) );                                                      \
   __asm__ __volatile__(                                                           \
    "movd       (%1), %%mm5 \n\t"  /* Load 4 Cbt        00 00 00 00 u3 u2 u1 u0 */ \
    "movd       (%2), %%mm6 \n\t"  /* Load 4 Crt        00 00 00 00 v3 v2 v1 v0 */ \
    "movq       (%0), %%mm0 \n\t"  /* Load 8 Y          y7 y6 y5 y4 y3 y2 y1 y0 */ \
    "punpcklbw %%mm7, %%mm5 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "punpcklbw %%mm7, %%mm6 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "paddw     %%mm3, %%mm5 \n\t"  /* Cb1 = Cbt + 7*Cb                          */ \
    "paddw     %%mm4, %%mm6 \n\t"  /* Cr1 = Crt + 7*Cr                          */ \
    "psrlw        $3, %%mm5 \n\t"  /* Cb1 = (Cbt + 7*Cb) / 8                    */ \
    /* either the shifts by 3 and 8 or mask off bits and shift by 5             */ \
    "psrlw        $3, %%mm6 \n\t"  /* Cr1 = (Crt + 7*Cr) / 8                    */ \
    "psllw        $8, %%mm6 \n\t"                                                  \
    "por       %%mm5, %%mm6 \n\t"  /* Cr1 Cb1 interl    v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "movq      %%mm0, %%mm3 \n\t"  /*                   y7 y6 y5 y4 y3 y2 y1 y0 */ \
    "punpcklbw %%mm6, %%mm3 \n\t"  /*                   v1 y3 u1 y2 v0 y1 u0 y0 */ \
    "movntq    %%mm3, (%3)  \n\t"  /* Store low YUYV1                           */ \
    "punpckhbw %%mm6, %%mm0 \n\t"  /*                   v3 y7 u3 y6 v2 y5 u2 y4 */ \
    "movntq    %%mm0, 8(%3) \n\t"  /* Store high YUYV1                          */ \
    :                                                                              \
    : "r" (p_y1), "r" (p_ut), "r" (p_vt), "r" (p_line1) );                         \
   __asm__ __volatile__(                                                           \
    "movd       (%1), %%mm3 \n\t"  /* Load 4 Cbb        00 00 00 00 u3 u2 u1 u0 */ \
    "movd       (%2), %%mm4 \n\t"  /* Load 4 Crb        00 00 00 00 v3 v2 v1 v0 */ \
    "movq       (%0), %%mm0 \n\t"  /* Load 8 Y          Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */ \
    "punpcklbw %%mm7, %%mm3 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "punpcklbw %%mm7, %%mm4 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "movq      %%mm3, %%mm5 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "movq      %%mm4, %%mm6 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "psllw        $1, %%mm5 \n\t"  /* Cbb * 2                                   */ \
    "psllw        $1, %%mm6 \n\t"  /* Crb * 2                                   */ \
    "paddw     %%mm5, %%mm3 \n\t"  /* Cbb * 3                                   */ \
    "paddw     %%mm6, %%mm4 \n\t"  /* Crb * 3                                   */ \
    "paddw     %%mm3, %%mm1 \n\t"  /* Cb2 = 3*Cbb + 5*Cb                        */ \
    "paddw     %%mm4, %%mm2 \n\t"  /* Cr2 = 3*Crb + 5*Cr                        */ \
    "psrlw        $3, %%mm1 \n\t"  /* Cb2 = (3*Cbb + 5*Cb) / 8                  */ \
    /* either the shifts by 3 and 8 or mask off bits and shift by 5             */ \
    "psrlw        $3, %%mm2 \n\t"  /* Cr2 = (3*Crb + 5*Cr) / 8                  */ \
    "psllw        $8, %%mm2 \n\t"                                                  \
    "por       %%mm1, %%mm2 \n\t"  /* Cr2 Cb2 interl    v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "movq      %%mm0, %%mm1 \n\t"  /*                   Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */ \
    "punpcklbw %%mm2, %%mm1 \n\t"  /*                   v1 Y3 u1 Y2 v0 Y1 u0 Y0 */ \
    "movntq    %%mm1, (%3)  \n\t"  /* Store low YUYV2                           */ \
    "punpckhbw %%mm2, %%mm0 \n\t"  /*                   v3 Y7 u3 Y6 v2 Y5 u2 Y4 */ \
    "movntq    %%mm0, 8(%3) \n\t"  /* Store high YUYV2                          */ \
    :                                                                              \
    : "r" (p_y2),  "r" (p_ub), "r" (p_vb),  "r" (p_line2) );                       \
  p_line1 += 16; p_line2 += 16; p_y1 += 8; p_y2 += 8; p_u += 4; p_v += 4;          \
  p_ub += 4; p_vb += 4; p_ut += 4; p_vt += 4;                                      \
} while(0)

#define MMXEXT_YUV420_YUYV_INTERLACED_EVEN( )                                      \
/* same as above, except the assembly input arguments are switched */              \
do {                                                                               \
   __asm__ __volatile__(".align 8 \n\t"                                            \
    "movd       (%0), %%mm1 \n\t"  /* Load 4 Cb         00 00 00 00 u3 u2 u1 u0 */ \
    "movd       (%1), %%mm2 \n\t"  /* Load 4 Cr         00 00 00 00 v3 v2 v1 v0 */ \
    "pxor      %%mm7, %%mm7 \n\t"  /*                   00 00 00 00 00 00 00 00 */ \
    "punpcklbw %%mm7, %%mm1 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "punpcklbw %%mm7, %%mm2 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "movq      %%mm1, %%mm3 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "movq      %%mm2, %%mm4 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "psllw        $2, %%mm3 \n\t"  /* Cb * 4                                    */ \
    "psllw        $2, %%mm4 \n\t"  /* Cr * 4                                    */ \
    "paddw     %%mm3, %%mm1 \n\t"  /* Cb * 5                                    */ \
    "paddw     %%mm4, %%mm2 \n\t"  /* Cr * 5                                    */ \
    "psrlw        $1,  %%mm3 \n\t" /* Cb * 2                                    */ \
    "psrlw        $1,  %%mm4 \n\t" /* Cr * 2                                    */ \
    "paddw     %%mm1, %%mm3 \n\t"  /* Cb * 7                                    */ \
    "paddw     %%mm2, %%mm4 \n\t"  /* Cr * 7                                    */ \
    :                                                                              \
    : "r" (p_u), "r" (p_v) );                                                      \
   __asm__ __volatile__(                                                           \
    "movd       (%1), %%mm5 \n\t"  /* Load 4 Cbt        00 00 00 00 u3 u2 u1 u0 */ \
    "movd       (%2), %%mm6 \n\t"  /* Load 4 Crt        00 00 00 00 v3 v2 v1 v0 */ \
    "movq       (%0), %%mm0 \n\t"  /* Load 8 Y          y7 y6 y5 y4 y3 y2 y1 y0 */ \
    "punpcklbw %%mm7, %%mm5 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "punpcklbw %%mm7, %%mm6 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "paddw     %%mm3, %%mm5 \n\t"  /* Cb1 = Cbt + 7*Cb                          */ \
    "paddw     %%mm4, %%mm6 \n\t"  /* Cr1 = Crt + 7*Cr                          */ \
    "psrlw        $3, %%mm5 \n\t"  /* Cb1 = (Cbt + 7*Cb) / 8                    */ \
    /* either the shifts by 3 and 8 or mask off bits and shift by 5             */ \
    "psrlw        $3, %%mm6 \n\t"  /* Cr1 = (Crt + 7*Cr) / 8                    */ \
    "psllw        $8, %%mm6 \n\t"                                                  \
    "por       %%mm5, %%mm6 \n\t"  /* Cr1 Cb1 interl    v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "movq      %%mm0, %%mm3 \n\t"  /*                   y7 y6 y5 y4 y3 y2 y1 y0 */ \
    "punpcklbw %%mm6, %%mm3 \n\t"  /*                   v1 y3 u1 y2 v0 y1 u0 y0 */ \
    "movntq    %%mm3, (%3)  \n\t"  /* Store low YUYV1                           */ \
    "punpckhbw %%mm6, %%mm0 \n\t"  /*                   v3 y7 u3 y6 v2 y5 u2 y4 */ \
    "movntq    %%mm0, 8(%3) \n\t"  /* Store high YUYV1                          */ \
    :                                                                              \
    : "r" (p_y2), "r" (p_ub), "r" (p_vb), "r" (p_line2) );                         \
   __asm__ __volatile__(                                                           \
    "movd       (%1), %%mm3 \n\t"  /* Load 4 Cbb        00 00 00 00 u3 u2 u1 u0 */ \
    "movd       (%2), %%mm4 \n\t"  /* Load 4 Crb        00 00 00 00 v3 v2 v1 v0 */ \
    "movq       (%0), %%mm0 \n\t"  /* Load 8 Y          Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */ \
    "punpcklbw %%mm7, %%mm3 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "punpcklbw %%mm7, %%mm4 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "movq      %%mm3, %%mm5 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "movq      %%mm4, %%mm6 \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "psllw        $1, %%mm5 \n\t"  /* Cbb * 2                                   */ \
    "psllw        $1, %%mm6 \n\t"  /* Crb * 2                                   */ \
    "paddw     %%mm5, %%mm3 \n\t"  /* Cbb * 3                                   */ \
    "paddw     %%mm6, %%mm4 \n\t"  /* Crb * 3                                   */ \
    "paddw     %%mm3, %%mm1 \n\t"  /* Cb2 = 3*Cbb + 5*Cb                        */ \
    "paddw     %%mm4, %%mm2 \n\t"  /* Cr2 = 3*Crb + 5*Cr                        */ \
    "psrlw        $3, %%mm1 \n\t"  /* Cb2 = (3*Cbb + 5*Cb) / 8                  */ \
    /* either the shifts by 3 and 8 or mask off bits and shift by 5             */ \
    "psrlw        $3, %%mm2 \n\t"  /* Cr2 = (3*Crb + 5*Cr) / 8                  */ \
    "psllw        $8, %%mm2 \n\t"                                                  \
    "por       %%mm1, %%mm2 \n\t"  /* Cr2 Cb2 interl    v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "movq      %%mm0, %%mm1 \n\t"  /*                   Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */ \
    "punpcklbw %%mm2, %%mm1 \n\t"  /*                   v1 Y3 u1 Y2 v0 Y1 u0 Y0 */ \
    "movntq    %%mm1, (%3)  \n\t"  /* Store low YUYV2                           */ \
    "punpckhbw %%mm2, %%mm0 \n\t"  /*                   v3 Y7 u3 Y6 v2 Y5 u2 Y4 */ \
    "movntq    %%mm0, 8(%3) \n\t"  /* Store high YUYV2                          */ \
    :                                                                              \
    : "r" (p_y1),  "r" (p_ut), "r" (p_vt),  "r" (p_line1) );                       \
  p_line1 += 16; p_line2 += 16; p_y1 += 8; p_y2 += 8; p_u += 4; p_v += 4;          \
  p_ub += 4; p_vb += 4; p_ut += 4; p_vt += 4;                                      \
} while(0)

#endif

static void yv12_to_yuy2_mmxext
  (const unsigned char *y_src, int y_src_pitch,
   const unsigned char *u_src, int u_src_pitch,
   const unsigned char *v_src, int v_src_pitch,
   unsigned char *yuy2_map, int yuy2_pitch,
   int width, int height, int progressive ) {
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    uint8_t *p_line1, *p_line2 = yuy2_map;
    const uint8_t *p_y1, *p_y2 = y_src;
    const uint8_t *p_u = u_src;
    const uint8_t *p_v = v_src;
    const uint8_t *p_ub, *p_vb;
    const uint8_t *p_ut = u_src;
    const uint8_t *p_vt = v_src;

    int i_x, i_y;
    int utmp, vtmp;

    const int i_source_margin = y_src_pitch - width;
    const int i_source_u_margin = u_src_pitch - width/2;
    const int i_source_v_margin = v_src_pitch - width/2;
    const int i_dest_margin = yuy2_pitch - width*2;

    if( progressive ) {

      for( i_y = height / 2; i_y-- ; )
      {
          p_line1 = p_line2;
          p_line2 += yuy2_pitch;

          p_y1 = p_y2;
          p_y2 += y_src_pitch;

          if( i_y > 1 ) {
            p_ub = p_u + u_src_pitch;
            p_vb = p_v + v_src_pitch;
          } else {
            p_ub = p_u;
            p_vb = p_v;
          }

          for( i_x = width / 8 ; i_x-- ; )
          {
              MMXEXT_YUV420_YUYV_PROGRESSIVE( );
          }
          for( i_x = (width % 8) / 2 ; i_x-- ; )
          {
              C_YUV420_YUYV_PROGRESSIVE( );
          }

          p_y2 += i_source_margin;
          p_u += i_source_u_margin;
          p_v += i_source_v_margin;
          p_ut = p_u - u_src_pitch;
          p_vt = p_v - v_src_pitch;
          p_line2 += i_dest_margin;
      }

    } else {

      for( i_y = height / 4 ; i_y-- ; )
      {
          p_line1 = p_line2;
          p_line2 += 2 * yuy2_pitch;

          p_y1 = p_y2;
          p_y2 += 2 * y_src_pitch;

          if( i_y > 1 ) {
            p_ub = p_u + 2 * u_src_pitch;
            p_vb = p_v + 2 * v_src_pitch;
          } else {
            p_ub = p_u;
            p_vb = p_v;
          }

          for( i_x = width / 8 ; i_x-- ; )
          {
              MMXEXT_YUV420_YUYV_INTERLACED_ODD( );
          }
          for( i_x = (width % 8) / 2 ; i_x-- ; )
          {
              C_YUV420_YUYV_INTERLACED_ODD( );
          }

          p_y2 += i_source_margin + y_src_pitch;
          p_u += i_source_u_margin + u_src_pitch;
          p_v += i_source_v_margin + v_src_pitch;
          p_ut = p_u - 2 * u_src_pitch;
          p_vt = p_v - 2 * v_src_pitch;
          p_line2 += i_dest_margin + yuy2_pitch;
      }

      p_line2 = yuy2_map + yuy2_pitch;
      p_y2 = y_src + y_src_pitch;
      p_u = u_src + u_src_pitch;
      p_v = v_src + v_src_pitch;
      p_ut = p_u;
      p_vt = p_v;
 
      for( i_y = height / 4 ; i_y-- ; )
      {
          p_line1 = p_line2;
          p_line2 += 2 * yuy2_pitch;

          p_y1 = p_y2;
          p_y2 += 2 * y_src_pitch;

          if( i_y > 1 ) {
            p_ub = p_u + 2 * u_src_pitch;
            p_vb = p_v + 2 * v_src_pitch;
          } else {
            p_ub = p_u;
            p_vb = p_v;
          }

          for( i_x = width / 8 ; i_x-- ; )
          {
              MMXEXT_YUV420_YUYV_INTERLACED_EVEN( );
          }
          for( i_x = (width % 8) / 2 ; i_x-- ; )
          {
              C_YUV420_YUYV_INTERLACED_EVEN( );
          }

          p_y2 += i_source_margin + y_src_pitch;
          p_u += i_source_u_margin + u_src_pitch;
          p_v += i_source_v_margin + v_src_pitch;
          p_ut = p_u - 2 * u_src_pitch;
          p_vt = p_v - 2 * v_src_pitch;
          p_line2 += i_dest_margin + yuy2_pitch;
      }

    }

    sfence();
    emms();

#endif
}

#define C_YUYV_YUV420( )                                          \
    *p_y1++ = *p_line1++; *p_y2++ = *p_line2++;                   \
    *p_u++ = (*p_line1++ + *p_line2++)>>1;                        \
    *p_y1++ = *p_line1++; *p_y2++ = *p_line2++;                   \
    *p_v++ = (*p_line1++ + *p_line2++)>>1;

static void yuy2_to_yv12_c
  (const unsigned char *yuy2_map, int yuy2_pitch,
   unsigned char *y_dst, int y_dst_pitch,
   unsigned char *u_dst, int u_dst_pitch,
   unsigned char *v_dst, int v_dst_pitch,
   int width, int height) {

    const uint8_t *p_line1, *p_line2 = yuy2_map;
    uint8_t *p_y1, *p_y2 = y_dst;
    uint8_t *p_u = u_dst;
    uint8_t *p_v = v_dst;

    int i_x, i_y;

    const int i_dest_margin = y_dst_pitch - width;
    const int i_dest_u_margin = u_dst_pitch - width/2;
    const int i_dest_v_margin = v_dst_pitch - width/2;
    const int i_source_margin = yuy2_pitch - width*2;


    for( i_y = height / 2 ; i_y-- ; )
    {
        p_line1 = p_line2;
        p_line2 += yuy2_pitch;

        p_y1 = p_y2;
        p_y2 += y_dst_pitch;

        for( i_x = width / 8 ; i_x-- ; )
        {
            C_YUYV_YUV420( );
            C_YUYV_YUV420( );
            C_YUYV_YUV420( );
            C_YUYV_YUV420( );
        }

        p_y2 += i_dest_margin;
        p_u += i_dest_u_margin;
        p_v += i_dest_v_margin;
        p_line2 += i_source_margin;
    }
}


#if defined(ARCH_X86) || defined(ARCH_X86_64)

/* yuy2->yv12 with subsampling (some ideas from mplayer's yuy2toyv12) */
#define MMXEXT_YUYV_YUV420( )                                                      \
do {                                                                               \
   __asm__ __volatile__(".align 8 \n\t"                                            \
    "movq       (%0), %%mm0 \n\t"  /* Load              v1 y3 u1 y2 v0 y1 u0 y0 */ \
    "movq      8(%0), %%mm1 \n\t"  /* Load              v3 y7 u3 y6 v2 y5 u2 y4 */ \
    "movq      %%mm0, %%mm2 \n\t"  /*                   v1 y3 u1 y2 v0 y1 u0 y0 */ \
    "movq      %%mm1, %%mm3 \n\t"  /*                   v3 y7 u3 y6 v2 y5 u2 y4 */ \
    "psrlw     $8, %%mm0    \n\t"  /*                   00 v1 00 u1 00 v0 00 u0 */ \
    "psrlw     $8, %%mm1    \n\t"  /*                   00 v3 00 u3 00 v2 00 u2 */ \
    "pand      %%mm7, %%mm2 \n\t"  /*                   00 y3 00 y2 00 y1 00 y0 */ \
    "pand      %%mm7, %%mm3 \n\t"  /*                   00 y7 00 y6 00 y5 00 y4 */ \
    "packuswb  %%mm1, %%mm0 \n\t"  /*                   v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "packuswb  %%mm3, %%mm2 \n\t"  /*                   y7 y6 y5 y4 y3 y2 y1 y0 */ \
    "movntq    %%mm2, (%1)  \n\t"  /* Store YYYYYYYY line1                      */ \
    :                                                                              \
    : "r" (p_line1), "r" (p_y1) );                                                 \
   __asm__ __volatile__(".align 8 \n\t"                                            \
    "movq       (%0), %%mm1 \n\t"  /* Load              v1 y3 u1 y2 v0 y1 u0 y0 */ \
    "movq      8(%0), %%mm2 \n\t"  /* Load              v3 y7 u3 y6 v2 y5 u2 y4 */ \
    "movq      %%mm1, %%mm3 \n\t"  /*                   v1 y3 u1 y2 v0 y1 u0 y0 */ \
    "movq      %%mm2, %%mm4 \n\t"  /*                   v3 y7 u3 y6 v2 y5 u2 y4 */ \
    "psrlw     $8, %%mm1    \n\t"  /*                   00 v1 00 u1 00 v0 00 u0 */ \
    "psrlw     $8, %%mm2    \n\t"  /*                   00 v3 00 u3 00 v2 00 u2 */ \
    "pand      %%mm7, %%mm3 \n\t"  /*                   00 y3 00 y2 00 y1 00 y0 */ \
    "pand      %%mm7, %%mm4 \n\t"  /*                   00 y7 00 y6 00 y5 00 y4 */ \
    "packuswb  %%mm2, %%mm1 \n\t"  /*                   v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "packuswb  %%mm4, %%mm3 \n\t"  /*                   y7 y6 y5 y4 y3 y2 y1 y0 */ \
    "movntq    %%mm3, (%1)  \n\t"  /* Store YYYYYYYY line2                      */ \
    :                                                                              \
    : "r" (p_line2), "r" (p_y2) );                                                 \
   __asm__ __volatile__(                                                           \
    "pavgb     %%mm1, %%mm0 \n\t"  /* (mean)            v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "movq      %%mm0, %%mm1 \n\t"  /*                   v3 u3 v2 u2 v1 u1 v0 u0 */ \
    "psrlw     $8, %%mm0    \n\t"  /*                   00 v3 00 v2 00 v1 00 v0 */ \
    "packuswb  %%mm0, %%mm0 \n\t"  /*                               v3 v2 v1 v0 */ \
    "movd      %%mm0, (%0)  \n\t"  /* Store VVVV                                */ \
    "pand      %%mm7, %%mm1 \n\t"  /*                   00 u3 00 u2 00 u1 00 u0 */ \
    "packuswb  %%mm1, %%mm1 \n\t"  /*                               u3 u2 u1 u0 */ \
    "movd      %%mm1, (%1)  \n\t"  /* Store UUUU                                */ \
    :                                                                              \
    : "r" (p_v), "r" (p_u) );                                                      \
  p_line1 += 16; p_line2 += 16; p_y1 += 8; p_y2 += 8; p_u += 4; p_v += 4;          \
} while(0)

#endif

static void yuy2_to_yv12_mmxext
  (const unsigned char *yuy2_map, int yuy2_pitch,
   unsigned char *y_dst, int y_dst_pitch,
   unsigned char *u_dst, int u_dst_pitch,
   unsigned char *v_dst, int v_dst_pitch,
   int width, int height) {
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    const uint8_t *p_line1, *p_line2 = yuy2_map;
    uint8_t *p_y1, *p_y2 = y_dst;
    uint8_t *p_u = u_dst;
    uint8_t *p_v = v_dst;

    int i_x, i_y;

    const int i_dest_margin = y_dst_pitch - width;
    const int i_dest_u_margin = u_dst_pitch - width/2;
    const int i_dest_v_margin = v_dst_pitch - width/2;
    const int i_source_margin = yuy2_pitch - width*2;

    __asm__ __volatile__(
     "pcmpeqw %mm7, %mm7           \n\t"
     "psrlw $8, %mm7               \n\t" /* 00 ff 00 ff 00 ff 00 ff */
    );

    for( i_y = height / 2 ; i_y-- ; )
    {
        p_line1 = p_line2;
        p_line2 += yuy2_pitch;

        p_y1 = p_y2;
        p_y2 += y_dst_pitch;

        for( i_x = width / 8 ; i_x-- ; )
        {
            MMXEXT_YUYV_YUV420( );
        }

        p_y2 += i_dest_margin;
        p_u += i_dest_u_margin;
        p_v += i_dest_v_margin;
        p_line2 += i_source_margin;
    }

    sfence();
    emms();
#endif
}


/*
 * init_yuv_conversion
 *
 * This function precalculates all of the tables used for converting RGB
 * values to YUV values. This function also decides which conversion
 * functions to use.
 */
void init_yuv_conversion(void) {

  int i;

  /* initialize the RGB -> YUV tables */
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

  /* determine best YUV444 -> YUY2 converter to use */
  if (xine_mm_accel() & MM_ACCEL_X86_MMX)
    yuv444_to_yuy2 = yuv444_to_yuy2_mmx;
  else
    yuv444_to_yuy2 = yuv444_to_yuy2_c;

  /* determine best YV12 -> YUY2 converter to use */
  if (xine_mm_accel() & MM_ACCEL_X86_MMXEXT)
    yv12_to_yuy2 = yv12_to_yuy2_mmxext;
  else
    yv12_to_yuy2 = yv12_to_yuy2_c;

  /* determine best YUY2 -> YV12 converter to use */
  if (xine_mm_accel() & MM_ACCEL_X86_MMXEXT)
    yuy2_to_yv12 = yuy2_to_yv12_mmxext;
  else
    yuy2_to_yv12 = yuy2_to_yv12_c;


  /* determine best YUV9 -> YV12 converter to use (only the portable C
   * version is available so far) */
  yuv9_to_yv12 = yuv9_to_yv12_c;

  /* determine best YUV411 -> YV12 converter to use (only the portable C
   * version is available so far) */
  yuv411_to_yv12 = yuv411_to_yv12_c;

}
