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
 * $Id: denoise3d.c,v 1.1 2003/06/29 18:56:24 miguelfreitas Exp $
 *
 * mplayer's denoise3d
 * Copyright (C) 2003 Daniel Moreno <comac@comac.darktech.org>
 */

#include "xine_internal.h"
#include "post.h"
#include "xineutils.h"
#include <math.h>
#include <pthread.h>

#define PARAM1_DEFAULT 4.0
#define PARAM2_DEFAULT 3.0
#define PARAM3_DEFAULT 6.0
#define MAX_LINE_WIDTH 2048


/* plugin class initialization function */
void *denoise3d_init_plugin(xine_t *xine, void *);


#if 0 /* moved to planar.c */
/* plugin catalog information */
post_info_t denoise3d_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 3, "denoise3d", XINE_VERSION_CODE, &denoise3d_special_info, &denoise3d_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
#endif

typedef struct post_plugin_denoise3d_s post_plugin_denoise3d_t;


/*
 * this is the struct used by "parameters api" 
 */
typedef struct denoise3d_parameters_s {

  double luma;
  double chroma;
  double time;

} denoise3d_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR( denoise3d_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, luma, NULL, 0, 10, 0, 
            "spatial luma strength" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, chroma, NULL, 0, 10, 0, 
            "spatial chroma strength" )
PARAM_ITEM( POST_PARAM_TYPE_DOUBLE, time, NULL, 0, 10, 0, 
            "temporal strength" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_denoise3d_s {
  post_plugin_t post;

  /* private data */
  xine_video_port_t *vo_port;
  xine_stream_t     *stream;

  denoise3d_parameters_t params;

  int Coefs[4][512];
  unsigned char Line[MAX_LINE_WIDTH];
  vo_frame_t *prev_frame;

  pthread_mutex_t    lock;
};

#define ABS(A) ( (A) > 0 ? (A) : -(A) )

static void PrecalcCoefs(int *Ct, double Dist25)
{
    int i;
    double Gamma, Simil;

    Gamma = log(0.25) / log(1.0 - Dist25/255.0);

    for (i = -256; i <= 255; i++)
    {
        Simil = 1.0 - ABS(i) / 255.0;
        Ct[256+i] = pow(Simil, Gamma) * 65536;
    }
}

static int set_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)this_gen;
  denoise3d_parameters_t *param = (denoise3d_parameters_t *)param_gen;
  double ChromTmp;

  pthread_mutex_lock (&this->lock);

  if( &this->params != param )
    memcpy( &this->params, param, sizeof(denoise3d_parameters_t) );

  ChromTmp = this->params.time * this->params.chroma / this->params.luma;

  PrecalcCoefs(this->Coefs[0], this->params.luma);
  PrecalcCoefs(this->Coefs[1], this->params.time);
  PrecalcCoefs(this->Coefs[2], this->params.chroma);
  PrecalcCoefs(this->Coefs[3], ChromTmp);

  pthread_mutex_unlock (&this->lock);

  return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) {
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)this_gen;
  denoise3d_parameters_t *param = (denoise3d_parameters_t *)param_gen;


  memcpy( param, &this->params, sizeof(denoise3d_parameters_t) );

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

typedef struct post_denoise3d_out_s post_denoise3d_out_t;
struct post_denoise3d_out_s {
  xine_post_out_t  xine_out;

  post_plugin_denoise3d_t *plugin;
};

/* plugin class functions */
static post_plugin_t *denoise3d_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *denoise3d_get_identifier(post_class_t *class_gen);
static char          *denoise3d_get_description(post_class_t *class_gen);
static void           denoise3d_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           denoise3d_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            denoise3d_rewire(xine_post_out_t *output, void *data);

/* replaced video_port functions */
static void           denoise3d_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *denoise3d_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, int ratio_code, 
				       int format, int flags);
static void           denoise3d_close(xine_video_port_t *port_gen, xine_stream_t *stream);

/* replaced vo_frame functions */
static int            denoise3d_draw(vo_frame_t *frame, xine_stream_t *stream);


void *denoise3d_init_plugin(xine_t *xine, void *data)
{
  post_class_t *class = (post_class_t *)malloc(sizeof(post_class_t));

  if (!class)
    return NULL;
  
  class->open_plugin     = denoise3d_open_plugin;
  class->get_identifier  = denoise3d_get_identifier;
  class->get_description = denoise3d_get_description;
  class->dispose         = denoise3d_class_dispose;

  return class;
}


static post_plugin_t *denoise3d_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)malloc(sizeof(post_plugin_denoise3d_t));
  xine_post_in_t            *input = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  xine_post_in_t            *input_api = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  post_denoise3d_out_t    *output = (post_denoise3d_out_t *)malloc(sizeof(post_denoise3d_out_t));
  post_video_port_t *port;
  
  if (!this || !input || !output || !video_target || !video_target[0]) {
    free(this);
    free(input);
    free(output);
    return NULL;
  }

  this->stream = NULL;

  this->params.luma = PARAM1_DEFAULT;
  this->params.chroma = PARAM2_DEFAULT;
  this->params.time = PARAM3_DEFAULT;
  this->prev_frame = NULL;

  pthread_mutex_init (&this->lock, NULL);

  port = post_intercept_video_port(&this->post, video_target[0]);
  /* replace with our own get_frame function */
  port->port.open         = denoise3d_open;
  port->port.get_frame    = denoise3d_get_frame;
  port->port.close        = denoise3d_close;
  
  input->name = "video";
  input->type = XINE_POST_DATA_VIDEO;
  input->data = (xine_video_port_t *)&port->port;

  input_api->name = "parameters";
  input_api->type = XINE_POST_DATA_PARAMETERS;
  input_api->data = &post_api;

  output->xine_out.name   = "denoise3d video";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = denoise3d_rewire;
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
  
  this->post.dispose = denoise3d_dispose;

  set_parameters ((xine_post_t *)this, &this->params);
  
  return &this->post;
}

static char *denoise3d_get_identifier(post_class_t *class_gen)
{
  return "denoise3d";
}

static char *denoise3d_get_description(post_class_t *class_gen)
{
  return "3D Denoiser (variable lowpass filter)";
}

static void denoise3d_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void denoise3d_dispose(post_plugin_t *this_gen)
{
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)this_gen;
  post_denoise3d_out_t *output = (post_denoise3d_out_t *)xine_list_first_content(this->post.output);
  xine_video_port_t *port = *(xine_video_port_t **)output->xine_out.data;

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


static int denoise3d_rewire(xine_post_out_t *output_gen, void *data)
{
  post_denoise3d_out_t *output = (post_denoise3d_out_t *)output_gen;
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

static void denoise3d_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)port->post;
  this->stream = stream;
  port->original_port->open(port->original_port, stream);
}

static vo_frame_t *denoise3d_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, int ratio_code, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;

  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio_code, format, flags);

  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = denoise3d_draw;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->copy = NULL;

  return frame;
}

static void denoise3d_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)port->post;

  if(this->prev_frame) {
    this->prev_frame->free(this->prev_frame);
    this->prev_frame = NULL;
  }

  this->stream = NULL;
  
  port->original_port->close(port->original_port, stream);
}

#define LowPass(Prev, Curr, Coef) (((Prev)*Coef[Prev - Curr] + (Curr)*(65536-(Coef[Prev - Curr]))) / 65536)

static void deNoise(unsigned char *Frame,        // mpi->planes[x]
                    unsigned char *FramePrev,    // pmpi->planes[x]
                    unsigned char *FrameDest,    // dmpi->planes[x]
                    unsigned char *LineAnt,      // vf->priv->Line (width bytes)
                    int W, int H, int sStride, int pStride, int dStride,
                    int *Horizontal, int *Vertical, int *Temporal)
{
    int X, Y;
    int sLineOffs = 0, pLineOffs = 0, dLineOffs = 0;
    unsigned char PixelAnt;

    /* First pixel has no left nor top neightbour. Only previous frame */
    LineAnt[0] = PixelAnt = Frame[0];
    FrameDest[0] = LowPass(FramePrev[0], LineAnt[0], Temporal);

    /* Fist line has no top neightbour. Only left one for each pixel and
     * last frame */
    for (X = 1; X < W; X++)
    {
        PixelAnt = LowPass(PixelAnt, Frame[X], Horizontal);
        LineAnt[X] = PixelAnt;
        FrameDest[X] = LowPass(FramePrev[X], LineAnt[X], Temporal);
    }

    for (Y = 1; Y < H; Y++)
    {
	sLineOffs += sStride, pLineOffs += pStride, dLineOffs += dStride;
        /* First pixel on each line doesn't have previous pixel */
        PixelAnt = Frame[sLineOffs];
        LineAnt[0] = LowPass(LineAnt[0], PixelAnt, Vertical);
        FrameDest[dLineOffs] = LowPass(FramePrev[pLineOffs], LineAnt[0], Temporal);

        for (X = 1; X < W; X++)
        {
            /* The rest are normal */
            PixelAnt = LowPass(PixelAnt, Frame[sLineOffs+X], Horizontal);
            LineAnt[X] = LowPass(LineAnt[X], PixelAnt, Vertical);
            FrameDest[dLineOffs+X] = LowPass(FramePrev[pLineOffs+X], LineAnt[X], Temporal);
        }
    }
}


static int denoise3d_draw(vo_frame_t *frame, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_plugin_denoise3d_t *this = (post_plugin_denoise3d_t *)port->post;
  vo_frame_t *out_frame;
  vo_frame_t *prev_frame;
  vo_frame_t *yv12_frame;
  int cw, ch;
  int skip;

  post_restore_video_frame(frame, port);

  if( !frame->bad_frame ) {


    /* convert to YV12 if needed */
    if( frame->format != XINE_IMGFMT_YV12 ) {

      yv12_frame = port->original_port->get_frame(port->original_port,
        frame->width, frame->height, frame->ratio, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);
  
      yv12_frame->pts = frame->pts;
      yv12_frame->duration = frame->duration;
      extra_info_merge(yv12_frame->extra_info, frame->extra_info);
  
      /* FIXME: implement! */
      /* yuy2_to_yv12() */
  
    } else {
      yv12_frame = frame;
      yv12_frame->lock(yv12_frame);
    }


    out_frame = port->original_port->get_frame(port->original_port,
      frame->width, frame->height, frame->ratio, XINE_IMGFMT_YV12, VO_BOTH_FIELDS);

  
    extra_info_merge(out_frame->extra_info, frame->extra_info);
  
    out_frame->pts = frame->pts;
    out_frame->duration = frame->duration;

    pthread_mutex_lock (&this->lock);

    cw = yv12_frame->width/2;
    ch = yv12_frame->height/2;
    prev_frame = (this->prev_frame) ? this->prev_frame : yv12_frame;

    deNoise(yv12_frame->base[0], prev_frame->base[0], out_frame->base[0],
            this->Line, yv12_frame->width, yv12_frame->height,
            yv12_frame->pitches[0], prev_frame->pitches[0], out_frame->pitches[0],
            this->Coefs[0] + 256,
            this->Coefs[0] + 256,
            this->Coefs[1] + 256);
    deNoise(yv12_frame->base[1], prev_frame->base[1], out_frame->base[1],
            this->Line, cw, ch,
            yv12_frame->pitches[1], prev_frame->pitches[1], out_frame->pitches[1],
            this->Coefs[2] + 256,
            this->Coefs[2] + 256,
            this->Coefs[3] + 256);
    deNoise(yv12_frame->base[2], prev_frame->base[2], out_frame->base[2],
            this->Line, cw, ch,
            yv12_frame->pitches[2], prev_frame->pitches[2], out_frame->pitches[2],
            this->Coefs[2] + 256,
            this->Coefs[2] + 256,
            this->Coefs[3] + 256);

    pthread_mutex_unlock (&this->lock);

    skip = out_frame->draw(out_frame, stream);
  
    frame->vpts = out_frame->vpts;

    out_frame->free(out_frame);

    if(this->prev_frame)
      this->prev_frame->free(this->prev_frame);
    this->prev_frame = yv12_frame;

  } else {
    skip = frame->draw(frame, stream);
  }

  
  return skip;
}
