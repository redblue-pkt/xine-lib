/*
 * Copyright (C) 2003 the xine project
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
 * $Id: 
 *
 * expand video filter by James Stembridge 24/05/2003
 *            improved by Michael Roitzsch
 * 
 * based on invert.c
 *
 */

#include "xine_internal.h"
#include "xineutils.h"
#include "post.h"

/* The expand trick explained:
 *
 * The expand plugin is meant to take frames of arbitrary aspect ratio and
 * converts them to 4:3 aspect by adding black bars on the top and bottom
 * of the frame. This allows us to shift overlays down into the black area
 * so they don't cover the image.
 *
 * How do we do that? The naive approach would be to intercept the frame's
 * draw() function and simply copy the frame's content into a larger one.
 * This is quite CPU intensive because of the huge memcpy()s involved.
 *
 * Therefore the better idea is to trick the decoder into rendering the
 * image into a frame with pre-attached black borders. This is the way:
 *  - when the decoder asks for a new frame, we allocate an enlarged
 *    frame from the original port and prepare it with black borders
 *  - we clone the frame by copying its vo_frame_t structure
 *  - we modify this structure so that the decoder will only see
 *    the area between the black bars
 *  - this frame is given to the decoder, which paints its image inside
 *  - when the decoder draws the frame, we intercept that and draw
 *    the enlarged version instead
 *  - same with freeing the frame
 * This way, the decoder (or any other post plugin down the tree) will only
 * see the frame area between the black bars and by that modify the
 * enlarged version directly. No need for later copying.
 */ 


/* plugin class initialization function */
void *expand_init_plugin(xine_t *xine, void *);

/* plugin structures */
typedef struct expand_parameters_s {
  int enable_automatic_shift;
  int overlay_y_offset;
} expand_parameters_t;

START_PARAM_DESCR(expand_parameters_t)
PARAM_ITEM(POST_PARAM_TYPE_BOOL, enable_automatic_shift, NULL, 0, 1, 0,
  "enable automatic overlay shifting")
PARAM_ITEM(POST_PARAM_TYPE_INT, overlay_y_offset, NULL, -500, 500, 0,
  "manually shift the overlay vertically")
END_PARAM_DESCR(expand_param_descr)

typedef struct post_expand_out_s {
  xine_post_out_t  xine_out;
  /* keep the stream for open/close when rewiring */
  xine_stream_t   *stream;
} post_expand_out_t;

typedef struct post_expand_s {
  post_plugin_t            post;
  
  post_overlay_manager_t  *overlay_manager;
  int                      enable_automatic_shift;
  int                      overlay_y_offset;
  int                      top_bar_height;
  
  vo_frame_t             **frames_prealloc;
  int                      num_frames;
} post_expand_t;

/* plugin class functions */
static post_plugin_t *expand_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *expand_get_identifier(post_class_t *class_gen);
static char          *expand_get_description(post_class_t *class_gen);
static void           expand_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           expand_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            expand_rewire(xine_post_out_t *output, void *data);

/* parameter functions */
static xine_post_api_descr_t *expand_get_param_descr(void);
static int            expand_set_parameters(xine_post_t *this_gen, void *param_gen);
static int            expand_get_parameters(xine_post_t *this_gen, void *param_gen);

/* replaced video_port functions */
static void           expand_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *expand_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, double ratio, 
				       int format, int flags);
static video_overlay_manager_t *expand_get_overlay_manager(xine_video_port_t *port_gen);
static void           expand_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* replaced vo_frame functions */
static int            expand_draw(vo_frame_t *frame, xine_stream_t *stream);
static void           expand_free(vo_frame_t *frame);
static void           expand_field(vo_frame_t *frame, int which_field);
static void           expand_lock(vo_frame_t *frame);

/* replaced overlay manager functions */
static int32_t        expand_overlay_add_event(video_overlay_manager_t *this_gen, void *event);


void *expand_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));
  
  if (!class)
    return NULL;
  
  class->open_plugin     = expand_open_plugin;
  class->get_identifier  = expand_get_identifier;
  class->get_description = expand_get_description;
  class->dispose         = expand_class_dispose;
  
  return class;
}


static post_plugin_t *expand_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_expand_t     *this        = (post_expand_t *)malloc(sizeof(post_expand_t));
  xine_post_in_t    *input       = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  xine_post_in_t    *input_param = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_expand_out_t *output      = (post_expand_out_t *)malloc(sizeof(post_expand_out_t));
  post_video_port_t *port;
  static xine_post_api_t post_api =
    { expand_set_parameters, expand_get_parameters, expand_get_param_descr };
  
  if (!this || !input || !input_param || !output || !video_target || !video_target[0]) {
    free(this);
    free(input);
    free(input_param);
    free(output);
    return NULL;
  }
  
  this->overlay_manager        = NULL;
  this->enable_automatic_shift = 0;
  this->overlay_y_offset       = 0;
  this->frames_prealloc        = NULL;
  this->num_frames             = 0;
  
  port = post_intercept_video_port(&this->post, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open                = expand_open;
  port->port.get_frame           = expand_get_frame;
  port->port.get_overlay_manager = expand_get_overlay_manager;
  port->port.close               = expand_close;
  
  input->name = "video";
  input->type = XINE_POST_DATA_VIDEO;
  input->data = (xine_video_port_t *)&port->port;
  
  input_param->name = "parameters";
  input_param->type = XINE_POST_DATA_PARAMETERS;
  input_param->data = &post_api;

  output->xine_out.name   = "expanded video";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = expand_rewire;
  output->stream          = NULL;
  
  this->post.xine_post.audio_input    = (xine_audio_port_t **)malloc(sizeof(xine_audio_port_t *));
  this->post.xine_post.audio_input[0] = NULL;
  this->post.xine_post.video_input    = (xine_video_port_t **)malloc(sizeof(xine_video_port_t *) * 2);
  this->post.xine_post.video_input[0] = &port->port;
  this->post.xine_post.video_input[1] = NULL;
  
  this->post.input  = xine_list_new();
  this->post.output = xine_list_new();
  
  xine_list_append_content(this->post.input, input);
  xine_list_append_content(this->post.input, input_param);
  xine_list_append_content(this->post.output, output);
  
  this->post.dispose = expand_dispose;
  
  return &this->post;
}

static char *expand_get_identifier(post_class_t *class_gen)
{
  return "expand";
}

static char *expand_get_description(post_class_t *class_gen)
{
  return "add black borders to top and bottom of video to expand it to 4:3 aspect ratio";
}

static void expand_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void expand_dispose(post_plugin_t *this_gen)
{
  post_expand_out_t *output = (post_expand_out_t *)xine_list_first_content(this_gen->output);
  xine_video_port_t *port = *(xine_video_port_t **)output->xine_out.data;
  post_expand_t     *this = (post_expand_t *)this_gen;
  int i;
  
  if (output->stream)
    port->close(port, output->stream);
  
  free(this->overlay_manager);
  
  for (i = 0; i < this->num_frames; i++)
    free(this->frames_prealloc[i]);
  free(this->frames_prealloc);

  free(this->post.xine_post.audio_input);
  free(this->post.xine_post.video_input);
  free(xine_list_first_content(this->post.input));
  free(xine_list_next_content(this->post.input));
  free(xine_list_first_content(this->post.output));
  xine_list_free(this->post.input);
  xine_list_free(this->post.output);
  free(this);
}


static int expand_rewire(xine_post_out_t *output_gen, void *data)
{
  post_expand_out_t *output = (post_expand_out_t *)output_gen;
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


static xine_post_api_descr_t *expand_get_param_descr(void)
{
  return &expand_param_descr;
}

static int expand_set_parameters(xine_post_t *this_gen, void *param_gen)
{
  post_expand_t *this = (post_expand_t *)this_gen;
  expand_parameters_t *param = (expand_parameters_t *)param_gen;
  
  this->enable_automatic_shift = param->enable_automatic_shift;
  this->overlay_y_offset       = param->overlay_y_offset;
  return 1;
}

static int expand_get_parameters(xine_post_t *this_gen, void *param_gen)
{
  post_expand_t *this = (post_expand_t *)this_gen;
  expand_parameters_t *param = (expand_parameters_t *)param_gen;
  
  param->enable_automatic_shift = this->enable_automatic_shift;
  param->overlay_y_offset       = this->overlay_y_offset;
  return 1;
}


static void expand_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_expand_out_t *output = (post_expand_out_t *)xine_list_first_content(port->post->output);
  output->stream = stream;
  port->original_port->open(port->original_port, stream);
}

static vo_frame_t *expand_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, double ratio, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_expand_t     *this = (post_expand_t *)port->post;
  vo_frame_t        *original_frame, *cloned_frame = NULL;
  uint32_t           new_height, top_bar_height;
  int                i, end;
  
  if (ratio <= 0.0) ratio = (double)width / (double)height;
  
  /* Calculate height of expanded frame */
  new_height = (double)height * ratio * 3.0 / 4.0;
  new_height = (new_height + 1) & ~1;
  top_bar_height = (new_height - height) / 2;
  top_bar_height = (top_bar_height + 1) & ~1;
  
  ((post_expand_t *)port->post)->top_bar_height = top_bar_height;

  if (new_height > height &&
      (format == XINE_IMGFMT_YV12 || format == XINE_IMGFMT_YUY2)) {
    original_frame = port->original_port->get_frame(port->original_port,
      width, new_height, 4.0 / 3.0, format, flags);
    
    /* search for a free frame among the preallocated */
    while (!cloned_frame) {
      for (i = 0; i < this->num_frames; i++)
	/* misusing is_first as a free flag */
	if (this->frames_prealloc[i]->is_first) break;
      if (i >= this->num_frames) {
	/* no free frame found -> we need to allocate some more */
	this->frames_prealloc =
	  realloc(this->frames_prealloc, sizeof(vo_frame_t *) * (this->num_frames + 15));
	for (i = this->num_frames; i < this->num_frames + 15; i++) {
	  this->frames_prealloc[i] = (vo_frame_t *)malloc(sizeof(vo_frame_t));
	  this->frames_prealloc[i]->is_first = 1;
	}
	this->num_frames += 15;
      } else
        cloned_frame = this->frames_prealloc[i];
    }
    /* misusing is_first as a free flag */
    cloned_frame->is_first = 0;
    
    xine_fast_memcpy(cloned_frame, original_frame, sizeof(vo_frame_t));
    /* replace with our own functions */
    cloned_frame->draw = expand_draw;
    cloned_frame->free = expand_free;
    /* decoders should not copy the frames, since they won't be displayed as is */
    /* FIXME: We might get speed improvements with copy-capable outputs by:
     *  - copying the top black bar here
     *  - letting the decoder copy the image
     *  - copying the bottom black bar in expand_draw()
     */
    cloned_frame->copy  = NULL;
    cloned_frame->field = expand_field;
    cloned_frame->lock  = expand_lock;
    
    cloned_frame->port  = port_gen;
    /* misuse the next pointer to remember the original */
    cloned_frame->next  = original_frame;
    
    /* paint black bars in the top and bottom of the frame and hide these
     * from the decoders by modifying the pointers to and
     * the size of the drawing area */
    cloned_frame->height = height;
    cloned_frame->ratio  = ratio;
    switch (format) {
    case XINE_IMGFMT_YV12:
      /* paint top bar */
      memset(cloned_frame->base[0],   0, cloned_frame->pitches[0] * top_bar_height    );
      memset(cloned_frame->base[1], 128, cloned_frame->pitches[1] * top_bar_height / 2);
      memset(cloned_frame->base[2], 128, cloned_frame->pitches[2] * top_bar_height / 2);
      /* paint bottom bar */
      memset(cloned_frame->base[0] + cloned_frame->pitches[0] * (top_bar_height + height)    ,   0,
        cloned_frame->pitches[0] * (new_height - top_bar_height - height)    );
      memset(cloned_frame->base[1] + cloned_frame->pitches[1] * (top_bar_height + height) / 2, 128,
        cloned_frame->pitches[1] * (new_height - top_bar_height - height) / 2);
      memset(cloned_frame->base[2] + cloned_frame->pitches[2] * (top_bar_height + height) / 2, 128,
        cloned_frame->pitches[2] * (new_height - top_bar_height - height) / 2);
      /* modify drawing area */
      cloned_frame->base[0] += cloned_frame->pitches[0] * top_bar_height;
      cloned_frame->base[1] += cloned_frame->pitches[1] * top_bar_height / 2;
      cloned_frame->base[2] += cloned_frame->pitches[2] * top_bar_height / 2;
      break;
    case XINE_IMGFMT_YUY2:
      /* paint top bar */
      end = cloned_frame->pitches[0] * top_bar_height;
      for (i = 0; i < end; i += 2) {
	cloned_frame->base[0][i]   = 0;
	cloned_frame->base[0][i+1] = 128;
      }
      /* paint bottom bar */
      end = cloned_frame->pitches[0] * new_height;
      for (i = cloned_frame->pitches[0] * (top_bar_height + height); i < end; i += 2) {
	cloned_frame->base[0][i]   = 0;
	cloned_frame->base[0][i+1] = 128;
      }
      /* modify drawing area */
      cloned_frame->base[0] += cloned_frame->pitches[0] * top_bar_height;
    }
  } else {
    cloned_frame = port->original_port->get_frame(port->original_port,
      width, height, ratio, format, flags);
    /* no need to intercept this one, we are not going to do anything with it */
  }
  
  return cloned_frame;
}

static video_overlay_manager_t *expand_get_overlay_manager(xine_video_port_t *port_gen)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_expand_t     *this = (post_expand_t *)port->post;
  
  if (!this->overlay_manager) {
    /* create a new overlay manager to intercept */
    this->overlay_manager = post_intercept_overlay_manager(&this->post,
      port->original_port->get_overlay_manager(port->original_port));
    /* replace with our own add_event function */
    this->overlay_manager->manager.add_event = expand_overlay_add_event;
  } else {
    /* the original port might have changed */
    this->overlay_manager->original_manager =
      port->original_port->get_overlay_manager(port->original_port);
  }
  
  return &this->overlay_manager->manager;
}

static void expand_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_expand_out_t *output = (post_expand_out_t *)xine_list_first_content(port->post->output);
  output->stream = NULL;
  port->original_port->close(port->original_port, stream);
}


static int expand_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  vo_frame_t        *original_frame = frame->next;
  int                skip;

  original_frame->pts       = frame->pts;
  original_frame->duration  = frame->duration;
  original_frame->bad_frame = frame->bad_frame;
  extra_info_merge(original_frame->extra_info, frame->extra_info);
  skip = original_frame->draw(original_frame, stream);
  frame->vpts = original_frame->vpts;
  return skip;
}

static void expand_free(vo_frame_t *frame)
{
  vo_frame_t        *original_frame = frame->next;

  original_frame->free(original_frame);
  if (--frame->lock_counter == 0)
    /* misusing is_first as a free flag */
    frame->is_first = 1;
}

static void expand_field(vo_frame_t *frame, int which_field)
{
}

static void expand_lock(vo_frame_t *frame)
{
  vo_frame_t        *original_frame = frame->next;
  
  original_frame->lock(original_frame);
  frame->lock_counter++;
}


static int32_t expand_overlay_add_event(video_overlay_manager_t *this_gen, void *event_gen)
{
  post_overlay_manager_t   *ovl_manager = (post_overlay_manager_t *)this_gen;
  video_overlay_event_t    *event = (video_overlay_event_t *)event_gen;
  post_expand_t            *this = (post_expand_t *)ovl_manager->post;
  
  if (event->event_type == OVERLAY_EVENT_SHOW) {
    switch (event->object.object_type) {
    case 0:
      /* regular subtitle */
      if (this->enable_automatic_shift)
	event->object.overlay->y += 2 * this->top_bar_height;
      else
	event->object.overlay->y += this->overlay_y_offset;
      break;
    case 1:
      /* menu overlay */
      event->object.overlay->y += this->top_bar_height;
    }
  }
  
  return ovl_manager->original_manager->add_event(ovl_manager->original_manager, event_gen);
}
