/* 
 * Copyright (C) 2001-2002 the xine project
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
 * $Id: xine_decoder.c,v 1.49 2002/07/15 21:42:33 esnel Exp $
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
#include <pthread.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "metronom.h"
#include "xineutils.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"

/*
#define LOG
*/

typedef struct ff_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t    *video_out;
  int               video_step;
  int               decoder_ok;

  xine_bmiheader    bih;
  unsigned char     *buf;
  int               bufsize;
  int               size;
  int               skipframes;
  int               illegal_vlc;

  AVPicture         av_picture;
  AVCodecContext    context;
} ff_decoder_t;

#define VIDEOBUFSIZE 128*1024

static int ff_can_handle (video_decoder_t *this_gen, int buf_type) {
  buf_type &= 0xFFFF0000;

  return ( buf_type == BUF_VIDEO_MSMPEG4_V3 ||
           buf_type == BUF_VIDEO_MSMPEG4_V2 ||
           buf_type == BUF_VIDEO_MSMPEG4_V1 ||
           buf_type == BUF_VIDEO_WMV7 ||
           buf_type == BUF_VIDEO_MPEG4 ||
           buf_type == BUF_VIDEO_XVID  ||
           buf_type == BUF_VIDEO_DIVX5 ||
           buf_type == BUF_VIDEO_MJPEG ||
	   /* buf_type == BUF_VIDEO_I263 || */
	   buf_type == BUF_VIDEO_H263 ||
	   buf_type == BUF_VIDEO_RV10 ||
	   /* PIX_FMT_YUV410P must be supported to enable svq1 */
	   /* buf_type == BUF_VIDEO_SORENSON_V1 || */
	   buf_type == BUF_VIDEO_JPEG);
}

static void ff_init (video_decoder_t *this_gen, vo_instance_t *video_out) {

  ff_decoder_t *this = (ff_decoder_t *) this_gen;

  this->video_out  = video_out;
  this->decoder_ok = 0;
  this->buf = NULL;
}

static void ff_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  ff_decoder_t *this = (ff_decoder_t *) this_gen;
  int ratio;
  
#ifdef LOG
  printf ("ffmpeg: processing packet type = %08x, buf : %d, buf->decoder_flags=%08x\n", 
	  buf->type, buf, buf->decoder_flags);
#endif

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) {

    AVCodec *codec = NULL;
    int codec_type;

    /* init package containing bih */

    memcpy ( &this->bih, buf->content, sizeof (xine_bmiheader));
    this->video_step = buf->decoder_info[1];

    /* init codec */

    codec_type = buf->type & 0xFFFF0000;

    switch (codec_type) {
    case BUF_VIDEO_MSMPEG4_V1:
      codec = avcodec_find_decoder (CODEC_ID_MSMPEG4V1);
      break;
    case BUF_VIDEO_MSMPEG4_V2:
      codec = avcodec_find_decoder (CODEC_ID_MSMPEG4V2);
      break;
    case BUF_VIDEO_MSMPEG4_V3:
      codec = avcodec_find_decoder (CODEC_ID_MSMPEG4);
      break;
    case BUF_VIDEO_WMV7:
      codec = avcodec_find_decoder (CODEC_ID_WMV1);
      break;
    case BUF_VIDEO_MPEG4 :
    case BUF_VIDEO_XVID :
    case BUF_VIDEO_DIVX5 :
      codec = avcodec_find_decoder (CODEC_ID_MPEG4);
      break;
    case BUF_VIDEO_JPEG:
    case BUF_VIDEO_MJPEG:
      codec = avcodec_find_decoder (CODEC_ID_MJPEG);
      break;
    case BUF_VIDEO_I263:
      codec = avcodec_find_decoder (CODEC_ID_H263I);
      break;
    case BUF_VIDEO_H263:
      codec = avcodec_find_decoder (CODEC_ID_H263);
      break;
    case BUF_VIDEO_RV10:
      codec = avcodec_find_decoder (CODEC_ID_RV10);
      break;
    case BUF_VIDEO_SORENSON_V1:
      codec = avcodec_find_decoder (CODEC_ID_SVQ1);
      break;
    default:
      printf ("ffmpeg: unknown video format (buftype: 0x%08X)\n",
	      buf->type & 0xFFFF0000);
    }

    if (!codec) {
      printf ("ffmpeg: couldn't find decoder\n");
      return;
    }
 
    /* force (width % 8 == 0), otherwise there will be 
     * display problems with Xv.
     */
    this->bih.biWidth = (this->bih.biWidth + 7) & (~7);
  
    memset(&this->context, 0, sizeof(this->context));
    this->context.width = this->bih.biWidth;
    this->context.height = this->bih.biHeight;
    
    if (avcodec_open (&this->context, codec) < 0) {
      printf ("ffmpeg: couldn't open decoder\n");
      return;
    }

    this->decoder_ok = 1;
    this->video_out->open (this->video_out);

    /* needed to play streams generated by MS ISO MPEG4 codec. 
       Michael Niedermayer explained:
       M$ "ISO MPEG4" uses illegal vlc code combinations, a ISO MPEG4 compliant
       decoder which support error resilience should handle them like errors.
    */
    if( this->illegal_vlc )
      this->context.error_resilience=-1;
    
    if( this->buf )
      free( this->buf );
    
    this->buf = malloc( VIDEOBUFSIZE );
    this->bufsize = VIDEOBUFSIZE;
    
    this->skipframes = 0;
    
  } else if (this->decoder_ok) {

    if( this->size + buf->size > this->bufsize ) {
      this->bufsize = this->size + 2 * buf->size;
      printf("ffmpeg: increasing source buffer to %d to avoid overflow.\n", 
        this->bufsize);
      this->buf = realloc( this->buf, this->bufsize );
    }
    
    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
      this->video_step = buf->decoder_info[0];

    if (buf->decoder_flags & BUF_FLAG_FRAME_END)  {

      vo_frame_t *img;
      int         got_picture, len, y;
      uint8_t    *dy, *du, *dv, *sy, *su, *sv;

      /* decode video frame */

      /* skip decoding b frames if too late */
      this->context.hurry_up = (this->skipframes > 2) ? 1:0;

      len = avcodec_decode_video (&this->context, &this->av_picture,
				  &got_picture, this->buf,
				  this->size);
#ifdef ARCH_X86
      emms_c ();
#endif

      switch(this->context.aspect_ratio_info) {
      case FF_ASPECT_SQUARE:
        ratio = XINE_ASPECT_RATIO_SQUARE;
        break;
      case FF_ASPECT_4_3_625:
      case FF_ASPECT_4_3_525:
        ratio = XINE_ASPECT_RATIO_4_3;
        break;
      case FF_ASPECT_16_9_625:
      case FF_ASPECT_16_9_525:
        ratio = XINE_ASPECT_RATIO_ANAMORPHIC;
        break;
      default:
        ratio = XINE_ASPECT_RATIO_DONT_TOUCH;
      }

      img = this->video_out->get_frame (this->video_out,
					/* this->av_picture.linesize[0],  */
					this->bih.biWidth,
					this->bih.biHeight,
					ratio, 
					IMGFMT_YV12,
					VO_BOTH_FIELDS);

      img->pts      = buf->pts;
      img->duration = this->video_step;
      if (len<0 || this->skipframes) {
	if( !this->skipframes )
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

	for (y=0; y<this->bih.biHeight; y++) {
	  
	  xine_fast_memcpy (dy, sy, this->bih.biWidth);
	  
	  dy += img->pitches[0];
	  
	  sy += this->av_picture.linesize[0];
	}
	
	for (y=0; y<(this->bih.biHeight/2); y++) {

	  if (this->context.pix_fmt != PIX_FMT_YUV444P) {
	  
	    xine_fast_memcpy (du, su, this->bih.biWidth/2);
	    xine_fast_memcpy (dv, sv, this->bih.biWidth/2);

	  } else {

	    int x;
	    uint8_t *src;
	    uint8_t *dst;
	    
	    /* subsample */

	    src = su; dst = du;
	    for (x=0; x<(this->bih.biWidth/2); x++) {
	      *dst = *src;
	      dst++;
	      src += 2;
	    }
	    src = sv; dst = dv;
	    for (x=0; x<(this->bih.biWidth/2); x++) {
	      *dst = *src;
	      dst++;
	      src += 2;
	    }

	  }
	  
	  du += img->pitches[1];
	  dv += img->pitches[2];
	  
	  if (this->context.pix_fmt != PIX_FMT_YUV420P) {
	    su += 2*this->av_picture.linesize[1];
	    sv += 2*this->av_picture.linesize[2];
	  } else {
	    su += this->av_picture.linesize[1];
	    sv += this->av_picture.linesize[2];
	  }
	}
	
	if (img->copy) {
	  int height = img->height;
	  uint8_t *src[3];

	  src[0] = img->base[0];
	  src[1] = img->base[1];
	  src[2] = img->base[2];

	  while ((height -= 16) >= 0) {
	    img->copy(img, src);
	    src[0] += 16 * img->pitches[0];
	    src[1] +=  8 * img->pitches[1];
	    src[2] +=  8 * img->pitches[2];
	  }
	}
      }
      
      this->skipframes = img->draw(img);
      if( this->skipframes < 0 )
        this->skipframes = 0;
      img->free(img);
      
      this->size = 0;
    }
  }
}

static void ff_flush (video_decoder_t *this_gen) {

}

static void ff_reset (video_decoder_t *this_gen) {
  /* seems to handle seeking quite nicelly without any code here */
}

static void ff_close (video_decoder_t *this_gen) {

  ff_decoder_t *this = (ff_decoder_t *) this_gen;

  if (this->decoder_ok) {
    avcodec_close (&this->context);

    this->video_out->close(this->video_out);
    this->decoder_ok = 0;
  }
  
  if (this->buf)
    free(this->buf);
  this->buf = NULL;
}

static char *ff_get_id(void) {
  return "ffmpeg video decoder";
}

void avcodec_register_all(void)
{
    static int inited = 0;
    
    if (inited != 0)
	return;
    inited = 1;

    /* decoders */
    register_avcodec(&h263_decoder);
    register_avcodec(&mpeg4_decoder);
    register_avcodec(&msmpeg4v1_decoder);
    register_avcodec(&msmpeg4v2_decoder);
    register_avcodec(&msmpeg4v3_decoder);
    register_avcodec(&wmv1_decoder);
    register_avcodec(&wmv2_decoder);
    register_avcodec(&mpeg_decoder);
    register_avcodec(&h263i_decoder);
    register_avcodec(&rv10_decoder);
    register_avcodec(&svq1_decoder);
    register_avcodec(&mjpeg_decoder);
}

static void init_routine(void) {
  avcodec_init();
  avcodec_register_all();
}

static void ff_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

video_decoder_t *init_video_decoder_plugin (int iface_version, xine_t *xine) {

  ff_decoder_t *this ;
  static pthread_once_t once_control = PTHREAD_ONCE_INIT;

  if (iface_version != 10) {
    printf(_("ffmpeg: plugin doesn't support plugin API version %d.\n"
	     "ffmpeg: this means there's a version mismatch between xine and this "
	     "ffmpeg: decoder plugin.\nInstalling current plugins should help.\n"),
	     iface_version);
    
    return NULL;
  }

  this = (ff_decoder_t *) malloc (sizeof (ff_decoder_t));

  this->video_decoder.interface_version   = iface_version;
  this->video_decoder.can_handle          = ff_can_handle;
  this->video_decoder.init                = ff_init;
  this->video_decoder.decode_data         = ff_decode_data;
  this->video_decoder.flush               = ff_flush;
  this->video_decoder.reset               = ff_reset;
  this->video_decoder.close               = ff_close;
  this->video_decoder.get_identifier      = ff_get_id;
  this->video_decoder.priority            = 5;
  this->video_decoder.dispose             = ff_dispose;
  this->size				  = 0;

  this->illegal_vlc = xine->config->register_bool (xine->config, "codec.ffmpeg_illegal_vlc", 1,
                      _("allow illegal vlc codes in mpeg4 streams"), NULL, NULL, NULL);

  pthread_once( &once_control, init_routine );

  return (video_decoder_t *) this;
}


