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
 * $Id: input_plugin.h,v 1.36 2002/10/25 15:36:22 mroi Exp $
 */

#ifndef HAVE_INPUT_PLUGIN_H
#define HAVE_INPUT_PLUGIN_H

#include <inttypes.h>
#include <sys/types.h>
#include <assert.h>
#include "buffer.h"
#include "configfile.h"

#define INPUT_PLUGIN_IFACE_VERSION   9
 
typedef struct input_class_s input_class_t ;
typedef struct input_plugin_s input_plugin_t;

struct input_class_s {

  /*
   * open a new instance of this plugin class
   */
  input_plugin_t* (*open_plugin) (input_class_t *this, xine_stream_t *stream, const char *mrl);

  /*
   * return short, human readable identifier for this plugin class
   */
  char* (*get_identifier) (input_class_t *this);

  /*
   * return human readable (verbose = 1 line) description for 
   * this plugin class
   */
  char* (*get_description) (input_class_t *this);

  /*
   * ls function, optional: may be NULL
   * return value: NULL => filename is a file, **char=> filename is a dir
   */
  xine_mrl_t ** (*get_dir) (input_class_t *this, const char *filename, int *nFiles);

  /*
   * generate autoplay list, optional: may be NULL
   * return value: list of MRLs
   */
  char ** (*get_autoplay_list) (input_class_t *this, int *num_files);

  /*
   * close down, free all resources
   */
  void (*dispose) (input_class_t *this);

  /*
   * eject/load the media (if possible)
   *
   * returns 0 for temporary failures
   */
  int (*eject_media) (input_class_t *this);
};

struct input_plugin_s {

  /*
   * return capabilities of input source
   */

  uint32_t (*get_capabilities) (input_plugin_t *this);

  /*
   * read nlen bytes, return number of bytes read
   */
  off_t (*read) (input_plugin_t *this, char *buf, off_t nlen);


  /*
   * read one block, return newly allocated block (or NULL on failure)
   * for blocked input sources len must be == blocksize
   * the fifo parameter is only used to get access to the buffer_pool_alloc function
   */
  buf_element_t *(*read_block)(input_plugin_t *this, fifo_buffer_t *fifo, off_t len);


  /*
   * seek position, return new position 
   *
   * if seeking failed, -1 is returned
   */
  off_t (*seek) (input_plugin_t *this, off_t offset, int origin);


  /*
   * get current position in stream.
   *
   */
  off_t (*get_current_pos) (input_plugin_t *this);


  /*
   * return length of input (-1 => unlimited, e.g. stream)
   */
  off_t (*get_length) (input_plugin_t *this);


  /*
   * return block size of input source (if supported, 0 otherwise)
   */

  uint32_t (*get_blocksize) (input_plugin_t *this);


  /*
   * return current MRL
   */
  char * (*get_mrl) (input_plugin_t *this);


  /*
   * request optional data from input plugin.
   */
  int (*get_optional_data) (input_plugin_t *this, void *data, int data_type);


  /*
   * close stream, free instance resources
   */
  void (*dispose) (input_plugin_t *this);

  /*
   * "backward" link to input plugin class struct
   */

  input_class_t *input_class;

};

/*
 * possible capabilites an input plugin can have:
 */
#define INPUT_CAP_NOCAP                0x00000000

/*
 * INPUT_CAP_SEEKABLE: 
 *   seek () works reliably. 
 *   even for plugins that do not have this flag set
 *   it is a good idea to implement the seek() function 
 *   in a "best effort" style anyway, so at least 
 *   throw away data for network streams when seeking forward
 */

#define INPUT_CAP_SEEKABLE             0x00000001

/*
 * INPUT_CAP_BLOCK:
 *   means more or less that a block device sits behind 
 *   this input plugin. get_blocksize must be implemented. 
 *   will be used for fast and efficient demuxing of 
 *   mpeg streams (demux_mpeg_block).
 */

#define INPUT_CAP_BLOCK                0x00000002

/*
 * INPUT_CAP_AUDIOLANG:
 * INPUT_CAP_SPULANG:
 *   input plugin knows something about audio/spu languages, 
 *   e.g. knows that audio stream #0 is english, 
 *   audio stream #1 is german, ...
 */

#define INPUT_CAP_AUDIOLANG            0x00000008
#define INPUT_CAP_SPULANG              0x00000010
 
/*
 * INPUT_CAP_VARIABLE_BITRATE:
 *   FIXME: ???
 */

#define INPUT_CAP_VARIABLE_BITRATE     0x00000020

/* 
 * INPUT_CAP_PREVIEW:
 *   get_optional_data can handle INPUT_OPTIONAL_DATA_PREVIEW 
 *   so a non-seekable stream plugin can povide the first 
 *   few bytes for demuxers to look at them and decide wheter 
 *   they can handle the stream or not. the preview data must 
 *   be buffered and delivered again through subsequent 
 *   read() calls.
 */

#define INPUT_CAP_PREVIEW              0x00000040  

/*
 * INPUT_CAP_CHAPTERS:
 *   The media streams provided by this plugin have an internal
 *   structure dividing it into segments usable for navigation.
 *   For those plugins, the behaviour of the skip button in UIs
 *   should be changed from "next MRL" to "next chapter" by
 *   sending XINE_EVENT_INPUT_NEXT.
 */

#define INPUT_CAP_CHAPTERS             0x00000080


#define INPUT_OPTIONAL_UNSUPPORTED    0
#define INPUT_OPTIONAL_SUCCESS        1

#define INPUT_OPTIONAL_DATA_AUDIOLANG 2
#define INPUT_OPTIONAL_DATA_SPULANG   3
#define INPUT_OPTIONAL_DATA_TEXTSPU0  4
#define INPUT_OPTIONAL_DATA_TEXTSPU1  5
#define INPUT_OPTIONAL_DATA_TEXTSPU2  6
#define INPUT_OPTIONAL_DATA_PREVIEW   7

#define MAX_MRL_ENTRIES 255

/* Types of mrls returned by get_dir() */
#define mrl_unknown        (0 << 0)
#define mrl_dvd            (1 << 0)
#define mrl_vcd            (1 << 1)
#define mrl_net            (1 << 2)
#define mrl_rtp            (1 << 3)
#define mrl_stdin          (1 << 4)
#define mrl_cda            (1 << 5)
#define mrl_file           (1 << 6)
#define mrl_file_fifo      (1 << 7)
#define mrl_file_chardev   (1 << 8)
#define mrl_file_directory (1 << 9)
#define mrl_file_blockdev  (1 << 10)
#define mrl_file_normal    (1 << 11)
#define mrl_file_symlink   (1 << 12)
#define mrl_file_sock      (1 << 13)
#define mrl_file_exec      (1 << 14)
#define mrl_file_backup    (1 << 15)
#define mrl_file_hidden    (1 << 16)

/*
 * Freeing/zeroing all of entries of given mrl.
 */
#define MRL_ZERO(m) {                                                         \
  if((m)) {                                                                   \
    if((m)->origin)                                                           \
      free((m)->origin);                                                      \
    if((m)->mrl)                                                              \
      free((m)->mrl);                                                         \
    if((m)->link)                                                             \
      free((m)->link);                                                        \
    (m)->origin = NULL;                                                       \
    (m)->mrl    = NULL;                                                       \
    (m)->link   = NULL;                                                       \
    (m)->type   = 0;                                                          \
    (m)->size   = (off_t) 0;                                                  \
  }                                                                           \
}

/*
 * Duplicate two mrls entries (s = source, d = destination).
 */
#define MRL_DUPLICATE(s, d) {                                                 \
  assert((s) != NULL);                                                        \
  assert((d) != NULL);                                                        \
                                                                              \
  if((s)->origin) {                                                           \
    if((d)->origin) {                                                         \
      (d)->origin = (char *) realloc((d)->origin, strlen((s)->origin) + 1);   \
      sprintf((d)->origin, "%s", (s)->origin);                                \
    }                                                                         \
    else                                                                      \
      (d)->origin = strdup((s)->origin);                                      \
  }                                                                           \
  else                                                                        \
    (d)->origin = NULL;                                                       \
                                                                              \
  if((s)->mrl) {                                                              \
    if((d)->mrl) {                                                            \
      (d)->mrl = (char *) realloc((d)->mrl, strlen((s)->mrl) + 1);            \
      sprintf((d)->mrl, "%s", (s)->mrl);                                      \
    }                                                                         \
    else                                                                      \
      (d)->mrl = strdup((s)->mrl);                                            \
  }                                                                           \
  else                                                                        \
    (d)->mrl = NULL;                                                          \
                                                                              \
  if((s)->link) {                                                             \
    if((d)->link) {                                                           \
      (d)->link = (char *) realloc((d)->link, strlen((s)->link) + 1);         \
      sprintf((d)->link, "%s", (s)->link);                                    \
    }                                                                         \
    else                                                                      \
      (d)->link = strdup((s)->link);                                          \
  }                                                                           \
  else                                                                        \
    (d)->link = NULL;                                                         \
                                                                              \
  (d)->type = (s)->type;                                                      \
  (d)->size = (s)->size;                                                      \
}

/*
 * Duplicate two arrays of mrls (s = source, d = destination).
 */
#define MRLS_DUPLICATE(s, d) {                                                \
  int i = 0;                                                                  \
                                                                              \
  assert((s) != NULL);                                                        \
  assert((d) != NULL);                                                        \
                                                                              \
  while((s) != NULL) {                                                        \
    d[i] = (xine_mrl_t *) malloc(sizeof(xine_mrl_t));                                   \
    MRL_DUPLICATE(s[i], d[i]);                                                \
    i++;                                                                      \
  }                                                                           \
}


#endif
