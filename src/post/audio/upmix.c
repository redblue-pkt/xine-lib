/*
 * Copyright (C) 2000-2004 the xine project
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
 * $Id: upmix.c,v 1.5 2004/05/16 15:13:34 jcdutton Exp $
 *
 */

#include <stdio.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "post.h"
#include "dsp.h"

#define FPS 20

#define FOO_WIDTH  320
#define FOO_HEIGHT 240

#define NUMSAMPLES 512

typedef struct post_plugin_upmix_s post_plugin_upmix_t;

typedef struct post_class_upmix_s post_class_upmix_t;

struct post_class_upmix_s {
  post_class_t        post_class;

  xine_t             *xine;
};
                                                                                                                           
/* Q value for low-pass filter */
#define Q 1.0
                                                                                                                           
/* Analog domain biquad section */
typedef struct{
  float a[3];           /* Numerator coefficients */
  float b[3];           /* Denominator coefficients */
} biquad_t;
                                                                                                                           
/* S-parameters for designing 4th order Butterworth filter */
static biquad_t s_param[2] = {{{1.0,0.0,0.0},{1.0,0.765367,1.0}},
                         {{1.0,0.0,0.0},{1.0,1.847759,1.0}}};
                                                                                                                           
/* Data for specific instances of this filter */
typedef struct af_sub_s
{
  float w[2][4];        /* Filter taps for low-pass filter */
  float q[2][2];        /* Circular queues */
  float fc;             /* Cutoff frequency [Hz] for low-pass filter */
  float k;              /* Filter gain */
}af_sub_t;

#ifndef IIR
#define IIR(in,w,q,out) { \
  float h0 = (q)[0]; \
  float h1 = (q)[1]; \
  float hn = (in) - h0 * (w)[0] - h1 * (w)[1];  \
  out = hn + h0 * (w)[2] + h1 * (w)[3];  \
  (q)[1] = h0; \
  (q)[0] = hn; \
}
#endif

struct post_plugin_upmix_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  post_out_t         video_output;

  /* private metronom for syncing the video */
  metronom_t        *metronom;
  
  double ratio;

  int data_idx;
  short data [2][NUMSAMPLES];
  audio_buffer_t *buf;   /* dummy buffer just to hold a copy of audio data */
  af_sub_t *sub;

  int channels;
  int channels_out;
  int sample_counter;
  int samples_per_frame;

  /* specific to upmix */
  unsigned char current_yuv_byte;
};

/**************************************************************************
 * upmix specific decode functions
 *************************************************************************/


/**************************************************************************
 * xine video post plugin functions
 *************************************************************************/

static int upmix_rewire_video(xine_post_out_t *output_gen, void *data)
{
  post_out_t *output = (post_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  post_plugin_upmix_t *this = (post_plugin_upmix_t *)output->post;
  
  if (!data)
    return 0;
  /* register our stream at the new output port */
  old_port->close(old_port, NULL);
  new_port->open(new_port, NULL);
  /* reconnect ourselves */
  this->vo_port = new_port;
  return 1;
}

static int upmix_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_upmix_t *this = (post_plugin_upmix_t *)port->post;
  uint32_t capabilities;

  _x_post_rewire(&this->post);
  _x_post_inc_usage(port);
  
  if (stream)
    port->stream = stream;
  else
    port->stream = POST_NULL_STREAM;
  port->bits = bits;
  port->rate = rate;
  port->mode = mode;
  capabilities = port->original_port->get_capabilities(port->original_port);
  
  this->ratio = (double)FOO_WIDTH/(double)FOO_HEIGHT;
  this->channels = _x_ao_mode2channels(mode);
  /* FIXME: Handle all desired output formats */
  if ((capabilities & AO_CAP_MODE_5_1CHANNEL) && (capabilities & AO_CAP_32BITS)) {
    this->channels_out=6;
    mode = AO_CAP_MODE_5_1CHANNEL;
    bits = 32; /* Upmix to Floats */
  } else {
    this->channels_out=2;
  }

  this->sub = xine_xmalloc(sizeof(af_sub_t));
  if (!this->sub) {
    return 0;
  }
  this->sub->fc = 60;    /* LFE Cutoff frequency 60Hz */
  this->sub->k = 1.0;
    if((-1 == szxform(s_param[0].a, s_param[0].b, Q, this->sub->fc,
       (float)rate, &this->sub->k, this->sub->w[0])) ||
       (-1 == szxform(s_param[1].a, s_param[1].b, Q, this->sub->fc,
       (float)rate, &this->sub->k, this->sub->w[1]))) {
    free(this->sub);
    this->sub=NULL;
    return 0;
  }

  this->samples_per_frame = rate / FPS;
  this->data_idx = 0;

  this->vo_port->open(this->vo_port, NULL);
  this->metronom->set_master(this->metronom, stream->metronom);

  return port->original_port->open(port->original_port, stream, bits, rate, mode );
}

static void upmix_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_upmix_t *this = (post_plugin_upmix_t *)port->post;

  port->stream = NULL;
 
  this->vo_port->close(this->vo_port, NULL);
  this->metronom->set_master(this->metronom, NULL);
 
  port->original_port->close(port->original_port, stream );
  
  _x_post_dec_usage(port);
}

static int upmix_frames_2to51_any_to_float(uint8_t *dst8, uint8_t *src8, int num_frames, int step_channel_in, af_sub_t *sub) {
  float *dst=(float *)dst8;
  int16_t *src16=(int16_t *)src8;
  float *src_float=(float *)src8;
  int src_num_channels=2;
  int dst_num_channels=6;
  int src_frame;
  int dst_frame;
  int32_t sample24;
  float sample;
  float left;
  float right;
  float sum;
  int frame;
  int src_units_per_sample=1; 
  if (step_channel_in == 3) src_units_per_sample=step_channel_in; /* Special handling for 24 bit 3byte input */
  
  for (frame=0;frame < num_frames; frame++) {
    dst_frame=frame*dst_num_channels;
    src_frame=frame*src_num_channels*src_units_per_sample;
    switch (step_channel_in) {
    case 1:
      left = src8[src_frame];
      left = (left - 128 ) / 128; /* FIXME: Need to verify this is correct */
      right = src8[src_frame+1];
      right = (right - 128) / 128;
      break;
    case 2:
      left = (1.0/SHRT_MAX)*((float)src16[src_frame]);
      right = (1.0/SHRT_MAX)*((float)src16[src_frame+1]);
      break;
    case 3:
#ifdef WORDS_BIGENDIAN
      sample24 = (src8[src_frame] << 24) | (src8[src_frame+1] << 16) | ( src8[src_frame+2] << 8);
#else
      sample24 = (src8[src_frame] << 8) | (src8[src_frame+1] << 16) | ( src8[src_frame+2] << 24);
#endif
      left = (1.0/INT32_MAX)*((float)sample24);
#ifdef WORDS_BIGENDIAN
      sample24 = (src8[src_frame+3] << 24) | (src8[src_frame+4] << 16) | ( src8[src_frame+5] << 8);
#else
      sample24 = (src8[src_frame+3] << 8) | (src8[src_frame+4] << 16) | ( src8[src_frame+5] << 24);
#endif
      right = (1.0/INT32_MAX)*((float)sample24);
      break;
    case 4:
      left = src_float[src_frame];
      right = src_float[src_frame+1];
      break;
    default:
      left = right = 0.0;
    }

    dst[dst_frame] = left;
    dst[dst_frame+1] = right;
    /* try a bit of dolby */
    /* FIXME: Dobly surround is a bit more complicated than this, but this is a start. */
    dst[dst_frame+2] = (left - right) / 2;
    dst[dst_frame+3] = (left - right) / 2;
    sum = (left + right) / 2;
    dst[dst_frame+4] = sum;
    /* Create the LFE channel using a low pass filter */
    /* filter feature ported from mplayer */
    sample = sum;
    IIR(sample * sub->k, sub->w[0], sub->q[0], sample);
    IIR(sample , sub->w[1], sub->q[1], sample);
    dst[dst_frame+5] = sample;

  }
  return frame;
}

static void upmix_port_put_buffer (xine_audio_port_t *port_gen, 
                             audio_buffer_t *buf, xine_stream_t *stream) {
  
  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_upmix_t *this = (post_plugin_upmix_t *)port->post;
  int src_step_frame;
  int dst_step_frame;
  int step_channel_in;
  int step_channel_out;
  uint8_t *data8src;
  uint8_t *data8dst;
  int num_bytes;
  int num_frames;
  int num_frames_done;
  int num_frames_processed=0;
 
  if ((this->channels==2) && (this->channels_out==6)) {
    while (num_frames_processed < buf->num_frames) {
      this->buf = port->original_port->get_buffer(port->original_port); 
      /* this->buf->num_frames is handled after the upmix */
      this->buf->vpts = buf->vpts;
      if (num_frames_processed != 0) this->buf->vpts = 0;
      this->buf->frame_header_count = buf->frame_header_count;
      this->buf->first_access_unit = buf->first_access_unit;
      /* FIXME: The audio buffer should contain this info.
       *        We should not have to get it from the open call.
       */
      this->buf->format.bits = 32; /* Upmix to floats */
      this->buf->format.rate = port->rate;
      this->buf->format.mode = AO_CAP_MODE_5_1CHANNEL;
      _x_extra_info_merge( this->buf->extra_info, buf->extra_info); 
      step_channel_in = port->bits>>3;
      step_channel_out = this->buf->format.bits>>3;
      dst_step_frame = this->channels_out*step_channel_out;
      src_step_frame = this->channels*step_channel_in;
      num_bytes=(buf->num_frames-num_frames_processed)*dst_step_frame;
      if (num_bytes > this->buf->mem_size) {
        num_bytes = this->buf->mem_size;
      }
      num_frames = num_bytes/dst_step_frame;
      data8src=(int8_t*)buf->mem;
      data8src+=num_frames_processed*src_step_frame;
      data8dst=(int8_t*)this->buf->mem;
      num_frames_done = upmix_frames_2to51_any_to_float(data8dst, data8src, num_frames, step_channel_in, this->sub);
      this->buf->num_frames = num_frames_done;
      num_frames_processed+= num_frames_done;
      /* pass data to original port */
      port->original_port->put_buffer(port->original_port, this->buf, stream );  
    }
    /* free data from origial buffer */
    buf->num_frames=0; /* UNDOCUMENTED, but hey, it works! Force old audio_out buffer free. */
  }
  port->original_port->put_buffer(port->original_port, buf, stream );  
  
  return;
}

static void upmix_dispose(post_plugin_t *this_gen)
{
  post_plugin_upmix_t *this = (post_plugin_upmix_t *)this_gen;

  if (_x_post_dispose(this_gen)) {
    this->metronom->exit(this->metronom);
    if (this->sub) free(this->sub);
    free(this);
  }
}

/* plugin class functions */
static post_plugin_t *upmix_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_class_upmix_t  *class = (post_class_upmix_t *)class_gen;
  post_plugin_upmix_t *this  = (post_plugin_upmix_t *)xine_xmalloc(sizeof(post_plugin_upmix_t));
  post_in_t            *input;
  post_out_t           *output;
  post_out_t           *outputv;
  post_audio_port_t    *port;
  
  if (!this || !video_target || !video_target[0] || !audio_target || !audio_target[0] ) {
    free(this);
    return NULL;
  }
  
  _x_post_init(&this->post, 1, 0);
  
  this->metronom = _x_metronom_init(1, 0, class->xine);

  this->vo_port = video_target[0];

  port = _x_post_intercept_audio_port(&this->post, audio_target[0], &input, &output);
  port->new_port.open       = upmix_port_open;
  port->new_port.close      = upmix_port_close;
  port->new_port.put_buffer = upmix_port_put_buffer;
  
  outputv                  = &this->video_output;
  outputv->xine_out.name   = "generated video";
  outputv->xine_out.type   = XINE_POST_DATA_VIDEO;
  outputv->xine_out.data   = (xine_video_port_t **)&this->vo_port;
  outputv->xine_out.rewire = upmix_rewire_video;
  outputv->post            = &this->post;
  xine_list_append_content(this->post.output, outputv);
  
  this->post.xine_post.audio_input[0] = &port->new_port;
  
  this->post.dispose = upmix_dispose;

  return &this->post;
}

static char *upmix_get_identifier(post_class_t *class_gen)
{
  return "upmix";
}

static char *upmix_get_description(post_class_t *class_gen)
{
  return "upmix";
}

static void upmix_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}

/* plugin class initialization function */
static void *upmix_init_plugin(xine_t *xine, void *data)
{
  post_class_upmix_t *class = (post_class_upmix_t *)malloc(sizeof(post_class_upmix_t));
  
  if (!class)
    return NULL;
  
  class->post_class.open_plugin     = upmix_open_plugin;
  class->post_class.get_identifier  = upmix_get_identifier;
  class->post_class.get_description = upmix_get_description;
  class->post_class.dispose         = upmix_class_dispose;
  
  class->xine                       = xine;
  
  return class;
}

/* plugin catalog information */
/* post_info_t upmix_special_info = { XINE_POST_TYPE_AUDIO_VISUALIZATION }; */
post_info_t upmix_special_info = { XINE_POST_TYPE_AUDIO_FILTER };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 9, "upmix", XINE_VERSION_CODE, &upmix_special_info, &upmix_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
