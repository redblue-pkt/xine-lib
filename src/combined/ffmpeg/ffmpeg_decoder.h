/*
 * Copyright (C) 2001-2020 the xine project
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */

#ifndef HAVE_XINE_DECODER_H
#define HAVE_XINE_DECODER_H

#include <sys/types.h>

#include <xine.h>

#if defined LIBAVCODEC_VERSION_INT

typedef struct ff_codec_s {
  uint32_t          type;
# if LIBAVCODEC_VERSION_INT >= ((54<<16)|(25<<8))
  enum AVCodecID    id;
# else
  enum CodecID      id;
# endif
  const char       *name;
} ff_codec_t;

extern const ff_codec_t ff_audio_lookup[];
extern const ff_codec_t ff_video_lookup[];
extern const size_t ff_video_lookup_entries;
extern const size_t ff_audio_lookup_entries;

#endif

void *init_audio_plugin (xine_t *xine, const void *data);
void *init_video_plugin (xine_t *xine, const void *data);
void *init_avio_input_plugin (xine_t *xine, const void *data);
void *init_avformat_input_plugin (xine_t *xine, const void *data);
void *init_avformat_demux_plugin (xine_t *xine, const void *data);

/* communication between avio/avformat input and avformat demux plugins */
#define INPUT_OPTIONAL_DATA_pb         0x1000
#define INPUT_OPTIONAL_DATA_fmt_ctx    0x1001

/* plugin ids */
#define INPUT_AVIO_ID     "avio"
#define DEMUX_AVFORMAT_ID "avformat"

void init_once_routine(void);

extern pthread_mutex_t ffmpeg_lock;

#endif
