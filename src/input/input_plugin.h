/* 
 * Copyright (C) 2000 the xine project
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: input_plugin.h,v 1.16 2001/10/22 22:50:01 richwareham Exp $
 */

#ifndef HAVE_INPUT_PLUGIN_H
#define HAVE_INPUT_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <sys/types.h>
#include <assert.h>
#include "buffer.h"
#include "configfile.h"

#define INPUT_PLUGIN_IFACE_VERSION   5
 
/*
 * Return pointer of allocate/cleaned memory size *size*.
 */
extern void *xmalloc(size_t);

#define MAX_MRL_ENTRIES 255

/* Types of mrls returned by get_dir() */
#define mrl_unknown        (0 << 0)
#define mrl_dvd            (1 << 0)
#define mrl_vcd            (1 << 1)
#define mrl_net            (1 << 2)
#define mrl_rtp            (1 << 3)
#define mrl_stdin          (1 << 4)
#define mrl_file           (1 << 5)
#define mrl_file_fifo      (1 << 6)
#define mrl_file_chardev   (1 << 7)
#define mrl_file_directory (1 << 8)
#define mrl_file_blockdev  (1 << 9)
#define mrl_file_normal    (1 << 10)
#define mrl_file_symlink   (1 << 11)
#define mrl_file_sock      (1 << 12)
#define mrl_file_exec      (1 << 13)
#define mrl_file_backup    (1 << 14)
#define mrl_file_hidden    (1 << 15)

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
    d[i] = (mrl_t *) malloc(sizeof(mrl_t));                                   \
    MRL_DUPLICATE(s[i], d[i]);                                                \
    i++;                                                                      \
  }                                                                           \
}

typedef struct {
  char         *origin;  /* Origin of grabbed mrls (eg: path for file plugin */
  char         *mrl;     /* <type>://<location>                              */
  char         *link;    /* name of link, if exist, otherwise NULL           */
  uint32_t      type;    /* match to mrl_type enum                           */
  off_t         size;    /* size of this source, may be 0                    */
} mrl_t;

typedef struct input_plugin_s input_plugin_t;

struct input_plugin_s
{

  /*
   * plugin interface version, lower versions _may_ be supported
   */
  int interface_version;

  /*
   * return capabilities of input source
   */

  uint32_t (*get_capabilities) (input_plugin_t *this);

  /*
   * open input MRL - return 1 if succ
   */
  int (*open) (input_plugin_t *this, char *mrl);


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
   * ls function
   * return value: NULL => filename is a file, **char=> filename is a dir
   */
  mrl_t** (*get_dir) (input_plugin_t *this, char *filename, int *nFiles);


  /*
   * eject/load the media (if it's possible)
   *
   * returns 0 for temporary failures
   */
  int (*eject_media) (input_plugin_t *this);


  /*
   * return current MRL
   */
  char * (*get_mrl) (input_plugin_t *this);


  /*
   * stop input source
   */
  void (*stop) (input_plugin_t *this);


  /*
   * close input source
   */
  void (*close) (input_plugin_t *this);


  /*
   * return human readable (verbose = 1 line) description for this plugin
   */
  char* (*get_description) (input_plugin_t *this);


  /*
   * return short, human readable identifier for this plugin
   * this is used for GUI buttons, The identifier must have max. 4 characters
   * characters (max. 5 including terminating \0)
   */
  char* (*get_identifier) (input_plugin_t *this);


  /*
   * generate autoplay list
   * return value: list of MRLs
   */
  char** (*get_autoplay_list) (input_plugin_t *this, int *nFiles);


  /*
   * request optional data from input plugin.
   */
  int (*get_optional_data) (input_plugin_t *this, void *data, int data_type);

  /*
   * check if it is possible/valid to directly branch to this MRL
   * optional: may be NULL
   */
  
  int (*is_branch_possible) (input_plugin_t *this, char *next_mrl);
};

/*
 * possible capabilites an input plugin can have:
 */
#define INPUT_CAP_NOCAP       0x00000000
#define INPUT_CAP_SEEKABLE    0x00000001
#define INPUT_CAP_BLOCK       0x00000002
#define INPUT_CAP_AUTOPLAY    0x00000004
#define INPUT_CAP_GET_DIR     0x00000008
#define INPUT_CAP_BROWSABLE   0x00000010
#define INPUT_CAP_CLUT        0x00000020
#define INPUT_CAP_AUDIOLANG   0x00000040
#define INPUT_CAP_SPULANG     0x00000080
#define INPUT_CAP_VARIABLE_BITRATE     0x00000100


#define INPUT_OPTIONAL_UNSUPPORTED    0
#define INPUT_OPTIONAL_SUCCESS        1

#define INPUT_OPTIONAL_DATA_CLUT      1
#define INPUT_OPTIONAL_DATA_AUDIOLANG 2
#define INPUT_OPTIONAL_DATA_SPULANG   3

/*
 * each input plugin _must_ implement this function:
 *
 * input_plugin_t *init_input_plugin (int iface, xine_t *xine) ;
 *
 */

#ifdef __cplusplus
}
#endif

#endif
