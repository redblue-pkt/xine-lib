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
 * Basic Oscilloscope Visualization Post Plugin For xine
 *   by Mike Melanson (melanson@pcisys.net)
 *
 * $Id: oscope.c,v 1.3 2003/01/14 21:00:42 miguelfreitas Exp $
 *
 */

#include <stdio.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "post.h"

#define FPS 20

#define NUMSAMPLES 512

#define OSCOPE_WIDTH  NUMSAMPLES
#define OSCOPE_HEIGHT 256

typedef struct post_plugin_oscope_s post_plugin_oscope_t;

struct post_plugin_oscope_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  xine_stream_t     *stream;

  int data_idx;
  short data [2][NUMSAMPLES];
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

  yuv_planes_t yuv;
};

/**************************************************************************
 * oscope specific decode functions
 *************************************************************************/

static void draw_oscope_dots(post_plugin_oscope_t *this) {

  int i;
  int pixel_ptr;
  int c_delta;

  memset(this->yuv.y, 0x00, OSCOPE_WIDTH * OSCOPE_HEIGHT);
  memset(this->yuv.u, 0x90, OSCOPE_WIDTH * OSCOPE_HEIGHT);
  memset(this->yuv.v, 0x80, OSCOPE_WIDTH * OSCOPE_HEIGHT);

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

  if (this->channels == 2) {

    /* draw stereo scopes */

    /* left channel is at top */
    for (i = 0; i < NUMSAMPLES; i++) {
      pixel_ptr = 
        ((OSCOPE_HEIGHT / 4) + (this->data[0][i] >> 9)) * OSCOPE_WIDTH + i;
      this->yuv.y[pixel_ptr] = 0xFF;
      this->yuv.u[pixel_ptr] = this->u_current;
      this->yuv.v[pixel_ptr] = this->v_current;
    }

    /* right channel is at bottom */
    for (i = 0; i < NUMSAMPLES; i++) {
      pixel_ptr = 
        ((OSCOPE_HEIGHT / 4 * 3) + (this->data[1][i] >> 9)) * OSCOPE_WIDTH + i;
      this->yuv.y[pixel_ptr] = 0xFF;
      this->yuv.u[pixel_ptr] = this->u_current;
      this->yuv.v[pixel_ptr] = this->v_current;
    }

  } else {

    /* draw mono scope */
    for (i = 0; i < NUMSAMPLES; i++) {
      pixel_ptr = 
        ((OSCOPE_HEIGHT / 2) + (this->data[0][i] >> 8)) * OSCOPE_WIDTH + i;
      this->yuv.y[pixel_ptr] = 0xFF;
      this->yuv.u[pixel_ptr] = this->u_current;
      this->yuv.v[pixel_ptr] = this->v_current;
    }

  }

  /* top line */
  for (i = 0, pixel_ptr = 0; i < OSCOPE_WIDTH; i++, pixel_ptr++)
      this->yuv.y[pixel_ptr] = 0xFF;

  /* middle line, only on stereo data */
  if (this->channels == 2) {
    for (i = 0, pixel_ptr = (OSCOPE_HEIGHT/2) * OSCOPE_WIDTH;
         i < OSCOPE_WIDTH; i++, pixel_ptr++)
        this->yuv.y[pixel_ptr] = 0xFF;
  }

  /* bottom line */
  for (i = 0, pixel_ptr = (OSCOPE_HEIGHT - 1) * OSCOPE_WIDTH;
       i < OSCOPE_WIDTH; i++, pixel_ptr++)
      this->yuv.y[pixel_ptr] = 0xFF;

}

/**************************************************************************
 * xine video post plugin functions
 *************************************************************************/

typedef struct post_oscope_out_s post_oscope_out_t;
struct post_oscope_out_s {
  xine_post_out_t     out;
  post_plugin_oscope_t *post;
};

static int oscope_rewire_audio(xine_post_out_t *output_gen, void *data)
{
  post_oscope_out_t *output = (post_oscope_out_t *)output_gen;
  xine_audio_port_t *old_port = *(xine_audio_port_t **)output_gen->data;
  xine_audio_port_t *new_port = (xine_audio_port_t *)data;
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)output->post;
  
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

static int oscope_rewire_video(xine_post_out_t *output_gen, void *data)
{
  post_oscope_out_t *output = (post_oscope_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)output->post;
  
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

static int oscope_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)port->post;

  this->bits = bits;
  this->mode = mode;
  this->channels = mode_channels(mode);
  this->samples_per_frame = rate / FPS;
  this->sample_rate = rate; 
  this->stream = stream;
  this->data_idx = 0;
  init_yuv_planes(&this->yuv, OSCOPE_WIDTH, OSCOPE_HEIGHT);

  return port->original_port->open(port->original_port, stream, bits, rate, mode );
}

static void oscope_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)port->post;

  this->stream = NULL;

  port->original_port->close(port->original_port, stream );
}

static void oscope_port_put_buffer (xine_audio_port_t *port_gen, 
                             audio_buffer_t *buf, xine_stream_t *stream) {
  
  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)port->post;
  vo_frame_t         *frame;
  int16_t *data;
  int8_t *data8;
  int samples_used = 0;
  uint64_t vpts = buf->vpts;
  int i, j;
  
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
  
  j = (this->channels >= 2) ? 1 : 0;

  do {
    
    if( this->bits == 8 ) {
      data8 = (int8_t *)buf->mem;
      data8 += samples_used * this->channels;
  
      /* scale 8 bit data to 16 bits and convert to signed as well */
      for( i = 0; i < buf->num_frames && this->data_idx < NUMSAMPLES;
           i++, this->data_idx++, data8 += this->channels ) {
        this->data[0][this->data_idx] = ((int16_t)data8[0] << 8) - 0x8000;
        this->data[1][this->data_idx] = ((int16_t)data8[j] << 8) - 0x8000;
      }
    } else {
      data = buf->mem;
      data += samples_used * this->channels;
  
      for( i = 0; i < buf->num_frames && this->data_idx < NUMSAMPLES;
           i++, this->data_idx++, data += this->channels ) {
        this->data[0][this->data_idx] = data[0];
        this->data[1][this->data_idx] = data[j];
      }
    }
  
    if( this->sample_counter >= this->samples_per_frame &&
        this->data_idx == NUMSAMPLES ) {
  
      this->data_idx = 0;
      samples_used += this->samples_per_frame;
  
      frame = this->vo_port->get_frame (this->vo_port, OSCOPE_WIDTH, OSCOPE_HEIGHT,
                                        XINE_VO_ASPECT_SQUARE, XINE_IMGFMT_YUY2,
                                        VO_BOTH_FIELDS);
      frame->pts = vpts;
      vpts = 0;
      frame->duration = 90000 * this->samples_per_frame / this->sample_rate;
      this->sample_counter -= this->samples_per_frame;
          
      draw_oscope_dots(this);
      yuv444_to_yuy2(&this->yuv, frame->base[0], frame->pitches[0]);
  
      frame->draw(frame, stream);
      frame->free(frame);

    }
  } while( this->sample_counter >= this->samples_per_frame );
}

static void oscope_dispose(post_plugin_t *this_gen)
{
  post_plugin_oscope_t *this = (post_plugin_oscope_t *)this_gen;
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
static post_plugin_t *oscope_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_oscope_t *this   = (post_plugin_oscope_t *)malloc(sizeof(post_plugin_oscope_t));
  xine_post_in_t     *input  = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_oscope_out_t    *output = (post_oscope_out_t *)malloc(sizeof(post_oscope_out_t));
  post_oscope_out_t    *outputv = (post_oscope_out_t *)malloc(sizeof(post_oscope_out_t));
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
  port->port.open = oscope_port_open;
  port->port.close = oscope_port_close;
  port->port.put_buffer = oscope_port_put_buffer;
  
  input->name = "audio in";
  input->type = XINE_POST_DATA_AUDIO;
  input->data = (xine_audio_port_t *)&port->port;

  output->out.name   = "audio out";
  output->out.type   = XINE_POST_DATA_AUDIO;
  output->out.data   = (xine_audio_port_t **)&port->original_port;
  output->out.rewire = oscope_rewire_audio;
  output->post       = this;
  
  outputv->out.name   = "generated video";
  outputv->out.type   = XINE_POST_DATA_VIDEO;
  outputv->out.data   = (xine_video_port_t **)&this->vo_port;
  outputv->out.rewire = oscope_rewire_video;
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
  
  this->post.dispose = oscope_dispose;

  return &this->post;
}

static char *oscope_get_identifier(post_class_t *class_gen)
{
  return "Oscillicope";
}

static char *oscope_get_description(post_class_t *class_gen)
{
  return "Oscilloscope";
}

static void oscope_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}

/* plugin class initialization function */
void *oscope_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));
  
  if (!class)
    return NULL;
  
  class->open_plugin     = oscope_open_plugin;
  class->get_identifier  = oscope_get_identifier;
  class->get_description = oscope_get_description;
  class->dispose         = oscope_class_dispose;
  
  return class;
}
