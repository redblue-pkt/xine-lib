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
 * $Id: xine_decoder.c,v 1.5 2001/08/21 19:39:50 jcdutton Exp $
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

  uint32_t         pts;

  mpgaudio_t      *mpg;

  ao_instance_t  *audio_out;
  int              output_sampling_rate;
  int              output_open;

} mpgdec_decoder_t;

int mpgdec_can_handle (audio_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_AUDIO_MPEG) ;
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

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  mpgdec_decoder_t *this ;

  if (iface_version != 2) {
    printf( "libmpg123: plugin doesn't support plugin API version %d.\n"
	    "libmpg123: this means there's a version mismatch between xine and this "
	    "libmpg123: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);

    return NULL;
  }

  this = (mpgdec_decoder_t *) malloc (sizeof (mpgdec_decoder_t));

  this->audio_decoder.interface_version   = 2;
  this->audio_decoder.can_handle          = mpgdec_can_handle;
  this->audio_decoder.init                = mpgdec_init;
  this->audio_decoder.decode_data         = mpgdec_decode_data;
  this->audio_decoder.close               = mpgdec_close;
  this->audio_decoder.get_identifier      = mpgdec_get_id;
  this->audio_decoder.priority            = 1;
  
  return (audio_decoder_t *) this;
}

