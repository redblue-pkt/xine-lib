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
 * $Id: xine_decoder.c,v 1.1 2001/08/04 20:14:54 guenter Exp $
 *
 * stuff needed to turn libac3 into a xine decoder plugin
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h> /* ntohs */

#include "audio_out.h"
#include "buffer.h"
#include "xine_internal.h"


typedef struct lpcm_decoder_s {
  audio_decoder_t  audio_decoder;

  uint32_t         pts;
  uint32_t         last_pts;
  
  ao_functions_t  *audio_out;
  int              output_open;

} lpcm_decoder_t;

int lpcm_can_handle (audio_decoder_t *this_gen, int buf_type) {
  return ((buf_type & 0xFFFF0000) == BUF_AUDIO_LPCM) ;
}


void lpcm_init (audio_decoder_t *this_gen, ao_functions_t *audio_out) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;

  this->audio_out     = audio_out;
  this->output_open   = 0;
  this->pts           = 0;
  this->last_pts      = 0;
}


void lpcm_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen;
  int16_t *p=(int16_t *)buf->content;
  int i;
  
  if (buf->decoder_info[0] == 0)
    return;
 
  if (buf->PTS) 
    this->pts = buf->PTS; 
  
  if (!this->output_open) {      
    this->output_open = (this->audio_out->open (this->audio_out, 16, 
                                                48000,
                                                AO_CAP_MODE_STEREO));
  }

  if (!this->output_open) 
    return;

  for(i=0; i<buf->size/2; i++)
    p[i] = ntohs(p[i]);     

  this->audio_out->write_audio_data (this->audio_out,
                                     buf->content,
				     buf->size/4,
                                     this->pts);

  
}

void lpcm_close (audio_decoder_t *this_gen) {

  lpcm_decoder_t *this = (lpcm_decoder_t *) this_gen; 

  if (this->output_open) 
    this->audio_out->close (this->audio_out);

  this->output_open = 0;
}

static char *lpcm_get_id(void) {
  return "lpcm";
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, config_values_t *cfg) {

  lpcm_decoder_t *this ;

  if (iface_version != 2) {
    printf( "liblpcm: plugin doesn't support plugin API version %d.\n"
	    "liblpcm: this means there's a version mismatch between xine and this "
	    "liblpcm: decoder plugin.\nInstalling current plugins should help.\n",
	    iface_version);
    return NULL;
  }

  this = (lpcm_decoder_t *) malloc (sizeof (lpcm_decoder_t));

  this->audio_decoder.interface_version   = 2;
  this->audio_decoder.can_handle          = lpcm_can_handle;
  this->audio_decoder.init                = lpcm_init;
  this->audio_decoder.decode_data         = lpcm_decode_data;
  this->audio_decoder.close               = lpcm_close;
  this->audio_decoder.get_identifier      = lpcm_get_id;
  this->audio_decoder.priority            = 1;
    
  return (audio_decoder_t *) this;
}

