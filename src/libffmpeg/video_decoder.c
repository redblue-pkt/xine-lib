/*
 * Copyright (C) 2001-2004 the xine project
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
 * $Id: video_decoder.c,v 1.10 2004/03/09 04:09:41 tmmm Exp $
 *
 * xine video decoder plugin using ffmpeg
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
#include <math.h>
#include <assert.h>

#define LOG_MODULE "ffmpeg_video_dec"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine_internal.h"
#include "bswap.h"
#include "buffer.h"
#include "xineutils.h"
#include "xine_decoder.h"

#include "libavcodec/libpostproc/postprocess.h"

#define VIDEOBUFSIZE        (128*1024)
#define SLICE_BUFFER_SIZE   (1194*1024)

#define SLICE_OFFSET_SIZE   128

#define ENABLE_DIRECT_RENDERING

typedef struct ff_video_decoder_s ff_video_decoder_t;

typedef struct ff_video_class_s {
  video_decoder_class_t   decoder_class;

  ff_video_decoder_t     *ip;
  xine_t                 *xine;
} ff_video_class_t;

struct ff_video_decoder_s {
  video_decoder_t   video_decoder;

  ff_video_class_t *class;

  xine_stream_t    *stream;
  int               video_step;
  int               decoder_ok;

  xine_bmiheader    bih;
  unsigned char     *buf;
  int               bufsize;
  int               size;
  int               skipframes;
  
  int               slice_offset_size;

  AVFrame           *av_frame;
  AVCodecContext    *context;
  AVCodec           *codec;
  
  int                pp_available;
  int                pp_quality;
  int                pp_quality_changed;
  int                pp_flags;
  pp_context_t      *pp_context;
  pp_mode_t         *pp_mode;

  /* mpeg sequence header parsing, stolen from libmpeg2 */

  uint32_t          shift;
  uint8_t          *chunk_buffer;
  uint8_t          *chunk_ptr;
  uint8_t           code;

  int               is_continous;

  double            aspect_ratio;
  int               frame_flags;
  
  int               output_format;
  yuv_planes_t      yuv;
  AVPaletteControl  palette_control;
};


#ifdef ENABLE_DIRECT_RENDERING
/* called from ffmpeg to do direct rendering method 1 */
static int get_buffer(AVCodecContext *context, AVFrame *av_frame){
  ff_video_decoder_t * this = (ff_video_decoder_t *)context->opaque;
  vo_frame_t *img;
  int align, width, height;
        
  align=15;
    
  width  = (context->width +align)&~align;
  height = (context->height+align)&~align;

  if( (this->context->pix_fmt != PIX_FMT_YUV420P) ||
      (width != context->width) || (height != context->height) ) {
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG, 
            _("ffmpeg_video_dec: unsupported frame format, DR1 disabled.\n"));

    this->context->get_buffer = avcodec_default_get_buffer;
    this->context->release_buffer = avcodec_default_release_buffer;
    return avcodec_default_get_buffer(context, av_frame);
  }

  img = this->stream->video_out->get_frame (this->stream->video_out,
                                            width,
                                            height,
                                            this->aspect_ratio, 
                                            this->output_format,
                                            VO_BOTH_FIELDS|this->frame_flags);

  av_frame->opaque = img;

  av_frame->data[0]= img->base[0];
  av_frame->data[1]= img->base[1];
  av_frame->data[2]= img->base[2];

  av_frame->linesize[0] = img->pitches[0];
  av_frame->linesize[1] = img->pitches[1];
  av_frame->linesize[2] = img->pitches[2];

  /* We should really keep track of the ages of xine frames (see
   * avcodec_default_get_buffer in libavcodec/utils.c)
   * For the moment tell ffmpeg that every frame is new (age = bignumber) */
  av_frame->age = 256*256*256*64;

  av_frame->type= FF_BUFFER_TYPE_USER;

  return 0;
}

static void release_buffer(struct AVCodecContext *context, AVFrame *av_frame){
  vo_frame_t *img = (vo_frame_t *)av_frame->opaque;
    
  assert(av_frame->type == FF_BUFFER_TYPE_USER);
  assert(av_frame->opaque);  
  
  av_frame->data[0]= NULL;
  av_frame->data[1]= NULL;
  av_frame->data[2]= NULL;
  
  img->free(img);

  av_frame->opaque = NULL;
}
#endif

static void init_video_codec (ff_video_decoder_t *this, xine_bmiheader *bih) {

  /* force (width % 8 == 0), otherwise there will be 
   * display problems with Xv. 
   */ 
  this->bih.biWidth = (this->bih.biWidth + 1) & (~1);

  this->context->width = this->bih.biWidth;
  this->context->height = this->bih.biHeight;
  this->context->stream_codec_tag = this->context->codec_tag = 
    _x_stream_info_get(this->stream, XINE_STREAM_INFO_VIDEO_FOURCC);

  /* some decoders (eg. dv) do not know the pix_fmt until they decode the
   * first frame. setting to -1 avoid enabling DR1 for them.
   */
  this->context->pix_fmt = -1;

  this->context->palctrl = &this->palette_control;

  if( bih && bih->biSize > sizeof(xine_bmiheader) ) {
    this->context->extradata_size = bih->biSize - sizeof(xine_bmiheader);
    this->context->extradata = malloc(this->context->extradata_size);
    memcpy( this->context->extradata, 
            (uint8_t *)bih + sizeof(xine_bmiheader),
            this->context->extradata_size ); 
  }

  if(bih)
    this->context->bits_per_sample = bih->biBitCount;
    
  /* Some codecs (eg rv10) copy flags in init so it's necessary to set
   * this flag here in case we are going to use direct rendering */
  if(this->codec->capabilities & CODEC_CAP_DR1)
    this->context->flags |= CODEC_FLAG_EMU_EDGE;

  if (avcodec_open (this->context, this->codec) < 0) {
    xprintf (this->stream->xine, XINE_VERBOSITY_LOG, 
             _("ffmpeg_video_dec: couldn't open decoder\n"));
    free(this->context);
    this->context = NULL;
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
    return;
  }

  this->decoder_ok = 1;
  
  this->aspect_ratio = (double)this->bih.biWidth / (double)this->bih.biHeight;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  this->context->width);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->context->height);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->aspect_ratio*10000);

  this->stream->video_out->open (this->stream->video_out, this->stream);

  if (this->buf)
    free (this->buf);
    
  this->buf = xine_xmalloc (VIDEOBUFSIZE);
  this->bufsize = VIDEOBUFSIZE;
  
  this->skipframes = 0;
  
  if((this->context->pix_fmt == PIX_FMT_RGBA32) ||
     (this->context->pix_fmt == PIX_FMT_RGB565) ||
     (this->context->pix_fmt == PIX_FMT_RGB555) ||
     (this->context->pix_fmt == PIX_FMT_BGR24) ||
     (this->context->pix_fmt == PIX_FMT_RGB24) ||
     (this->context->pix_fmt == PIX_FMT_PAL8)) {
    this->output_format = XINE_IMGFMT_YUY2;
    init_yuv_planes(&this->yuv, this->context->width, this->context->height);
  } else {
    this->output_format = XINE_IMGFMT_YV12;
#ifdef ENABLE_DIRECT_RENDERING
    if( this->context->pix_fmt == PIX_FMT_YUV420P &&
        this->codec->capabilities & CODEC_CAP_DR1 ) {
      this->context->get_buffer = get_buffer;
      this->context->release_buffer = release_buffer;
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, 
              _("ffmpeg_video_dec: direct rendering enabled\n"));
    }
#endif
  }
}

static void pp_quality_cb(void *user_data, xine_cfg_entry_t *entry) {
  ff_video_class_t   *class = (ff_video_class_t *) user_data;
  
  if(class->ip) {
    ff_video_decoder_t *this  = class->ip;
    
    if(this->pp_available) {
      this->pp_quality = entry->num_value;
      this->pp_quality_changed = 1;
    }
  }
}

static void pp_change_quality (ff_video_decoder_t *this) {
  if(this->pp_available && this->pp_quality) {
    if(!this->pp_context)
      this->pp_context = pp_get_context(this->context->width, this->context->height,
                                        this->pp_flags);
    if(this->pp_mode)
      pp_free_mode(this->pp_mode);
      
    this->pp_mode = pp_get_mode_by_name_and_quality("hb:a,vb:a,dr:a", 
                                                    this->pp_quality);
  } else {
    if(this->pp_mode) {
      pp_free_mode(this->pp_mode);
      this->pp_mode = NULL;
    }
    
    if(this->pp_context) {
      pp_free_context(this->pp_context);
      this->pp_context = NULL;
    }
  }
  
  this->pp_quality_changed = 0;
}

static void init_postprocess (ff_video_decoder_t *this) {
  uint32_t cpu_caps;
  xine_cfg_entry_t quality_entry;

  /* Read quality from config */
  if(xine_config_lookup_entry(this->class->xine, "codec.ffmpeg_pp_quality",
                              &quality_entry))
    this->pp_quality = quality_entry.num_value;
  else
    this->pp_quality = 0;
  
  /* Allow post processing on mpeg-4 (based) codecs */
  switch(this->codec->id) {
    case CODEC_ID_MPEG4:
    case CODEC_ID_MSMPEG4V1:
    case CODEC_ID_MSMPEG4V2:
    case CODEC_ID_MSMPEG4V3:
    case CODEC_ID_WMV1:
    case CODEC_ID_WMV2:
      this->pp_available = 1;
      break;
    default:
      this->pp_available = 0;
      break;
  }
  
  /* Detect what cpu accel we have */
  cpu_caps = xine_mm_accel();
  this->pp_flags = PP_FORMAT_420;
 
  if(cpu_caps & MM_ACCEL_X86_MMX)
    this->pp_flags |= PP_CPU_CAPS_MMX;
    
  if(cpu_caps & MM_ACCEL_X86_MMXEXT)
    this->pp_flags |= PP_CPU_CAPS_MMX2;
  
  if(cpu_caps & MM_ACCEL_X86_3DNOW)  
    this->pp_flags |= PP_CPU_CAPS_3DNOW;
   
  /* Set level */
  pp_change_quality(this);    
}

static void find_sequence_header (ff_video_decoder_t *this,
                                  uint8_t * current, uint8_t * end){

  uint8_t code;

  if (this->decoder_ok)
    return;

  while (current != end) {

    uint32_t shift;
    uint8_t *chunk_ptr;
    uint8_t *limit;
    uint8_t  byte;
    
    code = this->code;
    
    /* copy chunk */
    
    shift     = this->shift;
    chunk_ptr = this->chunk_ptr;
    limit     = current + (this->chunk_buffer + SLICE_BUFFER_SIZE - chunk_ptr);
    if (limit > end)
      limit = end;
    
    while (1) {
      
      byte = *current++;
      if (shift != 0x00000100) {
        shift = (shift | byte) << 8;
        *chunk_ptr++ = byte;
        if (current < limit)
          continue;
        if (current == end) {
          this->chunk_ptr = chunk_ptr;
          this->shift = shift;
          current = 0;
          break;
        } else {
          /* we filled the chunk buffer without finding a start code */
          this->code = 0xb4;    /* sequence_error_code */
          this->chunk_ptr = this->chunk_buffer;
          break;
        }
      }
      this->code = byte;
      this->chunk_ptr = this->chunk_buffer;
      this->shift = 0xffffff00;
      break;
    }

    if (current == NULL)
      return ;

    lprintf ("looking for sequence header... %02x\n", code);
  
    /* mpeg2_stats (code, this->chunk_buffer); */
    
    if (code == 0xb3) { /* sequence_header_code */

      int width, height, frame_rate_code;

      lprintf ("found sequence header !\n");

      height = (this->chunk_buffer[0] << 16) | (this->chunk_buffer[1] << 8) 
        | this->chunk_buffer[2];

      width = ((height >> 12) + 15) & ~15;
      height = ((height & 0xfff) + 15) & ~15;

      this->bih.biWidth  = width;
      this->bih.biHeight = height;

      frame_rate_code = this->chunk_buffer[3] & 15;

      switch (frame_rate_code) {
      case 1: /* 23.976 fps */
        this->video_step      = 3754;  /* actually it's 3753.75 */
        break;
      case 2: /* 24 fps */
        this->video_step      = 3750;
        break;
      case 3: /* 25 fps */
        this->video_step      = 3600;
        break;
      case 4: /* 29.97 fps */
        this->video_step      = 3003;
        break;
      case 5: /* 30 fps */
        this->video_step      = 3000;
        break;
      case 6: /* 50 fps */
        this->video_step      = 1800;
        break;
      case 7: /* 59.94 fps */
        this->video_step      = 1502;  /* actually it's 1501.5 */
        break;
      case 8: /* 60 fps */
        this->video_step      = 1500;
        break;
      default:
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG, 
                _("ffmpeg_video_dec: invalid/unknown frame rate code: %d \n"),
                frame_rate_code); 
        this->video_step      = 0;
      }

      _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, 
        "mpeg-1 (ffmpeg)");

      /*
       * init codec
       */

      this->codec = avcodec_find_decoder (CODEC_ID_MPEG1VIDEO); 
      if (!this->codec) {
        xprintf (this->stream->xine, XINE_VERBOSITY_LOG, 
                 _("avcodec_find_decoder (CODEC_ID_MPEG1VIDEO) failed.\n"));
        _x_abort();
      }

      this->is_continous = 1;
      init_video_codec (this, NULL);
    }
  }
}

static void ff_convert_frame(ff_video_decoder_t *this, vo_frame_t *img) {
  int         y;
  uint8_t    *dy, *du, *dv, *sy, *su, *sv;

  dy = img->base[0];
  du = img->base[1];
  dv = img->base[2];
  sy = this->av_frame->data[0];
  su = this->av_frame->data[1];
  sv = this->av_frame->data[2];

  if (this->context->pix_fmt == PIX_FMT_YUV410P) {

    yuv9_to_yv12(
     /* Y */
      this->av_frame->data[0],
      this->av_frame->linesize[0],
      img->base[0],
      img->pitches[0],
     /* U */
      this->av_frame->data[1],
      this->av_frame->linesize[1],
      img->base[1],
      img->pitches[1],
     /* V */
      this->av_frame->data[2],
      this->av_frame->linesize[2],
      img->base[2],
      img->pitches[2],
     /* width x height */
      this->context->width,
      this->context->height);

  } else if (this->context->pix_fmt == PIX_FMT_YUV411P) {

    yuv411_to_yv12(
     /* Y */
      this->av_frame->data[0],
      this->av_frame->linesize[0],
      img->base[0],
      img->pitches[0],
     /* U */
      this->av_frame->data[1],
      this->av_frame->linesize[1],
      img->base[1],
      img->pitches[1],
     /* V */
      this->av_frame->data[2],
      this->av_frame->linesize[2],
      img->base[2],
      img->pitches[2],
     /* width x height */
      this->context->width,
      this->context->height);

  } else if (this->context->pix_fmt == PIX_FMT_RGBA32) {
          
    int x, plane_ptr = 0;
    uint8_t *src;
            
    for(y = 0; y < this->context->height; y++) {
      src = sy;
      for(x = 0; x < this->context->width; x++) {
        uint8_t r, g, b;
              
        /* These probably need to be switched for big endian */
        b = *src; src++;
        g = *src; src++;
        r = *src; src += 2;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }
            
    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);
          
  } else if (this->context->pix_fmt == PIX_FMT_RGB565) {

    int x, plane_ptr = 0;
    uint8_t *src;
    uint16_t pixel16;

    for(y = 0; y < this->context->height; y++) {
      src = sy;
      for(x = 0; x < this->context->width; x++) {
        uint8_t r, g, b;
              
        /* a 16-bit RGB565 pixel is supposed to be stored in native-endian
         * byte order; the following should be endian-safe */
        pixel16 = *((uint16_t *)src);
        src += 2;
        b = (pixel16 << 3) & 0xFF;
        g = (pixel16 >> 3) & 0xFF;
        r = (pixel16 >> 8) & 0xFF;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }
            
    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);
          
  } else if (this->context->pix_fmt == PIX_FMT_RGB555) {
          
    int x, plane_ptr = 0;
    uint8_t *src;
    uint16_t pixel16;
            
    for(y = 0; y < this->context->height; y++) {
      src = sy;
      for(x = 0; x < this->context->width; x++) {
        uint8_t r, g, b;
              
        /* a 16-bit RGB555 pixel is supposed to be stored in native-endian
         * byte order; the following should be endian-safe */
        pixel16 = *((uint16_t *)src);
        src += 2;
        b = (pixel16 << 3) & 0xFF;
        g = (pixel16 >> 2) & 0xFF;
        r = (pixel16 >> 7) & 0xFF;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }
            
    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);
          
  } else if (this->context->pix_fmt == PIX_FMT_BGR24) {

    int x, plane_ptr = 0;
    uint8_t *src;

    for(y = 0; y < this->context->height; y++) {
      src = sy;
      for(x = 0; x < this->context->width; x++) {
        uint8_t r, g, b;
              
        b = *src++;
        g = *src++;
        r = *src++;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }
            
    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);
          
  } else if (this->context->pix_fmt == PIX_FMT_RGB24) {

    int x, plane_ptr = 0;
    uint8_t *src;

    for(y = 0; y < this->context->height; y++) {
      src = sy;
      for(x = 0; x < this->context->width; x++) {
        uint8_t r, g, b;
              
        r = *src++;
        g = *src++;
        b = *src++;

        this->yuv.y[plane_ptr] = COMPUTE_Y(r, g, b);
        this->yuv.u[plane_ptr] = COMPUTE_U(r, g, b);
        this->yuv.v[plane_ptr] = COMPUTE_V(r, g, b);
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }
            
    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);
          
  } else if (this->context->pix_fmt == PIX_FMT_PAL8) {
          
    int x, plane_ptr = 0;
    uint8_t *src;
    uint8_t pixel;
    uint32_t *palette32 = (uint32_t *)su;  /* palette is in data[1] */
    uint32_t rgb_color;
    uint8_t r, g, b;
    uint8_t y_palette[256];
    uint8_t u_palette[256];
    uint8_t v_palette[256];

    for (x = 0; x < 256; x++) {
      rgb_color = palette32[x];
      b = rgb_color & 0xFF;
      rgb_color >>= 8;
      g = rgb_color & 0xFF;
      rgb_color >>= 8;
      r = rgb_color & 0xFF;
      y_palette[x] = COMPUTE_Y(r, g, b);
      u_palette[x] = COMPUTE_U(r, g, b);
      v_palette[x] = COMPUTE_V(r, g, b);
    }

    for(y = 0; y < this->context->height; y++) {
      src = sy;
      for(x = 0; x < this->context->width; x++) {
        pixel = *src++;

        this->yuv.y[plane_ptr] = y_palette[pixel];
        this->yuv.u[plane_ptr] = u_palette[pixel];
        this->yuv.v[plane_ptr] = v_palette[pixel];
        plane_ptr++;
      }
      sy += this->av_frame->linesize[0];
    }
            
    yuv444_to_yuy2(&this->yuv, img->base[0], img->pitches[0]);
          
  } else {
          
    for (y=0; y<this->context->height; y++) {
      xine_fast_memcpy (dy, sy, this->context->width);
  
      dy += img->pitches[0];
  
      sy += this->av_frame->linesize[0];
    }

    for (y=0; y<(this->context->height/2); y++) {
      
      if (this->context->pix_fmt != PIX_FMT_YUV444P) {
        
        xine_fast_memcpy (du, su, this->context->width/2);
        xine_fast_memcpy (dv, sv, this->context->width/2);
        
      } else {
        
        int x;
        uint8_t *src;
        uint8_t *dst;
      
        /* subsample */
        
        src = su; dst = du;
        for (x=0; x<(this->context->width/2); x++) {
          *dst = *src;
          dst++;
          src += 2;
        }
        src = sv; dst = dv;
        for (x=0; x<(this->context->width/2); x++) {
          *dst = *src;
          dst++;
          src += 2;
        }

      }
  
      du += img->pitches[1];
      dv += img->pitches[2];

      if (this->context->pix_fmt != PIX_FMT_YUV420P) {
        su += 2*this->av_frame->linesize[1];
        sv += 2*this->av_frame->linesize[2];
      } else {
        su += this->av_frame->linesize[1];
        sv += this->av_frame->linesize[2];
      }
    }
  }
}


static const ff_codec_t ff_video_lookup[] = {
  {BUF_VIDEO_MSMPEG4_V1,  CODEC_ID_MSMPEG4V1, "Microsoft MPEG-4 v1 (ffmpeg)"},
  {BUF_VIDEO_MSMPEG4_V2,  CODEC_ID_MSMPEG4V2, "Microsoft MPEG-4 v2 (ffmpeg)"},
  {BUF_VIDEO_MSMPEG4_V3,  CODEC_ID_MSMPEG4V3, "Microsoft MPEG-4 v3 (ffmpeg)"},
  {BUF_VIDEO_WMV7,        CODEC_ID_WMV1,      "MS Windows Media Video 7 (ffmpeg)"},
  {BUF_VIDEO_WMV8,        CODEC_ID_WMV2,      "MS Windows Media Video 8 (ffmpeg)"},
  {BUF_VIDEO_MPEG4,       CODEC_ID_MPEG4,     "ISO MPEG-4 (ffmpeg)"},
  {BUF_VIDEO_XVID,        CODEC_ID_MPEG4,     "ISO MPEG-4 (ffmpeg)"},
  {BUF_VIDEO_DIVX5,       CODEC_ID_MPEG4,     "ISO MPEG-4 (ffmpeg)"},
  {BUF_VIDEO_JPEG,        CODEC_ID_MJPEG,     "Motion JPEG (ffmpeg)"},
  {BUF_VIDEO_MJPEG,       CODEC_ID_MJPEG,     "Motion JPEG (ffmpeg)"},
  {BUF_VIDEO_I263,        CODEC_ID_H263I,     "ITU H.263 (ffmpeg)"},
  {BUF_VIDEO_H263,        CODEC_ID_H263,      "H.263 (ffmpeg)"},
  {BUF_VIDEO_RV10,        CODEC_ID_RV10,      "Real Video 1.0 (ffmpeg)"},
  {BUF_VIDEO_RV20,        CODEC_ID_RV20,      "Real Video 2.0 (ffmpeg)"},
  {BUF_VIDEO_IV31,        CODEC_ID_INDEO3,    "Indeo Video 3.1 (ffmpeg)"},
  {BUF_VIDEO_IV32,        CODEC_ID_INDEO3,    "Indeo Video 3.2 (ffmpeg)"},
  {BUF_VIDEO_SORENSON_V1, CODEC_ID_SVQ1,      "Sorenson Video 1 (ffmpeg)"},
  {BUF_VIDEO_SORENSON_V3, CODEC_ID_SVQ3,      "Sorenson Video 3 (ffmpeg)"},
  {BUF_VIDEO_DV,          CODEC_ID_DVVIDEO,   "DV (ffmpeg)"},
  {BUF_VIDEO_HUFFYUV,     CODEC_ID_HUFFYUV,   "HuffYUV (ffmpeg)"},
  {BUF_VIDEO_VP31,        CODEC_ID_VP3,       "On2 VP3.1 (ffmpeg)"},
  {BUF_VIDEO_4XM,         CODEC_ID_4XM,       "4X Video (ffmpeg)"},
  {BUF_VIDEO_CINEPAK,     CODEC_ID_CINEPAK,   "Cinepak (ffmpeg)"},
  {BUF_VIDEO_MSVC,        CODEC_ID_MSVIDEO1,  "Microsoft Video 1 (ffmpeg)"},
  {BUF_VIDEO_MSRLE,       CODEC_ID_MSRLE,     "Microsoft RLE (ffmpeg)"},
  {BUF_VIDEO_RPZA,        CODEC_ID_RPZA,      "Apple Quicktime Video/RPZA (ffmpeg)"},
  {BUF_VIDEO_CYUV,        CODEC_ID_CYUV,      "Creative YUV (ffmpeg)"},
  {BUF_VIDEO_ROQ,         CODEC_ID_ROQ,       "Id Software RoQ (ffmpeg)"},
  {BUF_VIDEO_IDCIN,       CODEC_ID_IDCIN,     "Id Software CIN (ffmpeg)"},
  {BUF_VIDEO_WC3,         CODEC_ID_XAN_WC3,   "Xan (ffmpeg)"},
  {BUF_VIDEO_VQA,         CODEC_ID_WS_VQA,    "Westwood Studios VQA (ffmpeg)"},
  {BUF_VIDEO_INTERPLAY,   CODEC_ID_INTERPLAY_VIDEO, "Interplay MVE (ffmpeg)"},
  {BUF_VIDEO_FLI,         CODEC_ID_FLIC,      "FLIC Video (ffmpeg)"},
  {BUF_VIDEO_8BPS,        CODEC_ID_8BPS,      "Planar RGB (ffmpeg)"},
  {BUF_VIDEO_SMC,         CODEC_ID_SMC,       "Apple Quicktime Graphics/SMC (ffmpeg)"},
  {BUF_VIDEO_DUCKTM1,     CODEC_ID_TRUEMOTION1,"Duck TrueMotion v1 (ffmpeg)"},
  {BUF_VIDEO_VMD,         CODEC_ID_VMDVIDEO,   "Sierra VMD Video (ffmpeg)"},
  {BUF_VIDEO_ZLIB,        CODEC_ID_ZLIB,       "ZLIB Video (ffmpeg)"},
  {BUF_VIDEO_MSZH,        CODEC_ID_MSZH,       "MSZH Video (ffmpeg)"},
  {BUF_VIDEO_ASV1,        CODEC_ID_ASV1,       "ASV v1 Video (ffmpeg)"},
  {BUF_VIDEO_ASV2,        CODEC_ID_ASV2,       "ASV v2 Video (ffmpeg)"},
  {BUF_VIDEO_ATIVCR1,     CODEC_ID_VCR1,       "ATI VCR-1 (ffmpeg)"},
  {BUF_VIDEO_FLV1,        CODEC_ID_FLV1,       "Flash Video (ffmpeg)"} };

static void ff_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  ff_video_decoder_t *this = (ff_video_decoder_t *) this_gen;
  int i, codec_type;

  lprintf ("processing packet type = %08x, buf : %p, buf->decoder_flags=%08x\n", 
           buf->type, buf, buf->decoder_flags);

  codec_type = buf->type & 0xFFFF0000;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {

    lprintf ("preview\n");

    if ( (buf->type & 0xFFFF0000) == BUF_VIDEO_MPEG ) {
      find_sequence_header (this, buf->content, buf->content+buf->size);
    }
    return;
  }

  if ( (buf->decoder_flags & BUF_FLAG_HEADER) && 
      !(buf->decoder_flags & BUF_FLAG_SPECIAL) ) {

    lprintf ("header\n");

    /* init codec */
    this->codec = NULL;

    for(i = 0; i < sizeof(ff_video_lookup)/sizeof(ff_codec_t); i++)
      if(ff_video_lookup[i].type == codec_type) {
        this->codec = avcodec_find_decoder(ff_video_lookup[i].id);
        _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC,
                         ff_video_lookup[i].name);
        break;
      }

    if (!this->codec) {
      xprintf (this->stream->xine, XINE_VERBOSITY_LOG, 
               _("ffmpeg_video_dec: couldn't find ffmpeg decoder for buf type 0x%X\n"),
               codec_type);
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);
      return;
    }

    if (buf->decoder_flags & BUF_FLAG_STDHEADER) {
    
      /* init package containing bih */
      memcpy ( &this->bih, buf->content, sizeof(xine_bmiheader) );

      init_video_codec (this, (xine_bmiheader *) buf->content );
            
    } else {
    
      switch (codec_type) {
      case BUF_VIDEO_RV10:
      case BUF_VIDEO_RV20:
        this->bih.biWidth  = BE_16(&buf->content[12]);
        this->bih.biHeight = BE_16(&buf->content[14]);
        
        this->video_step =  
          90000.0 / ((double) BE_16(&buf->content[22]) + 
                     ((double) BE_16(&buf->content[24]) / 65536.0));
                     
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, 
                           this->video_step);

        this->context->sub_id = BE_32(&buf->content[30]);

        this->context->slice_offset = xine_xmalloc(sizeof(int)*SLICE_OFFSET_SIZE);
        this->slice_offset_size     = SLICE_OFFSET_SIZE;
        break;
      default:
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                "ffmpeg_video_dec: unknown header for buf type 0x%X\n", codec_type);
        return;
      }
      
      init_video_codec (this, NULL);
    }
    
    init_postprocess(this);
    
  } else if (buf->decoder_flags & BUF_FLAG_SPECIAL) {

    /* take care of all the various types of special buffers */

    if (buf->decoder_info[1] == BUF_SPECIAL_STSD_ATOM) {

      this->context->extradata_size = buf->decoder_info[2];
      this->context->extradata = xine_xmalloc(buf->decoder_info[2]);
      memcpy(this->context->extradata, buf->decoder_info_ptr[2],
        buf->decoder_info[2]);

    } else if (buf->decoder_info[1] == BUF_SPECIAL_PALETTE) {

      palette_entry_t *demuxer_palette;
      AVPaletteControl *decoder_palette;
      
      decoder_palette = (AVPaletteControl *)this->context->palctrl;
      demuxer_palette = (palette_entry_t *)buf->decoder_info_ptr[2];

      for (i = 0; i < buf->decoder_info[2]; i++) {
        decoder_palette->palette[i] = 
          (demuxer_palette[i].r << 16) |
          (demuxer_palette[i].g <<  8) |
          (demuxer_palette[i].b <<  0);
      }
      decoder_palette->palette_changed = 1;

    } else if (buf->decoder_info[1] == BUF_SPECIAL_RV_CHUNK_TABLE) {
    
      this->context->slice_count = buf->decoder_info[2]+1;
      
      if(this->context->slice_count > this->slice_offset_size) {
        this->context->slice_offset = realloc(this->context->slice_offset,
                                              sizeof(int)*this->context->slice_count);
        this->slice_offset_size = this->context->slice_count;
      }
      
      for(i = 0; i < this->context->slice_count; i++)
        this->context->slice_offset[i] = 
          ((uint32_t *) buf->decoder_info_ptr[2])[(2*i)+1];
    
    }

  } else {
  
    if( this->size + buf->size > this->bufsize ) {
      this->bufsize = this->size + 2 * buf->size;
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG, 
              _("ffmpeg_video_dec: increasing buffer to %d to avoid overflow.\n"), 
              this->bufsize);
      this->buf = realloc( this->buf, this->bufsize );
    }

    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
    this->size += buf->size;

  }
   
  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->video_step = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->video_step);
  }
  
  if (buf->decoder_flags & BUF_FLAG_ASPECT) {
    this->aspect_ratio = (double)buf->decoder_info[1] / (double)buf->decoder_info[2];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO, 
                       this->aspect_ratio*10000);
  }
  
  if (this->decoder_ok && this->size) {
  
    if ( (buf->decoder_flags & BUF_FLAG_FRAME_END) || this->is_continous ) {

      vo_frame_t *img;
      int         free_img;
      int         got_picture, len;
      int         offset;

      /* decode video frame(s) */


      /* flag for interlaced streams */
      this->frame_flags = 0;
      /* FIXME: which codecs can be interlaced?
         FIXME: check interlaced DCT and other codec specific info. */
      switch( codec_type ) {
        case BUF_VIDEO_DV:
          this->frame_flags |= VO_INTERLACED_FLAG;
          break;
        case BUF_VIDEO_MPEG:
          this->frame_flags |= VO_INTERLACED_FLAG;
          break;
        case BUF_VIDEO_MJPEG:
          this->frame_flags |= VO_INTERLACED_FLAG;
          break;
        case BUF_VIDEO_HUFFYUV:
          this->frame_flags |= VO_INTERLACED_FLAG;
          break;
      }

      /* skip decoding b frames if too late */
      this->context->hurry_up = (this->skipframes > 2) ? 1:0;

      offset = 0;
      while (this->size>0) {
        
        /* DV frames can be completely skipped */
        if( codec_type == BUF_VIDEO_DV && this->skipframes ) {
          len = this->size;
          got_picture = 1;
        } else
          len = avcodec_decode_video (this->context, this->av_frame,
                                      &got_picture, &this->buf[offset],
                                      this->size);
        if (len<0) {
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, 
                   "ffmpeg_video_dec: error decompressing frame\n");
          this->size=0;
          return;
        }

        this->size -= len;
        offset += len;

        if (!got_picture || !this->av_frame->data[0]) {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, 
                  "ffmpeg_video_dec: didn't get a picture, %d bytes left\n", 
                  this->size);

          if (this->size>0)
            memmove (this->buf, &this->buf[offset], this->size);

          return;
        }

        lprintf ("got a picture\n");

        if(av_cmp_q(this->context->sample_aspect_ratio, (AVRational){0,0})) {
          this->aspect_ratio = av_q2d(this->context->sample_aspect_ratio) * 
              (double)this->context->width / (double)this->context->height;
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  
                             this->aspect_ratio*10000);
        }

        if(!this->av_frame->opaque) {
          img = this->stream->video_out->get_frame (this->stream->video_out,
                                                    this->context->width,
                                                    this->context->height,
                                                    this->aspect_ratio, 
                                                    this->output_format,
                                                    VO_BOTH_FIELDS|this->frame_flags);
          free_img = 1;
        } else {
          img = (vo_frame_t*) this->av_frame->opaque;
          free_img = 0;
        }

        if (len<0 || this->skipframes) {
          if( !this->skipframes )
            xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, 
                     "ffmpeg_video_dec: error decompressing frame\n");
          img->bad_frame = 1;
        } else {
          img->bad_frame = 0;

          if(this->pp_quality_changed)
            pp_change_quality(this);

          if(this->pp_available && this->pp_quality) {

            if(this->av_frame->opaque) {
              img = this->stream->video_out->get_frame (this->stream->video_out,
                                                        img->width,
                                                        img->height,
                                                        this->aspect_ratio, 
                                                        this->output_format,
                                                        VO_BOTH_FIELDS|this->frame_flags);
              free_img = 1;
              img->bad_frame = 0;
            }

            pp_postprocess(this->av_frame->data, this->av_frame->linesize, 
                           img->base, img->pitches, 
                           img->width, img->height,
                           this->av_frame->qscale_table, this->av_frame->qstride,
                           this->pp_mode, this->pp_context, 
                           this->av_frame->pict_type);

          } else if(!this->av_frame->opaque) {
            ff_convert_frame(this, img);
          }
        }
      
        img->pts      = buf->pts;
        buf->pts      = 0;
        img->duration = this->video_step;

        this->skipframes = img->draw(img, this->stream);
        if( this->skipframes < 0 )
          this->skipframes = 0;

        if(free_img)
          img->free(img);
      }
    }

  }
}

static void ff_flush (video_decoder_t *this_gen) {
  lprintf ("ff_flush\n");
}

static void ff_reset (video_decoder_t *this_gen) {
  ff_video_decoder_t *this = (ff_video_decoder_t *) this_gen;

  lprintf ("ff_reset\n");

  this->size = 0;
  if(this->context)
    avcodec_flush_buffers(this->context);
}

static void ff_discontinuity (video_decoder_t *this_gen) {
  
  lprintf ("ff_discontinuity\n");
}

static void ff_dispose (video_decoder_t *this_gen) {
  ff_video_decoder_t *this = (ff_video_decoder_t *) this_gen;

  lprintf ("ff_dispose\n");

  if (this->decoder_ok) {
    avcodec_close (this->context);

    this->stream->video_out->close(this->stream->video_out, this->stream);
    this->decoder_ok = 0;
  }

  if(this->context && this->context->slice_offset)
    free(this->context->slice_offset);

  if(this->context && this->context->extradata)
    free(this->context->extradata);

  if((this->context) && 
   ((this->context->pix_fmt == PIX_FMT_RGBA32) ||
    (this->context->pix_fmt == PIX_FMT_RGB565) ||
    (this->context->pix_fmt == PIX_FMT_RGB555) ||
    (this->context->pix_fmt == PIX_FMT_PAL8)))
    free_yuv_planes(&this->yuv);
  
  if( this->context )
    free( this->context );

  if( this->av_frame )
    free( this->av_frame );
  
  if (this->buf)
    free(this->buf);
  this->buf = NULL;
  
  if(this->pp_context)
    pp_free_context(this->pp_context);
    
  if(this->pp_mode)
    pp_free_mode(this->pp_mode);
  
  free (this->chunk_buffer);
  free (this_gen);
}

static video_decoder_t *ff_video_open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {

  ff_video_decoder_t  *this ;

  lprintf ("open_plugin\n");

  this = (ff_video_decoder_t *) xine_xmalloc (sizeof (ff_video_decoder_t));

  this->video_decoder.decode_data         = ff_decode_data;
  this->video_decoder.flush               = ff_flush;
  this->video_decoder.reset               = ff_reset;
  this->video_decoder.discontinuity       = ff_discontinuity;
  this->video_decoder.dispose             = ff_dispose;
  this->size                              = 0;

  this->stream                            = stream;
  this->class                             = (ff_video_class_t *) class_gen;
  this->class->ip                         = this;

  this->av_frame        = avcodec_alloc_frame();
  this->context         = avcodec_alloc_context();
  this->context->opaque = this;
  
  this->chunk_buffer = xine_xmalloc (SLICE_BUFFER_SIZE + 4);

  this->decoder_ok    = 0;
  this->buf           = NULL;

  this->shift         = 0xffffff00;
  this->code          = 0xb4;
  this->chunk_ptr     = this->chunk_buffer;

  this->is_continous  = 0;
  this->aspect_ratio = 0;

  this->pp_quality  = 0;
  this->pp_context  = NULL;
  this->pp_mode     = NULL;

  return &this->video_decoder;
}

static char *ff_video_get_identifier (video_decoder_class_t *this) {
  return "ffmpeg video";
}

static char *ff_video_get_description (video_decoder_class_t *this) {
  return "ffmpeg based video decoder plugin";
}

static void ff_video_dispose_class (video_decoder_class_t *this) {
  free (this);
}

void *init_video_plugin (xine_t *xine, void *data) {

  ff_video_class_t *this;
  config_values_t  *config;
  
  this = (ff_video_class_t *) xine_xmalloc (sizeof (ff_video_class_t));

  this->decoder_class.open_plugin     = ff_video_open_plugin;
  this->decoder_class.get_identifier  = ff_video_get_identifier;
  this->decoder_class.get_description = ff_video_get_description;
  this->decoder_class.dispose         = ff_video_dispose_class;
  this->ip                            = NULL;
  this->xine                          = xine;

  pthread_once( &once_control, init_once_routine );
  
  /* Configuration for post processing quality - default to mid (3) for the
   * moment */
  config = xine->config;
  xine->config->register_range(config, "codec.ffmpeg_pp_quality", 3, 
    0, PP_QUALITY_MAX,  _("ffmpeg mpeg-4 postprocessing quality"), NULL,
    10, pp_quality_cb, this);  
  
  return this;
}

static uint32_t supported_video_types[] = { 
  BUF_VIDEO_MSMPEG4_V1, 
  BUF_VIDEO_MSMPEG4_V2,
  BUF_VIDEO_MSMPEG4_V3, 
  BUF_VIDEO_WMV7, 
  BUF_VIDEO_MPEG4,
  BUF_VIDEO_XVID, 
  BUF_VIDEO_DIVX5, 
  BUF_VIDEO_MJPEG,
  BUF_VIDEO_H263,
  BUF_VIDEO_RV10,
  BUF_VIDEO_RV20,
  BUF_VIDEO_IV31,
  BUF_VIDEO_IV32,
  BUF_VIDEO_SORENSON_V1,
  BUF_VIDEO_SORENSON_V3,
  BUF_VIDEO_JPEG, 
  BUF_VIDEO_MPEG, 
  BUF_VIDEO_DV,
  BUF_VIDEO_HUFFYUV,
  BUF_VIDEO_VP31,
  BUF_VIDEO_4XM,
  BUF_VIDEO_CINEPAK,
  BUF_VIDEO_MSVC,
  BUF_VIDEO_MSRLE,
  BUF_VIDEO_RPZA,
  BUF_VIDEO_CYUV,
  BUF_VIDEO_ROQ,
  BUF_VIDEO_IDCIN,
  BUF_VIDEO_WC3,
  BUF_VIDEO_VQA,
  BUF_VIDEO_INTERPLAY,
  BUF_VIDEO_FLI,
  BUF_VIDEO_8BPS,
  BUF_VIDEO_SMC,
  BUF_VIDEO_VMD,
  BUF_VIDEO_DUCKTM1,
  BUF_VIDEO_ZLIB,
  BUF_VIDEO_MSZH,
  BUF_VIDEO_ASV1,
  BUF_VIDEO_ASV2,
  BUF_VIDEO_ATIVCR1,
  BUF_VIDEO_FLV1,
  0 
};

static uint32_t wmv8_video_types[] = { 
  BUF_VIDEO_WMV8,
  0 
};

decoder_info_t dec_info_ffmpeg_video = {
  supported_video_types,   /* supported types */
  5                        /* priority        */
};

decoder_info_t dec_info_ffmpeg_wmv8 = {
  wmv8_video_types,        /* supported types */
  0                        /* priority        */
};
