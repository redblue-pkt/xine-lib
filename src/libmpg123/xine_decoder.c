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
 * $Id: xine_decoder.c,v 1.10 2002/09/05 22:18:57 mroi Exp $
 *
 * stuff needed to turn libmpg123 into a xine decoder plugin
 */

/*
 * FIXME: libmpg123 uses global variables (that are written to)
 */


#include <stdlib.h>

#include "audio_out.h"
#include "mpg123.h"
#include "mpglib.h"
#include "buffer.h"
#include "xine_internal.h"

#define FRAME_SIZE 4096

typedef struct mpgdec_decoder_s {
  audio_decoder_t  audio_decoder;

  int64_t         pts;

  mpgaudio_t      *mpg;

  ao_instance_t  *audio_out;
  int              output_sampling_rate;
  int              output_open;

} mpgdec_decoder_t;

void mpgdec_reset (audio_decoder_t *this_gen) {

  mpgdec_decoder_t *this = (mpgdec_decoder_t *) this_gen; 

  mpg_audio_reset (this->mpg);
}

void mpgdec_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  mpgdec_decoder_t *this = (mpgdec_decoder_t *) this_gen;

  this->audio_out = audio_out;
  this->mpg       = mpg_audio_init (audio_out);
}

void mpgdec_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  mpgdec_decoder_t *this = (mpgdec_decoder_t *) this_gen;

  /*
    printf ("libmpg123: decode data\n");
    fflush (stdout);
  */
  if (buf->decoder_info[0] >0) {
    /*
      printf ("libmpg123: decode data - doing it\n");
      fflush (stdout);
      */
    mpg_audio_decode_data (this->mpg, buf->content, buf->content + buf->size,
			   buf->PTS);
  }
}

void mpgdec_close (audio_decoder_t *this_gen) {

  mpgdec_decoder_t *this = (mpgdec_decoder_t *) this_gen; 

  mpg_audio_close (this->mpg);

  if (this->output_open) 
    this->audio_out->close (this->audio_out);
}

static char *mpgdec_get_id(void) {
  return "mpgdec";
}

static void *init_audio_decoder_plugin (xine_t *xine, void *data) {

  mpgdec_decoder_t *this ;

  this = (mpgdec_decoder_t *) malloc (sizeof (mpgdec_decoder_t));

  this->audio_decoder.init                = mpgdec_init;
  this->audio_decoder.reset               = mpgdec_reset;
  this->audio_decoder.decode_data         = mpgdec_decode_data;
  this->audio_decoder.close               = mpgdec_close;
  this->audio_decoder.get_identifier      = mpgdec_get_id;
  
  return (audio_decoder_t *) this;
}

static uint32_t audio_types[] = { BUF_AUDIO_MPEG, 0 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  1                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 9, "mpgdec", XINE_VERSION_CODE, &dec_info_audio, init_audio_decoder_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
