/*
 * Copyright (C) 2001-2004 the xine project
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
 * $Id: xine_decoder.h,v 1.4 2004/09/26 22:54:52 valtri Exp $
 *
 */
 
#ifndef HAVE_XINE_DECODER_H
#define HAVE_XINE_DECODER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_FFMPEG
#  include <avcodec.h>
#else
#  include "libavcodec/avcodec.h"
#endif

#ifdef _MSC_VER
#  undef malloc
#  undef free
#  undef realloc
#  undef printf
#  undef fprintf
#endif

typedef struct ff_codec_s {
  uint32_t          type;
  enum CodecID      id;
  const char       *name;
} ff_codec_t;

void *init_audio_plugin (xine_t *xine, void *data);
void *init_video_plugin (xine_t *xine, void *data);

extern decoder_info_t dec_info_ffmpeg_video;
extern decoder_info_t dec_info_ffmpeg_wmv8;
extern decoder_info_t dec_info_ffmpeg_audio;

extern pthread_once_t once_control;
void init_once_routine(void);

#endif
