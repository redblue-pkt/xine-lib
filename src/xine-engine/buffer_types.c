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
 * $Id: buffer_types.c,v 1.2 2001/11/07 19:06:15 miguelfreitas Exp $
 *
 *
 * contents:
 *
 * buffer types management. 
 * convert FOURCC and audioformattag to BUF_xxx defines
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "buffer.h"
#include "utils.h"

#define	WINE_TYPEDEFS_ONLY
#include "libw32dll/wine/avifmt.h"
#include "libw32dll/wine/windef.h"
#include "libw32dll/wine/vfw.h"
#include "libw32dll/wine/mmreg.h"

typedef struct video_db_s {
   uint32_t fourcc[20];
   uint32_t buf_type;
   char *name;
} video_db_t;

typedef struct audio_db_s {
   uint16_t formattag[10];
   uint32_t buf_type;
   char *name;
} audio_db_t;


static video_db_t video_db[] = {
{
  {
    mmioFOURCC('m', 'p', 'e', 'g'),
    0
  },
  BUF_VIDEO_MPEG,
  "MPEG 1/2"
},
{
  {
    mmioFOURCC('D', 'I', 'V', 'X'),
    mmioFOURCC('d', 'i', 'v', 'x'),
    mmioFOURCC('D', 'i', 'v', 'x'),
    mmioFOURCC('D', 'i', 'v', 'X'),
    0
  },
  BUF_VIDEO_MPEG4,
  "mpeg4 (opendivx) format"
},
{
  {
    mmioFOURCC('c', 'v', 'i', 'd'),
    0
  },
  BUF_VIDEO_CINEPAK,
  "Cinepak format"
},
{
  {
    mmioFOURCC('s', 'v', 'q', '1'),
    mmioFOURCC('s', 'v', 'q', 'i'),
    0
  },
  BUF_VIDEO_SORENSON,
  "Sorenson"
},
{
  {
    mmioFOURCC('M', 'P', 'G', '4'),
    mmioFOURCC('m', 'p', 'g', '4'),
    mmioFOURCC('M', 'P', '4', '1'),
    mmioFOURCC('m', 'p', '4', '1'),
    mmioFOURCC('M', 'P', '4', '2'),
    mmioFOURCC('m', 'p', '4', '2'),
    mmioFOURCC('D', 'I', 'V', '2'),
    mmioFOURCC('d', 'i', 'v', '2'),
    0
  },
  BUF_VIDEO_MSMPEG4_V12,
  "Microsoft MPEG-4 format v1/v2"
},
{
  {
    mmioFOURCC('M', 'P', '4', '3'),
    mmioFOURCC('m', 'p', '4', '3'),
    mmioFOURCC('D', 'I', 'V', '3'),
    mmioFOURCC('d', 'i', 'v', '3'),
    mmioFOURCC('D', 'I', 'V', '4'),
    mmioFOURCC('d', 'i', 'v', '4'),
    mmioFOURCC('D', 'I', 'V', '5'),
    mmioFOURCC('d', 'i', 'v', '5'),
    mmioFOURCC('D', 'I', 'V', '6'),
    mmioFOURCC('d', 'i', 'v', '6'),
    mmioFOURCC('3', 'I', 'V', '1'),
    0
  },
  BUF_VIDEO_MSMPEG4_V3,
  "Microsoft MPEG-4 format v3"
},
{
  {
    mmioFOURCC('d', 'm', 'b', '1'),
    mmioFOURCC('M', 'J', 'P', 'G'),
    mmioFOURCC('m', 'j', 'p', 'a'),
    mmioFOURCC('m', 'j', 'p', 'b'),
    0
  },
  BUF_VIDEO_MJPEG,
  "motion jpeg format"
},
{
  {
    mmioFOURCC('I', 'V', '5', '0'),
    mmioFOURCC('i', 'v', '5', '0'),
    0
  },
  BUF_VIDEO_IV50,
  "Indeo Video 5.0 format"
},
{
  {
    mmioFOURCC('I', 'V', '4', '1'),
    mmioFOURCC('i', 'v', '4', '1'),
    0
  },
  BUF_VIDEO_IV41,
  "Indeo Video 4.1 format"
},
{
  {
    mmioFOURCC('I', 'V', '3', '2'),
    mmioFOURCC('i', 'v', '3', '2'),
    0
  },
  BUF_VIDEO_IV32,
  "Indeo Video 3.2 format"
},
{
  {
    mmioFOURCC('I', 'V', '3', '1'),
    mmioFOURCC('i', 'v', '3', '1'),
    0
  },
  BUF_VIDEO_IV31,
  "Indeo Video 3.1 format"
},
{
  {
    mmioFOURCC('V', 'C', 'R', '1'),
    0
  },
  BUF_VIDEO_ATIVCR1,
  "ATI VCR1 format"
},
{
  {
    mmioFOURCC('V', 'C', 'R', '2'),
    0
  },
  BUF_VIDEO_ATIVCR2,
  "ATI VCR2 format"
},
{
  {
    mmioFOURCC('I', '2', '6', '3'),
    mmioFOURCC('i', '2', '6', '3'),
    0
  },
  BUF_VIDEO_I263,
  "I263 format"
},
{
  {
    0
  },
  BUF_VIDEO_RV10,
  ""
},
{
  {
    0
  },
  BUF_VIDEO_FILL,
  ""
},
{
  {
    mmioFOURCC('r','a','w',' '),
    0
  },
  BUF_VIDEO_RGB,
  ""
},
{
  { 
    /* is this right? copied from demux_qt:
    else if (!strncasecmp (video, "yuv2", 4))
    this->video_type = BUF_VIDEO_YUY2;
    */
    mmioFOURCC('y','u','v','2'),
    0
  },
  BUF_VIDEO_YUY2,
  ""
},
{
  {
    mmioFOURCC('j','p','e','g'),
    0
  },
  BUF_VIDEO_JPEG,
  "jpeg"
},
{
  {
    mmioFOURCC('W','M','V','1'),
    0
  },
  BUF_VIDEO_WMV7,
  "Windows Media Video 7"
},
{
  {
    mmioFOURCC('W','M','V','2'),
    0
  },
  BUF_VIDEO_WMV8,
  "Windows Media Video 8"
},
{ { 0 }, 0, "last entry" }
};


static audio_db_t audio_db[] = {
{
  {
    0x2000, 0
  },
  BUF_AUDIO_A52,
  "AC3"
},
{
  {
    0x50, 0x55, 0
  },
  BUF_AUDIO_MPEG,
  "MPEG layer 2/3"
},
{
  {
    0
  },
  BUF_AUDIO_LPCM_BE,
  "Uncompressed PCM big endian"
},
{
  {
    0x01, 0
  },
  BUF_AUDIO_LPCM_LE,
  "Uncompressed PCM little endian"
},
{
  {
    0x160, 0x161, 0
  },
  BUF_AUDIO_DIVXA,
  "DivX audio (WMA)"
},
{
  {
    0
  },
  BUF_AUDIO_DTS,
  "DTS"
},
{
  {
    0x02, 0
  },
  BUF_AUDIO_MSADPCM,
  "MS ADPCM"
},
{
  {
    0x11, 0
  },
  BUF_AUDIO_IMAADPCM,
  "IMA ADPCM"
},
{
  {
    0x31, 0x32, 0
  },
  BUF_AUDIO_MSGSM,
  "MS GSM"
},
{
  {
    0
  },
  BUF_AUDIO_VORBIS,
  "OggVorbis Audio"
},
{ { 0 }, 0, "last entry" }
};


static unsigned long str2ulong(unsigned char *str)
{
  return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

uint32_t fourcc_to_buf_video( void * fourcc ) {
int i, j;
uint32_t fourcc_int;
static uint32_t cached_fourcc=0;
static uint32_t cached_buf_type=0;

  fourcc_int = str2ulong(fourcc);

  if( fourcc_int == cached_fourcc )
    return cached_buf_type;
    
  for( i = 0; video_db[i].buf_type; i++ ) {
    for( j = 0; video_db[i].fourcc[j]; j++ ) {
      if( fourcc_int == video_db[i].fourcc[j] ) {
        cached_fourcc = fourcc_int;
        cached_buf_type = video_db[i].buf_type;
        return video_db[i].buf_type;
      }
    }
  }
  return 0;
}

char * buf_video_name( uint32_t buf_type ) {
int i;
  
  buf_type &= 0xffff0000;
  
  for( i = 0; video_db[i].buf_type; i++ ) {
    if( buf_type == video_db[i].buf_type ) {
        return video_db[i].name;
    }
  }

  return "unknow";
}

uint32_t formattag_to_buf_audio( uint16_t formattag ) {
int i, j;
static uint16_t cached_formattag=0;
static uint32_t cached_buf_type=0;

  if( formattag == cached_formattag )
    return cached_buf_type;
    
  for( i = 0; audio_db[i].buf_type; i++ ) {
    for( j = 0; audio_db[i].formattag[j]; j++ ) {
      if( formattag == audio_db[i].formattag[j] ) {
        cached_formattag = formattag;
        cached_buf_type = audio_db[i].buf_type;
        return audio_db[i].buf_type;
      }
    }
  }
  return 0;
}

char * buf_audio_name( uint32_t buf_type ) {
int i;
  
  buf_type &= 0xffff0000;
  
  for( i = 0; audio_db[i].buf_type; i++ ) {
    if( buf_type == audio_db[i].buf_type ) {
        return audio_db[i].name;
    }
  }

  return "unknow";
}
