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
 * $Id: deinterlace.c,v 1.1 2003/05/28 04:28:43 miguelfreitas Exp $
 */
 
/*
 * simple video deinterlacer plugin
 */

#include "xine_internal.h"
#include "post.h"
#include "xineutils.h"
#include <pthread.h>


/* plugin class initialization function */
static void *deinterlace_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
post_info_t deinterlace_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 2, "deinterlace", XINE_VERSION_CODE, &deinterlace_special_info, &deinterlace_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


typedef struct post_plugin_deinterlace_s post_plugin_deinterlace_t;

void deinterlace_bob_mmx(post_plugin_deinterlace_t *this, vo_frame_t *frame);
void deinterlace_weave_mmx(post_plugin_deinterlace_t *this, vo_frame_t *frame);
void deinterlace_greedy_mmx(post_plugin_deinterlace_t *this, vo_frame_t *frame);
void deinterlace_onefield(post_plugin_deinterlace_t *this, vo_frame_t *frame);
void deinterlace_linearblend_mmx(post_plugin_deinterlace_t *this, vo_frame_t *frame);
void deinterlace_linearblend(post_plugin_deinterlace_t *this, vo_frame_t *frame);

static struct {
  char *name;
  void (*function)(post_plugin_deinterlace_t *this, vo_frame_t *frame);
  uint32_t cpu_require;
} deinterlace_method[] =
{
  { "by driver", NULL, 0 },
  { "bob MMX", deinterlace_bob_mmx, MM_MMX },
  { "weave MMX", deinterlace_weave_mmx, MM_MMX },
  { "greedy MMX", deinterlace_greedy_mmx, MM_MMX },
  { "linearblend MMX", deinterlace_linearblend_mmx, MM_MMX },
  { "linearblend", deinterlace_linearblend, 0 },
  { NULL, NULL, 0 }
};

static char *enum_methods[sizeof(deinterlace_method)/sizeof(deinterlace_method[0])+1];

/*
 * this is the struct used by "parameters api" 
 */
typedef struct deinterlace_parameters_s {

  int method;
  int enabled;

} deinterlace_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( deinterlace_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, method, enum_methods, 0, 0, 0, 
            "deinterlace method" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, enabled, NULL, 0, 1, 0,
            "enable/disable" )
END_PARAM_DESCR( param_descr )

/* plugin structure */
struct post_plugin_deinterlace_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  xine_stream_t     *stream;

  int                cur_method;
  void             (*cur_function)(post_plugin_deinterlace_t *this, vo_frame_t *frame);
  int                enabled;

  pthread_mutex_t    lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)this_gen;
  deinterlace_parameters_t *param = (deinterlace_parameters_t *)param_gen;
  char *name;
  int i;

  pthread_mutex_lock (&this->lock);
      
  this->cur_method = param->method;
  name = enum_methods[this->cur_method];

  for(i = 0; deinterlace_method[i].name; i++ ) {
    if( !strcmp(name, deinterlace_method[i].name) )
      this->cur_function = deinterlace_method[i].function;
  }

  this->enabled = param->enabled;
  pthread_mutex_unlock (&this->lock);

  return 1;
}

int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)this_gen;
  deinterlace_parameters_t *param = (deinterlace_parameters_t *)param_gen;
  
  param->method = this->cur_method;
  param->enabled = this->enabled;

  return 1;
}
 
xine_post_api_descr_t * get_param_descr (void) {
  return &param_descr;
}

static xine_post_api_t post_api = {
  set_parameters,
  get_parameters,
  get_param_descr,
};

typedef struct post_deinterlace_out_s post_deinterlace_out_t;
struct post_deinterlace_out_s {
  xine_post_out_t  xine_out;

  post_plugin_deinterlace_t *plugin;
};

/* plugin class functions */
static post_plugin_t *deinterlace_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *deinterlace_get_identifier(post_class_t *class_gen);
static char          *deinterlace_get_description(post_class_t *class_gen);
static void           deinterlace_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           deinterlace_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            deinterlace_rewire(xine_post_out_t *output, void *data);

/* replaced video_port functions */
static void           deinterlace_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *deinterlace_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, int ratio_code, 
				       int format, int flags);
static void           deinterlace_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* replaced vo_frame functions */
static int            deinterlace_draw(vo_frame_t *frame, xine_stream_t *stream);


static void *deinterlace_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));
  uint32_t config_flags = xine_mm_accel();
  int i, j;

  if (!class)
    return NULL;
  
  class->open_plugin     = deinterlace_open_plugin;
  class->get_identifier  = deinterlace_get_identifier;
  class->get_description = deinterlace_get_description;
  class->dispose         = deinterlace_class_dispose;

  for(i = 0, j = 0; deinterlace_method[i].name; i++ ) {
    if( (config_flags & deinterlace_method[i].cpu_require) ==
        deinterlace_method[i].cpu_require )
        enum_methods[j++] = deinterlace_method[i].name;
  }
  enum_methods[j] = NULL;

  return class;
}


static post_plugin_t *deinterlace_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)malloc(sizeof(post_plugin_deinterlace_t));
  xine_post_in_t            *input = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  xine_post_in_t            *input_api = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_deinterlace_out_t    *output = (post_deinterlace_out_t *)malloc(sizeof(post_deinterlace_out_t));
  post_video_port_t *port;
  
  if (!this || !input || !output || !video_target || !video_target[0]) {
    free(this);
    free(input);
    free(output);
    return NULL;
  }

  this->stream = NULL;
  this->cur_function = NULL;
  this->cur_method = 0;
  this->enabled = 0;
  pthread_mutex_init (&this->lock, NULL);
  
  port = post_intercept_video_port(&this->post, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open      = deinterlace_open;
  port->port.get_frame = deinterlace_get_frame;
  port->port.close     = deinterlace_close;
  
  input->name = "video";
  input->type = XINE_POST_DATA_VIDEO;
  input->data = (xine_video_port_t *)&port->port;

  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;

  output->xine_out.name   = "deinterlaced video";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = deinterlace_rewire;
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
  
  this->post.dispose = deinterlace_dispose;
  
  return &this->post;
}

static char *deinterlace_get_identifier(post_class_t *class_gen)
{
  return "deinterlace";
}

static char *deinterlace_get_description(post_class_t *class_gen)
{
  return "frame deinterlacer";
}

static void deinterlace_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void deinterlace_dispose(post_plugin_t *this_gen)
{
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)this_gen;
  post_deinterlace_out_t *output = (post_deinterlace_out_t *)xine_list_first_content(this->post.output);
  xine_video_port_t *port = *(xine_video_port_t **)output->xine_out.data;
  
  if (this->stream)
    port->close(port, this->stream);

  free(this->post.xine_post.audio_input);
  free(this->post.xine_post.video_input);
  free(xine_list_first_content(this->post.input));
  free(xine_list_first_content(this->post.input));
  free(xine_list_first_content(this->post.output));
  xine_list_free(this->post.input);
  xine_list_free(this->post.output);
  free(this);
}


static int deinterlace_rewire(xine_post_out_t *output_gen, void *data)
{
  post_deinterlace_out_t *output = (post_deinterlace_out_t *)output_gen;
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


static void deinterlace_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  this->stream = stream;
  port->original_port->open(port->original_port, stream);
}

static vo_frame_t *deinterlace_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, int ratio_code, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  vo_frame_t        *frame;

  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio_code, format, flags);

  pthread_mutex_lock (&this->lock);

  /* do not intercept if not enabled */
  if( this->enabled && this->cur_method ) {
    post_intercept_video_frame(frame, port);
    /* replace with our own draw function */
    frame->draw = deinterlace_draw;
    /* decoders should not copy the frames, since they won't be displayed */
    frame->copy = NULL;
  }

  pthread_mutex_unlock (&this->lock);

  return frame;
}

static void deinterlace_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  this->stream = NULL;
  port->original_port->close(port->original_port, stream);
}


static int deinterlace_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  vo_frame_t *deinterlaced_frame;
  int size, i, skip;
  
  /* deinterlacer not implemented yet: this is the "inverter" function */

  deinterlaced_frame = port->original_port->get_frame(port->original_port,
    frame->width, frame->height, frame->ratio, frame->format, VO_BOTH_FIELDS);
  deinterlaced_frame->pts = frame->pts;
  deinterlaced_frame->duration = frame->duration;
  deinterlaced_frame->bad_frame = frame->bad_frame;
  extra_info_merge(deinterlaced_frame->extra_info, frame->extra_info);
    
  switch (deinterlaced_frame->format) {
  case XINE_IMGFMT_YUY2:
    size = deinterlaced_frame->pitches[0] * deinterlaced_frame->height;
    for (i = 0; i < size; i++)
      deinterlaced_frame->base[0][i] = 0xff - frame->base[0][i];
    break;
  case XINE_IMGFMT_YV12:
    /* Y */
    size = deinterlaced_frame->pitches[0] * deinterlaced_frame->height;
    for (i = 0; i < size; i++)
      deinterlaced_frame->base[0][i] = 0xff - frame->base[0][i];
    /* U */
    size = deinterlaced_frame->pitches[1] * ((deinterlaced_frame->height + 1) / 2);
    for (i = 0; i < size; i++)
      deinterlaced_frame->base[1][i] = 0xff - frame->base[1][i];
    /* V */
    size = deinterlaced_frame->pitches[2] * ((deinterlaced_frame->height + 1) / 2);
    for (i = 0; i < size; i++)
      deinterlaced_frame->base[2][i] = 0xff - frame->base[2][i];
    break;
  default:
    printf("deinterlace: cannot handle image format %d\n", frame->format);
    deinterlaced_frame->free(deinterlaced_frame);
    post_restore_video_frame(frame, port);
    return frame->draw(frame, stream);
  } 
  skip = deinterlaced_frame->draw(deinterlaced_frame, stream);
  deinterlaced_frame->free(deinterlaced_frame);
  frame->vpts = deinterlaced_frame->vpts;
  post_restore_video_frame(frame, port);
  
  return skip;
}
