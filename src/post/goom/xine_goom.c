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
 * $Id: xine_goom.c,v 1.24 2003/01/29 17:21:14 miguelfreitas Exp $
 *
 * GOOM post plugin.
 *
 * first version by Mark Thomas
 * ported to post plugin architecture by Miguel Freitas
 * real work by goom author, JC Hoelt <jeko@free.fr>.
 */

#include <stdio.h>

#include "config.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "post.h"

#include "goom_config.h"
#include "goom_core.h"

/*
#define LOG
*/
#define NUMSAMPLES  512
#define FPS          10

#define GOOM_WIDTH  320
#define GOOM_HEIGHT 240

/* colorspace conversion methods */
const char * goom_csc_methods[]={
  "Fast but not photorealistic",
  "Slow but looks better (mmx)",
  NULL
};

typedef struct post_plugin_goom_s post_plugin_goom_t;

typedef struct post_class_goom_s post_class_goom_t;

struct post_class_goom_s {
  post_class_t class;

  post_plugin_goom_t *ip;
  xine_t             *xine;
};

struct post_plugin_goom_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  xine_stream_t     *stream;
  
  post_class_goom_t *class;  
  
  int data_idx;
  gint16 data [2][512];
  audio_buffer_t buf;   /* dummy buffer just to hold a copy of audio data */
  
  int bits;
  int mode;
  int channels;
  int sample_rate;
  int sample_counter;
  int samples_per_frame;
  int width, height;
  int width_back, height_back;
  int fps;
  int use_asm;
  int csc_method;

  yuv_planes_t yuv;  
};

typedef struct post_goom_out_s post_goom_out_t;
struct post_goom_out_s {
  xine_post_out_t     out;
  post_plugin_goom_t *post;
};

/* plugin class initialization function */
static void *goom_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
post_info_t goom_special_info = { 
  XINE_POST_TYPE_AUDIO_VISUALIZATION
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST | PLUGIN_MUST_PRELOAD, 2, "goom", XINE_VERSION_CODE, &goom_special_info, &goom_init_plugin },
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
static int            goom_rewire_audio(xine_post_out_t *output, void *data);
static int            goom_rewire_video(xine_post_out_t *output, void *data);

static int goom_port_open(xine_audio_port_t *this, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode);

static void goom_port_close(xine_audio_port_t *this, xine_stream_t *stream );

static void goom_port_put_buffer (xine_audio_port_t *this, audio_buffer_t *buf, xine_stream_t *stream);

static void fps_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_goom_t *class = (post_class_goom_t*) data;
  
  if(class->ip) {
    post_plugin_goom_t *this = class->ip;
    this->fps = cfg->num_value;
    this->samples_per_frame = this->sample_rate / this->fps;
  }
}

static void width_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_goom_t *class = (post_class_goom_t*) data;
  
  if(class->ip) {
    post_plugin_goom_t *this = class->ip;
    this->width = cfg->num_value;
  }
}

static void height_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_goom_t *class = (post_class_goom_t*) data;
  
  if(class->ip) {
    post_plugin_goom_t *this = class->ip;
    this->height = cfg->num_value;
  }
}

static void use_asm_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_goom_t *class = (post_class_goom_t*) data;
  
  if(class->ip) {
    post_plugin_goom_t *this = class->ip;
    this->use_asm = cfg->num_value;
    goom_setAsmUse(this->use_asm);
  }
}

static void csc_method_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_goom_t *class = (post_class_goom_t*) data;
  
  if(class->ip) {
    post_plugin_goom_t *this = class->ip;
    this->csc_method = cfg->num_value;
  }
}

static void *goom_init_plugin(xine_t *xine, void *data)
{
  post_class_goom_t *this = (post_class_goom_t *)malloc(sizeof(post_class_goom_t));
  config_values_t *cfg;

  if (!this)
    return NULL;
  
  this->class.open_plugin     = goom_open_plugin;
  this->class.get_identifier  = goom_get_identifier;
  this->class.get_description = goom_get_description;
  this->class.dispose         = goom_class_dispose;
  this->ip                    = NULL;
  this->xine                  = xine;
  
  cfg = xine->config;

  cfg->register_num (cfg, "post.goom_fps", FPS,
                                 _("Frames per second to generate with Goom"),
                                 NULL, 10, fps_changed_cb, this);

  cfg->register_num (cfg, "post.goom_width", GOOM_WIDTH,
                                   _("Goom image width in pixels"),
                                   NULL, 20, width_changed_cb, this);
  
  cfg->register_num (cfg, "post.goom_height", GOOM_HEIGHT,
                                    _("Goom image height in pixels"),
                                    NULL, 20, height_changed_cb, this);
  

#ifdef ARCH_X86
  if (xine_mm_accel() & MM_ACCEL_X86_MMX) {
    cfg->register_bool (cfg, "post.goom_use_asm", 1,
                             _("Use Goom asm optimizations"),
                             NULL, 10, use_asm_changed_cb, this);
  }
#endif
  
#ifdef ARCH_PPC
  cfg->register_bool (cfg, "post.goom_use_asm", 1,
                           _("Use Goom asm optimizations"),
                           NULL, 10, use_asm_changed_cb, this);
#endif
  
  cfg->register_enum (cfg, "post.goom_csc_method", 0,
                           (char **)goom_csc_methods,
                           _("Colorspace conversion method used by Goom"),
                           NULL, 20, csc_method_changed_cb, this);

  return &this->class;
}


static post_plugin_t *goom_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_goom_t *this   = (post_plugin_goom_t *)malloc(sizeof(post_plugin_goom_t));
  post_class_goom_t  *class  = (post_class_goom_t*) class_gen;
  xine_post_in_t     *input  = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_goom_out_t    *output = (post_goom_out_t *)malloc(sizeof(post_goom_out_t));
  post_goom_out_t    *outputv = (post_goom_out_t *)malloc(sizeof(post_goom_out_t));
  post_audio_port_t  *port;
  xine_cfg_entry_t    fps_entry, width_entry, height_entry, use_asm_entry, csc_method_entry;

  if (!this || !input || !output || !outputv || !video_target || !video_target[0] ||
      !audio_target || !audio_target[0] ) {
    free(this);
    free(input);
    free(output);
    free(outputv);
    return NULL;
  }
  
  /*
   * Lookup config entries.
   */
  class->ip = this;
#ifdef LOG
  printf("goom: goom_open_plugin\n");
#endif

  if(xine_config_lookup_entry(class->xine, "post.goom_fps",
                              &fps_entry)) 
    fps_changed_cb(class, &fps_entry);

  if(xine_config_lookup_entry(class->xine, "post.goom_width",
                              &width_entry)) 
    width_changed_cb(class, &width_entry);

  if(xine_config_lookup_entry(class->xine, "post.goom_height",
                              &height_entry)) 
    height_changed_cb(class, &height_entry);

  if(xine_config_lookup_entry(class->xine, "post.goom_use_asm",
                              &use_asm_entry)) 
    use_asm_changed_cb(class, &use_asm_entry);

  if(xine_config_lookup_entry(class->xine, "post.goom_csc_method",
                              &csc_method_entry)) 
    csc_method_changed_cb(class, &csc_method_entry);

  this->width_back  = this->width;
  this->height_back = this->height;
  goom_init (this->width_back, this->height_back, 0);

  this->sample_counter = 0;
  this->stream  = NULL;
  this->vo_port = video_target[0];
  this->buf.mem = NULL;
  this->buf.mem_size = 0;  

  port = post_intercept_audio_port(&this->post, audio_target[0]);
  port->port.open = goom_port_open;
  port->port.close = goom_port_close;
  port->port.put_buffer = goom_port_put_buffer;
  
  input->name = "audio in";
  input->type = XINE_POST_DATA_AUDIO;
  input->data = (xine_audio_port_t *)&port->port;

  output->out.name   = "audio out";
  output->out.type   = XINE_POST_DATA_AUDIO;
  output->out.data   = (xine_audio_port_t **)&port->original_port;
  output->out.rewire = goom_rewire_audio;
  output->post       = this;
  
  outputv->out.name   = "generated video";
  outputv->out.type   = XINE_POST_DATA_VIDEO;
  outputv->out.data   = (xine_video_port_t **)&this->vo_port;
  outputv->out.rewire = goom_rewire_video;
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


static void goom_dispose(post_plugin_t *this_gen)
{
  post_plugin_goom_t *this = (post_plugin_goom_t *)this_gen;
  xine_post_out_t *output = (xine_post_out_t *)xine_list_first_content(this_gen->output);
  xine_video_port_t *port = *(xine_video_port_t **)output->data;

  goom_close();

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


static int goom_rewire_audio(xine_post_out_t *output_gen, void *data)
{
  post_goom_out_t *output = (post_goom_out_t *)output_gen;
  xine_audio_port_t *old_port = *(xine_audio_port_t **)output_gen->data;
  xine_audio_port_t *new_port = (xine_audio_port_t *)data;
  post_plugin_goom_t *this = (post_plugin_goom_t *)output->post;
  
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

static int goom_rewire_video(xine_post_out_t *output_gen, void *data)
{
  post_goom_out_t *output = (post_goom_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  post_plugin_goom_t *this = (post_plugin_goom_t *)output->post;
  
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

static int goom_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
		   uint32_t bits, uint32_t rate, int mode) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_goom_t *this = (post_plugin_goom_t *)port->post;

  this->bits = bits;
  this->mode = mode;
  this->channels = mode_channels(mode);
  this->samples_per_frame = rate / this->fps;
  this->sample_rate = rate; 
  this->stream = stream;
  this->data_idx = 0;
  init_yuv_planes(&this->yuv, this->width, this->height);

  return port->original_port->open(port->original_port, stream, bits, rate, mode );
}

static void goom_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream ) {

  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_goom_t *this = (post_plugin_goom_t *)port->post;

  free_yuv_planes(&this->yuv);

  this->stream = NULL;
 
  port->original_port->close(port->original_port, stream );
}

static void goom_port_put_buffer (xine_audio_port_t *port_gen, 
                             audio_buffer_t *buf, xine_stream_t *stream) {
  
  post_audio_port_t  *port = (post_audio_port_t *)port_gen;
  post_plugin_goom_t *this = (post_plugin_goom_t *)port->post;
  vo_frame_t         *frame;
  /* uint32_t *goom_frame; */
  uint8_t *goom_frame, *goom_frame_end;
  int16_t *data;
  int8_t *data8;
  int samples_used = 0;
  uint64_t vpts = buf->vpts;
  int i, j;
  uint8_t *dest_ptr;
  int width, height;
  
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

      goom_frame = (uint8_t *)goom_update (this->data, 0, 0, NULL, NULL);

      frame = this->vo_port->get_frame (this->vo_port, this->width_back, this->height_back,
                                        XINE_VO_ASPECT_SQUARE, XINE_IMGFMT_YUY2,
                                        VO_BOTH_FIELDS);
      
      frame->pts = vpts;
      vpts = 0;
      frame->duration = 90000 * this->samples_per_frame / this->sample_rate;
      this->sample_counter -= this->samples_per_frame;
  
      /* Try to be fast */
      dest_ptr = frame -> base[0];
      goom_frame_end = goom_frame + 4 * (this->width_back * this->height_back);

      if ((this->csc_method == 1) && 
          (xine_mm_accel() & MM_ACCEL_X86_MMX)) {
        int plane_ptr = 0;

        while (goom_frame < goom_frame_end) {
          uint8_t r, g, b;
      
          /* don't take endianness into account since MMX is only available
           * on Intel processors */
          b = *goom_frame; goom_frame++;
          g = *goom_frame; goom_frame++;
          r = *goom_frame; goom_frame += 2;

          this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
          this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
          this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
          plane_ptr++;
        }

        yuv444_to_yuy2(&this->yuv, frame->base[0], frame->pitches[0]);

      } else {

        while (goom_frame < goom_frame_end) {
          uint8_t r1, g1, b1, r2, g2, b2;
      
#ifdef __BIG_ENDIAN__
          goom_frame ++;
          r1 = *goom_frame; goom_frame++;
          g1 = *goom_frame; goom_frame++;
          b1 = *goom_frame; goom_frame += 2;
          r2 = *goom_frame; goom_frame++;
          g2 = *goom_frame; goom_frame++;
          b2 = *goom_frame; goom_frame++;
#else
          b1 = *goom_frame; goom_frame++;
          g1 = *goom_frame; goom_frame++;
          r1 = *goom_frame; goom_frame += 2;
          b2 = *goom_frame; goom_frame++;
          g2 = *goom_frame; goom_frame++;
          r2 = *goom_frame; goom_frame += 2;
#endif
      
          *dest_ptr = COMPUTE_Y(r1, g1, b1);
          dest_ptr++;
          *dest_ptr = COMPUTE_U(r1, g1, b1);
          dest_ptr++;
          *dest_ptr = COMPUTE_Y(r2, g2, b2);
          dest_ptr++;
          *dest_ptr = COMPUTE_V(r2, g2, b2);
          dest_ptr++;
        }
      }

      frame->draw(frame, stream);
      frame->free(frame);
      
      width  = this->width;
      height = this->height;
      if ((width != this->width_back) || (height != this->height_back)) {
        goom_close();
        goom_init (this->width, this->height, 0);
        this->width_back = width;
        this->height_back = height;
      }
      
    }
  } while( this->sample_counter >= this->samples_per_frame );
}
