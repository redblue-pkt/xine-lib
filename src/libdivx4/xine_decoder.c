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
 * $Id: xine_decoder.c,v 1.42 2002/09/05 20:44:39 mroi Exp $
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

/* 1: catch segmentation fault signal when checking version of libdivxdecore
 * 0: don't try to catch 
 * set to 0 if the signal stuff gives problems. tested in x86 linux w/ glibc2.
 */
#define CATCH_SIGSEGV 1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <dlfcn.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "metronom.h"
#include "xineutils.h"

#include "decore-if.h"

#if CATCH_SIGSEGV
#include <signal.h>

/*
#define LOG 
*/

/* to be able to restore the old handler */
void (*old_handler)(int);

void catch_sigsegv(int sig)
{
  printf("divx4: caught SIGSEGV, caused by libdivxdecore.\n"
         "divx4: please uninstall this library or disable the libdivxdecore\n"
         "divx4: version check by setting the following line in HOME/.xinerc:\n"
         "divx4: divx4_forceversion:1\n"
         "divx4: see xine-ui/doc/README.divx4 for details.\n"
         "divx4: fatal error; exiting.\n");
  abort();
}
#endif

typedef struct divx4_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t    *video_out;
  int               video_step;
  int               decoder_ok;

  xine_bmiheader    bih;
  unsigned char     *buf;
  int               size;
  int               bufsize;
  
  decoreFunc        decore; /* ptr to decore function in libdivxdecore */

  /* version as reported by decore with GET_OPT_VERSION command */
  int		    version;
  /* whether to decode MSMPEG4_V3 format
     (aka divx ;-) and divx 3.11-- thank god they dropped the smileys 
     with divx4)
  */
  int		    use_311_compat;
  /* postprocessing level; currently valid values 0-6 (internally 0-100)
     set by divx4_postproc in .xinerc */
  int		    postproc;
  /* can we handle 311 format? No easy way to find out (divx4linux can,
     OpenDivx cannot, so the user can set it in .xinerc. If 0, can_handle
     only returns MPEG4, yielding 311 to ffmpeg */
  int		    can_handle_311;
  int               skipframes;
} divx4_decoder_t;

#define VIDEOBUFSIZE 128*1024


static char* decore_retval(int ret)
{
  static char buf[128];
  switch (ret) {
  case DEC_OK: return "DEC_OK";
  case DEC_MEMORY: return "DEC_MEMORY";
  case DEC_BAD_FORMAT: return "DEC_BAD_FORMAT";
  case DEC_EXIT: return "DEC_EXIT";
  }
  sprintf(buf, "[Unknown code %d]", ret);
  return buf;	
}

/* try to get the version libdivxdecore */
static void divx4_get_version(divx4_decoder_t *this)
{
  /* if version set in xine config file, do not attempt further checking
   * (but do print a warning about this!) */
  if (this->version) {
    /* this dangerous stuff warrants an extra warning */
    printf("divx4: assuming libdivxdecore version is %d\n", this->version);
    return;
  }

#if CATCH_SIGSEGV
  /* try to catch possible segmentation fault triggered by version check.
   * old versions of OpenDivx are known to do this. 
   * we have to exit(1) in case it happens, but at least the user'll know
   * what happened */
  old_handler = signal(SIGSEGV, catch_sigsegv); 
  if (old_handler == SIG_ERR)
    printf("divx4: failed to set SIGSEGV handler for libdivxdecore version check. Danger!\n");
  /* ask decore for version, using arbitrary handle 123 */
  this->version = this->decore(123, DEC_OPT_VERSION, 0, 0);
  /* restore old signal handler */
  if (old_handler != SIG_ERR)
    signal(SIGSEGV, old_handler);
#else
  /* no SIGSEGV catching, let's hope survive this... */
  this->version = this->decore(123, DEC_OPT_VERSION, 0, 0);
#endif

  if (this->version < 100) { /* must be an error code */
    printf("divx4: libdivxdecore failed to return version number (returns %s)\n",
           decore_retval(this->version));
    this->version = 0;
  }
  printf("divx4: found divx4 or OpenDivx decore library, version %d\n", 
         this->version);
}

/* check to see if the libdivxdecore version is recent enough. 
 * returns 1 if ok, 0 if not. */
static int divx4_check_version(divx4_decoder_t *this)
{
  /* now check the version 
   * oktober '01 and later are ok. (early august releases had a (possible)
   * problem with DEC_OPT_RELEASE, which is very important currently) */
  if (this->version < 20011000) { 
    printf("divx4: libdivxdecore version \"%d\" too old. Need 20011000 or later\n"
           "divx4: see README.divx4 for details on where to find libdivxdecore.\n", 
           this->version);
    return 0; 
  }

  return 1;
}

static void divx4_set_pp(divx4_decoder_t *this) {
  DEC_SET setpp;   /* for setting postproc level */
  int ret;

#ifdef LOG
  printf ("divx4: this->decoder_ok=%d\n", this->decoder_ok);
#endif

  if (!this->decoder_ok)
    return;
    
  /* multiply postproc level by 10 for internal consumption */
  printf("divx4: Setting post processing level to %d (see ~/.xine/options)\n"
         "divx4: Valid range 0-6, reduce if you get frame drop\n", 
         this->postproc); 

  setpp.postproc_level   = this->postproc*10; 
  setpp.deblock_hor_luma = 0;
  setpp.deblock_ver_luma = 0;
  setpp.deblock_hor_chr  = 0;
  setpp.deblock_ver_chr  = 0;
  setpp.dering_luma      = 0;
  setpp.dering_chr       = 0;
  setpp.pp_semaphore     = 0;

  ret = this->decore((unsigned long)this, DEC_OPT_SETPP, &setpp, 0);
  if (ret != DEC_OK) {
    printf("divx4: decore DEC_OPT_SETPP command returned %s.\n", decore_retval(ret));
    /* perhaps not fatal, so we'll continue */
  }
}


/* helper function to initialize decore */
static int divx4_init_decoder(divx4_decoder_t *this, buf_element_t *buf) {

  DEC_PARAM param; /* for init                   */
  int ret, codec_type;

#ifdef LOG
  printf ("divx4: init_decoder\n");
#endif

  memcpy ( &this->bih, buf->content, sizeof (xine_bmiheader));
  this->video_step = buf->decoder_info[1];

  codec_type = buf->type & 0xFFFF0000;

  /* do we need divx 3.11 compatibility mode? */
  switch (buf->type & 0xFFFF0000) {
  case BUF_VIDEO_MSMPEG4_V1:
  case BUF_VIDEO_MSMPEG4_V2:
  case BUF_VIDEO_MSMPEG4_V3:
    if (this->version >= 20020303) { 
      param.codec_version=311;
      this->use_311_compat = 0;
    } else {
      this->use_311_compat = 1;
    }
    break;
  case BUF_VIDEO_MPEG4:
  case BUF_VIDEO_DIVX5:
    if (this->version >= 20020303)
      param.codec_version=500;

    this->use_311_compat = 0;
    if (this->version >= 20020303)
      param.codec_version=500;
    break;
  default:
    printf ("divx4: unknown video format (buftype: 0x%08X)\n",
	    buf->type & 0xFFFF0000);
  }

  /* setup decoder; inspired by avifile's plugin */
  param.x_dim=this->bih.biWidth;
  param.y_dim=this->bih.biHeight;
  param.time_incr = 15; /* no idea what this does */

  param.build_number=0;

  param.output_format=DEC_USER;
  param.build_number=0;
  memset(&param.buffers, 0, sizeof(param.buffers));

  ret = this->decore((unsigned long)this, DEC_OPT_INIT, &param, &this->bih);
  if (ret != DEC_OK) {
    printf("divx4: decore DEC_OPT_INIT command returned %s.\n", decore_retval(ret));
    return 0;
  }

  this->decoder_ok = 1;

  divx4_set_pp( this );
  
  return 1;
}

/* helper function to copy data from decore's internal buffer to a vo frame */
static inline void divx4_copy_frame(divx4_decoder_t *this, vo_frame_t *img, 
	DEC_PICTURE pict)
{      
  /* We need to copy the yuv data from the decoder's internal buffers.
     Y size is width*height, U and V width*height/4 */ 
  int i;
  int src_offset,dst_offset;

  /* copy y data; use shortcut if stride_y equals width */
  src_offset = 0;
  dst_offset = 0;
  if (pict.stride_y == img->pitches[0]) {
    xine_fast_memcpy(img->base[0]+dst_offset, pict.y, img->pitches[0]*this->bih.biHeight);
    dst_offset += img->pitches[0] * this->bih.biHeight;
  }
  else { /* copy line by line */
    for (i=0; i<this->bih.biHeight; i++) {
      xine_fast_memcpy(img->base[0]+dst_offset, pict.y+src_offset, this->bih.biWidth);
      src_offset += pict.stride_y;
      dst_offset += img->pitches[0];
    }
  }

  /* same for u,v data, but at 1/4 resolution.
     FIXME: Weird... I thought YV12 means order y-v-u, yet base[1] 
     seems to be u and base[2] is v. */

 /* copy u and v data */
  src_offset = 0;
  dst_offset = 0;
  if (pict.stride_uv == img->pitches[1] && pict.stride_uv == img->pitches[2]) {
    xine_fast_memcpy(img->base[1]+dst_offset, pict.u, (img->pitches[1]*this->bih.biHeight)/4);
    xine_fast_memcpy(img->base[2]+dst_offset, pict.v, (img->pitches[2]*this->bih.biHeight)/4);
    dst_offset += (this->bih.biWidth*this->bih.biHeight)/4;
  }
  else {
    int dst_offset_v = 0;
    for (i=0; i<this->bih.biHeight>>1; i++) {
      xine_fast_memcpy(img->base[1]+dst_offset, pict.u+src_offset, this->bih.biWidth/2);
      xine_fast_memcpy(img->base[2]+dst_offset_v, pict.v+src_offset, this->bih.biWidth/2);
      src_offset += pict.stride_uv;
      dst_offset += img->pitches[1];
      dst_offset_v += img->pitches[2];
    }
  } 
     
  /* check for the copy function pointer. If set, we need to call it
     with slices of 16 lines. Too bad we can't set the y,u and v
     stride values (because then we wouldn't need the first copy) */
  if (img->copy && img->bad_frame == 0) {
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

static int divx4_can_handle (video_decoder_t *this_gen, int buf_type) {
  divx4_decoder_t *this = (divx4_decoder_t *) this_gen;
  buf_type &= 0xFFFF0000;

#ifdef LOG
  printf ("divx4: can_handle\n");
#endif

  /* divx4 currently does not support MSMPEG4 v1/v2 */
  return ( (buf_type == BUF_VIDEO_MSMPEG4_V3 && this->can_handle_311) ||
           /* buf_type == BUF_VIDEO_MSMPEG4_V2 || */
           (buf_type == BUF_VIDEO_MPEG4) ||
	   (buf_type == BUF_VIDEO_DIVX5));
}

/* copied verbatim from ffmpeg plugin */
static void divx4_init (video_decoder_t *this_gen, vo_instance_t *video_out) {

  divx4_decoder_t *this = (divx4_decoder_t *) this_gen;

#ifdef LOG
  printf ("divx4: divx4_init\n");
#endif

  this->video_out  = video_out;
  this->decoder_ok = 0;
  this->buf = NULL;
}


static void divx4_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {

  DEC_FRAME frame; /* for getting a frame        */
  DEC_PICTURE pict;/* contains ptrs to the decoders internal yuv buffers */
  vo_frame_t *img; /* video out frame */
  int ret;

  divx4_decoder_t *this = (divx4_decoder_t *) this_gen;

#ifdef LOG
  printf ("divx4: decoding buffer %08x, flags = %08x\n", buf, buf->decoder_flags);
#endif 

  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;

  if (buf->decoder_flags & BUF_FLAG_HEADER) { /* need to initialize */
    /* only proceed if version is good and initialization succeeded */

#ifdef LOG
    printf ("divx4: get_version...\n");
#endif 

    divx4_get_version(this);	
    this->decoder_ok = ( divx4_check_version(this) &&
                         divx4_init_decoder (this, buf) );
    if (this->decoder_ok) {
      this->video_out->open (this->video_out);
    
      if( this->buf )
        free( this->buf );
    
      this->buf = malloc( VIDEOBUFSIZE );
      this->bufsize = VIDEOBUFSIZE;
    
      this->skipframes = 0;
    }
    return;
  }

  if (! this->decoder_ok) { /* don't try to do anything */
    /* if it is because of the version, print the warning again.
     * otherwise it's an unknown internal error. */
    if (divx4_check_version(this) != 0) /* version is good */
      printf("divx4: internal error; decoder not initialized.\n");
    return;
  }
    
  if( this->size + buf->size > this->bufsize ) {
    this->bufsize = this->size + 2 * buf->size;
    printf("divx4: increasing source buffer to %d to avoid overflow.\n", 
      this->bufsize);
    this->buf = realloc( this->buf, this->bufsize );
  }

  xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);
  this->size += buf->size;
  
  if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
    this->video_step = buf->decoder_info[0];

  if (buf->decoder_flags & BUF_FLAG_FRAME_END)  { /* need to decode a frame */
    /* allocate image (taken from ffmpeg plugin) */
    img = this->video_out->get_frame (this->video_out, this->bih.biWidth,
				      this->bih.biHeight, XINE_VO_ASPECT_DONT_TOUCH, 
				      XINE_IMGFMT_YV12, 
				      VO_BOTH_FIELDS);

    img->pts = buf->pts;
    img->duration = this->video_step;
    /* setup the decode frame parameters, as demonstrated by avifile.
       Old versions used DEC_YV12, but that was basically wrong and just
       happened to work with the Xv driver. Now DEC_USER is the only option.
    */
    frame.bitstream=this->buf;
    frame.bmp=&pict; /* decore will set ptrs to internal y,u&v buffers */
    frame.length=this->size;
    frame.render_flag=1;
    frame.stride=this->bih.biWidth;

    if(this->use_311_compat)
      ret = this->decore((unsigned long)this, DEC_OPT_FRAME_311, &frame, 0);
    else
      ret = this->decore((unsigned long)this, DEC_OPT_FRAME, &frame, 0);
      
    if (ret != DEC_OK || this->skipframes) {
      if( !this->skipframes )
        printf("divx4: decore DEC_OPT_FRAME command returned %s.\n", 
          decore_retval(ret));
      img->bad_frame = 1; /* better skip this one */
    }
    else {
      divx4_copy_frame(this, img, pict);
      img->bad_frame = 0;
    }

    this->skipframes = img->draw(img);
    if( this->skipframes < 0 )
      this->skipframes = 0;
    img->free(img);
      
    this->size = 0;
  }
}

static void divx4_close (video_decoder_t *this_gen)  {

  divx4_decoder_t *this = (divx4_decoder_t *) this_gen;

  printf ("divx4: close\n");

  if (this->decoder_ok) {
    /* FIXME: this segfaults here */
    /* Note: we NEED this; after 0.9.4, xine closes and reopens
     * decoders when seeking. If DEC_OPT_RELEASE is disabled, it will
     * cause a memory leak of plusminus 5M per shot */ 
    this->decore((unsigned long)this, DEC_OPT_RELEASE, 0, 0); 
    this->decoder_ok = 0;
    this->video_out->close(this->video_out);
  }
  
  if (this->buf)
    free(this->buf);
  this->buf = NULL;
}

static void divx4_update_postproc(void *this_gen, xine_cfg_entry_t *entry) {

  divx4_decoder_t *this = (divx4_decoder_t *) this_gen;

#ifdef LOG
  printf ("divx4: update_postproc this=0x%08x, decoder_ok = %d\n", this, this->decoder_ok);
#endif
  
  if( this->postproc != entry->num_value) {
    this->postproc = entry->num_value;
    divx4_set_pp( this );
  }
}


static char *divx4_get_id(void) {
#ifdef LOG
  printf ("divx4: get_id\n");
#endif
  return "divx4 video decoder";
}

static void divx4_flush(video_decoder_t *this_gen) {
#ifdef LOG
  printf ("divx4: flush\n");
#endif
}

static void divx4_reset(video_decoder_t *this_gen) {
  /* seems to handle seeking quite nicelly without any code here */
#ifdef LOG
  printf ("divx4: reset\n");
#endif
}

static void divx4_dispose(video_decoder_t *this_gen) {
#ifdef LOG
  printf ("divx4: dispose\n");
#endif
  free (this_gen);
}

/* This is pretty generic. I took the liberty to increase the
   priority over that of libffmpeg :-) */
static void *init_video_decoder_plugin (xine_t *xine, void *data) {

  divx4_decoder_t *this ;
  char *libdecore_name;
  void *libdecore_handle;
  decoreFunc libdecore_func = 0;
  config_values_t *cfg;

  cfg = xine->config;
  
  /* Try to dlopen libdivxdecore, then look for decore function 
     if it fails, print a message and return 0 so that xine ignores
     us from then on. */
  libdecore_name = cfg->register_string (cfg, "codec.divx4_libdivxdecore", "libdivxdecore.so",
					 _("Relative path to libdivxdecore.so to open"),
					 NULL, 0, NULL, NULL);  

  libdecore_handle = dlopen(libdecore_name, RTLD_LAZY);
  if (libdecore_handle)
    libdecore_func = dlsym(libdecore_handle, "decore"); 
  if (! libdecore_func) {
    /* no library or no decore function. this plugin can do nothing */
    return NULL;
  }

  this = (divx4_decoder_t *) malloc (sizeof (divx4_decoder_t));

  this->decoder_ok = 0;

  this->video_decoder.init                = divx4_init;
  this->video_decoder.decode_data         = divx4_decode_data;
  this->video_decoder.close               = divx4_close;
  this->video_decoder.get_identifier      = divx4_get_id;
  this->video_decoder.flush               = divx4_flush;
  this->video_decoder.reset               = divx4_reset;
  this->video_decoder.dispose             = divx4_dispose;
  this->video_decoder.priority            = cfg->register_num (cfg, "codec.divx4_priority", 4,
							       _("priority of the divx4 plugin (>5 => enable)"),
							       NULL, 0, NULL, NULL); 
  this->decore = libdecore_func;
  this->postproc 			  = cfg->register_range (cfg, "codec.divx4_postproc", 3,
								 0, 6,
								 _("the postprocessing level, 0 = none and fast, 6 = all and slow"),
								 NULL, 10, divx4_update_postproc, this);
  this->can_handle_311			  = cfg->register_bool (cfg, "codec.divx4_msmpeg4v3", 1,
								_("use divx4 plugin for msmpeg4v3 streams"),
								NULL, 10, NULL, NULL);
  this->size				  = 0;
  /* allow override of version checking by user */
  this->version 			  = cfg->register_num(cfg, "codec.divx4_forceversion", 0,
							      _("Divx version to check for (set to 0 (default) if unsure)"),
							      NULL, 20, NULL, NULL);

  /* if the version set in the config file, we can check right now. 
   * otherwise postpone until we retrieve the version from the library
   * in the first decoding call (we'll only ever get there if this
   * plugin has the highest priority, which by default it has not). */
  if ( this->version != 0 && divx4_check_version(this) == 0 ) { /* failed */
    free(this);
    return 0;
  }
  /* at the moment availabe values are 0-6, but future versions may support
     higher levels. Internally, postproc is multiplied by 10 and values 
     between 0 and 100 are valid */
  if (this->postproc > 10) this->postproc=10;
  if (this->postproc < 0) this->postproc=0;

#ifdef LOG
  printf ("divx4: this=0x%08x, decoder_ok = %d\n", this, this->decoder_ok);
#endif

  return (video_decoder_t *) this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t video_types[] = { 
  /* BUF_VIDEO_MSMPEG4_V3 && this->can_handle_311 , */
  /* BUF_VIDEO_MSMPEG4_V2, */
  BUF_VIDEO_MPEG4, BUF_VIDEO_DIVX5,
  0
 };

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  4                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 10, "divx4", XINE_VERSION_CODE, &dec_info_video, init_video_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
