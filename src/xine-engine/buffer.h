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
 * $Id: buffer.h,v 1.71 2002/10/12 19:18:48 tmmm Exp $
 *
 *
 * contents:
 *
 * buffer_entry structure - serves as a transport encapsulation
 *   of the mpeg audio/video data through xine
 *
 * free buffer pool management routines
 *
 * FIFO buffer structures/routines
 *
 */

#ifndef HAVE_BUFFER_H
#define HAVE_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>
#include <sys/types.h>

/*
 * buffer types
 *
 * a buffer type ID describes the contents of a buffer
 * it consists of three fields:
 *
 * buf_type = 0xMMDDCCCC
 *
 * MM   : major buffer type (CONTROL, VIDEO, AUDIO, SPU)
 * DD   : decoder selection (e.g. MPEG, OPENDIVX ... for VIDEO)
 * CCCC : channel number or other subtype information for the decoder
 */

#define BUF_MAJOR_MASK       0xFF000000
#define BUF_DECODER_MASK     0x00FF0000

/* control buffer types */

#define BUF_CONTROL_BASE          0x01000000
#define BUF_CONTROL_START         0x01000000
#define BUF_CONTROL_END           0x01010000
#define BUF_CONTROL_QUIT          0x01020000
#define BUF_CONTROL_DISCONTINUITY 0x01030000 /* former AVSYNC_RESET */
#define BUF_CONTROL_NOP           0x01040000
#define BUF_CONTROL_AUDIO_CHANNEL 0x01050000
#define BUF_CONTROL_SPU_CHANNEL   0x01060000
#define BUF_CONTROL_NEWPTS        0x01070000
#define BUF_CONTROL_RESET_DECODER 0x01080000
#define BUF_CONTROL_HEADERS_DONE  0x01090000

/* video buffer types:  (please keep in sync with buffer_types.c) */

#define BUF_VIDEO_BASE		0x02000000
#define BUF_VIDEO_MPEG		0x02000000
#define BUF_VIDEO_MPEG4		0x02010000
#define BUF_VIDEO_CINEPAK	0x02020000
#define BUF_VIDEO_SORENSON_V1	0x02030000
#define BUF_VIDEO_MSMPEG4_V2	0x02040000
#define BUF_VIDEO_MSMPEG4_V3	0x02050000
#define BUF_VIDEO_MJPEG		0x02060000
#define BUF_VIDEO_IV50		0x02070000
#define BUF_VIDEO_IV41		0x02080000
#define BUF_VIDEO_IV32		0x02090000
#define BUF_VIDEO_IV31		0x020a0000
#define BUF_VIDEO_ATIVCR1	0x020b0000
#define BUF_VIDEO_ATIVCR2	0x020c0000
#define BUF_VIDEO_I263		0x020d0000
#define BUF_VIDEO_RV10		0x020e0000
#define BUF_VIDEO_RGB		0x02100000
#define BUF_VIDEO_YUY2		0x02110000
#define BUF_VIDEO_JPEG		0x02120000
#define BUF_VIDEO_WMV7		0x02130000
#define BUF_VIDEO_WMV8		0x02140000
#define BUF_VIDEO_MSVC		0x02150000
#define BUF_VIDEO_DV		0x02160000
#define BUF_VIDEO_REAL    	0x02170000
#define BUF_VIDEO_VP31		0x02180000
#define BUF_VIDEO_H263		0x02190000
#define BUF_VIDEO_3IVX          0x021A0000
#define BUF_VIDEO_CYUV          0x021B0000
#define BUF_VIDEO_DIVX5         0x021C0000
#define BUF_VIDEO_XVID          0x021D0000
#define BUF_VIDEO_SMC		0x021E0000
#define BUF_VIDEO_RPZA		0x021F0000
#define BUF_VIDEO_QTRLE		0x02200000
#define BUF_VIDEO_MSRLE		0x02210000
#define BUF_VIDEO_DUCKTM1	0x02220000
#define BUF_VIDEO_FLI		0x02230000
#define BUF_VIDEO_ROQ		0x02240000
#define BUF_VIDEO_SORENSON_V3	0x02250000
#define BUF_VIDEO_MSMPEG4_V1	0x02260000
#define BUF_VIDEO_MSS1		0x02270000
#define BUF_VIDEO_IDCIN		0x02280000
#define BUF_VIDEO_PGVV		0x02290000
#define BUF_VIDEO_ZYGO		0x022A0000
#define BUF_VIDEO_TSCC		0x022B0000
#define BUF_VIDEO_YVU9		0x022C0000
#define BUF_VIDEO_VQA		0x022D0000
#define BUF_VIDEO_GREY		0x022E0000
#define BUF_VIDEO_XXAN		0x022F0000
#define BUF_VIDEO_WC3		0x02300000
#define BUF_VIDEO_YV12		0x02310000

/* audio buffer types:  (please keep in sync with buffer_types.c) */

#define BUF_AUDIO_BASE		0x03000000
#define BUF_AUDIO_A52		0x03000000
#define BUF_AUDIO_MPEG		0x03010000
#define BUF_AUDIO_LPCM_BE	0x03020000
#define BUF_AUDIO_LPCM_LE	0x03030000
#define BUF_AUDIO_DIVXA		0x03040000
#define BUF_AUDIO_DTS		0x03050000
#define BUF_AUDIO_MSADPCM	0x03060000
#define BUF_AUDIO_MSIMAADPCM	0x03070000
#define BUF_AUDIO_MSGSM		0x03080000 
#define BUF_AUDIO_VORBIS        0x03090000
#define BUF_AUDIO_IMC           0x030a0000
#define BUF_AUDIO_LH            0x030b0000
#define BUF_AUDIO_VOXWARE       0x030c0000
#define BUF_AUDIO_ACELPNET      0x030d0000
#define BUF_AUDIO_AAC           0x030e0000
#define BUF_AUDIO_REAL    	0x030f0000
#define BUF_AUDIO_VIVOG723      0x03100000
#define BUF_AUDIO_DK3ADPCM	0x03110000
#define BUF_AUDIO_DK4ADPCM	0x03120000
#define BUF_AUDIO_ROQ		0x03130000
#define BUF_AUDIO_QTIMAADPCM	0x03140000
#define BUF_AUDIO_MAC3		0x03150000
#define BUF_AUDIO_MAC6		0x03160000
#define BUF_AUDIO_QDESIGN1	0x03170000
#define BUF_AUDIO_QDESIGN2	0x03180000
#define BUF_AUDIO_QCLP		0x03190000
#define BUF_AUDIO_SMJPEG_IMA	0x031A0000
#define BUF_AUDIO_VQA_IMA	0x031B0000
#define BUF_AUDIO_MULAW		0x031C0000
#define BUF_AUDIO_ALAW		0x031D0000
#define BUF_AUDIO_GSM610	0x031E0000

/* spu buffer types:    */
 
#define BUF_SPU_BASE		0x04000000
#define BUF_SPU_CLUT		0x04000000
#define BUF_SPU_PACKAGE		0x04010000
#define BUF_SPU_SUBP_CONTROL	0x04020000
#define BUF_SPU_NAV		0x04030000
#define BUF_SPU_TEXT            0x04040000
#define BUF_SPU_CC              0x04050000

/* demuxer block types: */

#define BUF_DEMUX_BLOCK		0x05000000

typedef struct buf_element_s buf_element_t;
struct buf_element_s {
  buf_element_t        *next;

  unsigned char        *mem;
  unsigned char        *content;   /* start of raw content in mem (without header etc) */

  int32_t               size ;     /* size of _content_                                     */
  int32_t               max_size;  /* size of pre-allocated memory pointed to by "mem"      */ 
  uint32_t              type;
  int64_t               pts;       /* presentation time stamp, used for a/v sync            */
  int64_t               disc_off;  /* discontinuity offset                                  */
  off_t                 input_pos; /* remember where this buf came from in the input source */
  off_t                 input_length; /* remember the length of the input source */
  int                   input_time;/* time offset in seconds from beginning of stream       */

  uint32_t              decoder_flags; /* stuff like keyframe, is_header ... see below      */

  uint32_t              decoder_info[4]; /* additional decoder flags and other dec-spec. stuff */

  void (*free_buffer) (buf_element_t *buf);

  void                 *source;   /* pointer to source of this buffer for */
                                  /* free_buffer                          */

} ;

#define BUF_FLAG_KEYFRAME    0x0001
#define BUF_FLAG_FRAME_START 0x0002
#define BUF_FLAG_FRAME_END   0x0004
#define BUF_FLAG_HEADER      0x0008
#define BUF_FLAG_PREVIEW     0x0010
#define BUF_FLAG_END_USER    0x0020
#define BUF_FLAG_END_STREAM  0x0040
#define BUF_FLAG_FRAMERATE   0x0080
#define BUF_FLAG_SEEK        0x0100
#define BUF_FLAG_SPECIAL     0x0200
#define BUF_FLAG_NO_VIDEO    0x0400

/* these are the types of special buffers */
/* 
 * In a BUF_SPECIAL_PALETTE buffer:
 * decoder_info[1] = BUF_SPECIAL_PALETTE
 * decoder_info[2] = number of entries in palette table
 * decoder_info[3] = pointer to palette table
 * This buffer type is used to provide a file- and decoder-independent
 * facility to transport RGB color palettes from demuxers to decoders.
 * A palette table is an array of palette_entry_t structures. A decoder
 * should not count on this array to exist for the duration of the
 * program's execution and should copy, manipulate, and store the palette
 * data privately if it needs the palette information.
 */
#define BUF_SPECIAL_PALETTE  1

/*
 * In a BUF_SPECIAL_IDCIN_HUFFMAN_TABLE buffer:
 * decoder_info[1] = BUF_SPECIAL_IDCIN_HUFFMAN_TABLE
 * decoder_info[2] = pointer to a 65536-element byte array containing the
 *  Huffman tables from an Id CIN file
 * This buffer is used to transport the Huffman tables from an Id CIN
 * file to the Id CIN decoder. A decoder should not count on the byte array
 * to exist for the duration of the program's execution and should copy the
 * data into its own private structures.
 */
#define BUF_SPECIAL_IDCIN_HUFFMAN_TABLE  2

/*
 * In a BUF_SPECIAL_ASPECT buffer:
 * decoder_info[1] = BUF_SPECIAL_ASPECT
 * decoder_info[2] = aspect ratio code
 * decoder_info[3] = stream scale prohibitions
 * This buffer is used to force mpeg decoders to use a certain aspect.
 * Currently xine-dvdnav uses this, because it has more accurate information
 * about the aspect from the dvd ifo-data.
 * The stream scale prohibitions are also delivered, with bit 0 meaning
 * "deny letterboxing" and bit 1 meaning "deny pan&scan"
 */
#define BUF_SPECIAL_ASPECT  3

/*
 * In a BUF_SPECIAL_DECODER_CONFIG buffer:
 * decoder_info[1] = BUF_SPECIAL_DECODER_CONFIG
 * decoder_info[2] = data size
 * decoder_info[3] = pointer to data
 * This buffer is used to pass config information from  .mp4 files 
 * (atom esds) to decoders. both mpeg4 and aac streams use that.
 */
#define BUF_SPECIAL_DECODER_CONFIG  4

/*
 * In a BUF_SPECIAL_SAMPLE_SIZE_TABLE buffer:
 * decoder_info[1] = BUF_SPECIAL_SAMPLE_SIZE_TABLE
 * decoder_info[2] = 
 * decoder_info[3] = pointer to table with sample sizes 
 *                   (within current frame)
 * libfaad needs to decode data on sample-sized chunks. 
 * unfortunately original sample sizes are not know at decoder stage. 
 * this buffer is used to pass such information.
 */
#define BUF_SPECIAL_SAMPLE_SIZE_TABLE 5

/*
 * In a BUF_SPECIAL_LPCM_CONFIG buffer:
 * decoder_info[1] = BUF_SPECIAL_LPCM_CONFIG
 * decoder_info[2] = config data
 * lpcm data encoded into mpeg2 streams have a format configuration
 * byte in every frame. this is used to detect the sample rate,
 * number of bits and channels.
 */
#define BUF_SPECIAL_LPCM_CONFIG 6

/*
 * In a BUF_SPECIAL_VQA_VECTOR_SIZE:
 * decoder_info[1] = BUF_SPECIAL_VQA_VECTOR_SIZE
 * decoder_info[2] = vector width
 * decoder_info[3] = vector height
 * This buffer is used to transport the vector width and height dimensions
 * from the VQA demuxer to the VQA video decoder.
 */
#define BUF_SPECIAL_VQA_VECTOR_SIZE 7


typedef struct palette_entry_s palette_entry_t;
struct palette_entry_s
{
  unsigned char r, g, b;
} ;

typedef struct fifo_buffer_s fifo_buffer_t;
struct fifo_buffer_s
{
  buf_element_t  *first, *last;
  int             fifo_size;

  pthread_mutex_t mutex;
  pthread_cond_t  not_empty;

  /*
   * functions to access this fifo:
   */

  void (*put) (fifo_buffer_t *fifo, buf_element_t *buf);
  
  buf_element_t *(*get) (fifo_buffer_t *fifo);

  void (*clear) (fifo_buffer_t *fifo) ;

  int (*size) (fifo_buffer_t *fifo);

  void (*dispose) (fifo_buffer_t *fifo);

  /* 
   * alloc buffer for this fifo from global buf pool 
   * you don't have to use this function to allocate a buffer,
   * an input plugin can decide to implement it's own
   * buffer allocation functions
   */

  buf_element_t *(*buffer_pool_alloc) (fifo_buffer_t *this);

  /*
   * private variables for buffer pool management
   */

  buf_element_t   *buffer_pool_top;    /* a stack actually */
  pthread_mutex_t  buffer_pool_mutex;
  pthread_cond_t   buffer_pool_cond_not_empty;
  int              buffer_pool_num_free;
  int		   buffer_pool_capacity;
  int		   buffer_pool_buf_size;
  void            *buffer_pool_base; /*used to free mem chunk */
} ;

/*
 * allocate and initialize new (empty) fifo buffer,
 * init buffer pool for it:
 * allocate num_buffers of buf_size bytes each 
 */

fifo_buffer_t *fifo_buffer_new (int num_buffers, uint32_t buf_size);


/* return BUF_VIDEO_xxx given the fourcc
 * fourcc_int must be read in machine endianness
 * example: fourcc_int = *(uint32_t *)fourcc_char;
 */
uint32_t fourcc_to_buf_video( uint32_t fourcc_int );

/* return codec name given BUF_VIDEO_xxx */
char * buf_video_name( uint32_t buf_type );

/* return BUF_VIDEO_xxx given the formattag */
uint32_t formattag_to_buf_audio( uint32_t formattag );

/* return codec name given BUF_VIDEO_xxx */
char * buf_audio_name( uint32_t buf_type );


/* this is xine version of BITMAPINFOHEADER 
 * - should be safe to compile on 64bits machines 
 * - will always use machine endian format, so demuxers reading
 *   stuff from win32 formats must use the function below.
 */
typedef struct __attribute__((__packed__)) {
    int32_t        biSize;
    int32_t        biWidth;
    int32_t        biHeight;
    int16_t        biPlanes;
    int16_t        biBitCount;
    uint32_t       biCompression;
    int32_t        biSizeImage;
    int32_t        biXPelsPerMeter;
    int32_t        biYPelsPerMeter;
    int32_t        biClrUsed;
    int32_t        biClrImportant;
} xine_bmiheader;

/* convert xine_bmiheader struct from little endian */
void xine_bmiheader_le2me( xine_bmiheader *bih );

/* this is xine version of WAVEFORMATEX 
 * (the same comments from xine_bmiheader)
 */
typedef struct __attribute__((__packed__)) {
  int16_t   wFormatTag;
  int16_t   nChannels;
  int32_t   nSamplesPerSec;
  int32_t   nAvgBytesPerSec;
  int16_t   nBlockAlign;
  int16_t   wBitsPerSample;
  int16_t   cbSize;
} xine_waveformatex;

/* convert xine_waveformatex struct from little endian */
void xine_waveformatex_le2me( xine_waveformatex *wavex );

#ifdef __cplusplus
}
#endif

#endif
