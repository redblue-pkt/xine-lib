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
 * $Id: xine_decoder.h,v 1.2 2004/07/31 18:57:45 valtri Exp $
 *
 */
 
#ifndef HAVE_XINE_DECODER_H
#define HAVE_XINE_DECODER_H

#ifdef _MSC_VER
/* ffmpeg has own definitions of those types */
#  undef int8_t
#  undef uint8_t
#  undef int16_t
#  undef uint16_t
#  undef int32_t
#  undef uint32_t
#  undef int64_t
#  undef uint64_t
#endif

#include <avcodec.h>

#ifdef _MSC_VER
#  undef malloc
#  undef free
#  undef realloc
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
