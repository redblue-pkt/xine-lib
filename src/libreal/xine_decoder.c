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
 * $Id: xine_decoder.c,v 1.2 2002/11/20 11:57:44 mroi Exp $
 *
 * thin layer to use real binary-only codecs in xine
 *
 * code inspired by work from Florian Schneider for the MPlayer Project 
 */


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

#include "bswap.h"
#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"


#define LOG



typedef struct {
  video_decoder_class_t   decoder_class;
  void                   *rv_handle;

  unsigned long (*rvyuv_custom_message)(unsigned long*,void*);
  unsigned long (*rvyuv_free)(void*);
  unsigned long (*rvyuv_hive_message)(unsigned long,unsigned long);
  unsigned long (*rvyuv_init)(void*, void*); /* initdata,context */
  unsigned long (*rvyuv_transform)(char*, char*,unsigned long*,unsigned long*,void*);
} real_class_t;


typedef struct realdec_decoder_s {
  video_decoder_t  video_decoder;

  real_class_t    *cls;

  xine_stream_t   *stream;

  void            *context;

  int              width, height;

} realdec_decoder_t;

/* we need exact positions */
typedef struct {
        short unk1;
        short w;
        short h;
        short unk3;
        int unk2;
        int subformat;
        int unk5;
        int format;
} rv_init_t;

#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 256

void hexdump (char *buf, int length) {

  int i;

  printf ("pnm: ascii contents>");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    if ((c >= 32) && (c <= 128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");

  printf ("pnm: complete hexdump of package follows:\npnm:  ");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\npnm: ");

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");
}


typedef struct dp_hdr_s {
  uint32_t chunks;	/* number of chunks             */
  uint32_t timestamp;   /* timestamp from packet header */
  uint32_t len;	        /* length of actual data        */
  uint32_t chunktab;	/* offset to chunk offset array */
} dp_hdr_t;


static void realdec_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;
  real_class_t      *cls  = this->cls;

#ifdef LOG
  printf ("libreal: decode_data, flags=0x%08x ...\n", buf->decoder_flags);
#endif

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    /* real_find_sequence_header (&this->real, buf->content, buf->content + buf->size);*/
  } else if (buf->decoder_flags & BUF_FLAG_HEADER) {

    unsigned int* extrahdr = (unsigned int*) (buf->content+28);
    int           result;
    rv_init_t     init_data = {11, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, 
			       0, 1, 0}; /* rv30 */

    init_data.w = BE_16(&buf->content[12]);
    init_data.h = BE_16(&buf->content[14]);

    init_data.subformat = BE_32(&buf->content[26]);
    init_data.format    = BE_32(&buf->content[30]);

    hexdump (&init_data, sizeof (init_data));

    hexdump (buf->content, 32);
    hexdump (extrahdr, 10);

    printf ("libreal: init codec %dx%d... %x %x\n", 
	    init_data.w, init_data.h,
	    init_data.subformat, init_data.format );

    this->context = NULL;

    result = cls->rvyuv_init (&init_data, &this->context); 

    printf ("libreal: ... done %d\n", result);

    /* setup rv30 codec (codec sub-type and image dimensions): */
    if (init_data.format>=0x20200002){
      unsigned long cmsg24[4]={this->width,this->height,
			       this->width,this->height};
      unsigned long cmsg_data[3]={0x24,1+((init_data.subformat>>16)&7),
				  (unsigned long) &cmsg24};
      cls->rvyuv_custom_message (cmsg_data, this->context);

      printf ("libreal: special setup for rv30 done\n");

    }

    /*
      real_init (&this->real, stream->video_out);
    */

    this->stream->video_out->open(this->stream->video_out, this->stream);
    

  } else if (this->context) {

    int            result;
    vo_frame_t    *img;
    void          *data = buf->content;
    dp_hdr_t      *dp_hdr = (dp_hdr_t*)data;
    unsigned char *dp_data = ((unsigned char*)data)+sizeof(dp_hdr_t);
    uint32_t      *extra = (uint32_t*)(((char*)data)+dp_hdr->chunktab);
    unsigned long  transform_out[5];
    unsigned long  transform_in[6]={
      dp_hdr->len,	     /* length of the packet (sub-packets appended) */
      0,		     /* unknown, seems to be unused  */
      dp_hdr->chunks,	     /* number of sub-packets - 1    */
      (unsigned long) extra, /* table of sub-packet offsets  */
      0,		     /* unknown, seems to be unused  */
      dp_hdr->timestamp      /* timestamp (the integer value from the stream) */
    };

    img = this->stream->video_out->get_frame (this->stream->video_out,
					      /* this->av_picture.linesize[0],  */
					      this->width,
					      this->height,
					      42, 
					      XINE_IMGFMT_YV12,
					      VO_BOTH_FIELDS);

    img->pts       = 0; /* FIXME */
    img->duration  = 3000; /* FIXME */
    img->bad_frame = 0;

    printf ("libreal: decoding:\n");
    hexdump (data, buf->size);

    result = cls->rvyuv_transform (dp_data, 
				   img->base[0], 
				   transform_in,
				   transform_out, 
				   this->context);

    /* xine_fast_memcpy (dy, sy, this->bih.biWidth); */

    img->draw(img, this->stream);
    img->free(img);

  } else {
    /*
    real_decode_data (&this->real, buf->content, buf->content + buf->size,
		       buf->pts);
    */
  }

#ifdef LOG
  printf ("libreal: decode_data...done\n");
#endif
}

static void realdec_flush (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libreal: flush\n");
#endif

}

static void realdec_reset (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

}

static void realdec_discontinuity (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

}

static void realdec_dispose (video_decoder_t *this_gen) {

  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libreal: close\n");
#endif

  free (this);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, 
				     xine_stream_t *stream) {

  real_class_t      *cls = (real_class_t *) class_gen;
  realdec_decoder_t *this ;

  this = (realdec_decoder_t *) malloc (sizeof (realdec_decoder_t));
  memset(this, 0, sizeof (realdec_decoder_t));

  this->video_decoder.decode_data         = realdec_decode_data;
  this->video_decoder.flush               = realdec_flush;
  this->video_decoder.reset               = realdec_reset;
  this->video_decoder.discontinuity       = realdec_discontinuity;
  this->video_decoder.dispose             = realdec_dispose;
  this->stream                            = stream;
  this->cls                               = cls;

  this->context = 0;

  return &this->video_decoder;
}

/*
 * real plugin class
 */

static char *get_identifier (video_decoder_class_t *this) {
  return "realdec";
}

static char *get_description (video_decoder_class_t *this) {
  return "real binary-only codec based video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

/*
 * some fake functions to make real codecs happy 
 */
void *__builtin_vec_new(unsigned long size) {
  return malloc(size);
}
void __builtin_vec_delete(void *mem) {
  free(mem);
}
void __pure_virtual(void) {
  printf("libreal: FATAL: __pure_virtual() called!\n");
  /*      exit(1); */
}

/*
 * real codec loader
 */

static int load_syms_linux (real_class_t *cls, char *path) {

  printf ("libreal: opening shared obj '%s'\n", path);
  cls->rv_handle = dlopen (path, RTLD_LAZY);

  if (!cls->rv_handle) {
    printf ("libreal: error: %s\n", dlerror());
    return 0;
  }
  
  cls->rvyuv_custom_message = dlsym (cls->rv_handle, "RV20toYUV420CustomMessage");
  cls->rvyuv_free           = dlsym (cls->rv_handle, "RV20toYUV420Free");
  cls->rvyuv_hive_message   = dlsym (cls->rv_handle, "RV20toYUV420HiveMessage");
  cls->rvyuv_init           = dlsym (cls->rv_handle, "RV20toYUV420Init");
  cls->rvyuv_transform      = dlsym (cls->rv_handle, "RV20toYUV420Transform");
  
  if (cls->rvyuv_custom_message &&
      cls->rvyuv_free &&
      cls->rvyuv_hive_message &&
      cls->rvyuv_init &&
      cls->rvyuv_transform) 
    return 1;

  printf ("libreal: Error resolving symbols! (version incompatibility?)\n");
  return 0;
}

static void *init_class (xine_t *xine, void *data) {

  real_class_t *this;

  this = (real_class_t *) xine_xmalloc (sizeof (real_class_t));

  if (!load_syms_linux (this, "/usr/local/RealPlayer8/Codecs/drv3.so.6.0")) {
    free (this);
    return NULL;
  }

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t supported_types[] = { BUF_VIDEO_RV20, BUF_VIDEO_RV30, 0 };

static decoder_info_t dec_info_real = {
  supported_types,     /* supported types */
  6                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 13, "real", XINE_VERSION_CODE, &dec_info_real, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
