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
 * $Id: post.c,v 1.16 2003/10/22 20:38:10 komadori Exp $
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
    uint32_t height, double ratio, int format, int flags) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  return port->original_port->get_frame(port->original_port,
    width, height, ratio, format, flags);
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

static video_overlay_manager_t *post_video_get_overlay_manager(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  return port->original_port->get_overlay_manager(port->original_port);
}

static void post_video_flush(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  port->original_port->flush(port->original_port);
}

static int post_video_status(xine_video_port_t *port_gen, xine_stream_t *stream,
                             int *width, int *height, int64_t *img_duration) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  return port->original_port->status(port->original_port, stream, width, height, img_duration);
}

static int post_video_get_property(xine_video_port_t *port_gen, int property) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  return port->original_port->get_property(port->original_port, property);
}

static int post_video_set_property(xine_video_port_t *port_gen, int property, int value) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  return port->original_port->set_property(port->original_port, property, value);
}

post_video_port_t *post_intercept_video_port(post_plugin_t *post, xine_video_port_t *original) {
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
  post_port->port.get_overlay_manager    = post_video_get_overlay_manager;
  post_port->port.flush                  = post_video_flush;
  post_port->port.status                 = post_video_status;
  post_port->port.get_property           = post_video_get_property;
  post_port->port.set_property           = post_video_set_property;
  post_port->port.driver                 = original->driver;
  
  post_port->original_port               = original;
  post_port->post                        = post;
  
  return post_port;
}


/* dummy intercept functions for frames */
static void post_frame_free(vo_frame_t *vo_img) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  post_restore_video_frame(vo_img, port);
  vo_img->free(vo_img);
}
  
static void post_frame_proc_slice(vo_frame_t *vo_img, uint8_t **src) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  vo_img->port = port->original_port;
  port->original_frame.proc_slice(vo_img, src);
  vo_img->port = &port->port;
}

static void post_frame_proc_frame(vo_frame_t *vo_img, uint8_t **src) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  vo_img->port = port->original_port;
  port->original_frame.proc_frame(vo_img, src);
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

static void post_frame_lock(vo_frame_t *vo_img) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  post_restore_video_frame(vo_img, port);
  vo_img->lock(vo_img);
}

static void post_frame_dispose(vo_frame_t *vo_img) {
  post_video_port_t *port = (post_video_port_t *)vo_img->port;
  post_restore_video_frame(vo_img, port);
  vo_img->dispose(vo_img);
}

static void post_frame_proc_macro_block(int x,
			   int y,
			   int mb_type,
			   int motion_type,
			   int (*mv_field_sel)[2],
			   int *dmvector,
			   int cbp,
			   int dct_type,
			   vo_frame_t *current_frame,
			   vo_frame_t *forward_ref_frame,
			   vo_frame_t *backward_ref_frame,
			   int picture_structure,
			   int second_field,
			   int (*f_mot_pmv)[2],
			   int (*b_mot_pmv)[2]) {
  post_video_port_t *port = (post_video_port_t *)current_frame->port;
  post_restore_video_frame(current_frame, port);
  post_restore_video_frame(forward_ref_frame, port);
  post_restore_video_frame(backward_ref_frame, port);
  current_frame->proc_macro_block(x, y, mb_type, motion_type, mv_field_sel,
                                  dmvector, cbp, dct_type, current_frame,
                                  forward_ref_frame, backward_ref_frame,
                                  picture_structure, second_field, 
                                  f_mot_pmv, b_mot_pmv);
}



void post_intercept_video_frame(vo_frame_t *frame, post_video_port_t *port) {
  port->original_frame.port       = frame->port;
  port->original_frame.free       = frame->free;
  port->original_frame.proc_slice = frame->proc_slice;
  port->original_frame.proc_frame = frame->proc_frame;
  port->original_frame.field      = frame->field;
  port->original_frame.draw       = frame->draw;
  port->original_frame.lock       = frame->lock;
  port->original_frame.dispose    = frame->dispose;
  port->original_frame.proc_macro_block = frame->proc_macro_block;
  
  frame->port                     = &port->port;
  frame->free                     = post_frame_free;
  frame->proc_slice               = frame->proc_slice ? post_frame_proc_slice : NULL;
  frame->proc_frame               = frame->proc_frame ? post_frame_proc_frame : NULL;
  frame->field                    = post_frame_field;
  frame->draw                     = post_frame_draw;
  frame->lock                     = post_frame_lock;
  frame->dispose                  = post_frame_dispose;
  frame->proc_macro_block         = post_frame_proc_macro_block;
}

void post_restore_video_frame(vo_frame_t *frame, post_video_port_t *port) {
  frame->port                     = port->original_port;
  frame->free                     = port->original_frame.free;
  frame->proc_slice               = port->original_frame.proc_slice;
  frame->proc_frame               = port->original_frame.proc_frame;
  frame->field                    = port->original_frame.field;
  frame->draw                     = port->original_frame.draw;
  frame->lock                     = port->original_frame.lock;
  frame->dispose                  = port->original_frame.dispose;
  frame->proc_macro_block         = port->original_frame.proc_macro_block;
}


/* dummy intercept functions that just pass the call on to the original overlay manager */
static void post_overlay_init(video_overlay_manager_t *ovl_gen) {
  post_overlay_manager_t *ovl = (post_overlay_manager_t *)ovl_gen;
  ovl->original_manager->init(ovl->original_manager);
}

static void post_overlay_dispose(video_overlay_manager_t *ovl_gen) {
  post_overlay_manager_t *ovl = (post_overlay_manager_t *)ovl_gen;
  ovl->original_manager->dispose(ovl->original_manager);
}

static int32_t post_overlay_get_handle(video_overlay_manager_t *ovl_gen, int object_type) {
  post_overlay_manager_t *ovl = (post_overlay_manager_t *)ovl_gen;
  return ovl->original_manager->get_handle(ovl->original_manager, object_type);
}

static void post_overlay_free_handle(video_overlay_manager_t *ovl_gen, int32_t handle) {
  post_overlay_manager_t *ovl = (post_overlay_manager_t *)ovl_gen;
  ovl->original_manager->free_handle(ovl->original_manager, handle);
}

static int32_t post_overlay_add_event(video_overlay_manager_t *ovl_gen, void *event) {
  post_overlay_manager_t *ovl = (post_overlay_manager_t *)ovl_gen;
  return ovl->original_manager->add_event(ovl->original_manager, event);
}

static void post_overlay_flush_events(video_overlay_manager_t *ovl_gen) {
  post_overlay_manager_t *ovl = (post_overlay_manager_t *)ovl_gen;
  ovl->original_manager->flush_events(ovl->original_manager);
}

static int post_overlay_redraw_needed(video_overlay_manager_t *ovl_gen, int64_t vpts) {
  post_overlay_manager_t *ovl = (post_overlay_manager_t *)ovl_gen;
  return ovl->original_manager->redraw_needed(ovl->original_manager, vpts);
}

static void post_overlay_multiple_overlay_blend(video_overlay_manager_t *ovl_gen, int64_t vpts, 
	      vo_driver_t *output, vo_frame_t *vo_img, int enabled) {
  post_overlay_manager_t *ovl = (post_overlay_manager_t *)ovl_gen;
  ovl->original_manager->multiple_overlay_blend(ovl->original_manager, vpts, output, vo_img, enabled);
}


post_overlay_manager_t *post_intercept_overlay_manager(post_plugin_t *post,
			  video_overlay_manager_t *original) {
  post_overlay_manager_t *post_ovl = (post_overlay_manager_t *)malloc(sizeof(post_overlay_manager_t));
  
  if (!post_ovl)
    return NULL;
  
  post_ovl->manager.init                   = post_overlay_init;
  post_ovl->manager.dispose                = post_overlay_dispose;
  post_ovl->manager.get_handle             = post_overlay_get_handle;
  post_ovl->manager.free_handle            = post_overlay_free_handle;
  post_ovl->manager.add_event              = post_overlay_add_event;
  post_ovl->manager.flush_events           = post_overlay_flush_events;
  post_ovl->manager.redraw_needed          = post_overlay_redraw_needed;
  post_ovl->manager.multiple_overlay_blend = post_overlay_multiple_overlay_blend;
  
  post_ovl->original_manager               = original;
  post_ovl->post                           = post;
  
  return post_ovl;
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
  port->original_port->put_buffer(port->original_port, buf, stream);
}
                                    
static void post_audio_close(xine_audio_port_t *port_gen, xine_stream_t *stream) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  port->original_port->close(port->original_port, stream);
}

static void post_audio_exit(xine_audio_port_t *port_gen) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  port->original_port->exit(port->original_port);
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
  port->original_port->flush(port->original_port);
}

static int post_audio_status(xine_audio_port_t *port_gen, xine_stream_t *stream,
	       uint32_t *bits, uint32_t *rate, int *mode) {
  post_audio_port_t *port = (post_audio_port_t *)port_gen;
  return port->original_port->status(port->original_port, stream, bits, rate, mode);
}


post_audio_port_t *post_intercept_audio_port(post_plugin_t *post, xine_audio_port_t *original) {
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
  post_port->port.status                 = post_audio_status;
    
  post_port->original_port               = original;
  post_port->post                        = post;
  
  return post_port;
}
