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
 * $Id: xine_decoder.c,v 1.5 2002/07/17 20:29:04 miguelfreitas Exp $
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "audio_out.h"
#include "buffer.h"
#include "xine_internal.h"
#include "xineutils.h"
#include "faad.h"

/* #define LOG */

typedef struct faad_decoder_s {
  audio_decoder_t  audio_decoder;

  xine_t          *xine;
  
  /* faad2 stuff */
  faacDecHandle           faac_dec;
  faacDecConfigurationPtr faac_cfg;
  faacDecFrameInfo        faac_finfo;
  int                     faac_failed;
 
  int              mp4_mode;
  unsigned int    *sample_size_table;
  
  unsigned char   *buf;
  int              size;
  int              rec_audio_src_size;
  int              max_audio_src_size;
  int              pts;
  
  unsigned long    rate;
  int              bits_per_sample; 
  unsigned char    num_channels; 
  uint32_t         ao_cap_mode; 
   
  ao_instance_t   *audio_out;
  int              output_open;
} faad_decoder_t;

static int faad_can_handle (audio_decoder_t *this_gen, int buf_type) {
  buf_type &= 0xFFFF0000;

  return ( buf_type == BUF_AUDIO_AAC );
}


static void faad_reset (audio_decoder_t *this_gen) {

  faad_decoder_t *this = (faad_decoder_t *) this_gen;
  this->size = 0;
}

static void faad_init (audio_decoder_t *this_gen, ao_instance_t *audio_out) {

  faad_decoder_t *this = (faad_decoder_t *) this_gen;

  this->audio_out          = audio_out;
  this->output_open        = 0;
  this->faac_dec           = NULL; 
  this->faac_failed        = 0;
  this->buf                = NULL;
  this->size               = 0;
  this->max_audio_src_size = 0;
  this->sample_size_table  = NULL;
}

static int faad_open_dec( faad_decoder_t *this ) {
  this->faac_dec = faacDecOpen();
  if( !this->faac_dec ) {
    xine_log (this->xine, XINE_LOG_MSG,
              "libfaad: libfaad faacDecOpen() failed.\n" );
    this->faac_failed++;
    xine_report_codec( this->xine, XINE_CODEC_AUDIO, 0, BUF_AUDIO_AAC, 0);
    return 1;
  }
  
  if( !this->mp4_mode ) {
    /* Set the default object type and samplerate */
    /* This is useful for RAW AAC files */
    this->faac_cfg = faacDecGetCurrentConfiguration(this->faac_dec);
    if( this->faac_cfg ) {
      this->faac_cfg->defSampleRate = 44100;
      this->faac_cfg->outputFormat = FAAD_FMT_16BIT;
      this->faac_cfg->defObjectType = LC;
      faacDecSetConfiguration(this->faac_dec, this->faac_cfg);
    }
  }

  return 0;
}

static void faad_decode_audio ( faad_decoder_t *this, int end_frame ) {
  int used, decoded, outsize;
  uint8_t *sample_buffer;
  uint8_t *inbuf;
  audio_buffer_t *audio_buffer;
    
  if( !this->faac_dec )
    return;

  inbuf = this->buf;
  while( (this->mp4_mode && end_frame && this->size >= 10) ||
         (!this->mp4_mode && this->size >= this->rec_audio_src_size) ) {
      
    sample_buffer = faacDecDecode(this->faac_dec, 
                                  &this->faac_finfo, inbuf);
 
    if( this->mp4_mode && this->sample_size_table  )
      used = *this->sample_size_table++;
    else
      used = this->faac_finfo.bytesconsumed;

    decoded = this->faac_finfo.samples * 2; /* 1 sample = 2 bytes */
    
#ifdef LOG
//    printf("libfaad: decoded %d/%d output %ld\n",
//           used, this->size, this->faac_finfo.samples );
#endif
      
    if (sample_buffer == NULL) {
      printf("libfaad: %s\n", faacDecGetErrorMessage(this->faac_finfo.error));
      used = 1;
    }
    else {
      while( decoded ) {
        audio_buffer = this->audio_out->get_buffer (this->audio_out);
        
        if( decoded < audio_buffer->mem_size )
          outsize = decoded; 
        else
          outsize = audio_buffer->mem_size;
      
        xine_fast_memcpy( audio_buffer->mem, sample_buffer, outsize );

        audio_buffer->num_frames = outsize / (this->num_channels*2);
        audio_buffer->vpts = this->pts;

        this->audio_out->put_buffer (this->audio_out, audio_buffer);
        
        this->pts = 0;
        decoded -= outsize;
        sample_buffer += outsize;
      }
    }
      
    if(used >= this->size){
      this->size = 0;
    } else {
      this->size -= used;
      inbuf += used;
    }
  }

  if( this->mp4_mode && this->sample_size_table )
    this->size = 0;   
  else if( this->size )
    memmove( this->buf, inbuf, this->size);

  this->sample_size_table = NULL;
}

static void faad_decode_data (audio_decoder_t *this_gen, buf_element_t *buf) {

  faad_decoder_t *this = (faad_decoder_t *) this_gen;
  int used;
  
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)
    return;


  /* initialize libfaad with with config information from ESDS mp4/qt atom */
  if( !this->faac_dec && (buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      buf->decoder_info[1] == BUF_SPECIAL_DECODER_CONFIG ) {

    /* mode for playing data from .mp4 files */    
    this->mp4_mode = 1;

    if( !this->faac_dec ) {
      if( faad_open_dec(this) )
        return;
    }

    used = faacDecInit2(this->faac_dec, (void *)buf->decoder_info[3],
                        buf->decoder_info[2], &this->rate, &this->num_channels);
    
    if( used < 0 ) {
      xine_log (this->xine, XINE_LOG_MSG,
              "libfaad: libfaad faacDecInit2() failed.\n" );
      this->faac_failed++;
      this->faac_dec = NULL;
      xine_report_codec( this->xine, XINE_CODEC_AUDIO, 0, buf->type, 0);
      return;
    }
#ifdef LOG
    printf("libfaad: faacDecInit2 returned rate=%ld channels=%d\n",
            this->rate, this->num_channels );
#endif
  }

  /* get sample sizes table. this is needed since sample size 
     might differ from faac_finfo.bytesconsumed */
  if( (buf->decoder_flags & BUF_FLAG_SPECIAL) &&
      buf->decoder_info[1] == BUF_SPECIAL_SAMPLE_SIZE_TABLE ) {
#ifdef LOG
     printf("libfaad: sample_size_table received\n");
#endif
     this->sample_size_table = (unsigned int *)buf->decoder_info[3];
  }


  /* get audio parameters from file header 
     (may be overwritten by libfaad returned parameters) */  
  if (buf->decoder_flags & BUF_FLAG_HEADER) {
    
    this->rate=buf->decoder_info[1];
    this->bits_per_sample=buf->decoder_info[2] ; 
    this->num_channels=buf->decoder_info[3] ; 
  
    if (this->output_open) {
        this->audio_out->close (this->audio_out);
        this->output_open = 0;
    }

    if( this->faac_dec )
      faacDecClose(this->faac_dec);
    this->faac_dec = NULL;
    this->faac_failed = 0;
                                               
  } else {

#ifdef LOG
//    printf ("faad: decoding %d data bytes...\n", buf->size);
#endif

    if( (int)buf->size <= 0 || this->faac_failed )
      return;
  
    if( !this->size )
      this->pts = buf->pts;
  
    if( this->size + buf->size > this->max_audio_src_size ) {
      this->max_audio_src_size = this->size + 2 * buf->size;
      this->buf = realloc( this->buf, this->max_audio_src_size );
    }
  
    memcpy (&this->buf[this->size], buf->content, buf->size);
    this->size += buf->size;
    
    if( !this->faac_dec ) {

      /* mode for playing data from .aac files */    
      this->mp4_mode = 0;

      if( faad_open_dec(this) )
        return;

      used = faacDecInit(this->faac_dec, this->buf,
                         &this->rate, &this->num_channels);
      if( used < 0 ) {
        xine_log (this->xine, XINE_LOG_MSG,
                "libfaad: libfaad faacDecInit() failed.\n" );
        this->faac_failed++;
        faacDecClose(this->faac_dec);
        this->faac_dec = NULL;
        xine_report_codec( this->xine, XINE_CODEC_AUDIO, 0, buf->type, 0);
        return;
      }
#ifdef LOG
      printf("libfaad: faacDecInit() returned rate=%ld channels=%d (used=%d)\n",
              this->rate, this->num_channels, used);
#endif
                    
      this->size -= used;
      memmove(this->buf, &this->buf[used], this->size);

    } else {

      /* open audio device as needed */
      if (!this->output_open) {
        switch( this->num_channels ) {
          case 1:
            this->ao_cap_mode=AO_CAP_MODE_MONO; 
            break;
          case 2:
            this->ao_cap_mode=AO_CAP_MODE_STEREO;
            break; 
        }
        this->output_open = this->audio_out->open (this->audio_out,
                                                   this->bits_per_sample,
                                                   this->rate,
                                                   this->ao_cap_mode) ;
                                                               
        this->rec_audio_src_size = this->num_channels * FAAD_MIN_STREAMSIZE;
      }

      faad_decode_audio(this, buf->decoder_flags & BUF_FLAG_FRAME_END );
    }
  }
}

static void faad_close (audio_decoder_t *this_gen) {

  faad_decoder_t *this = (faad_decoder_t *) this_gen; 

  if (this->output_open) 
    this->audio_out->close (this->audio_out);
  this->output_open = 0;
    
  if( this->buf )
    free(this->buf);
  this->buf = NULL;
  this->size = 0;
  this->max_audio_src_size = 0;
  this->sample_size_table = NULL;
  
  if( this->faac_dec )
    faacDecClose(this->faac_dec);
  this->faac_dec = NULL;
  this->faac_failed = 0;
}

static char *faad_get_id(void) {
  return "faad";
}

static void faad_dispose (audio_decoder_t *this_gen) {
  free (this_gen);
}

audio_decoder_t *init_audio_decoder_plugin (int iface_version, xine_t *xine) {

  faad_decoder_t *this ;

  if (iface_version != 9) {
    printf(_("libfaad: plugin doesn't support plugin API version %d.\n"
	     "libfaad: this means there's a version mismatch between xine and this "
	     "libfaad: decoder plugin.\nInstalling current plugins should help.\n"),
	     iface_version);
    return NULL;
  }

  this = (faad_decoder_t *) malloc (sizeof (faad_decoder_t));
  this->xine = xine;
  
  this->audio_decoder.interface_version   = iface_version;
  this->audio_decoder.can_handle          = faad_can_handle;
  this->audio_decoder.init                = faad_init;
  this->audio_decoder.decode_data         = faad_decode_data;
  this->audio_decoder.reset               = faad_reset;
  this->audio_decoder.close               = faad_close;
  this->audio_decoder.get_identifier      = faad_get_id;
  this->audio_decoder.dispose             = faad_dispose;
  this->audio_decoder.priority            = 1;
    
  return (audio_decoder_t *) this;
}

