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
 * $Id: audio_decoder.c,v 1.1 2002/11/22 23:37:40 guenter Exp $
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
  void                   *ra_handle;

  unsigned long (*raCloseCodec)(void*);
  unsigned long (*raDecode)(void*, char*,unsigned long,char*,unsigned int*,long);
  unsigned long (*raFlush)(unsigned long,unsigned long,unsigned long);
  unsigned long (*raFreeDecoder)(void*);
  void*         (*raGetFlavorProperty)(void*,unsigned long,unsigned long,int*);
  unsigned long (*raInitDecoder)(void*, void*);
  unsigned long (*raOpenCodec2)(void*);
  unsigned long (*raSetFlavor)(void*,unsigned long);
  void          (*raSetDLLAccessPath)(char*);
  void          (*raSetPwd)(char*,char*);

} real_class_t;

typedef struct realdec_decoder_s {
  video_decoder_t  video_decoder;

  real_class_t    *cls;

  xine_stream_t   *stream;

} realdec_decoder_t;

typedef struct {
    int    samplerate;
    short  bits;
    short  channels;
    int    unk1;
    int    unk2;
    int    packetsize;
    int    unk3;
    void  *unk4;
} ra_init_t;

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

  printf ("libareal: complete hexdump of package follows:\nlibareal:  ");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\nlibareal: ");

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");
}

static void realdec_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;
  real_class_t      *cls  = this->cls;

#ifdef LOG
  printf ("libareal: decode_data, flags=0x%08x ...\n", buf->decoder_flags);
#endif

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {

    /* real_find_sequence_header (&this->real, buf->content, buf->content + buf->size);*/

  } else if (buf->decoder_flags & BUF_FLAG_HEADER) {

    int version ;
    int samples_per_sec, bits_per_sample, num_channels;
    int subpacket_size, coded_frame_size, codec_data_length, extras;

    version = BE_16 (buf->content);

    printf ("libareal: header buffer detected, header version %d\n", version);

    subpacket_size   = BE_16 (buf->content+40);
    coded_frame_size = BE_32 (buf->content+20);
    codec_data_length= 

    if (version == 4) {
      samples_per_sec = BE_16 (buf->content+44);
      bits_per_sample = BE_16 (buf->content+48);
      num_channels    = BE_16 (buf->content+50);
    } else {
      samples_per_sec = BE_16 (buf->content+50);
      bits_per_sample = BE_16 (buf->content+54);
      num_channels    = BE_16 (buf->content+56);
    }

    printf ("libareal: %d samples/sec, %d bits/sample, %d channels\n",
	    samples_per_sec, bits_per_sample, num_channels);

#if 0
    sh->samplerate = sh->wf->nSamplesPerSec;
    sh->samplesize = sh->wf->wBitsPerSample/8;
    sh->channels   = sh->wf->nChannels;
#endif

    { 
      ra_init_t init_data={
	samples_per_sec,
	bits_per_sample,
	num_channels,
	100, /* ??? */
	((short*)(sh->wf+1))[0],  /* subpacket size    */
	((short*)(sh->wf+1))[3],  /* coded frame size  */
	((short*)(sh->wf+1))[4],  /* codec data length */
	((char*)(sh->wf+1))+10    /* extras            */
      };
      result=raInitDecoder(sh->context,&init_data);
      if(result){
	mp_msg(MSGT_DECAUDIO,MSGL_WARN,"Decoder init failed, error code: 0x%X\n",result);
	return 0;
      }
    }
    
    if (cls->raSetPwd) {
      /* used by 'SIPR' */
      cls->raSetPwd (this->context, "Ardubancel Quazanga"); /* set password... lol. */
    }
  
    result=raSetFlavor(sh->context,((short*)(sh->wf+1))[2]);
    if(result){
      mp_msg(MSGT_DECAUDIO,MSGL_WARN,"Decoder flavor setup failed, error code: 0x%X\n",result);
      return 0;
    }

    prop=raGetFlavorProperty(sh->context,((short*)(sh->wf+1))[2],0,&len);
    mp_msg(MSGT_DECAUDIO,MSGL_INFO,"Audio codec: [%d] %s\n",((short*)(sh->wf+1))[2],prop);

    prop=raGetFlavorProperty(sh->context,((short*)(sh->wf+1))[2],1,&len);
    sh->i_bps=((*((int*)prop))+4)/8;
    mp_msg(MSGT_DECAUDIO,MSGL_INFO,"Audio bitrate: %5.3f kbit/s (%d bps)  \n",(*((int*)prop))*0.001f,sh->i_bps);

//    prop=raGetFlavorProperty(sh->context,((short*)(sh->wf+1))[2],0x13,&len);
//    mp_msg(MSGT_DECAUDIO,MSGL_INFO,"Samples/block?: %d  \n",(*((int*)prop)));

  sh->audio_out_minsize=128000; // no idea how to get... :(
  sh->audio_in_minsize=((short*)(sh->wf+1))[1]*sh->wf->nBlockAlign;
  
#endif

  } else {

    printf ("libareal: content buffer detected\n");

  }

#ifdef LOG
  printf ("libareal: decode_data...done\n");
#endif
}

static void realdec_flush (video_decoder_t *this_gen) {
  realdec_decoder_t *this = (realdec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libareal: flush\n");
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
  printf ("libareal: close\n");
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

#define REALCODEC_PATH "/opt/RealPlayer8/Codecs"

static int load_syms_linux (real_class_t *cls, char *path) {

  printf ("libareal: (audio) opening shared obj '%s'\n", path);
  cls->ra_handle = dlopen (path, RTLD_LAZY);

  if (!cls->ra_handle) {
    printf ("libareal: error: %s\n", dlerror());
    return 0;
  }
  
  cls->raCloseCodec        = dlsym (cls->ra_handle, "RACloseCodec");
  cls->raDecode            = dlsym (cls->ra_handle, "RADecode");
  cls->raFlush             = dlsym (cls->ra_handle, "RAFlush");
  cls->raFreeDecoder       = dlsym (cls->ra_handle, "RAFreeDecoder");
  cls->raGetFlavorProperty = dlsym (cls->ra_handle, "RAGetFlavorProperty");
  cls->raOpenCodec2        = dlsym (cls->ra_handle, "RAOpenCodec2");
  cls->raInitDecoder       = dlsym (cls->ra_handle, "RAInitDecoder");
  cls->raSetFlavor         = dlsym (cls->ra_handle, "RASetFlavor");
  cls->raSetDLLAccessPath  = dlsym (cls->ra_handle, "SetDLLAccessPath");
  cls->raSetPwd            = dlsym (cls->ra_handle, "RASetPwd"); /* optional, used by SIPR */

  printf ("libareal: codec loaded, symbols resolved\n");
    
  if (!cls->raCloseCodec || !cls->raDecode || !cls->raFlush || !cls->raFreeDecoder ||
      !cls->raGetFlavorProperty || !cls->raOpenCodec2 || !cls->raSetFlavor ||
      /*!raSetDLLAccessPath ||*/ !cls->raInitDecoder){
    printf ("libareal: (audio) Cannot resolve symbols - incompatible dll: %s\n", 
	    path);
    return 0;
  }

  printf ("libareal: raSetDLLAccessPath\n");

  if (cls->raSetDLLAccessPath){

    char path[1024];

    sprintf(path, "DT_Codecs=" REALCODEC_PATH);
    if(path[strlen(path)-1]!='/'){
      path[strlen(path)+1]=0;
      path[strlen(path)]='/';
    }
    path[strlen(path)+1]=0;

    printf ("libareal: path=%s\n", path);

    cls->raSetDLLAccessPath(path);
  }

  printf ("libareal: audio decoder loaded successfully\n");

  return 1;
}

static void *init_class (xine_t *xine, void *data) {

  real_class_t *this;

  this = (real_class_t *) xine_xmalloc (sizeof (real_class_t));

  if (!load_syms_linux (this, "/usr/local/RealPlayer8/Codecs/sipr.so.6.0")) {
    if (!load_syms_linux (this, "/opt/RealPlayer8/Codecs/sipr.so.6.0")) {
      free (this);
      return NULL;
    }
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
