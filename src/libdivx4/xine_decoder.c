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
 * $Id: xine_decoder.c,v 1.1 2001/10/07 18:06:00 guenter Exp $
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

#include "xine_internal.h"
#include "cpu_accel.h"
#include "video_out.h"
#include "buffer.h"
#include "metronom.h"

#include <decore.h>

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
  /* whether to decode MSMPEG4_V3 format
     (aka divx ;-) and divx 3.11-- thank god they dropped the smileys 
     with divx4)
  */
  int		    use_311_compat;
  /* postprocessing level; currently valid values 0-6 (internally 0-100)
     set by divx4_postproc in .xinerc */
  int		    postproc;
} divx4_decoder_t;

static unsigned long str2ulong(void *data) {

  unsigned char *str = data;
  return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

static int divx4_can_handle (video_decoder_t *this_gen, int buf_type) {
  buf_type &= 0xFFFF0000;

  /* divx4 currently does not support MSMPEG4 v1/v2 */
  return ( buf_type == BUF_VIDEO_MSMPEG4_V3 ||
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
    param.output_format=DEC_YV12;
    memset(&param.buffers, 0, sizeof(param.buffers));
    /* FIXME: check return value: */
    decore((unsigned long)this, DEC_OPT_INIT, &param, &this->bih);

    /* multiply postproc level by 10 for internal consumption */
    printf("Divx4: Setting post processing level to %d (see ~/.xinerc)\n"
	   "Divx4: Valid range 0-6, reduce if you get frame drop\n", 
	   this->postproc); 
    setpp.postproc_level=this->postproc*10;
    decore((unsigned long)this, DEC_OPT_SETPP, &setpp, 0);

    this->decoder_ok = 1;
    this->video_out->open (this->video_out);

  } else if (this->decoder_ok) {

    memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_info[0] == 2)  { // what does this mean?
      // allocate image (taken from ffmpeg plugin)
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
	FIXME: I assume here that the layout of base[] is flat,
	i.e. that base[0], [1] and [2] all point inside the same
	contiguous bit of memory. It seems to work for the Xv driver
	but I don not know if this is always acceptable.
      */
      frame.bitstream= (void*)this->buf;
      frame.bmp=img->base[0]; // can I do this?
      frame.length=this->size;
      frame.render_flag=1;
      frame.stride=this->biWidth;

      if(this->use_311_compat)
	decore((unsigned long)this, DEC_OPT_FRAME_311, &frame, 0);
      else
	decore((unsigned long)this, DEC_OPT_FRAME, &frame, 0);

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
      decore((unsigned long)this, DEC_OPT_RELEASE, 0, 0);*/

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

  if (iface_version != 2) {
    printf( "divx4: plugin doesn't support plugin API version %d.\n"
	    "divx4: this means there's a version mismatch between xine and this "
	    "divx4: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    
    return NULL;
  }

  this = (divx4_decoder_t *) malloc (sizeof (divx4_decoder_t));

  this->video_decoder.interface_version   = 2;
  this->video_decoder.can_handle          = divx4_can_handle;
  this->video_decoder.init                = divx4_init;
  this->video_decoder.decode_data         = divx4_decode_data;
  this->video_decoder.close               = divx4_close;
  this->video_decoder.get_identifier      = divx4_get_id;
  this->video_decoder.priority            = cfg->lookup_int(cfg, "divx4_priority", 6); 
  this->postproc = cfg->lookup_int(cfg, "divx4_postproc", 3);
  this->size				  = 0;

  return (video_decoder_t *) this;
}


