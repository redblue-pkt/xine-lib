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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: boxblur.c,v 1.5 2003/08/12 13:56:26 mroi Exp $
 *
 * mplayer's boxblur
 * Copyright (C) 2002 Michael Niedermayer <michaelni@gmx.at>
 */

#include "xine_internal.h"
#include "post.h"
#include "xineutils.h"
#include <pthread.h>

/* plugin class initialization function */
void *boxblur_init_plugin(xine_t *xine, void *);

#if 0 /* moved to planar.c */
/* plugin catalog information */
post_info_t boxblur_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 4, "boxblur", XINE_VERSION_CODE, &boxblur_special_info, &boxblur_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif

typedef struct post_plugin_boxblur_s post_plugin_boxblur_t;

/*
 * this is the struct used by "parameters api" 
 */
typedef struct boxblur_parameters_s {

  int luma_radius;
  int luma_power;
  int chroma_radius;
  int chroma_power;

} boxblur_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( boxblur_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, luma_radius, NULL, 0, 10, 0, 
            "radius of luma blur" )
PARAM_ITEM( POST_PARAM_TYPE_INT, luma_power, NULL, 0, 10, 0, 
            "power of luma blur" )
PARAM_ITEM( POST_PARAM_TYPE_INT, chroma_radius, NULL, -1, 10, 0, 
            "radius of chroma blur (-1 = same as luma)" )
PARAM_ITEM( POST_PARAM_TYPE_INT, chroma_power, NULL, -1, 10, 0, 
            "power of chroma blur (-1 = same as luma)" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_boxblur_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  xine_stream_t     *stream;

  boxblur_parameters_t params;

  pthread_mutex_t    lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)this_gen;
  boxblur_parameters_t *param = (boxblur_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);

  memcpy( &this->params, param, sizeof(boxblur_parameters_t) );

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)this_gen;
  boxblur_parameters_t *param = (boxblur_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(boxblur_parameters_t) );

  return 1;
}
 
static xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
};

typedef struct post_boxblur_out_s post_boxblur_out_t;
struct post_boxblur_out_s {
  xine_post_out_t  xine_out;

  post_plugin_boxblur_t *plugin;
};

/* plugin class functions */
static post_plugin_t *boxblur_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *boxblur_get_identifier(post_class_t *class_gen);
static char          *boxblur_get_description(post_class_t *class_gen);
static void           boxblur_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           boxblur_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            boxblur_rewire(xine_post_out_t *output, void *data);

/* replaced video_port functions */
static void           boxblur_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *boxblur_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, double ratio, 
				       int format, int flags);
static void           boxblur_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* replaced vo_frame functions */
static int            boxblur_draw(vo_frame_t *frame, xine_stream_t *stream);


void *boxblur_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));

  if (!class)
    return NULL;
  
  class->open_plugin     = boxblur_open_plugin;
  class->get_identifier  = boxblur_get_identifier;
  class->get_description = boxblur_get_description;
  class->dispose         = boxblur_class_dispose;

  return class;
}


static post_plugin_t *boxblur_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)malloc(sizeof(post_plugin_boxblur_t));
  xine_post_in_t            *input = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  xine_post_in_t            *input_api = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_boxblur_out_t    *output = (post_boxblur_out_t *)malloc(sizeof(post_boxblur_out_t));
  post_video_port_t *port;
  
  if (!this || !input || !output || !video_target || !video_target[0]) {
    free(this);
    free(input);
    free(output);
    return NULL;
  }

  this->stream = NULL;

  this->params.luma_radius = 2;
  this->params.luma_power = 1;
  this->params.chroma_radius = -1;
  this->params.chroma_power = -1;

  pthread_mutex_init (&this->lock, NULL);
  
  port = post_intercept_video_port(&this->post, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open         = boxblur_open;
  port->port.get_frame    = boxblur_get_frame;
  port->port.close        = boxblur_close;
  
  input->name = "video";
  input->type = XINE_POST_DATA_VIDEO;
  input->data = (xine_video_port_t *)&port->port;

  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;

  output->xine_out.name   = "boxblured video";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = boxblur_rewire;
  output->plugin          = this;
  
  this->post.xine_post.audio_input    = (xine_audio_port_t **)malloc(sizeof(xine_audio_port_t *));
  this->post.xine_post.audio_input[0] = NULL;
  this->post.xine_post.video_input    = (xine_video_port_t **)malloc(sizeof(xine_video_port_t *) * 2);
  this->post.xine_post.video_input[0] = &port->port;
  this->post.xine_post.video_input[1] = NULL;
  
  this->post.input  = xine_list_new();
  this->post.output = xine_list_new();
  
  xine_list_append_content(this->post.input, input);
  xine_list_append_content(this->post.input, input_api);
  xine_list_append_content(this->post.output, output);
  
  this->post.dispose = boxblur_dispose;
  
  return &this->post;
}

static char *boxblur_get_identifier(post_class_t *class_gen)
{
  return "boxblur";
}

static char *boxblur_get_description(post_class_t *class_gen)
{
  return "box blur filter from mplayer";
}

static void boxblur_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void boxblur_dispose(post_plugin_t *this_gen)
{
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)this_gen;
  post_boxblur_out_t *output = (post_boxblur_out_t *)xine_list_first_content(this->post.output);
  xine_video_port_t *port = *(xine_video_port_t **)output->xine_out.data;

  if (this->stream)
    port->close(port, this->stream);

  free(this->post.xine_post.audio_input);
  free(this->post.xine_post.video_input);
  free(xine_list_first_content(this->post.input));
  free(xine_list_first_content(this->post.output));
  xine_list_free(this->post.input);
  xine_list_free(this->post.output);
  free(this);
}


static int boxblur_rewire(xine_post_out_t *output_gen, void *data)
{
  post_boxblur_out_t *output = (post_boxblur_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  
  if (!data)
    return 0;

  if (output->plugin->stream) {
    /* register our stream at the new output port */
    old_port->close(old_port, output->plugin->stream);
    new_port->open(new_port, output->plugin->stream);
  }
  /* reconnect ourselves */
  *(xine_video_port_t **)output_gen->data = new_port;

  return 1;
}

static void boxblur_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)port->post;
  this->stream = stream;
  port->original_port->open(port->original_port, stream);
}

static vo_frame_t *boxblur_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, double ratio, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;

  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio, format, flags);

  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = boxblur_draw;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->copy = NULL;

  return frame;
}

static void boxblur_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)port->post;

  this->stream = NULL;
  
  port->original_port->close(port->original_port, stream);
}


static inline void blur(uint8_t *dst, uint8_t *src, int w, int radius, int dstStep, int srcStep){
	int x;
	const int length= radius*2 + 1;
	const int inv= ((1<<16) + length/2)/length;

	int sum= 0;

	for(x=0; x<radius; x++){
		sum+= src[x*srcStep]<<1;
	}
	sum+= src[radius*srcStep];

	for(x=0; x<=radius; x++){
		sum+= src[(radius+x)*srcStep] - src[(radius-x)*srcStep];
		dst[x*dstStep]= (sum*inv + (1<<15))>>16;
	}

	for(; x<w-radius; x++){
		sum+= src[(radius+x)*srcStep] - src[(x-radius-1)*srcStep];
		dst[x*dstStep]= (sum*inv + (1<<15))>>16;
	}

	for(; x<w; x++){
		sum+= src[(2*w-radius-x-1)*srcStep] - src[(x-radius-1)*srcStep];
		dst[x*dstStep]= (sum*inv + (1<<15))>>16;
	}
}

static inline void blur2(uint8_t *dst, uint8_t *src, int w, int radius, int power, int dstStep, int srcStep){
	uint8_t temp[2][4096];
	uint8_t *a= temp[0], *b=temp[1];
	
	if(radius){
		blur(a, src, w, radius, 1, srcStep);
		for(; power>2; power--){
			uint8_t *c;
			blur(b, a, w, radius, 1, 1);
			c=a; a=b; b=c;
		}
		if(power>1)
			blur(dst, a, w, radius, dstStep, 1);
		else{
			int i;
			for(i=0; i<w; i++)
				dst[i*dstStep]= a[i];
		}
	}else{
		int i;
		for(i=0; i<w; i++)
			dst[i*dstStep]= src[i*srcStep];
	}
}

static void hBlur(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride, int radius, int power){
	int y;
	
	if(radius==0 && dst==src) return;
	
	for(y=0; y<h; y++){
		blur2(dst + y*dstStride, src + y*srcStride, w, radius, power, 1, 1);
	}
}

static void vBlur(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride, int radius, int power){
	int x;
	
	if(radius==0 && dst==src) return;

	for(x=0; x<w; x++){
		blur2(dst + x, src + x, h, radius, power, dstStride, srcStride);
	}
}


static int boxblur_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_boxblur_t *this = (post_plugin_boxblur_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *yv12_frame;
  int chroma_radius, chroma_power;
  int cw, ch;
  int skip;

  post_restore_video_frame(frame, port);

  if( !frame->bad_frame ) {


    /* convert to YV12 if needed */
    if( frame->format != XINE_IMGFMT_YV12 ) {

      yv12_frame = port->original_port->get_frame(port->original_port,
        frame->width, frame->height, frame->ratio, XINE_IMGFMT_YV12, frame->flags | VO_BOTH_FIELDS);
  
      yv12_frame->pts = frame->pts;
      yv12_frame->duration = frame->duration;
      extra_info_merge(yv12_frame->extra_info, frame->extra_info);
  
      yuy2_to_yv12(frame->base[0], frame->pitches[0],
                   yv12_frame->base[0], yv12_frame->pitches[0],
                   yv12_frame->base[1], yv12_frame->pitches[1],
                   yv12_frame->base[2], yv12_frame->pitches[2],
                   frame->width, frame->height);

    } else {
      yv12_frame = frame;
      yv12_frame->lock(yv12_frame);
    }


    out_frame = port->original_port->get_frame(port->original_port,
      frame->width, frame->height, frame->ratio, XINE_IMGFMT_YV12, frame->flags | VO_BOTH_FIELDS);

  
    extra_info_merge(out_frame->extra_info, frame->extra_info);
  
    out_frame->pts = frame->pts;
    out_frame->duration = frame->duration;

    pthread_mutex_lock (&this->lock);

    chroma_radius = (this->params.chroma_radius != -1) ? this->params.chroma_radius : 
                                                         this->params.luma_radius;
    chroma_power = (this->params.chroma_power != -1) ? this->params.chroma_power : 
                                                       this->params.luma_power;
    cw = yv12_frame->width/2;
    ch = yv12_frame->height/2;

    hBlur(out_frame->base[0], yv12_frame->base[0], yv12_frame->width, yv12_frame->height, 
          out_frame->pitches[0], yv12_frame->pitches[0], this->params.luma_radius, this->params.luma_power);
    hBlur(out_frame->base[1], yv12_frame->base[1], cw,ch, 
          out_frame->pitches[1], yv12_frame->pitches[1], chroma_radius, chroma_power);
    hBlur(out_frame->base[2], yv12_frame->base[2], cw,ch, 
          out_frame->pitches[2], yv12_frame->pitches[2], chroma_radius, chroma_power);

    vBlur(out_frame->base[0], out_frame->base[0], yv12_frame->width, yv12_frame->height, 
          out_frame->pitches[0], out_frame->pitches[0], this->params.luma_radius, this->params.luma_power);
    vBlur(out_frame->base[1], out_frame->base[1], cw,ch, 
          out_frame->pitches[1], out_frame->pitches[1], chroma_radius, chroma_power);
    vBlur(out_frame->base[2], out_frame->base[2], cw,ch, 
          out_frame->pitches[2], out_frame->pitches[2], chroma_radius, chroma_power);

    pthread_mutex_unlock (&this->lock);

    skip = out_frame->draw(out_frame, stream);
  
    frame->vpts = out_frame->vpts;

    out_frame->free(out_frame);
    yv12_frame->free(yv12_frame);

  } else {
    skip = frame->draw(frame, stream);
  }

  
  return skip;
}
