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
 * $Id: audio_decoder.c,v 1.11 2002/12/15 21:49:14 guenter Exp $
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

/*
#define LOG
*/

typedef struct {
  video_decoder_class_t   decoder_class;

  char                   *real_codec_path;

} real_class_t;

typedef struct realdec_decoder_s {
  video_decoder_t  video_decoder;

  real_class_t    *cls;

  xine_stream_t   *stream;

  void            *ra_handle;

  unsigned long  (*raCloseCodec)(void*);
  unsigned long  (*raDecode)(void*, char*,unsigned long,char*,unsigned int*,long);
  unsigned long  (*raFlush)(unsigned long,unsigned long,unsigned long);
  unsigned long  (*raFreeDecoder)(void*);
  void*          (*raGetFlavorProperty)(void*,unsigned long,unsigned long,int*);
  unsigned long  (*raInitDecoder)(void*, void*);
  unsigned long  (*raOpenCodec2)(void*);
  unsigned long  (*raSetFlavor)(void*,unsigned long);
  void           (*raSetDLLAccessPath)(char*);
  void           (*raSetPwd)(char*,char*);

  void            *context;

  int              sps, w, h;
  int              block_align;

  uint8_t         *frame_buffer;
  uint8_t         *frame_reordered;
  int              frame_size;
  int              frame_num_bytes;

  int              sample_size;

  uint64_t         pts;

} realdec_decoder_t;

typedef struct {
    int    samplerate;
    short  bits;
    short  channels;
    int    unk1;
    int    subpacket_size;
    int    coded_frame_size;
    int    codec_data_length;
    void  *extras;
} ra_init_t;

#ifdef LOG
static void hexdump (char *buf, int length) {

  int i;

  printf ("libareal: ascii contents>");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    if ((c >= 32) && (c <= 128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");

  printf ("libareal: complete hexdump of package follows:\nlibareal 0x0000:  ");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\nlibareal 0x%04x: ", i+1);

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");
}
#endif

void *__builtin_new(unsigned long size) {
  return malloc(size);
}

static int load_syms_linux (realdec_decoder_t *this, char *codec_name) {

  char path[1024];

  sprintf (path, "%s/%s", this->cls->real_codec_path, codec_name);

#ifdef LOG
  printf ("libareal: (audio) opening shared obj '%s'\n", path);

#endif

  this->ra_handle = dlopen (path, RTLD_LAZY);

  if (!this->ra_handle) {
    printf ("libareal: error: %s\n", dlerror());
    return 0;
  }
  
  this->raCloseCodec        = dlsym (this->ra_handle, "RACloseCodec");
  this->raDecode            = dlsym (this->ra_handle, "RADecode");
  this->raFlush             = dlsym (this->ra_handle, "RAFlush");
  this->raFreeDecoder       = dlsym (this->ra_handle, "RAFreeDecoder");
  this->raGetFlavorProperty = dlsym (this->ra_handle, "RAGetFlavorProperty");
  this->raOpenCodec2        = dlsym (this->ra_handle, "RAOpenCodec2");
  this->raInitDecoder       = dlsym (this->ra_handle, "RAInitDecoder");
  this->raSetFlavor         = dlsym (this->ra_handle, "RASetFlavor");
  this->raSetDLLAccessPath  = dlsym (this->ra_handle, "SetDLLAccessPath");
  this->raSetPwd            = dlsym (this->ra_handle, "RASetPwd"); /* optional, used by SIPR */

  if (!this->raCloseCodec || !this->raDecode || !this->raFlush || !this->raFreeDecoder ||
      !this->raGetFlavorProperty || !this->raOpenCodec2 || !this->raSetFlavor ||
      /*!raSetDLLAccessPath ||*/ !this->raInitDecoder){
    printf ("libareal: (audio) Cannot resolve symbols - incompatible dll: %s\n", 
	    path);
    return 0;
  }

  if (this->raSetDLLAccessPath){

    char path[1024];

    sprintf(path, "DT_Codecs=%s", this->cls->real_codec_path);
    if (path[strlen(path)-1]!='/'){
      path[strlen(path)+1]=0;
      path[strlen(path)]='/';
    }
    path[strlen(path)+1]=0;

    this->raSetDLLAccessPath(path);
  }

#ifdef LOG
  printf ("libareal: audio decoder loaded successfully\n");
#endif

  return 1;
}

static int init_codec (realdec_decoder_t *this, buf_element_t *buf) {

  int   version, result ;
  int   samples_per_sec, bits_per_sample, num_channels;
  int   subpacket_size, coded_frame_size, codec_data_length;
  int   coded_frame_size2, data_len, flavor;
  int   mode;
  void *extras;
  
  /*
   * extract header data
   */

  version = BE_16 (buf->content);

#ifdef LOG
  printf ("libareal: header buffer detected, header version %d\n", version);
  hexdump (buf->content, buf->size);
#endif
    
  flavor           = BE_16 (buf->content+18);
  coded_frame_size = BE_32 (buf->content+20);
  codec_data_length= BE_16 (buf->content+36);
  coded_frame_size2= BE_16 (buf->content+38);
  subpacket_size   = BE_16 (buf->content+40);
    
  this->sps        = subpacket_size;
  this->w          = coded_frame_size2;
  this->h          = codec_data_length;

  if (version == 4) {
    samples_per_sec = BE_16 (buf->content+44);
    bits_per_sample = BE_16 (buf->content+48);
    num_channels    = BE_16 (buf->content+50);

    /* FIXME: */
    if (buf->type==BUF_AUDIO_COOK) {
      
      printf ("libareal: audio header version 4 for COOK audio not supported.\n");
      abort();
    }
    data_len        = 0; /* FIXME: COOK audio needs this */
    extras          = buf->content+0x43;

  } else {
    samples_per_sec = BE_16 (buf->content+50);
    bits_per_sample = BE_16 (buf->content+54);
    num_channels    = BE_16 (buf->content+56);
    data_len        = BE_32 (buf->content+0x46);
    extras          = buf->content+0x4a;
  }

  this->block_align= coded_frame_size2;

#ifdef LOG
  printf ("libareal: 0x%04x 0x%04x 0x%04x 0x%04x data_len 0x%04x\n",
	  subpacket_size, coded_frame_size, codec_data_length, 
	  coded_frame_size2, data_len);
  printf ("libareal: %d samples/sec, %d bits/sample, %d channels\n",
	  samples_per_sec, bits_per_sample, num_channels);
#endif

  /* load codec, resolv symbols */

  switch (buf->type) {
  case BUF_AUDIO_COOK:
    if (!load_syms_linux (this, "cook.so.6.0"))
      return 0;

    this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("Cook");
    break;
    
  case BUF_AUDIO_ATRK:
    if (!load_syms_linux (this, "atrc.so.6.0"))
      return 0;
    this->block_align = 384;
    this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("Atrac");
    break;

  case BUF_AUDIO_14_4:
    if (!load_syms_linux (this, "14_4.so.6.0"))
      return 0;
    this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("Real 14.4");
    break;

  case BUF_AUDIO_28_8:
    if (!load_syms_linux (this, "28_8.so.6.0"))
      return 0;
    this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("Real 28.8");
    break;

  case BUF_AUDIO_SIPRO:
    if (!load_syms_linux (this, "sipr.so.6.0"))
      return 0;
    /* this->block_align = 19; */
    this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("Sipro");
    break;

  default:
    printf ("libareal: error, i don't handle buf type 0x%08x\n",
	    buf->type);
    abort();
  }

  /*
   * init codec
   */

  result = this->raOpenCodec2 (&this->context);
  if (result) {
    printf ("libareal: error in raOpenCodec2: %d\n", result);
    return 0;
  }

  { 
    ra_init_t init_data={
      samples_per_sec,
      bits_per_sample,
      num_channels,
      100, /* ??? */
      subpacket_size,    /* subpacket size    */
      coded_frame_size,  /* coded frame size  */
      data_len,          /* codec data length */
      extras             /* extras            */
    };


#ifdef LOG
    printf ("libareal: init_data:\n");
    hexdump (&init_data, sizeof (ra_init_t));
    printf ("libareal: extras :\n");
    hexdump (init_data.extras, data_len);
#endif
     
    result = this->raInitDecoder (this->context, &init_data);
    if(result){
      printf ("libareal: decoder init failed, error code: 0x%x\n",
	      result);
      return 0;
    }
  }

  if (this->raSetPwd){
    /* used by 'SIPR' */
    this->raSetPwd (this->context, "Ardubancel Quazanga"); /* set password... lol. */
    printf ("libareal: password set\n");
  }

  result = this->raSetFlavor (this->context, flavor);
  if (result){
    printf ("libareal: decoder flavor setup failed, error code: 0x%x\n",
	    result);
    return 0;
  }

  /*
   * alloc buffers for data reordering
   */

  if (this->sps) {

    this->frame_size      = this->w/this->sps*this->h*this->sps;
    this->frame_buffer    = xine_xmalloc (this->frame_size);
    this->frame_reordered = xine_xmalloc (this->frame_size);
    this->frame_num_bytes = 0;

  } else {

    this->frame_size      = this->w*this->h;
    this->frame_buffer    = xine_xmalloc (this->frame_size);
    this->frame_reordered = this->frame_buffer;
    this->frame_num_bytes = 0;

  }

  /*
   * open audio output
   */

  switch (num_channels) {
  case 1:
    mode = AO_CAP_MODE_MONO;
    break;
  case 2:
    mode = AO_CAP_MODE_STEREO;
    break;
  default:
    printf ("libareal: oups, real can do more than 2 channels ?\n");
    abort();
  }

  this->stream->audio_out->open(this->stream->audio_out, 
				this->stream,
				bits_per_sample,
				samples_per_sec,
				mode) ;

  this->sample_size = num_channels * (bits_per_sample>>3);

  return 1;
}

static unsigned char sipr_swaps[38][2]={
    {0,63},{1,22},{2,44},{3,90},{5,81},{7,31},{8,86},{9,58},{10,36},{12,68},
    {13,39},{14,73},{15,53},{16,69},{17,57},{19,88},{20,34},{21,71},{24,46},
    {25,94},{26,54},{28,75},{29,50},{32,70},{33,92},{35,74},{38,85},{40,56},
    {42,87},{43,65},{45,59},{48,79},{49,93},{51,89},{55,95},{61,76},{67,83},
    {77,80} };

static void realdec_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libareal: decode_data %d bytes, flags=0x%08x, pts=%lld ...\n", 
	  buf->size, buf->decoder_flags, buf->pts);
#endif

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {

    /* real_find_sequence_header (&this->real, buf->content, buf->content + buf->size);*/

  } else if (buf->decoder_flags & BUF_FLAG_HEADER) {

    init_codec (this, buf) ;

  } else {

    int size;

#ifdef LOG
    printf ("libareal: content buffer detected, %d bytes\n", buf->size);
#endif

    if (buf->pts && !this->pts)
      this->pts = buf->pts;

    size = buf->size;

    while (size) {

      int needed;

      needed = this->frame_size - this->frame_num_bytes;

      if (needed>size) {
	
	memcpy (this->frame_buffer+this->frame_num_bytes, buf->content, size);
	this->frame_num_bytes += size;

#ifdef LOG
	printf ("libareal: buffering %d/%d bytes\n", this->frame_num_bytes, this->frame_size);
#endif

	size = 0;

      } else {

	int result;
	int len     =-1;
	int n;
	int sps     = this->sps;
	int w       = this->w;
	int h       = this->h;
	audio_buffer_t *audio_buffer;

#ifdef LOG
	printf ("libareal: buffering %d bytes\n", needed);
#endif

	memcpy (this->frame_buffer+this->frame_num_bytes, buf->content, needed);

	size -= needed;
	this->frame_num_bytes = 0;

#ifdef LOG
	printf ("libareal: frame completed. reordering...\n");
	printf ("libareal: bs=%d  sps=%d  w=%d h=%d \n",/*sh->wf->nBlockAlign*/-1,sps,w,h);
#endif

	if (!sps) {

	  int            j,n;
	  int            bs=h*w*2/96; /* nibbles per subpacket */
	  unsigned char *p=this->frame_buffer;
	  
	  /* 'sipr' way */
	  /* demux_read_data(sh->ds, p, h*w); */
	  for (n=0;n<38;n++){
	    int i=bs*sipr_swaps[n][0];
	    int o=bs*sipr_swaps[n][1];
	    /* swap nibbles of block 'i' with 'o'      TODO: optimize */
	    for (j=0;j<bs;j++) {
	      int x=(i&1) ? (p[(i>>1)]>>4) : (p[(i>>1)]&15);
	      int y=(o&1) ? (p[(o>>1)]>>4) : (p[(o>>1)]&15);
	      if (o&1) 
		p[(o>>1)]=(p[(o>>1)]&0x0F)|(x<<4);
	      else  
		p[(o>>1)]=(p[(o>>1)]&0xF0)|x;

	      if (i&1) 
		p[(i>>1)]=(p[(i>>1)]&0x0F)|(y<<4);
	      else  
		p[(i>>1)]=(p[(i>>1)]&0xF0)|y;

	      ++i;
	      ++o;
	    }
	  }
	  /*
	  sh->a_in_buffer_size=
	    sh->a_in_buffer_len=w*h;
	  */

	} else {
	  int      x, y;
	  uint8_t *s;
	  
	  /*  'cook' way */
	  
	  w /= sps; s = this->frame_buffer;
	  
	  for (y=0; y<h; y++)
	    
	    for (x=0; x<w; x++) {
	      
#ifdef LOG
	      printf ("libareal: x=%d, y=%d, off %d\n",
		      x, y, sps*(h*x+((h+1)/2)*(y&1)+(y>>1)));
#endif
	      
	      memcpy (this->frame_reordered+sps*(h*x+((h+1)/2)*(y&1)+(y>>1)),
		      s, sps);
	      s+=sps;
	      
	      /* demux_read_data(sh->ds, sh->a_in_buffer+sps*(h*x+((h+1)/2)*(y&1)+(y>>1)), 
		 sps); */
	      
	    }
	  /*
	    sh->a_in_buffer_size=
	    sh->a_in_buffer_len=w*h*sps;
	  */
	}

#ifdef LOG
	hexdump (this->frame_reordered, buf->size);
#endif
  
	n = 0;
	while (n<this->frame_size) {

	  audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);

	  result = this->raDecode (this->context, 
				   this->frame_reordered+n,
				   this->block_align,
				   audio_buffer->mem, &len, -1);

#ifdef LOG
	  printf ("libareal: raDecode result %d, len=%d\n", result, len);
#endif

	  audio_buffer->vpts       = this->pts; 

	  this->pts = 0;

	  audio_buffer->num_frames = len/this->sample_size;;
	  
	  this->stream->audio_out->put_buffer (this->stream->audio_out, 
					       audio_buffer, this->stream);
	  n+=this->block_align;
	}
      }
    }
  }


#ifdef LOG
  printf ("libareal: decode_data...done\n");
#endif
}

static void realdec_flush (video_decoder_t *this_gen) {
  /* realdec_decoder_t *this = (realdec_decoder_t *) this_gen; */

#ifdef LOG
  printf ("libareal: flush\n");
#endif

}

static void realdec_reset (video_decoder_t *this_gen) {
  /* realdec_decoder_t *this = (realdec_decoder_t *) this_gen; */

}

static void realdec_discontinuity (video_decoder_t *this_gen) {
  /* realdec_decoder_t *this = (realdec_decoder_t *) this_gen; */

}

static void realdec_dispose (video_decoder_t *this_gen) {

  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libareal: close\n");
#endif

  if (this->ra_handle)
    dlclose (this->ra_handle);

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

  return &this->video_decoder;
}

/*
 * real plugin class
 */

static char *get_identifier (video_decoder_class_t *this) {
  return "realadec";
}

static char *get_description (video_decoder_class_t *this) {
  return "real binary-only codec based audio decoder plugin";
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
  printf("libareal: FATAL: __pure_virtual() called!\n");
  /*      exit(1); */
}

/*
 * real audio codec loader
 */

static void codec_path_cb (void *data, xine_cfg_entry_t *cfg) {
  real_class_t *this = (real_class_t *) data;
  
  this->real_codec_path = cfg->str_value;
}

static void *init_class (xine_t *xine, void *data) {

  real_class_t       *this;
  config_values_t    *config = xine->config;

  this = (real_class_t *) xine_xmalloc (sizeof (real_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  this->real_codec_path = config->register_string (config, "codec.real_codecs_path", 
						   "unknown",
						   _("path to real player codecs, if installed"),
						   NULL, 10, codec_path_cb, (void *)this);
  
  if (!strcmp (this->real_codec_path, "unknown")) {

    struct stat s;

    /* try some auto-detection */

    if (!stat ("/usr/local/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
      config->update_string (config, "codec.real_codecs_path", 
			     "/usr/local/RealPlayer8/Codecs");
    if (!stat ("/usr/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
      config->update_string (config, "codec.real_codecs_path", 
			     "/usr/RealPlayer8/Codecs");
    if (!stat ("/opt/RealPlayer8/Codecs/drv3.so.6.0", &s)) 
      config->update_string (config, "codec.real_codecs_path", 
			     "/opt/RealPlayer8/Codecs");
  }

#ifdef LOG
  printf ("libareal: real codec path : %s\n",  this->real_codec_path);
#endif

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t audio_types[] = { 
  BUF_AUDIO_COOK, BUF_AUDIO_ATRK, BUF_AUDIO_14_4, BUF_AUDIO_28_8, BUF_AUDIO_SIPRO, 0
 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 12, "realadec", XINE_VERSION_CODE, &dec_info_audio, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
