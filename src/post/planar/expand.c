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
 * 
 * based on invert.c
 *
 */

#include "xine_internal.h"
#include "xineutils.h"
#include "post.h"


/* plugin class initialization function */
static void *expand_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
post_info_t expand_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 2, "expand", XINE_VERSION_CODE, &expand_special_info, &expand_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


/* plugin structure */
typedef struct post_expand_out_s post_expand_out_t;
struct post_expand_out_s {
  xine_post_out_t  xine_out;
  /* keep the stream for open/close when rewiring */
  xine_stream_t   *stream;
};

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

/* replaced video_port functions */
static void           expand_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *expand_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, int ratio_code, 
				       int format, int flags);
static void           expand_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* replaced vo_frame functions */
static int            expand_draw(vo_frame_t *frame, xine_stream_t *stream);


static void *expand_init_plugin(xine_t *xine, void *data)
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
  post_plugin_t     *this   = (post_plugin_t *)malloc(sizeof(post_plugin_t));
  xine_post_in_t    *input  = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_expand_out_t *output = (post_expand_out_t *)malloc(sizeof(post_expand_out_t));
  post_video_port_t *port;
  
  if (!this || !input || !output || !video_target || !video_target[0]) {
    free(this);
    free(input);
    free(output);
    return NULL;
  }
  
  port = post_intercept_video_port(this, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open      = expand_open;
  port->port.get_frame = expand_get_frame;
  port->port.close     = expand_close;
  
  input->name = "video";
  input->type = XINE_POST_DATA_VIDEO;
  input->data = (xine_video_port_t *)&port->port;

  output->xine_out.name   = "expanded video";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = expand_rewire;
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
  
  this->dispose = expand_dispose;
  
  return this;
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


static void expand_dispose(post_plugin_t *this)
{
  post_expand_out_t *output = (post_expand_out_t *)xine_list_first_content(this->output);
  xine_video_port_t *port = *(xine_video_port_t **)output->xine_out.data;
  
  if (output->stream)
    port->close(port, output->stream);

  free(this->xine_post.audio_input);
  free(this->xine_post.video_input);
  free(xine_list_first_content(this->input));
  free(xine_list_first_content(this->output));
  xine_list_free(this->input);
  xine_list_free(this->output);
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


static void expand_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_expand_out_t *output = (post_expand_out_t *)xine_list_first_content(port->post->output);
  output->stream = stream;
  port->original_port->open(port->original_port, stream);
}

static vo_frame_t *expand_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, int ratio_code, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;

  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio_code, format, flags);
  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = expand_draw;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->copy = NULL;
  return frame;
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
  post_video_port_t *port = (post_video_port_t *) frame->port;
  vo_frame_t *expanded_frame;
  int size, i, skip, new_height, border_height;
  double pixel_aspect;
  
  /* Find pixel aspect of video frame */
  switch(frame->ratio) {
    case XINE_VO_ASPECT_ANAMORPHIC:  /* anamorphic     */
    case XINE_VO_ASPECT_PAN_SCAN:    /* we display pan&scan as widescreen */
      pixel_aspect = 16.0 /9.0;
      break;
    case XINE_VO_ASPECT_DVB:         /* 2.11:1 */
      pixel_aspect = 2.11/1.0;
      break;
    case XINE_VO_ASPECT_SQUARE:      /* square pels */
    case XINE_VO_ASPECT_DONT_TOUCH:  /* don't touch aspect ratio */
      pixel_aspect = 1.0;
      break;
    case 0:                          /* forbidden -> 4:3 */
      printf("expand: invalid ratio, using 4:3\n");
    default:
      printf("expand: unknown aspect ratio (%d) in stream => using 4:3\n",
             frame->ratio);
    case XINE_VO_ASPECT_4_3:         /* 4:3 */
      pixel_aspect = 4.0 / 3.0;
      break;
  }
  
  /* Calculate height of expanded frame */
  new_height = (double) frame->width * pixel_aspect * 3.0 / 4.0;
  new_height = (new_height + 1) & ~1;
  
  if(new_height > frame->height) {
    expanded_frame = port->original_port->get_frame(port->original_port,
      frame->width, new_height, frame->ratio, frame->format, VO_BOTH_FIELDS);
    expanded_frame->pts = frame->pts;
    expanded_frame->duration = frame->duration;
    expanded_frame->bad_frame = frame->bad_frame;
    extra_info_merge(expanded_frame->extra_info, frame->extra_info);
    
    switch(frame->format) {
      case XINE_IMGFMT_YV12:
        /* Check pitches match */
        if((expanded_frame->pitches[0] != frame->pitches[0]) ||
           (expanded_frame->pitches[1] != frame->pitches[1]) ||
           (expanded_frame->pitches[2] != frame->pitches[2])) {
          printf("expand: pitches do not match, can't expand\n");
          expanded_frame->free(expanded_frame);
          goto do_nothing;
        }
              
        /* Make frame black */
        memset(expanded_frame->base[0], 0, expanded_frame->pitches[0] * new_height);
        memset(expanded_frame->base[1], 128, expanded_frame->pitches[1] * new_height / 2);
        memset(expanded_frame->base[2], 128, expanded_frame->pitches[2] * new_height / 2);

        border_height = (new_height - frame->height) / 2;
        border_height = (border_height + 1) & ~1;

        /* Copy video */
        xine_fast_memcpy(expanded_frame->base[0] + border_height * frame->pitches[0],
                         frame->base[0], frame->pitches[0] * frame->height);
        xine_fast_memcpy(expanded_frame->base[1] + border_height * frame->pitches[1] / 2,
                         frame->base[1], frame->pitches[1] * frame->height / 2);
        xine_fast_memcpy(expanded_frame->base[2] + border_height * frame->pitches[2] / 2,
                         frame->base[2], frame->pitches[2] * frame->height / 2);
        break;
      case XINE_IMGFMT_YUY2:
        /* Check pitches match */
        if(expanded_frame->pitches[0] != frame->pitches[0]) {
          printf("expand: pitches do not match, can't expand\n");
          expanded_frame->free(expanded_frame);
          goto do_nothing;
        }
        
        /* Make frame black - this is sloooow */
        size = expanded_frame->pitches[0] * new_height;
        for (i = 0; i < size; i += 2) {
          expanded_frame->base[0][i] = 0;
          expanded_frame->base[0][i+1] = 128;
        }
        
        border_height = (new_height - frame->height) / 2;
        
        /* Copy video */
        xine_fast_memcpy(expanded_frame->base[0] + border_height * frame->pitches[0],
                         frame->base[0], frame->pitches[0] * frame->height);
        break;
      default:
        printf("expand: unknown frame format format %d\n", frame->format);
        expanded_frame->free(expanded_frame);
        goto do_nothing;      
    }
    
    skip = expanded_frame->draw(expanded_frame, stream);
    expanded_frame->free(expanded_frame);
    frame->vpts = expanded_frame->vpts;
    post_restore_video_frame(frame, port);
  } else {
do_nothing:
    post_restore_video_frame(frame, port);
    skip =  frame->draw(frame, stream);
  }
  
  return skip;
}
