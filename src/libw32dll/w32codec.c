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
 * $Id: w32codec.c,v 1.27 2001/09/19 18:42:55 jkeil Exp $
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
#include "monitor.h"
#include "xine_internal.h"

extern char*   win32_codec_name; 
extern char*   win32_def_path;

typedef struct w32v_decoder_s {
  video_decoder_t   video_decoder;

  vo_instance_t    *video_out;
  int               video_step;
  int               decoder_ok;

  BITMAPINFOHEADER  bih, o_bih; 
  HIC               hic;
  int               yuv_supported ;
  int		    yuv_hack_needed ;
  int               flipped ;
  unsigned char     buf[128*1024];
  void             *img_buffer;
  int               size;
  long		    outfmt;

  /* profiler */
  int		   prof_rgb2yuv;
} w32v_decoder_t;

typedef struct w32a_decoder_s {
  audio_decoder_t   audio_decoder;

  ao_instance_t    *audio_out;
    int		    output_open;
  int               decoder_ok;

  unsigned char     buf[16384];
  int               size;   
  unsigned char     sample_buf[40000];
  HACMSTREAM        srcstream;
  int               rec_audio_src_size;
  int               num_channels;
} w32a_decoder_t;


/*
 * RGB->YUY2 conversion, we need is for xine video-codec ->
 * video-output interface
 *
 * YCbCr is defined per CCIR 601-1, except that Cb and Cr are
 * normalized to the range 0..MAXSAMPLE rather than -0.5 .. 0.5.
 * The conversion equations to be implemented are therefore
 *      Y  =  0.29900 * R + 0.58700 * G + 0.11400 * B
 *      Cb = -0.16874 * R - 0.33126 * G + 0.50000 * B  + CENTERSAMPLE
 *      Cr =  0.50000 * R - 0.41869 * G - 0.08131 * B  + CENTERSAMPLE
 * (These numbers are derived from TIFF 6.0 section 21, dated 3-June-92.)
 *
 * To avoid floating-point arithmetic, we represent the fractional
 * constants as integers scaled up by 2^16 (about 4 digits precision);
 * we have to divide the products by 2^16, with appropriate rounding,
 * to get the correct answer.
 *
 * FIXME: For the XShm video-out driver, this conversion is a huge
 * waste of time (converting from RGB->YUY2 here and converting back
 * from YUY2->RGB in the XShm driver).
 */
#define	MAXSAMPLE	255
#define	CENTERSAMPLE	128

#define	SCALEBITS	16
#define	FIX(x)	 	( (int32_t) ( (x) * (1<<SCALEBITS) + 0.5 ) )
#define	ONE_HALF	( (int32_t) (1<< (SCALEBITS-1)) )
#define	CBCR_OFFSET	(CENTERSAMPLE << SCALEBITS)

#define R_Y_OFF         0                       /* offset to R => Y section */
#define G_Y_OFF         (1*(MAXSAMPLE+1))       /* offset to G => Y section */
#define B_Y_OFF         (2*(MAXSAMPLE+1))       /* etc. */
#define R_CB_OFF        (3*(MAXSAMPLE+1))
#define G_CB_OFF        (4*(MAXSAMPLE+1))
#define B_CB_OFF        (5*(MAXSAMPLE+1))
#define R_CR_OFF        B_CB_OFF                /* B=>Cb, R=>Cr are the same */
#define G_CR_OFF        (6*(MAXSAMPLE+1))
#define B_CR_OFF        (7*(MAXSAMPLE+1))
#define TABLE_SIZE      (8*(MAXSAMPLE+1))


/*
 * HAS_SLOW_MULT:
 * 0: use integer multiplication in inner loop of rgb2yuv conversion
 * 1: use precomputed tables (avoids slow integer multiplication)
 *
 * (On a P-II/Athlon, the version using the precomputed tables is
 * slightly faster)
 */
#define	HAS_SLOW_MULT	1

#if	HAS_SLOW_MULT
static int32_t *rgb_ycc_tab;
#endif

static void w32v_init_rgb_ycc(void)
{
#if	HAS_SLOW_MULT
  /*
   * System has slow integer multiplication, so we precompute
   * the YCbCr constants times R,G,B for all possible values.
   */
  int i;
    
  if (rgb_ycc_tab) return;

  rgb_ycc_tab = malloc(TABLE_SIZE * sizeof(int32_t));

  for (i = 0; i <= MAXSAMPLE; i++) {
    rgb_ycc_tab[i+R_Y_OFF] = FIX(0.29900) * i;
    rgb_ycc_tab[i+G_Y_OFF] = FIX(0.58700) * i;
    rgb_ycc_tab[i+B_Y_OFF] = FIX(0.11400) * i     + ONE_HALF;
    rgb_ycc_tab[i+R_CB_OFF] = (-FIX(0.16874)) * i;
    rgb_ycc_tab[i+G_CB_OFF] = (-FIX(0.33126)) * i;
    /*
     * We use a rounding fudge-factor of 0.5-epsilon for Cb and Cr.
     * This ensures that the maximum output will round to MAXJSAMPLE
     * not MAXJSAMPLE+1, and thus that we don't have to range-limit.
     */
    rgb_ycc_tab[i+B_CB_OFF] = FIX(0.50000) * i    + CBCR_OFFSET + ONE_HALF-1;
    /*
     * B=>Cb and R=>Cr tables are the same
    rgb_ycc_tab[i+R_CR_OFF] = FIX(0.50000) * i    + CBCR_OFFSET + ONE_HALF-1;
     */
    rgb_ycc_tab[i+G_CR_OFF] = (-FIX(0.41869)) * i;
    rgb_ycc_tab[i+B_CR_OFF] = (-FIX(0.08131)) * i;
  }
#endif
}

static char* get_vids_codec_name(w32v_decoder_t *this,
				 int buf_type) {

  this->yuv_supported=0;
  this->yuv_hack_needed=0;
  this->flipped=0;

  switch (buf_type) {
  case BUF_VIDEO_MSMPEG4_V12:
    /* Microsoft MPEG-4 v1/v2 */
    this->yuv_supported=1;
    this->yuv_hack_needed=1;
    this->flipped=1;
    return "mpg4c32.dll";

  case BUF_VIDEO_MSMPEG4_V3:
    /* Microsoft MPEG-4 v3 */
    this->yuv_supported=1;
    this->yuv_hack_needed=1;
    this->flipped=1;
    return "divxc32.dll";

  case BUF_VIDEO_IV50:
    /* Video in Indeo Video 5 format */
    this->yuv_supported=1;   /* YUV pic is upside-down :( */
    this->flipped=0;
    return "ir50_32.dll";

  case BUF_VIDEO_IV41:
    /* Video in Indeo Video 4.1 format */
    this->flipped=0;
    return "ir41_32.dll";
    
  case BUF_VIDEO_IV32:
    /* Video in Indeo Video 3.2 format */
    this->flipped=1;
    return "ir32_32.dll";
    
  case BUF_VIDEO_IV31:
    /* Video in Indeo Video 3.1 format */
    this->flipped=1;
    return "ir32_32.dll";

  case BUF_VIDEO_CINEPAK:
    /* Video in Cinepak format */
    this->flipped=1;
    this->yuv_supported=0;
    return "iccvid.dll";

    /*** Only 16bit .DLL available (can't load under linux) ***
	 case mmioFOURCC('V', 'C', 'R', '1'):
	 printf("Video in ATI VCR1 format\n");
	 return "ativcr1.dll";
    */

  case BUF_VIDEO_ATIVCR2:
    /* Video in ATI VCR2 format */
    this->yuv_supported=1;
    return "ativcr2.dll";
    
  case BUF_VIDEO_I263:
    /* Video in I263 format */
    return "i263_32.drv";
    
  }

  printf ("w32codec: this didn't happen: unknown video buf type %08x\n",
	  buf_type);
  

  return NULL;
}

#undef IMGFMT_YUY2
#undef IMGFMT_YV12
#define IMGFMT_YUY2  mmioFOURCC('Y','U','Y','2')
#define IMGFMT_YV12  mmioFOURCC('Y','V','1','2')
#define IMGFMT_32RGB mmioFOURCC( 32,'R','G','B')
#define IMGFMT_24RGB mmioFOURCC( 24,'R','G','B')
#define IMGFMT_16RGB mmioFOURCC( 16,'R','G','B')
#define IMGFMT_15RGB mmioFOURCC( 15,'R','G','B')

static int w32v_can_handle (video_decoder_t *this_gen, int buf_type) {
  buf_type &= 0xFFFF0000;

  return ( buf_type == BUF_VIDEO_MSMPEG4_V12 ||
	   buf_type == BUF_VIDEO_MSMPEG4_V3 ||
	   buf_type == BUF_VIDEO_IV50 ||
           buf_type == BUF_VIDEO_IV41 ||
           buf_type == BUF_VIDEO_IV32 ||
           buf_type == BUF_VIDEO_IV31 ||
           buf_type == BUF_VIDEO_CINEPAK ||
           /* buf_type == BUF_VIDEO_ATIVCR1 || */
           buf_type == BUF_VIDEO_ATIVCR2 ||
	   buf_type == BUF_VIDEO_I263);
}

static void w32v_init (video_decoder_t *this_gen, vo_instance_t *video_out) {

  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  this->video_out  = video_out;
  this->decoder_ok = 0;
}


static void w32v_init_codec (w32v_decoder_t *this, int buf_type) {

  HRESULT  ret;
  uint32_t vo_cap;
  int outfmt;

  w32v_init_rgb_ycc();

  printf ("init codec...\n");

  memset(&this->o_bih, 0, sizeof(BITMAPINFOHEADER));
  this->o_bih.biSize = sizeof(BITMAPINFOHEADER);
  
  win32_codec_name = get_vids_codec_name (this, buf_type);

  outfmt = IMGFMT_15RGB;
  if (this->yuv_supported) {
    vo_cap = this->video_out->get_capabilities (this->video_out);
    if (vo_cap & VO_CAP_YUY2)
      outfmt = IMGFMT_YUY2;
  }

  this->hic = ICOpen (mmioFOURCC('v','i','d','c'), 
		      this->bih.biCompression, 
		      ICMODE_FASTDECOMPRESS);

  if(!this->hic){
    printf ("ICOpen failed! unknown codec %08lx / wrong parameters?\n",
	    this->bih.biCompression);
    this->decoder_ok = 0;
    return;
  }

  ret = ICDecompressGetFormat(this->hic, &this->bih, &this->o_bih);
  if(ret){
    printf("ICDecompressGetFormat (%.4s %08lx/%d) failed: Error %ld\n",
	   (char*)&this->o_bih.biCompression, 
	   this->bih.biCompression,
	   this->bih.biBitCount,
	   (long)ret);
    this->decoder_ok = 0;
    return;
  }

  printf ("w32codec: video output format: %.4s %08lx\n",
	  (char*)&this->o_bih.biCompression,
	  this->o_bih.biCompression);

  if(outfmt==IMGFMT_YUY2 || outfmt==IMGFMT_15RGB)
    this->o_bih.biBitCount=16;
  else
    this->o_bih.biBitCount=outfmt&0xFF;

  this->o_bih.biSizeImage = this->o_bih.biWidth * this->o_bih.biHeight
      * this->o_bih.biBitCount / 8;

  if (this->flipped)
    this->o_bih.biHeight=-this->bih.biHeight; 

  if(outfmt==IMGFMT_YUY2 && !this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2');
  else 
    this->o_bih.biCompression = 0;
      
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

  if (outfmt==IMGFMT_YUY2 && this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2'); 

  this->size = 0;

  this->img_buffer = malloc (this->o_bih.biSizeImage);

  this->video_out->open (this->video_out);

  this->outfmt = outfmt;
  this->decoder_ok = 1;
}

static void w32v_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  /*
  printf ("w32codec: processing packet type = %08x, buf : %d, 
          buf->decoder_info[0]=%d\n", 
	  buf->type, buf, buf->decoder_info[0]);
	  */

  if (buf->decoder_info[0] == 0) {
    /* init package containing bih */

    memcpy ( &this->bih, buf->content, sizeof (BITMAPINFOHEADER));
    this->video_step = buf->decoder_info[1];

    w32v_init_codec (this, buf->type);
    
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
					this->outfmt,
					this->video_step,
					VO_BOTH_FIELDS);

      /*
      ret = ICDecompress(this->hic, ICDECOMPRESS_NOTKEYFRAME, 
			 &this->bih, this->buf,
			 &this->o_bih, img->base[0]);
      */
      ret = ICDecompress(this->hic, ICDECOMPRESS_NOTKEYFRAME, 
			 &this->bih, this->buf,
			 &this->o_bih, this->img_buffer);

      if (this->outfmt==IMGFMT_YUY2) {
	/* already decoded into YUY2 format by DLL */
	memcpy(img->base[0], this->img_buffer, this->bih.biHeight*this->bih.biWidth*2);
      } else {
	/* now, convert rgb to yuv */
	int row, col;
#if	HAS_SLOW_MULT
	int32_t *ctab = rgb_ycc_tab;
#endif

	profiler_start_count (this->prof_rgb2yuv);

	for (row=0; row<this->bih.biHeight; row++) {

	  uint16_t *pixel, *out;

	  pixel = (uint16_t *) ( (uint8_t *)this->img_buffer + 2 * row * this->o_bih.biWidth );
	  out = (uint16_t *) (img->base[0] + 2 * row * this->o_bih.biWidth );

	  for (col=0; col<this->o_bih.biWidth; col++, pixel++, out++) {
	    
	    uint8_t   r,g,b;
	    uint8_t   y,u,v;
	    
	    b = (*pixel & 0x001F) << 3;
	    g = (*pixel & 0x03E0) >> 5 << 3;
	    r = (*pixel & 0x7C00) >> 10 << 3;
	    
#if	HAS_SLOW_MULT
	    y = (ctab[r+R_Y_OFF] + ctab[g+G_Y_OFF] + ctab[b+B_Y_OFF]) >> SCALEBITS;
	    if (!(col & 0x0001)) {
	      /* even pixel, do u */
	      u = (ctab[r+R_CB_OFF] + ctab[g+G_CB_OFF] + ctab[b+B_CB_OFF]) >> SCALEBITS;
	      *out = ( (uint16_t) u << 8) | (uint16_t) y;
	    } else {
	      /* odd pixel, do v */
	      v = (ctab[r+R_CR_OFF] + ctab[g+G_CR_OFF] + ctab[b+B_CR_OFF]) >> SCALEBITS;
	      *out = ( (uint16_t) v << 8) | (uint16_t) y;
	    }
#else
	    y = (FIX(0.299) * r + FIX(0.587) * g + FIX(0.114) * b + ONE_HALF) >> SCALEBITS;
	    if (!(col & 0x0001)) {
	      /* even pixel, do u */
	      u = (- FIX(0.16874) * r - FIX(0.33126) * g + FIX(0.5) * b + CBCR_OFFSET + ONE_HALF-1) >> SCALEBITS;
	      *out = ( (uint16_t) u << 8) | (uint16_t) y;
	    } else {
	      /* odd pixel, do v */
	      v = (FIX(0.5) * r - FIX(0.41869) * g - FIX(0.08131) * b + CBCR_OFFSET + ONE_HALF-1) >> SCALEBITS;
	      *out = ( (uint16_t) v << 8) | (uint16_t) y;
	    }
#endif
	    //printf("r %02x g %02x b %02x y %02x u %02x v %02x\n",r,g,b,y,u,v);
	  }
	}

	profiler_stop_count (this->prof_rgb2yuv);
      }

      img->PTS = buf->PTS;
      if(ret) {
	printf("Error decompressing frame, err=%ld\n", (long)ret); 
	img->bFrameBad = 1;
      } else
	img->bFrameBad = 0;
      
      if (img->copy) {
	int height = abs(this->o_bih.biHeight);
	int stride = this->o_bih.biWidth;
	uint8_t* src[3];
	  
	src[0] = img->base[0];
	  
	while ((height -= 16) >= 0) {
	  img->copy(img, src);
	  src[0] += 32 * stride;
	}
      }

      img->draw(img);
      img->free(img);

      this->size = 0;
    }

    /* printf ("w32codec: processing packet done\n"); */
  }
}

static void w32v_close (video_decoder_t *this_gen) {

  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  free (this->img_buffer);

  this->video_out->close(this->video_out);
}

static char *w32v_get_id(void) {
  return "vfw (win32) video decoder";
}

/*
 * audio stuff
 */

static int w32a_can_handle (audio_decoder_t *this_gen, int buf_type) {

  int codec = buf_type & 0xFFFF0000;

  return ( (codec == BUF_AUDIO_DIVXA) ||
	   (codec == BUF_AUDIO_MSADPCM) ||
	   (codec == BUF_AUDIO_IMAADPCM) ||
	   (codec == BUF_AUDIO_MSGSM) );
}

static char* get_auds_codec_name(w32a_decoder_t *this, int buf_type) {

  switch (buf_type) {
  case BUF_AUDIO_DIVXA:
    return "divxa32.acm";
  case BUF_AUDIO_MSADPCM:
    return "msadp32.acm";
  case BUF_AUDIO_IMAADPCM:
    return "imaadp32.acm";
  case BUF_AUDIO_MSGSM:
    return "msgsm32.acm";
  }
  printf ("w32codec: this didn't happen: unknown audio buf type %08x\n",
	  buf_type);
  return NULL;
}

static void w32a_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  this->audio_out  = audio_out;
  this->output_open = 0;
  this->decoder_ok = 0;
}

static int w32a_init_audio (w32a_decoder_t *this,
			    WAVEFORMATEX *in_fmt_,
			    int buf_type) {

  HRESULT ret;
  static WAVEFORMATEX wf;     
  /* long in_size=in_fmt_->nBlockAlign; */
  static WAVEFORMATEX *in_fmt;

  in_fmt = (WAVEFORMATEX *) malloc (64);

  memcpy (in_fmt, in_fmt_, sizeof (WAVEFORMATEX) + in_fmt_->cbSize);

  this->srcstream = 0;
  this->num_channels  = in_fmt->nChannels;
  
  if (this->output_open)
    this->audio_out->close (this->audio_out);

  this->output_open = this->audio_out->open( this->audio_out, 
					      16, in_fmt->nSamplesPerSec, 
					      (in_fmt->nChannels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO);
  if (!this->output_open) {
    printf("ACM_Decoder: Cannot open audio output device\n");
    return 0;
  }

  wf.nChannels       = in_fmt->nChannels;
  wf.nSamplesPerSec  = in_fmt->nSamplesPerSec;
  wf.nAvgBytesPerSec = 2*wf.nSamplesPerSec*wf.nChannels;
  wf.wFormatTag      = WAVE_FORMAT_PCM;
  wf.nBlockAlign     = 2*in_fmt->nChannels;
  wf.wBitsPerSample  = 16;
  wf.cbSize          = 0;
  
  win32_codec_name = get_auds_codec_name (this, buf_type);
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
	this->audio_out->write (this->audio_out,
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

    this->decoder_ok = w32a_init_audio (this, (WAVEFORMATEX *)buf->content, buf->type);
  } else if (this->decoder_ok) {

    w32a_decode_audio (this, buf->content, buf->size,
		       buf->decoder_info[0]==2, 
		       buf->PTS);
  }
}



static void w32a_close (audio_decoder_t *this_gen) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  acmStreamClose(this->srcstream, 0);

  if (!this->output_open) {
    this->audio_out->close (this->audio_out);
    this->output_open = 0;
  }

}

static char *w32a_get_id(void) {
  return "vfw (win32) audio decoder";
}

video_decoder_t *init_video_decoder_plugin (int iface_version, config_values_t *cfg) {

  w32v_decoder_t *this ;

  if (iface_version != 2) {
    printf( "w32codec: plugin doesn't support plugin API version %d.\n"
	    "w32codec: this means there's a version mismatch between xine and this "
	    "w32codec: decoder plugin.\nInstalling current input plugins should help.\n",
	    iface_version);
    
    return NULL;
  }

  win32_def_path = cfg->lookup_str (cfg, "win32_path", "/usr/lib/win32");

  this = (w32v_decoder_t *) malloc (sizeof (w32v_decoder_t));

  this->video_decoder.interface_version   = 2;
  this->video_decoder.can_handle          = w32v_can_handle;
  this->video_decoder.init                = w32v_init;
  this->video_decoder.decode_data         = w32v_decode_data;
  this->video_decoder.close               = w32v_close;
  this->video_decoder.get_identifier      = w32v_get_id;
  this->video_decoder.priority            = 1;

  this->prof_rgb2yuv = profiler_allocate_slot ("w32codec rgb2yuv convert");

  return (video_decoder_t *) this;
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  w32a_decoder_t *this ;

  if (iface_version != 2) {
    printf( "w32codec: plugin doesn't support plugin API version %d.\n"
	    "w32codec: this means there's a version mismatch between xine and this "
	    "w32codec: decoder plugin.\nInstalling current input plugins should help.\n",
	    iface_version);

    return NULL;
  }

  win32_def_path = cfg->lookup_str (cfg, "win32_path", "/usr/lib/win32");

  this = (w32a_decoder_t *) malloc (sizeof (w32a_decoder_t));

  this->audio_decoder.interface_version   = 2;
  this->audio_decoder.can_handle          = w32a_can_handle;
  this->audio_decoder.init                = w32a_init;
  this->audio_decoder.decode_data         = w32a_decode_data;
  this->audio_decoder.close               = w32a_close;
  this->audio_decoder.get_identifier      = w32a_get_id;
  this->audio_decoder.priority            = 1;
  
  return (audio_decoder_t *) this;
}

