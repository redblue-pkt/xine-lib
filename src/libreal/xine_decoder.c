/*
 * Copyright (C) 2000-2004 the xine project
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
 * $Id: xine_decoder.c,v 1.74 2004/12/08 17:10:29 miguelfreitas Exp $
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
#ifdef __x86_64__
  #include <elf.h>
#endif

#define LOG_MODULE "real_decoder"
#define LOG_VERBOSE
/*
#define LOG
*/
#include "bswap.h"
#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"
#include "xineutils.h"

typedef struct {
  video_decoder_class_t   decoder_class;

  /* empty so far */
} real_class_t;

#define BUF_SIZE       65536

typedef struct realdec_decoder_s {
  video_decoder_t  video_decoder;

  real_class_t    *cls;

  xine_stream_t   *stream;

  void            *rv_handle;

  uint32_t        (*rvyuv_custom_message)(uint32_t*, void*);
  uint32_t        (*rvyuv_free)(void*);
  uint32_t        (*rvyuv_hive_message)(uint32_t, uint32_t);
  uint32_t        (*rvyuv_init)(void*, void*); /* initdata,context */
  uint32_t        (*rvyuv_transform)(char*, char*, uint32_t*, uint32_t*,void*);

  void            *context;

  uint32_t         width, height;
  double           ratio;
  double           fps;

  uint8_t         *chunk_buffer;
  int              chunk_buffer_size;
  int              chunk_buffer_max;

  int64_t          pts;
  int              duration;

  uint8_t         *frame_buffer;
  int              frame_size;
  int              decoder_ok;

} realdec_decoder_t;

/* we need exact positions */
typedef struct {
  int16_t  unk1;
  int16_t  w;
  int16_t  h;
  int16_t  unk3;
  int32_t  unk2;
  int32_t  subformat;
  int32_t  unk5;
  int32_t  format;
} rv_init_t;


void *__builtin_vec_new(uint32_t size);
void __builtin_vec_delete(void *mem);
void __pure_virtual(void);

#ifdef __x86_64__
/* (gb) quick-n-dirty check to be run natively */
static int is_x86_64_object_(FILE *f)
{
  Elf64_Ehdr *hdr = malloc(sizeof(Elf64_Ehdr));
  if (hdr == NULL)
	return 0;

  if (fseek(f, 0, SEEK_SET) != 0) {
	free(hdr);
	return 0;
  }

  if (fread(hdr, sizeof(Elf64_Ehdr), 1, f) != 1) {
	free(hdr);
	return 0;
  }

  if (hdr->e_ident[EI_MAG0] != ELFMAG0 ||
	  hdr->e_ident[EI_MAG1] != ELFMAG1 ||
	  hdr->e_ident[EI_MAG2] != ELFMAG2 ||
	  hdr->e_ident[EI_MAG3] != ELFMAG3) {
	free(hdr);
	return 0;
  }

  return hdr->e_machine == EM_X86_64;
}

static inline int is_x86_64_object(const char *filename)
{
  FILE *f;
  int ret;

  if ((f = fopen(filename, "r")) == NULL)
	return 0;

  ret = is_x86_64_object_(f);
  fclose(f);
  return ret;
}
#endif

/*
 * real codec loader
 */

static int load_syms_linux (realdec_decoder_t *this, char *codec_name) {

  cfg_entry_t* entry = this->stream->xine->config->lookup_entry(
			 this->stream->xine->config, "codec.real_codecs_path");
  char path[1024];

  snprintf (path, sizeof(path), "%s/%s", entry->str_value, codec_name);

#ifdef __x86_64__
  /* check whether it's a real x86-64 library */
  if (!is_x86_64_object(path))
	return 0;
#endif

  lprintf ("opening shared obj '%s'\n", path);

  this->rv_handle = dlopen (path, RTLD_LAZY);

  if (!this->rv_handle) {
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, "libreal: error: %s\n", dlerror());
    _x_message(this->stream, XINE_MSG_LIBRARY_LOAD_ERROR,
                 codec_name, NULL);
    return 0;
  }
  
  this->rvyuv_custom_message = dlsym (this->rv_handle, "RV20toYUV420CustomMessage");
  this->rvyuv_free           = dlsym (this->rv_handle, "RV20toYUV420Free");
  this->rvyuv_hive_message   = dlsym (this->rv_handle, "RV20toYUV420HiveMessage");
  this->rvyuv_init           = dlsym (this->rv_handle, "RV20toYUV420Init");
  this->rvyuv_transform      = dlsym (this->rv_handle, "RV20toYUV420Transform");
  
  if (this->rvyuv_custom_message &&
      this->rvyuv_free &&
      this->rvyuv_hive_message &&
      this->rvyuv_init &&
      this->rvyuv_transform) 
    return 1;

  xprintf (this->stream->xine, XINE_VERBOSITY_LOG, 
	   _("libreal: Error resolving symbols! (version incompatibility?)\n"));
  return 0;
}

static int init_codec (realdec_decoder_t *this, buf_element_t *buf) {

  /* unsigned int* extrahdr = (unsigned int*) (buf->content+28); */
  int           result;
  rv_init_t     init_data = {11, 0, 0, 0, 0, 0, 1, 0}; /* rv30 */


  switch (buf->type) {
  case BUF_VIDEO_RV20:
    _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Real Video 2.0");
    if (!load_syms_linux (this, "drv2.so.6.0"))
      return 0;
    break;
  case BUF_VIDEO_RV30:
    _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Real Video 3.0");
    if (!load_syms_linux (this, "drv3.so.6.0"))
      return 0;
    break;
  case BUF_VIDEO_RV40:
    _x_meta_info_set(this->stream, XINE_META_INFO_VIDEOCODEC, "Real Video 4.0");
    if (!load_syms_linux(this, "drv4.so.6.0"))
      return 0;
    break;
  default:
    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG, 
	     "libreal: error, i don't handle buf type 0x%08x\n", buf->type);
    _x_abort();
  }

  init_data.w = BE_16(&buf->content[12]);
  init_data.h = BE_16(&buf->content[14]);
  
  this->width  = (init_data.w + 1) & (~1);
  this->height = (init_data.h + 1) & (~1);
  
  if(buf->decoder_flags & BUF_FLAG_ASPECT)
    this->ratio = (double)buf->decoder_info[1] / (double)buf->decoder_info[2];
  else
    this->ratio  = (double)this->width / (double)this->height;

  /* While the framerate is stored in the header it sometimes doesn't bear
   * much resemblence to the actual frequency of frames in the file. Hence
   * it's better to just let the engine estimate the frame duration for us */ 
#if 0
  this->fps      = (double) BE_16(&buf->content[22]) + 
                   ((double) BE_16(&buf->content[24]) / 65536.0);
  this->duration = 90000.0 / this->fps;
#endif
  
  lprintf("this->ratio=%d\n", this->ratio);
  
  lprintf ("init_data.w=%d(0x%x), init_data.h=%d(0x%x),"
	   "this->width=%d(0x%x), this->height=%d(0x%x)\n",
	   init_data.w, init_data.w,
	   init_data.h, init_data.h,
	   this->width, this->width, this->height, this->height);

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH,  this->width);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_RATIO,  this->ratio*10000);
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->duration);

  init_data.subformat = BE_32(&buf->content[26]);
  init_data.format    = BE_32(&buf->content[30]);
  
#ifdef LOG
  printf ("libreal: init_data for rvyuv_init:\n");
  xine_hexdump ((char *) &init_data, sizeof (init_data));
  
  printf ("libreal: buf->content\n");
  xine_hexdump (buf->content, buf->size);
#endif  
  lprintf ("init codec %dx%d... %x %x\n", 
	   init_data.w, init_data.h,
	   init_data.subformat, init_data.format );
  
  this->context = NULL;
  
  result = this->rvyuv_init (&init_data, &this->context); 
  
  lprintf ("init result: %d\n", result);

  /* setup rv30 codec (codec sub-type and image dimensions): */
  if ((init_data.format>=0x20200002) && (buf->type != BUF_VIDEO_RV40)) {
    int       i, j;
    uint32_t *cmsg24;
    uint32_t  cmsg_data[9];

    cmsg24 = xine_xmalloc((buf->size - 34 + 2) * sizeof(uint32_t));
    
    cmsg24[0] = this->width;
    cmsg24[1] = this->height;
    for(i = 2, j = 34; j < buf->size; i++, j++)
      cmsg24[i] = 4 * buf->content[j];
    
    cmsg_data[0] = 0x24;
    cmsg_data[1] = 1 + ((init_data.subformat >> 16) & 7);
    cmsg_data[2] = (uint32_t) cmsg24;

#ifdef LOG
    printf ("libreal: CustomMessage cmsg_data:\n");
    xine_hexdump ((uint8_t *) cmsg_data, sizeof (cmsg_data));
    printf ("libreal: cmsg24:\n");
    xine_hexdump ((uint8_t *) cmsg24, (buf->size - 34 + 2) * sizeof(uint32_t));
#endif
    
    this->rvyuv_custom_message (cmsg_data, this->context);
    
    free(cmsg24);
  }
  
  this->stream->video_out->open(this->stream->video_out, this->stream);
    
  this->frame_size   = this->width * this->height;
  this->frame_buffer = xine_xmalloc (this->width * this->height * 3 / 2);
  
  this->chunk_buffer = xine_xmalloc (BUF_SIZE);
  this->chunk_buffer_max = BUF_SIZE;
  
  return 1;
}

static void realdec_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  lprintf ("decode_data, flags=0x%08x, len=%d, pts=%lld ...\n", 
           buf->decoder_flags, buf->size, buf->pts);

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
    /* real_find_sequence_header (&this->real, buf->content, buf->content + buf->size);*/
    return;
  }
  
  if (buf->decoder_flags & BUF_FLAG_FRAMERATE) {
    this->duration = buf->decoder_info[0];
    _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, 
                         this->duration);
  }

  if (buf->decoder_flags & BUF_FLAG_HEADER) {

    this->decoder_ok = init_codec (this, buf);
    if( !this->decoder_ok )
      _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HANDLED, 0);

  } else if (this->decoder_ok && this->context) {
  
    /* Each frame starts with BUF_FLAG_FRAME_START and ends with
     * BUF_FLAG_FRAME_END.
     * The last buffer contains the chunk offset table.
     */

    if (!(buf->decoder_flags & BUF_FLAG_SPECIAL)) {
    
      lprintf ("buffer (%d bytes)\n", buf->size);

      if (buf->decoder_flags & BUF_FLAG_FRAME_START) {
        /* new frame starting */

        this->chunk_buffer_size = 0;
        this->pts = buf->pts;
        lprintf ("new frame starting, pts=%lld\n", this->pts);
      }
      
      if ((this->chunk_buffer_size + buf->size) > this->chunk_buffer_max) {
        lprintf("increasing chunk buffer size\n");
      
        this->chunk_buffer_max *= 2;
        this->chunk_buffer = realloc(this->chunk_buffer, this->chunk_buffer_max);
      }

      xine_fast_memcpy (this->chunk_buffer + this->chunk_buffer_size,
                        buf->content,
                        buf->size);

      this->chunk_buffer_size += buf->size;

    } else {
      /* end of frame, chunk table */
     
      lprintf ("special buffer (%d bytes)\n", buf->size);
      
      if (buf->decoder_info[1] == BUF_SPECIAL_RV_CHUNK_TABLE) {

        int            result;
        vo_frame_t    *img;

        uint32_t       transform_out[5];
        uint32_t       transform_in[6];

        lprintf ("chunk table\n");

        transform_in[0] = this->chunk_buffer_size; /* length of the packet (sub-packets appended) */
        transform_in[1] = 0;                       /* unknown, seems to be unused  */
        transform_in[2] = buf->decoder_info[2];    /* number of sub-packets - 1 */
        transform_in[3] = (uint32_t) buf->decoder_info_ptr[2]; /* table of sub-packet offsets */
        transform_in[4] = 0;                       /* unknown, seems to be unused  */
        transform_in[5] = this->pts / 90;          /* timestamp (the integer value from the stream) */

#ifdef LOG
        printf ("libreal: got %d chunks\n",
                buf->decoder_info[2] + 1);

        printf ("libreal: decoding %d bytes:\n", this->chunk_buffer_size);
        xine_hexdump (this->chunk_buffer, this->chunk_buffer_size);

        printf ("libreal: transform_in:\n");
        xine_hexdump ((uint8_t *) transform_in, 6 * 4);

        printf ("libreal: chunk_table:\n");
        xine_hexdump ((uint8_t *) buf->decoder_info_ptr[2], 
                      2*(buf->decoder_info[2]+1)*sizeof(uint32_t));
#endif

        result = this->rvyuv_transform (this->chunk_buffer,
                                        this->frame_buffer,
                                        transform_in,
                                        transform_out,
                                        this->context);

        lprintf ("transform result: %08x\n", result);
        lprintf ("transform_out:\n");
  #ifdef LOG
        xine_hexdump ((uint8_t *) transform_out, 5 * 4);
  #endif

        /* Sometimes the stream contains video of a different size
         * to that specified in the realmedia header */
        if(transform_out[0] && ((transform_out[3] != this->width) ||
                                (transform_out[4] != this->height))) {
          this->width  = transform_out[3];
          this->height = transform_out[4];

          this->frame_size = this->width * this->height;

          _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_WIDTH, this->width);
          _x_stream_info_set(this->stream, XINE_STREAM_INFO_VIDEO_HEIGHT, this->height);
        }

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                                  /* this->av_picture.linesize[0],  */
                                                  this->width,
                                                  this->height,
                                                  this->ratio,
                                                  XINE_IMGFMT_YV12,
                                                  VO_BOTH_FIELDS);

        img->pts       = this->pts;
        img->duration  = this->duration;
        _x_stream_info_set(this->stream, XINE_STREAM_INFO_FRAME_DURATION, this->duration);
        img->bad_frame = 0;

        yv12_to_yv12(
         /* Y */
          this->frame_buffer, this->width,
          img->base[0], img->pitches[0],
         /* U */
          this->frame_buffer + this->frame_size, this->width/2,
          img->base[1], img->pitches[1],
         /* V */
          this->frame_buffer + this->frame_size * 5/4, this->width/2,
          img->base[2], img->pitches[2],
         /* width x height */
          this->width, this->height);

        img->draw(img, this->stream);
        img->free(img);

      } else {
        /* unsupported special buf */
      }
    }
  }

  lprintf ("decode_data...done\n");
}

static void realdec_flush (video_decoder_t *this_gen) {
  /* realdec_decoder_t *this = (realdec_decoder_t *) this_gen; */

  lprintf ("flush\n");
}

static void realdec_reset (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;
  
  this->chunk_buffer_size = 0;
}

static void realdec_discontinuity (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;
  
  this->pts = 0;
}

static void realdec_dispose (video_decoder_t *this_gen) {

  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

  lprintf ("dispose\n");

  if (this->context)
    this->stream->video_out->close(this->stream->video_out, this->stream);

  if (this->rvyuv_free && this->context)
    this->rvyuv_free (this->context);

  if (this->rv_handle) 
    dlclose (this->rv_handle);

  if (this->frame_buffer)
    free (this->frame_buffer);
    
  if (this->chunk_buffer)
    free (this->chunk_buffer);
    
  free (this);

  lprintf ("dispose done\n");
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, 
				     xine_stream_t *stream) {

  real_class_t      *cls = (real_class_t *) class_gen;
  realdec_decoder_t *this ;

  this = (realdec_decoder_t *) xine_xmalloc (sizeof (realdec_decoder_t));

  this->video_decoder.decode_data         = realdec_decode_data;
  this->video_decoder.flush               = realdec_flush;
  this->video_decoder.reset               = realdec_reset;
  this->video_decoder.discontinuity       = realdec_discontinuity;
  this->video_decoder.dispose             = realdec_dispose;
  this->stream                            = stream;
  this->cls                               = cls;

  this->context    = 0;
  this->pts        = 0;

  this->duration   = 0;

  return &this->video_decoder;
}

/*
 * real plugin class
 */

static char *get_identifier (video_decoder_class_t *this) {
  return "realvdec";
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
void *__builtin_vec_new(uint32_t size) {
  return malloc(size);
}
void __builtin_vec_delete(void *mem) {
  free(mem);
}
void __pure_virtual(void) {
  lprintf("libreal: FATAL: __pure_virtual() called!\n");
  /*      exit(1); */
}


static void *init_class (xine_t *xine, void *data) {

  real_class_t       *this;
  config_values_t    *config = xine->config;
  char               *real_codec_path;
  char               *default_real_codec_path = "";
  struct stat s;

  this = (real_class_t *) xine_xmalloc (sizeof (real_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  /* try some auto-detection */

  if (!stat ("/usr/local/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
    default_real_codec_path = "/usr/local/RealPlayer8/Codecs";
  if (!stat ("/usr/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
    default_real_codec_path = "/usr/RealPlayer8/Codecs";
  if (!stat ("/usr/lib/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
    default_real_codec_path = "/usr/lib/RealPlayer8/Codecs";
  if (!stat ("/opt/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
    default_real_codec_path = "/opt/RealPlayer8/Codecs";
  if (!stat ("/usr/lib/RealPlayer9/users/Real/Codecs/drv3.so.6.0", &s)) 
    default_real_codec_path = "/usr/lib/RealPlayer9/users/Real/Codecs";
  if (!stat ("/usr/lib64/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
    default_real_codec_path = "/usr/lib64/RealPlayer8/Codecs";
  if (!stat ("/usr/lib64/RealPlayer9/users/Real/Codecs/drv3.so.6.0", &s)) 
    default_real_codec_path = "/usr/lib64/RealPlayer9/users/Real/Codecs";
  if (!stat ("/usr/lib/win32/drv3.so.6.0", &s)) 
    default_real_codec_path = "/usr/lib/win32";
  
  real_codec_path = config->register_string (config, "codec.real_codecs_path", 
					     default_real_codec_path,
					     _("path to RealPlayer codecs"),
					     _("If you have RealPlayer installed, specify the path "
					       "to its codec directory here. You can easily find "
					       "the codec directory by looking for a file named "
					       "\"drv3.so.6.0\" in it. If xine can find the RealPlayer "
					       "codecs, it will use them to decode RealPlayer content "
					       "for you. Consult the xine FAQ for more information on "
					       "how to install the codecs."),
					     10, NULL, this);

  lprintf ("real codec path : %s\n",  real_codec_path);

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t supported_types[] = { BUF_VIDEO_RV20,
                                      BUF_VIDEO_RV30,
                                      BUF_VIDEO_RV40,
                                      0 };

static decoder_info_t dec_info_real = {
  supported_types,     /* supported types */
  7                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 18, "real", XINE_VERSION_CODE, &dec_info_real, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
