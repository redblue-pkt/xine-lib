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
 * $Id: w32codec.c,v 1.95 2002/09/19 21:39:45 guenter Exp $
 *
 * routines for using w32 codecs
 * DirectShow support by Miguel Freitas (Nov/2001)
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
#include "wine/ldt_keeper.h"

#define NOAVIFILE_HEADERS
#include "DirectShow/guids.h"
#include "DirectShow/DS_AudioDecoder.h"
#include "DirectShow/DS_VideoDecoder.h"

#include "xine_internal.h"
#include "video_out.h"
#include "audio_out.h"
#include "buffer.h"
#include "xineutils.h"


#define LOG


static GUID CLSID_Voxware =
{
     0x73f7a062, 0x8829, 0x11d1,
     { 0xb5, 0x50, 0x00, 0x60, 0x97, 0x24, 0x2d, 0x8d }
};
    
static GUID CLSID_Acelp =
{
     0x4009f700, 0xaeba, 0x11d1,
     { 0x83, 0x44, 0x00, 0xc0, 0x4f, 0xb9, 0x2e, 0xb7 }
};
  
static GUID wmv1_clsid =
{
	0x4facbba1, 0xffd8, 0x4cd7,
	{0x82, 0x28, 0x61, 0xe2, 0xf6, 0x5c, 0xb1, 0xae}
};

static GUID wmv2_clsid =
{
	0x521fb373, 0x7654, 0x49f2,
	{0xbd, 0xb1, 0x0c, 0x6e, 0x66, 0x60, 0x71, 0x4f}
};

static GUID dvsd_clsid =
{
	0xB1B77C00, 0xC3E4, 0x11CF,
	{0xAF, 0x79, 0x00, 0xAA, 0x00, 0xB6, 0x7A, 0x42}
};

static GUID msmpeg4_clsid =
{
	0x82CCd3E0, 0xF71A, 0x11D0,
	{ 0x9f, 0xe5, 0x00, 0x60, 0x97, 0x78, 0xea, 0x66}
};

static GUID mss1_clsid =
{
	0x3301a7c4, 0x0a8d, 0x11d4,
	{ 0x91, 0x4d, 0x00, 0xc0, 0x4f, 0x61, 0x0d, 0x24 }
};


/* some data is shared inside wine loader.
 * this mutex seems to avoid some segfaults
 */
static pthread_mutex_t win32_codec_mutex;
static pthread_once_t once_control = PTHREAD_ONCE_INIT;
extern char*   win32_codec_name; 
extern char*   win32_def_path;

#define VIDEOBUFSIZE 128*1024

typedef struct w32v_decoder_s {
  video_decoder_t   video_decoder;

  xine_t           *xine;
  
  vo_instance_t    *video_out;
  int64_t           video_step;
  int               decoder_ok;

  BITMAPINFOHEADER  *bih, o_bih; 
  char              scratch1[16]; /* some codecs overflow o_bih */
  HIC               hic;
  int               yuv_supported ;
  int		    yuv_hack_needed ;
  int               flipped ;
  unsigned char    *buf;
  int               bufsize;
  void             *img_buffer;
  int               size;
  long		    outfmt;
  
  /* profiler */
  int		    prof_rgb2yuv;

  int               ex_functions;
  int               ds_driver;
  GUID             *guid;
  DS_VideoDecoder  *ds_dec;

  int               stream_id;
  int               skipframes;  
  
  ldt_fs_t *ldt_fs;
} w32v_decoder_t;

typedef struct w32a_decoder_s {
  audio_decoder_t   audio_decoder;
  
  xine_t           *xine;

  ao_instance_t    *audio_out;
  int               output_open;
  int               decoder_ok;

  unsigned char    *buf;
  int               size;   
  int64_t           pts;
  
  /* these are used for pts estimation */
  int64_t           lastpts, sumpts, sumsize;

  unsigned char    *outbuf;
  int               outsize;
      
  HACMSTREAM        srcstream;
  int               rec_audio_src_size;
  int               max_audio_src_size;
  int               num_channels;
  int               rate;
  
  int               ds_driver;
  GUID             *guid;
  DS_AudioDecoder  *ds_dec;
  
  ldt_fs_t *ldt_fs;
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
  this->ds_driver = 0;
  this->ex_functions = 0;
    
  buf_type &= 0xffff0000;

  switch (buf_type) {
  case BUF_VIDEO_MSMPEG4_V1:
  case BUF_VIDEO_MSMPEG4_V2:
    /* Microsoft MPEG-4 v1/v2 */
    /* old dll is disabled now due segfaults 
     * (using directshow instead)
    this->yuv_supported=1;
    this->yuv_hack_needed=1;
    this->flipped=1;
    return "mpg4c32.dll";
    */
    this->yuv_supported=1;
    this->ds_driver = 1;
    this->guid=&msmpeg4_clsid;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("MS MPEG-4 V1/V2");
    return "mpg4ds32.ax";    

  case BUF_VIDEO_MSMPEG4_V3:
    /* Microsoft MPEG-4 v3 */
    this->yuv_supported=1;
    this->yuv_hack_needed=1;
    this->flipped=1;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("MS MPEG-4 V3");
    return "divxc32.dll";

  case BUF_VIDEO_IV50:
    /* Video in Indeo Video 5 format */
    this->yuv_supported=1;   /* YUV pic is upside-down :( */
    this->flipped=0;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Indeo Video 5");
    return "ir50_32.dll";

  case BUF_VIDEO_IV41:
    /* Video in Indeo Video 4.1 format */
    this->flipped=1;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Indeo Video 4.1");
    return "ir41_32.dll";
    
  case BUF_VIDEO_IV32:
    /* Video in Indeo Video 3.2 format */
    this->flipped=1;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Indeo Video 3.2");
    return "ir32_32.dll";
    
  case BUF_VIDEO_IV31:
    /* Video in Indeo Video 3.1 format */
    this->flipped=1;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Indeo Video 3.1");
    return "ir32_32.dll";

  case BUF_VIDEO_CINEPAK:
    /* Video in Cinepak format */
    this->flipped=1;
    this->yuv_supported=0;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Cinepak");
    return "iccvid.dll";

    /*** Only 16bit .DLL available (can't load under linux) ***
	 case mmioFOURCC('V', 'C', 'R', '1'):
	 printf("Video in ATI VCR1 format\n");
	 return "ativcr1.dll";
    */

  case BUF_VIDEO_ATIVCR2:
    /* Video in ATI VCR2 format */
    this->yuv_supported=1;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("ATI VCR2");
    return "ativcr2.dll";
    
  case BUF_VIDEO_I263:
    /* Video in I263 format */
    this->flipped=1;
    this->yuv_supported=1;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("I263");
    return "i263_32.drv";

  case BUF_VIDEO_MSVC:
    /* Video in Windows Video 1 */
    /* note: can't play streams with 8bpp */
    this->flipped=1;
    this->yuv_supported=0;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("MS Windows Video 1");
    return "msvidc32.dll";    
    
  case BUF_VIDEO_DV:
    /* Sony DV Codec (not working yet) */
    this->yuv_supported=1;
    this->ds_driver = 1;
    this->guid=&dvsd_clsid;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Sony DV");
    return "qdv.dll";    
  
  case BUF_VIDEO_WMV7:
    this->yuv_supported=1;
    this->ds_driver = 1;
    this->guid=&wmv1_clsid;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("MS WMV 7");
    return "wmvds32.ax";    
  
  case BUF_VIDEO_WMV8:
    this->yuv_supported=1;
    this->ds_driver = 1;
    this->guid=&wmv2_clsid;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("MS WMV 8");
    return "wmv8ds32.ax";    
  
  case BUF_VIDEO_VP31:
    this->yuv_supported=1;
    this->ex_functions=1;
    this->flipped=1;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("VP 31");
    return "vp31vfw.dll";    

  case BUF_VIDEO_MSS1:
    this->ds_driver = 1;
    this->guid=&mss1_clsid;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Windows Screen Video");
    return "msscds32.ax";    

  case BUF_VIDEO_XXAN:
    this->flipped=1;
    this->yuv_supported=0;
    this->xine->meta_info[XINE_META_INFO_VIDEOCODEC] 
      = strdup ("Wing Commander IV Video");
    return "xanlib.dll";    
    
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

static void w32v_init (video_decoder_t *this_gen, vo_instance_t *video_out) {

  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  this->video_out  = video_out;
  this->decoder_ok = 0;
}


static void w32v_init_codec (w32v_decoder_t *this, int buf_type) {

  HRESULT  ret;
  uint32_t vo_cap;
  int outfmt;

#ifdef LOG
  printf ("w32codec: init codec...\n");
#endif

  memset(&this->o_bih, 0, sizeof(BITMAPINFOHEADER));
  this->o_bih.biSize = sizeof(BITMAPINFOHEADER);
  
  this->ldt_fs = Setup_LDT_Keeper();
  
  outfmt = IMGFMT_15RGB;
  if (this->yuv_supported) {
    vo_cap = this->video_out->get_capabilities (this->video_out);
    if (vo_cap & VO_CAP_YUY2)
      outfmt = IMGFMT_YUY2;
  }

  this->hic = ICOpen (mmioFOURCC('v','i','d','c'), 
		      this->bih->biCompression, 
		      ICMODE_FASTDECOMPRESS);

  if(!this->hic){
    xine_log (this->xine, XINE_LOG_MSG, 
              "w32codec: ICOpen failed! unknown codec %08lx / wrong parameters?\n",
              this->bih->biCompression);
    this->decoder_ok = 0;
    return;
  }

  ret = ICDecompressGetFormat(this->hic, this->bih, &this->o_bih);
  if(ret){
    xine_log (this->xine, XINE_LOG_MSG, 
              "w32codec: ICDecompressGetFormat (%.4s %08lx/%d) failed: Error %ld\n",
              (char*)&this->o_bih.biCompression, this->bih->biCompression, 
              this->bih->biBitCount, (long)ret);                
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
    this->o_bih.biHeight=-this->bih->biHeight; 

  if(outfmt==IMGFMT_YUY2 && !this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2');
  else 
    this->o_bih.biCompression = 0;
      
  ret = (!this->ex_functions) 
        ?ICDecompressQuery(this->hic, this->bih, &this->o_bih)
        :ICDecompressQueryEx(this->hic, this->bih, &this->o_bih);
  
  if(ret){
    xine_log (this->xine, XINE_LOG_MSG,
              "w32codec: ICDecompressQuery failed: Error %ld\n", (long)ret);
    this->decoder_ok = 0;
    return;
  }
  
  ret = (!this->ex_functions) 
        ?ICDecompressBegin(this->hic, this->bih, &this->o_bih)
        :ICDecompressBeginEx(this->hic, this->bih, &this->o_bih);
  
  if(ret){
    xine_log (this->xine, XINE_LOG_MSG,
              "w32codec: ICDecompressBegin failed: Error %ld\n", (long)ret);
    this->decoder_ok = 0;
    return;
  }

  if (outfmt==IMGFMT_YUY2 && this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2'); 

  this->size = 0;

  if ( this->img_buffer )
    free (this->img_buffer);
  this->img_buffer = malloc (this->o_bih.biSizeImage);
    
  if ( this->buf )
    free (this->buf);
  this->bufsize = VIDEOBUFSIZE;
  this->buf = malloc(this->bufsize);

  this->video_out->open (this->video_out);

  this->outfmt = outfmt;
  this->decoder_ok = 1;
}

static void w32v_init_ds_codec (w32v_decoder_t *this, int buf_type) {
  uint32_t vo_cap;
  int outfmt;
  
  printf ("w32codec: init Direct Show video codec...\n");
  
  memset(&this->o_bih, 0, sizeof(BITMAPINFOHEADER));
  this->o_bih.biSize = sizeof(BITMAPINFOHEADER);

  this->ldt_fs = Setup_LDT_Keeper();
  
  /* hack: dvsd is the only fourcc accepted by qdv.dll */
  if( buf_type ==  BUF_VIDEO_DV )
    this->bih->biCompression = mmioFOURCC('d','v','s','d');

  this->ds_dec = DS_VideoDecoder_Open(win32_codec_name, this->guid,
                                        this->bih, this->flipped, 0);
  
  if(!this->ds_dec){
    xine_log (this->xine, XINE_LOG_MSG,
              "w32codec: DS_VideoDecoder failed! unknown codec %08lx / wrong parameters?\n",
              this->bih->biCompression);
    this->decoder_ok = 0;
    return;
  }

  outfmt = IMGFMT_15RGB;
  if (this->yuv_supported) {
    vo_cap = this->video_out->get_capabilities (this->video_out);
    if (vo_cap & VO_CAP_YUY2)
      outfmt = IMGFMT_YUY2;
  }

  if(outfmt==IMGFMT_YUY2 || outfmt==IMGFMT_15RGB )
    this->o_bih.biBitCount=16;
  else
    this->o_bih.biBitCount=outfmt&0xFF;

  this->o_bih.biWidth = this->bih->biWidth;
  this->o_bih.biHeight = this->bih->biHeight;
  
  this->o_bih.biSizeImage = this->o_bih.biWidth * this->o_bih.biHeight
      * this->o_bih.biBitCount / 8;

  if (this->flipped)
    this->o_bih.biHeight=-this->bih->biHeight; 

  if(outfmt==IMGFMT_YUY2 && !this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2');
  else 
    this->o_bih.biCompression = 0;
      
  DS_VideoDecoder_SetDestFmt(this->ds_dec, this->o_bih.biBitCount, this->o_bih.biCompression);

  if (outfmt==IMGFMT_YUY2 && this->yuv_hack_needed)
    this->o_bih.biCompression = mmioFOURCC('Y','U','Y','2'); 
  
  DS_VideoDecoder_StartInternal(this->ds_dec);  
  
  this->size = 0;

  if ( this->img_buffer )
    free (this->img_buffer);
  this->img_buffer = malloc (this->o_bih.biSizeImage);
  
  if ( this->buf )
    free (this->buf);
  this->bufsize = VIDEOBUFSIZE;
  this->buf = malloc(this->bufsize);
  
  this->video_out->open (this->video_out);

  this->outfmt = outfmt;
  this->decoder_ok = 1;
}


static void w32v_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  
#ifdef LOG
  printf ("w32codec: processing packet type = %08x, buf->decoder_flags=%08x\n", 
	  buf->type, buf->decoder_flags);
#endif
  
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;
  
  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    if ( buf->type & 0xff )
      return;
    
#ifdef LOG
    printf ("w32codec: processing header ...\n"); 
#endif

    /* init package containing bih */
    if( this->bih )
      free( this->bih );
    this->bih = malloc(buf->size);
    memcpy ( this->bih, buf->content, buf->size );
    this->video_step = buf->decoder_info[1];
#ifdef LOG
    printf ("w32codec: video_step is %lld\n", this->video_step);
#endif

    pthread_mutex_lock(&win32_codec_mutex);
    win32_codec_name = get_vids_codec_name (this, buf->type);

    if( !this->ds_driver )
      w32v_init_codec (this, buf->type);
    else
      w32v_init_ds_codec (this, buf->type);
    
    if( !this->decoder_ok ) {
      xine_log (this->xine, XINE_LOG_MSG,
              "w32codec: decoder failed to start. Is '%s' installed?\n", 
              win32_codec_name );
      xine_report_codec( this->xine, XINE_CODEC_VIDEO, 0, buf->type, 0);
    }
                                         
    pthread_mutex_unlock(&win32_codec_mutex);
      
    this->stream_id = -1;
    this->skipframes = 0;
    
  } else if (this->decoder_ok) {

#ifdef LOG
    printf ("w32codec: processing packet ...\n"); 
#endif
    if( (int) buf->size <= 0 )
        return;
       
    if( this->stream_id < 0 )
       this->stream_id = buf->type & 0xff;
    
    if( this->stream_id != (buf->type & 0xff) )
       return;
    
    if( this->size + buf->size > this->bufsize ) {
      this->bufsize = this->size + 2 * buf->size;
      printf("w32codec: increasing source buffer to %d to avoid overflow.\n", 
        this->bufsize);
      this->buf = realloc( this->buf, this->bufsize );
    }
    
    xine_fast_memcpy (&this->buf[this->size], buf->content, buf->size);

    this->size += buf->size;

    if (buf->decoder_flags & BUF_FLAG_FRAMERATE)
      this->video_step = buf->decoder_info[0];

    if (buf->decoder_flags & BUF_FLAG_FRAME_END)  {

      HRESULT     ret;
      int         flags;
      vo_frame_t *img;
      uint8_t    *img_buffer = this->img_buffer;

      /* decoder video frame */

      this->bih->biSizeImage = this->size;
     
      img = this->video_out->get_frame (this->video_out,
					this->bih->biWidth, 
					this->bih->biHeight, 
					42, 
					IMGFMT_YUY2,
					VO_BOTH_FIELDS);

      img->duration = this->video_step;

#ifdef LOG
	printf ("w32codec: frame duration is %lld\n", this->video_step);
#endif

      if (this->outfmt==IMGFMT_YUY2)
         img_buffer = img->base[0];
         
      flags = 0;
      if( !(buf->decoder_flags & BUF_FLAG_KEYFRAME) )
        flags |= ICDECOMPRESS_NOTKEYFRAME;
      if( this->skipframes )
        flags |= ICDECOMPRESS_HURRYUP|ICDECOMPRESS_PREROL;
      
      pthread_mutex_lock(&win32_codec_mutex);
      if( !this->ds_driver )
        ret = (!this->ex_functions)
              ?ICDecompress(this->hic, flags,
			    this->bih, this->buf, &this->o_bih, 
			    (this->skipframes)?NULL:img_buffer)
              :ICDecompressEx(this->hic, flags,
			    this->bih, this->buf, &this->o_bih,
			    (this->skipframes)?NULL:img_buffer); 
      else {
        ret = DS_VideoDecoder_DecodeInternal(this->ds_dec, this->buf, this->size,
                            buf->decoder_flags & BUF_FLAG_KEYFRAME,
                            (this->skipframes)?NULL:img_buffer);
      }
      pthread_mutex_unlock(&win32_codec_mutex);
                         
      if (!this->skipframes) {
        if (this->outfmt==IMGFMT_YUY2) {
	  /* already decoded into YUY2 format by DLL */
	  /*
	  xine_fast_memcpy(img->base[0], this->img_buffer,
	                   this->bih.biHeight*this->bih.biWidth*2);
	  */
        } else {
	  /* now, convert rgb to yuv */
	  int row, col;
#if	HAS_SLOW_MULT
	  int32_t *ctab = rgb_ycc_tab;
#endif
  
	  xine_profiler_start_count (this->prof_rgb2yuv);
  
	  for (row=0; row<this->bih->biHeight; row++) {
  
	    uint16_t *pixel, *out;
  
	    pixel = (uint16_t *) ( (uint8_t *)this->img_buffer + 2 * row * this->o_bih.biWidth );
	    out = (uint16_t *) (img->base[0] + row * img->pitches[0] );
  
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
  
	  xine_profiler_stop_count (this->prof_rgb2yuv);
        }
      }
      
      img->pts = buf->pts;
      if (ret || this->skipframes) {
        if (!this->skipframes)
	  printf("w32codec: Error decompressing frame, err=%ld\n", (long)ret); 
	img->bad_frame = 1;
#ifdef LOG
	printf ("w32codec: BAD FRAME, duration is %d\n", img->duration);
#endif
      } else {
	img->bad_frame = 0;
#ifdef LOG
	printf ("w32codec: GOOD FRAME, duration is %d\n\n", img->duration);
#endif
      }
      
      if (img->copy && !this->skipframes) {
	int height = abs(this->o_bih.biHeight);
	uint8_t *src[3];

	src[0] = img->base[0];

	while ((height -= 16) >= 0) {
	  img->copy(img, src);
	  src[0] += 16 * img->pitches[0];
	}
      }

      this->skipframes = img->draw(img);

#ifdef LOG
      printf ("w32codec: skipframes is %d\n", this->skipframes);
#endif

      if (this->skipframes < 0)
        this->skipframes = 0;
      img->free(img);

      this->size = 0;
    }

    /* printf ("w32codec: processing packet done\n"); */
  }
}

static void w32v_flush (video_decoder_t *this_gen) {
}

static void w32v_reset (video_decoder_t *this_gen) {

  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;
  
  /* FIXME: need to improve this function. currently it
     doesn't avoid artifacts when seeking. */
  
  pthread_mutex_lock(&win32_codec_mutex);
  if ( !this->ds_driver ) {
    if( this->hic )
    {
      if (!this->ex_functions) 
        ICDecompressBegin(this->hic, this->bih, &this->o_bih);
      else
        ICDecompressBeginEx(this->hic, this->bih, &this->o_bih);
    }
  } else {
  }
  this->size = 0;
  pthread_mutex_unlock(&win32_codec_mutex);
}


static void w32v_close (video_decoder_t *this_gen) {

  w32v_decoder_t *this = (w32v_decoder_t *) this_gen;

  pthread_mutex_lock(&win32_codec_mutex);
  if ( !this->ds_driver ) {
    if( this->hic ) {
      ICDecompressEnd(this->hic);
      ICClose(this->hic);
    }
  } else {
    if( this->ds_dec )
      DS_VideoDecoder_Destroy(this->ds_dec);
  }
  Restore_LDT_Keeper( this->ldt_fs );
  pthread_mutex_unlock(&win32_codec_mutex);

  if ( this->img_buffer ) {
    free (this->img_buffer);
    this->img_buffer = NULL;
  }
  
  if ( this->buf ) {
    free (this->buf);
    this->buf = NULL;
  }

  if( this->bih ) {
    free( this->bih );
    this->bih = NULL;
  }

  if( this->decoder_ok )
  {  
    this->decoder_ok = 0;
    this->video_out->close(this->video_out);
  }
}

static char *w32v_get_id(void) {
  return "vfw (win32) video decoder";
}

static void w32v_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

/*
 * audio stuff
 */

static char* get_auds_codec_name(w32a_decoder_t *this, int buf_type) {

  buf_type = buf_type & 0xFFFF0000;
  this->ds_driver=0;

  switch (buf_type) {
  case BUF_AUDIO_DIVXA:
    this->xine->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("DivX audio (WMA)");
    return "divxa32.acm";
  case BUF_AUDIO_MSADPCM:
    this->xine->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("MS ADPCM");
    return "msadp32.acm";
  case BUF_AUDIO_MSIMAADPCM:
    this->xine->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("MS IMA ADPCM");
    return "imaadp32.acm";
  case BUF_AUDIO_MSGSM:
    this->xine->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("MS GSM");
    return "msgsm32.acm";
  case BUF_AUDIO_IMC:
    this->xine->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("Intel Music Coder");
    return "imc32.acm";
  case BUF_AUDIO_LH:
    this->xine->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("Lernout & Hauspie");
    return "lhacm.acm";
  case BUF_AUDIO_VOXWARE:
    this->ds_driver=1;
    this->guid=&CLSID_Voxware;
    this->xine->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("Voxware Metasound");
    return "voxmsdec.ax";
  case BUF_AUDIO_ACELPNET:
    this->ds_driver=1;
    this->guid=&CLSID_Acelp;
    this->xine->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("ACELP.net");
    return "acelpdec.ax";
  case BUF_AUDIO_VIVOG723:
    this->xine->meta_info[XINE_META_INFO_AUDIOCODEC] 
      = strdup ("Vivo G.723/Siren Audio Codec");
    return "vivog723.acm";
  }
  printf ("w32codec: this didn't happen: unknown audio buf type %08x\n",
	  buf_type);
  return NULL;
}

static void w32a_reset (audio_decoder_t *this_gen) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  this->size = 0;
}


static void w32a_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  this->audio_out  = audio_out;
  this->output_open = 0;
  this->decoder_ok = 0;
  
  this->buf = NULL;
  this->outbuf = NULL;
}

static int w32a_init_audio (w32a_decoder_t *this, buf_element_t *buf ) {

  HRESULT ret;
  void *ret2;
  static WAVEFORMATEX wf;     
  static WAVEFORMATEX *in_fmt;
  unsigned long in_size;
  unsigned long out_size;
  audio_buffer_t *audio_buffer;
  int audio_buffer_mem_size;

  in_fmt = (WAVEFORMATEX *) malloc (buf->size);
  memcpy (in_fmt, buf->content, buf->size);
  in_size=in_fmt->nBlockAlign;

  this->srcstream = 0;
  this->num_channels  = in_fmt->nChannels;
  this->rate = in_fmt->nSamplesPerSec;
  
  if (this->output_open)
    this->audio_out->close (this->audio_out);

  this->output_open = this->audio_out->open( this->audio_out, 
					      16, in_fmt->nSamplesPerSec, 
					      (in_fmt->nChannels == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO);
  if (!this->output_open) {
    printf("w32codec: (ACM_Decoder) Cannot open audio output device\n");
    return 0;
  }
  
  audio_buffer = this->audio_out->get_buffer (this->audio_out);
  audio_buffer_mem_size = audio_buffer->mem_size;
  audio_buffer->num_frames = 0;
  audio_buffer->vpts       = 0;
  this->audio_out->put_buffer (this->audio_out, audio_buffer);

  wf.nChannels       = in_fmt->nChannels;
  wf.nSamplesPerSec  = in_fmt->nSamplesPerSec;
  wf.nAvgBytesPerSec = 2*wf.nSamplesPerSec*wf.nChannels;
  wf.wFormatTag      = WAVE_FORMAT_PCM;
  wf.nBlockAlign     = 2*in_fmt->nChannels;
  wf.wBitsPerSample  = 16;
  wf.cbSize          = 0;
  
  this->ldt_fs = Setup_LDT_Keeper();
  win32_codec_name = get_auds_codec_name (this, buf->type);
  
  if( !this->ds_driver ) {
    ret=acmStreamOpen(&this->srcstream,(HACMDRIVER)NULL,
                      in_fmt,
                      &wf,
                      NULL,0,0,0);
    if(ret){
      if(ret==ACMERR_NOTPOSSIBLE)
        xine_log (this->xine, XINE_LOG_MSG,
                  "w32codec: (ACM_Decoder) Unappropriate audio format\n");
      else
        xine_log (this->xine, XINE_LOG_MSG,
                  "w32codec: (ACM_Decoder) acmStreamOpen error %d\n", (int) ret);
      this->srcstream = 0;
      return 0;
    }

    acmStreamSize(this->srcstream, in_size, &out_size, ACM_STREAMSIZEF_SOURCE);
    out_size*=2;
    if(out_size < audio_buffer_mem_size) 
      out_size=audio_buffer_mem_size;
    printf("w32codec: Audio buffer min. size: %d\n",(int)out_size);
  
    acmStreamSize(this->srcstream, out_size, (LPDWORD) &this->rec_audio_src_size, 
      ACM_STREAMSIZEF_DESTINATION);
  } else {
    ret2 = this->ds_dec=DS_AudioDecoder_Open(win32_codec_name,this->guid, in_fmt);
    
    if( ret2 == NULL ) {
      xine_log (this->xine, XINE_LOG_MSG, "w32codec: Error initializing DirectShow Audio\n");
      this->srcstream = 0;
      return 0;
    }
    
    out_size = audio_buffer_mem_size;  
    printf("w32codec: output buffer size: %d\n",(int)out_size);
    this->rec_audio_src_size=DS_AudioDecoder_GetSrcSize(this->ds_dec,out_size);
    
    /* somehow DS_Filters seems to eat more than rec_audio_src_size if the output 
       buffer is big enough. Doubling rec_audio_src_size should make this 
       impossible */
    this->rec_audio_src_size*=2; 
  }
  printf("w32codec: Recommended source buffer size: %d\n", this->rec_audio_src_size); 

  if( this->buf )
    free(this->buf);
  
  if( this->outbuf )
    free(this->outbuf);

  if( this->rec_audio_src_size < in_fmt->nBlockAlign ) {
    this->rec_audio_src_size = in_fmt->nBlockAlign;
    printf("w32codec: adjusting source buffer size to %d\n", this->rec_audio_src_size); 
  }
  
  this->max_audio_src_size = 2 * this->rec_audio_src_size;
  
  this->buf = malloc( this->max_audio_src_size );

  out_size += 32768;
  this->outbuf = malloc( out_size );
  this->outsize = out_size;
      
  this->size = 0;

  return 1;
}


static void w32a_decode_audio (w32a_decoder_t *this,
			       unsigned char *data, 
			       uint32_t size, 
			       int frame_end, 
			       int64_t pts) {

  static ACMSTREAMHEADER ash;
  HRESULT hr;
  int size_read, size_written;
  /* DWORD srcsize=0; */
    
  /* FIXME: this code still far from perfect, there are a/v sync
     issues with some streams.
  */
  
  /* buffer empty -> take pts from package */
  if( !this->size ) {
    this->pts = pts;
    /*
    printf("w32codec: resync pts (%d)\n",this->pts);
    */
    this->sumpts = this->sumsize = 0;
  } else if ( !this->pts ) {
    if( pts )
      this->sumpts += (pts - this->lastpts);
    this->sumsize += size;
  }
  
  /* force resync every 4 seconds */
  if( this->sumpts >= 4 * 90000 && pts ) {
    this->pts = pts - this->size * this->sumpts / this->sumsize;
    /*
    printf("w32codec: estimated resync pts (%d)\n",this->pts);
    */
    this->sumpts = this->sumsize = 0;
  }
  
  if( pts )
    this->lastpts = pts;
     
  if( this->size + size > this->max_audio_src_size ) {
    this->max_audio_src_size = this->size + 2 * size;
    printf("w32codec: increasing source buffer to %d to avoid overflow.\n", 
      this->max_audio_src_size);
    this->buf = realloc( this->buf, this->max_audio_src_size );
  }
  
  xine_fast_memcpy (&this->buf[this->size], data, size);
       
  this->size += size;

  while (this->size >= this->rec_audio_src_size) {
    memset(&ash, 0, sizeof(ash));
    ash.cbStruct=sizeof(ash);
    ash.fdwStatus=0;
    ash.dwUser=0; 
    ash.pbSrc=this->buf;
    ash.cbSrcLength=this->rec_audio_src_size;
    ash.pbDst=this->outbuf;
    ash.cbDstLength=this->outsize;
    
#ifdef LOG
    printf ("decoding %d of %d bytes (%02x %02x %02x %02x ... %02x %02x)\n", 
	    this->rec_audio_src_size, this->size,
	    this->buf[0], this->buf[1], this->buf[2], this->buf[3],
	    this->buf[this->rec_audio_src_size-2], this->buf[this->rec_audio_src_size-1]); 
#endif
    
    pthread_mutex_lock(&win32_codec_mutex);
    if( !this->ds_driver ) {
      hr=acmStreamPrepareHeader(this->srcstream,&ash,0);
      if(hr){
        printf("w32codec: (ACM_Decoder) acmStreamPrepareHeader error %d\n",(int)hr);
        pthread_mutex_unlock(&win32_codec_mutex);
        return;
      }
      
      hr=acmStreamConvert(this->srcstream,&ash,0);
    } else {
      hr=DS_AudioDecoder_Convert(this->ds_dec, ash.pbSrc, ash.cbSrcLength,
			     ash.pbDst, ash.cbDstLength,
		             &size_read, &size_written );
       ash.cbSrcLengthUsed = size_read;
       ash.cbDstLengthUsed = size_written;
    }
    pthread_mutex_unlock(&win32_codec_mutex);
   
    if(hr){
      printf ("w32codec: stream convert error %d, used %d bytes\n",
	      (int)hr,(int)ash.cbSrcLengthUsed);
      this->size-=ash.cbSrcLength;
    } else {
      int DstLengthUsed, bufsize;
      audio_buffer_t *audio_buffer;
      char *p;
#ifdef LOG
      printf ("acmStreamConvert worked, used %d bytes, generated %d bytes\n",
	      ash.cbSrcLengthUsed, ash.cbDstLengthUsed);
#endif
      DstLengthUsed = ash.cbDstLengthUsed;
      p = this->outbuf;
      
      while( DstLengthUsed )
      {
        audio_buffer = this->audio_out->get_buffer (this->audio_out);
        
	if( DstLengthUsed < audio_buffer->mem_size )
	  bufsize = DstLengthUsed;
	else
	  bufsize = audio_buffer->mem_size;
      
        xine_fast_memcpy( audio_buffer->mem, p, bufsize );
	/*
        printf("  outputing %d bytes, pts = %d\n", bufsize, pts );
        */
	audio_buffer->num_frames = bufsize / (this->num_channels*2);
	audio_buffer->vpts       = this->pts;

	this->audio_out->put_buffer (this->audio_out, audio_buffer);
        
	this->pts = 0;
        DstLengthUsed -= bufsize;
	p += bufsize;
      }
    }
    if(ash.cbSrcLengthUsed>=this->size){
      this->size=0;
    } else {
      this->size-=ash.cbSrcLengthUsed;
      xine_fast_memcpy( this->buf, &this->buf [ash.cbSrcLengthUsed], this->size);
    }

    pthread_mutex_lock(&win32_codec_mutex);
    if( !this->ds_driver ) {
      hr=acmStreamUnprepareHeader(this->srcstream,&ash,0);
      if(hr){
        printf("w32codec: (ACM_Decoder) acmStreamUnprepareHeader error %d\n",(int)hr);
      }
    }
    pthread_mutex_unlock(&win32_codec_mutex);
  }
}

static void w32a_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
#ifdef LOG
    printf ("w32codec: preview data ignored.\n");
#endif
    return;
  }
  
  if (buf->decoder_flags & BUF_FLAG_HEADER) {

#ifdef LOG
    printf ("w32codec: got audio header\n");
#endif

    pthread_mutex_lock(&win32_codec_mutex);
    this->decoder_ok = w32a_init_audio (this, buf);
    
    if( !this->decoder_ok ) {
      xine_log (this->xine, XINE_LOG_MSG,
              "w32codec: decoder failed to start. Is '%s' installed?\n", 
              win32_codec_name );
      xine_report_codec( this->xine, XINE_CODEC_AUDIO, 0, buf->type, 0);
    }
    pthread_mutex_unlock(&win32_codec_mutex);
 
  } else if (this->decoder_ok) {
#ifdef LOG
    printf ("w32codec: decoding %d data bytes...\n", buf->size);
#endif

    if( (int)buf->size <= 0 )
      return;

    w32a_decode_audio (this, buf->content, buf->size,
		       buf->decoder_flags & BUF_FLAG_FRAME_END, 
		       buf->pts);
  }
}


static void w32a_close (audio_decoder_t *this_gen) {

  w32a_decoder_t *this = (w32a_decoder_t *) this_gen;
  
  pthread_mutex_lock(&win32_codec_mutex);
  if( !this->ds_driver ) {
    if( this->srcstream ) {
      acmStreamClose(this->srcstream, 0);
      this->srcstream = 0;
    }
  } else {
    if( this->ds_dec )
      DS_AudioDecoder_Destroy(this->ds_dec);
    this->ds_dec = NULL;
  }

  Restore_LDT_Keeper(this->ldt_fs);
  pthread_mutex_unlock(&win32_codec_mutex);

  if( this->buf ) {
    free(this->buf);
    this->buf = NULL;
  }
  
  if( this->outbuf ) {
    free(this->outbuf);
    this->outbuf = NULL;
  }
  
  this->decoder_ok = 0;
  
  if (this->output_open) {
    this->audio_out->close (this->audio_out);
    this->output_open = 0;
  }
}

static char *w32a_get_id(void) {
  return "vfw (win32) audio decoder";
}

static void init_routine(void) {
  pthread_mutex_init (&win32_codec_mutex, NULL);
  w32v_init_rgb_ycc();
}

static void *init_video_decoder_plugin (xine_t *xine, void *data) {

  w32v_decoder_t *this ;
  config_values_t *cfg;

  cfg = xine->config;
  win32_def_path = cfg->register_string (cfg, "codec.win32_path", "/usr/lib/win32",
					 _("path to win32 codec dlls"),
					 NULL, 0, NULL, NULL);

  this = (w32v_decoder_t *) xine_xmalloc (sizeof (w32v_decoder_t));

  this->xine = xine;
  
  this->video_decoder.init                = w32v_init;
  this->video_decoder.decode_data         = w32v_decode_data;
  this->video_decoder.flush               = w32v_flush;
  this->video_decoder.reset               = w32v_reset;
  this->video_decoder.close               = w32v_close;
  this->video_decoder.get_identifier      = w32v_get_id;
  this->video_decoder.dispose             = w32v_dispose;

  pthread_once (&once_control, init_routine);
  
  this->prof_rgb2yuv = xine_profiler_allocate_slot ("w32codec rgb2yuv convert");

#ifdef SYNC_SHUTDOWN
  w32v_instance = NULL;
#endif

  return this;
}

static void w32a_dispose (audio_decoder_t *this_gen) {
  free (this_gen);
}

static void *init_audio_decoder_plugin (xine_t *xine, void *data) {

  w32a_decoder_t *this ;
  config_values_t *cfg;
  
  cfg = xine->config;
  win32_def_path = cfg->register_string (cfg, "codec.win32_path", "/usr/lib/win32",
					 _("path to win32 codec dlls"),
					 NULL, 0, NULL, NULL);

  this = (w32a_decoder_t *) xine_xmalloc (sizeof (w32a_decoder_t));

  this->xine = xine;
    
  this->audio_decoder.init                = w32a_init;
  this->audio_decoder.decode_data         = w32a_decode_data;
  this->audio_decoder.reset               = w32a_reset;
  this->audio_decoder.close               = w32a_close;
  this->audio_decoder.get_identifier      = w32a_get_id;
  this->audio_decoder.dispose             = w32a_dispose;
  
  pthread_once (&once_control, init_routine);

#ifdef SYNC_SHUTDOWN
  w32a_instance = NULL;
#endif
  
  return this;
}

/*
 * exported plugin catalog entry
 */

static uint32_t video_types[] = { 
  BUF_VIDEO_MSMPEG4_V1, BUF_VIDEO_MSMPEG4_V2, BUF_VIDEO_MSMPEG4_V3,
  BUF_VIDEO_IV50, BUF_VIDEO_IV41, BUF_VIDEO_IV32, BUF_VIDEO_IV31,
  BUF_VIDEO_CINEPAK, /* BUF_VIDEO_ATIVCR1, */
  BUF_VIDEO_ATIVCR2, BUF_VIDEO_I263, BUF_VIDEO_MSVC,
  BUF_VIDEO_DV, BUF_VIDEO_WMV7, BUF_VIDEO_WMV8,
  BUF_VIDEO_VP31, BUF_VIDEO_MSS1, BUF_VIDEO_XXAN,
  0
 };

static decoder_info_t dec_info_video = {
  video_types,         /* supported types */
  1                    /* priority        */
};

static uint32_t audio_types[] = { 
  BUF_AUDIO_DIVXA, BUF_AUDIO_MSADPCM, BUF_AUDIO_MSIMAADPCM,
  BUF_AUDIO_MSGSM, BUF_AUDIO_IMC, BUF_AUDIO_LH,
  BUF_AUDIO_VOXWARE, BUF_AUDIO_ACELPNET, BUF_AUDIO_VIVOG723,
  0
 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 10, "win32", XINE_VERSION_CODE, &dec_info_video, init_video_decoder_plugin },
  { PLUGIN_AUDIO_DECODER, 9, "win32", XINE_VERSION_CODE, &dec_info_audio, init_audio_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
