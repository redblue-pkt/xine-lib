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
 * $Id: xine_decoder.c,v 1.1 2003/06/09 23:08:11 jcdutton Exp $
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

#include "xine_internal.h"
#include "video_out.h"
#include "buffer.h"

/*
#define LOG
*/

typedef struct {
  video_decoder_class_t   decoder_class;
} mpeg2_class_t;


typedef struct mpeg2dec_decoder_s {
  video_decoder_t  video_decoder;
  void            *mpeg2dec;
  mpeg2_class_t   *class;
  xine_stream_t   *stream;
} mpeg2dec_decoder_t;

static void mpeg2dec_decode_data (video_decoder_t *this_gen, buf_element_t *buf) {
  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libmpeg2: decode_data, flags=0x%08x ...\n", buf->decoder_flags);
#endif


#ifdef LOG
  printf ("libmpeg2: decode_data...done\n");
#endif
}

static void mpeg2dec_flush (video_decoder_t *this_gen) {
  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libmpeg2: flush\n");
#endif

//  mpeg2_flush (&this->mpeg2);
}

static void mpeg2dec_reset (video_decoder_t *this_gen) {
  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

//  mpeg2_reset (&this->mpeg2);
}

static void mpeg2dec_discontinuity (video_decoder_t *this_gen) {
  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

//  mpeg2_discontinuity (&this->mpeg2);
}

static void mpeg2dec_dispose (video_decoder_t *this_gen) {

  mpeg2dec_decoder_t *this = (mpeg2dec_decoder_t *) this_gen;

#ifdef LOG
  printf ("libmpeg2: close\n");
#endif

  mpeg2_close (this->mpeg2dec);

  this->stream->video_out->close(this->stream->video_out, this->stream);

  free (this);
}

static video_decoder_t *open_plugin (video_decoder_class_t *class_gen, xine_stream_t *stream) {
  mpeg2dec_decoder_t *this ;

  this = (mpeg2dec_decoder_t *) malloc (sizeof (mpeg2dec_decoder_t));
  memset(this, 0, sizeof (mpeg2dec_decoder_t));

  this->video_decoder.decode_data         = mpeg2dec_decode_data;
  this->video_decoder.flush               = mpeg2dec_flush;
  this->video_decoder.reset               = mpeg2dec_reset;
  this->video_decoder.discontinuity       = mpeg2dec_discontinuity;
  this->video_decoder.dispose             = mpeg2dec_dispose;
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
