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
 * $Id: xine_decoder.c,v 1.2 2003/06/10 16:30:15 jcdutton Exp $
 *
 * stuff needed to turn libmpeg2 into a xine decoder plugin
 */


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <mpeg2dec/mpeg2.h>
#include <assert.h>

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"

/*
#define LOG
*/

typedef struct {
  video_decoder_class_t   decoder_class;
} mpeg2_class_t;


typedef struct mpeg2_video_decoder_s {
  video_decoder_t  video_decoder;
  mpeg2dec_t      *mpeg2dec;
  mpeg2_class_t   *class;
  xine_stream_t   *stream;
} mpeg2_video_decoder_t;

static void mpeg2_video_decode_data (video_decoder_t *this_gen, buf_element_t *buf_element) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;
  uint8_t * current = buf_element->content;
  uint8_t * end = buf_element->content + buf_element->size;
  const mpeg2_info_t * info;
  mpeg2_state_t state;
  vo_frame_t * img;
//  vo_setup_result_t setup_result;
#ifdef LOG
  printf ("libmpeg2: decode_data, flags=0x%08x ...\n", buf_element->decoder_flags);
#endif

  mpeg2_buffer (this->mpeg2dec, current, end);

  info = mpeg2_info (this->mpeg2dec);
  while ((state = mpeg2_parse (this->mpeg2dec)) != STATE_BUFFER) {
    switch (state) {
      case STATE_SEQUENCE:
        /* might set nb fbuf, convert format, stride */
        /* might set fbufs */
        mpeg2_custom_fbuf (this->mpeg2dec, 1);  /* <- Force libmpeg2 to use xine frame buffers. */
        img = this->stream->video_out->get_frame (this->stream->video_out,
                                              info->sequence->picture_width,
                                              info->sequence->picture_height,
                                              //picture->aspect_ratio_information,
                                              1,
                                              XINE_IMGFMT_YV12,
                                              //picture->picture_structure);
                                              0);
        mpeg2_set_buf (this->mpeg2dec, img->base, img);

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                              info->sequence->picture_width,
                                              info->sequence->picture_height,
                                              //picture->aspect_ratio_information,
                                              1,
                                              XINE_IMGFMT_YV12,
                                              //picture->picture_structure);
                                              0);
        mpeg2_set_buf (this->mpeg2dec, img->base, img);
        break;
      case STATE_PICTURE:
        /* might skip */
        /* might set fbuf */

        img = this->stream->video_out->get_frame (this->stream->video_out,
                                              info->sequence->picture_width,
                                              info->sequence->picture_height,
                                              //picture->aspect_ratio_information,
                                              1,
                                              XINE_IMGFMT_YV12,
                                              //picture->picture_structure);
                                              0);
        mpeg2_set_buf (this->mpeg2dec, img->base, img);
        break;
      case STATE_SLICE:
      case STATE_END:
        /* draw current picture */
        /* might free frame buffer */
        if (info->display_fbuf) {
          img = (vo_frame_t *) info->display_fbuf->id; 
          img->draw (img, this->stream);
        }
        if (info->discard_fbuf) {
          img = (vo_frame_t *) info->discard_fbuf->id; 
          img->free(img);
        }
        break;
      default:
        break;
   }

 }


#ifdef LOG
  printf ("libmpeg2: decode_data...done\n");
#endif
}

static void mpeg2_video_flush (video_decoder_t *this_gen) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

#ifdef LOG
  printf ("libmpeg2: flush\n");
#endif

//  mpeg2_flush (&this->mpeg2);
}

static void mpeg2_video_reset (video_decoder_t *this_gen) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

//  mpeg2_reset (&this->mpeg2dec);
}

static void mpeg2_video_discontinuity (video_decoder_t *this_gen) {
  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

//  mpeg2_discontinuity (&this->mpeg2dec);
}

static void mpeg2_video_dispose (video_decoder_t *this_gen) {

  mpeg2_video_decoder_t *this = (mpeg2_video_decoder_t *) this_gen;

#ifdef LOG
  printf ("libmpeg2: close\n");
#endif

  mpeg2_close (this->mpeg2dec);

  this->stream->video_out->close(this->stream->video_out, this->stream);

  free (this);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {
  mpeg2_video_decoder_t *this ;

  this = (mpeg2_video_decoder_t *) malloc (sizeof (mpeg2_video_decoder_t));
  memset(this, 0, sizeof (mpeg2_video_decoder_t));

  this->video_decoder.decode_data         = mpeg2_video_decode_data;
  this->video_decoder.flush               = mpeg2_video_flush;
  this->video_decoder.reset               = mpeg2_video_reset;
  this->video_decoder.discontinuity       = mpeg2_video_discontinuity;
  this->video_decoder.dispose             = mpeg2_video_dispose;
  this->stream                            = stream;
  this->class                             = (mpeg2_class_t *) class_gen;

  this->mpeg2dec = mpeg2_init ();
  stream->video_out->open(stream->video_out, stream);
//  this->mpeg2.force_aspect = 0;

  return &this->video_decoder;
}

/*
 * mpeg2 plugin class
 */

static char *get_identifier (video_decoder_class_t *this) {
  return "mpeg2dec";
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

static uint32_t supported_types[] = { BUF_VIDEO_MPEG, 0 };

static decoder_info_t dec_info_mpeg2 = {
  supported_types,     /* supported types */
  6                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_VIDEO_DECODER, 14, "mpeg2", XINE_VERSION_CODE, &dec_info_mpeg2, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
