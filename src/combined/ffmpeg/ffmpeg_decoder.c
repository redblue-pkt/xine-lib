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
 *
 * xine decoder plugin using ffmpeg
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>

#include <xine/xine_internal.h>
#include <xine/xine_plugin.h>

#include "ffmpeg_decoder.h"

#ifdef HAVE_AVFORMAT
#include <libavformat/avformat.h> // av_register_all()
#endif

#include "ffmpeg_compat.h"

#include "ff_audio_list.h"
#include "ff_video_list.h"

/*
 * common initialisation
 */

pthread_mutex_t ffmpeg_lock;

static void _init_once_routine(void) {
  pthread_mutex_init(&ffmpeg_lock, NULL);
  XFF_AVCODEC_INIT();
  XFF_AVCODEC_REGISTER_ALL();

#ifdef HAVE_AVFORMAT
# if !defined(LIBAVFORMAT_VERSION_INT) || LIBAVFORMAT_VERSION_INT < XFF_INT_VERSION(58,9,100)
  av_register_all();
# endif
  avformat_network_init();
#endif
}

void init_once_routine(void) {
  static pthread_once_t once_control = PTHREAD_ONCE_INIT;

  pthread_once( &once_control, _init_once_routine );
}

/*
 * exported plugin catalog entry
 */

#ifdef HAVE_AVFORMAT
static const input_info_t input_info_avio = {
  .priority = -1,
};

static const input_info_t input_info_avformat = {
  .priority = -2,
};

static const demuxer_info_t demux_info_avformat = {
  .priority = -1,
};
#endif /* HAVE_AVFORMAT */

static const decoder_info_t dec_info_ffmpeg_audio = {
  .supported_types = supported_audio_types,
  .priority = 7,
};

static const uint32_t wmv8_video_types[] = {
  BUF_VIDEO_WMV8,
  0
};

static const decoder_info_t dec_info_ffmpeg_wmv8 = {
  .supported_types = wmv8_video_types,
  .priority = 0,
};

static const uint32_t wmv9_video_types[] = {
  BUF_VIDEO_WMV9,
  0
};

static const decoder_info_t dec_info_ffmpeg_wmv9 = {
  .supported_types = wmv9_video_types,
  .priority = 0,
};

static const decoder_info_t dec_info_ffmpeg_video = {
  .supported_types = supported_video_types,
  .priority = 6,
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_DECODER | PLUGIN_MUST_PRELOAD, 19, "ffmpegvideo", XINE_VERSION_CODE, &dec_info_ffmpeg_video, init_video_plugin },
  { PLUGIN_VIDEO_DECODER, 19, "ffmpeg-wmv8", XINE_VERSION_CODE, &dec_info_ffmpeg_wmv8, init_video_plugin },
  { PLUGIN_VIDEO_DECODER, 19, "ffmpeg-wmv9", XINE_VERSION_CODE, &dec_info_ffmpeg_wmv9, init_video_plugin },
  { PLUGIN_AUDIO_DECODER, 16, "ffmpegaudio", XINE_VERSION_CODE, &dec_info_ffmpeg_audio, init_audio_plugin },
#ifdef HAVE_AVFORMAT
  { PLUGIN_INPUT,         18, INPUT_AVIO_ID,     XINE_VERSION_CODE, &input_info_avio,     init_avio_input_plugin },
  { PLUGIN_INPUT,         18, DEMUX_AVFORMAT_ID, XINE_VERSION_CODE, &input_info_avformat, init_avformat_input_plugin },
  { PLUGIN_DEMUX,         27, DEMUX_AVFORMAT_ID, XINE_VERSION_CODE, &demux_info_avformat, init_avformat_demux_plugin },
#endif
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
