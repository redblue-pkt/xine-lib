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
 * ID3 tag parser
 *
 * Supported versions: v1, v2.2
 * TODO: v2.3, v2.4
 *
 * $Id: id3.h,v 1.1 2003/12/07 23:05:41 tmattern Exp $
 */

#ifndef ID3_H
#define ID3_H

#include "xine_internal.h"
#include "xineutils.h"
#include "bswap.h"

/* id3v2 */
#define FOURCC_TAG BE_FOURCC
#define ID3V22_TAG FOURCC_TAG('I', 'D', '3', 2)  /* id3 v2.2 tags */
#define ID3V23_TAG FOURCC_TAG('I', 'D', '3', 3)  /* id3 v2.3 tags */
#define ID3V24_TAG FOURCC_TAG('I', 'D', '3', 4)  /* id3 v2.4 tags */

/* id2v2.2 */
#define ID3V22_FRAME_HEADER_SIZE       6
#define ID3V22_UNSYNCH_FLAG       0x8000
#define ID3V22_COMPRESS_FLAG      0x4000

/* id2v2.3 */
#define ID3V23_FRAME_HEADER_SIZE      10
#define ID3V23_UNSYNCH_FLAG       0x8000
#define ID3V23_EXTHEAD_FLAG       0x4000
#define ID3V23_EXP_FLAG           0x2000

/* id2v2.4 */
#define ID3V24_FRAME_HEADER_SIZE      10
#define ID3V24_UNSYNCH_FLAG       0x8000
#define ID3V24_EXTHEAD_FLAG       0x4000
#define ID3V24_EXP_FLAG           0x2000
#define ID3V24_FOOTER_FLAG        0x1000

typedef struct {
  uint32_t  id;
  uint8_t   revision;
  uint8_t   flags;
  uint32_t  size;
} id3v2_header_t;

typedef struct {
  uint32_t  id;
  uint32_t  size;
} id3v22_frame_header_t;

typedef struct {
  uint32_t  id;
  uint32_t  size;
  uint16_t  flags;
} id3v23_frame_header_t;

typedef struct {
  uint32_t  size;
  uint16_t  flags;
  uint32_t  padding_size;
  uint32_t  crc;
} id3v23_frame_ext_header_t;

typedef struct {
  char tag[3];
  char title[30];
  char artist[30];
  char album[30];
  char year[4];
  char comment[30];
  char genre;
} id3v1_tag_t;

int id3v1_parse_tag (input_plugin_t *input, xine_stream_t *stream);

int id3v22_parse_tag(input_plugin_t *input,
                     xine_stream_t *stream,
                     int8_t *mp3_frame_header);

int id3v23_parse_tag(input_plugin_t *input,
                     xine_stream_t *stream,
                     int8_t *mp3_frame_header);

#endif /* ID3_H */
