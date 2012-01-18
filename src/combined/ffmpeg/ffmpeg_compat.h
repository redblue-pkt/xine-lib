/*
 * Copyright (C) 2000-2012 the xine project
 *
 * This file is part of xine, a unix video player.
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
 * Compability macros for various ffmpeg versions
 */

#ifndef XINE_AVCODEC_COMPAT_H
#define XINE_AVCODEC_COMPAT_H

#ifndef LIBAVCODEC_VERSION_MAJOR
#  error ffmpeg headers must be included first !
#endif


#if LIBAVCODEC_VERSION_MAJOR > 51
#  define bits_per_sample bits_per_coded_sample
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 32)
#  define pp_context	pp_context_t
#  define pp_mode	pp_mode_t
#endif

/* reordered_opaque appeared in libavcodec 51.68.0 */
#define AVCODEC_HAS_REORDERED_OPAQUE
#if LIBAVCODEC_VERSION_INT < 0x334400
# undef AVCODEC_HAS_REORDERED_OPAQUE
#endif

/* avcodec_thread_init() */
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 112)
#  define DEPRECATED_AVCODEC_THREAD_INIT 1
#endif

/* av_parser_parse() */
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 94)
#  define AVPARSE 2
#else
#  define AVPARSE 1
#endif

/* avcodec_decode_video() */
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 32)
#  define AVVIDEO 2
#else
#  define AVVIDEO 1
#endif

/* avcodec_decode_audio() */
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 32)
#  define AVAUDIO 3
#else
#  define AVAUDIO 2
#endif


#endif /* XINE_AVCODEC_COMPAT_H */
