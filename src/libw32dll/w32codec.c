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
 * $Id: w32codec.c,v 1.1 2001/04/18 22:35:05 f1rmb Exp $
 *
 * routines for using w32 codecs
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "wine/msacm.h"
#include "wine/driver.h"
#include "wine/avifmt.h"
#include "wine/vfw.h"
#include "wine/mmreg.h"
#include "../video_out/video_out.h"
#include "../audio_out/audio_out.h"

extern vo_driver_t    *gVideoDriver;
extern char*   win32_codec_name; 
int            w32c_yuv_supported ;
int            w32c_yuv_hack_needed ;
int            w32c_flipped ;
unsigned char  w32c_buf[128*1024];
int            w32c_size;   
unsigned char  w32c_audio_buf[16384];
int            w32c_audio_size;   
unsigned char  w32c_sample_buf[40000];
BITMAPINFOHEADER w32c_bih, w32c_o_bih; 
HIC            w32c_hic;
void          *our_out_buffer;
HACMSTREAM     w32c_srcstream;
int            w32c_rec_audio_src_size;

char* get_vids_codec_name(unsigned long fccHandler, BITMAPINFOHEADER *bih) {

  w32c_yuv_supported=0;
  w32c_yuv_hack_needed=0;
  w32c_flipped=0;
  switch(fccHandler){
  case mmioFOURCC('M', 'P', 'G', '4'):
  case mmioFOURCC('m', 'p', 'g', '4'):
  case mmioFOURCC('M', 'P', '4', '2'):
  case mmioFOURCC('m', 'p', '4', '2'):
    /*	case mmioFOURCC('M', 'P', '4', '3'):
            case mmioFOURCC('m', 'p', '4', '3'): */
    /* Video in Microsoft MPEG-4 format */
    w32c_yuv_supported=1;
    w32c_yuv_hack_needed=1;
    return "mpg4c32.dll";
  case mmioFOURCC('M', 'P', '4', '3'):
  case mmioFOURCC('m', 'p', '4', '3'):
    /* Video in MPEG-4 v3 (really DivX) format */
    bih->biCompression=mmioFOURCC('d', 'i', 'v', '3');  /* hack */
    w32c_yuv_supported=1;
    w32c_yuv_hack_needed=1;
    return "divxc32.dll";

  case mmioFOURCC('D', 'I', 'V', '3'):
  case mmioFOURCC('d', 'i', 'v', '3'):
  case mmioFOURCC('D', 'I', 'V', '4'):
  case mmioFOURCC('d', 'i', 'v', '4'):
  case mmioFOURCC('M', 'P', '4', '1'):
  case mmioFOURCC('m', 'p', '4', '1'):
    /* Video in DivX ;-) format */
    w32c_yuv_supported  =1;
    w32c_yuv_hack_needed=1;
    return "divxc32.dll";
    
  case mmioFOURCC('I', 'V', '5', '0'):	    
  case mmioFOURCC('i', 'v', '5', '0'):	 
    /* Video in Indeo Video 5 format */
    w32c_yuv_supported=1;   /* YUV pic is upside-down :( */
    return "ir50_32.dll";
							
  case mmioFOURCC('I', 'V', '4', '1'):	    
  case mmioFOURCC('i', 'v', '4', '1'):	    
    /* Video in Indeo Video 4.1 format */
    w32c_flipped=1;
    return "ir41_32.dll";
    
  case mmioFOURCC('I', 'V', '3', '2'):	    
  case mmioFOURCC('i', 'v', '3', '2'):
    /* Video in Indeo Video 3.2 format */
    w32c_flipped=1;
    return "ir32_32.dll";
    
  case mmioFOURCC('c', 'v', 'i', 'd'):
    /* Video in Cinepak format */
    w32c_yuv_supported=1;
    return "iccvid.dll";

    /*** Only 16bit .DLL available (can't load under linux) ***
	 case mmioFOURCC('V', 'C', 'R', '1'):
	 printf("Video in ATI VCR1 format\n");
	 return "ativcr1.dll";
    */

  case mmioFOURCC('V', 'C', 'R', '2'):
    /* Video in ATI VCR2 format */
    w32c_yuv_supported=1;
    return "ativcr2.dll";
    
  case mmioFOURCC('I', '2', '6', '3'):
  case mmioFOURCC('i', '2', '6', '3'):
    /* Video in I263 format */
    return "i263_32.drv";
    
  case mmioFOURCC('M', 'J', 'P', 'G'):
    /* Video in MJPEG format */
    w32c_yuv_supported=1;
    return "mcmjpg32.dll";
    /* return "m3jpeg32.dll";
       return "libavi_mjpeg.so"; */
  }
  printf("UNKNOWN video codec: %.4s (0x%0X)\n",(char*)&fccHandler,(int)fccHandler);
  printf("If you know this video format and codec, you can edit codecs.c in the source!\n");
  printf("Please contact the author, send this movie to be supported by future version.\n");
  return NULL;
}

#define IMGFMT_YUY2 (('2'<<24)|('Y'<<16)|('U'<<8)|'Y')

int w32c_init_video (BITMAPINFOHEADER *bih_){
  HRESULT ret;
  int outfmt = IMGFMT_YUY2;
  int video_step;

  memcpy ( &w32c_bih, bih_, sizeof (BITMAPINFOHEADER));
  video_step = w32c_bih.biSize; /* HACK */
  w32c_bih.biSize = sizeof(BITMAPINFOHEADER);

  memset(&w32c_o_bih, 0, sizeof(BITMAPINFOHEADER));
  w32c_o_bih.biSize = sizeof(BITMAPINFOHEADER);
  
  win32_codec_name = get_vids_codec_name (w32c_bih.biCompression, &w32c_bih);  
  w32c_hic = ICOpen( 0x63646976, w32c_bih.biCompression, ICMODE_FASTDECOMPRESS);

  if(!w32c_hic){
    printf("ICOpen failed! unknown codec / wrong parameters?\n");
    return 0;
  }

  ret = ICDecompressGetFormat(w32c_hic, &w32c_bih, &w32c_o_bih);
  if(ret){
    printf("ICDecompressGetFormat failed: Error %ld\n", (long)ret);
    return 0;  
  }

  if(outfmt==IMGFMT_YUY2)
    w32c_o_bih.biBitCount=16;
  else
    w32c_o_bih.biBitCount=outfmt&0xFF;//   //24;

  w32c_o_bih.biSizeImage = w32c_o_bih.biWidth*w32c_o_bih.biHeight*(w32c_o_bih.biBitCount/8);
  
  /*
  if(!flipped)
    w32c_o_bih.biHeight=-bih.biHeight; */ /* flip image! */

  w32c_o_bih.biHeight=-w32c_bih.biHeight; 

  if(outfmt==IMGFMT_YUY2 && !w32c_yuv_hack_needed)
    w32c_o_bih.biCompression = mmioFOURCC('Y','U','Y','2');

  ret = ICDecompressQuery(w32c_hic, &w32c_bih, &w32c_o_bih);
  
  if(ret){
    printf("ICDecompressQuery failed: Error %ld\n", (long)ret);
    return 0; 
  }
  
  ret = ICDecompressBegin(w32c_hic, &w32c_bih, &w32c_o_bih);
  if(ret){
    printf("ICDecompressBegin failed: Error %ld\n", (long)ret);
    return 0; 
  }

  if (w32c_yuv_hack_needed) {
    w32c_o_bih.biCompression = mmioFOURCC('Y','U','Y','2'); 
  }
  
  w32c_size = 0;

  if (!(gVideoDriver->get_capabilities () && VO_CAP_YUY2)) {
    printf ("video output driver doesn't support YUY2 !!");
  }

  vo_set_image_format (w32c_bih.biWidth, w32c_bih.biHeight, 42, IMGFMT_YUY2, video_step);

  our_out_buffer = malloc (w32c_o_bih.biSizeImage);

  return 1;
}

int nFrame = 0;

void w32c_decode_video (unsigned char *data, uint32_t nSize, int bFrameEnd, uint32_t nPTS) {

  HRESULT ret;
  vo_image_buffer_t *img;

  memcpy (&w32c_buf[w32c_size], data, nSize);

  w32c_size += nSize;
  
  if (bFrameEnd) {
    
    w32c_bih.biSizeImage = w32c_size;
    /*
    printf ("Frame complete => decompressing [%d %d %d %d ... %d %d]size=%d\n",
	    w32c_buf[0],w32c_buf[1],w32c_buf[2],w32c_buf[3], 
	    w32c_buf[w32c_size-2],w32c_buf[w32c_size-1], w32c_size);
	    */

    img = vo_alloc_image_buffer();

    /* printf ("ICDecrompress %d\n",img); */
    
    ret = ICDecompress(w32c_hic, ICDECOMPRESS_NOTKEYFRAME, 
		       &w32c_bih, w32c_buf,
		       &w32c_o_bih, img->mem[0]);

    /* memcpy(img->mem[0],our_out_buffer,w32c_bih.biWidth*w32c_bih.biHeight*2); */
    /* memset(img->mem[1],128,w32c_o_bih.biWidth*w32c_o_bih.biHeight/4); */
    /* memset(img->mem[2],128,w32c_o_bih.biWidth*w32c_o_bih.biHeight/4); */
    
    img->PTS = nPTS;
    if(ret) {
      printf("Error decompressing frame, err=%ld\n", (long)ret); 
      img->bFrameBad = 1;
    } else
      img->bFrameBad = 0;

    img->nID = nFrame;
    nFrame++;

    vo_queue_frame (img);
    vo_free_image_buffer (img);
       
    
    w32c_size = 0;
  }
  
}

void w32c_close_video () {
}

char* get_auds_codec_name(int id){

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

int w32c_init_audio (WAVEFORMATEX *in_fmt_){

  HRESULT ret;
  static WAVEFORMATEX wf;     
  long in_size=in_fmt_->nBlockAlign;
  unsigned long srcsize=0;
  static WAVEFORMATEX *in_fmt;

  in_fmt = (WAVEFORMATEX *) malloc (64);

  memcpy (in_fmt, in_fmt_, sizeof (WAVEFORMATEX) + in_fmt_->cbSize);

  if ( (in_fmt->wFormatTag == 0x01) || (in_fmt->wFormatTag == 0x2000)
       || (in_fmt->wFormatTag == 0x50) || (in_fmt->wFormatTag == 0x53) ) {
    /* handled by other codecs in source code */
    return 1;
  }
  
  w32c_srcstream=NULL;
  
  gAudioOut->open (16, in_fmt->nSamplesPerSec, AO_MODE_STEREO); 

  wf.nChannels=in_fmt->nChannels;
  wf.nSamplesPerSec=in_fmt->nSamplesPerSec;
  wf.nAvgBytesPerSec=2*wf.nSamplesPerSec*wf.nChannels;
  wf.wFormatTag=WAVE_FORMAT_PCM;
  wf.nBlockAlign=2*in_fmt->nChannels;
  wf.wBitsPerSample=16;
  wf.cbSize=0;
  
  win32_codec_name = get_auds_codec_name (in_fmt->wFormatTag);
  ret=acmStreamOpen(&w32c_srcstream,(HACMDRIVER)NULL,
                    in_fmt,
		    &wf,
                    NULL,0,0,0);
  if(ret){
    if(ret==ACMERR_NOTPOSSIBLE)
      printf("ACM_Decoder: Unappropriate audio format\n");
    else
      printf("ACM_Decoder: acmStreamOpen error %d", ret);
    w32c_srcstream=NULL;
    return 0;
  }

  /*
  acmStreamSize(w32c_srcstream, in_size, &srcsize, ACM_STREAMSIZEF_SOURCE);
  printf("Audio buffer min. size: %d\n",srcsize);
  */

  acmStreamSize(w32c_srcstream, 16384, &w32c_rec_audio_src_size, ACM_STREAMSIZEF_DESTINATION);
  /* printf("recommended source buffer size: %d\n", w32c_rec_audio_src_size); */

  w32c_audio_size = 0;

  return 1;
}


void w32c_decode_audio (unsigned char *data, uint32_t nSize, int bFrameEnd, uint32_t nPTS) {

  static ACMSTREAMHEADER ash;
  HRESULT hr;
  DWORD srcsize=0;

  memcpy (&w32c_audio_buf[w32c_audio_size], data, nSize);

  w32c_audio_size += nSize;

  while (w32c_audio_size >= w32c_rec_audio_src_size) {

    memset(&ash, 0, sizeof(ash));
    ash.cbStruct=sizeof(ash);
    ash.fdwStatus=0;
    ash.dwUser=0; 
    ash.pbSrc=w32c_audio_buf;
    ash.cbSrcLength=w32c_rec_audio_src_size;
    ash.pbDst=w32c_sample_buf;
    ash.cbDstLength=20000;
    hr=acmStreamPrepareHeader(w32c_srcstream,&ash,0);
    if(hr){
      printf("ACM_Decoder: acmStreamPrepareHeader error %d\n",hr);
      return;
    }

    /*
    printf ("decoding %d of %d bytes (%02x %02x %02x %02x ... %02x %02x)\n", 
	    w32c_rec_audio_src_size, w32c_audio_size,
	    w32c_audio_buf[0], w32c_audio_buf[1], w32c_audio_buf[2], w32c_audio_buf[3],
	    w32c_audio_buf[w32c_rec_audio_src_size-2], w32c_audio_buf[w32c_rec_audio_src_size-1]); 
    */

    hr=acmStreamConvert(w32c_srcstream,&ash,0);
    if(hr){
      /* printf("acmStreamConvert error %d, used %d bytes\n",hr,ash.cbSrcLengthUsed); */
      ash.cbSrcLengthUsed = w32c_rec_audio_src_size; 
    } else {
      /*
      printf ("acmStreamConvert worked, used %d bytes, generated %d bytes\n",
	      ash.cbSrcLengthUsed, ash.cbDstLengthUsed);
      */
      if (ash.cbDstLengthUsed>0) {
	/*
	printf ("decoded : %02x %02x %02x %02x  ... %02x %02x \n",
		w32c_sample_buf[0], w32c_sample_buf[1], w32c_sample_buf[2], w32c_sample_buf[3], 
		w32c_sample_buf[ash.cbDstLengthUsed-2], w32c_sample_buf[ash.cbDstLengthUsed-1]);
		*/
	gAudioOut->write_audio_data (w32c_sample_buf, ash.cbDstLengthUsed / 4, nPTS); 
      }
    }
    if(ash.cbSrcLengthUsed>=w32c_audio_size){
      w32c_audio_size=0;
    } else {
      unsigned char *pSrc, *pDst;
      int i;

      w32c_audio_size-=ash.cbSrcLengthUsed;
      
      pSrc = &w32c_audio_buf [ash.cbSrcLengthUsed];
      pDst = w32c_audio_buf;
      for (i=0; i<w32c_audio_size; i++) {
	*pDst = *pSrc;
	pDst ++;
	pSrc ++;
      }
    }

    hr=acmStreamUnprepareHeader(w32c_srcstream,&ash,0);
    if(hr){
      printf("ACM_Decoder: acmStreamUnprepareHeader error %d\n",hr);
    }
  }
}

void w32c_close_audio () {
  acmStreamClose(w32c_srcstream, 0);
}
