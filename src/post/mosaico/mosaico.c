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
 * $Id: mosaico.c,v 1.2 2003/02/27 01:15:50 tmmm Exp $
 */
 
/*
 * simple video mosaico plugin
 */

#include <xine/xine_internal.h>
#include <xine/post.h>

#define MOVERSION (5)

#define DEFAULT_X (50)
#define DEFAULT_Y (50)
#define DEFAULT_W (150)
#define DEFAULT_H (150)
#define MAXPIP (5)

/* plugin class initialization function */
static void *mosaico_init_plugin(xine_t *xine, void *);

/* plugin catalog information */
post_info_t mosaico_special_info = { XINE_POST_TYPE_VIDEO_FILTER };

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 2, "mosaico", MOVERSION, &mosaico_special_info, &mosaico_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

typedef struct mosaico_stream_s mosaico_stream_t;
struct mosaico_stream_s {
  unsigned int x, y, w, h;
};

/* plugin structure */
typedef struct post_mosaico_out_s post_mosaico_out_t;
struct post_mosaico_out_s {
  xine_post_out_t  xine_out;
  /* keep the stream for open/close when rewiring */
  xine_stream_t   *stream; 
  vo_frame_t *saved_frame;
  vo_frame_t *saved_frame_2[MAXPIP]; 
  pthread_mutex_t mut1, mut2;
  mosaico_stream_t info[MAXPIP];
/*  //pthread_key_t key;*/
  unsigned int pip;
};

typedef struct post_class_mosaico_s post_class_mosaico_t;
struct post_class_mosaico_s {
  post_class_t class;
  xine_t *xine;
  post_mosaico_out_t *ip;
};

/* plugin class functions */
static post_plugin_t *mosaico_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target);
static char          *mosaico_get_identifier(post_class_t *class_gen);
static char          *mosaico_get_description(post_class_t *class_gen);
static void           mosaico_class_dispose(post_class_t *class_gen);

/* plugin instance functions */
static void           mosaico_dispose(post_plugin_t *this_gen);

/* rewire function */
static int            mosaico_rewire(xine_post_out_t *output, void *data);

/* replaced video_port functions */
static void           mosaico_open(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *mosaico_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, int ratio_code, 
				       int format, int flags);
static void           mosaico_close(xine_video_port_t *port_gen, xine_stream_t *stream);
static vo_frame_t    *mosaico_get_frame_2(xine_video_port_t *port_gen, uint32_t width, 
				       uint32_t height, int ratio_code, 
				       int format, int flags);

/* replaced vo_frame functions */
static int            mosaico_draw(vo_frame_t *frame, xine_stream_t *stream);
static int            mosaico_draw_2(vo_frame_t *frame, xine_stream_t *stream);

static void x_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_mosaico_t *class = (post_class_mosaico_t *)data;
  int i;
  sscanf(cfg->key, "post.mosaico_input%d", &i);
  if(class->ip) {
    post_mosaico_out_t *this = class->ip;
    this->info[i].x = cfg->num_value;
  }
}

static void y_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_mosaico_t *class = (post_class_mosaico_t *)data;
  int i;
  sscanf(cfg->key, "post.mosaico_input%d", &i);
  if(class->ip) {
    post_mosaico_out_t *this = class->ip;
    this->info[i].y = cfg->num_value;
  }
}

static void w_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_mosaico_t *class = (post_class_mosaico_t *)data;
  int i;
  sscanf(cfg->key, "post.mosaico_input%d", &i);
  if(class->ip) {
    post_mosaico_out_t *this = class->ip;
    this->info[i].w = cfg->num_value;
  }
}

static void h_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  post_class_mosaico_t *class = (post_class_mosaico_t *)data;
  int i;
  sscanf(cfg->key, "post.mosaico_input%d", &i);
  if(class->ip) {
    post_mosaico_out_t *this = class->ip;
    this->info[i].h = cfg->num_value;
  }
}


static void *mosaico_init_plugin(xine_t *xine, void *data)
{
  post_class_mosaico_t *this = (post_class_mosaico_t *)malloc(sizeof(post_class_mosaico_t));
  config_values_t *cfg;
  int i;
  char string[255];
/*
  //add config entry management
  //post.mosaico_input_0_xywh //background
  //post.mosaico_input_[1..MAX]_(x, y, w, h) //little screen
*/

  if (!this)
    return NULL;
  
  this->class.open_plugin     = mosaico_open_plugin;
  this->class.get_identifier  = mosaico_get_identifier;
  this->class.get_description = mosaico_get_description;
  this->class.dispose         = mosaico_class_dispose;
  this->xine                  = xine;
  this->ip  = NULL;
  cfg = xine->config;

  for(i=1;i<=MAXPIP;i++) {
    sprintf(string, "post.mosaico_input%d_x", i);
    cfg->register_num (cfg, string, DEFAULT_X, _("Default x position"), NULL, 10, x_changed_cb, this);
    sprintf(string, "post.mosaico_input%d_y", i);
    cfg->register_num (cfg, string, DEFAULT_Y, _("Default y position"), NULL, 10, y_changed_cb, this);
    sprintf(string, "post.mosaico_input%d_w", i);
    cfg->register_num (cfg, string, DEFAULT_W, _("Default width"), NULL, 10, w_changed_cb, this);
    sprintf(string, "post.mosaico_input%d_h", i);
    cfg->register_num (cfg, string, DEFAULT_H, _("Default height"), NULL, 10, h_changed_cb, this);
  }

  printf("mosaico loaded\n");

  return &this->class;
}

static post_plugin_t *mosaico_open_plugin(post_class_t *class_gen, int inputs,
					 xine_audio_port_t **audio_target,
					 xine_video_port_t **video_target)
{
  post_plugin_t     *this   = (post_plugin_t *)malloc(sizeof(post_plugin_t));
  xine_post_in_t    *input1  = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
  xine_post_in_t    *input2;
  post_mosaico_out_t *output = (post_mosaico_out_t *)malloc(sizeof(post_mosaico_out_t));
  post_class_mosaico_t *class = (post_class_mosaico_t *) class_gen;
  post_video_port_t *port, *port2;
  xine_cfg_entry_t    x_entry, y_entry, w_entry, h_entry;
  int i;
  char string[255];
    
  printf("mosaico open\n");

  if (!this || !input1 || !output || !video_target || !video_target[0]) {
    free(this);
    free(input1);
    free(output);
    return NULL;
  }

  class->ip = output;  

  this->input = xine_list_new();
  this->output = xine_list_new();

/*  //background*/
  port = post_intercept_video_port(this, video_target[0]);
  port->port.open      = mosaico_open;
  port->port.get_frame = mosaico_get_frame;
  port->port.close     = mosaico_close;
  input1->name = "video in";
  input1->type = XINE_POST_DATA_VIDEO;
  input1->data = (xine_video_port_t *)&port->port;
  output->xine_out.name   = "video out";
  output->xine_out.type   = XINE_POST_DATA_VIDEO;
  output->xine_out.data   = (xine_video_port_t **)&port->original_port;
  output->xine_out.rewire = mosaico_rewire;
  output->stream          = NULL;
  output->pip = inputs-1;
  xine_list_append_content(this->output, output);
 
/*  //init mutex*/
  pthread_mutex_init(&output->mut1, NULL);
  pthread_mutex_init(&output->mut2, NULL);
  output->saved_frame = NULL;
  this->xine_post.audio_input    = (xine_audio_port_t **)malloc(sizeof(xine_audio_port_t *));
  this->xine_post.audio_input[0] = NULL;
  this->xine_post.video_input    = (xine_video_port_t **)malloc(sizeof(xine_video_port_t *) * (inputs+1));
  this->xine_post.video_input[0] = &port->port;
  xine_list_append_content(this->input, input1);

  for(i=1;i<inputs;i++) {
    /*registry operations*/
    input2 = (xine_post_in_t *)malloc(sizeof(xine_post_in_t));
    output->saved_frame_2[i-1] = NULL;
    sprintf(string, "post.mosaico_input%d_x", i);
    if(xine_config_lookup_entry(class->xine, string, &x_entry)) 
      x_changed_cb(class, &x_entry);

    sprintf(string, "post.mosaico_input%d_y", i);
    if(xine_config_lookup_entry(class->xine, string, &y_entry)) 
      y_changed_cb(class, &y_entry);

    sprintf(string, "post.mosaico_input%d_w", i);
    if(xine_config_lookup_entry(class->xine, string, &w_entry)) 
      w_changed_cb(class, &w_entry);

    sprintf(string, "post.mosaico_input%d_h", i);
    if(xine_config_lookup_entry(class->xine, string, &h_entry)) 
      h_changed_cb(class, &h_entry);

    port2 = post_intercept_video_port(this, video_target[i]);

    /* replace with our own get_frame function */
    port2->port.open = mosaico_open;
    port2->port.get_frame = mosaico_get_frame_2;
    port2->port.close = mosaico_close;

    sprintf(string, "video in %d", i);
    input2->name = strdup(string);
    input2->type = XINE_POST_DATA_VIDEO;
    input2->data = (xine_video_port_t *)&port2->port;
    
    this->xine_post.video_input[i] = &port2->port;
    xine_list_append_content(this->input, input2);
  }

  this->xine_post.video_input[i+1] = NULL;     
  this->dispose = mosaico_dispose;

/*  //pthread_key_create(&output->key, NULL);*/

  return this;
}

static char *mosaico_get_identifier(post_class_t *class_gen)
{
  return "mosaico";
}

static char *mosaico_get_description(post_class_t *class_gen)
{
  return "Mosaico is a picture in picture (pip) post plugin";
}

static void mosaico_class_dispose(post_class_t *class_gen)
{
  free(class_gen);
}


static void mosaico_dispose(post_plugin_t *this)
{
  post_mosaico_out_t *output = (post_mosaico_out_t *)xine_list_first_content(this->output);
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


static int mosaico_rewire(xine_post_out_t *output_gen, void *data)
{
  post_mosaico_out_t *output = (post_mosaico_out_t *)output_gen;
  xine_video_port_t *old_port = *(xine_video_port_t **)output_gen->data;
  /*xine_post_in_t *input = (xine_post_in_t *) data;*/
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

static void mosaico_open(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_mosaico_out_t *output = (post_mosaico_out_t *)xine_list_first_content(port->post->output);
  xine_video_port_t *pt;
  xine_post_in_t *in;
  int i = 0;
  int *dato, *dato2;

  in = xine_list_first_content(port->post->input);

  /*  while(in != NULL) {
    pt = in->data;
    if(pt == port_gen) break;//printf("trovato %d\n", i);

    in = xine_list_next_content(port->post->input);
    i++;
    }*/
  output->stream = stream;
  port->original_port->open(port->original_port, stream);
   
  /*  dato = malloc(sizeof(int));
  *dato = i;
  pthread_setspecific(output->key, dato);
  dato2 = pthread_getspecific(output->key);
  printf("%d\n", *dato2);*/
}

static vo_frame_t *mosaico_get_frame(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, int ratio_code, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;

  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio_code, format, flags);
  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = mosaico_draw;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->copy = NULL;
  return frame;
}

static vo_frame_t *mosaico_get_frame_2(xine_video_port_t *port_gen, uint32_t width, 
				    uint32_t height, int ratio_code, 
				    int format, int flags)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  vo_frame_t        *frame;

  frame = port->original_port->get_frame(port->original_port,
    width, height, ratio_code, format, flags);
  post_intercept_video_frame(frame, port);
  /* replace with our own draw function */
  frame->draw = mosaico_draw_2;
  /* decoders should not copy the frames, since they won't be displayed */
  frame->copy = NULL;
  return frame;
}

static void mosaico_close(xine_video_port_t *port_gen, xine_stream_t *stream)
{
  post_video_port_t *port = (post_video_port_t *)port_gen;
  post_mosaico_out_t *output = (post_mosaico_out_t *)xine_list_first_content(port->post->output);
  output->stream = NULL;
  port->original_port->close(port->original_port, stream);
}

static void frame_copy_content(vo_frame_t *to, vo_frame_t *from) {
  int size;

  if((to == NULL)||(from == NULL)) {
    printf("oi oi oi \n\n");
    return;
  }
  /* //it works only for XINE_IMGFMT_YV12*/
  switch (from->format) {
  case XINE_IMGFMT_YUY2:
    printf("not supported\n");
    /*size = new_frame->pitches[0] * new_frame->height;   
    for (i = 0; i < size; i++)
    new_frame->base[0][i] = frame->base[0][i];*/
    break;     
  
  case XINE_IMGFMT_YV12:
    /* Y */
    size = to->pitches[0] * to->height;    
    xine_fast_memcpy(to->base[0], from->base[0], size);

    /* U */
    size = to->pitches[1] * ((to->height + 1) / 2);
    xine_fast_memcpy(to->base[1], from->base[1], size);

    /* V */
    size = to->pitches[2] * ((to->height + 1) / 2);
    xine_fast_memcpy(to->base[2], from->base[2], size);
    
  }
}

static int _mosaico_draw_1(vo_frame_t *frame, post_mosaico_out_t *output) {  

  post_video_port_t *port;

  if(frame != NULL) {
    port =  (post_video_port_t *)frame->port;
    pthread_mutex_lock(&output->mut1);
    if(output->saved_frame != NULL) output->saved_frame->free(output->saved_frame);
    output->saved_frame = port->original_port->get_frame(port->original_port,
						 frame->width, frame->height, frame->ratio, frame->format, VO_BOTH_FIELDS);
    output->saved_frame->pts = frame->pts;
    output->saved_frame->duration = frame->duration;
    output->saved_frame->bad_frame = frame->bad_frame;
    frame_copy_content(output->saved_frame, frame);
    pthread_mutex_unlock(&output->mut1);
  }
  return 0;
}

static int _mosaico_draw_2(vo_frame_t *frame, post_mosaico_out_t *output, int cont) {

  unsigned long size, i, j, wx, wy, ciclo;
  unsigned long zx, zy, wid, wid2, des1, des2, hei;
  unsigned long scalex, scaley, pos_in_x, pos_in_y, pos_in;
  post_video_port_t *port;  

  pthread_mutex_lock(&output->mut1);
  pthread_mutex_lock(&output->mut2);

  if((output->saved_frame_2[cont] == NULL)&&(frame == NULL)) {
    printf("frame_2 NULL\n");
    pthread_mutex_unlock(&output->mut1);
    pthread_mutex_unlock(&output->mut2);
    return 0;
  }

  if(output->saved_frame == NULL) {
    printf("saved frame NULL\n");
    pthread_mutex_unlock(&output->mut1);
    pthread_mutex_unlock(&output->mut2);
    return 0;
  }  
 
  if(frame != NULL) {
    port = (post_video_port_t *)frame->port;
    if(output->saved_frame_2[cont] != NULL) {
      output->saved_frame_2[cont]->free(output->saved_frame_2[cont]);
      output->saved_frame_2[cont] = NULL;
    }
  
    output->saved_frame_2[cont] = port->original_port->get_frame(port->original_port,
						   frame->width, frame->height, frame->ratio, frame->format, VO_BOTH_FIELDS);
    output->saved_frame_2[cont]->pts = frame->pts;
    output->saved_frame_2[cont]->duration = frame->duration;
    output->saved_frame_2[cont]->bad_frame = frame->bad_frame;

    frame_copy_content(output->saved_frame_2[cont], frame);
  }

  for(ciclo=1;ciclo<=output->pip;ciclo++) {
    if(output->saved_frame_2[ciclo-1] == NULL) continue;
    /*initialize vars*/
    wx = output->info[ciclo].w;
    wy = output->info[ciclo].h;
    scalex = scaley = 3;
    wid = output->saved_frame->width ;   
    wid2 = output->saved_frame_2[ciclo-1]->width ;
    wid2 <<= scalex;
    hei = output->saved_frame_2[ciclo-1]->height;
    hei <<= scaley;
    zx = wid2 / wx;
    zy = hei / wy;
    pos_in_x = output->info[ciclo].x; pos_in_y = output->info[ciclo].y;
    pos_in = pos_in_y*wid+pos_in_x;


    switch (output->saved_frame_2[ciclo-1]->format) {
    case XINE_IMGFMT_YUY2:
      printf("not supported\n");
      /*size = new_frame->pitches[0] * new_frame->height;   
	for (i = 0; i < size; i++)
	new_frame->base[0][i] = frame->base[0][i];*/
      break;     
  
    case XINE_IMGFMT_YV12:
      /* Y */
      for(j = 0; j < wy; j++)
	for (i = 0; i < wx; i++) {
	  des1 = (i+ j*wid);
	  des2 = ((i*zx)>>scalex)+(((j*zy)>>scaley) * (wid2>>scalex));
/*
	  //if(!j) printf("%d %d\n", (i*zx)/scalex, wid2/scalex);
	  //if(!i) printf("%d %d\n", ((j*zy)>>scaley), (hei>>scaley));
*/
	  output->saved_frame->base[0][pos_in + des1] = output->saved_frame_2[ciclo-1]->base[0][des2];
	}

      /* U */
      wid = (wid+1) / 2;
      wid2 = (wid2+1) / 2;
      pos_in_x = (pos_in_x+1)/2;pos_in_y = (pos_in_y+1)/2;
      pos_in = pos_in_y * wid + pos_in_x;
      wx = (wx+1)/2; wy = (wy+1)/2; 
      for(j = 0; j < wy; j++)
	for (i = 0; i < wx; i++) {
	  des1 = (i+ j*wid);
	  des2 = ((i*zx)>>scalex)+(((j*zy)>>scaley) * (wid2>>scalex));
	  output->saved_frame->base[1][pos_in + des1] = output->saved_frame_2[ciclo-1]->base[1][(des2)];
	}

      /* V */
      for(j = 0; j < wy; j++)
	for (i = 0; i < wx; i++) {
	  des1 = (i+ j*wid);
	  des2 = ((i*zx)>>scalex)+(((j*zy)>>scaley) * (wid2>>scalex));
	  output->saved_frame->base[2][pos_in + des1] = output->saved_frame_2[ciclo-1]->base[2][des2];
	}

      break;
    default:    
      printf("Mosaico: cannot handle image format %d\n", frame->format);
      /*new_frame->free(new_frame);
	post_restore_video_frame(frame, port);
	return frame->draw(frame, stream);*/
    }
  }

  pthread_mutex_unlock(&output->mut1);
  pthread_mutex_unlock(&output->mut2);

  return 0;
}

static int mosaico_draw(vo_frame_t *frame, xine_stream_t *stream)
{
/*  //vo_frame_t *new_frame;*/
  int skip;
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_mosaico_out_t *output = (post_mosaico_out_t *)xine_list_first_content(port->post->output);

  _mosaico_draw_1(frame, output);
  _mosaico_draw_2(NULL, output, 0);
  pthread_mutex_lock(&output->mut1);
  if(output->saved_frame != NULL) {
    skip = output->saved_frame->draw(output->saved_frame, stream);
/*    //new_frame->free(new_frame);*/
    frame->vpts = output->saved_frame->vpts;
    pthread_mutex_unlock(&output->mut1);
    post_restore_video_frame(frame, port);
  
    return skip;
  }
  printf("ERROR!! oh oh\n\n");
  return 0;
}

static int mosaico_draw_2(vo_frame_t *frame, xine_stream_t *stream)
{
/*  //vo_frame_t *new_frame;*/
  post_video_port_t *port = (post_video_port_t *)frame->port;
  post_mosaico_out_t *output = (post_mosaico_out_t *)xine_list_first_content(port->post->output);
  int *dato;

  xine_video_port_t *pt;
  xine_post_in_t *in;
  int i = 0;

  in = xine_list_first_content(port->post->input);

  while(in != NULL) {
    pt = in->data;
    if(pt == frame->port) {
/*      //printf("trovato %d\n", i);*/
      break;
    }
    in = xine_list_next_content(port->post->input);
    i++;
  }

  _mosaico_draw_1(NULL, output);
  _mosaico_draw_2(frame, output, i-1);

  post_restore_video_frame(frame, port);
  
  return 0;
}
