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
 * $Id: xine_goom.c,v 1.1 2002/12/25 04:59:14 miguelfreitas Exp $
 *
 * GOOM post plugin.
 *
 * first version by Mark Thomas
 * ported to post plugin architecture by Miguel Freitas
 * real work by goom author, JC Hoelt <jeko@free.fr>.
 */

#include <stdio.h>

#include "xine_internal.h"
#include "post.h"

#include "goom_core.h"

#define FPS 10
#define DURATION 90000/FPS

#define GOOM_WIDTH  320
#define GOOM_HEIGHT 200

typedef struct post_plugin_goom_s post_plugin_goom_t;

struct post_plugin_goom_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  
  gint16 data [2][512];
  
  int bits;
  int channels;
  int sample_rate;
  int sample_counter;
  int samples_per_frame;
};

/* plugin class initialization function */
static void *goom_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 1, "goom", XINE_VERSION_CODE, NULL, &goom_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


/* plugin class functions */
static post_plugin_t *goom_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *goom_get_identifier(post_class_t *class_gen);
static char          *goom_get_description(post_class_t *class_gen);
static void           goom_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           goom_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            goom_rewire(xine_post_out_t *output, void *data);

static int goom_port_open(xine_audio_port_t *this, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode);

static void goom_port_close(xine_audio_port_t *this, xine_stream_t *stream );

static void goom_port_put_buffer (xine_audio_port_t *this, audio_buffer_t *buf, xine_stream_t *stream);

static void *goom_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));
  
  if (!class)
    return NULL;
  
  class->open_plugin     = goom_open_plugin;
  class->get_identifier  = goom_get_identifier;
  class->get_description = goom_get_description;
  class->dispose         = goom_class_dispose;
  
  return class;
}


static post_plugin_t *goom_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_goom_t *this   = (post_plugin_goom_t *)malloc(sizeof(post_plugin_goom_t));
  xine_post_in_t     *input  = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  xine_post_out_t    *output = (xine_post_out_t *)malloc(sizeof(xine_post_out_t));
  xine_post_out_t    *outputv = (xine_post_out_t *)malloc(sizeof(xine_post_out_t));
  post_audio_port_t  *port;
  
  if (!this || !input || !output || !outputv || !video_target || !video_target[0] ||
      !audio_target || !audio_target[0] ) {
    free(this);
    free(input);
    free(output);
    free(outputv);
    return NULL;
  }
  
  goom_init (GOOM_WIDTH, GOOM_HEIGHT, 0);
  this->sample_counter = 0;
  this->vo_port = video_target[0];
  
  port = post_intercept_audio_port(audio_target[0]);
  port->post = &this->post.xine_post;
  port->port.open = goom_port_open;
  port->port.close = goom_port_close;
  port->port.put_buffer = goom_port_put_buffer;
  
  input->name = "audio in";
  input->type = XINE_POST_DATA_AUDIO;
  input->data = (xine_audio_port_t *)&port->port;

  output->name   = "audio out";
  output->type   = XINE_POST_DATA_AUDIO;
  output->data   = (xine_audio_port_t **)&port->original_port;
  output->rewire = goom_rewire;
  
  outputv->name   = "generated video";
  outputv->type   = XINE_POST_DATA_VIDEO;
  outputv->data   = (xine_video_port_t **)&this->vo_port;
  outputv->rewire = NULL;
  
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
  
  this->post.dispose = goom_dispose;
  
  return &this->post;
}

static char *goom_get_identifier(post_class_t *class_gen)
{
  return "goom";
}

static char *goom_get_description(post_class_t *class_gen)
{
  return "What a GOOM";
}

static void goom_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void goom_dispose(post_plugin_t *this)
{
  goom_close();

  free(this->xine_post.audio_input);
  free(this->xine_post.video_input);
  free(xine_list_first_content(this->input));
  free(xine_list_first_content(this->output));
  xine_list_free(this->input);
  xine_list_free(this->output);
  free(this);
}


static int goom_rewire(xine_post_out_t *output, void *data)
{
  if (!data)
    return 0;
  *(xine_audio_port_t **)output->data = (xine_audio_port_t *)data;
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

static int goom_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_goom_t *this = (post_plugin_goom_t *)port->post;

  this->vo_port->open( this->vo_port, stream );
  
  this->bits = bits;
  this->channels = mode_channels(mode);
  this->samples_per_frame = rate / FPS;
  this->sample_rate = rate; 
  return port->original_port->open(port->original_port, stream, bits, rate, mode );
}

static void goom_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_goom_t *this = (post_plugin_goom_t *)port->post;

  this->vo_port->close( this->vo_port, stream );
 
  port->original_port->close(port->original_port, stream );
}

static void goom_port_put_buffer (xine_audio_port_t *port_gen, 
                             audio_buffer_t *buf, xine_stream_t *stream) {
  
  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_goom_t *this = (post_plugin_goom_t *)port->post;
  vo_frame_t         *frame;
  uint32_t *goom_frame;
  int16_t *data;
  int8_t *data8;
  int i, j;

  this->sample_counter += buf->num_frames;
  
  if( this->sample_counter >= this->samples_per_frame &&
      buf->num_frames >= 512 ) {
    
    data = buf->mem;
    data8 = (int8_t *)buf->mem;
    j = (this->channels >= 2) ? 1 : 0;
        
    if( this->bits == 8 ) {        
      for( i = 0; i < 512; i++, data8 += this->channels ) {
        this->data[0][i] = (int16_t)data8[0] << 8;
        this->data[1][i] = (int16_t)data8[j] << 8;
      }
    } else {
      for( i = 0; i < 512; i++, data += this->channels ) {
        this->data[0][i] = data[0];
        this->data[1][i] = data[j];
      }
    }
        
    goom_frame = goom_update (this->data, 0);
    
    frame = this->vo_port->get_frame (this->vo_port, GOOM_WIDTH, GOOM_HEIGHT,
                                      XINE_VO_ASPECT_SQUARE, XINE_IMGFMT_YUY2,
                                      VO_BOTH_FIELDS);
    frame->pts = buf->vpts;
    frame->duration = 90000 * this->sample_counter / this->sample_rate;
  
    /* FIXME: use accelerated color conversion */
    for (i=0, j=0; i<GOOM_WIDTH * GOOM_HEIGHT; i+=2, j+=4) {
      double r1 = (goom_frame[i]   & 0xFF0000) >> 16;
      double g1 = (goom_frame[i]   &   0xFF00) >>  8;
      double b1 = (goom_frame[i]   &     0xFF);
      double r2 = (goom_frame[i+1] & 0xFF0000) >> 16;
      double g2 = (goom_frame[i+1] &   0xFF00) >>  8;
      double b2 = (goom_frame[i+1] &     0xFF);
      uint8_t y1 = (0.257 * r1) + (0.504 * g1) + (0.098 * b1) + 16;
      uint8_t y2 = (0.257 * r2) + (0.504 * g2) + (0.098 * b2) + 16;
      uint8_t u  = -(0.074 * (r1 + r2)) - (0.1455 * (g1 + g2)) + (0.2195 * (b1 + b2)) + 128;
      uint8_t v  =  (0.2195 * (r1 + r2)) - (0.184 * (g1 + g2)) - (0.0355 * (b1 + b2)) + 128;
  
      frame -> base[0][j]   = y1;
      frame -> base[0][j+1] = u;
      frame -> base[0][j+2] = y2;
      frame -> base[0][j+3] = v;
    
    }
    
    frame->draw(frame, stream);
    frame->free(frame);
    
    this->sample_counter = 0;
  }
  port->original_port->put_buffer(port->original_port, buf, stream );  
}
