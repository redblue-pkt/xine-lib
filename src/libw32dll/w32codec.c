/* 
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: w32codec.c,v 1.8 2001/06/21 17:34:23 guenter Exp $
 *
 * routines for using w32 codecs
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "wine/msacm.h"
#include "wine/driver.h"
#include "wine/avifmt.h"
#include "wine/vfw.h"
#include "wine/mmreg.h"

#include "video_out.h"
#include "audio_out.h"
#include "buffer.h"
#include "xine_internal.h"

extern char*   win32_codec_name; 

typedef struct w32v_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t    *video_out;
  int               video_step;
  int               decoder_ok;

  BITMAPINFOHEADER  bih, o_bih; 
  HIC               hic;
  int               yuv_supported ;
  int               yuv_hack_needed ;
  int               flipped ;
  unsigned char     buf[128*1024];
  void             *our_out_buffer;
  int               size;   
} w32v_decoder_t;

typedef struct w32a_decoder_s {
  audio_decoder_t   audio_decoder;

  ao_functions_t   *audio_out;
  int               decoder_ok;

  unsigned char     buf[16384];
  int               size;   
  unsigned char     sample_buf[40000];
  HACMSTREAM        srcstream;
  int               rec_audio_src_size;
  int               num_channels;
} w32a_decoder_t;


static char* get_vids_codec_name(w32v_decoder_t *this,
				 unsigned long fccHandler, 
				 BITMAPINFOHEADER *bih) {

  this->yuv_supported=0;
  this->yuv_hack_needed=0;
  this->flipped=0;
  switch(fccHandler){
  case mmioFOURCC('M', 'P', 'G', '4'):
  case mmioFOURCC('m', 'p', 'g', '4'):
  case mmioFOURCC('M', 'P', '4', '2'):
  case mmioFOURCC('m', 'p', '4', '2'):
    /*	case mmioFOURCC('M', 'P', '4', '3'):
            case mmioFOURCC('m', 'p', '4', '3'): */
    /* Video in Microsoft MPEG-4 format */
    this->yuv_supported=1;
    this->yuv_hack_needed=1;
    return "mpg4c32.dll";
  case mmioFOURCC('M', 'P', '4', '3'):
  case mmioFOURCC('m', 'p', '4', '3'):
    /* Video in MPEG-4 v3 (really DivX) format */
    bih->biCompression=mmioFOURCC('d', 'i', 'v', '3');  /* hack */
    this->yuv_supported=1;
    this->yuv_hack_needed=1;
    return "divxc32.dll";

  case mmioFOURCC('D', 'I', 'V', '3'):
  case mmioFOURCC('d', 'i', 'v', '3'):
  case mmioFOURCC('D', 'I', 'V', '4'):
  case mmioFOURCC('d', 'i', 'v', '4'):
  case mmioFOURCC('M', 'P', '4', '1'):
  case mmioFOURCC('m', 'p', '4', '1'):
    /* Video in DivX ;-) format */
    this->yuv_supported  =1;
    this->yuv_hack_needed=1;
    return "divxc32.dll";
    
  case mmioFOURCC('I', 'V', '5', '0'):	    
  case mmioFOURCC('i', 'v', '5', '0'):	 
    /* Video in Indeo Video 5 format */
    this->yuv_supported=1;   /* YUV pic is upside-down :( */
    return "ir50_32.dll";
							
  case mmioFOURCC('I', 'V', '4', '1'):	    
  case mmioFOURCC('i', 'v', '4', '1'):	    
    /* Video in Indeo Video 4.1 format */
    this->flipped=1;
    return "ir41_32.dll";
    
  case mmioFOURCC('I', 'V', '3', '2'):	    
  case mmioFOURCC('i', 'v', '3', '2'):
    /* Video in Indeo Video 3.2 format */
    this->flipped=1;
    return "ir32_32.dll";
    
  case mmioFOURCC('c', 'v', 'i', 'd'):
    /* Video in Cinepak format */
    this->yuv_supported=1;
    return "iccvid.dll";

    /*** Only 16bit .DLL available (can't load under linux) ***
	 case mmioFOURCC('V', 'C', 'R', '1'):
	 printf("Video in ATI VCR1 format\n");
	 return "ativcr1.dll";
    */

  case mmioFOURCC('V', 'C', 'R', '2'):
    /* Video in ATI VCR2 format */
    this->yuv_supported=1;
    return "ativcr2.dll";
    
  case mmioFOURCC('I', '2', '6', '3'):
  case mmioFOURCC('i', '2', '6', '3'):
    /* Video in I263 format */
    return "i263_32.drv";
    
  case mmioFOURCC('M', 'J', 'P', 'G'):
    /* Video in MJPEG format */
    this->yuv_supported=1;
    return "mcmjpg32.dll";
    /* return "m3jpeg32.dll";
       return "libavi_mjpeg.so"; */
  }
  printf("UNKNOWN video codec: %.4s (0x%0X)\n",(char*)&fccHandler,(int)fccHandler);
  printf("If you know this video format and codec, you can edit w32codec.c in the source!\n");
  printf("Please contact the author, send this movie to be supported by future version.\n");
  return NULL;
}

#define IMGFMT_YUY2 (('2'<<24)|('Y'<<16)|('U'<<8)|'Y')

static int w32v_can_handle (video_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_VIDEO_AVI) ;
}

static void w32v_init (video_decoder_t *this_gen, vo_instance_t *video_out) {

  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  this->video_out  = video_out;
  this->decoder_ok = 0;
}


static void w32v_init_codec (w32v_decoder_t *this) {

  HRESULT ret;
  int outfmt = IMGFMT_YUY2;

  memset(&this->o_bih, 0, sizeof(BITMAPINFOHEADER));
  this->o_bih.biSize = sizeof(BITMAPINFOHEADER);
  
  win32_codec_name = get_vids_codec_name (this, this->bih.biCompression, &this->bih);  
  this->hic = ICOpen( 0x63646976, this->bih.biCompression, ICMODE_FASTDECOMPRESS);

  if(!this->hic){
    printf ("ICOpen failed! unknown codec / wrong parameters?\n");
    this->decoder_ok = 0;
    return;
  }

  ret = ICDecompressGetFormat(this->hic, &this->bih, &this->o_bih);
  if(ret){
    printf("ICDecompressGetFormat failed: Error %ld\n", (long)ret);
    this->decoder_ok = 0;
    return;
  }

  if(outfmt==IMGFMT_YUY2)
    this->o_bih.biBitCount=16;
  else
    this->o_bih.biBitCount=outfmt&0xFF;//   //24;

  this->o_bih.biSizeImage = this->o_bih.biWidth*this->o_bih.biHeight*(this->o_bih.biBitCount/8);
  
  /*
  if(!flipped)
    this->o_bih.biHeight=-bih.biHeight; */ /* flip image! */

  this->o_bih.biHeight=-this->bih.biHeight; 

  if(outfmt==IMGFMT_YUY2 && !this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2');

  ret = ICDecompressQuery(this->hic, &this->bih, &this->o_bih);
  
  if(ret){
    printf("ICDecompressQuery failed: Error %ld\n", (long)ret);
    this->decoder_ok = 0;
    return;
  }
  
  ret = ICDecompressBegin(this->hic, &this->bih, &this->o_bih);
  if(ret){
    printf("ICDecompressBegin failed: Error %ld\n", (long)ret);
    this->decoder_ok = 0;
    return;
  }

  if (this->yuv_hack_needed) {
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2'); 
  }
  
  this->size = 0;

  if (!( (this->video_out->get_capabilities (this->video_out)) & VO_CAP_YUY2)) {
    printf ("video output driver doesn't support YUY2 !!\n");
    this->decoder_ok = 0;
    return;
  }

  this->our_out_buffer = malloc (this->o_bih.biSizeImage);

  this->video_out->open (this->video_out);

  this->decoder_ok = 1;
}

static void w32v_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  /*
  printf ("w32codec: processing packet type = %08x, buf : %d, buf->decoder_info[0]=%d\n", 
	  buf->type, buf, buf->decoder_info[0]);
	  */

  if (buf->decoder_info[0] == 0) {
    /* init package containing bih */

    memcpy ( &this->bih, buf->content, sizeof (BITMAPINFOHEADER));
    this->video_step = buf->decoder_info[1];

    w32v_init_codec (this);
    
  } else if (this->decoder_ok) {

    /* printf ("w32codec: processing packet ...\n"); */

    memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_info[0] == 2)  {

      HRESULT     ret;
      vo_frame_t *img;

      /* decoder video frame */

      this->bih.biSizeImage = this->size;

      img = this->video_out->get_frame (this->video_out,
					this->bih.biWidth, 
					this->bih.biHeight, 
					42, 
					IMGFMT_YUY2, 
					this->video_step);

      ret = ICDecompress(this->hic, ICDECOMPRESS_NOTKEYFRAME, 
			 &this->bih, this->buf,
			 &this->o_bih, img->base[0]);
      
      img->PTS = buf->PTS;
      if(ret) {
	printf("Error decompressing frame, err=%ld\n", (long)ret); 
	img->bFrameBad = 1;
      } else
	img->bFrameBad = 0;
      
      img->draw(img);
      img->free(img);

      this->size = 0;
    }

    /* printf ("w32codec: processing packet done\n"); */
  }
}

static void w32v_close (video_decoder_t *this_gen) {

  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  this->video_out->close(this->video_out);
}

static char *w32v_get_id(void) {
  return "vfw (win32) video decoder";
}

/*
 * audio stuff
 */

static int w32a_can_handle (audio_decoder_t *this_gen, int buf_type) {

  return ((buf_type & 0xFFFF0000) == BUF_AUDIO_AVI) ;
}

static char* get_auds_codec_name(w32a_decoder_t *this, int id){

  switch (id){
  case 0x160:/* DivX audio */
  case 0x161:/* DivX audio */
    return "divxa32.acm";
  case 0x2:  /* MS ADPCM   */
    return "msadp32.acm";
  case 0x55: /* MPEG l3    */
    return "l3codeca.acm";
  case 0x11: /* IMA ADPCM  */
    return "imaadp32.acm";
  case 0x31: /* MS GSM     */
  case 0x32: /* MS GSM     */
    return "msgsm32.acm";
  }
  printf("UNKNOWN audio codec: 0x%0X\n",id);
  printf("If you know this audio format and codec, you can edit codecs.c in the source!\n");
  printf("Please contact the author, send this movie to be supported by future version.\n");
  return NULL;
}

static void w32a_init (audio_decoder_t *this_gen, ao_functions_t *audio_out) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  this->audio_out  = audio_out;
  this->decoder_ok = 0;
}

static int w32a_init_audio (w32a_decoder_t *this, WAVEFORMATEX *in_fmt_){

  HRESULT ret;
  static WAVEFORMATEX wf;     
  /* long in_size=in_fmt_->nBlockAlign; */
  static WAVEFORMATEX *in_fmt;

  in_fmt = (WAVEFORMATEX *) malloc (64);

  memcpy (in_fmt, in_fmt_, sizeof (WAVEFORMATEX) + in_fmt_->cbSize);

  if ( (in_fmt->wFormatTag == 0x01) || (in_fmt->wFormatTag == 0x2000)
       || (in_fmt->wFormatTag == 0x50) || (in_fmt->wFormatTag == 0x53) ) {
    /* handled by other codecs in source code */
    return 1;
  }
  
  this->srcstream = 0;
  this->num_channels  = in_fmt->nChannels;
  
  this->audio_out->open( this->audio_out, 
			 16, in_fmt->nSamplesPerSec, 
			 (in_fmt->nChannels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO); 

  wf.nChannels       = in_fmt->nChannels;
  wf.nSamplesPerSec  = in_fmt->nSamplesPerSec;
  wf.nAvgBytesPerSec = 2*wf.nSamplesPerSec*wf.nChannels;
  wf.wFormatTag      = WAVE_FORMAT_PCM;
  wf.nBlockAlign     = 2*in_fmt->nChannels;
  wf.wBitsPerSample  = 16;
  wf.cbSize          = 0;
  
  win32_codec_name = get_auds_codec_name (this, in_fmt->wFormatTag);
  ret=acmStreamOpen(&this->srcstream,(HACMDRIVER)NULL,
                    in_fmt,
		    &wf,
                    NULL,0,0,0);
  if(ret){
    if(ret==ACMERR_NOTPOSSIBLE)
      printf("ACM_Decoder: Unappropriate audio format\n");
    else
      printf("ACM_Decoder: acmStreamOpen error %d", (int) ret);
    this->srcstream = 0;
    return 0;
  }

  /*
  acmStreamSize(this->srcstream, in_size, &srcsize, ACM_STREAMSIZEF_SOURCE);
  printf("Audio buffer min. size: %d\n",srcsize);
  */

  acmStreamSize(this->srcstream, 16384, (LPDWORD) &this->rec_audio_src_size, 
		ACM_STREAMSIZEF_DESTINATION);
  /* printf("recommended source buffer size: %d\n", this->rec_audio_src_size); */

  this->size = 0;

  return 1;
}


static void w32a_decode_audio (w32a_decoder_t *this,
			       unsigned char *data, uint32_t nSize, 
			       int bFrameEnd, uint32_t nPTS) {

  static ACMSTREAMHEADER ash;
  HRESULT hr;
  /* DWORD srcsize=0; */

  memcpy (&this->buf[this->size], data, nSize);

  this->size += nSize;

  while (this->size >= this->rec_audio_src_size) {

    memset(&ash, 0, sizeof(ash));
    ash.cbStruct=sizeof(ash);
    ash.fdwStatus=0;
    ash.dwUser=0; 
    ash.pbSrc=this->buf;
    ash.cbSrcLength=this->rec_audio_src_size;
    ash.pbDst=this->sample_buf;
    ash.cbDstLength=20000;
    hr=acmStreamPrepareHeader(this->srcstream,&ash,0);
    if(hr){
      printf("ACM_Decoder: acmStreamPrepareHeader error %d\n",(int)hr);
      return;
    }

    /*
    printf ("decoding %d of %d bytes (%02x %02x %02x %02x ... %02x %02x)\n", 
	    this->rec_audio_src_size, this->size,
	    this->buf[0], this->buf[1], this->buf[2], this->buf[3],
	    this->buf[this->rec_audio_src_size-2], this->buf[this->rec_audio_src_size-1]); 
    */

    hr=acmStreamConvert(this->srcstream,&ash,0);
    if(hr){
      /* printf("acmStreamConvert error %d, used %d bytes\n",hr,ash.cbSrcLengthUsed); */
      ash.cbSrcLengthUsed = this->rec_audio_src_size; 
    } else {
      /*
      printf ("acmStreamConvert worked, used %d bytes, generated %d bytes\n",
	      ash.cbSrcLengthUsed, ash.cbDstLengthUsed);
      */
      if (ash.cbDstLengthUsed>0) {
	/*
	printf ("decoded : %02x %02x %02x %02x  ... %02x %02x \n",
		this->sample_buf[0], this->sample_buf[1], this->sample_buf[2], this->sample_buf[3], 
		this->sample_buf[ash.cbDstLengthUsed-2], this->sample_buf[ash.cbDstLengthUsed-1]);
		*/
	this->audio_out->write_audio_data (this->audio_out,
					   (int16_t*) this->sample_buf, 
					   ash.cbDstLengthUsed / (this->num_channels*2), 
					   nPTS); 
      }
    }
    if(ash.cbSrcLengthUsed>=this->size){
      this->size=0;
    } else {
      unsigned char *pSrc, *pDst;
      int i;

      this->size-=ash.cbSrcLengthUsed;
      
      pSrc = &this->buf [ash.cbSrcLengthUsed];
      pDst = this->buf;
      for (i=0; i<this->size; i++) {
	*pDst = *pSrc;
	pDst ++;
	pSrc ++;
      }
    }

    hr=acmStreamUnprepareHeader(this->srcstream,&ash,0);
    if(hr){
      printf("ACM_Decoder: acmStreamUnprepareHeader error %d\n",(int)hr);
    }
  }
}

static void w32a_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  if (buf->decoder_info[0] == 0) {
    /* init package containing bih */

    this->decoder_ok = w32a_init_audio (this, (WAVEFORMATEX *)buf->content);
  } else if (this->decoder_ok) {

    w32a_decode_audio (this, buf->content, buf->size,
		       buf->decoder_info[0]==2, 
		       buf->PTS);
  }
}



static void w32a_close (audio_decoder_t *this_gen) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  acmStreamClose(this->srcstream, 0);
}

static char *w32a_get_id(void) {
  return "vfw (win32) audio decoder";
}

video_decoder_t *init_video_decoder_plugin (int iface_version, config_values_t *cfg) {

  w32v_decoder_t *this ;

  if (iface_version != 1)
    return NULL;

  this = (w32v_decoder_t *) malloc (sizeof (w32v_decoder_t));

  this->video_decoder.interface_version   = 1;
  this->video_decoder.can_handle          = w32v_can_handle;
  this->video_decoder.init                = w32v_init;
  this->video_decoder.decode_data         = w32v_decode_data;
  this->video_decoder.close               = w32v_close;
  this->video_decoder.get_identifier      = w32v_get_id;

  return (video_decoder_t *) this;
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  w32a_decoder_t *this ;

  if (iface_version != 1)
    return NULL;

  this = (w32a_decoder_t *) malloc (sizeof (w32a_decoder_t));

  this->audio_decoder.interface_version   = 1;
  this->audio_decoder.can_handle          = w32a_can_handle;
  this->audio_decoder.init                = w32a_init;
  this->audio_decoder.decode_data         = w32a_decode_data;
  this->audio_decoder.close               = w32a_close;
  this->audio_decoder.get_identifier      = w32a_get_id;
  
  return (audio_decoder_t *) this;
}

