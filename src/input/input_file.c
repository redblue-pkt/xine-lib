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
 * $Id: input_file.c,v 1.1 2001/04/18 22:34:04 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "xine.h"
#include "monitor.h"
#include "input_plugin.h"


static uint32_t xine_debug;
static int input_file_handle;
static char *input_file_mrl;

static uint32_t file_plugin_get_capabilities () {
  return INPUT_CAP_SEEKABLE;
}

static int file_plugin_open (char *mrl) {

  char *filename;

  input_file_mrl = mrl;

  if (!strncasecmp (mrl, "file:",5))
    filename = &mrl[5];
  else
    filename = mrl;

  xprintf (VERBOSE|INPUT, "Opening >%s<\n",filename);

  input_file_handle = open (filename, O_RDONLY);

  if (input_file_handle == -1) {
    return 0;
  }

  return 1;
}


static off_t file_plugin_read (char *buf, off_t len) {
  return read (input_file_handle, buf, len);
}

static buf_element_t *file_plugin_read_block (fifo_buffer_t *fifo, off_t todo) {

  off_t num_bytes, total_bytes;
  buf_element_t *buf = fifo->buffer_pool_alloc ();

  buf->content = buf->mem;
  total_bytes = 0;

  while (total_bytes < todo) {
    num_bytes = read (input_file_handle, buf->mem + total_bytes, todo-total_bytes);
    total_bytes += num_bytes;
    if (!num_bytes) {
      buf->free_buffer (buf);
      return NULL;
    }
  }

  return buf;
}


static off_t file_plugin_seek (off_t offset, int origin) {
  return lseek (input_file_handle, offset, origin);
}


static off_t file_plugin_get_current_pos (){
  return lseek (input_file_handle, 0, SEEK_CUR);
}


static off_t file_plugin_get_length (void) {
  struct stat buf ;

  if (fstat (input_file_handle, &buf) == 0) {
    return buf.st_size;
  } else
    perror ("system call fstat");
  return 0;
}

static uint32_t file_plugin_get_blocksize () {
  return 0;
}

static char **file_plugin_get_dir (char *filename, int *nFiles) {
  /* not yet implemented */

  printf ("input_file : get_dir () not implemented yet!\n");

  return NULL;
}

static int file_plugin_eject_media () {
  return 1; /* doesn't make sense */
}

static char* file_plugin_get_mrl () {
  return input_file_mrl;
}

static void file_plugin_close (void) {
  xprintf (VERBOSE|INPUT, "closing input\n");

  close(input_file_handle);
  input_file_handle = -1;
}


static char *file_plugin_get_description (void) {
  return "plain file input plugin as shipped with xine";
}


static char *file_plugin_get_identifier (void) {
  return "file";
}


static input_plugin_t plugin_info = {
  INPUT_INTERFACE_VERSION,
  file_plugin_get_capabilities,
  file_plugin_open,
  file_plugin_read,
  file_plugin_read_block,
  file_plugin_seek,
  file_plugin_get_current_pos,
  file_plugin_get_length,
  file_plugin_get_blocksize,
  file_plugin_get_dir,
  file_plugin_eject_media,
  file_plugin_get_mrl,
  file_plugin_close,
  file_plugin_get_description,
  file_plugin_get_identifier,
  NULL, /* autoplay */
  NULL /* clut */
};


input_plugin_t *get_input_plugin (int iface, config_values_t *config) {

  /* FIXME: set debug level (from config?) */

  switch (iface) {
  case 1:
    input_file_handle = -1;
    return &plugin_info;
    break;
  default:
    fprintf(stderr,
	    "File input plugin doesn't support plugin API version %d.\n"
	    "PLUGIN DISABLED.\n"
	    "This means there's a version mismatch between xine and this input"
	    "plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }
}
