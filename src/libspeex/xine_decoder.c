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
 * $Id: xine_decoder.c,v 1.1 2003/05/25 13:39:14 guenter Exp $
 *
 * (ogg/)speex audio decoder plugin (libspeex wrapper) for xine
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
#include <speex.h>
#include <speex_header.h>
#include <speex_callbacks.h>
#include <speex_stereo.h>

#define MAX_FRAME_SIZE 2000

/*
#define LOG
*/

typedef struct {
  audio_decoder_class_t   decoder_class;
} speex_class_t;

typedef struct speex_decoder_s {
  audio_decoder_t   audio_decoder;

  int64_t           pts;

  int               output_sampling_rate;
  int               output_open;
  int               output_mode;

  /* speex stuff */
  void             *st;
  int               frame_size;
  int               rate;
  int               nframes;
  int               channels;
  SpeexBits         bits;
  SpeexStereoState  stereo;
  int               expect_metadata;

  float             output[MAX_FRAME_SIZE];

  int               header_count;

  xine_stream_t    *stream;

} speex_decoder_t;


static void speex_reset (audio_decoder_t *this_gen) {

  speex_decoder_t *this = (speex_decoder_t *) this_gen;

  speex_bits_init (&this->bits);
}

static void speex_discontinuity (audio_decoder_t *this_gen) {

  speex_decoder_t *this = (speex_decoder_t *) this_gen;

  this->pts=0;
}

/* Known speex comment keys from ogg123 sources*/
static struct {
  char *key;         /* includes the '=' for programming convenience */
  int   xine_metainfo_index;
} speex_comment_keys[] = {
  {"ARTIST=", XINE_META_INFO_ARTIST},
  {"ALBUM=", XINE_META_INFO_ALBUM},
  {"TITLE=", XINE_META_INFO_TITLE},
  {"GENRE=", XINE_META_INFO_GENRE},
  {"DESCRIPTION=", XINE_META_INFO_COMMENT},
  {"DATE=", XINE_META_INFO_YEAR},
  {NULL, 0}
};

#define readint(buf, base) (((buf[base+3]<<24)&0xff000000)| \
                           ((buf[base+2]<<16)&0xff0000)| \
                           ((buf[base+1]<<8)&0xff00)| \
                            (buf[base]&0xff))

static
void read_metadata (speex_decoder_t *this, char * comments, int length)
{
  char * c = comments;
  int len, i, nb_fields;
  char * end;

  this->stream->meta_info[XINE_META_INFO_AUDIOCODEC] = strdup ("speex");

  if (length < 8) {
    printf ("libspeex: invalid/corrupted comments\n");
    return;
  }

  end = c+length;
  len = readint (c, 0);
  c += 4;

  if (c+len > end) {
    printf ("libspeex: invalid/corrupted comments\n");
    return;
  }

#ifdef LOG
  /* Encoder */
  printf ("libspeex: ");
  fwrite (c, 1, len, stdout);
  printf ("\n");
#endif

  c += len;

  if (c+4 > end) {
    printf ("libspeex: invalid/corrupted comments\n");
    return;
  }

  nb_fields = readint (c, 0);
  c += 4;

  for (i = 0; i < nb_fields; i++) {
    if (c+4 > end) {
      printf ("libspeex: invalid/corrupted comments\n");
      return;
    }

    len = readint (c, 0);
    c += 4;
    if (c+len > end) {
      printf ("libspeex: invalid/corrupted comments\n");
      return;
    }

#ifdef LOG
    printf ("libspeex: ");
    fwrite (c, 1, len, stdout);
    printf ("\n");
#endif

    for (i = 0; speex_comment_keys[i].key != NULL; i++) {

      if ( !strncasecmp (speex_comment_keys[i].key, c,
			 strlen(speex_comment_keys[i].key)) ) {
	int keylen = strlen(speex_comment_keys[i].key);
	char * meta_info;
	
#ifdef LOG
	printf ("libspeex: known metadata %d %d\n",
		i, speex_comment_keys[i].xine_metainfo_index);
#endif
	
	meta_info = xine_xmalloc (len - keylen);
	memcpy (meta_info, c + keylen, len - keylen);
	
	this->stream->meta_info[speex_comment_keys[i].xine_metainfo_index] =
	  meta_info;
      }
    }

    c += len;
  }
}

static void speex_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  speex_decoder_t *this = (speex_decoder_t *) this_gen;

#ifdef LOG
  printf ("libspeex: decode buf=%8p content=%8p flags=%08x\n",
	  buf, buf->content, buf->decoder_flags);
#endif

  if (buf->decoder_flags & BUF_FLAG_PREVIEW) {
#ifdef LOG
    printf ("libspeex: preview buffer, %d headers to go\n", this->header_count);
#endif

    if (this->header_count) {

      if (!this->st) {
	SpeexMode * spx_mode;
	SpeexHeader * spx_header;
	int modeID;
	int bitrate;

	speex_bits_init (&this->bits);

	spx_header = speex_packet_to_header (buf->content, buf->size);

	if (!spx_header) {
	  printf ("libspeex: could not read Speex header\n");
	  return;
	}

	modeID = spx_header->mode;
	spx_mode = speex_mode_list[modeID];

	if (spx_mode->bitstream_version != spx_header->mode_bitstream_version) {
	  printf ("libspeex: incompatible Speex mode bitstream version\n");
	  return;
	}

	this->st = speex_decoder_init (spx_mode);
	if (!this->st) {
	  printf ("libspeex: decoder initialization failed\n");
	  return;
	}

	this->rate = spx_header->rate;
	speex_decoder_ctl (this->st, SPEEX_SET_SAMPLING_RATE, &this->rate);
	this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE]
	  = this->rate;

	this->channels = spx_header->nb_channels;
	if (this->channels == 2) {
	  SpeexCallback callback;
	
	  callback.callback_id = SPEEX_INBAND_STEREO;
	  callback.func = speex_std_stereo_request_handler;
	  callback.data = &this->stereo;
	  speex_decoder_ctl (this->st, SPEEX_SET_HANDLER, &callback);
	}

	this->nframes = spx_header->frames_per_packet;
	if (!this->nframes) this->nframes = 1;
      
	speex_decoder_ctl (this->st, SPEEX_GET_FRAME_SIZE, &this->frame_size);

	speex_decoder_ctl (this->st, SPEEX_GET_BITRATE, &bitrate);
	if (bitrate <= 1) bitrate = 16000; /* assume 16 kbit */
	this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE] = bitrate;

	this->header_count += spx_header->extra_headers;
	this->expect_metadata = 1;

	free (spx_header);
      } else if (this->expect_metadata) {
	read_metadata (this, buf->content, buf->size);
      }

      this->header_count--;

      if (!this->header_count) {
	int mode = AO_CAP_MODE_MONO;

	switch (this->channels) {
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
	  printf ("libspeex: help, %d channels ?!\n",
		  this->channels);
	  /* FIXME: handle error */
	}
	
	if (!this->output_open) {
	  this->output_open =
	    this->stream->audio_out->open(this->stream->audio_out, 
					  this->stream,
					  16,
					  this->rate,
					  mode);
	}
      }
    }
    
  } else if (this->output_open) {
    int i, j;

    audio_buffer_t *audio_buffer;

    audio_buffer =
      this->stream->audio_out->get_buffer (this->stream->audio_out);

    speex_bits_read_from (&this->bits, buf->content, buf->size);

    for (j = 0; j < this->nframes; j++) {
      int ret;
      int bitrate;
      ogg_int16_t * ptr = audio_buffer->mem;

      ret = speex_decode (this->st, &this->bits, this->output);

      if (ret==-1)
	break;
      if (ret==-2) {
	printf ("libspeex: Decoding error, corrupted stream?\n");
	break;
      }
      if (speex_bits_remaining(&this->bits)<0) {
	printf ("libspeex: Decoding overflow, corrupted stream?\n");
	break;
      }

      if (this->channels == 2) {
	speex_decode_stereo (this->output, this->frame_size, &this->stereo);
      }

      speex_decoder_ctl (this->st, SPEEX_GET_BITRATE, &bitrate);
      if (bitrate <= 1) bitrate = 16000; /* assume 16 kbit */
      this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE] = bitrate;

      /*PCM saturation (just in case)*/
      for (i=0; i < this->frame_size * this->channels; i++)
	{
	  if (this->output[i]>32000.0)
	    this->output[i]=32000.0;
	  else if (this->output[i]<-32000.0)
	    this->output[i]=-32000.0;
	}

      /*Convert to short and play */	
      for (i=0; i< this->frame_size * this->channels; i++) {
	*ptr++ = (ogg_int16_t)this->output[i];
      }

      audio_buffer->vpts       = this->pts;
      this->pts=0;
      audio_buffer->num_frames = this->frame_size;
	
      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
	
      buf->pts=0;

    }
  }
#ifdef LOG
  else
    printf ("libspeex: output not open\n");
#endif
}

static void speex_dispose (audio_decoder_t *this_gen) {

  speex_decoder_t *this = (speex_decoder_t *) this_gen; 
  
  if (this->st) {
    speex_decoder_destroy (this->st);
  }
  speex_bits_destroy (&this->bits);

  if (this->output_open) 
    this->stream->audio_out->close (this->stream->audio_out, this->stream);

  free (this_gen);
}

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, 
				     xine_stream_t *stream) {

  speex_decoder_t *this ;
  static SpeexStereoState init_stereo = SPEEX_STEREO_STATE_INIT;

  this = (speex_decoder_t *) malloc (sizeof (speex_decoder_t));

  this->audio_decoder.decode_data         = speex_decode_data;
  this->audio_decoder.reset               = speex_reset;
  this->audio_decoder.discontinuity       = speex_discontinuity;
  this->audio_decoder.dispose             = speex_dispose;
  this->stream                            = stream;

  this->output_open     = 0;
  this->header_count    = 2;
  this->expect_metadata = 0;

  this->st = NULL;

  this->channels = 1;

  memcpy (&this->stereo, &init_stereo, sizeof (SpeexStereoState));

  return (audio_decoder_t *) this;
}

/*
 * speex plugin class
 */

static char *get_identifier (audio_decoder_class_t *this) {
  return "speex";
}

static char *get_description (audio_decoder_class_t *this) {
  return "Speex audio decoder plugin";
}

static void dispose_class (audio_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  speex_class_t *this;
  
  this = (speex_class_t *) malloc (sizeof (speex_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

static uint32_t audio_types[] = { 
  BUF_AUDIO_SPEEX, 0
 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 13, "speex", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
