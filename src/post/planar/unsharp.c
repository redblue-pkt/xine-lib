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
 * $Id: unsharp.c,v 1.7 2003/10/06 21:52:44 miguelfreitas Exp $
 *
 * mplayer's unsharp
 * Copyright (C) 2002 Rémi Guyomarch <rguyom@pobox.com>
 */

#include "xine_internal.h"
#include "post.h"
#include "xineutils.h"
#include <pthread.h>

#ifndef HAVE_MEMALIGN
#define memalign(a,b) malloc(b)
#endif


#ifndef MIN
#define        MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define        MAX(a,b) (((a)>(b))?(a):(b))
#endif

/*===========================================================================*/

#define MIN_MATRIX_SIZE 3
#define MAX_MATRIX_SIZE 63

typedef struct FilterParam {
    int msizeX, msizeY;
    double amount;
    uint32_t *SC[MAX_MATRIX_SIZE-1];
} FilterParam;

struct vf_priv_s {
    FilterParam lumaParam;
    FilterParam chromaParam;
    int width, height;
};


/*===========================================================================*/

/* This code is based on :

An Efficient algorithm for Gaussian blur using finite-state machines
Frederick M. Waltz and John W. V. Miller

SPIE Conf. on Machine Vision Systems for Inspection and Metrology VII
Originally published Boston, Nov 98

*/

static void unsharp( uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int width, int height, FilterParam *fp ) {

    uint32_t **SC = fp->SC;
    uint32_t SR[MAX_MATRIX_SIZE-1], Tmp1, Tmp2;
    uint8_t* src2 = src; 

    int32_t res;
    int x, y, z;
    int amount = fp->amount * 65536.0;
    int stepsX = fp->msizeX/2;
    int stepsY = fp->msizeY/2;
    int scalebits = (stepsX+stepsY)*2;
    int32_t halfscale = 1 << ((stepsX+stepsY)*2-1);

    if( !fp->amount ) {
	if( src == dst )
	    return;
	if( dstStride == srcStride ) 
	    xine_fast_memcpy( dst, src, srcStride*height );
	else
	    for( y=0; y<height; y++, dst+=dstStride, src+=srcStride )
		xine_fast_memcpy( dst, src, width );
	return;
    }

    for( y=0; y<2*stepsY; y++ )
	memset( SC[y], 0, sizeof(SC[y][0]) * (width+2*stepsX) );

    for( y=-stepsY; y<height+stepsY; y++ ) {
	if( y < height ) src2 = src;
	memset( SR, 0, sizeof(SR[0]) * (2*stepsX-1) );
	for( x=-stepsX; x<width+stepsX; x++ ) {
	    Tmp1 = x<=0 ? src2[0] : x>=width ? src2[width-1] : src2[x];
	    for( z=0; z<stepsX*2; z+=2 ) {
		Tmp2 = SR[z+0] + Tmp1; SR[z+0] = Tmp1;
		Tmp1 = SR[z+1] + Tmp2; SR[z+1] = Tmp2;
	    }
	    for( z=0; z<stepsY*2; z+=2 ) {
		Tmp2 = SC[z+0][x+stepsX] + Tmp1; SC[z+0][x+stepsX] = Tmp1;
		Tmp1 = SC[z+1][x+stepsX] + Tmp2; SC[z+1][x+stepsX] = Tmp2;
	    }
	    if( x>=stepsX && y>=stepsY ) {
		uint8_t* srx = src - stepsY*srcStride + x - stepsX;
		uint8_t* dsx = dst - stepsY*dstStride + x - stepsX;
		
		res = (int32_t)*srx + ( ( ( (int32_t)*srx - (int32_t)((Tmp1+halfscale) >> scalebits) ) * amount ) >> 16 );
		*dsx = res>255 ? 255 : res<0 ? 0 : (uint8_t)res;
	    }
	}
	if( y >= 0 ) {
	    dst += dstStride;
	    src += srcStride;
	}
    }
}


/* plugin class initialization function */
void *unsharp_init_plugin(xine_t *xine, void *);

typedef struct post_plugin_unsharp_s post_plugin_unsharp_t;

/*
 * this is the struct used by "parameters api" 
 */
typedef struct unsharp_parameters_s {

  int luma_matrix_width;
  int luma_matrix_height;
  double luma_amount;

  int chroma_matrix_width;
  int chroma_matrix_height;
  double chroma_amount;

} unsharp_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( unsharp_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, luma_matrix_width, NULL, 3, 11, 0, 
            "width of the matrix (must be odd)" )
PARAM_ITEM( POST_PARAM_TYPE_INT, luma_matrix_height, NULL, 3, 11, 0, 
            "height of the matrix (must be odd)" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, luma_amount, NULL, -2, 2, 0, 
            "relative amount of sharpness/blur (=0 disable, <0 blur, >0 sharpen)" )
PARAM_ITEM( POST_PARAM_TYPE_INT, chroma_matrix_width, NULL, 3, 11, 0, 
            "width of the matrix (must be odd)" )
PARAM_ITEM( POST_PARAM_TYPE_INT, chroma_matrix_height, NULL, 3, 11, 0, 
            "height of the matrix (must be odd)" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, chroma_amount, NULL, -2, 2, 0, 
            "relative amount of sharpness/blur (=0 disable, <0 blur, >0 sharpen)" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_unsharp_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  xine_stream_t     *stream;

  unsharp_parameters_t params;
  struct vf_priv_s     priv;

  pthread_mutex_t    lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)this_gen;
  unsharp_parameters_t *param = (unsharp_parameters_t *)param_gen;
  FilterParam *fp;

  pthread_mutex_lock (&this->lock);

  if( &this->params != param )
    memcpy( &this->params, param, sizeof(unsharp_parameters_t) );

  fp = &this->priv.lumaParam;
  fp->msizeX = 1 | MIN( MAX( param->luma_matrix_width, MIN_MATRIX_SIZE ), MAX_MATRIX_SIZE );
  fp->msizeY = 1 | MIN( MAX( param->luma_matrix_height, MIN_MATRIX_SIZE ), MAX_MATRIX_SIZE );
  fp->amount = param->luma_amount;

  fp = &this->priv.chromaParam;
  fp->msizeX = 1 | MIN( MAX( param->chroma_matrix_width, MIN_MATRIX_SIZE ), MAX_MATRIX_SIZE );
  fp->msizeY = 1 | MIN( MAX( param->chroma_matrix_height, MIN_MATRIX_SIZE ), MAX_MATRIX_SIZE );
  fp->amount = param->chroma_amount;

  this->priv.width = this->priv.height = 0;

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)this_gen;
  unsharp_parameters_t *param = (unsharp_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(unsharp_parameters_t) );

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

typedef struct post_unsharp_out_s post_unsharp_out_t;
struct post_unsharp_out_s {
  xine_post_out_t  xine_out;

  post_plugin_unsharp_t *plugin;
};

/* plugin class functions */
static post_plugin_t *unsharp_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *unsharp_get_identifier(post_class_t *class_gen);
static char          *unsharp_get_description(post_class_t *class_gen);
static void           unsharp_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           unsharp_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            unsharp_rewire(xine_post_out_t *output, void *data);

/* replaced video_port functions */
static void           unsharp_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *unsharp_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, double ratio, 
				       int format, int flags);
static void           unsharp_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* replaced vo_frame functions */
static int            unsharp_draw(vo_frame_t *frame, xine_stream_t *stream);


void *unsharp_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));

  if (!class)
    return NULL;
  
  class->open_plugin     = unsharp_open_plugin;
  class->get_identifier  = unsharp_get_identifier;
  class->get_description = unsharp_get_description;
  class->dispose         = unsharp_class_dispose;

  return class;
}


static post_plugin_t *unsharp_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)malloc(sizeof(post_plugin_unsharp_t));
  xine_post_in_t            *input = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  xine_post_in_t            *input_api = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_unsharp_out_t    *output = (post_unsharp_out_t *)malloc(sizeof(post_unsharp_out_t));
  post_video_port_t *port;
  
  if (!this || !input || !input_api || !output || !video_target || !video_target[0]) {
    free(this);
    free(input);
    free(input_api);
    free(output);
    return NULL;
  }

  this->stream = NULL;

  this->params.luma_matrix_width = 5;
  this->params.luma_matrix_height = 5;
  this->params.luma_amount = 0.0;

  this->params.chroma_matrix_width = 3;
  this->params.chroma_matrix_height = 3;
  this->params.chroma_amount = 0.0;

  pthread_mutex_init (&this->lock, NULL);
  
  port = post_intercept_video_port(&this->post, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open         = unsharp_open;
  port->port.get_frame    = unsharp_get_frame;
  port->port.close        = unsharp_close;
  
  input->name = "video";
  input->type = XINE_POST_DATA_VIDEO;
  input->data = (xine_video_port_t *)&port->port;

  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;

  output->xine_out.name   = "unsharped video";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = unsharp_rewire;
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

  set_parameters ((xine_post_t *)this, &this->params);
  
  this->post.dispose = unsharp_dispose;
  
  return &this->post;
}

static char *unsharp_get_identifier(post_class_t *class_gen)
{
  return "unsharp";
}

static char *unsharp_get_description(post_class_t *class_gen)
{
  return "unsharp mask & gaussian blur";
}

static void unsharp_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void unsharp_dispose(post_plugin_t *this_gen)
{
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)this_gen;
  post_unsharp_out_t *output = (post_unsharp_out_t *)xine_list_first_content(this->post.output);
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


static int unsharp_rewire(xine_post_out_t *output_gen, void *data)
{
  post_unsharp_out_t *output = (post_unsharp_out_t *)output_gen;
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

static void unsharp_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)port->post;
  this->stream = stream;
  port->original_port->open(port->original_port, stream);
}

static vo_frame_t *unsharp_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, double ratio, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;

  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio, format, flags);

  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = unsharp_draw;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->copy = NULL;

  return frame;
}

static void unsharp_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)port->post;

  this->stream = NULL;
  
  port->original_port->close(port->original_port, stream);
}




static int unsharp_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_unsharp_t *this = (post_plugin_unsharp_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *yv12_frame;
  int skip;

  post_restore_video_frame(frame, port);

  if( !frame->bad_frame &&
      (this->priv.lumaParam.amount || this->priv.chromaParam.amount) ) {


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

    if( frame->width != this->priv.width || frame->height != this->priv.height ) {
       int z, stepsX, stepsY;
       FilterParam *fp;

       this->priv.width = frame->width;
       this->priv.height = frame->height;

       fp = &this->priv.lumaParam;
       memset( fp->SC, 0, sizeof( fp->SC ) );
       stepsX = fp->msizeX/2;
       stepsY = fp->msizeY/2;
       for( z=0; z<2*stepsY; z++ )
         fp->SC[z] = memalign( 16, sizeof(*(fp->SC[z])) * (frame->width+2*stepsX) );
     
       fp = &this->priv.chromaParam;
       memset( fp->SC, 0, sizeof( fp->SC ) );
       stepsX = fp->msizeX/2;
       stepsY = fp->msizeY/2;
       for( z=0; z<2*stepsY; z++ )
         fp->SC[z] = memalign( 16, sizeof(*(fp->SC[z])) * (frame->width+2*stepsX) );
    }

    unsharp( out_frame->base[0], yv12_frame->base[0], out_frame->pitches[0], yv12_frame->pitches[0], yv12_frame->width,   yv12_frame->height,   &this->priv.lumaParam );
    unsharp( out_frame->base[1], yv12_frame->base[1], out_frame->pitches[1], yv12_frame->pitches[1], yv12_frame->width/2, yv12_frame->height/2, &this->priv.chromaParam );
    unsharp( out_frame->base[2], yv12_frame->base[2], out_frame->pitches[2], yv12_frame->pitches[2], yv12_frame->width/2, yv12_frame->height/2, &this->priv.chromaParam );

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
