/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * This file is part of xine, a unix video player.
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
 * $Id: mpeg_encoders.c,v 1.7 2002/05/06 11:26:37 jcdutton Exp $
 *
 * mpeg encoders for the dxr3 video out plugin.  
 */ 

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_LIBRTE
#  define _GNU_SOURCE
#  include <unistd.h>
#  include <rte.h>
#endif
#ifdef HAVE_LIBFAME
#  include <fame.h>
#endif

#include "dxr3_video_out.h"

#define MV_COMMAND 0
/* buffer size for encoded mpeg1 stream; will hold one intra frame 
 * at 640x480 typical sizes are <50 kB. 512 kB should be plenty */
#define DEFAULT_BUFFER_SIZE 512*1024

#ifdef HAVE_LIBRTE
typedef struct {
  encoder_data_t encoder_data;
  int width, height;
  rte_context* context; /* handle for encoding */
  void* rte_ptr; /* buffer maintened by librte */
  double rte_time; /* frame time (s) */
  double rte_time_step; /* time per frame (s) */
  double rte_bitrate; /* mpeg out bitrate, default 2.3e6 bits/s */
} rte_data_t;


static void mp1e_callback(rte_context *context, void *data, ssize_t size, 
  void* user_data)
{
  dxr3_driver_t *this = (dxr3_driver_t*)user_data;
  em8300_register_t regs; 
  char tmpstr[128];

  if (this->enhanced_mode)
  {
    regs.microcode_register=1;   /* Yes, this is a MC Reg */
    regs.reg = MV_COMMAND;
    regs.val=6; /* Mike's mystery number :-) */
    ioctl(this->fd_control, EM8300_IOCTL_WRITEREG, &regs);
  }
  if (this->fd_video == CLOSED_FOR_ENCODER) {
    snprintf (tmpstr, sizeof(tmpstr), "%s_mv%s", this->devname, this->devnum);
    this->fd_video = open(tmpstr, O_WRONLY | O_NONBLOCK);
  }
  if (this->fd_video < 0) return;
  /* FIXME: Is a SETPTS necessary here? */
  if (write(this->fd_video, data, size) < 0)
    perror("dxr3: writing to video device");
}

static int rte_on_update_format(dxr3_driver_t *drv) 
{
  rte_data_t *this = (rte_data_t*)drv->enc;
  rte_context* context; 
  rte_codec *codec;
  int width, height;
  char tmpstr[128];

  width = drv->video_width;
  height = drv->video_height;

  if (this->context != 0) {/* already running */
    printf("dxr3: closing current encoding context.\n");
    rte_stop(this->context);
    rte_context_delete(this->context);
    this->context = 0;
  }
  this->width = width;
  this->height = height;

  this->context = rte_context_new (width, height, "mp1e", drv);
  if (! this->context) {
    printf("dxr3: failed to get rte context.\n");
    return 1;
  }
  context = this->context; /* shortcut */
  rte_set_verbosity(context, 2);
  /* get mpeg codec handle */
  codec = rte_codec_set(context, RTE_STREAM_VIDEO, 0, "mpeg1_video");
  if (! codec) {
    printf("dxr3: could not create codec.\n");
    rte_context_destroy(context);
    context = 0;
    return 1;
  }
  this->rte_bitrate=drv->config->register_range(drv->config,"dxr3.rte_bitrate",10000, 1000,20000, "Dxr3enc: rte mpeg output bitrate (kbit/s)",NULL,NULL,NULL);
  this->rte_bitrate *= 1000; /* config in kbit/s, rte wants bit/s */
  /* FIXME: this needs to be replaced with a codec option call. 
   * However, there seems to be none for the colour format!
   * So we'll use the deprecated set_video_parameters instead. 
   * Alternative is to manually set context->video_format (RTE_YU... )
   * and context->video_bytes (= width * height * bytes/pixel) */
  rte_set_video_parameters(context, 
    (drv->format == IMGFMT_YV12 ? RTE_YUV420 : RTE_YUYV),
    context->width, context->height, 
    context->video_rate, context->output_video_bits, 
    context->gop_sequence);
  /* Now set a whole bunch of codec options */
  /* If I understand correctly, virtual_frame_rate is the frame rate
   * of the source (can be anything), while coded_frame_rate must be
   * one of the mpeg1 alloweds */
  if (!rte_option_set(codec, "virtual_frame_rate", drv->fps))
    printf("dxr3: WARNING: rte_option_set failed; virtual_frame_rate=%g.\n",drv->fps);
  if (!rte_option_set(codec, "coded_frame_rate", drv->fps))
    printf("dxr3: WARNING: rte_option_set failed; coded_frame_rate=%g.\n",drv->fps);
  if (!rte_option_set(codec, "bit_rate", (int)this->rte_bitrate))
    printf("dxr3: WARNING: rte_option_set failed; bit_rate = %d.\n", 
      (int)this->rte_bitrate);
  if (!rte_option_set(codec, "gop_sequence", "I"))
    printf("dxr3: WARNING: rte_option_set failed; gop_sequence = \"I\".\n");
  /* just to be sure, disable motion comp (not needed in I frames) */
  if (!rte_option_set(codec, "motion_compensation", 0))
    printf("dxr3: WARNING: rte_option_set failed; motion_compensation = 0.\n");
  rte_set_input(context, RTE_VIDEO, RTE_PUSH, FALSE, NULL, NULL, NULL);
  snprintf (tmpstr, sizeof(tmpstr), "%s_mv", drv->devname);
  rte_set_output(context, mp1e_callback, NULL, NULL);
  if (!rte_init_context(context)) {
    printf("dxr3: cannot init the context: %s\n",
      context->error);
    rte_context_delete(context);
    context = 0;
    return 1;
  }
  /* do the sync'ing and start encoding */
  if (!rte_start_encoding(context)) {
    printf("dxr3: cannot start encoding: %s\n",
      context->error);
    rte_context_delete(context);
    context = 0;
    return 1;
  }
  this->rte_ptr = rte_push_video_data(context, NULL, 0); 
  if (! this->rte_ptr) {
    printf("dxr3: failed to get encoder buffer pointer.\n");
    return 1;
  }
  this->rte_time = 0.0;
  this->rte_time_step = 1.0/drv->fps;
  
  return 0;
}

static int rte_on_display_frame( dxr3_driver_t* drv, dxr3_frame_t* frame )
{
  int size;
  rte_data_t* this = (rte_data_t*)drv->enc;

  if ( (this->width != frame->width) || (this->height != frame->oheight)){
    /* maybe we were reinitialized and get an old frame. */
    return 0;
  }
  size = frame->width * frame->oheight;
  if (frame->vo_frame.format == IMGFMT_YV12)
    xine_fast_memcpy(this->rte_ptr, frame->real_base[0], size*3/2);
  else
    xine_fast_memcpy(this->rte_ptr, frame->real_base[0], size*2);
  this->rte_time += this->rte_time_step;
  this->rte_ptr = rte_push_video_data(this->context, this->rte_ptr, 
          this->rte_time);
    frame->vo_frame.displayed(&frame->vo_frame); 
  return 0;
}

static int rte_on_close( dxr3_driver_t *drv )
{
  rte_data_t *this = (rte_data_t*)drv->enc;
  if (this->context) {
    rte_stop(this->context);
    rte_context_delete(this->context);
    this->context = 0;
  }
  free(this);
  drv->enc = 0;
  return 0;
}

int dxr3_rte_init( dxr3_driver_t *drv )
{
  rte_data_t* this;
  if (! rte_init() ) {
    printf("dxr3: failed to init librte\n");
    return 1;
  } 
  this = malloc(sizeof(rte_data_t));
  if (!this)
    return 1;
  memset(this, 0, sizeof(rte_data_t));
  this->encoder_data.type = ENC_RTE;
  this->context = 0;
  this->encoder_data.on_update_format = rte_on_update_format;
  this->encoder_data.on_frame_copy = 0;
  this->encoder_data.on_display_frame = rte_on_display_frame;
  this->encoder_data.on_close = rte_on_close;
  drv->enc = (encoder_data_t*)this;
  return 0;
}

#endif

#ifdef HAVE_LIBFAME
typedef struct {
  encoder_data_t encoder_data;
  fame_context_t *fc; /* needed for fame calls */
  fame_parameters_t fp;
  fame_yuv_t yuv;
  /* temporary buffer for mpeg data */
  char    *buffer;
  /* temporary buffer for YUY2->YV12 conversion */
        uint8_t   *out[3]; /* aligned buffer for YV12 data */
        uint8_t   *buf; /* unaligned YV12 buffer */
} fame_data_t;


static fame_parameters_t dummy_init_fp = FAME_PARAMETERS_INITIALIZER;

static int fame_on_update_format(dxr3_driver_t *drv) 
{
  fame_data_t *this = (fame_data_t*)drv->enc;
   double fps;

  /* if YUY2 and dimensions changed, we need to re-allocate the
   * internal YV12 buffer */
  if (this->buf) free(this->buf);  
  this->buf = 0;
  this->out[0] = this->out[1] = this->out[2] = 0;
  if (drv->format == IMGFMT_YUY2) {
    int image_size = drv->video_width * drv->video_height;

    this->out[0] = malloc_aligned(16, image_size * 3/2, 
      (void*)&this->buf);
    this->out[1] = this->out[0] + image_size; 
    this->out[2] = this->out[1] + image_size/4; 

    /* fill with black (yuv 16,128,128) */
    memset(this->out[0], 16, image_size);
    memset(this->out[1], 128, image_size/4);
    memset(this->out[2], 128, image_size/4);

    printf("dxr3: Using YUY2->YV12 conversion\n");  
  }

  if (this->fc) {
    printf("dxr3: closing current encoding context.\n");
    fame_close(this->fc);
    this->fc = 0;
  }
  if (!this->fc)
    this->fc = fame_open();
  if (!this->fc) {
          printf("Couldn't start the FAME library\n");
    return 1;
  }
  
  if (!this->buffer)
    this->buffer = (unsigned char *) malloc (DEFAULT_BUFFER_SIZE);
  if (!this->buffer) {
    printf("Couldn't allocate temp buffer for mpeg data\n");
    return 1;
  }

  this->fp = dummy_init_fp; 
  this->fp.quality=drv->config->register_range(drv->config,"dxr3.fame_quality",90, 10,100, "Dxr3enc: fame mpeg encoding quality",NULL,NULL,NULL);
  /* the really interesting bit is the quantizer scale. The formula
   * below is copied from libfame's sources (could be changed in the
   * future) */
  printf("dxr3: quality %d -> quant scale = %d\n", this->fp.quality,
          1 + (30*(100-this->fp.quality)+50)/100);
  this->fp.width = drv->video_width;
  this->fp.height = drv->video_height;
  this->fp.profile = "mpeg1";
  this->fp.coding = "I";
  this->fp.verbose = 1;   /* we don't need any more info.. thanks :) */ 

  /* start guessing the framerate */
  fps = drv->fps;
  if (fabs(fps - 25) < 0.01) { /* PAL */
    printf("dxr3: setting mpeg output framerate to PAL (25 Hz)\n");
    this->fp.frame_rate_num = 25; this->fp.frame_rate_den = 1; 
  }  
  else if (fabs(fps - 24) < 0.01) { /* FILM */
          printf("dxr3: setting mpeg output framerate to FILM (24 Hz))\n");
    this->fp.frame_rate_num = 24; this->fp.frame_rate_den = 1; 
  }
  else if (fabs(fps - 23.976) < 0.01) { /* NTSC-FILM */
    printf("dxr3: setting mpeg output framerate to NTSC-FILM (23.976 Hz))\n");
    this->fp.frame_rate_num = 24000; this->fp.frame_rate_den = 1001; 
  }
  else if (fabs(fps - 29.97) < 0.01) { /* NTSC */
    printf("dxr3: setting mpeg output framerate to NTSC (29.97 Hz)\n");
    this->fp.frame_rate_num = 30000; this->fp.frame_rate_den = 1001;
  }
  else { /* try 1/fps, if not legal, libfame will go to PAL */
    this->fp.frame_rate_num = (int)(fps + 0.5); this->fp.frame_rate_den = 1;
    printf("dxr3: trying to set mpeg output framerate to %d Hz\n",
      this->fp.frame_rate_num);
  }
  fame_init (this->fc, &this->fp, this->buffer, DEFAULT_BUFFER_SIZE);
  
  return 0;
}

static int fame_prepare_frame(fame_data_t* this, dxr3_driver_t *drv, 
  dxr3_frame_t *frame) 
{
  int i, j, w2;
  uint8_t *y, *u, *v, *yuy2;

  if (frame->vo_frame.bad_frame)
    return 0;

  if (frame->vo_frame.format == IMGFMT_YUY2) {
    /* need YUY2->YV12 conversion */
    if (! (this->out[0] && this->out[1] && this->out[2]) ) {
      printf("dxr3: Internal error. Internal YV12 buffer not created.\n");
      return 1;
    }
    /* need conversion */
    y = this->out[0] + frame->width*drv->top_bar;
    u = this->out[1] + frame->width/2*(drv->top_bar/2);
    v = this->out[2] + frame->width/2*(drv->top_bar/2);
    yuy2 = frame->vo_frame.base[0];
    w2 = frame->width/2;
    for (i=0; i<frame->height; i+=2) {
      for (j=0; j<w2; j++) {
        /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
        *(y++) = *(yuy2++);
        *(u++) = *(yuy2++);
        *(y++) = *(yuy2++);
        *(v++) = *(yuy2++);
      }
      /* down sampling */
      for (j=0; j<w2; j++) {
        /* skip every second line for U and V */
        *(y++) = *(yuy2++);
        yuy2++;
        *(y++) = *(yuy2++);
        yuy2++;
      }
    }
    /* reset for encoder */
    y = this->out[0];
    u = this->out[1];
    v = this->out[2];
  }
  else { /* YV12 */
    y = frame->real_base[0];
    u = frame->real_base[1];
    v = frame->real_base[2];
  }

  this->yuv.y=y;
  this->yuv.u=u;
  this->yuv.v=v;
  return 0;
}


static int fame_on_display_frame( dxr3_driver_t* drv, dxr3_frame_t* frame)
{
  char tmpstr[128];
  em8300_register_t regs; 
  int size;
  fame_data_t *this = (fame_data_t*)drv->enc;

  if ((frame->width != this->fp.width) || (frame->oheight != this->fp.height)) {
    /* probably an old frame for a previous context. ignore it */
    return 0;
  }

  fame_prepare_frame(this, drv, frame);
  size = fame_encode_frame(this->fc, &this->yuv, NULL);
  
  if (drv->enhanced_mode)
  {
    regs.microcode_register=1; /* Yes, this is a MC Reg */
    regs.reg = MV_COMMAND;
    regs.val=6; /* Mike's mystery number :-) */
    ioctl(drv->fd_control, EM8300_IOCTL_WRITEREG, &regs);
  }

  if (drv->fd_video == CLOSED_FOR_ENCODER) {
    snprintf (tmpstr, sizeof(tmpstr), "%s_mv%s", drv->devname, drv->devnum);
    drv->fd_video = open(tmpstr, O_WRONLY | O_NONBLOCK);
  }
  if (drv->fd_video >= 0)
    /* FIXME: Is a SETPTS necessary here? */
    if (write(drv->fd_video, this->buffer, size) < 0) 
      perror("dxr3: writing to video device");
  frame->vo_frame.displayed(&frame->vo_frame); 
  return 0;
}

static int fame_on_close( dxr3_driver_t *drv )
{
  fame_data_t *this = (fame_data_t*)drv->enc;
  if (this->fc) {
    fame_close(this->fc);
  }
  free(this);
  drv->enc = 0;
  return 0;
}

int dxr3_fame_init( dxr3_driver_t *drv )
{
  fame_data_t* this;
  this = malloc(sizeof(fame_data_t));
  if (! this)
    return 1;
  memset(this, 0, sizeof(fame_data_t));
  this->encoder_data.type = ENC_FAME;
  /* fame context */
  this->fc = 0;
  this->encoder_data.on_update_format = fame_on_update_format;
  this->encoder_data.on_frame_copy = NULL;
  this->encoder_data.on_display_frame = fame_on_display_frame;
  this->encoder_data.on_close = fame_on_close;
  drv->enc = (encoder_data_t*)this;
  return 0;
}

#endif

