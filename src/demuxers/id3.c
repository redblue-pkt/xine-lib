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
 * Supported versions: v1, v2.2, v2.3
 * TODO: v2.4
 *
 * ID3v2 specs: http://www.id3.org/
 *
 * $Id: id3.c,v 1.3 2003/12/07 23:17:07 tmattern Exp $
 */
 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define LOG_MODULE "id3"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine_internal.h"
#include "xineutils.h"
#include "bswap.h"
#include "id3.h"

int id3v1_parse_tag (input_plugin_t *input, xine_stream_t *stream) {

  off_t len;
  id3v1_tag_t tag;

  /* id3v1 */
  len = input->read (input, (char *)&tag, 128);

  if (len == 128) {

    if ( (tag.tag[0]=='T') && (tag.tag[1]=='A') && (tag.tag[2]=='G') ) {

      lprintf("id3v1 tag found\n");
      _x_meta_info_n_set(stream, XINE_META_INFO_TITLE, tag.title, 30);
      _x_meta_info_n_set(stream, XINE_META_INFO_ARTIST, tag.artist, 30);
      _x_meta_info_n_set(stream, XINE_META_INFO_ALBUM, tag.album, 30);
      _x_meta_info_n_set(stream, XINE_META_INFO_COMMENT, tag.comment, 30);
    }
    return 1;
  } else {
    return 0;
  }
}



static int id3v2_parse_header(input_plugin_t *input, uint8_t *mp3_frame_header,
                              id3v2_header_t *tag_header) {
  uint8_t buf[6];

  tag_header->id = BE_32(mp3_frame_header);
  if (input->read (input, buf, 6) == 6) {
    tag_header->revision = buf[0];
    tag_header->flags    = buf[1];

    /* only 7 bits per byte (unsynch) */
    tag_header->size     = (buf[2] << 21) + (buf[3] << 14) + (buf[4] << 7) + buf[5];
    lprintf("tag: ID3 v2.%d.%d\n", mp3_frame_header[3], tag_header->revision);
    lprintf("flags: %d\n", tag_header->flags);
    lprintf("size: %d\n", tag_header->size);
    return 1;
  } else {
    return 0;
  }
}

/* id3 v2.2 */

static int id3v22_parse_frame_header(input_plugin_t *input,
                                     id3v22_frame_header_t *frame_header) {
  uint8_t buf[ID3V22_FRAME_HEADER_SIZE];
  int len;

  len  = input->read (input, buf, ID3V22_FRAME_HEADER_SIZE);
  if (len == ID3V22_FRAME_HEADER_SIZE) {
    frame_header->id    = (buf[0] << 16) + (buf[1] << 8) + buf[2];

    /* only 7 bits per byte (unsynch) */
    frame_header->size  = (buf[3] << 14) + (buf[4] << 7) + buf[5];

    lprintf("frame: %c%c%c: size: %d\n", buf[0], buf[1], buf[2],
            frame_header->size);

    return 1;
  } else {
    return 0;
  }
}

static int id3v22_interp_frame(input_plugin_t *input,
                               xine_stream_t *stream,
                               id3v22_frame_header_t *frame_header) {
  /*
   * FIXME: supports unicode
   */
  char buf[4096];

  if (frame_header->size >= 4096) {
    lprintf("too long\n");
    return 1;
  }

  if (input->read (input, buf, frame_header->size) == frame_header->size) {
    buf[frame_header->size] = 0;

    switch (frame_header->id) {
      case ( FOURCC_TAG(0, 'T', 'T', '1') ):
        _x_meta_info_set(stream, XINE_META_INFO_GENRE, buf + 1);
        break;

      case ( FOURCC_TAG(0, 'T', 'T', '2') ):
        _x_meta_info_set(stream, XINE_META_INFO_TITLE, buf + 1);
        break;

      case ( FOURCC_TAG(0, 'T', 'P', '1') ):
        _x_meta_info_set(stream, XINE_META_INFO_ARTIST, buf + 1);
        break;

      case ( FOURCC_TAG(0, 'T', 'A', 'L') ):
        _x_meta_info_set(stream, XINE_META_INFO_ALBUM, buf + 1);
        break;

      case ( FOURCC_TAG(0, 'T', 'Y', 'E') ):
        _x_meta_info_set(stream, XINE_META_INFO_YEAR, buf + 1);
        break;

      case ( FOURCC_TAG(0, 'C', 'O', 'M') ):
        _x_meta_info_set(stream, XINE_META_INFO_COMMENT, buf + 1 + 3);
        break;

      default:
        lprintf("unhandled frame\n");
    }

    return 1;
  } else {
    lprintf("read error\n");
    return 0;
  }
}


int id3v22_parse_tag(input_plugin_t *input,
                     xine_stream_t *stream,
                     int8_t *mp3_frame_header) {
  id3v2_header_t tag_header;
  id3v22_frame_header_t tag_frame_header;
  int pos = 0;

  if (id3v2_parse_header(input, mp3_frame_header, &tag_header)) {

    if (tag_header.flags & ID3V22_COMPRESS_FLAG) {
      /* compressed tag ? just skip it */
      input->seek (input, tag_header.size - pos, SEEK_CUR);
    } else {
      /* frame parsing */
      while ((pos + ID3V22_FRAME_HEADER_SIZE) <= tag_header.size) {
        if (id3v22_parse_frame_header(input, &tag_frame_header)) {
          pos += ID3V22_FRAME_HEADER_SIZE;
          if (tag_frame_header.id && tag_frame_header.size) {
            if ((pos + tag_frame_header.size) <= tag_header.size) {
              if (!id3v22_interp_frame(input, stream, &tag_frame_header)) {
                xprintf(stream->xine, XINE_VERBOSITY_DEBUG, 
                        "id3: invalid frame content\n");
              }
            } else {
              xprintf(stream->xine, XINE_VERBOSITY_DEBUG, 
                      "id3: invalid frame header\n");
              input->seek (input, tag_header.size - pos, SEEK_CUR);
              return 1;
            }
            pos += tag_frame_header.size;
          } else {
            /* end of frames, the rest is padding */
            input->seek (input, tag_header.size - pos, SEEK_CUR);
            return 1;
          }
        } else {
          xprintf(stream->xine, XINE_VERBOSITY_DEBUG, 
                  "id3: id3v2_parse_frame_header problem\n");
          return 0;
        }
      }
    }
    return 1;
  } else {
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "id3: id3v2_parse_header problem\n");
    return 0;
  }
}

/* id3 v2.3 */

static int id3v23_parse_frame_header(input_plugin_t *input,
                                     id3v23_frame_header_t *frame_header) {
  uint8_t buf[ID3V23_FRAME_HEADER_SIZE];
  int len;

  len  = input->read (input, buf, ID3V23_FRAME_HEADER_SIZE);
  if (len == ID3V23_FRAME_HEADER_SIZE) {
    frame_header->id    = BE_32(buf);

    /* only 7 bits per byte (unsynch) */
    frame_header->size  = (buf[4] << 21) + (buf[5] << 14) + (buf[6] << 7) + buf[7];

    frame_header->flags = BE_16(buf + 8);

    lprintf("frame: %c%c%c%c, size: %d, flags: %X\n", buf[0], buf[1], buf[2], buf[3],
            frame_header->size, frame_header->flags);

    return 1;
  } else {
    return 0;
  }
}

static int id3v23_parse_frame_ext_header(input_plugin_t *input,
                                         id3v23_frame_ext_header_t *frame_ext_header) {
  uint8_t buf[14];

  if (input->read (input, buf, 4) == 4) {
    /* only 7 bits per byte (unsynch) */
    frame_ext_header->size  = (buf[0] << 21) + (buf[1] << 14) + (buf[2] << 7) + buf[3];
    
    if (frame_ext_header->size == 6) {
      if (input->read (input, buf + 4, 6) == 6) {
        frame_ext_header->flags = BE_16(buf + 4);
        frame_ext_header->padding_size = BE_32(buf + 6);
        frame_ext_header->crc = 0;
      } else {
        return 0;
      }        

    } else if (frame_ext_header->size == 10) {
      if (input->read (input, buf + 4, 10) == 10) {
        frame_ext_header->flags = BE_16(buf + 4);
        frame_ext_header->padding_size = BE_32(buf + 6);
        frame_ext_header->crc = BE_32(buf + 10);
      } else {
        return 0;
      }

    } else {
      lprintf("invalid ext header size: %d\n", frame_ext_header->size);
      return 0;
    }

    lprintf("ext header: size: %d, flags: %X, padding_size: %d, crc: %d\n",
            frame_ext_header->size, frame_ext_header->flags,
            frame_ext_header->padding_size, frame_ext_header->crc);
    return 1;
  } else {
    return 0;
  }
}

static int id3v23_interp_frame(input_plugin_t *input,
                               xine_stream_t *stream,
                               id3v23_frame_header_t *frame_header) {
  /*
   * FIXME: supports unicode
   */
  char buf[4096];

  if (frame_header->size >= 4096) {
    lprintf("too long\n");
    return 1;
  }

  if (input->read (input, buf, frame_header->size) == frame_header->size) {
    buf[frame_header->size] = 0;

    switch (frame_header->id) {
      case ( FOURCC_TAG('T', 'C', 'O', 'N') ):
        _x_meta_info_set(stream, XINE_META_INFO_GENRE, buf + 1);
        break;

      case ( FOURCC_TAG('T', 'I', 'T', '2') ):
        _x_meta_info_set(stream, XINE_META_INFO_TITLE, buf + 1);
        break;

      case ( FOURCC_TAG('T', 'P', 'E', '1') ):
        _x_meta_info_set(stream, XINE_META_INFO_ARTIST, buf + 1);
        break;

      case ( FOURCC_TAG('T', 'A', 'L', 'B') ):
        _x_meta_info_set(stream, XINE_META_INFO_ALBUM, buf + 1);
        break;

      case ( FOURCC_TAG('T', 'Y', 'E', 'R') ):
        _x_meta_info_set(stream, XINE_META_INFO_YEAR, buf + 1);
        break;

      case ( FOURCC_TAG('C', 'O', 'M', 'M') ):
        _x_meta_info_set(stream, XINE_META_INFO_COMMENT, buf + 1 + 3);
        break;

      default:
        lprintf("unhandled frame\n");
    }

    return 1;
  } else {
    lprintf("read error\n");
    return 0;
  }
}

int id3v23_parse_tag(input_plugin_t *input,
                     xine_stream_t *stream,
                     int8_t *mp3_frame_header) {
  id3v2_header_t tag_header;
  id3v23_frame_header_t tag_frame_header;
  id3v23_frame_ext_header_t tag_frame_ext_header;
  int pos = 0;

  if (id3v2_parse_header(input, mp3_frame_header, &tag_header)) {

    if (tag_header.flags & ID3V23_EXTHEAD_FLAG) {
      /* extended header */
      if (!id3v23_parse_frame_ext_header(input, &tag_frame_ext_header)) {
        return 0;
      }
    }
    
    /* frame parsing */
    while ((pos + ID3V23_FRAME_HEADER_SIZE) <= tag_header.size) {
      if (id3v23_parse_frame_header(input, &tag_frame_header)) {
        pos += ID3V23_FRAME_HEADER_SIZE;
        if (tag_frame_header.id && tag_frame_header.size) {
          if ((pos + tag_frame_header.size) <= tag_header.size) {
            if (!id3v23_interp_frame(input, stream, &tag_frame_header)) {
              xprintf(stream->xine, XINE_VERBOSITY_DEBUG, 
                      "id3: invalid frame content\n");
            }
          } else {
            xprintf(stream->xine, XINE_VERBOSITY_DEBUG, 
                    "id3: invalid frame header\n");
            input->seek (input, tag_header.size - pos, SEEK_CUR);
            return 1;
          }
          pos += tag_frame_header.size;
        } else {
          /* end of frames, the rest is padding */
          input->seek (input, tag_header.size - pos, SEEK_CUR);
          return 1;
        }
      } else {
        xprintf(stream->xine, XINE_VERBOSITY_DEBUG, 
                "id3: id3v2_parse_frame_header problem\n");
        return 0;
      }
    }
    return 1;
  } else {
    xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "id3v22: id3v2_parse_header problem\n");
    return 0;
  }
}
