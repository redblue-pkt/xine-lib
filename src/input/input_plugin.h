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
 * $Id: input_plugin.h,v 1.8 2001/07/01 23:37:04 guenter Exp $
 */

#ifndef HAVE_INPUT_PLUGIN_H
#define HAVE_INPUT_PLUGIN_H

#include <inttypes.h>
#include <sys/types.h>
#include "buffer.h"
#include "configfile.h"

#define INPUT_PLUGIN_IFACE_VERSION   2
 
#ifndef CLUT_T
#define CLUT_T
typedef struct {         /* CLUT == Color LookUp Table */
	uint8_t foo		: 8; /* UNKNOWN: 0x00? */
	uint8_t y		: 8;
	uint8_t cr		: 8;
	uint8_t cb		: 8;
} __attribute__ ((packed)) clut_t;
#endif

#define MAX_MRL_ENTRIES 255

/* Types of mrls returned by get_dir() */
#define mrl_unknown       0x0
#define mrl_dvd           0x1
#define mrl_vcd           0x3
#define mrl_net           0x4
#define mrl_rtp           0x5
#define mrl_stdin         0x6
#define mrl_fifo          0x7
#define mrl_chardev       0x8
#define mrl_directory     0x9
#define mrl_blockdev      0xA
#define mrl_normal        0xB
#define mrl_symbolic_link 0xC
#define mrl_sock          0xD
/* bit for exec file, should be combinated with mrl_normal type*/
#define mrl_type_exec          0xFFFF8000 

typedef struct {
  char *mrl;        /* <type>://<location              */
  int   type;       /* match to mrl_type enum          */
  off_t size;       /* size of this source, may be 0   */
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
   * Request optional datas from input plugin.
   */
  int (*get_optional_data) (input_plugin_t *this, void *data, int data_type);

  /*
   * deliver an input event (mouse press/move, keypress)
   * optional: may be NULL
   */
  void (*handle_input_event) (input_plugin_t *this, int event_type, int key,
			      int x, int y);

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


#define INPUT_OPTIONAL_UNSUPPORTED    0
#define INPUT_OPTIONAL_SUCCESS        1

#define INPUT_OPTIONAL_DATA_CLUT      1
#define INPUT_OPTIONAL_DATA_AUDIOLANG 2

#define INPUT_EVENT_MOUSEBUTTON 1
#define INPUT_EVENT_KEYPRESS    2
#define INPUT_EVENT_MOUSEMOVE   3

#endif

