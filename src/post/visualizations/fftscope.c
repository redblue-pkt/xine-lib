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
 * Fast Fourier Transform Visualization Post Plugin For xine
 *   by Mike Melanson (melanson@pcisys.net)
 *
 * FFT code by Steve Haehnichen, originally licensed under GPL v1
 *
 * $Id: fftscope.c,v 1.10 2003/02/17 00:02:52 tmattern Exp $
 *
 */

#include <stdio.h>
#include <math.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "post.h"
#include "bswap.h"

#define FPS 20

#define FFT_WIDTH  512
#define FFT_HEIGHT 256

#define NUMSAMPLES 512
#define MAXCHANNELS  6

typedef struct post_plugin_fftscope_s post_plugin_fftscope_t;

struct _complex
{
  double re;
  double im;
};
typedef struct _complex complex;

struct post_plugin_fftscope_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  xine_stream_t     *stream;

  int data_idx;
  complex wave[MAXCHANNELS][NUMSAMPLES];
  int amp_max[MAXCHANNELS][NUMSAMPLES / 2];
  uint8_t amp_max_y[MAXCHANNELS][NUMSAMPLES / 2];
  uint8_t amp_max_u[MAXCHANNELS][NUMSAMPLES / 2];
  uint8_t amp_max_v[MAXCHANNELS][NUMSAMPLES / 2];
  int     amp_age[MAXCHANNELS][NUMSAMPLES / 2];
  audio_buffer_t buf;   /* dummy buffer just to hold a copy of audio data */

  int bits;
  int mode;
  int channels;
  int sample_rate;
  int sample_counter;
  int samples_per_frame;

  unsigned char u_current;
  unsigned char v_current;
  int u_direction;
  int v_direction;
};

/**************************************************************************
 * fftscope specific decode functions
 *************************************************************************/

#define LOG_BITS 9

# define                SINE(x)         SineTable[(x)]
# define                COSINE(x)       CosineTable[(x)]
# define                WINDOW(x)       WinTable[(x)]
static double            *SineTable, *CosineTable, *WinTable;

#define PERMUTE(x, y)   reverse((x), (y))

/* Number of samples in one "frame" */
#define SAMPLES         (1 << bits)
#define REAL(x)         wave[(x)].re
#define IMAG(x)         wave[(x)].im
#define ALPHA           0.54

/*
 *  Bit reverser for unsigned ints
 *  Reverses 'bits' bits.
 */
inline const unsigned int
reverse (unsigned int val, int bits)
{
  unsigned int retn = 0;

  while (bits--)
    {
      retn <<= 1;
      retn |= (val & 1);
      val >>= 1;
    }
  return (retn);
}

/*
 *  Here is the real work-horse.
 *  It's a generic FFT, so nothing is lost or approximated.
 *  The samples in wave[] should be in order, and they
 *  will be decimated when fft() returns.
 */
static void fft (complex wave[], int bits)
{
  register int  loop, loop1, loop2;
  unsigned      i1;             /* going to right shift this */
  int           i2, i3, i4, y;
  double         a1, a2, b1, b2, z1, z2;

  i1 = SAMPLES / 2;
  i2 = 1;

  /* perform the butterflys */

  for (loop = 0; loop < bits; loop++)
    {
      i3 = 0;
      i4 = i1;

      for (loop1 = 0; loop1 < i2; loop1++)
        {
          y  = PERMUTE(i3 / (int)i1, bits);
          z1 = COSINE(y);
          z2 = -SINE(y);

          for (loop2 = i3; loop2 < i4; loop2++)
            {
              a1 = REAL(loop2);
              a2 = IMAG(loop2);

              b1 = z1 * REAL(loop2+i1) - z2 * IMAG(loop2+i1);
              b2 = z2 * REAL(loop2+i1) + z1 * IMAG(loop2+i1);

              REAL(loop2) = a1 + b1;
              IMAG(loop2) = a2 + b2;

              REAL(loop2+i1) = a1 - b1;
              IMAG(loop2+i1) = a2 - b2;
            }

          i3 += (i1 << 1);
          i4 += (i1 << 1);
        }

      i1 >>= 1;
      i2 <<= 1;
    }
}

/*
 *  Initializer for FFT routines.  Currently only sets up tables.
 *  - Generates scaled lookup tables for sin() and cos()
 *  - Fills a table for the Hamming window function
 */
static void fft_init (int bits)
{
  int i;
  const double   TWOPIoN   = (atan(1.0) * 8.0) / (double)SAMPLES;
  const double   TWOPIoNm1 = (atan(1.0) * 8.0) / (double)(SAMPLES - 1);

  SineTable   = malloc (sizeof(double) * SAMPLES);
  CosineTable = malloc (sizeof(double) * SAMPLES);
  WinTable    = malloc (sizeof(double) * SAMPLES);
  for (i=0; i < SAMPLES; i++)
    {
      SineTable[i]   = sin((double) i * TWOPIoN);
      CosineTable[i] = cos((double) i * TWOPIoN);
      /*
       * Generalized Hamming window function.
       * Set ALPHA to 0.54 for a hanning window. (Good idea)
       */
      WinTable[i] = ALPHA + ((1.0 - ALPHA)
                                * cos (TWOPIoNm1 * (i - SAMPLES/2)));
    }
}

/*
 *  Apply some windowing function to the samples.
 */
static void window (complex wave[], int bits)
{
  int i;

  for (i = 0; i < SAMPLES; i++)
    {
      REAL(i) *= WINDOW(i);
      IMAG(i) *= WINDOW(i);
    }
}

/*
 *  Calculate amplitude of component n in the decimated wave[] array.
 */
static double amp (int n, complex wave[], int bits)
{
  n = PERMUTE (n, bits);
  return (hypot (REAL(n), IMAG(n)));
}

/*
 *  Calculate phase of component n in the decimated wave[] array.
 */
static double phase (int n, complex wave[], int bits)
{
  n = PERMUTE (n, bits);
  if (REAL(n) != 0.0)
    return (atan (IMAG(n) / REAL(n)));
  else
    return (0.0);
}

/*
 *  Scale sampled values.
 *  Do this *before* the fft.
 */
static void scale (complex wave[], int bits)
{
  int i;

  for (i = 0; i < SAMPLES; i++)  {
    wave[i].re /= SAMPLES;
    wave[i].im /= SAMPLES;
  }
}

/*
 *  Fade out a YUV pixel
 */
void fade_out_yuv(uint8_t *y, uint8_t *u, uint8_t *v, float factor) {
#if 0
  float r, g, b;

  /* YUV -> RGB */
  r = 1.164 * (*y - 16) + 1.596 * (*v - 128);
  g = 1.164 * (*y - 16) - 0.813 * (*v - 128) - 0.391 * (*u - 128);
  b = 1.164 * (*y - 16) + 2.018 * (*u - 128);

  /* fade out by a 0.9 factor */
  r *= factor;
  g *= factor;
  b *= factor;

  /* RGB -> YUV */
  *y = (uint8_t)((0.257 * r) + (0.504 * g) + (0.098 * b) + 16);
  *u = (uint8_t)(-(0.148 * r) - (0.291 * g) + (0.439 * b) + 128);
  *v = (uint8_t)((0.439 * r) - (0.368 * g) - (0.071 * b) + 128);
#endif

  *y = (uint8_t)(factor * (*y - 16)) + 16;
  *u = (uint8_t)(factor * (*u - 128)) + 128;
  *v = (uint8_t)(factor * (*v - 128)) + 128;
}


static void draw_fftscope(post_plugin_fftscope_t *this, vo_frame_t *frame) {

  int i, j, c;
  int map_ptr, map_ptr_bkp;
  int amp_int, amp_max, x;
  float amp_float;
  uint32_t yuy2_pair, yuy2_pair_max, yuy2_white;
  int c_delta;

  /* clear the YUY2 map */
  for (i = 0; i < FFT_WIDTH * FFT_HEIGHT / 2; i++)
    ((uint32_t *)frame->base[0])[i] = be2me_32(0x00900080);

  /* get a random delta between 1..6 */
  c_delta = (rand() % 6) + 1;
  /* apply it to the current U value */
  if (this->u_direction) {
    if (this->u_current + c_delta > 255) {
      this->u_current = 255;
      this->u_direction = 0;
    } else
      this->u_current += c_delta;
  } else {
    if (this->u_current - c_delta < 0) {
      this->u_current = 0;
      this->u_direction = 1;
    } else
      this->u_current -= c_delta;
  }

  /* get a random delta between 1..3 */
  c_delta = (rand() % 3) + 1;
  /* apply it to the current V value */
  if (this->v_direction) {
    if (this->v_current + c_delta > 255) {
      this->v_current = 255;
      this->v_direction = 0;
    } else
      this->v_current += c_delta;
  } else {
    if (this->v_current - c_delta < 0) {
      this->v_current = 0;
      this->v_direction = 1;
    } else
      this->v_current -= c_delta;
  }

  yuy2_pair = be2me_32(
    (0x7F << 24) |
    (this->u_current << 16) |
    (0x7F << 8) |
    this->v_current);

  yuy2_white = be2me_32(
    (0xFF << 24) |
    (0x80 << 16) |
    (0xFF << 8) |
    0x80);

  for (c = 0; c < this->channels; c++){
    /* perform FFT for channel data */
    window(this->wave[c], LOG_BITS);
    scale(this->wave[c], LOG_BITS);
    fft(this->wave[c], LOG_BITS);

    /* plot the FFT points for the channel */
    for (i = 0; i < NUMSAMPLES / 2; i++) {

      map_ptr = ((FFT_HEIGHT * (c+1) / this->channels -1 ) * FFT_WIDTH + i * 2) / 2;
      map_ptr_bkp = map_ptr;
      amp_float = amp(i, this->wave[c], LOG_BITS);
      if (amp_float == 0)
        amp_int = 0;
      else
        amp_int = (int)((60/this->channels) * log10(amp_float));
      if (amp_int > 255/this->channels)
        amp_int = 255/this->channels;
      if (amp_int < 0)
        amp_int = 0;

      for (j = 0; j < amp_int; j++, map_ptr -= FFT_WIDTH / 2)
        ((uint32_t *)frame->base[0])[map_ptr] = yuy2_pair;

      /* amp max */
      yuy2_pair_max = be2me_32(
        (this->amp_max_y[c][i] << 24) |
        (this->amp_max_u[c][i] << 16) |
        (this->amp_max_y[c][i] << 8) |
        this->amp_max_v[c][i]);

      /* gravity */
      this->amp_age[c][i]++;
      if (this->amp_age[c][i] < 10) {
        amp_max = this->amp_max[c][i];
      } else {
        x = this->amp_age[c][i] - 10;
        amp_max = this->amp_max[c][i] - x * x;
      }

      /* new peak ? */
      if (amp_int > amp_max) {
        this->amp_max[c][i] = amp_int;
        this->amp_age[c][i] = 0;
        this->amp_max_y[c][i] = 0x7f;
        this->amp_max_u[c][i] = this->u_current;
        this->amp_max_v[c][i] = this->v_current;
        fade_out_yuv(&this->amp_max_y[c][i], &this->amp_max_u[c][i],
          &this->amp_max_v[c][i], 0.5);
        amp_max = amp_int;
      } else {
        fade_out_yuv(&this->amp_max_y[c][i], &this->amp_max_u[c][i],
          &this->amp_max_v[c][i], 0.95);
      }

      /* draw peaks */
      for (j = amp_int; j < (amp_max - 1); j++, map_ptr -= FFT_WIDTH / 2)
        ((uint32_t *)frame->base[0])[map_ptr] = yuy2_pair_max;

      /* top */
      ((uint32_t *)frame->base[0])[map_ptr] = yuy2_white;

      /* persistence of top */
      if (this->amp_age[c][i] >= 10) {
        x = this->amp_age[c][i] - 10;
        x = 0x5f - x;
        if (x < 0x10) x = 0x10;
        ((uint32_t *)frame->base[0])[map_ptr_bkp -
          this->amp_max[c][i] * (FFT_WIDTH / 2)] =
            be2me_32((x << 24) | (0x80 << 16) | (x << 8) | 0x80);
      }
    }
  }

  /* top line */
  for (map_ptr = 0; map_ptr < FFT_WIDTH / 2; map_ptr++)
    ((uint32_t *)frame->base[0])[map_ptr] = yuy2_white;

  /* lines under each channel */
  for (c = 0; c < this->channels; c++){
    for (i = 0, map_ptr = ((FFT_HEIGHT * (c+1) / this->channels -1 ) * FFT_WIDTH) / 2;
       i < FFT_WIDTH / 2; i++, map_ptr++)
    ((uint32_t *)frame->base[0])[map_ptr] = yuy2_white;
  }

}

/**************************************************************************
 * xine video post plugin functions
 *************************************************************************/

typedef struct post_fftscope_out_s post_fftscope_out_t;
struct post_fftscope_out_s {
  xine_post_out_t     out;
  post_plugin_fftscope_t *post;
};

static int fftscope_rewire_audio(xine_post_out_t *output_gen, void *data)
{
  post_fftscope_out_t *output = (post_fftscope_out_t *)output_gen;
  xine_audio_port_t *old_port = *(xine_audio_port_t **)output_gen->data;
  xine_audio_port_t *new_port = (xine_audio_port_t *)data;
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)output->post;

  if (!data)
    return 0;
  if (this->stream) {
    /* register our stream at the new output port */
    old_port->close(old_port, this->stream);
    new_port->open(new_port, this->stream, this->bits, this->sample_rate, this->mode);
  }
  /* reconnect ourselves */
  *(xine_audio_port_t **)output_gen->data = new_port;
  return 1;
}

static int fftscope_rewire_video(xine_post_out_t *output_gen, void *data)
{
  post_fftscope_out_t *output = (post_fftscope_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)output->post;

  if (!data)
    return 0;
  if (this->stream) {
    /* register our stream at the new output port */
    old_port->close(old_port, this->stream);
    new_port->open(new_port, this->stream);
  }
  /* reconnect ourselves */
  *(xine_video_port_t **)output_gen->data = new_port;
  return 1;
}

static int mode_channels( int mode ) {
  switch( mode ) {
  case AO_CAP_MODE_MONO:
    return 1;
  case AO_CAP_MODE_STEREO:
    return 2;
  case AO_CAP_MODE_4CHANNEL:
    return 4;
  case AO_CAP_MODE_5CHANNEL:
    return 5;
  case AO_CAP_MODE_5_1CHANNEL:
    return 6;
  }
  return 0;
}

static int fftscope_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)port->post;
  int c, i;

  this->bits = bits;
  this->mode = mode;
  this->channels = mode_channels(mode);
  if( this->channels > MAXCHANNELS )
    this->channels = MAXCHANNELS;
  this->samples_per_frame = rate / FPS;
  this->sample_rate = rate;
  this->stream = stream;
  this->data_idx = 0;
  fft_init(LOG_BITS);

  for (c = 0; c < this->channels; c++) {
    for (i = 0; i < (NUMSAMPLES / 2); i++) {
      this->amp_max[c][i]   = 0;
      this->amp_max_y[c][i] = 0;
      this->amp_max_u[c][i] = 0;
      this->amp_max_v[c][i] = 0;
      this->amp_age[c][i]   = 0;
    }
  }

  return port->original_port->open(port->original_port, stream, bits, rate, mode );
}

static void fftscope_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)port->post;

  this->stream = NULL;

  port->original_port->close(port->original_port, stream );
}

static void fftscope_port_put_buffer (xine_audio_port_t *port_gen,
                             audio_buffer_t *buf, xine_stream_t *stream) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)port->post;
  vo_frame_t         *frame;
  int16_t *data;
  int8_t *data8;
  int samples_used = 0;
  uint64_t vpts = buf->vpts;
  int i, c;

  /* make a copy of buf data for private use */
  if( this->buf.mem_size < buf->mem_size ) {
    this->buf.mem = realloc(this->buf.mem, buf->mem_size);
    this->buf.mem_size = buf->mem_size;
  }
  memcpy(this->buf.mem, buf->mem,
         buf->num_frames*this->channels*((this->bits == 8)?1:2));
  this->buf.num_frames = buf->num_frames;

  /* pass data to original port */
  port->original_port->put_buffer(port->original_port, buf, stream );

  /* we must not use original data anymore, it should have already being moved
   * to the fifo of free audio buffers. just use our private copy instead.
   */
  buf = &this->buf;

  this->sample_counter += buf->num_frames;

  do {

    if( this->bits == 8 ) {
      data8 = (int8_t *)buf->mem;
      data8 += samples_used * this->channels;

      /* scale 8 bit data to 16 bits and convert to signed as well */
      for( i = 0; i < buf->num_frames && this->data_idx < NUMSAMPLES;
           i++, this->data_idx++, data8 += this->channels ) {
        for( c = 0; c < this->channels; c++){
          this->wave[c][this->data_idx].re = (double)(data8[c] << 8) - 0x8000;
          this->wave[c][this->data_idx].im = 0;
        }
      }
    } else {
      data = buf->mem;
      data += samples_used * this->channels;

      for( i = 0; i < buf->num_frames && this->data_idx < NUMSAMPLES;
           i++, this->data_idx++, data += this->channels ) {
        for( c = 0; c < this->channels; c++){
          this->wave[c][this->data_idx].re = (double)data[c];
          this->wave[c][this->data_idx].im = 0;
        }
      }
    }

    if( this->sample_counter >= this->samples_per_frame &&
        this->data_idx == NUMSAMPLES ) {

      this->data_idx = 0;
      samples_used += this->samples_per_frame;

      frame = this->vo_port->get_frame (this->vo_port, FFT_WIDTH, FFT_HEIGHT,
                                        XINE_VO_ASPECT_SQUARE, XINE_IMGFMT_YUY2,
                                        VO_BOTH_FIELDS);
      frame->pts = vpts;
      vpts = 0;
      frame->duration = 90000 * this->samples_per_frame / this->sample_rate;
      this->sample_counter -= this->samples_per_frame;

      draw_fftscope(this, frame);

      frame->draw(frame, stream);
      frame->free(frame);
    }
  } while( this->sample_counter >= this->samples_per_frame );
}

static void fftscope_dispose(post_plugin_t *this_gen)
{
  post_plugin_fftscope_t *this = (post_plugin_fftscope_t *)this_gen;
  xine_post_out_t *output = (xine_post_out_t *)xine_list_first_content(this_gen->output);
  xine_video_port_t *port = *(xine_video_port_t **)output->data;

  if (this->stream)
    port->close(port, this->stream);

  free(this->post.xine_post.audio_input);
  free(this->post.xine_post.video_input);
  free(xine_list_first_content(this->post.input));
  free(xine_list_first_content(this->post.output));
  xine_list_free(this->post.input);
  xine_list_free(this->post.output);
  if(this->buf.mem)
    free(this->buf.mem);
  free(this);
}

/* plugin class functions */
static post_plugin_t *fftscope_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_fftscope_t *this   = (post_plugin_fftscope_t *)malloc(sizeof(post_plugin_fftscope_t));
  xine_post_in_t     *input  = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_fftscope_out_t    *output = (post_fftscope_out_t *)malloc(sizeof(post_fftscope_out_t));
  post_fftscope_out_t    *outputv = (post_fftscope_out_t *)malloc(sizeof(post_fftscope_out_t));
  post_audio_port_t  *port;

  if (!this || !input || !output || !outputv || !video_target || !video_target[0] ||
      !audio_target || !audio_target[0] ) {
    free(this);
    free(input);
    free(output);
    free(outputv);
    return NULL;
  }

  this->sample_counter = 0;
  this->stream  = NULL;
  this->vo_port = video_target[0];
  this->buf.mem = NULL;
  this->buf.mem_size = 0;

  port = post_intercept_audio_port(&this->post, audio_target[0]);
  port->port.open = fftscope_port_open;
  port->port.close = fftscope_port_close;
  port->port.put_buffer = fftscope_port_put_buffer;

  input->name = "audio in";
  input->type = XINE_POST_DATA_AUDIO;
  input->data = (xine_audio_port_t *)&port->port;

  output->out.name   = "audio out";
  output->out.type   = XINE_POST_DATA_AUDIO;
  output->out.data   = (xine_audio_port_t **)&port->original_port;
  output->out.rewire = fftscope_rewire_audio;
  output->post       = this;

  outputv->out.name   = "generated video";
  outputv->out.type   = XINE_POST_DATA_VIDEO;
  outputv->out.data   = (xine_video_port_t **)&this->vo_port;
  outputv->out.rewire = fftscope_rewire_video;
  outputv->post       = this;

  this->post.xine_post.audio_input    = (xine_audio_port_t **)malloc(sizeof(xine_audio_port_t *) * 2);
  this->post.xine_post.audio_input[0] = &port->port;
  this->post.xine_post.audio_input[1] = NULL;
  this->post.xine_post.video_input    = (xine_video_port_t **)malloc(sizeof(xine_video_port_t *) * 1);
  this->post.xine_post.video_input[0] = NULL;

  this->post.input  = xine_list_new();
  this->post.output = xine_list_new();

  xine_list_append_content(this->post.input, input);
  xine_list_append_content(this->post.output, output);
  xine_list_append_content(this->post.output, outputv);

  this->post.dispose = fftscope_dispose;

  return &this->post;
}

static char *fftscope_get_identifier(post_class_t *class_gen)
{
  return "FFT Scope";
}

static char *fftscope_get_description(post_class_t *class_gen)
{
  return "FFT Scope";
}

static void fftscope_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}

/* plugin class initialization function */
void *fftscope_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));
  
  if (!class)
    return NULL;
  
  class->open_plugin     = fftscope_open_plugin;
  class->get_identifier  = fftscope_get_identifier;
  class->get_description = fftscope_get_description;
  class->dispose         = fftscope_class_dispose;
  
  return class;
}
