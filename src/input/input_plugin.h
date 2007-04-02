/*
 * Copyright (C) 2000-2004 the xine project
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
 * $Id: input_plugin.h,v 1.64 2007/01/19 01:05:25 dgp85 Exp $
 */

#ifndef HAVE_INPUT_PLUGIN_H
#define HAVE_INPUT_PLUGIN_H

#include <sys/types.h>

#ifdef XINE_COMPILE
#  include <inttypes.h>
#  include "xineutils.h"
#  include "buffer.h"
#  include "configfile.h"
#else
#  include <xine/os_types.h>
#  include <xine/xineutils.h>
#  include <xine/buffer.h>
#  include <xine/configfile.h>
#endif

#define INPUT_PLUGIN_IFACE_VERSION   17
 
typedef struct input_class_s input_class_t ;
typedef struct input_plugin_s input_plugin_t;

struct input_class_s {

  /*
   * create a new instance of this plugin class
   * return NULL if the plugin does'nt handle the given mrl
   */
  input_plugin_t* (*get_instance) (input_class_t *this, xine_stream_t *stream, const char *mrl);

  /*
   * return short, human readable identifier for this plugin class
   * The returned string should not be malloc'd as it is not freed.
   */
  const char* (*get_identifier) (input_class_t *this);

  /*
   * return human readable (verbose = 1 line) description for 
   * this plugin class
   */
  const char* (*get_description) (input_class_t *this);

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
   * eject/load the media (if possible), optional: may be NULL
   *
   * returns 0 for temporary failures
   */
  int (*eject_media) (input_class_t *this);
};

struct input_plugin_s {

  /*
   * open the stream
   * return 0 if an error occured
   */
  int (*open) (input_plugin_t *this);

  /*
   * return capabilities of the current playable entity. See
   * get_current_pos below for a description of a "playable entity"
   * Capabilities a created by "OR"ing a mask of constants listed
   * below which start "INPUT_CAP".  
   *
   * depending on the values set, some of the functions below
   * will or will not get called or should (not) be able to
   * do certain tasks.
   *
   * for example if INPUT_CAP_SEEKABLE is set,
   * the seek() function is expected to work fully at any time.
   * however, if the flag is not set, the seek() function should
   * make a best-effort attempt to seek, e.g. at least
   * relative forward seeking should work.
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
   * seek to time position, return new position 
   * time_offset is given in miliseconds
   *
   * if seeking failed, -1 is returned
   *
   * note: only SEEK_SET (0) is currently supported as origin
   * note: may be NULL is not supported
   */
  off_t (*seek_time) (input_plugin_t *this, int time_offset, int origin);


  /*
   * get current position in stream.
   *
   */
  off_t (*get_current_pos) (input_plugin_t *this);

  
  /*
   * get current time position in stream in miliseconds.
   *
   * note: may be NULL is not supported
   */
  int (*get_current_time) (input_plugin_t *this);


  /*
   * return number of bytes in the next playable entity or -1 if the
   * input is unlimited, as would be the case in a network stream.
   * 
   * A "playable entity" tends to be the entities listed in a playback
   * list or the units on which playback control generally works on.
   * It might be the number of bytes in a VCD "segment" or "track" (if
   * the track has no "entry" subdivisions), or the number of bytes in
   * a PS (Program Segment or "Chapter") of a DVD. If there are no
   * subdivisions of the input medium and it is considered one
   * indivisible entity, it would be the byte count of that entity;
   * for example, the length in bytes of an MPEG file.

   * This length information is used, for example when in setting the
   * absolute or relative play position or possibly calculating the
   * bit rate.
   */
  off_t (*get_length) (input_plugin_t *this);


  /*
   * return block size in bytes of next complete playable entity (if
   * supported, 0 otherwise). See the description above under
   * get_length for a description of a "complete playable entity".
   * 
   * this block size is only used for mpeg streams stored on
   * a block oriented storage media, e.g. DVDs and VCDs, to speed
   * up the demuxing process. only set this (and the INPUT_CAP_BLOCK
   * flag) if this is the case for your input plugin.
   *
   * make this function simply return 0 if unsure.
   */

  uint32_t (*get_blocksize) (input_plugin_t *this);


  /*
   * return current MRL
   */
  const char * (*get_mrl) (input_plugin_t *this);


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

  void *node; /* used by plugin loader */

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
 *   *((int *)data) will provide the requested channel number
 *   and awaits the language back in (char *)data
 */

#define INPUT_CAP_AUDIOLANG            0x00000008
#define INPUT_CAP_SPULANG              0x00000010
 
/* 
 * INPUT_CAP_PREVIEW:
 *   get_optional_data can handle INPUT_OPTIONAL_DATA_PREVIEW 
 *   so a non-seekable stream plugin can povide the first 
 *   few bytes for demuxers to look at them and decide wheter 
 *   they can handle the stream or not. the preview data must 
 *   be buffered and delivered again through subsequent 
 *   read() calls.
 *   caller must provide a buffer allocated with at least 
 *   MAX_PREVIEW_SIZE bytes.
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

/*
 * INPUT_CAP_RIP_FORBIDDEN:
 *   means that rip/disk saving must not be used.
 *   (probably at author's request)
 */

#define INPUT_CAP_RIP_FORBIDDEN        0x00000100


#define INPUT_IS_SEEKABLE(input) (((input)->get_capabilities(input) & INPUT_CAP_SEEKABLE) != 0)

#define INPUT_OPTIONAL_UNSUPPORTED    0
#define INPUT_OPTIONAL_SUCCESS        1

#define INPUT_OPTIONAL_DATA_AUDIOLANG 2
#define INPUT_OPTIONAL_DATA_SPULANG   3
#define INPUT_OPTIONAL_DATA_PREVIEW   7

#define MAX_MRL_ENTRIES 255
#define MAX_PREVIEW_SIZE 4096

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
  _x_assert((s) != NULL);                                                     \
  _x_assert((d) != NULL);                                                     \
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
  _x_assert((s) != NULL);                                                     \
  _x_assert((d) != NULL);                                                     \
                                                                              \
  while((s) != NULL) {                                                        \
    d[i] = (xine_mrl_t *) malloc(sizeof(xine_mrl_t));                         \
    MRL_DUPLICATE(s[i], d[i]);                                                \
    i++;                                                                      \
  }                                                                           \
}


#endif
