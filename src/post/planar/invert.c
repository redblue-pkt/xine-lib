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
 * $Id: invert.c,v 1.5 2002/12/25 15:05:06 mroi Exp $
 */
 
/*
 * simple video inverter plugin
 */

#include "xine_internal.h"
#include "post.h"


/* plugin class initialization function */
static void *invert_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 1, "invert", XINE_VERSION_CODE, NULL, &invert_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


/* plugin structure */
typedef struct post_invert_out_s post_invert_out_t;
struct post_invert_out_s {
  xine_post_out_t  xine_out;
  /* keep the stream for open/close when rewiring */
  xine_stream_t   *stream;
};

/* plugin class functions */
static post_plugin_t *invert_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *invert_get_identifier(post_class_t *class_gen);
static char          *invert_get_description(post_class_t *class_gen);
static void           invert_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           invert_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            invert_rewire(xine_post_out_t *output, void *data);

/* replaced video_port functions */
static void           invert_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *invert_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, int ratio_code, 
				       int format, int flags);
static void           invert_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* replaced vo_frame functions */
static int            invert_draw(vo_frame_t *frame, xine_stream_t *stream);


static void *invert_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));
  
  if (!class)
    return NULL;
  
  class->open_plugin     = invert_open_plugin;
  class->get_identifier  = invert_get_identifier;
  class->get_description = invert_get_description;
  class->dispose         = invert_class_dispose;
  
  return class;
}


static post_plugin_t *invert_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_t     *this   = (post_plugin_t *)malloc(sizeof(post_plugin_t));
  xine_post_in_t    *input  = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_invert_out_t *output = (post_invert_out_t *)malloc(sizeof(post_invert_out_t));
  post_video_port_t *port;
  
  if (!this || !input || !output || !video_target || !video_target[0]) {
    free(this);
    free(input);
    free(output);
    return NULL;
  }
  
  port = post_intercept_video_port(this, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open      = invert_open;
  port->port.get_frame = invert_get_frame;
  port->port.close     = invert_close;
  
  input->name = "video";
  input->type = XINE_POST_DATA_VIDEO;
  input->data = (xine_video_port_t *)&port->port;

  output->xine_out.name   = "inverted video";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = invert_rewire;
  output->stream          = NULL;
  
  this->xine_post.audio_input    = (xine_audio_port_t **)malloc(sizeof(xine_audio_port_t *));
  this->xine_post.audio_input[0] = NULL;
  this->xine_post.video_input    = (xine_video_port_t **)malloc(sizeof(xine_video_port_t *) * 2);
  this->xine_post.video_input[0] = &port->port;
  this->xine_post.video_input[1] = NULL;
  
  this->input  = xine_list_new();
  this->output = xine_list_new();
  
  xine_list_append_content(this->input, input);
  xine_list_append_content(this->output, output);
  
  this->dispose = invert_dispose;
  
  return this;
}

static char *invert_get_identifier(post_class_t *class_gen)
{
  return "invert";
}

static char *invert_get_description(post_class_t *class_gen)
{
  return "inverts the colours of every video frame";
}

static void invert_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void invert_dispose(post_plugin_t *this)
{
  free(this->xine_post.audio_input);
  free(this->xine_post.video_input);
  free(xine_list_first_content(this->input));
  free(xine_list_first_content(this->output));
  xine_list_free(this->input);
  xine_list_free(this->output);
  free(this);
}


static int invert_rewire(xine_post_out_t *output_gen, void *data)
{
  post_invert_out_t *output = (post_invert_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  xine_video_port_t *new_port = (xine_video_port_t *)data;
  
  if (!data)
    return 0;
  if (output->stream) {
    /* register our stream at the new output port */
    old_port->close(old_port, output->stream);
    new_port->open(new_port, output->stream);
  }
  /* reconnect ourselves */
  *(xine_video_port_t **)output_gen->data = new_port;
  return 1;
}


static void invert_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_invert_out_t *output = (post_invert_out_t *)xine_list_first_content(port->post->output);
  output->stream = stream;
  port->original_port->open(port->original_port, stream);
}

static vo_frame_t *invert_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, int ratio_code, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;

  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio_code, format, flags);
  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = invert_draw;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->copy = NULL;
  return frame;
}

static void invert_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_invert_out_t *output = (post_invert_out_t *)xine_list_first_content(port->post->output);
  output->stream = NULL;
  port->original_port->close(port->original_port, stream);
}


static int invert_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  vo_frame_t *inverted_frame;
  int size, i, skip;
  
  inverted_frame = port->original_port->get_frame(port->original_port,
    frame->width, frame->height, frame->ratio, frame->format, VO_BOTH_FIELDS);
  inverted_frame->pts = frame->pts;
  inverted_frame->duration = frame->duration;
  inverted_frame->bad_frame = frame->bad_frame;
  
  switch (inverted_frame->format) {
  case XINE_IMGFMT_YUY2:
    size = inverted_frame->pitches[0] * inverted_frame->height;
    for (i = 0; i < size; i++)
      inverted_frame->base[0][i] = 0xff - frame->base[0][i];
    break;
  case XINE_IMGFMT_YV12:
    /* Y */
    size = inverted_frame->pitches[0] * inverted_frame->height;
    for (i = 0; i < size; i++)
      inverted_frame->base[0][i] = 0xff - frame->base[0][i];
    /* U */
    size = inverted_frame->pitches[1] * ((inverted_frame->height + 1) / 2);
    for (i = 0; i < size; i++)
      inverted_frame->base[1][i] = 0xff - frame->base[1][i];
    /* V */
    size = inverted_frame->pitches[2] * ((inverted_frame->height + 1) / 2);
    for (i = 0; i < size; i++)
      inverted_frame->base[2][i] = 0xff - frame->base[2][i];
    break;
  default:
    printf("invert: cannot handle image format %d\n", frame->format);
    inverted_frame->free(inverted_frame);
    post_restore_video_frame(frame, port);
    return frame->draw(frame, stream);
  } 
  skip = inverted_frame->draw(inverted_frame, stream);
  inverted_frame->free(inverted_frame);
  frame->vpts = inverted_frame->vpts;
  post_restore_video_frame(frame, port);
  
  return skip;
}
