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
 * FILM (CPK) File Demuxer by Mike Melanson (melanson@pcisys.net)
 * For more information on the FILM file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_film.c,v 1.53 2003/01/10 21:10:59 miguelfreitas Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "demux.h"
#include "bswap.h"

#define FOURCC_TAG( ch0, ch1, ch2, ch3 )                                \
        ( (long)(unsigned char)(ch3) | ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | ( (long)(unsigned char)(ch0) << 24 ) )

#define FILM_TAG FOURCC_TAG('F', 'I', 'L', 'M')
#define FDSC_TAG FOURCC_TAG('F', 'D', 'S', 'C')
#define STAB_TAG FOURCC_TAG('S', 'T', 'A', 'B')
#define CVID_TAG FOURCC_TAG('c', 'v', 'i', 'd')

typedef struct {
  off_t sample_offset;
  unsigned int sample_size;
  unsigned int syncinfo1;
  unsigned int syncinfo2;
  int64_t pts;
} film_sample_t;

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  off_t                data_start;
  off_t                data_size;
  int                  status;

  /* when this flag is set, demuxer only dispatches audio samples until it
   * encounters a video keyframe, then it starts sending every frame again */
  int                  waiting_for_keyframe;

  char                 version[4];

  /* video information */
  unsigned int         video_codec;
  unsigned int         video_type;
  xine_bmiheader       bih;

  /* audio information */
  unsigned int         audio_type;
  unsigned int         sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;

  /* playback information */
  unsigned int         frequency;
  unsigned int         sample_count;
  film_sample_t       *sample_table;
  unsigned int         current_sample;
  unsigned int         last_sample;
  int                  total_time;

  char                 last_mrl[1024];
} demux_film_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;
  config_values_t  *config;
} demux_film_class_t;

/* Open a FILM file
 * This function is called from the _open() function of this demuxer.
 * It returns 1 if FILM file was opened successfully. */
static int open_film_file(demux_film_t *film) {

  unsigned char *film_header;
  unsigned int film_header_size;
  unsigned char scratch[16];
  unsigned int chunk_type;
  unsigned int chunk_size;
  unsigned int i, j;
  unsigned int audio_byte_count = 0;
  int64_t largest_pts = 0;

  /* initialize structure fields */
  film->bih.biWidth = 0;
  film->bih.biHeight = 0;
  film->video_codec = 0;
  film->sample_rate = 0;
  film->audio_bits = 0;
  film->audio_channels = 0;

  /* reset the file */
  film->input->seek(film->input, 0, SEEK_SET);

  /* get the signature, header length and file version */
  if (film->input->read(film->input, scratch, 16) != 16) {
    return 0;
  }

  /* FILM signature correct? */
  if (strncmp(scratch, "FILM", 4)) {
    return 0;
  }

  /* header size = header size - 16-byte FILM signature */
  film_header_size = BE_32(&scratch[4]) - 16;
  film_header = xine_xmalloc(film_header_size);
  if (!film_header)
    return 0;
  strncpy(film->version, &scratch[8], 4);

  /* load the rest of the FILM header */
  if (film->input->read(film->input, film_header, film_header_size) != 
    film_header_size) {
    return 0;
  }

  /* get the starting offset */
  film->data_start = film->input->get_current_pos(film->input);
  film->data_size = film->input->get_length(film->input) - film->data_start;

  /* traverse the FILM header */
  i = 0;
  while (i < film_header_size) {
    chunk_type = BE_32(&film_header[i]);
    chunk_size = BE_32(&film_header[i + 4]);

    /* sanity check the chunk size */
    if (i + chunk_size > film_header_size) {
      xine_log(film->stream->xine, XINE_LOG_MSG,
        _("invalid FILM chunk size\n"));
      return 0;
    }

    switch(chunk_type) {
    case FDSC_TAG:
      /* always fetch the video information */
      film->bih.biWidth = BE_32(&film_header[i + 16]);
      film->bih.biHeight = BE_32(&film_header[i + 12]);
      film->video_codec = *(uint32_t *)&film_header[i + 8];
      film->video_type = fourcc_to_buf_video(*(uint32_t *)&film_header[i + 8]);

      if( !film->video_type )
        film->video_type = BUF_VIDEO_UNKNOWN;
      
      /* fetch the audio information if the chunk size checks out */
      if (chunk_size == 32) {
        film->audio_channels = film_header[21];
        film->audio_bits = film_header[22];
        film->sample_rate = BE_16(&film_header[24]);
      } else {
        /* If the FDSC chunk is not 32 bytes long, this is an early FILM
         * file. Make a few assumptions about the audio parms based on the
         * video codec used in the file. */
        if (film->video_type == BUF_VIDEO_CINEPAK) {
          film->audio_channels = 1;
          film->audio_bits = 8;
          film->sample_rate = 22050;
        } else if (film->video_type == BUF_VIDEO_SEGA) {
          film->audio_channels = 1;
          film->audio_bits = 8;
          film->sample_rate = 16000;
        }
      }
      if (film->sample_rate)
        film->audio_type = BUF_AUDIO_LPCM_BE;
      else
        film->audio_type = 0;
      break;

    case STAB_TAG:
      /* load the sample table */
      if (film->sample_table)
        free(film->sample_table);
      film->frequency = BE_32(&film_header[i + 8]);
      film->sample_count = BE_32(&film_header[i + 12]);
      film->sample_table =
        xine_xmalloc(film->sample_count * sizeof(film_sample_t));
      for (j = 0; j < film->sample_count; j++) {
        film->sample_table[j].sample_offset = 
          BE_32(&film_header[(i + 16) + j * 16 + 0])
          + film_header_size + 16;
        film->sample_table[j].sample_size = 
          BE_32(&film_header[(i + 16) + j * 16 + 4]);
        film->sample_table[j].syncinfo1 = 
          BE_32(&film_header[(i + 16) + j * 16 + 8]);
        film->sample_table[j].syncinfo2 = 
          BE_32(&film_header[(i + 16) + j * 16 + 12]);

        /* figure out the pts */
        if (film->sample_table[j].syncinfo1 == 0xFFFFFFFF) {
          film->sample_table[j].pts = audio_byte_count;
          film->sample_table[j].pts *= 90000;
          film->sample_table[j].pts /= 
            (film->sample_rate * film->audio_channels * (film->audio_bits / 8));
          audio_byte_count += film->sample_table[j].sample_size;
        }
        else
          film->sample_table[j].pts = 
            (90000 * (film->sample_table[j].syncinfo1 & 0x7FFFFFFF)) /
            film->frequency;

        if (film->sample_table[j].pts > largest_pts)
          largest_pts = film->sample_table[j].pts;
      }

      /*
       * in some files, this chunk length does not account for the 16-byte
       * chunk preamble; watch for it
       */
      if (chunk_size == film->sample_count * 16)
        i += 16;
      break;

    default:
      xine_log(film->stream->xine, XINE_LOG_MSG,
        _("unrecognized FILM chunk\n"));
      return 0;
    }

    i += chunk_size;
  }

  film->total_time = largest_pts / 90;

  return 1;
}

static int demux_film_send_chunk(demux_plugin_t *this_gen) {

  demux_film_t *this = (demux_film_t *) this_gen;
  buf_element_t *buf = NULL;
  unsigned int cvid_chunk_size;
  unsigned int i, j;
  int fixed_cvid_header;
  unsigned int remaining_sample_bytes;
  int64_t frame_duration;

  i = this->current_sample;

  /* if there is an incongruency between last and current sample, it
   * must be time to send a new pts */
  if (this->last_sample + 1 != this->current_sample) {
    /* send new pts */
    xine_demux_control_newpts(this->stream, this->sample_table[i].pts,
      (this->sample_table[i].pts) ? BUF_FLAG_SEEK : 0);
  }

  this->last_sample = this->current_sample;
  this->current_sample++;

  /* check if all the samples have been sent */
  if (i >= this->sample_count) {
    this->status = DEMUX_FINISHED;
    return this->status;
  }

  /* check if we're only sending audio samples until the next keyframe */
  if ((this->waiting_for_keyframe) && 
      (this->sample_table[i].syncinfo1 != 0xFFFFFFFF)) {
    if ((this->sample_table[i].syncinfo1 & 0x80000000) == 0) {
      this->waiting_for_keyframe = 0;
    } else {
      /* move on to the next sample */
      return this->status;
    }
  }

  if ((this->sample_table[i].syncinfo1 != 0xFFFFFFFF) &&
    (this->video_type == BUF_VIDEO_CINEPAK)) {
    /* do a special song and dance when loading CVID data */
    if (this->version[0])
      cvid_chunk_size = this->sample_table[i].sample_size - 2;
    else
      cvid_chunk_size = this->sample_table[i].sample_size - 6;

    /* reset flag */
    fixed_cvid_header = 0;

    remaining_sample_bytes = cvid_chunk_size;
    this->input->seek(this->input, this->sample_table[i].sample_offset,
      SEEK_SET);

    /* frame duration is the pts diff between this video frame and
     * the next video frame (so search for the next video frame) */
    frame_duration = 0;
    j = i;
    while (++j < this->sample_count) {
      if (this->sample_table[j].syncinfo1 != 0xFFFFFFFF) {
        frame_duration =
          this->sample_table[j].pts - this->sample_table[i].pts;
        break;
      }
    }

    while (remaining_sample_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = this->video_type;
      buf->extra_info->input_pos = 
        this->sample_table[i].sample_offset - this->data_start;
      buf->extra_info->input_length = this->data_size;
      buf->extra_info->input_time = this->sample_table[i].pts / 90;
      buf->pts = this->sample_table[i].pts;

      /* set the frame duration */
      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = frame_duration;
            
      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (!fixed_cvid_header) {
        if (this->input->read(this->input, buf->content, 10) != 10) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        /* skip over the extra non-spec CVID bytes */
        this->input->seek(this->input, 
          this->sample_table[i].sample_size - cvid_chunk_size, SEEK_CUR);

        /* load the rest of the chunk */
        if (this->input->read(this->input, buf->content + 10, 
          buf->size - 10) != buf->size - 10) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }

        /* adjust the length in the CVID data chunk */
        buf->content[1] = (cvid_chunk_size >> 16) & 0xFF;
        buf->content[2] = (cvid_chunk_size >>  8) & 0xFF;
        buf->content[3] = (cvid_chunk_size >>  0) & 0xFF;

        fixed_cvid_header = 1;
      } else {
        if (this->input->read(this->input, buf->content, buf->size) !=
          buf->size) {
          buf->free_buffer(buf);
          this->status = DEMUX_FINISHED;
          break;
        }
      }

      if ((this->sample_table[i].syncinfo1 & 0x80000000) == 0)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;
      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
      this->video_fifo->put(this->video_fifo, buf);
    }

  } else if (this->sample_table[i].syncinfo1 != 0xFFFFFFFF) {

    /* load a non-cvid video chunk */
    remaining_sample_bytes = this->sample_table[i].sample_size;
    this->input->seek(this->input, this->sample_table[i].sample_offset,
      SEEK_SET);

    /* frame duration is the pts diff between this video frame and
     * the next video frame (so search for the next video frame) */
    frame_duration = 0;
    j = i;
    while (++j < this->sample_count) {
      if (this->sample_table[j].syncinfo1 != 0xFFFFFFFF) {
        frame_duration =
          this->sample_table[j].pts - this->sample_table[i].pts;
        break;
      }
    }

    while (remaining_sample_bytes) {
      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
      buf->type = this->video_type;
      buf->extra_info->input_pos = 
        this->sample_table[i].sample_offset - this->data_start;
      buf->extra_info->input_length = this->data_size;
      buf->extra_info->input_time = this->sample_table[i].pts / 90;
      buf->pts = this->sample_table[i].pts;

      /* set the frame duration */
      buf->decoder_flags |= BUF_FLAG_FRAMERATE;
      buf->decoder_info[0] = frame_duration;
            
      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if ((this->sample_table[i].syncinfo1 & 0x80000000) == 0)
        buf->decoder_flags |= BUF_FLAG_KEYFRAME;
      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;
      this->video_fifo->put(this->video_fifo, buf);
    }
  } else if( this->audio_fifo ) {
    /* load an audio sample and packetize it */
    remaining_sample_bytes = this->sample_table[i].sample_size;
    this->input->seek(this->input, this->sample_table[i].sample_offset,
      SEEK_SET);

    while (remaining_sample_bytes) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = this->audio_type;
      buf->extra_info->input_pos = 
        this->sample_table[i].sample_offset - this->data_start;
      buf->extra_info->input_length = this->data_size;
      buf->extra_info->input_time = this->sample_table[i].pts / 90;
      buf->pts = this->sample_table[i].pts;

      if (remaining_sample_bytes > buf->max_size)
        buf->size = buf->max_size;
      else
        buf->size = remaining_sample_bytes;
      remaining_sample_bytes -= buf->size;

      if (this->input->read(this->input, buf->content, buf->size) !=
        buf->size) {
        buf->free_buffer(buf);
        this->status = DEMUX_FINISHED;
        break;
      }

      if (this->video_type == BUF_VIDEO_SEGA) {
        /* if the file uses the SEGA video codec, assume this is
         * sign/magnitude audio */
        for (j = 0; j < buf->size; j++)
          if (buf->content[j] < 0x80)
            buf->content[j] += 0x80;
          else
            buf->content[j] = -(buf->content[j] & 0x7F) + 0x80;
      } else if (this->audio_bits == 8) {
        /* convert 8-bit data from signed -> unsigned */
        for (j = 0; j < buf->size; j++)
          buf->content[j] += 0x80;
      }

      if (!remaining_sample_bytes)
        buf->decoder_flags |= BUF_FLAG_FRAME_END;

      this->audio_fifo->put(this->audio_fifo, buf);
    }
  }
  
  return this->status;
}

static void demux_film_send_headers(demux_plugin_t *this_gen) {

  demux_film_t *this = (demux_film_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 
    (this->video_type) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 
    (this->audio_type) ? 1 : 0;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]  = this->bih.biWidth;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT] = this->bih.biHeight;
  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_FOURCC] = this->video_codec;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->sample_rate;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->audio_bits;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->video_type) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = 3000;  /* initial video_step */
    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = this->video_type;
    this->video_fifo->put (this->video_fifo, buf);
  }

  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = BUF_AUDIO_LPCM_BE;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->sample_rate;
    buf->decoder_info[2] = this->audio_bits;
    buf->decoder_info[3] = this->audio_channels;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_film_seek (demux_plugin_t *this_gen,
                            off_t start_pos, int start_time) {
  demux_film_t *this = (demux_film_t *) this_gen;
  int best_index;
  int left, middle, right;
  int found;
  int64_t keyframe_pts;

  this->waiting_for_keyframe = 0;

  /* perform a binary search on the sample table, testing the offset 
   * boundaries first */
  if (start_pos <= 0)
    best_index = 0;
  else if (start_pos >= this->data_size) {
    this->status = DEMUX_FINISHED;
    return this->status;
  } else {
    start_pos += this->data_start;
    left = 0;
    right = this->sample_count - 1;
    found = 0;

    while (!found) {
      middle = (left + right) / 2;
      if ((start_pos >= this->sample_table[middle].sample_offset) &&
          (start_pos <= this->sample_table[middle].sample_offset + 
           this->sample_table[middle].sample_size)) {
        found = 1;
      } else if (start_pos < this->sample_table[middle].sample_offset) {
        right = middle;
      } else {
        left = middle;
      }
    }

    best_index = middle;
  }

  /* search back in the table for the nearest keyframe */
  while (best_index) {
    if ((this->sample_table[best_index].syncinfo1 & 0x80000000) == 0) {
      break;
    }
    best_index--;
  }

  /* not done yet; now that the nearest keyframe has been found, seek
   * back to the first audio frame that has a pts less than or equal to
   * that of the keyframe */
  this->waiting_for_keyframe = 1;
  keyframe_pts = this->sample_table[best_index].pts;
  while (best_index) {
    if ((this->sample_table[best_index].syncinfo1 == 0xFFFFFFFF) &&
        (this->sample_table[best_index].pts < keyframe_pts)) {
      break;
    }
    best_index--;
  }

  this->current_sample = best_index;
  this->status = DEMUX_OK;
  xine_demux_flush_engine(this->stream);
  
  if( !this->stream->demux_thread_running ) {
    this->waiting_for_keyframe = 0;

    this->last_sample = 0;
  }
    
  return this->status;
}

static void demux_film_dispose (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  if (this->sample_table)
    free(this->sample_table);
  free(this);
}

static int demux_film_get_status (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  return this->status;
}

static int demux_film_get_stream_length (demux_plugin_t *this_gen) {
  demux_film_t *this = (demux_film_t *) this_gen;

  return this->total_time;
}

static uint32_t demux_film_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_film_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input_gen) {

  input_plugin_t *input = (input_plugin_t *) input_gen;
  demux_film_t    *this;

  if (! (input->get_capabilities(input) & INPUT_CAP_SEEKABLE)) {
    printf(_("demux_film.c: input not seekable, can not handle!\n"));
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_film_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_film_send_headers;
  this->demux_plugin.send_chunk        = demux_film_send_chunk;
  this->demux_plugin.seek              = demux_film_seek;
  this->demux_plugin.dispose           = demux_film_dispose;
  this->demux_plugin.get_status        = demux_film_get_status;
  this->demux_plugin.get_stream_length = demux_film_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_film_get_capabilities;
  this->demux_plugin.get_optional_data = demux_film_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_film_file(this)) {
      free (this);
      return NULL;
    }

  break;

  case METHOD_BY_EXTENSION: {
    char *ending, *mrl;

    mrl = input->get_mrl (input);

    ending = strrchr(mrl, '.');

    if (!ending) {
      free (this);
      return NULL;
    }

    if (strncasecmp (ending, ".cpk", 4) &&
        strncasecmp (ending, ".cak", 4) &&
        strncasecmp (ending, ".film", 5)) {
      free (this);
      return NULL;
    }

    if (!open_film_file(this)) {
      free (this);
      return NULL;
    }

  }

  break;

  default:
    free (this);
    return NULL;
  }

  strncpy (this->last_mrl, input->get_mrl (input), 1024);

  /* print vital stats */
  xine_log (this->stream->xine, XINE_LOG_MSG,
    _("demux_film: FILM version %c%c%c%c, running time: %d min, %d sec\n"),
    this->version[0],
    this->version[1],
    this->version[2],
    this->version[3],
    this->total_time / 60,
    this->total_time % 60);
  if (this->video_type)
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("demux_film: %c%c%c%c video @ %dx%d, %d Hz playback clock\n"),
      (this->video_codec >> 24) & 0xFF,
      (this->video_codec >> 16) & 0xFF,
      (this->video_codec >>  8) & 0xFF,
      (this->video_codec >>  0) & 0xFF,
      this->bih.biWidth,
      this->bih.biHeight,
      this->frequency);
  if (this->audio_type)
    xine_log (this->stream->xine, XINE_LOG_MSG,
      _("demux_film: %d Hz, %d-bit %s%s PCM audio\n"),
      this->sample_rate,
      this->audio_bits,
      (this->audio_bits == 16) ? "big-endian " : "",
      (this->audio_channels == 1) ? "mono" : "stereo");

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "FILM (CPK) demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "FILM (CPK)";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "cpk cak film";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {

  demux_film_class_t *this = (demux_film_class_t *) this_gen;

  free (this);
}

void *demux_film_init_plugin (xine_t *xine, void *data) {

  demux_film_class_t     *this;

  this         = xine_xmalloc (sizeof (demux_film_class_t));
  this->config = xine->config;
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}
