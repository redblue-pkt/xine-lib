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
 * $Id: input_plugin.h,v 1.4 2001/05/03 00:02:42 f1rmb Exp $
 */

#ifndef HAVE_INPUT_PLUGIN_H
#define HAVE_INPUT_PLUGIN_H

#include <inttypes.h>
#include <sys/types.h>
#include "buffer.h"
#include "configfile.h"

#define INPUT_PLUGIN_IFACE_VERSION   1
 
#ifndef CLUT_T
#define CLUT_T
typedef struct {         /* CLUT == Color LookUp Table */
	uint8_t foo		: 8; /* UNKNOWN: 0x00? */
	uint8_t y		: 8;
	uint8_t cr		: 8;
	uint8_t cb		: 8;
} __attribute__ ((packed)) clut_t;
#endif

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
  char** (*get_dir) (input_plugin_t *this, char *filename, int *nFiles);


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
   * gets the subtitle/menu palette
   */
  clut_t* (*get_clut) (input_plugin_t *this);
};

/*
 * possible capabilites an input plugin can have:
 */

#define INPUT_CAP_SEEKABLE    0x00000001
#define INPUT_CAP_BLOCK       0x00000002
#define INPUT_CAP_AUTOPLAY    0x00000004
#define INPUT_CAP_BROWSABLE   0x00000008
#define INPUT_CAP_CLUT        0x00000010

#endif
