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
 * $Id: post.h,v 1.9 2003/05/31 18:33:31 miguelfreitas Exp $
 *
 * post plugin definitions
 *
 */

#ifndef XINE_POST_H
#define XINE_POST_H

#ifdef XINE_COMPILE
#  include "xine.h"
#  include "video_out.h"
#  include "audio_out.h"
#  include "xineutils.h"
#else
#  include <xine.h>
#  include <xine/video_out.h>
#  include <xine/audio_out.h>
#  include <xine/xineutils.h>
#endif

#define POST_PLUGIN_IFACE_VERSION 3


typedef struct post_class_s post_class_t;
typedef struct post_plugin_s post_plugin_t;


struct post_class_s {

  /*
   * open a new instance of this plugin class
   */
  post_plugin_t* (*open_plugin) (post_class_t *this, int inputs,
				 xine_audio_port_t **audio_target,
				 xine_video_port_t **video_target);
  
  /*
   * return short, human readable identifier for this plugin class
   */
  char* (*get_identifier) (post_class_t *this);

  /*
   * return human readable (verbose = 1 line) description for 
   * this plugin class
   */
  char* (*get_description) (post_class_t *this);

  /*
   * free all class-related resources
   */

  void (*dispose) (post_class_t *this);
};

struct post_plugin_s {

  /* public part of the plugin */
  xine_post_t         xine_post;
  
  /*
   * the connections announced by the plugin
   * the plugin must fill these with xine_post_{in,out}_t on init
   */
  xine_list_t        *input;
  xine_list_t        *output;
  
  /*
   * close down, free all resources
   */
  void (*dispose) (post_plugin_t *this);
  
  /* plugins don't have to care for the stuff below */
  
  /* used when the user requests a list of all inputs/outputs */
  const char        **input_ids;
  const char        **output_ids;

  /* used by plugin loader */
  void *node;
};


/* Post plugins work by intercepting calls to video or audio ports
 * in the sense of the decorator design pattern. They reuse the
 * functions of a given target port, but add own functionality in
 * front of that port by creating a new port structure and filling in
 * the function pointers with pointers to own functions that
 * would do something and then call the original port function.
 *
 * Much the same is done with video frames which have their own
 * set of functions attached that you might need to decorate.
 */

/* helper structure for intercepting video port calls */
typedef struct post_video_port_s post_video_port_t;
struct post_video_port_s {

  /* the new public port with replaced function pointers */
  xine_video_port_t  port;
  
  /* the original port to call its functions from inside yours */
  xine_video_port_t *original_port;
  
  /* here you can keep information about the frames */
  vo_frame_t         original_frame;
  
  /* backward reference so that you have access to the post plugin
   * when the call only gives you the port */
  post_plugin_t     *post;
};

/* use this to create a new, trivially decorated video port in which
 * port functions can be replaced with own implementations */
post_video_port_t *post_intercept_video_port(post_plugin_t *post, xine_video_port_t *port);

/* use this to decorate and to undecorate a frame so that its functions
 * can be replaced with own implementations */
void post_intercept_video_frame(vo_frame_t *frame, post_video_port_t *port);
void post_restore_video_frame(vo_frame_t *frame, post_video_port_t *port);

/* helper structure for intercepting audio port calls */
typedef struct post_audio_port_s post_audio_port_t;
struct post_audio_port_s {

  /* the new public port with replaced function pointers */
  xine_audio_port_t  port;
  
  /* the original port to call its functions from inside yours */
  xine_audio_port_t *original_port;
  
  /* backward reference so that you have access to the post plugin
   * when the call only gives you the port */
  post_plugin_t     *post;
};

/* use this to create a new, trivially decorated audio port in which
 * port functions can be replaced with own implementations */
post_audio_port_t *post_intercept_audio_port(post_plugin_t *post, xine_audio_port_t *port);


/* macros to create parameter descriptors */ 

#define START_PARAM_DESCR( param_t ) \
static param_t __temp_s; \
static xine_post_api_parameter_t __temp_p[] = {

#define PARAM_ITEM( param_type, var, enumv, min, max, readonly, descr ) \
{ param_type, #var, sizeof(__temp_s.var), \
  (char *)&__temp_s.var-(char *)&__temp_s, enumv, min, max, readonly, descr },

#define END_PARAM_DESCR( name ) \
  { POST_PARAM_TYPE_LAST, NULL, 0, 0, NULL, 0, 0, 1, NULL } \
}; \
static xine_post_api_descr_t name = { \
  sizeof( __temp_s ), \
  __temp_p \
};

#endif
