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
 * $Id: post.c,v 1.3 2002/12/24 13:36:21 miguelfreitas Exp $
 */
 
/*
 * some helper functions for post plugins
 */

#include "post.h"
#include <stdarg.h>

/* dummy intercept functions that just pass the call on to the original port */
static uint32_t post_video_get_capabilities(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  return port->original_port->get_capabilities(port->original_port);
}

static void post_video_open(xine_video_port_t *port_gen, xine_stream_t *stream) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  port->original_port->open(port->original_port, stream);
}

static vo_frame_t *post_video_get_frame(xine_video_port_t *port_gen, uint32_t width, 
    uint32_t height, int ratio_code, int format, int flags) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  return port->original_port->get_frame(port->original_port,
    width, height, ratio_code, format, flags);
}

static vo_frame_t *post_video_get_last_frame(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  return port->original_port->get_last_frame(port->original_port);
}
  
static void post_video_enable_ovl(xine_video_port_t *port_gen, int ovl_enable) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  port->original_port->enable_ovl(port->original_port, ovl_enable);
}
  
static void post_video_close(xine_video_port_t *port_gen, xine_stream_t *stream) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  port->original_port->close(port->original_port, stream);
}

static void post_video_exit(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  port->original_port->exit(port->original_port);
}

static video_overlay_instance_t *post_video_get_overlay_instance(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  return port->original_port->get_overlay_instance(port->original_port);
}

static void post_video_flush(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  port->original_port->flush(port->original_port);
}


post_video_port_t *post_intercept_video_port(xine_video_port_t *original) {
  post_video_port_t *post_port = (post_video_port_t *)malloc(sizeof(post_video_port_t));
  
  if (!post_port)
    return NULL;
  
  post_port->port.get_capabilities       = post_video_get_capabilities;
  post_port->port.open                   = post_video_open;
  post_port->port.get_frame              = post_video_get_frame;
  post_port->port.get_last_frame         = post_video_get_last_frame;
  post_port->port.enable_ovl             = post_video_enable_ovl;
  post_port->port.close                  = post_video_close;
  post_port->port.exit                   = post_video_exit;
  post_port->port.get_overlay_instance   = post_video_get_overlay_instance;
  post_port->port.flush                  = post_video_flush;
  post_port->port.driver                 = original->driver;
  
  post_port->original_port               = original;
  
  return post_port;
}


/* dummy intercept functions for frames */
static void post_frame_free(vo_frame_t *vo_img) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  post_restore_video_frame(vo_img, port);
  vo_img->free(vo_img);
}
  
static void post_frame_copy(vo_frame_t *vo_img, uint8_t **src) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  vo_img->port = port->original_port;
  port->original_frame.copy(vo_img, src);
  vo_img->port = &port->port;
}

static void post_frame_field(vo_frame_t *vo_img, int which_field) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  vo_img->port = port->original_port;
  port->original_frame.field(vo_img, which_field);
  vo_img->port = &port->port;
}

static int post_frame_draw(vo_frame_t *vo_img, xine_stream_t *stream) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  post_restore_video_frame(vo_img, port);
  return vo_img->draw(vo_img, stream);
}

static void post_frame_displayed(vo_frame_t *vo_img) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  post_restore_video_frame(vo_img, port);
  vo_img->displayed(vo_img);
}

static void post_frame_dispose(vo_frame_t *vo_img) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  post_restore_video_frame(vo_img, port);
  vo_img->dispose(vo_img);
}


void post_intercept_video_frame(vo_frame_t *frame, post_video_port_t *port) {
  port->original_frame.port       = frame->port;
  port->original_frame.free       = frame->free;
  port->original_frame.copy       = frame->copy;
  port->original_frame.field      = frame->field;
  port->original_frame.draw       = frame->draw;
  port->original_frame.displayed  = frame->displayed;
  port->original_frame.dispose    = frame->dispose;
  
  frame->port                     = &port->port;
  frame->free                     = post_frame_free;
  frame->copy                     = frame->copy ? post_frame_copy : NULL; /* this one can be NULL */
  frame->field                    = post_frame_field;
  frame->draw                     = post_frame_draw;
  frame->displayed                = post_frame_displayed;
  frame->dispose                  = post_frame_dispose;
}

void post_restore_video_frame(vo_frame_t *frame, post_video_port_t *port) {
  frame->port                     = port->original_port;
  frame->free                     = port->original_frame.free;
  frame->copy                     = port->original_frame.copy;
  frame->field                    = port->original_frame.field;
  frame->draw                     = port->original_frame.draw;
  frame->displayed                = port->original_frame.displayed;
  frame->dispose                  = port->original_frame.dispose;
}

/* dummy intercept functions that just pass the call on to the original port */
static uint32_t post_audio_get_capabilities(xine_audio_port_t *port_gen) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->get_capabilities(port->original_port);
}

static int post_audio_get_property(xine_audio_port_t *port_gen, int property) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->get_property(port->original_port, property);
}

static int post_audio_set_property(xine_audio_port_t *port_gen, int property, int value) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->set_property(port->original_port, property, value);
}

static int post_audio_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
	       uint32_t bits, uint32_t rate, int mode) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->open(port->original_port, stream, bits, rate, mode);
}

static audio_buffer_t * post_audio_get_buffer(xine_audio_port_t *port_gen) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->get_buffer(port->original_port);
}

static void post_audio_put_buffer(xine_audio_port_t *port_gen, audio_buffer_t *buf,
                                  xine_stream_t *stream) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->put_buffer(port->original_port, buf, stream);
}
                                    
static void post_audio_close(xine_audio_port_t *port_gen, xine_stream_t *stream) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->close(port->original_port, stream);
}

static void post_audio_exit(xine_audio_port_t *port_gen) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->exit(port->original_port);
}

static int post_audio_control (xine_audio_port_t *port_gen, int cmd, ...) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  va_list args;
  void *arg;
  int rval;

  va_start(args, cmd);
  arg = va_arg(args, void*);
  rval = port->original_port->control(port->original_port, cmd, arg);
  va_end(args);

  return rval;
}

static void post_audio_flush(xine_audio_port_t *port_gen) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->flush(port->original_port);
}

post_audio_port_t *post_intercept_audio_port(xine_audio_port_t *original) {
  post_audio_port_t *post_port = (post_audio_port_t *)malloc(sizeof(post_audio_port_t));
  
  if (!post_port)
    return NULL;
  
  post_port->port.open                   = post_audio_open;
  post_port->port.get_buffer             = post_audio_get_buffer;
  post_port->port.put_buffer             = post_audio_put_buffer;
  post_port->port.close                  = post_audio_close;
  post_port->port.exit                   = post_audio_exit;
  post_port->port.get_capabilities       = post_audio_get_capabilities;
  post_port->port.get_property           = post_audio_get_property;
  post_port->port.set_property           = post_audio_set_property;
  post_port->port.control                = post_audio_control;
  post_port->port.flush                  = post_audio_flush;
  post_port->port.driver                 = original->driver;
  
    
  post_port->original_port               = original;
  
  return post_port;
}
