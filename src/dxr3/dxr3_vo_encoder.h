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
 * $Id: dxr3_vo_encoder.h,v 1.9 2001/12/02 07:10:02 hrm Exp $
 *
 */

/************************************************************************* 
 * Dxr3 Encoding Plugin Configuration Options 		 		 *
 *************************************************************************/

/* 1: enable to buffer the mpeg1 stream; 
 * 0: write to mpeg device immediately;
 * with 1 sync is better, but playback still not smooth (but see below) 
 * (not supported with mp1e; will disable automatically) */
#define USE_MPEG_BUFFER 1

/* 1: write 6 to MV_COMMAND register. This seems to fix playback problems!
 * 0: don't write to register */
#define USE_MAGIC_REGISTER 1

/* select one mpeg encoder out of the three below */
#define USE_LIBFAME 1

#define USE_FFMPEG 0

#define USE_MP1E 0

/************************************************************************* 
 * Dxr3 Encoding private stuff below - You shouldn't need to change this *
 *************************************************************************/

/* buffer size for encoded mpeg1 stream; will hold one intra frame 
 * at 640x480 typical sizes are <50 kB. 512 kB should be plenty */
#define DEFAULT_BUFFER_SIZE 512*1024

#if (USE_LIBFAME && USE_FFMPEG) || (USE_LIBFAME && USE_MP1E) || (USE_LIBFFMPEG && USE_MP1E)
#  error "Configure only one mpeg encoder"
#endif

#if USE_LIBFAME
	#include <fame.h>

	/* some global stuff for libfame, could use some cleanup :-) */
	fame_parameters_t fp = FAME_PARAMETERS_INITIALIZER;
	fame_yuv_t yuv;
	fame_context_t *fc; /* needed for fame calls */
#endif

#if USE_FFMPEG
	/* use libffmpeg */
	#include <ffmpeg/avcodec.h>
	AVCodecContext *avc;
	AVPicture avp;
	AVCodec *avcodec;
#endif

#if USE_MP1E
	#include <stdio.h>
	FILE* mp1e;
	char* mp1e_command; /* from config, with %s for runtime stuff */
	char mp1e_cmd[256]; /* complete, with %s filled in */
#  undef USE_MPEG_BUFFER
#  define USE_MPEG_BUFFER 0 
#endif

/* mpeg1 buffer, used by both encoders */
unsigned char *buffer;

