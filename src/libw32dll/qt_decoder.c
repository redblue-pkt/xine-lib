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
 * $Id: qt_decoder.c,v 1.11 2003/01/11 13:10:13 guenter Exp $
 *
 * quicktime video/audio decoder plugin, using win32 dlls
 * most of this code comes directly from MPlayer
 * authors: A'rpi and Sascha Sommer
 *
 * rv40 support by Chris Rankin <cj.rankin@ntlworld.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "bswap.h"
#include "xine_internal.h"
#include "audio_out.h"
#include "buffer.h"

#include "qtx/qtxsdk/components.h"
#include "wine/windef.h"
#include "wine/ldt_keeper.h"

/*
 * This version of the macro avoids compiler warnings about
 * multiple-character constants. It also does NOT assume
 * that an unsigned long is 32 bits wide.
 */
#ifdef FOUR_CHAR_CODE
#  undef FOUR_CHAR_CODE
#endif
#define FOUR_CHAR_CODE(a,b,c,d)  (uint32_t)( ((unsigned char)(a)<<24) | \
                                             ((unsigned char)(b)<<16) | \
                                             ((unsigned char)(c)<<8) | \
                                             (unsigned char)(d) )

/*
#define LOG
*/

/*
 *
 * part 0: common wine stuff
 * =========================
 *
 */

HMODULE   WINAPI LoadLibraryA(LPCSTR);
FARPROC   WINAPI GetProcAddress(HMODULE,LPCSTR);
int       WINAPI FreeLibrary(HMODULE);

/* some data is shared inside wine loader.
 * this mutex seems to avoid some segfaults
 */
static pthread_once_t   once_control = PTHREAD_ONCE_INIT;
static pthread_mutex_t  win32_codec_mutex;
extern char            *win32_def_path;

static void init_routine(void) {

#ifdef LOG
  printf ("qt_decoder: init_routine\n");
#endif

  pthread_mutex_init (&win32_codec_mutex, NULL);

#ifdef LOG
  printf ("qt_decoder: init_routine completed\n");
#endif
}

#define BUFSIZE 1024*1024

/*
 *
 * part 1: audio decoder
 * =====================
 *
 */

typedef struct OpaqueSoundConverter*    SoundConverter;
typedef unsigned long                   UnsignedFixed;
typedef uint8_t                          Byte;
typedef struct SoundComponentData {
    long                            flags;
    OSType                          format;
    short                           numChannels;
    short                           sampleSize;
    UnsignedFixed                   sampleRate;
    long                            sampleCount;
    Byte *                          buffer;
    long                            reserved;
}SoundComponentData;


typedef int (__cdecl* LPFUNC1)(long flag);
typedef int (__cdecl* LPFUNC2)(const SoundComponentData *, 
			       const SoundComponentData *,
			       SoundConverter *);
typedef int (__cdecl* LPFUNC3)(SoundConverter sc);
typedef int (__cdecl* LPFUNC4)(void);
typedef int (__cdecl* LPFUNC5)(SoundConverter sc, OSType selector,void * infoPtr);
typedef int (__cdecl* LPFUNC6)(SoundConverter sc, 
			       unsigned long inputBytesTarget,
			       unsigned long *inputFrames,
			       unsigned long *inputBytes,
			       unsigned long *outputBytes );
typedef int (__cdecl* LPFUNC7)(SoundConverter sc, 
			       const void    *inputPtr, 
			       unsigned long inputFrames,
			       void          *outputPtr,
			       unsigned long *outputFrames,
			       unsigned long *outputBytes );
typedef int (__cdecl* LPFUNC8)(SoundConverter sc,
			       void      *outputPtr,
			       unsigned long *outputFrames,
			       unsigned long *outputBytes);
typedef int (__cdecl* LPFUNC9)(SoundConverter sc) ;        


typedef struct {
  audio_decoder_class_t   decoder_class;
} qta_class_t;

typedef struct qta_decoder_s {
  audio_decoder_t     audio_decoder;

  int                 codec_initialized;
  int                 output_open;

  xine_stream_t      *stream;

  HINSTANCE           qtml_dll; 

  xine_waveformatex   wave;
  uint8_t             out_buf[1000000];

  LPFUNC1             InitializeQTML;
  LPFUNC2             SoundConverterOpen;
  LPFUNC3             SoundConverterClose;
  LPFUNC4             TerminateQTML;
  LPFUNC5             SoundConverterSetInfo;
  LPFUNC6             SoundConverterGetBufferSizes;
  LPFUNC7             SoundConverterConvertBuffer;
  LPFUNC8             SoundConverterEndConversion;
  LPFUNC9             SoundConverterBeginConversion;

  SoundConverter      myConverter;
  SoundComponentData  InputFormatInfo, OutputFormatInfo;

  int                 InFrameSize;
  int                 OutFrameSize;
  long                FramesToGet;

  int                 frame_size;

  uint8_t             data[BUFSIZE];
  int                 data_len;
  int                 num_frames;

} qta_decoder_t;

#ifdef LOG
static void qta_hexdump (char *buf, int length) {

  int i;

  printf ("qt_audio: ascii contents>");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    if ((c >= 32) && (c <= 128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");

  printf ("qt_audio: complete hexdump of package follows:\nqt_audio: 0x0000:  ");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\nqt_audio: 0x%04x: ", i+1);

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");
}
#endif

static void qta_init_driver (qta_decoder_t *this, buf_element_t *buf) {

  int           error;
  unsigned long InputBufferSize=0;  /* size of the input buffer */
  unsigned long OutputBufferSize=0; /* size of the output buffer */
  unsigned long WantedBufferSize=0; /* the size you want your buffers to be */
  int           mode;

  this->FramesToGet = 0;

#ifdef LOG
  printf ("qt_audio: init_driver... (trying to lock mutex...)\n");
#endif

  pthread_mutex_lock(&win32_codec_mutex);

#ifdef LOG
  printf ("qt_audio: init_driver... (mutex locked)\n");
#endif

  Setup_LDT_Keeper();
  
  this->qtml_dll = LoadLibraryA("qtmlClient.dll");
  
  if (this->qtml_dll == NULL ) {
    printf ("qt_audio: failed to load dll\n" );
    return;
  }

  this->InitializeQTML = (LPFUNC1)GetProcAddress (this->qtml_dll, "InitializeQTML");
  if ( this->InitializeQTML == NULL )  {
    printf ("qt_audio: failed geting proc address InitializeQTML\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
  this->SoundConverterOpen = (LPFUNC2)GetProcAddress (this->qtml_dll, "SoundConverterOpen");
  if ( this->SoundConverterOpen == NULL ) {
    printf ("qt_audio: failed getting proc address SoundConverterOpen\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
  this->SoundConverterClose = (LPFUNC3)GetProcAddress (this->qtml_dll, "SoundConverterClose");
  if ( this->SoundConverterClose == NULL ) {
    printf ("qt_audio: failed getting proc address SoundConverterClose\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
  this->TerminateQTML = (LPFUNC4)GetProcAddress (this->qtml_dll, "TerminateQTML");
  if ( this->TerminateQTML == NULL ) {
    printf ("qt_audio: failed getting proc address TerminateQTML\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
  this->SoundConverterSetInfo = (LPFUNC5)GetProcAddress (this->qtml_dll, "SoundConverterSetInfo");
  if ( this->SoundConverterSetInfo == NULL ) {
    printf ("qt_audio: failed getting proc address SoundConverterSetInfo\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
  this->SoundConverterGetBufferSizes = (LPFUNC6)GetProcAddress (this->qtml_dll, "SoundConverterGetBufferSizes");
  if ( this->SoundConverterGetBufferSizes == NULL ) {
    printf ("qt_audio: failed getting proc address SoundConverterGetBufferSizes\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
  this->SoundConverterConvertBuffer = (LPFUNC7)GetProcAddress (this->qtml_dll, "SoundConverterConvertBuffer");
  if ( this->SoundConverterConvertBuffer == NULL ) {
    printf ("qt_audio: failed getting proc address SoundConverterConvertBuffer1\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
  this->SoundConverterEndConversion = (LPFUNC8)GetProcAddress (this->qtml_dll, "SoundConverterEndConversion");
  if ( this->SoundConverterEndConversion == NULL ) {
    printf ("qt_audio: failed getting proc address SoundConverterEndConversion\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
  this->SoundConverterBeginConversion = (LPFUNC9)GetProcAddress (this->qtml_dll, "SoundConverterBeginConversion");
  if ( this->SoundConverterBeginConversion == NULL ) {
    printf ("qt_audio: failed getting proc address SoundConverterBeginConversion\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
#ifdef LOG
  printf ("qt_audio: Standard init done you may now call supported functions\n");
#endif

  error = this->InitializeQTML(6+16);
#ifdef LOG
  printf ("qt_audio: InitializeQTML:%i\n",error);
#endif
  if (error) {
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }

  this->OutputFormatInfo.flags       = this->InputFormatInfo.flags       = 0;
  this->OutputFormatInfo.sampleCount = this->InputFormatInfo.sampleCount = 0;
  this->OutputFormatInfo.buffer      = this->InputFormatInfo.buffer      = NULL;
  this->OutputFormatInfo.reserved    = this->InputFormatInfo.reserved    = 0;
  this->OutputFormatInfo.numChannels = this->InputFormatInfo.numChannels = this->wave.nChannels;
  this->InputFormatInfo.sampleSize   = this->wave.wBitsPerSample;
  this->OutputFormatInfo.sampleSize  = 16;
  this->OutputFormatInfo.sampleRate  = this->InputFormatInfo.sampleRate  = this->wave.nSamplesPerSec;

  switch (buf->type) {
  case BUF_AUDIO_QDESIGN1:
    this->InputFormatInfo.format = FOUR_CHAR_CODE('Q','D','M','1');
    break;
  case BUF_AUDIO_QDESIGN2:
    this->InputFormatInfo.format = FOUR_CHAR_CODE('Q','D','M','2');
    break;
  default:
    printf ("qt_audio: fourcc for buftype %08x ?\n", buf->type);
    abort ();
  }

  this->OutputFormatInfo.format = FOUR_CHAR_CODE('N','O','N','E');

#ifdef LOG
  printf ("qt_audio: input format:\n");
  qta_hexdump (&this->InputFormatInfo, sizeof (SoundComponentData));
  printf ("qt_audio: output format:\n");
  qta_hexdump (&this->OutputFormatInfo, sizeof (SoundComponentData));
  printf ("qt_audio: stsd atom: \n");
  qta_hexdump ((unsigned char *)buf->decoder_info_ptr[2], buf->decoder_info[2]);
#endif

  error = this->SoundConverterOpen (&this->InputFormatInfo, 
				    &this->OutputFormatInfo, 
				    &this->myConverter);
#ifdef LOG
  printf ("qt_audio: SoundConverterOpen:%i\n",error);
#endif
  if (error) {
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }

  if (buf->decoder_info[2] > 0x48) {
    error = this->SoundConverterSetInfo (this->myConverter,
					 FOUR_CHAR_CODE('w','a','v','e'),
					 ((unsigned char *)buf->decoder_info_ptr[2]) + 0x48);
#ifdef LOG
    printf ("qt_audio: SoundConverterSetInfo:%i\n",error);
#endif
    if (error) {
      pthread_mutex_unlock(&win32_codec_mutex);

      return;
    }
  }

  WantedBufferSize = this->OutputFormatInfo.numChannels*this->OutputFormatInfo.sampleRate*2;
  error = this->SoundConverterGetBufferSizes (this->myConverter,
					      WantedBufferSize, &this->FramesToGet,
					      &InputBufferSize, &OutputBufferSize);
#ifdef LOG
  printf ("qt_audio: SoundConverterGetBufferSizes:%i\n", error);
  printf ("qt_audio: WantedBufferSize = %li\n", WantedBufferSize);
  printf ("qt_audio: InputBufferSize  = %li\n", InputBufferSize);
  printf ("qt_audio: OutputBufferSize = %li\n", OutputBufferSize);
  printf ("qt_audio: this->FramesToGet = %li\n", this->FramesToGet);
#endif    

  this->InFrameSize   = (InputBufferSize+this->FramesToGet-1)/this->FramesToGet;
  this->OutFrameSize  = OutputBufferSize/this->FramesToGet;

#ifdef LOG
  printf ("qt_audio: FrameSize: %i -> %i\n", this->InFrameSize, this->OutFrameSize);
#endif    

  error = this->SoundConverterBeginConversion (this->myConverter);
#ifdef LOG
  printf ("qt_audio: SoundConverterBeginConversion:%i\n",error);
#endif    
  if (error) {    
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }

#ifdef LOG
  printf ("qt_audio: opening output.\n");
#endif    

  switch (this->wave.nChannels) {
  case 1: 
    mode = AO_CAP_MODE_MONO;
    break;
  case 2: 
    mode = AO_CAP_MODE_STEREO;
    break;
  case 4: 
    mode = AO_CAP_MODE_4CHANNEL;
    break;
  case 5: 
    mode = AO_CAP_MODE_5CHANNEL;
    break;
  case 6: 
    mode = AO_CAP_MODE_5_1CHANNEL;
    break;
  default:
    printf ("qt_audio: help, %d channels ?!\n",
	    this->wave.nChannels);
    abort ();
  }

  this->frame_size = this->wave.nChannels * this->wave.wBitsPerSample / 8;

  this->output_open = this->stream->audio_out->open(this->stream->audio_out, 
						    this->stream,
						    this->wave.wBitsPerSample,
						    this->wave.nSamplesPerSec,
						    mode) ;

  this->codec_initialized = 1;

#ifdef LOG
  printf ("qt_audio: mutex unlock\n");
#endif    

  pthread_mutex_unlock(&win32_codec_mutex);
}

static void qta_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  qta_decoder_t *this = (qta_decoder_t *) this_gen;

#ifdef LOG
  printf ("qt_audio: decode buf=%08x %d bytes flags=%08x pts=%lld\n",
	  buf, buf->size, buf->decoder_flags, buf->pts);
#endif

  if (buf->decoder_flags & BUF_FLAG_HEADER) {

    memcpy (&this->wave, buf->content, sizeof (xine_waveformatex));

    this->wave.nChannels      = buf->decoder_info[3];
    this->wave.wBitsPerSample = buf->decoder_info[2];
    this->wave.nSamplesPerSec = buf->decoder_info[1];

#ifdef LOG 
    printf ("qt_audio: header copied\n");
#endif
    
  } else if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
#ifdef LOG 
    printf ("qt_audio: special buffer\n");
#endif
    
    if (buf->decoder_info[1] == BUF_SPECIAL_STSD_ATOM) {
      
#ifdef LOG 
      printf ("qt_audio: got stsd atom -> init codec\n");
#endif

      if (!this->codec_initialized) {
	qta_init_driver (this, buf);
      }
      
      if (!this->codec_initialized)
        this->stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED] = 0;
    }
  } else if( this->codec_initialized ) {

    memcpy (&this->data[this->data_len], buf->content, buf->size);
    this->data_len += buf->size;

    if ((this->InFrameSize != 0) && (this->data_len > this->InFrameSize)) {

      int num_frames = this->data_len / this->InFrameSize;
      long out_frames, out_bytes;
      int error, frames_left, bytes_sent;

      pthread_mutex_lock(&win32_codec_mutex);
      error = this->SoundConverterConvertBuffer (this->myConverter,
						 this->data,
						 num_frames, 
						 this->out_buf,
						 &out_frames, &out_bytes);
      pthread_mutex_unlock(&win32_codec_mutex);

#ifdef LOG
      printf ("qt_audio: decoded %d frames => %d frames (error %d)\n",
	      num_frames, out_frames, error);
#endif

      this->data_len -= this->InFrameSize * num_frames;
      if (this->data_len>0)
	memmove (this->data, this->data+num_frames*this->InFrameSize, this->data_len);
      

      frames_left = out_frames;
      bytes_sent = 0;
      while (frames_left>0) {

	audio_buffer_t *audio_buffer;
	int             nframes;

	audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);

	nframes = audio_buffer->mem_size / this->frame_size;

	if (nframes > frames_left)
	  nframes = frames_left;

	memcpy (audio_buffer->mem, this->out_buf+bytes_sent, nframes * this->frame_size);

	audio_buffer->vpts = buf->pts;
	buf->pts = 0;  /* only the first buffer gets the real pts */
	audio_buffer->num_frames = nframes;

#ifdef LOG
	printf ("qt_audio: sending %d frames, %d frames left\n",
		nframes, frames_left);
#endif

	this->stream->audio_out->put_buffer (this->stream->audio_out, 
					     audio_buffer, this->stream);
	
	bytes_sent += nframes*this->frame_size;
	frames_left -= nframes;
      }

    }
    
  }
}

static void qta_reset (audio_decoder_t *this_gen) {
}


static void qta_discontinuity (audio_decoder_t *this_gen) {
}

static void qta_dispose (audio_decoder_t *this_gen) {

  qta_decoder_t *this = (qta_decoder_t *) this_gen; 
  int error;
  unsigned long ConvertedFrames=0;
  unsigned long ConvertedBytes=0;
  error = this->SoundConverterEndConversion (this->myConverter,NULL,
					     &ConvertedFrames,&ConvertedBytes);
#ifdef LOG
  printf ("qt_audio: SoundConverterEndConversion:%i\n",error);
#endif
  error = this->SoundConverterClose (this->myConverter);
#ifdef LOG
  printf ("qt_audio: SoundConverterClose:%i\n",error);
#endif

  if (this->output_open)
    this->stream->audio_out->close (this->stream->audio_out, this->stream);
  this->output_open = 0;

  free (this);
}

static audio_decoder_t *qta_open_plugin (audio_decoder_class_t *class_gen, 
					 xine_stream_t *stream) {

  qta_decoder_t *this ;

  this = (qta_decoder_t *) xine_xmalloc (sizeof (qta_decoder_t));

  this->audio_decoder.decode_data         = qta_decode_data;
  this->audio_decoder.reset               = qta_reset;
  this->audio_decoder.discontinuity       = qta_discontinuity;
  this->audio_decoder.dispose             = qta_dispose;
  this->stream                            = stream;

  this->output_open     = 0;

  return (audio_decoder_t *) this;
}

/*
 * qta plugin class
 */

static char *qta_get_identifier (audio_decoder_class_t *this) {
  return "qta";
}

static char *qta_get_description (audio_decoder_class_t *this) {
  return "quicktime audio decoder plugin";
}

static void qta_dispose_class (audio_decoder_class_t *this) {
  free (this);
}

static void *qta_init_class (xine_t *xine, void *data) {

  qta_class_t     *this;
  config_values_t *cfg;

  pthread_once (&once_control, init_routine);

  this = (qta_class_t *) malloc (sizeof (qta_class_t));

  this->decoder_class.open_plugin     = qta_open_plugin;
  this->decoder_class.get_identifier  = qta_get_identifier;
  this->decoder_class.get_description = qta_get_description;
  this->decoder_class.dispose         = qta_dispose_class;

  cfg = xine->config;
  win32_def_path = cfg->register_string (cfg, "codec.win32_path", 
					 "/usr/lib/win32",
					 _("path to win32 codec dlls"),
					 NULL, 0, NULL, NULL);

  return this;
}

static uint32_t audio_types[] = { 
  BUF_AUDIO_QDESIGN1, BUF_AUDIO_QDESIGN2, 0
};

static decoder_info_t qta_dec_info = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

/*
 *
 * part 2: video decoder
 * =====================
 *
 */

typedef struct {
  video_decoder_class_t   decoder_class;

  char                   *qt_codec_path;

} qtv_class_t;


typedef struct qtv_decoder_s {
  video_decoder_t  video_decoder;

  qtv_class_t     *cls;

  xine_stream_t   *stream;

  HINSTANCE        qtml_dll; 

  xine_bmiheader   bih;

  int              codec_initialized;

  uint8_t         *plane;

  uint8_t          data[BUFSIZE];
  int              data_len;

  /* ComponentDescription desc; */         /* for FindNextComponent() */
  ComponentInstance       ci;              /* codec handle */
  /* CodecInfo cinfo;*/	                   /* for ImageCodecGetCodecInfo() */
  /* Component prev=NULL; */
  /* ComponentResult cres;  */
  CodecCapabilities       codeccap;        /* for decpar */
  CodecDecompressParams   decpar;          /* for ImageCodecPreDecompress() */
  /* ImageSubCodecDecompressCapabilities icap; // for ImageCodecInitialize() */
  Rect                    OutBufferRect;   /* the dimensions of our GWorld */

  GWorldPtr               OutBufferGWorld; /* a GWorld is some kind of 
					      description for a drawing 
					      environment */
  ImageDescriptionHandle  framedescHandle;

  /* function pointers */

  Component         (*FindNextComponent)    (Component prev,
					     ComponentDescription* desc);
  OSErr             (*GetComponentInfo)     (Component prev,
					     ComponentDescription* desc,
					     Handle h1,Handle h2,Handle h3);
  long              (*CountComponents)      (ComponentDescription* desc);
  OSErr             (*InitializeQTML)       (long flags);
  OSErr             (*EnterMovies)          (void);
  ComponentInstance (*OpenComponent)        (Component c);
  ComponentResult   (*ImageCodecInitialize) (ComponentInstance ci,
					     ImageSubCodecDecompressCapabilities * cap);
  ComponentResult   (*ImageCodecBeginBand)  (ComponentInstance ci,
					     CodecDecompressParams * params,
					     ImageSubCodecDecompressRecord * drp,
					     long flags);
  ComponentResult   (*ImageCodecDrawBand)   (ComponentInstance ci,
					     ImageSubCodecDecompressRecord * drp);
  ComponentResult   (*ImageCodecEndBand)    (ComponentInstance ci,
					     ImageSubCodecDecompressRecord * drp,
					     OSErr result, long flags);
  ComponentResult   (*ImageCodecGetCodecInfo) (ComponentInstance ci,
					       CodecInfo *info);
  ComponentResult   (*ImageCodecPreDecompress)(ComponentInstance ci,
					       CodecDecompressParams * params);
  ComponentResult   (*ImageCodecBandDecompress)(ComponentInstance      ci,
						CodecDecompressParams * params);
  PixMapHandle      (*GetGWorldPixMap)         (GWorldPtr offscreenGWorld);                  
  OSErr             (*QTNewGWorldFromPtr)(GWorldPtr *gw,
					  OSType pixelFormat,
					  const Rect *boundsRect,
					  CTabHandle cTable,
					  /*GDHandle*/void* aGDevice, /*unused*/
					  GWorldFlags flags,
					  void *baseAddr,
					  long rowBytes); 
  OSErr             (*NewHandleClear)(Size byteCount);

} qtv_decoder_t;

#ifdef LOG
static void qtv_hexdump (char *buf, int length) {

  int i;

  printf ("qt_video: ascii contents>");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    if ((c >= 32) && (c <= 128))
      printf ("%c", c);
    else
      printf (".");
  }
  printf ("\n");

  printf ("qt_video: complete hexdump of package follows:\nqt_video: 0x0000:  ");
  for (i = 0; i < length; i++) {
    unsigned char c = buf[i];

    printf ("%02x", c);

    if ((i % 16) == 15)
      printf ("\nqt_video: 0x%04x: ", i+1);

    if ((i % 2) == 1)
      printf (" ");

  }
  printf ("\n");
}
#endif

/*
 * qt codec loader
 */

static void qtv_init_driver (qtv_decoder_t *this, buf_element_t *buf) {

  long                                result = 1;
  ComponentResult                     cres;
  ComponentDescription                desc;
  Component                           prev=NULL;
  CodecInfo                           cinfo;	/* for ImageCodecGetCodecInfo() */
  ImageSubCodecDecompressCapabilities icap; /* for ImageCodecInitialize() */
  ImageDescription                   *id;

#ifdef LOG
  printf ("qt_video: init_driver... (trying to lock mutex...)\n");
#endif

  pthread_mutex_lock(&win32_codec_mutex);

#ifdef LOG
  printf ("qt_video: mutex locked\n");
#endif

  Setup_LDT_Keeper();
  
  this->qtml_dll = LoadLibraryA("qtmlClient.dll");
  
  if (this->qtml_dll == NULL ) {
    printf ("qt_video: failed to load dll\n" );
    return;
  }

  this->InitializeQTML = GetProcAddress (this->qtml_dll, "InitializeQTML");
  this->EnterMovies = GetProcAddress (this->qtml_dll, "EnterMovies");
  this->FindNextComponent = GetProcAddress (this->qtml_dll, "FindNextComponent");
  this->CountComponents = GetProcAddress (this->qtml_dll, "CountComponents");
  this->GetComponentInfo = GetProcAddress (this->qtml_dll, "GetComponentInfo");
  this->OpenComponent = GetProcAddress (this->qtml_dll, "OpenComponent");
  this->ImageCodecInitialize = GetProcAddress (this->qtml_dll, "ImageCodecInitialize");
  this->ImageCodecGetCodecInfo = GetProcAddress (this->qtml_dll, "ImageCodecGetCodecInfo");
  this->ImageCodecBeginBand = GetProcAddress (this->qtml_dll, "ImageCodecBeginBand");
  this->ImageCodecPreDecompress = GetProcAddress (this->qtml_dll, "ImageCodecPreDecompress");
  this->ImageCodecBandDecompress = GetProcAddress (this->qtml_dll, "ImageCodecBandDecompress");
  this->GetGWorldPixMap = GetProcAddress (this->qtml_dll, "GetGWorldPixMap");
  this->QTNewGWorldFromPtr = GetProcAddress (this->qtml_dll, "QTNewGWorldFromPtr");
  this->NewHandleClear = GetProcAddress (this->qtml_dll, "NewHandleClear");
    
  if (!this->InitializeQTML || !this->EnterMovies || !this->FindNextComponent 
      || !this->ImageCodecBandDecompress){
    printf ("qt_video: invalid qt DLL!\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }

#ifdef LOG
  printf ("qt_video: calling InitializeQTML...\n");
#endif

  result = this->InitializeQTML(6+16);
  /*    result=InitializeQTML(0); */
#ifdef LOG
  printf("qt_video: InitializeQTML returned %d\n",result);
#endif
  /*    result=EnterMovies(); */
  /*    printf("EnterMovies->%d\n",result); */

  memset(&desc,0,sizeof(desc));
  desc.componentType = FOUR_CHAR_CODE('i','m','d','c');
  desc.componentSubType = FOUR_CHAR_CODE('S','V','Q','3');

  desc.componentManufacturer=0;
  desc.componentFlags=0;
  desc.componentFlagsMask=0;

#ifdef LOG
  printf("qt_video: Count = %d\n", this->CountComponents(&desc));
#endif

  prev = this->FindNextComponent(NULL,&desc);
  if(!prev){
    printf("Cannot find requested component\n");
    pthread_mutex_unlock(&win32_codec_mutex);
    return;
  }
#ifdef LOG
  printf ("qt_video: Found it! ID = 0x%X\n",prev);
#endif

  this->ci = this->OpenComponent(prev);
  printf ("qt_video: this->ci=%p\n",this->ci);

  memset (&icap,0,sizeof(icap));
  cres = this->ImageCodecInitialize (this->ci, &icap);
#ifdef LOG
  printf ("qt_video: ImageCodecInitialize->%p  size=%d (%d)\n",
	  cres,icap.recordSize,icap.decompressRecordSize);
#endif

  memset(&cinfo,0,sizeof(cinfo));
  cres = this->ImageCodecGetCodecInfo (this->ci, &cinfo);
#ifdef LOG
  printf ("qt_video: Flags: compr: 0x%X  decomp: 0x%X format: 0x%X\n",
	  cinfo.compressFlags, cinfo.decompressFlags, cinfo.formatFlags);
  printf ("qt_video: Codec name: %.*s\n", ((unsigned char*)&cinfo.typeName)[0],
	  ((unsigned char*)&cinfo.typeName)+1);
#endif

  /* make a yuy2 gworld */
  this->OutBufferRect.top    = 0;
  this->OutBufferRect.left   = 0;
  this->OutBufferRect.right  = this->bih.biWidth;
  this->OutBufferRect.bottom = this->bih.biHeight;

#ifdef LOG
  printf ("qt_video: image size %d x %d\n",
	  this->bih.biWidth,  this->bih.biHeight);

  printf ("qt_video: stsd (%d bytes):\n", buf->decoder_info[2]) ;

  qtv_hexdump ((unsigned char *)buf->decoder_info_ptr[2], buf->decoder_info[2]);
#endif

  {
    uint8_t *stdata = ((unsigned char *)buf->decoder_info_ptr[2]) + 20;
    int      stdata_len = buf->decoder_info[2] - 24;

    id=malloc (8+stdata_len) ; /* trak->stdata_len); */
    id->idSize          = 8+stdata_len;
    id->cType           = FOUR_CHAR_CODE('S','V','Q','3');
    id->version         = BE_16 (stdata+ 8);
    id->revisionLevel   = BE_16 (stdata+10);
    id->vendor          = BE_32 (stdata+12);
    id->temporalQuality = BE_32 (stdata+16);
    id->spatialQuality  = BE_32 (stdata+20);
    id->width           = BE_16 (stdata+24);
    id->height          = BE_16 (stdata+26);
    id->hRes            = BE_32 (stdata+28);
    id->vRes            = BE_32 (stdata+32);
    id->dataSize        = BE_32 (stdata+36);
    id->frameCount      = BE_16 (stdata+40);
    memcpy(&id->name,stdata+42,32);
    id->depth           = BE_16 (stdata+74);
    id->clutID          = BE_16 (stdata+76);
    if (stdata_len>78)	
      memcpy (((char*)&id->clutID)+2, stdata+78, stdata_len-78);

#ifdef LOG
    printf ("qt_video: id (%d bytes)\n", stdata_len);
    qtv_hexdump (id, stdata_len);
#endif

  }

#ifdef LOG
  printf ("qt_video: ImageDescription size: %d\n",
	  id->idSize);

  qtv_hexdump (id, id->idSize);
#endif

  this->framedescHandle = (ImageDescriptionHandle) this->NewHandleClear (id->idSize);

#ifdef LOG
  printf ("qt_video: framedescHandle = %x\n",
	  this->framedescHandle);
#endif

  memcpy (*this->framedescHandle, id, id->idSize);

  /*
   * alloc video plane
   */

  this->plane = malloc (this->bih.biWidth * this->bih.biHeight * 3);

  result = this->QTNewGWorldFromPtr(&this->OutBufferGWorld,  
				    kYUVSPixelFormat, /*pixel format of new GWorld==YUY2 */
				    &this->OutBufferRect,   /*we should benchmark if yvu9 is faster for svq3, too */
				    0, 
				    0, 
				    0, 
				    this->plane,
				    this->bih.biWidth*2);

#ifdef LOG
  printf ("qt_video: NewGWorldFromPtr returned:%d\n",
	  65536-(result&0xffff));
#endif

  this->decpar.imageDescription = this->framedescHandle;
  this->decpar.startLine        = 0;
  this->decpar.stopLine         = (**this->framedescHandle).height;
  this->decpar.frameNumber      = 1;
  this->decpar.matrixFlags      = 0;
  this->decpar.matrixType       = 0;
  this->decpar.matrix           = 0;
  this->decpar.capabilities     = &this->codeccap;
  this->decpar.accuracy         = codecNormalQuality;
  this->decpar.port             = this->OutBufferGWorld;
  this->decpar.srcRect          = this->OutBufferRect;
      
  this->decpar.transferMode     = srcCopy;
  this->decpar.dstPixMap        = **this->GetGWorldPixMap (this->OutBufferGWorld);/*destPixmap;  */
      
  cres = this->ImageCodecPreDecompress (this->ci, &this->decpar);
#ifdef LOG
  printf ("qt_video: ImageCodecPreDecompress cres=0x%X\n", cres);
#endif
      
  this->data_len = 0;

  this->codec_initialized = 1;

  this->stream->video_out->open (this->stream->video_out, this->stream);

  pthread_mutex_unlock(&win32_codec_mutex);

}

static void qtv_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  qtv_decoder_t *this = (qtv_decoder_t *) this_gen;

#ifdef LOG
  printf ("qt_video: decode_data, flags=0x%08x, len=%d, pts=%lld ...\n", 
	  buf->decoder_flags, buf->size, buf->pts);
#endif

  if (buf->decoder_flags & BUF_FLAG_HEADER) {

#ifdef LOG 
    printf ("qt_video: copying bih\n");
#endif

    memcpy (&this->bih, buf->content, sizeof (xine_bmiheader));

  } else if (buf->decoder_flags & BUF_FLAG_SPECIAL) {
#ifdef LOG 
    printf ("qt_video: special buffer\n");
#endif
    
    if (buf->decoder_info[1] == BUF_SPECIAL_STSD_ATOM) {
      
#ifdef LOG 
      printf ("qt_video: got stsd atom -> init codec\n");
#endif

      if (!this->codec_initialized) {
	qtv_init_driver (this, buf);
      }
      if (!this->codec_initialized)
        this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 0;
    }
  } else if (this->codec_initialized) {

#ifdef LOG 
    printf ("qt_video: actual image data\n");
#endif

    memcpy (&this->data[this->data_len], buf->content, buf->size);
    this->data_len += buf->size;

#ifdef LOG 
    printf ("qt_video: got %d bytes in buffer\n", this->data_len);
#endif
    
    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {

      ComponentResult  cres;
      vo_frame_t      *img;

      pthread_mutex_lock(&win32_codec_mutex);

      this->decpar.data       = this->data;
      this->decpar.bufferSize = this->data_len;
      (**this->framedescHandle).dataSize=this->data_len;

      cres = this->ImageCodecBandDecompress (this->ci, &this->decpar);
    
      ++this->decpar.frameNumber;

      pthread_mutex_unlock(&win32_codec_mutex);

      if (cres&0xFFFF){
	printf("qt_video: ImageCodecBandDecompress cres=0x%X (-0x%X) %d :(\n",
	       cres,-cres,cres);
      }

      img = this->stream->video_out->get_frame (this->stream->video_out,
						this->bih.biWidth,
						this->bih.biHeight,
						42, 
						XINE_IMGFMT_YUY2,
						VO_BOTH_FIELDS);
	
      img->pts       = buf->pts;
      img->duration  = buf->decoder_info[0];
      img->bad_frame = 0;
	
      xine_fast_memcpy (img->base[0], this->plane, 
			this->bih.biWidth*this->bih.biHeight*2);
	
      img->draw(img, this->stream);
      img->free(img);

      this->data_len = 0;
    }
  }

#ifdef LOG
  printf ("qt_video: decode_data...done\n");
#endif
}

static void qtv_flush (video_decoder_t *this_gen) {
  /* qtv_decoder_t *this = (qtv_decoder_t *) this_gen; */

#ifdef LOG
  printf ("qt_video: flush\n");
#endif

}

static void qtv_reset (video_decoder_t *this_gen) {
  /* qtv_decoder_t *this = (qtv_decoder_t *) this_gen; */

}

static void qtv_discontinuity (video_decoder_t *this_gen) {
  /* qtv_decoder_t *this = (qtv_decoder_t *) this_gen; */

}

static void qtv_dispose (video_decoder_t *this_gen) {

  qtv_decoder_t *this = (qtv_decoder_t *) this_gen;

  if (this->codec_initialized) {
    this->stream->video_out->close(this->stream->video_out, this->stream);
    this->codec_initialized = 0;
  }

#ifdef LOG
  printf ("qt_video: dispose\n");
#endif

  free (this);
}

static video_decoder_t *qtv_open_plugin (video_decoder_class_t *class_gen, 
					 xine_stream_t *stream) {

  qtv_class_t   *cls = (qtv_class_t *) class_gen;
  qtv_decoder_t *this ;

  this = (qtv_decoder_t *) xine_xmalloc (sizeof (qtv_decoder_t));

  this->video_decoder.decode_data         = qtv_decode_data;
  this->video_decoder.flush               = qtv_flush;
  this->video_decoder.reset               = qtv_reset;
  this->video_decoder.discontinuity       = qtv_discontinuity;
  this->video_decoder.dispose             = qtv_dispose;
  this->stream                            = stream;
  this->cls                               = cls;

  return &this->video_decoder;
}


/*
 * qtv plugin class
 */

static char *qtv_get_identifier (video_decoder_class_t *this) {
  return "qtvdec";
}

static char *qtv_get_description (video_decoder_class_t *this) {
  return "quicktime binary-only codec based video decoder plugin";
}

static void qtv_dispose_class (video_decoder_class_t *this) {
  free (this);
}

/*
 * some fake functions to make qt codecs happy 
 */

#if 0
static void codec_path_cb (void *data, xine_cfg_entry_t *cfg) {
  qtv_class_t *this = (qt_class_t *) data;
  
  this->qt_codec_path = cfg->str_value;
}
#endif

static void *qtv_init_class (xine_t *xine, void *data) {

  qtv_class_t        *this;
  config_values_t    *cfg = xine->config; 

  win32_def_path = cfg->register_string (cfg, "codec.win32_path", "/usr/lib/win32",
					 _("path to win32 codec dlls"),
					 NULL, 0, NULL, NULL);

#ifdef LOG
  printf ("qtv_init_class...\n");
#endif

  pthread_once (&once_control, init_routine);

  this = (qtv_class_t *) xine_xmalloc (sizeof (qtv_class_t));

  this->decoder_class.open_plugin     = qtv_open_plugin;
  this->decoder_class.get_identifier  = qtv_get_identifier;
  this->decoder_class.get_description = qtv_get_description;
  this->decoder_class.dispose         = qtv_dispose_class;

  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t qtv_supported_types[] = { BUF_VIDEO_SORENSON_V3, 0 };

static decoder_info_t qtv_dec_info = {
  qtv_supported_types,     /* supported types */
  6                        /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 14, "qtv", XINE_VERSION_CODE, &qtv_dec_info, qtv_init_class },
  { PLUGIN_AUDIO_DECODER | PLUGIN_MUST_PRELOAD, 13, "qta", XINE_VERSION_CODE, &qta_dec_info, qta_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

