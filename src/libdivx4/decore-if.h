/* 
 * Copyright (C) 2001 the xine project
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
 * $Id: decore-if.h,v 1.1 2001/10/14 00:43:06 guenter Exp $ 
 *
 * This file documents the interface for the decore() function
 * in libdivxdecore. In case of problems, it is recommended you compare
 * the definitions in this file with the header file that came with your
 * divx4 library (see below).
 *
 * The divxdecore library can be found as part of the divx4linux package at
 * http://www.divx.com or http://avifile.sourceforge.net The package also
 * contains a text file documenting the decore/encore API.
 *
 * Alternatively, the open source OpenDivx project (http://www.projectmayo.com)
 * also provides a divxdecore library with the same interface. There is
 * a project called XviD at http://www.videocoding.de that extends the
 * opendivx project.
 *
 * THIS FILE AND THE XINE DECORE PLUGIN ARE INTENDED FOR THE BINARY DIVX4LINUX
 * PACKAGE. OPENDIVX DECORE LIBRARIES ARE CURRENTLY NOT SUPPORTED AND MAY OR
 * MAY NOT WORK. 
 * 
 * Harm van der Heijden <hrm@users.sourceforge.net>
 */

#ifdef __cplusplus
extern "C" {
#endif 

#ifndef _DECORE_IF_H_
#define _DECORE_IF_H_

/* decore commands (2nd parameter in decore()) */
#define DEC_OPT_MEMORY_REQS	0
#define DEC_OPT_INIT		1
#define DEC_OPT_RELEASE		2
#define DEC_OPT_SETPP		3 
#define DEC_OPT_SETOUT		4 
#define DEC_OPT_FRAME		5
#define DEC_OPT_FRAME_311	6
#define DEC_OPT_SETPP2		7

/* decore() return values. */
#define DEC_OK			0
#define DEC_MEMORY		1
#define DEC_BAD_FORMAT		2
#define DEC_EXIT		3

/* colour formats -- yuv */
#define DEC_YUY2		1
#define DEC_YUV2 		DEC_YUY2
#define DEC_UYVY		2
#define DEC_420			3
#define DEC_YV12		13 /* looks like an afterthought, eh? */

/* colour formats -- rgb 
   not yet used by xine, but perhaps in the future.
   (decore yuv->rgb conversion may be better than libsdl/Xshm) */
#define DEC_RGB32		4 
#define DEC_RGB24		5 
#define DEC_RGB555		6 
#define DEC_RGB565		7	

#define DEC_RGB32_INV		8
#define DEC_RGB24_INV		9
#define DEC_RGB555_INV 		10
#define DEC_RGB565_INV 		11

/* pseudo colour format; makes decore() return pointers to internal
   yuv buffers for manual conversion, see DEC_PICTURE */  
#define DEC_USER		12

/* memory requirement structure; the officical codec spec calls for
   a DEC_OPT_MEMORY_REQ call prior to DEC_OPT_INIT, but it does not
   actually seem to be used */
typedef struct
{
	unsigned long mp4_edged_ref_buffers_size;
	unsigned long mp4_edged_for_buffers_size;
	unsigned long mp4_edged_back_buffers_size;
	unsigned long mp4_display_buffers_size;
	unsigned long mp4_state_size;
	unsigned long mp4_tables_size;
	unsigned long mp4_stream_size;
	unsigned long mp4_reference_size;
} DEC_MEM_REQS;

/* included in DEC_PARAM for init, not really used otherwise. */
typedef struct 
{
	void * mp4_edged_ref_buffers;  
	void * mp4_edged_for_buffers; 
	void * mp4_edged_back_buffers;
	void * mp4_display_buffers;
	void * mp4_state;
	void * mp4_tables;
	void * mp4_stream;
	void * mp4_reference;
} DEC_BUFFERS;

/* struct for DEC_OPT_INIT */
typedef struct 
{
	int x_dim; /* frame width */
	int y_dim; /* frame height */
	int output_format; /* refers to colour formats defined above */	
	int time_incr; /* mystery parameter, use 15 */
	DEC_BUFFERS buffers; /* memcpy 0's in this struct before init */
} DEC_PARAM;

/* struct for DEC_OPT_DECODE */
typedef struct
{
	void *bmp; /* pointer to rgb, yuv or DEC_PICTURE buffer */
	void *bitstream; /* input bit stream */
	long length; /* length of input */
	int render_flag; /* 1 to actually render the frame */
	unsigned int stride; /* bmp stride; should be width */
} DEC_FRAME;

/* decode frame information. not yet used by xine */
typedef struct
{
	int intra;
	int *quant_store;
	int quant_stride;
} DEC_FRAME_INFO;

/* structure for DEC_OPT_SETPP, for setting the postprocessing level */
typedef struct
{
	int postproc_level; /* between 0-100, actually used 0-60 */ 
} DEC_SET;

/* structure for DEC_USER output format, should be used instead of bmp ptr */
typedef struct
{
	void *y;
	void *u;
	void *v;
	int stride_y;
	int stride_uv;
} DEC_PICTURE;

/* Finally, decore() itself. Refer to the official codec interface text for
   a complete description. */
int decore( unsigned long handle, unsigned long dec_opt, void* param1, void* param2);
/* handle: unique caller handle. xine uses the video_decoder_t ptr. 
   dec_opt: decore command id
   param1: depends on command. Usually ptr to struct with input values.
   param2: depends on command. Usually ptr to struct with output values. */

/* typedef for pointer to decore function */
typedef int (*decoreFunc)(unsigned long, unsigned long, void*, void*); 

#endif // _DECORE_IF_H_
#ifdef __cplusplus
}
#endif
 
