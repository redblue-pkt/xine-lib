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
 * Reference Visualization Post Plugin For xine
 *   by Mike Melanson (melanson@pcisys.net)
 * This is an example/template for the xine visualization post plugin
 * process. It simply paints the screen a solid color and rotates through
 * colors on each iteration.
 *
 * $Id: fooviz.c,v 1.7 2003/05/31 18:33:30 miguelfreitas Exp $
 *
 */

#include <stdio.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "post.h"

#define FPS 20

#define FOO_WIDTH  320
#define FOO_HEIGHT 240

#define NUMSAMPLES 512

typedef struct post_plugin_fooviz_s post_plugin_fooviz_t;

struct post_plugin_fooviz_s {
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

  /* specific to fooviz */
  unsigned char current_yuv_byte;
};

/**************************************************************************
 * fooviz specific decode functions
 *************************************************************************/


/**************************************************************************
 * xine video post plugin functions
 *************************************************************************/

typedef struct post_fooviz_out_s post_fooviz_out_t;
struct post_fooviz_out_s {
  xine_post_out_t     out;
  post_plugin_fooviz_t *post;
};

static int fooviz_rewire_audio(xine_post_out_t *output_gen, void *data)
{
  post_fooviz_out_t *output = (post_fooviz_out_t *)output_gen;
  xine_audio_port_t *old_port = *(xine_audio_port_t **)output_gen->data;
  xine_audio_port_t *new_port = (xine_audio_port_t *)data;
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)output->post;
  
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

static int fooviz_rewire_video(xine_post_out_t *output_gen, void *data)
{
  post_fooviz_out_t *output = (post_fooviz_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)output->post;
  
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

static int fooviz_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)port->post;

  this->bits = bits;
  this->mode = mode;
  this->channels = mode_channels(mode);
  this->samples_per_frame = rate / FPS;
  this->sample_rate = rate; 
  this->stream = stream;
  this->data_idx = 0;

  return port->original_port->open(port->original_port, stream, bits, rate, mode );
}

static void fooviz_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)port->post;

  this->stream = NULL;
 
  port->original_port->close(port->original_port, stream );
}

static void fooviz_port_put_buffer (xine_audio_port_t *port_gen, 
                             audio_buffer_t *buf, xine_stream_t *stream) {
  
  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)port->post;
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
  
      frame = this->vo_port->get_frame (this->vo_port, FOO_WIDTH, FOO_HEIGHT,
                                        XINE_VO_ASPECT_SQUARE, XINE_IMGFMT_YUY2,
                                        VO_BOTH_FIELDS);
      frame->extra_info->invalid = 1;
      frame->bad_frame = 0;
      frame->pts = vpts;
      vpts = 0;
      frame->duration = 90000 * this->samples_per_frame / this->sample_rate;
      this->sample_counter -= this->samples_per_frame;

      memset(frame->base[0], this->current_yuv_byte, FOO_WIDTH * FOO_HEIGHT * 2);
      this->current_yuv_byte += 3;

      frame->draw(frame, stream);
      frame->free(frame);
    }
  } while( this->sample_counter >= this->samples_per_frame );
}

static void fooviz_dispose(post_plugin_t *this_gen)
{
  post_plugin_fooviz_t *this = (post_plugin_fooviz_t *)this_gen;
  xine_post_out_t *output = (xine_post_out_t *)xine_list_last_content(this_gen->output);
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
static post_plugin_t *fooviz_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_fooviz_t *this   = (post_plugin_fooviz_t *)malloc(sizeof(post_plugin_fooviz_t));
  xine_post_in_t     *input  = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_fooviz_out_t    *output = (post_fooviz_out_t *)malloc(sizeof(post_fooviz_out_t));
  post_fooviz_out_t    *outputv = (post_fooviz_out_t *)malloc(sizeof(post_fooviz_out_t));
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
  port->port.open = fooviz_port_open;
  port->port.close = fooviz_port_close;
  port->port.put_buffer = fooviz_port_put_buffer;
  
  input->name = "audio in";
  input->type = XINE_POST_DATA_AUDIO;
  input->data = (xine_audio_port_t *)&port->port;

  output->out.name   = "audio out";
  output->out.type   = XINE_POST_DATA_AUDIO;
  output->out.data   = (xine_audio_port_t **)&port->original_port;
  output->out.rewire = fooviz_rewire_audio;
  output->post       = this;
  
  outputv->out.name   = "generated video";
  outputv->out.type   = XINE_POST_DATA_VIDEO;
  outputv->out.data   = (xine_video_port_t **)&this->vo_port;
  outputv->out.rewire = fooviz_rewire_video;
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
  
  this->post.dispose = fooviz_dispose;

  return &this->post;
}

static char *fooviz_get_identifier(post_class_t *class_gen)
{
  return "fooviz";
}

static char *fooviz_get_description(post_class_t *class_gen)
{
  return "fooviz";
}

static void fooviz_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}

/* plugin class initialization function */
static void *fooviz_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));
  
  if (!class)
    return NULL;
  
  class->open_plugin     = fooviz_open_plugin;
  class->get_identifier  = fooviz_get_identifier;
  class->get_description = fooviz_get_description;
  class->dispose         = fooviz_class_dispose;
  
  return class;
}

/* plugin catalog information */
post_info_t fooviz_special_info = { XINE_POST_TYPE_AUDIO_VISUALIZATION };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 3, "fooviz", XINE_VERSION_CODE, &fooviz_special_info, &fooviz_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
