/*
 * Copyright (C) 2003 J.Asselman <j.asselman@itsec.nl>
 *                    ITsec Professional Services
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
 * Dummy decoder for RAW Recorded PCM data
 * Sample rate: 44100
 * Channels: 2 (Stereo)
 * Resolution: 16
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "xine_internal.h"
#include "video_out.h"
#include "audio_out.h"
#include "buffer.h"
#include "xineutils.h"
#include "bswap.h"

#define AUDIOBUFSIZE 128*1024

#define SAMPLERATE 44100
#define CHANNELS 2
#define BITS 16
#define CAPMODE AO_CAP_MODE_STEREO

typedef struct {
  audio_decoder_class_t   decoder_class;
} pcm_class_t;

typedef struct pcm_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_stream_t    *stream;

  unsigned int     buf_type;

  unsigned char    *buf;
  int               bufsize;
  int               size;
  char		    open;
  int		  disc;
} pcm_decoder_t;

static void pcm_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {\
   pcm_decoder_t *this = (pcm_decoder_t *) this_gen;
   audio_buffer_t           *aud;
   char *offset;
   int	 bytes_left = buf->decoder_info[1];
   int	 bits_per_frame = buf->decoder_info[1] / buf->decoder_info[0];
   
#ifdef LOG
   printf(__FILE__ ": decode_data, flags=0x%08x , mem size: %d, frames: %d...\n", 
	 buf->decoder_flags, buf->decoder_info[1], buf->decoder_info[0]);
#endif

   if (this->stream->stream_info[XINE_STREAM_INFO_AUDIO_MODE] == 0) {
#ifdef LOG
      printf(__FILE__ ": Someone changed the audio mode. Closing device\r");
#endif
      this->stream->audio_out->close(this->stream->audio_out, this->stream);
      this->open = 0;
   }

   if (!this->open)
      this->open = this->stream->audio_out->open(this->stream->audio_out,
	    this->stream, BITS, SAMPLERATE, CAPMODE);
   
   if (!this->open)
      /* Snif :'( output still not open */
      return;
  
   offset = buf->content;
   
   while (bytes_left > 0 && (!this->disc || buf->pts)) {
      int size;
      
      if (buf->pts)
	 this->disc = 0;
      
      aud = this->stream->audio_out->get_buffer(this->stream->audio_out);
      
      if (aud->mem_size == 0) {
         printf(__FILE__ ": :( Got an audio buffer with size 0!\r\n");
         return;
      }
      
      size = bytes_left > aud->mem_size ? aud->mem_size : bytes_left;
   
      aud->vpts = buf->pts;
      aud->num_frames = size / bits_per_frame;

      xine_fast_memcpy(aud->mem, offset, size);
   
      this->stream->audio_out->put_buffer(this->stream->audio_out, aud, this->stream);

      bytes_left -= size;
      offset += size;

      buf->pts = 0;
   }
}

static void pcm_reset (audio_decoder_t *this_gen) {
  /* pcm_decoder_t *this = (pcm_decoder_t *) this_gen; */
}

static void pcm_discontinuity (audio_decoder_t *this_gen)
{
   pcm_decoder_t *this = (pcm_decoder_t *) this_gen;

   this->disc = 1;
}

static void pcm_dispose (audio_decoder_t *this_gen)
{
   pcm_decoder_t *this = (pcm_decoder_t *) this_gen;

#ifdef LOG 
   printf(__FILE__ ": Cleaning up\n");
#endif
   
   if (this->open)
      this->stream->audio_out->close (this->stream->audio_out, this->stream);

   this->open = 0;
   
   if (this->buf)
      free(this->buf);
   
   free (this_gen);
}

/*
 * PCM decoder class code
 */

static audio_decoder_t *open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {

   pcm_decoder_t *this;
   
   this = (pcm_decoder_t *) malloc (sizeof (pcm_decoder_t));
   memset(this, 0, sizeof (pcm_decoder_t));
    
   this->audio_decoder.decode_data         = pcm_decode_data;
   this->audio_decoder.reset               = pcm_reset;
   this->audio_decoder.discontinuity       = pcm_discontinuity;
   this->audio_decoder.dispose             = pcm_dispose;
   
   this->buf = NULL;
   this->stream = stream;
   this->open = 0;

   stream->audio_out->open (stream->audio_out, stream, 
	 BITS, SAMPLERATE, CAPMODE);
      
  return &this->audio_decoder;
}

static char *get_identifier (audio_decoder_class_t *this) {
  return "PCM";
}

static char *get_description (audio_decoder_class_t *this) {
  return "Dummy PCM stream decoder";
}

static void dispose_class (audio_decoder_class_t *this) {
  free (this);
}

static void *init_plugin (xine_t *xine, void *data) {

  pcm_class_t *this ;

  this = (pcm_class_t *) malloc (sizeof (pcm_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;

  return this;
}

static uint32_t audio_types[] = { BUF_AUDIO_RAWPCM, 0 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  10                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_AUDIO_DECODER, 13, "pcm", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
/*
 * vim:sw=3:sts=3:
 */

