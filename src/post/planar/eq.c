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
 * $Id: eq.c,v 1.5 2003/08/15 14:43:30 mroi Exp $
 *
 * mplayer's eq (soft video equalizer)
 * Copyright (C) Richard Felker
 */

#include "xine_internal.h"
#include "post.h"
#include "xineutils.h"
#include <pthread.h>


#ifdef ARCH_X86
static void process_MMX(unsigned char *dest, int dstride, unsigned char *src, int sstride,
		    int w, int h, int brightness, int contrast)
{
	int i;
	int pel;
	int dstep = dstride-w;
	int sstep = sstride-w;
	short brvec[4];
	short contvec[4];

	contrast = ((contrast+100)*256*16)/100;
	brightness = ((brightness+100)*511)/200-128 - contrast/32;

	brvec[0] = brvec[1] = brvec[2] = brvec[3] = brightness;
	contvec[0] = contvec[1] = contvec[2] = contvec[3] = contrast;
		
	while (h--) {
		asm volatile (
			"movq (%5), %%mm3 \n\t"
			"movq (%6), %%mm4 \n\t"
			"pxor %%mm0, %%mm0 \n\t"
			"movl %4, %%eax\n\t"
                       ".balign 16 \n\t"
			"1: \n\t"
			"movq (%0), %%mm1 \n\t"
			"movq (%0), %%mm2 \n\t"
			"punpcklbw %%mm0, %%mm1 \n\t"
			"punpckhbw %%mm0, %%mm2 \n\t"
			"psllw $4, %%mm1 \n\t"
			"psllw $4, %%mm2 \n\t"
			"pmulhw %%mm4, %%mm1 \n\t"
			"pmulhw %%mm4, %%mm2 \n\t"
			"paddw %%mm3, %%mm1 \n\t"
			"paddw %%mm3, %%mm2 \n\t"
			"packuswb %%mm2, %%mm1 \n\t"
			"addl $8, %0 \n\t"
			"movq %%mm1, (%1) \n\t"
			"addl $8, %1 \n\t"
			"decl %%eax \n\t"
			"jnz 1b \n\t"
			: "=r" (src), "=r" (dest)
			: "0" (src), "1" (dest), "r" (w>>3), "r" (brvec), "r" (contvec)
			: "%eax"
		);

		for (i = w&7; i; i--)
		{
			pel = ((*src++* contrast)>>12) + brightness;
			if(pel&768) pel = (-pel)>>31;
			*dest++ = pel;
		}

		src += sstep;
		dest += dstep;
	}
	asm volatile ( "emms \n\t" ::: "memory" );
}
#endif

static void process_C(unsigned char *dest, int dstride, unsigned char *src, int sstride,
		    int w, int h, int brightness, int contrast)
{
	int i;
	int pel;
	int dstep = dstride-w;
	int sstep = sstride-w;

	contrast = ((contrast+100)*256*256)/100;
	brightness = ((brightness+100)*511)/200-128 - contrast/512;

	while (h--) {
		for (i = w; i; i--)
		{
			pel = ((*src++* contrast)>>16) + brightness;
			if(pel&768) pel = (-pel)>>31;
			*dest++ = pel;
		}
		src += sstep;
		dest += dstep;
	}
}

static void (*process)(unsigned char *dest, int dstride, unsigned char *src, int sstride,
		       int w, int h, int brightness, int contrast);


/* plugin class initialization function */
void *eq_init_plugin(xine_t *xine, void *);

#if 0 /* moved to planar.c */
/* plugin catalog information */
post_info_t eq_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 4, "eq", XINE_VERSION_CODE, &eq_special_info, &eq_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif

typedef struct post_plugin_eq_s post_plugin_eq_t;

/*
 * this is the struct used by "parameters api" 
 */
typedef struct eq_parameters_s {

  int brightness;
  int contrast;

} eq_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( eq_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, brightness, NULL, -100, 100, 0, 
            "brightness" )
PARAM_ITEM( POST_PARAM_TYPE_INT, contrast, NULL, -100, 100, 0, 
            "contrast" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_eq_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  xine_stream_t     *stream;

  eq_parameters_t params;

  pthread_mutex_t    lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_eq_t *this = (post_plugin_eq_t *)this_gen;
  eq_parameters_t *param = (eq_parameters_t *)param_gen;

  pthread_mutex_lock (&this->lock);

  memcpy( &this->params, param, sizeof(eq_parameters_t) );

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_eq_t *this = (post_plugin_eq_t *)this_gen;
  eq_parameters_t *param = (eq_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(eq_parameters_t) );

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

typedef struct post_eq_out_s post_eq_out_t;
struct post_eq_out_s {
  xine_post_out_t  xine_out;

  post_plugin_eq_t *plugin;
};

/* plugin class functions */
static post_plugin_t *eq_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *eq_get_identifier(post_class_t *class_gen);
static char          *eq_get_description(post_class_t *class_gen);
static void           eq_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           eq_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            eq_rewire(xine_post_out_t *output, void *data);

/* replaced video_port functions */
static int            eq_get_property(xine_video_port_t *port_gen, int property);
static int            eq_set_property(xine_video_port_t *port_gen, int property, int value);
static void           eq_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *eq_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, double ratio, 
				       int format, int flags);
static void           eq_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* replaced vo_frame functions */
static int            eq_draw(vo_frame_t *frame, xine_stream_t *stream);


void *eq_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));

  if (!class)
    return NULL;
  
  class->open_plugin     = eq_open_plugin;
  class->get_identifier  = eq_get_identifier;
  class->get_description = eq_get_description;
  class->dispose         = eq_class_dispose;

  return class;
}


static post_plugin_t *eq_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_eq_t *this = (post_plugin_eq_t *)malloc(sizeof(post_plugin_eq_t));
  xine_post_in_t            *input = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  xine_post_in_t            *input_api = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_eq_out_t    *output = (post_eq_out_t *)malloc(sizeof(post_eq_out_t));
  post_video_port_t *port;
  
  if (!this || !input || !input_api || !output || !video_target || !video_target[0]) {
    free(this);
    free(input);
    free(input_api);
    free(output);
    return NULL;
  }

  this->stream = NULL;

  process = process_C;
#ifdef ARCH_X86
  if( xine_mm_accel() & MM_ACCEL_X86_MMX ) 
    process = process_MMX;
#endif

  this->params.brightness = 0;
  this->params.contrast = 0;

  pthread_mutex_init (&this->lock, NULL);
  
  port = post_intercept_video_port(&this->post, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open         = eq_open;
  port->port.get_frame    = eq_get_frame;
  port->port.close        = eq_close;
  port->port.get_property = eq_get_property;
  port->port.set_property = eq_set_property;
  
  input->name = "video";
  input->type = XINE_POST_DATA_VIDEO;
  input->data = (xine_video_port_t *)&port->port;

  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;

  output->xine_out.name   = "eqd video";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = eq_rewire;
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
  
  this->post.dispose = eq_dispose;
  
  return &this->post;
}

static char *eq_get_identifier(post_class_t *class_gen)
{
  return "eq";
}

static char *eq_get_description(post_class_t *class_gen)
{
  return "soft video equalizer";
}

static void eq_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void eq_dispose(post_plugin_t *this_gen)
{
  post_plugin_eq_t *this = (post_plugin_eq_t *)this_gen;
  post_eq_out_t *output = (post_eq_out_t *)xine_list_first_content(this->post.output);
  xine_video_port_t *port = *(xine_video_port_t **)output->xine_out.data;

  if (this->stream)
    port->close(port, this->stream);

  free(this->post.xine_post.audio_input);
  free(this->post.xine_post.video_input);
  free(xine_list_first_content(this->post.input));
  free(xine_list_next_content(this->post.input));
  free(xine_list_first_content(this->post.output));
  xine_list_free(this->post.input);
  xine_list_free(this->post.output);
  free(this);
}


static int eq_rewire(xine_post_out_t *output_gen, void *data)
{
  post_eq_out_t *output = (post_eq_out_t *)output_gen;
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

static int eq_get_property(xine_video_port_t *port_gen, int property) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_eq_t *this = (post_plugin_eq_t *)port->post;
  if( property == XINE_PARAM_VO_BRIGHTNESS )
    return 65535 * (this->params.brightness + 100) / 200;
  else if( property == XINE_PARAM_VO_CONTRAST )
    return 65535 * (this->params.contrast + 100) / 200;
  else
    return port->original_port->get_property(port->original_port, property);
}

static int eq_set_property(xine_video_port_t *port_gen, int property, int value) {
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_eq_t *this = (post_plugin_eq_t *)port->post;
  if( property == XINE_PARAM_VO_BRIGHTNESS ) {
    pthread_mutex_lock (&this->lock);
    this->params.brightness = (200 * value / 65535) - 100;
    pthread_mutex_unlock (&this->lock);
    return value;
  } else if( property == XINE_PARAM_VO_CONTRAST ) {
    pthread_mutex_lock (&this->lock);
    this->params.contrast = (200 * value / 65535) - 100;
    pthread_mutex_unlock (&this->lock);
    return value;
  } else
    return port->original_port->set_property(port->original_port, property, value);
}

static void eq_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_eq_t *this = (post_plugin_eq_t *)port->post;
  this->stream = stream;
  port->original_port->open(port->original_port, stream);
}

static vo_frame_t *eq_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, double ratio, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;

  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio, format, flags);

  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = eq_draw;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->copy = NULL;

  return frame;
}

static void eq_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_eq_t *this = (post_plugin_eq_t *)port->post;

  this->stream = NULL;
  
  port->original_port->close(port->original_port, stream);
}




static int eq_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_eq_t *this = (post_plugin_eq_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *yv12_frame;
  int skip;

  post_restore_video_frame(frame, port);

  if( !frame->bad_frame &&
      ((this->params.brightness != 0) || (this->params.contrast != 0)) ) {

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

    process(out_frame->base[0], out_frame->pitches[0],
            yv12_frame->base[0], yv12_frame->pitches[0],
            frame->width, frame->height,
            this->params.brightness, this->params.contrast);
    xine_fast_memcpy(out_frame->base[1],yv12_frame->base[1],
                     yv12_frame->pitches[1] * frame->height/2);
    xine_fast_memcpy(out_frame->base[2],yv12_frame->base[2],
                     yv12_frame->pitches[2] * frame->height/2);

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
