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
 * XviD video decoder plugin (libxvidcore wrapper) for xine
 *
 * by Tomas Kovar <tomask@mac.com>
 * with ideas from the ffmpeg xine plugin.
 *
 * Requires the xvidcore library. Find it at http://www.xvid.org.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <pthread.h>
#include <xvid.h>

#include "bswap.h"
#include "xine_internal.h"
#include "buffer.h"
#include "xine-utils/xineutils.h"

#define	VIDEOBUFSIZE	128 * 1024

/*
#define LOG
*/


typedef struct xvid_decoder_s {
    video_decoder_t	video_decoder;
    
    vo_instance_t	*video_out;
    int			decoder_running;
    int			skip_frames;

    unsigned char	*buffer;
    int			buffer_size;
    int			frame_size;

    int			frame_width;
    int			frame_height;

    /* frame_duration a.k.a. video_step. It is one second metronom */
    /* ticks (90,000) divided by fps (provided by demuxer from system */
    /* stream), i.e. for how many ticks the frame will be displayed */
    int			frame_duration;
    
    void		*xvid_handle;
} xvid_decoder_t;

static int xvid_can_handle (video_decoder_t *this_gen, int buf_type) {
    buf_type &= (BUF_MAJOR_MASK|BUF_DECODER_MASK);

    /* FIXME: what is it exactly that xvid can handle? :> */

    return ((buf_type == BUF_VIDEO_XVID) || (buf_type == BUF_VIDEO_DIVX5));
}

static void xvid_init_plugin (video_decoder_t *this_gen, vo_instance_t *video_out) {
    xvid_decoder_t *this = (xvid_decoder_t *) this_gen;
    
    this->video_out = video_out;
    this->decoder_running = 0;
    this->buffer = NULL;
    this->xvid_handle = NULL;
}

static void xvid_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
    int xerr;
    xvid_decoder_t *this = (xvid_decoder_t *) this_gen;

#ifdef LOG
    printf ("xvid: processing packet type = %08x, buf: %08x, buf->decoder_flags=%08x\n",
	    buf->type, buf, buf->decoder_flags);
#endif
    
    if (buf->decoder_flags & BUF_FLAG_HEADER) {
	xine_bmiheader *bih;
	XVID_DEC_PARAM xparam;
    
	/* initialize data describing video stream */
	bih = (xine_bmiheader *) buf->content;
	this->frame_duration = buf->decoder_info[1];
	this->frame_width = bih->biWidth;
	this->frame_height = bih->biHeight;
	
	/* initialize decoder */
	if (this->xvid_handle) {
	    xvid_decore (this->xvid_handle, XVID_DEC_DESTROY, NULL, NULL);
	    this->xvid_handle = NULL;
	}
	
	xparam.width = this->frame_width;
	xparam.height = this->frame_height;
	xparam.handle = NULL;
	if ((xerr = xvid_decore (NULL, XVID_DEC_CREATE, &xparam, NULL)) == XVID_ERR_OK) {
	    this->xvid_handle = xparam.handle;
	
	    /* initialize video out */
	    this->video_out->open (this->video_out);
	
	    /* initialize buffer */
	    if (this->buffer) {
		free (this->buffer);
	    }
	    
	    this->buffer = malloc (VIDEOBUFSIZE);
	    this->buffer_size = VIDEOBUFSIZE;
	
	    this->decoder_running = 1;
	    this->skip_frames = 0;
	} else {
	    printf ("xvid: cannot initialize xvid decoder, error = %d.\n", xerr);
	    return;
	}

    } else {
	if (this->decoder_running) {
	
	    /* collect all video stream fragments until "end of frame" mark */
	    if (this->frame_size + buf->size > this->buffer_size) {
		this->buffer_size = this->frame_size + 2 * buf->size;
		printf ("xvid: increasing source buffer to %d to avoid overflow.\n", this->buffer_size);
		this->buffer = realloc (this->buffer, this->buffer_size);
	    }
	    
	    xine_fast_memcpy (&this->buffer[this->frame_size], buf->content, buf->size);
	    this->frame_size += buf->size;
	    
	    /* after collecting complete frame, decode it and schedule to display it */
	    if (buf->decoder_flags & BUF_FLAG_FRAME_END) {
		vo_frame_t *image;
		XVID_DEC_FRAME xframe;
		
		image = this->video_out->get_frame (this->video_out,
						    this->frame_width, this->frame_height,
						    XINE_ASPECT_RATIO_DONT_TOUCH,
						    IMGFMT_YV12, VO_BOTH_FIELDS);
		image->pts = buf->pts;
		image->duration = this->frame_duration;
		image->bad_frame = 0;
		
		/* decode frame */
		xframe.bitstream = this->buffer;
		xframe.length = this->frame_size;
		/* FIXME: This assumes, that YV12 planes are correctly laid off */
		xframe.image = image->base[0];
		xframe.stride = this->frame_width;
		xframe.colorspace = XVID_CSP_YV12;

		if ((xerr = xvid_decore (this->xvid_handle, XVID_DEC_DECODE, &xframe, NULL)) != XVID_ERR_OK
		  || this->skip_frames) {
		    if (this->skip_frames) {
			printf ("xvid: skipping frame.\n");
		    } else {
			printf ("xvid: error decompressing frame, error code = %d.\n", xerr);
		    }
		    image->bad_frame = 1;
		} 
		
		/* add frame to display queue */
		this->skip_frames = image->draw (image);
		if (this->skip_frames < 0) {
		    this->skip_frames = 0;
		}

		image->free (image);
		this->frame_size = 0;
	    }
	}
    }
}

static void xvid_flush (video_decoder_t *this_gen) {
}

static void xvid_close_plugin (video_decoder_t *this_gen) {
    xvid_decoder_t *this = (xvid_decoder_t *) this_gen;
    
    if (this->decoder_running) {
	xvid_decore (this->xvid_handle, XVID_DEC_DESTROY, NULL, NULL);
	this->xvid_handle = NULL;
	this->video_out->close (this->video_out);
	this->decoder_running = 0;
    }
    if (this->buffer) {
	free (this->buffer);
	this->buffer = NULL;
    }
}

static char *xvid_get_id(void) {
    return "XviD video decoder";
}

static void xvid_dispose (video_decoder_t *this_gen) {
  free (this_gen);
}

video_decoder_t *init_video_decoder_plugin (int iface_version, xine_t *xine) {
    xvid_decoder_t *this;
    XVID_INIT_PARAM xinit;
    
    if (iface_version != 9) {
	printf (_("xvid: plugin doesn't support plugin API version %d.\n"
		  "xvid: this means there's a version mismatch between xine and this\n"
		  "xvid: decoder plugin. Installing current plugins should help.\n"),
		iface_version);
	return NULL;
    }
    
    xinit.cpu_flags = 0;
    xvid_init(NULL, 0, &xinit, NULL);
    if (xinit.api_version != API_VERSION) {
	printf (_("xvid: there is mismatch between API used by currently installed XviD\n"
		  "xvid: library (%d.%d) and library used to compile this plugin (%d.%d).\n"
		  "xvid: Compiling this plugin against current XviD library should help.\n"),
		xinit.api_version >> 16, xinit.api_version & 0xFFFF,
		API_VERSION >> 16, API_VERSION & 0xFFFF);
	return NULL;
    }

    this = (xvid_decoder_t *) malloc (sizeof (xvid_decoder_t));
    
    this->video_decoder.interface_version = iface_version;
    this->video_decoder.can_handle	  = xvid_can_handle;
    this->video_decoder.init		  = xvid_init_plugin;
    this->video_decoder.decode_data	  = xvid_decode_data;
    this->video_decoder.flush		  = xvid_flush;
    this->video_decoder.close		  = xvid_close_plugin;
    this->video_decoder.get_identifier	  = xvid_get_id;
    this->video_decoder.dispose		  = xvid_dispose;
    this->video_decoder.priority	  = 6;
    this->frame_size			  = 0;
    
    return (video_decoder_t *) this;
}
