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
 * $Id: xine_plugin.c,v 1.1 2003/06/13 01:48:10 miguelfreitas Exp $
 *
 * advanced video deinterlacer plugin
 * Jun/2003 by Miguel Freitas
 *
 * heavily based on tvtime.sf.net by Billy Biggs
 */

#include "xine_internal.h"
#include "post.h"
#include "xineutils.h"
#include <pthread.h>

#include "tvtime.h"
#include "speedy.h"
#include "deinterlace.h"
#include "plugins/plugins.h"

/* plugin class initialization function */
static void *deinterlace_init_plugin(xine_t *xine, void *);


/* plugin catalog information */
post_info_t deinterlace_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 3, "tvtime", XINE_VERSION_CODE, &deinterlace_special_info, &deinterlace_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};


typedef struct post_plugin_deinterlace_s post_plugin_deinterlace_t;

#define MAX_NUM_METHODS 30
static char *enum_methods[MAX_NUM_METHODS];
static char *enum_pulldown[] = { "none", "vektor", NULL };
static char *enum_framerate[] = { "full", "half (top)", "half (bottom)", NULL };

/*
 * this is the struct used by "parameters api" 
 */
typedef struct deinterlace_parameters_s {

  int method;
  int enabled;
  int pulldown;
  int framerate_mode;
  int judder_correction;
  int use_progressive_frame_flag;

} deinterlace_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( deinterlace_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, method, enum_methods, 0, 0, 0, 
            "deinterlace method" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, enabled, NULL, 0, 1, 0,
            "enable/disable" )
PARAM_ITEM( POST_PARAM_TYPE_INT, pulldown, enum_pulldown, 0, 0, 0, 
            "pulldown algorithm" )
PARAM_ITEM( POST_PARAM_TYPE_INT, framerate_mode, enum_framerate, 0, 0, 0, 
            "framerate output mode" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, judder_correction, NULL, 0, 1, 0,
            "make frames evenly spaced for film mode (24 fps)" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, use_progressive_frame_flag, NULL, 0, 1, 0,
            "disable deinterlacing when progressive_frame flag is set" )
END_PARAM_DESCR( param_descr )


#define NUM_RECENT_FRAMES  2
#define FPS_24_DURATION    3754
#define FRAMES_TO_SYNC     20

/* plugin structure */
struct post_plugin_deinterlace_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  xine_stream_t     *stream;

  int                cur_method;
  int                enabled;
  int                pulldown;
  int                framerate_mode;
  int                judder_correction;
  int                use_progressive_frame_flag;
  tvtime_t          *tvtime;

  int                framecounter;
  uint8_t            rff_pattern;

  vo_frame_t        *recent_frame[NUM_RECENT_FRAMES];

  pthread_mutex_t    lock;
};


static void _flush_frames(post_plugin_deinterlace_t *this) 
{
  int i;
  
  for( i = 0; i < NUM_RECENT_FRAMES; i++ ) {
    if( this->recent_frame[i] ) {
      this->recent_frame[i]->free(this->recent_frame[i]);
      this->recent_frame[i] = NULL;
    }
  }
  tvtime_reset_context(this->tvtime);
}

static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)this_gen;
  deinterlace_parameters_t *param = (deinterlace_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);

  if( this->enabled != param->enabled )
    _flush_frames(this);

  this->cur_method = param->method;

  this->enabled = param->enabled;

  this->pulldown = param->pulldown;
  this->framerate_mode = param->framerate_mode;
  this->judder_correction = param->judder_correction;
  this->use_progressive_frame_flag = param->use_progressive_frame_flag;

  this->tvtime->pulldown_alg = this->pulldown;
  this->tvtime->curmethod = get_deinterlace_method( this->cur_method-1 );

  pthread_mutex_unlock (&this->lock);

  return 1;
}

int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)this_gen;
  deinterlace_parameters_t *param = (deinterlace_parameters_t *)param_gen;
  
  param->method = this->cur_method;
  param->enabled = this->enabled;
  param->pulldown = this->pulldown;
  param->framerate_mode = this->framerate_mode;
  param->judder_correction = this->judder_correction;
  param->use_progressive_frame_flag = this->use_progressive_frame_flag;

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
static int            deinterlace_get_property(xine_video_port_t *port_gen, int property);
static int            deinterlace_set_property(xine_video_port_t *port_gen, int property, int value);
static void           deinterlace_flush(xine_video_port_t *port_gen);
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
  int i;

  if (!class)
    return NULL;
  
  class->open_plugin     = deinterlace_open_plugin;
  class->get_identifier  = deinterlace_get_identifier;
  class->get_description = deinterlace_get_description;
  class->dispose         = deinterlace_class_dispose;


  setup_speedy_calls(0);

  greedy_plugin_init();

  linearblend_plugin_init();

  linear_plugin_init();
  weave_plugin_init();
  double_plugin_init();
  vfir_plugin_init();

  /* scalerbob_plugin_init(); -- not supported properly, use onefieldxv */

  greedy2frame_plugin_init();
  /*
  dscaler_greedyh_plugin_init();
  dscaler_twoframe_plugin_init();

  dscaler_videobob_plugin_init();
  dscaler_videoweave_plugin_init();
  dscaler_tomsmocomp_plugin_init();
  */
  filter_deinterlace_methods( config_flags, 5 /*fieldsavailable*/ );
  if( !get_num_deinterlace_methods() ) {
      fprintf( stderr, "tvtime: No deinterlacing methods "
                       "available, exiting.\n" );
      return NULL;
  }

  enum_methods[0] = "by driver";
  for(i = 0; i < get_num_deinterlace_methods(); i++ ) {
    enum_methods[i+1] = (char *)get_deinterlace_method(i)->short_name;
  }
  enum_methods[i+1] = NULL;

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
  this->cur_method = 7;
  this->pulldown = 1;
  this->enabled = 1;
  this->framerate_mode = 0;
  this->judder_correction = 1;
  this->use_progressive_frame_flag = 0;
  this->framecounter = 0;
  memset( &this->recent_frame, 0, sizeof(this->recent_frame) );

  this->tvtime = tvtime_new_context();

  this->tvtime->pulldown_alg = this->pulldown;
  this->tvtime->curmethod = get_deinterlace_method( this->cur_method-1 );

  pthread_mutex_init (&this->lock, NULL);
  
  port = post_intercept_video_port(&this->post, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open         = deinterlace_open;
  port->port.get_frame    = deinterlace_get_frame;
  port->port.close        = deinterlace_close;
  port->port.get_property = deinterlace_get_property;
  port->port.set_property = deinterlace_set_property;
  port->port.flush        = deinterlace_flush;
  
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
  return "tvtime";
}

static char *deinterlace_get_description(post_class_t *class_gen)
{
  return "advanced deinterlacer plugin with pulldown detection";
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

  _flush_frames(this);

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

static int deinterlace_get_property(xine_video_port_t *port_gen, int property) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  if( property == XINE_PARAM_VO_DEINTERLACE && this->cur_method )
    return this->enabled;
  else
    return port->original_port->get_property(port->original_port, property);
}

static int deinterlace_set_property(xine_video_port_t *port_gen, int property, int value) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  if( property == XINE_PARAM_VO_DEINTERLACE && this->cur_method ) {
    pthread_mutex_lock (&this->lock);

    if( this->enabled != value ) {
      int i;
    
      for( i = 0; i < NUM_RECENT_FRAMES; i++ ) {
        if( this->recent_frame[i] ) {
          this->recent_frame[i]->free(this->recent_frame[i]);
          this->recent_frame[i] = NULL;
        }
      }
    }

    this->enabled = value;

    pthread_mutex_unlock (&this->lock);

    return this->enabled;
  } else
    return port->original_port->set_property(port->original_port, property, value);
}

static void deinterlace_flush(xine_video_port_t *port_gen) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;

  _flush_frames(this);

  port->original_port->flush(port->original_port);
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

  /* do not intercept if not enabled or not interlaced */
  if( this->enabled && this->cur_method &&
      (flags & VO_INTERLACED_FLAG) ) {
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
  int i;

  this->stream = NULL;
  
  for( i = 0; i < NUM_RECENT_FRAMES; i++ ) {
    if( this->recent_frame[i] ) {
      this->recent_frame[i]->free(this->recent_frame[i]);
      this->recent_frame[i] = NULL;
    }
  }
 
  port->original_port->close(port->original_port, stream);
}


static int deinterlace_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_deinterlace_t *this = (post_plugin_deinterlace_t *)port->post;
  vo_frame_t *deinterlaced_frame;
  vo_frame_t *yuy2_frame;
  int i, skip, progressive = 0;

  post_restore_video_frame(frame, port);

  /* this should be used to detect any special rff pattern */
  this->rff_pattern = this->rff_pattern << 1;
  this->rff_pattern |= !!frame->repeat_first_field;
  
  if( ((this->rff_pattern & 0xff) == 0xaa ||
      (this->rff_pattern & 0xff) == 0x55) ) {
    /* special case for ntsc 3:2 pulldown */
    progressive = 1;
  }

  if( !frame->bad_frame ) {


    /* convert to YUY2 if needed */
    if( frame->format == XINE_IMGFMT_YV12 ) {

      yuy2_frame = port->original_port->get_frame(port->original_port,
        frame->width, frame->height, frame->ratio, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);
  
      yuy2_frame->pts = frame->pts;
      yuy2_frame->duration = frame->duration;
      extra_info_merge(yuy2_frame->extra_info, frame->extra_info);
  
      /* the logic for deciding upsampling to use comes from:
       * http://www.hometheaterhifi.com/volume_8_2/dvd-benchmark-special-report-chroma-bug-4-2001.html
       */
      yv12_to_yuy2(frame->base[0], frame->pitches[0], 
                   frame->base[1], frame->pitches[1], 
                   frame->base[2], frame->pitches[2], 
                   yuy2_frame->base[0], yuy2_frame->pitches[0],
                   frame->width, frame->height, 
                   frame->progressive_frame || progressive );
  
    } else {
      yuy2_frame = frame;
      yuy2_frame->lock(yuy2_frame);
    }


    pthread_mutex_lock (&this->lock);
    /* check if frame format changed */
    for(i = 0; i < NUM_RECENT_FRAMES; i++ ) {
      if( this->recent_frame[i] && 
          (this->recent_frame[i]->width != frame->width || 
           this->recent_frame[i]->height != frame->height) ) { 
        this->recent_frame[i]->free(this->recent_frame[i]);
        this->recent_frame[i] = NULL;
      }
    }


    /* using frame->progressive_frame may help displaying still menus.
     * however, it is known that some rare material set it wrong.
     */
    if( progressive || 
        (this->use_progressive_frame_flag && frame->progressive_frame) ) {

      pthread_mutex_unlock (&this->lock);
      skip = yuy2_frame->draw(yuy2_frame, stream);
      pthread_mutex_lock (&this->lock);
      frame->vpts = yuy2_frame->vpts;

    } else {
      int force24fps;
      int fields[2];

      if( this->framerate_mode == FRAMERATE_FULL ) {
        fields[0] = 0;
        fields[1] = 1;
      } else if ( this->framerate_mode == FRAMERATE_HALF_TFF ) {
        fields[0] = 0;
      } else if ( this->framerate_mode == FRAMERATE_HALF_BFF ) {
        fields[0] = 1;
      }


      force24fps = this->judder_correction && 
                   ( (this->pulldown == PULLDOWN_DALIAS) ||
                     (this->pulldown == PULLDOWN_VEKTOR && this->tvtime->filmmode) );
  
      skip = 0;
  
      /* Build the output from the first field. */
      pthread_mutex_unlock (&this->lock);
      deinterlaced_frame = port->original_port->get_frame(port->original_port,
        frame->width, frame->height, frame->ratio, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);
      pthread_mutex_lock (&this->lock);
  
      extra_info_merge(deinterlaced_frame->extra_info, frame->extra_info);
  
      if( this->tvtime->curmethod->doscalerbob ) {
        deinterlaced_frame->bad_frame = !tvtime_build_copied_field(this->tvtime,
                           deinterlaced_frame->base[0],
                           yuy2_frame->base[0], fields[0],
                           frame->width, frame->height, 
                           yuy2_frame->pitches[0], deinterlaced_frame->pitches[0] );
      } else {
        deinterlaced_frame->bad_frame = !tvtime_build_deinterlaced_frame(this->tvtime,
                           deinterlaced_frame->base[0],
                           yuy2_frame->base[0], 
                           (this->recent_frame[0])?this->recent_frame[0]->base[0]:yuy2_frame->base[0], 
                           (this->recent_frame[1])?this->recent_frame[1]->base[0]:yuy2_frame->base[0],
                           fields[0], frame->width, frame->height, 
                           yuy2_frame->pitches[0], deinterlaced_frame->pitches[0]);
      }
    
      if( this->tvtime->curmethod->doscalerbob ) {
        deinterlaced_frame->height /= 2;    
      } else {
  
      }
  
      pthread_mutex_unlock (&this->lock);
      if( force24fps ) {
        if( !deinterlaced_frame->bad_frame ) {
          this->framecounter++;
          if( frame->pts && this->framecounter > FRAMES_TO_SYNC ) {
            deinterlaced_frame->pts = frame->pts;
            this->framecounter = 0;
          } else
            deinterlaced_frame->pts = 0;
          deinterlaced_frame->duration = FPS_24_DURATION;
          skip = deinterlaced_frame->draw(deinterlaced_frame, stream);
        } else {
          skip = 0;
        }
      } else {
        deinterlaced_frame->pts = frame->pts;
        deinterlaced_frame->duration = (this->framerate_mode == FRAMERATE_FULL)?
                                       frame->duration/2:frame->duration;
        skip = deinterlaced_frame->draw(deinterlaced_frame, stream);
      }
  
      frame->vpts = deinterlaced_frame->vpts;
      deinterlaced_frame->free(deinterlaced_frame);
      pthread_mutex_lock (&this->lock);

      force24fps = this->judder_correction && 
                   ( (this->pulldown == PULLDOWN_DALIAS) ||
                     (this->pulldown == PULLDOWN_VEKTOR && this->tvtime->filmmode) );
  
      if( this->framerate_mode == FRAMERATE_FULL ) {
  
         /* Build the output from the second field. */
        pthread_mutex_unlock (&this->lock);
        deinterlaced_frame = port->original_port->get_frame(port->original_port,
          frame->width, frame->height, frame->ratio, XINE_IMGFMT_YUY2, VO_BOTH_FIELDS);
        pthread_mutex_lock (&this->lock);
  
        extra_info_merge(deinterlaced_frame->extra_info, frame->extra_info);
  
        if( skip > 0 && !this->pulldown ) {
          deinterlaced_frame->bad_frame = 1;
        } else {
          if( this->tvtime->curmethod->doscalerbob ) {
            deinterlaced_frame->bad_frame = !tvtime_build_copied_field(this->tvtime,
                               deinterlaced_frame->base[0],
                               yuy2_frame->base[0], fields[1],
                               frame->width, frame->height, 
                               yuy2_frame->pitches[0], deinterlaced_frame->pitches[0] );
          } else {
            deinterlaced_frame->bad_frame = !tvtime_build_deinterlaced_frame(this->tvtime,
                               deinterlaced_frame->base[0],
                               yuy2_frame->base[0], 
                               (this->recent_frame[0])?this->recent_frame[0]->base[0]:yuy2_frame->base[0], 
                               (this->recent_frame[1])?this->recent_frame[1]->base[0]:yuy2_frame->base[0],
                               fields[1], frame->width, frame->height, 
                               yuy2_frame->pitches[0], deinterlaced_frame->pitches[0]);
          }
        }
  
        if( this->tvtime->curmethod->doscalerbob ) {
          deinterlaced_frame->height /= 2;    
        } else {
  
        }
  
        pthread_mutex_unlock (&this->lock);
        if( force24fps ) {
          if( !deinterlaced_frame->bad_frame ) {
            this->framecounter++;
            if( frame->pts && this->framecounter > FRAMES_TO_SYNC ) {
              deinterlaced_frame->pts = frame->pts;
              this->framecounter = 0;
            } else
              deinterlaced_frame->pts = 0;
            deinterlaced_frame->duration = FPS_24_DURATION;
            skip = deinterlaced_frame->draw(deinterlaced_frame, stream);
          } else {
            skip = 0;
          }
        } else {
          deinterlaced_frame->pts = 0;
          deinterlaced_frame->duration = frame->duration/2;
          skip = deinterlaced_frame->draw(deinterlaced_frame, stream);
        }
  
        frame->vpts = deinterlaced_frame->vpts;
        deinterlaced_frame->free(deinterlaced_frame);
        pthread_mutex_lock (&this->lock);
      }
    }

    /* don't drop frames when pulldown mode is enabled. otherwise 
     * pulldown detection fails (yo-yo effect has also been seen)
     */
    if( this->pulldown )
      skip = 0;

    /* keep track of recent frames */
    i = NUM_RECENT_FRAMES-1;
    if( this->recent_frame[i] )
      this->recent_frame[i]->free(this->recent_frame[i]);
    for( ; i ; i-- )
      this->recent_frame[i] = this->recent_frame[i-1];
    this->recent_frame[0] = yuy2_frame;

    pthread_mutex_unlock (&this->lock);

  } else {
    skip = frame->draw(frame, stream);
  }
  
  
  return skip;
}
