/* 
 * Copyright (C) 2000-2001 the xine project
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
 * $Id: input_stdin_fifo.c,v 1.4 2001/05/07 01:31:44 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "xine_internal.h"
#include "monitor.h"
#include "input_plugin.h"

static uint32_t xine_debug;

typedef struct {
  input_plugin_t   input_plugin;
  
  int              fh;
  char            *mrl;
  config_values_t *config;
  off_t            curpos;

} stdin_input_plugin_t;

/*
 *
 */
static int stdin_plugin_open(input_plugin_t *this_gen, char *mrl) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;
  char *filename;
  char *pfn;

  this->mrl = mrl;

  if(!strncasecmp(mrl, "stdin:", 6) 
      || !strncmp(mrl, "-", 1)) {
    filename = "/dev/stdin";
  } 
  else if(!strncasecmp(mrl, "fifo:", 5)) {

    if((pfn = strrchr((mrl+5), ':')) != NULL) {
      filename = ++pfn;
    }
    else {
      filename = (char *) &mrl[5];
    }

  } 
  else {
    /* filename = (char *) mrl; */
    return 0;
  }
  
  xprintf (VERBOSE|INPUT, "Opening >%s<\n",filename);

  this->fh = open (filename, O_RDONLY);
  this->curpos = 0;
  
  if(this->fh == -1) {
    return 0;
  }

  return 1;
}

/*
 *
 */
static off_t stdin_plugin_read (input_plugin_t *this_gen, 
				      char *buf, off_t nlen) {

  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;
  off_t n, nBytesRead = 0;

  while (nBytesRead < nlen) {
    n = read(this->fh, &buf[nBytesRead], nlen-nBytesRead);

    if (n < 0) {
      this->curpos += n;
      return n;
    }
    else if (!n) {
      this->curpos += nBytesRead;
      return nBytesRead;
    }

    nBytesRead += n;
  }

  this->curpos += nBytesRead;
  return nBytesRead;
}

/*
 *
 */
static off_t stdin_plugin_get_length(input_plugin_t *this_gen) {

  return 0;
}

/*
 *
 */
static uint32_t stdin_plugin_get_capabilities(input_plugin_t *this_gen) {
  
  return INPUT_CAP_NOCAP;
}

/*
 *
 */
static uint32_t stdin_plugin_get_blocksize(input_plugin_t *this_gen) {

  return 0;
}

/*
 *
 */
static off_t stdin_plugin_get_current_pos (input_plugin_t *this_gen){
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  return this->curpos;
}

/*
 *
 */
static int stdin_plugin_eject_media(input_plugin_t *this_gen) {
  return 1;
}

/*
 *
 */
static char* stdin_plugin_get_mrl (input_plugin_t *this_gen) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  return this->mrl;
}

/*
 *
 */
static void stdin_plugin_close(input_plugin_t *this_gen) {
  stdin_input_plugin_t *this = (stdin_input_plugin_t *) this_gen;

  close(this->fh);
  this->fh = -1;
}

/*
 *
 */
static char *stdin_plugin_get_description (input_plugin_t *this_gen) {
  return "stdin/fifo input plugin as shipped with xine";
}

/*
 *
 */
static char *stdin_plugin_get_identifier(input_plugin_t *this_gen) {
  return "stdin_fifo";
}

/*
 *
 */
static int stdin_plugin_get_optional_data (input_plugin_t *this_gen, 
					   void *data, int data_type) {

  return INPUT_OPTIONAL_UNSUPPORTED;
}

/*
 *
 */
input_plugin_t *init_input_plugin (int iface, config_values_t *config) {

  stdin_input_plugin_t *this;

  xine_debug = config->lookup_int (config, "xine_debug", 0);

  switch (iface) {
  case 1:
    this = (stdin_input_plugin_t *) malloc (sizeof (stdin_input_plugin_t));

    this->input_plugin.interface_version = INPUT_PLUGIN_IFACE_VERSION;
    this->input_plugin.get_capabilities  = stdin_plugin_get_capabilities;
    this->input_plugin.open              = stdin_plugin_open;
    this->input_plugin.read              = stdin_plugin_read;
    this->input_plugin.read_block        = NULL;
    this->input_plugin.seek              = NULL;
    this->input_plugin.get_current_pos   = stdin_plugin_get_current_pos;
    this->input_plugin.get_length        = stdin_plugin_get_length;
    this->input_plugin.get_blocksize     = stdin_plugin_get_blocksize;
    this->input_plugin.get_dir           = NULL;
    this->input_plugin.eject_media       = stdin_plugin_eject_media;
    this->input_plugin.get_mrl           = stdin_plugin_get_mrl;
    this->input_plugin.close             = stdin_plugin_close;
    this->input_plugin.get_description   = stdin_plugin_get_description;
    this->input_plugin.get_identifier    = stdin_plugin_get_identifier;
    this->input_plugin.get_autoplay_list = NULL;
    this->input_plugin.get_optional_data = stdin_plugin_get_optional_data;

    this->fh      = -1;
    this->mrl     = NULL;
    this->config  = config;
    this->curpos  = 0;

    return (input_plugin_t *) this;
    break;
  default:
    fprintf(stderr,
	    "Stdin input plugin doesn't support plugin API version %d.\n"
	    "PLUGIN DISABLED.\n"
	    "This means there's a version mismatch between xine and this input"
	    "plugin.\nInstalling current input plugins should help.\n",
	    iface);
    return NULL;
  }
}
/* ------------------------------------------------------------------------- */
