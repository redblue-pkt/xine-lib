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
 * $Id: xine_decoder.c,v 1.3 2001/10/15 16:13:23 jkeil Exp $
 *
 * xine decoder plugin using divx4
 *
 * by Harm van der Heijden <hrm@users.sourceforge.net>
 * Based on the ffmpeg xine plugin, with ideas from avifile's 
 * (http://avifile.sourceforge.net) divx4 plugin.
 *
 * Requires the divxdecore library. Find it at http://www.divx.com or 
 * http://avifile.sourceforge.net 
 * This plugin may or may not work with the open source codec OpenDivx.
 *
 * This is My First Plugin (tm). Read the source comments for hairy details.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <dlfcn.h>

#include "xine_internal.h"
#include "cpu_accel.h"
#include "video_out.h"
#include "buffer.h"
#include "metronom.h"

#include "decore-if.h"

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

typedef struct divx4_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t    *video_out;
  int               video_step;
  int               decoder_ok;

  BITMAPINFOHEADER  bih;
  long		    biWidth;
  long		    biHeight;
  unsigned char     buf[128*1024];
  int               size;
  decoreFunc        decore; /* ptr to decore function in libdivxdecore */
  /* whether to decode MSMPEG4_V3 format
     (aka divx ;-) and divx 3.11-- thank god they dropped the smileys 
     with divx4)
  */
  int		    use_311_compat;
  /* postprocessing level; currently valid values 0-6 (internally 0-100)
     set by divx4_postproc in .xinerc */
  int		    postproc;
  /* what output format we ask of decore()
     supported at the moment:
     DEC_YV12, copied straight into image buffer by decore(), 
     fast but perhaps risky.
     DEC_USER, decore() returns pointers to internal y,u,v buffers, 
     and we copy the data ourselves. Not optimised, so probably slower.
     It seems that OpenDivx likes this better. */
  int		    decore_format; 
  /* can we handle 311 format? No easy way to find out (divx4linux can,
     OpenDivx cannot, so the user can set it in .xinerc. If 0, can_handle
     only returns MPEG4, yielding 311 to ffmpeg */
  int		    can_handle_311;
} divx4_decoder_t;

static unsigned long str2ulong(void *data) {

  unsigned char *str = data;
  return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

static char* decore_retval(int ret)
{
  switch (ret) {
  case DEC_OK: return "DEC_OK";
  case DEC_MEMORY: return "DEC_MEMORY";
  case DEC_BAD_FORMAT: return "DEC_BAD_FORMAT";
  case DEC_EXIT: return "DEC_EXIT";
  }
  return "[Unknown code]";	
}

static int divx4_can_handle (video_decoder_t *this_gen, int buf_type) {
  divx4_decoder_t *this = (divx4_decoder_t *) this_gen;
  buf_type &= 0xFFFF0000;

  /* divx4 currently does not support MSMPEG4 v1/v2 */
  return ( (buf_type == BUF_VIDEO_MSMPEG4_V3 && this->can_handle_311) ||
           /* buf_type == BUF_VIDEO_MSMPEG4_V12 || */
           buf_type == BUF_VIDEO_MPEG4);
}

/* copied verbatim from ffmpeg plugin */
static void divx4_init (video_decoder_t *this_gen, vo_instance_t *video_out) {

  divx4_decoder_t *this = (divx4_decoder_t *) this_gen;

  this->video_out  = video_out;
  this->decoder_ok = 0;
}

static void divx4_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {

  DEC_PARAM param; /* for init                   */
  DEC_SET setpp;   /* for setting postproc level */
  DEC_FRAME frame; /* for getting a frame        */
  DEC_PICTURE pict;/* contains ptrs to the decoders internal yuv buffers */
  int ret;

  divx4_decoder_t *this = (divx4_decoder_t *) this_gen;

  if (buf->decoder_info[0] == 0) {

    int codec_type;

    memcpy ( &this->bih, buf->content, sizeof (BITMAPINFOHEADER));
    this->biWidth = str2ulong(&this->bih.biWidth);
    this->biHeight = str2ulong(&this->bih.biHeight);
    this->video_step = buf->decoder_info[1];

    codec_type = buf->type & 0xFFFF0000;

    /* do we need divx 3.11 compatibility mode? */
    switch (buf->type & 0xFFFF0000) {
    case BUF_VIDEO_MSMPEG4_V12:
    case BUF_VIDEO_MSMPEG4_V3:
      this->use_311_compat = 1;
      break;
    case BUF_VIDEO_MPEG4 :
      this->use_311_compat = 0;
      break;
    default:
      printf ("divx4: unknown video format (buftype: 0x%08X)\n",
	      buf->type & 0xFFFF0000);
    }

    /* setup decoder; inspired by avifile's plugin */
    param.x_dim=this->biWidth;
    param.y_dim=this->biHeight;
    param.time_incr = 15; /* no idea what this does */
    /* FIXME: the decoder can also supply RGB data, and my avifile experience
       is that it's far preferable over generic yuv conversion. Would this
       be interesting for the XShm crowd, lacking a YUV overlay? */
    param.output_format=this->decore_format;
    memset(&param.buffers, 0, sizeof(param.buffers));

    ret = this->decore((unsigned long)this, DEC_OPT_INIT, &param, &this->bih);
    if (ret != DEC_OK) {
	printf("divx4: decore DEC_OPT_INIT command returned %s.\n", decore_retval(ret));
	return;
    }

    /* multiply postproc level by 10 for internal consumption */
    printf("divx4: Setting post processing level to %d (see ~/.xinerc)\n"
	   "divx4: Valid range 0-6, reduce if you get frame drop\n", 
	   this->postproc); 
    setpp.postproc_level=this->postproc*10;

    ret = this->decore((unsigned long)this, DEC_OPT_SETPP, &setpp, 0);
    if (ret != DEC_OK)
    {
	printf("divx4: decore DEC_OPT_SETPP command returned %s.\n", decore_retval(ret));
	/* perhaps not fatal, so we'll continue */
    }

    this->decoder_ok = 1;
    this->video_out->open (this->video_out);

  } else if (this->decoder_ok) {

    memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_info[0] == 2)  { /* what does this mean? */
      /* allocate image (taken from ffmpeg plugin) */
      vo_frame_t *img;
      img = this->video_out->get_frame (this->video_out,
					/* this->av_picture.linesize[0],  */
					this->biWidth,
					this->biHeight,
					42, 
					IMGFMT_YV12,
					this->video_step,
					VO_BOTH_FIELDS);

      /*
	setup the decode frame parameters, as demonstrated by avifile.
	If decore format is DEC_YV12, I assume here that the layout of 
        base[] is flat, i.e. that base[0], [1] and [2] all point inside the same
	contiguous bit of memory. It seems to work for the Xv driver
	but I don't not know if this is always acceptable. Use DEC_USER if this
        causes problems (configurable via .xinerc). Also, OpenDivx seems to
        prefer DEC_USER.
      */
      frame.bitstream= (void*)this->buf;

      if (this->decore_format == DEC_USER)
        frame.bmp=&pict; /* decore will set ptrs to internal y,u&v buffers */
      else
        frame.bmp=img->base[0]; /* YV12: assume y,u & v buffers are contiguous */

      frame.length=this->size;
      frame.render_flag=1;
      frame.stride=this->biWidth;

      if(this->use_311_compat)
	ret = this->decore((unsigned long)this, DEC_OPT_FRAME_311, &frame, 0);
      else
	ret = this->decore((unsigned long)this, DEC_OPT_FRAME, &frame, 0);

      if (ret != DEC_OK) {
	printf("divx4: decore DEC_OPT_FRAME command returned %s.\n", decore_retval(ret));
	img->bad_frame = 1; /* better skip this one */
      }
      else if (this->decore_format == DEC_USER)
      {
        /* We need to copy the yuv data from the decoder's internal buffers.
           Y size is width*height, U and V width*height/4 */ 
        int i;
        int src_offset,dst_offset;
        /* shortcut if stride_y equals width */
        if (pict.stride_y == img->width) {
          memcpy(img->base[0], pict.y, img->width*img->height);
        }
        else { /* copy line by line */
	  src_offset=dst_offset = 0;
          for (i=0; i<img->height; i++) {
            memcpy(img->base[0]+dst_offset, pict.y+src_offset, img->width);
            src_offset += pict.stride_y;
            dst_offset += img->width;
          }
        }
        /* same for u,v data, but at 1/4 resolution.
           FIXME: Weird... I thought YV12 means order y-v-u, yet base[1] 
           seems to be u and base[2] is v. */
        if (pict.stride_uv == img->width>>1) {
          memcpy(img->base[1], pict.u, (img->width*img->height)>>2);
          memcpy(img->base[2], pict.v, (img->width*img->height)>>2);
        }
        else {
	  src_offset=dst_offset = 0;
          for (i=0; i<img->height>>1; i++) {
            memcpy(img->base[1]+dst_offset, pict.u+src_offset, img->width>>1);
            memcpy(img->base[2]+dst_offset, pict.v+src_offset, img->width>>1);
            src_offset += pict.stride_uv;
            dst_offset += img->width>>1;
	  }
        } 
      }

      /* more video-out voodoo:
	 some sort of copy operation, looks like. Straight from the ffmpeg
	 plugin. The XShm driver seems to need it, the Xv one does not. */
      if (img->copy && img->bad_frame == 0) {
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


      /* this again from ffmpeg plugin */
      img->PTS = buf->PTS;
      img->draw(img);
      img->free(img);
      
      this->size = 0;
    }
  }
}

static void divx4_close (video_decoder_t *this_gen)  {

  divx4_decoder_t *this = (divx4_decoder_t *) this_gen;

  if (this->decoder_ok) {
    /* FIXME: this segfaults here
      (note: avifile also has the release command commented out;
       probably a known 'feature')
      this->decore((unsigned long)this, DEC_OPT_RELEASE, 0, 0);*/

    this->video_out->close(this->video_out);
    this->decoder_ok = 0;
  }
}

static char *divx4_get_id(void) {
  return "divx4 video decoder";
}

/* This is pretty generic. I took the liberty to increase the
   priority over that of libffmpeg :-) */
video_decoder_t *init_video_decoder_plugin (int iface_version, config_values_t *cfg) {

  divx4_decoder_t *this ;
  char *libdecore_name;
  void *libdecore_handle;
  decoreFunc libdecore_func = 0;

  if (iface_version != 2) {
    printf( "divx4: plugin doesn't support plugin API version %d.\n"
	    "divx4: this means there's a version mismatch between xine and this "
	    "divx4: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    
    return NULL;
  }

  /* Try to dlopen libdivxdecore, then look for decore function 
     if it fails, print a message and return 0 so that xine ignores
     us from then on. */
  libdecore_name = cfg->lookup_str(cfg, "divx4_libdivxdecore", "libdivxdecore.so");  
  libdecore_handle = dlopen(libdecore_name, RTLD_LAZY);
  if (libdecore_handle)
    libdecore_func = dlsym(libdecore_handle, "decore"); 
  if (! libdecore_func) {
    printf("divx4: could not find decore function in library \"%s\"\n"
           "divx4: system returned \"%s\"\n"
           "divx4: libdivxdecore unavailable; this plugin will be disabled.\n", 
           libdecore_name, dlerror()); 
    return NULL;
  }
  printf("divx4: successfully opened \"%s\"\n", libdecore_name);

  this = (divx4_decoder_t *) malloc (sizeof (divx4_decoder_t));

  this->video_decoder.interface_version   = 2;
  this->video_decoder.can_handle          = divx4_can_handle;
  this->video_decoder.init                = divx4_init;
  this->video_decoder.decode_data         = divx4_decode_data;
  this->video_decoder.close               = divx4_close;
  this->video_decoder.get_identifier      = divx4_get_id;
  this->video_decoder.priority            = cfg->lookup_int(cfg, "divx4_priority", 6); 
  this->decore = libdecore_func;
  this->postproc 			  = cfg->lookup_int(cfg, "divx4_postproc", 3);
  this->decore_format			  = cfg->lookup_int(cfg, "divx4_decoreformat", 1);
  this->can_handle_311			  = cfg->lookup_int(cfg, "divx4_msmpeg4v3", 1);
  this->size				  = 0;

  /* at the moment availabe values are 0-6, but future versions may support
     higher levels. Internally, postproc is multiplied by 10 and values 
     between 0 and 100 are valid */
  if (this->postproc > 10) this->postproc=10;
  if (this->postproc < 0) this->postproc=0;

  /* translate the decore_format value to the internal constant, correct if
     an illegal value was given. 
     This might someday be extended to allow for RGB output from the decoder */
  if (this->decore_format == 0) this->decore_format = DEC_YV12;
  else if (this->decore_format == 1) this->decore_format = DEC_USER;
  else {
    printf("divx4: Illegal value %d for divx4_decoreformat, using 1.\n"
           "divx4: Valid are 0 (YV12, faster) and 1 (USER, safer). No quality difference.\n",
           this->decore_format);
    this->decore_format = DEC_USER;
  }
  return (video_decoder_t *) this;
}


