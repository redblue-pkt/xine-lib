/* 
 * Copyright (C) 2001 the xine project
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
 * $Id: xine_decoder.c,v 1.17 2001/11/07 21:35:01 miguelfreitas Exp $
 *
 * xine decoder plugin using ffmpeg
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "xine_internal.h"
#include "cpu_accel.h"
#include "video_out.h"
#include "buffer.h"
#include "metronom.h"
#include "memcpy.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"

/* now this is ripped of wine's vfw.h */
typedef struct {
    long        biSize;
    long        biWidth;
    long        biHeight;
    short       biPlanes;
    short       biBitCount;
    long        biCompression;
    long        biSizeImage;
    long        biXPelsPerMeter;
    long        biYPelsPerMeter;
    long        biClrUsed;
    long        biClrImportant;
} BITMAPINFOHEADER;
#ifndef mmioFOURCC
#define mmioFOURCC( ch0, ch1, ch2, ch3 )                                         \
        ( (long)(unsigned char)(ch0) | ( (long)(unsigned char)(ch1) << 8 ) |     \
        ( (long)(unsigned char)(ch2) << 16 ) | ( (long)(unsigned char)(ch3) << 24 ) )
#endif

typedef struct ff_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t    *video_out;
  int               video_step;
  int               decoder_ok;

  BITMAPINFOHEADER  bih;
  long		    biWidth;
  long		    biHeight;
  unsigned char     buf[128*1024];
  int               size;

  AVPicture         av_picture;
  AVCodecContext    context;
} ff_decoder_t;


/*
#define IMGFMT_YUY2  mmioFOURCC('Y','U','Y','2')
#define IMGFMT_YV12  mmioFOURCC('Y','V','1','2')
*/

static unsigned long str2ulong(void *data)
{
  unsigned char *str = data;
  return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

/*
static unsigned short str2ushort(void *data)
{
  unsigned char *str = data;
  return ( str[0] | (str[1]<<8) );
}
*/

static int ff_can_handle (video_decoder_t *this_gen, int buf_type) {
  buf_type &= 0xFFFF0000;

  /* ffmpeg currently does not support MSMPEG4 v1/v2 */
  /* there's some problem with I263 too */
  return ( buf_type == BUF_VIDEO_MSMPEG4_V3 ||
           /* buf_type == BUF_VIDEO_MSMPEG4_V12 || */
           buf_type == BUF_VIDEO_MPEG4 ||
           buf_type == BUF_VIDEO_MJPEG ||
	   /* buf_type == BUF_VIDEO_I263 || */
	   buf_type == BUF_VIDEO_RV10 ||
	   buf_type == BUF_VIDEO_JPEG);
}

static void ff_init (video_decoder_t *this_gen, vo_instance_t *video_out) {

  ff_decoder_t *this = (ff_decoder_t *) this_gen;

  this->video_out  = video_out;
  this->decoder_ok = 0;
}

static void ff_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  ff_decoder_t *this = (ff_decoder_t *) this_gen;

  /*
  printf ("ffmpeg: processing packet type = %08x, buf : %d, buf->decoder_info[0]=%d\n", 
	  buf->type, buf, buf->decoder_info[0]);
  */

  if (buf->decoder_info[0] == 0) {

    AVCodec *codec = NULL;
    int codec_type;

    /* init package containing bih */

    memcpy ( &this->bih, buf->content, sizeof (BITMAPINFOHEADER));
    this->biWidth = str2ulong(&this->bih.biWidth);
    this->biHeight = str2ulong(&this->bih.biHeight);
    this->video_step = buf->decoder_info[1];

    /* init codec */

    codec_type = buf->type & 0xFFFF0000;

    /*
    if (str2ulong(&this->bih.biCompression) == mmioFOURCC('D', 'I', 'V', 'X')) {
      printf ("ffmpeg: mpeg4 (opendivx) format detected\n");

      codec = avcodec_find_decoder (CODEC_ID_OPENDIVX);
    } else {
      printf ("ffmpeg: ms mpeg4 format detected\n");
      codec = avcodec_find_decoder (CODEC_ID_MSMPEG4);
    }
    */

    switch (buf->type & 0xFFFF0000) {
    case BUF_VIDEO_MSMPEG4_V12:
    case BUF_VIDEO_MSMPEG4_V3:
      codec = avcodec_find_decoder (CODEC_ID_MSMPEG4);
      break;
    case BUF_VIDEO_MPEG4 :
      codec = avcodec_find_decoder (CODEC_ID_MPEG4);
      break;
    case BUF_VIDEO_JPEG:
    case BUF_VIDEO_MJPEG:
      codec = avcodec_find_decoder (CODEC_ID_MJPEG);
      break;
    case BUF_VIDEO_I263:
      codec = avcodec_find_decoder (CODEC_ID_H263);
      break;
    case BUF_VIDEO_RV10:
      codec = avcodec_find_decoder (CODEC_ID_RV10);
      break;
    default:
      printf ("ffmpeg: unknown video format (buftype: 0x%08X)\n",
	      buf->type & 0xFFFF0000);
    }

    if (!codec) {
      printf ("ffmpeg: couldn't find decoder\n");
      return;
    }

    memset(&this->context, 0, sizeof(this->context));
    this->context.width = this->biWidth;
    this->context.height = this->biHeight;

    if (avcodec_open (&this->context, codec) < 0) {
      printf ("ffmpeg: couldn't open decoder\n");
      return;
    }

    this->decoder_ok = 1;
    this->video_out->open (this->video_out);
    
  } else if (this->decoder_ok) {

    fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_info[0] == 2)  {

      vo_frame_t *img;
      int         got_picture, len, y;
      uint8_t    *dy, *du, *dv, *sy, *su, *sv;

      /* decoder video frame */

      /* this->bih.biSizeImage = this->size; */

      len = avcodec_decode_video (&this->context, &this->av_picture,
				  &got_picture, this->buf,
				  this->size);
#ifdef ARCH_X86
      emms_c ();
#endif

      img = this->video_out->get_frame (this->video_out,
					/* this->av_picture.linesize[0],  */
					this->biWidth,
					this->biHeight,
					42, 
					IMGFMT_YV12,
					this->video_step,
					VO_BOTH_FIELDS);

      img->PTS = buf->PTS;
      if (len<0) {
	printf ("ffmpeg: error decompressing frame\n");
	img->bad_frame = 1;
      } else {
	img->bad_frame = 0;

	dy = img->base[0];
	du = img->base[1];
	dv = img->base[2];
	sy = this->av_picture.data[0];
	su = this->av_picture.data[1];
	sv = this->av_picture.data[2];

	for (y=0; y<this->biHeight; y++) {
	  
	  fast_memcpy (dy, sy, this->biWidth);
	  
	  dy += this->biWidth;
	  
	  sy += this->av_picture.linesize[0];
	}
	
	for (y=0; y<(this->biHeight/2); y++) {

	  if (this->context.pix_fmt != PIX_FMT_YUV444P) {
	  
	    fast_memcpy (du, su, this->biWidth/2);
	    fast_memcpy (dv, sv, this->biWidth/2);

	  } else {

	    int x;
	    uint8_t *src;
	    uint8_t *dst;
	    
	    /* subsample */

	    src = su; dst = du;
	    for (x=0; x<(this->biWidth/2); x++) {
	      *dst = *src;
	      dst++;
	      src += 2;
	    }
	    src = sv; dst = dv;
	    for (x=0; x<(this->biWidth/2); x++) {
	      *dst = *src;
	      dst++;
	      src += 2;
	    }

	  }
	  
	  du += this->biWidth/2;
	  dv += this->biWidth/2;
	  
	  if (this->context.pix_fmt != PIX_FMT_YUV420P) {
	    su += 2*this->av_picture.linesize[1];
	    sv += 2*this->av_picture.linesize[2];
	  } else {
	    su += this->av_picture.linesize[1];
	    sv += this->av_picture.linesize[2];
	  }
	}
	
	if (img->copy) {
	  
	  int height = abs(this->biHeight);
	  int stride = this->biWidth;
	  uint8_t* src[3];
	  
	  src[0] = img->base[0];
	  src[1] = img->base[1];
	  src[2] = img->base[2];
	  while ((height -= 16) >= 0) {
	    img->copy(img, src);
	    src[0] += 16 * stride;
	    src[1] +=  4 * stride;
	    src[2] +=  4 * stride;
	  }
	}
      }
      img->draw(img);
      img->free(img);
      
      this->size = 0;
    }
  }
}

static void ff_close (video_decoder_t *this_gen) {

  ff_decoder_t *this = (ff_decoder_t *) this_gen;

  if (this->decoder_ok) {
    avcodec_close (&this->context);

    this->video_out->close(this->video_out);
    this->decoder_ok = 0;
  }
}

static char *ff_get_id(void) {
  return "ffmpeg video decoder";
}


video_decoder_t *init_video_decoder_plugin (int iface_version, config_values_t *cfg) {

  ff_decoder_t *this ;

  if (iface_version != 2) {
    printf( "ffmpeg: plugin doesn't support plugin API version %d.\n"
	    "ffmpeg: this means there's a version mismatch between xine and this "
	    "ffmpeg: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    
    return NULL;
  }

  this = (ff_decoder_t *) malloc (sizeof (ff_decoder_t));

  this->video_decoder.interface_version   = 2;
  this->video_decoder.can_handle          = ff_can_handle;
  this->video_decoder.init                = ff_init;
  this->video_decoder.decode_data         = ff_decode_data;
  this->video_decoder.close               = ff_close;
  this->video_decoder.get_identifier      = ff_get_id;
  this->video_decoder.priority            = 5;
  this->size				  = 0;

  avcodec_init();
  avcodec_register_all();

  return (video_decoder_t *) this;
}


