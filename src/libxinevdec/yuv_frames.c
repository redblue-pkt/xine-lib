/* 
 * Copyright (C) 2003 the xine project
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
 * $Id: yuv_frames.c,v 1.4 2003/06/11 23:08:55 miguelfreitas Exp $
 *
 * dummy video decoder for uncompressed video frames as delivered by v4l
 */


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"

/*
#define LOG
*/

typedef struct {
  video_decoder_class_t   decoder_class;
} mpeg2_class_t;


typedef struct yuv_frames_decoder_s {
  video_decoder_t  video_decoder;
  mpeg2_class_t   *class;
  xine_stream_t   *stream;
} yuv_frames_decoder_t;

static void yuv_frames_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  yuv_frames_decoder_t *this = (yuv_frames_decoder_t *) this_gen;
  int                   frame_size;
  vo_frame_t           *img;

#ifdef LOG
  printf ("yuv_frames: decode_data, flags=0x%08x ...\n", buf->decoder_flags);
#endif

  img = this->stream->video_out->get_frame (this->stream->video_out,
					    buf->decoder_info[0],
					    buf->decoder_info[1],
					    ASPECT_FULL, 
					    XINE_IMGFMT_YV12,
					    VO_BOTH_FIELDS | VO_INTERLACED_FLAG);

  img->duration = 3600;
  img->pts      = buf->pts;

  frame_size = buf->decoder_info[0] * buf->decoder_info[1];

  xine_fast_memcpy (img->base[0], buf->content, frame_size);
  xine_fast_memcpy (img->base[1], buf->content+frame_size, frame_size/4);
  xine_fast_memcpy (img->base[2], buf->content+frame_size*5/4, frame_size/4);

  img->draw (img, this->stream);
  img->free (img);

#ifdef LOG
  printf ("yuv_frames: decode_data...done\n");
#endif
}

static void yuv_frames_flush (video_decoder_t *this_gen) {
  /* yuv_frames_decoder_t *this = (yuv_frames_decoder_t *) this_gen; */

#ifdef LOG
  printf ("yuv_frames: flush\n");
#endif
}

static void yuv_frames_reset (video_decoder_t *this_gen) {
  /* yuv_frames_decoder_t *this = (yuv_frames_decoder_t *) this_gen; */
}

static void yuv_frames_discontinuity (video_decoder_t *this_gen) {
  /* yuv_frames_decoder_t *this = (yuv_frames_decoder_t *) this_gen; */
}

static void yuv_frames_dispose (video_decoder_t *this_gen) {

  yuv_frames_decoder_t *this = (yuv_frames_decoder_t *) this_gen;

#ifdef LOG
  printf ("yuv_frames: close\n");
#endif

  this->stream->video_out->close(this->stream->video_out, this->stream);

  free (this);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {
  yuv_frames_decoder_t *this ;

  this = (yuv_frames_decoder_t *) malloc (sizeof (yuv_frames_decoder_t));
  memset(this, 0, sizeof (yuv_frames_decoder_t));

  this->video_decoder.decode_data         = yuv_frames_decode_data;
  this->video_decoder.flush               = yuv_frames_flush;
  this->video_decoder.reset               = yuv_frames_reset;
  this->video_decoder.discontinuity       = yuv_frames_discontinuity;
  this->video_decoder.dispose             = yuv_frames_dispose;
  this->stream                            = stream;
  this->class                             = (mpeg2_class_t *) class_gen;

  stream->video_out->open(stream->video_out, stream);

  return &this->video_decoder;
}

/*
 * mpeg2 plugin class
 */

static char *get_identifier (video_decoder_class_t *this) {
  return "yuv_frames";
}

static char *get_description (video_decoder_class_t *this) {
  return "mpeg2 based video decoder plugin";
}

static void dispose_class (video_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  mpeg2_class_t *this;

  this = (mpeg2_class_t *) malloc (sizeof (mpeg2_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}
/*
 * exported plugin catalog entry
 */

static uint32_t supported_types[] = { BUF_VIDEO_YUV_FRAMES, 0 };

static decoder_info_t dec_info_yuv_frames = {
  supported_types,     /* supported types */
  6                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 14, "yuv_frames", XINE_VERSION_CODE, &dec_info_yuv_frames, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
