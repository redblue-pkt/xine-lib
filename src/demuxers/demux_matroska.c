/*
 * Copyright (C) 2000-2003 the xine project
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
 * $Id: demux_matroska.c,v 1.5 2004/01/09 01:26:33 miguelfreitas Exp $
 *
 * demultiplexer for matroska streams
 *
 * TODO:
 *   memory leaks
 *   seeking
 *   subtitles
 *   more codecs
 *   metadata
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MODULE "demux_matroska"
#define LOG_VERBOSE
/*
#define LOG
*/
#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "bswap.h"

#include "ebml.h"
#include "matroska.h"

#define NUM_PREVIEW_BUFFERS      10

#define MAX_STREAMS             128
#define MAX_FRAMES               32

#define WRAP_THRESHOLD        90000

typedef struct {

  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;

  input_plugin_t      *input;

  int                  status;

  ebml_parser_t       *ebml;

  /* segment element */
  ebml_elem_t          segment;
  uint64_t             timecode_scale;
  int                  duration;            /* in millis */
  int                  preview_sent;
  int                  preview_mode;

  /* meta seek info */
  off_t                seekhead_pos;
  off_t                info_pos;
  off_t                tracks_pos;
  off_t                chapters_pos;
  off_t                cues_pos;
  off_t                attachments_pos;
  off_t                tags_pos;
  int                  has_seekhead;
  int                  seekhead_handled;

  /* tracks */
  int                  num_tracks;
  matroska_track_t    *tracks[MAX_STREAMS];

  /* block */
  uint8_t             *block_data;
  int                  block_data_size;

  /* current tracks */
  matroska_track_t    *video_track;
  matroska_track_t    *audio_track;
  matroska_track_t    *sub_track;

  int                  send_newpts;
  int                  buf_flag_seek;
} demux_matroska_t ;

typedef struct {

  demux_class_t     demux_class;

  /* class-wide, global variables here */

  xine_t           *xine;

} demux_matroska_class_t;


static void check_newpts (demux_matroska_t *this, int64_t pts,
                          matroska_track_t *track) {
  int64_t diff;

  diff = pts - track->last_pts;

  if (pts && (this->send_newpts || (track->last_pts && abs(diff)>WRAP_THRESHOLD)) ) {
    int i;

    lprintf ("sending newpts %lld\n", pts);

    if (this->buf_flag_seek) {
      _x_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      _x_demux_control_newpts(this->stream, pts, 0);
    }

    this->send_newpts = 0;
    for (i = 0; i < this->num_tracks; i++) {
      this->tracks[i]->last_pts = 0;
    }
  }

  if (pts)
    track->last_pts = pts;

}


static int parse_info(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;
  double duration = 0.0; /* in matroska unit */
  
  while (next_level == 2) {
    ebml_elem_t elem;
    
    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_I_TIMECODESCALE:
        lprintf("timecode_scale\n");
        if (!ebml_read_uint(ebml, &elem, &this->timecode_scale))
          return 0;
        break;
      case MATROSKA_ID_I_DURATION: {
        
        lprintf("duration\n");
        if (!ebml_read_float(ebml, &elem, &duration))
          return 0;
      }
      break;
      
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  if (this->timecode_scale == 0) {
    this->timecode_scale = 1000000;
  }
  this->duration = (int)(duration * (double)this->timecode_scale / 1000000.0);
  lprintf("timecode_scale: %lld\n", this->timecode_scale);
  lprintf("duration: %d\n", this->duration);
  return 1;
}


static int parse_video_track (demux_matroska_t *this, matroska_video_track_t *vt) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 4;
  uint64_t val;

  while (next_level == 4) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_TV_FLAGINTERLACED:
        lprintf("MATROSKA_ID_TV_FLAGINTERLACED\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        vt->flag_interlaced = val;
        break;
      case MATROSKA_ID_TV_PIXELWIDTH:
        lprintf("MATROSKA_ID_TV_PIXELWIDTH\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        vt->pixel_witdh = val;
        break;
      case MATROSKA_ID_TV_PIXELHEIGHT:
        lprintf("MATROSKA_ID_TV_PIXELHEIGHT\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        vt->pixel_height = val;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_audio_track (demux_matroska_t *this, matroska_audio_track_t *at) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 4;

  while (next_level == 4) {
    ebml_elem_t elem;
    uint64_t    val;
    double      fval;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_TA_SAMPLINGFREQUENCY:
        lprintf("MATROSKA_ID_TA_SAMPLINGFREQUENCY\n");
        if (!ebml_read_float(ebml, &elem, &fval))
          return 0;
        at->sampling_freq = (int)fval;
        break;
      case MATROSKA_ID_TA_OUTPUTSAMPLINGFREQUENCY:
        lprintf("MATROSKA_ID_TA_OUTPUTSAMPLINGFREQUENCY\n");
        if (!ebml_read_float(ebml, &elem, &fval))
          return 0;
        at->output_sampling_freq = (int)fval;
        break;
      case MATROSKA_ID_TA_CHANNELS:
        lprintf("MATROSKA_ID_TA_CHANNELS\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        at->channels = val;
        break;
      case MATROSKA_ID_TA_BITDEPTH:
        lprintf("MATROSKA_ID_TA_BITDEPTH\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        at->bits_per_sample = val;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static void init_codec_video(demux_matroska_t *this, matroska_track_t *track) {
  buf_element_t *buf;

  buf = track->fifo->buffer_pool_alloc (track->fifo);
  buf->type = track->buf_type;

  if (track->codec_private_len > buf->max_size) {
    buf->size = buf->max_size;
  } else {
    buf->size = track->codec_private_len;
  }

  if (buf->size)
    xine_fast_memcpy (buf->content, track->codec_private, buf->size);
  else
    buf->content = NULL;

  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_FRAME_END;
  buf->type          = track->buf_type;
  buf->pts           = 0;
  track->fifo->put (track->fifo, buf);
}


static void init_codec_audio(demux_matroska_t *this, matroska_track_t *track) {
  buf_element_t *buf;

  buf = track->fifo->buffer_pool_alloc (track->fifo);

  /* default param */
  buf->decoder_info[0] = 0;
  buf->decoder_info[1] = 44100;
  buf->decoder_info[2] = 16;
  buf->decoder_info[3] = 2;
  /* track param */
  if (track->audio_track) {
    if (track->audio_track->sampling_freq)
      buf->decoder_info[1] = track->audio_track->sampling_freq;
    if (track->audio_track->bits_per_sample)
      buf->decoder_info[2] = track->audio_track->bits_per_sample;
    if (track->audio_track->channels)
      buf->decoder_info[3] = track->audio_track->channels;
  }
  lprintf("%d Hz, %d bits, %d channels\n", buf->decoder_info[1],
          buf->decoder_info[2], buf->decoder_info[3]);

  if (track->codec_private_len > buf->max_size) {
    buf->size = buf->max_size;
  } else {
    buf->size = track->codec_private_len;
  }

  if (buf->size)
    xine_fast_memcpy (buf->content, track->codec_private, buf->size);
  else
    buf->content = NULL;

  buf->decoder_flags = BUF_FLAG_HEADER|BUF_FLAG_FRAME_END;
  buf->type          = track->buf_type;
  buf->pts           = 0;
  track->fifo->put (track->fifo, buf);
}


static void init_codec_vorbis(demux_matroska_t *this, matroska_track_t *track) {
  buf_element_t *buf;
  uint8_t nb_lace;
  int frame[3];
  int i;
  uint8_t *data;

  nb_lace = track->codec_private[0];
  if (nb_lace != 2)
    return;

  frame[0] = track->codec_private[1];
  frame[1] = track->codec_private[2];
  frame[2] = track->codec_private_len - frame[0] - frame[1] - 3;

  data = track->codec_private + 3;
  for (i = 0; i < 3; i++) {
    buf = track->fifo->buffer_pool_alloc (track->fifo);
    buf->decoder_flags = BUF_FLAG_PREVIEW;
    buf->type          = track->buf_type;
    buf->pts           = 0;
    buf->size          = frame[i];

    xine_fast_memcpy (buf->content, data, buf->size);
    data += buf->size;

    track->fifo->put (track->fifo, buf);
  }
}


static int parse_track_entry(demux_matroska_t *this, matroska_track_t *track) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 3;
  
  while (next_level == 3) {
    ebml_elem_t elem;
    
    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_TR_NUMBER: {
        uint64_t num;
        lprintf("TrackNumber\n");
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        track->track_num = num;
      }
      break;
      
      case MATROSKA_ID_TR_TYPE: {
        uint64_t num;
        lprintf("TrackType\n");
        if (!ebml_read_uint(ebml, &elem, &num))
          return 0;
        track->track_type = num;
      }
      break;
      
      case MATROSKA_ID_TR_CODECID: {
        char *codec_id = malloc (elem.len + 1);
        lprintf("CodecID\n");
        if (!ebml_read_ascii(ebml, &elem, codec_id))
          return 0;
        codec_id[elem.len] = '\0';
        track->codec_id = codec_id;
      }
      break;
        
      case MATROSKA_ID_TR_CODECPRIVATE: {
        char *codec_private = malloc (elem.len);
        lprintf("CodecPrivate\n");
        if (!ebml_read_binary(ebml, &elem, codec_private))
          return 0;
        track->codec_private = codec_private;
        track->codec_private_len = elem.len;
      }
      break;
        
      case MATROSKA_ID_TR_LANGUAGE: {
        char *language = malloc (elem.len + 1);
        lprintf("Language\n");
        if (!ebml_read_ascii(ebml, &elem, language))
          return 0;
        language[elem.len] = '\0';
        track->language = language;
      }
      break;
      
      case MATROSKA_ID_TV:
        lprintf("Video\n");
        if (track->video_track)
          return 1;
        track->video_track = (matroska_video_track_t *)xine_xmalloc(sizeof(matroska_video_track_t));
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if (!parse_video_track(this, track->video_track))
          return 0;
      break;
      
      case MATROSKA_ID_TA:
        lprintf("Audio\n");
        if (track->audio_track)
          return 1;
        track->audio_track = (matroska_audio_track_t *)xine_xmalloc(sizeof(matroska_audio_track_t));
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if (!parse_audio_track(this, track->audio_track))
          return 0;
      break;
        
      case MATROSKA_ID_TR_FLAGDEFAULT: {
        uint64_t val;
        
        lprintf("Default\n");
        if (!ebml_read_uint(ebml, &elem, &val))
          return 0;
        track->default_flag = (int)val;
      }
      break;

      case MATROSKA_ID_TR_UID:
      case MATROSKA_ID_TR_FLAGENABLED:
      case MATROSKA_ID_TR_FLAGLACING:
      case MATROSKA_ID_TR_MINCACHE:
      case MATROSKA_ID_TR_MAXCACHE:
      case MATROSKA_ID_TR_DEFAULTDURATION:
      case MATROSKA_ID_TR_TIMECODESCALE:
      case MATROSKA_ID_TR_NAME:
      case MATROSKA_ID_TR_CODECNAME:
      case MATROSKA_ID_TR_CODECSETTINGS:
      case MATROSKA_ID_TR_CODECINFOURL:
      case MATROSKA_ID_TR_CODECDOWNLOADURL:
      case MATROSKA_ID_TR_CODECDECODEALL:
      case MATROSKA_ID_TR_OVERLAY:
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem)) {
          return 0;
        }
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }

  switch(track->track_type) {
    case MATROSKA_TRACK_VIDEO:
      if (!this->video_track) {
        this->video_track = track;
        track->fifo = this->stream->video_fifo;
      }
      break;
    case MATROSKA_TRACK_AUDIO:
      if (!this->audio_track) {
        this->audio_track = track;
        track->fifo = this->stream->audio_fifo;
      }
      break;
    case MATROSKA_TRACK_SUBTITLE:
      if (!this->sub_track) {
        this->sub_track = track;
        track->fifo = this->stream->video_fifo;
      }
      break;
    case MATROSKA_TRACK_COMPLEX:
    case MATROSKA_TRACK_LOGO:
    case MATROSKA_TRACK_CONTROL:
      break;
  }
  
  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
          "demux_matroska: Track %d, %s %s\n",
          track->track_num,
          (track->codec_id ? track->codec_id : ""),
          (track->language ? track->language : ""));
  if (track->codec_id && track->fifo) {
    int init_mode = 0;;

    if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_VFW_FOURCC)) {
      xine_bmiheader *bih;

      lprintf("MATROSKA_CODEC_ID_V_VFW_FOURCC\n");
      bih = (xine_bmiheader*)track->codec_private;
      _x_bmiheader_le2me(bih);

      track->buf_type = _x_fourcc_to_buf_video(bih->biCompression);

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_UNCOMPRESSED)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG4_SP)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG4_ASP)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG4_AP)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MSMPEG4V3)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG1)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MPEG2)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_REAL_RV10)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_REAL_RV20)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_REAL_RV30)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_REAL_RV40)) {
    
      lprintf("MATROSKA_CODEC_ID_V_REAL_RV40\n");
      /* track->buf_type = BUF_VIDEO_RV40; */

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_V_MJPEG)) {
    } else if ((!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_MPEG1_L1)) ||
               (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_MPEG1_L2)) ||
               (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_MPEG1_L3))) {
      lprintf("MATROSKA_CODEC_ID_A_MPEG1\n");
      track->buf_type = BUF_AUDIO_MPEG;

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_PCM_INT_BE)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_PCM_INT_LE)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_PCM_FLOAT)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_AC3)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_DTS)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_VORBIS)) {

      lprintf("MATROSKA_CODEC_ID_A_VORBIS\n");
      track->buf_type = BUF_AUDIO_VORBIS;
      init_mode = 2;

    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_ACM)) {
    } else if (!strncmp(track->codec_id, MATROSKA_CODEC_ID_A_AAC,
                        sizeof(MATROSKA_CODEC_ID_A_AAC - 1))) {
      lprintf("MATROSKA_CODEC_ID_A_AAC\n");
      track->buf_type = BUF_AUDIO_AAC;
      init_mode = 1;
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_14_4)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_28_8)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_COOK)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_SIPR)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_RALF)) {
    } else if (!strcmp(track->codec_id, MATROSKA_CODEC_ID_A_REAL_ATRC)) {
    } else {
      lprintf("unknown codec\n");
    }

    if (track->buf_type) {
      switch (init_mode) {
        case 0:
          init_codec_video(this, track);
        break;

        case 1:
          init_codec_audio(this, track);
        break;

        case 2:
          init_codec_vorbis(this, track);
        break;
      }
    }
  }
  
  return 1;
}


static int parse_tracks(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;
  
  while (next_level == 2) {
    ebml_elem_t elem;
    
    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_TR_ENTRY: {
        this->tracks[this->num_tracks] = xine_xmalloc(sizeof(matroska_track_t));
        lprintf("TrackEntry\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if (!parse_track_entry(this, this->tracks[this->num_tracks]))
          return 0;
        this->num_tracks++;
      }
      break;
      
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_chapters(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;
  
  while (next_level == 2) {
    ebml_elem_t elem;
    
    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_cues(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;
  
  while (next_level == 2) {
    ebml_elem_t elem;
    
    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_attachments(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;
  
  while (next_level == 2) {
    ebml_elem_t elem;
    
    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_tags(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;
  
  while (next_level == 2) {
    ebml_elem_t elem;
    
    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_ebml_uint(demux_matroska_t *this, uint8_t *data, uint64_t *num) {
  uint8_t mask = 0x80;
  int size = 1;
  int i;

  /* compute the size of the "data len" (1-8 bytes) */
  while (size <= 8 && !(data[0] & mask)) {
    size++;
    mask >>= 1;
  }
  if (size > 8) {
    off_t pos = this->input->get_current_pos(this->input);
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: Invalid Track Number at position %llu\n", pos);
    return 0;
  }

  *num = data[0];
  *num &= mask - 1;

  for (i = 1; i < size; i++) {
    *num = (*num << 8) | data[i];
  }
  return size;
}


static int parse_ebml_sint(demux_matroska_t *this, uint8_t *data, int64_t *num) {
  uint64_t unum;
  int size;

  size = parse_ebml_uint(this, data, &unum);
  if (!size)
    return 0;

  /* formula taken from gstreamer demuxer */
  if (unum == -1)
    *num = -1;
  else
    *num = unum - ((1 << ((7 * size) - 1)) - 1);
  
  return size;
}

static int find_track_by_id(demux_matroska_t *this, int track_num,
                            matroska_track_t **track) {
  int i;

  *track = NULL;
  for (i = 0; i < this->num_tracks; i++) {
    if (this->tracks[i]->track_num == track_num) {
      *track = this->tracks[i];
      return 1;
    }
  }
  return 0;
}

static int read_block_data (demux_matroska_t *this, int len) {
  /* memory management */
  if (this->block_data_size < len) {
    if (this->block_data)
      this->block_data = realloc(this->block_data, len);
    else
      this->block_data = malloc(len);
    this->block_data_size = len;
  }

  /* block datas */
  if (this->input->read(this->input, this->block_data, len) != len) {
    off_t pos = this->input->get_current_pos(this->input);
    xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
            "demux_matroska: read error at position %llu\n", pos);
    return 0;
  }
  return 1;
}

static int parse_block (demux_matroska_t *this, uint64_t block_size,
                        uint64_t timecode, uint64_t duration) {
  matroska_track_t *track;
  int64_t           track_num;
  int               timecode_diff;
  uint8_t          *data;
  uint8_t           flags;
  int               gap;
  int               lacing;
  int               num_len;
  
  if (!read_block_data(this, block_size))
    return 0;

  data = this->block_data;
  if (!(num_len = parse_ebml_uint(this, data, &track_num)))
    return 0;
  data += num_len;
    
  timecode_diff = (int)BE_16(data);
  data += 2;

  flags = *data;
  data += 1;
  
  lprintf("track_num: %lld, timecode_diff: %d, flags: 0x%x\n", track_num, timecode_diff, flags);

  gap = flags & 1;
  lacing = (flags >> 1) & 0x3;

  if (!find_track_by_id(this, (int)track_num, &track)) {
     xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
             "demux_matroska: invalid track id: %lld\n", track_num);
     return 0;
  }

  if ((track == this->video_track) ||
      (track == this->audio_track) ||
      (track == this->sub_track)) {
    int64_t pts;
    int decoder_flags = 0;
    off_t input_pos, input_len;

    pts = ((int64_t)timecode + timecode_diff) *
          (int64_t)this->timecode_scale * (int64_t)90 /
          (int64_t)1000000;
    lprintf("pts: %lld\n", pts);

    check_newpts(this, pts, track);

    if (this->preview_mode) {
      this->preview_sent++;
      decoder_flags |= BUF_FLAG_PREVIEW;
    }
    
    input_pos = this->input->get_current_pos(this->input);
    input_len = this->input->get_length(this->input);
    
    if (lacing == MATROSKA_NO_LACING) {
      int block_size_left;
      lprintf("no lacing\n");

      block_size_left = (this->block_data + block_size) - data;
      lprintf("size: %d, block_size: %lld\n", block_size_left, block_size);
      _x_demux_send_data(track->fifo, data, block_size_left,
                         pts, track->buf_type, decoder_flags,
                         input_pos, input_len, pts / 90,
                         this->duration, 0);
    } else {
    
      int block_size_left;
      uint8_t lace_num;
      int frame[MAX_FRAMES];
      int i;

      /* number of laced frames */
      lace_num = *data;
      data++;
      lprintf("lace_num: %d\n", lace_num);
      if ((lace_num + 1) > MAX_FRAMES) {
        xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                "demux_matroska: too many frames: %d\n", lace_num);
        return 0;
      }
      block_size_left = this->block_data + block_size - data;

      switch (lacing) {
        case MATROSKA_XIPH_LACING: {

          lprintf("xiph lacing\n");

          /* size of each frame */
          for (i = 0; i < lace_num; i++) {
            int size = 0;
            while (*data == 255) {
              size += *data;
              data++; block_size_left--;
            }
            size += *data;
            data++; block_size_left--;
            frame[i] = size;
            block_size_left -= size;
          }

          /* last frame */
          frame[lace_num] = block_size_left;
        }
        break;

        case MATROSKA_FIXED_SIZE_LACING:
          lprintf("fixed size lacing\n");
          for (i = 0; i < lace_num; i++) {
            frame[i] = block_size / (lace_num + 1);
            block_size_left -= frame[i];
          }
          frame[lace_num] = block_size_left;
          break;

        case MATROSKA_EBML_LACING: {
          int64_t tmp;

          lprintf("ebml lacing\n");

          /* size of each frame */
          if (!(num_len = parse_ebml_uint(this, data, &tmp)))
            return 0;
          data += num_len; block_size_left -= num_len;
          frame[0] = (int) tmp;
          lprintf("first frame len: %d\n", frame[0]);
          block_size_left -= frame[0];

          for (i = 1; i < lace_num; i++) {
            if (!(num_len = parse_ebml_sint(this, data, &tmp)))
              return 0;

            data += num_len; block_size_left -= num_len;
            frame[i] = frame[i-1] + tmp;
            block_size_left -= frame[i];
          }

          /* last frame */
          frame[lace_num] = block_size_left;
        }
        break;
        default:
          xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
                  "demux_matroska: invalid lacing: %d\n", lacing);
          return 0;
      }
      /* send each frame to the decoder */
      for (i = 0; i <= lace_num; i++) {
        _x_demux_send_data(track->fifo, data, frame[i],
                           pts, track->buf_type, decoder_flags,
                           input_pos, input_len, pts / 90,
                           0, 0);
        data += frame[i];
        pts = 0;
      }
    }
  }
  return 1;
}

static int parse_block_group(demux_matroska_t *this,
                             uint64_t timecode, uint64_t duration) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 3;

  while (next_level == 3) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CL_BLOCK:
        lprintf("block\n");
        if (!parse_block(this, elem.len, timecode, duration))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}

static int parse_cluster(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;
  uint64_t timecode = 0;
  uint64_t duration = 0;

  while (next_level == 2) {
    ebml_elem_t elem;

    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_CL_TIMECODE:
        lprintf("timecode\n");
        if (!ebml_read_uint(ebml, &elem, &timecode))
          return 0;
        break;
      case MATROSKA_ID_CL_DURATION:
        lprintf("duration\n");
        if (!ebml_read_uint(ebml, &elem, &duration))
          return 0;
        break;
      case MATROSKA_ID_CL_BLOCKGROUP:
        lprintf("blockgroup\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if (!parse_block_group(this, timecode, duration))
          return 0;
        break;
      case MATROSKA_ID_CL_BLOCK:
        lprintf("block\n");
        if (!ebml_skip(ebml, &elem))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_seek_entry(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 3;
  int has_id = 0;
  int has_position = 0;
  uint64_t id, pos;
  
  while (next_level == 3) {
    ebml_elem_t elem;
    
    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_S_ID:
        lprintf("SeekID\n");
        if (!ebml_read_uint(ebml, &elem, &id))
          return 0;
        has_id = 1;
        break;
      case MATROSKA_ID_S_POSITION:
        lprintf("SeekPosition\n");
        if (!ebml_read_uint(ebml, &elem, &pos))
          return 0;
        has_position = 1;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  if (has_id && has_position) {
    
    switch (id) {
      case MATROSKA_ID_INFO:
        lprintf("Seek Entry: Info: %lld\n", pos);
        this->info_pos = this->segment.start + pos;
        break;
      case MATROSKA_ID_SEEKHEAD:
        lprintf("Seek Entry: SeekHead: %lld\n", pos);
        this->seekhead_pos = this->segment.start + pos;
        break;
      case MATROSKA_ID_CLUSTER:
        lprintf("Seek Entry: Cluster: %lld\n", pos);
        break;
      case MATROSKA_ID_TRACKS:
        lprintf("Seek Entry: Tracks: %lld\n", pos);
        this->tracks_pos = this->segment.start + pos;
        break;
      case MATROSKA_ID_CUES:
        lprintf("Seek Entry: Cues: %lld\n", pos);
        this->cues_pos = this->segment.start + pos;
        break;
      case MATROSKA_ID_ATTACHMENTS:
        lprintf("Seek Entry: Attachements: %lld\n", pos);
        this->attachments_pos = this->segment.start + pos;
        break;
      case MATROSKA_ID_CHAPTERS:
        lprintf("Seek Entry: Chapters: %lld\n", pos);
        this->chapters_pos = this->segment.start + pos;
        break;
      default:
        lprintf("Unhandled Seek Entry ID: 0x%llx\n", id);
    }
    return 1;
  } else {
    lprintf("incomplete Seek Entry\n");
    return 1;
  }
}


static int parse_seekhead(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;
  int next_level = 2;
  
  this->has_seekhead = 1;
  
  while (next_level == 2) {
    ebml_elem_t elem;
    
    if (!ebml_read_elem_head(ebml, &elem))
      return 0;

    switch (elem.id) {
      case MATROSKA_ID_S_ENTRY:
        lprintf("Seek\n");
        if (!ebml_read_master (ebml, &elem))
          return 0;
        if (!parse_seek_entry(this))
          return 0;
        break;
      default:
        lprintf("Unhandled ID: 0x%x\n", elem.id);
        if (!ebml_skip(ebml, &elem))
          return 0;
    }
    next_level = ebml_get_next_level(ebml, &elem);
  }
  return 1;
}


static int parse_top_level_head(demux_matroska_t *this, int *next_level) {
  ebml_parser_t *ebml = this->ebml;
  ebml_elem_t elem;

  if (!ebml_read_elem_head(ebml, &elem))
    return 0;

  switch (elem.id) {
    case MATROSKA_ID_SEEKHEAD:
      lprintf("SeekHead\n");
      if (!ebml_read_master (ebml, &elem))
        return 0;
      if (!parse_seekhead(this))
        return 0;
      this->has_seekhead = 1;
      break;
    case MATROSKA_ID_INFO:
      lprintf("Info\n");
      if (!ebml_read_master (ebml, &elem))
        return 0;
      if (!parse_info(this))
        return 0;
      break;
    case MATROSKA_ID_TRACKS:
      lprintf("Tracks\n");
      if (!ebml_read_master (ebml, &elem))
        return 0;
      if (!parse_tracks(this))
        return 0;
      break;
    case MATROSKA_ID_CHAPTERS:
      lprintf("Chapters\n");
      if (!ebml_read_master (ebml, &elem))
        return 0;
      if (!parse_chapters(this))
        return 0;
      break;
    case MATROSKA_ID_CLUSTER:
      lprintf("Cluster\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_CUES:
      lprintf("Cues\n");
      if (!ebml_read_master (ebml, &elem))
        return 0;
      if (!parse_cues(this))
        return 0;
      break;
    case MATROSKA_ID_ATTACHMENTS:
      lprintf("Attachments\n");
      if (!ebml_read_master (ebml, &elem))
        return 0;
      if (!parse_attachments(this))
        return 0;
      break;
    case MATROSKA_ID_TAGS:
      lprintf("Tags\n");
      if (!ebml_read_master (ebml, &elem))
        return 0;
      if (!parse_tags(this))
        return 0;
      break;
      
    default:
      lprintf("Unhandled ID: 0x%x\n", elem.id);
      if (!ebml_skip(ebml, &elem))
        return 0;
  }
  if (next_level)
    *next_level = ebml_get_next_level(ebml, &elem);
  return 1;
}

static int parse_top_level(demux_matroska_t *this, int *next_level) {
  ebml_parser_t *ebml = this->ebml;
  ebml_elem_t elem;

  if (!ebml_read_elem_head(ebml, &elem))
    return 0;

  switch (elem.id) {
    case MATROSKA_ID_SEEKHEAD:
      lprintf("SeekHead\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      this->has_seekhead = 1;
      break;
    case MATROSKA_ID_INFO:
      lprintf("Info\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_TRACKS:
      lprintf("Tracks\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_CHAPTERS:
      lprintf("Chapters\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_CLUSTER:
      lprintf("Cluster\n");
      if (!ebml_read_master (ebml, &elem))
        return 0;
      if (!parse_cluster(this))
        return 0;
      break;
    case MATROSKA_ID_CUES:
      lprintf("Cues\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_ATTACHMENTS:
      lprintf("Attachments\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;
    case MATROSKA_ID_TAGS:
      lprintf("Tags\n");
      if (!ebml_skip(ebml, &elem))
        return 0;
      break;

    default:
      lprintf("Unhandled ID: 0x%x\n", elem.id);
      if (!ebml_skip(ebml, &elem))
        return 0;
  }
  if (next_level)
    *next_level = ebml_get_next_level(ebml, &elem);
  return 1;
}


static int parse_segment(demux_matroska_t *this) {
  ebml_parser_t *ebml = this->ebml;

  /* check segment id */
  if (!ebml_read_elem_head(ebml, &this->segment))
    return 0;

  if (this->segment.id == MATROSKA_ID_SEGMENT) {
    int next_level;
    
    lprintf("Segment\n");

    if (!ebml_read_master (ebml, &this->segment))
      return 0;
    
    next_level = 1;
    while (next_level == 1) {
      if (!parse_top_level_head(this, &next_level))
        return 0;
#if 0
      if (this->has_seekhead && !this->seekhead_handled) {
        if (this->seekhead_pos) {
          if (this->input->seek(this->input, this->seekhead_pos, SEEK_SET) < 0)
            return 0;
          this->seekhead_pos = 0;
        } else {
          /* parse all top level elements except clusters */
          if (this->info_pos) {
            if (this->input->seek(this->input, this->info_pos, SEEK_SET) < 0)
              return 0;
            if (!parse_top_level(this, &next_level))
              return 0;
          }
          if (this->tracks_pos) {
            if (this->input->seek(this->input, this->tracks_pos, SEEK_SET) < 0)
              return 0;
            if (!parse_top_level(this, &next_level))
              return 0;
          }
          if (this->chapters_pos) {
            if (this->input->seek(this->input, this->chapters_pos, SEEK_SET) < 0)
              return 0;
            if (!parse_top_level(this, &next_level))
              return 0;
          }
          if (this->cues_pos) {
            if (this->input->seek(this->input, this->cues_pos, SEEK_SET) < 0)
              return 0;
            if (!parse_top_level(this, &next_level))
              return 0;
          }
          if (this->attachments_pos) {
            if (this->input->seek(this->input, this->attachments_pos, SEEK_SET) < 0)
              return 0;
            if (!parse_top_level(this, &next_level))
              return 0;
          }
          if (this->tags_pos) {
            if (this->input->seek(this->input, this->tags_pos, SEEK_SET) < 0)
              return 0;
            if (!parse_top_level(this, &next_level))
              return 0;
          }
          /* this->seekhead_handled = 1; */
          return 1;
        }
      }
#endif
    }
    return 1;
  } else {
    /* not a segment */
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
            "demux_matroska: invalid segment\n");
    return 0;
  }
}

static int demux_matroska_send_chunk (demux_plugin_t *this_gen) {

  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  int next_level;

  if (!parse_top_level(this, &next_level)) {
    this->status = DEMUX_FINISHED;
  }
  return this->status;
}


static int demux_matroska_get_status (demux_plugin_t *this_gen) {
  demux_matroska_t *this = (demux_matroska_t *) this_gen;

  return this->status;
}


static void demux_matroska_send_headers (demux_plugin_t *this_gen) {

  demux_matroska_t *this = (demux_matroska_t *) this_gen;
  int next_level;

  _x_demux_control_start (this->stream);

  if (!parse_segment(this))
    this->status = DEMUX_FINISHED;
  else
    this->status = DEMUX_OK;

  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_VIDEO, (this->video_track != NULL));
  _x_stream_info_set(this->stream, XINE_STREAM_INFO_HAS_AUDIO, (this->audio_track != NULL));

    
  /*
   * send preview buffers
   */
/*
  for (i = 0; i < NUM_PREVIEW_BUFFERS; i++) {
    if (!demux_mpgaudio_next (this, BUF_FLAG_PREVIEW)) {
      break;
    }
  }
    */

  /* enter in the segment */
  ebml_read_master (this->ebml, &this->segment);

  /* seek to the beginning of the segment */
  this->input->seek(this->input, this->segment.start, SEEK_SET);

  this->preview_sent = 0;
  this->preview_mode = 1;

  next_level = 1;
  while ((this->preview_sent < NUM_PREVIEW_BUFFERS) && (next_level == 1)) {
    if (!parse_top_level (this, &next_level)) {
      break;
    }
  }
  this->preview_mode = 0;

  /* seek to the beginning of the segment */
  this->input->seek(this->input, this->segment.start, SEEK_SET);
}


static int demux_matroska_seek (demux_plugin_t *this_gen,
                                off_t start_pos, int start_time, int playing) {

  demux_matroska_t *this = (demux_matroska_t *) this_gen;

  this->status = DEMUX_OK;

  return this->status;
}


static void demux_matroska_dispose (demux_plugin_t *this) {

  free (this);
}


static int demux_matroska_get_stream_length (demux_plugin_t *this_gen) {

  demux_matroska_t *this = (demux_matroska_t *) this_gen;

  return (int)this->duration;
}


static uint32_t demux_matroska_get_capabilities (demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}


static int demux_matroska_get_optional_data (demux_plugin_t *this_gen,
                                             void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
} 

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_matroska_t *this;
  ebml_parser_t    *ebml = NULL;
                                      
  lprintf("trying to open %s...\n", input->get_mrl(input));

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    if (!(input->get_capabilities (input) & INPUT_CAP_SEEKABLE))
      return NULL;
    input->seek(input, 0, SEEK_SET);
    ebml = new_ebml_parser(stream->xine, input);
    if (!ebml_check_header(ebml))
      return NULL;
  }
  break;

  case METHOD_BY_EXTENSION: {
    char *mrl = input->get_mrl(input);
    char *extensions;
    
    lprintf ("stage by extension %s\n", mrl);
    
    extensions = class_gen->get_extensions (class_gen);
    
    if (!_x_demux_check_extension (mrl, extensions))
      return NULL;
      
  }
  break;

  case METHOD_EXPLICIT:
  break;
  
  default:
    return NULL;
  }
  
  this = xine_xmalloc (sizeof (demux_matroska_t));

  this->demux_plugin.send_headers      = demux_matroska_send_headers;
  this->demux_plugin.send_chunk        = demux_matroska_send_chunk;
  this->demux_plugin.seek              = demux_matroska_seek;
  this->demux_plugin.dispose           = demux_matroska_dispose;
  this->demux_plugin.get_status        = demux_matroska_get_status;
  this->demux_plugin.get_stream_length = demux_matroska_get_stream_length;
  this->demux_plugin.get_capabilities  = demux_matroska_get_capabilities;
  this->demux_plugin.get_optional_data = demux_matroska_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;
  
  this->input      = input;
  this->status     = DEMUX_FINISHED;
  this->stream     = stream;
  
  if (!ebml) {
    ebml = new_ebml_parser(stream->xine, input);
    if (!ebml_check_header(ebml))
      goto error;
  }
  this->ebml = ebml;
  
  /* check header fields */
  if (ebml->max_id_len > 4)
    goto error;
  if (ebml->max_size_len > 8)
    goto error;
  if (strcmp(ebml->doctype, "matroska"))
    goto error;
  
  return &this->demux_plugin;
  
error:
  if (ebml)
    dispose_ebml_parser(ebml);
  free(this);
  return NULL;
}


/*
 * demux matroska class
 */

static char *get_description (demux_class_t *this_gen) {
  return "matroska demux plugin";
}


static char *get_identifier (demux_class_t *this_gen) {
  return "matroska";
}


static char *get_extensions (demux_class_t *this_gen) {
  return "mkv";
}


static char *get_mimetypes (demux_class_t *this_gen) {
  return "video/mkv: mkv: matroska;";
}


static void class_dispose (demux_class_t *this_gen) {

  demux_matroska_class_t *this = (demux_matroska_class_t *) this_gen;

  free (this);
}


static void *init_class (xine_t *xine, void *data) {
  
  demux_matroska_class_t     *this;
  
  this         = xine_xmalloc (sizeof (demux_matroska_class_t));
  this->xine   = xine;

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_DEMUX, 23, "matroska", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
