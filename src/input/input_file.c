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
 * $Id: input_file.c,v 1.7 2001/05/03 00:02:42 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "xine_internal.h"
#include "monitor.h"
#include "input_plugin.h"

static uint32_t xine_debug;

typedef struct file_input_plugin_s {
  input_plugin_t   input_plugin;
  
  int              fh;
  char            *mrl;
  config_values_t *config;
} file_input_plugin_t;

static uint32_t file_plugin_get_capabilities (input_plugin_t *this_gen) {
#warning "remove AUTOPLAY capability."
  return INPUT_CAP_SEEKABLE | INPUT_CAP_AUTOPLAY;
}

static int file_plugin_open (input_plugin_t *this_gen, char *mrl) {

  char                *filename;
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  this->mrl = mrl;

  if (!strncasecmp (mrl, "file:",5))
    filename = &mrl[5];
  else
    filename = mrl;

  xprintf (VERBOSE|INPUT, "Opening >%s<\n",filename);

  this->fh = open (filename, O_RDONLY);

  if (this->fh == -1) {
    return 0;
  }

  return 1;
}


static off_t file_plugin_read (input_plugin_t *this_gen, char *buf, off_t len) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;
  return read (this->fh, buf, len);
}

static buf_element_t *file_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t todo) {

  off_t                 num_bytes, total_bytes;
  file_input_plugin_t  *this = (file_input_plugin_t *) this_gen;
  buf_element_t        *buf = fifo->buffer_pool_alloc (fifo);

  buf->content = buf->mem;
  total_bytes = 0;

  while (total_bytes < todo) {
    num_bytes = read (this->fh, buf->mem + total_bytes, todo-total_bytes);
    total_bytes += num_bytes;
    if (!num_bytes) {
      buf->free_buffer (buf);
      return NULL;
    }
  }

  buf->size = total_bytes;

  return buf;
}


static off_t file_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  return lseek (this->fh, offset, origin);
}


static off_t file_plugin_get_current_pos (input_plugin_t *this_gen){
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  return lseek (this->fh, 0, SEEK_CUR);
}


static off_t file_plugin_get_length (input_plugin_t *this_gen) {

  struct stat          buf ;
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  if (fstat (this->fh, &buf) == 0) {
    return buf.st_size;
  } else
    perror ("system call fstat");
  return 0;
}

static uint32_t file_plugin_get_blocksize (input_plugin_t *this_gen) {
  return 0;
}

static char **file_plugin_get_dir (input_plugin_t *this_gen, char *filename, int *nFiles) {
  /* not yet implemented */

  printf ("input_file : get_dir () not implemented yet!\n");

  return NULL;
}

static int file_plugin_eject_media (input_plugin_t *this_gen) {
  return 1; /* doesn't make sense */
}

static char* file_plugin_get_mrl (input_plugin_t *this_gen) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  return this->mrl;
}

static void file_plugin_close (input_plugin_t *this_gen) {
  file_input_plugin_t *this = (file_input_plugin_t *) this_gen;

  xprintf (VERBOSE|INPUT, "closing input\n");

  close(this->fh);
  this->fh = -1;
}


static char *file_plugin_get_description (input_plugin_t *this_gen) {
  return "plain file input plugin as shipped with xine";
}


static char *file_plugin_get_identifier (input_plugin_t *this_gen) {
  return "file";
}

input_plugin_t *init_input_plugin (int iface, config_values_t *config) {

  file_input_plugin_t *this;

  xine_debug = config->lookup_int (config, "xine_debug", 0);

  switch (iface) {
  case 1:
    this = (file_input_plugin_t *) malloc (sizeof (file_input_plugin_t));

    this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
    this->input_plugin.get_capabilities  = file_plugin_get_capabilities;
    this->input_plugin.open              = file_plugin_open;
    this->input_plugin.read              = file_plugin_read;
    this->input_plugin.read_block        = file_plugin_read_block;
    this->input_plugin.seek              = file_plugin_seek;
    this->input_plugin.get_current_pos   = file_plugin_get_current_pos;
    this->input_plugin.get_length        = file_plugin_get_length;
    this->input_plugin.get_blocksize     = file_plugin_get_blocksize;
    this->input_plugin.get_dir           = file_plugin_get_dir;
    this->input_plugin.eject_media       = file_plugin_eject_media;
    this->input_plugin.get_mrl           = file_plugin_get_mrl;
    this->input_plugin.close             = file_plugin_close;
    this->input_plugin.get_description   = file_plugin_get_description;
    this->input_plugin.get_identifier    = file_plugin_get_identifier;
    this->input_plugin.get_autoplay_list = NULL;
    this->input_plugin.get_clut          = NULL;

    this->fh      = -1;
    this->mrl     = NULL;
    this->config  = config;

    return (input_plugin_t *) this;
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

