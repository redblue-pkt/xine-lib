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
 * $Id: xine_decoder.c,v 1.22 2002/11/20 11:57:45 mroi Exp $
 *
 * (ogg/)vorbis audio decoder plugin (libvorbis wrapper) for xine
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "xine_internal.h"
#include "audio_out.h"
#include "buffer.h"

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#define MAX_NUM_SAMPLES 4096

/*
#define LOG
*/

typedef struct {
  audio_decoder_class_t   decoder_class;
} vorbis_class_t;

typedef struct vorbis_decoder_s {
  audio_decoder_t   audio_decoder;

  int64_t           pts;

  int               output_sampling_rate;
  int               output_open;
  int               output_mode;

  /* vorbis stuff */
  vorbis_info       vi; 
  vorbis_comment    vc;
  vorbis_dsp_state  vd;
  vorbis_block      vb;

  int16_t           convbuffer[MAX_NUM_SAMPLES];
  int               convsize;

  int               header_count;

  xine_stream_t    *stream;

} vorbis_decoder_t;


static void vorbis_reset (audio_decoder_t *this_gen) {

  vorbis_decoder_t *this = (vorbis_decoder_t *) this_gen;

  vorbis_synthesis_init(&this->vd,&this->vi); 
  vorbis_block_init(&this->vd,&this->vb);     
}

static void vorbis_discontinuity (audio_decoder_t *this_gen) {
}

/* Known vorbis comment keys from ogg123 sources*/
static struct {
  char *key;         /* includes the '=' for programming convenience */
  int   xine_metainfo_index;
} vorbis_comment_keys[] = {
  {"ARTIST=", XINE_META_INFO_ARTIST},
  {"ALBUM=", XINE_META_INFO_ALBUM},
  {"TITLE=", XINE_META_INFO_TITLE},
  {"GENRE=", XINE_META_INFO_GENRE},
  {"DESCRIPTION=", XINE_META_INFO_COMMENT},
  {"DATE=", XINE_META_INFO_YEAR},
  {NULL, 0}
};

static void get_metadata (vorbis_decoder_t *this) {

  char **ptr=this->vc.user_comments;
  while(*ptr){

    char *comment = *ptr;
    int i;

#ifdef LOG
    printf("libvorbis: %s\n", comment);
#endif

    for (i = 0; vorbis_comment_keys[i].key != NULL; i++) {

      if ( !strncasecmp (vorbis_comment_keys[i].key, comment,
			 strlen(vorbis_comment_keys[i].key)) ) {

#ifdef LOG
	printf ("libvorbis: known metadata %d %d\n",
		i, vorbis_comment_keys[i].xine_metainfo_index);
#endif

	this->stream->meta_info[vorbis_comment_keys[i].xine_metainfo_index] 
	  = strdup (comment + strlen(vorbis_comment_keys[i].key));

      }
    }
    ++ptr;
  }

  this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("vorbis");
}

static void vorbis_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  vorbis_decoder_t *this = (vorbis_decoder_t *) this_gen;
  ogg_packet *op = (ogg_packet *) buf->content;

#ifdef LOG
  printf ("libvorbis: decode buf=%08x content=%08x op=%08x packet=%08x flags=%08x\n",
	  buf, buf->content, op, op->packet, buf->decoder_flags);
#endif
  
  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
#ifdef LOG
    printf ("libvorbis: preview buffer, %d headers to go\n", this->header_count);
#endif
    if (this->header_count) {

      if(vorbis_synthesis_headerin(&this->vi,&this->vc,op)<0){ 
	/* error case; not a vorbis header */
	printf("libvorbis: this bitstream does not contain vorbis audio data.\n");
	return;
      }

      this->header_count--;

      if (!this->header_count) {
      
	int mode = AO_CAP_MODE_MONO;
	
	get_metadata (this);

	
	switch (this->vi.channels) {
	case 1: 
	  mode = AO_CAP_MODE_MONO;
	  break;
	case 2: 
	  mode = AO_CAP_MODE_STEREO;
	  break;
	case 4: 
	  mode = AO_CAP_MODE_4CHANNEL;
	  break;
	case 5: 
	  mode = AO_CAP_MODE_5CHANNEL;
	  break;
	case 6: 
	  mode = AO_CAP_MODE_5_1CHANNEL;
	  break;
	default:
	  printf ("libvorbis: help, %d channels ?!\n",
		  this->vi.channels);
	  /* FIXME: handle error */
	}
	
	this->convsize=MAX_NUM_SAMPLES/this->vi.channels;

	if (!this->output_open) {
	  this->output_open = this->stream->audio_out->open(this->stream->audio_out, 
						    this->stream,
						    16,
						    this->vi.rate,
						    mode) ;

	  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE]=this->vi.bitrate_nominal;

	}
	
	vorbis_synthesis_init(&this->vd,&this->vi); 
	vorbis_block_init(&this->vd,&this->vb);     
      }
    }
 
  } else if (this->output_open) {

    float **pcm;
    int samples;

    if(vorbis_synthesis(&this->vb,op)==0) 
      vorbis_synthesis_blockin(&this->vd,&this->vb);

    while ((samples=vorbis_synthesis_pcmout(&this->vd,&pcm))>0){

      int i,j;
      int clipflag=0;
      int bout=(samples<this->convsize?samples:this->convsize);
      audio_buffer_t *audio_buffer;

      audio_buffer = this->stream->audio_out->get_buffer (this->stream->audio_out);
      
      /* convert floats to 16 bit signed ints (host order) and
	 interleave */
      for(i=0;i<this->vi.channels;i++){
	ogg_int16_t *ptr=audio_buffer->mem+i;
	float  *mono=pcm[i];
	for(j=0;j<bout;j++){
	  int val=mono[j]*32767.f;
	  /* might as well guard against clipping */
	  if(val>32767){
	    val=32767;
	    clipflag=1;
	  }
	  if(val<-32768){
	    val=-32768;
	    clipflag=1;
	  }
	  *ptr=val;
	  ptr+=this->vi.channels;
	}
      }

      audio_buffer->vpts       = buf->pts;
      audio_buffer->num_frames = bout;

      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);

      buf->pts=0;
      vorbis_synthesis_read(&this->vd,bout);
    }
  } 
#ifdef LOG
  else
    printf ("libvorbis: output not open\n");

#endif
}

static void vorbis_dispose (audio_decoder_t *this_gen) {

  vorbis_decoder_t *this = (vorbis_decoder_t *) this_gen; 

  vorbis_block_clear(&this->vb);
  vorbis_dsp_clear(&this->vd);
  vorbis_comment_clear(&this->vc);
  vorbis_info_clear(&this->vi);  /* must be called last */

  if (this->output_open) 
    this->stream->audio_out->close (this->stream->audio_out, this->stream);

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, 
				     xine_stream_t *stream) {

  vorbis_decoder_t *this ;

  this = (vorbis_decoder_t *) malloc (sizeof (vorbis_decoder_t));

  this->audio_decoder.decode_data         = vorbis_decode_data;
  this->audio_decoder.reset               = vorbis_reset;
  this->audio_decoder.discontinuity       = vorbis_discontinuity;
  this->audio_decoder.dispose             = vorbis_dispose;
  this->stream                            = stream;

  this->output_open     = 0;
  this->header_count    = 3;
  this->convsize        = 0;

  vorbis_info_init(&this->vi);
  vorbis_comment_init(&this->vc);

  return (audio_decoder_t *) this;
}

/*
 * vorbis plugin class
 */

static char *get_identifier (audio_decoder_class_t *this) {
  return "vorbis";
}

static char *get_description (audio_decoder_class_t *this) {
  return "vorbis audio decoder plugin";
}

static void dispose_class (audio_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  vorbis_class_t *this;
  
  this = (vorbis_class_t *) malloc (sizeof (vorbis_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

static uint32_t audio_types[] = { 
  BUF_AUDIO_VORBIS, 0
 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 12, "vorbis", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
