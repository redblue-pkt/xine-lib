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
 * $Id: input_plugin.h,v 1.1 2001/04/18 22:34:05 f1rmb Exp $
 */

#ifndef HAVE_INPUT_PLUGIN_H
#define HAVE_INPUT_PLUGIN_H

#include <inttypes.h>
#include <sys/types.h>
#include "buffer.h"
#include "configfile.h"

#define INPUT_INTERFACE_VERSION   1
 
#ifndef CLUT_T
#define CLUT_T
typedef struct {         /* CLUT == Color LookUp Table */
	uint8_t foo		: 8; /* UNKNOWN: 0x00? */
	uint8_t y		: 8;
	uint8_t cr		: 8;
	uint8_t cb		: 8;
} __attribute__ ((packed)) clut_t;
#endif

typedef struct input_plugin_s
{

  /*
   * plugin interface version, lower versions _may_ be supported
   */
  int interface_version;

  /*
   * return capabilities of input source
   */

  uint32_t (*get_capabilities) (void);

  /*
   * open input MRL - return 1 if succ
   */
  int (*open) (char *mrl);


  /*
   * read nlen bytes, return number of bytes read
   */
  off_t (*read) (char *buf, off_t nlen);


  /*
   * read one block, return newly allocated block (or NULL on failure)
   * for blocked input sources len must be == blocksize
   * the fifo parameter is only used to get access to the buffer_pool_alloc function
   */
  buf_element_t *(*read_block)(fifo_buffer_t *fifo, off_t len);


  /*
   * seek position, return new position 
   *
   * if seeking failed, -1 is returned
   */
  off_t (*seek) (off_t offset, int origin);


  /*
   * get current position in stream.
   *
   */
  off_t (*get_current_pos) (void);


  /*
   * return length of input (-1 => unlimited, e.g. stream)
   */
  off_t (*get_length) (void);


  /*
   * return block size of input source (if supported, 0 otherwise)
   */

  uint32_t (*get_blocksize) (void);


  /*
   * ls function
   * return value: NULL => filename is a file, **char=> filename is a dir
   */
  char** (*get_dir) (char *filename, int *nFiles);


  /*
   * eject/load the media (if it's possible)
   *
   * returns 0 for temporary failures
   */
  int (*eject_media) (void);


  /*
   * return current MRL
   */
  char * (*get_mrl) (void);


  /*
   * close input source
   */
  void (*close) (void);


  /*
   * return human readable (verbose = 1 line) description for this plugin
   */
  char* (*get_description) (void);


  /*
   * return short, human readable identifier for this plugin
   * this is used for GUI buttons, The identifier must have max. 4 characters
   * characters (max. 5 including terminating \0)
   */
  char* (*get_identifier) (void);


  /*
   * generate autoplay list
   * return value: list of MRLs
   */
  char** (*get_autoplay_list) (int *nFiles);


  /*
   * gets the subtitle/menu palette
   */
  clut_t* (*get_clut) (void);


} input_plugin_t;

#define INPUT_CAP_SEEKABLE 1
#define INPUT_CAP_BLOCK    2
#define INPUT_CAP_AUTOPLAY 4
#define INPUT_CAP_CLUT     8


/*
 * init/get plugin structure
 *
 * try to initialize the plugin with given interface version
 * and configuration options
 */
input_plugin_t *get_input_plugin (int requested_interface,
				 config_values_t *config);



#endif
