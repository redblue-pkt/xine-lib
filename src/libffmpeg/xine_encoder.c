/* 
 * Copyright (C) 2000-2003 the xine project
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
 * $Id: xine_encoder.c,v 1.1 2003/05/25 18:34:54 mroi Exp $
 */
 
/* mpeg encoders for the dxr3 video out plugin. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

#include "../dxr3/video_out_dxr3.h"
#include "libavcodec/avcodec.h"

#define LOG_ENC 0

/* buffer size for encoded mpeg1 stream; will hold one intra frame 
 * at 640x480 typical sizes are <50 kB. 512 kB should be plenty */
#define DEFAULT_BUFFER_SIZE 512*1024


/*initialisation function*/
int         dxr3_encoder_init(dxr3_driver_t *drv);

/* functions required by encoder api */
static int lavc_on_update_format(dxr3_driver_t *drv, dxr3_frame_t *frame);
static int lavc_on_display_frame(dxr3_driver_t *drv, dxr3_frame_t *frame);
static int lavc_on_unneeded(dxr3_driver_t *drv);

/*encoder structure*/
typedef struct lavc_data_s {
  encoder_data_t     encoder_data;
  AVCodecContext     *context;       	/* handle for encoding */
  int                width, height;  	/* width and height of the video frame */
  uint8_t            *ffmpeg_buffer;    /* lavc buffer */
  AVFrame            *picture;       	/* picture to be encoded */
  uint8_t            *out[3];  			/* aligned buffer for YV12 data */
  uint8_t            *buf;     			/* unaligned YV12 buffer */
} lavc_data_t;


int dxr3_encoder_init(dxr3_driver_t *drv)
{
  lavc_data_t* this;
  avcodec_init();

  register_avcodec(&mpeg1video_encoder);  
#if LOG_ENC
  printf("dxr3_mpeg_encoder: lavc init , version %x\n", avcodec_version());
#endif
  this = malloc(sizeof(lavc_data_t));
  if (!this) return 0;
  memset(this, 0, sizeof(lavc_data_t));

  this->encoder_data.type             = ENC_LAVC;
  this->encoder_data.on_update_format = lavc_on_update_format;
  this->encoder_data.on_frame_copy    = NULL;
  this->encoder_data.on_display_frame = lavc_on_display_frame;
  this->encoder_data.on_unneeded      = lavc_on_unneeded;
  this->context                       = 0;
  
  drv->enc = &this->encoder_data;
  return 1;
}

/* helper function */
static int lavc_prepare_frame(lavc_data_t *this, dxr3_driver_t *drv, dxr3_frame_t *frame);

static int lavc_on_update_format(dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  lavc_data_t *this = (lavc_data_t *)drv->enc;
  AVCodec *codec;
  double fps;
  unsigned char use_quantizer;
  
  if (this->context) {
    avcodec_close(this->context);
    free(this->context);
    free(this->picture);
    this->context = NULL;
    this->picture = NULL;
  }
  
  /* if YUY2 and dimensions changed, we need to re-allocate the
   * internal YV12 buffer */
  if (frame->vo_frame.format == XINE_IMGFMT_YUY2) {
    int image_size = frame->vo_frame.width * frame->oheight;

    this->out[0] = xine_xmalloc_aligned(16, image_size * 3/2, 
      (void *)&this->buf);
    this->out[1] = this->out[0] + image_size; 
    this->out[2] = this->out[1] + image_size/4; 

    /* fill with black (yuv 16,128,128) */
    memset(this->out[0], 16, image_size);
    memset(this->out[1], 128, image_size/4);
    memset(this->out[2], 128, image_size/4);
#if LOG_ENC
    printf("dxr3_mpeg_encoder: Using YUY2->YV12 conversion\n");  
#endif
  }
  
  
  /* resolution must be a multiple of two */
  if ((frame->vo_frame.pitches[0] % 2 != 0) || (frame->oheight % 2 != 0)) {
    printf("dxr3_mpeg_encoder: lavc only handles video dimensions which are multiples of 2\n");
    return 0;
  }

  /* get mpeg codec handle */
  codec = avcodec_find_encoder(CODEC_ID_MPEG1VIDEO);
  if (!codec) {
    printf("dxr3_mpeg_encoder: lavc MPEG1 codec not found\n");
    return 0;
  }
#if LOG_ENC
  printf("dxr3_mpeg_encoder: lavc MPEG1 encoder found.\n");
#endif
  this->width = frame->vo_frame.pitches[0];
  this->height = frame->oheight;

  this->context = avcodec_alloc_context();
  if (!this->context) {
    printf("dxr3_mpeg_encoder: Couldn't start the ffmpeg library\n");
    return 0;
  } 
  this->picture = avcodec_alloc_frame();
  if (!this->picture) {
    printf("dxr3_mpeg_encoder: Couldn't allocate ffmpeg frame\n");
    return 0;
  }
  
  /* put sample parameters */
  this->context->bit_rate = drv->class->xine->config->register_range(drv->class->xine->config,
    "dxr3.lavc_bitrate", 10000, 1000, 20000,
    _("Dxr3enc: libavcodec mpeg output bitrate (kbit/s)"),
    _("The bitrate the libavcodec mpeg encoder should use for dxr3's encoding mode"), 10,
    NULL, NULL);
    this->context->bit_rate *= 1000; /* config in kbit/s, libavcodec wants bit/s */
    
  use_quantizer = drv->class->xine->config->register_range(drv->class->xine->config,
    "dxr3.lavc_quantizer", 1, 0, 1,
    _("Dxr3enc: Use quantizer instead of bitrate"),NULL, 0, NULL, NULL);

  if (use_quantizer) {        
    this->context->qmin = drv->class->xine->config->register_range(drv->class->xine->config,
    "dxr3.lavc_qmin", 1, 1, 10,
    _("Dxr3enc: Minimum quantizer"),NULL , 10, NULL, NULL);
     
    this->context->qmax = drv->class->xine->config->register_range(drv->class->xine->config,
    "dxr3.lavc_qmax", 2, 1, 20,
    _("Dxr3enc: Maximum quantizer"),NULL, 10, NULL, NULL);  
  }

#if LOG_ENC
  printf("dxr3_mpeg_encoder: lavc -> bitrate %d  \n", this->context->bit_rate);
#endif 
  
  this->context->width  = frame->vo_frame.width;
  this->context->height = frame->oheight;

  this->context->gop_size = 0; /*intra frames only */
  this->context->me_method = ME_ZERO; /*motion estimation type*/
  
  /* start guessing the framerate */
  fps = 90000.0 / frame->vo_frame.duration;
#if LOG_ENC
   printf("dxr3_mpeg_encoder: fps = %f\n", fps);
#endif
  
  
  if (fabs(fps - 25) < 0.01) { /* PAL */
#if LOG_ENC
    printf("dxr3_mpeg_encoder: setting mpeg output framerate to PAL (25 Hz)\n");
#endif
    this->context->frame_rate = 25;  
    this->context->frame_rate_base= 1;
  }  
  else if (fabs(fps - 24) < 0.01) { /* FILM */
#if LOG_ENC
    printf("dxr3_mpeg_encoder: setting mpeg output framerate to FILM (24 Hz)\n");
#endif
    this->context->frame_rate = 24;  
    this->context->frame_rate_base= 1;
  }
  else if (fabs(fps - 23.976) < 0.01) { /* NTSC-FILM */
#if LOG_ENC
    printf("dxr3_mpeg_encoder: setting mpeg output framerate to NTSC-FILM (23.976 Hz)\n");
#endif
    this->context->frame_rate = 24000;  
    this->context->frame_rate_base= 1001;
  }
  else if (fabs(fps - 29.97) < 0.01) { /* NTSC */
#if LOG_ENC
    printf("dxr3_mpeg_encoder: setting mpeg output framerate to NTSC (29.97 Hz)\n");
#endif
    this->context->frame_rate = 30000;  
    this->context->frame_rate_base= 1001;
  } else { /* will go to PAL */
    this->context->frame_rate = 25;
    this->context->frame_rate_base= 1;
#if LOG_ENC
    printf("dxr3_mpeg_encoder: trying to set mpeg output framerate to 25 Hz\n");
#endif
  }
  
  /* open avcodec */
  if (avcodec_open(this->context, codec) < 0) {
    printf("dxr3_mpeg_encoder: could not open codec\n");
    return 0;
  }
#if LOG_ENC
  printf("dxr3_mpeg_encoder: lavc MPEG1 codec opened.\n");
#endif
  
  if (!this->ffmpeg_buffer)
    this->ffmpeg_buffer = (unsigned char *)malloc(DEFAULT_BUFFER_SIZE); /* why allocate more than needed ?! */
  if (!this->ffmpeg_buffer) {
    printf("dxr3_mpeg_encoder: Couldn't allocate temp buffer for mpeg data\n");
    return 0;
  }

  /* set the dxr3 playmode */
  if (drv->enhanced_mode) {
    em8300_register_t regs;
    regs.microcode_register = 1;
    regs.reg = 0;
    regs.val = MVCOMMAND_SYNC;
    ioctl(drv->fd_control, EM8300_IOCTL_WRITEREG, &regs);
  }

  return 1;
}

static int lavc_on_display_frame(dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  int size;
  lavc_data_t* this = (lavc_data_t *)drv->enc;
  char tmpstr[128];
  ssize_t written;
	
  if (frame->vo_frame.bad_frame) return 1;
    /* ignore old frames */
  if ((frame->vo_frame.width != this->context->width) || (frame->oheight != this->context->height)) {
	frame->vo_frame.displayed(&frame->vo_frame);
    printf("LAVC ignoring frame !!!\n");
    return 1;
  }

  /* prepare frame for conversion, handles YUY2 -> YV12 conversion when necessary */
  lavc_prepare_frame(this, drv, frame);

  /* do the encoding */
  size = avcodec_encode_video(this->context, this->ffmpeg_buffer, DEFAULT_BUFFER_SIZE, this->picture);

  frame->vo_frame.displayed(&frame->vo_frame);
	
  if (drv->fd_video == CLOSED_FOR_ENCODER) {
      snprintf (tmpstr, sizeof(tmpstr), "%s_mv%s", drv->class->devname, drv->class->devnum);
      drv->fd_video = open(tmpstr, O_WRONLY | O_NONBLOCK);
    }
  if (drv->fd_video < 0) return 0;
      written = write(drv->fd_video, this->ffmpeg_buffer, size);
  if (written < 0) {
      printf("dxr3_mpeg_encoder: video device write failed (%s)\n",
           strerror(errno));
      return 0;
    }
  if (written != size)
      printf("dxr3_mpeg_encoder: Could only write %d of %d mpeg bytes.\n",
           written, size);
  return 1;
}

static int lavc_on_unneeded(dxr3_driver_t *drv)
{
  lavc_data_t *this = (lavc_data_t *)drv->enc;
#if LOG_ENC
  printf("dxr3_mpeg_encoder: flushing buffers\n");
#endif
  if (this->context) {
    avcodec_close(this->context);
    free(this->context);
    free(this->picture);
    this->context = NULL;
    this->picture = NULL;
  }
  return 1;
}
static int lavc_prepare_frame(lavc_data_t *this, dxr3_driver_t *drv, dxr3_frame_t *frame)
{
  int i, j, w2;
  uint8_t *yuy2;

  if (frame->vo_frame.bad_frame) return 1;

  if (frame->vo_frame.format == XINE_IMGFMT_YUY2) {
    /* need YUY2->YV12 conversion */
    if (!(this->out[0] && this->out[1] && this->out[2]) ) {
      printf("dxr3_mpeg_encoder: Internal YV12 buffer not created.\n");
      return 0;
    }
    this->picture->data[0] = this->out[0] +  frame->vo_frame.width      *  drv->top_bar;		/* y */
    this->picture->data[1] = this->out[1] + (frame->vo_frame.width / 2) * (drv->top_bar / 2);	/* u */
    this->picture->data[2] = this->out[2] + (frame->vo_frame.width / 2) * (drv->top_bar / 2);	/* v */
    yuy2 = frame->vo_frame.base[0];
    w2 = frame->vo_frame.width / 2;
    for (i = 0; i < frame->vo_frame.height; i += 2) {
      for (j = 0; j < w2; j++) {
        /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
        *(this->picture->data[0]++) = *(yuy2++);
        *(this->picture->data[1]++) = *(yuy2++);
        *(this->picture->data[0]++) = *(yuy2++);
        *(this->picture->data[2]++) = *(yuy2++);
      }
      /* down sampling */
      for (j = 0; j < w2; j++) {
        /* skip every second line for U and V */
        *(this->picture->data[0]++) = *(yuy2++);
        yuy2++;
        *(this->picture->data[0]++) = *(yuy2++);
        yuy2++;
      }
    }
    /* reset for encoder */
    this->picture->data[0] = this->out[0];
    this->picture->data[1] = this->out[1];
    this->picture->data[2] = this->out[2];
  }
  else { /* YV12 **/  
	this->picture->data[0] = frame->real_base[0];
    this->picture->data[1] = frame->real_base[1];
    this->picture->data[2] = frame->real_base[2];
  }
  this->picture->linesize[0] = this->context->width;
  this->picture->linesize[1] = this->context->width / 2;
  this->picture->linesize[2] = this->context->width / 2;
  return 1;
}
