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
 * John McCutchan 2003
 * FLAC Decoder (http://flac.sf.net)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <FLAC/stream_decoder.h>


#include "xine_internal.h"
#include "audio_out.h"
#include "buffer.h"

/*
#define LOG 1
*/

typedef struct {
  audio_decoder_class_t   decoder_class;
} flac_class_t;

typedef struct flac_decoder_s {
  audio_decoder_t   audio_decoder;

  int64_t           pts;

  int               output_sampling_rate;
  int               output_open;
  int               output_mode;

  xine_stream_t    *stream;

  FLAC__StreamDecoder *flac_decoder;

  int sample_rate;
  int bits_per_sample;
  int channels;

  unsigned char *buf;
  int            buf_size;
  int            buf_pos;
  int            min_size;

} flac_decoder_t;

/*
 * FLAC callback functions
 */

static FLAC__StreamDecoderReadStatus 
flac_read_callback (const FLAC__StreamDecoder *decoder, 
                    FLAC__byte buffer[],
                    unsigned *bytes,
                    void *client_data)
{
    flac_decoder_t *this = (flac_decoder_t *)client_data;
    int number_of_bytes_to_copy;

#ifdef LOG
    printf("libflac: flac_read_callback: %d\n", *bytes);
#endif

    if (this->buf_pos > *bytes)
        number_of_bytes_to_copy = *bytes;
    else
        number_of_bytes_to_copy = this->buf_pos;

#ifdef LOG
    printf("libflac: number_of_bytes_to_copy: %d\n", number_of_bytes_to_copy);
#endif

    *bytes = number_of_bytes_to_copy;

    xine_fast_memcpy (buffer, this->buf, number_of_bytes_to_copy);

    this->buf_pos -= number_of_bytes_to_copy;
    memmove(this->buf, &this->buf[number_of_bytes_to_copy], this->buf_pos );

    if(number_of_bytes_to_copy)
      return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    else
      return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

static FLAC__StreamDecoderWriteStatus
flac_write_callback (const FLAC__StreamDecoder *decoder,
                     const FLAC__Frame *frame,
                     const FLAC__int32 *const buffer[],
                     void *client_data)
{
    flac_decoder_t *this = (flac_decoder_t *)client_data;
    audio_buffer_t *audio_buffer = NULL;
    int samples_left = frame->header.blocksize;
    int bytes_per_sample = (frame->header.bits_per_sample == 8)?1:2;
    int buf_samples;
    int8_t *data8;
    int16_t *data16;
    int i,j;

#ifdef LOG
    printf("libflac: flac_write_callback\n");
#endif

    while( samples_left ) {
     
      audio_buffer = this->stream->audio_out->get_buffer(this->stream->audio_out);

      if( audio_buffer->mem_size < samples_left * frame->header.channels * bytes_per_sample )
        buf_samples = audio_buffer->mem_size / (frame->header.channels * bytes_per_sample);
      else
        buf_samples = samples_left;
        
        
      if( frame->header.bits_per_sample == 8 ) {
        data8 = (int8_t *)audio_buffer->mem;
        
        for( j=0; j < buf_samples; j++ )
          for( i=0; i < frame->header.channels; i++ )
            *data8++ = buffer[i][j];
       
      } else {
        
        data16 = (int16_t *)audio_buffer->mem;
        
        for( j=0; j < buf_samples; j++ )
          for( i=0; i < frame->header.channels; i++ )
            *data16++ = buffer[i][j];
      }     

      audio_buffer->num_frames = buf_samples;
      audio_buffer->vpts = this->pts;
      this->pts = 0;
      this->stream->audio_out->put_buffer (this->stream->audio_out, audio_buffer, this->stream);
      
      samples_left -= buf_samples;
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
flac_metadata_callback (const FLAC__StreamDecoder *decoder,
                        const FLAC__StreamMetadata *metadata,
                        void *client_data)
{
    flac_decoder_t *this = (flac_decoder_t *)client_data;
  
#ifdef LOG
    printf("libflac: Metadata callback called!\n");
#endif
       
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
#ifdef LOG
      printf("libflac: min_blocksize = %d\n", metadata->data.stream_info.min_blocksize);
      printf("libflac: max_blocksize = %d\n", metadata->data.stream_info.max_blocksize);
      printf("libflac: min_framesize = %d\n", metadata->data.stream_info.min_framesize);
      printf("libflac: max_framesize = %d\n", metadata->data.stream_info.max_framesize);
#endif
      
      /* does not work well:
      this->min_size = 2 * metadata->data.stream_info.max_blocksize; */
    }
    return;
}

static void
flac_error_callback (const FLAC__StreamDecoder *decoder,
                     FLAC__StreamDecoderErrorStatus status,
                     void *client_data)
{
    /* This will be called if there is an error in the flac stream */
#ifdef LOG
    printf("libflac: flac_error_callback\n");

    if (status == FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC)
        printf("libflac: Decoder lost synchronization.\n");
    else if (status == FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER)
        printf("libflac: Decoder encounted a corrupted frame header.\n");
    else if (status == FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH)
        printf("libflac: Frame's data did not match the CRC in the footer.\n");
    else
        printf("libflac: unknown error.\n");
#endif
                                                                                
    return;
}




/*
 * FLAC plugin decoder
 */

static void 
flac_reset (audio_decoder_t *this_gen)
{
  flac_decoder_t *this = (flac_decoder_t *) this_gen;

  this->buf_pos = 0;
  
  if( FLAC__stream_decoder_get_state(this->flac_decoder) != 
                FLAC__STREAM_DECODER_SEARCH_FOR_METADATA ) 
    FLAC__stream_decoder_flush (this->flac_decoder);
}

static void 
flac_discontinuity (audio_decoder_t *this_gen) 
{
  flac_decoder_t *this = (flac_decoder_t *) this_gen;

  this->pts = 0;
#ifdef LOG
  printf("libflac: Discontinuity!\n");
#endif
}

static void 
flac_decode_data (audio_decoder_t *this_gen, buf_element_t *buf)
{
    flac_decoder_t *this = (flac_decoder_t *) this_gen;
    int ret = 1;
    
    /* We are getting the stream header, open up the audio
     * device, and collect information about the stream
     */
    if (buf->decoder_flags & BUF_FLAG_HEADER)
    {
        int mode = AO_CAP_MODE_MONO;

        this->sample_rate     = buf->decoder_info[1];
        this->bits_per_sample = buf->decoder_info[2];
        this->channels        = buf->decoder_info[3];

        switch (this->channels)
        {
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
        }

        if (!this->output_open)
        {
            this->output_open = this->stream->audio_out->open (
                                            this->stream->audio_out,
                                            this->stream,
                                            this->bits_per_sample,
                                            this->sample_rate,
                                            mode);


        }
        this->buf_pos = 0;
    } else if (this->output_open)
    {
        /* This isn't a header frame and we have opened the output device */


        /* What we have buffered so far, and what is coming in
         * is larger than our buffer
         */
        if (this->buf_pos + buf->size > this->buf_size)
        {
            this->buf_size += 2 * buf->size;
            this->buf = realloc (this->buf, this->buf_size);
#ifdef LOG
            printf("libflac: reallocating buffer to %d\n", this->buf_size);
#endif
        }

        xine_fast_memcpy (&this->buf[this->buf_pos], buf->content, buf->size);
        this->buf_pos += buf->size;
        
        if (buf->pts)
          this->pts = buf->pts;

        /* We have enough to decode a frame */
        while( ret && this->buf_pos > this->min_size ) {
            
            if( FLAC__stream_decoder_get_state(this->flac_decoder) == 
                FLAC__STREAM_DECODER_SEARCH_FOR_METADATA ) {
#ifdef LOG
              printf("libflac: process_until_end_of_metadata\n");
#endif
              ret = FLAC__stream_decoder_process_until_end_of_metadata (this->flac_decoder);
            } else {
#ifdef LOG
              printf("libflac: process_single\n");
#endif
              ret = FLAC__stream_decoder_process_single (this->flac_decoder);
            }
        }
    } else
        return;
    

}

static void
flac_dispose (audio_decoder_t *this_gen) {
    flac_decoder_t *this = (flac_decoder_t *) this_gen; 

    FLAC__stream_decoder_finish (this->flac_decoder);

    FLAC__stream_decoder_delete (this->flac_decoder);

    if (this->output_open) 
        this->stream->audio_out->close (this->stream->audio_out, this->stream);

    free (this_gen);
}

static audio_decoder_t *
open_plugin (audio_decoder_class_t *class_gen, xine_stream_t *stream) {
    flac_decoder_t *this ;

    this = (flac_decoder_t *) malloc (sizeof (flac_decoder_t));

    this->audio_decoder.decode_data         = flac_decode_data;
    this->audio_decoder.reset               = flac_reset;
    this->audio_decoder.discontinuity       = flac_discontinuity;
    this->audio_decoder.dispose             = flac_dispose;
    this->stream                            = stream;

    this->output_open     = 0;
    this->buf      = NULL;
    this->buf_size = 0;
    this->min_size = 65536;
    this->pts      = 0;

    this->flac_decoder = FLAC__stream_decoder_new();

    FLAC__stream_decoder_set_read_callback     (this->flac_decoder,
                                                flac_read_callback);
    FLAC__stream_decoder_set_write_callback    (this->flac_decoder,
                                                flac_write_callback);
    FLAC__stream_decoder_set_metadata_callback (this->flac_decoder,
                                                flac_metadata_callback);
    FLAC__stream_decoder_set_error_callback    (this->flac_decoder,
                                                flac_error_callback);

    FLAC__stream_decoder_set_client_data (this->flac_decoder, this);

    FLAC__stream_decoder_init (this->flac_decoder);

    return (audio_decoder_t *) this;
}

/*
 * flac plugin class
 */

static char *get_identifier (audio_decoder_class_t *this) {
  return "flacdec";
}

static char *get_description (audio_decoder_class_t *this) {
  return "flac audio decoder plugin";
}

static void dispose_class (audio_decoder_class_t *this) {
  free (this);
}

static void *
init_plugin (xine_t *xine, void *data) {
    flac_class_t *this;
  
    this = (flac_class_t *) malloc (sizeof (flac_class_t));

    this->decoder_class.open_plugin     = open_plugin;
    this->decoder_class.get_identifier  = get_identifier;
    this->decoder_class.get_description = get_description;
    this->decoder_class.dispose         = dispose_class;


    return this;
}

static uint32_t audio_types[] = { 
  BUF_AUDIO_FLAC, 0
 };

static decoder_info_t dec_info_audio = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

/* from demux_flac.c */
void *demux_flac_init_class (xine_t *xine, void *data);

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 20, "flac", XINE_VERSION_CODE, NULL, demux_flac_init_class },
  { PLUGIN_AUDIO_DECODER, 13, "flacdec", XINE_VERSION_CODE, &dec_info_audio, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
